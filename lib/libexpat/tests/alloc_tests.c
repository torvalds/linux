/* Tests in the "allocation" test case for the Expat test suite
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
   Copyright (c) 2025      Berkay Eren Ürün <berkay.ueruen@siemens.com>
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

#include <math.h> /* NAN, INFINITY */
#include <stdbool.h>
#include <stdint.h> /* for SIZE_MAX */
#include <string.h>
#include <assert.h>

#include "expat_config.h"

#include "expat.h"
#include "internal.h"
#include "common.h"
#include "minicheck.h"
#include "dummy.h"
#include "handlers.h"
#include "alloc_tests.h"

static void
alloc_setup(void) {
  XML_Memory_Handling_Suite memsuite = {duff_allocator, duff_reallocator, free};

  /* Ensure the parser creation will go through */
  g_allocation_count = ALLOC_ALWAYS_SUCCEED;
  g_reallocation_count = REALLOC_ALWAYS_SUCCEED;
  g_parser = XML_ParserCreate_MM(NULL, &memsuite, NULL);
  if (g_parser == NULL)
    fail("Parser not created");
}

static void
alloc_teardown(void) {
  basic_teardown();
}

/* Test the effects of allocation failures on xml declaration processing */
START_TEST(test_alloc_parse_xdecl) {
  const char *text = "<?xml version='1.0' encoding='utf-8'?>\n"
                     "<doc>Hello, world</doc>";
  int i;
  const int max_alloc_count = 15;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetXmlDeclHandler(g_parser, dummy_xdecl_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* Resetting the parser is insufficient, because some memory
     * allocations are cached within the parser.  Instead we use
     * the teardown and setup routines to ensure that we have the
     * right sort of parser back in our hands.
     */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed with max allocations");
}
END_TEST

/* As above, but with an encoding big enough to cause storing the
 * version information to expand the string pool being used.
 */
START_TEST(test_alloc_parse_xdecl_2) {
  const char *text
      = "<?xml version='1.0' encoding='"
        /* Each line is 64 characters */
        "ThisIsAStupidlyLongEncodingNameIntendedToTriggerPoolGrowth123456"
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
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMN"
        "'?>"
        "<doc>Hello, world</doc>";
  int i;
  const int max_alloc_count = 20;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetXmlDeclHandler(g_parser, dummy_xdecl_handler);
    XML_SetUnknownEncodingHandler(g_parser, long_encoding_handler, NULL);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed with max allocations");
}
END_TEST

/* Test the effects of allocation failures on a straightforward parse */
START_TEST(test_alloc_parse_pi) {
  const char *text = "<?xml version='1.0' encoding='utf-8'?>\n"
                     "<?pi unknown?>\n"
                     "<doc>"
                     "Hello, world"
                     "</doc>";
  int i;
  const int max_alloc_count = 15;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetProcessingInstructionHandler(g_parser, dummy_pi_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed with max allocations");
}
END_TEST

START_TEST(test_alloc_parse_pi_2) {
  const char *text = "<?xml version='1.0' encoding='utf-8'?>\n"
                     "<doc>"
                     "Hello, world"
                     "<?pi unknown?>\n"
                     "</doc>";
  int i;
  const int max_alloc_count = 15;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetProcessingInstructionHandler(g_parser, dummy_pi_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed with max allocations");
}
END_TEST

START_TEST(test_alloc_parse_pi_3) {
  const char *text
      = "<?"
        /* 64 characters per line */
        "This processing instruction should be long enough to ensure that"
        "it triggers the growth of an internal string pool when the      "
        "allocator fails at a cruicial moment FGHIJKLMNOPABCDEFGHIJKLMNOP"
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
        "Q?><doc/>";
  int i;
  const int max_alloc_count = 20;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetProcessingInstructionHandler(g_parser, dummy_pi_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed with max allocations");
}
END_TEST

START_TEST(test_alloc_parse_comment) {
  const char *text = "<?xml version='1.0' encoding='utf-8'?>\n"
                     "<!-- Test parsing this comment -->"
                     "<doc>Hi</doc>";
  int i;
  const int max_alloc_count = 15;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetCommentHandler(g_parser, dummy_comment_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed with max allocations");
}
END_TEST

START_TEST(test_alloc_parse_comment_2) {
  const char *text = "<?xml version='1.0' encoding='utf-8'?>\n"
                     "<doc>"
                     "Hello, world"
                     "<!-- Parse this comment too -->"
                     "</doc>";
  int i;
  const int max_alloc_count = 15;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetCommentHandler(g_parser, dummy_comment_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed with max allocations");
}
END_TEST

/* Test that external parser creation running out of memory is
 * correctly reported.  Based on the external entity test cases.
 */
START_TEST(test_alloc_create_external_parser) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc>&entity;</doc>";
  char foo_text[] = "<!ELEMENT doc (#PCDATA)*>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetUserData(g_parser, foo_text);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_duff_loader);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR) {
    fail("External parser allocator returned success incorrectly");
  }
}
END_TEST

/* More external parser memory allocation testing */
START_TEST(test_alloc_run_external_parser) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>\n"
                     "<!DOCTYPE doc SYSTEM 'foo'>\n"
                     "<doc>&entity;</doc>";
  char foo_text[] = "<!ELEMENT doc (#PCDATA)*>";
  unsigned int i;
  const unsigned int max_alloc_count = 15;

  for (i = 0; i < max_alloc_count; i++) {
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetUserData(g_parser, foo_text);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_null_loader);
    g_allocation_count = (int)i;
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parsing ignored failing allocator");
  else if (i == max_alloc_count)
    fail("Parsing failed with allocation count 10");
}
END_TEST

/* Test that running out of memory in dtdCopy is correctly reported.
 * Based on test_default_ns_from_ext_subset_and_ext_ge()
 */
START_TEST(test_alloc_dtd_copy_default_atts) {
  const char *text = "<?xml version='1.0'?>\n"
                     "<!DOCTYPE doc SYSTEM 'http://example.org/doc.dtd' [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/entity.ent'>\n"
                     "]>\n"
                     "<doc xmlns='http://example.org/ns1'>\n"
                     "&en;\n"
                     "</doc>";
  int callno = 0;

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_dbl_handler);
  XML_SetUserData(g_parser, &callno);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test more external entity allocation failure paths */
START_TEST(test_alloc_external_entity) {
  const char *text = "<?xml version='1.0'?>\n"
                     "<!DOCTYPE doc SYSTEM 'http://example.org/doc.dtd' [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/entity.ent'>\n"
                     "]>\n"
                     "<doc xmlns='http://example.org/ns1'>\n"
                     "&en;\n"
                     "</doc>";
  int i;
  const int alloc_test_max_repeats = 50;
  int callno = 0;

  for (i = 0; i < alloc_test_max_repeats; i++) {
    g_allocation_count = -1;
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_dbl_handler_2);
    callno = 0;
    XML_SetUserData(g_parser, &callno);
    g_allocation_count = i;
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        == XML_STATUS_OK)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  g_allocation_count = -1;
  if (i == 0)
    fail("External entity parsed despite duff allocator");
  if (i == alloc_test_max_repeats)
    fail("External entity not parsed at max allocation count");
}
END_TEST

/* Test more allocation failure paths */
START_TEST(test_alloc_ext_entity_set_encoding) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  int i;
  const int max_allocation_count = 30;

  for (i = 0; i < max_allocation_count; i++) {
    XML_SetExternalEntityRefHandler(g_parser,
                                    external_entity_alloc_set_encoding);
    g_allocation_count = i;
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        == XML_STATUS_OK)
      break;
    g_allocation_count = -1;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Encoding check succeeded despite failing allocator");
  if (i == max_allocation_count)
    fail("Encoding failed at max allocation count");
}
END_TEST

/* Test the effects of allocation failure in internal entities.
 * Based on test_unknown_encoding_internal_entity
 */
START_TEST(test_alloc_internal_entity) {
  const char *text = "<?xml version='1.0' encoding='unsupported-encoding'?>\n"
                     "<!DOCTYPE test [<!ENTITY foo 'bar'>]>\n"
                     "<test a='&foo;'/>";
  unsigned int i;
  const unsigned int max_alloc_count = 20;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = (int)i;
    XML_SetUnknownEncodingHandler(g_parser, unknown_released_encoding_handler,
                                  NULL);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Internal entity worked despite failing allocations");
  else if (i == max_alloc_count)
    fail("Internal entity failed at max allocation count");
}
END_TEST

START_TEST(test_alloc_parameter_entity) {
  const char *text = "<!DOCTYPE foo ["
                     "<!ENTITY % param1 \"<!ENTITY internal 'some_text'>\">"
                     "%param1;"
                     "]> <foo>&internal;content</foo>";
  int i;
  const int alloc_test_max_repeats = 30;

  for (i = 0; i < alloc_test_max_repeats; i++) {
    g_allocation_count = i;
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    alloc_teardown();
    alloc_setup();
  }
  g_allocation_count = -1;
  if (i == 0)
    fail("Parameter entity processed despite duff allocator");
  if (i == alloc_test_max_repeats)
    fail("Parameter entity not processed at max allocation count");
}
END_TEST

/* Test the robustness against allocation failure of element handling
 * Based on test_dtd_default_handling().
 */
START_TEST(test_alloc_dtd_default_handling) {
  const char *text = "<!DOCTYPE doc [\n"
                     "<!ENTITY e SYSTEM 'http://example.org/e'>\n"
                     "<!NOTATION n SYSTEM 'http://example.org/n'>\n"
                     "<!ENTITY e1 SYSTEM 'http://example.org/e' NDATA n>\n"
                     "<!ELEMENT doc (#PCDATA)>\n"
                     "<!ATTLIST doc a CDATA #IMPLIED>\n"
                     "<?pi in dtd?>\n"
                     "<!--comment in dtd-->\n"
                     "]>\n"
                     "<doc><![CDATA[text in doc]]></doc>";
  const XML_Char *expected = XCS("\n\n\n\n\n\n\n\n\n<doc>text in doc</doc>");
  CharData storage;
  int i;
  const int max_alloc_count = 25;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    init_dummy_handlers();
    XML_SetDefaultHandler(g_parser, accumulate_characters);
    XML_SetDoctypeDeclHandler(g_parser, dummy_start_doctype_handler,
                              dummy_end_doctype_handler);
    XML_SetEntityDeclHandler(g_parser, dummy_entity_decl_handler);
    XML_SetNotationDeclHandler(g_parser, dummy_notation_decl_handler);
    XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
    XML_SetAttlistDeclHandler(g_parser, dummy_attlist_decl_handler);
    XML_SetProcessingInstructionHandler(g_parser, dummy_pi_handler);
    XML_SetCommentHandler(g_parser, dummy_comment_handler);
    XML_SetCdataSectionHandler(g_parser, dummy_start_cdata_handler,
                               dummy_end_cdata_handler);
    XML_SetUnparsedEntityDeclHandler(g_parser,
                                     dummy_unparsed_entity_decl_handler);
    CharData_Init(&storage);
    XML_SetUserData(g_parser, &storage);
    XML_SetCharacterDataHandler(g_parser, accumulate_characters);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Default DTD parsed despite allocation failures");
  if (i == max_alloc_count)
    fail("Default DTD not parsed with maximum alloc count");
  CharData_CheckXMLChars(&storage, expected);
  if (get_dummy_handler_flags()
      != (DUMMY_START_DOCTYPE_HANDLER_FLAG | DUMMY_END_DOCTYPE_HANDLER_FLAG
          | DUMMY_ENTITY_DECL_HANDLER_FLAG | DUMMY_NOTATION_DECL_HANDLER_FLAG
          | DUMMY_ELEMENT_DECL_HANDLER_FLAG | DUMMY_ATTLIST_DECL_HANDLER_FLAG
          | DUMMY_COMMENT_HANDLER_FLAG | DUMMY_PI_HANDLER_FLAG
          | DUMMY_START_CDATA_HANDLER_FLAG | DUMMY_END_CDATA_HANDLER_FLAG
          | DUMMY_UNPARSED_ENTITY_DECL_HANDLER_FLAG))
    fail("Not all handlers were called");
}
END_TEST

/* Test robustness of XML_SetEncoding() with a failing allocator */
START_TEST(test_alloc_explicit_encoding) {
  int i;
  const int max_alloc_count = 5;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    if (XML_SetEncoding(g_parser, XCS("us-ascii")) == XML_STATUS_OK)
      break;
  }
  if (i == 0)
    fail("Encoding set despite failing allocator");
  else if (i == max_alloc_count)
    fail("Encoding not set at max allocation count");
}
END_TEST

