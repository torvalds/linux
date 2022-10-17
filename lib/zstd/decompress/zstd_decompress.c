/*
 * Copyright (c) Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/* ***************************************************************
*  Tuning parameters
*****************************************************************/
/*!
 * HEAPMODE :
 * Select how default decompression function ZSTD_decompress() allocates its context,
 * on stack (0), or into heap (1, default; requires malloc()).
 * Note that functions with explicit context such as ZSTD_decompressDCtx() are unaffected.
 */
#ifndef ZSTD_HEAPMODE
#  define ZSTD_HEAPMODE 1
#endif

/*!
*  LEGACY_SUPPORT :
*  if set to 1+, ZSTD_decompress() can decode older formats (v0.1+)
*/

/*!
 *  MAXWINDOWSIZE_DEFAULT :
 *  maximum window size accepted by DStream __by default__.
 *  Frames requiring more memory will be rejected.
 *  It's possible to set a different limit using ZSTD_DCtx_setMaxWindowSize().
 */
#ifndef ZSTD_MAXWINDOWSIZE_DEFAULT
#  define ZSTD_MAXWINDOWSIZE_DEFAULT (((U32)1 << ZSTD_WINDOWLOG_LIMIT_DEFAULT) + 1)
#endif

/*!
 *  NO_FORWARD_PROGRESS_MAX :
 *  maximum allowed nb of calls to ZSTD_decompressStream()
 *  without any forward progress
 *  (defined as: no byte read from input, and no byte flushed to output)
 *  before triggering an error.
 */
#ifndef ZSTD_NO_FORWARD_PROGRESS_MAX
#  define ZSTD_NO_FORWARD_PROGRESS_MAX 16
#endif


/*-*******************************************************
*  Dependencies
*********************************************************/
#include "../common/zstd_deps.h"   /* ZSTD_memcpy, ZSTD_memmove, ZSTD_memset */
#include "../common/cpu.h"         /* bmi2 */
#include "../common/mem.h"         /* low level memory routines */
#define FSE_STATIC_LINKING_ONLY
#include "../common/fse.h"
#define HUF_STATIC_LINKING_ONLY
#include "../common/huf.h"
#include <linux/xxhash.h> /* xxh64_reset, xxh64_update, xxh64_digest, XXH64 */
#include "../common/zstd_internal.h"  /* blockProperties_t */
#include "zstd_decompress_internal.h"   /* ZSTD_DCtx */
#include "zstd_ddict.h"  /* ZSTD_DDictDictContent */
#include "zstd_decompress_block.h"   /* ZSTD_decompressBlock_internal */




/* ***********************************
 * Multiple DDicts Hashset internals *
 *************************************/

#define DDICT_HASHSET_MAX_LOAD_FACTOR_COUNT_MULT 4
#define DDICT_HASHSET_MAX_LOAD_FACTOR_SIZE_MULT 3   /* These two constants represent SIZE_MULT/COUNT_MULT load factor without using a float.
                                                     * Currently, that means a 0.75 load factor.
                                                     * So, if count * COUNT_MULT / size * SIZE_MULT != 0, then we've exceeded
                                                     * the load factor of the ddict hash set.
                                                     */

#define DDICT_HASHSET_TABLE_BASE_SIZE 64
#define DDICT_HASHSET_RESIZE_FACTOR 2

/* Hash function to determine starting position of dict insertion within the table
 * Returns an index between [0, hashSet->ddictPtrTableSize]
 */
static size_t ZSTD_DDictHashSet_getIndex(const ZSTD_DDictHashSet* hashSet, U32 dictID) {
    const U64 hash = xxh64(&dictID, sizeof(U32), 0);
    /* DDict ptr table size is a multiple of 2, use size - 1 as mask to get index within [0, hashSet->ddictPtrTableSize) */
    return hash & (hashSet->ddictPtrTableSize - 1);
}

/* Adds DDict to a hashset without resizing it.
 * If inserting a DDict with a dictID that already exists in the set, replaces the one in the set.
 * Returns 0 if successful, or a zstd error code if something went wrong.
 */
static size_t ZSTD_DDictHashSet_emplaceDDict(ZSTD_DDictHashSet* hashSet, const ZSTD_DDict* ddict) {
    const U32 dictID = ZSTD_getDictID_fromDDict(ddict);
    size_t idx = ZSTD_DDictHashSet_getIndex(hashSet, dictID);
    const size_t idxRangeMask = hashSet->ddictPtrTableSize - 1;
    RETURN_ERROR_IF(hashSet->ddictPtrCount == hashSet->ddictPtrTableSize, GENERIC, "Hash set is full!");
    DEBUGLOG(4, "Hashed index: for dictID: %u is %zu", dictID, idx);
    while (hashSet->ddictPtrTable[idx] != NULL) {
        /* Replace existing ddict if inserting ddict with same dictID */
        if (ZSTD_getDictID_fromDDict(hashSet->ddictPtrTable[idx]) == dictID) {
            DEBUGLOG(4, "DictID already exists, replacing rather than adding");
            hashSet->ddictPtrTable[idx] = ddict;
            return 0;
        }
        idx &= idxRangeMask;
        idx++;
    }
    DEBUGLOG(4, "Final idx after probing for dictID %u is: %zu", dictID, idx);
    hashSet->ddictPtrTable[idx] = ddict;
    hashSet->ddictPtrCount++;
    return 0;
}

/* Expands hash table by factor of DDICT_HASHSET_RESIZE_FACTOR and
 * rehashes all values, allocates new table, frees old table.
 * Returns 0 on success, otherwise a zstd error code.
 */
static size_t ZSTD_DDictHashSet_expand(ZSTD_DDictHashSet* hashSet, ZSTD_customMem customMem) {
    size_t newTableSize = hashSet->ddictPtrTableSize * DDICT_HASHSET_RESIZE_FACTOR;
    const ZSTD_DDict** newTable = (const ZSTD_DDict**)ZSTD_customCalloc(sizeof(ZSTD_DDict*) * newTableSize, customMem);
    const ZSTD_DDict** oldTable = hashSet->ddictPtrTable;
    size_t oldTableSize = hashSet->ddictPtrTableSize;
    size_t i;

    DEBUGLOG(4, "Expanding DDict hash table! Old size: %zu new size: %zu", oldTableSize, newTableSize);
    RETURN_ERROR_IF(!newTable, memory_allocation, "Expanded hashset allocation failed!");
    hashSet->ddictPtrTable = newTable;
    hashSet->ddictPtrTableSize = newTableSize;
    hashSet->ddictPtrCount = 0;
    for (i = 0; i < oldTableSize; ++i) {
        if (oldTable[i] != NULL) {
            FORWARD_IF_ERROR(ZSTD_DDictHashSet_emplaceDDict(hashSet, oldTable[i]), "");
        }
    }
    ZSTD_customFree((void*)oldTable, customMem);
    DEBUGLOG(4, "Finished re-hash");
    return 0;
}

/* Fetches a DDict with the given dictID
 * Returns the ZSTD_DDict* with the requested dictID. If it doesn't exist, then returns NULL.
 */
static const ZSTD_DDict* ZSTD_DDictHashSet_getDDict(ZSTD_DDictHashSet* hashSet, U32 dictID) {
    size_t idx = ZSTD_DDictHashSet_getIndex(hashSet, dictID);
    const size_t idxRangeMask = hashSet->ddictPtrTableSize - 1;
    DEBUGLOG(4, "Hashed index: for dictID: %u is %zu", dictID, idx);
    for (;;) {
        size_t currDictID = ZSTD_getDictID_fromDDict(hashSet->ddictPtrTable[idx]);
        if (currDictID == dictID || currDictID == 0) {
            /* currDictID == 0 implies a NULL ddict entry */
            break;
        } else {
            idx &= idxRangeMask;    /* Goes to start of table when we reach the end */
            idx++;
        }
    }
    DEBUGLOG(4, "Final idx after probing for dictID %u is: %zu", dictID, idx);
    return hashSet->ddictPtrTable[idx];
}

/* Allocates space for and returns a ddict hash set
 * The hash set's ZSTD_DDict* table has all values automatically set to NULL to begin with.
 * Returns NULL if allocation failed.
 */
static ZSTD_DDictHashSet* ZSTD_createDDictHashSet(ZSTD_customMem customMem) {
    ZSTD_DDictHashSet* ret = (ZSTD_DDictHashSet*)ZSTD_customMalloc(sizeof(ZSTD_DDictHashSet), customMem);
    DEBUGLOG(4, "Allocating new hash set");
    if (!ret)
        return NULL;
    ret->ddictPtrTable = (const ZSTD_DDict**)ZSTD_customCalloc(DDICT_HASHSET_TABLE_BASE_SIZE * sizeof(ZSTD_DDict*), customMem);
    if (!ret->ddictPtrTable) {
        ZSTD_customFree(ret, customMem);
        return NULL;
    }
    ret->ddictPtrTableSize = DDICT_HASHSET_TABLE_BASE_SIZE;
    ret->ddictPtrCount = 0;
    return ret;
}

/* Frees the table of ZSTD_DDict* within a hashset, then frees the hashset itself.
 * Note: The ZSTD_DDict* within the table are NOT freed.
 */
static void ZSTD_freeDDictHashSet(ZSTD_DDictHashSet* hashSet, ZSTD_customMem customMem) {
    DEBUGLOG(4, "Freeing ddict hash set");
    if (hashSet && hashSet->ddictPtrTable) {
        ZSTD_customFree((void*)hashSet->ddictPtrTable, customMem);
    }
    if (hashSet) {
        ZSTD_customFree(hashSet, customMem);
    }
}

/* Public function: Adds a DDict into the ZSTD_DDictHashSet, possibly triggering a resize of the hash set.
 * Returns 0 on success, or a ZSTD error.
 */
static size_t ZSTD_DDictHashSet_addDDict(ZSTD_DDictHashSet* hashSet, const ZSTD_DDict* ddict, ZSTD_customMem customMem) {
    DEBUGLOG(4, "Adding dict ID: %u to hashset with - Count: %zu Tablesize: %zu", ZSTD_getDictID_fromDDict(ddict), hashSet->ddictPtrCount, hashSet->ddictPtrTableSize);
    if (hashSet->ddictPtrCount * DDICT_HASHSET_MAX_LOAD_FACTOR_COUNT_MULT / hashSet->ddictPtrTableSize * DDICT_HASHSET_MAX_LOAD_FACTOR_SIZE_MULT != 0) {
        FORWARD_IF_ERROR(ZSTD_DDictHashSet_expand(hashSet, customMem), "");
    }
    FORWARD_IF_ERROR(ZSTD_DDictHashSet_emplaceDDict(hashSet, ddict), "");
    return 0;
}

/*-*************************************************************
*   Context management
***************************************************************/
size_t ZSTD_sizeof_DCtx (const ZSTD_DCtx* dctx)
{
    if (dctx==NULL) return 0;   /* support sizeof NULL */
    return sizeof(*dctx)
           + ZSTD_sizeof_DDict(dctx->ddictLocal)
           + dctx->inBuffSize + dctx->outBuffSize;
}

size_t ZSTD_estimateDCtxSize(void) { return sizeof(ZSTD_DCtx); }


static size_t ZSTD_startingInputLength(ZSTD_format_e format)
{
    size_t const startingInputLength = ZSTD_FRAMEHEADERSIZE_PREFIX(format);
    /* only supports formats ZSTD_f_zstd1 and ZSTD_f_zstd1_magicless */
    assert( (format == ZSTD_f_zstd1) || (format == ZSTD_f_zstd1_magicless) );
    return startingInputLength;
}

static void ZSTD_DCtx_resetParameters(ZSTD_DCtx* dctx)
{
    assert(dctx->streamStage == zdss_init);
    dctx->format = ZSTD_f_zstd1;
    dctx->maxWindowSize = ZSTD_MAXWINDOWSIZE_DEFAULT;
    dctx->outBufferMode = ZSTD_bm_buffered;
    dctx->forceIgnoreChecksum = ZSTD_d_validateChecksum;
    dctx->refMultipleDDicts = ZSTD_rmd_refSingleDDict;
}

