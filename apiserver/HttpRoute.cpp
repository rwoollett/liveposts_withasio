#include "HttpRoute.h"

namespace Rest
{

  HttpRoute::~HttpRoute() {};

  void default_http_route_func(std::shared_ptr<Session> session,
                               std::shared_ptr<PQClient> dbclient,
                               std::shared_ptr<RedisPublish::Sender> redisPublish,
                               const http::request<http::string_body> &req,
                               SendCall &&send)
  {
    throw std::runtime_error("Not implemented");
  };
}
