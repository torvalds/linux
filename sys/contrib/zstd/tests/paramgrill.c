/*
 * Copyright (c) 2015-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/*-************************************
*  Dependencies
**************************************/
#include "util.h"      /* Ensure platform.h is compiled first; also : compiler options, UTIL_GetFileSize */
#include <stdlib.h>    /* malloc */
#include <stdio.h>     /* fprintf, fopen, ftello64 */
#include <string.h>    /* strcmp */
#include <math.h>      /* log */
#include <assert.h>

#include "mem.h"
#define ZSTD_STATIC_LINKING_ONLY   /* ZSTD_parameters, ZSTD_estimateCCtxSize */
#include "zstd.h"
#include "datagen.h"
#include "xxhash.h"
#include "benchfn.h"
#include "benchzstd.h"
#include "zstd_errors.h"
#include "zstd_internal.h"     /* should not be needed */


/*-************************************
*  Constants
**************************************/
#define PROGRAM_DESCRIPTION "ZSTD parameters tester"
#define AUTHOR "Yann Collet"
#define WELCOME_MESSAGE "*** %s %s %i-bits, by %s ***\n", PROGRAM_DESCRIPTION, ZSTD_VERSION_STRING, (int)(sizeof(void*)*8), AUTHOR

#define TIMELOOP_NANOSEC      (1*1000000000ULL) /* 1 second */
#define NB_LEVELS_TRACKED 22   /* ensured being >= ZSTD_maxCLevel() in BMK_init_level_constraints() */

static const size_t maxMemory = (sizeof(size_t)==4)  ?  (2 GB - 64 MB) : (size_t)(1ULL << ((sizeof(size_t)*8)-31));

#define COMPRESSIBILITY_DEFAULT 0.50

static const U64 g_maxVariationTime = 60 * SEC_TO_MICRO;
static const int g_maxNbVariations = 64;


/*-************************************
*  Macros
**************************************/
#define DISPLAY(...)  fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(n, ...) if(g_displayLevel >= n) { fprintf(stderr, __VA_ARGS__); }
#define DEBUGOUTPUT(...) { if (DEBUG) DISPLAY(__VA_ARGS__); }

#define TIMED 0
#ifndef DEBUG
#  define DEBUG 0
#endif

#undef MIN
#undef MAX
#define MIN(a,b)   ( (a) < (b) ? (a) : (b) )
#define MAX(a,b)   ( (a) > (b) ? (a) : (b) )
#define CUSTOM_LEVEL 99
#define BASE_CLEVEL 1

#define FADT_MIN 0
#define FADT_MAX ((U32)-1)

#define WLOG_RANGE (ZSTD_WINDOWLOG_MAX - ZSTD_WINDOWLOG_MIN + 1)
#define CLOG_RANGE (ZSTD_CHAINLOG_MAX - ZSTD_CHAINLOG_MIN + 1)
#define HLOG_RANGE (ZSTD_HASHLOG_MAX - ZSTD_HASHLOG_MIN + 1)
#define SLOG_RANGE (ZSTD_SEARCHLOG_MAX - ZSTD_SEARCHLOG_MIN + 1)
#define MML_RANGE  (ZSTD_MINMATCH_MAX - ZSTD_MINMATCH_MIN + 1)
#define TLEN_RANGE  17
#define STRT_RANGE (ZSTD_STRATEGY_MAX - ZSTD_STRATEGY_MIN + 1)
#define FADT_RANGE   3

#define CHECKTIME(r) { if(BMK_timeSpan_s(g_time) > g_timeLimit_s) { DEBUGOUTPUT("Time Limit Reached\n"); return r; } }
#define CHECKTIMEGT(ret, val, _gototag) { if(BMK_timeSpan_s(g_time) > g_timeLimit_s) { DEBUGOUTPUT("Time Limit Reached\n"); ret = val; goto _gototag; } }

#define PARAM_UNSET ((U32)-2) /* can't be -1 b/c fadt uses -1 */

static const char* g_stratName[ZSTD_STRATEGY_MAX+1] = {
                "(none)       ", "ZSTD_fast    ", "ZSTD_dfast   ",
                "ZSTD_greedy  ", "ZSTD_lazy    ", "ZSTD_lazy2   ",
                "ZSTD_btlazy2 ", "ZSTD_btopt   ", "ZSTD_btultra ",
                "ZSTD_btultra2"};

static const U32 tlen_table[TLEN_RANGE] = { 0, 1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 256, 512, 999 };


/*-************************************
*  Setup for Adding new params
**************************************/

/* indices for each of the variables */
typedef enum {
    wlog_ind = 0,
    clog_ind = 1,
    hlog_ind = 2,
    slog_ind = 3,
    mml_ind  = 4,
    tlen_ind = 5,
    strt_ind = 6,
    fadt_ind = 7, /* forceAttachDict */
    NUM_PARAMS = 8
} varInds_t;

typedef struct {
    U32 vals[NUM_PARAMS];
} paramValues_t;

/* minimum value of parameters */
static const U32 mintable[NUM_PARAMS] =
        { ZSTD_WINDOWLOG_MIN, ZSTD_CHAINLOG_MIN, ZSTD_HASHLOG_MIN, ZSTD_SEARCHLOG_MIN, ZSTD_MINMATCH_MIN, ZSTD_TARGETLENGTH_MIN, ZSTD_STRATEGY_MIN, FADT_MIN };

/* maximum value of parameters */
static const U32 maxtable[NUM_PARAMS] =
        { ZSTD_WINDOWLOG_MAX, ZSTD_CHAINLOG_MAX, ZSTD_HASHLOG_MAX, ZSTD_SEARCHLOG_MAX, ZSTD_MINMATCH_MAX, ZSTD_TARGETLENGTH_MAX, ZSTD_STRATEGY_MAX, FADT_MAX };

/* # of values parameters can take on */
static const U32 rangetable[NUM_PARAMS] =
        { WLOG_RANGE, CLOG_RANGE, HLOG_RANGE, SLOG_RANGE, MML_RANGE, TLEN_RANGE, STRT_RANGE, FADT_RANGE };

/* ZSTD_cctxSetParameter() index to set */
static const ZSTD_cParameter cctxSetParamTable[NUM_PARAMS] =
        { ZSTD_c_windowLog, ZSTD_c_chainLog, ZSTD_c_hashLog, ZSTD_c_searchLog, ZSTD_c_minMatch, ZSTD_c_targetLength, ZSTD_c_strategy, ZSTD_c_forceAttachDict };

/* names of parameters */
static const char* g_paramNames[NUM_PARAMS] =
        { "windowLog", "chainLog", "hashLog","searchLog", "minMatch", "targetLength", "strategy", "forceAttachDict" };

/* shortened names of parameters */
static const char* g_shortParamNames[NUM_PARAMS] =
        { "wlog", "clog", "hlog", "slog", "mml", "tlen", "strat", "fadt" };

/* maps value from { 0 to rangetable[param] - 1 } to valid paramvalues */
static U32 rangeMap(varInds_t param, int ind)
{
    ind = MAX(MIN(ind, (int)rangetable[param] - 1), 0);
    switch(param) {
        case wlog_ind: /* using default: triggers -Wswitch-enum */
        case clog_ind:
        case hlog_ind:
        case slog_ind:
        case mml_ind:
        case strt_ind:
            return mintable[param] + ind;
        case tlen_ind:
            return tlen_table[ind];
        case fadt_ind: /* 0, 1, 2 -> -1, 0, 1 */
            return ind - 1;
        case NUM_PARAMS:
        default:;
    }
    DISPLAY("Error, not a valid param\n ");
    assert(0);
    return (U32)-1;
}

/* inverse of rangeMap */
static int invRangeMap(varInds_t param, U32 value)
{
    value = MIN(MAX(mintable[param], value), maxtable[param]);
    switch(param) {
        case wlog_ind:
        case clog_ind:
        case hlog_ind:
        case slog_ind:
        case mml_ind:
        case strt_ind:
            return value - mintable[param];
        case tlen_ind: /* bin search */
        {
            int lo = 0;
            int hi = TLEN_RANGE;
            while(lo < hi) {
                int mid = (lo + hi) / 2;
                if(tlen_table[mid] < value) {
                    lo = mid + 1;
                } if(tlen_table[mid] == value) {
                    return mid;
                } else {
                    hi = mid;
                }
            }
            return lo;
        }
        case fadt_ind:
            return (int)value + 1;
        case NUM_PARAMS:
        default:;
    }
    DISPLAY("Error, not a valid param\n ");
    assert(0);
    return -2;
}

/* display of params */
static void displayParamVal(FILE* f, varInds_t param, unsigned value, int width)
{
    switch(param) {
        case wlog_ind:
        case clog_ind:
        case hlog_ind:
        case slog_ind:
        case mml_ind:
        case tlen_ind:
            if(width) {
                fprintf(f, "%*u", width, value);
            } else {
                fprintf(f, "%u", value);
            }
            break;
        case strt_ind:
            if(width) {
                fprintf(f, "%*s", width, g_stratName[value]);
            } else {
                fprintf(f, "%s", g_stratName[value]);
            }
            break;
        case fadt_ind:   /* force attach dict */
            if(width) {
                fprintf(f, "%*d", width, (int)value);
            } else {
                fprintf(f, "%d", (int)value);
            }
            break;
        case NUM_PARAMS:
        default:
            DISPLAY("Error, not a valid param\n ");
            assert(0);
            break;
    }
}


/*-************************************
*  Benchmark Parameters/Global Variables
**************************************/

/* General Utility */
static U32 g_timeLimit_s = 99999;   /* about 27 hours */
static UTIL_time_t g_time; /* to be used to compare solution finding speeds to compare to original */
static U32 g_blockSize = 0;
static U32 g_rand = 1;

/* Display */
static int g_displayLevel = 3;
static BYTE g_silenceParams[NUM_PARAMS];   /* can selectively silence some params when displaying them */

/* Mode Selection */
static U32 g_singleRun = 0;
static U32 g_optimizer = 0;
static int g_optmode = 0;

/* For cLevel Table generation */
static U32 g_target = 0;
static U32 g_noSeed = 0;

/* For optimizer */
static paramValues_t g_params; /* Initialized at the beginning of main w/ emptyParams() function */
static double g_ratioMultiplier = 5.;
static U32 g_strictness = PARAM_UNSET; /* range 1 - 100, measure of how strict  */
static BMK_benchResult_t g_lvltarget;

typedef enum {
    directMap,
    xxhashMap,
    noMemo
} memoTableType_t;

typedef struct {
    memoTableType_t tableType;
    BYTE* table;
    size_t tableLen;
    varInds_t varArray[NUM_PARAMS];
    size_t varLen;
} memoTable_t;

typedef struct {
    BMK_benchResult_t result;
    paramValues_t params;
} winnerInfo_t;

typedef struct {
    U32 cSpeed;  /* bytes / sec */
    U32 dSpeed;
    U32 cMem;    /* bytes */
} constraint_t;

typedef struct winner_ll_node winner_ll_node;
struct winner_ll_node {
    winnerInfo_t res;
    winner_ll_node* next;
};

static winner_ll_node* g_winners; /* linked list sorted ascending by cSize & cSpeed */

/*
 * Additional Global Variables (Defined Above Use)
 * g_level_constraint
 * g_alreadyTested
 * g_maxTries
 * g_clockGranularity
 */


/*-*******************************************************
*  General Util Functions
*********************************************************/

/* nullified useless params, to ensure count stats */
/* cleans up params for memoizing / display */
static paramValues_t sanitizeParams(paramValues_t params)
{
    if (params.vals[strt_ind] == ZSTD_fast)
        params.vals[clog_ind] = 0, params.vals[slog_ind] = 0;
    if (params.vals[strt_ind] == ZSTD_dfast)
        params.vals[slog_ind] = 0;
    if ( (params.vals[strt_ind] < ZSTD_btopt) && (params.vals[strt_ind] != ZSTD_fast) )
        params.vals[tlen_ind] = 0;

    return params;
}

static ZSTD_compressionParameters pvalsToCParams(paramValues_t p)
{
    ZSTD_compressionParameters c;
    memset(&c, 0, sizeof(ZSTD_compressionParameters));
    c.windowLog = p.vals[wlog_ind];
    c.chainLog = p.vals[clog_ind];
    c.hashLog = p.vals[hlog_ind];
    c.searchLog = p.vals[slog_ind];
    c.minMatch = p.vals[mml_ind];
    c.targetLength = p.vals[tlen_ind];
    c.strategy = p.vals[strt_ind];
    /* no forceAttachDict */
    return c;
}

static paramValues_t cParamsToPVals(ZSTD_compressionParameters c)
{
    paramValues_t p;
    varInds_t i;
    p.vals[wlog_ind] = c.windowLog;
    p.vals[clog_ind] = c.chainLog;
    p.vals[hlog_ind] = c.hashLog;
    p.vals[slog_ind] = c.searchLog;
    p.vals[mml_ind]  = c.minMatch;
    p.vals[tlen_ind] = c.targetLength;
    p.vals[strt_ind] = c.strategy;

    /* set all other params to their minimum value */
    for (i = strt_ind + 1; i < NUM_PARAMS; i++) {
        p.vals[i] = mintable[i];
    }
    return p;
}

/* equivalent of ZSTD_adjustCParams for paramValues_t */
static paramValues_t
adjustParams(paramValues_t p, const size_t maxBlockSize, const size_t dictSize)
{
    paramValues_t ot = p;
    varInds_t i;
    p = cParamsToPVals(ZSTD_adjustCParams(pvalsToCParams(p), maxBlockSize, dictSize));
    if (!dictSize) { p.vals[fadt_ind] = 0; }
    /* retain value of all other parameters */
    for(i = strt_ind + 1; i < NUM_PARAMS; i++) {
        p.vals[i] = ot.vals[i];
    }
    return p;
}