static void ZSTD_initDCtx_internal(ZSTD_DCtx* dctx)
{
    dctx->staticSize  = 0;
    dctx->ddict       = NULL;
    dctx->ddictLocal  = NULL;
    dctx->dictEnd     = NULL;
    dctx->ddictIsCold = 0;
    dctx->dictUses = ZSTD_dont_use;
    dctx->inBuff      = NULL;
    dctx->inBuffSize  = 0;
    dctx->outBuffSize = 0;
    dctx->streamStage = zdss_init;
    dctx->legacyContext = NULL;
    dctx->previousLegacyVersion = 0;
    dctx->noForwardProgress = 0;
    dctx->oversizedDuration = 0;
    dctx->bmi2 = ZSTD_cpuid_bmi2(ZSTD_cpuid());
    dctx->ddictSet = NULL;
    ZSTD_DCtx_resetParameters(dctx);
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    dctx->dictContentEndForFuzzing = NULL;
#endif
}

ZSTD_DCtx* ZSTD_initStaticDCtx(void *workspace, size_t workspaceSize)
{
    ZSTD_DCtx* const dctx = (ZSTD_DCtx*) workspace;

    if ((size_t)workspace & 7) return NULL;  /* 8-aligned */
    if (workspaceSize < sizeof(ZSTD_DCtx)) return NULL;  /* minimum size */

    ZSTD_initDCtx_internal(dctx);
    dctx->staticSize = workspaceSize;
    dctx->inBuff = (char*)(dctx+1);
    return dctx;
}

ZSTD_DCtx* ZSTD_createDCtx_advanced(ZSTD_customMem customMem)
{
    if ((!customMem.customAlloc) ^ (!customMem.customFree)) return NULL;

    {   ZSTD_DCtx* const dctx = (ZSTD_DCtx*)ZSTD_customMalloc(sizeof(*dctx), customMem);
        if (!dctx) return NULL;
        dctx->customMem = customMem;
        ZSTD_initDCtx_internal(dctx);
        return dctx;
    }
}

ZSTD_DCtx* ZSTD_createDCtx(void)
{
    DEBUGLOG(3, "ZSTD_createDCtx");
    return ZSTD_createDCtx_advanced(ZSTD_defaultCMem);
}

static void ZSTD_clearDict(ZSTD_DCtx* dctx)
{
    ZSTD_freeDDict(dctx->ddictLocal);
    dctx->ddictLocal = NULL;
    dctx->ddict = NULL;
    dctx->dictUses = ZSTD_dont_use;
}

size_t ZSTD_freeDCtx(ZSTD_DCtx* dctx)
{
    if (dctx==NULL) return 0;   /* support free on NULL */
    RETURN_ERROR_IF(dctx->staticSize, memory_allocation, "not compatible with static DCtx");
    {   ZSTD_customMem const cMem = dctx->customMem;
        ZSTD_clearDict(dctx);
        ZSTD_customFree(dctx->inBuff, cMem);
        dctx->inBuff = NULL;
        if (dctx->ddictSet) {
            ZSTD_freeDDictHashSet(dctx->ddictSet, cMem);
            dctx->ddictSet = NULL;
        }
        ZSTD_customFree(dctx, cMem);
        return 0;
    }
}

/* no longer useful */
void ZSTD_copyDCtx(ZSTD_DCtx* dstDCtx, const ZSTD_DCtx* srcDCtx)
{
    size_t const toCopy = (size_t)((char*)(&dstDCtx->inBuff) - (char*)dstDCtx);
    ZSTD_memcpy(dstDCtx, srcDCtx, toCopy);  /* no need to copy workspace */
}

/* Given a dctx with a digested frame params, re-selects the correct ZSTD_DDict based on
 * the requested dict ID from the frame. If there exists a reference to the correct ZSTD_DDict, then
 * accordingly sets the ddict to be used to decompress the frame.
 *
 * If no DDict is found, then no action is taken, and the ZSTD_DCtx::ddict remains as-is.
 *
 * ZSTD_d_refMultipleDDicts must be enabled for this function to be called.
 */
static void ZSTD_DCtx_selectFrameDDict(ZSTD_DCtx* dctx) {
    assert(dctx->refMultipleDDicts && dctx->ddictSet);
    DEBUGLOG(4, "Adjusting DDict based on requested dict ID from frame");
    if (dctx->ddict) {
        const ZSTD_DDict* frameDDict = ZSTD_DDictHashSet_getDDict(dctx->ddictSet, dctx->fParams.dictID);
        if (frameDDict) {
            DEBUGLOG(4, "DDict found!");
            ZSTD_clearDict(dctx);
            dctx->dictID = dctx->fParams.dictID;
            dctx->ddict = frameDDict;
            dctx->dictUses = ZSTD_use_indefinitely;
        }
    }
}


/*-*************************************************************
 *   Frame header decoding
 ***************************************************************/

/*! ZSTD_isFrame() :
 *  Tells if the content of `buffer` starts with a valid Frame Identifier.
 *  Note : Frame Identifier is 4 bytes. If `size < 4`, @return will always be 0.
 *  Note 2 : Legacy Frame Identifiers are considered valid only if Legacy Support is enabled.
 *  Note 3 : Skippable Frame Identifiers are considered valid. */
unsigned ZSTD_isFrame(const void* buffer, size_t size)
{
    if (size < ZSTD_FRAMEIDSIZE) return 0;
    {   U32 const magic = MEM_readLE32(buffer);
        if (magic == ZSTD_MAGICNUMBER) return 1;
        if ((magic & ZSTD_MAGIC_SKIPPABLE_MASK) == ZSTD_MAGIC_SKIPPABLE_START) return 1;
    }
    return 0;
}

/* ZSTD_frameHeaderSize_internal() :
 *  srcSize must be large enough to reach header size fields.
 *  note : only works for formats ZSTD_f_zstd1 and ZSTD_f_zstd1_magicless.
 * @return : size of the Frame Header
 *           or an error code, which can be tested with ZSTD_isError() */
static size_t ZSTD_frameHeaderSize_internal(const void* src, size_t srcSize, ZSTD_format_e format)
{
    size_t const minInputSize = ZSTD_startingInputLength(format);
    RETURN_ERROR_IF(srcSize < minInputSize, srcSize_wrong, "");

    {   BYTE const fhd = ((const BYTE*)src)[minInputSize-1];
        U32 const dictID= fhd & 3;
        U32 const singleSegment = (fhd >> 5) & 1;
        U32 const fcsId = fhd >> 6;
        return minInputSize + !singleSegment
             + ZSTD_did_fieldSize[dictID] + ZSTD_fcs_fieldSize[fcsId]
             + (singleSegment && !fcsId);
    }
}

/* ZSTD_frameHeaderSize() :
 *  srcSize must be >= ZSTD_frameHeaderSize_prefix.
 * @return : size of the Frame Header,
 *           or an error code (if srcSize is too small) */
size_t ZSTD_frameHeaderSize(const void* src, size_t srcSize)
{
    return ZSTD_frameHeaderSize_internal(src, srcSize, ZSTD_f_zstd1);
}


/* ZSTD_getFrameHeader_advanced() :
 *  decode Frame Header, or require larger `srcSize`.
 *  note : only works for formats ZSTD_f_zstd1 and ZSTD_f_zstd1_magicless
 * @return : 0, `zfhPtr` is correctly filled,
 *          >0, `srcSize` is too small, value is wanted `srcSize` amount,
 *           or an error code, which can be tested using ZSTD_isError() */
size_t ZSTD_getFrameHeader_advanced(ZSTD_frameHeader* zfhPtr, const void* src, size_t srcSize, ZSTD_format_e format)
{
    const BYTE* ip = (const BYTE*)src;
    size_t const minInputSize = ZSTD_startingInputLength(format);

    ZSTD_memset(zfhPtr, 0, sizeof(*zfhPtr));   /* not strictly necessary, but static analyzer do not understand that zfhPtr is only going to be read only if return value is zero, since they are 2 different signals */
    if (srcSize < minInputSize) return minInputSize;
    RETURN_ERROR_IF(src==NULL, GENERIC, "invalid parameter");

    if ( (format != ZSTD_f_zstd1_magicless)
      && (MEM_readLE32(src) != ZSTD_MAGICNUMBER) ) {
        if ((MEM_readLE32(src) & ZSTD_MAGIC_SKIPPABLE_MASK) == ZSTD_MAGIC_SKIPPABLE_START) {
            /* skippable frame */
            if (srcSize < ZSTD_SKIPPABLEHEADERSIZE)
                return ZSTD_SKIPPABLEHEADERSIZE; /* magic number + frame length */
            ZSTD_memset(zfhPtr, 0, sizeof(*zfhPtr));
            zfhPtr->frameContentSize = MEM_readLE32((const char *)src + ZSTD_FRAMEIDSIZE);
            zfhPtr->frameType = ZSTD_skippableFrame;
            return 0;
        }
        RETURN_ERROR(prefix_unknown, "");
    }

    /* ensure there is enough `srcSize` to fully read/decode frame header */
    {   size_t const fhsize = ZSTD_frameHeaderSize_internal(src, srcSize, format);
        if (srcSize < fhsize) return fhsize;
        zfhPtr->headerSize = (U32)fhsize;
    }

    {   BYTE const fhdByte = ip[minInputSize-1];
        size_t pos = minInputSize;
        U32 const dictIDSizeCode = fhdByte&3;
        U32 const checksumFlag = (fhdByte>>2)&1;
        U32 const singleSegment = (fhdByte>>5)&1;
        U32 const fcsID = fhdByte>>6;
        U64 windowSize = 0;
        U32 dictID = 0;
        U64 frameContentSize = ZSTD_CONTENTSIZE_UNKNOWN;
        RETURN_ERROR_IF((fhdByte & 0x08) != 0, frameParameter_unsupported,
                        "reserved bits, must be zero");

        if (!singleSegment) {
            BYTE const wlByte = ip[pos++];
            U32 const windowLog = (wlByte >> 3) + ZSTD_WINDOWLOG_ABSOLUTEMIN;
            RETURN_ERROR_IF(windowLog > ZSTD_WINDOWLOG_MAX, frameParameter_windowTooLarge, "");
            windowSize = (1ULL << windowLog);
            windowSize += (windowSize >> 3) * (wlByte&7);
        }
        switch(dictIDSizeCode)
        {
            default:
                assert(0);  /* impossible */
                ZSTD_FALLTHROUGH;
            case 0 : break;
            case 1 : dictID = ip[pos]; pos++; break;
            case 2 : dictID = MEM_readLE16(ip+pos); pos+=2; break;
            case 3 : dictID = MEM_readLE32(ip+pos); pos+=4; break;
        }
        switch(fcsID)
        {
            default:
                assert(0);  /* impossible */
                ZSTD_FALLTHROUGH;
            case 0 : if (singleSegment) frameContentSize = ip[pos]; break;
            case 1 : frameContentSize = MEM_readLE16(ip+pos)+256; break;
            case 2 : frameContentSize = MEM_readLE32(ip+pos); break;
            case 3 : frameContentSize = MEM_readLE64(ip+pos); break;
        }
        if (singleSegment) windowSize = frameContentSize;

        zfhPtr->frameType = ZSTD_frame;
        zfhPtr->frameContentSize = frameContentSize;
        zfhPtr->windowSize = windowSize;
        zfhPtr->blockSizeMax = (unsigned) MIN(windowSize, ZSTD_BLOCKSIZE_MAX);
        zfhPtr->dictID = dictID;
        zfhPtr->checksumFlag = checksumFlag;
    }
    return 0;
}

/* ZSTD_getFrameHeader() :
 *  decode Frame Header, or require larger `srcSize`.
 *  note : this function does not consume input, it only reads it.
 * @return : 0, `zfhPtr` is correctly filled,
 *          >0, `srcSize` is too small, value is wanted `srcSize` amount,
 *           or an error code, which can be tested using ZSTD_isError() */
size_t ZSTD_getFrameHeader(ZSTD_frameHeader* zfhPtr, const void* src, size_t srcSize)
{
    return ZSTD_getFrameHeader_advanced(zfhPtr, src, srcSize, ZSTD_f_zstd1);
}


/* ZSTD_getFrameContentSize() :
 *  compatible with legacy mode
 * @return : decompressed size of the single frame pointed to be `src` if known, otherwise
 *         - ZSTD_CONTENTSIZE_UNKNOWN if the size cannot be determined
 *         - ZSTD_CONTENTSIZE_ERROR if an error occurred (e.g. invalid magic number, srcSize too small) */
unsigned long long ZSTD_getFrameContentSize(const void *src, size_t srcSize)
{
    {   ZSTD_frameHeader zfh;
        if (ZSTD_getFrameHeader(&zfh, src, srcSize) != 0)
            return ZSTD_CONTENTSIZE_ERROR;
        if (zfh.frameType == ZSTD_skippableFrame) {
            return 0;
        } else {
            return zfh.frameContentSize;
    }   }
}

