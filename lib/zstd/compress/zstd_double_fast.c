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
#include "zstd_double_fast.h"


void ZSTD_fillDoubleHashTable(ZSTD_matchState_t* ms,
                              void const* end, ZSTD_dictTableLoadMethod_e dtlm)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    U32* const hashLarge = ms->hashTable;
    U32  const hBitsL = cParams->hashLog;
    U32  const mls = cParams->minMatch;
    U32* const hashSmall = ms->chainTable;
    U32  const hBitsS = cParams->chainLog;
    const BYTE* const base = ms->window.base;
    const BYTE* ip = base + ms->nextToUpdate;
    const BYTE* const iend = ((const BYTE*)end) - HASH_READ_SIZE;
    const U32 fastHashFillStep = 3;

    /* Always insert every fastHashFillStep position into the hash tables.
     * Insert the other positions into the large hash table if their entry
     * is empty.
     */
    for (; ip + fastHashFillStep - 1 <= iend; ip += fastHashFillStep) {
        U32 const curr = (U32)(ip - base);
        U32 i;
        for (i = 0; i < fastHashFillStep; ++i) {
            size_t const smHash = ZSTD_hashPtr(ip + i, hBitsS, mls);
            size_t const lgHash = ZSTD_hashPtr(ip + i, hBitsL, 8);
            if (i == 0)
                hashSmall[smHash] = curr + i;
            if (i == 0 || hashLarge[lgHash] == 0)
                hashLarge[lgHash] = curr + i;
            /* Only load extra positions for ZSTD_dtlm_full */
            if (dtlm == ZSTD_dtlm_fast)
                break;
    }   }
}


