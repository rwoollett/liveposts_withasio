//
// Created by rodney on 17/04/2021.
//

#ifndef SRC_RESTROUTE_H
#define SRC_RESTROUTE_H

#include <string>

namespace Rest
{

  class Route
  {
  public:
    virtual ~Route(){};

    virtual std::string request_id() = 0;

  };

}

#endif /* SRC_RESTROUTE_H */