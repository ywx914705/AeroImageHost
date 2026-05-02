FROM ubuntu:22.04 AS build

# 设置环境变量
ENV DEBIAN_FRONTEND=noninteractive

# 国内镜像加速（构建阶段）
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
    ca-certificates

# 为 miniocpp 创建 CMake config 文件（Ubuntu apt 包不提供 vcpkg 风格的 config）
RUN mkdir -p /usr/lib/cmake/unofficial-curlpp
RUN printf 'include(CMakeFindDependencyMacro)\nadd_library(unofficial::curlpp::curlpp SHARED IMPORTED)\nset_target_properties(unofficial::curlpp::curlpp PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libcurlpp.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n  INTERFACE_LINK_LIBRARIES "CURL::libcurl"\n)\n' > /usr/lib/cmake/unofficial-curlpp/unofficial-curlpp-config.cmake

RUN mkdir -p /usr/lib/cmake/unofficial-inih
RUN printf 'add_library(unofficial::inih::inih SHARED IMPORTED)\nset_target_properties(unofficial::inih::inih PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libinih.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\nadd_library(unofficial::inih::inireader SHARED IMPORTED)\nset_target_properties(unofficial::inih::inireader PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libinih.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n  INTERFACE_LINK_LIBRARIES "unofficial::inih::inih"\n)\n' > /usr/lib/cmake/unofficial-inih/unofficial-inih-config.cmake

RUN mkdir -p /usr/lib/cmake/nlohmann_json
RUN printf 'include(CMakeFindDependencyMacro)\nadd_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)\nset_target_properties(nlohmann_json::nlohmann_json PROPERTIES\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\n' > /usr/lib/cmake/nlohmann_json/nlohmann_jsonConfig.cmake

RUN mkdir -p /usr/lib/cmake/pugixml
RUN printf 'add_library(pugixml::pugixml SHARED IMPORTED)\nset_target_properties(pugixml::pugixml PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libpugixml.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\nadd_library(pugixml::pugixml-static STATIC IMPORTED)\nset_target_properties(pugixml::pugixml-static PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libpugixml.a"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\n' > /usr/lib/cmake/pugixml/pugixmlConfig.cmake

# 编译安装 miniocpp（MinIO C++ SDK，Ubuntu 源中没有）
RUN git clone --depth 1 https://github.com/minio/minio-cpp.git /tmp/minio-cpp && \
    cd /tmp/minio-cpp && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) && \
    make install && \
    rm -rf /tmp/minio-cpp

# 复制项目代码
WORKDIR /app
COPY . .

# 生成项目文件
RUN mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)


# 最终镜像阶段
FROM ubuntu:22.04

# 国内镜像加速（运行时阶段）
RUN sed -i 's|deb.debian.org|mirrors.aliyun.com|g' /etc/apt/sources.list

# 复制必需的库和运行时
RUN apt-get update && apt-get install -y \
    libssl3 \
    libcurl4 \
    libmysqlclient21 \
    libvips42 \
    libhiredis0.14 \
    libcpprest2.10 \
    libcurlpp1 \
    libpugixml1.13 \
    libinih1

# 复制 miniocpp 运行时库（从构建阶段）
COPY --from=build /usr/local/lib/libminiocpp.so* /usr/local/lib/
RUN ldconfig

# 复制应用和配置
WORKDIR /app
COPY --from=build /app/build/AeroImageHost .
COPY --from=build /app/config/config-docker.json ./config.json

# 创建数据目录
RUN mkdir -p /app/logs

# 设置权限
RUN chown -R 1001:1001 /app
USER 1001

# 配置和启动
EXPOSE 8082
CMD ["./AeroImageHost"]