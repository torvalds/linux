/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/* ******************************************************************
 * huff0 huffman codec,
 * part of Finite State Entropy library
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * You can contact the author at :
 * - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
****************************************************************** */

#ifndef HUF_H_298734234
#define HUF_H_298734234

/* *** Dependencies *** */
#include "zstd_deps.h"    /* size_t */
#include "mem.h"          /* U32 */
#define FSE_STATIC_LINKING_ONLY
#include "fse.h"

/* ***   Tool functions *** */
#define HUF_BLOCKSIZE_MAX (128 * 1024)   /*< maximum input size for a single block compressed with HUF_compress */
size_t HUF_compressBound(size_t size);   /*< maximum compressed size (worst case) */

/* Error Management */
unsigned    HUF_isError(size_t code);       /*< tells if a return value is an error code */
const char* HUF_getErrorName(size_t code);  /*< provides error code string (useful for debugging) */


#define HUF_WORKSPACE_SIZE ((8 << 10) + 512 /* sorting scratch space */)
#define HUF_WORKSPACE_SIZE_U64 (HUF_WORKSPACE_SIZE / sizeof(U64))

/* *** Constants *** */
#define HUF_TABLELOG_MAX      12      /* max runtime value of tableLog (due to static allocation); can be modified up to HUF_TABLELOG_ABSOLUTEMAX */
#define HUF_TABLELOG_DEFAULT  11      /* default tableLog value when none specified */
#define HUF_SYMBOLVALUE_MAX  255

#define HUF_TABLELOG_ABSOLUTEMAX  12  /* absolute limit of HUF_MAX_TABLELOG. Beyond that value, code does not work */
#if (HUF_TABLELOG_MAX > HUF_TABLELOG_ABSOLUTEMAX)
#  error "HUF_TABLELOG_MAX is too large !"
#endif


/* ****************************************
*  Static allocation
******************************************/
/* HUF buffer bounds */
#define HUF_CTABLEBOUND 129
#define HUF_BLOCKBOUND(size) (size + (size>>8) + 8)   /* only true when incompressible is pre-filtered with fast heuristic */
#define HUF_COMPRESSBOUND(size) (HUF_CTABLEBOUND + HUF_BLOCKBOUND(size))   /* Macro version, useful for static allocation */

/* static allocation of HUF's Compression Table */
/* this is a private definition, just exposed for allocation and strict aliasing purpose. never EVER access its members directly */
typedef size_t HUF_CElt;   /* consider it an incomplete type */
#define HUF_CTABLE_SIZE_ST(maxSymbolValue)   ((maxSymbolValue)+2)   /* Use tables of size_t, for proper alignment */
#define HUF_CTABLE_SIZE(maxSymbolValue)       (HUF_CTABLE_SIZE_ST(maxSymbolValue) * sizeof(size_t))
#define HUF_CREATE_STATIC_CTABLE(name, maxSymbolValue) \
    HUF_CElt name[HUF_CTABLE_SIZE_ST(maxSymbolValue)] /* no final ; */

/* static allocation of HUF's DTable */
typedef U32 HUF_DTable;
#define HUF_DTABLE_SIZE(maxTableLog)   (1 + (1<<(maxTableLog)))
#define HUF_CREATE_STATIC_DTABLEX1(DTable, maxTableLog) \
        HUF_DTable DTable[HUF_DTABLE_SIZE((maxTableLog)-1)] = { ((U32)((maxTableLog)-1) * 0x01000001) }
#define HUF_CREATE_STATIC_DTABLEX2(DTable, maxTableLog) \
        HUF_DTable DTable[HUF_DTABLE_SIZE(maxTableLog)] = { ((U32)(maxTableLog) * 0x01000001) }


/* ****************************************
*  Advanced decompression functions
******************************************/

/*
 * Huffman flags bitset.
 * For all flags, 0 is the default value.
 */
typedef enum {
    /*
     * If compiled with DYNAMIC_BMI2: Set flag only if the CPU supports BMI2 at runtime.
     * Otherwise: Ignored.
     */
    HUF_flags_bmi2 = (1 << 0),
    /*
     * If set: Test possible table depths to find the one that produces the smallest header + encoded size.
     * If unset: Use heuristic to find the table depth.
     */
    HUF_flags_optimalDepth = (1 << 1),
    /*
     * If set: If the previous table can encode the input, always reuse the previous table.
     * If unset: If the previous table can encode the input, reuse the previous table if it results in a smaller output.
     */
    HUF_flags_preferRepeat = (1 << 2),
    /*
     * If set: Sample the input and check if the sample is uncompressible, if it is then don't attempt to compress.
     * If unset: Always histogram the entire input.
     */
    HUF_flags_suspectUncompressible = (1 << 3),
    /*
     * If set: Don't use assembly implementations
     * If unset: Allow using assembly implementations
     */
    HUF_flags_disableAsm = (1 << 4),
    /*
     * If set: Don't use the fast decoding loop, always use the fallback decoding loop.
     * If unset: Use the fast decoding loop when possible.
     */
    HUF_flags_disableFast = (1 << 5)
} HUF_flags_e;


