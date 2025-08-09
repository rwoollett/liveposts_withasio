#include "RouteHandler.h"
#include "ErrorHandler.h"
#include <boost/url/decode_view.hpp>
#include <sstream>
#include <iostream>
#include <parallel/algorithm>
using std::begin;
using std::end;
using std::find_if;

namespace
{
  HttpRoute *CreateHttpRoute()
  {

    return new HttpRoute();
  }

}

std::pair<std::unique_ptr<HttpRoute>, Parameters> Rest::RouteHandler::match(const RoutePath &route_path) const
{
  // Try to find full path name with m_handlers find
  // If found then this is the handler to use for HttpRoute - go and create it from factory
  //
  // If not next iter uses: truncate down one segmont and keep removed as the variable
  //
  // Continue to truncate url and keep the next removed as the variable, the varialbe list will grow larger
  // Finish try to match when url segmont is empty and the list of variable not match
  // If a match is made at any stage with the segmonts static, and the list of varialve segmont, then
  //  a handler is found and can be created.
  // Create the handler and populate the vaiables found int the match.
  // The url can have query parameters. ie. the part of url after the ? and key value pairs. This is removed
  // from the url when finding route paths registered in the routes factory.
  // Remove the query paramters from url. Only the segmonts in the path are used, along with
  // dynamic segmonts such as {var} which is used for parameters key value pairs.
  auto [route_url, query_params] = RouteHandler::parse_query_params(route_path.path);
  auto url = route_url;

  auto segmonts = segmonts_list(url);
  std::string route_name{};
  auto url_seq_cnt = segmonts.size();
  auto index = url_seq_cnt; // found index is set after find_if.

  // Go over the handlers in the order declared in program
  auto iter = __gnu_parallel::find_if(begin(m_handlers), end(m_handlers), [&](const RouteHandle &handle)
                                      {
    bool is_match{false};
    index = url_seq_cnt;
    auto url_find = url; //.substr(1);
    //std::cerr << "RouteHandler init " << url_find << " " << handle.m_static_name << " i = " << index << "\n";
    while (index > 0)
    {
      // try full url with first in handlers routes
      if (handle.m_static_name == url_find)
      {
        D(std::cerr << "handler static match " << handle.m_static_name << " = " << url_find << "\n";)
        // Check if truncated url by (url_seq_cnt - i) has params to fill
        // If not all params can be filled then not matched
        size_t k = 0;
        size_t j = index;
        for (auto p : handle.m_param_names)
        {
          //std::cerr << "each param match " << p << " = " << segmonts[j] << "\n";
          j++;
          k++;
        }
        //std::cerr << "i = " << index << " k = " << k << " Does k equal vars count: k=" << k << " varcnt=" << (url_seq_cnt - index) << " is " << (k == (url_seq_cnt - index)) << "\n";
        if (k == (url_seq_cnt - index))
        {
          return true;
        }
        return false;
      }

      //std::cerr << "after try " << url_find << "\n";
      auto last_pos = url_find.find_last_of('/');
      url_find = url_find.substr(0, last_pos);
      //std::cerr << "last_pos " << last_pos << " " << url_find << "\n";
      index--;
    }

    return is_match; });

  if (iter != cend(m_handlers))
  {
    // Fill the params from the url as match is true with the param defined for route.
    RouteHandle found_route = *iter;
    SegmontList p_found = found_route.m_param_names;
    Parameters params;
    size_t j = index; // found index in find_if

    std::string method_name{};
    MethodNameFromEnum(route_path, method_name);

    std::stringstream ssParams;
    for (auto p : p_found)
    {
      params[p] = segmonts[j];
      ssParams << "{" << p << "}=" << segmonts[j] << " ";
      j++;
    }
    std::cout << " " << method_name << "=> " << found_route.m_static_name << " " << ssParams.str() << std::endl;

    return {std::unique_ptr<HttpRoute>(m_route_factory.CreateObject(
                {route_path.method, found_route.m_static_name})),
            params};
  }
  else
  {
    throw std::runtime_error("Route not matched");
  }
};

void Rest::RouteHandler::declare_route_handler(
    const RoutePath &route_path,
    HttpRoute::RouteCall callback)
{
  auto result = handler_segmonts(route_path.path);
  std::vector<std::string> static_segmont = result.first;
  std::vector<std::string> variable_segmont = result.second;

  std::string method_name{};
  MethodNameFromEnum(route_path, method_name);

  std::string route_name{};
  for (auto s : static_segmont)
  {
    route_name += '/' + s;
  }

  std::string variables{};
  for (auto s : variable_segmont)
  {
    variables += '{' + s + "} ";
  }

  std::cout << "Declared route: " << method_name << "=> " << route_name << " " << variables << "\n";
  m_handlers.push_back({route_name, variable_segmont});
  m_route_factory.Register({route_path.method, route_name}, CreateHttpRoute, callback);
}

