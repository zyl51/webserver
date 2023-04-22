#ifndef COND_H
#define COND_H

#include <pthread.h>
#include <time.h>


class Cond {

private:

    // 条件变量
    pthread_cond_t m_cond;
    

public:
    Cond();
    ~Cond();

    // 阻塞等待唤醒
    bool wait(pthread_mutex_t *);

    // 超时等待
    bool timedwait(pthread_mutex_t *, timespec);

    // 唤醒单个等待条件
    bool signal();

    // 唤醒全部等待条件
    bool broadcast();

};

#endif