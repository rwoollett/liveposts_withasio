cmake_minimum_required(VERSION 3.16)

add_executable(LivePostSvc
  routes/Routes.h
  routes/Routes.cpp
  main.cpp
)

target_compile_definitions(LivePostSvc PUBLIC JWT_DISABLE_PICOJSON=1)

target_include_directories(
  LivePostSvc PUBLIC
  ${LIBPQ_INCLUDE_DIRS}
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
  $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(
  LivePostSvc PUBLIC
  Boost::program_options
  Boost::url
#  Threads::Threads
  APIServer
  LivePostsModel
  Cookies
  jwt-cpp::jwt-cpp
)

get_target_property(dirs LivePostsModel INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "Include dirs: ${dirs}")
