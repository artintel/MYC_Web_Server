#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log(){
    m_count = 0;
    m_is_async = false;
}

Log::~Log(){
    if(m_fp != NULL){
        fclose(m_fp);
    }
}

// 规定异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char* file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size){
    if( max_queue_size >= 1){
        // 边界条件，用 != 0 不是特别好
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    m_close_log = close_log;

    // 输出内容的长度
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);

    // 日志的最大行数
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    /*
        strrchr(const char* __s, int __c)
        字符串 s 从后往前查找第一个出现字符 c 的位置，int 传入，最后
        转为 char
    */
    const char* p = strrchr(file_name, '/');
    char log_full_name[1024] = { 0 };

    // 自定义日志名
    // 若输入的文件名没有 /, 则直接将时间 + 文件名作为日志名
    if(p == NULL){
        snprintf(log_full_name, 1023, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else{
        strcpy(log_name, p + 1);
        // p - file_name + 1 --> p 和 file_name 之间的字符串长度 + 1
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 1023, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    m_fp = fopen(log_full_name, "a");
    if(m_fp == NULL){
        return false;
    }
    return true;
}

void Log::write_log(int level, const char* format, ...){
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t  = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch(level){
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }

    m_mutex.lock();

    // 更新行数
    m_count++;

    // 日志写入前判断当前 day 是否是创建日志的时间，行数是否超过最大行限制
    // 如果是创建日志时间，写入日志，否则按照当前时间创建新的 log, 更新创建时间和行数
    // 如果超过最大行限制，在当前日志的末尾加 count/max_lines 为后缀创建新 Log
    // 日志不是今天或写入的日志行数是最大行的倍数
    // m_split_lines 为最大行数
    if( m_today != my_tm.tm_mday || m_count % m_split_lines == 0 ){
        char new_log[1024] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        // 格式化日志名中时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        // 如果时间不是今天，则创建今天的日志，更新 m_today 和 m_count
        if(m_today != my_tm.tm_mday){
            snprintf(new_log, 1023, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else{
            snprintf(new_log, 1023, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
    m_mutex.unlock();

    va_list valst;
    // 将传入的 format 参数赋值给 valst, 便于格式化输出
    va_start(valst, format);

    string log_str;
    m_mutex.lock();
    // 写入具体的事件内容格式
    int n = snprintf(m_buf, 1024, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    // 内容格式化，用于向字符串中打印数据、数据格式用户自定义
    // 返回写入到字符数组 str 中的字符个数(不包含终止符)
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;
    m_mutex.unlock();

    // 若 m_is_async 为 true 表示异步，默认同步
    // 若异步，则将日志信息假如阻塞队列，同步则加锁向文件中写
    if( m_is_async && !m_log_queue->full()){
        m_log_queue->push(log_str);
    }
    else{
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }
    va_end(valst);
}

void Log::flush(void){
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}
