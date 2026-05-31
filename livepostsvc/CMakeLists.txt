cmake_minimum_required(VERSION 3.16)

add_executable(LivePostSvc
  routes/CreateAuthor.h
  routes/CreateAuthor.cpp
  routes/CreatePost.h
  routes/CreatePost.cpp
  routes/FetchPost.h
  routes/FetchPost.cpp
  routes/FetchAuthor.h
  routes/FetchAuthor.cpp
  routes/Routes.h
  routes/StagePost.h
  routes/StagePost.cpp
  prerender/Prerender.h
  prerender/Prerender.cpp
  main.cpp
)

target_compile_definitions(LivePostSvc 
  PUBLIC
   JWT_DISABLE_PICOJSON=1
  PRIVATE 
    GIT_COMMIT="${GIT_COMMIT}"
    GIT_BRANCH="${GIT_BRANCH}"
    GIT_DIRTY="${GIT_DIRTY}"
    BUILD_DATE="${BUILD_DATE}"   
)


target_include_directories(
  LivePostSvc PUBLIC
  ${LIBPQ_INCLUDE_DIRS}
  $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
)

target_link_libraries(
  LivePostSvc PUBLIC
  Boost::program_options
  Boost::url
  RwllttNet::APIServer
  LivePostsModel
)
