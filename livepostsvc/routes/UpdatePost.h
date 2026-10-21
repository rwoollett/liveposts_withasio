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
    void onCommit(PGresult *res) override;

  private:
    Rest::Parameters params_;

    LivePostsModel::PostUpdate postUpdate_;
    LivePostsModel::Post updatedPost_;
    UpdatePostWorkStep workStep_;
    std::vector<std::string> paramStrings_;
    std::vector<const char *> paramValues_;
    std::vector<int> paramLengths_;
    std::vector<int> paramFormats_;
    std::string resultBody_;

    static constexpr const char *sql =
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
