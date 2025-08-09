
#ifndef SRC_APP_REST_SESSION_H_
#define SRC_APP_REST_SESSION_H_

#include <boost/beast/version.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/file_body.hpp>
#include <boost/beast/http/parser.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>

#include "RouteHandler.h"
#include "PQClient.h"
#include "../redisPublish/Publish.h" // RedisPublish class

#include "ErrorHandler.h"
#include "Response.h"

#include <iostream>
#include <memory>
#include <string>
#include <sstream>

namespace beast = boost::beast;      // from <boost/core/error.hpp>
namespace http = boost::beast::http; // from <boost/beast/http.hpp>
namespace net = boost::asio;         // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;    // from <boost/asio/ip/tcp.hpp>
using Rest::Parameters;
using MatchRoute = std::pair<std::unique_ptr<HttpRoute>, Parameters>;
using Rest::PQClient;

namespace Rest
{

  class WSClientManager;

  // Report a failure
  void fail(beast::error_code ec, char const *what);

  // Handles an HTTP server connection
  class Session : public std::enable_shared_from_this<Session>
  {

    beast::tcp_stream stream_;
    io_context &ioc_;
    beast::flat_buffer buffer_;
    std::shared_ptr<std::string const> doc_root_;
    std::shared_ptr<RouteHandler> m_route_handler;
    std::shared_ptr<RedisPublish::Sender> m_redisPublish;
    WSClientManager &m_wsclient_manager;
    net::strand<net::io_context::executor_type> strand_; // Strand for thread safety
    std::shared_ptr<PQClient> m_db_client;
    std::shared_ptr<void> res_;
    boost::optional<http::request_parser<http::string_body>> parser_;
    http::request<http::string_body> m_request;
    Parameters m_parameters{};

  public:
    // Take ownership of the stream
    Session(tcp::socket &&socket,
            std::shared_ptr<std::string const> const &doc_root,
            std::shared_ptr<RouteHandler> route_handler,
            std::shared_ptr<RedisPublish::Sender> redisPublish,
            WSClientManager &wsclient_manager,
            io_context &ioc);

    ~Session();

    const Parameters &getReqUrlParameters() const
    {
      return m_parameters;
    }

    // Start the asynchronous operation
    void run();

    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred);
    void do_close();

    template <class Send>
    void handle_request(Send send)
    {
      boost::asio::dispatch(strand_, [self = shared_from_this(), send]() mutable
                            {
                              if (self->m_request.method() == http::verb::options)
                              {
                                std::cout << "HandleRequest  send allow origin: " << string(self->m_request.target()) << "\n";
                                // Respond to GET request
                                http::response<http::string_body> res{http::status::ok, self->m_request.version()};
                                res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                                Rest::Response::add_cors_headers(res);
                                res.prepare_payload();
                                return send(std::move(res));
                              }

                              if (!self->m_db_client)
                              {
                                auto apidb_name = std::getenv("APIDB_NAME");
                                auto apidb_user = std::getenv("APIDB_USER");
                                auto apidb_password = std::getenv("APIDB_PASSWORD");
                                auto apidb_host = std::getenv("APIDB_HOST");
                                auto apidb_port = std::getenv("APIDB_PORT");
                                uint16_t dbport = static_cast<uint16_t>(std::atoi(apidb_port));
                                self->m_db_client = std::make_shared<PQClient>(self->ioc_,
                                                                               self->strand_,
                                                                               std::string(apidb_name),
                                                                               std::string(apidb_user),
                                                                               std::string(apidb_password),
                                                                               std::string(apidb_host),
                                                                               dbport);
                                // return send(Rest::Response::server_error(self->m_request, "Database client has failed "));
                              }
                              try
                              {
                                MatchRoute route = self->m_route_handler->match({static_cast<int>(self->m_request.method()), std::string(self->m_request.target())});
                                self->m_parameters = route.second;
                                if (self->m_request.method() == http::verb::put)
                                {
                                  route.first->do_route(self, self->m_db_client, self->m_redisPublish, self->m_request, std::move(send));
                                  return;
                                }

                                if (self->m_request.method() == http::verb::get)
                                {
                                  // HttpRequest httpRequest(self, std::move(req), send);
                                  // for (const auto &field : self->m_request.base())
                                  // {
                                  //   std::cout << "  ---- Field: " << field.name_string() << ": " << field.value() << std::endl;
                                  // }

                                  route.first->do_route(self, self->m_db_client, self->m_redisPublish, self->m_request, std::move(send));
                                  return;
                                }

                                if (self->m_request.method() == http::verb::delete_)
                                {
                                  // HttpRequest httpRequest(self, std::move(req), send);
                                  // for (const auto &field : self->m_request.base())
                                  // {
                                  //   std::cout << "  ---- Field: " << field.name_string() << ": " << field.value() << std::endl;
                                  // }

                                  route.first->do_route(self, self->m_db_client, self->m_redisPublish, self->m_request, std::move(send));
                                  return;
                                }
                              }
                              catch (const std::exception &e)
                              {
                                D(std::cerr << e.what() << " " << self->m_request.target() << std::endl;)
                              }

                              ///=======================================================
                              // not a registered route with req.target
                              ///=======================================================
                              auto &req = self->m_request;

                              // Use target as api url first
                              std::string path = string(req.target());

                              // Make sure we can handle the method
                              if (req.method() != http::verb::get && req.method() != http::verb::head)
                                return send(Rest::Response::bad_request(req, "Unknown HTTP-method"));

                              // Request path must be absolute and not contain "..".
                              if (req.target().empty() || req.target()[0] != '/' || req.target().find("..") != beast::string_view::npos)
                                return send(Rest::Response::bad_request(req, "Illegal request-target"));

                              // Build the path to the requested file
                              path = self->path_cat(*self->doc_root_, req.target());
                              if (req.target().back() == '/')
                                path.append("index.html");

                              //================================================================================
                              // Attempt to open the file
                              //================================================================================
                              D(std::cerr << path.c_str() << std::endl;)

                              beast::error_code ec;
                              http::file_body::value_type body;
                              body.open(path.c_str(), beast::file_mode::scan, ec);

                              // Handle the case where the file doesn't exist
                              if (ec == beast::errc::no_such_file_or_directory)
                                return send(Rest::Response::not_found(req, req.target()));

                              // Handle an unknown error
                              if (ec)
                                return send(Rest::Response::server_error(req, ec.message()));

                              // Cache the size since we need it after the move
                              auto const size = body.size();

                              // Respond to HEAD request
                              // if (req.method() == http::verb::head)
                              // {
                              //   http::response<http::empty_body> res{http::status::ok, req.version()};
                              //   res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                              //   res.set(http::field::content_type, self->mime_type(path));
                              //   res.content_length(size);
                              //   res.keep_alive(req.keep_alive());
                              //   send(std::move(res));
                              //   return;
                              // }

                              // Respond to GET request
                              http::response<http::file_body>
                                  res{std::piecewise_construct, std::make_tuple(std::move(body)), std::make_tuple(http::status::ok, req.version())};
                              res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                              res.set(http::field::content_type, self->mime_type(path));
                              res.content_length(size);
                              res.keep_alive(req.keep_alive());
                              return send(std::move(res));

                              //
                            });
    }

    std::string
    path_cat(beast::string_view base, beast::string_view path);

    // Return a reasonable mime type based on the extension of a file.
    beast::string_view mime_type(beast::string_view path);
  };
}
#endif /* SRC_APP_REST_SESSION_H_ */