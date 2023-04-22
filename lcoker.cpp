#include "locker.h"
#include <exception>

// 构造函数，初始化 m_mutex
Locker::Locker() {
    if (pthread_mutex_init(&m_mutex, NULL) != 0) {
        throw std::exception();
    }
}


// 析构函数，销毁 m_mutex
Locker::~Locker() {
    pthread_mutex_destroy(&m_mutex);
}


// 给 m_mutex 加锁
bool Locker::lock() {
    return pthread_mutex_lock(&m_mutex) == 0;
}


// 解开 m_mutex 的锁
bool Locker::unlock() {
    return pthread_mutex_unlock(&m_mutex) == 0;
}

// 获取 m_mutex
pthread_mutex_t * Locker::get() {
    return &m_mutex;
}