FORCE_INLINE_TEMPLATE
size_t ZSTD_compressBlock_doubleFast_generic(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize,
        U32 const mls /* template */, ZSTD_dictMode_e const dictMode)
{
    ZSTD_compressionParameters const* cParams = &ms->cParams;
    U32* const hashLong = ms->hashTable;
    const U32 hBitsL = cParams->hashLog;
    U32* const hashSmall = ms->chainTable;
    const U32 hBitsS = cParams->chainLog;
    const BYTE* const base = ms->window.base;
    const BYTE* const istart = (const BYTE*)src;
    const BYTE* ip = istart;
    const BYTE* anchor = istart;
    const U32 endIndex = (U32)((size_t)(istart - base) + srcSize);
    /* presumes that, if there is a dictionary, it must be using Attach mode */
    const U32 prefixLowestIndex = ZSTD_getLowestPrefixIndex(ms, endIndex, cParams->windowLog);
    const BYTE* const prefixLowest = base + prefixLowestIndex;
    const BYTE* const iend = istart + srcSize;
    const BYTE* const ilimit = iend - HASH_READ_SIZE;
    U32 offset_1=rep[0], offset_2=rep[1];
    U32 offsetSaved = 0;

    const ZSTD_matchState_t* const dms = ms->dictMatchState;
    const ZSTD_compressionParameters* const dictCParams =
                                     dictMode == ZSTD_dictMatchState ?
                                     &dms->cParams : NULL;
    const U32* const dictHashLong  = dictMode == ZSTD_dictMatchState ?
                                     dms->hashTable : NULL;
    const U32* const dictHashSmall = dictMode == ZSTD_dictMatchState ?
                                     dms->chainTable : NULL;
    const U32 dictStartIndex       = dictMode == ZSTD_dictMatchState ?
                                     dms->window.dictLimit : 0;
    const BYTE* const dictBase     = dictMode == ZSTD_dictMatchState ?
                                     dms->window.base : NULL;
    const BYTE* const dictStart    = dictMode == ZSTD_dictMatchState ?
                                     dictBase + dictStartIndex : NULL;
    const BYTE* const dictEnd      = dictMode == ZSTD_dictMatchState ?
                                     dms->window.nextSrc : NULL;
    const U32 dictIndexDelta       = dictMode == ZSTD_dictMatchState ?
                                     prefixLowestIndex - (U32)(dictEnd - dictBase) :
                                     0;
    const U32 dictHBitsL           = dictMode == ZSTD_dictMatchState ?
                                     dictCParams->hashLog : hBitsL;
    const U32 dictHBitsS           = dictMode == ZSTD_dictMatchState ?
                                     dictCParams->chainLog : hBitsS;
    const U32 dictAndPrefixLength  = (U32)((ip - prefixLowest) + (dictEnd - dictStart));

    DEBUGLOG(5, "ZSTD_compressBlock_doubleFast_generic");

    assert(dictMode == ZSTD_noDict || dictMode == ZSTD_dictMatchState);

    /* if a dictionary is attached, it must be within window range */
    if (dictMode == ZSTD_dictMatchState) {
        assert(ms->window.dictLimit + (1U << cParams->windowLog) >= endIndex);
    }

    /* init */
    ip += (dictAndPrefixLength == 0);
    if (dictMode == ZSTD_noDict) {
        U32 const curr = (U32)(ip - base);
        U32 const windowLow = ZSTD_getLowestPrefixIndex(ms, curr, cParams->windowLog);
        U32 const maxRep = curr - windowLow;
        if (offset_2 > maxRep) offsetSaved = offset_2, offset_2 = 0;
        if (offset_1 > maxRep) offsetSaved = offset_1, offset_1 = 0;
    }
    if (dictMode == ZSTD_dictMatchState) {
        /* dictMatchState repCode checks don't currently handle repCode == 0
         * disabling. */
        assert(offset_1 <= dictAndPrefixLength);
        assert(offset_2 <= dictAndPrefixLength);
    }

    /* Main Search Loop */
    while (ip < ilimit) {   /* < instead of <=, because repcode check at (ip+1) */
        size_t mLength;
        U32 offset;
        size_t const h2 = ZSTD_hashPtr(ip, hBitsL, 8);
        size_t const h = ZSTD_hashPtr(ip, hBitsS, mls);
        size_t const dictHL = ZSTD_hashPtr(ip, dictHBitsL, 8);
        size_t const dictHS = ZSTD_hashPtr(ip, dictHBitsS, mls);
        U32 const curr = (U32)(ip-base);
        U32 const matchIndexL = hashLong[h2];
        U32 matchIndexS = hashSmall[h];
        const BYTE* matchLong = base + matchIndexL;
        const BYTE* match = base + matchIndexS;
        const U32 repIndex = curr + 1 - offset_1;
        const BYTE* repMatch = (dictMode == ZSTD_dictMatchState
                            && repIndex < prefixLowestIndex) ?
                               dictBase + (repIndex - dictIndexDelta) :
                               base + repIndex;
        hashLong[h2] = hashSmall[h] = curr;   /* update hash tables */

        /* check dictMatchState repcode */
        if (dictMode == ZSTD_dictMatchState
            && ((U32)((prefixLowestIndex-1) - repIndex) >= 3 /* intentional underflow */)
            && (MEM_read32(repMatch) == MEM_read32(ip+1)) ) {
            const BYTE* repMatchEnd = repIndex < prefixLowestIndex ? dictEnd : iend;
            mLength = ZSTD_count_2segments(ip+1+4, repMatch+4, iend, repMatchEnd, prefixLowest) + 4;
            ip++;
            ZSTD_storeSeq(seqStore, (size_t)(ip-anchor), anchor, iend, 0, mLength-MINMATCH);
            goto _match_stored;
        }

        /* check noDict repcode */
        if ( dictMode == ZSTD_noDict
          && ((offset_1 > 0) & (MEM_read32(ip+1-offset_1) == MEM_read32(ip+1)))) {
            mLength = ZSTD_count(ip+1+4, ip+1+4-offset_1, iend) + 4;
            ip++;
            ZSTD_storeSeq(seqStore, (size_t)(ip-anchor), anchor, iend, 0, mLength-MINMATCH);
            goto _match_stored;
        }

        if (matchIndexL > prefixLowestIndex) {
            /* check prefix long match */
            if (MEM_read64(matchLong) == MEM_read64(ip)) {
                mLength = ZSTD_count(ip+8, matchLong+8, iend) + 8;
                offset = (U32)(ip-matchLong);
                while (((ip>anchor) & (matchLong>prefixLowest)) && (ip[-1] == matchLong[-1])) { ip--; matchLong--; mLength++; } /* catch up */
                goto _match_found;
            }
        } else if (dictMode == ZSTD_dictMatchState) {
            /* check dictMatchState long match */
            U32 const dictMatchIndexL = dictHashLong[dictHL];
            const BYTE* dictMatchL = dictBase + dictMatchIndexL;
            assert(dictMatchL < dictEnd);

            if (dictMatchL > dictStart && MEM_read64(dictMatchL) == MEM_read64(ip)) {
                mLength = ZSTD_count_2segments(ip+8, dictMatchL+8, iend, dictEnd, prefixLowest) + 8;
                offset = (U32)(curr - dictMatchIndexL - dictIndexDelta);
                while (((ip>anchor) & (dictMatchL>dictStart)) && (ip[-1] == dictMatchL[-1])) { ip--; dictMatchL--; mLength++; } /* catch up */
                goto _match_found;
        }   }

        if (matchIndexS > prefixLowestIndex) {
            /* check prefix short match */
            if (MEM_read32(match) == MEM_read32(ip)) {
                goto _search_next_long;
            }
        } else if (dictMode == ZSTD_dictMatchState) {
            /* check dictMatchState short match */
            U32 const dictMatchIndexS = dictHashSmall[dictHS];
            match = dictBase + dictMatchIndexS;
            matchIndexS = dictMatchIndexS + dictIndexDelta;

            if (match > dictStart && MEM_read32(match) == MEM_read32(ip)) {
                goto _search_next_long;
        }   }

        ip += ((ip-anchor) >> kSearchStrength) + 1;
#if defined(__aarch64__)
        PREFETCH_L1(ip+256);
#endif
        continue;

_search_next_long:

        {   size_t const hl3 = ZSTD_hashPtr(ip+1, hBitsL, 8);
            size_t const dictHLNext = ZSTD_hashPtr(ip+1, dictHBitsL, 8);
            U32 const matchIndexL3 = hashLong[hl3];
            const BYTE* matchL3 = base + matchIndexL3;
            hashLong[hl3] = curr + 1;

            /* check prefix long +1 match */
            if (matchIndexL3 > prefixLowestIndex) {
                if (MEM_read64(matchL3) == MEM_read64(ip+1)) {
                    mLength = ZSTD_count(ip+9, matchL3+8, iend) + 8;
                    ip++;
                    offset = (U32)(ip-matchL3);
                    while (((ip>anchor) & (matchL3>prefixLowest)) && (ip[-1] == matchL3[-1])) { ip--; matchL3--; mLength++; } /* catch up */
                    goto _match_found;
                }
            } else if (dictMode == ZSTD_dictMatchState) {
                /* check dict long +1 match */
                U32 const dictMatchIndexL3 = dictHashLong[dictHLNext];
                const BYTE* dictMatchL3 = dictBase + dictMatchIndexL3;
                assert(dictMatchL3 < dictEnd);
                if (dictMatchL3 > dictStart && MEM_read64(dictMatchL3) == MEM_read64(ip+1)) {
                    mLength = ZSTD_count_2segments(ip+1+8, dictMatchL3+8, iend, dictEnd, prefixLowest) + 8;
                    ip++;
                    offset = (U32)(curr + 1 - dictMatchIndexL3 - dictIndexDelta);
                    while (((ip>anchor) & (dictMatchL3>dictStart)) && (ip[-1] == dictMatchL3[-1])) { ip--; dictMatchL3--; mLength++; } /* catch up */
                    goto _match_found;
        }   }   }

        /* if no long +1 match, explore the short match we found */
        if (dictMode == ZSTD_dictMatchState && matchIndexS < prefixLowestIndex) {
            mLength = ZSTD_count_2segments(ip+4, match+4, iend, dictEnd, prefixLowest) + 4;
            offset = (U32)(curr - matchIndexS);
            while (((ip>anchor) & (match>dictStart)) && (ip[-1] == match[-1])) { ip--; match--; mLength++; } /* catch up */
        } else {
            mLength = ZSTD_count(ip+4, match+4, iend) + 4;
            offset = (U32)(ip - match);
            while (((ip>anchor) & (match>prefixLowest)) && (ip[-1] == match[-1])) { ip--; match--; mLength++; } /* catch up */
        }

_match_found:
        offset_2 = offset_1;
        offset_1 = offset;

        ZSTD_storeSeq(seqStore, (size_t)(ip-anchor), anchor, iend, offset + ZSTD_REP_MOVE, mLength-MINMATCH);

_match_stored:
        /* match found */
        ip += mLength;
        anchor = ip;

        if (ip <= ilimit) {
            /* Complementary insertion */
            /* done after iLimit test, as candidates could be > iend-8 */
            {   U32 const indexToInsert = curr+2;
                hashLong[ZSTD_hashPtr(base+indexToInsert, hBitsL, 8)] = indexToInsert;
                hashLong[ZSTD_hashPtr(ip-2, hBitsL, 8)] = (U32)(ip-2-base);
                hashSmall[ZSTD_hashPtr(base+indexToInsert, hBitsS, mls)] = indexToInsert;
                hashSmall[ZSTD_hashPtr(ip-1, hBitsS, mls)] = (U32)(ip-1-base);
            }

            /* check immediate repcode */
            if (dictMode == ZSTD_dictMatchState) {
                while (ip <= ilimit) {
                    U32 const current2 = (U32)(ip-base);
                    U32 const repIndex2 = current2 - offset_2;
                    const BYTE* repMatch2 = dictMode == ZSTD_dictMatchState
                        && repIndex2 < prefixLowestIndex ?
                            dictBase + repIndex2 - dictIndexDelta :
                            base + repIndex2;
                    if ( ((U32)((prefixLowestIndex-1) - (U32)repIndex2) >= 3 /* intentional overflow */)
                       && (MEM_read32(repMatch2) == MEM_read32(ip)) ) {
                        const BYTE* const repEnd2 = repIndex2 < prefixLowestIndex ? dictEnd : iend;
                        size_t const repLength2 = ZSTD_count_2segments(ip+4, repMatch2+4, iend, repEnd2, prefixLowest) + 4;
                        U32 tmpOffset = offset_2; offset_2 = offset_1; offset_1 = tmpOffset;   /* swap offset_2 <=> offset_1 */
                        ZSTD_storeSeq(seqStore, 0, anchor, iend, 0, repLength2-MINMATCH);
                        hashSmall[ZSTD_hashPtr(ip, hBitsS, mls)] = current2;
                        hashLong[ZSTD_hashPtr(ip, hBitsL, 8)] = current2;
                        ip += repLength2;
                        anchor = ip;
                        continue;
                    }
                    break;
            }   }

            if (dictMode == ZSTD_noDict) {
                while ( (ip <= ilimit)
                     && ( (offset_2>0)
                        & (MEM_read32(ip) == MEM_read32(ip - offset_2)) )) {
                    /* store sequence */
                    size_t const rLength = ZSTD_count(ip+4, ip+4-offset_2, iend) + 4;
                    U32 const tmpOff = offset_2; offset_2 = offset_1; offset_1 = tmpOff;  /* swap offset_2 <=> offset_1 */
                    hashSmall[ZSTD_hashPtr(ip, hBitsS, mls)] = (U32)(ip-base);
                    hashLong[ZSTD_hashPtr(ip, hBitsL, 8)] = (U32)(ip-base);
                    ZSTD_storeSeq(seqStore, 0, anchor, iend, 0, rLength-MINMATCH);
                    ip += rLength;
                    anchor = ip;
                    continue;   /* faster when present ... (?) */
        }   }   }
    }   /* while (ip < ilimit) */

    /* save reps for next block */
    rep[0] = offset_1 ? offset_1 : offsetSaved;
    rep[1] = offset_2 ? offset_2 : offsetSaved;

    /* Return the last literals size */
    return (size_t)(iend - anchor);
}