void Rest::RouteHandler::MethodNameFromEnum(const Rest::RoutePath &route_path, std::string &method_name) const
{
  switch (route_path.method)
  {
  case 1:
  {
    method_name = "Del    ";
    break;
  }
  case 2:
  {
    method_name = "Get    ";
    break;
  }
  case 5:
  {
    method_name = "Put    ";
    break;
  }
  default:
    method_name = "";
  };
}

std::pair<std::vector<std::string>, std::vector<std::string>> Rest::RouteHandler::handler_segmonts(const std::string &path) const
{
  std::string::const_iterator i = path.cbegin();
  std::vector<std::string> static_segmont{};
  std::vector<std::string> variable_segmont{};
  std::stringstream ss{};
  int var_count = 0;
  try
  {
    while (i != path.cend())
    { // level root
      if (*i == '/')
      {
        i++;
        // level in a /
        if (*i == '{')
        {
          auto res = segmont_variable(i, path.cend());
          variable_segmont.push_back(res.second);
          i = res.first;
          var_count++;
        }
        else if (i == path.cend())
          break;
        else
        {
          if (var_count)
          {
            ss << "not allowed variables before static segmonts";
            throw std::runtime_error(ss.str());
          }
          auto res = segmont_static(i, path.cend());
          static_segmont.push_back(res.second);
          i = res.first;
        }
      }
      else if (*i == '{')
      {
        ss << "not allowed multiple variables inside / separators";
        throw std::runtime_error(ss.str());
      }
      else
        i++;
    }
  }
  catch (std::exception &e)
  {
    throw;
  }
  return {static_segmont, variable_segmont};
}

std::vector<std::string> Rest::RouteHandler::segmonts_list(
    const std::string &path) const
{
  std::string::const_iterator i = path.cbegin();
  std::vector<std::string> static_segmont{};
  std::stringstream ss{};
  try
  {
    while (i != path.cend())
    { // level root
      if (*i == '/')
      {
        i++;
        // level in a /
        if (i == path.cend())
          break;
        else
        {
          auto res = segmont_from_url(i, path.cend());
          static_segmont.push_back(res.second);
          i = res.first;
        }
      }
      else
        i++;
    }
  }
  catch (std::exception &e)
  {
    throw;
  }
  return static_segmont;
}

SegmontAtC Rest::RouteHandler::segmont_static(std::string::const_iterator i,
                                              std::string::const_iterator e) const
{
  std::string static_segmont{};
  std::stringstream ss{};
  if (!isalpha(*i))
  {
    ss << "not valid in segmont " << *i;
    throw std::runtime_error(ss.str());
  }
  while (!(i == e || *i == '/'))
  {
    if (!(isalnum(*i) || *i == '_'))
    {
      ss << "not valid in segmont " << *i;
      throw std::runtime_error(ss.str());
    }
    static_segmont.push_back(*i);
    i++;
  }
  return {i, static_segmont};
}

SegmontAtC Rest::RouteHandler::segmont_from_url(std::string::const_iterator i,
                                                std::string::const_iterator e) const
{
  std::string static_segmont{};
  while (!(i == e || *i == '/'))
  {
    static_segmont.push_back(*i);
    i++;
  }
  return {i, static_segmont};
}

SegmontAtC Rest::RouteHandler::segmont_variable(std::string::const_iterator i,
                                                std::string::const_iterator e) const
{
  std::string variable{};
  std::stringstream ss{};

  if (*i != '{')
  {
    ss << "not valid in variable segmont " << *i;
    throw std::runtime_error(ss.str());
  }
  i++;
  if (!isalpha(*i))
  {
    ss << "not valid in variable segmont " << *i;
    throw std::runtime_error(ss.str());
  }
  while (!(i == e || *i == '}'))
  {
    if (!(isalnum(*i) || *i == '_'))
    {
      ss << "not valid in variable segmont " << *i;
      throw std::runtime_error(ss.str());
    }
    variable.push_back(*i);
    i++;
  }
  if (i == e)
  {
    ss << "not valid with missing } in variable segmont";
    throw std::runtime_error(ss.str());
  }
  // std::cerr << *i;
  i++;
  return {i, variable};
}

std::tuple<std::string, std::map<std::string, std::string>> Rest::RouteHandler::parse_query_params(const std::string &url)
{

  std::map<std::string, std::string> params;
  std::string target = std::string(url);
  std::string route_url = url;
  size_t query_start = target.find("?");
  if (query_start != std::string::npos)
  {
    route_url = target.substr(0, query_start);
    std::string query_string = target.substr(query_start + 1);
    boost::urls::decode_view dv(query_string);
    std::cout << "url decoded = " << dv << std::endl;
    std::stringstream ss(query_string);
    std::string param;
    while (std::getline(ss, param, '&'))
    {
      size_t eq_pos = param.find("=");
      if (eq_pos != std::string::npos)
      {
        std::string key = param.substr(0, eq_pos);
        std::string value = param.substr(eq_pos + 1);
        params[key] = value;
      }
    }
  }
  return {route_url, params};
}
