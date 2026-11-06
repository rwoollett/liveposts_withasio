#include "PrerenderPost.h"

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

  PrerenderPostOp::PrerenderPostOp(RequestContext ctx)
      : ctx_(std::move(ctx)), send_(std::move(ctx_.send))
  {
  }

  void PrerenderPostOp::start()
  {
    if (!parseReq())
      return; // parseReq already sent error

    doWork();
  }

  bool PrerenderPostOp::parseReq()
  {
    t0_ = std::chrono::system_clock::now();

    params_ = ctx_.session->getReqUrlParameters();

    if (params_["postId"].empty())
    {
      sendError("Invalid prerender post");
      return false;
    }

    // Build paramStrings_ (owned)
    paramStrings_.clear();
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

  void PrerenderPostOp::doWork()
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

  void PrerenderPostOp::onWorkResult(PGresult *res)
  {
    if (!res)
    {
      sendError("Prerender post failed: " + ctx_.db->connErrorMessage());
      return;
    }

    auto status = PQresultStatus(res);

    // Fatal error
    if (status == PGRES_FATAL_ERROR)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Prerender post failed: " + err);
      return;
    }

    // Only PGRES_TUPLES_OK is the real INSERT ... RETURNING row
    if (status != PGRES_TUPLES_OK)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Prerender post failed: " + err);
      return;
    }

    // --- Success path: parse the returned rows ---
    try
    {
      json root;
      int cols = PQnfields(res);
      int rows = PQntuples(res);

      if (rows == 0)
      {
        root["prerenderPost"] = "No rows returned from query";
        PQclear(res);
        sendError("Prerender post failed: No post found.");
        return;
      }

      auto t1 = std::chrono::system_clock::now();
      auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0_);
      mt_logging::logger().log({fmt::format(" PrerenderPostOp::onWorkResult async sql duration :{} ms.  Post {}",
                                            milliseconds.count(),
                                            params_["postId"]),
                                mt_logging::LogLevel::Info, true});

      prerenderPost_ = LivePostsModel::PG::Posts::fromPGRes(res, cols, 0);
      root["prerenderPost"] = prerenderPost_;

      // Status is Live:
      // 1) do prerender and send broadcast stagepostevent
      json jsonPost = prerenderPost_;
      Prerender::prerenderPost(jsonPost.dump());

      auto tA1 = std::chrono::system_clock::now();
      auto millisecondsA = std::chrono::duration_cast<std::chrono::milliseconds>(tA1 - t1);
      mt_logging::logger().log({fmt::format(" PrerenderPostOp::onWorkResult prerender duration :{} ms.  Post {}",
                                            millisecondsA.count(),
                                            params_["postId"]),
                                mt_logging::LogLevel::Info, true});

      LivePostsEvents::PostStageEvent eventStage;
      eventStage.id = prerenderPost_.id;
      eventStage.slug = prerenderPost_.slug;
      json jsonEvent = eventStage;
      ctx_.redis->publish(
          std::getenv("REDIS_GATEWAY_CHANNEL") == nullptr ? "ws_events" : std::getenv("REDIS_GATEWAY_CHANNEL"),
          jsonEvent.dump());

      auto tB1 = std::chrono::system_clock::now();
      auto millisecondsB = std::chrono::duration_cast<std::chrono::milliseconds>(tB1 - t0_);
      mt_logging::logger().log({fmt::format(" PrerenderPostOp::onWorkResult total duration :{} ms.  Post {}",
                                            millisecondsB.count(),
                                            params_["postId"]),
                                mt_logging::LogLevel::Info, true});
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
  void PrerenderPostOp::sendError(const std::string &msg)
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

  void PrerenderPostOp::sendSuccess(const std::string &body)
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