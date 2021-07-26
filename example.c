
/* tell znet include all implement details into here */ 
#define ZN_IMPLEMENTATION

/* if you want use select backend on *nix or Mac, define this: */
/*#define ZN_USE_SELECT*/

/* include znet library: only need a header file! */
#include "znet.h"


#include <stdio.h>


/* we make a macro to compute a literal string's length, and put
 * length after string. */
#define send_string(str) str, sizeof(str)-1

/* znet doesn't maintain send/receive buffers. we should maintain them
 * ourselves. these buffer should alive in a whole receive duration
 * (from zn_recv() to on_recv() callback), or a whole send duration
 * (from zn_send() to on_send() callback). So we should allocate them
 * on heap. we make a struct for hold buffer and other useful
 * informations. we could set this data as the last argument (ud) of
 * zn_recv()/zn_send(), and retrieve them from the first pointer of
 * callbacks.  */
#define MYDATA_BUFLEN 1024

typedef struct MyData {
    char buffer[MYDATA_BUFLEN];
    int idx;
    int count;
} MyData;

/* function to accept a new coming connection. */
void on_accept(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp);

/* function when a client is connected on a server. */
void on_connection(void *ud, zn_Tcp *tcp, unsigned err);

/* function when a tcp in server received something. */
void on_server_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count);

/* function when a tcp in server sent something. */
void on_server_sent(void *ud, zn_Tcp *tcp, unsigned err, unsigned count);

/* function when a tcp in client sent something. */
void on_client_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count);

/* function when a tcp in client sent something. */
void on_client_sent(void *ud, zn_Tcp *tcp, unsigned err, unsigned count);

/* function when a udp in server received something. notice that we
 * needn't a function that when a udp sent something, because udp send
 * always returns immediately. */
void on_udp_recv(void *ud, zn_Udp *udp, unsigned err, unsigned count,
        const char *addr, unsigned port);

/* functions that will called after some milliseconds after.  */
zn_Time on_timer(void *ud, zn_Timer *timer, zn_Time elapsed);

/* tcp listen port */
const int PORT = 12345; 

/* the main entry of our examples. */
int main(void) {
    zn_State  *S;         /* the znet event loop handler */
    zn_Timer  *timer;     /* znet timer handler          */
    zn_Tcp    *tcp;       /* znet tcp client handler     */
    zn_Accept *accept;    /* znet tcp service handler    */
    zn_Udp    *udpclient; /* znet udp client handler     */
    zn_Udp    *udpserver; /* znet udp service handler    */
    MyData    *data;      /* our user data pointer       */

    /* we can print out which engine znet uses: */
    printf("znet example: use %s engine.\n", zn_engine());

    /* first, we initialize znet global environment. we needn't do
     * this on *nix or Mac, because on Windows we should initialize
     * and free the WinSocks2 global service, the
     * zn_initialize()/zn_deinitialize() function is prepared for this
     * situation. */
    zn_initialize();

    /* after network environment is ready, we can create a new event
     * loop handler now. the zn_State object is the center object that
     * hold any resource znet uses, when everything overs, you should
     * call zn_close() to clean up all resources znet used. */
    S = zn_newstate();
    if (S == NULL) {
        fprintf(stderr, "create znet handler failured\n");
        return 2; /* error out */
    }

    /* create a znet tcp server */
    accept = zn_newaccept(S, 0);

    /* this server listen to port */
    if (zn_listen(accept, "127.0.0.1", PORT) == ZN_OK) {
        printf("[%p] accept listening to %d ...\n", accept, PORT);
    }

    /* this server and when new connection coming, on_accept()
     * function will be called.
     * the 3rd argument of zn_accept will be send to on_accept as-is.
     * we don't use this pointer here, but will use in when send
     * messages. (all functions that required a callback function
     * pointer all have this user-data pointer */
    zn_accept(accept, on_accept, NULL);

    /* make a timer to close server after 3 seconds. */
    timer = zn_newtimer(S, on_timer, accept);
#if !defined(MANUAL_CLOSE) /* for debug */
    zn_starttimer(timer, 3000);
#endif

    /* now connect to the server we created */
    data = (MyData*)malloc(sizeof(MyData));
    data->idx = 1;
    data->count = 0;
    tcp = zn_newtcp(S);
    zn_connect(tcp, "127.0.0.1", PORT, 0, on_connection, data);

    /* ..., and another one */
    data = (MyData*)malloc(sizeof(MyData));
    data->idx = 2;
    data->count = 0;
    tcp = zn_newtcp(S);
    zn_connect(tcp, "127.0.0.1", PORT, 0, on_connection, data);

    udpserver = zn_newudp(S, "127.0.0.1", 8088);
    udpclient = zn_newudp(S, "127.0.0.1", 0);

    /* now we try to recv packages from udp client. we must prepare a
     * buffer for receive messages, and the buffer should available in
     * the all receiving durations. notice that we send MyData pointer
     * as the context user pointer to read messages from buffer. */
    data = (MyData*)malloc(sizeof(MyData));
    data->count = 0; /* we receive this count times */
    zn_recvfrom(udpserver, data->buffer, MYDATA_BUFLEN,
            on_udp_recv, data);

    /* and now we can send messages to udp now.
     * Notice we use the send_string() macro, so the real call is
     * like:
     * zn_sendto(udpclient, "....", 27, "127.0.0.1", 8088); */
#define UDP_ADDR "127.0.0.1", 8088
    zn_sendto(udpclient, send_string("Hello World From UDP!"), UDP_ADDR);
    /* ... five times. */
    zn_sendto(udpclient, send_string("Hello World From UDP!"), UDP_ADDR);
    zn_sendto(udpclient, send_string("Hello World From UDP!"), UDP_ADDR);
    zn_sendto(udpclient, send_string("Hello World From UDP!"), UDP_ADDR);
    zn_sendto(udpclient, send_string("Hello World From UDP!"), UDP_ADDR);
#undef UDP_ADDR /* done with this macro */

    /* now all prepare work are done. we now run poller to process all
     * subsequent messages. */
    zn_run(S, ZN_RUN_LOOP);

    /* when server dowm (all zn_Accept object are deleted), zn_run()
     * will return, we can cleanup resources in zn_State object now */
    zn_close(S);

    /* and shutdown global environment */
    zn_deinitialize();
    return 0;
}

