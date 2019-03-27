/*
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

#include <stdlib.h>     /* malloc, free */
#include <limits.h>     /* UINT_MAX */
#include <assert.h>

#define XXH_STATIC_LINKING_ONLY
#define XXH_NAMESPACE ZSTD_
#include "xxhash.h"

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include "zstd_errors.h"
#include "mem.h"
#include "zstd_seekable.h"

#define CHECK_Z(f) { size_t const ret = (f); if (ret != 0) return ret; }

#undef ERROR
#define ERROR(name) ((size_t)-ZSTD_error_##name)

#undef MIN
#undef MAX
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct {
    U32 cSize;
    U32 dSize;
    U32 checksum;
} framelogEntry_t;

struct ZSTD_frameLog_s {
    framelogEntry_t* entries;
    U32 size;
    U32 capacity;

    int checksumFlag;

    /* for use when streaming out the seek table */
    U32 seekTablePos;
    U32 seekTableIndex;
} framelog_t;

struct ZSTD_seekable_CStream_s {
    ZSTD_CStream* cstream;
    ZSTD_frameLog framelog;

    U32 frameCSize;
    U32 frameDSize;

    XXH64_state_t xxhState;

    U32 maxFrameSize;

    int writingSeekTable;
};

size_t ZSTD_seekable_frameLog_allocVec(ZSTD_frameLog* fl)
{
    /* allocate some initial space */
    size_t const FRAMELOG_STARTING_CAPACITY = 16;
    fl->entries = (framelogEntry_t*)malloc(
            sizeof(framelogEntry_t) * FRAMELOG_STARTING_CAPACITY);
    if (fl->entries == NULL) return ERROR(memory_allocation);
    fl->capacity = FRAMELOG_STARTING_CAPACITY;

    return 0;
}

size_t ZSTD_seekable_frameLog_freeVec(ZSTD_frameLog* fl)
{
    if (fl != NULL) free(fl->entries);
    return 0;
}

ZSTD_frameLog* ZSTD_seekable_createFrameLog(int checksumFlag)
{
    ZSTD_frameLog* fl = malloc(sizeof(ZSTD_frameLog));
    if (fl == NULL) return NULL;

    if (ZSTD_isError(ZSTD_seekable_frameLog_allocVec(fl))) {
        free(fl);
        return NULL;
    }

    fl->checksumFlag = checksumFlag;
    fl->seekTablePos = 0;
    fl->seekTableIndex = 0;
    fl->size = 0;

    return fl;
}

size_t ZSTD_seekable_freeFrameLog(ZSTD_frameLog* fl)
{
    ZSTD_seekable_frameLog_freeVec(fl);
    free(fl);
    return 0;
}

ZSTD_seekable_CStream* ZSTD_seekable_createCStream()
{
    ZSTD_seekable_CStream* zcs = malloc(sizeof(ZSTD_seekable_CStream));

    if (zcs == NULL) return NULL;

    memset(zcs, 0, sizeof(*zcs));

    zcs->cstream = ZSTD_createCStream();
    if (zcs->cstream == NULL) goto failed1;

    if (ZSTD_isError(ZSTD_seekable_frameLog_allocVec(&zcs->framelog))) goto failed2;

    return zcs;

failed2:
    ZSTD_freeCStream(zcs->cstream);
failed1:
    free(zcs);
    return NULL;
}

size_t ZSTD_seekable_freeCStream(ZSTD_seekable_CStream* zcs)
{
    if (zcs == NULL) return 0; /* support free on null */
    ZSTD_freeCStream(zcs->cstream);
    ZSTD_seekable_frameLog_freeVec(&zcs->framelog);
    free(zcs);

    return 0;
}

