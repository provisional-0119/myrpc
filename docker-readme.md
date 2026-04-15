# myrpc Docker 部署指南

## 前置要求

- Docker >= 20.10
- Docker Compose >= 2.0

## 快速启动

### 方式一：使用 docker-compose（推荐）

```bash
# 构建镜像并启动所有服务
docker-compose up -d --build

# 查看日志
docker-compose logs -f

# 停止所有服务
docker-compose down
```

### 方式二：仅启动 ZooKeeper

```bash
docker-compose up -d zookeeper
```

### 方式三：仅构建镜像

```bash
docker-compose build
```

## 服务说明

| 服务 | 端口 | 说明 |
|------|------|------|
| zookeeper | 2181 | 服务注册中心 |
| rpc-server | 8000 | RPC 服务端 |
| rpc-client | - | RPC 客户端（调用服务后退出） |

## 自定义配置

修改 `bin/test.conf` 文件来更改配置：

```conf
rpcserverip=0.0.0.0      # 服务监听地址
rpcserverport=8000         # 服务端口
zookeeperip=zookeeper      # ZooKeeper 地址（Docker 网络内）
zookeeperport=2181         # ZooKeeper 端口
```

## 手动运行（不使用 docker-compose）

```bash
# 构建镜像
docker build -t myrpc .

# 启动 ZooKeeper
docker run -d --name myrpc-zookeeper -p 2181:2181 zookeeper:3.7

# 运行服务端
docker run -d --name myrpc-server \
  --link myrpc-zookeeper:zookeeper \
  -p 8000:8000 \
  -v $(pwd)/bin/test.conf:/app/bin/test.conf:ro \
  myrpc /app/bin/provider -i /app/bin/test.conf

# 运行客户端
docker run --name myrpc-client \
  --link myrpc-server \
  -v $(pwd)/bin/test.conf:/app/bin/test.conf:ro \
  myrpc /app/bin/consumer -i /app/bin/test.conf
```

## 清理

```bash
# 停止并删除容器
docker-compose down

# 删除镜像
docker rmi myrpc-mprpc

# 删除数据卷（如果需要）
docker volume prune
```

## 调试

```bash
# 进入容器
docker exec -it myrpc-server /bin/bash

# 查看进程
docker exec myrpc-server ps aux

# 查看网络
docker exec myrpc-server netstat -tlnp
```
