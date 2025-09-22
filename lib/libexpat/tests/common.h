/* Commonly used functions for the Expat test suite
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 2001-2006 Fred L. Drake, Jr. <fdrake@users.sourceforge.net>
   Copyright (c) 2003      Greg Stein <gstein@users.sourceforge.net>
   Copyright (c) 2005-2007 Steven Solie <steven@solie.ca>
   Copyright (c) 2005-2012 Karl Waclawek <karl@waclawek.net>
   Copyright (c) 2016-2025 Sebastian Pipping <sebastian@pipping.org>
   Copyright (c) 2017-2022 Rhodri James <rhodri@wildebeest.org.uk>
   Copyright (c) 2017      Joe Orton <jorton@redhat.com>
   Copyright (c) 2017      José Gutiérrez de la Concha <jose@zeroc.com>
   Copyright (c) 2018      Marco Maggi <marco.maggi-ipsu@poste.it>
   Copyright (c) 2019      David Loffredo <loffredo@steptools.com>
   Copyright (c) 2020      Tim Gates <tim.gates@iress.com>
   Copyright (c) 2021      Donghee Na <donghee.na@python.org>
   Copyright (c) 2023      Sony Corporation / Snild Dolkow <snild@sony.com>
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

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XML_COMMON_H
#  define XML_COMMON_H

#  include "expat_config.h"
#  include "minicheck.h"
#  include "chardata.h"

#  ifdef XML_LARGE_SIZE
#    define XML_FMT_INT_MOD "ll"
#  else
#    define XML_FMT_INT_MOD "l"
#  endif

#  ifdef XML_UNICODE_WCHAR_T
#    define XML_FMT_STR "ls"
#    include <wchar.h>
#    define xcstrlen(s) wcslen(s)
#    define xcstrcmp(s, t) wcscmp((s), (t))
#    define xcstrncmp(s, t, n) wcsncmp((s), (t), (n))
#    define XCS(s) _XCS(s)
#    define _XCS(s) L##s
#  else
#    ifdef XML_UNICODE
#      error "No support for UTF-16 character without wchar_t in tests"
#    else
#      define XML_FMT_STR "s"
#      define xcstrlen(s) strlen(s)
#      define xcstrcmp(s, t) strcmp((s), (t))
#      define xcstrncmp(s, t, n) strncmp((s), (t), (n))
#      define XCS(s) s
#    endif /* XML_UNICODE */
#  endif   /* XML_UNICODE_WCHAR_T */

extern XML_Parser g_parser;

extern XML_Bool g_resumable;
extern XML_Bool g_abortable;

extern int g_chunkSize;

extern const char *long_character_data_text;
extern const char *long_cdata_text;
extern const char *get_buffer_test_text;

extern void tcase_add_test__ifdef_xml_dtd(TCase *tc, tcase_test_function test);
extern void tcase_add_test__if_xml_ge(TCase *tc, tcase_test_function test);

extern void basic_teardown(void);

extern void _xml_failure(XML_Parser parser, const char *file, int line);

#  define xml_failure(parser) _xml_failure((parser), __FILE__, __LINE__)

extern enum XML_Status _XML_Parse_SINGLE_BYTES(XML_Parser parser, const char *s,
                                               int len, int isFinal);

extern void _expect_failure(const char *text, enum XML_Error errorCode,
                            const char *errorMessage, const char *file,
                            int lineno);

#  define expect_failure(text, errorCode, errorMessage)                        \
    _expect_failure((text), (errorCode), (errorMessage), __FILE__, __LINE__)

/* Support functions for handlers to collect up character and attribute data.
 */

extern void _run_character_check(const char *text, const XML_Char *expected,
                                 const char *file, int line);

#  define run_character_check(text, expected)                                  \
    _run_character_check(text, expected, __FILE__, __LINE__)

extern void _run_attribute_check(const char *text, const XML_Char *expected,
                                 const char *file, int line);

#  define run_attribute_check(text, expected)                                  \
    _run_attribute_check(text, expected, __FILE__, __LINE__)

typedef struct ExtTest {
  const char *parse_text;
  const XML_Char *encoding;
  CharData *storage;
} ExtTest;

extern void _run_ext_character_check(const char *text, ExtTest *test_data,
                                     const XML_Char *expected, const char *file,
                                     int line);

#  define run_ext_character_check(text, test_data, expected)                   \
    _run_ext_character_check(text, test_data, expected, __FILE__, __LINE__)

#  define ALLOC_ALWAYS_SUCCEED (-1)
#  define REALLOC_ALWAYS_SUCCEED (-1)

extern int g_allocation_count;
extern int g_reallocation_count;

extern void *duff_allocator(size_t size);

extern void *duff_reallocator(void *ptr, size_t size);

extern char *portable_strndup(const char *s, size_t n);

#endif /* XML_COMMON_H */

#ifdef __cplusplus
}
#endif
