cmake_minimum_required(VERSION 3.16)

add_library(APIServer
  BroadcastSubscriber.h
  ErrorHandler.cpp
  ErrorHandler.h
  HttpRoute.cpp
  HttpRoute.h
  Parameters.cpp
  Parameters.h
  PQClient.cpp
  PQClient.h
  Response.h
  RestServer.cpp
  RestServer.h
  Route.h
  RouteFactorys.h
  RouteHandler.cpp
  RouteHandler.h
  Session.cpp
  Session.h
  Websocket.cpp
  Websocket.h
  WSClientManager.cpp
  WSClientManager.h
)

target_compile_definitions(APIServer PUBLIC _GLIBCXX_PARALLEL=1)

target_include_directories(
  APIServer PUBLIC
  ${LIBPQ_INCLUDE_DIRS}
  $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(
  APIServer PUBLIC
  Boost::program_options
  Boost::url
  Threads::Threads
  OpenMP::OpenMP_CXX
  OpenSSL::SSL
  OpenSSL::Crypto
  ${LIBPQ_LIBRARIES}
  RedisPublish
  RedisSubscribe
)
