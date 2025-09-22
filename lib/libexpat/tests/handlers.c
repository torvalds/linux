/* XML handler functions for the Expat test suite
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

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "expat_config.h"

#include "expat.h"
#include "internal.h"
#include "chardata.h"
#include "structdata.h"
#include "common.h"
#include "handlers.h"

/* Global variables for user parameter settings tests */
/* Variable holding the expected handler userData */
const void *g_handler_data = NULL;
/* Count of the number of times the comment handler has been invoked */
int g_comment_count = 0;
/* Count of the number of skipped entities */
int g_skip_count = 0;
/* Count of the number of times the XML declaration handler is invoked */
int g_xdecl_count = 0;

/* Start/End Element Handlers */

void XMLCALL
start_element_event_handler(void *userData, const XML_Char *name,
                            const XML_Char **atts) {
  UNUSED_P(atts);
  CharData_AppendXMLChars((CharData *)userData, name, -1);
}

void XMLCALL
end_element_event_handler(void *userData, const XML_Char *name) {
  CharData *storage = (CharData *)userData;
  CharData_AppendXMLChars(storage, XCS("/"), 1);
  CharData_AppendXMLChars(storage, name, -1);
}

void XMLCALL
start_element_event_handler2(void *userData, const XML_Char *name,
                             const XML_Char **attr) {
  StructData *storage = (StructData *)userData;
  UNUSED_P(attr);
  StructData_AddItem(storage, name, (int)XML_GetCurrentColumnNumber(g_parser),
                     (int)XML_GetCurrentLineNumber(g_parser), STRUCT_START_TAG);
}

void XMLCALL
end_element_event_handler2(void *userData, const XML_Char *name) {
  StructData *storage = (StructData *)userData;
  StructData_AddItem(storage, name, (int)XML_GetCurrentColumnNumber(g_parser),
                     (int)XML_GetCurrentLineNumber(g_parser), STRUCT_END_TAG);
}

void XMLCALL
counting_start_element_handler(void *userData, const XML_Char *name,
                               const XML_Char **atts) {
  ParserAndElementInfo *const parserAndElementInfos
      = (ParserAndElementInfo *)userData;
  ElementInfo *info = parserAndElementInfos->info;
  AttrInfo *attr;
  int count, id, i;

  while (info->name != NULL) {
    if (! xcstrcmp(name, info->name))
      break;
    info++;
  }
  if (info->name == NULL)
    fail("Element not recognised");
  /* The attribute count is twice what you might expect.  It is a
   * count of items in atts, an array which contains alternating
   * attribute names and attribute values.  For the naive user this
   * is possibly a little unexpected, but it is what the
   * documentation in expat.h tells us to expect.
   */
  count = XML_GetSpecifiedAttributeCount(parserAndElementInfos->parser);
  if (info->attr_count * 2 != count) {
    fail("Not got expected attribute count");
    return;
  }
  id = XML_GetIdAttributeIndex(parserAndElementInfos->parser);
  if (id == -1 && info->id_name != NULL) {
    fail("ID not present");
    return;
  }
  if (id != -1 && xcstrcmp(atts[id], info->id_name) != 0) {
    fail("ID does not have the correct name");
    return;
  }
  for (i = 0; i < info->attr_count; i++) {
    attr = info->attributes;
    while (attr->name != NULL) {
      if (! xcstrcmp(atts[0], attr->name))
        break;
      attr++;
    }
    if (attr->name == NULL) {
      fail("Attribute not recognised");
      return;
    }
    if (xcstrcmp(atts[1], attr->value) != 0) {
      fail("Attribute has wrong value");
      return;
    }
    /* Remember, two entries in atts per attribute (see above) */
    atts += 2;
  }
}

void XMLCALL
suspending_end_handler(void *userData, const XML_Char *s) {
  UNUSED_P(s);
  XML_StopParser((XML_Parser)userData, 1);
}

void XMLCALL
start_element_suspender(void *userData, const XML_Char *name,
                        const XML_Char **atts) {
  UNUSED_P(userData);
  UNUSED_P(atts);
  if (! xcstrcmp(name, XCS("suspend")))
    XML_StopParser(g_parser, XML_TRUE);
  if (! xcstrcmp(name, XCS("abort")))
    XML_StopParser(g_parser, XML_FALSE);
}

/* Check that an element name and attribute name match the expected values.
   The expected values are passed as an array reference of string pointers
   provided as the userData argument; the first is the expected
   element name, and the second is the expected attribute name.
*/
int g_triplet_start_flag = XML_FALSE;
int g_triplet_end_flag = XML_FALSE;

void XMLCALL
triplet_start_checker(void *userData, const XML_Char *name,
                      const XML_Char **atts) {
  XML_Char **elemstr = (XML_Char **)userData;
  char buffer[1024];
  if (xcstrcmp(elemstr[0], name) != 0) {
    snprintf(buffer, sizeof(buffer),
             "unexpected start string: '%" XML_FMT_STR "'", name);
    fail(buffer);
  }
  if (xcstrcmp(elemstr[1], atts[0]) != 0) {
    snprintf(buffer, sizeof(buffer),
             "unexpected attribute string: '%" XML_FMT_STR "'", atts[0]);
    fail(buffer);
  }
  g_triplet_start_flag = XML_TRUE;
}

/* Check that the element name passed to the end-element handler matches
   the expected value.  The expected value is passed as the first element
   in an array of strings passed as the userData argument.
*/
void XMLCALL
triplet_end_checker(void *userData, const XML_Char *name) {
  XML_Char **elemstr = (XML_Char **)userData;
  if (xcstrcmp(elemstr[0], name) != 0) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer),
             "unexpected end string: '%" XML_FMT_STR "'", name);
    fail(buffer);
  }
  g_triplet_end_flag = XML_TRUE;
}

void XMLCALL
overwrite_start_checker(void *userData, const XML_Char *name,
                        const XML_Char **atts) {
  CharData *storage = (CharData *)userData;
  CharData_AppendXMLChars(storage, XCS("start "), 6);
  CharData_AppendXMLChars(storage, name, -1);
  while (*atts != NULL) {
    CharData_AppendXMLChars(storage, XCS("\nattribute "), 11);
    CharData_AppendXMLChars(storage, *atts, -1);
    atts += 2;
  }
  CharData_AppendXMLChars(storage, XCS("\n"), 1);
}

void XMLCALL
overwrite_end_checker(void *userData, const XML_Char *name) {
  CharData *storage = (CharData *)userData;
  CharData_AppendXMLChars(storage, XCS("end "), 4);
  CharData_AppendXMLChars(storage, name, -1);
  CharData_AppendXMLChars(storage, XCS("\n"), 1);
}

void XMLCALL
start_element_fail(void *userData, const XML_Char *name,
                   const XML_Char **atts) {
  UNUSED_P(userData);
  UNUSED_P(name);
  UNUSED_P(atts);

  /* We should never get here. */
  fail("should never reach start_element_fail()");
}

void XMLCALL
start_ns_clearing_start_element(void *userData, const XML_Char *prefix,
                                const XML_Char *uri) {
  UNUSED_P(prefix);
  UNUSED_P(uri);
  XML_SetStartElementHandler((XML_Parser)userData, NULL);
}

void XMLCALL
start_element_issue_240(void *userData, const XML_Char *name,
                        const XML_Char **atts) {
  DataIssue240 *mydata = (DataIssue240 *)userData;
  UNUSED_P(name);
  UNUSED_P(atts);
  mydata->deep++;
}

