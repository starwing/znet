#ifndef zn_deque_h
#define zn_deque_h


#ifndef ZN_NS_BEGIN
# ifdef __cplusplus
#   define ZN_NS_BEGIN extern "C" {
#   define ZN_NS_END   }
# else
#   define ZN_NS_BEGIN
#   define ZN_NS_END
# endif
#endif /* ZN_NS_BEGIN */

#if !defined(ZN_API) && defined(_WIN32)
# ifdef ZN_IMPLEMENTATION
#  define ZN_API __declspec(dllexport)
# else
#  define ZN_API __declspec(dllimport)
# endif
#endif

#ifndef ZN_API
# define ZN_API extern
#endif

#include <stddef.h>


ZN_NS_BEGIN


typedef struct zn_Deque     zn_Deque;
typedef struct zn_DequeItem zn_DequeItem;

typedef void zn_DequeHandler(void *ud, zn_DequeItem *item);

ZN_API zn_Deque *zn_newdeque (void);
ZN_API void      zn_deldeque (zn_Deque *d, zn_DequeItem *head);

ZN_API void zn_pushfront (zn_Deque *d, zn_DequeItem *item);
ZN_API void zn_pushback  (zn_Deque *d, zn_DequeItem *item);

ZN_API zn_DequeItem *zn_front (zn_Deque *d);
ZN_API zn_DequeItem *zn_back  (zn_Deque *d);

ZN_API zn_DequeItem *zn_popfront (zn_Deque *d, int waitms);
ZN_API zn_DequeItem *zn_popback  (zn_Deque *d, int waitms);


/* deque item routines */

ZN_API zn_DequeItem *zn_newitem (zn_Deque *d, size_t size);
ZN_API void          zn_delitem (zn_DequeItem *item);

ZN_API void zn_inititem   (zn_DequeItem *item);
ZN_API void zn_detachitem (zn_DequeItem *item);

ZN_API void zn_cleardeque (zn_Deque *d, zn_DequeItem *head);
ZN_API void zn_visititems (zn_DequeItem *head, zn_DequeHandler *h, void *ud);

struct zn_DequeItem {
    zn_DequeItem *next;
    zn_DequeItem *prev;
    zn_Deque *deque;
};


ZN_NS_END

#endif /* zn_deque_h */


#if defined(ZN_IMPLEMENTATION) && !defined(zn_deque_implemented)
#define zn_deque_implemented


#include <stdlib.h>

ZN_NS_BEGIN


/* platform independency routines */

ZN_API void zn_inititem(zn_DequeItem *item)
{ item->next = item->prev = item; item->deque = NULL; }

static void znD_detach(zn_DequeItem *item) {
    if (item != NULL) {
        item->prev->next = item->next;
        item->next->prev = item->prev;
        item->prev = item->next = item;
        item->deque = NULL;
    }
}

static void znD_insert(zn_DequeItem *head, zn_DequeItem *item) {
    item->next = head;
    item->prev = head->prev;
    head->prev->next = item;
    head->prev = item;
}

ZN_API zn_DequeItem *zn_newitem(zn_Deque *d, size_t size) {
    zn_DequeItem *item;
    if (size < sizeof(zn_DequeItem))
        size = sizeof(zn_DequeItem);
    item = (zn_DequeItem*)malloc(size);
    if (item == NULL) return NULL;
    zn_inititem(item);
    item->deque = d;
    return item;
}

ZN_API void zn_delitem(zn_DequeItem *item) {
    zn_detachitem(item);
    free(item);
}

ZN_API void zn_visititems(zn_DequeItem *head, zn_DequeHandler *h, void *ud) {
    zn_DequeItem *i, *next;
    for (i = head->next, next = i->next;
            i != head;
            i = next, next = next->next)
        h(ud, i);
}


#define ZN_DS_NORMAL 0
#define ZN_DS_EXIT   1


#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif /* WIN32_LEAN_AND_MEAN */

