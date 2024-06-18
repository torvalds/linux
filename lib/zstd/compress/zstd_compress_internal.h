/*
 * Copyright (c) Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/* This header contains definitions
 * that shall **only** be used by modules within lib/compress.
 */

#ifndef ZSTD_COMPRESS_H
#define ZSTD_COMPRESS_H

/*-*************************************
*  Dependencies
***************************************/
#include "../common/zstd_internal.h"
#include "zstd_cwksp.h"


/*-*************************************
*  Constants
***************************************/
#define kSearchStrength      8
#define HASH_READ_SIZE       8
#define ZSTD_DUBT_UNSORTED_MARK 1   /* For btlazy2 strategy, index ZSTD_DUBT_UNSORTED_MARK==1 means "unsorted".
                                       It could be confused for a real successor at index "1", if sorted as larger than its predecessor.
                                       It's not a big deal though : candidate will just be sorted again.
                                       Additionally, candidate position 1 will be lost.
                                       But candidate 1 cannot hide a large tree of candidates, so it's a minimal loss.
                                       The benefit is that ZSTD_DUBT_UNSORTED_MARK cannot be mishandled after table re-use with a different strategy.
                                       This constant is required by ZSTD_compressBlock_btlazy2() and ZSTD_reduceTable_internal() */


/*-*************************************
*  Context memory management
***************************************/
typedef enum { ZSTDcs_created=0, ZSTDcs_init, ZSTDcs_ongoing, ZSTDcs_ending } ZSTD_compressionStage_e;
typedef enum { zcss_init=0, zcss_load, zcss_flush } ZSTD_cStreamStage;

typedef struct ZSTD_prefixDict_s {
    const void* dict;
    size_t dictSize;
    ZSTD_dictContentType_e dictContentType;
} ZSTD_prefixDict;

typedef struct {
    void* dictBuffer;
    void const* dict;
    size_t dictSize;
    ZSTD_dictContentType_e dictContentType;
    ZSTD_CDict* cdict;
} ZSTD_localDict;

typedef struct {
    HUF_CElt CTable[HUF_CTABLE_SIZE_ST(255)];
    HUF_repeat repeatMode;
} ZSTD_hufCTables_t;

typedef struct {
    FSE_CTable offcodeCTable[FSE_CTABLE_SIZE_U32(OffFSELog, MaxOff)];
    FSE_CTable matchlengthCTable[FSE_CTABLE_SIZE_U32(MLFSELog, MaxML)];
    FSE_CTable litlengthCTable[FSE_CTABLE_SIZE_U32(LLFSELog, MaxLL)];
    FSE_repeat offcode_repeatMode;
    FSE_repeat matchlength_repeatMode;
    FSE_repeat litlength_repeatMode;
} ZSTD_fseCTables_t;

typedef struct {
    ZSTD_hufCTables_t huf;
    ZSTD_fseCTables_t fse;
} ZSTD_entropyCTables_t;

/* *********************************************
*  Entropy buffer statistics structs and funcs *
***********************************************/
/* ZSTD_hufCTablesMetadata_t :
 *  Stores Literals Block Type for a super-block in hType, and
 *  huffman tree description in hufDesBuffer.
 *  hufDesSize refers to the size of huffman tree description in bytes.
 *  This metadata is populated in ZSTD_buildBlockEntropyStats_literals() */
typedef struct {
    symbolEncodingType_e hType;
    BYTE hufDesBuffer[ZSTD_MAX_HUF_HEADER_SIZE];
    size_t hufDesSize;
} ZSTD_hufCTablesMetadata_t;

/* ZSTD_fseCTablesMetadata_t :
 *  Stores symbol compression modes for a super-block in {ll, ol, ml}Type, and
 *  fse tables in fseTablesBuffer.
 *  fseTablesSize refers to the size of fse tables in bytes.
 *  This metadata is populated in ZSTD_buildBlockEntropyStats_sequences() */
typedef struct {
    symbolEncodingType_e llType;
    symbolEncodingType_e ofType;
    symbolEncodingType_e mlType;
    BYTE fseTablesBuffer[ZSTD_MAX_FSE_HEADERS_SIZE];
    size_t fseTablesSize;
    size_t lastCountSize; /* This is to account for bug in 1.3.4. More detail in ZSTD_entropyCompressSeqStore_internal() */
} ZSTD_fseCTablesMetadata_t;

typedef struct {
    ZSTD_hufCTablesMetadata_t hufMetadata;
    ZSTD_fseCTablesMetadata_t fseMetadata;
} ZSTD_entropyCTablesMetadata_t;

/* ZSTD_buildBlockEntropyStats() :
 *  Builds entropy for the block.
 *  @return : 0 on success or error code */
size_t ZSTD_buildBlockEntropyStats(seqStore_t* seqStorePtr,
                             const ZSTD_entropyCTables_t* prevEntropy,
                                   ZSTD_entropyCTables_t* nextEntropy,
                             const ZSTD_CCtx_params* cctxParams,
                                   ZSTD_entropyCTablesMetadata_t* entropyMetadata,
                                   void* workspace, size_t wkspSize);

/* *******************************
*  Compression internals structs *
*********************************/

typedef struct {
    U32 off;            /* Offset sumtype code for the match, using ZSTD_storeSeq() format */
    U32 len;            /* Raw length of match */
} ZSTD_match_t;

typedef struct {
    U32 offset;         /* Offset of sequence */
    U32 litLength;      /* Length of literals prior to match */
    U32 matchLength;    /* Raw length of match */
} rawSeq;

typedef struct {
  rawSeq* seq;          /* The start of the sequences */
  size_t pos;           /* The index in seq where reading stopped. pos <= size. */
  size_t posInSequence; /* The position within the sequence at seq[pos] where reading
                           stopped. posInSequence <= seq[pos].litLength + seq[pos].matchLength */
  size_t size;          /* The number of sequences. <= capacity. */
  size_t capacity;      /* The capacity starting from `seq` pointer */
} rawSeqStore_t;

UNUSED_ATTR static const rawSeqStore_t kNullRawSeqStore = {NULL, 0, 0, 0, 0};

typedef struct {
    int price;
    U32 off;
    U32 mlen;
    U32 litlen;
    U32 rep[ZSTD_REP_NUM];
} ZSTD_optimal_t;

typedef enum { zop_dynamic=0, zop_predef } ZSTD_OptPrice_e;

typedef struct {
    /* All tables are allocated inside cctx->workspace by ZSTD_resetCCtx_internal() */
    unsigned* litFreq;           /* table of literals statistics, of size 256 */
    unsigned* litLengthFreq;     /* table of litLength statistics, of size (MaxLL+1) */
    unsigned* matchLengthFreq;   /* table of matchLength statistics, of size (MaxML+1) */
    unsigned* offCodeFreq;       /* table of offCode statistics, of size (MaxOff+1) */
    ZSTD_match_t* matchTable;    /* list of found matches, of size ZSTD_OPT_NUM+1 */
    ZSTD_optimal_t* priceTable;  /* All positions tracked by optimal parser, of size ZSTD_OPT_NUM+1 */

    U32  litSum;                 /* nb of literals */
    U32  litLengthSum;           /* nb of litLength codes */
    U32  matchLengthSum;         /* nb of matchLength codes */
    U32  offCodeSum;             /* nb of offset codes */
    U32  litSumBasePrice;        /* to compare to log2(litfreq) */
    U32  litLengthSumBasePrice;  /* to compare to log2(llfreq)  */
    U32  matchLengthSumBasePrice;/* to compare to log2(mlfreq)  */
    U32  offCodeSumBasePrice;    /* to compare to log2(offreq)  */
    ZSTD_OptPrice_e priceType;   /* prices can be determined dynamically, or follow a pre-defined cost structure */
    const ZSTD_entropyCTables_t* symbolCosts;  /* pre-calculated dictionary statistics */
    ZSTD_paramSwitch_e literalCompressionMode;
} optState_t;

typedef struct {
  ZSTD_entropyCTables_t entropy;
  U32 rep[ZSTD_REP_NUM];
} ZSTD_compressedBlockState_t;

