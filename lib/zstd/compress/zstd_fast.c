/*
 * Copyright (c) Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include "zstd_compress_internal.h"  /* ZSTD_hashPtr, ZSTD_count, ZSTD_storeSeq */
#include "zstd_fast.h"


void ZSTD_fillHashTable(ZSTD_matchState_t* ms,
                        const void* const end,
                        ZSTD_dictTableLoadMethod_e dtlm)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    U32* const hashTable = ms->hashTable;
    U32  const hBits = cParams->hashLog;
    U32  const mls = cParams->minMatch;
    const BYTE* const base = ms->window.base;
    const BYTE* ip = base + ms->nextToUpdate;
    const BYTE* const iend = ((const BYTE*)end) - HASH_READ_SIZE;
    const U32 fastHashFillStep = 3;

    /* Always insert every fastHashFillStep position into the hash table.
     * Insert the other positions if their hash entry is empty.
     */
    for ( ; ip + fastHashFillStep < iend + 2; ip += fastHashFillStep) {
        U32 const curr = (U32)(ip - base);
        size_t const hash0 = ZSTD_hashPtr(ip, hBits, mls);
        hashTable[hash0] = curr;
        if (dtlm == ZSTD_dtlm_fast) continue;
        /* Only load extra positions for ZSTD_dtlm_full */
        {   U32 p;
            for (p = 1; p < fastHashFillStep; ++p) {
                size_t const hash = ZSTD_hashPtr(ip + p, hBits, mls);
                if (hashTable[hash] == 0) {  /* not yet filled */
                    hashTable[hash] = curr + p;
    }   }   }   }
}


FORCE_INLINE_TEMPLATE size_t
ZSTD_compressBlock_fast_generic(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize,
        U32 const mls)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    U32* const hashTable = ms->hashTable;
    U32 const hlog = cParams->hashLog;
    /* support stepSize of 0 */
    size_t const stepSize = cParams->targetLength + !(cParams->targetLength) + 1;
    const BYTE* const base = ms->window.base;
    const BYTE* const istart = (const BYTE*)src;
    /* We check ip0 (ip + 0) and ip1 (ip + 1) each loop */
    const BYTE* ip0 = istart;
    const BYTE* ip1;
    const BYTE* anchor = istart;
    const U32   endIndex = (U32)((size_t)(istart - base) + srcSize);
    const U32   prefixStartIndex = ZSTD_getLowestPrefixIndex(ms, endIndex, cParams->windowLog);
    const BYTE* const prefixStart = base + prefixStartIndex;
    const BYTE* const iend = istart + srcSize;
    const BYTE* const ilimit = iend - HASH_READ_SIZE;
    U32 offset_1=rep[0], offset_2=rep[1];
    U32 offsetSaved = 0;

    /* init */
    DEBUGLOG(5, "ZSTD_compressBlock_fast_generic");
    ip0 += (ip0 == prefixStart);
    ip1 = ip0 + 1;
    {   U32 const curr = (U32)(ip0 - base);
        U32 const windowLow = ZSTD_getLowestPrefixIndex(ms, curr, cParams->windowLog);
        U32 const maxRep = curr - windowLow;
        if (offset_2 > maxRep) offsetSaved = offset_2, offset_2 = 0;
        if (offset_1 > maxRep) offsetSaved = offset_1, offset_1 = 0;
    }

    /* Main Search Loop */
#ifdef __INTEL_COMPILER
    /* From intel 'The vector pragma indicates that the loop should be
     * vectorized if it is legal to do so'. Can be used together with
     * #pragma ivdep (but have opted to exclude that because intel
     * warns against using it).*/
    #pragma vector always
