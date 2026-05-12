#include "Routes.h"

#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include "apiserver/RouteHandler.h"
#include <pubsub/publish/Publish.h>
#include <nlohmann/json.hpp>
#include "livepostsmodel/model.h"
#include "livepostsmodel/pq.h"
#include "CreatePost.h"
#include "../prerender/Prerender.h"
#include "slugger.h"

#include <memory>
#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>

using json = nlohmann::json;
using Timestamp::parseDate;
using Rest::RouteHandler;

namespace Routes
{

  namespace LivePosts
  {

    void allocatePost(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      auto query = "SELECT "
                   "\"Posts\".\"id\", \"title\", \"slug\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
                   "\"allocated\", \"live\", "
                   "\"Users\".\"name\" AS \"userName\" "
                   "FROM \"Posts\" LEFT JOIN \"Users\" ON "
                   "\"Posts\".\"userId\"=\"Users\".\"id\" "
                   "WHERE \"Posts\".\"allocated\"='f' "
                   ";";

      auto updateSql =
          "UPDATE \"Posts\" "
          "SET \"allocated\"=$1 "
          "WHERE \"id\"=$2 "
          "RETURNING id, \"title\", \"slug\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
          "\"allocated\", \"live\", "
          "(SELECT \"name\" FROM \"Users\" WHERE \"Users\".\"id\" = \"Posts\".\"userId\") AS \"userName\""
          ";";

      std::cout << "AllocatePost query\n"
                << query << std::endl
                << "AllocatePost update query\n"
                << updateSql << std::endl;

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      auto paramLengths = std::make_shared<std::vector<int>>();
      auto paramFormats = std::make_shared<std::vector<int>>();
      auto apiResult = std::make_shared<std::string>();
      auto foundPost = std::make_shared<LivePostsModel::Post>();

      auto endTransaction = [req, apiResult, send](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Allocate Post has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Allocate Post execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        return send(std::move(Rest::Response::success_request(req, apiResult->c_str())));
      };

      auto paramQueryAllocatedPost = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Allocate Post has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["allocatePost"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Update Allocate Post execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Allocate Post has been shown as finished when null res returned." << std::endl;)
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
          root["allocatePost"] = json::array();
          apiResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        try
        {
          // Only one row is returned
          root["allocatePost"] = json::array();
          root["allocatePost"].push_back(LivePostsModel::PG::Posts::fromPGRes(res, cols, 0));
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
        apiResult->assign(root.dump());
        dbclient->asyncQuery("COMMIT", endTransaction);
      };

      auto queryAllocatedPost = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Allocate Post has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["allocatePost"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Allocate Post execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Allocate Post Query has been shown as finished when null res returned." << std::endl;)
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
          root["allocatePost"] = json::array();
          apiResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        try
        {
          // Only one row is returned
          root["allocatePost"] = json::array();
          *foundPost = LivePostsModel::PG::Posts::fromPGRes(res, cols, 0);
          root["allocatePost"].push_back(*foundPost);
        }
        catch (const std::string &e)
        {
          return send(std::move(Rest::Response::server_error(req, e)));
        }
        apiResult->assign(root.dump());
        std::cout << "=====> Allocated Post query found: " << root.dump() << std::endl;

        // Now before end transaction update allocated = true for foundPost in DB
        paramStrings->push_back(std::to_string(true));
        paramStrings->push_back(std::to_string(foundPost->id));
        for (const auto &s : *paramStrings)
          paramLengths->push_back(s.size());
        for (const auto &s : *paramStrings)
          paramFormats->push_back(0);

        dbclient->asyncParamQuery(updateSql, *paramStrings, *paramLengths, *paramFormats, paramQueryAllocatedPost);
      };

      auto beginTransaction = [=](PGresult *beginRes)
      {
        if (!beginRes)
        {
          D(std::cerr << "beginTransaction Allocate Post Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Allocate Post Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncQuery(query, queryAllocatedPost);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };


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


  }
}