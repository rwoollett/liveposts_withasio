#ifndef SRC_APP_REST_ERROR_HANDLER_H_
#define SRC_APP_REST_ERROR_HANDLER_H_

#include <boost/beast/core/error.hpp>


#ifdef NDEBUG
#define D(x)
#else
#define D(x) x
#endif

namespace Rest
{
  // Report a failure
  void fail(boost::beast::error_code ec, char const *what);
}

#endif /* SRC_APP_REST_ERROR_HANDLER_H_ */
