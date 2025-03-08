// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include "zstd_compress_internal.h"
#include "hist.h"
#include "zstd_opt.h"

#if !defined(ZSTD_EXCLUDE_BTLAZY2_BLOCK_COMPRESSOR) \
 || !defined(ZSTD_EXCLUDE_BTOPT_BLOCK_COMPRESSOR) \
 || !defined(ZSTD_EXCLUDE_BTULTRA_BLOCK_COMPRESSOR)

#define ZSTD_LITFREQ_ADD    2   /* scaling factor for litFreq, so that frequencies adapt faster to new stats */
#define ZSTD_MAX_PRICE     (1<<30)

#define ZSTD_PREDEF_THRESHOLD 8   /* if srcSize < ZSTD_PREDEF_THRESHOLD, symbols' cost is assumed static, directly determined by pre-defined distributions */


/*-*************************************
*  Price functions for optimal parser
***************************************/

#if 0    /* approximation at bit level (for tests) */
#  define BITCOST_ACCURACY 0
#  define BITCOST_MULTIPLIER (1 << BITCOST_ACCURACY)
#  define WEIGHT(stat, opt) ((void)(opt), ZSTD_bitWeight(stat))
#elif 0  /* fractional bit accuracy (for tests) */
#  define BITCOST_ACCURACY 8
#  define BITCOST_MULTIPLIER (1 << BITCOST_ACCURACY)
#  define WEIGHT(stat,opt) ((void)(opt), ZSTD_fracWeight(stat))
#else    /* opt==approx, ultra==accurate */
#  define BITCOST_ACCURACY 8
#  define BITCOST_MULTIPLIER (1 << BITCOST_ACCURACY)
#  define WEIGHT(stat,opt) ((opt) ? ZSTD_fracWeight(stat) : ZSTD_bitWeight(stat))
#endif

/* ZSTD_bitWeight() :
 * provide estimated "cost" of a stat in full bits only */
MEM_STATIC U32 ZSTD_bitWeight(U32 stat)
{
    return (ZSTD_highbit32(stat+1) * BITCOST_MULTIPLIER);
}

/* ZSTD_fracWeight() :
 * provide fractional-bit "cost" of a stat,
 * using linear interpolation approximation */
MEM_STATIC U32 ZSTD_fracWeight(U32 rawStat)
{
    U32 const stat = rawStat + 1;
    U32 const hb = ZSTD_highbit32(stat);
    U32 const BWeight = hb * BITCOST_MULTIPLIER;
    /* Fweight was meant for "Fractional weight"
     * but it's effectively a value between 1 and 2
     * using fixed point arithmetic */
    U32 const FWeight = (stat << BITCOST_ACCURACY) >> hb;
    U32 const weight = BWeight + FWeight;
    assert(hb + BITCOST_ACCURACY < 31);
    return weight;
}

#if (DEBUGLEVEL>=2)
/* debugging function,
 * @return price in bytes as fractional value
 * for debug messages only */
MEM_STATIC double ZSTD_fCost(int price)
{
    return (double)price / (BITCOST_MULTIPLIER*8);
}
#endif

static int ZSTD_compressedLiterals(optState_t const* const optPtr)
{
    return optPtr->literalCompressionMode != ZSTD_ps_disable;
}

static void ZSTD_setBasePrices(optState_t* optPtr, int optLevel)
{
    if (ZSTD_compressedLiterals(optPtr))
        optPtr->litSumBasePrice = WEIGHT(optPtr->litSum, optLevel);
    optPtr->litLengthSumBasePrice = WEIGHT(optPtr->litLengthSum, optLevel);
    optPtr->matchLengthSumBasePrice = WEIGHT(optPtr->matchLengthSum, optLevel);
    optPtr->offCodeSumBasePrice = WEIGHT(optPtr->offCodeSum, optLevel);
}


static U32 sum_u32(const unsigned table[], size_t nbElts)
{
    size_t n;
    U32 total = 0;
    for (n=0; n<nbElts; n++) {
        total += table[n];
    }
    return total;
}

typedef enum { base_0possible=0, base_1guaranteed=1 } base_directive_e;

static U32
ZSTD_downscaleStats(unsigned* table, U32 lastEltIndex, U32 shift, base_directive_e base1)
{
    U32 s, sum=0;
    DEBUGLOG(5, "ZSTD_downscaleStats (nbElts=%u, shift=%u)",
            (unsigned)lastEltIndex+1, (unsigned)shift );
    assert(shift < 30);
    for (s=0; s<lastEltIndex+1; s++) {
        unsigned const base = base1 ? 1 : (table[s]>0);
        unsigned const newStat = base + (table[s] >> shift);
        sum += newStat;
        table[s] = newStat;
    }
    return sum;
}

/* ZSTD_scaleStats() :
 * reduce all elt frequencies in table if sum too large
 * return the resulting sum of elements */
static U32 ZSTD_scaleStats(unsigned* table, U32 lastEltIndex, U32 logTarget)
{
    U32 const prevsum = sum_u32(table, lastEltIndex+1);
    U32 const factor = prevsum >> logTarget;
    DEBUGLOG(5, "ZSTD_scaleStats (nbElts=%u, target=%u)", (unsigned)lastEltIndex+1, (unsigned)logTarget);
    assert(logTarget < 30);
    if (factor <= 1) return prevsum;
    return ZSTD_downscaleStats(table, lastEltIndex, ZSTD_highbit32(factor), base_1guaranteed);
}

/* ZSTD_rescaleFreqs() :
 * if first block (detected by optPtr->litLengthSum == 0) : init statistics
 *    take hints from dictionary if there is one
 *    and init from zero if there is none,
 *    using src for literals stats, and baseline stats for sequence symbols
 * otherwise downscale existing stats, to be used as seed for next block.
 */
static void
ZSTD_rescaleFreqs(optState_t* const optPtr,
            const BYTE* const src, size_t const srcSize,
                  int const optLevel)
{
    int const compressedLiterals = ZSTD_compressedLiterals(optPtr);
    DEBUGLOG(5, "ZSTD_rescaleFreqs (srcSize=%u)", (unsigned)srcSize);
    optPtr->priceType = zop_dynamic;

    if (optPtr->litLengthSum == 0) {  /* no literals stats collected -> first block assumed -> init */

        /* heuristic: use pre-defined stats for too small inputs */
        if (srcSize <= ZSTD_PREDEF_THRESHOLD) {
            DEBUGLOG(5, "srcSize <= %i : use predefined stats", ZSTD_PREDEF_THRESHOLD);
            optPtr->priceType = zop_predef;
        }

        assert(optPtr->symbolCosts != NULL);
        if (optPtr->symbolCosts->huf.repeatMode == HUF_repeat_valid) {

            /* huffman stats covering the full value set : table presumed generated by dictionary */
            optPtr->priceType = zop_dynamic;

            if (compressedLiterals) {
                /* generate literals statistics from huffman table */
                unsigned lit;
                assert(optPtr->litFreq != NULL);
                optPtr->litSum = 0;
                for (lit=0; lit<=MaxLit; lit++) {
                    U32 const scaleLog = 11;   /* scale to 2K */
                    U32 const bitCost = HUF_getNbBitsFromCTable(optPtr->symbolCosts->huf.CTable, lit);
                    assert(bitCost <= scaleLog);
                    optPtr->litFreq[lit] = bitCost ? 1 << (scaleLog-bitCost) : 1 /*minimum to calculate cost*/;
                    optPtr->litSum += optPtr->litFreq[lit];
            }   }

            {   unsigned ll;
                FSE_CState_t llstate;
                FSE_initCState(&llstate, optPtr->symbolCosts->fse.litlengthCTable);
                optPtr->litLengthSum = 0;
                for (ll=0; ll<=MaxLL; ll++) {
                    U32 const scaleLog = 10;   /* scale to 1K */
                    U32 const bitCost = FSE_getMaxNbBits(llstate.symbolTT, ll);
                    assert(bitCost < scaleLog);
                    optPtr->litLengthFreq[ll] = bitCost ? 1 << (scaleLog-bitCost) : 1 /*minimum to calculate cost*/;
                    optPtr->litLengthSum += optPtr->litLengthFreq[ll];
            }   }

            {   unsigned ml;
                FSE_CState_t mlstate;
                FSE_initCState(&mlstate, optPtr->symbolCosts->fse.matchlengthCTable);
                optPtr->matchLengthSum = 0;
                for (ml=0; ml<=MaxML; ml++) {
                    U32 const scaleLog = 10;
                    U32 const bitCost = FSE_getMaxNbBits(mlstate.symbolTT, ml);
                    assert(bitCost < scaleLog);
                    optPtr->matchLengthFreq[ml] = bitCost ? 1 << (scaleLog-bitCost) : 1 /*minimum to calculate cost*/;
                    optPtr->matchLengthSum += optPtr->matchLengthFreq[ml];
            }   }

            {   unsigned of;
                FSE_CState_t ofstate;
                FSE_initCState(&ofstate, optPtr->symbolCosts->fse.offcodeCTable);
                optPtr->offCodeSum = 0;
                for (of=0; of<=MaxOff; of++) {
                    U32 const scaleLog = 10;
                    U32 const bitCost = FSE_getMaxNbBits(ofstate.symbolTT, of);
                    assert(bitCost < scaleLog);
                    optPtr->offCodeFreq[of] = bitCost ? 1 << (scaleLog-bitCost) : 1 /*minimum to calculate cost*/;
                    optPtr->offCodeSum += optPtr->offCodeFreq[of];
            }   }

        } else {  /* first block, no dictionary */

            assert(optPtr->litFreq != NULL);
            if (compressedLiterals) {
                /* base initial cost of literals on direct frequency within src */
                unsigned lit = MaxLit;
                HIST_count_simple(optPtr->litFreq, &lit, src, srcSize);   /* use raw first block to init statistics */
                optPtr->litSum = ZSTD_downscaleStats(optPtr->litFreq, MaxLit, 8, base_0possible);
            }

            {   unsigned const baseLLfreqs[MaxLL+1] = {
                    4, 2, 1, 1, 1, 1, 1, 1,
                    1, 1, 1, 1, 1, 1, 1, 1,
                    1, 1, 1, 1, 1, 1, 1, 1,
                    1, 1, 1, 1, 1, 1, 1, 1,
                    1, 1, 1, 1
                };
                ZSTD_memcpy(optPtr->litLengthFreq, baseLLfreqs, sizeof(baseLLfreqs));
                optPtr->litLengthSum = sum_u32(baseLLfreqs, MaxLL+1);
            }

            {   unsigned ml;
                for (ml=0; ml<=MaxML; ml++)
                    optPtr->matchLengthFreq[ml] = 1;
            }
            optPtr->matchLengthSum = MaxML+1;

            {   unsigned const baseOFCfreqs[MaxOff+1] = {
                    6, 2, 1, 1, 2, 3, 4, 4,
                    4, 3, 2, 1, 1, 1, 1, 1,
                    1, 1, 1, 1, 1, 1, 1, 1,
                    1, 1, 1, 1, 1, 1, 1, 1
                };
                ZSTD_memcpy(optPtr->offCodeFreq, baseOFCfreqs, sizeof(baseOFCfreqs));
                optPtr->offCodeSum = sum_u32(baseOFCfreqs, MaxOff+1);
            }

        }

    } else {   /* new block : scale down accumulated statistics */

        if (compressedLiterals)
            optPtr->litSum = ZSTD_scaleStats(optPtr->litFreq, MaxLit, 12);
        optPtr->litLengthSum = ZSTD_scaleStats(optPtr->litLengthFreq, MaxLL, 11);
        optPtr->matchLengthSum = ZSTD_scaleStats(optPtr->matchLengthFreq, MaxML, 11);
        optPtr->offCodeSum = ZSTD_scaleStats(optPtr->offCodeFreq, MaxOff, 11);
    }

    ZSTD_setBasePrices(optPtr, optLevel);
}

