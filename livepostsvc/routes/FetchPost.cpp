#include "FetchPost.h"

#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include <pubsub/publish/Publish.h>
#include <nlohmann/json.hpp>
#include <mtlog/mt_log.hpp>
#include "livepostsmodel/pq.h"

using json = nlohmann::json;
using Rest::RouteHandler;
using Timestamp::parseDate;

namespace Routes::LivePosts
{

  FetchPostOp::FetchPostOp(RequestContext ctx)
      : ctx_(std::move(ctx)), send_(std::move(ctx_.send))
  {
  }

  void FetchPostOp::start()
  {
    if (!parseReq())
      return; // parseReq already sent error

    doWork();
  }

  bool FetchPostOp::parseReq()
  {
    paramValues_.clear();
    paramLengths_.clear();
    paramFormats_.clear();
    return true;
  }

  void FetchPostOp::doWork()
  {
    auto self = shared_from_this();

    ctx_.db->asyncExec(
        sql,
        [self](PGresult *res)
        {
          self->onWorkResult(res);
        });
  }

  void FetchPostOp::onWorkResult(PGresult *res)
  {
    if (!res)
    {
      sendError("Fetch post failed: " + ctx_.db->connErrorMessage());
      return;
    }

    auto status = PQresultStatus(res);

    // Fatal error
    if (status == PGRES_FATAL_ERROR)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Fetch post failed: " + err);
      return;
    }

    // Only PGRES_TUPLES_OK is the real INSERT ... RETURNING row
    if (status != PGRES_TUPLES_OK)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Fetch post failed: " + err);
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
        root["fetchPosts"] = json::array();
        PQclear(res);
        sendSuccess(root.dump());
        return;
      }

      root["fetchPosts"] = json::array();
      for (int row = 0; row < rows; row++)
      {
        LivePostsModel::Post post = LivePostsModel::PG::Posts::fromPGRes(res, cols, row);
        root["fetchPosts"].push_back(post);
      }
      PQclear(res);
      sendSuccess(root.dump());
    }
    catch (const std::exception &e)
    {
      PQclear(res);
      sendError(e.what());
    }
  }

  // --- Local helpers (no DbOpBase) ---
  void FetchPostOp::sendError(const std::string &msg)
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

  void FetchPostOp::sendSuccess(const std::string &body)
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