/*
 * Copyright (c) Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include "zstd_compress_internal.h"
#include "zstd_lazy.h"


/*-*************************************
*  Binary Tree search
***************************************/

static void
ZSTD_updateDUBT(ZSTD_matchState_t* ms,
                const BYTE* ip, const BYTE* iend,
                U32 mls)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    U32* const hashTable = ms->hashTable;
    U32  const hashLog = cParams->hashLog;

    U32* const bt = ms->chainTable;
    U32  const btLog  = cParams->chainLog - 1;
    U32  const btMask = (1 << btLog) - 1;

    const BYTE* const base = ms->window.base;
    U32 const target = (U32)(ip - base);
    U32 idx = ms->nextToUpdate;

    if (idx != target)
        DEBUGLOG(7, "ZSTD_updateDUBT, from %u to %u (dictLimit:%u)",
                    idx, target, ms->window.dictLimit);
    assert(ip + 8 <= iend);   /* condition for ZSTD_hashPtr */
    (void)iend;

    assert(idx >= ms->window.dictLimit);   /* condition for valid base+idx */
    for ( ; idx < target ; idx++) {
        size_t const h  = ZSTD_hashPtr(base + idx, hashLog, mls);   /* assumption : ip + 8 <= iend */
        U32    const matchIndex = hashTable[h];

        U32*   const nextCandidatePtr = bt + 2*(idx&btMask);
        U32*   const sortMarkPtr  = nextCandidatePtr + 1;

        DEBUGLOG(8, "ZSTD_updateDUBT: insert %u", idx);
        hashTable[h] = idx;   /* Update Hash Table */
        *nextCandidatePtr = matchIndex;   /* update BT like a chain */
        *sortMarkPtr = ZSTD_DUBT_UNSORTED_MARK;
    }
    ms->nextToUpdate = target;
}


/* ZSTD_insertDUBT1() :
 *  sort one already inserted but unsorted position
 *  assumption : curr >= btlow == (curr - btmask)
 *  doesn't fail */
static void
ZSTD_insertDUBT1(const ZSTD_matchState_t* ms,
                 U32 curr, const BYTE* inputEnd,
                 U32 nbCompares, U32 btLow,
                 const ZSTD_dictMode_e dictMode)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    U32* const bt = ms->chainTable;
    U32  const btLog  = cParams->chainLog - 1;
    U32  const btMask = (1 << btLog) - 1;
    size_t commonLengthSmaller=0, commonLengthLarger=0;
    const BYTE* const base = ms->window.base;
    const BYTE* const dictBase = ms->window.dictBase;
    const U32 dictLimit = ms->window.dictLimit;
    const BYTE* const ip = (curr>=dictLimit) ? base + curr : dictBase + curr;
    const BYTE* const iend = (curr>=dictLimit) ? inputEnd : dictBase + dictLimit;
    const BYTE* const dictEnd = dictBase + dictLimit;
    const BYTE* const prefixStart = base + dictLimit;
    const BYTE* match;
    U32* smallerPtr = bt + 2*(curr&btMask);
    U32* largerPtr  = smallerPtr + 1;
    U32 matchIndex = *smallerPtr;   /* this candidate is unsorted : next sorted candidate is reached through *smallerPtr, while *largerPtr contains previous unsorted candidate (which is already saved and can be overwritten) */
    U32 dummy32;   /* to be nullified at the end */
    U32 const windowValid = ms->window.lowLimit;
    U32 const maxDistance = 1U << cParams->windowLog;
    U32 const windowLow = (curr - windowValid > maxDistance) ? curr - maxDistance : windowValid;


    DEBUGLOG(8, "ZSTD_insertDUBT1(%u) (dictLimit=%u, lowLimit=%u)",
                curr, dictLimit, windowLow);
    assert(curr >= btLow);
    assert(ip < iend);   /* condition for ZSTD_count */

    for (; nbCompares && (matchIndex > windowLow); --nbCompares) {
        U32* const nextPtr = bt + 2*(matchIndex & btMask);
        size_t matchLength = MIN(commonLengthSmaller, commonLengthLarger);   /* guaranteed minimum nb of common bytes */
        assert(matchIndex < curr);
        /* note : all candidates are now supposed sorted,
         * but it's still possible to have nextPtr[1] == ZSTD_DUBT_UNSORTED_MARK
         * when a real index has the same value as ZSTD_DUBT_UNSORTED_MARK */

        if ( (dictMode != ZSTD_extDict)
          || (matchIndex+matchLength >= dictLimit)  /* both in current segment*/
          || (curr < dictLimit) /* both in extDict */) {
            const BYTE* const mBase = ( (dictMode != ZSTD_extDict)
                                     || (matchIndex+matchLength >= dictLimit)) ?
                                        base : dictBase;
            assert( (matchIndex+matchLength >= dictLimit)   /* might be wrong if extDict is incorrectly set to 0 */
                 || (curr < dictLimit) );
            match = mBase + matchIndex;
            matchLength += ZSTD_count(ip+matchLength, match+matchLength, iend);
        } else {
            match = dictBase + matchIndex;
            matchLength += ZSTD_count_2segments(ip+matchLength, match+matchLength, iend, dictEnd, prefixStart);
            if (matchIndex+matchLength >= dictLimit)
                match = base + matchIndex;   /* preparation for next read of match[matchLength] */
        }

        DEBUGLOG(8, "ZSTD_insertDUBT1: comparing %u with %u : found %u common bytes ",
                    curr, matchIndex, (U32)matchLength);

        if (ip+matchLength == iend) {   /* equal : no way to know if inf or sup */
            break;   /* drop , to guarantee consistency ; miss a bit of compression, but other solutions can corrupt tree */
        }

        if (match[matchLength] < ip[matchLength]) {  /* necessarily within buffer */
            /* match is smaller than current */
            *smallerPtr = matchIndex;             /* update smaller idx */
            commonLengthSmaller = matchLength;    /* all smaller will now have at least this guaranteed common length */
            if (matchIndex <= btLow) { smallerPtr=&dummy32; break; }   /* beyond tree size, stop searching */
            DEBUGLOG(8, "ZSTD_insertDUBT1: %u (>btLow=%u) is smaller : next => %u",
                        matchIndex, btLow, nextPtr[1]);
            smallerPtr = nextPtr+1;               /* new "candidate" => larger than match, which was smaller than target */
            matchIndex = nextPtr[1];              /* new matchIndex, larger than previous and closer to current */
        } else {
            /* match is larger than current */
            *largerPtr = matchIndex;
            commonLengthLarger = matchLength;
            if (matchIndex <= btLow) { largerPtr=&dummy32; break; }   /* beyond tree size, stop searching */
            DEBUGLOG(8, "ZSTD_insertDUBT1: %u (>btLow=%u) is larger => %u",
                        matchIndex, btLow, nextPtr[0]);
            largerPtr = nextPtr;
            matchIndex = nextPtr[0];
    }   }

    *smallerPtr = *largerPtr = 0;
}


static size_t
ZSTD_DUBT_findBetterDictMatch (
        const ZSTD_matchState_t* ms,
        const BYTE* const ip, const BYTE* const iend,
        size_t* offsetPtr,
        size_t bestLength,
        U32 nbCompares,
        U32 const mls,
        const ZSTD_dictMode_e dictMode)
{
    const ZSTD_matchState_t * const dms = ms->dictMatchState;
    const ZSTD_compressionParameters* const dmsCParams = &dms->cParams;
    const U32 * const dictHashTable = dms->hashTable;
    U32         const hashLog = dmsCParams->hashLog;
    size_t      const h  = ZSTD_hashPtr(ip, hashLog, mls);
    U32               dictMatchIndex = dictHashTable[h];

    const BYTE* const base = ms->window.base;
    const BYTE* const prefixStart = base + ms->window.dictLimit;
    U32         const curr = (U32)(ip-base);
    const BYTE* const dictBase = dms->window.base;
    const BYTE* const dictEnd = dms->window.nextSrc;
    U32         const dictHighLimit = (U32)(dms->window.nextSrc - dms->window.base);
    U32         const dictLowLimit = dms->window.lowLimit;
    U32         const dictIndexDelta = ms->window.lowLimit - dictHighLimit;

    U32*        const dictBt = dms->chainTable;
    U32         const btLog  = dmsCParams->chainLog - 1;
    U32         const btMask = (1 << btLog) - 1;
    U32         const btLow = (btMask >= dictHighLimit - dictLowLimit) ? dictLowLimit : dictHighLimit - btMask;

    size_t commonLengthSmaller=0, commonLengthLarger=0;

    (void)dictMode;
    assert(dictMode == ZSTD_dictMatchState);

    for (; nbCompares && (dictMatchIndex > dictLowLimit); --nbCompares) {
        U32* const nextPtr = dictBt + 2*(dictMatchIndex & btMask);
        size_t matchLength = MIN(commonLengthSmaller, commonLengthLarger);   /* guaranteed minimum nb of common bytes */
        const BYTE* match = dictBase + dictMatchIndex;
        matchLength += ZSTD_count_2segments(ip+matchLength, match+matchLength, iend, dictEnd, prefixStart);
        if (dictMatchIndex+matchLength >= dictHighLimit)
            match = base + dictMatchIndex + dictIndexDelta;   /* to prepare for next usage of match[matchLength] */

        if (matchLength > bestLength) {
            U32 matchIndex = dictMatchIndex + dictIndexDelta;
            if ( (4*(int)(matchLength-bestLength)) > (int)(ZSTD_highbit32(curr-matchIndex+1) - ZSTD_highbit32((U32)offsetPtr[0]+1)) ) {
                DEBUGLOG(9, "ZSTD_DUBT_findBetterDictMatch(%u) : found better match length %u -> %u and offsetCode %u -> %u (dictMatchIndex %u, matchIndex %u)",
                    curr, (U32)bestLength, (U32)matchLength, (U32)*offsetPtr, STORE_OFFSET(curr - matchIndex), dictMatchIndex, matchIndex);
                bestLength = matchLength, *offsetPtr = STORE_OFFSET(curr - matchIndex);
            }
            if (ip+matchLength == iend) {   /* reached end of input : ip[matchLength] is not valid, no way to know if it's larger or smaller than match */
                break;   /* drop, to guarantee consistency (miss a little bit of compression) */
            }
        }

        if (match[matchLength] < ip[matchLength]) {
            if (dictMatchIndex <= btLow) { break; }   /* beyond tree size, stop the search */
            commonLengthSmaller = matchLength;    /* all smaller will now have at least this guaranteed common length */
            dictMatchIndex = nextPtr[1];              /* new matchIndex larger than previous (closer to current) */
        } else {
            /* match is larger than current */
            if (dictMatchIndex <= btLow) { break; }   /* beyond tree size, stop the search */
            commonLengthLarger = matchLength;
            dictMatchIndex = nextPtr[0];
        }
    }

    if (bestLength >= MINMATCH) {
        U32 const mIndex = curr - (U32)STORED_OFFSET(*offsetPtr); (void)mIndex;
        DEBUGLOG(8, "ZSTD_DUBT_findBetterDictMatch(%u) : found match of length %u and offsetCode %u (pos %u)",
                    curr, (U32)bestLength, (U32)*offsetPtr, mIndex);
    }
    return bestLength;

}


static size_t
ZSTD_DUBT_findBestMatch(ZSTD_matchState_t* ms,
                        const BYTE* const ip, const BYTE* const iend,
                        size_t* offsetPtr,
                        U32 const mls,
                        const ZSTD_dictMode_e dictMode)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    U32*   const hashTable = ms->hashTable;
    U32    const hashLog = cParams->hashLog;
    size_t const h  = ZSTD_hashPtr(ip, hashLog, mls);
    U32          matchIndex  = hashTable[h];

    const BYTE* const base = ms->window.base;
    U32    const curr = (U32)(ip-base);
    U32    const windowLow = ZSTD_getLowestMatchIndex(ms, curr, cParams->windowLog);

    U32*   const bt = ms->chainTable;
    U32    const btLog  = cParams->chainLog - 1;
    U32    const btMask = (1 << btLog) - 1;
    U32    const btLow = (btMask >= curr) ? 0 : curr - btMask;
    U32    const unsortLimit = MAX(btLow, windowLow);

    U32*         nextCandidate = bt + 2*(matchIndex&btMask);
    U32*         unsortedMark = bt + 2*(matchIndex&btMask) + 1;
    U32          nbCompares = 1U << cParams->searchLog;
    U32          nbCandidates = nbCompares;
    U32          previousCandidate = 0;

    DEBUGLOG(7, "ZSTD_DUBT_findBestMatch (%u) ", curr);
    assert(ip <= iend-8);   /* required for h calculation */
    assert(dictMode != ZSTD_dedicatedDictSearch);

    /* reach end of unsorted candidates list */
    while ( (matchIndex > unsortLimit)
         && (*unsortedMark == ZSTD_DUBT_UNSORTED_MARK)
         && (nbCandidates > 1) ) {
        DEBUGLOG(8, "ZSTD_DUBT_findBestMatch: candidate %u is unsorted",
                    matchIndex);
        *unsortedMark = previousCandidate;  /* the unsortedMark becomes a reversed chain, to move up back to original position */
        previousCandidate = matchIndex;
        matchIndex = *nextCandidate;
        nextCandidate = bt + 2*(matchIndex&btMask);
        unsortedMark = bt + 2*(matchIndex&btMask) + 1;
        nbCandidates --;
    }

    /* nullify last candidate if it's still unsorted
     * simplification, detrimental to compression ratio, beneficial for speed */
    if ( (matchIndex > unsortLimit)
      && (*unsortedMark==ZSTD_DUBT_UNSORTED_MARK) ) {
        DEBUGLOG(7, "ZSTD_DUBT_findBestMatch: nullify last unsorted candidate %u",
                    matchIndex);
        *nextCandidate = *unsortedMark = 0;
    }

    /* batch sort stacked candidates */
    matchIndex = previousCandidate;
    while (matchIndex) {  /* will end on matchIndex == 0 */
        U32* const nextCandidateIdxPtr = bt + 2*(matchIndex&btMask) + 1;
        U32 const nextCandidateIdx = *nextCandidateIdxPtr;
        ZSTD_insertDUBT1(ms, matchIndex, iend,
                         nbCandidates, unsortLimit, dictMode);
        matchIndex = nextCandidateIdx;
        nbCandidates++;
    }

    /* find longest match */
    {   size_t commonLengthSmaller = 0, commonLengthLarger = 0;
        const BYTE* const dictBase = ms->window.dictBase;
        const U32 dictLimit = ms->window.dictLimit;
        const BYTE* const dictEnd = dictBase + dictLimit;
        const BYTE* const prefixStart = base + dictLimit;
        U32* smallerPtr = bt + 2*(curr&btMask);
        U32* largerPtr  = bt + 2*(curr&btMask) + 1;
        U32 matchEndIdx = curr + 8 + 1;
        U32 dummy32;   /* to be nullified at the end */
        size_t bestLength = 0;

        matchIndex  = hashTable[h];
        hashTable[h] = curr;   /* Update Hash Table */

        for (; nbCompares && (matchIndex > windowLow); --nbCompares) {
            U32* const nextPtr = bt + 2*(matchIndex & btMask);
            size_t matchLength = MIN(commonLengthSmaller, commonLengthLarger);   /* guaranteed minimum nb of common bytes */
            const BYTE* match;

            if ((dictMode != ZSTD_extDict) || (matchIndex+matchLength >= dictLimit)) {
                match = base + matchIndex;
                matchLength += ZSTD_count(ip+matchLength, match+matchLength, iend);
            } else {
                match = dictBase + matchIndex;
                matchLength += ZSTD_count_2segments(ip+matchLength, match+matchLength, iend, dictEnd, prefixStart);
                if (matchIndex+matchLength >= dictLimit)
                    match = base + matchIndex;   /* to prepare for next usage of match[matchLength] */
            }

            if (matchLength > bestLength) {
                if (matchLength > matchEndIdx - matchIndex)
                    matchEndIdx = matchIndex + (U32)matchLength;
                if ( (4*(int)(matchLength-bestLength)) > (int)(ZSTD_highbit32(curr-matchIndex+1) - ZSTD_highbit32((U32)offsetPtr[0]+1)) )
                    bestLength = matchLength, *offsetPtr = STORE_OFFSET(curr - matchIndex);
                if (ip+matchLength == iend) {   /* equal : no way to know if inf or sup */
                    if (dictMode == ZSTD_dictMatchState) {
                        nbCompares = 0; /* in addition to avoiding checking any
                                         * further in this loop, make sure we
                                         * skip checking in the dictionary. */
                    }
                    break;   /* drop, to guarantee consistency (miss a little bit of compression) */
                }
            }

            if (match[matchLength] < ip[matchLength]) {
                /* match is smaller than current */
                *smallerPtr = matchIndex;             /* update smaller idx */
                commonLengthSmaller = matchLength;    /* all smaller will now have at least this guaranteed common length */
                if (matchIndex <= btLow) { smallerPtr=&dummy32; break; }   /* beyond tree size, stop the search */
                smallerPtr = nextPtr+1;               /* new "smaller" => larger of match */
                matchIndex = nextPtr[1];              /* new matchIndex larger than previous (closer to current) */
            } else {
                /* match is larger than current */
                *largerPtr = matchIndex;
                commonLengthLarger = matchLength;
                if (matchIndex <= btLow) { largerPtr=&dummy32; break; }   /* beyond tree size, stop the search */
                largerPtr = nextPtr;
                matchIndex = nextPtr[0];
        }   }

        *smallerPtr = *largerPtr = 0;

        assert(nbCompares <= (1U << ZSTD_SEARCHLOG_MAX)); /* Check we haven't underflowed. */
        if (dictMode == ZSTD_dictMatchState && nbCompares) {
            bestLength = ZSTD_DUBT_findBetterDictMatch(
                    ms, ip, iend,
                    offsetPtr, bestLength, nbCompares,
                    mls, dictMode);
        }

        assert(matchEndIdx > curr+8); /* ensure nextToUpdate is increased */
        ms->nextToUpdate = matchEndIdx - 8;   /* skip repetitive patterns */
        if (bestLength >= MINMATCH) {
            U32 const mIndex = curr - (U32)STORED_OFFSET(*offsetPtr); (void)mIndex;
            DEBUGLOG(8, "ZSTD_DUBT_findBestMatch(%u) : found match of length %u and offsetCode %u (pos %u)",
                        curr, (U32)bestLength, (U32)*offsetPtr, mIndex);
        }
        return bestLength;
    }
}


/* ZSTD_BtFindBestMatch() : Tree updater, providing best match */
FORCE_INLINE_TEMPLATE size_t
ZSTD_BtFindBestMatch( ZSTD_matchState_t* ms,
                const BYTE* const ip, const BYTE* const iLimit,
                      size_t* offsetPtr,
                const U32 mls /* template */,
                const ZSTD_dictMode_e dictMode)
{
    DEBUGLOG(7, "ZSTD_BtFindBestMatch");
    if (ip < ms->window.base + ms->nextToUpdate) return 0;   /* skipped area */
    ZSTD_updateDUBT(ms, ip, iLimit, mls);
    return ZSTD_DUBT_findBestMatch(ms, ip, iLimit, offsetPtr, mls, dictMode);
}

/* *********************************
* Dedicated dict search
***********************************/

void ZSTD_dedicatedDictSearch_lazy_loadDictionary(ZSTD_matchState_t* ms, const BYTE* const ip)
{
    const BYTE* const base = ms->window.base;
    U32 const target = (U32)(ip - base);
    U32* const hashTable = ms->hashTable;
    U32* const chainTable = ms->chainTable;
    U32 const chainSize = 1 << ms->cParams.chainLog;
    U32 idx = ms->nextToUpdate;
    U32 const minChain = chainSize < target - idx ? target - chainSize : idx;
    U32 const bucketSize = 1 << ZSTD_LAZY_DDSS_BUCKET_LOG;
    U32 const cacheSize = bucketSize - 1;
    U32 const chainAttempts = (1 << ms->cParams.searchLog) - cacheSize;
    U32 const chainLimit = chainAttempts > 255 ? 255 : chainAttempts;

    /* We know the hashtable is oversized by a factor of `bucketSize`.
     * We are going to temporarily pretend `bucketSize == 1`, keeping only a
     * single entry. We will use the rest of the space to construct a temporary
     * chaintable.
     */
    U32 const hashLog = ms->cParams.hashLog - ZSTD_LAZY_DDSS_BUCKET_LOG;
    U32* const tmpHashTable = hashTable;
    U32* const tmpChainTable = hashTable + ((size_t)1 << hashLog);
    U32 const tmpChainSize = (U32)((1 << ZSTD_LAZY_DDSS_BUCKET_LOG) - 1) << hashLog;
    U32 const tmpMinChain = tmpChainSize < target ? target - tmpChainSize : idx;
    U32 hashIdx;

    assert(ms->cParams.chainLog <= 24);
    assert(ms->cParams.hashLog > ms->cParams.chainLog);
    assert(idx != 0);
    assert(tmpMinChain <= minChain);

    /* fill conventional hash table and conventional chain table */
    for ( ; idx < target; idx++) {
        U32 const h = (U32)ZSTD_hashPtr(base + idx, hashLog, ms->cParams.minMatch);
        if (idx >= tmpMinChain) {
            tmpChainTable[idx - tmpMinChain] = hashTable[h];
        }
        tmpHashTable[h] = idx;
    }

    /* sort chains into ddss chain table */
    {
        U32 chainPos = 0;
        for (hashIdx = 0; hashIdx < (1U << hashLog); hashIdx++) {
            U32 count;
            U32 countBeyondMinChain = 0;
            U32 i = tmpHashTable[hashIdx];
            for (count = 0; i >= tmpMinChain && count < cacheSize; count++) {
                /* skip through the chain to the first position that won't be
                 * in the hash cache bucket */
                if (i < minChain) {
                    countBeyondMinChain++;
                }
                i = tmpChainTable[i - tmpMinChain];
            }
            if (count == cacheSize) {
                for (count = 0; count < chainLimit;) {
                    if (i < minChain) {
                        if (!i || ++countBeyondMinChain > cacheSize) {
                            /* only allow pulling `cacheSize` number of entries
                             * into the cache or chainTable beyond `minChain`,
                             * to replace the entries pulled out of the
                             * chainTable into the cache. This lets us reach
                             * back further without increasing the total number
                             * of entries in the chainTable, guaranteeing the
                             * DDSS chain table will fit into the space
                             * allocated for the regular one. */
                            break;
                        }
                    }
                    chainTable[chainPos++] = i;
                    count++;
                    if (i < tmpMinChain) {
                        break;
                    }
                    i = tmpChainTable[i - tmpMinChain];
                }
            } else {
                count = 0;
            }
            if (count) {
                tmpHashTable[hashIdx] = ((chainPos - count) << 8) + count;
            } else {
                tmpHashTable[hashIdx] = 0;
            }
        }
        assert(chainPos <= chainSize); /* I believe this is guaranteed... */
    }

    /* move chain pointers into the last entry of each hash bucket */
    for (hashIdx = (1 << hashLog); hashIdx; ) {
        U32 const bucketIdx = --hashIdx << ZSTD_LAZY_DDSS_BUCKET_LOG;
        U32 const chainPackedPointer = tmpHashTable[hashIdx];
        U32 i;
        for (i = 0; i < cacheSize; i++) {
            hashTable[bucketIdx + i] = 0;
        }
        hashTable[bucketIdx + bucketSize - 1] = chainPackedPointer;
    }

    /* fill the buckets of the hash table */
    for (idx = ms->nextToUpdate; idx < target; idx++) {
        U32 const h = (U32)ZSTD_hashPtr(base + idx, hashLog, ms->cParams.minMatch)
                   << ZSTD_LAZY_DDSS_BUCKET_LOG;
        U32 i;
        /* Shift hash cache down 1. */
        for (i = cacheSize - 1; i; i--)
            hashTable[h + i] = hashTable[h + i - 1];
        hashTable[h] = idx;
    }

    ms->nextToUpdate = target;
}

/* Returns the longest match length found in the dedicated dict search structure.
 * If none are longer than the argument ml, then ml will be returned.
 */