/* ZSTD_rawLiteralsCost() :
 * price of literals (only) in specified segment (which length can be 0).
 * does not include price of literalLength symbol */
static U32 ZSTD_rawLiteralsCost(const BYTE* const literals, U32 const litLength,
                                const optState_t* const optPtr,
                                int optLevel)
{
    DEBUGLOG(8, "ZSTD_rawLiteralsCost (%u literals)", litLength);
    if (litLength == 0) return 0;

    if (!ZSTD_compressedLiterals(optPtr))
        return (litLength << 3) * BITCOST_MULTIPLIER;  /* Uncompressed - 8 bytes per literal. */

    if (optPtr->priceType == zop_predef)
        return (litLength*6) * BITCOST_MULTIPLIER;  /* 6 bit per literal - no statistic used */

    /* dynamic statistics */
    {   U32 price = optPtr->litSumBasePrice * litLength;
        U32 const litPriceMax = optPtr->litSumBasePrice - BITCOST_MULTIPLIER;
        U32 u;
        assert(optPtr->litSumBasePrice >= BITCOST_MULTIPLIER);
        for (u=0; u < litLength; u++) {
            U32 litPrice = WEIGHT(optPtr->litFreq[literals[u]], optLevel);
            if (UNLIKELY(litPrice > litPriceMax)) litPrice = litPriceMax;
            price -= litPrice;
        }
        return price;
    }
}

/* ZSTD_litLengthPrice() :
 * cost of literalLength symbol */
static U32 ZSTD_litLengthPrice(U32 const litLength, const optState_t* const optPtr, int optLevel)
{
    assert(litLength <= ZSTD_BLOCKSIZE_MAX);
    if (optPtr->priceType == zop_predef)
        return WEIGHT(litLength, optLevel);

    /* ZSTD_LLcode() can't compute litLength price for sizes >= ZSTD_BLOCKSIZE_MAX
     * because it isn't representable in the zstd format.
     * So instead just pretend it would cost 1 bit more than ZSTD_BLOCKSIZE_MAX - 1.
     * In such a case, the block would be all literals.
     */
    if (litLength == ZSTD_BLOCKSIZE_MAX)
        return BITCOST_MULTIPLIER + ZSTD_litLengthPrice(ZSTD_BLOCKSIZE_MAX - 1, optPtr, optLevel);

    /* dynamic statistics */
    {   U32 const llCode = ZSTD_LLcode(litLength);
        return (LL_bits[llCode] * BITCOST_MULTIPLIER)
             + optPtr->litLengthSumBasePrice
             - WEIGHT(optPtr->litLengthFreq[llCode], optLevel);
    }
}

/* ZSTD_getMatchPrice() :
 * Provides the cost of the match part (offset + matchLength) of a sequence.
 * Must be combined with ZSTD_fullLiteralsCost() to get the full cost of a sequence.
 * @offBase : sumtype, representing an offset or a repcode, and using numeric representation of ZSTD_storeSeq()
 * @optLevel: when <2, favors small offset for decompression speed (improved cache efficiency)
 */
FORCE_INLINE_TEMPLATE U32
ZSTD_getMatchPrice(U32 const offBase,
                   U32 const matchLength,
             const optState_t* const optPtr,
                   int const optLevel)
{
    U32 price;
    U32 const offCode = ZSTD_highbit32(offBase);
    U32 const mlBase = matchLength - MINMATCH;
    assert(matchLength >= MINMATCH);

    if (optPtr->priceType == zop_predef)  /* fixed scheme, does not use statistics */
        return WEIGHT(mlBase, optLevel)
             + ((16 + offCode) * BITCOST_MULTIPLIER); /* emulated offset cost */

    /* dynamic statistics */
    price = (offCode * BITCOST_MULTIPLIER) + (optPtr->offCodeSumBasePrice - WEIGHT(optPtr->offCodeFreq[offCode], optLevel));
    if ((optLevel<2) /*static*/ && offCode >= 20)
        price += (offCode-19)*2 * BITCOST_MULTIPLIER; /* handicap for long distance offsets, favor decompression speed */

    /* match Length */
    {   U32 const mlCode = ZSTD_MLcode(mlBase);
        price += (ML_bits[mlCode] * BITCOST_MULTIPLIER) + (optPtr->matchLengthSumBasePrice - WEIGHT(optPtr->matchLengthFreq[mlCode], optLevel));
    }

    price += BITCOST_MULTIPLIER / 5;   /* heuristic : make matches a bit more costly to favor less sequences -> faster decompression speed */

    DEBUGLOG(8, "ZSTD_getMatchPrice(ml:%u) = %u", matchLength, price);
    return price;
}

/* ZSTD_updateStats() :
 * assumption : literals + litLength <= iend */
static void ZSTD_updateStats(optState_t* const optPtr,
                             U32 litLength, const BYTE* literals,
                             U32 offBase, U32 matchLength)
{
    /* literals */
    if (ZSTD_compressedLiterals(optPtr)) {
        U32 u;
        for (u=0; u < litLength; u++)
            optPtr->litFreq[literals[u]] += ZSTD_LITFREQ_ADD;
        optPtr->litSum += litLength*ZSTD_LITFREQ_ADD;
    }

    /* literal Length */
    {   U32 const llCode = ZSTD_LLcode(litLength);
        optPtr->litLengthFreq[llCode]++;
        optPtr->litLengthSum++;
    }

    /* offset code : follows storeSeq() numeric representation */
    {   U32 const offCode = ZSTD_highbit32(offBase);
        assert(offCode <= MaxOff);
        optPtr->offCodeFreq[offCode]++;
        optPtr->offCodeSum++;
    }

    /* match Length */
    {   U32 const mlBase = matchLength - MINMATCH;
        U32 const mlCode = ZSTD_MLcode(mlBase);
        optPtr->matchLengthFreq[mlCode]++;
        optPtr->matchLengthSum++;
    }
}


/* ZSTD_readMINMATCH() :
 * function safe only for comparisons
 * assumption : memPtr must be at least 4 bytes before end of buffer */
MEM_STATIC U32 ZSTD_readMINMATCH(const void* memPtr, U32 length)
{
    switch (length)
    {
    default :
    case 4 : return MEM_read32(memPtr);
    case 3 : if (MEM_isLittleEndian())
                return MEM_read32(memPtr)<<8;
             else
                return MEM_read32(memPtr)>>8;
    }
}


/* Update hashTable3 up to ip (excluded)
   Assumption : always within prefix (i.e. not within extDict) */
static
ZSTD_ALLOW_POINTER_OVERFLOW_ATTR
U32 ZSTD_insertAndFindFirstIndexHash3 (const ZSTD_MatchState_t* ms,
                                       U32* nextToUpdate3,
                                       const BYTE* const ip)
{
    U32* const hashTable3 = ms->hashTable3;
    U32 const hashLog3 = ms->hashLog3;
    const BYTE* const base = ms->window.base;
    U32 idx = *nextToUpdate3;
    U32 const target = (U32)(ip - base);
    size_t const hash3 = ZSTD_hash3Ptr(ip, hashLog3);
    assert(hashLog3 > 0);

    while(idx < target) {
        hashTable3[ZSTD_hash3Ptr(base+idx, hashLog3)] = idx;
        idx++;
    }

    *nextToUpdate3 = target;
    return hashTable3[hash3];
}


/*-*************************************
*  Binary Tree search
***************************************/
/* ZSTD_insertBt1() : add one or multiple positions to tree.
 * @param ip assumed <= iend-8 .
 * @param target The target of ZSTD_updateTree_internal() - we are filling to this position
 * @return : nb of positions added */
