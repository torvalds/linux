/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

#include "zstd_ldm.h"

#include "debug.h"
#include "zstd_fast.h"          /* ZSTD_fillHashTable() */
#include "zstd_double_fast.h"   /* ZSTD_fillDoubleHashTable() */

#define LDM_BUCKET_SIZE_LOG 3
#define LDM_MIN_MATCH_LENGTH 64
#define LDM_HASH_RLOG 7
#define LDM_HASH_CHAR_OFFSET 10

void ZSTD_ldm_adjustParameters(ldmParams_t* params,
                               ZSTD_compressionParameters const* cParams)
{
    params->windowLog = cParams->windowLog;
    ZSTD_STATIC_ASSERT(LDM_BUCKET_SIZE_LOG <= ZSTD_LDM_BUCKETSIZELOG_MAX);
    DEBUGLOG(4, "ZSTD_ldm_adjustParameters");
    if (!params->bucketSizeLog) params->bucketSizeLog = LDM_BUCKET_SIZE_LOG;
    if (!params->minMatchLength) params->minMatchLength = LDM_MIN_MATCH_LENGTH;
    if (cParams->strategy >= ZSTD_btopt) {
      /* Get out of the way of the optimal parser */
      U32 const minMatch = MAX(cParams->targetLength, params->minMatchLength);
      assert(minMatch >= ZSTD_LDM_MINMATCH_MIN);
      assert(minMatch <= ZSTD_LDM_MINMATCH_MAX);
      params->minMatchLength = minMatch;
    }
    if (params->hashLog == 0) {
        params->hashLog = MAX(ZSTD_HASHLOG_MIN, params->windowLog - LDM_HASH_RLOG);
        assert(params->hashLog <= ZSTD_HASHLOG_MAX);
    }
    if (params->hashRateLog == 0) {
        params->hashRateLog = params->windowLog < params->hashLog
                                   ? 0
                                   : params->windowLog - params->hashLog;
    }
    params->bucketSizeLog = MIN(params->bucketSizeLog, params->hashLog);
}

size_t ZSTD_ldm_getTableSize(ldmParams_t params)
{
    size_t const ldmHSize = ((size_t)1) << params.hashLog;
    size_t const ldmBucketSizeLog = MIN(params.bucketSizeLog, params.hashLog);
    size_t const ldmBucketSize =
        ((size_t)1) << (params.hashLog - ldmBucketSizeLog);
    size_t const totalSize = ldmBucketSize + ldmHSize * sizeof(ldmEntry_t);
    return params.enableLdm ? totalSize : 0;
}

size_t ZSTD_ldm_getMaxNbSeq(ldmParams_t params, size_t maxChunkSize)
{
    return params.enableLdm ? (maxChunkSize / params.minMatchLength) : 0;
}

/** ZSTD_ldm_getSmallHash() :
 *  numBits should be <= 32
 *  If numBits==0, returns 0.
 *  @return : the most significant numBits of value. */
static U32 ZSTD_ldm_getSmallHash(U64 value, U32 numBits)
{
    assert(numBits <= 32);
    return numBits == 0 ? 0 : (U32)(value >> (64 - numBits));
}

/** ZSTD_ldm_getChecksum() :
 *  numBitsToDiscard should be <= 32
 *  @return : the next most significant 32 bits after numBitsToDiscard */
static U32 ZSTD_ldm_getChecksum(U64 hash, U32 numBitsToDiscard)
{
    assert(numBitsToDiscard <= 32);
    return (hash >> (64 - 32 - numBitsToDiscard)) & 0xFFFFFFFF;
}

/** ZSTD_ldm_getTag() ;
 *  Given the hash, returns the most significant numTagBits bits
 *  after (32 + hbits) bits.
 *
 *  If there are not enough bits remaining, return the last
 *  numTagBits bits. */
static U32 ZSTD_ldm_getTag(U64 hash, U32 hbits, U32 numTagBits)
{
    assert(numTagBits < 32 && hbits <= 32);
    if (32 - hbits < numTagBits) {
        return hash & (((U32)1 << numTagBits) - 1);
    } else {
        return (hash >> (32 - hbits - numTagBits)) & (((U32)1 << numTagBits) - 1);
    }
}

