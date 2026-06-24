#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"

int main(int argc, char *argv[])
{
    int ret;
    
    printf("mcpcomm: starting...\n");
    
    /* 初始化服务器 */
    ret = server_init(argc, argv);
    if (ret != 0) {
        if (ret < 0) {
            /* --help 被调用，正常退出 */
            return 0;
        }
        printf("mcpcomm: server init failed\n");
        return -1;
    }
    
    /* 启动服务器 */
    ret = server_start();
    
    printf("mcpcomm: stopped\n");
    return ret;
}