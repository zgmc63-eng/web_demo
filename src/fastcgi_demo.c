#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define FCGI_VERSION_1 1
#define FCGI_BEGIN_REQUEST 1
#define FCGI_ABORT_REQUEST 2
#define FCGI_END_REQUEST 3
#define FCGI_PARAMS 4
#define FCGI_STDIN 5
#define FCGI_STDOUT 6
#define FCGI_STDERR 7

#define FCGI_RESPONDER 1
#define FCGI_REQUEST_COMPLETE 0

#define MAX_CONTENT 65535
#define MAX_BODY 65536
#define MAX_PARAMS 64
#define MAX_PARAM_KEY 64
#define MAX_PARAM_VALUE 512

typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t request_id;
    uint16_t content_length;
    uint8_t padding_length;
    uint8_t reserved;
} FcgiHeader;

typedef struct {
    char key[MAX_PARAM_KEY];
    char value[MAX_PARAM_VALUE];
} Param;

typedef struct {
    uint16_t id;
    int begun;
    int params_done;
    int stdin_done;
    Param params[MAX_PARAMS];
    size_t param_count;
    char body[MAX_BODY];
    size_t body_len;
} Request;

static volatile sig_atomic_t running = 1;

static void stop_running(int signo) {
    (void)signo;
    running = 0;
}

