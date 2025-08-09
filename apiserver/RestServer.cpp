
#include "RestServer.h"
#include "Session.h"
#include <boost/asio/strand.hpp>
#include <boost/beast/core/bind_handler.hpp>

namespace net = boost::asio;

namespace Rest
{
  RestServer::RestServer(
      io_context &ioc,
      tcp::endpoint endpoint,
      std::shared_ptr<std::string const> const &doc_root,
      std::shared_ptr<RedisPublish::Sender> sender)
      : ioc_(ioc),
        acceptor_(net::make_strand(ioc)),
        doc_root_(doc_root),
        m_route_handler{new RouteHandler()},
        m_redisPublishSend{sender},
        m_wsclient_manager()

  {
    beast::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec)
    {
      fail(ec, "open");
      return;
    }

    // Allow address reuse
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec)
    {
      fail(ec, "set_option");
      return;
    }

    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec)
    {
      fail(ec, "bind");
      return;
    }

    // Start listening for connections
    acceptor_.listen(
        net::socket_base::max_listen_connections, ec);
    if (ec)
    {
      fail(ec, "listen");
      return;
    }
  }

  RestServer::~RestServer()
  {
    D(std::cout << "Restserver destr: wsclient manager websocket clients count:" << m_wsclient_manager.Size()
                << std::endl;)
  }

  void RestServer::do_accept()
  {
    //  The new connection gets its own strand
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(
            &RestServer::on_accept,
            shared_from_this()));
  }

  void RestServer::on_accept(beast::error_code ec, tcp::socket socket)
  {
    // Check whether the server was stopped by a signal before this
    // completion handler had a chance to run.
    if (!acceptor_.is_open())
    {
      return;
    }
    if (ec)
    {
      fail(ec, "accept");
      return;
    }
    else
    {
      try
      {
        // Create the session and run it
        D(std::cout << "rest server on accept listener m_route_handler.use_count() = " << m_route_handler.use_count() << '\n';)
        std::make_shared<Session>(std::move(socket), doc_root_,
                                  m_route_handler->shared_from_this(),
                                  m_redisPublishSend,
                                  m_wsclient_manager,
                                  ioc_)
            ->run();
      }
      catch (std::string error)
      {
        std::cerr << "Error creating session!! " << error << std::endl;
        fail(ec, std::string(std::string("session create ") + error).c_str());
        return;
      }
      catch (...)
      {
        std::cerr << "Error creating session!! " << std::endl;
        fail(ec, "session create unknown why");
        return;
      }
    }

    // Accept another connection
    do_accept();
  }

  void RestServer::get(const std::string &route, HttpRoute::RouteCall callback)
  {
    m_route_handler->declare_route_handler({static_cast<int>(http::verb::get), route}, callback);
  };

  void RestServer::remove(const std::string &route, HttpRoute::RouteCall callback)
  {
    m_route_handler->declare_route_handler({static_cast<int>(http::verb::delete_), route}, callback);
  };

  void RestServer::put(const std::string &route, HttpRoute::RouteCall callback)
  {
    m_route_handler->declare_route_handler({static_cast<int>(http::verb::put), route}, callback);
  };
}
