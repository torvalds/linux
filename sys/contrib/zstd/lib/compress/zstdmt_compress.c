/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/* ======   Compiler specifics   ====== */
#if defined(_MSC_VER)
#  pragma warning(disable : 4204)   /* disable: C4204: non-constant aggregate initializer */
#endif


/* ======   Constants   ====== */
#define ZSTDMT_OVERLAPLOG_DEFAULT 0


/* ======   Dependencies   ====== */
#include <string.h>      /* memcpy, memset */
#include <limits.h>      /* INT_MAX, UINT_MAX */
#include "pool.h"        /* threadpool */
#include "threading.h"   /* mutex */
#include "zstd_compress_internal.h"  /* MIN, ERROR, ZSTD_*, ZSTD_highbit32 */
#include "zstd_ldm.h"
#include "zstdmt_compress.h"

/* Guards code to support resizing the SeqPool.
 * We will want to resize the SeqPool to save memory in the future.
 * Until then, comment the code out since it is unused.
 */
#define ZSTD_RESIZE_SEQPOOL 0

/* ======   Debug   ====== */
#if defined(DEBUGLEVEL) && (DEBUGLEVEL>=2) \
    && !defined(_MSC_VER) \
    && !defined(__MINGW32__)

#  include <stdio.h>
#  include <unistd.h>
#  include <sys/times.h>

#  define DEBUG_PRINTHEX(l,p,n) {            \
    unsigned debug_u;                        \
    for (debug_u=0; debug_u<(n); debug_u++)  \
        RAWLOG(l, "%02X ", ((const unsigned char*)(p))[debug_u]); \
    RAWLOG(l, " \n");                        \
}

static unsigned long long GetCurrentClockTimeMicroseconds(void)
{
   static clock_t _ticksPerSecond = 0;
   if (_ticksPerSecond <= 0) _ticksPerSecond = sysconf(_SC_CLK_TCK);

   {   struct tms junk; clock_t newTicks = (clock_t) times(&junk);
       return ((((unsigned long long)newTicks)*(1000000))/_ticksPerSecond);
}  }

#define MUTEX_WAIT_TIME_DLEVEL 6
#define ZSTD_PTHREAD_MUTEX_LOCK(mutex) {          \
    if (DEBUGLEVEL >= MUTEX_WAIT_TIME_DLEVEL) {   \
        unsigned long long const beforeTime = GetCurrentClockTimeMicroseconds(); \
        ZSTD_pthread_mutex_lock(mutex);           \
        {   unsigned long long const afterTime = GetCurrentClockTimeMicroseconds(); \
            unsigned long long const elapsedTime = (afterTime-beforeTime); \
            if (elapsedTime > 1000) {  /* or whatever threshold you like; I'm using 1 millisecond here */ \
                DEBUGLOG(MUTEX_WAIT_TIME_DLEVEL, "Thread took %llu microseconds to acquire mutex %s \n", \
                   elapsedTime, #mutex);          \
        }   }                                     \
    } else {                                      \
        ZSTD_pthread_mutex_lock(mutex);           \
    }                                             \
}

#else

#  define ZSTD_PTHREAD_MUTEX_LOCK(m) ZSTD_pthread_mutex_lock(m)
#  define DEBUG_PRINTHEX(l,p,n) {}

#endif


/* =====   Buffer Pool   ===== */
/* a single Buffer Pool can be invoked from multiple threads in parallel */

typedef struct buffer_s {
    void* start;
    size_t capacity;
} buffer_t;

static const buffer_t g_nullBuffer = { NULL, 0 };

typedef struct ZSTDMT_bufferPool_s {
    ZSTD_pthread_mutex_t poolMutex;
    size_t bufferSize;
    unsigned totalBuffers;
    unsigned nbBuffers;
    ZSTD_customMem cMem;
    buffer_t bTable[1];   /* variable size */
} ZSTDMT_bufferPool;

static ZSTDMT_bufferPool* ZSTDMT_createBufferPool(unsigned nbWorkers, ZSTD_customMem cMem)
{
    unsigned const maxNbBuffers = 2*nbWorkers + 3;
    ZSTDMT_bufferPool* const bufPool = (ZSTDMT_bufferPool*)ZSTD_calloc(
        sizeof(ZSTDMT_bufferPool) + (maxNbBuffers-1) * sizeof(buffer_t), cMem);
    if (bufPool==NULL) return NULL;
    if (ZSTD_pthread_mutex_init(&bufPool->poolMutex, NULL)) {
        ZSTD_free(bufPool, cMem);
        return NULL;
    }
    bufPool->bufferSize = 64 KB;
    bufPool->totalBuffers = maxNbBuffers;
    bufPool->nbBuffers = 0;
    bufPool->cMem = cMem;
    return bufPool;
}

static void ZSTDMT_freeBufferPool(ZSTDMT_bufferPool* bufPool)
{
    unsigned u;
    DEBUGLOG(3, "ZSTDMT_freeBufferPool (address:%08X)", (U32)(size_t)bufPool);
    if (!bufPool) return;   /* compatibility with free on NULL */
    for (u=0; u<bufPool->totalBuffers; u++) {
        DEBUGLOG(4, "free buffer %2u (address:%08X)", u, (U32)(size_t)bufPool->bTable[u].start);
        ZSTD_free(bufPool->bTable[u].start, bufPool->cMem);
    }
    ZSTD_pthread_mutex_destroy(&bufPool->poolMutex);
    ZSTD_free(bufPool, bufPool->cMem);
}

/* only works at initialization, not during compression */
static size_t ZSTDMT_sizeof_bufferPool(ZSTDMT_bufferPool* bufPool)
{
    size_t const poolSize = sizeof(*bufPool)
                          + (bufPool->totalBuffers - 1) * sizeof(buffer_t);
    unsigned u;
    size_t totalBufferSize = 0;
    ZSTD_pthread_mutex_lock(&bufPool->poolMutex);
    for (u=0; u<bufPool->totalBuffers; u++)
        totalBufferSize += bufPool->bTable[u].capacity;
    ZSTD_pthread_mutex_unlock(&bufPool->poolMutex);

    return poolSize + totalBufferSize;
}

/* ZSTDMT_setBufferSize() :
 * all future buffers provided by this buffer pool will have _at least_ this size
 * note : it's better for all buffers to have same size,
 * as they become freely interchangeable, reducing malloc/free usages and memory fragmentation */
static void ZSTDMT_setBufferSize(ZSTDMT_bufferPool* const bufPool, size_t const bSize)
{
    ZSTD_pthread_mutex_lock(&bufPool->poolMutex);
    DEBUGLOG(4, "ZSTDMT_setBufferSize: bSize = %u", (U32)bSize);
    bufPool->bufferSize = bSize;
    ZSTD_pthread_mutex_unlock(&bufPool->poolMutex);
}


static ZSTDMT_bufferPool* ZSTDMT_expandBufferPool(ZSTDMT_bufferPool* srcBufPool, U32 nbWorkers)
{
    unsigned const maxNbBuffers = 2*nbWorkers + 3;
    if (srcBufPool==NULL) return NULL;
    if (srcBufPool->totalBuffers >= maxNbBuffers) /* good enough */
        return srcBufPool;
    /* need a larger buffer pool */
    {   ZSTD_customMem const cMem = srcBufPool->cMem;
        size_t const bSize = srcBufPool->bufferSize;   /* forward parameters */
        ZSTDMT_bufferPool* newBufPool;
        ZSTDMT_freeBufferPool(srcBufPool);
        newBufPool = ZSTDMT_createBufferPool(nbWorkers, cMem);
        if (newBufPool==NULL) return newBufPool;
        ZSTDMT_setBufferSize(newBufPool, bSize);
        return newBufPool;
    }
}

/** ZSTDMT_getBuffer() :
 *  assumption : bufPool must be valid
 * @return : a buffer, with start pointer and size
 *  note: allocation may fail, in this case, start==NULL and size==0 */
static buffer_t ZSTDMT_getBuffer(ZSTDMT_bufferPool* bufPool)
{
    size_t const bSize = bufPool->bufferSize;
    DEBUGLOG(5, "ZSTDMT_getBuffer: bSize = %u", (U32)bufPool->bufferSize);
    ZSTD_pthread_mutex_lock(&bufPool->poolMutex);
    if (bufPool->nbBuffers) {   /* try to use an existing buffer */
        buffer_t const buf = bufPool->bTable[--(bufPool->nbBuffers)];
        size_t const availBufferSize = buf.capacity;
        bufPool->bTable[bufPool->nbBuffers] = g_nullBuffer;
        if ((availBufferSize >= bSize) & ((availBufferSize>>3) <= bSize)) {
            /* large enough, but not too much */
            DEBUGLOG(5, "ZSTDMT_getBuffer: provide buffer %u of size %u",
                        bufPool->nbBuffers, (U32)buf.capacity);
            ZSTD_pthread_mutex_unlock(&bufPool->poolMutex);
            return buf;
        }
        /* size conditions not respected : scratch this buffer, create new one */
        DEBUGLOG(5, "ZSTDMT_getBuffer: existing buffer does not meet size conditions => freeing");
        ZSTD_free(buf.start, bufPool->cMem);
    }
    ZSTD_pthread_mutex_unlock(&bufPool->poolMutex);
    /* create new buffer */
    DEBUGLOG(5, "ZSTDMT_getBuffer: create a new buffer");
    {   buffer_t buffer;
        void* const start = ZSTD_malloc(bSize, bufPool->cMem);
        buffer.start = start;   /* note : start can be NULL if malloc fails ! */
        buffer.capacity = (start==NULL) ? 0 : bSize;
        if (start==NULL) {
            DEBUGLOG(5, "ZSTDMT_getBuffer: buffer allocation failure !!");
        } else {
            DEBUGLOG(5, "ZSTDMT_getBuffer: created buffer of size %u", (U32)bSize);
        }
        return buffer;
    }
}

#if ZSTD_RESIZE_SEQPOOL
/** ZSTDMT_resizeBuffer() :
 * assumption : bufPool must be valid
 * @return : a buffer that is at least the buffer pool buffer size.
 *           If a reallocation happens, the data in the input buffer is copied.
 */
static buffer_t ZSTDMT_resizeBuffer(ZSTDMT_bufferPool* bufPool, buffer_t buffer)
{
    size_t const bSize = bufPool->bufferSize;
    if (buffer.capacity < bSize) {
        void* const start = ZSTD_malloc(bSize, bufPool->cMem);
        buffer_t newBuffer;
        newBuffer.start = start;
        newBuffer.capacity = start == NULL ? 0 : bSize;
        if (start != NULL) {
            assert(newBuffer.capacity >= buffer.capacity);
            memcpy(newBuffer.start, buffer.start, buffer.capacity);
            DEBUGLOG(5, "ZSTDMT_resizeBuffer: created buffer of size %u", (U32)bSize);
            return newBuffer;
        }
        DEBUGLOG(5, "ZSTDMT_resizeBuffer: buffer allocation failure !!");
    }
    return buffer;
}
#endif

/* store buffer for later re-use, up to pool capacity */
static void ZSTDMT_releaseBuffer(ZSTDMT_bufferPool* bufPool, buffer_t buf)
{
    DEBUGLOG(5, "ZSTDMT_releaseBuffer");
    if (buf.start == NULL) return;   /* compatible with release on NULL */
    ZSTD_pthread_mutex_lock(&bufPool->poolMutex);
    if (bufPool->nbBuffers < bufPool->totalBuffers) {
        bufPool->bTable[bufPool->nbBuffers++] = buf;  /* stored for later use */
        DEBUGLOG(5, "ZSTDMT_releaseBuffer: stored buffer of size %u in slot %u",
                    (U32)buf.capacity, (U32)(bufPool->nbBuffers-1));
        ZSTD_pthread_mutex_unlock(&bufPool->poolMutex);
        return;
    }
    ZSTD_pthread_mutex_unlock(&bufPool->poolMutex);
    /* Reached bufferPool capacity (should not happen) */
    DEBUGLOG(5, "ZSTDMT_releaseBuffer: pool capacity reached => freeing ");
    ZSTD_free(buf.start, bufPool->cMem);
}


/* =====   Seq Pool Wrapper   ====== */

static rawSeqStore_t kNullRawSeqStore = {NULL, 0, 0, 0};

typedef ZSTDMT_bufferPool ZSTDMT_seqPool;

static size_t ZSTDMT_sizeof_seqPool(ZSTDMT_seqPool* seqPool)
{
    return ZSTDMT_sizeof_bufferPool(seqPool);
}

static rawSeqStore_t bufferToSeq(buffer_t buffer)
{
    rawSeqStore_t seq = {NULL, 0, 0, 0};
    seq.seq = (rawSeq*)buffer.start;
    seq.capacity = buffer.capacity / sizeof(rawSeq);
    return seq;
}

static buffer_t seqToBuffer(rawSeqStore_t seq)
{
    buffer_t buffer;
    buffer.start = seq.seq;
    buffer.capacity = seq.capacity * sizeof(rawSeq);
    return buffer;
}

static rawSeqStore_t ZSTDMT_getSeq(ZSTDMT_seqPool* seqPool)
{
    if (seqPool->bufferSize == 0) {
        return kNullRawSeqStore;
    }
    return bufferToSeq(ZSTDMT_getBuffer(seqPool));
}

#if ZSTD_RESIZE_SEQPOOL
static rawSeqStore_t ZSTDMT_resizeSeq(ZSTDMT_seqPool* seqPool, rawSeqStore_t seq)
{
  return bufferToSeq(ZSTDMT_resizeBuffer(seqPool, seqToBuffer(seq)));
}
#endif

