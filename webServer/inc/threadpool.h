/* ************************************************************************
> File Name:     threadpool.h
# Author:         巫成洋
> Created Time:  Tue 23 Nov 2021 10:08:23 AM CST
> Description:   
 ************************************************************************/
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>
#include"locker.h"
//写一个线程池
template<class T>
class threadpool
{
public:
    //构造函数
    threadpool(int thread_number=8,int max_requests=10000);
    //析构函数
    ~threadpool();
    //向请求队列中添加任务
    bool append(T*request);
private:
    //工作线程入口函数
    static void *worker(void * arg);
    void run();//实际执行的函数
private:
   int m_thread_number;//维护线程池中线程的数量
   int m_max_requests;//请求队列中最大任务数量
   pthread_t * m_threads;//用于维护线程池中线程描述符
   std::list<T*>m_workqueue;//请求队列
   locker m_queuelocker;//互斥锁，用于保护请求队列
   sem m_queuestat;//信号量，用于同步线程之间请求队列中任务的数量
   bool m_stop;
};
//构造函数的具体实现
template<class T>
threadpool<T>::threadpool(int thread_number,int max_requests)
:m_thread_number(thread_number),m_max_requests(max_requests),m_threads(NULL),m_stop(false)
{
   if((thread_number<=0)||(max_requests<=0))
   {
       throw std::exception();
   }
   //动态分配一个数组
   m_threads=new pthread_t[m_thread_number];
   if(!m_threads)
   {
       throw std::exception();
   }
    //创建thread_number个线程，并设置为脱离状态
   for(int i=0;i<m_thread_number;++i)
   {
       printf("create the %dth thread \n",i);
       //当我们在c++程序中要创建线程时，传入的函数必须是静态函数，如果要在静态函数中调用类中非静态成员遍历
       //那么就需要将对象以参数的形式传入，所以传入的是this
       if(pthread_create(m_threads+i,NULL,worker,this)!=0)
       {
           delete [] m_threads;
           throw std::exception();
       }
       if(pthread_detach(m_threads[i]))
       {
           delete [] m_threads;
           throw std::exception();
       }
   }
}

template<class T>
threadpool<T>::~threadpool()
{
    delete [] m_threads;
    m_stop=true;
}

template<class T>
bool threadpool<T>::append(T * request)
{
    //互斥锁进行加锁以保护请求队列
    m_queuelocker.lock();
    if(m_workqueue.size()>=m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();//信号量加一，并且唤醒堵塞以请求任务的线程
    return true;
}

template<class T>
void * threadpool<T>::worker(void *arg)
{
    threadpool * pool =(threadpool*)arg;
    pool->run();
    return pool;
}
//实际执行的函数，不断的从请求队列中拿出任务执行
template<class T>
void threadpool<T>::run()
{
   while(!m_stop)
   {
       m_queuestat.wait();
       m_queuelocker.lock();
       if(m_workqueue.empty())
       {
           m_queuelocker.unlock();
           continue;
       }
       T*request=m_workqueue.front();
       m_workqueue.pop_front();
       m_queuelocker.unlock();
       if(!request)
       {
           continue;
       }
       printf("child thread handle task...\n");
       //处理该任务
       request->process();
       
   }
}
#endif