typedef struct {
    BYTE const* nextSrc;       /* next block here to continue on current prefix */
    BYTE const* base;          /* All regular indexes relative to this position */
    BYTE const* dictBase;      /* extDict indexes relative to this position */
    U32 dictLimit;             /* below that point, need extDict */
    U32 lowLimit;              /* below that point, no more valid data */
    U32 nbOverflowCorrections; /* Number of times overflow correction has run since
                                * ZSTD_window_init(). Useful for debugging coredumps
                                * and for ZSTD_WINDOW_OVERFLOW_CORRECT_FREQUENTLY.
                                */
} ZSTD_window_t;

#define ZSTD_WINDOW_START_INDEX 2

typedef struct ZSTD_matchState_t ZSTD_matchState_t;

#define ZSTD_ROW_HASH_CACHE_SIZE 8       /* Size of prefetching hash cache for row-based matchfinder */

struct ZSTD_matchState_t {
    ZSTD_window_t window;   /* State for window round buffer management */
    U32 loadedDictEnd;      /* index of end of dictionary, within context's referential.
                             * When loadedDictEnd != 0, a dictionary is in use, and still valid.
                             * This relies on a mechanism to set loadedDictEnd=0 when dictionary is no longer within distance.
                             * Such mechanism is provided within ZSTD_window_enforceMaxDist() and ZSTD_checkDictValidity().
                             * When dict referential is copied into active context (i.e. not attached),
                             * loadedDictEnd == dictSize, since referential starts from zero.
                             */
    U32 nextToUpdate;       /* index from which to continue table update */
    U32 hashLog3;           /* dispatch table for matches of len==3 : larger == faster, more memory */

    U32 rowHashLog;                          /* For row-based matchfinder: Hashlog based on nb of rows in the hashTable.*/
    U16* tagTable;                           /* For row-based matchFinder: A row-based table containing the hashes and head index. */
    U32 hashCache[ZSTD_ROW_HASH_CACHE_SIZE]; /* For row-based matchFinder: a cache of hashes to improve speed */

    U32* hashTable;
    U32* hashTable3;
    U32* chainTable;

    U32 forceNonContiguous; /* Non-zero if we should force non-contiguous load for the next window update. */

    int dedicatedDictSearch;  /* Indicates whether this matchState is using the
                               * dedicated dictionary search structure.
                               */
    optState_t opt;         /* optimal parser state */
    const ZSTD_matchState_t* dictMatchState;
    ZSTD_compressionParameters cParams;
    const rawSeqStore_t* ldmSeqStore;
};

typedef struct {
    ZSTD_compressedBlockState_t* prevCBlock;
    ZSTD_compressedBlockState_t* nextCBlock;
    ZSTD_matchState_t matchState;
} ZSTD_blockState_t;

typedef struct {
    U32 offset;
    U32 checksum;
} ldmEntry_t;

typedef struct {
    BYTE const* split;
    U32 hash;
    U32 checksum;
    ldmEntry_t* bucket;
} ldmMatchCandidate_t;

#define LDM_BATCH_SIZE 64

typedef struct {
    ZSTD_window_t window;   /* State for the window round buffer management */
    ldmEntry_t* hashTable;
    U32 loadedDictEnd;
    BYTE* bucketOffsets;    /* Next position in bucket to insert entry */
    size_t splitIndices[LDM_BATCH_SIZE];
    ldmMatchCandidate_t matchCandidates[LDM_BATCH_SIZE];
} ldmState_t;

typedef struct {
    ZSTD_paramSwitch_e enableLdm; /* ZSTD_ps_enable to enable LDM. ZSTD_ps_auto by default */
    U32 hashLog;            /* Log size of hashTable */
    U32 bucketSizeLog;      /* Log bucket size for collision resolution, at most 8 */
    U32 minMatchLength;     /* Minimum match length */
    U32 hashRateLog;       /* Log number of entries to skip */
    U32 windowLog;          /* Window log for the LDM */
} ldmParams_t;

typedef struct {
    int collectSequences;
    ZSTD_Sequence* seqStart;
    size_t seqIndex;
    size_t maxSequences;
} SeqCollector;

struct ZSTD_CCtx_params_s {
    ZSTD_format_e format;
    ZSTD_compressionParameters cParams;
    ZSTD_frameParameters fParams;

    int compressionLevel;
    int forceWindow;           /* force back-references to respect limit of
                                * 1<<wLog, even for dictionary */
    size_t targetCBlockSize;   /* Tries to fit compressed block size to be around targetCBlockSize.
                                * No target when targetCBlockSize == 0.
                                * There is no guarantee on compressed block size */
    int srcSizeHint;           /* User's best guess of source size.
                                * Hint is not valid when srcSizeHint == 0.
                                * There is no guarantee that hint is close to actual source size */

    ZSTD_dictAttachPref_e attachDictPref;
    ZSTD_paramSwitch_e literalCompressionMode;

    /* Multithreading: used to pass parameters to mtctx */
    int nbWorkers;
    size_t jobSize;
    int overlapLog;
    int rsyncable;

    /* Long distance matching parameters */
    ldmParams_t ldmParams;

    /* Dedicated dict search algorithm trigger */
    int enableDedicatedDictSearch;

    /* Input/output buffer modes */
    ZSTD_bufferMode_e inBufferMode;
    ZSTD_bufferMode_e outBufferMode;

    /* Sequence compression API */
    ZSTD_sequenceFormat_e blockDelimiters;
    int validateSequences;

    /* Block splitting */
    ZSTD_paramSwitch_e useBlockSplitter;

    /* Param for deciding whether to use row-based matchfinder */
    ZSTD_paramSwitch_e useRowMatchFinder;

    /* Always load a dictionary in ext-dict mode (not prefix mode)? */
    int deterministicRefPrefix;

    /* Internal use, for createCCtxParams() and freeCCtxParams() only */
    ZSTD_customMem customMem;
};  /* typedef'd to ZSTD_CCtx_params within "zstd.h" */

#define COMPRESS_SEQUENCES_WORKSPACE_SIZE (sizeof(unsigned) * (MaxSeq + 2))
#define ENTROPY_WORKSPACE_SIZE (HUF_WORKSPACE_SIZE + COMPRESS_SEQUENCES_WORKSPACE_SIZE)

/*
 * Indicates whether this compression proceeds directly from user-provided
 * source buffer to user-provided destination buffer (ZSTDb_not_buffered), or
 * whether the context needs to buffer the input/output (ZSTDb_buffered).
 */
typedef enum {
    ZSTDb_not_buffered,
    ZSTDb_buffered
} ZSTD_buffered_policy_e;

/*
 * Struct that contains all elements of block splitter that should be allocated
 * in a wksp.
 */
#define ZSTD_MAX_NB_BLOCK_SPLITS 196
typedef struct {
    seqStore_t fullSeqStoreChunk;
    seqStore_t firstHalfSeqStore;
    seqStore_t secondHalfSeqStore;
    seqStore_t currSeqStore;
    seqStore_t nextSeqStore;

    U32 partitions[ZSTD_MAX_NB_BLOCK_SPLITS];
    ZSTD_entropyCTablesMetadata_t entropyMetadata;
} ZSTD_blockSplitCtx;

struct ZSTD_CCtx_s {
    ZSTD_compressionStage_e stage;
    int cParamsChanged;                  /* == 1 if cParams(except wlog) or compression level are changed in requestedParams. Triggers transmission of new params to ZSTDMT (if available) then reset to 0. */
    int bmi2;                            /* == 1 if the CPU supports BMI2 and 0 otherwise. CPU support is determined dynamically once per context lifetime. */
    ZSTD_CCtx_params requestedParams;
    ZSTD_CCtx_params appliedParams;
    ZSTD_CCtx_params simpleApiParams;    /* Param storage used by the simple API - not sticky. Must only be used in top-level simple API functions for storage. */
    U32   dictID;
    size_t dictContentSize;

