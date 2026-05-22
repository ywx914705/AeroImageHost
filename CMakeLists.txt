cmake_minimum_required(VERSION 3.10)
project(AeroImageHost)

set(CMAKE_CXX_STANDARD 17)

list(APPEND CMAKE_PREFIX_PATH "/usr/local")
if(DEFINED ENV{HOME})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local")
endif()

find_package(Drogon CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPS REQUIRED vips)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# 替代性的MySQL查找方法
find_path(MYSQL_INCLUDE_DIR mysql.h
    PATHS /usr/include /usr/local/include
    PATH_SUFFIXES mysql
)

find_library(MYSQL_LIBRARY
    NAMES mysqlclient mysql
    PATHS /usr/lib /usr/local/lib
)

if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR "MySQL headers or libraries not found. Please install MySQL development package.")
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/http
    ${CMAKE_SOURCE_DIR}/storage
    ${CMAKE_SOURCE_DIR}/config
    ${CMAKE_SOURCE_DIR}/image
    ${CMAKE_SOURCE_DIR}/utils
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/monitor
    ${MYSQL_INCLUDE_DIR}
    ${VIPS_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    /usr/local/include
    /usr/local/include/miniocpp
)

file(GLOB_RECURSE SOURCES
    "config/*.cc"
    "http/*.cc"
    "http/Controllers/*.cc"
    "http/Filters/*.cc"
    "storage/*.cc"
    "image/*.cc"
    "utils/*.cc"
    "src/*.cc"
    "monitor/*.cc"
    "main.cc"
)

# .cc 文件由 GLOB_RECURSE 自动发现，无需手动追加

add_executable(${PROJECT_NAME} ${SOURCES})

find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED cairo)
include_directories(${CAIRO_INCLUDE_DIRS})
link_directories(${CAIRO_LIBRARY_DIRS})

target_link_libraries(${PROJECT_NAME}
    Drogon::Drogon
    crypt
    ${VIPS_LIBRARIES}
    ${CAIRO_LIBRARIES}
    fontconfig
    OpenSSL::SSL
    Threads::Threads
    ${MYSQL_LIBRARY}
    hiredis
    pthread
    ssl
    crypto
    miniocpp
    curlpp
    pugixml
    INIReader
    inih
    curl
    z
)

option(BUILD_TESTING "Build and register unit tests" ON)
include(CTest)
if(BUILD_TESTING)
    add_executable(test_metrics_collector
        tests/test_metrics_collector.cc
        monitor/MetricsCollector.cc
        src/ConnectionPool.cc
        src/RedisClient.cc
        src/Log.cc
        utils/Utils.cc)
    target_include_directories(test_metrics_collector PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/monitor
        ${MYSQL_INCLUDE_DIR}
        ${VIPS_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS})
    target_link_libraries(test_metrics_collector
        Drogon::Drogon
        ${MYSQL_LIBRARY}
        hiredis
        pthread
        ssl
        crypto
        curl
        z)
    add_test(NAME test_metrics_collector COMMAND test_metrics_collector)
endif()
