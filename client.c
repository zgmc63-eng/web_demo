#include "client.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 响应数据结构 */
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} ResponseData;

/* CURL 初始化标志 */
static int g_curl_initialized = 0;

/**
 * CURL 写回调函数
 */
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    ResponseData *response = (ResponseData *)userp;
    
    if (response->size + total_size >= response->capacity) {
        size_t new_capacity = response->capacity * 2;
        if (new_capacity < response->size + total_size + 1) {
            new_capacity = response->size + total_size + 1;
        }
        char *new_data = (char *)realloc(response->data, new_capacity);
        if (!new_data) {
            return 0;
        }
        response->data = new_data;
        response->capacity = new_capacity;
    }
    
    memcpy(response->data + response->size, contents, total_size);
    response->size += total_size;
    response->data[response->size] = '\0';
    
    return total_size;
}

/**
 * 初始化 HTTP 客户端（libcurl）
 */
int client_init(void) {
    if (g_curl_initialized) {
        return 0;
    }
    
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        printf("[CLIENT] curl_global_init failed: %s\n", curl_easy_strerror(res));
        return -1;
    }
    
    g_curl_initialized = 1;
    return 0;
}

/**
 * 清理 HTTP 客户端资源
 */
void client_cleanup(void) {
    if (g_curl_initialized) {
        curl_global_cleanup();
        g_curl_initialized = 0;
    }
}

/**
 * 发送 HTTP POST 请求到 webcomm
 */
int client_post(const char *path, const char *body, char *response, size_t response_len) {
    CURL *curl = NULL;
    CURLcode res;
    int result = -1;
    
    if (!g_curl_initialized) {
        printf("[CLIENT] client_init not called\n");
        return -1;
    }
    
    if (!path || !response || response_len == 0) {
        printf("[CLIENT] invalid parameters\n");
        return -1;
    }
    
    /* 创建响应数据结构 */
    ResponseData resp_data = {NULL, 0, 0};
    resp_data.capacity = response_len;
    resp_data.data = (char *)malloc(response_len);
    if (!resp_data.data) {
        return -1;
    }
    resp_data.data[0] = '\0';
    
    /* 初始化 CURL */
    curl = curl_easy_init();
    if (!curl) {
        printf("[CLIENT] curl_easy_init failed\n");
        free(resp_data.data);
        return -1;
    }
    
    /* 设置 CURL 选项 */
    curl_easy_setopt(curl, CURLOPT_URL, path);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(body));
    }
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    
    /* 忽略 SSL 验证（用于 HTTPS） */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    /* 执行请求 */
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        printf("[CLIENT] curl_easy_perform failed: %s\n", curl_easy_strerror(res));
        result = -1;
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[CLIENT] HTTP response code: %ld\n", http_code);
        
        /* 复制响应数据 */
        if (resp_data.size > 0 && response) {
            size_t copy_size = resp_data.size < response_len ? resp_data.size : response_len - 1;
            memcpy(response, resp_data.data, copy_size);
            response[copy_size] = '\0';
            printf("[CLIENT] Response received, length: %zu\n", copy_size);
        }
        result = 0;
    }
    
    /* 清理 */
    curl_easy_cleanup(curl);
    free(resp_data.data);
    
    return result;
}

/**
 * 发送 JSON POST 请求到 webcomm
 */
int client_post_json(const char *path, const char *body, char *response, size_t response_len) {
    CURL *curl = NULL;
    CURLcode res;
    int result = -1;
    
    if (!g_curl_initialized) {
        printf("[CLIENT] client_init not called\n");
        return -1;
    }
    
    if (!path || !response || response_len == 0) {
        printf("[CLIENT] invalid parameters\n");
        return -1;
    }
    
    /* 创建响应数据结构 */
    ResponseData resp_data = {NULL, 0, 0};
    resp_data.capacity = response_len;
    resp_data.data = (char *)malloc(response_len);
    if (!resp_data.data) {
        return -1;
    }
    resp_data.data[0] = '\0';
    
    /* 初始化 CURL */
    curl = curl_easy_init();
    if (!curl) {
        printf("[CLIENT] curl_easy_init failed\n");
        free(resp_data.data);
        return -1;
    }
    
    /* 设置 CURL 选项 */
    curl_easy_setopt(curl, CURLOPT_URL, path);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(body));
    }
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    
    /* 忽略 SSL 验证（用于 HTTPS） */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    /* 设置请求头 */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    /* 执行请求 */
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        printf("[CLIENT] curl_easy_perform failed: %s\n", curl_easy_strerror(res));
        result = -1;
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        printf("[CLIENT] HTTP response code: %ld\n", http_code);
        
        /* 复制响应数据 */
        if (resp_data.size > 0 && response) {
            size_t copy_size = resp_data.size < response_len ? resp_data.size : response_len - 1;
            memcpy(response, resp_data.data, copy_size);
            response[copy_size] = '\0';
            printf("[CLIENT] Response received, length: %zu\n", copy_size);
        }
        result = 0;
    }
    
    /* 清理 */
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp_data.data);
    
    return result;
}