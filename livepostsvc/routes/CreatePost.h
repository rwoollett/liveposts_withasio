#pragma once

#include "apiserver/DBOpBase.h"
#include "RouteCommon.h"
#include "livepostsmodel/model.h"

using Rest::PQClient;
using Rest::Session;

namespace Routes::LivePosts
{

  enum class CreatePostWorkStep
  {
    AuthFind,
    Create,
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
    bool onAuthFind(PGresult *res);
    bool onCreate(PGresult *res);
    void onCommit(PGresult *res) override;

  private:
    void workAdvance()
    {
      if (workStep_ == CreatePostWorkStep::Done)
        return;

      switch (workStep_)
      {
      case CreatePostWorkStep::AuthFind:
        workStep_ = CreatePostWorkStep::Create;
        break;
      case CreatePostWorkStep::Create:
        workStep_ = CreatePostWorkStep::Done;
      }
    }

  private:
    LivePostsModel::PostCreate postCreate_;
    LivePostsModel::User postUser_;
    LivePostsModel::Post newPost_;
    CreatePostWorkStep workStep_;

    std::vector<std::string> paramStringsAuthFind_;
    std::vector<const char *> paramValuesAuthFind_;
    std::vector<int> paramLengthsAuthFind_;
    std::vector<int> paramFormatsAuthFind_;

    std::vector<std::string> paramStrings_;
    std::vector<const char *> paramValues_;
    std::vector<int> paramLengths_;
    std::vector<int> paramFormats_;
    std::string resultBody_;

    static constexpr const char *selectAuthSql = "SELECT "
                                                 "id, \"authUserId\", \"authUserEmail\", \"name\" "
                                                 "FROM \"Users\" "
                                                 "WHERE \"authUserId\" = $1 "
                                                 ";";

    static constexpr const char *createPostSql =
        "INSERT INTO \"Posts\" "
        "(\"title\", \"content\", \"userId\", \"date\", \"status\") VALUES ($1, $2, $3, NOW(), $4) "
        "RETURNING id, \"title\", \"slug\", \"content\", \"draftTitle\", \"draftContent\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
        "\"allocated\", \"status\", "
        "(SELECT \"name\" FROM \"Users\" WHERE \"Users\".\"id\" = \"Posts\".\"userId\") AS \"userName\""
        ";";
  };

}
