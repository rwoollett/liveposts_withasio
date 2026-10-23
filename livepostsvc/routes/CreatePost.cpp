#include "CreatePost.h"
#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include "apiserver/RouteHandler.h"
#include <redis_pubsub/publish/Publish.h>
#include <nlohmann/json.hpp>
#include "livepostsmodel/pq.h"

#include <memory>
#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>

using json = nlohmann::json;
using Rest::makeParamVectors;
using Rest::RequestContext;
using Rest::safePgErrorForJson;
using Timestamp::parseDate;

namespace Routes::LivePosts
{
  CreatePostOp::CreatePostOp(RequestContext ctx)
      : DbOpBase(std::move(ctx)),
        workStep_(CreatePostWorkStep::AuthFind)
  {
  }

  bool CreatePostOp::parseReq()
  {
    try
    {
      postCreate_ = json::parse(ctx_.req.body());
    }
    catch (...)
    {
      sendError("Invalid JSON");
      return false;
    }

    if (!LivePostsModel::Validate::PostCreate(postCreate_))
    {
      sendError("Invalid Post Create data");
      return false;
    }

    // Build auth check paramStrings_ (owned)
    paramStringsAuthFind_.clear();
    paramStringsAuthFind_.push_back(ctx_.authUserId);

    paramValuesAuthFind_.clear();
    for (auto &s : paramStringsAuthFind_)
      paramValuesAuthFind_.push_back(s.c_str());

    paramLengthsAuthFind_.assign(paramStringsAuthFind_.size(), 0);
    paramFormatsAuthFind_.assign(paramStringsAuthFind_.size(), 0);

    return true;
  }

  void CreatePostOp::doWork()
  {
    auto self = shared_from_this();

    switch (workStep_)
    {
    case CreatePostWorkStep::AuthFind:
      ctx_.db->asyncExecParams(
          selectAuthSql,
          paramValuesAuthFind_,
          paramLengthsAuthFind_,
          paramFormatsAuthFind_,
          static_cast<int>(paramValuesAuthFind_.size()),
          [self](PGresult *res)
          { self->handleWork(res); });

      break;
    case CreatePostWorkStep::Create:
      ctx_.db->asyncExecParams(
          createPostSql,
          paramValues_,
          paramLengths_,
          paramFormats_,
          static_cast<int>(paramValues_.size()),
          [self](PGresult *res)
          { self->handleWork(res); });

      break;
    case CreatePostWorkStep::Done:
      // Should never be called

      break;
    }
  }

  void CreatePostOp::handleWork(PGresult *res)
  {
    if (!onWorkResult(res))
    {
      state_ = State::Rollback;
      return advance();
    }

    // Find next work from previous state of CreatePostWorkStep
    workAdvance();

    if (workStep_ != CreatePostWorkStep::Done)
    {
      state_ = State::Work;
    }
    else
    {
      state_ = State::Commit;
    }
    advance();
  }

  bool CreatePostOp::onWorkResult(PGresult *res)
  {
    if (!res)
    {
      sendError("Create post failed: " + ctx_.db->connErrorMessage());
      return false;
    }

    auto status = PQresultStatus(res);

    // Ignore PGRES_COMMAND_OK (completion of INSERT)
    if (status == PGRES_COMMAND_OK)
    {
      PQclear(res);
      return true;
    }

    // Fatal error
    if (status == PGRES_FATAL_ERROR)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Create post failed: " + err);
      return false;
    }

    // Only PGRES_TUPLES_OK is the real INSERT ... RETURNING row
    if (status != PGRES_TUPLES_OK)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Create post failed: " + err);
      return false;
    }

    switch (workStep_)
    {
    case CreatePostWorkStep::AuthFind:
      return onAuthFind(res);
    case CreatePostWorkStep::Create:
      return onCreate(res);
    case CreatePostWorkStep::Done:
      return true;
    }
    return false;
  }

  bool CreatePostOp::onAuthFind(PGresult *res)
  {
    try
    {
      json root;
      int rows = PQntuples(res);

      if (rows == 0)
      {
        PQclear(res);
        mt_logging::logger().log(
            {fmt::format(
                 " CreatePostOp::onAuthFind: Not successfully authorized to create post. user email {}  user auth id {}",
                 ctx_.authUserEmail, ctx_.authUserId),
             mt_logging::LogLevel::Info,
             true});

        sendError("Not successfully authorized to create post");
        return false;
      }

      int cols = PQnfields(res);
      postUser_ = LivePostsModel::PG::Users::fromPGRes(res, cols, 0);

      // Build create post paramStrings_ (owned)
      paramStrings_.clear();
      paramStrings_.push_back(postCreate_.title);
      paramStrings_.push_back(postCreate_.content);
      paramStrings_.push_back(std::to_string(postUser_.id));
      paramStrings_.push_back(LivePostsModel::to_string(LivePostsModel::PostStatus::PendingModeration));

      // Build paramValues_
      paramValues_.clear();
      for (auto &s : paramStrings_)
        paramValues_.push_back(s.c_str());

      // lengths + formats
      paramLengths_.assign(paramStrings_.size(), 0);
      paramFormats_.assign(paramStrings_.size(), 0);

      PQclear(res);
      return true;
    }
    catch (const std::exception &e)
    {
      PQclear(res);
      sendError(e.what());
      return false;
    }
  }

  bool CreatePostOp::onCreate(PGresult *res)
  {
    try
    {
      int cols = PQnfields(res);
      newPost_ = LivePostsModel::PG::Posts::fromPGRes(res, cols, 0);
      PQclear(res);

      json root;
      root["createPost"] = newPost_;
      resultBody_ = root.dump();
      return true;
    }
    catch (const std::exception &e)
    {
      PQclear(res);
      sendError(e.what());
      return false;
    }
  }

  void CreatePostOp::onCommit(PGresult *res)
  {
    if (!res)
    {
      sendServerError("COMMIT failed: " + ctx_.db->connErrorMessage());
      return;
    }

    auto status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendServerError("COMMIT failed: " + err);
      return;
    }
    PQclear(res);

    try
    {
      LivePostsEvents::PostCreateEvent event;
      event.id = newPost_.id;
      event.wsId = postCreate_.wsId;
      json jsonEvent = event;

      ctx_.redis->produce(
          std::string(LivePostsEvents::SubjectNames.at(event.subject)),
          {std::pair<std::string, std::string>{"postId", std::to_string(newPost_.id)},
           std::pair<std::string, std::string>{"wsId", postCreate_.wsId}});

      sendSuccess(resultBody_);
      state_ = State::Done;
      return;
    }
    catch (const std::exception &e)
    {
      std::string err = e.what();
      mt_logging::logger().log({err, mt_logging::LogLevel::Error, true});
      sendError("COMMIT redis event failed: " + err);
    }
  }

}