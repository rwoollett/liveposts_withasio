#include "TTTRoutes.h"

#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include "apiserver/RouteHandler.h"
#include "redisPublish/Publish.h"
#include <nlohmann/json.hpp>
#include "tttmodel/model.h"
#include "tttmodel/pq.h"
#include <memory>
#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>

using json = nlohmann::json;
using TTTModel::parseDate;
using Rest::RouteHandler;

namespace Routes
{

  namespace TicTacToe
  {

    const char *BOARD_INIT = "0,0,0,0,0,0,0,0,0";
    // /**=============================================================== */
    // /** CSToken hosts known                                            */
    // /**=============================================================== */
    // void cstokenHosts(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    // {
    //   auto query = "select host, count(host) from \"Client\" group by host;";
    //   auto queryCallback = [sess, req, send](PGresult *res)
    //   {
    //     if (!res)
    //     {
    //       D(std::cerr << "Query has been shown as finished when null res returned." << std::endl;)
    //       return;
    //     }

    //     json root;
    //     root["hosts"] = json::array({});

    //     ExecStatusType resStatus = PQresultStatus(res);
    //     if (resStatus != PGRES_TUPLES_OK)
    //     {
    //       std::cerr << "Query execution failed " + std::string(PQresultErrorMessage(res));
    //       return send(std::move(Rest::Response::server_error(req, "Query execution failed")));
    //     }

    //     int rows = PQntuples(res);
    //     for (int i = 0; i < rows; i++)
    //     {
    //       json host;
    //       host["name"] = std::string(PQgetvalue(res, i, 0));
    //       host["amount"] = std::atoi(PQgetvalue(res, i, 1));
    //       root["hosts"].push_back(host);
    //     }
    //     return send(std::move(Rest::Response::success_request(req, root.dump())));
    //   };

    //   dbclient->asyncQuery(query, queryCallback);
    // };

    /**=============================================================== */
    /** Create game                                                    */
    /**=============================================================== */
    void createGame(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      // Validation check on put req body and bad request is made if not valid
      TTTModel::Game game;
      try
      {
        // clientCS = TTTModel::Json::ClientCS::fromJson(req.body());
        game = json::parse(req.body());
      }
      catch (json::exception &e)
      {
        json err = e.what();
        auto msg = err.dump();
        return send(std::move(Rest::Response::bad_request(req, msg.substr(1, msg.size() - 2))));
      }
      catch (const std::string &e)
      {
        return send(std::move(Rest::Response::bad_request(req, e)));
      }

      if (!TTTModel::Validate::Game(game))
      {
        return send(std::move(Rest::Response::bad_request(req, "Game is not having valid data.")));
      }

      auto sql =
          "INSERT INTO \"Game\" "
          "(\"userId\", \"board\", \"createdAt\") VALUES ($1, $2, NOW()) "
          "RETURNING id, \"userId\", board, \"createdAt\""
          ";";

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      paramStrings->push_back(game.userId);
      paramStrings->push_back(std::string(BOARD_INIT));

      auto paramLengths = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramLengths->push_back(s.size());

      auto paramFormats = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramFormats->push_back(0);

      auto apiResult = std::make_shared<std::string>();
      // Place holder for the event
      auto newGame = std::make_shared<TTTModel::Game>();

      auto endTransaction = [redisPublish, req, apiResult, send, newGame](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Create Game has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Create Game execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        // Publish GameCreateEvent subject.
        // If not validate newgame means this request update is unsuccessful due to no row found
        try
        {
          if (TTTModel::Validate::Game(*newGame))
          {
            TTTEvents::GameCreateEvent event;
            event.gameId = newGame->id;
            event.board = newGame->board;
            event.tpCreatedAt = newGame->tpCreatedAt;
            json jsonEvent = event;

            /////
            csTTTMessageCount++;
            std::cout << "  Sending to publish: Subject (" << TTTEvents::SubjectNames.at(event.subject) << ")"
                      << " " << csTTTMessageCount << " TTT messages made. "
                      << std::endl;
            ///
            redisPublish->Send(
                std::string(TTTEvents::SubjectNames.at(event.subject)),
                jsonEvent.dump());
          }

          return send(std::move(Rest::Response::success_request(req, apiResult->c_str())));
        }
        catch (json::exception &e)
        {
          json err = e.what();
          auto msg = err.dump();
          return send(std::move(Rest::Response::bad_request(req, msg.substr(1, msg.size() - 2))));
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
      };

      auto paramQueryCreateGame = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Create Game has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["createGame"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query createGame execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Create Game Query has been shown as finished when null res returned." << std::endl;)
                                   return;
                                 }
                                 return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
                                 //
                               });

