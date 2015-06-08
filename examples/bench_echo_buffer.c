#define ZN_IMPLEMENTATION
#include "../znet.h"
#include "../znet_buffer.h"
#include <stdio.h>
#include <string.h>


#include "znet_bufferpool.h"


#define BLOCK_SIZE 1024
char send_data[BLOCK_SIZE];
size_t send_ok, send_err, send_bytes;
size_t recv_ok, recv_err, recv_bytes;

char     addr[ZN_MAX_ADDRLEN] = "127.0.0.1";
unsigned port = 8081;

int is_client;
zn_State *S;
zn_BufferPool pool;

static void client_error(zn_Tcp *tcp, zn_BufferPoolNode *data);

static void on_client_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    zn_BufferPoolNode *data = (zn_BufferPoolNode*)ud;
    if (err != ZN_OK) {
        ++send_err;
        client_error(tcp, data);
        return;
    }
    ++send_ok;
    send_bytes += count;
    if (zn_sendfinish(&data->send, count))
        zn_send(tcp, zn_sendbuff(&data->send), zn_sendsize(&data->send),
                on_client_send, ud);
}

static void on_client_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    zn_BufferPoolNode *data = (zn_BufferPoolNode*)ud;
    if (err != ZN_OK) {
        ++recv_err;
        client_error(tcp, data);
        return;
    }

    ++recv_ok;
    recv_bytes += count;
    zn_recvfinish(&data->recv, count);
    zn_recv(tcp, zn_recvbuff(&data->recv), zn_recvsize(&data->recv),
            on_client_recv, ud);
}

static size_t on_header(void *ud, const char *buff, size_t len) {
    unsigned short packet_len;
    if (len < 2) return 0;
    memcpy(&packet_len, buff, 2);
    return (size_t)ntohs(packet_len);
}

static void on_client_packet(void *ud, const char *buff, size_t len) {
    zn_BufferPoolNode *data = (zn_BufferPoolNode*)ud;
    if (zn_sendprepare(&data->send, buff, len)
            && zn_send(data->tcp, zn_sendbuff(&data->send), zn_sendsize(&data->send),
                on_client_send, ud) != ZN_OK)
        client_error(data->tcp, ud);
}

static void on_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    zn_BufferPoolNode *data = (zn_BufferPoolNode*)ud;

    if (err != ZN_OK) {
        client_error(tcp, ud);
        return;
    }

    if (zn_recv(tcp, zn_recvbuff(&data->recv), zn_recvsize(&data->recv),
                on_client_recv, ud) != ZN_OK)
        client_error(tcp, ud);
    else if (zn_sendprepare(&data->send, send_data, BLOCK_SIZE)
            && zn_send(tcp, zn_sendbuff(&data->send), zn_sendsize(&data->send),
                on_client_send, ud) != ZN_OK)
        client_error(tcp, ud);
}

static void new_connection(void *ud, zn_State *S) {
    zn_BufferPoolNode *data = zn_getbuffer(&pool);
    zn_recvonheader(&data->recv, on_header, data);
    zn_recvonpacket(&data->recv, on_client_packet, data);
    data->tcp = zn_newtcp(S);
    if (zn_connect(data->tcp, addr, port, on_connect, data) != ZN_OK)
        zn_post(S, new_connection, NULL);
}

static void client_error(zn_Tcp *tcp, zn_BufferPoolNode *data) {
    zn_putbuffer(&pool, data);
    zn_deltcp(tcp);
    zn_post(S, new_connection, NULL);
}

static void on_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    zn_BufferPoolNode *data = (zn_BufferPoolNode*)ud;
    if (err != ZN_OK) {
        ++send_err;
        zn_deltcp(tcp);
        zn_putbuffer(&pool, data);
        return;
    }
    ++send_ok;
    send_bytes += count;
    if (zn_sendfinish(&data->send, count))
        zn_send(tcp, zn_sendbuff(&data->send), zn_sendsize(&data->send),
                on_send, ud);
}

