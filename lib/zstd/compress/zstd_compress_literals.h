/*
 * Copyright (c) Yann Collet, Facebook, Inc.
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

size_t ZSTD_compressRleLiteralsBlock (void* dst, size_t dstCapacity, const void* src, size_t srcSize);

/* If suspectUncompressible then some sampling checks will be run to potentially skip huffman coding */
size_t ZSTD_compressLiterals (ZSTD_hufCTables_t const* prevHuf,
                              ZSTD_hufCTables_t* nextHuf,
                              ZSTD_strategy strategy, int disableLiteralCompression,
                              void* dst, size_t dstCapacity,
                        const void* src, size_t srcSize,
                              void* entropyWorkspace, size_t entropyWorkspaceSize,
                        const int bmi2,
                        unsigned suspectUncompressible);

#endif /* ZSTD_COMPRESS_LITERALS_H */
