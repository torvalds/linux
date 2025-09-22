/* Tests in the "basic" test case for the Expat test suite
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
   Copyright (c) 2023-2024 Sony Corporation / Snild Dolkow <snild@sony.com>
   Copyright (c) 2024-2025 Berkay Eren Ürün <berkay.ueruen@siemens.com>
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

#if defined(NDEBUG)
#  undef NDEBUG /* because test suite relies on assert(...) at the moment */
#endif

#include <assert.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#if ! defined(__cplusplus)
#  include <stdbool.h>
#endif

#include "expat_config.h"

#include "expat.h"
#include "internal.h"
#include "minicheck.h"
#include "structdata.h"
#include "common.h"
#include "dummy.h"
#include "handlers.h"
#include "siphash.h"
#include "basic_tests.h"

static void
basic_setup(void) {
  g_parser = XML_ParserCreate(NULL);
  if (g_parser == NULL)
    fail("Parser not created.");
}

/*
 * Character & encoding tests.
 */

START_TEST(test_nul_byte) {
  char text[] = "<doc>\0</doc>";

  /* test that a NUL byte (in US-ASCII data) is an error */
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_OK)
    fail("Parser did not report error on NUL-byte.");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_INVALID_TOKEN)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_u0000_char) {
  /* test that a NUL byte (in US-ASCII data) is an error */
  expect_failure("<doc>&#0;</doc>", XML_ERROR_BAD_CHAR_REF,
                 "Parser did not report error on NUL-byte.");
}
END_TEST

START_TEST(test_siphash_self) {
  if (! sip24_valid())
    fail("SipHash self-test failed");
}
END_TEST

START_TEST(test_siphash_spec) {
  /* https://131002.net/siphash/siphash.pdf (page 19, "Test values") */
  const char message[] = "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09"
                         "\x0a\x0b\x0c\x0d\x0e";
  const size_t len = sizeof(message) - 1;
  const uint64_t expected = SIP_ULL(0xa129ca61U, 0x49be45e5U);
  struct siphash state;
  struct sipkey key;

  sip_tokey(&key, "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09"
                  "\x0a\x0b\x0c\x0d\x0e\x0f");
  sip24_init(&state, &key);

  /* Cover spread across calls */
  sip24_update(&state, message, 4);
  sip24_update(&state, message + 4, len - 4);

  /* Cover null length */
  sip24_update(&state, message, 0);

  if (sip24_final(&state) != expected)
    fail("sip24_final failed spec test\n");

  /* Cover wrapper */
  if (siphash24(message, len, &key) != expected)
    fail("siphash24 failed spec test\n");
}
END_TEST

START_TEST(test_bom_utf8) {
  /* This test is really just making sure we don't core on a UTF-8 BOM. */
  const char *text = "\357\273\277<e/>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_bom_utf16_be) {
  char text[] = "\376\377\0<\0e\0/\0>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_bom_utf16_le) {
  char text[] = "\377\376<\0e\0/\0>\0";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_nobom_utf16_le) {
  char text[] = " \0<\0e\0/\0>\0";

  if (g_chunkSize == 1) {
    // TODO: with just the first byte, we can't tell the difference between
    // UTF-16-LE and UTF-8. Avoid the failure for now.
    return;
  }

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_hash_collision) {
  /* For full coverage of the lookup routine, we need to ensure a
   * hash collision even though we can only tell that we have one
   * through breakpoint debugging or coverage statistics.  The
   * following will cause a hash collision on machines with a 64-bit
   * long type; others will have to experiment.  The full coverage
   * tests invoked from qa.sh usually provide a hash collision, but
   * not always.  This is an attempt to provide insurance.
   */
#define COLLIDING_HASH_SALT (unsigned long)SIP_ULL(0xffffffffU, 0xff99fc90U)
  const char *text
      = "<doc>\n"
        "<a1/><a2/><a3/><a4/><a5/><a6/><a7/><a8/>\n"
        "<b1></b1><b2 attr='foo'>This is a foo</b2><b3></b3><b4></b4>\n"
        "<b5></b5><b6></b6><b7></b7><b8></b8>\n"
        "<c1/><c2/><c3/><c4/><c5/><c6/><c7/><c8/>\n"
        "<d1/><d2/><d3/><d4/><d5/><d6/><d7/>\n"
        "<d8>This triggers the table growth and collides with b2</d8>\n"
        "</doc>\n";

  XML_SetHashSalt(g_parser, COLLIDING_HASH_SALT);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST
#undef COLLIDING_HASH_SALT

/* Regression test for SF bug #491986. */
START_TEST(test_danish_latin1) {
  const char *text = "<?xml version='1.0' encoding='iso-8859-1'?>\n"
                     "<e>J\xF8rgen \xE6\xF8\xE5\xC6\xD8\xC5</e>";
#ifdef XML_UNICODE
  const XML_Char *expected
      = XCS("J\x00f8rgen \x00e6\x00f8\x00e5\x00c6\x00d8\x00c5");
#else
  const XML_Char *expected
      = XCS("J\xC3\xB8rgen \xC3\xA6\xC3\xB8\xC3\xA5\xC3\x86\xC3\x98\xC3\x85");
#endif
  run_character_check(text, expected);
}
END_TEST

/* Regression test for SF bug #514281. */
START_TEST(test_french_charref_hexidecimal) {
  const char *text = "<?xml version='1.0' encoding='iso-8859-1'?>\n"
                     "<doc>&#xE9;&#xE8;&#xE0;&#xE7;&#xEA;&#xC8;</doc>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00e9\x00e8\x00e0\x00e7\x00ea\x00c8");
#else
  const XML_Char *expected
      = XCS("\xC3\xA9\xC3\xA8\xC3\xA0\xC3\xA7\xC3\xAA\xC3\x88");
#endif
  run_character_check(text, expected);
}
END_TEST

START_TEST(test_french_charref_decimal) {
  const char *text = "<?xml version='1.0' encoding='iso-8859-1'?>\n"
                     "<doc>&#233;&#232;&#224;&#231;&#234;&#200;</doc>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00e9\x00e8\x00e0\x00e7\x00ea\x00c8");
#else
  const XML_Char *expected
      = XCS("\xC3\xA9\xC3\xA8\xC3\xA0\xC3\xA7\xC3\xAA\xC3\x88");
#endif
  run_character_check(text, expected);
}
END_TEST

START_TEST(test_french_latin1) {
  const char *text = "<?xml version='1.0' encoding='iso-8859-1'?>\n"
                     "<doc>\xE9\xE8\xE0\xE7\xEa\xC8</doc>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00e9\x00e8\x00e0\x00e7\x00ea\x00c8");
#else
  const XML_Char *expected
      = XCS("\xC3\xA9\xC3\xA8\xC3\xA0\xC3\xA7\xC3\xAA\xC3\x88");
#endif
  run_character_check(text, expected);
}
END_TEST

START_TEST(test_french_utf8) {
  const char *text = "<?xml version='1.0' encoding='utf-8'?>\n"
                     "<doc>\xC3\xA9</doc>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00e9");
#else
  const XML_Char *expected = XCS("\xC3\xA9");
#endif
  run_character_check(text, expected);
}
END_TEST

/* Regression test for SF bug #600479.
   XXX There should be a test that exercises all legal XML Unicode
   characters as PCDATA and attribute value content, and XML Name
   characters as part of element and attribute names.
*/
START_TEST(test_utf8_false_rejection) {
  const char *text = "<doc>\xEF\xBA\xBF</doc>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\xfebf");
#else
  const XML_Char *expected = XCS("\xEF\xBA\xBF");
#endif
  run_character_check(text, expected);
}
END_TEST

/* Regression test for SF bug #477667.
   This test assures that any 8-bit character followed by a 7-bit
   character will not be mistakenly interpreted as a valid UTF-8
   sequence.
*/
START_TEST(test_illegal_utf8) {
  char text[100];
  int i;

  for (i = 128; i <= 255; ++i) {
    snprintf(text, sizeof(text), "<e>%ccd</e>", i);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        == XML_STATUS_OK) {
      snprintf(text, sizeof(text),
               "expected token error for '%c' (ordinal %d) in UTF-8 text", i,
               i);
      fail(text);
    } else if (XML_GetErrorCode(g_parser) != XML_ERROR_INVALID_TOKEN)
      xml_failure(g_parser);
    /* Reset the parser since we use the same parser repeatedly. */
    XML_ParserReset(g_parser, NULL);
  }
}
END_TEST

/* Examples, not masks: */
#define UTF8_LEAD_1 "\x7f" /* 0b01111111 */
#define UTF8_LEAD_2 "\xdf" /* 0b11011111 */
#define UTF8_LEAD_3 "\xef" /* 0b11101111 */
#define UTF8_LEAD_4 "\xf7" /* 0b11110111 */
#define UTF8_FOLLOW "\xbf" /* 0b10111111 */

START_TEST(test_utf8_auto_align) {
  struct TestCase {
    ptrdiff_t expectedMovementInChars;
    const char *input;
  };

  struct TestCase cases[] = {
      {00, ""},

      {00, UTF8_LEAD_1},

      {-1, UTF8_LEAD_2},
      {00, UTF8_LEAD_2 UTF8_FOLLOW},

      {-1, UTF8_LEAD_3},
      {-2, UTF8_LEAD_3 UTF8_FOLLOW},
      {00, UTF8_LEAD_3 UTF8_FOLLOW UTF8_FOLLOW},

      {-1, UTF8_LEAD_4},
      {-2, UTF8_LEAD_4 UTF8_FOLLOW},
      {-3, UTF8_LEAD_4 UTF8_FOLLOW UTF8_FOLLOW},
      {00, UTF8_LEAD_4 UTF8_FOLLOW UTF8_FOLLOW UTF8_FOLLOW},
  };

  size_t i = 0;
  bool success = true;
  for (; i < sizeof(cases) / sizeof(*cases); i++) {
    const char *fromLim = cases[i].input + strlen(cases[i].input);
    const char *const fromLimInitially = fromLim;
    ptrdiff_t actualMovementInChars;

    _INTERNAL_trim_to_complete_utf8_characters(cases[i].input, &fromLim);

    actualMovementInChars = (fromLim - fromLimInitially);
    if (actualMovementInChars != cases[i].expectedMovementInChars) {
      size_t j = 0;
      success = false;
      printf("[-] UTF-8 case %2u: Expected movement by %2d chars"
             ", actually moved by %2d chars: \"",
             (unsigned)(i + 1), (int)cases[i].expectedMovementInChars,
             (int)actualMovementInChars);
      for (; j < strlen(cases[i].input); j++) {
        printf("\\x%02x", (unsigned char)cases[i].input[j]);
      }
      printf("\"\n");
    }
  }

  if (! success) {
    fail("UTF-8 auto-alignment is not bullet-proof\n");
  }
}
END_TEST

START_TEST(test_utf16) {
  /* <?xml version="1.0" encoding="UTF-16"?>
   *  <doc a='123'>some {A} text</doc>
   *
   * where {A} is U+FF21, FULLWIDTH LATIN CAPITAL LETTER A
   */
  char text[]
      = "\000<\000?\000x\000m\000\154\000 \000v\000e\000r\000s\000i\000o"
        "\000n\000=\000'\0001\000.\000\060\000'\000 \000e\000n\000c\000o"
        "\000d\000i\000n\000g\000=\000'\000U\000T\000F\000-\0001\000\066"
        "\000'\000?\000>\000\n"
        "\000<\000d\000o\000c\000 \000a\000=\000'\0001\0002\0003\000'\000>"
        "\000s\000o\000m\000e\000 \xff\x21\000 \000t\000e\000x\000t\000"
        "<\000/\000d\000o\000c\000>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("some \xff21 text");
#else
  const XML_Char *expected = XCS("some \357\274\241 text");
#endif
  CharData storage;

  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetCharacterDataHandler(g_parser, accumulate_characters);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_utf16_le_epilog_newline) {
  unsigned int first_chunk_bytes = 17;
  char text[] = "\xFF\xFE"                  /* BOM */
                "<\000e\000/\000>\000"      /* document element */
                "\r\000\n\000\r\000\n\000"; /* epilog */

  if (first_chunk_bytes >= sizeof(text) - 1)
    fail("bad value of first_chunk_bytes");
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)first_chunk_bytes, XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  else {
    enum XML_Status rc;
    rc = _XML_Parse_SINGLE_BYTES(g_parser, text + first_chunk_bytes,
                                 (int)(sizeof(text) - first_chunk_bytes - 1),
                                 XML_TRUE);
    if (rc == XML_STATUS_ERROR)
      xml_failure(g_parser);
  }
}
END_TEST

/* Test that an outright lie in the encoding is faulted */
START_TEST(test_not_utf16) {
  const char *text = "<?xml version='1.0' encoding='utf-16'?>"
                     "<doc>Hi</doc>";

  /* Use a handler to provoke the appropriate code paths */
  XML_SetXmlDeclHandler(g_parser, dummy_xdecl_handler);
  expect_failure(text, XML_ERROR_INCORRECT_ENCODING,
                 "UTF-16 declared in UTF-8 not faulted");
}
END_TEST

/* Test that an unknown encoding is rejected */
START_TEST(test_bad_encoding) {
  const char *text = "<doc>Hi</doc>";

  if (! XML_SetEncoding(g_parser, XCS("unknown-encoding")))
    fail("XML_SetEncoding failed");
  expect_failure(text, XML_ERROR_UNKNOWN_ENCODING,
                 "Unknown encoding not faulted");
}
END_TEST

/* Regression test for SF bug #481609, #774028. */
START_TEST(test_latin1_umlauts) {
  const char *text
      = "<?xml version='1.0' encoding='iso-8859-1'?>\n"
        "<e a='\xE4 \xF6 \xFC &#228; &#246; &#252; &#x00E4; &#x0F6; &#xFC; >'\n"
        "  >\xE4 \xF6 \xFC &#228; &#246; &#252; &#x00E4; &#x0F6; &#xFC; ></e>";
#ifdef XML_UNICODE
  /* Expected results in UTF-16 */
  const XML_Char *expected = XCS("\x00e4 \x00f6 \x00fc ")
      XCS("\x00e4 \x00f6 \x00fc ") XCS("\x00e4 \x00f6 \x00fc >");
#else
  /* Expected results in UTF-8 */
  const XML_Char *expected = XCS("\xC3\xA4 \xC3\xB6 \xC3\xBC ")
      XCS("\xC3\xA4 \xC3\xB6 \xC3\xBC ") XCS("\xC3\xA4 \xC3\xB6 \xC3\xBC >");
#endif

  run_character_check(text, expected);
  XML_ParserReset(g_parser, NULL);
  run_attribute_check(text, expected);
  /* Repeat with a default handler */
  XML_ParserReset(g_parser, NULL);
  XML_SetDefaultHandler(g_parser, dummy_default_handler);
  run_character_check(text, expected);
  XML_ParserReset(g_parser, NULL);
  XML_SetDefaultHandler(g_parser, dummy_default_handler);
  run_attribute_check(text, expected);
}
END_TEST

/* Test that an element name with a 4-byte UTF-8 character is rejected */
START_TEST(test_long_utf8_character) {
  const char *text
      = "<?xml version='1.0' encoding='utf-8'?>\n"
        /* 0xf0 0x90 0x80 0x80 = U+10000, the first Linear B character */
        "<do\xf0\x90\x80\x80/>";
  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "4-byte UTF-8 character in element name not faulted");
}
END_TEST

/* Test that a long latin-1 attribute (too long to convert in one go)
 * is correctly converted
 */
START_TEST(test_long_latin1_attribute) {
  const char *text
      = "<?xml version='1.0' encoding='iso-8859-1'?>\n"
        "<doc att='"
        /* 64 characters per line */
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        /* Last character splits across a buffer boundary */
        "\xe4'>\n</doc>";

  const XML_Char *expected =
      /* 64 characters per line */
      /* clang-format off */
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO")
  /* clang-format on */
#ifdef XML_UNICODE
                                                  XCS("\x00e4");
#else
                                                  XCS("\xc3\xa4");
#endif

  run_attribute_check(text, expected);
}
END_TEST

/* Test that a long ASCII attribute (too long to convert in one go)
 * is correctly converted
 */
START_TEST(test_long_ascii_attribute) {
  const char *text
      = "<?xml version='1.0' encoding='us-ascii'?>\n"
        "<doc att='"
        /* 64 characters per line */
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
        "01234'>\n</doc>";
  const XML_Char *expected =
      /* 64 characters per line */
      /* clang-format off */
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("01234");
  /* clang-format on */

  run_attribute_check(text, expected);
}
END_TEST

/* Regression test #1 for SF bug #653180. */
START_TEST(test_line_number_after_parse) {
  const char *text = "<tag>\n"
                     "\n"
                     "\n</tag>";
  XML_Size lineno;

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  lineno = XML_GetCurrentLineNumber(g_parser);
  if (lineno != 4) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer),
             "expected 4 lines, saw %" XML_FMT_INT_MOD "u", lineno);
    fail(buffer);
  }
}
END_TEST

/* Regression test #2 for SF bug #653180. */
START_TEST(test_column_number_after_parse) {
  const char *text = "<tag></tag>";
  XML_Size colno;

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  colno = XML_GetCurrentColumnNumber(g_parser);
  if (colno != 11) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer),
             "expected 11 columns, saw %" XML_FMT_INT_MOD "u", colno);
    fail(buffer);
  }
}
END_TEST

/* Regression test #3 for SF bug #653180. */
START_TEST(test_line_and_column_numbers_inside_handlers) {
  const char *text = "<a>\n"      /* Unix end-of-line */
                     "  <b>\r\n"  /* Windows end-of-line */
                     "    <c/>\r" /* Mac OS end-of-line */
                     "  </b>\n"
                     "  <d>\n"
                     "    <f/>\n"
                     "  </d>\n"
                     "</a>";
  const StructDataEntry expected[]
      = {{XCS("a"), 0, 1, STRUCT_START_TAG}, {XCS("b"), 2, 2, STRUCT_START_TAG},
         {XCS("c"), 4, 3, STRUCT_START_TAG}, {XCS("c"), 8, 3, STRUCT_END_TAG},
         {XCS("b"), 2, 4, STRUCT_END_TAG},   {XCS("d"), 2, 5, STRUCT_START_TAG},
         {XCS("f"), 4, 6, STRUCT_START_TAG}, {XCS("f"), 8, 6, STRUCT_END_TAG},
         {XCS("d"), 2, 7, STRUCT_END_TAG},   {XCS("a"), 0, 8, STRUCT_END_TAG}};
  const int expected_count = sizeof(expected) / sizeof(StructDataEntry);
  StructData storage;

  StructData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetStartElementHandler(g_parser, start_element_event_handler2);
  XML_SetEndElementHandler(g_parser, end_element_event_handler2);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);

  StructData_CheckItems(&storage, expected, expected_count);
  StructData_Dispose(&storage);
}
END_TEST

/* Regression test #4 for SF bug #653180. */
START_TEST(test_line_number_after_error) {
  const char *text = "<a>\n"
                     "  <b>\n"
                     "  </a>"; /* missing </b> */
  XML_Size lineno;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Expected a parse error");

  lineno = XML_GetCurrentLineNumber(g_parser);
  if (lineno != 3) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer),
             "expected 3 lines, saw %" XML_FMT_INT_MOD "u", lineno);
    fail(buffer);
  }
}
END_TEST

/* Regression test #5 for SF bug #653180. */
START_TEST(test_column_number_after_error) {
  const char *text = "<a>\n"
                     "  <b>\n"
                     "  </a>"; /* missing </b> */
  XML_Size colno;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Expected a parse error");

  colno = XML_GetCurrentColumnNumber(g_parser);
  if (colno != 4) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer),
             "expected 4 columns, saw %" XML_FMT_INT_MOD "u", colno);
    fail(buffer);
  }
}
END_TEST

/* Regression test for SF bug #478332. */
START_TEST(test_really_long_lines) {
  /* This parses an input line longer than INIT_DATA_BUF_SIZE
     characters long (defined to be 1024 in xmlparse.c).  We take a
     really cheesy approach to building the input buffer, because
     this avoids writing bugs in buffer-filling code.
  */
  const char *text
      = "<e>"
        /* 64 chars */
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        /* until we have at least 1024 characters on the line: */
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "</e>";
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test cdata processing across a buffer boundary */
START_TEST(test_really_long_encoded_lines) {
  /* As above, except that we want to provoke an output buffer
   * overflow with a non-trivial encoding.  For this we need to pass
   * the whole cdata in one go, not byte-by-byte.
   */
  void *buffer;
  const char *text
      = "<?xml version='1.0' encoding='iso-8859-1'?>"
        "<e>"
        /* 64 chars */
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        /* until we have at least 1024 characters on the line: */
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+"
        "</e>";
  int parse_len = (int)strlen(text);

  /* Need a cdata handler to provoke the code path we want to test */
  XML_SetCharacterDataHandler(g_parser, dummy_cdata_handler);
  buffer = XML_GetBuffer(g_parser, parse_len);
  if (buffer == NULL)
    fail("Could not allocate parse buffer");
  assert(buffer != NULL);
  memcpy(buffer, text, parse_len);
  if (XML_ParseBuffer(g_parser, parse_len, XML_TRUE) == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/*
 * Element event tests.
 */

START_TEST(test_end_element_events) {
  const char *text = "<a><b><c/></b><d><f/></d></a>";
  const XML_Char *expected = XCS("/c/b/f/d/a");
  CharData storage;

  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetEndElementHandler(g_parser, end_element_event_handler);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/*
 * Attribute tests.
 */

/* Helper used by the following tests; this checks any "attr" and "refs"
   attributes to make sure whitespace has been normalized.

   Return true if whitespace has been normalized in a string, using
   the rules for attribute value normalization.  The 'is_cdata' flag
   is needed since CDATA attributes don't need to have multiple
   whitespace characters collapsed to a single space, while other
   attribute data types do.  (Section 3.3.3 of the recommendation.)
*/
static int
is_whitespace_normalized(const XML_Char *s, int is_cdata) {
  int blanks = 0;
  int at_start = 1;
  while (*s) {
    if (*s == XCS(' '))
      ++blanks;
    else if (*s == XCS('\t') || *s == XCS('\n') || *s == XCS('\r'))
      return 0;
    else {
      if (at_start) {
        at_start = 0;
        if (blanks && ! is_cdata)
          /* illegal leading blanks */
          return 0;
      } else if (blanks > 1 && ! is_cdata)
        return 0;
      blanks = 0;
    }
    ++s;
  }
  if (blanks && ! is_cdata)
    return 0;
  return 1;
}

/* Check the attribute whitespace checker: */
START_TEST(test_helper_is_whitespace_normalized) {
  assert(is_whitespace_normalized(XCS("abc"), 0));
  assert(is_whitespace_normalized(XCS("abc"), 1));
  assert(is_whitespace_normalized(XCS("abc def ghi"), 0));
  assert(is_whitespace_normalized(XCS("abc def ghi"), 1));
  assert(! is_whitespace_normalized(XCS(" abc def ghi"), 0));
  assert(is_whitespace_normalized(XCS(" abc def ghi"), 1));
  assert(! is_whitespace_normalized(XCS("abc  def ghi"), 0));
  assert(is_whitespace_normalized(XCS("abc  def ghi"), 1));
  assert(! is_whitespace_normalized(XCS("abc def ghi "), 0));
  assert(is_whitespace_normalized(XCS("abc def ghi "), 1));
  assert(! is_whitespace_normalized(XCS(" "), 0));
  assert(is_whitespace_normalized(XCS(" "), 1));
  assert(! is_whitespace_normalized(XCS("\t"), 0));
  assert(! is_whitespace_normalized(XCS("\t"), 1));
  assert(! is_whitespace_normalized(XCS("\n"), 0));
  assert(! is_whitespace_normalized(XCS("\n"), 1));
  assert(! is_whitespace_normalized(XCS("\r"), 0));
  assert(! is_whitespace_normalized(XCS("\r"), 1));
  assert(! is_whitespace_normalized(XCS("abc\t def"), 1));
}
END_TEST

static void XMLCALL
check_attr_contains_normalized_whitespace(void *userData, const XML_Char *name,
                                          const XML_Char **atts) {
  int i;
  UNUSED_P(userData);
  UNUSED_P(name);
  for (i = 0; atts[i] != NULL; i += 2) {
    const XML_Char *attrname = atts[i];
    const XML_Char *value = atts[i + 1];
    if (xcstrcmp(XCS("attr"), attrname) == 0
        || xcstrcmp(XCS("ents"), attrname) == 0
        || xcstrcmp(XCS("refs"), attrname) == 0) {
      if (! is_whitespace_normalized(value, 0)) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                 "attribute value not normalized: %" XML_FMT_STR
                 "='%" XML_FMT_STR "'",
                 attrname, value);
        fail(buffer);
      }
    }
  }
}

START_TEST(test_attr_whitespace_normalization) {
  const char *text
      = "<!DOCTYPE doc [\n"
        "  <!ATTLIST doc\n"
        "            attr NMTOKENS #REQUIRED\n"
        "            ents ENTITIES #REQUIRED\n"
        "            refs IDREFS   #REQUIRED>\n"
        "]>\n"
        "<doc attr='    a  b c\t\td\te\t' refs=' id-1   \t  id-2\t\t'  \n"
        "     ents=' ent-1   \t\r\n"
        "            ent-2  ' >\n"
        "  <e id='id-1'/>\n"
        "  <e id='id-2'/>\n"
        "</doc>";

  XML_SetStartElementHandler(g_parser,
                             check_attr_contains_normalized_whitespace);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/*
 * XML declaration tests.
 */

START_TEST(test_xmldecl_misplaced) {
  expect_failure("\n"
                 "<?xml version='1.0'?>\n"
                 "<a/>",
                 XML_ERROR_MISPLACED_XML_PI,
                 "failed to report misplaced XML declaration");
}
END_TEST

START_TEST(test_xmldecl_invalid) {
  expect_failure("<?xml version='1.0' \xc3\xa7?>\n<doc/>", XML_ERROR_XML_DECL,
                 "Failed to report invalid XML declaration");
}
END_TEST

START_TEST(test_xmldecl_missing_attr) {
  expect_failure("<?xml ='1.0'?>\n<doc/>\n", XML_ERROR_XML_DECL,
                 "Failed to report missing XML declaration attribute");
}
END_TEST

START_TEST(test_xmldecl_missing_value) {
  expect_failure("<?xml version='1.0' encoding='us-ascii' standalone?>\n"
                 "<doc/>",
                 XML_ERROR_XML_DECL,
                 "Failed to report missing attribute value");
}
END_TEST

/* Regression test for SF bug #584832. */
START_TEST(test_unknown_encoding_internal_entity) {
  const char *text = "<?xml version='1.0' encoding='unsupported-encoding'?>\n"
                     "<!DOCTYPE test [<!ENTITY foo 'bar'>]>\n"
                     "<test a='&foo;'/>";

  XML_SetUnknownEncodingHandler(g_parser, UnknownEncodingHandler, NULL);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test unrecognised encoding handler */
START_TEST(test_unrecognised_encoding_internal_entity) {
  const char *text = "<?xml version='1.0' encoding='unsupported-encoding'?>\n"
                     "<!DOCTYPE test [<!ENTITY foo 'bar'>]>\n"
                     "<test a='&foo;'/>";

  XML_SetUnknownEncodingHandler(g_parser, UnrecognisedEncodingHandler, NULL);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Unrecognised encoding not rejected");
}
END_TEST

/* Regression test for SF bug #620106. */
START_TEST(test_ext_entity_set_encoding) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtTest test_data
      = {/* This text says it's an unsupported encoding, but it's really
            UTF-8, which we tell Expat using XML_SetEncoding().
         */
         "<?xml encoding='iso-8859-3'?>\xC3\xA9", XCS("utf-8"), NULL};
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00e9");
#else
  const XML_Char *expected = XCS("\xc3\xa9");
#endif

  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  run_ext_character_check(text, &test_data, expected);
}
END_TEST

/* Test external entities with no handler */
START_TEST(test_ext_entity_no_handler) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";

  XML_SetDefaultHandler(g_parser, dummy_default_handler);
  run_character_check(text, XCS(""));
}
END_TEST

/* Test UTF-8 BOM is accepted */
START_TEST(test_ext_entity_set_bom) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtTest test_data = {"\xEF\xBB\xBF" /* BOM */
                       "<?xml encoding='iso-8859-3'?>"
                       "\xC3\xA9",
                       XCS("utf-8"), NULL};
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00e9");
#else
  const XML_Char *expected = XCS("\xc3\xa9");
#endif

  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  run_ext_character_check(text, &test_data, expected);
}
END_TEST

/* Test that bad encodings are faulted */
START_TEST(test_ext_entity_bad_encoding) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtFaults fault
      = {"<?xml encoding='iso-8859-3'?>u", "Unsupported encoding not faulted",
         XCS("unknown"), XML_ERROR_UNKNOWN_ENCODING};

  XML_SetExternalEntityRefHandler(g_parser, external_entity_faulter);
  XML_SetUserData(g_parser, &fault);
  expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                 "Bad encoding should not have been accepted");
}
END_TEST

