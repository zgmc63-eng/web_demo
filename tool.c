#include "tool.h"
#include "mcp.h"
#include "cJSON.h"
#include "client.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
/**
 * 工具信息结构体
 */
typedef struct
{
    const char *name;                                                                        // 工具名称
    const char *description;                                                                 // 工具描述
    const FieldDef *fields;                                                                  // 字段定义表
    int (*handler)(cJSON *params, cJSON *paramss); // 处理函数
} ToolInfo;

static ToolInfo g_tools[];
/**
 * 向 webcomm 发送 MCP 请求（封装 HTTP 请求逻辑）
 * 直接使用原始的 params 进行转发，不做任何修改
 * @param tool_name 工具名称
 * @param mcp_params MCP 格式的参数（包含 arguments 对象，可为 NULL）
 * @param response webcomm 响应缓冲区
 * @param response_len 响应缓冲区大小
 * @return 0 成功，-1 失败
 */
static int send_webcomm_request(const char *tool_name, cJSON *mcp_params, char *response, size_t response_len)
{
    char webcomm_path[256];
    char webcomm_response[8192];
    char *request_json = NULL;

    /* 构建 webcomm 的 API 路径 */
    snprintf(webcomm_path, sizeof(webcomm_path), "https://127.0.0.1/web/api/v1/mcp");

    printf("[TOOL] Calling %s via %s\n", tool_name, webcomm_path);

    /* 直接使用原始的 params 作为请求体 */
    if (mcp_params)
    {
        request_json = cJSON_PrintUnformatted(mcp_params);
        if (!request_json)
        {
            return 0;
        }
    }
    else
    {
        /* 如果没有 params，返回错误 */
        printf("[TOOL] No params provided\n");
        return 0;
    }

    printf("[TOOL] Request JSON: %s\n", request_json);

    /* 发送 POST 请求到 webcomm */
    int post_ret = client_post_json(webcomm_path, request_json, webcomm_response, sizeof(webcomm_response));
    free(request_json);

    if (post_ret != 0)
    {
        printf("[TOOL] Request failed\n");
        return 0;
    }

    /* 复制原始响应 */
    size_t copy_len = strlen(webcomm_response);
    if (copy_len >= response_len)
    {
        copy_len = response_len - 1;
    }
    memcpy(response, webcomm_response, copy_len);
    response[copy_len] = '\0';

    printf("[TOOL] Raw Response received:\n%s\n", response);

    return 1;
}


/**
 * 构建业务错误响应（isError=true）
 * @param message 错误消息
 * @param response 响应缓冲区
 * @param response_len 响应缓冲区大小
 * @return 0 成功，-1 失败
 */
static int tool_build_error_response(const char *message, cJSON *mcp_result)
{
    cJSON *content_array = NULL;

    if (!mcp_result)
    {
        printf("[TOOL] Failed to create MCP result\n");
        return 0;
    }

    /* 构建 content 数组，包含错误消息 */
    content_array = cJSON_CreateArray();
    if (!content_array)
    {
        printf("[TOOL] Failed to create content array\n");
        return 0;
    }

    cJSON *text_item = cJSON_CreateObject();
    if (text_item)
    {
        cJSON_AddStringToObject(text_item, "type", "text");
        cJSON_AddStringToObject(text_item, "text", message ? message : "Unknown error");
        cJSON_AddItemToArray(content_array, text_item);
    }

    cJSON_AddItemToObject(mcp_result, "content", content_array);
    cJSON_AddBoolToObject(mcp_result, "isError", true);

    return 1;
}

/**
 * 根据字段定义表构建 JSON Schema（直接在 while 中计算数量）
 */
