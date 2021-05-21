#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
/* 引入第 14 章介绍的线程同步机制包装类 */
#include "locker.h"
#include "sql_pool.h"

template < typename T >
class threadpool{
public:
    /* 参数 thread_number 是线程池中线程的数量，max_requests 是请求队列中最多允许的、
    等待处理的请求的数量 */
    threadpool( connection_pool* connPool, int thread_number = 8, int max_requests = 10000 );
    ~threadpool();
    /* 往请求队列中添加任务 */
    bool append( T* request, int state );
private:
    /* 工作线程运行的函数，它不断从工作队列中取出任务并执行之 */
    /* static 多个线程池共用 worker */
    static void* worker( void* arg );
    void run();

private:
    connection_pool* m_connPool;        /* 数据库连接 */
    int m_thread_number;                /* 线程池中的线程数 */
    int m_max_requests;                 /* 请求队列中允许的最大请求数 */
    pthread_t* m_threads;               /* 描述线程池的数组，其大小为 m_thread_number */
    std::list< T* > m_workqueue;        /* 请求队列 */
    locker m_queuelocker;               /* 保护请求队列的互斥锁 */
    sem m_queuestat;                    /* 是否有任务需要处理 */
    bool m_stop;                        /* 是否结束线程 */
};

template< typename T >
threadpool< T >::threadpool( connection_pool* connPool, int thread_number, int max_requests ):
                                m_connPool(connPool),
                                m_thread_number( thread_number ), 
                                m_max_requests( max_requests ), m_stop( false ), m_threads( NULL )
{
    if( ( thread_number <= 0 ) || ( max_requests <= 0 ) ){
        throw std::exception();
    }
    m_threads = new pthread_t[ m_thread_number ];
    if( !m_threads ){
        throw std::exception();
    }
    /* 创建 thread_number 个线程，并将它们都设置为脱离线程 */
    for( int i = 0; i < thread_number; ++i ){
        printf( "create the %dth thread\n", i );
        if( pthread_create( m_threads + i, NULL, worker, this ) != 0 ){
            delete[] m_threads;
            throw std::exception();
        }
        /*
        detachstate 线程脱离状态。它有 PTHREAD_CREATE_JOINABLE 和 PTHREAD_CREATE_DETACH 两个可选值。
        前者指定线程是可以被回收的，后者使调用线程脱离与进程中其他线程的同步，脱离了与其他线程同步的线程称为“脱离线程”。
        脱离线程在退出时将自行释放其占用的系统资源。线程创建时该属性的默认值时 PTHREAD_CREATE_JOINABLE。
        此外，也可以使用 pthread_detach 函数直接将线程设置为脱离线程
        */
        if( pthread_detach( m_threads[i] ) ){
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template< typename T >
threadpool< T >::~threadpool(){
    delete[] m_threads;
    m_stop = true;
}
template< typename T >
bool threadpool< T >::append( T* request, int state ){
    /* 操作工作队列时一定要加锁，因为它被所有线程共享 */
    /* 互斥锁 */
    m_queuelocker.lock();
    // threadpool( int thread_number = 8, int max_requests = 10000 );
    if( m_workqueue.size() > m_max_requests ){
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back( request );
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template < typename T >
void* threadpool< T >::worker( void* arg ){
    threadpool* pool = ( threadpool* )arg;
    pool->run();
    return pool;
}

template< typename T >
void threadpool< T >::run(){
    while( !m_stop ){
        m_queuestat.wait();
        m_queuelocker.lock();
        if( m_workqueue.empty() ){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if( !request ) continue;
        if( request->m_state == 0 ){
            if( request->read() ){
                request->improv = 1;
                connectionRAII mysqlcon(&request->mysql, m_connPool);
                request->process();
            }
            else{
                request->improv = 1;
                request->timer_flag = 1;
            }
        }
        else{
            // m_state == 1 写流程
            if( request->write() ){
                request->improv = 1;
            }
            else{
                request->improv = 1;
                request->timer_flag = 1;
            }
        }
    }
}
#endif