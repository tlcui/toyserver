#pragma once
#ifndef DB_CONN_POOL
#define DB_CONN_POOL

#include <mysql/mysql.h>
#include <list>
#include <string>
#include "locker.h"

// singleton
class db_conn_pool
{
public:
    static db_conn_pool* get_instance();
    void init(const std::string& url, const std::string& user, const std::string& password, const std::string& databasename,
              int port, int maxconn);
    MYSQL* get_connection();
    bool release_connection(MYSQL* conn);
    int get_freeconn_num();
    void destroy_conn_pool();

private:
    db_conn_pool();
    ~db_conn_pool();
    
    int m_max_conn;
    int m_cur_conn;
    int m_free_conn;
    Locker locker;
    Sem sem;

    std::list<MYSQL*> connlist;

    std::string m_url; // address
    int m_port; // port of mysql  
    std::string m_user; // user name of mysql
    std::string m_password;
    std::string m_databasename;

};

#endif
