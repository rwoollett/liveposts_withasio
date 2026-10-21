#include "UpdatePost.h"
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
  UpdatePostOp::UpdatePostOp(RequestContext ctx)
      : DbOpBase(std::move(ctx)),
        workStep_(UpdatePostWorkStep::Done)
  {
  }

  bool UpdatePostOp::parseReq()
  {
    params_ = ctx_.session->getReqUrlParameters();

    if (params_["postId"].empty())
    {
      sendError("Invalid update post requires post id");
      return false;
    }

    try
    {
      postUpdate_ = json::parse(ctx_.req.body());
    }
    catch (...)
    {
      sendError("Invalid JSON");
      return false;
    }

    if (!LivePostsModel::Validate::PostUpdate(postUpdate_))
    {
      sendError("Invalid Post Update data");
      return false;
    }

    // Build paramStrings_ (owned)
    paramStrings_.clear();
    paramStrings_.push_back(postUpdate_.title);
    paramStrings_.push_back(postUpdate_.content);
    paramStrings_.push_back(LivePostsModel::to_string(LivePostsModel::PostStatus::PendingModeration));
    paramStrings_.push_back(params_["postId"]);

    // Build paramValues_
    paramValues_.clear();
    for (auto &s : paramStrings_)
      paramValues_.push_back(s.c_str());

    // lengths + formats
    paramLengths_.assign(paramStrings_.size(), 0);
    paramFormats_.assign(paramStrings_.size(), 0);

    return true;
  }

  void UpdatePostOp::doWork()
  {
    auto self = shared_from_this();
    ctx_.db->asyncExecParams(
        sql,
        paramValues_,
        paramLengths_,
        paramFormats_,
        static_cast<int>(paramValues_.size()),
        [self](PGresult *res)
        { self->handleWork(res); });
  }

  void UpdatePostOp::handleWork(PGresult *res)
  {
    if (!onWorkResult(res))
    {
      state_ = State::Rollback;
      return advance();
    }
    if (workStep_ != UpdatePostWorkStep::Done)
    {
      state_ = State::Work;
    }
    else
    {
      state_ = State::Commit;
    }
    advance();
  }

  bool UpdatePostOp::onWorkResult(PGresult *res)
  {
    if (!res)
    {
      sendError("Update post failed: " + ctx_.db->connErrorMessage());
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
      sendError("Update post failed: " + err);
      return false;
    }

    // Only PGRES_TUPLES_OK is the real INSERT ... RETURNING row
    if (status != PGRES_TUPLES_OK)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Update post failed: " + err);
      return false;
    }

    try
    {
      int cols = PQnfields(res);
      updatedPost_ = LivePostsModel::PG::Posts::fromPGRes(res, cols, 0);
      PQclear(res);

      json root;
      root["updatePost"] = updatedPost_;
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

  void UpdatePostOp::onCommit(PGresult *res)
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
      LivePostsEvents::PostUpdateEvent event;
      event.id = updatedPost_.id;
      event.wsId = postUpdate_.wsId;
      json jsonEvent = event;

      ctx_.redis->produce(
          std::string(LivePostsEvents::SubjectNames.at(event.subject)),
          {std::pair<std::string, std::string>{"postId", std::to_string(updatedPost_.id)},
           std::pair<std::string, std::string>{"wsId", postUpdate_.wsId}});

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