#endif
    while (ip1 < ilimit) {   /* < instead of <=, because check at ip0+2 */
        size_t mLength;
        BYTE const* ip2 = ip0 + 2;
        size_t const h0 = ZSTD_hashPtr(ip0, hlog, mls);
        U32 const val0 = MEM_read32(ip0);
        size_t const h1 = ZSTD_hashPtr(ip1, hlog, mls);
        U32 const val1 = MEM_read32(ip1);
        U32 const current0 = (U32)(ip0-base);
        U32 const current1 = (U32)(ip1-base);
        U32 const matchIndex0 = hashTable[h0];
        U32 const matchIndex1 = hashTable[h1];
        BYTE const* repMatch = ip2 - offset_1;
        const BYTE* match0 = base + matchIndex0;
        const BYTE* match1 = base + matchIndex1;
        U32 offcode;

#if defined(__aarch64__)
        PREFETCH_L1(ip0+256);
#endif

        hashTable[h0] = current0;   /* update hash table */
        hashTable[h1] = current1;   /* update hash table */

        assert(ip0 + 1 == ip1);

        if ((offset_1 > 0) & (MEM_read32(repMatch) == MEM_read32(ip2))) {
            mLength = (ip2[-1] == repMatch[-1]) ? 1 : 0;
            ip0 = ip2 - mLength;
            match0 = repMatch - mLength;
            mLength += 4;
            offcode = 0;
            goto _match;
        }
        if ((matchIndex0 > prefixStartIndex) && MEM_read32(match0) == val0) {
            /* found a regular match */
            goto _offset;
        }
        if ((matchIndex1 > prefixStartIndex) && MEM_read32(match1) == val1) {
            /* found a regular match after one literal */
            ip0 = ip1;
            match0 = match1;
            goto _offset;
        }
        {   size_t const step = ((size_t)(ip0-anchor) >> (kSearchStrength - 1)) + stepSize;
            assert(step >= 2);
            ip0 += step;
            ip1 += step;
            continue;
        }
_offset: /* Requires: ip0, match0 */
        /* Compute the offset code */
        offset_2 = offset_1;
        offset_1 = (U32)(ip0-match0);
        offcode = offset_1 + ZSTD_REP_MOVE;
        mLength = 4;
        /* Count the backwards match length */
        while (((ip0>anchor) & (match0>prefixStart))
             && (ip0[-1] == match0[-1])) { ip0--; match0--; mLength++; } /* catch up */

_match: /* Requires: ip0, match0, offcode */
        /* Count the forward length */
        mLength += ZSTD_count(ip0+mLength, match0+mLength, iend);
        ZSTD_storeSeq(seqStore, (size_t)(ip0-anchor), anchor, iend, offcode, mLength-MINMATCH);
        /* match found */
        ip0 += mLength;
        anchor = ip0;

        if (ip0 <= ilimit) {
            /* Fill Table */
            assert(base+current0+2 > istart);  /* check base overflow */
            hashTable[ZSTD_hashPtr(base+current0+2, hlog, mls)] = current0+2;  /* here because current+2 could be > iend-8 */
            hashTable[ZSTD_hashPtr(ip0-2, hlog, mls)] = (U32)(ip0-2-base);

            if (offset_2 > 0) { /* offset_2==0 means offset_2 is invalidated */
                while ( (ip0 <= ilimit) && (MEM_read32(ip0) == MEM_read32(ip0 - offset_2)) ) {
                    /* store sequence */
                    size_t const rLength = ZSTD_count(ip0+4, ip0+4-offset_2, iend) + 4;
                    { U32 const tmpOff = offset_2; offset_2 = offset_1; offset_1 = tmpOff; } /* swap offset_2 <=> offset_1 */
                    hashTable[ZSTD_hashPtr(ip0, hlog, mls)] = (U32)(ip0-base);
                    ip0 += rLength;
                    ZSTD_storeSeq(seqStore, 0 /*litLen*/, anchor, iend, 0 /*offCode*/, rLength-MINMATCH);
                    anchor = ip0;
                    continue;   /* faster when present (confirmed on gcc-8) ... (?) */
        }   }   }
        ip1 = ip0 + 1;
    }

    /* save reps for next block */
    rep[0] = offset_1 ? offset_1 : offsetSaved;
    rep[1] = offset_2 ? offset_2 : offsetSaved;

    /* Return the last literals size */
    return (size_t)(iend - anchor);
}