static cJSON *build_object_schema_from_defs(const FieldDef *fields)
{
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    // 构建 required 数组
    cJSON *required = cJSON_CreateArray();
    cJSON *properties = cJSON_CreateObject();

    // 直接在 while 中计算数量
    int count = 0;
    while (fields[count].name != NULL)
    {
        const FieldDef *field = &fields[count];

        // 添加到 required 数组（如果必填）
        if (field->required)
        {
            cJSON_AddItemToArray(required, cJSON_CreateString(field->name));
        }

        // 构建字段的 schema
        cJSON *field_schema = cJSON_CreateObject();
        switch (field->type)
        {
        case SCHEMA_TYPE_STRING:
            cJSON_AddStringToObject(field_schema, "type", "string");
            break;
        case SCHEMA_TYPE_INTEGER:
            cJSON_AddStringToObject(field_schema, "type", "integer");
            break;
        case SCHEMA_TYPE_NUMBER:
            cJSON_AddStringToObject(field_schema, "type", "number");
            break;
        case SCHEMA_TYPE_BOOLEAN:
            cJSON_AddStringToObject(field_schema, "type", "boolean");
            break;
        case SCHEMA_TYPE_ARRAY:
            cJSON_AddStringToObject(field_schema, "type", "array");
            break;
        case SCHEMA_TYPE_OBJECT:
            cJSON_AddStringToObject(field_schema, "type", "object");
            break;
        }

        // 添加描述（如果有的话）
        if (field->description)
        {
            cJSON_AddStringToObject(field_schema, "description", field->description);
        }

        cJSON_AddItemToObject(properties, field->name, field_schema);
        count++;
    }

    cJSON_AddItemToObject(schema, "properties", properties);
    cJSON_AddItemToObject(schema, "required", required);

    return schema;
}

/**
 * 无参数工具的通用 inputSchema
 */
static cJSON *tool_no_params_get_input_schema(const FieldDef *fields)
{
    (void)fields; // 避免未使用警告

    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    cJSON *properties = cJSON_CreateObject();
    cJSON_AddItemToObject(schema, "properties", properties);

    return schema;
}


static int tool_handle_webcomm_response(cJSON *webcomm_root, cJSON *mcp_result)
{
    if(mcp_result == NULL)
    {
        return 0; /* 不构建响应 */
    }
    cJSON *code = cJSON_GetObjectItem(webcomm_root, "code");
    cJSON *message = cJSON_GetObjectItem(webcomm_root, "message");

    /* 如果 code 字段不存在或不是数字，视为业务错误 */
    if (!code || !cJSON_IsNumber(code))
    {
        printf("[TOOL] Invalid code field in response\n");
        tool_build_error_response(message ? message->valuestring : "Invalid response", mcp_result);
        return 2; /* 构建了错误响应 */
    }

    /* code = 0 表示成功 */
    if (code->valuedouble == 0)
    {
        /* 检查 data 是否为空对象 */
        cJSON *data = cJSON_GetObjectItem(webcomm_root, "data");
        if (data && cJSON_IsObject(data) && cJSON_GetArraySize(data) == 0)
        {
            printf("[TOOL] Success with empty data, return 'success' text\n");
            /* 构建空成功响应，content 包含 "success" */
            cJSON *content_array = cJSON_CreateArray();
            if (content_array)
            {
                cJSON *text_item = cJSON_CreateObject();
                if (text_item)
                {
                    cJSON_AddStringToObject(text_item, "type", "text");
                    cJSON_AddStringToObject(text_item, "text", "success");
                    cJSON_AddItemToArray(content_array, text_item);
                }
                cJSON_AddItemToObject(mcp_result, "content", content_array);
                cJSON_AddBoolToObject(mcp_result, "isError", false);
            }
            return 1; /* 构建了空成功响应 */            
        }
        return 0; /* 继续处理成功响应 */
    }

    /* code = 25 或 26 表示参数错误（协议层错误） */
    if (code->valuedouble == 25 || code->valuedouble == 26)
    {
        printf("[TOOL] Parameter error, code=%f\n", code->valuedouble);
        /* 构建协议层错误响应 */
        cJSON_AddNumberToObject(mcp_result, "code", -32602); /* JSON-RPC 参数错误 */
        cJSON_AddStringToObject(mcp_result, "message", message ? message->valuestring : "Invalid params");
        cJSON_AddNullToObject(mcp_result, "data");
        return 2; /* 构建了协议层错误响应 */
    }

    /* 其他 code 值为业务错误 */
    printf("[TOOL] Business error, code=%f\n", code->valuedouble);
    tool_build_error_response( message ? message->valuestring : "Business error", mcp_result);
    return 2; /* 构建了业务错误响应 */
}


