FROM ubuntu:22.04 AS build

# 设置环境变量
ENV DEBIAN_FRONTEND=noninteractive

# 安装构建依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libssl-dev \
    libcurl4-openssl-dev \
    libmysqlclient-dev \
    libvips-dev \
    git \
    wget \
    ca-certificates

# 复制项目代码
WORKDIR /app
COPY . .

# 生成项目文件
RUN mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)


# 最终镜像阶段
FROM ubuntu:22.04

# 复制必需的库和运行时
RUN apt-get update && apt-get install -y \
    libssl3 \
    libcurl4 \
    libmysqlclient21 \
    libvips42 \
    libhiredis16

# 复制应用和配置
WORKDIR /app
COPY --from=build /app/build/AeroImageHost .
COPY --from=build /app/config/config.json.example ./config.json

# 创建数据目录
RUN mkdir -p /app/logs

# 设置权限
RUN chown -R 1001:1001 /app
USER 1001

# 配置和启动
EXPOSE 8082
CMD ["./AeroImageHost"]