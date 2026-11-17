#include <stdio.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"
#include "time_util.h"

#define CONNECTION_SIZE 1024

#define MAX_PORTS 20

typedef int (*msg_handler)(char *msg, int length, char *response);

static msg_handler kvs_handler;

int accept_cb(int fd);
int recv_cb(int fd);
int send_cb(int fd);

int kvs_request(struct conn *c) {
    c->wlength = kvs_handler(c->rbuffer, c->rlength, c->wbuffer);
    return 0;
}

int kvs_response(struct conn *c) {
    return 0;
}

// int echo_response(struct conn *c) {
//     c->wlength = c->rlength; 
//     int n = snprintf(c->wbuffer, BUFFER_LENGTH, "Response from server: %.*s", c->rlength, c->rbuffer);
//     c->wlength = n;
//
//     printf("[%d]RECV: %.*s\n", c->rlength, c->rlength, c->rbuffer);
//
//     printf("[%d]Response: %.*s\n", c->wlength, c->wlength, c->wbuffer);
//     return 0;
// }

int epfd = 0;

struct conn conn_list[CONNECTION_SIZE] = {0};


int r_init_server(unsigned short port) {

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (-1 == bind(sockfd, (struct sockaddr *)&servaddr, sizeof(struct sockaddr))) {
        printf("bind failed: %s\n", strerror(errno));
    }

    listen(sockfd, 10);

    return sockfd;
}

int set_event(int fd, int event, int epoll_op) {
    struct epoll_event ev;
    ev.events = event;
    ev.data.fd = fd;
    epoll_ctl(epfd, epoll_op, fd, &ev);

    return 0;
}

int event_register(int fd, int event) {
    if (fd < 0) {
        return -1;
    }

    conn_list[fd].fd = fd;
    conn_list[fd].r_action.recv_callback = recv_cb;
    conn_list[fd].send_callback = send_cb;

    memset(conn_list[fd].rbuffer, 0, BUFFER_LENGTH);
    conn_list[fd].rlength = 0;

    memset(conn_list[fd].wbuffer, 0, BUFFER_LENGTH);
    conn_list[fd].wlength = 0;

    set_event(fd, event, EPOLL_CTL_ADD);

    return 0;
}

// listenfd -> EPOLLIN -> accept_cb
int accept_cb(int fd) {
    struct sockaddr_in clientaddr;
    socklen_t len = sizeof(clientaddr);

    int clientfd = accept(fd, (struct sockaddr *)&clientaddr, &len);
    if (clientfd < 0) {
        printf("accept errno: %d -> %s\n", errno, strerror(errno));
        return -1;
    }

    event_register(clientfd, EPOLLIN);

    return 0;
}

/*
 * clientfd -> EPOLLIN -> recv_cb
 *
 * 一个连接在 EPOLLIN / EPOLLOUT 两个状态之间来回切换（一问一答）:
 *
 *           EPOLLIN 触发                     EPOLLOUT 触发
 *   ┌──────────────────────┐        ┌──────────────────────┐
 *   │ recv_cb:             │        │ send_cb:             │
 *   │  读请求 → 处理       │ ─────▶ │  发送 wbuffer        │
 *   │  MOD 成 EPOLLOUT     │        │  MOD 回 EPOLLIN      │
 *   └──────────────────────┘ ◀───── └──────────────────────┘
 *
 * recv_cb 不直接 send，只把回复放进 wbuffer，然后让 epoll 在 fd 可写时
 * 通知我们。这样所有 I/O 都由 epoll 驱动，对方接收慢时也不会阻塞在 send 上。
 */
int recv_cb(int fd) {
    memset(conn_list[fd].rbuffer, 0, BUFFER_LENGTH);
    // ① 从内核读取数据到 rbuffer
    int count = recv(fd, conn_list[fd].rbuffer, BUFFER_LENGTH, 0);

    // ② 处理断开 / 出错
    //    count == 0: 客户端正常关闭连接
//    count <  0: 出错
    if (count <= 0) {
        if (count < 0) {
            printf("count: %d, errno: %d, %s\n", count, errno, strerror(errno));
        }

        close(fd);
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);

        return 0;
    }

    // ③ 记录收到多少字节
    conn_list[fd].rlength = count;

    // ④ 交给业务层处理: 解析 rbuffer，把回复写进 wbuffer，长度写进 wlength
    kvs_request(&conn_list[fd]);

    // ⑤ 回复已准备好，把监听事件切换成"可写"
    set_event(fd, EPOLLOUT, EPOLL_CTL_MOD);

    return count;
}

int send_cb(int fd) {
    kvs_response(&conn_list[fd]);
    //echo_response(&conn_list[fd]);
    int count = 0;
    if (conn_list[fd].wlength != 0) {
        count = send(fd, conn_list[fd].wbuffer, conn_list[fd].wlength, 0);
    }

    set_event(fd, EPOLLIN, EPOLL_CTL_MOD);

    return count;
}

int reactor_start(unsigned port, msg_handler handler) {

    kvs_handler = handler;

    epfd = epoll_create(1);

    int i = 0;
    for (i = 0; i < MAX_PORTS; i++) {
        int sockfd = r_init_server(port + i);

        conn_list[sockfd].fd = sockfd;
        conn_list[sockfd].r_action.accept_callback = accept_cb;

        set_event(sockfd, EPOLLIN, EPOLL_CTL_ADD);
    }

    while (1) {
        struct epoll_event events[1024] = {0};
        int nready = epoll_wait(epfd, events, 1024, -1);

        int i = 0;
        for (i = 0; i < nready; i++) {
            int connfd = events[i].data.fd;


            if (events[i].events & EPOLLIN) {
                conn_list[connfd].r_action.recv_callback(connfd);
            }

            if (events[i].events & EPOLLOUT) {
                conn_list[connfd].send_callback(connfd);
            }
        }
    }

    return 0;
}

