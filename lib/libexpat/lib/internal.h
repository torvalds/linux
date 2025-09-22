/* internal.h

   Internal definitions used by Expat.  This is not needed to compile
   client code.

   The following calling convention macros are defined for frequently
   called functions:

   FASTCALL    - Used for those internal functions that have a simple
                 body and a low number of arguments and local variables.

   PTRCALL     - Used for functions called though function pointers.

   PTRFASTCALL - Like PTRCALL, but for low number of arguments.

   inline      - Used for selected internal functions for which inlining
                 may improve performance on some platforms.

   Note: Use of these macros is based on judgement, not hard rules,
         and therefore subject to change.
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 2002-2003 Fred L. Drake, Jr. <fdrake@users.sourceforge.net>
   Copyright (c) 2002-2006 Karl Waclawek <karl@waclawek.net>
   Copyright (c) 2003      Greg Stein <gstein@users.sourceforge.net>
   Copyright (c) 2016-2025 Sebastian Pipping <sebastian@pipping.org>
   Copyright (c) 2018      Yury Gribov <tetra2005@gmail.com>
   Copyright (c) 2019      David Loffredo <loffredo@steptools.com>
   Copyright (c) 2023-2024 Sony Corporation / Snild Dolkow <snild@sony.com>
   Copyright (c) 2024      Taichi Haradaguchi <20001722@ymail.ne.jp>
   Licensed under the MIT license:

   Permission is  hereby granted,  free of charge,  to any  person obtaining
   a  copy  of  this  software   and  associated  documentation  files  (the
   "Software"),  to  deal in  the  Software  without restriction,  including
   without  limitation the  rights  to use,  copy,  modify, merge,  publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit
   persons  to whom  the Software  is  furnished to  do so,  subject to  the
   following conditions:

   The above copyright  notice and this permission notice  shall be included
   in all copies or substantial portions of the Software.

   THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
   EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED  TO  THE WARRANTIES  OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
   NO EVENT SHALL THE AUTHORS OR  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR  OTHER LIABILITY, WHETHER  IN AN  ACTION OF CONTRACT,  TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#if defined(__GNUC__) && defined(__i386__) && ! defined(__MINGW32__)
/* We'll use this version by default only where we know it helps.

   regparm() generates warnings on Solaris boxes.   See SF bug #692878.

   Instability reported with egcs on a RedHat Linux 7.3.
   Let's comment out:
   #define FASTCALL __attribute__((stdcall, regparm(3)))
   and let's try this:
*/
#  define FASTCALL __attribute__((regparm(3)))
#  define PTRFASTCALL __attribute__((regparm(3)))
#endif

/* Using __fastcall seems to have an unexpected negative effect under
   MS VC++, especially for function pointers, so we won't use it for
   now on that platform. It may be reconsidered for a future release
   if it can be made more effective.
   Likely reason: __fastcall on Windows is like stdcall, therefore
   the compiler cannot perform stack optimizations for call clusters.
*/

/* Make sure all of these are defined if they aren't already. */

#ifndef FASTCALL
#  define FASTCALL
#endif

#ifndef PTRCALL
#  define PTRCALL
#endif

#ifndef PTRFASTCALL
#  define PTRFASTCALL
#endif

#ifndef XML_MIN_SIZE
#  if ! defined(__cplusplus) && ! defined(inline)
#    ifdef __GNUC__
#      define inline __inline
#    endif /* __GNUC__ */
#  endif
#endif /* XML_MIN_SIZE */

#ifdef __cplusplus
#  define inline inline
#else
#  ifndef inline
#    define inline
#  endif
#endif

#include <limits.h> // ULONG_MAX

#if defined(_WIN32)                                                            \
    && (! defined(__USE_MINGW_ANSI_STDIO)                                      \
        || (1 - __USE_MINGW_ANSI_STDIO - 1 == 0))
#  define EXPAT_FMT_ULL(midpart) "%" midpart "I64u"
#  if defined(_WIN64) // Note: modifiers "td" and "zu" do not work for MinGW
#    define EXPAT_FMT_PTRDIFF_T(midpart) "%" midpart "I64d"
#    define EXPAT_FMT_SIZE_T(midpart) "%" midpart "I64u"
#  else
#    define EXPAT_FMT_PTRDIFF_T(midpart) "%" midpart "d"
#    define EXPAT_FMT_SIZE_T(midpart) "%" midpart "u"
#  endif
#else
#  define EXPAT_FMT_ULL(midpart) "%" midpart "llu"
#  if ! defined(ULONG_MAX)
#    error Compiler did not define ULONG_MAX for us
#  elif ULONG_MAX == 18446744073709551615u // 2^64-1
#    define EXPAT_FMT_PTRDIFF_T(midpart) "%" midpart "ld"
#    define EXPAT_FMT_SIZE_T(midpart) "%" midpart "lu"
#  elif defined(EMSCRIPTEN) // 32bit mode Emscripten
#    define EXPAT_FMT_PTRDIFF_T(midpart) "%" midpart "ld"
#    define EXPAT_FMT_SIZE_T(midpart) "%" midpart "zu"
#  else
#    define EXPAT_FMT_PTRDIFF_T(midpart) "%" midpart "d"
#    define EXPAT_FMT_SIZE_T(midpart) "%" midpart "u"
#  endif
#endif

#ifndef UNUSED_P
#  define UNUSED_P(p) (void)p
#endif

/* NOTE BEGIN If you ever patch these defaults to greater values
              for non-attack XML payload in your environment,
              please file a bug report with libexpat.  Thank you!
*/
#define EXPAT_BILLION_LAUGHS_ATTACK_PROTECTION_MAXIMUM_AMPLIFICATION_DEFAULT   \
  100.0f
#define EXPAT_BILLION_LAUGHS_ATTACK_PROTECTION_ACTIVATION_THRESHOLD_DEFAULT    \
  8388608 // 8 MiB, 2^23

#define EXPAT_ALLOC_TRACKER_MAXIMUM_AMPLIFICATION_DEFAULT 100.0f
#define EXPAT_ALLOC_TRACKER_ACTIVATION_THRESHOLD_DEFAULT                       \
  67108864 // 64 MiB, 2^26

/* NOTE END */

#include "expat.h" // so we can use type XML_Parser below

#ifdef __cplusplus
extern "C" {
#endif

void _INTERNAL_trim_to_complete_utf8_characters(const char *from,
                                                const char **fromLimRef);

#if defined(XML_GE) && XML_GE == 1
unsigned long long testingAccountingGetCountBytesDirect(XML_Parser parser);
unsigned long long testingAccountingGetCountBytesIndirect(XML_Parser parser);
const char *unsignedCharToPrintable(unsigned char c);
#endif

extern
#if ! defined(XML_TESTING)
    const
#endif
    XML_Bool g_reparseDeferralEnabledDefault; // written ONLY in runtests.c
#if defined(XML_TESTING)
void *expat_malloc(XML_Parser parser, size_t size, int sourceLine);
void expat_free(XML_Parser parser, void *ptr, int sourceLine);
void *expat_realloc(XML_Parser parser, void *ptr, size_t size, int sourceLine);
extern unsigned int g_bytesScanned; // used for testing only
#endif

#ifdef __cplusplus
}
#endif