/* Test robustness of XML_SetBase against a failing allocator */
START_TEST(test_alloc_set_base) {
  const XML_Char *new_base = XCS("/local/file/name.xml");
  int i;
  const int max_alloc_count = 5;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    if (XML_SetBase(g_parser, new_base) == XML_STATUS_OK)
      break;
  }
  if (i == 0)
    fail("Base set despite failing allocator");
  else if (i == max_alloc_count)
    fail("Base not set with max allocation count");
}
END_TEST

/* Test buffer extension in the face of a duff reallocator */
START_TEST(test_alloc_realloc_buffer) {
  const char *text = get_buffer_test_text;
  void *buffer;
  int i;
  const int max_realloc_count = 10;

  /* Get a smallish buffer */
  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    buffer = XML_GetBuffer(g_parser, 1536);
    if (buffer == NULL)
      fail("1.5K buffer reallocation failed");
    assert(buffer != NULL);
    memcpy(buffer, text, strlen(text));
    if (XML_ParseBuffer(g_parser, (int)strlen(text), XML_FALSE)
        == XML_STATUS_OK)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  g_reallocation_count = -1;
  if (i == 0)
    fail("Parse succeeded with no reallocation");
  else if (i == max_realloc_count)
    fail("Parse failed with max reallocation count");
}
END_TEST

/* Same test for external entity parsers */
START_TEST(test_alloc_ext_entity_realloc_buffer) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/dummy.ent'>\n"
                     "]>\n"
                     "<doc>&en;</doc>";
  int i;
  const int max_realloc_count = 10;

  for (i = 0; i < max_realloc_count; i++) {
    XML_SetExternalEntityRefHandler(g_parser, external_entity_reallocator);
    XML_SetUserData(g_parser, &i);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        == XML_STATUS_OK)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Succeeded with no reallocations");
  if (i == max_realloc_count)
    fail("Failed with max reallocations");
}
END_TEST

/* Test elements with many attributes are handled correctly */
START_TEST(test_alloc_realloc_many_attributes) {
  const char *text = "<!DOCTYPE doc [\n"
                     "<!ATTLIST doc za CDATA 'default'>\n"
                     "<!ATTLIST doc zb CDATA 'def2'>\n"
                     "<!ATTLIST doc zc CDATA 'def3'>\n"
                     "]>\n"
                     "<doc a='1'"
                     "     b='2'"
                     "     c='3'"
                     "     d='4'"
                     "     e='5'"
                     "     f='6'"
                     "     g='7'"
                     "     h='8'"
                     "     i='9'"
                     "     j='10'"
                     "     k='11'"
                     "     l='12'"
                     "     m='13'"
                     "     n='14'"
                     "     p='15'"
                     "     q='16'"
                     "     r='17'"
                     "     s='18'>"
                     "</doc>";
  int i;
  const int max_realloc_count = 10;

  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite no reallocations");
  if (i == max_realloc_count)
    fail("Parse failed at max reallocations");
}
END_TEST

