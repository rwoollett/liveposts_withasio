#pragma once

#include "apiserver/HttpRoute.h"
#include "CreateAuthor.h"
#include "CreatePost.h"
#include "FetchAuthor.h"
#include "FetchPost.h"
#include "RouteCommon.h"
#include "StagePost.h"
#include <boost/asio/dispatch.hpp>

using Rest::RequestContext;
namespace net = boost::asio; // from <boost/asio.hpp>

namespace Routes
{
  namespace LivePosts
  {
    inline void healthCheck(RequestContext ctx)
    {
      json root = "OK";
      std::string result = root.dump();
      auto &strand = ctx.session->strand(); // <-- bind reference ONCE

      net::dispatch(strand,
                    [ctx = std::move(ctx), result = std::move(result)]() mutable
                    {
                      ctx.send(Rest::Response::success_request(ctx.req, result));
                    });
    };

    inline void createPost(RequestContext ctx)
    {
      auto op = std::make_shared<CreatePostOp>(std::move(ctx));
      op->start();
    }

    inline void fetchPosts(RequestContext ctx)
    {
      auto op = std::make_shared<FetchPostOp>(std::move(ctx));
      op->start();
    }

    inline void stagePost(RequestContext ctx)
    {
      auto op = std::make_shared<StagePostOp>(std::move(ctx));
      op->start();
    }

    inline void createAuthor(RequestContext ctx)
    {
      auto op = std::make_shared<CreateAuthorOp>(std::move(ctx));
      op->start();
    }

    inline void fetchAuthor(RequestContext ctx)
    {
      auto op = std::make_shared<FetchAuthorOp>(std::move(ctx));
      op->start();
    }

    inline void homePage(RequestContext ctx)
    {
      json root;
      root["title"] = "LivePost Service";
      root["description"] = "The Live Post UI interaction "
                            "to test the stack of NetProc.";
      root["navCards"] = json::array();
      root["popularCards"] = json::array();

      json card;
      card["title"] = "Laboratory Collection";
      card["catchPhrase"] = "Time complexity of algorithm is how fast it perform the algorithm. Fast solutions are O(n), slow solutions are O(n2) or greater.";

      root["navCards"].push_back(card);
      root["popularCards"].push_back(card);

      std::string result;
      auto &strand = ctx.session->strand(); // <-- bind reference ONCE
      try
      {
        result = root.dump();
        net::dispatch(strand,
                      [ctx = std::move(ctx), result = std::move(result)]() mutable
                      {
                        ctx.send(Rest::Response::success_request(ctx.req, result));
                      });
        return;
      }
      catch (const std::string &e)
      {
        net::dispatch(strand,
                      [ctx = std::move(ctx), result = std::move(e)]() mutable
                      {
                        ctx.send(Rest::Response::server_error(ctx.req, result));
                      });
        return;
      }
    };

    inline void moderate(RequestContext ctx)
    {
      auto &strand = ctx.session->strand(); // <-- bind reference ONCE

      LivePostsModel::Moderation moderation;
      try
      {
        moderation = json::parse(ctx.req.body());
      }
      catch (...)
      {
        net::dispatch(strand,
                      [ctx = std::move(ctx)]() mutable
                      {
                        ctx.send(Rest::Response::bad_request(ctx.req, "Invalid JSON"));
                      });
        return;
      }

      if (!LivePostsModel::Validate::Moderation(moderation))
      {
        net::dispatch(strand,
                      [ctx = std::move(ctx)]() mutable
                      {
                        ctx.send(Rest::Response::bad_request(ctx.req, "Invalid moderation data"));
                      });
        return;
      }

      try
      {
        std::string result;
        json root;

        LivePostsEvents::ModerateJobEvent event;
        event.id = moderation.id;
        event.userId = moderation.userId;
        event.value = moderation.value;
        root["moderateValue"] = moderation;
        result.assign(root.dump());

        mt_logging::logger().log(
            {fmt::format(
                 " Sending to producer: Subject ({}) message made.",
                 LivePostsEvents::SubjectNames.at(event.subject)),
             mt_logging::LogLevel::Debug,
             true});

        ctx.redis->produce(
            std::string(LivePostsEvents::SubjectNames.at(event.subject)),
            {std::pair<std::string, std::string>{"id", moderation.id},
             std::pair<std::string, std::string>{"userId", moderation.userId},
             std::pair<std::string, std::string>{"value", moderation.value}});

        net::dispatch(strand,
                      [ctx = std::move(ctx), msg = std::move(result)]() mutable
                      {
                        ctx.send(Rest::Response::success_request(ctx.req, msg));
                      });
        return;
      }
      catch (const std::exception &e)
      {
        mt_logging::logger().log(
            {e.what(),
             mt_logging::LogLevel::Debug,
             true});
        net::dispatch(strand,
                      [ctx = std::move(ctx), msg = std::move(e.what())]() mutable
                      {
                        ctx.send(Rest::Response::server_error(ctx.req, "Publish producer error"));
                      });
        return;
      }
      catch (const std::string &e)
      {
        net::dispatch(strand,
                      [ctx = std::move(ctx), msg = std::move(e)]() mutable
                      {
                        ctx.send(Rest::Response::server_error(ctx.req, msg));
                      });
        return;
      }
    }

    // void fetchPosts(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send);
    // void allocatePost(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send);
    // void stagePost(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send);

    // void createUser(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send);
    // void findUserByAuthId(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send);
    // void findUserById(std::shared_ptr<Session> sess, std::shared_ptr<PQClient> dbclient, std::shared_ptr<RedisPublish::Sender> redisPublish, const http::request<http::string_body> &req, SendCall &&send);

  }
}
