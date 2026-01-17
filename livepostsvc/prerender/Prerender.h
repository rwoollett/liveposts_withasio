#ifndef PRERENDERER_H
#define PRERENDERER_H

#include <string>
#include <variant>
#include <iostream>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace Prerender 
{
    struct PipeError
    {
      std::string message;
    };
    template <typename Data>
    using PipeResponse = std::variant<Data, PipeError>;

    template <typename InstanceData>
    static PipeResponse<InstanceData> response(json const &json)
    {
      auto error = json.find("error");
      if (error != json.end())
      {
        PipeError apiError;
        apiError.message = *error;
        return apiError;
      }
      else
      {
        auto const &data = json;
        return InstanceData(data);
      }
    };

    class AtomicFolderSwapError : public std::runtime_error {
      public:
      using std::runtime_error::runtime_error;
    };

    struct PrerenderPayload { 
      std::string post;
    };

    struct PrerenderResult { 
      bool ok; 
      std::string slug;
      std::string route; 
      std::string finalDir; // e.g. "/var/www/site/posts/1234" 
      std::string stagingDir; // e.g. "/var/www/site-staging/posts/1234" 
      std::string error;
    };

    inline void to_json(json &jsonOut, PrerenderPayload const &value)
    {
      jsonOut["post"] = value.post;
    }

    inline void from_json(json const &jsonIn, PrerenderPayload &value)
    {
      jsonIn.at("post").get_to(value.post);
    };


    inline void to_json(json &jsonOut, PrerenderResult const &value)
    {
      jsonOut["ok"] = value.ok;
      jsonOut["slug"] = value.slug;
      jsonOut["route"] = value.route;
      jsonOut["outputDir"] = value.finalDir;
      jsonOut["stagingDir"] = value.stagingDir;
      jsonOut["error"] = value.error;
    }

    inline void from_json(json const &jsonIn, PrerenderResult &value)
    {
      jsonIn.at("ok").get_to(value.ok);
      jsonIn.at("slug").get_to(value.slug);
      jsonIn.at("route").get_to(value.route);
      jsonIn.at("outputDir").get_to(value.finalDir);
      jsonIn.at("stagingDir").get_to(value.stagingDir);
      if (jsonIn.contains("error"))
      {
        jsonIn.at("error").get_to(value.error);
      }
    };
    
    void prerenderPost(const std::string& json);

}

#endif // PRERENDERER_H