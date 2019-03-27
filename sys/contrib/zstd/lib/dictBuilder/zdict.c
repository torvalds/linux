/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/*-**************************************
*  Tuning parameters
****************************************/
#define MINRATIO 4   /* minimum nb of apparition to be selected in dictionary */
#define ZDICT_MAX_SAMPLES_SIZE (2000U << 20)
#define ZDICT_MIN_SAMPLES_SIZE (ZDICT_CONTENTSIZE_MIN * MINRATIO)


/*-**************************************
*  Compiler Options
****************************************/
/* Unix Large Files support (>4GB) */
#define _FILE_OFFSET_BITS 64
#if (defined(__sun__) && (!defined(__LP64__)))   /* Sun Solaris 32-bits requires specific definitions */
#  define _LARGEFILE_SOURCE
#elif ! defined(__LP64__)                        /* No point defining Large file for 64 bit */
#  define _LARGEFILE64_SOURCE
#endif


/*-*************************************
*  Dependencies
***************************************/
#include <stdlib.h>        /* malloc, free */
#include <string.h>        /* memset */
#include <stdio.h>         /* fprintf, fopen, ftello64 */
#include <time.h>          /* clock */

#include "mem.h"           /* read */
#include "fse.h"           /* FSE_normalizeCount, FSE_writeNCount */
#define HUF_STATIC_LINKING_ONLY
#include "huf.h"           /* HUF_buildCTable, HUF_writeCTable */
#include "zstd_internal.h" /* includes zstd.h */
#include "xxhash.h"        /* XXH64 */
#include "divsufsort.h"
#ifndef ZDICT_STATIC_LINKING_ONLY
#  define ZDICT_STATIC_LINKING_ONLY
#endif
#include "zdict.h"


/*-*************************************
*  Constants
***************************************/
#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define DICTLISTSIZE_DEFAULT 10000

#define NOISELENGTH 32

static const int g_compressionLevel_default = 3;
static const U32 g_selectivity_default = 9;


/*-*************************************
*  Console display
***************************************/
#define DISPLAY(...)         { fprintf(stderr, __VA_ARGS__); fflush( stderr ); }
#define DISPLAYLEVEL(l, ...) if (notificationLevel>=l) { DISPLAY(__VA_ARGS__); }    /* 0 : no display;   1: errors;   2: default;  3: details;  4: debug */

static clock_t ZDICT_clockSpan(clock_t nPrevious) { return clock() - nPrevious; }

static void ZDICT_printHex(const void* ptr, size_t length)
{
    const BYTE* const b = (const BYTE*)ptr;
    size_t u;
    for (u=0; u<length; u++) {
        BYTE c = b[u];
        if (c<32 || c>126) c = '.';   /* non-printable char */
        DISPLAY("%c", c);
    }
}


/*-********************************************************
*  Helper functions
**********************************************************/
unsigned ZDICT_isError(size_t errorCode) { return ERR_isError(errorCode); }

const char* ZDICT_getErrorName(size_t errorCode) { return ERR_getErrorName(errorCode); }

unsigned ZDICT_getDictID(const void* dictBuffer, size_t dictSize)
{
    if (dictSize < 8) return 0;
    if (MEM_readLE32(dictBuffer) != ZSTD_MAGIC_DICTIONARY) return 0;
    return MEM_readLE32((const char*)dictBuffer + 4);
}


/*-********************************************************
*  Dictionary training functions
**********************************************************/
static unsigned ZDICT_NbCommonBytes (size_t val)
{
    if (MEM_isLittleEndian()) {
        if (MEM_64bits()) {
#       if defined(_MSC_VER) && defined(_WIN64)
            unsigned long r = 0;
            _BitScanForward64( &r, (U64)val );
            return (unsigned)(r>>3);
#       elif defined(__GNUC__) && (__GNUC__ >= 3)
            return (__builtin_ctzll((U64)val) >> 3);
#       else
            static const int DeBruijnBytePos[64] = { 0, 0, 0, 0, 0, 1, 1, 2, 0, 3, 1, 3, 1, 4, 2, 7, 0, 2, 3, 6, 1, 5, 3, 5, 1, 3, 4, 4, 2, 5, 6, 7, 7, 0, 1, 2, 3, 3, 4, 6, 2, 6, 5, 5, 3, 4, 5, 6, 7, 1, 2, 4, 6, 4, 4, 5, 7, 2, 6, 5, 7, 6, 7, 7 };
            return DeBruijnBytePos[((U64)((val & -(long long)val) * 0x0218A392CDABBD3FULL)) >> 58];
#       endif
        } else { /* 32 bits */
#       if defined(_MSC_VER)
            unsigned long r=0;
            _BitScanForward( &r, (U32)val );
            return (unsigned)(r>>3);
#       elif defined(__GNUC__) && (__GNUC__ >= 3)
            return (__builtin_ctz((U32)val) >> 3);
#       else
            static const int DeBruijnBytePos[32] = { 0, 0, 3, 0, 3, 1, 3, 0, 3, 2, 2, 1, 3, 2, 0, 1, 3, 3, 1, 2, 2, 2, 2, 0, 3, 1, 2, 0, 1, 0, 1, 1 };
            return DeBruijnBytePos[((U32)((val & -(S32)val) * 0x077CB531U)) >> 27];
#       endif
        }
    } else {  /* Big Endian CPU */
        if (MEM_64bits()) {
#       if defined(_MSC_VER) && defined(_WIN64)
            unsigned long r = 0;
            _BitScanReverse64( &r, val );
            return (unsigned)(r>>3);
#       elif defined(__GNUC__) && (__GNUC__ >= 3)
            return (__builtin_clzll(val) >> 3);
#       else
            unsigned r;
            const unsigned n32 = sizeof(size_t)*4;   /* calculate this way due to compiler complaining in 32-bits mode */
            if (!(val>>n32)) { r=4; } else { r=0; val>>=n32; }
            if (!(val>>16)) { r+=2; val>>=8; } else { val>>=24; }
            r += (!val);
            return r;
#       endif
        } else { /* 32 bits */
#       if defined(_MSC_VER)
            unsigned long r = 0;
            _BitScanReverse( &r, (unsigned long)val );
            return (unsigned)(r>>3);
#       elif defined(__GNUC__) && (__GNUC__ >= 3)
            return (__builtin_clz((U32)val) >> 3);
#       else
            unsigned r;
            if (!(val>>16)) { r=2; val>>=8; } else { r=0; val>>=24; }
            r += (!val);
            return r;
#       endif
    }   }
}


