#pragma once
#ifndef LOCKER_H
#define LOCKER_h 

#include <exception>
#include <pthread.h>
#include <semaphore.h>

//class of managing semaphore resources
class Sem
{
public:
    Sem()
    {
        if(sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    
    Sem(int num)
    {
        if(sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }

    ~Sem()
    {
        sem_destroy(&m_sem);
    }

    int wait()
    {
        return sem_wait(&m_sem);
    }

    int post()
    {
        return sem_post(&m_sem);
    }

private:
    sem_t m_sem;

//copy constructor is forbidden
//operator = will be called  in db_conn_pool.cpp
private:
    Sem(const Sem&) = delete; 
};

//class of managing mutex resources
class Locker
{
public:
    Locker()
    {
        if(pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    
    ~Locker() 
    {
        pthread_mutex_destroy(&m_mutex);
    }

    int lock()
    {
        return pthread_mutex_lock(&m_mutex);
    }

    int unlock()
    {
        return pthread_mutex_unlock(&m_mutex);
    }

private:
    pthread_mutex_t m_mutex;

//copy constructor and operator = is forbidden
private:
    Locker(const Locker&) = delete;
    Locker& operator = (const Locker&) = delete;
};

//class of managing condition variable sources
class Cond
{
public:
    Cond()
    {
        if(pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
        if(pthread_cond_init(&m_cond, NULL) != 0)
        {
            //allocated resources should be released if constructor fails
            pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }

    ~Cond()
    {
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }

    int wait()
    {
        pthread_mutex_lock(&m_mutex);
        int ret = pthread_cond_wait(&m_cond, &m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret;
    }

    int signal()
    {
        return pthread_cond_signal(&m_cond);
    }
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;

//copy constructor and operator = is forbidden
private:
    Cond(const Cond&) = delete;
    Cond& operator = (const Cond&) = delete;
};
#endif
