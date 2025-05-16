/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_DOUBLE_FAST_H
#define ZSTD_DOUBLE_FAST_H

#include "../common/mem.h"      /* U32 */
#include "zstd_compress_internal.h"     /* ZSTD_CCtx, size_t */

#ifndef ZSTD_EXCLUDE_DFAST_BLOCK_COMPRESSOR

void ZSTD_fillDoubleHashTable(ZSTD_MatchState_t* ms,
                              void const* end, ZSTD_dictTableLoadMethod_e dtlm,
                              ZSTD_tableFillPurpose_e tfp);

size_t ZSTD_compressBlock_doubleFast(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD_compressBlock_doubleFast_dictMatchState(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD_compressBlock_doubleFast_extDict(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);

#define ZSTD_COMPRESSBLOCK_DOUBLEFAST ZSTD_compressBlock_doubleFast
#define ZSTD_COMPRESSBLOCK_DOUBLEFAST_DICTMATCHSTATE ZSTD_compressBlock_doubleFast_dictMatchState
#define ZSTD_COMPRESSBLOCK_DOUBLEFAST_EXTDICT ZSTD_compressBlock_doubleFast_extDict
#else
#define ZSTD_COMPRESSBLOCK_DOUBLEFAST NULL
#define ZSTD_COMPRESSBLOCK_DOUBLEFAST_DICTMATCHSTATE NULL
#define ZSTD_COMPRESSBLOCK_DOUBLEFAST_EXTDICT NULL
#endif /* ZSTD_EXCLUDE_DFAST_BLOCK_COMPRESSOR */

#endif /* ZSTD_DOUBLE_FAST_H */
