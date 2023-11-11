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
    HUF_CompressWeightsWksp* wksp = (HUF_CompressWeightsWksp*)workspace;

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


typedef struct {
    HUF_CompressWeightsWksp wksp;
    BYTE bitsToWeight[HUF_TABLELOG_MAX + 1];   /* precomputed conversion table */
    BYTE huffWeight[HUF_SYMBOLVALUE_MAX];
} HUF_WriteCTableWksp;

size_t HUF_writeCTable_wksp(void* dst, size_t maxDstSize,
                            const HUF_CElt* CTable, unsigned maxSymbolValue, unsigned huffLog,
                            void* workspace, size_t workspaceSize)
{
    BYTE* op = (BYTE*)dst;
    U32 n;
    HUF_WriteCTableWksp* wksp = (HUF_WriteCTableWksp*)workspace;

    /* check conditions */
    if (workspaceSize < sizeof(HUF_WriteCTableWksp)) return ERROR(GENERIC);
    if (maxSymbolValue > HUF_SYMBOLVALUE_MAX) return ERROR(maxSymbolValue_tooLarge);

    /* convert to weight */
    wksp->bitsToWeight[0] = 0;
    for (n=1; n<huffLog+1; n++)
        wksp->bitsToWeight[n] = (BYTE)(huffLog + 1 - n);
    for (n=0; n<maxSymbolValue; n++)
        wksp->huffWeight[n] = wksp->bitsToWeight[CTable[n].nbBits];

    /* attempt weights compression by FSE */
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

    /* get symbol weights */
    CHECK_V_F(readSize, HUF_readStats(huffWeight, HUF_SYMBOLVALUE_MAX+1, rankVal, &nbSymbols, &tableLog, src, srcSize));
    *hasZeroWeights = (rankVal[0] > 0);

    /* check result */
    if (tableLog > HUF_TABLELOG_MAX) return ERROR(tableLog_tooLarge);
    if (nbSymbols > *maxSymbolValuePtr+1) return ERROR(maxSymbolValue_tooSmall);

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
            CTable[n].nbBits = (BYTE)(tableLog + 1 - w) & -(w != 0);
    }   }

    /* fill val */
    {   U16 nbPerRank[HUF_TABLELOG_MAX+2]  = {0};  /* support w=0=>n=tableLog+1 */
        U16 valPerRank[HUF_TABLELOG_MAX+2] = {0};
        { U32 n; for (n=0; n<nbSymbols; n++) nbPerRank[CTable[n].nbBits]++; }
        /* determine stating value per rank */
        valPerRank[tableLog+1] = 0;   /* for w==0 */
        {   U16 min = 0;
            U32 n; for (n=tableLog; n>0; n--) {  /* start at n=tablelog <-> w=1 */
                valPerRank[n] = min;     /* get starting value within each rank */
                min += nbPerRank[n];
                min >>= 1;
        }   }
        /* assign value within rank, symbol order */
        { U32 n; for (n=0; n<nbSymbols; n++) CTable[n].val = valPerRank[CTable[n].nbBits]++; }
    }

    *maxSymbolValuePtr = nbSymbols - 1;
    return readSize;
}

U32 HUF_getNbBits(const void* symbolTable, U32 symbolValue)
{
    const HUF_CElt* table = (const HUF_CElt*)symbolTable;
    assert(symbolValue <= HUF_SYMBOLVALUE_MAX);
    return table[symbolValue].nbBits;
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
    U32 base;
    U32 curr;
} rankPos;

typedef nodeElt huffNodeTable[HUF_CTABLE_WORKSPACE_SIZE_U32];

#define RANK_POSITION_TABLE_SIZE 32

typedef struct {
  huffNodeTable huffNodeTbl;
  rankPos rankPosition[RANK_POSITION_TABLE_SIZE];
} HUF_buildCTable_wksp_tables;

