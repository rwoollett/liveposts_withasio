#include <nlohmann/json.hpp>
#include "restclient-cpp/restclient.h"
#include "restclient-cpp/connection.h"
#include "tttmodel/model.h"
#include "tttmodel/timestamp.h"
#include <random>
#include <thread>
#include <iostream>
#include <sstream>
#include "load.h"

using json = nlohmann::json;

constexpr const char *SERVER_URL = "http://localhost:3009";
auto names = std::vector<std::string>{"Gary", "Barry", "Carl", "John", "Brian", "David", "Boll", "Carry", "Sue", "Delia"};
std::default_random_engine generator;
std::uniform_int_distribution<int> distribution(0, 9);

std::vector<TTTModel::Game> game_creater(int startID, int amt)
{
  std::vector<TTTModel::Game> games;

  // Fake user ids
  auto itemran = std::bind(distribution, generator);

  for (int i = startID; i < (startID + amt); i++)
  {
    auto r1 = itemran();
    auto r2 = itemran();
    auto name = names[r1] + " " + names[r2];
    TTTModel::Game game;
    // game.id = std::to_string(i);
    game.userId = std::to_string(i);
    game.board = "0,0,0,0,0,0,0,0,0";
    auto tpOptCA = TTTModel::parseDate("2025-06-01 12:30:00");
    if (tpOptCA)
      game.tpCreatedAt = *tpOptCA;

    games.push_back(game);
  }
  return games;
};

int main(int, char **)
{
  RestClient::init();
  RestClient::Connection conn1(SERVER_URL);
  RestClient::Connection conn2(SERVER_URL);
  RestClient::Connection conn3(SERVER_URL);
  bool doPrint{true};
  bool doPrintHeader{false};
  bool doCreateGame = false;

  while (true)
  {
    auto game_list = game_creater(6000, 100);

    // Create 20 games and retrieve game id in each
    for (int i = 50; i < 70; i++)
    {
      json game = game_list[i];
      std::cout << game.dump() << std::endl;
      RestClient::Response rx;
      rx = conn1.put("/api/v1/game/create", game.dump());
      std::string errs{};
      PrintLog(std::string("put (\"/api/v1/game/create\")") + " -- ", doPrint, doPrintHeader, rx);
      json root{};
      try
      {
        root = json::parse(rx.body);
        std::cout << root["createGame"].dump() << std::endl;
        TTTModel::Game new_game = root["createGame"];
        game_list[i].id = new_game.id;
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

      auto game_list_while = &game_list;

      // Create threads with each a put to SERVER_URL
      std::thread t1([&conn1, game_list_while, doPrint, doPrintHeader]()
                     {
                       for (int i = 50; i < 70; i++)
                       {
                         json game = game_list_while->at(i);
                         TTTModel::GameStart start_game;
                         start_game.gameId = game["id"];
                         json start_game_json = start_game;
                         RestClient::Response rx = conn1.put("/api/v1/game/start", start_game_json.dump());
                         std::string errs{};
                         json root{};
                         PrintLog("put (\"/api/v1/game/start\") --  ", doPrint, doPrintHeader, rx);
                         try
                         {
                           root = json::parse(rx.body);
                         }
                         catch (json::exception &e)
                         {
                           errs += e.what();
                           std::cerr << errs << "\n";
                         }
                       } //
                     });

      std::thread t2([&conn2, game_list_while, doPrint, doPrintHeader]()
                     {
                       std::this_thread::sleep_for(std::chrono::milliseconds(500));
                       for (int i = 50; i < 70; i++)
                       {
                         TTTModel::Game game = game_list_while->at(i);
                         TTTModel::PlayerMove move;
                         move.gameId = game.id;
                         move.player = 1;
                         move.moveCell = -1; // AI starts
                         move.isOpponentStart = true;
                         json move_json = move;
                         RestClient::Response rx = conn2.put("/api/v1/game/move", move_json.dump());
                         std::string errs{};
                         PrintLog("put (\"/api/v1/game/move\") -- ", doPrint, doPrintHeader, rx);
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
                       //
                     });

      t1.join();
      t2.join();
      std::this_thread::sleep_for(std::chrono::milliseconds(3500));
      for (int i = 50; i < 70; i++)
      {
        json game = game_list_while->at(i);
        RestClient::Response rx = conn3.del(std::string("/api/v1/game/complete/" + std::string(game["id"])));

        std::string errs{};
        PrintLog("del(\"" + std::string("/api/v1/game/complete/" + std::string(game["id"])) + "\") -- ", doPrint, doPrintHeader, rx);
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
