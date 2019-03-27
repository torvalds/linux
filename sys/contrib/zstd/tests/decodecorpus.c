/*
 * Copyright (c) 2017-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "zstd.h"
#include "zstd_internal.h"
#include "mem.h"
#define ZDICT_STATIC_LINKING_ONLY
#include "zdict.h"

/* Direct access to internal compression functions is required */
#include "zstd_compress.c"

#define XXH_STATIC_LINKING_ONLY
#include "xxhash.h"     /* XXH64 */

#ifndef MIN
    #define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX_PATH
    #ifdef PATH_MAX
        #define MAX_PATH PATH_MAX
    #else
        #define MAX_PATH 256
    #endif
#endif

/*-************************************
*  DISPLAY Macros
**************************************/
#define DISPLAY(...)          fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...)  if (g_displayLevel>=l) { DISPLAY(__VA_ARGS__); }
static U32 g_displayLevel = 2;

#define DISPLAYUPDATE(...)                                                     \
    do {                                                                       \
        if ((UTIL_clockSpanMicro(g_displayClock) > g_refreshRate) ||           \
            (g_displayLevel >= 4)) {                                           \
            g_displayClock = UTIL_getTime();                                   \
            DISPLAY(__VA_ARGS__);                                              \
            if (g_displayLevel >= 4) fflush(stderr);                           \
        }                                                                      \
    } while (0)

static const U64 g_refreshRate = SEC_TO_MICRO / 6;
static UTIL_time_t g_displayClock = UTIL_TIME_INITIALIZER;

