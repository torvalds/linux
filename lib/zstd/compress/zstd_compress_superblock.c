/*
 * Copyright (c) Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

 /*-*************************************
 *  Dependencies
 ***************************************/
#include "zstd_compress_superblock.h"

#include "../common/zstd_internal.h"  /* ZSTD_getSequenceLength */
#include "hist.h"                     /* HIST_countFast_wksp */
#include "zstd_compress_internal.h"
#include "zstd_compress_sequences.h"
#include "zstd_compress_literals.h"

/*-*************************************
*  Superblock entropy buffer structs
***************************************/
/* ZSTD_hufCTablesMetadata_t :
 *  Stores Literals Block Type for a super-block in hType, and
 *  huffman tree description in hufDesBuffer.
 *  hufDesSize refers to the size of huffman tree description in bytes.
 *  This metadata is populated in ZSTD_buildSuperBlockEntropy_literal() */
typedef struct {
    symbolEncodingType_e hType;
    BYTE hufDesBuffer[ZSTD_MAX_HUF_HEADER_SIZE];
    size_t hufDesSize;
} ZSTD_hufCTablesMetadata_t;

/* ZSTD_fseCTablesMetadata_t :
 *  Stores symbol compression modes for a super-block in {ll, ol, ml}Type, and
 *  fse tables in fseTablesBuffer.
 *  fseTablesSize refers to the size of fse tables in bytes.
 *  This metadata is populated in ZSTD_buildSuperBlockEntropy_sequences() */
typedef struct {
    symbolEncodingType_e llType;
    symbolEncodingType_e ofType;
    symbolEncodingType_e mlType;
    BYTE fseTablesBuffer[ZSTD_MAX_FSE_HEADERS_SIZE];
    size_t fseTablesSize;
    size_t lastCountSize; /* This is to account for bug in 1.3.4. More detail in ZSTD_compressSubBlock_sequences() */
} ZSTD_fseCTablesMetadata_t;

typedef struct {
    ZSTD_hufCTablesMetadata_t hufMetadata;
    ZSTD_fseCTablesMetadata_t fseMetadata;
} ZSTD_entropyCTablesMetadata_t;


/* ZSTD_buildSuperBlockEntropy_literal() :
 *  Builds entropy for the super-block literals.
 *  Stores literals block type (raw, rle, compressed, repeat) and
 *  huffman description table to hufMetadata.
 *  @return : size of huffman description table or error code */
static size_t ZSTD_buildSuperBlockEntropy_literal(void* const src, size_t srcSize,
                                            const ZSTD_hufCTables_t* prevHuf,
                                                  ZSTD_hufCTables_t* nextHuf,
                                                  ZSTD_hufCTablesMetadata_t* hufMetadata,
                                                  const int disableLiteralsCompression,
                                                  void* workspace, size_t wkspSize)
{
    BYTE* const wkspStart = (BYTE*)workspace;
    BYTE* const wkspEnd = wkspStart + wkspSize;
    BYTE* const countWkspStart = wkspStart;
    unsigned* const countWksp = (unsigned*)workspace;
    const size_t countWkspSize = (HUF_SYMBOLVALUE_MAX + 1) * sizeof(unsigned);
    BYTE* const nodeWksp = countWkspStart + countWkspSize;
    const size_t nodeWkspSize = wkspEnd-nodeWksp;
    unsigned maxSymbolValue = 255;
    unsigned huffLog = HUF_TABLELOG_DEFAULT;
    HUF_repeat repeat = prevHuf->repeatMode;

    DEBUGLOG(5, "ZSTD_buildSuperBlockEntropy_literal (srcSize=%zu)", srcSize);

    /* Prepare nextEntropy assuming reusing the existing table */
    ZSTD_memcpy(nextHuf, prevHuf, sizeof(*prevHuf));

    if (disableLiteralsCompression) {
        DEBUGLOG(5, "set_basic - disabled");
        hufMetadata->hType = set_basic;
        return 0;
    }

    /* small ? don't even attempt compression (speed opt) */
#   define COMPRESS_LITERALS_SIZE_MIN 63
    {   size_t const minLitSize = (prevHuf->repeatMode == HUF_repeat_valid) ? 6 : COMPRESS_LITERALS_SIZE_MIN;
        if (srcSize <= minLitSize) {
            DEBUGLOG(5, "set_basic - too small");
            hufMetadata->hType = set_basic;
            return 0;
        }
    }

    /* Scan input and build symbol stats */
    {   size_t const largest = HIST_count_wksp (countWksp, &maxSymbolValue, (const BYTE*)src, srcSize, workspace, wkspSize);
        FORWARD_IF_ERROR(largest, "HIST_count_wksp failed");
        if (largest == srcSize) {
            DEBUGLOG(5, "set_rle");
            hufMetadata->hType = set_rle;
            return 0;
        }
        if (largest <= (srcSize >> 7)+4) {
            DEBUGLOG(5, "set_basic - no gain");
            hufMetadata->hType = set_basic;
            return 0;
        }
    }

    /* Validate the previous Huffman table */
    if (repeat == HUF_repeat_check && !HUF_validateCTable((HUF_CElt const*)prevHuf->CTable, countWksp, maxSymbolValue)) {
        repeat = HUF_repeat_none;
    }

    /* Build Huffman Tree */
    ZSTD_memset(nextHuf->CTable, 0, sizeof(nextHuf->CTable));
    huffLog = HUF_optimalTableLog(huffLog, srcSize, maxSymbolValue);
    {   size_t const maxBits = HUF_buildCTable_wksp((HUF_CElt*)nextHuf->CTable, countWksp,
                                                    maxSymbolValue, huffLog,
                                                    nodeWksp, nodeWkspSize);
        FORWARD_IF_ERROR(maxBits, "HUF_buildCTable_wksp");
        huffLog = (U32)maxBits;
        {   /* Build and write the CTable */
            size_t const newCSize = HUF_estimateCompressedSize(
                    (HUF_CElt*)nextHuf->CTable, countWksp, maxSymbolValue);
            size_t const hSize = HUF_writeCTable_wksp(
                    hufMetadata->hufDesBuffer, sizeof(hufMetadata->hufDesBuffer),
                    (HUF_CElt*)nextHuf->CTable, maxSymbolValue, huffLog,
                    nodeWksp, nodeWkspSize);
            /* Check against repeating the previous CTable */
            if (repeat != HUF_repeat_none) {
                size_t const oldCSize = HUF_estimateCompressedSize(
                        (HUF_CElt const*)prevHuf->CTable, countWksp, maxSymbolValue);
                if (oldCSize < srcSize && (oldCSize <= hSize + newCSize || hSize + 12 >= srcSize)) {
                    DEBUGLOG(5, "set_repeat - smaller");
                    ZSTD_memcpy(nextHuf, prevHuf, sizeof(*prevHuf));
                    hufMetadata->hType = set_repeat;
                    return 0;
                }
            }
            if (newCSize + hSize >= srcSize) {
                DEBUGLOG(5, "set_basic - no gains");
                ZSTD_memcpy(nextHuf, prevHuf, sizeof(*prevHuf));
                hufMetadata->hType = set_basic;
                return 0;
            }
            DEBUGLOG(5, "set_compressed (hSize=%u)", (U32)hSize);
            hufMetadata->hType = set_compressed;
            nextHuf->repeatMode = HUF_repeat_check;
            return hSize;
        }
    }
}

/* ZSTD_buildSuperBlockEntropy_sequences() :
 *  Builds entropy for the super-block sequences.
 *  Stores symbol compression modes and fse table to fseMetadata.
 *  @return : size of fse tables or error code */
static size_t ZSTD_buildSuperBlockEntropy_sequences(seqStore_t* seqStorePtr,
                                              const ZSTD_fseCTables_t* prevEntropy,
                                                    ZSTD_fseCTables_t* nextEntropy,
                                              const ZSTD_CCtx_params* cctxParams,
                                                    ZSTD_fseCTablesMetadata_t* fseMetadata,
                                                    void* workspace, size_t wkspSize)
{
    BYTE* const wkspStart = (BYTE*)workspace;
    BYTE* const wkspEnd = wkspStart + wkspSize;
    BYTE* const countWkspStart = wkspStart;
    unsigned* const countWksp = (unsigned*)workspace;
    const size_t countWkspSize = (MaxSeq + 1) * sizeof(unsigned);
    BYTE* const cTableWksp = countWkspStart + countWkspSize;
    const size_t cTableWkspSize = wkspEnd-cTableWksp;
    ZSTD_strategy const strategy = cctxParams->cParams.strategy;
    FSE_CTable* CTable_LitLength = nextEntropy->litlengthCTable;
    FSE_CTable* CTable_OffsetBits = nextEntropy->offcodeCTable;
    FSE_CTable* CTable_MatchLength = nextEntropy->matchlengthCTable;
    const BYTE* const ofCodeTable = seqStorePtr->ofCode;
    const BYTE* const llCodeTable = seqStorePtr->llCode;
    const BYTE* const mlCodeTable = seqStorePtr->mlCode;
    size_t const nbSeq = seqStorePtr->sequences - seqStorePtr->sequencesStart;
    BYTE* const ostart = fseMetadata->fseTablesBuffer;
    BYTE* const oend = ostart + sizeof(fseMetadata->fseTablesBuffer);
    BYTE* op = ostart;

    assert(cTableWkspSize >= (1 << MaxFSELog) * sizeof(FSE_FUNCTION_TYPE));
    DEBUGLOG(5, "ZSTD_buildSuperBlockEntropy_sequences (nbSeq=%zu)", nbSeq);
    ZSTD_memset(workspace, 0, wkspSize);

    fseMetadata->lastCountSize = 0;
    /* convert length/distances into codes */
    ZSTD_seqToCodes(seqStorePtr);
    /* build CTable for Literal Lengths */
    {   U32 LLtype;
        unsigned max = MaxLL;
        size_t const mostFrequent = HIST_countFast_wksp(countWksp, &max, llCodeTable, nbSeq, workspace, wkspSize);  /* can't fail */
        DEBUGLOG(5, "Building LL table");
        nextEntropy->litlength_repeatMode = prevEntropy->litlength_repeatMode;
        LLtype = ZSTD_selectEncodingType(&nextEntropy->litlength_repeatMode,
                                        countWksp, max, mostFrequent, nbSeq,
                                        LLFSELog, prevEntropy->litlengthCTable,
                                        LL_defaultNorm, LL_defaultNormLog,
                                        ZSTD_defaultAllowed, strategy);
        assert(set_basic < set_compressed && set_rle < set_compressed);
        assert(!(LLtype < set_compressed && nextEntropy->litlength_repeatMode != FSE_repeat_none)); /* We don't copy tables */
        {   size_t const countSize = ZSTD_buildCTable(op, oend - op, CTable_LitLength, LLFSELog, (symbolEncodingType_e)LLtype,
                                                    countWksp, max, llCodeTable, nbSeq, LL_defaultNorm, LL_defaultNormLog, MaxLL,
                                                    prevEntropy->litlengthCTable, sizeof(prevEntropy->litlengthCTable),
                                                    cTableWksp, cTableWkspSize);
            FORWARD_IF_ERROR(countSize, "ZSTD_buildCTable for LitLens failed");
            if (LLtype == set_compressed)
                fseMetadata->lastCountSize = countSize;
            op += countSize;
            fseMetadata->llType = (symbolEncodingType_e) LLtype;
    }   }
    /* build CTable for Offsets */
    {   U32 Offtype;
        unsigned max = MaxOff;
        size_t const mostFrequent = HIST_countFast_wksp(countWksp, &max, ofCodeTable, nbSeq, workspace, wkspSize);  /* can't fail */
        /* We can only use the basic table if max <= DefaultMaxOff, otherwise the offsets are too large */
        ZSTD_defaultPolicy_e const defaultPolicy = (max <= DefaultMaxOff) ? ZSTD_defaultAllowed : ZSTD_defaultDisallowed;
        DEBUGLOG(5, "Building OF table");
        nextEntropy->offcode_repeatMode = prevEntropy->offcode_repeatMode;
        Offtype = ZSTD_selectEncodingType(&nextEntropy->offcode_repeatMode,
                                        countWksp, max, mostFrequent, nbSeq,
                                        OffFSELog, prevEntropy->offcodeCTable,
                                        OF_defaultNorm, OF_defaultNormLog,
                                        defaultPolicy, strategy);
        assert(!(Offtype < set_compressed && nextEntropy->offcode_repeatMode != FSE_repeat_none)); /* We don't copy tables */
        {   size_t const countSize = ZSTD_buildCTable(op, oend - op, CTable_OffsetBits, OffFSELog, (symbolEncodingType_e)Offtype,
                                                    countWksp, max, ofCodeTable, nbSeq, OF_defaultNorm, OF_defaultNormLog, DefaultMaxOff,
                                                    prevEntropy->offcodeCTable, sizeof(prevEntropy->offcodeCTable),
                                                    cTableWksp, cTableWkspSize);
            FORWARD_IF_ERROR(countSize, "ZSTD_buildCTable for Offsets failed");
            if (Offtype == set_compressed)
                fseMetadata->lastCountSize = countSize;
            op += countSize;
            fseMetadata->ofType = (symbolEncodingType_e) Offtype;
    }   }
    /* build CTable for MatchLengths */
    {   U32 MLtype;
        unsigned max = MaxML;
        size_t const mostFrequent = HIST_countFast_wksp(countWksp, &max, mlCodeTable, nbSeq, workspace, wkspSize);   /* can't fail */
        DEBUGLOG(5, "Building ML table (remaining space : %i)", (int)(oend-op));
        nextEntropy->matchlength_repeatMode = prevEntropy->matchlength_repeatMode;
        MLtype = ZSTD_selectEncodingType(&nextEntropy->matchlength_repeatMode,
                                        countWksp, max, mostFrequent, nbSeq,
                                        MLFSELog, prevEntropy->matchlengthCTable,
                                        ML_defaultNorm, ML_defaultNormLog,
                                        ZSTD_defaultAllowed, strategy);
        assert(!(MLtype < set_compressed && nextEntropy->matchlength_repeatMode != FSE_repeat_none)); /* We don't copy tables */
        {   size_t const countSize = ZSTD_buildCTable(op, oend - op, CTable_MatchLength, MLFSELog, (symbolEncodingType_e)MLtype,
                                                    countWksp, max, mlCodeTable, nbSeq, ML_defaultNorm, ML_defaultNormLog, MaxML,
                                                    prevEntropy->matchlengthCTable, sizeof(prevEntropy->matchlengthCTable),
                                                    cTableWksp, cTableWkspSize);
            FORWARD_IF_ERROR(countSize, "ZSTD_buildCTable for MatchLengths failed");
            if (MLtype == set_compressed)
                fseMetadata->lastCountSize = countSize;
            op += countSize;
            fseMetadata->mlType = (symbolEncodingType_e) MLtype;
    }   }
    assert((size_t) (op-ostart) <= sizeof(fseMetadata->fseTablesBuffer));
    return op-ostart;
}


/* ZSTD_buildSuperBlockEntropy() :
 *  Builds entropy for the super-block.
 *  @return : 0 on success or error code */
static size_t
ZSTD_buildSuperBlockEntropy(seqStore_t* seqStorePtr,
                      const ZSTD_entropyCTables_t* prevEntropy,
                            ZSTD_entropyCTables_t* nextEntropy,
                      const ZSTD_CCtx_params* cctxParams,
                            ZSTD_entropyCTablesMetadata_t* entropyMetadata,
                            void* workspace, size_t wkspSize)
{
    size_t const litSize = seqStorePtr->lit - seqStorePtr->litStart;
    DEBUGLOG(5, "ZSTD_buildSuperBlockEntropy");
    entropyMetadata->hufMetadata.hufDesSize =
        ZSTD_buildSuperBlockEntropy_literal(seqStorePtr->litStart, litSize,
                                            &prevEntropy->huf, &nextEntropy->huf,
                                            &entropyMetadata->hufMetadata,
                                            ZSTD_disableLiteralsCompression(cctxParams),
                                            workspace, wkspSize);
    FORWARD_IF_ERROR(entropyMetadata->hufMetadata.hufDesSize, "ZSTD_buildSuperBlockEntropy_literal failed");
    entropyMetadata->fseMetadata.fseTablesSize =
        ZSTD_buildSuperBlockEntropy_sequences(seqStorePtr,
                                              &prevEntropy->fse, &nextEntropy->fse,
                                              cctxParams,
                                              &entropyMetadata->fseMetadata,
                                              workspace, wkspSize);
    FORWARD_IF_ERROR(entropyMetadata->fseMetadata.fseTablesSize, "ZSTD_buildSuperBlockEntropy_sequences failed");
    return 0;
}

/* ZSTD_compressSubBlock_literal() :
 *  Compresses literals section for a sub-block.
 *  When we have to write the Huffman table we will sometimes choose a header
 *  size larger than necessary. This is because we have to pick the header size
 *  before we know the table size + compressed size, so we have a bound on the
 *  table size. If we guessed incorrectly, we fall back to uncompressed literals.
 *
 *  We write the header when writeEntropy=1 and set entropyWritten=1 when we succeeded
 *  in writing the header, otherwise it is set to 0.
 *
 *  hufMetadata->hType has literals block type info.
 *      If it is set_basic, all sub-blocks literals section will be Raw_Literals_Block.
 *      If it is set_rle, all sub-blocks literals section will be RLE_Literals_Block.
 *      If it is set_compressed, first sub-block's literals section will be Compressed_Literals_Block
 *      If it is set_compressed, first sub-block's literals section will be Treeless_Literals_Block
 *      and the following sub-blocks' literals sections will be Treeless_Literals_Block.
 *  @return : compressed size of literals section of a sub-block
 *            Or 0 if it unable to compress.
 *            Or error code */
static size_t ZSTD_compressSubBlock_literal(const HUF_CElt* hufTable,
                                    const ZSTD_hufCTablesMetadata_t* hufMetadata,
                                    const BYTE* literals, size_t litSize,
                                    void* dst, size_t dstSize,
                                    const int bmi2, int writeEntropy, int* entropyWritten)
{
    size_t const header = writeEntropy ? 200 : 0;
    size_t const lhSize = 3 + (litSize >= (1 KB - header)) + (litSize >= (16 KB - header));
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + dstSize;
    BYTE* op = ostart + lhSize;
    U32 const singleStream = lhSize == 3;
    symbolEncodingType_e hType = writeEntropy ? hufMetadata->hType : set_repeat;
    size_t cLitSize = 0;

    (void)bmi2; /* TODO bmi2... */

    DEBUGLOG(5, "ZSTD_compressSubBlock_literal (litSize=%zu, lhSize=%zu, writeEntropy=%d)", litSize, lhSize, writeEntropy);

    *entropyWritten = 0;
    if (litSize == 0 || hufMetadata->hType == set_basic) {
      DEBUGLOG(5, "ZSTD_compressSubBlock_literal using raw literal");
      return ZSTD_noCompressLiterals(dst, dstSize, literals, litSize);
    } else if (hufMetadata->hType == set_rle) {
      DEBUGLOG(5, "ZSTD_compressSubBlock_literal using rle literal");
      return ZSTD_compressRleLiteralsBlock(dst, dstSize, literals, litSize);
    }

    assert(litSize > 0);
    assert(hufMetadata->hType == set_compressed || hufMetadata->hType == set_repeat);

    if (writeEntropy && hufMetadata->hType == set_compressed) {
        ZSTD_memcpy(op, hufMetadata->hufDesBuffer, hufMetadata->hufDesSize);
        op += hufMetadata->hufDesSize;
        cLitSize += hufMetadata->hufDesSize;
        DEBUGLOG(5, "ZSTD_compressSubBlock_literal (hSize=%zu)", hufMetadata->hufDesSize);
    }

    /* TODO bmi2 */
    {   const size_t cSize = singleStream ? HUF_compress1X_usingCTable(op, oend-op, literals, litSize, hufTable)
                                          : HUF_compress4X_usingCTable(op, oend-op, literals, litSize, hufTable);
        op += cSize;
        cLitSize += cSize;
        if (cSize == 0 || ERR_isError(cSize)) {
            DEBUGLOG(5, "Failed to write entropy tables %s", ZSTD_getErrorName(cSize));
            return 0;
        }
        /* If we expand and we aren't writing a header then emit uncompressed */
        if (!writeEntropy && cLitSize >= litSize) {
            DEBUGLOG(5, "ZSTD_compressSubBlock_literal using raw literal because uncompressible");
            return ZSTD_noCompressLiterals(dst, dstSize, literals, litSize);
        }
        /* If we are writing headers then allow expansion that doesn't change our header size. */
        if (lhSize < (size_t)(3 + (cLitSize >= 1 KB) + (cLitSize >= 16 KB))) {
            assert(cLitSize > litSize);
            DEBUGLOG(5, "Literals expanded beyond allowed header size");
            return ZSTD_noCompressLiterals(dst, dstSize, literals, litSize);
        }
        DEBUGLOG(5, "ZSTD_compressSubBlock_literal (cSize=%zu)", cSize);
    }

    /* Build header */
    switch(lhSize)
    {
    case 3: /* 2 - 2 - 10 - 10 */
        {   U32 const lhc = hType + ((!singleStream) << 2) + ((U32)litSize<<4) + ((U32)cLitSize<<14);
            MEM_writeLE24(ostart, lhc);
            break;
        }
    case 4: /* 2 - 2 - 14 - 14 */
        {   U32 const lhc = hType + (2 << 2) + ((U32)litSize<<4) + ((U32)cLitSize<<18);
            MEM_writeLE32(ostart, lhc);
            break;
        }
    case 5: /* 2 - 2 - 18 - 18 */
        {   U32 const lhc = hType + (3 << 2) + ((U32)litSize<<4) + ((U32)cLitSize<<22);
            MEM_writeLE32(ostart, lhc);
            ostart[4] = (BYTE)(cLitSize >> 10);
            break;
        }
    default:  /* not possible : lhSize is {3,4,5} */
        assert(0);
    }
    *entropyWritten = 1;
    DEBUGLOG(5, "Compressed literals: %u -> %u", (U32)litSize, (U32)(op-ostart));
    return op-ostart;
}

static size_t ZSTD_seqDecompressedSize(seqStore_t const* seqStore, const seqDef* sequences, size_t nbSeq, size_t litSize, int lastSequence) {
    const seqDef* const sstart = sequences;
    const seqDef* const send = sequences + nbSeq;
    const seqDef* sp = sstart;
    size_t matchLengthSum = 0;
    size_t litLengthSum = 0;
    while (send-sp > 0) {
        ZSTD_sequenceLength const seqLen = ZSTD_getSequenceLength(seqStore, sp);
        litLengthSum += seqLen.litLength;
        matchLengthSum += seqLen.matchLength;
        sp++;
    }
    assert(litLengthSum <= litSize);
    if (!lastSequence) {
        assert(litLengthSum == litSize);
    }
    return matchLengthSum + litSize;
}

/* ZSTD_compressSubBlock_sequences() :
 *  Compresses sequences section for a sub-block.
 *  fseMetadata->llType, fseMetadata->ofType, and fseMetadata->mlType have
 *  symbol compression modes for the super-block.
 *  The first successfully compressed block will have these in its header.
 *  We set entropyWritten=1 when we succeed in compressing the sequences.
 *  The following sub-blocks will always have repeat mode.
 *  @return : compressed size of sequences section of a sub-block
 *            Or 0 if it is unable to compress
 *            Or error code. */
static size_t ZSTD_compressSubBlock_sequences(const ZSTD_fseCTables_t* fseTables,
                                              const ZSTD_fseCTablesMetadata_t* fseMetadata,
                                              const seqDef* sequences, size_t nbSeq,
                                              const BYTE* llCode, const BYTE* mlCode, const BYTE* ofCode,
                                              const ZSTD_CCtx_params* cctxParams,
                                              void* dst, size_t dstCapacity,
                                              const int bmi2, int writeEntropy, int* entropyWritten)
{
    const int longOffsets = cctxParams->cParams.windowLog > STREAM_ACCUMULATOR_MIN;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + dstCapacity;
    BYTE* op = ostart;
    BYTE* seqHead;

    DEBUGLOG(5, "ZSTD_compressSubBlock_sequences (nbSeq=%zu, writeEntropy=%d, longOffsets=%d)", nbSeq, writeEntropy, longOffsets);

    *entropyWritten = 0;
    /* Sequences Header */
    RETURN_ERROR_IF((oend-op) < 3 /*max nbSeq Size*/ + 1 /*seqHead*/,
                    dstSize_tooSmall, "");
    if (nbSeq < 0x7F)
        *op++ = (BYTE)nbSeq;
    else if (nbSeq < LONGNBSEQ)
        op[0] = (BYTE)((nbSeq>>8) + 0x80), op[1] = (BYTE)nbSeq, op+=2;
    else
        op[0]=0xFF, MEM_writeLE16(op+1, (U16)(nbSeq - LONGNBSEQ)), op+=3;
    if (nbSeq==0) {
        return op - ostart;
    }

    /* seqHead : flags for FSE encoding type */
    seqHead = op++;

    DEBUGLOG(5, "ZSTD_compressSubBlock_sequences (seqHeadSize=%u)", (unsigned)(op-ostart));

    if (writeEntropy) {
        const U32 LLtype = fseMetadata->llType;
        const U32 Offtype = fseMetadata->ofType;
        const U32 MLtype = fseMetadata->mlType;
        DEBUGLOG(5, "ZSTD_compressSubBlock_sequences (fseTablesSize=%zu)", fseMetadata->fseTablesSize);
        *seqHead = (BYTE)((LLtype<<6) + (Offtype<<4) + (MLtype<<2));
        ZSTD_memcpy(op, fseMetadata->fseTablesBuffer, fseMetadata->fseTablesSize);
        op += fseMetadata->fseTablesSize;
    } else {
        const U32 repeat = set_repeat;
        *seqHead = (BYTE)((repeat<<6) + (repeat<<4) + (repeat<<2));
    }

    {   size_t const bitstreamSize = ZSTD_encodeSequences(
                                        op, oend - op,
                                        fseTables->matchlengthCTable, mlCode,
                                        fseTables->offcodeCTable, ofCode,
                                        fseTables->litlengthCTable, llCode,
                                        sequences, nbSeq,
                                        longOffsets, bmi2);
        FORWARD_IF_ERROR(bitstreamSize, "ZSTD_encodeSequences failed");
        op += bitstreamSize;
        /* zstd versions <= 1.3.4 mistakenly report corruption when
         * FSE_readNCount() receives a buffer < 4 bytes.
         * Fixed by https://github.com/facebook/zstd/pull/1146.
         * This can happen when the last set_compressed table present is 2
         * bytes and the bitstream is only one byte.
         * In this exceedingly rare case, we will simply emit an uncompressed
         * block, since it isn't worth optimizing.
         */
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
        if (writeEntropy && fseMetadata->lastCountSize && fseMetadata->lastCountSize + bitstreamSize < 4) {
            /* NCountSize >= 2 && bitstreamSize > 0 ==> lastCountSize == 3 */
            assert(fseMetadata->lastCountSize + bitstreamSize == 3);
            DEBUGLOG(5, "Avoiding bug in zstd decoder in versions <= 1.3.4 by "
                        "emitting an uncompressed block.");
            return 0;
        }
#endif
        DEBUGLOG(5, "ZSTD_compressSubBlock_sequences (bitstreamSize=%zu)", bitstreamSize);
    }

    /* zstd versions <= 1.4.0 mistakenly report error when
     * sequences section body size is less than 3 bytes.
     * Fixed by https://github.com/facebook/zstd/pull/1664.
     * This can happen when the previous sequences section block is compressed
     * with rle mode and the current block's sequences section is compressed
     * with repeat mode where sequences section body size can be 1 byte.
     */
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    if (op-seqHead < 4) {
        DEBUGLOG(5, "Avoiding bug in zstd decoder in versions <= 1.4.0 by emitting "
                    "an uncompressed block when sequences are < 4 bytes");
        return 0;
    }
#endif

    *entropyWritten = 1;
    return op - ostart;
}