static void ZSTDMT_releaseSeq(ZSTDMT_seqPool* seqPool, rawSeqStore_t seq)
{
  ZSTDMT_releaseBuffer(seqPool, seqToBuffer(seq));
}

static void ZSTDMT_setNbSeq(ZSTDMT_seqPool* const seqPool, size_t const nbSeq)
{
  ZSTDMT_setBufferSize(seqPool, nbSeq * sizeof(rawSeq));
}

static ZSTDMT_seqPool* ZSTDMT_createSeqPool(unsigned nbWorkers, ZSTD_customMem cMem)
{
    ZSTDMT_seqPool* const seqPool = ZSTDMT_createBufferPool(nbWorkers, cMem);
    if (seqPool == NULL) return NULL;
    ZSTDMT_setNbSeq(seqPool, 0);
    return seqPool;
}

static void ZSTDMT_freeSeqPool(ZSTDMT_seqPool* seqPool)
{
    ZSTDMT_freeBufferPool(seqPool);
}

static ZSTDMT_seqPool* ZSTDMT_expandSeqPool(ZSTDMT_seqPool* pool, U32 nbWorkers)
{
    return ZSTDMT_expandBufferPool(pool, nbWorkers);
}


/* =====   CCtx Pool   ===== */
/* a single CCtx Pool can be invoked from multiple threads in parallel */

typedef struct {
    ZSTD_pthread_mutex_t poolMutex;
    int totalCCtx;
    int availCCtx;
    ZSTD_customMem cMem;
    ZSTD_CCtx* cctx[1];   /* variable size */
} ZSTDMT_CCtxPool;

/* note : all CCtx borrowed from the pool should be released back to the pool _before_ freeing the pool */
static void ZSTDMT_freeCCtxPool(ZSTDMT_CCtxPool* pool)
{
    int cid;
    for (cid=0; cid<pool->totalCCtx; cid++)
        ZSTD_freeCCtx(pool->cctx[cid]);  /* note : compatible with free on NULL */
    ZSTD_pthread_mutex_destroy(&pool->poolMutex);
    ZSTD_free(pool, pool->cMem);
}

/* ZSTDMT_createCCtxPool() :
 * implies nbWorkers >= 1 , checked by caller ZSTDMT_createCCtx() */
static ZSTDMT_CCtxPool* ZSTDMT_createCCtxPool(int nbWorkers,
                                              ZSTD_customMem cMem)
{
    ZSTDMT_CCtxPool* const cctxPool = (ZSTDMT_CCtxPool*) ZSTD_calloc(
        sizeof(ZSTDMT_CCtxPool) + (nbWorkers-1)*sizeof(ZSTD_CCtx*), cMem);
    assert(nbWorkers > 0);
    if (!cctxPool) return NULL;
    if (ZSTD_pthread_mutex_init(&cctxPool->poolMutex, NULL)) {
        ZSTD_free(cctxPool, cMem);
        return NULL;
    }
    cctxPool->cMem = cMem;
    cctxPool->totalCCtx = nbWorkers;
    cctxPool->availCCtx = 1;   /* at least one cctx for single-thread mode */
    cctxPool->cctx[0] = ZSTD_createCCtx_advanced(cMem);
    if (!cctxPool->cctx[0]) { ZSTDMT_freeCCtxPool(cctxPool); return NULL; }
    DEBUGLOG(3, "cctxPool created, with %u workers", nbWorkers);
    return cctxPool;
}

static ZSTDMT_CCtxPool* ZSTDMT_expandCCtxPool(ZSTDMT_CCtxPool* srcPool,
                                              int nbWorkers)
{
    if (srcPool==NULL) return NULL;
    if (nbWorkers <= srcPool->totalCCtx) return srcPool;   /* good enough */
    /* need a larger cctx pool */
    {   ZSTD_customMem const cMem = srcPool->cMem;
        ZSTDMT_freeCCtxPool(srcPool);
        return ZSTDMT_createCCtxPool(nbWorkers, cMem);
    }
}

/* only works during initialization phase, not during compression */
static size_t ZSTDMT_sizeof_CCtxPool(ZSTDMT_CCtxPool* cctxPool)
{
    ZSTD_pthread_mutex_lock(&cctxPool->poolMutex);
    {   unsigned const nbWorkers = cctxPool->totalCCtx;
        size_t const poolSize = sizeof(*cctxPool)
                                + (nbWorkers-1) * sizeof(ZSTD_CCtx*);
        unsigned u;
        size_t totalCCtxSize = 0;
        for (u=0; u<nbWorkers; u++) {
            totalCCtxSize += ZSTD_sizeof_CCtx(cctxPool->cctx[u]);
        }
        ZSTD_pthread_mutex_unlock(&cctxPool->poolMutex);
        assert(nbWorkers > 0);
        return poolSize + totalCCtxSize;
    }
}

static ZSTD_CCtx* ZSTDMT_getCCtx(ZSTDMT_CCtxPool* cctxPool)
{
    DEBUGLOG(5, "ZSTDMT_getCCtx");
    ZSTD_pthread_mutex_lock(&cctxPool->poolMutex);
    if (cctxPool->availCCtx) {
        cctxPool->availCCtx--;
        {   ZSTD_CCtx* const cctx = cctxPool->cctx[cctxPool->availCCtx];
            ZSTD_pthread_mutex_unlock(&cctxPool->poolMutex);
            return cctx;
    }   }
    ZSTD_pthread_mutex_unlock(&cctxPool->poolMutex);
    DEBUGLOG(5, "create one more CCtx");
    return ZSTD_createCCtx_advanced(cctxPool->cMem);   /* note : can be NULL, when creation fails ! */
}

static void ZSTDMT_releaseCCtx(ZSTDMT_CCtxPool* pool, ZSTD_CCtx* cctx)
{
    if (cctx==NULL) return;   /* compatibility with release on NULL */
    ZSTD_pthread_mutex_lock(&pool->poolMutex);
    if (pool->availCCtx < pool->totalCCtx)
        pool->cctx[pool->availCCtx++] = cctx;
    else {
        /* pool overflow : should not happen, since totalCCtx==nbWorkers */
        DEBUGLOG(4, "CCtx pool overflow : free cctx");
        ZSTD_freeCCtx(cctx);
    }
    ZSTD_pthread_mutex_unlock(&pool->poolMutex);
}

/* ====   Serial State   ==== */

typedef struct {
    void const* start;
    size_t size;
} range_t;

typedef struct {
    /* All variables in the struct are protected by mutex. */
    ZSTD_pthread_mutex_t mutex;
    ZSTD_pthread_cond_t cond;
    ZSTD_CCtx_params params;
    ldmState_t ldmState;
    XXH64_state_t xxhState;
    unsigned nextJobID;
    /* Protects ldmWindow.
     * Must be acquired after the main mutex when acquiring both.
     */
    ZSTD_pthread_mutex_t ldmWindowMutex;
    ZSTD_pthread_cond_t ldmWindowCond;  /* Signaled when ldmWindow is udpated */
    ZSTD_window_t ldmWindow;  /* A thread-safe copy of ldmState.window */
} serialState_t;

static int ZSTDMT_serialState_reset(serialState_t* serialState, ZSTDMT_seqPool* seqPool, ZSTD_CCtx_params params, size_t jobSize)
{
    /* Adjust parameters */
    if (params.ldmParams.enableLdm) {
        DEBUGLOG(4, "LDM window size = %u KB", (1U << params.cParams.windowLog) >> 10);
        ZSTD_ldm_adjustParameters(&params.ldmParams, &params.cParams);
        assert(params.ldmParams.hashLog >= params.ldmParams.bucketSizeLog);
        assert(params.ldmParams.hashRateLog < 32);
        serialState->ldmState.hashPower =
                ZSTD_rollingHash_primePower(params.ldmParams.minMatchLength);
    } else {
        memset(&params.ldmParams, 0, sizeof(params.ldmParams));
    }
    serialState->nextJobID = 0;
    if (params.fParams.checksumFlag)
        XXH64_reset(&serialState->xxhState, 0);
    if (params.ldmParams.enableLdm) {
        ZSTD_customMem cMem = params.customMem;
        unsigned const hashLog = params.ldmParams.hashLog;
        size_t const hashSize = ((size_t)1 << hashLog) * sizeof(ldmEntry_t);
        unsigned const bucketLog =
            params.ldmParams.hashLog - params.ldmParams.bucketSizeLog;
        size_t const bucketSize = (size_t)1 << bucketLog;
        unsigned const prevBucketLog =
            serialState->params.ldmParams.hashLog -
            serialState->params.ldmParams.bucketSizeLog;
        /* Size the seq pool tables */
        ZSTDMT_setNbSeq(seqPool, ZSTD_ldm_getMaxNbSeq(params.ldmParams, jobSize));
        /* Reset the window */
        ZSTD_window_clear(&serialState->ldmState.window);
        serialState->ldmWindow = serialState->ldmState.window;
        /* Resize tables and output space if necessary. */
        if (serialState->ldmState.hashTable == NULL || serialState->params.ldmParams.hashLog < hashLog) {
            ZSTD_free(serialState->ldmState.hashTable, cMem);
            serialState->ldmState.hashTable = (ldmEntry_t*)ZSTD_malloc(hashSize, cMem);
        }
        if (serialState->ldmState.bucketOffsets == NULL || prevBucketLog < bucketLog) {
            ZSTD_free(serialState->ldmState.bucketOffsets, cMem);
            serialState->ldmState.bucketOffsets = (BYTE*)ZSTD_malloc(bucketSize, cMem);
        }
        if (!serialState->ldmState.hashTable || !serialState->ldmState.bucketOffsets)
            return 1;
        /* Zero the tables */
        memset(serialState->ldmState.hashTable, 0, hashSize);
        memset(serialState->ldmState.bucketOffsets, 0, bucketSize);
    }
    serialState->params = params;
    serialState->params.jobSize = (U32)jobSize;
    return 0;
}

static int ZSTDMT_serialState_init(serialState_t* serialState)
{
    int initError = 0;
    memset(serialState, 0, sizeof(*serialState));
    initError |= ZSTD_pthread_mutex_init(&serialState->mutex, NULL);
    initError |= ZSTD_pthread_cond_init(&serialState->cond, NULL);
    initError |= ZSTD_pthread_mutex_init(&serialState->ldmWindowMutex, NULL);
    initError |= ZSTD_pthread_cond_init(&serialState->ldmWindowCond, NULL);
    return initError;
}

static void ZSTDMT_serialState_free(serialState_t* serialState)
{
    ZSTD_customMem cMem = serialState->params.customMem;
    ZSTD_pthread_mutex_destroy(&serialState->mutex);
    ZSTD_pthread_cond_destroy(&serialState->cond);
    ZSTD_pthread_mutex_destroy(&serialState->ldmWindowMutex);
    ZSTD_pthread_cond_destroy(&serialState->ldmWindowCond);
    ZSTD_free(serialState->ldmState.hashTable, cMem);
    ZSTD_free(serialState->ldmState.bucketOffsets, cMem);
}

static void ZSTDMT_serialState_update(serialState_t* serialState,
                                      ZSTD_CCtx* jobCCtx, rawSeqStore_t seqStore,
                                      range_t src, unsigned jobID)
{
    /* Wait for our turn */
    ZSTD_PTHREAD_MUTEX_LOCK(&serialState->mutex);
    while (serialState->nextJobID < jobID) {
        DEBUGLOG(5, "wait for serialState->cond");
        ZSTD_pthread_cond_wait(&serialState->cond, &serialState->mutex);
    }
    /* A future job may error and skip our job */
    if (serialState->nextJobID == jobID) {
        /* It is now our turn, do any processing necessary */
        if (serialState->params.ldmParams.enableLdm) {
            size_t error;
            assert(seqStore.seq != NULL && seqStore.pos == 0 &&
                   seqStore.size == 0 && seqStore.capacity > 0);
            assert(src.size <= serialState->params.jobSize);
            ZSTD_window_update(&serialState->ldmState.window, src.start, src.size);
            error = ZSTD_ldm_generateSequences(
                &serialState->ldmState, &seqStore,
                &serialState->params.ldmParams, src.start, src.size);
            /* We provide a large enough buffer to never fail. */
            assert(!ZSTD_isError(error)); (void)error;
            /* Update ldmWindow to match the ldmState.window and signal the main
             * thread if it is waiting for a buffer.
             */
            ZSTD_PTHREAD_MUTEX_LOCK(&serialState->ldmWindowMutex);
            serialState->ldmWindow = serialState->ldmState.window;
            ZSTD_pthread_cond_signal(&serialState->ldmWindowCond);
            ZSTD_pthread_mutex_unlock(&serialState->ldmWindowMutex);
        }
        if (serialState->params.fParams.checksumFlag && src.size > 0)
            XXH64_update(&serialState->xxhState, src.start, src.size);
    }
    /* Now it is the next jobs turn */
    serialState->nextJobID++;
    ZSTD_pthread_cond_broadcast(&serialState->cond);
    ZSTD_pthread_mutex_unlock(&serialState->mutex);

    if (seqStore.size > 0) {
        size_t const err = ZSTD_referenceExternalSequences(
            jobCCtx, seqStore.seq, seqStore.size);
        assert(serialState->params.ldmParams.enableLdm);
        assert(!ZSTD_isError(err));
        (void)err;
    }
}