FORCE_INLINE_TEMPLATE
size_t ZSTD_dedicatedDictSearch_lazy_search(size_t* offsetPtr, size_t ml, U32 nbAttempts,
                                            const ZSTD_matchState_t* const dms,
                                            const BYTE* const ip, const BYTE* const iLimit,
                                            const BYTE* const prefixStart, const U32 curr,
                                            const U32 dictLimit, const size_t ddsIdx) {
    const U32 ddsLowestIndex  = dms->window.dictLimit;
    const BYTE* const ddsBase = dms->window.base;
    const BYTE* const ddsEnd  = dms->window.nextSrc;
    const U32 ddsSize         = (U32)(ddsEnd - ddsBase);
    const U32 ddsIndexDelta   = dictLimit - ddsSize;
    const U32 bucketSize      = (1 << ZSTD_LAZY_DDSS_BUCKET_LOG);
    const U32 bucketLimit     = nbAttempts < bucketSize - 1 ? nbAttempts : bucketSize - 1;
    U32 ddsAttempt;
    U32 matchIndex;

    for (ddsAttempt = 0; ddsAttempt < bucketSize - 1; ddsAttempt++) {
        PREFETCH_L1(ddsBase + dms->hashTable[ddsIdx + ddsAttempt]);
    }

    {
        U32 const chainPackedPointer = dms->hashTable[ddsIdx + bucketSize - 1];
        U32 const chainIndex = chainPackedPointer >> 8;

        PREFETCH_L1(&dms->chainTable[chainIndex]);
    }

    for (ddsAttempt = 0; ddsAttempt < bucketLimit; ddsAttempt++) {
        size_t currentMl=0;
        const BYTE* match;
        matchIndex = dms->hashTable[ddsIdx + ddsAttempt];
        match = ddsBase + matchIndex;

        if (!matchIndex) {
            return ml;
        }

        /* guaranteed by table construction */
        (void)ddsLowestIndex;
        assert(matchIndex >= ddsLowestIndex);
        assert(match+4 <= ddsEnd);
        if (MEM_read32(match) == MEM_read32(ip)) {
            /* assumption : matchIndex <= dictLimit-4 (by table construction) */
            currentMl = ZSTD_count_2segments(ip+4, match+4, iLimit, ddsEnd, prefixStart) + 4;
        }

        /* save best solution */
        if (currentMl > ml) {
            ml = currentMl;
            *offsetPtr = STORE_OFFSET(curr - (matchIndex + ddsIndexDelta));
            if (ip+currentMl == iLimit) {
                /* best possible, avoids read overflow on next attempt */
                return ml;
            }
        }
    }

    {
        U32 const chainPackedPointer = dms->hashTable[ddsIdx + bucketSize - 1];
        U32 chainIndex = chainPackedPointer >> 8;
        U32 const chainLength = chainPackedPointer & 0xFF;
        U32 const chainAttempts = nbAttempts - ddsAttempt;
        U32 const chainLimit = chainAttempts > chainLength ? chainLength : chainAttempts;
        U32 chainAttempt;

        for (chainAttempt = 0 ; chainAttempt < chainLimit; chainAttempt++) {
            PREFETCH_L1(ddsBase + dms->chainTable[chainIndex + chainAttempt]);
        }

        for (chainAttempt = 0 ; chainAttempt < chainLimit; chainAttempt++, chainIndex++) {
            size_t currentMl=0;
            const BYTE* match;
            matchIndex = dms->chainTable[chainIndex];
            match = ddsBase + matchIndex;

            /* guaranteed by table construction */
            assert(matchIndex >= ddsLowestIndex);
            assert(match+4 <= ddsEnd);
            if (MEM_read32(match) == MEM_read32(ip)) {
                /* assumption : matchIndex <= dictLimit-4 (by table construction) */
                currentMl = ZSTD_count_2segments(ip+4, match+4, iLimit, ddsEnd, prefixStart) + 4;
            }

            /* save best solution */
            if (currentMl > ml) {
                ml = currentMl;
                *offsetPtr = STORE_OFFSET(curr - (matchIndex + ddsIndexDelta));
                if (ip+currentMl == iLimit) break; /* best possible, avoids read overflow on next attempt */
            }
        }
    }
    return ml;
}


/* *********************************
*  Hash Chain
***********************************/
#define NEXT_IN_CHAIN(d, mask)   chainTable[(d) & (mask)]

/* Update chains up to ip (excluded)
   Assumption : always within prefix (i.e. not within extDict) */
FORCE_INLINE_TEMPLATE U32 ZSTD_insertAndFindFirstIndex_internal(
                        ZSTD_matchState_t* ms,
                        const ZSTD_compressionParameters* const cParams,
                        const BYTE* ip, U32 const mls)
{
    U32* const hashTable  = ms->hashTable;
    const U32 hashLog = cParams->hashLog;
    U32* const chainTable = ms->chainTable;
    const U32 chainMask = (1 << cParams->chainLog) - 1;
    const BYTE* const base = ms->window.base;
    const U32 target = (U32)(ip - base);
    U32 idx = ms->nextToUpdate;

    while(idx < target) { /* catch up */
        size_t const h = ZSTD_hashPtr(base+idx, hashLog, mls);
        NEXT_IN_CHAIN(idx, chainMask) = hashTable[h];
        hashTable[h] = idx;
        idx++;
    }

    ms->nextToUpdate = target;
    return hashTable[ZSTD_hashPtr(ip, hashLog, mls)];
}

U32 ZSTD_insertAndFindFirstIndex(ZSTD_matchState_t* ms, const BYTE* ip) {
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    return ZSTD_insertAndFindFirstIndex_internal(ms, cParams, ip, ms->cParams.minMatch);
}

/* inlining is important to hardwire a hot branch (template emulation) */
FORCE_INLINE_TEMPLATE
size_t ZSTD_HcFindBestMatch(
                        ZSTD_matchState_t* ms,
                        const BYTE* const ip, const BYTE* const iLimit,
                        size_t* offsetPtr,
                        const U32 mls, const ZSTD_dictMode_e dictMode)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    U32* const chainTable = ms->chainTable;
    const U32 chainSize = (1 << cParams->chainLog);
    const U32 chainMask = chainSize-1;
    const BYTE* const base = ms->window.base;
    const BYTE* const dictBase = ms->window.dictBase;
    const U32 dictLimit = ms->window.dictLimit;
    const BYTE* const prefixStart = base + dictLimit;
    const BYTE* const dictEnd = dictBase + dictLimit;
    const U32 curr = (U32)(ip-base);
    const U32 maxDistance = 1U << cParams->windowLog;
    const U32 lowestValid = ms->window.lowLimit;
    const U32 withinMaxDistance = (curr - lowestValid > maxDistance) ? curr - maxDistance : lowestValid;
    const U32 isDictionary = (ms->loadedDictEnd != 0);
    const U32 lowLimit = isDictionary ? lowestValid : withinMaxDistance;
    const U32 minChain = curr > chainSize ? curr - chainSize : 0;
    U32 nbAttempts = 1U << cParams->searchLog;
    size_t ml=4-1;

    const ZSTD_matchState_t* const dms = ms->dictMatchState;
    const U32 ddsHashLog = dictMode == ZSTD_dedicatedDictSearch
                         ? dms->cParams.hashLog - ZSTD_LAZY_DDSS_BUCKET_LOG : 0;
    const size_t ddsIdx = dictMode == ZSTD_dedicatedDictSearch
                        ? ZSTD_hashPtr(ip, ddsHashLog, mls) << ZSTD_LAZY_DDSS_BUCKET_LOG : 0;

    U32 matchIndex;

    if (dictMode == ZSTD_dedicatedDictSearch) {
        const U32* entry = &dms->hashTable[ddsIdx];
        PREFETCH_L1(entry);
    }

    /* HC4 match finder */
    matchIndex = ZSTD_insertAndFindFirstIndex_internal(ms, cParams, ip, mls);

    for ( ; (matchIndex>=lowLimit) & (nbAttempts>0) ; nbAttempts--) {
        size_t currentMl=0;
        if ((dictMode != ZSTD_extDict) || matchIndex >= dictLimit) {
            const BYTE* const match = base + matchIndex;
            assert(matchIndex >= dictLimit);   /* ensures this is true if dictMode != ZSTD_extDict */
            if (match[ml] == ip[ml])   /* potentially better */
                currentMl = ZSTD_count(ip, match, iLimit);
        } else {
            const BYTE* const match = dictBase + matchIndex;
            assert(match+4 <= dictEnd);
            if (MEM_read32(match) == MEM_read32(ip))   /* assumption : matchIndex <= dictLimit-4 (by table construction) */
                currentMl = ZSTD_count_2segments(ip+4, match+4, iLimit, dictEnd, prefixStart) + 4;
        }

        /* save best solution */
        if (currentMl > ml) {
            ml = currentMl;
            *offsetPtr = STORE_OFFSET(curr - matchIndex);
            if (ip+currentMl == iLimit) break; /* best possible, avoids read overflow on next attempt */
        }

        if (matchIndex <= minChain) break;
        matchIndex = NEXT_IN_CHAIN(matchIndex, chainMask);
    }

    assert(nbAttempts <= (1U << ZSTD_SEARCHLOG_MAX)); /* Check we haven't underflowed. */
    if (dictMode == ZSTD_dedicatedDictSearch) {
        ml = ZSTD_dedicatedDictSearch_lazy_search(offsetPtr, ml, nbAttempts, dms,
                                                  ip, iLimit, prefixStart, curr, dictLimit, ddsIdx);
    } else if (dictMode == ZSTD_dictMatchState) {
        const U32* const dmsChainTable = dms->chainTable;
        const U32 dmsChainSize         = (1 << dms->cParams.chainLog);
        const U32 dmsChainMask         = dmsChainSize - 1;
        const U32 dmsLowestIndex       = dms->window.dictLimit;
        const BYTE* const dmsBase      = dms->window.base;
        const BYTE* const dmsEnd       = dms->window.nextSrc;
        const U32 dmsSize              = (U32)(dmsEnd - dmsBase);
        const U32 dmsIndexDelta        = dictLimit - dmsSize;
        const U32 dmsMinChain = dmsSize > dmsChainSize ? dmsSize - dmsChainSize : 0;

        matchIndex = dms->hashTable[ZSTD_hashPtr(ip, dms->cParams.hashLog, mls)];

        for ( ; (matchIndex>=dmsLowestIndex) & (nbAttempts>0) ; nbAttempts--) {
            size_t currentMl=0;
            const BYTE* const match = dmsBase + matchIndex;
            assert(match+4 <= dmsEnd);
            if (MEM_read32(match) == MEM_read32(ip))   /* assumption : matchIndex <= dictLimit-4 (by table construction) */
                currentMl = ZSTD_count_2segments(ip+4, match+4, iLimit, dmsEnd, prefixStart) + 4;

            /* save best solution */
            if (currentMl > ml) {
                ml = currentMl;
                assert(curr > matchIndex + dmsIndexDelta);
                *offsetPtr = STORE_OFFSET(curr - (matchIndex + dmsIndexDelta));
                if (ip+currentMl == iLimit) break; /* best possible, avoids read overflow on next attempt */
            }

            if (matchIndex <= dmsMinChain) break;

            matchIndex = dmsChainTable[matchIndex & dmsChainMask];
        }
    }

    return ml;
}

/* *********************************
* (SIMD) Row-based matchfinder
***********************************/
/* Constants for row-based hash */
#define ZSTD_ROW_HASH_TAG_OFFSET 16     /* byte offset of hashes in the match state's tagTable from the beginning of a row */
#define ZSTD_ROW_HASH_TAG_BITS 8        /* nb bits to use for the tag */
#define ZSTD_ROW_HASH_TAG_MASK ((1u << ZSTD_ROW_HASH_TAG_BITS) - 1)
#define ZSTD_ROW_HASH_MAX_ENTRIES 64    /* absolute maximum number of entries per row, for all configurations */

#define ZSTD_ROW_HASH_CACHE_MASK (ZSTD_ROW_HASH_CACHE_SIZE - 1)

typedef U64 ZSTD_VecMask;   /* Clarifies when we are interacting with a U64 representing a mask of matches */

/* ZSTD_VecMask_next():
 * Starting from the LSB, returns the idx of the next non-zero bit.
 * Basically counting the nb of trailing zeroes.
 */
static U32 ZSTD_VecMask_next(ZSTD_VecMask val) {
    assert(val != 0);
#   if (defined(__GNUC__) && ((__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 4))))
    if (sizeof(size_t) == 4) {
        U32 mostSignificantWord = (U32)(val >> 32);
        U32 leastSignificantWord = (U32)val;
        if (leastSignificantWord == 0) {
            return 32 + (U32)__builtin_ctz(mostSignificantWord);
        } else {
            return (U32)__builtin_ctz(leastSignificantWord);
        }
    } else {
        return (U32)__builtin_ctzll(val);
    }
#   else
    /* Software ctz version: http://aggregate.org/MAGIC/#Trailing%20Zero%20Count
     * and: https://stackoverflow.com/questions/2709430/count-number-of-bits-in-a-64-bit-long-big-integer
     */
    val = ~val & (val - 1ULL); /* Lowest set bit mask */
    val = val - ((val >> 1) & 0x5555555555555555);
    val = (val & 0x3333333333333333ULL) + ((val >> 2) & 0x3333333333333333ULL);
    return (U32)((((val + (val >> 4)) & 0xF0F0F0F0F0F0F0FULL) * 0x101010101010101ULL) >> 56);
#   endif
}

/* ZSTD_rotateRight_*():
 * Rotates a bitfield to the right by "count" bits.
 * https://en.wikipedia.org/w/index.php?title=Circular_shift&oldid=991635599#Implementing_circular_shifts
 */
