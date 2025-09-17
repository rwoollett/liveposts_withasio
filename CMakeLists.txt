cmake_minimum_required(VERSION 3.16)
project(
  LivePostService VERSION 1.0
  DESCRIPTION "Http/Rest Service for Live Posts."
  LANGUAGES CXX C
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_definitions(HAVE_ASIO)
endif()

# set everything up for c++ 20 features
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_DEBUG_LIBS OFF)
set(Boost_USE_RELEASE_LIBS ON)
set(BOOST_ROOT /usr/local)
find_package(Boost 1.86 REQUIRED COMPONENTS program_options url)
find_package(Threads)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED libpq)

if(LIBPQ_FOUND)
  message("-- libpq found via pkg-config")
  message("-- Include dirs: ${LIBPQ_INCLUDE_DIRS}")
  message("-- Libraries: ${LIBPQ_LIBRARIES}")
  message("-- CFLAGS: ${LIBPQ_CFLAGS_OTHER}")
  add_compile_definitions(LIBPQ_FOUND)
else()
  message(FATAL_ERROR "libpq not found via pkg-config")
endif()

if(Boost_FOUND)
  message("-- Boost 1.86 found, using ${Boost_INCLUDE_DIR}")
endif()

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Change 1) LivePostsModel post, tpDate, reactiosns, date optional, reactions optional on create
# Posts and Users, userName string optional userName on post json to Post
FetchContent_Declare(
  LivePostsModel
  GIT_REPOSITORY https://github.com/rwoollett/livepostmodel.git
  GIT_TAG master
)
FetchContent_MakeAvailable(livepostsmodel)
# Cookies
FetchContent_Declare(
  Cookies
  GIT_REPOSITORY https://github.com/rwoollett/cookieParse.git
  GIT_TAG master
)
FetchContent_MakeAvailable(cookies)

FetchContent_Declare(
  jwt-cpp
  GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
  GIT_TAG v0.7.0
)
set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "disable building examples" FORCE)
FetchContent_MakeAvailable(jwt-cpp)

FetchContent_Declare(
  cppcodec
  GIT_REPOSITORY https://github.com/tplgy/cppcodec.git
  GIT_TAG master
)
FetchContent_MakeAvailable(cppcodec)

message(${cookies_SOURCE_DIR})

target_link_libraries(LivePostsModel PUBLIC nlohmann_json::nlohmann_json ${LIBPQ_LIBRARIES})
target_include_directories(LivePostsModel PUBLIC $<BUILD_INTERFACE:${LIBPQ_INCLUDE_DIRS}>)

add_subdirectory(apiserver)
target_link_libraries(APIServer PUBLIC nlohmann_json::nlohmann_json ${LIBPQ_LIBRARIES})

add_subdirectory(redisSubscribe)

add_subdirectory(redisPublish)

add_subdirectory(livepostsvc)

option(BUILD_TESTS "Build tests" OFF)

if(BUILD_TESTS)
  add_subdirectory(cpputest)
endif()

if(NOT BUILD_TESTS)
  include(GNUInstallDirs)
  install(TARGETS LivePostSvc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
