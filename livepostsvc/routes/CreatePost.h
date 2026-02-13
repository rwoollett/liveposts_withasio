#pragma once

#include "apiserver/DBOpBase.h"
#include "RouteCommon.h"
#include "livepostsmodel/model.h"

using Rest::PQClient;
using Rest::Session;

namespace Routes::LivePosts
{

  enum class WorkStep
  {
    InsertGame,
    InsertPlayers,
    Done
  };

  class CreatePostOp : public Rest::DbOpBase
  {
  public:
    CreatePostOp(
        std::shared_ptr<PQClient> db,
        std::shared_ptr<RedisPublish::Sender> publish,
        std::shared_ptr<Session> sess,
        const http::request<http::string_body> &req,
        SendCall send);

  protected:
    bool parseReq() override;
    void doWork() override;
    bool onWorkResult(PGresult *res) override;
    void handleWork(PGresult *res) override;

  private:
    LivePostsModel::Post post_;
    WorkStep workStep_;
    std::shared_ptr<RedisPublish::Sender> publish_;
    std::shared_ptr<std::string> createPostResult_;
    std::shared_ptr<std::vector<std::string>> paramStrings_;
    std::shared_ptr<std::vector<int>> paramLengths_;
    std::shared_ptr<std::vector<int>> paramFormats_;
  };

}
