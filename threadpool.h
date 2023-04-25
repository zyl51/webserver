#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include "locker.h"
#include "sem.h"
#include "cond.h"
#include <iostream>
#include <pthread.h>

// 线程池
template <class T>
class ThreadPool
{

private:
    // 线程的数量
    int m_thread_number;

    // 线程数组，大小为m_thread_number
    pthread_t *m_threads;

    // 允许等待的最大数量
    int m_max_requests;

    // 请求队列，需要处理的任务
    std::list<T *> m_workqueue;

    // 请求队列的互斥锁
    Locker m_queuelocker;

    // 是否有任务需要处理，信号量的数量
    Sem m_queuestat;

    // 是否结束进程
    bool m_stop;

public:
    // thread_number是线程池中线程的数量
    // max_requests是请求队列中最多允许的、等待处理的请求的数量
    ThreadPool(int thraed_number = 8, int max_requests = 10000);

    // 析构函数，销毁线程的数据
    ~ThreadPool();

    // 增加任务进工作队列中
    bool append(T *);

private:
    // 创建线程之后的运行函数
    static void *worker(void *arg);

    // 取出队列中任务，不断的运行线程处理任务
    void run();
};

#endif

// 线程池的构造函数
template <class T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests)
{

    // 传入错误的参数
    if (thread_number <= 0 || max_requests <= 0)
    {
        throw std::exception();
    }

    // 属性赋值
    m_thread_number = thread_number;
    m_max_requests = max_requests;
    m_stop = false;
    m_queuelocker = Locker();
    m_queuestat = Sem();
    m_workqueue.clear();

    // 创建线程数组
    m_threads = new pthread_t[m_thread_number];

    if (m_threads == nullptr)
    {
        throw std::exception();
    }

    // 创建线程
    for (int i = 0; i < m_thread_number; i++)
    {
        printf("create the %dth thread\n", i);

        // 如果创建失败返回
        if (pthread_create(&m_threads[i], nullptr, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }

        // 创建线程分离
        if (pthread_detach(m_threads[i]) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 线程池的析构函数，销毁一些变量
template <class T>
ThreadPool<T>::~ThreadPool()
{
    delete[] m_threads;
    m_stop = true;
    m_workqueue.clear();
}

template <class T>
bool ThreadPool<T>::append(T *requests)
{

    // 操作工作队列时一定要加锁，因为它被所有线程共享。
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(requests);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// 创建的线程需要运行的函数
template <class T>
void *ThreadPool<T>::worker(void *arg)
{

    ThreadPool *p = (ThreadPool *)arg;
    p->run();

    return p;
}

// 线程池处理函数
template <class T>
void ThreadPool<T>::run()
{
    while (!m_stop)
    {
        // 信号量不为0，可以运行
        m_queuestat.wait();
        m_queuelocker.lock();

        // 获取队列第一个请求
        T *requests = m_workqueue.front();
        // 第一个请求已经取出执行，可以pop掉
        m_workqueue.pop_front();

        // 解锁
        m_queuelocker.unlock();
        if (requests == nullptr)
        {
            continue;
        }

        // 请求的处理
        requests->process();
    }
}