static int get_notice_content(cJSON *content_array, cJSON *webcomm_root)
{
    cJSON *data = cJSON_GetObjectItem(webcomm_root, "data");
    if (!data || !cJSON_IsObject(data))
    {
        printf("[TOOL] No data object in response\n");
        return 0;
    }

    cJSON *notices = cJSON_GetObjectItem(data, "notices");

    if (notices && cJSON_IsArray(notices))
    {
        for (int i = 0; i < cJSON_GetArraySize(notices); i++)
        {
            cJSON *notice = cJSON_GetArrayItem(notices, i);
            if (notice && cJSON_IsObject(notice))
            {
                cJSON *notice_id = cJSON_GetObjectItem(notice, "notice_id");
                cJSON *notice_time = cJSON_GetObjectItem(notice, "notice_time");
                cJSON *notice_content = cJSON_GetObjectItem(notice, "notice_content");

                if (notice_time && cJSON_IsString(notice_time) &&
                    notice_content && cJSON_IsString(notice_content))
                {
                    char text_buf[1024];
                    snprintf(text_buf, sizeof(text_buf), "布告ID:%d, 时间:%s, 内容:%s",
                             notice_id->valueint, notice_time->valuestring, notice_content->valuestring);

                    cJSON *text_item = cJSON_CreateObject();
                    cJSON_AddStringToObject(text_item, "type", "text");
                    cJSON_AddStringToObject(text_item, "text", text_buf);
                    cJSON_AddItemToArray(content_array, text_item);
                }
            }
        }
    }

    return 1;
}

/**
 * 通用的 webcomm 响应处理函数
 * @param index 工具索引
 * @param params 参数
 * @param response 响应缓冲区
 * @param response_len 响应缓冲区大小
 * @return 0 表示失败，非 0 表示成功
 */
static int handle_webcomm_response_with_content(int index, cJSON *params, cJSON *mcp_result)
{
    char webcomm_response[8192];
    cJSON *webcomm_root = NULL;
    cJSON *content_array = NULL;
    int ret = 0;

    if(mcp_result == NULL)
    {
        return 0; /* 不构建响应 */
    }

    /* 调用统一的 webcomm 请求函数 */
    ret = send_webcomm_request(g_tools[index].name, params, webcomm_response, sizeof(webcomm_response));
    if (ret == 0)
    {
        printf("[TOOL] send_webcomm_request failed\n");
        return 0;
    }

    /* 解析 webcomm 的响应 */
    webcomm_root = cJSON_Parse(webcomm_response);
    if (!webcomm_root)
    {
        printf("[TOOL] Failed to parse webcomm response\n");
        return 0;
    }

    /* 处理 webcomm 响应的 code 字段 */
    ret = tool_handle_webcomm_response(webcomm_root, mcp_result);
    if (ret != 0)
    {
        /* 1 表示已构建响应（包括空成功和错误），直接返回 0 表示成功 */
        cJSON_Delete(webcomm_root);
        return ret;
    }

    if (g_tools[index].handler == NULL)
    {
        cJSON_Delete(webcomm_root);
        return 0;
    }
    else
    {        
        /* 构建 content 数组 */
        content_array = cJSON_CreateArray();
        if (!content_array)
        {
            printf("[TOOL] Failed to create content array\n");
            cJSON_Delete(webcomm_root);
            return 0;
        }
        /* 调用工具的处理函数来构建 content */
        ret = g_tools[index].handler(content_array, webcomm_root);
        
        /* 注意：如果 handler 返回 0，表示需要继续处理 content */
        if (ret == 1 && content_array)
        {
            cJSON_AddItemToObject(mcp_result, "content", content_array);
            cJSON_AddBoolToObject(mcp_result, "isError", false);
        }
    }
    /* 注意：content_array 的所有权已经转移到 mcp_root，不需要再删除 */
    cJSON_Delete(webcomm_root);
    
    return ret;
}
    
