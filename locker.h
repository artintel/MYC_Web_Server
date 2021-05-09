#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

/* 封装信号量的类 */
class sem{
public:
    /* 创建并初始化信号量 */
    sem(){
        // int sem_init( sem_t* sem, int pshared, unsigned int value );
        if( sem_init( &m_sem, 0, 0 ) != 0 ){
            /* 构造函数没有返回值，可以通过抛出异常来报告错误 */
            throw std::exception();
        }
    }
    sem(int number){
        if( sem_init( &m_sem, 0, number ) != 0 ){
            throw std::exception();
        }
    }
    /* 销毁信号量 */
    ~sem(){
        // int sem_destroy( sem_t* sem );
        sem_destroy( &m_sem );
    }
    /* 等待信号量 */
    bool wait(){
        // int sem_wait( sem_t* sem );
        return sem_wait( &m_sem ) == 0;
    }
    /* 增加信号量 */
    bool post(){
        // int sem_post( sem_t* sem );
        return sem_post( &m_sem ) == 0;
    }
private:
    // POSIX 信号量类型
    sem_t m_sem;
};

/* 封装互斥锁的类 */
class locker{
public:
    /* 创建并初始化锁 */
    locker(){
        // int pthread_mutex_init( pthread_mutex_t* mutex, 
        //          const pthread_mutexattr_t* mutexattr );
        // 等价于 pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        if( pthread_mutex_init( &m_mutex, NULL ) != 0 ){
            throw std::exception();
        }
    }
    /* 销毁互斥锁 */
    ~locker(){
        // int pthread_mutex_destroy( pthread_mutex_t* mutex );
        pthread_mutex_destroy( &m_mutex );
    }
    /* 获取互斥锁 */
    bool lock(){
        // int ptrhead_mutex_lock( pthread_mutex_t* mutex );
        return pthread_mutex_lock( &m_mutex ) == 0;
    }
    /* 释放互斥锁 */
    bool unlock(){
        // int pthread_mutex_unlock( phtread_mutex_t* mutex );
        return pthread_mutex_unlock( &m_mutex ) == 0;
    }
private:
    pthread_mutex_t m_mutex;
};
/* 封装条件变量的类 */
/* 
   如果互斥锁是用于同步线程对共享数据的访问的话，
   那么条件变量则是用于在线程之间同步共享数据的值。
   条件变量提供了一种线程间的通知机制：当某个共享数据达到某个值的时候，
   唤醒等待这个共享数据的线程。
 */
class cond{
public:
    /* 创建并初始化条件变量 */
    cond(){
        // int pthread_cond_init( pthread_cond_t* cond, 
        //                        const pthread_condattr_t* cond_attr );
        if( pthread_mutex_init( &m_mutex, NULL ) != 0 ){
            throw std::exception();
        }
        if( pthread_cond_init( &m_cond, NULL ) != 0 ){
            /* 构造函数中一旦出现问题，就应该立即释放已经成功分配了的资源 */
            pthread_mutex_destroy( &m_mutex );
            throw std::exception();
        }
    }
    /* 销毁条件变量 */
    ~cond(){
        pthread_mutex_destroy( &m_mutex );
        // int pthread_cond_destroy( pthread_cond_t* cond );
        pthread_cond_destroy( &m_cond );
    }
    /* 等待条件变量 */
    bool wait(){
        int ret = 0;
        pthread_mutex_lock( &m_mutex );
        // int pthread_cond_wait( pthread_cond_t* cond, pthread_mutex_t* mutex );
        ret = pthread_cond_wait( &m_cond, &m_mutex );
        pthread_mutex_unlock( &m_mutex );
        return ret == 0;
    }
    /* 唤醒等待条件变量的线程 */
    bool signal(){
        // int pthread_cond_signal( pthread_cond_t* cond );
        return pthread_cond_signal( &m_cond ) == 0;
    }
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif