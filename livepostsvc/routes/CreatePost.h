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
    CreatePostOp(RequestContext ctx);

  protected:
    bool parseReq() override;
    void doWork() override;
    bool onWorkResult(PGresult *res) override;
    void handleWork(PGresult *res) override;
    void onCommit(PGresult *res) override;

  private:
    LivePostsModel::PostCreate postCreate_; // TODO needs a PostCreate structure so to send websocket user, which is not to do with Post
    LivePostsModel::Post newPost_;
    WorkStep workStep_;
    std::vector<std::string> paramStrings_;
    std::vector<const char *> paramValues_;
    std::vector<int> paramLengths_;
    std::vector<int> paramFormats_;
    std::string resultBody_;

    static constexpr const char *CREATE_BOARD_INIT = "0,0,0,0,0,0,0,0,0";

    static constexpr const char *createPostSql =
        "INSERT INTO \"Posts\" "
        "(\"title\", \"content\", \"userId\", \"date\", \"status\") VALUES ($1, $2, $3, NOW(), $4) "
        "RETURNING id, \"title\", \"slug\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
        "\"allocated\", \"status\", "
        "(SELECT \"name\" FROM \"Users\" WHERE \"Users\".\"id\" = \"Posts\".\"userId\") AS \"userName\""
        ";";
  };

}
