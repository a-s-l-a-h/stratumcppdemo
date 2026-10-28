cmake_minimum_required(VERSION 3.22.1)
project(stratumcppdemo CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(stratum)                      # builds libstratum.so

add_library(stratumcppdemo SHARED native-lib.cpp)
find_library(log-lib log)
target_link_libraries(stratumcppdemo PRIVATE stratum ${log-lib})