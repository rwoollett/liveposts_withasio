#ifndef SRC_APP_REST_WEBSOCKET_SESSION_H_
#define SRC_APP_REST_WEBSOCKET_SESSION_H_

#include <boost/beast/core/error.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/core/bind_handler.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <memory>
#include <string>
#include "ErrorHandler.h"
#include "RouteHandler.h"
#include <thread>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

namespace Rest
{

  class WSClientManager;

  // Echoes back all received WebSocket messages
  class websocket_session : public std::enable_shared_from_this<websocket_session>
  {
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::string user_;
    WSClientManager &wsclient_manager_;
    net::strand<net::io_context::executor_type> strand_; // Strand for thread safety
    bool isRegistered{true};
    // In websocket_session.h
    std::deque<std::string> write_msgs_;
    bool write_in_progress_;
    // boost::asio::steady_timer timer_;
    //  std::atomic<uint64_t> timer_generation_{0};
    //  std::atomic<uint64_t> timer_expired_{0};

  public:
    // Take ownership of the socket
    explicit websocket_session(
        tcp::socket &&socket,
        const std::string &user,
        WSClientManager &wsclient_manager,
        net::io_context &ioc)
        : ws_(std::move(socket)),
          user_(user),
          wsclient_manager_(wsclient_manager),
          strand_(net::make_strand(ioc)),
          isRegistered{true},
          write_msgs_{},
          write_in_progress_{false}


    // timer_{strand_, std::chrono::steady_clock::now() + std::chrono::seconds(60)}
    {
    }

    ~websocket_session()
    {
      std::cout << "desctr websocket session for " << user_ << "\n";
    }

    // Start the asynchronous accept operation
    template <class Body, class Allocator>
    void
    do_accept(http::request<Body, http::basic_fields<Allocator>> req)
    {
      std::cout << "do_accept  thread id: " << std::this_thread::get_id() << ", do_accept  websession id: " << user_ << std::endl;

      // Set suggested timeout settings for the websocket
      ws_.set_option(
          websocket::stream_base::timeout::suggested(
              beast::role_type::server));

      // Set a decorator to change the Server of the handshake
      ws_.set_option(websocket::stream_base::decorator(
          [](websocket::response_type &res)
          {
            res.set(http::field::server,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                        " tictactoe api ws server");
          }));

      // Accept the websocket handshake
      ws_.async_accept(
          req,
          net::bind_executor(strand_,
                             beast::bind_front_handler(
                                 &websocket_session::on_accept,
                                 shared_from_this())));
      // ws_.async_accept(
      //     req,
      //     beast::bind_front_handler(
      //         &websocket_session::on_accept,
      //         shared_from_this()));
    }

    void async_send(const std::string &msg);

    void close()
    {
      std::cout << "close - do close the websocket from server end." << std::endl;
      auto self = shared_from_this();
      net::post(
          strand_,
          [self = shared_from_this()]()
          {
            self->ws_.close(websocket::close_code::normal);
          });
    }

  private:
    void on_accept(beast::error_code ec);
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    void do_async_write();
    void on_async_write(beast::error_code ec, std::size_t bytes_transferred);
    // void reset_timer();
    // void on_timeout(beast::error_code ec);
    // void on_close(beast::error_code ec);
  };
}
#endif /* SRC_APP_REST_WEBSOCKET_SESSION_H_ */