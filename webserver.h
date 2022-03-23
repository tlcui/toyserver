#pragma once
#ifndef WEBSERVER
#define WEBSERVER

#include <signal.h>
#include "http_conn.h"
#include "threadpool.h"
#include "timer.h"

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10000;
const time_t TIMESLOT = 5;

class WebServer 
{
public:
    WebServer(); 
    ~WebServer();

    void init(int port, int thread_num);
    void event_listen();
    void event_loop();
    bool handle_newclient();
    bool handle_signal(bool &timeout, bool &stop_server);
    void handle_read(int sockfd);
    void handle_write(int sockfd);
    void timer(int connfd, const struct sockaddr_in &client_address);

private:
    int m_port;
    int m_epollfd;
    int m_pipefd[2];
    http_conn *users; // an array 

    Threadpool<http_conn> *m_pool; // this is just a pointer, not an array
    int m_thread_num;

    epoll_event events[MAX_EVENT_NUMBER];
    int m_listenfd;

    Timer_heap timer_heap;
    std::vector<Timer*> timer_arr;  //an array, the index means the fd of the timer
};

#endif
