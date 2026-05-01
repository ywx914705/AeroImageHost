cmake_minimum_required(VERSION 3.10)
project(AeroImageHost)

set(CMAKE_CXX_STANDARD 17)

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
    "storage/*.cc"
    "image/*.cc"
    "utils/*.cc"
    "src/*.cc"
    "main.cc"
)

# 添加新创建的文件
list(APPEND SOURCES
    "storage/EmailVerificationDAO.cpp"
    "utils/EmailSender.cpp"
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME}
    crypt
    ${VIPS_LIBRARIES}
    OpenSSL::SSL
    Threads::Threads
    ${MYSQL_LIBRARY}
    hiredis
    pthread
    cpprest
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
