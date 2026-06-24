#ifndef CLIENT_H
#define CLIENT_H

#include <stddef.h>

/**
 * 初始化 HTTP 客户端（libcurl）
 * @return 0 成功，-1 失败
 */
int client_init(void);

/**
 * 清理 HTTP 客户端资源
 */
void client_cleanup(void);

/**
 * 发送 HTTP POST 请求到 webcomm
 * @param path 请求路径
 * @param body 请求体（JSON 格式）
 * @param response 响应缓冲区
 * @param response_len 响应缓冲区大小
 * @return 0 成功，-1 失败
 */
int client_post(const char *path, const char *body, char *response, size_t response_len);

/**
 * 发送 JSON POST 请求到 webcomm（自动设置 Content-Type）
 * @param path 请求路径
 * @param body JSON 字符串
 * @param response 响应缓冲区
 * @param response_len 响应缓冲区大小
 * @return 0 成功，-1 失败
 */
int client_post_json(const char *path, const char *body, char *response, size_t response_len);

#endif /* CLIENT_H */