    ZSTD_cwksp workspace; /* manages buffer for dynamic allocations */
    size_t blockSize;
    unsigned long long pledgedSrcSizePlusOne;  /* this way, 0 (default) == unknown */
    unsigned long long consumedSrcSize;
    unsigned long long producedCSize;
    struct xxh64_state xxhState;
    ZSTD_customMem customMem;
    ZSTD_threadPool* pool;
    size_t staticSize;
    SeqCollector seqCollector;
    int isFirstBlock;
    int initialized;

    seqStore_t seqStore;      /* sequences storage ptrs */
    ldmState_t ldmState;      /* long distance matching state */
    rawSeq* ldmSequences;     /* Storage for the ldm output sequences */
    size_t maxNbLdmSequences;
    rawSeqStore_t externSeqStore; /* Mutable reference to external sequences */
    ZSTD_blockState_t blockState;
    U32* entropyWorkspace;  /* entropy workspace of ENTROPY_WORKSPACE_SIZE bytes */

    /* Whether we are streaming or not */
    ZSTD_buffered_policy_e bufferedPolicy;

    /* streaming */
    char*  inBuff;
    size_t inBuffSize;
    size_t inToCompress;
    size_t inBuffPos;
    size_t inBuffTarget;
    char*  outBuff;
    size_t outBuffSize;
    size_t outBuffContentSize;
    size_t outBuffFlushedSize;
    ZSTD_cStreamStage streamStage;
    U32    frameEnded;

    /* Stable in/out buffer verification */
    ZSTD_inBuffer expectedInBuffer;
    size_t expectedOutBufferSize;

    /* Dictionary */
    ZSTD_localDict localDict;
    const ZSTD_CDict* cdict;
    ZSTD_prefixDict prefixDict;   /* single-usage dictionary */

    /* Multi-threading */

    /* Tracing */

    /* Workspace for block splitter */
    ZSTD_blockSplitCtx blockSplitCtx;
};

typedef enum { ZSTD_dtlm_fast, ZSTD_dtlm_full } ZSTD_dictTableLoadMethod_e;

typedef enum {
    ZSTD_noDict = 0,
    ZSTD_extDict = 1,
    ZSTD_dictMatchState = 2,
    ZSTD_dedicatedDictSearch = 3
} ZSTD_dictMode_e;

typedef enum {
    ZSTD_cpm_noAttachDict = 0,  /* Compression with ZSTD_noDict or ZSTD_extDict.
                                 * In this mode we use both the srcSize and the dictSize
                                 * when selecting and adjusting parameters.
                                 */
    ZSTD_cpm_attachDict = 1,    /* Compression with ZSTD_dictMatchState or ZSTD_dedicatedDictSearch.
                                 * In this mode we only take the srcSize into account when selecting
                                 * and adjusting parameters.
                                 */
    ZSTD_cpm_createCDict = 2,   /* Creating a CDict.
                                 * In this mode we take both the source size and the dictionary size
                                 * into account when selecting and adjusting the parameters.
                                 */
    ZSTD_cpm_unknown = 3,       /* ZSTD_getCParams, ZSTD_getParams, ZSTD_adjustParams.
                                 * We don't know what these parameters are for. We default to the legacy
                                 * behavior of taking both the source size and the dict size into account
                                 * when selecting and adjusting parameters.
                                 */
} ZSTD_cParamMode_e;

typedef size_t (*ZSTD_blockCompressor) (
        ZSTD_matchState_t* bs, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize);
ZSTD_blockCompressor ZSTD_selectBlockCompressor(ZSTD_strategy strat, ZSTD_paramSwitch_e rowMatchfinderMode, ZSTD_dictMode_e dictMode);


MEM_STATIC U32 ZSTD_LLcode(U32 litLength)
{
    static const BYTE LL_Code[64] = {  0,  1,  2,  3,  4,  5,  6,  7,
                                       8,  9, 10, 11, 12, 13, 14, 15,
                                      16, 16, 17, 17, 18, 18, 19, 19,
                                      20, 20, 20, 20, 21, 21, 21, 21,
                                      22, 22, 22, 22, 22, 22, 22, 22,
                                      23, 23, 23, 23, 23, 23, 23, 23,
                                      24, 24, 24, 24, 24, 24, 24, 24,
                                      24, 24, 24, 24, 24, 24, 24, 24 };
    static const U32 LL_deltaCode = 19;
    return (litLength > 63) ? ZSTD_highbit32(litLength) + LL_deltaCode : LL_Code[litLength];
}

/* ZSTD_MLcode() :
 * note : mlBase = matchLength - MINMATCH;
 *        because it's the format it's stored in seqStore->sequences */
MEM_STATIC U32 ZSTD_MLcode(U32 mlBase)
{
    static const BYTE ML_Code[128] = { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
                                      16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
                                      32, 32, 33, 33, 34, 34, 35, 35, 36, 36, 36, 36, 37, 37, 37, 37,
                                      38, 38, 38, 38, 38, 38, 38, 38, 39, 39, 39, 39, 39, 39, 39, 39,
                                      40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40,
                                      41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41,
                                      42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
                                      42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42 };
    static const U32 ML_deltaCode = 36;
    return (mlBase > 127) ? ZSTD_highbit32(mlBase) + ML_deltaCode : ML_Code[mlBase];
}

/* ZSTD_cParam_withinBounds:
 * @return 1 if value is within cParam bounds,
 * 0 otherwise */
MEM_STATIC int ZSTD_cParam_withinBounds(ZSTD_cParameter cParam, int value)
{
    ZSTD_bounds const bounds = ZSTD_cParam_getBounds(cParam);
    if (ZSTD_isError(bounds.error)) return 0;
    if (value < bounds.lowerBound) return 0;
    if (value > bounds.upperBound) return 0;
    return 1;
}

/* ZSTD_noCompressBlock() :
 * Writes uncompressed block to dst buffer from given src.
 * Returns the size of the block */
MEM_STATIC size_t ZSTD_noCompressBlock (void* dst, size_t dstCapacity, const void* src, size_t srcSize, U32 lastBlock)
{
    U32 const cBlockHeader24 = lastBlock + (((U32)bt_raw)<<1) + (U32)(srcSize << 3);
    RETURN_ERROR_IF(srcSize + ZSTD_blockHeaderSize > dstCapacity,
                    dstSize_tooSmall, "dst buf too small for uncompressed block");
    MEM_writeLE24(dst, cBlockHeader24);
    ZSTD_memcpy((BYTE*)dst + ZSTD_blockHeaderSize, src, srcSize);
    return ZSTD_blockHeaderSize + srcSize;
}

MEM_STATIC size_t ZSTD_rleCompressBlock (void* dst, size_t dstCapacity, BYTE src, size_t srcSize, U32 lastBlock)
{
    BYTE* const op = (BYTE*)dst;
    U32 const cBlockHeader = lastBlock + (((U32)bt_rle)<<1) + (U32)(srcSize << 3);
    RETURN_ERROR_IF(dstCapacity < 4, dstSize_tooSmall, "");
    MEM_writeLE24(op, cBlockHeader);
    op[3] = src;
    return 4;
}


/* ZSTD_minGain() :
 * minimum compression required
 * to generate a compress block or a compressed literals section.
 * note : use same formula for both situations */
MEM_STATIC size_t ZSTD_minGain(size_t srcSize, ZSTD_strategy strat)
{
    U32 const minlog = (strat>=ZSTD_btultra) ? (U32)(strat) - 1 : 6;
    ZSTD_STATIC_ASSERT(ZSTD_btultra == 8);
    assert(ZSTD_cParam_withinBounds(ZSTD_c_strategy, strat));
    return (srcSize >> minlog) + 2;
}

MEM_STATIC int ZSTD_literalsCompressionIsDisabled(const ZSTD_CCtx_params* cctxParams)
{
    switch (cctxParams->literalCompressionMode) {
    case ZSTD_ps_enable:
        return 0;
    case ZSTD_ps_disable:
        return 1;
    default:
        assert(0 /* impossible: pre-validated */);
        ZSTD_FALLTHROUGH;
    case ZSTD_ps_auto:
        return (cctxParams->cParams.strategy == ZSTD_fast) && (cctxParams->cParams.targetLength > 0);
    }
}

