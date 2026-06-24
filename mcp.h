#ifndef MCP_H
#define MCP_H

#include "cJSON.h"
#include <stddef.h>

/* MCP 协议版本 */
#define MCP_JSONRPC_VERSION "2.0"

/* MCP 协议版本号 */
#define MCP_PROTOCOL_VERSION "2025-11-25"

/**
 * 处理 initialize 方法（支持 SSE 事件推送）
 * @param id_json 请求ID
 * @param response 响应缓冲区
 * @param response_len 响应缓冲区大小
 * @param event_buffer 事件缓冲区（用于 SSE 事件）
 * @param event_buffer_len 事件缓冲区大小
 * @return 0 成功，-1 失败
 */
int mcp_handle_initialize_ex(int id, char *response, size_t response_len,
                              char *event_buffer, size_t event_buffer_len);

/**
 * 构建错误响应
 */
int mcp_build_error_response(int id, int code, const char *message, char *response, size_t response_len);

/**
 * 处理 MCP 请求
 */
int mcp_handle_request(int id, const char *method, cJSON *params, char *response, size_t response_len);

#endif /* MCP_H */