#include "server.h"
#include "mcp.h"
#include "cJSON.h"
#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/select.h>
#include <stdbool.h>
#include <strings.h>

#define BUFFER_SIZE 65536

/* 日志打印宏 */
/* 总是打印的日志（请求/响应） */
#define LOG(fmt, ...) printf("[LOG] " fmt "\n", ##__VA_ARGS__)

/* 调试日志（受 debug_log 控制） */
#define LOG_DEBUG(fmt, ...) do { if (g_server_config.debug_log) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); } while(0)

/* 信息日志（只在调试模式下输出） */
#define LOG_INFO(fmt, ...) do { if (g_server_config.debug_log) printf("[INFO] " fmt "\n", ##__VA_ARGS__); } while(0)

#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)

/* 全局配置 */
server_config_t g_server_config = {
    .port = 3001,
    .debug_log = false,  /* 默认启用调试日志 */
    .verbose = false
};

/* 全局变量 */
static volatile int g_running = 0;
static int g_server_fd = -1;

/* 函数声明 */
static void handle_client(int client_fd);
static int parse_http_request(const char *buf, char *method, size_t mlen,
                              char *path, size_t plen, const char **body_out, bool *use_sse_out);
static void http_send_json(int fd, int status, const char *json);
static void http_send_sse(int fd, const char *json);
static void http_send_sse_event(int fd, const char *json);
static int parse_http_headers(const char *buf, char *key, size_t klen, char *value, size_t vlen, const char **next);

/**
 * 初始化服务器
 */
#include <fcntl.h>

int server_init(int argc, char *argv[])
{
    int i;
    
    /* 解析命令行参数 */
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            g_server_config.port = atoi(argv[++i]);
            printf("mcpcomm: port set to %d\n", g_server_config.port);
        }
        else if (strcmp(argv[i], "--debug") == 0)
        {
            g_server_config.debug_log = true;
            printf("mcpcomm: debug log enabled\n");
        }
        else if (strcmp(argv[i], "--nodebug") == 0)
        {
            g_server_config.debug_log = false;
            printf("mcpcomm: debug log disabled\n");
        }
        else if (strcmp(argv[i], "--verbose") == 0)
        {
            g_server_config.verbose = true;
            printf("mcpcomm: verbose mode enabled\n");
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            printf("Usage: mcpcomm [options]\n");
            printf("Options:\n");
            printf("  --port <port>     Set server port (default: 3001)\n");
            printf("  --debug           Enable debug log\n");
            printf("  --nodebug         Disable debug log\n");
            printf("  --verbose         Enable verbose mode\n");
            printf("  --help            Show this help message\n");
            return -1;
        }
    }
    
    signal(SIGINT, server_stop_signal);
    signal(SIGTERM, server_stop_signal);
    signal(SIGPIPE, SIG_IGN);
    
    /* 初始化 HTTP 客户端 */
    if (client_init() != 0) {
        printf("server: client_init failed\n");
        return -1;
    }
    
    return 0;
}

/**
 * 信号处理函数
 */
void server_stop_signal(int signum)
{
    (void)signum;
    g_running = 0;
    if (g_server_fd >= 0)
    {
        shutdown(g_server_fd, SHUT_RDWR);
    }
}

/**
 * 创建服务器socket
 */
static int create_server(int port)
{
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    /* 创建socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        printf("server: socket creation failed: %s\n", strerror(errno));
        return -1;
    }

    /* 设置socket选项 */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        printf("server: setsockopt SO_REUSEADDR failed: %s\n", strerror(errno));
        close(server_fd);
        return -1;
    }

    /* 绑定地址 */
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 */
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        printf("server: bind failed on port %d: %s\n", port, strerror(errno));
        close(server_fd);
        return -1;
    }

    /* 监听 */
    if (listen(server_fd, 16) < 0)
    {
        printf("server: listen failed: %s\n", strerror(errno));
        close(server_fd);
        return -1;
    }

    printf("server: listening on 127.0.0.1:%d\n", port);
    return server_fd;
}

/**
 * 解析HTTP头部
 * 返回0表示找到头部，-1表示没有更多头部
 */