FORCE_INLINE_TEMPLATE
U64 ZSTD_rotateRight_U64(U64 const value, U32 count) {
    assert(count < 64);
    count &= 0x3F; /* for fickle pattern recognition */
    return (value >> count) | (U64)(value << ((0U - count) & 0x3F));
}

FORCE_INLINE_TEMPLATE
U32 ZSTD_rotateRight_U32(U32 const value, U32 count) {
    assert(count < 32);
    count &= 0x1F; /* for fickle pattern recognition */
    return (value >> count) | (U32)(value << ((0U - count) & 0x1F));
}

FORCE_INLINE_TEMPLATE
U16 ZSTD_rotateRight_U16(U16 const value, U32 count) {
    assert(count < 16);
    count &= 0x0F; /* for fickle pattern recognition */
    return (value >> count) | (U16)(value << ((0U - count) & 0x0F));
}

/* ZSTD_row_nextIndex():
 * Returns the next index to insert at within a tagTable row, and updates the "head"
 * value to reflect the update. Essentially cycles backwards from [0, {entries per row})
 */
FORCE_INLINE_TEMPLATE U32 ZSTD_row_nextIndex(BYTE* const tagRow, U32 const rowMask) {
  U32 const next = (*tagRow - 1) & rowMask;
  *tagRow = (BYTE)next;
  return next;
}

/* ZSTD_isAligned():
 * Checks that a pointer is aligned to "align" bytes which must be a power of 2.
 */
MEM_STATIC int ZSTD_isAligned(void const* ptr, size_t align) {
    assert((align & (align - 1)) == 0);
    return (((size_t)ptr) & (align - 1)) == 0;
}

/* ZSTD_row_prefetch():
 * Performs prefetching for the hashTable and tagTable at a given row.
 */
FORCE_INLINE_TEMPLATE void ZSTD_row_prefetch(U32 const* hashTable, U16 const* tagTable, U32 const relRow, U32 const rowLog) {
    PREFETCH_L1(hashTable + relRow);
    if (rowLog >= 5) {
        PREFETCH_L1(hashTable + relRow + 16);
        /* Note: prefetching more of the hash table does not appear to be beneficial for 128-entry rows */
    }
    PREFETCH_L1(tagTable + relRow);
    if (rowLog == 6) {
        PREFETCH_L1(tagTable + relRow + 32);
    }
    assert(rowLog == 4 || rowLog == 5 || rowLog == 6);
    assert(ZSTD_isAligned(hashTable + relRow, 64));                 /* prefetched hash row always 64-byte aligned */
    assert(ZSTD_isAligned(tagTable + relRow, (size_t)1 << rowLog)); /* prefetched tagRow sits on correct multiple of bytes (32,64,128) */
}

/* ZSTD_row_fillHashCache():
 * Fill up the hash cache starting at idx, prefetching up to ZSTD_ROW_HASH_CACHE_SIZE entries,
 * but not beyond iLimit.
 */
FORCE_INLINE_TEMPLATE void ZSTD_row_fillHashCache(ZSTD_matchState_t* ms, const BYTE* base,
                                   U32 const rowLog, U32 const mls,
                                   U32 idx, const BYTE* const iLimit)
{
    U32 const* const hashTable = ms->hashTable;
    U16 const* const tagTable = ms->tagTable;
    U32 const hashLog = ms->rowHashLog;
    U32 const maxElemsToPrefetch = (base + idx) > iLimit ? 0 : (U32)(iLimit - (base + idx) + 1);
    U32 const lim = idx + MIN(ZSTD_ROW_HASH_CACHE_SIZE, maxElemsToPrefetch);

    for (; idx < lim; ++idx) {
        U32 const hash = (U32)ZSTD_hashPtr(base + idx, hashLog + ZSTD_ROW_HASH_TAG_BITS, mls);
        U32 const row = (hash >> ZSTD_ROW_HASH_TAG_BITS) << rowLog;
        ZSTD_row_prefetch(hashTable, tagTable, row, rowLog);
        ms->hashCache[idx & ZSTD_ROW_HASH_CACHE_MASK] = hash;
    }

    DEBUGLOG(6, "ZSTD_row_fillHashCache(): [%u %u %u %u %u %u %u %u]", ms->hashCache[0], ms->hashCache[1],
                                                     ms->hashCache[2], ms->hashCache[3], ms->hashCache[4],
                                                     ms->hashCache[5], ms->hashCache[6], ms->hashCache[7]);
}

/* ZSTD_row_nextCachedHash():
 * Returns the hash of base + idx, and replaces the hash in the hash cache with the byte at
 * base + idx + ZSTD_ROW_HASH_CACHE_SIZE. Also prefetches the appropriate rows from hashTable and tagTable.
 */
FORCE_INLINE_TEMPLATE U32 ZSTD_row_nextCachedHash(U32* cache, U32 const* hashTable,
                                                  U16 const* tagTable, BYTE const* base,
                                                  U32 idx, U32 const hashLog,
                                                  U32 const rowLog, U32 const mls)
{
    U32 const newHash = (U32)ZSTD_hashPtr(base+idx+ZSTD_ROW_HASH_CACHE_SIZE, hashLog + ZSTD_ROW_HASH_TAG_BITS, mls);
    U32 const row = (newHash >> ZSTD_ROW_HASH_TAG_BITS) << rowLog;
    ZSTD_row_prefetch(hashTable, tagTable, row, rowLog);
    {   U32 const hash = cache[idx & ZSTD_ROW_HASH_CACHE_MASK];
        cache[idx & ZSTD_ROW_HASH_CACHE_MASK] = newHash;
        return hash;
    }
}

/* ZSTD_row_update_internalImpl():
 * Updates the hash table with positions starting from updateStartIdx until updateEndIdx.
 */
FORCE_INLINE_TEMPLATE void ZSTD_row_update_internalImpl(ZSTD_matchState_t* ms,
                                                        U32 updateStartIdx, U32 const updateEndIdx,
                                                        U32 const mls, U32 const rowLog,
                                                        U32 const rowMask, U32 const useCache)
{
    U32* const hashTable = ms->hashTable;
    U16* const tagTable = ms->tagTable;
    U32 const hashLog = ms->rowHashLog;
    const BYTE* const base = ms->window.base;

    DEBUGLOG(6, "ZSTD_row_update_internalImpl(): updateStartIdx=%u, updateEndIdx=%u", updateStartIdx, updateEndIdx);
    for (; updateStartIdx < updateEndIdx; ++updateStartIdx) {
        U32 const hash = useCache ? ZSTD_row_nextCachedHash(ms->hashCache, hashTable, tagTable, base, updateStartIdx, hashLog, rowLog, mls)
                                  : (U32)ZSTD_hashPtr(base + updateStartIdx, hashLog + ZSTD_ROW_HASH_TAG_BITS, mls);
        U32 const relRow = (hash >> ZSTD_ROW_HASH_TAG_BITS) << rowLog;
        U32* const row = hashTable + relRow;
        BYTE* tagRow = (BYTE*)(tagTable + relRow);  /* Though tagTable is laid out as a table of U16, each tag is only 1 byte.
                                                       Explicit cast allows us to get exact desired position within each row */
        U32 const pos = ZSTD_row_nextIndex(tagRow, rowMask);

        assert(hash == ZSTD_hashPtr(base + updateStartIdx, hashLog + ZSTD_ROW_HASH_TAG_BITS, mls));
        ((BYTE*)tagRow)[pos + ZSTD_ROW_HASH_TAG_OFFSET] = hash & ZSTD_ROW_HASH_TAG_MASK;
        row[pos] = updateStartIdx;
    }
}

/* ZSTD_row_update_internal():
 * Inserts the byte at ip into the appropriate position in the hash table, and updates ms->nextToUpdate.
 * Skips sections of long matches as is necessary.
 */
FORCE_INLINE_TEMPLATE void ZSTD_row_update_internal(ZSTD_matchState_t* ms, const BYTE* ip,
                                                    U32 const mls, U32 const rowLog,
                                                    U32 const rowMask, U32 const useCache)
{
    U32 idx = ms->nextToUpdate;
    const BYTE* const base = ms->window.base;
    const U32 target = (U32)(ip - base);
    const U32 kSkipThreshold = 384;
    const U32 kMaxMatchStartPositionsToUpdate = 96;
    const U32 kMaxMatchEndPositionsToUpdate = 32;

    if (useCache) {
        /* Only skip positions when using hash cache, i.e.
         * if we are loading a dict, don't skip anything.
         * If we decide to skip, then we only update a set number
         * of positions at the beginning and end of the match.
         */
        if (UNLIKELY(target - idx > kSkipThreshold)) {
            U32 const bound = idx + kMaxMatchStartPositionsToUpdate;
            ZSTD_row_update_internalImpl(ms, idx, bound, mls, rowLog, rowMask, useCache);
            idx = target - kMaxMatchEndPositionsToUpdate;
            ZSTD_row_fillHashCache(ms, base, rowLog, mls, idx, ip+1);
        }
    }
    assert(target >= idx);
    ZSTD_row_update_internalImpl(ms, idx, target, mls, rowLog, rowMask, useCache);
    ms->nextToUpdate = target;
}

/* ZSTD_row_update():
 * External wrapper for ZSTD_row_update_internal(). Used for filling the hashtable during dictionary
 * processing.
 */
void ZSTD_row_update(ZSTD_matchState_t* const ms, const BYTE* ip) {
    const U32 rowLog = BOUNDED(4, ms->cParams.searchLog, 6);
    const U32 rowMask = (1u << rowLog) - 1;
    const U32 mls = MIN(ms->cParams.minMatch, 6 /* mls caps out at 6 */);

    DEBUGLOG(5, "ZSTD_row_update(), rowLog=%u", rowLog);
    ZSTD_row_update_internal(ms, ip, mls, rowLog, rowMask, 0 /* dont use cache */);
}

#if defined(ZSTD_ARCH_X86_SSE2)
FORCE_INLINE_TEMPLATE ZSTD_VecMask
ZSTD_row_getSSEMask(int nbChunks, const BYTE* const src, const BYTE tag, const U32 head)
{
    const __m128i comparisonMask = _mm_set1_epi8((char)tag);
    int matches[4] = {0};
    int i;
    assert(nbChunks == 1 || nbChunks == 2 || nbChunks == 4);
    for (i=0; i<nbChunks; i++) {
        const __m128i chunk = _mm_loadu_si128((const __m128i*)(const void*)(src + 16*i));
        const __m128i equalMask = _mm_cmpeq_epi8(chunk, comparisonMask);
        matches[i] = _mm_movemask_epi8(equalMask);
    }
    if (nbChunks == 1) return ZSTD_rotateRight_U16((U16)matches[0], head);
    if (nbChunks == 2) return ZSTD_rotateRight_U32((U32)matches[1] << 16 | (U32)matches[0], head);
    assert(nbChunks == 4);
    return ZSTD_rotateRight_U64((U64)matches[3] << 48 | (U64)matches[2] << 32 | (U64)matches[1] << 16 | (U64)matches[0], head);
}
#endif

/* Returns a ZSTD_VecMask (U32) that has the nth bit set to 1 if the newly-computed "tag" matches
 * the hash at the nth position in a row of the tagTable.
 * Each row is a circular buffer beginning at the value of "head". So we must rotate the "matches" bitfield
 * to match up with the actual layout of the entries within the hashTable */
