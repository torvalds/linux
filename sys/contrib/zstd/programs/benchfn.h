/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/* benchfn :
 * benchmark any function on a set of input
 * providing result in nanoSecPerRun
 * or detecting and returning an error
 */

#if defined (__cplusplus)
extern "C" {
#endif

#ifndef BENCH_FN_H_23876
#define BENCH_FN_H_23876

/* ===  Dependencies  === */
#include <stddef.h>   /* size_t */


/* ====  Benchmark any function, iterated on a set of blocks  ==== */

/* BMK_runTime_t: valid result return type */

typedef struct {
    unsigned long long nanoSecPerRun;  /* time per iteration (over all blocks) */
    size_t sumOfReturn;         /* sum of return values */
} BMK_runTime_t;


/* BMK_runOutcome_t:
 * type expressing the outcome of a benchmark run by BMK_benchFunction(),
 * which can be either valid or invalid.
 * benchmark outcome can be invalid if errorFn is provided.
 * BMK_runOutcome_t must be considered "opaque" : never access its members directly.
 * Instead, use its assigned methods :
 * BMK_isSuccessful_runOutcome, BMK_extract_runTime, BMK_extract_errorResult.
 * The structure is only described here to allow its allocation on stack. */

typedef struct {
    BMK_runTime_t internal_never_ever_use_directly;
    size_t error_result_never_ever_use_directly;
    int error_tag_never_ever_use_directly;
} BMK_runOutcome_t;


/* prototypes for benchmarked functions */
typedef size_t (*BMK_benchFn_t)(const void* src, size_t srcSize, void* dst, size_t dstCapacity, void* customPayload);
typedef size_t (*BMK_initFn_t)(void* initPayload);
typedef unsigned (*BMK_errorFn_t)(size_t);


/* BMK_benchFunction() parameters are provided through following structure.
 * This is preferable for readability,
 * as the number of parameters required is pretty large.
 * No initializer is provided, because it doesn't make sense to provide some "default" :
 * all parameters should be specified by the caller */
typedef struct {
    BMK_benchFn_t benchFn;   /* the function to benchmark, over the set of blocks */
    void* benchPayload;      /* pass custom parameters to benchFn  :
                              * (*benchFn)(srcBuffers[i], srcSizes[i], dstBuffers[i], dstCapacities[i], benchPayload) */
    BMK_initFn_t initFn;     /* (*initFn)(initPayload) is run once per run, at the beginning. */
    void* initPayload;       /* Both arguments can be NULL, in which case nothing is run. */
    BMK_errorFn_t errorFn;   /* errorFn will check each return value of benchFn over each block, to determine if it failed or not.
                              * errorFn can be NULL, in which case no check is performed.
                              * errorFn must return 0 when benchFn was successful, and >= 1 if it detects an error.
                              * Execution is stopped as soon as an error is detected.
                              * the triggering return value can be retrieved using BMK_extract_errorResult(). */
    size_t blockCount;       /* number of blocks to operate benchFn on.
                              * It's also the size of all array parameters :
                              * srcBuffers, srcSizes, dstBuffers, dstCapacities, blockResults */
    const void *const * srcBuffers; /* array of buffers to be operated on by benchFn */
    const size_t* srcSizes;  /* array of the sizes of srcBuffers buffers */
    void *const * dstBuffers;/* array of buffers to be written into by benchFn */
    const size_t* dstCapacities; /* array of the capacities of dstBuffers buffers */
    size_t* blockResults;    /* Optional: store the return value of benchFn for each block. Use NULL if this result is not requested. */
} BMK_benchParams_t;


/* BMK_benchFunction() :
 * This function benchmarks benchFn and initFn, providing a result.
 *
 * params : see description of BMK_benchParams_t above.
 * nbLoops: defines number of times benchFn is run over the full set of blocks.
 *          Minimum value is 1. A 0 is interpreted as a 1.
 *
 * @return: can express either an error or a successful result.
 *          Use BMK_isSuccessful_runOutcome() to check if benchmark was successful.
 *          If yes, extract the result with BMK_extract_runTime(),
 *          it will contain :
 *              .sumOfReturn : the sum of all return values of benchFn through all of blocks
 *              .nanoSecPerRun : time per run of benchFn + (time for initFn / nbLoops)
 *          .sumOfReturn is generally intended for functions which return a # of bytes written into dstBuffer,
 *              in which case, this value will be the total amount of bytes written into dstBuffer.
 *
 * blockResults : when provided (!= NULL), and when benchmark is successful,
 *                params.blockResults contains all return values of `benchFn` over all blocks.
 *                when provided (!= NULL), and when benchmark failed,
 *                params.blockResults contains return values of `benchFn` over all blocks preceding and including the failed block.
 */
BMK_runOutcome_t BMK_benchFunction(BMK_benchParams_t params, unsigned nbLoops);



/* check first if the benchmark was successful or not */
int BMK_isSuccessful_runOutcome(BMK_runOutcome_t outcome);

/* If the benchmark was successful, extract the result.
 * note : this function will abort() program execution if benchmark failed !
 *        always check if benchmark was successful first !
 */
BMK_runTime_t BMK_extract_runTime(BMK_runOutcome_t outcome);

/* when benchmark failed, it means one invocation of `benchFn` failed.
 * The failure was detected by `errorFn`, operating on return values of `benchFn`.
 * Returns the faulty return value.
 * note : this function will abort() program execution if benchmark did not failed.
 *        always check if benchmark failed first !
 */
size_t BMK_extract_errorResult(BMK_runOutcome_t outcome);



/* ====  Benchmark any function, returning intermediate results  ==== */

/* state information tracking benchmark session */
typedef struct BMK_timedFnState_s BMK_timedFnState_t;

/* BMK_benchTimedFn() :
 * Similar to BMK_benchFunction(), most arguments being identical.
 * Automatically determines `nbLoops` so that each result is regularly produced at interval of about run_ms.
 * Note : minimum `nbLoops` is 1, therefore a run may last more than run_ms, and possibly even more than total_ms.
 * Usage - initialize timedFnState, select benchmark duration (total_ms) and each measurement duration (run_ms)
 *         call BMK_benchTimedFn() repetitively, each measurement is supposed to last about run_ms
 *         Check if total time budget is spent or exceeded, using BMK_isCompleted_TimedFn()
 */
BMK_runOutcome_t BMK_benchTimedFn(BMK_timedFnState_t* timedFnState,
                                  BMK_benchParams_t params);

/* Tells if duration of all benchmark runs has exceeded total_ms
 */
int BMK_isCompleted_TimedFn(const BMK_timedFnState_t* timedFnState);

/* BMK_createTimedFnState() and BMK_resetTimedFnState() :
 * Create/Set BMK_timedFnState_t for next benchmark session,
 * which shall last a minimum of total_ms milliseconds,
 * producing intermediate results, paced at interval of (approximately) run_ms.
 */
BMK_timedFnState_t* BMK_createTimedFnState(unsigned total_ms, unsigned run_ms);
void BMK_resetTimedFnState(BMK_timedFnState_t* timedFnState, unsigned total_ms, unsigned run_ms);
void BMK_freeTimedFnState(BMK_timedFnState_t* state);



#endif   /* BENCH_FN_H_23876 */

#if defined (__cplusplus)
}
#endif