size_t ZSTD_seekable_initCStream(ZSTD_seekable_CStream* zcs,
                                 int compressionLevel,
                                 int checksumFlag,
                                 unsigned maxFrameSize)
{
    zcs->framelog.size = 0;
    zcs->frameCSize = 0;
    zcs->frameDSize = 0;

    /* make sure maxFrameSize has a reasonable value */
    if (maxFrameSize > ZSTD_SEEKABLE_MAX_FRAME_DECOMPRESSED_SIZE) {
        return ERROR(frameParameter_unsupported);
    }

    zcs->maxFrameSize = maxFrameSize
                                ? maxFrameSize
                                : ZSTD_SEEKABLE_MAX_FRAME_DECOMPRESSED_SIZE;

    zcs->framelog.checksumFlag = checksumFlag;
    if (zcs->framelog.checksumFlag) {
        XXH64_reset(&zcs->xxhState, 0);
    }

    zcs->framelog.seekTablePos = 0;
    zcs->framelog.seekTableIndex = 0;
    zcs->writingSeekTable = 0;

    return ZSTD_initCStream(zcs->cstream, compressionLevel);
}

size_t ZSTD_seekable_logFrame(ZSTD_frameLog* fl,
                              unsigned compressedSize,
                              unsigned decompressedSize,
                              unsigned checksum)
{
    if (fl->size == ZSTD_SEEKABLE_MAXFRAMES)
        return ERROR(frameIndex_tooLarge);

    /* grow the buffer if required */
    if (fl->size == fl->capacity) {
        /* exponential size increase for constant amortized runtime */
        size_t const newCapacity = fl->capacity * 2;
        framelogEntry_t* const newEntries = realloc(fl->entries,
                sizeof(framelogEntry_t) * newCapacity);

        if (newEntries == NULL) return ERROR(memory_allocation);

        fl->entries = newEntries;
        assert(newCapacity <= UINT_MAX);
        fl->capacity = (U32)newCapacity;
    }

    fl->entries[fl->size] = (framelogEntry_t){
            compressedSize, decompressedSize, checksum
    };
    fl->size++;

    return 0;
}

size_t ZSTD_seekable_endFrame(ZSTD_seekable_CStream* zcs, ZSTD_outBuffer* output)
{
    size_t const prevOutPos = output->pos;
    /* end the frame */
    size_t ret = ZSTD_endStream(zcs->cstream, output);

    zcs->frameCSize += output->pos - prevOutPos;

    /* need to flush before doing the rest */
    if (ret) return ret;

    /* frame done */

    /* store the frame data for later */
    ret = ZSTD_seekable_logFrame(
            &zcs->framelog, zcs->frameCSize, zcs->frameDSize,
            zcs->framelog.checksumFlag
                    ? XXH64_digest(&zcs->xxhState) & 0xFFFFFFFFU
                    : 0);
    if (ret) return ret;

    /* reset for the next frame */
    zcs->frameCSize = 0;
    zcs->frameDSize = 0;

    ZSTD_resetCStream(zcs->cstream, 0);
    if (zcs->framelog.checksumFlag)
        XXH64_reset(&zcs->xxhState, 0);

    return 0;
}

size_t ZSTD_seekable_compressStream(ZSTD_seekable_CStream* zcs, ZSTD_outBuffer* output, ZSTD_inBuffer* input)
{
    const BYTE* const inBase = (const BYTE*) input->src + input->pos;
    size_t inLen = input->size - input->pos;

    inLen = MIN(inLen, (size_t)(zcs->maxFrameSize - zcs->frameDSize));

    /* if we haven't finished flushing the last frame, don't start writing a new one */
    if (inLen > 0) {
        ZSTD_inBuffer inTmp = { inBase, inLen, 0 };
        size_t const prevOutPos = output->pos;

        size_t const ret = ZSTD_compressStream(zcs->cstream, output, &inTmp);

        if (zcs->framelog.checksumFlag) {
            XXH64_update(&zcs->xxhState, inBase, inTmp.pos);
        }

        zcs->frameCSize += output->pos - prevOutPos;
        zcs->frameDSize += inTmp.pos;

        input->pos += inTmp.pos;

        if (ZSTD_isError(ret)) return ret;
    }

    if (zcs->maxFrameSize == zcs->frameDSize) {
        /* log the frame and start over */
        size_t const ret = ZSTD_seekable_endFrame(zcs, output);
        if (ZSTD_isError(ret)) return ret;

        /* get the client ready for the next frame */
        return (size_t)zcs->maxFrameSize;
    }

    return (size_t)(zcs->maxFrameSize - zcs->frameDSize);
}