FORCE_INLINE_TEMPLATE ZSTD_VecMask
ZSTD_row_getMatchMask(const BYTE* const tagRow, const BYTE tag, const U32 head, const U32 rowEntries)
{
    const BYTE* const src = tagRow + ZSTD_ROW_HASH_TAG_OFFSET;
    assert((rowEntries == 16) || (rowEntries == 32) || rowEntries == 64);
    assert(rowEntries <= ZSTD_ROW_HASH_MAX_ENTRIES);

#if defined(ZSTD_ARCH_X86_SSE2)

    return ZSTD_row_getSSEMask(rowEntries / 16, src, tag, head);

#else /* SW or NEON-LE */

# if defined(ZSTD_ARCH_ARM_NEON)
  /* This NEON path only works for little endian - otherwise use SWAR below */
    if (MEM_isLittleEndian()) {
        if (rowEntries == 16) {
            const uint8x16_t chunk = vld1q_u8(src);
            const uint16x8_t equalMask = vreinterpretq_u16_u8(vceqq_u8(chunk, vdupq_n_u8(tag)));
            const uint16x8_t t0 = vshlq_n_u16(equalMask, 7);
            const uint32x4_t t1 = vreinterpretq_u32_u16(vsriq_n_u16(t0, t0, 14));
            const uint64x2_t t2 = vreinterpretq_u64_u32(vshrq_n_u32(t1, 14));
            const uint8x16_t t3 = vreinterpretq_u8_u64(vsraq_n_u64(t2, t2, 28));
            const U16 hi = (U16)vgetq_lane_u8(t3, 8);
            const U16 lo = (U16)vgetq_lane_u8(t3, 0);
            return ZSTD_rotateRight_U16((hi << 8) | lo, head);
        } else if (rowEntries == 32) {
            const uint16x8x2_t chunk = vld2q_u16((const U16*)(const void*)src);
            const uint8x16_t chunk0 = vreinterpretq_u8_u16(chunk.val[0]);
            const uint8x16_t chunk1 = vreinterpretq_u8_u16(chunk.val[1]);
            const uint8x16_t equalMask0 = vceqq_u8(chunk0, vdupq_n_u8(tag));
            const uint8x16_t equalMask1 = vceqq_u8(chunk1, vdupq_n_u8(tag));
            const int8x8_t pack0 = vqmovn_s16(vreinterpretq_s16_u8(equalMask0));
            const int8x8_t pack1 = vqmovn_s16(vreinterpretq_s16_u8(equalMask1));
            const uint8x8_t t0 = vreinterpret_u8_s8(pack0);
            const uint8x8_t t1 = vreinterpret_u8_s8(pack1);
            const uint8x8_t t2 = vsri_n_u8(t1, t0, 2);
            const uint8x8x2_t t3 = vuzp_u8(t2, t0);
            const uint8x8_t t4 = vsri_n_u8(t3.val[1], t3.val[0], 4);
            const U32 matches = vget_lane_u32(vreinterpret_u32_u8(t4), 0);
            return ZSTD_rotateRight_U32(matches, head);
        } else { /* rowEntries == 64 */
            const uint8x16x4_t chunk = vld4q_u8(src);
            const uint8x16_t dup = vdupq_n_u8(tag);
            const uint8x16_t cmp0 = vceqq_u8(chunk.val[0], dup);
            const uint8x16_t cmp1 = vceqq_u8(chunk.val[1], dup);
            const uint8x16_t cmp2 = vceqq_u8(chunk.val[2], dup);
            const uint8x16_t cmp3 = vceqq_u8(chunk.val[3], dup);

            const uint8x16_t t0 = vsriq_n_u8(cmp1, cmp0, 1);
            const uint8x16_t t1 = vsriq_n_u8(cmp3, cmp2, 1);
            const uint8x16_t t2 = vsriq_n_u8(t1, t0, 2);
            const uint8x16_t t3 = vsriq_n_u8(t2, t2, 4);
            const uint8x8_t t4 = vshrn_n_u16(vreinterpretq_u16_u8(t3), 4);
            const U64 matches = vget_lane_u64(vreinterpret_u64_u8(t4), 0);
            return ZSTD_rotateRight_U64(matches, head);
        }
    }
# endif /* ZSTD_ARCH_ARM_NEON */
    /* SWAR */
    {   const size_t chunkSize = sizeof(size_t);
        const size_t shiftAmount = ((chunkSize * 8) - chunkSize);
        const size_t xFF = ~((size_t)0);
        const size_t x01 = xFF / 0xFF;
        const size_t x80 = x01 << 7;
        const size_t splatChar = tag * x01;
        ZSTD_VecMask matches = 0;
        int i = rowEntries - chunkSize;
        assert((sizeof(size_t) == 4) || (sizeof(size_t) == 8));
        if (MEM_isLittleEndian()) { /* runtime check so have two loops */
            const size_t extractMagic = (xFF / 0x7F) >> chunkSize;
            do {
                size_t chunk = MEM_readST(&src[i]);
                chunk ^= splatChar;
                chunk = (((chunk | x80) - x01) | chunk) & x80;
                matches <<= chunkSize;
                matches |= (chunk * extractMagic) >> shiftAmount;
                i -= chunkSize;
            } while (i >= 0);
        } else { /* big endian: reverse bits during extraction */
            const size_t msb = xFF ^ (xFF >> 1);
            const size_t extractMagic = (msb / 0x1FF) | msb;
            do {
                size_t chunk = MEM_readST(&src[i]);
                chunk ^= splatChar;
                chunk = (((chunk | x80) - x01) | chunk) & x80;
                matches <<= chunkSize;
                matches |= ((chunk >> 7) * extractMagic) >> shiftAmount;
                i -= chunkSize;
            } while (i >= 0);
        }
        matches = ~matches;
        if (rowEntries == 16) {
            return ZSTD_rotateRight_U16((U16)matches, head);
        } else if (rowEntries == 32) {
            return ZSTD_rotateRight_U32((U32)matches, head);
        } else {
            return ZSTD_rotateRight_U64((U64)matches, head);
        }
    }
#endif
}

/* The high-level approach of the SIMD row based match finder is as follows:
 * - Figure out where to insert the new entry:
 *      - Generate a hash from a byte along with an additional 1-byte "short hash". The additional byte is our "tag"
 *      - The hashTable is effectively split into groups or "rows" of 16 or 32 entries of U32, and the hash determines
 *        which row to insert into.
 *      - Determine the correct position within the row to insert the entry into. Each row of 16 or 32 can
 *        be considered as a circular buffer with a "head" index that resides in the tagTable.
 *      - Also insert the "tag" into the equivalent row and position in the tagTable.
 *          - Note: The tagTable has 17 or 33 1-byte entries per row, due to 16 or 32 tags, and 1 "head" entry.
 *                  The 17 or 33 entry rows are spaced out to occur every 32 or 64 bytes, respectively,
 *                  for alignment/performance reasons, leaving some bytes unused.
 * - Use SIMD to efficiently compare the tags in the tagTable to the 1-byte "short hash" and
 *   generate a bitfield that we can cycle through to check the collisions in the hash table.
 * - Pick the longest match.
 */
FORCE_INLINE_TEMPLATE
size_t ZSTD_RowFindBestMatch(
                        ZSTD_matchState_t* ms,
                        const BYTE* const ip, const BYTE* const iLimit,
                        size_t* offsetPtr,
                        const U32 mls, const ZSTD_dictMode_e dictMode,
                        const U32 rowLog)
{
    U32* const hashTable = ms->hashTable;
    U16* const tagTable = ms->tagTable;
    U32* const hashCache = ms->hashCache;
    const U32 hashLog = ms->rowHashLog;
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    const BYTE* const base = ms->window.base;
    const BYTE* const dictBase = ms->window.dictBase;
    const U32 dictLimit = ms->window.dictLimit;
    const BYTE* const prefixStart = base + dictLimit;
    const BYTE* const dictEnd = dictBase + dictLimit;
    const U32 curr = (U32)(ip-base);
    const U32 maxDistance = 1U << cParams->windowLog;
    const U32 lowestValid = ms->window.lowLimit;
    const U32 withinMaxDistance = (curr - lowestValid > maxDistance) ? curr - maxDistance : lowestValid;
    const U32 isDictionary = (ms->loadedDictEnd != 0);
    const U32 lowLimit = isDictionary ? lowestValid : withinMaxDistance;
    const U32 rowEntries = (1U << rowLog);
    const U32 rowMask = rowEntries - 1;
    const U32 cappedSearchLog = MIN(cParams->searchLog, rowLog); /* nb of searches is capped at nb entries per row */
    U32 nbAttempts = 1U << cappedSearchLog;
    size_t ml=4-1;

    /* DMS/DDS variables that may be referenced laster */
    const ZSTD_matchState_t* const dms = ms->dictMatchState;

    /* Initialize the following variables to satisfy static analyzer */
    size_t ddsIdx = 0;
    U32 ddsExtraAttempts = 0; /* cctx hash tables are limited in searches, but allow extra searches into DDS */
    U32 dmsTag = 0;
    U32* dmsRow = NULL;
    BYTE* dmsTagRow = NULL;

    if (dictMode == ZSTD_dedicatedDictSearch) {
        const U32 ddsHashLog = dms->cParams.hashLog - ZSTD_LAZY_DDSS_BUCKET_LOG;
        {   /* Prefetch DDS hashtable entry */
            ddsIdx = ZSTD_hashPtr(ip, ddsHashLog, mls) << ZSTD_LAZY_DDSS_BUCKET_LOG;
            PREFETCH_L1(&dms->hashTable[ddsIdx]);
        }
        ddsExtraAttempts = cParams->searchLog > rowLog ? 1U << (cParams->searchLog - rowLog) : 0;
    }

    if (dictMode == ZSTD_dictMatchState) {
        /* Prefetch DMS rows */
        U32* const dmsHashTable = dms->hashTable;
        U16* const dmsTagTable = dms->tagTable;
        U32 const dmsHash = (U32)ZSTD_hashPtr(ip, dms->rowHashLog + ZSTD_ROW_HASH_TAG_BITS, mls);
        U32 const dmsRelRow = (dmsHash >> ZSTD_ROW_HASH_TAG_BITS) << rowLog;
        dmsTag = dmsHash & ZSTD_ROW_HASH_TAG_MASK;
        dmsTagRow = (BYTE*)(dmsTagTable + dmsRelRow);
        dmsRow = dmsHashTable + dmsRelRow;
        ZSTD_row_prefetch(dmsHashTable, dmsTagTable, dmsRelRow, rowLog);
    }

    /* Update the hashTable and tagTable up to (but not including) ip */
    ZSTD_row_update_internal(ms, ip, mls, rowLog, rowMask, 1 /* useCache */);
    {   /* Get the hash for ip, compute the appropriate row */
        U32 const hash = ZSTD_row_nextCachedHash(hashCache, hashTable, tagTable, base, curr, hashLog, rowLog, mls);
        U32 const relRow = (hash >> ZSTD_ROW_HASH_TAG_BITS) << rowLog;
        U32 const tag = hash & ZSTD_ROW_HASH_TAG_MASK;
        U32* const row = hashTable + relRow;
        BYTE* tagRow = (BYTE*)(tagTable + relRow);
        U32 const head = *tagRow & rowMask;
        U32 matchBuffer[ZSTD_ROW_HASH_MAX_ENTRIES];
        size_t numMatches = 0;
        size_t currMatch = 0;
        ZSTD_VecMask matches = ZSTD_row_getMatchMask(tagRow, (BYTE)tag, head, rowEntries);

        /* Cycle through the matches and prefetch */
        for (; (matches > 0) && (nbAttempts > 0); --nbAttempts, matches &= (matches - 1)) {
            U32 const matchPos = (head + ZSTD_VecMask_next(matches)) & rowMask;
            U32 const matchIndex = row[matchPos];
            assert(numMatches < rowEntries);
            if (matchIndex < lowLimit)
                break;
            if ((dictMode != ZSTD_extDict) || matchIndex >= dictLimit) {
                PREFETCH_L1(base + matchIndex);
            } else {
                PREFETCH_L1(dictBase + matchIndex);
            }
            matchBuffer[numMatches++] = matchIndex;
        }

        /* Speed opt: insert current byte into hashtable too. This allows us to avoid one iteration of the loop
           in ZSTD_row_update_internal() at the next search. */
        {
            U32 const pos = ZSTD_row_nextIndex(tagRow, rowMask);
            tagRow[pos + ZSTD_ROW_HASH_TAG_OFFSET] = (BYTE)tag;
            row[pos] = ms->nextToUpdate++;
        }

        /* Return the longest match */
        for (; currMatch < numMatches; ++currMatch) {
            U32 const matchIndex = matchBuffer[currMatch];
            size_t currentMl=0;
            assert(matchIndex < curr);
            assert(matchIndex >= lowLimit);

            if ((dictMode != ZSTD_extDict) || matchIndex >= dictLimit) {
                const BYTE* const match = base + matchIndex;
                assert(matchIndex >= dictLimit);   /* ensures this is true if dictMode != ZSTD_extDict */
                if (match[ml] == ip[ml])   /* potentially better */
                    currentMl = ZSTD_count(ip, match, iLimit);
            } else {
                const BYTE* const match = dictBase + matchIndex;
                assert(match+4 <= dictEnd);
                if (MEM_read32(match) == MEM_read32(ip))   /* assumption : matchIndex <= dictLimit-4 (by table construction) */
                    currentMl = ZSTD_count_2segments(ip+4, match+4, iLimit, dictEnd, prefixStart) + 4;
            }

            /* Save best solution */
            if (currentMl > ml) {
                ml = currentMl;
                *offsetPtr = STORE_OFFSET(curr - matchIndex);
                if (ip+currentMl == iLimit) break; /* best possible, avoids read overflow on next attempt */
            }
        }
    }

    assert(nbAttempts <= (1U << ZSTD_SEARCHLOG_MAX)); /* Check we haven't underflowed. */
    if (dictMode == ZSTD_dedicatedDictSearch) {
        ml = ZSTD_dedicatedDictSearch_lazy_search(offsetPtr, ml, nbAttempts + ddsExtraAttempts, dms,
                                                  ip, iLimit, prefixStart, curr, dictLimit, ddsIdx);
    } else if (dictMode == ZSTD_dictMatchState) {
        /* TODO: Measure and potentially add prefetching to DMS */
        const U32 dmsLowestIndex       = dms->window.dictLimit;
        const BYTE* const dmsBase      = dms->window.base;
        const BYTE* const dmsEnd       = dms->window.nextSrc;
        const U32 dmsSize              = (U32)(dmsEnd - dmsBase);
        const U32 dmsIndexDelta        = dictLimit - dmsSize;

        {   U32 const head = *dmsTagRow & rowMask;
            U32 matchBuffer[ZSTD_ROW_HASH_MAX_ENTRIES];
            size_t numMatches = 0;
            size_t currMatch = 0;
            ZSTD_VecMask matches = ZSTD_row_getMatchMask(dmsTagRow, (BYTE)dmsTag, head, rowEntries);

            for (; (matches > 0) && (nbAttempts > 0); --nbAttempts, matches &= (matches - 1)) {
                U32 const matchPos = (head + ZSTD_VecMask_next(matches)) & rowMask;
                U32 const matchIndex = dmsRow[matchPos];
                if (matchIndex < dmsLowestIndex)
                    break;
                PREFETCH_L1(dmsBase + matchIndex);
                matchBuffer[numMatches++] = matchIndex;
            }

            /* Return the longest match */
            for (; currMatch < numMatches; ++currMatch) {
                U32 const matchIndex = matchBuffer[currMatch];
                size_t currentMl=0;
                assert(matchIndex >= dmsLowestIndex);
                assert(matchIndex < curr);

                {   const BYTE* const match = dmsBase + matchIndex;
                    assert(match+4 <= dmsEnd);
                    if (MEM_read32(match) == MEM_read32(ip))
                        currentMl = ZSTD_count_2segments(ip+4, match+4, iLimit, dmsEnd, prefixStart) + 4;
                }

                if (currentMl > ml) {
                    ml = currentMl;
                    assert(curr > matchIndex + dmsIndexDelta);
                    *offsetPtr = STORE_OFFSET(curr - (matchIndex + dmsIndexDelta));
                    if (ip+currentMl == iLimit) break;
                }
            }
        }
    }
    return ml;
}


