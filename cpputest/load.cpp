#include <nlohmann/json.hpp>
#include "restclient-cpp/restclient.h"
#include "restclient-cpp/connection.h"
#include "livepostsmodel/model.h"
#include "timestamp/timestamp.h"
#include "ApiResponse.h"
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
using range_pair = std::pair<int, int>;
using range_list = std::vector<range_pair>;

std::vector<LivePostsModel::Post> liveposts_creator(int amt)
{
  std::vector<LivePostsModel::Post> liveposts;

  // Fake user ids
  auto itemran = std::bind(distribution, generator);

  for (int i = 0; i < amt; i++)
  {
    LivePostsModel::Post post;
    post.id = -1;
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
  bool doPrint{false};
  bool doPrintHeader{false};
  std::string wsId = "web_client_1";

  if (!std::getenv("TEST_COOKIE"))
  {
    std::cerr << "Environment variables TEST_COOKIE not set." << std::endl;
    exit(1);
  }

  RestClient::HeaderFields headers;
  headers["Cookie"] = std::string(std::getenv("TEST_COOKIE"));

  auto findRanges = [](int amt, int set_size, range_list &list) -> void
  {
    // assert((amt % set_size) == 0 && "amt must be divisible by set_size");
    //  throw std::out_of_range("Use an amount and set size that fits without extras. ie modulus is not zero.");
    if (set_size == 0)
      throw std::invalid_argument("set_size must not be zero");

    if ((amt % set_size) != 0)
      throw std::logic_error("amt must be divisible by set_size");

    list.clear();

    int size = amt / set_size;
    int z = amt;
    while (amt > 0)
    {
      list.push_back({z - amt, z - ((amt - set_size) + 1)});
      amt = amt - set_size;
    }
  };

  while (true)
  {
    int total_items = 60; 
    int size_set = 20;
    try
    {
      range_list ranges;
      auto livepost_list = liveposts_creator(total_items);
      findRanges(total_items, size_set, ranges);
      // if (ranges.size() < 3)
      // {
      //   throw std::logic_error("ranges size must be greater than 3.");
      // }

      for (std::pair<int, int> range : ranges)
      {
        std::cout << "x=" << range.first << ", y=" << range.second << std::endl;
      }
      std::cout << "ranges size: " << ranges.size() << std::endl;

      std::vector<std::unique_ptr<RestClient::Connection>> conn_list;
      conn_list.reserve(ranges.size());

      for (int i = 0; i < ranges.size(); i++)
        conn_list.emplace_back(std::make_unique<RestClient::Connection>(SERVER_URL));

      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      auto create_post = [wsId, &headers, &livepost_list, doPrint, doPrintHeader](RestClient::Connection &conn, int x, int y)
      {
        try
        {

          for (int i = x; i <= y; i++)
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            ApiResponse<std::optional<LivePostsModel::Post>> v;
            conn.SetHeaders(headers);

            LivePostsModel::PostCreate p;
            p.wsId = wsId;
            p.title = livepost_list[i].title;
            p.content = livepost_list[i].content;
            RestClient::Response rx;
            json j = p;
            rx = conn.put("/api/v1/liveposts/posts", j.dump());
            std::string errs{};
            PrintLog(std::string(std::to_string(i) + " - put (\"/api/v1/liveposts/posts\")"), doPrint, doPrintHeader, rx);
            json root{};
            try
            {
              root = json::parse(rx.body);
              v = response<std::optional<LivePostsModel::Post>>(root, "createPost");
              if (std::holds_alternative<ApiError>(v))
              {
                auto error = std::get<ApiError>(v);
                std::cerr << "[error " << i << " " << error.message << "]\n";
              }
              else
              {
                auto response = std::get<std::optional<LivePostsModel::Post>>(v).value();
                livepost_list[i].id = response.id;
                std::cerr << "[post=" << response.id << " i=" << i << "] ";
              }
            }
            catch (json::exception &e)
            {
              errs += e.what();
              errs += rx.body;
              std::cerr << "**************" << errs << "\n";
              exit(1);
            }
          } //
        }
        catch (const std::exception &e)
        {
          std::cerr << "create_game thread exception: " << e.what() << "\n";
          exit(1);
        }
        catch (...)
        {
          std::cerr << "create_game thread exception: unknown\n";
          exit(1);
        }
      };

      // auto start_game = [wsId, &livepost_list, doPrint, doPrintHeader](RestClient::Connection &conn, int x, int y)
      // {
      //   for (int i = x; i < y; i++)
      //   {
      //     ApiResponse<std::optional<TTTModel::Game>> v;

      //     json game = livepost_list.at(i);
      //     TTTModel::GameStart start_game;
      //     start_game.gameId = game["id"];
      //     start_game.userId = wsId;
      //     json start_game_json = start_game;
      //     RestClient::Response rx = conn.put("/api/v1/ttt/game/start", start_game_json.dump());
      //     std::string errs{};
      //     json root{};
      //     PrintLog("put (\"/api/v1/ttt/game/start\") --  ", doPrint, doPrintHeader, rx);
      //     try
      //     {
      //       root = json::parse(rx.body);
      //       v = response<std::optional<TTTModel::Game>>(root, "startGame");
      //       if (std::holds_alternative<ApiError>(v))
      //       {
      //         auto error = std::get<ApiError>(v);
      //         std::cerr << "[error " << i << " " << error.message << "]\n";
      //       }
      //       else
      //       {
      //         auto response = std::get<std::optional<TTTModel::Game>>(v).value();
      //         std::cerr << "[game=" << response.id << " i=" << i << "] ";
      //       }
      //     }
      //     catch (json::exception &e)
      //     {
      //       errs += e.what();
      //       errs += rx.body;
      //       std::cerr << "**************" << errs << "\n";
      //       exit(1);
      //     }
      //   } //
      // };

      // auto player_move = [wsId, &livepost_list, doPrint, doPrintHeader](RestClient::Connection &conn, int x, int y)
      // {
      //   for (int i = x; i < y; i++)
      //   {
      //     ApiResponse<std::optional<TTTModel::PlayerMove>> v;

      //     TTTModel::Game game = game_list.at(i);
      //     TTTModel::PlayerMove move;
      //     move.gameId = game.id;
      //     move.userId = wsId;
      //     move.player = 1;
      //     move.moveCell = -1; // AI starts
      //     move.isOpponentStart = true;
      //     json move_json = move;
      //     RestClient::Response rx = conn.put("/api/v1/ttt/game/move", move_json.dump());
      //     std::string errs{};
      //     PrintLog("put (\"/api/v1/ttt/game/move\") -- ", doPrint, doPrintHeader, rx);
      //     json root{};
      //     try
      //     {
      //       root = json::parse(rx.body);
      //       v = response<std::optional<TTTModel::PlayerMove>>(root, "boardMove");
      //       if (std::holds_alternative<ApiError>(v))
      //       {
      //         auto error = std::get<ApiError>(v);
      //         std::cerr << "[error " << i << " " << error.message << "]\n";
      //       }
      //       else
      //       {
      //         auto response = std::get<std::optional<TTTModel::PlayerMove>>(v).value();
      //         std::cerr << "[move=" << response.id << " i=" << i << "] ";
      //       }
      //     }
      //     catch (json::exception &e)
      //     {
      //       errs += e.what();
      //       errs += rx.body;
      //       std::cerr << "**************" << errs << "\n";
      //       exit(1);
      //     }
      //   }
      //   //
      // };

      auto health_check = [doPrint, doPrintHeader](RestClient::Connection &conn, int x, int y)
      {
        try
        {

          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          for (int i = x; i <= y; i++)
          {
            RestClient::Response rx;
            rx = conn.get("/health");
            std::string errs{};
            PrintLog(std::string(std::to_string(i) + " - get (\"/health\")"), doPrint, doPrintHeader, rx);
            json root{};
            try
            {
              root = json::parse(rx.body);
            }
            catch (json::exception &e)
            {
              errs += e.what();
              errs += rx.body;
              std::cerr << "**************" << errs << "\n";
              exit(1);
            }
            std::cerr << "[done " << i << " " << root << "] ";
          } //
        }
        catch (const std::exception &e)
        {
          std::cerr << "health_check thread exception: " << e.what() << "\n";
          exit(1);
        }
        catch (...)
        {
          std::cerr << "health_check thread exception: unknown\n";
          exit(1);
        }
      };

      std::vector<std::thread> create_threads;
      create_threads.reserve(ranges.size());
      for (size_t i = 0; i < ranges.size(); ++i)
      {
        auto range = ranges[i]; // copy
        create_threads.emplace_back(
            [create_post, conn = conn_list[i].get(), i, range]()
            {
              create_post(*conn, range.first, range.second);
            });
        std::cerr << "[" << range.first << "," << range.second << "] ";
      }
      std::cerr << std::endl;
      for (auto &t : create_threads)
      {
        t.join();
        // std::cerr << ".";
      }
      std::cerr << "\nDone Post Create threads" << std::endl;

      // std::vector<std::thread> start_threads;
      // start_threads.reserve(ranges.size());
      // for (size_t i = 0; i < ranges.size(); ++i)
      // {
      //   auto range = ranges[i]; // copy
      //   start_threads.emplace_back(
      //       [start_game, conn = conn_list[i].get(), i, range]()
      //       {
      //         start_game(*conn, range.first, range.second);
      //       });
      //   std::cerr << "[" << range.first << "," << range.second << "] ";
      // }
      // for (auto &t : start_threads)
      // {
      //   t.join();
      // }
      // std::cerr << "\nDone Start threads" << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // std::vector<std::thread> play_threads;
      // play_threads.reserve(ranges.size());
      // for (size_t i = 0; i < ranges.size(); ++i)
      // {
      //   auto range = ranges[i]; // copy
      //   play_threads.emplace_back(
      //       [player_move, conn = conn_list[i].get(), i, range]()
      //       {
      //         player_move(*conn, range.first, range.second);
      //       });
      // }
      // for (auto &t : play_threads)
      // {
      //   t.join();
      // }
      // std::cerr << "\nDone Player move threads" << std::endl;
      // std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // auto conn = conn_list.at(0).get();
      // for (int i = 0; i < total_items; i++)
      // {
      //   json game = livepost_list.at(i);
      //   TTTModel::Game game_delete = game;
      //   if (!(game_delete.id.empty()))
      //   {
      //     RestClient::Response rx = conn->del(std::string("/api/v1/ttt/game/complete/" + std::string(game_delete.id)));
      //     std::string errs{};
      //     PrintLog(std::to_string(i) +
      //                  " - del(\"" + std::string("/api/v1/ttt/game/complete/" + std::string(game_delete.id)) + "\")",
      //              doPrint, doPrintHeader, rx);
      //     json root{};
      //     try
      //     {
      //       root = json::parse(rx.body);
      //     }
      //     catch (json::exception &e)
      //     {
      //       errs += e.what();
      //       std::cerr << errs << "\n";
      //     }
      //   }
      // }

      for (auto &conn : conn_list)
      {
        conn->Terminate();
        conn.reset();
      }

      //
      for (std::pair<int, int> range : ranges)
      {
        std::cout << "x=" << range.first << ", y=" << range.second << std::endl;
      }
      std::cout << "ranges size: " << ranges.size() << std::endl;

      break;
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
    std::cerr << header << "\n";
  }
}
