/* Tests in the "miscellaneous" test case for the Expat test suite
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

#if defined(NDEBUG)
#  undef NDEBUG /* because test suite relies on assert(...) at the moment */
#endif

#include <assert.h>
#include <string.h>

#include "expat_config.h"

#include "expat.h"
#include "internal.h"
#include "minicheck.h"
#include "memcheck.h"
#include "common.h"
#include "ascii.h" /* for ASCII_xxx */
#include "handlers.h"
#include "misc_tests.h"

void XMLCALL accumulate_characters_ext_handler(void *userData,
                                               const XML_Char *s, int len);

/* Test that a failure to allocate the parser structure fails gracefully */
START_TEST(test_misc_alloc_create_parser) {
  XML_Memory_Handling_Suite memsuite = {duff_allocator, realloc, free};
  unsigned int i;
  const unsigned int max_alloc_count = 10;

  /* Something this simple shouldn't need more than 10 allocations */
  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = (int)i;
    g_parser = XML_ParserCreate_MM(NULL, &memsuite, NULL);
    if (g_parser != NULL)
      break;
  }
  if (i == 0)
    fail("Parser unexpectedly ignored failing allocator");
  else if (i == max_alloc_count)
    fail("Parser not created with max allocation count");
}
END_TEST

/* Test memory allocation failures for a parser with an encoding */
START_TEST(test_misc_alloc_create_parser_with_encoding) {
  XML_Memory_Handling_Suite memsuite = {duff_allocator, realloc, free};
  unsigned int i;
  const unsigned int max_alloc_count = 10;

  /* Try several levels of allocation */
  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = (int)i;
    g_parser = XML_ParserCreate_MM(XCS("us-ascii"), &memsuite, NULL);
    if (g_parser != NULL)
      break;
  }
  if (i == 0)
    fail("Parser ignored failing allocator");
  else if (i == max_alloc_count)
    fail("Parser not created with max allocation count");
}
END_TEST

/* Test that freeing a NULL parser doesn't cause an explosion.
 * (Not actually tested anywhere else)
 */
START_TEST(test_misc_null_parser) {
  XML_ParserFree(NULL);
}
END_TEST

#if defined(__has_feature)
#  if __has_feature(undefined_behavior_sanitizer)
#    define EXPAT_TESTS_UBSAN 1
#  else
#    define EXPAT_TESTS_UBSAN 0
#  endif
#else
#  define EXPAT_TESTS_UBSAN 0
#endif

/* Test that XML_ErrorString rejects out-of-range codes */
START_TEST(test_misc_error_string) {
#if ! EXPAT_TESTS_UBSAN // because this would trigger UBSan
  union {
    enum XML_Error xml_error;
    int integer;
  } trickery;

  assert_true(sizeof(enum XML_Error) == sizeof(int)); // self-test

  trickery.integer = -1;
  if (XML_ErrorString(trickery.xml_error) != NULL)
    fail("Negative error code not rejected");

  trickery.integer = 100;
  if (XML_ErrorString(trickery.xml_error) != NULL)
    fail("Large error code not rejected");
#endif
}
END_TEST

/* Test the version information is consistent */

/* Since we are working in XML_LChars (potentially 16-bits), we
 * can't use the standard C library functions for character
 * manipulation and have to roll our own.
 */
static int
parse_version(const XML_LChar *version_text,
              XML_Expat_Version *version_struct) {
  if (! version_text)
    return XML_FALSE;

  while (*version_text != 0x00) {
    if (*version_text >= ASCII_0 && *version_text <= ASCII_9)
      break;
    version_text++;
  }
  if (*version_text == 0x00)
    return XML_FALSE;

  /* version_struct->major = strtoul(version_text, 10, &version_text) */
  version_struct->major = 0;
  while (*version_text >= ASCII_0 && *version_text <= ASCII_9) {
    version_struct->major
        = 10 * version_struct->major + (*version_text++ - ASCII_0);
  }
  if (*version_text++ != ASCII_PERIOD)
    return XML_FALSE;

  /* Now for the minor version number */
  version_struct->minor = 0;
  while (*version_text >= ASCII_0 && *version_text <= ASCII_9) {
    version_struct->minor
        = 10 * version_struct->minor + (*version_text++ - ASCII_0);
  }
  if (*version_text++ != ASCII_PERIOD)
    return XML_FALSE;

  /* Finally the micro version number */
  version_struct->micro = 0;
  while (*version_text >= ASCII_0 && *version_text <= ASCII_9) {
    version_struct->micro
        = 10 * version_struct->micro + (*version_text++ - ASCII_0);
  }
  if (*version_text != 0x00)
    return XML_FALSE;
  return XML_TRUE;
}