/*! ZSTD_safecopyLiterals() :
 *  memcpy() function that won't read beyond more than WILDCOPY_OVERLENGTH bytes past ilimit_w.
 *  Only called when the sequence ends past ilimit_w, so it only needs to be optimized for single
 *  large copies.
 */
static void
ZSTD_safecopyLiterals(BYTE* op, BYTE const* ip, BYTE const* const iend, BYTE const* ilimit_w)
{
    assert(iend > ilimit_w);
    if (ip <= ilimit_w) {
        ZSTD_wildcopy(op, ip, ilimit_w - ip, ZSTD_no_overlap);
        op += ilimit_w - ip;
        ip = ilimit_w;
    }
    while (ip < iend) *op++ = *ip++;
}

#define ZSTD_REP_MOVE     (ZSTD_REP_NUM-1)
#define STORE_REPCODE_1 STORE_REPCODE(1)
#define STORE_REPCODE_2 STORE_REPCODE(2)
#define STORE_REPCODE_3 STORE_REPCODE(3)
#define STORE_REPCODE(r) (assert((r)>=1), assert((r)<=3), (r)-1)
#define STORE_OFFSET(o)  (assert((o)>0), o + ZSTD_REP_MOVE)
#define STORED_IS_OFFSET(o)  ((o) > ZSTD_REP_MOVE)
#define STORED_IS_REPCODE(o) ((o) <= ZSTD_REP_MOVE)
#define STORED_OFFSET(o)  (assert(STORED_IS_OFFSET(o)), (o)-ZSTD_REP_MOVE)
#define STORED_REPCODE(o) (assert(STORED_IS_REPCODE(o)), (o)+1)  /* returns ID 1,2,3 */
#define STORED_TO_OFFBASE(o) ((o)+1)
#define OFFBASE_TO_STORED(o) ((o)-1)

/*! ZSTD_storeSeq() :
 *  Store a sequence (litlen, litPtr, offCode and matchLength) into seqStore_t.
 *  @offBase_minus1 : Users should use employ macros STORE_REPCODE_X and STORE_OFFSET().
 *  @matchLength : must be >= MINMATCH
 *  Allowed to overread literals up to litLimit.
*/
HINT_INLINE UNUSED_ATTR void
ZSTD_storeSeq(seqStore_t* seqStorePtr,
              size_t litLength, const BYTE* literals, const BYTE* litLimit,
              U32 offBase_minus1,
              size_t matchLength)
{
    BYTE const* const litLimit_w = litLimit - WILDCOPY_OVERLENGTH;
    BYTE const* const litEnd = literals + litLength;
#if defined(DEBUGLEVEL) && (DEBUGLEVEL >= 6)
    static const BYTE* g_start = NULL;
    if (g_start==NULL) g_start = (const BYTE*)literals;  /* note : index only works for compression within a single segment */
    {   U32 const pos = (U32)((const BYTE*)literals - g_start);
        DEBUGLOG(6, "Cpos%7u :%3u literals, match%4u bytes at offCode%7u",
               pos, (U32)litLength, (U32)matchLength, (U32)offBase_minus1);
    }
#endif
    assert((size_t)(seqStorePtr->sequences - seqStorePtr->sequencesStart) < seqStorePtr->maxNbSeq);
    /* copy Literals */
    assert(seqStorePtr->maxNbLit <= 128 KB);
    assert(seqStorePtr->lit + litLength <= seqStorePtr->litStart + seqStorePtr->maxNbLit);
    assert(literals + litLength <= litLimit);
    if (litEnd <= litLimit_w) {
        /* Common case we can use wildcopy.
	 * First copy 16 bytes, because literals are likely short.
	 */
        assert(WILDCOPY_OVERLENGTH >= 16);
        ZSTD_copy16(seqStorePtr->lit, literals);
        if (litLength > 16) {
            ZSTD_wildcopy(seqStorePtr->lit+16, literals+16, (ptrdiff_t)litLength-16, ZSTD_no_overlap);
        }
    } else {
        ZSTD_safecopyLiterals(seqStorePtr->lit, literals, litEnd, litLimit_w);
    }
    seqStorePtr->lit += litLength;

    /* literal Length */
    if (litLength>0xFFFF) {
        assert(seqStorePtr->longLengthType == ZSTD_llt_none); /* there can only be a single long length */
        seqStorePtr->longLengthType = ZSTD_llt_literalLength;
        seqStorePtr->longLengthPos = (U32)(seqStorePtr->sequences - seqStorePtr->sequencesStart);
    }
    seqStorePtr->sequences[0].litLength = (U16)litLength;

    /* match offset */
    seqStorePtr->sequences[0].offBase = STORED_TO_OFFBASE(offBase_minus1);

    /* match Length */
    assert(matchLength >= MINMATCH);
    {   size_t const mlBase = matchLength - MINMATCH;
        if (mlBase>0xFFFF) {
            assert(seqStorePtr->longLengthType == ZSTD_llt_none); /* there can only be a single long length */
            seqStorePtr->longLengthType = ZSTD_llt_matchLength;
            seqStorePtr->longLengthPos = (U32)(seqStorePtr->sequences - seqStorePtr->sequencesStart);
        }
        seqStorePtr->sequences[0].mlBase = (U16)mlBase;
    }

    seqStorePtr->sequences++;
}

/* ZSTD_updateRep() :
 * updates in-place @rep (array of repeat offsets)
 * @offBase_minus1 : sum-type, with same numeric representation as ZSTD_storeSeq()
 */
MEM_STATIC void
ZSTD_updateRep(U32 rep[ZSTD_REP_NUM], U32 const offBase_minus1, U32 const ll0)
{
    if (STORED_IS_OFFSET(offBase_minus1)) {  /* full offset */
        rep[2] = rep[1];
        rep[1] = rep[0];
        rep[0] = STORED_OFFSET(offBase_minus1);
    } else {   /* repcode */
        U32 const repCode = STORED_REPCODE(offBase_minus1) - 1 + ll0;
        if (repCode > 0) {  /* note : if repCode==0, no change */
            U32 const currentOffset = (repCode==ZSTD_REP_NUM) ? (rep[0] - 1) : rep[repCode];
            rep[2] = (repCode >= 2) ? rep[1] : rep[2];
            rep[1] = rep[0];
            rep[0] = currentOffset;
        } else {   /* repCode == 0 */
            /* nothing to do */
        }
    }
}

typedef struct repcodes_s {
    U32 rep[3];
} repcodes_t;

MEM_STATIC repcodes_t
ZSTD_newRep(U32 const rep[ZSTD_REP_NUM], U32 const offBase_minus1, U32 const ll0)
{
    repcodes_t newReps;
    ZSTD_memcpy(&newReps, rep, sizeof(newReps));
    ZSTD_updateRep(newReps.rep, offBase_minus1, ll0);
    return newReps;
}


/*-*************************************
*  Match length counter
***************************************/
static unsigned ZSTD_NbCommonBytes (size_t val)
{
    if (MEM_isLittleEndian()) {
        if (MEM_64bits()) {
#       if (__GNUC__ >= 4)
            return (__builtin_ctzll((U64)val) >> 3);
#       else
            static const int DeBruijnBytePos[64] = { 0, 0, 0, 0, 0, 1, 1, 2,
                                                     0, 3, 1, 3, 1, 4, 2, 7,
                                                     0, 2, 3, 6, 1, 5, 3, 5,
                                                     1, 3, 4, 4, 2, 5, 6, 7,
                                                     7, 0, 1, 2, 3, 3, 4, 6,
                                                     2, 6, 5, 5, 3, 4, 5, 6,
                                                     7, 1, 2, 4, 6, 4, 4, 5,
                                                     7, 2, 6, 5, 7, 6, 7, 7 };
            return DeBruijnBytePos[((U64)((val & -(long long)val) * 0x0218A392CDABBD3FULL)) >> 58];
#       endif
        } else { /* 32 bits */
#       if (__GNUC__ >= 3)
            return (__builtin_ctz((U32)val) >> 3);
#       else
            static const int DeBruijnBytePos[32] = { 0, 0, 3, 0, 3, 1, 3, 0,
                                                     3, 2, 2, 1, 3, 2, 0, 1,
                                                     3, 3, 1, 2, 2, 2, 2, 0,
                                                     3, 1, 2, 0, 1, 0, 1, 1 };
            return DeBruijnBytePos[((U32)((val & -(S32)val) * 0x077CB531U)) >> 27];
#       endif
        }
    } else {  /* Big Endian CPU */
        if (MEM_64bits()) {
#       if (__GNUC__ >= 4)
            return (__builtin_clzll(val) >> 3);
#       else
            unsigned r;
            const unsigned n32 = sizeof(size_t)*4;   /* calculate this way due to compiler complaining in 32-bits mode */
            if (!(val>>n32)) { r=4; } else { r=0; val>>=n32; }
            if (!(val>>16)) { r+=2; val>>=8; } else { val>>=24; }
            r += (!val);
            return r;
#       endif
        } else { /* 32 bits */
#       if (__GNUC__ >= 3)
            return (__builtin_clz((U32)val) >> 3);
#       else
            unsigned r;
            if (!(val>>16)) { r=2; val>>=8; } else { r=0; val>>=24; }
            r += (!val);
            return r;
#       endif
    }   }
}