static size_t BMK_findMaxMem(U64 requiredMem)
{
    size_t const step = 64 MB;
    void* testmem = NULL;

    requiredMem = (((requiredMem >> 26) + 1) << 26);
    if (requiredMem > maxMemory) requiredMem = maxMemory;

    requiredMem += 2 * step;
    while (!testmem && requiredMem > 0) {
        testmem = malloc ((size_t)requiredMem);
        requiredMem -= step;
    }

    free (testmem);
    return (size_t) requiredMem;
}

/* accuracy in seconds only, span can be multiple years */
static U32 BMK_timeSpan_s(const UTIL_time_t tStart)
{
    return (U32)(UTIL_clockSpanMicro(tStart) / 1000000ULL);
}

static U32 FUZ_rotl32(U32 x, U32 r)
{
    return ((x << r) | (x >> (32 - r)));
}

static U32 FUZ_rand(U32* src)
{
    const U32 prime1 = 2654435761U;
    const U32 prime2 = 2246822519U;
    U32 rand32 = *src;
    rand32 *= prime1;
    rand32 += prime2;
    rand32  = FUZ_rotl32(rand32, 13);
    *src = rand32;
    return rand32 >> 5;
}

#define BOUNDCHECK(val,min,max) {                     \
    if (((val)<(min)) | ((val)>(max))) {              \
        DISPLAY("INVALID PARAMETER CONSTRAINTS\n");   \
        return 0;                                     \
}   }

static int paramValid(const paramValues_t paramTarget)
{
    U32 i;
    for(i = 0; i < NUM_PARAMS; i++) {
        BOUNDCHECK(paramTarget.vals[i], mintable[i], maxtable[i]);
    }
    return 1;
}

/* cParamUnsetMin() :
 * if any parameter in paramTarget is not yet set,
 * it will receive its corresponding minimal value.
 * This function never fails */
static paramValues_t cParamUnsetMin(paramValues_t paramTarget)
{
    varInds_t vi;
    for (vi = 0; vi < NUM_PARAMS; vi++) {
        if (paramTarget.vals[vi] == PARAM_UNSET) {
            paramTarget.vals[vi] = mintable[vi];
        }
    }
    return paramTarget;
}

static paramValues_t emptyParams(void)
{
    U32 i;
    paramValues_t p;
    for(i = 0; i < NUM_PARAMS; i++) {
        p.vals[i] = PARAM_UNSET;
    }
    return p;
}

static winnerInfo_t initWinnerInfo(const paramValues_t p)
{
    winnerInfo_t w1;
    w1.result.cSpeed = 0.;
    w1.result.dSpeed = 0.;
    w1.result.cMem = (size_t)-1;
    w1.result.cSize = (size_t)-1;
    w1.params = p;
    return w1;
}

static paramValues_t
overwriteParams(paramValues_t base, const paramValues_t mask)
{
    U32 i;
    for(i = 0; i < NUM_PARAMS; i++) {
        if(mask.vals[i] != PARAM_UNSET) {
            base.vals[i] = mask.vals[i];
        }
    }
    return base;
}

static void
paramVaryOnce(const varInds_t paramIndex, const int amt, paramValues_t* ptr)
{
    ptr->vals[paramIndex] = rangeMap(paramIndex,
                                     invRangeMap(paramIndex, ptr->vals[paramIndex]) + amt);
}

/* varies ptr by nbChanges respecting varyParams*/
static void
paramVariation(paramValues_t* ptr, memoTable_t* mtAll, const U32 nbChanges)
{
    paramValues_t p;
    U32 validated = 0;
    while (!validated) {
        U32 i;
        p = *ptr;
        for (i = 0 ; i < nbChanges ; i++) {
            const U32 changeID = (U32)FUZ_rand(&g_rand) % (mtAll[p.vals[strt_ind]].varLen << 1);
            paramVaryOnce(mtAll[p.vals[strt_ind]].varArray[changeID >> 1], ((changeID & 1) << 1) - 1, &p);
        }
        validated = paramValid(p);
    }
    *ptr = p;
}

/* Completely random parameter selection */
static paramValues_t randomParams(void)
{
    varInds_t v; paramValues_t p;
    for(v = 0; v < NUM_PARAMS; v++) {
        p.vals[v] = rangeMap(v, FUZ_rand(&g_rand) % rangetable[v]);
    }
    return p;
}

static U64 g_clockGranularity = 100000000ULL;

static void init_clockGranularity(void)
{
    UTIL_time_t const clockStart = UTIL_getTime();
    U64 el1 = 0, el2 = 0;
    int i = 0;
    do {
        el1 = el2;
        el2 = UTIL_clockSpanNano(clockStart);
        if(el1 < el2) {
            U64 iv = el2 - el1;
            if(g_clockGranularity > iv) {
                g_clockGranularity = iv;
                i = 0;
            } else {
                i++;
            }
        }
    } while(i < 10);
    DEBUGOUTPUT("Granularity: %llu\n", (unsigned long long)g_clockGranularity);
}

/*-************************************
*  Optimizer Util Functions
**************************************/

/* checks results are feasible */
static int feasible(const BMK_benchResult_t results, const constraint_t target) {
    return (results.cSpeed >= target.cSpeed)
        && (results.dSpeed >= target.dSpeed)
        && (results.cMem <= target.cMem)
        && (!g_optmode || results.cSize <= g_lvltarget.cSize);
}

/* hill climbing value for part 1 */
/* Scoring here is a linear reward for all set constraints normalized between 0 to 1
 * (with 0 at 0 and 1 being fully fulfilling the constraint), summed with a logarithmic
 * bonus to exceeding the constraint value. We also give linear ratio for compression ratio.
 * The constant factors are experimental.
 */
static double
resultScore(const BMK_benchResult_t res, const size_t srcSize, const constraint_t target)
{
    double cs = 0., ds = 0., rt, cm = 0.;
    const double r1 = 1, r2 = 0.1, rtr = 0.5;
    double ret;
    if(target.cSpeed) { cs = res.cSpeed / (double)target.cSpeed; }
    if(target.dSpeed) { ds = res.dSpeed / (double)target.dSpeed; }
    if(target.cMem != (U32)-1) { cm = (double)target.cMem / res.cMem; }
    rt = ((double)srcSize / res.cSize);

    ret = (MIN(1, cs) + MIN(1, ds)  + MIN(1, cm))*r1 + rt * rtr +
         (MAX(0, log(cs))+ MAX(0, log(ds))+ MAX(0, log(cm))) * r2;

    return ret;
}

/* calculates normalized squared euclidean distance of result1 if it is in the first quadrant relative to lvlRes */
static double
resultDistLvl(const BMK_benchResult_t result1, const BMK_benchResult_t lvlRes)
{
    double normalizedCSpeedGain1 = (result1.cSpeed / lvlRes.cSpeed) - 1;
    double normalizedRatioGain1 = ((double)lvlRes.cSize / result1.cSize) - 1;
    if(normalizedRatioGain1 < 0 || normalizedCSpeedGain1 < 0) {
        return 0.0;
    }
    return normalizedRatioGain1 * g_ratioMultiplier + normalizedCSpeedGain1;
}

/* return true if r2 strictly better than r1 */
static int
compareResultLT(const BMK_benchResult_t result1, const BMK_benchResult_t result2, const constraint_t target, size_t srcSize)
{
    if(feasible(result1, target) && feasible(result2, target)) {
        if(g_optmode) {
            return resultDistLvl(result1, g_lvltarget) < resultDistLvl(result2, g_lvltarget);
        } else {
            return (result1.cSize > result2.cSize)
                || (result1.cSize == result2.cSize && result2.cSpeed > result1.cSpeed)
                || (result1.cSize == result2.cSize && result2.cSpeed == result1.cSpeed && result2.dSpeed > result1.dSpeed);
        }
    }
    return feasible(result2, target)
        || (!feasible(result1, target)
            && (resultScore(result1, srcSize, target) < resultScore(result2, srcSize, target)));
}

static constraint_t relaxTarget(constraint_t target) {
    target.cMem = (U32)-1;
    target.cSpeed *= ((double)g_strictness) / 100;
    target.dSpeed *= ((double)g_strictness) / 100;
    return target;
}

static void optimizerAdjustInput(paramValues_t* pc, const size_t maxBlockSize)
{
    varInds_t v;
    for(v = 0; v < NUM_PARAMS; v++) {
        if(pc->vals[v] != PARAM_UNSET) {
            U32 newval = MIN(MAX(pc->vals[v], mintable[v]), maxtable[v]);
            if(newval != pc->vals[v]) {
                pc->vals[v] = newval;
                DISPLAY("Warning: parameter %s not in valid range, adjusting to ",
                        g_paramNames[v]);
                displayParamVal(stderr, v, newval, 0); DISPLAY("\n");
            }
        }
    }

    if(pc->vals[wlog_ind] != PARAM_UNSET) {

        U32 sshb = maxBlockSize > 1 ? ZSTD_highbit32((U32)(maxBlockSize-1)) + 1 : 1;
        /* edge case of highBit not working for 0 */

        if(maxBlockSize < (1ULL << 31) && sshb + 1 < pc->vals[wlog_ind]) {
            U32 adjust = MAX(mintable[wlog_ind], sshb);
            if(adjust != pc->vals[wlog_ind]) {
                pc->vals[wlog_ind] = adjust;
                DISPLAY("Warning: windowLog larger than src/block size, adjusted to %u\n",
                        (unsigned)pc->vals[wlog_ind]);
            }
        }
    }

    if(pc->vals[wlog_ind] != PARAM_UNSET && pc->vals[clog_ind] != PARAM_UNSET) {
        U32 maxclog;
        if(pc->vals[strt_ind] == PARAM_UNSET || pc->vals[strt_ind] >= (U32)ZSTD_btlazy2) {
            maxclog = pc->vals[wlog_ind] + 1;
        } else {
            maxclog = pc->vals[wlog_ind];
        }

        if(pc->vals[clog_ind] > maxclog) {
            pc->vals[clog_ind] = maxclog;
            DISPLAY("Warning: chainlog too much larger than windowLog size, adjusted to %u\n",
                    (unsigned)pc->vals[clog_ind]);
        }
    }

    if(pc->vals[wlog_ind] != PARAM_UNSET && pc->vals[hlog_ind] != PARAM_UNSET) {
        if(pc->vals[wlog_ind] + 1 < pc->vals[hlog_ind]) {
            pc->vals[hlog_ind] = pc->vals[wlog_ind] + 1;
            DISPLAY("Warning: hashlog too much larger than windowLog size, adjusted to %u\n",
                    (unsigned)pc->vals[hlog_ind]);
        }
    }

    if(pc->vals[slog_ind] != PARAM_UNSET && pc->vals[clog_ind] != PARAM_UNSET) {
        if(pc->vals[slog_ind] > pc->vals[clog_ind]) {
            pc->vals[clog_ind] = pc->vals[slog_ind];
            DISPLAY("Warning: searchLog larger than chainLog, adjusted to %u\n",
                    (unsigned)pc->vals[slog_ind]);
        }
    }
}

static int
redundantParams(const paramValues_t paramValues, const constraint_t target, const size_t maxBlockSize)
{
    return
       (ZSTD_estimateCStreamSize_usingCParams(pvalsToCParams(paramValues)) > (size_t)target.cMem) /* Uses too much memory */
    || ((1ULL << (paramValues.vals[wlog_ind] - 1)) >= maxBlockSize && paramValues.vals[wlog_ind] != mintable[wlog_ind]) /* wlog too much bigger than src size */
    || (paramValues.vals[clog_ind] > (paramValues.vals[wlog_ind] + (paramValues.vals[strt_ind] > ZSTD_btlazy2))) /* chainLog larger than windowLog*/
    || (paramValues.vals[slog_ind] > paramValues.vals[clog_ind]) /* searchLog larger than chainLog */
    || (paramValues.vals[hlog_ind] > paramValues.vals[wlog_ind] + 1); /* hashLog larger than windowLog + 1 */
}


/*-************************************
*  Display Functions
**************************************/

/* BMK_paramValues_into_commandLine() :
 * transform a set of parameters paramValues_t
 * into a command line compatible with `zstd` syntax
 * and writes it into FILE* f.
 * f must be already opened and writable */
static void
BMK_paramValues_into_commandLine(FILE* f, const paramValues_t params)
{
    varInds_t v;
    int first = 1;
    fprintf(f,"--zstd=");
    for (v = 0; v < NUM_PARAMS; v++) {
        if (g_silenceParams[v]) { continue; }
        if (!first) { fprintf(f, ","); }
        fprintf(f,"%s=", g_paramNames[v]);

        if (v == strt_ind) { fprintf(f,"%u", (unsigned)params.vals[v]); }
        else { displayParamVal(f, v, params.vals[v], 0); }
        first = 0;
    }
    fprintf(f, "\n");
}


/* comparison function: */
/* strictly better, strictly worse, equal, speed-side adv, size-side adv */
#define WORSE_RESULT 0
#define BETTER_RESULT 1
#define ERROR_RESULT 2

#define SPEED_RESULT 4
#define SIZE_RESULT 5
/* maybe have epsilon-eq to limit table size? */
static int
speedSizeCompare(const BMK_benchResult_t r1, const BMK_benchResult_t r2)
{
    if(r1.cSpeed < r2.cSpeed) {
        if(r1.cSize >= r2.cSize) {
            return BETTER_RESULT;
        }
        return SPEED_RESULT; /* r2 is smaller but not faster. */
    } else {
        if(r1.cSize <= r2.cSize) {
            return WORSE_RESULT;
        }
        return SIZE_RESULT; /* r2 is faster but not smaller */
    }
}