/* Try handing an invalid encoding to an external entity parser */
START_TEST(test_ext_entity_bad_encoding_2) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc>&entity;</doc>";
  ExtFaults fault
      = {"<!ELEMENT doc (#PCDATA)*>", "Unknown encoding not faulted",
         XCS("unknown-encoding"), XML_ERROR_UNKNOWN_ENCODING};

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_faulter);
  XML_SetUserData(g_parser, &fault);
  expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                 "Bad encoding not faulted in external entity handler");
}
END_TEST

/* Test that no error is reported for unknown entities if we don't
   read an external subset.  This was fixed in Expat 1.95.5.
*/
START_TEST(test_wfc_undeclared_entity_unread_external_subset) {
  const char *text = "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc>&entity;</doc>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test that an error is reported for unknown entities if we don't
   have an external subset.
*/
START_TEST(test_wfc_undeclared_entity_no_external_subset) {
  expect_failure("<doc>&entity;</doc>", XML_ERROR_UNDEFINED_ENTITY,
                 "Parser did not report undefined entity w/out a DTD.");
}
END_TEST

/* Test that an error is reported for unknown entities if we don't
   read an external subset, but have been declared standalone.
*/
START_TEST(test_wfc_undeclared_entity_standalone) {
  const char *text
      = "<?xml version='1.0' encoding='us-ascii' standalone='yes'?>\n"
        "<!DOCTYPE doc SYSTEM 'foo'>\n"
        "<doc>&entity;</doc>";

  expect_failure(text, XML_ERROR_UNDEFINED_ENTITY,
                 "Parser did not report undefined entity (standalone).");
}
END_TEST

/* Test that an error is reported for unknown entities if we have read
   an external subset, and standalone is true.
*/
START_TEST(test_wfc_undeclared_entity_with_external_subset_standalone) {
  const char *text
      = "<?xml version='1.0' encoding='us-ascii' standalone='yes'?>\n"
        "<!DOCTYPE doc SYSTEM 'foo'>\n"
        "<doc>&entity;</doc>";
  ExtTest test_data = {"<!ELEMENT doc (#PCDATA)*>", NULL, NULL};

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetUserData(g_parser, &test_data);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  expect_failure(text, XML_ERROR_UNDEFINED_ENTITY,
                 "Parser did not report undefined entity (external DTD).");
}
END_TEST

/* Test that external entity handling is not done if the parsing flag
 * is set to UNLESS_STANDALONE
 */
START_TEST(test_entity_with_external_subset_unless_standalone) {
  const char *text
      = "<?xml version='1.0' encoding='us-ascii' standalone='yes'?>\n"
        "<!DOCTYPE doc SYSTEM 'foo'>\n"
        "<doc>&entity;</doc>";
  ExtTest test_data = {"<!ENTITY entity 'bar'>", NULL, NULL};

  XML_SetParamEntityParsing(g_parser,
                            XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE);
  XML_SetUserData(g_parser, &test_data);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  expect_failure(text, XML_ERROR_UNDEFINED_ENTITY,
                 "Parser did not report undefined entity");
}
END_TEST

/* Test that no error is reported for unknown entities if we have read
   an external subset, and standalone is false.
*/
START_TEST(test_wfc_undeclared_entity_with_external_subset) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc>&entity;</doc>";
  ExtTest test_data = {"<!ELEMENT doc (#PCDATA)*>", NULL, NULL};

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  run_ext_character_check(text, &test_data, XCS(""));
}
END_TEST

/* Test that an error is reported if our NotStandalone handler fails */
START_TEST(test_not_standalone_handler_reject) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc>&entity;</doc>";
  ExtTest test_data = {"<!ELEMENT doc (#PCDATA)*>", NULL, NULL};

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetUserData(g_parser, &test_data);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  XML_SetNotStandaloneHandler(g_parser, reject_not_standalone_handler);
  expect_failure(text, XML_ERROR_NOT_STANDALONE,
                 "NotStandalone handler failed to reject");

  /* Try again but without external entity handling */
  XML_ParserReset(g_parser, NULL);
  XML_SetNotStandaloneHandler(g_parser, reject_not_standalone_handler);
  expect_failure(text, XML_ERROR_NOT_STANDALONE,
                 "NotStandalone handler failed to reject");
}
END_TEST

/* Test that no error is reported if our NotStandalone handler succeeds */
START_TEST(test_not_standalone_handler_accept) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc>&entity;</doc>";
  ExtTest test_data = {"<!ELEMENT doc (#PCDATA)*>", NULL, NULL};

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  XML_SetNotStandaloneHandler(g_parser, accept_not_standalone_handler);
  run_ext_character_check(text, &test_data, XCS(""));

  /* Repeat without the external entity handler */
  XML_ParserReset(g_parser, NULL);
  XML_SetNotStandaloneHandler(g_parser, accept_not_standalone_handler);
  run_character_check(text, XCS(""));
}
END_TEST

START_TEST(test_entity_start_tag_level_greater_than_one) {
  const char *const text = "<!DOCTYPE t1 [\n"
                           "  <!ENTITY e1 'hello'>\n"
                           "]>\n"
                           "<t1>\n"
                           "  <t2>&e1;</t2>\n"
                           "</t1>\n";

  XML_Parser parser = XML_ParserCreate(NULL);
  assert_true(_XML_Parse_SINGLE_BYTES(parser, text, (int)strlen(text),
                                      /*isFinal*/ XML_TRUE)
              == XML_STATUS_OK);
  XML_ParserFree(parser);
}
END_TEST

START_TEST(test_wfc_no_recursive_entity_refs) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY entity '&#38;entity;'>\n"
                     "]>\n"
                     "<doc>&entity;</doc>";

  expect_failure(text, XML_ERROR_RECURSIVE_ENTITY_REF,
                 "Parser did not report recursive entity reference.");
}
END_TEST

START_TEST(test_no_indirectly_recursive_entity_refs) {
  struct TestCase {
    const char *doc;
    bool usesParameterEntities;
  };

  const struct TestCase cases[] = {
      // general entity + character data
      {"<!DOCTYPE a [\n"
       "  <!ENTITY e1 '&e2;'>\n"
       "  <!ENTITY e2 '&e1;'>\n"
       "]><a>&e2;</a>\n",
       false},

      // general entity + attribute value
      {"<!DOCTYPE a [\n"
       "  <!ENTITY e1 '&e2;'>\n"
       "  <!ENTITY e2 '&e1;'>\n"
       "]><a k1='&e2;' />\n",
       false},

      // parameter entity
      {"<!DOCTYPE doc [\n"
       "  <!ENTITY % p1 '&#37;p2;'>\n"
       "  <!ENTITY % p2 '&#37;p1;'>\n"
       "  <!ENTITY % define_g \"<!ENTITY g '&#37;p2;'>\">\n"
       "  %define_g;\n"
       "]>\n"
       "<doc/>\n",
       true},
  };
  const XML_Bool reset_or_not[] = {XML_TRUE, XML_FALSE};

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    for (size_t j = 0; j < sizeof(reset_or_not) / sizeof(reset_or_not[0]);
         j++) {
      const XML_Bool reset_wanted = reset_or_not[j];
      const char *const doc = cases[i].doc;
      const bool usesParameterEntities = cases[i].usesParameterEntities;

      set_subtest("[%i,reset=%i] %s", (int)i, (int)j, doc);

#ifdef XML_DTD // both GE and DTD
      const bool rejection_expected = true;
#elif XML_GE == 1 // GE but not DTD
      const bool rejection_expected = ! usesParameterEntities;
#else             // neither DTD nor GE
      const bool rejection_expected = false;
#endif

      XML_Parser parser = XML_ParserCreate(NULL);

#ifdef XML_DTD
      if (usesParameterEntities) {
        assert_true(
            XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_ALWAYS)
            == 1);
      }
#else
      UNUSED_P(usesParameterEntities);
#endif // XML_DTD

      const enum XML_Status status
          = _XML_Parse_SINGLE_BYTES(parser, doc, (int)strlen(doc),
                                    /*isFinal*/ XML_TRUE);

      if (rejection_expected) {
        assert_true(status == XML_STATUS_ERROR);
        assert_true(XML_GetErrorCode(parser) == XML_ERROR_RECURSIVE_ENTITY_REF);
      } else {
        assert_true(status == XML_STATUS_OK);
      }

      if (reset_wanted) {
        // This covers free'ing of (eventually) all three open entity lists by
        // XML_ParserReset.
        XML_ParserReset(parser, NULL);
      }

      // This covers free'ing of (eventually) all three open entity lists by
      // XML_ParserFree (unless XML_ParserReset has already done that above).
      XML_ParserFree(parser);
    }
  }
}
END_TEST

START_TEST(test_recursive_external_parameter_entity_2) {
  struct TestCase {
    const char *doc;
    enum XML_Status expectedStatus;
  };

  struct TestCase cases[] = {
      {"<!ENTITY % p1 '%p1;'>", XML_STATUS_ERROR},
      {"<!ENTITY % p1 '%p1;'>"
       "<!ENTITY % p1 'first declaration wins'>",
       XML_STATUS_ERROR},
      {"<!ENTITY % p1 'first declaration wins'>"
       "<!ENTITY % p1 '%p1;'>",
       XML_STATUS_OK},
      {"<!ENTITY % p1 '&#37;p1;'>", XML_STATUS_OK},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const char *const doc = cases[i].doc;
    const enum XML_Status expectedStatus = cases[i].expectedStatus;
    set_subtest("%s", doc);

    XML_Parser parser = XML_ParserCreate(NULL);
    assert_true(parser != NULL);

    XML_Parser ext_parser = XML_ExternalEntityParserCreate(parser, NULL, NULL);
    assert_true(ext_parser != NULL);

    const enum XML_Status actualStatus
        = _XML_Parse_SINGLE_BYTES(ext_parser, doc, (int)strlen(doc), XML_TRUE);

    assert_true(actualStatus == expectedStatus);
    if (actualStatus != XML_STATUS_OK) {
      assert_true(XML_GetErrorCode(ext_parser)
                  == XML_ERROR_RECURSIVE_ENTITY_REF);
    }

    XML_ParserFree(ext_parser);
    XML_ParserFree(parser);
  }
}
END_TEST

/* Test incomplete external entities are faulted */
START_TEST(test_ext_entity_invalid_parse) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  const ExtFaults faults[]
      = {{"<", "Incomplete element declaration not faulted", NULL,
          XML_ERROR_UNCLOSED_TOKEN},
         {"<\xe2\x82", /* First two bytes of a three-byte char */
          "Incomplete character not faulted", NULL, XML_ERROR_PARTIAL_CHAR},
         {"<tag>\xe2\x82", "Incomplete character in CDATA not faulted", NULL,
          XML_ERROR_PARTIAL_CHAR},
         {NULL, NULL, NULL, XML_ERROR_NONE}};
  const ExtFaults *fault = faults;

  for (; fault->parse_text != NULL; fault++) {
    set_subtest("\"%s\"", fault->parse_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_faulter);
    XML_SetUserData(g_parser, (void *)fault);
    expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                   "Parser did not report external entity error");
    XML_ParserReset(g_parser, NULL);
  }
}
END_TEST

/* Regression test for SF bug #483514. */
START_TEST(test_dtd_default_handling) {
  const char *text = "<!DOCTYPE doc [\n"
                     "<!ENTITY e SYSTEM 'http://example.org/e'>\n"
                     "<!NOTATION n SYSTEM 'http://example.org/n'>\n"
                     "<!ELEMENT doc EMPTY>\n"
                     "<!ATTLIST doc a CDATA #IMPLIED>\n"
                     "<?pi in dtd?>\n"
                     "<!--comment in dtd-->\n"
                     "]><doc/>";

  XML_SetDefaultHandler(g_parser, accumulate_characters);
  XML_SetStartDoctypeDeclHandler(g_parser, dummy_start_doctype_handler);
  XML_SetEndDoctypeDeclHandler(g_parser, dummy_end_doctype_handler);
  XML_SetEntityDeclHandler(g_parser, dummy_entity_decl_handler);
  XML_SetNotationDeclHandler(g_parser, dummy_notation_decl_handler);
  XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
  XML_SetAttlistDeclHandler(g_parser, dummy_attlist_decl_handler);
  XML_SetProcessingInstructionHandler(g_parser, dummy_pi_handler);
  XML_SetCommentHandler(g_parser, dummy_comment_handler);
  XML_SetStartCdataSectionHandler(g_parser, dummy_start_cdata_handler);
  XML_SetEndCdataSectionHandler(g_parser, dummy_end_cdata_handler);
  run_character_check(text, XCS("\n\n\n\n\n\n\n<doc/>"));
}
END_TEST

