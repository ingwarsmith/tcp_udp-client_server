cmake_minimum_required(VERSION 3.5)

project(srvapp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package( Threads )

add_executable(srvapp main.cpp)

target_link_libraries( srvapp ${CMAKE_THREAD_LIBS_INIT} )
