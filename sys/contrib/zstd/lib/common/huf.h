/* ******************************************************************
   huff0 huffman codec,
   part of Finite State Entropy library
   Copyright (C) 2013-present, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
****************************************************************** */

#if defined (__cplusplus)
extern "C" {
#endif

#ifndef HUF_H_298734234
#define HUF_H_298734234

/* *** Dependencies *** */
#include <stddef.h>    /* size_t */


/* *** library symbols visibility *** */
/* Note : when linking with -fvisibility=hidden on gcc, or by default on Visual,
 *        HUF symbols remain "private" (internal symbols for library only).
 *        Set macro FSE_DLL_EXPORT to 1 if you want HUF symbols visible on DLL interface */
#if defined(FSE_DLL_EXPORT) && (FSE_DLL_EXPORT==1) && defined(__GNUC__) && (__GNUC__ >= 4)
#  define HUF_PUBLIC_API __attribute__ ((visibility ("default")))
#elif defined(FSE_DLL_EXPORT) && (FSE_DLL_EXPORT==1)   /* Visual expected */
#  define HUF_PUBLIC_API __declspec(dllexport)
#elif defined(FSE_DLL_IMPORT) && (FSE_DLL_IMPORT==1)
#  define HUF_PUBLIC_API __declspec(dllimport)  /* not required, just to generate faster code (saves a function pointer load from IAT and an indirect jump) */
#else
#  define HUF_PUBLIC_API
#endif


/* ========================== */
/* ***  simple functions  *** */
/* ========================== */

/** HUF_compress() :
 *  Compress content from buffer 'src', of size 'srcSize', into buffer 'dst'.
 * 'dst' buffer must be already allocated.
 *  Compression runs faster if `dstCapacity` >= HUF_compressBound(srcSize).
 * `srcSize` must be <= `HUF_BLOCKSIZE_MAX` == 128 KB.
 * @return : size of compressed data (<= `dstCapacity`).
 *  Special values : if return == 0, srcData is not compressible => Nothing is stored within dst !!!
 *                   if HUF_isError(return), compression failed (more details using HUF_getErrorName())
 */
HUF_PUBLIC_API size_t HUF_compress(void* dst, size_t dstCapacity,
                             const void* src, size_t srcSize);

/** HUF_decompress() :
 *  Decompress HUF data from buffer 'cSrc', of size 'cSrcSize',
 *  into already allocated buffer 'dst', of minimum size 'dstSize'.
 * `originalSize` : **must** be the ***exact*** size of original (uncompressed) data.
 *  Note : in contrast with FSE, HUF_decompress can regenerate
 *         RLE (cSrcSize==1) and uncompressed (cSrcSize==dstSize) data,
 *         because it knows size to regenerate (originalSize).
 * @return : size of regenerated data (== originalSize),
 *           or an error code, which can be tested using HUF_isError()
 */
HUF_PUBLIC_API size_t HUF_decompress(void* dst,  size_t originalSize,
                               const void* cSrc, size_t cSrcSize);


/* ***   Tool functions *** */
#define HUF_BLOCKSIZE_MAX (128 * 1024)                  /**< maximum input size for a single block compressed with HUF_compress */
HUF_PUBLIC_API size_t HUF_compressBound(size_t size);   /**< maximum compressed size (worst case) */

/* Error Management */
HUF_PUBLIC_API unsigned    HUF_isError(size_t code);       /**< tells if a return value is an error code */
HUF_PUBLIC_API const char* HUF_getErrorName(size_t code);  /**< provides error code string (useful for debugging) */


/* ***   Advanced function   *** */

/** HUF_compress2() :
 *  Same as HUF_compress(), but offers control over `maxSymbolValue` and `tableLog`.
 * `maxSymbolValue` must be <= HUF_SYMBOLVALUE_MAX .
 * `tableLog` must be `<= HUF_TABLELOG_MAX` . */
HUF_PUBLIC_API size_t HUF_compress2 (void* dst, size_t dstCapacity,
                               const void* src, size_t srcSize,
                               unsigned maxSymbolValue, unsigned tableLog);

/** HUF_compress4X_wksp() :
 *  Same as HUF_compress2(), but uses externally allocated `workSpace`.
 * `workspace` must have minimum alignment of 4, and be at least as large as HUF_WORKSPACE_SIZE */
#define HUF_WORKSPACE_SIZE (6 << 10)
#define HUF_WORKSPACE_SIZE_U32 (HUF_WORKSPACE_SIZE / sizeof(U32))
HUF_PUBLIC_API size_t HUF_compress4X_wksp (void* dst, size_t dstCapacity,
                                     const void* src, size_t srcSize,
                                     unsigned maxSymbolValue, unsigned tableLog,
                                     void* workSpace, size_t wkspSize);

#endif   /* HUF_H_298734234 */

/* ******************************************************************
 *  WARNING !!
 *  The following section contains advanced and experimental definitions
 *  which shall never be used in the context of a dynamic library,
 *  because they are not guaranteed to remain stable in the future.
 *  Only consider them in association with static linking.
 * *****************************************************************/
#if defined(HUF_STATIC_LINKING_ONLY) && !defined(HUF_H_HUF_STATIC_LINKING_ONLY)
#define HUF_H_HUF_STATIC_LINKING_ONLY

/* *** Dependencies *** */
#include "mem.h"   /* U32 */


/* *** Constants *** */
#define HUF_TABLELOG_MAX      12      /* max runtime value of tableLog (due to static allocation); can be modified up to HUF_ABSOLUTEMAX_TABLELOG */
#define HUF_TABLELOG_DEFAULT  11      /* default tableLog value when none specified */
#define HUF_SYMBOLVALUE_MAX  255

#define HUF_TABLELOG_ABSOLUTEMAX  15  /* absolute limit of HUF_MAX_TABLELOG. Beyond that value, code does not work */
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
#define HUF_CTABLE_SIZE_U32(maxSymbolValue)   ((maxSymbolValue)+1)   /* Use tables of U32, for proper alignment */
#define HUF_CTABLE_SIZE(maxSymbolValue)       (HUF_CTABLE_SIZE_U32(maxSymbolValue) * sizeof(U32))
#define HUF_CREATE_STATIC_CTABLE(name, maxSymbolValue) \
    U32 name##hb[HUF_CTABLE_SIZE_U32(maxSymbolValue)]; \
    void* name##hv = &(name##hb); \
    HUF_CElt* name = (HUF_CElt*)(name##hv)   /* no final ; */

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
size_t HUF_decompress4X1 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
#ifndef HUF_FORCE_DECOMPRESS_X1
size_t HUF_decompress4X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */
#endif

size_t HUF_decompress4X_DCtx (HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< decodes RLE and uncompressed */
size_t HUF_decompress4X_hufOnly(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize); /**< considers RLE and uncompressed as errors */
size_t HUF_decompress4X_hufOnly_wksp(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize); /**< considers RLE and uncompressed as errors */
size_t HUF_decompress4X1_DCtx(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
size_t HUF_decompress4X1_DCtx_wksp(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize);   /**< single-symbol decoder */
#ifndef HUF_FORCE_DECOMPRESS_X1
size_t HUF_decompress4X2_DCtx(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */
size_t HUF_decompress4X2_DCtx_wksp(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize);   /**< double-symbols decoder */
#endif


/* ****************************************
 *  HUF detailed API
 * ****************************************/

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
unsigned HUF_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue);
typedef struct HUF_CElt_s HUF_CElt;   /* incomplete type */
size_t HUF_buildCTable (HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue, unsigned maxNbBits);   /* @return : maxNbBits; CTable and count can overlap. In which case, CTable will overwrite count content */
size_t HUF_writeCTable (void* dst, size_t maxDstSize, const HUF_CElt* CTable, unsigned maxSymbolValue, unsigned huffLog);
size_t HUF_compress4X_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const HUF_CElt* CTable);

typedef enum {
   HUF_repeat_none,  /**< Cannot use the previous table */
   HUF_repeat_check, /**< Can use the previous table but it must be checked. Note : The previous table must have been constructed by HUF_compress{1, 4}X_repeat */
   HUF_repeat_valid  /**< Can use the previous table and it is assumed to be valid */
 } HUF_repeat;
/** HUF_compress4X_repeat() :
 *  Same as HUF_compress4X_wksp(), but considers using hufTable if *repeat != HUF_repeat_none.
 *  If it uses hufTable it does not modify hufTable or repeat.
 *  If it doesn't, it sets *repeat = HUF_repeat_none, and it sets hufTable to the table used.
 *  If preferRepeat then the old table will always be used if valid. */
size_t HUF_compress4X_repeat(void* dst, size_t dstSize,
                       const void* src, size_t srcSize,
                       unsigned maxSymbolValue, unsigned tableLog,
                       void* workSpace, size_t wkspSize,    /**< `workSpace` must be aligned on 4-bytes boundaries, `wkspSize` must be >= HUF_WORKSPACE_SIZE */
                       HUF_CElt* hufTable, HUF_repeat* repeat, int preferRepeat, int bmi2);

/** HUF_buildCTable_wksp() :
 *  Same as HUF_buildCTable(), but using externally allocated scratch buffer.
 * `workSpace` must be aligned on 4-bytes boundaries, and its size must be >= HUF_CTABLE_WORKSPACE_SIZE.
 */
#define HUF_CTABLE_WORKSPACE_SIZE_U32 (2*HUF_SYMBOLVALUE_MAX +1 +1)
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

/** HUF_readCTable() :
 *  Loading a CTable saved with HUF_writeCTable() */
size_t HUF_readCTable (HUF_CElt* CTable, unsigned* maxSymbolValuePtr, const void* src, size_t srcSize);

/** HUF_getNbBits() :
 *  Read nbBits from CTable symbolTable, for symbol `symbolValue` presumed <= HUF_SYMBOLVALUE_MAX
 *  Note 1 : is not inlined, as HUF_CElt definition is private
 *  Note 2 : const void* used, so that it can provide a statically allocated table as argument (which uses type U32) */
U32 HUF_getNbBits(const void* symbolTable, U32 symbolValue);

/*
 * HUF_decompress() does the following:
 * 1. select the decompression algorithm (X1, X2) based on pre-computed heuristics
 * 2. build Huffman table from save, using HUF_readDTableX?()
 * 3. decode 1 or 4 segments in parallel using HUF_decompress?X?_usingDTable()
 */

/** HUF_selectDecoder() :
 *  Tells which decoder is likely to decode faster,
 *  based on a set of pre-computed metrics.
 * @return : 0==HUF_decompress4X1, 1==HUF_decompress4X2 .
 *  Assumption : 0 < dstSize <= 128 KB */
U32 HUF_selectDecoder (size_t dstSize, size_t cSrcSize);

/**
 *  The minimum workspace size for the `workSpace` used in
 *  HUF_readDTableX1_wksp() and HUF_readDTableX2_wksp().
 *
 *  The space used depends on HUF_TABLELOG_MAX, ranging from ~1500 bytes when
 *  HUF_TABLE_LOG_MAX=12 to ~1850 bytes when HUF_TABLE_LOG_MAX=15.
 *  Buffer overflow errors may potentially occur if code modifications result in
 *  a required workspace size greater than that specified in the following
 *  macro.
 */
#define HUF_DECOMPRESS_WORKSPACE_SIZE (2 << 10)
#define HUF_DECOMPRESS_WORKSPACE_SIZE_U32 (HUF_DECOMPRESS_WORKSPACE_SIZE / sizeof(U32))

#ifndef HUF_FORCE_DECOMPRESS_X2
size_t HUF_readDTableX1 (HUF_DTable* DTable, const void* src, size_t srcSize);
size_t HUF_readDTableX1_wksp (HUF_DTable* DTable, const void* src, size_t srcSize, void* workSpace, size_t wkspSize);
#endif
#ifndef HUF_FORCE_DECOMPRESS_X1
size_t HUF_readDTableX2 (HUF_DTable* DTable, const void* src, size_t srcSize);
size_t HUF_readDTableX2_wksp (HUF_DTable* DTable, const void* src, size_t srcSize, void* workSpace, size_t wkspSize);
#endif

size_t HUF_decompress4X_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF_DTable* DTable);
#ifndef HUF_FORCE_DECOMPRESS_X2
size_t HUF_decompress4X1_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF_DTable* DTable);
#endif
#ifndef HUF_FORCE_DECOMPRESS_X1
size_t HUF_decompress4X2_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF_DTable* DTable);
#endif


