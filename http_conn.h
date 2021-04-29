#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include "Timer_Linklist.h"
constexpr auto TIMESLOT = 60;
static sort_timer_lst timer_lst;
class http_conn{
public:
/* 文件名的最大长度 */
static const int FILENAME_LEN = 200;
/* 读缓冲区的大小 */
static const int READ_BUFFER_SIZE = 2048;
/* 写缓冲区的大小 */
static const int WRITE_BUFFER_SIZE = 1024;
/* HTTP 请求方法，但目前只支持 GET */
enum METHOD{ GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };
/* 解析客户请求时，主状态机所处的状态(第八章) https://blog.csdn.net/qq_39274501/article/details/112789128 */
enum CHECK_STATE{ CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
/* 服务器处理 HTTP 请求的可能结果 */
enum HTTP_CODE{ NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST,
INTERNAL_ERROR, CLOSE_CONNECTION };
/* 行的读取状态 */
enum LINE_STATUS{ LINE_OK = 0, LINE_BAD, LINE_OPEN };
public:
    http_conn(){}
    ~http_conn(){}
public:
    /* 初始化新接收的连接 */
    void init( int sockfd, const sockaddr_in& addr );
    /* 关闭连接 */
    void close_conn( bool real_close = true );
    /* 处理客户请求 */
    void process();
    /* 非阻塞读操作 */
    bool read();
    /* 非阻塞写操作 */
    bool write();
private:
    /* 初始化连接 */
    void init();
    /* 解析 HTTP 请求 */
    HTTP_CODE process_read();
    /* 填充 HTTP 应答 */
    bool process_write( HTTP_CODE ret );

    
    /* 下面这一组函数被 process_read 调用以分析 HTTP 请求 */
    HTTP_CODE parse_request_line( char* text );
    HTTP_CODE parse_headers( char* text );
    HTTP_CODE parse_content( char* text );
    HTTP_CODE do_request();
    char* get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();

    /* 下面这一组函数被 process_write 调用以填充 HTTP 应答 */
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_linger();
    bool add_content_length( int content_length );
    bool add_blank_line();
public:
    /* 所有 socket 上的事件都被注册到同一个 epoll 内核事件表中，所以将 epoll 文件描述符设置为静态的 */
    static int m_epollfd;
    /* 统计用户数量 */
    static int m_user_count;
    int m_sockfd;
    util_timer* m_timer;
private:
    /* 该 HTTP 连接的 socket 和对方的 socket 地址 */
    sockaddr_in m_address;
    /* 读缓冲区 */
    char m_read_buf[ READ_BUFFER_SIZE ];
    /* 标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置 */
    int m_read_idx;
    /* 当前正在分析的字符在都缓冲区中的位置 */
    int m_checked_idx;
    /* 当前正在解析的行的起始位置 */
    int m_start_line;
    /* 写缓冲区 */
    char m_write_buf[ WRITE_BUFFER_SIZE ];
    /* 写缓冲区中代发的字节数 */
    int m_write_idx;

    /* 主状态机当前所处的状态 */
    CHECK_STATE m_check_state;
    /* 请求方法 */
    METHOD m_method;
    /* 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root 是网站根目录 */
    char m_real_file[ FILENAME_LEN ];
    /* 客户请求的目标文件的文件名 */
    char* m_url;
    /* HTTP 协议版本号，这里仅支持 HTTP/1.1 */
    char* m_version;
    /* 主机名 */
    char* m_host;
    char* m_User_Agent;
    /* HTTP 请求的消息体长度 */
    int m_content_length;
    /* HTTP 请求是否要求保持连接 */
    bool m_linger;

    /* 客户请求的目标文件被 mmap 到内存中的起始位置 */
    char* m_file_address;
    /* 
        目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
        // struct stat 是用来描述 linux 系统文件属性的结构体
        // struct stat  
        // {   
        //     dev_t       st_dev;     /* ID of device containing file -文件所在设备的ID*/ 
        //     ino_t       st_ino;     /* inode number -inode节点号*/    
        //     mode_t      st_mode;    /* protection -保护模式?*/    
        //     nlink_t     st_nlink;   /* number of hard links -链向此文件的连接数(硬连接)*/    
        //     uid_t       st_uid;     /* user ID of owner -user id*/    
        //     gid_t       st_gid;     /* group ID of owner - group id*/    
        //     dev_t       st_rdev;    /* device ID (if special file) -设备号，针对设备文件*/    
        //     off_t       st_size;    /* total size, in bytes -文件大小，字节为单位*/    
        //     blksize_t   st_blksize; /* blocksize for filesystem I/O -系统块的大小*/    
        //     blkcnt_t    st_blocks;  /* number of blocks allocated -文件所占块数*/    
        //     time_t      st_atime;   /* time of last access -最近存取时间*/    
        //     time_t      st_mtime;   /* time of last modification -最近修改时间*/    
        //     time_t      st_ctime;   /* time of last status change - */    
        // }; */
    struct stat m_file_stat;
    /* 将采用 writev 来执行写操作，所以定义下面两个成员，其中 m_iv_count 表示读写内存块的数量
        // struct iovec { 
        //     ptr_t iov_base; /* Starting address */ 
        //     size_t iov_len; /* Length in bytes */ 
        // }; */
    // iovec 结构体封装了一块内存的地址起始位置和长度
    struct iovec m_iv[2];
    int m_iv_count;
};


#endif