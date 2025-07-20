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

#ifndef ZSTD_COMPRESS_SEQUENCES_H
#define ZSTD_COMPRESS_SEQUENCES_H

#include "zstd_compress_internal.h" /* SeqDef */
#include "../common/fse.h" /* FSE_repeat, FSE_CTable */
#include "../common/zstd_internal.h" /* SymbolEncodingType_e, ZSTD_strategy */

typedef enum {
    ZSTD_defaultDisallowed = 0,
    ZSTD_defaultAllowed = 1
} ZSTD_DefaultPolicy_e;

SymbolEncodingType_e
ZSTD_selectEncodingType(
        FSE_repeat* repeatMode, unsigned const* count, unsigned const max,
        size_t const mostFrequent, size_t nbSeq, unsigned const FSELog,
        FSE_CTable const* prevCTable,
        short const* defaultNorm, U32 defaultNormLog,
        ZSTD_DefaultPolicy_e const isDefaultAllowed,
        ZSTD_strategy const strategy);

size_t
ZSTD_buildCTable(void* dst, size_t dstCapacity,
                FSE_CTable* nextCTable, U32 FSELog, SymbolEncodingType_e type,
                unsigned* count, U32 max,
                const BYTE* codeTable, size_t nbSeq,
                const S16* defaultNorm, U32 defaultNormLog, U32 defaultMax,
                const FSE_CTable* prevCTable, size_t prevCTableSize,
                void* entropyWorkspace, size_t entropyWorkspaceSize);

size_t ZSTD_encodeSequences(
            void* dst, size_t dstCapacity,
            FSE_CTable const* CTable_MatchLength, BYTE const* mlCodeTable,
            FSE_CTable const* CTable_OffsetBits, BYTE const* ofCodeTable,
            FSE_CTable const* CTable_LitLength, BYTE const* llCodeTable,
            SeqDef const* sequences, size_t nbSeq, int longOffsets, int bmi2);

size_t ZSTD_fseBitCost(
    FSE_CTable const* ctable,
    unsigned const* count,
    unsigned const max);

size_t ZSTD_crossEntropyCost(short const* norm, unsigned accuracyLog,
                             unsigned const* count, unsigned const max);
#endif /* ZSTD_COMPRESS_SEQUENCES_H */
