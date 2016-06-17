#ifndef znet_task_h
#define znet_task_h


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

#ifndef ZN_MAX_THREAD_COUNT
# define ZN_MAX_THREAD_COUNT 4
#endif /* ZN_MAX_THREAD_COUNT */


ZN_NS_BEGIN

typedef struct zn_TaskPool zn_TaskPool;

typedef void zn_TaskHandler (void *ud, zn_TaskPool *ws);

ZN_API zn_TaskPool *zn_newtaskpool (int nthread);
ZN_API void         zn_deltaskpool (zn_TaskPool *ws);

ZN_API int zn_taskcount (zn_TaskPool *ws);
ZN_API int zn_addtask   (zn_TaskPool *ws, zn_TaskHandler *h, void *ud);

ZN_API void zn_pausetasks (zn_TaskPool *ws, int pause);


ZN_NS_END

#endif /* znet_task_h */

#if defined(ZN_IMPLEMENTATION) && !defined(zn_task_implemented)
#define zn_task_implemented

ZN_NS_BEGIN


#include <stdlib.h>
#include <string.h>


/* linked list routines */

#ifndef zn_list_h
#define zn_list_h

#define znL_entry(T) T *next; T **pprev
#define znL_head(T)  struct T##_hlist { znL_entry(T); }

#define znL_init(n)                    do { \
    (n)->pprev = &(n)->next;                \
    (n)->next = NULL;                     } while (0)

#define znL_insert(h, n)               do { \
    (n)->pprev = (h);                       \
    (n)->next = *(h);                       \
    if (*(h) != NULL)                       \
        (*(h))->pprev = &(n)->next;         \
    *(h) = (n);                           } while (0)

#define znL_remove(n)                  do { \
    if ((n)->next != NULL)                  \
        (n)->next->pprev = (n)->pprev;      \
    *(n)->pprev = (n)->next;              } while (0)

#define znL_apply(type, h, func)       do { \
    type *tmp_ = (type*)(h);                \
    (h) = NULL;                             \
    while (tmp_) {                          \
        type *next_ = tmp_->next;           \
        func(tmp_);                         \
        tmp_ = next_;                       \
    }                                     } while (0)

#define znQ_entry(T) T* next
#define znQ_type(T)  struct T##_queue { T *first; T **plast; }

#define znQ_init(h)                    do { \
    (h)->first = NULL;                      \
    (h)->plast = &(h)->first;             } while (0)

#define znQ_enqueue(h, n)              do { \
    *(h)->plast = (n);                      \
    (h)->plast = &(n)->next;                \
    (n)->next = NULL;                     } while (0)

#define znQ_dequeue(h, pn)             do { \
    if (((pn) = (h)->first) != NULL) {      \
        (h)->first = (h)->first->next;      \
        if ((h)->plast == &(pn)->next)      \
            (h)->plast = &(h)->first; }     } while (0)

#define znQ_apply(type, h, func)       do { \
    type *tmp_ = (h)->first;                \
    znQ_init(h);                            \
    while (tmp_) {                          \
        type *next_ = tmp_->next;           \
        func(tmp_);                         \
        tmp_ = next_;                       \
    }                                     } while (0)

#endif /* zn_list_h */


#define ZN_WS_NORMAL 0
#define ZN_WS_PAUSE  1
#define ZN_WS_EXIT   2


#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>


typedef struct zn_Task {
    znQ_entry(struct zn_Task);
    void *ud;
    zn_TaskHandler *h;
} zn_Task;

typedef struct zn_TaskPool {
    HANDLE threads[ZN_MAX_THREAD_COUNT];
    HANDLE event;
    CRITICAL_SECTION lock;
    znQ_type(zn_Task) tasks, freed_tasks;
    int nthread;
    int ntask;
    int status;
} zn_TaskPool;

ZN_API int zn_taskcount(zn_TaskPool *ws)
{ return ws->ntask; }

static DWORD WINAPI zn_tasker(LPVOID lpParameter) {
    zn_TaskPool *ws = (zn_TaskPool*)lpParameter;
    zn_Task *task = NULL;
    int status = ZN_WS_NORMAL;

    while (status != ZN_WS_EXIT) {
        if (WaitForSingleObject(ws->event, INFINITE) != WAIT_OBJECT_0)
            return 1;

        EnterCriticalSection(&ws->lock);
        status = ws->status;
        if (status == ZN_WS_EXIT)
            SetEvent(ws->event);
        if (status != ZN_WS_PAUSE)
            znQ_dequeue(&ws->tasks, task);
        LeaveCriticalSection(&ws->lock);

        if (status == ZN_WS_PAUSE)
            continue;

        while (task != NULL) {
            if (task->h)
                task->h(task->ud, ws);

            EnterCriticalSection(&ws->lock);
            --ws->ntask;
            znQ_enqueue(&ws->freed_tasks, task);
            znQ_dequeue(&ws->tasks, task);
            LeaveCriticalSection(&ws->lock);
        }
    }

    return 0;
}