/* 0 for insertion, 1 for no insert */
/* maintain invariant speedSizeCompare(n, n->next) = SPEED_RESULT */
static int
insertWinner(const winnerInfo_t w, const constraint_t targetConstraints)
{
    BMK_benchResult_t r = w.result;
    winner_ll_node* cur_node = g_winners;
    /* first node to insert */
    if(!feasible(r, targetConstraints)) {
        return 1;
    }

    if(g_winners == NULL) {
        winner_ll_node* first_node = malloc(sizeof(winner_ll_node));
        if(first_node == NULL) {
            return 1;
        }
        first_node->next = NULL;
        first_node->res = w;
        g_winners = first_node;
        return 0;
    }

    while(cur_node->next != NULL) {
        switch(speedSizeCompare(cur_node->res.result, r)) {
            case WORSE_RESULT:
            {
                return 1; /* never insert if better */
            }
            case BETTER_RESULT:
            {
                winner_ll_node* tmp;
                cur_node->res = cur_node->next->res;
                tmp = cur_node->next;
                cur_node->next = cur_node->next->next;
                free(tmp);
                break;
            }
            case SIZE_RESULT:
            {
                cur_node = cur_node->next;
                break;
            }
            case SPEED_RESULT: /* insert after first size result, then return */
            {
                winner_ll_node* newnode = malloc(sizeof(winner_ll_node));
                if(newnode == NULL) {
                    return 1;
                }
                newnode->res = cur_node->res;
                cur_node->res = w;
                newnode->next = cur_node->next;
                cur_node->next = newnode;
                return 0;
            }
        }

    }

    assert(cur_node->next == NULL);
    switch(speedSizeCompare(cur_node->res.result, r)) {
        case WORSE_RESULT:
        {
            return 1; /* never insert if better */
        }
        case BETTER_RESULT:
        {
            cur_node->res = w;
            return 0;
        }
        case SIZE_RESULT:
        {
            winner_ll_node* newnode = malloc(sizeof(winner_ll_node));
            if(newnode == NULL) {
                return 1;
            }
            newnode->res = w;
            newnode->next = NULL;
            cur_node->next = newnode;
            return 0;
        }
        case SPEED_RESULT: /* insert before first size result, then return */
        {
            winner_ll_node* newnode = malloc(sizeof(winner_ll_node));
            if(newnode == NULL) {
                return 1;
            }
            newnode->res = cur_node->res;
            cur_node->res = w;
            newnode->next = cur_node->next;
            cur_node->next = newnode;
            return 0;
        }
        default:
            return 1;
    }
}

static void
BMK_displayOneResult(FILE* f, winnerInfo_t res, const size_t srcSize)
{
    varInds_t v;
    int first = 1;
    res.params = cParamUnsetMin(res.params);
    fprintf(f, "    {");
    for (v = 0; v < NUM_PARAMS; v++) {
        if (g_silenceParams[v]) { continue; }
        if (!first) { fprintf(f, ","); }
        displayParamVal(f, v, res.params.vals[v], 3);
        first = 0;
    }

    {   double const ratio = res.result.cSize ?
                            (double)srcSize / res.result.cSize : 0;
        double const cSpeedMBps = (double)res.result.cSpeed / MB_UNIT;
        double const dSpeedMBps = (double)res.result.dSpeed / MB_UNIT;

        fprintf(f, " },     /* R:%5.3f at %5.1f MB/s - %5.1f MB/s */\n",
                            ratio, cSpeedMBps, dSpeedMBps);
    }
}

/* Writes to f the results of a parameter benchmark */
/* when used with --optimize, will only print results better than previously discovered */
static void
BMK_printWinner(FILE* f, const int cLevel, const BMK_benchResult_t result, const paramValues_t params, const size_t srcSize)
{
    char lvlstr[15] = "Custom Level";
    winnerInfo_t w;
    w.params = params;
    w.result = result;

    fprintf(f, "\r%79s\r", "");

    if(cLevel != CUSTOM_LEVEL) {
        snprintf(lvlstr, 15, "  Level %2d  ", cLevel);
    }

    if(TIMED) {
        const U64 mn_in_ns = 60ULL * TIMELOOP_NANOSEC;
        const U64 time_ns = UTIL_clockSpanNano(g_time);
        const U64 minutes = time_ns / mn_in_ns;
        fprintf(f, "%1lu:%2lu:%05.2f - ",
                (unsigned long) minutes / 60,
                (unsigned long) minutes % 60,
                (double)(time_ns - (minutes * mn_in_ns)) / TIMELOOP_NANOSEC );
    }

    fprintf(f, "/* %s */   ", lvlstr);
    BMK_displayOneResult(f, w, srcSize);
}

static void
BMK_printWinnerOpt(FILE* f, const U32 cLevel, const BMK_benchResult_t result, const paramValues_t params, const constraint_t targetConstraints, const size_t srcSize)
{
    /* global winner used for constraints */
                                    /* cSize, cSpeed, dSpeed, cMem */
    static winnerInfo_t g_winner = { { (size_t)-1LL, 0, 0, (size_t)-1LL },
                                     { { PARAM_UNSET, PARAM_UNSET, PARAM_UNSET, PARAM_UNSET, PARAM_UNSET, PARAM_UNSET, PARAM_UNSET, PARAM_UNSET } }
                                   };
    if ( DEBUG
      || compareResultLT(g_winner.result, result, targetConstraints, srcSize)
      || g_displayLevel >= 4) {
        if ( DEBUG
          && compareResultLT(g_winner.result, result, targetConstraints, srcSize)) {
            DISPLAY("New Winner: \n");
        }

        if(g_displayLevel >= 2) {
            BMK_printWinner(f, cLevel, result, params, srcSize);
        }

        if(compareResultLT(g_winner.result, result, targetConstraints, srcSize)) {
            if(g_displayLevel >= 1) { BMK_paramValues_into_commandLine(f, params); }
            g_winner.result = result;
            g_winner.params = params;
        }
    }

    if(g_optmode && g_optimizer && (DEBUG || g_displayLevel == 3)) {
        winnerInfo_t w;
        winner_ll_node* n;
        w.result = result;
        w.params = params;
        insertWinner(w, targetConstraints);

        if(!DEBUG) { fprintf(f, "\033c"); }
        fprintf(f, "\n");

        /* the table */
        fprintf(f, "================================\n");
        for(n = g_winners; n != NULL; n = n->next) {
            BMK_displayOneResult(f, n->res, srcSize);
        }
        fprintf(f, "================================\n");
        fprintf(f, "Level Bounds: R: > %.3f AND C: < %.1f MB/s \n\n",
            (double)srcSize / g_lvltarget.cSize, (double)g_lvltarget.cSpeed / MB_UNIT);


        fprintf(f, "Overall Winner: \n");
        BMK_displayOneResult(f, g_winner, srcSize);
        BMK_paramValues_into_commandLine(f, g_winner.params);

        fprintf(f, "Latest BMK: \n");\
        BMK_displayOneResult(f, w, srcSize);
    }
}


/* BMK_print_cLevelEntry() :
 * Writes one cLevelTable entry, for one level.
 * f must exist, be already opened, and be seekable.
 * this function cannot error.
 */
static void
BMK_print_cLevelEntry(FILE* f, const int cLevel,
                      paramValues_t params,
                      const BMK_benchResult_t result, const size_t srcSize)
{
    varInds_t v;
    int first = 1;

    assert(cLevel >= 0);
    assert(cLevel <= NB_LEVELS_TRACKED);
    params = cParamUnsetMin(params);

    fprintf(f, "   {");
    /* print cParams.
     * assumption : all cParams are present and in order in the following range */
    for (v = 0; v <= strt_ind; v++) {
        if (!first) { fprintf(f, ","); }
        displayParamVal(f, v, params.vals[v], 3);
        first = 0;
    }
    /* print comment */
    {   double const ratio = result.cSize ?
                            (double)srcSize / result.cSize : 0;
        double const cSpeedMBps = (double)result.cSpeed / MB_UNIT;
        double const dSpeedMBps = (double)result.dSpeed / MB_UNIT;

        fprintf(f, " },   /* level %2i:  R=%5.3f at %5.1f MB/s - %5.1f MB/s */\n",
                             cLevel, ratio, cSpeedMBps, dSpeedMBps);
    }
}


/* BMK_print_cLevelTable() :
 * print candidate compression table into proposed FILE* f.
 * f must exist, be already opened, and be seekable.
 * winners must be a table of NB_LEVELS_TRACKED+1 elements winnerInfo_t, all entries presumed initialized
 * this function cannot error.
 */
static void
BMK_print_cLevelTable(FILE* f, const winnerInfo_t* winners, const size_t srcSize)
{
    int cLevel;

    fprintf(f, "\n /* Proposed configurations : */ \n");
    fprintf(f, "   /* W,  C,  H,  S,  L,  T, strat */ \n");

    for (cLevel=0; cLevel <= NB_LEVELS_TRACKED; cLevel++)
        BMK_print_cLevelEntry(f,
                              cLevel, winners[cLevel].params,
                              winners[cLevel].result, srcSize);
}


/* BMK_saveAndPrint_cLevelTable() :
 * save candidate compression table into FILE* f,
 * and then to stdout.
 * f must exist, be already opened, and be seekable.
 * winners must be a table of NB_LEVELS_TRACKED+1 elements winnerInfo_t, all entries presumed initialized
 * this function cannot error.
 */
static void
BMK_saveAndPrint_cLevelTable(FILE* const f,
                       const winnerInfo_t* winners,
                       const size_t srcSize)
{
    fseek(f, 0, SEEK_SET);
    BMK_print_cLevelTable(f, winners, srcSize);
    fflush(f);
    BMK_print_cLevelTable(stdout, winners, srcSize);
}


/*-*******************************************************
*  Functions to Benchmark
*********************************************************/

typedef struct {
    ZSTD_CCtx* cctx;
    const void* dictBuffer;
    size_t dictBufferSize;
    int cLevel;
    const paramValues_t* comprParams;
} BMK_initCCtxArgs;

static size_t local_initCCtx(void* payload) {
    const BMK_initCCtxArgs* ag = (const BMK_initCCtxArgs*)payload;
    varInds_t i;
    ZSTD_CCtx_reset(ag->cctx, ZSTD_reset_session_and_parameters);
    ZSTD_CCtx_setParameter(ag->cctx, ZSTD_c_compressionLevel, ag->cLevel);

    for(i = 0; i < NUM_PARAMS; i++) {
        if(ag->comprParams->vals[i] != PARAM_UNSET)
        ZSTD_CCtx_setParameter(ag->cctx, cctxSetParamTable[i], ag->comprParams->vals[i]);
    }
    ZSTD_CCtx_loadDictionary(ag->cctx, ag->dictBuffer, ag->dictBufferSize);

    return 0;
}

typedef struct {
    ZSTD_DCtx* dctx;
    const void* dictBuffer;
    size_t dictBufferSize;
} BMK_initDCtxArgs;

static size_t local_initDCtx(void* payload) {
    const BMK_initDCtxArgs* ag = (const BMK_initDCtxArgs*)payload;
    ZSTD_DCtx_reset(ag->dctx, ZSTD_reset_session_and_parameters);
    ZSTD_DCtx_loadDictionary(ag->dctx, ag->dictBuffer, ag->dictBufferSize);
    return 0;
}

/* additional argument is just the context */
static size_t local_defaultCompress(
                            const void* srcBuffer, size_t srcSize,
                            void* dstBuffer, size_t dstSize,
                            void* addArgs)
{
    ZSTD_CCtx* cctx = (ZSTD_CCtx*)addArgs;
    assert(dstSize == ZSTD_compressBound(srcSize)); /* specific to this version, which is only used in paramgrill */
    return ZSTD_compress2(cctx, dstBuffer, dstSize, srcBuffer, srcSize);
}

/* additional argument is just the context */
static size_t local_defaultDecompress(
    const void* srcBuffer, size_t srcSize,
    void* dstBuffer, size_t dstSize,
    void* addArgs) {
    size_t moreToFlush = 1;
    ZSTD_DCtx* dctx = (ZSTD_DCtx*)addArgs;
    ZSTD_inBuffer in;
    ZSTD_outBuffer out;
    in.src = srcBuffer;
    in.size = srcSize;
    in.pos = 0;
    out.dst = dstBuffer;
    out.size = dstSize;
    out.pos = 0;
    while (moreToFlush) {
        if(out.pos == out.size) {
            return (size_t)-ZSTD_error_dstSize_tooSmall;
        }
        moreToFlush = ZSTD_decompressStream(dctx,
                            &out, &in);
        if (ZSTD_isError(moreToFlush)) {
            return moreToFlush;
        }
    }
    return out.pos;

}

/*-************************************
*  Data Initialization Functions
**************************************/

typedef struct {
    void* srcBuffer;
    size_t srcSize;
    const void** srcPtrs;
    size_t* srcSizes;
    void** dstPtrs;
    size_t* dstCapacities;
    size_t* dstSizes;
    void** resPtrs;
    size_t* resSizes;
    size_t nbBlocks;
    size_t maxBlockSize;
} buffers_t;

typedef struct {
    size_t dictSize;
    void* dictBuffer;
    ZSTD_CCtx* cctx;
    ZSTD_DCtx* dctx;
} contexts_t;

static void freeNonSrcBuffers(const buffers_t b) {
    free(b.srcPtrs);
    free(b.srcSizes);

    if(b.dstPtrs != NULL) {
        free(b.dstPtrs[0]);
    }
    free(b.dstPtrs);
    free(b.dstCapacities);
    free(b.dstSizes);

    if(b.resPtrs != NULL) {
        free(b.resPtrs[0]);
    }
    free(b.resPtrs);
    free(b.resSizes);
}

static void freeBuffers(const buffers_t b) {
    if(b.srcPtrs != NULL) {
        free(b.srcBuffer);
    }
    freeNonSrcBuffers(b);
}

