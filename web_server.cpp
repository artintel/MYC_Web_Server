#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "log.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd( int epollfd, int fd, bool one_shot = false );
extern int removefd( int epollfd, int fd );
extern int setnonblocking(int fd);
/*
struct sigaction
{
#ifdef __USE_POSIX199309
    union
    {
        _sighandler_t sa_handler;
        void (*sa_sigaction) ( int, siginfo_t*,  void* );
    }
    __sigaction_handler;
#define sa_handler        __sigaction_handler.sa_handler
#define sa_sigaction      __sigaction_handler.sa_sigaction
#else
    _sighandler_t sa_handler;
#endif
    _sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer) (void);
};
*/
void sig_handler(int);
void addsig( int sig, void( handler )(int) = sig_handler, bool restart = true ){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler; // SIG_IGN
    if( restart ){
        sa.sa_flags |= SA_RESTART;
    }
    // sa_mask 成员设置进程的信号编码，以确定哪些信号不能发送给本线程
    sigfillset( &sa.sa_mask ); // 在信号集中设置所有信号
    // int sigaction( int sig, const struct sigaction* act, struct sigaction* oact );
    assert( sigaction( sig, &sa, NULL ) != -1 ); // sigaction 信号处理函数接口
}

void show_error( int connfd, const char* info ){
    printf( "%s", info );
    send( connfd, info, strlen( info ), 0 );
    close( connfd );
}

// timer
// extern constexpr auto TIMESLOT = 5;
static int pipefd[2];

