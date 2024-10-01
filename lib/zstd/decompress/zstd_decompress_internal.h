/*
 * Copyright (c) Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/* zstd_decompress_internal:
 * objects and definitions shared within lib/decompress modules */

 #ifndef ZSTD_DECOMPRESS_INTERNAL_H
 #define ZSTD_DECOMPRESS_INTERNAL_H


/*-*******************************************************
 *  Dependencies
 *********************************************************/
#include "../common/mem.h"             /* BYTE, U16, U32 */
#include "../common/zstd_internal.h"   /* ZSTD_seqSymbol */



/*-*******************************************************
 *  Constants
 *********************************************************/
static UNUSED_ATTR const U32 LL_base[MaxLL+1] = {
                 0,    1,    2,     3,     4,     5,     6,      7,
                 8,    9,   10,    11,    12,    13,    14,     15,
                16,   18,   20,    22,    24,    28,    32,     40,
                48,   64, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000,
                0x2000, 0x4000, 0x8000, 0x10000 };

static UNUSED_ATTR const U32 OF_base[MaxOff+1] = {
                 0,        1,       1,       5,     0xD,     0x1D,     0x3D,     0x7D,
                 0xFD,   0x1FD,   0x3FD,   0x7FD,   0xFFD,   0x1FFD,   0x3FFD,   0x7FFD,
                 0xFFFD, 0x1FFFD, 0x3FFFD, 0x7FFFD, 0xFFFFD, 0x1FFFFD, 0x3FFFFD, 0x7FFFFD,
                 0xFFFFFD, 0x1FFFFFD, 0x3FFFFFD, 0x7FFFFFD, 0xFFFFFFD, 0x1FFFFFFD, 0x3FFFFFFD, 0x7FFFFFFD };

static UNUSED_ATTR const U32 OF_bits[MaxOff+1] = {
                     0,  1,  2,  3,  4,  5,  6,  7,
                     8,  9, 10, 11, 12, 13, 14, 15,
                    16, 17, 18, 19, 20, 21, 22, 23,
                    24, 25, 26, 27, 28, 29, 30, 31 };

static UNUSED_ATTR const U32 ML_base[MaxML+1] = {
                     3,  4,  5,    6,     7,     8,     9,    10,
                    11, 12, 13,   14,    15,    16,    17,    18,
                    19, 20, 21,   22,    23,    24,    25,    26,
                    27, 28, 29,   30,    31,    32,    33,    34,
                    35, 37, 39,   41,    43,    47,    51,    59,
                    67, 83, 99, 0x83, 0x103, 0x203, 0x403, 0x803,
                    0x1003, 0x2003, 0x4003, 0x8003, 0x10003 };


/*-*******************************************************
 *  Decompression types
 *********************************************************/
 typedef struct {
     U32 fastMode;
     U32 tableLog;
 } ZSTD_seqSymbol_header;

 typedef struct {
     U16  nextState;
     BYTE nbAdditionalBits;
     BYTE nbBits;
     U32  baseValue;
 } ZSTD_seqSymbol;

 #define SEQSYMBOL_TABLE_SIZE(log)   (1 + (1 << (log)))

#define ZSTD_BUILD_FSE_TABLE_WKSP_SIZE (sizeof(S16) * (MaxSeq + 1) + (1u << MaxFSELog) + sizeof(U64))
#define ZSTD_BUILD_FSE_TABLE_WKSP_SIZE_U32 ((ZSTD_BUILD_FSE_TABLE_WKSP_SIZE + sizeof(U32) - 1) / sizeof(U32))

typedef struct {
    ZSTD_seqSymbol LLTable[SEQSYMBOL_TABLE_SIZE(LLFSELog)];    /* Note : Space reserved for FSE Tables */
    ZSTD_seqSymbol OFTable[SEQSYMBOL_TABLE_SIZE(OffFSELog)];   /* is also used as temporary workspace while building hufTable during DDict creation */
    ZSTD_seqSymbol MLTable[SEQSYMBOL_TABLE_SIZE(MLFSELog)];    /* and therefore must be at least HUF_DECOMPRESS_WORKSPACE_SIZE large */
    HUF_DTable hufTable[HUF_DTABLE_SIZE(HufLog)];  /* can accommodate HUF_decompress4X */
    U32 rep[ZSTD_REP_NUM];
    U32 workspace[ZSTD_BUILD_FSE_TABLE_WKSP_SIZE_U32];
} ZSTD_entropyDTables_t;

typedef enum { ZSTDds_getFrameHeaderSize, ZSTDds_decodeFrameHeader,
               ZSTDds_decodeBlockHeader, ZSTDds_decompressBlock,
               ZSTDds_decompressLastBlock, ZSTDds_checkChecksum,
               ZSTDds_decodeSkippableHeader, ZSTDds_skipFrame } ZSTD_dStage;

typedef enum { zdss_init=0, zdss_loadHeader,
               zdss_read, zdss_load, zdss_flush } ZSTD_dStreamStage;