/** ZSTD_ldm_getBucket() :
 *  Returns a pointer to the start of the bucket associated with hash. */
static ldmEntry_t* ZSTD_ldm_getBucket(
        ldmState_t* ldmState, size_t hash, ldmParams_t const ldmParams)
{
    return ldmState->hashTable + (hash << ldmParams.bucketSizeLog);
}

/** ZSTD_ldm_insertEntry() :
 *  Insert the entry with corresponding hash into the hash table */
static void ZSTD_ldm_insertEntry(ldmState_t* ldmState,
                                 size_t const hash, const ldmEntry_t entry,
                                 ldmParams_t const ldmParams)
{
    BYTE* const bucketOffsets = ldmState->bucketOffsets;
    *(ZSTD_ldm_getBucket(ldmState, hash, ldmParams) + bucketOffsets[hash]) = entry;
    bucketOffsets[hash]++;
    bucketOffsets[hash] &= ((U32)1 << ldmParams.bucketSizeLog) - 1;
}

/** ZSTD_ldm_makeEntryAndInsertByTag() :
 *
 *  Gets the small hash, checksum, and tag from the rollingHash.
 *
 *  If the tag matches (1 << ldmParams.hashRateLog)-1, then
 *  creates an ldmEntry from the offset, and inserts it into the hash table.
 *
 *  hBits is the length of the small hash, which is the most significant hBits
 *  of rollingHash. The checksum is the next 32 most significant bits, followed
 *  by ldmParams.hashRateLog bits that make up the tag. */
static void ZSTD_ldm_makeEntryAndInsertByTag(ldmState_t* ldmState,
                                             U64 const rollingHash,
                                             U32 const hBits,
                                             U32 const offset,
                                             ldmParams_t const ldmParams)
{
    U32 const tag = ZSTD_ldm_getTag(rollingHash, hBits, ldmParams.hashRateLog);
    U32 const tagMask = ((U32)1 << ldmParams.hashRateLog) - 1;
    if (tag == tagMask) {
        U32 const hash = ZSTD_ldm_getSmallHash(rollingHash, hBits);
        U32 const checksum = ZSTD_ldm_getChecksum(rollingHash, hBits);
        ldmEntry_t entry;
        entry.offset = offset;
        entry.checksum = checksum;
        ZSTD_ldm_insertEntry(ldmState, hash, entry, ldmParams);
    }
}

/** ZSTD_ldm_countBackwardsMatch() :
 *  Returns the number of bytes that match backwards before pIn and pMatch.
 *
 *  We count only bytes where pMatch >= pBase and pIn >= pAnchor. */
static size_t ZSTD_ldm_countBackwardsMatch(
            const BYTE* pIn, const BYTE* pAnchor,
            const BYTE* pMatch, const BYTE* pBase)
{
    size_t matchLength = 0;
    while (pIn > pAnchor && pMatch > pBase && pIn[-1] == pMatch[-1]) {
        pIn--;
        pMatch--;
        matchLength++;
    }
    return matchLength;
}

/** ZSTD_ldm_fillFastTables() :
 *
 *  Fills the relevant tables for the ZSTD_fast and ZSTD_dfast strategies.
 *  This is similar to ZSTD_loadDictionaryContent.
 *
 *  The tables for the other strategies are filled within their
 *  block compressors. */
static size_t ZSTD_ldm_fillFastTables(ZSTD_matchState_t* ms,
                                      void const* end)
{
    const BYTE* const iend = (const BYTE*)end;

    switch(ms->cParams.strategy)
    {
    case ZSTD_fast:
        ZSTD_fillHashTable(ms, iend, ZSTD_dtlm_fast);
        break;

    case ZSTD_dfast:
        ZSTD_fillDoubleHashTable(ms, iend, ZSTD_dtlm_fast);
        break;

    case ZSTD_greedy:
    case ZSTD_lazy:
    case ZSTD_lazy2:
    case ZSTD_btlazy2:
    case ZSTD_btopt:
    case ZSTD_btultra:
    case ZSTD_btultra2:
        break;
    default:
        assert(0);  /* not possible : not a valid strategy id */
    }

    return 0;
}

