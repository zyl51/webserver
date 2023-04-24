#include <iostream>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

class Http_Connect {
public:
    static const int READ_BUFFER_SIZE = 2048;     // 读缓存区的大小
    static const int WRITE_BUFFER_SIZE = 1024;    // 写缓存区的大小

    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
   enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATE {LINE_OK = 0, LINE_BAD, LINE_OPEN};

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, 
        FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};


public:
    static int m_epollfd; // 所有socket的epoll文件描述符，指向红黑树
    static int m_uesr_count; // 用户的数量，客户端的数量

private:
    int m_sockfd; //该http来连接的fd，用于通信
    sockaddr_in m_address; // 客户端的信息

    char m_read_buf[READ_BUFFER_SIZE];  ///读缓存区
    int m_read_idx; // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置

    int m_checked_idx;                      // 当前正在分析的字符在读缓冲区中的位置
    int m_start_line;                       // 当前正在解析的行的起始位置
    char * m_url;                           // 请求目标文件的文件名
    char * m_version;                       // 协议版本，这里只支持1.1
    char * m_host;                          // 请求主机名
    METHOD m_method;                        // 请求方法
    bool m_linger;                          //  http请求是否保持连接


    CHECK_STATE m_check_state;              // 主状态机当前所处的状态


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

private:
    //初始化其他数据的
    void init();
    // 解析http请求
    HTTP_CODE process_read();

    // 下面的函数被 process_read 调用，用以解析数据
    HTTP_CODE parse_request_line(char * text);  //请求行
    HTTP_CODE parse_request_headers(char * text);   // 请求头
    HTTP_CODE parse_request_content(char * text);   // 请求体
    HTTP_CODE do_request(); // 做请求
    LINE_STATE parse_line();    // 解析一行
    char * get_line() { return m_read_buf + m_start_line; } // 取得的行的首地址


};