size_t ZSTD_compressBlock_doubleFast(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    const U32 mls = ms->cParams.minMatch;
    switch(mls)
    {
    default: /* includes case 3 */
    case 4 :
        return ZSTD_compressBlock_doubleFast_generic(ms, seqStore, rep, src, srcSize, 4, ZSTD_noDict);
    case 5 :
        return ZSTD_compressBlock_doubleFast_generic(ms, seqStore, rep, src, srcSize, 5, ZSTD_noDict);
    case 6 :
        return ZSTD_compressBlock_doubleFast_generic(ms, seqStore, rep, src, srcSize, 6, ZSTD_noDict);
    case 7 :
        return ZSTD_compressBlock_doubleFast_generic(ms, seqStore, rep, src, srcSize, 7, ZSTD_noDict);
    }
}


size_t ZSTD_compressBlock_doubleFast_dictMatchState(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    const U32 mls = ms->cParams.minMatch;
    switch(mls)
    {
    default: /* includes case 3 */
    case 4 :
        return ZSTD_compressBlock_doubleFast_generic(ms, seqStore, rep, src, srcSize, 4, ZSTD_dictMatchState);
    case 5 :
        return ZSTD_compressBlock_doubleFast_generic(ms, seqStore, rep, src, srcSize, 5, ZSTD_dictMatchState);
    case 6 :
        return ZSTD_compressBlock_doubleFast_generic(ms, seqStore, rep, src, srcSize, 6, ZSTD_dictMatchState);
    case 7 :
        return ZSTD_compressBlock_doubleFast_generic(ms, seqStore, rep, src, srcSize, 7, ZSTD_dictMatchState);
    }
}


