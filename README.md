- [ ] 编译：

  ```bash
  g++ -o main web_server.cpp http_conn.cpp -lpthread
  ```

- 定时器的实现
  实现了最简单的双链表模式的定时器
  实现功能描述:
    (修改了只能HTTP连接过后才启动定时器的BUG)
    - 每建立一个连接，就会启动定时器，在超时时间到达仍不活跃的连接将被清除
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
      // 这里报错
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
  - [ ] 整理代码文件 - 应该会需要的用到 CMake 的知识了。后续学习
  - [ ] 整理 REAMDME，更美观
  - [ ] 尝试和数据库联结
  - [ ] 服务器资源或者数据库资源用网页的方式输出在客户端网页前端上
  - [ ] ...还没想好