/* we stop server after 3 seconds. */
zn_Time on_timer(void *ud, zn_Timer *timer, zn_Time elapsed) {
    zn_Accept *accept = (zn_Accept*)ud;
    (void)timer;
    printf("close accept(%p) after 3s (elapsed: %d)\nexit...\n", accept, (int)elapsed);
    zn_delaccept(accept);
    /* return value is the next time the timer callback function
     * called, return 0 means we want delete this timer and don't
     * called again. */
    return 0;
}

/* the server accept callback: when a new connection comes, this
 * function will be called. you must call zn_accept() to notify znet
 * you are done with this connection and ready to accept another
 * connection.  */
void on_accept(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp) {
    (void)ud;

    /* if err is not ZN_OK, we meet errors. simple return. */
    if (err != ZN_OK) {
        fprintf(stderr, "[%p] some bad thing happens to server\n"
                "  when accept connection: %sn", accept, zn_strerror(err));
        return;
    }

    printf("[%p] a new connection(%p) is comming :-)\n", accept, tcp);

    /* Now we sure a real connection comes. when a connection comes,
     * we receive messages from connection.
     * first, we receive something from client, so we need a buffer to
     * hold result: */
    MyData *data = (MyData*)malloc(sizeof(MyData));
    /* OK, send recv request to znet, and when receive done, our
     * on_server_recv() function will be called, with our data
     * pointer. */
    zn_recv(tcp, data->buffer, MYDATA_BUFLEN, on_server_recv, data);

    /* at the same time, we send some greet message to our guest: */
    zn_send(tcp, send_string("welcome to connect our server :)"), on_server_sent, NULL);

    /* now we done with this connection, all subsequent operations
     * will be done in this connection, but not here. we are ready to
     * accept another connection now. */
    zn_accept(accept, on_accept, NULL);
}

void on_server_sent(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    /* send work may error out, we first check the result code: */
    if (err != ZN_OK) {
        fprintf(stderr, "[%p] server meed problems when send something: %s\n",
                tcp, zn_strerror(err));
        return;
    }

    /* now we know that the message are sent to the server, we can
     * send more messages here.
     * Notice that when we send our first greet message, the ud
     * pointer we send is NULL, so we could use this to determine
     * the times we done the send work.  */

    if (ud == NULL) { /* the first time? */
        printf("[%p] first send to client done\n", tcp);
        zn_send(tcp, send_string("this is our second message."),
                on_server_sent, (void*)1);
    } else { /* not the first time? */
        /* do nothing. but a log. */
        printf("[%p] second send to client done (%u bytes)\n", tcp, count);
    }
}

/* when our connection receive something, znet will call this
 * function, but just when we call zn_recv() to tell znet "I'm ready
 * to process in-coming messages!", if you don't want accept messages
 * from client sometime, just do not call zn_recv(), and messages will
 * stay in your OS's buffer. */