/* ****************************************
 *  HUF detailed API
 * ****************************************/
#define HUF_OPTIMAL_DEPTH_THRESHOLD ZSTD_btultra

/*! HUF_compress() does the following:
 *  1. count symbol occurrence from source[] into table count[] using FSE_count() (exposed within "fse.h")
 *  2. (optional) refine tableLog using HUF_optimalTableLog()
 *  3. build Huffman table from count using HUF_buildCTable()
 *  4. save Huffman table to memory buffer using HUF_writeCTable()
 *  5. encode the data stream using HUF_compress4X_usingCTable()
 *
 *  The following API allows targeting specific sub-functions for advanced tasks.
 *  For example, it's possible to compress several blocks using the same 'CTable',
 *  or to save and regenerate 'CTable' using external methods.
 */
unsigned HUF_minTableLog(unsigned symbolCardinality);
unsigned HUF_cardinality(const unsigned* count, unsigned maxSymbolValue);
unsigned HUF_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue, void* workSpace,
 size_t wkspSize, HUF_CElt* table, const unsigned* count, int flags); /* table is used as scratch space for building and testing tables, not a return value */
size_t HUF_writeCTable_wksp(void* dst, size_t maxDstSize, const HUF_CElt* CTable, unsigned maxSymbolValue, unsigned huffLog, void* workspace, size_t workspaceSize);
size_t HUF_compress4X_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const HUF_CElt* CTable, int flags);
size_t HUF_estimateCompressedSize(const HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue);
int HUF_validateCTable(const HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue);

typedef enum {
   HUF_repeat_none,  /*< Cannot use the previous table */
   HUF_repeat_check, /*< Can use the previous table but it must be checked. Note : The previous table must have been constructed by HUF_compress{1, 4}X_repeat */
   HUF_repeat_valid  /*< Can use the previous table and it is assumed to be valid */
 } HUF_repeat;

/* HUF_compress4X_repeat() :
 *  Same as HUF_compress4X_wksp(), but considers using hufTable if *repeat != HUF_repeat_none.
 *  If it uses hufTable it does not modify hufTable or repeat.
 *  If it doesn't, it sets *repeat = HUF_repeat_none, and it sets hufTable to the table used.
 *  If preferRepeat then the old table will always be used if valid.
 *  If suspectUncompressible then some sampling checks will be run to potentially skip huffman coding */
size_t HUF_compress4X_repeat(void* dst, size_t dstSize,
                       const void* src, size_t srcSize,
                       unsigned maxSymbolValue, unsigned tableLog,
                       void* workSpace, size_t wkspSize,    /*< `workSpace` must be aligned on 4-bytes boundaries, `wkspSize` must be >= HUF_WORKSPACE_SIZE */
                       HUF_CElt* hufTable, HUF_repeat* repeat, int flags);

/* HUF_buildCTable_wksp() :
 *  Same as HUF_buildCTable(), but using externally allocated scratch buffer.
 * `workSpace` must be aligned on 4-bytes boundaries, and its size must be >= HUF_CTABLE_WORKSPACE_SIZE.
 */
#define HUF_CTABLE_WORKSPACE_SIZE_U32 ((4 * (HUF_SYMBOLVALUE_MAX + 1)) + 192)
#define HUF_CTABLE_WORKSPACE_SIZE (HUF_CTABLE_WORKSPACE_SIZE_U32 * sizeof(unsigned))
size_t HUF_buildCTable_wksp (HUF_CElt* tree,
                       const unsigned* count, U32 maxSymbolValue, U32 maxNbBits,
                             void* workSpace, size_t wkspSize);

/*! HUF_readStats() :
 *  Read compact Huffman tree, saved by HUF_writeCTable().
 * `huffWeight` is destination buffer.
 * @return : size read from `src` , or an error Code .
 *  Note : Needed by HUF_readCTable() and HUF_readDTableXn() . */
size_t HUF_readStats(BYTE* huffWeight, size_t hwSize,
                     U32* rankStats, U32* nbSymbolsPtr, U32* tableLogPtr,
                     const void* src, size_t srcSize);

/*! HUF_readStats_wksp() :
 * Same as HUF_readStats() but takes an external workspace which must be
 * 4-byte aligned and its size must be >= HUF_READ_STATS_WORKSPACE_SIZE.
 * If the CPU has BMI2 support, pass bmi2=1, otherwise pass bmi2=0.
 */
#define HUF_READ_STATS_WORKSPACE_SIZE_U32 FSE_DECOMPRESS_WKSP_SIZE_U32(6, HUF_TABLELOG_MAX-1)
#define HUF_READ_STATS_WORKSPACE_SIZE (HUF_READ_STATS_WORKSPACE_SIZE_U32 * sizeof(unsigned))
size_t HUF_readStats_wksp(BYTE* huffWeight, size_t hwSize,
                          U32* rankStats, U32* nbSymbolsPtr, U32* tableLogPtr,
                          const void* src, size_t srcSize,
                          void* workspace, size_t wkspSize,
                          int flags);

