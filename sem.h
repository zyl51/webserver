#ifndef SEM_H
#define SEM_H

#include <semaphore.h>

class Sem {
private:
    // 信号量
    sem_t m_sem;

public:
    Sem();
    Sem(int);
    ~Sem();

    // 信号量的加锁，如果为0，就阻塞，调用一个减 1 
    bool wait();

    // 调用一次 + 1
    bool post();

};


#endif