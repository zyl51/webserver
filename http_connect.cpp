#include "http_connect.h"

int Http_Connect::m_epollfd = -1; // 所有socket的epoll文件描述符，指向红黑树
int Http_Connect::m_uesr_count = 0 ; // 用户的数量，客户端的数量


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
    if (ret == -1) {
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

void modifyfd(int epollfd, int fd, int event) {

    epoll_event eventt;
    eventt.data.fd;
    eventt.events = event | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &eventt);

}

// 加入文件描述符的时候进行的初始化
void Http_Connect::init(int sockfd, const sockaddr_in& addr) {
    this->m_sockfd = sockfd;
    this->m_address = addr;

    init();
    
    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, m_sockfd, true);
    m_uesr_count ++ ;
}

void Http_Connect::init() {
    bzero(m_read_buf, READ_BUFFER_SIZE);
    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_url = nullptr;
    m_version = nullptr;
    m_host = nullptr;
    m_method = GET;
    m_linger = false;

    m_check_state = CHECK_STATE_REQUESTLINE;
    
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
    // std::cout << "一次性读完\n";

    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    while (true) {
        // 从m_read_buf + m_read_idx索引出开始保存数据，大小是READ_BUFFER_SIZE - m_read_idx
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, 
            READ_BUFFER_SIZE - m_read_idx, 0);

        if (bytes_read == -1) {
            // 数据读完了
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                return false;
            }

        } else if (bytes_read == 0) {
            // 对方关闭连接
            return false;
        }

        m_read_idx += bytes_read;

    }

    std::cout << "读取到的数据\n" << m_read_buf << std::endl;
    return true;
}


// 主状态机，解析请求
Http_Connect::HTTP_CODE Http_Connect::process_read() {

    // 初始的状态
    LINE_STATE line_state = LINE_OK;    //行的初始状态
    HTTP_CODE ret = NO_REQUEST;     // 总的一个请求头体的初始状态

    char * text = nullptr;

    // 解析到了一行完整的数据，或者解析到了请求头就不用一行一行解析，而是一整个解析
    while ((line_state = parse_line()) == LINE_OK
        || ((m_check_state == CHECK_STATE_CONTENT) && (line_state == LINE_OK))) {
        
        // 获取行的起始地址，就是获取一行数据
        text = get_line();

        m_start_line = m_checked_idx;
        printf("got 1 http line : %s\n", text);

        // 有限状态机，看判断主状态机的状态
        switch (m_check_state) {

            case CHECK_STATE_REQUESTLINE: {
                // 获取请求行的结果
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER: {
                // 解析请求头
                ret = parse_request_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    // 解析具体的请求信息
                    return do_request();
                }
                break;
            }

            case CHECK_STATE_CONTENT: {
                // 解析请求体
                ret = parse_request_content(text);

                if (ret == GET_REQUEST) {
                    return do_request();
                }
                // 失败表示请求尚未完整
                line_state = LINE_OPEN;
                break;
            }

            default : {
                return INTERNAL_ERROR;
            }

        }

    }

    return NO_REQUEST;
}


// 解析http的一行数据，判断依据为\r\n
// 其实是获取一行数据
Http_Connect::LINE_STATE Http_Connect::parse_line() {
    char temp;

    for (; m_checked_idx < m_read_idx; m_checked_idx ++ ) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') { 
            if ((m_checked_idx + 1 == m_read_idx)) {
                // 发现到\r就结束了，那么数据不完整
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_idx + 1] == '\n') {
                // 匹配到\r\n
                m_read_buf[m_checked_idx ++ ] = '\0';
                m_read_buf[m_checked_idx ++ ] = '\0';
                return LINE_OK;
            }
            // 数据错误
            return LINE_BAD;
        } else if (temp == '\n') {
            // 匹配到\n，检查\r
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx ++ ] = '\0';
                return LINE_OK;
            }
            // 数据错误
            return LINE_BAD;
        }

        
    }

    return LINE_OPEN;

}

// 解析http请求行，获取请求方法，目标URL， HTTP版本
Http_Connect::HTTP_CODE Http_Connect::parse_request_line(char * text) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url =  '\0';
    m_url ++ ;

    char * method = text;
    // 无视大小写比较
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }


    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    *m_version = '\0';
    m_version ++ ;
    if (strcasecmp(m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }   


    // 有些请求是http://192.168.1.2/index.html
    if (strncasecmp(m_url, "http://", 7) == 0) {
        // 192.168.1.2/index.html
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    // 检查状态变成检查头
    m_check_state = CHECK_STATE_HEADER;

}

// 解析http的一个头部信息
Http_Connect::HTTP_CODE Http_Connect::parse_request_headers(char * text) {
    
}


Http_Connect::HTTP_CODE Http_Connect::parse_request_content(char * text) {

}


Http_Connect::HTTP_CODE Http_Connect::do_request() {

}


bool Http_Connect::write() {
    // 一次性写
    std::cout << "一次性写完\n";

    return true;
}

void Http_Connect::process() {

    // 解析读
    HTTP_CODE ret = process_read();
    if (ret == NO_REQUEST) {
        // 请求数据不完整
        modifyfd(m_epollfd, m_sockfd, EPOLLIN);
        return ;
    }


    std::cout << "解析请求，创建响应" << std::endl;

}