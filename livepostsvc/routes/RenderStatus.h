#pragma once

#include "RouteCommon.h"
#include "livepostsmodel/model.h"
#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include "apiserver/HttpRoute.h"

using Rest::RequestContext;
using Rest::Response::bad_request;
using Rest::Response::success_request;

namespace Routes::LivePosts
{

  class RenderStatusOp : public std::enable_shared_from_this<RenderStatusOp>
  {
  public:
    RenderStatusOp(Rest::RequestContext ctx);

    void start();

  protected:
    bool parseReq();
    void doWork();
    void onWorkResult(PGresult *res);

    void sendError(const std::string &msg);
    void sendSuccess(const std::string &body);

  private:
    std::chrono::_V2::system_clock::time_point t0_;

    Rest::Parameters params_;
    LivePostsModel::Post renderStatusPost_;

    RequestContext ctx_;
    Rest::AnySend send_;

    std::vector<std::string> paramStrings_;
    std::vector<const char *> paramValues_;
    std::vector<int> paramLengths_;
    std::vector<int> paramFormats_;

    static constexpr const char *sql =
        "UPDATE \"Posts\" "
        "SET \"renderStatus\"=$2 WHERE \"id\"=$1 "
        "RETURNING id, \"title\", \"slug\", \"content\", \"draftTitle\", \"draftContent\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
        "\"allocated\", \"status\", \"renderStatus\", "
        "(SELECT \"name\" FROM \"Users\" WHERE \"Users\".\"id\" = \"Posts\".\"userId\") AS \"userName\""
        ";";
  };
}
