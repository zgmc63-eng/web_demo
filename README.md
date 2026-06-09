# NGINX + FastCGI C Demo

这是一个不依赖 `libfcgi` 的最小 C 语言 FastCGI responder，用来观察 NGINX 和 FastCGI 程序之间如何初始化连接、传递参数、传递请求体并返回响应。

## 编译

```sh
make
```

## 启动 FastCGI 程序

```sh
./fastcgi_demo /tmp/fastcgi_demo.sock
```

程序启动时会完成这些初始化动作：

1. 创建 Unix domain socket：`socket(AF_UNIX, SOCK_STREAM, 0)`。
2. 删除旧 socket 文件：`unlink(path)`。
3. 绑定 socket 路径：`bind(...)`。
4. 开始监听：`listen(fd, 16)`。
5. 循环 `accept(...)` 等待 NGINX 连接。

## NGINX 配置示例

把下面的 `location` 放进一个 `server` 块里：

```nginx
location /fcgi-demo {
    include fastcgi_params;
    fastcgi_param SCRIPT_FILENAME /fcgi-demo;
    fastcgi_pass unix:/tmp/fastcgi_demo.sock;
}
```

测试：

```sh
curl 'http://127.0.0.1/fcgi-demo?name=nginx' -d 'hello=fastcgi'
```

## FastCGI 通信流程

NGINX 连接到 Unix socket 后，会按 FastCGI record 协议发送请求：

1. `FCGI_BEGIN_REQUEST`：告诉应用开始一个请求，角色通常是 `FCGI_RESPONDER`。
2. `FCGI_PARAMS`：发送 CGI/FastCGI 环境变量，例如 `REQUEST_METHOD`、`REQUEST_URI`、`QUERY_STRING`。空的 `FCGI_PARAMS` record 表示参数结束。
3. `FCGI_STDIN`：发送 HTTP 请求体。空的 `FCGI_STDIN` record 表示请求体结束。
4. 应用返回一个或多个 `FCGI_STDOUT` record，内容里先写 HTTP 响应头，再写响应体。
5. 应用发送空的 `FCGI_STDOUT` record，再发送 `FCGI_END_REQUEST` 表示请求完成。

核心代码在 `src/fastcgi_demo.c`：

- `listen_unix_socket()` 展示 FastCGI 服务端初始化。
- `handle_connection()` 展示读取 FastCGI record、收集参数和 body。
- `parse_params()` 展示 FastCGI name/value 参数解码。
- `send_response()` 展示 FastCGI 响应 record 的写法。

> 这个示例为了便于阅读，一次连接只处理一个请求，并且使用固定大小缓冲区；生产环境应该处理并发、长 body、错误恢复、权限和超时等问题。
