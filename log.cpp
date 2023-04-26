#include "log.h"

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if (m_fp != nullptr)
    {
        fflush(m_fp);
        fclose(m_fp);
    }
    m_fp = nullptr;
}


// 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
bool Log::init(const char * file_name, int log_buf_size, 
    int split_lines, int max_queue_size)
{
    // 如果设置了max_queue_size, 那么就是选择异步日志
    if (max_queue_size >= 1)
    {
        // 设置flag，异步日志
        m_is_async = true;
        // std::cout << m_is_async << std::endl;
        
        // 创建并设置队列的长度
        m_log_queue = new BlockQueue<std::string>(max_queue_size);

        pthread_t pid;

        pthread_create(&pid, nullptr, flush_log_thread, nullptr);
    }

    // 输出内容长度
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];

    // 日志最大行数
    m_split_lines = split_lines;

    time_t t = time(nullptr);
    struct tm * sys_tm = localtime(&t);
    struct tm my_tm = * sys_tm;

    // 从后往前找到第一个'/'
    const char * p = strrchr(file_name, '/');
    char full_file_name[256] = {0};

    // 提供的文件路径中没有
    if (p == nullptr)
    {
        // 文件名
        strcpy(log_name, file_name);
        snprintf(full_file_name, 255, "%d_%02d_%02d_%s", 
            my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }
    else
    {
        // 将 p 往后移动 1 个位置
        strncpy(log_name, p + 1, strlen(p + 1));
        // 路径名
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(full_file_name, 255, "%s%d_%02d_%02d_%s", 
            dir_name, my_tm.tm_year + 1900, my_tm.tm_mon, my_tm.tm_mday, log_name);

    }

    m_today = my_tm.tm_mday;

    // 追加的方式写
    m_fp = fopen(full_file_name, "a");
    if (m_fp == nullptr)
    {
        return false;
    }

    return true;
}


void * Log::async_write_log()
{
    std::string single_log;
    // std::cout << "--------" << single_log << std::endl;
    //从阻塞队列中取出一个日志string，写入文件
    while (m_log_queue->pop(single_log))
    {
        m_mutex.lock();
        // std::cout << "--------" << single_log << std::endl;
        fputs(single_log.c_str(), m_fp);
        fflush(m_fp);
        m_mutex.unlock();
    }
}

// 格式化写
void Log::write_log(int level, const char *format, ...)
{
    // struct timeval now; 微秒
    // struct timespec now; 纳秒
    struct timeval now;
    gettimeofday(&now, nullptr);
    time_t t = now.tv_sec;
    struct tm * sys_tm = localtime(&t);
    struct tm my_tm = * sys_tm;

    char levels[16] = {0};

    // 分级
    switch (level)
    {
        case 0:
            strcpy(levels, "[debug]:");
            break;
        case 1:
            strcpy(levels, "[info]:");
            break;
        case 2:
            strcpy(levels, "[warn]:");
            break;
        case 3:
            strcpy(levels, "[error]:");
            break;
        default:
            strcpy(levels, "[info]:");
            break;
    }

    m_mutex.lock();

    // 更新现有行数
    m_count ++ ;

    //日志不是今天或写入的日志行数是最大行的倍数
    //m_split_lines为最大行数
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
    {
        // 刷新
        fflush(m_fp);
        fclose(m_fp);   

        char tail[16] = {0};
        // 如果时间不是今天
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year, my_tm.tm_mon, my_tm.tm_mday);
        
        char new_log[256] = {0};

         //如果是时间不是今天,则创建今天的日志，更新m_today和m_count
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
             //超过了最大行，在之前的日志名基础上加后缀, m_count/m_split_lines
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    // 格式化输出到缓存区中
    va_list arg_list;
    va_start(arg_list, format);

    m_mutex.lock();

    // 时间格式化
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, levels);

    // 内容格式化
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 2, format, arg_list);

    // 加入换行和结束符
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';

    std::string log_str = std::string(m_buf);

    m_mutex.unlock();
    // std::cout << "m_buf-------------" << m_buf << std::endl;
    // std::cout << "log_str-------------" << log_str << std::endl;
    // 如果是异步日志，则放入队列中
    // std::cout << "----------" << m_log_queue->full() << std::endl;
    if (m_is_async && !m_log_queue->full())
    {
        // std::cout << "--------" << std::endl;
        m_log_queue->push(log_str);
    }
    else 
    {
        // 同步，直接写入文件
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(arg_list);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}