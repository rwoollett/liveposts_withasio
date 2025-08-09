//
// Created by rodney on 26/07/2020.
//

#include "WSClientManager.h"
#include "ErrorHandler.h"
#include "Websocket.h"
// #include "cstokenmodel/model.h"
#include <iostream>
#include <string>

namespace Rest
{
  WSClientManager::WSClientManager() : m_ws_clients{}, m_class_lock(),
                                       m_cond_not_empty(), m_cond_not_full(), m_client_count_{0},
                                       m_size(0), m_stop(false),
                                       m_redisSubscribe{}, m_broadcastSubscribed{} //
  {
    m_redisSubscribe.main_redis(m_broadcastSubscribed);
    m_broadcastSubscribed.setBroadcastCallback(
        [this](const std::string &msg)
        { this->BroadcastToAll(msg); });

    m_message_dispatcher = {
        {"clientCS_Connected", [this](std::shared_ptr<websocket_session> session, json payload)
         { onClientConnect(session, payload); }},
        {"clientCS_Disconnected", [this](std::shared_ptr<websocket_session> session, json payload)
         { onClientDisconnect(session, payload); }},
        {"csToken_request", [this](std::shared_ptr<websocket_session> session, json payload)
         { onTokenRequest(session, payload); }},
        {"csToken_acquire", [this](std::shared_ptr<websocket_session> session, json payload)
         { onTokenAcquire(session, payload); }},
        {"default", [this](std::shared_ptr<websocket_session>, json)
         { onUnknown(); }},
    };
  };

  WSClientManager::~WSClientManager()
  {
    D(std::cerr << "WSClientManager destroyed\n";)
  };

  void WSClientManager::RegisterUser(const std::string &user, std::shared_ptr<websocket_session> &&client)
  {
    {
      std::unique_lock<std::mutex> cl(m_class_lock);
      m_cond_not_full.wait(cl, [this]
                           { return m_size < (MAX) || m_stop; });

      auto it = m_ws_clients.find(user);
      if (it != m_ws_clients.end() && it->second)
      {
        it->second->close();
        std::cerr << "user found ws in map and overwritten new ws client! - keep size as same value." << std::endl;
      }
      else
      {
        m_size += 1;
      }
      m_ws_clients[user] = std::move(client);
    }
    m_cond_not_empty.notify_all();

    // - implement webscoket close and use then newer one:-
    // std::lock_guard<std::mutex> lock(m_class_lock);
    // auto it = m_ws_clients.find(user);
    // if (it != m_ws_clients.end() && it->second) {
    //     it->second->close(); // Implement a close() method in websocket_session
    // }
    // m_ws_clients[user] = std::move(client);
    // ... update m_size, etc.
  }

  std::shared_ptr<websocket_session> WSClientManager::GetSession(const std::string &user)
  {
    std::lock_guard<std::mutex> lock(m_class_lock);
    auto it = m_ws_clients.find(user);
    if (it != m_ws_clients.end())
      return it->second;
    return nullptr;
  }

  void WSClientManager::UnregisterUser(const std::string &user)
  {
    D(std::cout << "Start UnregisterUser " << user << " size: " << m_size << std::endl;)
    {
      std::unique_lock<std::mutex> cl(m_class_lock);
      {
        m_cond_not_empty.wait(cl, [this]
                              { return m_size > 0 || m_stop; });
      }
      D(std::cout << "UnregisterUser: size " << m_size << std::endl;)
      if (m_size > 0)
      {

        auto it = m_ws_clients.find(user);
        if (it != m_ws_clients.end())
        {
          D(std::cout << "UnregisterUser number ref for smartptr: " << it->second.use_count() << std::endl;)
          m_ws_clients.erase(it);
          m_size -= 1;
          D(std::cout << "UnregisterUser erased web socket for " << user << std::endl;)
        }
      }
    }
    D(std::cout << "Finish UnregisterUser " << user << " size: " << m_size << std::endl;)
    m_cond_not_full.notify_all();
  }

  void WSClientManager::BroadcastToAll(const std::string &message)
  {
    std::lock_guard<std::mutex> lock(m_class_lock);
    for (auto &[user, session] : m_ws_clients)
    {
      std::cout << "WSClientManager::BroadcastToAll, sessid: " << user << " (ref session:" << session << ")" << std::endl;
      if (session)
      {
        try
        {
          json event = json::parse(message);
          std::string subject = event.value("subject", "default");
          json payload = event.value("payload", json::object());

          // if (m_message_dispatcher.contains(subject))
          //{
          //  m_message_dispatcher[subject](session, payload);
          std::cout << "WSClientManager::BroadcastToAll " << std::endl
                    << "-- call async_send  thread id: " << std::this_thread::get_id() << ", sessid: " << user << std::endl
                    << "   message: " << event.dump() << std::endl
                    << "   size:    " << event.dump().size() << std::endl
                    << std::endl;

          session->async_send(event.dump()); // async_send should be implemented in websocket_session
          //}
          // else
          //{
          //  m_message_dispatcher["default"](session, payload);
          // }
        }
        catch (const std::exception &e)
        {
          std::cerr << "###### " << std::endl
                    << "Parse/dispatch error: " << e.what() << std::endl
                    << "######" << std::endl;
        }
      }
    }
  }

  void WSClientManager::stop()
  {
    {
      std::lock_guard<std::mutex> lock(m_class_lock);
      m_stop = true;
    }
    m_cond_not_empty.notify_all();
    m_cond_not_full.notify_all();
  }

  void WSClientManager::onClientConnect(std::shared_ptr<websocket_session>, const json &payload)
  {
  }

  void WSClientManager::onClientDisconnect(std::shared_ptr<websocket_session>, const json &payload)
  {
  }

  void WSClientManager::onTokenRequest(std::shared_ptr<websocket_session>, const json &payload)
  {
  }

  void WSClientManager::onTokenAcquire(std::shared_ptr<websocket_session>, const json &payload)
  {
  }

  void WSClientManager::onUnknown()
  {
    std::cerr << "Unknown subject received" << std::endl;
  }

}