/* ====================== */
/* single stream variants */
/* ====================== */

size_t HUF_compress1X (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog);
size_t HUF_compress1X_wksp (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void* workSpace, size_t wkspSize);  /**< `workSpace` must be a table of at least HUF_WORKSPACE_SIZE_U32 unsigned */
size_t HUF_compress1X_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const HUF_CElt* CTable);
/** HUF_compress1X_repeat() :
 *  Same as HUF_compress1X_wksp(), but considers using hufTable if *repeat != HUF_repeat_none.
 *  If it uses hufTable it does not modify hufTable or repeat.
 *  If it doesn't, it sets *repeat = HUF_repeat_none, and it sets hufTable to the table used.
 *  If preferRepeat then the old table will always be used if valid. */
size_t HUF_compress1X_repeat(void* dst, size_t dstSize,
                       const void* src, size_t srcSize,
                       unsigned maxSymbolValue, unsigned tableLog,
                       void* workSpace, size_t wkspSize,   /**< `workSpace` must be aligned on 4-bytes boundaries, `wkspSize` must be >= HUF_WORKSPACE_SIZE */
                       HUF_CElt* hufTable, HUF_repeat* repeat, int preferRepeat, int bmi2);

size_t HUF_decompress1X1 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /* single-symbol decoder */
#ifndef HUF_FORCE_DECOMPRESS_X1
size_t HUF_decompress1X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /* double-symbol decoder */
#endif

