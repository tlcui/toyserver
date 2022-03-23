#pragma once
#ifndef THREADPOOL_H
#define THREADPOOL_H 

#include <deque>
#include <cstdio>
#include "locker.h"

// T stands for the class of tasks 
template<class T>
class Threadpool
{
public:
    Threadpool(int thread_number = 8, int max_requests = 10000);
    ~Threadpool();
    int append(T* request); //add a request to the queue

private:
    static void* thread_work(void* arg); //function of threads
    void run(); //function of requests

private:
    int m_thread_number;
    int m_max_requests;
    pthread_t* m_threads; //array of threads, with number of m_thread_number 
    std::deque<T*> m_workqueue; //queue of requests 
    Locker m_queuelocker; //mutex of m_workqueue
    Sem empty_queue;
    int m_stop;
};

template<class T>
Threadpool<T>::Threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(0), m_threads(nullptr)
{
    if(thread_number <= 0 || max_requests <= 0)
    {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads)
    {
        throw std::exception();
    }

    //create detached threads
    for(int i=0;  i<thread_number; ++i)
    {
        //printf("create the %dth thread\n", i);
        if(pthread_create(m_threads+i, NULL, thread_work, this) != 0)
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
Threadpool<T>::~Threadpool()
{
    delete [] m_threads;
    m_stop = 1;
}

template<class T>
int Threadpool<T>::append(T* request)
{
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests)
    {
        //printf("---append failure, line 78---\n");
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    //printf("a request comes in\n");
    m_queuelocker.unlock();
    empty_queue.post();
    return 1;
}

template<class T>
void* Threadpool<T>::thread_work(void* arg)
{
    Threadpool* pool = (Threadpool*)arg;
    pool->run();
    return pool;
}

template<class T>
void Threadpool<T>::run()
{
    while(!m_stop)
    {
        //printf("---might blocked here, line 102, threadpool.h---\n");
        empty_queue.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty())
        {
            //printf("empty_queue\n");
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request)
        {
            //printf("---threadpool.h, line 115---\n");
            continue;
        }
        
        //printf("start processing\n");
        request->process(); //the class of tasks has to give a completion of process()
        //printf("end processing\n");
    }

    if(m_stop)
    {
        pthread_exit(NULL);
    }
}
#endif