void on_server_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    MyData *data = (MyData*)ud; /* our data from zn_recv() */

    /* we expect client close our link by itself. so if a err occurs,
     * maybe it means a connection is over, so we could delete it
     * safely. */
    if (err != ZN_OK) {
        fprintf(stderr, "[%p] error when receiving from client: %s\n",
                tcp, zn_strerror(err));
        /* after error, tcp object will be closed. but you still could
         * use it to do other things, e.g. to connect to other server.
         * but we don't wanna do that now, so put it back to znet.
         * you can also delete it in on_server_sent(), but in here we
         * could free our data.  */
        zn_deltcp(tcp);
        free(data); /* don't forget to free our buffer. */
        return;
    }

    printf("[%p] server receive something from client(%d bytes):"
            " %.*s\n", tcp, (int)count, (int)count, data->buffer);

    /* make another receive process... */
    err = zn_recv(tcp, data->buffer, MYDATA_BUFLEN, on_server_recv, data);
    if (err != ZN_OK) {
        printf("[%p] prepare to receive error: %s\n", tcp, zn_strerror(err));
        zn_deltcp(tcp);
        free(data);
    }
}

/* the client connection callback: when you want to connect other
 * server, and it's done, this function will be called. */
void on_connection(void *ud, zn_Tcp *tcp, unsigned err) {
    MyData *data = (MyData*)ud;
    if (err != ZN_OK) { /* no lucky? let's try again. */
        /* we use ud to find out which time we tried. */
        fprintf(stderr, "[%p] client%d can not connect to server now: %s\n",
                tcp, data->idx, zn_strerror(err));
        if (++data->count < 10) {
            fprintf(stderr, "[%p client%d just try again (%d times)! :-/ \n",
                    tcp, data->idx, data->count);
            zn_connect(tcp, "127.0.0.1", PORT, 0, on_connection, data);
        }
        else {
            fprintf(stderr, "[%p] client%d just give up to connect :-( \n",
                    tcp, data->idx);
            zn_deltcp(tcp);
            free(data);
        }
        return;
    }

    printf("[%p] client%d connected to server now!\n", tcp, data->idx);

    /* now we connect to the server, send something to server.
     * when send done, on_send() is called.  */
    /*zn_send(tcp, send_string("Hello world\n"), on_send, NULL);*/

    /* but, we want not just send one message, but five messages to
     * server. how we know which message we sent done? a idea is
     * setting many callback functions, but the better way is use a
     * context object to hold memories about how many message we sent.
     * */
    data->count = 0;
    zn_send(tcp, send_string("this is the first message from client!"),
            on_client_sent, data);

    /* and we raise a request to make znet check whether server send us
     * something ... */
    zn_recv(tcp, data->buffer, MYDATA_BUFLEN, on_client_recv, data);
}

void on_client_sent(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    MyData *data = (MyData*)ud;

    /* send work may error out, we first check the result code: */
    if (err != ZN_OK) {
        fprintf(stderr, "[%p] client%d meet problem when send something: %s\n",
                tcp, data->idx, zn_strerror(err));
        zn_deltcp(tcp); /* and we close connection. */
        free(data);
        return;
    }

    printf("[%p] client%d send message%d success! (%u bytes)\n",
            tcp, data->idx, data->count, count);
    if (++data->count > 5) {
        printf("[%p] client%d ok, closed!\n", tcp, data->idx);
        zn_deltcp(tcp); /* and we close connection. */
        free(data);
        return;
    }

    printf("[%p] client%d send message%d to server ...\n",
            tcp, data->idx, data->count);
    zn_send(tcp, send_string("message from client..."),
            on_client_sent, data);
}

void on_client_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    MyData *data = (MyData*)ud; /* our data from zn_recv() */

    if (err != ZN_OK) {
        fprintf(stderr, "[%p] client%d meet error when receiving: %s",
                tcp, data->idx, zn_strerror(err));
        return;
    }

    fprintf(stderr, "[%p] client%d received from server(%d bytes): %.*s\n",
            tcp, data->idx, (int)count, (int)count, data->buffer);
}

/* we received messages from other udp ports. */
void on_udp_recv(void *ud, zn_Udp *udp, unsigned err, unsigned count,
        const char *addr, unsigned port) {
    MyData *data = (MyData*)ud;

    if (err != ZN_OK) {
        fprintf(stderr, "[%p] udp meet error when receiving message: %s\n",
                udp, zn_strerror(err));
        zn_deludp(udp);
        return;
    }

    ++data->count;
    printf("[%p] udp received message%d from %s:%d(%d bytes): %.*s\n",
            udp, data->count, addr, port, (int)count, (int)count, data->buffer);

    if (data->count >= 5) {
        zn_deludp(udp);
    } else {
        zn_recvfrom(udp, data->buffer, MYDATA_BUFLEN, on_udp_recv, data);
    }
}

/* cc: flags+='-ggdb -O0' 
 * win32cc: libs+='-lws2_32'
 * linuxcc: libs+='-pthread -lrt' */

