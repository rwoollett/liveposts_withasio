#include "RejectPost.h"

#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include <nlohmann/json.hpp>
#include <mtlog/mt_log.hpp>
#include "livepostsmodel/pq.h"

using json = nlohmann::json;
using Rest::RouteHandler;
using Timestamp::parseDate;

namespace Routes::LivePosts
{

  RejectPostOp::RejectPostOp(RequestContext ctx)
      : ctx_(std::move(ctx)), send_(std::move(ctx_.send))
  {
  }

  void RejectPostOp::start()
  {
    if (!parseReq())
      return; // parseReq already sent error

    doWork();
  }

  bool RejectPostOp::parseReq()
  {
    params_ = ctx_.session->getReqUrlParameters();

    if (params_["postId"].empty())
    {
      sendError("Invalid reject post");
      return false;
    }

    // Build paramStrings_ (owned)
    paramStrings_.clear();
    paramStrings_.push_back(LivePostsModel::to_string(LivePostsModel::PostStatus::Rejected));
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

  void RejectPostOp::doWork()
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

  void RejectPostOp::onWorkResult(PGresult *res)
  {
    if (!res)
    {
      sendError("Reject post failed: " + ctx_.db->connErrorMessage());
      return;
    }

    auto status = PQresultStatus(res);

    // Fatal error
    if (status == PGRES_FATAL_ERROR)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Reject post failed: " + err);
      return;
    }

    // Only PGRES_TUPLES_OK is the real INSERT ... RETURNING row
    if (status != PGRES_TUPLES_OK)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Reject post failed: " + err);
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
        root["rejectPost"] = "No rows returned from query";
        PQclear(res);
        sendSuccess(root.dump());
        return;
      }

      // Only one row is returned
      updatedPostStage_ = LivePostsModel::PG::Posts::fromPGRes(res, cols, 0);
      PQclear(res);

      root["rejectPost"] = updatedPostStage_;

      // and send PostStatusChangedEvent
      LivePostsEvents::PostStatusChangedEvent eventStatusChanged;
      eventStatusChanged.postId = updatedPostStage_.id;
      eventStatusChanged.status = LivePostsModel::PostStatus::Rejected;

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
  void RejectPostOp::sendError(const std::string &msg)
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

  void RejectPostOp::sendSuccess(const std::string &body)
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