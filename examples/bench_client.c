#define ZN_IMPLEMENTATION
#include "../znet.h"
#include "../znet_buffer.h"
#include "znet_bufferpool.h"


#include <stdio.h>
#include <string.h>


zn_State *S;
zn_BufferPool pool;

#define INTERVAL 5000
#define DATA_SIZE 1024

char addr[ZN_MAX_ADDRLEN];
char data[DATA_SIZE+2];
unsigned port = 12345;
int send_count = 0;
int recv_count = 0;
int connect_count = 0;

static void on_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    zn_BufferPoolNode *node = (zn_BufferPoolNode*)ud;
    if (err != ZN_OK) {
        zn_putbuffer(&pool, node);
        zn_deltcp(tcp);
        return;
    }
    send_count += count;
    zn_sendfinish(&node->send, count);
    if (node->user_data == 4) {
        zn_deltcp(tcp);
        zn_putbuffer(&pool, node);
    }
}

static void on_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    zn_BufferPoolNode *node = (zn_BufferPoolNode*)ud;
    if (err != ZN_OK) {
        zn_putbuffer(&pool, node);
        zn_deltcp(tcp);
        return;
    }
    recv_count += count;
    zn_recvfinish(&node->recv, count);
    zn_recv(tcp,
        zn_recvbuff(&node->recv),
        zn_recvsize(&node->recv), on_recv, ud);
}

static size_t on_header(void *ud, const char *buff, size_t len) {
    unsigned short plen;
    if (len < 2) return 0;
    memcpy(&plen, buff, 2);
    return ntohs(plen);
}

static void on_packet(void *ud, const char *buff, size_t len) {
    zn_BufferPoolNode *node = (zn_BufferPoolNode*)ud;
    if (zn_sendprepare(&node->send, buff, len))
        zn_send(node->tcp,
            zn_sendbuff(&node->send),
            zn_sendsize(&node->send), on_send, node);
    ++node->user_data;
}

static void on_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    zn_BufferPoolNode *node = (zn_BufferPoolNode*)ud;
    if (err != ZN_OK) {
        zn_putbuffer(&pool, node);
        zn_deltcp(tcp);
        return;
    }
    ++connect_count;
    zn_recv(tcp,
        zn_recvbuff(&node->recv),
        zn_recvsize(&node->recv), on_recv, ud);
    if (zn_sendprepare(&node->send, data, DATA_SIZE+2))
        zn_send(tcp,
            zn_sendbuff(&node->send),
            zn_sendsize(&node->send), on_send, ud);
}

static int on_client(void *ud, zn_Timer *timer, unsigned elapsed) {
    zn_BufferPoolNode *node = zn_getbuffer(&pool);
    zn_Tcp *tcp = zn_newtcp(S);
    zn_recvonheader(&node->recv, on_header, node);
    zn_recvonpacket(&node->recv, on_packet, node);
    node->user_data = 0;
    node->tcp = tcp;
    zn_connect(tcp, addr, port, on_connect, node);
    return 1;
}

static void init_data(void) {
    int count = 2;
    const char *padding = "Hello world\r\n";
    size_t paddlen = strlen(padding);
    short len = htons(DATA_SIZE);
    memcpy(data, &len, 2);
    while (count < DATA_SIZE) {
        strcpy(data+count, padding);
        count += paddlen;
    }
}

static int on_timer(void *ud, zn_Timer *timer, unsigned elapsed) {
    printf("%d: connect=%d, recv=%d, send=%d\n",
            zn_time(), connect_count, recv_count, send_count);
    connect_count = 0;
    recv_count = 0;
    send_count = 0;
    return INTERVAL;
}

int main(int argc, char **argv) {
    zn_Timer *t1, *t2;
    strcpy(addr, "127.0.0.1");
    if (argc == 2) {
        unsigned p = atoi(argv[1]);
        if (p != 0) port = port;
    }
    else if (argc == 3) {
        unsigned p = atoi(argv[2]);
        strcpy(addr, argv[1]);
        if (p != 0) port = port;
    }

    init_data();
    zn_initialize();
    S = zn_newstate();
    zn_initbuffpool(&pool);
    if (S == NULL)
        return 2;

    t1 = zn_newtimer(S, on_timer, NULL);
    zn_starttimer(t1, INTERVAL);

    t2 = zn_newtimer(S, on_client, NULL);
    zn_starttimer(t2, 0);

    return zn_run(S, ZN_RUN_LOOP);
}
/* cc: flags+='-s -O3' libs+='-lws2_32' */
