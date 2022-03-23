#include "timer.h"

static const int MAX_FD = 65536;

Timer::Timer()
{
    expire = 1;
    valid = true;
}
Timer::Timer(time_t delay)
{
    expire = time(NULL) + delay;
    valid = true;
}

void Timer::terminate(int epollfd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, sockfd, 0);
    close(sockfd);
    http_conn::m_user_count--;
}

Timer_heap::Timer_heap(int epollfd)
{
    this->epollfd = epollfd;
    heap.reserve(MAX_FD);
}

bool Timer_heap::empty() const
{
    return heap.empty();
}

void Timer_heap::add_timer(Timer* timer)
{
    if(heap.empty())
    {
        heap.push_back(timer);
        return;
    }
    heap.push_back(timer);
    push_heap(heap.begin(), heap.end(), Timer_cmp());
}

void Timer_heap::pop_timer()
{
    Timer* tmp = heap.front();
    pop_heap(heap.begin(), heap.end(), Timer_cmp());
    heap.pop_back();
    delete tmp;
}

void Timer_heap::del_timer(Timer* timer)
{
    timer->valid = false;
    timer->expire = 1;   
    // actually, we didn't remove the timer from the heap
}

Timer* Timer_heap::top() const
{
    if(heap.empty())
    {
        return nullptr;
    }
    else
    {
        return heap.front();
    }
}

void Timer_heap::adjust_timer(Timer* timer, time_t delay)
{
    // update expire time if the there is something new on this connection
    timer->expire = time(NULL) + delay;
}

void Timer_heap::reheap()
{
    make_heap(heap.begin(), heap.end(), Timer_cmp());
}

void Timer_heap::tick()
{
    Timer *tmp = top();
    time_t cur = time(NULL);
    while(!heap.empty())
    {
        if(!tmp)
        {
            break;
        }
        if(tmp->expire > cur)
        {
            break;
        }
        if(tmp->valid)
        {
            tmp->terminate(epollfd);
        }
        pop_timer();
        tmp = top();
    }
}