/** ZSTD_ldm_fillLdmHashTable() :
 *
 *  Fills hashTable from (lastHashed + 1) to iend (non-inclusive).
 *  lastHash is the rolling hash that corresponds to lastHashed.
 *
 *  Returns the rolling hash corresponding to position iend-1. */
static U64 ZSTD_ldm_fillLdmHashTable(ldmState_t* state,
                                     U64 lastHash, const BYTE* lastHashed,
                                     const BYTE* iend, const BYTE* base,
                                     U32 hBits, ldmParams_t const ldmParams)
{
    U64 rollingHash = lastHash;
    const BYTE* cur = lastHashed + 1;

    while (cur < iend) {
        rollingHash = ZSTD_rollingHash_rotate(rollingHash, cur[-1],
                                              cur[ldmParams.minMatchLength-1],
                                              state->hashPower);
        ZSTD_ldm_makeEntryAndInsertByTag(state,
                                         rollingHash, hBits,
                                         (U32)(cur - base), ldmParams);
        ++cur;
    }
    return rollingHash;
}


/** ZSTD_ldm_limitTableUpdate() :
 *
 *  Sets cctx->nextToUpdate to a position corresponding closer to anchor
 *  if it is far way
 *  (after a long match, only update tables a limited amount). */
static void ZSTD_ldm_limitTableUpdate(ZSTD_matchState_t* ms, const BYTE* anchor)
{
    U32 const current = (U32)(anchor - ms->window.base);
    if (current > ms->nextToUpdate + 1024) {
        ms->nextToUpdate =
            current - MIN(512, current - ms->nextToUpdate - 1024);
    }
}

