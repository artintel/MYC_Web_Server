- [ ] 编译：

  ```bash
  g++ -o main web_server.cpp http_conn.cpp -lpthread
  ```

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
  - [ ] 增加定时器功能，对不活跃连接定时清除( 实现中 )
  - [x] 压力测试
  - [ ] 整理代码文件 - 应该会需要的用到 CMake 的知识了。后续学习
  - [ ] 整理 REAMDME，更美观
  - [ ] 尝试和数据库联结
  - [ ] 服务器资源或者数据库资源用网页的方式输出在客户端网页前端上
  - [ ] ...还没想好