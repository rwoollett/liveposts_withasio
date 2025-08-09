#include "ErrorHandler.h"
#include <iostream>

// Report a failure
void Rest::fail(boost::beast::error_code ec, char const *what)
{
  std::cerr << what << ": " << ec.message() << "\n";
}
