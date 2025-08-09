#include "Websocket.h"
#include "WSClientManager.h"

void Rest::websocket_session::on_accept(beast::error_code ec)
{
  if (ec)
    return fail(ec, "accept");

  D(std::cout << "No. of registered users " << wsclient_manager_.Size() << std::endl;)

  do_read();
}

void Rest::websocket_session::do_read()
{
  // Read a message into our buffer
  ws_.async_read(
      buffer_,
      net::bind_executor(strand_,
                         beast::bind_front_handler(
                             &websocket_session::on_read,
                             shared_from_this())));
}

void Rest::websocket_session::on_read(
    beast::error_code ec,
    std::size_t bytes_transferred)
{
  boost::ignore_unused(bytes_transferred);

  std::cout << "on_read  thread id: " << std::this_thread::get_id() << ", on_read  websession id: " << user_ << std::endl;

  // This indicates that the websocket_session was closed
  if (ec == websocket::error::closed)
  {
    std::cout << "websocket closed unregistered " << isRegistered << "\n";
    if (isRegistered)
    {
      wsclient_manager_.UnregisterUser(user_);
      isRegistered = false;
    }
    return;
  }

  if (ec)
  {
    fail(ec, "read");
    std::cout << "read unregistered " << isRegistered << "\n";
    if (isRegistered)
    {
      wsclient_manager_.UnregisterUser(user_);
      isRegistered = false;
    }
    return;
  }

  // Echo the message
  ws_.text(ws_.got_text());
  ws_.async_write(
      buffer_.data(),
      net::bind_executor(strand_,
                         beast::bind_front_handler(
                             &websocket_session::on_write,
                             shared_from_this())));
}

void Rest::websocket_session::on_write(
    beast::error_code ec,
    std::size_t bytes_transferred)
{
  boost::ignore_unused(bytes_transferred);
  std::cout << "on_write  thread id: " << std::this_thread::get_id() << ", on_write  websession id: " << user_ << std::endl;

  if (ec)
    return fail(ec, "write");

  // Clear the buffer
  buffer_.consume(buffer_.size());

  // Do another read
  do_read();
}

void Rest::websocket_session::async_send(const std::string &msg)
{
  // Post the write operation through the strand
  boost::asio::post(strand_,
                    [self = shared_from_this(), msg]()
                    {
                      bool write_in_progress = !self->write_msgs_.empty();
                      self->write_msgs_.push_back(msg);
                      std::cout << "--1---> async_send queued thread id: " << std::this_thread::get_id() << ", sessid: " << self->user_ << std::endl
                                << "        msg:" << msg << std::endl
                                << "        size:" << msg.size()
                                << std::endl;
                      if (!write_in_progress)
                      {
                        self->do_async_write();
                      }
                    });
}

void Rest::websocket_session::do_async_write()
{
  std::cout << "--2---> do_async_write queued thread id: " << std::this_thread::get_id() << ", sessid: " << user_ << std::endl
            << "        msg:" << write_msgs_.front() << std::endl
            << std::endl;
  ws_.text(true);
  ws_.async_write(
      net::buffer(write_msgs_.front()),
      net::bind_executor(strand_,
                         beast::bind_front_handler(
                             &websocket_session::on_async_write,
                             shared_from_this())));
}

void Rest::websocket_session::on_async_write(
    beast::error_code ec,
    std::size_t bytes_transferred)
{
  if (ec)
    return fail(ec, "write");

  std::cout << "--3---> on_async_write thread id: " << std::this_thread::get_id() << ", sessid: " << user_ << std::endl
            << "        msg:" << write_msgs_.front() << std::endl
            << std::endl;
  write_msgs_.pop_front();
  if (!write_msgs_.empty())
  {
    do_async_write();
  }
}
