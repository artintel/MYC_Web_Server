#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_pool.h"

using namespace std;

// 构造函数
connection_pool::connection_pool(){
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool* connection_pool::GetInstance(){
    static connection_pool connPool;
    return &connPool;    
}

// 构造初始化
void connection_pool::init(string url, 
                           string User, 
                           string PassWord, 
                           string DBName,
                           int Port,
                           int MaxConn /*, int close_log*/)
{
    // 初始化数据库信息
    // this->m_url = url;
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    // m_close_log = close_log;
    // 创建 MaxConn 条数据库连接
    for(int i = 0; i < MaxConn; i++){
        MYSQL* con = NULL;
        con = mysql_init(con);

        if(con == NULL){
            cout << "Error:" << mysql_error(con);
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

        if( con == NULL ){
            printf("日志错误系统待实现~\n");
            // LOG_ERROR("MySQL Error");
            exit(1);
        }
        // 更新连接池和空闲连接数量
        connList.push_back(con);
        ++m_FreeConn;
    }
    // 将信号量初始化为最大连接数量
    reserve = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* connection_pool::GetConnection(){
    MYSQL* con = NULL;
    if( connList.size() == 0 ) return NULL;

    // 取出连接，信号量原子减1，为0则等待，阻塞
    // wait() 就是信号量的减一操作
    reserve.wait();
    lock.lock();
    con = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;
    lock.unlock();
    return con;
}

// 释放当前连接
bool connection_pool::ReleaseConnection(MYSQL* con){
    if( con == NULL ) return false;
    lock.lock();
    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;
    lock.unlock();
    // 信号量加一
    reserve.post();
    return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool(){
    lock.lock();
    if(connList.size() > 0){
        for(auto elem : connList){
            MYSQL* con = elem;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}

// 当前空闲的连接数
int connection_pool::GetFreeConn(){
    // return this->m_FreeConn;
    return m_FreeConn;
}

connection_pool::~connection_pool(){
    DestroyPool();
}

// 修改的是数据库连接，而数据库连接本身就是指针类型，所以参数需要通过双指针才能对其进行修改
connectionRAII::connectionRAII(MYSQL** SQL, connection_pool* connPool){
    *SQL = connPool->GetConnection();
    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);
}