static size_t readSkippableFrameSize(void const* src, size_t srcSize)
{
    size_t const skippableHeaderSize = ZSTD_SKIPPABLEHEADERSIZE;
    U32 sizeU32;

    RETURN_ERROR_IF(srcSize < ZSTD_SKIPPABLEHEADERSIZE, srcSize_wrong, "");

    sizeU32 = MEM_readLE32((BYTE const*)src + ZSTD_FRAMEIDSIZE);
    RETURN_ERROR_IF((U32)(sizeU32 + ZSTD_SKIPPABLEHEADERSIZE) < sizeU32,
                    frameParameter_unsupported, "");
    {
        size_t const skippableSize = skippableHeaderSize + sizeU32;
        RETURN_ERROR_IF(skippableSize > srcSize, srcSize_wrong, "");
        return skippableSize;
    }
}

/* ZSTD_findDecompressedSize() :
 *  compatible with legacy mode
 *  `srcSize` must be the exact length of some number of ZSTD compressed and/or
 *      skippable frames
 *  @return : decompressed size of the frames contained */
unsigned long long ZSTD_findDecompressedSize(const void* src, size_t srcSize)
{
    unsigned long long totalDstSize = 0;

    while (srcSize >= ZSTD_startingInputLength(ZSTD_f_zstd1)) {
        U32 const magicNumber = MEM_readLE32(src);

        if ((magicNumber & ZSTD_MAGIC_SKIPPABLE_MASK) == ZSTD_MAGIC_SKIPPABLE_START) {
            size_t const skippableSize = readSkippableFrameSize(src, srcSize);
            if (ZSTD_isError(skippableSize)) {
                return ZSTD_CONTENTSIZE_ERROR;
            }
            assert(skippableSize <= srcSize);

            src = (const BYTE *)src + skippableSize;
            srcSize -= skippableSize;
            continue;
        }

        {   unsigned long long const ret = ZSTD_getFrameContentSize(src, srcSize);
            if (ret >= ZSTD_CONTENTSIZE_ERROR) return ret;

            /* check for overflow */
            if (totalDstSize + ret < totalDstSize) return ZSTD_CONTENTSIZE_ERROR;
            totalDstSize += ret;
        }
        {   size_t const frameSrcSize = ZSTD_findFrameCompressedSize(src, srcSize);
            if (ZSTD_isError(frameSrcSize)) {
                return ZSTD_CONTENTSIZE_ERROR;
            }

            src = (const BYTE *)src + frameSrcSize;
            srcSize -= frameSrcSize;
        }
    }  /* while (srcSize >= ZSTD_frameHeaderSize_prefix) */

    if (srcSize) return ZSTD_CONTENTSIZE_ERROR;

    return totalDstSize;
}

/* ZSTD_getDecompressedSize() :
 *  compatible with legacy mode
 * @return : decompressed size if known, 0 otherwise
             note : 0 can mean any of the following :
                   - frame content is empty
                   - decompressed size field is not present in frame header
                   - frame header unknown / not supported
                   - frame header not complete (`srcSize` too small) */
unsigned long long ZSTD_getDecompressedSize(const void* src, size_t srcSize)
{
    unsigned long long const ret = ZSTD_getFrameContentSize(src, srcSize);
    ZSTD_STATIC_ASSERT(ZSTD_CONTENTSIZE_ERROR < ZSTD_CONTENTSIZE_UNKNOWN);
    return (ret >= ZSTD_CONTENTSIZE_ERROR) ? 0 : ret;
}


/* ZSTD_decodeFrameHeader() :
 * `headerSize` must be the size provided by ZSTD_frameHeaderSize().
 * If multiple DDict references are enabled, also will choose the correct DDict to use.
 * @return : 0 if success, or an error code, which can be tested using ZSTD_isError() */
static size_t ZSTD_decodeFrameHeader(ZSTD_DCtx* dctx, const void* src, size_t headerSize)
{
    size_t const result = ZSTD_getFrameHeader_advanced(&(dctx->fParams), src, headerSize, dctx->format);
    if (ZSTD_isError(result)) return result;    /* invalid header */
    RETURN_ERROR_IF(result>0, srcSize_wrong, "headerSize too small");

    /* Reference DDict requested by frame if dctx references multiple ddicts */
    if (dctx->refMultipleDDicts == ZSTD_rmd_refMultipleDDicts && dctx->ddictSet) {
        ZSTD_DCtx_selectFrameDDict(dctx);
    }

#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    /* Skip the dictID check in fuzzing mode, because it makes the search
     * harder.
     */
    RETURN_ERROR_IF(dctx->fParams.dictID && (dctx->dictID != dctx->fParams.dictID),
                    dictionary_wrong, "");
#endif
    dctx->validateChecksum = (dctx->fParams.checksumFlag && !dctx->forceIgnoreChecksum) ? 1 : 0;
    if (dctx->validateChecksum) xxh64_reset(&dctx->xxhState, 0);
    dctx->processedCSize += headerSize;
    return 0;
}

static ZSTD_frameSizeInfo ZSTD_errorFrameSizeInfo(size_t ret)
{
    ZSTD_frameSizeInfo frameSizeInfo;
    frameSizeInfo.compressedSize = ret;
    frameSizeInfo.decompressedBound = ZSTD_CONTENTSIZE_ERROR;
    return frameSizeInfo;
}

static ZSTD_frameSizeInfo ZSTD_findFrameSizeInfo(const void* src, size_t srcSize)
{
    ZSTD_frameSizeInfo frameSizeInfo;
    ZSTD_memset(&frameSizeInfo, 0, sizeof(ZSTD_frameSizeInfo));


    if ((srcSize >= ZSTD_SKIPPABLEHEADERSIZE)
        && (MEM_readLE32(src) & ZSTD_MAGIC_SKIPPABLE_MASK) == ZSTD_MAGIC_SKIPPABLE_START) {
        frameSizeInfo.compressedSize = readSkippableFrameSize(src, srcSize);
        assert(ZSTD_isError(frameSizeInfo.compressedSize) ||
               frameSizeInfo.compressedSize <= srcSize);
        return frameSizeInfo;
    } else {
        const BYTE* ip = (const BYTE*)src;
        const BYTE* const ipstart = ip;
        size_t remainingSize = srcSize;
        size_t nbBlocks = 0;
        ZSTD_frameHeader zfh;

        /* Extract Frame Header */
        {   size_t const ret = ZSTD_getFrameHeader(&zfh, src, srcSize);
            if (ZSTD_isError(ret))
                return ZSTD_errorFrameSizeInfo(ret);
            if (ret > 0)
                return ZSTD_errorFrameSizeInfo(ERROR(srcSize_wrong));
        }

        ip += zfh.headerSize;
        remainingSize -= zfh.headerSize;

        /* Iterate over each block */
        while (1) {
            blockProperties_t blockProperties;
            size_t const cBlockSize = ZSTD_getcBlockSize(ip, remainingSize, &blockProperties);
            if (ZSTD_isError(cBlockSize))
                return ZSTD_errorFrameSizeInfo(cBlockSize);

            if (ZSTD_blockHeaderSize + cBlockSize > remainingSize)
                return ZSTD_errorFrameSizeInfo(ERROR(srcSize_wrong));

            ip += ZSTD_blockHeaderSize + cBlockSize;
            remainingSize -= ZSTD_blockHeaderSize + cBlockSize;
            nbBlocks++;

            if (blockProperties.lastBlock) break;
        }

        /* Final frame content checksum */
        if (zfh.checksumFlag) {
            if (remainingSize < 4)
                return ZSTD_errorFrameSizeInfo(ERROR(srcSize_wrong));
            ip += 4;
        }

        frameSizeInfo.compressedSize = (size_t)(ip - ipstart);
        frameSizeInfo.decompressedBound = (zfh.frameContentSize != ZSTD_CONTENTSIZE_UNKNOWN)
                                        ? zfh.frameContentSize
                                        : nbBlocks * zfh.blockSizeMax;
        return frameSizeInfo;
    }
}

/* ZSTD_findFrameCompressedSize() :
 *  compatible with legacy mode
 *  `src` must point to the start of a ZSTD frame, ZSTD legacy frame, or skippable frame
 *  `srcSize` must be at least as large as the frame contained
 *  @return : the compressed size of the frame starting at `src` */
size_t ZSTD_findFrameCompressedSize(const void *src, size_t srcSize)
{
    ZSTD_frameSizeInfo const frameSizeInfo = ZSTD_findFrameSizeInfo(src, srcSize);
    return frameSizeInfo.compressedSize;
}

/* ZSTD_decompressBound() :
 *  compatible with legacy mode
 *  `src` must point to the start of a ZSTD frame or a skippeable frame
 *  `srcSize` must be at least as large as the frame contained
 *  @return : the maximum decompressed size of the compressed source
 */
unsigned long long ZSTD_decompressBound(const void* src, size_t srcSize)
{
    unsigned long long bound = 0;
    /* Iterate over each frame */
    while (srcSize > 0) {
        ZSTD_frameSizeInfo const frameSizeInfo = ZSTD_findFrameSizeInfo(src, srcSize);
        size_t const compressedSize = frameSizeInfo.compressedSize;
        unsigned long long const decompressedBound = frameSizeInfo.decompressedBound;
        if (ZSTD_isError(compressedSize) || decompressedBound == ZSTD_CONTENTSIZE_ERROR)
            return ZSTD_CONTENTSIZE_ERROR;
        assert(srcSize >= compressedSize);
        src = (const BYTE*)src + compressedSize;
        srcSize -= compressedSize;
        bound += decompressedBound;
    }
    return bound;
}


/*-*************************************************************
 *   Frame decoding
 ***************************************************************/

/* ZSTD_insertBlock() :
 *  insert `src` block into `dctx` history. Useful to track uncompressed blocks. */
size_t ZSTD_insertBlock(ZSTD_DCtx* dctx, const void* blockStart, size_t blockSize)
{
    DEBUGLOG(5, "ZSTD_insertBlock: %u bytes", (unsigned)blockSize);
    ZSTD_checkContinuity(dctx, blockStart, blockSize);
    dctx->previousDstEnd = (const char*)blockStart + blockSize;
    return blockSize;
}


static size_t ZSTD_copyRawBlock(void* dst, size_t dstCapacity,
                          const void* src, size_t srcSize)
{
    DEBUGLOG(5, "ZSTD_copyRawBlock");
    RETURN_ERROR_IF(srcSize > dstCapacity, dstSize_tooSmall, "");
    if (dst == NULL) {
        if (srcSize == 0) return 0;
        RETURN_ERROR(dstBuffer_null, "");
    }
    ZSTD_memcpy(dst, src, srcSize);
    return srcSize;
}

static size_t ZSTD_setRleBlock(void* dst, size_t dstCapacity,
                               BYTE b,
                               size_t regenSize)
{
    RETURN_ERROR_IF(regenSize > dstCapacity, dstSize_tooSmall, "");
    if (dst == NULL) {
        if (regenSize == 0) return 0;
        RETURN_ERROR(dstBuffer_null, "");
    }
    ZSTD_memset(dst, b, regenSize);
    return regenSize;
}

static void ZSTD_DCtx_trace_end(ZSTD_DCtx const* dctx, U64 uncompressedSize, U64 compressedSize, unsigned streaming)
{
    (void)dctx;
    (void)uncompressedSize;
    (void)compressedSize;
    (void)streaming;
}


/*! ZSTD_decompressFrame() :
 * @dctx must be properly initialized
 *  will update *srcPtr and *srcSizePtr,
 *  to make *srcPtr progress by one frame. */
static size_t ZSTD_decompressFrame(ZSTD_DCtx* dctx,
                                   void* dst, size_t dstCapacity,
                             const void** srcPtr, size_t *srcSizePtr)
{
    const BYTE* const istart = (const BYTE*)(*srcPtr);
    const BYTE* ip = istart;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = dstCapacity != 0 ? ostart + dstCapacity : ostart;
    BYTE* op = ostart;
    size_t remainingSrcSize = *srcSizePtr;

    DEBUGLOG(4, "ZSTD_decompressFrame (srcSize:%i)", (int)*srcSizePtr);

    /* check */
    RETURN_ERROR_IF(
        remainingSrcSize < ZSTD_FRAMEHEADERSIZE_MIN(dctx->format)+ZSTD_blockHeaderSize,
        srcSize_wrong, "");

    /* Frame Header */
    {   size_t const frameHeaderSize = ZSTD_frameHeaderSize_internal(
                ip, ZSTD_FRAMEHEADERSIZE_PREFIX(dctx->format), dctx->format);
        if (ZSTD_isError(frameHeaderSize)) return frameHeaderSize;
        RETURN_ERROR_IF(remainingSrcSize < frameHeaderSize+ZSTD_blockHeaderSize,
                        srcSize_wrong, "");
        FORWARD_IF_ERROR( ZSTD_decodeFrameHeader(dctx, ip, frameHeaderSize) , "");
        ip += frameHeaderSize; remainingSrcSize -= frameHeaderSize;
    }

    /* Loop on each block */
    while (1) {
        size_t decodedSize;
        blockProperties_t blockProperties;
        size_t const cBlockSize = ZSTD_getcBlockSize(ip, remainingSrcSize, &blockProperties);
        if (ZSTD_isError(cBlockSize)) return cBlockSize;

        ip += ZSTD_blockHeaderSize;
        remainingSrcSize -= ZSTD_blockHeaderSize;
        RETURN_ERROR_IF(cBlockSize > remainingSrcSize, srcSize_wrong, "");

        switch(blockProperties.blockType)
        {
        case bt_compressed:
            decodedSize = ZSTD_decompressBlock_internal(dctx, op, (size_t)(oend-op), ip, cBlockSize, /* frame */ 1);
            break;
        case bt_raw :
            decodedSize = ZSTD_copyRawBlock(op, (size_t)(oend-op), ip, cBlockSize);
            break;
        case bt_rle :
            decodedSize = ZSTD_setRleBlock(op, (size_t)(oend-op), *ip, blockProperties.origSize);
            break;
        case bt_reserved :
        default:
            RETURN_ERROR(corruption_detected, "invalid block type");
        }

        if (ZSTD_isError(decodedSize)) return decodedSize;
        if (dctx->validateChecksum)
            xxh64_update(&dctx->xxhState, op, decodedSize);
        if (decodedSize != 0)
            op += decodedSize;
        assert(ip != NULL);
        ip += cBlockSize;
        remainingSrcSize -= cBlockSize;
        if (blockProperties.lastBlock) break;
    }

    if (dctx->fParams.frameContentSize != ZSTD_CONTENTSIZE_UNKNOWN) {
        RETURN_ERROR_IF((U64)(op-ostart) != dctx->fParams.frameContentSize,
                        corruption_detected, "");
    }
    if (dctx->fParams.checksumFlag) { /* Frame content checksum verification */
        RETURN_ERROR_IF(remainingSrcSize<4, checksum_wrong, "");
        if (!dctx->forceIgnoreChecksum) {
            U32 const checkCalc = (U32)xxh64_digest(&dctx->xxhState);
            U32 checkRead;
            checkRead = MEM_readLE32(ip);
            RETURN_ERROR_IF(checkRead != checkCalc, checksum_wrong, "");
        }
        ip += 4;
        remainingSrcSize -= 4;
    }
    ZSTD_DCtx_trace_end(dctx, (U64)(op-ostart), (U64)(ip-istart), /* streaming */ 0);
    /* Allow caller to get size read */
    *srcPtr = ip;
    *srcSizePtr = remainingSrcSize;
    return (size_t)(op-ostart);
}

static size_t ZSTD_decompressMultiFrame(ZSTD_DCtx* dctx,
                                        void* dst, size_t dstCapacity,
                                  const void* src, size_t srcSize,
                                  const void* dict, size_t dictSize,
                                  const ZSTD_DDict* ddict)
{
    void* const dststart = dst;
    int moreThan1Frame = 0;

    DEBUGLOG(5, "ZSTD_decompressMultiFrame");
    assert(dict==NULL || ddict==NULL);  /* either dict or ddict set, not both */

    if (ddict) {
        dict = ZSTD_DDict_dictContent(ddict);
        dictSize = ZSTD_DDict_dictSize(ddict);
    }

    while (srcSize >= ZSTD_startingInputLength(dctx->format)) {


        {   U32 const magicNumber = MEM_readLE32(src);
            DEBUGLOG(4, "reading magic number %08X (expecting %08X)",
                        (unsigned)magicNumber, ZSTD_MAGICNUMBER);
            if ((magicNumber & ZSTD_MAGIC_SKIPPABLE_MASK) == ZSTD_MAGIC_SKIPPABLE_START) {
                size_t const skippableSize = readSkippableFrameSize(src, srcSize);
                FORWARD_IF_ERROR(skippableSize, "readSkippableFrameSize failed");
                assert(skippableSize <= srcSize);

                src = (const BYTE *)src + skippableSize;
                srcSize -= skippableSize;
                continue;
        }   }

        if (ddict) {
            /* we were called from ZSTD_decompress_usingDDict */
            FORWARD_IF_ERROR(ZSTD_decompressBegin_usingDDict(dctx, ddict), "");
        } else {
            /* this will initialize correctly with no dict if dict == NULL, so
             * use this in all cases but ddict */
            FORWARD_IF_ERROR(ZSTD_decompressBegin_usingDict(dctx, dict, dictSize), "");
        }
        ZSTD_checkContinuity(dctx, dst, dstCapacity);

        {   const size_t res = ZSTD_decompressFrame(dctx, dst, dstCapacity,
                                                    &src, &srcSize);
            RETURN_ERROR_IF(
                (ZSTD_getErrorCode(res) == ZSTD_error_prefix_unknown)
             && (moreThan1Frame==1),
                srcSize_wrong,
                "At least one frame successfully completed, "
                "but following bytes are garbage: "
                "it's more likely to be a srcSize error, "
                "specifying more input bytes than size of frame(s). "
                "Note: one could be unlucky, it might be a corruption error instead, "
                "happening right at the place where we expect zstd magic bytes. "
                "But this is _much_ less likely than a srcSize field error.");
            if (ZSTD_isError(res)) return res;
            assert(res <= dstCapacity);
            if (res != 0)
                dst = (BYTE*)dst + res;
            dstCapacity -= res;
        }
        moreThan1Frame = 1;
    }  /* while (srcSize >= ZSTD_frameHeaderSize_prefix) */

    RETURN_ERROR_IF(srcSize, srcSize_wrong, "input not entirely consumed");

    return (size_t)((BYTE*)dst - (BYTE*)dststart);
}

size_t ZSTD_decompress_usingDict(ZSTD_DCtx* dctx,
                                 void* dst, size_t dstCapacity,
                           const void* src, size_t srcSize,
                           const void* dict, size_t dictSize)
{
    return ZSTD_decompressMultiFrame(dctx, dst, dstCapacity, src, srcSize, dict, dictSize, NULL);
}


static ZSTD_DDict const* ZSTD_getDDict(ZSTD_DCtx* dctx)
{
    switch (dctx->dictUses) {
    default:
        assert(0 /* Impossible */);
        ZSTD_FALLTHROUGH;
    case ZSTD_dont_use:
        ZSTD_clearDict(dctx);
        return NULL;
    case ZSTD_use_indefinitely:
        return dctx->ddict;
    case ZSTD_use_once:
        dctx->dictUses = ZSTD_dont_use;
        return dctx->ddict;
    }
}

size_t ZSTD_decompressDCtx(ZSTD_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    return ZSTD_decompress_usingDDict(dctx, dst, dstCapacity, src, srcSize, ZSTD_getDDict(dctx));
}


size_t ZSTD_decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
#if defined(ZSTD_HEAPMODE) && (ZSTD_HEAPMODE>=1)
    size_t regenSize;
    ZSTD_DCtx* const dctx = ZSTD_createDCtx();
    RETURN_ERROR_IF(dctx==NULL, memory_allocation, "NULL pointer!");
    regenSize = ZSTD_decompressDCtx(dctx, dst, dstCapacity, src, srcSize);
    ZSTD_freeDCtx(dctx);
    return regenSize;
#else   /* stack mode */
    ZSTD_DCtx dctx;
    ZSTD_initDCtx_internal(&dctx);
    return ZSTD_decompressDCtx(&dctx, dst, dstCapacity, src, srcSize);
#endif
}


/*-**************************************
*   Advanced Streaming Decompression API
*   Bufferless and synchronous
****************************************/
size_t ZSTD_nextSrcSizeToDecompress(ZSTD_DCtx* dctx) { return dctx->expected; }

/*
 * Similar to ZSTD_nextSrcSizeToDecompress(), but when a block input can be streamed,
 * we allow taking a partial block as the input. Currently only raw uncompressed blocks can
 * be streamed.
 *
 * For blocks that can be streamed, this allows us to reduce the latency until we produce
 * output, and avoid copying the input.
 *
 * @param inputSize - The total amount of input that the caller currently has.
 */
static size_t ZSTD_nextSrcSizeToDecompressWithInputSize(ZSTD_DCtx* dctx, size_t inputSize) {
    if (!(dctx->stage == ZSTDds_decompressBlock || dctx->stage == ZSTDds_decompressLastBlock))
        return dctx->expected;
    if (dctx->bType != bt_raw)
        return dctx->expected;
    return MIN(MAX(inputSize, 1), dctx->expected);
}

ZSTD_nextInputType_e ZSTD_nextInputType(ZSTD_DCtx* dctx) {
    switch(dctx->stage)
    {
    default:   /* should not happen */
        assert(0);
        ZSTD_FALLTHROUGH;
    case ZSTDds_getFrameHeaderSize:
        ZSTD_FALLTHROUGH;
    case ZSTDds_decodeFrameHeader:
        return ZSTDnit_frameHeader;
    case ZSTDds_decodeBlockHeader:
        return ZSTDnit_blockHeader;
    case ZSTDds_decompressBlock:
        return ZSTDnit_block;
    case ZSTDds_decompressLastBlock:
        return ZSTDnit_lastBlock;
    case ZSTDds_checkChecksum:
        return ZSTDnit_checksum;
    case ZSTDds_decodeSkippableHeader:
        ZSTD_FALLTHROUGH;
    case ZSTDds_skipFrame:
        return ZSTDnit_skippableFrame;
    }
}

static int ZSTD_isSkipFrame(ZSTD_DCtx* dctx) { return dctx->stage == ZSTDds_skipFrame; }

/* ZSTD_decompressContinue() :
 *  srcSize : must be the exact nb of bytes expected (see ZSTD_nextSrcSizeToDecompress())
 *  @return : nb of bytes generated into `dst` (necessarily <= `dstCapacity)
 *            or an error code, which can be tested using ZSTD_isError() */
