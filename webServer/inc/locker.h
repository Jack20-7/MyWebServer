/* ************************************************************************
> File Name:     locker.h
# Author:         巫成洋
> Created Time:  Mon 22 Nov 2021 03:25:23 PM CST
> Description:   
 ************************************************************************/
#ifndef LOCKER_H
#define LOCKER_H

#include<exception>
#include<pthread.h>
#include<semaphore.h>
//封装信号量、互斥锁和条件变量，方便后面进行使用
class sem
{
public:
    sem()
    {
        if(sem_init(&m_sem,0,0)!=0)
        {
            throw std::exception();
        }
    }
    ~sem()
    {
      sem_destroy(&m_sem);
    }
    bool wait()
    {
        //将信号量减一，如果等于0就阻塞
       return  sem_wait(&m_sem)==0;
    }
    bool post()
    {
        //将信号量加一，如果大于零就唤醒一个等待该信号量的线程
        return sem_post(&m_sem)==0;
    }
private:
  sem_t m_sem; 

};
//互斥锁
class locker
{
public:
    locker()
    {
        if(pthread_mutex_init(&m_mutex,NULL)!=0)
        {
            throw std::exception();
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()
    {
       return pthread_mutex_lock(&m_mutex)==0;
    }
    bool unlock()
    {

        return pthread_mutex_unlock(&m_mutex)==0;
    }
private:
    pthread_mutex_t m_mutex;
};
//条件变量
class cond
{
public:
    cond()
    {
        if(pthread_mutex_init(&m_mutex,NULL)!=0)
        {
            throw std::exception();
        }
        if(pthread_cond_init(&m_cond,NULL)!=0)
        {
            pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
        pthread_mutex_destroy(&m_mutex);
    }
    bool wait()
    {
        int ret=0;
        pthread_mutex_lock(&m_mutex);
        ret=pthread_cond_wait(&m_cond,&m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret==0;
    }
    bool signal()
    {
       return pthread_cond_signal(&m_cond)==0;
    }
private:
    pthread_mutex_t m_mutex;//互斥锁，用来保护条件变量
    pthread_cond_t m_cond;//条件变量
};

#endif
