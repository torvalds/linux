/* Dummy handler functions for the Expat test suite
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
   Copyright (c) 2016-2022 Sebastian Pipping <sebastian@pipping.org>
   Copyright (c) 2017-2022 Rhodri James <rhodri@wildebeest.org.uk>
   Copyright (c) 2017      Joe Orton <jorton@redhat.com>
   Copyright (c) 2017      José Gutiérrez de la Concha <jose@zeroc.com>
   Copyright (c) 2018      Marco Maggi <marco.maggi-ipsu@poste.it>
   Copyright (c) 2019      David Loffredo <loffredo@steptools.com>
   Copyright (c) 2020      Tim Gates <tim.gates@iress.com>
   Copyright (c) 2021      Donghee Na <donghee.na@python.org>
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

#include "expat.h"
#include "internal.h"
#include "common.h"
#include "dummy.h"

/* Dummy handlers for when we need to set a handler to tickle a bug,
   but it doesn't need to do anything.
*/
static unsigned long dummy_handler_flags = 0;

void
init_dummy_handlers(void) {
  dummy_handler_flags = 0;
}

unsigned long
get_dummy_handler_flags(void) {
  return dummy_handler_flags;
}

void XMLCALL
dummy_xdecl_handler(void *userData, const XML_Char *version,
                    const XML_Char *encoding, int standalone) {
  UNUSED_P(userData);
  UNUSED_P(version);
  UNUSED_P(encoding);
  UNUSED_P(standalone);
}

void XMLCALL
dummy_start_doctype_handler(void *userData, const XML_Char *doctypeName,
                            const XML_Char *sysid, const XML_Char *pubid,
                            int has_internal_subset) {
  UNUSED_P(userData);
  UNUSED_P(doctypeName);
  UNUSED_P(sysid);
  UNUSED_P(pubid);
  UNUSED_P(has_internal_subset);
  dummy_handler_flags |= DUMMY_START_DOCTYPE_HANDLER_FLAG;
}

void XMLCALL
dummy_end_doctype_handler(void *userData) {
  UNUSED_P(userData);
  dummy_handler_flags |= DUMMY_END_DOCTYPE_HANDLER_FLAG;
}

void XMLCALL
dummy_entity_decl_handler(void *userData, const XML_Char *entityName,
                          int is_parameter_entity, const XML_Char *value,
                          int value_length, const XML_Char *base,
                          const XML_Char *systemId, const XML_Char *publicId,
                          const XML_Char *notationName) {
  UNUSED_P(userData);
  UNUSED_P(entityName);
  UNUSED_P(is_parameter_entity);
  UNUSED_P(value);
  UNUSED_P(value_length);
  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  UNUSED_P(notationName);
  dummy_handler_flags |= DUMMY_ENTITY_DECL_HANDLER_FLAG;
}

void XMLCALL
dummy_notation_decl_handler(void *userData, const XML_Char *notationName,
                            const XML_Char *base, const XML_Char *systemId,
                            const XML_Char *publicId) {
  UNUSED_P(userData);
  UNUSED_P(notationName);
  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  dummy_handler_flags |= DUMMY_NOTATION_DECL_HANDLER_FLAG;
}

void XMLCALL
dummy_element_decl_handler(void *userData, const XML_Char *name,
                           XML_Content *model) {
  UNUSED_P(userData);
  UNUSED_P(name);
  /* The content model must be freed by the handler.  Unfortunately
   * we cannot pass the parser as the userData because this is used
   * with other handlers that require other userData.
   */
  XML_FreeContentModel(g_parser, model);
  dummy_handler_flags |= DUMMY_ELEMENT_DECL_HANDLER_FLAG;
}

void XMLCALL
dummy_attlist_decl_handler(void *userData, const XML_Char *elname,
                           const XML_Char *attname, const XML_Char *att_type,
                           const XML_Char *dflt, int isrequired) {
  UNUSED_P(userData);
  UNUSED_P(elname);
  UNUSED_P(attname);
  UNUSED_P(att_type);
  UNUSED_P(dflt);
  UNUSED_P(isrequired);
  dummy_handler_flags |= DUMMY_ATTLIST_DECL_HANDLER_FLAG;
}