size_t ZSTD_compressBlock_fast(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    U32 const mls = ms->cParams.minMatch;
    assert(ms->dictMatchState == NULL);
    switch(mls)
    {
    default: /* includes case 3 */
    case 4 :
        return ZSTD_compressBlock_fast_generic(ms, seqStore, rep, src, srcSize, 4);
    case 5 :
        return ZSTD_compressBlock_fast_generic(ms, seqStore, rep, src, srcSize, 5);
    case 6 :
        return ZSTD_compressBlock_fast_generic(ms, seqStore, rep, src, srcSize, 6);
    case 7 :
        return ZSTD_compressBlock_fast_generic(ms, seqStore, rep, src, srcSize, 7);
    }
}

FORCE_INLINE_TEMPLATE
size_t ZSTD_compressBlock_fast_dictMatchState_generic(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize, U32 const mls)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    U32* const hashTable = ms->hashTable;
    U32 const hlog = cParams->hashLog;
    /* support stepSize of 0 */
    U32 const stepSize = cParams->targetLength + !(cParams->targetLength);
    const BYTE* const base = ms->window.base;
    const BYTE* const istart = (const BYTE*)src;
    const BYTE* ip = istart;
    const BYTE* anchor = istart;
    const U32   prefixStartIndex = ms->window.dictLimit;
    const BYTE* const prefixStart = base + prefixStartIndex;
    const BYTE* const iend = istart + srcSize;
    const BYTE* const ilimit = iend - HASH_READ_SIZE;
    U32 offset_1=rep[0], offset_2=rep[1];
    U32 offsetSaved = 0;

    const ZSTD_matchState_t* const dms = ms->dictMatchState;
    const ZSTD_compressionParameters* const dictCParams = &dms->cParams ;
    const U32* const dictHashTable = dms->hashTable;
    const U32 dictStartIndex       = dms->window.dictLimit;
    const BYTE* const dictBase     = dms->window.base;
    const BYTE* const dictStart    = dictBase + dictStartIndex;
    const BYTE* const dictEnd      = dms->window.nextSrc;
    const U32 dictIndexDelta       = prefixStartIndex - (U32)(dictEnd - dictBase);
    const U32 dictAndPrefixLength  = (U32)(ip - prefixStart + dictEnd - dictStart);
    const U32 dictHLog             = dictCParams->hashLog;

    /* if a dictionary is still attached, it necessarily means that
     * it is within window size. So we just check it. */
    const U32 maxDistance = 1U << cParams->windowLog;
    const U32 endIndex = (U32)((size_t)(ip - base) + srcSize);
    assert(endIndex - prefixStartIndex <= maxDistance);
    (void)maxDistance; (void)endIndex;   /* these variables are not used when assert() is disabled */

    /* ensure there will be no underflow
     * when translating a dict index into a local index */
    assert(prefixStartIndex >= (U32)(dictEnd - dictBase));

    /* init */
    DEBUGLOG(5, "ZSTD_compressBlock_fast_dictMatchState_generic");
    ip += (dictAndPrefixLength == 0);
    /* dictMatchState repCode checks don't currently handle repCode == 0
     * disabling. */
    assert(offset_1 <= dictAndPrefixLength);
    assert(offset_2 <= dictAndPrefixLength);

    /* Main Search Loop */
    while (ip < ilimit) {   /* < instead of <=, because repcode check at (ip+1) */
        size_t mLength;
        size_t const h = ZSTD_hashPtr(ip, hlog, mls);
        U32 const curr = (U32)(ip-base);
        U32 const matchIndex = hashTable[h];
        const BYTE* match = base + matchIndex;
        const U32 repIndex = curr + 1 - offset_1;
        const BYTE* repMatch = (repIndex < prefixStartIndex) ?
                               dictBase + (repIndex - dictIndexDelta) :
                               base + repIndex;
        hashTable[h] = curr;   /* update hash table */

        if ( ((U32)((prefixStartIndex-1) - repIndex) >= 3) /* intentional underflow : ensure repIndex isn't overlapping dict + prefix */
          && (MEM_read32(repMatch) == MEM_read32(ip+1)) ) {
            const BYTE* const repMatchEnd = repIndex < prefixStartIndex ? dictEnd : iend;
            mLength = ZSTD_count_2segments(ip+1+4, repMatch+4, iend, repMatchEnd, prefixStart) + 4;
            ip++;
            ZSTD_storeSeq(seqStore, (size_t)(ip-anchor), anchor, iend, 0, mLength-MINMATCH);
        } else if ( (matchIndex <= prefixStartIndex) ) {
            size_t const dictHash = ZSTD_hashPtr(ip, dictHLog, mls);
            U32 const dictMatchIndex = dictHashTable[dictHash];
            const BYTE* dictMatch = dictBase + dictMatchIndex;
            if (dictMatchIndex <= dictStartIndex ||
                MEM_read32(dictMatch) != MEM_read32(ip)) {
                assert(stepSize >= 1);
                ip += ((ip-anchor) >> kSearchStrength) + stepSize;
                continue;
            } else {
                /* found a dict match */
                U32 const offset = (U32)(curr-dictMatchIndex-dictIndexDelta);
                mLength = ZSTD_count_2segments(ip+4, dictMatch+4, iend, dictEnd, prefixStart) + 4;
                while (((ip>anchor) & (dictMatch>dictStart))
                     && (ip[-1] == dictMatch[-1])) {
                    ip--; dictMatch--; mLength++;
                } /* catch up */
                offset_2 = offset_1;
                offset_1 = offset;
                ZSTD_storeSeq(seqStore, (size_t)(ip-anchor), anchor, iend, offset + ZSTD_REP_MOVE, mLength-MINMATCH);
            }
        } else if (MEM_read32(match) != MEM_read32(ip)) {
            /* it's not a match, and we're not going to check the dictionary */
            assert(stepSize >= 1);
            ip += ((ip-anchor) >> kSearchStrength) + stepSize;
            continue;
        } else {
            /* found a regular match */
            U32 const offset = (U32)(ip-match);
            mLength = ZSTD_count(ip+4, match+4, iend) + 4;
            while (((ip>anchor) & (match>prefixStart))
                 && (ip[-1] == match[-1])) { ip--; match--; mLength++; } /* catch up */
            offset_2 = offset_1;
            offset_1 = offset;
            ZSTD_storeSeq(seqStore, (size_t)(ip-anchor), anchor, iend, offset + ZSTD_REP_MOVE, mLength-MINMATCH);
        }

        /* match found */
        ip += mLength;
        anchor = ip;

        if (ip <= ilimit) {
            /* Fill Table */
            assert(base+curr+2 > istart);  /* check base overflow */
            hashTable[ZSTD_hashPtr(base+curr+2, hlog, mls)] = curr+2;  /* here because curr+2 could be > iend-8 */
            hashTable[ZSTD_hashPtr(ip-2, hlog, mls)] = (U32)(ip-2-base);

            /* check immediate repcode */
            while (ip <= ilimit) {
                U32 const current2 = (U32)(ip-base);
                U32 const repIndex2 = current2 - offset_2;
                const BYTE* repMatch2 = repIndex2 < prefixStartIndex ?
                        dictBase - dictIndexDelta + repIndex2 :
                        base + repIndex2;
                if ( ((U32)((prefixStartIndex-1) - (U32)repIndex2) >= 3 /* intentional overflow */)
                   && (MEM_read32(repMatch2) == MEM_read32(ip)) ) {
                    const BYTE* const repEnd2 = repIndex2 < prefixStartIndex ? dictEnd : iend;
                    size_t const repLength2 = ZSTD_count_2segments(ip+4, repMatch2+4, iend, repEnd2, prefixStart) + 4;
                    U32 tmpOffset = offset_2; offset_2 = offset_1; offset_1 = tmpOffset;   /* swap offset_2 <=> offset_1 */
                    ZSTD_storeSeq(seqStore, 0, anchor, iend, 0, repLength2-MINMATCH);
                    hashTable[ZSTD_hashPtr(ip, hlog, mls)] = current2;
                    ip += repLength2;
                    anchor = ip;
                    continue;
                }
                break;
            }
        }
    }

    /* save reps for next block */
    rep[0] = offset_1 ? offset_1 : offsetSaved;
    rep[1] = offset_2 ? offset_2 : offsetSaved;

    /* Return the last literals size */
    return (size_t)(iend - anchor);
}