/*! ZDICT_count() :
    Count the nb of common bytes between 2 pointers.
    Note : this function presumes end of buffer followed by noisy guard band.
*/
static size_t ZDICT_count(const void* pIn, const void* pMatch)
{
    const char* const pStart = (const char*)pIn;
    for (;;) {
        size_t const diff = MEM_readST(pMatch) ^ MEM_readST(pIn);
        if (!diff) {
            pIn = (const char*)pIn+sizeof(size_t);
            pMatch = (const char*)pMatch+sizeof(size_t);
            continue;
        }
        pIn = (const char*)pIn+ZDICT_NbCommonBytes(diff);
        return (size_t)((const char*)pIn - pStart);
    }
}


typedef struct {
    U32 pos;
    U32 length;
    U32 savings;
} dictItem;

static void ZDICT_initDictItem(dictItem* d)
{
    d->pos = 1;
    d->length = 0;
    d->savings = (U32)(-1);
}


#define LLIMIT 64          /* heuristic determined experimentally */
#define MINMATCHLENGTH 7   /* heuristic determined experimentally */
static dictItem ZDICT_analyzePos(
                       BYTE* doneMarks,
                       const int* suffix, U32 start,
                       const void* buffer, U32 minRatio, U32 notificationLevel)
{
    U32 lengthList[LLIMIT] = {0};
    U32 cumulLength[LLIMIT] = {0};
    U32 savings[LLIMIT] = {0};
    const BYTE* b = (const BYTE*)buffer;
    size_t maxLength = LLIMIT;
    size_t pos = suffix[start];
    U32 end = start;
    dictItem solution;

    /* init */
    memset(&solution, 0, sizeof(solution));
    doneMarks[pos] = 1;

    /* trivial repetition cases */
    if ( (MEM_read16(b+pos+0) == MEM_read16(b+pos+2))
       ||(MEM_read16(b+pos+1) == MEM_read16(b+pos+3))
       ||(MEM_read16(b+pos+2) == MEM_read16(b+pos+4)) ) {
        /* skip and mark segment */
        U16 const pattern16 = MEM_read16(b+pos+4);
        U32 u, patternEnd = 6;
        while (MEM_read16(b+pos+patternEnd) == pattern16) patternEnd+=2 ;
        if (b[pos+patternEnd] == b[pos+patternEnd-1]) patternEnd++;
        for (u=1; u<patternEnd; u++)
            doneMarks[pos+u] = 1;
        return solution;
    }

    /* look forward */
    {   size_t length;
        do {
            end++;
            length = ZDICT_count(b + pos, b + suffix[end]);
        } while (length >= MINMATCHLENGTH);
    }

    /* look backward */
    {   size_t length;
        do {
            length = ZDICT_count(b + pos, b + *(suffix+start-1));
            if (length >=MINMATCHLENGTH) start--;
        } while(length >= MINMATCHLENGTH);
    }

    /* exit if not found a minimum nb of repetitions */
    if (end-start < minRatio) {
        U32 idx;
        for(idx=start; idx<end; idx++)
            doneMarks[suffix[idx]] = 1;
        return solution;
    }

    {   int i;
        U32 mml;
        U32 refinedStart = start;
        U32 refinedEnd = end;

        DISPLAYLEVEL(4, "\n");
        DISPLAYLEVEL(4, "found %3u matches of length >= %i at pos %7u  ", (unsigned)(end-start), MINMATCHLENGTH, (unsigned)pos);
        DISPLAYLEVEL(4, "\n");

        for (mml = MINMATCHLENGTH ; ; mml++) {
            BYTE currentChar = 0;
            U32 currentCount = 0;
            U32 currentID = refinedStart;
            U32 id;
            U32 selectedCount = 0;
            U32 selectedID = currentID;
            for (id =refinedStart; id < refinedEnd; id++) {
                if (b[suffix[id] + mml] != currentChar) {
                    if (currentCount > selectedCount) {
                        selectedCount = currentCount;
                        selectedID = currentID;
                    }
                    currentID = id;
                    currentChar = b[ suffix[id] + mml];
                    currentCount = 0;
                }
                currentCount ++;
            }
            if (currentCount > selectedCount) {  /* for last */
                selectedCount = currentCount;
                selectedID = currentID;
            }

            if (selectedCount < minRatio)
                break;
            refinedStart = selectedID;
            refinedEnd = refinedStart + selectedCount;
        }

        /* evaluate gain based on new dict */
        start = refinedStart;
        pos = suffix[refinedStart];
        end = start;
        memset(lengthList, 0, sizeof(lengthList));

        /* look forward */
        {   size_t length;
            do {
                end++;
                length = ZDICT_count(b + pos, b + suffix[end]);
                if (length >= LLIMIT) length = LLIMIT-1;
                lengthList[length]++;
            } while (length >=MINMATCHLENGTH);
        }

        /* look backward */
        {   size_t length = MINMATCHLENGTH;
            while ((length >= MINMATCHLENGTH) & (start > 0)) {
                length = ZDICT_count(b + pos, b + suffix[start - 1]);
                if (length >= LLIMIT) length = LLIMIT - 1;
                lengthList[length]++;
                if (length >= MINMATCHLENGTH) start--;
            }
        }

        /* largest useful length */
        memset(cumulLength, 0, sizeof(cumulLength));
        cumulLength[maxLength-1] = lengthList[maxLength-1];
        for (i=(int)(maxLength-2); i>=0; i--)
            cumulLength[i] = cumulLength[i+1] + lengthList[i];

        for (i=LLIMIT-1; i>=MINMATCHLENGTH; i--) if (cumulLength[i]>=minRatio) break;
        maxLength = i;

        /* reduce maxLength in case of final into repetitive data */
        {   U32 l = (U32)maxLength;
            BYTE const c = b[pos + maxLength-1];
            while (b[pos+l-2]==c) l--;
            maxLength = l;
        }
        if (maxLength < MINMATCHLENGTH) return solution;   /* skip : no long-enough solution */

        /* calculate savings */
        savings[5] = 0;
        for (i=MINMATCHLENGTH; i<=(int)maxLength; i++)
            savings[i] = savings[i-1] + (lengthList[i] * (i-3));

        DISPLAYLEVEL(4, "Selected dict at position %u, of length %u : saves %u (ratio: %.2f)  \n",
                     (unsigned)pos, (unsigned)maxLength, (unsigned)savings[maxLength], (double)savings[maxLength] / maxLength);

        solution.pos = (U32)pos;
        solution.length = (U32)maxLength;
        solution.savings = savings[maxLength];

        /* mark positions done */
        {   U32 id;
            for (id=start; id<end; id++) {
                U32 p, pEnd, length;
                U32 const testedPos = suffix[id];
                if (testedPos == pos)
                    length = solution.length;
                else {
                    length = (U32)ZDICT_count(b+pos, b+testedPos);
                    if (length > solution.length) length = solution.length;
                }
                pEnd = (U32)(testedPos + length);
                for (p=testedPos; p<pEnd; p++)
                    doneMarks[p] = 1;
    }   }   }

    return solution;
}


