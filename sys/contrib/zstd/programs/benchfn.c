/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */



/* *************************************
*  Includes
***************************************/
#include "platform.h"    /* Large Files support */
#include "util.h"        /* UTIL_getFileSize, UTIL_sleep */
#include <stdlib.h>      /* malloc, free */
#include <string.h>      /* memset */
#include <stdio.h>       /* fprintf, fopen */
#undef NDEBUG            /* assert must not be disabled */
#include <assert.h>      /* assert */

#include "mem.h"
#include "benchfn.h"


/* *************************************
*  Constants
***************************************/
#define TIMELOOP_MICROSEC     (1*1000000ULL) /* 1 second */
#define TIMELOOP_NANOSEC      (1*1000000000ULL) /* 1 second */
#define ACTIVEPERIOD_MICROSEC (70*TIMELOOP_MICROSEC) /* 70 seconds */
#define COOLPERIOD_SEC        10

#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)


/* *************************************
*  Errors
***************************************/
#ifndef DEBUG
#  define DEBUG 0
#endif

#define DISPLAY(...)       fprintf(stderr, __VA_ARGS__)
#define DEBUGOUTPUT(...) { if (DEBUG) DISPLAY(__VA_ARGS__); }

/* error without displaying */
#define RETURN_QUIET_ERROR(retValue, ...) {           \
    DEBUGOUTPUT("%s: %i: \n", __FILE__, __LINE__);    \
    DEBUGOUTPUT("Error : ");                          \
    DEBUGOUTPUT(__VA_ARGS__);                         \
    DEBUGOUTPUT(" \n");                               \
    return retValue;                                  \
}


/* *************************************
*  Benchmarking an arbitrary function
***************************************/

int BMK_isSuccessful_runOutcome(BMK_runOutcome_t outcome)
{
    return outcome.error_tag_never_ever_use_directly == 0;
}

/* warning : this function will stop program execution if outcome is invalid !
 *           check outcome validity first, using BMK_isValid_runResult() */
BMK_runTime_t BMK_extract_runTime(BMK_runOutcome_t outcome)
{
    assert(outcome.error_tag_never_ever_use_directly == 0);
    return outcome.internal_never_ever_use_directly;
}

size_t BMK_extract_errorResult(BMK_runOutcome_t outcome)
{
    assert(outcome.error_tag_never_ever_use_directly != 0);
    return outcome.error_result_never_ever_use_directly;
}

static BMK_runOutcome_t BMK_runOutcome_error(size_t errorResult)
{
    BMK_runOutcome_t b;
    memset(&b, 0, sizeof(b));
    b.error_tag_never_ever_use_directly = 1;
    b.error_result_never_ever_use_directly = errorResult;
    return b;
}

static BMK_runOutcome_t BMK_setValid_runTime(BMK_runTime_t runTime)
{
    BMK_runOutcome_t outcome;
    outcome.error_tag_never_ever_use_directly = 0;
    outcome.internal_never_ever_use_directly = runTime;
    return outcome;
}


/* initFn will be measured once, benchFn will be measured `nbLoops` times */
/* initFn is optional, provide NULL if none */
/* benchFn must return a size_t value that errorFn can interpret */
/* takes # of blocks and list of size & stuff for each. */
/* can report result of benchFn for each block into blockResult. */
/* blockResult is optional, provide NULL if this information is not required */
/* note : time per loop can be reported as zero if run time < timer resolution */
BMK_runOutcome_t BMK_benchFunction(BMK_benchParams_t p,
                                   unsigned nbLoops)
{
    size_t dstSize = 0;
    nbLoops += !nbLoops;   /* minimum nbLoops is 1 */

    /* init */
    {   size_t i;
        for(i = 0; i < p.blockCount; i++) {
            memset(p.dstBuffers[i], 0xE5, p.dstCapacities[i]);  /* warm up and erase result buffer */
        }
#if 0
        /* based on testing these seem to lower accuracy of multiple calls of 1 nbLoops vs 1 call of multiple nbLoops
         * (Makes former slower)
         */
        UTIL_sleepMilli(5);  /* give processor time to other processes */
        UTIL_waitForNextTick();
#endif
    }

    /* benchmark */
    {   UTIL_time_t const clockStart = UTIL_getTime();
        unsigned loopNb, blockNb;
        if (p.initFn != NULL) p.initFn(p.initPayload);
        for (loopNb = 0; loopNb < nbLoops; loopNb++) {
            for (blockNb = 0; blockNb < p.blockCount; blockNb++) {
                size_t const res = p.benchFn(p.srcBuffers[blockNb], p.srcSizes[blockNb],
                                   p.dstBuffers[blockNb], p.dstCapacities[blockNb],
                                   p.benchPayload);
                if (loopNb == 0) {
                    if (p.blockResults != NULL) p.blockResults[blockNb] = res;
                    if ((p.errorFn != NULL) && (p.errorFn(res))) {
                        RETURN_QUIET_ERROR(BMK_runOutcome_error(res),
                            "Function benchmark failed on block %u (of size %u) with error %i",
                            blockNb, (unsigned)p.srcSizes[blockNb], (int)res);
                    }
                    dstSize += res;
            }   }
        }  /* for (loopNb = 0; loopNb < nbLoops; loopNb++) */

        {   U64 const totalTime = UTIL_clockSpanNano(clockStart);
            BMK_runTime_t rt;
            rt.nanoSecPerRun = totalTime / nbLoops;
            rt.sumOfReturn = dstSize;
            return BMK_setValid_runTime(rt);
    }   }
}


