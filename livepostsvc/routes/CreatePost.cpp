#include "CreatePost.h"
#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include "apiserver/RouteHandler.h"
#include <pubsub/publish/Publish.h>
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
        workStep_(WorkStep::Done)
  {
  }

  bool CreatePostOp::parseReq()
  {
    try
    {
      post_ = json::parse(ctx_.req.body());
    }
    catch (...)
    {
      sendError("Invalid JSON");
      return false;
    }

    if (!LivePostsModel::Validate::Posts(post_))
    {
      sendError("Invalid Game data");
      return false;
    }

    // Build paramStrings_ (owned)
    paramStrings_.clear();
    paramStrings_.push_back(post_.title);
    paramStrings_.push_back(post_.content);
    paramStrings_.push_back(std::to_string(post_.userId));

    // Build paramValues_
    paramValues_.clear();
    for (auto &s : paramStrings_)
      paramValues_.push_back(s.c_str());

    // lengths + formats
    paramLengths_.assign(paramStrings_.size(), 0);
    paramFormats_.assign(paramStrings_.size(), 0);

    return true;
  }

  void CreatePostOp::doWork()
  {
    auto self = shared_from_this();
    ctx_.db->asyncExecParams(
        createPostSql,
        paramValues_,
        paramLengths_,
        paramFormats_,
        static_cast<int>(paramValues_.size()),
        [self](PGresult *res)
        { self->handleWork(res); });
  }

  void CreatePostOp::handleWork(PGresult *res)
  {
    if (!onWorkResult(res))
    {
      state_ = State::Rollback;
      return advance();
    }
    if (workStep_ != WorkStep::Done)
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

    LivePostsEvents::PostCreateEvent event;
    event.id = newPost_.id;
    event.userId = newPost_.userId;
    event.title = newPost_.title;
    event.userName = newPost_.userName;
    event.live = newPost_.live;
    event.allocated = newPost_.allocated;
    json jsonEvent = event;

    Routes::LivePosts::cntLivePostMessage++;
    mt_logging::logger().log(
        {fmt::format(
             " Sending to publish: Subject ({}) message made.",
             LivePostsEvents::SubjectNames.at(event.subject)),
         mt_logging::LogLevel::Debug,
         true});

    ctx_.redis->publish(
        std::string(LivePostsEvents::SubjectNames.at(event.subject)),
        jsonEvent.dump());

    sendSuccess(resultBody_);
    state_ = State::Done;
    return;
  }

}