/* ZSTD_compressSubBlock() :
 *  Compresses a single sub-block.
 *  @return : compressed size of the sub-block
 *            Or 0 if it failed to compress. */
static size_t ZSTD_compressSubBlock(const ZSTD_entropyCTables_t* entropy,
                                    const ZSTD_entropyCTablesMetadata_t* entropyMetadata,
                                    const seqDef* sequences, size_t nbSeq,
                                    const BYTE* literals, size_t litSize,
                                    const BYTE* llCode, const BYTE* mlCode, const BYTE* ofCode,
                                    const ZSTD_CCtx_params* cctxParams,
                                    void* dst, size_t dstCapacity,
                                    const int bmi2,
                                    int writeLitEntropy, int writeSeqEntropy,
                                    int* litEntropyWritten, int* seqEntropyWritten,
                                    U32 lastBlock)
{
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + dstCapacity;
    BYTE* op = ostart + ZSTD_blockHeaderSize;
    DEBUGLOG(5, "ZSTD_compressSubBlock (litSize=%zu, nbSeq=%zu, writeLitEntropy=%d, writeSeqEntropy=%d, lastBlock=%d)",
                litSize, nbSeq, writeLitEntropy, writeSeqEntropy, lastBlock);
    {   size_t cLitSize = ZSTD_compressSubBlock_literal((const HUF_CElt*)entropy->huf.CTable,
                                                        &entropyMetadata->hufMetadata, literals, litSize,
                                                        op, oend-op, bmi2, writeLitEntropy, litEntropyWritten);
        FORWARD_IF_ERROR(cLitSize, "ZSTD_compressSubBlock_literal failed");
        if (cLitSize == 0) return 0;
        op += cLitSize;
    }
    {   size_t cSeqSize = ZSTD_compressSubBlock_sequences(&entropy->fse,
                                                  &entropyMetadata->fseMetadata,
                                                  sequences, nbSeq,
                                                  llCode, mlCode, ofCode,
                                                  cctxParams,
                                                  op, oend-op,
                                                  bmi2, writeSeqEntropy, seqEntropyWritten);
        FORWARD_IF_ERROR(cSeqSize, "ZSTD_compressSubBlock_sequences failed");
        if (cSeqSize == 0) return 0;
        op += cSeqSize;
    }
    /* Write block header */
    {   size_t cSize = (op-ostart)-ZSTD_blockHeaderSize;
        U32 const cBlockHeader24 = lastBlock + (((U32)bt_compressed)<<1) + (U32)(cSize << 3);
        MEM_writeLE24(ostart, cBlockHeader24);
    }
    return op-ostart;
}

static size_t ZSTD_estimateSubBlockSize_literal(const BYTE* literals, size_t litSize,
                                                const ZSTD_hufCTables_t* huf,
                                                const ZSTD_hufCTablesMetadata_t* hufMetadata,
                                                void* workspace, size_t wkspSize,
                                                int writeEntropy)
{
    unsigned* const countWksp = (unsigned*)workspace;
    unsigned maxSymbolValue = 255;
    size_t literalSectionHeaderSize = 3; /* Use hard coded size of 3 bytes */

    if (hufMetadata->hType == set_basic) return litSize;
    else if (hufMetadata->hType == set_rle) return 1;
    else if (hufMetadata->hType == set_compressed || hufMetadata->hType == set_repeat) {
        size_t const largest = HIST_count_wksp (countWksp, &maxSymbolValue, (const BYTE*)literals, litSize, workspace, wkspSize);
        if (ZSTD_isError(largest)) return litSize;
        {   size_t cLitSizeEstimate = HUF_estimateCompressedSize((const HUF_CElt*)huf->CTable, countWksp, maxSymbolValue);
            if (writeEntropy) cLitSizeEstimate += hufMetadata->hufDesSize;
            return cLitSizeEstimate + literalSectionHeaderSize;
    }   }
    assert(0); /* impossible */
    return 0;
}

static size_t ZSTD_estimateSubBlockSize_symbolType(symbolEncodingType_e type,
                        const BYTE* codeTable, unsigned maxCode,
                        size_t nbSeq, const FSE_CTable* fseCTable,
                        const U32* additionalBits,
                        short const* defaultNorm, U32 defaultNormLog, U32 defaultMax,
                        void* workspace, size_t wkspSize)
{
    unsigned* const countWksp = (unsigned*)workspace;
    const BYTE* ctp = codeTable;
    const BYTE* const ctStart = ctp;
    const BYTE* const ctEnd = ctStart + nbSeq;
    size_t cSymbolTypeSizeEstimateInBits = 0;
    unsigned max = maxCode;

    HIST_countFast_wksp(countWksp, &max, codeTable, nbSeq, workspace, wkspSize);  /* can't fail */
    if (type == set_basic) {
        /* We selected this encoding type, so it must be valid. */
        assert(max <= defaultMax);
        cSymbolTypeSizeEstimateInBits = max <= defaultMax
                ? ZSTD_crossEntropyCost(defaultNorm, defaultNormLog, countWksp, max)
                : ERROR(GENERIC);
    } else if (type == set_rle) {
        cSymbolTypeSizeEstimateInBits = 0;
    } else if (type == set_compressed || type == set_repeat) {
        cSymbolTypeSizeEstimateInBits = ZSTD_fseBitCost(fseCTable, countWksp, max);
    }
    if (ZSTD_isError(cSymbolTypeSizeEstimateInBits)) return nbSeq * 10;
    while (ctp < ctEnd) {
        if (additionalBits) cSymbolTypeSizeEstimateInBits += additionalBits[*ctp];
        else cSymbolTypeSizeEstimateInBits += *ctp; /* for offset, offset code is also the number of additional bits */
        ctp++;
    }
    return cSymbolTypeSizeEstimateInBits / 8;
}

static size_t ZSTD_estimateSubBlockSize_sequences(const BYTE* ofCodeTable,
                                                  const BYTE* llCodeTable,
                                                  const BYTE* mlCodeTable,
                                                  size_t nbSeq,
                                                  const ZSTD_fseCTables_t* fseTables,
                                                  const ZSTD_fseCTablesMetadata_t* fseMetadata,
                                                  void* workspace, size_t wkspSize,
                                                  int writeEntropy)
{
    size_t sequencesSectionHeaderSize = 3; /* Use hard coded size of 3 bytes */
    size_t cSeqSizeEstimate = 0;
    cSeqSizeEstimate += ZSTD_estimateSubBlockSize_symbolType(fseMetadata->ofType, ofCodeTable, MaxOff,
                                         nbSeq, fseTables->offcodeCTable, NULL,
                                         OF_defaultNorm, OF_defaultNormLog, DefaultMaxOff,
                                         workspace, wkspSize);
    cSeqSizeEstimate += ZSTD_estimateSubBlockSize_symbolType(fseMetadata->llType, llCodeTable, MaxLL,
                                         nbSeq, fseTables->litlengthCTable, LL_bits,
                                         LL_defaultNorm, LL_defaultNormLog, MaxLL,
                                         workspace, wkspSize);
    cSeqSizeEstimate += ZSTD_estimateSubBlockSize_symbolType(fseMetadata->mlType, mlCodeTable, MaxML,
                                         nbSeq, fseTables->matchlengthCTable, ML_bits,
                                         ML_defaultNorm, ML_defaultNormLog, MaxML,
                                         workspace, wkspSize);
    if (writeEntropy) cSeqSizeEstimate += fseMetadata->fseTablesSize;
    return cSeqSizeEstimate + sequencesSectionHeaderSize;
}

static size_t ZSTD_estimateSubBlockSize(const BYTE* literals, size_t litSize,
                                        const BYTE* ofCodeTable,
                                        const BYTE* llCodeTable,
                                        const BYTE* mlCodeTable,
                                        size_t nbSeq,
                                        const ZSTD_entropyCTables_t* entropy,
                                        const ZSTD_entropyCTablesMetadata_t* entropyMetadata,
                                        void* workspace, size_t wkspSize,
                                        int writeLitEntropy, int writeSeqEntropy) {
    size_t cSizeEstimate = 0;
    cSizeEstimate += ZSTD_estimateSubBlockSize_literal(literals, litSize,
                                                         &entropy->huf, &entropyMetadata->hufMetadata,
                                                         workspace, wkspSize, writeLitEntropy);
    cSizeEstimate += ZSTD_estimateSubBlockSize_sequences(ofCodeTable, llCodeTable, mlCodeTable,
                                                         nbSeq, &entropy->fse, &entropyMetadata->fseMetadata,
                                                         workspace, wkspSize, writeSeqEntropy);
    return cSizeEstimate + ZSTD_blockHeaderSize;
}

static int ZSTD_needSequenceEntropyTables(ZSTD_fseCTablesMetadata_t const* fseMetadata)
{
    if (fseMetadata->llType == set_compressed || fseMetadata->llType == set_rle)
        return 1;
    if (fseMetadata->mlType == set_compressed || fseMetadata->mlType == set_rle)
        return 1;
    if (fseMetadata->ofType == set_compressed || fseMetadata->ofType == set_rle)
        return 1;
    return 0;
}

/* ZSTD_compressSubBlock_multi() :
 *  Breaks super-block into multiple sub-blocks and compresses them.
 *  Entropy will be written to the first block.
 *  The following blocks will use repeat mode to compress.
 *  All sub-blocks are compressed blocks (no raw or rle blocks).
 *  @return : compressed size of the super block (which is multiple ZSTD blocks)
 *            Or 0 if it failed to compress. */
