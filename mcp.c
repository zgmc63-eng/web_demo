#include "mcp.h"
#include "cJSON.h"
#include "client.h"
#include "tool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/**
 * 构建 notifications/initialized 事件
 * 格式: event: message\ndata: <json>\n\n
 */
static int mcp_build_initialized_notification(char *buffer, size_t buffer_len)
{
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddStringToObject(root, "jsonrpc", MCP_JSONRPC_VERSION);
    cJSON_AddStringToObject(root, "method", "notifications/initialized");
    
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str)
    {
        snprintf(buffer, buffer_len, "%s", json_str);
        cJSON_free(json_str);
    }
    cJSON_Delete(root);
    
    return 1;
}

/**
 * 构建 initialize 响应
 */
static int mcp_build_initialize_response(int id, char *response, size_t response_len)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *result = cJSON_CreateObject();
    cJSON *capabilities = cJSON_CreateObject();
    cJSON *serverInfo = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "jsonrpc", MCP_JSONRPC_VERSION);
    cJSON_AddNumberToObject(root, "id", id);
    cJSON_AddItemToObject(root, "result", result);

    cJSON_AddStringToObject(result, "protocolVersion", MCP_PROTOCOL_VERSION);

    /* capabilities 字段应该是对象类型 */
    cJSON *tools = cJSON_CreateObject();

    /* 添加 capabilities 对象 */
    cJSON_AddItemToObject(capabilities, "tools", tools);
    // TODO: 支持 resources, prompts, elicitation, roots, tasks

    cJSON_AddItemToObject(result, "capabilities", capabilities);

    /* serverInfo */
    cJSON_AddStringToObject(serverInfo, "name", "c6-mcpcomm");
    cJSON_AddStringToObject(serverInfo, "version", "1.0.0");
    cJSON_AddItemToObject(result, "serverInfo", serverInfo);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str)
    {
        snprintf(response, response_len, "%s", json_str);
        cJSON_free(json_str);
    }
    cJSON_Delete(root);

    return 1;
}

/**
 * 构建错误响应
 */
int mcp_build_error_response(int id, int code, const char *message, char *response, size_t response_len)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *error = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "jsonrpc", MCP_JSONRPC_VERSION);
    if(id >= 0)
    {
        cJSON_AddNumberToObject(root, "id", id);
    }
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

    return 1;
}

/**
 * 构建 success 响应
 */
static int mcp_build_success_response(int id, cJSON *result_obj, char *response, size_t response_len)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "jsonrpc", MCP_JSONRPC_VERSION);
    cJSON_AddStringToObject(root, "id", id);
    if (result_obj)
    {
        cJSON_AddItemToObject(root, "result", result_obj);
    }
    else
    {
        cJSON_AddNullToObject(root, "result");
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str)
    {
        snprintf(response, response_len, "%s", json_str);
        cJSON_free(json_str);
    }
    cJSON_Delete(root);

    return 1;
}

/**
 * 处理 initialize 方法（支持 SSE 事件推送）
 * @param id 请求ID
 * @param response 响应缓冲区
 * @param response_len 响应缓冲区大小int
 * @param event_buffer 事件缓冲区（用于 SSE 事件）
 * @param event_buffer_len 事件缓冲区大小
 * @return 0 成功，-1 失败
 */
int mcp_handle_initialize_ex(int id, char *response, size_t response_len,
                              char *event_buffer, size_t event_buffer_len)
{
    int ret;
    
    /* 构建 initialize 响应 */
    ret = mcp_build_initialize_response(id, response, response_len);
    if (ret != 1)
    {
        return ret;
    }
    
    /* 构建 notifications/initialized 事件 */
    if (event_buffer && event_buffer_len > 0)
    {
        ret = mcp_build_initialized_notification(event_buffer, event_buffer_len);
    }
    
    return ret;
}

/**
 * 处理 initialize 方法（简化版本，不使用 SSE）
 */
