#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "../src/time_util.h"
#include "../src/mem_pool/mem_pool.h"


#define MAX_MSG_LENGTH      1024

int send_msg(int connfd, char *msg, int length) {
    int res = send(connfd, msg, length, 0);
    if (res < 0) {
        perror("send");
        exit(1);
    }

    return res;
}


int recv_msg(int connfd, char *msg, int length) {
    int res = recv(connfd, msg, length, 0);
    if (res < 0) {
        perror("recv");
        exit(1);
    }

    return res;
}


void testcase(int connfd, char *msg, char *pattern, char *casename) {
    if (!msg || !pattern || !casename) {
        printf("missing param\n");
        return;
    }

    send_msg(connfd, msg, strlen(msg));

    char result[MAX_MSG_LENGTH] = {0};
    recv_msg(connfd, result, MAX_MSG_LENGTH);

    if (strcmp(result, pattern) == 0) {
        // printf("==> PASS -> %s\n", casename);
    } else {
        printf("==> FAILED -> %s, '%s' != '%s' \n", casename, result, pattern);
        exit(1);
    }
}

int connect_tcp_server(const char *ip, unsigned short port) {
    int connfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_in));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if (0 != connect(connfd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in))) {
        perror("Connect Error\n");
        return -1;
    }

    return connfd;
}


void array_testcase(int connfd, int num_cases) {

    int i = 0;

    struct timeval tv_begin;
    gettimeofday(&tv_begin, NULL);

    for (i = 0; i < num_cases; i++) {
        testcase(connfd, "SET Teacher King", "OK\r\n", "SET-Teacher");
        testcase(connfd, "GET Teacher", "King\r\n", "GET-Teacher");
        testcase(connfd, "MOD Teacher Darren", "OK\r\n", "MOD-Teacher");
        testcase(connfd, "GET Teacher", "Darren\r\n", "GET-Teacher");
        testcase(connfd, "EXIST Teacher", "EXIST\r\n", "GET-Teacher");
        testcase(connfd, "DEL Teacher", "OK\r\n", "DEL-Teacher");
        testcase(connfd, "GET Teacher", "NO EXIST\r\n", "GET-Teacher");
        testcase(connfd, "MOD Teacher KING", "NO EXIST\r\n", "MOD-Teacher");
        testcase(connfd, "EXIST Teacher", "NO EXIST\r\n", "GET-Teacher");
    }

    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);

    int time_used = time_sub_ms(&tv_end, &tv_begin);

    printf("array testcase -> time used: %d, qps: %d\n", time_used, num_cases * 1000 / time_used);

}

void rbtree_testcase(int connfd, int num_cases) {

    int i = 0;

    struct timeval tv_begin;
    gettimeofday(&tv_begin, NULL);

    for (i = 0; i < num_cases; i++) {
        testcase(connfd, "RSET Teacher King", "OK\r\n", "SET-Teacher");
        testcase(connfd, "RGET Teacher", "King\r\n", "GET-Teacher");
        testcase(connfd, "RMOD Teacher Darren", "OK\r\n", "MOD-Teacher");
        testcase(connfd, "RGET Teacher", "Darren\r\n", "GET-Teacher");
        testcase(connfd, "REXIST Teacher", "EXIST\r\n", "GET-Teacher");
        testcase(connfd, "RDEL Teacher", "OK\r\n", "DEL-Teacher");
        testcase(connfd, "RGET Teacher", "NO EXIST\r\n", "GET-Teacher");
        testcase(connfd, "RMOD Teacher KING", "NO EXIST\r\n", "MOD-Teacher");
        testcase(connfd, "REXIST Teacher", "NO EXIST\r\n", "GET-Teacher");
    }

    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);

    int time_used = time_sub_ms(&tv_end, &tv_begin);

    printf("rbtree testcase -> time used: %d, qps: %d\n", time_used, 9 * num_cases * 1000 / time_used);

}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("arg error\n");
        return -1;
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);
    int connfd = connect_tcp_server(ip, port);
    array_testcase(connfd, 100000);
    //rbtree_testcase(connfd, 100000);

}
