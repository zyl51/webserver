#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include "locker.h"
#include "sem.h"
#include "cond.h"

// 线程池
template<class T>
class ThreadPool {

private:

    // 线程的数量
    int m_thread_number;

    // 线程数组，大小为m_thread_number
    pthread_t * m_threads;

    // 允许等待的最大数量
    int m_max_requests;

    // 请求队列，需要处理的任务
    std::list<T*> m_workqueue;

    // 请求队列的互斥锁
    locker m_queuelocker;

    // 是否有任务需要处理，信号量的数量
    sem m_queuestat;

    // 是否结束进程
    bool m_stop;

    
public:
    //thread_number是线程池中线程的数量
    //max_requests是请求队列中最多允许的、等待处理的请求的数量
    ThreadPool(int thraed_number = 8, int max_requests = 10000);

    // 析构函数，销毁线程的数据
    ~ThreadPool();

    // 增加任务进工作队列中
    bool append(T *);

private:
    // 创建线程之后的运行函数
    void * worker(void * arg);

    // 取出队列中任务，不断的运行线程处理任务
    void run();

};

#endif