/*
 * Generate search functions templated on (dictMode, mls, rowLog).
 * These functions are outlined for code size & compilation time.
 * ZSTD_searchMax() dispatches to the correct implementation function.
 *
 * TODO: The start of the search function involves loading and calculating a
 * bunch of constants from the ZSTD_matchState_t. These computations could be
 * done in an initialization function, and saved somewhere in the match state.
 * Then we could pass a pointer to the saved state instead of the match state,
 * and avoid duplicate computations.
 *
 * TODO: Move the match re-winding into searchMax. This improves compression
 * ratio, and unlocks further simplifications with the next TODO.
 *
 * TODO: Try moving the repcode search into searchMax. After the re-winding
 * and repcode search are in searchMax, there is no more logic in the match
 * finder loop that requires knowledge about the dictMode. So we should be
 * able to avoid force inlining it, and we can join the extDict loop with
 * the single segment loop. It should go in searchMax instead of its own
 * function to avoid having multiple virtual function calls per search.
 */

#define ZSTD_BT_SEARCH_FN(dictMode, mls) ZSTD_BtFindBestMatch_##dictMode##_##mls
#define ZSTD_HC_SEARCH_FN(dictMode, mls) ZSTD_HcFindBestMatch_##dictMode##_##mls
#define ZSTD_ROW_SEARCH_FN(dictMode, mls, rowLog) ZSTD_RowFindBestMatch_##dictMode##_##mls##_##rowLog

#define ZSTD_SEARCH_FN_ATTRS FORCE_NOINLINE