#define CHECKERR(code)                                                         \
    do {                                                                       \
        if (ZSTD_isError(code)) {                                              \
            DISPLAY("Error occurred while generating data: %s\n",              \
                    ZSTD_getErrorName(code));                                  \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/*-*******************************************************
*  Random function
*********************************************************/
static U32 RAND(U32* src)
{
#define RAND_rotl32(x,r) ((x << r) | (x >> (32 - r)))
    static const U32 prime1 = 2654435761U;
    static const U32 prime2 = 2246822519U;
    U32 rand32 = *src;
    rand32 *= prime1;
    rand32 += prime2;
    rand32  = RAND_rotl32(rand32, 13);
    *src = rand32;
    return RAND_rotl32(rand32, 27);
#undef RAND_rotl32
}

#define DISTSIZE (8192)

/* Write `size` bytes into `ptr`, all of which are less than or equal to `maxSymb` */
static void RAND_bufferMaxSymb(U32* seed, void* ptr, size_t size, int maxSymb)
{
    size_t i;
    BYTE* op = ptr;

    for (i = 0; i < size; i++) {
        op[i] = (BYTE) (RAND(seed) % (maxSymb + 1));
    }
}

/* Write `size` random bytes into `ptr` */
static void RAND_buffer(U32* seed, void* ptr, size_t size)
{
    size_t i;
    BYTE* op = ptr;

    for (i = 0; i + 4 <= size; i += 4) {
        MEM_writeLE32(op + i, RAND(seed));
    }
    for (; i < size; i++) {
        op[i] = RAND(seed) & 0xff;
    }
}

/* Write `size` bytes into `ptr` following the distribution `dist` */
static void RAND_bufferDist(U32* seed, BYTE* dist, void* ptr, size_t size)
{
    size_t i;
    BYTE* op = ptr;

    for (i = 0; i < size; i++) {
        op[i] = dist[RAND(seed) % DISTSIZE];
    }
}

/* Generate a random distribution where the frequency of each symbol follows a
 * geometric distribution defined by `weight`
 * `dist` should have size at least `DISTSIZE` */
static void RAND_genDist(U32* seed, BYTE* dist, double weight)
{
    size_t i = 0;
    size_t statesLeft = DISTSIZE;
    BYTE symb = (BYTE) (RAND(seed) % 256);
    BYTE step = (BYTE) ((RAND(seed) % 256) | 1); /* force it to be odd so it's relatively prime to 256 */

    while (i < DISTSIZE) {
        size_t states = ((size_t)(weight * statesLeft)) + 1;
        size_t j;
        for (j = 0; j < states && i < DISTSIZE; j++, i++) {
            dist[i] = symb;
        }

        symb += step;
        statesLeft -= states;
    }
}

/* Generates a random number in the range [min, max) */
static inline U32 RAND_range(U32* seed, U32 min, U32 max)
{
    return (RAND(seed) % (max-min)) + min;
}

#define ROUND(x) ((U32)(x + 0.5))

/* Generates a random number in an exponential distribution with mean `mean` */
static double RAND_exp(U32* seed, double mean)
{
    double const u = RAND(seed) / (double) UINT_MAX;
    return log(1-u) * (-mean);
}

/*-*******************************************************
*  Constants and Structs
*********************************************************/
const char *BLOCK_TYPES[] = {"raw", "rle", "compressed"};

#define MAX_DECOMPRESSED_SIZE_LOG 20
#define MAX_DECOMPRESSED_SIZE (1ULL << MAX_DECOMPRESSED_SIZE_LOG)

#define MAX_WINDOW_LOG 22 /* Recommended support is 8MB, so limit to 4MB + mantissa */

#define MIN_SEQ_LEN (3)
#define MAX_NB_SEQ ((ZSTD_BLOCKSIZE_MAX + MIN_SEQ_LEN - 1) / MIN_SEQ_LEN)

BYTE CONTENT_BUFFER[MAX_DECOMPRESSED_SIZE];
BYTE FRAME_BUFFER[MAX_DECOMPRESSED_SIZE * 2];
BYTE LITERAL_BUFFER[ZSTD_BLOCKSIZE_MAX];

seqDef SEQUENCE_BUFFER[MAX_NB_SEQ];
BYTE SEQUENCE_LITERAL_BUFFER[ZSTD_BLOCKSIZE_MAX]; /* storeSeq expects a place to copy literals to */
BYTE SEQUENCE_LLCODE[ZSTD_BLOCKSIZE_MAX];
BYTE SEQUENCE_MLCODE[ZSTD_BLOCKSIZE_MAX];
BYTE SEQUENCE_OFCODE[ZSTD_BLOCKSIZE_MAX];

unsigned WKSP[1024];

typedef struct {
    size_t contentSize; /* 0 means unknown (unless contentSize == windowSize == 0) */
    unsigned windowSize; /* contentSize >= windowSize means single segment */
} frameHeader_t;

/* For repeat modes */
typedef struct {
    U32 rep[ZSTD_REP_NUM];

    int hufInit;
    /* the distribution used in the previous block for repeat mode */
    BYTE hufDist[DISTSIZE];
    U32 hufTable [256]; /* HUF_CElt is an incomplete type */

    int fseInit;
    FSE_CTable offcodeCTable  [FSE_CTABLE_SIZE_U32(OffFSELog, MaxOff)];
    FSE_CTable matchlengthCTable[FSE_CTABLE_SIZE_U32(MLFSELog, MaxML)];
    FSE_CTable litlengthCTable  [FSE_CTABLE_SIZE_U32(LLFSELog, MaxLL)];

    /* Symbols that were present in the previous distribution, for use with
     * set_repeat */
    BYTE litlengthSymbolSet[36];
    BYTE offsetSymbolSet[29];
    BYTE matchlengthSymbolSet[53];
} cblockStats_t;

typedef struct {
    void* data;
    void* dataStart;
    void* dataEnd;

    void* src;
    void* srcStart;
    void* srcEnd;

    frameHeader_t header;

    cblockStats_t stats;
    cblockStats_t oldStats; /* so they can be rolled back if uncompressible */
} frame_t;

typedef struct {
    int useDict;
    U32 dictID;
    size_t dictContentSize;
    BYTE* dictContent;
} dictInfo;

typedef enum {
  gt_frame = 0,  /* generate frames */
  gt_block,      /* generate compressed blocks without block/frame headers */
} genType_e;

/*-*******************************************************
*  Global variables (set from command line)
*********************************************************/
U32 g_maxDecompressedSizeLog = MAX_DECOMPRESSED_SIZE_LOG;  /* <= 20 */
U32 g_maxBlockSize = ZSTD_BLOCKSIZE_MAX;                       /* <= 128 KB */

/*-*******************************************************
*  Generator Functions
*********************************************************/

struct {
    int contentSize; /* force the content size to be present */
} opts; /* advanced options on generation */

/* Generate and write a random frame header */
static void writeFrameHeader(U32* seed, frame_t* frame, dictInfo info)
{
    BYTE* const op = frame->data;
    size_t pos = 0;
    frameHeader_t fh;

    BYTE windowByte = 0;

    int singleSegment = 0;
    int contentSizeFlag = 0;
    int fcsCode = 0;

    memset(&fh, 0, sizeof(fh));

    /* generate window size */
    {
        /* Follow window algorithm from specification */
        int const exponent = RAND(seed) % (MAX_WINDOW_LOG - 10);
        int const mantissa = RAND(seed) % 8;
        windowByte = (BYTE) ((exponent << 3) | mantissa);
        fh.windowSize = (1U << (exponent + 10));
        fh.windowSize += fh.windowSize / 8 * mantissa;
    }

    {
        /* Generate random content size */
        size_t highBit;
        if (RAND(seed) & 7 && g_maxDecompressedSizeLog > 7) {
            /* do content of at least 128 bytes */
            highBit = 1ULL << RAND_range(seed, 7, g_maxDecompressedSizeLog);
        } else if (RAND(seed) & 3) {
            /* do small content */
            highBit = 1ULL << RAND_range(seed, 0, MIN(7, 1U << g_maxDecompressedSizeLog));
        } else {
            /* 0 size frame */
            highBit = 0;
        }
        fh.contentSize = highBit ? highBit + (RAND(seed) % highBit) : 0;

        /* provide size sometimes */
        contentSizeFlag = opts.contentSize | (RAND(seed) & 1);

        if (contentSizeFlag && (fh.contentSize == 0 || !(RAND(seed) & 7))) {
            /* do single segment sometimes */
            fh.windowSize = (U32) fh.contentSize;
            singleSegment = 1;
        }
    }

    if (contentSizeFlag) {
        /* Determine how large fcs field has to be */
        int minFcsCode = (fh.contentSize >= 256) +
                               (fh.contentSize >= 65536 + 256) +
                               (fh.contentSize > 0xFFFFFFFFU);
        if (!singleSegment && !minFcsCode) {
            minFcsCode = 1;
        }
        fcsCode = minFcsCode + (RAND(seed) % (4 - minFcsCode));
        if (fcsCode == 1 && fh.contentSize < 256) fcsCode++;
    }

    /* write out the header */
    MEM_writeLE32(op + pos, ZSTD_MAGICNUMBER);
    pos += 4;

    {
        /*
         * fcsCode: 2-bit flag specifying how many bytes used to represent Frame_Content_Size (bits 7-6)
         * singleSegment: 1-bit flag describing if data must be regenerated within a single continuous memory segment. (bit 5)
         * contentChecksumFlag: 1-bit flag that is set if frame includes checksum at the end -- set to 1 below (bit 2)
         * dictBits: 2-bit flag describing how many bytes Dictionary_ID uses -- set to 3 (bits 1-0)
         * For more information: https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md#frame_header
         */
        int const dictBits = info.useDict ? 3 : 0;
        BYTE const frameHeaderDescriptor =
                (BYTE) ((fcsCode << 6) | (singleSegment << 5) | (1 << 2) | dictBits);
        op[pos++] = frameHeaderDescriptor;
    }

    if (!singleSegment) {
        op[pos++] = windowByte;
    }
    if (info.useDict) {
        MEM_writeLE32(op + pos, (U32) info.dictID);
        pos += 4;
    }
    if (contentSizeFlag) {
        switch (fcsCode) {
        default: /* Impossible */
        case 0: op[pos++] = (BYTE) fh.contentSize; break;
        case 1: MEM_writeLE16(op + pos, (U16) (fh.contentSize - 256)); pos += 2; break;
        case 2: MEM_writeLE32(op + pos, (U32) fh.contentSize); pos += 4; break;
        case 3: MEM_writeLE64(op + pos, (U64) fh.contentSize); pos += 8; break;
        }
    }

    DISPLAYLEVEL(3, " frame content size:\t%u\n", (unsigned)fh.contentSize);
    DISPLAYLEVEL(3, " frame window size:\t%u\n", fh.windowSize);
    DISPLAYLEVEL(3, " content size flag:\t%d\n", contentSizeFlag);
    DISPLAYLEVEL(3, " single segment flag:\t%d\n", singleSegment);

    frame->data = op + pos;
    frame->header = fh;
}

/* Write a literal block in either raw or RLE form, return the literals size */
static size_t writeLiteralsBlockSimple(U32* seed, frame_t* frame, size_t contentSize)
{
    BYTE* op = (BYTE*)frame->data;
    int const type = RAND(seed) % 2;
    int const sizeFormatDesc = RAND(seed) % 8;
    size_t litSize;
    size_t maxLitSize = MIN(contentSize, g_maxBlockSize);

    if (sizeFormatDesc == 0) {
        /* Size_FormatDesc = ?0 */
        maxLitSize = MIN(maxLitSize, 31);
    } else if (sizeFormatDesc <= 4) {
        /* Size_FormatDesc = 01 */
        maxLitSize = MIN(maxLitSize, 4095);
    } else {
        /* Size_Format = 11 */
        maxLitSize = MIN(maxLitSize, 1048575);
    }

    litSize = RAND(seed) % (maxLitSize + 1);
    if (frame->src == frame->srcStart && litSize == 0) {
        litSize = 1; /* no empty literals if there's nothing preceding this block */
    }
    if (litSize + 3 > contentSize) {
        litSize = contentSize; /* no matches shorter than 3 are allowed */
    }
    /* use smallest size format that fits */
    if (litSize < 32) {
        op[0] = (type | (0 << 2) | (litSize << 3)) & 0xff;
        op += 1;
    } else if (litSize < 4096) {
        op[0] = (type | (1 << 2) | (litSize << 4)) & 0xff;
        op[1] = (litSize >> 4) & 0xff;
        op += 2;
    } else {
        op[0] = (type | (3 << 2) | (litSize << 4)) & 0xff;
        op[1] = (litSize >> 4) & 0xff;
        op[2] = (litSize >> 12) & 0xff;
        op += 3;
    }

    if (type == 0) {
        /* Raw literals */
        DISPLAYLEVEL(4, "   raw literals\n");

        RAND_buffer(seed, LITERAL_BUFFER, litSize);
        memcpy(op, LITERAL_BUFFER, litSize);
        op += litSize;
    } else {
        /* RLE literals */
        BYTE const symb = (BYTE) (RAND(seed) % 256);

        DISPLAYLEVEL(4, "   rle literals: 0x%02x\n", (unsigned)symb);

        memset(LITERAL_BUFFER, symb, litSize);
        op[0] = symb;
        op++;
    }

    frame->data = op;

    return litSize;
}

/* Generate a Huffman header for the given source */
static size_t writeHufHeader(U32* seed, HUF_CElt* hufTable, void* dst, size_t dstSize,
                                 const void* src, size_t srcSize)
{
    BYTE* const ostart = (BYTE*)dst;
    BYTE* op = ostart;

    unsigned huffLog = 11;
    unsigned maxSymbolValue = 255;

    unsigned count[HUF_SYMBOLVALUE_MAX+1];

    /* Scan input and build symbol stats */
    {   size_t const largest = HIST_count_wksp (count, &maxSymbolValue, (const BYTE*)src, srcSize, WKSP, sizeof(WKSP));
        assert(!HIST_isError(largest));
        if (largest == srcSize) { *ostart = ((const BYTE*)src)[0]; return 0; }   /* single symbol, rle */
        if (largest <= (srcSize >> 7)+1) return 0;   /* Fast heuristic : not compressible enough */
    }

    /* Build Huffman Tree */
    /* Max Huffman log is 11, min is highbit(maxSymbolValue)+1 */
    huffLog = RAND_range(seed, ZSTD_highbit32(maxSymbolValue)+1, huffLog+1);
    DISPLAYLEVEL(6, "     huffman log: %u\n", huffLog);
    {   size_t const maxBits = HUF_buildCTable_wksp (hufTable, count, maxSymbolValue, huffLog, WKSP, sizeof(WKSP));
        CHECKERR(maxBits);
        huffLog = (U32)maxBits;
    }

    /* Write table description header */
    {   size_t const hSize = HUF_writeCTable (op, dstSize, hufTable, maxSymbolValue, huffLog);
        if (hSize + 12 >= srcSize) return 0;   /* not useful to try compression */
        op += hSize;
    }

    return op - ostart;
}

/* Write a Huffman coded literals block and return the literals size */
static size_t writeLiteralsBlockCompressed(U32* seed, frame_t* frame, size_t contentSize)
{
    BYTE* origop = (BYTE*)frame->data;
    BYTE* opend = (BYTE*)frame->dataEnd;
    BYTE* op;
    BYTE* const ostart = origop;
    int const sizeFormat = RAND(seed) % 4;
    size_t litSize;
    size_t hufHeaderSize = 0;
    size_t compressedSize = 0;
    size_t maxLitSize = MIN(contentSize-3, g_maxBlockSize);

    symbolEncodingType_e hType;

    if (contentSize < 64) {
        /* make sure we get reasonably-sized literals for compression */
        return ERROR(GENERIC);
    }

    DISPLAYLEVEL(4, "   compressed literals\n");

    switch (sizeFormat) {
    case 0: /* fall through, size is the same as case 1 */
    case 1:
        maxLitSize = MIN(maxLitSize, 1023);
        origop += 3;
        break;
    case 2:
        maxLitSize = MIN(maxLitSize, 16383);
        origop += 4;
        break;
    case 3:
        maxLitSize = MIN(maxLitSize, 262143);
        origop += 5;
        break;
    default:; /* impossible */
    }

    do {
        op = origop;
        do {
            litSize = RAND(seed) % (maxLitSize + 1);
        } while (litSize < 32); /* avoid small literal sizes */
        if (litSize + 3 > contentSize) {
            litSize = contentSize; /* no matches shorter than 3 are allowed */
        }

        /* most of the time generate a new distribution */
        if ((RAND(seed) & 3) || !frame->stats.hufInit) {
            do {
                if (RAND(seed) & 3) {
                    /* add 10 to ensure some compressability */
                    double const weight = ((RAND(seed) % 90) + 10) / 100.0;

                    DISPLAYLEVEL(5, "    distribution weight: %d%%\n",
                                 (int)(weight * 100));

                    RAND_genDist(seed, frame->stats.hufDist, weight);
                } else {
                    /* sometimes do restricted range literals to force
                     * non-huffman headers */
                    DISPLAYLEVEL(5, "    small range literals\n");
                    RAND_bufferMaxSymb(seed, frame->stats.hufDist, DISTSIZE,
                                       15);
                }
                RAND_bufferDist(seed, frame->stats.hufDist, LITERAL_BUFFER,
                                litSize);

                /* generate the header from the distribution instead of the
                 * actual data to avoid bugs with symbols that were in the
                 * distribution but never showed up in the output */
                hufHeaderSize = writeHufHeader(
                        seed, (HUF_CElt*)frame->stats.hufTable, op, opend - op,
                        frame->stats.hufDist, DISTSIZE);
                CHECKERR(hufHeaderSize);
                /* repeat until a valid header is written */
            } while (hufHeaderSize == 0);
            op += hufHeaderSize;
            hType = set_compressed;

            frame->stats.hufInit = 1;
        } else {
            /* repeat the distribution/table from last time */
            DISPLAYLEVEL(5, "    huffman repeat stats\n");
            RAND_bufferDist(seed, frame->stats.hufDist, LITERAL_BUFFER,
                            litSize);
            hufHeaderSize = 0;
            hType = set_repeat;
        }

        do {
            compressedSize =
                    sizeFormat == 0
                            ? HUF_compress1X_usingCTable(
                                      op, opend - op, LITERAL_BUFFER, litSize,
                                      (HUF_CElt*)frame->stats.hufTable)
                            : HUF_compress4X_usingCTable(
                                      op, opend - op, LITERAL_BUFFER, litSize,
                                      (HUF_CElt*)frame->stats.hufTable);
            CHECKERR(compressedSize);
            /* this only occurs when it could not compress or similar */
        } while (compressedSize <= 0);

        op += compressedSize;

        compressedSize += hufHeaderSize;
        DISPLAYLEVEL(5, "    regenerated size: %u\n", (unsigned)litSize);
        DISPLAYLEVEL(5, "    compressed size: %u\n", (unsigned)compressedSize);
        if (compressedSize >= litSize) {
            DISPLAYLEVEL(5, "     trying again\n");
            /* if we have to try again, reset the stats so we don't accidentally
             * try to repeat a distribution we just made */
            frame->stats = frame->oldStats;
        } else {
            break;
        }
    } while (1);

    /* write header */
    switch (sizeFormat) {
    case 0: /* fall through, size is the same as case 1 */
    case 1: {
        U32 const header = hType | (sizeFormat << 2) | ((U32)litSize << 4) |
                           ((U32)compressedSize << 14);
        MEM_writeLE24(ostart, header);
        break;
    }
    case 2: {
        U32 const header = hType | (sizeFormat << 2) | ((U32)litSize << 4) |
                           ((U32)compressedSize << 18);
        MEM_writeLE32(ostart, header);
        break;
    }
    case 3: {
        U32 const header = hType | (sizeFormat << 2) | ((U32)litSize << 4) |
                           ((U32)compressedSize << 22);
        MEM_writeLE32(ostart, header);
        ostart[4] = (BYTE)(compressedSize >> 10);
        break;
    }
    default:; /* impossible */
    }

    frame->data = op;
    return litSize;
}

static size_t writeLiteralsBlock(U32* seed, frame_t* frame, size_t contentSize)
{
    /* only do compressed for larger segments to avoid compressibility issues */
    if (RAND(seed) & 7 && contentSize >= 64) {
        return writeLiteralsBlockCompressed(seed, frame, contentSize);
    } else {
        return writeLiteralsBlockSimple(seed, frame, contentSize);
    }
}

static inline void initSeqStore(seqStore_t *seqStore) {
    seqStore->maxNbSeq = MAX_NB_SEQ;
    seqStore->maxNbLit = ZSTD_BLOCKSIZE_MAX;
    seqStore->sequencesStart = SEQUENCE_BUFFER;
    seqStore->litStart = SEQUENCE_LITERAL_BUFFER;
    seqStore->llCode = SEQUENCE_LLCODE;
    seqStore->mlCode = SEQUENCE_MLCODE;
    seqStore->ofCode = SEQUENCE_OFCODE;

    ZSTD_resetSeqStore(seqStore);
}

/* Randomly generate sequence commands */
static U32 generateSequences(U32* seed, frame_t* frame, seqStore_t* seqStore,
                                size_t contentSize, size_t literalsSize, dictInfo info)
{
    /* The total length of all the matches */
    size_t const remainingMatch = contentSize - literalsSize;
    size_t excessMatch = 0;
    U32 numSequences = 0;

    U32 i;


    const BYTE* literals = LITERAL_BUFFER;
    BYTE* srcPtr = frame->src;

    if (literalsSize != contentSize) {
        /* each match must be at least MIN_SEQ_LEN, so this is the maximum
         * number of sequences we can have */
        U32 const maxSequences = (U32)remainingMatch / MIN_SEQ_LEN;
        numSequences = (RAND(seed) % maxSequences) + 1;

        /* the extra match lengths we have to allocate to each sequence */
        excessMatch = remainingMatch - numSequences * MIN_SEQ_LEN;
    }

    DISPLAYLEVEL(5, "    total match lengths: %u\n", (unsigned)remainingMatch);
    for (i = 0; i < numSequences; i++) {
        /* Generate match and literal lengths by exponential distribution to
         * ensure nice numbers */
        U32 matchLen =
                MIN_SEQ_LEN +
                ROUND(RAND_exp(seed, excessMatch / (double)(numSequences - i)));
        U32 literalLen =
                (RAND(seed) & 7)
                        ? ROUND(RAND_exp(seed,
                                         literalsSize /
                                                 (double)(numSequences - i)))
                        : 0;
        /* actual offset, code to send, and point to copy up to when shifting
         * codes in the repeat offsets history */
        U32 offset, offsetCode, repIndex;

        /* bounds checks */
        matchLen = (U32) MIN(matchLen, excessMatch + MIN_SEQ_LEN);
        literalLen = MIN(literalLen, (U32) literalsSize);
        if (i == 0 && srcPtr == frame->srcStart && literalLen == 0) literalLen = 1;
        if (i + 1 == numSequences) matchLen = MIN_SEQ_LEN + (U32) excessMatch;

        memcpy(srcPtr, literals, literalLen);
        srcPtr += literalLen;
        do {
            if (RAND(seed) & 7) {
                /* do a normal offset */
                U32 const dataDecompressed = (U32)((BYTE*)srcPtr-(BYTE*)frame->srcStart);
                offset = (RAND(seed) %
                          MIN(frame->header.windowSize,
                              (size_t)((BYTE*)srcPtr - (BYTE*)frame->srcStart))) +
                         1;
                if (info.useDict && (RAND(seed) & 1) && i + 1 != numSequences && dataDecompressed < frame->header.windowSize) {
                    /* need to occasionally generate offsets that go past the start */
                    /* including i+1 != numSequences because the last sequences has to adhere to predetermined contentSize */
                    U32 lenPastStart = (RAND(seed) % info.dictContentSize) + 1;
                    offset = (U32)((BYTE*)srcPtr - (BYTE*)frame->srcStart)+lenPastStart;
                    if (offset > frame->header.windowSize) {
                        if (lenPastStart < MIN_SEQ_LEN) {
                            /* when offset > windowSize, matchLen bound by end of dictionary (lenPastStart) */
                            /* this also means that lenPastStart must be greater than MIN_SEQ_LEN */
                            /* make sure lenPastStart does not go past dictionary start though */
                            lenPastStart = MIN(lenPastStart+MIN_SEQ_LEN, (U32)info.dictContentSize);
                            offset = (U32)((BYTE*)srcPtr - (BYTE*)frame->srcStart) + lenPastStart;
                        }
                        {
                            U32 const matchLenBound = MIN(frame->header.windowSize, lenPastStart);
                            matchLen = MIN(matchLen, matchLenBound);
                        }
                    }
                }
                offsetCode = offset + ZSTD_REP_MOVE;
                repIndex = 2;
            } else {
                /* do a repeat offset */
                offsetCode = RAND(seed) % 3;
                if (literalLen > 0) {
                    offset = frame->stats.rep[offsetCode];
                    repIndex = offsetCode;
                } else {
                    /* special case */
                    offset = offsetCode == 2 ? frame->stats.rep[0] - 1
                                           : frame->stats.rep[offsetCode + 1];
                    repIndex = MIN(2, offsetCode + 1);
                }
            }
        } while (((!info.useDict) && (offset > (size_t)((BYTE*)srcPtr - (BYTE*)frame->srcStart))) || offset == 0);

        {
            size_t j;
            BYTE* const dictEnd = info.dictContent + info.dictContentSize;
            for (j = 0; j < matchLen; j++) {
                if ((U32)((BYTE*)srcPtr - (BYTE*)frame->srcStart) < offset) {
                    /* copy from dictionary instead of literals */
                    size_t const dictOffset = offset - (srcPtr - (BYTE*)frame->srcStart);
                    *srcPtr = *(dictEnd - dictOffset);
                }
                else {
                    *srcPtr = *(srcPtr-offset);
                }
                srcPtr++;
            }
        }

        {   int r;
            for (r = repIndex; r > 0; r--) {
                frame->stats.rep[r] = frame->stats.rep[r - 1];
            }
            frame->stats.rep[0] = offset;
        }

        DISPLAYLEVEL(6, "      LL: %5u OF: %5u ML: %5u",
                    (unsigned)literalLen, (unsigned)offset, (unsigned)matchLen);
        DISPLAYLEVEL(7, " srcPos: %8u seqNb: %3u",
                     (unsigned)((BYTE*)srcPtr - (BYTE*)frame->srcStart), (unsigned)i);
        DISPLAYLEVEL(6, "\n");
        if (offsetCode < 3) {
            DISPLAYLEVEL(7, "        repeat offset: %d\n", (int)repIndex);
        }
        /* use libzstd sequence handling */
        ZSTD_storeSeq(seqStore, literalLen, literals, offsetCode,
                      matchLen - MINMATCH);

        literalsSize -= literalLen;
        excessMatch -= (matchLen - MIN_SEQ_LEN);
        literals += literalLen;
    }

    memcpy(srcPtr, literals, literalsSize);
    srcPtr += literalsSize;
    DISPLAYLEVEL(6, "      excess literals: %5u", (unsigned)literalsSize);
    DISPLAYLEVEL(7, " srcPos: %8u", (unsigned)((BYTE*)srcPtr - (BYTE*)frame->srcStart));
    DISPLAYLEVEL(6, "\n");

    return numSequences;
}

static void initSymbolSet(const BYTE* symbols, size_t len, BYTE* set, BYTE maxSymbolValue)
{
    size_t i;

    memset(set, 0, (size_t)maxSymbolValue+1);

    for (i = 0; i < len; i++) {
        set[symbols[i]] = 1;
    }
}

static int isSymbolSubset(const BYTE* symbols, size_t len, const BYTE* set, BYTE maxSymbolValue)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (symbols[i] > maxSymbolValue || !set[symbols[i]]) {
            return 0;
        }
    }
    return 1;
}