/* Test handling of attribute declarations */
START_TEST(test_dtd_attr_handling) {
  const char *prolog = "<!DOCTYPE doc [\n"
                       "<!ELEMENT doc EMPTY>\n";
  AttTest attr_data[]
      = {{"<!ATTLIST doc a ( one | two | three ) #REQUIRED>\n"
          "]>"
          "<doc a='two'/>",
          XCS("doc"), XCS("a"),
          XCS("(one|two|three)"), /* Extraneous spaces will be removed */
          NULL, XML_TRUE},
         {"<!NOTATION foo SYSTEM 'http://example.org/foo'>\n"
          "<!ATTLIST doc a NOTATION (foo) #IMPLIED>\n"
          "]>"
          "<doc/>",
          XCS("doc"), XCS("a"), XCS("NOTATION(foo)"), NULL, XML_FALSE},
         {"<!ATTLIST doc a NOTATION (foo) 'bar'>\n"
          "]>"
          "<doc/>",
          XCS("doc"), XCS("a"), XCS("NOTATION(foo)"), XCS("bar"), XML_FALSE},
         {"<!ATTLIST doc a CDATA '\xdb\xb2'>\n"
          "]>"
          "<doc/>",
          XCS("doc"), XCS("a"), XCS("CDATA"),
#ifdef XML_UNICODE
          XCS("\x06f2"),
#else
          XCS("\xdb\xb2"),
#endif
          XML_FALSE},
         {NULL, NULL, NULL, NULL, NULL, XML_FALSE}};
  AttTest *test;

  for (test = attr_data; test->definition != NULL; test++) {
    set_subtest("%s", test->definition);
    XML_SetAttlistDeclHandler(g_parser, verify_attlist_decl_handler);
    XML_SetUserData(g_parser, test);
    if (_XML_Parse_SINGLE_BYTES(g_parser, prolog, (int)strlen(prolog),
                                XML_FALSE)
        == XML_STATUS_ERROR)
      xml_failure(g_parser);
    if (_XML_Parse_SINGLE_BYTES(g_parser, test->definition,
                                (int)strlen(test->definition), XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(g_parser);
    XML_ParserReset(g_parser, NULL);
  }
}
END_TEST

/* See related SF bug #673791.
   When namespace processing is enabled, setting the namespace URI for
   a prefix is not allowed; this test ensures that it *is* allowed
   when namespace processing is not enabled.
   (See Namespaces in XML, section 2.)
*/
START_TEST(test_empty_ns_without_namespaces) {
  const char *text = "<doc xmlns:prefix='http://example.org/'>\n"
                     "  <e xmlns:prefix=''/>\n"
                     "</doc>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Regression test for SF bug #824420.
   Checks that an xmlns:prefix attribute set in an attribute's default
   value isn't misinterpreted.
*/
START_TEST(test_ns_in_attribute_default_without_namespaces) {
  const char *text = "<!DOCTYPE e:element [\n"
                     "  <!ATTLIST e:element\n"
                     "    xmlns:e CDATA 'http://example.org/'>\n"
                     "      ]>\n"
                     "<e:element/>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Regression test for SF bug #1515266: missing check of stopped
   parser in doContext() 'for' loop. */
START_TEST(test_stop_parser_between_char_data_calls) {
  /* The sample data must be big enough that there are two calls to
     the character data handler from within the inner "for" loop of
     the XML_TOK_DATA_CHARS case in doContent(), and the character
     handler must stop the parser and clear the character data
     handler.
  */
  const char *text = long_character_data_text;

  XML_SetCharacterDataHandler(g_parser, clearing_aborting_character_handler);
  g_resumable = XML_FALSE;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (XML_GetErrorCode(g_parser) != XML_ERROR_ABORTED)
    xml_failure(g_parser);
}
END_TEST

/* Regression test for SF bug #1515266: missing check of stopped
   parser in doContext() 'for' loop. */
START_TEST(test_suspend_parser_between_char_data_calls) {
  /* The sample data must be big enough that there are two calls to
     the character data handler from within the inner "for" loop of
     the XML_TOK_DATA_CHARS case in doContent(), and the character
     handler must stop the parser and clear the character data
     handler.
  */
  const char *text = long_character_data_text;

  XML_SetCharacterDataHandler(g_parser, clearing_aborting_character_handler);
  g_resumable = XML_TRUE;
  // can't use SINGLE_BYTES here, because it'll return early on suspension, and
  // we won't know exactly how much input we actually managed to give Expat.
  if (XML_Parse(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_SUSPENDED)
    xml_failure(g_parser);
  if (XML_GetErrorCode(g_parser) != XML_ERROR_NONE)
    xml_failure(g_parser);
  /* Try parsing directly */
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Attempt to continue parse while suspended not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_SUSPENDED)
    fail("Suspended parse not faulted with correct error");
}
END_TEST

/* Test repeated calls to XML_StopParser are handled correctly */
START_TEST(test_repeated_stop_parser_between_char_data_calls) {
  const char *text = long_character_data_text;

  XML_SetCharacterDataHandler(g_parser, parser_stop_character_handler);
  g_resumable = XML_FALSE;
  g_abortable = XML_FALSE;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Failed to double-stop parser");

  XML_ParserReset(g_parser, NULL);
  XML_SetCharacterDataHandler(g_parser, parser_stop_character_handler);
  g_resumable = XML_TRUE;
  g_abortable = XML_FALSE;
  // can't use SINGLE_BYTES here, because it'll return early on suspension, and
  // we won't know exactly how much input we actually managed to give Expat.
  if (XML_Parse(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_SUSPENDED)
    fail("Failed to double-suspend parser");

  XML_ParserReset(g_parser, NULL);
  XML_SetCharacterDataHandler(g_parser, parser_stop_character_handler);
  g_resumable = XML_TRUE;
  g_abortable = XML_TRUE;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Failed to suspend-abort parser");
}
END_TEST

START_TEST(test_good_cdata_ascii) {
  const char *text = "<a><![CDATA[<greeting>Hello, world!</greeting>]]></a>";
  const XML_Char *expected = XCS("<greeting>Hello, world!</greeting>");

  CharData storage;
  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetCharacterDataHandler(g_parser, accumulate_characters);
  /* Add start and end handlers for coverage */
  XML_SetStartCdataSectionHandler(g_parser, dummy_start_cdata_handler);
  XML_SetEndCdataSectionHandler(g_parser, dummy_end_cdata_handler);

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);

  /* Try again, this time with a default handler */
  XML_ParserReset(g_parser, NULL);
  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetCharacterDataHandler(g_parser, accumulate_characters);
  XML_SetDefaultHandler(g_parser, dummy_default_handler);

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_good_cdata_utf16) {
  /* Test data is:
   *   <?xml version='1.0' encoding='utf-16'?>
   *   <a><![CDATA[hello]]></a>
   */
  const char text[]
      = "\0<\0?\0x\0m\0l\0"
        " \0v\0e\0r\0s\0i\0o\0n\0=\0'\0\x31\0.\0\x30\0'\0"
        " \0e\0n\0c\0o\0d\0i\0n\0g\0=\0'\0u\0t\0f\0-\0"
        "1\0"
        "6\0'"
        "\0?\0>\0\n"
        "\0<\0a\0>\0<\0!\0[\0C\0D\0A\0T\0A\0[\0h\0e\0l\0l\0o\0]\0]\0>\0<\0/\0a\0>";
  const XML_Char *expected = XCS("hello");

  CharData storage;
  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetCharacterDataHandler(g_parser, accumulate_characters);

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_good_cdata_utf16_le) {
  /* Test data is:
   *   <?xml version='1.0' encoding='utf-16'?>
   *   <a><![CDATA[hello]]></a>
   */
  const char text[]
      = "<\0?\0x\0m\0l\0"
        " \0v\0e\0r\0s\0i\0o\0n\0=\0'\0\x31\0.\0\x30\0'\0"
        " \0e\0n\0c\0o\0d\0i\0n\0g\0=\0'\0u\0t\0f\0-\0"
        "1\0"
        "6\0'"
        "\0?\0>\0\n"
        "\0<\0a\0>\0<\0!\0[\0C\0D\0A\0T\0A\0[\0h\0e\0l\0l\0o\0]\0]\0>\0<\0/\0a\0>\0";
  const XML_Char *expected = XCS("hello");

  CharData storage;
  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetCharacterDataHandler(g_parser, accumulate_characters);

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test UTF16 conversion of a long cdata string */

/* 16 characters: handy macro to reduce visual clutter */
#define A_TO_P_IN_UTF16 "\0A\0B\0C\0D\0E\0F\0G\0H\0I\0J\0K\0L\0M\0N\0O\0P"

START_TEST(test_long_cdata_utf16) {
  /* Test data is:
   * <?xlm version='1.0' encoding='utf-16'?>
   * <a><![CDATA[
   * ABCDEFGHIJKLMNOP
   * ]]></a>
   */
  const char text[]
      = "\0<\0?\0x\0m\0l\0 "
        "\0v\0e\0r\0s\0i\0o\0n\0=\0'\0\x31\0.\0\x30\0'\0 "
        "\0e\0n\0c\0o\0d\0i\0n\0g\0=\0'\0u\0t\0f\0-\0\x31\0\x36\0'\0?\0>"
        "\0<\0a\0>\0<\0!\0[\0C\0D\0A\0T\0A\0["
      /* 64 characters per line */
      /* clang-format off */
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16  A_TO_P_IN_UTF16
        A_TO_P_IN_UTF16
        /* clang-format on */
        "\0]\0]\0>\0<\0/\0a\0>";
  const XML_Char *expected =
      /* clang-format off */
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP")
        XCS("ABCDEFGHIJKLMNOP");
  /* clang-format on */
  CharData storage;
  void *buffer;

  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetCharacterDataHandler(g_parser, accumulate_characters);
  buffer = XML_GetBuffer(g_parser, sizeof(text) - 1);
  if (buffer == NULL)
    fail("Could not allocate parse buffer");
  assert(buffer != NULL);
  memcpy(buffer, text, sizeof(text) - 1);
  if (XML_ParseBuffer(g_parser, sizeof(text) - 1, XML_TRUE) == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test handling of multiple unit UTF-16 characters */
START_TEST(test_multichar_cdata_utf16) {
  /* Test data is:
   *   <?xml version='1.0' encoding='utf-16'?>
   *   <a><![CDATA[{MINIM}{CROTCHET}]]></a>
   *
   * where {MINIM} is U+1d15e (a minim or half-note)
   *   UTF-16: 0xd834 0xdd5e
   *   UTF-8:  0xf0 0x9d 0x85 0x9e
   * and {CROTCHET} is U+1d15f (a crotchet or quarter-note)
   *   UTF-16: 0xd834 0xdd5f
   *   UTF-8:  0xf0 0x9d 0x85 0x9f
   */
  const char text[] = "\0<\0?\0x\0m\0l\0"
                      " \0v\0e\0r\0s\0i\0o\0n\0=\0'\0\x31\0.\0\x30\0'\0"
                      " \0e\0n\0c\0o\0d\0i\0n\0g\0=\0'\0u\0t\0f\0-\0"
                      "1\0"
                      "6\0'"
                      "\0?\0>\0\n"
                      "\0<\0a\0>\0<\0!\0[\0C\0D\0A\0T\0A\0["
                      "\xd8\x34\xdd\x5e\xd8\x34\xdd\x5f"
                      "\0]\0]\0>\0<\0/\0a\0>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\xd834\xdd5e\xd834\xdd5f");
#else
  const XML_Char *expected = XCS("\xf0\x9d\x85\x9e\xf0\x9d\x85\x9f");
#endif
  CharData storage;

  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetCharacterDataHandler(g_parser, accumulate_characters);

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test that an element name with a UTF-16 surrogate pair is rejected */
START_TEST(test_utf16_bad_surrogate_pair) {
  /* Test data is:
   *   <?xml version='1.0' encoding='utf-16'?>
   *   <a><![CDATA[{BADLINB}]]></a>
   *
   * where {BADLINB} is U+10000 (the first Linear B character)
   * with the UTF-16 surrogate pair in the wrong order, i.e.
   *   0xdc00 0xd800
   */
  const char text[] = "\0<\0?\0x\0m\0l\0"
                      " \0v\0e\0r\0s\0i\0o\0n\0=\0'\0\x31\0.\0\x30\0'\0"
                      " \0e\0n\0c\0o\0d\0i\0n\0g\0=\0'\0u\0t\0f\0-\0"
                      "1\0"
                      "6\0'"
                      "\0?\0>\0\n"
                      "\0<\0a\0>\0<\0!\0[\0C\0D\0A\0T\0A\0["
                      "\xdc\x00\xd8\x00"
                      "\0]\0]\0>\0<\0/\0a\0>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Reversed UTF-16 surrogate pair not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_INVALID_TOKEN)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_bad_cdata) {
  struct CaseData {
    const char *text;
    enum XML_Error expectedError;
  };

  struct CaseData cases[]
      = {{"<a><", XML_ERROR_UNCLOSED_TOKEN},
         {"<a><!", XML_ERROR_UNCLOSED_TOKEN},
         {"<a><![", XML_ERROR_UNCLOSED_TOKEN},
         {"<a><![C", XML_ERROR_UNCLOSED_TOKEN},
         {"<a><![CD", XML_ERROR_UNCLOSED_TOKEN},
         {"<a><![CDA", XML_ERROR_UNCLOSED_TOKEN},
         {"<a><![CDAT", XML_ERROR_UNCLOSED_TOKEN},
         {"<a><![CDATA", XML_ERROR_UNCLOSED_TOKEN},

         {"<a><![CDATA[", XML_ERROR_UNCLOSED_CDATA_SECTION},
         {"<a><![CDATA[]", XML_ERROR_UNCLOSED_CDATA_SECTION},
         {"<a><![CDATA[]]", XML_ERROR_UNCLOSED_CDATA_SECTION},

         {"<a><!<a/>", XML_ERROR_INVALID_TOKEN},
         {"<a><![<a/>", XML_ERROR_UNCLOSED_TOKEN},  /* ?! */
         {"<a><![C<a/>", XML_ERROR_UNCLOSED_TOKEN}, /* ?! */
         {"<a><![CD<a/>", XML_ERROR_INVALID_TOKEN},
         {"<a><![CDA<a/>", XML_ERROR_INVALID_TOKEN},
         {"<a><![CDAT<a/>", XML_ERROR_INVALID_TOKEN},
         {"<a><![CDATA<a/>", XML_ERROR_INVALID_TOKEN},

         {"<a><![CDATA[<a/>", XML_ERROR_UNCLOSED_CDATA_SECTION},
         {"<a><![CDATA[]<a/>", XML_ERROR_UNCLOSED_CDATA_SECTION},
         {"<a><![CDATA[]]<a/>", XML_ERROR_UNCLOSED_CDATA_SECTION}};

  size_t i = 0;
  for (; i < sizeof(cases) / sizeof(struct CaseData); i++) {
    set_subtest("%s", cases[i].text);
    const enum XML_Status actualStatus = _XML_Parse_SINGLE_BYTES(
        g_parser, cases[i].text, (int)strlen(cases[i].text), XML_TRUE);
    const enum XML_Error actualError = XML_GetErrorCode(g_parser);

    assert(actualStatus == XML_STATUS_ERROR);

    if (actualError != cases[i].expectedError) {
      char message[100];
      snprintf(message, sizeof(message),
               "Expected error %d but got error %d for case %u: \"%s\"\n",
               cases[i].expectedError, actualError, (unsigned int)i + 1,
               cases[i].text);
      fail(message);
    }

    XML_ParserReset(g_parser, NULL);
  }
}
END_TEST

/* Test failures in UTF-16 CDATA */
START_TEST(test_bad_cdata_utf16) {
  struct CaseData {
    size_t text_bytes;
    const char *text;
    enum XML_Error expected_error;
  };

  const char prolog[] = "\0<\0?\0x\0m\0l\0"
                        " \0v\0e\0r\0s\0i\0o\0n\0=\0'\0\x31\0.\0\x30\0'\0"
                        " \0e\0n\0c\0o\0d\0i\0n\0g\0=\0'\0u\0t\0f\0-\0"
                        "1\0"
                        "6\0'"
                        "\0?\0>\0\n"
                        "\0<\0a\0>";
  struct CaseData cases[] = {
      {1, "\0", XML_ERROR_UNCLOSED_TOKEN},
      {2, "\0<", XML_ERROR_UNCLOSED_TOKEN},
      {3, "\0<\0", XML_ERROR_UNCLOSED_TOKEN},
      {4, "\0<\0!", XML_ERROR_UNCLOSED_TOKEN},
      {5, "\0<\0!\0", XML_ERROR_UNCLOSED_TOKEN},
      {6, "\0<\0!\0[", XML_ERROR_UNCLOSED_TOKEN},
      {7, "\0<\0!\0[\0", XML_ERROR_UNCLOSED_TOKEN},
      {8, "\0<\0!\0[\0C", XML_ERROR_UNCLOSED_TOKEN},
      {9, "\0<\0!\0[\0C\0", XML_ERROR_UNCLOSED_TOKEN},
      {10, "\0<\0!\0[\0C\0D", XML_ERROR_UNCLOSED_TOKEN},
      {11, "\0<\0!\0[\0C\0D\0", XML_ERROR_UNCLOSED_TOKEN},
      {12, "\0<\0!\0[\0C\0D\0A", XML_ERROR_UNCLOSED_TOKEN},
      {13, "\0<\0!\0[\0C\0D\0A\0", XML_ERROR_UNCLOSED_TOKEN},
      {14, "\0<\0!\0[\0C\0D\0A\0T", XML_ERROR_UNCLOSED_TOKEN},
      {15, "\0<\0!\0[\0C\0D\0A\0T\0", XML_ERROR_UNCLOSED_TOKEN},
      {16, "\0<\0!\0[\0C\0D\0A\0T\0A", XML_ERROR_UNCLOSED_TOKEN},
      {17, "\0<\0!\0[\0C\0D\0A\0T\0A\0", XML_ERROR_UNCLOSED_TOKEN},
      {18, "\0<\0!\0[\0C\0D\0A\0T\0A\0[", XML_ERROR_UNCLOSED_CDATA_SECTION},
      {19, "\0<\0!\0[\0C\0D\0A\0T\0A\0[\0", XML_ERROR_UNCLOSED_CDATA_SECTION},
      {20, "\0<\0!\0[\0C\0D\0A\0T\0A\0[\0Z", XML_ERROR_UNCLOSED_CDATA_SECTION},
      /* Now add a four-byte UTF-16 character */
      {21, "\0<\0!\0[\0C\0D\0A\0T\0A\0[\0Z\xd8",
       XML_ERROR_UNCLOSED_CDATA_SECTION},
      {22, "\0<\0!\0[\0C\0D\0A\0T\0A\0[\0Z\xd8\x34", XML_ERROR_PARTIAL_CHAR},
      {23, "\0<\0!\0[\0C\0D\0A\0T\0A\0[\0Z\xd8\x34\xdd",
       XML_ERROR_PARTIAL_CHAR},
      {24, "\0<\0!\0[\0C\0D\0A\0T\0A\0[\0Z\xd8\x34\xdd\x5e",
       XML_ERROR_UNCLOSED_CDATA_SECTION}};
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(struct CaseData); i++) {
    set_subtest("case %lu", (long unsigned)(i + 1));
    enum XML_Status actual_status;
    enum XML_Error actual_error;

    if (_XML_Parse_SINGLE_BYTES(g_parser, prolog, (int)sizeof(prolog) - 1,
                                XML_FALSE)
        == XML_STATUS_ERROR)
      xml_failure(g_parser);
    actual_status = _XML_Parse_SINGLE_BYTES(g_parser, cases[i].text,
                                            (int)cases[i].text_bytes, XML_TRUE);
    assert(actual_status == XML_STATUS_ERROR);
    actual_error = XML_GetErrorCode(g_parser);
    if (actual_error != cases[i].expected_error) {
      char message[1024];

      snprintf(message, sizeof(message),
               "Expected error %d (%" XML_FMT_STR "), got %d (%" XML_FMT_STR
               ") for case %lu\n",
               cases[i].expected_error,
               XML_ErrorString(cases[i].expected_error), actual_error,
               XML_ErrorString(actual_error), (long unsigned)(i + 1));
      fail(message);
    }
    XML_ParserReset(g_parser, NULL);
  }
}
END_TEST

/* Test stopping the parser in cdata handler */
START_TEST(test_stop_parser_between_cdata_calls) {
  const char *text = long_cdata_text;

  XML_SetCharacterDataHandler(g_parser, clearing_aborting_character_handler);
  g_resumable = XML_FALSE;
  expect_failure(text, XML_ERROR_ABORTED, "Parse not aborted in CDATA handler");
}
END_TEST

/* Test suspending the parser in cdata handler */
START_TEST(test_suspend_parser_between_cdata_calls) {
  if (g_chunkSize != 0) {
    // this test does not use SINGLE_BYTES, because of suspension
    return;
  }

  const char *text = long_cdata_text;
  enum XML_Status result;

  XML_SetCharacterDataHandler(g_parser, clearing_aborting_character_handler);
  g_resumable = XML_TRUE;
  // can't use SINGLE_BYTES here, because it'll return early on suspension, and
  // we won't know exactly how much input we actually managed to give Expat.
  result = XML_Parse(g_parser, text, (int)strlen(text), XML_TRUE);
  if (result != XML_STATUS_SUSPENDED) {
    if (result == XML_STATUS_ERROR)
      xml_failure(g_parser);
    fail("Parse not suspended in CDATA handler");
  }
  if (XML_GetErrorCode(g_parser) != XML_ERROR_NONE)
    xml_failure(g_parser);
}
END_TEST

/* Test memory allocation functions */
START_TEST(test_memory_allocation) {
  char *buffer = (char *)XML_MemMalloc(g_parser, 256);
  char *p;

  if (buffer == NULL) {
    fail("Allocation failed");
  } else {
    /* Try writing to memory; some OSes try to cheat! */
    buffer[0] = 'T';
    buffer[1] = 'E';
    buffer[2] = 'S';
    buffer[3] = 'T';
    buffer[4] = '\0';
    if (strcmp(buffer, "TEST") != 0) {
      fail("Memory not writable");
    } else {
      p = (char *)XML_MemRealloc(g_parser, buffer, 512);
      if (p == NULL) {
        fail("Reallocation failed");
      } else {
        /* Write again, just to be sure */
        buffer = p;
        buffer[0] = 'V';
        if (strcmp(buffer, "VEST") != 0) {
          fail("Reallocated memory not writable");
        }
      }
    }
    XML_MemFree(g_parser, buffer);
  }
}
END_TEST

/* Test XML_DefaultCurrent() passes handling on correctly */
START_TEST(test_default_current) {
  const char *text = "<doc>hell]</doc>";
  const char *entity_text = "<!DOCTYPE doc [\n"
                            "<!ENTITY entity '&#37;'>\n"
                            "]>\n"
                            "<doc>&entity;</doc>";

  set_subtest("with defaulting");
  {
    struct handler_record_list storage;
    storage.count = 0;
    XML_SetDefaultHandler(g_parser, record_default_handler);
    XML_SetCharacterDataHandler(g_parser, record_cdata_handler);
    XML_SetUserData(g_parser, &storage);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(g_parser);
    int i = 0;
    assert_record_handler_called(&storage, i++, "record_default_handler", 5);
    // we should have gotten one or more cdata callbacks, totaling 5 chars
    int cdata_len_remaining = 5;
    while (cdata_len_remaining > 0) {
      const struct handler_record_entry *c_entry
          = handler_record_get(&storage, i++);
      assert_true(strcmp(c_entry->name, "record_cdata_handler") == 0);
      assert_true(c_entry->arg > 0);
      assert_true(c_entry->arg <= cdata_len_remaining);
      cdata_len_remaining -= c_entry->arg;
      // default handler must follow, with the exact same len argument.
      assert_record_handler_called(&storage, i++, "record_default_handler",
                                   c_entry->arg);
    }
    assert_record_handler_called(&storage, i++, "record_default_handler", 6);
    assert_true(storage.count == i);
  }

  /* Again, without the defaulting */
  set_subtest("no defaulting");
  {
    struct handler_record_list storage;
    storage.count = 0;
    XML_ParserReset(g_parser, NULL);
    XML_SetDefaultHandler(g_parser, record_default_handler);
    XML_SetCharacterDataHandler(g_parser, record_cdata_nodefault_handler);
    XML_SetUserData(g_parser, &storage);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(g_parser);
    int i = 0;
    assert_record_handler_called(&storage, i++, "record_default_handler", 5);
    // we should have gotten one or more cdata callbacks, totaling 5 chars
    int cdata_len_remaining = 5;
    while (cdata_len_remaining > 0) {
      const struct handler_record_entry *c_entry
          = handler_record_get(&storage, i++);
      assert_true(strcmp(c_entry->name, "record_cdata_nodefault_handler") == 0);
      assert_true(c_entry->arg > 0);
      assert_true(c_entry->arg <= cdata_len_remaining);
      cdata_len_remaining -= c_entry->arg;
    }
    assert_record_handler_called(&storage, i++, "record_default_handler", 6);
    assert_true(storage.count == i);
  }

  /* Now with an internal entity to complicate matters */
  set_subtest("with internal entity");
  {
    struct handler_record_list storage;
    storage.count = 0;
    XML_ParserReset(g_parser, NULL);
    XML_SetDefaultHandler(g_parser, record_default_handler);
    XML_SetCharacterDataHandler(g_parser, record_cdata_handler);
    XML_SetUserData(g_parser, &storage);
    if (_XML_Parse_SINGLE_BYTES(g_parser, entity_text, (int)strlen(entity_text),
                                XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(g_parser);
    /* The default handler suppresses the entity */
    assert_record_handler_called(&storage, 0, "record_default_handler", 9);
    assert_record_handler_called(&storage, 1, "record_default_handler", 1);
    assert_record_handler_called(&storage, 2, "record_default_handler", 3);
    assert_record_handler_called(&storage, 3, "record_default_handler", 1);
    assert_record_handler_called(&storage, 4, "record_default_handler", 1);
    assert_record_handler_called(&storage, 5, "record_default_handler", 1);
    assert_record_handler_called(&storage, 6, "record_default_handler", 8);
    assert_record_handler_called(&storage, 7, "record_default_handler", 1);
    assert_record_handler_called(&storage, 8, "record_default_handler", 6);
    assert_record_handler_called(&storage, 9, "record_default_handler", 1);
    assert_record_handler_called(&storage, 10, "record_default_handler", 7);
    assert_record_handler_called(&storage, 11, "record_default_handler", 1);
    assert_record_handler_called(&storage, 12, "record_default_handler", 1);
    assert_record_handler_called(&storage, 13, "record_default_handler", 1);
    assert_record_handler_called(&storage, 14, "record_default_handler", 1);
    assert_record_handler_called(&storage, 15, "record_default_handler", 1);
    assert_record_handler_called(&storage, 16, "record_default_handler", 5);
    assert_record_handler_called(&storage, 17, "record_default_handler", 8);
    assert_record_handler_called(&storage, 18, "record_default_handler", 6);
    assert_true(storage.count == 19);
  }

  /* Again, with a skip handler */
  set_subtest("with skip handler");
  {
    struct handler_record_list storage;
    storage.count = 0;
    XML_ParserReset(g_parser, NULL);
    XML_SetDefaultHandler(g_parser, record_default_handler);
    XML_SetCharacterDataHandler(g_parser, record_cdata_handler);
    XML_SetSkippedEntityHandler(g_parser, record_skip_handler);
    XML_SetUserData(g_parser, &storage);
    if (_XML_Parse_SINGLE_BYTES(g_parser, entity_text, (int)strlen(entity_text),
                                XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(g_parser);
    /* The default handler suppresses the entity */
    assert_record_handler_called(&storage, 0, "record_default_handler", 9);
    assert_record_handler_called(&storage, 1, "record_default_handler", 1);
    assert_record_handler_called(&storage, 2, "record_default_handler", 3);
    assert_record_handler_called(&storage, 3, "record_default_handler", 1);
    assert_record_handler_called(&storage, 4, "record_default_handler", 1);
    assert_record_handler_called(&storage, 5, "record_default_handler", 1);
    assert_record_handler_called(&storage, 6, "record_default_handler", 8);
    assert_record_handler_called(&storage, 7, "record_default_handler", 1);
    assert_record_handler_called(&storage, 8, "record_default_handler", 6);
    assert_record_handler_called(&storage, 9, "record_default_handler", 1);
    assert_record_handler_called(&storage, 10, "record_default_handler", 7);
    assert_record_handler_called(&storage, 11, "record_default_handler", 1);
    assert_record_handler_called(&storage, 12, "record_default_handler", 1);
    assert_record_handler_called(&storage, 13, "record_default_handler", 1);
    assert_record_handler_called(&storage, 14, "record_default_handler", 1);
    assert_record_handler_called(&storage, 15, "record_default_handler", 1);
    assert_record_handler_called(&storage, 16, "record_default_handler", 5);
    assert_record_handler_called(&storage, 17, "record_skip_handler", 0);
    assert_record_handler_called(&storage, 18, "record_default_handler", 6);
    assert_true(storage.count == 19);
  }

  /* This time, allow the entity through */
  set_subtest("allow entity");
  {
    struct handler_record_list storage;
    storage.count = 0;
    XML_ParserReset(g_parser, NULL);
    XML_SetDefaultHandlerExpand(g_parser, record_default_handler);
    XML_SetCharacterDataHandler(g_parser, record_cdata_handler);
    XML_SetUserData(g_parser, &storage);
    if (_XML_Parse_SINGLE_BYTES(g_parser, entity_text, (int)strlen(entity_text),
                                XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(g_parser);
    assert_record_handler_called(&storage, 0, "record_default_handler", 9);
    assert_record_handler_called(&storage, 1, "record_default_handler", 1);
    assert_record_handler_called(&storage, 2, "record_default_handler", 3);
    assert_record_handler_called(&storage, 3, "record_default_handler", 1);
    assert_record_handler_called(&storage, 4, "record_default_handler", 1);
    assert_record_handler_called(&storage, 5, "record_default_handler", 1);
    assert_record_handler_called(&storage, 6, "record_default_handler", 8);
    assert_record_handler_called(&storage, 7, "record_default_handler", 1);
    assert_record_handler_called(&storage, 8, "record_default_handler", 6);
    assert_record_handler_called(&storage, 9, "record_default_handler", 1);
    assert_record_handler_called(&storage, 10, "record_default_handler", 7);
    assert_record_handler_called(&storage, 11, "record_default_handler", 1);
    assert_record_handler_called(&storage, 12, "record_default_handler", 1);
    assert_record_handler_called(&storage, 13, "record_default_handler", 1);
    assert_record_handler_called(&storage, 14, "record_default_handler", 1);
    assert_record_handler_called(&storage, 15, "record_default_handler", 1);
    assert_record_handler_called(&storage, 16, "record_default_handler", 5);
    assert_record_handler_called(&storage, 17, "record_cdata_handler", 1);
    assert_record_handler_called(&storage, 18, "record_default_handler", 1);
    assert_record_handler_called(&storage, 19, "record_default_handler", 6);
    assert_true(storage.count == 20);
  }

  /* Finally, without passing the cdata to the default handler */
  set_subtest("not passing cdata");
  {
    struct handler_record_list storage;
    storage.count = 0;
    XML_ParserReset(g_parser, NULL);
    XML_SetDefaultHandlerExpand(g_parser, record_default_handler);
    XML_SetCharacterDataHandler(g_parser, record_cdata_nodefault_handler);
    XML_SetUserData(g_parser, &storage);
    if (_XML_Parse_SINGLE_BYTES(g_parser, entity_text, (int)strlen(entity_text),
                                XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(g_parser);
    assert_record_handler_called(&storage, 0, "record_default_handler", 9);
    assert_record_handler_called(&storage, 1, "record_default_handler", 1);
    assert_record_handler_called(&storage, 2, "record_default_handler", 3);
    assert_record_handler_called(&storage, 3, "record_default_handler", 1);
    assert_record_handler_called(&storage, 4, "record_default_handler", 1);
    assert_record_handler_called(&storage, 5, "record_default_handler", 1);
    assert_record_handler_called(&storage, 6, "record_default_handler", 8);
    assert_record_handler_called(&storage, 7, "record_default_handler", 1);
    assert_record_handler_called(&storage, 8, "record_default_handler", 6);
    assert_record_handler_called(&storage, 9, "record_default_handler", 1);
    assert_record_handler_called(&storage, 10, "record_default_handler", 7);
    assert_record_handler_called(&storage, 11, "record_default_handler", 1);
    assert_record_handler_called(&storage, 12, "record_default_handler", 1);
    assert_record_handler_called(&storage, 13, "record_default_handler", 1);
    assert_record_handler_called(&storage, 14, "record_default_handler", 1);
    assert_record_handler_called(&storage, 15, "record_default_handler", 1);
    assert_record_handler_called(&storage, 16, "record_default_handler", 5);
    assert_record_handler_called(&storage, 17, "record_cdata_nodefault_handler",
                                 1);
    assert_record_handler_called(&storage, 18, "record_default_handler", 6);
    assert_true(storage.count == 19);
  }
}
END_TEST

/* Test DTD element parsing code paths */
START_TEST(test_dtd_elements) {
  const char *text = "<!DOCTYPE doc [\n"
                     "<!ELEMENT doc (chapter)>\n"
                     "<!ELEMENT chapter (#PCDATA)>\n"
                     "]>\n"
                     "<doc><chapter>Wombats are go</chapter></doc>";

  XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

static void XMLCALL
element_decl_check_model(void *userData, const XML_Char *name,
                         XML_Content *model) {
  UNUSED_P(userData);
  uint32_t errorFlags = 0;

  /* Expected model array structure is this:
   * [0] (type 6, quant 0)
   *   [1] (type 5, quant 0)
   *     [3] (type 4, quant 0, name "bar")
   *     [4] (type 4, quant 0, name "foo")
   *     [5] (type 4, quant 3, name "xyz")
   *   [2] (type 4, quant 2, name "zebra")
   */
  errorFlags |= ((xcstrcmp(name, XCS("junk")) == 0) ? 0 : (1u << 0));
  errorFlags |= ((model != NULL) ? 0 : (1u << 1));

  if (model != NULL) {
    errorFlags |= ((model[0].type == XML_CTYPE_SEQ) ? 0 : (1u << 2));
    errorFlags |= ((model[0].quant == XML_CQUANT_NONE) ? 0 : (1u << 3));
    errorFlags |= ((model[0].numchildren == 2) ? 0 : (1u << 4));
    errorFlags |= ((model[0].children == &model[1]) ? 0 : (1u << 5));
    errorFlags |= ((model[0].name == NULL) ? 0 : (1u << 6));

    errorFlags |= ((model[1].type == XML_CTYPE_CHOICE) ? 0 : (1u << 7));
    errorFlags |= ((model[1].quant == XML_CQUANT_NONE) ? 0 : (1u << 8));
    errorFlags |= ((model[1].numchildren == 3) ? 0 : (1u << 9));
    errorFlags |= ((model[1].children == &model[3]) ? 0 : (1u << 10));
    errorFlags |= ((model[1].name == NULL) ? 0 : (1u << 11));

    errorFlags |= ((model[2].type == XML_CTYPE_NAME) ? 0 : (1u << 12));
    errorFlags |= ((model[2].quant == XML_CQUANT_REP) ? 0 : (1u << 13));
    errorFlags |= ((model[2].numchildren == 0) ? 0 : (1u << 14));
    errorFlags |= ((model[2].children == NULL) ? 0 : (1u << 15));
    errorFlags
        |= ((xcstrcmp(model[2].name, XCS("zebra")) == 0) ? 0 : (1u << 16));

    errorFlags |= ((model[3].type == XML_CTYPE_NAME) ? 0 : (1u << 17));
    errorFlags |= ((model[3].quant == XML_CQUANT_NONE) ? 0 : (1u << 18));
    errorFlags |= ((model[3].numchildren == 0) ? 0 : (1u << 19));
    errorFlags |= ((model[3].children == NULL) ? 0 : (1u << 20));
    errorFlags |= ((xcstrcmp(model[3].name, XCS("bar")) == 0) ? 0 : (1u << 21));

    errorFlags |= ((model[4].type == XML_CTYPE_NAME) ? 0 : (1u << 22));
    errorFlags |= ((model[4].quant == XML_CQUANT_NONE) ? 0 : (1u << 23));
    errorFlags |= ((model[4].numchildren == 0) ? 0 : (1u << 24));
    errorFlags |= ((model[4].children == NULL) ? 0 : (1u << 25));
    errorFlags |= ((xcstrcmp(model[4].name, XCS("foo")) == 0) ? 0 : (1u << 26));

    errorFlags |= ((model[5].type == XML_CTYPE_NAME) ? 0 : (1u << 27));
    errorFlags |= ((model[5].quant == XML_CQUANT_PLUS) ? 0 : (1u << 28));
    errorFlags |= ((model[5].numchildren == 0) ? 0 : (1u << 29));
    errorFlags |= ((model[5].children == NULL) ? 0 : (1u << 30));
    errorFlags |= ((xcstrcmp(model[5].name, XCS("xyz")) == 0) ? 0 : (1u << 31));
  }

  XML_SetUserData(g_parser, (void *)(uintptr_t)errorFlags);
  XML_FreeContentModel(g_parser, model);
}

START_TEST(test_dtd_elements_nesting) {
  // Payload inspired by a test in Perl's XML::Parser
  const char *text = "<!DOCTYPE foo [\n"
                     "<!ELEMENT junk ((bar|foo|xyz+), zebra*)>\n"
                     "]>\n"
                     "<foo/>";

  XML_SetUserData(g_parser, (void *)(uintptr_t)-1);

  XML_SetElementDeclHandler(g_parser, element_decl_check_model);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);

  if ((uint32_t)(uintptr_t)XML_GetUserData(g_parser) != 0)
    fail("Element declaration model regression detected");
}
END_TEST

/* Test foreign DTD handling */
START_TEST(test_set_foreign_dtd) {
  const char *text1 = "<?xml version='1.0' encoding='us-ascii'?>\n";
  const char *text2 = "<doc>&entity;</doc>";
  ExtTest test_data = {"<!ELEMENT doc (#PCDATA)*>", NULL, NULL};

  /* Check hash salt is passed through too */
  XML_SetHashSalt(g_parser, 0x12345678);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetUserData(g_parser, &test_data);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  /* Add a default handler to exercise more code paths */
  XML_SetDefaultHandler(g_parser, dummy_default_handler);
  if (XML_UseForeignDTD(g_parser, XML_TRUE) != XML_ERROR_NONE)
    fail("Could not set foreign DTD");
  if (_XML_Parse_SINGLE_BYTES(g_parser, text1, (int)strlen(text1), XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);

  /* Ensure that trying to set the DTD after parsing has started
   * is faulted, even if it's the same setting.
   */
  if (XML_UseForeignDTD(g_parser, XML_TRUE)
      != XML_ERROR_CANT_CHANGE_FEATURE_ONCE_PARSING)
    fail("Failed to reject late foreign DTD setting");
  /* Ditto for the hash salt */
  if (XML_SetHashSalt(g_parser, 0x23456789))
    fail("Failed to reject late hash salt change");

  /* Now finish the parse */
  if (_XML_Parse_SINGLE_BYTES(g_parser, text2, (int)strlen(text2), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test foreign DTD handling with a failing NotStandalone handler */
START_TEST(test_foreign_dtd_not_standalone) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<doc>&entity;</doc>";
  ExtTest test_data = {"<!ELEMENT doc (#PCDATA)*>", NULL, NULL};

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetUserData(g_parser, &test_data);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  XML_SetNotStandaloneHandler(g_parser, reject_not_standalone_handler);
  if (XML_UseForeignDTD(g_parser, XML_TRUE) != XML_ERROR_NONE)
    fail("Could not set foreign DTD");
  expect_failure(text, XML_ERROR_NOT_STANDALONE,
                 "NotStandalonehandler failed to reject");
}
END_TEST

/* Test invalid character in a foreign DTD is faulted */
START_TEST(test_invalid_foreign_dtd) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<doc>&entity;</doc>";
  ExtFaults test_data
      = {"$", "Dollar not faulted", NULL, XML_ERROR_INVALID_TOKEN};

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetUserData(g_parser, &test_data);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_faulter);
  XML_UseForeignDTD(g_parser, XML_TRUE);
  expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                 "Bad DTD should not have been accepted");
}
END_TEST

/* Test foreign DTD use with a doctype */
START_TEST(test_foreign_dtd_with_doctype) {
  const char *text1 = "<?xml version='1.0' encoding='us-ascii'?>\n"
                      "<!DOCTYPE doc [<!ENTITY entity 'hello world'>]>\n";
  const char *text2 = "<doc>&entity;</doc>";
  ExtTest test_data = {"<!ELEMENT doc (#PCDATA)*>", NULL, NULL};

  /* Check hash salt is passed through too */
  XML_SetHashSalt(g_parser, 0x12345678);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetUserData(g_parser, &test_data);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  /* Add a default handler to exercise more code paths */
  XML_SetDefaultHandler(g_parser, dummy_default_handler);
  if (XML_UseForeignDTD(g_parser, XML_TRUE) != XML_ERROR_NONE)
    fail("Could not set foreign DTD");
  if (_XML_Parse_SINGLE_BYTES(g_parser, text1, (int)strlen(text1), XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);

  /* Ensure that trying to set the DTD after parsing has started
   * is faulted, even if it's the same setting.
   */
  if (XML_UseForeignDTD(g_parser, XML_TRUE)
      != XML_ERROR_CANT_CHANGE_FEATURE_ONCE_PARSING)
    fail("Failed to reject late foreign DTD setting");
  /* Ditto for the hash salt */
  if (XML_SetHashSalt(g_parser, 0x23456789))
    fail("Failed to reject late hash salt change");

  /* Now finish the parse */
  if (_XML_Parse_SINGLE_BYTES(g_parser, text2, (int)strlen(text2), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test XML_UseForeignDTD with no external subset present */
START_TEST(test_foreign_dtd_without_external_subset) {
  const char *text = "<!DOCTYPE doc [<!ENTITY foo 'bar'>]>\n"
                     "<doc>&foo;</doc>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetUserData(g_parser, NULL);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_null_loader);
  XML_UseForeignDTD(g_parser, XML_TRUE);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_empty_foreign_dtd) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<doc>&entity;</doc>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_null_loader);
  XML_UseForeignDTD(g_parser, XML_TRUE);
  expect_failure(text, XML_ERROR_UNDEFINED_ENTITY,
                 "Undefined entity not faulted");
}
END_TEST

/* Test XML Base is set and unset appropriately */
START_TEST(test_set_base) {
  const XML_Char *old_base;
  const XML_Char *new_base = XCS("/local/file/name.xml");

  old_base = XML_GetBase(g_parser);
  if (XML_SetBase(g_parser, new_base) != XML_STATUS_OK)
    fail("Unable to set base");
  if (xcstrcmp(XML_GetBase(g_parser), new_base) != 0)
    fail("Base setting not correct");
  if (XML_SetBase(g_parser, NULL) != XML_STATUS_OK)
    fail("Unable to NULL base");
  if (XML_GetBase(g_parser) != NULL)
    fail("Base setting not nulled");
  XML_SetBase(g_parser, old_base);
}
END_TEST

/* Test attribute counts, indexing, etc */
START_TEST(test_attributes) {
  const char *text = "<!DOCTYPE doc [\n"
                     "<!ELEMENT doc (tag)>\n"
                     "<!ATTLIST doc id ID #REQUIRED>\n"
                     "]>"
                     "<doc a='1' id='one' b='2'>"
                     "<tag c='3'/>"
                     "</doc>";
  AttrInfo doc_info[] = {{XCS("a"), XCS("1")},
                         {XCS("b"), XCS("2")},
                         {XCS("id"), XCS("one")},
                         {NULL, NULL}};
  AttrInfo tag_info[] = {{XCS("c"), XCS("3")}, {NULL, NULL}};
  ElementInfo info[] = {{XCS("doc"), 3, XCS("id"), NULL},
                        {XCS("tag"), 1, NULL, NULL},
                        {NULL, 0, NULL, NULL}};
  info[0].attributes = doc_info;
  info[1].attributes = tag_info;

  XML_Parser parser = XML_ParserCreate(NULL);
  assert_true(parser != NULL);
  ParserAndElementInfo parserAndElementInfos = {
      parser,
      info,
  };

  XML_SetStartElementHandler(parser, counting_start_element_handler);
  XML_SetUserData(parser, &parserAndElementInfos);
  if (_XML_Parse_SINGLE_BYTES(parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(parser);

  XML_ParserFree(parser);
}
END_TEST

/* Test reset works correctly in the middle of processing an internal
 * entity.  Exercises some obscure code in XML_ParserReset().
 */
START_TEST(test_reset_in_entity) {
  if (g_chunkSize != 0) {
    // this test does not use SINGLE_BYTES, because of suspension
    return;
  }

  const char *text = "<!DOCTYPE doc [\n"
                     "<!ENTITY wombat 'wom'>\n"
                     "<!ENTITY entity 'hi &wom; there'>\n"
                     "]>\n"
                     "<doc>&entity;</doc>";
  XML_ParsingStatus status;

  g_resumable = XML_TRUE;
  XML_SetCharacterDataHandler(g_parser, clearing_aborting_character_handler);
  // can't use SINGLE_BYTES here, because it'll return early on suspension, and
  // we won't know exactly how much input we actually managed to give Expat.
  if (XML_Parse(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  XML_GetParsingStatus(g_parser, &status);
  if (status.parsing != XML_SUSPENDED)
    fail("Parsing status not SUSPENDED");
  XML_ParserReset(g_parser, NULL);
  XML_GetParsingStatus(g_parser, &status);
  if (status.parsing != XML_INITIALIZED)
    fail("Parsing status doesn't reset to INITIALIZED");
}
END_TEST

/* Test that resume correctly passes through parse errors */
START_TEST(test_resume_invalid_parse) {
  const char *text = "<doc>Hello</doc"; /* Missing closing wedge */

  g_resumable = XML_TRUE;
  XML_SetCharacterDataHandler(g_parser, clearing_aborting_character_handler);
  if (XML_Parse(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (XML_ResumeParser(g_parser) == XML_STATUS_OK)
    fail("Resumed invalid parse not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_UNCLOSED_TOKEN)
    fail("Invalid parse not correctly faulted");
}
END_TEST

/* Test that re-suspended parses are correctly passed through */
START_TEST(test_resume_resuspended) {
  const char *text = "<doc>Hello<meep/>world</doc>";

  g_resumable = XML_TRUE;
  XML_SetCharacterDataHandler(g_parser, clearing_aborting_character_handler);
  if (XML_Parse(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  g_resumable = XML_TRUE;
  XML_SetCharacterDataHandler(g_parser, clearing_aborting_character_handler);
  if (XML_ResumeParser(g_parser) != XML_STATUS_SUSPENDED)
    fail("Resumption not suspended");
  /* This one should succeed and finish up */
  if (XML_ResumeParser(g_parser) != XML_STATUS_OK)
    xml_failure(g_parser);
}
END_TEST

/* Test that CDATA shows up correctly through a default handler */
START_TEST(test_cdata_default) {
  const char *text = "<doc><![CDATA[Hello\nworld]]></doc>";
  const XML_Char *expected = XCS("<doc><![CDATA[Hello\nworld]]></doc>");
  CharData storage;

  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetDefaultHandler(g_parser, accumulate_characters);

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test resetting a subordinate parser does exactly nothing */
START_TEST(test_subordinate_reset) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc>&entity;</doc>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_resetter);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test suspending a subordinate parser */
START_TEST(test_subordinate_suspend) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc>&entity;</doc>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_suspender);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test suspending a subordinate parser from an XML declaration */
/* Increases code coverage of the tests */

START_TEST(test_subordinate_xdecl_suspend) {
  const char *text
      = "<!DOCTYPE doc [\n"
        "  <!ENTITY entity SYSTEM 'http://example.org/dummy.ent'>\n"
        "]>\n"
        "<doc>&entity;</doc>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_suspend_xmldecl);
  g_resumable = XML_TRUE;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_subordinate_xdecl_abort) {
  const char *text
      = "<!DOCTYPE doc [\n"
        "  <!ENTITY entity SYSTEM 'http://example.org/dummy.ent'>\n"
        "]>\n"
        "<doc>&entity;</doc>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_suspend_xmldecl);
  g_resumable = XML_FALSE;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test external entity fault handling with suspension */
START_TEST(test_ext_entity_invalid_suspended_parse) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtFaults faults[]
      = {{"<?xml version='1.0' encoding='us-ascii'?><",
          "Incomplete element declaration not faulted", NULL,
          XML_ERROR_UNCLOSED_TOKEN},
         {/* First two bytes of a three-byte char */
          "<?xml version='1.0' encoding='utf-8'?>\xe2\x82",
          "Incomplete character not faulted", NULL, XML_ERROR_PARTIAL_CHAR},
         {NULL, NULL, NULL, XML_ERROR_NONE}};
  ExtFaults *fault;

  for (fault = &faults[0]; fault->parse_text != NULL; fault++) {
    set_subtest("%s", fault->parse_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser,
                                    external_entity_suspending_faulter);
    XML_SetUserData(g_parser, fault);
    expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                   "Parser did not report external entity error");
    XML_ParserReset(g_parser, NULL);
  }
}
END_TEST

/* Test setting an explicit encoding */
START_TEST(test_explicit_encoding) {
  const char *text1 = "<doc>Hello ";
  const char *text2 = " World</doc>";

  /* Just check that we can set the encoding to NULL before starting */
  if (XML_SetEncoding(g_parser, NULL) != XML_STATUS_OK)
    fail("Failed to initialise encoding to NULL");
  /* Say we are UTF-8 */
  if (XML_SetEncoding(g_parser, XCS("utf-8")) != XML_STATUS_OK)
    fail("Failed to set explicit encoding");
  if (_XML_Parse_SINGLE_BYTES(g_parser, text1, (int)strlen(text1), XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  /* Try to switch encodings mid-parse */
  if (XML_SetEncoding(g_parser, XCS("us-ascii")) != XML_STATUS_ERROR)
    fail("Allowed encoding change");
  if (_XML_Parse_SINGLE_BYTES(g_parser, text2, (int)strlen(text2), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  /* Try now the parse is over */
  if (XML_SetEncoding(g_parser, NULL) != XML_STATUS_OK)
    fail("Failed to unset encoding");
}
END_TEST

/* Test handling of trailing CR (rather than newline) */
START_TEST(test_trailing_cr) {
  const char *text = "<doc>\r";
  int found_cr;

  /* Try with a character handler, for code coverage */
  XML_SetCharacterDataHandler(g_parser, cr_cdata_handler);
  XML_SetUserData(g_parser, &found_cr);
  found_cr = 0;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_OK)
    fail("Failed to fault unclosed doc");
  if (found_cr == 0)
    fail("Did not catch the carriage return");
  XML_ParserReset(g_parser, NULL);

  /* Now with a default handler instead */
  XML_SetDefaultHandler(g_parser, cr_cdata_handler);
  XML_SetUserData(g_parser, &found_cr);
  found_cr = 0;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_OK)
    fail("Failed to fault unclosed doc");
  if (found_cr == 0)
    fail("Did not catch default carriage return");
}
END_TEST

/* Test trailing CR in an external entity parse */
START_TEST(test_ext_entity_trailing_cr) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  int found_cr;

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_cr_catcher);
  XML_SetUserData(g_parser, &found_cr);
  found_cr = 0;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_OK)
    xml_failure(g_parser);
  if (found_cr == 0)
    fail("No carriage return found");
  XML_ParserReset(g_parser, NULL);

  /* Try again with a different trailing CR */
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_bad_cr_catcher);
  XML_SetUserData(g_parser, &found_cr);
  found_cr = 0;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_OK)
    xml_failure(g_parser);
  if (found_cr == 0)
    fail("No carriage return found");
}
END_TEST

/* Test handling of trailing square bracket */
START_TEST(test_trailing_rsqb) {
  const char *text8 = "<doc>]";
  const char text16[] = "\xFF\xFE<\000d\000o\000c\000>\000]\000";
  int found_rsqb;
  int text8_len = (int)strlen(text8);

  XML_SetCharacterDataHandler(g_parser, rsqb_handler);
  XML_SetUserData(g_parser, &found_rsqb);
  found_rsqb = 0;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text8, text8_len, XML_TRUE)
      == XML_STATUS_OK)
    fail("Failed to fault unclosed doc");
  if (found_rsqb == 0)
    fail("Did not catch the right square bracket");

  /* Try again with a different encoding */
  XML_ParserReset(g_parser, NULL);
  XML_SetCharacterDataHandler(g_parser, rsqb_handler);
  XML_SetUserData(g_parser, &found_rsqb);
  found_rsqb = 0;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text16, (int)sizeof(text16) - 1,
                              XML_TRUE)
      == XML_STATUS_OK)
    fail("Failed to fault unclosed doc");
  if (found_rsqb == 0)
    fail("Did not catch the right square bracket");

  /* And finally with a default handler */
  XML_ParserReset(g_parser, NULL);
  XML_SetDefaultHandler(g_parser, rsqb_handler);
  XML_SetUserData(g_parser, &found_rsqb);
  found_rsqb = 0;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text16, (int)sizeof(text16) - 1,
                              XML_TRUE)
      == XML_STATUS_OK)
    fail("Failed to fault unclosed doc");
  if (found_rsqb == 0)
    fail("Did not catch the right square bracket");
}
END_TEST

/* Test trailing right square bracket in an external entity parse */
START_TEST(test_ext_entity_trailing_rsqb) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  int found_rsqb;

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_rsqb_catcher);
  XML_SetUserData(g_parser, &found_rsqb);
  found_rsqb = 0;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_OK)
    xml_failure(g_parser);
  if (found_rsqb == 0)
    fail("No right square bracket found");
}
END_TEST

/* Test CDATA handling in an external entity */
START_TEST(test_ext_entity_good_cdata) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_good_cdata_ascii);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_OK)
    xml_failure(g_parser);
}
END_TEST

/* Test user parameter settings */
START_TEST(test_user_parameters) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<!-- Primary parse -->\n"
                     "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc>&entity;";
  const char *epilog = "<!-- Back to primary parser -->\n"
                       "</doc>";

  g_comment_count = 0;
  g_skip_count = 0;
  g_xdecl_count = 0;
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetXmlDeclHandler(g_parser, xml_decl_handler);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_param_checker);
  XML_SetCommentHandler(g_parser, data_check_comment_handler);
  XML_SetSkippedEntityHandler(g_parser, param_check_skip_handler);
  XML_UseParserAsHandlerArg(g_parser);
  XML_SetUserData(g_parser, (void *)1);
  g_handler_data = g_parser;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  /* Ensure we can't change policy mid-parse */
  if (XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_NEVER))
    fail("Changed param entity parsing policy while parsing");
  if (_XML_Parse_SINGLE_BYTES(g_parser, epilog, (int)strlen(epilog), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (g_comment_count != 3)
    fail("Comment handler not invoked enough times");
  if (g_skip_count != 1)
    fail("Skip handler not invoked enough times");
  if (g_xdecl_count != 1)
    fail("XML declaration handler not invoked");
}
END_TEST

/* Test that an explicit external entity handler argument replaces
 * the parser as the first argument.
 *
 * We do not call the first parameter to the external entity handler
 * 'parser' for once, since the first time the handler is called it
 * will actually be a text string.  We need to be able to access the
 * global 'parser' variable to create our external entity parser from,
 * since there are code paths we need to ensure get executed.
 */
START_TEST(test_ext_entity_ref_parameter) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc>&entity;</doc>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_ref_param_checker);
  /* Set a handler arg that is not NULL and not parser (which is
   * what NULL would cause to be passed.
   */
  XML_SetExternalEntityRefHandlerArg(g_parser, (void *)text);
  g_handler_data = text;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);

  /* Now try again with unset args */
  XML_ParserReset(g_parser, NULL);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_ref_param_checker);
  XML_SetExternalEntityRefHandlerArg(g_parser, NULL);
  g_handler_data = g_parser;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test the parsing of an empty string */
START_TEST(test_empty_parse) {
  const char *text = "<doc></doc>";
  const char *partial = "<doc>";

  if (XML_Parse(g_parser, NULL, 0, XML_FALSE) == XML_STATUS_ERROR)
    fail("Parsing empty string faulted");
  if (XML_Parse(g_parser, NULL, 0, XML_TRUE) != XML_STATUS_ERROR)
    fail("Parsing final empty string not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_NO_ELEMENTS)
    fail("Parsing final empty string faulted for wrong reason");

  /* Now try with valid text before the empty end */
  XML_ParserReset(g_parser, NULL);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (XML_Parse(g_parser, NULL, 0, XML_TRUE) == XML_STATUS_ERROR)
    fail("Parsing final empty string faulted");

  /* Now try with invalid text before the empty end */
  XML_ParserReset(g_parser, NULL);
  if (_XML_Parse_SINGLE_BYTES(g_parser, partial, (int)strlen(partial),
                              XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (XML_Parse(g_parser, NULL, 0, XML_TRUE) != XML_STATUS_ERROR)
    fail("Parsing final incomplete empty string not faulted");
}
END_TEST

/* Test XML_Parse for len < 0 */
START_TEST(test_negative_len_parse) {
  const char *const doc = "<root/>";
  for (int isFinal = 0; isFinal < 2; isFinal++) {
    set_subtest("isFinal=%d", isFinal);

    XML_Parser parser = XML_ParserCreate(NULL);

    if (XML_GetErrorCode(parser) != XML_ERROR_NONE)
      fail("There was not supposed to be any initial parse error.");

    const enum XML_Status status = XML_Parse(parser, doc, -1, isFinal);

    if (status != XML_STATUS_ERROR)
      fail("Negative len was expected to fail the parse but did not.");

    if (XML_GetErrorCode(parser) != XML_ERROR_INVALID_ARGUMENT)
      fail("Parse error does not match XML_ERROR_INVALID_ARGUMENT.");

    XML_ParserFree(parser);
  }
}
END_TEST

/* Test XML_ParseBuffer for len < 0 */
START_TEST(test_negative_len_parse_buffer) {
  const char *const doc = "<root/>";
  for (int isFinal = 0; isFinal < 2; isFinal++) {
    set_subtest("isFinal=%d", isFinal);

    XML_Parser parser = XML_ParserCreate(NULL);

    if (XML_GetErrorCode(parser) != XML_ERROR_NONE)
      fail("There was not supposed to be any initial parse error.");

    void *const buffer = XML_GetBuffer(parser, (int)strlen(doc));

    if (buffer == NULL)
      fail("XML_GetBuffer failed.");

    memcpy(buffer, doc, strlen(doc));

    const enum XML_Status status = XML_ParseBuffer(parser, -1, isFinal);

    if (status != XML_STATUS_ERROR)
      fail("Negative len was expected to fail the parse but did not.");

    if (XML_GetErrorCode(parser) != XML_ERROR_INVALID_ARGUMENT)
      fail("Parse error does not match XML_ERROR_INVALID_ARGUMENT.");

    XML_ParserFree(parser);
  }
}
END_TEST

/* Test odd corners of the XML_GetBuffer interface */
static enum XML_Status
get_feature(enum XML_FeatureEnum feature_id, long *presult) {
  const XML_Feature *feature = XML_GetFeatureList();

  if (feature == NULL)
    return XML_STATUS_ERROR;
  for (; feature->feature != XML_FEATURE_END; feature++) {
    if (feature->feature == feature_id) {
      *presult = feature->value;
      return XML_STATUS_OK;
    }
  }
  return XML_STATUS_ERROR;
}

/* Test odd corners of the XML_GetBuffer interface */
START_TEST(test_get_buffer_1) {
  const char *text = get_buffer_test_text;
  void *buffer;
  long context_bytes;

  /* Attempt to allocate a negative length buffer */
  if (XML_GetBuffer(g_parser, -12) != NULL)
    fail("Negative length buffer not failed");

  /* Now get a small buffer and extend it past valid length */
  buffer = XML_GetBuffer(g_parser, 1536);
  if (buffer == NULL)
    fail("1.5K buffer failed");
  assert(buffer != NULL);
  memcpy(buffer, text, strlen(text));
  if (XML_ParseBuffer(g_parser, (int)strlen(text), XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (XML_GetBuffer(g_parser, INT_MAX) != NULL)
    fail("INT_MAX buffer not failed");

  /* Now try extending it a more reasonable but still too large
   * amount.  The allocator in XML_GetBuffer() doubles the buffer
   * size until it exceeds the requested amount or INT_MAX.  If it
   * exceeds INT_MAX, it rejects the request, so we want a request
   * between INT_MAX and INT_MAX/2.  A gap of 1K seems comfortable,
   * with an extra byte just to ensure that the request is off any
   * boundary.  The request will be inflated internally by
   * XML_CONTEXT_BYTES (if >=1), so we subtract that from our
   * request.
   */
  if (get_feature(XML_FEATURE_CONTEXT_BYTES, &context_bytes) != XML_STATUS_OK)
    context_bytes = 0;
  if (XML_GetBuffer(g_parser, INT_MAX - (context_bytes + 1025)) != NULL)
    fail("INT_MAX- buffer not failed");

  /* Now try extending it a carefully crafted amount */
  if (XML_GetBuffer(g_parser, 1000) == NULL)
    fail("1000 buffer failed");
}
END_TEST

/* Test more corners of the XML_GetBuffer interface */
START_TEST(test_get_buffer_2) {
  const char *text = get_buffer_test_text;
  void *buffer;

  /* Now get a decent buffer */
  buffer = XML_GetBuffer(g_parser, 1536);
  if (buffer == NULL)
    fail("1.5K buffer failed");
  assert(buffer != NULL);
  memcpy(buffer, text, strlen(text));
  if (XML_ParseBuffer(g_parser, (int)strlen(text), XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);

  /* Extend it, to catch a different code path */
  if (XML_GetBuffer(g_parser, 1024) == NULL)
    fail("1024 buffer failed");
}
END_TEST

/* Test for signed integer overflow CVE-2022-23852 */
#if XML_CONTEXT_BYTES > 0
START_TEST(test_get_buffer_3_overflow) {
  XML_Parser parser = XML_ParserCreate(NULL);
  assert(parser != NULL);

  const char *const text = "\n";
  const int expectedKeepValue = (int)strlen(text);

  // After this call, variable "keep" in XML_GetBuffer will
  // have value expectedKeepValue
  if (_XML_Parse_SINGLE_BYTES(parser, text, (int)strlen(text),
                              XML_FALSE /* isFinal */)
      == XML_STATUS_ERROR)
    xml_failure(parser);

  assert(expectedKeepValue > 0);
  if (XML_GetBuffer(parser, INT_MAX - expectedKeepValue + 1) != NULL)
    fail("enlarging buffer not failed");

  XML_ParserFree(parser);
}
END_TEST
#endif // XML_CONTEXT_BYTES > 0

START_TEST(test_buffer_can_grow_to_max) {
  const char *const prefixes[] = {
      "",
      "<",
      "<x a='",
      "<doc><x a='",
      "<document><x a='",
      "<averylongelementnamesuchthatitwillhopefullystretchacrossmultiplelinesand"
      "lookprettyridiculousitsalsoveryhardtoreadandifyouredoingitihavetowonderif"
      "youreallydonthaveanythingbettertodoofcourseiguessicouldveputsomethingbadin"
      "herebutipromisethatididntheybtwhowgreatarespacesandpunctuationforhelping"
      "withreadabilityprettygreatithinkanywaysthisisprobablylongenoughbye><x a='"};
  const int num_prefixes = sizeof(prefixes) / sizeof(prefixes[0]);
  int maxbuf = INT_MAX / 2 + (INT_MAX & 1); // round up without overflow
#if defined(__MINGW32__) && ! defined(__MINGW64__)
  // workaround for mingw/wine32 on GitHub CI not being able to reach 1GiB
  // Can we make a big allocation?
  void *big = malloc(maxbuf);
  if (! big) {
    // The big allocation failed. Let's be a little lenient.
    maxbuf = maxbuf / 2;
  }
  free(big);
#endif

  for (int i = 0; i < num_prefixes; ++i) {
    set_subtest("\"%s\"", prefixes[i]);
    XML_Parser parser = XML_ParserCreate(NULL);
#if XML_GE == 1
    assert_true(XML_SetAllocTrackerActivationThreshold(parser, (size_t)-1)
                == XML_TRUE); // i.e. deactivate
#endif
    const int prefix_len = (int)strlen(prefixes[i]);
    const enum XML_Status s
        = _XML_Parse_SINGLE_BYTES(parser, prefixes[i], prefix_len, XML_FALSE);
    if (s != XML_STATUS_OK)
      xml_failure(parser);

    // XML_CONTEXT_BYTES of the prefix may remain in the buffer;
    // subtracting the whole prefix is easiest, and close enough.
    assert_true(XML_GetBuffer(parser, maxbuf - prefix_len) != NULL);
    // The limit should be consistent; no prefix should allow us to
    // reach above the max buffer size.
    assert_true(XML_GetBuffer(parser, maxbuf + 1) == NULL);
    XML_ParserFree(parser);
  }
}
END_TEST

START_TEST(test_getbuffer_allocates_on_zero_len) {
  for (int first_len = 1; first_len >= 0; first_len--) {
    set_subtest("with len=%d first", first_len);
    XML_Parser parser = XML_ParserCreate(NULL);
    assert_true(parser != NULL);
    assert_true(XML_GetBuffer(parser, first_len) != NULL);
    assert_true(XML_GetBuffer(parser, 0) != NULL);
    if (XML_ParseBuffer(parser, 0, XML_FALSE) != XML_STATUS_OK)
      xml_failure(parser);
    XML_ParserFree(parser);
  }
}
END_TEST

/* Test position information macros */
START_TEST(test_byte_info_at_end) {
  const char *text = "<doc></doc>";

  if (XML_GetCurrentByteIndex(g_parser) != -1
      || XML_GetCurrentByteCount(g_parser) != 0)
    fail("Byte index/count incorrect at start of parse");
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  /* At end, the count will be zero and the index the end of string */
  if (XML_GetCurrentByteCount(g_parser) != 0)
    fail("Terminal byte count incorrect");
  if (XML_GetCurrentByteIndex(g_parser) != (XML_Index)strlen(text))
    fail("Terminal byte index incorrect");
}
END_TEST

/* Test position information from errors */
#define PRE_ERROR_STR "<doc></"
#define POST_ERROR_STR "wombat></doc>"
START_TEST(test_byte_info_at_error) {
  const char *text = PRE_ERROR_STR POST_ERROR_STR;

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_OK)
    fail("Syntax error not faulted");
  if (XML_GetCurrentByteCount(g_parser) != 0)
    fail("Error byte count incorrect");
  if (XML_GetCurrentByteIndex(g_parser) != strlen(PRE_ERROR_STR))
    fail("Error byte index incorrect");
}
END_TEST
#undef PRE_ERROR_STR
#undef POST_ERROR_STR

/* Test position information in handler */
#define START_ELEMENT "<e>"
#define CDATA_TEXT "Hello"
#define END_ELEMENT "</e>"
START_TEST(test_byte_info_at_cdata) {
  const char *text = START_ELEMENT CDATA_TEXT END_ELEMENT;
  int offset, size;
  ByteTestData data;

  /* Check initial context is empty */
  if (XML_GetInputContext(g_parser, &offset, &size) != NULL)
    fail("Unexpected context at start of parse");

  data.start_element_len = (int)strlen(START_ELEMENT);
  data.cdata_len = (int)strlen(CDATA_TEXT);
  data.total_string_len = (int)strlen(text);
  XML_SetCharacterDataHandler(g_parser, byte_character_handler);
  XML_SetUserData(g_parser, &data);
  if (XML_Parse(g_parser, text, (int)strlen(text), XML_TRUE) != XML_STATUS_OK)
    xml_failure(g_parser);
}
END_TEST
#undef START_ELEMENT
#undef CDATA_TEXT
#undef END_ELEMENT

/* Test predefined entities are correctly recognised */
START_TEST(test_predefined_entities) {
  const char *text = "<doc>&lt;&gt;&amp;&quot;&apos;</doc>";
  const XML_Char *expected = XCS("<doc>&lt;&gt;&amp;&quot;&apos;</doc>");
  const XML_Char *result = XCS("<>&\"'");
  CharData storage;

  XML_SetDefaultHandler(g_parser, accumulate_characters);
  /* run_character_check uses XML_SetCharacterDataHandler(), which
   * unfortunately heads off a code path that we need to exercise.
   */
  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  /* The default handler doesn't translate the entities */
  CharData_CheckXMLChars(&storage, expected);

  /* Now try again and check the translation */
  XML_ParserReset(g_parser, NULL);
  run_character_check(text, result);
}
END_TEST

/* Regression test that an invalid tag in an external parameter
 * reference in an external DTD is correctly faulted.
 *
 * Only a few specific tags are legal in DTDs ignoring comments and
 * processing instructions, all of which begin with an exclamation
 * mark.  "<el/>" is not one of them, so the parser should raise an
 * error on encountering it.
 */
START_TEST(test_invalid_tag_in_dtd) {
  const char *text = "<!DOCTYPE doc SYSTEM '004-1.ent'>\n"
                     "<doc></doc>\n";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_param);
  expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                 "Invalid tag IN DTD external param not rejected");
}
END_TEST

/* Test entities not quite the predefined ones are not mis-recognised */
START_TEST(test_not_predefined_entities) {
  const char *text[] = {"<doc>&pt;</doc>", "<doc>&amo;</doc>",
                        "<doc>&quid;</doc>", "<doc>&apod;</doc>", NULL};
  int i = 0;

  while (text[i] != NULL) {
    expect_failure(text[i], XML_ERROR_UNDEFINED_ENTITY,
                   "Undefined entity not rejected");
    XML_ParserReset(g_parser, NULL);
    i++;
  }
}
END_TEST

/* Test conditional inclusion (IGNORE) */
START_TEST(test_ignore_section) {
  const char *text = "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc><e>&entity;</e></doc>";
  const XML_Char *expected
      = XCS("<![IGNORE[<!ELEMENT e (#PCDATA)*>]]>\n&entity;");
  CharData storage;

  CharData_Init(&storage);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetUserData(g_parser, &storage);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_load_ignore);
  XML_SetDefaultHandler(g_parser, accumulate_characters);
  XML_SetStartDoctypeDeclHandler(g_parser, dummy_start_doctype_handler);
  XML_SetEndDoctypeDeclHandler(g_parser, dummy_end_doctype_handler);
  XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
  XML_SetStartElementHandler(g_parser, dummy_start_element);
  XML_SetEndElementHandler(g_parser, dummy_end_element);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_ignore_section_utf16) {
  const char text[] =
      /* <!DOCTYPE d SYSTEM 's'> */
      "<\0!\0D\0O\0C\0T\0Y\0P\0E\0 \0d\0 "
      "\0S\0Y\0S\0T\0E\0M\0 \0'\0s\0'\0>\0\n\0"
      /* <d><e>&en;</e></d> */
      "<\0d\0>\0<\0e\0>\0&\0e\0n\0;\0<\0/\0e\0>\0<\0/\0d\0>\0";
  const XML_Char *expected = XCS("<![IGNORE[<!ELEMENT e (#PCDATA)*>]]>\n&en;");
  CharData storage;

  CharData_Init(&storage);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetUserData(g_parser, &storage);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_load_ignore_utf16);
  XML_SetDefaultHandler(g_parser, accumulate_characters);
  XML_SetStartDoctypeDeclHandler(g_parser, dummy_start_doctype_handler);
  XML_SetEndDoctypeDeclHandler(g_parser, dummy_end_doctype_handler);
  XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
  XML_SetStartElementHandler(g_parser, dummy_start_element);
  XML_SetEndElementHandler(g_parser, dummy_end_element);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_ignore_section_utf16_be) {
  const char text[] =
      /* <!DOCTYPE d SYSTEM 's'> */
      "\0<\0!\0D\0O\0C\0T\0Y\0P\0E\0 \0d\0 "
      "\0S\0Y\0S\0T\0E\0M\0 \0'\0s\0'\0>\0\n"
      /* <d><e>&en;</e></d> */
      "\0<\0d\0>\0<\0e\0>\0&\0e\0n\0;\0<\0/\0e\0>\0<\0/\0d\0>";
  const XML_Char *expected = XCS("<![IGNORE[<!ELEMENT e (#PCDATA)*>]]>\n&en;");
  CharData storage;

  CharData_Init(&storage);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetUserData(g_parser, &storage);
  XML_SetExternalEntityRefHandler(g_parser,
                                  external_entity_load_ignore_utf16_be);
  XML_SetDefaultHandler(g_parser, accumulate_characters);
  XML_SetStartDoctypeDeclHandler(g_parser, dummy_start_doctype_handler);
  XML_SetEndDoctypeDeclHandler(g_parser, dummy_end_doctype_handler);
  XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
  XML_SetStartElementHandler(g_parser, dummy_start_element);
  XML_SetEndElementHandler(g_parser, dummy_end_element);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test mis-formatted conditional exclusion */
START_TEST(test_bad_ignore_section) {
  const char *text = "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc><e>&entity;</e></doc>";
  ExtFaults faults[]
      = {{"<![IGNORE[<!ELEM", "Broken-off declaration not faulted", NULL,
          XML_ERROR_SYNTAX},
         {"<![IGNORE[\x01]]>", "Invalid XML character not faulted", NULL,
          XML_ERROR_INVALID_TOKEN},
         {/* FIrst two bytes of a three-byte char */
          "<![IGNORE[\xe2\x82", "Partial XML character not faulted", NULL,
          XML_ERROR_PARTIAL_CHAR},
         {NULL, NULL, NULL, XML_ERROR_NONE}};
  ExtFaults *fault;

  for (fault = &faults[0]; fault->parse_text != NULL; fault++) {
    set_subtest("%s", fault->parse_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_faulter);
    XML_SetUserData(g_parser, fault);
    expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                   "Incomplete IGNORE section not failed");
    XML_ParserReset(g_parser, NULL);
  }
}
END_TEST

struct bom_testdata {
  const char *external;
  int split;
  XML_Bool nested_callback_happened;
};

static int XMLCALL
external_bom_checker(XML_Parser parser, const XML_Char *context,
                     const XML_Char *base, const XML_Char *systemId,
                     const XML_Char *publicId) {
  const char *text;
  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);

  XML_Parser ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");

  if (! xcstrcmp(systemId, XCS("004-2.ent"))) {
    struct bom_testdata *const testdata
        = (struct bom_testdata *)XML_GetUserData(parser);
    const char *const external = testdata->external;
    const int split = testdata->split;
    testdata->nested_callback_happened = XML_TRUE;

    if (_XML_Parse_SINGLE_BYTES(ext_parser, external, split, XML_FALSE)
        != XML_STATUS_OK) {
      xml_failure(ext_parser);
    }
    text = external + split; // the parse below will continue where we left off.
  } else if (! xcstrcmp(systemId, XCS("004-1.ent"))) {
    text = "<!ELEMENT doc EMPTY>\n"
           "<!ENTITY % e1 SYSTEM '004-2.ent'>\n"
           "<!ENTITY % e2 '%e1;'>\n";
  } else {
    fail("unknown systemId");
  }

  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_OK)
    xml_failure(ext_parser);

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

/* regression test: BOM should be consumed when followed by a partial token. */
START_TEST(test_external_bom_consumed) {
  const char *const text = "<!DOCTYPE doc SYSTEM '004-1.ent'>\n"
                           "<doc></doc>\n";
  const char *const external = "\xEF\xBB\xBF<!ATTLIST doc a1 CDATA 'value'>";
  const int len = (int)strlen(external);
  for (int split = 0; split <= len; ++split) {
    set_subtest("split at byte %d", split);

    struct bom_testdata testdata;
    testdata.external = external;
    testdata.split = split;
    testdata.nested_callback_happened = XML_FALSE;

    XML_Parser parser = XML_ParserCreate(NULL);
    if (parser == NULL) {
      fail("Couldn't create parser");
    }
    XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(parser, external_bom_checker);
    XML_SetUserData(parser, &testdata);
    if (_XML_Parse_SINGLE_BYTES(parser, text, (int)strlen(text), XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(parser);
    if (! testdata.nested_callback_happened) {
      fail("ref handler not called");
    }
    XML_ParserFree(parser);
  }
}
END_TEST

/* Test recursive parsing */
START_TEST(test_external_entity_values) {
  const char *text = "<!DOCTYPE doc SYSTEM '004-1.ent'>\n"
                     "<doc></doc>\n";
  ExtFaults data_004_2[] = {
      {"<!ATTLIST doc a1 CDATA 'value'>", NULL, NULL, XML_ERROR_NONE},
      {"<!ATTLIST $doc a1 CDATA 'value'>", "Invalid token not faulted", NULL,
       XML_ERROR_INVALID_TOKEN},
      {"'wombat", "Unterminated string not faulted", NULL,
       XML_ERROR_UNCLOSED_TOKEN},
      {"\xe2\x82", "Partial UTF-8 character not faulted", NULL,
       XML_ERROR_PARTIAL_CHAR},
      {"<?xml version='1.0' encoding='utf-8'?>\n", NULL, NULL, XML_ERROR_NONE},
      {"<?xml?>", "Malformed XML declaration not faulted", NULL,
       XML_ERROR_XML_DECL},
      {/* UTF-8 BOM */
       "\xEF\xBB\xBF<!ATTLIST doc a1 CDATA 'value'>", NULL, NULL,
       XML_ERROR_NONE},
      {"<?xml version='1.0' encoding='utf-8'?>\n$",
       "Invalid token after text declaration not faulted", NULL,
       XML_ERROR_INVALID_TOKEN},
      {"<?xml version='1.0' encoding='utf-8'?>\n'wombat",
       "Unterminated string after text decl not faulted", NULL,
       XML_ERROR_UNCLOSED_TOKEN},
      {"<?xml version='1.0' encoding='utf-8'?>\n\xe2\x82",
       "Partial UTF-8 character after text decl not faulted", NULL,
       XML_ERROR_PARTIAL_CHAR},
      {"%e1;", "Recursive parameter entity not faulted", NULL,
       XML_ERROR_RECURSIVE_ENTITY_REF},
      {NULL, NULL, NULL, XML_ERROR_NONE}};
  int i;

  for (i = 0; data_004_2[i].parse_text != NULL; i++) {
    set_subtest("%s", data_004_2[i].parse_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_valuer);
    XML_SetUserData(g_parser, &data_004_2[i]);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(g_parser);
    XML_ParserReset(g_parser, NULL);
  }
}
END_TEST

/* Test the recursive parse interacts with a not standalone handler */
START_TEST(test_ext_entity_not_standalone) {
  const char *text = "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc></doc>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_not_standalone);
  expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                 "Standalone rejection not caught");
}
END_TEST

START_TEST(test_ext_entity_value_abort) {
  const char *text = "<!DOCTYPE doc SYSTEM '004-1.ent'>\n"
                     "<doc></doc>\n";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_value_aborter);
  g_resumable = XML_FALSE;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_bad_public_doctype) {
  const char *text = "<?xml version='1.0' encoding='utf-8'?>\n"
                     "<!DOCTYPE doc PUBLIC '{BadName}' 'test'>\n"
                     "<doc></doc>";

  /* Setting a handler provokes a particular code path */
  XML_SetDoctypeDeclHandler(g_parser, dummy_start_doctype_handler,
                            dummy_end_doctype_handler);
  expect_failure(text, XML_ERROR_PUBLICID, "Bad Public ID not failed");
}
END_TEST

/* Test based on ibm/valid/P32/ibm32v04.xml */
START_TEST(test_attribute_enum_value) {
  const char *text = "<?xml version='1.0' standalone='no'?>\n"
                     "<!DOCTYPE animal SYSTEM 'test.dtd'>\n"
                     "<animal>This is a \n    <a/>  \n\nyellow tiger</animal>";
  ExtTest dtd_data
      = {"<!ELEMENT animal (#PCDATA|a)*>\n"
         "<!ELEMENT a EMPTY>\n"
         "<!ATTLIST animal xml:space (default|preserve) 'preserve'>",
         NULL, NULL};
  const XML_Char *expected = XCS("This is a \n      \n\nyellow tiger");

  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  XML_SetUserData(g_parser, &dtd_data);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  /* An attribute list handler provokes a different code path */
  XML_SetAttlistDeclHandler(g_parser, dummy_attlist_decl_handler);
  run_ext_character_check(text, &dtd_data, expected);
}
END_TEST

/* Slightly bizarrely, the library seems to silently ignore entity
 * definitions for predefined entities, even when they are wrong.  The
 * language of the XML 1.0 spec is somewhat unhelpful as to what ought
 * to happen, so this is currently treated as acceptable.
 */
START_TEST(test_predefined_entity_redefinition) {
  const char *text = "<!DOCTYPE doc [\n"
                     "<!ENTITY apos 'foo'>\n"
                     "]>\n"
                     "<doc>&apos;</doc>";
  run_character_check(text, XCS("'"));
}
END_TEST

/* Test that the parser stops processing the DTD after an unresolved
 * parameter entity is encountered.
 */
START_TEST(test_dtd_stop_processing) {
  const char *text = "<!DOCTYPE doc [\n"
                     "%foo;\n"
                     "<!ENTITY bar 'bas'>\n"
                     "]><doc/>";

  XML_SetEntityDeclHandler(g_parser, dummy_entity_decl_handler);
  init_dummy_handlers();
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (get_dummy_handler_flags() != 0)
    fail("DTD processing still going after undefined PE");
}
END_TEST

/* Test public notations with no system ID */
START_TEST(test_public_notation_no_sysid) {
  const char *text = "<!DOCTYPE doc [\n"
                     "<!NOTATION note PUBLIC 'foo'>\n"
                     "<!ELEMENT doc EMPTY>\n"
                     "]>\n<doc/>";

  init_dummy_handlers();
  XML_SetNotationDeclHandler(g_parser, dummy_notation_decl_handler);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (get_dummy_handler_flags() != DUMMY_NOTATION_DECL_HANDLER_FLAG)
    fail("Notation declaration handler not called");
}
END_TEST

START_TEST(test_nested_groups) {
  const char *text
      = "<!DOCTYPE doc [\n"
        "<!ELEMENT doc "
        /* Sixteen elements per line */
        "(e,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,"
        "(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?,(e?"
        "))))))))))))))))))))))))))))))))>\n"
        "<!ELEMENT e EMPTY>"
        "]>\n"
        "<doc><e/></doc>";
  CharData storage;

  CharData_Init(&storage);
  XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
  XML_SetStartElementHandler(g_parser, record_element_start_handler);
  XML_SetUserData(g_parser, &storage);
  init_dummy_handlers();
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, XCS("doce"));
  if (get_dummy_handler_flags() != DUMMY_ELEMENT_DECL_HANDLER_FLAG)
    fail("Element handler not fired");
}
END_TEST

START_TEST(test_group_choice) {
  const char *text = "<!DOCTYPE doc [\n"
                     "<!ELEMENT doc (a|b|c)+>\n"
                     "<!ELEMENT a EMPTY>\n"
                     "<!ELEMENT b (#PCDATA)>\n"
                     "<!ELEMENT c ANY>\n"
                     "]>\n"
                     "<doc>\n"
                     "<a/>\n"
                     "<b attr='foo'>This is a foo</b>\n"
                     "<c></c>\n"
                     "</doc>\n";

  XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
  init_dummy_handlers();
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (get_dummy_handler_flags() != DUMMY_ELEMENT_DECL_HANDLER_FLAG)
    fail("Element handler flag not raised");
}
END_TEST

START_TEST(test_standalone_parameter_entity) {
  const char *text = "<?xml version='1.0' standalone='yes'?>\n"
                     "<!DOCTYPE doc SYSTEM 'http://example.org/' [\n"
                     "<!ENTITY % entity '<!ELEMENT doc (#PCDATA)>'>\n"
                     "%entity;\n"
                     "]>\n"
                     "<doc></doc>";
  char dtd_data[] = "<!ENTITY % e1 'foo'>\n";

  XML_SetUserData(g_parser, dtd_data);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_public);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test skipping of parameter entity in an external DTD */
/* Derived from ibm/invalid/P69/ibm69i01.xml */
START_TEST(test_skipped_parameter_entity) {
  const char *text = "<?xml version='1.0'?>\n"
                     "<!DOCTYPE root SYSTEM 'http://example.org/dtd.ent' [\n"
                     "<!ELEMENT root (#PCDATA|a)* >\n"
                     "]>\n"
                     "<root></root>";
  ExtTest dtd_data = {"%pe2;", NULL, NULL};

  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  XML_SetUserData(g_parser, &dtd_data);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetSkippedEntityHandler(g_parser, dummy_skip_handler);
  init_dummy_handlers();
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (get_dummy_handler_flags() != DUMMY_SKIP_HANDLER_FLAG)
    fail("Skip handler not executed");
}
END_TEST

/* Test recursive parameter entity definition rejected in external DTD */
START_TEST(test_recursive_external_parameter_entity) {
  const char *text = "<?xml version='1.0'?>\n"
                     "<!DOCTYPE root SYSTEM 'http://example.org/dtd.ent' [\n"
                     "<!ELEMENT root (#PCDATA|a)* >\n"
                     "]>\n"
                     "<root></root>";
  ExtFaults dtd_data = {"<!ENTITY % pe2 '&#37;pe2;'>\n%pe2;",
                        "Recursive external parameter entity not faulted", NULL,
                        XML_ERROR_RECURSIVE_ENTITY_REF};

  XML_SetExternalEntityRefHandler(g_parser, external_entity_faulter);
  XML_SetUserData(g_parser, &dtd_data);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                 "Recursive external parameter not spotted");
}
END_TEST

/* Test undefined parameter entity in external entity handler */
START_TEST(test_undefined_ext_entity_in_external_dtd) {
  const char *text = "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc></doc>\n";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_devaluer);
  XML_SetUserData(g_parser, NULL);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);

  /* Now repeat without the external entity ref handler invoking
   * another copy of itself.
   */
  XML_ParserReset(g_parser, NULL);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_devaluer);
  XML_SetUserData(g_parser, g_parser); /* Any non-NULL value will do */
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test suspending the parse on receiving an XML declaration works */
START_TEST(test_suspend_xdecl) {
  const char *text = long_character_data_text;

  XML_SetXmlDeclHandler(g_parser, entity_suspending_xdecl_handler);
  XML_SetUserData(g_parser, g_parser);
  g_resumable = XML_TRUE;
  // can't use SINGLE_BYTES here, because it'll return early on suspension, and
  // we won't know exactly how much input we actually managed to give Expat.
  if (XML_Parse(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_SUSPENDED)
    xml_failure(g_parser);
  if (XML_GetErrorCode(g_parser) != XML_ERROR_NONE)
    xml_failure(g_parser);
  /* Attempt to start a new parse while suspended */
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Attempt to parse while suspended not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_SUSPENDED)
    fail("Suspended parse not faulted with correct error");
}
END_TEST