static void ZSTDMT_serialState_ensureFinished(serialState_t* serialState,
                                              unsigned jobID, size_t cSize)
{
    ZSTD_PTHREAD_MUTEX_LOCK(&serialState->mutex);
    if (serialState->nextJobID <= jobID) {
        assert(ZSTD_isError(cSize)); (void)cSize;
        DEBUGLOG(5, "Skipping past job %u because of error", jobID);
        serialState->nextJobID = jobID + 1;
        ZSTD_pthread_cond_broadcast(&serialState->cond);

        ZSTD_PTHREAD_MUTEX_LOCK(&serialState->ldmWindowMutex);
        ZSTD_window_clear(&serialState->ldmWindow);
        ZSTD_pthread_cond_signal(&serialState->ldmWindowCond);
        ZSTD_pthread_mutex_unlock(&serialState->ldmWindowMutex);
    }
    ZSTD_pthread_mutex_unlock(&serialState->mutex);

}


/* ------------------------------------------ */
/* =====          Worker thread         ===== */
/* ------------------------------------------ */

static const range_t kNullRange = { NULL, 0 };

typedef struct {
    size_t   consumed;                   /* SHARED - set0 by mtctx, then modified by worker AND read by mtctx */
    size_t   cSize;                      /* SHARED - set0 by mtctx, then modified by worker AND read by mtctx, then set0 by mtctx */
    ZSTD_pthread_mutex_t job_mutex;      /* Thread-safe - used by mtctx and worker */
    ZSTD_pthread_cond_t job_cond;        /* Thread-safe - used by mtctx and worker */
    ZSTDMT_CCtxPool* cctxPool;           /* Thread-safe - used by mtctx and (all) workers */
    ZSTDMT_bufferPool* bufPool;          /* Thread-safe - used by mtctx and (all) workers */
    ZSTDMT_seqPool* seqPool;             /* Thread-safe - used by mtctx and (all) workers */
    serialState_t* serial;               /* Thread-safe - used by mtctx and (all) workers */
    buffer_t dstBuff;                    /* set by worker (or mtctx), then read by worker & mtctx, then modified by mtctx => no barrier */
    range_t prefix;                      /* set by mtctx, then read by worker & mtctx => no barrier */
    range_t src;                         /* set by mtctx, then read by worker & mtctx => no barrier */
    unsigned jobID;                      /* set by mtctx, then read by worker => no barrier */
    unsigned firstJob;                   /* set by mtctx, then read by worker => no barrier */
    unsigned lastJob;                    /* set by mtctx, then read by worker => no barrier */
    ZSTD_CCtx_params params;             /* set by mtctx, then read by worker => no barrier */
    const ZSTD_CDict* cdict;             /* set by mtctx, then read by worker => no barrier */
    unsigned long long fullFrameSize;    /* set by mtctx, then read by worker => no barrier */
    size_t   dstFlushed;                 /* used only by mtctx */
    unsigned frameChecksumNeeded;        /* used only by mtctx */
} ZSTDMT_jobDescription;

#define JOB_ERROR(e) {                          \
    ZSTD_PTHREAD_MUTEX_LOCK(&job->job_mutex);   \
    job->cSize = e;                             \
    ZSTD_pthread_mutex_unlock(&job->job_mutex); \
    goto _endJob;                               \
}

/* ZSTDMT_compressionJob() is a POOL_function type */
static void ZSTDMT_compressionJob(void* jobDescription)
{
    ZSTDMT_jobDescription* const job = (ZSTDMT_jobDescription*)jobDescription;
    ZSTD_CCtx_params jobParams = job->params;   /* do not modify job->params ! copy it, modify the copy */
    ZSTD_CCtx* const cctx = ZSTDMT_getCCtx(job->cctxPool);
    rawSeqStore_t rawSeqStore = ZSTDMT_getSeq(job->seqPool);
    buffer_t dstBuff = job->dstBuff;
    size_t lastCBlockSize = 0;

    /* ressources */
    if (cctx==NULL) JOB_ERROR(ERROR(memory_allocation));
    if (dstBuff.start == NULL) {   /* streaming job : doesn't provide a dstBuffer */
        dstBuff = ZSTDMT_getBuffer(job->bufPool);
        if (dstBuff.start==NULL) JOB_ERROR(ERROR(memory_allocation));
        job->dstBuff = dstBuff;   /* this value can be read in ZSTDMT_flush, when it copies the whole job */
    }
    if (jobParams.ldmParams.enableLdm && rawSeqStore.seq == NULL)
        JOB_ERROR(ERROR(memory_allocation));

    /* Don't compute the checksum for chunks, since we compute it externally,
     * but write it in the header.
     */
    if (job->jobID != 0) jobParams.fParams.checksumFlag = 0;
    /* Don't run LDM for the chunks, since we handle it externally */
    jobParams.ldmParams.enableLdm = 0;


    /* init */
    if (job->cdict) {
        size_t const initError = ZSTD_compressBegin_advanced_internal(cctx, NULL, 0, ZSTD_dct_auto, ZSTD_dtlm_fast, job->cdict, jobParams, job->fullFrameSize);
        assert(job->firstJob);  /* only allowed for first job */
        if (ZSTD_isError(initError)) JOB_ERROR(initError);
    } else {  /* srcStart points at reloaded section */
        U64 const pledgedSrcSize = job->firstJob ? job->fullFrameSize : job->src.size;
        {   size_t const forceWindowError = ZSTD_CCtxParam_setParameter(&jobParams, ZSTD_c_forceMaxWindow, !job->firstJob);
            if (ZSTD_isError(forceWindowError)) JOB_ERROR(forceWindowError);
        }
        {   size_t const initError = ZSTD_compressBegin_advanced_internal(cctx,
                                        job->prefix.start, job->prefix.size, ZSTD_dct_rawContent, /* load dictionary in "content-only" mode (no header analysis) */
                                        ZSTD_dtlm_fast,
                                        NULL, /*cdict*/
                                        jobParams, pledgedSrcSize);
            if (ZSTD_isError(initError)) JOB_ERROR(initError);
    }   }

    /* Perform serial step as early as possible, but after CCtx initialization */
    ZSTDMT_serialState_update(job->serial, cctx, rawSeqStore, job->src, job->jobID);

    if (!job->firstJob) {  /* flush and overwrite frame header when it's not first job */
        size_t const hSize = ZSTD_compressContinue(cctx, dstBuff.start, dstBuff.capacity, job->src.start, 0);
        if (ZSTD_isError(hSize)) JOB_ERROR(hSize);
        DEBUGLOG(5, "ZSTDMT_compressionJob: flush and overwrite %u bytes of frame header (not first job)", (U32)hSize);
        ZSTD_invalidateRepCodes(cctx);
    }

    /* compress */
    {   size_t const chunkSize = 4*ZSTD_BLOCKSIZE_MAX;
        int const nbChunks = (int)((job->src.size + (chunkSize-1)) / chunkSize);
        const BYTE* ip = (const BYTE*) job->src.start;
        BYTE* const ostart = (BYTE*)dstBuff.start;
        BYTE* op = ostart;
        BYTE* oend = op + dstBuff.capacity;
        int chunkNb;
        if (sizeof(size_t) > sizeof(int)) assert(job->src.size < ((size_t)INT_MAX) * chunkSize);   /* check overflow */
        DEBUGLOG(5, "ZSTDMT_compressionJob: compress %u bytes in %i blocks", (U32)job->src.size, nbChunks);
        assert(job->cSize == 0);
        for (chunkNb = 1; chunkNb < nbChunks; chunkNb++) {
            size_t const cSize = ZSTD_compressContinue(cctx, op, oend-op, ip, chunkSize);
            if (ZSTD_isError(cSize)) JOB_ERROR(cSize);
            ip += chunkSize;
            op += cSize; assert(op < oend);
            /* stats */
            ZSTD_PTHREAD_MUTEX_LOCK(&job->job_mutex);
            job->cSize += cSize;
            job->consumed = chunkSize * chunkNb;
            DEBUGLOG(5, "ZSTDMT_compressionJob: compress new block : cSize==%u bytes (total: %u)",
                        (U32)cSize, (U32)job->cSize);
            ZSTD_pthread_cond_signal(&job->job_cond);   /* warns some more data is ready to be flushed */
            ZSTD_pthread_mutex_unlock(&job->job_mutex);
        }
        /* last block */
        assert(chunkSize > 0);
        assert((chunkSize & (chunkSize - 1)) == 0);  /* chunkSize must be power of 2 for mask==(chunkSize-1) to work */
        if ((nbChunks > 0) | job->lastJob /*must output a "last block" flag*/ ) {
            size_t const lastBlockSize1 = job->src.size & (chunkSize-1);
            size_t const lastBlockSize = ((lastBlockSize1==0) & (job->src.size>=chunkSize)) ? chunkSize : lastBlockSize1;
            size_t const cSize = (job->lastJob) ?
                 ZSTD_compressEnd     (cctx, op, oend-op, ip, lastBlockSize) :
                 ZSTD_compressContinue(cctx, op, oend-op, ip, lastBlockSize);
            if (ZSTD_isError(cSize)) JOB_ERROR(cSize);
            lastCBlockSize = cSize;
    }   }

_endJob:
    ZSTDMT_serialState_ensureFinished(job->serial, job->jobID, job->cSize);
    if (job->prefix.size > 0)
        DEBUGLOG(5, "Finished with prefix: %zx", (size_t)job->prefix.start);
    DEBUGLOG(5, "Finished with source: %zx", (size_t)job->src.start);
    /* release resources */
    ZSTDMT_releaseSeq(job->seqPool, rawSeqStore);
    ZSTDMT_releaseCCtx(job->cctxPool, cctx);
    /* report */
    ZSTD_PTHREAD_MUTEX_LOCK(&job->job_mutex);
    if (ZSTD_isError(job->cSize)) assert(lastCBlockSize == 0);
    job->cSize += lastCBlockSize;
    job->consumed = job->src.size;  /* when job->consumed == job->src.size , compression job is presumed completed */
    ZSTD_pthread_cond_signal(&job->job_cond);
    ZSTD_pthread_mutex_unlock(&job->job_mutex);
}


/* ------------------------------------------ */
/* =====   Multi-threaded compression   ===== */
/* ------------------------------------------ */

typedef struct {
    range_t prefix;         /* read-only non-owned prefix buffer */
    buffer_t buffer;
    size_t filled;
} inBuff_t;

typedef struct {
  BYTE* buffer;     /* The round input buffer. All jobs get references
                     * to pieces of the buffer. ZSTDMT_tryGetInputRange()
                     * handles handing out job input buffers, and makes
                     * sure it doesn't overlap with any pieces still in use.
                     */
  size_t capacity;  /* The capacity of buffer. */
  size_t pos;       /* The position of the current inBuff in the round
                     * buffer. Updated past the end if the inBuff once
                     * the inBuff is sent to the worker thread.
                     * pos <= capacity.
                     */
} roundBuff_t;

static const roundBuff_t kNullRoundBuff = {NULL, 0, 0};

#define RSYNC_LENGTH 32

typedef struct {
  U64 hash;
  U64 hitMask;
  U64 primePower;
} rsyncState_t;

struct ZSTDMT_CCtx_s {
    POOL_ctx* factory;
    ZSTDMT_jobDescription* jobs;
    ZSTDMT_bufferPool* bufPool;
    ZSTDMT_CCtxPool* cctxPool;
    ZSTDMT_seqPool* seqPool;
    ZSTD_CCtx_params params;
    size_t targetSectionSize;
    size_t targetPrefixSize;
    int jobReady;        /* 1 => one job is already prepared, but pool has shortage of workers. Don't create a new job. */
    inBuff_t inBuff;
    roundBuff_t roundBuff;
    serialState_t serial;
    rsyncState_t rsync;
    unsigned singleBlockingThread;
    unsigned jobIDMask;
    unsigned doneJobID;
    unsigned nextJobID;
    unsigned frameEnded;
    unsigned allJobsCompleted;
    unsigned long long frameContentSize;
    unsigned long long consumed;
    unsigned long long produced;
    ZSTD_customMem cMem;
    ZSTD_CDict* cdictLocal;
    const ZSTD_CDict* cdict;
};

static void ZSTDMT_freeJobsTable(ZSTDMT_jobDescription* jobTable, U32 nbJobs, ZSTD_customMem cMem)
{
    U32 jobNb;
    if (jobTable == NULL) return;
    for (jobNb=0; jobNb<nbJobs; jobNb++) {
        ZSTD_pthread_mutex_destroy(&jobTable[jobNb].job_mutex);
        ZSTD_pthread_cond_destroy(&jobTable[jobNb].job_cond);
    }
    ZSTD_free(jobTable, cMem);
}

/* ZSTDMT_allocJobsTable()
 * allocate and init a job table.
 * update *nbJobsPtr to next power of 2 value, as size of table */
static ZSTDMT_jobDescription* ZSTDMT_createJobsTable(U32* nbJobsPtr, ZSTD_customMem cMem)
{
    U32 const nbJobsLog2 = ZSTD_highbit32(*nbJobsPtr) + 1;
    U32 const nbJobs = 1 << nbJobsLog2;
    U32 jobNb;
    ZSTDMT_jobDescription* const jobTable = (ZSTDMT_jobDescription*)
                ZSTD_calloc(nbJobs * sizeof(ZSTDMT_jobDescription), cMem);
    int initError = 0;
    if (jobTable==NULL) return NULL;
    *nbJobsPtr = nbJobs;
    for (jobNb=0; jobNb<nbJobs; jobNb++) {
        initError |= ZSTD_pthread_mutex_init(&jobTable[jobNb].job_mutex, NULL);
        initError |= ZSTD_pthread_cond_init(&jobTable[jobNb].job_cond, NULL);
    }
    if (initError != 0) {
        ZSTDMT_freeJobsTable(jobTable, nbJobs, cMem);
        return NULL;
    }
    return jobTable;
}