static size_t writeSequences(U32* seed, frame_t* frame, seqStore_t* seqStorePtr,
                             size_t nbSeq)
{
    /* This code is mostly copied from ZSTD_compressSequences in zstd_compress.c */
    unsigned count[MaxSeq+1];
    S16 norm[MaxSeq+1];
    FSE_CTable* CTable_LitLength = frame->stats.litlengthCTable;
    FSE_CTable* CTable_OffsetBits = frame->stats.offcodeCTable;
    FSE_CTable* CTable_MatchLength = frame->stats.matchlengthCTable;
    U32 LLtype, Offtype, MLtype;   /* compressed, raw or rle */
    const seqDef* const sequences = seqStorePtr->sequencesStart;
    const BYTE* const ofCodeTable = seqStorePtr->ofCode;
    const BYTE* const llCodeTable = seqStorePtr->llCode;
    const BYTE* const mlCodeTable = seqStorePtr->mlCode;
    BYTE* const oend = (BYTE*)frame->dataEnd;
    BYTE* op = (BYTE*)frame->data;
    BYTE* seqHead;
    BYTE scratchBuffer[1<<MAX(MLFSELog,LLFSELog)];

    /* literals compressing block removed so that can be done separately */

    /* Sequences Header */
    if ((oend-op) < 3 /*max nbSeq Size*/ + 1 /*seqHead */) return ERROR(dstSize_tooSmall);
    if (nbSeq < 0x7F) *op++ = (BYTE)nbSeq;
    else if (nbSeq < LONGNBSEQ) op[0] = (BYTE)((nbSeq>>8) + 0x80), op[1] = (BYTE)nbSeq, op+=2;
    else op[0]=0xFF, MEM_writeLE16(op+1, (U16)(nbSeq - LONGNBSEQ)), op+=3;

    if (nbSeq==0) {
        frame->data = op;
        return 0;
    }

    /* seqHead : flags for FSE encoding type */
    seqHead = op++;

    /* convert length/distances into codes */
    ZSTD_seqToCodes(seqStorePtr);

    /* CTable for Literal Lengths */
    {   unsigned max = MaxLL;
        size_t const mostFrequent = HIST_countFast_wksp(count, &max, llCodeTable, nbSeq, WKSP, sizeof(WKSP));   /* cannot fail */
        assert(!HIST_isError(mostFrequent));
        if (mostFrequent == nbSeq) {
            /* do RLE if we have the chance */
            *op++ = llCodeTable[0];
            FSE_buildCTable_rle(CTable_LitLength, (BYTE)max);
            LLtype = set_rle;
        } else if (frame->stats.fseInit && !(RAND(seed) & 3) &&
                   isSymbolSubset(llCodeTable, nbSeq,
                                  frame->stats.litlengthSymbolSet, 35)) {
            /* maybe do repeat mode if we're allowed to */
            LLtype = set_repeat;
        } else if (!(RAND(seed) & 3)) {
            /* maybe use the default distribution */
            FSE_buildCTable_wksp(CTable_LitLength, LL_defaultNorm, MaxLL, LL_defaultNormLog, scratchBuffer, sizeof(scratchBuffer));
            LLtype = set_basic;
        } else {
            /* fall back on a full table */
            size_t nbSeq_1 = nbSeq;
            const U32 tableLog = FSE_optimalTableLog(LLFSELog, nbSeq, max);
            if (count[llCodeTable[nbSeq-1]]>1) { count[llCodeTable[nbSeq-1]]--; nbSeq_1--; }
            FSE_normalizeCount(norm, tableLog, count, nbSeq_1, max);
            { size_t const NCountSize = FSE_writeNCount(op, oend-op, norm, max, tableLog);   /* overflow protected */
              if (FSE_isError(NCountSize)) return ERROR(GENERIC);
              op += NCountSize; }
            FSE_buildCTable_wksp(CTable_LitLength, norm, max, tableLog, scratchBuffer, sizeof(scratchBuffer));
            LLtype = set_compressed;
    }   }

    /* CTable for Offsets */
    /* see Literal Lengths for descriptions of mode choices */
    {   unsigned max = MaxOff;
        size_t const mostFrequent = HIST_countFast_wksp(count, &max, ofCodeTable, nbSeq, WKSP, sizeof(WKSP));   /* cannot fail */
        assert(!HIST_isError(mostFrequent));
        if (mostFrequent == nbSeq) {
            *op++ = ofCodeTable[0];
            FSE_buildCTable_rle(CTable_OffsetBits, (BYTE)max);
            Offtype = set_rle;
        } else if (frame->stats.fseInit && !(RAND(seed) & 3) &&
                   isSymbolSubset(ofCodeTable, nbSeq,
                                  frame->stats.offsetSymbolSet, 28)) {
            Offtype = set_repeat;
        } else if (!(RAND(seed) & 3)) {
            FSE_buildCTable_wksp(CTable_OffsetBits, OF_defaultNorm, DefaultMaxOff, OF_defaultNormLog, scratchBuffer, sizeof(scratchBuffer));
            Offtype = set_basic;
        } else {
            size_t nbSeq_1 = nbSeq;
            const U32 tableLog = FSE_optimalTableLog(OffFSELog, nbSeq, max);
            if (count[ofCodeTable[nbSeq-1]]>1) { count[ofCodeTable[nbSeq-1]]--; nbSeq_1--; }
            FSE_normalizeCount(norm, tableLog, count, nbSeq_1, max);
            { size_t const NCountSize = FSE_writeNCount(op, oend-op, norm, max, tableLog);   /* overflow protected */
              if (FSE_isError(NCountSize)) return ERROR(GENERIC);
              op += NCountSize; }
            FSE_buildCTable_wksp(CTable_OffsetBits, norm, max, tableLog, scratchBuffer, sizeof(scratchBuffer));
            Offtype = set_compressed;
    }   }

    /* CTable for MatchLengths */
    /* see Literal Lengths for descriptions of mode choices */
    {   unsigned max = MaxML;
        size_t const mostFrequent = HIST_countFast_wksp(count, &max, mlCodeTable, nbSeq, WKSP, sizeof(WKSP));   /* cannot fail */
        assert(!HIST_isError(mostFrequent));
        if (mostFrequent == nbSeq) {
            *op++ = *mlCodeTable;
            FSE_buildCTable_rle(CTable_MatchLength, (BYTE)max);
            MLtype = set_rle;
        } else if (frame->stats.fseInit && !(RAND(seed) & 3) &&
                   isSymbolSubset(mlCodeTable, nbSeq,
                                  frame->stats.matchlengthSymbolSet, 52)) {
            MLtype = set_repeat;
        } else if (!(RAND(seed) & 3)) {
            /* sometimes do default distribution */
            FSE_buildCTable_wksp(CTable_MatchLength, ML_defaultNorm, MaxML, ML_defaultNormLog, scratchBuffer, sizeof(scratchBuffer));
            MLtype = set_basic;
        } else {
            /* fall back on table */
            size_t nbSeq_1 = nbSeq;
            const U32 tableLog = FSE_optimalTableLog(MLFSELog, nbSeq, max);
            if (count[mlCodeTable[nbSeq-1]]>1) { count[mlCodeTable[nbSeq-1]]--; nbSeq_1--; }
            FSE_normalizeCount(norm, tableLog, count, nbSeq_1, max);
            { size_t const NCountSize = FSE_writeNCount(op, oend-op, norm, max, tableLog);   /* overflow protected */
              if (FSE_isError(NCountSize)) return ERROR(GENERIC);
              op += NCountSize; }
            FSE_buildCTable_wksp(CTable_MatchLength, norm, max, tableLog, scratchBuffer, sizeof(scratchBuffer));
            MLtype = set_compressed;
    }   }
    frame->stats.fseInit = 1;
    initSymbolSet(llCodeTable, nbSeq, frame->stats.litlengthSymbolSet, 35);
    initSymbolSet(ofCodeTable, nbSeq, frame->stats.offsetSymbolSet, 28);
    initSymbolSet(mlCodeTable, nbSeq, frame->stats.matchlengthSymbolSet, 52);

    DISPLAYLEVEL(5, "    LL type: %d OF type: %d ML type: %d\n", (unsigned)LLtype, (unsigned)Offtype, (unsigned)MLtype);

    *seqHead = (BYTE)((LLtype<<6) + (Offtype<<4) + (MLtype<<2));

    /* Encoding Sequences */
    {   BIT_CStream_t blockStream;
        FSE_CState_t  stateMatchLength;
        FSE_CState_t  stateOffsetBits;
        FSE_CState_t  stateLitLength;

        CHECK_E(BIT_initCStream(&blockStream, op, oend-op), dstSize_tooSmall); /* not enough space remaining */

        /* first symbols */
        FSE_initCState2(&stateMatchLength, CTable_MatchLength, mlCodeTable[nbSeq-1]);
        FSE_initCState2(&stateOffsetBits,  CTable_OffsetBits,  ofCodeTable[nbSeq-1]);
        FSE_initCState2(&stateLitLength,   CTable_LitLength,   llCodeTable[nbSeq-1]);
        BIT_addBits(&blockStream, sequences[nbSeq-1].litLength, LL_bits[llCodeTable[nbSeq-1]]);
        if (MEM_32bits()) BIT_flushBits(&blockStream);
        BIT_addBits(&blockStream, sequences[nbSeq-1].matchLength, ML_bits[mlCodeTable[nbSeq-1]]);
        if (MEM_32bits()) BIT_flushBits(&blockStream);
        BIT_addBits(&blockStream, sequences[nbSeq-1].offset, ofCodeTable[nbSeq-1]);
        BIT_flushBits(&blockStream);

        {   size_t n;
            for (n=nbSeq-2 ; n<nbSeq ; n--) {      /* intentional underflow */
                BYTE const llCode = llCodeTable[n];
                BYTE const ofCode = ofCodeTable[n];
                BYTE const mlCode = mlCodeTable[n];
                U32  const llBits = LL_bits[llCode];
                U32  const ofBits = ofCode;                                     /* 32b*/  /* 64b*/
                U32  const mlBits = ML_bits[mlCode];
                                                                                /* (7)*/  /* (7)*/
                FSE_encodeSymbol(&blockStream, &stateOffsetBits, ofCode);       /* 15 */  /* 15 */
                FSE_encodeSymbol(&blockStream, &stateMatchLength, mlCode);      /* 24 */  /* 24 */
                if (MEM_32bits()) BIT_flushBits(&blockStream);                  /* (7)*/
                FSE_encodeSymbol(&blockStream, &stateLitLength, llCode);        /* 16 */  /* 33 */
                if (MEM_32bits() || (ofBits+mlBits+llBits >= 64-7-(LLFSELog+MLFSELog+OffFSELog)))
                    BIT_flushBits(&blockStream);                                /* (7)*/
                BIT_addBits(&blockStream, sequences[n].litLength, llBits);
                if (MEM_32bits() && ((llBits+mlBits)>24)) BIT_flushBits(&blockStream);
                BIT_addBits(&blockStream, sequences[n].matchLength, mlBits);
                if (MEM_32bits()) BIT_flushBits(&blockStream);                  /* (7)*/
                BIT_addBits(&blockStream, sequences[n].offset, ofBits);         /* 31 */
                BIT_flushBits(&blockStream);                                    /* (7)*/
        }   }

        FSE_flushCState(&blockStream, &stateMatchLength);
        FSE_flushCState(&blockStream, &stateOffsetBits);
        FSE_flushCState(&blockStream, &stateLitLength);

        {   size_t const streamSize = BIT_closeCStream(&blockStream);
            if (streamSize==0) return ERROR(dstSize_tooSmall);   /* not enough space */
            op += streamSize;
    }   }

    frame->data = op;

    return 0;
}