/*
 * HUF_sort():
 * Sorts the symbols [0, maxSymbolValue] by count[symbol] in decreasing order.
 *
 * @param[out] huffNode       Sorted symbols by decreasing count. Only members `.count` and `.byte` are filled.
 *                            Must have (maxSymbolValue + 1) entries.
 * @param[in]  count          Histogram of the symbols.
 * @param[in]  maxSymbolValue Maximum symbol value.
 * @param      rankPosition   This is a scratch workspace. Must have RANK_POSITION_TABLE_SIZE entries.
 */
static void HUF_sort(nodeElt* huffNode, const unsigned* count, U32 maxSymbolValue, rankPos* rankPosition)
{
    int n;
    int const maxSymbolValue1 = (int)maxSymbolValue + 1;

    /* Compute base and set curr to base.
     * For symbol s let lowerRank = BIT_highbit32(count[n]+1) and rank = lowerRank + 1.
     * Then 2^lowerRank <= count[n]+1 <= 2^rank.
     * We attribute each symbol to lowerRank's base value, because we want to know where
     * each rank begins in the output, so for rank R we want to count ranks R+1 and above.
     */
    ZSTD_memset(rankPosition, 0, sizeof(*rankPosition) * RANK_POSITION_TABLE_SIZE);
    for (n = 0; n < maxSymbolValue1; ++n) {
        U32 lowerRank = BIT_highbit32(count[n] + 1);
        rankPosition[lowerRank].base++;
    }
    assert(rankPosition[RANK_POSITION_TABLE_SIZE - 1].base == 0);
    for (n = RANK_POSITION_TABLE_SIZE - 1; n > 0; --n) {
        rankPosition[n-1].base += rankPosition[n].base;
        rankPosition[n-1].curr = rankPosition[n-1].base;
    }
    /* Sort */
    for (n = 0; n < maxSymbolValue1; ++n) {
        U32 const c = count[n];
        U32 const r = BIT_highbit32(c+1) + 1;
        U32 pos = rankPosition[r].curr++;
        /* Insert into the correct position in the rank.
         * We have at most 256 symbols, so this insertion should be fine.
         */
        while ((pos > rankPosition[r].base) && (c > huffNode[pos-1].count)) {
            huffNode[pos] = huffNode[pos-1];
            pos--;
        }
        huffNode[pos].count = c;
        huffNode[pos].byte  = (BYTE)n;
    }
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
        CTable[huffNode[n].byte].nbBits = huffNode[n].nbBits;   /* push nbBits per symbol, symbol order */
    for (n=0; n<alphabetSize; n++)
        CTable[n].val = valPerRank[CTable[n].nbBits]++;   /* assign value within rank, symbol order */
}

size_t HUF_buildCTable_wksp (HUF_CElt* tree, const unsigned* count, U32 maxSymbolValue, U32 maxNbBits, void* workSpace, size_t wkspSize)
{
    HUF_buildCTable_wksp_tables* const wksp_tables = (HUF_buildCTable_wksp_tables*)workSpace;
    nodeElt* const huffNode0 = wksp_tables->huffNodeTbl;
    nodeElt* const huffNode = huffNode0+1;
    int nonNullRank;

    /* safety checks */
    if (((size_t)workSpace & 3) != 0) return ERROR(GENERIC);  /* must be aligned on 4-bytes boundaries */
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

    HUF_buildCTableFromTree(tree, huffNode, nonNullRank, maxSymbolValue, maxNbBits);

    return maxNbBits;
}

size_t HUF_estimateCompressedSize(const HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue)
{
    size_t nbBits = 0;
    int s;
    for (s = 0; s <= (int)maxSymbolValue; ++s) {
        nbBits += CTable[s].nbBits * count[s];
    }
    return nbBits >> 3;
}

int HUF_validateCTable(const HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue) {
  int bad = 0;
  int s;
  for (s = 0; s <= (int)maxSymbolValue; ++s) {
    bad |= (count[s] != 0) & (CTable[s].nbBits == 0);
  }
  return !bad;
}

size_t HUF_compressBound(size_t size) { return HUF_COMPRESSBOUND(size); }