static int isIncluded(const void* in, const void* container, size_t length)
{
    const char* const ip = (const char*) in;
    const char* const into = (const char*) container;
    size_t u;

    for (u=0; u<length; u++) {  /* works because end of buffer is a noisy guard band */
        if (ip[u] != into[u]) break;
    }

    return u==length;
}

/*! ZDICT_tryMerge() :
    check if dictItem can be merged, do it if possible
    @return : id of destination elt, 0 if not merged
*/
static U32 ZDICT_tryMerge(dictItem* table, dictItem elt, U32 eltNbToSkip, const void* buffer)
{
    const U32 tableSize = table->pos;
    const U32 eltEnd = elt.pos + elt.length;
    const char* const buf = (const char*) buffer;

    /* tail overlap */
    U32 u; for (u=1; u<tableSize; u++) {
        if (u==eltNbToSkip) continue;
        if ((table[u].pos > elt.pos) && (table[u].pos <= eltEnd)) {  /* overlap, existing > new */
            /* append */
            U32 const addedLength = table[u].pos - elt.pos;
            table[u].length += addedLength;
            table[u].pos = elt.pos;
            table[u].savings += elt.savings * addedLength / elt.length;   /* rough approx */
            table[u].savings += elt.length / 8;    /* rough approx bonus */
            elt = table[u];
            /* sort : improve rank */
            while ((u>1) && (table[u-1].savings < elt.savings))
            table[u] = table[u-1], u--;
            table[u] = elt;
            return u;
    }   }

    /* front overlap */
    for (u=1; u<tableSize; u++) {
        if (u==eltNbToSkip) continue;

        if ((table[u].pos + table[u].length >= elt.pos) && (table[u].pos < elt.pos)) {  /* overlap, existing < new */
            /* append */
            int const addedLength = (int)eltEnd - (table[u].pos + table[u].length);
            table[u].savings += elt.length / 8;    /* rough approx bonus */
            if (addedLength > 0) {   /* otherwise, elt fully included into existing */
                table[u].length += addedLength;
                table[u].savings += elt.savings * addedLength / elt.length;   /* rough approx */
            }
            /* sort : improve rank */
            elt = table[u];
            while ((u>1) && (table[u-1].savings < elt.savings))
                table[u] = table[u-1], u--;
            table[u] = elt;
            return u;
        }

        if (MEM_read64(buf + table[u].pos) == MEM_read64(buf + elt.pos + 1)) {
            if (isIncluded(buf + table[u].pos, buf + elt.pos + 1, table[u].length)) {
                size_t const addedLength = MAX( (int)elt.length - (int)table[u].length , 1 );
                table[u].pos = elt.pos;
                table[u].savings += (U32)(elt.savings * addedLength / elt.length);
                table[u].length = MIN(elt.length, table[u].length + 1);
                return u;
            }
        }
    }

    return 0;
}


static void ZDICT_removeDictItem(dictItem* table, U32 id)
{
    /* convention : table[0].pos stores nb of elts */
    U32 const max = table[0].pos;
    U32 u;
    if (!id) return;   /* protection, should never happen */
    for (u=id; u<max-1; u++)
        table[u] = table[u+1];
    table->pos--;
}


static void ZDICT_insertDictItem(dictItem* table, U32 maxSize, dictItem elt, const void* buffer)
{
    /* merge if possible */
    U32 mergeId = ZDICT_tryMerge(table, elt, 0, buffer);
    if (mergeId) {
        U32 newMerge = 1;
        while (newMerge) {
            newMerge = ZDICT_tryMerge(table, table[mergeId], mergeId, buffer);
            if (newMerge) ZDICT_removeDictItem(table, mergeId);
            mergeId = newMerge;
        }
        return;
    }

    /* insert */
    {   U32 current;
        U32 nextElt = table->pos;
        if (nextElt >= maxSize) nextElt = maxSize-1;
        current = nextElt-1;
        while (table[current].savings < elt.savings) {
            table[current+1] = table[current];
            current--;
        }
        table[current+1] = elt;
        table->pos = nextElt+1;
    }
}


static U32 ZDICT_dictSize(const dictItem* dictList)
{
    U32 u, dictSize = 0;
    for (u=1; u<dictList[0].pos; u++)
        dictSize += dictList[u].length;
    return dictSize;
}