static size_t writeSequencesBlock(U32* seed, frame_t* frame, size_t contentSize,
                                  size_t literalsSize, dictInfo info)
{
    seqStore_t seqStore;
    size_t numSequences;


    initSeqStore(&seqStore);

    /* randomly generate sequences */
    numSequences = generateSequences(seed, frame, &seqStore, contentSize, literalsSize, info);
    /* write them out to the frame data */
    CHECKERR(writeSequences(seed, frame, &seqStore, numSequences));

    return numSequences;
}

static size_t writeCompressedBlock(U32* seed, frame_t* frame, size_t contentSize, dictInfo info)
{
    BYTE* const blockStart = (BYTE*)frame->data;
    size_t literalsSize;
    size_t nbSeq;

    DISPLAYLEVEL(4, "  compressed block:\n");

    literalsSize = writeLiteralsBlock(seed, frame, contentSize);

    DISPLAYLEVEL(4, "   literals size: %u\n", (unsigned)literalsSize);

    nbSeq = writeSequencesBlock(seed, frame, contentSize, literalsSize, info);

    DISPLAYLEVEL(4, "   number of sequences: %u\n", (unsigned)nbSeq);

    return (BYTE*)frame->data - blockStart;
}

static void writeBlock(U32* seed, frame_t* frame, size_t contentSize,
                       int lastBlock, dictInfo info)
{
    int const blockTypeDesc = RAND(seed) % 8;
    size_t blockSize;
    int blockType;

    BYTE *const header = (BYTE*)frame->data;
    BYTE *op = header + 3;

    DISPLAYLEVEL(4, " block:\n");
    DISPLAYLEVEL(4, "  block content size: %u\n", (unsigned)contentSize);
    DISPLAYLEVEL(4, "  last block: %s\n", lastBlock ? "yes" : "no");

    if (blockTypeDesc == 0) {
        /* Raw data frame */

        RAND_buffer(seed, frame->src, contentSize);
        memcpy(op, frame->src, contentSize);

        op += contentSize;
        blockType = 0;
        blockSize = contentSize;
    } else if (blockTypeDesc == 1) {
        /* RLE */
        BYTE const symbol = RAND(seed) & 0xff;

        op[0] = symbol;
        memset(frame->src, symbol, contentSize);

        op++;
        blockType = 1;
        blockSize = contentSize;
    } else {
        /* compressed, most common */
        size_t compressedSize;
        blockType = 2;

        frame->oldStats = frame->stats;

        frame->data = op;
        compressedSize = writeCompressedBlock(seed, frame, contentSize, info);
        if (compressedSize >= contentSize) {   /* compressed block must be strictly smaller than uncompressed one */
            blockType = 0;
            memcpy(op, frame->src, contentSize);

            op += contentSize;
            blockSize = contentSize; /* fall back on raw block if data doesn't
                                        compress */

            frame->stats = frame->oldStats; /* don't update the stats */
        } else {
            op += compressedSize;
            blockSize = compressedSize;
        }
    }
    frame->src = (BYTE*)frame->src + contentSize;

    DISPLAYLEVEL(4, "  block type: %s\n", BLOCK_TYPES[blockType]);
    DISPLAYLEVEL(4, "  block size field: %u\n", (unsigned)blockSize);

    header[0] = (BYTE) ((lastBlock | (blockType << 1) | (blockSize << 3)) & 0xff);
    MEM_writeLE16(header + 1, (U16) (blockSize >> 5));

    frame->data = op;
}