/* srcBuffer will be freed by freeBuffers now */
static int createBuffersFromMemory(buffers_t* buff, void * srcBuffer, const size_t nbFiles,
    const size_t* fileSizes)
{
    size_t pos = 0, n, blockSize;
    U32 maxNbBlocks, blockNb = 0;
    buff->srcSize = 0;
    for(n = 0; n < nbFiles; n++) {
        buff->srcSize += fileSizes[n];
    }

    if(buff->srcSize == 0) {
        DISPLAY("No data to bench\n");
        return 1;
    }

    blockSize = g_blockSize ? g_blockSize : buff->srcSize;
    maxNbBlocks = (U32) ((buff->srcSize + (blockSize-1)) / blockSize) + (U32)nbFiles;

    buff->srcPtrs = (const void**)calloc(maxNbBlocks, sizeof(void*));
    buff->srcSizes = (size_t*)malloc(maxNbBlocks * sizeof(size_t));

    buff->dstPtrs = (void**)calloc(maxNbBlocks, sizeof(void*));
    buff->dstCapacities = (size_t*)malloc(maxNbBlocks * sizeof(size_t));
    buff->dstSizes = (size_t*)malloc(maxNbBlocks * sizeof(size_t));

    buff->resPtrs = (void**)calloc(maxNbBlocks, sizeof(void*));
    buff->resSizes = (size_t*)malloc(maxNbBlocks * sizeof(size_t));

    if(!buff->srcPtrs || !buff->srcSizes || !buff->dstPtrs || !buff->dstCapacities || !buff->dstSizes || !buff->resPtrs || !buff->resSizes) {
        DISPLAY("alloc error\n");
        freeNonSrcBuffers(*buff);
        return 1;
    }

    buff->srcBuffer = srcBuffer;
    buff->srcPtrs[0] = (const void*)buff->srcBuffer;
    buff->dstPtrs[0] = malloc(ZSTD_compressBound(buff->srcSize) + (maxNbBlocks * 1024));
    buff->resPtrs[0] = malloc(buff->srcSize);

    if(!buff->dstPtrs[0] || !buff->resPtrs[0]) {
        DISPLAY("alloc error\n");
        freeNonSrcBuffers(*buff);
        return 1;
    }

    for(n = 0; n < nbFiles; n++) {
        size_t pos_end = pos + fileSizes[n];
        for(; pos < pos_end; blockNb++) {
            buff->srcPtrs[blockNb] = (const void*)((char*)srcBuffer + pos);
            buff->srcSizes[blockNb] = blockSize;
            pos += blockSize;
        }

        if(fileSizes[n] > 0) { buff->srcSizes[blockNb - 1] = ((fileSizes[n] - 1) % blockSize) + 1; }
        pos = pos_end;
    }

    buff->dstCapacities[0] = ZSTD_compressBound(buff->srcSizes[0]);
    buff->dstSizes[0] = buff->dstCapacities[0];
    buff->resSizes[0] = buff->srcSizes[0];
    buff->maxBlockSize = buff->srcSizes[0];

    for(n = 1; n < blockNb; n++) {
        buff->dstPtrs[n] = ((char*)buff->dstPtrs[n-1]) + buff->dstCapacities[n-1];
        buff->resPtrs[n] = ((char*)buff->resPtrs[n-1]) + buff->resSizes[n-1];
        buff->dstCapacities[n] = ZSTD_compressBound(buff->srcSizes[n]);
        buff->dstSizes[n] = buff->dstCapacities[n];
        buff->resSizes[n] = buff->srcSizes[n];

        buff->maxBlockSize = MAX(buff->maxBlockSize, buff->srcSizes[n]);
    }

    buff->nbBlocks = blockNb;

    return 0;
}

/* allocates buffer's arguments. returns success / failuere */
static int createBuffers(buffers_t* buff, const char* const * const fileNamesTable,
                          size_t nbFiles) {
    size_t pos = 0;
    size_t n;
    size_t totalSizeToLoad = UTIL_getTotalFileSize(fileNamesTable, (U32)nbFiles);
    size_t benchedSize = MIN(BMK_findMaxMem(totalSizeToLoad * 3) / 3, totalSizeToLoad);
    size_t* fileSizes = calloc(sizeof(size_t), nbFiles);
    void* srcBuffer = NULL;
    int ret = 0;

    if(!totalSizeToLoad || !benchedSize) {
        ret = 1;
        DISPLAY("Nothing to Bench\n");
        goto _cleanUp;
    }

    srcBuffer = malloc(benchedSize);

    if(!fileSizes || !srcBuffer) {
        ret = 1;
        goto _cleanUp;
    }

    for(n = 0; n < nbFiles; n++) {
        FILE* f;
        U64 fileSize = UTIL_getFileSize(fileNamesTable[n]);
        if (UTIL_isDirectory(fileNamesTable[n])) {
            DISPLAY("Ignoring %s directory...       \n", fileNamesTable[n]);
            continue;
        }
        if (fileSize == UTIL_FILESIZE_UNKNOWN) {
            DISPLAY("Cannot evaluate size of %s, ignoring ... \n", fileNamesTable[n]);
            continue;
        }
        f = fopen(fileNamesTable[n], "rb");
        if (f==NULL) {
            DISPLAY("impossible to open file %s\n", fileNamesTable[n]);
            fclose(f);
            ret = 10;
            goto _cleanUp;
        }

        DISPLAYLEVEL(2, "Loading %s...       \r", fileNamesTable[n]);

        if (fileSize + pos > benchedSize) fileSize = benchedSize - pos, nbFiles=n;   /* buffer too small - stop after this file */
        {
            char* buffer = (char*)(srcBuffer);
            size_t const readSize = fread((buffer)+pos, 1, (size_t)fileSize, f);
            fclose(f);
            if (readSize != (size_t)fileSize) {
                DISPLAY("could not read %s", fileNamesTable[n]);
                ret = 1;
                goto _cleanUp;
            }

            fileSizes[n] = readSize;
            pos += readSize;
        }
    }

    ret = createBuffersFromMemory(buff, srcBuffer, nbFiles, fileSizes);

_cleanUp:
    if(ret) { free(srcBuffer); }
    free(fileSizes);
    return ret;
}

static void freeContexts(const contexts_t ctx) {
    free(ctx.dictBuffer);
    ZSTD_freeCCtx(ctx.cctx);
    ZSTD_freeDCtx(ctx.dctx);
}

static int createContexts(contexts_t* ctx, const char* dictFileName) {
    FILE* f;
    size_t readSize;
    ctx->cctx = ZSTD_createCCtx();
    ctx->dctx = ZSTD_createDCtx();
    assert(ctx->cctx != NULL);
    assert(ctx->dctx != NULL);

    if(dictFileName == NULL) {
        ctx->dictSize = 0;
        ctx->dictBuffer = NULL;
        return 0;
    }
    {   U64 const dictFileSize = UTIL_getFileSize(dictFileName);
        assert(dictFileSize != UTIL_FILESIZE_UNKNOWN);
        ctx->dictSize = dictFileSize;
        assert((U64)ctx->dictSize == dictFileSize); /* check overflow */
    }
    ctx->dictBuffer = malloc(ctx->dictSize);

    f = fopen(dictFileName, "rb");

    if (f==NULL) {
        DISPLAY("unable to open file\n");
        freeContexts(*ctx);
        return 1;
    }

    if (ctx->dictSize > 64 MB || !(ctx->dictBuffer)) {
        DISPLAY("dictionary too large\n");
        fclose(f);
        freeContexts(*ctx);
        return 1;
    }
    readSize = fread(ctx->dictBuffer, 1, ctx->dictSize, f);
    fclose(f);
    if (readSize != ctx->dictSize) {
        DISPLAY("unable to read file\n");
        freeContexts(*ctx);
        return 1;
    }
    return 0;
}

/*-************************************
*  Optimizer Memoization Functions
**************************************/

/* return: new length */
/* keep old array, will need if iter over strategy. */
/* prunes useless params */
static size_t sanitizeVarArray(varInds_t* varNew, const size_t varLength, const varInds_t* varArray, const ZSTD_strategy strat) {
    size_t i, j = 0;
    for(i = 0; i < varLength; i++) {
        if( !((varArray[i] == clog_ind && strat == ZSTD_fast)
            || (varArray[i] == slog_ind && strat == ZSTD_fast)
            || (varArray[i] == slog_ind && strat == ZSTD_dfast)
            || (varArray[i] == tlen_ind && strat < ZSTD_btopt && strat != ZSTD_fast))) {
            varNew[j] = varArray[i];
            j++;
        }
    }
    return j;
}

/* res should be NUM_PARAMS size */
/* constructs varArray from paramValues_t style parameter */
/* pass in using dict. */
static size_t variableParams(const paramValues_t paramConstraints, varInds_t* res, const int usingDictionary) {
    varInds_t i;
    size_t j = 0;
    for(i = 0; i < NUM_PARAMS; i++) {
        if(paramConstraints.vals[i] == PARAM_UNSET) {
            if(i == fadt_ind && !usingDictionary) continue; /* don't use fadt if no dictionary */
            res[j] = i; j++;
        }
    }
    return j;
}

/* length of memo table given free variables */
static size_t memoTableLen(const varInds_t* varyParams, const size_t varyLen) {
    size_t arrayLen = 1;
    size_t i;
    for(i = 0; i < varyLen; i++) {
        if(varyParams[i] == strt_ind) continue; /* strategy separated by table */
        arrayLen *= rangetable[varyParams[i]];
    }
    return arrayLen;
}

/* returns unique index in memotable of compression parameters */
static unsigned memoTableIndDirect(const paramValues_t* ptr, const varInds_t* varyParams, const size_t varyLen) {
    size_t i;
    unsigned ind = 0;
    for(i = 0; i < varyLen; i++) {
        varInds_t v = varyParams[i];
        if(v == strt_ind) continue; /* exclude strategy from memotable */
        ind *= rangetable[v]; ind += (unsigned)invRangeMap(v, ptr->vals[v]);
    }
    return ind;
}

static size_t memoTableGet(const memoTable_t* memoTableArray, const paramValues_t p) {
    const memoTable_t mt = memoTableArray[p.vals[strt_ind]];
    switch(mt.tableType) {
        case directMap:
            return mt.table[memoTableIndDirect(&p, mt.varArray, mt.varLen)];
        case xxhashMap:
            return mt.table[(XXH64(&p.vals, sizeof(U32) * NUM_PARAMS, 0) >> 3) % mt.tableLen];
        case noMemo:
            return 0;
    }
    return 0; /* should never happen, stop compiler warnings */
}

static void memoTableSet(const memoTable_t* memoTableArray, const paramValues_t p, const BYTE value) {
    const memoTable_t mt = memoTableArray[p.vals[strt_ind]];
    switch(mt.tableType) {
        case directMap:
            mt.table[memoTableIndDirect(&p, mt.varArray, mt.varLen)] = value; break;
        case xxhashMap:
            mt.table[(XXH64(&p.vals, sizeof(U32) * NUM_PARAMS, 0) >> 3) % mt.tableLen] = value; break;
        case noMemo:
            break;
    }
}

/* frees all allocated memotables */
/* secret contract :
 * mtAll is a table of (ZSTD_STRATEGY_MAX+1) memoTable_t */
static void freeMemoTableArray(memoTable_t* const mtAll) {
    int i;
    if(mtAll == NULL) { return; }
    for(i = 1; i <= (int)ZSTD_STRATEGY_MAX; i++) {
        free(mtAll[i].table);
    }
    free(mtAll);
}

/* inits memotables for all (including mallocs), all strategies */
/* takes unsanitized varyParams */
static memoTable_t*
createMemoTableArray(const paramValues_t p,
                     const varInds_t* const varyParams,
                     const size_t varyLen,
                     const U32 memoTableLog)
{
    memoTable_t* const mtAll = (memoTable_t*)calloc(sizeof(memoTable_t),(ZSTD_STRATEGY_MAX + 1));
    ZSTD_strategy i, stratMin = ZSTD_STRATEGY_MIN, stratMax = ZSTD_STRATEGY_MAX;

    if(mtAll == NULL) {
        return NULL;
    }

    for(i = 1; i <= (int)ZSTD_STRATEGY_MAX; i++) {
        mtAll[i].varLen = sanitizeVarArray(mtAll[i].varArray, varyLen, varyParams, i);
    }

    /* no memoization */
    if(memoTableLog == 0) {
        for(i = 1; i <= (int)ZSTD_STRATEGY_MAX; i++) {
            mtAll[i].tableType = noMemo;
            mtAll[i].table = NULL;
            mtAll[i].tableLen = 0;
        }
        return mtAll;
    }


    if(p.vals[strt_ind] != PARAM_UNSET) {
        stratMin = p.vals[strt_ind];
        stratMax = p.vals[strt_ind];
    }


    for(i = stratMin; i <= stratMax; i++) {
        size_t mtl = memoTableLen(mtAll[i].varArray, mtAll[i].varLen);
        mtAll[i].tableType = directMap;

        if(memoTableLog != PARAM_UNSET && mtl > (1ULL << memoTableLog)) { /* use hash table */ /* provide some option to only use hash tables? */
            mtAll[i].tableType = xxhashMap;
            mtl = (1ULL << memoTableLog);
        }

        mtAll[i].table = (BYTE*)calloc(sizeof(BYTE), mtl);
        mtAll[i].tableLen = mtl;

        if(mtAll[i].table == NULL) {
            freeMemoTableArray(mtAll);
            return NULL;
        }
    }

    return mtAll;
}

/* Sets pc to random unmeasured set of parameters */
/* specifiy strategy */
static void randomConstrainedParams(paramValues_t* pc, const memoTable_t* memoTableArray, const ZSTD_strategy st)
{
    size_t j;
    const memoTable_t mt = memoTableArray[st];
    pc->vals[strt_ind] = st;
    for(j = 0; j < mt.tableLen; j++) {
        int i;
        for(i = 0; i < NUM_PARAMS; i++) {
            varInds_t v = mt.varArray[i];
            if(v == strt_ind) continue;
            pc->vals[v] = rangeMap(v, FUZ_rand(&g_rand) % rangetable[v]);
        }

        if(!(memoTableGet(memoTableArray, *pc))) break; /* only pick unpicked params. */
    }
}