static size_t ZDICT_trainBuffer_legacy(dictItem* dictList, U32 dictListSize,
                            const void* const buffer, size_t bufferSize,   /* buffer must end with noisy guard band */
                            const size_t* fileSizes, unsigned nbFiles,
                            unsigned minRatio, U32 notificationLevel)
{
    int* const suffix0 = (int*)malloc((bufferSize+2)*sizeof(*suffix0));
    int* const suffix = suffix0+1;
    U32* reverseSuffix = (U32*)malloc((bufferSize)*sizeof(*reverseSuffix));
    BYTE* doneMarks = (BYTE*)malloc((bufferSize+16)*sizeof(*doneMarks));   /* +16 for overflow security */
    U32* filePos = (U32*)malloc(nbFiles * sizeof(*filePos));
    size_t result = 0;
    clock_t displayClock = 0;
    clock_t const refreshRate = CLOCKS_PER_SEC * 3 / 10;

#   define DISPLAYUPDATE(l, ...) if (notificationLevel>=l) { \
            if (ZDICT_clockSpan(displayClock) > refreshRate)  \
            { displayClock = clock(); DISPLAY(__VA_ARGS__); \
            if (notificationLevel>=4) fflush(stderr); } }

    /* init */
    DISPLAYLEVEL(2, "\r%70s\r", "");   /* clean display line */
    if (!suffix0 || !reverseSuffix || !doneMarks || !filePos) {
        result = ERROR(memory_allocation);
        goto _cleanup;
    }
    if (minRatio < MINRATIO) minRatio = MINRATIO;
    memset(doneMarks, 0, bufferSize+16);

    /* limit sample set size (divsufsort limitation)*/
    if (bufferSize > ZDICT_MAX_SAMPLES_SIZE) DISPLAYLEVEL(3, "sample set too large : reduced to %u MB ...\n", (unsigned)(ZDICT_MAX_SAMPLES_SIZE>>20));
    while (bufferSize > ZDICT_MAX_SAMPLES_SIZE) bufferSize -= fileSizes[--nbFiles];

    /* sort */
    DISPLAYLEVEL(2, "sorting %u files of total size %u MB ...\n", nbFiles, (unsigned)(bufferSize>>20));
    {   int const divSuftSortResult = divsufsort((const unsigned char*)buffer, suffix, (int)bufferSize, 0);
        if (divSuftSortResult != 0) { result = ERROR(GENERIC); goto _cleanup; }
    }
    suffix[bufferSize] = (int)bufferSize;   /* leads into noise */
    suffix0[0] = (int)bufferSize;           /* leads into noise */
    /* build reverse suffix sort */
    {   size_t pos;
        for (pos=0; pos < bufferSize; pos++)
            reverseSuffix[suffix[pos]] = (U32)pos;
        /* note filePos tracks borders between samples.
           It's not used at this stage, but planned to become useful in a later update */
        filePos[0] = 0;
        for (pos=1; pos<nbFiles; pos++)
            filePos[pos] = (U32)(filePos[pos-1] + fileSizes[pos-1]);
    }

    DISPLAYLEVEL(2, "finding patterns ... \n");
    DISPLAYLEVEL(3, "minimum ratio : %u \n", minRatio);

    {   U32 cursor; for (cursor=0; cursor < bufferSize; ) {
            dictItem solution;
            if (doneMarks[cursor]) { cursor++; continue; }
            solution = ZDICT_analyzePos(doneMarks, suffix, reverseSuffix[cursor], buffer, minRatio, notificationLevel);
            if (solution.length==0) { cursor++; continue; }
            ZDICT_insertDictItem(dictList, dictListSize, solution, buffer);
            cursor += solution.length;
            DISPLAYUPDATE(2, "\r%4.2f %% \r", (double)cursor / bufferSize * 100);
    }   }

_cleanup:
    free(suffix0);
    free(reverseSuffix);
    free(doneMarks);
    free(filePos);
    return result;
}


static void ZDICT_fillNoise(void* buffer, size_t length)
{
    unsigned const prime1 = 2654435761U;
    unsigned const prime2 = 2246822519U;
    unsigned acc = prime1;
    size_t p=0;;
    for (p=0; p<length; p++) {
        acc *= prime2;
        ((unsigned char*)buffer)[p] = (unsigned char)(acc >> 21);
    }
}


typedef struct
{
    ZSTD_CDict* dict;    /* dictionary */
    ZSTD_CCtx* zc;     /* working context */
    void* workPlace;   /* must be ZSTD_BLOCKSIZE_MAX allocated */
} EStats_ress_t;

#define MAXREPOFFSET 1024

static void ZDICT_countEStats(EStats_ress_t esr, ZSTD_parameters params,
                              unsigned* countLit, unsigned* offsetcodeCount, unsigned* matchlengthCount, unsigned* litlengthCount, U32* repOffsets,
                              const void* src, size_t srcSize,
                              U32 notificationLevel)
{
    size_t const blockSizeMax = MIN (ZSTD_BLOCKSIZE_MAX, 1 << params.cParams.windowLog);
    size_t cSize;

    if (srcSize > blockSizeMax) srcSize = blockSizeMax;   /* protection vs large samples */
    {   size_t const errorCode = ZSTD_compressBegin_usingCDict(esr.zc, esr.dict);
        if (ZSTD_isError(errorCode)) { DISPLAYLEVEL(1, "warning : ZSTD_compressBegin_usingCDict failed \n"); return; }

    }
    cSize = ZSTD_compressBlock(esr.zc, esr.workPlace, ZSTD_BLOCKSIZE_MAX, src, srcSize);
    if (ZSTD_isError(cSize)) { DISPLAYLEVEL(3, "warning : could not compress sample size %u \n", (unsigned)srcSize); return; }

    if (cSize) {  /* if == 0; block is not compressible */
        const seqStore_t* const seqStorePtr = ZSTD_getSeqStore(esr.zc);

        /* literals stats */
        {   const BYTE* bytePtr;
            for(bytePtr = seqStorePtr->litStart; bytePtr < seqStorePtr->lit; bytePtr++)
                countLit[*bytePtr]++;
        }

        /* seqStats */
        {   U32 const nbSeq = (U32)(seqStorePtr->sequences - seqStorePtr->sequencesStart);
            ZSTD_seqToCodes(seqStorePtr);

            {   const BYTE* codePtr = seqStorePtr->ofCode;
                U32 u;
                for (u=0; u<nbSeq; u++) offsetcodeCount[codePtr[u]]++;
            }

            {   const BYTE* codePtr = seqStorePtr->mlCode;
                U32 u;
                for (u=0; u<nbSeq; u++) matchlengthCount[codePtr[u]]++;
            }

            {   const BYTE* codePtr = seqStorePtr->llCode;
                U32 u;
                for (u=0; u<nbSeq; u++) litlengthCount[codePtr[u]]++;
            }

            if (nbSeq >= 2) { /* rep offsets */
                const seqDef* const seq = seqStorePtr->sequencesStart;
                U32 offset1 = seq[0].offset - 3;
                U32 offset2 = seq[1].offset - 3;
                if (offset1 >= MAXREPOFFSET) offset1 = 0;
                if (offset2 >= MAXREPOFFSET) offset2 = 0;
                repOffsets[offset1] += 3;
                repOffsets[offset2] += 1;
    }   }   }
}

