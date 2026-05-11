#include "StagePost.h"

#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include <pubsub/publish/Publish.h>
#include <nlohmann/json.hpp>
#include <mtlog/mt_log.hpp>
#include "livepostsmodel/pq.h"
#include "slugger.h"
#include "../prerender/Prerender.h"

using json = nlohmann::json;
using Rest::RouteHandler;
using Timestamp::parseDate;

namespace Routes::LivePosts
{

  StagePostOp::StagePostOp(RequestContext ctx)
      : ctx_(std::move(ctx)), send_(std::move(ctx_.send))
  {
  }

  void StagePostOp::start()
  {
    if (!parseReq())
      return; // parseReq already sent error

    doWork();
  }

  bool StagePostOp::parseReq()
  {
    try
    {
      stagePostInput_ = json::parse(ctx_.req.body());
    }
    catch (...)
    {
      sendError("Invalid JSON");
      return false;
    }

    if (!LivePostsModel::Validate::PostStage(stagePostInput_))
    {
      sendError("Invalid Game data");
      return false;
    }

    std::string slug = slugger::make_slug(stagePostInput_.title, std::to_string(stagePostInput_.postId), 30);
    std::cout << slug << "\n";

    // Build paramStrings_ (owned)
    paramStrings_.clear();
    paramStrings_.push_back(std::to_string(stagePostInput_.live));
    paramStrings_.push_back(slug);
    paramStrings_.push_back(std::to_string(stagePostInput_.postId));

    // Build paramValues_
    paramValues_.clear();
    for (auto &s : paramStrings_)
      paramValues_.push_back(s.c_str());

    // lengths + formats
    paramLengths_.assign(paramStrings_.size(), 0);
    paramFormats_.assign(paramStrings_.size(), 0);

    return true;
  }

  void StagePostOp::doWork()
  {
    auto self = shared_from_this();
    ctx_.db->asyncExecParams(
        sql,
        paramValues_,
        paramLengths_,
        paramFormats_,
        static_cast<int>(paramValues_.size()),
        [self](PGresult *res)
        { self->onWorkResult(res); });
  }

  void StagePostOp::onWorkResult(PGresult *res)
  {
    if (!res)
    {
      sendError("Stage post failed: " + ctx_.db->connErrorMessage());
      return;
    }

    auto status = PQresultStatus(res);

    // Fatal error
    if (status == PGRES_FATAL_ERROR)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Stage post failed: " + err);
      return;
    }

    // Only PGRES_TUPLES_OK is the real INSERT ... RETURNING row
    if (status != PGRES_TUPLES_OK)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Stage post failed: " + err);
      return;
    }

    // --- Success path: parse the returned rows ---
    try
    {
      int rows = PQntuples(res);
      int cols = PQnfields(res);
      json root;
      if (rows == 0)
      {
        root["stagePost"] = "No rows returned from query";
        PQclear(res);
        sendSuccess(root.dump());
        return;
      }

      // Only one row is returned
      updatedPostStage_ = LivePostsModel::PG::Posts::fromPGRes(res, cols, 0);
      PQclear(res);

      json jsonPost = updatedPostStage_;
      Prerender::prerenderPost(jsonPost.dump());
      root["stagePost"] = updatedPostStage_;

      LivePostsEvents::PostStageEvent event;
      event.id = updatedPostStage_.id;
      event.slug = updatedPostStage_.slug;
      json jsonEvent = event;
      ctx_.redis->publish(
          std::string(LivePostsEvents::SubjectNames.at(event.subject)),
          jsonEvent.dump());

      sendSuccess(root.dump());
    }
    catch (const std::string &e)
    {
      PQclear(res);
      sendError(e);
    }
    catch (const std::exception &e)
    {
      PQclear(res);
      sendError(e.what());
    }
  }

  // --- Local helpers (no DbOpBase) ---
  void StagePostOp::sendError(const std::string &msg)
  {
    auto session = ctx_.session;
    auto &strand = session->strand();
    auto req = ctx_.req;
    net::dispatch(
        strand,
        [self = shared_from_this(),
         send = std::move(send_),
         req = std::move(req),
         body = std::move(msg)]() mutable
        {
          send(bad_request(req, body));
        });
  }

  void StagePostOp::sendSuccess(const std::string &body)
  {
    auto session = ctx_.session;
    auto &strand = session->strand();
    auto req = ctx_.req;
    net::dispatch(
        strand,
        [self = shared_from_this(),
         send = std::move(send_),
         req = std::move(req),
         body = std::move(body)]() mutable
        {
          send(success_request(req, body));
        });
  }
}