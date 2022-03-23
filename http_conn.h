#pragma once
#ifndef HTTPCONN_H
#define HTTPCONN_H

#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <unordered_map>
#include <string>
#include "locker.h"
#include "db_conn_pool.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    /* methods of http requests */
    enum METHOD {GET=0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS,
                 CONNECT, PATCH};
    
    /* 3 possible states of the main state machine*/
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE=0, CHECK_STATE_HEADER,
                      CHECK_STATE_CONTENT};
    
    enum LINE_STATUS {LINE_OK=0, LINE_BAD, LINE_OPEN};

    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE,
                    FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR,
                    CLOSED_CONNECTION};

public:
    http_conn() {}
    ~http_conn() {}

public:
    // initialize a new accepted connection
    void init(int sockfd, const sockaddr_in& addr);

    // close a connection
    void close_conn(bool real_close = true);

    // process a client's request 
    void process();

    // nonblock reading
    bool read();

    // nonblock writing
    bool write();

private:  
    // initialize a new accepted connection,it will be called by init() above in public  
    void init();

    // parse http requests 
    HTTP_CODE process_read();

    // prepare http response
    bool process_write(HTTP_CODE ret);

    /* the group of functions listed below will be called by
     * process_read() to parse http requests */
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line () {return m_read_buf+m_start_line;}
    LINE_STATUS parse_line();

    /* the group of functions listed below will be called by
     * process_write() to givve http responses */
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_status_line(int status, const char* title);
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    static db_conn_pool* m_connpool;

    // key is username, value is password
    static std::unordered_map<std::string, std::string> user_info;

private:
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE];
    
    // mark the next position of the last read byte in m_read_buf 
    int m_read_idx;

    // mark the position of the char parsed currently in m_read_buf
    int m_checked_idx;
    
    // starting position of the line parsed currently 
    int m_start_line;

    char m_write_buf[WRITE_BUFFER_SIZE];

    // bytes to be sent in m_write_buf
    int m_write_idx;

    CHECK_STATE m_check_state;// state of the main state machine
    METHOD m_method;

    /* path of the file requested by client, actually it equals to
     * doc_root+m_url, where doc_root is the root directory 
     * see http_conn.cpp for doc_root*/ 
    char m_real_file[FILENAME_LEN];

    // name of the file requested by client
    char* m_url;

    // version of http protocal
    char* m_version;

    // name of host
    char* m_host;

    // length of http request message
    int m_content_length;

    // whether keep connectiong or not 
    bool m_linger;

    // starting position of requested file after mmap 
    char* m_file_address;

    struct stat m_file_stat;

    struct iovec m_iv[2];
    int m_iv_count;

    int cgi; // used for post
    char* m_string;
    Locker lock; // locker of user_info

    int bytes_to_send;
    int bytes_have_send;
};

int set_nonblocking(int fd);
void addfd(int epollfd, int fd, bool one_shot);
void removefd(int epollfd, int fd);
void modfd(int epollfd, int fd, int ev);
void init_user_info();

#endif
