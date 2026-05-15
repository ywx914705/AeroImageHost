FROM ubuntu:22.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's|deb.debian.org|mirrors.aliyun.com|g' /etc/apt/sources.list

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libssl-dev \
    libcurl4-openssl-dev \
    libmysqlclient-dev \
    libvips-dev \
    libhiredis-dev \
    libcurlpp-dev \
    libpugixml-dev \
    libinih-dev \
    nlohmann-json3-dev \
    zlib1g-dev \
    libjsoncpp-dev \
    uuid-dev \
    libbrotli-dev \
    git \
    wget \
    curl \
    ca-certificates

RUN mkdir -p /usr/lib/cmake/unofficial-curlpp
RUN printf 'include(CMakeFindDependencyMacro)\nadd_library(unofficial::curlpp::curlpp SHARED IMPORTED)\nset_target_properties(unofficial::curlpp::curlpp PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libcurlpp.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n  INTERFACE_LINK_LIBRARIES "CURL::libcurl"\n)\n' > /usr/lib/cmake/unofficial-curlpp/unofficial-curlpp-config.cmake

RUN mkdir -p /usr/lib/cmake/unofficial-inih
RUN printf 'add_library(unofficial::inih::inih SHARED IMPORTED)\nset_target_properties(unofficial::inih::inih PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libinih.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\nadd_library(unofficial::inih::inireader SHARED IMPORTED)\nset_target_properties(unofficial::inih::inireader PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libinih.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n  INTERFACE_LINK_LIBRARIES "unofficial::inih::inih"\n)\n' > /usr/lib/cmake/unofficial-inih/unofficial-inih-config.cmake

RUN mkdir -p /usr/lib/cmake/nlohmann_json
RUN printf 'include(CMakeFindDependencyMacro)\nadd_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)\nset_target_properties(nlohmann_json::nlohmann_json PROPERTIES\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\n' > /usr/lib/cmake/nlohmann_json/nlohmann_jsonConfig.cmake

RUN mkdir -p /usr/lib/cmake/pugixml
RUN printf 'add_library(pugixml::pugixml SHARED IMPORTED)\nset_target_properties(pugixml::pugixml PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libpugixml.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\nadd_library(pugixml::pugixml-static STATIC IMPORTED)\nset_target_properties(pugixml::pugixml-static PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libpugixml.a"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\n' > /usr/lib/cmake/pugixml/pugixmlConfig.cmake

RUN git clone --depth 1 --branch v1.9.4 https://github.com/drogonframework/drogon.git /tmp/drogon && \
    cd /tmp/drogon && \
    git submodule update --init --recursive && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF .. && \
    make -j"$(nproc)" && make install && \
    cd / && rm -rf /tmp/drogon && ldconfig

RUN printf 'add_library(CURL::libcurl SHARED IMPORTED)\n' \
          'set_target_properties(CURL::libcurl PROPERTIES\n' \
          '  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libcurl.so"\n' \
          '  INTERFACE_INCLUDE_DIRECTORIES "/usr/include")\n' \
          > /tmp/curl_fix.cmake

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

WORKDIR /app
COPY . .
RUN mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON .. && \
    make -j$(nproc) && \
    ctest --output-on-failure


FROM ubuntu:22.04

LABEL maintainer="AeroImageHost Team"
LABEL description="High-performance image hosting system"
LABEL version="1.0.0"

ENV TZ=Asia/Shanghai

RUN sed -i 's|deb.debian.org|mirrors.aliyun.com|g' /etc/apt/sources.list

RUN apt-get update && apt-get install -y \
    libssl3 \
    libcurl4 \
    libmysqlclient21 \
    libvips42 \
    libhiredis0.14 \
    libcurlpp0 \
    libpugixml1v5 \
    libinih1 \
    libjsoncpp25 \
    zlib1g \
    libuuid1 \
    libbrotli1 \
    curl \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /usr/local/lib/libminiocpp.so* /usr/local/lib/
COPY --from=build /usr/local/lib/libdrogon.so* /usr/local/lib/
COPY --from=build /usr/local/lib/libtrantor.so* /usr/local/lib/
COPY --from=build /usr/lib/x86_64-linux-gnu/libINIReader.so* /usr/lib/x86_64-linux-gnu/
RUN ldconfig

WORKDIR /app
COPY --from=build /app/build/AeroImageHost .
COPY --from=build /app/config/config-docker.example.json ./config.json
COPY www/ ./www/
RUN mkdir -p /app/logs

RUN chown -R 1001:1001 /app
USER 1001

EXPOSE 8082

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8082/api/health || exit 1

CMD ["./AeroImageHost"]
