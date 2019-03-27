/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/* ======   Dependencies   ======= */
#include <stddef.h>    /* size_t */
#include "debug.h"     /* assert */
#include "zstd_internal.h"  /* ZSTD_malloc, ZSTD_free */
#include "pool.h"

/* ======   Compiler specifics   ====== */
#if defined(_MSC_VER)
#  pragma warning(disable : 4204)        /* disable: C4204: non-constant aggregate initializer */
#endif


#ifdef ZSTD_MULTITHREAD

#include "threading.h"   /* pthread adaptation */

/* A job is a function and an opaque argument */
typedef struct POOL_job_s {
    POOL_function function;
    void *opaque;
} POOL_job;

struct POOL_ctx_s {
    ZSTD_customMem customMem;
    /* Keep track of the threads */
    ZSTD_pthread_t* threads;
    size_t threadCapacity;
    size_t threadLimit;

    /* The queue is a circular buffer */
    POOL_job *queue;
    size_t queueHead;
    size_t queueTail;
    size_t queueSize;

    /* The number of threads working on jobs */
    size_t numThreadsBusy;
    /* Indicates if the queue is empty */
    int queueEmpty;

    /* The mutex protects the queue */
    ZSTD_pthread_mutex_t queueMutex;
    /* Condition variable for pushers to wait on when the queue is full */
    ZSTD_pthread_cond_t queuePushCond;
    /* Condition variables for poppers to wait on when the queue is empty */
    ZSTD_pthread_cond_t queuePopCond;
    /* Indicates if the queue is shutting down */
    int shutdown;
};

/* POOL_thread() :
 * Work thread for the thread pool.
 * Waits for jobs and executes them.
 * @returns : NULL on failure else non-null.
 */
static void* POOL_thread(void* opaque) {
    POOL_ctx* const ctx = (POOL_ctx*)opaque;
    if (!ctx) { return NULL; }
    for (;;) {
        /* Lock the mutex and wait for a non-empty queue or until shutdown */
        ZSTD_pthread_mutex_lock(&ctx->queueMutex);

        while ( ctx->queueEmpty
            || (ctx->numThreadsBusy >= ctx->threadLimit) ) {
            if (ctx->shutdown) {
                /* even if !queueEmpty, (possible if numThreadsBusy >= threadLimit),
                 * a few threads will be shutdown while !queueEmpty,
                 * but enough threads will remain active to finish the queue */
                ZSTD_pthread_mutex_unlock(&ctx->queueMutex);
                return opaque;
            }
            ZSTD_pthread_cond_wait(&ctx->queuePopCond, &ctx->queueMutex);
        }
        /* Pop a job off the queue */
        {   POOL_job const job = ctx->queue[ctx->queueHead];
            ctx->queueHead = (ctx->queueHead + 1) % ctx->queueSize;
            ctx->numThreadsBusy++;
            ctx->queueEmpty = ctx->queueHead == ctx->queueTail;
            /* Unlock the mutex, signal a pusher, and run the job */
            ZSTD_pthread_cond_signal(&ctx->queuePushCond);
            ZSTD_pthread_mutex_unlock(&ctx->queueMutex);

            job.function(job.opaque);

            /* If the intended queue size was 0, signal after finishing job */
            ZSTD_pthread_mutex_lock(&ctx->queueMutex);
            ctx->numThreadsBusy--;
            if (ctx->queueSize == 1) {
                ZSTD_pthread_cond_signal(&ctx->queuePushCond);
            }
            ZSTD_pthread_mutex_unlock(&ctx->queueMutex);
        }
    }  /* for (;;) */
    assert(0);  /* Unreachable */
}

POOL_ctx* POOL_create(size_t numThreads, size_t queueSize) {
    return POOL_create_advanced(numThreads, queueSize, ZSTD_defaultCMem);
}

