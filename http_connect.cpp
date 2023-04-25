#include "http_connect.h"

// 定义HTTP响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char *doc_root = "/home/nowcoder/webserver/resources";

// 设置文件描述符的非阻塞
void setnonblocking(int fd)
{
    // 获取文件描述符的状态
    int old_option = fcntl(fd, F_GETFL);
    if (old_option == -1)
    {
        perror("fnctl get failed\n");
        exit(-1);
    }

    // 设置文件描述符状态
    int new_option = old_option | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, new_option) == -1)
    {
        perror("fcntl set failed\n");
        exit(-1);
    }
}

// 向epoll代理中加入需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    // EPOLLIN为监听读，EPOLLRDHUP为对方关闭连接时触发的事件
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.fd = fd;

    if (one_shot)
    {
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }

    // 添加事件
    int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    if (ret == -1)
    {
        perror("epoll_ctl failed");
        exit(-1);
    }
    // 设置文件为非阻塞
    setnonblocking(fd);
}

// 从epollfd中删除文件描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

void modifyfd(int epollfd, int fd, int event)
{

    epoll_event eventt;
    eventt.data.fd = fd;
    eventt.events = event | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &eventt);
}

// 所有socket的epoll文件描述符，指向红黑树
int Http_Connect::m_epollfd = -1;
// 用户的数量，客户端的数量
int Http_Connect::m_uesr_count = 0;

void Http_Connect::close_connect()
{
    // 如果没有被关闭
    if (m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;  // 设置为当前数组中用户已经被关闭，已经空余
        m_uesr_count--; // 用户数减 1
    }
}

// 加入文件描述符的时候进行的初始化
void Http_Connect::init(int sockfd, const sockaddr_in &addr)
{
    this->m_sockfd = sockfd;
    this->m_address = addr;

    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, m_sockfd, true);
    m_uesr_count++;
    init();
}

void Http_Connect::init()
{
    bzero(m_read_buf, READ_BUFFER_SIZE);
    m_read_idx = 0;    // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    m_checked_idx = 0; // 当前正在分析的字符在读缓冲区中的位置
    m_start_line = 0;  // 当前正在解析的行的起始位置

    m_check_state = CHECK_STATE_REQUESTLINE; // 主状态机当前所处的状态
    m_method = GET;                          // 请求方法

    bzero(m_real_file, FILENAME_LEN); // 存储文件的真实文件路径，doc_root + m_url
    m_url = nullptr;                    // 请求目标文件的文件名
    m_version = nullptr;                // 协议版本，这里只支持1.1
    m_host = nullptr;                   // 请求主机名
    m_content_length = 0;               // 请求体的总长度
    m_linger = false;

    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    m_write_idx = 0;
    m_file_address = nullptr;
    m_iv_count = 0;

    byte_to_send = 0;
    byte_have_send = 0;
}

bool Http_Connect::read()
{
    // 一次性缓存区都读取
    // std::cout << "一次性读完\n";

    // 如果读到的缓存区的位置大于缓存区的大小
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    // 读取的字节数
    int bytes_read = 0;
    while (true)
    {
        // 从m_read_buf + m_read_idx索引出开始保存数据，大小是READ_BUFFER_SIZE - m_read_idx
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                          READ_BUFFER_SIZE - m_read_idx, 0);

        if (bytes_read == -1)
        {
            // 数据读完了
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            else
            {
                // 出错
                return false;
            }
        }
        else if (bytes_read == 0)
        {
            // 对方关闭连接
            return false;
        }

        // 更新数据读取的位置
        m_read_idx += bytes_read;
    }

    // std::cout << "读取到的数据\n" << m_read_buf << std::endl;
    return true;
}

// 解析http的一行数据，判断依据为\r\n
// 其实是获取一行数据
Http_Connect::LINE_STATE Http_Connect::parse_line()
{
    char temp;

    for (; m_checked_idx < m_read_idx; m_checked_idx++)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1 == m_read_idx))
            {
                // 发现到\r就结束了，那么数据不完整
                return LINE_OPEN;
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                // 匹配到\r\n
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 数据错误
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            // 匹配到\n，检查\r
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r'))
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 数据错误
            return LINE_BAD;
        }
    }

    return LINE_OPEN;
}

