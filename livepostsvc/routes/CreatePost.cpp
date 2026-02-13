#include "CreatePost.h"
#include "apiserver/Session.h"
#include "apiserver/PQClient.h"
#include "apiserver/Response.h"
#include "apiserver/RouteHandler.h"
#include <pubsub/publish/Publish.h>
#include <nlohmann/json.hpp>
#include "livepostsmodel/pq.h"

#include <memory>
#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>

using json = nlohmann::json;
using LivePostsModel::parseDate;
using Rest::makeParamVectors;
using Rest::RouteHandler;
using Rest::safePgErrorForJson;

namespace Routes::LivePosts
{
  const char *CREATE_BOARD_INIT = "0,0,0,0,0,0,0,0,0";

  const char *createPostSql =
      "INSERT INTO \"Posts\" "
      "(\"title\", \"content\", \"userId\", \"date\") VALUES ($1, $2, $3, NOW()) "
      "RETURNING id, \"title\", \"slug\", \"content\", \"userId\", \"date\", \"thumbsUp\", \"hooray\", \"heart\", \"rocket\", \"eyes\", "
      "\"allocated\", \"live\", "
      "(SELECT \"name\" FROM \"Users\" WHERE \"Users\".\"id\" = \"Posts\".\"userId\") AS \"userName\""
      ";";

  CreatePostOp::CreatePostOp(
      std::shared_ptr<PQClient> db,
      std::shared_ptr<RedisPublish::Sender> publish,
      std::shared_ptr<Session> sess,
      const http::request<http::string_body> &req,
      SendCall send)
      : DbOpBase(db, sess, req, send),
        workStep_(WorkStep::Done),
        publish_(publish),
        createPostResult_(std::make_shared<std::string>())
  {
  }

  bool CreatePostOp::parseReq()
  {
    try
    {
      post_ = json::parse(req_.body());
    }
    catch (...)
    {
      sendError("Invalid JSON");
      return false;
    }

    if (!LivePostsModel::Validate::Posts(post_))
    {
      sendError("Invalid Game data");
      return false;
    }

    std::vector<std::string> postVals = {
        post_.title,
        post_.content,
        std::to_string(post_.userId)};

    std::tie(paramStrings_, paramLengths_, paramFormats_) =
        makeParamVectors(postVals);

    return true;
  }

  void CreatePostOp::doWork()
  {
    auto self = shared_from_this();
    db_->asyncParamQuery(
        createPostSql,
        *paramStrings_, *paramLengths_, *paramFormats_,
        [self](PGresult *res)
        { self->handleWork(res); });
  }

  void CreatePostOp::handleWork(PGresult *res)
  {
    if (!onWorkResult(res))
    {
      state_ = State::Rollback;
      return advance();
    }
    if (workStep_ != WorkStep::Done)
    {
      doWork();
    }
    state_ = State::Commit;
    advance();
  }

  bool CreatePostOp::onWorkResult(PGresult *res)
  {
    std::string err = safePgErrorForJson(db_->connErrorMessage(), res);

    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
    {
      *createPostResult_ = "Create Post failed " + err;
      sendError(*createPostResult_);
      return false;
    }

    try
    {
      int cols = PQnfields(res);
      LivePostsModel::Post newPost = LivePostsModel::PG::Posts::fromPGRes(res, cols, 0);

      LivePostsEvents::PostCreateEvent event;
      event.id = newPost.id;
      event.userId = newPost.userId;
      event.title = newPost.title;
      event.userName = newPost.userName;
      event.live = newPost.live;
      event.allocated = newPost.allocated;
      json jsonEvent = event;

      Routes::LivePosts::cntLivePostMessage++;
      std::cout << "  Sending to publish: Subject (" << LivePostsEvents::SubjectNames.at(event.subject) << ")"
                << " " << Routes::LivePosts::cntLivePostMessage << " LivePost messages made. "
                << std::endl;

      publish_->Send(
          std::string(LivePostsEvents::SubjectNames.at(event.subject)),
          jsonEvent.dump());

      json root;
      root["createPost"] = newPost;
      *createPostResult_ = root.dump();
    }
    catch (const std::exception &e)
    {
      sendError(e.what());
      return false;
    }

    sendSuccess(*createPostResult_);
    return true;
  }

}