static void writeBlocks(U32* seed, frame_t* frame, dictInfo info)
{
    size_t contentLeft = frame->header.contentSize;
    size_t const maxBlockSize = MIN(g_maxBlockSize, frame->header.windowSize);
    while (1) {
        /* 1 in 4 chance of ending frame */
        int const lastBlock = contentLeft > maxBlockSize ? 0 : !(RAND(seed) & 3);
        size_t blockContentSize;
        if (lastBlock) {
            blockContentSize = contentLeft;
        } else {
            if (contentLeft > 0 && (RAND(seed) & 7)) {
                /* some variable size block */
                blockContentSize = RAND(seed) % (MIN(maxBlockSize, contentLeft)+1);
            } else if (contentLeft > maxBlockSize && (RAND(seed) & 1)) {
                /* some full size block */
                blockContentSize = maxBlockSize;
            } else {
                /* some empty block */
                blockContentSize = 0;
            }
        }

        writeBlock(seed, frame, blockContentSize, lastBlock, info);

        contentLeft -= blockContentSize;
        if (lastBlock) break;
    }
}

static void writeChecksum(frame_t* frame)
{
    /* write checksum so implementations can verify their output */
    U64 digest = XXH64(frame->srcStart, (BYTE*)frame->src-(BYTE*)frame->srcStart, 0);
    DISPLAYLEVEL(3, "  checksum: %08x\n", (unsigned)digest);
    MEM_writeLE32(frame->data, (U32)digest);
    frame->data = (BYTE*)frame->data + 4;
}