static int
versions_equal(const XML_Expat_Version *first,
               const XML_Expat_Version *second) {
  return (first->major == second->major && first->minor == second->minor
          && first->micro == second->micro);
}

START_TEST(test_misc_version) {
  XML_Expat_Version read_version = XML_ExpatVersionInfo();
  /* Silence compiler warning with the following assignment */
  XML_Expat_Version parsed_version = {0, 0, 0};
  const XML_LChar *version_text = XML_ExpatVersion();

  if (version_text == NULL)
    fail("Could not obtain version text");
  assert(version_text != NULL);
  if (! parse_version(version_text, &parsed_version))
    fail("Unable to parse version text");
  if (! versions_equal(&read_version, &parsed_version))
    fail("Version mismatch");

  if (xcstrcmp(version_text, XCS("expat_2.7.2"))
      != 0) /* needs bump on releases */
    fail("XML_*_VERSION in expat.h out of sync?\n");
}
END_TEST

/* Test feature information */
START_TEST(test_misc_features) {
  const XML_Feature *features = XML_GetFeatureList();

  /* Prevent problems with double-freeing parsers */
  g_parser = NULL;
  if (features == NULL) {
    fail("Failed to get feature information");
  } else {
    /* Loop through the features checking what we can */
    while (features->feature != XML_FEATURE_END) {
      switch (features->feature) {
      case XML_FEATURE_SIZEOF_XML_CHAR:
        if (features->value != sizeof(XML_Char))
          fail("Incorrect size of XML_Char");
        break;
      case XML_FEATURE_SIZEOF_XML_LCHAR:
        if (features->value != sizeof(XML_LChar))
          fail("Incorrect size of XML_LChar");
        break;
      default:
        break;
      }
      features++;
    }
  }
}
END_TEST

/* Regression test for GitHub Issue #17: memory leak parsing attribute
 * values with mixed bound and unbound namespaces.
 */
START_TEST(test_misc_attribute_leak) {
  const char *text = "<D xmlns:L=\"D\" l:a='' L:a=''/>";
  XML_Memory_Handling_Suite memsuite
      = {tracking_malloc, tracking_realloc, tracking_free};

  g_parser = XML_ParserCreate_MM(XCS("UTF-8"), &memsuite, XCS("\n"));
  expect_failure(text, XML_ERROR_UNBOUND_PREFIX, "Unbound prefixes not found");
  XML_ParserFree(g_parser);
  /* Prevent the teardown trying to double free */
  g_parser = NULL;

  if (! tracking_report())
    fail("Memory leak found");
}
END_TEST

/* Test parser created for UTF-16LE is successful */
START_TEST(test_misc_utf16le) {
  const char text[] =
      /* <?xml version='1.0'?><q>Hi</q> */
      "<\0?\0x\0m\0l\0 \0"
      "v\0e\0r\0s\0i\0o\0n\0=\0'\0\x31\0.\0\x30\0'\0?\0>\0"
      "<\0q\0>\0H\0i\0<\0/\0q\0>\0";
  const XML_Char *expected = XCS("Hi");
  CharData storage;

  g_parser = XML_ParserCreate(XCS("UTF-16LE"));
  if (g_parser == NULL)
    fail("Parser not created");

  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetCharacterDataHandler(g_parser, accumulate_characters);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_misc_stop_during_end_handler_issue_240_1) {
  XML_Parser parser;
  DataIssue240 *mydata;
  enum XML_Status result;
  const char *const doc1 = "<doc><e1/><e><foo/></e></doc>";

  parser = XML_ParserCreate(NULL);
  XML_SetElementHandler(parser, start_element_issue_240, end_element_issue_240);
  mydata = (DataIssue240 *)malloc(sizeof(DataIssue240));
  assert_true(mydata != NULL);
  mydata->parser = parser;
  mydata->deep = 0;
  XML_SetUserData(parser, mydata);

  result = _XML_Parse_SINGLE_BYTES(parser, doc1, (int)strlen(doc1), 1);
  XML_ParserFree(parser);
  free(mydata);
  if (result != XML_STATUS_ERROR)
    fail("Stopping the parser did not work as expected");
}
END_TEST