size_t HUF_decompress1X_DCtx (HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);
size_t HUF_decompress1X_DCtx_wksp (HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize);
#ifndef HUF_FORCE_DECOMPRESS_X2
size_t HUF_decompress1X1_DCtx(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
size_t HUF_decompress1X1_DCtx_wksp(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize);   /**< single-symbol decoder */
#endif
#ifndef HUF_FORCE_DECOMPRESS_X1
size_t HUF_decompress1X2_DCtx(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */
size_t HUF_decompress1X2_DCtx_wksp(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize);   /**< double-symbols decoder */
#endif

size_t HUF_decompress1X_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF_DTable* DTable);   /**< automatic selection of sing or double symbol decoder, based on DTable */
#ifndef HUF_FORCE_DECOMPRESS_X2
size_t HUF_decompress1X1_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF_DTable* DTable);
#endif
#ifndef HUF_FORCE_DECOMPRESS_X1
size_t HUF_decompress1X2_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF_DTable* DTable);
#endif

/* BMI2 variants.
 * If the CPU has BMI2 support, pass bmi2=1, otherwise pass bmi2=0.
 */
size_t HUF_decompress1X_usingDTable_bmi2(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF_DTable* DTable, int bmi2);
#ifndef HUF_FORCE_DECOMPRESS_X2
size_t HUF_decompress1X1_DCtx_wksp_bmi2(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize, int bmi2);
#endif
size_t HUF_decompress4X_usingDTable_bmi2(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF_DTable* DTable, int bmi2);
size_t HUF_decompress4X_hufOnly_wksp_bmi2(HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize, int bmi2);

#endif /* HUF_STATIC_LINKING_ONLY */

#if defined (__cplusplus)
}
#endif