static void outputBuffer(const void* buf, size_t size, const char* const path)
{
    /* write data out to file */
    const BYTE* ip = (const BYTE*)buf;
    FILE* out;
    if (path) {
        out = fopen(path, "wb");
    } else {
        out = stdout;
    }
    if (!out) {
        fprintf(stderr, "Failed to open file at %s: ", path);
        perror(NULL);
        exit(1);
    }

    {   size_t fsize = size;
        size_t written = 0;
        while (written < fsize) {
            written += fwrite(ip + written, 1, fsize - written, out);
            if (ferror(out)) {
                fprintf(stderr, "Failed to write to file at %s: ", path);
                perror(NULL);
                exit(1);
            }
        }
    }

    if (path) {
        fclose(out);
    }
}

static void initFrame(frame_t* fr)
{
    memset(fr, 0, sizeof(*fr));
    fr->data = fr->dataStart = FRAME_BUFFER;
    fr->dataEnd = FRAME_BUFFER + sizeof(FRAME_BUFFER);
    fr->src = fr->srcStart = CONTENT_BUFFER;
    fr->srcEnd = CONTENT_BUFFER + sizeof(CONTENT_BUFFER);

    /* init repeat codes */
    fr->stats.rep[0] = 1;
    fr->stats.rep[1] = 4;
    fr->stats.rep[2] = 8;
}

/**
 * Generated a single zstd compressed block with no block/frame header.
 * Returns the final seed.
 */
static U32 generateCompressedBlock(U32 seed, frame_t* frame, dictInfo info)
{
    size_t blockContentSize;
    int blockWritten = 0;
    BYTE* op;
    DISPLAYLEVEL(4, "block seed: %u\n", (unsigned)seed);
    initFrame(frame);
    op = (BYTE*)frame->data;

    while (!blockWritten) {
        size_t cSize;
        /* generate window size */
        {   int const exponent = RAND(&seed) % (MAX_WINDOW_LOG - 10);
            int const mantissa = RAND(&seed) % 8;
            frame->header.windowSize = (1U << (exponent + 10));
            frame->header.windowSize += (frame->header.windowSize / 8) * mantissa;
        }

        /* generate content size */
        {   size_t const maxBlockSize = MIN(g_maxBlockSize, frame->header.windowSize);
            if (RAND(&seed) & 15) {
                /* some full size blocks */
                blockContentSize = maxBlockSize;
            } else if (RAND(&seed) & 7 && g_maxBlockSize >= (1U << 7)) {
                /* some small blocks <= 128 bytes*/
                blockContentSize = RAND(&seed) % (1U << 7);
            } else {
                /* some variable size blocks */
                blockContentSize = RAND(&seed) % maxBlockSize;
            }
        }

        /* try generating a compressed block */
        frame->oldStats = frame->stats;
        frame->data = op;
        cSize = writeCompressedBlock(&seed, frame, blockContentSize, info);
        if (cSize >= blockContentSize) {  /* compressed size must be strictly smaller than decompressed size : https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md#blocks */
            /* data doesn't compress -- try again */
            frame->stats = frame->oldStats; /* don't update the stats */
            DISPLAYLEVEL(5, "   can't compress block : try again \n");
        } else {
            blockWritten = 1;
            DISPLAYLEVEL(4, "   block size: %u \n", (unsigned)cSize);
            frame->src = (BYTE*)frame->src + blockContentSize;
        }
    }
    return seed;
}

/* Return the final seed */
static U32 generateFrame(U32 seed, frame_t* fr, dictInfo info)
{
    /* generate a complete frame */
    DISPLAYLEVEL(3, "frame seed: %u\n", (unsigned)seed);
    initFrame(fr);

    writeFrameHeader(&seed, fr, info);
    writeBlocks(&seed, fr, info);
    writeChecksum(fr);

    return seed;
}

/*_*******************************************************
*  Dictionary Helper Functions
*********************************************************/
/* returns 0 if successful, otherwise returns 1 upon error */
static int genRandomDict(U32 dictID, U32 seed, size_t dictSize, BYTE* fullDict)
{
    /* allocate space for samples */
    int ret = 0;
    unsigned const numSamples = 4;
    size_t sampleSizes[4];
    BYTE* const samples = malloc(5000*sizeof(BYTE));
    if (samples == NULL) {
        DISPLAY("Error: could not allocate space for samples\n");
        return 1;
    }

    /* generate samples */
    {   unsigned literalValue = 1;
        unsigned samplesPos = 0;
        size_t currSize = 1;
        while (literalValue <= 4) {
            sampleSizes[literalValue - 1] = currSize;
            {   size_t k;
                for (k = 0; k < currSize; k++) {
                    *(samples + (samplesPos++)) = (BYTE)literalValue;
            }   }
            literalValue++;
            currSize *= 16;
    }   }

    {   size_t dictWriteSize = 0;
        ZDICT_params_t zdictParams;
        size_t const headerSize = MAX(dictSize/4, 256);
        size_t const dictContentSize = dictSize - headerSize;
        BYTE* const dictContent = fullDict + headerSize;
        if (dictContentSize < ZDICT_CONTENTSIZE_MIN || dictSize < ZDICT_DICTSIZE_MIN) {
            DISPLAY("Error: dictionary size is too small\n");
            ret = 1;
            goto exitGenRandomDict;
        }

        /* init dictionary params */
        memset(&zdictParams, 0, sizeof(zdictParams));
        zdictParams.dictID = dictID;
        zdictParams.notificationLevel = 1;

        /* fill in dictionary content */
        RAND_buffer(&seed, (void*)dictContent, dictContentSize);

        /* finalize dictionary with random samples */
        dictWriteSize = ZDICT_finalizeDictionary(fullDict, dictSize,
                                    dictContent, dictContentSize,
                                    samples, sampleSizes, numSamples,
                                    zdictParams);

        if (ZDICT_isError(dictWriteSize)) {
            DISPLAY("Could not finalize dictionary: %s\n", ZDICT_getErrorName(dictWriteSize));
            ret = 1;
        }
    }

exitGenRandomDict:
    free(samples);
    return ret;
}

static dictInfo initDictInfo(int useDict, size_t dictContentSize, BYTE* dictContent, U32 dictID){
    /* allocate space statically */
    dictInfo dictOp;
    memset(&dictOp, 0, sizeof(dictOp));
    dictOp.useDict = useDict;
    dictOp.dictContentSize = dictContentSize;
    dictOp.dictContent = dictContent;
    dictOp.dictID = dictID;
    return dictOp;
}

/*-*******************************************************
*  Test Mode
*********************************************************/

BYTE DECOMPRESSED_BUFFER[MAX_DECOMPRESSED_SIZE];

static size_t testDecodeSimple(frame_t* fr)
{
    /* test decoding the generated data with the simple API */
    size_t const ret = ZSTD_decompress(DECOMPRESSED_BUFFER, MAX_DECOMPRESSED_SIZE,
                           fr->dataStart, (BYTE*)fr->data - (BYTE*)fr->dataStart);

    if (ZSTD_isError(ret)) return ret;

    if (memcmp(DECOMPRESSED_BUFFER, fr->srcStart,
               (BYTE*)fr->src - (BYTE*)fr->srcStart) != 0) {
        return ERROR(corruption_detected);
    }

    return ret;
}

static size_t testDecodeStreaming(frame_t* fr)
{
    /* test decoding the generated data with the streaming API */
    ZSTD_DStream* zd = ZSTD_createDStream();
    ZSTD_inBuffer in;
    ZSTD_outBuffer out;
    size_t ret;

    if (!zd) return ERROR(memory_allocation);

    in.src = fr->dataStart;
    in.pos = 0;
    in.size = (BYTE*)fr->data - (BYTE*)fr->dataStart;

    out.dst = DECOMPRESSED_BUFFER;
    out.pos = 0;
    out.size = ZSTD_DStreamOutSize();

    ZSTD_initDStream(zd);
    while (1) {
        ret = ZSTD_decompressStream(zd, &out, &in);
        if (ZSTD_isError(ret)) goto cleanup; /* error */
        if (ret == 0) break; /* frame is done */

        /* force decoding to be done in chunks */
        out.size += MIN(ZSTD_DStreamOutSize(), MAX_DECOMPRESSED_SIZE - out.size);
    }

    ret = out.pos;

    if (memcmp(out.dst, fr->srcStart, out.pos) != 0) {
        return ERROR(corruption_detected);
    }

cleanup:
    ZSTD_freeDStream(zd);
    return ret;
}

