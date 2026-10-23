#pragma once

#include "apiserver/DBOpBase.h"
#include "RouteCommon.h"
#include "livepostsmodel/model.h"

using Rest::PQClient;
using Rest::Session;

namespace Routes::LivePosts
{

  enum class UpdatePostWorkStep
  {
    AuthCheck,
    Update,
    Done
  };

  class UpdatePostOp : public Rest::DbOpBase
  {
  public:
    UpdatePostOp(RequestContext ctx);

  protected:
    bool parseReq() override;
    void doWork() override;
    bool onWorkResult(PGresult *res) override;
    void handleWork(PGresult *res) override;
    bool onAuthCheck(PGresult *res);
    bool onUpdate(PGresult *res);

    void onCommit(PGresult *res) override;

  private:
    void workAdvance()
    {
      if (workStep_ == UpdatePostWorkStep::Done)
        return;

      switch (workStep_)
      {
      case UpdatePostWorkStep::AuthCheck:
        workStep_ = UpdatePostWorkStep::Update;
        break;
      case UpdatePostWorkStep::Update:
        workStep_ = UpdatePostWorkStep::Done;
      }
    }

  private:
    Rest::Parameters params_;

    LivePostsModel::PostUpdate postUpdate_;
    LivePostsModel::Post updatedPost_;
    UpdatePostWorkStep workStep_;

    std::vector<std::string> paramStringsAuthCheck_;
    std::vector<const char *> paramValuesAuthCheck_;
    std::vector<int> paramLengthsAuthCheck_;
    std::vector<int> paramFormatsAuthCheck_;

    std::vector<std::string> paramStrings_;
    std::vector<const char *> paramValues_;
    std::vector<int> paramLengths_;
    std::vector<int> paramFormats_;

    std::string resultBody_;

    static constexpr const char *selectAuthSql = "SELECT \"id\" FROM \"Posts\" "
                                         "LEFT JOIN \"Users\" ON \"Posts\".\"userId\" = \"Users\".\"id\" "
                                         "WHERE \"Users\".\"authUserId\" = $1 "
                                         "AND   \"Posts\".\"id\" = $2 "
                                         ";";

    static constexpr const char *updatePostSql =
        "UPDATE \"Posts\" "
        "SET \"draftTitle\"=$1, "
        " \"draftContent\"=$2, "
        " \"status\"=$3, "
        " \"date\"=NOW() "
        "WHERE \"id\"=$4 "
        "RETURNING id, \"title\", \"slug\", \"content\", \"draftTitle\", \"draftContent\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
        "\"allocated\", \"status\", "
        "(SELECT \"name\" FROM \"Users\" WHERE \"Users\".\"id\" = \"Posts\".\"userId\") AS \"userName\""
        ";";
  };

}
