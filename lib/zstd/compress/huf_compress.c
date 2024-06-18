/* ******************************************************************
 * Huffman encoder, part of New Generation Entropy library
 * Copyright (c) Yann Collet, Facebook, Inc.
 *
 *  You can contact the author at :
 *  - FSE+HUF source repository : https://github.com/Cyan4973/FiniteStateEntropy
 *  - Public forum : https://groups.google.com/forum/#!forum/lz4c
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
****************************************************************** */

/* **************************************************************
*  Compiler specifics
****************************************************************/


/* **************************************************************
*  Includes
****************************************************************/
#include "../common/zstd_deps.h"     /* ZSTD_memcpy, ZSTD_memset */
#include "../common/compiler.h"
#include "../common/bitstream.h"
#include "hist.h"
#define FSE_STATIC_LINKING_ONLY   /* FSE_optimalTableLog_internal */
#include "../common/fse.h"        /* header compression */
#define HUF_STATIC_LINKING_ONLY
#include "../common/huf.h"
#include "../common/error_private.h"


/* **************************************************************
*  Error Management
****************************************************************/
#define HUF_isError ERR_isError
#define HUF_STATIC_ASSERT(c) DEBUG_STATIC_ASSERT(c)   /* use only *after* variable declarations */


/* **************************************************************
*  Utils
****************************************************************/
unsigned HUF_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue)
{
    return FSE_optimalTableLog_internal(maxTableLog, srcSize, maxSymbolValue, 1);
}


/* *******************************************************
*  HUF : Huffman block compression
*********************************************************/
#define HUF_WORKSPACE_MAX_ALIGNMENT 8

static void* HUF_alignUpWorkspace(void* workspace, size_t* workspaceSizePtr, size_t align)
{
    size_t const mask = align - 1;
    size_t const rem = (size_t)workspace & mask;
    size_t const add = (align - rem) & mask;
    BYTE* const aligned = (BYTE*)workspace + add;
    assert((align & (align - 1)) == 0); /* pow 2 */
    assert(align <= HUF_WORKSPACE_MAX_ALIGNMENT);
    if (*workspaceSizePtr >= add) {
        assert(add < align);
        assert(((size_t)aligned & mask) == 0);
        *workspaceSizePtr -= add;
        return aligned;
    } else {
        *workspaceSizePtr = 0;
        return NULL;
    }
}


/* HUF_compressWeights() :
 * Same as FSE_compress(), but dedicated to huff0's weights compression.
 * The use case needs much less stack memory.
 * Note : all elements within weightTable are supposed to be <= HUF_TABLELOG_MAX.
 */
#define MAX_FSE_TABLELOG_FOR_HUFF_HEADER 6

typedef struct {
    FSE_CTable CTable[FSE_CTABLE_SIZE_U32(MAX_FSE_TABLELOG_FOR_HUFF_HEADER, HUF_TABLELOG_MAX)];
    U32 scratchBuffer[FSE_BUILD_CTABLE_WORKSPACE_SIZE_U32(HUF_TABLELOG_MAX, MAX_FSE_TABLELOG_FOR_HUFF_HEADER)];
    unsigned count[HUF_TABLELOG_MAX+1];
    S16 norm[HUF_TABLELOG_MAX+1];
} HUF_CompressWeightsWksp;

static size_t HUF_compressWeights(void* dst, size_t dstSize, const void* weightTable, size_t wtSize, void* workspace, size_t workspaceSize)
{
    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const oend = ostart + dstSize;

    unsigned maxSymbolValue = HUF_TABLELOG_MAX;
    U32 tableLog = MAX_FSE_TABLELOG_FOR_HUFF_HEADER;
    HUF_CompressWeightsWksp* wksp = (HUF_CompressWeightsWksp*)HUF_alignUpWorkspace(workspace, &workspaceSize, ZSTD_ALIGNOF(U32));

    if (workspaceSize < sizeof(HUF_CompressWeightsWksp)) return ERROR(GENERIC);

    /* init conditions */
    if (wtSize <= 1) return 0;  /* Not compressible */

    /* Scan input and build symbol stats */
    {   unsigned const maxCount = HIST_count_simple(wksp->count, &maxSymbolValue, weightTable, wtSize);   /* never fails */
        if (maxCount == wtSize) return 1;   /* only a single symbol in src : rle */
        if (maxCount == 1) return 0;        /* each symbol present maximum once => not compressible */
    }

    tableLog = FSE_optimalTableLog(tableLog, wtSize, maxSymbolValue);
    CHECK_F( FSE_normalizeCount(wksp->norm, tableLog, wksp->count, wtSize, maxSymbolValue, /* useLowProbCount */ 0) );

    /* Write table description header */
    {   CHECK_V_F(hSize, FSE_writeNCount(op, (size_t)(oend-op), wksp->norm, maxSymbolValue, tableLog) );
        op += hSize;
    }

    /* Compress */
    CHECK_F( FSE_buildCTable_wksp(wksp->CTable, wksp->norm, maxSymbolValue, tableLog, wksp->scratchBuffer, sizeof(wksp->scratchBuffer)) );
    {   CHECK_V_F(cSize, FSE_compress_usingCTable(op, (size_t)(oend - op), weightTable, wtSize, wksp->CTable) );
        if (cSize == 0) return 0;   /* not enough space for compressed data */
        op += cSize;
    }

    return (size_t)(op-ostart);
}

static size_t HUF_getNbBits(HUF_CElt elt)
{
    return elt & 0xFF;
}

static size_t HUF_getNbBitsFast(HUF_CElt elt)
{
    return elt;
}

static size_t HUF_getValue(HUF_CElt elt)
{
    return elt & ~0xFF;
}

static size_t HUF_getValueFast(HUF_CElt elt)
{
    return elt;
}

static void HUF_setNbBits(HUF_CElt* elt, size_t nbBits)
{
    assert(nbBits <= HUF_TABLELOG_ABSOLUTEMAX);
    *elt = nbBits;
}

static void HUF_setValue(HUF_CElt* elt, size_t value)
{
    size_t const nbBits = HUF_getNbBits(*elt);
    if (nbBits > 0) {
        assert((value >> nbBits) == 0);
        *elt |= value << (sizeof(HUF_CElt) * 8 - nbBits);
    }
}

typedef struct {
    HUF_CompressWeightsWksp wksp;
    BYTE bitsToWeight[HUF_TABLELOG_MAX + 1];   /* precomputed conversion table */
    BYTE huffWeight[HUF_SYMBOLVALUE_MAX];
} HUF_WriteCTableWksp;

size_t HUF_writeCTable_wksp(void* dst, size_t maxDstSize,
                            const HUF_CElt* CTable, unsigned maxSymbolValue, unsigned huffLog,
                            void* workspace, size_t workspaceSize)
{
    HUF_CElt const* const ct = CTable + 1;
    BYTE* op = (BYTE*)dst;
    U32 n;
    HUF_WriteCTableWksp* wksp = (HUF_WriteCTableWksp*)HUF_alignUpWorkspace(workspace, &workspaceSize, ZSTD_ALIGNOF(U32));

    /* check conditions */
    if (workspaceSize < sizeof(HUF_WriteCTableWksp)) return ERROR(GENERIC);
    if (maxSymbolValue > HUF_SYMBOLVALUE_MAX) return ERROR(maxSymbolValue_tooLarge);

    /* convert to weight */
    wksp->bitsToWeight[0] = 0;
    for (n=1; n<huffLog+1; n++)
        wksp->bitsToWeight[n] = (BYTE)(huffLog + 1 - n);
    for (n=0; n<maxSymbolValue; n++)
        wksp->huffWeight[n] = wksp->bitsToWeight[HUF_getNbBits(ct[n])];

    /* attempt weights compression by FSE */
    if (maxDstSize < 1) return ERROR(dstSize_tooSmall);
    {   CHECK_V_F(hSize, HUF_compressWeights(op+1, maxDstSize-1, wksp->huffWeight, maxSymbolValue, &wksp->wksp, sizeof(wksp->wksp)) );
        if ((hSize>1) & (hSize < maxSymbolValue/2)) {   /* FSE compressed */
            op[0] = (BYTE)hSize;
            return hSize+1;
    }   }

    /* write raw values as 4-bits (max : 15) */
    if (maxSymbolValue > (256-128)) return ERROR(GENERIC);   /* should not happen : likely means source cannot be compressed */
    if (((maxSymbolValue+1)/2) + 1 > maxDstSize) return ERROR(dstSize_tooSmall);   /* not enough space within dst buffer */
    op[0] = (BYTE)(128 /*special case*/ + (maxSymbolValue-1));
    wksp->huffWeight[maxSymbolValue] = 0;   /* to be sure it doesn't cause msan issue in final combination */
    for (n=0; n<maxSymbolValue; n+=2)
        op[(n/2)+1] = (BYTE)((wksp->huffWeight[n] << 4) + wksp->huffWeight[n+1]);
    return ((maxSymbolValue+1)/2) + 1;
}