// 解析http请求行，获取请求方法，目标URL， HTTP版本
Http_Connect::HTTP_CODE Http_Connect::parse_request_line(char *text)
{
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url = '\0';
    m_url++;

    char *method = text;
    // 无视大小写比较
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST;
    }

    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    *m_version = '\0';
    m_version++;
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    // 有些请求是http://192.168.1.2/index.html
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        // 192.168.1.2/index.html
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    // 检查状态变成检查头
    m_check_state = CHECK_STATE_HEADER;

    return NO_REQUEST;
}

// 解析http的一个头部信息
Http_Connect::HTTP_CODE Http_Connect::parse_request_headers(char *text)
{
    // 遇到空行，表示解析头完毕
    if (text[0] == '\0') {
        // 如果请求体有数据，那么还需要获取请求体的数据，也就是content_length
        // 状态机转移到CHECK_STATE_CONTENT
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        //否则说明我们已经到达边界，已经解析完http请求头内容
        return GET_REQUEST;
    } 
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        /* 
            strspn函数会在str1中从左到右扫描每个字符，直到遇到第一个不在str2中的字符为止，
            并返回此时已经扫描过的str1的前缀长度。
            如果str1中的所有字符都在str2中出现过，则返回str1的长度。
        */
       text += strspn(text, " \t"); // 转移到空格和制表符后面
       m_host = text;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        // 判断是不是长连接
        if (strncasecmp(text, "keep-alive", 10))
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        // 获取了请求体的长度大小
        m_content_length = atol(text);
    }
    else 
    {
        printf("oop! unknow header %s\n", text);
    }

    return NO_REQUEST;
}


// 我们没有真正解析http的请求体，只是判断有没有完整的读入了
Http_Connect::HTTP_CODE Http_Connect::parse_request_content(char * text)
{
    if (m_read_idx >= m_content_length + m_checked_idx)
    {
        // 条件满足时，其实就是解析完成了请求头，请求体的长度 + 解析到的长度
        // 而 text 就是就是请求体的起始地址
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

// 主状态机，解析请求
Http_Connect::HTTP_CODE Http_Connect::process_read()
{
    // 初始的状态
    LINE_STATE line_state = LINE_OK; // 行的初始状态
    HTTP_CODE ret = NO_REQUEST;      // 总的一个请求头体的初始状态

    char *text = nullptr;

    // 解析到了一行完整的数据，或者解析到了请求体就不用一行一行解析，而是一整个解析
    while ((line_state = parse_line()) == LINE_OK 
        || ((m_check_state == CHECK_STATE_CONTENT) && (line_state == LINE_OK)))
    {

        // 获取行的起始地址，就是获取一行数据
        text = get_line();

        m_start_line = m_checked_idx;
        printf("got 1 http line : %s\n", text);

        // 有限状态机，看判断主状态机的状态
        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                // 获取请求行的结果
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER:
            {
                // 解析请求头
                ret = parse_request_headers(text);
                if (ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST)
                {
                    // 解析具体的请求信息
                    return do_request();
                }
                break;
            }

            case CHECK_STATE_CONTENT:
            {
                // 解析请求体
                ret = parse_request_content(text);

                if (ret == GET_REQUEST)
                {
                    return do_request();
                }
                // 失败表示请求尚未完整
                line_state = LINE_OPEN;
                break;
            }

            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }

    return NO_REQUEST;
}



// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
Http_Connect::HTTP_CODE Http_Connect::do_request()
{
    // 根目录 /home/nowcoder/webserver/resources
    // 先将根目录获取到
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // 留一个空字符的位置
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if (stat(m_real_file, &m_file_stat) < 0)
    {
        // stat函数调用失败, 认为没有这个资源
        return NO_REQUEST;
    }

    // 判断访问的权限
    // S_IROTH 是 stat 的宏，表明其他用户的可读权限
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    // 判断文件类型也可以用以下方法
    // if ((m_file_stat.st_mode & S_IFMT) == S_IFDIR)

    // 只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char * ) mmap(nullptr, m_file_stat.st_size, PROT_READ, MAP_SHARED, fd, 0);

    if (m_file_address == nullptr)
    {
        perror("mmap");
        exit(-1);
    }

    close(fd);
    // 数据内存映射成功
    return FILE_REQUEST;
}


void Http_Connect::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, sizeof(m_file_address));
        m_file_address = nullptr;
    }
}