static size_t ZSTD_compressSubBlock_multi(const seqStore_t* seqStorePtr,
                            const ZSTD_compressedBlockState_t* prevCBlock,
                            ZSTD_compressedBlockState_t* nextCBlock,
                            const ZSTD_entropyCTablesMetadata_t* entropyMetadata,
                            const ZSTD_CCtx_params* cctxParams,
                                  void* dst, size_t dstCapacity,
                            const void* src, size_t srcSize,
                            const int bmi2, U32 lastBlock,
                            void* workspace, size_t wkspSize)
{
    const seqDef* const sstart = seqStorePtr->sequencesStart;
    const seqDef* const send = seqStorePtr->sequences;
    const seqDef* sp = sstart;
    const BYTE* const lstart = seqStorePtr->litStart;
    const BYTE* const lend = seqStorePtr->lit;
    const BYTE* lp = lstart;
    BYTE const* ip = (BYTE const*)src;
    BYTE const* const iend = ip + srcSize;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + dstCapacity;
    BYTE* op = ostart;
    const BYTE* llCodePtr = seqStorePtr->llCode;
    const BYTE* mlCodePtr = seqStorePtr->mlCode;
    const BYTE* ofCodePtr = seqStorePtr->ofCode;
    size_t targetCBlockSize = cctxParams->targetCBlockSize;
    size_t litSize, seqCount;
    int writeLitEntropy = entropyMetadata->hufMetadata.hType == set_compressed;
    int writeSeqEntropy = 1;
    int lastSequence = 0;

    DEBUGLOG(5, "ZSTD_compressSubBlock_multi (litSize=%u, nbSeq=%u)",
                (unsigned)(lend-lp), (unsigned)(send-sstart));

    litSize = 0;
    seqCount = 0;
    do {
        size_t cBlockSizeEstimate = 0;
        if (sstart == send) {
            lastSequence = 1;
        } else {
            const seqDef* const sequence = sp + seqCount;
            lastSequence = sequence == send - 1;
            litSize += ZSTD_getSequenceLength(seqStorePtr, sequence).litLength;
            seqCount++;
        }
        if (lastSequence) {
            assert(lp <= lend);
            assert(litSize <= (size_t)(lend - lp));
            litSize = (size_t)(lend - lp);
        }
        /* I think there is an optimization opportunity here.
         * Calling ZSTD_estimateSubBlockSize for every sequence can be wasteful
         * since it recalculates estimate from scratch.
         * For example, it would recount literal distribution and symbol codes everytime.
         */
        cBlockSizeEstimate = ZSTD_estimateSubBlockSize(lp, litSize, ofCodePtr, llCodePtr, mlCodePtr, seqCount,
                                                       &nextCBlock->entropy, entropyMetadata,
                                                       workspace, wkspSize, writeLitEntropy, writeSeqEntropy);
        if (cBlockSizeEstimate > targetCBlockSize || lastSequence) {
            int litEntropyWritten = 0;
            int seqEntropyWritten = 0;
            const size_t decompressedSize = ZSTD_seqDecompressedSize(seqStorePtr, sp, seqCount, litSize, lastSequence);
            const size_t cSize = ZSTD_compressSubBlock(&nextCBlock->entropy, entropyMetadata,
                                                       sp, seqCount,
                                                       lp, litSize,
                                                       llCodePtr, mlCodePtr, ofCodePtr,
                                                       cctxParams,
                                                       op, oend-op,
                                                       bmi2, writeLitEntropy, writeSeqEntropy,
                                                       &litEntropyWritten, &seqEntropyWritten,
                                                       lastBlock && lastSequence);
            FORWARD_IF_ERROR(cSize, "ZSTD_compressSubBlock failed");
            if (cSize > 0 && cSize < decompressedSize) {
                DEBUGLOG(5, "Committed the sub-block");
                assert(ip + decompressedSize <= iend);
                ip += decompressedSize;
                sp += seqCount;
                lp += litSize;
                op += cSize;
                llCodePtr += seqCount;
                mlCodePtr += seqCount;
                ofCodePtr += seqCount;
                litSize = 0;
                seqCount = 0;
                /* Entropy only needs to be written once */
                if (litEntropyWritten) {
                    writeLitEntropy = 0;
                }
                if (seqEntropyWritten) {
                    writeSeqEntropy = 0;
                }
            }
        }
    } while (!lastSequence);
    if (writeLitEntropy) {
        DEBUGLOG(5, "ZSTD_compressSubBlock_multi has literal entropy tables unwritten");
        ZSTD_memcpy(&nextCBlock->entropy.huf, &prevCBlock->entropy.huf, sizeof(prevCBlock->entropy.huf));
    }
    if (writeSeqEntropy && ZSTD_needSequenceEntropyTables(&entropyMetadata->fseMetadata)) {
        /* If we haven't written our entropy tables, then we've violated our contract and
         * must emit an uncompressed block.
         */
        DEBUGLOG(5, "ZSTD_compressSubBlock_multi has sequence entropy tables unwritten");
        return 0;
    }
    if (ip < iend) {
        size_t const cSize = ZSTD_noCompressBlock(op, oend - op, ip, iend - ip, lastBlock);
        DEBUGLOG(5, "ZSTD_compressSubBlock_multi last sub-block uncompressed, %zu bytes", (size_t)(iend - ip));
        FORWARD_IF_ERROR(cSize, "ZSTD_noCompressBlock failed");
        assert(cSize != 0);
        op += cSize;
        /* We have to regenerate the repcodes because we've skipped some sequences */
        if (sp < send) {
            seqDef const* seq;
            repcodes_t rep;
            ZSTD_memcpy(&rep, prevCBlock->rep, sizeof(rep));
            for (seq = sstart; seq < sp; ++seq) {
                rep = ZSTD_updateRep(rep.rep, seq->offset - 1, ZSTD_getSequenceLength(seqStorePtr, seq).litLength == 0);
            }
            ZSTD_memcpy(nextCBlock->rep, &rep, sizeof(rep));
        }
    }
    DEBUGLOG(5, "ZSTD_compressSubBlock_multi compressed");
    return op-ostart;
}

size_t ZSTD_compressSuperBlock(ZSTD_CCtx* zc,
                               void* dst, size_t dstCapacity,
                               void const* src, size_t srcSize,
                               unsigned lastBlock) {
    ZSTD_entropyCTablesMetadata_t entropyMetadata;

    FORWARD_IF_ERROR(ZSTD_buildSuperBlockEntropy(&zc->seqStore,
          &zc->blockState.prevCBlock->entropy,
          &zc->blockState.nextCBlock->entropy,
          &zc->appliedParams,
          &entropyMetadata,
          zc->entropyWorkspace, ENTROPY_WORKSPACE_SIZE /* statically allocated in resetCCtx */), "");

    return ZSTD_compressSubBlock_multi(&zc->seqStore,
            zc->blockState.prevCBlock,
            zc->blockState.nextCBlock,
            &entropyMetadata,
            &zc->appliedParams,
            dst, dstCapacity,
            src, srcSize,
            zc->bmi2, lastBlock,
            zc->entropyWorkspace, ENTROPY_WORKSPACE_SIZE /* statically allocated in resetCCtx */);
}