FORCE_INLINE_TEMPLATE void
HUF_encodeSymbol(BIT_CStream_t* bitCPtr, U32 symbol, const HUF_CElt* CTable)
{
    BIT_addBitsFast(bitCPtr, CTable[symbol].val, CTable[symbol].nbBits);
}

#define HUF_FLUSHBITS(s)  BIT_flushBits(s)

#define HUF_FLUSHBITS_1(stream) \
    if (sizeof((stream)->bitContainer)*8 < HUF_TABLELOG_MAX*2+7) HUF_FLUSHBITS(stream)

#define HUF_FLUSHBITS_2(stream) \
    if (sizeof((stream)->bitContainer)*8 < HUF_TABLELOG_MAX*4+7) HUF_FLUSHBITS(stream)

FORCE_INLINE_TEMPLATE size_t
HUF_compress1X_usingCTable_internal_body(void* dst, size_t dstSize,
                                   const void* src, size_t srcSize,
                                   const HUF_CElt* CTable)
{
    const BYTE* ip = (const BYTE*) src;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + dstSize;
    BYTE* op = ostart;
    size_t n;
    BIT_CStream_t bitC;

    /* init */
    if (dstSize < 8) return 0;   /* not enough space to compress */
    { size_t const initErr = BIT_initCStream(&bitC, op, (size_t)(oend-op));
      if (HUF_isError(initErr)) return 0; }

    n = srcSize & ~3;  /* join to mod 4 */
    switch (srcSize & 3)
    {
        case 3:
            HUF_encodeSymbol(&bitC, ip[n+ 2], CTable);
            HUF_FLUSHBITS_2(&bitC);
            ZSTD_FALLTHROUGH;
        case 2:
            HUF_encodeSymbol(&bitC, ip[n+ 1], CTable);
            HUF_FLUSHBITS_1(&bitC);
            ZSTD_FALLTHROUGH;
        case 1:
            HUF_encodeSymbol(&bitC, ip[n+ 0], CTable);
            HUF_FLUSHBITS(&bitC);
            ZSTD_FALLTHROUGH;
        case 0: ZSTD_FALLTHROUGH;
        default: break;
    }

    for (; n>0; n-=4) {  /* note : n&3==0 at this stage */
        HUF_encodeSymbol(&bitC, ip[n- 1], CTable);
        HUF_FLUSHBITS_1(&bitC);
        HUF_encodeSymbol(&bitC, ip[n- 2], CTable);
        HUF_FLUSHBITS_2(&bitC);
        HUF_encodeSymbol(&bitC, ip[n- 3], CTable);
        HUF_FLUSHBITS_1(&bitC);
        HUF_encodeSymbol(&bitC, ip[n- 4], CTable);
        HUF_FLUSHBITS(&bitC);
    }

    return BIT_closeCStream(&bitC);
}

#if DYNAMIC_BMI2

static TARGET_ATTRIBUTE("bmi2") size_t
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
    return HUF_compress1X_usingCTable_internal(dst, dstSize, src, srcSize, CTable, /* bmi2 */ 0);
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
        if (cSize==0) return 0;
        assert(cSize <= 65535);
        MEM_writeLE16(ostart, (U16)cSize);
        op += cSize;
    }

    ip += segmentSize;
    assert(op <= oend);
    {   CHECK_V_F(cSize, HUF_compress1X_usingCTable_internal(op, (size_t)(oend-op), ip, segmentSize, CTable, bmi2) );
        if (cSize==0) return 0;
        assert(cSize <= 65535);
        MEM_writeLE16(ostart+2, (U16)cSize);
        op += cSize;
    }

    ip += segmentSize;
    assert(op <= oend);
    {   CHECK_V_F(cSize, HUF_compress1X_usingCTable_internal(op, (size_t)(oend-op), ip, segmentSize, CTable, bmi2) );
        if (cSize==0) return 0;
        assert(cSize <= 65535);
        MEM_writeLE16(ostart+4, (U16)cSize);
        op += cSize;
    }

    ip += segmentSize;
    assert(op <= oend);
    assert(ip <= iend);
    {   CHECK_V_F(cSize, HUF_compress1X_usingCTable_internal(op, (size_t)(oend-op), ip, (size_t)(iend-ip), CTable, bmi2) );
        if (cSize==0) return 0;
        op += cSize;
    }

    return (size_t)(op-ostart);
}

