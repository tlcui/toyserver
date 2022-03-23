#include "http_conn.h"

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satify.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title  = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

const char* doc_root = "/home/tlcui/toyserver/root";

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;
db_conn_pool* http_conn::m_connpool = db_conn_pool::get_instance();
std::unordered_map<std::string, std::string> http_conn::user_info = {};

void init_user_info()
{
    http_conn::m_connpool->init("localhost", "root", "123456", "toyserver", 0, 4);
    MYSQL* mysql = http_conn::m_connpool->get_connection();
    mysql_query(mysql, "select username, password from user");

    MYSQL_RES* result = mysql_store_result(mysql);

    MYSQL_ROW row = mysql_fetch_row(result);
    while(row)
    {
        std::string temp1(row[0]);
        std::string temp2(row[1]);
        http_conn::user_info[temp1] = temp2;
        row = mysql_fetch_row(result);
    }

    http_conn::m_connpool->release_connection(mysql);
}

int set_nonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}


void http_conn::close_conn(bool real_close)
{
    if(real_close && (m_sockfd!=-1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd  = -1;
        m_user_count--;
    }
}

void http_conn::init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd, sockfd, true);
    
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    m_user_count++;

    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    bytes_to_send = 0;
    bytes_have_send = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// read all the data from client, until there's nothing to read or client disconnects
bool http_conn::read()
{
    if(m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    int bytes_read = 0;
    while(1)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,  READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK) // read complete 
            {
                break;
            }
            return false;
        }
        else if(bytes_read == 0) // client disconnects
        {
            return false;
        }

        m_read_idx += bytes_read;
    }
    return true;
}