          return;
        }
        int rows = PQntuples(res);
        int cols = PQnfields(res);
        try
        {
          // Only one row is returned
          *newGame = TTTModel::PG::Game::fromPGRes(res, cols, 0);
          json jsonGame = *newGame;
          jsonGame["id"] = PQgetvalue(res, 0, 0); // Game id
          root["createGame"] = jsonGame;
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
        apiResult->assign(root.dump());
        dbclient->asyncQuery("COMMIT", endTransaction);
      };

      auto beginTransaction = [=](PGresult *beginRes)
      {
        if (!beginRes)
        {
          D(std::cerr << "beginTransaction Create Game Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Create Game Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncParamQuery(sql, *paramStrings, *paramLengths, *paramFormats, paramQueryCreateGame);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };

    /**=============================================================== */
    /** Start game                                                     */
    /**=============================================================== */
    void startGame(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      // Validation check on put req body and bad request is made if not valid
      TTTModel::GameStart gameStart;
      try
      {
        gameStart = json::parse(req.body());
      }
      catch (json::exception &e)
      {
        json err = e.what();
        auto msg = err.dump();
        return send(std::move(Rest::Response::bad_request(req, msg.substr(1, msg.size() - 2))));
      }
      catch (const std::string &e)
      {
        return send(std::move(Rest::Response::bad_request(req, e)));
      }

      if (!TTTModel::Validate::GameStart(gameStart))
      {
        return send(std::move(Rest::Response::bad_request(req, "Game start is not having valid data.")));
      }

      auto sql =
          "UPDATE \"Game\" "
          "SET \"board\"=$1 WHERE \"id\"=$2 "
          "RETURNING id, \"userId\", board, \"createdAt\""
          ";";

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      paramStrings->push_back(std::string(BOARD_INIT));
      paramStrings->push_back(gameStart.gameId);

      auto paramLengths = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramLengths->push_back(s.size());

      auto paramFormats = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramFormats->push_back(0);

      auto apiResult = std::make_shared<std::string>();
      // Place holder for the event
      auto updatedGame = std::make_shared<TTTModel::Game>();

      auto endTransaction = [redisPublish, updatedGame, req, apiResult, send](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Start Game has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Start Game execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        // Publish GameCreateEvent subject.
        // If not validate updatedGame means this request update is unsuccessful due to no row found
        try
        {
          if (TTTModel::Validate::Game(*updatedGame))
          {
            TTTEvents::GameCreateEvent event;
            event.gameId = updatedGame->id;
            event.board = updatedGame->board;
            event.tpCreatedAt = updatedGame->tpCreatedAt;
            json jsonEvent = event;

            /////
            csTTTMessageCount++;
            std::cout << "  Sending to publish: Subject (" << TTTEvents::SubjectNames.at(event.subject) << ")"
                      << " " << csTTTMessageCount << " TTT messages made. "
                      << std::endl;
            ///
            redisPublish->Send(
                std::string(TTTEvents::SubjectNames.at(event.subject)),
                jsonEvent.dump());
          }

          return send(std::move(Rest::Response::success_request(req, apiResult->c_str())));
        }
        catch (json::exception &e)
        {
          json err = e.what();
          auto msg = err.dump();
          return send(std::move(Rest::Response::bad_request(req, msg.substr(1, msg.size() - 2))));
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
      };

      auto paramQueryStartGame = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Start Game has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["startGame"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Start Game execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Start Game Query has been shown as finished when null res returned." << std::endl;)
                                   return;
                                 }
                                 return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
                                 //
                               });

          return;
        }
        int rows = PQntuples(res);
        int cols = PQnfields(res);
        if (rows == 0)
        {
          root["startGame"] = "No rows returned from query";
          apiResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        try
        {
          // Only one row is returned
          *updatedGame = TTTModel::PG::Game::fromPGRes(res, cols, 0);
          json jsonGame = *updatedGame;
          jsonGame["id"] = PQgetvalue(res, 0, 0); // Game id
          root["startGame"] = jsonGame;
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
        apiResult->assign(root.dump());
        dbclient->asyncQuery("COMMIT", endTransaction);
      };

      auto beginTransaction = [=](PGresult *beginRes)
      {
        if (!beginRes)
        {
          D(std::cerr << "beginTransaction Start Game Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Start Game Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncParamQuery(sql, *paramStrings, *paramLengths, *paramFormats, paramQueryStartGame);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };

    /**=============================================================== */
    /** Player move                                                    */
    /**=============================================================== */
    void boardMove(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      // Validation check on put req body and bad request is made if not valid
      TTTModel::PlayerMove playerMove;
      try
      {
        playerMove = json::parse(req.body());
      }
      catch (json::exception &e)
      {
        json err = e.what();
        auto msg = err.dump();
        return send(std::move(Rest::Response::bad_request(req, msg.substr(1, msg.size() - 2))));
      }
      catch (const std::string &e)
      {
        return send(std::move(Rest::Response::bad_request(req, e)));
      }

      if (!TTTModel::Validate::PlayerMove(playerMove))
      {
        return send(std::move(Rest::Response::bad_request(req, "Board move is not having valid data.")));
      }

      auto sql =
          "INSERT INTO \"PlayerMove\" "
          "(\"gameId\", \"player\", \"moveCell\", \"isOpponentStart\") VALUES ($1, $2, $3, $4) "
          "RETURNING id, \"gameId\", \"player\", \"moveCell\", \"isOpponentStart\", \"allocated\", "
          "(SELECT \"board\" FROM \"Game\" WHERE \"Game\".\"id\" = \"PlayerMove\".\"gameId\") AS \"board\""
          ";";

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      paramStrings->push_back(playerMove.gameId);
      paramStrings->push_back(std::to_string(playerMove.player));
      paramStrings->push_back(std::to_string(playerMove.moveCell));
      paramStrings->push_back(std::to_string(playerMove.isOpponentStart));

      auto paramLengths = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramLengths->push_back(s.size());

      auto paramFormats = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramFormats->push_back(0);

      auto apiResult = std::make_shared<std::string>();
      // Place holder for the event
      auto newMove = std::make_shared<TTTModel::PlayerMove>();

      auto endTransaction = [redisPublish, newMove, req, apiResult, send](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Board move has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Board move execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        // Publish GameCreateEvent subject.
        // If not validate updatedGame means this request update is unsuccessful due to no row found
        try
        {
          if (TTTModel::Validate::PlayerMove(*newMove))
          {
            TTTEvents::PlayerMoveEvent event;
            event.id = newMove->id;
            event.gameId = newMove->gameId;
            event.player = newMove->player;
            event.moveCell = newMove->moveCell;
            event.board = newMove->board;
            event.isOpponentStart = newMove->isOpponentStart;
            json jsonEvent = event;

            /////
            csTTTMessageCount++;
            std::cout << "  Sending to publish: Subject (" << TTTEvents::SubjectNames.at(event.subject) << ")"
                      << " " << csTTTMessageCount << " TTT messages made. "
                      << std::endl;
            ///
            redisPublish->Send(
                std::string(TTTEvents::SubjectNames.at(event.subject)),
                jsonEvent.dump());
          }

          return send(std::move(Rest::Response::success_request(req, apiResult->c_str())));
        }
        catch (json::exception &e)
        {
          json err = e.what();
          auto msg = err.dump();
          return send(std::move(Rest::Response::bad_request(req, msg.substr(1, msg.size() - 2))));
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
      };

      auto paramQueryBoardMove = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Board move has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["boardMove"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Board move execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Board move Query has been shown as finished when null res returned." << std::endl;)
                                   return;
                                 }
                                 return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
                                 //
                               });

          return;
        }
        int rows = PQntuples(res);
        int cols = PQnfields(res);
        if (rows == 0)
        {
          root["boardMove"] = "No rows returned from query";
          apiResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        try
        {
          // Only one row is returned
          *newMove = TTTModel::PG::PlayerMove::fromPGRes(res, cols, 0);
          json jsonMove = *newMove;
          jsonMove["id"] = PQgetvalue(res, 0, 0); // PlayerMove id
          root["boardMove"] = jsonMove;
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
        apiResult->assign(root.dump());
        dbclient->asyncQuery("COMMIT", endTransaction);
      };

      auto beginTransaction = [=](PGresult *beginRes)
      {
        if (!beginRes)
        {
          D(std::cerr << "beginTransaction Board move Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Board move Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncParamQuery(sql, *paramStrings, *paramLengths, *paramFormats, paramQueryBoardMove);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };

    /**=============================================================== */
    /** Delete Game completed, after a time period.
     ** Also remove PlayerMoves for game
    /**=============================================================== */
    void deleteGame(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      auto params = sess->getReqUrlParameters(); // The Game Id request url
      auto [route_url, query_params] = RouteHandler::parse_query_params(std::string(req.target()));
      D(for (const auto &[key, value] : query_params) { std::cout << key << ": " << value << std::endl; })
      D(for (auto param : params) { std::cout << param.first << " " << param.second << std::endl; });

      auto deleteGameSql =
          "DELETE FROM \"Game\" "
          "WHERE id = $1 "
          "RETURNING id, \"userId\", board, \"createdAt\""
          ";";

      auto deletePlayerMoveSql =
          "DELETE FROM \"PlayerMove\" "
          "WHERE \"gameId\" = $1 "
          "RETURNING id, \"gameId\", \"player\", \"moveCell\", \"isOpponentStart\", \"allocated\", "
          "(SELECT \"board\" FROM \"Game\" WHERE \"Game\".\"id\" = \"PlayerMove\".\"gameId\") AS \"board\""
          ";";

      auto paramGameDeleteStrings = std::make_shared<std::vector<std::string>>();
      paramGameDeleteStrings->push_back(params["gameId"]);

      auto paramGameDeleteLength = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramGameDeleteStrings)
        paramGameDeleteLength->push_back(s.size());

      auto paramGameDeleteFormats = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramGameDeleteStrings)
        paramGameDeleteFormats->push_back(0);

      auto paramDeletePlayerMoveStrings = std::make_shared<std::vector<std::string>>();
      paramDeletePlayerMoveStrings->push_back(params["gameId"]);

      auto paramDeletePlayerMoveLengths = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramDeletePlayerMoveStrings)
        paramDeletePlayerMoveLengths->push_back(s.size());

      auto paramDeletePlayerMoveFormats = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramDeletePlayerMoveStrings)
        paramDeletePlayerMoveFormats->push_back(0);

      auto deletePlayerMoveResult = std::make_shared<std::string>();
      auto deleteGameResult = std::make_shared<std::string>();

      auto endTransaction = [=](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Query execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }
        // Merge into json deleteGameResult and deletePlayerMoveResult
        json result;
        result["game"] = json::parse(*deleteGameResult);
        result["playerMove"] = json::parse(*deletePlayerMoveResult);

        return send(std::move(Rest::Response::success_request(req, result.dump().c_str())));
      };

      auto paramQueryDeleteGame = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Delete Game has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["deleteGame"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Delete Game execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Query Delete Game has been shown as finished when null res returned." << std::endl;)
                                   return;
                                 }
                                 return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
                                 //
                               });

          return;
        }
        int rows = PQntuples(res);
        if (rows == 0)
        {
          root["deleteGame"] = "No rows returned from query";
          deleteGameResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        int cols = PQnfields(res);
        try
        {
          // Only one row is returned
          root["deleteGame"] = TTTModel::PG::Game::fromPGRes(res, cols, 0);
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }

        deleteGameResult->assign(root.dump());
        dbclient->asyncQuery("COMMIT", endTransaction);
      };

      auto paramQueryDeletePlayerMove = [=](PGresult *moveRes)
      {
        if (!moveRes)
        {
          D(std::cerr << "Query Delete Player Move has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["deletePlayerMove"] = {};

        ExecStatusType resStatus = PQresultStatus(moveRes);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Delete Player Move execution failed: " + std::string(PQresultErrorMessage(moveRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Query Delete Player Move has been shown as finished when null res returned." << std::endl;)
                                   return;
                                 }
                                 return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
                                 //
                               });
          return;
        }
        int rows = PQntuples(moveRes);
        int cols = PQnfields(moveRes);