START_TEST(test_misc_stop_during_end_handler_issue_240_2) {
  XML_Parser parser;
  DataIssue240 *mydata;
  enum XML_Status result;
  const char *const doc2 = "<doc><elem/></doc>";

  parser = XML_ParserCreate(NULL);
  XML_SetElementHandler(parser, start_element_issue_240, end_element_issue_240);
  mydata = (DataIssue240 *)malloc(sizeof(DataIssue240));
  assert_true(mydata != NULL);
  mydata->parser = parser;
  mydata->deep = 0;
  XML_SetUserData(parser, mydata);

  result = _XML_Parse_SINGLE_BYTES(parser, doc2, (int)strlen(doc2), 1);
  XML_ParserFree(parser);
  free(mydata);
  if (result != XML_STATUS_ERROR)
    fail("Stopping the parser did not work as expected");
}
END_TEST

START_TEST(test_misc_deny_internal_entity_closing_doctype_issue_317) {
  const char *const inputOne
      = "<!DOCTYPE d [\n"
        "<!ENTITY % element_d '<!ELEMENT d (#PCDATA)*>'>\n"
        "%element_d;\n"
        "<!ENTITY % e ']><d/>'>\n"
        "\n"
        "%e;";
  const char *const inputTwo
      = "<!DOCTYPE d [\n"
        "<!ENTITY % element_d '<!ELEMENT d (#PCDATA)*>'>\n"
        "%element_d;\n"
        "<!ENTITY % e1 ']><d/>'><!ENTITY % e2 '&#37;e1;'>\n"
        "\n"
        "%e2;";
  const char *const inputThree
      = "<!DOCTYPE d [\n"
        "<!ENTITY % element_d '<!ELEMENT d (#PCDATA)*>'>\n"
        "%element_d;\n"
        "<!ENTITY % e ']><d'>\n"
        "\n"
        "%e;/>";
  const char *const inputIssue317
      = "<!DOCTYPE doc [\n"
        "<!ENTITY % element_doc '<!ELEMENT doc (#PCDATA)*>'>\n"
        "%element_doc;\n"
        "<!ENTITY % foo ']>\n"
        "<doc>Hell<oc (#PCDATA)*>'>\n"
        "%foo;\n"
        "]>\n"
        "<doc>Hello, world</dVc>";

  const char *const inputs[] = {inputOne, inputTwo, inputThree, inputIssue317};
  const XML_Bool suspendOrNot[] = {XML_FALSE, XML_TRUE};
  size_t inputIndex = 0;

  for (; inputIndex < sizeof(inputs) / sizeof(inputs[0]); inputIndex++) {
    for (size_t suspendOrNotIndex = 0;
         suspendOrNotIndex < sizeof(suspendOrNot) / sizeof(suspendOrNot[0]);
         suspendOrNotIndex++) {
      const char *const input = inputs[inputIndex];
      const XML_Bool suspend = suspendOrNot[suspendOrNotIndex];
      if (suspend && (g_chunkSize > 0)) {
        // We cannot use _XML_Parse_SINGLE_BYTES below due to suspension, and
        // so chunk sizes >0 would only repeat the very same test
        // due to use of plain XML_Parse; we are saving upon that runtime:
        return;
      }

      set_subtest("[input=%d suspend=%s] %s", (int)inputIndex,
                  suspend ? "true" : "false", input);
      XML_Parser parser;
      enum XML_Status parseResult;
      int setParamEntityResult;
      XML_Size lineNumber;
      XML_Size columnNumber;

      parser = XML_ParserCreate(NULL);
      setParamEntityResult
          = XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
      if (setParamEntityResult != 1)
        fail("Failed to set XML_PARAM_ENTITY_PARSING_ALWAYS.");

      if (suspend) {
        XML_SetUserData(parser, parser);
        XML_SetElementDeclHandler(parser, suspend_after_element_declaration);
      }

      if (suspend) {
        // can't use SINGLE_BYTES here, because it'll return early on
        // suspension, and we won't know exactly how much input we actually
        // managed to give Expat.
        parseResult = XML_Parse(parser, input, (int)strlen(input), 0);

        while (parseResult == XML_STATUS_SUSPENDED) {
          parseResult = XML_ResumeParser(parser);
        }

        if (parseResult != XML_STATUS_ERROR) {
          // can't use SINGLE_BYTES here, because it'll return early on
          // suspension, and we won't know exactly how much input we actually
          // managed to give Expat.
          parseResult = XML_Parse(parser, "", 0, 1);
        }

        while (parseResult == XML_STATUS_SUSPENDED) {
          parseResult = XML_ResumeParser(parser);
        }
      } else {
        parseResult
            = _XML_Parse_SINGLE_BYTES(parser, input, (int)strlen(input), 0);

        if (parseResult != XML_STATUS_ERROR) {
          parseResult = _XML_Parse_SINGLE_BYTES(parser, "", 0, 1);
        }
      }

      if (parseResult != XML_STATUS_ERROR) {
        fail("Parsing was expected to fail but succeeded.");
      }

      if (XML_GetErrorCode(parser) != XML_ERROR_INVALID_TOKEN)
        fail("Error code does not match XML_ERROR_INVALID_TOKEN");

      lineNumber = XML_GetCurrentLineNumber(parser);
      if (lineNumber != 6)
        fail("XML_GetCurrentLineNumber does not work as expected.");

      columnNumber = XML_GetCurrentColumnNumber(parser);
      if (columnNumber != 0)
        fail("XML_GetCurrentColumnNumber does not work as expected.");

      XML_ParserFree(parser);
    }
  }
}
END_TEST

