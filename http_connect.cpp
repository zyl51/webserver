#include "http_connect.h"

static int m_epollfd = -1; // 所有socket的epoll文件描述符，指向红黑树
static int m_uesr_count = 0 ; // 用户的数量，客户端的数量


// 设置文件描述符的非阻塞
void setnonblocking(int fd) {
    // 获取文件描述符的状态
    int old_option = fcntl(fd, F_GETFL);
    if (old_option == -1) {
        perror("fnctl get failed\n");
        exit(-1);
    }

    // 设置文件描述符状态
    int new_option = old_option | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, new_option) == -1) {
        perror("fcntl set failed\n");
        exit(-1);
    }
}


// 向epoll代理中加入需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    // EPOLLIN为监听读，EPOLLRDHUP为对方关闭连接时触发的事件
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.fd = fd;

    if (one_shot) {
        //防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }

    // 添加事件
    int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    if (~ret) {
        perror("epoll_ctl failed");
        exit(-1);
    }
    // 设置文件为非阻塞
    setnonblocking(fd);

}

// 从epollfd中删除文件描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// 加入文件描述符的时候进行的初始化
void Http_Connect::init(int sockfd, const sockaddr_in& addr) {
    this->m_sockfd = sockfd;
    this->m_address = addr;
    
    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, m_sockfd, true);
    m_uesr_count ++ ;
}

void Http_Connect::close_connect() {
    // 如果没有被关闭
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;  //设置为当前数组中用户已经被关闭，已经空余
        m_uesr_count -- ; // 用户数减 1
    }

}

bool Http_Connect::read() {
    // 一次性缓存区都读取

    return true;
}

bool Http_Connect::write() {
    // 一次性写

    return true;
}