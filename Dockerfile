# myrpc - 轻量级 RPC 框架
# 基于 Ubuntu 22.04 LTS

FROM ubuntu:22.04

# 设置非交互式安装
ENV DEBIAN_FRONTEND=noninteractive

# 安装编译工具和项目依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libprotobuf-dev \
    protobuf-compiler \
    libboost-all-dev \
    libzookeeper-mt-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /app

# 暴露端口（RPC 服务默认端口）
EXPOSE 8000

# 设置默认命令
CMD ["/bin/bash"]