/*! HUF_writeCTable() :
    `CTable` : Huffman tree to save, using huf representation.
    @return : size of saved CTable */
size_t HUF_writeCTable (void* dst, size_t maxDstSize,
                        const HUF_CElt* CTable, unsigned maxSymbolValue, unsigned huffLog)
{
    HUF_WriteCTableWksp wksp;
    return HUF_writeCTable_wksp(dst, maxDstSize, CTable, maxSymbolValue, huffLog, &wksp, sizeof(wksp));
}


size_t HUF_readCTable (HUF_CElt* CTable, unsigned* maxSymbolValuePtr, const void* src, size_t srcSize, unsigned* hasZeroWeights)
{
    BYTE huffWeight[HUF_SYMBOLVALUE_MAX + 1];   /* init not required, even though some static analyzer may complain */
    U32 rankVal[HUF_TABLELOG_ABSOLUTEMAX + 1];   /* large enough for values from 0 to 16 */
    U32 tableLog = 0;
    U32 nbSymbols = 0;
    HUF_CElt* const ct = CTable + 1;

    /* get symbol weights */
    CHECK_V_F(readSize, HUF_readStats(huffWeight, HUF_SYMBOLVALUE_MAX+1, rankVal, &nbSymbols, &tableLog, src, srcSize));
    *hasZeroWeights = (rankVal[0] > 0);

    /* check result */
    if (tableLog > HUF_TABLELOG_MAX) return ERROR(tableLog_tooLarge);
    if (nbSymbols > *maxSymbolValuePtr+1) return ERROR(maxSymbolValue_tooSmall);

    CTable[0] = tableLog;

    /* Prepare base value per rank */
    {   U32 n, nextRankStart = 0;
        for (n=1; n<=tableLog; n++) {
            U32 curr = nextRankStart;
            nextRankStart += (rankVal[n] << (n-1));
            rankVal[n] = curr;
    }   }

    /* fill nbBits */
    {   U32 n; for (n=0; n<nbSymbols; n++) {
            const U32 w = huffWeight[n];
            HUF_setNbBits(ct + n, (BYTE)(tableLog + 1 - w) & -(w != 0));
    }   }

    /* fill val */
    {   U16 nbPerRank[HUF_TABLELOG_MAX+2]  = {0};  /* support w=0=>n=tableLog+1 */
        U16 valPerRank[HUF_TABLELOG_MAX+2] = {0};
        { U32 n; for (n=0; n<nbSymbols; n++) nbPerRank[HUF_getNbBits(ct[n])]++; }
        /* determine stating value per rank */
        valPerRank[tableLog+1] = 0;   /* for w==0 */
        {   U16 min = 0;
            U32 n; for (n=tableLog; n>0; n--) {  /* start at n=tablelog <-> w=1 */
                valPerRank[n] = min;     /* get starting value within each rank */
                min += nbPerRank[n];
                min >>= 1;
        }   }
        /* assign value within rank, symbol order */
        { U32 n; for (n=0; n<nbSymbols; n++) HUF_setValue(ct + n, valPerRank[HUF_getNbBits(ct[n])]++); }
    }

    *maxSymbolValuePtr = nbSymbols - 1;
    return readSize;
}

U32 HUF_getNbBitsFromCTable(HUF_CElt const* CTable, U32 symbolValue)
{
    const HUF_CElt* ct = CTable + 1;
    assert(symbolValue <= HUF_SYMBOLVALUE_MAX);
    return (U32)HUF_getNbBits(ct[symbolValue]);
}


typedef struct nodeElt_s {
    U32 count;
    U16 parent;
    BYTE byte;
    BYTE nbBits;
} nodeElt;

/*
 * HUF_setMaxHeight():
 * Enforces maxNbBits on the Huffman tree described in huffNode.
 *
 * It sets all nodes with nbBits > maxNbBits to be maxNbBits. Then it adjusts
 * the tree to so that it is a valid canonical Huffman tree.
 *
 * @pre               The sum of the ranks of each symbol == 2^largestBits,
 *                    where largestBits == huffNode[lastNonNull].nbBits.
 * @post              The sum of the ranks of each symbol == 2^largestBits,
 *                    where largestBits is the return value <= maxNbBits.
 *
 * @param huffNode    The Huffman tree modified in place to enforce maxNbBits.
 * @param lastNonNull The symbol with the lowest count in the Huffman tree.
 * @param maxNbBits   The maximum allowed number of bits, which the Huffman tree
 *                    may not respect. After this function the Huffman tree will
 *                    respect maxNbBits.
 * @return            The maximum number of bits of the Huffman tree after adjustment,
 *                    necessarily no more than maxNbBits.
 */