static size_t ZSTD_ldm_generateSequences_internal(
        ldmState_t* ldmState, rawSeqStore_t* rawSeqStore,
        ldmParams_t const* params, void const* src, size_t srcSize)
{
    /* LDM parameters */
    int const extDict = ZSTD_window_hasExtDict(ldmState->window);
    U32 const minMatchLength = params->minMatchLength;
    U64 const hashPower = ldmState->hashPower;
    U32 const hBits = params->hashLog - params->bucketSizeLog;
    U32 const ldmBucketSize = 1U << params->bucketSizeLog;
    U32 const hashRateLog = params->hashRateLog;
    U32 const ldmTagMask = (1U << params->hashRateLog) - 1;
    /* Prefix and extDict parameters */
    U32 const dictLimit = ldmState->window.dictLimit;
    U32 const lowestIndex = extDict ? ldmState->window.lowLimit : dictLimit;
    BYTE const* const base = ldmState->window.base;
    BYTE const* const dictBase = extDict ? ldmState->window.dictBase : NULL;
    BYTE const* const dictStart = extDict ? dictBase + lowestIndex : NULL;
    BYTE const* const dictEnd = extDict ? dictBase + dictLimit : NULL;
    BYTE const* const lowPrefixPtr = base + dictLimit;
    /* Input bounds */
    BYTE const* const istart = (BYTE const*)src;
    BYTE const* const iend = istart + srcSize;
    BYTE const* const ilimit = iend - MAX(minMatchLength, HASH_READ_SIZE);
    /* Input positions */
    BYTE const* anchor = istart;
    BYTE const* ip = istart;
    /* Rolling hash */
    BYTE const* lastHashed = NULL;
    U64 rollingHash = 0;

    while (ip <= ilimit) {
        size_t mLength;
        U32 const current = (U32)(ip - base);
        size_t forwardMatchLength = 0, backwardMatchLength = 0;
        ldmEntry_t* bestEntry = NULL;
        if (ip != istart) {
            rollingHash = ZSTD_rollingHash_rotate(rollingHash, lastHashed[0],
                                                  lastHashed[minMatchLength],
                                                  hashPower);
        } else {
            rollingHash = ZSTD_rollingHash_compute(ip, minMatchLength);
        }
        lastHashed = ip;

        /* Do not insert and do not look for a match */
        if (ZSTD_ldm_getTag(rollingHash, hBits, hashRateLog) != ldmTagMask) {
           ip++;
           continue;
        }

        /* Get the best entry and compute the match lengths */
        {
            ldmEntry_t* const bucket =
                ZSTD_ldm_getBucket(ldmState,
                                   ZSTD_ldm_getSmallHash(rollingHash, hBits),
                                   *params);
            ldmEntry_t* cur;
            size_t bestMatchLength = 0;
            U32 const checksum = ZSTD_ldm_getChecksum(rollingHash, hBits);

            for (cur = bucket; cur < bucket + ldmBucketSize; ++cur) {
                size_t curForwardMatchLength, curBackwardMatchLength,
                       curTotalMatchLength;
                if (cur->checksum != checksum || cur->offset <= lowestIndex) {
                    continue;
                }
                if (extDict) {
                    BYTE const* const curMatchBase =
                        cur->offset < dictLimit ? dictBase : base;
                    BYTE const* const pMatch = curMatchBase + cur->offset;
                    BYTE const* const matchEnd =
                        cur->offset < dictLimit ? dictEnd : iend;
                    BYTE const* const lowMatchPtr =
                        cur->offset < dictLimit ? dictStart : lowPrefixPtr;

                    curForwardMatchLength = ZSTD_count_2segments(
                                                ip, pMatch, iend,
                                                matchEnd, lowPrefixPtr);
                    if (curForwardMatchLength < minMatchLength) {
                        continue;
                    }
                    curBackwardMatchLength =
                        ZSTD_ldm_countBackwardsMatch(ip, anchor, pMatch,
                                                     lowMatchPtr);
                    curTotalMatchLength = curForwardMatchLength +
                                          curBackwardMatchLength;
                } else { /* !extDict */
                    BYTE const* const pMatch = base + cur->offset;
                    curForwardMatchLength = ZSTD_count(ip, pMatch, iend);
                    if (curForwardMatchLength < minMatchLength) {
                        continue;
                    }
                    curBackwardMatchLength =
                        ZSTD_ldm_countBackwardsMatch(ip, anchor, pMatch,
                                                     lowPrefixPtr);
                    curTotalMatchLength = curForwardMatchLength +
                                          curBackwardMatchLength;
                }

                if (curTotalMatchLength > bestMatchLength) {
                    bestMatchLength = curTotalMatchLength;
                    forwardMatchLength = curForwardMatchLength;
                    backwardMatchLength = curBackwardMatchLength;
                    bestEntry = cur;
                }
            }
        }

        /* No match found -- continue searching */
        if (bestEntry == NULL) {
            ZSTD_ldm_makeEntryAndInsertByTag(ldmState, rollingHash,
                                             hBits, current,
                                             *params);
            ip++;
            continue;
        }

        /* Match found */
        mLength = forwardMatchLength + backwardMatchLength;
        ip -= backwardMatchLength;

        {
            /* Store the sequence:
             * ip = current - backwardMatchLength
             * The match is at (bestEntry->offset - backwardMatchLength)
             */
            U32 const matchIndex = bestEntry->offset;
            U32 const offset = current - matchIndex;
            rawSeq* const seq = rawSeqStore->seq + rawSeqStore->size;

            /* Out of sequence storage */
            if (rawSeqStore->size == rawSeqStore->capacity)
                return ERROR(dstSize_tooSmall);
            seq->litLength = (U32)(ip - anchor);
            seq->matchLength = (U32)mLength;
            seq->offset = offset;
            rawSeqStore->size++;
        }

        /* Insert the current entry into the hash table */
        ZSTD_ldm_makeEntryAndInsertByTag(ldmState, rollingHash, hBits,
                                         (U32)(lastHashed - base),
                                         *params);

        assert(ip + backwardMatchLength == lastHashed);

        /* Fill the hash table from lastHashed+1 to ip+mLength*/
        /* Heuristic: don't need to fill the entire table at end of block */
        if (ip + mLength <= ilimit) {
            rollingHash = ZSTD_ldm_fillLdmHashTable(
                              ldmState, rollingHash, lastHashed,
                              ip + mLength, base, hBits, *params);
            lastHashed = ip + mLength - 1;
        }
        ip += mLength;
        anchor = ip;
    }
    return iend - anchor;
}

/*! ZSTD_ldm_reduceTable() :
 *  reduce table indexes by `reducerValue` */