ZN_API zn_TaskPool *zn_newtaskpool(int nthread) {
    int i;
    zn_TaskPool *ws = (zn_TaskPool*)malloc(sizeof(zn_TaskPool));
    if (ws == NULL) return NULL;
    memset(ws, 0, sizeof(*ws));
    if (nthread <= 0 || nthread > ZN_MAX_THREAD_COUNT)
        nthread = ZN_MAX_THREAD_COUNT;
    ws->event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (ws->event == NULL) goto err;
    InitializeCriticalSection(&ws->lock);
    ws->status = ZN_WS_NORMAL;
    ws->nthread = nthread;
    znQ_init(&ws->tasks);
    znQ_init(&ws->freed_tasks);
    for (i = 0; i < nthread; ++i) {
        ws->threads[i] = CreateThread(NULL, 0, &zn_tasker, (LPVOID)ws, 0, NULL);
        if (ws->threads[i] == NULL) {
            if (i == 0) goto err;
            ws->nthread = i;
            break;
        }
    }
    return ws;
err:
    if (ws->event != NULL) CloseHandle(ws->event);
    DeleteCriticalSection(&ws->lock);
    free(ws);
    return NULL;
}

ZN_API void zn_deltaskpool(zn_TaskPool *ws) {
    int i;
    DWORD ret;
    EnterCriticalSection(&ws->lock);
    ws->status = ZN_WS_EXIT;
    SetEvent(ws->event);
    LeaveCriticalSection(&ws->lock);

    ret = WaitForMultipleObjects(ws->nthread, ws->threads, TRUE, INFINITE);
    if (ret >= WAIT_OBJECT_0 && ret < WAIT_OBJECT_0 + ws->nthread) {
        for (i = 0; i < ws->nthread; ++i)
            CloseHandle(ws->threads[i]);
    }
    else {
        for (i = 0; i < ws->nthread; ++i) {
            TerminateThread(ws->threads[i], 0);
            CloseHandle(ws->threads[i]);
        }
    }
    CloseHandle(ws->event);
    DeleteCriticalSection(&ws->lock);
    znQ_apply(zn_Task, &ws->freed_tasks, free);
    znQ_apply(zn_Task, &ws->tasks, free);
    free(ws);
}

ZN_API int zn_addtask(zn_TaskPool *ws, zn_TaskHandler *h, void *ud) {
    int status;
    zn_Task *task = NULL;

    EnterCriticalSection(&ws->lock);
    status = ws->status;
    if (ws->status != ZN_WS_EXIT)
        znQ_dequeue(&ws->freed_tasks, task);
    LeaveCriticalSection(&ws->lock);

    if (status == ZN_WS_EXIT || (task == NULL &&
            (task = (zn_Task*)malloc(sizeof(zn_Task))) == NULL))
        return 0;
    task->h = h;
    task->ud = ud;

    EnterCriticalSection(&ws->lock);
    ++ws->ntask;
    znQ_enqueue(&ws->tasks, task);
    if (ws->status != ZN_WS_PAUSE)
        SetEvent(ws->event);
    LeaveCriticalSection(&ws->lock);
    return 1;
}

ZN_API void zn_pausetasks(zn_TaskPool *ws, int pause) {
    EnterCriticalSection(&ws->lock);
    ws->status = pause ? ZN_WS_PAUSE : ZN_WS_NORMAL;
    LeaveCriticalSection(&ws->lock);
}


#else /* POSIX systems */


#include <pthread.h>

typedef struct zn_Task {
    znQ_entry(struct zn_Task);
    void *ud;
    zn_TaskHandler *h;
} zn_Task;

typedef struct zn_TaskPool {
    pthread_t threads[ZN_MAX_THREAD_COUNT];
    pthread_cond_t event;
    pthread_mutex_t lock;
    znQ_type(zn_Task) tasks, freed_tasks;
    int nthread;
    int ntask;
    int idle_threads;
    int status;
} zn_TaskPool;