static int parse_http_headers(const char *buf, char *key, size_t klen, char *value, size_t vlen, const char **next)
{
    const char *line_end = strstr(buf, "\r\n");
    if (!line_end || buf == line_end)
    {
        /* 空行或没有更多头部 */
        *next = NULL;
        return -1;
    }

    /* 解析 key */
    size_t i = 0;
    while (i < (size_t)(line_end - buf) && buf[i] != ':' && i < klen - 1)
    {
        key[i] = buf[i];
        i++;
    }
    key[i] = '\0';

    if (i == 0)
    {
        *next = NULL;
        return -1;
    }

    /* 跳过冒号和空格/制表符 */
    size_t j = i;
    while (j < (size_t)(line_end - buf) && (buf[j] == ' ' || buf[j] == '\t' || buf[j] == ':'))
    {
        j++;
    }

    /* 解析 value */
    size_t v = 0;
    while (j < (size_t)(line_end - buf) && v < vlen - 1)
    {
        value[v] = buf[j];
        v++;
        j++;
    }
    value[v] = '\0';

    /* 下一个头部 - 跳过 \r\n */
    *next = line_end + 2;

    return 0;
}

/**
 * 解析HTTP请求
 */
static int parse_http_request(const char *buf, char *method, size_t mlen,
                              char *path, size_t plen, const char **body_out, bool *use_sse_out)
{
    const char *line_end = strchr(buf, '\n');
    if (!line_end)
    {
        printf("parse: no line ending found\n");
        return -1;
    }

    /* 复制请求行 */
    size_t line_len = line_end - buf;
    if (line_len == 0)
    {
        printf("parse: empty request line\n");
        return -1;
    }

    /* 解析方法 */
    size_t i = 0;
    while (i < line_len && buf[i] != ' ' && i < mlen - 1)
    {
        method[i] = buf[i];
        i++;
    }
    method[i] = '\0';

    if (i == 0)
    {
        printf("parse: no method found\n");
        return -1;
    }

    /* 跳过空格 */
    while (i < line_len && buf[i] == ' ')
    {
        i++;
    }

    /* 解析路径 */
    size_t j = 0;
    while (i < line_len && buf[i] != ' ' && j < plen - 1)
    {
        path[j] = buf[i];
        i++;
        j++;
    }
    path[j] = '\0';

    /* 解析头部，查找 SSE 支持 */
    const char *headers_start = strstr(buf, "\r\n\r\n");
    if (headers_start)
    {
        const char *header_line = buf;
        /* 跳过请求行 - HTTP请求行以\r\n结尾 */
        header_line = strstr(header_line, "\r\n");
        if (header_line)
        {
            header_line += 2; /* 跳过\r\n */
        }

        while (header_line && *header_line != '\0' && header_line < headers_start)
        {
            char key[64] = {0};
            char value[256] = {0};
            const char *next_header;

            if (parse_http_headers(header_line, key, sizeof(key), value, sizeof(value), &next_header) != 0)
            {
                break;
            }

            /* 调试日志：打印解析的头部（只在 debug 模式下输出） */
            LOG_DEBUG("Header - key='%s', value='%s'", key, value);

            /* 检查 Accept 头部以检测 SSE（只在 debug 模式下输出） */
            if (strcasecmp(key, "Accept") == 0 && strstr(value, "text/event-stream") != NULL)
            {
                if (use_sse_out)
                {
                    *use_sse_out = true;
                }
                LOG_DEBUG("SSE detected via Accept header");
            }

            header_line = next_header;
        }
    }

    const char *body = strstr(buf, "\r\n\r\n");
    if (body)
        *body_out = body + 4;
    else
        *body_out = NULL;

    return 0;
}

/**
 * 构建错误响应
 */
int build_error_response(const char *id_json, int code, const char *message, char *response, size_t response_len)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *error = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "jsonrpc", MCP_JSONRPC_VERSION);
    cJSON_AddStringToObject(root, "id", id_json);
    cJSON_AddItemToObject(root, "error", error);

    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str)
    {
        snprintf(response, response_len, "%s", json_str);
        cJSON_free(json_str);
    }
    cJSON_Delete(root);

    return 0;
}

/* 删除未使用的 debug_print_request 函数 */

/**
 * 发送JSON响应（非流式）
 */
static void http_send_json(int fd, int status, const char *json)
{
    char header[512];
    size_t body_len = json ? strlen(json) : 0;

    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             status, status == 200 ? "OK" : "Bad Request", body_len);

    /* 输出响应基本信息和body内容（总是输出） */
    LOG("=== HTTP Response ===");
    LOG("Status: %d", status);
    if (body_len > 0) {
        LOG("Body: %s", json);
    }
    LOG("=====================");

    send(fd, header, strlen(header), 0);
    if (body_len > 0)
    {
        send(fd, json, body_len, 0);
    }
}