MEM_STATIC size_t ZSTD_count(const BYTE* pIn, const BYTE* pMatch, const BYTE* const pInLimit)
{
    const BYTE* const pStart = pIn;
    const BYTE* const pInLoopLimit = pInLimit - (sizeof(size_t)-1);

    if (pIn < pInLoopLimit) {
        { size_t const diff = MEM_readST(pMatch) ^ MEM_readST(pIn);
          if (diff) return ZSTD_NbCommonBytes(diff); }
        pIn+=sizeof(size_t); pMatch+=sizeof(size_t);
        while (pIn < pInLoopLimit) {
            size_t const diff = MEM_readST(pMatch) ^ MEM_readST(pIn);
            if (!diff) { pIn+=sizeof(size_t); pMatch+=sizeof(size_t); continue; }
            pIn += ZSTD_NbCommonBytes(diff);
            return (size_t)(pIn - pStart);
    }   }
    if (MEM_64bits() && (pIn<(pInLimit-3)) && (MEM_read32(pMatch) == MEM_read32(pIn))) { pIn+=4; pMatch+=4; }
    if ((pIn<(pInLimit-1)) && (MEM_read16(pMatch) == MEM_read16(pIn))) { pIn+=2; pMatch+=2; }
    if ((pIn<pInLimit) && (*pMatch == *pIn)) pIn++;
    return (size_t)(pIn - pStart);
}

/* ZSTD_count_2segments() :
 *  can count match length with `ip` & `match` in 2 different segments.
 *  convention : on reaching mEnd, match count continue starting from iStart
 */
MEM_STATIC size_t
ZSTD_count_2segments(const BYTE* ip, const BYTE* match,
                     const BYTE* iEnd, const BYTE* mEnd, const BYTE* iStart)
{
    const BYTE* const vEnd = MIN( ip + (mEnd - match), iEnd);
    size_t const matchLength = ZSTD_count(ip, match, vEnd);
    if (match + matchLength != mEnd) return matchLength;
    DEBUGLOG(7, "ZSTD_count_2segments: found a 2-parts match (current length==%zu)", matchLength);
    DEBUGLOG(7, "distance from match beginning to end dictionary = %zi", mEnd - match);
    DEBUGLOG(7, "distance from current pos to end buffer = %zi", iEnd - ip);
    DEBUGLOG(7, "next byte : ip==%02X, istart==%02X", ip[matchLength], *iStart);
    DEBUGLOG(7, "final match length = %zu", matchLength + ZSTD_count(ip+matchLength, iStart, iEnd));
    return matchLength + ZSTD_count(ip+matchLength, iStart, iEnd);
}


/*-*************************************
 *  Hashes
 ***************************************/
static const U32 prime3bytes = 506832829U;
static U32    ZSTD_hash3(U32 u, U32 h) { return ((u << (32-24)) * prime3bytes)  >> (32-h) ; }
MEM_STATIC size_t ZSTD_hash3Ptr(const void* ptr, U32 h) { return ZSTD_hash3(MEM_readLE32(ptr), h); } /* only in zstd_opt.h */

static const U32 prime4bytes = 2654435761U;
static U32    ZSTD_hash4(U32 u, U32 h) { return (u * prime4bytes) >> (32-h) ; }
static size_t ZSTD_hash4Ptr(const void* ptr, U32 h) { return ZSTD_hash4(MEM_read32(ptr), h); }

static const U64 prime5bytes = 889523592379ULL;
static size_t ZSTD_hash5(U64 u, U32 h) { return (size_t)(((u  << (64-40)) * prime5bytes) >> (64-h)) ; }
static size_t ZSTD_hash5Ptr(const void* p, U32 h) { return ZSTD_hash5(MEM_readLE64(p), h); }

static const U64 prime6bytes = 227718039650203ULL;
static size_t ZSTD_hash6(U64 u, U32 h) { return (size_t)(((u  << (64-48)) * prime6bytes) >> (64-h)) ; }
static size_t ZSTD_hash6Ptr(const void* p, U32 h) { return ZSTD_hash6(MEM_readLE64(p), h); }

static const U64 prime7bytes = 58295818150454627ULL;
static size_t ZSTD_hash7(U64 u, U32 h) { return (size_t)(((u  << (64-56)) * prime7bytes) >> (64-h)) ; }
static size_t ZSTD_hash7Ptr(const void* p, U32 h) { return ZSTD_hash7(MEM_readLE64(p), h); }

static const U64 prime8bytes = 0xCF1BBCDCB7A56463ULL;
static size_t ZSTD_hash8(U64 u, U32 h) { return (size_t)(((u) * prime8bytes) >> (64-h)) ; }
static size_t ZSTD_hash8Ptr(const void* p, U32 h) { return ZSTD_hash8(MEM_readLE64(p), h); }

MEM_STATIC FORCE_INLINE_ATTR
size_t ZSTD_hashPtr(const void* p, U32 hBits, U32 mls)
{
    switch(mls)
    {
    default:
    case 4: return ZSTD_hash4Ptr(p, hBits);
    case 5: return ZSTD_hash5Ptr(p, hBits);
    case 6: return ZSTD_hash6Ptr(p, hBits);
    case 7: return ZSTD_hash7Ptr(p, hBits);
    case 8: return ZSTD_hash8Ptr(p, hBits);
    }
}

/* ZSTD_ipow() :
 * Return base^exponent.
 */
static U64 ZSTD_ipow(U64 base, U64 exponent)
{
    U64 power = 1;
    while (exponent) {
      if (exponent & 1) power *= base;
      exponent >>= 1;
      base *= base;
    }
    return power;
}

#define ZSTD_ROLL_HASH_CHAR_OFFSET 10

/* ZSTD_rollingHash_append() :
 * Add the buffer to the hash value.
 */
static U64 ZSTD_rollingHash_append(U64 hash, void const* buf, size_t size)
{
    BYTE const* istart = (BYTE const*)buf;
    size_t pos;
    for (pos = 0; pos < size; ++pos) {
        hash *= prime8bytes;
        hash += istart[pos] + ZSTD_ROLL_HASH_CHAR_OFFSET;
    }
    return hash;
}

/* ZSTD_rollingHash_compute() :
 * Compute the rolling hash value of the buffer.
 */
MEM_STATIC U64 ZSTD_rollingHash_compute(void const* buf, size_t size)
{
    return ZSTD_rollingHash_append(0, buf, size);
}

/* ZSTD_rollingHash_primePower() :
 * Compute the primePower to be passed to ZSTD_rollingHash_rotate() for a hash
 * over a window of length bytes.
 */
MEM_STATIC U64 ZSTD_rollingHash_primePower(U32 length)
{
    return ZSTD_ipow(prime8bytes, length - 1);
}

/* ZSTD_rollingHash_rotate() :
 * Rotate the rolling hash by one byte.
 */
