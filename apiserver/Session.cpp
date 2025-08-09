#include "Session.h"

#include <boost/beast/websocket.hpp>
#include <boost/beast/core/bind_handler.hpp>
#include "WSClientManager.h"
#include "Websocket.h"

namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>

namespace Rest
{

  Session::Session(
      tcp::socket &&socket,
      std::shared_ptr<std::string const> const &doc_root,
      std::shared_ptr<RouteHandler> route_handler,
      std::shared_ptr<RedisPublish::Sender> redisPublish,
      WSClientManager &wsclient_manager,
      io_context &ioc)
      : stream_(std::move(socket)),
        ioc_(ioc),
        doc_root_(doc_root),
        m_route_handler(route_handler),
        m_redisPublish(redisPublish),
        m_wsclient_manager(wsclient_manager),
        strand_(boost::asio::make_strand(ioc)), // Initialize the strand
        m_db_client{}

  {
  }

  Session::~Session()
  {
    if (m_db_client)
    {
      m_db_client.reset();
    }
  }

  // Start the asynchronous operation
  void Session::run()
  {
    net::dispatch(strand_,
                  beast::bind_front_handler(
                      &Session::do_read,
                      shared_from_this()));
  }

  void Session::do_read()
  {
    // Make the request empty before reading, otherwise the operation behavior is undefined.
    // Construct a new parser for each message
    parser_.emplace();

    // Apply a reasonable limit to the allowed size of the body in bytes to prevent abuse.
    parser_->body_limit(1000000);

    // Set the timeout.
    stream_.expires_after(std::chrono::seconds(30));

    // Read a request
    http::async_read(stream_, buffer_, *parser_,
                     beast::bind_front_handler(
                         &Session::on_read,
                         shared_from_this()));
  }

  void Session::on_read(
      beast::error_code ec,
      std::size_t bytes_transferred)
  {
    boost::ignore_unused(bytes_transferred);

    // This means they closed the connection
    if (ec == http::error::end_of_stream)
    {
      return do_close();
    }

    if (ec)
      return fail(ec, "read");

    // See if it is a WebSocket Upgrade
    if (websocket::is_upgrade(parser_->get()))
    {
      try
      {
        auto [route_url, query_params] = Rest::RouteHandler::parse_query_params(std::string(parser_->get().target()));
        auto incrID = m_wsclient_manager.IncrementID();
        std::stringstream ss;
        auto web_user = query_params.contains("user") ? query_params["user"] : "webclient";
        auto qp_type = query_params.contains("type") ? query_params["type"] : "all";
        ss.clear();
        ss << web_user << "_" << incrID;
        auto user = ss.str();
        std::cout << " o Route url      " << route_url << " found for websocket upgrade" << std::endl
                  << " o WSMgr incr Id: " << incrID << std::endl
                  << " o User WS:       " << user << std::endl
                  << " o User:          " << web_user << std::endl
                  << " o Type:          " << qp_type << std::endl;
        // Create a websocket session, transferring ownership
        // of both the socket and the HTTP request.
        auto ws = std::make_shared<websocket_session>(
            stream_.release_socket(),
            user,
            m_wsclient_manager,
            ioc_);

        m_wsclient_manager.RegisterUser(
            user,
            std::move(ws));
        auto ws_session = m_wsclient_manager.GetSession(user);
        if (ws_session)
          ws_session->do_accept(parser_->release());
        else
          fail(ec, "register wsclient");
      }
      catch (...)
      {
        std::cerr << "Create web session error" << std::endl;
      }
      return;
    }

    boost::asio::dispatch(strand_,
                          [self = shared_from_this()]()
                          {
                            // Store the released request in m_request
                            self->m_request = self->parser_->release();
                            // Check the payload size
                            D(if (auto size = self->m_request.payload_size()) { std::cout << "Request payload size (from Content-Length): " << *size << " bytes" << std::endl; } else {
                              std::cout << "Request payload size is unknown (no Content-Length header)" << std::endl;
                              std::cout << "Request body size (parsed): " << self->m_request.body().size() << " bytes" << std::endl; })

                            // Send the response
                            auto send = [self](auto &&msg)
                            {
                              auto sp = std::make_shared<std::decay_t<decltype(msg)>>(std::forward<decltype(msg)>(msg));
                              self->res_ = sp;

                              http::async_write(
                                  self->stream_,
                                  *sp,
                                  beast::bind_front_handler(&Session::on_write, self, sp->need_eof()));
                            };

                            self->handle_request(std::move(send));
                          });
  }

  void Session::on_write(
      bool close,
      beast::error_code ec,
      std::size_t bytes_transferred)
  {
    boost::ignore_unused(bytes_transferred);

    if (ec)
      return fail(ec, "write");

    if (close)
    {
      // This means we should close the connection, usually because
      // the response indicated the "Connection: close" semantic.
      return do_close();
    }

    // We're done with the response so delete it
    res_ = nullptr;

    // Read another request
    // Dispatch do_read through the strand
    boost::asio::dispatch(strand_,
                          beast::bind_front_handler(
                              &Session::do_read,
                              shared_from_this()));
  }

  void Session::do_close()
  {
    // Send a TCP shutdown
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    D(std::cout << "### session close\n";)
    // At this point the connection is closed gracefully
  }

  std::string Session::path_cat(beast::string_view base, beast::string_view path)
  {
    if (base.empty())
      return std::string(path);
    std::string result(base);
#ifdef BOOST_MSVC
    char constexpr path_separator = '\\';
    if (result.back() == path_separator)
      result.resize(result.size() - 1);
    result.append(path.data(), path.size());
    for (auto &c : result)
      if (c == '/')
        c = path_separator;
#else
    char constexpr path_separator = '/';
    if (result.back() == path_separator)
      result.resize(result.size() - 1);
    result.append(path.data(), path.size());
#endif
    return result;
  }

  beast::string_view Session::mime_type(beast::string_view path)
  {
    using beast::iequals;
    auto const ext = [&path]
    {
      auto const pos = path.rfind(".");
      if (pos == beast::string_view::npos)
        return beast::string_view{};
      return path.substr(pos);
    }();
    if (iequals(ext, ".htm"))
      return "text/html";
    if (iequals(ext, ".html"))
      return "text/html";
    if (iequals(ext, ".php"))
      return "text/html";
    if (iequals(ext, ".css"))
      return "text/css";
    if (iequals(ext, ".txt"))
      return "text/plain";
    if (iequals(ext, ".js"))
      return "application/javascript";
    if (iequals(ext, ".json"))
      return "application/json";
    if (iequals(ext, ".xml"))
      return "application/xml";
    if (iequals(ext, ".swf"))
      return "application/x-shockwave-flash";
    if (iequals(ext, ".flv"))
      return "video/x-flv";
    if (iequals(ext, ".png"))
      return "image/png";
    if (iequals(ext, ".jpe"))
      return "image/jpeg";
    if (iequals(ext, ".jpeg"))
      return "image/jpeg";
    if (iequals(ext, ".jpg"))
      return "image/jpeg";
    if (iequals(ext, ".gif"))
      return "image/gif";
    if (iequals(ext, ".bmp"))
      return "image/bmp";
    if (iequals(ext, ".ico"))
      return "image/vnd.microsoft.icon";
    if (iequals(ext, ".tiff"))
      return "image/tiff";
    if (iequals(ext, ".tif"))
      return "image/tiff";
    if (iequals(ext, ".svg"))
      return "image/svg+xml";
    if (iequals(ext, ".svgz"))
      return "image/svg+xml";
    return "application/text";
  }

}