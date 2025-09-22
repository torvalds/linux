/* Tests in the "namespace" test case for the Expat test suite
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
   Copyright (c) 2016-2023 Sebastian Pipping <sebastian@pipping.org>
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

#include "expat_config.h"

#include <string.h>

#include "expat.h"
#include "internal.h"
#include "minicheck.h"
#include "common.h"
#include "dummy.h"
#include "handlers.h"
#include "ns_tests.h"

static void
namespace_setup(void) {
  g_parser = XML_ParserCreateNS(NULL, XCS(' '));
  if (g_parser == NULL)
    fail("Parser not created.");
}

static void
namespace_teardown(void) {
  basic_teardown();
}

START_TEST(test_return_ns_triplet) {
  const char *text = "<foo:e xmlns:foo='http://example.org/' bar:a='12'\n"
                     "       xmlns:bar='http://example.org/'>";
  const char *epilog = "</foo:e>";
  const XML_Char *elemstr[]
      = {XCS("http://example.org/ e foo"), XCS("http://example.org/ a bar")};
  XML_SetReturnNSTriplet(g_parser, XML_TRUE);
  XML_SetUserData(g_parser, (void *)elemstr);
  XML_SetElementHandler(g_parser, triplet_start_checker, triplet_end_checker);
  XML_SetNamespaceDeclHandler(g_parser, dummy_start_namespace_decl_handler,
                              dummy_end_namespace_decl_handler);
  g_triplet_start_flag = XML_FALSE;
  g_triplet_end_flag = XML_FALSE;
  init_dummy_handlers();
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  /* Check that unsetting "return triplets" fails while still parsing */
  XML_SetReturnNSTriplet(g_parser, XML_FALSE);
  if (_XML_Parse_SINGLE_BYTES(g_parser, epilog, (int)strlen(epilog), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (! g_triplet_start_flag)
    fail("triplet_start_checker not invoked");
  if (! g_triplet_end_flag)
    fail("triplet_end_checker not invoked");
  if (get_dummy_handler_flags()
      != (DUMMY_START_NS_DECL_HANDLER_FLAG | DUMMY_END_NS_DECL_HANDLER_FLAG))
    fail("Namespace handlers not called");
}
END_TEST

/* Test that the parsing status is correctly reset by XML_ParserReset().
 * We use test_return_ns_triplet() for our example parse to improve
 * coverage of tidying up code executed.
 */
START_TEST(test_ns_parser_reset) {
  XML_ParsingStatus status;

  XML_GetParsingStatus(g_parser, &status);
  if (status.parsing != XML_INITIALIZED)
    fail("parsing status doesn't start INITIALIZED");
  test_return_ns_triplet();
  XML_GetParsingStatus(g_parser, &status);
  if (status.parsing != XML_FINISHED)
    fail("parsing status doesn't end FINISHED");
  XML_ParserReset(g_parser, NULL);
  XML_GetParsingStatus(g_parser, &status);
  if (status.parsing != XML_INITIALIZED)
    fail("parsing status doesn't reset to INITIALIZED");
}
END_TEST

static void
run_ns_tagname_overwrite_test(const char *text, const XML_Char *result) {
  CharData storage;
  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetElementHandler(g_parser, overwrite_start_checker,
                        overwrite_end_checker);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, result);
}

/* Regression test for SF bug #566334. */
START_TEST(test_ns_tagname_overwrite) {
  const char *text = "<n:e xmlns:n='http://example.org/'>\n"
                     "  <n:f n:attr='foo'/>\n"
                     "  <n:g n:attr2='bar'/>\n"
                     "</n:e>";
  const XML_Char *result = XCS("start http://example.org/ e\n")
      XCS("start http://example.org/ f\n")
          XCS("attribute http://example.org/ attr\n")
              XCS("end http://example.org/ f\n")
                  XCS("start http://example.org/ g\n")
                      XCS("attribute http://example.org/ attr2\n")
                          XCS("end http://example.org/ g\n")
                              XCS("end http://example.org/ e\n");
  run_ns_tagname_overwrite_test(text, result);
}
END_TEST

