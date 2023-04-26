#include <iostream>
#include "http_connect.h"
#include <signal.h>
#include <cstring>
#include <assert.h>
#include "threadpool.h"
#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>
#include "log.h"

const int MAX_FD = 65535;           // 文件描述符的最大数量
const int MAX_EVENT_NUMBER = 10000; // 监听的最大事件个数

// 添加文件描述符
extern void addfd(int epollfd, int fd, bool one_shot);
// 删除文件描述符
extern void removefd(int epollfd, int fd);
// 修改文件描述符
extern void modifyfd(int epollfd, int fd, int event);

Log * log = nullptr;

// 增加信号处理函数
void addsig(int sig, void(handler)(int))
{

    struct sigaction sa;
    //  清空sa
    memset(&sa, '\0', sizeof(sa));

    sa.sa_flags = 0;
    sa.sa_handler = handler; // 设置捕捉到信号之后的处理函数
    sigfillset(&sa.sa_mask); // 全部置为 1
    sigaction(sig, &sa, NULL);
}

int main(int argc, char *argv[])
{

    // 如果参数不正确就返回并且告诉他我们该怎么操作
    if (argc <= 1)
    {
        printf("userage: %s port\n", basename(argv[0]));
        return 1;
    }

    // 获取访问端口
    int port = atoi(argv[1]);
    // 增加信号量的捕捉
    addsig(SIGPIPE, SIG_IGN);

    try
    {
        log = Log::get_instance();
    }
    catch(...)
    {
        return 1;
    }

    log->init("./ServerLog", 2000, 800000, 800);
    // std::cout << "----" << log << std::endl;


    // 创建线程池，任务类型时 Http_Connect
    ThreadPool<Http_Connect> *pool = nullptr;
    // 进行异常处理
    try
    {
        pool = new ThreadPool<Http_Connect>(8, 10000);
    }
    catch (...)
    {
        return 1;
    }

    // 创建一个连接的数组，表示的文件描述符
    Http_Connect *users = new Http_Connect[MAX_FD];

    // 创建监听的socketfd
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1)
    {
        perror("socket failed");
        return -1;
    }

    // 设置端口复用
    int reuse = 1;
    int setsockopt_ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定端口
    sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    int bind_ret = bind(listenfd, (sockaddr *)&address, sizeof(address));
    if (bind_ret == -1)
    {
        perror("bind failed");
        return -1;
    }

    // 设置监听
    listen(listenfd, 5);

    // 创建读写缓存区改变的事件数组，也就是存储epoll查询事件的
    epoll_event events[MAX_EVENT_NUMBER];

    // 创建我们的代码epoll
    int epollfd = epoll_create(5);
    if (epollfd == -1)
    {
        perror("epoll_create failed");
        return -1;
    }
    // 添加到epoll事件中
    addfd(epollfd, listenfd, false);
    Http_Connect::m_epollfd = epollfd;

    while (true)
    {

        // 阻塞判断数据缓存是否有变化
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        // 如果不是被信号中断的错误
        if ((number == -1) && (errno != EINTR))
        {
            perror("epoll_wait failed");
            break;
        }

        for (int i = 0; i < number; i++)
        {

            int sockfd = events[i].data.fd;
            if (sockfd == listenfd)
            {

                // 获取客户端的文件描述符，用于进行通信
                sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (sockaddr *)&client_address, &client_addrlength);
                if (connfd == -1)
                {
                    printf("accept failed, errno: %d\n", errno);
                    break;
                }

                // 判断服务器服务的用户是否已经爆满
                if (Http_Connect::m_uesr_count >= MAX_FD)
                {
                    //std::cout << "向客户端发送服务器正忙，请稍后重试\n";
                    close(connfd); // 关闭通信的客户端文件描述符
                    continue;
                }

                users[connfd].init(connfd, client_address);
            }
            else if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
            {
                // EPOLLRDHUP 表示对端的写操作已经关闭
                // EPOLLHUP 表示对端异常关闭连接、套接字错误、或者套接字被重置
                users[sockfd].close_connect();
            }
            else if (events[i].events & EPOLLIN)
            {
                // 检测到读事件，一次性读
                if (users[sockfd].read())
                {
                    // 加入事件处理
                    pool->append(&users[sockfd]);
                }
                else
                {
                    // 关闭连接
                    users[sockfd].close_connect();
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                if (users[sockfd].write() == false)
                {
                    // 关闭连接
                    users[sockfd].close_connect();
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;

    return 0;
}