/* Test aborting the parse in an epilog works */
START_TEST(test_abort_epilog) {
  const char *text = "<doc></doc>\n\r\n";
  XML_Char trigger_char = XCS('\r');

  XML_SetDefaultHandler(g_parser, selective_aborting_default_handler);
  XML_SetUserData(g_parser, &trigger_char);
  g_resumable = XML_FALSE;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Abort not triggered");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_ABORTED)
    xml_failure(g_parser);
}
END_TEST

/* Test a different code path for abort in the epilog */
START_TEST(test_abort_epilog_2) {
  const char *text = "<doc></doc>\n";
  XML_Char trigger_char = XCS('\n');

  XML_SetDefaultHandler(g_parser, selective_aborting_default_handler);
  XML_SetUserData(g_parser, &trigger_char);
  g_resumable = XML_FALSE;
  expect_failure(text, XML_ERROR_ABORTED, "Abort not triggered");
}
END_TEST

/* Test suspension from the epilog */
START_TEST(test_suspend_epilog) {
  const char *text = "<doc></doc>\n";
  XML_Char trigger_char = XCS('\n');

  XML_SetDefaultHandler(g_parser, selective_aborting_default_handler);
  XML_SetUserData(g_parser, &trigger_char);
  g_resumable = XML_TRUE;
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_SUSPENDED)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_suspend_in_sole_empty_tag) {
  const char *text = "<doc/>";
  enum XML_Status rc;

  XML_SetEndElementHandler(g_parser, suspending_end_handler);
  XML_SetUserData(g_parser, g_parser);
  rc = _XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE);
  if (rc == XML_STATUS_ERROR)
    xml_failure(g_parser);
  else if (rc != XML_STATUS_SUSPENDED)
    fail("Suspend not triggered");
  rc = XML_ResumeParser(g_parser);
  if (rc == XML_STATUS_ERROR)
    xml_failure(g_parser);
  else if (rc != XML_STATUS_OK)
    fail("Resume failed");
}
END_TEST

