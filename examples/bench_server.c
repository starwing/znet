#define ZN_IMPLEMENTATION
#include "../znet.h"
#include "../zn_buffer.h"
#include "zn_bufferpool.h"

#include <stdio.h>


/* server */

zn_State *S;
zn_BufferPool pool;

#define INTERVAL 5000
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
    zn_sendprepare(&node->send, buff, len);
    zn_send(node->tcp,
            zn_sendbuff(&node->send),
            zn_sendsize(&node->send), on_send, node);
}

static void on_accept(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp) {
    zn_BufferPoolNode *node;
    if (err != ZN_OK)
        exit(2);
    ++connect_count;
    node = zn_getbuffer(&pool);
    zn_recvonheader(&node->recv, on_header, node);
    zn_recvonpacket(&node->recv, on_packet, node);
    node->tcp = tcp;
    if (zn_recv(tcp,
               zn_recvbuff(&node->recv),
               zn_recvsize(&node->recv), on_recv, node) != ZN_OK) {
        zn_putbuffer(&pool, node);
        zn_deltcp(tcp);
    }
    zn_accept(accept, on_accept, ud);
}

static zn_Time on_timer(void *ud, zn_Timer *timer, zn_Time elapsed) {
    printf("%d: connect=%d, recv=%d, send=%d\n",
            zn_time(), connect_count, recv_count, send_count);
    connect_count = 0;
    recv_count = 0;
    send_count = 0;
    return INTERVAL;
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

int main(int argc, char **argv) {
    unsigned port = 12345;
    zn_Accept *accept;
    zn_Timer *timer;
    if (argc == 2) {
        unsigned p = atoi(argv[1]);
        if (p != 0) port = p;
    }

    zn_initialize();
    S = zn_newstate();
    zn_initbuffpool(&pool);
    if (S == NULL)
        return 2;

    accept = zn_newaccept(S);
    if (accept == NULL)
        return 2;
    zn_listen(accept, "0.0.0.0", port);
    zn_accept(accept, on_accept, NULL);
    printf("listening at: %u\n", port);

    timer = zn_newtimer(S, on_timer, NULL);
    zn_starttimer(timer, INTERVAL);

    register_interrupted();
    return zn_run(S, ZN_RUN_LOOP);
}
/* cc: flags+='-s -O3' libs+='-lws2_32' */
