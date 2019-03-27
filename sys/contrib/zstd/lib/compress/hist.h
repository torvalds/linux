/* ******************************************************************
   hist : Histogram functions
   part of Finite State Entropy project
   Copyright (C) 2013-present, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - FSE source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */

/* --- dependencies --- */
#include <stddef.h>   /* size_t */


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

unsigned HIST_isError(size_t code);  /**< tells if a return value is an error code */


/* --- advanced histogram functions --- */

#define HIST_WKSP_SIZE_U32 1024
#define HIST_WKSP_SIZE    (HIST_WKSP_SIZE_U32 * sizeof(unsigned))
/** HIST_count_wksp() :
 *  Same as HIST_count(), but using an externally provided scratch buffer.
 *  Benefit is this function will use very little stack space.
 * `workSpace` is a writable buffer which must be 4-bytes aligned,
 * `workSpaceSize` must be >= HIST_WKSP_SIZE
 */
size_t HIST_count_wksp(unsigned* count, unsigned* maxSymbolValuePtr,
                       const void* src, size_t srcSize,
                       void* workSpace, size_t workSpaceSize);

/** HIST_countFast() :
 *  same as HIST_count(), but blindly trusts that all byte values within src are <= *maxSymbolValuePtr.
 *  This function is unsafe, and will segfault if any value within `src` is `> *maxSymbolValuePtr`
 */
size_t HIST_countFast(unsigned* count, unsigned* maxSymbolValuePtr,
                      const void* src, size_t srcSize);

/** HIST_countFast_wksp() :
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
