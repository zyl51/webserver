#ifndef LOG_H
#define LOG_H

#include <iostream>
#include "locker.h"
#include "blockqueue.h"
#include <cstring>
#include <stdarg.h>

class Log
{
public:
    // C++11之后静态局部变量不用担心线程安全问题
    static Log * get_instance()
    {
        static Log instance;
        return &instance;
    }

    // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char * file_name, int log_buf_size = 8129, 
        int split_lines = 50000, int max_queue_size = 0);

    // 异步写日志公有方法，调用私有方法async_write_log
    static void * flush_log_thread(void * arg)
    {
        Log::get_instance()->async_write_log();
    }

    //将输出内容按照标准格式整理
    void write_log(int level, const char * format, ...);


    //强制刷新缓冲区
    void flush();

private:
    Log();
    ~Log();

    // 异步写日志方法
    void * async_write_log();


private:

    char dir_name[128];     // 路径名
    char log_name[128];     // log文件名
    int m_split_lines;      // 日志最大行数
    int m_log_buf_size;     // 日志缓存区大小
    long long m_count;      // 日志行数记录
    int m_today;            // 按天分文件,记录当前时间是那一天
    FILE * m_fp;            // 打开log的文件指针
    char * m_buf;           // 要输出的内容
    BlockQueue<std::string> * m_log_queue;  // 阻塞队列
    bool m_is_async;        // 是否同步标志位
    Locker m_mutex;         // 同步类

};

//这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif