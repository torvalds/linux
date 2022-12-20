/*
 * Copyright (c) Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_CWKSP_H
#define ZSTD_CWKSP_H

/*-*************************************
*  Dependencies
***************************************/
#include "../common/zstd_internal.h"


/*-*************************************
*  Constants
***************************************/

/* Since the workspace is effectively its own little malloc implementation /
 * arena, when we run under ASAN, we should similarly insert redzones between
 * each internal element of the workspace, so ASAN will catch overruns that
 * reach outside an object but that stay inside the workspace.
 *
 * This defines the size of that redzone.
 */
#ifndef ZSTD_CWKSP_ASAN_REDZONE_SIZE
#define ZSTD_CWKSP_ASAN_REDZONE_SIZE 128
#endif


/* Set our tables and aligneds to align by 64 bytes */
#define ZSTD_CWKSP_ALIGNMENT_BYTES 64

/*-*************************************
*  Structures
***************************************/
typedef enum {
    ZSTD_cwksp_alloc_objects,
    ZSTD_cwksp_alloc_buffers,
    ZSTD_cwksp_alloc_aligned
} ZSTD_cwksp_alloc_phase_e;

/*
 * Used to describe whether the workspace is statically allocated (and will not
 * necessarily ever be freed), or if it's dynamically allocated and we can
 * expect a well-formed caller to free this.
 */
typedef enum {
    ZSTD_cwksp_dynamic_alloc,
    ZSTD_cwksp_static_alloc
} ZSTD_cwksp_static_alloc_e;

/*
 * Zstd fits all its internal datastructures into a single continuous buffer,
 * so that it only needs to perform a single OS allocation (or so that a buffer
 * can be provided to it and it can perform no allocations at all). This buffer
 * is called the workspace.
 *
 * Several optimizations complicate that process of allocating memory ranges
 * from this workspace for each internal datastructure:
 *
 * - These different internal datastructures have different setup requirements:
 *
 *   - The static objects need to be cleared once and can then be trivially
 *     reused for each compression.
 *
 *   - Various buffers don't need to be initialized at all--they are always
 *     written into before they're read.
 *
 *   - The matchstate tables have a unique requirement that they don't need
 *     their memory to be totally cleared, but they do need the memory to have
 *     some bound, i.e., a guarantee that all values in the memory they've been
 *     allocated is less than some maximum value (which is the starting value
 *     for the indices that they will then use for compression). When this
 *     guarantee is provided to them, they can use the memory without any setup
 *     work. When it can't, they have to clear the area.
 *
 * - These buffers also have different alignment requirements.
 *
 * - We would like to reuse the objects in the workspace for multiple
 *   compressions without having to perform any expensive reallocation or
 *   reinitialization work.
 *
 * - We would like to be able to efficiently reuse the workspace across
 *   multiple compressions **even when the compression parameters change** and
 *   we need to resize some of the objects (where possible).
 *
 * To attempt to manage this buffer, given these constraints, the ZSTD_cwksp
 * abstraction was created. It works as follows:
 *
 * Workspace Layout:
 *
 * [                        ... workspace ...                         ]
 * [objects][tables ... ->] free space [<- ... aligned][<- ... buffers]
 *
 * The various objects that live in the workspace are divided into the
 * following categories, and are allocated separately:
 *
 * - Static objects: this is optionally the enclosing ZSTD_CCtx or ZSTD_CDict,
 *   so that literally everything fits in a single buffer. Note: if present,
 *   this must be the first object in the workspace, since ZSTD_customFree{CCtx,
 *   CDict}() rely on a pointer comparison to see whether one or two frees are
 *   required.
 *
 * - Fixed size objects: these are fixed-size, fixed-count objects that are
 *   nonetheless "dynamically" allocated in the workspace so that we can
 *   control how they're initialized separately from the broader ZSTD_CCtx.
 *   Examples:
 *   - Entropy Workspace
 *   - 2 x ZSTD_compressedBlockState_t
 *   - CDict dictionary contents
 *
 * - Tables: these are any of several different datastructures (hash tables,
 *   chain tables, binary trees) that all respect a common format: they are
 *   uint32_t arrays, all of whose values are between 0 and (nextSrc - base).
 *   Their sizes depend on the cparams. These tables are 64-byte aligned.
 *
 * - Aligned: these buffers are used for various purposes that require 4 byte
 *   alignment, but don't require any initialization before they're used. These
 *   buffers are each aligned to 64 bytes.
 *
 * - Buffers: these buffers are used for various purposes that don't require
 *   any alignment or initialization before they're used. This means they can
 *   be moved around at no cost for a new compression.
 *
 * Allocating Memory:
 *
 * The various types of objects must be allocated in order, so they can be
 * correctly packed into the workspace buffer. That order is:
 *
 * 1. Objects
 * 2. Buffers
 * 3. Aligned/Tables
 *
 * Attempts to reserve objects of different types out of order will fail.
 */
