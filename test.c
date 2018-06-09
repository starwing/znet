#define ZN_IMPLEMENTATION
#include <stdio.h>
#include "znet.h"

zn_State *S;
zn_Accept *a;

#if TEST_IPV6
#define IP   "::1"
#else
#define IP   "127.0.0.1"
#endif
#define PORT 12345

static void on_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    printf("%p: sent: %s (%u): %s\n", tcp, (char*)ud, count, zn_strerror(err));
    free(ud);
    zn_deltcp(tcp);
}

static void on_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    printf("%p: received: %s (%d) %s\n",
           tcp, (char*)ud, count, zn_strerror(err));
    if (err == ZN_OK)
        zn_send(tcp, ud, count, on_send, ud);
    else
        free(ud);
}

static void on_accept(void *ud, zn_Accept *a, unsigned err, zn_Tcp *tcp) {
    char *buff = malloc(128);
    printf("%p: accepted: %s\n", tcp, zn_strerror(err));
    printf("%p: receiving ... ", tcp);
    int ret = zn_recv(tcp, buff, 128, on_recv, buff);
    if (ret != ZN_OK) free(buff);
    printf("%s\n", zn_strerror(ret));
    zn_accept(a, on_accept, ud);
}

static void on_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    printf("%p: on_connect: %s\n", tcp, zn_strerror(err));
    if (err != ZN_OK) {
        zn_deltcp(tcp);
        return;
    }
    char *buff = malloc(128);
    if (!buff) return;
    memset(buff, 0, 128);
    strcpy(buff, "hello world!");
    if (zn_send(tcp, buff, 128, on_send, buff) != ZN_OK)
	free(buff);
}

static zn_Time on_timer(void *ud, zn_Timer *t, zn_Time elapsed) {
    static int i = 0;

    if (i == 5) {
        printf("stop listening ...\n");
        zn_delaccept(a);
        return 0;
    }

    printf("%d>> %u\n", ++i, (unsigned)elapsed);
    zn_Tcp *tcp = zn_newtcp(S);
    printf("%p: connecting ...", tcp);
    int ret = zn_connect(tcp, IP, PORT, on_connect, NULL);
    printf("%s\n", zn_strerror(ret));

    return 1000;
}

static void cleanup(void) {
    printf("exiting ... ");
    zn_close(S);
    printf("OK\n");
    printf("deinitialize ... ");
    zn_deinitialize();
    printf("OK\n");
}

#ifdef _WIN32
static int deinited = 0;
static BOOL WINAPI on_interrupted(DWORD dwCtrlEvent) {
    if (!deinited) {
        deinited = 1;
        /* windows ctrl handler is running at another thread */
        zn_post(S, (zn_PostHandler*)cleanup, NULL);
    }
    return TRUE;
}

static void register_interrupted(void) {
    SetConsoleCtrlHandler(on_interrupted, TRUE);
}
#else
#include <signal.h>

static void on_interrupted(int signum) {
    if (signum == SIGINT)
        cleanup();
}

static void register_interrupted(void) {
   struct sigaction act; 
   act.sa_flags = SA_RESETHAND;
   act.sa_handler = on_interrupted;
   sigaction(SIGINT, &act, NULL);
}
#endif

int main(void) {
    printf("engine: %s\n", zn_engine());
    zn_initialize();
    if ((S = zn_newstate()) == NULL)
        return 2;

    /* server */
    a = zn_newaccept(S);
    zn_listen(a, IP, PORT);
    zn_accept(a, on_accept, NULL);
    printf("%p: listening ...\n", a);

    /* client */
    zn_Timer *t = zn_newtimer(S, on_timer, NULL);
    zn_starttimer(t, 1000);

    register_interrupted();
    return zn_run(S, ZN_RUN_LOOP);
}

/* win32cc: libs+='-lws2_32 -lmswsock' */
/* linuxcc: flags+='-pthread -lrt' */
/* maccc: flags+='-pthread' */