#define GEN_ZSTD_BT_SEARCH_FN(dictMode, mls)                                           \
    ZSTD_SEARCH_FN_ATTRS size_t ZSTD_BT_SEARCH_FN(dictMode, mls)(                      \
            ZSTD_matchState_t* ms,                                                     \
            const BYTE* ip, const BYTE* const iLimit,                                  \
            size_t* offBasePtr)                                                        \
    {                                                                                  \
        assert(MAX(4, MIN(6, ms->cParams.minMatch)) == mls);                           \
        return ZSTD_BtFindBestMatch(ms, ip, iLimit, offBasePtr, mls, ZSTD_##dictMode); \
    }                                                                                  \

#define GEN_ZSTD_HC_SEARCH_FN(dictMode, mls)                                          \
    ZSTD_SEARCH_FN_ATTRS size_t ZSTD_HC_SEARCH_FN(dictMode, mls)(                     \
            ZSTD_matchState_t* ms,                                                    \
            const BYTE* ip, const BYTE* const iLimit,                                 \
            size_t* offsetPtr)                                                        \
    {                                                                                 \
        assert(MAX(4, MIN(6, ms->cParams.minMatch)) == mls);                          \
        return ZSTD_HcFindBestMatch(ms, ip, iLimit, offsetPtr, mls, ZSTD_##dictMode); \
    }                                                                                 \

#define GEN_ZSTD_ROW_SEARCH_FN(dictMode, mls, rowLog)                                          \
    ZSTD_SEARCH_FN_ATTRS size_t ZSTD_ROW_SEARCH_FN(dictMode, mls, rowLog)(                     \
            ZSTD_matchState_t* ms,                                                             \
            const BYTE* ip, const BYTE* const iLimit,                                          \
            size_t* offsetPtr)                                                                 \
    {                                                                                          \
        assert(MAX(4, MIN(6, ms->cParams.minMatch)) == mls);                                   \
        assert(MAX(4, MIN(6, ms->cParams.searchLog)) == rowLog);                               \
        return ZSTD_RowFindBestMatch(ms, ip, iLimit, offsetPtr, mls, ZSTD_##dictMode, rowLog); \
    }                                                                                          \

#define ZSTD_FOR_EACH_ROWLOG(X, dictMode, mls) \
    X(dictMode, mls, 4)                        \
    X(dictMode, mls, 5)                        \
    X(dictMode, mls, 6)

#define ZSTD_FOR_EACH_MLS_ROWLOG(X, dictMode) \
    ZSTD_FOR_EACH_ROWLOG(X, dictMode, 4)      \
    ZSTD_FOR_EACH_ROWLOG(X, dictMode, 5)      \
    ZSTD_FOR_EACH_ROWLOG(X, dictMode, 6)

#define ZSTD_FOR_EACH_MLS(X, dictMode) \
    X(dictMode, 4)                     \
    X(dictMode, 5)                     \
    X(dictMode, 6)

#define ZSTD_FOR_EACH_DICT_MODE(X, ...) \
    X(__VA_ARGS__, noDict)              \
    X(__VA_ARGS__, extDict)             \
    X(__VA_ARGS__, dictMatchState)      \
    X(__VA_ARGS__, dedicatedDictSearch)

/* Generate row search fns for each combination of (dictMode, mls, rowLog) */
ZSTD_FOR_EACH_DICT_MODE(ZSTD_FOR_EACH_MLS_ROWLOG, GEN_ZSTD_ROW_SEARCH_FN)
/* Generate binary Tree search fns for each combination of (dictMode, mls) */
ZSTD_FOR_EACH_DICT_MODE(ZSTD_FOR_EACH_MLS, GEN_ZSTD_BT_SEARCH_FN)
/* Generate hash chain search fns for each combination of (dictMode, mls) */
ZSTD_FOR_EACH_DICT_MODE(ZSTD_FOR_EACH_MLS, GEN_ZSTD_HC_SEARCH_FN)

typedef enum { search_hashChain=0, search_binaryTree=1, search_rowHash=2 } searchMethod_e;

#define GEN_ZSTD_CALL_BT_SEARCH_FN(dictMode, mls)                         \
    case mls:                                                             \
        return ZSTD_BT_SEARCH_FN(dictMode, mls)(ms, ip, iend, offsetPtr);
#define GEN_ZSTD_CALL_HC_SEARCH_FN(dictMode, mls)                         \
    case mls:                                                             \
        return ZSTD_HC_SEARCH_FN(dictMode, mls)(ms, ip, iend, offsetPtr);
#define GEN_ZSTD_CALL_ROW_SEARCH_FN(dictMode, mls, rowLog)                         \
    case rowLog:                                                                   \
        return ZSTD_ROW_SEARCH_FN(dictMode, mls, rowLog)(ms, ip, iend, offsetPtr);

#define ZSTD_SWITCH_MLS(X, dictMode)   \
    switch (mls) {                     \
        ZSTD_FOR_EACH_MLS(X, dictMode) \
    }

#define ZSTD_SWITCH_ROWLOG(dictMode, mls)                                    \
    case mls:                                                                \
        switch (rowLog) {                                                    \
            ZSTD_FOR_EACH_ROWLOG(GEN_ZSTD_CALL_ROW_SEARCH_FN, dictMode, mls) \
        }                                                                    \
        ZSTD_UNREACHABLE;                                                    \
        break;

#define ZSTD_SWITCH_SEARCH_METHOD(dictMode)                       \
    switch (searchMethod) {                                       \
        case search_hashChain:                                    \
            ZSTD_SWITCH_MLS(GEN_ZSTD_CALL_HC_SEARCH_FN, dictMode) \
            break;                                                \
        case search_binaryTree:                                   \
            ZSTD_SWITCH_MLS(GEN_ZSTD_CALL_BT_SEARCH_FN, dictMode) \
            break;                                                \
        case search_rowHash:                                      \
            ZSTD_SWITCH_MLS(ZSTD_SWITCH_ROWLOG, dictMode)         \
            break;                                                \
    }                                                             \
    ZSTD_UNREACHABLE;

/*
 * Searches for the longest match at @p ip.
 * Dispatches to the correct implementation function based on the
 * (searchMethod, dictMode, mls, rowLog). We use switch statements
 * here instead of using an indirect function call through a function
 * pointer because after Spectre and Meltdown mitigations, indirect
 * function calls can be very costly, especially in the kernel.
 *
 * NOTE: dictMode and searchMethod should be templated, so those switch
 * statements should be optimized out. Only the mls & rowLog switches
 * should be left.
 *
 * @param ms The match state.
 * @param ip The position to search at.
 * @param iend The end of the input data.
 * @param[out] offsetPtr Stores the match offset into this pointer.
 * @param mls The minimum search length, in the range [4, 6].
 * @param rowLog The row log (if applicable), in the range [4, 6].
 * @param searchMethod The search method to use (templated).
 * @param dictMode The dictMode (templated).
 *
 * @returns The length of the longest match found, or < mls if no match is found.
 * If a match is found its offset is stored in @p offsetPtr.
 */
FORCE_INLINE_TEMPLATE size_t ZSTD_searchMax(
    ZSTD_matchState_t* ms,
    const BYTE* ip,
    const BYTE* iend,
    size_t* offsetPtr,
    U32 const mls,
    U32 const rowLog,
    searchMethod_e const searchMethod,
    ZSTD_dictMode_e const dictMode)
{
    if (dictMode == ZSTD_noDict) {
        ZSTD_SWITCH_SEARCH_METHOD(noDict)
    } else if (dictMode == ZSTD_extDict) {
        ZSTD_SWITCH_SEARCH_METHOD(extDict)
    } else if (dictMode == ZSTD_dictMatchState) {
        ZSTD_SWITCH_SEARCH_METHOD(dictMatchState)
    } else if (dictMode == ZSTD_dedicatedDictSearch) {
        ZSTD_SWITCH_SEARCH_METHOD(dedicatedDictSearch)
    }
    ZSTD_UNREACHABLE;
    return 0;
}

/* *******************************
*  Common parser - lazy strategy
*********************************/

FORCE_INLINE_TEMPLATE size_t
ZSTD_compressBlock_lazy_generic(
                        ZSTD_matchState_t* ms, seqStore_t* seqStore,
                        U32 rep[ZSTD_REP_NUM],
                        const void* src, size_t srcSize,
                        const searchMethod_e searchMethod, const U32 depth,
                        ZSTD_dictMode_e const dictMode)
{
    const BYTE* const istart = (const BYTE*)src;
    const BYTE* ip = istart;
    const BYTE* anchor = istart;
    const BYTE* const iend = istart + srcSize;
    const BYTE* const ilimit = (searchMethod == search_rowHash) ? iend - 8 - ZSTD_ROW_HASH_CACHE_SIZE : iend - 8;
    const BYTE* const base = ms->window.base;
    const U32 prefixLowestIndex = ms->window.dictLimit;
    const BYTE* const prefixLowest = base + prefixLowestIndex;
    const U32 mls = BOUNDED(4, ms->cParams.minMatch, 6);
    const U32 rowLog = BOUNDED(4, ms->cParams.searchLog, 6);

    U32 offset_1 = rep[0], offset_2 = rep[1], savedOffset=0;

    const int isDMS = dictMode == ZSTD_dictMatchState;
    const int isDDS = dictMode == ZSTD_dedicatedDictSearch;
    const int isDxS = isDMS || isDDS;
    const ZSTD_matchState_t* const dms = ms->dictMatchState;
    const U32 dictLowestIndex      = isDxS ? dms->window.dictLimit : 0;
    const BYTE* const dictBase     = isDxS ? dms->window.base : NULL;
    const BYTE* const dictLowest   = isDxS ? dictBase + dictLowestIndex : NULL;
    const BYTE* const dictEnd      = isDxS ? dms->window.nextSrc : NULL;
    const U32 dictIndexDelta       = isDxS ?
                                     prefixLowestIndex - (U32)(dictEnd - dictBase) :
                                     0;
    const U32 dictAndPrefixLength = (U32)((ip - prefixLowest) + (dictEnd - dictLowest));

    DEBUGLOG(5, "ZSTD_compressBlock_lazy_generic (dictMode=%u) (searchFunc=%u)", (U32)dictMode, (U32)searchMethod);
    ip += (dictAndPrefixLength == 0);
    if (dictMode == ZSTD_noDict) {
        U32 const curr = (U32)(ip - base);
        U32 const windowLow = ZSTD_getLowestPrefixIndex(ms, curr, ms->cParams.windowLog);
        U32 const maxRep = curr - windowLow;
        if (offset_2 > maxRep) savedOffset = offset_2, offset_2 = 0;
        if (offset_1 > maxRep) savedOffset = offset_1, offset_1 = 0;
    }
    if (isDxS) {
        /* dictMatchState repCode checks don't currently handle repCode == 0
         * disabling. */
        assert(offset_1 <= dictAndPrefixLength);
        assert(offset_2 <= dictAndPrefixLength);
    }

    if (searchMethod == search_rowHash) {
        ZSTD_row_fillHashCache(ms, base, rowLog,
                            MIN(ms->cParams.minMatch, 6 /* mls caps out at 6 */),
                            ms->nextToUpdate, ilimit);
    }

    /* Match Loop */
#if defined(__x86_64__)
    /* I've measured random a 5% speed loss on levels 5 & 6 (greedy) when the
     * code alignment is perturbed. To fix the instability align the loop on 32-bytes.
     */
    __asm__(".p2align 5");
#endif
    while (ip < ilimit) {
        size_t matchLength=0;
        size_t offcode=STORE_REPCODE_1;
        const BYTE* start=ip+1;
        DEBUGLOG(7, "search baseline (depth 0)");

        /* check repCode */
        if (isDxS) {
            const U32 repIndex = (U32)(ip - base) + 1 - offset_1;
            const BYTE* repMatch = ((dictMode == ZSTD_dictMatchState || dictMode == ZSTD_dedicatedDictSearch)
                                && repIndex < prefixLowestIndex) ?
                                   dictBase + (repIndex - dictIndexDelta) :
                                   base + repIndex;
            if (((U32)((prefixLowestIndex-1) - repIndex) >= 3 /* intentional underflow */)
                && (MEM_read32(repMatch) == MEM_read32(ip+1)) ) {
                const BYTE* repMatchEnd = repIndex < prefixLowestIndex ? dictEnd : iend;
                matchLength = ZSTD_count_2segments(ip+1+4, repMatch+4, iend, repMatchEnd, prefixLowest) + 4;
                if (depth==0) goto _storeSequence;
            }
        }
        if ( dictMode == ZSTD_noDict
          && ((offset_1 > 0) & (MEM_read32(ip+1-offset_1) == MEM_read32(ip+1)))) {
            matchLength = ZSTD_count(ip+1+4, ip+1+4-offset_1, iend) + 4;
            if (depth==0) goto _storeSequence;
        }

        /* first search (depth 0) */
        {   size_t offsetFound = 999999999;
            size_t const ml2 = ZSTD_searchMax(ms, ip, iend, &offsetFound, mls, rowLog, searchMethod, dictMode);
            if (ml2 > matchLength)
                matchLength = ml2, start = ip, offcode=offsetFound;
        }

        if (matchLength < 4) {
            ip += ((ip-anchor) >> kSearchStrength) + 1;   /* jump faster over incompressible sections */
            continue;
        }

        /* let's try to find a better solution */
        if (depth>=1)
        while (ip<ilimit) {
            DEBUGLOG(7, "search depth 1");
            ip ++;
            if ( (dictMode == ZSTD_noDict)
              && (offcode) && ((offset_1>0) & (MEM_read32(ip) == MEM_read32(ip - offset_1)))) {
                size_t const mlRep = ZSTD_count(ip+4, ip+4-offset_1, iend) + 4;
                int const gain2 = (int)(mlRep * 3);
                int const gain1 = (int)(matchLength*3 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offcode)) + 1);
                if ((mlRep >= 4) && (gain2 > gain1))
                    matchLength = mlRep, offcode = STORE_REPCODE_1, start = ip;
            }
            if (isDxS) {
                const U32 repIndex = (U32)(ip - base) - offset_1;
                const BYTE* repMatch = repIndex < prefixLowestIndex ?
                               dictBase + (repIndex - dictIndexDelta) :
                               base + repIndex;
                if (((U32)((prefixLowestIndex-1) - repIndex) >= 3 /* intentional underflow */)
                    && (MEM_read32(repMatch) == MEM_read32(ip)) ) {
                    const BYTE* repMatchEnd = repIndex < prefixLowestIndex ? dictEnd : iend;
                    size_t const mlRep = ZSTD_count_2segments(ip+4, repMatch+4, iend, repMatchEnd, prefixLowest) + 4;
                    int const gain2 = (int)(mlRep * 3);
                    int const gain1 = (int)(matchLength*3 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offcode)) + 1);
                    if ((mlRep >= 4) && (gain2 > gain1))
                        matchLength = mlRep, offcode = STORE_REPCODE_1, start = ip;
                }
            }
            {   size_t offset2=999999999;
                size_t const ml2 = ZSTD_searchMax(ms, ip, iend, &offset2, mls, rowLog, searchMethod, dictMode);
                int const gain2 = (int)(ml2*4 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offset2)));   /* raw approx */
                int const gain1 = (int)(matchLength*4 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offcode)) + 4);
                if ((ml2 >= 4) && (gain2 > gain1)) {
                    matchLength = ml2, offcode = offset2, start = ip;
                    continue;   /* search a better one */
            }   }

            /* let's find an even better one */
            if ((depth==2) && (ip<ilimit)) {
                DEBUGLOG(7, "search depth 2");
                ip ++;
                if ( (dictMode == ZSTD_noDict)
                  && (offcode) && ((offset_1>0) & (MEM_read32(ip) == MEM_read32(ip - offset_1)))) {
                    size_t const mlRep = ZSTD_count(ip+4, ip+4-offset_1, iend) + 4;
                    int const gain2 = (int)(mlRep * 4);
                    int const gain1 = (int)(matchLength*4 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offcode)) + 1);
                    if ((mlRep >= 4) && (gain2 > gain1))
                        matchLength = mlRep, offcode = STORE_REPCODE_1, start = ip;
                }
                if (isDxS) {
                    const U32 repIndex = (U32)(ip - base) - offset_1;
                    const BYTE* repMatch = repIndex < prefixLowestIndex ?
                                   dictBase + (repIndex - dictIndexDelta) :
                                   base + repIndex;
                    if (((U32)((prefixLowestIndex-1) - repIndex) >= 3 /* intentional underflow */)
                        && (MEM_read32(repMatch) == MEM_read32(ip)) ) {
                        const BYTE* repMatchEnd = repIndex < prefixLowestIndex ? dictEnd : iend;
                        size_t const mlRep = ZSTD_count_2segments(ip+4, repMatch+4, iend, repMatchEnd, prefixLowest) + 4;
                        int const gain2 = (int)(mlRep * 4);
                        int const gain1 = (int)(matchLength*4 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offcode)) + 1);
                        if ((mlRep >= 4) && (gain2 > gain1))
                            matchLength = mlRep, offcode = STORE_REPCODE_1, start = ip;
                    }
                }
                {   size_t offset2=999999999;
                    size_t const ml2 = ZSTD_searchMax(ms, ip, iend, &offset2, mls, rowLog, searchMethod, dictMode);
                    int const gain2 = (int)(ml2*4 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offset2)));   /* raw approx */
                    int const gain1 = (int)(matchLength*4 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offcode)) + 7);
                    if ((ml2 >= 4) && (gain2 > gain1)) {
                        matchLength = ml2, offcode = offset2, start = ip;
                        continue;
            }   }   }
            break;  /* nothing found : store previous solution */
        }

        /* NOTE:
         * Pay attention that `start[-value]` can lead to strange undefined behavior
         * notably if `value` is unsigned, resulting in a large positive `-value`.
         */
        /* catch up */
        if (STORED_IS_OFFSET(offcode)) {
            if (dictMode == ZSTD_noDict) {
                while ( ((start > anchor) & (start - STORED_OFFSET(offcode) > prefixLowest))
                     && (start[-1] == (start-STORED_OFFSET(offcode))[-1]) )  /* only search for offset within prefix */
                    { start--; matchLength++; }
            }
            if (isDxS) {
                U32 const matchIndex = (U32)((size_t)(start-base) - STORED_OFFSET(offcode));
                const BYTE* match = (matchIndex < prefixLowestIndex) ? dictBase + matchIndex - dictIndexDelta : base + matchIndex;
                const BYTE* const mStart = (matchIndex < prefixLowestIndex) ? dictLowest : prefixLowest;
                while ((start>anchor) && (match>mStart) && (start[-1] == match[-1])) { start--; match--; matchLength++; }  /* catch up */
            }
            offset_2 = offset_1; offset_1 = (U32)STORED_OFFSET(offcode);
        }
        /* store sequence */
_storeSequence:
        {   size_t const litLength = (size_t)(start - anchor);
            ZSTD_storeSeq(seqStore, litLength, anchor, iend, (U32)offcode, matchLength);
            anchor = ip = start + matchLength;
        }

        /* check immediate repcode */
        if (isDxS) {
            while (ip <= ilimit) {
                U32 const current2 = (U32)(ip-base);
                U32 const repIndex = current2 - offset_2;
                const BYTE* repMatch = repIndex < prefixLowestIndex ?
                        dictBase - dictIndexDelta + repIndex :
                        base + repIndex;
                if ( ((U32)((prefixLowestIndex-1) - (U32)repIndex) >= 3 /* intentional overflow */)
                   && (MEM_read32(repMatch) == MEM_read32(ip)) ) {
                    const BYTE* const repEnd2 = repIndex < prefixLowestIndex ? dictEnd : iend;
                    matchLength = ZSTD_count_2segments(ip+4, repMatch+4, iend, repEnd2, prefixLowest) + 4;
                    offcode = offset_2; offset_2 = offset_1; offset_1 = (U32)offcode;   /* swap offset_2 <=> offset_1 */
                    ZSTD_storeSeq(seqStore, 0, anchor, iend, STORE_REPCODE_1, matchLength);
                    ip += matchLength;
                    anchor = ip;
                    continue;
                }
                break;
            }
        }

        if (dictMode == ZSTD_noDict) {
            while ( ((ip <= ilimit) & (offset_2>0))
                 && (MEM_read32(ip) == MEM_read32(ip - offset_2)) ) {
                /* store sequence */
                matchLength = ZSTD_count(ip+4, ip+4-offset_2, iend) + 4;
                offcode = offset_2; offset_2 = offset_1; offset_1 = (U32)offcode; /* swap repcodes */
                ZSTD_storeSeq(seqStore, 0, anchor, iend, STORE_REPCODE_1, matchLength);
                ip += matchLength;
                anchor = ip;
                continue;   /* faster when present ... (?) */
    }   }   }

    /* Save reps for next block */
    rep[0] = offset_1 ? offset_1 : savedOffset;
    rep[1] = offset_2 ? offset_2 : savedOffset;

    /* Return the last literals size */
    return (size_t)(iend - anchor);
}


size_t ZSTD_compressBlock_btlazy2(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_binaryTree, 2, ZSTD_noDict);
}

size_t ZSTD_compressBlock_lazy2(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 2, ZSTD_noDict);
}

size_t ZSTD_compressBlock_lazy(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 1, ZSTD_noDict);
}

size_t ZSTD_compressBlock_greedy(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 0, ZSTD_noDict);
}

size_t ZSTD_compressBlock_btlazy2_dictMatchState(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_binaryTree, 2, ZSTD_dictMatchState);
}