// vice state machine
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        // temp is the char to be parsed 
        temp = m_read_buf[m_checked_idx];
        
        // if temp=='\r', then it might be end of a line(it depends on the next char)
        if(temp == '\r')
        {
            // the next char reach the end of data, meaning incomplete data
            if((m_checked_idx+1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            // come across end of line 
            else if(m_read_buf[m_checked_idx+1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }

        // if temp=='\n', then it also might be the end of a line 
        else if(temp == '\n')
        {
            if((m_checked_idx > 1) && (m_read_buf[m_checked_idx-1] = '\0'))
            {
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// parse http requestline,obtain request method,URL and http version
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    // figure out and return the first position of '\t' or ' ' in the request line 
    m_url = strpbrk(text, " \t");

    // if there is no '\t' or ' ', then http syntax is wrong
    if(!m_url)
    {
        //printf("---bad request, line 198, http_conn.cpp---\n");
        return BAD_REQUEST;
    }

    // replace this position with '\0' to obtain the string in front of it 
    *m_url++ = '\0';

    // obtain the string
    char* method = text;

    if(strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
        //printf("---method: %s---\n", method);
    }
    else if(strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
    {
        //printf("---bad request, line 220, http_conn.cpp---\n");
        return BAD_REQUEST;
    }

    // skip the following ' ' and '\t'
    m_url += strspn(m_url, " \t");
    
    m_version = strpbrk(m_url, " \t");
    if(!m_version)
    {
        //printf("---bad request, line 230, http_conn.cpp---\n");
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    // we merely support http1.1
    if(strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        //printf("---bad request, line 240, http_conn.cpp---\n");
        return BAD_REQUEST;
    }
    
    // skip "http://" if it exists
    if(strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;

        // skip the address and find out the path of the file requested
        m_url = strchr(m_url, '/');
    }

    if(strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/')
    {
        //printf("---bad request, line 261, http_conn.cpp---\n");
        return BAD_REQUEST;
    }

    // in this case the request is a GET(m_url is /), getting index.html.
    if(strlen(m_url) == 1)
    {
        strcat(m_url, "index.html");
    }
    //printf("-----the client is looking for %s\n", m_url);
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    // find out is it a header or an empty line?
    if(text[0] ==  '\0') // remember that in parse_line() we replace all the \r\n with \0\0
    {
        // GET or POST?
        if(m_content_length)
        {
            // it is a POST, so we need to jump to parse the content 
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        //it is a GET. GET ends with an empty line, so now we have a complete http request of GET 
        //printf("---parse_headers complete, it is a GET---\n");
        return GET_REQUEST;
    }
    
    // still headers 
    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        
        //ski; all the ' ' and '\t'
        text += strspn(text, " \t");

        if(strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atoi(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        //printf("unknown header: %s\n", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    // whether we read all the content 
    if(m_read_idx >= m_content_length + m_checked_idx)
    {
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

// main state machine, which will be called by process()
http_conn::HTTP_CODE http_conn::process_read()
{
    // initialize the state of vice state machine and the result of http request 
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = nullptr;

    while( (m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)
           || ( (line_status = parse_line()) == LINE_OK) )
    {
        text = get_line();
        //printf("---got a line here: %s---\n", text);
        // the m_start_line for the next round
        m_start_line = m_checked_idx;

        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
                {
                    ret = parse_request_line(text);
                    if(ret == BAD_REQUEST)
                    {
                        return BAD_REQUEST;
                    }
                    break;
                }
            case CHECK_STATE_HEADER:
                {
                    ret = parse_headers(text);
                    if(ret == BAD_REQUEST)
                    {
                        return BAD_REQUEST;
                    }
                    else if(ret == GET_REQUEST)
                    {
                        //printf("---starting do_request---\n");
                        return do_request();
                    }
                    break;
                }
            case CHECK_STATE_CONTENT:
                {
                    ret = parse_content(text);
                    if(ret == GET_REQUEST)
                    {
                        return do_request();
                    }
                    
                    //break the cycle 
                    line_status = LINE_OPEN;

                    break;
                }
            default:
                {
                    return INTERNAL_ERROR;
                }
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    // initialize m_real_file
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    
    // find '/' in m_url
    const char *p = strrchr(m_url, '/');
    if(cgi==1 && ( *(p+1) == '2' || *(p+1) == '3' ) )
    {
       char flag = *(p+1);
       // the content looks like "user=123&password=123"
       char user[100], password_b[100];
       int i;
       for(i = 5; m_string[i] != '&'; ++i)
       {
            user[i-5] = m_string[i];
       }
       user[i-5] = '\0';
       int j=0;
       for(i = i + 10; m_string[i] != '\0'; ++i, ++j)
       {
            password_b[j] = m_string[i];
       }
       password_b[j] = '\0';
       if(flag == '3') //register
       {
            char* sql = new char[200];
            strcpy(sql, "insert into user(username, password) values(");
            strcat(sql, "'");
            strcat(sql, user);
            strcat(sql, "', '");
            strcat(sql, password_b);
            strcat(sql, "')");
            if(user_info.count(user)) // user name already exists
            {
                strcpy(m_url, "/registerError.html");
            }
            else
            {
                lock.lock();
                MYSQL* mysql = m_connpool->get_connection();
                user_info[user] = password_b;
                mysql_query(mysql, sql);
                m_connpool->release_connection(mysql);
                lock.unlock();
                strcpy(m_url, "/log.html");
            }
            delete [] sql;
       }
        else if(flag == '2') // log in
        {
            if(user_info.count(user) && user_info[user] == password_b)
            {
                strcpy(m_url, "/welcome.html");
            }
            else
            {
                strcpy(m_url, "/logError.html");
            }
        }
    }

    if(*(p+1) == '0')
    {
        char* m_url_real = new char[200];
        strcpy(m_url_real, "/register.html");

        // get the complete path of register.html
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        delete [] m_url_real;
    }
    
    else if(*(p+1) == '1')
    {
        char* m_url_real = new char[200];
        strcpy(m_url_real, "/log.html");

        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        delete [] m_url_real;
    }

    // it is not register nor login 
    else
    {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    if(stat(m_real_file, &m_file_stat) < 0) // file doesn't exist 
    {
        return NO_REQUEST;
    }

    if(!(m_file_stat.st_mode & S_IROTH)) // if not readable
    {
        return FORBIDDEN_REQUEST;
    }

    if(S_ISDIR(m_file_stat.st_mode)) // if it's a directory
    {
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = static_cast<char*>(mmap(NULL, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
    
    close(fd);
    //printf("---do_request returning FILE_REQUEST---\n");
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::add_response(const char* format, ...)
{
    if(m_write_idx > WRITE_BUFFER_SIZE)
    {
        return false;
    }

    va_list arg_list;
    va_start(arg_list, format);

    // write data(format) into m_write_buf, return the length of data written
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx,
                        format, arg_list);
    
    if(len >= WRITE_BUFFER_SIZE- 1 - m_write_idx)
    {
        va_end(arg_list);
        return false;
    }

    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", m_linger ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

// prepare the http-response
bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
        case INTERNAL_ERROR:
            {
                add_status_line(500, error_500_title);
                add_headers(strlen(error_500_form));
                if(!add_content(error_500_form))
                {
                    return false;
                }
                break;
            }

        case BAD_REQUEST:
            {
                add_status_line(404, error_404_title);
                add_headers(strlen(error_404_form));
                if(!add_content(error_404_form))
                {
                    return false;
                }
                break;
            }

        case FORBIDDEN_REQUEST:
            {
                add_status_line(403, error_403_title);
                add_headers(strlen(error_403_form));
                if(!add_content(error_403_form))
                {
                    return false;    
                }
                break;
            }
            
        case FILE_REQUEST:
            {
                add_status_line(200, ok_200_title);
                if(m_file_stat.st_size)
                {
                    add_headers(m_file_stat.st_size);

                    // let m_write_buf be the first memory area 
                    m_iv[0].iov_base = m_write_buf;
                    m_iv[0].iov_len = m_write_idx;

                    // let m_file_address be the second memory area 
                    m_iv[1].iov_base = m_file_address;
                    m_iv[1].iov_len = m_file_stat.st_size;

                    m_iv_count = 2;
                    
                    bytes_to_send = m_write_idx + m_file_stat.st_size;
                    return true;
                }
                else // the file requested is of zero size 
                {
                    const char* ok_string = "<html><body></body></html>";
                    add_headers(strlen(ok_string));
                    if(!add_content(ok_string))
                    {
                        return false;
                    }
                }
            }

        default: return false;
    }

    // other than FILE_REQUEST, we only need one memory area to send data
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count =1;
    bytes_to_send = m_write_idx;

    return true;
}

void http_conn::process()
{
    //printf("---process_read---\n");
    HTTP_CODE read_ret = process_read();
    //printf("---process_read complete---\n");
    // incomplete request, so we keep listening until it's ready next time 
    if(read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    
    //printf("---process_write start---\n");
    bool write_ret = process_write(read_ret);
    if(!write_ret)
    {
        //printf("---process_write failed, close_conn()---\n");
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

bool http_conn::write()
{
    //printf("---main thread start write()---\n");
    //printf("---%s---\n", m_write_buf);
    //printf("---%s---\n", m_file_address);
    int temp = 0;

    // empty http-response, usually it won't happen
    if(bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if(temp < 0)
        {
            if(errno == EAGAIN) // buffer is full
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }

            // if buffer is not full, then something must be wrong
            //printf("---line 711---, something's wrong\n");
            unmap();
            return false;
        }
        
        bytes_have_send += temp;
        bytes_to_send -= temp;

        // if data in the first memory area have been completely transmitted 
        if(bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if(bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if(m_linger)
            {
                init();
                return true;
            }
            else
            {
                //printf("---line 744---, complete, about to close\n");
                return false;
            }
        }
    }
}

