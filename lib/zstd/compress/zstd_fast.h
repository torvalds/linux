/*
 * Copyright (c) Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_FAST_H
#define ZSTD_FAST_H


#include "../common/mem.h"      /* U32 */
#include "zstd_compress_internal.h"

void ZSTD_fillHashTable(ZSTD_matchState_t* ms,
                        void const* end, ZSTD_dictTableLoadMethod_e dtlm);
size_t ZSTD_compressBlock_fast(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD_compressBlock_fast_dictMatchState(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD_compressBlock_fast_extDict(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);


#endif /* ZSTD_FAST_H */
