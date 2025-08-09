#ifndef SRC_APP_RESPONSE_H_
#define SRC_APP_RESPONSE_H_

#include <boost/beast/version.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>

namespace http = boost::beast::http;
namespace beast = boost::beast;

namespace Rest
{

  namespace Response
  {
    // Add this in your Session class or as a utility function
    template <typename Response>
    void add_cors_headers(Response &res)
    {
      res.set(boost::beast::http::field::access_control_allow_origin, "*"); // Or your allowed origin
      res.set(boost::beast::http::field::access_control_allow_methods, "GET, POST, PUT, DELETE, OPTIONS");
      res.set(boost::beast::http::field::access_control_allow_headers, "Content-Type, Authorization");
      res.set(boost::beast::http::field::access_control_allow_credentials, "true");
    }

    // success request response
    auto const success_request = [](const http::request<http::string_body> &req, beast::string_view body)
    {
      auto cookie_text = std::getenv("SET_COOKIE");

      http::response<http::string_body> res{http::status::ok, req.version()};
      res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
      res.set(http::field::content_type, "application/json");
      //res.set(http::field::set_cookie, std::string(cookie_text));
      add_cors_headers(res);
      res.keep_alive(req.keep_alive());
      res.body() = std::string(body);
      res.prepare_payload();
      return res;
    };

    // bad request response
    auto const bad_request = [](const http::request<http::string_body> &req, beast::string_view why)
    {
      http::response<http::string_body> res{http::status::bad_request, req.version()};
      res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
      res.set(http::field::content_type, "application/json");
      add_cors_headers(res);
      res.keep_alive(req.keep_alive());
      res.body() = "{\"error\": \"" + std::string(why) + "\"}";
      res.prepare_payload();
      return res;
    };

    // // server error response
    auto const server_error = [](const http::request<http::string_body> &req, beast::string_view what)
    {
      http::response<http::string_body> res{http::status::internal_server_error, req.version()};
      res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
      res.set(http::field::content_type, "application/json");
      add_cors_headers(res);
      res.keep_alive(req.keep_alive());
      res.body() = std::string(what);
      res.body() = "{\"error\": \"" + std::string(what) + "\"}";
      res.prepare_payload();
      return res;
    };

    // not found response
    auto const not_found = [](const http::request<http::string_body> &req, beast::string_view target)
    {
      http::response<http::string_body> res{http::status::not_found, req.version()};
      res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
      res.set(http::field::content_type, "application/json");
      add_cors_headers(res);
      res.keep_alive(req.keep_alive());
      res.body() = "{\"error\": \"The resource '" + std::string(target) + "' was not found.\"}";
      res.prepare_payload();
      return res;
    };
  }
}

#endif // SRC_APP_RESPONSE_H_