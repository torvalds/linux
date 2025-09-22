/*
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 1997-2000 Thai Open Source Software Center Ltd
   Copyright (c) 2000      Clark Cooper <coopercc@users.sourceforge.net>
   Copyright (c) 2017-2019 Sebastian Pipping <sebastian@pipping.org>
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

enum {
  BT_NONXML,   /* e.g. noncharacter-FFFF */
  BT_MALFORM,  /* illegal, with regard to encoding */
  BT_LT,       /* less than = "<" */
  BT_AMP,      /* ampersand = "&" */
  BT_RSQB,     /* right square bracket = "[" */
  BT_LEAD2,    /* lead byte of a 2-byte UTF-8 character */
  BT_LEAD3,    /* lead byte of a 3-byte UTF-8 character */
  BT_LEAD4,    /* lead byte of a 4-byte UTF-8 character */
  BT_TRAIL,    /* trailing unit, e.g. second 16-bit unit of a 4-byte char. */
  BT_CR,       /* carriage return = "\r" */
  BT_LF,       /* line feed = "\n" */
  BT_GT,       /* greater than = ">" */
  BT_QUOT,     /* quotation character = "\"" */
  BT_APOS,     /* apostrophe = "'" */
  BT_EQUALS,   /* equal sign = "=" */
  BT_QUEST,    /* question mark = "?" */
  BT_EXCL,     /* exclamation mark = "!" */
  BT_SOL,      /* solidus, slash = "/" */
  BT_SEMI,     /* semicolon = ";" */
  BT_NUM,      /* number sign = "#" */
  BT_LSQB,     /* left square bracket = "[" */
  BT_S,        /* white space, e.g. "\t", " "[, "\r"] */
  BT_NMSTRT,   /* non-hex name start letter = "G".."Z" + "g".."z" + "_" */
  BT_COLON,    /* colon = ":" */
  BT_HEX,      /* hex letter = "A".."F" + "a".."f" */
  BT_DIGIT,    /* digit = "0".."9" */
  BT_NAME,     /* dot and middle dot = "." + chr(0xb7) */
  BT_MINUS,    /* minus = "-" */
  BT_OTHER,    /* known not to be a name or name start character */
  BT_NONASCII, /* might be a name or name start character */
  BT_PERCNT,   /* percent sign = "%" */
  BT_LPAR,     /* left parenthesis = "(" */
  BT_RPAR,     /* right parenthesis = "(" */
  BT_AST,      /* asterisk = "*" */
  BT_PLUS,     /* plus sign = "+" */
  BT_COMMA,    /* comma = "," */
  BT_VERBAR    /* vertical bar = "|" */
};

#include <stddef.h>