typedef struct {
    void* workspace;
    void* workspaceEnd;

    void* objectEnd;
    void* tableEnd;
    void* tableValidEnd;
    void* allocStart;

    BYTE allocFailed;
    int workspaceOversizedDuration;
    ZSTD_cwksp_alloc_phase_e phase;
    ZSTD_cwksp_static_alloc_e isStatic;
} ZSTD_cwksp;

/*-*************************************
*  Functions
***************************************/

MEM_STATIC size_t ZSTD_cwksp_available_space(ZSTD_cwksp* ws);

MEM_STATIC void ZSTD_cwksp_assert_internal_consistency(ZSTD_cwksp* ws) {
    (void)ws;
    assert(ws->workspace <= ws->objectEnd);
    assert(ws->objectEnd <= ws->tableEnd);
    assert(ws->objectEnd <= ws->tableValidEnd);
    assert(ws->tableEnd <= ws->allocStart);
    assert(ws->tableValidEnd <= ws->allocStart);
    assert(ws->allocStart <= ws->workspaceEnd);
}

/*
 * Align must be a power of 2.
 */
MEM_STATIC size_t ZSTD_cwksp_align(size_t size, size_t const align) {
    size_t const mask = align - 1;
    assert((align & mask) == 0);
    return (size + mask) & ~mask;
}

/*
 * Use this to determine how much space in the workspace we will consume to
 * allocate this object. (Normally it should be exactly the size of the object,
 * but under special conditions, like ASAN, where we pad each object, it might
 * be larger.)
 *
 * Since tables aren't currently redzoned, you don't need to call through this
 * to figure out how much space you need for the matchState tables. Everything
 * else is though.
 *
 * Do not use for sizing aligned buffers. Instead, use ZSTD_cwksp_aligned_alloc_size().
 */
MEM_STATIC size_t ZSTD_cwksp_alloc_size(size_t size) {
    if (size == 0)
        return 0;
    return size;
}

/*
 * Returns an adjusted alloc size that is the nearest larger multiple of 64 bytes.
 * Used to determine the number of bytes required for a given "aligned".
 */
MEM_STATIC size_t ZSTD_cwksp_aligned_alloc_size(size_t size) {
    return ZSTD_cwksp_alloc_size(ZSTD_cwksp_align(size, ZSTD_CWKSP_ALIGNMENT_BYTES));
}

/*
 * Returns the amount of additional space the cwksp must allocate
 * for internal purposes (currently only alignment).
 */
MEM_STATIC size_t ZSTD_cwksp_slack_space_required(void) {
    /* For alignment, the wksp will always allocate an additional n_1=[1, 64] bytes
     * to align the beginning of tables section, as well as another n_2=[0, 63] bytes
     * to align the beginning of the aligned section.
     *
     * n_1 + n_2 == 64 bytes if the cwksp is freshly allocated, due to tables and
     * aligneds being sized in multiples of 64 bytes.
     */
    size_t const slackSpace = ZSTD_CWKSP_ALIGNMENT_BYTES;
    return slackSpace;
}


/*
 * Return the number of additional bytes required to align a pointer to the given number of bytes.
 * alignBytes must be a power of two.
 */
MEM_STATIC size_t ZSTD_cwksp_bytes_to_align_ptr(void* ptr, const size_t alignBytes) {
    size_t const alignBytesMask = alignBytes - 1;
    size_t const bytes = (alignBytes - ((size_t)ptr & (alignBytesMask))) & alignBytesMask;
    assert((alignBytes & alignBytesMask) == 0);
    assert(bytes != ZSTD_CWKSP_ALIGNMENT_BYTES);
    return bytes;
}

/*
 * Internal function. Do not use directly.
 * Reserves the given number of bytes within the aligned/buffer segment of the wksp,
 * which counts from the end of the wksp (as opposed to the object/table segment).
 *
 * Returns a pointer to the beginning of that space.
 */
MEM_STATIC void*
ZSTD_cwksp_reserve_internal_buffer_space(ZSTD_cwksp* ws, size_t const bytes)
{
    void* const alloc = (BYTE*)ws->allocStart - bytes;
    void* const bottom = ws->tableEnd;
    DEBUGLOG(5, "cwksp: reserving %p %zd bytes, %zd bytes remaining",
        alloc, bytes, ZSTD_cwksp_available_space(ws) - bytes);
    ZSTD_cwksp_assert_internal_consistency(ws);
    assert(alloc >= bottom);
    if (alloc < bottom) {
        DEBUGLOG(4, "cwksp: alloc failed!");
        ws->allocFailed = 1;
        return NULL;
    }
    /* the area is reserved from the end of wksp.
     * If it overlaps with tableValidEnd, it voids guarantees on values' range */
    if (alloc < ws->tableValidEnd) {
        ws->tableValidEnd = alloc;
    }
    ws->allocStart = alloc;
    return alloc;
}

/*
 * Moves the cwksp to the next phase, and does any necessary allocations.
 * cwksp initialization must necessarily go through each phase in order.
 * Returns a 0 on success, or zstd error
 */
MEM_STATIC size_t
ZSTD_cwksp_internal_advance_phase(ZSTD_cwksp* ws, ZSTD_cwksp_alloc_phase_e phase)
{
    assert(phase >= ws->phase);
    if (phase > ws->phase) {
        /* Going from allocating objects to allocating buffers */
        if (ws->phase < ZSTD_cwksp_alloc_buffers &&
                phase >= ZSTD_cwksp_alloc_buffers) {
            ws->tableValidEnd = ws->objectEnd;
        }

        /* Going from allocating buffers to allocating aligneds/tables */
        if (ws->phase < ZSTD_cwksp_alloc_aligned &&
                phase >= ZSTD_cwksp_alloc_aligned) {
            {   /* Align the start of the "aligned" to 64 bytes. Use [1, 64] bytes. */
                size_t const bytesToAlign =
                    ZSTD_CWKSP_ALIGNMENT_BYTES - ZSTD_cwksp_bytes_to_align_ptr(ws->allocStart, ZSTD_CWKSP_ALIGNMENT_BYTES);
                DEBUGLOG(5, "reserving aligned alignment addtl space: %zu", bytesToAlign);
                ZSTD_STATIC_ASSERT((ZSTD_CWKSP_ALIGNMENT_BYTES & (ZSTD_CWKSP_ALIGNMENT_BYTES - 1)) == 0); /* power of 2 */
                RETURN_ERROR_IF(!ZSTD_cwksp_reserve_internal_buffer_space(ws, bytesToAlign),
                                memory_allocation, "aligned phase - alignment initial allocation failed!");
            }
            {   /* Align the start of the tables to 64 bytes. Use [0, 63] bytes */
                void* const alloc = ws->objectEnd;
                size_t const bytesToAlign = ZSTD_cwksp_bytes_to_align_ptr(alloc, ZSTD_CWKSP_ALIGNMENT_BYTES);
                void* const objectEnd = (BYTE*)alloc + bytesToAlign;
                DEBUGLOG(5, "reserving table alignment addtl space: %zu", bytesToAlign);
                RETURN_ERROR_IF(objectEnd > ws->workspaceEnd, memory_allocation,
                                "table phase - alignment initial allocation failed!");
                ws->objectEnd = objectEnd;
                ws->tableEnd = objectEnd;  /* table area starts being empty */
                if (ws->tableValidEnd < ws->tableEnd) {
                    ws->tableValidEnd = ws->tableEnd;
        }   }   }
        ws->phase = phase;
        ZSTD_cwksp_assert_internal_consistency(ws);
    }
    return 0;
}

/*
 * Returns whether this object/buffer/etc was allocated in this workspace.
 */