MEM_STATIC U64 ZSTD_rollingHash_rotate(U64 hash, BYTE toRemove, BYTE toAdd, U64 primePower)
{
    hash -= (toRemove + ZSTD_ROLL_HASH_CHAR_OFFSET) * primePower;
    hash *= prime8bytes;
    hash += toAdd + ZSTD_ROLL_HASH_CHAR_OFFSET;
    return hash;
}

/*-*************************************
*  Round buffer management
***************************************/
#if (ZSTD_WINDOWLOG_MAX_64 > 31)
# error "ZSTD_WINDOWLOG_MAX is too large : would overflow ZSTD_CURRENT_MAX"
#endif
/* Max current allowed */
#define ZSTD_CURRENT_MAX ((3U << 29) + (1U << ZSTD_WINDOWLOG_MAX))
/* Maximum chunk size before overflow correction needs to be called again */
#define ZSTD_CHUNKSIZE_MAX                                                     \
    ( ((U32)-1)                  /* Maximum ending current index */            \
    - ZSTD_CURRENT_MAX)          /* Maximum beginning lowLimit */

/*
 * ZSTD_window_clear():
 * Clears the window containing the history by simply setting it to empty.
 */
MEM_STATIC void ZSTD_window_clear(ZSTD_window_t* window)
{
    size_t const endT = (size_t)(window->nextSrc - window->base);
    U32 const end = (U32)endT;

    window->lowLimit = end;
    window->dictLimit = end;
}

MEM_STATIC U32 ZSTD_window_isEmpty(ZSTD_window_t const window)
{
    return window.dictLimit == ZSTD_WINDOW_START_INDEX &&
           window.lowLimit == ZSTD_WINDOW_START_INDEX &&
           (window.nextSrc - window.base) == ZSTD_WINDOW_START_INDEX;
}

/*
 * ZSTD_window_hasExtDict():
 * Returns non-zero if the window has a non-empty extDict.
 */
MEM_STATIC U32 ZSTD_window_hasExtDict(ZSTD_window_t const window)
{
    return window.lowLimit < window.dictLimit;
}

/*
 * ZSTD_matchState_dictMode():
 * Inspects the provided matchState and figures out what dictMode should be
 * passed to the compressor.
 */
MEM_STATIC ZSTD_dictMode_e ZSTD_matchState_dictMode(const ZSTD_matchState_t *ms)
{
    return ZSTD_window_hasExtDict(ms->window) ?
        ZSTD_extDict :
        ms->dictMatchState != NULL ?
            (ms->dictMatchState->dedicatedDictSearch ? ZSTD_dedicatedDictSearch : ZSTD_dictMatchState) :
            ZSTD_noDict;
}

/* Defining this macro to non-zero tells zstd to run the overflow correction
 * code much more frequently. This is very inefficient, and should only be
 * used for tests and fuzzers.
 */
#ifndef ZSTD_WINDOW_OVERFLOW_CORRECT_FREQUENTLY
#  ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#    define ZSTD_WINDOW_OVERFLOW_CORRECT_FREQUENTLY 1
#  else
#    define ZSTD_WINDOW_OVERFLOW_CORRECT_FREQUENTLY 0
#  endif
#endif

/*
 * ZSTD_window_canOverflowCorrect():
 * Returns non-zero if the indices are large enough for overflow correction
 * to work correctly without impacting compression ratio.
 */
MEM_STATIC U32 ZSTD_window_canOverflowCorrect(ZSTD_window_t const window,
                                              U32 cycleLog,
                                              U32 maxDist,
                                              U32 loadedDictEnd,
                                              void const* src)
{
    U32 const cycleSize = 1u << cycleLog;
    U32 const curr = (U32)((BYTE const*)src - window.base);
    U32 const minIndexToOverflowCorrect = cycleSize
                                        + MAX(maxDist, cycleSize)
                                        + ZSTD_WINDOW_START_INDEX;

    /* Adjust the min index to backoff the overflow correction frequency,
     * so we don't waste too much CPU in overflow correction. If this
     * computation overflows we don't really care, we just need to make
     * sure it is at least minIndexToOverflowCorrect.
     */
    U32 const adjustment = window.nbOverflowCorrections + 1;
    U32 const adjustedIndex = MAX(minIndexToOverflowCorrect * adjustment,
                                  minIndexToOverflowCorrect);
    U32 const indexLargeEnough = curr > adjustedIndex;

    /* Only overflow correct early if the dictionary is invalidated already,
     * so we don't hurt compression ratio.
     */
    U32 const dictionaryInvalidated = curr > maxDist + loadedDictEnd;

    return indexLargeEnough && dictionaryInvalidated;
}

/*
 * ZSTD_window_needOverflowCorrection():
 * Returns non-zero if the indices are getting too large and need overflow
 * protection.
 */
MEM_STATIC U32 ZSTD_window_needOverflowCorrection(ZSTD_window_t const window,
                                                  U32 cycleLog,
                                                  U32 maxDist,
                                                  U32 loadedDictEnd,
                                                  void const* src,
                                                  void const* srcEnd)
{
    U32 const curr = (U32)((BYTE const*)srcEnd - window.base);
    if (ZSTD_WINDOW_OVERFLOW_CORRECT_FREQUENTLY) {
        if (ZSTD_window_canOverflowCorrect(window, cycleLog, maxDist, loadedDictEnd, src)) {
            return 1;
        }
    }
    return curr > ZSTD_CURRENT_MAX;
}

/*
 * ZSTD_window_correctOverflow():
 * Reduces the indices to protect from index overflow.
 * Returns the correction made to the indices, which must be applied to every
 * stored index.
 *
 * The least significant cycleLog bits of the indices must remain the same,
 * which may be 0. Every index up to maxDist in the past must be valid.
 */
MEM_STATIC U32 ZSTD_window_correctOverflow(ZSTD_window_t* window, U32 cycleLog,
                                           U32 maxDist, void const* src)
{
    /* preemptive overflow correction:
     * 1. correction is large enough:
     *    lowLimit > (3<<29) ==> current > 3<<29 + 1<<windowLog
     *    1<<windowLog <= newCurrent < 1<<chainLog + 1<<windowLog
     *
     *    current - newCurrent
     *    > (3<<29 + 1<<windowLog) - (1<<windowLog + 1<<chainLog)
     *    > (3<<29) - (1<<chainLog)
     *    > (3<<29) - (1<<30)             (NOTE: chainLog <= 30)
     *    > 1<<29
     *
     * 2. (ip+ZSTD_CHUNKSIZE_MAX - cctx->base) doesn't overflow:
     *    After correction, current is less than (1<<chainLog + 1<<windowLog).
     *    In 64-bit mode we are safe, because we have 64-bit ptrdiff_t.
     *    In 32-bit mode we are safe, because (chainLog <= 29), so
     *    ip+ZSTD_CHUNKSIZE_MAX - cctx->base < 1<<32.
     * 3. (cctx->lowLimit + 1<<windowLog) < 1<<32:
     *    windowLog <= 31 ==> 3<<29 + 1<<windowLog < 7<<29 < 1<<32.
     */
    U32 const cycleSize = 1u << cycleLog;
    U32 const cycleMask = cycleSize - 1;
    U32 const curr = (U32)((BYTE const*)src - window->base);
    U32 const currentCycle = curr & cycleMask;
    /* Ensure newCurrent - maxDist >= ZSTD_WINDOW_START_INDEX. */
    U32 const currentCycleCorrection = currentCycle < ZSTD_WINDOW_START_INDEX
                                     ? MAX(cycleSize, ZSTD_WINDOW_START_INDEX)
                                     : 0;
    U32 const newCurrent = currentCycle
                         + currentCycleCorrection
                         + MAX(maxDist, cycleSize);
    U32 const correction = curr - newCurrent;
    /* maxDist must be a power of two so that:
     *   (newCurrent & cycleMask) == (curr & cycleMask)
     * This is required to not corrupt the chains / binary tree.
     */
    assert((maxDist & (maxDist - 1)) == 0);
    assert((curr & cycleMask) == (newCurrent & cycleMask));
    assert(curr > newCurrent);
    if (!ZSTD_WINDOW_OVERFLOW_CORRECT_FREQUENTLY) {
        /* Loose bound, should be around 1<<29 (see above) */
        assert(correction > 1<<28);
    }

    window->base += correction;
    window->dictBase += correction;
    if (window->lowLimit < correction + ZSTD_WINDOW_START_INDEX) {
        window->lowLimit = ZSTD_WINDOW_START_INDEX;
    } else {
        window->lowLimit -= correction;
    }
    if (window->dictLimit < correction + ZSTD_WINDOW_START_INDEX) {
        window->dictLimit = ZSTD_WINDOW_START_INDEX;
    } else {
        window->dictLimit -= correction;
    }

    /* Ensure we can still reference the full window. */
    assert(newCurrent >= maxDist);
    assert(newCurrent - maxDist >= ZSTD_WINDOW_START_INDEX);
    /* Ensure that lowLimit and dictLimit didn't underflow. */
    assert(window->lowLimit <= newCurrent);
    assert(window->dictLimit <= newCurrent);

    ++window->nbOverflowCorrections;

    DEBUGLOG(4, "Correction of 0x%x bytes to lowLimit=0x%x", correction,
             window->lowLimit);
    return correction;
}