static void on_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    zn_BufferPoolNode *data = (zn_BufferPoolNode*)ud;
    if (err != ZN_OK) {
        ++recv_err;
        zn_deltcp(tcp);
        zn_putbuffer(&pool, data);
        return;
    }
    ++recv_ok;
    recv_bytes += count;
    zn_recvfinish(&data->recv, count);
    zn_recv(tcp, zn_recvbuff(&data->recv), zn_recvsize(&data->recv),
            on_recv, ud);
}

static void on_packet(void *ud, const char *buff, size_t len) {
    zn_BufferPoolNode *data = (zn_BufferPoolNode*)ud;
    if (zn_sendprepare(&data->send, buff, len)
            && zn_send(data->tcp, zn_sendbuff(&data->send), zn_sendsize(&data->send),
                on_send, ud) != ZN_OK)
    {
        zn_deltcp(data->tcp);
        zn_putbuffer(&pool, data);
    }
}

static void on_accept(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp) {
    zn_BufferPoolNode *data = zn_getbuffer(&pool);
    if (err != ZN_OK)
        return;
    zn_recvonheader(&data->recv, on_header, data);
    zn_recvonpacket(&data->recv, on_packet, data);
    data->tcp = tcp;
    if (zn_recv(tcp, zn_recvbuff(&data->recv), zn_recvsize(&data->recv),
                on_recv, data) != ZN_OK)
        zn_deltcp(tcp);
    zn_accept(accept, on_accept, ud);
}

static void human_readed(size_t sz) {
    if (sz < 1024)
        printf("%dB", sz);
    else if (sz < 1024*1024)
        printf("%.3fKB", sz/1024.0);
    else if (sz < 1024*1024*1024)
        printf("%.3fMB", sz/(1024.0*1024.0));
    else
        printf("%.3fGB", sz/(1024.0*1024.0*1024.0));
}

static void print_ud(const char *title) {
    printf("(recv=%d/%d/", recv_ok, recv_err);
    human_readed(recv_bytes);
    printf(", send=%d/%d/", send_ok, send_err);
    human_readed(send_bytes);
    printf(")");
    recv_ok = recv_err = recv_bytes = 0;
    send_ok = send_err = send_bytes = 0;
}

static void on_summary(void *ud, zn_Timer *timer, unsigned elapsed) {
    printf("%u: ", zn_time());
    if (is_client)
        print_ud("client");
    else
        print_ud("server");
    printf("\n");
    zn_starttimer(timer, 1000);
}

int main(int argc, const char **argv) {
    if (argc == 2 && strcmp(argv[1], "-h") == 0) {
        printf("usage: %s [(client/server) [ip [port]]]\n", argv[0]);
        exit(0);
    }
    if (argc > 1) {
        if (strcmp(argv[1], "client") == 0)
            is_client = 1;
    }
    if (argc > 2) {
        strncpy(addr, argv[2], ZN_MAX_ADDRLEN-1);
    }
    if (argc > 3) {
        unsigned p = atoi(argv[3]);
        if (p != 0) port = port;
    }

    zn_initialize();
    if ((S = zn_newstate()) == NULL) return 2;

    zn_initbuffpool(&pool);
    {
        unsigned short dlen = htons(BLOCK_SIZE-2);
        memcpy(send_data, &dlen, 2);
    }

    if (is_client) {
        zn_post(S, new_connection, NULL);
        printf("connecting to %s:%d ...\n", addr, port);
    }
    else {
        zn_Accept *accept;
        if ((accept = zn_newaccept(S)) == NULL) return 2;
        zn_listen(accept, addr, port);
        zn_accept(accept, on_accept, NULL);
        printf("listening at %s:%d ...\n", addr, port);
    }

    zn_starttimer(zn_newtimer(S, on_summary, NULL), 1000);

    return zn_run(S, ZN_RUN_LOOP);
}
/* cc: flags+='-s -O3' libs+='-lws2_32' */