size_t ZSTD_decompressContinue(ZSTD_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    DEBUGLOG(5, "ZSTD_decompressContinue (srcSize:%u)", (unsigned)srcSize);
    /* Sanity check */
    RETURN_ERROR_IF(srcSize != ZSTD_nextSrcSizeToDecompressWithInputSize(dctx, srcSize), srcSize_wrong, "not allowed");
    ZSTD_checkContinuity(dctx, dst, dstCapacity);

    dctx->processedCSize += srcSize;

    switch (dctx->stage)
    {
    case ZSTDds_getFrameHeaderSize :
        assert(src != NULL);
        if (dctx->format == ZSTD_f_zstd1) {  /* allows header */
            assert(srcSize >= ZSTD_FRAMEIDSIZE);  /* to read skippable magic number */
            if ((MEM_readLE32(src) & ZSTD_MAGIC_SKIPPABLE_MASK) == ZSTD_MAGIC_SKIPPABLE_START) {        /* skippable frame */
                ZSTD_memcpy(dctx->headerBuffer, src, srcSize);
                dctx->expected = ZSTD_SKIPPABLEHEADERSIZE - srcSize;  /* remaining to load to get full skippable frame header */
                dctx->stage = ZSTDds_decodeSkippableHeader;
                return 0;
        }   }
        dctx->headerSize = ZSTD_frameHeaderSize_internal(src, srcSize, dctx->format);
        if (ZSTD_isError(dctx->headerSize)) return dctx->headerSize;
        ZSTD_memcpy(dctx->headerBuffer, src, srcSize);
        dctx->expected = dctx->headerSize - srcSize;
        dctx->stage = ZSTDds_decodeFrameHeader;
        return 0;

    case ZSTDds_decodeFrameHeader:
        assert(src != NULL);
        ZSTD_memcpy(dctx->headerBuffer + (dctx->headerSize - srcSize), src, srcSize);
        FORWARD_IF_ERROR(ZSTD_decodeFrameHeader(dctx, dctx->headerBuffer, dctx->headerSize), "");
        dctx->expected = ZSTD_blockHeaderSize;
        dctx->stage = ZSTDds_decodeBlockHeader;
        return 0;

    case ZSTDds_decodeBlockHeader:
        {   blockProperties_t bp;
            size_t const cBlockSize = ZSTD_getcBlockSize(src, ZSTD_blockHeaderSize, &bp);
            if (ZSTD_isError(cBlockSize)) return cBlockSize;
            RETURN_ERROR_IF(cBlockSize > dctx->fParams.blockSizeMax, corruption_detected, "Block Size Exceeds Maximum");
            dctx->expected = cBlockSize;
            dctx->bType = bp.blockType;
            dctx->rleSize = bp.origSize;
            if (cBlockSize) {
                dctx->stage = bp.lastBlock ? ZSTDds_decompressLastBlock : ZSTDds_decompressBlock;
                return 0;
            }
            /* empty block */
            if (bp.lastBlock) {
                if (dctx->fParams.checksumFlag) {
                    dctx->expected = 4;
                    dctx->stage = ZSTDds_checkChecksum;
                } else {
                    dctx->expected = 0; /* end of frame */
                    dctx->stage = ZSTDds_getFrameHeaderSize;
                }
            } else {
                dctx->expected = ZSTD_blockHeaderSize;  /* jump to next header */
                dctx->stage = ZSTDds_decodeBlockHeader;
            }
            return 0;
        }

    case ZSTDds_decompressLastBlock:
    case ZSTDds_decompressBlock:
        DEBUGLOG(5, "ZSTD_decompressContinue: case ZSTDds_decompressBlock");
        {   size_t rSize;
            switch(dctx->bType)
            {
            case bt_compressed:
                DEBUGLOG(5, "ZSTD_decompressContinue: case bt_compressed");
                rSize = ZSTD_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize, /* frame */ 1);
                dctx->expected = 0;  /* Streaming not supported */
                break;
            case bt_raw :
                assert(srcSize <= dctx->expected);
                rSize = ZSTD_copyRawBlock(dst, dstCapacity, src, srcSize);
                FORWARD_IF_ERROR(rSize, "ZSTD_copyRawBlock failed");
                assert(rSize == srcSize);
                dctx->expected -= rSize;
                break;
            case bt_rle :
                rSize = ZSTD_setRleBlock(dst, dstCapacity, *(const BYTE*)src, dctx->rleSize);
                dctx->expected = 0;  /* Streaming not supported */
                break;
            case bt_reserved :   /* should never happen */
            default:
                RETURN_ERROR(corruption_detected, "invalid block type");
            }
            FORWARD_IF_ERROR(rSize, "");
            RETURN_ERROR_IF(rSize > dctx->fParams.blockSizeMax, corruption_detected, "Decompressed Block Size Exceeds Maximum");
            DEBUGLOG(5, "ZSTD_decompressContinue: decoded size from block : %u", (unsigned)rSize);
            dctx->decodedSize += rSize;
            if (dctx->validateChecksum) xxh64_update(&dctx->xxhState, dst, rSize);
            dctx->previousDstEnd = (char*)dst + rSize;

            /* Stay on the same stage until we are finished streaming the block. */
            if (dctx->expected > 0) {
                return rSize;
            }

            if (dctx->stage == ZSTDds_decompressLastBlock) {   /* end of frame */
                DEBUGLOG(4, "ZSTD_decompressContinue: decoded size from frame : %u", (unsigned)dctx->decodedSize);
                RETURN_ERROR_IF(
                    dctx->fParams.frameContentSize != ZSTD_CONTENTSIZE_UNKNOWN
                 && dctx->decodedSize != dctx->fParams.frameContentSize,
                    corruption_detected, "");
                if (dctx->fParams.checksumFlag) {  /* another round for frame checksum */
                    dctx->expected = 4;
                    dctx->stage = ZSTDds_checkChecksum;
                } else {
                    ZSTD_DCtx_trace_end(dctx, dctx->decodedSize, dctx->processedCSize, /* streaming */ 1);
                    dctx->expected = 0;   /* ends here */
                    dctx->stage = ZSTDds_getFrameHeaderSize;
                }
            } else {
                dctx->stage = ZSTDds_decodeBlockHeader;
                dctx->expected = ZSTD_blockHeaderSize;
            }
            return rSize;
        }

    case ZSTDds_checkChecksum:
        assert(srcSize == 4);  /* guaranteed by dctx->expected */
        {
            if (dctx->validateChecksum) {
                U32 const h32 = (U32)xxh64_digest(&dctx->xxhState);
                U32 const check32 = MEM_readLE32(src);
                DEBUGLOG(4, "ZSTD_decompressContinue: checksum : calculated %08X :: %08X read", (unsigned)h32, (unsigned)check32);
                RETURN_ERROR_IF(check32 != h32, checksum_wrong, "");
            }
            ZSTD_DCtx_trace_end(dctx, dctx->decodedSize, dctx->processedCSize, /* streaming */ 1);
            dctx->expected = 0;
            dctx->stage = ZSTDds_getFrameHeaderSize;
            return 0;
        }

    case ZSTDds_decodeSkippableHeader:
        assert(src != NULL);
        assert(srcSize <= ZSTD_SKIPPABLEHEADERSIZE);
        ZSTD_memcpy(dctx->headerBuffer + (ZSTD_SKIPPABLEHEADERSIZE - srcSize), src, srcSize);   /* complete skippable header */
        dctx->expected = MEM_readLE32(dctx->headerBuffer + ZSTD_FRAMEIDSIZE);   /* note : dctx->expected can grow seriously large, beyond local buffer size */
        dctx->stage = ZSTDds_skipFrame;
        return 0;

    case ZSTDds_skipFrame:
        dctx->expected = 0;
        dctx->stage = ZSTDds_getFrameHeaderSize;
        return 0;

    default:
        assert(0);   /* impossible */
        RETURN_ERROR(GENERIC, "impossible to reach");   /* some compiler require default to do something */
    }
}


static size_t ZSTD_refDictContent(ZSTD_DCtx* dctx, const void* dict, size_t dictSize)
{
    dctx->dictEnd = dctx->previousDstEnd;
    dctx->virtualStart = (const char*)dict - ((const char*)(dctx->previousDstEnd) - (const char*)(dctx->prefixStart));
    dctx->prefixStart = dict;
    dctx->previousDstEnd = (const char*)dict + dictSize;
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    dctx->dictContentBeginForFuzzing = dctx->prefixStart;
    dctx->dictContentEndForFuzzing = dctx->previousDstEnd;
#endif
    return 0;
}

/*! ZSTD_loadDEntropy() :
 *  dict : must point at beginning of a valid zstd dictionary.
 * @return : size of entropy tables read */
size_t
ZSTD_loadDEntropy(ZSTD_entropyDTables_t* entropy,
                  const void* const dict, size_t const dictSize)
{
    const BYTE* dictPtr = (const BYTE*)dict;
    const BYTE* const dictEnd = dictPtr + dictSize;

    RETURN_ERROR_IF(dictSize <= 8, dictionary_corrupted, "dict is too small");
    assert(MEM_readLE32(dict) == ZSTD_MAGIC_DICTIONARY);   /* dict must be valid */
    dictPtr += 8;   /* skip header = magic + dictID */

    ZSTD_STATIC_ASSERT(offsetof(ZSTD_entropyDTables_t, OFTable) == offsetof(ZSTD_entropyDTables_t, LLTable) + sizeof(entropy->LLTable));
    ZSTD_STATIC_ASSERT(offsetof(ZSTD_entropyDTables_t, MLTable) == offsetof(ZSTD_entropyDTables_t, OFTable) + sizeof(entropy->OFTable));
    ZSTD_STATIC_ASSERT(sizeof(entropy->LLTable) + sizeof(entropy->OFTable) + sizeof(entropy->MLTable) >= HUF_DECOMPRESS_WORKSPACE_SIZE);
    {   void* const workspace = &entropy->LLTable;   /* use fse tables as temporary workspace; implies fse tables are grouped together */
        size_t const workspaceSize = sizeof(entropy->LLTable) + sizeof(entropy->OFTable) + sizeof(entropy->MLTable);
#ifdef HUF_FORCE_DECOMPRESS_X1
        /* in minimal huffman, we always use X1 variants */
        size_t const hSize = HUF_readDTableX1_wksp(entropy->hufTable,
                                                dictPtr, dictEnd - dictPtr,
                                                workspace, workspaceSize);
#else
        size_t const hSize = HUF_readDTableX2_wksp(entropy->hufTable,
                                                dictPtr, (size_t)(dictEnd - dictPtr),
                                                workspace, workspaceSize);
#endif
        RETURN_ERROR_IF(HUF_isError(hSize), dictionary_corrupted, "");
        dictPtr += hSize;
    }

    {   short offcodeNCount[MaxOff+1];
        unsigned offcodeMaxValue = MaxOff, offcodeLog;
        size_t const offcodeHeaderSize = FSE_readNCount(offcodeNCount, &offcodeMaxValue, &offcodeLog, dictPtr, (size_t)(dictEnd-dictPtr));
        RETURN_ERROR_IF(FSE_isError(offcodeHeaderSize), dictionary_corrupted, "");
        RETURN_ERROR_IF(offcodeMaxValue > MaxOff, dictionary_corrupted, "");
        RETURN_ERROR_IF(offcodeLog > OffFSELog, dictionary_corrupted, "");
        ZSTD_buildFSETable( entropy->OFTable,
                            offcodeNCount, offcodeMaxValue,
                            OF_base, OF_bits,
                            offcodeLog,
                            entropy->workspace, sizeof(entropy->workspace),
                            /* bmi2 */0);
        dictPtr += offcodeHeaderSize;
    }

    {   short matchlengthNCount[MaxML+1];
        unsigned matchlengthMaxValue = MaxML, matchlengthLog;
        size_t const matchlengthHeaderSize = FSE_readNCount(matchlengthNCount, &matchlengthMaxValue, &matchlengthLog, dictPtr, (size_t)(dictEnd-dictPtr));
        RETURN_ERROR_IF(FSE_isError(matchlengthHeaderSize), dictionary_corrupted, "");
        RETURN_ERROR_IF(matchlengthMaxValue > MaxML, dictionary_corrupted, "");
        RETURN_ERROR_IF(matchlengthLog > MLFSELog, dictionary_corrupted, "");
        ZSTD_buildFSETable( entropy->MLTable,
                            matchlengthNCount, matchlengthMaxValue,
                            ML_base, ML_bits,
                            matchlengthLog,
                            entropy->workspace, sizeof(entropy->workspace),
                            /* bmi2 */ 0);
        dictPtr += matchlengthHeaderSize;
    }

    {   short litlengthNCount[MaxLL+1];
        unsigned litlengthMaxValue = MaxLL, litlengthLog;
        size_t const litlengthHeaderSize = FSE_readNCount(litlengthNCount, &litlengthMaxValue, &litlengthLog, dictPtr, (size_t)(dictEnd-dictPtr));
        RETURN_ERROR_IF(FSE_isError(litlengthHeaderSize), dictionary_corrupted, "");
        RETURN_ERROR_IF(litlengthMaxValue > MaxLL, dictionary_corrupted, "");
        RETURN_ERROR_IF(litlengthLog > LLFSELog, dictionary_corrupted, "");
        ZSTD_buildFSETable( entropy->LLTable,
                            litlengthNCount, litlengthMaxValue,
                            LL_base, LL_bits,
                            litlengthLog,
                            entropy->workspace, sizeof(entropy->workspace),
                            /* bmi2 */ 0);
        dictPtr += litlengthHeaderSize;
    }

    RETURN_ERROR_IF(dictPtr+12 > dictEnd, dictionary_corrupted, "");
    {   int i;
        size_t const dictContentSize = (size_t)(dictEnd - (dictPtr+12));
        for (i=0; i<3; i++) {
            U32 const rep = MEM_readLE32(dictPtr); dictPtr += 4;
            RETURN_ERROR_IF(rep==0 || rep > dictContentSize,
                            dictionary_corrupted, "");
            entropy->rep[i] = rep;
    }   }

    return (size_t)(dictPtr - (const BYTE*)dict);
}