static size_t ZSTD_compressBlock_doubleFast_extDict_generic(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize,
        U32 const mls /* template */)
{
    ZSTD_compressionParameters const* cParams = &ms->cParams;
    U32* const hashLong = ms->hashTable;
    U32  const hBitsL = cParams->hashLog;
    U32* const hashSmall = ms->chainTable;
    U32  const hBitsS = cParams->chainLog;
    const BYTE* const istart = (const BYTE*)src;
    const BYTE* ip = istart;
    const BYTE* anchor = istart;
    const BYTE* const iend = istart + srcSize;
    const BYTE* const ilimit = iend - 8;
    const BYTE* const base = ms->window.base;
    const U32   endIndex = (U32)((size_t)(istart - base) + srcSize);
    const U32   lowLimit = ZSTD_getLowestMatchIndex(ms, endIndex, cParams->windowLog);
    const U32   dictStartIndex = lowLimit;
    const U32   dictLimit = ms->window.dictLimit;
    const U32   prefixStartIndex = (dictLimit > lowLimit) ? dictLimit : lowLimit;
    const BYTE* const prefixStart = base + prefixStartIndex;
    const BYTE* const dictBase = ms->window.dictBase;
    const BYTE* const dictStart = dictBase + dictStartIndex;
    const BYTE* const dictEnd = dictBase + prefixStartIndex;
    U32 offset_1=rep[0], offset_2=rep[1];

    DEBUGLOG(5, "ZSTD_compressBlock_doubleFast_extDict_generic (srcSize=%zu)", srcSize);

    /* if extDict is invalidated due to maxDistance, switch to "regular" variant */
    if (prefixStartIndex == dictStartIndex)
        return ZSTD_compressBlock_doubleFast_generic(ms, seqStore, rep, src, srcSize, mls, ZSTD_noDict);

    /* Search Loop */
    while (ip < ilimit) {  /* < instead of <=, because (ip+1) */
        const size_t hSmall = ZSTD_hashPtr(ip, hBitsS, mls);
        const U32 matchIndex = hashSmall[hSmall];
        const BYTE* const matchBase = matchIndex < prefixStartIndex ? dictBase : base;
        const BYTE* match = matchBase + matchIndex;

        const size_t hLong = ZSTD_hashPtr(ip, hBitsL, 8);
        const U32 matchLongIndex = hashLong[hLong];
        const BYTE* const matchLongBase = matchLongIndex < prefixStartIndex ? dictBase : base;
        const BYTE* matchLong = matchLongBase + matchLongIndex;

        const U32 curr = (U32)(ip-base);
        const U32 repIndex = curr + 1 - offset_1;   /* offset_1 expected <= curr +1 */
        const BYTE* const repBase = repIndex < prefixStartIndex ? dictBase : base;
        const BYTE* const repMatch = repBase + repIndex;
        size_t mLength;
        hashSmall[hSmall] = hashLong[hLong] = curr;   /* update hash table */

        if ((((U32)((prefixStartIndex-1) - repIndex) >= 3) /* intentional underflow : ensure repIndex doesn't overlap dict + prefix */
            & (repIndex > dictStartIndex))
          && (MEM_read32(repMatch) == MEM_read32(ip+1)) ) {
            const BYTE* repMatchEnd = repIndex < prefixStartIndex ? dictEnd : iend;
            mLength = ZSTD_count_2segments(ip+1+4, repMatch+4, iend, repMatchEnd, prefixStart) + 4;
            ip++;
            ZSTD_storeSeq(seqStore, (size_t)(ip-anchor), anchor, iend, 0, mLength-MINMATCH);
        } else {
            if ((matchLongIndex > dictStartIndex) && (MEM_read64(matchLong) == MEM_read64(ip))) {
                const BYTE* const matchEnd = matchLongIndex < prefixStartIndex ? dictEnd : iend;
                const BYTE* const lowMatchPtr = matchLongIndex < prefixStartIndex ? dictStart : prefixStart;
                U32 offset;
                mLength = ZSTD_count_2segments(ip+8, matchLong+8, iend, matchEnd, prefixStart) + 8;
                offset = curr - matchLongIndex;
                while (((ip>anchor) & (matchLong>lowMatchPtr)) && (ip[-1] == matchLong[-1])) { ip--; matchLong--; mLength++; }   /* catch up */
                offset_2 = offset_1;
                offset_1 = offset;
                ZSTD_storeSeq(seqStore, (size_t)(ip-anchor), anchor, iend, offset + ZSTD_REP_MOVE, mLength-MINMATCH);

            } else if ((matchIndex > dictStartIndex) && (MEM_read32(match) == MEM_read32(ip))) {
                size_t const h3 = ZSTD_hashPtr(ip+1, hBitsL, 8);
                U32 const matchIndex3 = hashLong[h3];
                const BYTE* const match3Base = matchIndex3 < prefixStartIndex ? dictBase : base;
                const BYTE* match3 = match3Base + matchIndex3;
                U32 offset;
                hashLong[h3] = curr + 1;
                if ( (matchIndex3 > dictStartIndex) && (MEM_read64(match3) == MEM_read64(ip+1)) ) {
                    const BYTE* const matchEnd = matchIndex3 < prefixStartIndex ? dictEnd : iend;
                    const BYTE* const lowMatchPtr = matchIndex3 < prefixStartIndex ? dictStart : prefixStart;
                    mLength = ZSTD_count_2segments(ip+9, match3+8, iend, matchEnd, prefixStart) + 8;
                    ip++;
                    offset = curr+1 - matchIndex3;
                    while (((ip>anchor) & (match3>lowMatchPtr)) && (ip[-1] == match3[-1])) { ip--; match3--; mLength++; } /* catch up */
                } else {
                    const BYTE* const matchEnd = matchIndex < prefixStartIndex ? dictEnd : iend;
                    const BYTE* const lowMatchPtr = matchIndex < prefixStartIndex ? dictStart : prefixStart;
                    mLength = ZSTD_count_2segments(ip+4, match+4, iend, matchEnd, prefixStart) + 4;
                    offset = curr - matchIndex;
                    while (((ip>anchor) & (match>lowMatchPtr)) && (ip[-1] == match[-1])) { ip--; match--; mLength++; }   /* catch up */
                }
                offset_2 = offset_1;
                offset_1 = offset;
                ZSTD_storeSeq(seqStore, (size_t)(ip-anchor), anchor, iend, offset + ZSTD_REP_MOVE, mLength-MINMATCH);

            } else {
                ip += ((ip-anchor) >> kSearchStrength) + 1;
                continue;
        }   }

        /* move to next sequence start */
        ip += mLength;
        anchor = ip;

        if (ip <= ilimit) {
            /* Complementary insertion */
            /* done after iLimit test, as candidates could be > iend-8 */
            {   U32 const indexToInsert = curr+2;
                hashLong[ZSTD_hashPtr(base+indexToInsert, hBitsL, 8)] = indexToInsert;
                hashLong[ZSTD_hashPtr(ip-2, hBitsL, 8)] = (U32)(ip-2-base);
                hashSmall[ZSTD_hashPtr(base+indexToInsert, hBitsS, mls)] = indexToInsert;
                hashSmall[ZSTD_hashPtr(ip-1, hBitsS, mls)] = (U32)(ip-1-base);
            }

            /* check immediate repcode */
            while (ip <= ilimit) {
                U32 const current2 = (U32)(ip-base);
                U32 const repIndex2 = current2 - offset_2;
                const BYTE* repMatch2 = repIndex2 < prefixStartIndex ? dictBase + repIndex2 : base + repIndex2;
                if ( (((U32)((prefixStartIndex-1) - repIndex2) >= 3)   /* intentional overflow : ensure repIndex2 doesn't overlap dict + prefix */
                    & (repIndex2 > dictStartIndex))
                  && (MEM_read32(repMatch2) == MEM_read32(ip)) ) {
                    const BYTE* const repEnd2 = repIndex2 < prefixStartIndex ? dictEnd : iend;
                    size_t const repLength2 = ZSTD_count_2segments(ip+4, repMatch2+4, iend, repEnd2, prefixStart) + 4;
                    U32 const tmpOffset = offset_2; offset_2 = offset_1; offset_1 = tmpOffset;   /* swap offset_2 <=> offset_1 */
                    ZSTD_storeSeq(seqStore, 0, anchor, iend, 0, repLength2-MINMATCH);
                    hashSmall[ZSTD_hashPtr(ip, hBitsS, mls)] = current2;
                    hashLong[ZSTD_hashPtr(ip, hBitsL, 8)] = current2;
                    ip += repLength2;
                    anchor = ip;
                    continue;
                }
                break;
    }   }   }

    /* save reps for next block */
    rep[0] = offset_1;
    rep[1] = offset_2;

    /* Return the last literals size */
    return (size_t)(iend - anchor);
}


size_t ZSTD_compressBlock_doubleFast_extDict(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    U32 const mls = ms->cParams.minMatch;
    switch(mls)
    {
    default: /* includes case 3 */
    case 4 :
        return ZSTD_compressBlock_doubleFast_extDict_generic(ms, seqStore, rep, src, srcSize, 4);
    case 5 :
        return ZSTD_compressBlock_doubleFast_extDict_generic(ms, seqStore, rep, src, srcSize, 5);
    case 6 :
        return ZSTD_compressBlock_doubleFast_extDict_generic(ms, seqStore, rep, src, srcSize, 6);
    case 7 :
        return ZSTD_compressBlock_doubleFast_extDict_generic(ms, seqStore, rep, src, srcSize, 7);
    }
}