START_TEST(test_misc_tag_mismatch_reset_leak) {
#ifdef XML_NS
  const char *const text = "<open xmlns='https://namespace1.test'></close>";
  XML_Parser parser = XML_ParserCreateNS(NULL, XCS('\n'));

  if (_XML_Parse_SINGLE_BYTES(parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Call to parse was expected to fail");
  if (XML_GetErrorCode(parser) != XML_ERROR_TAG_MISMATCH)
    fail("Call to parse was expected to fail from a closing tag mismatch");

  XML_ParserReset(parser, NULL);

  if (_XML_Parse_SINGLE_BYTES(parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Call to parse was expected to fail");
  if (XML_GetErrorCode(parser) != XML_ERROR_TAG_MISMATCH)
    fail("Call to parse was expected to fail from a closing tag mismatch");

  XML_ParserFree(parser);
#endif
}
END_TEST

START_TEST(test_misc_create_external_entity_parser_with_null_context) {
  // With XML_DTD undefined, the only supported case of external entities
  // is pattern "<!ENTITY entity123 SYSTEM 'filename123'>". A NULL context
  // was causing a segfault through a null pointer dereference in function
  // setContext, previously.
  XML_Parser parser = XML_ParserCreate(NULL);
  XML_Parser ext_parser = XML_ExternalEntityParserCreate(parser, NULL, NULL);
#ifdef XML_DTD
  assert_true(ext_parser != NULL);
  XML_ParserFree(ext_parser);
#else
  assert_true(ext_parser == NULL);
#endif /* XML_DTD */
  XML_ParserFree(parser);
}
END_TEST

START_TEST(test_misc_general_entities_support) {
  const char *const doc
      = "<!DOCTYPE r [\n"
        "<!ENTITY e1 'v1'>\n"
        "<!ENTITY e2 SYSTEM 'v2'>\n"
        "]>\n"
        "<r a1='[&e1;]'>[&e1;][&e2;][&amp;&apos;&gt;&lt;&quot;]</r>";

  CharData storage;
  CharData_Init(&storage);

  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, &storage);
  XML_SetStartElementHandler(parser, accumulate_start_element);
  XML_SetExternalEntityRefHandler(parser,
                                  external_entity_failer__if_not_xml_ge);
  XML_SetEntityDeclHandler(parser, accumulate_entity_decl);
  XML_SetCharacterDataHandler(parser, accumulate_characters);

  if (_XML_Parse_SINGLE_BYTES(parser, doc, (int)strlen(doc), XML_TRUE)
      != XML_STATUS_OK) {
    xml_failure(parser);
  }

  XML_ParserFree(parser);

  CharData_CheckXMLChars(&storage,
  /* clang-format off */
#if XML_GE == 1
                         XCS("e1=v1\n")
                         XCS("e2=(null)\n")
                         XCS("(r(a1=[v1]))\n")
                         XCS("[v1][][&'><\"]")
#else
                         XCS("e1=&amp;e1;\n")
                         XCS("e2=(null)\n")
                         XCS("(r(a1=[&e1;]))\n")
                         XCS("[&e1;][&e2;][&'><\"]")
#endif
  );
  /* clang-format on */
}
END_TEST

static void XMLCALL
resumable_stopping_character_handler(void *userData, const XML_Char *s,
                                     int len) {
  UNUSED_P(s);
  UNUSED_P(len);
  XML_Parser parser = (XML_Parser)userData;
  XML_StopParser(parser, XML_TRUE);
}

// NOTE: This test needs active LeakSanitizer to be of actual use
START_TEST(test_misc_char_handler_stop_without_leak) {
  const char *const data
      = "<!DOCTYPE t1[<!ENTITY e1 'angle<'><!ENTITY e2 '&e1;'>]><t1>&e2;";
  XML_Parser parser = XML_ParserCreate(NULL);
  assert_true(parser != NULL);
  XML_SetUserData(parser, parser);
  XML_SetCharacterDataHandler(parser, resumable_stopping_character_handler);
  _XML_Parse_SINGLE_BYTES(parser, data, (int)strlen(data), XML_FALSE);
  XML_ParserFree(parser);
}
END_TEST

START_TEST(test_misc_resumeparser_not_crashing) {
  XML_Parser parser = XML_ParserCreate(NULL);
  XML_GetBuffer(parser, 1);
  XML_StopParser(parser, /*resumable=*/XML_TRUE);
  XML_ResumeParser(parser); // could crash here, previously
  XML_ParserFree(parser);
}
END_TEST

START_TEST(test_misc_stopparser_rejects_unstarted_parser) {
  const XML_Bool cases[] = {XML_TRUE, XML_FALSE};
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const XML_Bool resumable = cases[i];
    XML_Parser parser = XML_ParserCreate(NULL);
    assert_true(XML_GetErrorCode(parser) == XML_ERROR_NONE);
    assert_true(XML_StopParser(parser, resumable) == XML_STATUS_ERROR);
    assert_true(XML_GetErrorCode(parser) == XML_ERROR_NOT_STARTED);
    XML_ParserFree(parser);
  }
}
END_TEST

/* Adaptation of accumulate_characters that takes ExtHdlrData input to work with
 * test_renter_loop_finite_content below */
void XMLCALL
accumulate_characters_ext_handler(void *userData, const XML_Char *s, int len) {
  ExtHdlrData *const test_data = (ExtHdlrData *)userData;
  CharData_AppendXMLChars(test_data->storage, s, len);
}

/* Test that internalEntityProcessor does not re-enter forever;
 * based on files tests/xmlconf/xmltest/valid/ext-sa/012.{xml,ent} */
START_TEST(test_renter_loop_finite_content) {
  CharData storage;
  CharData_Init(&storage);
  const char *const text = "<!DOCTYPE doc [\n"
                           "<!ENTITY e1 '&e2;'>\n"
                           "<!ENTITY e2 '&e3;'>\n"
                           "<!ENTITY e3 SYSTEM '012.ent'>\n"
                           "<!ENTITY e4 '&e5;'>\n"
                           "<!ENTITY e5 '(e5)'>\n"
                           "<!ELEMENT doc (#PCDATA)>\n"
                           "]>\n"
                           "<doc>&e1;</doc>\n";
  ExtHdlrData test_data = {"&e4;\n", external_entity_null_loader, &storage};
  const XML_Char *const expected = XCS("(e5)\n");

  XML_Parser parser = XML_ParserCreate(NULL);
  assert_true(parser != NULL);
  XML_SetUserData(parser, &test_data);
  XML_SetExternalEntityRefHandler(parser, external_entity_oneshot_loader);
  XML_SetCharacterDataHandler(parser, accumulate_characters_ext_handler);
  if (_XML_Parse_SINGLE_BYTES(parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(parser);

  CharData_CheckXMLChars(&storage, expected);
  XML_ParserFree(parser);
}
END_TEST

// Inspired by function XML_OriginalString of Perl's XML::Parser
static char *
dup_original_string(XML_Parser parser) {
  const int byte_count = XML_GetCurrentByteCount(parser);

  assert_true(byte_count >= 0);

  int offset = -1;
  int size = -1;

  const char *const context = XML_GetInputContext(parser, &offset, &size);

#if XML_CONTEXT_BYTES > 0
  assert_true(context != NULL);
  assert_true(offset >= 0);
  assert_true(size >= 0);
  return portable_strndup(context + offset, byte_count);
#else
  assert_true(context == NULL);
  return NULL;
#endif
}

static void
on_characters_issue_980(void *userData, const XML_Char *s, int len) {
  (void)s;
  (void)len;
  XML_Parser parser = (XML_Parser)userData;

  char *const original_string = dup_original_string(parser);

#if XML_CONTEXT_BYTES > 0
  assert_true(original_string != NULL);
  assert_true(strcmp(original_string, "&draft.day;") == 0);
  free(original_string);
#else
  assert_true(original_string == NULL);
#endif
}

START_TEST(test_misc_expected_event_ptr_issue_980) {
  // NOTE: This is a tiny subset of sample "REC-xml-19980210.xml"
  //       from Perl's XML::Parser
  const char *const doc = "<!DOCTYPE day [\n"
                          "  <!ENTITY draft.day '10'>\n"
                          "]>\n"
                          "<day>&draft.day;</day>\n";

  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, parser);
  XML_SetCharacterDataHandler(parser, on_characters_issue_980);

  assert_true(_XML_Parse_SINGLE_BYTES(parser, doc, (int)strlen(doc),
                                      /*isFinal=*/XML_TRUE)
              == XML_STATUS_OK);

  XML_ParserFree(parser);
}
END_TEST

void
make_miscellaneous_test_case(Suite *s) {
  TCase *tc_misc = tcase_create("miscellaneous tests");

  suite_add_tcase(s, tc_misc);
  tcase_add_checked_fixture(tc_misc, NULL, basic_teardown);

  tcase_add_test(tc_misc, test_misc_alloc_create_parser);
  tcase_add_test(tc_misc, test_misc_alloc_create_parser_with_encoding);
  tcase_add_test(tc_misc, test_misc_null_parser);
  tcase_add_test(tc_misc, test_misc_error_string);
  tcase_add_test(tc_misc, test_misc_version);
  tcase_add_test(tc_misc, test_misc_features);
  tcase_add_test(tc_misc, test_misc_attribute_leak);
  tcase_add_test(tc_misc, test_misc_utf16le);
  tcase_add_test(tc_misc, test_misc_stop_during_end_handler_issue_240_1);
  tcase_add_test(tc_misc, test_misc_stop_during_end_handler_issue_240_2);
  tcase_add_test__ifdef_xml_dtd(
      tc_misc, test_misc_deny_internal_entity_closing_doctype_issue_317);
  tcase_add_test(tc_misc, test_misc_tag_mismatch_reset_leak);
  tcase_add_test(tc_misc,
                 test_misc_create_external_entity_parser_with_null_context);
  tcase_add_test(tc_misc, test_misc_general_entities_support);
  tcase_add_test(tc_misc, test_misc_char_handler_stop_without_leak);
  tcase_add_test(tc_misc, test_misc_resumeparser_not_crashing);
  tcase_add_test(tc_misc, test_misc_stopparser_rejects_unstarted_parser);
  tcase_add_test__if_xml_ge(tc_misc, test_renter_loop_finite_content);
  tcase_add_test(tc_misc, test_misc_expected_event_ptr_issue_980);
}