static void ZSTD_ldm_reduceTable(ldmEntry_t* const table, U32 const size,
                                 U32 const reducerValue)
{
    U32 u;
    for (u = 0; u < size; u++) {
        if (table[u].offset < reducerValue) table[u].offset = 0;
        else table[u].offset -= reducerValue;
    }
}

size_t ZSTD_ldm_generateSequences(
        ldmState_t* ldmState, rawSeqStore_t* sequences,
        ldmParams_t const* params, void const* src, size_t srcSize)
{
    U32 const maxDist = 1U << params->windowLog;
    BYTE const* const istart = (BYTE const*)src;
    BYTE const* const iend = istart + srcSize;
    size_t const kMaxChunkSize = 1 << 20;
    size_t const nbChunks = (srcSize / kMaxChunkSize) + ((srcSize % kMaxChunkSize) != 0);
    size_t chunk;
    size_t leftoverSize = 0;

    assert(ZSTD_CHUNKSIZE_MAX >= kMaxChunkSize);
    /* Check that ZSTD_window_update() has been called for this chunk prior
     * to passing it to this function.
     */
    assert(ldmState->window.nextSrc >= (BYTE const*)src + srcSize);
    /* The input could be very large (in zstdmt), so it must be broken up into
     * chunks to enforce the maximmum distance and handle overflow correction.
     */
    assert(sequences->pos <= sequences->size);
    assert(sequences->size <= sequences->capacity);
    for (chunk = 0; chunk < nbChunks && sequences->size < sequences->capacity; ++chunk) {
        BYTE const* const chunkStart = istart + chunk * kMaxChunkSize;
        size_t const remaining = (size_t)(iend - chunkStart);
        BYTE const *const chunkEnd =
            (remaining < kMaxChunkSize) ? iend : chunkStart + kMaxChunkSize;
        size_t const chunkSize = chunkEnd - chunkStart;
        size_t newLeftoverSize;
        size_t const prevSize = sequences->size;

        assert(chunkStart < iend);
        /* 1. Perform overflow correction if necessary. */
        if (ZSTD_window_needOverflowCorrection(ldmState->window, chunkEnd)) {
            U32 const ldmHSize = 1U << params->hashLog;
            U32 const correction = ZSTD_window_correctOverflow(
                &ldmState->window, /* cycleLog */ 0, maxDist, src);
            ZSTD_ldm_reduceTable(ldmState->hashTable, ldmHSize, correction);
        }
        /* 2. We enforce the maximum offset allowed.
         *
         * kMaxChunkSize should be small enough that we don't lose too much of
         * the window through early invalidation.
         * TODO: * Test the chunk size.
         *       * Try invalidation after the sequence generation and test the
         *         the offset against maxDist directly.
         */
        ZSTD_window_enforceMaxDist(&ldmState->window, chunkEnd, maxDist, NULL, NULL);
        /* 3. Generate the sequences for the chunk, and get newLeftoverSize. */
        newLeftoverSize = ZSTD_ldm_generateSequences_internal(
            ldmState, sequences, params, chunkStart, chunkSize);
        if (ZSTD_isError(newLeftoverSize))
            return newLeftoverSize;
        /* 4. We add the leftover literals from previous iterations to the first
         *    newly generated sequence, or add the `newLeftoverSize` if none are
         *    generated.
         */
        /* Prepend the leftover literals from the last call */
        if (prevSize < sequences->size) {
            sequences->seq[prevSize].litLength += (U32)leftoverSize;
            leftoverSize = newLeftoverSize;
        } else {
            assert(newLeftoverSize == chunkSize);
            leftoverSize += chunkSize;
        }
    }
    return 0;
}

void ZSTD_ldm_skipSequences(rawSeqStore_t* rawSeqStore, size_t srcSize, U32 const minMatch) {
    while (srcSize > 0 && rawSeqStore->pos < rawSeqStore->size) {
        rawSeq* seq = rawSeqStore->seq + rawSeqStore->pos;
        if (srcSize <= seq->litLength) {
            /* Skip past srcSize literals */
            seq->litLength -= (U32)srcSize;
            return;
        }
        srcSize -= seq->litLength;
        seq->litLength = 0;
        if (srcSize < seq->matchLength) {
            /* Skip past the first srcSize of the match */
            seq->matchLength -= (U32)srcSize;
            if (seq->matchLength < minMatch) {
                /* The match is too short, omit it */
                if (rawSeqStore->pos + 1 < rawSeqStore->size) {
                    seq[1].litLength += seq[0].matchLength;
                }
                rawSeqStore->pos++;
            }
            return;
        }
        srcSize -= seq->matchLength;
        seq->matchLength = 0;
        rawSeqStore->pos++;
    }
}

