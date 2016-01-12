#ifndef znet_work_h
#define znet_work_h


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

#ifndef ZN_MAX_WORKCOUNT
# define ZN_MAX_WORKCOUNT 4
#endif /* ZN_MAX_WORKCOUNT */


ZN_NS_BEGIN

typedef struct zn_WorkState zn_WorkState;

typedef void zn_WorkHandler (void *ud, zn_WorkState *ws);

ZN_API zn_WorkState *zn_newwork (int nthread);
ZN_API void          zn_delwork (zn_WorkState *ws);

ZN_API int zn_workcount (zn_WorkState *ws);
ZN_API int zn_addwork   (zn_WorkState *ws, zn_WorkHandler *h, void *ud);

ZN_API void zn_enablework (zn_WorkState *ws, int enable);


ZN_NS_END

#endif /* znet_work_h */

#if defined(ZN_IMPLEMENTATION) && !defined(zn_work_implemented)
#define zn_work_implemented

ZN_NS_BEGIN


#include <stdlib.h>


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
    if (((pn) = (h)->first) != NULL)        \
        (h)->first = (h)->first->next;      \
    if ((h)->plast == &(pn)->next)          \
        (h)->plast = &(h)->first;           } while (0)

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


typedef struct zn_Work {
    znQ_entry(struct zn_Work);
    void *ud;
    zn_WorkHandler *h;
} zn_Work;

typedef struct zn_WorkState {
    HANDLE threads[ZN_MAX_WORKCOUNT];
    HANDLE event;
    CRITICAL_SECTION lock;
    znQ_type(zn_Work) works, freed_works;
    int nthread;
    int nwork;
    int status;
} zn_WorkState;

ZN_API int zn_workcount(zn_WorkState *ws)
{ return ws->nwork; }

static DWORD WINAPI zn_worker(LPVOID lpParameter) {
    zn_WorkState *ws = (zn_WorkState*)lpParameter;
    zn_Work *work = NULL;
    int status = ZN_WS_NORMAL;

    while (status != ZN_WS_EXIT) {
        if (WaitForSingleObject(ws->event, INFINITE) != WAIT_OBJECT_0)
            return 1;

        EnterCriticalSection(&ws->lock);
        status = ws->status;
        if (status != ZN_WS_EXIT)
            ResetEvent(ws->event);
        if (status != ZN_WS_PAUSE) {
            znQ_dequeue(&ws->works, work);
            if (work) --ws->nwork;
        }
        LeaveCriticalSection(&ws->lock);

        if (status == ZN_WS_PAUSE)
            continue;

        while (work != NULL) {
            if (work->h)
                work->h(work->ud, ws);

            EnterCriticalSection(&ws->lock);
            znQ_enqueue(&ws->freed_works, work);
            znQ_dequeue(&ws->works, work);
            if (work) --ws->nwork;
            LeaveCriticalSection(&ws->lock);
        }
    }

    return 0;
}

ZN_API zn_WorkState *zn_newwork(int nthread) {
    int i;
    zn_WorkState *ws = (zn_WorkState*)malloc(sizeof(zn_WorkState));
    HANDLE event;
    if (ws == NULL) return NULL;
    if (nthread <= 0 || nthread > ZN_MAX_WORKCOUNT)
        nthread = ZN_MAX_WORKCOUNT;
    event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ws->event != NULL) {
        memset(ws, 0, sizeof(*ws));
        ws->event = event;
        ws->nthread = nthread;
        znQ_init(&ws->works);
        znQ_init(&ws->freed_works);
        InitializeCriticalSection(&ws->lock);
        for (i = 0; i < nthread; ++i)
            ws->threads[i] = CreateThread(NULL, 0, &zn_worker, (LPVOID)ws, 0, NULL);
        return ws;
    }
    free(ws);
    return NULL;
}

ZN_API void zn_delwork(zn_WorkState *ws) {
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
    znQ_apply(zn_Work, &ws->freed_works, free);
    znQ_apply(zn_Work, &ws->works, free);
    free(ws);
}

ZN_API int zn_addwork(zn_WorkState *ws, zn_WorkHandler *h, void *ud) {
    int status;
    zn_Work *work = NULL;

    EnterCriticalSection(&ws->lock);
    status = ws->status;
    if (ws->status != ZN_WS_EXIT)
        znQ_dequeue(&ws->freed_works, work);
    LeaveCriticalSection(&ws->lock);

    if (status == ZN_WS_EXIT || (work == NULL &&
            (work = (zn_Work*)malloc(sizeof(zn_Work))) == NULL))
        return 0;
    work->h = h;
    work->ud = ud;

    EnterCriticalSection(&ws->lock);
    ++ws->nwork;
    znQ_enqueue(&ws->works, work);
    SetEvent(ws->event);
    LeaveCriticalSection(&ws->lock);
    return 1;
}

ZN_API void zn_enablework(zn_WorkState *ws, int enable) {
    EnterCriticalSection(&ws->lock);
    ws->status = enable ? ZN_WS_NORMAL : ZN_WS_PAUSE;
    LeaveCriticalSection(&ws->lock);
}


#elif defined(__linux__)
#else
#endif


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */
/* cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc' */