/*-************************************
*  Benchmarking Functions
**************************************/

static void display_params_tested(paramValues_t cParams)
{
    varInds_t vi;
    DISPLAYLEVEL(3, "\r testing :");
    for (vi=0; vi < NUM_PARAMS; vi++) {
        DISPLAYLEVEL(3, "%3u,", (unsigned)cParams.vals[vi]);
    }
    DISPLAYLEVEL(3, "\b    \r");
}

/* Replicate functionality of benchMemAdvanced, but with pre-split src / dst buffers */
/* The purpose is so that sufficient information is returned so that a decompression call to benchMemInvertible is possible */
/* BMK_benchMemAdvanced(srcBuffer,srcSize, dstBuffer, dstSize, fileSizes, nbFiles, 0, &cParams, dictBuffer, dictSize, ctx, dctx, 0, "File", &adv); */
/* nbSeconds used in same way as in BMK_advancedParams_t */
/* if in decodeOnly, then srcPtr's will be compressed blocks, and uncompressedBlocks will be written to dstPtrs */
/* dictionary nullable, nothing else though. */
/* note : it would be a lot better if this function was present in benchzstd.c,
 * sharing code with benchMemAdvanced(), since it's technically a part of it */
static BMK_benchOutcome_t
BMK_benchMemInvertible( buffers_t buf, contexts_t ctx,
                        int cLevel, const paramValues_t* comprParams,
                        BMK_mode_t mode, unsigned nbSeconds)
{
    U32 i;
    BMK_benchResult_t bResult;
    const void *const *const srcPtrs = (const void *const *const)buf.srcPtrs;
    size_t const *const srcSizes = buf.srcSizes;
    void** const dstPtrs = buf.dstPtrs;
    size_t const *const dstCapacities = buf.dstCapacities;
    size_t* const dstSizes = buf.dstSizes;
    void** const resPtrs = buf.resPtrs;
    size_t const *const resSizes = buf.resSizes;
    const void* dictBuffer = ctx.dictBuffer;
    const size_t dictBufferSize = ctx.dictSize;
    const size_t nbBlocks = buf.nbBlocks;
    const size_t srcSize = buf.srcSize;
    ZSTD_CCtx* cctx = ctx.cctx;
    ZSTD_DCtx* dctx = ctx.dctx;

    /* init */
    display_params_tested(*comprParams);
    memset(&bResult, 0, sizeof(bResult));

    /* warmimg up memory */
    for (i = 0; i < buf.nbBlocks; i++) {
        if (mode != BMK_decodeOnly) {
            RDG_genBuffer(dstPtrs[i], dstCapacities[i], 0.10, 0.50, 1);
        } else {
            RDG_genBuffer(resPtrs[i], resSizes[i], 0.10, 0.50, 1);
        }
    }

    /* Bench */
    {
        /* init args */
        int compressionCompleted = (mode == BMK_decodeOnly);
        int decompressionCompleted = (mode == BMK_compressOnly);
        BMK_timedFnState_t* timeStateCompress = BMK_createTimedFnState(nbSeconds * 1000, 1000);
        BMK_timedFnState_t* timeStateDecompress = BMK_createTimedFnState(nbSeconds * 1000, 1000);
        BMK_benchParams_t cbp, dbp;
        BMK_initCCtxArgs cctxprep;
        BMK_initDCtxArgs dctxprep;

        cbp.benchFn = local_defaultCompress;
        cbp.benchPayload = cctx;
        cbp.initFn = local_initCCtx;
        cbp.initPayload = &cctxprep;
        cbp.errorFn = ZSTD_isError;
        cbp.blockCount = nbBlocks;
        cbp.srcBuffers = srcPtrs;
        cbp.srcSizes = srcSizes;
        cbp.dstBuffers = dstPtrs;
        cbp.dstCapacities = dstCapacities;
        cbp.blockResults = dstSizes;

        cctxprep.cctx = cctx;
        cctxprep.dictBuffer = dictBuffer;
        cctxprep.dictBufferSize = dictBufferSize;
        cctxprep.cLevel = cLevel;
        cctxprep.comprParams = comprParams;

        dbp.benchFn = local_defaultDecompress;
        dbp.benchPayload = dctx;
        dbp.initFn = local_initDCtx;
        dbp.initPayload = &dctxprep;
        dbp.errorFn = ZSTD_isError;
        dbp.blockCount = nbBlocks;
        dbp.srcBuffers = (const void* const *) dstPtrs;
        dbp.srcSizes = dstCapacities;
        dbp.dstBuffers = resPtrs;
        dbp.dstCapacities = resSizes;
        dbp.blockResults = NULL;

        dctxprep.dctx = dctx;
        dctxprep.dictBuffer = dictBuffer;
        dctxprep.dictBufferSize = dictBufferSize;

        assert(timeStateCompress != NULL);
        assert(timeStateDecompress != NULL);
        while(!compressionCompleted) {
            BMK_runOutcome_t const cOutcome = BMK_benchTimedFn(timeStateCompress, cbp);

            if (!BMK_isSuccessful_runOutcome(cOutcome)) {
                BMK_benchOutcome_t bOut;
                memset(&bOut, 0, sizeof(bOut));
                bOut.tag = 1;   /* should rather be a function or a constant */
                BMK_freeTimedFnState(timeStateCompress);
                BMK_freeTimedFnState(timeStateDecompress);
                return bOut;
            }
            {   BMK_runTime_t const rResult = BMK_extract_runTime(cOutcome);
                bResult.cSpeed = (srcSize * TIMELOOP_NANOSEC) / rResult.nanoSecPerRun;
                bResult.cSize = rResult.sumOfReturn;
            }
            compressionCompleted = BMK_isCompleted_TimedFn(timeStateCompress);
        }

        while (!decompressionCompleted) {
            BMK_runOutcome_t const dOutcome = BMK_benchTimedFn(timeStateDecompress, dbp);

            if (!BMK_isSuccessful_runOutcome(dOutcome)) {
                BMK_benchOutcome_t bOut;
                memset(&bOut, 0, sizeof(bOut));
                bOut.tag = 1;   /* should rather be a function or a constant */
                BMK_freeTimedFnState(timeStateCompress);
                BMK_freeTimedFnState(timeStateDecompress);
                return bOut;
            }
            {   BMK_runTime_t const rResult = BMK_extract_runTime(dOutcome);
                bResult.dSpeed = (srcSize * TIMELOOP_NANOSEC) / rResult.nanoSecPerRun;
            }
            decompressionCompleted = BMK_isCompleted_TimedFn(timeStateDecompress);
        }

        BMK_freeTimedFnState(timeStateCompress);
        BMK_freeTimedFnState(timeStateDecompress);
    }

   /* Bench */
    bResult.cMem = (1 << (comprParams->vals[wlog_ind])) + ZSTD_sizeof_CCtx(cctx);

    {   BMK_benchOutcome_t bOut;
        bOut.tag = 0;
        bOut.internal_never_use_directly = bResult;  /* should be a function */
        return bOut;
    }
}

/* BMK_benchParam() :
 * benchmark a set of `cParams` over sample `buf`,
 * store the result in `resultPtr`.
 * @return : 0 if success, 1 if error */
static int BMK_benchParam ( BMK_benchResult_t* resultPtr,
                            buffers_t buf, contexts_t ctx,
                            paramValues_t cParams)
{
    BMK_benchOutcome_t const outcome = BMK_benchMemInvertible(buf, ctx,
                                                        BASE_CLEVEL, &cParams,
                                                        BMK_both, 3);
    if (!BMK_isSuccessful_benchOutcome(outcome)) return 1;
    *resultPtr = BMK_extract_benchResult(outcome);
    return 0;
}


/* Benchmarking which stops when we are sufficiently sure the solution is infeasible / worse than the winner */
#define VARIANCE 1.2
static int allBench(BMK_benchResult_t* resultPtr,
                const buffers_t buf, const contexts_t ctx,
                const paramValues_t cParams,
                const constraint_t target,
                BMK_benchResult_t* winnerResult, int feas)
{
    BMK_benchResult_t benchres;
    double uncertaintyConstantC = 3., uncertaintyConstantD = 3.;
    double winnerRS;

    BMK_benchOutcome_t const outcome = BMK_benchMemInvertible(buf, ctx, BASE_CLEVEL, &cParams, BMK_both, 2);
    if (!BMK_isSuccessful_benchOutcome(outcome)) {
        DEBUGOUTPUT("Benchmarking failed \n");
        return ERROR_RESULT;
    }
    benchres = BMK_extract_benchResult(outcome);

    winnerRS = resultScore(*winnerResult, buf.srcSize, target);
    DEBUGOUTPUT("WinnerScore: %f \n ", winnerRS);

    *resultPtr = benchres;

    /* anything with worse ratio in feas is definitely worse, discard */
    if(feas && benchres.cSize < winnerResult->cSize && !g_optmode) {
        return WORSE_RESULT;
    }

    /* calculate uncertainty in compression / decompression runs */
    if (benchres.cSpeed) {
        U64 const loopDurationC = (((U64)buf.srcSize * TIMELOOP_NANOSEC) / benchres.cSpeed);
        uncertaintyConstantC = ((loopDurationC + (double)(2 * g_clockGranularity))/loopDurationC);
    }

    if (benchres.dSpeed) {
        U64 const loopDurationD = (((U64)buf.srcSize * TIMELOOP_NANOSEC) / benchres.dSpeed);
        uncertaintyConstantD = ((loopDurationD + (double)(2 * g_clockGranularity))/loopDurationD);
    }

    /* optimistic assumption of benchres */
    {   BMK_benchResult_t resultMax = benchres;
        resultMax.cSpeed *= uncertaintyConstantC * VARIANCE;
        resultMax.dSpeed *= uncertaintyConstantD * VARIANCE;

        /* disregard infeasible results in feas mode */
        /* disregard if resultMax < winner in infeas mode */
        if((feas && !feasible(resultMax, target)) ||
          (!feas && (winnerRS > resultScore(resultMax, buf.srcSize, target)))) {
            return WORSE_RESULT;
        }
    }

    /* compare by resultScore when in infeas */
    /* compare by compareResultLT when in feas */
    if((!feas && (resultScore(benchres, buf.srcSize, target) > resultScore(*winnerResult, buf.srcSize, target))) ||
       (feas && (compareResultLT(*winnerResult, benchres, target, buf.srcSize))) )  {
        return BETTER_RESULT;
    } else {
        return WORSE_RESULT;
    }
}


#define INFEASIBLE_THRESHOLD 200
/* Memoized benchmarking, won't benchmark anything which has already been benchmarked before. */
static int benchMemo(BMK_benchResult_t* resultPtr,
                const buffers_t buf, const contexts_t ctx,
                const paramValues_t cParams,
                const constraint_t target,
                BMK_benchResult_t* winnerResult, memoTable_t* const memoTableArray,
                const int feas) {
    static int bmcount = 0;
    int res;

    if ( memoTableGet(memoTableArray, cParams) >= INFEASIBLE_THRESHOLD
      || redundantParams(cParams, target, buf.maxBlockSize) ) {
        return WORSE_RESULT;
    }

    res = allBench(resultPtr, buf, ctx, cParams, target, winnerResult, feas);

    if(DEBUG && !(bmcount % 250)) {
        DISPLAY("Count: %d\n", bmcount);
        bmcount++;
    }
    BMK_printWinnerOpt(stdout, CUSTOM_LEVEL, *resultPtr, cParams, target, buf.srcSize);

    if(res == BETTER_RESULT || feas) {
        memoTableSet(memoTableArray, cParams, 255); /* what happens if collisions are frequent */
    }
    return res;
}


typedef struct {
    U64 cSpeed_min;
    U64 dSpeed_min;
    U32 windowLog_max;
    ZSTD_strategy strategy_max;
} level_constraints_t;

static level_constraints_t g_level_constraint[NB_LEVELS_TRACKED+1];

static void BMK_init_level_constraints(int bytePerSec_level1)
{
    assert(NB_LEVELS_TRACKED >= ZSTD_maxCLevel());
    memset(g_level_constraint, 0, sizeof(g_level_constraint));
    g_level_constraint[1].cSpeed_min = bytePerSec_level1;
    g_level_constraint[1].dSpeed_min = 0.;
    g_level_constraint[1].windowLog_max = 19;
    g_level_constraint[1].strategy_max = ZSTD_fast;

    /* establish speed objectives (relative to level 1) */
    {   int l;
        for (l=2; l<=NB_LEVELS_TRACKED; l++) {
            g_level_constraint[l].cSpeed_min = (g_level_constraint[l-1].cSpeed_min * 49) / 64;
            g_level_constraint[l].dSpeed_min = 0.;
            g_level_constraint[l].windowLog_max = (l<20) ? 23 : l+5;   /* only --ultra levels >= 20 can use windowlog > 23 */
            g_level_constraint[l].strategy_max = ZSTD_STRATEGY_MAX;
    }   }
}