static
ZSTD_ALLOW_POINTER_OVERFLOW_ATTR
U32 ZSTD_insertBt1(
                const ZSTD_MatchState_t* ms,
                const BYTE* const ip, const BYTE* const iend,
                U32 const target,
                U32 const mls, const int extDict)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    U32*   const hashTable = ms->hashTable;
    U32    const hashLog = cParams->hashLog;
    size_t const h  = ZSTD_hashPtr(ip, hashLog, mls);
    U32*   const bt = ms->chainTable;
    U32    const btLog  = cParams->chainLog - 1;
    U32    const btMask = (1 << btLog) - 1;
    U32 matchIndex = hashTable[h];
    size_t commonLengthSmaller=0, commonLengthLarger=0;
    const BYTE* const base = ms->window.base;
    const BYTE* const dictBase = ms->window.dictBase;
    const U32 dictLimit = ms->window.dictLimit;
    const BYTE* const dictEnd = dictBase + dictLimit;
    const BYTE* const prefixStart = base + dictLimit;
    const BYTE* match;
    const U32 curr = (U32)(ip-base);
    const U32 btLow = btMask >= curr ? 0 : curr - btMask;
    U32* smallerPtr = bt + 2*(curr&btMask);
    U32* largerPtr  = smallerPtr + 1;
    U32 dummy32;   /* to be nullified at the end */
    /* windowLow is based on target because
     * we only need positions that will be in the window at the end of the tree update.
     */
    U32 const windowLow = ZSTD_getLowestMatchIndex(ms, target, cParams->windowLog);
    U32 matchEndIdx = curr+8+1;
    size_t bestLength = 8;
    U32 nbCompares = 1U << cParams->searchLog;
#ifdef ZSTD_C_PREDICT
    U32 predictedSmall = *(bt + 2*((curr-1)&btMask) + 0);
    U32 predictedLarge = *(bt + 2*((curr-1)&btMask) + 1);
    predictedSmall += (predictedSmall>0);
    predictedLarge += (predictedLarge>0);
#endif /* ZSTD_C_PREDICT */

    DEBUGLOG(8, "ZSTD_insertBt1 (%u)", curr);

    assert(curr <= target);
    assert(ip <= iend-8);   /* required for h calculation */
    hashTable[h] = curr;   /* Update Hash Table */

    assert(windowLow > 0);
    for (; nbCompares && (matchIndex >= windowLow); --nbCompares) {
        U32* const nextPtr = bt + 2*(matchIndex & btMask);
        size_t matchLength = MIN(commonLengthSmaller, commonLengthLarger);   /* guaranteed minimum nb of common bytes */
        assert(matchIndex < curr);

#ifdef ZSTD_C_PREDICT   /* note : can create issues when hlog small <= 11 */
        const U32* predictPtr = bt + 2*((matchIndex-1) & btMask);   /* written this way, as bt is a roll buffer */
        if (matchIndex == predictedSmall) {
            /* no need to check length, result known */
            *smallerPtr = matchIndex;
            if (matchIndex <= btLow) { smallerPtr=&dummy32; break; }   /* beyond tree size, stop the search */
            smallerPtr = nextPtr+1;               /* new "smaller" => larger of match */
            matchIndex = nextPtr[1];              /* new matchIndex larger than previous (closer to current) */
            predictedSmall = predictPtr[1] + (predictPtr[1]>0);
            continue;
        }
        if (matchIndex == predictedLarge) {
            *largerPtr = matchIndex;
            if (matchIndex <= btLow) { largerPtr=&dummy32; break; }   /* beyond tree size, stop the search */
            largerPtr = nextPtr;
            matchIndex = nextPtr[0];
            predictedLarge = predictPtr[0] + (predictPtr[0]>0);
            continue;
        }
#endif

        if (!extDict || (matchIndex+matchLength >= dictLimit)) {
            assert(matchIndex+matchLength >= dictLimit);   /* might be wrong if actually extDict */
            match = base + matchIndex;
            matchLength += ZSTD_count(ip+matchLength, match+matchLength, iend);
        } else {
            match = dictBase + matchIndex;
            matchLength += ZSTD_count_2segments(ip+matchLength, match+matchLength, iend, dictEnd, prefixStart);
            if (matchIndex+matchLength >= dictLimit)
                match = base + matchIndex;   /* to prepare for next usage of match[matchLength] */
        }

        if (matchLength > bestLength) {
            bestLength = matchLength;
            if (matchLength > matchEndIdx - matchIndex)
                matchEndIdx = matchIndex + (U32)matchLength;
        }

        if (ip+matchLength == iend) {   /* equal : no way to know if inf or sup */
            break;   /* drop , to guarantee consistency ; miss a bit of compression, but other solutions can corrupt tree */
        }

        if (match[matchLength] < ip[matchLength]) {  /* necessarily within buffer */
            /* match is smaller than current */
            *smallerPtr = matchIndex;             /* update smaller idx */
            commonLengthSmaller = matchLength;    /* all smaller will now have at least this guaranteed common length */
            if (matchIndex <= btLow) { smallerPtr=&dummy32; break; }   /* beyond tree size, stop searching */
            smallerPtr = nextPtr+1;               /* new "candidate" => larger than match, which was smaller than target */
            matchIndex = nextPtr[1];              /* new matchIndex, larger than previous and closer to current */
        } else {
            /* match is larger than current */
            *largerPtr = matchIndex;
            commonLengthLarger = matchLength;
            if (matchIndex <= btLow) { largerPtr=&dummy32; break; }   /* beyond tree size, stop searching */
            largerPtr = nextPtr;
            matchIndex = nextPtr[0];
    }   }

    *smallerPtr = *largerPtr = 0;
    {   U32 positions = 0;
        if (bestLength > 384) positions = MIN(192, (U32)(bestLength - 384));   /* speed optimization */
        assert(matchEndIdx > curr + 8);
        return MAX(positions, matchEndIdx - (curr + 8));
    }
}

FORCE_INLINE_TEMPLATE
ZSTD_ALLOW_POINTER_OVERFLOW_ATTR
void ZSTD_updateTree_internal(
                ZSTD_MatchState_t* ms,
                const BYTE* const ip, const BYTE* const iend,
                const U32 mls, const ZSTD_dictMode_e dictMode)
{
    const BYTE* const base = ms->window.base;
    U32 const target = (U32)(ip - base);
    U32 idx = ms->nextToUpdate;
    DEBUGLOG(7, "ZSTD_updateTree_internal, from %u to %u  (dictMode:%u)",
                idx, target, dictMode);

    while(idx < target) {
        U32 const forward = ZSTD_insertBt1(ms, base+idx, iend, target, mls, dictMode == ZSTD_extDict);
        assert(idx < (U32)(idx + forward));
        idx += forward;
    }
    assert((size_t)(ip - base) <= (size_t)(U32)(-1));
    assert((size_t)(iend - base) <= (size_t)(U32)(-1));
    ms->nextToUpdate = target;
}

void ZSTD_updateTree(ZSTD_MatchState_t* ms, const BYTE* ip, const BYTE* iend) {
    ZSTD_updateTree_internal(ms, ip, iend, ms->cParams.minMatch, ZSTD_noDict);
}

