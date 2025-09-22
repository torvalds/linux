/*
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 1997-2000 Thai Open Source Software Center Ltd
   Copyright (c) 2000      Clark Cooper <coopercc@users.sourceforge.net>
   Copyright (c) 2002      Fred L. Drake, Jr. <fdrake@users.sourceforge.net>
   Copyright (c) 2017      Sebastian Pipping <sebastian@pipping.org>
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

/* 0x80 */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0x84 */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0x88 */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0x8C */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0x90 */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0x94 */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0x98 */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0x9C */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0xA0 */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0xA4 */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0xA8 */ BT_OTHER, BT_OTHER, BT_NMSTRT, BT_OTHER,
    /* 0xAC */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0xB0 */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0xB4 */ BT_OTHER, BT_NMSTRT, BT_OTHER, BT_NAME,
    /* 0xB8 */ BT_OTHER, BT_OTHER, BT_NMSTRT, BT_OTHER,
    /* 0xBC */ BT_OTHER, BT_OTHER, BT_OTHER, BT_OTHER,
    /* 0xC0 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xC4 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xC8 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xCC */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xD0 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xD4 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_OTHER,
    /* 0xD8 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xDC */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xE0 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xE4 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xE8 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xEC */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xF0 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xF4 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_OTHER,
    /* 0xF8 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
    /* 0xFC */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