        if (rows == 0)
        {
          root["deletePlayerMove"] = "No rows returned from query";
          deletePlayerMoveResult->assign(root.dump());
          dbclient->asyncParamQuery(deleteGameSql, *paramGameDeleteStrings, *paramGameDeleteLength, *paramGameDeleteFormats, paramQueryDeleteGame);
          return;
        }

        try
        {
          root["deletePlayerMove"] = json::array();
          for (int row = 0; row < rows; row++)
          {
            root["deletePlayerMove"].push_back(TTTModel::PG::PlayerMove::fromPGRes(moveRes, cols, row));
            // parent["id"] = std::atoi(PQgetvalue(moveRes, 0, 0));
            // parent["gameId"] = PQgetvalue(moveRes, 0, 1);
          }
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }

        deletePlayerMoveResult->assign(root.dump());
        dbclient->asyncParamQuery(deleteGameSql, *paramGameDeleteStrings, *paramGameDeleteLength, *paramGameDeleteFormats, paramQueryDeleteGame);
      };

      auto beginTransaction = [=](PGresult *beginRes)
      {
        if (!beginRes)
        {
          D(std::cerr << "beginTransaction Query Delete Game has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Query Delete Game execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncParamQuery(deletePlayerMoveSql, *paramDeletePlayerMoveStrings, *paramDeletePlayerMoveLengths, *paramDeletePlayerMoveFormats, paramQueryDeletePlayerMove);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };

    /**================================================================== */
    /** Get the next Player Move unallocated (retrieve only one or none)  */
    /**================================================================== */
    void playerMove(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {

      auto query = "SELECT "
                   "\"PlayerMove\".\"id\", \"allocated\", \"gameId\", \"player\", \"moveCell\", \"isOpponentStart\", "
                   "\"Game\".\"userId\" AS \"userId\", "
                   "\"Game\".\"board\" AS \"board\" "
                   "FROM \"PlayerMove\" LEFT JOIN \"Game\" ON "
                   "\"PlayerMove\".\"gameId\"=\"Game\".\"id\" "
                   "WHERE \"PlayerMove\".\"allocated\"='f' "
                   ";";

      auto updateSql =
          "UPDATE \"PlayerMove\" "
          "SET \"allocated\"=$1 "
          "WHERE \"id\"=$2 "
          "RETURNING id, \"gameId\", \"player\", \"moveCell\", \"isOpponentStart\", \"allocated\", "
          "(SELECT \"board\" FROM \"Game\" WHERE \"Game\".\"id\" = \"PlayerMove\".\"gameId\") AS \"board\""
          ";";

      D(std::cout << "PlayerMove query\n"
                  << query << std::endl
                  << "PlayerMove query\n"
                  << updateSql << std::endl;)

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      auto paramLengths = std::make_shared<std::vector<int>>();
      auto paramFormats = std::make_shared<std::vector<int>>();
      auto apiResult = std::make_shared<std::string>();
      auto foundMove = std::make_shared<TTTModel::PlayerMove>();

      auto endTransaction = [req, apiResult, send](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Player Move has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Board move execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        return send(std::move(Rest::Response::success_request(req, apiResult->c_str())));
      };

      auto paramQueryAllocatedPlayerMove = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Allocated Player Move has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["playerMove"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Allocated Player Move execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Allocated Player Move has been shown as finished when null res returned." << std::endl;)
                                   return;
                                 }
                                 return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
                                 //
                               });

          return;
        }
        int rows = PQntuples(res);
        int cols = PQnfields(res);
        if (rows == 0)
        {
          root["playerMove"] = json::array();
          apiResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        try
        {
          // Only one row is returned
          root["playerMove"] = json::array();
          auto allocatedMove = TTTModel::PG::PlayerMove::fromPGRes(res, cols, 0);
          json jsonMove = allocatedMove;
          jsonMove["id"] = PQgetvalue(res, 0, 0); // Player Move id
          root["playerMove"].push_back(jsonMove);
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
        apiResult->assign(root.dump());
        dbclient->asyncQuery("COMMIT", endTransaction);
      };

      auto queryPlayerMove = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Player Move has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["playerMove"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Player Move execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Player Move Query has been shown as finished when null res returned." << std::endl;)
                                   return;
                                 }
                                 return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
                                 //
                               });

          return;
        }
        int rows = PQntuples(res);
        int cols = PQnfields(res);
        if (rows == 0)
        {
          root["playerMove"] = json::array();
          apiResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        try
        {
          // Only one row is returned
          root["playerMove"] = json::array();
          *foundMove = TTTModel::PG::PlayerMove::fromPGRes(res, cols, 0);
          json jsonMove = *foundMove;
          jsonMove["id"] = PQgetvalue(res, 0, 0); // PlayerMove id
          root["playerMove"].push_back(jsonMove);
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
        apiResult->assign(root.dump());
        std::cout << "=====> Player Move query found: " << root.dump() << std::endl;

        // Now before end transaction update allocated = true for foundMove in DB
        paramStrings->push_back(std::to_string(true));
        paramStrings->push_back(foundMove->id);
        for (const auto &s : *paramStrings)
          paramLengths->push_back(s.size());
        for (const auto &s : *paramStrings)
          paramFormats->push_back(0);

        dbclient->asyncParamQuery(updateSql, *paramStrings, *paramLengths, *paramFormats, paramQueryAllocatedPlayerMove);
      };

      auto beginTransaction = [=](PGresult *beginRes)
      {
        if (!beginRes)
        {
          D(std::cerr << "beginTransaction Player Move Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Board move Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncQuery(query, queryPlayerMove);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };

    /**================================================================== */
    /** Update board for game Id                                          */
    /**================================================================== */
    void boardUpdate(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      // Validation check on put req body and bad request is made if not valid
      TTTModel::GameUpdate gameUpdateInput;
      try
      {
        gameUpdateInput = json::parse(req.body());
      }
      catch (json::exception &e)
      {
        json err = e.what();
        auto msg = err.dump();
        return send(std::move(Rest::Response::bad_request(req, msg.substr(1, msg.size() - 2))));
      }
      catch (const std::string &e)
      {
        return send(std::move(Rest::Response::bad_request(req, e)));
      }

      if (!TTTModel::Validate::GameUpdate(gameUpdateInput))
      {
        return send(std::move(Rest::Response::bad_request(req, "Board Update is not having valid data.")));
      }

      auto sql =
          "UPDATE \"Game\" "
          "SET \"board\"=$1 WHERE \"id\"=$2 "
          "RETURNING id, \"userId\", board, \"createdAt\""
          ";";

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      paramStrings->push_back(gameUpdateInput.board);
      paramStrings->push_back(gameUpdateInput.gameId);

      auto paramLengths = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramLengths->push_back(s.size());

      auto paramFormats = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramFormats->push_back(0);

      auto apiResult = std::make_shared<std::string>();
      // Place holder for the event
      auto updatedGame = std::make_shared<TTTModel::Game>();

      auto endTransaction = [redisPublish, updatedGame, gameUpdateInput, req, apiResult, send](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Board Update has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Board Update execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        // Publish GameCreateEvent subject.
        // If not validate updatedGame means this request update is unsuccessful due to no row found
        try
        {
          if (TTTModel::Validate::Game(*updatedGame))
          {
            TTTEvents::GameUpdateByIdEvent event;
            event.gameId = updatedGame->id;
            event.board = updatedGame->board;
            event.result = gameUpdateInput.result;
            json jsonEvent = event;

            /////
            csTTTMessageCount++;
            std::cout << "  Sending to publish: Subject (" << TTTEvents::SubjectNames.at(event.subject) << ")"
                      << " " << csTTTMessageCount << " TTT messages made. "
                      << std::endl;
            ///
            redisPublish->Send(
                std::string(TTTEvents::SubjectNames.at(event.subject)),
                jsonEvent.dump());
          }

          return send(std::move(Rest::Response::success_request(req, apiResult->c_str())));
        }
        catch (json::exception &e)
        {
          json err = e.what();
          auto msg = err.dump();
          return send(std::move(Rest::Response::bad_request(req, msg.substr(1, msg.size() - 2))));
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
      };

      auto paramQueryUpdateGame = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Board Update has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["updateGame"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Board Update execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Board Update Query has been shown as finished when null res returned." << std::endl;)
                                   return;
                                 }
                                 return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
                                 //
                               });

          return;
        }
        int rows = PQntuples(res);
        int cols = PQnfields(res);
        if (rows == 0)
        {
          root["updateGame"] = "No rows returned from query";
          apiResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        try
        {
          // Only one row is returned
          *updatedGame = TTTModel::PG::Game::fromPGRes(res, cols, 0);
          json jsonGame = *updatedGame;
          jsonGame["id"] = PQgetvalue(res, 0, 0); // Game id
          jsonGame["result"] = gameUpdateInput.result;
          root["updateGame"] = jsonGame;
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
        apiResult->assign(root.dump());
        dbclient->asyncQuery("COMMIT", endTransaction);
      };

      auto beginTransaction = [=](PGresult *beginRes)
      {
        if (!beginRes)
        {
          D(std::cerr << "beginTransaction Board Update Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Board Update Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncParamQuery(sql, *paramStrings, *paramLengths, *paramFormats, paramQueryUpdateGame);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };

    /**=============================================================== */
    /** HealthCheck                                                    */
    /**=============================================================== */
    void healthCheck(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      json root;
      root["healthCheck"] = {};
      std::string result;
      try
      {
        root = "OK";
        result.assign(root.dump());
        return send(std::move(Rest::Response::success_request(req, result)));
      }
      catch (const std::string &e)
      {
        return send(std::move(Rest::Response::server_error(req, e)));
      }
    };

    /**=============================================================== */
    /** Home Page                                                      */
    /**=============================================================== */
    void homePage(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      json root;
      root["title"] = "TTT Service";
      root["description"] = "The TTT service is a user game for  a UI interaction "
                            "to test the stack of NetProc.";
      root["navCards"] = json::array();
      root["popularCards"] = json::array();

      json card;
      card["title"] = "Laboratory Collection";
      card["catchPhrase"] = "Laboratory Collection";

      root["navCards"].push_back(card);
      root["popularCards"].push_back(card);

      std::string result;
      try
      {
        result.assign(root.dump());
        return send(std::move(Rest::Response::success_request(req, result)));
      }
      catch (const std::string &e)
      {
        return send(std::move(Rest::Response::server_error(req, e)));
      }
    };

    /**=============================================================== */
    /** User list                                                      */
    /**=============================================================== */
    void userList(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      json root = json::array();

      json user;
      user["id"] = "400";
      user["name"] = "Mrs. Ryan Adams";
      root.push_back(user);
      user["id"] = "300";
      user["name"] = "Natalie Emard";
      root.push_back(user);

      std::string result;
      try
      {
        result.assign(root.dump());
        return send(std::move(Rest::Response::success_request(req, result)));
      }
      catch (const std::string &e)
      {
        return send(std::move(Rest::Response::server_error(req, e)));
      }
    };

    /**=============================================================== */
    /** Posts list                                                      */
    /**=============================================================== */
    void posts(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      json root = json::array();

      json post;
      post["id"] = "UX9NrG-3OUJPiSbvJUeOf";
      post["date"] = "2021-06-12T04:30:12.000Z";
      post["title"] = "Some tough spiders are thought of simply as figs";
      post["content"] = "Their bird was, in this moment, a silly bear. They were lost without the amicable kiwi that composed their fox. A peach is an amicable crocodile. A tidy fox without sharks is truly a scorpion of willing cats. Shouting with happiness, a currant is a wise currant!";
      post["user"] = "400";
      post["reactions"]["thumbsUp"] = 0;
      post["reactions"]["hooray"] = 0;
      post["reactions"]["heart"] = 0;
      post["reactions"]["rocket"] = 0;
      post["reactions"]["eyes"] = 0;
      root.push_back(post);

      std::string result;
      try
      {
        result.assign(root.dump());
        return send(std::move(Rest::Response::success_request(req, result)));
      }
      catch (const std::string &e)
      {
        return send(std::move(Rest::Response::server_error(req, e)));
      }
    };

  }
}