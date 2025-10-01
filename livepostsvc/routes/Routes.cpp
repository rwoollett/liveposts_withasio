#include "Routes.h"

#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include "apiserver/RouteHandler.h"
#include "redisPublish/Publish.h"
#include <nlohmann/json.hpp>
#include "livepostsmodel/model.h"
#include "livepostsmodel/pq.h"

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>
#include "cookies/parse.h"

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

    bool verify_jwt(const std::string &auth_header)
    {
      auto jwt_secret_key = std::getenv("JWT_SECRET_KEY");
      std::string keyword = "Bearer";
      std::string token;
      using traits = jwt::traits::nlohmann_json;

      // Find the position of "Bearer"
      size_t pos = auth_header.find(keyword);
      if (pos != std::string::npos)
      {
        // Extract the token after "Bearer"
        token = auth_header.substr(pos + keyword.length());

        // Trim leading spaces (optional)
        token.erase(0, token.find_first_not_of(" "));
      }
      else
      {
        return false;
      }

      try
      {
        auto decoded = jwt::decode<traits>(token);
        D(std::cout << " decoded:" << decoded.get_payload() << std::endl;)
        auto verifier = jwt::verify<traits>()
                            .allow_algorithm(jwt::algorithm::hs256{std::string(jwt_secret_key)});
        //.with_issuer("your_issuer");

        verifier.verify(decoded);
        return true; // Token is valid
      }
      catch (const std::exception &e)
      {
        std::cerr << "JWT verification failed: " << e.what() << std::endl;
        return false; // Invalid token
      }
    }

    bool authorize_request(const http::request<http::string_body> &req, SendCall &send) 
    {
      try {
        auto cookies{Cookies::cookie_map(req[http::field::cookie])}; // use the python RFC6265-compliant.
        D(std::cout << "Cookie values" << std::endl;
        for (const auto &[key, val] : cookies)
        {
          std::cout << key << " = " << val << '\n';
        })

        if (!cookies.contains("auth:sess")) {
          send(Rest::Response::unauthorized_request(req, "No credentials (cookies) found."));
          return false;
        }

        std::string decoded = Cookies::base64_decode(cookies.at("auth:sess"));
        json auth_sess = json::parse(decoded);
        if (!verify_jwt("Bearer " + auth_sess.value("jwt", "empty")))
        {
          send(Rest::Response::unauthorized_request(req, "Unauthorized"));
          return false;
        }
        
        // Authorized
        return true;
        
      } catch (const std::exception &e) 
      {
        json err = e.what();
        auto msg = err.dump();
        send(std::move(Rest::Response::bad_request(req, msg.substr(1, msg.size() - 2))));
        return false;
      }

    }

    /**=============================================================== */
    /** Create post                                                    */
    /**=============================================================== */
    void createPost(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {

      if (!authorize_request(req, send)) {
        std::cout << "        Unauthorized\n";
        return;
      }
      std::cout << "        Authorized\n";
      // Validation check on put req body and bad request is made if not valid
      LivePostsModel::Post post;

      std::cout << req.body() << std::endl;
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
          "RETURNING id, \"title\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
          "\"allocated\", \"live\", "
          "(SELECT \"name\" FROM \"Users\" WHERE \"Users\".\"id\" = \"Posts\".\"userId\") AS \"userName\""
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

        // Publish PostCreateEvent subject.
        // If not validate newPost means this request update is unsuccessful due to no row found
        try
        {
          if (LivePostsModel::Validate::Posts(*newPost))
          {
            LivePostsEvents::PostCreateEvent event;
            event.id = newPost->id;
            event.userId = newPost->userId;
            event.title = newPost->title;
            event.userName = newPost->userName;
            event.live = newPost->live;
            event.allocated = newPost->allocated;
            json jsonEvent = event;

            cntLivePostMessage++;
            std::cout << "  Sending to publish: Subject (" << LivePostsEvents::SubjectNames.at(event.subject) << ")"
                      << " " << cntLivePostMessage << " LivePost messages made. "
                      << std::endl;
            redisPublish->Send(
                std::string(LivePostsEvents::SubjectNames.at(event.subject)),
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

    /**======================================================================= */
    /** Get the next unallocated Post Created as  (retrieve only one or none)  */
    /**======================================================================= */
    void allocatePost(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {

      auto query = "SELECT "
                   "\"Posts\".\"id\", \"title\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
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
          "RETURNING id, \"title\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
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

    /**====================================================================== */
    /** Update Post as live true (means the file is created at www work folder
    /**====================================================================== */
    void stagePost(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {
      // Validation check on put req body and bad request is made if not valid
      LivePostsModel::PostStage stagePostInput;
      try
      {
        stagePostInput = json::parse(req.body());
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

      if (!LivePostsModel::Validate::PostStage(stagePostInput))
      {
        return send(std::move(Rest::Response::bad_request(req, "Stage Post input is not having valid data.")));
      }

      auto sql =
          "UPDATE \"Posts\" "
          "SET \"live\"=$1 WHERE \"id\"=$2 "
          "RETURNING id, \"title\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
          "\"allocated\", \"live\", "
          "(SELECT \"name\" FROM \"Users\" WHERE \"Users\".\"id\" = \"Posts\".\"userId\") AS \"userName\""
          ";";

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      paramStrings->push_back(std::to_string(stagePostInput.live));
      paramStrings->push_back(std::to_string(stagePostInput.postId));

      auto paramLengths = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramLengths->push_back(s.size());

      auto paramFormats = std::make_shared<std::vector<int>>();
      for (const auto &s : *paramStrings)
        paramFormats->push_back(0);

      auto apiResult = std::make_shared<std::string>();
      // Place holder for the event
      auto updatedPostStage = std::make_shared<LivePostsModel::Post>();

      auto endTransaction = [redisPublish, updatedPostStage, stagePostInput, req, apiResult, send](PGresult *endRes)
      {
        if (!endRes)
        {
          D(std::cerr << "endTransaction Stage Post has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(endRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "COMMIT command Stage Post execution failed: " + std::string(PQresultErrorMessage(endRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        // Publish PostStageEvent subject.
        // If not validate updatedPostStage means this request update is unsuccessful due to no row found
        try
        {
          if (LivePostsModel::Validate::Posts(*updatedPostStage))
          {
            LivePostsEvents::PostStageEvent event;
            event.id = updatedPostStage->id;
            event.slug = std::to_string(updatedPostStage->id) + updatedPostStage->title;
            json jsonEvent = event;

            /////
            cntLivePostMessage++;
            std::cout << "  Sending to publish: Subject (" << LivePostsEvents::SubjectNames.at(event.subject) << ")"
                      << " " << cntLivePostMessage << " LivePosts messages made. "
                      << std::endl;
            ///
            redisPublish->Send(
                std::string(LivePostsEvents::SubjectNames.at(event.subject)),
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

      auto paramQueryPostStageUpdate = [=](PGresult *res)
      {
        if (!res)
        {
          D(std::cerr << "Query Stage Post Update has been shown as finished when null res returned." << std::endl;)
          return;
        }

        json root;
        root["stagePost"] = {};

        ExecStatusType resStatus = PQresultStatus(res);
        if (resStatus != PGRES_TUPLES_OK)
        {
          json error = "Query Stage Post Update execution failed: " + std::string(PQresultErrorMessage(res));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          dbclient->asyncQuery("ROLLBACK", [=](PGresult *res)
                               {
                                 if (!res)
                                 {
                                   D(std::cerr << "ROLLBACK Stage Post Update Query has been shown as finished when null res returned." << std::endl;)
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
          root["stagePost"] = "No rows returned from query";
          apiResult->assign(root.dump());
          dbclient->asyncQuery("COMMIT", endTransaction);
          return;
        }

        try
        {
          // Only one row is returned
          *updatedPostStage = LivePostsModel::PG::Posts::fromPGRes(res, cols, 0);
          root["stagePost"] = *updatedPostStage;
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
          D(std::cerr << "beginTransaction Stage Post Query has been shown as finished when null res returned." << std::endl;)
          return;
        }

        ExecStatusType resStatus = PQresultStatus(beginRes);
        if (resStatus != PGRES_COMMAND_OK)
        {
          json error = "Begin command Stage Post Query execution failed: " + std::string(PQresultErrorMessage(beginRes));
          auto msg = error.dump();
          std::cerr << msg << std::endl;
          return send(std::move(Rest::Response::server_error(req, msg.substr(1, msg.size() - 2))));
        }

        dbclient->asyncParamQuery(sql, *paramStrings, *paramLengths, *paramFormats, paramQueryPostStageUpdate);
      };

      dbclient->asyncQuery("BEGIN", beginTransaction);
    };


    /**================================================================== */
    /** Get Posts                                                         */
    /**================================================================== */
    void fetchPosts(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send)
    {

      auto query = "SELECT "
                   "\"Posts\".\"id\", \"title\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
                   "\"allocated\", \"live\", "
                   "\"Users\".\"name\" AS \"userName\" "
                   "FROM \"Posts\" LEFT JOIN \"Users\" ON \"Posts\".\"userId\" = \"Users\".\"id\""
                   ";";

      D(std::cout << "FetchPost query\n"
                  << query << std::endl;)

      auto paramStrings = std::make_shared<std::vector<std::string>>();
      auto paramLengths = std::make_shared<std::vector<int>>();
      auto paramFormats = std::make_shared<std::vector<int>>();
      auto apiResult = std::make_shared<std::string>();

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
            //            std::cout << "userName: " << std::string(PQgetvalue(res, row, 10)) << std::endl;
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
      if (!authorize_request(req, send)) {
        std::cout << "        Unauthorized\n";
        return;
      }
      std::cout << "        Authorized\n";

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