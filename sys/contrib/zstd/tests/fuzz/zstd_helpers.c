/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

#define ZSTD_STATIC_LINKING_ONLY

#include "zstd_helpers.h"
#include "fuzz_helpers.h"
#include "zstd.h"

static void set(ZSTD_CCtx *cctx, ZSTD_cParameter param, int value)
{
    FUZZ_ZASSERT(ZSTD_CCtx_setParameter(cctx, param, value));
}

static void setRand(ZSTD_CCtx *cctx, ZSTD_cParameter param, unsigned min,
                    unsigned max, uint32_t *state) {
    unsigned const value = FUZZ_rand32(state, min, max);
    set(cctx, param, value);
}

ZSTD_compressionParameters FUZZ_randomCParams(size_t srcSize, uint32_t *state)
{
    /* Select compression parameters */
    ZSTD_compressionParameters cParams;
    cParams.windowLog = FUZZ_rand32(state, ZSTD_WINDOWLOG_MIN, 15);
    cParams.hashLog = FUZZ_rand32(state, ZSTD_HASHLOG_MIN, 15);
    cParams.chainLog = FUZZ_rand32(state, ZSTD_CHAINLOG_MIN, 16);
    cParams.searchLog = FUZZ_rand32(state, ZSTD_SEARCHLOG_MIN, 9);
    cParams.minMatch = FUZZ_rand32(state, ZSTD_MINMATCH_MIN,
                                          ZSTD_MINMATCH_MAX);
    cParams.targetLength = FUZZ_rand32(state, 0, 512);
    cParams.strategy = FUZZ_rand32(state, ZSTD_STRATEGY_MIN, ZSTD_STRATEGY_MAX);
    return ZSTD_adjustCParams(cParams, srcSize, 0);
}

ZSTD_frameParameters FUZZ_randomFParams(uint32_t *state)
{
    /* Select frame parameters */
    ZSTD_frameParameters fParams;
    fParams.contentSizeFlag = FUZZ_rand32(state, 0, 1);
    fParams.checksumFlag = FUZZ_rand32(state, 0, 1);
    fParams.noDictIDFlag = FUZZ_rand32(state, 0, 1);
    return fParams;
}

ZSTD_parameters FUZZ_randomParams(size_t srcSize, uint32_t *state)
{
    ZSTD_parameters params;
    params.cParams = FUZZ_randomCParams(srcSize, state);
    params.fParams = FUZZ_randomFParams(state);
    return params;
}

void FUZZ_setRandomParameters(ZSTD_CCtx *cctx, size_t srcSize, uint32_t *state)
{
    ZSTD_compressionParameters cParams = FUZZ_randomCParams(srcSize, state);
    set(cctx, ZSTD_c_windowLog, cParams.windowLog);
    set(cctx, ZSTD_c_hashLog, cParams.hashLog);
    set(cctx, ZSTD_c_chainLog, cParams.chainLog);
    set(cctx, ZSTD_c_searchLog, cParams.searchLog);
    set(cctx, ZSTD_c_minMatch, cParams.minMatch);
    set(cctx, ZSTD_c_targetLength, cParams.targetLength);
    set(cctx, ZSTD_c_strategy, cParams.strategy);
    /* Select frame parameters */
    setRand(cctx, ZSTD_c_contentSizeFlag, 0, 1, state);
    setRand(cctx, ZSTD_c_checksumFlag, 0, 1, state);
    setRand(cctx, ZSTD_c_dictIDFlag, 0, 1, state);
    setRand(cctx, ZSTD_c_forceAttachDict, 0, 2, state);
    /* Select long distance matchig parameters */
    setRand(cctx, ZSTD_c_enableLongDistanceMatching, 0, 1, state);
    setRand(cctx, ZSTD_c_ldmHashLog, ZSTD_HASHLOG_MIN, 16, state);
    setRand(cctx, ZSTD_c_ldmMinMatch, ZSTD_LDM_MINMATCH_MIN,
            ZSTD_LDM_MINMATCH_MAX, state);
    setRand(cctx, ZSTD_c_ldmBucketSizeLog, 0, ZSTD_LDM_BUCKETSIZELOG_MAX,
            state);
    setRand(cctx, ZSTD_c_ldmHashRateLog, ZSTD_LDM_HASHRATELOG_MIN,
            ZSTD_LDM_HASHRATELOG_MAX, state);
}