/*
 * ZSTD_window_enforceMaxDist():
 * Updates lowLimit so that:
 *    (srcEnd - base) - lowLimit == maxDist + loadedDictEnd
 *
 * It ensures index is valid as long as index >= lowLimit.
 * This must be called before a block compression call.
 *
 * loadedDictEnd is only defined if a dictionary is in use for current compression.
 * As the name implies, loadedDictEnd represents the index at end of dictionary.
 * The value lies within context's referential, it can be directly compared to blockEndIdx.
 *
 * If loadedDictEndPtr is NULL, no dictionary is in use, and we use loadedDictEnd == 0.
 * If loadedDictEndPtr is not NULL, we set it to zero after updating lowLimit.
 * This is because dictionaries are allowed to be referenced fully
 * as long as the last byte of the dictionary is in the window.
 * Once input has progressed beyond window size, dictionary cannot be referenced anymore.
 *
 * In normal dict mode, the dictionary lies between lowLimit and dictLimit.
 * In dictMatchState mode, lowLimit and dictLimit are the same,
 * and the dictionary is below them.
 * forceWindow and dictMatchState are therefore incompatible.
 */
MEM_STATIC void
ZSTD_window_enforceMaxDist(ZSTD_window_t* window,
                     const void* blockEnd,
                           U32   maxDist,
                           U32*  loadedDictEndPtr,
                     const ZSTD_matchState_t** dictMatchStatePtr)
{
    U32 const blockEndIdx = (U32)((BYTE const*)blockEnd - window->base);
    U32 const loadedDictEnd = (loadedDictEndPtr != NULL) ? *loadedDictEndPtr : 0;
    DEBUGLOG(5, "ZSTD_window_enforceMaxDist: blockEndIdx=%u, maxDist=%u, loadedDictEnd=%u",
                (unsigned)blockEndIdx, (unsigned)maxDist, (unsigned)loadedDictEnd);

    /* - When there is no dictionary : loadedDictEnd == 0.
         In which case, the test (blockEndIdx > maxDist) is merely to avoid
         overflowing next operation `newLowLimit = blockEndIdx - maxDist`.
       - When there is a standard dictionary :
         Index referential is copied from the dictionary,
         which means it starts from 0.
         In which case, loadedDictEnd == dictSize,
         and it makes sense to compare `blockEndIdx > maxDist + dictSize`
         since `blockEndIdx` also starts from zero.
       - When there is an attached dictionary :
         loadedDictEnd is expressed within the referential of the context,
         so it can be directly compared against blockEndIdx.
    */
    if (blockEndIdx > maxDist + loadedDictEnd) {
        U32 const newLowLimit = blockEndIdx - maxDist;
        if (window->lowLimit < newLowLimit) window->lowLimit = newLowLimit;
        if (window->dictLimit < window->lowLimit) {
            DEBUGLOG(5, "Update dictLimit to match lowLimit, from %u to %u",
                        (unsigned)window->dictLimit, (unsigned)window->lowLimit);
            window->dictLimit = window->lowLimit;
        }
        /* On reaching window size, dictionaries are invalidated */
        if (loadedDictEndPtr) *loadedDictEndPtr = 0;
        if (dictMatchStatePtr) *dictMatchStatePtr = NULL;
    }
}

/* Similar to ZSTD_window_enforceMaxDist(),
 * but only invalidates dictionary
 * when input progresses beyond window size.
 * assumption : loadedDictEndPtr and dictMatchStatePtr are valid (non NULL)
 *              loadedDictEnd uses same referential as window->base
 *              maxDist is the window size */
MEM_STATIC void
ZSTD_checkDictValidity(const ZSTD_window_t* window,
                       const void* blockEnd,
                             U32   maxDist,
                             U32*  loadedDictEndPtr,
                       const ZSTD_matchState_t** dictMatchStatePtr)
{
    assert(loadedDictEndPtr != NULL);
    assert(dictMatchStatePtr != NULL);
    {   U32 const blockEndIdx = (U32)((BYTE const*)blockEnd - window->base);
        U32 const loadedDictEnd = *loadedDictEndPtr;
        DEBUGLOG(5, "ZSTD_checkDictValidity: blockEndIdx=%u, maxDist=%u, loadedDictEnd=%u",
                    (unsigned)blockEndIdx, (unsigned)maxDist, (unsigned)loadedDictEnd);
        assert(blockEndIdx >= loadedDictEnd);

        if (blockEndIdx > loadedDictEnd + maxDist) {
            /* On reaching window size, dictionaries are invalidated.
             * For simplification, if window size is reached anywhere within next block,
             * the dictionary is invalidated for the full block.
             */
            DEBUGLOG(6, "invalidating dictionary for current block (distance > windowSize)");
            *loadedDictEndPtr = 0;
            *dictMatchStatePtr = NULL;
        } else {
            if (*loadedDictEndPtr != 0) {
                DEBUGLOG(6, "dictionary considered valid for current block");
    }   }   }
}

MEM_STATIC void ZSTD_window_init(ZSTD_window_t* window) {
    ZSTD_memset(window, 0, sizeof(*window));
    window->base = (BYTE const*)" ";
    window->dictBase = (BYTE const*)" ";
    ZSTD_STATIC_ASSERT(ZSTD_DUBT_UNSORTED_MARK < ZSTD_WINDOW_START_INDEX); /* Start above ZSTD_DUBT_UNSORTED_MARK */
    window->dictLimit = ZSTD_WINDOW_START_INDEX;    /* start from >0, so that 1st position is valid */
    window->lowLimit = ZSTD_WINDOW_START_INDEX;     /* it ensures first and later CCtx usages compress the same */
    window->nextSrc = window->base + ZSTD_WINDOW_START_INDEX;   /* see issue #1241 */
    window->nbOverflowCorrections = 0;
}

/*
 * ZSTD_window_update():
 * Updates the window by appending [src, src + srcSize) to the window.
 * If it is not contiguous, the current prefix becomes the extDict, and we
 * forget about the extDict. Handles overlap of the prefix and extDict.
 * Returns non-zero if the segment is contiguous.
 */
