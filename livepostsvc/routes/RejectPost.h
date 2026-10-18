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

  class RejectPostOp : public std::enable_shared_from_this<RejectPostOp>
  {
  public:
    RejectPostOp(Rest::RequestContext ctx);

    void start();

  protected:
    bool parseReq();
    void doWork();
    void onWorkResult(PGresult *res);

    void sendError(const std::string &msg);
    void sendSuccess(const std::string &body);

  private:
    Rest::Parameters params_;
    LivePostsModel::Post updatedPostStage_;

    RequestContext ctx_;
    Rest::AnySend send_;

    std::vector<std::string> paramStrings_;
    std::vector<const char *> paramValues_;
    std::vector<int> paramLengths_;
    std::vector<int> paramFormats_;

    static constexpr const char *sql =
        "UPDATE \"Posts\" "
        "SET \"status\"=$1 "
        "WHERE \"id\"=$2 "
        "RETURNING id, \"title\", \"slug\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
        "\"allocated\", \"status\", "
        "(SELECT \"name\" FROM \"Users\" WHERE \"Users\".\"id\" = \"Posts\".\"userId\") AS \"userName\""
        ";";
  };
}