static size_t ZSTD_decompress_insertDictionary(ZSTD_DCtx* dctx, const void* dict, size_t dictSize)
{
    if (dictSize < 8) return ZSTD_refDictContent(dctx, dict, dictSize);
    {   U32 const magic = MEM_readLE32(dict);
        if (magic != ZSTD_MAGIC_DICTIONARY) {
            return ZSTD_refDictContent(dctx, dict, dictSize);   /* pure content mode */
    }   }
    dctx->dictID = MEM_readLE32((const char*)dict + ZSTD_FRAMEIDSIZE);

    /* load entropy tables */
    {   size_t const eSize = ZSTD_loadDEntropy(&dctx->entropy, dict, dictSize);
        RETURN_ERROR_IF(ZSTD_isError(eSize), dictionary_corrupted, "");
        dict = (const char*)dict + eSize;
        dictSize -= eSize;
    }
    dctx->litEntropy = dctx->fseEntropy = 1;

    /* reference dictionary content */
    return ZSTD_refDictContent(dctx, dict, dictSize);
}

size_t ZSTD_decompressBegin(ZSTD_DCtx* dctx)
{
    assert(dctx != NULL);
    dctx->expected = ZSTD_startingInputLength(dctx->format);  /* dctx->format must be properly set */
    dctx->stage = ZSTDds_getFrameHeaderSize;
    dctx->processedCSize = 0;
    dctx->decodedSize = 0;
    dctx->previousDstEnd = NULL;
    dctx->prefixStart = NULL;
    dctx->virtualStart = NULL;
    dctx->dictEnd = NULL;
    dctx->entropy.hufTable[0] = (HUF_DTable)((HufLog)*0x1000001);  /* cover both little and big endian */
    dctx->litEntropy = dctx->fseEntropy = 0;
    dctx->dictID = 0;
    dctx->bType = bt_reserved;
    ZSTD_STATIC_ASSERT(sizeof(dctx->entropy.rep) == sizeof(repStartValue));
    ZSTD_memcpy(dctx->entropy.rep, repStartValue, sizeof(repStartValue));  /* initial repcodes */
    dctx->LLTptr = dctx->entropy.LLTable;
    dctx->MLTptr = dctx->entropy.MLTable;
    dctx->OFTptr = dctx->entropy.OFTable;
    dctx->HUFptr = dctx->entropy.hufTable;
    return 0;
}

size_t ZSTD_decompressBegin_usingDict(ZSTD_DCtx* dctx, const void* dict, size_t dictSize)
{
    FORWARD_IF_ERROR( ZSTD_decompressBegin(dctx) , "");
    if (dict && dictSize)
        RETURN_ERROR_IF(
            ZSTD_isError(ZSTD_decompress_insertDictionary(dctx, dict, dictSize)),
            dictionary_corrupted, "");
    return 0;
}


/* ======   ZSTD_DDict   ====== */

size_t ZSTD_decompressBegin_usingDDict(ZSTD_DCtx* dctx, const ZSTD_DDict* ddict)
{
    DEBUGLOG(4, "ZSTD_decompressBegin_usingDDict");
    assert(dctx != NULL);
    if (ddict) {
        const char* const dictStart = (const char*)ZSTD_DDict_dictContent(ddict);
        size_t const dictSize = ZSTD_DDict_dictSize(ddict);
        const void* const dictEnd = dictStart + dictSize;
        dctx->ddictIsCold = (dctx->dictEnd != dictEnd);
        DEBUGLOG(4, "DDict is %s",
                    dctx->ddictIsCold ? "~cold~" : "hot!");
    }
    FORWARD_IF_ERROR( ZSTD_decompressBegin(dctx) , "");
    if (ddict) {   /* NULL ddict is equivalent to no dictionary */
        ZSTD_copyDDictParameters(dctx, ddict);
    }
    return 0;
}

/*! ZSTD_getDictID_fromDict() :
 *  Provides the dictID stored within dictionary.
 *  if @return == 0, the dictionary is not conformant with Zstandard specification.
 *  It can still be loaded, but as a content-only dictionary. */
unsigned ZSTD_getDictID_fromDict(const void* dict, size_t dictSize)
{
    if (dictSize < 8) return 0;
    if (MEM_readLE32(dict) != ZSTD_MAGIC_DICTIONARY) return 0;
    return MEM_readLE32((const char*)dict + ZSTD_FRAMEIDSIZE);
}

/*! ZSTD_getDictID_fromFrame() :
 *  Provides the dictID required to decompress frame stored within `src`.
 *  If @return == 0, the dictID could not be decoded.
 *  This could for one of the following reasons :
 *  - The frame does not require a dictionary (most common case).
 *  - The frame was built with dictID intentionally removed.
 *    Needed dictionary is a hidden information.
 *    Note : this use case also happens when using a non-conformant dictionary.
 *  - `srcSize` is too small, and as a result, frame header could not be decoded.
 *    Note : possible if `srcSize < ZSTD_FRAMEHEADERSIZE_MAX`.
 *  - This is not a Zstandard frame.
 *  When identifying the exact failure cause, it's possible to use
 *  ZSTD_getFrameHeader(), which will provide a more precise error code. */
unsigned ZSTD_getDictID_fromFrame(const void* src, size_t srcSize)
{
    ZSTD_frameHeader zfp = { 0, 0, 0, ZSTD_frame, 0, 0, 0 };
    size_t const hError = ZSTD_getFrameHeader(&zfp, src, srcSize);
    if (ZSTD_isError(hError)) return 0;
    return zfp.dictID;
}


/*! ZSTD_decompress_usingDDict() :
*   Decompression using a pre-digested Dictionary
*   Use dictionary without significant overhead. */
size_t ZSTD_decompress_usingDDict(ZSTD_DCtx* dctx,
                                  void* dst, size_t dstCapacity,
                            const void* src, size_t srcSize,
                            const ZSTD_DDict* ddict)
{
    /* pass content and size in case legacy frames are encountered */
    return ZSTD_decompressMultiFrame(dctx, dst, dstCapacity, src, srcSize,
                                     NULL, 0,
                                     ddict);
}


/*=====================================
*   Streaming decompression
*====================================*/

ZSTD_DStream* ZSTD_createDStream(void)
{
    DEBUGLOG(3, "ZSTD_createDStream");
    return ZSTD_createDStream_advanced(ZSTD_defaultCMem);
}

ZSTD_DStream* ZSTD_initStaticDStream(void *workspace, size_t workspaceSize)
{
    return ZSTD_initStaticDCtx(workspace, workspaceSize);
}

ZSTD_DStream* ZSTD_createDStream_advanced(ZSTD_customMem customMem)
{
    return ZSTD_createDCtx_advanced(customMem);
}

size_t ZSTD_freeDStream(ZSTD_DStream* zds)
{
    return ZSTD_freeDCtx(zds);
}


/* ***  Initialization  *** */

size_t ZSTD_DStreamInSize(void)  { return ZSTD_BLOCKSIZE_MAX + ZSTD_blockHeaderSize; }
size_t ZSTD_DStreamOutSize(void) { return ZSTD_BLOCKSIZE_MAX; }

size_t ZSTD_DCtx_loadDictionary_advanced(ZSTD_DCtx* dctx,
                                   const void* dict, size_t dictSize,
                                         ZSTD_dictLoadMethod_e dictLoadMethod,
                                         ZSTD_dictContentType_e dictContentType)
{
    RETURN_ERROR_IF(dctx->streamStage != zdss_init, stage_wrong, "");
    ZSTD_clearDict(dctx);
    if (dict && dictSize != 0) {
        dctx->ddictLocal = ZSTD_createDDict_advanced(dict, dictSize, dictLoadMethod, dictContentType, dctx->customMem);
        RETURN_ERROR_IF(dctx->ddictLocal == NULL, memory_allocation, "NULL pointer!");
        dctx->ddict = dctx->ddictLocal;
        dctx->dictUses = ZSTD_use_indefinitely;
    }
    return 0;
}

size_t ZSTD_DCtx_loadDictionary_byReference(ZSTD_DCtx* dctx, const void* dict, size_t dictSize)
{
    return ZSTD_DCtx_loadDictionary_advanced(dctx, dict, dictSize, ZSTD_dlm_byRef, ZSTD_dct_auto);
}

size_t ZSTD_DCtx_loadDictionary(ZSTD_DCtx* dctx, const void* dict, size_t dictSize)
{
    return ZSTD_DCtx_loadDictionary_advanced(dctx, dict, dictSize, ZSTD_dlm_byCopy, ZSTD_dct_auto);
}

size_t ZSTD_DCtx_refPrefix_advanced(ZSTD_DCtx* dctx, const void* prefix, size_t prefixSize, ZSTD_dictContentType_e dictContentType)
{
    FORWARD_IF_ERROR(ZSTD_DCtx_loadDictionary_advanced(dctx, prefix, prefixSize, ZSTD_dlm_byRef, dictContentType), "");
    dctx->dictUses = ZSTD_use_once;
    return 0;
}

size_t ZSTD_DCtx_refPrefix(ZSTD_DCtx* dctx, const void* prefix, size_t prefixSize)
{
    return ZSTD_DCtx_refPrefix_advanced(dctx, prefix, prefixSize, ZSTD_dct_rawContent);
}


/* ZSTD_initDStream_usingDict() :
 * return : expected size, aka ZSTD_startingInputLength().
 * this function cannot fail */
size_t ZSTD_initDStream_usingDict(ZSTD_DStream* zds, const void* dict, size_t dictSize)
{
    DEBUGLOG(4, "ZSTD_initDStream_usingDict");
    FORWARD_IF_ERROR( ZSTD_DCtx_reset(zds, ZSTD_reset_session_only) , "");
    FORWARD_IF_ERROR( ZSTD_DCtx_loadDictionary(zds, dict, dictSize) , "");
    return ZSTD_startingInputLength(zds->format);
}

/* note : this variant can't fail */
size_t ZSTD_initDStream(ZSTD_DStream* zds)
{
    DEBUGLOG(4, "ZSTD_initDStream");
    return ZSTD_initDStream_usingDDict(zds, NULL);
}

/* ZSTD_initDStream_usingDDict() :
 * ddict will just be referenced, and must outlive decompression session
 * this function cannot fail */
size_t ZSTD_initDStream_usingDDict(ZSTD_DStream* dctx, const ZSTD_DDict* ddict)
{
    FORWARD_IF_ERROR( ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only) , "");
    FORWARD_IF_ERROR( ZSTD_DCtx_refDDict(dctx, ddict) , "");
    return ZSTD_startingInputLength(dctx->format);
}

/* ZSTD_resetDStream() :
 * return : expected size, aka ZSTD_startingInputLength().
 * this function cannot fail */
size_t ZSTD_resetDStream(ZSTD_DStream* dctx)
{
    FORWARD_IF_ERROR(ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only), "");
    return ZSTD_startingInputLength(dctx->format);
}


size_t ZSTD_DCtx_refDDict(ZSTD_DCtx* dctx, const ZSTD_DDict* ddict)
{
    RETURN_ERROR_IF(dctx->streamStage != zdss_init, stage_wrong, "");
    ZSTD_clearDict(dctx);
    if (ddict) {
        dctx->ddict = ddict;
        dctx->dictUses = ZSTD_use_indefinitely;
        if (dctx->refMultipleDDicts == ZSTD_rmd_refMultipleDDicts) {
            if (dctx->ddictSet == NULL) {
                dctx->ddictSet = ZSTD_createDDictHashSet(dctx->customMem);
                if (!dctx->ddictSet) {
                    RETURN_ERROR(memory_allocation, "Failed to allocate memory for hash set!");
                }
            }
            assert(!dctx->staticSize);  /* Impossible: ddictSet cannot have been allocated if static dctx */
            FORWARD_IF_ERROR(ZSTD_DDictHashSet_addDDict(dctx->ddictSet, ddict, dctx->customMem), "");
        }
    }
    return 0;
}

/* ZSTD_DCtx_setMaxWindowSize() :
 * note : no direct equivalence in ZSTD_DCtx_setParameter,
 * since this version sets windowSize, and the other sets windowLog */
size_t ZSTD_DCtx_setMaxWindowSize(ZSTD_DCtx* dctx, size_t maxWindowSize)
{
    ZSTD_bounds const bounds = ZSTD_dParam_getBounds(ZSTD_d_windowLogMax);
    size_t const min = (size_t)1 << bounds.lowerBound;
    size_t const max = (size_t)1 << bounds.upperBound;
    RETURN_ERROR_IF(dctx->streamStage != zdss_init, stage_wrong, "");
    RETURN_ERROR_IF(maxWindowSize < min, parameter_outOfBound, "");
    RETURN_ERROR_IF(maxWindowSize > max, parameter_outOfBound, "");
    dctx->maxWindowSize = maxWindowSize;
    return 0;
}

size_t ZSTD_DCtx_setFormat(ZSTD_DCtx* dctx, ZSTD_format_e format)
{
    return ZSTD_DCtx_setParameter(dctx, ZSTD_d_format, (int)format);
}

