/*
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 2002-2004 Fred L. Drake, Jr. <fdrake@users.sourceforge.net>
   Copyright (c) 2003      Greg Stein <gstein@users.sourceforge.net>
   Copyright (c) 2016      Gilles Espinasse <g.esp@free.fr>
   Copyright (c) 2016-2023 Sebastian Pipping <sebastian@pipping.org>
   Copyright (c) 2017      Joe Orton <jorton@redhat.com>
   Copyright (c) 2017      Rhodri James <rhodri@wildebeest.org.uk>
   Copyright (c) 2022      Sean McBride <sean@rogue-research.com>
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

#include "expat_config.h"
#include "minicheck.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "chardata.h"

static int
xmlstrlen(const XML_Char *s) {
  int len = 0;
  assert(s != NULL);
  while (s[len] != 0)
    ++len;
  return len;
}

void
CharData_Init(CharData *storage) {
  assert(storage != NULL);
  storage->count = -1;
}

void
CharData_AppendXMLChars(CharData *storage, const XML_Char *s, int len) {
  int maxchars;

  assert(storage != NULL);
  assert(s != NULL);
  maxchars = sizeof(storage->data) / sizeof(storage->data[0]);
  if (storage->count < 0)
    storage->count = 0;
  if (len < 0)
    len = xmlstrlen(s);
  if ((len + storage->count) > maxchars) {
    len = (maxchars - storage->count);
  }
  if (len + storage->count < (int)sizeof(storage->data)) {
    memcpy(storage->data + storage->count, s, len * sizeof(storage->data[0]));
    storage->count += len;
  }
}

int
CharData_CheckXMLChars(CharData *storage, const XML_Char *expected) {
  int len = xmlstrlen(expected);
  int count;

  assert(storage != NULL);
  count = (storage->count < 0) ? 0 : storage->count;
  if (len != count) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer),
             "wrong number of data characters: got %d, expected %d", count,
             len);
    fail(buffer);
    return 0;
  }
  if (memcmp(expected, storage->data, len * sizeof(storage->data[0])) != 0) {
    fail("got bad data bytes");
    return 0;
  }
  return 1;
}
