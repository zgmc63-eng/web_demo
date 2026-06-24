#ifndef TOOL_H
#define TOOL_H

#include "cJSON.h"
#include <stdbool.h>

/**
 * 字段类型定义
 */
typedef enum {
    SCHEMA_TYPE_STRING,
    SCHEMA_TYPE_INTEGER,
    SCHEMA_TYPE_NUMBER,
    SCHEMA_TYPE_BOOLEAN,
    SCHEMA_TYPE_ARRAY,
    SCHEMA_TYPE_OBJECT
} SchemaType;

/**
 * 字段定义结构体
 */
typedef struct {
    const char *name;           // 字段名
    SchemaType type;            // 字段类型
    const char *description;    // 字段描述（可选）
    bool required;              // 是否必填
} FieldDef;

/**
 * 工具数量
 */
int tool_get_count(void);

/**
 * 根据工具名称获取索引
 * @param tool_name 工具名称
 * @return 索引，-1 表示未找到
 */
int tool_find_index(const char *tool_name);

/**
 * 根据索引获取工具名称
 * @param index 索引
 * @param name 缓冲区
 * @param name_len 缓冲区大小
 * @return 0 成功，-1 失败
 */
int tool_get_name(int index, char *name, size_t name_len);

/**
 * 根据索引获取工具描述
 * @param index 索引
 * @param description 缓冲区
 * @param description_len 缓冲区大小
 * @return 0 成功，-1 失败
 */
int tool_get_description(int index, char *description, size_t description_len);

/**
 * 根据索引获取工具 inputSchema
 * @param index 索引
 * @return cJSON 对象，需要调用 cJSON_Delete 释放
 */
cJSON *tool_get_input_schema(int index);

/**
 * 调用工具处理函数
 * @param index 索引
 * @param id_json 请求ID
 * @param params 请求参数
 * @param response 响应缓冲区
 * @param response_len 响应缓冲区大小
 * @return 0 成功，-1 失败
 */
int tool_call(int index, cJSON *params, cJSON *mcp_result);

/**
 * 工具处理函数类型定义
 */
typedef int (*ToolHandler)(const char *id_json, cJSON *params, char *response, size_t response_len);

#endif /* TOOL_H */