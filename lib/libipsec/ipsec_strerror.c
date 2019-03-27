/*	$KAME: ipsec_strerror.c,v 1.7 2000/07/30 00:45:12 itojun Exp $	*/

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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>

#include <string.h>
#include <netipsec/ipsec.h>

#include "ipsec_strerror.h"

int __ipsec_errcode;

static const char *ipsec_errlist[] = {
"Success",					/*EIPSEC_NO_ERROR*/
"Not supported",				/*EIPSEC_NOT_SUPPORTED*/
"Invalid argument",				/*EIPSEC_INVAL_ARGUMENT*/
"Invalid sadb message",				/*EIPSEC_INVAL_SADBMSG*/
"Invalid version",				/*EIPSEC_INVAL_VERSION*/
"Invalid security policy",			/*EIPSEC_INVAL_POLICY*/
"Invalid address specification",		/*EIPSEC_INVAL_ADDRESS*/
"Invalid ipsec protocol",			/*EIPSEC_INVAL_PROTO*/
"Invalid ipsec mode",				/*EIPSEC_INVAL_MODE*/
"Invalid ipsec level",				/*EIPSEC_INVAL_LEVEL*/
"Invalid SA type",				/*EIPSEC_INVAL_SATYPE*/
"Invalid message type",				/*EIPSEC_INVAL_MSGTYPE*/
"Invalid extension type",			/*EIPSEC_INVAL_EXTTYPE*/
"Invalid algorithm type",			/*EIPSEC_INVAL_ALGS*/
"Invalid key length",				/*EIPSEC_INVAL_KEYLEN*/
"Invalid address family",			/*EIPSEC_INVAL_FAMILY*/
"Invalid prefix length",			/*EIPSEC_INVAL_PREFIXLEN*/
"Invalid direciton",				/*EIPSEC_INVAL_DIR*/
"SPI range violation",				/*EIPSEC_INVAL_SPI*/
"No protocol specified",			/*EIPSEC_NO_PROTO*/
"No algorithm specified",			/*EIPSEC_NO_ALGS*/
"No buffers available",				/*EIPSEC_NO_BUFS*/
"Must get supported algorithms list first",	/*EIPSEC_DO_GET_SUPP_LIST*/
"Protocol mismatch",				/*EIPSEC_PROTO_MISMATCH*/
"Family mismatch",				/*EIPSEC_FAMILY_MISMATCH*/
"Too few arguments",				/*EIPSEC_FEW_ARGUMENTS*/
NULL,						/*EIPSEC_SYSTEM_ERROR*/
"Unknown error",				/*EIPSEC_MAX*/
};

const char *ipsec_strerror(void)
{
	if (__ipsec_errcode < 0 || __ipsec_errcode > EIPSEC_MAX)
		__ipsec_errcode = EIPSEC_MAX;

	return ipsec_errlist[__ipsec_errcode];
}

void __ipsec_set_strerror(const char *str)
{
	__ipsec_errcode = EIPSEC_SYSTEM_ERROR;
	ipsec_errlist[EIPSEC_SYSTEM_ERROR] = str;

	return;
}