FORCE_INLINE_TEMPLATE
ZSTD_ALLOW_POINTER_OVERFLOW_ATTR
U32
ZSTD_insertBtAndGetAllMatches (
                ZSTD_match_t* matches,  /* store result (found matches) in this table (presumed large enough) */
                ZSTD_MatchState_t* ms,
                U32* nextToUpdate3,
                const BYTE* const ip, const BYTE* const iLimit,
                const ZSTD_dictMode_e dictMode,
                const U32 rep[ZSTD_REP_NUM],
                const U32 ll0,  /* tells if associated literal length is 0 or not. This value must be 0 or 1 */
                const U32 lengthToBeat,
                const U32 mls /* template */)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    U32 const sufficient_len = MIN(cParams->targetLength, ZSTD_OPT_NUM -1);
    const BYTE* const base = ms->window.base;
    U32 const curr = (U32)(ip-base);
    U32 const hashLog = cParams->hashLog;
    U32 const minMatch = (mls==3) ? 3 : 4;
    U32* const hashTable = ms->hashTable;
    size_t const h  = ZSTD_hashPtr(ip, hashLog, mls);
    U32 matchIndex  = hashTable[h];
    U32* const bt   = ms->chainTable;
    U32 const btLog = cParams->chainLog - 1;
    U32 const btMask= (1U << btLog) - 1;
    size_t commonLengthSmaller=0, commonLengthLarger=0;
    const BYTE* const dictBase = ms->window.dictBase;
    U32 const dictLimit = ms->window.dictLimit;
    const BYTE* const dictEnd = dictBase + dictLimit;
    const BYTE* const prefixStart = base + dictLimit;
    U32 const btLow = (btMask >= curr) ? 0 : curr - btMask;
    U32 const windowLow = ZSTD_getLowestMatchIndex(ms, curr, cParams->windowLog);
    U32 const matchLow = windowLow ? windowLow : 1;
    U32* smallerPtr = bt + 2*(curr&btMask);
    U32* largerPtr  = bt + 2*(curr&btMask) + 1;
    U32 matchEndIdx = curr+8+1;   /* farthest referenced position of any match => detects repetitive patterns */
    U32 dummy32;   /* to be nullified at the end */
    U32 mnum = 0;
    U32 nbCompares = 1U << cParams->searchLog;

    const ZSTD_MatchState_t* dms    = dictMode == ZSTD_dictMatchState ? ms->dictMatchState : NULL;
    const ZSTD_compressionParameters* const dmsCParams =
                                      dictMode == ZSTD_dictMatchState ? &dms->cParams : NULL;
    const BYTE* const dmsBase       = dictMode == ZSTD_dictMatchState ? dms->window.base : NULL;
    const BYTE* const dmsEnd        = dictMode == ZSTD_dictMatchState ? dms->window.nextSrc : NULL;
    U32         const dmsHighLimit  = dictMode == ZSTD_dictMatchState ? (U32)(dmsEnd - dmsBase) : 0;
    U32         const dmsLowLimit   = dictMode == ZSTD_dictMatchState ? dms->window.lowLimit : 0;
    U32         const dmsIndexDelta = dictMode == ZSTD_dictMatchState ? windowLow - dmsHighLimit : 0;
    U32         const dmsHashLog    = dictMode == ZSTD_dictMatchState ? dmsCParams->hashLog : hashLog;
    U32         const dmsBtLog      = dictMode == ZSTD_dictMatchState ? dmsCParams->chainLog - 1 : btLog;
    U32         const dmsBtMask     = dictMode == ZSTD_dictMatchState ? (1U << dmsBtLog) - 1 : 0;
    U32         const dmsBtLow      = dictMode == ZSTD_dictMatchState && dmsBtMask < dmsHighLimit - dmsLowLimit ? dmsHighLimit - dmsBtMask : dmsLowLimit;

    size_t bestLength = lengthToBeat-1;
    DEBUGLOG(8, "ZSTD_insertBtAndGetAllMatches: current=%u", curr);

    /* check repCode */
    assert(ll0 <= 1);   /* necessarily 1 or 0 */
    {   U32 const lastR = ZSTD_REP_NUM + ll0;
        U32 repCode;
        for (repCode = ll0; repCode < lastR; repCode++) {
            U32 const repOffset = (repCode==ZSTD_REP_NUM) ? (rep[0] - 1) : rep[repCode];
            U32 const repIndex = curr - repOffset;
            U32 repLen = 0;
            assert(curr >= dictLimit);
            if (repOffset-1 /* intentional overflow, discards 0 and -1 */ < curr-dictLimit) {  /* equivalent to `curr > repIndex >= dictLimit` */
                /* We must validate the repcode offset because when we're using a dictionary the
                 * valid offset range shrinks when the dictionary goes out of bounds.
                 */
                if ((repIndex >= windowLow) & (ZSTD_readMINMATCH(ip, minMatch) == ZSTD_readMINMATCH(ip - repOffset, minMatch))) {
                    repLen = (U32)ZSTD_count(ip+minMatch, ip+minMatch-repOffset, iLimit) + minMatch;
                }
            } else {  /* repIndex < dictLimit || repIndex >= curr */
                const BYTE* const repMatch = dictMode == ZSTD_dictMatchState ?
                                             dmsBase + repIndex - dmsIndexDelta :
                                             dictBase + repIndex;
                assert(curr >= windowLow);
                if ( dictMode == ZSTD_extDict
                  && ( ((repOffset-1) /*intentional overflow*/ < curr - windowLow)  /* equivalent to `curr > repIndex >= windowLow` */
                     & (ZSTD_index_overlap_check(dictLimit, repIndex)) )
                  && (ZSTD_readMINMATCH(ip, minMatch) == ZSTD_readMINMATCH(repMatch, minMatch)) ) {
                    repLen = (U32)ZSTD_count_2segments(ip+minMatch, repMatch+minMatch, iLimit, dictEnd, prefixStart) + minMatch;
                }
                if (dictMode == ZSTD_dictMatchState
                  && ( ((repOffset-1) /*intentional overflow*/ < curr - (dmsLowLimit + dmsIndexDelta))  /* equivalent to `curr > repIndex >= dmsLowLimit` */
                     & (ZSTD_index_overlap_check(dictLimit, repIndex)) )
                  && (ZSTD_readMINMATCH(ip, minMatch) == ZSTD_readMINMATCH(repMatch, minMatch)) ) {
                    repLen = (U32)ZSTD_count_2segments(ip+minMatch, repMatch+minMatch, iLimit, dmsEnd, prefixStart) + minMatch;
            }   }
            /* save longer solution */
            if (repLen > bestLength) {
                DEBUGLOG(8, "found repCode %u (ll0:%u, offset:%u) of length %u",
                            repCode, ll0, repOffset, repLen);
                bestLength = repLen;
                matches[mnum].off = REPCODE_TO_OFFBASE(repCode - ll0 + 1);  /* expect value between 1 and 3 */
                matches[mnum].len = (U32)repLen;
                mnum++;
                if ( (repLen > sufficient_len)
                   | (ip+repLen == iLimit) ) {  /* best possible */
                    return mnum;
    }   }   }   }

    /* HC3 match finder */
    if ((mls == 3) /*static*/ && (bestLength < mls)) {
        U32 const matchIndex3 = ZSTD_insertAndFindFirstIndexHash3(ms, nextToUpdate3, ip);
        if ((matchIndex3 >= matchLow)
          & (curr - matchIndex3 < (1<<18)) /*heuristic : longer distance likely too expensive*/ ) {
            size_t mlen;
            if ((dictMode == ZSTD_noDict) /*static*/ || (dictMode == ZSTD_dictMatchState) /*static*/ || (matchIndex3 >= dictLimit)) {
                const BYTE* const match = base + matchIndex3;
                mlen = ZSTD_count(ip, match, iLimit);
            } else {
                const BYTE* const match = dictBase + matchIndex3;
                mlen = ZSTD_count_2segments(ip, match, iLimit, dictEnd, prefixStart);
            }

            /* save best solution */
            if (mlen >= mls /* == 3 > bestLength */) {
                DEBUGLOG(8, "found small match with hlog3, of length %u",
                            (U32)mlen);
                bestLength = mlen;
                assert(curr > matchIndex3);
                assert(mnum==0);  /* no prior solution */
                matches[0].off = OFFSET_TO_OFFBASE(curr - matchIndex3);
                matches[0].len = (U32)mlen;
                mnum = 1;
                if ( (mlen > sufficient_len) |
                     (ip+mlen == iLimit) ) {  /* best possible length */
                    ms->nextToUpdate = curr+1;  /* skip insertion */
                    return 1;
        }   }   }
        /* no dictMatchState lookup: dicts don't have a populated HC3 table */
    }  /* if (mls == 3) */

    hashTable[h] = curr;   /* Update Hash Table */

    for (; nbCompares && (matchIndex >= matchLow); --nbCompares) {
        U32* const nextPtr = bt + 2*(matchIndex & btMask);
        const BYTE* match;
        size_t matchLength = MIN(commonLengthSmaller, commonLengthLarger);   /* guaranteed minimum nb of common bytes */
        assert(curr > matchIndex);

        if ((dictMode == ZSTD_noDict) || (dictMode == ZSTD_dictMatchState) || (matchIndex+matchLength >= dictLimit)) {
            assert(matchIndex+matchLength >= dictLimit);  /* ensure the condition is correct when !extDict */
            match = base + matchIndex;
            if (matchIndex >= dictLimit) assert(memcmp(match, ip, matchLength) == 0);  /* ensure early section of match is equal as expected */
            matchLength += ZSTD_count(ip+matchLength, match+matchLength, iLimit);
        } else {
            match = dictBase + matchIndex;
            assert(memcmp(match, ip, matchLength) == 0);  /* ensure early section of match is equal as expected */
            matchLength += ZSTD_count_2segments(ip+matchLength, match+matchLength, iLimit, dictEnd, prefixStart);
            if (matchIndex+matchLength >= dictLimit)
                match = base + matchIndex;   /* prepare for match[matchLength] read */
        }

        if (matchLength > bestLength) {
            DEBUGLOG(8, "found match of length %u at distance %u (offBase=%u)",
                    (U32)matchLength, curr - matchIndex, OFFSET_TO_OFFBASE(curr - matchIndex));
            assert(matchEndIdx > matchIndex);
            if (matchLength > matchEndIdx - matchIndex)
                matchEndIdx = matchIndex + (U32)matchLength;
            bestLength = matchLength;
            matches[mnum].off = OFFSET_TO_OFFBASE(curr - matchIndex);
            matches[mnum].len = (U32)matchLength;
            mnum++;
            if ( (matchLength > ZSTD_OPT_NUM)
               | (ip+matchLength == iLimit) /* equal : no way to know if inf or sup */) {
                if (dictMode == ZSTD_dictMatchState) nbCompares = 0; /* break should also skip searching dms */
                break; /* drop, to preserve bt consistency (miss a little bit of compression) */
        }   }

        if (match[matchLength] < ip[matchLength]) {
            /* match smaller than current */
            *smallerPtr = matchIndex;             /* update smaller idx */
            commonLengthSmaller = matchLength;    /* all smaller will now have at least this guaranteed common length */
            if (matchIndex <= btLow) { smallerPtr=&dummy32; break; }   /* beyond tree size, stop the search */
            smallerPtr = nextPtr+1;               /* new candidate => larger than match, which was smaller than current */
            matchIndex = nextPtr[1];              /* new matchIndex, larger than previous, closer to current */
        } else {
            *largerPtr = matchIndex;
            commonLengthLarger = matchLength;
            if (matchIndex <= btLow) { largerPtr=&dummy32; break; }   /* beyond tree size, stop the search */
            largerPtr = nextPtr;
            matchIndex = nextPtr[0];
    }   }

    *smallerPtr = *largerPtr = 0;

    assert(nbCompares <= (1U << ZSTD_SEARCHLOG_MAX)); /* Check we haven't underflowed. */
    if (dictMode == ZSTD_dictMatchState && nbCompares) {
        size_t const dmsH = ZSTD_hashPtr(ip, dmsHashLog, mls);
        U32 dictMatchIndex = dms->hashTable[dmsH];
        const U32* const dmsBt = dms->chainTable;
        commonLengthSmaller = commonLengthLarger = 0;
        for (; nbCompares && (dictMatchIndex > dmsLowLimit); --nbCompares) {
            const U32* const nextPtr = dmsBt + 2*(dictMatchIndex & dmsBtMask);
            size_t matchLength = MIN(commonLengthSmaller, commonLengthLarger);   /* guaranteed minimum nb of common bytes */
            const BYTE* match = dmsBase + dictMatchIndex;
            matchLength += ZSTD_count_2segments(ip+matchLength, match+matchLength, iLimit, dmsEnd, prefixStart);
            if (dictMatchIndex+matchLength >= dmsHighLimit)
                match = base + dictMatchIndex + dmsIndexDelta;   /* to prepare for next usage of match[matchLength] */

            if (matchLength > bestLength) {
                matchIndex = dictMatchIndex + dmsIndexDelta;
                DEBUGLOG(8, "found dms match of length %u at distance %u (offBase=%u)",
                        (U32)matchLength, curr - matchIndex, OFFSET_TO_OFFBASE(curr - matchIndex));
                if (matchLength > matchEndIdx - matchIndex)
                    matchEndIdx = matchIndex + (U32)matchLength;
                bestLength = matchLength;
                matches[mnum].off = OFFSET_TO_OFFBASE(curr - matchIndex);
                matches[mnum].len = (U32)matchLength;
                mnum++;
                if ( (matchLength > ZSTD_OPT_NUM)
                   | (ip+matchLength == iLimit) /* equal : no way to know if inf or sup */) {
                    break;   /* drop, to guarantee consistency (miss a little bit of compression) */
            }   }

            if (dictMatchIndex <= dmsBtLow) { break; }   /* beyond tree size, stop the search */
            if (match[matchLength] < ip[matchLength]) {
                commonLengthSmaller = matchLength;    /* all smaller will now have at least this guaranteed common length */
                dictMatchIndex = nextPtr[1];              /* new matchIndex larger than previous (closer to current) */
            } else {
                /* match is larger than current */
                commonLengthLarger = matchLength;
                dictMatchIndex = nextPtr[0];
    }   }   }  /* if (dictMode == ZSTD_dictMatchState) */

    assert(matchEndIdx > curr+8);
    ms->nextToUpdate = matchEndIdx - 8;  /* skip repetitive patterns */
    return mnum;
}

