#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <signal.h>
#include <pthread.h>

#include <vector>
#include <algorithm>

#include "echo.h"

using namespace std;

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

#define MAX_HANDLE      128

pthread_mutex_t workers_lock;
vector<pair<int, pthread_t> > workers;
bool broadcast = false;

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

void intHandler(int dummy) {
    pthread_mutex_lock(&workers_lock);
    void *msg;
    char *buf_body = "Server closed.\n";
    size_t len = construct_echo_msg_v1(&msg, ECHO_CMD::END, buf_body, strlen(buf_body));
    for(auto it = workers.begin(); it != workers.end(); it++) {
        sendn(it->first, msg, len);
        close(it->first);
    }
    pthread_mutex_unlock(&workers_lock);
    printf("[!] closing server...");
    exit(0);
}

void *worker_function (void *_data) {
    int client_fd = *((int *) _data);
    void *buf = malloc(sizeof(MAX_BUF_SIZE));
    void *buf_body = malloc(sizeof(MAX_BUF_SIZE));
    while (1) {
        DEBUG_PRINT("waiting for msg...\n");
        if(!readn(client_fd, buf, sizeof(struct echo_header_v1)))
            goto EXIT;
        struct echo_header_v1 *echo_header_view = (struct echo_header_v1 *) buf;
        DEBUG_PRINT("read... %4s\n", buf);
        if (memcmp(echo_header_view->magic, ECHO_MAGIC, sizeof(echo_header_view->magic)) != 0)
            goto EXIT;
        DEBUG_PRINT("version... %d\n", echo_header_view->version);
        if (echo_header_view->version == ECHO_VERSION::v1) {
            if (echo_header_view->cmd == ECHO_CMD::SEND) {
                if (!readn(client_fd, buf_body, echo_header_view->body_len))
                    goto EXIT;
                printf("[+] msg (%u bytes): ", echo_header_view->body_len);
                fflush(stdout);
                write(1, buf_body, echo_header_view->body_len);
                void *msg;
                size_t len = construct_echo_msg_v1(&msg, ECHO_CMD::SEND, buf_body, echo_header_view->body_len);
                if (broadcast) {
                    pthread_mutex_lock(&workers_lock);
                    for(auto it = workers.begin(); it != workers.end(); it++) {
                        sendn(it->first, msg, len);
                    }
                    pthread_mutex_unlock(&workers_lock);
                }
                else {
                    if (!sendn(client_fd, msg, len)) {
                        free(msg);
                        goto EXIT;
                    }
                }
                free(msg);
                
            }
            else if(echo_header_view->cmd == ECHO_CMD::END) {
                goto EXIT;
            }
        }
        else {
            void *msg;
            char *buf_body = "Refused from server (wrong version).\n";
            size_t len = construct_echo_msg_v1(&msg, ECHO_CMD::END, buf_body, strlen(buf_body));
            sendn(client_fd, msg, len);
            free(msg);
            goto EXIT;
        }
    }
    EXIT:
    DEBUG_PRINT("exit...\n");
    close(client_fd);
}

int main (int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port> [-b]\n", argv[0]);
        fprintf(stderr, "Listens to specified port, accepts client and echos msgs.\n");
        fprintf(stderr, " -b     broadcast all received msgs to all clients\n");
        exit(1);
    }
    
    int port = atoi(argv[1]);
    
    if (argc >= 3 && strncmp("-b", argv[2], sizeof("-b")) == 0) {
        broadcast = true;
    }
    pthread_mutex_init(&workers_lock, NULL);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(server_fd != -1, strerror(errno));
    
    int optval = 1;
    ASSERT(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) != -1, strerror(errno));
    
    struct sockaddr_in server_struct;
    bzero(&server_struct, sizeof(struct sockaddr_in));
    server_struct.sin_family = AF_INET;
    server_struct.sin_port = htons(port);
    server_struct.sin_addr.s_addr = htonl(INADDR_ANY);
    ASSERT(bind(server_fd, (const sockaddr *) &server_struct, sizeof(server_struct)) != -1, strerror(errno));
    
    ASSERT(listen(server_fd, MAX_HANDLE) != -1, strerror(errno));

    printf("[*] listening on port %d\n", port);
    printf("[*] broadcast mode %s\n", broadcast ? "on": "off");

    signal(SIGINT, intHandler);

    while (1) {
        struct sockaddr_in client_struct;
        socklen_t client_struct_len = sizeof(client_struct);
        int *client_fd = new int;
        *client_fd = accept(server_fd, (struct sockaddr *) &client_struct, &client_struct_len);
        DASSERT(*client_fd != -1, strerror(errno));
        pthread_mutex_lock(&workers_lock);
        pthread_t thread;
        ASSERT(pthread_create(&thread, NULL, worker_function, (void *) client_fd) == 0, "worker not created");
        workers.push_back(make_pair(*client_fd, thread));
        pthread_mutex_unlock(&workers_lock);
    }

}