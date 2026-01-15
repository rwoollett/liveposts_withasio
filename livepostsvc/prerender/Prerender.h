#ifndef PRERENDERER_H
#define PRERENDERER_H

#include <string>
//#include "livepostsmodel/model.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace Prerender 
{
    class AtomicFolderSwapError : public std::runtime_error {
      public:
      using std::runtime_error::runtime_error;
    };

    struct PrerenderPayload { 
      std::string slug; 
      std::string post;
    };

    struct PrerenderResult { 
      bool ok; 
      std::string route; 
      std::string finalDir; // e.g. "/var/www/site/posts/1234" 
      std::string stagingDir; // e.g. "/var/www/site-staging/posts/1234" 
    };

    inline void to_json(json &jsonOut, PrerenderPayload const &value)
    {
      jsonOut["slug"] = value.slug;
      jsonOut["post"] = value.post;
    }

    inline void from_json(json const &jsonIn, PrerenderPayload &value)
    {
      jsonIn.at("slug").get_to(value.slug);
      jsonIn.at("post").get_to(value.post);
    };


    inline void to_json(json &jsonOut, PrerenderResult const &value)
    {
      jsonOut["ok"] = value.ok;
      jsonOut["route"] = value.route;
      jsonOut["outputDir"] = value.finalDir;
      jsonOut["stagingDir"] = value.stagingDir;
    }

    inline void from_json(json const &jsonIn, PrerenderResult &value)
    {
      jsonIn.at("ok").get_to(value.ok);
      jsonIn.at("route").get_to(value.route);
      jsonIn.at("outputDir").get_to(value.finalDir);
      jsonIn.at("stagingDir").get_to(value.stagingDir);
    };
    
    void prerenderPost(const std::string& slug, const std::string& json);

}

#endif // PRERENDERER_H