static size_t ZDICT_totalSampleSize(const size_t* fileSizes, unsigned nbFiles)
{
    size_t total=0;
    unsigned u;
    for (u=0; u<nbFiles; u++) total += fileSizes[u];
    return total;
}

typedef struct { U32 offset; U32 count; } offsetCount_t;

static void ZDICT_insertSortCount(offsetCount_t table[ZSTD_REP_NUM+1], U32 val, U32 count)
{
    U32 u;
    table[ZSTD_REP_NUM].offset = val;
    table[ZSTD_REP_NUM].count = count;
    for (u=ZSTD_REP_NUM; u>0; u--) {
        offsetCount_t tmp;
        if (table[u-1].count >= table[u].count) break;
        tmp = table[u-1];
        table[u-1] = table[u];
        table[u] = tmp;
    }
}

/* ZDICT_flatLit() :
 * rewrite `countLit` to contain a mostly flat but still compressible distribution of literals.
 * necessary to avoid generating a non-compressible distribution that HUF_writeCTable() cannot encode.
 */
static void ZDICT_flatLit(unsigned* countLit)
{
    int u;
    for (u=1; u<256; u++) countLit[u] = 2;
    countLit[0]   = 4;
    countLit[253] = 1;
    countLit[254] = 1;
}

#define OFFCODE_MAX 30  /* only applicable to first block */
static size_t ZDICT_analyzeEntropy(void*  dstBuffer, size_t maxDstSize,
                                   unsigned compressionLevel,
                             const void*  srcBuffer, const size_t* fileSizes, unsigned nbFiles,
                             const void* dictBuffer, size_t  dictBufferSize,
                                   unsigned notificationLevel)
{
    unsigned countLit[256];
    HUF_CREATE_STATIC_CTABLE(hufTable, 255);
    unsigned offcodeCount[OFFCODE_MAX+1];
    short offcodeNCount[OFFCODE_MAX+1];
    U32 offcodeMax = ZSTD_highbit32((U32)(dictBufferSize + 128 KB));
    unsigned matchLengthCount[MaxML+1];
    short matchLengthNCount[MaxML+1];
    unsigned litLengthCount[MaxLL+1];
    short litLengthNCount[MaxLL+1];
    U32 repOffset[MAXREPOFFSET];
    offsetCount_t bestRepOffset[ZSTD_REP_NUM+1];
    EStats_ress_t esr = { NULL, NULL, NULL };
    ZSTD_parameters params;
    U32 u, huffLog = 11, Offlog = OffFSELog, mlLog = MLFSELog, llLog = LLFSELog, total;
    size_t pos = 0, errorCode;
    size_t eSize = 0;
    size_t const totalSrcSize = ZDICT_totalSampleSize(fileSizes, nbFiles);
    size_t const averageSampleSize = totalSrcSize / (nbFiles + !nbFiles);
    BYTE* dstPtr = (BYTE*)dstBuffer;

    /* init */
    DEBUGLOG(4, "ZDICT_analyzeEntropy");
    if (offcodeMax>OFFCODE_MAX) { eSize = ERROR(dictionaryCreation_failed); goto _cleanup; }   /* too large dictionary */
    for (u=0; u<256; u++) countLit[u] = 1;   /* any character must be described */
    for (u=0; u<=offcodeMax; u++) offcodeCount[u] = 1;
    for (u=0; u<=MaxML; u++) matchLengthCount[u] = 1;
    for (u=0; u<=MaxLL; u++) litLengthCount[u] = 1;
    memset(repOffset, 0, sizeof(repOffset));
    repOffset[1] = repOffset[4] = repOffset[8] = 1;
    memset(bestRepOffset, 0, sizeof(bestRepOffset));
    if (compressionLevel==0) compressionLevel = g_compressionLevel_default;
    params = ZSTD_getParams(compressionLevel, averageSampleSize, dictBufferSize);

    esr.dict = ZSTD_createCDict_advanced(dictBuffer, dictBufferSize, ZSTD_dlm_byRef, ZSTD_dct_rawContent, params.cParams, ZSTD_defaultCMem);
    esr.zc = ZSTD_createCCtx();
    esr.workPlace = malloc(ZSTD_BLOCKSIZE_MAX);
    if (!esr.dict || !esr.zc || !esr.workPlace) {
        eSize = ERROR(memory_allocation);
        DISPLAYLEVEL(1, "Not enough memory \n");
        goto _cleanup;
    }

    /* collect stats on all samples */
    for (u=0; u<nbFiles; u++) {
        ZDICT_countEStats(esr, params,
                          countLit, offcodeCount, matchLengthCount, litLengthCount, repOffset,
                         (const char*)srcBuffer + pos, fileSizes[u],
                          notificationLevel);
        pos += fileSizes[u];
    }

    /* analyze, build stats, starting with literals */
    {   size_t maxNbBits = HUF_buildCTable (hufTable, countLit, 255, huffLog);
        if (HUF_isError(maxNbBits)) {
            eSize = ERROR(GENERIC);
            DISPLAYLEVEL(1, " HUF_buildCTable error \n");
            goto _cleanup;
        }
        if (maxNbBits==8) {  /* not compressible : will fail on HUF_writeCTable() */
            DISPLAYLEVEL(2, "warning : pathological dataset : literals are not compressible : samples are noisy or too regular \n");
            ZDICT_flatLit(countLit);  /* replace distribution by a fake "mostly flat but still compressible" distribution, that HUF_writeCTable() can encode */
            maxNbBits = HUF_buildCTable (hufTable, countLit, 255, huffLog);
            assert(maxNbBits==9);
        }
        huffLog = (U32)maxNbBits;
    }

    /* looking for most common first offsets */
    {   U32 offset;
        for (offset=1; offset<MAXREPOFFSET; offset++)
            ZDICT_insertSortCount(bestRepOffset, offset, repOffset[offset]);
    }
    /* note : the result of this phase should be used to better appreciate the impact on statistics */

    total=0; for (u=0; u<=offcodeMax; u++) total+=offcodeCount[u];
    errorCode = FSE_normalizeCount(offcodeNCount, Offlog, offcodeCount, total, offcodeMax);
    if (FSE_isError(errorCode)) {
        eSize = ERROR(GENERIC);
        DISPLAYLEVEL(1, "FSE_normalizeCount error with offcodeCount \n");
        goto _cleanup;
    }
    Offlog = (U32)errorCode;

    total=0; for (u=0; u<=MaxML; u++) total+=matchLengthCount[u];
    errorCode = FSE_normalizeCount(matchLengthNCount, mlLog, matchLengthCount, total, MaxML);
    if (FSE_isError(errorCode)) {
        eSize = ERROR(GENERIC);
        DISPLAYLEVEL(1, "FSE_normalizeCount error with matchLengthCount \n");
        goto _cleanup;
    }
    mlLog = (U32)errorCode;

    total=0; for (u=0; u<=MaxLL; u++) total+=litLengthCount[u];
    errorCode = FSE_normalizeCount(litLengthNCount, llLog, litLengthCount, total, MaxLL);
    if (FSE_isError(errorCode)) {
        eSize = ERROR(GENERIC);
        DISPLAYLEVEL(1, "FSE_normalizeCount error with litLengthCount \n");
        goto _cleanup;
    }
    llLog = (U32)errorCode;

    /* write result to buffer */
    {   size_t const hhSize = HUF_writeCTable(dstPtr, maxDstSize, hufTable, 255, huffLog);
        if (HUF_isError(hhSize)) {
            eSize = ERROR(GENERIC);
            DISPLAYLEVEL(1, "HUF_writeCTable error \n");
            goto _cleanup;
        }
        dstPtr += hhSize;
        maxDstSize -= hhSize;
        eSize += hhSize;
    }

    {   size_t const ohSize = FSE_writeNCount(dstPtr, maxDstSize, offcodeNCount, OFFCODE_MAX, Offlog);
        if (FSE_isError(ohSize)) {
            eSize = ERROR(GENERIC);
            DISPLAYLEVEL(1, "FSE_writeNCount error with offcodeNCount \n");
            goto _cleanup;
        }
        dstPtr += ohSize;
        maxDstSize -= ohSize;
        eSize += ohSize;
    }

    {   size_t const mhSize = FSE_writeNCount(dstPtr, maxDstSize, matchLengthNCount, MaxML, mlLog);
        if (FSE_isError(mhSize)) {
            eSize = ERROR(GENERIC);
            DISPLAYLEVEL(1, "FSE_writeNCount error with matchLengthNCount \n");
            goto _cleanup;
        }
        dstPtr += mhSize;
        maxDstSize -= mhSize;
        eSize += mhSize;
    }

    {   size_t const lhSize = FSE_writeNCount(dstPtr, maxDstSize, litLengthNCount, MaxLL, llLog);
        if (FSE_isError(lhSize)) {
            eSize = ERROR(GENERIC);
            DISPLAYLEVEL(1, "FSE_writeNCount error with litlengthNCount \n");
            goto _cleanup;
        }
        dstPtr += lhSize;
        maxDstSize -= lhSize;
        eSize += lhSize;
    }

    if (maxDstSize<12) {
        eSize = ERROR(GENERIC);
        DISPLAYLEVEL(1, "not enough space to write RepOffsets \n");
        goto _cleanup;
    }
# if 0
    MEM_writeLE32(dstPtr+0, bestRepOffset[0].offset);
    MEM_writeLE32(dstPtr+4, bestRepOffset[1].offset);
    MEM_writeLE32(dstPtr+8, bestRepOffset[2].offset);
#else
    /* at this stage, we don't use the result of "most common first offset",
       as the impact of statistics is not properly evaluated */
    MEM_writeLE32(dstPtr+0, repStartValue[0]);
    MEM_writeLE32(dstPtr+4, repStartValue[1]);
    MEM_writeLE32(dstPtr+8, repStartValue[2]);
#endif
    eSize += 12;

_cleanup:
    ZSTD_freeCDict(esr.dict);
    ZSTD_freeCCtx(esr.zc);
    free(esr.workPlace);

    return eSize;
}