void sig_handler(int sig){
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void timer_handler(){
    timer_lst.tick();
    alarm( TIMESLOT );
}

void cb_func(http_conn* client){
	// epoll_ctl( m_epollfd, EPOLL_CTL_DEL, user_data->m_sockfd, 0 );
	// assert( user_data );
    // removefd(m_epollfd, m_sockfd);
    client->close_conn();
}


int main( int argc, char* argv[] ){
    if( argc <= 2 ){
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];       // 保存 webserver 的 ip 地址
    int port = atoi( argv[2] );     // 保存 webserver 的 端口

    // 资源路径
    char server_path[200];
    // getcwd()会将当前工作目录的绝对路径复制到参数buffer所指的内存空间中,
    // 参数maxlen为buffer的空间大小
    getcwd(server_path, 200);
    char root[11] = "/root_file";
    char* root_file = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(root_file, server_path);
    strcat(root_file, root);

    // 开启异步写日志 
    int LOGWrite = 1;
    // 默认日志不关闭
    int m_close_log = 0;

    //初始化日志
    if (1 == LOGWrite)
        Log::get_instance()->init("./ServerLog/Log", m_close_log, 2000, 800000, 800);
    else
        Log::get_instance()->init("./ServerLog/Log", m_close_log, 2000, 800000, 0);

    /* 忽略 SIGPIPE 信号
        SIG_IGN 表示忽略目标的信号，这是一个接收函数
        #include <bits/signum.h>
        #define SIG_DEF ((__sighandler_t) 0)
        #define SIG_IGN ((__sighandler_t) 1)
    */
    addsig( SIGPIPE, SIG_IGN ); // 默认情况下，往一个读端关闭的通道或 socket 连接中写数据将引发
                                // SIGPIPE 信号。我们需要捕获并处理，至少忽略。因为进程收到 SIGPIPE
                                // 信号的默认行为是结束进程。引起 `SIGPIPE 信号的写操作将设置 error 
                                // 为 EPIPE

    /* 预先为每个可能的客户连接分配一个 http_conn 对象 */
    http_conn* users = new http_conn[ MAX_FD ];
    assert( users );

    /* 启动数据库池 */
    connection_pool* connPool;
    string user = "root";    // 登陆数据库用户名
    string passWord = "123456";    // 登陆数据库密码
    string databaseName = "web";    // 使用数据库名
    int sql_num = 8;

    // 初始化数据库连接池
    connPool = connection_pool::GetInstance();
    connPool->init("localhost", user, passWord, databaseName, port, sql_num);

    // 初始化数据库读取表
    users->initmysql_result(connPool);

    /* 启动线程池 */
    threadpool< http_conn >* pool = NULL;
    try{
        pool = new threadpool< http_conn >(connPool);
    }
    catch( ... ){
        return 1;
    }

    int user_count = 0;

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 ); // 创建监听 socket 套接字
    assert( listenfd >= 0 );

    // struct linger {
    // 　　int l_onoff;
    // 　　int l_linger;
    // };

    /* Linux 下 tcp 连接断开的时候调用 close() 函数，有优雅断开和强制断开两种方式
    设置断开连接的方式是通过设置 socket 描述符一个 linger 结构体属性
    1. l_onoff != 0; l_linger 忽略
        close() 立刻返回，底层会将未发送完的数据发送完后再释放资源，即优雅退出
    2. l_onoff != 0; l_linger = 0;
        close() 立刻返回，但不会发送未发送完成的数据，而是通过一个 REST 包强制地关闭 socket 描述符，即强制退出
    3. l_onoff != 0; l_linger > 0;
        close() 不会立刻返回，内核会延迟一段时间，这个时间就由 l_linger 的值来决定。如果超时时间到达之前，发送
        完未发送的数据(包括 FIN 包)并得到另一端的确认，close() 会返回正确， socket 描述符优雅性退出。否则，close()
        会直接返回错误值，未发送数据丢失，socket 描述符强制退出。需要注意，如果 socket 描述符被设置未阻塞型，则 close()
        会直接返回值
     */

    // struct linger tmp = { 1, 1 }; // 优雅退出
    struct linger tmp = { 0, 1 }; // 优雅退出
    // int setsockopt( int sockfd, int level, int option_name, const void* option_value, 
    //                     socklen_t option_len );
    // SO_LINGER	若有数据待发送，则延迟关闭
    // SOL_SOCKET   通用 socket 选项，与协议无关
    setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof( tmp ) );

    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    ret = bind( listenfd, ( struct sockaddr*)&address, sizeof( address ) );
    assert( ret >= 0 );

    ret = listen( listenfd, 5 );
    assert( ret >= 0 );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    // 将 listenfd 注册到 epollfd 里的事件管理表中
    addfd( epollfd, listenfd, false );
    // static int m_epollfd; 类内静态成员的使用方式和静态必须在类外初始化
    http_conn::m_epollfd = epollfd;

    // timer
    ret = socketpair( PF_UNIX, SOCK_STREAM, 0, pipefd );
    assert( ret != -1 );
    setnonblocking( pipefd[1] );

    addfd( epollfd, pipefd[0] );
    addsig( SIGALRM );
    addsig( SIGTERM );
    bool stop_server = false;
    bool timeout = false;
    alarm( TIMESLOT );

    while( !stop_server ){
        // int epoll_wait( int epfd, struct epoll_event* events, int maxevents, int timeout );
        // wait 函数如果检测到事件，就酱所有就绪的事件从内核事件表(epfd参数指定)中复制到它的第二个参数
        // events 指向的数组中。这个而数组只用于输出 epoll_wait 检测到的就绪事件。
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if( ( number < 0 ) && ( errno != EINTR ) ){
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for( int i = 0; i < number; i++ ){
            int sockfd = events[i].data.fd;
            if( sockfd == listenfd ){
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength  );
                if( connfd < 0 ){
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                if( http_conn::m_user_count >= MAX_FD ){
                    show_error( connfd, "Internal server busy" );
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }
                /* 初始化客户链接 */
                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].m_timer = timer;
                timer_lst.add_timer(timer);
                users[connfd].init( connfd, client_address, root_file, user, passWord, databaseName);
            }
            else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) ){
                int sig;
                char signals[1024];
                ret = recv( pipefd[0], signals, sizeof( signals ), 0);
                if ( ret == -1 ) {
                    LOG_ERROR("%s", "signal failure");
                    continue;
                }
                else if ( ret == 0 ) {
                    LOG_ERROR("%s", "signal failure");
                    continue;
                }
                else{
                    for( int i = 0; i < ret; ++i ){
                        switch( signals[i] ){
                        case SIGALRM:
                        {
                            timeout = true;
                            break;
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                        }
                        }
                    }
                }
            }
            else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ){
                /* 如果有异常，直接关闭客户链接 */
                users[sockfd].close_conn();
            }
            else if( events[i].events & EPOLLIN ){
                /* 根据读的结果，决定是将任务添加到线程池，还是关闭连接 */
                util_timer* timer = users[sockfd].m_timer;
                if( timer ){
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 * TIMESLOT;
                    // printf( "adjust timer read\n" );
                    timer_lst.adjust_timer( timer );
                    LOG_INFO("%s", "adjust timer once");
                }
                // m_state = 0 | 1 来区分是读还是写的工作
                pool->append( users + sockfd, 0 );
                while(true){
                    // 数据可读触发, 线程回调函数开始读
                    // read 正确读取数据并且后续的处理都正确
                    // improv = 1; timer_flag = 0;
                    // read 读取报错
                    // improv = 1; timer_flag = 1;
                    if( users[sockfd].improv == 1 ){
                        // 数据可读触发，但是读到了错误信息或者说报错 timer_flag = 1
                        if( users[sockfd].timer_flag == 1 ){
                            users[sockfd].close_conn();
                            timer_lst.del_timer(timer);
                            users[sockfd].timer_flag = 0;
                            printf("%d-%d\n",sockfd, users[sockfd].m_sockfd);
                            LOG_INFO("close fd %d", users[sockfd].m_sockfd);
                        }
                        users[sockfd].improv = 0;
                        break;
                    }
                }
            }
            else if( events[i].events & EPOLLOUT ){
                /* 根据写的结果，决定是否关闭连接 */
                util_timer* timer = users[sockfd].m_timer;
                if( timer ){
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 * TIMESLOT;
                    // printf( "adjust timer write\n" );
                    timer_lst.adjust_timer( timer );   
                    LOG_INFO("%s", "adjust timer once");
                }
                pool->append( users + sockfd, 1 );
                while(true){
                    if( users[sockfd].improv == 1 ){
                        if( users[sockfd].timer_flag == 1 ){
                            users[sockfd].close_conn();
                            timer_lst.del_timer(timer);
                            users[sockfd].timer_flag = 0;
                            LOG_INFO("close fd %d", users[sockfd].m_sockfd);
                        }
                        users[sockfd].improv = 0;
                        break;
                    }
                }
            }
            else
            {}
        }
        if( timeout ){
            timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }   
    }
    close( epollfd );
    close( listenfd );
    close( pipefd[1] );
    close( pipefd[0] );
    delete[] users;
    delete pool;
    return 0;
}