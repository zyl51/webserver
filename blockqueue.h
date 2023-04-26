#ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H

#include <list>
#include <string>
#include <pthread.h>
#include <sys/time.h>
#include <exception>

template<class T>
class BlockQueue
{
private:
    std::list<T> m_blockqueue;  // 队列
    pthread_mutex_t m_mutex;    // 锁
    pthread_cond_t m_cond;      // 条件变量
    int m_max_size;             // 队列最大长度
    int m_size;                 // 有多少个请求在队列中

public:
    BlockQueue(int max_size = 1000);
    ~BlockQueue();

    // 将 item 加入队列之中
    bool push(const T& item);

    // 将 item 为传出参数，并将队列的头 pop 掉
    bool pop(T& item);

    // 设置等待时长
    bool pop(T& item, int seconds);

    // 判断队列是否已经满了
    bool full();

};

template<class T>
BlockQueue<T>::BlockQueue(int max_size)
{
    m_max_size = max_size;
    m_size = 0;
    // 初始化锁
    if (pthread_mutex_init(&m_mutex, nullptr)) { throw std::exception(); }
    if (pthread_cond_init(&m_cond, nullptr)) { throw std::exception(); }
}

template<class T>
BlockQueue<T>::~BlockQueue()
{
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

template<class T>
bool BlockQueue<T>::push(const T& item)
{
    pthread_mutex_lock(&m_mutex);
    if (m_size >= m_max_size)
    {
        // 通知消费者开始消费才行了 
        pthread_cond_signal(&m_cond);
        pthread_mutex_unlock(&m_mutex);
        return false;
    }

    // 加入队列之中
    m_blockqueue.push_back(item);

    // 告诉消费者有产品了
    m_size ++ ;
    pthread_cond_signal(&m_cond);
    pthread_mutex_unlock(&m_mutex);
    return true;
}


template<class T>
bool BlockQueue<T>::pop(T& item)
{
    pthread_mutex_lock(&m_mutex);
    while (m_size <= 0)
    {
        // 抢到互斥锁则返回0
        // 等待生产者开始生产
        if (pthread_cond_wait(&m_cond, &m_mutex) != 0)
        {
            pthread_mutex_lock(&m_mutex);
            return false;
        }
    }

    // 获取队列中的内容
    item = m_blockqueue.front();
    m_blockqueue.pop_front();

    m_size -- ;

    pthread_mutex_unlock(&m_mutex);

    return true;
}

template<class T>
bool BlockQueue<T>::pop(T& item, int seconds)
{
    timeval now;
    timespec t;
    gettimeofday(&now, nullptr);
    t.tv_sec = now.tv_sec + seconds;
    t.tv_nsec = now.tv_usec * 1000;

    pthread_mutex_lock(&m_mutex);

    if (m_size <= 0)
    {
        // 如果为满足，return false
        if (pthread_cond_timedwait(&m_cond, &m_mutex, &t) != 0)
        {
            pthread_mutex_unlock(&m_mutex);
            return false;
        }  
    }

    // 获取队列中的内容
    item = m_blockqueue.front();
    m_blockqueue.pop_front();

    m_size -- ;

    pthread_mutex_unlock(&m_mutex);

    return true;

}

// 判断队列是否已经满了
template<class T>
bool BlockQueue<T>::full()
{
    // std::cout << m_size << " " << m_max_size << std::endl;
    return m_size >= m_max_size;
}

#endif