size_t ZSTD_compressBlock_fast_dictMatchState(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    U32 const mls = ms->cParams.minMatch;
    assert(ms->dictMatchState != NULL);
    switch(mls)
    {
    default: /* includes case 3 */
    case 4 :
        return ZSTD_compressBlock_fast_dictMatchState_generic(ms, seqStore, rep, src, srcSize, 4);
    case 5 :
        return ZSTD_compressBlock_fast_dictMatchState_generic(ms, seqStore, rep, src, srcSize, 5);
    case 6 :
        return ZSTD_compressBlock_fast_dictMatchState_generic(ms, seqStore, rep, src, srcSize, 6);
    case 7 :
        return ZSTD_compressBlock_fast_dictMatchState_generic(ms, seqStore, rep, src, srcSize, 7);
    }
}


static size_t ZSTD_compressBlock_fast_extDict_generic(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize, U32 const mls)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    U32* const hashTable = ms->hashTable;
    U32 const hlog = cParams->hashLog;
    /* support stepSize of 0 */
    U32 const stepSize = cParams->targetLength + !(cParams->targetLength);
    const BYTE* const base = ms->window.base;
    const BYTE* const dictBase = ms->window.dictBase;
    const BYTE* const istart = (const BYTE*)src;
    const BYTE* ip = istart;
    const BYTE* anchor = istart;
    const U32   endIndex = (U32)((size_t)(istart - base) + srcSize);
    const U32   lowLimit = ZSTD_getLowestMatchIndex(ms, endIndex, cParams->windowLog);
    const U32   dictStartIndex = lowLimit;
    const BYTE* const dictStart = dictBase + dictStartIndex;
    const U32   dictLimit = ms->window.dictLimit;
    const U32   prefixStartIndex = dictLimit < lowLimit ? lowLimit : dictLimit;
    const BYTE* const prefixStart = base + prefixStartIndex;
    const BYTE* const dictEnd = dictBase + prefixStartIndex;
    const BYTE* const iend = istart + srcSize;
    const BYTE* const ilimit = iend - 8;
    U32 offset_1=rep[0], offset_2=rep[1];

    DEBUGLOG(5, "ZSTD_compressBlock_fast_extDict_generic (offset_1=%u)", offset_1);

    /* switch to "regular" variant if extDict is invalidated due to maxDistance */
    if (prefixStartIndex == dictStartIndex)
        return ZSTD_compressBlock_fast_generic(ms, seqStore, rep, src, srcSize, mls);

    /* Search Loop */
    while (ip < ilimit) {  /* < instead of <=, because (ip+1) */
        const size_t h = ZSTD_hashPtr(ip, hlog, mls);
        const U32    matchIndex = hashTable[h];
        const BYTE* const matchBase = matchIndex < prefixStartIndex ? dictBase : base;
        const BYTE*  match = matchBase + matchIndex;
        const U32    curr = (U32)(ip-base);
        const U32    repIndex = curr + 1 - offset_1;
        const BYTE* const repBase = repIndex < prefixStartIndex ? dictBase : base;
        const BYTE* const repMatch = repBase + repIndex;
        hashTable[h] = curr;   /* update hash table */
        DEBUGLOG(7, "offset_1 = %u , curr = %u", offset_1, curr);
        assert(offset_1 <= curr +1);   /* check repIndex */

        if ( (((U32)((prefixStartIndex-1) - repIndex) >= 3) /* intentional underflow */ & (repIndex > dictStartIndex))
           && (MEM_read32(repMatch) == MEM_read32(ip+1)) ) {
            const BYTE* const repMatchEnd = repIndex < prefixStartIndex ? dictEnd : iend;
            size_t const rLength = ZSTD_count_2segments(ip+1 +4, repMatch +4, iend, repMatchEnd, prefixStart) + 4;
            ip++;
            ZSTD_storeSeq(seqStore, (size_t)(ip-anchor), anchor, iend, 0, rLength-MINMATCH);
            ip += rLength;
            anchor = ip;
        } else {
            if ( (matchIndex < dictStartIndex) ||
                 (MEM_read32(match) != MEM_read32(ip)) ) {
                assert(stepSize >= 1);
                ip += ((ip-anchor) >> kSearchStrength) + stepSize;
                continue;
            }
            {   const BYTE* const matchEnd = matchIndex < prefixStartIndex ? dictEnd : iend;
                const BYTE* const lowMatchPtr = matchIndex < prefixStartIndex ? dictStart : prefixStart;
                U32 const offset = curr - matchIndex;
                size_t mLength = ZSTD_count_2segments(ip+4, match+4, iend, matchEnd, prefixStart) + 4;
                while (((ip>anchor) & (match>lowMatchPtr)) && (ip[-1] == match[-1])) { ip--; match--; mLength++; }   /* catch up */
                offset_2 = offset_1; offset_1 = offset;  /* update offset history */
                ZSTD_storeSeq(seqStore, (size_t)(ip-anchor), anchor, iend, offset + ZSTD_REP_MOVE, mLength-MINMATCH);
                ip += mLength;
                anchor = ip;
        }   }

        if (ip <= ilimit) {
            /* Fill Table */
            hashTable[ZSTD_hashPtr(base+curr+2, hlog, mls)] = curr+2;
            hashTable[ZSTD_hashPtr(ip-2, hlog, mls)] = (U32)(ip-2-base);
            /* check immediate repcode */
            while (ip <= ilimit) {
                U32 const current2 = (U32)(ip-base);
                U32 const repIndex2 = current2 - offset_2;
                const BYTE* const repMatch2 = repIndex2 < prefixStartIndex ? dictBase + repIndex2 : base + repIndex2;
                if ( (((U32)((prefixStartIndex-1) - repIndex2) >= 3) & (repIndex2 > dictStartIndex))  /* intentional overflow */
                   && (MEM_read32(repMatch2) == MEM_read32(ip)) ) {
                    const BYTE* const repEnd2 = repIndex2 < prefixStartIndex ? dictEnd : iend;
                    size_t const repLength2 = ZSTD_count_2segments(ip+4, repMatch2+4, iend, repEnd2, prefixStart) + 4;
                    { U32 const tmpOffset = offset_2; offset_2 = offset_1; offset_1 = tmpOffset; }  /* swap offset_2 <=> offset_1 */
                    ZSTD_storeSeq(seqStore, 0 /*litlen*/, anchor, iend, 0 /*offcode*/, repLength2-MINMATCH);
                    hashTable[ZSTD_hashPtr(ip, hlog, mls)] = current2;
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


size_t ZSTD_compressBlock_fast_extDict(
        ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
        void const* src, size_t srcSize)
{
    U32 const mls = ms->cParams.minMatch;
    switch(mls)
    {
    default: /* includes case 3 */
    case 4 :
        return ZSTD_compressBlock_fast_extDict_generic(ms, seqStore, rep, src, srcSize, 4);
    case 5 :
        return ZSTD_compressBlock_fast_extDict_generic(ms, seqStore, rep, src, srcSize, 5);
    case 6 :
        return ZSTD_compressBlock_fast_extDict_generic(ms, seqStore, rep, src, srcSize, 6);
    case 7 :
        return ZSTD_compressBlock_fast_extDict_generic(ms, seqStore, rep, src, srcSize, 7);
    }
}