void XMLCALL
dummy_comment_handler(void *userData, const XML_Char *data) {
  UNUSED_P(userData);
  UNUSED_P(data);
  dummy_handler_flags |= DUMMY_COMMENT_HANDLER_FLAG;
}

void XMLCALL
dummy_pi_handler(void *userData, const XML_Char *target, const XML_Char *data) {
  UNUSED_P(userData);
  UNUSED_P(target);
  UNUSED_P(data);
  dummy_handler_flags |= DUMMY_PI_HANDLER_FLAG;
}

void XMLCALL
dummy_start_element(void *userData, const XML_Char *name,
                    const XML_Char **atts) {
  UNUSED_P(userData);
  UNUSED_P(name);
  UNUSED_P(atts);
  dummy_handler_flags |= DUMMY_START_ELEMENT_HANDLER_FLAG;
}

void XMLCALL
dummy_end_element(void *userData, const XML_Char *name) {
  UNUSED_P(userData);
  UNUSED_P(name);
}

void XMLCALL
dummy_start_cdata_handler(void *userData) {
  UNUSED_P(userData);
  dummy_handler_flags |= DUMMY_START_CDATA_HANDLER_FLAG;
}

void XMLCALL
dummy_end_cdata_handler(void *userData) {
  UNUSED_P(userData);
  dummy_handler_flags |= DUMMY_END_CDATA_HANDLER_FLAG;
}

void XMLCALL
dummy_cdata_handler(void *userData, const XML_Char *s, int len) {
  UNUSED_P(userData);
  UNUSED_P(s);
  UNUSED_P(len);
}

void XMLCALL
dummy_start_namespace_decl_handler(void *userData, const XML_Char *prefix,
                                   const XML_Char *uri) {
  UNUSED_P(userData);
  UNUSED_P(prefix);
  UNUSED_P(uri);
  dummy_handler_flags |= DUMMY_START_NS_DECL_HANDLER_FLAG;
}

void XMLCALL
dummy_end_namespace_decl_handler(void *userData, const XML_Char *prefix) {
  UNUSED_P(userData);
  UNUSED_P(prefix);
  dummy_handler_flags |= DUMMY_END_NS_DECL_HANDLER_FLAG;
}

/* This handler is obsolete, but while the code exists we should
 * ensure that dealing with the handler is covered by tests.
 */
void XMLCALL
dummy_unparsed_entity_decl_handler(void *userData, const XML_Char *entityName,
                                   const XML_Char *base,
                                   const XML_Char *systemId,
                                   const XML_Char *publicId,
                                   const XML_Char *notationName) {
  UNUSED_P(userData);
  UNUSED_P(entityName);
  UNUSED_P(base);
  UNUSED_P(systemId);
  UNUSED_P(publicId);
  UNUSED_P(notationName);
  dummy_handler_flags |= DUMMY_UNPARSED_ENTITY_DECL_HANDLER_FLAG;
}

void XMLCALL
dummy_default_handler(void *userData, const XML_Char *s, int len) {
  UNUSED_P(userData);
  UNUSED_P(s);
  UNUSED_P(len);
}

void XMLCALL
dummy_start_doctype_decl_handler(void *userData, const XML_Char *doctypeName,
                                 const XML_Char *sysid, const XML_Char *pubid,
                                 int has_internal_subset) {
  UNUSED_P(userData);
  UNUSED_P(doctypeName);
  UNUSED_P(sysid);
  UNUSED_P(pubid);
  UNUSED_P(has_internal_subset);
  dummy_handler_flags |= DUMMY_START_DOCTYPE_DECL_HANDLER_FLAG;
}

void XMLCALL
dummy_end_doctype_decl_handler(void *userData) {
  UNUSED_P(userData);
  dummy_handler_flags |= DUMMY_END_DOCTYPE_DECL_HANDLER_FLAG;
}

void XMLCALL
dummy_skip_handler(void *userData, const XML_Char *entityName,
                   int is_parameter_entity) {
  UNUSED_P(userData);
  UNUSED_P(entityName);
  UNUSED_P(is_parameter_entity);
  dummy_handler_flags |= DUMMY_SKIP_HANDLER_FLAG;
}
