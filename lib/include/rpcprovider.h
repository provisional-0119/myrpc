#pragma once
#include "google/protobuf/service.h"
#include <boost/asio.hpp>
#include <string>
#include <functional>
#include <google/protobuf/descriptor.h>
#include <unordered_map>
#include <memory>
#include <thread>
#include <vector>

// 前置声明
class RpcConnection;

// 框架提供的专门发布rpc服务的网络对象类
class RpcProvider
{
public:
    RpcProvider();
    
    // 这里是框架提供给外部使用的，可以发布rpc方法的函数接口
    void NotifyService(google::protobuf::Service *service);

    // 启动rpc服务节点，开始提供rpc远程网络调用服务
    void Run();

    // 内部处理方法（开放给 RpcConnection 使用）
    void HandleRpcRequest(std::shared_ptr<RpcConnection> conn, 
                          const std::string& service_name, 
                          const std::string& method_name, 
                          const std::string& args_str);

private:
    // service服务类型信息
    struct ServiceInfo
    {
        google::protobuf::Service *m_service; // 保存服务对象
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor*> m_methodMap; // 保存服务方法
    };
    // 存储注册成功的服务对象和其服务方法的所有信息
    std::unordered_map<std::string, ServiceInfo> m_serviceMap;

    // Boost.Asio 核心组件
    boost::asio::io_context m_ioContext;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> m_acceptor;

    void DoAccept();
    void SendRpcResponse(std::shared_ptr<RpcConnection> conn, google::protobuf::Message* response);
};