size_t ZDICT_finalizeDictionary(void* dictBuffer, size_t dictBufferCapacity,
                          const void* customDictContent, size_t dictContentSize,
                          const void* samplesBuffer, const size_t* samplesSizes,
                          unsigned nbSamples, ZDICT_params_t params)
{
    size_t hSize;
#define HBUFFSIZE 256   /* should prove large enough for all entropy headers */
    BYTE header[HBUFFSIZE];
    int const compressionLevel = (params.compressionLevel == 0) ? g_compressionLevel_default : params.compressionLevel;
    U32 const notificationLevel = params.notificationLevel;

    /* check conditions */
    DEBUGLOG(4, "ZDICT_finalizeDictionary");
    if (dictBufferCapacity < dictContentSize) return ERROR(dstSize_tooSmall);
    if (dictContentSize < ZDICT_CONTENTSIZE_MIN) return ERROR(srcSize_wrong);
    if (dictBufferCapacity < ZDICT_DICTSIZE_MIN) return ERROR(dstSize_tooSmall);

    /* dictionary header */
    MEM_writeLE32(header, ZSTD_MAGIC_DICTIONARY);
    {   U64 const randomID = XXH64(customDictContent, dictContentSize, 0);
        U32 const compliantID = (randomID % ((1U<<31)-32768)) + 32768;
        U32 const dictID = params.dictID ? params.dictID : compliantID;
        MEM_writeLE32(header+4, dictID);
    }
    hSize = 8;

    /* entropy tables */
    DISPLAYLEVEL(2, "\r%70s\r", "");   /* clean display line */
    DISPLAYLEVEL(2, "statistics ... \n");
    {   size_t const eSize = ZDICT_analyzeEntropy(header+hSize, HBUFFSIZE-hSize,
                                  compressionLevel,
                                  samplesBuffer, samplesSizes, nbSamples,
                                  customDictContent, dictContentSize,
                                  notificationLevel);
        if (ZDICT_isError(eSize)) return eSize;
        hSize += eSize;
    }

    /* copy elements in final buffer ; note : src and dst buffer can overlap */
    if (hSize + dictContentSize > dictBufferCapacity) dictContentSize = dictBufferCapacity - hSize;
    {   size_t const dictSize = hSize + dictContentSize;
        char* dictEnd = (char*)dictBuffer + dictSize;
        memmove(dictEnd - dictContentSize, customDictContent, dictContentSize);
        memcpy(dictBuffer, header, hSize);
        return dictSize;
    }
}


