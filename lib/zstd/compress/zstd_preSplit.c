// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include "../common/compiler.h" /* ZSTD_ALIGNOF */
#include "../common/mem.h" /* S64 */
#include "../common/zstd_deps.h" /* ZSTD_memset */
#include "../common/zstd_internal.h" /* ZSTD_STATIC_ASSERT */
#include "hist.h" /* HIST_add */
#include "zstd_preSplit.h"


#define BLOCKSIZE_MIN 3500
#define THRESHOLD_PENALTY_RATE 16
#define THRESHOLD_BASE (THRESHOLD_PENALTY_RATE - 2)
#define THRESHOLD_PENALTY 3

#define HASHLENGTH 2
#define HASHLOG_MAX 10
#define HASHTABLESIZE (1 << HASHLOG_MAX)
#define HASHMASK (HASHTABLESIZE - 1)
#define KNUTH 0x9e3779b9

/* for hashLog > 8, hash 2 bytes.
 * for hashLog == 8, just take the byte, no hashing.
 * The speed of this method relies on compile-time constant propagation */
FORCE_INLINE_TEMPLATE unsigned hash2(const void *p, unsigned hashLog)
{
    assert(hashLog >= 8);
    if (hashLog == 8) return (U32)((const BYTE*)p)[0];
    assert(hashLog <= HASHLOG_MAX);
    return (U32)(MEM_read16(p)) * KNUTH >> (32 - hashLog);
}


typedef struct {
  unsigned events[HASHTABLESIZE];
  size_t nbEvents;
} Fingerprint;
typedef struct {
    Fingerprint pastEvents;
    Fingerprint newEvents;
} FPStats;

static void initStats(FPStats* fpstats)
{
    ZSTD_memset(fpstats, 0, sizeof(FPStats));
}

FORCE_INLINE_TEMPLATE void
addEvents_generic(Fingerprint* fp, const void* src, size_t srcSize, size_t samplingRate, unsigned hashLog)
{
    const char* p = (const char*)src;
    size_t limit = srcSize - HASHLENGTH + 1;
    size_t n;
    assert(srcSize >= HASHLENGTH);
    for (n = 0; n < limit; n+=samplingRate) {
        fp->events[hash2(p+n, hashLog)]++;
    }
    fp->nbEvents += limit/samplingRate;
}

FORCE_INLINE_TEMPLATE void
recordFingerprint_generic(Fingerprint* fp, const void* src, size_t srcSize, size_t samplingRate, unsigned hashLog)
{
    ZSTD_memset(fp, 0, sizeof(unsigned) * ((size_t)1 << hashLog));
    fp->nbEvents = 0;
    addEvents_generic(fp, src, srcSize, samplingRate, hashLog);
}

typedef void (*RecordEvents_f)(Fingerprint* fp, const void* src, size_t srcSize);

#define FP_RECORD(_rate) ZSTD_recordFingerprint_##_rate

#define ZSTD_GEN_RECORD_FINGERPRINT(_rate, _hSize)                                 \
    static void FP_RECORD(_rate)(Fingerprint* fp, const void* src, size_t srcSize) \
    {                                                                              \
        recordFingerprint_generic(fp, src, srcSize, _rate, _hSize);                \
    }

ZSTD_GEN_RECORD_FINGERPRINT(1, 10)
ZSTD_GEN_RECORD_FINGERPRINT(5, 10)
ZSTD_GEN_RECORD_FINGERPRINT(11, 9)
ZSTD_GEN_RECORD_FINGERPRINT(43, 8)


static U64 abs64(S64 s64) { return (U64)((s64 < 0) ? -s64 : s64); }

static U64 fpDistance(const Fingerprint* fp1, const Fingerprint* fp2, unsigned hashLog)
{
    U64 distance = 0;
    size_t n;
    assert(hashLog <= HASHLOG_MAX);
    for (n = 0; n < ((size_t)1 << hashLog); n++) {
        distance +=
            abs64((S64)fp1->events[n] * (S64)fp2->nbEvents - (S64)fp2->events[n] * (S64)fp1->nbEvents);
    }
    return distance;
}

/* Compare newEvents with pastEvents
 * return 1 when considered "too different"
 */
static int compareFingerprints(const Fingerprint* ref,
                            const Fingerprint* newfp,
                            int penalty,
                            unsigned hashLog)
{
    assert(ref->nbEvents > 0);
    assert(newfp->nbEvents > 0);
    {   U64 p50 = (U64)ref->nbEvents * (U64)newfp->nbEvents;
        U64 deviation = fpDistance(ref, newfp, hashLog);
        U64 threshold = p50 * (U64)(THRESHOLD_BASE + penalty) / THRESHOLD_PENALTY_RATE;
        return deviation >= threshold;
    }
}

static void mergeEvents(Fingerprint* acc, const Fingerprint* newfp)
{
    size_t n;
    for (n = 0; n < HASHTABLESIZE; n++) {
        acc->events[n] += newfp->events[n];
    }
    acc->nbEvents += newfp->nbEvents;
}

static void flushEvents(FPStats* fpstats)
{
    size_t n;
    for (n = 0; n < HASHTABLESIZE; n++) {
        fpstats->pastEvents.events[n] = fpstats->newEvents.events[n];
    }
    fpstats->pastEvents.nbEvents = fpstats->newEvents.nbEvents;
    ZSTD_memset(&fpstats->newEvents, 0, sizeof(fpstats->newEvents));
}

