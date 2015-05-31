#define ZN_IMPLEMENTATION
#include "../znet.h"
#include <stdio.h>

static void on_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    free(ud);
    printf("%p: on_send: %s (%d)\n", tcp, zn_strerror(err), count);
}

static void on_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    char *buff = (char*)malloc(1024);
    printf("%p: on_recv: %s (%d): %s\n", tcp, zn_strerror(err), count, (char*)ud);
    if (err != ZN_OK) {
        printf("%p: close\n", tcp);
        zn_deltcp(tcp);
        return;
    }
    if (zn_send(tcp, ud, count, on_send, ud) != ZN_OK) {
        free(ud);
        printf("%p: close\n", tcp);
        zn_deltcp(tcp);
    }
    if (zn_recv(tcp, buff, 1024, on_recv, buff) != ZN_OK) {
        free(buff);
        printf("%p: close\n", tcp);
        zn_deltcp(tcp);
    }
}

static void on_accept(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp) {
    char *buff = (char*)malloc(1024);
    if (err != ZN_OK)
        exit(2);
    printf("accept: %p\n", tcp);
    if (zn_recv(tcp, buff, 1024, on_recv, buff) != ZN_OK) {
        free(buff);
        printf("%p: close\n", tcp);
        zn_deltcp(tcp);
    }
    zn_accept(accept, on_accept, ud);
}

int main(int argc, const char **argv) {
    unsigned port = 12345;
    zn_State *S;
    zn_Accept *accept;
    if (argc == 2) {
        unsigned p = atoi(argv[1]);
        if (p != 0) port = port;
    }

    zn_initialize();
    S = zn_newstate();
    if (S == NULL)
        return 2;

    accept = zn_newaccept(S);
    if (accept == NULL)
        return 2;
    zn_listen(accept, "0.0.0.0", port);
    zn_accept(accept, on_accept, NULL);
    printf("listening at: %u\n", port);

    return zn_run(S, ZN_RUN_LOOP);
}
/* cc: libs+='-lws2_32' */