MEM_STATIC int ZSTD_cwksp_owns_buffer(const ZSTD_cwksp* ws, const void* ptr)
{
    return (ptr != NULL) && (ws->workspace <= ptr) && (ptr <= ws->workspaceEnd);
}

/*
 * Internal function. Do not use directly.
 */
MEM_STATIC void*
ZSTD_cwksp_reserve_internal(ZSTD_cwksp* ws, size_t bytes, ZSTD_cwksp_alloc_phase_e phase)
{
    void* alloc;
    if (ZSTD_isError(ZSTD_cwksp_internal_advance_phase(ws, phase)) || bytes == 0) {
        return NULL;
    }


    alloc = ZSTD_cwksp_reserve_internal_buffer_space(ws, bytes);


    return alloc;
}

/*
 * Reserves and returns unaligned memory.
 */
MEM_STATIC BYTE* ZSTD_cwksp_reserve_buffer(ZSTD_cwksp* ws, size_t bytes)
{
    return (BYTE*)ZSTD_cwksp_reserve_internal(ws, bytes, ZSTD_cwksp_alloc_buffers);
}

/*
 * Reserves and returns memory sized on and aligned on ZSTD_CWKSP_ALIGNMENT_BYTES (64 bytes).
 */
MEM_STATIC void* ZSTD_cwksp_reserve_aligned(ZSTD_cwksp* ws, size_t bytes)
{
    void* ptr = ZSTD_cwksp_reserve_internal(ws, ZSTD_cwksp_align(bytes, ZSTD_CWKSP_ALIGNMENT_BYTES),
                                            ZSTD_cwksp_alloc_aligned);
    assert(((size_t)ptr & (ZSTD_CWKSP_ALIGNMENT_BYTES-1))== 0);
    return ptr;
}

/*
 * Aligned on 64 bytes. These buffers have the special property that
 * their values remain constrained, allowing us to re-use them without
 * memset()-ing them.
 */
MEM_STATIC void* ZSTD_cwksp_reserve_table(ZSTD_cwksp* ws, size_t bytes)
{
    const ZSTD_cwksp_alloc_phase_e phase = ZSTD_cwksp_alloc_aligned;
    void* alloc;
    void* end;
    void* top;

    if (ZSTD_isError(ZSTD_cwksp_internal_advance_phase(ws, phase))) {
        return NULL;
    }
    alloc = ws->tableEnd;
    end = (BYTE *)alloc + bytes;
    top = ws->allocStart;

    DEBUGLOG(5, "cwksp: reserving %p table %zd bytes, %zd bytes remaining",
        alloc, bytes, ZSTD_cwksp_available_space(ws) - bytes);
    assert((bytes & (sizeof(U32)-1)) == 0);
    ZSTD_cwksp_assert_internal_consistency(ws);
    assert(end <= top);
    if (end > top) {
        DEBUGLOG(4, "cwksp: table alloc failed!");
        ws->allocFailed = 1;
        return NULL;
    }
    ws->tableEnd = end;


    assert((bytes & (ZSTD_CWKSP_ALIGNMENT_BYTES-1)) == 0);
    assert(((size_t)alloc & (ZSTD_CWKSP_ALIGNMENT_BYTES-1))== 0);
    return alloc;
}

/*
 * Aligned on sizeof(void*).
 * Note : should happen only once, at workspace first initialization
 */
MEM_STATIC void* ZSTD_cwksp_reserve_object(ZSTD_cwksp* ws, size_t bytes)
{
    size_t const roundedBytes = ZSTD_cwksp_align(bytes, sizeof(void*));
    void* alloc = ws->objectEnd;
    void* end = (BYTE*)alloc + roundedBytes;


    DEBUGLOG(4,
        "cwksp: reserving %p object %zd bytes (rounded to %zd), %zd bytes remaining",
        alloc, bytes, roundedBytes, ZSTD_cwksp_available_space(ws) - roundedBytes);
    assert((size_t)alloc % ZSTD_ALIGNOF(void*) == 0);
    assert(bytes % ZSTD_ALIGNOF(void*) == 0);
    ZSTD_cwksp_assert_internal_consistency(ws);
    /* we must be in the first phase, no advance is possible */
    if (ws->phase != ZSTD_cwksp_alloc_objects || end > ws->workspaceEnd) {
        DEBUGLOG(3, "cwksp: object alloc failed!");
        ws->allocFailed = 1;
        return NULL;
    }
    ws->objectEnd = end;
    ws->tableEnd = end;
    ws->tableValidEnd = end;


    return alloc;
}