static size_t testDecodeWithDict(U32 seed, genType_e genType)
{
    /* create variables */
    size_t const dictSize = RAND(&seed) % (10 << 20) + ZDICT_DICTSIZE_MIN + ZDICT_CONTENTSIZE_MIN;
    U32 const dictID = RAND(&seed);
    size_t errorDetected = 0;
    BYTE* const fullDict = malloc(dictSize);
    if (fullDict == NULL) {
        return ERROR(GENERIC);
    }

    /* generate random dictionary */
    if (genRandomDict(dictID, seed, dictSize, fullDict)) {  /* return 0 on success */
        errorDetected = ERROR(GENERIC);
        goto dictTestCleanup;
    }


    {   frame_t fr;
        dictInfo info;
        ZSTD_DCtx* const dctx = ZSTD_createDCtx();
        size_t ret;

        /* get dict info */
        {   size_t const headerSize = MAX(dictSize/4, 256);
            size_t const dictContentSize = dictSize-headerSize;
            BYTE* const dictContent = fullDict+headerSize;
            info = initDictInfo(1, dictContentSize, dictContent, dictID);
        }

        /* manually decompress and check difference */
        if (genType == gt_frame) {
            /* Test frame */
            generateFrame(seed, &fr, info);
            ret = ZSTD_decompress_usingDict(dctx, DECOMPRESSED_BUFFER, MAX_DECOMPRESSED_SIZE,
                                            fr.dataStart, (BYTE*)fr.data - (BYTE*)fr.dataStart,
                                            fullDict, dictSize);
        } else {
            /* Test block */
            generateCompressedBlock(seed, &fr, info);
            ret = ZSTD_decompressBegin_usingDict(dctx, fullDict, dictSize);
            if (ZSTD_isError(ret)) {
                errorDetected = ret;
                ZSTD_freeDCtx(dctx);
                goto dictTestCleanup;
            }
            ret = ZSTD_decompressBlock(dctx, DECOMPRESSED_BUFFER, MAX_DECOMPRESSED_SIZE,
                                       fr.dataStart, (BYTE*)fr.data - (BYTE*)fr.dataStart);
        }
        ZSTD_freeDCtx(dctx);

        if (ZSTD_isError(ret)) {
            errorDetected = ret;
            goto dictTestCleanup;
        }

        if (memcmp(DECOMPRESSED_BUFFER, fr.srcStart, (BYTE*)fr.src - (BYTE*)fr.srcStart) != 0) {
            errorDetected = ERROR(corruption_detected);
            goto dictTestCleanup;
        }
    }

dictTestCleanup:
    free(fullDict);
    return errorDetected;
}

static size_t testDecodeRawBlock(frame_t* fr)
{
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    size_t ret = ZSTD_decompressBegin(dctx);
    if (ZSTD_isError(ret)) return ret;

    ret = ZSTD_decompressBlock(
            dctx,
            DECOMPRESSED_BUFFER, MAX_DECOMPRESSED_SIZE,
            fr->dataStart, (BYTE*)fr->data - (BYTE*)fr->dataStart);
    ZSTD_freeDCtx(dctx);
    if (ZSTD_isError(ret)) return ret;

    if (memcmp(DECOMPRESSED_BUFFER, fr->srcStart,
               (BYTE*)fr->src - (BYTE*)fr->srcStart) != 0) {
        return ERROR(corruption_detected);
    }

    return ret;
}

static int runBlockTest(U32* seed)
{
    frame_t fr;
    U32 const seedCopy = *seed;
    {   dictInfo const info = initDictInfo(0, 0, NULL, 0);
        *seed = generateCompressedBlock(*seed, &fr, info);
    }

    {   size_t const r = testDecodeRawBlock(&fr);
        if (ZSTD_isError(r)) {
            DISPLAY("Error in block mode on test seed %u: %s\n",
                    (unsigned)seedCopy, ZSTD_getErrorName(r));
            return 1;
        }
    }

    {   size_t const r = testDecodeWithDict(*seed, gt_block);
        if (ZSTD_isError(r)) {
            DISPLAY("Error in block mode with dictionary on test seed %u: %s\n",
                    (unsigned)seedCopy, ZSTD_getErrorName(r));
            return 1;
        }
    }
    return 0;
}

static int runFrameTest(U32* seed)
{
    frame_t fr;
    U32 const seedCopy = *seed;
    {   dictInfo const info = initDictInfo(0, 0, NULL, 0);
        *seed = generateFrame(*seed, &fr, info);
    }

    {   size_t const r = testDecodeSimple(&fr);
        if (ZSTD_isError(r)) {
            DISPLAY("Error in simple mode on test seed %u: %s\n",
                    (unsigned)seedCopy, ZSTD_getErrorName(r));
            return 1;
        }
    }
    {   size_t const r = testDecodeStreaming(&fr);
        if (ZSTD_isError(r)) {
            DISPLAY("Error in streaming mode on test seed %u: %s\n",
                    (unsigned)seedCopy, ZSTD_getErrorName(r));
            return 1;
        }
    }
    {   size_t const r = testDecodeWithDict(*seed, gt_frame);  /* avoid big dictionaries */
        if (ZSTD_isError(r)) {
            DISPLAY("Error in dictionary mode on test seed %u: %s\n",
                    (unsigned)seedCopy, ZSTD_getErrorName(r));
            return 1;
        }
    }
    return 0;
}

static int runTestMode(U32 seed, unsigned numFiles, unsigned const testDurationS,
                       genType_e genType)
{
    unsigned fnum;

    UTIL_time_t const startClock = UTIL_getTime();
    U64 const maxClockSpan = testDurationS * SEC_TO_MICRO;

    if (numFiles == 0 && !testDurationS) numFiles = 1;

    DISPLAY("seed: %u\n", (unsigned)seed);

    for (fnum = 0; fnum < numFiles || UTIL_clockSpanMicro(startClock) < maxClockSpan; fnum++) {
        if (fnum < numFiles)
            DISPLAYUPDATE("\r%u/%u        ", fnum, numFiles);
        else
            DISPLAYUPDATE("\r%u           ", fnum);

        {   int const ret = (genType == gt_frame) ?
                            runFrameTest(&seed) :
                            runBlockTest(&seed);
            if (ret) return ret;
        }
    }

    DISPLAY("\r%u tests completed: ", fnum);
    DISPLAY("OK\n");

    return 0;
}

/*-*******************************************************
*  File I/O
*********************************************************/

static int generateFile(U32 seed, const char* const path,
                        const char* const origPath, genType_e genType)
{
    frame_t fr;

    DISPLAY("seed: %u\n", (unsigned)seed);

    {   dictInfo const info = initDictInfo(0, 0, NULL, 0);
        if (genType == gt_frame) {
            generateFrame(seed, &fr, info);
        } else {
            generateCompressedBlock(seed, &fr, info);
        }
    }
    outputBuffer(fr.dataStart, (BYTE*)fr.data - (BYTE*)fr.dataStart, path);
    if (origPath) {
        outputBuffer(fr.srcStart, (BYTE*)fr.src - (BYTE*)fr.srcStart, origPath);
    }
    return 0;
}

static int generateCorpus(U32 seed, unsigned numFiles, const char* const path,
                          const char* const origPath, genType_e genType)
{
    char outPath[MAX_PATH];
    unsigned fnum;

    DISPLAY("seed: %u\n", (unsigned)seed);

    for (fnum = 0; fnum < numFiles; fnum++) {
        frame_t fr;

        DISPLAYUPDATE("\r%u/%u        ", fnum, numFiles);

        {   dictInfo const info = initDictInfo(0, 0, NULL, 0);
            if (genType == gt_frame) {
                seed = generateFrame(seed, &fr, info);
            } else {
                seed = generateCompressedBlock(seed, &fr, info);
            }
        }

        if (snprintf(outPath, MAX_PATH, "%s/z%06u.zst", path, fnum) + 1 > MAX_PATH) {
            DISPLAY("Error: path too long\n");
            return 1;
        }
        outputBuffer(fr.dataStart, (BYTE*)fr.data - (BYTE*)fr.dataStart, outPath);

        if (origPath) {
            if (snprintf(outPath, MAX_PATH, "%s/z%06u", origPath, fnum) + 1 > MAX_PATH) {
                DISPLAY("Error: path too long\n");
                return 1;
            }
            outputBuffer(fr.srcStart, (BYTE*)fr.src - (BYTE*)fr.srcStart, outPath);
        }
    }

    DISPLAY("\r%u/%u      \n", fnum, numFiles);

    return 0;
}