START_TEST(test_unfinished_epilog) {
  const char *text = "<doc></doc><";

  expect_failure(text, XML_ERROR_UNCLOSED_TOKEN,
                 "Incomplete epilog entry not faulted");
}
END_TEST

START_TEST(test_partial_char_in_epilog) {
  const char *text = "<doc></doc>\xe2\x82";

  /* First check that no fault is raised if the parse is not finished */
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  /* Now check that it is faulted once we finish */
  if (XML_ParseBuffer(g_parser, 0, XML_TRUE) != XML_STATUS_ERROR)
    fail("Partial character in epilog not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_PARTIAL_CHAR)
    xml_failure(g_parser);
}
END_TEST

/* Test resuming a parse suspended in entity substitution */
START_TEST(test_suspend_resume_internal_entity) {
  const char *text
      = "<!DOCTYPE doc [\n"
        "<!ENTITY foo '<suspend>Hi<suspend>Ho</suspend></suspend>'>\n"
        "]>\n"
        "<doc>&foo;</doc>\n";
  const XML_Char *expected1 = XCS("Hi");
  const XML_Char *expected2 = XCS("HiHo");
  CharData storage;

  CharData_Init(&storage);
  XML_SetStartElementHandler(g_parser, start_element_suspender);
  XML_SetCharacterDataHandler(g_parser, accumulate_characters);
  XML_SetUserData(g_parser, &storage);
  // can't use SINGLE_BYTES here, because it'll return early on suspension, and
  // we won't know exactly how much input we actually managed to give Expat.
  if (XML_Parse(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_SUSPENDED)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, XCS(""));
  if (XML_ResumeParser(g_parser) != XML_STATUS_SUSPENDED)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected1);
  if (XML_ResumeParser(g_parser) != XML_STATUS_OK)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected2);
}
END_TEST

START_TEST(test_suspend_resume_internal_entity_issue_629) {
  const char *const text
      = "<!DOCTYPE a [<!ENTITY e '<!--COMMENT-->a'>]><a>&e;<b>\n"
        "<"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "/>"
        "</b></a>";
  const size_t firstChunkSizeBytes = 54;

  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, parser);
  XML_SetCommentHandler(parser, suspending_comment_handler);

  if (XML_Parse(parser, text, (int)firstChunkSizeBytes, XML_FALSE)
      != XML_STATUS_SUSPENDED)
    xml_failure(parser);
  if (XML_ResumeParser(parser) != XML_STATUS_OK)
    xml_failure(parser);
  if (_XML_Parse_SINGLE_BYTES(parser, text + firstChunkSizeBytes,
                              (int)(strlen(text) - firstChunkSizeBytes),
                              XML_TRUE)
      != XML_STATUS_OK)
    xml_failure(parser);
  XML_ParserFree(parser);
}
END_TEST

/* Test syntax error is caught at parse resumption */
START_TEST(test_resume_entity_with_syntax_error) {
  if (g_chunkSize != 0) {
    // this test does not use SINGLE_BYTES, because of suspension
    return;
  }

  const char *text = "<!DOCTYPE doc [\n"
                     "<!ENTITY foo '<suspend>Hi</wombat>'>\n"
                     "]>\n"
                     "<doc>&foo;</doc>\n";

  XML_SetStartElementHandler(g_parser, start_element_suspender);
  // can't use SINGLE_BYTES here, because it'll return early on suspension, and
  // we won't know exactly how much input we actually managed to give Expat.
  if (XML_Parse(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_SUSPENDED)
    xml_failure(g_parser);
  if (XML_ResumeParser(g_parser) != XML_STATUS_ERROR)
    fail("Syntax error in entity not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_TAG_MISMATCH)
    xml_failure(g_parser);
}
END_TEST

/* Test suspending and resuming in a parameter entity substitution */
START_TEST(test_suspend_resume_parameter_entity) {
  const char *text = "<!DOCTYPE doc [\n"
                     "<!ENTITY % foo '<!ELEMENT doc (#PCDATA)*>'>\n"
                     "%foo;\n"
                     "]>\n"
                     "<doc>Hello, world</doc>";
  const XML_Char *expected = XCS("Hello, world");
  CharData storage;

  CharData_Init(&storage);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetElementDeclHandler(g_parser, element_decl_suspender);
  XML_SetCharacterDataHandler(g_parser, accumulate_characters);
  XML_SetUserData(g_parser, &storage);
  if (XML_Parse(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_SUSPENDED)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, XCS(""));
  if (XML_ResumeParser(g_parser) != XML_STATUS_OK)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test attempting to use parser after an error is faulted */
START_TEST(test_restart_on_error) {
  const char *text = "<$doc><doc></doc>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Invalid tag name not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_INVALID_TOKEN)
    xml_failure(g_parser);
  if (XML_Parse(g_parser, NULL, 0, XML_TRUE) != XML_STATUS_ERROR)
    fail("Restarting invalid parse not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_INVALID_TOKEN)
    xml_failure(g_parser);
}
END_TEST

/* Test that angle brackets in an attribute default value are faulted */
START_TEST(test_reject_lt_in_attribute_value) {
  const char *text = "<!DOCTYPE doc [<!ATTLIST doc a CDATA '<bar>'>]>\n"
                     "<doc></doc>";

  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Bad attribute default not faulted");
}
END_TEST

START_TEST(test_reject_unfinished_param_in_att_value) {
  const char *text = "<!DOCTYPE doc [<!ATTLIST doc a CDATA '&foo'>]>\n"
                     "<doc></doc>";

  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Bad attribute default not faulted");
}
END_TEST

START_TEST(test_trailing_cr_in_att_value) {
  const char *text = "<doc a='value\r'/>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Try parsing a general entity within a parameter entity in a
 * standalone internal DTD.  Covers a corner case in the parser.
 */
START_TEST(test_standalone_internal_entity) {
  const char *text = "<?xml version='1.0' standalone='yes' ?>\n"
                     "<!DOCTYPE doc [\n"
                     "  <!ELEMENT doc (#PCDATA)>\n"
                     "  <!ENTITY % pe '<!ATTLIST doc att2 CDATA \"&ge;\">'>\n"
                     "  <!ENTITY ge 'AttDefaultValue'>\n"
                     "  %pe;\n"
                     "]>\n"
                     "<doc att2='any'/>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test that a reference to an unknown external entity is skipped */
START_TEST(test_skipped_external_entity) {
  const char *text = "<!DOCTYPE doc SYSTEM 'http://example.org/'>\n"
                     "<doc></doc>\n";
  ExtTest test_data = {"<!ELEMENT doc EMPTY>\n"
                       "<!ENTITY % e2 '%e1;'>\n",
                       NULL, NULL};

  XML_SetUserData(g_parser, &test_data);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test a different form of unknown external entity */
START_TEST(test_skipped_null_loaded_ext_entity) {
  const char *text = "<!DOCTYPE doc SYSTEM 'http://example.org/one.ent'>\n"
                     "<doc />";
  ExtHdlrData test_data
      = {"<!ENTITY % pe1 SYSTEM 'http://example.org/two.ent'>\n"
         "<!ENTITY % pe2 '%pe1;'>\n"
         "%pe2;\n",
         external_entity_null_loader, NULL};

  XML_SetUserData(g_parser, &test_data);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_oneshot_loader);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_skipped_unloaded_ext_entity) {
  const char *text = "<!DOCTYPE doc SYSTEM 'http://example.org/one.ent'>\n"
                     "<doc />";
  ExtHdlrData test_data
      = {"<!ENTITY % pe1 SYSTEM 'http://example.org/two.ent'>\n"
         "<!ENTITY % pe2 '%pe1;'>\n"
         "%pe2;\n",
         NULL, NULL};

  XML_SetUserData(g_parser, &test_data);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_oneshot_loader);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test that a parameter entity value ending with a carriage return
 * has it translated internally into a newline.
 */
START_TEST(test_param_entity_with_trailing_cr) {
#define PARAM_ENTITY_NAME "pe"
#define PARAM_ENTITY_CORE_VALUE "<!ATTLIST doc att CDATA \"default\">"
  const char *text = "<!DOCTYPE doc SYSTEM 'http://example.org/'>\n"
                     "<doc/>";
  ExtTest test_data
      = {"<!ENTITY % " PARAM_ENTITY_NAME " '" PARAM_ENTITY_CORE_VALUE "\r'>\n"
         "%" PARAM_ENTITY_NAME ";\n",
         NULL, NULL};

  XML_SetUserData(g_parser, &test_data);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader);
  XML_SetEntityDeclHandler(g_parser, param_entity_match_handler);
  param_entity_match_init(XCS(PARAM_ENTITY_NAME),
                          XCS(PARAM_ENTITY_CORE_VALUE) XCS("\n"));
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  int entity_match_flag = get_param_entity_match_flag();
  if (entity_match_flag == ENTITY_MATCH_FAIL)
    fail("Parameter entity CR->NEWLINE conversion failed");
  else if (entity_match_flag == ENTITY_MATCH_NOT_FOUND)
    fail("Parameter entity not parsed");
}
#undef PARAM_ENTITY_NAME
#undef PARAM_ENTITY_CORE_VALUE
END_TEST

START_TEST(test_invalid_character_entity) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY entity '&#x110000;'>\n"
                     "]>\n"
                     "<doc>&entity;</doc>";

  expect_failure(text, XML_ERROR_BAD_CHAR_REF,
                 "Out of range character reference not faulted");
}
END_TEST

START_TEST(test_invalid_character_entity_2) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY entity '&#xg0;'>\n"
                     "]>\n"
                     "<doc>&entity;</doc>";

  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Out of range character reference not faulted");
}
END_TEST

START_TEST(test_invalid_character_entity_3) {
  const char text[] =
      /* <!DOCTYPE doc [\n */
      "\0<\0!\0D\0O\0C\0T\0Y\0P\0E\0 \0d\0o\0c\0 \0[\0\n"
      /* U+0E04 = KHO KHWAI
       * U+0E08 = CHO CHAN */
      /* <!ENTITY entity '&\u0e04\u0e08;'>\n */
      "\0<\0!\0E\0N\0T\0I\0T\0Y\0 \0e\0n\0t\0i\0t\0y\0 "
      "\0'\0&\x0e\x04\x0e\x08\0;\0'\0>\0\n"
      /* ]>\n */
      "\0]\0>\0\n"
      /* <doc>&entity;</doc> */
      "\0<\0d\0o\0c\0>\0&\0e\0n\0t\0i\0t\0y\0;\0<\0/\0d\0o\0c\0>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Invalid start of entity name not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_UNDEFINED_ENTITY)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_invalid_character_entity_4) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY entity '&#1114112;'>\n" /* = &#x110000 */
                     "]>\n"
                     "<doc>&entity;</doc>";

  expect_failure(text, XML_ERROR_BAD_CHAR_REF,
                 "Out of range character reference not faulted");
}
END_TEST

/* Test that processing instructions are picked up by a default handler */
START_TEST(test_pi_handled_in_default) {
  const char *text = "<?test processing instruction?>\n<doc/>";
  const XML_Char *expected = XCS("<?test processing instruction?>\n<doc/>");
  CharData storage;

  CharData_Init(&storage);
  XML_SetDefaultHandler(g_parser, accumulate_characters);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test that comments are picked up by a default handler */
START_TEST(test_comment_handled_in_default) {
  const char *text = "<!-- This is a comment -->\n<doc/>";
  const XML_Char *expected = XCS("<!-- This is a comment -->\n<doc/>");
  CharData storage;

  CharData_Init(&storage);
  XML_SetDefaultHandler(g_parser, accumulate_characters);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test PIs that look almost but not quite like XML declarations */
START_TEST(test_pi_yml) {
  const char *text = "<?yml something like data?><doc/>";
  const XML_Char *expected = XCS("yml: something like data\n");
  CharData storage;

  CharData_Init(&storage);
  XML_SetProcessingInstructionHandler(g_parser, accumulate_pi_characters);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_pi_xnl) {
  const char *text = "<?xnl nothing like data?><doc/>";
  const XML_Char *expected = XCS("xnl: nothing like data\n");
  CharData storage;

  CharData_Init(&storage);
  XML_SetProcessingInstructionHandler(g_parser, accumulate_pi_characters);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_pi_xmm) {
  const char *text = "<?xmm everything like data?><doc/>";
  const XML_Char *expected = XCS("xmm: everything like data\n");
  CharData storage;

  CharData_Init(&storage);
  XML_SetProcessingInstructionHandler(g_parser, accumulate_pi_characters);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_utf16_pi) {
  const char text[] =
      /* <?{KHO KHWAI}{CHO CHAN}?>
       * where {KHO KHWAI} = U+0E04
       * and   {CHO CHAN}  = U+0E08
       */
      "<\0?\0\x04\x0e\x08\x0e?\0>\0"
      /* <q/> */
      "<\0q\0/\0>\0";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x0e04\x0e08: \n");
#else
  const XML_Char *expected = XCS("\xe0\xb8\x84\xe0\xb8\x88: \n");
#endif
  CharData storage;

  CharData_Init(&storage);
  XML_SetProcessingInstructionHandler(g_parser, accumulate_pi_characters);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_utf16_be_pi) {
  const char text[] =
      /* <?{KHO KHWAI}{CHO CHAN}?>
       * where {KHO KHWAI} = U+0E04
       * and   {CHO CHAN}  = U+0E08
       */
      "\0<\0?\x0e\x04\x0e\x08\0?\0>"
      /* <q/> */
      "\0<\0q\0/\0>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x0e04\x0e08: \n");
#else
  const XML_Char *expected = XCS("\xe0\xb8\x84\xe0\xb8\x88: \n");
#endif
  CharData storage;

  CharData_Init(&storage);
  XML_SetProcessingInstructionHandler(g_parser, accumulate_pi_characters);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test that comments can be picked up and translated */
START_TEST(test_utf16_be_comment) {
  const char text[] =
      /* <!-- Comment A --> */
      "\0<\0!\0-\0-\0 \0C\0o\0m\0m\0e\0n\0t\0 \0A\0 \0-\0-\0>\0\n"
      /* <doc/> */
      "\0<\0d\0o\0c\0/\0>";
  const XML_Char *expected = XCS(" Comment A ");
  CharData storage;

  CharData_Init(&storage);
  XML_SetCommentHandler(g_parser, accumulate_comment);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_utf16_le_comment) {
  const char text[] =
      /* <!-- Comment B --> */
      "<\0!\0-\0-\0 \0C\0o\0m\0m\0e\0n\0t\0 \0B\0 \0-\0-\0>\0\n\0"
      /* <doc/> */
      "<\0d\0o\0c\0/\0>\0";
  const XML_Char *expected = XCS(" Comment B ");
  CharData storage;

  CharData_Init(&storage);
  XML_SetCommentHandler(g_parser, accumulate_comment);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test that the unknown encoding handler with map entries that expect
 * conversion but no conversion function is faulted
 */
START_TEST(test_missing_encoding_conversion_fn) {
  const char *text = "<?xml version='1.0' encoding='no-conv'?>\n"
                     "<doc>\x81</doc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  /* MiscEncodingHandler sets up an encoding with every top-bit-set
   * character introducing a two-byte sequence.  For this, it
   * requires a convert function.  The above function call doesn't
   * pass one through, so when BadEncodingHandler actually gets
   * called it should supply an invalid encoding.
   */
  expect_failure(text, XML_ERROR_UNKNOWN_ENCODING,
                 "Encoding with missing convert() not faulted");
}
END_TEST

START_TEST(test_failing_encoding_conversion_fn) {
  const char *text = "<?xml version='1.0' encoding='failing-conv'?>\n"
                     "<doc>\x81</doc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  /* BadEncodingHandler sets up an encoding with every top-bit-set
   * character introducing a two-byte sequence.  For this, it
   * requires a convert function.  The above function call passes
   * one that insists all possible sequences are invalid anyway.
   */
  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Encoding with failing convert() not faulted");
}
END_TEST

/* Test unknown encoding conversions */
START_TEST(test_unknown_encoding_success) {
  const char *text = "<?xml version='1.0' encoding='prefix-conv'?>\n"
                     /* Equivalent to <eoc>Hello, world</eoc> */
                     "<\x81\x64\x80oc>Hello, world</\x81\x64\x80oc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  run_character_check(text, XCS("Hello, world"));
}
END_TEST

/* Test bad name character in unknown encoding */
START_TEST(test_unknown_encoding_bad_name) {
  const char *text = "<?xml version='1.0' encoding='prefix-conv'?>\n"
                     "<\xff\x64oc>Hello, world</\xff\x64oc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Bad name start in unknown encoding not faulted");
}
END_TEST

/* Test bad mid-name character in unknown encoding */
START_TEST(test_unknown_encoding_bad_name_2) {
  const char *text = "<?xml version='1.0' encoding='prefix-conv'?>\n"
                     "<d\xffoc>Hello, world</d\xffoc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Bad name in unknown encoding not faulted");
}
END_TEST

/* Test element name that is long enough to fill the conversion buffer
 * in an unknown encoding, finishing with an encoded character.
 */
START_TEST(test_unknown_encoding_long_name_1) {
  const char *text = "<?xml version='1.0' encoding='prefix-conv'?>\n"
                     "<abcdefghabcdefghabcdefghijkl\x80m\x80n\x80o\x80p>"
                     "Hi"
                     "</abcdefghabcdefghabcdefghijkl\x80m\x80n\x80o\x80p>";
  const XML_Char *expected = XCS("abcdefghabcdefghabcdefghijklmnop");
  CharData storage;

  CharData_Init(&storage);
  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  XML_SetStartElementHandler(g_parser, record_element_start_handler);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test element name that is long enough to fill the conversion buffer
 * in an unknown encoding, finishing with an simple character.
 */
START_TEST(test_unknown_encoding_long_name_2) {
  const char *text = "<?xml version='1.0' encoding='prefix-conv'?>\n"
                     "<abcdefghabcdefghabcdefghijklmnop>"
                     "Hi"
                     "</abcdefghabcdefghabcdefghijklmnop>";
  const XML_Char *expected = XCS("abcdefghabcdefghabcdefghijklmnop");
  CharData storage;

  CharData_Init(&storage);
  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  XML_SetStartElementHandler(g_parser, record_element_start_handler);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_invalid_unknown_encoding) {
  const char *text = "<?xml version='1.0' encoding='invalid-9'?>\n"
                     "<doc>Hello world</doc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  expect_failure(text, XML_ERROR_UNKNOWN_ENCODING,
                 "Invalid unknown encoding not faulted");
}
END_TEST

START_TEST(test_unknown_ascii_encoding_ok) {
  const char *text = "<?xml version='1.0' encoding='ascii-like'?>\n"
                     "<doc>Hello, world</doc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  run_character_check(text, XCS("Hello, world"));
}
END_TEST

START_TEST(test_unknown_ascii_encoding_fail) {
  const char *text = "<?xml version='1.0' encoding='ascii-like'?>\n"
                     "<doc>Hello, \x80 world</doc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Invalid character not faulted");
}
END_TEST

START_TEST(test_unknown_encoding_invalid_length) {
  const char *text = "<?xml version='1.0' encoding='invalid-len'?>\n"
                     "<doc>Hello, world</doc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  expect_failure(text, XML_ERROR_UNKNOWN_ENCODING,
                 "Invalid unknown encoding not faulted");
}
END_TEST

START_TEST(test_unknown_encoding_invalid_topbit) {
  const char *text = "<?xml version='1.0' encoding='invalid-a'?>\n"
                     "<doc>Hello, world</doc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  expect_failure(text, XML_ERROR_UNKNOWN_ENCODING,
                 "Invalid unknown encoding not faulted");
}
END_TEST

START_TEST(test_unknown_encoding_invalid_surrogate) {
  const char *text = "<?xml version='1.0' encoding='invalid-surrogate'?>\n"
                     "<doc>Hello, \x82 world</doc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Invalid unknown encoding not faulted");
}
END_TEST

