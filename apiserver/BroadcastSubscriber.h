#ifndef REST_BROADCAST_SUBSCRIBER_H_
#define REST_BROADCAST_SUBSCRIBER_H_

#include "../redisSubscribe/Subscribe.h"

namespace Rest
{
  class BroadcastSubscriber : public RedisSubscribe::Awakener
  {

  public:
    BroadcastSubscriber() : RedisSubscribe::Awakener() {};

    // This function will broadcast the messages to the main thread.
    // It will print the messages to the standard output.
    // This function is called by the receiver when it receives a message from Redis.
    // The base class will print the messages.
    // It is able to be overridden in a derived class if you want to handle the messages differently.
    // If you want to handle the messages differently, you can override this function in a derived class.
    // Plus then it may not be required to use the Awakener class wait_broadcast method to
    // synchronize another thread with the broadcast messages.
    virtual void broadcast_messages(const std::list<std::string> &broadcast_messages)
    {
      D(std::cout << "BroadcastSubscriber::broadcast_messages\n";)
      {
        if (broadcast_messages.empty())
          return; // If there are no messages, do not update.
        // The base class will print the messages.
        for (const auto &msg : broadcast_messages)
        {
          D(std::cout << "BroadcastSubscriber::broadcast_messages " <<  msg << std::endl;)
          if (broadcastCallback_)
            broadcastCallback_(msg);
        }
        std::cout << std::endl;
        D(std::cout << "******************************************************#\n\n";)
      }
      // io_utility::log_file("signal.log", {"NMTokens AwakenerWaitable broadcast_messages: ", m_node.ip, " - ", m_node.name, "\n"});
    };

    virtual void stop()
    {
      std::cout << "BroadcastSubscriber::stop\n";
    }

    virtual void on_subscribe()
    {
      std::cout << "BroadcastSubscriber::on_subscribe\n";
      // io_utility::log_file("signal.log", {"NMTokens AwakenerWaitable on_subscribe: ", m_node.ip, " - ", m_node.name, "\n"});
    }

    void setBroadcastCallback(std::function<void(const std::string &)> cb)
    {
      broadcastCallback_ = std::move(cb);
    }

  private:
    std::function<void(const std::string &)> broadcastCallback_;
  };
}
#endif // REST_BROADCAST_SUBSCRIBER_H_