static size_t ZSTDMT_expandJobsTable (ZSTDMT_CCtx* mtctx, U32 nbWorkers) {
    U32 nbJobs = nbWorkers + 2;
    if (nbJobs > mtctx->jobIDMask+1) {  /* need more job capacity */
        ZSTDMT_freeJobsTable(mtctx->jobs, mtctx->jobIDMask+1, mtctx->cMem);
        mtctx->jobIDMask = 0;
        mtctx->jobs = ZSTDMT_createJobsTable(&nbJobs, mtctx->cMem);
        if (mtctx->jobs==NULL) return ERROR(memory_allocation);
        assert((nbJobs != 0) && ((nbJobs & (nbJobs - 1)) == 0));  /* ensure nbJobs is a power of 2 */
        mtctx->jobIDMask = nbJobs - 1;
    }
    return 0;
}


/* ZSTDMT_CCtxParam_setNbWorkers():
 * Internal use only */
size_t ZSTDMT_CCtxParam_setNbWorkers(ZSTD_CCtx_params* params, unsigned nbWorkers)
{
    if (nbWorkers > ZSTDMT_NBWORKERS_MAX) nbWorkers = ZSTDMT_NBWORKERS_MAX;
    params->nbWorkers = nbWorkers;
    params->overlapLog = ZSTDMT_OVERLAPLOG_DEFAULT;
    params->jobSize = 0;
    return nbWorkers;
}

ZSTDMT_CCtx* ZSTDMT_createCCtx_advanced(unsigned nbWorkers, ZSTD_customMem cMem)
{
    ZSTDMT_CCtx* mtctx;
    U32 nbJobs = nbWorkers + 2;
    int initError;
    DEBUGLOG(3, "ZSTDMT_createCCtx_advanced (nbWorkers = %u)", nbWorkers);

    if (nbWorkers < 1) return NULL;
    nbWorkers = MIN(nbWorkers , ZSTDMT_NBWORKERS_MAX);
    if ((cMem.customAlloc!=NULL) ^ (cMem.customFree!=NULL))
        /* invalid custom allocator */
        return NULL;

    mtctx = (ZSTDMT_CCtx*) ZSTD_calloc(sizeof(ZSTDMT_CCtx), cMem);
    if (!mtctx) return NULL;
    ZSTDMT_CCtxParam_setNbWorkers(&mtctx->params, nbWorkers);
    mtctx->cMem = cMem;
    mtctx->allJobsCompleted = 1;
    mtctx->factory = POOL_create_advanced(nbWorkers, 0, cMem);
    mtctx->jobs = ZSTDMT_createJobsTable(&nbJobs, cMem);
    assert(nbJobs > 0); assert((nbJobs & (nbJobs - 1)) == 0);  /* ensure nbJobs is a power of 2 */
    mtctx->jobIDMask = nbJobs - 1;
    mtctx->bufPool = ZSTDMT_createBufferPool(nbWorkers, cMem);
    mtctx->cctxPool = ZSTDMT_createCCtxPool(nbWorkers, cMem);
    mtctx->seqPool = ZSTDMT_createSeqPool(nbWorkers, cMem);
    initError = ZSTDMT_serialState_init(&mtctx->serial);
    mtctx->roundBuff = kNullRoundBuff;
    if (!mtctx->factory | !mtctx->jobs | !mtctx->bufPool | !mtctx->cctxPool | !mtctx->seqPool | initError) {
        ZSTDMT_freeCCtx(mtctx);
        return NULL;
    }
    DEBUGLOG(3, "mt_cctx created, for %u threads", nbWorkers);
    return mtctx;
}

ZSTDMT_CCtx* ZSTDMT_createCCtx(unsigned nbWorkers)
{
    return ZSTDMT_createCCtx_advanced(nbWorkers, ZSTD_defaultCMem);
}


/* ZSTDMT_releaseAllJobResources() :
 * note : ensure all workers are killed first ! */
static void ZSTDMT_releaseAllJobResources(ZSTDMT_CCtx* mtctx)
{
    unsigned jobID;
    DEBUGLOG(3, "ZSTDMT_releaseAllJobResources");
    for (jobID=0; jobID <= mtctx->jobIDMask; jobID++) {
        DEBUGLOG(4, "job%02u: release dst address %08X", jobID, (U32)(size_t)mtctx->jobs[jobID].dstBuff.start);
        ZSTDMT_releaseBuffer(mtctx->bufPool, mtctx->jobs[jobID].dstBuff);
        mtctx->jobs[jobID].dstBuff = g_nullBuffer;
        mtctx->jobs[jobID].cSize = 0;
    }
    memset(mtctx->jobs, 0, (mtctx->jobIDMask+1)*sizeof(ZSTDMT_jobDescription));
    mtctx->inBuff.buffer = g_nullBuffer;
    mtctx->inBuff.filled = 0;
    mtctx->allJobsCompleted = 1;
}

static void ZSTDMT_waitForAllJobsCompleted(ZSTDMT_CCtx* mtctx)
{
    DEBUGLOG(4, "ZSTDMT_waitForAllJobsCompleted");
    while (mtctx->doneJobID < mtctx->nextJobID) {
        unsigned const jobID = mtctx->doneJobID & mtctx->jobIDMask;
        ZSTD_PTHREAD_MUTEX_LOCK(&mtctx->jobs[jobID].job_mutex);
        while (mtctx->jobs[jobID].consumed < mtctx->jobs[jobID].src.size) {
            DEBUGLOG(4, "waiting for jobCompleted signal from job %u", mtctx->doneJobID);   /* we want to block when waiting for data to flush */
            ZSTD_pthread_cond_wait(&mtctx->jobs[jobID].job_cond, &mtctx->jobs[jobID].job_mutex);
        }
        ZSTD_pthread_mutex_unlock(&mtctx->jobs[jobID].job_mutex);
        mtctx->doneJobID++;
    }
}

size_t ZSTDMT_freeCCtx(ZSTDMT_CCtx* mtctx)
{
    if (mtctx==NULL) return 0;   /* compatible with free on NULL */
    POOL_free(mtctx->factory);   /* stop and free worker threads */
    ZSTDMT_releaseAllJobResources(mtctx);  /* release job resources into pools first */
    ZSTDMT_freeJobsTable(mtctx->jobs, mtctx->jobIDMask+1, mtctx->cMem);
    ZSTDMT_freeBufferPool(mtctx->bufPool);
    ZSTDMT_freeCCtxPool(mtctx->cctxPool);
    ZSTDMT_freeSeqPool(mtctx->seqPool);
    ZSTDMT_serialState_free(&mtctx->serial);
    ZSTD_freeCDict(mtctx->cdictLocal);
    if (mtctx->roundBuff.buffer)
        ZSTD_free(mtctx->roundBuff.buffer, mtctx->cMem);
    ZSTD_free(mtctx, mtctx->cMem);
    return 0;
}

size_t ZSTDMT_sizeof_CCtx(ZSTDMT_CCtx* mtctx)
{
    if (mtctx == NULL) return 0;   /* supports sizeof NULL */
    return sizeof(*mtctx)
            + POOL_sizeof(mtctx->factory)
            + ZSTDMT_sizeof_bufferPool(mtctx->bufPool)
            + (mtctx->jobIDMask+1) * sizeof(ZSTDMT_jobDescription)
            + ZSTDMT_sizeof_CCtxPool(mtctx->cctxPool)
            + ZSTDMT_sizeof_seqPool(mtctx->seqPool)
            + ZSTD_sizeof_CDict(mtctx->cdictLocal)
            + mtctx->roundBuff.capacity;
}

/* Internal only */
size_t
ZSTDMT_CCtxParam_setMTCtxParameter(ZSTD_CCtx_params* params,
                                   ZSTDMT_parameter parameter,
                                   int value)
{
    DEBUGLOG(4, "ZSTDMT_CCtxParam_setMTCtxParameter");
    switch(parameter)
    {
    case ZSTDMT_p_jobSize :
        DEBUGLOG(4, "ZSTDMT_CCtxParam_setMTCtxParameter : set jobSize to %i", value);
        if ( value != 0  /* default */
          && value < ZSTDMT_JOBSIZE_MIN)
            value = ZSTDMT_JOBSIZE_MIN;
        assert(value >= 0);
        if (value > ZSTDMT_JOBSIZE_MAX) value = ZSTDMT_JOBSIZE_MAX;
        params->jobSize = value;
        return value;

    case ZSTDMT_p_overlapLog :
        DEBUGLOG(4, "ZSTDMT_p_overlapLog : %i", value);
        if (value < ZSTD_OVERLAPLOG_MIN) value = ZSTD_OVERLAPLOG_MIN;
        if (value > ZSTD_OVERLAPLOG_MAX) value = ZSTD_OVERLAPLOG_MAX;
        params->overlapLog = value;
        return value;

    case ZSTDMT_p_rsyncable :
        value = (value != 0);
        params->rsyncable = value;
        return value;

    default :
        return ERROR(parameter_unsupported);
    }
}

size_t ZSTDMT_setMTCtxParameter(ZSTDMT_CCtx* mtctx, ZSTDMT_parameter parameter, int value)
{
    DEBUGLOG(4, "ZSTDMT_setMTCtxParameter");
    return ZSTDMT_CCtxParam_setMTCtxParameter(&mtctx->params, parameter, value);
}

size_t ZSTDMT_getMTCtxParameter(ZSTDMT_CCtx* mtctx, ZSTDMT_parameter parameter, int* value)
{
    switch (parameter) {
    case ZSTDMT_p_jobSize:
        assert(mtctx->params.jobSize <= INT_MAX);
        *value = (int)(mtctx->params.jobSize);
        break;
    case ZSTDMT_p_overlapLog:
        *value = mtctx->params.overlapLog;
        break;
    case ZSTDMT_p_rsyncable:
        *value = mtctx->params.rsyncable;
        break;
    default:
        return ERROR(parameter_unsupported);
    }
    return 0;
}

/* Sets parameters relevant to the compression job,
 * initializing others to default values. */
static ZSTD_CCtx_params ZSTDMT_initJobCCtxParams(ZSTD_CCtx_params const params)
{
    ZSTD_CCtx_params jobParams;
    memset(&jobParams, 0, sizeof(jobParams));

    jobParams.cParams = params.cParams;
    jobParams.fParams = params.fParams;
    jobParams.compressionLevel = params.compressionLevel;

    return jobParams;
}


/* ZSTDMT_resize() :
 * @return : error code if fails, 0 on success */
static size_t ZSTDMT_resize(ZSTDMT_CCtx* mtctx, unsigned nbWorkers)
{
    if (POOL_resize(mtctx->factory, nbWorkers)) return ERROR(memory_allocation);
    CHECK_F( ZSTDMT_expandJobsTable(mtctx, nbWorkers) );
    mtctx->bufPool = ZSTDMT_expandBufferPool(mtctx->bufPool, nbWorkers);
    if (mtctx->bufPool == NULL) return ERROR(memory_allocation);
    mtctx->cctxPool = ZSTDMT_expandCCtxPool(mtctx->cctxPool, nbWorkers);
    if (mtctx->cctxPool == NULL) return ERROR(memory_allocation);
    mtctx->seqPool = ZSTDMT_expandSeqPool(mtctx->seqPool, nbWorkers);
    if (mtctx->seqPool == NULL) return ERROR(memory_allocation);
    ZSTDMT_CCtxParam_setNbWorkers(&mtctx->params, nbWorkers);
    return 0;
}


/*! ZSTDMT_updateCParams_whileCompressing() :
 *  Updates a selected set of compression parameters, remaining compatible with currently active frame.
 *  New parameters will be applied to next compression job. */
void ZSTDMT_updateCParams_whileCompressing(ZSTDMT_CCtx* mtctx, const ZSTD_CCtx_params* cctxParams)
{
    U32 const saved_wlog = mtctx->params.cParams.windowLog;   /* Do not modify windowLog while compressing */
    int const compressionLevel = cctxParams->compressionLevel;
    DEBUGLOG(5, "ZSTDMT_updateCParams_whileCompressing (level:%i)",
                compressionLevel);
    mtctx->params.compressionLevel = compressionLevel;
    {   ZSTD_compressionParameters cParams = ZSTD_getCParamsFromCCtxParams(cctxParams, 0, 0);
        cParams.windowLog = saved_wlog;
        mtctx->params.cParams = cParams;
    }
}

/* ZSTDMT_getFrameProgression():
 * tells how much data has been consumed (input) and produced (output) for current frame.
 * able to count progression inside worker threads.
 * Note : mutex will be acquired during statistics collection inside workers. */
ZSTD_frameProgression ZSTDMT_getFrameProgression(ZSTDMT_CCtx* mtctx)
{
    ZSTD_frameProgression fps;
    DEBUGLOG(5, "ZSTDMT_getFrameProgression");
    fps.ingested = mtctx->consumed + mtctx->inBuff.filled;
    fps.consumed = mtctx->consumed;
    fps.produced = fps.flushed = mtctx->produced;
    fps.currentJobID = mtctx->nextJobID;
    fps.nbActiveWorkers = 0;
    {   unsigned jobNb;
        unsigned lastJobNb = mtctx->nextJobID + mtctx->jobReady; assert(mtctx->jobReady <= 1);
        DEBUGLOG(6, "ZSTDMT_getFrameProgression: jobs: from %u to <%u (jobReady:%u)",
                    mtctx->doneJobID, lastJobNb, mtctx->jobReady)
        for (jobNb = mtctx->doneJobID ; jobNb < lastJobNb ; jobNb++) {
            unsigned const wJobID = jobNb & mtctx->jobIDMask;
            ZSTDMT_jobDescription* jobPtr = &mtctx->jobs[wJobID];
            ZSTD_pthread_mutex_lock(&jobPtr->job_mutex);
            {   size_t const cResult = jobPtr->cSize;
                size_t const produced = ZSTD_isError(cResult) ? 0 : cResult;
                size_t const flushed = ZSTD_isError(cResult) ? 0 : jobPtr->dstFlushed;
                assert(flushed <= produced);
                fps.ingested += jobPtr->src.size;
                fps.consumed += jobPtr->consumed;
                fps.produced += produced;
                fps.flushed  += flushed;
                fps.nbActiveWorkers += (jobPtr->consumed < jobPtr->src.size);
            }
            ZSTD_pthread_mutex_unlock(&mtctx->jobs[wJobID].job_mutex);
        }
    }
    return fps;
}


