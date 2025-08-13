#include "Routes.h"

#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include "apiserver/RouteHandler.h"
#include "redisPublish/Publish.h"
#include <nlohmann/json.hpp>
#include "livepostsmodel/model.h"
#include "livepostsmodel/pq.h"
#include <memory>
#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>

using json = nlohmann::json;
using LivePostsModel::parseDate;
using Rest::RouteHandler;

namespace Routes
{

  namespace LivePosts
  {

    /**=============================================================== */
    /** Create post                                                    */
    /**=============================================================== */
    void createPost(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      // Validation check on put req body and bad request is made if not valid
      LivePostsModel::Post post;
      try
      {
        post = json::parse(req.body());
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

      if (!LivePostsModel::Validate::Posts(post))
      {
        return send(std::move(Rest::Response::bad_request(req, "Post is not having valid data.")));
      }

      auto sql =
          "INSERT INTO \"Posts\" "
          "(\"title\", \"content\", \"userId\", \"date\") VALUES ($1, $2, $3, NOW()) "
          "RETURNING id, \"title\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\""
          ";";

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      paramStrings->push_back(post.title);
      paramStrings->push_back(post.content);
      paramStrings->push_back(std::to_string(post.userId));

      auto paramLengths = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramLengths->push_back(s.size());

      auto paramFormats = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramFormats->push_back(0);

      auto apiResult = std::make_shared<std::string>();
      // Place holder for the event
      auto newPost = std::make_shared<LivePostsModel::Post>();

      auto endTransaction = [redisPublish, req, apiResult, send, newPost](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Create Post has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Create Post execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        // Publish GameCreateEvent subject.
        // If not validate newPost means this request update is unsuccessful due to no row found
        try
        {
          //  if (LivePostsModel::Validate::Post(*newPost))
          //  {
          // TTTEvents::GameCreateEvent event;
          // event.gameId = newPost->id;
          // event.board = newPost->board;
          // event.tpCreatedAt = newPost->tpCreatedAt;
          // json jsonEvent = event;

          // /////
          // cntLivePostMessage++;
          // std::cout << "  Sending to publish: Subject (" << TTTEvents::SubjectNames.at(event.subject) << ")"
          //           << " " << cntLivePostMessage << " TTT messages made. "
          //           << std::endl;
          // ///
          // redisPublish->Send(
          //     std::string(TTTEvents::SubjectNames.at(event.subject)),
          //     jsonEvent.dump());
          //}

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

      auto paramQueryCreatePost = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Create Post has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["createPost"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query createPost execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Create Post Query has been shown as finished when null res returned." << std::endl;)
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
          *newPost = LivePostsModel::PG::Posts::fromPGRes(res, cols, 0);
          json jsonGame = *newPost;
          jsonGame["id"] = PQgetvalue(res, 0, 0);
          root["createPost"] = jsonGame;
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
          D(std::cerr << "beginTransaction Create Post Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Create Post Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncParamQuery(sql, *paramStrings, *paramLengths, *paramFormats, paramQueryCreatePost);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };

    /**================================================================== */
    /** Get Posts                                                         */
    /**================================================================== */
    void fetchPosts(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {

      auto query = "SELECT "
                   "id, \"title\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\" "
                   "FROM \"Posts\" "
                   ";";

      D(std::cout << "FetchPost query\n"
                  << query << std::endl;)

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      auto paramLengths = std::make_shared<std::vector<int>>();
      auto paramFormats = std::make_shared<std::vector<int>>();
      auto apiResult = std::make_shared<std::string>();
      //      auto foundMove = std::make_shared<TTTModel::PlayerMove>();

      auto endTransaction = [req, apiResult, send](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Fetch Posts has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Fetch Posts execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        return send(std::move(Rest::Response::success_request(req, apiResult->c_str())));
      };

      auto queryFetchPosts = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Fetch Posts has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["fetchPosts"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Fetch Posts execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Fetch Posts Query has been shown as finished when null res returned." << std::endl;)
                                   return;
                                 }
                                 return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
                                 //
                               });

          return;
        }
        int rows = PQntuples(res);
        int cols = PQnfields(res);
        // for (int i = 0; i < cols; i++)
        // {
        //   Oid typeOid = PQftype(res, i); // Get the data type OID of the column
        //   printf("Column %d has data type OID: %u\n", i, typeOid);
        //   const char *field_name = PQfname(res, i);
        //   std::cout << "Column " << i << " name: " << field_name << std::endl;
        // }

        if (rows == 0)
        {
          root["fetchPosts"] = json::array();
          apiResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        try
        {
          root["fetchPosts"] = json::array();
          for (int row = 0; row < rows; row++)
          {
            LivePostsModel::Post post = LivePostsModel::PG::Posts::fromPGRes(res, cols, row);
            root["fetchPosts"].push_back(post);
          }
        }
        catch (const std::exception &e)
        {
          return send(std::move(Rest::Response::server_error(req, e.what())));
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
        apiResult->assign(root.dump());
        // std::cout << "=====> Fetch Posts query found: " << root.dump() << std::endl;

        dbclient->asyncQuery("COMMIT", endTransaction);
      };

      auto beginTransaction = [=](PGresult *beginRes)
      {
        if (!beginRes)
        {
          D(std::cerr << "beginTransaction Fetch Posts Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Fetch Posts Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncQuery(query, queryFetchPosts);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };

    /**=============================================================== */
    /** Create user                                                    */
    /**=============================================================== */
    void createUser(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      // Validation check on put req body and bad request is made if not valid
      LivePostsModel::User user;
      try
      {
        user = json::parse(req.body());
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

      if (!LivePostsModel::Validate::Users(user))
      {
        return send(std::move(Rest::Response::bad_request(req, "User is not having valid data.")));
      }

      auto sql =
          "INSERT INTO \"Users\" "
          "(\"authId\", \"name\") VALUES ($1, $2) "
          "RETURNING id, \"authId\", \"name\""
          ";";

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      paramStrings->push_back(user.authId);
      paramStrings->push_back(user.name);

      auto paramLengths = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramLengths->push_back(s.size());

      auto paramFormats = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramFormats->push_back(0);

      auto apiResult = std::make_shared<std::string>();
      // Place holder for the event
      auto newUser = std::make_shared<LivePostsModel::User>();

      auto endTransaction = [redisPublish, req, apiResult, send, newUser](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Create User has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Create User execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        return send(std::move(Rest::Response::success_request(req, apiResult->c_str())));
      };

      auto paramQueryCreatePost = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Create User has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["createUser"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query createUser execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Create User Query has been shown as finished when null res returned." << std::endl;)
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
          *newUser = LivePostsModel::PG::Users::fromPGRes(res, cols, 0);
          json jsonGame = *newUser;
          jsonGame["id"] = PQgetvalue(res, 0, 0);
          root["createUser"] = jsonGame;
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
          D(std::cerr << "beginTransaction Create User Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Create User Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncParamQuery(sql, *paramStrings, *paramLengths, *paramFormats, paramQueryCreatePost);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };

    /**================================================================== */
    /** Get User By AuthId                                             */
    /**================================================================== */
    void findUserByAuthId(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      // Need to get the route param value

      auto params = sess->getReqUrlParameters(); // The FROM and TO in the request url
      auto [route_url, query_params] = RouteHandler::parse_query_params(std::string(req.target()));
      for (const auto &[key, value] : query_params)
      {
        std::cout << key << ": " << value << std::endl;
      };
      for (auto param : params)
      {
        std::cout << param.first << " " << param.second << std::endl;
      };

      auto query = "SELECT "
                   "id, \"authId\", \"name\" "
                   "FROM \"Users\" "
                   "WHERE \"authId\" = $1 "
                   ";";

      D(std::cout << "Find User By AuthId\n"
                  << query << std::endl;)

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      paramStrings->push_back(params["authId"]);

      auto paramLengths = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramLengths->push_back(s.size());

      auto paramFormats = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramFormats->push_back(0);

      auto apiResult = std::make_shared<std::string>();
      //      auto foundMove = std::make_shared<TTTModel::PlayerMove>();

      auto endTransaction = [req, apiResult, send](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Find User By AuthId has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Find User By AuthId execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        return send(std::move(Rest::Response::success_request(req, apiResult->c_str())));
      };

      auto queryFindUserByAuthId = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Find User By AuthId has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["fetchUserByAuthId"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Find User By AuthId execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Find User By AuthId Query has been shown as finished when null res returned." << std::endl;)
                                   return;
                                 }
                                 return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
                                 //
                               });

          return;
        }
        int rows = PQntuples(res);
        int cols = PQnfields(res);
        // for (int i = 0; i < cols; i++)
        // {
        //   Oid typeOid = PQftype(res, i); // Get the data type OID of the column
        //   printf("Column %d has data type OID: %u\n", i, typeOid);
        //   const char *field_name = PQfname(res, i);
        //   std::cout << "Column " << i << " name: " << field_name << std::endl;
        // }

        if (rows == 0)
        {
          root["fetchUserByAuthId"] = json::array();
          apiResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        try
        {
          root["fetchUserByAuthId"] = json::array();
          for (int row = 0; row < rows; row++)
          {
            LivePostsModel::User user = LivePostsModel::PG::Users::fromPGRes(res, cols, row);
            root["fetchUserByAuthId"].push_back(user);
          }
        }
        catch (const std::exception &e)
        {
          return send(std::move(Rest::Response::server_error(req, e.what())));
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
        apiResult->assign(root.dump());
        // std::cout << "=====> Find User By AuthId query found: " << root.dump() << std::endl;

        dbclient->asyncQuery("COMMIT", endTransaction);
      };

      auto beginTransaction = [=](PGresult *beginRes)
      {
        if (!beginRes)
        {
          D(std::cerr << "beginTransaction Find User By AuthId Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Find User By AuthId Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncParamQuery(query, *paramStrings, *paramLengths, *paramFormats, queryFindUserByAuthId);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };

    /**================================================================== */
    /** Get User By Id                                             */
    /**================================================================== */
    void findUserById(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      // Need to get the route param value

      auto params = sess->getReqUrlParameters(); // The FROM and TO in the request url
      auto [route_url, query_params] = RouteHandler::parse_query_params(std::string(req.target()));
      for (const auto &[key, value] : query_params)
      {
        std::cout << key << ": " << value << std::endl;
      };
      for (auto param : params)
      {
        std::cout << param.first << " " << param.second << std::endl;
      };

      auto query = "SELECT "
                   "id, \"authId\", \"name\" "
                   "FROM \"Users\" "
                   "WHERE \"id\" = $1 "
                   ";";

      D(std::cout << "Find User By Id\n"
                  << query << std::endl;)

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      paramStrings->push_back(params["id"]);

      auto paramLengths = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramLengths->push_back(s.size());

      auto paramFormats = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramFormats->push_back(0);

      auto apiResult = std::make_shared<std::string>();
      //      auto foundMove = std::make_shared<TTTModel::PlayerMove>();

      auto endTransaction = [req, apiResult, send](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Find User By Id has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Find User By Id execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        return send(std::move(Rest::Response::success_request(req, apiResult->c_str())));
      };

      auto queryFindUserByAuthId = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Find User By Id has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["fetchUserById"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Find User By Id execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Find User By Id Query has been shown as finished when null res returned." << std::endl;)
                                   return;
                                 }
                                 return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
                                 //
                               });

          return;
        }
        int rows = PQntuples(res);
        int cols = PQnfields(res);
        // for (int i = 0; i < cols; i++)
        // {
        //   Oid typeOid = PQftype(res, i); // Get the data type OID of the column
        //   printf("Column %d has data type OID: %u\n", i, typeOid);
        //   const char *field_name = PQfname(res, i);
        //   std::cout << "Column " << i << " name: " << field_name << std::endl;
        // }

        if (rows == 0)
        {
          root["fetchUserById"] = json::array();
          apiResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        try
        {
          root["fetchUserById"] = json::array();
          for (int row = 0; row < rows; row++)
          {
            LivePostsModel::User user = LivePostsModel::PG::Users::fromPGRes(res, cols, row);
            root["fetchUserById"].push_back(user);
          }
        }
        catch (const std::exception &e)
        {
          return send(std::move(Rest::Response::server_error(req, e.what())));
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
        apiResult->assign(root.dump());
        // std::cout << "=====> Find User By AuthId query found: " << root.dump() << std::endl;

        dbclient->asyncQuery("COMMIT", endTransaction);
      };

      auto beginTransaction = [=](PGresult *beginRes)
      {
        if (!beginRes)
        {
          D(std::cerr << "beginTransaction Find User By Id Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Find User By Id Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncParamQuery(query, *paramStrings, *paramLengths, *paramFormats, queryFindUserByAuthId);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };

    // /**================================================================== */
    // /** Update board for game Id                                          */
    // /**================================================================== */
    // void boardUpdate(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    // {
    //   // Validation check on put req body and bad request is made if not valid
    //   TTTModel::GameUpdate gameUpdateInput;
    //   try
    //   {
    //     gameUpdateInput = json::parse(req.body());
    //   }
    //   catch (json::exception &e)
    //   {
    //     json err = e.what();
    //     auto msg = err.dump();
    //     return send(std::move(Rest::Response::bad_request(req, msg.substr(1, msg.size() - 2))));
    //   }
    //   catch (const std::string &e)
    //   {
    //     return send(std::move(Rest::Response::bad_request(req, e)));
    //   }

    //   if (!TTTModel::Validate::GameUpdate(gameUpdateInput))
    //   {
    //     return send(std::move(Rest::Response::bad_request(req, "Board Update is not having valid data.")));
    //   }

    //   auto sql =
    //       "UPDATE \"Game\" "
    //       "SET \"board\"=$1 WHERE \"id\"=$2 "
    //       "RETURNING id, \"userId\", board, \"createdAt\""
    //       ";";

    //   auto paramStrings = std::make_shared<std::vector<std::string>>();
    //   paramStrings->push_back(gameUpdateInput.board);
    //   paramStrings->push_back(gameUpdateInput.gameId);

    //   auto paramLengths = std::make_shared<std::vector<int>>();
    //   for (const auto &s : *paramStrings)
    //     paramLengths->push_back(s.size());

    //   auto paramFormats = std::make_shared<std::vector<int>>();
    //   for (const auto &s : *paramStrings)
    //     paramFormats->push_back(0);

    //   auto apiResult = std::make_shared<std::string>();
    //   // Place holder for the event
    //   auto updatedGame = std::make_shared<TTTModel::Game>();

    //   auto endTransaction = [redisPublish, updatedGame, gameUpdateInput, req, apiResult, send](PGresult *endRes)
    //   {
    //     if (!endRes)
    //     {
    //       D(std::cerr << "endTransaction Board Update has been shown as finished when null res returned." << std::endl;)
    //       return;
    //     }

    //     ExecStatusType resStatus = PQresultStatus(endRes);
    //     if (resStatus != PGRES_COMMAND_OK)
    //     {
    //       json error = "COMMIT command Board Update execution failed: " + std::string(PQresultErrorMessage(endRes));
    //       auto msg = error.dump();
    //       std::cerr << msg << std::endl;
    //       return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
    //     }

    //     // Publish GameCreateEvent subject.
    //     // If not validate updatedGame means this request update is unsuccessful due to no row found
    //     try
    //     {
    //       if (TTTModel::Validate::Game(*updatedGame))
    //       {
    //         TTTEvents::GameUpdateByIdEvent event;
    //         event.gameId = updatedGame->id;
    //         event.board = updatedGame->board;
    //         event.result = gameUpdateInput.result;
    //         json jsonEvent = event;

    //         /////
    //         cntLivePostMessage++;
    //         std::cout << "  Sending to publish: Subject (" << TTTEvents::SubjectNames.at(event.subject) << ")"
    //                   << " " << cntLivePostMessage << " TTT messages made. "
    //                   << std::endl;
    //         ///
    //         redisPublish->Send(
    //             std::string(TTTEvents::SubjectNames.at(event.subject)),
    //             jsonEvent.dump());
    //       }

    //       return send(std::move(Rest::Response::success_request(req, apiResult->c_str())));
    //     }
    //     catch (json::exception &e)
    //     {
    //       json err = e.what();
    //       auto msg = err.dump();
    //       return send(std::move(Rest::Response::bad_request(req, msg.substr(1, msg.size() - 2))));
    //     }
    //     catch (const std::string &e)
    //     {
    //       return send(std::move(Rest::Response::server_error(req, e)));
    //     }
    //   };

    //   auto paramQueryUpdateGame = [=](PGresult *res)
    //   {
    //     if (!res)
    //     {
    //       D(std::cerr << "Query Board Update has been shown as finished when null res returned." << std::endl;)
    //       return;
    //     }

    //     json root;
    //     root["updateGame"] = {};

    //     ExecStatusType resStatus = PQresultStatus(res);
    //     if (resStatus != PGRES_TUPLES_OK)
    //     {
    //       json error = "Query Board Update execution failed: " + std::string(PQresultErrorMessage(res));
    //       auto msg = error.dump();
    //       std::cerr << msg << std::endl;
    //       dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
    //                            {
    //                              if (!res)
    //                              {
    //                                D(std::cerr << "ROLLBACK Board Update Query has been shown as finished when null res returned." << std::endl;)
    //                                return;
    //                              }
    //                              return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
    //                              //
    //                            });

    //       return;
    //     }
    //     int rows = PQntuples(res);
    //     int cols = PQnfields(res);
    //     if (rows == 0)
    //     {
    //       root["updateGame"] = "No rows returned from query";
    //       apiResult->assign(root.dump());
    //       dbclient->asyncQuery("COMMIT", endTransaction);
    //       return;
    //     }

    //     try
    //     {
    //       // Only one row is returned
    //       *updatedGame = TTTModel::PG::Game::fromPGRes(res, cols, 0);
    //       json jsonGame = *updatedGame;
    //       jsonGame["id"] = PQgetvalue(res, 0, 0); // Game id
    //       jsonGame["result"] = gameUpdateInput.result;
    //       root["updateGame"] = jsonGame;
    //     }
    //     catch (const std::string &e)
    //     {
    //       return send(std::move(Rest::Response::server_error(req, e)));
    //     }
    //     apiResult->assign(root.dump());
    //     dbclient->asyncQuery("COMMIT", endTransaction);
    //   };

    //   auto beginTransaction = [=](PGresult *beginRes)
    //   {
    //     if (!beginRes)
    //     {
    //       D(std::cerr << "beginTransaction Board Update Query has been shown as finished when null res returned." << std::endl;)
    //       return;
    //     }

    //     ExecStatusType resStatus = PQresultStatus(beginRes);
    //     if (resStatus != PGRES_COMMAND_OK)
    //     {
    //       json error = "Begin command Board Update Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
    //       auto msg = error.dump();
    //       std::cerr << msg << std::endl;
    //       return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
    //     }

    //     dbclient->asyncParamQuery(sql, *paramStrings, *paramLengths, *paramFormats, paramQueryUpdateGame);
    //   };

    //   dbclient->asyncQuery("BEGIN", beginTransaction);
    // };

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
      root["title"] = "LivePost Service";
      root["description"] = "The Live Post UI interaction "
                            "to test the stack of NetProc.";
      root["navCards"] = json::array();
      root["popularCards"] = json::array();

      json card;
      card["title"] = "Laboratory Collection";
      card["catchPhrase"] = "Time complexity of algorithm is how fast it perform the algorithm. Fast solutions are O(n), slow solutions are O(n2) or greater.";

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
      user["id"] = "temp@hello.co.nz";
      user["name"] = "User at Hello.co.nz";
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
      post["user"] = "300";
      post["reactions"]["thumbsUp"] = 0;
      post["reactions"]["hooray"] = 2;
      post["reactions"]["heart"] = 4;
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