bool Http_Connect::write()
{
    // 一次性写
    int temp = 0;

    if (byte_to_send == 0)
    {
        modifyfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp == -1)
        {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if (errno == EAGAIN)
            {
                modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
            }
        }

        byte_to_send -= temp;
        byte_have_send += temp;

        if (byte_have_send >= m_iv[0].iov_len)
        {
            m_iv[1].iov_base = m_file_address + byte_have_send - m_write_idx;
            m_iv[1].iov_len = byte_to_send;
        }
        else 
        {
            m_iv[0].iov_base = m_write_buf + byte_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (byte_to_send <= 0)
        {
            // 没有数据需要发送了
            unmap();
            modifyfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger)
            {
                init();
                return true;
            }
            else 
            {
                return false;
            }

        }

    }

    return true;
}

bool Http_Connect::add_reponse(const char * format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }

    va_list arg_list;
    va_start(arg_list, format);
    /*
        vsnprintf是C语言中的一个函数，用于格式化字符串。
        它的作用类似于printf函数，但使用可变数量的参数而非固定数量的参数。
        vsnprintf函数的第一个参数为目标缓冲区，第二个参数为缓冲区大小，第三个参数为格式化字符串，
        后面的参数为可变数量的参数列表。
        自动最后填空格
    */
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - m_write_idx,
        format, arg_list);

    if (len >= WRITE_BUFFER_SIZE - m_write_idx - 1)
    {// 读取的长度大于缓存区长度
        return false;
    }
    m_write_idx += len;

    va_end(arg_list);
    return true;
}

bool Http_Connect::add_status_line(int status, const char * title)
{
    return add_reponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool Http_Connect::add_headers(int content_length)  
{
    if (!add_content_length(content_length)) { return false; }
    if (!add_content_type()) { return false; }
    if (!add_linger()) { return false; }
    if (!add_blank_line()) { return false; }
    return true;
}

bool Http_Connect::add_content_length(int content_length)
{
    return add_reponse("Content-Length: %d\r\n", content_length);
}

bool Http_Connect::add_linger()
{
    return add_reponse("Connection: %s\r\n", m_linger ? "keep-alive" : "close");
}

bool Http_Connect::add_blank_line()
{
    return add_reponse("%s", "\r\n");
}

bool Http_Connect::add_content(const char * content)
{
    return add_reponse("%s", content);
}

bool Http_Connect::add_content_type()
{
    return add_reponse("Content-Type: %s\r\n", "text/html");
}

bool Http_Connect::process_write(Http_Connect::HTTP_CODE ret)
{
    switch(ret)
    {
        case BAD_REQUEST:
        {
            // 增加响应状态行
            add_status_line(400, error_400_title);
            // 响应状态头
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form))
            {
                return false;
            }
            break;
        }

        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
            {
                return false;
            }
            break;
        }

        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
            {
                return false;
            }
            break;
        }

        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            byte_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }

        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
            {
                return false;
            }
            break;
        }

        default:
            return false;
    }
    
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    byte_to_send = m_write_idx;
    return true;
}

void Http_Connect::process()
{

    // 解析读
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        // 请求数据不完整
        modifyfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // 生成响应
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        // 发现请求有错误，那么就关闭连接
        close_connect();
    }
    // 注册写事件，将错误或者资源返回给客户
    modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
}