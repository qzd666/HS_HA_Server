cmake_minimum_required(VERSION 3.15)
project(HS_HA_Server)
set(CMAKE_CXX_STANDARD 11)

include_directories(
    ${PROJECT_SOURCE_DIR}/HTTP
    ${PROJECT_SOURCE_DIR}/Log
    ${PROJECT_SOURCE_DIR}/ConSQL
    ${PROJECT_SOURCE_DIR}/ThreadPool
    ${PROJECT_SOURCE_DIR}/Timer
    )

add_subdirectory(HTTP)
add_subdirectory(Log)
add_subdirectory(ConSQL)
#add_subdirectory(ThreadPool)
add_subdirectory(Timer)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
add_executable(server main.cpp)

find_package(Threads REQUIRED)

#set(MYSQL_LIBRARIES "libmysqlclient.so")
#find_package(MySQL REQUIRED)
#find_library(MYSQL_LIBRARIES NAMES mysqlclient libmysqlclient PATHS /usr/lib)
set(MYSQL_LIBRARIES "/usr/lib/libmysqlclient.so")
set(MYSQL_INCLUDE_DIR "/usr/include/mysql")
include_directories(${MYSQL_INCLUDE_DIR})
#target_include_directories(server PRIVATE ${MYSQL_INCLUDE_DIR})

target_link_libraries(server PRIVATE HTTP Log ConSQL Timer Threads::Threads ${MYSQL_LIBRARIES})