/* ====  Benchmarking any function, providing intermediate results  ==== */

struct BMK_timedFnState_s {
    U64 timeSpent_ns;
    U64 timeBudget_ns;
    U64 runBudget_ns;
    BMK_runTime_t fastestRun;
    unsigned nbLoops;
    UTIL_time_t coolTime;
};  /* typedef'd to BMK_timedFnState_t within bench.h */

BMK_timedFnState_t* BMK_createTimedFnState(unsigned total_ms, unsigned run_ms)
{
    BMK_timedFnState_t* const r = (BMK_timedFnState_t*)malloc(sizeof(*r));
    if (r == NULL) return NULL;   /* malloc() error */
    BMK_resetTimedFnState(r, total_ms, run_ms);
    return r;
}

void BMK_freeTimedFnState(BMK_timedFnState_t* state) {
    free(state);
}

void BMK_resetTimedFnState(BMK_timedFnState_t* timedFnState, unsigned total_ms, unsigned run_ms)
{
    if (!total_ms) total_ms = 1 ;
    if (!run_ms) run_ms = 1;
    if (run_ms > total_ms) run_ms = total_ms;
    timedFnState->timeSpent_ns = 0;
    timedFnState->timeBudget_ns = (U64)total_ms * TIMELOOP_NANOSEC / 1000;
    timedFnState->runBudget_ns = (U64)run_ms * TIMELOOP_NANOSEC / 1000;
    timedFnState->fastestRun.nanoSecPerRun = (U64)(-1LL);
    timedFnState->fastestRun.sumOfReturn = (size_t)(-1LL);
    timedFnState->nbLoops = 1;
    timedFnState->coolTime = UTIL_getTime();
}

/* Tells if nb of seconds set in timedFnState for all runs is spent.
 * note : this function will return 1 if BMK_benchFunctionTimed() has actually errored. */
int BMK_isCompleted_TimedFn(const BMK_timedFnState_t* timedFnState)
{
    return (timedFnState->timeSpent_ns >= timedFnState->timeBudget_ns);
}


#undef MIN
#define MIN(a,b)   ( (a) < (b) ? (a) : (b) )

#define MINUSABLETIME  (TIMELOOP_NANOSEC / 2)  /* 0.5 seconds */

BMK_runOutcome_t BMK_benchTimedFn(BMK_timedFnState_t* cont,
                                  BMK_benchParams_t p)
{
    U64 const runBudget_ns = cont->runBudget_ns;
    U64 const runTimeMin_ns = runBudget_ns / 2;
    int completed = 0;
    BMK_runTime_t bestRunTime = cont->fastestRun;

    while (!completed) {
        BMK_runOutcome_t runResult;

        /* Overheat protection */
        if (UTIL_clockSpanMicro(cont->coolTime) > ACTIVEPERIOD_MICROSEC) {
            DEBUGOUTPUT("\rcooling down ...    \r");
            UTIL_sleep(COOLPERIOD_SEC);
            cont->coolTime = UTIL_getTime();
        }

        /* reinitialize capacity */
        runResult = BMK_benchFunction(p, cont->nbLoops);

        if(!BMK_isSuccessful_runOutcome(runResult)) { /* error : move out */
            return runResult;
        }

        {   BMK_runTime_t const newRunTime = BMK_extract_runTime(runResult);
            U64 const loopDuration_ns = newRunTime.nanoSecPerRun * cont->nbLoops;

            cont->timeSpent_ns += loopDuration_ns;

            /* estimate nbLoops for next run to last approximately 1 second */
            if (loopDuration_ns > (runBudget_ns / 50)) {
                U64 const fastestRun_ns = MIN(bestRunTime.nanoSecPerRun, newRunTime.nanoSecPerRun);
                cont->nbLoops = (U32)(runBudget_ns / fastestRun_ns) + 1;
            } else {
                /* previous run was too short : blindly increase workload by x multiplier */
                const unsigned multiplier = 10;
                assert(cont->nbLoops < ((unsigned)-1) / multiplier);  /* avoid overflow */
                cont->nbLoops *= multiplier;
            }

            if(loopDuration_ns < runTimeMin_ns) {
                /* don't report results for which benchmark run time was too small : increased risks of rounding errors */
                assert(completed == 0);
                continue;
            } else {
                if(newRunTime.nanoSecPerRun < bestRunTime.nanoSecPerRun) {
                    bestRunTime = newRunTime;
                }
                completed = 1;
            }
        }
    }   /* while (!completed) */

    return BMK_setValid_runTime(bestRunTime);
}