void XMLCALL
end_element_issue_240(void *userData, const XML_Char *name) {
  DataIssue240 *mydata = (DataIssue240 *)userData;

  UNUSED_P(name);
  mydata->deep--;
  if (mydata->deep == 0) {
    XML_StopParser(mydata->parser, 0);
  }
}

/* Text encoding handlers */

int XMLCALL
UnknownEncodingHandler(void *data, const XML_Char *encoding,
                       XML_Encoding *info) {
  UNUSED_P(data);
  if (xcstrcmp(encoding, XCS("unsupported-encoding")) == 0) {
    int i;
    for (i = 0; i < 256; ++i)
      info->map[i] = i;
    info->data = NULL;
    info->convert = NULL;
    info->release = NULL;
    return XML_STATUS_OK;
  }
  return XML_STATUS_ERROR;
}

static void
dummy_release(void *data) {
  UNUSED_P(data);
}

int XMLCALL
UnrecognisedEncodingHandler(void *data, const XML_Char *encoding,
                            XML_Encoding *info) {
  UNUSED_P(data);
  UNUSED_P(encoding);
  info->data = NULL;
  info->convert = NULL;
  info->release = dummy_release;
  return XML_STATUS_ERROR;
}

int XMLCALL
unknown_released_encoding_handler(void *data, const XML_Char *encoding,
                                  XML_Encoding *info) {
  UNUSED_P(data);
  if (! xcstrcmp(encoding, XCS("unsupported-encoding"))) {
    int i;

    for (i = 0; i < 256; i++)
      info->map[i] = i;
    info->data = NULL;
    info->convert = NULL;
    info->release = dummy_release;
    return XML_STATUS_OK;
  }
  return XML_STATUS_ERROR;
}

static int XMLCALL
failing_converter(void *data, const char *s) {
  UNUSED_P(data);
  UNUSED_P(s);
  /* Always claim to have failed */
  return -1;
}

static int XMLCALL
prefix_converter(void *data, const char *s) {
  UNUSED_P(data);
  /* If the first byte is 0xff, raise an error */
  if (s[0] == (char)-1)
    return -1;
  /* Just add the low bits of the first byte to the second */
  return (s[1] + (s[0] & 0x7f)) & 0x01ff;
}

int XMLCALL
MiscEncodingHandler(void *data, const XML_Char *encoding, XML_Encoding *info) {
  int i;
  int high_map = -2; /* Assume a 2-byte sequence */

  if (! xcstrcmp(encoding, XCS("invalid-9"))
      || ! xcstrcmp(encoding, XCS("ascii-like"))
      || ! xcstrcmp(encoding, XCS("invalid-len"))
      || ! xcstrcmp(encoding, XCS("invalid-a"))
      || ! xcstrcmp(encoding, XCS("invalid-surrogate"))
      || ! xcstrcmp(encoding, XCS("invalid-high")))
    high_map = -1;

  for (i = 0; i < 128; ++i)
    info->map[i] = i;
  for (; i < 256; ++i)
    info->map[i] = high_map;

  /* If required, put an invalid value in the ASCII entries */
  if (! xcstrcmp(encoding, XCS("invalid-9")))
    info->map[9] = 5;
  /* If required, have a top-bit set character starts a 5-byte sequence */
  if (! xcstrcmp(encoding, XCS("invalid-len")))
    info->map[0x81] = -5;
  /* If required, make a top-bit set character a valid ASCII character */
  if (! xcstrcmp(encoding, XCS("invalid-a")))
    info->map[0x82] = 'a';
  /* If required, give a top-bit set character a forbidden value,
   * what would otherwise be the first of a surrogate pair.
   */
  if (! xcstrcmp(encoding, XCS("invalid-surrogate")))
    info->map[0x83] = 0xd801;
  /* If required, give a top-bit set character too high a value */
  if (! xcstrcmp(encoding, XCS("invalid-high")))
    info->map[0x84] = 0x010101;

  info->data = data;
  info->release = NULL;
  if (! xcstrcmp(encoding, XCS("failing-conv")))
    info->convert = failing_converter;
  else if (! xcstrcmp(encoding, XCS("prefix-conv")))
    info->convert = prefix_converter;
  else
    info->convert = NULL;
  return XML_STATUS_OK;
}

int XMLCALL
long_encoding_handler(void *userData, const XML_Char *encoding,
                      XML_Encoding *info) {
  int i;

  UNUSED_P(userData);
  UNUSED_P(encoding);
  for (i = 0; i < 256; i++)
    info->map[i] = i;
  info->data = NULL;
  info->convert = NULL;
  info->release = NULL;
  return XML_STATUS_OK;
}

/* External Entity Handlers */

int XMLCALL
external_entity_optioner(XML_Parser parser, const XML_Char *context,
                         const XML_Char *base, const XML_Char *systemId,
                         const XML_Char *publicId) {
  ExtOption *options = (ExtOption *)XML_GetUserData(parser);
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(publicId);
  while (options->parse_text != NULL) {
    if (! xcstrcmp(systemId, options->system_id)) {
      enum XML_Status rc;
      ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
      if (ext_parser == NULL)
        return XML_STATUS_ERROR;
      rc = _XML_Parse_SINGLE_BYTES(ext_parser, options->parse_text,
                                   (int)strlen(options->parse_text), XML_TRUE);
      XML_ParserFree(ext_parser);
      return rc;
    }
    options++;
  }
  fail("No suitable option found");
  return XML_STATUS_ERROR;
}