static int BMK_seed(winnerInfo_t* winners,
                    const paramValues_t params,
                    const buffers_t buf,
                    const contexts_t ctx)
{
    BMK_benchResult_t testResult;
    int better = 0;
    int cLevel;

    BMK_benchParam(&testResult, buf, ctx, params);

    for (cLevel = 1; cLevel <= NB_LEVELS_TRACKED; cLevel++) {

        if (testResult.cSpeed < g_level_constraint[cLevel].cSpeed_min)
            continue;   /* not fast enough for this level */
        if (testResult.dSpeed < g_level_constraint[cLevel].dSpeed_min)
            continue;   /* not fast enough for this level */
        if (params.vals[wlog_ind] > g_level_constraint[cLevel].windowLog_max)
            continue;   /* too much memory for this level */
        if (params.vals[strt_ind] > g_level_constraint[cLevel].strategy_max)
            continue;   /* forbidden strategy for this level */
        if (winners[cLevel].result.cSize==0) {
            /* first solution for this cLevel */
            winners[cLevel].result = testResult;
            winners[cLevel].params = params;
            BMK_print_cLevelEntry(stdout, cLevel, params, testResult, buf.srcSize);
            better = 1;
            continue;
        }

        if ((double)testResult.cSize <= ((double)winners[cLevel].result.cSize * (1. + (0.02 / cLevel))) ) {
            /* Validate solution is "good enough" */
            double W_ratio = (double)buf.srcSize / testResult.cSize;
            double O_ratio = (double)buf.srcSize / winners[cLevel].result.cSize;
            double W_ratioNote = log (W_ratio);
            double O_ratioNote = log (O_ratio);
            size_t W_DMemUsed = (1 << params.vals[wlog_ind]) + (16 KB);
            size_t O_DMemUsed = (1 << winners[cLevel].params.vals[wlog_ind]) + (16 KB);
            double W_DMemUsed_note = W_ratioNote * ( 40 + 9*cLevel) - log((double)W_DMemUsed);
            double O_DMemUsed_note = O_ratioNote * ( 40 + 9*cLevel) - log((double)O_DMemUsed);

            size_t W_CMemUsed = (1 << params.vals[wlog_ind]) + ZSTD_estimateCCtxSize_usingCParams(pvalsToCParams(params));
            size_t O_CMemUsed = (1 << winners[cLevel].params.vals[wlog_ind]) + ZSTD_estimateCCtxSize_usingCParams(pvalsToCParams(winners[cLevel].params));
            double W_CMemUsed_note = W_ratioNote * ( 50 + 13*cLevel) - log((double)W_CMemUsed);
            double O_CMemUsed_note = O_ratioNote * ( 50 + 13*cLevel) - log((double)O_CMemUsed);

            double W_CSpeed_note = W_ratioNote * ( 30 + 10*cLevel) + log(testResult.cSpeed);
            double O_CSpeed_note = O_ratioNote * ( 30 + 10*cLevel) + log(winners[cLevel].result.cSpeed);

            double W_DSpeed_note = W_ratioNote * ( 20 + 2*cLevel) + log(testResult.dSpeed);
            double O_DSpeed_note = O_ratioNote * ( 20 + 2*cLevel) + log(winners[cLevel].result.dSpeed);

            if (W_DMemUsed_note < O_DMemUsed_note) {
                /* uses too much Decompression memory for too little benefit */
                if (W_ratio > O_ratio)
                DISPLAYLEVEL(3, "Decompression Memory : %5.3f @ %4.1f MB  vs  %5.3f @ %4.1f MB   : not enough for level %i\n",
                         W_ratio, (double)(W_DMemUsed) / 1024 / 1024,
                         O_ratio, (double)(O_DMemUsed) / 1024 / 1024,   cLevel);
                continue;
            }
            if (W_CMemUsed_note < O_CMemUsed_note) {
                /* uses too much memory for compression for too little benefit */
                if (W_ratio > O_ratio)
                DISPLAYLEVEL(3, "Compression Memory : %5.3f @ %4.1f MB  vs  %5.3f @ %4.1f MB   : not enough for level %i\n",
                         W_ratio, (double)(W_CMemUsed) / 1024 / 1024,
                         O_ratio, (double)(O_CMemUsed) / 1024 / 1024,
                         cLevel);
                continue;
            }
            if (W_CSpeed_note   < O_CSpeed_note  ) {
                /* too large compression speed difference for the compression benefit */
                if (W_ratio > O_ratio)
                DISPLAYLEVEL(3, "Compression Speed : %5.3f @ %4.1f MB/s  vs  %5.3f @ %4.1f MB/s   : not enough for level %i\n",
                         W_ratio, (double)testResult.cSpeed / MB_UNIT,
                         O_ratio, (double)winners[cLevel].result.cSpeed / MB_UNIT,
                         cLevel);
                continue;
            }
            if (W_DSpeed_note   < O_DSpeed_note  ) {
                /* too large decompression speed difference for the compression benefit */
                if (W_ratio > O_ratio)
                DISPLAYLEVEL(3, "Decompression Speed : %5.3f @ %4.1f MB/s  vs  %5.3f @ %4.1f MB/s   : not enough for level %i\n",
                         W_ratio, (double)testResult.dSpeed / MB_UNIT,
                         O_ratio, (double)winners[cLevel].result.dSpeed / MB_UNIT,
                         cLevel);
                continue;
            }

            if (W_ratio < O_ratio)
                DISPLAYLEVEL(3, "Solution %4.3f selected over %4.3f at level %i, due to better secondary statistics \n",
                                W_ratio, O_ratio, cLevel);

            winners[cLevel].result = testResult;
            winners[cLevel].params = params;
            BMK_print_cLevelEntry(stdout, cLevel, params, testResult, buf.srcSize);

            better = 1;
    }   }

    return better;
}

/*-************************************
*  Compression Level Table Generation Functions
**************************************/

#define PARAMTABLELOG   25
#define PARAMTABLESIZE (1<<PARAMTABLELOG)
#define PARAMTABLEMASK (PARAMTABLESIZE-1)
static BYTE g_alreadyTested[PARAMTABLESIZE] = {0};   /* init to zero */

static BYTE* NB_TESTS_PLAYED(paramValues_t p)
{
    ZSTD_compressionParameters const cParams = pvalsToCParams(sanitizeParams(p));
    unsigned long long const h64 = XXH64(&cParams, sizeof(cParams), 0);
    return &g_alreadyTested[(h64 >> 3) & PARAMTABLEMASK];
}

static void playAround(FILE* f,
                       winnerInfo_t* winners,
                       paramValues_t p,
                       const buffers_t buf, const contexts_t ctx)
{
    int nbVariations = 0;
    UTIL_time_t const clockStart = UTIL_getTime();

    while (UTIL_clockSpanMicro(clockStart) < g_maxVariationTime) {
        if (nbVariations++ > g_maxNbVariations) break;

        do {
            int i;
            for(i = 0; i < 4; i++) {
                paramVaryOnce(FUZ_rand(&g_rand) % (strt_ind + 1),
                              ((FUZ_rand(&g_rand) & 1) << 1) - 1,
                              &p);
            }
        } while (!paramValid(p));

        /* exclude faster if already played params */
        if (FUZ_rand(&g_rand) & ((1 << *NB_TESTS_PLAYED(p))-1))
            continue;

        /* test */
        {   BYTE* const b = NB_TESTS_PLAYED(p);
            (*b)++;
        }
        if (!BMK_seed(winners, p, buf, ctx)) continue;

        /* improvement found => search more */
        BMK_saveAndPrint_cLevelTable(f, winners, buf.srcSize);
        playAround(f, winners, p, buf, ctx);
    }

}

static void
BMK_selectRandomStart( FILE* f,
                       winnerInfo_t* winners,
                       const buffers_t buf, const contexts_t ctx)
{
    U32 const id = FUZ_rand(&g_rand) % (NB_LEVELS_TRACKED+1);
    if ((id==0) || (winners[id].params.vals[wlog_ind]==0)) {
        /* use some random entry */
        paramValues_t const p = adjustParams(cParamsToPVals(pvalsToCParams(randomParams())), /* defaults nonCompression parameters */
                                             buf.srcSize, 0);
        playAround(f, winners, p, buf, ctx);
    } else {
        playAround(f, winners, winners[id].params, buf, ctx);
    }
}


/* BMK_generate_cLevelTable() :
 * test a large number of configurations
 * and distribute them accross compression levels according to speed conditions.
 * display and save all intermediate results into rfName = "grillResults.txt".
 * the function automatically stops after g_timeLimit_s.
 * this function cannot error, it directly exit() in case of problem.
 */
static void BMK_generate_cLevelTable(const buffers_t buf, const contexts_t ctx)
{
    paramValues_t params;
    winnerInfo_t winners[NB_LEVELS_TRACKED+1];
    const char* const rfName = "grillResults.txt";
    FILE* const f = fopen(rfName, "w");

    /* init */
    assert(g_singleRun==0);
    memset(winners, 0, sizeof(winners));
    if (f==NULL) { DISPLAY("error opening %s \n", rfName); exit(1); }

    if (g_target) {
        BMK_init_level_constraints(g_target * MB_UNIT);
    } else {
        /* baseline config for level 1 */
        paramValues_t const l1params = cParamsToPVals(ZSTD_getCParams(1, buf.maxBlockSize, ctx.dictSize));
        BMK_benchResult_t testResult;
        BMK_benchParam(&testResult, buf, ctx, l1params);
        BMK_init_level_constraints((int)((testResult.cSpeed * 31) / 32));
    }

    /* populate initial solution */
    {   const int maxSeeds = g_noSeed ? 1 : ZSTD_maxCLevel();
        int i;
        for (i=0; i<=maxSeeds; i++) {
            params = cParamsToPVals(ZSTD_getCParams(i, buf.maxBlockSize, 0));
            BMK_seed(winners, params, buf, ctx);
    }   }
    BMK_saveAndPrint_cLevelTable(f, winners, buf.srcSize);

    /* start tests */
    {   const UTIL_time_t grillStart = UTIL_getTime();
        do {
            BMK_selectRandomStart(f, winners, buf, ctx);
        } while (BMK_timeSpan_s(grillStart) < g_timeLimit_s);
    }

    /* end summary */
    BMK_saveAndPrint_cLevelTable(f, winners, buf.srcSize);
    DISPLAY("grillParams operations completed \n");

    /* clean up*/
    fclose(f);
}


/*-************************************
*  Single Benchmark Functions
**************************************/

static int
benchOnce(const buffers_t buf, const contexts_t ctx, const int cLevel)
{
    BMK_benchResult_t testResult;
    g_params = adjustParams(overwriteParams(cParamsToPVals(ZSTD_getCParams(cLevel, buf.maxBlockSize, ctx.dictSize)), g_params), buf.maxBlockSize, ctx.dictSize);

    if (BMK_benchParam(&testResult, buf, ctx, g_params)) {
        DISPLAY("Error during benchmarking\n");
        return 1;
    }

    BMK_printWinner(stdout, CUSTOM_LEVEL, testResult, g_params, buf.srcSize);

    return 0;
}

static int benchSample(double compressibility, int cLevel)
{
    const char* const name = "Sample 10MB";
    size_t const benchedSize = 10 MB;
    void* const srcBuffer = malloc(benchedSize);
    int ret = 0;

    buffers_t buf;
    contexts_t ctx;

    if(srcBuffer == NULL) {
        DISPLAY("Out of Memory\n");
        return 2;
    }

    RDG_genBuffer(srcBuffer, benchedSize, compressibility, 0.0, 0);

    if(createBuffersFromMemory(&buf, srcBuffer, 1, &benchedSize)) {
        DISPLAY("Buffer Creation Error\n");
        free(srcBuffer);
        return 3;
    }

    if(createContexts(&ctx, NULL)) {
        DISPLAY("Context Creation Error\n");
        freeBuffers(buf);
        return 1;
    }

    /* bench */
    DISPLAY("\r%79s\r", "");
    DISPLAY("using %s %i%%: \n", name, (int)(compressibility*100));

    if(g_singleRun) {
        ret = benchOnce(buf, ctx, cLevel);
    } else {
        BMK_generate_cLevelTable(buf, ctx);
    }

    freeBuffers(buf);
    freeContexts(ctx);

    return ret;
}

/* benchFiles() :
 * note: while this function takes a table of filenames,
 * in practice, only the first filename will be used */
static int benchFiles(const char** fileNamesTable, int nbFiles,
                      const char* dictFileName, int cLevel)
{
    buffers_t buf;
    contexts_t ctx;
    int ret = 0;

    if (createBuffers(&buf, fileNamesTable, nbFiles)) {
        DISPLAY("unable to load files\n");
        return 1;
    }

    if (createContexts(&ctx, dictFileName)) {
        DISPLAY("unable to load dictionary\n");
        freeBuffers(buf);
        return 2;
    }

    DISPLAY("\r%79s\r", "");
    if (nbFiles == 1) {
        DISPLAY("using %s : \n", fileNamesTable[0]);
    } else {
        DISPLAY("using %d Files : \n", nbFiles);
    }

    if (g_singleRun) {
        ret = benchOnce(buf, ctx, cLevel);
    } else {
        BMK_generate_cLevelTable(buf, ctx);
    }

    freeBuffers(buf);
    freeContexts(ctx);
    return ret;
}


/*-************************************
*  Local Optimization Functions
**************************************/

/* One iteration of hill climbing. Specifically, it first tries all
 * valid parameter configurations w/ manhattan distance 1 and picks the best one
 * failing that, it progressively tries candidates further and further away (up to #dim + 2)
 * if it finds a candidate exceeding winnerInfo, it will repeat. Otherwise, it will stop the
 * current stage of hill climbing.
 * Each iteration of hill climbing proceeds in 2 'phases'. Phase 1 climbs according to
 * the resultScore function, which is effectively a linear increase in reward until it reaches
 * the constraint-satisfying value, it which point any excess results in only logarithmic reward.
 * This aims to find some constraint-satisfying point.
 * Phase 2 optimizes in accordance with what the original function sets out to maximize, with
 * all feasible solutions valued over all infeasible solutions.
 */

/* sanitize all params here.
 * all generation after random should be sanitized. (maybe sanitize random)
 */