static void removeEvents(Fingerprint* acc, const Fingerprint* slice)
{
    size_t n;
    for (n = 0; n < HASHTABLESIZE; n++) {
        assert(acc->events[n] >= slice->events[n]);
        acc->events[n] -= slice->events[n];
    }
    acc->nbEvents -= slice->nbEvents;
}

#define CHUNKSIZE (8 << 10)
static size_t ZSTD_splitBlock_byChunks(const void* blockStart, size_t blockSize,
                        int level,
                        void* workspace, size_t wkspSize)
{
    static const RecordEvents_f records_fs[] = {
        FP_RECORD(43), FP_RECORD(11), FP_RECORD(5), FP_RECORD(1)
    };
    static const unsigned hashParams[] = { 8, 9, 10, 10 };
    const RecordEvents_f record_f = (assert(0<=level && level<=3), records_fs[level]);
    FPStats* const fpstats = (FPStats*)workspace;
    const char* p = (const char*)blockStart;
    int penalty = THRESHOLD_PENALTY;
    size_t pos = 0;
    assert(blockSize == (128 << 10));
    assert(workspace != NULL);
    assert((size_t)workspace % ZSTD_ALIGNOF(FPStats) == 0);
    ZSTD_STATIC_ASSERT(ZSTD_SLIPBLOCK_WORKSPACESIZE >= sizeof(FPStats));
    assert(wkspSize >= sizeof(FPStats)); (void)wkspSize;

    initStats(fpstats);
    record_f(&fpstats->pastEvents, p, CHUNKSIZE);
    for (pos = CHUNKSIZE; pos <= blockSize - CHUNKSIZE; pos += CHUNKSIZE) {
        record_f(&fpstats->newEvents, p + pos, CHUNKSIZE);
        if (compareFingerprints(&fpstats->pastEvents, &fpstats->newEvents, penalty, hashParams[level])) {
            return pos;
        } else {
            mergeEvents(&fpstats->pastEvents, &fpstats->newEvents);
            if (penalty > 0) penalty--;
        }
    }
    assert(pos == blockSize);
    return blockSize;
    (void)flushEvents; (void)removeEvents;
}

/* ZSTD_splitBlock_fromBorders(): very fast strategy :
 * compare fingerprint from beginning and end of the block,
 * derive from their difference if it's preferable to split in the middle,
 * repeat the process a second time, for finer grained decision.
 * 3 times did not brought improvements, so I stopped at 2.
 * Benefits are good enough for a cheap heuristic.
 * More accurate splitting saves more, but speed impact is also more perceptible.
 * For better accuracy, use more elaborate variant *_byChunks.
 */
static size_t ZSTD_splitBlock_fromBorders(const void* blockStart, size_t blockSize,
                        void* workspace, size_t wkspSize)
{
#define SEGMENT_SIZE 512
    FPStats* const fpstats = (FPStats*)workspace;
    Fingerprint* middleEvents = (Fingerprint*)(void*)((char*)workspace + 512 * sizeof(unsigned));
    assert(blockSize == (128 << 10));
    assert(workspace != NULL);
    assert((size_t)workspace % ZSTD_ALIGNOF(FPStats) == 0);
    ZSTD_STATIC_ASSERT(ZSTD_SLIPBLOCK_WORKSPACESIZE >= sizeof(FPStats));
    assert(wkspSize >= sizeof(FPStats)); (void)wkspSize;

    initStats(fpstats);
    HIST_add(fpstats->pastEvents.events, blockStart, SEGMENT_SIZE);
    HIST_add(fpstats->newEvents.events, (const char*)blockStart + blockSize - SEGMENT_SIZE, SEGMENT_SIZE);
    fpstats->pastEvents.nbEvents = fpstats->newEvents.nbEvents = SEGMENT_SIZE;
    if (!compareFingerprints(&fpstats->pastEvents, &fpstats->newEvents, 0, 8))
        return blockSize;

    HIST_add(middleEvents->events, (const char*)blockStart + blockSize/2 - SEGMENT_SIZE/2, SEGMENT_SIZE);
    middleEvents->nbEvents = SEGMENT_SIZE;
    {   U64 const distFromBegin = fpDistance(&fpstats->pastEvents, middleEvents, 8);
        U64 const distFromEnd = fpDistance(&fpstats->newEvents, middleEvents, 8);
        U64 const minDistance = SEGMENT_SIZE * SEGMENT_SIZE / 3;
        if (abs64((S64)distFromBegin - (S64)distFromEnd) < minDistance)
            return 64 KB;
        return (distFromBegin > distFromEnd) ? 32 KB : 96 KB;
    }
}

size_t ZSTD_splitBlock(const void* blockStart, size_t blockSize,
                    int level,
                    void* workspace, size_t wkspSize)
{
    DEBUGLOG(6, "ZSTD_splitBlock (level=%i)", level);
    assert(0<=level && level<=4);
    if (level == 0)
        return ZSTD_splitBlock_fromBorders(blockStart, blockSize, workspace, wkspSize);
    /* level >= 1*/
    return ZSTD_splitBlock_byChunks(blockStart, blockSize, level-1, workspace, wkspSize);
}
