cmake_minimum_required(VERSION 3.15)
project (HTTP)
set(CMAKE_CXX_STANDARD 11)

aux_source_directory(./ SRC)
add_library(HTTP ${SRC})
