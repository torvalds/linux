/* crypto/ripemd/rmdconst.h */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 * 
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 * 
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from 
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 * 
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */
#define KL0 0x00000000L
#define KL1 0x5A827999L
#define KL2 0x6ED9EBA1L
#define KL3 0x8F1BBCDCL
#define KL4 0xA953FD4EL

#define KR0 0x50A28BE6L
#define KR1 0x5C4DD124L
#define KR2 0x6D703EF3L
#define KR3 0x7A6D76E9L
#define KR4 0x00000000L

#define WL00  0
#define SL00 11
#define WL01  1
#define SL01 14
#define WL02  2
#define SL02 15
#define WL03  3
#define SL03 12
#define WL04  4
#define SL04  5
#define WL05  5
#define SL05  8
#define WL06  6
#define SL06  7
#define WL07  7
#define SL07  9
#define WL08  8
#define SL08 11
#define WL09  9
#define SL09 13
#define WL10 10
#define SL10 14
#define WL11 11
#define SL11 15
#define WL12 12
#define SL12  6
#define WL13 13
#define SL13  7
#define WL14 14
#define SL14  9
#define WL15 15
#define SL15  8

#define WL16  7
#define SL16  7
#define WL17  4
#define SL17  6
#define WL18 13
#define SL18  8
#define WL19  1
#define SL19 13
#define WL20 10
#define SL20 11
#define WL21  6
#define SL21  9
#define WL22 15
#define SL22  7
#define WL23  3
#define SL23 15
#define WL24 12
#define SL24  7
#define WL25  0
#define SL25 12
#define WL26  9
#define SL26 15
#define WL27  5
#define SL27  9
#define WL28  2
#define SL28 11
#define WL29 14
#define SL29  7
#define WL30 11
#define SL30 13
#define WL31  8
#define SL31 12

#define WL32  3
#define SL32 11
#define WL33 10
#define SL33 13
#define WL34 14
#define SL34  6
#define WL35  4
#define SL35  7
#define WL36  9
#define SL36 14
#define WL37 15
#define SL37  9
#define WL38  8
#define SL38 13
#define WL39  1
#define SL39 15
#define WL40  2
#define SL40 14
#define WL41  7
#define SL41  8
#define WL42  0
#define SL42 13
#define WL43  6
#define SL43  6
#define WL44 13
#define SL44  5
#define WL45 11
#define SL45 12
#define WL46  5
#define SL46  7
#define WL47 12
#define SL47  5

#define WL48  1
#define SL48 11
#define WL49  9
#define SL49 12
#define WL50 11
#define SL50 14
#define WL51 10
#define SL51 15
#define WL52  0
#define SL52 14
#define WL53  8
#define SL53 15
#define WL54 12
#define SL54  9
#define WL55  4
#define SL55  8
#define WL56 13
#define SL56  9
#define WL57  3
#define SL57 14
#define WL58  7
#define SL58  5
#define WL59 15
#define SL59  6
#define WL60 14
#define SL60  8
#define WL61  5
#define SL61  6
#define WL62  6
#define SL62  5
#define WL63  2
#define SL63 12

#define WL64  4
#define SL64  9
#define WL65  0
#define SL65 15
#define WL66  5
#define SL66  5
#define WL67  9
#define SL67 11
#define WL68  7
#define SL68  6
#define WL69 12
#define SL69  8
#define WL70  2
#define SL70 13
#define WL71 10
#define SL71 12
#define WL72 14
#define SL72  5
#define WL73  1
#define SL73 12
#define WL74  3
#define SL74 13
#define WL75  8
#define SL75 14
#define WL76 11
#define SL76 11
#define WL77  6
#define SL77  8
#define WL78 15
#define SL78  5
#define WL79 13
#define SL79  6

#define WR00  5
#define SR00  8
#define WR01 14
#define SR01  9
#define WR02  7
#define SR02  9
#define WR03  0
#define SR03 11
#define WR04  9
#define SR04 13
#define WR05  2
#define SR05 15
#define WR06 11
#define SR06 15
#define WR07  4
#define SR07  5
#define WR08 13
#define SR08  7
#define WR09  6
#define SR09  7
#define WR10 15
#define SR10  8
#define WR11  8
#define SR11 11
#define WR12  1
#define SR12 14
#define WR13 10
#define SR13 14
#define WR14  3
#define SR14 12
#define WR15 12
#define SR15  6

#define WR16  6
#define SR16  9
#define WR17 11
#define SR17 13
#define WR18  3
#define SR18 15
#define WR19  7
#define SR19  7
#define WR20  0
#define SR20 12
#define WR21 13
#define SR21  8
#define WR22  5
#define SR22  9
#define WR23 10
#define SR23 11
#define WR24 14
#define SR24  7
#define WR25 15
#define SR25  7
#define WR26  8
#define SR26 12
#define WR27 12
#define SR27  7
#define WR28  4
#define SR28  6
#define WR29  9
#define SR29 15
#define WR30  1
#define SR30 13
#define WR31  2
#define SR31 11

#define WR32 15
#define SR32  9
#define WR33  5
#define SR33  7
#define WR34  1
#define SR34 15
#define WR35  3
#define SR35 11
#define WR36  7
#define SR36  8
#define WR37 14
#define SR37  6
#define WR38  6
#define SR38  6
#define WR39  9
#define SR39 14
#define WR40 11
#define SR40 12
#define WR41  8
#define SR41 13
#define WR42 12
#define SR42  5
#define WR43  2
#define SR43 14
#define WR44 10
#define SR44 13
#define WR45  0
#define SR45 13
#define WR46  4
#define SR46  7
#define WR47 13
#define SR47  5

#define WR48  8
#define SR48 15
#define WR49  6
#define SR49  5
#define WR50  4
#define SR50  8
#define WR51  1
#define SR51 11
#define WR52  3
#define SR52 14
#define WR53 11
#define SR53 14
#define WR54 15
#define SR54  6
#define WR55  0
#define SR55 14
#define WR56  5
#define SR56  6
#define WR57 12
#define SR57  9
#define WR58  2
#define SR58 12
#define WR59 13
#define SR59  9
#define WR60  9
#define SR60 12
#define WR61  7
#define SR61  5
#define WR62 10
#define SR62 15
#define WR63 14
#define SR63  8

#define WR64 12
#define SR64  8
#define WR65 15
#define SR65  5
#define WR66 10
#define SR66 12
#define WR67  4
#define SR67  9
#define WR68  1
#define SR68 12
#define WR69  5
#define SR69  5
#define WR70  8
#define SR70 14
#define WR71  7
#define SR71  6
#define WR72  6
#define SR72  8
#define WR73  2
#define SR73 13
#define WR74 13
#define SR74  6
#define WR75 14
#define SR75  5
#define WR76  0
#define SR76 15
#define WR77  3
#define SR77 13
#define WR78  9
#define SR78 11
#define WR79 11
#define SR79 11