int XMLCALL
external_entity_loader(XML_Parser parser, const XML_Char *context,
                       const XML_Char *base, const XML_Char *systemId,
                       const XML_Char *publicId) {
  ExtTest *test_data = (ExtTest *)XML_GetUserData(parser);
  XML_Parser extparser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  extparser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (extparser == NULL)
    fail("Could not create external entity parser.");
  if (test_data->encoding != NULL) {
    if (! XML_SetEncoding(extparser, test_data->encoding))
      fail("XML_SetEncoding() ignored for external entity");
  }
  if (_XML_Parse_SINGLE_BYTES(extparser, test_data->parse_text,
                              (int)strlen(test_data->parse_text), XML_TRUE)
      == XML_STATUS_ERROR) {
    xml_failure(extparser);
    return XML_STATUS_ERROR;
  }
  XML_ParserFree(extparser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_faulter(XML_Parser parser, const XML_Char *context,
                        const XML_Char *base, const XML_Char *systemId,
                        const XML_Char *publicId) {
  XML_Parser ext_parser;
  ExtFaults *fault = (ExtFaults *)XML_GetUserData(parser);

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  if (fault->encoding != NULL) {
    if (! XML_SetEncoding(ext_parser, fault->encoding))
      fail("XML_SetEncoding failed");
  }
  if (_XML_Parse_SINGLE_BYTES(ext_parser, fault->parse_text,
                              (int)strlen(fault->parse_text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail(fault->fail_text);
  if (XML_GetErrorCode(ext_parser) != fault->error)
    xml_failure(ext_parser);

  XML_ParserFree(ext_parser);
  return XML_STATUS_ERROR;
}

int XMLCALL
external_entity_null_loader(XML_Parser parser, const XML_Char *context,
                            const XML_Char *base, const XML_Char *systemId,
                            const XML_Char *publicId) {
  UNUSED_P(parser);
  UNUSED_P(context);
  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_resetter(XML_Parser parser, const XML_Char *context,
                         const XML_Char *base, const XML_Char *systemId,
                         const XML_Char *publicId) {
  const char *text = "<!ELEMENT doc (#PCDATA)*>";
  XML_Parser ext_parser;
  XML_ParsingStatus status;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  XML_GetParsingStatus(ext_parser, &status);
  if (status.parsing != XML_INITIALIZED) {
    fail("Parsing status is not INITIALIZED");
    return XML_STATUS_ERROR;
  }
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR) {
    xml_failure(parser);
    return XML_STATUS_ERROR;
  }
  XML_GetParsingStatus(ext_parser, &status);
  if (status.parsing != XML_FINISHED) {
    fail("Parsing status is not FINISHED");
    return XML_STATUS_ERROR;
  }
  /* Check we can't parse here */
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Parsing when finished not faulted");
  if (XML_GetErrorCode(ext_parser) != XML_ERROR_FINISHED)
    fail("Parsing when finished faulted with wrong code");
  XML_ParserReset(ext_parser, NULL);
  XML_GetParsingStatus(ext_parser, &status);
  if (status.parsing != XML_FINISHED) {
    fail("Parsing status not still FINISHED");
    return XML_STATUS_ERROR;
  }
  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

void XMLCALL
entity_suspending_decl_handler(void *userData, const XML_Char *name,
                               XML_Content *model) {
  XML_Parser ext_parser = (XML_Parser)userData;

  UNUSED_P(name);
  if (XML_StopParser(ext_parser, XML_TRUE) != XML_STATUS_ERROR)
    fail("Attempting to suspend a subordinate parser not faulted");
  if (XML_GetErrorCode(ext_parser) != XML_ERROR_SUSPEND_PE)
    fail("Suspending subordinate parser get wrong code");
  XML_SetElementDeclHandler(ext_parser, NULL);
  XML_FreeContentModel(g_parser, model);
}

int XMLCALL
external_entity_suspender(XML_Parser parser, const XML_Char *context,
                          const XML_Char *base, const XML_Char *systemId,
                          const XML_Char *publicId) {
  const char *text = "<!ELEMENT doc (#PCDATA)*>";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  XML_SetElementDeclHandler(ext_parser, entity_suspending_decl_handler);
  XML_SetUserData(ext_parser, ext_parser);
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR) {
    xml_failure(ext_parser);
    return XML_STATUS_ERROR;
  }
  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

void XMLCALL
entity_suspending_xdecl_handler(void *userData, const XML_Char *version,
                                const XML_Char *encoding, int standalone) {
  XML_Parser ext_parser = (XML_Parser)userData;

  UNUSED_P(version);
  UNUSED_P(encoding);
  UNUSED_P(standalone);
  XML_StopParser(ext_parser, g_resumable);
  XML_SetXmlDeclHandler(ext_parser, NULL);
}

int XMLCALL
external_entity_suspend_xmldecl(XML_Parser parser, const XML_Char *context,
                                const XML_Char *base, const XML_Char *systemId,
                                const XML_Char *publicId) {
  const char *text = "<?xml version='1.0' encoding='us-ascii'?>";
  XML_Parser ext_parser;
  XML_ParsingStatus status;
  enum XML_Status rc;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  XML_SetXmlDeclHandler(ext_parser, entity_suspending_xdecl_handler);
  XML_SetUserData(ext_parser, ext_parser);
  rc = _XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE);
  XML_GetParsingStatus(ext_parser, &status);
  if (g_resumable) {
    if (rc == XML_STATUS_ERROR)
      xml_failure(ext_parser);
    if (status.parsing != XML_SUSPENDED)
      fail("Ext Parsing status not SUSPENDED");
  } else {
    if (rc != XML_STATUS_ERROR)
      fail("Ext parsing not aborted");
    if (XML_GetErrorCode(ext_parser) != XML_ERROR_ABORTED)
      xml_failure(ext_parser);
    if (status.parsing != XML_FINISHED)
      fail("Ext Parsing status not FINISHED");
  }

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_suspending_faulter(XML_Parser parser, const XML_Char *context,
                                   const XML_Char *base,
                                   const XML_Char *systemId,
                                   const XML_Char *publicId) {
  XML_Parser ext_parser;
  ExtFaults *fault = (ExtFaults *)XML_GetUserData(parser);
  void *buffer;
  int parse_len = (int)strlen(fault->parse_text);

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  XML_SetXmlDeclHandler(ext_parser, entity_suspending_xdecl_handler);
  XML_SetUserData(ext_parser, ext_parser);
  g_resumable = XML_TRUE;
  buffer = XML_GetBuffer(ext_parser, parse_len);
  if (buffer == NULL)
    fail("Could not allocate parse buffer");
  assert(buffer != NULL);
  memcpy(buffer, fault->parse_text, parse_len);
  if (XML_ParseBuffer(ext_parser, parse_len, XML_FALSE) != XML_STATUS_SUSPENDED)
    fail("XML declaration did not suspend");
  if (XML_ResumeParser(ext_parser) != XML_STATUS_OK)
    xml_failure(ext_parser);
  if (XML_ParseBuffer(ext_parser, 0, XML_TRUE) != XML_STATUS_ERROR)
    fail(fault->fail_text);
  if (XML_GetErrorCode(ext_parser) != fault->error)
    xml_failure(ext_parser);

  XML_ParserFree(ext_parser);
  return XML_STATUS_ERROR;
}

int XMLCALL
external_entity_failer__if_not_xml_ge(XML_Parser parser,
                                      const XML_Char *context,
                                      const XML_Char *base,
                                      const XML_Char *systemId,
                                      const XML_Char *publicId) {
  UNUSED_P(parser);
  UNUSED_P(context);
  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
#if XML_GE == 0
  fail(
      "Function external_entity_suspending_failer was called despite XML_GE==0.");
#endif
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_cr_catcher(XML_Parser parser, const XML_Char *context,
                           const XML_Char *base, const XML_Char *systemId,
                           const XML_Char *publicId) {
  const char *text = "\r";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  XML_SetCharacterDataHandler(ext_parser, cr_cdata_handler);
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(ext_parser);
  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_bad_cr_catcher(XML_Parser parser, const XML_Char *context,
                               const XML_Char *base, const XML_Char *systemId,
                               const XML_Char *publicId) {
  const char *text = "<tag>\r";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  XML_SetCharacterDataHandler(ext_parser, cr_cdata_handler);
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_OK)
    fail("Async entity error not caught");
  if (XML_GetErrorCode(ext_parser) != XML_ERROR_ASYNC_ENTITY)
    xml_failure(ext_parser);
  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_rsqb_catcher(XML_Parser parser, const XML_Char *context,
                             const XML_Char *base, const XML_Char *systemId,
                             const XML_Char *publicId) {
  const char *text = "<tag>]";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  XML_SetCharacterDataHandler(ext_parser, rsqb_handler);
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      != XML_STATUS_ERROR)
    fail("Async entity error not caught");
  if (XML_GetErrorCode(ext_parser) != XML_ERROR_ASYNC_ENTITY)
    xml_failure(ext_parser);
  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_good_cdata_ascii(XML_Parser parser, const XML_Char *context,
                                 const XML_Char *base, const XML_Char *systemId,
                                 const XML_Char *publicId) {
  const char *text = "<a><![CDATA[<greeting>Hello, world!</greeting>]]></a>";
  const XML_Char *expected = XCS("<greeting>Hello, world!</greeting>");
  CharData storage;
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  CharData_Init(&storage);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  XML_SetUserData(ext_parser, &storage);
  XML_SetCharacterDataHandler(ext_parser, accumulate_characters);

  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(ext_parser);
  CharData_CheckXMLChars(&storage, expected);

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_param_checker(XML_Parser parser, const XML_Char *context,
                              const XML_Char *base, const XML_Char *systemId,
                              const XML_Char *publicId) {
  const char *text = "<!-- Subordinate parser -->\n"
                     "<!ELEMENT doc (#PCDATA)*>";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  g_handler_data = ext_parser;
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR) {
    xml_failure(parser);
    return XML_STATUS_ERROR;
  }
  g_handler_data = parser;
  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_ref_param_checker(XML_Parser parameter, const XML_Char *context,
                                  const XML_Char *base,
                                  const XML_Char *systemId,
                                  const XML_Char *publicId) {
  const char *text = "<!ELEMENT doc (#PCDATA)*>";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  if ((void *)parameter != g_handler_data)
    fail("External entity ref handler parameter not correct");

  /* Here we use the global 'parser' variable */
  ext_parser = XML_ExternalEntityParserCreate(g_parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(ext_parser);

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_param(XML_Parser parser, const XML_Char *context,
                      const XML_Char *base, const XML_Char *systemId,
                      const XML_Char *publicId) {
  const char *text1 = "<!ELEMENT doc EMPTY>\n"
                      "<!ENTITY % e1 SYSTEM '004-2.ent'>\n"
                      "<!ENTITY % e2 '%e1;'>\n"
                      "%e1;\n";
  const char *text2 = "<!ELEMENT el EMPTY>\n"
                      "<el/>\n";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(publicId);
  if (systemId == NULL)
    return XML_STATUS_OK;

  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");

  if (! xcstrcmp(systemId, XCS("004-1.ent"))) {
    if (_XML_Parse_SINGLE_BYTES(ext_parser, text1, (int)strlen(text1), XML_TRUE)
        != XML_STATUS_ERROR)
      fail("Inner DTD with invalid tag not rejected");
    if (XML_GetErrorCode(ext_parser) != XML_ERROR_EXTERNAL_ENTITY_HANDLING)
      xml_failure(ext_parser);
  } else if (! xcstrcmp(systemId, XCS("004-2.ent"))) {
    if (_XML_Parse_SINGLE_BYTES(ext_parser, text2, (int)strlen(text2), XML_TRUE)
        != XML_STATUS_ERROR)
      fail("Invalid tag in external param not rejected");
    if (XML_GetErrorCode(ext_parser) != XML_ERROR_SYNTAX)
      xml_failure(ext_parser);
  } else {
    fail("Unknown system ID");
  }

  XML_ParserFree(ext_parser);
  return XML_STATUS_ERROR;
}

int XMLCALL
external_entity_load_ignore(XML_Parser parser, const XML_Char *context,
                            const XML_Char *base, const XML_Char *systemId,
                            const XML_Char *publicId) {
  const char *text = "<![IGNORE[<!ELEMENT e (#PCDATA)*>]]>";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(parser);

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_load_ignore_utf16(XML_Parser parser, const XML_Char *context,
                                  const XML_Char *base,
                                  const XML_Char *systemId,
                                  const XML_Char *publicId) {
  const char text[] =
      /* <![IGNORE[<!ELEMENT e (#PCDATA)*>]]> */
      "<\0!\0[\0I\0G\0N\0O\0R\0E\0[\0"
      "<\0!\0E\0L\0E\0M\0E\0N\0T\0 \0e\0 \0"
      "(\0#\0P\0C\0D\0A\0T\0A\0)\0*\0>\0]\0]\0>\0";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(parser);

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_load_ignore_utf16_be(XML_Parser parser, const XML_Char *context,
                                     const XML_Char *base,
                                     const XML_Char *systemId,
                                     const XML_Char *publicId) {
  const char text[] =
      /* <![IGNORE[<!ELEMENT e (#PCDATA)*>]]> */
      "\0<\0!\0[\0I\0G\0N\0O\0R\0E\0["
      "\0<\0!\0E\0L\0E\0M\0E\0N\0T\0 \0e\0 "
      "\0(\0#\0P\0C\0D\0A\0T\0A\0)\0*\0>\0]\0]\0>";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)sizeof(text) - 1, XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(parser);

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_valuer(XML_Parser parser, const XML_Char *context,
                       const XML_Char *base, const XML_Char *systemId,
                       const XML_Char *publicId) {
  const char *text1 = "<!ELEMENT doc EMPTY>\n"
                      "<!ENTITY % e1 SYSTEM '004-2.ent'>\n"
                      "<!ENTITY % e2 '%e1;'>\n"
                      "%e1;\n";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(publicId);
  if (systemId == NULL)
    return XML_STATUS_OK;
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  if (! xcstrcmp(systemId, XCS("004-1.ent"))) {
    if (_XML_Parse_SINGLE_BYTES(ext_parser, text1, (int)strlen(text1), XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(ext_parser);
  } else if (! xcstrcmp(systemId, XCS("004-2.ent"))) {
    ExtFaults *fault = (ExtFaults *)XML_GetUserData(parser);
    enum XML_Status status;
    enum XML_Error error;

    status = _XML_Parse_SINGLE_BYTES(ext_parser, fault->parse_text,
                                     (int)strlen(fault->parse_text), XML_TRUE);
    if (fault->error == XML_ERROR_NONE) {
      if (status == XML_STATUS_ERROR)
        xml_failure(ext_parser);
    } else {
      if (status != XML_STATUS_ERROR)
        fail(fault->fail_text);
      error = XML_GetErrorCode(ext_parser);
      if (error != fault->error
          && (fault->error != XML_ERROR_XML_DECL
              || error != XML_ERROR_TEXT_DECL))
        xml_failure(ext_parser);
    }
  }

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_not_standalone(XML_Parser parser, const XML_Char *context,
                               const XML_Char *base, const XML_Char *systemId,
                               const XML_Char *publicId) {
  const char *text1 = "<!ELEMENT doc EMPTY>\n"
                      "<!ENTITY % e1 SYSTEM 'bar'>\n"
                      "%e1;\n";
  const char *text2 = "<!ATTLIST doc a1 CDATA 'value'>";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(publicId);
  if (systemId == NULL)
    return XML_STATUS_OK;
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  if (! xcstrcmp(systemId, XCS("foo"))) {
    XML_SetNotStandaloneHandler(ext_parser, reject_not_standalone_handler);
    if (_XML_Parse_SINGLE_BYTES(ext_parser, text1, (int)strlen(text1), XML_TRUE)
        != XML_STATUS_ERROR)
      fail("Expected not standalone rejection");
    if (XML_GetErrorCode(ext_parser) != XML_ERROR_NOT_STANDALONE)
      xml_failure(ext_parser);
    XML_SetNotStandaloneHandler(ext_parser, NULL);
    XML_ParserFree(ext_parser);
    return XML_STATUS_ERROR;
  } else if (! xcstrcmp(systemId, XCS("bar"))) {
    if (_XML_Parse_SINGLE_BYTES(ext_parser, text2, (int)strlen(text2), XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(ext_parser);
  }

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_value_aborter(XML_Parser parser, const XML_Char *context,
                              const XML_Char *base, const XML_Char *systemId,
                              const XML_Char *publicId) {
  const char *text1 = "<!ELEMENT doc EMPTY>\n"
                      "<!ENTITY % e1 SYSTEM '004-2.ent'>\n"
                      "<!ENTITY % e2 '%e1;'>\n"
                      "%e1;\n";
  const char *text2 = "<?xml version='1.0' encoding='utf-8'?>";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(publicId);
  if (systemId == NULL)
    return XML_STATUS_OK;
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");
  if (! xcstrcmp(systemId, XCS("004-1.ent"))) {
    if (_XML_Parse_SINGLE_BYTES(ext_parser, text1, (int)strlen(text1), XML_TRUE)
        == XML_STATUS_ERROR)
      xml_failure(ext_parser);
  }
  if (! xcstrcmp(systemId, XCS("004-2.ent"))) {
    XML_SetXmlDeclHandler(ext_parser, entity_suspending_xdecl_handler);
    XML_SetUserData(ext_parser, ext_parser);
    if (_XML_Parse_SINGLE_BYTES(ext_parser, text2, (int)strlen(text2), XML_TRUE)
        != XML_STATUS_ERROR)
      fail("Aborted parse not faulted");
    if (XML_GetErrorCode(ext_parser) != XML_ERROR_ABORTED)
      xml_failure(ext_parser);
  }

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_public(XML_Parser parser, const XML_Char *context,
                       const XML_Char *base, const XML_Char *systemId,
                       const XML_Char *publicId) {
  const char *text1 = (const char *)XML_GetUserData(parser);
  const char *text2 = "<!ATTLIST doc a CDATA 'value'>";
  const char *text = NULL;
  XML_Parser ext_parser;
  int parse_res;

  UNUSED_P(base);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    return XML_STATUS_ERROR;
  if (systemId != NULL && ! xcstrcmp(systemId, XCS("http://example.org/"))) {
    text = text1;
  } else if (publicId != NULL && ! xcstrcmp(publicId, XCS("foo"))) {
    text = text2;
  } else
    fail("Unexpected parameters to external entity parser");
  assert(text != NULL);
  parse_res
      = _XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE);
  XML_ParserFree(ext_parser);
  return parse_res;
}

int XMLCALL
external_entity_devaluer(XML_Parser parser, const XML_Char *context,
                         const XML_Char *base, const XML_Char *systemId,
                         const XML_Char *publicId) {
  const char *text = "<!ELEMENT doc EMPTY>\n"
                     "<!ENTITY % e1 SYSTEM 'bar'>\n"
                     "%e1;\n";
  XML_Parser ext_parser;
  int clear_handler_flag = (XML_GetUserData(parser) != NULL);

  UNUSED_P(base);
  UNUSED_P(publicId);
  if (systemId == NULL || ! xcstrcmp(systemId, XCS("bar")))
    return XML_STATUS_OK;
  if (xcstrcmp(systemId, XCS("foo")) != 0)
    fail("Unexpected system ID");
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could note create external entity parser");
  if (clear_handler_flag)
    XML_SetExternalEntityRefHandler(ext_parser, NULL);
  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(ext_parser);

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_oneshot_loader(XML_Parser parser, const XML_Char *context,
                               const XML_Char *base, const XML_Char *systemId,
                               const XML_Char *publicId) {
  ExtHdlrData *test_data = (ExtHdlrData *)XML_GetUserData(parser);
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser.");
  /* Use the requested entity parser for further externals */
  XML_SetExternalEntityRefHandler(ext_parser, test_data->handler);
  if (_XML_Parse_SINGLE_BYTES(ext_parser, test_data->parse_text,
                              (int)strlen(test_data->parse_text), XML_TRUE)
      == XML_STATUS_ERROR) {
    xml_failure(ext_parser);
  }

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_loader2(XML_Parser parser, const XML_Char *context,
                        const XML_Char *base, const XML_Char *systemId,
                        const XML_Char *publicId) {
  ExtTest2 *test_data = (ExtTest2 *)XML_GetUserData(parser);
  XML_Parser extparser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  extparser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (extparser == NULL)
    fail("Coulr not create external entity parser");
  if (test_data->encoding != NULL) {
    if (! XML_SetEncoding(extparser, test_data->encoding))
      fail("XML_SetEncoding() ignored for external entity");
  }
  if (_XML_Parse_SINGLE_BYTES(extparser, test_data->parse_text,
                              test_data->parse_len, XML_TRUE)
      == XML_STATUS_ERROR) {
    xml_failure(extparser);
  }

  XML_ParserFree(extparser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_faulter2(XML_Parser parser, const XML_Char *context,
                         const XML_Char *base, const XML_Char *systemId,
                         const XML_Char *publicId) {
  ExtFaults2 *test_data = (ExtFaults2 *)XML_GetUserData(parser);
  XML_Parser extparser;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  extparser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (extparser == NULL)
    fail("Could not create external entity parser");
  if (test_data->encoding != NULL) {
    if (! XML_SetEncoding(extparser, test_data->encoding))
      fail("XML_SetEncoding() ignored for external entity");
  }
  if (_XML_Parse_SINGLE_BYTES(extparser, test_data->parse_text,
                              test_data->parse_len, XML_TRUE)
      != XML_STATUS_ERROR)
    fail(test_data->fail_text);
  if (XML_GetErrorCode(extparser) != test_data->error)
    xml_failure(extparser);

  XML_ParserFree(extparser);
  return XML_STATUS_ERROR;
}

int XMLCALL
external_entity_unfinished_attlist(XML_Parser parser, const XML_Char *context,
                                   const XML_Char *base,
                                   const XML_Char *systemId,
                                   const XML_Char *publicId) {
  const char *text = "<!ELEMENT barf ANY>\n"
                     "<!ATTLIST barf my_attr (blah|%blah;a|foo) #REQUIRED>\n"
                     "<!--COMMENT-->\n";
  XML_Parser ext_parser;

  UNUSED_P(base);
  UNUSED_P(publicId);
  if (systemId == NULL)
    return XML_STATUS_OK;

  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");

  if (_XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR)
    xml_failure(ext_parser);

  XML_ParserFree(ext_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_handler(XML_Parser parser, const XML_Char *context,
                        const XML_Char *base, const XML_Char *systemId,
                        const XML_Char *publicId) {
  void *user_data = XML_GetUserData(parser);
  const char *text;
  XML_Parser p2;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  if (user_data == NULL)
    text = ("<!ELEMENT doc (e+)>\n"
            "<!ATTLIST doc xmlns CDATA #IMPLIED>\n"
            "<!ELEMENT e EMPTY>\n");
  else
    text = ("<?xml version='1.0' encoding='us-ascii'?>"
            "<e/>");

  /* Set user data to any non-NULL value */
  XML_SetUserData(parser, parser);
  p2 = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (_XML_Parse_SINGLE_BYTES(p2, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR) {
    xml_failure(p2);
    return XML_STATUS_ERROR;
  }
  XML_ParserFree(p2);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_duff_loader(XML_Parser parser, const XML_Char *context,
                            const XML_Char *base, const XML_Char *systemId,
                            const XML_Char *publicId) {
  XML_Parser new_parser;
  unsigned int i;
  const unsigned int max_alloc_count = 10;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  /* Try a few different allocation levels */
  for (i = 0; i < max_alloc_count; i++) {
    g_allocation_count = (int)i;
    new_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
    if (new_parser != NULL) {
      XML_ParserFree(new_parser);
      break;
    }
  }
  if (i == 0)
    fail("External parser creation ignored failing allocator");
  else if (i == max_alloc_count)
    fail("Extern parser not created with max allocation count");

  /* Make sure other random allocation doesn't now fail */
  g_allocation_count = ALLOC_ALWAYS_SUCCEED;

  /* Make sure the failure code path is executed too */
  return XML_STATUS_ERROR;
}

int XMLCALL
external_entity_dbl_handler(XML_Parser parser, const XML_Char *context,
                            const XML_Char *base, const XML_Char *systemId,
                            const XML_Char *publicId) {
  int *pcallno = (int *)XML_GetUserData(parser);
  int callno = *pcallno;
  const char *text;
  XML_Parser new_parser = NULL;
  int i;
  const int max_alloc_count = 20;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  if (callno == 0) {
    /* First time through, check how many calls to malloc occur */
    text = ("<!ELEMENT doc (e+)>\n"
            "<!ATTLIST doc xmlns CDATA #IMPLIED>\n"
            "<!ELEMENT e EMPTY>\n");
    g_allocation_count = 10000;
    new_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
    if (new_parser == NULL) {
      fail("Unable to allocate first external parser");
      return XML_STATUS_ERROR;
    }
    /* Stash the number of calls in the user data */
    *pcallno = 10000 - g_allocation_count;
  } else {
    text = ("<?xml version='1.0' encoding='us-ascii'?>"
            "<e/>");
    /* Try at varying levels to exercise more code paths */
    for (i = 0; i < max_alloc_count; i++) {
      g_allocation_count = callno + i;
      new_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
      if (new_parser != NULL)
        break;
    }
    if (i == 0) {
      fail("Second external parser unexpectedly created");
      XML_ParserFree(new_parser);
      return XML_STATUS_ERROR;
    } else if (i == max_alloc_count) {
      fail("Second external parser not created");
      return XML_STATUS_ERROR;
    }
  }

  g_allocation_count = ALLOC_ALWAYS_SUCCEED;
  if (_XML_Parse_SINGLE_BYTES(new_parser, text, (int)strlen(text), XML_TRUE)
      == XML_STATUS_ERROR) {
    xml_failure(new_parser);
    return XML_STATUS_ERROR;
  }
  XML_ParserFree(new_parser);
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_dbl_handler_2(XML_Parser parser, const XML_Char *context,
                              const XML_Char *base, const XML_Char *systemId,
                              const XML_Char *publicId) {
  int *pcallno = (int *)XML_GetUserData(parser);
  int callno = *pcallno;
  const char *text;
  XML_Parser new_parser;
  enum XML_Status rv;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  if (callno == 0) {
    /* Try different allocation levels for whole exercise */
    text = ("<!ELEMENT doc (e+)>\n"
            "<!ATTLIST doc xmlns CDATA #IMPLIED>\n"
            "<!ELEMENT e EMPTY>\n");
    *pcallno = 1;
    new_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
    if (new_parser == NULL)
      return XML_STATUS_ERROR;
    rv = _XML_Parse_SINGLE_BYTES(new_parser, text, (int)strlen(text), XML_TRUE);
  } else {
    /* Just run through once */
    text = ("<?xml version='1.0' encoding='us-ascii'?>"
            "<e/>");
    new_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
    if (new_parser == NULL)
      return XML_STATUS_ERROR;
    rv = _XML_Parse_SINGLE_BYTES(new_parser, text, (int)strlen(text), XML_TRUE);
  }
  XML_ParserFree(new_parser);
  if (rv == XML_STATUS_ERROR)
    return XML_STATUS_ERROR;
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_alloc_set_encoding(XML_Parser parser, const XML_Char *context,
                                   const XML_Char *base,
                                   const XML_Char *systemId,
                                   const XML_Char *publicId) {
  /* As for external_entity_loader() */
  const char *text = "<?xml encoding='iso-8859-3'?>"
                     "\xC3\xA9";
  XML_Parser ext_parser;
  enum XML_Status status;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    return XML_STATUS_ERROR;
  if (! XML_SetEncoding(ext_parser, XCS("utf-8"))) {
    XML_ParserFree(ext_parser);
    return XML_STATUS_ERROR;
  }
  status
      = _XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE);
  XML_ParserFree(ext_parser);
  if (status == XML_STATUS_ERROR)
    return XML_STATUS_ERROR;
  return XML_STATUS_OK;
}

int XMLCALL
external_entity_reallocator(XML_Parser parser, const XML_Char *context,
                            const XML_Char *base, const XML_Char *systemId,
                            const XML_Char *publicId) {
  const char *text = get_buffer_test_text;
  XML_Parser ext_parser;
  void *buffer;
  enum XML_Status status;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    fail("Could not create external entity parser");

  g_reallocation_count = *(int *)XML_GetUserData(parser);
  buffer = XML_GetBuffer(ext_parser, 1536);
  if (buffer == NULL)
    fail("Buffer allocation failed");
  assert(buffer != NULL);
  memcpy(buffer, text, strlen(text));
  status = XML_ParseBuffer(ext_parser, (int)strlen(text), XML_FALSE);
  g_reallocation_count = -1;
  XML_ParserFree(ext_parser);
  return (status == XML_STATUS_OK) ? XML_STATUS_OK : XML_STATUS_ERROR;
}

int XMLCALL
external_entity_alloc(XML_Parser parser, const XML_Char *context,
                      const XML_Char *base, const XML_Char *systemId,
                      const XML_Char *publicId) {
  const char *text = (const char *)XML_GetUserData(parser);
  XML_Parser ext_parser;
  int parse_res;

  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  ext_parser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (ext_parser == NULL)
    return XML_STATUS_ERROR;
  parse_res
      = _XML_Parse_SINGLE_BYTES(ext_parser, text, (int)strlen(text), XML_TRUE);
  XML_ParserFree(ext_parser);
  return parse_res;
}

int XMLCALL
external_entity_parser_create_alloc_fail_handler(XML_Parser parser,
                                                 const XML_Char *context,
                                                 const XML_Char *base,
                                                 const XML_Char *systemId,
                                                 const XML_Char *publicId) {
  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);

  if (context != NULL)
    fail("Unexpected non-NULL context");

  // The following number intends to fail the upcoming allocation in line
  // "parser->m_protocolEncodingName = copyString(encodingName,
  // &(parser->m_mem));" in function parserInit.
  g_allocation_count = 3;

  const XML_Char *const encodingName = XCS("UTF-8"); // needs something non-NULL
  const XML_Parser ext_parser
      = XML_ExternalEntityParserCreate(parser, context, encodingName);
  if (ext_parser != NULL)
    fail(
        "Call to XML_ExternalEntityParserCreate was expected to fail out-of-memory");

  g_allocation_count = ALLOC_ALWAYS_SUCCEED;
  return XML_STATUS_ERROR;
}

#if XML_GE == 1
int
accounting_external_entity_ref_handler(XML_Parser parser,
                                       const XML_Char *context,
                                       const XML_Char *base,
                                       const XML_Char *systemId,
                                       const XML_Char *publicId) {
  UNUSED_P(base);
  UNUSED_P(publicId);

  const struct AccountingTestCase *const testCase
      = (const struct AccountingTestCase *)XML_GetUserData(parser);

  const char *externalText = NULL;
  if (xcstrcmp(systemId, XCS("first.ent")) == 0) {
    externalText = testCase->firstExternalText;
  } else if (xcstrcmp(systemId, XCS("second.ent")) == 0) {
    externalText = testCase->secondExternalText;
  } else {
    assert(! "systemId is neither \"first.ent\" nor \"second.ent\"");
  }
  assert(externalText);

  XML_Parser entParser = XML_ExternalEntityParserCreate(parser, context, 0);
  assert(entParser);

  const enum XML_Status status = _XML_Parse_SINGLE_BYTES(
      entParser, externalText, (int)strlen(externalText), XML_TRUE);

  XML_ParserFree(entParser);
  return status;
}
#endif /* XML_GE == 1 */

/* NotStandalone handlers */

int XMLCALL
reject_not_standalone_handler(void *userData) {
  UNUSED_P(userData);
  return XML_STATUS_ERROR;
}

int XMLCALL
accept_not_standalone_handler(void *userData) {
  UNUSED_P(userData);
  return XML_STATUS_OK;
}

/* Attribute List handlers */
void XMLCALL
verify_attlist_decl_handler(void *userData, const XML_Char *element_name,
                            const XML_Char *attr_name,
                            const XML_Char *attr_type,
                            const XML_Char *default_value, int is_required) {
  AttTest *at = (AttTest *)userData;

  if (xcstrcmp(element_name, at->element_name) != 0)
    fail("Unexpected element name in attribute declaration");
  if (xcstrcmp(attr_name, at->attr_name) != 0)
    fail("Unexpected attribute name in attribute declaration");
  if (xcstrcmp(attr_type, at->attr_type) != 0)
    fail("Unexpected attribute type in attribute declaration");
  if ((default_value == NULL && at->default_value != NULL)
      || (default_value != NULL && at->default_value == NULL)
      || (default_value != NULL
          && xcstrcmp(default_value, at->default_value) != 0))
    fail("Unexpected default value in attribute declaration");
  if (is_required != at->is_required)
    fail("Requirement mismatch in attribute declaration");
}

/* Character Data handlers */

void XMLCALL
clearing_aborting_character_handler(void *userData, const XML_Char *s,
                                    int len) {
  UNUSED_P(userData);
  UNUSED_P(s);
  UNUSED_P(len);
  XML_StopParser(g_parser, g_resumable);
  XML_SetCharacterDataHandler(g_parser, NULL);
}

void XMLCALL
parser_stop_character_handler(void *userData, const XML_Char *s, int len) {
  UNUSED_P(userData);
  UNUSED_P(s);
  UNUSED_P(len);
  XML_ParsingStatus status;
  XML_GetParsingStatus(g_parser, &status);
  if (status.parsing == XML_FINISHED) {
    return; // the parser was stopped by a previous call to this handler.
  }
  XML_StopParser(g_parser, g_resumable);
  XML_SetCharacterDataHandler(g_parser, NULL);
  if (! g_resumable) {
    /* Check that aborting an aborted parser is faulted */
    if (XML_StopParser(g_parser, XML_FALSE) != XML_STATUS_ERROR)
      fail("Aborting aborted parser not faulted");
    if (XML_GetErrorCode(g_parser) != XML_ERROR_FINISHED)
      xml_failure(g_parser);
  } else if (g_abortable) {
    /* Check that aborting a suspended parser works */
    if (XML_StopParser(g_parser, XML_FALSE) == XML_STATUS_ERROR)
      xml_failure(g_parser);
  } else {
    /* Check that suspending a suspended parser works */
    if (XML_StopParser(g_parser, XML_TRUE) != XML_STATUS_ERROR)
      fail("Suspending suspended parser not faulted");
    if (XML_GetErrorCode(g_parser) != XML_ERROR_SUSPENDED)
      xml_failure(g_parser);
  }
}

void XMLCALL
cr_cdata_handler(void *userData, const XML_Char *s, int len) {
  int *pfound = (int *)userData;

  /* Internal processing turns the CR into a newline for the
   * character data handler, but not for the default handler
   */
  if (len == 1 && (*s == XCS('\n') || *s == XCS('\r')))
    *pfound = 1;
}

void XMLCALL
rsqb_handler(void *userData, const XML_Char *s, int len) {
  int *pfound = (int *)userData;

  if (len == 1 && *s == XCS(']'))
    *pfound = 1;
}

void XMLCALL
byte_character_handler(void *userData, const XML_Char *s, int len) {
#if XML_CONTEXT_BYTES > 0
  int offset, size;
  const char *buffer;
  ByteTestData *data = (ByteTestData *)userData;

  UNUSED_P(s);
  buffer = XML_GetInputContext(g_parser, &offset, &size);
  if (buffer == NULL)
    fail("Failed to get context buffer");
  if (offset != data->start_element_len)
    fail("Context offset in unexpected position");
  if (len != data->cdata_len)
    fail("CDATA length reported incorrectly");
  if (size != data->total_string_len)
    fail("Context size is not full buffer");
  if (XML_GetCurrentByteIndex(g_parser) != offset)
    fail("Character byte index incorrect");
  if (XML_GetCurrentByteCount(g_parser) != len)
    fail("Character byte count incorrect");
#else
  UNUSED_P(s);
  UNUSED_P(userData);
  UNUSED_P(len);
#endif
}

void XMLCALL
ext2_accumulate_characters(void *userData, const XML_Char *s, int len) {
  ExtTest2 *test_data = (ExtTest2 *)userData;
  accumulate_characters(test_data->storage, s, len);
}

/* Handlers that record their function name and int arg. */

static void
record_call(struct handler_record_list *const rec, const char *funcname,
            const int arg) {
  const int max_entries = sizeof(rec->entries) / sizeof(rec->entries[0]);
  assert_true(rec->count < max_entries);
  struct handler_record_entry *const e = &rec->entries[rec->count++];
  e->name = funcname;
  e->arg = arg;
}

void XMLCALL
record_default_handler(void *userData, const XML_Char *s, int len) {
  UNUSED_P(s);
  record_call((struct handler_record_list *)userData, __func__, len);
}

void XMLCALL
record_cdata_handler(void *userData, const XML_Char *s, int len) {
  UNUSED_P(s);
  record_call((struct handler_record_list *)userData, __func__, len);
  XML_DefaultCurrent(g_parser);
}

void XMLCALL
record_cdata_nodefault_handler(void *userData, const XML_Char *s, int len) {
  UNUSED_P(s);
  record_call((struct handler_record_list *)userData, __func__, len);
}

void XMLCALL
record_skip_handler(void *userData, const XML_Char *entityName,
                    int is_parameter_entity) {
  UNUSED_P(entityName);
  record_call((struct handler_record_list *)userData, __func__,
              is_parameter_entity);
}

void XMLCALL
record_element_start_handler(void *userData, const XML_Char *name,
                             const XML_Char **atts) {
  UNUSED_P(atts);
  CharData_AppendXMLChars((CharData *)userData, name, (int)xcstrlen(name));
}

void XMLCALL
record_element_end_handler(void *userData, const XML_Char *name) {
  CharData *storage = (CharData *)userData;

  CharData_AppendXMLChars(storage, XCS("/"), 1);
  CharData_AppendXMLChars(storage, name, -1);
}

const struct handler_record_entry *
_handler_record_get(const struct handler_record_list *storage, int index,
                    const char *file, int line) {
  if (storage->count <= index) {
    _fail(file, line, "too few handler calls");
  }
  return &storage->entries[index];
}

/* Entity Declaration Handlers */
static const XML_Char *entity_name_to_match = NULL;
static const XML_Char *entity_value_to_match = NULL;
static int entity_match_flag = ENTITY_MATCH_NOT_FOUND;

void XMLCALL
param_entity_match_handler(void *userData, const XML_Char *entityName,
                           int is_parameter_entity, const XML_Char *value,
                           int value_length, const XML_Char *base,
                           const XML_Char *systemId, const XML_Char *publicId,
                           const XML_Char *notationName) {
  UNUSED_P(userData);
  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  UNUSED_P(notationName);
  if (! is_parameter_entity || entity_name_to_match == NULL
      || entity_value_to_match == NULL) {
    return;
  }
  if (! xcstrcmp(entityName, entity_name_to_match)) {
    /* The cast here is safe because we control the horizontal and
     * the vertical, and we therefore know our strings are never
     * going to overflow an int.
     */
    if (value_length != (int)xcstrlen(entity_value_to_match)
        || xcstrncmp(value, entity_value_to_match, value_length) != 0) {
      entity_match_flag = ENTITY_MATCH_FAIL;
    } else {
      entity_match_flag = ENTITY_MATCH_SUCCESS;
    }
  }
  /* Else leave the match flag alone */
}

void
param_entity_match_init(const XML_Char *name, const XML_Char *value) {
  entity_name_to_match = name;
  entity_value_to_match = value;
  entity_match_flag = ENTITY_MATCH_NOT_FOUND;
}

int
get_param_entity_match_flag(void) {
  return entity_match_flag;
}

/* Misc handlers */

void XMLCALL
xml_decl_handler(void *userData, const XML_Char *version,
                 const XML_Char *encoding, int standalone) {
  UNUSED_P(version);
  UNUSED_P(encoding);
  if (userData != g_handler_data)
    fail("User data (xml decl) not correctly set");
  if (standalone != -1)
    fail("Standalone not flagged as not present in XML decl");
  g_xdecl_count++;
}

void XMLCALL
param_check_skip_handler(void *userData, const XML_Char *entityName,
                         int is_parameter_entity) {
  UNUSED_P(entityName);
  UNUSED_P(is_parameter_entity);
  if (userData != g_handler_data)
    fail("User data (skip) not correctly set");
  g_skip_count++;
}

void XMLCALL
data_check_comment_handler(void *userData, const XML_Char *data) {
  UNUSED_P(data);
  /* Check that the userData passed through is what we expect */
  if (userData != g_handler_data)
    fail("User data (parser) not correctly set");
  /* Check that the user data in the parser is appropriate */
  if (XML_GetUserData(userData) != (void *)1)
    fail("User data in parser not correctly set");
  g_comment_count++;
}

void XMLCALL
selective_aborting_default_handler(void *userData, const XML_Char *s, int len) {
  const XML_Char trigger_char = *(const XML_Char *)userData;

  int found = 0;
  for (int i = 0; i < len; ++i) {
    if (s[i] == trigger_char) {
      found = 1;
      break;
    }
  }

  if (found) {
    XML_StopParser(g_parser, g_resumable);
    XML_SetDefaultHandler(g_parser, NULL);
  }
}

void XMLCALL
suspending_comment_handler(void *userData, const XML_Char *data) {
  UNUSED_P(data);
  XML_Parser parser = (XML_Parser)userData;
  XML_StopParser(parser, XML_TRUE);
}

void XMLCALL
element_decl_suspender(void *userData, const XML_Char *name,
                       XML_Content *model) {
  UNUSED_P(userData);
  UNUSED_P(name);
  XML_StopParser(g_parser, XML_TRUE);
  XML_FreeContentModel(g_parser, model);
}

void XMLCALL
suspend_after_element_declaration(void *userData, const XML_Char *name,
                                  XML_Content *model) {
  UNUSED_P(name);
  XML_Parser parser = (XML_Parser)userData;
  assert_true(XML_StopParser(parser, /*resumable*/ XML_TRUE) == XML_STATUS_OK);
  XML_FreeContentModel(parser, model);
}

void XMLCALL
accumulate_pi_characters(void *userData, const XML_Char *target,
                         const XML_Char *data) {
  CharData *storage = (CharData *)userData;

  CharData_AppendXMLChars(storage, target, -1);
  CharData_AppendXMLChars(storage, XCS(": "), 2);
  CharData_AppendXMLChars(storage, data, -1);
  CharData_AppendXMLChars(storage, XCS("\n"), 1);
}

void XMLCALL
accumulate_comment(void *userData, const XML_Char *data) {
  CharData *storage = (CharData *)userData;

  CharData_AppendXMLChars(storage, data, -1);
}

void XMLCALL
accumulate_entity_decl(void *userData, const XML_Char *entityName,
                       int is_parameter_entity, const XML_Char *value,
                       int value_length, const XML_Char *base,
                       const XML_Char *systemId, const XML_Char *publicId,
                       const XML_Char *notationName) {
  CharData *storage = (CharData *)userData;

  UNUSED_P(is_parameter_entity);
  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  UNUSED_P(notationName);
  CharData_AppendXMLChars(storage, entityName, -1);
  CharData_AppendXMLChars(storage, XCS("="), 1);
  if (value == NULL)
    CharData_AppendXMLChars(storage, XCS("(null)"), -1);
  else
    CharData_AppendXMLChars(storage, value, value_length);
  CharData_AppendXMLChars(storage, XCS("\n"), 1);
}

void XMLCALL
accumulate_char_data_and_suspend(void *userData, const XML_Char *s, int len) {
  ParserPlusStorage *const parserPlusStorage = (ParserPlusStorage *)userData;

  CharData_AppendXMLChars(parserPlusStorage->storage, s, len);

  for (int i = 0; i < len; i++) {
    if (s[i] == 'Z') {
      XML_StopParser(parserPlusStorage->parser, /*resumable=*/XML_TRUE);
      break;
    }
  }
}

void XMLCALL
accumulate_start_element(void *userData, const XML_Char *name,
                         const XML_Char **atts) {
  CharData *const storage = (CharData *)userData;
  CharData_AppendXMLChars(storage, XCS("("), 1);
  CharData_AppendXMLChars(storage, name, -1);

  if ((atts != NULL) && (atts[0] != NULL)) {
    CharData_AppendXMLChars(storage, XCS("("), 1);
    while (atts[0] != NULL) {
      CharData_AppendXMLChars(storage, atts[0], -1);
      CharData_AppendXMLChars(storage, XCS("="), 1);
      CharData_AppendXMLChars(storage, atts[1], -1);
      atts += 2;
      if (atts[0] != NULL) {
        CharData_AppendXMLChars(storage, XCS(","), 1);
      }
    }
    CharData_AppendXMLChars(storage, XCS(")"), 1);
  }

  CharData_AppendXMLChars(storage, XCS(")\n"), 2);
}

void XMLCALL
accumulate_characters(void *userData, const XML_Char *s, int len) {
  CharData *const storage = (CharData *)userData;
  CharData_AppendXMLChars(storage, s, len);
}

void XMLCALL
accumulate_attribute(void *userData, const XML_Char *name,
                     const XML_Char **atts) {
  CharData *const storage = (CharData *)userData;
  UNUSED_P(name);
  /* Check there are attributes to deal with */
  if (atts == NULL)
    return;

  while (storage->count < 0 && atts[0] != NULL) {
    /* "accumulate" the value of the first attribute we see */
    CharData_AppendXMLChars(storage, atts[1], -1);
    atts += 2;
  }
}

void XMLCALL
ext_accumulate_characters(void *userData, const XML_Char *s, int len) {
  ExtTest *const test_data = (ExtTest *)userData;
  accumulate_characters(test_data->storage, s, len);
}

void XMLCALL
checking_default_handler(void *userData, const XML_Char *s, int len) {
  DefaultCheck *data = (DefaultCheck *)userData;
  int i;

  for (i = 0; data[i].expected != NULL; i++) {
    if (data[i].expectedLen == len
        && ! memcmp(data[i].expected, s, len * sizeof(XML_Char))) {
      data[i].seen = XML_TRUE;
      break;
    }
  }
}

void XMLCALL
accumulate_and_suspend_comment_handler(void *userData, const XML_Char *data) {
  ParserPlusStorage *const parserPlusStorage = (ParserPlusStorage *)userData;
  accumulate_comment(parserPlusStorage->storage, data);
  XML_StopParser(parserPlusStorage->parser, XML_TRUE);
}
