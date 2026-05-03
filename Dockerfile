FROM ubuntu:22.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

# 国内镜像加速
RUN sed -i 's|deb.debian.org|mirrors.aliyun.com|g' /etc/apt/sources.list

# 安装构建依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libssl-dev \
    libcurl4-openssl-dev \
    libmysqlclient-dev \
    libvips-dev \
    libcpprest-dev \
    libhiredis-dev \
    libcurlpp-dev \
    libpugixml-dev \
    libinih-dev \
    nlohmann-json3-dev \
    git \
    wget \
    curl \
    ca-certificates

# 手动创建其他库的 CMake 配置文件（curlpp, inih, nlohmann_json, pugixml）
RUN mkdir -p /usr/lib/cmake/unofficial-curlpp
RUN printf 'include(CMakeFindDependencyMacro)\nadd_library(unofficial::curlpp::curlpp SHARED IMPORTED)\nset_target_properties(unofficial::curlpp::curlpp PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libcurlpp.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n  INTERFACE_LINK_LIBRARIES "CURL::libcurl"\n)\n' > /usr/lib/cmake/unofficial-curlpp/unofficial-curlpp-config.cmake

RUN mkdir -p /usr/lib/cmake/unofficial-inih
RUN printf 'add_library(unofficial::inih::inih SHARED IMPORTED)\nset_target_properties(unofficial::inih::inih PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libinih.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\nadd_library(unofficial::inih::inireader SHARED IMPORTED)\nset_target_properties(unofficial::inih::inireader PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libinih.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n  INTERFACE_LINK_LIBRARIES "unofficial::inih::inih"\n)\n' > /usr/lib/cmake/unofficial-inih/unofficial-inih-config.cmake

RUN mkdir -p /usr/lib/cmake/nlohmann_json
RUN printf 'include(CMakeFindDependencyMacro)\nadd_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)\nset_target_properties(nlohmann_json::nlohmann_json PROPERTIES\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\n' > /usr/lib/cmake/nlohmann_json/nlohmann_jsonConfig.cmake

RUN mkdir -p /usr/lib/cmake/pugixml
RUN printf 'add_library(pugixml::pugixml SHARED IMPORTED)\nset_target_properties(pugixml::pugixml PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libpugixml.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\nadd_library(pugixml::pugixml-static STATIC IMPORTED)\nset_target_properties(pugixml::pugixml-static PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libpugixml.a"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\n' > /usr/lib/cmake/pugixml/pugixmlConfig.cmake

# 生成 CURL::libcurl 补丁文件（供 miniocpp 使用）
RUN printf 'add_library(CURL::libcurl SHARED IMPORTED)\n' \
          'set_target_properties(CURL::libcurl PROPERTIES\n' \
          '  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libcurl.so"\n' \
          '  INTERFACE_INCLUDE_DIRECTORIES "/usr/include")\n' \
          > /tmp/curl_fix.cmake

# 下载 miniocpp（直连 GitHub，失败则走 ghproxy 镜像）
RUN (curl -L -o /tmp/minio-cpp.tar.gz \
        https://github.com/minio/minio-cpp/archive/refs/heads/master.tar.gz \
        --retry 3 --retry-delay 10 --max-time 120 \
     || \
     curl -L -o /tmp/minio-cpp.tar.gz \
        https://ghproxy.com/https://github.com/minio/minio-cpp/archive/refs/heads/master.tar.gz \
        --retry 3 --retry-delay 10 --max-time 120) && \
    tar -xzf /tmp/minio-cpp.tar.gz -C /tmp && \
    mv /tmp/minio-cpp-* /tmp/minio-cpp && \
    cd /tmp/minio-cpp && \
    sed -i '1i\include("/tmp/curl_fix.cmake")' CMakeLists.txt && \
    sed -i 's/doc\.append_child(pugi::node_pcdata)\.set_value(value);/doc.append_child(pugi::node_pcdata).set_value(value.c_str());/' src/utils.cc && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) && \
    make install && \
    rm -rf /tmp/minio-cpp /tmp/minio-cpp.tar.gz /tmp/curl_fix.cmake

# 编译主项目
WORKDIR /app
COPY . .
RUN mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)


# ========= 运行镜像 =========
FROM ubuntu:22.04

RUN sed -i 's|deb.debian.org|mirrors.aliyun.com|g' /etc/apt/sources.list

RUN apt-get update && apt-get install -y \
    libssl3 \
    libcurl4 \
    libmysqlclient21 \
    libvips42 \
    libhiredis0.14 \
    libcpprest2.10 \
    libcurlpp0 \
    libpugixml1v5 \
    libinih1

COPY --from=build /usr/local/lib/libminiocpp.so* /usr/local/lib/
RUN ldconfig

WORKDIR /app
COPY --from=build /app/build/AeroImageHost .
COPY --from=build /app/config/config-docker.json ./config.json
RUN mkdir -p /app/logs

RUN chown -R 1001:1001 /app
USER 1001

EXPOSE 8082
CMD ["./AeroImageHost"]