size_t ZSTDMT_toFlushNow(ZSTDMT_CCtx* mtctx)
{
    size_t toFlush;
    unsigned const jobID = mtctx->doneJobID;
    assert(jobID <= mtctx->nextJobID);
    if (jobID == mtctx->nextJobID) return 0;   /* no active job => nothing to flush */

    /* look into oldest non-fully-flushed job */
    {   unsigned const wJobID = jobID & mtctx->jobIDMask;
        ZSTDMT_jobDescription* const jobPtr = &mtctx->jobs[wJobID];
        ZSTD_pthread_mutex_lock(&jobPtr->job_mutex);
        {   size_t const cResult = jobPtr->cSize;
            size_t const produced = ZSTD_isError(cResult) ? 0 : cResult;
            size_t const flushed = ZSTD_isError(cResult) ? 0 : jobPtr->dstFlushed;
            assert(flushed <= produced);
            toFlush = produced - flushed;
            if (toFlush==0 && (jobPtr->consumed >= jobPtr->src.size)) {
                /* doneJobID is not-fully-flushed, but toFlush==0 : doneJobID should be compressing some more data */
                assert(jobPtr->consumed < jobPtr->src.size);
            }
        }
        ZSTD_pthread_mutex_unlock(&mtctx->jobs[wJobID].job_mutex);
    }

    return toFlush;
}


/* ------------------------------------------ */
/* =====   Multi-threaded compression   ===== */
/* ------------------------------------------ */

static unsigned ZSTDMT_computeTargetJobLog(ZSTD_CCtx_params const params)
{
    if (params.ldmParams.enableLdm)
        /* In Long Range Mode, the windowLog is typically oversized.
         * In which case, it's preferable to determine the jobSize
         * based on chainLog instead. */
        return MAX(21, params.cParams.chainLog + 4);
    return MAX(20, params.cParams.windowLog + 2);
}

static int ZSTDMT_overlapLog_default(ZSTD_strategy strat)
{
    switch(strat)
    {
        case ZSTD_btultra2:
            return 9;
        case ZSTD_btultra:
        case ZSTD_btopt:
            return 8;
        case ZSTD_btlazy2:
        case ZSTD_lazy2:
            return 7;
        case ZSTD_lazy:
        case ZSTD_greedy:
        case ZSTD_dfast:
        case ZSTD_fast:
        default:;
    }
    return 6;
}

static int ZSTDMT_overlapLog(int ovlog, ZSTD_strategy strat)
{
    assert(0 <= ovlog && ovlog <= 9);
    if (ovlog == 0) return ZSTDMT_overlapLog_default(strat);
    return ovlog;
}

static size_t ZSTDMT_computeOverlapSize(ZSTD_CCtx_params const params)
{
    int const overlapRLog = 9 - ZSTDMT_overlapLog(params.overlapLog, params.cParams.strategy);
    int ovLog = (overlapRLog >= 8) ? 0 : (params.cParams.windowLog - overlapRLog);
    assert(0 <= overlapRLog && overlapRLog <= 8);
    if (params.ldmParams.enableLdm) {
        /* In Long Range Mode, the windowLog is typically oversized.
         * In which case, it's preferable to determine the jobSize
         * based on chainLog instead.
         * Then, ovLog becomes a fraction of the jobSize, rather than windowSize */
        ovLog = MIN(params.cParams.windowLog, ZSTDMT_computeTargetJobLog(params) - 2)
                - overlapRLog;
    }
    assert(0 <= ovLog && ovLog <= 30);
    DEBUGLOG(4, "overlapLog : %i", params.overlapLog);
    DEBUGLOG(4, "overlap size : %i", 1 << ovLog);
    return (ovLog==0) ? 0 : (size_t)1 << ovLog;
}

static unsigned
ZSTDMT_computeNbJobs(ZSTD_CCtx_params params, size_t srcSize, unsigned nbWorkers)
{
    assert(nbWorkers>0);
    {   size_t const jobSizeTarget = (size_t)1 << ZSTDMT_computeTargetJobLog(params);
        size_t const jobMaxSize = jobSizeTarget << 2;
        size_t const passSizeMax = jobMaxSize * nbWorkers;
        unsigned const multiplier = (unsigned)(srcSize / passSizeMax) + 1;
        unsigned const nbJobsLarge = multiplier * nbWorkers;
        unsigned const nbJobsMax = (unsigned)(srcSize / jobSizeTarget) + 1;
        unsigned const nbJobsSmall = MIN(nbJobsMax, nbWorkers);
        return (multiplier>1) ? nbJobsLarge : nbJobsSmall;
}   }

/* ZSTDMT_compress_advanced_internal() :
 * This is a blocking function : it will only give back control to caller after finishing its compression job.
 */
static size_t ZSTDMT_compress_advanced_internal(
                ZSTDMT_CCtx* mtctx,
                void* dst, size_t dstCapacity,
          const void* src, size_t srcSize,
          const ZSTD_CDict* cdict,
                ZSTD_CCtx_params params)
{
    ZSTD_CCtx_params const jobParams = ZSTDMT_initJobCCtxParams(params);
    size_t const overlapSize = ZSTDMT_computeOverlapSize(params);
    unsigned const nbJobs = ZSTDMT_computeNbJobs(params, srcSize, params.nbWorkers);
    size_t const proposedJobSize = (srcSize + (nbJobs-1)) / nbJobs;
    size_t const avgJobSize = (((proposedJobSize-1) & 0x1FFFF) < 0x7FFF) ? proposedJobSize + 0xFFFF : proposedJobSize;   /* avoid too small last block */
    const char* const srcStart = (const char*)src;
    size_t remainingSrcSize = srcSize;
    unsigned const compressWithinDst = (dstCapacity >= ZSTD_compressBound(srcSize)) ? nbJobs : (unsigned)(dstCapacity / ZSTD_compressBound(avgJobSize));  /* presumes avgJobSize >= 256 KB, which should be the case */
    size_t frameStartPos = 0, dstBufferPos = 0;
    assert(jobParams.nbWorkers == 0);
    assert(mtctx->cctxPool->totalCCtx == params.nbWorkers);

    params.jobSize = (U32)avgJobSize;
    DEBUGLOG(4, "ZSTDMT_compress_advanced_internal: nbJobs=%2u (rawSize=%u bytes; fixedSize=%u) ",
                nbJobs, (U32)proposedJobSize, (U32)avgJobSize);

    if ((nbJobs==1) | (params.nbWorkers<=1)) {   /* fallback to single-thread mode : this is a blocking invocation anyway */
        ZSTD_CCtx* const cctx = mtctx->cctxPool->cctx[0];
        DEBUGLOG(4, "ZSTDMT_compress_advanced_internal: fallback to single-thread mode");
        if (cdict) return ZSTD_compress_usingCDict_advanced(cctx, dst, dstCapacity, src, srcSize, cdict, jobParams.fParams);
        return ZSTD_compress_advanced_internal(cctx, dst, dstCapacity, src, srcSize, NULL, 0, jobParams);
    }

    assert(avgJobSize >= 256 KB);  /* condition for ZSTD_compressBound(A) + ZSTD_compressBound(B) <= ZSTD_compressBound(A+B), required to compress directly into Dst (no additional buffer) */
    ZSTDMT_setBufferSize(mtctx->bufPool, ZSTD_compressBound(avgJobSize) );
    if (ZSTDMT_serialState_reset(&mtctx->serial, mtctx->seqPool, params, avgJobSize))
        return ERROR(memory_allocation);

    CHECK_F( ZSTDMT_expandJobsTable(mtctx, nbJobs) );  /* only expands if necessary */

    {   unsigned u;
        for (u=0; u<nbJobs; u++) {
            size_t const jobSize = MIN(remainingSrcSize, avgJobSize);
            size_t const dstBufferCapacity = ZSTD_compressBound(jobSize);
            buffer_t const dstAsBuffer = { (char*)dst + dstBufferPos, dstBufferCapacity };
            buffer_t const dstBuffer = u < compressWithinDst ? dstAsBuffer : g_nullBuffer;
            size_t dictSize = u ? overlapSize : 0;

            mtctx->jobs[u].prefix.start = srcStart + frameStartPos - dictSize;
            mtctx->jobs[u].prefix.size = dictSize;
            mtctx->jobs[u].src.start = srcStart + frameStartPos;
            mtctx->jobs[u].src.size = jobSize; assert(jobSize > 0);  /* avoid job.src.size == 0 */
            mtctx->jobs[u].consumed = 0;
            mtctx->jobs[u].cSize = 0;
            mtctx->jobs[u].cdict = (u==0) ? cdict : NULL;
            mtctx->jobs[u].fullFrameSize = srcSize;
            mtctx->jobs[u].params = jobParams;
            /* do not calculate checksum within sections, but write it in header for first section */
            mtctx->jobs[u].dstBuff = dstBuffer;
            mtctx->jobs[u].cctxPool = mtctx->cctxPool;
            mtctx->jobs[u].bufPool = mtctx->bufPool;
            mtctx->jobs[u].seqPool = mtctx->seqPool;
            mtctx->jobs[u].serial = &mtctx->serial;
            mtctx->jobs[u].jobID = u;
            mtctx->jobs[u].firstJob = (u==0);
            mtctx->jobs[u].lastJob = (u==nbJobs-1);

            DEBUGLOG(5, "ZSTDMT_compress_advanced_internal: posting job %u  (%u bytes)", u, (U32)jobSize);
            DEBUG_PRINTHEX(6, mtctx->jobs[u].prefix.start, 12);
            POOL_add(mtctx->factory, ZSTDMT_compressionJob, &mtctx->jobs[u]);

            frameStartPos += jobSize;
            dstBufferPos += dstBufferCapacity;
            remainingSrcSize -= jobSize;
    }   }

    /* collect result */
    {   size_t error = 0, dstPos = 0;
        unsigned jobID;
        for (jobID=0; jobID<nbJobs; jobID++) {
            DEBUGLOG(5, "waiting for job %u ", jobID);
            ZSTD_PTHREAD_MUTEX_LOCK(&mtctx->jobs[jobID].job_mutex);
            while (mtctx->jobs[jobID].consumed < mtctx->jobs[jobID].src.size) {
                DEBUGLOG(5, "waiting for jobCompleted signal from job %u", jobID);
                ZSTD_pthread_cond_wait(&mtctx->jobs[jobID].job_cond, &mtctx->jobs[jobID].job_mutex);
            }
            ZSTD_pthread_mutex_unlock(&mtctx->jobs[jobID].job_mutex);
            DEBUGLOG(5, "ready to write job %u ", jobID);

            {   size_t const cSize = mtctx->jobs[jobID].cSize;
                if (ZSTD_isError(cSize)) error = cSize;
                if ((!error) && (dstPos + cSize > dstCapacity)) error = ERROR(dstSize_tooSmall);
                if (jobID) {   /* note : job 0 is written directly at dst, which is correct position */
                    if (!error)
                        memmove((char*)dst + dstPos, mtctx->jobs[jobID].dstBuff.start, cSize);  /* may overlap when job compressed within dst */
                    if (jobID >= compressWithinDst) {  /* job compressed into its own buffer, which must be released */
                        DEBUGLOG(5, "releasing buffer %u>=%u", jobID, compressWithinDst);
                        ZSTDMT_releaseBuffer(mtctx->bufPool, mtctx->jobs[jobID].dstBuff);
                }   }
                mtctx->jobs[jobID].dstBuff = g_nullBuffer;
                mtctx->jobs[jobID].cSize = 0;
                dstPos += cSize ;
            }
        }  /* for (jobID=0; jobID<nbJobs; jobID++) */

        DEBUGLOG(4, "checksumFlag : %u ", params.fParams.checksumFlag);
        if (params.fParams.checksumFlag) {
            U32 const checksum = (U32)XXH64_digest(&mtctx->serial.xxhState);
            if (dstPos + 4 > dstCapacity) {
                error = ERROR(dstSize_tooSmall);
            } else {
                DEBUGLOG(4, "writing checksum : %08X \n", checksum);
                MEM_writeLE32((char*)dst + dstPos, checksum);
                dstPos += 4;
        }   }

        if (!error) DEBUGLOG(4, "compressed size : %u  ", (U32)dstPos);
        return error ? error : dstPos;
    }
}

size_t ZSTDMT_compress_advanced(ZSTDMT_CCtx* mtctx,
                                void* dst, size_t dstCapacity,
                          const void* src, size_t srcSize,
                          const ZSTD_CDict* cdict,
                                ZSTD_parameters params,
                                int overlapLog)
{
    ZSTD_CCtx_params cctxParams = mtctx->params;
    cctxParams.cParams = params.cParams;
    cctxParams.fParams = params.fParams;
    assert(ZSTD_OVERLAPLOG_MIN <= overlapLog && overlapLog <= ZSTD_OVERLAPLOG_MAX);
    cctxParams.overlapLog = overlapLog;
    return ZSTDMT_compress_advanced_internal(mtctx,
                                             dst, dstCapacity,
                                             src, srcSize,
                                             cdict, cctxParams);
}


