#include "sem.h"
#include <exception>

// 构造函数
Sem::Sem()
{
    if (sem_init(&m_sem, 0, 0) != 0)
    {
        throw std::exception();
    }
}

// 构造函数带初始值 num
Sem::Sem(int num)
{
    if (sem_init(&m_sem, 0, (unsigned int)num) != 0)
    {
        throw std::exception();
    }
}

// 析构函数，销毁信号量 m_sem
Sem::~Sem()
{
    sem_destroy(&m_sem);
}

// 阻塞信号量，数值 -1
bool Sem::wait()
{
    return sem_wait(&m_sem) == 0;
}

// 增加信号的值，+1
bool Sem::post()
{
    return sem_post(&m_sem) == 0;
}