START_TEST(test_unknown_encoding_invalid_high) {
  const char *text = "<?xml version='1.0' encoding='invalid-high'?>\n"
                     "<doc>Hello, world</doc>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  expect_failure(text, XML_ERROR_UNKNOWN_ENCODING,
                 "Invalid unknown encoding not faulted");
}
END_TEST

START_TEST(test_unknown_encoding_invalid_attr_value) {
  const char *text = "<?xml version='1.0' encoding='prefix-conv'?>\n"
                     "<doc attr='\xff\x30'/>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Invalid attribute valid not faulted");
}
END_TEST

/* Test an external entity parser set to use latin-1 detects UTF-16
 * BOMs correctly.
 */
/* Test that UTF-16 BOM does not select UTF-16 given explicit encoding */
START_TEST(test_ext_entity_latin1_utf16le_bom) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtTest2 test_data
      = {/* If UTF-16, 0xfeff is the BOM and 0x204c is black left bullet */
         /* If Latin-1, 0xff = Y-diaeresis, 0xfe = lowercase thorn,
          *   0x4c = L and 0x20 is a space
          */
         "\xff\xfe\x4c\x20", 4, XCS("iso-8859-1"), NULL};
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00ff\x00feL ");
#else
  /* In UTF-8, y-diaeresis is 0xc3 0xbf, lowercase thorn is 0xc3 0xbe */
  const XML_Char *expected = XCS("\xc3\xbf\xc3\xbeL ");
#endif
  CharData storage;

  CharData_Init(&storage);
  test_data.storage = &storage;
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader2);
  XML_SetUserData(g_parser, &test_data);
  XML_SetCharacterDataHandler(g_parser, ext2_accumulate_characters);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_ext_entity_latin1_utf16be_bom) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtTest2 test_data
      = {/* If UTF-16, 0xfeff is the BOM and 0x204c is black left bullet */
         /* If Latin-1, 0xff = Y-diaeresis, 0xfe = lowercase thorn,
          *   0x4c = L and 0x20 is a space
          */
         "\xfe\xff\x20\x4c", 4, XCS("iso-8859-1"), NULL};
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00fe\x00ff L");
#else
  /* In UTF-8, y-diaeresis is 0xc3 0xbf, lowercase thorn is 0xc3 0xbe */
  const XML_Char *expected = XCS("\xc3\xbe\xc3\xbf L");
#endif
  CharData storage;

  CharData_Init(&storage);
  test_data.storage = &storage;
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader2);
  XML_SetUserData(g_parser, &test_data);
  XML_SetCharacterDataHandler(g_parser, ext2_accumulate_characters);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Parsing the full buffer rather than a byte at a time makes a
 * difference to the encoding scanning code, so repeat the above tests
 * without breaking them down by byte.
 */
START_TEST(test_ext_entity_latin1_utf16le_bom2) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtTest2 test_data
      = {/* If UTF-16, 0xfeff is the BOM and 0x204c is black left bullet */
         /* If Latin-1, 0xff = Y-diaeresis, 0xfe = lowercase thorn,
          *   0x4c = L and 0x20 is a space
          */
         "\xff\xfe\x4c\x20", 4, XCS("iso-8859-1"), NULL};
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00ff\x00feL ");
#else
  /* In UTF-8, y-diaeresis is 0xc3 0xbf, lowercase thorn is 0xc3 0xbe */
  const XML_Char *expected = XCS("\xc3\xbf\xc3\xbeL ");
#endif
  CharData storage;

  CharData_Init(&storage);
  test_data.storage = &storage;
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader2);
  XML_SetUserData(g_parser, &test_data);
  XML_SetCharacterDataHandler(g_parser, ext2_accumulate_characters);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_ext_entity_latin1_utf16be_bom2) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtTest2 test_data
      = {/* If UTF-16, 0xfeff is the BOM and 0x204c is black left bullet */
         /* If Latin-1, 0xff = Y-diaeresis, 0xfe = lowercase thorn,
          *   0x4c = L and 0x20 is a space
          */
         "\xfe\xff\x20\x4c", 4, XCS("iso-8859-1"), NULL};
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00fe\x00ff L");
#else
  /* In UTF-8, y-diaeresis is 0xc3 0xbf, lowercase thorn is 0xc3 0xbe */
  const XML_Char *expected = "\xc3\xbe\xc3\xbf L";
#endif
  CharData storage;

  CharData_Init(&storage);
  test_data.storage = &storage;
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader2);
  XML_SetUserData(g_parser, &test_data);
  XML_SetCharacterDataHandler(g_parser, ext2_accumulate_characters);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test little-endian UTF-16 given an explicit big-endian encoding */
START_TEST(test_ext_entity_utf16_be) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtTest2 test_data = {"<\0e\0/\0>\0", 8, XCS("utf-16be"), NULL};
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x3c00\x6500\x2f00\x3e00");
#else
  const XML_Char *expected = XCS("\xe3\xb0\x80"   /* U+3C00 */
                                 "\xe6\x94\x80"   /* U+6500 */
                                 "\xe2\xbc\x80"   /* U+2F00 */
                                 "\xe3\xb8\x80"); /* U+3E00 */
#endif
  CharData storage;

  CharData_Init(&storage);
  test_data.storage = &storage;
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader2);
  XML_SetUserData(g_parser, &test_data);
  XML_SetCharacterDataHandler(g_parser, ext2_accumulate_characters);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test big-endian UTF-16 given an explicit little-endian encoding */
START_TEST(test_ext_entity_utf16_le) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtTest2 test_data = {"\0<\0e\0/\0>", 8, XCS("utf-16le"), NULL};
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x3c00\x6500\x2f00\x3e00");
#else
  const XML_Char *expected = XCS("\xe3\xb0\x80"   /* U+3C00 */
                                 "\xe6\x94\x80"   /* U+6500 */
                                 "\xe2\xbc\x80"   /* U+2F00 */
                                 "\xe3\xb8\x80"); /* U+3E00 */
#endif
  CharData storage;

  CharData_Init(&storage);
  test_data.storage = &storage;
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader2);
  XML_SetUserData(g_parser, &test_data);
  XML_SetCharacterDataHandler(g_parser, ext2_accumulate_characters);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test little-endian UTF-16 given no explicit encoding.
 * The existing default encoding (UTF-8) is assumed to hold without a
 * BOM to contradict it, so the entity value will in fact provoke an
 * error because 0x00 is not a valid XML character.  We parse the
 * whole buffer in one go rather than feeding it in byte by byte to
 * exercise different code paths in the initial scanning routines.
 */
START_TEST(test_ext_entity_utf16_unknown) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtFaults2 test_data
      = {"a\0b\0c\0", 6, "Invalid character in entity not faulted", NULL,
         XML_ERROR_INVALID_TOKEN};

  XML_SetExternalEntityRefHandler(g_parser, external_entity_faulter2);
  XML_SetUserData(g_parser, &test_data);
  expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                 "Invalid character should not have been accepted");
}
END_TEST

/* Test not-quite-UTF-8 BOM (0xEF 0xBB 0xBF) */
START_TEST(test_ext_entity_utf8_non_bom) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  ExtTest2 test_data
      = {"\xef\xbb\x80", /* Arabic letter DAD medial form, U+FEC0 */
         3, NULL, NULL};
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\xfec0");
#else
  const XML_Char *expected = XCS("\xef\xbb\x80");
#endif
  CharData storage;

  CharData_Init(&storage);
  test_data.storage = &storage;
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader2);
  XML_SetUserData(g_parser, &test_data);
  XML_SetCharacterDataHandler(g_parser, ext2_accumulate_characters);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test that UTF-8 in a CDATA section is correctly passed through */
START_TEST(test_utf8_in_cdata_section) {
  const char *text = "<doc><![CDATA[one \xc3\xa9 two]]></doc>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("one \x00e9 two");
#else
  const XML_Char *expected = XCS("one \xc3\xa9 two");
#endif

  run_character_check(text, expected);
}
END_TEST

/* Test that little-endian UTF-16 in a CDATA section is handled */
START_TEST(test_utf8_in_cdata_section_2) {
  const char *text = "<doc><![CDATA[\xc3\xa9]\xc3\xa9two]]></doc>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00e9]\x00e9two");
#else
  const XML_Char *expected = XCS("\xc3\xa9]\xc3\xa9two");
#endif

  run_character_check(text, expected);
}
END_TEST

START_TEST(test_utf8_in_start_tags) {
  struct test_case {
    bool goodName;
    bool goodNameStart;
    const char *tagName;
  };

  // The idea with the tests below is this:
  // We want to cover 1-, 2- and 3-byte sequences, 4-byte sequences
  // go to isNever and are hence not a concern.
  //
  // We start with a character that is a valid name character
  // (or even name-start character, see XML 1.0r4 spec) and then we flip
  // single bits at places where (1) the result leaves the UTF-8 encoding space
  // and (2) we stay in the same n-byte sequence family.
  //
  // The flipped bits are highlighted in angle brackets in comments,
  // e.g. "[<1>011 1001]" means we had [0011 1001] but we now flipped
  // the most significant bit to 1 to leave UTF-8 encoding space.
  struct test_case cases[] = {
      // 1-byte UTF-8: [0xxx xxxx]
      {true, true, "\x3A"},   // [0011 1010] = ASCII colon ':'
      {false, false, "\xBA"}, // [<1>011 1010]
      {true, false, "\x39"},  // [0011 1001] = ASCII nine '9'
      {false, false, "\xB9"}, // [<1>011 1001]

      // 2-byte UTF-8: [110x xxxx] [10xx xxxx]
      {true, true, "\xDB\xA5"},   // [1101 1011] [1010 0101] =
                                  // Arabic small waw U+06E5
      {false, false, "\x9B\xA5"}, // [1<0>01 1011] [1010 0101]
      {false, false, "\xDB\x25"}, // [1101 1011] [<0>010 0101]
      {false, false, "\xDB\xE5"}, // [1101 1011] [1<1>10 0101]
      {true, false, "\xCC\x81"},  // [1100 1100] [1000 0001] =
                                  // combining char U+0301
      {false, false, "\x8C\x81"}, // [1<0>00 1100] [1000 0001]
      {false, false, "\xCC\x01"}, // [1100 1100] [<0>000 0001]
      {false, false, "\xCC\xC1"}, // [1100 1100] [1<1>00 0001]

      // 3-byte UTF-8: [1110 xxxx] [10xx xxxx] [10xxxxxx]
      {true, true, "\xE0\xA4\x85"},   // [1110 0000] [1010 0100] [1000 0101] =
                                      // Devanagari Letter A U+0905
      {false, false, "\xA0\xA4\x85"}, // [1<0>10 0000] [1010 0100] [1000 0101]
      {false, false, "\xE0\x24\x85"}, // [1110 0000] [<0>010 0100] [1000 0101]
      {false, false, "\xE0\xE4\x85"}, // [1110 0000] [1<1>10 0100] [1000 0101]
      {false, false, "\xE0\xA4\x05"}, // [1110 0000] [1010 0100] [<0>000 0101]
      {false, false, "\xE0\xA4\xC5"}, // [1110 0000] [1010 0100] [1<1>00 0101]
      {true, false, "\xE0\xA4\x81"},  // [1110 0000] [1010 0100] [1000 0001] =
                                      // combining char U+0901
      {false, false, "\xA0\xA4\x81"}, // [1<0>10 0000] [1010 0100] [1000 0001]
      {false, false, "\xE0\x24\x81"}, // [1110 0000] [<0>010 0100] [1000 0001]
      {false, false, "\xE0\xE4\x81"}, // [1110 0000] [1<1>10 0100] [1000 0001]
      {false, false, "\xE0\xA4\x01"}, // [1110 0000] [1010 0100] [<0>000 0001]
      {false, false, "\xE0\xA4\xC1"}, // [1110 0000] [1010 0100] [1<1>00 0001]
  };
  const bool atNameStart[] = {true, false};

  size_t i = 0;
  char doc[1024];
  size_t failCount = 0;

  // we need all the bytes to be parsed, but we don't want the errors that can
  // trigger on isFinal=XML_TRUE, so we skip the test if the heuristic is on.
  if (g_reparseDeferralEnabledDefault) {
    return;
  }

  for (; i < sizeof(cases) / sizeof(cases[0]); i++) {
    size_t j = 0;
    for (; j < sizeof(atNameStart) / sizeof(atNameStart[0]); j++) {
      const bool expectedSuccess
          = atNameStart[j] ? cases[i].goodNameStart : cases[i].goodName;
      snprintf(doc, sizeof(doc), "<%s%s><!--", atNameStart[j] ? "" : "a",
               cases[i].tagName);
      XML_Parser parser = XML_ParserCreate(NULL);

      const enum XML_Status status = _XML_Parse_SINGLE_BYTES(
          parser, doc, (int)strlen(doc), /*isFinal=*/XML_FALSE);

      bool success = true;
      if ((status == XML_STATUS_OK) != expectedSuccess) {
        success = false;
      }
      if ((status == XML_STATUS_ERROR)
          && (XML_GetErrorCode(parser) != XML_ERROR_INVALID_TOKEN)) {
        success = false;
      }

      if (! success) {
        fprintf(
            stderr,
            "FAIL case %2u (%sat name start, %u-byte sequence, error code %d)\n",
            (unsigned)i + 1u, atNameStart[j] ? "    " : "not ",
            (unsigned)strlen(cases[i].tagName), XML_GetErrorCode(parser));
        failCount++;
      }

      XML_ParserFree(parser);
    }
  }

  if (failCount > 0) {
    fail("UTF-8 regression detected");
  }
}
END_TEST

/* Test trailing spaces in elements are accepted */
START_TEST(test_trailing_spaces_in_elements) {
  const char *text = "<doc   >Hi</doc >";
  const XML_Char *expected = XCS("doc/doc");
  CharData storage;

  CharData_Init(&storage);
  XML_SetElementHandler(g_parser, record_element_start_handler,
                        record_element_end_handler);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_utf16_attribute) {
  const char text[] =
      /* <d {KHO KHWAI}{CHO CHAN}='a'/>
       * where {KHO KHWAI} = U+0E04 = 0xe0 0xb8 0x84 in UTF-8
       * and   {CHO CHAN}  = U+0E08 = 0xe0 0xb8 0x88 in UTF-8
       */
      "<\0d\0 \0\x04\x0e\x08\x0e=\0'\0a\0'\0/\0>\0";
  const XML_Char *expected = XCS("a");
  CharData storage;

  CharData_Init(&storage);
  XML_SetStartElementHandler(g_parser, accumulate_attribute);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_utf16_second_attr) {
  /* <d a='1' {KHO KHWAI}{CHO CHAN}='2'/>
   * where {KHO KHWAI} = U+0E04 = 0xe0 0xb8 0x84 in UTF-8
   * and   {CHO CHAN}  = U+0E08 = 0xe0 0xb8 0x88 in UTF-8
   */
  const char text[] = "<\0d\0 \0a\0=\0'\0\x31\0'\0 \0"
                      "\x04\x0e\x08\x0e=\0'\0\x32\0'\0/\0>\0";
  const XML_Char *expected = XCS("1");
  CharData storage;

  CharData_Init(&storage);
  XML_SetStartElementHandler(g_parser, accumulate_attribute);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_attr_after_solidus) {
  const char *text = "<doc attr1='a' / attr2='b'>";

  expect_failure(text, XML_ERROR_INVALID_TOKEN, "Misplaced / not faulted");
}
END_TEST

START_TEST(test_utf16_pe) {
  /* <!DOCTYPE doc [
   * <!ENTITY % {KHO KHWAI}{CHO CHAN} '<!ELEMENT doc (#PCDATA)>'>
   * %{KHO KHWAI}{CHO CHAN};
   * ]>
   * <doc></doc>
   *
   * where {KHO KHWAI} = U+0E04 = 0xe0 0xb8 0x84 in UTF-8
   * and   {CHO CHAN}  = U+0E08 = 0xe0 0xb8 0x88 in UTF-8
   */
  const char text[] = "\0<\0!\0D\0O\0C\0T\0Y\0P\0E\0 \0d\0o\0c\0 \0[\0\n"
                      "\0<\0!\0E\0N\0T\0I\0T\0Y\0 \0%\0 \x0e\x04\x0e\x08\0 "
                      "\0'\0<\0!\0E\0L\0E\0M\0E\0N\0T\0 "
                      "\0d\0o\0c\0 \0(\0#\0P\0C\0D\0A\0T\0A\0)\0>\0'\0>\0\n"
                      "\0%\x0e\x04\x0e\x08\0;\0\n"
                      "\0]\0>\0\n"
                      "\0<\0d\0o\0c\0>\0<\0/\0d\0o\0c\0>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x0e04\x0e08=<!ELEMENT doc (#PCDATA)>\n");
#else
  const XML_Char *expected
      = XCS("\xe0\xb8\x84\xe0\xb8\x88=<!ELEMENT doc (#PCDATA)>\n");
#endif
  CharData storage;

  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetEntityDeclHandler(g_parser, accumulate_entity_decl);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test that duff attribute description keywords are rejected */
START_TEST(test_bad_attr_desc_keyword) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ATTLIST doc attr CDATA #!IMPLIED>\n"
                     "]>\n"
                     "<doc />";

  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Bad keyword !IMPLIED not faulted");
}
END_TEST

/* Test that an invalid attribute description keyword consisting of
 * UTF-16 characters with their top bytes non-zero are correctly
 * faulted
 */
START_TEST(test_bad_attr_desc_keyword_utf16) {
  /* <!DOCTYPE d [
   * <!ATTLIST d a CDATA #{KHO KHWAI}{CHO CHAN}>
   * ]><d/>
   *
   * where {KHO KHWAI} = U+0E04 = 0xe0 0xb8 0x84 in UTF-8
   * and   {CHO CHAN}  = U+0E08 = 0xe0 0xb8 0x88 in UTF-8
   */
  const char text[]
      = "\0<\0!\0D\0O\0C\0T\0Y\0P\0E\0 \0d\0 \0[\0\n"
        "\0<\0!\0A\0T\0T\0L\0I\0S\0T\0 \0d\0 \0a\0 \0C\0D\0A\0T\0A\0 "
        "\0#\x0e\x04\x0e\x08\0>\0\n"
        "\0]\0>\0<\0d\0/\0>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Invalid UTF16 attribute keyword not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_SYNTAX)
    xml_failure(g_parser);
}
END_TEST

/* Test that invalid syntax in a <!DOCTYPE> is rejected.  Do this
 * using prefix-encoding (see above) to trigger specific code paths
 */
START_TEST(test_bad_doctype) {
  const char *text = "<?xml version='1.0' encoding='prefix-conv'?>\n"
                     "<!DOCTYPE doc [ \x80\x44 ]><doc/>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  expect_failure(text, XML_ERROR_SYNTAX,
                 "Invalid bytes in DOCTYPE not faulted");
}
END_TEST

START_TEST(test_bad_doctype_utf8) {
  const char *text = "<!DOCTYPE \xDB\x25"
                     "doc><doc/>"; // [1101 1011] [<0>010 0101]
  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Invalid UTF-8 in DOCTYPE not faulted");
}
END_TEST

START_TEST(test_bad_doctype_utf16) {
  const char text[] =
      /* <!DOCTYPE doc [ \x06f2 ]><doc/>
       *
       * U+06F2 = EXTENDED ARABIC-INDIC DIGIT TWO, a valid number
       * (name character) but not a valid letter (name start character)
       */
      "\0<\0!\0D\0O\0C\0T\0Y\0P\0E\0 \0d\0o\0c\0 \0[\0 "
      "\x06\xf2"
      "\0 \0]\0>\0<\0d\0o\0c\0/\0>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Invalid bytes in DOCTYPE not faulted");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_SYNTAX)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_bad_doctype_plus) {
  const char *text = "<!DOCTYPE 1+ [ <!ENTITY foo 'bar'> ]>\n"
                     "<1+>&foo;</1+>";

  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "'+' in document name not faulted");
}
END_TEST

START_TEST(test_bad_doctype_star) {
  const char *text = "<!DOCTYPE 1* [ <!ENTITY foo 'bar'> ]>\n"
                     "<1*>&foo;</1*>";

  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "'*' in document name not faulted");
}
END_TEST

START_TEST(test_bad_doctype_query) {
  const char *text = "<!DOCTYPE 1? [ <!ENTITY foo 'bar'> ]>\n"
                     "<1?>&foo;</1?>";

  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "'?' in document name not faulted");
}
END_TEST

START_TEST(test_unknown_encoding_bad_ignore) {
  const char *text = "<?xml version='1.0' encoding='prefix-conv'?>"
                     "<!DOCTYPE doc SYSTEM 'foo'>"
                     "<doc><e>&entity;</e></doc>";
  ExtFaults fault = {"<![IGNORE[<!ELEMENT \xffG (#PCDATA)*>]]>",
                     "Invalid character not faulted", XCS("prefix-conv"),
                     XML_ERROR_INVALID_TOKEN};

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_faulter);
  XML_SetUserData(g_parser, &fault);
  expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                 "Bad IGNORE section with unknown encoding not failed");
}
END_TEST

START_TEST(test_entity_in_utf16_be_attr) {
  const char text[] =
      /* <e a='&#228; &#x00E4;'></e> */
      "\0<\0e\0 \0a\0=\0'\0&\0#\0\x32\0\x32\0\x38\0;\0 "
      "\0&\0#\0x\0\x30\0\x30\0E\0\x34\0;\0'\0>\0<\0/\0e\0>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00e4 \x00e4");
#else
  const XML_Char *expected = XCS("\xc3\xa4 \xc3\xa4");
