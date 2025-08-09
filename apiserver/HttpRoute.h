//
// Created by rodney on 17/04/2021.
//

#ifndef SRC_RESTHTTPROUTE_H
#define SRC_RESTHTTPROUTE_H

#include "Route.h"
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <functional>
#include <memory>

namespace http = boost::beast::http; 
using Rest::Route;

namespace RedisPublish {
  class Sender;
}
namespace Rest
{
  class Session;
  class PQClient;
  using SendCall = std::function<void(http::response<http::string_body> &&msg)>;

  void default_http_route_func(
      std::shared_ptr<Session> session,
      std::shared_ptr<PQClient> dbclient,
      std::shared_ptr<RedisPublish::Sender> redisPublish,
      const http::request<http::string_body> &req,
      SendCall &&send);

  class HttpRoute : public Route
  {

  public:
    using RouteCall = std::function<void(std::shared_ptr<Session> session,
                                         std::shared_ptr<PQClient> dbclient,
                                         std::shared_ptr<RedisPublish::Sender> redisPublish,
                                         const http::request<http::string_body> &req,
                                         SendCall &&send)>;
    HttpRoute() : m_routeFunc(default_http_route_func) {};

    virtual ~HttpRoute();
    inline virtual std::string request_id() override
    {
      return "HttpRequest";
    };

    void register_route_callback(RouteCall callback)
    {
      m_routeFunc = callback;
    };
    void do_route(std::shared_ptr<Session> session,
                  std::shared_ptr<PQClient> dbclient,
                  std::shared_ptr<RedisPublish::Sender> redisPublish,
                  const http::request<http::string_body> &req,
                  SendCall &&send)
    {
      return m_routeFunc(session, dbclient, redisPublish, req, std::move(send));
    };

  private:
    RouteCall m_routeFunc;
  };

}

#endif /* SRC_RESTHTTPROUTE_H */