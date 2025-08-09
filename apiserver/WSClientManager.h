
#ifndef SRC_APP_REST_WSCLIENT_MANAGER_H_
#define SRC_APP_REST_WSCLIENT_MANAGER_H_

#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <thread>
#include "BroadcastSubscriber.h"
#include "../redisSubscribe/Subscribe.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using RedisSubscribe::Subscribe;
using Rest::BroadcastSubscriber;

namespace Rest
{
  class websocket_session;

  class WSClientManager // : public std::enable_shared_from_this<WSClientManager>
  {

  private:
    using MessageHandler = std::function<void(std::shared_ptr<websocket_session>, const nlohmann::json &)>;
    const int MAX = 10000; // Linit to 10000 wsclients
    std::unordered_map<std::string, std::shared_ptr<websocket_session>> m_ws_clients;
    std::unordered_map<std::string, MessageHandler> m_message_dispatcher;
    std::vector<std::shared_ptr<websocket_session>> m_dedup;
    std::mutex m_class_lock;
    std::condition_variable m_cond_not_empty;
    std::condition_variable m_cond_not_full;
    std::atomic<uint64_t> m_client_count_{0};
    int m_timeOut;
    int m_size;
    bool m_stop;
    Subscribe m_redisSubscribe;
    BroadcastSubscriber m_broadcastSubscribed;

  public:
    WSClientManager();
    ~WSClientManager();

    int IncrementID()
    {
      return ++m_client_count_;
    };

    int Size()
    {
      std::lock_guard<std::mutex> cl(m_class_lock);
      return m_size;
    };
    void RegisterUser(const std::string &user, std::shared_ptr<websocket_session> &&client);
    std::shared_ptr<websocket_session> GetSession(const std::string &user);
    void UnregisterUser(const std::string &user);

    void BroadcastToAll(const std::string &message);

    void stop();

  private:
   // void registerHandler(const std::string &event, MessageHandler handler);
   // void dispatch(const std::string &message);
    // Event handlers for subscribed redis subjects to websocket
   void onClientConnect(std::shared_ptr<websocket_session>, const json& payload);
   void onClientDisconnect(std::shared_ptr<websocket_session>, const json& payload);
   void onTokenRequest(std::shared_ptr<websocket_session>, const json& payload);
   void onTokenAcquire(std::shared_ptr<websocket_session>, const json& payload);

   void onUnknown();

  };
}
#endif // SRC_APP_REST_WSCLIENT_MANAGER_H_