typedef U32 (*ZSTD_getAllMatchesFn)(
    ZSTD_match_t*,
    ZSTD_MatchState_t*,
    U32*,
    const BYTE*,
    const BYTE*,
    const U32 rep[ZSTD_REP_NUM],
    U32 const ll0,
    U32 const lengthToBeat);

FORCE_INLINE_TEMPLATE
ZSTD_ALLOW_POINTER_OVERFLOW_ATTR
U32 ZSTD_btGetAllMatches_internal(
        ZSTD_match_t* matches,
        ZSTD_MatchState_t* ms,
        U32* nextToUpdate3,
        const BYTE* ip,
        const BYTE* const iHighLimit,
        const U32 rep[ZSTD_REP_NUM],
        U32 const ll0,
        U32 const lengthToBeat,
        const ZSTD_dictMode_e dictMode,
        const U32 mls)
{
    assert(BOUNDED(3, ms->cParams.minMatch, 6) == mls);
    DEBUGLOG(8, "ZSTD_BtGetAllMatches(dictMode=%d, mls=%u)", (int)dictMode, mls);
    if (ip < ms->window.base + ms->nextToUpdate)
        return 0;   /* skipped area */
    ZSTD_updateTree_internal(ms, ip, iHighLimit, mls, dictMode);
    return ZSTD_insertBtAndGetAllMatches(matches, ms, nextToUpdate3, ip, iHighLimit, dictMode, rep, ll0, lengthToBeat, mls);
}

#define ZSTD_BT_GET_ALL_MATCHES_FN(dictMode, mls) ZSTD_btGetAllMatches_##dictMode##_##mls

