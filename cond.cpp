#include "cond.h"
#include <exception>

// 构造函数，初始化 m_cond
Cond::Cond()
{
    if (pthread_cond_init(&m_cond, NULL) != 0)
    {
        throw std::exception();
    }
}

// 销毁 m_cond
Cond::~Cond()
{
    pthread_cond_destroy(&m_cond);
}

// 条件阻塞等待唤醒 m_cond
bool Cond::wait(pthread_mutex_t *mutex)
{
    return pthread_cond_wait(&m_cond, mutex) == 0;
}

// 超时等待条件阻塞
bool Cond::timedwait(pthread_mutex_t *mutex, timespec t)
{
    return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
}

// 唤醒等待进程
bool Cond::signal()
{
    return pthread_cond_signal(&m_cond) == 0;
}

// 唤醒全部进程
bool Cond::broadcast()
{
    return pthread_cond_broadcast(&m_cond) == 0;
}