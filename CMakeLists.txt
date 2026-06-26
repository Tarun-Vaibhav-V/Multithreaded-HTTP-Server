cmake_minimum_required(VERSION 3.16)
project(httpserver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

add_executable(httpserver
  src/main.cpp
  src/thread_pool.cpp
  src/http_parser.cpp
  src/http_response.cpp
  src/server.cpp
  src/router.cpp
)

target_include_directories(httpserver PRIVATE include)
target_link_libraries(httpserver PRIVATE Threads::Threads)
