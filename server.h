#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include <stdbool.h>

/* 全局配置 */
typedef struct {
    int port;           /* 服务器端口 */
    bool debug_log;     /* 调试日志开关 */
    bool verbose;       /* 详细日志开关 */
} server_config_t;

/* 外部配置变量 */
extern server_config_t g_server_config;

/**
 * 初始化服务器
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 0 成功，非0 失败
 */
int server_init(int argc, char *argv[]);
void server_stop_signal(int signum);

/**
 * 启动服务器
 * @return 0 成功，非0 失败
 */
int server_start(void);

/**
 * 停止服务器
 */
void server_stop(void);

/**
 * 构建错误响应
 */
int build_error_response(int id, int code, const char *message, char *response, size_t response_len);

#endif /* SERVER_H */