static inline size_t ZSTD_seekable_seekTableSize(const ZSTD_frameLog* fl)
{
    size_t const sizePerFrame = 8 + (fl->checksumFlag?4:0);
    size_t const seekTableLen = ZSTD_SKIPPABLEHEADERSIZE +
                                sizePerFrame * fl->size +
                                ZSTD_seekTableFooterSize;

    return seekTableLen;
}

static inline size_t ZSTD_stwrite32(ZSTD_frameLog* fl,
                                    ZSTD_outBuffer* output, U32 const value,
                                    U32 const offset)
{
    if (fl->seekTablePos < offset + 4) {
        BYTE tmp[4]; /* so that we can work with buffers too small to write a whole word to */
        size_t const lenWrite =
                MIN(output->size - output->pos, offset + 4 - fl->seekTablePos);
        MEM_writeLE32(tmp, value);
        memcpy((BYTE*)output->dst + output->pos,
               tmp + (fl->seekTablePos - offset), lenWrite);
        output->pos += lenWrite;
        fl->seekTablePos += lenWrite;

        if (lenWrite < 4) return ZSTD_seekable_seekTableSize(fl) - fl->seekTablePos;
    }
    return 0;
}

size_t ZSTD_seekable_writeSeekTable(ZSTD_frameLog* fl, ZSTD_outBuffer* output)
{
    /* seekTableIndex: the current index in the table and
     * seekTableSize: the amount of the table written so far
     *
     * This function is written this way so that if it has to return early
     * because of a small buffer, it can keep going where it left off.
     */

    size_t const sizePerFrame = 8 + (fl->checksumFlag?4:0);
    size_t const seekTableLen = ZSTD_seekable_seekTableSize(fl);

    CHECK_Z(ZSTD_stwrite32(fl, output, ZSTD_MAGIC_SKIPPABLE_START | 0xE, 0));
    assert(seekTableLen <= (size_t)UINT_MAX);
    CHECK_Z(ZSTD_stwrite32(fl, output, (U32)seekTableLen - ZSTD_SKIPPABLEHEADERSIZE, 4));

    while (fl->seekTableIndex < fl->size) {
        unsigned long long const start = ZSTD_SKIPPABLEHEADERSIZE + sizePerFrame * fl->seekTableIndex;
        assert(start + 8 <= UINT_MAX);
        CHECK_Z(ZSTD_stwrite32(fl, output,
                               fl->entries[fl->seekTableIndex].cSize,
                               (U32)start + 0));

        CHECK_Z(ZSTD_stwrite32(fl, output,
                               fl->entries[fl->seekTableIndex].dSize,
                               (U32)start + 4));

        if (fl->checksumFlag) {
            CHECK_Z(ZSTD_stwrite32(
                    fl, output, fl->entries[fl->seekTableIndex].checksum,
                    (U32)start + 8));
        }

        fl->seekTableIndex++;
    }

    assert(seekTableLen <= UINT_MAX);
    CHECK_Z(ZSTD_stwrite32(fl, output, fl->size,
                           (U32)seekTableLen - ZSTD_seekTableFooterSize));

    if (output->size - output->pos < 1) return seekTableLen - fl->seekTablePos;
    if (fl->seekTablePos < seekTableLen - 4) {
        BYTE sfd = 0;
        sfd |= (fl->checksumFlag) << 7;

        ((BYTE*)output->dst)[output->pos] = sfd;
        output->pos++;
        fl->seekTablePos++;
    }

    CHECK_Z(ZSTD_stwrite32(fl, output, ZSTD_SEEKABLE_MAGICNUMBER,
                           (U32)seekTableLen - 4));

    if (fl->seekTablePos != seekTableLen) return ERROR(GENERIC);
    return 0;
}

size_t ZSTD_seekable_endStream(ZSTD_seekable_CStream* zcs, ZSTD_outBuffer* output)
{
    if (!zcs->writingSeekTable && zcs->frameDSize) {
        const size_t endFrame = ZSTD_seekable_endFrame(zcs, output);
        if (ZSTD_isError(endFrame)) return endFrame;
        /* return an accurate size hint */
        if (endFrame) return endFrame + ZSTD_seekable_seekTableSize(&zcs->framelog);
    }

    zcs->writingSeekTable = 1;

    return ZSTD_seekable_writeSeekTable(&zcs->framelog, output);
}
