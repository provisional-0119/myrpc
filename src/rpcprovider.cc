#include "rpcprovider.h"
#include "mprpcapplication.h"
#include "rpcheader.pb.h"
#include "logger.h"
#include "zookeeperutil.h"
#include <iostream>

using boost::asio::ip::tcp;

// =========================================================================
// RpcConnection 类负责管理单个TCP连接的生命周期和异步读写（解决粘包/半包）
// =========================================================================
class RpcConnection : public std::enable_shared_from_this<RpcConnection> {
public:
    RpcConnection(tcp::socket socket, RpcProvider* provider)
        : m_socket(std::move(socket)), m_provider(provider) {}

    void Start() {
        ReadHeaderSize();
    }

    void SendResponseAndClose(const std::string& response_str) {
        auto self(shared_from_this());
        boost::asio::async_write(m_socket, boost::asio::buffer(response_str),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                // 模拟http的短链接服务，由rpcprovider主动断开连接
                boost::system::error_code ignore_ec;
                m_socket.shutdown(tcp::socket::shutdown_both, ignore_ec);
                m_socket.close(ignore_ec);
            });
    }

private:
    void ReadHeaderSize() {
        auto self(shared_from_this());
        // 1. 读取 4 字节的 header_size
        boost::asio::async_read(m_socket, boost::asio::buffer(&m_header_size, 4),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    ReadHeader();
                }
            });
    }

    void ReadHeader() {
        auto self(shared_from_this());
        m_header_buf.resize(m_header_size);
        // 2. 准确读取 header_size 大小的数据流
        boost::asio::async_read(m_socket, boost::asio::buffer(m_header_buf),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    mprpc::RpcHeader rpcHeader;
                    if (rpcHeader.ParseFromArray(m_header_buf.data(), m_header_size)) {
                        m_service_name = rpcHeader.service_name();
                        m_method_name = rpcHeader.method_name();
                        m_args_size = rpcHeader.args_size();
                        ReadArgs();
                    } else {
                        std::cout << "rpc_header parse error!" << std::endl;
                    }
                }
            });
    }

    void ReadArgs() {
        auto self(shared_from_this());
        m_args_buf.resize(m_args_size);
        // 3. 准确读取 args_size 大小的参数数据流
        boost::asio::async_read(m_socket, boost::asio::buffer(m_args_buf),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    std::string args_str(m_args_buf.begin(), m_args_buf.end());
                    // 接收完整后，交由 Provider 分发处理
                    m_provider->HandleRpcRequest(self, m_service_name, m_method_name, args_str);
                }
            });
    }

    tcp::socket m_socket;
    RpcProvider* m_provider;

    uint32_t m_header_size;
    std::vector<char> m_header_buf;
    
    std::string m_service_name;
    std::string m_method_name;
    uint32_t m_args_size;
    std::vector<char> m_args_buf;
};

// =========================================================================
// RpcProvider 类实现
// =========================================================================

RpcProvider::RpcProvider() {}

void RpcProvider::NotifyService(google::protobuf::Service *service)
{
    ServiceInfo service_info;
    const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();
    std::string service_name = pserviceDesc->name();
    int methodCnt = pserviceDesc->method_count();

    LOG_INFO("service_name:%s", service_name.c_str());

    for (int i = 0; i < methodCnt; ++i)
    {
        const google::protobuf::MethodDescriptor* pmethodDesc = pserviceDesc->method(i);
        std::string method_name = pmethodDesc->name();
        service_info.m_methodMap.insert({method_name, pmethodDesc});
        LOG_INFO("method_name:%s", method_name.c_str());
    }
    service_info.m_service = service;
    m_serviceMap.insert({service_name, service_info});
}

void RpcProvider::Run()
{
    std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());

    // 初始化 Acceptor
    tcp::endpoint endpoint(boost::asio::ip::make_address(ip), port);
    m_acceptor = std::make_unique<tcp::acceptor>(m_ioContext, endpoint);

    // 注册到 ZooKeeper (原逻辑保留)
    ZkClient zkCli;
    zkCli.Start();
    for (auto &sp : m_serviceMap) 
    {
        std::string service_path = "/" + sp.first;
        zkCli.Create(service_path.c_str(), nullptr, 0);
        for (auto &mp : sp.second.m_methodMap)
        {
            std::string method_path = service_path + "/" + mp.first;
            char method_path_data[128] = {0};
            sprintf(method_path_data, "%s:%d", ip.c_str(), port);
            zkCli.Create(method_path.c_str(), method_path_data, strlen(method_path_data), ZOO_EPHEMERAL);
        }
    }

    std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;

    // 启动异步接收连接
    DoAccept();

    // 运行 io_context，相当于 muduo 的 setThreadNum 和 m_eventLoop.loop()
    // 开启4个线程去跑 io_context 任务队列
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([this]() {
            m_ioContext.run();
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

void RpcProvider::DoAccept()
{
    m_acceptor->async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                // 创建一个Connection对象来处理这个连接的完整生命周期
                std::make_shared<RpcConnection>(std::move(socket), this)->Start();
            }
            // 继续监听下一个连接
            DoAccept();
        });
}

void RpcProvider::HandleRpcRequest(std::shared_ptr<RpcConnection> conn, 
                                   const std::string& service_name, 
                                   const std::string& method_name, 
                                   const std::string& args_str)
{
    auto it = m_serviceMap.find(service_name);
    if (it == m_serviceMap.end()) {
        std::cout << service_name << " is not exist!" << std::endl;
        return;
    }

    auto mit = it->second.m_methodMap.find(method_name);
    if (mit == it->second.m_methodMap.end()) {
        std::cout << service_name << ":" << method_name << " is not exist!" << std::endl;
        return;
    }

    google::protobuf::Service *service = it->second.m_service;
    const google::protobuf::MethodDescriptor *method = mit->second;

    google::protobuf::Message *request = service->GetRequestPrototype(method).New();
    if (!request->ParseFromString(args_str)) {
        std::cout << "request parse error, content:" << args_str << std::endl;
        return;
    }
    google::protobuf::Message *response = service->GetResponsePrototype(method).New();

    // 绑定回调：由于 C++ Protobuf Closure 限制，这里结合 std::bind 绑定 conn
    google::protobuf::Closure *done = google::protobuf::NewCallback<RpcProvider, 
                                                                    std::shared_ptr<RpcConnection>, 
                                                                    google::protobuf::Message*>
                                                                    (this, 
                                                                    &RpcProvider::SendRpcResponse, 
                                                                    conn, response);

    // 框架底层执行 RPC 逻辑
    service->CallMethod(method, nullptr, request, response, done);
}

void RpcProvider::SendRpcResponse(std::shared_ptr<RpcConnection> conn, google::protobuf::Message *response)
{
    std::string response_str;
    if (response->SerializeToString(&response_str)) {
        // 调用 Connection 去发数据并断开 socket
        conn->SendResponseAndClose(response_str);
    } else {
        std::cout << "serialize response_str error!" << std::endl; 
        conn->SendResponseAndClose(""); // 失败也应该断开
    }
}