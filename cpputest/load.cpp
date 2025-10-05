#include <nlohmann/json.hpp>
#include "restclient-cpp/restclient.h"
#include "restclient-cpp/connection.h"
#include "livepostsmodel/model.h"
#include "livepostsmodel/timestamp.h"
#include <random>
#include <thread>
#include <iostream>
#include <sstream>
#include "load.h"

using json = nlohmann::json;

constexpr const char *SERVER_URL = "http://localhost:3011";
auto names = std::vector<std::string>{"Gary", "Barry", "Carl", "John", "Brian", "David", "Boll", "Carry", "Sue", "Delia"};
std::default_random_engine generator;
std::uniform_int_distribution<int> distribution(0, 9);

std::vector<LivePostsModel::Post> liveposts_creator(int startID, int amt)
{
  std::vector<LivePostsModel::Post> liveposts;

  // Fake user ids
  auto itemran = std::bind(distribution, generator);

  for (int i = startID; i < (startID + amt); i++)
  {
    LivePostsModel::Post post;
    post.userId = 1;
    post.userName = "Temp User at Hello co nz";
    post.content = "Their bird was, in this moment, a silly bear. They were lost without the amicable kiwi that composed their fox. A peach is an amicable crocodile. A tidy fox without sharks is truly a scorpion of willing cats. Shouting with happiness, a currant is a wise currant!";
    post.title = "Some tough spiders are thought of simply as figs";
    liveposts.push_back(post);
  }
  return liveposts;
};

int main(int, char **)
{
  RestClient::init();
  RestClient::Connection conn1(SERVER_URL);
  RestClient::Connection conn2(SERVER_URL);
  RestClient::Connection conn3(SERVER_URL);
  bool doPrint{true};
  bool doPrintHeader{false};
  bool doCreatePost = false;

  while (true)
  {
    auto liveposts_list = liveposts_creator(6000, 100);

    // Create 20 liveposts and retrieve game id in each
    for (int i = 50; i < 70; i++)
    {
      json post = liveposts_list[i];
      std::cout << post.dump() << std::endl;
      RestClient::Response rx;
      rx = conn1.put("/api/v1/liveposts/posts", post.dump());
      std::string errs{};
      PrintLog(std::string("put (\"/api/v1/liveposts/posts\")") + " -- ", doPrint, doPrintHeader, rx);
      json root{};
      try
      {
        root = json::parse(rx.body);
        std::cout << root["createPost"].dump() << std::endl;
        LivePostsModel::Post new_post = root["createPost"];
        liveposts_list[i].id = new_post.id;
      }
      catch (json::exception &e)
      {
        errs += e.what();
        std::cerr << errs << "\n";
      }
    } //

    try
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      std::cerr << "Next loop\n";

      auto liveposts_list_while = &liveposts_list;

      // // Create threads with each a put to SERVER_URL
      // std::thread t1([&conn1, liveposts_list_while, doPrint, doPrintHeader]()
      //                {
      //                  for (int i = 50; i < 70; i++)
      //                  {
      //                    json post = liveposts_list_while->at(i);
      //                    LivePostsModel::PostStart start_post;
      //                    start_post.postId = post["id"];
      //                    json start_post_json = start_post;
      //                    RestClient::Response rx = conn1.put("/api/v1/post/start", start_post_json.dump());
      //                    std::string errs{};
      //                    json root{};
      //                    PrintLog("put (\"/api/v1/post/start\") --  ", doPrint, doPrintHeader, rx);
      //                    try
      //                    {
      //                      root = json::parse(rx.body);
      //                    }
      //                    catch (json::exception &e)
      //                    {
      //                      errs += e.what();
      //                      std::cerr << errs << "\n";
      //                    }
      //                  } //
      //                });

      // std::thread t2([&conn2, liveposts_list_while, doPrint, doPrintHeader]()
      //                {
      //                  std::this_thread::sleep_for(std::chrono::milliseconds(500));
      //                  for (int i = 50; i < 70; i++)
      //                  {
      //                    LivePostsModel::Post post = liveposts_list_while->at(i);
      //                    TTTModel::PlayerMove move;
      //                    move.postId = post.id;
      //                    move.player = 1;
      //                    move.moveCell = -1; // AI starts
      //                    move.isOpponentStart = true;
      //                    json move_json = move;
      //                    RestClient::Response rx = conn2.put("/api/v1/post/move", move_json.dump());
      //                    std::string errs{};
      //                    PrintLog("put (\"/api/v1/post/move\") -- ", doPrint, doPrintHeader, rx);
      //                    json root{};
      //                    try
      //                    {
      //                      root = json::parse(rx.body);
      //                    }
      //                    catch (json::exception &e)
      //                    {
      //                      errs += e.what();
      //                      std::cerr << errs << "\n";
      //                    }
      //                  }
      //                  //
      //                });

      // t1.join();
      // t2.join();


      std::this_thread::sleep_for(std::chrono::milliseconds(3500));
      for (int i = 50; i < 70; i++)
      {
        json post = liveposts_list_while->at(i);
        RestClient::Response rx = conn3.del(std::string("/api/v1/liveposts/post/complete/" + std::string(post["id"])));

        std::string errs{};
        PrintLog("del(\"" + std::string("/api/v1/liveposts/post/complete/" + std::string(post["id"])) + "\") -- ", doPrint, doPrintHeader, rx);
        json root{};
        try
        {
          root = json::parse(rx.body);
        }
        catch (json::exception &e)
        {
          errs += e.what();
          std::cerr << errs << "\n";
        }
      }
    }
    catch (std::exception &e)
    {
      std::cerr << e.what() << "\n";
      break;
    }
    catch (...)
    {
      std::cerr << "excepted\n";
    }
  }
  std::cerr << "out\n";

  return 0;
}

void PrintLog(const std::string &header, bool doPrint, bool doPrintHeader, RestClient::Response &rx)
{
  if (doPrint)
  {
    if (doPrintHeader)
    {
      std::stringstream heads;
      for (auto h : rx.headers)
      {
        heads << " [" << h.first << "] " << h.second << "\n";
      }
      std::cerr << header << heads.str() << "\n";
    }
    std::cerr << header << " [" << rx.body << "]\n";
  }
}