static U32 HUF_setMaxHeight(nodeElt* huffNode, U32 lastNonNull, U32 maxNbBits)
{
    const U32 largestBits = huffNode[lastNonNull].nbBits;
    /* early exit : no elt > maxNbBits, so the tree is already valid. */
    if (largestBits <= maxNbBits) return largestBits;

    /* there are several too large elements (at least >= 2) */
    {   int totalCost = 0;
        const U32 baseCost = 1 << (largestBits - maxNbBits);
        int n = (int)lastNonNull;

        /* Adjust any ranks > maxNbBits to maxNbBits.
         * Compute totalCost, which is how far the sum of the ranks is
         * we are over 2^largestBits after adjust the offending ranks.
         */
        while (huffNode[n].nbBits > maxNbBits) {
            totalCost += baseCost - (1 << (largestBits - huffNode[n].nbBits));
            huffNode[n].nbBits = (BYTE)maxNbBits;
            n--;
        }
        /* n stops at huffNode[n].nbBits <= maxNbBits */
        assert(huffNode[n].nbBits <= maxNbBits);
        /* n end at index of smallest symbol using < maxNbBits */
        while (huffNode[n].nbBits == maxNbBits) --n;

        /* renorm totalCost from 2^largestBits to 2^maxNbBits
         * note : totalCost is necessarily a multiple of baseCost */
        assert((totalCost & (baseCost - 1)) == 0);
        totalCost >>= (largestBits - maxNbBits);
        assert(totalCost > 0);

        /* repay normalized cost */
        {   U32 const noSymbol = 0xF0F0F0F0;
            U32 rankLast[HUF_TABLELOG_MAX+2];

            /* Get pos of last (smallest = lowest cum. count) symbol per rank */
            ZSTD_memset(rankLast, 0xF0, sizeof(rankLast));
            {   U32 currentNbBits = maxNbBits;
                int pos;
                for (pos=n ; pos >= 0; pos--) {
                    if (huffNode[pos].nbBits >= currentNbBits) continue;
                    currentNbBits = huffNode[pos].nbBits;   /* < maxNbBits */
                    rankLast[maxNbBits-currentNbBits] = (U32)pos;
            }   }

            while (totalCost > 0) {
                /* Try to reduce the next power of 2 above totalCost because we
                 * gain back half the rank.
                 */
                U32 nBitsToDecrease = BIT_highbit32((U32)totalCost) + 1;
                for ( ; nBitsToDecrease > 1; nBitsToDecrease--) {
                    U32 const highPos = rankLast[nBitsToDecrease];
                    U32 const lowPos = rankLast[nBitsToDecrease-1];
                    if (highPos == noSymbol) continue;
                    /* Decrease highPos if no symbols of lowPos or if it is
                     * not cheaper to remove 2 lowPos than highPos.
                     */
                    if (lowPos == noSymbol) break;
                    {   U32 const highTotal = huffNode[highPos].count;
                        U32 const lowTotal = 2 * huffNode[lowPos].count;
                        if (highTotal <= lowTotal) break;
                }   }
                /* only triggered when no more rank 1 symbol left => find closest one (note : there is necessarily at least one !) */
                assert(rankLast[nBitsToDecrease] != noSymbol || nBitsToDecrease == 1);
                /* HUF_MAX_TABLELOG test just to please gcc 5+; but it should not be necessary */
                while ((nBitsToDecrease<=HUF_TABLELOG_MAX) && (rankLast[nBitsToDecrease] == noSymbol))
                    nBitsToDecrease++;
                assert(rankLast[nBitsToDecrease] != noSymbol);
                /* Increase the number of bits to gain back half the rank cost. */
                totalCost -= 1 << (nBitsToDecrease-1);
                huffNode[rankLast[nBitsToDecrease]].nbBits++;

                /* Fix up the new rank.
                 * If the new rank was empty, this symbol is now its smallest.
                 * Otherwise, this symbol will be the largest in the new rank so no adjustment.
                 */
                if (rankLast[nBitsToDecrease-1] == noSymbol)
                    rankLast[nBitsToDecrease-1] = rankLast[nBitsToDecrease];
                /* Fix up the old rank.
                 * If the symbol was at position 0, meaning it was the highest weight symbol in the tree,
                 * it must be the only symbol in its rank, so the old rank now has no symbols.
                 * Otherwise, since the Huffman nodes are sorted by count, the previous position is now
                 * the smallest node in the rank. If the previous position belongs to a different rank,
                 * then the rank is now empty.
                 */
                if (rankLast[nBitsToDecrease] == 0)    /* special case, reached largest symbol */
                    rankLast[nBitsToDecrease] = noSymbol;
                else {
                    rankLast[nBitsToDecrease]--;
                    if (huffNode[rankLast[nBitsToDecrease]].nbBits != maxNbBits-nBitsToDecrease)
                        rankLast[nBitsToDecrease] = noSymbol;   /* this rank is now empty */
                }
            }   /* while (totalCost > 0) */

            /* If we've removed too much weight, then we have to add it back.
             * To avoid overshooting again, we only adjust the smallest rank.
             * We take the largest nodes from the lowest rank 0 and move them
             * to rank 1. There's guaranteed to be enough rank 0 symbols because
             * TODO.
             */
            while (totalCost < 0) {  /* Sometimes, cost correction overshoot */
                /* special case : no rank 1 symbol (using maxNbBits-1);
                 * let's create one from largest rank 0 (using maxNbBits).
                 */
                if (rankLast[1] == noSymbol) {
                    while (huffNode[n].nbBits == maxNbBits) n--;
                    huffNode[n+1].nbBits--;
                    assert(n >= 0);
                    rankLast[1] = (U32)(n+1);
                    totalCost++;
                    continue;
                }
                huffNode[ rankLast[1] + 1 ].nbBits--;
                rankLast[1]++;
                totalCost ++;
            }
        }   /* repay normalized cost */
    }   /* there are several too large elements (at least >= 2) */

    return maxNbBits;
}

typedef struct {
    U16 base;
    U16 curr;
} rankPos;

typedef nodeElt huffNodeTable[HUF_CTABLE_WORKSPACE_SIZE_U32];

/* Number of buckets available for HUF_sort() */
#define RANK_POSITION_TABLE_SIZE 192

typedef struct {
  huffNodeTable huffNodeTbl;
  rankPos rankPosition[RANK_POSITION_TABLE_SIZE];
} HUF_buildCTable_wksp_tables;

/* RANK_POSITION_DISTINCT_COUNT_CUTOFF == Cutoff point in HUF_sort() buckets for which we use log2 bucketing.
 * Strategy is to use as many buckets as possible for representing distinct
 * counts while using the remainder to represent all "large" counts.
 *
 * To satisfy this requirement for 192 buckets, we can do the following:
 * Let buckets 0-166 represent distinct counts of [0, 166]
 * Let buckets 166 to 192 represent all remaining counts up to RANK_POSITION_MAX_COUNT_LOG using log2 bucketing.
 */
#define RANK_POSITION_MAX_COUNT_LOG 32
#define RANK_POSITION_LOG_BUCKETS_BEGIN (RANK_POSITION_TABLE_SIZE - 1) - RANK_POSITION_MAX_COUNT_LOG - 1 /* == 158 */
#define RANK_POSITION_DISTINCT_COUNT_CUTOFF RANK_POSITION_LOG_BUCKETS_BEGIN + BIT_highbit32(RANK_POSITION_LOG_BUCKETS_BEGIN) /* == 166 */

/* Return the appropriate bucket index for a given count. See definition of
 * RANK_POSITION_DISTINCT_COUNT_CUTOFF for explanation of bucketing strategy.
 */
static U32 HUF_getIndex(U32 const count) {
    return (count < RANK_POSITION_DISTINCT_COUNT_CUTOFF)
        ? count
        : BIT_highbit32(count) + RANK_POSITION_LOG_BUCKETS_BEGIN;
}

/* Helper swap function for HUF_quickSortPartition() */
static void HUF_swapNodes(nodeElt* a, nodeElt* b) {
	nodeElt tmp = *a;
	*a = *b;
	*b = tmp;
}

/* Returns 0 if the huffNode array is not sorted by descending count */
MEM_STATIC int HUF_isSorted(nodeElt huffNode[], U32 const maxSymbolValue1) {
    U32 i;
    for (i = 1; i < maxSymbolValue1; ++i) {
        if (huffNode[i].count > huffNode[i-1].count) {
            return 0;
        }
    }
    return 1;
}

/* Insertion sort by descending order */
HINT_INLINE void HUF_insertionSort(nodeElt huffNode[], int const low, int const high) {
    int i;
    int const size = high-low+1;
    huffNode += low;
    for (i = 1; i < size; ++i) {
        nodeElt const key = huffNode[i];
        int j = i - 1;
        while (j >= 0 && huffNode[j].count < key.count) {
            huffNode[j + 1] = huffNode[j];
            j--;
        }
        huffNode[j + 1] = key;
    }
}

/* Pivot helper function for quicksort. */
static int HUF_quickSortPartition(nodeElt arr[], int const low, int const high) {
    /* Simply select rightmost element as pivot. "Better" selectors like
     * median-of-three don't experimentally appear to have any benefit.
     */
    U32 const pivot = arr[high].count;
    int i = low - 1;
    int j = low;
    for ( ; j < high; j++) {
        if (arr[j].count > pivot) {
            i++;
            HUF_swapNodes(&arr[i], &arr[j]);
        }
    }
    HUF_swapNodes(&arr[i + 1], &arr[high]);
    return i + 1;
}

/* Classic quicksort by descending with partially iterative calls
 * to reduce worst case callstack size.
 */
static void HUF_simpleQuickSort(nodeElt arr[], int low, int high) {
    int const kInsertionSortThreshold = 8;
    if (high - low < kInsertionSortThreshold) {
        HUF_insertionSort(arr, low, high);
        return;
    }
    while (low < high) {
        int const idx = HUF_quickSortPartition(arr, low, high);
        if (idx - low < high - idx) {
            HUF_simpleQuickSort(arr, low, idx - 1);
            low = idx + 1;
        } else {
            HUF_simpleQuickSort(arr, idx + 1, high);
            high = idx - 1;
        }
    }
}

