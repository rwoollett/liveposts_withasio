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

  class CreateAuthorOp : public std::enable_shared_from_this<CreateAuthorOp>
  {
  public:
    CreateAuthorOp(Rest::RequestContext ctx);

    void start();

  protected:
    bool parseReq();
    void doWork();
    void onWorkResult(PGresult *res);

    void sendError(const std::string &msg);
    void sendSuccess(const std::string &body);

  private:
    LivePostsModel::User userInput_;

    RequestContext ctx_;
    Rest::AnySend send_;

    std::vector<std::string> paramStrings_;
    std::vector<const char *> paramValues_;
    std::vector<int> paramLengths_;
    std::vector<int> paramFormats_;

    static constexpr const char *sql =
          "INSERT INTO \"Users\" "
          "(\"authId\", \"name\") VALUES ($1, $2) "
          "RETURNING id, \"authId\", \"name\""
          ";";
  };
}
