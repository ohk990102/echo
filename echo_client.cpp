#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "echo.h"

#define ASSERT(cond, msg)\
if(!(cond)) {\
    fprintf(stderr, "ASSERT FAILED [%s:%d]: %s\n", __FILE__, __LINE__, (msg));\
    exit(-1);\
}

#ifdef DEBUG
#define DASSERT(cond, msg)\
if(!(cond)) {\
    fprintf(stderr, "DASSERT FAILED [%s:%d]: %s\n", __FILE__, __LINE__, (msg));\
    exit(-1);\
}

#define DEBUG_PRINT(fmt, ...) printf("DEBUG PRINT [%s:%d]: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define DASSERT(...) {}
#define DEBUG_PRINT(...) {}
#endif


bool readn (int fd, void *buf, size_t size) {
    size_t size_read = 0;
    while (size_read < size) {
        ssize_t tmp = read(fd, (void *)((uint8_t *)buf + size_read), size - size_read);
        if (tmp == -1)
            return false;
        size_read += tmp;
    }
    return true;
}

bool sendn(int fd,void *buf, size_t size) {
    size_t size_send = 0;
    while (size_send < size) {
        ssize_t tmp = write(fd, (void *)((uint8_t *)buf + size_send), size - size_send);
        if (tmp == -1)
            return false;
        size_send += tmp;
    }
    return true;
}

void *worker_function(void *_data) {
    int client_fd = *((int *)_data);
    void *buf = malloc(sizeof(MAX_BUF_SIZE));
    void *buf_body = malloc(sizeof(MAX_BUF_SIZE));
    while (1) {
        DEBUG_PRINT("waiting for msg...\n");
        if(!readn(client_fd, buf, sizeof(struct echo_header_v1)))
            goto EXIT;
        struct echo_header_v1 *echo_header_view = (struct echo_header_v1 *) buf;
        if (memcmp(echo_header_view->magic, ECHO_MAGIC, sizeof(echo_header_view->magic)) != 0)
            goto EXIT;
        if (echo_header_view->version == ECHO_VERSION::v1) {
            if (echo_header_view->cmd == ECHO_CMD::SEND) {
                if (!readn(client_fd, buf_body, echo_header_view->body_len))
                    goto EXIT;
                printf("[+] msg (%u bytes): ", echo_header_view->body_len);
                fflush(stdout);
                write(1, buf_body, echo_header_view->body_len);
            }
            else if (echo_header_view->cmd == ECHO_CMD::END) {
                if (!readn(client_fd, buf_body, echo_header_view->body_len))
                    goto EXIT;
                printf("[!] server (%u bytes): ", echo_header_view->body_len);
                fflush(stdout);
                write(1, buf_body, echo_header_view->body_len);
                exit(0);
            }
        }
        else {
            goto EXIT;
        }
    }
    EXIT:
    close(client_fd);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
        fprintf(stderr, "Connects to host:port, sends and receives msgs.\n");
        exit(1);
    }

    int port = atoi(argv[2]);
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(client_fd != -1, strerror(errno));
    struct sockaddr_in client_struct;
    bzero(&client_struct, sizeof(struct sockaddr_in));
    client_struct.sin_family = AF_INET;
    client_struct.sin_addr.s_addr = inet_addr(argv[1]);
    client_struct.sin_port = htons(port);

    ASSERT(connect(client_fd, (const sockaddr *) &client_struct, sizeof(client_struct)) == 0, strerror(errno));
    printf("[+] connected\n");
    pthread_t worker;
    ASSERT(pthread_create(&worker, NULL, worker_function, &client_fd) == 0, "worker not created");
    char *buf = (char *)malloc(MAX_BUF_SIZE);
    while(1) {
        fgets(buf, MAX_BUF_SIZE, stdin);
        size_t body_len = strlen(buf);
        if (body_len == 0)
            continue;
        void *msg;
        size_t len = construct_echo_msg_v1(&msg, ECHO_CMD::SEND, buf, body_len);
        if (sendn(client_fd, msg, len) == false)
            break;
        free(msg);
    }

}