/* Interface to some helper routines used to accumulate and check
   structured content.
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 2017 Rhodri James <rhodri@wildebeest.org.uk>
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

#ifndef XML_STRUCTDATA_H
#  define XML_STRUCTDATA_H 1

#  include "expat.h"

typedef struct {
  const XML_Char *str;
  int data0;
  int data1;
  int data2;
} StructDataEntry;

typedef struct {
  int count;     /* Number of entries used */
  int max_count; /* Number of StructDataEntry items in `entries` */
  StructDataEntry *entries;
} StructData;

void StructData_Init(StructData *storage);

void StructData_AddItem(StructData *storage, const XML_Char *s, int data0,
                        int data1, int data2);

void StructData_CheckItems(StructData *storage, const StructDataEntry *expected,
                           int count);

void StructData_Dispose(StructData *storage);

#endif /* XML_STRUCTDATA_H */

#ifdef __cplusplus
}
#endif