/* Test handling of a public entity with failing allocator */
START_TEST(test_alloc_public_entity_value) {
  const char *text = "<!DOCTYPE doc SYSTEM 'http://example.org/'>\n"
                     "<doc></doc>\n";
  char dtd_text[]
      = "<!ELEMENT doc EMPTY>\n"
        "<!ENTITY % e1 PUBLIC 'foo' 'bar.ent'>\n"
        "<!ENTITY % "
        /* Each line is 64 characters */
        "ThisIsAStupidlyLongParameterNameIntendedToTriggerPoolGrowth12345"
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
        " '%e1;'>\n"
        "%e1;\n";
  int i;
  const int max_alloc_count = 50;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    init_dummy_handlers();
    XML_SetUserData(g_parser, dtd_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_public);
    /* Provoke a particular code path */
    XML_SetEntityDeclHandler(g_parser, dummy_entity_decl_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parsing worked despite failing allocation");
  if (i == max_alloc_count)
    fail("Parsing failed at max allocation count");
  if (get_dummy_handler_flags() != DUMMY_ENTITY_DECL_HANDLER_FLAG)
    fail("Entity declaration handler not called");
}
END_TEST

START_TEST(test_alloc_realloc_subst_public_entity_value) {
  const char *text = "<!DOCTYPE doc SYSTEM 'http://example.org/'>\n"
                     "<doc></doc>\n";
  char dtd_text[]
      = "<!ELEMENT doc EMPTY>\n"
        "<!ENTITY % "
        /* Each line is 64 characters */
        "ThisIsAStupidlyLongParameterNameIntendedToTriggerPoolGrowth12345"
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
        " PUBLIC 'foo' 'bar.ent'>\n"
        "%ThisIsAStupidlyLongParameterNameIntendedToTriggerPoolGrowth12345"
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
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP;";
  int i;
  const int max_realloc_count = 10;

  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    XML_SetUserData(g_parser, dtd_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_public);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parsing worked despite failing reallocation");
  if (i == max_realloc_count)
    fail("Parsing failed at max reallocation count");
}
END_TEST

START_TEST(test_alloc_parse_public_doctype) {
  const char *text
      = "<?xml version='1.0' encoding='utf-8'?>\n"
        "<!DOCTYPE doc PUBLIC '"
        /* 64 characters per line */
        "http://example.com/a/long/enough/name/to/trigger/pool/growth/zz/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "' 'test'>\n"
        "<doc></doc>";
  int i;
  const int max_alloc_count = 25;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    init_dummy_handlers();
    XML_SetDoctypeDeclHandler(g_parser, dummy_start_doctype_decl_handler,
                              dummy_end_doctype_decl_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
  if (get_dummy_handler_flags()
      != (DUMMY_START_DOCTYPE_DECL_HANDLER_FLAG
          | DUMMY_END_DOCTYPE_DECL_HANDLER_FLAG))
    fail("Doctype handler functions not called");
}
END_TEST

START_TEST(test_alloc_parse_public_doctype_long_name) {
  const char *text
      = "<?xml version='1.0' encoding='utf-8'?>\n"
        "<!DOCTYPE doc PUBLIC 'http://example.com/foo' '"
        /* 64 characters per line */
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNOP"
        "'>\n"
        "<doc></doc>";
  int i;
  const int max_alloc_count = 25;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetDoctypeDeclHandler(g_parser, dummy_start_doctype_decl_handler,
                              dummy_end_doctype_decl_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
}
END_TEST

/* Test foreign DTD handling */
START_TEST(test_alloc_set_foreign_dtd) {
  const char *text1 = "<?xml version='1.0' encoding='us-ascii'?>\n"
                      "<doc>&entity;</doc>";
  char text2[] = "<!ELEMENT doc (#PCDATA)*>";
  int i;
  const int max_alloc_count = 25;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetUserData(g_parser, &text2);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_alloc);
    if (XML_UseForeignDTD(g_parser, XML_TRUE) != XML_ERROR_NONE)
      fail("Could not set foreign DTD");
    if (_XML_Parse_SINGLE_BYTES(g_parser, text1, (int)strlen(text1), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
}
END_TEST

/* Test based on ibm/valid/P32/ibm32v04.xml */
START_TEST(test_alloc_attribute_enum_value) {
  const char *text = "<?xml version='1.0' standalone='no'?>\n"
                     "<!DOCTYPE animal SYSTEM 'test.dtd'>\n"
                     "<animal>This is a \n    <a/>  \n\nyellow tiger</animal>";
  char dtd_text[] = "<!ELEMENT animal (#PCDATA|a)*>\n"
                    "<!ELEMENT a EMPTY>\n"
                    "<!ATTLIST animal xml:space (default|preserve) 'preserve'>";
  int i;
  const int max_alloc_count = 30;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetExternalEntityRefHandler(g_parser, external_entity_alloc);
    XML_SetUserData(g_parser, dtd_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    /* An attribute list handler provokes a different code path */
    XML_SetAttlistDeclHandler(g_parser, dummy_attlist_decl_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
}
END_TEST

/* Test attribute enums sufficient to overflow the string pool */
START_TEST(test_alloc_realloc_attribute_enum_value) {
  const char *text = "<?xml version='1.0' standalone='no'?>\n"
                     "<!DOCTYPE animal SYSTEM 'test.dtd'>\n"
                     "<animal>This is a yellow tiger</animal>";
  /* We wish to define a collection of attribute enums that will
   * cause the string pool storing them to have to expand.  This
   * means more than 1024 bytes, including the parentheses and
   * separator bars.
   */
  char dtd_text[]
      = "<!ELEMENT animal (#PCDATA)*>\n"
        "<!ATTLIST animal thing "
        "(default"
        /* Each line is 64 characters */
        "|ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|BBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|CBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|DBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|EBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|FBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|GBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|HBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|IBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|JBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|KBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|LBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|MBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|NBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|OBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|PBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO)"
        " 'default'>";
  int i;
  const int max_realloc_count = 10;

  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    XML_SetExternalEntityRefHandler(g_parser, external_entity_alloc);
    XML_SetUserData(g_parser, dtd_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    /* An attribute list handler provokes a different code path */
    XML_SetAttlistDeclHandler(g_parser, dummy_attlist_decl_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing reallocator");
  if (i == max_realloc_count)
    fail("Parse failed at maximum reallocation count");
}
END_TEST

/* Test attribute enums in a #IMPLIED attribute forcing pool growth */
START_TEST(test_alloc_realloc_implied_attribute) {
  /* Forcing this particular code path is a balancing act.  The
   * addition of the closing parenthesis and terminal NUL must be
   * what pushes the string of enums over the 1024-byte limit,
   * otherwise a different code path will pick up the realloc.
   */
  const char *text
      = "<!DOCTYPE doc [\n"
        "<!ELEMENT doc EMPTY>\n"
        "<!ATTLIST doc a "
        /* Each line is 64 characters */
        "(ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|BBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|CBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|DBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|EBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|FBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|GBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|HBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|IBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|JBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|KBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|LBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|MBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|NBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|OBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|PBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMN)"
        " #IMPLIED>\n"
        "]><doc/>";
  int i;
  const int max_realloc_count = 10;

  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    XML_SetAttlistDeclHandler(g_parser, dummy_attlist_decl_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing reallocator");
  if (i == max_realloc_count)
    fail("Parse failed at maximum reallocation count");
}
END_TEST

/* Test attribute enums in a defaulted attribute forcing pool growth */
START_TEST(test_alloc_realloc_default_attribute) {
  /* Forcing this particular code path is a balancing act.  The
   * addition of the closing parenthesis and terminal NUL must be
   * what pushes the string of enums over the 1024-byte limit,
   * otherwise a different code path will pick up the realloc.
   */
  const char *text
      = "<!DOCTYPE doc [\n"
        "<!ELEMENT doc EMPTY>\n"
        "<!ATTLIST doc a "
        /* Each line is 64 characters */
        "(ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|BBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|CBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|DBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|EBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|FBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|GBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|HBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|IBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|JBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|KBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|LBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|MBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|NBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|OBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO"
        "|PBCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMN)"
        " 'ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNO'"
        ">\n]><doc/>";
  int i;
  const int max_realloc_count = 10;

  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    XML_SetAttlistDeclHandler(g_parser, dummy_attlist_decl_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing reallocator");
  if (i == max_realloc_count)
    fail("Parse failed at maximum reallocation count");
}
END_TEST

/* Test long notation name with dodgy allocator */
START_TEST(test_alloc_notation) {
  const char *text
      = "<!DOCTYPE doc [\n"
        "<!NOTATION "
        /* Each line is 64 characters */
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
        " SYSTEM 'http://example.org/n'>\n"
        "<!ENTITY e SYSTEM 'http://example.org/e' NDATA "
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
        ">\n"
        "<!ELEMENT doc EMPTY>\n"
        "]>\n<doc/>";
  int i;
  const int max_alloc_count = 20;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    init_dummy_handlers();
    XML_SetNotationDeclHandler(g_parser, dummy_notation_decl_handler);
    XML_SetEntityDeclHandler(g_parser, dummy_entity_decl_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite allocation failures");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
  if (get_dummy_handler_flags()
      != (DUMMY_ENTITY_DECL_HANDLER_FLAG | DUMMY_NOTATION_DECL_HANDLER_FLAG))
    fail("Entity declaration handler not called");
}
END_TEST

/* Test public notation with dodgy allocator */
START_TEST(test_alloc_public_notation) {
  const char *text
      = "<!DOCTYPE doc [\n"
        "<!NOTATION note PUBLIC '"
        /* 64 characters per line */
        "http://example.com/a/long/enough/name/to/trigger/pool/growth/zz/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "' 'foo'>\n"
        "<!ENTITY e SYSTEM 'http://example.com/e' NDATA note>\n"
        "<!ELEMENT doc EMPTY>\n"
        "]>\n<doc/>";
  int i;
  const int max_alloc_count = 20;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    init_dummy_handlers();
    XML_SetNotationDeclHandler(g_parser, dummy_notation_decl_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite allocation failures");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
  if (get_dummy_handler_flags() != DUMMY_NOTATION_DECL_HANDLER_FLAG)
    fail("Notation handler not called");
}
END_TEST

/* Test public notation with dodgy allocator */
START_TEST(test_alloc_system_notation) {
  const char *text
      = "<!DOCTYPE doc [\n"
        "<!NOTATION note SYSTEM '"
        /* 64 characters per line */
        "http://example.com/a/long/enough/name/to/trigger/pool/growth/zz/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/ABCDEFGHIJKLMNO/"
        "'>\n"
        "<!ENTITY e SYSTEM 'http://example.com/e' NDATA note>\n"
        "<!ELEMENT doc EMPTY>\n"
        "]>\n<doc/>";
  int i;
  const int max_alloc_count = 20;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    init_dummy_handlers();
    XML_SetNotationDeclHandler(g_parser, dummy_notation_decl_handler);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite allocation failures");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
  if (get_dummy_handler_flags() != DUMMY_NOTATION_DECL_HANDLER_FLAG)
    fail("Notation handler not called");
}
END_TEST

START_TEST(test_alloc_nested_groups) {
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
  int i;
  const int max_alloc_count = 20;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    CharData_Init(&storage);
    XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
    XML_SetStartElementHandler(g_parser, record_element_start_handler);
    XML_SetUserData(g_parser, &storage);
    init_dummy_handlers();
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }

  if (i == 0)
    fail("Parse succeeded despite failing reallocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum reallocation count");
  CharData_CheckXMLChars(&storage, XCS("doce"));
  if (get_dummy_handler_flags() != DUMMY_ELEMENT_DECL_HANDLER_FLAG)
    fail("Element handler not fired");
}
END_TEST

START_TEST(test_alloc_realloc_nested_groups) {
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
  int i;
  const int max_realloc_count = 10;

  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    CharData_Init(&storage);
    XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
    XML_SetStartElementHandler(g_parser, record_element_start_handler);
    XML_SetUserData(g_parser, &storage);
    init_dummy_handlers();
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }

  if (i == 0)
    fail("Parse succeeded despite failing reallocator");
  if (i == max_realloc_count)
    fail("Parse failed at maximum reallocation count");
  CharData_CheckXMLChars(&storage, XCS("doce"));
  if (get_dummy_handler_flags() != DUMMY_ELEMENT_DECL_HANDLER_FLAG)
    fail("Element handler not fired");
}
END_TEST

START_TEST(test_alloc_large_group) {
  const char *text = "<!DOCTYPE doc [\n"
                     "<!ELEMENT doc ("
                     "a1|a2|a3|a4|a5|a6|a7|a8|"
                     "b1|b2|b3|b4|b5|b6|b7|b8|"
                     "c1|c2|c3|c4|c5|c6|c7|c8|"
                     "d1|d2|d3|d4|d5|d6|d7|d8|"
                     "e1"
                     ")+>\n"
                     "]>\n"
                     "<doc>\n"
                     "<a1/>\n"
                     "</doc>\n";
  int i;
  const int max_alloc_count = 50;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
    init_dummy_handlers();
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
  if (get_dummy_handler_flags() != DUMMY_ELEMENT_DECL_HANDLER_FLAG)
    fail("Element handler flag not raised");
}
END_TEST

START_TEST(test_alloc_realloc_group_choice) {
  const char *text = "<!DOCTYPE doc [\n"
                     "<!ELEMENT doc ("
                     "a1|a2|a3|a4|a5|a6|a7|a8|"
                     "b1|b2|b3|b4|b5|b6|b7|b8|"
                     "c1|c2|c3|c4|c5|c6|c7|c8|"
                     "d1|d2|d3|d4|d5|d6|d7|d8|"
                     "e1"
                     ")+>\n"
                     "]>\n"
                     "<doc>\n"
                     "<a1/>\n"
                     "<b2 attr='foo'>This is a foo</b2>\n"
                     "<c3></c3>\n"
                     "</doc>\n";
  int i;
  const int max_realloc_count = 10;

  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    XML_SetElementDeclHandler(g_parser, dummy_element_decl_handler);
    init_dummy_handlers();
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing reallocator");
  if (i == max_realloc_count)
    fail("Parse failed at maximum reallocation count");
  if (get_dummy_handler_flags() != DUMMY_ELEMENT_DECL_HANDLER_FLAG)
    fail("Element handler flag not raised");
}
END_TEST

START_TEST(test_alloc_pi_in_epilog) {
  const char *text = "<doc></doc>\n"
                     "<?pi in epilog?>";
  int i;
  const int max_alloc_count = 15;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetProcessingInstructionHandler(g_parser, dummy_pi_handler);
    init_dummy_handlers();
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse completed despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
  if (get_dummy_handler_flags() != DUMMY_PI_HANDLER_FLAG)
    fail("Processing instruction handler not invoked");
}
END_TEST

START_TEST(test_alloc_comment_in_epilog) {
  const char *text = "<doc></doc>\n"
                     "<!-- comment in epilog -->";
  int i;
  const int max_alloc_count = 15;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetCommentHandler(g_parser, dummy_comment_handler);
    init_dummy_handlers();
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse completed despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
  if (get_dummy_handler_flags() != DUMMY_COMMENT_HANDLER_FLAG)
    fail("Processing instruction handler not invoked");
}
END_TEST

START_TEST(test_alloc_realloc_long_attribute_value) {
  const char *text
      = "<!DOCTYPE doc [<!ENTITY foo '"
        /* Each line is 64 characters */
        "This entity will be substituted as an attribute value, and is   "
        "calculated to be exactly long enough that the terminating NUL   "
        "that the library adds internally will trigger the string pool to"
        "grow. GHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
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
        "'>]>\n"
        "<doc a='&foo;'></doc>";
  int i;
  const int max_realloc_count = 10;

  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing reallocator");
  if (i == max_realloc_count)
    fail("Parse failed at maximum reallocation count");
}
END_TEST

START_TEST(test_alloc_attribute_whitespace) {
  const char *text = "<doc a=' '></doc>";
  int i;
  const int max_alloc_count = 15;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
}
END_TEST

START_TEST(test_alloc_attribute_predefined_entity) {
  const char *text = "<doc a='&amp;'></doc>";
  int i;
  const int max_alloc_count = 15;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
}
END_TEST

/* Test that a character reference at the end of a suitably long
 * default value for an attribute can trigger pool growth, and recovers
 * if the allocator fails on it.
 */
START_TEST(test_alloc_long_attr_default_with_char_ref) {
  const char *text
      = "<!DOCTYPE doc [<!ATTLIST doc a CDATA '"
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
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHI"
        "&#x31;'>]>\n"
        "<doc/>";
  int i;
  const int max_alloc_count = 20;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
}
END_TEST

/* Test that a long character reference substitution triggers a pool
 * expansion correctly for an attribute value.
 */
START_TEST(test_alloc_long_attr_value) {
  const char *text
      = "<!DOCTYPE test [<!ENTITY foo '\n"
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
        "'>]>\n"
        "<test a='&foo;'/>";
  int i;
  const int max_alloc_count = 25;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing allocator");
  if (i == max_alloc_count)
    fail("Parse failed at maximum allocation count");
}
END_TEST

/* Test that an error in a nested parameter entity substitution is
 * handled correctly.  It seems unlikely that the code path being
 * exercised can be reached purely by carefully crafted XML, but an
 * allocation error in the right place will definitely do it.
 */
START_TEST(test_alloc_nested_entities) {
  const char *text = "<!DOCTYPE doc SYSTEM 'http://example.org/one.ent'>\n"
                     "<doc />";
  ExtFaults test_data
      = {"<!ENTITY % pe1 '"
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
         "'>\n"
         "<!ENTITY % pe2 '%pe1;'>\n"
         "<!ENTITY % pe3 '%pe2;'>",
         "Memory Fail not faulted", NULL, XML_ERROR_NO_MEMORY};

  /* Causes an allocation error in a nested storeEntityValue() */
  g_allocation_count = 12;
  XML_SetUserData(g_parser, &test_data);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_faulter);
  expect_failure(text, XML_ERROR_EXTERNAL_ENTITY_HANDLING,
                 "Entity allocation failure not noted");
}
END_TEST

START_TEST(test_alloc_realloc_param_entity_newline) {
  const char *text = "<!DOCTYPE doc SYSTEM 'http://example.org/'>\n"
                     "<doc/>";
  char dtd_text[]
      = "<!ENTITY % pe '<!ATTLIST doc att CDATA \""
        /* 64 characters per line */
        "This default value is carefully crafted so that the carriage    "
        "return right at the end of the entity string causes an internal "
        "string pool to have to grow.  This allows us to test the alloc  "
        "failure path from that point. OPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
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
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDE"
        "\">\n'>"
        "%pe;\n";
  int i;
  const int max_realloc_count = 5;

  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    XML_SetUserData(g_parser, dtd_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_alloc);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing reallocator");
  if (i == max_realloc_count)
    fail("Parse failed at maximum reallocation count");
}
END_TEST

START_TEST(test_alloc_realloc_ce_extends_pe) {
  const char *text = "<!DOCTYPE doc SYSTEM 'http://example.org/'>\n"
                     "<doc/>";
  char dtd_text[]
      = "<!ENTITY % pe '<!ATTLIST doc att CDATA \""
        /* 64 characters per line */
        "This default value is carefully crafted so that the character   "
        "entity at the end causes an internal string pool to have to     "
        "grow.  This allows us to test the allocation failure path from  "
        "that point onwards. EFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
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
        "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFG&#x51;"
        "\">\n'>"
        "%pe;\n";
  int i;
  const int max_realloc_count = 5;

  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    XML_SetUserData(g_parser, dtd_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_alloc);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parse succeeded despite failing reallocator");
  if (i == max_realloc_count)
    fail("Parse failed at maximum reallocation count");
}
END_TEST

START_TEST(test_alloc_realloc_attributes) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ATTLIST doc\n"
                     "    a1  (a|b|c)   'a'\n"
                     "    a2  (foo|bar) #IMPLIED\n"
                     "    a3  NMTOKEN   #IMPLIED\n"
                     "    a4  NMTOKENS  #IMPLIED\n"
                     "    a5  ID        #IMPLIED\n"
                     "    a6  IDREF     #IMPLIED\n"
                     "    a7  IDREFS    #IMPLIED\n"
                     "    a8  ENTITY    #IMPLIED\n"
                     "    a9  ENTITIES  #IMPLIED\n"
                     "    a10 CDATA     #IMPLIED\n"
                     "  >]>\n"
                     "<doc>wombat</doc>\n";
  int i;
  const int max_realloc_count = 5;

  for (i = 0; i < max_realloc_count; i++) {
    g_reallocation_count = i;
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }

  if (i == 0)
    fail("Parse succeeded despite failing reallocator");
  if (i == max_realloc_count)
    fail("Parse failed at maximum reallocation count");
}
END_TEST

START_TEST(test_alloc_long_doc_name) {
  const char *text =
      /* 64 characters per line */
      "<LongRootElementNameThatWillCauseTheNextAllocationToExpandTheStr"
      "ingPoolForTheDTDQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AZ"
      " a='1'/>";
  int i;
  const int max_alloc_count = 20;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parsing worked despite failing reallocations");
  else if (i == max_alloc_count)
    fail("Parsing failed even at max reallocation count");
}
END_TEST

START_TEST(test_alloc_long_base) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ENTITY e SYSTEM 'foo'>\n"
                     "]>\n"
                     "<doc>&e;</doc>";
  char entity_text[] = "Hello world";
  const XML_Char *base =
      /* 64 characters per line */
      /* clang-format off */
        XCS("LongBaseURI/that/will/overflow/an/internal/buffer/and/cause/it/t")
        XCS("o/have/to/grow/PQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/")
        XCS("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789A/");
  /* clang-format on */
  int i;
  const int max_alloc_count = 25;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetUserData(g_parser, entity_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_alloc);
    if (XML_SetBase(g_parser, base) == XML_STATUS_ERROR) {
      XML_ParserReset(g_parser, NULL);
      continue;
    }
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parsing worked despite failing allocations");
  else if (i == max_alloc_count)
    fail("Parsing failed even at max allocation count");
}
END_TEST

START_TEST(test_alloc_long_public_id) {
  const char *text
      = "<!DOCTYPE doc [\n"
        "  <!ENTITY e PUBLIC '"
        /* 64 characters per line */
        "LongPublicIDThatShouldResultInAnInternalStringPoolGrowingAtASpec"
        "ificMomentKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "' 'bar'>\n"
        "]>\n"
        "<doc>&e;</doc>";
  char entity_text[] = "Hello world";
  int i;
  const int max_alloc_count = 40;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetUserData(g_parser, entity_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_alloc);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parsing worked despite failing allocations");
  else if (i == max_alloc_count)
    fail("Parsing failed even at max allocation count");
}
END_TEST

START_TEST(test_alloc_long_entity_value) {
  const char *text
      = "<!DOCTYPE doc [\n"
        "  <!ENTITY e1 '"
        /* 64 characters per line */
        "Long entity value that should provoke a string pool to grow whil"
        "e setting up to parse the external entity below. xyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "'>\n"
        "  <!ENTITY e2 SYSTEM 'bar'>\n"
        "]>\n"
        "<doc>&e2;</doc>";
  char entity_text[] = "Hello world";
  int i;
  const int max_alloc_count = 40;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetUserData(g_parser, entity_text);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_alloc);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;
    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parsing worked despite failing allocations");
  else if (i == max_alloc_count)
    fail("Parsing failed even at max allocation count");
}
END_TEST

START_TEST(test_alloc_long_notation) {
  const char *text
      = "<!DOCTYPE doc [\n"
        "  <!NOTATION note SYSTEM '"
        /* 64 characters per line */
        "ALongNotationNameThatShouldProvokeStringPoolGrowthWhileCallingAn"
        "ExternalEntityParserUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "'>\n"
        "  <!ENTITY e1 SYSTEM 'foo' NDATA "
        /* 64 characters per line */
        "ALongNotationNameThatShouldProvokeStringPoolGrowthWhileCallingAn"
        "ExternalEntityParserUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789AB"
        ">\n"
        "  <!ENTITY e2 SYSTEM 'bar'>\n"
        "]>\n"
        "<doc>&e2;</doc>";
  ExtOption options[]
      = {{XCS("foo"), "Entity Foo"}, {XCS("bar"), "Entity Bar"}, {NULL, NULL}};
  int i;
  const int max_alloc_count = 40;

  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = i;
    XML_SetUserData(g_parser, options);
    XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetExternalEntityRefHandler(g_parser, external_entity_optioner);
    if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
        != XML_STATUS_ERROR)
      break;

    /* See comment in test_alloc_parse_xdecl() */
    alloc_teardown();
    alloc_setup();
  }
  if (i == 0)
    fail("Parsing worked despite failing allocations");
  else if (i == max_alloc_count)
    fail("Parsing failed even at max allocation count");
}
END_TEST

START_TEST(test_alloc_reset_after_external_entity_parser_create_fail) {
  const char *const text = "<!DOCTYPE doc SYSTEM 'foo'><doc/>";

  XML_SetExternalEntityRefHandler(
      g_parser, external_entity_parser_create_alloc_fail_handler);
  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Call to parse was expected to fail");

  if (XML_GetErrorCode(g_parser) != XML_ERROR_EXTERNAL_ENTITY_HANDLING)
    fail("Call to parse was expected to fail from the external entity handler");

  XML_ParserReset(g_parser, NULL);
}
END_TEST

#if defined(XML_TESTING)
START_TEST(test_alloc_tracker_size_recorded) {
  XML_Memory_Handling_Suite memsuite = {malloc, realloc, free};

  bool values[] = {true, false};
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    const bool useMemSuite = values[i];
    set_subtest("useMemSuite=%d", (int)useMemSuite);
    XML_Parser parser = useMemSuite
                            ? XML_ParserCreate_MM(NULL, &memsuite, XCS("|"))
                            : XML_ParserCreate(NULL);

#if XML_GE == 1
    void *ptr = expat_malloc(parser, 10, -1);

    assert_true(ptr != NULL);
    assert_true(*((size_t *)ptr - 1) == 10);

    assert_true(expat_realloc(parser, ptr, SIZE_MAX / 2, -1) == NULL);

    assert_true(*((size_t *)ptr - 1) == 10); // i.e. unchanged

    ptr = expat_realloc(parser, ptr, 20, -1);

    assert_true(ptr != NULL);
    assert_true(*((size_t *)ptr - 1) == 20);

    expat_free(parser, ptr, -1);
#endif

    XML_ParserFree(parser);
  }
}
END_TEST

START_TEST(test_alloc_tracker_maximum_amplification) {
  if (g_reparseDeferralEnabledDefault == XML_TRUE) {
    return;
  }

  XML_Parser parser = XML_ParserCreate(NULL);

  // Get .m_accounting.countBytesDirect from 0 to 3
  const char *const chunk = "<e>";
  assert_true(_XML_Parse_SINGLE_BYTES(parser, chunk, (int)strlen(chunk),
                                      /*isFinal=*/XML_FALSE)
              == XML_STATUS_OK);

#if XML_GE == 1
  // Stop activation threshold from interfering
  assert_true(XML_SetAllocTrackerActivationThreshold(parser, 0) == XML_TRUE);

  // Exceed maximum amplification: should be rejected.
  assert_true(expat_malloc(parser, 1000, -1) == NULL);

  // Increase maximum amplification, and try the same amount once more: should
  // work.
  assert_true(XML_SetAllocTrackerMaximumAmplification(parser, 3000.0f)
              == XML_TRUE);

  void *const ptr = expat_malloc(parser, 1000, -1);
  assert_true(ptr != NULL);
  expat_free(parser, ptr, -1);
#endif

  XML_ParserFree(parser);
}
END_TEST

START_TEST(test_alloc_tracker_threshold) {
  XML_Parser parser = XML_ParserCreate(NULL);

#if XML_GE == 1
  // Exceed maximum amplification *before* (default) threshold: should work.
  void *const ptr = expat_malloc(parser, 1000, -1);
  assert_true(ptr != NULL);
  expat_free(parser, ptr, -1);

  // Exceed maximum amplification *after* threshold: should be rejected.
  assert_true(XML_SetAllocTrackerActivationThreshold(parser, 999) == XML_TRUE);
  assert_true(expat_malloc(parser, 1000, -1) == NULL);
#endif

  XML_ParserFree(parser);
}
END_TEST

START_TEST(test_alloc_tracker_getbuffer_unlimited) {
  XML_Parser parser = XML_ParserCreate(NULL);

#if XML_GE == 1
  // Artificially lower threshold
  assert_true(XML_SetAllocTrackerActivationThreshold(parser, 0) == XML_TRUE);

  // Self-test: Prove that threshold is as rejecting as expected
  assert_true(expat_malloc(parser, 1000, -1) == NULL);
#endif
  // XML_GetBuffer should be allowed to pass, though
  assert_true(XML_GetBuffer(parser, 1000) != NULL);

  XML_ParserFree(parser);
}
END_TEST
#endif

START_TEST(test_alloc_tracker_api) {
  XML_Parser parserWithoutParent = XML_ParserCreate(NULL);
  XML_Parser parserWithParent = XML_ExternalEntityParserCreate(
      parserWithoutParent, XCS("entity123"), NULL);
  if (parserWithoutParent == NULL)
    fail("parserWithoutParent is NULL");
  if (parserWithParent == NULL)
    fail("parserWithParent is NULL");

#if XML_GE == 1
  // XML_SetAllocTrackerMaximumAmplification, error cases
  if (XML_SetAllocTrackerMaximumAmplification(NULL, 123.0f) == XML_TRUE)
    fail("Call with NULL parser is NOT supposed to succeed");
  if (XML_SetAllocTrackerMaximumAmplification(parserWithParent, 123.0f)
      == XML_TRUE)
    fail("Call with non-root parser is NOT supposed to succeed");
  if (XML_SetAllocTrackerMaximumAmplification(parserWithoutParent, NAN)
      == XML_TRUE)
    fail("Call with NaN limit is NOT supposed to succeed");
  if (XML_SetAllocTrackerMaximumAmplification(parserWithoutParent, -1.0f)
      == XML_TRUE)
    fail("Call with negative limit is NOT supposed to succeed");
  if (XML_SetAllocTrackerMaximumAmplification(parserWithoutParent, 0.9f)
      == XML_TRUE)
    fail("Call with positive limit <1.0 is NOT supposed to succeed");

  // XML_SetAllocTrackerMaximumAmplification, success cases
  if (XML_SetAllocTrackerMaximumAmplification(parserWithoutParent, 1.0f)
      == XML_FALSE)
    fail("Call with positive limit >=1.0 is supposed to succeed");
  if (XML_SetAllocTrackerMaximumAmplification(parserWithoutParent, 123456.789f)
      == XML_FALSE)
    fail("Call with positive limit >=1.0 is supposed to succeed");
  if (XML_SetAllocTrackerMaximumAmplification(parserWithoutParent, INFINITY)
      == XML_FALSE)
    fail("Call with positive limit >=1.0 is supposed to succeed");

  // XML_SetAllocTrackerActivationThreshold, error cases
  if (XML_SetAllocTrackerActivationThreshold(NULL, 123) == XML_TRUE)
    fail("Call with NULL parser is NOT supposed to succeed");
  if (XML_SetAllocTrackerActivationThreshold(parserWithParent, 123) == XML_TRUE)
    fail("Call with non-root parser is NOT supposed to succeed");

  // XML_SetAllocTrackerActivationThreshold, success cases
  if (XML_SetAllocTrackerActivationThreshold(parserWithoutParent, 123)
      == XML_FALSE)
    fail("Call with non-NULL parentless parser is supposed to succeed");
#endif // XML_GE == 1

  XML_ParserFree(parserWithParent);
  XML_ParserFree(parserWithoutParent);
}
END_TEST

START_TEST(test_mem_api_cycle) {
  XML_Parser parser = XML_ParserCreate(NULL);

  void *ptr = XML_MemMalloc(parser, 10);

  assert_true(ptr != NULL);
  memset(ptr, 'x', 10); // assert writability, with ASan in mind

  ptr = XML_MemRealloc(parser, ptr, 20);

  assert_true(ptr != NULL);
  memset(ptr, 'y', 20); // assert writability, with ASan in mind

  XML_MemFree(parser, ptr);

  XML_ParserFree(parser);
}
END_TEST

START_TEST(test_mem_api_unlimited) {
  XML_Parser parser = XML_ParserCreate(NULL);

#if XML_GE == 1
  assert_true(XML_SetAllocTrackerActivationThreshold(parser, 0) == XML_TRUE);
#endif

  void *ptr = XML_MemMalloc(parser, 1000);

  assert_true(ptr != NULL);

  ptr = XML_MemRealloc(parser, ptr, 2000);

  assert_true(ptr != NULL);

  XML_MemFree(parser, ptr);

  XML_ParserFree(parser);
}
END_TEST

void
make_alloc_test_case(Suite *s) {
  TCase *tc_alloc = tcase_create("allocation tests");

  suite_add_tcase(s, tc_alloc);
  tcase_add_checked_fixture(tc_alloc, alloc_setup, alloc_teardown);

  tcase_add_test(tc_alloc, test_alloc_parse_xdecl);
  tcase_add_test(tc_alloc, test_alloc_parse_xdecl_2);
  tcase_add_test(tc_alloc, test_alloc_parse_pi);
  tcase_add_test(tc_alloc, test_alloc_parse_pi_2);
  tcase_add_test(tc_alloc, test_alloc_parse_pi_3);
  tcase_add_test(tc_alloc, test_alloc_parse_comment);
  tcase_add_test(tc_alloc, test_alloc_parse_comment_2);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_create_external_parser);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_run_external_parser);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_dtd_copy_default_atts);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_external_entity);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_ext_entity_set_encoding);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_internal_entity);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_parameter_entity);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_dtd_default_handling);
  tcase_add_test(tc_alloc, test_alloc_explicit_encoding);
  tcase_add_test(tc_alloc, test_alloc_set_base);
  tcase_add_test(tc_alloc, test_alloc_realloc_buffer);
  tcase_add_test__if_xml_ge(tc_alloc, test_alloc_ext_entity_realloc_buffer);
  tcase_add_test(tc_alloc, test_alloc_realloc_many_attributes);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_public_entity_value);
  tcase_add_test__ifdef_xml_dtd(tc_alloc,
                                test_alloc_realloc_subst_public_entity_value);
  tcase_add_test(tc_alloc, test_alloc_parse_public_doctype);
  tcase_add_test(tc_alloc, test_alloc_parse_public_doctype_long_name);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_set_foreign_dtd);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_attribute_enum_value);
  tcase_add_test__ifdef_xml_dtd(tc_alloc,
                                test_alloc_realloc_attribute_enum_value);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_realloc_implied_attribute);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_realloc_default_attribute);
  tcase_add_test__if_xml_ge(tc_alloc, test_alloc_notation);
  tcase_add_test(tc_alloc, test_alloc_public_notation);
  tcase_add_test(tc_alloc, test_alloc_system_notation);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_nested_groups);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_realloc_nested_groups);
  tcase_add_test(tc_alloc, test_alloc_large_group);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_realloc_group_choice);
  tcase_add_test(tc_alloc, test_alloc_pi_in_epilog);
  tcase_add_test(tc_alloc, test_alloc_comment_in_epilog);
  tcase_add_test__ifdef_xml_dtd(tc_alloc,
                                test_alloc_realloc_long_attribute_value);
  tcase_add_test(tc_alloc, test_alloc_attribute_whitespace);
  tcase_add_test(tc_alloc, test_alloc_attribute_predefined_entity);
  tcase_add_test(tc_alloc, test_alloc_long_attr_default_with_char_ref);
  tcase_add_test__if_xml_ge(tc_alloc, test_alloc_long_attr_value);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_nested_entities);
  tcase_add_test__ifdef_xml_dtd(tc_alloc,
                                test_alloc_realloc_param_entity_newline);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_realloc_ce_extends_pe);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_realloc_attributes);
  tcase_add_test(tc_alloc, test_alloc_long_doc_name);
  tcase_add_test__if_xml_ge(tc_alloc, test_alloc_long_base);
  tcase_add_test__if_xml_ge(tc_alloc, test_alloc_long_public_id);
  tcase_add_test__if_xml_ge(tc_alloc, test_alloc_long_entity_value);
  tcase_add_test__if_xml_ge(tc_alloc, test_alloc_long_notation);

  tcase_add_test__ifdef_xml_dtd(
      tc_alloc, test_alloc_reset_after_external_entity_parser_create_fail);

#if defined(XML_TESTING)
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_tracker_size_recorded);
  tcase_add_test__ifdef_xml_dtd(tc_alloc,
                                test_alloc_tracker_maximum_amplification);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_tracker_threshold);
  tcase_add_test__ifdef_xml_dtd(tc_alloc,
                                test_alloc_tracker_getbuffer_unlimited);
#endif
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_alloc_tracker_api);

  tcase_add_test(tc_alloc, test_mem_api_cycle);
  tcase_add_test__ifdef_xml_dtd(tc_alloc, test_mem_api_unlimited);
}
