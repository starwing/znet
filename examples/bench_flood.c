#define ZN_IMPLEMENTATION
#include "../znet.h"
#include <stdio.h>
#include <string.h>


char     addr[ZN_MAX_ADDRLEN] = "127.0.0.1";
unsigned port = 8081;


#define BLOCK_SIZE 1024
typedef struct Userdata {
    size_t send_ok, send_err, send_bytes;
    size_t recv_ok, recv_err, recv_bytes;
    char send[BLOCK_SIZE];
    char recv[BLOCK_SIZE];
} Userdata;

int is_client;
zn_State *S;
Userdata server;
Userdata client;

static void client_error(zn_Tcp *tcp);

static void on_client_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    Userdata *data = (Userdata*)ud;
    if (err != ZN_OK) {
        ++data->send_err;
        client_error(tcp);
        return;
    }
    ++data->send_ok;
    data->send_bytes += count;
    if (zn_send(tcp, data->send, BLOCK_SIZE, on_client_send, ud) != ZN_OK)
        client_error(tcp);
}

static void on_client_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    Userdata *data = (Userdata*)ud;
    if (err != ZN_OK) {
        ++data->recv_err;
        client_error(tcp);
        return;
    }

    ++data->recv_ok;
    data->recv_bytes += count;
    if (zn_recv(tcp, data->recv, BLOCK_SIZE, on_client_recv, ud) != ZN_OK)
        client_error(tcp);
}

static void on_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    if (err != ZN_OK)
        client_error(tcp);
    else if (zn_send(tcp, client.send, BLOCK_SIZE, on_client_send, &client) != ZN_OK)
        client_error(tcp);
    else if (zn_recv(tcp, client.recv, BLOCK_SIZE, on_client_recv, &client) != ZN_OK)
        client_error(tcp);
}

static void on_error(void *ud, zn_State *S) {
    zn_Tcp *tcp = zn_newtcp(S);
    if (zn_connect(tcp, addr, port, on_connect, &client) != ZN_OK)
        zn_post(S, on_error, NULL);
}

static void client_error(zn_Tcp *tcp) {
    zn_deltcp(tcp);
    zn_post(S, on_error, NULL);
}

static void on_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    Userdata *data = (Userdata*)ud;
    if (err != ZN_OK) {
        ++data->send_err;
        zn_deltcp(tcp);
        return;
    }
    ++data->send_ok;
    data->send_bytes += count;
    if (zn_send(tcp, data->send, BLOCK_SIZE, on_send, ud) != ZN_OK)
        zn_deltcp(tcp);
}

static void on_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    Userdata *data = (Userdata*)ud;
    if (err != ZN_OK) {
        ++data->recv_err;
        zn_deltcp(tcp);
        return;
    }
    ++data->recv_ok;
    data->recv_bytes += count;
    if (zn_recv(tcp, data->recv, BLOCK_SIZE, on_recv, ud) != ZN_OK)
        zn_deltcp(tcp);
}

static void on_accept(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp) {
    if (err != ZN_OK)
        return;
    if (zn_send(tcp, server.send, BLOCK_SIZE, on_send, &server) != ZN_OK)
        zn_deltcp(tcp);
    else if (zn_recv(tcp, server.recv, BLOCK_SIZE, on_recv, &server) != ZN_OK)
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

static void print_ud(Userdata *ud, const char *title) {
    printf("(recv=%d/%d/", ud->recv_ok, ud->recv_err);
    human_readed(ud->recv_bytes);
    printf(", send=%d/%d/", ud->send_ok, ud->send_err);
    human_readed(ud->send_bytes);
    printf(")");
    ud->recv_ok = ud->recv_err = ud->recv_bytes = 0;
    ud->send_ok = ud->send_err = ud->send_bytes = 0;
}

static int on_summary(void *ud, zn_Timer *timer, unsigned elapsed) {
    printf("%u: ", zn_time());
    if (is_client)
        print_ud(&client, "client");
    else
        print_ud(&server, "server");
    printf("\n");
    return 1000;
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

    if (is_client) {
        zn_Tcp *tcp;
        if ((tcp = zn_newtcp(S)) == NULL) return 2;
        zn_connect(tcp, addr, port, on_connect, NULL);
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