static size_t ZDICT_addEntropyTablesFromBuffer_advanced(
        void* dictBuffer, size_t dictContentSize, size_t dictBufferCapacity,
        const void* samplesBuffer, const size_t* samplesSizes, unsigned nbSamples,
        ZDICT_params_t params)
{
    int const compressionLevel = (params.compressionLevel == 0) ? g_compressionLevel_default : params.compressionLevel;
    U32 const notificationLevel = params.notificationLevel;
    size_t hSize = 8;

    /* calculate entropy tables */
    DISPLAYLEVEL(2, "\r%70s\r", "");   /* clean display line */
    DISPLAYLEVEL(2, "statistics ... \n");
    {   size_t const eSize = ZDICT_analyzeEntropy((char*)dictBuffer+hSize, dictBufferCapacity-hSize,
                                  compressionLevel,
                                  samplesBuffer, samplesSizes, nbSamples,
                                  (char*)dictBuffer + dictBufferCapacity - dictContentSize, dictContentSize,
                                  notificationLevel);
        if (ZDICT_isError(eSize)) return eSize;
        hSize += eSize;
    }

    /* add dictionary header (after entropy tables) */
    MEM_writeLE32(dictBuffer, ZSTD_MAGIC_DICTIONARY);
    {   U64 const randomID = XXH64((char*)dictBuffer + dictBufferCapacity - dictContentSize, dictContentSize, 0);
        U32 const compliantID = (randomID % ((1U<<31)-32768)) + 32768;
        U32 const dictID = params.dictID ? params.dictID : compliantID;
        MEM_writeLE32((char*)dictBuffer+4, dictID);
    }

    if (hSize + dictContentSize < dictBufferCapacity)
        memmove((char*)dictBuffer + hSize, (char*)dictBuffer + dictBufferCapacity - dictContentSize, dictContentSize);
    return MIN(dictBufferCapacity, hSize+dictContentSize);
}

/* Hidden declaration for dbio.c */
size_t ZDICT_trainFromBuffer_unsafe_legacy(
                            void* dictBuffer, size_t maxDictSize,
                            const void* samplesBuffer, const size_t* samplesSizes, unsigned nbSamples,
                            ZDICT_legacy_params_t params);