static winnerInfo_t climbOnce(const constraint_t target,
                memoTable_t* mtAll,
                const buffers_t buf, const contexts_t ctx,
                const paramValues_t init)
{
    /*
     * cparam - currently considered 'center'
     * candidate - params to benchmark/results
     * winner - best option found so far.
     */
    paramValues_t cparam = init;
    winnerInfo_t candidateInfo, winnerInfo;
    int better = 1;
    int feas = 0;

    winnerInfo = initWinnerInfo(init);
    candidateInfo = winnerInfo;

    {   winnerInfo_t bestFeasible1 = initWinnerInfo(cparam);
        DEBUGOUTPUT("Climb Part 1\n");
        while(better) {
            int offset;
            size_t i, dist;
            const size_t varLen = mtAll[cparam.vals[strt_ind]].varLen;
            better = 0;
            DEBUGOUTPUT("Start\n");
            cparam = winnerInfo.params;
            candidateInfo.params = cparam;
             /* all dist-1 candidates */
            for (i = 0; i < varLen; i++) {
                for (offset = -1; offset <= 1; offset += 2) {
                    CHECKTIME(winnerInfo);
                    candidateInfo.params = cparam;
                    paramVaryOnce(mtAll[cparam.vals[strt_ind]].varArray[i], offset, &candidateInfo.params);

                    if(paramValid(candidateInfo.params)) {
                        int res;
                        res = benchMemo(&candidateInfo.result, buf, ctx,
                            sanitizeParams(candidateInfo.params), target, &winnerInfo.result, mtAll, feas);
                        DEBUGOUTPUT("Res: %d\n", res);
                        if(res == BETTER_RESULT) { /* synonymous with better when called w/ infeasibleBM */
                            winnerInfo = candidateInfo;
                            better = 1;
                            if(compareResultLT(bestFeasible1.result, winnerInfo.result, target, buf.srcSize)) {
                                bestFeasible1 = winnerInfo;
                            }
                        }
                    }
                }  /* for (offset = -1; offset <= 1; offset += 2) */
            }   /* for (i = 0; i < varLen; i++) */

            if(better) {
                continue;
            }

            for (dist = 2; dist < varLen + 2; dist++) { /* varLen is # dimensions */
                for (i = 0; i < (1 << varLen) / varLen + 2; i++) {
                    int res;
                    CHECKTIME(winnerInfo);
                    candidateInfo.params = cparam;
                    /* param error checking already done here */
                    paramVariation(&candidateInfo.params, mtAll, (U32)dist);

                    res = benchMemo(&candidateInfo.result,
                                buf, ctx,
                                sanitizeParams(candidateInfo.params), target,
                                &winnerInfo.result, mtAll, feas);
                    DEBUGOUTPUT("Res: %d\n", res);
                    if (res == BETTER_RESULT) { /* synonymous with better in this case*/
                        winnerInfo = candidateInfo;
                        better = 1;
                        if (compareResultLT(bestFeasible1.result, winnerInfo.result, target, buf.srcSize)) {
                            bestFeasible1 = winnerInfo;
                        }
                        break;
                    }
                }

                if (better) {
                    break;
                }
            }   /* for(dist = 2; dist < varLen + 2; dist++) */

            if (!better) { /* infeas -> feas -> stop */
                if (feas) return winnerInfo;
                feas = 1;
                better = 1;
                winnerInfo = bestFeasible1; /* note with change, bestFeasible may not necessarily be feasible, but if one has been benchmarked, it will be. */
                DEBUGOUTPUT("Climb Part 2\n");
            }
        }
        winnerInfo = bestFeasible1;
    }

    return winnerInfo;
}

/* Optimizes for a fixed strategy */

/* flexible parameters: iterations of failed climbing (or if we do non-random, maybe this is when everything is close to visitied)
   weight more on visit for bad results, less on good results/more on later results / ones with more failures.
   allocate memoTable here.
 */
static winnerInfo_t
optimizeFixedStrategy(const buffers_t buf, const contexts_t ctx,
                      const constraint_t target, paramValues_t paramTarget,
                      const ZSTD_strategy strat,
                      memoTable_t* memoTableArray, const int tries)
{
    int i = 0;

    paramValues_t init;
    winnerInfo_t winnerInfo, candidateInfo;
    winnerInfo = initWinnerInfo(emptyParams());
    /* so climb is given the right fixed strategy */
    paramTarget.vals[strt_ind] = strat;
    /* to pass ZSTD_checkCParams */
    paramTarget = cParamUnsetMin(paramTarget);

    init = paramTarget;

    for(i = 0; i < tries; i++) {
        DEBUGOUTPUT("Restart\n");
        do {
            randomConstrainedParams(&init, memoTableArray, strat);
        } while(redundantParams(init, target, buf.maxBlockSize));
        candidateInfo = climbOnce(target, memoTableArray, buf, ctx, init);
        if (compareResultLT(winnerInfo.result, candidateInfo.result, target, buf.srcSize)) {
            winnerInfo = candidateInfo;
            BMK_printWinnerOpt(stdout, CUSTOM_LEVEL, winnerInfo.result, winnerInfo.params, target, buf.srcSize);
            i = 0;
            continue;
        }
        CHECKTIME(winnerInfo);
        i++;
    }
    return winnerInfo;
}

/* goes best, best-1, best+1, best-2, ... */
/* return 0 if nothing remaining */
static int nextStrategy(const int currentStrategy, const int bestStrategy)
{
    if(bestStrategy <= currentStrategy) {
        int candidate = 2 * bestStrategy - currentStrategy - 1;
        if(candidate < 1) {
            candidate = currentStrategy + 1;
            if(candidate > (int)ZSTD_STRATEGY_MAX) {
                return 0;
            } else {
                return candidate;
            }
        } else {
            return candidate;
        }
    } else { /* bestStrategy >= currentStrategy */
        int candidate = 2 * bestStrategy - currentStrategy;
        if(candidate > (int)ZSTD_STRATEGY_MAX) {
            candidate = currentStrategy - 1;
            if(candidate < 1) {
                return 0;
            } else {
                return candidate;
            }
        } else {
            return candidate;
        }
    }
}

/* experiment with playing with this and decay value */

/* main fn called when using --optimize */
/* Does strategy selection by benchmarking default compression levels
 * then optimizes by strategy, starting with the best one and moving
 * progressively moving further away by number
 * args:
 * fileNamesTable - list of files to benchmark
 * nbFiles - length of fileNamesTable
 * dictFileName - name of dictionary file if one, else NULL
 * target - performance constraints (cSpeed, dSpeed, cMem)
 * paramTarget - parameter constraints (i.e. restriction search space to where strategy = ZSTD_fast)
 * cLevel - compression level to exceed (all solutions must be > lvl in cSpeed + ratio)
 */

static int g_maxTries = 5;
#define TRY_DECAY 1

static int
optimizeForSize(const char* const * const fileNamesTable, const size_t nbFiles,
                const char* dictFileName,
                constraint_t target, paramValues_t paramTarget,
                const int cLevelOpt, const int cLevelRun,
                const U32 memoTableLog)
{
    varInds_t varArray [NUM_PARAMS];
    int ret = 0;
    const size_t varLen = variableParams(paramTarget, varArray, dictFileName != NULL);
    winnerInfo_t winner = initWinnerInfo(emptyParams());
    memoTable_t* allMT = NULL;
    paramValues_t paramBase;
    contexts_t ctx;
    buffers_t buf;
    g_time = UTIL_getTime();

    if (createBuffers(&buf, fileNamesTable, nbFiles)) {
        DISPLAY("unable to load files\n");
        return 1;
    }

    if (createContexts(&ctx, dictFileName)) {
        DISPLAY("unable to load dictionary\n");
        freeBuffers(buf);
        return 2;
    }

    if (nbFiles == 1) {
        DISPLAYLEVEL(2, "Loading %s...       \r", fileNamesTable[0]);
    } else {
        DISPLAYLEVEL(2, "Loading %lu Files...       \r", (unsigned long)nbFiles);
    }

    /* sanitize paramTarget */
    optimizerAdjustInput(&paramTarget, buf.maxBlockSize);
    paramBase = cParamUnsetMin(paramTarget);

    allMT = createMemoTableArray(paramTarget, varArray, varLen, memoTableLog);

    if (!allMT) {
        DISPLAY("MemoTable Init Error\n");
        ret = 2;
        goto _cleanUp;
    }

    /* default strictnesses */
    if (g_strictness == PARAM_UNSET) {
        if(g_optmode) {
            g_strictness = 100;
        } else {
            g_strictness = 90;
        }
    } else {
        if(0 >= g_strictness || g_strictness > 100) {
            DISPLAY("Strictness Outside of Bounds\n");
            ret = 4;
            goto _cleanUp;
        }
    }

    /* use level'ing mode instead of normal target mode */
    if (g_optmode) {
        winner.params = cParamsToPVals(ZSTD_getCParams(cLevelOpt, buf.maxBlockSize, ctx.dictSize));
        if(BMK_benchParam(&winner.result, buf, ctx, winner.params)) {
            ret = 3;
            goto _cleanUp;
        }

        g_lvltarget = winner.result;
        g_lvltarget.cSpeed *= ((double)g_strictness) / 100;
        g_lvltarget.dSpeed *= ((double)g_strictness) / 100;
        g_lvltarget.cSize /= ((double)g_strictness) / 100;

        target.cSpeed = (U32)g_lvltarget.cSpeed;
        target.dSpeed = (U32)g_lvltarget.dSpeed;

        BMK_printWinnerOpt(stdout, cLevelOpt, winner.result, winner.params, target, buf.srcSize);
    }

    /* Don't want it to return anything worse than the best known result */
    if (g_singleRun) {
        BMK_benchResult_t res;
        g_params = adjustParams(overwriteParams(cParamsToPVals(ZSTD_getCParams(cLevelRun, buf.maxBlockSize, ctx.dictSize)), g_params), buf.maxBlockSize, ctx.dictSize);
        if (BMK_benchParam(&res, buf, ctx, g_params)) {
            ret = 45;
            goto _cleanUp;
        }
        if(compareResultLT(winner.result, res, relaxTarget(target), buf.srcSize)) {
            winner.result = res;
            winner.params = g_params;
        }
    }

    /* bench */
    DISPLAYLEVEL(2, "\r%79s\r", "");
    if(nbFiles == 1) {
        DISPLAYLEVEL(2, "optimizing for %s", fileNamesTable[0]);
    } else {
        DISPLAYLEVEL(2, "optimizing for %lu Files", (unsigned long)nbFiles);
    }

    if(target.cSpeed != 0) { DISPLAYLEVEL(2," - limit compression speed %u MB/s", (unsigned)(target.cSpeed >> 20)); }
    if(target.dSpeed != 0) { DISPLAYLEVEL(2, " - limit decompression speed %u MB/s", (unsigned)(target.dSpeed >> 20)); }
    if(target.cMem != (U32)-1) { DISPLAYLEVEL(2, " - limit memory %u MB", (unsigned)(target.cMem >> 20)); }

    DISPLAYLEVEL(2, "\n");
    init_clockGranularity();

    {   paramValues_t CParams;

        /* find best solution from default params */
        {   const int maxSeeds = g_noSeed ? 1 : ZSTD_maxCLevel();
            DEBUGOUTPUT("Strategy Selection\n");
            if (paramTarget.vals[strt_ind] == PARAM_UNSET) {
                BMK_benchResult_t candidate;
                int i;
                for (i=1; i<=maxSeeds; i++) {
                    int ec;
                    CParams = overwriteParams(cParamsToPVals(ZSTD_getCParams(i, buf.maxBlockSize, ctx.dictSize)), paramTarget);
                    ec = BMK_benchParam(&candidate, buf, ctx, CParams);
                    BMK_printWinnerOpt(stdout, i, candidate, CParams, target, buf.srcSize);

                    if(!ec && compareResultLT(winner.result, candidate, relaxTarget(target), buf.srcSize)) {
                        winner.result = candidate;
                        winner.params = CParams;
                    }

                    CHECKTIMEGT(ret, 0, _displayCleanUp); /* if pass time limit, stop */
                    /* if the current params are too slow, just stop. */
                    if(target.cSpeed > candidate.cSpeed * 3 / 2) { break; }
                }

                BMK_printWinnerOpt(stdout, CUSTOM_LEVEL, winner.result, winner.params, target, buf.srcSize);
            }
        }

        DEBUGOUTPUT("Real Opt\n");
        /* start 'real' optimization */
        {   int bestStrategy = (int)winner.params.vals[strt_ind];
            if (paramTarget.vals[strt_ind] == PARAM_UNSET) {
                int st = bestStrategy;
                int tries = g_maxTries;

                /* one iterations of hill climbing with the level-defined parameters. */
                {   winnerInfo_t const w1 = climbOnce(target, allMT, buf, ctx, winner.params);
                    if (compareResultLT(winner.result, w1.result, target, buf.srcSize)) {
                        winner = w1;
                    }
                    CHECKTIMEGT(ret, 0, _displayCleanUp);
                }

                while(st && tries > 0) {
                    winnerInfo_t wc;
                    DEBUGOUTPUT("StrategySwitch: %s\n", g_stratName[st]);

                    wc = optimizeFixedStrategy(buf, ctx, target, paramBase, st, allMT, tries);

                    if(compareResultLT(winner.result, wc.result, target, buf.srcSize)) {
                        winner = wc;
                        tries = g_maxTries;
                        bestStrategy = st;
                    } else {
                        st = nextStrategy(st, bestStrategy);
                        tries -= TRY_DECAY;
                    }
                    CHECKTIMEGT(ret, 0, _displayCleanUp);
                }
            } else {
                winner = optimizeFixedStrategy(buf, ctx, target, paramBase, paramTarget.vals[strt_ind], allMT, g_maxTries);
            }

        }

        /* no solution found */
        if(winner.result.cSize == (size_t)-1) {
            ret = 1;
            DISPLAY("No feasible solution found\n");
            goto _cleanUp;
        }

        /* end summary */
_displayCleanUp:
        if (g_displayLevel >= 0) {
            BMK_displayOneResult(stdout, winner, buf.srcSize);
        }
        BMK_paramValues_into_commandLine(stdout, winner.params);
        DISPLAYLEVEL(1, "grillParams size - optimizer completed \n");
    }

_cleanUp:
    freeContexts(ctx);
    freeBuffers(buf);
    freeMemoTableArray(allMT);
    return ret;
}

/*-************************************
*  CLI parsing functions
**************************************/