#endif
  CharData storage;

  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetStartElementHandler(g_parser, accumulate_attribute);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_entity_in_utf16_le_attr) {
  const char text[] =
      /* <e a='&#228; &#x00E4;'></e> */
      "<\0e\0 \0a\0=\0'\0&\0#\0\x32\0\x32\0\x38\0;\0 \0"
      "&\0#\0x\0\x30\0\x30\0E\0\x34\0;\0'\0>\0<\0/\0e\0>\0";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("\x00e4 \x00e4");
#else
  const XML_Char *expected = XCS("\xc3\xa4 \xc3\xa4");
#endif
  CharData storage;

  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetStartElementHandler(g_parser, accumulate_attribute);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_entity_public_utf16_be) {
  const char text[] =
      /* <!DOCTYPE d [ */
      "\0<\0!\0D\0O\0C\0T\0Y\0P\0E\0 \0d\0 \0[\0\n"
      /* <!ENTITY % e PUBLIC 'foo' 'bar.ent'> */
      "\0<\0!\0E\0N\0T\0I\0T\0Y\0 \0%\0 \0e\0 \0P\0U\0B\0L\0I\0C\0 "
      "\0'\0f\0o\0o\0'\0 \0'\0b\0a\0r\0.\0e\0n\0t\0'\0>\0\n"
      /* %e; */
      "\0%\0e\0;\0\n"
      /* ]> */
      "\0]\0>\0\n"
      /* <d>&j;</d> */
      "\0<\0d\0>\0&\0j\0;\0<\0/\0d\0>";
  ExtTest2 test_data
      = {/* <!ENTITY j 'baz'> */
         "\0<\0!\0E\0N\0T\0I\0T\0Y\0 \0j\0 \0'\0b\0a\0z\0'\0>", 34, NULL, NULL};
  const XML_Char *expected = XCS("baz");
  CharData storage;

  CharData_Init(&storage);
  test_data.storage = &storage;
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader2);
  XML_SetUserData(g_parser, &test_data);
  XML_SetCharacterDataHandler(g_parser, ext2_accumulate_characters);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_entity_public_utf16_le) {
  const char text[] =
      /* <!DOCTYPE d [ */
      "<\0!\0D\0O\0C\0T\0Y\0P\0E\0 \0d\0 \0[\0\n\0"
      /* <!ENTITY % e PUBLIC 'foo' 'bar.ent'> */
      "<\0!\0E\0N\0T\0I\0T\0Y\0 \0%\0 \0e\0 \0P\0U\0B\0L\0I\0C\0 \0"
      "'\0f\0o\0o\0'\0 \0'\0b\0a\0r\0.\0e\0n\0t\0'\0>\0\n\0"
      /* %e; */
      "%\0e\0;\0\n\0"
      /* ]> */
      "]\0>\0\n\0"
      /* <d>&j;</d> */
      "<\0d\0>\0&\0j\0;\0<\0/\0d\0>\0";
  ExtTest2 test_data
      = {/* <!ENTITY j 'baz'> */
         "<\0!\0E\0N\0T\0I\0T\0Y\0 \0j\0 \0'\0b\0a\0z\0'\0>\0", 34, NULL, NULL};
  const XML_Char *expected = XCS("baz");
  CharData storage;

  CharData_Init(&storage);
  test_data.storage = &storage;
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_loader2);
  XML_SetUserData(g_parser, &test_data);
  XML_SetCharacterDataHandler(g_parser, ext2_accumulate_characters);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test that a doctype with neither an internal nor external subset is
 * faulted
 */
START_TEST(test_short_doctype) {
  const char *text = "<!DOCTYPE doc></doc>";
  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "DOCTYPE without subset not rejected");
}
END_TEST

START_TEST(test_short_doctype_2) {
  const char *text = "<!DOCTYPE doc PUBLIC></doc>";
  expect_failure(text, XML_ERROR_SYNTAX,
                 "DOCTYPE without Public ID not rejected");
}
END_TEST

START_TEST(test_short_doctype_3) {
  const char *text = "<!DOCTYPE doc SYSTEM></doc>";
  expect_failure(text, XML_ERROR_SYNTAX,
                 "DOCTYPE without System ID not rejected");
}
END_TEST

START_TEST(test_long_doctype) {
  const char *text = "<!DOCTYPE doc PUBLIC 'foo' 'bar' 'baz'></doc>";
  expect_failure(text, XML_ERROR_SYNTAX, "DOCTYPE with extra ID not rejected");
}
END_TEST

START_TEST(test_bad_entity) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY foo PUBLIC>\n"
                     "]>\n"
                     "<doc/>";
  expect_failure(text, XML_ERROR_SYNTAX,
                 "ENTITY without Public ID is not rejected");
}
END_TEST

/* Test unquoted value is faulted */
START_TEST(test_bad_entity_2) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY % foo bar>\n"
                     "]>\n"
                     "<doc/>";
  expect_failure(text, XML_ERROR_SYNTAX,
                 "ENTITY without Public ID is not rejected");
}
END_TEST

START_TEST(test_bad_entity_3) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY % foo PUBLIC>\n"
                     "]>\n"
                     "<doc/>";
  expect_failure(text, XML_ERROR_SYNTAX,
                 "Parameter ENTITY without Public ID is not rejected");
}
END_TEST

START_TEST(test_bad_entity_4) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY % foo SYSTEM>\n"
                     "]>\n"
                     "<doc/>";
  expect_failure(text, XML_ERROR_SYNTAX,
                 "Parameter ENTITY without Public ID is not rejected");
}
END_TEST

START_TEST(test_bad_notation) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!NOTATION n SYSTEM>\n"
                     "]>\n"
                     "<doc/>";
  expect_failure(text, XML_ERROR_SYNTAX,
                 "Notation without System ID is not rejected");
}
END_TEST

/* Test for issue #11, wrongly suppressed default handler */
START_TEST(test_default_doctype_handler) {
  const char *text = "<!DOCTYPE doc PUBLIC 'pubname' 'test.dtd' [\n"
                     "  <!ENTITY foo 'bar'>\n"
                     "]>\n"
                     "<doc>&foo;</doc>";
  DefaultCheck test_data[] = {{XCS("'pubname'"), 9, XML_FALSE},
                              {XCS("'test.dtd'"), 10, XML_FALSE},
                              {NULL, 0, XML_FALSE}};
  int i;

  XML_SetUserData(g_parser, &test_data);
  XML_SetDefaultHandler(g_parser, checking_default_handler);
  XML_SetEntityDeclHandler(g_parser, dummy_entity_decl_handler);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  for (i = 0; test_data[i].expected != NULL; i++)
    if (! test_data[i].seen)
      fail("Default handler not run for public !DOCTYPE");
}
END_TEST

START_TEST(test_empty_element_abort) {
  const char *text = "<abort/>";

  XML_SetStartElementHandler(g_parser, start_element_suspender);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Expected to error on abort");
}
END_TEST

/* Regression test for GH issue #612: unfinished m_declAttributeType
 * allocation in ->m_tempPool can corrupt following allocation.
 */
START_TEST(test_pool_integrity_with_unfinished_attr) {
  const char *text = "<?xml version='1.0' encoding='UTF-8'?>\n"
                     "<!DOCTYPE foo [\n"
                     "<!ELEMENT foo ANY>\n"
                     "<!ENTITY % entp SYSTEM \"external.dtd\">\n"
                     "%entp;\n"
                     "]>\n"
                     "<a></a>\n";
  const XML_Char *expected = XCS("COMMENT");
  CharData storage;

  CharData_Init(&storage);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_unfinished_attlist);
  XML_SetAttlistDeclHandler(g_parser, dummy_attlist_decl_handler);
  XML_SetCommentHandler(g_parser, accumulate_comment);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

/* Test a possible early return location in internalEntityProcessor */
START_TEST(test_entity_ref_no_elements) {
  const char *const text = "<!DOCTYPE foo [\n"
                           "<!ENTITY e1 \"test\">\n"
                           "]> <foo>&e1;"; // intentionally missing newline

  XML_Parser parser = XML_ParserCreate(NULL);
  assert_true(_XML_Parse_SINGLE_BYTES(parser, text, (int)strlen(text), XML_TRUE)
              == XML_STATUS_ERROR);
  assert_true(XML_GetErrorCode(parser) == XML_ERROR_NO_ELEMENTS);
  XML_ParserFree(parser);
}
END_TEST

/* Tests if chained entity references lead to unbounded recursion */
START_TEST(test_deep_nested_entity) {
  const size_t N_LINES = 60000;
  const size_t SIZE_PER_LINE = 50;

  char *const text = (char *)malloc((N_LINES + 4) * SIZE_PER_LINE);
  if (text == NULL) {
    fail("malloc failed");
  }

  char *textPtr = text;

  // Create the XML
  textPtr += snprintf(textPtr, SIZE_PER_LINE,
                      "<!DOCTYPE foo [\n"
                      "	<!ENTITY s0 'deepText'>\n");

  for (size_t i = 1; i < N_LINES; ++i) {
    textPtr += snprintf(textPtr, SIZE_PER_LINE, "  <!ENTITY s%lu '&s%lu;'>\n",
                        (long unsigned)i, (long unsigned)(i - 1));
  }

  snprintf(textPtr, SIZE_PER_LINE, "]> <foo>&s%lu;</foo>\n",
           (long unsigned)(N_LINES - 1));

  const XML_Char *const expected = XCS("deepText");

  CharData storage;
  CharData_Init(&storage);

  XML_Parser parser = XML_ParserCreate(NULL);

  XML_SetCharacterDataHandler(parser, accumulate_characters);
  XML_SetUserData(parser, &storage);

  if (_XML_Parse_SINGLE_BYTES(parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(parser);

  CharData_CheckXMLChars(&storage, expected);
  XML_ParserFree(parser);
  free(text);
}
END_TEST

/* Tests if chained entity references in attributes
lead to unbounded recursion */
START_TEST(test_deep_nested_attribute_entity) {
  const size_t N_LINES = 60000;
  const size_t SIZE_PER_LINE = 100;

  char *const text = (char *)malloc((N_LINES + 4) * SIZE_PER_LINE);
  if (text == NULL) {
    fail("malloc failed");
  }

  char *textPtr = text;

  // Create the XML
  textPtr += snprintf(textPtr, SIZE_PER_LINE,
                      "<!DOCTYPE foo [\n"
                      "	<!ENTITY s0 'deepText'>\n");

  for (size_t i = 1; i < N_LINES; ++i) {
    textPtr += snprintf(textPtr, SIZE_PER_LINE, "  <!ENTITY s%lu '&s%lu;'>\n",
                        (long unsigned)i, (long unsigned)(i - 1));
  }

  snprintf(textPtr, SIZE_PER_LINE, "]> <foo name='&s%lu;'>mainText</foo>\n",
           (long unsigned)(N_LINES - 1));

  AttrInfo doc_info[] = {{XCS("name"), XCS("deepText")}, {NULL, NULL}};
  ElementInfo info[] = {{XCS("foo"), 1, NULL, NULL}, {NULL, 0, NULL, NULL}};
  info[0].attributes = doc_info;

  XML_Parser parser = XML_ParserCreate(NULL);
  ParserAndElementInfo parserPlusElemenInfo = {parser, info};

  XML_SetStartElementHandler(parser, counting_start_element_handler);
  XML_SetUserData(parser, &parserPlusElemenInfo);

  if (_XML_Parse_SINGLE_BYTES(parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(parser);

  XML_ParserFree(parser);
  free(text);
}
END_TEST

START_TEST(test_deep_nested_entity_delayed_interpretation) {
  const size_t N_LINES = 70000;
  const size_t SIZE_PER_LINE = 100;

  char *const text = (char *)malloc((N_LINES + 4) * SIZE_PER_LINE);
  if (text == NULL) {
    fail("malloc failed");
  }

  char *textPtr = text;

  // Create the XML
  textPtr += snprintf(textPtr, SIZE_PER_LINE,
                      "<!DOCTYPE foo [\n"
                      "	<!ENTITY %% s0 'deepText'>\n");

  for (size_t i = 1; i < N_LINES; ++i) {
    textPtr += snprintf(textPtr, SIZE_PER_LINE,
                        "  <!ENTITY %% s%lu '&#37;s%lu;'>\n", (long unsigned)i,
                        (long unsigned)(i - 1));
  }

  snprintf(textPtr, SIZE_PER_LINE,
           "  <!ENTITY %% define_g \"<!ENTITY g '&#37;s%lu;'>\">\n"
           "  %%define_g;\n"
           "]>\n"
           "<foo/>\n",
           (long unsigned)(N_LINES - 1));

  XML_Parser parser = XML_ParserCreate(NULL);

  XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  if (_XML_Parse_SINGLE_BYTES(parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(parser);

  XML_ParserFree(parser);
  free(text);
}
END_TEST

START_TEST(test_nested_entity_suspend) {
  const char *const text = "<!DOCTYPE a [\n"
                           "  <!ENTITY e1 '<!--e1-->'>\n"
                           "  <!ENTITY e2 '<!--e2 head-->&e1;<!--e2 tail-->'>\n"
                           "  <!ENTITY e3 '<!--e3 head-->&e2;<!--e3 tail-->'>\n"
                           "]>\n"
                           "<a><!--start-->&e3;<!--end--></a>";
  const XML_Char *const expected = XCS("start") XCS("e3 head") XCS("e2 head")
      XCS("e1") XCS("e2 tail") XCS("e3 tail") XCS("end");
  CharData storage;
  CharData_Init(&storage);
  XML_Parser parser = XML_ParserCreate(NULL);
  ParserPlusStorage parserPlusStorage = {parser, &storage};

  XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetCommentHandler(parser, accumulate_and_suspend_comment_handler);
  XML_SetUserData(parser, &parserPlusStorage);

  enum XML_Status status = XML_Parse(parser, text, (int)strlen(text), XML_TRUE);
  while (status == XML_STATUS_SUSPENDED) {
    status = XML_ResumeParser(parser);
  }
  if (status != XML_STATUS_OK)
    xml_failure(parser);

  CharData_CheckXMLChars(&storage, expected);
  XML_ParserFree(parser);
}
END_TEST

START_TEST(test_nested_entity_suspend_2) {
  const char *const text = "<!DOCTYPE doc [\n"
                           "  <!ENTITY ge1 'head1Ztail1'>\n"
                           "  <!ENTITY ge2 'head2&ge1;tail2'>\n"
                           "  <!ENTITY ge3 'head3&ge2;tail3'>\n"
                           "]>\n"
                           "<doc>&ge3;</doc>";
  const XML_Char *const expected = XCS("head3") XCS("head2") XCS("head1")
      XCS("Z") XCS("tail1") XCS("tail2") XCS("tail3");
  CharData storage;
  CharData_Init(&storage);
  XML_Parser parser = XML_ParserCreate(NULL);
  ParserPlusStorage parserPlusStorage = {parser, &storage};

  XML_SetCharacterDataHandler(parser, accumulate_char_data_and_suspend);
  XML_SetUserData(parser, &parserPlusStorage);

  enum XML_Status status = XML_Parse(parser, text, (int)strlen(text), XML_TRUE);
  while (status == XML_STATUS_SUSPENDED) {
    status = XML_ResumeParser(parser);
  }
  if (status != XML_STATUS_OK)
    xml_failure(parser);

  CharData_CheckXMLChars(&storage, expected);
  XML_ParserFree(parser);
}
END_TEST

#if defined(XML_TESTING)
/* Regression test for quadratic parsing on large tokens */
START_TEST(test_big_tokens_scale_linearly) {
  const struct {
    const char *pre;
    const char *post;
  } text[] = {
      {"<a>", "</a>"},                      // assumed good, used as baseline
      {"<b><![CDATA[ value: ", " ]]></b>"}, // CDATA, performed OK before patch
      {"<c attr='", "'></c>"},              // big attribute, used to be O(N²)
      {"<d><!-- ", " --></d>"},             // long comment, used to be O(N²)
      {"<e><", "/></e>"},                   // big elem name, used to be O(N²)
  };
  const int num_cases = sizeof(text) / sizeof(text[0]);
  char aaaaaa[4096];
  const int fillsize = (int)sizeof(aaaaaa);
  const int fillcount = 100;
  const unsigned approx_bytes = fillsize * fillcount; // ignore pre/post.
  const unsigned max_factor = 4;
  const unsigned max_scanned = max_factor * approx_bytes;

  memset(aaaaaa, 'a', fillsize);

  if (! g_reparseDeferralEnabledDefault) {
    return; // heuristic is disabled; we would get O(n^2) and fail.
  }

  for (int i = 0; i < num_cases; ++i) {
    XML_Parser parser = XML_ParserCreate(NULL);
    assert_true(parser != NULL);
    enum XML_Status status;
    set_subtest("text=\"%saaaaaa%s\"", text[i].pre, text[i].post);

    // parse the start text
    g_bytesScanned = 0;
    status = _XML_Parse_SINGLE_BYTES(parser, text[i].pre,
                                     (int)strlen(text[i].pre), XML_FALSE);
    if (status != XML_STATUS_OK) {
      xml_failure(parser);
    }

    // parse lots of 'a', failing the test early if it takes too long
    unsigned past_max_count = 0;
    for (int f = 0; f < fillcount; ++f) {
      status = _XML_Parse_SINGLE_BYTES(parser, aaaaaa, fillsize, XML_FALSE);
      if (status != XML_STATUS_OK) {
        xml_failure(parser);
      }
      if (g_bytesScanned > max_scanned) {
        // We're not done, and have already passed the limit -- the test will
        // definitely fail. This block allows us to save time by failing early.
        const unsigned pushed
            = (unsigned)strlen(text[i].pre) + (f + 1) * fillsize;
        fprintf(
            stderr,
            "after %d/%d loops: pushed=%u scanned=%u (factor ~%.2f) max_scanned: %u (factor ~%u)\n",
            f + 1, fillcount, pushed, g_bytesScanned,
            g_bytesScanned / (double)pushed, max_scanned, max_factor);
        past_max_count++;
        // We are failing, but allow a few log prints first. If we don't reach
        // a count of five, the test will fail after the loop instead.
        assert_true(past_max_count < 5);
      }
    }

    // parse the end text
    status = _XML_Parse_SINGLE_BYTES(parser, text[i].post,
                                     (int)strlen(text[i].post), XML_TRUE);
    if (status != XML_STATUS_OK) {
      xml_failure(parser);
    }

    assert_true(g_bytesScanned > approx_bytes); // or the counter isn't working
    if (g_bytesScanned > max_scanned) {
      fprintf(
          stderr,
          "after all input: scanned=%u (factor ~%.2f) max_scanned: %u (factor ~%u)\n",
          g_bytesScanned, g_bytesScanned / (double)approx_bytes, max_scanned,
          max_factor);
      fail("scanned too many bytes");
    }

    XML_ParserFree(parser);
  }
}
END_TEST
#endif

START_TEST(test_set_reparse_deferral) {
  const char *const pre = "<d>";
  const char *const start = "<x attr='";
  const char *const end = "'></x>";
  char eeeeee[100];
  const int fillsize = (int)sizeof(eeeeee);
  memset(eeeeee, 'e', fillsize);

  for (int enabled = 0; enabled <= 1; enabled += 1) {
    set_subtest("deferral=%d", enabled);

    XML_Parser parser = XML_ParserCreate(NULL);
    assert_true(parser != NULL);
    assert_true(XML_SetReparseDeferralEnabled(parser, enabled));
    // pre-grow the buffer to avoid reparsing due to almost-fullness
    assert_true(XML_GetBuffer(parser, fillsize * 10103) != NULL);

    CharData storage;
    CharData_Init(&storage);
    XML_SetUserData(parser, &storage);
    XML_SetStartElementHandler(parser, start_element_event_handler);

    enum XML_Status status;
    // parse the start text
    status = XML_Parse(parser, pre, (int)strlen(pre), XML_FALSE);
    if (status != XML_STATUS_OK) {
      xml_failure(parser);
    }
    CharData_CheckXMLChars(&storage, XCS("d")); // first element should be done

    // ..and the start of the token
    status = XML_Parse(parser, start, (int)strlen(start), XML_FALSE);
    if (status != XML_STATUS_OK) {
      xml_failure(parser);
    }
    CharData_CheckXMLChars(&storage, XCS("d")); // still just the first one

    // try to parse lots of 'e', but the token isn't finished
    for (int c = 0; c < 100; ++c) {
      status = XML_Parse(parser, eeeeee, fillsize, XML_FALSE);
      if (status != XML_STATUS_OK) {
        xml_failure(parser);
      }
    }
    CharData_CheckXMLChars(&storage, XCS("d")); // *still* just the first one

    // end the <x> token.
    status = XML_Parse(parser, end, (int)strlen(end), XML_FALSE);
    if (status != XML_STATUS_OK) {
      xml_failure(parser);
    }

    if (enabled) {
      // In general, we may need to push more data to trigger a reparse attempt,
      // but in this test, the data is constructed to always require it.
      CharData_CheckXMLChars(&storage, XCS("d")); // or the test is incorrect
      // 2x the token length should suffice; the +1 covers the start and end.
      for (int c = 0; c < 101; ++c) {
        status = XML_Parse(parser, eeeeee, fillsize, XML_FALSE);
        if (status != XML_STATUS_OK) {
          xml_failure(parser);
        }
      }
    }
    CharData_CheckXMLChars(&storage, XCS("dx")); // the <x> should be done

    XML_ParserFree(parser);
  }
}
END_TEST

struct element_decl_data {
  XML_Parser parser;
  int count;
};

static void
element_decl_counter(void *userData, const XML_Char *name, XML_Content *model) {
  UNUSED_P(name);
  struct element_decl_data *testdata = (struct element_decl_data *)userData;
  testdata->count += 1;
  XML_FreeContentModel(testdata->parser, model);
}

static int
external_inherited_parser(XML_Parser p, const XML_Char *context,
                          const XML_Char *base, const XML_Char *systemId,
                          const XML_Char *publicId) {
  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  const char *const pre = "<!ELEMENT document ANY>\n";
  const char *const start = "<!ELEMENT ";
  const char *const end = " ANY>\n";
  const char *const post = "<!ELEMENT xyz ANY>\n";
  const int enabled = *(int *)XML_GetUserData(p);
  char eeeeee[100];
  char spaces[100];
  const int fillsize = (int)sizeof(eeeeee);
  assert_true(fillsize == (int)sizeof(spaces));
  memset(eeeeee, 'e', fillsize);
  memset(spaces, ' ', fillsize);

  XML_Parser parser = XML_ExternalEntityParserCreate(p, context, NULL);
  assert_true(parser != NULL);
  // pre-grow the buffer to avoid reparsing due to almost-fullness
  assert_true(XML_GetBuffer(parser, fillsize * 10103) != NULL);

  struct element_decl_data testdata;
  testdata.parser = parser;
  testdata.count = 0;
  XML_SetUserData(parser, &testdata);
  XML_SetElementDeclHandler(parser, element_decl_counter);

  enum XML_Status status;
  // parse the initial text
  status = XML_Parse(parser, pre, (int)strlen(pre), XML_FALSE);
  if (status != XML_STATUS_OK) {
    xml_failure(parser);
  }
  assert_true(testdata.count == 1); // first element should be done

  // ..and the start of the big token
  status = XML_Parse(parser, start, (int)strlen(start), XML_FALSE);
  if (status != XML_STATUS_OK) {
    xml_failure(parser);
  }
  assert_true(testdata.count == 1); // still just the first one

  // try to parse lots of 'e', but the token isn't finished
  for (int c = 0; c < 100; ++c) {
    status = XML_Parse(parser, eeeeee, fillsize, XML_FALSE);
    if (status != XML_STATUS_OK) {
      xml_failure(parser);
    }
  }
  assert_true(testdata.count == 1); // *still* just the first one

  // end the big token.
  status = XML_Parse(parser, end, (int)strlen(end), XML_FALSE);
  if (status != XML_STATUS_OK) {
    xml_failure(parser);
  }

  if (enabled) {
    // In general, we may need to push more data to trigger a reparse attempt,
    // but in this test, the data is constructed to always require it.
    assert_true(testdata.count == 1); // or the test is incorrect
    // 2x the token length should suffice; the +1 covers the start and end.
    for (int c = 0; c < 101; ++c) {
      status = XML_Parse(parser, spaces, fillsize, XML_FALSE);
      if (status != XML_STATUS_OK) {
        xml_failure(parser);
      }
    }
  }
  assert_true(testdata.count == 2); // the big token should be done

  // parse the final text
  status = XML_Parse(parser, post, (int)strlen(post), XML_TRUE);
  if (status != XML_STATUS_OK) {
    xml_failure(parser);
  }
  assert_true(testdata.count == 3); // after isFinal=XML_TRUE, all must be done

  XML_ParserFree(parser);
  return XML_STATUS_OK;
}

START_TEST(test_reparse_deferral_is_inherited) {
  const char *const text
      = "<!DOCTYPE document SYSTEM 'something.ext'><document/>";
  for (int enabled = 0; enabled <= 1; ++enabled) {
    set_subtest("deferral=%d", enabled);

    XML_Parser parser = XML_ParserCreate(NULL);
    assert_true(parser != NULL);
    XML_SetUserData(parser, (void *)&enabled);
    XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    // this handler creates a sub-parser and checks that its deferral behavior
    // is what we expected, based on the value of `enabled` (in userdata).
    XML_SetExternalEntityRefHandler(parser, external_inherited_parser);
    assert_true(XML_SetReparseDeferralEnabled(parser, enabled));
    if (XML_Parse(parser, text, (int)strlen(text), XML_TRUE) != XML_STATUS_OK)
      xml_failure(parser);

    XML_ParserFree(parser);
  }
}
END_TEST

START_TEST(test_set_reparse_deferral_on_null_parser) {
  assert_true(XML_SetReparseDeferralEnabled(NULL, 0) == XML_FALSE);
  assert_true(XML_SetReparseDeferralEnabled(NULL, 1) == XML_FALSE);
  assert_true(XML_SetReparseDeferralEnabled(NULL, 10) == XML_FALSE);
  assert_true(XML_SetReparseDeferralEnabled(NULL, 100) == XML_FALSE);
  assert_true(XML_SetReparseDeferralEnabled(NULL, (XML_Bool)INT_MIN)
              == XML_FALSE);
  assert_true(XML_SetReparseDeferralEnabled(NULL, (XML_Bool)INT_MAX)
              == XML_FALSE);
}
END_TEST

START_TEST(test_set_reparse_deferral_on_the_fly) {
  const char *const pre = "<d><x attr='";
  const char *const end = "'></x>";
  char iiiiii[100];
  const int fillsize = (int)sizeof(iiiiii);
  memset(iiiiii, 'i', fillsize);

  XML_Parser parser = XML_ParserCreate(NULL);
  assert_true(parser != NULL);
  assert_true(XML_SetReparseDeferralEnabled(parser, XML_TRUE));

  CharData storage;
  CharData_Init(&storage);
  XML_SetUserData(parser, &storage);
  XML_SetStartElementHandler(parser, start_element_event_handler);

  enum XML_Status status;
  // parse the start text
  status = XML_Parse(parser, pre, (int)strlen(pre), XML_FALSE);
  if (status != XML_STATUS_OK) {
    xml_failure(parser);
  }
  CharData_CheckXMLChars(&storage, XCS("d")); // first element should be done

  // try to parse some 'i', but the token isn't finished
  status = XML_Parse(parser, iiiiii, fillsize, XML_FALSE);
  if (status != XML_STATUS_OK) {
    xml_failure(parser);
  }
  CharData_CheckXMLChars(&storage, XCS("d")); // *still* just the first one

  // end the <x> token.
  status = XML_Parse(parser, end, (int)strlen(end), XML_FALSE);
  if (status != XML_STATUS_OK) {
    xml_failure(parser);
  }
  CharData_CheckXMLChars(&storage, XCS("d")); // not yet.

  // now change the heuristic setting and add *no* data
  assert_true(XML_SetReparseDeferralEnabled(parser, XML_FALSE));
  // we avoid isFinal=XML_TRUE, because that would force-bypass the heuristic.
  status = XML_Parse(parser, "", 0, XML_FALSE);
  if (status != XML_STATUS_OK) {
    xml_failure(parser);
  }
  CharData_CheckXMLChars(&storage, XCS("dx"));

  XML_ParserFree(parser);
}
END_TEST

START_TEST(test_set_bad_reparse_option) {
  XML_Parser parser = XML_ParserCreate(NULL);
  assert_true(XML_FALSE == XML_SetReparseDeferralEnabled(parser, 2));
  assert_true(XML_FALSE == XML_SetReparseDeferralEnabled(parser, 3));
  assert_true(XML_FALSE == XML_SetReparseDeferralEnabled(parser, 99));
  assert_true(XML_FALSE == XML_SetReparseDeferralEnabled(parser, 127));
  assert_true(XML_FALSE == XML_SetReparseDeferralEnabled(parser, 128));
  assert_true(XML_FALSE == XML_SetReparseDeferralEnabled(parser, 129));
  assert_true(XML_FALSE == XML_SetReparseDeferralEnabled(parser, 255));
  assert_true(XML_TRUE == XML_SetReparseDeferralEnabled(parser, 0));
  assert_true(XML_TRUE == XML_SetReparseDeferralEnabled(parser, 1));
  XML_ParserFree(parser);
}
END_TEST

static size_t g_totalAlloc = 0;
static size_t g_biggestAlloc = 0;

static void *
counting_realloc(void *ptr, size_t size) {
  g_totalAlloc += size;
  if (size > g_biggestAlloc) {
    g_biggestAlloc = size;
  }
  return realloc(ptr, size);
}

static void *
counting_malloc(size_t size) {
  return counting_realloc(NULL, size);
}

START_TEST(test_bypass_heuristic_when_close_to_bufsize) {
  if (g_chunkSize != 0) {
    // this test does not use SINGLE_BYTES, because it depends on very precise
    // buffer fills.
    return;
  }
  if (! g_reparseDeferralEnabledDefault) {
    return; // this test is irrelevant when the deferral heuristic is disabled.
  }

  const int document_length = 65536;
  char *const document = (char *)malloc(document_length);

  const XML_Memory_Handling_Suite memfuncs = {
      counting_malloc,
      counting_realloc,
      free,
  };

  const int leading_list[] = {0, 3, 61, 96, 400, 401, 4000, 4010, 4099, -1};
  const int bigtoken_list[] = {3000, 4000, 4001, 4096, 4099, 5000, 20000, -1};
  const int fillsize_list[] = {131, 256, 399, 400, 401, 1025, 4099, 4321, -1};

  for (const int *leading = leading_list; *leading >= 0; leading++) {
    for (const int *bigtoken = bigtoken_list; *bigtoken >= 0; bigtoken++) {
      for (const int *fillsize = fillsize_list; *fillsize >= 0; fillsize++) {
        set_subtest("leading=%d bigtoken=%d fillsize=%d", *leading, *bigtoken,
                    *fillsize);
        // start by checking that the test looks reasonably valid
        assert_true(*leading + *bigtoken <= document_length);

        // put 'x' everywhere; some will be overwritten by elements.
        memset(document, 'x', document_length);
        // maybe add an initial tag
        if (*leading) {
          assert_true(*leading >= 3); // or the test case is invalid
          memcpy(document, "<a>", 3);
        }
        // add the large token
        document[*leading + 0] = '<';
        document[*leading + 1] = 'b';
        memset(&document[*leading + 2], ' ', *bigtoken - 2); // a spacy token
        document[*leading + *bigtoken - 1] = '>';

        // 1 for 'b', plus 1 or 0 depending on the presence of 'a'
        const int expected_elem_total = 1 + (*leading ? 1 : 0);

        XML_Parser parser = XML_ParserCreate_MM(NULL, &memfuncs, NULL);
        assert_true(parser != NULL);

        CharData storage;
        CharData_Init(&storage);
        XML_SetUserData(parser, &storage);
        XML_SetStartElementHandler(parser, start_element_event_handler);

        g_biggestAlloc = 0;
        g_totalAlloc = 0;
        int offset = 0;
        // fill data until the big token is covered (but not necessarily parsed)
        while (offset < *leading + *bigtoken) {
          assert_true(offset + *fillsize <= document_length);
          const enum XML_Status status
              = XML_Parse(parser, &document[offset], *fillsize, XML_FALSE);
          if (status != XML_STATUS_OK) {
            xml_failure(parser);
          }
          offset += *fillsize;
        }
        // Now, check that we've had a buffer allocation that could fit the
        // context bytes and our big token. In order to detect a special case,
        // we need to know how many bytes of our big token were included in the
        // first push that contained _any_ bytes of the big token:
        const int bigtok_first_chunk_bytes = *fillsize - (*leading % *fillsize);
        if (bigtok_first_chunk_bytes >= *bigtoken && XML_CONTEXT_BYTES == 0) {
          // Special case: we aren't saving any context, and the whole big token
          // was covered by a single fill, so Expat may have parsed directly
          // from our input pointer, without allocating an internal buffer.
        } else if (*leading < XML_CONTEXT_BYTES) {
          assert_true(g_biggestAlloc >= *leading + (size_t)*bigtoken);
        } else {
          assert_true(g_biggestAlloc >= XML_CONTEXT_BYTES + (size_t)*bigtoken);
        }
        // fill data until the big token is actually parsed
        while (storage.count < expected_elem_total) {
          const size_t alloc_before = g_totalAlloc;
          assert_true(offset + *fillsize <= document_length);
          const enum XML_Status status
              = XML_Parse(parser, &document[offset], *fillsize, XML_FALSE);
          if (status != XML_STATUS_OK) {
            xml_failure(parser);
          }
          offset += *fillsize;
          // since all the bytes of the big token are already in the buffer,
          // the bufsize ceiling should make us finish its parsing without any
          // further buffer allocations. We assume that there will be no other
          // large allocations in this test.
          assert_true(g_totalAlloc - alloc_before < 4096);
        }
        // test-the-test: was our alloc even called?
        assert_true(g_totalAlloc > 0);
        // test-the-test: there shouldn't be any extra start elements
        assert_true(storage.count == expected_elem_total);

        XML_ParserFree(parser);
      }
    }
  }
  free(document);
}
END_TEST

#if defined(XML_TESTING)
START_TEST(test_varying_buffer_fills) {
  const int KiB = 1024;
  const int MiB = 1024 * KiB;
  const int document_length = 16 * MiB;
  const int big = 7654321; // arbitrarily chosen between 4 and 8 MiB

  if (g_chunkSize != 0) {
    return; // this test is slow, and doesn't use _XML_Parse_SINGLE_BYTES().
  }

  char *const document = (char *)malloc(document_length);
  assert_true(document != NULL);
  memset(document, 'x', document_length);
  document[0] = '<';
  document[1] = 't';
  memset(&document[2], ' ', big - 2); // a very spacy token
  document[big - 1] = '>';

  // Each testcase is a list of buffer fill sizes, terminated by a value < 0.
  // When reparse deferral is enabled, the final (negated) value is the expected
  // maximum number of bytes scanned in parse attempts.
  const int testcases[][30] = {
      {8 * MiB, -8 * MiB},
      {4 * MiB, 4 * MiB, -12 * MiB}, // try at 4MB, then 8MB = 12 MB total
      // zero-size fills shouldn't trigger the bypass
      {4 * MiB, 0, 4 * MiB, -12 * MiB},
      {4 * MiB, 0, 0, 4 * MiB, -12 * MiB},
      {4 * MiB, 0, 1 * MiB, 0, 3 * MiB, -12 * MiB},
      // try to hit the buffer ceiling only once (at the end)
      {4 * MiB, 2 * MiB, 1 * MiB, 512 * KiB, 256 * KiB, 256 * KiB, -12 * MiB},
      // try to hit the same buffer ceiling multiple times
      {4 * MiB + 1, 2 * MiB, 1 * MiB, 512 * KiB, -25 * MiB},

      // try to hit every ceiling, by always landing 1K shy of the buffer size
      {1 * KiB, 2 * KiB, 4 * KiB, 8 * KiB, 16 * KiB, 32 * KiB, 64 * KiB,
       128 * KiB, 256 * KiB, 512 * KiB, 1 * MiB, 2 * MiB, 4 * MiB, -16 * MiB},

      // try to avoid every ceiling, by always landing 1B past the buffer size
      // the normal 2x heuristic threshold still forces parse attempts.
      {2 * KiB + 1,          // will attempt 2KiB + 1 ==> total 2KiB + 1
       2 * KiB, 4 * KiB,     // will attempt 8KiB + 1 ==> total 10KiB + 2
       8 * KiB, 16 * KiB,    // will attempt 32KiB + 1 ==> total 42KiB + 3
       32 * KiB, 64 * KiB,   // will attempt 128KiB + 1 ==> total 170KiB + 4
       128 * KiB, 256 * KiB, // will attempt 512KiB + 1 ==> total 682KiB + 5
       512 * KiB, 1 * MiB,   // will attempt 2MiB + 1 ==> total 2M + 682K + 6
       2 * MiB, 4 * MiB,     // will attempt 8MiB + 1 ==> total 10M + 682K + 7
       -(10 * MiB + 682 * KiB + 7)},
      // try to avoid every ceiling again, except on our last fill.
      {2 * KiB + 1,          // will attempt 2KiB + 1 ==> total 2KiB + 1
       2 * KiB, 4 * KiB,     // will attempt 8KiB + 1 ==> total 10KiB + 2
       8 * KiB, 16 * KiB,    // will attempt 32KiB + 1 ==> total 42KiB + 3
       32 * KiB, 64 * KiB,   // will attempt 128KiB + 1 ==> total 170KiB + 4
       128 * KiB, 256 * KiB, // will attempt 512KiB + 1 ==> total 682KiB + 5
       512 * KiB, 1 * MiB,   // will attempt 2MiB + 1 ==> total 2M + 682K + 6
       2 * MiB, 4 * MiB - 1, // will attempt 8MiB ==> total 10M + 682K + 6
       -(10 * MiB + 682 * KiB + 6)},

      // try to hit ceilings on the way multiple times
      {512 * KiB + 1, 256 * KiB, 128 * KiB, 128 * KiB - 1, // 1 MiB buffer
       512 * KiB + 1, 256 * KiB, 128 * KiB, 128 * KiB - 1, // 2 MiB buffer
       1 * MiB + 1, 512 * KiB, 256 * KiB, 256 * KiB - 1,   // 4 MiB buffer
       2 * MiB + 1, 1 * MiB, 512 * KiB,                    // 8 MiB buffer
       // we'll make a parse attempt at every parse call
       -(45 * MiB + 12)},
  };
  const int testcount = sizeof(testcases) / sizeof(testcases[0]);
  for (int test_i = 0; test_i < testcount; test_i++) {
    const int *fillsize = testcases[test_i];
    set_subtest("#%d {%d %d %d %d ...}", test_i, fillsize[0], fillsize[1],
                fillsize[2], fillsize[3]);
    XML_Parser parser = XML_ParserCreate(NULL);
    assert_true(parser != NULL);

    CharData storage;
    CharData_Init(&storage);
    XML_SetUserData(parser, &storage);
    XML_SetStartElementHandler(parser, start_element_event_handler);

    g_bytesScanned = 0;
    int worstcase_bytes = 0; // sum of (buffered bytes at each XML_Parse call)
    int offset = 0;
    while (*fillsize >= 0) {
      assert_true(offset + *fillsize <= document_length); // or test is invalid
      const enum XML_Status status
          = XML_Parse(parser, &document[offset], *fillsize, XML_FALSE);
      if (status != XML_STATUS_OK) {
        xml_failure(parser);
      }
      offset += *fillsize;
      fillsize++;
      assert_true(offset <= INT_MAX - worstcase_bytes); // avoid overflow
      worstcase_bytes += offset; // we might've tried to parse all pending bytes
    }
    assert_true(storage.count == 1); // the big token should've been parsed
    assert_true(g_bytesScanned > 0); // test-the-test: does our counter work?
    if (g_reparseDeferralEnabledDefault) {
      // heuristic is enabled; some XML_Parse calls may have deferred reparsing
      const unsigned max_bytes_scanned = -*fillsize;
      if (g_bytesScanned > max_bytes_scanned) {
        fprintf(stderr,
                "bytes scanned in parse attempts: actual=%u limit=%u \n",
                g_bytesScanned, max_bytes_scanned);
        fail("too many bytes scanned in parse attempts");
      }
    }
    assert_true(g_bytesScanned <= (unsigned)worstcase_bytes);

    XML_ParserFree(parser);
  }
  free(document);
}
END_TEST
#endif

void
make_basic_test_case(Suite *s) {
  TCase *tc_basic = tcase_create("basic tests");

  suite_add_tcase(s, tc_basic);
  tcase_add_checked_fixture(tc_basic, basic_setup, basic_teardown);

  tcase_add_test(tc_basic, test_nul_byte);
  tcase_add_test(tc_basic, test_u0000_char);
  tcase_add_test(tc_basic, test_siphash_self);
  tcase_add_test(tc_basic, test_siphash_spec);
  tcase_add_test(tc_basic, test_bom_utf8);
  tcase_add_test(tc_basic, test_bom_utf16_be);
  tcase_add_test(tc_basic, test_bom_utf16_le);
  tcase_add_test(tc_basic, test_nobom_utf16_le);
  tcase_add_test(tc_basic, test_hash_collision);
  tcase_add_test(tc_basic, test_illegal_utf8);
  tcase_add_test(tc_basic, test_utf8_auto_align);
  tcase_add_test(tc_basic, test_utf16);
  tcase_add_test(tc_basic, test_utf16_le_epilog_newline);
  tcase_add_test(tc_basic, test_not_utf16);
  tcase_add_test(tc_basic, test_bad_encoding);
  tcase_add_test(tc_basic, test_latin1_umlauts);
  tcase_add_test(tc_basic, test_long_utf8_character);
  tcase_add_test(tc_basic, test_long_latin1_attribute);
  tcase_add_test(tc_basic, test_long_ascii_attribute);
  /* Regression test for SF bug #491986. */
  tcase_add_test(tc_basic, test_danish_latin1);
  /* Regression test for SF bug #514281. */
  tcase_add_test(tc_basic, test_french_charref_hexidecimal);
  tcase_add_test(tc_basic, test_french_charref_decimal);
  tcase_add_test(tc_basic, test_french_latin1);
  tcase_add_test(tc_basic, test_french_utf8);
  tcase_add_test(tc_basic, test_utf8_false_rejection);
  tcase_add_test(tc_basic, test_line_number_after_parse);
  tcase_add_test(tc_basic, test_column_number_after_parse);
  tcase_add_test(tc_basic, test_line_and_column_numbers_inside_handlers);
  tcase_add_test(tc_basic, test_line_number_after_error);
  tcase_add_test(tc_basic, test_column_number_after_error);
  tcase_add_test(tc_basic, test_really_long_lines);
  tcase_add_test(tc_basic, test_really_long_encoded_lines);
  tcase_add_test(tc_basic, test_end_element_events);
  tcase_add_test(tc_basic, test_helper_is_whitespace_normalized);
  tcase_add_test(tc_basic, test_attr_whitespace_normalization);
  tcase_add_test(tc_basic, test_xmldecl_misplaced);
  tcase_add_test(tc_basic, test_xmldecl_invalid);
  tcase_add_test(tc_basic, test_xmldecl_missing_attr);
  tcase_add_test(tc_basic, test_xmldecl_missing_value);
  tcase_add_test__if_xml_ge(tc_basic, test_unknown_encoding_internal_entity);
  tcase_add_test(tc_basic, test_unrecognised_encoding_internal_entity);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ext_entity_set_encoding);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ext_entity_no_handler);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ext_entity_set_bom);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ext_entity_bad_encoding);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ext_entity_bad_encoding_2);
  tcase_add_test(tc_basic, test_wfc_undeclared_entity_unread_external_subset);
  tcase_add_test(tc_basic, test_wfc_undeclared_entity_no_external_subset);
  tcase_add_test(tc_basic, test_wfc_undeclared_entity_standalone);
  tcase_add_test(tc_basic,
                 test_wfc_undeclared_entity_with_external_subset_standalone);
  tcase_add_test(tc_basic, test_entity_with_external_subset_unless_standalone);
  tcase_add_test(tc_basic, test_wfc_undeclared_entity_with_external_subset);
  tcase_add_test(tc_basic, test_not_standalone_handler_reject);
  tcase_add_test(tc_basic, test_not_standalone_handler_accept);
  tcase_add_test(tc_basic, test_entity_start_tag_level_greater_than_one);
  tcase_add_test__if_xml_ge(tc_basic, test_wfc_no_recursive_entity_refs);
  tcase_add_test(tc_basic, test_no_indirectly_recursive_entity_refs);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ext_entity_invalid_parse);
  tcase_add_test__if_xml_ge(tc_basic, test_dtd_default_handling);
  tcase_add_test(tc_basic, test_dtd_attr_handling);
  tcase_add_test(tc_basic, test_empty_ns_without_namespaces);
  tcase_add_test(tc_basic, test_ns_in_attribute_default_without_namespaces);
  tcase_add_test(tc_basic, test_stop_parser_between_char_data_calls);
  tcase_add_test(tc_basic, test_suspend_parser_between_char_data_calls);
  tcase_add_test(tc_basic, test_repeated_stop_parser_between_char_data_calls);
  tcase_add_test(tc_basic, test_good_cdata_ascii);
  tcase_add_test(tc_basic, test_good_cdata_utf16);
  tcase_add_test(tc_basic, test_good_cdata_utf16_le);
  tcase_add_test(tc_basic, test_long_cdata_utf16);
  tcase_add_test(tc_basic, test_multichar_cdata_utf16);
  tcase_add_test(tc_basic, test_utf16_bad_surrogate_pair);
  tcase_add_test(tc_basic, test_bad_cdata);
  tcase_add_test(tc_basic, test_bad_cdata_utf16);
  tcase_add_test(tc_basic, test_stop_parser_between_cdata_calls);
  tcase_add_test(tc_basic, test_suspend_parser_between_cdata_calls);
  tcase_add_test(tc_basic, test_memory_allocation);
  tcase_add_test__if_xml_ge(tc_basic, test_default_current);
  tcase_add_test(tc_basic, test_dtd_elements);
  tcase_add_test(tc_basic, test_dtd_elements_nesting);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_set_foreign_dtd);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_foreign_dtd_not_standalone);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_invalid_foreign_dtd);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_foreign_dtd_with_doctype);
  tcase_add_test__ifdef_xml_dtd(tc_basic,
                                test_foreign_dtd_without_external_subset);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_empty_foreign_dtd);
  tcase_add_test(tc_basic, test_set_base);
  tcase_add_test(tc_basic, test_attributes);
  tcase_add_test__if_xml_ge(tc_basic, test_reset_in_entity);
  tcase_add_test(tc_basic, test_resume_invalid_parse);
  tcase_add_test(tc_basic, test_resume_resuspended);
  tcase_add_test(tc_basic, test_cdata_default);
  tcase_add_test(tc_basic, test_subordinate_reset);
  tcase_add_test(tc_basic, test_subordinate_suspend);
  tcase_add_test__if_xml_ge(tc_basic, test_subordinate_xdecl_suspend);
  tcase_add_test__if_xml_ge(tc_basic, test_subordinate_xdecl_abort);
  tcase_add_test__ifdef_xml_dtd(tc_basic,
                                test_ext_entity_invalid_suspended_parse);
  tcase_add_test(tc_basic, test_explicit_encoding);
  tcase_add_test(tc_basic, test_trailing_cr);
  tcase_add_test__if_xml_ge(tc_basic, test_ext_entity_trailing_cr);
  tcase_add_test(tc_basic, test_trailing_rsqb);
  tcase_add_test__if_xml_ge(tc_basic, test_ext_entity_trailing_rsqb);
  tcase_add_test__if_xml_ge(tc_basic, test_ext_entity_good_cdata);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_user_parameters);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ext_entity_ref_parameter);
  tcase_add_test(tc_basic, test_empty_parse);
  tcase_add_test(tc_basic, test_negative_len_parse);
  tcase_add_test(tc_basic, test_negative_len_parse_buffer);
  tcase_add_test(tc_basic, test_get_buffer_1);
  tcase_add_test(tc_basic, test_get_buffer_2);
