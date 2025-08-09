/*
 * RestServer.h
 *
 *  Created on: 26/08/2017
 *      Author: rodney
 */

#ifndef SRC_APP_RESTSERVER_H_
#define SRC_APP_RESTSERVER_H_

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/error.hpp>
#include "RouteHandler.h"
#include "WSClientManager.h"
#include "../redisPublish/Publish.h" // RedisPublish class
#include "ErrorHandler.h"
#include <cstdlib>
#include <memory>
#include <string>

namespace beast = boost::beast;   // from <boost/beast/core/error.hpp>
using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

using boost::asio::io_context;
using Rest::RouteHandler;
using Rest::HttpRoute;
using Rest::Parameters;
using Rest::PQClient;

namespace Rest
{

  // Accepts incoming connections and launches the sessions
  class RestServer : public std::enable_shared_from_this<RestServer>
  {
    io_context &ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<std::string const> doc_root_;
    std::shared_ptr<RouteHandler> m_route_handler;
    std::shared_ptr<RedisPublish::Sender> m_redisPublishSend;
    WSClientManager m_wsclient_manager;

  public:
    RestServer(io_context &ioc,
               tcp::endpoint endpoint,
               std::shared_ptr<std::string const> const &doc_root,
               std::shared_ptr<RedisPublish::Sender> sender);

    ~RestServer();

    void get(const std::string &route, HttpRoute::RouteCall);
    void remove(const std::string &route, HttpRoute::RouteCall);
    void put(const std::string &route, HttpRoute::RouteCall);

    // Start accepting incoming connections
    void run()
    {
      do_accept();
    }

  private:
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);

  };
}
#endif /* SRC_APP_RESTSERVER_H_ */
