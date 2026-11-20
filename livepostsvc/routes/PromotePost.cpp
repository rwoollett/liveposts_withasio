#include "PromotePost.h"

#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include <redis_pubsub/publish/Publish.h>
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

  PromotePostOp::PromotePostOp(RequestContext ctx)
      : ctx_(std::move(ctx)), send_(std::move(ctx_.send))
  {
  }

  void PromotePostOp::start()
  {
    if (!parseReq())
      return; // parseReq already sent error

    doWork();
  }

  bool PromotePostOp::parseReq()
  {
    params_ = ctx_.session->getReqUrlParameters();

    if (params_["postId"].empty())
    {
      sendError("Invalid promote post");
      return false;
    }

    try
    {
      promotePostInput_ = json::parse(ctx_.req.body());
    }
    catch (...)
    {
      sendError("Invalid JSON");
      return false;
    }

    if (!LivePostsModel::Validate::PostStage(promotePostInput_))
    {
      sendError("Invalid Promote post data");
      return false;
    }

    std::string slug = slugger::make_slug(promotePostInput_.title, params_["postId"], 30);
    std::cout << slug << "\n";

    // Build paramStrings_ (owned)
    paramStrings_.clear();
    paramStrings_.push_back(LivePostsModel::to_string(LivePostsModel::PostStatus::Live));
    paramStrings_.push_back(slug);
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

  void PromotePostOp::doWork()
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

  void PromotePostOp::onWorkResult(PGresult *res)
  {
    if (!res)
    {
      sendError("Promote post failed: " + ctx_.db->connErrorMessage());
      return;
    }

    auto status = PQresultStatus(res);

    // Fatal error
    if (status == PGRES_FATAL_ERROR)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Promote post failed: " + err);
      return;
    }

    // Only PGRES_TUPLES_OK is the real INSERT ... RETURNING row
    if (status != PGRES_TUPLES_OK)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Promote post failed: " + err);
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
        root["promotePost"] = "No rows returned from query";
        PQclear(res);
        sendSuccess(root.dump());
        return;
      }

      // Only one row is returned
      updatedPostPromote_ = LivePostsModel::PG::Posts::fromPGRes(res, cols, 0);

      root["promotePost"] = updatedPostPromote_;
      // Set status to Live:
      // 1) produce prerender job on stream PostRenderEvent
      // 2) and postpublishevent for caller wsid

      LivePostsEvents::PostRenderEvent eventStage;
      eventStage.id = updatedPostPromote_.id;
      eventStage.slug = updatedPostPromote_.slug;

      json jsonEvent = eventStage;
      // Use partition on post id
      int partition = updatedPostPromote_.id % 4;
      auto stream =
          fmt::format("{}_{}", 
            std::string(LivePostsEvents::SubjectNames.at(eventStage.subject)), 
            partition);

      ctx_.redis->produce(
          stream,
          {std::pair<std::string, std::string>{"postId", std::to_string(updatedPostPromote_.id)}});


      // and send PostStatusChangedEvent
      LivePostsEvents::PostStatusChangedEvent eventStatusChanged;
      eventStatusChanged.postId = updatedPostPromote_.id;
      eventStatusChanged.status = LivePostsModel::PostStatus::Live;
      eventStatusChanged.authorUserId = updatedPostPromote_.userId;
      eventStatusChanged.slug = updatedPostPromote_.slug;
      eventStatusChanged.wsId = promotePostInput_.wsId;

      json jsonStatusChanged = eventStatusChanged;
      ctx_.redis->publish(
          std::getenv("REDIS_GATEWAY_CHANNEL") == nullptr ? "ws_events" : std::getenv("REDIS_GATEWAY_CHANNEL"),
          jsonStatusChanged.dump());

      PQclear(res);
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
  void PromotePostOp::sendError(const std::string &msg)
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

  void PromotePostOp::sendSuccess(const std::string &body)
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