#ifndef LOCK_H
#define LOCK_H

#include <pthread.h>

class Locker
{
private:
    // 互斥锁
    pthread_mutex_t m_mutex;

public:
    Locker();
    ~Locker();

    // 加锁
    bool lock();

    // 解锁
    bool unlock();

    // 获取锁;
    pthread_mutex_t *get();
};

#endif