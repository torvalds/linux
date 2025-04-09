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

#ifndef ZSTD_COMPRESS_LITERALS_H
#define ZSTD_COMPRESS_LITERALS_H

#include "zstd_compress_internal.h" /* ZSTD_hufCTables_t, ZSTD_minGain() */


size_t ZSTD_noCompressLiterals (void* dst, size_t dstCapacity, const void* src, size_t srcSize);

/* ZSTD_compressRleLiteralsBlock() :
 * Conditions :
 * - All bytes in @src are identical
 * - dstCapacity >= 4 */
size_t ZSTD_compressRleLiteralsBlock (void* dst, size_t dstCapacity, const void* src, size_t srcSize);

/* ZSTD_compressLiterals():
 * @entropyWorkspace: must be aligned on 4-bytes boundaries
 * @entropyWorkspaceSize : must be >= HUF_WORKSPACE_SIZE
 * @suspectUncompressible: sampling checks, to potentially skip huffman coding
 */
size_t ZSTD_compressLiterals (void* dst, size_t dstCapacity,
                        const void* src, size_t srcSize,
                              void* entropyWorkspace, size_t entropyWorkspaceSize,
                        const ZSTD_hufCTables_t* prevHuf,
                              ZSTD_hufCTables_t* nextHuf,
                              ZSTD_strategy strategy, int disableLiteralCompression,
                              int suspectUncompressible,
                              int bmi2);

#endif /* ZSTD_COMPRESS_LITERALS_H */