MEM_STATIC void ZSTD_cwksp_mark_tables_dirty(ZSTD_cwksp* ws)
{
    DEBUGLOG(4, "cwksp: ZSTD_cwksp_mark_tables_dirty");


    assert(ws->tableValidEnd >= ws->objectEnd);
    assert(ws->tableValidEnd <= ws->allocStart);
    ws->tableValidEnd = ws->objectEnd;
    ZSTD_cwksp_assert_internal_consistency(ws);
}

MEM_STATIC void ZSTD_cwksp_mark_tables_clean(ZSTD_cwksp* ws) {
    DEBUGLOG(4, "cwksp: ZSTD_cwksp_mark_tables_clean");
    assert(ws->tableValidEnd >= ws->objectEnd);
    assert(ws->tableValidEnd <= ws->allocStart);
    if (ws->tableValidEnd < ws->tableEnd) {
        ws->tableValidEnd = ws->tableEnd;
    }
    ZSTD_cwksp_assert_internal_consistency(ws);
}

/*
 * Zero the part of the allocated tables not already marked clean.
 */
MEM_STATIC void ZSTD_cwksp_clean_tables(ZSTD_cwksp* ws) {
    DEBUGLOG(4, "cwksp: ZSTD_cwksp_clean_tables");
    assert(ws->tableValidEnd >= ws->objectEnd);
    assert(ws->tableValidEnd <= ws->allocStart);
    if (ws->tableValidEnd < ws->tableEnd) {
        ZSTD_memset(ws->tableValidEnd, 0, (BYTE*)ws->tableEnd - (BYTE*)ws->tableValidEnd);
    }
    ZSTD_cwksp_mark_tables_clean(ws);
}

/*
 * Invalidates table allocations.
 * All other allocations remain valid.
 */
MEM_STATIC void ZSTD_cwksp_clear_tables(ZSTD_cwksp* ws) {
    DEBUGLOG(4, "cwksp: clearing tables!");


    ws->tableEnd = ws->objectEnd;
    ZSTD_cwksp_assert_internal_consistency(ws);
}

/*
 * Invalidates all buffer, aligned, and table allocations.
 * Object allocations remain valid.
 */
MEM_STATIC void ZSTD_cwksp_clear(ZSTD_cwksp* ws) {
    DEBUGLOG(4, "cwksp: clearing!");



    ws->tableEnd = ws->objectEnd;
    ws->allocStart = ws->workspaceEnd;
    ws->allocFailed = 0;
    if (ws->phase > ZSTD_cwksp_alloc_buffers) {
        ws->phase = ZSTD_cwksp_alloc_buffers;
    }
    ZSTD_cwksp_assert_internal_consistency(ws);
}

/*
 * The provided workspace takes ownership of the buffer [start, start+size).
 * Any existing values in the workspace are ignored (the previously managed
 * buffer, if present, must be separately freed).
 */
MEM_STATIC void ZSTD_cwksp_init(ZSTD_cwksp* ws, void* start, size_t size, ZSTD_cwksp_static_alloc_e isStatic) {
    DEBUGLOG(4, "cwksp: init'ing workspace with %zd bytes", size);
    assert(((size_t)start & (sizeof(void*)-1)) == 0); /* ensure correct alignment */
    ws->workspace = start;
    ws->workspaceEnd = (BYTE*)start + size;
    ws->objectEnd = ws->workspace;
    ws->tableValidEnd = ws->objectEnd;
    ws->phase = ZSTD_cwksp_alloc_objects;
    ws->isStatic = isStatic;
    ZSTD_cwksp_clear(ws);
    ws->workspaceOversizedDuration = 0;
    ZSTD_cwksp_assert_internal_consistency(ws);
}