size_t ZSTDMT_compressCCtx(ZSTDMT_CCtx* mtctx,
                           void* dst, size_t dstCapacity,
                     const void* src, size_t srcSize,
                           int compressionLevel)
{
    ZSTD_parameters params = ZSTD_getParams(compressionLevel, srcSize, 0);
    int const overlapLog = ZSTDMT_overlapLog_default(params.cParams.strategy);
    params.fParams.contentSizeFlag = 1;
    return ZSTDMT_compress_advanced(mtctx, dst, dstCapacity, src, srcSize, NULL, params, overlapLog);
}


/* ====================================== */
/* =======      Streaming API     ======= */
/* ====================================== */

size_t ZSTDMT_initCStream_internal(
        ZSTDMT_CCtx* mtctx,
        const void* dict, size_t dictSize, ZSTD_dictContentType_e dictContentType,
        const ZSTD_CDict* cdict, ZSTD_CCtx_params params,
        unsigned long long pledgedSrcSize)
{
    DEBUGLOG(4, "ZSTDMT_initCStream_internal (pledgedSrcSize=%u, nbWorkers=%u, cctxPool=%u)",
                (U32)pledgedSrcSize, params.nbWorkers, mtctx->cctxPool->totalCCtx);

    /* params supposed partially fully validated at this point */
    assert(!ZSTD_isError(ZSTD_checkCParams(params.cParams)));
    assert(!((dict) && (cdict)));  /* either dict or cdict, not both */

    /* init */
    if (params.nbWorkers != mtctx->params.nbWorkers)
        CHECK_F( ZSTDMT_resize(mtctx, params.nbWorkers) );

    if (params.jobSize != 0 && params.jobSize < ZSTDMT_JOBSIZE_MIN) params.jobSize = ZSTDMT_JOBSIZE_MIN;
    if (params.jobSize > (size_t)ZSTDMT_JOBSIZE_MAX) params.jobSize = ZSTDMT_JOBSIZE_MAX;

    mtctx->singleBlockingThread = (pledgedSrcSize <= ZSTDMT_JOBSIZE_MIN);  /* do not trigger multi-threading when srcSize is too small */
    if (mtctx->singleBlockingThread) {
        ZSTD_CCtx_params const singleThreadParams = ZSTDMT_initJobCCtxParams(params);
        DEBUGLOG(5, "ZSTDMT_initCStream_internal: switch to single blocking thread mode");
        assert(singleThreadParams.nbWorkers == 0);
        return ZSTD_initCStream_internal(mtctx->cctxPool->cctx[0],
                                         dict, dictSize, cdict,
                                         singleThreadParams, pledgedSrcSize);
    }

    DEBUGLOG(4, "ZSTDMT_initCStream_internal: %u workers", params.nbWorkers);

    if (mtctx->allJobsCompleted == 0) {   /* previous compression not correctly finished */
        ZSTDMT_waitForAllJobsCompleted(mtctx);
        ZSTDMT_releaseAllJobResources(mtctx);
        mtctx->allJobsCompleted = 1;
    }

    mtctx->params = params;
    mtctx->frameContentSize = pledgedSrcSize;
    if (dict) {
        ZSTD_freeCDict(mtctx->cdictLocal);
        mtctx->cdictLocal = ZSTD_createCDict_advanced(dict, dictSize,
                                                    ZSTD_dlm_byCopy, dictContentType, /* note : a loadPrefix becomes an internal CDict */
                                                    params.cParams, mtctx->cMem);
        mtctx->cdict = mtctx->cdictLocal;
        if (mtctx->cdictLocal == NULL) return ERROR(memory_allocation);
    } else {
        ZSTD_freeCDict(mtctx->cdictLocal);
        mtctx->cdictLocal = NULL;
        mtctx->cdict = cdict;
    }

    mtctx->targetPrefixSize = ZSTDMT_computeOverlapSize(params);
    DEBUGLOG(4, "overlapLog=%i => %u KB", params.overlapLog, (U32)(mtctx->targetPrefixSize>>10));
    mtctx->targetSectionSize = params.jobSize;
    if (mtctx->targetSectionSize == 0) {
        mtctx->targetSectionSize = 1ULL << ZSTDMT_computeTargetJobLog(params);
    }
    if (params.rsyncable) {
        /* Aim for the targetsectionSize as the average job size. */
        U32 const jobSizeMB = (U32)(mtctx->targetSectionSize >> 20);
        U32 const rsyncBits = ZSTD_highbit32(jobSizeMB) + 20;
        assert(jobSizeMB >= 1);
        DEBUGLOG(4, "rsyncLog = %u", rsyncBits);
        mtctx->rsync.hash = 0;
        mtctx->rsync.hitMask = (1ULL << rsyncBits) - 1;
        mtctx->rsync.primePower = ZSTD_rollingHash_primePower(RSYNC_LENGTH);
    }
    if (mtctx->targetSectionSize < mtctx->targetPrefixSize) mtctx->targetSectionSize = mtctx->targetPrefixSize;  /* job size must be >= overlap size */
    DEBUGLOG(4, "Job Size : %u KB (note : set to %u)", (U32)(mtctx->targetSectionSize>>10), (U32)params.jobSize);
    DEBUGLOG(4, "inBuff Size : %u KB", (U32)(mtctx->targetSectionSize>>10));
    ZSTDMT_setBufferSize(mtctx->bufPool, ZSTD_compressBound(mtctx->targetSectionSize));
    {
        /* If ldm is enabled we need windowSize space. */
        size_t const windowSize = mtctx->params.ldmParams.enableLdm ? (1U << mtctx->params.cParams.windowLog) : 0;
        /* Two buffers of slack, plus extra space for the overlap
         * This is the minimum slack that LDM works with. One extra because
         * flush might waste up to targetSectionSize-1 bytes. Another extra
         * for the overlap (if > 0), then one to fill which doesn't overlap
         * with the LDM window.
         */
        size_t const nbSlackBuffers = 2 + (mtctx->targetPrefixSize > 0);
        size_t const slackSize = mtctx->targetSectionSize * nbSlackBuffers;
        /* Compute the total size, and always have enough slack */
        size_t const nbWorkers = MAX(mtctx->params.nbWorkers, 1);
        size_t const sectionsSize = mtctx->targetSectionSize * nbWorkers;
        size_t const capacity = MAX(windowSize, sectionsSize) + slackSize;
        if (mtctx->roundBuff.capacity < capacity) {
            if (mtctx->roundBuff.buffer)
                ZSTD_free(mtctx->roundBuff.buffer, mtctx->cMem);
            mtctx->roundBuff.buffer = (BYTE*)ZSTD_malloc(capacity, mtctx->cMem);
            if (mtctx->roundBuff.buffer == NULL) {
                mtctx->roundBuff.capacity = 0;
                return ERROR(memory_allocation);
            }
            mtctx->roundBuff.capacity = capacity;
        }
    }
    DEBUGLOG(4, "roundBuff capacity : %u KB", (U32)(mtctx->roundBuff.capacity>>10));
    mtctx->roundBuff.pos = 0;
    mtctx->inBuff.buffer = g_nullBuffer;
    mtctx->inBuff.filled = 0;
    mtctx->inBuff.prefix = kNullRange;
    mtctx->doneJobID = 0;
    mtctx->nextJobID = 0;
    mtctx->frameEnded = 0;
    mtctx->allJobsCompleted = 0;
    mtctx->consumed = 0;
    mtctx->produced = 0;
    if (ZSTDMT_serialState_reset(&mtctx->serial, mtctx->seqPool, params, mtctx->targetSectionSize))
        return ERROR(memory_allocation);
    return 0;
}

size_t ZSTDMT_initCStream_advanced(ZSTDMT_CCtx* mtctx,
                             const void* dict, size_t dictSize,
                                   ZSTD_parameters params,
                                   unsigned long long pledgedSrcSize)
{
    ZSTD_CCtx_params cctxParams = mtctx->params;  /* retrieve sticky params */
    DEBUGLOG(4, "ZSTDMT_initCStream_advanced (pledgedSrcSize=%u)", (U32)pledgedSrcSize);
    cctxParams.cParams = params.cParams;
    cctxParams.fParams = params.fParams;
    return ZSTDMT_initCStream_internal(mtctx, dict, dictSize, ZSTD_dct_auto, NULL,
                                       cctxParams, pledgedSrcSize);
}

size_t ZSTDMT_initCStream_usingCDict(ZSTDMT_CCtx* mtctx,
                               const ZSTD_CDict* cdict,
                                     ZSTD_frameParameters fParams,
                                     unsigned long long pledgedSrcSize)
{
    ZSTD_CCtx_params cctxParams = mtctx->params;
    if (cdict==NULL) return ERROR(dictionary_wrong);   /* method incompatible with NULL cdict */
    cctxParams.cParams = ZSTD_getCParamsFromCDict(cdict);
    cctxParams.fParams = fParams;
    return ZSTDMT_initCStream_internal(mtctx, NULL, 0 /*dictSize*/, ZSTD_dct_auto, cdict,
                                       cctxParams, pledgedSrcSize);
}


/* ZSTDMT_resetCStream() :
 * pledgedSrcSize can be zero == unknown (for the time being)
 * prefer using ZSTD_CONTENTSIZE_UNKNOWN,
 * as `0` might mean "empty" in the future */
size_t ZSTDMT_resetCStream(ZSTDMT_CCtx* mtctx, unsigned long long pledgedSrcSize)
{
    if (!pledgedSrcSize) pledgedSrcSize = ZSTD_CONTENTSIZE_UNKNOWN;
    return ZSTDMT_initCStream_internal(mtctx, NULL, 0, ZSTD_dct_auto, 0, mtctx->params,
                                       pledgedSrcSize);
}

size_t ZSTDMT_initCStream(ZSTDMT_CCtx* mtctx, int compressionLevel) {
    ZSTD_parameters const params = ZSTD_getParams(compressionLevel, ZSTD_CONTENTSIZE_UNKNOWN, 0);
    ZSTD_CCtx_params cctxParams = mtctx->params;   /* retrieve sticky params */
    DEBUGLOG(4, "ZSTDMT_initCStream (cLevel=%i)", compressionLevel);
    cctxParams.cParams = params.cParams;
    cctxParams.fParams = params.fParams;
    return ZSTDMT_initCStream_internal(mtctx, NULL, 0, ZSTD_dct_auto, NULL, cctxParams, ZSTD_CONTENTSIZE_UNKNOWN);
}


/* ZSTDMT_writeLastEmptyBlock()
 * Write a single empty block with an end-of-frame to finish a frame.
 * Job must be created from streaming variant.
 * This function is always successfull if expected conditions are fulfilled.
 */
static void ZSTDMT_writeLastEmptyBlock(ZSTDMT_jobDescription* job)
{
    assert(job->lastJob == 1);
    assert(job->src.size == 0);   /* last job is empty -> will be simplified into a last empty block */
    assert(job->firstJob == 0);   /* cannot be first job, as it also needs to create frame header */
    assert(job->dstBuff.start == NULL);   /* invoked from streaming variant only (otherwise, dstBuff might be user's output) */
    job->dstBuff = ZSTDMT_getBuffer(job->bufPool);
    if (job->dstBuff.start == NULL) {
      job->cSize = ERROR(memory_allocation);
      return;
    }
    assert(job->dstBuff.capacity >= ZSTD_blockHeaderSize);   /* no buffer should ever be that small */
    job->src = kNullRange;
    job->cSize = ZSTD_writeLastEmptyBlock(job->dstBuff.start, job->dstBuff.capacity);
    assert(!ZSTD_isError(job->cSize));
    assert(job->consumed == 0);
}