MEM_STATIC U32 ZSTD_window_update(ZSTD_window_t* window,
                                  void const* src, size_t srcSize,
                                  int forceNonContiguous)
{
    BYTE const* const ip = (BYTE const*)src;
    U32 contiguous = 1;
    DEBUGLOG(5, "ZSTD_window_update");
    if (srcSize == 0)
        return contiguous;
    assert(window->base != NULL);
    assert(window->dictBase != NULL);
    /* Check if blocks follow each other */
    if (src != window->nextSrc || forceNonContiguous) {
        /* not contiguous */
        size_t const distanceFromBase = (size_t)(window->nextSrc - window->base);
        DEBUGLOG(5, "Non contiguous blocks, new segment starts at %u", window->dictLimit);
        window->lowLimit = window->dictLimit;
        assert(distanceFromBase == (size_t)(U32)distanceFromBase);  /* should never overflow */
        window->dictLimit = (U32)distanceFromBase;
        window->dictBase = window->base;
        window->base = ip - distanceFromBase;
        /* ms->nextToUpdate = window->dictLimit; */
        if (window->dictLimit - window->lowLimit < HASH_READ_SIZE) window->lowLimit = window->dictLimit;   /* too small extDict */
        contiguous = 0;
    }
    window->nextSrc = ip + srcSize;
    /* if input and dictionary overlap : reduce dictionary (area presumed modified by input) */
    if ( (ip+srcSize > window->dictBase + window->lowLimit)
       & (ip < window->dictBase + window->dictLimit)) {
        ptrdiff_t const highInputIdx = (ip + srcSize) - window->dictBase;
        U32 const lowLimitMax = (highInputIdx > (ptrdiff_t)window->dictLimit) ? window->dictLimit : (U32)highInputIdx;
        window->lowLimit = lowLimitMax;
        DEBUGLOG(5, "Overlapping extDict and input : new lowLimit = %u", window->lowLimit);
    }
    return contiguous;
}

/*
 * Returns the lowest allowed match index. It may either be in the ext-dict or the prefix.
 */
MEM_STATIC U32 ZSTD_getLowestMatchIndex(const ZSTD_matchState_t* ms, U32 curr, unsigned windowLog)
{
    U32 const maxDistance = 1U << windowLog;
    U32 const lowestValid = ms->window.lowLimit;
    U32 const withinWindow = (curr - lowestValid > maxDistance) ? curr - maxDistance : lowestValid;
    U32 const isDictionary = (ms->loadedDictEnd != 0);
    /* When using a dictionary the entire dictionary is valid if a single byte of the dictionary
     * is within the window. We invalidate the dictionary (and set loadedDictEnd to 0) when it isn't
     * valid for the entire block. So this check is sufficient to find the lowest valid match index.
     */
    U32 const matchLowest = isDictionary ? lowestValid : withinWindow;
    return matchLowest;
}

/*
 * Returns the lowest allowed match index in the prefix.
 */
MEM_STATIC U32 ZSTD_getLowestPrefixIndex(const ZSTD_matchState_t* ms, U32 curr, unsigned windowLog)
{
    U32    const maxDistance = 1U << windowLog;
    U32    const lowestValid = ms->window.dictLimit;
    U32    const withinWindow = (curr - lowestValid > maxDistance) ? curr - maxDistance : lowestValid;
    U32    const isDictionary = (ms->loadedDictEnd != 0);
    /* When computing the lowest prefix index we need to take the dictionary into account to handle
     * the edge case where the dictionary and the source are contiguous in memory.
     */
    U32    const matchLowest = isDictionary ? lowestValid : withinWindow;
    return matchLowest;
}



/* debug functions */
#if (DEBUGLEVEL>=2)

MEM_STATIC double ZSTD_fWeight(U32 rawStat)
{
    U32 const fp_accuracy = 8;
    U32 const fp_multiplier = (1 << fp_accuracy);
    U32 const newStat = rawStat + 1;
    U32 const hb = ZSTD_highbit32(newStat);
    U32 const BWeight = hb * fp_multiplier;
    U32 const FWeight = (newStat << fp_accuracy) >> hb;
    U32 const weight = BWeight + FWeight;
    assert(hb + fp_accuracy < 31);
    return (double)weight / fp_multiplier;
}

/* display a table content,
 * listing each element, its frequency, and its predicted bit cost */
MEM_STATIC void ZSTD_debugTable(const U32* table, U32 max)
{
    unsigned u, sum;
    for (u=0, sum=0; u<=max; u++) sum += table[u];
    DEBUGLOG(2, "total nb elts: %u", sum);
    for (u=0; u<=max; u++) {
        DEBUGLOG(2, "%2u: %5u  (%.2f)",
                u, table[u], ZSTD_fWeight(sum) - ZSTD_fWeight(table[u]) );
    }
}

#endif



/* ===============================================================
 * Shared internal declarations
 * These prototypes may be called from sources not in lib/compress
 * =============================================================== */

/* ZSTD_loadCEntropy() :
 * dict : must point at beginning of a valid zstd dictionary.
 * return : size of dictionary header (size of magic number + dict ID + entropy tables)
 * assumptions : magic number supposed already checked
 *               and dictSize >= 8 */
size_t ZSTD_loadCEntropy(ZSTD_compressedBlockState_t* bs, void* workspace,
                         const void* const dict, size_t dictSize);

void ZSTD_reset_compressedBlockState(ZSTD_compressedBlockState_t* bs);

/* ==============================================================
 * Private declarations
 * These prototypes shall only be called from within lib/compress
 * ============================================================== */

/* ZSTD_getCParamsFromCCtxParams() :
 * cParams are built depending on compressionLevel, src size hints,
 * LDM and manually set compression parameters.
 * Note: srcSizeHint == 0 means 0!
 */
ZSTD_compressionParameters ZSTD_getCParamsFromCCtxParams(
        const ZSTD_CCtx_params* CCtxParams, U64 srcSizeHint, size_t dictSize, ZSTD_cParamMode_e mode);

/*! ZSTD_initCStream_internal() :
 *  Private use only. Init streaming operation.
 *  expects params to be valid.
 *  must receive dict, or cdict, or none, but not both.
 *  @return : 0, or an error code */
size_t ZSTD_initCStream_internal(ZSTD_CStream* zcs,
                     const void* dict, size_t dictSize,
                     const ZSTD_CDict* cdict,
                     const ZSTD_CCtx_params* params, unsigned long long pledgedSrcSize);

void ZSTD_resetSeqStore(seqStore_t* ssPtr);

/*! ZSTD_getCParamsFromCDict() :
 *  as the name implies */
ZSTD_compressionParameters ZSTD_getCParamsFromCDict(const ZSTD_CDict* cdict);

/* ZSTD_compressBegin_advanced_internal() :
 * Private use only. To be called from zstdmt_compress.c. */
size_t ZSTD_compressBegin_advanced_internal(ZSTD_CCtx* cctx,
                                    const void* dict, size_t dictSize,
                                    ZSTD_dictContentType_e dictContentType,
                                    ZSTD_dictTableLoadMethod_e dtlm,
                                    const ZSTD_CDict* cdict,
                                    const ZSTD_CCtx_params* params,
                                    unsigned long long pledgedSrcSize);

/* ZSTD_compress_advanced_internal() :
 * Private use only. To be called from zstdmt_compress.c. */
size_t ZSTD_compress_advanced_internal(ZSTD_CCtx* cctx,
                                       void* dst, size_t dstCapacity,
                                 const void* src, size_t srcSize,
                                 const void* dict,size_t dictSize,
                                 const ZSTD_CCtx_params* params);


/* ZSTD_writeLastEmptyBlock() :
 * output an empty Block with end-of-frame mark to complete a frame
 * @return : size of data written into `dst` (== ZSTD_blockHeaderSize (defined in zstd_internal.h))
 *           or an error code if `dstCapacity` is too small (<ZSTD_blockHeaderSize)
 */
size_t ZSTD_writeLastEmptyBlock(void* dst, size_t dstCapacity);


/* ZSTD_referenceExternalSequences() :
 * Must be called before starting a compression operation.
 * seqs must parse a prefix of the source.
 * This cannot be used when long range matching is enabled.
 * Zstd will use these sequences, and pass the literals to a secondary block
 * compressor.
 * @return : An error code on failure.
 * NOTE: seqs are not verified! Invalid sequences can cause out-of-bounds memory
 * access and data corruption.
 */
size_t ZSTD_referenceExternalSequences(ZSTD_CCtx* cctx, rawSeq* seq, size_t nbSeq);

/* ZSTD_cycleLog() :
 *  condition for correct operation : hashLog > 1 */
U32 ZSTD_cycleLog(U32 hashLog, ZSTD_strategy strat);

/* ZSTD_CCtx_trace() :
 *  Trace the end of a compression call.
 */
void ZSTD_CCtx_trace(ZSTD_CCtx* cctx, size_t extraCSize);

#endif /* ZSTD_COMPRESS_H */