/** longCommandWArg() :
 *  check if *stringPtr is the same as longCommand.
 *  If yes, @return 1 and advances *stringPtr to the position which immediately follows longCommand.
 * @return 0 and doesn't modify *stringPtr otherwise.
 * from zstdcli.c
 */
static unsigned longCommandWArg(const char** stringPtr, const char* longCommand)
{
    size_t const comSize = strlen(longCommand);
    int const result = !strncmp(*stringPtr, longCommand, comSize);
    if (result) *stringPtr += comSize;
    return result;
}

static void errorOut(const char* msg)
{
    DISPLAY("%s \n", msg); exit(1);
}

/*! readU32FromChar() :
 * @return : unsigned integer value read from input in `char` format.
 *  allows and interprets K, KB, KiB, M, MB and MiB suffix.
 *  Will also modify `*stringPtr`, advancing it to position where it stopped reading.
 *  Note : function will exit() program if digit sequence overflows */
static unsigned readU32FromChar(const char** stringPtr)
{
    const char errorMsg[] = "error: numeric value too large";
    unsigned sign = 1;
    unsigned result = 0;
    if(**stringPtr == '-') { sign = (unsigned)-1; (*stringPtr)++; }
    while ((**stringPtr >='0') && (**stringPtr <='9')) {
        unsigned const max = (((unsigned)(-1)) / 10) - 1;
        if (result > max) errorOut(errorMsg);
        result *= 10, result += **stringPtr - '0', (*stringPtr)++ ;
    }
    if ((**stringPtr=='K') || (**stringPtr=='M')) {
        unsigned const maxK = ((unsigned)(-1)) >> 10;
        if (result > maxK) errorOut(errorMsg);
        result <<= 10;
        if (**stringPtr=='M') {
            if (result > maxK) errorOut(errorMsg);
            result <<= 10;
        }
        (*stringPtr)++;  /* skip `K` or `M` */
        if (**stringPtr=='i') (*stringPtr)++;
        if (**stringPtr=='B') (*stringPtr)++;
    }
    return result * sign;
}

static double readDoubleFromChar(const char** stringPtr)
{
    double result = 0, divide = 10;
    while ((**stringPtr >='0') && (**stringPtr <='9')) {
        result *= 10, result += **stringPtr - '0', (*stringPtr)++ ;
    }
    if(**stringPtr!='.') {
        return result;
    }
    (*stringPtr)++;
    while ((**stringPtr >='0') && (**stringPtr <='9')) {
        result += (double)(**stringPtr - '0') / divide, divide *= 10, (*stringPtr)++ ;
    }
    return result;
}

static int usage(const char* exename)
{
    DISPLAY( "Usage :\n");
    DISPLAY( "      %s [arg] file\n", exename);
    DISPLAY( "Arguments :\n");
    DISPLAY( " file : path to the file used as reference (if none, generates a compressible sample)\n");
    DISPLAY( " -H/-h  : Help (this text + advanced options)\n");
    return 0;
}

static int usage_advanced(void)
{
    DISPLAY( "\nAdvanced options :\n");
    DISPLAY( " -T#          : set level 1 speed objective \n");
    DISPLAY( " -B#          : cut input into blocks of size # (default : single block) \n");
    DISPLAY( " --optimize=  : same as -O with more verbose syntax (see README.md)\n");
    DISPLAY( " -S           : Single run \n");
    DISPLAY( " --zstd       : Single run, parameter selection same as zstdcli \n");
    DISPLAY( " -P#          : generated sample compressibility (default : %.1f%%) \n", COMPRESSIBILITY_DEFAULT * 100);
    DISPLAY( " -t#          : Caps runtime of operation in seconds (default : %u seconds (%.1f hours)) \n",
                                (unsigned)g_timeLimit_s, (double)g_timeLimit_s / 3600);
    DISPLAY( " -v           : Prints Benchmarking output\n");
    DISPLAY( " -D           : Next argument dictionary file\n");
    DISPLAY( " -s           : Seperate Files\n");
    return 0;
}

static int badusage(const char* exename)
{
    DISPLAY("Wrong parameters\n");
    usage(exename);
    return 1;
}

#define PARSE_SUB_ARGS(stringLong, stringShort, variable) { \
    if ( longCommandWArg(&argument, stringLong)             \
      || longCommandWArg(&argument, stringShort) ) {        \
          variable = readU32FromChar(&argument);            \
          if (argument[0]==',') {                           \
              argument++; continue;                         \
          } else break;                                     \
}   }

/* 1 if successful parse, 0 otherwise */
static int parse_params(const char** argptr, paramValues_t* pv) {
    int matched = 0;
    const char* argOrig = *argptr;
    varInds_t v;
    for(v = 0; v < NUM_PARAMS; v++) {
        if ( longCommandWArg(argptr,g_shortParamNames[v])
          || longCommandWArg(argptr, g_paramNames[v]) ) {
            if(**argptr == '=') {
                (*argptr)++;
                pv->vals[v] = readU32FromChar(argptr);
                matched = 1;
                break;
            }
        }
        /* reset and try again */
        *argptr = argOrig;
    }
    return matched;
}

/*-************************************
*  Main
**************************************/

int main(int argc, const char** argv)
{
    int i,
        filenamesStart=0,
        result;
    const char* exename=argv[0];
    const char* input_filename = NULL;
    const char* dictFileName = NULL;
    U32 main_pause = 0;
    int cLevelOpt = 0, cLevelRun = 0;
    int seperateFiles = 0;
    double compressibility = COMPRESSIBILITY_DEFAULT;
    U32 memoTableLog = PARAM_UNSET;
    constraint_t target = { 0, 0, (U32)-1 };

    paramValues_t paramTarget = emptyParams();
    g_params = emptyParams();

    assert(argc>=1);   /* for exename */

    for(i=1; i<argc; i++) {
        const char* argument = argv[i];
        DEBUGOUTPUT("%d: %s\n", i, argument);
        assert(argument != NULL);

        if(!strcmp(argument,"--no-seed")) { g_noSeed = 1; continue; }

        if (longCommandWArg(&argument, "--optimize=")) {
            g_optimizer = 1;
            for ( ; ;) {
                if(parse_params(&argument, &paramTarget)) { if(argument[0] == ',') { argument++; continue; } else break; }
                PARSE_SUB_ARGS("compressionSpeed=" ,  "cSpeed=", target.cSpeed);
                PARSE_SUB_ARGS("decompressionSpeed=", "dSpeed=", target.dSpeed);
                PARSE_SUB_ARGS("compressionMemory=" , "cMem=", target.cMem);
                PARSE_SUB_ARGS("strict=", "stc=", g_strictness);
                PARSE_SUB_ARGS("maxTries=", "tries=", g_maxTries);
                PARSE_SUB_ARGS("memoLimitLog=", "memLog=", memoTableLog);
                if (longCommandWArg(&argument, "level=") || longCommandWArg(&argument, "lvl=")) { cLevelOpt = readU32FromChar(&argument); g_optmode = 1; if (argument[0]==',') { argument++; continue; } else break; }
                if (longCommandWArg(&argument, "speedForRatio=") || longCommandWArg(&argument, "speedRatio=")) { g_ratioMultiplier = readDoubleFromChar(&argument); if (argument[0]==',') { argument++; continue; } else break; }

                DISPLAY("invalid optimization parameter \n");
                return 1;
            }

            if (argument[0] != 0) {
                DISPLAY("invalid --optimize= format\n");
                return 1; /* check the end of string */
            }
            continue;
        } else if (longCommandWArg(&argument, "--zstd=")) {
        /* Decode command (note : aggregated commands are allowed) */
            g_singleRun = 1;
            for ( ; ;) {
                if(parse_params(&argument, &g_params)) { if(argument[0] == ',') { argument++; continue; } else break; }
                if (longCommandWArg(&argument, "level=") || longCommandWArg(&argument, "lvl=")) { cLevelRun = readU32FromChar(&argument); g_params = emptyParams(); if (argument[0]==',') { argument++; continue; } else break; }

                DISPLAY("invalid compression parameter \n");
                return 1;
            }

            if (argument[0] != 0) {
                DISPLAY("invalid --zstd= format\n");
                return 1; /* check the end of string */
            }
            continue;
            /* if not return, success */

        } else if (longCommandWArg(&argument, "--display=")) {
            /* Decode command (note : aggregated commands are allowed) */
            memset(g_silenceParams, 1, sizeof(g_silenceParams));
            for ( ; ;) {
                int found = 0;
                varInds_t v;
                for(v = 0; v < NUM_PARAMS; v++) {
                    if(longCommandWArg(&argument, g_shortParamNames[v]) || longCommandWArg(&argument, g_paramNames[v])) {
                        g_silenceParams[v] = 0;
                        found = 1;
                    }
                }
                if(longCommandWArg(&argument, "compressionParameters") || longCommandWArg(&argument, "cParams")) {
                    for(v = 0; v <= strt_ind; v++) {
                        g_silenceParams[v] = 0;
                    }
                    found = 1;
                }


                if(found) {
                    if(argument[0]==',') {
                        continue;
                    } else {
                        break;
                    }
                }
                DISPLAY("invalid parameter name parameter \n");
                return 1;
            }

            if (argument[0] != 0) {
                DISPLAY("invalid --display format\n");
                return 1; /* check the end of string */
            }
            continue;
        } else if (argument[0]=='-') {
            argument++;

            while (argument[0]!=0) {

                switch(argument[0])
                {
                    /* Display help on usage */
                case 'h' :
                case 'H': usage(exename); usage_advanced(); return 0;

                    /* Pause at the end (hidden option) */
                case 'p': main_pause = 1; argument++; break;

                    /* Sample compressibility (when no file provided) */
                case 'P':
                    argument++;
                    {   U32 const proba32 = readU32FromChar(&argument);
                        compressibility = (double)proba32 / 100.;
                    }
                    break;

                    /* Run Single conf */
                case 'S':
                    g_singleRun = 1;
                    argument++;
                    for ( ; ; ) {
                        switch(*argument)
                        {
                        case 'w':
                            argument++;
                            g_params.vals[wlog_ind] = readU32FromChar(&argument);
                            continue;
                        case 'c':
                            argument++;
                            g_params.vals[clog_ind] = readU32FromChar(&argument);
                            continue;
                        case 'h':
                            argument++;
                            g_params.vals[hlog_ind] = readU32FromChar(&argument);
                            continue;
                        case 's':
                            argument++;
                            g_params.vals[slog_ind] = readU32FromChar(&argument);
                            continue;
                        case 'l':  /* search length */
                            argument++;
                            g_params.vals[mml_ind] = readU32FromChar(&argument);
                            continue;
                        case 't':  /* target length */
                            argument++;
                            g_params.vals[tlen_ind] = readU32FromChar(&argument);
                            continue;
                        case 'S':  /* strategy */
                            argument++;
                            g_params.vals[strt_ind] = readU32FromChar(&argument);
                            continue;
                        case 'f':  /* forceAttachDict */
                            argument++;
                            g_params.vals[fadt_ind] = readU32FromChar(&argument);
                            continue;
                        case 'L':
                            {   argument++;
                                cLevelRun = readU32FromChar(&argument);
                                g_params = emptyParams();
                                continue;
                            }
                        default : ;
                        }
                        break;
                    }

                    break;

                    /* target level1 speed objective, in MB/s */
                case 'T':
                    argument++;
                    g_target = readU32FromChar(&argument);
                    break;

                    /* cut input into blocks */
                case 'B':
                    argument++;
                    g_blockSize = readU32FromChar(&argument);
                    DISPLAY("using %u KB block size \n", (unsigned)(g_blockSize>>10));
                    break;

                    /* caps runtime (in seconds) */
                case 't':
                    argument++;
                    g_timeLimit_s = readU32FromChar(&argument);
                    break;

                case 's':
                    argument++;
                    seperateFiles = 1;
                    break;

                case 'q':
                    while (argument[0] == 'q') { argument++; g_displayLevel--; }
                    break;

                case 'v':
                    while (argument[0] == 'v') { argument++; g_displayLevel++; }
                    break;

                /* load dictionary file (only applicable for optimizer rn) */
                case 'D':
                    if(i == argc - 1) { /* last argument, return error. */
                        DISPLAY("Dictionary file expected but not given : %d\n", i);
                        return 1;
                    } else {
                        i++;
                        dictFileName = argv[i];
                        argument += strlen(argument);
                    }
                    break;

                    /* Unknown command */
                default : return badusage(exename);
                }
            }
            continue;
        }   /* if (argument[0]=='-') */

        /* first provided filename is input */
        if (!input_filename) { input_filename=argument; filenamesStart=i; continue; }
    }

    /* Welcome message */
    DISPLAYLEVEL(2, WELCOME_MESSAGE);

    if (filenamesStart==0) {
        if (g_optimizer) {
            DISPLAY("Optimizer Expects File\n");
            return 1;
        } else {
            result = benchSample(compressibility, cLevelRun);
        }
    } else {
        if(seperateFiles) {
            for(i = 0; i < argc - filenamesStart; i++) {
                if (g_optimizer) {
                    result = optimizeForSize(argv+filenamesStart + i, 1, dictFileName, target, paramTarget, cLevelOpt, cLevelRun, memoTableLog);
                    if(result) { DISPLAY("Error on File %d", i); return result; }
                } else {
                    result = benchFiles(argv+filenamesStart + i, 1, dictFileName, cLevelRun);
                    if(result) { DISPLAY("Error on File %d", i); return result; }
                }
            }
        } else {
            if (g_optimizer) {
                result = optimizeForSize(argv+filenamesStart, argc-filenamesStart, dictFileName, target, paramTarget, cLevelOpt, cLevelRun, memoTableLog);
            } else {
                result = benchFiles(argv+filenamesStart, argc-filenamesStart, dictFileName, cLevelRun);
            }
        }
    }

    if (main_pause) { int unused; printf("press enter...\n"); unused = getchar(); (void)unused; }

    return result;
}