typedef enum {
    ZSTD_use_indefinitely = -1,  /* Use the dictionary indefinitely */
    ZSTD_dont_use = 0,           /* Do not use the dictionary (if one exists free it) */
    ZSTD_use_once = 1            /* Use the dictionary once and set to ZSTD_dont_use */
} ZSTD_dictUses_e;

/* Hashset for storing references to multiple ZSTD_DDict within ZSTD_DCtx */
typedef struct {
    const ZSTD_DDict** ddictPtrTable;
    size_t ddictPtrTableSize;
    size_t ddictPtrCount;
} ZSTD_DDictHashSet;

struct ZSTD_DCtx_s
{
    const ZSTD_seqSymbol* LLTptr;
    const ZSTD_seqSymbol* MLTptr;
    const ZSTD_seqSymbol* OFTptr;
    const HUF_DTable* HUFptr;
    ZSTD_entropyDTables_t entropy;
    U32 workspace[HUF_DECOMPRESS_WORKSPACE_SIZE_U32];   /* space needed when building huffman tables */
    const void* previousDstEnd;   /* detect continuity */
    const void* prefixStart;      /* start of current segment */
    const void* virtualStart;     /* virtual start of previous segment if it was just before current one */
    const void* dictEnd;          /* end of previous segment */
    size_t expected;
    ZSTD_frameHeader fParams;
    U64 processedCSize;
    U64 decodedSize;
    blockType_e bType;            /* used in ZSTD_decompressContinue(), store blockType between block header decoding and block decompression stages */
    ZSTD_dStage stage;
    U32 litEntropy;
    U32 fseEntropy;
    struct xxh64_state xxhState;
    size_t headerSize;
    ZSTD_format_e format;
    ZSTD_forceIgnoreChecksum_e forceIgnoreChecksum;   /* User specified: if == 1, will ignore checksums in compressed frame. Default == 0 */
    U32 validateChecksum;         /* if == 1, will validate checksum. Is == 1 if (fParams.checksumFlag == 1) and (forceIgnoreChecksum == 0). */
    const BYTE* litPtr;
    ZSTD_customMem customMem;
    size_t litSize;
    size_t rleSize;
    size_t staticSize;
    int bmi2;                     /* == 1 if the CPU supports BMI2 and 0 otherwise. CPU support is determined dynamically once per context lifetime. */

    /* dictionary */
    ZSTD_DDict* ddictLocal;
    const ZSTD_DDict* ddict;     /* set by ZSTD_initDStream_usingDDict(), or ZSTD_DCtx_refDDict() */
    U32 dictID;
    int ddictIsCold;             /* if == 1 : dictionary is "new" for working context, and presumed "cold" (not in cpu cache) */
    ZSTD_dictUses_e dictUses;
    ZSTD_DDictHashSet* ddictSet;                    /* Hash set for multiple ddicts */
    ZSTD_refMultipleDDicts_e refMultipleDDicts;     /* User specified: if == 1, will allow references to multiple DDicts. Default == 0 (disabled) */

    /* streaming */
    ZSTD_dStreamStage streamStage;
    char*  inBuff;
    size_t inBuffSize;
    size_t inPos;
    size_t maxWindowSize;
    char*  outBuff;
    size_t outBuffSize;
    size_t outStart;
    size_t outEnd;
    size_t lhSize;
    void* legacyContext;
    U32 previousLegacyVersion;
    U32 legacyVersion;
    U32 hostageByte;
    int noForwardProgress;
    ZSTD_bufferMode_e outBufferMode;
    ZSTD_outBuffer expectedOutBuffer;

    /* workspace */
    BYTE litBuffer[ZSTD_BLOCKSIZE_MAX + WILDCOPY_OVERLENGTH];
    BYTE headerBuffer[ZSTD_FRAMEHEADERSIZE_MAX];

    size_t oversizedDuration;

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    void const* dictContentBeginForFuzzing;
    void const* dictContentEndForFuzzing;
#endif

    /* Tracing */
};  /* typedef'd to ZSTD_DCtx within "zstd.h" */


/*-*******************************************************
 *  Shared internal functions
 *********************************************************/

/*! ZSTD_loadDEntropy() :
 *  dict : must point at beginning of a valid zstd dictionary.
 * @return : size of dictionary header (size of magic number + dict ID + entropy tables) */
size_t ZSTD_loadDEntropy(ZSTD_entropyDTables_t* entropy,
                   const void* const dict, size_t const dictSize);

/*! ZSTD_checkContinuity() :
 *  check if next `dst` follows previous position, where decompression ended.
 *  If yes, do nothing (continue on current segment).
 *  If not, classify previous segment as "external dictionary", and start a new segment.
 *  This function cannot fail. */
void ZSTD_checkContinuity(ZSTD_DCtx* dctx, const void* dst, size_t dstSize);


#endif /* ZSTD_DECOMPRESS_INTERNAL_H */