/**
 * 发送 SSE 流式响应
 * 格式: event: message\ndata: <json>\n\n
 */
static void http_send_sse(int fd, const char *json)
{
    char header[512];
    char sse_data[1024];

    /* SSE 响应头 - 使用 \r\n 作为行结束符 */
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/event-stream\r\n"
             "Cache-Control: no-cache\r\n"
             "Connection: keep-alive\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n");

    /* 构建 SSE 数据格式: event: message\ndata: <json>\n\n */
    snprintf(sse_data, sizeof(sse_data), "event: message\ndata: %s\r\n\r\n", json);

    /* 输出 SSE 响应基本信息和body内容（总是输出） */
    LOG("=== SSE Response ===");
    LOG("Event: message");
    LOG("Data: %s", json);
    LOG("====================");

    send(fd, header, strlen(header), 0);
    send(fd, sse_data, strlen(sse_data), 0);
}

/**
 * 发送 SSE 事件（不带响应头，用于后续事件）
 * 格式: event: message\ndata: <json>\r\n\r\n
 */
static void http_send_sse_event(int fd, const char *json)
{
    char sse_data[1024];

    /* 构建 SSE 数据格式: event: message\ndata: <json>\r\n\r\n */
    snprintf(sse_data, sizeof(sse_data), "event: message\ndata: %s\r\n\r\n", json);

    /* 输出 SSE 事件基本信息和body内容（总是输出） */
    LOG("=== SSE Event ===");
    LOG("Event: message");
    LOG("Data: %s", json);
    LOG("=================");

    send(fd, sse_data, strlen(sse_data), 0);
}

/**
 * 处理客户端连接（支持流式 HTTP）
 */