POOL_ctx* POOL_create_advanced(size_t numThreads, size_t queueSize,
                               ZSTD_customMem customMem) {
    POOL_ctx* ctx;
    /* Check parameters */
    if (!numThreads) { return NULL; }
    /* Allocate the context and zero initialize */
    ctx = (POOL_ctx*)ZSTD_calloc(sizeof(POOL_ctx), customMem);
    if (!ctx) { return NULL; }
    /* Initialize the job queue.
     * It needs one extra space since one space is wasted to differentiate
     * empty and full queues.
     */
    ctx->queueSize = queueSize + 1;
    ctx->queue = (POOL_job*)ZSTD_malloc(ctx->queueSize * sizeof(POOL_job), customMem);
    ctx->queueHead = 0;
    ctx->queueTail = 0;
    ctx->numThreadsBusy = 0;
    ctx->queueEmpty = 1;
    (void)ZSTD_pthread_mutex_init(&ctx->queueMutex, NULL);
    (void)ZSTD_pthread_cond_init(&ctx->queuePushCond, NULL);
    (void)ZSTD_pthread_cond_init(&ctx->queuePopCond, NULL);
    ctx->shutdown = 0;
    /* Allocate space for the thread handles */
    ctx->threads = (ZSTD_pthread_t*)ZSTD_malloc(numThreads * sizeof(ZSTD_pthread_t), customMem);
    ctx->threadCapacity = 0;
    ctx->customMem = customMem;
    /* Check for errors */
    if (!ctx->threads || !ctx->queue) { POOL_free(ctx); return NULL; }
    /* Initialize the threads */
    {   size_t i;
        for (i = 0; i < numThreads; ++i) {
            if (ZSTD_pthread_create(&ctx->threads[i], NULL, &POOL_thread, ctx)) {
                ctx->threadCapacity = i;
                POOL_free(ctx);
                return NULL;
        }   }
        ctx->threadCapacity = numThreads;
        ctx->threadLimit = numThreads;
    }
    return ctx;
}

/*! POOL_join() :
    Shutdown the queue, wake any sleeping threads, and join all of the threads.
*/
static void POOL_join(POOL_ctx* ctx) {
    /* Shut down the queue */
    ZSTD_pthread_mutex_lock(&ctx->queueMutex);
    ctx->shutdown = 1;
    ZSTD_pthread_mutex_unlock(&ctx->queueMutex);
    /* Wake up sleeping threads */
    ZSTD_pthread_cond_broadcast(&ctx->queuePushCond);
    ZSTD_pthread_cond_broadcast(&ctx->queuePopCond);
    /* Join all of the threads */
    {   size_t i;
        for (i = 0; i < ctx->threadCapacity; ++i) {
            ZSTD_pthread_join(ctx->threads[i], NULL);  /* note : could fail */
    }   }
}

void POOL_free(POOL_ctx *ctx) {
    if (!ctx) { return; }
    POOL_join(ctx);
    ZSTD_pthread_mutex_destroy(&ctx->queueMutex);
    ZSTD_pthread_cond_destroy(&ctx->queuePushCond);
    ZSTD_pthread_cond_destroy(&ctx->queuePopCond);
    ZSTD_free(ctx->queue, ctx->customMem);
    ZSTD_free(ctx->threads, ctx->customMem);
    ZSTD_free(ctx, ctx->customMem);
}



size_t POOL_sizeof(POOL_ctx *ctx) {
    if (ctx==NULL) return 0;  /* supports sizeof NULL */
    return sizeof(*ctx)
        + ctx->queueSize * sizeof(POOL_job)
        + ctx->threadCapacity * sizeof(ZSTD_pthread_t);
}


/* @return : 0 on success, 1 on error */
static int POOL_resize_internal(POOL_ctx* ctx, size_t numThreads)
{
    if (numThreads <= ctx->threadCapacity) {
        if (!numThreads) return 1;
        ctx->threadLimit = numThreads;
        return 0;
    }
    /* numThreads > threadCapacity */
    {   ZSTD_pthread_t* const threadPool = (ZSTD_pthread_t*)ZSTD_malloc(numThreads * sizeof(ZSTD_pthread_t), ctx->customMem);
        if (!threadPool) return 1;
        /* replace existing thread pool */
        memcpy(threadPool, ctx->threads, ctx->threadCapacity * sizeof(*threadPool));
        ZSTD_free(ctx->threads, ctx->customMem);
        ctx->threads = threadPool;
        /* Initialize additional threads */
        {   size_t threadId;
            for (threadId = ctx->threadCapacity; threadId < numThreads; ++threadId) {
                if (ZSTD_pthread_create(&threadPool[threadId], NULL, &POOL_thread, ctx)) {
                    ctx->threadCapacity = threadId;
                    return 1;
            }   }
    }   }
    /* successfully expanded */
    ctx->threadCapacity = numThreads;
    ctx->threadLimit = numThreads;
    return 0;
}

