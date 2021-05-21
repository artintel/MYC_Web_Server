#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "locker.h"
using namespace std;

template<class T>
class block_queue{
public:
    block_queue(int max_size = 1000){
        if(max_size <= 0)
            exit(-1);
        m_max_size = max_size;
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_array = new T[max_size];
    }
    void clear(){
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }
    ~block_queue(){
        m_mutex.lock();
        if( m_array != NULL )
            delete[] m_array;
        m_mutex.unlock();
    }
    // 判断队满
    bool full(){
        m_mutex.lock();
        if( m_size >= m_max_size ){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    // 判断队列是否空
    bool empty(){
        m_mutex.lock();
        if( m_size == 0 ){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    // 返回队首元素
    bool front(T& value){
        m_mutex.lock();
        if( m_size == 0 ){
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    // 返回队尾元素
    bool back(T& value){
        m_mutex.lock();
        if( m_size == 0 ){
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }
    int size(){
        int tmp = 0;
        m_mutex.lock();
        // 线程安全，避免查询过程中 m_size 被其他线程更新
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }
    int max_size(){
        return m_max_size;
    }
    // 队列添加元素
    bool push(const T& item){
        m_mutex.lock();
        if(m_size >= m_max_size){
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }
    // 取出任务
    bool pop(T& item){
        m_mutex.lock();
        while(m_size <= 0){
            if( !m_cond.wait(m_mutex.get())){
                m_mutex.unlock();
                return false;
            }
        }
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;
    cond m_cond;

    T* m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};

#endif