static size_t ZSTDMT_createCompressionJob(ZSTDMT_CCtx* mtctx, size_t srcSize, ZSTD_EndDirective endOp)
{
    unsigned const jobID = mtctx->nextJobID & mtctx->jobIDMask;
    int const endFrame = (endOp == ZSTD_e_end);

    if (mtctx->nextJobID > mtctx->doneJobID + mtctx->jobIDMask) {
        DEBUGLOG(5, "ZSTDMT_createCompressionJob: will not create new job : table is full");
        assert((mtctx->nextJobID & mtctx->jobIDMask) == (mtctx->doneJobID & mtctx->jobIDMask));
        return 0;
    }

    if (!mtctx->jobReady) {
        BYTE const* src = (BYTE const*)mtctx->inBuff.buffer.start;
        DEBUGLOG(5, "ZSTDMT_createCompressionJob: preparing job %u to compress %u bytes with %u preload ",
                    mtctx->nextJobID, (U32)srcSize, (U32)mtctx->inBuff.prefix.size);
        mtctx->jobs[jobID].src.start = src;
        mtctx->jobs[jobID].src.size = srcSize;
        assert(mtctx->inBuff.filled >= srcSize);
        mtctx->jobs[jobID].prefix = mtctx->inBuff.prefix;
        mtctx->jobs[jobID].consumed = 0;
        mtctx->jobs[jobID].cSize = 0;
        mtctx->jobs[jobID].params = mtctx->params;
        mtctx->jobs[jobID].cdict = mtctx->nextJobID==0 ? mtctx->cdict : NULL;
        mtctx->jobs[jobID].fullFrameSize = mtctx->frameContentSize;
        mtctx->jobs[jobID].dstBuff = g_nullBuffer;
        mtctx->jobs[jobID].cctxPool = mtctx->cctxPool;
        mtctx->jobs[jobID].bufPool = mtctx->bufPool;
        mtctx->jobs[jobID].seqPool = mtctx->seqPool;
        mtctx->jobs[jobID].serial = &mtctx->serial;
        mtctx->jobs[jobID].jobID = mtctx->nextJobID;
        mtctx->jobs[jobID].firstJob = (mtctx->nextJobID==0);
        mtctx->jobs[jobID].lastJob = endFrame;
        mtctx->jobs[jobID].frameChecksumNeeded = mtctx->params.fParams.checksumFlag && endFrame && (mtctx->nextJobID>0);
        mtctx->jobs[jobID].dstFlushed = 0;

        /* Update the round buffer pos and clear the input buffer to be reset */
        mtctx->roundBuff.pos += srcSize;
        mtctx->inBuff.buffer = g_nullBuffer;
        mtctx->inBuff.filled = 0;
        /* Set the prefix */
        if (!endFrame) {
            size_t const newPrefixSize = MIN(srcSize, mtctx->targetPrefixSize);
            mtctx->inBuff.prefix.start = src + srcSize - newPrefixSize;
            mtctx->inBuff.prefix.size = newPrefixSize;
        } else {   /* endFrame==1 => no need for another input buffer */
            mtctx->inBuff.prefix = kNullRange;
            mtctx->frameEnded = endFrame;
            if (mtctx->nextJobID == 0) {
                /* single job exception : checksum is already calculated directly within worker thread */
                mtctx->params.fParams.checksumFlag = 0;
        }   }

        if ( (srcSize == 0)
          && (mtctx->nextJobID>0)/*single job must also write frame header*/ ) {
            DEBUGLOG(5, "ZSTDMT_createCompressionJob: creating a last empty block to end frame");
            assert(endOp == ZSTD_e_end);  /* only possible case : need to end the frame with an empty last block */
            ZSTDMT_writeLastEmptyBlock(mtctx->jobs + jobID);
            mtctx->nextJobID++;
            return 0;
        }
    }

    DEBUGLOG(5, "ZSTDMT_createCompressionJob: posting job %u : %u bytes  (end:%u, jobNb == %u (mod:%u))",
                mtctx->nextJobID,
                (U32)mtctx->jobs[jobID].src.size,
                mtctx->jobs[jobID].lastJob,
                mtctx->nextJobID,
                jobID);
    if (POOL_tryAdd(mtctx->factory, ZSTDMT_compressionJob, &mtctx->jobs[jobID])) {
        mtctx->nextJobID++;
        mtctx->jobReady = 0;
    } else {
        DEBUGLOG(5, "ZSTDMT_createCompressionJob: no worker available for job %u", mtctx->nextJobID);
        mtctx->jobReady = 1;
    }
    return 0;
}


/*! ZSTDMT_flushProduced() :
 *  flush whatever data has been produced but not yet flushed in current job.
 *  move to next job if current one is fully flushed.
 * `output` : `pos` will be updated with amount of data flushed .
 * `blockToFlush` : if >0, the function will block and wait if there is no data available to flush .
 * @return : amount of data remaining within internal buffer, 0 if no more, 1 if unknown but > 0, or an error code */
static size_t ZSTDMT_flushProduced(ZSTDMT_CCtx* mtctx, ZSTD_outBuffer* output, unsigned blockToFlush, ZSTD_EndDirective end)
{
    unsigned const wJobID = mtctx->doneJobID & mtctx->jobIDMask;
    DEBUGLOG(5, "ZSTDMT_flushProduced (blocking:%u , job %u <= %u)",
                blockToFlush, mtctx->doneJobID, mtctx->nextJobID);
    assert(output->size >= output->pos);

    ZSTD_PTHREAD_MUTEX_LOCK(&mtctx->jobs[wJobID].job_mutex);
    if (  blockToFlush
      && (mtctx->doneJobID < mtctx->nextJobID) ) {
        assert(mtctx->jobs[wJobID].dstFlushed <= mtctx->jobs[wJobID].cSize);
        while (mtctx->jobs[wJobID].dstFlushed == mtctx->jobs[wJobID].cSize) {  /* nothing to flush */
            if (mtctx->jobs[wJobID].consumed == mtctx->jobs[wJobID].src.size) {
                DEBUGLOG(5, "job %u is completely consumed (%u == %u) => don't wait for cond, there will be none",
                            mtctx->doneJobID, (U32)mtctx->jobs[wJobID].consumed, (U32)mtctx->jobs[wJobID].src.size);
                break;
            }
            DEBUGLOG(5, "waiting for something to flush from job %u (currently flushed: %u bytes)",
                        mtctx->doneJobID, (U32)mtctx->jobs[wJobID].dstFlushed);
            ZSTD_pthread_cond_wait(&mtctx->jobs[wJobID].job_cond, &mtctx->jobs[wJobID].job_mutex);  /* block when nothing to flush but some to come */
    }   }

    /* try to flush something */
    {   size_t cSize = mtctx->jobs[wJobID].cSize;                  /* shared */
        size_t const srcConsumed = mtctx->jobs[wJobID].consumed;   /* shared */
        size_t const srcSize = mtctx->jobs[wJobID].src.size;       /* read-only, could be done after mutex lock, but no-declaration-after-statement */
        ZSTD_pthread_mutex_unlock(&mtctx->jobs[wJobID].job_mutex);
        if (ZSTD_isError(cSize)) {
            DEBUGLOG(5, "ZSTDMT_flushProduced: job %u : compression error detected : %s",
                        mtctx->doneJobID, ZSTD_getErrorName(cSize));
            ZSTDMT_waitForAllJobsCompleted(mtctx);
            ZSTDMT_releaseAllJobResources(mtctx);
            return cSize;
        }
        /* add frame checksum if necessary (can only happen once) */
        assert(srcConsumed <= srcSize);
        if ( (srcConsumed == srcSize)   /* job completed -> worker no longer active */
          && mtctx->jobs[wJobID].frameChecksumNeeded ) {
            U32 const checksum = (U32)XXH64_digest(&mtctx->serial.xxhState);
            DEBUGLOG(4, "ZSTDMT_flushProduced: writing checksum : %08X \n", checksum);
            MEM_writeLE32((char*)mtctx->jobs[wJobID].dstBuff.start + mtctx->jobs[wJobID].cSize, checksum);
            cSize += 4;
            mtctx->jobs[wJobID].cSize += 4;  /* can write this shared value, as worker is no longer active */
            mtctx->jobs[wJobID].frameChecksumNeeded = 0;
        }

        if (cSize > 0) {   /* compression is ongoing or completed */
            size_t const toFlush = MIN(cSize - mtctx->jobs[wJobID].dstFlushed, output->size - output->pos);
            DEBUGLOG(5, "ZSTDMT_flushProduced: Flushing %u bytes from job %u (completion:%u/%u, generated:%u)",
                        (U32)toFlush, mtctx->doneJobID, (U32)srcConsumed, (U32)srcSize, (U32)cSize);
            assert(mtctx->doneJobID < mtctx->nextJobID);
            assert(cSize >= mtctx->jobs[wJobID].dstFlushed);
            assert(mtctx->jobs[wJobID].dstBuff.start != NULL);
            memcpy((char*)output->dst + output->pos,
                   (const char*)mtctx->jobs[wJobID].dstBuff.start + mtctx->jobs[wJobID].dstFlushed,
                   toFlush);
            output->pos += toFlush;
            mtctx->jobs[wJobID].dstFlushed += toFlush;  /* can write : this value is only used by mtctx */

            if ( (srcConsumed == srcSize)    /* job is completed */
              && (mtctx->jobs[wJobID].dstFlushed == cSize) ) {   /* output buffer fully flushed => free this job position */
                DEBUGLOG(5, "Job %u completed (%u bytes), moving to next one",
                        mtctx->doneJobID, (U32)mtctx->jobs[wJobID].dstFlushed);
                ZSTDMT_releaseBuffer(mtctx->bufPool, mtctx->jobs[wJobID].dstBuff);
                DEBUGLOG(5, "dstBuffer released");
                mtctx->jobs[wJobID].dstBuff = g_nullBuffer;
                mtctx->jobs[wJobID].cSize = 0;   /* ensure this job slot is considered "not started" in future check */
                mtctx->consumed += srcSize;
                mtctx->produced += cSize;
                mtctx->doneJobID++;
        }   }

        /* return value : how many bytes left in buffer ; fake it to 1 when unknown but >0 */
        if (cSize > mtctx->jobs[wJobID].dstFlushed) return (cSize - mtctx->jobs[wJobID].dstFlushed);
        if (srcSize > srcConsumed) return 1;   /* current job not completely compressed */
    }
    if (mtctx->doneJobID < mtctx->nextJobID) return 1;   /* some more jobs ongoing */
    if (mtctx->jobReady) return 1;      /* one job is ready to push, just not yet in the list */
    if (mtctx->inBuff.filled > 0) return 1;   /* input is not empty, and still needs to be converted into a job */
    mtctx->allJobsCompleted = mtctx->frameEnded;   /* all jobs are entirely flushed => if this one is last one, frame is completed */
    if (end == ZSTD_e_end) return !mtctx->frameEnded;  /* for ZSTD_e_end, question becomes : is frame completed ? instead of : are internal buffers fully flushed ? */
    return 0;   /* internal buffers fully flushed */
}

/**
 * Returns the range of data used by the earliest job that is not yet complete.
 * If the data of the first job is broken up into two segments, we cover both
 * sections.
 */
static range_t ZSTDMT_getInputDataInUse(ZSTDMT_CCtx* mtctx)
{
    unsigned const firstJobID = mtctx->doneJobID;
    unsigned const lastJobID = mtctx->nextJobID;
    unsigned jobID;

    for (jobID = firstJobID; jobID < lastJobID; ++jobID) {
        unsigned const wJobID = jobID & mtctx->jobIDMask;
        size_t consumed;

        ZSTD_PTHREAD_MUTEX_LOCK(&mtctx->jobs[wJobID].job_mutex);
        consumed = mtctx->jobs[wJobID].consumed;
        ZSTD_pthread_mutex_unlock(&mtctx->jobs[wJobID].job_mutex);

        if (consumed < mtctx->jobs[wJobID].src.size) {
            range_t range = mtctx->jobs[wJobID].prefix;
            if (range.size == 0) {
                /* Empty prefix */
                range = mtctx->jobs[wJobID].src;
            }
            /* Job source in multiple segments not supported yet */
            assert(range.start <= mtctx->jobs[wJobID].src.start);
            return range;
        }
    }
    return kNullRange;
}

/**
 * Returns non-zero iff buffer and range overlap.
 */
static int ZSTDMT_isOverlapped(buffer_t buffer, range_t range)
{
    BYTE const* const bufferStart = (BYTE const*)buffer.start;
    BYTE const* const bufferEnd = bufferStart + buffer.capacity;
    BYTE const* const rangeStart = (BYTE const*)range.start;
    BYTE const* const rangeEnd = rangeStart + range.size;

    if (rangeStart == NULL || bufferStart == NULL)
        return 0;
    /* Empty ranges cannot overlap */
    if (bufferStart == bufferEnd || rangeStart == rangeEnd)
        return 0;

    return bufferStart < rangeEnd && rangeStart < bufferEnd;
}

static int ZSTDMT_doesOverlapWindow(buffer_t buffer, ZSTD_window_t window)
{
    range_t extDict;
    range_t prefix;

    DEBUGLOG(5, "ZSTDMT_doesOverlapWindow");
    extDict.start = window.dictBase + window.lowLimit;
    extDict.size = window.dictLimit - window.lowLimit;

    prefix.start = window.base + window.dictLimit;
    prefix.size = window.nextSrc - (window.base + window.dictLimit);
    DEBUGLOG(5, "extDict [0x%zx, 0x%zx)",
                (size_t)extDict.start,
                (size_t)extDict.start + extDict.size);
    DEBUGLOG(5, "prefix  [0x%zx, 0x%zx)",
                (size_t)prefix.start,
                (size_t)prefix.start + prefix.size);

    return ZSTDMT_isOverlapped(buffer, extDict)
        || ZSTDMT_isOverlapped(buffer, prefix);
}

static void ZSTDMT_waitForLdmComplete(ZSTDMT_CCtx* mtctx, buffer_t buffer)
{
    if (mtctx->params.ldmParams.enableLdm) {
        ZSTD_pthread_mutex_t* mutex = &mtctx->serial.ldmWindowMutex;
        DEBUGLOG(5, "ZSTDMT_waitForLdmComplete");
        DEBUGLOG(5, "source  [0x%zx, 0x%zx)",
                    (size_t)buffer.start,
                    (size_t)buffer.start + buffer.capacity);
        ZSTD_PTHREAD_MUTEX_LOCK(mutex);
        while (ZSTDMT_doesOverlapWindow(buffer, mtctx->serial.ldmWindow)) {
            DEBUGLOG(5, "Waiting for LDM to finish...");
            ZSTD_pthread_cond_wait(&mtctx->serial.ldmWindowCond, mutex);
        }
        DEBUGLOG(6, "Done waiting for LDM to finish");
        ZSTD_pthread_mutex_unlock(mutex);
    }
}

/**
 * Attempts to set the inBuff to the next section to fill.
 * If any part of the new section is still in use we give up.
 * Returns non-zero if the buffer is filled.
 */
