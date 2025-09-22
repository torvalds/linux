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

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XML_DUMMY_H
#  define XML_DUMMY_H

#  define DUMMY_START_DOCTYPE_HANDLER_FLAG (1UL << 0)
#  define DUMMY_END_DOCTYPE_HANDLER_FLAG (1UL << 1)
#  define DUMMY_ENTITY_DECL_HANDLER_FLAG (1UL << 2)
#  define DUMMY_NOTATION_DECL_HANDLER_FLAG (1UL << 3)
#  define DUMMY_ELEMENT_DECL_HANDLER_FLAG (1UL << 4)
#  define DUMMY_ATTLIST_DECL_HANDLER_FLAG (1UL << 5)
#  define DUMMY_COMMENT_HANDLER_FLAG (1UL << 6)
#  define DUMMY_PI_HANDLER_FLAG (1UL << 7)
#  define DUMMY_START_ELEMENT_HANDLER_FLAG (1UL << 8)
#  define DUMMY_START_CDATA_HANDLER_FLAG (1UL << 9)
#  define DUMMY_END_CDATA_HANDLER_FLAG (1UL << 10)
#  define DUMMY_UNPARSED_ENTITY_DECL_HANDLER_FLAG (1UL << 11)
#  define DUMMY_START_NS_DECL_HANDLER_FLAG (1UL << 12)
#  define DUMMY_END_NS_DECL_HANDLER_FLAG (1UL << 13)
#  define DUMMY_START_DOCTYPE_DECL_HANDLER_FLAG (1UL << 14)
#  define DUMMY_END_DOCTYPE_DECL_HANDLER_FLAG (1UL << 15)
#  define DUMMY_SKIP_HANDLER_FLAG (1UL << 16)
#  define DUMMY_DEFAULT_HANDLER_FLAG (1UL << 17)

extern void init_dummy_handlers(void);
extern unsigned long get_dummy_handler_flags(void);

extern void XMLCALL dummy_xdecl_handler(void *userData, const XML_Char *version,
                                        const XML_Char *encoding,
                                        int standalone);

extern void XMLCALL dummy_start_doctype_handler(void *userData,
                                                const XML_Char *doctypeName,
                                                const XML_Char *sysid,
                                                const XML_Char *pubid,
                                                int has_internal_subset);

extern void XMLCALL dummy_end_doctype_handler(void *userData);

extern void XMLCALL dummy_entity_decl_handler(
    void *userData, const XML_Char *entityName, int is_parameter_entity,
    const XML_Char *value, int value_length, const XML_Char *base,
    const XML_Char *systemId, const XML_Char *publicId,
    const XML_Char *notationName);

extern void XMLCALL dummy_notation_decl_handler(void *userData,
                                                const XML_Char *notationName,
                                                const XML_Char *base,
                                                const XML_Char *systemId,
                                                const XML_Char *publicId);

extern void XMLCALL dummy_element_decl_handler(void *userData,
                                               const XML_Char *name,
                                               XML_Content *model);

extern void XMLCALL dummy_attlist_decl_handler(
    void *userData, const XML_Char *elname, const XML_Char *attname,
    const XML_Char *att_type, const XML_Char *dflt, int isrequired);

extern void XMLCALL dummy_comment_handler(void *userData, const XML_Char *data);

extern void XMLCALL dummy_pi_handler(void *userData, const XML_Char *target,
                                     const XML_Char *data);

extern void XMLCALL dummy_start_element(void *userData, const XML_Char *name,
                                        const XML_Char **atts);

extern void XMLCALL dummy_end_element(void *userData, const XML_Char *name);

extern void XMLCALL dummy_start_cdata_handler(void *userData);

extern void XMLCALL dummy_end_cdata_handler(void *userData);

extern void XMLCALL dummy_cdata_handler(void *userData, const XML_Char *s,
                                        int len);

extern void XMLCALL dummy_start_namespace_decl_handler(void *userData,
                                                       const XML_Char *prefix,
                                                       const XML_Char *uri);

extern void XMLCALL dummy_end_namespace_decl_handler(void *userData,
                                                     const XML_Char *prefix);

extern void XMLCALL dummy_unparsed_entity_decl_handler(
    void *userData, const XML_Char *entityName, const XML_Char *base,
    const XML_Char *systemId, const XML_Char *publicId,
    const XML_Char *notationName);

extern void XMLCALL dummy_default_handler(void *userData, const XML_Char *s,
                                          int len);

extern void XMLCALL dummy_start_doctype_decl_handler(
    void *userData, const XML_Char *doctypeName, const XML_Char *sysid,
    const XML_Char *pubid, int has_internal_subset);

extern void XMLCALL dummy_end_doctype_decl_handler(void *userData);

extern void XMLCALL dummy_skip_handler(void *userData,
                                       const XML_Char *entityName,
                                       int is_parameter_entity);

#endif /* XML_DUMMY_H */

#ifdef __cplusplus
}
#endif
