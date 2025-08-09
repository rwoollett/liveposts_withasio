
#ifndef SRC_RESTROUTEHANDLER_H_
#define SRC_RESTROUTEHANDLER_H_

#include "RouteFactorys.h"
#include "Parameters.h"
#include <map>
#include <vector>
#include <functional>

using Rest::HttpRoute;
using Rest::Parameters;
using SegmontAtC = std::pair<std::string::const_iterator, std::string>;
using SegmontList = std::vector<std::string>;
using std::hash;

namespace Rest
{

  struct RoutePath
  {
    int method;
    std::string path;
  };

  struct RouteHandle
  {
    std::string m_static_name;
    SegmontList m_param_names;
  };

  using RouteFactory = Rest::Factory::RoutesFactory<RoutePath>;

  class RouteHandler : public std::enable_shared_from_this<RouteHandler>
  {
    using RouteHandlers = std::vector<RouteHandle>;
    RouteFactory m_route_factory{
        150,
        [](const RoutePath &r)
        {
          return hash<int>()(static_cast<int>(r.method)) ^ hash<string>()(r.path);
        },
        [](const RoutePath &r, const RoutePath &r2)
        {
          return r.method == r2.method && r.path == r2.path;
        }};
    RouteHandlers m_handlers{};

  public:
    std::pair<std::unique_ptr<HttpRoute>, Parameters> match(const RoutePath &path) const;

    void declare_route_handler(const RoutePath &path, HttpRoute::RouteCall callback);

    void MethodNameFromEnum(const Rest::RoutePath &route_path, std::string &method_name) const;

    static std::tuple<std::string, std::map<std::string, std::string>> parse_query_params(const std::string &url);

  private:
    std::pair<std::vector<std::string>, std::vector<std::string>> handler_segmonts(const std::string &path) const;

    std::vector<std::string> segmonts_list(const std::string &path) const;
    SegmontAtC segmont_from_url(std::string::const_iterator i,
                                std::string::const_iterator e) const;
    SegmontAtC segmont_static(std::string::const_iterator i,
                              std::string::const_iterator e) const;
    SegmontAtC segmont_variable(std::string::const_iterator i,
                                std::string::const_iterator e) const;
  };

}

#endif /* SRC_RESTROUTEHANDLER_H_ */