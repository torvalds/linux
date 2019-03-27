/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */



/*-************************************
*  Dependencies
**************************************/
#include "datagen.h"
#include "platform.h"  /* SET_BINARY_MODE */
#include <stdlib.h>    /* malloc, free */
#include <stdio.h>     /* FILE, fwrite, fprintf */
#include <string.h>    /* memcpy */
#include "mem.h"       /* U32 */


/*-************************************
*  Macros
**************************************/
#define KB *(1 <<10)
#define MIN(a,b)  ( (a) < (b) ? (a) : (b) )

#define RDG_DEBUG 0
#define TRACE(...)   if (RDG_DEBUG) fprintf(stderr, __VA_ARGS__ )


/*-************************************
*  Local constants
**************************************/
#define LTLOG 13
#define LTSIZE (1<<LTLOG)
#define LTMASK (LTSIZE-1)


/*-*******************************************************
*  Local Functions
*********************************************************/
#define RDG_rotl32(x,r) ((x << r) | (x >> (32 - r)))
static U32 RDG_rand(U32* src)
{
    static const U32 prime1 = 2654435761U;
    static const U32 prime2 = 2246822519U;
    U32 rand32 = *src;
    rand32 *= prime1;
    rand32 ^= prime2;
    rand32  = RDG_rotl32(rand32, 13);
    *src = rand32;
    return rand32 >> 5;
}


static void RDG_fillLiteralDistrib(BYTE* ldt, double ld)
{
    BYTE const firstChar = (ld<=0.0) ?   0 : '(';
    BYTE const lastChar  = (ld<=0.0) ? 255 : '}';
    BYTE character = (ld<=0.0) ? 0 : '0';
    U32 u;

    if (ld<=0.0) ld = 0.0;
    for (u=0; u<LTSIZE; ) {
        U32 const weight = (U32)((double)(LTSIZE - u) * ld) + 1;
        U32 const end = MIN ( u + weight , LTSIZE);
        while (u < end) ldt[u++] = character;
        character++;
        if (character > lastChar) character = firstChar;
    }
}


static BYTE RDG_genChar(U32* seed, const BYTE* ldt)
{
    U32 const id = RDG_rand(seed) & LTMASK;
    return ldt[id];  /* memory-sanitizer fails here, stating "uninitialized value" when table initialized with P==0.0. Checked : table is fully initialized */
}


static U32 RDG_rand15Bits (U32* seedPtr)
{
    return RDG_rand(seedPtr) & 0x7FFF;
}

static U32 RDG_randLength(U32* seedPtr)
{
    if (RDG_rand(seedPtr) & 7) return (RDG_rand(seedPtr) & 0xF);   /* small length */
    return (RDG_rand(seedPtr) & 0x1FF) + 0xF;
}

static void RDG_genBlock(void* buffer, size_t buffSize, size_t prefixSize, double matchProba, const BYTE* ldt, U32* seedPtr)
{
    BYTE* const buffPtr = (BYTE*)buffer;
    U32 const matchProba32 = (U32)(32768 * matchProba);
    size_t pos = prefixSize;
    U32 prevOffset = 1;

    /* special case : sparse content */
    while (matchProba >= 1.0) {
        size_t size0 = RDG_rand(seedPtr) & 3;
        size0  = (size_t)1 << (16 + size0 * 2);
        size0 += RDG_rand(seedPtr) & (size0-1);   /* because size0 is power of 2*/
        if (buffSize < pos + size0) {
            memset(buffPtr+pos, 0, buffSize-pos);
            return;
        }
        memset(buffPtr+pos, 0, size0);
        pos += size0;
        buffPtr[pos-1] = RDG_genChar(seedPtr, ldt);
        continue;
    }

    /* init */
    if (pos==0) buffPtr[0] = RDG_genChar(seedPtr, ldt), pos=1;

    /* Generate compressible data */
    while (pos < buffSize) {
        /* Select : Literal (char) or Match (within 32K) */
        if (RDG_rand15Bits(seedPtr) < matchProba32) {
            /* Copy (within 32K) */
            U32 const length = RDG_randLength(seedPtr) + 4;
            U32 const d = (U32) MIN(pos + length , buffSize);
            U32 const repeatOffset = (RDG_rand(seedPtr) & 15) == 2;
            U32 const randOffset = RDG_rand15Bits(seedPtr) + 1;
            U32 const offset = repeatOffset ? prevOffset : (U32) MIN(randOffset , pos);
            size_t match = pos - offset;
            while (pos < d) buffPtr[pos++] = buffPtr[match++];   /* correctly manages overlaps */
            prevOffset = offset;
        } else {
            /* Literal (noise) */
            U32 const length = RDG_randLength(seedPtr);
            U32 const d = (U32) MIN(pos + length, buffSize);
            while (pos < d) buffPtr[pos++] = RDG_genChar(seedPtr, ldt);
    }   }
}


void RDG_genBuffer(void* buffer, size_t size, double matchProba, double litProba, unsigned seed)
{
    U32 seed32 = seed;
    BYTE ldt[LTSIZE];
    memset(ldt, '0', sizeof(ldt));  /* yes, character '0', this is intentional */
    if (litProba<=0.0) litProba = matchProba / 4.5;
    RDG_fillLiteralDistrib(ldt, litProba);
    RDG_genBlock(buffer, size, 0, matchProba, ldt, &seed32);
}


void RDG_genStdout(unsigned long long size, double matchProba, double litProba, unsigned seed)
{
    U32 seed32 = seed;
    size_t const stdBlockSize = 128 KB;
    size_t const stdDictSize = 32 KB;
    BYTE* const buff = (BYTE*)malloc(stdDictSize + stdBlockSize);
    U64 total = 0;
    BYTE ldt[LTSIZE];   /* literals distribution table */

    /* init */
    if (buff==NULL) { perror("datagen"); exit(1); }
    if (litProba<=0.0) litProba = matchProba / 4.5;
    memset(ldt, '0', sizeof(ldt));   /* yes, character '0', this is intentional */
    RDG_fillLiteralDistrib(ldt, litProba);
    SET_BINARY_MODE(stdout);

    /* Generate initial dict */
    RDG_genBlock(buff, stdDictSize, 0, matchProba, ldt, &seed32);

    /* Generate compressible data */
    while (total < size) {
        size_t const genBlockSize = (size_t) (MIN (stdBlockSize, size-total));
        RDG_genBlock(buff, stdDictSize+stdBlockSize, stdDictSize, matchProba, ldt, &seed32);
        total += genBlockSize;
        { size_t const unused = fwrite(buff, 1, genBlockSize, stdout); (void)unused; }
        /* update dict */
        memcpy(buff, buff + stdBlockSize, stdDictSize);
    }

    /* cleanup */
    free(buff);
}