#include <Windows.h>

struct zn_Deque {
    zn_DequeItem head;
    CRITICAL_SECTION lock;
    HANDLE event;
    int wait_threads;
    int status;
};

ZN_API zn_Deque *zn_newdeque(void) {
    zn_Deque *d = (zn_Deque*)malloc(sizeof(zn_Deque));
    if (d == NULL) return NULL;
    d->event = CreateSemaphore(NULL, 0, LONG_MAX, 0);
    if (d->event == NULL) {
        free(d);
        return NULL;
    }
    d->status = ZN_DS_NORMAL;
    d->head.deque = d;
    d->wait_threads = 0;
    zn_inititem(&d->head);
    InitializeCriticalSection(&d->lock);
    return d;
}

ZN_API void zn_deldeque(zn_Deque *d, zn_DequeItem *head) {
    int wait_threads;
    EnterCriticalSection(&d->lock);
    d->status = ZN_DS_EXIT;
    wait_threads = d->wait_threads;
    ReleaseSemaphore(d->event, wait_threads, NULL);
    LeaveCriticalSection(&d->lock);

    while (wait_threads != 0) {
        EnterCriticalSection(&d->lock);
        wait_threads = d->wait_threads;
        LeaveCriticalSection(&d->lock);
    }

    EnterCriticalSection(&d->lock);
    if (head) *head = d->head;
    zn_inititem(&d->head);
    LeaveCriticalSection(&d->lock);

    CloseHandle(d->event);
    DeleteCriticalSection(&d->lock);
    free(d);
}

static zn_DequeItem *zn_pop(zn_Deque *d, int waitms, zn_DequeItem *(*f)(zn_Deque *d)) {
    int status, should_wait = 0;
    zn_DequeItem *item;
    EnterCriticalSection(&d->lock);
    status = d->status;
    item = f(d);
    znD_detach(item);
    if (item == NULL && waitms != 0 && status == ZN_DS_NORMAL) {
        should_wait = 1;
        ++d->wait_threads;
    }
    LeaveCriticalSection(&d->lock);
    if (should_wait) {
        DWORD time = waitms >= 0 ? (DWORD)waitms : INFINITE;
        WaitForSingleObject(d->event, time);

        EnterCriticalSection(&d->lock);
        --d->wait_threads;
        item = f(d);
        znD_detach(item);
        LeaveCriticalSection(&d->lock);
    }
    return item;
}

static void znD_lock(zn_Deque *d)   { EnterCriticalSection(&d->lock); }
static void znD_unlock(zn_Deque *d) { LeaveCriticalSection(&d->lock); }
static void znD_signal(zn_Deque *d) { ReleaseSemaphore(d->event, 1, NULL); }

#else

#include <pthread.h>

struct zn_Deque {
    zn_DequeItem head;
    pthread_mutex_t lock;
    pthread_cond_t event;
    int wait_threads;
    int status;
};

ZN_API zn_Deque *zn_newdeque(void) {
    zn_Deque *d = (zn_Deque*)malloc(sizeof(zn_Deque));
    if (d == NULL) return NULL;
    if (pthread_mutex_init(&d->lock, NULL) != 0) goto err;
    if (pthread_cond_init(&d->event, NULL) != 0) goto err;
    d->status = ZN_DS_NORMAL;
    d->head.deque = d;
    d->wait_threads = 0;
    zn_inititem(&d->head);
    return d;
err:
    free(d);
    return NULL;
}

ZN_API void zn_deldeque(zn_Deque *d, zn_DequeItem *head) {
    int wait_threads;
    pthread_mutex_lock(&d->lock);
    d->status = ZN_DS_EXIT;
    wait_threads = d->wait_threads;
    pthread_mutex_unlock(&d->lock);

    while (wait_threads != 0) {
        pthread_mutex_lock(&d->lock);
        pthread_cond_broadcast(&d->event);
        wait_threads = d->wait_threads;
        pthread_mutex_unlock(&d->lock);
    }

    if (head) *head = d->head;
    zn_inititem(&d->head);
    pthread_mutex_unlock(&d->lock);
    pthread_cond_destroy(&d->event);
    pthread_mutex_destroy(&d->lock);
    free(d);
}