static void handle_client(int client_fd)
{
    char buffer[BUFFER_SIZE] = {0};
    char response[BUFFER_SIZE] = {0};
    ssize_t bytes_read;
    fd_set read_fds;
    struct timeval timeout;
    bool use_sse = false;

    /* 设置超时 */
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    /* 循环处理多个请求（用于 SSE/流式 HTTP） */
    while (1)
    {
        /* 重置缓冲区 */
        memset(buffer, 0, sizeof(buffer));

        /* 读取请求 */
        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);

        timeout.tv_sec = 10;
        timeout.tv_usec = 0;

        int ret = select(client_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ret <= 0)
        {
            if (ret == 0) {
                printf("server: client read timeout\n");
            } else {
                printf("server: select failed: %s\n", strerror(errno));
            }
            break;
        }

        bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0)
        {
            printf("server: read failed: %s\n", strerror(errno));
            break;
        }

        buffer[bytes_read] = '\0';
        
        /* 解析HTTP请求 */
        char method[16] = {0};
        char path[256] = {0};
        const char *body = NULL;

        if (parse_http_request(buffer, method, sizeof(method), path, sizeof(path), &body, &use_sse) != 0)
        {
            printf("server: parse_http_request failed\n");
            break;
        }

        /* 打印请求详情（简化日志） */
        LOG("=== HTTP Request ===");
        LOG("Method: %s, Path: %s, UseSSE: %s", method, path, use_sse ? "true" : "false");
        
        /* 如果是 /mcp 请求，打印body内容（总是输出） */
        if (strcmp(method, "POST") == 0 && strcmp(path, "/mcp") == 0 && body) {
            LOG("Request for /mcp");
            LOG("Body: %s", body);
        }

        /* 检查是否是 /mcp 请求 */
        if (strcmp(method, "POST") == 0 && strcmp(path, "/mcp") == 0)
        {
            if (!body || body[0] == '\0')
            {
                LOG_ERROR("server: empty body");
                http_send_json(client_fd, 400, "{\"error\":\"empty body\"}");
                continue;
            }

            /* 解析JSON-RPC请求 */
            cJSON *root = cJSON_Parse(body);
            if (!root)
            {
                printf("server: JSON parse error: %s\n", cJSON_GetErrorPtr());
                build_error_response("null", -32700, "Parse error", response, sizeof(response));
                http_send_json(client_fd, 200, response);
                continue;
            }

            /* 获取id和method */
            cJSON *id = cJSON_GetObjectItem(root, "id");
            char id_json[40] = "null";
            if (id)
            {
                if (cJSON_IsString(id))
                {
                    snprintf(id_json, sizeof(id_json), "\"%s\"", id->valuestring);
                }
                else if (cJSON_IsNumber(id))
                {
                    snprintf(id_json, sizeof(id_json), "%d", id->valueint);
                }
            }

            cJSON *method_item = cJSON_GetObjectItem(root, "method");
            if (!method_item || !cJSON_IsString(method_item))
            {
                printf("server: method not found\n");
                build_error_response(id_json, -32600, "Invalid Request: method required", response, sizeof(response));
                http_send_json(client_fd, 200, response);
                cJSON_Delete(root);
                continue;
            }

            LOG_INFO("Request method=%s, id=%s", method_item->valuestring, id_json);

            /* 获取params */
            cJSON *params = cJSON_GetObjectItem(root, "params");

            /* 处理 initialize 请求：发送响应和 notifications/initialized 事件 */
            if (strcmp(method_item->valuestring, "initialize") == 0 && use_sse)
            {
                char event_buffer[BUFFER_SIZE] = {0};
                
                LOG_DEBUG("=== Initialize Request (SSE Mode) ===");
                
                /* 调用 MCP 处理 initialize 方法（带事件推送）*/
                int ret = mcp_handle_initialize_ex(id_json, response, sizeof(response),
                                                   event_buffer, sizeof(event_buffer));
                if (ret == 0)
                {
                    LOG_ERROR("server: mcp_handle_initialize_ex failed");
                    http_send_json(client_fd, 200, response);
                    cJSON_Delete(root);
                    break;
                }
                
                /* 发送 initialize 响应（SSE 格式） */
                http_send_sse(client_fd, response);
                
                /* 发送 notifications/initialized 事件 */
                if (event_buffer[0] != '\0')
                {
                    http_send_sse_event(client_fd, event_buffer);
                    LOG("Sent notifications/initialized event");
                }
                
                /* SSE 模式下保持连接不关闭 */
                LOG("SSE connection kept alive");
                
                cJSON_Delete(root);
                continue; /* 继续等待后续请求 */
            }
            
            /* 调用 MCP 处理方法 */
            if (mcp_handle_request(id_json, method_item->valuestring, params, response, sizeof(response)) == 0)
            {
                LOG("Request failed: %s", method_item->valuestring);
                http_send_json(client_fd, 200, response);
            }
            else
            {
                LOG("Sending response for: %s", method_item->valuestring);
                if (use_sse)
                {
                    http_send_sse(client_fd, response);
                }
                else
                {
                    http_send_json(client_fd, 200, response);
                }
            }

            cJSON_Delete(root);
            break; /* 发送响应后立即退出循环 */
        }
        else if (strcmp(method, "OPTIONS") == 0 && strcmp(path, "/mcp") == 0)
        {
            /* OPTIONS 预检请求 - 返回 CORS 允许头 */
            const char *resp =
                "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: content-type, Accept, mcp-protocol-version\r\n"
                "Access-Control-Max-Age: 86400\r\n"
                "Connection: keep-alive\r\n"
                "\r\n";
            send(client_fd, resp, strlen(resp), 0);
            LOG("server: OPTIONS request handled");
            break; /* 发送响应后立即退出循环 */
        }
        else
        {
            printf("server: not /mcp request\n");
            http_send_json(client_fd, 404, "{\"error\":\"not found\"}");
            break; /* 发送响应后立即退出循环 */
        }
    }

    close(client_fd);
    LOG("server: client connection closed");
}

/**
 * 启动服务器
 */
int server_start(void)
{
    g_server_fd = create_server(g_server_config.port);
    if (g_server_fd < 0)
    {
        return -1;
    }

    g_running = 1;
    printf("server: server started\n");

    while (g_running)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd;

        client_fd = accept(g_server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            printf("server: accept failed: %s\n", strerror(errno));
            continue;
        }

        printf("server: client connected from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        /* 处理客户端连接 */
        handle_client(client_fd);
    }

    return 0;
}

/**
 * 停止服务器
 */
void server_stop(void)
{
    g_running = 0;
    if (g_server_fd >= 0)
    {
        close(g_server_fd);
        g_server_fd = -1;
    }
    
    /* 清理 HTTP 客户端资源 */
    client_cleanup();
}
