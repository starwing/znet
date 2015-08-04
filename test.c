#define ZN_IMPLEMENTATION
#include <stdio.h>
#include "znet.h"

zn_State *S;
zn_Accept *a;

void on_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    printf("%p: sent: %s (%u): %s\n", tcp, (char*)ud, count, zn_strerror(err));
    free(ud);
    zn_deltcp(tcp);
}

void on_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    printf("%p: received: %s (%d) %s\n", tcp, (char*)ud, count, zn_strerror(err));
    if (err == ZN_OK)
        zn_send(tcp, ud, count, on_send, ud);
    else
        free(ud);
}

void on_accept(void *ud, zn_Accept *a, unsigned err, zn_Tcp *tcp) {
    char *buff = malloc(128);
    printf("%p: accepted: %s\n", tcp, zn_strerror(err));
    printf("%p: receiving ... ", tcp);
    int ret = zn_recv(tcp, buff, 128, on_recv, buff);
    printf("%s\n", zn_strerror(ret));
    zn_accept(a, on_accept, ud);
}

void on_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    printf("%p: on_connect: %s\n", tcp, zn_strerror(err));
    if (err != ZN_OK) {
        zn_deltcp(tcp);
        return;
    }
    char *buff = malloc(128);
    if (!buff) return;
    memset(buff, 0, 128);
    strcpy(buff, "hello world!");
    zn_send(tcp, buff, 128, on_send, buff);
}

int on_timer(void *ud, zn_Timer *t, unsigned elapsed) {
    static int i = 0;

    printf("%d>> %d\n", i, elapsed);
    zn_Tcp *tcp = zn_newtcp(S);
    printf("%p: connecting ...", tcp);
    int ret = zn_connect(tcp, "127.0.0.1", 12345, on_connect, NULL);
    printf("%s\n", zn_strerror(ret));

    if (i++ == 5) {
        printf("stop listening ...\n");
        zn_delaccept(a);
        return 0;
    }
    return 1000;
}

int deinited = 0;
void cleanup(void) {
    if (!deinited) {
        deinited = 1;
        printf("exiting ... ");
        zn_close(S);
        printf("OK\n");
        printf("deinitialize ... ");
        zn_deinitialize();
        printf("OK\n");
    }
}

#ifdef _WIN32
BOOL WINAPI on_interrupted(DWORD dwCtrlEvent) {
    zn_post(S, (zn_PostHandler*)cleanup, NULL);
    return TRUE;
}
#endif

int main(void) {
    zn_initialize();
    if ((S = zn_newstate()) == NULL)
        return 255;
    atexit(cleanup);

    /* server */
    a = zn_newaccept(S);
    zn_listen(a, "127.0.0.1", 12345);
    zn_accept(a, on_accept, NULL);
    printf("%p: listening ...\n", a);

    /* client */
    zn_Timer *t = zn_newtimer(S, on_timer, NULL);
    zn_starttimer(t, 1000);

#ifdef _WIN32
    SetConsoleCtrlHandler(on_interrupted, TRUE);
#endif

    return zn_run(S, ZN_RUN_LOOP);
}

/* cc: libs+='-lws2_32 -lmswsock' */
