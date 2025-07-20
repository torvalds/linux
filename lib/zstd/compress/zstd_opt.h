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

#ifndef ZSTD_OPT_H
#define ZSTD_OPT_H

#include "zstd_compress_internal.h"

#if !defined(ZSTD_EXCLUDE_BTLAZY2_BLOCK_COMPRESSOR) \
 || !defined(ZSTD_EXCLUDE_BTOPT_BLOCK_COMPRESSOR) \
 || !defined(ZSTD_EXCLUDE_BTULTRA_BLOCK_COMPRESSOR)
/* used in ZSTD_loadDictionaryContent() */
void ZSTD_updateTree(ZSTD_MatchState_t* ms, const BYTE* ip, const BYTE* iend);
#endif

#ifndef ZSTD_EXCLUDE_BTOPT_BLOCK_COMPRESSOR
size_t ZSTD_compressBlock_btopt(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD_compressBlock_btopt_dictMatchState(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD_compressBlock_btopt_extDict(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);

#define ZSTD_COMPRESSBLOCK_BTOPT ZSTD_compressBlock_btopt
#define ZSTD_COMPRESSBLOCK_BTOPT_DICTMATCHSTATE ZSTD_compressBlock_btopt_dictMatchState
#define ZSTD_COMPRESSBLOCK_BTOPT_EXTDICT ZSTD_compressBlock_btopt_extDict
#else
#define ZSTD_COMPRESSBLOCK_BTOPT NULL
#define ZSTD_COMPRESSBLOCK_BTOPT_DICTMATCHSTATE NULL
#define ZSTD_COMPRESSBLOCK_BTOPT_EXTDICT NULL
#endif

#ifndef ZSTD_EXCLUDE_BTULTRA_BLOCK_COMPRESSOR
size_t ZSTD_compressBlock_btultra(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD_compressBlock_btultra_dictMatchState(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD_compressBlock_btultra_extDict(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);

        /* note : no btultra2 variant for extDict nor dictMatchState,
         * because btultra2 is not meant to work with dictionaries
         * and is only specific for the first block (no prefix) */
size_t ZSTD_compressBlock_btultra2(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);

#define ZSTD_COMPRESSBLOCK_BTULTRA ZSTD_compressBlock_btultra
#define ZSTD_COMPRESSBLOCK_BTULTRA_DICTMATCHSTATE ZSTD_compressBlock_btultra_dictMatchState
#define ZSTD_COMPRESSBLOCK_BTULTRA_EXTDICT ZSTD_compressBlock_btultra_extDict
#define ZSTD_COMPRESSBLOCK_BTULTRA2 ZSTD_compressBlock_btultra2
#else
#define ZSTD_COMPRESSBLOCK_BTULTRA NULL
#define ZSTD_COMPRESSBLOCK_BTULTRA_DICTMATCHSTATE NULL
#define ZSTD_COMPRESSBLOCK_BTULTRA_EXTDICT NULL
#define ZSTD_COMPRESSBLOCK_BTULTRA2 NULL
#endif

#endif /* ZSTD_OPT_H */
