
#include <liburing.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define EVENT_ACCEPT 0
#define EVENT_READ 1
#define EVENT_WRITE 2

#define ENTRIES_LENGTH 1024
#define BUFFER_LENGTH 1024
#define MAX_CONN 1024


extern int kvs_protocol(char *msg, int length, char *response);

typedef int (*msg_handler)(char *msg, int length, char *response);
static msg_handler kvs_handler;


struct conn_info {
    int fd;
    int event;
    char buffer[BUFFER_LENGTH];
    char wbuffer[BUFFER_LENGTH];
};

struct conn_info conn_list[MAX_CONN] = {0};

int p_init_server(unsigned short port) {

    int                sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(struct sockaddr_in));
    serveraddr.sin_family      = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port        = htons(port);

    if (-1 == bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(struct sockaddr))) {
        perror("bind");
        return -1;
    }

    listen(sockfd, 10);

    return sockfd;
}

int set_event_recv(struct io_uring *ring, int sockfd, size_t len, int flags) {

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (sqe == NULL) {
        return -1; // queue is full
    }

    struct conn_info *conn = &conn_list[sockfd];
    conn->fd = sockfd;
    conn->event = EVENT_READ;
    memset(conn->buffer, 0, len);

    io_uring_prep_recv(sqe, sockfd, conn->buffer, len, flags);
    io_uring_sqe_set_data(sqe, conn);

    return 0;
}

int set_event_send(struct io_uring *ring, int sockfd, size_t len, int flags) {

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (sqe == NULL) {
        return -1;
    }

    struct conn_info *conn = &conn_list[sockfd];
    conn->fd = sockfd;
    conn->event = EVENT_WRITE;

    // TODO: to echo
    // memset(conn->buffer, 0, len);

    io_uring_prep_send(sqe, sockfd, conn->wbuffer, len, flags);
    io_uring_sqe_set_data(sqe, conn);

    return 0;
}

int set_event_accept(struct io_uring *ring, int sockfd, struct sockaddr *addr, socklen_t *addrlen,
                     int flags) {

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);

    if (sqe == NULL) {
        return -1;
    }

    struct conn_info *conn = &conn_list[sockfd];
    conn->fd = sockfd;
    conn->event = EVENT_ACCEPT;

    io_uring_prep_accept(sqe, sockfd, addr, addrlen, flags);
    io_uring_sqe_set_data(sqe, conn);

    return 0;
}

int proactor_start(unsigned short port, msg_handler handler) {

    int sockfd  = p_init_server(port);
    kvs_handler = handler;

    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    struct io_uring ring;
    io_uring_queue_init_params(ENTRIES_LENGTH, &ring, &params);

    struct sockaddr_in clientaddr;
    socklen_t          len = sizeof(clientaddr);
    set_event_accept(&ring, sockfd, (struct sockaddr *)&clientaddr, &len, 0);

    while (1) {

        io_uring_submit(&ring);

        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);

        struct io_uring_cqe *cqes[128];
        int                  nready = io_uring_peek_batch_cqe(&ring, cqes, 128); // epoll_wait

        int i = 0;
        for (i = 0; i < nready; i++) {

            struct io_uring_cqe *entries = cqes[i];
            struct conn_info     *info = io_uring_cqe_get_data(entries);
            int ret = entries->res;

            if (info->event == EVENT_ACCEPT) {

                len = sizeof(clientaddr);
                set_event_accept(&ring, sockfd, (struct sockaddr *)&clientaddr, &len, 0);

                int connfd = ret;
                if (connfd < 0) {
                    printf("accept failed: %s\n", strerror(-connfd));
                    continue;
                }
                if (connfd >= MAX_CONN) { // conn_list 放不下
                    close(connfd);
                    continue;
                }

                set_event_recv(&ring, connfd, BUFFER_LENGTH-1, 0);

            } else if (info->event == EVENT_READ) {

                if (ret <= 0) {
                    close(info->fd);
                } else {

                    ret = kvs_handler(info->buffer, ret, info->wbuffer);
                    set_event_send(&ring, info->fd, ret, 0);
                }
            } else if (info->event == EVENT_WRITE) {

                if (ret <= 0) {
                    printf("write failed for fd: %d, %s\n", info->fd, strerror(-ret));
                    close(info->fd);
                    continue;
                } else {
                    // printf("set_event_send ret: %d, %s\n", ret, buffer);

                    set_event_recv(&ring, info->fd, BUFFER_LENGTH - 1, 0);
                }
            }
        }

        io_uring_cq_advance(&ring, nready);
    }
}