/* @return : 0 on success, 1 on error */
int POOL_resize(POOL_ctx* ctx, size_t numThreads)
{
    int result;
    if (ctx==NULL) return 1;
    ZSTD_pthread_mutex_lock(&ctx->queueMutex);
    result = POOL_resize_internal(ctx, numThreads);
    ZSTD_pthread_cond_broadcast(&ctx->queuePopCond);
    ZSTD_pthread_mutex_unlock(&ctx->queueMutex);
    return result;
}

/**
 * Returns 1 if the queue is full and 0 otherwise.
 *
 * When queueSize is 1 (pool was created with an intended queueSize of 0),
 * then a queue is empty if there is a thread free _and_ no job is waiting.
 */
static int isQueueFull(POOL_ctx const* ctx) {
    if (ctx->queueSize > 1) {
        return ctx->queueHead == ((ctx->queueTail + 1) % ctx->queueSize);
    } else {
        return (ctx->numThreadsBusy == ctx->threadLimit) ||
               !ctx->queueEmpty;
    }
}


static void POOL_add_internal(POOL_ctx* ctx, POOL_function function, void *opaque)
{
    POOL_job const job = {function, opaque};
    assert(ctx != NULL);
    if (ctx->shutdown) return;

    ctx->queueEmpty = 0;
    ctx->queue[ctx->queueTail] = job;
    ctx->queueTail = (ctx->queueTail + 1) % ctx->queueSize;
    ZSTD_pthread_cond_signal(&ctx->queuePopCond);
}

void POOL_add(POOL_ctx* ctx, POOL_function function, void* opaque)
{
    assert(ctx != NULL);
    ZSTD_pthread_mutex_lock(&ctx->queueMutex);
    /* Wait until there is space in the queue for the new job */
    while (isQueueFull(ctx) && (!ctx->shutdown)) {
        ZSTD_pthread_cond_wait(&ctx->queuePushCond, &ctx->queueMutex);
    }
    POOL_add_internal(ctx, function, opaque);
    ZSTD_pthread_mutex_unlock(&ctx->queueMutex);
}


int POOL_tryAdd(POOL_ctx* ctx, POOL_function function, void* opaque)
{
    assert(ctx != NULL);
    ZSTD_pthread_mutex_lock(&ctx->queueMutex);
    if (isQueueFull(ctx)) {
        ZSTD_pthread_mutex_unlock(&ctx->queueMutex);
        return 0;
    }
    POOL_add_internal(ctx, function, opaque);
    ZSTD_pthread_mutex_unlock(&ctx->queueMutex);
    return 1;
}


#else  /* ZSTD_MULTITHREAD  not defined */

/* ========================== */
/* No multi-threading support */
/* ========================== */


/* We don't need any data, but if it is empty, malloc() might return NULL. */
struct POOL_ctx_s {
    int dummy;
};
static POOL_ctx g_ctx;

POOL_ctx* POOL_create(size_t numThreads, size_t queueSize) {
    return POOL_create_advanced(numThreads, queueSize, ZSTD_defaultCMem);
}

POOL_ctx* POOL_create_advanced(size_t numThreads, size_t queueSize, ZSTD_customMem customMem) {
    (void)numThreads;
    (void)queueSize;
    (void)customMem;
    return &g_ctx;
}

void POOL_free(POOL_ctx* ctx) {
    assert(!ctx || ctx == &g_ctx);
    (void)ctx;
}

int POOL_resize(POOL_ctx* ctx, size_t numThreads) {
    (void)ctx; (void)numThreads;
    return 0;
}

void POOL_add(POOL_ctx* ctx, POOL_function function, void* opaque) {
    (void)ctx;
    function(opaque);
}

int POOL_tryAdd(POOL_ctx* ctx, POOL_function function, void* opaque) {
    (void)ctx;
    function(opaque);
    return 1;
}

size_t POOL_sizeof(POOL_ctx* ctx) {
    if (ctx==NULL) return 0;  /* supports sizeof NULL */
    assert(ctx == &g_ctx);
    return sizeof(*ctx);
}

#endif  /* ZSTD_MULTITHREAD */
