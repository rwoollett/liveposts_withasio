
#ifndef SRC_RESTROUTEFACTORY_H_
#define SRC_RESTROUTEFACTORY_H_

#include "Route.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include "HttpRoute.h"
#include <iostream>
using Rest::Route;
using Rest::HttpRoute;

namespace Rest::Factory
{

  /**
 * \defgroup	FactoryErrorPoliciesGroup Factory Error Policies
 * \ingroup		FactoryGroup
 * \brief		Manages the "Unknown Type" error in an object factory
 *
 * \class DefaultFactoryError
 * \ingroup		FactoryErrorPoliciesGroup
 * \brief		Default policy that throws an exception
 *
 */

  template <typename IdentifierType, class AbstractProduct>
  struct DefaultFactoryError
  {
    struct Exception : public std::exception
    {
      const char *what() const throw() { return "Unknown Type"; }
    };

    static AbstractProduct *OnUnknownType(IdentifierType)
    {
      throw Exception();
    }
  };

  template <
      class AbstractProduct,
      typename IdentifierType,
      typename ProductCreator = AbstractProduct *(*)(),
      template <typename, class> class FactoryErrorPolicy = DefaultFactoryError>
  class HttpFactory : public FactoryErrorPolicy<IdentifierType, AbstractProduct>
  {
    typedef std::map<IdentifierType, ProductCreator> IdToProductMap;
    IdToProductMap associations_;

  public:
    HttpFactory()
        : associations_()
    {
    }

    ~HttpFactory()
    {
      associations_.erase(associations_.begin(), associations_.end());
    }

    bool Register(const IdentifierType &id, ProductCreator creator)
    {
      return associations_.insert(
                              typename IdToProductMap::value_type(id, creator))
                 .second != 0;
    }

    bool Unregister(const IdentifierType &id)
    {
      return associations_.erase(id) != 0;
    }

    std::vector<IdentifierType> RegisteredIds()
    {
      std::vector<IdentifierType> ids;
      for (typename IdToProductMap::iterator it = associations_.begin();
           it != associations_.end(); ++it)
      {
        ids.push_back(it->first);
      }
      return ids;
    }

    AbstractProduct *CreateObject(const IdentifierType &id)
    {
      typename IdToProductMap::iterator i = associations_.find(id);
      if (i != associations_.end())
        return (i->second)();
      return this->OnUnknownType(id);
    }
  };


  // Specialized HttpRoute factory with registered callbacks with HttpRequest and HttpResponse
  template <
      typename IdentifierType,
      template <typename, class> class FactoryErrorPolicy = DefaultFactoryError>
  class RoutesFactory : public FactoryErrorPolicy<IdentifierType, HttpRoute>
  {

    using IdToProductMap = std::unordered_map<
        IdentifierType,
        std::pair<HttpRoute *(*)(), Rest::HttpRoute::RouteCall>,
        std::function<std::size_t(const IdentifierType &)>,
        std::function<bool(const IdentifierType &, const IdentifierType &)>>;
    IdToProductMap associations_;

  public:
    RoutesFactory(long unsigned int bucket_size,
                std::function<size_t(const IdentifierType &r)> hf,
                std::function<bool(const IdentifierType &r, const IdentifierType &r2)> eq)
        : associations_(bucket_size, hf, eq)
    {
    }

    ~RoutesFactory()
    {
      associations_.erase(associations_.begin(), associations_.end());
      std::cerr << "Route factory association erased: " << associations_.size() << std::endl;
    }

    bool Register(const IdentifierType &id, HttpRoute *(*creator)(), Rest::HttpRoute::RouteCall callback)
    {
      std::pair<HttpRoute *(*)(), Rest::HttpRoute::RouteCall> pair = {creator, callback};
      return associations_.insert(
                              typename IdToProductMap::value_type(id, pair))
                 .second != 0;
    }

    bool Unregister(const IdentifierType &id)
    {
      return associations_.erase(id) != 0;
    }

    std::vector<IdentifierType> RegisteredIds()
    {
      std::vector<IdentifierType> ids;
      for (typename IdToProductMap::iterator it = associations_.begin();
           it != associations_.end(); ++it)
      {
        ids.push_back(it->first);
      }
      return ids;
    }

    HttpRoute *CreateObject(const IdentifierType &id) const
    {
      typename IdToProductMap::const_iterator i = associations_.find(id);
      if (i != associations_.end())
      {
        HttpRoute *route = ((i->second).first)();
        route->register_route_callback((i->second).second);
        return route; 
      };
      return this->OnUnknownType(id);
    }
  };

}
#endif /* SRC_RESTROUTEFACTORY_H_ */