static zn_DequeItem *zn_pop(zn_Deque *d, int waitms, zn_DequeItem *(*f)(zn_Deque *d)) {
    int status;
    zn_DequeItem *item;
    pthread_mutex_lock(&d->lock);
    status = d->status;
    item = f(d);
    znD_detach(item);
    if (item == NULL && status == ZN_DS_NORMAL && waitms != 0) {
        ++d->wait_threads;
        if (waitms < 0)
            pthread_cond_wait(&d->event, &d->lock);
        else {
            struct timespec ts;
            ts.tv_sec  =  waitms / 1000;
            ts.tv_nsec = (waitms % 1000) * 1000000;
            pthread_cond_timedwait(&d->event, &d->lock, &ts);
        }
        --d->wait_threads;

        item = f(d);
        znD_detach(item);
    }
    pthread_mutex_unlock(&d->lock);
    return item;
}

static void znD_lock(zn_Deque *d)   { pthread_mutex_lock(&d->lock); }
static void znD_unlock(zn_Deque *d) { pthread_mutex_unlock(&d->lock); }
static void znD_signal(zn_Deque *d) { pthread_cond_signal(&d->event); }

#endif


/* generated locked routines */

ZN_API zn_DequeItem *zn_popfront(zn_Deque *d, int waitms)
{ return zn_pop(d, waitms, znD_frontU); }

ZN_API zn_DequeItem *zn_popback(zn_Deque *d, int waitms)
{ return zn_pop(d, waitms, znD_backU); }

static zn_DequeItem *znD_frontU(zn_Deque *d) {
    zn_DequeItem *item = d->head.next;
    if (item == &d->head)
        return NULL;
    return item;
}

static zn_DequeItem *znD_backU(zn_Deque *d) {
    zn_DequeItem *item = d->head.prev;
    if (item == &d->head)
        return NULL;
    return item;
}

ZN_API void zn_cleardeque(zn_Deque *d, zn_DequeItem *head) {
    znD_lock(d);
    if (head) *head = d->head;
    zn_inititem(&d->head);
    znD_unlock(d);
}

ZN_API void zn_detachitem(zn_DequeItem *item) {
    if (item) {
        zn_Deque *d = item->deque;
        if (d == NULL) return;
        znD_lock(d);
        if (item->deque == d)
            znD_detach(item);
        znD_unlock(d);
    }
}

ZN_API void zn_pushfront(zn_Deque *d, zn_DequeItem *item) {
    if (d->status == ZN_DS_EXIT || item->deque != NULL) return;
    znD_lock(d);
    item->deque = d;
    znD_insert(d->head.next, item);
    znD_signal(d);
    znD_unlock(d);
}

ZN_API void zn_pushback(zn_Deque *d, zn_DequeItem *item) {
    if (d->status == ZN_DS_EXIT || item->deque != NULL) return;
    znD_lock(d);
    item->deque = d;
    znD_insert(&d->head, item);
    znD_signal(d);
    znD_unlock(d);
}

ZN_API zn_DequeItem *zn_front(zn_Deque *d) {
    zn_DequeItem *item;
    znD_lock(d);
    item = znD_frontU(d);
    znD_unlock(d);
    return item;
}

ZN_API zn_DequeItem *zn_back(zn_Deque *d) {
    zn_DequeItem *item;
    znD_lock(d);
    item = znD_backU(d);
    znD_unlock(d);
    return item;
}


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */

/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc' output='zn_deque.dll'
   unixcc: flags+='-O3 -shared -fPIC -DZN_IMPLEMENTATION -xc' output='zn_deque.so' */

