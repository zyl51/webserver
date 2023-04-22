#include "threadpool.h"
#include <iostream>

// 线程池的构造函数
template<class T>
ThreadPool<T>::ThreadPool(int thread_numebr, int max_requests) {

    // 传入错误的参数
    if (thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }


    // 属性赋值
    m_thread_number = thread_numebr;
    m_max_requests = max_requests;
    m_stop = false;
    m_queuelocker = locker();
    m_queuestat = sem();
    m_workqueue.clear();

    // 创建线程数组
    m_threads = new pthread_t[m_thread_number];

    if (m_threads == nullptr) {
        throw std::exception();
    }

    // 创建线程
    for (int i = 0; i < m_thread_number; i ++ ) {
        printf("create the %dth thread\n", i);

        // 如果创建失败返回
        if (pthread_create(m_threads[i], nullptr, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        // 创建线程分离
        if (pthread_detach(m_threads[i]) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

    }
}


// 线程池的析构函数，销毁一些变量
template<class T>
ThreadPool<T>::~ThreadPool() {
    delete[] m_threads;
    m_stop = true;
    m_workqueue.clear();
}


template<class T>
bool ThreadPool<T>::append(T * requests) {

    // 操作工作队列时一定要加锁，因为它被所有线程共享。
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(requests);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;

}


// 创建的线程需要运行的函数
template<class T>
void * ThreadPool<T>::worker(void * arg) {

    ThreadPool * p = (ThreadPool *) arg;
    p->run();

    return p;
}


// 线程池处理函数
template<class T>
void ThreadPool<T>::run() {

    while (!m_stop) {
        // 信号量不为0，可以运行
        m_queuestat.wait();
        m_queuelocker.lock();

        // 获取队列第一个请求
        T * requests = m_workqueue.front();
        // 第一个请求已经取出执行，可以pop掉
        m_workqueue.pop_front();

        // 解锁
        m_queuelocker.unlock();
        if (requests == nullptr) {
            continue;
        }

        // 请求的处理
        requests->process();
    }

}