/*
 * HUF_sort():
 * Sorts the symbols [0, maxSymbolValue] by count[symbol] in decreasing order.
 * This is a typical bucket sorting strategy that uses either quicksort or insertion sort to sort each bucket.
 *
 * @param[out] huffNode       Sorted symbols by decreasing count. Only members `.count` and `.byte` are filled.
 *                            Must have (maxSymbolValue + 1) entries.
 * @param[in]  count          Histogram of the symbols.
 * @param[in]  maxSymbolValue Maximum symbol value.
 * @param      rankPosition   This is a scratch workspace. Must have RANK_POSITION_TABLE_SIZE entries.
 */
static void HUF_sort(nodeElt huffNode[], const unsigned count[], U32 const maxSymbolValue, rankPos rankPosition[]) {
    U32 n;
    U32 const maxSymbolValue1 = maxSymbolValue+1;

    /* Compute base and set curr to base.
     * For symbol s let lowerRank = HUF_getIndex(count[n]) and rank = lowerRank + 1.
     * See HUF_getIndex to see bucketing strategy.
     * We attribute each symbol to lowerRank's base value, because we want to know where
     * each rank begins in the output, so for rank R we want to count ranks R+1 and above.
     */
    ZSTD_memset(rankPosition, 0, sizeof(*rankPosition) * RANK_POSITION_TABLE_SIZE);
    for (n = 0; n < maxSymbolValue1; ++n) {
        U32 lowerRank = HUF_getIndex(count[n]);
        assert(lowerRank < RANK_POSITION_TABLE_SIZE - 1);
        rankPosition[lowerRank].base++;
    }

    assert(rankPosition[RANK_POSITION_TABLE_SIZE - 1].base == 0);
    /* Set up the rankPosition table */
    for (n = RANK_POSITION_TABLE_SIZE - 1; n > 0; --n) {
        rankPosition[n-1].base += rankPosition[n].base;
        rankPosition[n-1].curr = rankPosition[n-1].base;
    }

    /* Insert each symbol into their appropriate bucket, setting up rankPosition table. */
    for (n = 0; n < maxSymbolValue1; ++n) {
        U32 const c = count[n];
        U32 const r = HUF_getIndex(c) + 1;
        U32 const pos = rankPosition[r].curr++;
        assert(pos < maxSymbolValue1);
        huffNode[pos].count = c;
        huffNode[pos].byte  = (BYTE)n;
    }

    /* Sort each bucket. */
    for (n = RANK_POSITION_DISTINCT_COUNT_CUTOFF; n < RANK_POSITION_TABLE_SIZE - 1; ++n) {
        U32 const bucketSize = rankPosition[n].curr-rankPosition[n].base;
        U32 const bucketStartIdx = rankPosition[n].base;
        if (bucketSize > 1) {
            assert(bucketStartIdx < maxSymbolValue1);
            HUF_simpleQuickSort(huffNode + bucketStartIdx, 0, bucketSize-1);
        }
    }

    assert(HUF_isSorted(huffNode, maxSymbolValue1));
}

/* HUF_buildCTable_wksp() :
 *  Same as HUF_buildCTable(), but using externally allocated scratch buffer.
 *  `workSpace` must be aligned on 4-bytes boundaries, and be at least as large as sizeof(HUF_buildCTable_wksp_tables).
 */
#define STARTNODE (HUF_SYMBOLVALUE_MAX+1)

/* HUF_buildTree():
 * Takes the huffNode array sorted by HUF_sort() and builds an unlimited-depth Huffman tree.
 *
 * @param huffNode        The array sorted by HUF_sort(). Builds the Huffman tree in this array.
 * @param maxSymbolValue  The maximum symbol value.
 * @return                The smallest node in the Huffman tree (by count).
 */
static int HUF_buildTree(nodeElt* huffNode, U32 maxSymbolValue)
{
    nodeElt* const huffNode0 = huffNode - 1;
    int nonNullRank;
    int lowS, lowN;
    int nodeNb = STARTNODE;
    int n, nodeRoot;
    /* init for parents */
    nonNullRank = (int)maxSymbolValue;
    while(huffNode[nonNullRank].count == 0) nonNullRank--;
    lowS = nonNullRank; nodeRoot = nodeNb + lowS - 1; lowN = nodeNb;
    huffNode[nodeNb].count = huffNode[lowS].count + huffNode[lowS-1].count;
    huffNode[lowS].parent = huffNode[lowS-1].parent = (U16)nodeNb;
    nodeNb++; lowS-=2;
    for (n=nodeNb; n<=nodeRoot; n++) huffNode[n].count = (U32)(1U<<30);
    huffNode0[0].count = (U32)(1U<<31);  /* fake entry, strong barrier */

    /* create parents */
    while (nodeNb <= nodeRoot) {
        int const n1 = (huffNode[lowS].count < huffNode[lowN].count) ? lowS-- : lowN++;
        int const n2 = (huffNode[lowS].count < huffNode[lowN].count) ? lowS-- : lowN++;
        huffNode[nodeNb].count = huffNode[n1].count + huffNode[n2].count;
        huffNode[n1].parent = huffNode[n2].parent = (U16)nodeNb;
        nodeNb++;
    }

    /* distribute weights (unlimited tree height) */
    huffNode[nodeRoot].nbBits = 0;
    for (n=nodeRoot-1; n>=STARTNODE; n--)
        huffNode[n].nbBits = huffNode[ huffNode[n].parent ].nbBits + 1;
    for (n=0; n<=nonNullRank; n++)
        huffNode[n].nbBits = huffNode[ huffNode[n].parent ].nbBits + 1;

    return nonNullRank;
}

/*
 * HUF_buildCTableFromTree():
 * Build the CTable given the Huffman tree in huffNode.
 *
 * @param[out] CTable         The output Huffman CTable.
 * @param      huffNode       The Huffman tree.
 * @param      nonNullRank    The last and smallest node in the Huffman tree.
 * @param      maxSymbolValue The maximum symbol value.
 * @param      maxNbBits      The exact maximum number of bits used in the Huffman tree.
 */
static void HUF_buildCTableFromTree(HUF_CElt* CTable, nodeElt const* huffNode, int nonNullRank, U32 maxSymbolValue, U32 maxNbBits)
{
    HUF_CElt* const ct = CTable + 1;
    /* fill result into ctable (val, nbBits) */
    int n;
    U16 nbPerRank[HUF_TABLELOG_MAX+1] = {0};
    U16 valPerRank[HUF_TABLELOG_MAX+1] = {0};
    int const alphabetSize = (int)(maxSymbolValue + 1);
    for (n=0; n<=nonNullRank; n++)
        nbPerRank[huffNode[n].nbBits]++;
    /* determine starting value per rank */
    {   U16 min = 0;
        for (n=(int)maxNbBits; n>0; n--) {
            valPerRank[n] = min;      /* get starting value within each rank */
            min += nbPerRank[n];
            min >>= 1;
    }   }
    for (n=0; n<alphabetSize; n++)
        HUF_setNbBits(ct + huffNode[n].byte, huffNode[n].nbBits);   /* push nbBits per symbol, symbol order */
    for (n=0; n<alphabetSize; n++)
        HUF_setValue(ct + n, valPerRank[HUF_getNbBits(ct[n])]++);   /* assign value within rank, symbol order */
    CTable[0] = maxNbBits;
}