size_t HUF_compress4X_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const HUF_CElt* CTable)
{
    return HUF_compress4X_usingCTable_internal(dst, dstSize, src, srcSize, CTable, /* bmi2 */ 0);
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
    HUF_CElt CTable[HUF_SYMBOLVALUE_MAX + 1];
    union {
        HUF_buildCTable_wksp_tables buildCTable_wksp;
        HUF_WriteCTableWksp writeCTable_wksp;
    } wksps;
} HUF_compress_tables_t;

/* HUF_compress_internal() :
 * `workSpace_align4` must be aligned on 4-bytes boundaries,
 * and occupies the same space as a table of HUF_WORKSPACE_SIZE_U32 unsigned */
static size_t
HUF_compress_internal (void* dst, size_t dstSize,
                 const void* src, size_t srcSize,
                       unsigned maxSymbolValue, unsigned huffLog,
                       HUF_nbStreams_e nbStreams,
                       void* workSpace_align4, size_t wkspSize,
                       HUF_CElt* oldHufTable, HUF_repeat* repeat, int preferRepeat,
                 const int bmi2)
{
    HUF_compress_tables_t* const table = (HUF_compress_tables_t*)workSpace_align4;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + dstSize;
    BYTE* op = ostart;

    HUF_STATIC_ASSERT(sizeof(*table) <= HUF_WORKSPACE_SIZE);
    assert(((size_t)workSpace_align4 & 3) == 0);   /* must be aligned on 4-bytes boundaries */

    /* checks & inits */
    if (wkspSize < HUF_WORKSPACE_SIZE) return ERROR(workSpace_tooSmall);
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

    /* Scan input and build symbol stats */
    {   CHECK_V_F(largest, HIST_count_wksp (table->count, &maxSymbolValue, (const BYTE*)src, srcSize, workSpace_align4, wkspSize) );
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
        /* Zero unused symbols in CTable, so we can check it for validity */
        ZSTD_memset(table->CTable + (maxSymbolValue + 1), 0,
               sizeof(table->CTable) - ((maxSymbolValue + 1) * sizeof(HUF_CElt)));
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
                                 NULL, NULL, 0, 0 /*bmi2*/);
}

size_t HUF_compress1X_repeat (void* dst, size_t dstSize,
                      const void* src, size_t srcSize,
                      unsigned maxSymbolValue, unsigned huffLog,
                      void* workSpace, size_t wkspSize,
                      HUF_CElt* hufTable, HUF_repeat* repeat, int preferRepeat, int bmi2)
{
    return HUF_compress_internal(dst, dstSize, src, srcSize,
                                 maxSymbolValue, huffLog, HUF_singleStream,
                                 workSpace, wkspSize, hufTable,
                                 repeat, preferRepeat, bmi2);
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
                                 NULL, NULL, 0, 0 /*bmi2*/);
}

/* HUF_compress4X_repeat():
 * compress input using 4 streams.
 * re-use an existing huffman compression table */
size_t HUF_compress4X_repeat (void* dst, size_t dstSize,
                      const void* src, size_t srcSize,
                      unsigned maxSymbolValue, unsigned huffLog,
                      void* workSpace, size_t wkspSize,
                      HUF_CElt* hufTable, HUF_repeat* repeat, int preferRepeat, int bmi2)
{
    return HUF_compress_internal(dst, dstSize, src, srcSize,
                                 maxSymbolValue, huffLog, HUF_fourStreams,
                                 workSpace, wkspSize,
                                 hufTable, repeat, preferRepeat, bmi2);
}

