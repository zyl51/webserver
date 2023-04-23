#include <iostream>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

class Http_Connect {
private:
    int m_sockfd; //该http来连接的fd，用于通信
    sockaddr_in m_address; // 客户端的信息

public:
    static int m_epollfd; // 所有socket的epoll文件描述符，指向红黑树
    static int m_uesr_count; // 用户的数量，客户端的数量
    

public:
    Http_Connect() {}
    ~Http_Connect() {}

public:
    // 接收新的连接，将新的连接加入epoll等操作
    void init(int sockfd, const sockaddr_in& addr);
    // 断开连接，需要关闭文件描述符
    void close_connect();
    // 处理客户端的请求
    void process();
    // 读取缓存区的数据,设置非阻塞
    bool read();
    // 向缓存区中写入数据，设置非阻塞
    bool write();

};