size_t HUF_buildCTable_wksp (HUF_CElt* CTable, const unsigned* count, U32 maxSymbolValue, U32 maxNbBits, void* workSpace, size_t wkspSize)
{
    HUF_buildCTable_wksp_tables* const wksp_tables = (HUF_buildCTable_wksp_tables*)HUF_alignUpWorkspace(workSpace, &wkspSize, ZSTD_ALIGNOF(U32));
    nodeElt* const huffNode0 = wksp_tables->huffNodeTbl;
    nodeElt* const huffNode = huffNode0+1;
    int nonNullRank;

    /* safety checks */
    if (wkspSize < sizeof(HUF_buildCTable_wksp_tables))
      return ERROR(workSpace_tooSmall);
    if (maxNbBits == 0) maxNbBits = HUF_TABLELOG_DEFAULT;
    if (maxSymbolValue > HUF_SYMBOLVALUE_MAX)
      return ERROR(maxSymbolValue_tooLarge);
    ZSTD_memset(huffNode0, 0, sizeof(huffNodeTable));

    /* sort, decreasing order */
    HUF_sort(huffNode, count, maxSymbolValue, wksp_tables->rankPosition);

    /* build tree */
    nonNullRank = HUF_buildTree(huffNode, maxSymbolValue);

    /* enforce maxTableLog */
    maxNbBits = HUF_setMaxHeight(huffNode, (U32)nonNullRank, maxNbBits);
    if (maxNbBits > HUF_TABLELOG_MAX) return ERROR(GENERIC);   /* check fit into table */

    HUF_buildCTableFromTree(CTable, huffNode, nonNullRank, maxSymbolValue, maxNbBits);

    return maxNbBits;
}

size_t HUF_estimateCompressedSize(const HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue)
{
    HUF_CElt const* ct = CTable + 1;
    size_t nbBits = 0;
    int s;
    for (s = 0; s <= (int)maxSymbolValue; ++s) {
        nbBits += HUF_getNbBits(ct[s]) * count[s];
    }
    return nbBits >> 3;
}

int HUF_validateCTable(const HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue) {
  HUF_CElt const* ct = CTable + 1;
  int bad = 0;
  int s;
  for (s = 0; s <= (int)maxSymbolValue; ++s) {
    bad |= (count[s] != 0) & (HUF_getNbBits(ct[s]) == 0);
  }
  return !bad;
}

size_t HUF_compressBound(size_t size) { return HUF_COMPRESSBOUND(size); }

/* HUF_CStream_t:
 * Huffman uses its own BIT_CStream_t implementation.
 * There are three major differences from BIT_CStream_t:
 *   1. HUF_addBits() takes a HUF_CElt (size_t) which is
 *      the pair (nbBits, value) in the format:
 *      format:
 *        - Bits [0, 4)            = nbBits
 *        - Bits [4, 64 - nbBits)  = 0
 *        - Bits [64 - nbBits, 64) = value
 *   2. The bitContainer is built from the upper bits and
 *      right shifted. E.g. to add a new value of N bits
 *      you right shift the bitContainer by N, then or in
 *      the new value into the N upper bits.
 *   3. The bitstream has two bit containers. You can add
 *      bits to the second container and merge them into
 *      the first container.
 */

#define HUF_BITS_IN_CONTAINER (sizeof(size_t) * 8)

typedef struct {
    size_t bitContainer[2];
    size_t bitPos[2];

    BYTE* startPtr;
    BYTE* ptr;
    BYTE* endPtr;
} HUF_CStream_t;

/*! HUF_initCStream():
 * Initializes the bitstream.
 * @returns 0 or an error code.
 */
static size_t HUF_initCStream(HUF_CStream_t* bitC,
                                  void* startPtr, size_t dstCapacity)
{
    ZSTD_memset(bitC, 0, sizeof(*bitC));
    bitC->startPtr = (BYTE*)startPtr;
    bitC->ptr = bitC->startPtr;
    bitC->endPtr = bitC->startPtr + dstCapacity - sizeof(bitC->bitContainer[0]);
    if (dstCapacity <= sizeof(bitC->bitContainer[0])) return ERROR(dstSize_tooSmall);
    return 0;
}

/*! HUF_addBits():
 * Adds the symbol stored in HUF_CElt elt to the bitstream.
 *
 * @param elt   The element we're adding. This is a (nbBits, value) pair.
 *              See the HUF_CStream_t docs for the format.
 * @param idx   Insert into the bitstream at this idx.
 * @param kFast This is a template parameter. If the bitstream is guaranteed
 *              to have at least 4 unused bits after this call it may be 1,
 *              otherwise it must be 0. HUF_addBits() is faster when fast is set.
 */
FORCE_INLINE_TEMPLATE void HUF_addBits(HUF_CStream_t* bitC, HUF_CElt elt, int idx, int kFast)
{
    assert(idx <= 1);
    assert(HUF_getNbBits(elt) <= HUF_TABLELOG_ABSOLUTEMAX);
    /* This is efficient on x86-64 with BMI2 because shrx
     * only reads the low 6 bits of the register. The compiler
     * knows this and elides the mask. When fast is set,
     * every operation can use the same value loaded from elt.
     */
    bitC->bitContainer[idx] >>= HUF_getNbBits(elt);
    bitC->bitContainer[idx] |= kFast ? HUF_getValueFast(elt) : HUF_getValue(elt);
    /* We only read the low 8 bits of bitC->bitPos[idx] so it
     * doesn't matter that the high bits have noise from the value.
     */
    bitC->bitPos[idx] += HUF_getNbBitsFast(elt);
    assert((bitC->bitPos[idx] & 0xFF) <= HUF_BITS_IN_CONTAINER);
    /* The last 4-bits of elt are dirty if fast is set,
     * so we must not be overwriting bits that have already been
     * inserted into the bit container.
     */
#if DEBUGLEVEL >= 1
    {
        size_t const nbBits = HUF_getNbBits(elt);
        size_t const dirtyBits = nbBits == 0 ? 0 : BIT_highbit32((U32)nbBits) + 1;
        (void)dirtyBits;
        /* Middle bits are 0. */
        assert(((elt >> dirtyBits) << (dirtyBits + nbBits)) == 0);
        /* We didn't overwrite any bits in the bit container. */
        assert(!kFast || (bitC->bitPos[idx] & 0xFF) <= HUF_BITS_IN_CONTAINER);
        (void)dirtyBits;
    }
#endif
}

FORCE_INLINE_TEMPLATE void HUF_zeroIndex1(HUF_CStream_t* bitC)
{
    bitC->bitContainer[1] = 0;
    bitC->bitPos[1] = 0;
}

/*! HUF_mergeIndex1() :
 * Merges the bit container @ index 1 into the bit container @ index 0
 * and zeros the bit container @ index 1.
 */
FORCE_INLINE_TEMPLATE void HUF_mergeIndex1(HUF_CStream_t* bitC)
{
    assert((bitC->bitPos[1] & 0xFF) < HUF_BITS_IN_CONTAINER);
    bitC->bitContainer[0] >>= (bitC->bitPos[1] & 0xFF);
    bitC->bitContainer[0] |= bitC->bitContainer[1];
    bitC->bitPos[0] += bitC->bitPos[1];
    assert((bitC->bitPos[0] & 0xFF) <= HUF_BITS_IN_CONTAINER);
}

/*! HUF_flushBits() :
* Flushes the bits in the bit container @ index 0.
*
* @post bitPos will be < 8.
* @param kFast If kFast is set then we must know a-priori that
*              the bit container will not overflow.
*/
FORCE_INLINE_TEMPLATE void HUF_flushBits(HUF_CStream_t* bitC, int kFast)
{
    /* The upper bits of bitPos are noisy, so we must mask by 0xFF. */
    size_t const nbBits = bitC->bitPos[0] & 0xFF;
    size_t const nbBytes = nbBits >> 3;
    /* The top nbBits bits of bitContainer are the ones we need. */
    size_t const bitContainer = bitC->bitContainer[0] >> (HUF_BITS_IN_CONTAINER - nbBits);
    /* Mask bitPos to account for the bytes we consumed. */
    bitC->bitPos[0] &= 7;
    assert(nbBits > 0);
    assert(nbBits <= sizeof(bitC->bitContainer[0]) * 8);
    assert(bitC->ptr <= bitC->endPtr);
    MEM_writeLEST(bitC->ptr, bitContainer);
    bitC->ptr += nbBytes;
    assert(!kFast || bitC->ptr <= bitC->endPtr);
    if (!kFast && bitC->ptr > bitC->endPtr) bitC->ptr = bitC->endPtr;
    /* bitContainer doesn't need to be modified because the leftover
     * bits are already the top bitPos bits. And we don't care about
     * noise in the lower values.
     */
}

/*! HUF_endMark()
 * @returns The Huffman stream end mark: A 1-bit value = 1.
 */
static HUF_CElt HUF_endMark(void)
{
    HUF_CElt endMark;
    HUF_setNbBits(&endMark, 1);
    HUF_setValue(&endMark, 1);
    return endMark;
}

/*! HUF_closeCStream() :
 *  @return Size of CStream, in bytes,
 *          or 0 if it could not fit into dstBuffer */
static size_t HUF_closeCStream(HUF_CStream_t* bitC)
{
    HUF_addBits(bitC, HUF_endMark(), /* idx */ 0, /* kFast */ 0);
    HUF_flushBits(bitC, /* kFast */ 0);
    {
        size_t const nbBits = bitC->bitPos[0] & 0xFF;
        if (bitC->ptr >= bitC->endPtr) return 0; /* overflow detected */
        return (bitC->ptr - bitC->startPtr) + (nbBits > 0);
    }
}

FORCE_INLINE_TEMPLATE void
HUF_encodeSymbol(HUF_CStream_t* bitCPtr, U32 symbol, const HUF_CElt* CTable, int idx, int fast)
{
    HUF_addBits(bitCPtr, CTable[symbol], idx, fast);
}

FORCE_INLINE_TEMPLATE void
HUF_compress1X_usingCTable_internal_body_loop(HUF_CStream_t* bitC,
                                   const BYTE* ip, size_t srcSize,
                                   const HUF_CElt* ct,
                                   int kUnroll, int kFastFlush, int kLastFast)
{
    /* Join to kUnroll */
    int n = (int)srcSize;
    int rem = n % kUnroll;
    if (rem > 0) {
        for (; rem > 0; --rem) {
            HUF_encodeSymbol(bitC, ip[--n], ct, 0, /* fast */ 0);
        }
        HUF_flushBits(bitC, kFastFlush);
    }
    assert(n % kUnroll == 0);

    /* Join to 2 * kUnroll */
    if (n % (2 * kUnroll)) {
        int u;
        for (u = 1; u < kUnroll; ++u) {
            HUF_encodeSymbol(bitC, ip[n - u], ct, 0, 1);
        }
        HUF_encodeSymbol(bitC, ip[n - kUnroll], ct, 0, kLastFast);
        HUF_flushBits(bitC, kFastFlush);
        n -= kUnroll;
    }
    assert(n % (2 * kUnroll) == 0);

    for (; n>0; n-= 2 * kUnroll) {
        /* Encode kUnroll symbols into the bitstream @ index 0. */
        int u;
        for (u = 1; u < kUnroll; ++u) {
            HUF_encodeSymbol(bitC, ip[n - u], ct, /* idx */ 0, /* fast */ 1);
        }
        HUF_encodeSymbol(bitC, ip[n - kUnroll], ct, /* idx */ 0, /* fast */ kLastFast);
        HUF_flushBits(bitC, kFastFlush);
        /* Encode kUnroll symbols into the bitstream @ index 1.
         * This allows us to start filling the bit container
         * without any data dependencies.
         */
        HUF_zeroIndex1(bitC);
        for (u = 1; u < kUnroll; ++u) {
            HUF_encodeSymbol(bitC, ip[n - kUnroll - u], ct, /* idx */ 1, /* fast */ 1);
        }
        HUF_encodeSymbol(bitC, ip[n - kUnroll - kUnroll], ct, /* idx */ 1, /* fast */ kLastFast);
        /* Merge bitstream @ index 1 into the bitstream @ index 0 */
        HUF_mergeIndex1(bitC);
        HUF_flushBits(bitC, kFastFlush);
    }
    assert(n == 0);

}

/*
 * Returns a tight upper bound on the output space needed by Huffman
 * with 8 bytes buffer to handle over-writes. If the output is at least
 * this large we don't need to do bounds checks during Huffman encoding.
 */
static size_t HUF_tightCompressBound(size_t srcSize, size_t tableLog)
{
    return ((srcSize * tableLog) >> 3) + 8;
}


FORCE_INLINE_TEMPLATE size_t
HUF_compress1X_usingCTable_internal_body(void* dst, size_t dstSize,
                                   const void* src, size_t srcSize,
                                   const HUF_CElt* CTable)
{
    U32 const tableLog = (U32)CTable[0];
    HUF_CElt const* ct = CTable + 1;
    const BYTE* ip = (const BYTE*) src;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + dstSize;
    BYTE* op = ostart;
    HUF_CStream_t bitC;

    /* init */
    if (dstSize < 8) return 0;   /* not enough space to compress */
    { size_t const initErr = HUF_initCStream(&bitC, op, (size_t)(oend-op));
      if (HUF_isError(initErr)) return 0; }

    if (dstSize < HUF_tightCompressBound(srcSize, (size_t)tableLog) || tableLog > 11)
        HUF_compress1X_usingCTable_internal_body_loop(&bitC, ip, srcSize, ct, /* kUnroll */ MEM_32bits() ? 2 : 4, /* kFast */ 0, /* kLastFast */ 0);
    else {
        if (MEM_32bits()) {
            switch (tableLog) {
            case 11:
                HUF_compress1X_usingCTable_internal_body_loop(&bitC, ip, srcSize, ct, /* kUnroll */ 2, /* kFastFlush */ 1, /* kLastFast */ 0);
                break;
            case 10: ZSTD_FALLTHROUGH;
            case 9: ZSTD_FALLTHROUGH;
            case 8:
                HUF_compress1X_usingCTable_internal_body_loop(&bitC, ip, srcSize, ct, /* kUnroll */ 2, /* kFastFlush */ 1, /* kLastFast */ 1);
                break;
            case 7: ZSTD_FALLTHROUGH;
            default:
                HUF_compress1X_usingCTable_internal_body_loop(&bitC, ip, srcSize, ct, /* kUnroll */ 3, /* kFastFlush */ 1, /* kLastFast */ 1);
                break;
            }
        } else {
            switch (tableLog) {
            case 11:
                HUF_compress1X_usingCTable_internal_body_loop(&bitC, ip, srcSize, ct, /* kUnroll */ 5, /* kFastFlush */ 1, /* kLastFast */ 0);
                break;
            case 10:
                HUF_compress1X_usingCTable_internal_body_loop(&bitC, ip, srcSize, ct, /* kUnroll */ 5, /* kFastFlush */ 1, /* kLastFast */ 1);
                break;
            case 9:
                HUF_compress1X_usingCTable_internal_body_loop(&bitC, ip, srcSize, ct, /* kUnroll */ 6, /* kFastFlush */ 1, /* kLastFast */ 0);
                break;
            case 8:
                HUF_compress1X_usingCTable_internal_body_loop(&bitC, ip, srcSize, ct, /* kUnroll */ 7, /* kFastFlush */ 1, /* kLastFast */ 0);
                break;
            case 7:
                HUF_compress1X_usingCTable_internal_body_loop(&bitC, ip, srcSize, ct, /* kUnroll */ 8, /* kFastFlush */ 1, /* kLastFast */ 0);
                break;
            case 6: ZSTD_FALLTHROUGH;
            default:
                HUF_compress1X_usingCTable_internal_body_loop(&bitC, ip, srcSize, ct, /* kUnroll */ 9, /* kFastFlush */ 1, /* kLastFast */ 1);
                break;
            }
        }
    }
    assert(bitC.ptr <= bitC.endPtr);

    return HUF_closeCStream(&bitC);
}