static int generateCorpusWithDict(U32 seed, unsigned numFiles, const char* const path,
                                  const char* const origPath, const size_t dictSize,
                                  genType_e genType)
{
    char outPath[MAX_PATH];
    BYTE* fullDict;
    U32 const dictID = RAND(&seed);
    int errorDetected = 0;

    if (snprintf(outPath, MAX_PATH, "%s/dictionary", path) + 1 > MAX_PATH) {
        DISPLAY("Error: path too long\n");
        return 1;
    }

    /* allocate space for the dictionary */
    fullDict = malloc(dictSize);
    if (fullDict == NULL) {
        DISPLAY("Error: could not allocate space for full dictionary.\n");
        return 1;
    }

    /* randomly generate the dictionary */
    {   int const ret = genRandomDict(dictID, seed, dictSize, fullDict);
        if (ret != 0) {
            errorDetected = ret;
            goto dictCleanup;
        }
    }

    /* write out dictionary */
    if (numFiles != 0) {
        if (snprintf(outPath, MAX_PATH, "%s/dictionary", path) + 1 > MAX_PATH) {
            DISPLAY("Error: dictionary path too long\n");
            errorDetected = 1;
            goto dictCleanup;
        }
        outputBuffer(fullDict, dictSize, outPath);
    }
    else {
        outputBuffer(fullDict, dictSize, "dictionary");
    }

    /* generate random compressed/decompressed files */
    {   unsigned fnum;
        for (fnum = 0; fnum < MAX(numFiles, 1); fnum++) {
            frame_t fr;
            DISPLAYUPDATE("\r%u/%u        ", fnum, numFiles);
            {
                size_t const headerSize = MAX(dictSize/4, 256);
                size_t const dictContentSize = dictSize-headerSize;
                BYTE* const dictContent = fullDict+headerSize;
                dictInfo const info = initDictInfo(1, dictContentSize, dictContent, dictID);
                if (genType == gt_frame) {
                    seed = generateFrame(seed, &fr, info);
                } else {
                    seed = generateCompressedBlock(seed, &fr, info);
                }
            }

            if (numFiles != 0) {
                if (snprintf(outPath, MAX_PATH, "%s/z%06u.zst", path, fnum) + 1 > MAX_PATH) {
                    DISPLAY("Error: path too long\n");
                    errorDetected = 1;
                    goto dictCleanup;
                }
                outputBuffer(fr.dataStart, (BYTE*)fr.data - (BYTE*)fr.dataStart, outPath);

                if (origPath) {
                    if (snprintf(outPath, MAX_PATH, "%s/z%06u", origPath, fnum) + 1 > MAX_PATH) {
                        DISPLAY("Error: path too long\n");
                        errorDetected = 1;
                        goto dictCleanup;
                    }
                    outputBuffer(fr.srcStart, (BYTE*)fr.src - (BYTE*)fr.srcStart, outPath);
                }
            }
            else {
                outputBuffer(fr.dataStart, (BYTE*)fr.data - (BYTE*)fr.dataStart, path);
                if (origPath) {
                    outputBuffer(fr.srcStart, (BYTE*)fr.src - (BYTE*)fr.srcStart, origPath);
                }
            }
        }
    }

dictCleanup:
    free(fullDict);
    return errorDetected;
}


/*_*******************************************************
*  Command line
*********************************************************/
static U32 makeSeed(void)
{
    U32 t = (U32) time(NULL);
    return XXH32(&t, sizeof(t), 0) % 65536;
}

static unsigned readInt(const char** argument)
{
    unsigned val = 0;
    while ((**argument>='0') && (**argument<='9')) {
        val *= 10;
        val += **argument - '0';
        (*argument)++;
    }
    return val;
}

static void usage(const char* programName)
{
    DISPLAY( "Usage :\n");
    DISPLAY( "      %s [args]\n", programName);
    DISPLAY( "\n");
    DISPLAY( "Arguments :\n");
    DISPLAY( " -p<path> : select output path (default:stdout)\n");
    DISPLAY( "                in multiple files mode this should be a directory\n");
    DISPLAY( " -o<path> : select path to output original file (default:no output)\n");
    DISPLAY( "                in multiple files mode this should be a directory\n");
    DISPLAY( " -s#      : select seed (default:random based on time)\n");
    DISPLAY( " -n#      : number of files to generate (default:1)\n");
    DISPLAY( " -t       : activate test mode (test files against libzstd instead of outputting them)\n");
    DISPLAY( " -T#      : length of time to run tests for\n");
    DISPLAY( " -v       : increase verbosity level (default:0, max:7)\n");
    DISPLAY( " -h/H     : display help/long help and exit\n");
}

static void advancedUsage(const char* programName)
{
    usage(programName);
    DISPLAY( "\n");
    DISPLAY( "Advanced arguments        :\n");
    DISPLAY( " --content-size           : always include the content size in the frame header\n");
    DISPLAY( " --use-dict=#             : include a dictionary used to decompress the corpus\n");
    DISPLAY( " --gen-blocks             : generate raw compressed blocks without block/frame headers\n");
    DISPLAY( " --max-block-size-log=#   : max block size log, must be in range [2, 17]\n");
    DISPLAY( " --max-content-size-log=# : max content size log, must be <= 20\n");
    DISPLAY( "                            (this is ignored with gen-blocks)\n");
}

/*! readU32FromChar() :
    @return : unsigned integer value read from input in `char` format
    allows and interprets K, KB, KiB, M, MB and MiB suffix.
    Will also modify `*stringPtr`, advancing it to position where it stopped reading.
    Note : function result can overflow if digit string > MAX_UINT */
static unsigned readU32FromChar(const char** stringPtr)
{
    unsigned result = 0;
    while ((**stringPtr >='0') && (**stringPtr <='9'))
        result *= 10, result += **stringPtr - '0', (*stringPtr)++ ;
    if ((**stringPtr=='K') || (**stringPtr=='M')) {
        result <<= 10;
        if (**stringPtr=='M') result <<= 10;
        (*stringPtr)++ ;
        if (**stringPtr=='i') (*stringPtr)++;
        if (**stringPtr=='B') (*stringPtr)++;
    }
    return result;
}

/** longCommandWArg() :
 *  check if *stringPtr is the same as longCommand.
 *  If yes, @return 1 and advances *stringPtr to the position which immediately follows longCommand.
 *  @return 0 and doesn't modify *stringPtr otherwise.
 */
static unsigned longCommandWArg(const char** stringPtr, const char* longCommand)
{
    size_t const comSize = strlen(longCommand);
    int const result = !strncmp(*stringPtr, longCommand, comSize);
    if (result) *stringPtr += comSize;
    return result;
}

int main(int argc, char** argv)
{
    U32 seed = 0;
    int seedset = 0;
    unsigned numFiles = 0;
    unsigned testDuration = 0;
    int testMode = 0;
    const char* path = NULL;
    const char* origPath = NULL;
    int useDict = 0;
    unsigned dictSize = (10 << 10); /* 10 kB default */
    genType_e genType = gt_frame;

    int argNb;

    /* Check command line */
    for (argNb=1; argNb<argc; argNb++) {
        const char* argument = argv[argNb];
        if(!argument) continue;   /* Protection if argument empty */

        /* Handle commands. Aggregated commands are allowed */
        if (argument[0]=='-') {
            argument++;
            while (*argument!=0) {
                switch(*argument)
                {
                case 'h':
                    usage(argv[0]);
                    return 0;
                case 'H':
                    advancedUsage(argv[0]);
                    return 0;
                case 'v':
                    argument++;
                    g_displayLevel++;
                    break;
                case 's':
                    argument++;
                    seedset=1;
                    seed = readInt(&argument);
                    break;
                case 'n':
                    argument++;
                    numFiles = readInt(&argument);
                    break;
                case 'T':
                    argument++;
                    testDuration = readInt(&argument);
                    if (*argument == 'm') {
                        testDuration *= 60;
                        argument++;
                        if (*argument == 'n') argument++;
                    }
                    break;
                case 'o':
                    argument++;
                    origPath = argument;
                    argument += strlen(argument);
                    break;
                case 'p':
                    argument++;
                    path = argument;
                    argument += strlen(argument);
                    break;
                case 't':
                    argument++;
                    testMode = 1;
                    break;
                case '-':
                    argument++;
                    if (strcmp(argument, "content-size") == 0) {
                        opts.contentSize = 1;
                    } else if (longCommandWArg(&argument, "use-dict=")) {
                        dictSize = readU32FromChar(&argument);
                        useDict = 1;
                    } else if (strcmp(argument, "gen-blocks") == 0) {
                        genType = gt_block;
                    } else if (longCommandWArg(&argument, "max-block-size-log=")) {
                        U32 value = readU32FromChar(&argument);
                        if (value >= 2 && value <= ZSTD_BLOCKSIZE_MAX) {
                            g_maxBlockSize = 1U << value;
                        }
                    } else if (longCommandWArg(&argument, "max-content-size-log=")) {
                        U32 value = readU32FromChar(&argument);
                        g_maxDecompressedSizeLog =
                                MIN(MAX_DECOMPRESSED_SIZE_LOG, value);
                    } else {
                        advancedUsage(argv[0]);
                        return 1;
                    }
                    argument += strlen(argument);
                    break;
                default:
                    usage(argv[0]);
                    return 1;
    }   }   }   }   /* for (argNb=1; argNb<argc; argNb++) */

    if (!seedset) {
        seed = makeSeed();
    }

    if (testMode) {
        return runTestMode(seed, numFiles, testDuration, genType);
    } else {
        if (testDuration) {
            DISPLAY("Error: -T requires test mode (-t)\n\n");
            usage(argv[0]);
            return 1;
        }
    }

    if (!path) {
        DISPLAY("Error: path is required in file generation mode\n");
        usage(argv[0]);
        return 1;
    }

    if (numFiles == 0 && useDict == 0) {
        return generateFile(seed, path, origPath, genType);
    } else if (useDict == 0){
        return generateCorpus(seed, numFiles, path, origPath, genType);
    } else {
        /* should generate files with a dictionary */
        return generateCorpusWithDict(seed, numFiles, path, origPath, dictSize, genType);
    }

}
