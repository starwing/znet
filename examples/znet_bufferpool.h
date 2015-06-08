#ifndef zn_bufferpool_h
#define zn_bufferpool_h


#include <stddef.h>
#include <stdlib.h>


#ifndef ZN_BUFFER_FIELDS
# define ZN_BUFFER_FIELDS uintptr_t user_data; 
#endif

typedef struct zn_BufferPoolNode {
    struct zn_BufferPoolNode *next;
    zn_Tcp *tcp;
    ZN_BUFFER_FIELDS
    zn_SendBuffer send;
    zn_RecvBuffer recv;
} zn_BufferPoolNode, *zn_BufferPool;


static void zn_initbuffpool(zn_BufferPool *pool) {
    *pool = NULL;
}

static zn_BufferPoolNode *zn_getbuffer(zn_BufferPool *pool) {
    zn_BufferPoolNode *node = *pool;
    if (node != NULL) {
        *pool = (*pool)->next;
        return node;
    }
    node = (zn_BufferPoolNode*)malloc(sizeof(zn_BufferPoolNode));
    node->next = NULL;
    node->tcp = NULL;
    zn_initrecvbuffer(&node->recv);
    zn_initsendbuffer(&node->send);
    return node;
}

static void zn_putbuffer(zn_BufferPool *pool, zn_BufferPoolNode *node) {
    node->next = *pool;
    *pool = node;
}


#endif /* zn_bufferpool_h */