#if DYNAMIC_BMI2

static BMI2_TARGET_ATTRIBUTE size_t
HUF_compress1X_usingCTable_internal_bmi2(void* dst, size_t dstSize,
                                   const void* src, size_t srcSize,
                                   const HUF_CElt* CTable)
{
    return HUF_compress1X_usingCTable_internal_body(dst, dstSize, src, srcSize, CTable);
}

static size_t
HUF_compress1X_usingCTable_internal_default(void* dst, size_t dstSize,
                                      const void* src, size_t srcSize,
                                      const HUF_CElt* CTable)
{
    return HUF_compress1X_usingCTable_internal_body(dst, dstSize, src, srcSize, CTable);
}

static size_t
HUF_compress1X_usingCTable_internal(void* dst, size_t dstSize,
                              const void* src, size_t srcSize,
                              const HUF_CElt* CTable, const int bmi2)
{
    if (bmi2) {
        return HUF_compress1X_usingCTable_internal_bmi2(dst, dstSize, src, srcSize, CTable);
    }
    return HUF_compress1X_usingCTable_internal_default(dst, dstSize, src, srcSize, CTable);
}

#else

static size_t
HUF_compress1X_usingCTable_internal(void* dst, size_t dstSize,
                              const void* src, size_t srcSize,
                              const HUF_CElt* CTable, const int bmi2)
{
    (void)bmi2;
    return HUF_compress1X_usingCTable_internal_body(dst, dstSize, src, srcSize, CTable);
}

#endif

size_t HUF_compress1X_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const HUF_CElt* CTable)
{
    return HUF_compress1X_usingCTable_bmi2(dst, dstSize, src, srcSize, CTable, /* bmi2 */ 0);
}

size_t HUF_compress1X_usingCTable_bmi2(void* dst, size_t dstSize, const void* src, size_t srcSize, const HUF_CElt* CTable, int bmi2)
{
    return HUF_compress1X_usingCTable_internal(dst, dstSize, src, srcSize, CTable, bmi2);
}

static size_t
HUF_compress4X_usingCTable_internal(void* dst, size_t dstSize,
                              const void* src, size_t srcSize,
                              const HUF_CElt* CTable, int bmi2)
{
    size_t const segmentSize = (srcSize+3)/4;   /* first 3 segments */
    const BYTE* ip = (const BYTE*) src;
    const BYTE* const iend = ip + srcSize;
    BYTE* const ostart = (BYTE*) dst;
    BYTE* const oend = ostart + dstSize;
    BYTE* op = ostart;

    if (dstSize < 6 + 1 + 1 + 1 + 8) return 0;   /* minimum space to compress successfully */
    if (srcSize < 12) return 0;   /* no saving possible : too small input */
    op += 6;   /* jumpTable */

    assert(op <= oend);
    {   CHECK_V_F(cSize, HUF_compress1X_usingCTable_internal(op, (size_t)(oend-op), ip, segmentSize, CTable, bmi2) );
        if (cSize == 0 || cSize > 65535) return 0;
        MEM_writeLE16(ostart, (U16)cSize);
        op += cSize;
    }

    ip += segmentSize;
    assert(op <= oend);
    {   CHECK_V_F(cSize, HUF_compress1X_usingCTable_internal(op, (size_t)(oend-op), ip, segmentSize, CTable, bmi2) );
        if (cSize == 0 || cSize > 65535) return 0;
        MEM_writeLE16(ostart+2, (U16)cSize);
        op += cSize;
    }

    ip += segmentSize;
    assert(op <= oend);
    {   CHECK_V_F(cSize, HUF_compress1X_usingCTable_internal(op, (size_t)(oend-op), ip, segmentSize, CTable, bmi2) );
        if (cSize == 0 || cSize > 65535) return 0;
        MEM_writeLE16(ostart+4, (U16)cSize);
        op += cSize;
    }

    ip += segmentSize;
    assert(op <= oend);
    assert(ip <= iend);
    {   CHECK_V_F(cSize, HUF_compress1X_usingCTable_internal(op, (size_t)(oend-op), ip, (size_t)(iend-ip), CTable, bmi2) );
        if (cSize == 0 || cSize > 65535) return 0;
        op += cSize;
    }

    return (size_t)(op-ostart);
}

size_t HUF_compress4X_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const HUF_CElt* CTable)
{
    return HUF_compress4X_usingCTable_bmi2(dst, dstSize, src, srcSize, CTable, /* bmi2 */ 0);
}

size_t HUF_compress4X_usingCTable_bmi2(void* dst, size_t dstSize, const void* src, size_t srcSize, const HUF_CElt* CTable, int bmi2)
{
    return HUF_compress4X_usingCTable_internal(dst, dstSize, src, srcSize, CTable, bmi2);
}

typedef enum { HUF_singleStream, HUF_fourStreams } HUF_nbStreams_e;

static size_t HUF_compressCTable_internal(
                BYTE* const ostart, BYTE* op, BYTE* const oend,
                const void* src, size_t srcSize,
                HUF_nbStreams_e nbStreams, const HUF_CElt* CTable, const int bmi2)
{
    size_t const cSize = (nbStreams==HUF_singleStream) ?
                         HUF_compress1X_usingCTable_internal(op, (size_t)(oend - op), src, srcSize, CTable, bmi2) :
                         HUF_compress4X_usingCTable_internal(op, (size_t)(oend - op), src, srcSize, CTable, bmi2);
    if (HUF_isError(cSize)) { return cSize; }
    if (cSize==0) { return 0; }   /* uncompressible */
    op += cSize;
    /* check compressibility */
    assert(op >= ostart);
    if ((size_t)(op-ostart) >= srcSize-1) { return 0; }
    return (size_t)(op-ostart);
}

typedef struct {
    unsigned count[HUF_SYMBOLVALUE_MAX + 1];
    HUF_CElt CTable[HUF_CTABLE_SIZE_ST(HUF_SYMBOLVALUE_MAX)];
    union {
        HUF_buildCTable_wksp_tables buildCTable_wksp;
        HUF_WriteCTableWksp writeCTable_wksp;
        U32 hist_wksp[HIST_WKSP_SIZE_U32];
    } wksps;
} HUF_compress_tables_t;

#define SUSPECT_INCOMPRESSIBLE_SAMPLE_SIZE 4096
#define SUSPECT_INCOMPRESSIBLE_SAMPLE_RATIO 10  /* Must be >= 2 */

/* HUF_compress_internal() :
 * `workSpace_align4` must be aligned on 4-bytes boundaries,
 * and occupies the same space as a table of HUF_WORKSPACE_SIZE_U64 unsigned */