/* Regression test for SF bug #566334. */
START_TEST(test_ns_tagname_overwrite_triplet) {
  const char *text = "<n:e xmlns:n='http://example.org/'>\n"
                     "  <n:f n:attr='foo'/>\n"
                     "  <n:g n:attr2='bar'/>\n"
                     "</n:e>";
  const XML_Char *result = XCS("start http://example.org/ e n\n")
      XCS("start http://example.org/ f n\n")
          XCS("attribute http://example.org/ attr n\n")
              XCS("end http://example.org/ f n\n")
                  XCS("start http://example.org/ g n\n")
                      XCS("attribute http://example.org/ attr2 n\n")
                          XCS("end http://example.org/ g n\n")
                              XCS("end http://example.org/ e n\n");
  XML_SetReturnNSTriplet(g_parser, XML_TRUE);
  run_ns_tagname_overwrite_test(text, result);
}
END_TEST

/* Regression test for SF bug #620343. */
START_TEST(test_start_ns_clears_start_element) {
  /* This needs to use separate start/end tags; using the empty tag
     syntax doesn't cause the problematic path through Expat to be
     taken.
  */
  const char *text = "<e xmlns='http://example.org/'></e>";

  XML_SetStartElementHandler(g_parser, start_element_fail);
  XML_SetStartNamespaceDeclHandler(g_parser, start_ns_clearing_start_element);
  XML_SetEndNamespaceDeclHandler(g_parser, dummy_end_namespace_decl_handler);
  XML_UseParserAsHandlerArg(g_parser);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Regression test for SF bug #616863. */
START_TEST(test_default_ns_from_ext_subset_and_ext_ge) {
  const char *text = "<?xml version='1.0'?>\n"
                     "<!DOCTYPE doc SYSTEM 'http://example.org/doc.dtd' [\n"
                     "  <!ENTITY en SYSTEM 'http://example.org/entity.ent'>\n"
                     "]>\n"
                     "<doc xmlns='http://example.org/ns1'>\n"
                     "&en;\n"
                     "</doc>";

  XML_SetParamEntityParsing(g_parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
  XML_SetExternalEntityRefHandler(g_parser, external_entity_handler);
  /* We actually need to set this handler to tickle this bug. */
  XML_SetStartElementHandler(g_parser, dummy_start_element);
  XML_SetUserData(g_parser, NULL);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Regression test #1 for SF bug #673791. */
START_TEST(test_ns_prefix_with_empty_uri_1) {
  const char *text = "<doc xmlns:prefix='http://example.org/'>\n"
                     "  <e xmlns:prefix=''/>\n"
                     "</doc>";

  expect_failure(text, XML_ERROR_UNDECLARING_PREFIX,
                 "Did not report re-setting namespace"
                 " URI with prefix to ''.");
}
END_TEST

/* Regression test #2 for SF bug #673791. */
START_TEST(test_ns_prefix_with_empty_uri_2) {
  const char *text = "<?xml version='1.0'?>\n"
                     "<docelem xmlns:pre=''/>";

  expect_failure(text, XML_ERROR_UNDECLARING_PREFIX,
                 "Did not report setting namespace URI with prefix to ''.");
}
END_TEST

/* Regression test #3 for SF bug #673791. */
START_TEST(test_ns_prefix_with_empty_uri_3) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ELEMENT doc EMPTY>\n"
                     "  <!ATTLIST doc\n"
                     "    xmlns:prefix CDATA ''>\n"
                     "]>\n"
                     "<doc/>";

  expect_failure(text, XML_ERROR_UNDECLARING_PREFIX,
                 "Didn't report attr default setting NS w/ prefix to ''.");
}
END_TEST

/* Regression test #4 for SF bug #673791. */
START_TEST(test_ns_prefix_with_empty_uri_4) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ELEMENT prefix:doc EMPTY>\n"
                     "  <!ATTLIST prefix:doc\n"
                     "    xmlns:prefix CDATA 'http://example.org/'>\n"
                     "]>\n"
                     "<prefix:doc/>";
  /* Packaged info expected by the end element handler;
     the weird structuring lets us reuse the triplet_end_checker()
     function also used for another test. */
  const XML_Char *elemstr[] = {XCS("http://example.org/ doc prefix")};
  XML_SetReturnNSTriplet(g_parser, XML_TRUE);
  XML_SetUserData(g_parser, (void *)elemstr);
  XML_SetEndElementHandler(g_parser, triplet_end_checker);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test with non-xmlns prefix */
START_TEST(test_ns_unbound_prefix) {
  const char *text = "<!DOCTYPE doc [\n"
                     "  <!ELEMENT prefix:doc EMPTY>\n"
                     "  <!ATTLIST prefix:doc\n"
                     "    notxmlns:prefix CDATA 'http://example.org/'>\n"
                     "]>\n"
                     "<prefix:doc/>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Unbound prefix incorrectly passed");
  if (XML_GetErrorCode(g_parser) != XML_ERROR_UNBOUND_PREFIX)
    xml_failure(g_parser);
}
END_TEST

START_TEST(test_ns_default_with_empty_uri) {
  const char *text = "<doc xmlns='http://example.org/'>\n"
                     "  <e xmlns=''/>\n"
                     "</doc>";
  /* Add some handlers to exercise extra code paths */
  XML_SetStartNamespaceDeclHandler(g_parser,
                                   dummy_start_namespace_decl_handler);
  XML_SetEndNamespaceDeclHandler(g_parser, dummy_end_namespace_decl_handler);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Regression test for SF bug #692964: two prefixes for one namespace. */
START_TEST(test_ns_duplicate_attrs_diff_prefixes) {
  const char *text = "<doc xmlns:a='http://example.org/a'\n"
                     "     xmlns:b='http://example.org/a'\n"
                     "     a:a='v' b:a='v' />";
  expect_failure(text, XML_ERROR_DUPLICATE_ATTRIBUTE,
                 "did not report multiple attributes with same URI+name");
}
END_TEST

START_TEST(test_ns_duplicate_hashes) {
  /* The hash of an attribute is calculated as the hash of its URI
   * concatenated with a space followed by its name (after the
   * colon).  We wish to generate attributes with the same hash
   * value modulo the attribute table size so that we can check that
   * the attribute hash table works correctly.  The attribute hash
   * table size will be the smallest power of two greater than the
   * number of attributes, but at least eight.  There is
   * unfortunately no programmatic way of getting the hash or the
   * table size at user level, but the test code coverage percentage
   * will drop if the hashes cease to point to the same row.
   *
   * The cunning plan is to have few enough attributes to have a
   * reliable table size of 8, and have the single letter attribute
   * names be 8 characters apart, producing a hash which will be the
   * same modulo 8.
   */
  const char *text = "<doc xmlns:a='http://example.org/a'\n"
                     "     a:a='v' a:i='w' />";
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Regression test for SF bug #695401: unbound prefix. */
START_TEST(test_ns_unbound_prefix_on_attribute) {
  const char *text = "<doc a:attr=''/>";
  expect_failure(text, XML_ERROR_UNBOUND_PREFIX,
                 "did not report unbound prefix on attribute");
}
END_TEST

/* Regression test for SF bug #695401: unbound prefix. */
START_TEST(test_ns_unbound_prefix_on_element) {
  const char *text = "<a:doc/>";
  expect_failure(text, XML_ERROR_UNBOUND_PREFIX,
                 "did not report unbound prefix on element");
}
END_TEST

/* Test that long element names with namespaces are handled correctly */
START_TEST(test_ns_long_element) {
  const char *text
      = "<foo:thisisalongenoughelementnametotriggerareallocation\n"
        " xmlns:foo='http://example.org/' bar:a='12'\n"
        " xmlns:bar='http://example.org/'>"
        "</foo:thisisalongenoughelementnametotriggerareallocation>";
  const XML_Char *elemstr[]
      = {XCS("http://example.org/")
             XCS(" thisisalongenoughelementnametotriggerareallocation foo"),
         XCS("http://example.org/ a bar")};

  XML_SetReturnNSTriplet(g_parser, XML_TRUE);
  XML_SetUserData(g_parser, (void *)elemstr);
  XML_SetElementHandler(g_parser, triplet_start_checker, triplet_end_checker);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test mixed population of prefixed and unprefixed attributes */
START_TEST(test_ns_mixed_prefix_atts) {
  const char *text = "<e a='12' bar:b='13'\n"
                     " xmlns:bar='http://example.org/'>"
                     "</e>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test having a long namespaced element name inside a short one.
 * This exercises some internal buffer reallocation that is shared
 * across elements with the same namespace URI.
 */
START_TEST(test_ns_extend_uri_buffer) {
  const char *text = "<foo:e xmlns:foo='http://example.org/'>"
                     " <foo:thisisalongenoughnametotriggerallocationaction"
                     "   foo:a='12' />"
                     "</foo:e>";
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test that xmlns is correctly rejected as an attribute in the xmlns
 * namespace, but not in other namespaces
 */
START_TEST(test_ns_reserved_attributes) {
  const char *text1
      = "<foo:e xmlns:foo='http://example.org/' xmlns:xmlns='12' />";
  const char *text2
      = "<foo:e xmlns:foo='http://example.org/' foo:xmlns='12' />";
  expect_failure(text1, XML_ERROR_RESERVED_PREFIX_XMLNS,
                 "xmlns not rejected as an attribute");
  XML_ParserReset(g_parser, NULL);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text2, (int)strlen(text2), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test more reserved attributes */
START_TEST(test_ns_reserved_attributes_2) {
  const char *text1 = "<foo:e xmlns:foo='http://example.org/'"
                      "  xmlns:xml='http://example.org/' />";
  const char *text2
      = "<foo:e xmlns:foo='http://www.w3.org/XML/1998/namespace' />";
  const char *text3 = "<foo:e xmlns:foo='http://www.w3.org/2000/xmlns/' />";

  expect_failure(text1, XML_ERROR_RESERVED_PREFIX_XML,
                 "xml not rejected as an attribute");
  XML_ParserReset(g_parser, NULL);
  expect_failure(text2, XML_ERROR_RESERVED_NAMESPACE_URI,
                 "Use of w3.org URL not faulted");
  XML_ParserReset(g_parser, NULL);
  expect_failure(text3, XML_ERROR_RESERVED_NAMESPACE_URI,
                 "Use of w3.org xmlns URL not faulted");
}
END_TEST

/* Test string pool handling of namespace names of 2048 characters */
/* Exercises a particular string pool growth path */
START_TEST(test_ns_extremely_long_prefix) {
  /* C99 compilers are only required to support 4095-character
   * strings, so the following needs to be split in two to be safe
   * for all compilers.
   */
  const char *text1
      = "<doc "
        /* 64 character on each line */
        /* ...gives a total length of 2048 */
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
        ":a='12'";
  const char *text2
      = " xmlns:"
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
        "='foo'\n>"
        "</doc>";

  if (_XML_Parse_SINGLE_BYTES(g_parser, text1, (int)strlen(text1), XML_FALSE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text2, (int)strlen(text2), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
}
END_TEST

/* Test unknown encoding handlers in namespace setup */
START_TEST(test_ns_unknown_encoding_success) {
  const char *text = "<?xml version='1.0' encoding='prefix-conv'?>\n"
                     "<foo:e xmlns:foo='http://example.org/'>Hi</foo:e>";

  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  run_character_check(text, XCS("Hi"));
}
END_TEST

/* Test that too many colons are rejected */
START_TEST(test_ns_double_colon) {
  const char *text = "<foo:e xmlns:foo='http://example.org/' foo:a:b='bar' />";
  const enum XML_Status status
      = _XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE);
#ifdef XML_NS
  if ((status == XML_STATUS_OK)
      || (XML_GetErrorCode(g_parser) != XML_ERROR_INVALID_TOKEN)) {
    fail("Double colon in attribute name not faulted"
         " (despite active namespace support)");
  }
#else
  if (status != XML_STATUS_OK) {
    fail("Double colon in attribute name faulted"
         " (despite inactive namespace support");
  }
#endif
}
END_TEST

START_TEST(test_ns_double_colon_element) {
  const char *text = "<foo:bar:e xmlns:foo='http://example.org/' />";
  const enum XML_Status status
      = _XML_Parse_SINGLE_BYTES(g_parser, text, (int)strlen(text), XML_TRUE);
#ifdef XML_NS
  if ((status == XML_STATUS_OK)
      || (XML_GetErrorCode(g_parser) != XML_ERROR_INVALID_TOKEN)) {
    fail("Double colon in element name not faulted"
         " (despite active namespace support)");
  }
#else
  if (status != XML_STATUS_OK) {
    fail("Double colon in element name faulted"
         " (despite inactive namespace support");
  }
#endif
}
END_TEST

/* Test that non-name characters after a colon are rejected */
START_TEST(test_ns_bad_attr_leafname) {
  const char *text = "<foo:e xmlns:foo='http://example.org/' foo:?ar='baz' />";

  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Invalid character in leafname not faulted");
}
END_TEST

START_TEST(test_ns_bad_element_leafname) {
  const char *text = "<foo:?oc xmlns:foo='http://example.org/' />";

  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Invalid character in element leafname not faulted");
}
END_TEST

/* Test high-byte-set UTF-16 characters are valid in a leafname */
START_TEST(test_ns_utf16_leafname) {
  const char text[] =
      /* <n:e xmlns:n='URI' n:{KHO KHWAI}='a' />
       * where {KHO KHWAI} = U+0E04 = 0xe0 0xb8 0x84 in UTF-8
       */
      "<\0n\0:\0e\0 \0x\0m\0l\0n\0s\0:\0n\0=\0'\0U\0R\0I\0'\0 \0"
      "n\0:\0\x04\x0e=\0'\0a\0'\0 \0/\0>\0";
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

START_TEST(test_ns_utf16_element_leafname) {
  const char text[] =
      /* <n:{KHO KHWAI} xmlns:n='URI'/>
       * where {KHO KHWAI} = U+0E04 = 0xe0 0xb8 0x84 in UTF-8
       */
      "\0<\0n\0:\x0e\x04\0 \0x\0m\0l\0n\0s\0:\0n\0=\0'\0U\0R\0I\0'\0/\0>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("URI \x0e04");
#else
  const XML_Char *expected = XCS("URI \xe0\xb8\x84");
#endif
  CharData storage;

  CharData_Init(&storage);
  XML_SetStartElementHandler(g_parser, start_element_event_handler);
  XML_SetUserData(g_parser, &storage);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_ns_utf16_doctype) {
  const char text[] =
      /* <!DOCTYPE foo:{KHO KHWAI} [ <!ENTITY bar 'baz'> ]>\n
       * where {KHO KHWAI} = U+0E04 = 0xe0 0xb8 0x84 in UTF-8
       */
      "\0<\0!\0D\0O\0C\0T\0Y\0P\0E\0 \0f\0o\0o\0:\x0e\x04\0 "
      "\0[\0 \0<\0!\0E\0N\0T\0I\0T\0Y\0 \0b\0a\0r\0 \0'\0b\0a\0z\0'\0>\0 "
      "\0]\0>\0\n"
      /* <foo:{KHO KHWAI} xmlns:foo='URI'>&bar;</foo:{KHO KHWAI}> */
      "\0<\0f\0o\0o\0:\x0e\x04\0 "
      "\0x\0m\0l\0n\0s\0:\0f\0o\0o\0=\0'\0U\0R\0I\0'\0>"
      "\0&\0b\0a\0r\0;"
      "\0<\0/\0f\0o\0o\0:\x0e\x04\0>";
#ifdef XML_UNICODE
  const XML_Char *expected = XCS("URI \x0e04");
#else
  const XML_Char *expected = XCS("URI \xe0\xb8\x84");
#endif
  CharData storage;

  CharData_Init(&storage);
  XML_SetUserData(g_parser, &storage);
  XML_SetStartElementHandler(g_parser, start_element_event_handler);
  XML_SetUnknownEncodingHandler(g_parser, MiscEncodingHandler, NULL);
  if (_XML_Parse_SINGLE_BYTES(g_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(g_parser);
  CharData_CheckXMLChars(&storage, expected);
}
END_TEST

START_TEST(test_ns_invalid_doctype) {
  const char *text = "<!DOCTYPE foo:!bad [ <!ENTITY bar 'baz' ]>\n"
                     "<foo:!bad>&bar;</foo:!bad>";

  expect_failure(text, XML_ERROR_INVALID_TOKEN,
                 "Invalid character in document local name not faulted");
}
END_TEST

START_TEST(test_ns_double_colon_doctype) {
  const char *text = "<!DOCTYPE foo:a:doc [ <!ENTITY bar 'baz' ]>\n"
                     "<foo:a:doc>&bar;</foo:a:doc>";

  expect_failure(text, XML_ERROR_SYNTAX,
                 "Double colon in document name not faulted");
}
END_TEST

START_TEST(test_ns_separator_in_uri) {
  struct test_case {
    enum XML_Status expectedStatus;
    const char *doc;
    XML_Char namesep;
  };
  struct test_case cases[] = {
      {XML_STATUS_OK, "<doc xmlns='one_two' />", XCS('\n')},
      {XML_STATUS_ERROR, "<doc xmlns='one&#x0A;two' />", XCS('\n')},
      {XML_STATUS_OK, "<doc xmlns='one:two' />", XCS(':')},
  };

  size_t i = 0;
  size_t failCount = 0;
  for (; i < sizeof(cases) / sizeof(cases[0]); i++) {
    set_subtest("%s", cases[i].doc);
    XML_Parser parser = XML_ParserCreateNS(NULL, cases[i].namesep);
    XML_SetElementHandler(parser, dummy_start_element, dummy_end_element);
    if (_XML_Parse_SINGLE_BYTES(parser, cases[i].doc, (int)strlen(cases[i].doc),
                                /*isFinal*/ XML_TRUE)
        != cases[i].expectedStatus) {
      failCount++;
    }
    XML_ParserFree(parser);
  }

  if (failCount) {
    fail("Namespace separator handling is broken");
  }
}
END_TEST

void
make_namespace_test_case(Suite *s) {
  TCase *tc_namespace = tcase_create("XML namespaces");

  suite_add_tcase(s, tc_namespace);
  tcase_add_checked_fixture(tc_namespace, namespace_setup, namespace_teardown);
  tcase_add_test(tc_namespace, test_return_ns_triplet);
  tcase_add_test(tc_namespace, test_ns_parser_reset);
  tcase_add_test(tc_namespace, test_ns_tagname_overwrite);
  tcase_add_test(tc_namespace, test_ns_tagname_overwrite_triplet);
  tcase_add_test(tc_namespace, test_start_ns_clears_start_element);
  tcase_add_test__ifdef_xml_dtd(tc_namespace,
                                test_default_ns_from_ext_subset_and_ext_ge);
  tcase_add_test(tc_namespace, test_ns_prefix_with_empty_uri_1);
  tcase_add_test(tc_namespace, test_ns_prefix_with_empty_uri_2);
  tcase_add_test(tc_namespace, test_ns_prefix_with_empty_uri_3);
  tcase_add_test(tc_namespace, test_ns_prefix_with_empty_uri_4);
  tcase_add_test(tc_namespace, test_ns_unbound_prefix);
  tcase_add_test(tc_namespace, test_ns_default_with_empty_uri);
  tcase_add_test(tc_namespace, test_ns_duplicate_attrs_diff_prefixes);
  tcase_add_test(tc_namespace, test_ns_duplicate_hashes);
  tcase_add_test(tc_namespace, test_ns_unbound_prefix_on_attribute);
  tcase_add_test(tc_namespace, test_ns_unbound_prefix_on_element);
  tcase_add_test(tc_namespace, test_ns_long_element);
  tcase_add_test(tc_namespace, test_ns_mixed_prefix_atts);
  tcase_add_test(tc_namespace, test_ns_extend_uri_buffer);
  tcase_add_test(tc_namespace, test_ns_reserved_attributes);
  tcase_add_test(tc_namespace, test_ns_reserved_attributes_2);
  tcase_add_test(tc_namespace, test_ns_extremely_long_prefix);
  tcase_add_test(tc_namespace, test_ns_unknown_encoding_success);
  tcase_add_test(tc_namespace, test_ns_double_colon);
  tcase_add_test(tc_namespace, test_ns_double_colon_element);
  tcase_add_test(tc_namespace, test_ns_bad_attr_leafname);
  tcase_add_test(tc_namespace, test_ns_bad_element_leafname);
  tcase_add_test(tc_namespace, test_ns_utf16_leafname);
  tcase_add_test(tc_namespace, test_ns_utf16_element_leafname);
  tcase_add_test__if_xml_ge(tc_namespace, test_ns_utf16_doctype);
  tcase_add_test(tc_namespace, test_ns_invalid_doctype);
  tcase_add_test(tc_namespace, test_ns_double_colon_doctype);
  tcase_add_test(tc_namespace, test_ns_separator_in_uri);
}
