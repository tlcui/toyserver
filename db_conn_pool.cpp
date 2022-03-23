#include "db_conn_pool.h"

db_conn_pool::db_conn_pool()
{
    m_cur_conn = 0;
    m_free_conn = 0;
}

db_conn_pool* db_conn_pool::get_instance()
{
    static db_conn_pool pool;
    return &pool;
}

void db_conn_pool::init(const std::string& url, const std::string& user, const std::string& password, const std::string& databasename,
          int port, int maxconn)
{
    m_url = url;
    m_port = port;
    m_user = user;
    m_databasename = databasename;
    m_password = password;

    for(int i=0; i < maxconn; i++)
    {
        MYSQL* con = nullptr;
        con = mysql_init(con);

        if(!con)
        {
            return;
        }
        con = mysql_real_connect(con, url.c_str(), user.c_str(), password.c_str(), databasename.c_str(), port, NULL, 0);

        if(!con)
        {
            return;
        }
        
        connlist.push_back(con);
        ++m_free_conn;
    }

    sem = Sem(m_free_conn);
    m_max_conn = m_free_conn;
}

MYSQL* db_conn_pool::get_connection()
{
    MYSQL* con = nullptr;
    if(connlist.empty())
    {
        return nullptr;
    }

    sem.wait();
    locker.lock();

    con = connlist.front();
    connlist.pop_front();

    --m_free_conn;
    ++m_cur_conn;

    locker.unlock();
    return con;
}

bool db_conn_pool::release_connection(MYSQL* conn)
{
    if(!conn)
    {
        return false;
    }

    locker.lock();

    connlist.push_back(conn);
    ++m_free_conn;
    --m_cur_conn;

    locker.unlock();
    sem.post();
    return true;
}

void db_conn_pool::destroy_conn_pool()
{
    locker.lock();
    if(!connlist.empty())
    {
        for(auto con : connlist)
        {
            mysql_close(con);
        }
        m_cur_conn = 0;
        m_free_conn = 0;
        connlist.clear();
    }
    locker.unlock();
}

int db_conn_pool::get_freeconn_num()
{
    return m_free_conn;
}

db_conn_pool::~db_conn_pool()
{
    destroy_conn_pool();
}