static int mcp_handle_initialize(int id, char *response, size_t response_len)
{
    return mcp_handle_initialize_ex(id, response, response_len, NULL, 0);
}

/**
 * 处理 tools/list 方法
 */
static int mcp_handle_tools_list( int id, char *response, size_t response_len)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", MCP_JSONRPC_VERSION);

    cJSON_AddNumberToObject(root, "id", id);
    cJSON *result = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();
    cJSON_AddItemToObject(result, "tools", tools);

    /* 使用工具注册表动态生成工具列表 */
    for (int i = 0; i < tool_get_count(); i++)
    {
        char name[64];
        char description[256];
        cJSON *inputSchema = NULL;

        if (tool_get_name(i, name, sizeof(name)) != 0)
            continue;
        if (tool_get_description(i, description, sizeof(description)) != 0)
            continue;
        inputSchema = tool_get_input_schema(i);
        if (!inputSchema)
            continue;

        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", name);
        cJSON_AddStringToObject(tool, "description", description);
        cJSON_AddItemToObject(tool, "inputSchema", cJSON_Duplicate(inputSchema, 1));
        cJSON_AddItemToArray(tools, tool);

        cJSON_Delete(inputSchema);
    }

    cJSON_AddItemToObject(root, "result", result);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str)
    {
        snprintf(response, response_len, "%s", json_str);
        free(json_str);
    }
    cJSON_Delete(root);

    return 1;
}

/**
 * 处理 tools/call 方法
 */
static int mcp_handle_tools_call(int id, cJSON *params, char *response, size_t response_len)
{
    cJSON *result = NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", MCP_JSONRPC_VERSION);
    cJSON_AddNumberToObject(root, "id", id);

    cJSON *tool_name = cJSON_GetObjectItem(params, "name");
    if (!tool_name)
    {
        tool_name = cJSON_GetObjectItem(params, "tool_name");
    }

    if (!tool_name || !cJSON_IsString(tool_name))
    {
        result = cJSON_CreateObject();
        cJSON_AddNumberToObject(result, "code", -32602);
        cJSON_AddStringToObject(result, "message", "Invalid params: tool_name required");
        cJSON_AddItemToObject(root, "error", result);
    }
    else
    {
        printf("mcp: tools/call tool_name=%s\n", tool_name->valuestring);
         /* 调用统一的工具调度函数 */
        int index = tool_find_index(tool_name->valuestring);
        if (index < 0)
        {
            result = cJSON_CreateObject();
            cJSON_AddNumberToObject(result, "code", -32602);
            cJSON_AddStringToObject(result, "message", "Tool not found");
            cJSON_AddItemToObject(root, "error", result);
        }
        else
        {
            result = cJSON_CreateObject();
            int ret = tool_call(index, params, result);
            if (ret == 0)
            {
                //失败
                cJSON_AddNumberToObject(result, "code", -32603);
                cJSON_AddStringToObject(result, "message", "Service error");
                cJSON_AddItemToObject(root, "error", result);
            }
            else
            {
                cJSON_AddItemToObject(root, "result", result);
            }
        }
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str)
    {
        snprintf(response, response_len, "%s", json_str);
        free(json_str);
    }
    cJSON_Delete(root);
    return 1;
}

/**
 * 处理 MCP 请求
 * 返回 0 表示成功，-1 表示失败
 */
int mcp_handle_request(int id, const char *method, cJSON *params, char *response, size_t response_len)
{
    /* 对于 tools/call 方法，使用工具模块处理 */
    if (strcmp(method, "tools/call") == 0 && params)
    {
        return mcp_handle_tools_call(id, params, response, response_len);
    }
    else if (strcmp(method, "initialize") == 0)
    {
        return mcp_handle_initialize(id, response, response_len);
    }
    else if (strcmp(method, "tools/list") == 0)
    {
        return mcp_handle_tools_list(id, response, response_len);
    }
    else
    {
        return mcp_build_error_response(id, -32601, "Method not found", response, response_len);
    }
}