MEM_STATIC size_t ZSTD_cwksp_create(ZSTD_cwksp* ws, size_t size, ZSTD_customMem customMem) {
    void* workspace = ZSTD_customMalloc(size, customMem);
    DEBUGLOG(4, "cwksp: creating new workspace with %zd bytes", size);
    RETURN_ERROR_IF(workspace == NULL, memory_allocation, "NULL pointer!");
    ZSTD_cwksp_init(ws, workspace, size, ZSTD_cwksp_dynamic_alloc);
    return 0;
}

MEM_STATIC void ZSTD_cwksp_free(ZSTD_cwksp* ws, ZSTD_customMem customMem) {
    void *ptr = ws->workspace;
    DEBUGLOG(4, "cwksp: freeing workspace");
    ZSTD_memset(ws, 0, sizeof(ZSTD_cwksp));
    ZSTD_customFree(ptr, customMem);
}

/*
 * Moves the management of a workspace from one cwksp to another. The src cwksp
 * is left in an invalid state (src must be re-init()'ed before it's used again).
 */
MEM_STATIC void ZSTD_cwksp_move(ZSTD_cwksp* dst, ZSTD_cwksp* src) {
    *dst = *src;
    ZSTD_memset(src, 0, sizeof(ZSTD_cwksp));
}

MEM_STATIC size_t ZSTD_cwksp_sizeof(const ZSTD_cwksp* ws) {
    return (size_t)((BYTE*)ws->workspaceEnd - (BYTE*)ws->workspace);
}

MEM_STATIC size_t ZSTD_cwksp_used(const ZSTD_cwksp* ws) {
    return (size_t)((BYTE*)ws->tableEnd - (BYTE*)ws->workspace)
         + (size_t)((BYTE*)ws->workspaceEnd - (BYTE*)ws->allocStart);
}

MEM_STATIC int ZSTD_cwksp_reserve_failed(const ZSTD_cwksp* ws) {
    return ws->allocFailed;
}

/*-*************************************
*  Functions Checking Free Space
***************************************/

/* ZSTD_alignmentSpaceWithinBounds() :
 * Returns if the estimated space needed for a wksp is within an acceptable limit of the
 * actual amount of space used.
 */
MEM_STATIC int ZSTD_cwksp_estimated_space_within_bounds(const ZSTD_cwksp* const ws,
                                                        size_t const estimatedSpace, int resizedWorkspace) {
    if (resizedWorkspace) {
        /* Resized/newly allocated wksp should have exact bounds */
        return ZSTD_cwksp_used(ws) == estimatedSpace;
    } else {
        /* Due to alignment, when reusing a workspace, we can actually consume 63 fewer or more bytes
         * than estimatedSpace. See the comments in zstd_cwksp.h for details.
         */
        return (ZSTD_cwksp_used(ws) >= estimatedSpace - 63) && (ZSTD_cwksp_used(ws) <= estimatedSpace + 63);
    }
}


MEM_STATIC size_t ZSTD_cwksp_available_space(ZSTD_cwksp* ws) {
    return (size_t)((BYTE*)ws->allocStart - (BYTE*)ws->tableEnd);
}

MEM_STATIC int ZSTD_cwksp_check_available(ZSTD_cwksp* ws, size_t additionalNeededSpace) {
    return ZSTD_cwksp_available_space(ws) >= additionalNeededSpace;
}

MEM_STATIC int ZSTD_cwksp_check_too_large(ZSTD_cwksp* ws, size_t additionalNeededSpace) {
    return ZSTD_cwksp_check_available(
        ws, additionalNeededSpace * ZSTD_WORKSPACETOOLARGE_FACTOR);
}

MEM_STATIC int ZSTD_cwksp_check_wasteful(ZSTD_cwksp* ws, size_t additionalNeededSpace) {
    return ZSTD_cwksp_check_too_large(ws, additionalNeededSpace)
        && ws->workspaceOversizedDuration > ZSTD_WORKSPACETOOLARGE_MAXDURATION;
}

MEM_STATIC void ZSTD_cwksp_bump_oversized_duration(
        ZSTD_cwksp* ws, size_t additionalNeededSpace) {
    if (ZSTD_cwksp_check_too_large(ws, additionalNeededSpace)) {
        ws->workspaceOversizedDuration++;
    } else {
        ws->workspaceOversizedDuration = 0;
    }
}


#endif /* ZSTD_CWKSP_H */
