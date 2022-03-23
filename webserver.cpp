#include "webserver.h"
#include <cassert>

static int *pipefd;

void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, 0x00, sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    int ret = sigaction(sig, &sa, NULL);
    assert(ret >= 0);
}

void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

WebServer::WebServer():timer_heap(-1)
{
    users = new http_conn[MAX_FD];
    timer_arr.resize(MAX_FD);
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete [] users;
    delete m_pool;
}

void WebServer::init(int port, int thread_num)
{
    m_port = port;
    m_thread_num = thread_num;

    m_pool = new Threadpool<http_conn>(m_thread_num, 20000);
    init_user_info();
}

void WebServer::event_listen()
{
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd  >= 0);

    struct linger tmp = {1,0};
    setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);
   
    ret = listen(m_listenfd, 10);
    assert(ret >= 0);

    m_epollfd= epoll_create(5);
    assert(m_epollfd >= 0);
    http_conn::m_epollfd = m_epollfd;
    addfd(m_epollfd, m_listenfd, false);

    timer_heap.epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret >= 0);
    pipefd = m_pipefd;
    set_nonblocking(m_pipefd[1]);
    addfd(m_epollfd, m_pipefd[0],false);

    addsig(SIGPIPE, SIG_IGN);
    addsig(SIGALRM, sig_handler);
    addsig(SIGTERM, sig_handler);
}

void WebServer::timer(int connfd, const sockaddr_in& client_address)
{
    users[connfd].init(connfd, client_address);

    Timer* timer = new Timer(3*TIMESLOT);
    timer->sockfd = connfd;
    timer_heap.add_timer(timer);
    timer_arr[connfd] = timer;
}

bool WebServer::handle_newclient()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
    if(connfd < 0)
    {
        return false;
    }

    if(http_conn::m_user_count + 4 >= MAX_FD)
    {
        return false;
    }

    timer(connfd, client_address);
    //printf("---new client initialized---\n");
    return true;
}

bool WebServer::handle_signal(bool &timeout, bool &stop_server)
{
    char signals[1024];
    int ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if(ret <= 0)
    {
        return false;
    }
    else
    {
        for(int i = 0; i < ret; i++)
        {
            switch(signals[i])
            {
            case SIGTERM:
                {
                    stop_server = true;
                    break;
                }
            case SIGALRM:
                {
                    timeout = true;
                    break;
                }
            default:
                break;
            }
        }
    }
    return true;
}

void WebServer:: handle_read(int sockfd)
{
    Timer* timer = timer_arr[sockfd];

    if(users[sockfd].read())
    {
        m_pool->append(users+sockfd);
        if(timer)
        {
            timer->expire = time(NULL) + 3*TIMESLOT;
        }
    }
    else
    {
        // it is the same as users[sockfd].close_conn();
        timer->terminate(m_epollfd);
        timer_heap.del_timer(timer);
    }
}

void WebServer::handle_write(int sockfd)
{
    Timer* timer = timer_arr[sockfd];
    
    if(users[sockfd].write())
    {
        //printf("---write returns true---\n");
        if(timer)
        {
            timer->expire = time(NULL) + 3*TIMESLOT;
        }
    }
    else
    {
        //printf("---write returns false---\n");
        timer->terminate(m_epollfd);
        timer_heap.del_timer(timer);
    }
}

void WebServer::event_loop()
{
    bool timeout = false;
    bool stop_server = false;

    alarm(TIMESLOT);

    while(!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR)
        {
            //printf("---epoll failure---\n");
            break;
        }

        for(int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            if(sockfd == m_listenfd)
            {
                bool flag = handle_newclient();
                if(!flag)
                {
                    //printf("---handle new client failed\n---");
                    continue;
                }
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                Timer* timer = timer_arr[sockfd];
                timer->terminate(m_epollfd);
                timer_heap.del_timer(timer);
            }
            else if(sockfd == m_pipefd[0] && events[i].events & EPOLLIN)
            {
                //printf("---handle_signal---\n");
                bool flag = handle_signal(timeout, stop_server);
                if(!flag)
                {
                    //printf("---handle signal failed---\n");
                }
            }
            else if(events[i].events & EPOLLIN)
            {
                handle_read(sockfd);
                //printf("---handle_read accomplished---\n");
            }
            else if(events[i].events & EPOLLOUT)
            {
                handle_write(sockfd);
                //printf("---handle_write accomplished---\n");
            }
            //printf("---stucked, line 247, webserver.cpp---\n");
        }
        //printf("---stucked, line 249, webserver.cpp---\n");
        if(!timer_heap.empty())
        {
            timer_heap.reheap();
        }

        if(timeout)
        {
            //printf("---timeout---\n");
            timer_heap.tick();
            timeout = false;
        }
    }
}