// static int create_overview_dashboard_notice_handler(cJSON *params, char *response, size_t response_len)
// {

/**
 * 字段定义表
 */

// update_overview_dashboard_notice 工具
static const FieldDef update_notice_fields[] = {
    {"notice_id",       SCHEMA_TYPE_INTEGER,    "布告信息的编号",   true},
    {"notice_content",  SCHEMA_TYPE_STRING,     "布告信息的内容",   true},
    {NULL, 0, NULL, false} // 结束标记
};

static const FieldDef create_notice_fields[] = {
    {"notice_content", SCHEMA_TYPE_STRING, "布告信息的内容", true},
    {NULL, 0, NULL, false} // 结束标记
};

static const FieldDef delete_notice_fields[] = {
    {"notice_id",   SCHEMA_TYPE_INTEGER,  "布告信息的编号", true},
    {NULL, 0, NULL, false} // 结束标记
};

static ToolInfo g_tools[] = {
    {"get_overview_dashboard_notice",       "获取设备所有布告信息",         NULL,                       get_notice_content},
    {"update_overview_dashboard_notice",    "更新设备指定布告信息",         update_notice_fields,       NULL},
    {"create_overview_dashboard_notice",    "创建设备一条布告信息",         create_notice_fields,       NULL},
    {"delete_overview_dashboard_notice",    "删除设备指定布告信息",         delete_notice_fields,       NULL},
};

/**
 * 工具数量
 */
int tool_get_count(void)
{
    return sizeof(g_tools) / sizeof(g_tools[0]);
}

/**
 * 根据工具名称获取索引
 */
int tool_find_index(const char *tool_name)
{
    for (int i = 0; i < tool_get_count(); i++)
    {
        if (strcmp(g_tools[i].name, tool_name) == 0)
        {
            return i;
        }
    }
    return -1;
}

/**
 * 根据索引获取工具名称
 */
int tool_get_name(int index, char *name, size_t name_len)
{
    if (index < 0 || index >= tool_get_count())
    {
        return -1;
    }
    
    strncpy(name, g_tools[index].name, name_len - 1);
    name[name_len - 1] = '\0';
    
    return 0;
}

/**
 * 根据索引获取工具描述
 */
int tool_get_description(int index, char *description, size_t description_len)
{
    if (index < 0 || index >= tool_get_count())
    {
        return -1;
    }
    
    strncpy(description, g_tools[index].description, description_len - 1);
    description[description_len - 1] = '\0';
    
    return 0;
}

/**
 * 根据索引获取工具 inputSchema
 */
cJSON *tool_get_input_schema(int index)
{
    if (index < 0 || index >= tool_get_count())
    {
        return NULL;
    }
    
    const ToolInfo *info = &g_tools[index];
    
    // 检查字段数组是否为空（NULL 结束标记为第一个元素）
    if (info->fields == NULL || info->fields[0].name == NULL)
    {
        return tool_no_params_get_input_schema(info->fields);
    }

    // 根据字段定义构建 schema
    return build_object_schema_from_defs(info->fields);
}

/**
 * 调用工具处理函数
 */
int tool_call(int index, cJSON *params, cJSON *mcp_result)
{
    int ret;
    
    if (index < 0 || index >= tool_get_count())
    {
        return 0; // 失败
    }
    ret = handle_webcomm_response_with_content(index, params, mcp_result);
    // ret = g_tools[index].handler(id_json, params, response, response_len);
    return ret;
}