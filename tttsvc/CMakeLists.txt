cmake_minimum_required(VERSION 3.16)

add_executable(TicTacToeSvc
  routes/TTTRoutes.h
  routes/TTTRoutes.cpp
  main.cpp
)

target_compile_definitions(TicTacToeSvc PUBLIC _GLIBCXX_PARALLEL=1)

target_include_directories(
  TicTacToeSvc PUBLIC
  ${LIBPQ_INCLUDE_DIRS}
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
  $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(
  TicTacToeSvc PUBLIC
  Boost::program_options
  Boost::url
  Threads::Threads
  APIServer
  TTTModel
  Cookies
)

get_target_property(dirs TTTModel INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "Include dirs: ${dirs}")
