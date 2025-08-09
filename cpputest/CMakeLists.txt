cmake_minimum_required(VERSION 3.16)

add_executable(LoadTest
  load.cpp
)

target_link_libraries(
  LoadTest PRIVATE
  Threads::Threads
  -lrestclient-cpp
  -lcurl
  LivePostsModel
)

get_target_property(dirs LivePostsModel INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "Include dirs: ${dirs}")