ZSTD_bounds ZSTD_dParam_getBounds(ZSTD_dParameter dParam)
{
    ZSTD_bounds bounds = { 0, 0, 0 };
    switch(dParam) {
        case ZSTD_d_windowLogMax:
            bounds.lowerBound = ZSTD_WINDOWLOG_ABSOLUTEMIN;
            bounds.upperBound = ZSTD_WINDOWLOG_MAX;
            return bounds;
        case ZSTD_d_format:
            bounds.lowerBound = (int)ZSTD_f_zstd1;
            bounds.upperBound = (int)ZSTD_f_zstd1_magicless;
            ZSTD_STATIC_ASSERT(ZSTD_f_zstd1 < ZSTD_f_zstd1_magicless);
            return bounds;
        case ZSTD_d_stableOutBuffer:
            bounds.lowerBound = (int)ZSTD_bm_buffered;
            bounds.upperBound = (int)ZSTD_bm_stable;
            return bounds;
        case ZSTD_d_forceIgnoreChecksum:
            bounds.lowerBound = (int)ZSTD_d_validateChecksum;
            bounds.upperBound = (int)ZSTD_d_ignoreChecksum;
            return bounds;
        case ZSTD_d_refMultipleDDicts:
            bounds.lowerBound = (int)ZSTD_rmd_refSingleDDict;
            bounds.upperBound = (int)ZSTD_rmd_refMultipleDDicts;
            return bounds;
        default:;
    }
    bounds.error = ERROR(parameter_unsupported);
    return bounds;
}

/* ZSTD_dParam_withinBounds:
 * @return 1 if value is within dParam bounds,
 * 0 otherwise */
static int ZSTD_dParam_withinBounds(ZSTD_dParameter dParam, int value)
{
    ZSTD_bounds const bounds = ZSTD_dParam_getBounds(dParam);
    if (ZSTD_isError(bounds.error)) return 0;
    if (value < bounds.lowerBound) return 0;
    if (value > bounds.upperBound) return 0;
    return 1;
}

#define CHECK_DBOUNDS(p,v) {                \
    RETURN_ERROR_IF(!ZSTD_dParam_withinBounds(p, v), parameter_outOfBound, ""); \
}

size_t ZSTD_DCtx_getParameter(ZSTD_DCtx* dctx, ZSTD_dParameter param, int* value)
{
    switch (param) {
        case ZSTD_d_windowLogMax:
            *value = (int)ZSTD_highbit32((U32)dctx->maxWindowSize);
            return 0;
        case ZSTD_d_format:
            *value = (int)dctx->format;
            return 0;
        case ZSTD_d_stableOutBuffer:
            *value = (int)dctx->outBufferMode;
            return 0;
        case ZSTD_d_forceIgnoreChecksum:
            *value = (int)dctx->forceIgnoreChecksum;
            return 0;
        case ZSTD_d_refMultipleDDicts:
            *value = (int)dctx->refMultipleDDicts;
            return 0;
        default:;
    }
    RETURN_ERROR(parameter_unsupported, "");
}

size_t ZSTD_DCtx_setParameter(ZSTD_DCtx* dctx, ZSTD_dParameter dParam, int value)
{
    RETURN_ERROR_IF(dctx->streamStage != zdss_init, stage_wrong, "");
    switch(dParam) {
        case ZSTD_d_windowLogMax:
            if (value == 0) value = ZSTD_WINDOWLOG_LIMIT_DEFAULT;
            CHECK_DBOUNDS(ZSTD_d_windowLogMax, value);
            dctx->maxWindowSize = ((size_t)1) << value;
            return 0;
        case ZSTD_d_format:
            CHECK_DBOUNDS(ZSTD_d_format, value);
            dctx->format = (ZSTD_format_e)value;
            return 0;
        case ZSTD_d_stableOutBuffer:
            CHECK_DBOUNDS(ZSTD_d_stableOutBuffer, value);
            dctx->outBufferMode = (ZSTD_bufferMode_e)value;
            return 0;
        case ZSTD_d_forceIgnoreChecksum:
            CHECK_DBOUNDS(ZSTD_d_forceIgnoreChecksum, value);
            dctx->forceIgnoreChecksum = (ZSTD_forceIgnoreChecksum_e)value;
            return 0;
        case ZSTD_d_refMultipleDDicts:
            CHECK_DBOUNDS(ZSTD_d_refMultipleDDicts, value);
            if (dctx->staticSize != 0) {
                RETURN_ERROR(parameter_unsupported, "Static dctx does not support multiple DDicts!");
            }
            dctx->refMultipleDDicts = (ZSTD_refMultipleDDicts_e)value;
            return 0;
        default:;
    }
    RETURN_ERROR(parameter_unsupported, "");
}

size_t ZSTD_DCtx_reset(ZSTD_DCtx* dctx, ZSTD_ResetDirective reset)
{
    if ( (reset == ZSTD_reset_session_only)
      || (reset == ZSTD_reset_session_and_parameters) ) {
        dctx->streamStage = zdss_init;
        dctx->noForwardProgress = 0;
    }
    if ( (reset == ZSTD_reset_parameters)
      || (reset == ZSTD_reset_session_and_parameters) ) {
        RETURN_ERROR_IF(dctx->streamStage != zdss_init, stage_wrong, "");
        ZSTD_clearDict(dctx);
        ZSTD_DCtx_resetParameters(dctx);
    }
    return 0;
}


size_t ZSTD_sizeof_DStream(const ZSTD_DStream* dctx)
{
    return ZSTD_sizeof_DCtx(dctx);
}

size_t ZSTD_decodingBufferSize_min(unsigned long long windowSize, unsigned long long frameContentSize)
{
    size_t const blockSize = (size_t) MIN(windowSize, ZSTD_BLOCKSIZE_MAX);
    unsigned long long const neededRBSize = windowSize + blockSize + (WILDCOPY_OVERLENGTH * 2);
    unsigned long long const neededSize = MIN(frameContentSize, neededRBSize);
    size_t const minRBSize = (size_t) neededSize;
    RETURN_ERROR_IF((unsigned long long)minRBSize != neededSize,
                    frameParameter_windowTooLarge, "");
    return minRBSize;
}

size_t ZSTD_estimateDStreamSize(size_t windowSize)
{
    size_t const blockSize = MIN(windowSize, ZSTD_BLOCKSIZE_MAX);
    size_t const inBuffSize = blockSize;  /* no block can be larger */
    size_t const outBuffSize = ZSTD_decodingBufferSize_min(windowSize, ZSTD_CONTENTSIZE_UNKNOWN);
    return ZSTD_estimateDCtxSize() + inBuffSize + outBuffSize;
}

size_t ZSTD_estimateDStreamSize_fromFrame(const void* src, size_t srcSize)
{
    U32 const windowSizeMax = 1U << ZSTD_WINDOWLOG_MAX;   /* note : should be user-selectable, but requires an additional parameter (or a dctx) */
    ZSTD_frameHeader zfh;
    size_t const err = ZSTD_getFrameHeader(&zfh, src, srcSize);
    if (ZSTD_isError(err)) return err;
    RETURN_ERROR_IF(err>0, srcSize_wrong, "");
    RETURN_ERROR_IF(zfh.windowSize > windowSizeMax,
                    frameParameter_windowTooLarge, "");
    return ZSTD_estimateDStreamSize((size_t)zfh.windowSize);
}


/* *****   Decompression   ***** */

static int ZSTD_DCtx_isOverflow(ZSTD_DStream* zds, size_t const neededInBuffSize, size_t const neededOutBuffSize)
{
    return (zds->inBuffSize + zds->outBuffSize) >= (neededInBuffSize + neededOutBuffSize) * ZSTD_WORKSPACETOOLARGE_FACTOR;
}

static void ZSTD_DCtx_updateOversizedDuration(ZSTD_DStream* zds, size_t const neededInBuffSize, size_t const neededOutBuffSize)
{
    if (ZSTD_DCtx_isOverflow(zds, neededInBuffSize, neededOutBuffSize))
        zds->oversizedDuration++;
    else
        zds->oversizedDuration = 0;
}

static int ZSTD_DCtx_isOversizedTooLong(ZSTD_DStream* zds)
{
    return zds->oversizedDuration >= ZSTD_WORKSPACETOOLARGE_MAXDURATION;
}

/* Checks that the output buffer hasn't changed if ZSTD_obm_stable is used. */
static size_t ZSTD_checkOutBuffer(ZSTD_DStream const* zds, ZSTD_outBuffer const* output)
{
    ZSTD_outBuffer const expect = zds->expectedOutBuffer;
    /* No requirement when ZSTD_obm_stable is not enabled. */
    if (zds->outBufferMode != ZSTD_bm_stable)
        return 0;
    /* Any buffer is allowed in zdss_init, this must be the same for every other call until
     * the context is reset.
     */
    if (zds->streamStage == zdss_init)
        return 0;
    /* The buffer must match our expectation exactly. */
    if (expect.dst == output->dst && expect.pos == output->pos && expect.size == output->size)
        return 0;
    RETURN_ERROR(dstBuffer_wrong, "ZSTD_d_stableOutBuffer enabled but output differs!");
}

/* Calls ZSTD_decompressContinue() with the right parameters for ZSTD_decompressStream()
 * and updates the stage and the output buffer state. This call is extracted so it can be
 * used both when reading directly from the ZSTD_inBuffer, and in buffered input mode.
 * NOTE: You must break after calling this function since the streamStage is modified.
 */
static size_t ZSTD_decompressContinueStream(
            ZSTD_DStream* zds, char** op, char* oend,
            void const* src, size_t srcSize) {
    int const isSkipFrame = ZSTD_isSkipFrame(zds);
    if (zds->outBufferMode == ZSTD_bm_buffered) {
        size_t const dstSize = isSkipFrame ? 0 : zds->outBuffSize - zds->outStart;
        size_t const decodedSize = ZSTD_decompressContinue(zds,
                zds->outBuff + zds->outStart, dstSize, src, srcSize);
        FORWARD_IF_ERROR(decodedSize, "");
        if (!decodedSize && !isSkipFrame) {
            zds->streamStage = zdss_read;
        } else {
            zds->outEnd = zds->outStart + decodedSize;
            zds->streamStage = zdss_flush;
        }
    } else {
        /* Write directly into the output buffer */
        size_t const dstSize = isSkipFrame ? 0 : (size_t)(oend - *op);
        size_t const decodedSize = ZSTD_decompressContinue(zds, *op, dstSize, src, srcSize);
        FORWARD_IF_ERROR(decodedSize, "");
        *op += decodedSize;
        /* Flushing is not needed. */
        zds->streamStage = zdss_read;
        assert(*op <= oend);
        assert(zds->outBufferMode == ZSTD_bm_stable);
    }
    return 0;
}