/*! ZDICT_trainFromBuffer_unsafe_legacy() :
*   Warning : `samplesBuffer` must be followed by noisy guard band.
*   @return : size of dictionary, or an error code which can be tested with ZDICT_isError()
*/
size_t ZDICT_trainFromBuffer_unsafe_legacy(
                            void* dictBuffer, size_t maxDictSize,
                            const void* samplesBuffer, const size_t* samplesSizes, unsigned nbSamples,
                            ZDICT_legacy_params_t params)
{
    U32 const dictListSize = MAX(MAX(DICTLISTSIZE_DEFAULT, nbSamples), (U32)(maxDictSize/16));
    dictItem* const dictList = (dictItem*)malloc(dictListSize * sizeof(*dictList));
    unsigned const selectivity = params.selectivityLevel == 0 ? g_selectivity_default : params.selectivityLevel;
    unsigned const minRep = (selectivity > 30) ? MINRATIO : nbSamples >> selectivity;
    size_t const targetDictSize = maxDictSize;
    size_t const samplesBuffSize = ZDICT_totalSampleSize(samplesSizes, nbSamples);
    size_t dictSize = 0;
    U32 const notificationLevel = params.zParams.notificationLevel;

    /* checks */
    if (!dictList) return ERROR(memory_allocation);
    if (maxDictSize < ZDICT_DICTSIZE_MIN) { free(dictList); return ERROR(dstSize_tooSmall); }   /* requested dictionary size is too small */
    if (samplesBuffSize < ZDICT_MIN_SAMPLES_SIZE) { free(dictList); return ERROR(dictionaryCreation_failed); }   /* not enough source to create dictionary */

    /* init */
    ZDICT_initDictItem(dictList);

    /* build dictionary */
    ZDICT_trainBuffer_legacy(dictList, dictListSize,
                       samplesBuffer, samplesBuffSize,
                       samplesSizes, nbSamples,
                       minRep, notificationLevel);

    /* display best matches */
    if (params.zParams.notificationLevel>= 3) {
        unsigned const nb = MIN(25, dictList[0].pos);
        unsigned const dictContentSize = ZDICT_dictSize(dictList);
        unsigned u;
        DISPLAYLEVEL(3, "\n %u segments found, of total size %u \n", (unsigned)dictList[0].pos-1, dictContentSize);
        DISPLAYLEVEL(3, "list %u best segments \n", nb-1);
        for (u=1; u<nb; u++) {
            unsigned const pos = dictList[u].pos;
            unsigned const length = dictList[u].length;
            U32 const printedLength = MIN(40, length);
            if ((pos > samplesBuffSize) || ((pos + length) > samplesBuffSize)) {
                free(dictList);
                return ERROR(GENERIC);   /* should never happen */
            }
            DISPLAYLEVEL(3, "%3u:%3u bytes at pos %8u, savings %7u bytes |",
                         u, length, pos, (unsigned)dictList[u].savings);
            ZDICT_printHex((const char*)samplesBuffer+pos, printedLength);
            DISPLAYLEVEL(3, "| \n");
    }   }


    /* create dictionary */
    {   unsigned dictContentSize = ZDICT_dictSize(dictList);
        if (dictContentSize < ZDICT_CONTENTSIZE_MIN) { free(dictList); return ERROR(dictionaryCreation_failed); }   /* dictionary content too small */
        if (dictContentSize < targetDictSize/4) {
            DISPLAYLEVEL(2, "!  warning : selected content significantly smaller than requested (%u < %u) \n", dictContentSize, (unsigned)maxDictSize);
            if (samplesBuffSize < 10 * targetDictSize)
                DISPLAYLEVEL(2, "!  consider increasing the number of samples (total size : %u MB)\n", (unsigned)(samplesBuffSize>>20));
            if (minRep > MINRATIO) {
                DISPLAYLEVEL(2, "!  consider increasing selectivity to produce larger dictionary (-s%u) \n", selectivity+1);
                DISPLAYLEVEL(2, "!  note : larger dictionaries are not necessarily better, test its efficiency on samples \n");
            }
        }

        if ((dictContentSize > targetDictSize*3) && (nbSamples > 2*MINRATIO) && (selectivity>1)) {
            unsigned proposedSelectivity = selectivity-1;
            while ((nbSamples >> proposedSelectivity) <= MINRATIO) { proposedSelectivity--; }
            DISPLAYLEVEL(2, "!  note : calculated dictionary significantly larger than requested (%u > %u) \n", dictContentSize, (unsigned)maxDictSize);
            DISPLAYLEVEL(2, "!  consider increasing dictionary size, or produce denser dictionary (-s%u) \n", proposedSelectivity);
            DISPLAYLEVEL(2, "!  always test dictionary efficiency on real samples \n");
        }

        /* limit dictionary size */
        {   U32 const max = dictList->pos;   /* convention : nb of useful elts within dictList */
            U32 currentSize = 0;
            U32 n; for (n=1; n<max; n++) {
                currentSize += dictList[n].length;
                if (currentSize > targetDictSize) { currentSize -= dictList[n].length; break; }
            }
            dictList->pos = n;
            dictContentSize = currentSize;
        }

        /* build dict content */
        {   U32 u;
            BYTE* ptr = (BYTE*)dictBuffer + maxDictSize;
            for (u=1; u<dictList->pos; u++) {
                U32 l = dictList[u].length;
                ptr -= l;
                if (ptr<(BYTE*)dictBuffer) { free(dictList); return ERROR(GENERIC); }   /* should not happen */
                memcpy(ptr, (const char*)samplesBuffer+dictList[u].pos, l);
        }   }

        dictSize = ZDICT_addEntropyTablesFromBuffer_advanced(dictBuffer, dictContentSize, maxDictSize,
                                                             samplesBuffer, samplesSizes, nbSamples,
                                                             params.zParams);
    }

    /* clean up */
    free(dictList);
    return dictSize;
}


/* ZDICT_trainFromBuffer_legacy() :
 * issue : samplesBuffer need to be followed by a noisy guard band.
 * work around : duplicate the buffer, and add the noise */
size_t ZDICT_trainFromBuffer_legacy(void* dictBuffer, size_t dictBufferCapacity,
                              const void* samplesBuffer, const size_t* samplesSizes, unsigned nbSamples,
                              ZDICT_legacy_params_t params)
{
    size_t result;
    void* newBuff;
    size_t const sBuffSize = ZDICT_totalSampleSize(samplesSizes, nbSamples);
    if (sBuffSize < ZDICT_MIN_SAMPLES_SIZE) return 0;   /* not enough content => no dictionary */

    newBuff = malloc(sBuffSize + NOISELENGTH);
    if (!newBuff) return ERROR(memory_allocation);

    memcpy(newBuff, samplesBuffer, sBuffSize);
    ZDICT_fillNoise((char*)newBuff + sBuffSize, NOISELENGTH);   /* guard band, for end of buffer condition */

    result =
        ZDICT_trainFromBuffer_unsafe_legacy(dictBuffer, dictBufferCapacity, newBuff,
                                            samplesSizes, nbSamples, params);
    free(newBuff);
    return result;
}


size_t ZDICT_trainFromBuffer(void* dictBuffer, size_t dictBufferCapacity,
                             const void* samplesBuffer, const size_t* samplesSizes, unsigned nbSamples)
{
    ZDICT_fastCover_params_t params;
    DEBUGLOG(3, "ZDICT_trainFromBuffer");
    memset(&params, 0, sizeof(params));
    params.d = 8;
    params.steps = 4;
    /* Default to level 6 since no compression level information is available */
    params.zParams.compressionLevel = 3;
#if defined(DEBUGLEVEL) && (DEBUGLEVEL>=1)
    params.zParams.notificationLevel = DEBUGLEVEL;
#endif
    return ZDICT_optimizeTrainFromBuffer_fastCover(dictBuffer, dictBufferCapacity,
                                               samplesBuffer, samplesSizes, nbSamples,
                                               &params);
}

size_t ZDICT_addEntropyTablesFromBuffer(void* dictBuffer, size_t dictContentSize, size_t dictBufferCapacity,
                                  const void* samplesBuffer, const size_t* samplesSizes, unsigned nbSamples)
{
    ZDICT_params_t params;
    memset(&params, 0, sizeof(params));
    return ZDICT_addEntropyTablesFromBuffer_advanced(dictBuffer, dictContentSize, dictBufferCapacity,
                                                     samplesBuffer, samplesSizes, nbSamples,
                                                     params);
}