static int ZSTDMT_tryGetInputRange(ZSTDMT_CCtx* mtctx)
{
    range_t const inUse = ZSTDMT_getInputDataInUse(mtctx);
    size_t const spaceLeft = mtctx->roundBuff.capacity - mtctx->roundBuff.pos;
    size_t const target = mtctx->targetSectionSize;
    buffer_t buffer;

    DEBUGLOG(5, "ZSTDMT_tryGetInputRange");
    assert(mtctx->inBuff.buffer.start == NULL);
    assert(mtctx->roundBuff.capacity >= target);

    if (spaceLeft < target) {
        /* ZSTD_invalidateRepCodes() doesn't work for extDict variants.
         * Simply copy the prefix to the beginning in that case.
         */
        BYTE* const start = (BYTE*)mtctx->roundBuff.buffer;
        size_t const prefixSize = mtctx->inBuff.prefix.size;

        buffer.start = start;
        buffer.capacity = prefixSize;
        if (ZSTDMT_isOverlapped(buffer, inUse)) {
            DEBUGLOG(5, "Waiting for buffer...");
            return 0;
        }
        ZSTDMT_waitForLdmComplete(mtctx, buffer);
        memmove(start, mtctx->inBuff.prefix.start, prefixSize);
        mtctx->inBuff.prefix.start = start;
        mtctx->roundBuff.pos = prefixSize;
    }
    buffer.start = mtctx->roundBuff.buffer + mtctx->roundBuff.pos;
    buffer.capacity = target;

    if (ZSTDMT_isOverlapped(buffer, inUse)) {
        DEBUGLOG(5, "Waiting for buffer...");
        return 0;
    }
    assert(!ZSTDMT_isOverlapped(buffer, mtctx->inBuff.prefix));

    ZSTDMT_waitForLdmComplete(mtctx, buffer);

    DEBUGLOG(5, "Using prefix range [%zx, %zx)",
                (size_t)mtctx->inBuff.prefix.start,
                (size_t)mtctx->inBuff.prefix.start + mtctx->inBuff.prefix.size);
    DEBUGLOG(5, "Using source range [%zx, %zx)",
                (size_t)buffer.start,
                (size_t)buffer.start + buffer.capacity);


    mtctx->inBuff.buffer = buffer;
    mtctx->inBuff.filled = 0;
    assert(mtctx->roundBuff.pos + buffer.capacity <= mtctx->roundBuff.capacity);
    return 1;
}

typedef struct {
  size_t toLoad;  /* The number of bytes to load from the input. */
  int flush;      /* Boolean declaring if we must flush because we found a synchronization point. */
} syncPoint_t;

/**
 * Searches through the input for a synchronization point. If one is found, we
 * will instruct the caller to flush, and return the number of bytes to load.
 * Otherwise, we will load as many bytes as possible and instruct the caller
 * to continue as normal.
 */
static syncPoint_t
findSynchronizationPoint(ZSTDMT_CCtx const* mtctx, ZSTD_inBuffer const input)
{
    BYTE const* const istart = (BYTE const*)input.src + input.pos;
    U64 const primePower = mtctx->rsync.primePower;
    U64 const hitMask = mtctx->rsync.hitMask;

    syncPoint_t syncPoint;
    U64 hash;
    BYTE const* prev;
    size_t pos;

    syncPoint.toLoad = MIN(input.size - input.pos, mtctx->targetSectionSize - mtctx->inBuff.filled);
    syncPoint.flush = 0;
    if (!mtctx->params.rsyncable)
        /* Rsync is disabled. */
        return syncPoint;
    if (mtctx->inBuff.filled + syncPoint.toLoad < RSYNC_LENGTH)
        /* Not enough to compute the hash.
         * We will miss any synchronization points in this RSYNC_LENGTH byte
         * window. However, since it depends only in the internal buffers, if the
         * state is already synchronized, we will remain synchronized.
         * Additionally, the probability that we miss a synchronization point is
         * low: RSYNC_LENGTH / targetSectionSize.
         */
        return syncPoint;
    /* Initialize the loop variables. */
    if (mtctx->inBuff.filled >= RSYNC_LENGTH) {
        /* We have enough bytes buffered to initialize the hash.
         * Start scanning at the beginning of the input.
         */
        pos = 0;
        prev = (BYTE const*)mtctx->inBuff.buffer.start + mtctx->inBuff.filled - RSYNC_LENGTH;
        hash = ZSTD_rollingHash_compute(prev, RSYNC_LENGTH);
    } else {
        /* We don't have enough bytes buffered to initialize the hash, but
         * we know we have at least RSYNC_LENGTH bytes total.
         * Start scanning after the first RSYNC_LENGTH bytes less the bytes
         * already buffered.
         */
        pos = RSYNC_LENGTH - mtctx->inBuff.filled;
        prev = (BYTE const*)mtctx->inBuff.buffer.start - pos;
        hash = ZSTD_rollingHash_compute(mtctx->inBuff.buffer.start, mtctx->inBuff.filled);
        hash = ZSTD_rollingHash_append(hash, istart, pos);
    }
    /* Starting with the hash of the previous RSYNC_LENGTH bytes, roll
     * through the input. If we hit a synchronization point, then cut the
     * job off, and tell the compressor to flush the job. Otherwise, load
     * all the bytes and continue as normal.
     * If we go too long without a synchronization point (targetSectionSize)
     * then a block will be emitted anyways, but this is okay, since if we
     * are already synchronized we will remain synchronized.
     */
    for (; pos < syncPoint.toLoad; ++pos) {
        BYTE const toRemove = pos < RSYNC_LENGTH ? prev[pos] : istart[pos - RSYNC_LENGTH];
        /* if (pos >= RSYNC_LENGTH) assert(ZSTD_rollingHash_compute(istart + pos - RSYNC_LENGTH, RSYNC_LENGTH) == hash); */
        hash = ZSTD_rollingHash_rotate(hash, toRemove, istart[pos], primePower);
        if ((hash & hitMask) == hitMask) {
            syncPoint.toLoad = pos + 1;
            syncPoint.flush = 1;
            break;
        }
    }
    return syncPoint;
}

size_t ZSTDMT_nextInputSizeHint(const ZSTDMT_CCtx* mtctx)
{
    size_t hintInSize = mtctx->targetSectionSize - mtctx->inBuff.filled;
    if (hintInSize==0) hintInSize = mtctx->targetSectionSize;
    return hintInSize;
}

/** ZSTDMT_compressStream_generic() :
 *  internal use only - exposed to be invoked from zstd_compress.c
 *  assumption : output and input are valid (pos <= size)
 * @return : minimum amount of data remaining to flush, 0 if none */
size_t ZSTDMT_compressStream_generic(ZSTDMT_CCtx* mtctx,
                                     ZSTD_outBuffer* output,
                                     ZSTD_inBuffer* input,
                                     ZSTD_EndDirective endOp)
{
    unsigned forwardInputProgress = 0;
    DEBUGLOG(5, "ZSTDMT_compressStream_generic (endOp=%u, srcSize=%u)",
                (U32)endOp, (U32)(input->size - input->pos));
    assert(output->pos <= output->size);
    assert(input->pos  <= input->size);

    if (mtctx->singleBlockingThread) {  /* delegate to single-thread (synchronous) */
        return ZSTD_compressStream_generic(mtctx->cctxPool->cctx[0], output, input, endOp);
    }

    if ((mtctx->frameEnded) && (endOp==ZSTD_e_continue)) {
        /* current frame being ended. Only flush/end are allowed */
        return ERROR(stage_wrong);
    }

    /* single-pass shortcut (note : synchronous-mode) */
    if ( (!mtctx->params.rsyncable)   /* rsyncable mode is disabled */
      && (mtctx->nextJobID == 0)      /* just started */
      && (mtctx->inBuff.filled == 0)  /* nothing buffered */
      && (!mtctx->jobReady)           /* no job already created */
      && (endOp == ZSTD_e_end)        /* end order */
      && (output->size - output->pos >= ZSTD_compressBound(input->size - input->pos)) ) { /* enough space in dst */
        size_t const cSize = ZSTDMT_compress_advanced_internal(mtctx,
                (char*)output->dst + output->pos, output->size - output->pos,
                (const char*)input->src + input->pos, input->size - input->pos,
                mtctx->cdict, mtctx->params);
        if (ZSTD_isError(cSize)) return cSize;
        input->pos = input->size;
        output->pos += cSize;
        mtctx->allJobsCompleted = 1;
        mtctx->frameEnded = 1;
        return 0;
    }

    /* fill input buffer */
    if ( (!mtctx->jobReady)
      && (input->size > input->pos) ) {   /* support NULL input */
        if (mtctx->inBuff.buffer.start == NULL) {
            assert(mtctx->inBuff.filled == 0); /* Can't fill an empty buffer */
            if (!ZSTDMT_tryGetInputRange(mtctx)) {
                /* It is only possible for this operation to fail if there are
                 * still compression jobs ongoing.
                 */
                DEBUGLOG(5, "ZSTDMT_tryGetInputRange failed");
                assert(mtctx->doneJobID != mtctx->nextJobID);
            } else
                DEBUGLOG(5, "ZSTDMT_tryGetInputRange completed successfully : mtctx->inBuff.buffer.start = %p", mtctx->inBuff.buffer.start);
        }
        if (mtctx->inBuff.buffer.start != NULL) {
            syncPoint_t const syncPoint = findSynchronizationPoint(mtctx, *input);
            if (syncPoint.flush && endOp == ZSTD_e_continue) {
                endOp = ZSTD_e_flush;
            }
            assert(mtctx->inBuff.buffer.capacity >= mtctx->targetSectionSize);
            DEBUGLOG(5, "ZSTDMT_compressStream_generic: adding %u bytes on top of %u to buffer of size %u",
                        (U32)syncPoint.toLoad, (U32)mtctx->inBuff.filled, (U32)mtctx->targetSectionSize);
            memcpy((char*)mtctx->inBuff.buffer.start + mtctx->inBuff.filled, (const char*)input->src + input->pos, syncPoint.toLoad);
            input->pos += syncPoint.toLoad;
            mtctx->inBuff.filled += syncPoint.toLoad;
            forwardInputProgress = syncPoint.toLoad>0;
        }
        if ((input->pos < input->size) && (endOp == ZSTD_e_end))
            endOp = ZSTD_e_flush;   /* can't end now : not all input consumed */
    }

    if ( (mtctx->jobReady)
      || (mtctx->inBuff.filled >= mtctx->targetSectionSize)  /* filled enough : let's compress */
      || ((endOp != ZSTD_e_continue) && (mtctx->inBuff.filled > 0))  /* something to flush : let's go */
      || ((endOp == ZSTD_e_end) && (!mtctx->frameEnded)) ) {   /* must finish the frame with a zero-size block */
        size_t const jobSize = mtctx->inBuff.filled;
        assert(mtctx->inBuff.filled <= mtctx->targetSectionSize);
        CHECK_F( ZSTDMT_createCompressionJob(mtctx, jobSize, endOp) );
    }

    /* check for potential compressed data ready to be flushed */
    {   size_t const remainingToFlush = ZSTDMT_flushProduced(mtctx, output, !forwardInputProgress, endOp); /* block if there was no forward input progress */
        if (input->pos < input->size) return MAX(remainingToFlush, 1);  /* input not consumed : do not end flush yet */
        DEBUGLOG(5, "end of ZSTDMT_compressStream_generic: remainingToFlush = %u", (U32)remainingToFlush);
        return remainingToFlush;
    }
}


size_t ZSTDMT_compressStream(ZSTDMT_CCtx* mtctx, ZSTD_outBuffer* output, ZSTD_inBuffer* input)
{
    CHECK_F( ZSTDMT_compressStream_generic(mtctx, output, input, ZSTD_e_continue) );

    /* recommended next input size : fill current input buffer */
    return mtctx->targetSectionSize - mtctx->inBuff.filled;   /* note : could be zero when input buffer is fully filled and no more availability to create new job */
}


static size_t ZSTDMT_flushStream_internal(ZSTDMT_CCtx* mtctx, ZSTD_outBuffer* output, ZSTD_EndDirective endFrame)
{
    size_t const srcSize = mtctx->inBuff.filled;
    DEBUGLOG(5, "ZSTDMT_flushStream_internal");

    if ( mtctx->jobReady     /* one job ready for a worker to pick up */
      || (srcSize > 0)       /* still some data within input buffer */
      || ((endFrame==ZSTD_e_end) && !mtctx->frameEnded)) {  /* need a last 0-size block to end frame */
           DEBUGLOG(5, "ZSTDMT_flushStream_internal : create a new job (%u bytes, end:%u)",
                        (U32)srcSize, (U32)endFrame);
        CHECK_F( ZSTDMT_createCompressionJob(mtctx, srcSize, endFrame) );
    }

    /* check if there is any data available to flush */
    return ZSTDMT_flushProduced(mtctx, output, 1 /* blockToFlush */, endFrame);
}


size_t ZSTDMT_flushStream(ZSTDMT_CCtx* mtctx, ZSTD_outBuffer* output)
{
    DEBUGLOG(5, "ZSTDMT_flushStream");
    if (mtctx->singleBlockingThread)
        return ZSTD_flushStream(mtctx->cctxPool->cctx[0], output);
    return ZSTDMT_flushStream_internal(mtctx, output, ZSTD_e_flush);
}

size_t ZSTDMT_endStream(ZSTDMT_CCtx* mtctx, ZSTD_outBuffer* output)
{
    DEBUGLOG(4, "ZSTDMT_endStream");
    if (mtctx->singleBlockingThread)
        return ZSTD_endStream(mtctx->cctxPool->cctx[0], output);
    return ZSTDMT_flushStream_internal(mtctx, output, ZSTD_e_end);
}
