#pragma once
#ifndef TIMER
#define TIMER

#include <time.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <deque>
#include <queue>
#include <algorithm>
#include "http_conn.h"

class Timer
{
public:
    Timer();
    Timer(time_t delay);
    ~Timer() {}
    void terminate(int epollfd);

    time_t expire;
    int sockfd;

    bool valid;
};

struct Timer_cmp
{
    bool operator() (Timer* a, Timer* b)
    {
        return a->expire > b->expire;
    }    
};

class Timer_heap
{
public:
    Timer_heap(int epollfd);
    void add_timer(Timer* timer);
    void pop_timer();
    void del_timer(Timer* timer);
    void adjust_timer(Timer* timer, time_t delay);
    void reheap(); // keep the heap structure
    bool empty() const;
    Timer* top() const;
    
    // this function will be called if there is a expired timer
    void tick();

    int epollfd;

private:
    std::vector<Timer*> heap;
};
#endif