ZN_API int zn_taskcount(zn_TaskPool *ws)
{ return ws->ntask; }

static void *zn_tasker(void *ud) {
    zn_TaskPool *ws = (zn_TaskPool*)ud;
    zn_Task *task = NULL;
    int status = ZN_WS_NORMAL;

    while (status != ZN_WS_EXIT) {
        pthread_mutex_lock(&ws->lock);
        for (;;) {
            status = ws->status;
            znQ_dequeue(&ws->tasks, task);
            if (status == ZN_WS_EXIT || task != NULL)
                break;
            ++ws->idle_threads;
            pthread_cond_wait(&ws->event, &ws->lock);
            --ws->idle_threads;
        }
        pthread_mutex_unlock(&ws->lock);

        if (status == ZN_WS_PAUSE)
            continue;

        while (task != NULL) {
            if (task->h)
                task->h(task->ud, ws);

            pthread_mutex_lock(&ws->lock);
            --ws->ntask;
            znQ_enqueue(&ws->freed_tasks, task);
            znQ_dequeue(&ws->tasks, task);
            pthread_mutex_unlock(&ws->lock);
        }
    }

    return NULL;
}

ZN_API zn_TaskPool *zn_newtaskpool(int nthread) {
    int i = 0;
    zn_TaskPool *ws = (zn_TaskPool*)malloc(sizeof(zn_TaskPool));
    if (ws == NULL) return NULL;
    if (nthread <= 0 || nthread > ZN_MAX_THREAD_COUNT)
        nthread = ZN_MAX_THREAD_COUNT;
    memset(ws, 0, sizeof(*ws));
    if (pthread_cond_init(&ws->event, NULL) != 0)
        goto err;
    if (pthread_mutex_init(&ws->lock, NULL) != 0)
        goto err;
    ws->status = ZN_WS_NORMAL;
    ws->nthread = nthread;
    znQ_init(&ws->tasks);
    znQ_init(&ws->freed_tasks);
    for (i = 0; i < nthread; ++i) {
        if (pthread_create(&ws->threads[i], NULL, &zn_tasker, (void*)ws) != 0) {
            if (i == 0) goto err; 
            ws->nthread = i;
            break;
        }
    }
    return ws;
err:
    pthread_cond_destroy(&ws->event);
    pthread_mutex_destroy(&ws->lock);
    free(ws);
    return NULL;
}

ZN_API void zn_deltaskpool(zn_TaskPool *ws) {
    int i;
    pthread_mutex_lock(&ws->lock);
    ws->status = ZN_WS_EXIT;
    pthread_cond_broadcast(&ws->event);
    pthread_mutex_unlock(&ws->lock);

    for (i = 0; i < ws->nthread; ++i) {
        if (pthread_join(ws->threads[i], NULL) != 0)
            pthread_cancel(ws->threads[i]);
    }

    pthread_cond_destroy(&ws->event);
    pthread_mutex_destroy(&ws->lock);
    znQ_apply(zn_Task, &ws->freed_tasks, free);
    znQ_apply(zn_Task, &ws->tasks, free);
    free(ws);
}

ZN_API int zn_addtask(zn_TaskPool *ws, zn_TaskHandler *h, void *ud) {
    int status;
    zn_Task *task = NULL;

    pthread_mutex_lock(&ws->lock);
    status = ws->status;
    if (ws->status != ZN_WS_EXIT)
        znQ_dequeue(&ws->freed_tasks, task);
    pthread_mutex_unlock(&ws->lock);

    if (status == ZN_WS_EXIT || (task == NULL &&
            (task = (zn_Task*)malloc(sizeof(zn_Task))) == NULL))
        return 0;
    task->h = h;
    task->ud = ud;

    pthread_mutex_lock(&ws->lock);
    ++ws->ntask;
    znQ_enqueue(&ws->tasks, task);
    if (ws->idle_threads != 0 && ws->status != ZN_WS_PAUSE)
        pthread_cond_signal(&ws->event);
    pthread_mutex_unlock(&ws->lock);
    return 1;
}

ZN_API void zn_pausetasks(zn_TaskPool *ws, int pause) {
    pthread_mutex_lock(&ws->lock);
    ws->status = pause ? ZN_WS_PAUSE : ZN_WS_NORMAL;
    pthread_mutex_unlock(&ws->lock);
}


#endif


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */
/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * unixcc: flags+='-s -O3 -shared -fPIC -DZN_IMPLEMENTATION -xc' */