#if XML_CONTEXT_BYTES > 0
  tcase_add_test(tc_basic, test_get_buffer_3_overflow);
#endif
  tcase_add_test(tc_basic, test_buffer_can_grow_to_max);
  tcase_add_test(tc_basic, test_getbuffer_allocates_on_zero_len);
  tcase_add_test(tc_basic, test_byte_info_at_end);
  tcase_add_test(tc_basic, test_byte_info_at_error);
  tcase_add_test(tc_basic, test_byte_info_at_cdata);
  tcase_add_test(tc_basic, test_predefined_entities);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_invalid_tag_in_dtd);
  tcase_add_test(tc_basic, test_not_predefined_entities);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ignore_section);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ignore_section_utf16);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ignore_section_utf16_be);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_bad_ignore_section);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_external_bom_consumed);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_external_entity_values);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ext_entity_not_standalone);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_ext_entity_value_abort);
  tcase_add_test(tc_basic, test_bad_public_doctype);
  tcase_add_test(tc_basic, test_attribute_enum_value);
  tcase_add_test(tc_basic, test_predefined_entity_redefinition);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_dtd_stop_processing);
  tcase_add_test(tc_basic, test_public_notation_no_sysid);
  tcase_add_test(tc_basic, test_nested_groups);
  tcase_add_test(tc_basic, test_group_choice);
  tcase_add_test(tc_basic, test_standalone_parameter_entity);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_skipped_parameter_entity);
  tcase_add_test__ifdef_xml_dtd(tc_basic,
                                test_recursive_external_parameter_entity);
  tcase_add_test__ifdef_xml_dtd(tc_basic,
                                test_recursive_external_parameter_entity_2);
  tcase_add_test(tc_basic, test_undefined_ext_entity_in_external_dtd);
  tcase_add_test(tc_basic, test_suspend_xdecl);
  tcase_add_test(tc_basic, test_abort_epilog);
  tcase_add_test(tc_basic, test_abort_epilog_2);
  tcase_add_test(tc_basic, test_suspend_epilog);
  tcase_add_test(tc_basic, test_suspend_in_sole_empty_tag);
  tcase_add_test(tc_basic, test_unfinished_epilog);
  tcase_add_test(tc_basic, test_partial_char_in_epilog);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_suspend_resume_internal_entity);
  tcase_add_test__ifdef_xml_dtd(tc_basic,
                                test_suspend_resume_internal_entity_issue_629);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_resume_entity_with_syntax_error);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_suspend_resume_parameter_entity);
  tcase_add_test(tc_basic, test_restart_on_error);
  tcase_add_test(tc_basic, test_reject_lt_in_attribute_value);
  tcase_add_test(tc_basic, test_reject_unfinished_param_in_att_value);
  tcase_add_test(tc_basic, test_trailing_cr_in_att_value);
  tcase_add_test(tc_basic, test_standalone_internal_entity);
  tcase_add_test(tc_basic, test_skipped_external_entity);
  tcase_add_test(tc_basic, test_skipped_null_loaded_ext_entity);
  tcase_add_test(tc_basic, test_skipped_unloaded_ext_entity);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_param_entity_with_trailing_cr);
  tcase_add_test__if_xml_ge(tc_basic, test_invalid_character_entity);
  tcase_add_test__if_xml_ge(tc_basic, test_invalid_character_entity_2);
  tcase_add_test__if_xml_ge(tc_basic, test_invalid_character_entity_3);
  tcase_add_test__if_xml_ge(tc_basic, test_invalid_character_entity_4);
  tcase_add_test(tc_basic, test_pi_handled_in_default);
  tcase_add_test(tc_basic, test_comment_handled_in_default);
  tcase_add_test(tc_basic, test_pi_yml);
  tcase_add_test(tc_basic, test_pi_xnl);
  tcase_add_test(tc_basic, test_pi_xmm);
  tcase_add_test(tc_basic, test_utf16_pi);
  tcase_add_test(tc_basic, test_utf16_be_pi);
  tcase_add_test(tc_basic, test_utf16_be_comment);
  tcase_add_test(tc_basic, test_utf16_le_comment);
  tcase_add_test(tc_basic, test_missing_encoding_conversion_fn);
  tcase_add_test(tc_basic, test_failing_encoding_conversion_fn);
  tcase_add_test(tc_basic, test_unknown_encoding_success);
  tcase_add_test(tc_basic, test_unknown_encoding_bad_name);
  tcase_add_test(tc_basic, test_unknown_encoding_bad_name_2);
  tcase_add_test(tc_basic, test_unknown_encoding_long_name_1);
  tcase_add_test(tc_basic, test_unknown_encoding_long_name_2);
  tcase_add_test(tc_basic, test_invalid_unknown_encoding);
  tcase_add_test(tc_basic, test_unknown_ascii_encoding_ok);
  tcase_add_test(tc_basic, test_unknown_ascii_encoding_fail);
  tcase_add_test(tc_basic, test_unknown_encoding_invalid_length);
  tcase_add_test(tc_basic, test_unknown_encoding_invalid_topbit);
  tcase_add_test(tc_basic, test_unknown_encoding_invalid_surrogate);
  tcase_add_test(tc_basic, test_unknown_encoding_invalid_high);
  tcase_add_test(tc_basic, test_unknown_encoding_invalid_attr_value);
  tcase_add_test__if_xml_ge(tc_basic, test_ext_entity_latin1_utf16le_bom);
  tcase_add_test__if_xml_ge(tc_basic, test_ext_entity_latin1_utf16be_bom);
  tcase_add_test__if_xml_ge(tc_basic, test_ext_entity_latin1_utf16le_bom2);
  tcase_add_test__if_xml_ge(tc_basic, test_ext_entity_latin1_utf16be_bom2);
  tcase_add_test__if_xml_ge(tc_basic, test_ext_entity_utf16_be);
  tcase_add_test__if_xml_ge(tc_basic, test_ext_entity_utf16_le);
  tcase_add_test__if_xml_ge(tc_basic, test_ext_entity_utf16_unknown);
  tcase_add_test__if_xml_ge(tc_basic, test_ext_entity_utf8_non_bom);
  tcase_add_test(tc_basic, test_utf8_in_cdata_section);
  tcase_add_test(tc_basic, test_utf8_in_cdata_section_2);
  tcase_add_test(tc_basic, test_utf8_in_start_tags);
  tcase_add_test(tc_basic, test_trailing_spaces_in_elements);
  tcase_add_test(tc_basic, test_utf16_attribute);
  tcase_add_test(tc_basic, test_utf16_second_attr);
  tcase_add_test(tc_basic, test_attr_after_solidus);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_utf16_pe);
  tcase_add_test(tc_basic, test_bad_attr_desc_keyword);
  tcase_add_test(tc_basic, test_bad_attr_desc_keyword_utf16);
  tcase_add_test(tc_basic, test_bad_doctype);
  tcase_add_test(tc_basic, test_bad_doctype_utf8);
  tcase_add_test(tc_basic, test_bad_doctype_utf16);
  tcase_add_test(tc_basic, test_bad_doctype_plus);
  tcase_add_test(tc_basic, test_bad_doctype_star);
  tcase_add_test(tc_basic, test_bad_doctype_query);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_unknown_encoding_bad_ignore);
  tcase_add_test(tc_basic, test_entity_in_utf16_be_attr);
  tcase_add_test(tc_basic, test_entity_in_utf16_le_attr);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_entity_public_utf16_be);
  tcase_add_test__ifdef_xml_dtd(tc_basic, test_entity_public_utf16_le);
  tcase_add_test(tc_basic, test_short_doctype);
  tcase_add_test(tc_basic, test_short_doctype_2);
  tcase_add_test(tc_basic, test_short_doctype_3);
  tcase_add_test(tc_basic, test_long_doctype);
  tcase_add_test(tc_basic, test_bad_entity);
  tcase_add_test(tc_basic, test_bad_entity_2);
  tcase_add_test(tc_basic, test_bad_entity_3);
  tcase_add_test(tc_basic, test_bad_entity_4);
  tcase_add_test(tc_basic, test_bad_notation);
  tcase_add_test(tc_basic, test_default_doctype_handler);
  tcase_add_test(tc_basic, test_empty_element_abort);
  tcase_add_test__ifdef_xml_dtd(tc_basic,
                                test_pool_integrity_with_unfinished_attr);
  tcase_add_test__if_xml_ge(tc_basic, test_entity_ref_no_elements);
  tcase_add_test__if_xml_ge(tc_basic, test_deep_nested_entity);
  tcase_add_test__if_xml_ge(tc_basic, test_deep_nested_attribute_entity);
  tcase_add_test__if_xml_ge(tc_basic,
                            test_deep_nested_entity_delayed_interpretation);
  tcase_add_test__if_xml_ge(tc_basic, test_nested_entity_suspend);
  tcase_add_test__if_xml_ge(tc_basic, test_nested_entity_suspend_2);
#if defined(XML_TESTING)
  tcase_add_test(tc_basic, test_big_tokens_scale_linearly);
#endif
  tcase_add_test(tc_basic, test_set_reparse_deferral);
  tcase_add_test(tc_basic, test_reparse_deferral_is_inherited);
  tcase_add_test(tc_basic, test_set_reparse_deferral_on_null_parser);
  tcase_add_test(tc_basic, test_set_reparse_deferral_on_the_fly);
  tcase_add_test(tc_basic, test_set_bad_reparse_option);
  tcase_add_test(tc_basic, test_bypass_heuristic_when_close_to_bufsize);
#if defined(XML_TESTING)
  tcase_add_test(tc_basic, test_varying_buffer_fills);
#endif
}