static size_t
HUF_compress_internal (void* dst, size_t dstSize,
                 const void* src, size_t srcSize,
                       unsigned maxSymbolValue, unsigned huffLog,
                       HUF_nbStreams_e nbStreams,
                       void* workSpace, size_t wkspSize,
                       HUF_CElt* oldHufTable, HUF_repeat* repeat, int preferRepeat,
                 const int bmi2, unsigned suspectUncompressible)
{
    HUF_compress_tables_t* const table = (HUF_compress_tables_t*)HUF_alignUpWorkspace(workSpace, &wkspSize, ZSTD_ALIGNOF(size_t));
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + dstSize;
    BYTE* op = ostart;

    HUF_STATIC_ASSERT(sizeof(*table) + HUF_WORKSPACE_MAX_ALIGNMENT <= HUF_WORKSPACE_SIZE);

    /* checks & inits */
    if (wkspSize < sizeof(*table)) return ERROR(workSpace_tooSmall);
    if (!srcSize) return 0;  /* Uncompressed */
    if (!dstSize) return 0;  /* cannot fit anything within dst budget */
    if (srcSize > HUF_BLOCKSIZE_MAX) return ERROR(srcSize_wrong);   /* current block size limit */
    if (huffLog > HUF_TABLELOG_MAX) return ERROR(tableLog_tooLarge);
    if (maxSymbolValue > HUF_SYMBOLVALUE_MAX) return ERROR(maxSymbolValue_tooLarge);
    if (!maxSymbolValue) maxSymbolValue = HUF_SYMBOLVALUE_MAX;
    if (!huffLog) huffLog = HUF_TABLELOG_DEFAULT;

    /* Heuristic : If old table is valid, use it for small inputs */
    if (preferRepeat && repeat && *repeat == HUF_repeat_valid) {
        return HUF_compressCTable_internal(ostart, op, oend,
                                           src, srcSize,
                                           nbStreams, oldHufTable, bmi2);
    }

    /* If uncompressible data is suspected, do a smaller sampling first */
    DEBUG_STATIC_ASSERT(SUSPECT_INCOMPRESSIBLE_SAMPLE_RATIO >= 2);
    if (suspectUncompressible && srcSize >= (SUSPECT_INCOMPRESSIBLE_SAMPLE_SIZE * SUSPECT_INCOMPRESSIBLE_SAMPLE_RATIO)) {
        size_t largestTotal = 0;
        {   unsigned maxSymbolValueBegin = maxSymbolValue;
            CHECK_V_F(largestBegin, HIST_count_simple (table->count, &maxSymbolValueBegin, (const BYTE*)src, SUSPECT_INCOMPRESSIBLE_SAMPLE_SIZE) );
            largestTotal += largestBegin;
        }
        {   unsigned maxSymbolValueEnd = maxSymbolValue;
            CHECK_V_F(largestEnd, HIST_count_simple (table->count, &maxSymbolValueEnd, (const BYTE*)src + srcSize - SUSPECT_INCOMPRESSIBLE_SAMPLE_SIZE, SUSPECT_INCOMPRESSIBLE_SAMPLE_SIZE) );
            largestTotal += largestEnd;
        }
        if (largestTotal <= ((2 * SUSPECT_INCOMPRESSIBLE_SAMPLE_SIZE) >> 7)+4) return 0;   /* heuristic : probably not compressible enough */
    }

    /* Scan input and build symbol stats */
    {   CHECK_V_F(largest, HIST_count_wksp (table->count, &maxSymbolValue, (const BYTE*)src, srcSize, table->wksps.hist_wksp, sizeof(table->wksps.hist_wksp)) );
        if (largest == srcSize) { *ostart = ((const BYTE*)src)[0]; return 1; }   /* single symbol, rle */
        if (largest <= (srcSize >> 7)+4) return 0;   /* heuristic : probably not compressible enough */
    }

    /* Check validity of previous table */
    if ( repeat
      && *repeat == HUF_repeat_check
      && !HUF_validateCTable(oldHufTable, table->count, maxSymbolValue)) {
        *repeat = HUF_repeat_none;
    }
    /* Heuristic : use existing table for small inputs */
    if (preferRepeat && repeat && *repeat != HUF_repeat_none) {
        return HUF_compressCTable_internal(ostart, op, oend,
                                           src, srcSize,
                                           nbStreams, oldHufTable, bmi2);
    }

    /* Build Huffman Tree */
    huffLog = HUF_optimalTableLog(huffLog, srcSize, maxSymbolValue);
    {   size_t const maxBits = HUF_buildCTable_wksp(table->CTable, table->count,
                                            maxSymbolValue, huffLog,
                                            &table->wksps.buildCTable_wksp, sizeof(table->wksps.buildCTable_wksp));
        CHECK_F(maxBits);
        huffLog = (U32)maxBits;
    }
    /* Zero unused symbols in CTable, so we can check it for validity */
    {
        size_t const ctableSize = HUF_CTABLE_SIZE_ST(maxSymbolValue);
        size_t const unusedSize = sizeof(table->CTable) - ctableSize * sizeof(HUF_CElt);
        ZSTD_memset(table->CTable + ctableSize, 0, unusedSize);
    }

    /* Write table description header */
    {   CHECK_V_F(hSize, HUF_writeCTable_wksp(op, dstSize, table->CTable, maxSymbolValue, huffLog,
                                              &table->wksps.writeCTable_wksp, sizeof(table->wksps.writeCTable_wksp)) );
        /* Check if using previous huffman table is beneficial */
        if (repeat && *repeat != HUF_repeat_none) {
            size_t const oldSize = HUF_estimateCompressedSize(oldHufTable, table->count, maxSymbolValue);
            size_t const newSize = HUF_estimateCompressedSize(table->CTable, table->count, maxSymbolValue);
            if (oldSize <= hSize + newSize || hSize + 12 >= srcSize) {
                return HUF_compressCTable_internal(ostart, op, oend,
                                                   src, srcSize,
                                                   nbStreams, oldHufTable, bmi2);
        }   }

        /* Use the new huffman table */
        if (hSize + 12ul >= srcSize) { return 0; }
        op += hSize;
        if (repeat) { *repeat = HUF_repeat_none; }
        if (oldHufTable)
            ZSTD_memcpy(oldHufTable, table->CTable, sizeof(table->CTable));  /* Save new table */
    }
    return HUF_compressCTable_internal(ostart, op, oend,
                                       src, srcSize,
                                       nbStreams, table->CTable, bmi2);
}


size_t HUF_compress1X_wksp (void* dst, size_t dstSize,
                      const void* src, size_t srcSize,
                      unsigned maxSymbolValue, unsigned huffLog,
                      void* workSpace, size_t wkspSize)
{
    return HUF_compress_internal(dst, dstSize, src, srcSize,
                                 maxSymbolValue, huffLog, HUF_singleStream,
                                 workSpace, wkspSize,
                                 NULL, NULL, 0, 0 /*bmi2*/, 0);
}

size_t HUF_compress1X_repeat (void* dst, size_t dstSize,
                      const void* src, size_t srcSize,
                      unsigned maxSymbolValue, unsigned huffLog,
                      void* workSpace, size_t wkspSize,
                      HUF_CElt* hufTable, HUF_repeat* repeat, int preferRepeat,
                      int bmi2, unsigned suspectUncompressible)
{
    return HUF_compress_internal(dst, dstSize, src, srcSize,
                                 maxSymbolValue, huffLog, HUF_singleStream,
                                 workSpace, wkspSize, hufTable,
                                 repeat, preferRepeat, bmi2, suspectUncompressible);
}

/* HUF_compress4X_repeat():
 * compress input using 4 streams.
 * provide workspace to generate compression tables */
size_t HUF_compress4X_wksp (void* dst, size_t dstSize,
                      const void* src, size_t srcSize,
                      unsigned maxSymbolValue, unsigned huffLog,
                      void* workSpace, size_t wkspSize)
{
    return HUF_compress_internal(dst, dstSize, src, srcSize,
                                 maxSymbolValue, huffLog, HUF_fourStreams,
                                 workSpace, wkspSize,
                                 NULL, NULL, 0, 0 /*bmi2*/, 0);
}

/* HUF_compress4X_repeat():
 * compress input using 4 streams.
 * consider skipping quickly
 * re-use an existing huffman compression table */
size_t HUF_compress4X_repeat (void* dst, size_t dstSize,
                      const void* src, size_t srcSize,
                      unsigned maxSymbolValue, unsigned huffLog,
                      void* workSpace, size_t wkspSize,
                      HUF_CElt* hufTable, HUF_repeat* repeat, int preferRepeat, int bmi2, unsigned suspectUncompressible)
{
    return HUF_compress_internal(dst, dstSize, src, srcSize,
                                 maxSymbolValue, huffLog, HUF_fourStreams,
                                 workSpace, wkspSize,
                                 hufTable, repeat, preferRepeat, bmi2, suspectUncompressible);
}

