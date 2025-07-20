/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/* ******************************************************************
 * hist : Histogram functions
 * part of Finite State Entropy project
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 *  You can contact the author at :
 *  - FSE source repository : https://github.com/Cyan4973/FiniteStateEntropy
 *  - Public forum : https://groups.google.com/forum/#!forum/lz4c
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
****************************************************************** */

/* --- dependencies --- */
#include "../common/zstd_deps.h"   /* size_t */


/* --- simple histogram functions --- */

/*! HIST_count():
 *  Provides the precise count of each byte within a table 'count'.
 * 'count' is a table of unsigned int, of minimum size (*maxSymbolValuePtr+1).
 *  Updates *maxSymbolValuePtr with actual largest symbol value detected.
 * @return : count of the most frequent symbol (which isn't identified).
 *           or an error code, which can be tested using HIST_isError().
 *           note : if return == srcSize, there is only one symbol.
 */
size_t HIST_count(unsigned* count, unsigned* maxSymbolValuePtr,
                  const void* src, size_t srcSize);

unsigned HIST_isError(size_t code);  /*< tells if a return value is an error code */


/* --- advanced histogram functions --- */

#define HIST_WKSP_SIZE_U32 1024
#define HIST_WKSP_SIZE    (HIST_WKSP_SIZE_U32 * sizeof(unsigned))
/* HIST_count_wksp() :
 *  Same as HIST_count(), but using an externally provided scratch buffer.
 *  Benefit is this function will use very little stack space.
 * `workSpace` is a writable buffer which must be 4-bytes aligned,
 * `workSpaceSize` must be >= HIST_WKSP_SIZE
 */
size_t HIST_count_wksp(unsigned* count, unsigned* maxSymbolValuePtr,
                       const void* src, size_t srcSize,
                       void* workSpace, size_t workSpaceSize);

/* HIST_countFast() :
 *  same as HIST_count(), but blindly trusts that all byte values within src are <= *maxSymbolValuePtr.
 *  This function is unsafe, and will segfault if any value within `src` is `> *maxSymbolValuePtr`
 */
size_t HIST_countFast(unsigned* count, unsigned* maxSymbolValuePtr,
                      const void* src, size_t srcSize);

/* HIST_countFast_wksp() :
 *  Same as HIST_countFast(), but using an externally provided scratch buffer.
 * `workSpace` is a writable buffer which must be 4-bytes aligned,
 * `workSpaceSize` must be >= HIST_WKSP_SIZE
 */
size_t HIST_countFast_wksp(unsigned* count, unsigned* maxSymbolValuePtr,
                           const void* src, size_t srcSize,
                           void* workSpace, size_t workSpaceSize);

/*! HIST_count_simple() :
 *  Same as HIST_countFast(), this function is unsafe,
 *  and will segfault if any value within `src` is `> *maxSymbolValuePtr`.
 *  It is also a bit slower for large inputs.
 *  However, it does not need any additional memory (not even on stack).
 * @return : count of the most frequent symbol.
 *  Note this function doesn't produce any error (i.e. it must succeed).
 */
unsigned HIST_count_simple(unsigned* count, unsigned* maxSymbolValuePtr,
                           const void* src, size_t srcSize);

/*! HIST_add() :
 *  Lowest level: just add nb of occurrences of characters from @src into @count.
 *  @count is not reset. @count array is presumed large enough (i.e. 1 KB).
 @  This function does not need any additional stack memory.
 */
void HIST_add(unsigned* count, const void* src, size_t srcSize);
