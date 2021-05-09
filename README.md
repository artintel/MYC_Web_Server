> 编译： 

> `g++ -o web_server web_server.cpp http_conn.cpp sql_pool.cpp -lpthread -I/usr/include/mysql -L/usr/lib/mysql/ -L/usr/bin/mysql -lmysqlclient`

<a href="https://sm.ms/image/y6b97LAUYZQEutv" target="_blank"><img src="https://i.loli.net/2021/05/09/y6b97LAUYZQEutv.gif" ></a>

> ./web_server 192.168.249.133 8080
- 目前实现了
  - 系统启动时静态生成 8 个线程的线程池
  - epoll 管理 I/O 事件
  - 配合锁和信号量对任务队列进行管理 ( 线程池里的线程共享任务队列，任务队列采用 list 数据结构实现 )
  - 采用 REACTOR 模型管理不同的线程从任务队列中对任务进行并发处理
  - http/1.1 GET 请求资源响应
  - http POST 注册登陆过程向服务器推送相关信息
  - 统一事件源，定时器定时清理不活跃连接

- 数据库连接池的实现
  实现的原理：原理上来说和线程池的核心思想是相同的，就是设置定量的(或者动态增长)数据库连接动态地分配给用户，当用户不再使用时便断开放回池，降低了重新和数据库服务器连接的代价。
  每个线程都有一个成员 m_mysql_pool 用来指向初始化的数据库连接池(静态单例模式)，并在进行读写(主要是注册时需要得到相应的用户信息对比)时取用已经建立好了的数据库连接，事后再放回

  数据库连接池的类：
    std::list 来对给定数量的数据库连接进行维护 --> push_back pop_front front
    同时通过信号量初始化为数据库连接池大小和锁同步机制来观察使用情况
    > sem(poll_num)
    > lock
    > post or wait
    > unlock
  使用 RAII 机制实现数据库连接的释放操作
  ```cpp
  class connectionRAII{
  public:
    connectionRAII(MYSQL **con, connection_pool *connPool);
    ~connectionRAII();
    
  private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
  };
  ```
  在用户要使用数据库时
  ```cpp
  connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
    *SQL = connPool->GetConnection();
    
    conRAII = *SQL;
    poolRAII = connPool; // 通过这一句，connectionRAII::poolRAII 成员保存数据库连接池
    //  当connectionRAII 析构的时候，释放数据库连接
  }
  connectionRAII::~connectionRAII(){
	  poolRAII->ReleaseConnection(conRAII); // 当前使用的数据库连接放回数据库连接池
  }
  ```
  项目中 RAII 机制体现
  ```cpp
  // threadpool.h --> void threadpool< T >::run()
  if( request->read() ){
    request->improv = 1;
    connectionRAII mysqlcon(&request->mysql, m_connPool);
    request->process();
  }
  // mysqlcon 局部成员离开 if 作用域之后，自动调用 connectionRAII 析构函数，数据库连接释放回连接池
  ```

  利用 POST 方法来进行用户创建或者登陆过程中向服务器推送信息


- 编译：

  ```bash
  g++ -o main web_server.cpp http_conn.cpp -lpthread
  ```

- 定时器的实现
  实现了最简单的双链表模式的定时器
  实现功能描述:
    (修改了只能HTTP连接过后才启动定时器的BUG)
    - 每建立一个新的连接，该连接就会启动属于自己的定时器；把当超时时间到达仍不活跃的连接清除
    - 每次完成读或者写 ( EPOLLIN | EPOLLOUT ) 的操作都会进行定时器的更新
    - 主线程的定时器每隔一段周期进行检查，如果有到达超时时间的连接将被清除
  会需要修改的地方：
    - 纯连接的定时时间和 Keep-alive 的HTTP连接超时时间应该不同
    - 思考中...

  实现的原理：
  
  **统一事件源**

  信号是一种异步事件，信号处理函数和程序的主循环是两条不同的执行路线。把信号的主要处理逻辑放在程序的主循环中，当信号处理函数被触发时，通知主循环程序接收到信号，并把信号值传递给主循环，主循环再根据接收到的信号值执行目标信号对应的逻辑代码。信号处理函数使用管道来将信号“传递”给主循环：信号处理函数往管道写端写入信号值，主循环则从管道的读端读出该信号值。主循环通过使用 I/O 复用系统调用来监听管道的读端文件描述符上的可读时间。如此一来，信号事件就能和其他I/O事件一样被处理，即统一事件源

  周期性地调用 `alarm(TIMESLOT)`来设定闹钟
  对于每次连接触发任务后进行调整，要更新自己的超时时间
  ```cpp
  util_timer* timer = users[sockfd].m_timer;
  if( users[sockfd].read() ){
      pool->append( users + sockfd );
      if( timer ){
          time_t cur = time(NULL);
          timer->expire = cur + 3 * TIMESLOT;
          printf( "adjust timer read\n" );
          timer_lst.add_timer( timer );
      }
  }
  ```
  util_timer 定时器结构体

  成员包含了指向自己绑定的 http 连接的指针

  同时

  http_conn 的成员也需要包含对应的定时器成员

  他们之间通过 sort_timer_lst 相互联系

  sort_timer_lst 初始化定时器链表，每个定时器绑定对应的 http 连接

  http 的 m_timer 成员绑定对应的定时器，在进行调整的时候通过 http 能够直接找到对应的 timer 进行 adjust




- 压测
  - 测试工具：webbench
  - 测试环境: ubuntu 18.04

- 100 client

> Benchmarking: GET http://192.168.249.133:8080/index.html (using HTTP/1.1)

> 100 clients, running 60 sec.

> Speed=28906 pages/min, 810807 bytes/sec.

> Requests: 28906 susceed, 0 failed.

- 500 client

> Benchmarking: GET http://192.168.249.133:8080/index.html (using HTTP/1.1)
>
> 500 clients, running 60 sec.
>
> Speed=28512 pages/min, 799758 bytes/sec.
>
> Requests: 28512 susceed, 0 failed.

- 1000 client

> Benchmarking: GET http://192.168.249.133:8080/index.html (using HTTP/1.1)
>
> 1000 clients, running 60 sec.
>
> Speed=27718 pages/min, 777483 bytes/sec.
>
> Requests: 27718 susceed, 0 failed.

- 5000 client

> Benchmarking: GET http://192.168.249.133:8080/index.html (using HTTP/1.1)
>
> 5000 clients, running 60 sec.
>
> Speed=29728 pages/min, 833870 bytes/sec.
>
> Requests: 29728 susceed, 0 failed.

- 10000 client

> Benchmarking: GET http://192.168.249.133:8080/index.html (using HTTP/1.1)
>
> 10000 clients, running 60 sec.
>
> Speed=27708 pages/min, 777209 bytes/sec.
>
> Requests: 27708 susceed, 0 failed.

  计划：

  - [x] 阅读源码，了解了服务器解析客户端的 http 请求并最后返回“网页”
  - [x] 增加定时器功能，对不活跃连接定时清除( 已实现 )
  - [x] 压力测试
  - [x] 数据库连接池--保存注册登陆信息
  - [ ] 整理代码文件 - 应该会需要的用到 CMake 的知识了。后续学习
  - [ ] 整理 REAMDME，更美观
  - [x] 尝试和数据库联结
  - [x] 服务器资源或者数据库资源用网页的方式输出在客户端网页前端上
  - [ ] ...还没想好