/* HUF_readCTable() :
 *  Loading a CTable saved with HUF_writeCTable() */
size_t HUF_readCTable (HUF_CElt* CTable, unsigned* maxSymbolValuePtr, const void* src, size_t srcSize, unsigned *hasZeroWeights);

/* HUF_getNbBitsFromCTable() :
 *  Read nbBits from CTable symbolTable, for symbol `symbolValue` presumed <= HUF_SYMBOLVALUE_MAX
 *  Note 1 : If symbolValue > HUF_readCTableHeader(symbolTable).maxSymbolValue, returns 0
 *  Note 2 : is not inlined, as HUF_CElt definition is private
 */
U32 HUF_getNbBitsFromCTable(const HUF_CElt* symbolTable, U32 symbolValue);

typedef struct {
    BYTE tableLog;
    BYTE maxSymbolValue;
    BYTE unused[sizeof(size_t) - 2];
} HUF_CTableHeader;

/* HUF_readCTableHeader() :
 *  @returns The header from the CTable specifying the tableLog and the maxSymbolValue.
 */
HUF_CTableHeader HUF_readCTableHeader(HUF_CElt const* ctable);

/*
 * HUF_decompress() does the following:
 * 1. select the decompression algorithm (X1, X2) based on pre-computed heuristics
 * 2. build Huffman table from save, using HUF_readDTableX?()
 * 3. decode 1 or 4 segments in parallel using HUF_decompress?X?_usingDTable()
 */

/* HUF_selectDecoder() :
 *  Tells which decoder is likely to decode faster,
 *  based on a set of pre-computed metrics.
 * @return : 0==HUF_decompress4X1, 1==HUF_decompress4X2 .
 *  Assumption : 0 < dstSize <= 128 KB */
U32 HUF_selectDecoder (size_t dstSize, size_t cSrcSize);

/*
 *  The minimum workspace size for the `workSpace` used in
 *  HUF_readDTableX1_wksp() and HUF_readDTableX2_wksp().
 *
 *  The space used depends on HUF_TABLELOG_MAX, ranging from ~1500 bytes when
 *  HUF_TABLE_LOG_MAX=12 to ~1850 bytes when HUF_TABLE_LOG_MAX=15.
 *  Buffer overflow errors may potentially occur if code modifications result in
 *  a required workspace size greater than that specified in the following
 *  macro.
 */
#define HUF_DECOMPRESS_WORKSPACE_SIZE ((2 << 10) + (1 << 9))
#define HUF_DECOMPRESS_WORKSPACE_SIZE_U32 (HUF_DECOMPRESS_WORKSPACE_SIZE / sizeof(U32))


/* ====================== */
/* single stream variants */
/* ====================== */

size_t HUF_compress1X_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const HUF_CElt* CTable, int flags);
/* HUF_compress1X_repeat() :
 *  Same as HUF_compress1X_wksp(), but considers using hufTable if *repeat != HUF_repeat_none.
 *  If it uses hufTable it does not modify hufTable or repeat.
 *  If it doesn't, it sets *repeat = HUF_repeat_none, and it sets hufTable to the table used.
 *  If preferRepeat then the old table will always be used if valid.
 *  If suspectUncompressible then some sampling checks will be run to potentially skip huffman coding */
size_t HUF_compress1X_repeat(void* dst, size_t dstSize,
                       const void* src, size_t srcSize,
                       unsigned maxSymbolValue, unsigned tableLog,
                       void* workSpace, size_t wkspSize,   /*< `workSpace` must be aligned on 4-bytes boundaries, `wkspSize` must be >= HUF_WORKSPACE_SIZE */
                       HUF_CElt* hufTable, HUF_repeat* repeat, int flags);

size_t HUF_decompress1X_DCtx_wksp(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize, int flags);
#ifndef HUF_FORCE_DECOMPRESS_X1
size_t HUF_decompress1X2_DCtx_wksp(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize, int flags);   /*< double-symbols decoder */
#endif

/* BMI2 variants.
 * If the CPU has BMI2 support, pass bmi2=1, otherwise pass bmi2=0.
 */
size_t HUF_decompress1X_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF_DTable* DTable, int flags);
#ifndef HUF_FORCE_DECOMPRESS_X2
size_t HUF_decompress1X1_DCtx_wksp(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize, int flags);
#endif
size_t HUF_decompress4X_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF_DTable* DTable, int flags);
size_t HUF_decompress4X_hufOnly_wksp(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize, int flags);
#ifndef HUF_FORCE_DECOMPRESS_X2
size_t HUF_readDTableX1_wksp(HUF_DTable* DTable, const void* src, size_t srcSize, void* workSpace, size_t wkspSize, int flags);
#endif
#ifndef HUF_FORCE_DECOMPRESS_X1
size_t HUF_readDTableX2_wksp(HUF_DTable* DTable, const void* src, size_t srcSize, void* workSpace, size_t wkspSize, int flags);
#endif

#endif   /* HUF_H_298734234 */
