/*	$FreeBSD$	*/
/*	$KAME: ipsec_strerror.h,v 1.8 2000/07/30 00:45:12 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, 1998, and 1999 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

extern int __ipsec_errcode;
extern void __ipsec_set_strerror(const char *);

#define EIPSEC_NO_ERROR		0	/*success*/
#define EIPSEC_NOT_SUPPORTED	1	/*not supported*/
#define EIPSEC_INVAL_ARGUMENT	2	/*invalid argument*/
#define EIPSEC_INVAL_SADBMSG	3	/*invalid sadb message*/
#define EIPSEC_INVAL_VERSION	4	/*invalid version*/
#define EIPSEC_INVAL_POLICY	5	/*invalid security policy*/
#define EIPSEC_INVAL_ADDRESS	6	/*invalid address specification*/
#define EIPSEC_INVAL_PROTO	7	/*invalid ipsec protocol*/
#define EIPSEC_INVAL_MODE	8	/*Invalid ipsec mode*/
#define EIPSEC_INVAL_LEVEL	9	/*invalid ipsec level*/
#define EIPSEC_INVAL_SATYPE	10	/*invalid SA type*/
#define EIPSEC_INVAL_MSGTYPE	11	/*invalid message type*/
#define EIPSEC_INVAL_EXTTYPE	12	/*invalid extension type*/
#define EIPSEC_INVAL_ALGS	13	/*Invalid algorithm type*/
#define EIPSEC_INVAL_KEYLEN	14	/*invalid key length*/
#define EIPSEC_INVAL_FAMILY	15	/*invalid address family*/
#define EIPSEC_INVAL_PREFIXLEN	16	/*SPI range violation*/
#define EIPSEC_INVAL_DIR	17	/*Invalid direciton*/
#define EIPSEC_INVAL_SPI	18	/*invalid prefixlen*/
#define EIPSEC_NO_PROTO		19	/*no protocol specified*/
#define EIPSEC_NO_ALGS		20	/*No algorithm specified*/
#define EIPSEC_NO_BUFS		21	/*no buffers available*/
#define EIPSEC_DO_GET_SUPP_LIST	22	/*must get supported algorithm first*/
#define EIPSEC_PROTO_MISMATCH	23	/*protocol mismatch*/
#define EIPSEC_FAMILY_MISMATCH	24	/*family mismatch*/
#define EIPSEC_FEW_ARGUMENTS	25	/*Too few arguments*/
#define EIPSEC_SYSTEM_ERROR	26	/*system error*/
#define EIPSEC_MAX		27	/*unknown error*/