size_t ZSTD_compressBlock_lazy2_dictMatchState(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 2, ZSTD_dictMatchState);
}

size_t ZSTD_compressBlock_lazy_dictMatchState(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 1, ZSTD_dictMatchState);
}

size_t ZSTD_compressBlock_greedy_dictMatchState(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 0, ZSTD_dictMatchState);
}


size_t ZSTD_compressBlock_lazy2_dedicatedDictSearch(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 2, ZSTD_dedicatedDictSearch);
}

size_t ZSTD_compressBlock_lazy_dedicatedDictSearch(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 1, ZSTD_dedicatedDictSearch);
}

size_t ZSTD_compressBlock_greedy_dedicatedDictSearch(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 0, ZSTD_dedicatedDictSearch);
}

/* Row-based matchfinder */
size_t ZSTD_compressBlock_lazy2_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 2, ZSTD_noDict);
}

size_t ZSTD_compressBlock_lazy_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 1, ZSTD_noDict);
}

size_t ZSTD_compressBlock_greedy_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 0, ZSTD_noDict);
}

size_t ZSTD_compressBlock_lazy2_dictMatchState_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 2, ZSTD_dictMatchState);
}

size_t ZSTD_compressBlock_lazy_dictMatchState_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 1, ZSTD_dictMatchState);
}

size_t ZSTD_compressBlock_greedy_dictMatchState_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 0, ZSTD_dictMatchState);
}


size_t ZSTD_compressBlock_lazy2_dedicatedDictSearch_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 2, ZSTD_dedicatedDictSearch);
}

size_t ZSTD_compressBlock_lazy_dedicatedDictSearch_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 1, ZSTD_dedicatedDictSearch);
}

size_t ZSTD_compressBlock_greedy_dedicatedDictSearch_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 0, ZSTD_dedicatedDictSearch);
}

FORCE_INLINE_TEMPLATE
size_t ZSTD_compressBlock_lazy_extDict_generic(
                        ZSTD_matchState_t* ms, seqStore_t* seqStore,
                        U32 rep[ZSTD_REP_NUM],
                        const void* src, size_t srcSize,
                        const searchMethod_e searchMethod, const U32 depth)
{
    const BYTE* const istart = (const BYTE*)src;
    const BYTE* ip = istart;
    const BYTE* anchor = istart;
    const BYTE* const iend = istart + srcSize;
    const BYTE* const ilimit = searchMethod == search_rowHash ? iend - 8 - ZSTD_ROW_HASH_CACHE_SIZE : iend - 8;
    const BYTE* const base = ms->window.base;
    const U32 dictLimit = ms->window.dictLimit;
    const BYTE* const prefixStart = base + dictLimit;
    const BYTE* const dictBase = ms->window.dictBase;
    const BYTE* const dictEnd  = dictBase + dictLimit;
    const BYTE* const dictStart  = dictBase + ms->window.lowLimit;
    const U32 windowLog = ms->cParams.windowLog;
    const U32 mls = BOUNDED(4, ms->cParams.minMatch, 6);
    const U32 rowLog = BOUNDED(4, ms->cParams.searchLog, 6);

    U32 offset_1 = rep[0], offset_2 = rep[1];

    DEBUGLOG(5, "ZSTD_compressBlock_lazy_extDict_generic (searchFunc=%u)", (U32)searchMethod);

    /* init */
    ip += (ip == prefixStart);
    if (searchMethod == search_rowHash) {
        ZSTD_row_fillHashCache(ms, base, rowLog,
                               MIN(ms->cParams.minMatch, 6 /* mls caps out at 6 */),
                               ms->nextToUpdate, ilimit);
    }

    /* Match Loop */
#if defined(__x86_64__)
    /* I've measured random a 5% speed loss on levels 5 & 6 (greedy) when the
     * code alignment is perturbed. To fix the instability align the loop on 32-bytes.
     */
    __asm__(".p2align 5");
#endif
    while (ip < ilimit) {
        size_t matchLength=0;
        size_t offcode=STORE_REPCODE_1;
        const BYTE* start=ip+1;
        U32 curr = (U32)(ip-base);

        /* check repCode */
        {   const U32 windowLow = ZSTD_getLowestMatchIndex(ms, curr+1, windowLog);
            const U32 repIndex = (U32)(curr+1 - offset_1);
            const BYTE* const repBase = repIndex < dictLimit ? dictBase : base;
            const BYTE* const repMatch = repBase + repIndex;
            if ( ((U32)((dictLimit-1) - repIndex) >= 3) /* intentional overflow */
               & (offset_1 <= curr+1 - windowLow) ) /* note: we are searching at curr+1 */
            if (MEM_read32(ip+1) == MEM_read32(repMatch)) {
                /* repcode detected we should take it */
                const BYTE* const repEnd = repIndex < dictLimit ? dictEnd : iend;
                matchLength = ZSTD_count_2segments(ip+1+4, repMatch+4, iend, repEnd, prefixStart) + 4;
                if (depth==0) goto _storeSequence;
        }   }

        /* first search (depth 0) */
        {   size_t offsetFound = 999999999;
            size_t const ml2 = ZSTD_searchMax(ms, ip, iend, &offsetFound, mls, rowLog, searchMethod, ZSTD_extDict);
            if (ml2 > matchLength)
                matchLength = ml2, start = ip, offcode=offsetFound;
        }

        if (matchLength < 4) {
            ip += ((ip-anchor) >> kSearchStrength) + 1;   /* jump faster over incompressible sections */
            continue;
        }

        /* let's try to find a better solution */
        if (depth>=1)
        while (ip<ilimit) {
            ip ++;
            curr++;
            /* check repCode */
            if (offcode) {
                const U32 windowLow = ZSTD_getLowestMatchIndex(ms, curr, windowLog);
                const U32 repIndex = (U32)(curr - offset_1);
                const BYTE* const repBase = repIndex < dictLimit ? dictBase : base;
                const BYTE* const repMatch = repBase + repIndex;
                if ( ((U32)((dictLimit-1) - repIndex) >= 3) /* intentional overflow : do not test positions overlapping 2 memory segments  */
                   & (offset_1 <= curr - windowLow) ) /* equivalent to `curr > repIndex >= windowLow` */
                if (MEM_read32(ip) == MEM_read32(repMatch)) {
                    /* repcode detected */
                    const BYTE* const repEnd = repIndex < dictLimit ? dictEnd : iend;
                    size_t const repLength = ZSTD_count_2segments(ip+4, repMatch+4, iend, repEnd, prefixStart) + 4;
                    int const gain2 = (int)(repLength * 3);
                    int const gain1 = (int)(matchLength*3 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offcode)) + 1);
                    if ((repLength >= 4) && (gain2 > gain1))
                        matchLength = repLength, offcode = STORE_REPCODE_1, start = ip;
            }   }

            /* search match, depth 1 */
            {   size_t offset2=999999999;
                size_t const ml2 = ZSTD_searchMax(ms, ip, iend, &offset2, mls, rowLog, searchMethod, ZSTD_extDict);
                int const gain2 = (int)(ml2*4 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offset2)));   /* raw approx */
                int const gain1 = (int)(matchLength*4 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offcode)) + 4);
                if ((ml2 >= 4) && (gain2 > gain1)) {
                    matchLength = ml2, offcode = offset2, start = ip;
                    continue;   /* search a better one */
            }   }

            /* let's find an even better one */
            if ((depth==2) && (ip<ilimit)) {
                ip ++;
                curr++;
                /* check repCode */
                if (offcode) {
                    const U32 windowLow = ZSTD_getLowestMatchIndex(ms, curr, windowLog);
                    const U32 repIndex = (U32)(curr - offset_1);
                    const BYTE* const repBase = repIndex < dictLimit ? dictBase : base;
                    const BYTE* const repMatch = repBase + repIndex;
                    if ( ((U32)((dictLimit-1) - repIndex) >= 3) /* intentional overflow : do not test positions overlapping 2 memory segments  */
                       & (offset_1 <= curr - windowLow) ) /* equivalent to `curr > repIndex >= windowLow` */
                    if (MEM_read32(ip) == MEM_read32(repMatch)) {
                        /* repcode detected */
                        const BYTE* const repEnd = repIndex < dictLimit ? dictEnd : iend;
                        size_t const repLength = ZSTD_count_2segments(ip+4, repMatch+4, iend, repEnd, prefixStart) + 4;
                        int const gain2 = (int)(repLength * 4);
                        int const gain1 = (int)(matchLength*4 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offcode)) + 1);
                        if ((repLength >= 4) && (gain2 > gain1))
                            matchLength = repLength, offcode = STORE_REPCODE_1, start = ip;
                }   }

                /* search match, depth 2 */
                {   size_t offset2=999999999;
                    size_t const ml2 = ZSTD_searchMax(ms, ip, iend, &offset2, mls, rowLog, searchMethod, ZSTD_extDict);
                    int const gain2 = (int)(ml2*4 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offset2)));   /* raw approx */
                    int const gain1 = (int)(matchLength*4 - ZSTD_highbit32((U32)STORED_TO_OFFBASE(offcode)) + 7);
                    if ((ml2 >= 4) && (gain2 > gain1)) {
                        matchLength = ml2, offcode = offset2, start = ip;
                        continue;
            }   }   }
            break;  /* nothing found : store previous solution */
        }

        /* catch up */
        if (STORED_IS_OFFSET(offcode)) {
            U32 const matchIndex = (U32)((size_t)(start-base) - STORED_OFFSET(offcode));
            const BYTE* match = (matchIndex < dictLimit) ? dictBase + matchIndex : base + matchIndex;
            const BYTE* const mStart = (matchIndex < dictLimit) ? dictStart : prefixStart;
            while ((start>anchor) && (match>mStart) && (start[-1] == match[-1])) { start--; match--; matchLength++; }  /* catch up */
            offset_2 = offset_1; offset_1 = (U32)STORED_OFFSET(offcode);
        }

        /* store sequence */
_storeSequence:
        {   size_t const litLength = (size_t)(start - anchor);
            ZSTD_storeSeq(seqStore, litLength, anchor, iend, (U32)offcode, matchLength);
            anchor = ip = start + matchLength;
        }

        /* check immediate repcode */
        while (ip <= ilimit) {
            const U32 repCurrent = (U32)(ip-base);
            const U32 windowLow = ZSTD_getLowestMatchIndex(ms, repCurrent, windowLog);
            const U32 repIndex = repCurrent - offset_2;
            const BYTE* const repBase = repIndex < dictLimit ? dictBase : base;
            const BYTE* const repMatch = repBase + repIndex;
            if ( ((U32)((dictLimit-1) - repIndex) >= 3) /* intentional overflow : do not test positions overlapping 2 memory segments  */
               & (offset_2 <= repCurrent - windowLow) ) /* equivalent to `curr > repIndex >= windowLow` */
            if (MEM_read32(ip) == MEM_read32(repMatch)) {
                /* repcode detected we should take it */
                const BYTE* const repEnd = repIndex < dictLimit ? dictEnd : iend;
                matchLength = ZSTD_count_2segments(ip+4, repMatch+4, iend, repEnd, prefixStart) + 4;
                offcode = offset_2; offset_2 = offset_1; offset_1 = (U32)offcode;   /* swap offset history */
                ZSTD_storeSeq(seqStore, 0, anchor, iend, STORE_REPCODE_1, matchLength);
                ip += matchLength;
                anchor = ip;
                continue;   /* faster when present ... (?) */
            }
            break;
    }   }

    /* Save reps for next block */
    rep[0] = offset_1;
    rep[1] = offset_2;

    /* Return the last literals size */
    return (size_t)(iend - anchor);
}


size_t ZSTD_compressBlock_greedy_extDict(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_extDict_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 0);
}

size_t ZSTD_compressBlock_lazy_extDict(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)

{
    return ZSTD_compressBlock_lazy_extDict_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 1);
}

size_t ZSTD_compressBlock_lazy2_extDict(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)

{
    return ZSTD_compressBlock_lazy_extDict_generic(ms, seqStore, rep, src, srcSize, search_hashChain, 2);
}

size_t ZSTD_compressBlock_btlazy2_extDict(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)

{
    return ZSTD_compressBlock_lazy_extDict_generic(ms, seqStore, rep, src, srcSize, search_binaryTree, 2);
}

size_t ZSTD_compressBlock_greedy_extDict_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    return ZSTD_compressBlock_lazy_extDict_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 0);
}

size_t ZSTD_compressBlock_lazy_extDict_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)

{
    return ZSTD_compressBlock_lazy_extDict_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 1);
}

size_t ZSTD_compressBlock_lazy2_extDict_row(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)

{
    return ZSTD_compressBlock_lazy_extDict_generic(ms, seqStore, rep, src, srcSize, search_rowHash, 2);
}
