/*	$OpenBSD: res_debug_syms.c,v 1.2 2015/10/05 02:57:16 guenther Exp $	*/

/*
 * ++Copyright++ 1985, 1990, 1993
 * -
 * Copyright (c) 1985, 1990, 1993
 *    The Regents of the University of California.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * -
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * -
 * Portions Copyright (c) 1995 by International Business Machines, Inc.
 *
 * International Business Machines, Inc. (hereinafter called IBM) grants
 * permission under its copyrights to use, copy, modify, and distribute this
 * Software with or without fee, provided that the above copyright notice and
 * all paragraphs of this notice appear in all copies, and that the name of IBM
 * not be used in connection with the marketing of any product incorporating
 * the Software or modifications thereof, without specific, written prior
 * permission.
 *
 * To the extent it has a right to do so, IBM grants an immunity from suit
 * under its patents, if any, for the use, sale or manufacture of products to
 * the extent that such products are used for performing Domain Name System
 * dynamic updates in TCP/IP networks by means of the Software.  No immunity is
 * granted for any product per se or for any other function of any product.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", AND IBM DISCLAIMS ALL WARRANTIES,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE.  IN NO EVENT SHALL IBM BE LIABLE FOR ANY SPECIAL,
 * DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE, EVEN
 * IF IBM IS APPRISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * --Copyright--
 */


#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>

#include <resolv.h>
#include <stdio.h>

/*
 * Names of RR classes and qclasses.  Classes and qclasses are the same, except
 * that C_ANY is a qclass but not a class.  (You can ask for records of class
 * C_ANY, but you can't have any records of that class in the database.)
 */
const struct res_sym __p_class_syms[] = {
	{C_IN,		"IN"},
	{C_CHAOS,	"CHAOS"},
	{C_HS,		"HS"},
	{C_HS,		"HESIOD"},
	{C_ANY,		"ANY"},
	{C_IN, 		(char *)0}
};

/*
 * Names of RR types and qtypes.  Types and qtypes are the same, except
 * that T_ANY is a qtype but not a type.  (You can ask for records of type
 * T_ANY, but you can't have any records of that type in the database.)
 */
const struct res_sym __p_type_syms[] = {
	{T_A,		"A",		"address"},
	{T_NS,		"NS",		"name server"},
	{T_MD,		"MD",		"mail destination (deprecated)"},
	{T_MF,		"MF",		"mail forwarder (deprecated)"},
	{T_CNAME,	"CNAME",	"canonical name"},
	{T_SOA,		"SOA",		"start of authority"},
	{T_MB,		"MB",		"mailbox"},
	{T_MG,		"MG",		"mail group member"},
	{T_MR,		"MR",		"mail rename"},
	{T_NULL,	"NULL",		"null"},
	{T_WKS,		"WKS",		"well-known service (deprecated)"},
	{T_PTR,		"PTR",		"domain name pointer"},
	{T_HINFO,	"HINFO",	"host information"},
	{T_MINFO,	"MINFO",	"mailbox information"},
	{T_MX,		"MX",		"mail exchanger"},
	{T_TXT,		"TXT",		"text"},
	{T_RP,		"RP",		"responsible person"},
	{T_AFSDB,	"AFSDB",	"DCE or AFS server"},
	{T_X25,		"X25",		"X25 address"},
	{T_ISDN,	"ISDN",		"ISDN address"},
	{T_RT,		"RT",		"router"},
	{T_NSAP,	"NSAP",		"nsap address"},
	{T_NSAP_PTR,	"NSAP_PTR",	"domain name pointer"},
	{T_SIG,		"SIG",		"signature"},
	{T_KEY,		"KEY",		"key"},
	{T_PX,		"PX",		"mapping information"},
	{T_GPOS,	"GPOS",		"geographical position (withdrawn)"},
	{T_AAAA,	"AAAA",		"IPv6 address"},
	{T_LOC,		"LOC",		"location"},
	{T_NXT,		"NXT",		"next valid name (unimplemented)"},
	{T_EID,		"EID",		"endpoint identifier (unimplemented)"},
	{T_NIMLOC,	"NIMLOC",	"NIMROD locator (unimplemented)"},
	{T_SRV,		"SRV",		"server selection"},
	{T_ATMA,	"ATMA",		"ATM address (unimplemented)"},
	{T_IXFR,	"IXFR",		"incremental zone transfer"},
	{T_AXFR,	"AXFR",		"zone transfer"},
	{T_MAILB,	"MAILB",	"mailbox-related data (deprecated)"},
	{T_MAILA,	"MAILA",	"mail agent (deprecated)"},
	{T_UINFO,	"UINFO",	"user information (nonstandard)"},
	{T_UID,		"UID",		"user ID (nonstandard)"},
	{T_GID,		"GID",		"group ID (nonstandard)"},
	{T_NAPTR,	"NAPTR",	"URN Naming Authority"},
#ifdef ALLOW_T_UNSPEC
	{T_UNSPEC,	"UNSPEC",	"unspecified data (nonstandard)"},
#endif /* ALLOW_T_UNSPEC */
	{T_ANY,		"ANY",		"\"any\""},
	{0, 		NULL,		NULL}
};

const char *
__sym_ntos(const struct res_sym *syms, int number, int *success)
{
	static char unname[20];

	for (; syms->name != 0; syms++) {
		if (number == syms->number) {
			if (success)
				*success = 1;
			return (syms->name);
		}
	}

	snprintf(unname, sizeof unname, "%d", number);
	if (success)
		*success = 0;
	return (unname);
}
DEF_STRONG(__sym_ntos);

/*
 * Return a string for the type
 */
const char *
__p_type(int type)
{
	return (__sym_ntos (__p_type_syms, type, (int *)0));
}
DEF_STRONG(__p_type);

/*
 * Return a mnemonic for class
 */
const char *
__p_class(int class)
{
	return (__sym_ntos (__p_class_syms, class, (int *)0));
}
DEF_STRONG(__p_class);

