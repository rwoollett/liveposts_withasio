#include "FetchAuthor.h"

#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include <redis_pubsub/publish/Publish.h>
#include <nlohmann/json.hpp>
#include <mtlog/mt_log.hpp>
#include "livepostsmodel/pq.h"

using json = nlohmann::json;
using Rest::RouteHandler;
using Timestamp::parseDate;

namespace Routes::LivePosts
{

  FetchAuthorOp::FetchAuthorOp(RequestContext ctx)
      : ctx_(std::move(ctx)), send_(std::move(ctx_.send))
  {
  }

  void FetchAuthorOp::start()
  {
    if (!parseReq())
      return; // parseReq already sent error

    doWork();
  }

  bool FetchAuthorOp::parseReq()
  {
    params_ = ctx_.session->getReqUrlParameters();

    if (params_["wsId"].empty())
    {
      sendError("Invalid fetch author requires wsId");
      return false;
    }

    mt_logging::logger().log(
        {fmt::format(
             " FetchAuthorOp::parseReq: user auth email {}  user auth id {}  wsId {}",
             ctx_.authUserEmail, ctx_.authUserId, params_["wsId"]),
         mt_logging::LogLevel::Debug,
         true});

    paramStrings_.clear();
    paramStrings_.push_back(ctx_.authUserId);

    // Build paramValues_
    paramValues_.clear();
    for (auto &s : paramStrings_)
      paramValues_.push_back(s.c_str());

    // lengths + formats
    paramLengths_.assign(paramStrings_.size(), 0);
    paramFormats_.assign(paramStrings_.size(), 0);

    return true;
  }

  void FetchAuthorOp::doWork()
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

  void FetchAuthorOp::onWorkResult(PGresult *res)
  {
    if (!res)
    {
      sendError("Fetch author failed: " + ctx_.db->connErrorMessage());
      return;
    }

    auto status = PQresultStatus(res);
    mt_logging::logger().log(
        {fmt::format(
             "FetchAuthorOp::onWorkResult status={} ({}) error: {}",
             static_cast<int>(status),
             PQresStatus(status),
             PQresultErrorMessage(res)),
         mt_logging::LogLevel::Info,
         true});

    // Fatal error
    if (status == PGRES_FATAL_ERROR)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Fetch author failed: " + err);
      return;
    }

    // Only PGRES_TUPLES_OK is the real INSERT ... RETURNING row
    if (status != PGRES_TUPLES_OK)
    {
      std::string err = PQresultErrorMessage(res);
      PQclear(res);
      sendError("Fetch author failed: " + err);
      return;
    }

    // --- Success path: parse the returned rows ---
    try
    {
      json root;
      int rows = PQntuples(res);
      mt_logging::logger().log(
          {fmt::format(
               "FetchAuthorOp::onWorkResult rows={}",
               rows),
           mt_logging::LogLevel::Info,
           true});
      if (rows == 0)
      {
        root["fetchAuthor"] = json::array();

        LivePostsEvents::UserCreateEvent event;
        event.authUserEmail = ctx_.authUserEmail;
        event.wsId = params_["wsId"];
        json jsonEvent = event;
        mt_logging::logger().log(
            {fmt::format(
                 "FetchAuthorOp::onWorkResult usercreateevent made {}",
                 ctx_.authUserEmail),
             mt_logging::LogLevel::Info,
             true});

        ctx_.redis->produce(
            std::string(LivePostsEvents::SubjectNames.at(event.subject)),
            {std::pair<std::string, std::string>{"authUserEmail", ctx_.authUserEmail},
             std::pair<std::string, std::string>{"authUserId", ctx_.authUserId},
             std::pair<std::string, std::string>{"wsId", params_["wsId"]}});

        mt_logging::logger().log(
            {fmt::format(
                 "FetchAuthorOp::onWorkResult sendsuccess {}",
                 ctx_.authUserId),
             mt_logging::LogLevel::Info,
             true});

        PQclear(res);
        sendSuccess(root.dump());
        return;
      }

      root["fetchAuthor"] = json::array();
      int cols = PQnfields(res);
      for (int row = 0; row < rows; row++)
      {
        LivePostsModel::User user = LivePostsModel::PG::Users::fromPGRes(res, cols, row);
        root["fetchAuthor"].push_back(user);
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
  void FetchAuthorOp::sendError(const std::string &msg)
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

  void FetchAuthorOp::sendSuccess(const std::string &body)
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