#define GEN_ZSTD_BT_GET_ALL_MATCHES_(dictMode, mls)            \
    static U32 ZSTD_BT_GET_ALL_MATCHES_FN(dictMode, mls)(      \
            ZSTD_match_t* matches,                             \
            ZSTD_MatchState_t* ms,                             \
            U32* nextToUpdate3,                                \
            const BYTE* ip,                                    \
            const BYTE* const iHighLimit,                      \
            const U32 rep[ZSTD_REP_NUM],                       \
            U32 const ll0,                                     \
            U32 const lengthToBeat)                            \
    {                                                          \
        return ZSTD_btGetAllMatches_internal(                  \
                matches, ms, nextToUpdate3, ip, iHighLimit,    \
                rep, ll0, lengthToBeat, ZSTD_##dictMode, mls); \
    }

#define GEN_ZSTD_BT_GET_ALL_MATCHES(dictMode)  \
    GEN_ZSTD_BT_GET_ALL_MATCHES_(dictMode, 3)  \
    GEN_ZSTD_BT_GET_ALL_MATCHES_(dictMode, 4)  \
    GEN_ZSTD_BT_GET_ALL_MATCHES_(dictMode, 5)  \
    GEN_ZSTD_BT_GET_ALL_MATCHES_(dictMode, 6)

GEN_ZSTD_BT_GET_ALL_MATCHES(noDict)
GEN_ZSTD_BT_GET_ALL_MATCHES(extDict)
GEN_ZSTD_BT_GET_ALL_MATCHES(dictMatchState)

#define ZSTD_BT_GET_ALL_MATCHES_ARRAY(dictMode)  \
    {                                            \
        ZSTD_BT_GET_ALL_MATCHES_FN(dictMode, 3), \
        ZSTD_BT_GET_ALL_MATCHES_FN(dictMode, 4), \
        ZSTD_BT_GET_ALL_MATCHES_FN(dictMode, 5), \
        ZSTD_BT_GET_ALL_MATCHES_FN(dictMode, 6)  \
    }

static ZSTD_getAllMatchesFn
ZSTD_selectBtGetAllMatches(ZSTD_MatchState_t const* ms, ZSTD_dictMode_e const dictMode)
{
    ZSTD_getAllMatchesFn const getAllMatchesFns[3][4] = {
        ZSTD_BT_GET_ALL_MATCHES_ARRAY(noDict),
        ZSTD_BT_GET_ALL_MATCHES_ARRAY(extDict),
        ZSTD_BT_GET_ALL_MATCHES_ARRAY(dictMatchState)
    };
    U32 const mls = BOUNDED(3, ms->cParams.minMatch, 6);
    assert((U32)dictMode < 3);
    assert(mls - 3 < 4);
    return getAllMatchesFns[(int)dictMode][mls - 3];
}

/* ***********************
*  LDM helper functions  *
*************************/

/* Struct containing info needed to make decision about ldm inclusion */
typedef struct {
    RawSeqStore_t seqStore;   /* External match candidates store for this block */
    U32 startPosInBlock;      /* Start position of the current match candidate */
    U32 endPosInBlock;        /* End position of the current match candidate */
    U32 offset;               /* Offset of the match candidate */
} ZSTD_optLdm_t;

/* ZSTD_optLdm_skipRawSeqStoreBytes():
 * Moves forward in @rawSeqStore by @nbBytes,
 * which will update the fields 'pos' and 'posInSequence'.
 */
static void ZSTD_optLdm_skipRawSeqStoreBytes(RawSeqStore_t* rawSeqStore, size_t nbBytes)
{
    U32 currPos = (U32)(rawSeqStore->posInSequence + nbBytes);
    while (currPos && rawSeqStore->pos < rawSeqStore->size) {
        rawSeq currSeq = rawSeqStore->seq[rawSeqStore->pos];
        if (currPos >= currSeq.litLength + currSeq.matchLength) {
            currPos -= currSeq.litLength + currSeq.matchLength;
            rawSeqStore->pos++;
        } else {
            rawSeqStore->posInSequence = currPos;
            break;
        }
    }
    if (currPos == 0 || rawSeqStore->pos == rawSeqStore->size) {
        rawSeqStore->posInSequence = 0;
    }
}

/* ZSTD_opt_getNextMatchAndUpdateSeqStore():
 * Calculates the beginning and end of the next match in the current block.
 * Updates 'pos' and 'posInSequence' of the ldmSeqStore.
 */
static void
ZSTD_opt_getNextMatchAndUpdateSeqStore(ZSTD_optLdm_t* optLdm, U32 currPosInBlock,
                                       U32 blockBytesRemaining)
{
    rawSeq currSeq;
    U32 currBlockEndPos;
    U32 literalsBytesRemaining;
    U32 matchBytesRemaining;

    /* Setting match end position to MAX to ensure we never use an LDM during this block */
    if (optLdm->seqStore.size == 0 || optLdm->seqStore.pos >= optLdm->seqStore.size) {
        optLdm->startPosInBlock = UINT_MAX;
        optLdm->endPosInBlock = UINT_MAX;
        return;
    }
    /* Calculate appropriate bytes left in matchLength and litLength
     * after adjusting based on ldmSeqStore->posInSequence */
    currSeq = optLdm->seqStore.seq[optLdm->seqStore.pos];
    assert(optLdm->seqStore.posInSequence <= currSeq.litLength + currSeq.matchLength);
    currBlockEndPos = currPosInBlock + blockBytesRemaining;
    literalsBytesRemaining = (optLdm->seqStore.posInSequence < currSeq.litLength) ?
            currSeq.litLength - (U32)optLdm->seqStore.posInSequence :
            0;
    matchBytesRemaining = (literalsBytesRemaining == 0) ?
            currSeq.matchLength - ((U32)optLdm->seqStore.posInSequence - currSeq.litLength) :
            currSeq.matchLength;

    /* If there are more literal bytes than bytes remaining in block, no ldm is possible */
    if (literalsBytesRemaining >= blockBytesRemaining) {
        optLdm->startPosInBlock = UINT_MAX;
        optLdm->endPosInBlock = UINT_MAX;
        ZSTD_optLdm_skipRawSeqStoreBytes(&optLdm->seqStore, blockBytesRemaining);
        return;
    }

    /* Matches may be < minMatch by this process. In that case, we will reject them
       when we are deciding whether or not to add the ldm */
    optLdm->startPosInBlock = currPosInBlock + literalsBytesRemaining;
    optLdm->endPosInBlock = optLdm->startPosInBlock + matchBytesRemaining;
    optLdm->offset = currSeq.offset;

    if (optLdm->endPosInBlock > currBlockEndPos) {
        /* Match ends after the block ends, we can't use the whole match */
        optLdm->endPosInBlock = currBlockEndPos;
        ZSTD_optLdm_skipRawSeqStoreBytes(&optLdm->seqStore, currBlockEndPos - currPosInBlock);
    } else {
        /* Consume nb of bytes equal to size of sequence left */
        ZSTD_optLdm_skipRawSeqStoreBytes(&optLdm->seqStore, literalsBytesRemaining + matchBytesRemaining);
    }
}

/* ZSTD_optLdm_maybeAddMatch():
 * Adds a match if it's long enough,
 * based on it's 'matchStartPosInBlock' and 'matchEndPosInBlock',
 * into 'matches'. Maintains the correct ordering of 'matches'.
 */
static void ZSTD_optLdm_maybeAddMatch(ZSTD_match_t* matches, U32* nbMatches,
                                      const ZSTD_optLdm_t* optLdm, U32 currPosInBlock,
                                      U32 minMatch)
{
    U32 const posDiff = currPosInBlock - optLdm->startPosInBlock;
    /* Note: ZSTD_match_t actually contains offBase and matchLength (before subtracting MINMATCH) */
    U32 const candidateMatchLength = optLdm->endPosInBlock - optLdm->startPosInBlock - posDiff;

    /* Ensure that current block position is not outside of the match */
    if (currPosInBlock < optLdm->startPosInBlock
      || currPosInBlock >= optLdm->endPosInBlock
      || candidateMatchLength < minMatch) {
        return;
    }

    if (*nbMatches == 0 || ((candidateMatchLength > matches[*nbMatches-1].len) && *nbMatches < ZSTD_OPT_NUM)) {
        U32 const candidateOffBase = OFFSET_TO_OFFBASE(optLdm->offset);
        DEBUGLOG(6, "ZSTD_optLdm_maybeAddMatch(): Adding ldm candidate match (offBase: %u matchLength %u) at block position=%u",
                 candidateOffBase, candidateMatchLength, currPosInBlock);
        matches[*nbMatches].len = candidateMatchLength;
        matches[*nbMatches].off = candidateOffBase;
        (*nbMatches)++;
    }
}

/* ZSTD_optLdm_processMatchCandidate():
 * Wrapper function to update ldm seq store and call ldm functions as necessary.
 */
static void
ZSTD_optLdm_processMatchCandidate(ZSTD_optLdm_t* optLdm,
                                  ZSTD_match_t* matches, U32* nbMatches,
                                  U32 currPosInBlock, U32 remainingBytes,
                                  U32 minMatch)
{
    if (optLdm->seqStore.size == 0 || optLdm->seqStore.pos >= optLdm->seqStore.size) {
        return;
    }

    if (currPosInBlock >= optLdm->endPosInBlock) {
        if (currPosInBlock > optLdm->endPosInBlock) {
            /* The position at which ZSTD_optLdm_processMatchCandidate() is called is not necessarily
             * at the end of a match from the ldm seq store, and will often be some bytes
             * over beyond matchEndPosInBlock. As such, we need to correct for these "overshoots"
             */
            U32 const posOvershoot = currPosInBlock - optLdm->endPosInBlock;
            ZSTD_optLdm_skipRawSeqStoreBytes(&optLdm->seqStore, posOvershoot);
        }
        ZSTD_opt_getNextMatchAndUpdateSeqStore(optLdm, currPosInBlock, remainingBytes);
    }
    ZSTD_optLdm_maybeAddMatch(matches, nbMatches, optLdm, currPosInBlock, minMatch);
}


/*-*******************************
*  Optimal parser
*********************************/

#if 0 /* debug */

static void
listStats(const U32* table, int lastEltID)
{
    int const nbElts = lastEltID + 1;
    int enb;
    for (enb=0; enb < nbElts; enb++) {
        (void)table;
        /* RAWLOG(2, "%3i:%3i,  ", enb, table[enb]); */
        RAWLOG(2, "%4i,", table[enb]);
    }
    RAWLOG(2, " \n");
}

#endif

#define LIT_PRICE(_p) (int)ZSTD_rawLiteralsCost(_p, 1, optStatePtr, optLevel)
#define LL_PRICE(_l) (int)ZSTD_litLengthPrice(_l, optStatePtr, optLevel)
#define LL_INCPRICE(_l) (LL_PRICE(_l) - LL_PRICE(_l-1))

FORCE_INLINE_TEMPLATE
ZSTD_ALLOW_POINTER_OVERFLOW_ATTR
size_t
ZSTD_compressBlock_opt_generic(ZSTD_MatchState_t* ms,
                               SeqStore_t* seqStore,
                               U32 rep[ZSTD_REP_NUM],
                         const void* src, size_t srcSize,
                         const int optLevel,
                         const ZSTD_dictMode_e dictMode)
{
    optState_t* const optStatePtr = &ms->opt;
    const BYTE* const istart = (const BYTE*)src;
    const BYTE* ip = istart;
    const BYTE* anchor = istart;
    const BYTE* const iend = istart + srcSize;
    const BYTE* const ilimit = iend - 8;
    const BYTE* const base = ms->window.base;
    const BYTE* const prefixStart = base + ms->window.dictLimit;
    const ZSTD_compressionParameters* const cParams = &ms->cParams;

    ZSTD_getAllMatchesFn getAllMatches = ZSTD_selectBtGetAllMatches(ms, dictMode);

    U32 const sufficient_len = MIN(cParams->targetLength, ZSTD_OPT_NUM -1);
    U32 const minMatch = (cParams->minMatch == 3) ? 3 : 4;
    U32 nextToUpdate3 = ms->nextToUpdate;

    ZSTD_optimal_t* const opt = optStatePtr->priceTable;
    ZSTD_match_t* const matches = optStatePtr->matchTable;
    ZSTD_optimal_t lastStretch;
    ZSTD_optLdm_t optLdm;

    ZSTD_memset(&lastStretch, 0, sizeof(ZSTD_optimal_t));

    optLdm.seqStore = ms->ldmSeqStore ? *ms->ldmSeqStore : kNullRawSeqStore;
    optLdm.endPosInBlock = optLdm.startPosInBlock = optLdm.offset = 0;
    ZSTD_opt_getNextMatchAndUpdateSeqStore(&optLdm, (U32)(ip-istart), (U32)(iend-ip));

    /* init */
    DEBUGLOG(5, "ZSTD_compressBlock_opt_generic: current=%u, prefix=%u, nextToUpdate=%u",
                (U32)(ip - base), ms->window.dictLimit, ms->nextToUpdate);
    assert(optLevel <= 2);
    ZSTD_rescaleFreqs(optStatePtr, (const BYTE*)src, srcSize, optLevel);
    ip += (ip==prefixStart);

    /* Match Loop */
    while (ip < ilimit) {
        U32 cur, last_pos = 0;

        /* find first match */
        {   U32 const litlen = (U32)(ip - anchor);
            U32 const ll0 = !litlen;
            U32 nbMatches = getAllMatches(matches, ms, &nextToUpdate3, ip, iend, rep, ll0, minMatch);
            ZSTD_optLdm_processMatchCandidate(&optLdm, matches, &nbMatches,
                                              (U32)(ip-istart), (U32)(iend-ip),
                                              minMatch);
            if (!nbMatches) {
                DEBUGLOG(8, "no match found at cPos %u", (unsigned)(ip-istart));
                ip++;
                continue;
            }

            /* Match found: let's store this solution, and eventually find more candidates.
             * During this forward pass, @opt is used to store stretches,
             * defined as "a match followed by N literals".
             * Note how this is different from a Sequence, which is "N literals followed by a match".
             * Storing stretches allows us to store different match predecessors
             * for each literal position part of a literals run. */

            /* initialize opt[0] */
            opt[0].mlen = 0;  /* there are only literals so far */
            opt[0].litlen = litlen;
            /* No need to include the actual price of the literals before the first match
             * because it is static for the duration of the forward pass, and is included
             * in every subsequent price. But, we include the literal length because
             * the cost variation of litlen depends on the value of litlen.
             */
            opt[0].price = LL_PRICE(litlen);
            ZSTD_STATIC_ASSERT(sizeof(opt[0].rep[0]) == sizeof(rep[0]));
            ZSTD_memcpy(&opt[0].rep, rep, sizeof(opt[0].rep));

            /* large match -> immediate encoding */
            {   U32 const maxML = matches[nbMatches-1].len;
                U32 const maxOffBase = matches[nbMatches-1].off;
                DEBUGLOG(6, "found %u matches of maxLength=%u and maxOffBase=%u at cPos=%u => start new series",
                            nbMatches, maxML, maxOffBase, (U32)(ip-prefixStart));

                if (maxML > sufficient_len) {
                    lastStretch.litlen = 0;
                    lastStretch.mlen = maxML;
                    lastStretch.off = maxOffBase;
                    DEBUGLOG(6, "large match (%u>%u) => immediate encoding",
                                maxML, sufficient_len);
                    cur = 0;
                    last_pos = maxML;
                    goto _shortestPath;
            }   }

            /* set prices for first matches starting position == 0 */
            assert(opt[0].price >= 0);
            {   U32 pos;
                U32 matchNb;
                for (pos = 1; pos < minMatch; pos++) {
                    opt[pos].price = ZSTD_MAX_PRICE;
                    opt[pos].mlen = 0;
                    opt[pos].litlen = litlen + pos;
                }
                for (matchNb = 0; matchNb < nbMatches; matchNb++) {
                    U32 const offBase = matches[matchNb].off;
                    U32 const end = matches[matchNb].len;
                    for ( ; pos <= end ; pos++ ) {
                        int const matchPrice = (int)ZSTD_getMatchPrice(offBase, pos, optStatePtr, optLevel);
                        int const sequencePrice = opt[0].price + matchPrice;
                        DEBUGLOG(7, "rPos:%u => set initial price : %.2f",
                                    pos, ZSTD_fCost(sequencePrice));
                        opt[pos].mlen = pos;
                        opt[pos].off = offBase;
                        opt[pos].litlen = 0; /* end of match */
                        opt[pos].price = sequencePrice + LL_PRICE(0);
                    }
                }
                last_pos = pos-1;
                opt[pos].price = ZSTD_MAX_PRICE;
            }
        }

        /* check further positions */
        for (cur = 1; cur <= last_pos; cur++) {
            const BYTE* const inr = ip + cur;
            assert(cur <= ZSTD_OPT_NUM);
            DEBUGLOG(7, "cPos:%i==rPos:%u", (int)(inr-istart), cur);

            /* Fix current position with one literal if cheaper */
            {   U32 const litlen = opt[cur-1].litlen + 1;
                int const price = opt[cur-1].price
                                + LIT_PRICE(ip+cur-1)
                                + LL_INCPRICE(litlen);
                assert(price < 1000000000); /* overflow check */
                if (price <= opt[cur].price) {
                    ZSTD_optimal_t const prevMatch = opt[cur];
                    DEBUGLOG(7, "cPos:%i==rPos:%u : better price (%.2f<=%.2f) using literal (ll==%u) (hist:%u,%u,%u)",
                                (int)(inr-istart), cur, ZSTD_fCost(price), ZSTD_fCost(opt[cur].price), litlen,
                                opt[cur-1].rep[0], opt[cur-1].rep[1], opt[cur-1].rep[2]);
                    opt[cur] = opt[cur-1];
                    opt[cur].litlen = litlen;
                    opt[cur].price = price;
                    if ( (optLevel >= 1) /* additional check only for higher modes */
                      && (prevMatch.litlen == 0) /* replace a match */
                      && (LL_INCPRICE(1) < 0) /* ll1 is cheaper than ll0 */
                      && LIKELY(ip + cur < iend)
                    ) {
                        /* check next position, in case it would be cheaper */
                        int with1literal = prevMatch.price + LIT_PRICE(ip+cur) + LL_INCPRICE(1);
                        int withMoreLiterals = price + LIT_PRICE(ip+cur) + LL_INCPRICE(litlen+1);
                        DEBUGLOG(7, "then at next rPos %u : match+1lit %.2f vs %ulits %.2f",
                                cur+1, ZSTD_fCost(with1literal), litlen+1, ZSTD_fCost(withMoreLiterals));
                        if ( (with1literal < withMoreLiterals)
                          && (with1literal < opt[cur+1].price) ) {
                            /* update offset history - before it disappears */
                            U32 const prev = cur - prevMatch.mlen;
                            Repcodes_t const newReps = ZSTD_newRep(opt[prev].rep, prevMatch.off, opt[prev].litlen==0);
                            assert(cur >= prevMatch.mlen);
                            DEBUGLOG(7, "==> match+1lit is cheaper (%.2f < %.2f) (hist:%u,%u,%u) !",
                                        ZSTD_fCost(with1literal), ZSTD_fCost(withMoreLiterals),
                                        newReps.rep[0], newReps.rep[1], newReps.rep[2] );
                            opt[cur+1] = prevMatch;  /* mlen & offbase */
                            ZSTD_memcpy(opt[cur+1].rep, &newReps, sizeof(Repcodes_t));
                            opt[cur+1].litlen = 1;
                            opt[cur+1].price = with1literal;
                            if (last_pos < cur+1) last_pos = cur+1;
                        }
                    }
                } else {
                    DEBUGLOG(7, "cPos:%i==rPos:%u : literal would cost more (%.2f>%.2f)",
                                (int)(inr-istart), cur, ZSTD_fCost(price), ZSTD_fCost(opt[cur].price));
                }
            }

            /* Offset history is not updated during match comparison.
             * Do it here, now that the match is selected and confirmed.
             */
            ZSTD_STATIC_ASSERT(sizeof(opt[cur].rep) == sizeof(Repcodes_t));
            assert(cur >= opt[cur].mlen);
            if (opt[cur].litlen == 0) {
                /* just finished a match => alter offset history */
                U32 const prev = cur - opt[cur].mlen;
                Repcodes_t const newReps = ZSTD_newRep(opt[prev].rep, opt[cur].off, opt[prev].litlen==0);
                ZSTD_memcpy(opt[cur].rep, &newReps, sizeof(Repcodes_t));
            }

            /* last match must start at a minimum distance of 8 from oend */
            if (inr > ilimit) continue;

            if (cur == last_pos) break;

            if ( (optLevel==0) /*static_test*/
              && (opt[cur+1].price <= opt[cur].price + (BITCOST_MULTIPLIER/2)) ) {
                DEBUGLOG(7, "skip current position : next rPos(%u) price is cheaper", cur+1);
                continue;  /* skip unpromising positions; about ~+6% speed, -0.01 ratio */
            }

            assert(opt[cur].price >= 0);
            {   U32 const ll0 = (opt[cur].litlen == 0);
                int const previousPrice = opt[cur].price;
                int const basePrice = previousPrice + LL_PRICE(0);
                U32 nbMatches = getAllMatches(matches, ms, &nextToUpdate3, inr, iend, opt[cur].rep, ll0, minMatch);
                U32 matchNb;

                ZSTD_optLdm_processMatchCandidate(&optLdm, matches, &nbMatches,
                                                  (U32)(inr-istart), (U32)(iend-inr),
                                                  minMatch);

                if (!nbMatches) {
                    DEBUGLOG(7, "rPos:%u : no match found", cur);
                    continue;
                }

                {   U32 const longestML = matches[nbMatches-1].len;
                    DEBUGLOG(7, "cPos:%i==rPos:%u, found %u matches, of longest ML=%u",
                                (int)(inr-istart), cur, nbMatches, longestML);

                    if ( (longestML > sufficient_len)
                      || (cur + longestML >= ZSTD_OPT_NUM)
                      || (ip + cur + longestML >= iend) ) {
                        lastStretch.mlen = longestML;
                        lastStretch.off = matches[nbMatches-1].off;
                        lastStretch.litlen = 0;
                        last_pos = cur + longestML;
                        goto _shortestPath;
                }   }

                /* set prices using matches found at position == cur */
                for (matchNb = 0; matchNb < nbMatches; matchNb++) {
                    U32 const offset = matches[matchNb].off;
                    U32 const lastML = matches[matchNb].len;
                    U32 const startML = (matchNb>0) ? matches[matchNb-1].len+1 : minMatch;
                    U32 mlen;

                    DEBUGLOG(7, "testing match %u => offBase=%4u, mlen=%2u, llen=%2u",
                                matchNb, matches[matchNb].off, lastML, opt[cur].litlen);

                    for (mlen = lastML; mlen >= startML; mlen--) {  /* scan downward */
                        U32 const pos = cur + mlen;
                        int const price = basePrice + (int)ZSTD_getMatchPrice(offset, mlen, optStatePtr, optLevel);

                        if ((pos > last_pos) || (price < opt[pos].price)) {
                            DEBUGLOG(7, "rPos:%u (ml=%2u) => new better price (%.2f<%.2f)",
                                        pos, mlen, ZSTD_fCost(price), ZSTD_fCost(opt[pos].price));
                            while (last_pos < pos) {
                                /* fill empty positions, for future comparisons */
                                last_pos++;
                                opt[last_pos].price = ZSTD_MAX_PRICE;
                                opt[last_pos].litlen = !0;  /* just needs to be != 0, to mean "not an end of match" */
                            }
                            opt[pos].mlen = mlen;
                            opt[pos].off = offset;
                            opt[pos].litlen = 0;
                            opt[pos].price = price;
                        } else {
                            DEBUGLOG(7, "rPos:%u (ml=%2u) => new price is worse (%.2f>=%.2f)",
                                        pos, mlen, ZSTD_fCost(price), ZSTD_fCost(opt[pos].price));
                            if (optLevel==0) break;  /* early update abort; gets ~+10% speed for about -0.01 ratio loss */
                        }
            }   }   }
            opt[last_pos+1].price = ZSTD_MAX_PRICE;
        }  /* for (cur = 1; cur <= last_pos; cur++) */

        lastStretch = opt[last_pos];
        assert(cur >= lastStretch.mlen);
        cur = last_pos - lastStretch.mlen;

_shortestPath:   /* cur, last_pos, best_mlen, best_off have to be set */
        assert(opt[0].mlen == 0);
        assert(last_pos >= lastStretch.mlen);
        assert(cur == last_pos - lastStretch.mlen);

        if (lastStretch.mlen==0) {
            /* no solution : all matches have been converted into literals */
            assert(lastStretch.litlen == (ip - anchor) + last_pos);
            ip += last_pos;
            continue;
        }
        assert(lastStretch.off > 0);

        /* Update offset history */
        if (lastStretch.litlen == 0) {
            /* finishing on a match : update offset history */
            Repcodes_t const reps = ZSTD_newRep(opt[cur].rep, lastStretch.off, opt[cur].litlen==0);
            ZSTD_memcpy(rep, &reps, sizeof(Repcodes_t));
        } else {
            ZSTD_memcpy(rep, lastStretch.rep, sizeof(Repcodes_t));
            assert(cur >= lastStretch.litlen);
            cur -= lastStretch.litlen;
        }

        /* Let's write the shortest path solution.
         * It is stored in @opt in reverse order,
         * starting from @storeEnd (==cur+2),
         * effectively partially @opt overwriting.
         * Content is changed too:
         * - So far, @opt stored stretches, aka a match followed by literals
         * - Now, it will store sequences, aka literals followed by a match
         */
        {   U32 const storeEnd = cur + 2;
            U32 storeStart = storeEnd;
            U32 stretchPos = cur;

            DEBUGLOG(6, "start reverse traversal (last_pos:%u, cur:%u)",
                        last_pos, cur); (void)last_pos;
            assert(storeEnd < ZSTD_OPT_SIZE);
            DEBUGLOG(6, "last stretch copied into pos=%u (llen=%u,mlen=%u,ofc=%u)",
                        storeEnd, lastStretch.litlen, lastStretch.mlen, lastStretch.off);
            if (lastStretch.litlen > 0) {
                /* last "sequence" is unfinished: just a bunch of literals */
                opt[storeEnd].litlen = lastStretch.litlen;
                opt[storeEnd].mlen = 0;
                storeStart = storeEnd-1;
                opt[storeStart] = lastStretch;
            } {
                opt[storeEnd] = lastStretch;  /* note: litlen will be fixed */
                storeStart = storeEnd;
            }
            while (1) {
                ZSTD_optimal_t nextStretch = opt[stretchPos];
                opt[storeStart].litlen = nextStretch.litlen;
                DEBUGLOG(6, "selected sequence (llen=%u,mlen=%u,ofc=%u)",
                            opt[storeStart].litlen, opt[storeStart].mlen, opt[storeStart].off);
                if (nextStretch.mlen == 0) {
                    /* reaching beginning of segment */
                    break;
                }
                storeStart--;
                opt[storeStart] = nextStretch; /* note: litlen will be fixed */
                assert(nextStretch.litlen + nextStretch.mlen <= stretchPos);
                stretchPos -= nextStretch.litlen + nextStretch.mlen;
            }

            /* save sequences */
            DEBUGLOG(6, "sending selected sequences into seqStore");
            {   U32 storePos;
                for (storePos=storeStart; storePos <= storeEnd; storePos++) {
                    U32 const llen = opt[storePos].litlen;
                    U32 const mlen = opt[storePos].mlen;
                    U32 const offBase = opt[storePos].off;
                    U32 const advance = llen + mlen;
                    DEBUGLOG(6, "considering seq starting at %i, llen=%u, mlen=%u",
                                (int)(anchor - istart), (unsigned)llen, (unsigned)mlen);

                    if (mlen==0) {  /* only literals => must be last "sequence", actually starting a new stream of sequences */
                        assert(storePos == storeEnd);   /* must be last sequence */
                        ip = anchor + llen;     /* last "sequence" is a bunch of literals => don't progress anchor */
                        continue;   /* will finish */
                    }

                    assert(anchor + llen <= iend);
                    ZSTD_updateStats(optStatePtr, llen, anchor, offBase, mlen);
                    ZSTD_storeSeq(seqStore, llen, anchor, iend, offBase, mlen);
                    anchor += advance;
                    ip = anchor;
            }   }
            DEBUGLOG(7, "new offset history : %u, %u, %u", rep[0], rep[1], rep[2]);

            /* update all costs */
            ZSTD_setBasePrices(optStatePtr, optLevel);
        }
    }   /* while (ip < ilimit) */

    /* Return the last literals size */
    return (size_t)(iend - anchor);
}
#endif /* build exclusions */

#ifndef ZSTD_EXCLUDE_BTOPT_BLOCK_COMPRESSOR
static size_t ZSTD_compressBlock_opt0(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        const void* src, size_t srcSize, const ZSTD_dictMode_e dictMode)
{
    return ZSTD_compressBlock_opt_generic(ms, seqStore, rep, src, srcSize, 0 /* optLevel */, dictMode);
}
#endif

#ifndef ZSTD_EXCLUDE_BTULTRA_BLOCK_COMPRESSOR
static size_t ZSTD_compressBlock_opt2(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        const void* src, size_t srcSize, const ZSTD_dictMode_e dictMode)
{
    return ZSTD_compressBlock_opt_generic(ms, seqStore, rep, src, srcSize, 2 /* optLevel */, dictMode);
}
#endif

#ifndef ZSTD_EXCLUDE_BTOPT_BLOCK_COMPRESSOR
size_t ZSTD_compressBlock_btopt(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        const void* src, size_t srcSize)
{
    DEBUGLOG(5, "ZSTD_compressBlock_btopt");
    return ZSTD_compressBlock_opt0(ms, seqStore, rep, src, srcSize, ZSTD_noDict);
}
#endif




#ifndef ZSTD_EXCLUDE_BTULTRA_BLOCK_COMPRESSOR
/* ZSTD_initStats_ultra():
 * make a first compression pass, just to seed stats with more accurate starting values.
 * only works on first block, with no dictionary and no ldm.
 * this function cannot error out, its narrow contract must be respected.
 */
static
ZSTD_ALLOW_POINTER_OVERFLOW_ATTR
void ZSTD_initStats_ultra(ZSTD_MatchState_t* ms,
                          SeqStore_t* seqStore,
                          U32 rep[ZSTD_REP_NUM],
                    const void* src, size_t srcSize)
{
    U32 tmpRep[ZSTD_REP_NUM];  /* updated rep codes will sink here */
    ZSTD_memcpy(tmpRep, rep, sizeof(tmpRep));

    DEBUGLOG(4, "ZSTD_initStats_ultra (srcSize=%zu)", srcSize);
    assert(ms->opt.litLengthSum == 0);    /* first block */
    assert(seqStore->sequences == seqStore->sequencesStart);   /* no ldm */
    assert(ms->window.dictLimit == ms->window.lowLimit);   /* no dictionary */
    assert(ms->window.dictLimit - ms->nextToUpdate <= 1);  /* no prefix (note: intentional overflow, defined as 2-complement) */

    ZSTD_compressBlock_opt2(ms, seqStore, tmpRep, src, srcSize, ZSTD_noDict);   /* generate stats into ms->opt*/

    /* invalidate first scan from history, only keep entropy stats */
    ZSTD_resetSeqStore(seqStore);
    ms->window.base -= srcSize;
    ms->window.dictLimit += (U32)srcSize;
    ms->window.lowLimit = ms->window.dictLimit;
    ms->nextToUpdate = ms->window.dictLimit;

}

size_t ZSTD_compressBlock_btultra(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        const void* src, size_t srcSize)
{
    DEBUGLOG(5, "ZSTD_compressBlock_btultra (srcSize=%zu)", srcSize);
    return ZSTD_compressBlock_opt2(ms, seqStore, rep, src, srcSize, ZSTD_noDict);
}

size_t ZSTD_compressBlock_btultra2(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        const void* src, size_t srcSize)
{
    U32 const curr = (U32)((const BYTE*)src - ms->window.base);
    DEBUGLOG(5, "ZSTD_compressBlock_btultra2 (srcSize=%zu)", srcSize);

    /* 2-passes strategy:
     * this strategy makes a first pass over first block to collect statistics
     * in order to seed next round's statistics with it.
     * After 1st pass, function forgets history, and starts a new block.
     * Consequently, this can only work if no data has been previously loaded in tables,
     * aka, no dictionary, no prefix, no ldm preprocessing.
     * The compression ratio gain is generally small (~0.5% on first block),
     * the cost is 2x cpu time on first block. */
    assert(srcSize <= ZSTD_BLOCKSIZE_MAX);
    if ( (ms->opt.litLengthSum==0)   /* first block */
      && (seqStore->sequences == seqStore->sequencesStart)  /* no ldm */
      && (ms->window.dictLimit == ms->window.lowLimit)   /* no dictionary */
      && (curr == ms->window.dictLimit)    /* start of frame, nothing already loaded nor skipped */
      && (srcSize > ZSTD_PREDEF_THRESHOLD) /* input large enough to not employ default stats */
      ) {
        ZSTD_initStats_ultra(ms, seqStore, rep, src, srcSize);
    }

    return ZSTD_compressBlock_opt2(ms, seqStore, rep, src, srcSize, ZSTD_noDict);
}
#endif

#ifndef ZSTD_EXCLUDE_BTOPT_BLOCK_COMPRESSOR
size_t ZSTD_compressBlock_btopt_dictMatchState(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        const void* src, size_t srcSize)
{
    return ZSTD_compressBlock_opt0(ms, seqStore, rep, src, srcSize, ZSTD_dictMatchState);
}

size_t ZSTD_compressBlock_btopt_extDict(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        const void* src, size_t srcSize)
{
    return ZSTD_compressBlock_opt0(ms, seqStore, rep, src, srcSize, ZSTD_extDict);
}
#endif

#ifndef ZSTD_EXCLUDE_BTULTRA_BLOCK_COMPRESSOR
size_t ZSTD_compressBlock_btultra_dictMatchState(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        const void* src, size_t srcSize)
{
    return ZSTD_compressBlock_opt2(ms, seqStore, rep, src, srcSize, ZSTD_dictMatchState);
}

size_t ZSTD_compressBlock_btultra_extDict(
        ZSTD_MatchState_t* ms, SeqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        const void* src, size_t srcSize)
{
    return ZSTD_compressBlock_opt2(ms, seqStore, rep, src, srcSize, ZSTD_extDict);
}
#endif

/* note : no btultra2 variant for extDict nor dictMatchState,
 * because btultra2 is not meant to work with dictionaries
 * and is only specific for the first block (no prefix) */
