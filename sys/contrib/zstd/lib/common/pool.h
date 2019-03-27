/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef POOL_H
#define POOL_H

#if defined (__cplusplus)
extern "C" {
#endif


#include <stddef.h>   /* size_t */
#define ZSTD_STATIC_LINKING_ONLY   /* ZSTD_customMem */
#include "zstd.h"

typedef struct POOL_ctx_s POOL_ctx;

/*! POOL_create() :
 *  Create a thread pool with at most `numThreads` threads.
 * `numThreads` must be at least 1.
 *  The maximum number of queued jobs before blocking is `queueSize`.
 * @return : POOL_ctx pointer on success, else NULL.
*/
POOL_ctx* POOL_create(size_t numThreads, size_t queueSize);

POOL_ctx* POOL_create_advanced(size_t numThreads, size_t queueSize,
                               ZSTD_customMem customMem);

/*! POOL_free() :
 *  Free a thread pool returned by POOL_create().
 */
void POOL_free(POOL_ctx* ctx);

/*! POOL_resize() :
 *  Expands or shrinks pool's number of threads.
 *  This is more efficient than releasing + creating a new context,
 *  since it tries to preserve and re-use existing threads.
 * `numThreads` must be at least 1.
 * @return : 0 when resize was successful,
 *           !0 (typically 1) if there is an error.
 *    note : only numThreads can be resized, queueSize remains unchanged.
 */
int POOL_resize(POOL_ctx* ctx, size_t numThreads);

/*! POOL_sizeof() :
 * @return threadpool memory usage
 *  note : compatible with NULL (returns 0 in this case)
 */
size_t POOL_sizeof(POOL_ctx* ctx);

/*! POOL_function :
 *  The function type that can be added to a thread pool.
 */
typedef void (*POOL_function)(void*);

/*! POOL_add() :
 *  Add the job `function(opaque)` to the thread pool. `ctx` must be valid.
 *  Possibly blocks until there is room in the queue.
 *  Note : The function may be executed asynchronously,
 *         therefore, `opaque` must live until function has been completed.
 */
void POOL_add(POOL_ctx* ctx, POOL_function function, void* opaque);


/*! POOL_tryAdd() :
 *  Add the job `function(opaque)` to thread pool _if_ a worker is available.
 *  Returns immediately even if not (does not block).
 * @return : 1 if successful, 0 if not.
 */
int POOL_tryAdd(POOL_ctx* ctx, POOL_function function, void* opaque);


#if defined (__cplusplus)
}
#endif

#endif