static int read_exact(int fd, void *buf, size_t len) {
    char *out = buf;
    while (len > 0) {
        ssize_t n = read(fd, out, len);
        if (n == 0) {
            return 0;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        out += n;
        len -= (size_t)n;
    }
    return 1;
}

static int write_exact(int fd, const void *buf, size_t len) {
    const char *in = buf;
    while (len > 0) {
        ssize_t n = write(fd, in, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        in += n;
        len -= (size_t)n;
    }
    return 0;
}

static int read_header(int fd, FcgiHeader *header) {
    uint8_t raw[8];
    int rc = read_exact(fd, raw, sizeof(raw));
    if (rc <= 0) {
        return rc;
    }

    header->version = raw[0];
    header->type = raw[1];
    header->request_id = (uint16_t)((raw[2] << 8) | raw[3]);
    header->content_length = (uint16_t)((raw[4] << 8) | raw[5]);
    header->padding_length = raw[6];
    header->reserved = raw[7];
    return 1;
}

static int write_record(int fd, uint8_t type, uint16_t request_id, const void *content, uint16_t len) {
    uint8_t header[8] = {
        FCGI_VERSION_1,
        type,
        (uint8_t)(request_id >> 8),
        (uint8_t)(request_id & 0xff),
        (uint8_t)(len >> 8),
        (uint8_t)(len & 0xff),
        0,
        0,
    };

    if (write_exact(fd, header, sizeof(header)) < 0) {
        return -1;
    }
    if (len > 0 && write_exact(fd, content, len) < 0) {
        return -1;
    }
    return 0;
}

static uint32_t read_name_value_length(const uint8_t *buf, size_t len, size_t *offset) {
    if (*offset >= len) {
        return UINT32_MAX;
    }

    uint8_t first = buf[(*offset)++];
    if ((first & 0x80) == 0) {
        return first;
    }

    if (*offset + 3 > len) {
        return UINT32_MAX;
    }

    uint32_t value = ((uint32_t)(first & 0x7f) << 24) |
                     ((uint32_t)buf[*offset] << 16) |
                     ((uint32_t)buf[*offset + 1] << 8) |
                     (uint32_t)buf[*offset + 2];
    *offset += 3;
    return value;
}

static void parse_params(Request *request, const uint8_t *buf, size_t len) {
    size_t offset = 0;
    while (offset < len && request->param_count < MAX_PARAMS) {
        uint32_t name_len = read_name_value_length(buf, len, &offset);
        uint32_t value_len = read_name_value_length(buf, len, &offset);
        if (name_len == UINT32_MAX || value_len == UINT32_MAX || offset + name_len + value_len > len) {
            return;
        }

        Param *param = &request->params[request->param_count++];
        size_t key_len = name_len < MAX_PARAM_KEY - 1 ? name_len : MAX_PARAM_KEY - 1;
        size_t val_len = value_len < MAX_PARAM_VALUE - 1 ? value_len : MAX_PARAM_VALUE - 1;
        memcpy(param->key, buf + offset, key_len);
        param->key[key_len] = '\0';
        offset += name_len;
        memcpy(param->value, buf + offset, val_len);
        param->value[val_len] = '\0';
        offset += value_len;
    }
}

static const char *param_value(const Request *request, const char *key) {
    for (size_t i = 0; i < request->param_count; i++) {
        if (strcmp(request->params[i].key, key) == 0) {
            return request->params[i].value;
        }
    }
    return "";
}

static int send_response(int fd, const Request *request) {
    char content[4096];
    int n = snprintf(content, sizeof(content),
                     "Status: 200 OK\r\n"
                     "Content-Type: text/plain; charset=utf-8\r\n"
                     "\r\n"
                     "Hello from a tiny C FastCGI responder.\n\n"
                     "request_id=%u\n"
                     "method=%s\n"
                     "uri=%s\n"
                     "query=%s\n"
                     "body_bytes=%zu\n"
                     "body=%.*s\n",
                     request->id,
                     param_value(request, "REQUEST_METHOD"),
                     param_value(request, "REQUEST_URI"),
                     param_value(request, "QUERY_STRING"),
                     request->body_len,
                     (int)request->body_len,
                     request->body);
    if (n < 0) {
        return -1;
    }

    size_t total = (size_t)n < sizeof(content) ? (size_t)n : sizeof(content) - 1;
    size_t sent = 0;
    while (sent < total) {
        size_t chunk = total - sent;
        if (chunk > MAX_CONTENT) {
            chunk = MAX_CONTENT;
        }
        if (write_record(fd, FCGI_STDOUT, request->id, content + sent, (uint16_t)chunk) < 0) {
            return -1;
        }
        sent += chunk;
    }

    uint8_t end_body[8] = {0, 0, 0, 0, FCGI_REQUEST_COMPLETE, 0, 0, 0};
    return write_record(fd, FCGI_STDOUT, request->id, NULL, 0) == 0 &&
           write_record(fd, FCGI_END_REQUEST, request->id, end_body, sizeof(end_body)) == 0
               ? 0
               : -1;
}

static int handle_connection(int client_fd) {
    Request request;
    memset(&request, 0, sizeof(request));

    while (running) {
        FcgiHeader header;
        int rc = read_header(client_fd, &header);
        if (rc <= 0) {
            return rc;
        }
        if (header.version != FCGI_VERSION_1) {
            return -1;
        }

        uint8_t content[MAX_CONTENT];
        if (header.content_length > 0 && read_exact(client_fd, content, header.content_length) <= 0) {
            return -1;
        }
        if (header.padding_length > 0) {
            uint8_t padding[255];
            if (read_exact(client_fd, padding, header.padding_length) <= 0) {
                return -1;
            }
        }

        if (header.type == FCGI_BEGIN_REQUEST) {
            if (header.content_length < 3) {
                return -1;
            }
            memset(&request, 0, sizeof(request));
            request.id = header.request_id;
            uint16_t role = (uint16_t)((content[0] << 8) | content[1]);
            request.begun = role == FCGI_RESPONDER;
        } else if (header.type == FCGI_PARAMS && request.begun) {
            if (header.content_length == 0) {
                request.params_done = 1;
            } else {
                parse_params(&request, content, header.content_length);
            }
        } else if (header.type == FCGI_STDIN && request.begun) {
            if (header.content_length == 0) {
                request.stdin_done = 1;
            } else {
                size_t remaining = MAX_BODY - request.body_len - 1;
                size_t copy_len = header.content_length < remaining ? header.content_length : remaining;
                memcpy(request.body + request.body_len, content, copy_len);
                request.body_len += copy_len;
                request.body[request.body_len] = '\0';
            }
        } else if (header.type == FCGI_ABORT_REQUEST) {
            return 0;
        }

        if (request.begun && request.params_done && request.stdin_done) {
            return send_response(client_fd, &request);
        }
    }

    return 0;
}

static int listen_unix_socket(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "socket path is too long: %s\n", path);
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    chmod(path, 0666);

    if (listen(fd, 16) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

int main(int argc, char **argv) {
    const char *socket_path = argc > 1 ? argv[1] : "/tmp/fastcgi_demo.sock";
    signal(SIGINT, stop_running);
    signal(SIGTERM, stop_running);

    int server_fd = listen_unix_socket(socket_path);
    if (server_fd < 0) {
        return EXIT_FAILURE;
    }

    printf("FastCGI demo listening on unix:%s\n", socket_path);
    fflush(stdout);

    while (running) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }

        if (handle_connection(client_fd) < 0) {
            fprintf(stderr, "failed to handle FastCGI connection\n");
        }
        close(client_fd);
    }

    close(server_fd);
    unlink(socket_path);
    return EXIT_SUCCESS;
}