/**
 * If the sequence length is longer than remaining then the sequence is split
 * between this block and the next.
 *
 * Returns the current sequence to handle, or if the rest of the block should
 * be literals, it returns a sequence with offset == 0.
 */
static rawSeq maybeSplitSequence(rawSeqStore_t* rawSeqStore,
                                 U32 const remaining, U32 const minMatch)
{
    rawSeq sequence = rawSeqStore->seq[rawSeqStore->pos];
    assert(sequence.offset > 0);
    /* Likely: No partial sequence */
    if (remaining >= sequence.litLength + sequence.matchLength) {
        rawSeqStore->pos++;
        return sequence;
    }
    /* Cut the sequence short (offset == 0 ==> rest is literals). */
    if (remaining <= sequence.litLength) {
        sequence.offset = 0;
    } else if (remaining < sequence.litLength + sequence.matchLength) {
        sequence.matchLength = remaining - sequence.litLength;
        if (sequence.matchLength < minMatch) {
            sequence.offset = 0;
        }
    }
    /* Skip past `remaining` bytes for the future sequences. */
    ZSTD_ldm_skipSequences(rawSeqStore, remaining, minMatch);
    return sequence;
}

size_t ZSTD_ldm_blockCompress(rawSeqStore_t* rawSeqStore,
    ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
    void const* src, size_t srcSize)
{
    const ZSTD_compressionParameters* const cParams = &ms->cParams;
    unsigned const minMatch = cParams->minMatch;
    ZSTD_blockCompressor const blockCompressor =
        ZSTD_selectBlockCompressor(cParams->strategy, ZSTD_matchState_dictMode(ms));
    /* Input bounds */
    BYTE const* const istart = (BYTE const*)src;
    BYTE const* const iend = istart + srcSize;
    /* Input positions */
    BYTE const* ip = istart;

    DEBUGLOG(5, "ZSTD_ldm_blockCompress: srcSize=%zu", srcSize);
    assert(rawSeqStore->pos <= rawSeqStore->size);
    assert(rawSeqStore->size <= rawSeqStore->capacity);
    /* Loop through each sequence and apply the block compressor to the lits */
    while (rawSeqStore->pos < rawSeqStore->size && ip < iend) {
        /* maybeSplitSequence updates rawSeqStore->pos */
        rawSeq const sequence = maybeSplitSequence(rawSeqStore,
                                                   (U32)(iend - ip), minMatch);
        int i;
        /* End signal */
        if (sequence.offset == 0)
            break;

        assert(sequence.offset <= (1U << cParams->windowLog));
        assert(ip + sequence.litLength + sequence.matchLength <= iend);

        /* Fill tables for block compressor */
        ZSTD_ldm_limitTableUpdate(ms, ip);
        ZSTD_ldm_fillFastTables(ms, ip);
        /* Run the block compressor */
        DEBUGLOG(5, "calling block compressor on segment of size %u", sequence.litLength);
        {
            size_t const newLitLength =
                blockCompressor(ms, seqStore, rep, ip, sequence.litLength);
            ip += sequence.litLength;
            /* Update the repcodes */
            for (i = ZSTD_REP_NUM - 1; i > 0; i--)
                rep[i] = rep[i-1];
            rep[0] = sequence.offset;
            /* Store the sequence */
            ZSTD_storeSeq(seqStore, newLitLength, ip - newLitLength,
                          sequence.offset + ZSTD_REP_MOVE,
                          sequence.matchLength - MINMATCH);
            ip += sequence.matchLength;
        }
    }
    /* Fill the tables for the block compressor */
    ZSTD_ldm_limitTableUpdate(ms, ip);
    ZSTD_ldm_fillFastTables(ms, ip);
    /* Compress the last literals */
    return blockCompressor(ms, seqStore, rep, ip, iend - ip);
}