size_t ZSTD_decompressStream(ZSTD_DStream* zds, ZSTD_outBuffer* output, ZSTD_inBuffer* input)
{
    const char* const src = (const char*)input->src;
    const char* const istart = input->pos != 0 ? src + input->pos : src;
    const char* const iend = input->size != 0 ? src + input->size : src;
    const char* ip = istart;
    char* const dst = (char*)output->dst;
    char* const ostart = output->pos != 0 ? dst + output->pos : dst;
    char* const oend = output->size != 0 ? dst + output->size : dst;
    char* op = ostart;
    U32 someMoreWork = 1;

    DEBUGLOG(5, "ZSTD_decompressStream");
    RETURN_ERROR_IF(
        input->pos > input->size,
        srcSize_wrong,
        "forbidden. in: pos: %u   vs size: %u",
        (U32)input->pos, (U32)input->size);
    RETURN_ERROR_IF(
        output->pos > output->size,
        dstSize_tooSmall,
        "forbidden. out: pos: %u   vs size: %u",
        (U32)output->pos, (U32)output->size);
    DEBUGLOG(5, "input size : %u", (U32)(input->size - input->pos));
    FORWARD_IF_ERROR(ZSTD_checkOutBuffer(zds, output), "");

    while (someMoreWork) {
        switch(zds->streamStage)
        {
        case zdss_init :
            DEBUGLOG(5, "stage zdss_init => transparent reset ");
            zds->streamStage = zdss_loadHeader;
            zds->lhSize = zds->inPos = zds->outStart = zds->outEnd = 0;
            zds->legacyVersion = 0;
            zds->hostageByte = 0;
            zds->expectedOutBuffer = *output;
            ZSTD_FALLTHROUGH;

        case zdss_loadHeader :
            DEBUGLOG(5, "stage zdss_loadHeader (srcSize : %u)", (U32)(iend - ip));
            {   size_t const hSize = ZSTD_getFrameHeader_advanced(&zds->fParams, zds->headerBuffer, zds->lhSize, zds->format);
                if (zds->refMultipleDDicts && zds->ddictSet) {
                    ZSTD_DCtx_selectFrameDDict(zds);
                }
                DEBUGLOG(5, "header size : %u", (U32)hSize);
                if (ZSTD_isError(hSize)) {
                    return hSize;   /* error */
                }
                if (hSize != 0) {   /* need more input */
                    size_t const toLoad = hSize - zds->lhSize;   /* if hSize!=0, hSize > zds->lhSize */
                    size_t const remainingInput = (size_t)(iend-ip);
                    assert(iend >= ip);
                    if (toLoad > remainingInput) {   /* not enough input to load full header */
                        if (remainingInput > 0) {
                            ZSTD_memcpy(zds->headerBuffer + zds->lhSize, ip, remainingInput);
                            zds->lhSize += remainingInput;
                        }
                        input->pos = input->size;
                        return (MAX((size_t)ZSTD_FRAMEHEADERSIZE_MIN(zds->format), hSize) - zds->lhSize) + ZSTD_blockHeaderSize;   /* remaining header bytes + next block header */
                    }
                    assert(ip != NULL);
                    ZSTD_memcpy(zds->headerBuffer + zds->lhSize, ip, toLoad); zds->lhSize = hSize; ip += toLoad;
                    break;
            }   }

            /* check for single-pass mode opportunity */
            if (zds->fParams.frameContentSize != ZSTD_CONTENTSIZE_UNKNOWN
                && zds->fParams.frameType != ZSTD_skippableFrame
                && (U64)(size_t)(oend-op) >= zds->fParams.frameContentSize) {
                size_t const cSize = ZSTD_findFrameCompressedSize(istart, (size_t)(iend-istart));
                if (cSize <= (size_t)(iend-istart)) {
                    /* shortcut : using single-pass mode */
                    size_t const decompressedSize = ZSTD_decompress_usingDDict(zds, op, (size_t)(oend-op), istart, cSize, ZSTD_getDDict(zds));
                    if (ZSTD_isError(decompressedSize)) return decompressedSize;
                    DEBUGLOG(4, "shortcut to single-pass ZSTD_decompress_usingDDict()")
                    ip = istart + cSize;
                    op += decompressedSize;
                    zds->expected = 0;
                    zds->streamStage = zdss_init;
                    someMoreWork = 0;
                    break;
            }   }

            /* Check output buffer is large enough for ZSTD_odm_stable. */
            if (zds->outBufferMode == ZSTD_bm_stable
                && zds->fParams.frameType != ZSTD_skippableFrame
                && zds->fParams.frameContentSize != ZSTD_CONTENTSIZE_UNKNOWN
                && (U64)(size_t)(oend-op) < zds->fParams.frameContentSize) {
                RETURN_ERROR(dstSize_tooSmall, "ZSTD_obm_stable passed but ZSTD_outBuffer is too small");
            }

            /* Consume header (see ZSTDds_decodeFrameHeader) */
            DEBUGLOG(4, "Consume header");
            FORWARD_IF_ERROR(ZSTD_decompressBegin_usingDDict(zds, ZSTD_getDDict(zds)), "");

            if ((MEM_readLE32(zds->headerBuffer) & ZSTD_MAGIC_SKIPPABLE_MASK) == ZSTD_MAGIC_SKIPPABLE_START) {  /* skippable frame */
                zds->expected = MEM_readLE32(zds->headerBuffer + ZSTD_FRAMEIDSIZE);
                zds->stage = ZSTDds_skipFrame;
            } else {
                FORWARD_IF_ERROR(ZSTD_decodeFrameHeader(zds, zds->headerBuffer, zds->lhSize), "");
                zds->expected = ZSTD_blockHeaderSize;
                zds->stage = ZSTDds_decodeBlockHeader;
            }

            /* control buffer memory usage */
            DEBUGLOG(4, "Control max memory usage (%u KB <= max %u KB)",
                        (U32)(zds->fParams.windowSize >>10),
                        (U32)(zds->maxWindowSize >> 10) );
            zds->fParams.windowSize = MAX(zds->fParams.windowSize, 1U << ZSTD_WINDOWLOG_ABSOLUTEMIN);
            RETURN_ERROR_IF(zds->fParams.windowSize > zds->maxWindowSize,
                            frameParameter_windowTooLarge, "");

            /* Adapt buffer sizes to frame header instructions */
            {   size_t const neededInBuffSize = MAX(zds->fParams.blockSizeMax, 4 /* frame checksum */);
                size_t const neededOutBuffSize = zds->outBufferMode == ZSTD_bm_buffered
                        ? ZSTD_decodingBufferSize_min(zds->fParams.windowSize, zds->fParams.frameContentSize)
                        : 0;

                ZSTD_DCtx_updateOversizedDuration(zds, neededInBuffSize, neededOutBuffSize);

                {   int const tooSmall = (zds->inBuffSize < neededInBuffSize) || (zds->outBuffSize < neededOutBuffSize);
                    int const tooLarge = ZSTD_DCtx_isOversizedTooLong(zds);

                    if (tooSmall || tooLarge) {
                        size_t const bufferSize = neededInBuffSize + neededOutBuffSize;
                        DEBUGLOG(4, "inBuff  : from %u to %u",
                                    (U32)zds->inBuffSize, (U32)neededInBuffSize);
                        DEBUGLOG(4, "outBuff : from %u to %u",
                                    (U32)zds->outBuffSize, (U32)neededOutBuffSize);
                        if (zds->staticSize) {  /* static DCtx */
                            DEBUGLOG(4, "staticSize : %u", (U32)zds->staticSize);
                            assert(zds->staticSize >= sizeof(ZSTD_DCtx));  /* controlled at init */
                            RETURN_ERROR_IF(
                                bufferSize > zds->staticSize - sizeof(ZSTD_DCtx),
                                memory_allocation, "");
                        } else {
                            ZSTD_customFree(zds->inBuff, zds->customMem);
                            zds->inBuffSize = 0;
                            zds->outBuffSize = 0;
                            zds->inBuff = (char*)ZSTD_customMalloc(bufferSize, zds->customMem);
                            RETURN_ERROR_IF(zds->inBuff == NULL, memory_allocation, "");
                        }
                        zds->inBuffSize = neededInBuffSize;
                        zds->outBuff = zds->inBuff + zds->inBuffSize;
                        zds->outBuffSize = neededOutBuffSize;
            }   }   }
            zds->streamStage = zdss_read;
            ZSTD_FALLTHROUGH;

        case zdss_read:
            DEBUGLOG(5, "stage zdss_read");
            {   size_t const neededInSize = ZSTD_nextSrcSizeToDecompressWithInputSize(zds, (size_t)(iend - ip));
                DEBUGLOG(5, "neededInSize = %u", (U32)neededInSize);
                if (neededInSize==0) {  /* end of frame */
                    zds->streamStage = zdss_init;
                    someMoreWork = 0;
                    break;
                }
                if ((size_t)(iend-ip) >= neededInSize) {  /* decode directly from src */
                    FORWARD_IF_ERROR(ZSTD_decompressContinueStream(zds, &op, oend, ip, neededInSize), "");
                    ip += neededInSize;
                    /* Function modifies the stage so we must break */
                    break;
            }   }
            if (ip==iend) { someMoreWork = 0; break; }   /* no more input */
            zds->streamStage = zdss_load;
            ZSTD_FALLTHROUGH;

        case zdss_load:
            {   size_t const neededInSize = ZSTD_nextSrcSizeToDecompress(zds);
                size_t const toLoad = neededInSize - zds->inPos;
                int const isSkipFrame = ZSTD_isSkipFrame(zds);
                size_t loadedSize;
                /* At this point we shouldn't be decompressing a block that we can stream. */
                assert(neededInSize == ZSTD_nextSrcSizeToDecompressWithInputSize(zds, iend - ip));
                if (isSkipFrame) {
                    loadedSize = MIN(toLoad, (size_t)(iend-ip));
                } else {
                    RETURN_ERROR_IF(toLoad > zds->inBuffSize - zds->inPos,
                                    corruption_detected,
                                    "should never happen");
                    loadedSize = ZSTD_limitCopy(zds->inBuff + zds->inPos, toLoad, ip, (size_t)(iend-ip));
                }
                ip += loadedSize;
                zds->inPos += loadedSize;
                if (loadedSize < toLoad) { someMoreWork = 0; break; }   /* not enough input, wait for more */

                /* decode loaded input */
                zds->inPos = 0;   /* input is consumed */
                FORWARD_IF_ERROR(ZSTD_decompressContinueStream(zds, &op, oend, zds->inBuff, neededInSize), "");
                /* Function modifies the stage so we must break */
                break;
            }
        case zdss_flush:
            {   size_t const toFlushSize = zds->outEnd - zds->outStart;
                size_t const flushedSize = ZSTD_limitCopy(op, (size_t)(oend-op), zds->outBuff + zds->outStart, toFlushSize);
                op += flushedSize;
                zds->outStart += flushedSize;
                if (flushedSize == toFlushSize) {  /* flush completed */
                    zds->streamStage = zdss_read;
                    if ( (zds->outBuffSize < zds->fParams.frameContentSize)
                      && (zds->outStart + zds->fParams.blockSizeMax > zds->outBuffSize) ) {
                        DEBUGLOG(5, "restart filling outBuff from beginning (left:%i, needed:%u)",
                                (int)(zds->outBuffSize - zds->outStart),
                                (U32)zds->fParams.blockSizeMax);
                        zds->outStart = zds->outEnd = 0;
                    }
                    break;
            }   }
            /* cannot complete flush */
            someMoreWork = 0;
            break;

        default:
            assert(0);    /* impossible */
            RETURN_ERROR(GENERIC, "impossible to reach");   /* some compiler require default to do something */
    }   }

    /* result */
    input->pos = (size_t)(ip - (const char*)(input->src));
    output->pos = (size_t)(op - (char*)(output->dst));

    /* Update the expected output buffer for ZSTD_obm_stable. */
    zds->expectedOutBuffer = *output;

    if ((ip==istart) && (op==ostart)) {  /* no forward progress */
        zds->noForwardProgress ++;
        if (zds->noForwardProgress >= ZSTD_NO_FORWARD_PROGRESS_MAX) {
            RETURN_ERROR_IF(op==oend, dstSize_tooSmall, "");
            RETURN_ERROR_IF(ip==iend, srcSize_wrong, "");
            assert(0);
        }
    } else {
        zds->noForwardProgress = 0;
    }
    {   size_t nextSrcSizeHint = ZSTD_nextSrcSizeToDecompress(zds);
        if (!nextSrcSizeHint) {   /* frame fully decoded */
            if (zds->outEnd == zds->outStart) {  /* output fully flushed */
                if (zds->hostageByte) {
                    if (input->pos >= input->size) {
                        /* can't release hostage (not present) */
                        zds->streamStage = zdss_read;
                        return 1;
                    }
                    input->pos++;  /* release hostage */
                }   /* zds->hostageByte */
                return 0;
            }  /* zds->outEnd == zds->outStart */
            if (!zds->hostageByte) { /* output not fully flushed; keep last byte as hostage; will be released when all output is flushed */
                input->pos--;   /* note : pos > 0, otherwise, impossible to finish reading last block */
                zds->hostageByte=1;
            }
            return 1;
        }  /* nextSrcSizeHint==0 */
        nextSrcSizeHint += ZSTD_blockHeaderSize * (ZSTD_nextInputType(zds) == ZSTDnit_block);   /* preload header of next block */
        assert(zds->inPos <= nextSrcSizeHint);
        nextSrcSizeHint -= zds->inPos;   /* part already loaded*/
        return nextSrcSizeHint;
    }
}

size_t ZSTD_decompressStream_simpleArgs (
                            ZSTD_DCtx* dctx,
                            void* dst, size_t dstCapacity, size_t* dstPos,
                      const void* src, size_t srcSize, size_t* srcPos)
{
    ZSTD_outBuffer output = { dst, dstCapacity, *dstPos };
    ZSTD_inBuffer  input  = { src, srcSize, *srcPos };
    /* ZSTD_compress_generic() will check validity of dstPos and srcPos */
    size_t const cErr = ZSTD_decompressStream(dctx, &output, &input);
    *dstPos = output.pos;
    *srcPos = input.pos;
    return cErr;
}
