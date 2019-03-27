/*-
 * SPDX-License-Identifier: (ISC AND BSD-3-Clause)
 *
 * Portions Copyright (C) 2004, 2005, 2008, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Portions Copyright (C) 1996-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Copyright (c) 1985
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
 */

/*
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
 */

/*
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
 */

#if defined(LIBC_SCCS) && !defined(lint)
static const char sccsid[] = "@(#)res_debug.c	8.1 (Berkeley) 6/4/93";
static const char rcsid[] = "$Id: res_debug.c,v 1.19 2009/02/26 11:20:20 tbox Exp $";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "port_before.h"

#include <sys/param.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <netdb.h>
#include <resolv.h>
#include <resolv_mt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "port_after.h"

#ifdef SPRINTF_CHAR
# define SPRINTF(x) strlen(sprintf/**/x)
#else
# define SPRINTF(x) sprintf x
#endif

extern const char *_res_opcodes[];
extern const char *_res_sectioncodes[];

/*%
 * Print the current options.
 */
void
fp_resstat(const res_state statp, FILE *file) {
	u_long mask;

	fprintf(file, ";; res options:");
	for (mask = 1;  mask != 0U;  mask <<= 1)
		if (statp->options & mask)
			fprintf(file, " %s", p_option(mask));
	putc('\n', file);
}

static void
do_section(const res_state statp,
	   ns_msg *handle, ns_sect section,
	   int pflag, FILE *file)
{
	int n, sflag, rrnum;
	static int buflen = 2048;
	char *buf;
	ns_opcode opcode;
	ns_rr rr;

	/*
	 * Print answer records.
	 */
	sflag = (statp->pfcode & pflag);
	if (statp->pfcode && !sflag)
		return;

	buf = malloc(buflen);
	if (buf == NULL) {
		fprintf(file, ";; memory allocation failure\n");
		return;
	}

	opcode = (ns_opcode) ns_msg_getflag(*handle, ns_f_opcode);
	rrnum = 0;
	for (;;) {
		if (ns_parserr(handle, section, rrnum, &rr)) {
			if (errno != ENODEV)
				fprintf(file, ";; ns_parserr: %s\n",
					strerror(errno));
			else if (rrnum > 0 && sflag != 0 &&
				 (statp->pfcode & RES_PRF_HEAD1))
				putc('\n', file);
			goto cleanup;
		}
		if (rrnum == 0 && sflag != 0 && (statp->pfcode & RES_PRF_HEAD1))
			fprintf(file, ";; %s SECTION:\n",
				p_section(section, opcode));
		if (section == ns_s_qd)
			fprintf(file, ";;\t%s, type = %s, class = %s\n",
				ns_rr_name(rr),
				p_type(ns_rr_type(rr)),
				p_class(ns_rr_class(rr)));
		else if (section == ns_s_ar && ns_rr_type(rr) == ns_t_opt) {
			u_int16_t optcode, optlen, rdatalen = ns_rr_rdlen(rr);
			u_int32_t ttl = ns_rr_ttl(rr);

			fprintf(file,
				"; EDNS: version: %u, udp=%u, flags=%04x\n",
				(ttl>>16)&0xff, ns_rr_class(rr), ttl&0xffff);

			while (rdatalen >= 4) {
				const u_char *cp = ns_rr_rdata(rr);
				int i;

				GETSHORT(optcode, cp);
				GETSHORT(optlen, cp);

				if (optcode == NS_OPT_NSID) {
					fputs("; NSID: ", file);
					if (optlen == 0) {
						fputs("; NSID\n", file);
					} else {
						fputs("; NSID: ", file);
						for (i = 0; i < optlen; i++)
							fprintf(file, "%02x ",
								cp[i]);
						fputs(" (",file);
						for (i = 0; i < optlen; i++)
							fprintf(file, "%c",
								isprint(cp[i])?
								cp[i] : '.');
						fputs(")\n", file);
					}
				} else {
					if (optlen == 0) {
						fprintf(file, "; OPT=%u\n",
							optcode);
					} else {
						fprintf(file, "; OPT=%u: ",
							optcode);
						for (i = 0; i < optlen; i++)
							fprintf(file, "%02x ",
								cp[i]);
						fputs(" (",file);
						for (i = 0; i < optlen; i++)
							fprintf(file, "%c",
								isprint(cp[i]) ?
									cp[i] : '.');
						fputs(")\n", file);
					}
				}
				rdatalen -= 4 + optlen;
			}
		} else {
			n = ns_sprintrr(handle, &rr, NULL, NULL,
					buf, buflen);
			if (n < 0) {
				if (errno == ENOSPC) {
					free(buf);
					buf = NULL;
					if (buflen < 131072)
						buf = malloc(buflen += 1024);
					if (buf == NULL) {
						fprintf(file,
					      ";; memory allocation failure\n");
					      return;
					}
					continue;
				}
				fprintf(file, ";; ns_sprintrr: %s\n",
					strerror(errno));
				goto cleanup;
			}
			fputs(buf, file);
			fputc('\n', file);
		}
		rrnum++;
	}
 cleanup:
	if (buf != NULL)
		free(buf);
}

/*%
 * Print the contents of a query.
 * This is intended to be primarily a debugging routine.
 */
void
res_pquery(const res_state statp, const u_char *msg, int len, FILE *file) {
	ns_msg handle;
	int qdcount, ancount, nscount, arcount;
	u_int opcode, rcode, id;

	if (ns_initparse(msg, len, &handle) < 0) {
		fprintf(file, ";; ns_initparse: %s\n", strerror(errno));
		return;
	}
	opcode = ns_msg_getflag(handle, ns_f_opcode);
	rcode = ns_msg_getflag(handle, ns_f_rcode);
	id = ns_msg_id(handle);
	qdcount = ns_msg_count(handle, ns_s_qd);
	ancount = ns_msg_count(handle, ns_s_an);
	nscount = ns_msg_count(handle, ns_s_ns);
	arcount = ns_msg_count(handle, ns_s_ar);

	/*
	 * Print header fields.
	 */
	if ((!statp->pfcode) || (statp->pfcode & RES_PRF_HEADX) || rcode)
		fprintf(file,
			";; ->>HEADER<<- opcode: %s, status: %s, id: %d\n",
			_res_opcodes[opcode], p_rcode(rcode), id);
	if ((!statp->pfcode) || (statp->pfcode & RES_PRF_HEADX))
		putc(';', file);
	if ((!statp->pfcode) || (statp->pfcode & RES_PRF_HEAD2)) {
		fprintf(file, "; flags:");
		if (ns_msg_getflag(handle, ns_f_qr))
			fprintf(file, " qr");
		if (ns_msg_getflag(handle, ns_f_aa))
			fprintf(file, " aa");
		if (ns_msg_getflag(handle, ns_f_tc))
			fprintf(file, " tc");
		if (ns_msg_getflag(handle, ns_f_rd))
			fprintf(file, " rd");
		if (ns_msg_getflag(handle, ns_f_ra))
			fprintf(file, " ra");
		if (ns_msg_getflag(handle, ns_f_z))
			fprintf(file, " ??");
		if (ns_msg_getflag(handle, ns_f_ad))
			fprintf(file, " ad");
		if (ns_msg_getflag(handle, ns_f_cd))
			fprintf(file, " cd");
	}
	if ((!statp->pfcode) || (statp->pfcode & RES_PRF_HEAD1)) {
		fprintf(file, "; %s: %d",
			p_section(ns_s_qd, opcode), qdcount);
		fprintf(file, ", %s: %d",
			p_section(ns_s_an, opcode), ancount);
		fprintf(file, ", %s: %d",
			p_section(ns_s_ns, opcode), nscount);
		fprintf(file, ", %s: %d",
			p_section(ns_s_ar, opcode), arcount);
	}
	if ((!statp->pfcode) || (statp->pfcode &
		(RES_PRF_HEADX | RES_PRF_HEAD2 | RES_PRF_HEAD1))) {
		putc('\n',file);
	}
	/*
	 * Print the various sections.
	 */
	do_section(statp, &handle, ns_s_qd, RES_PRF_QUES, file);
	do_section(statp, &handle, ns_s_an, RES_PRF_ANS, file);
	do_section(statp, &handle, ns_s_ns, RES_PRF_AUTH, file);
	do_section(statp, &handle, ns_s_ar, RES_PRF_ADD, file);
	if (qdcount == 0 && ancount == 0 &&
	    nscount == 0 && arcount == 0)
		putc('\n', file);
}

const u_char *
p_cdnname(const u_char *cp, const u_char *msg, int len, FILE *file) {
	char name[MAXDNAME];
	int n;

	if ((n = dn_expand(msg, msg + len, cp, name, sizeof name)) < 0)
		return (NULL);
	if (name[0] == '\0')
		putc('.', file);
	else
		fputs(name, file);
	return (cp + n);
}

const u_char *
p_cdname(const u_char *cp, const u_char *msg, FILE *file) {
	return (p_cdnname(cp, msg, PACKETSZ, file));
}

/*%
 * Return a fully-qualified domain name from a compressed name (with
   length supplied).  */

const u_char *
p_fqnname(const u_char *cp, const u_char *msg, int msglen, char *name,
    int namelen)
{
	int n, newlen;

	if ((n = dn_expand(msg, cp + msglen, cp, name, namelen)) < 0)
		return (NULL);
	newlen = strlen(name);
	if (newlen == 0 || name[newlen - 1] != '.') {
		if (newlen + 1 >= namelen)	/*%< Lack space for final dot */
			return (NULL);
		else
			strcpy(name + newlen, ".");
	}
	return (cp + n);
}

/* XXX:	the rest of these functions need to become length-limited, too. */

const u_char *
p_fqname(const u_char *cp, const u_char *msg, FILE *file) {
	char name[MAXDNAME];
	const u_char *n;

	n = p_fqnname(cp, msg, MAXCDNAME, name, sizeof name);
	if (n == NULL)
		return (NULL);
	fputs(name, file);
	return (n);
}

/*%
 * Names of RR classes and qclasses.  Classes and qclasses are the same, except
 * that C_ANY is a qclass but not a class.  (You can ask for records of class
 * C_ANY, but you can't have any records of that class in the database.)
 */
const struct res_sym __p_class_syms[] = {
	{C_IN,		"IN",		(char *)0},
	{C_CHAOS,	"CH",		(char *)0},
	{C_CHAOS,	"CHAOS",	(char *)0},
	{C_HS,		"HS",		(char *)0},
	{C_HS,		"HESIOD",	(char *)0},
	{C_ANY,		"ANY",		(char *)0},
	{C_NONE,	"NONE",		(char *)0},
	{C_IN, 		(char *)0,	(char *)0}
};

/*%
 * Names of message sections.
 */
static const struct res_sym __p_default_section_syms[] = {
	{ns_s_qd,	"QUERY",	(char *)0},
	{ns_s_an,	"ANSWER",	(char *)0},
	{ns_s_ns,	"AUTHORITY",	(char *)0},
	{ns_s_ar,	"ADDITIONAL",	(char *)0},
	{0,		(char *)0,	(char *)0}
};

static const struct res_sym __p_update_section_syms[] = {
	{S_ZONE,	"ZONE",		(char *)0},
	{S_PREREQ,	"PREREQUISITE",	(char *)0},
	{S_UPDATE,	"UPDATE",	(char *)0},
	{S_ADDT,	"ADDITIONAL",	(char *)0},
	{0,		(char *)0,	(char *)0}
};

const struct res_sym __p_key_syms[] = {
	{NS_ALG_MD5RSA,		"RSA",		"RSA KEY with MD5 hash"},
	{NS_ALG_DH,		"DH",		"Diffie Hellman"},
	{NS_ALG_DSA,		"DSA",		"Digital Signature Algorithm"},
	{NS_ALG_EXPIRE_ONLY,	"EXPIREONLY",	"No algorithm"},
	{NS_ALG_PRIVATE_OID,	"PRIVATE",	"Algorithm obtained from OID"},
	{0,			NULL,		NULL}
};

const struct res_sym __p_cert_syms[] = {
	{cert_t_pkix,	"PKIX",		"PKIX (X.509v3) Certificate"},
	{cert_t_spki,	"SPKI",		"SPKI certificate"},
	{cert_t_pgp,	"PGP",		"PGP certificate"},
	{cert_t_url,	"URL",		"URL Private"},
	{cert_t_oid,	"OID",		"OID Private"},
	{0,		NULL,		NULL}
};

/*%
 * Names of RR types and qtypes.  Types and qtypes are the same, except
 * that T_ANY is a qtype but not a type.  (You can ask for records of type
 * T_ANY, but you can't have any records of that type in the database.)
 */
const struct res_sym __p_type_syms[] = {
	{ns_t_a,	"A",		"address"},
	{ns_t_ns,	"NS",		"name server"},
	{ns_t_md,	"MD",		"mail destination (deprecated)"},
	{ns_t_mf,	"MF",		"mail forwarder (deprecated)"},
	{ns_t_cname,	"CNAME",	"canonical name"},
	{ns_t_soa,	"SOA",		"start of authority"},
	{ns_t_mb,	"MB",		"mailbox"},
	{ns_t_mg,	"MG",		"mail group member"},
	{ns_t_mr,	"MR",		"mail rename"},
	{ns_t_null,	"NULL",		"null"},
	{ns_t_wks,	"WKS",		"well-known service (deprecated)"},
	{ns_t_ptr,	"PTR",		"domain name pointer"},
	{ns_t_hinfo,	"HINFO",	"host information"},
	{ns_t_minfo,	"MINFO",	"mailbox information"},
	{ns_t_mx,	"MX",		"mail exchanger"},
	{ns_t_txt,	"TXT",		"text"},
	{ns_t_rp,	"RP",		"responsible person"},
	{ns_t_afsdb,	"AFSDB",	"DCE or AFS server"},
	{ns_t_x25,	"X25",		"X25 address"},
	{ns_t_isdn,	"ISDN",		"ISDN address"},
	{ns_t_rt,	"RT",		"router"},
	{ns_t_nsap,	"NSAP",		"nsap address"},
	{ns_t_nsap_ptr,	"NSAP_PTR",	"domain name pointer"},
	{ns_t_sig,	"SIG",		"signature"},
	{ns_t_key,	"KEY",		"key"},
	{ns_t_px,	"PX",		"mapping information"},
	{ns_t_gpos,	"GPOS",		"geographical position (withdrawn)"},
	{ns_t_aaaa,	"AAAA",		"IPv6 address"},
	{ns_t_loc,	"LOC",		"location"},
	{ns_t_nxt,	"NXT",		"next valid name (unimplemented)"},
	{ns_t_eid,	"EID",		"endpoint identifier (unimplemented)"},
	{ns_t_nimloc,	"NIMLOC",	"NIMROD locator (unimplemented)"},
	{ns_t_srv,	"SRV",		"server selection"},
	{ns_t_atma,	"ATMA",		"ATM address (unimplemented)"},
	{ns_t_naptr,	"NAPTR",	"naptr"},
	{ns_t_kx,	"KX",		"key exchange"},
	{ns_t_cert,	"CERT",		"certificate"},
	{ns_t_a6,	"A",		"IPv6 address (experminental)"},
	{ns_t_dname,	"DNAME",	"non-terminal redirection"},
	{ns_t_opt,	"OPT",		"opt"},
	{ns_t_apl,	"apl",		"apl"},
	{ns_t_ds,	"DS",		"delegation signer"},
	{ns_t_sshfp,	"SSFP",		"SSH fingerprint"},
	{ns_t_ipseckey,	"IPSECKEY",	"IPSEC key"},
	{ns_t_rrsig,	"RRSIG",	"rrsig"},
	{ns_t_nsec,	"NSEC",		"nsec"},
	{ns_t_dnskey,	"DNSKEY",	"DNS key"},
	{ns_t_dhcid,	"DHCID",       "dynamic host configuration identifier"},
	{ns_t_nsec3,	"NSEC3",	"nsec3"},
	{ns_t_nsec3param, "NSEC3PARAM", "NSEC3 parameters"},
	{ns_t_hip,	"HIP",		"host identity protocol"},
	{ns_t_spf,	"SPF",		"sender policy framework"},
	{ns_t_tkey,	"TKEY",		"tkey"},
	{ns_t_tsig,	"TSIG",		"transaction signature"},
	{ns_t_ixfr,	"IXFR",		"incremental zone transfer"},
	{ns_t_axfr,	"AXFR",		"zone transfer"},
	{ns_t_zxfr,	"ZXFR",		"compressed zone transfer"},
	{ns_t_mailb,	"MAILB",	"mailbox-related data (deprecated)"},
	{ns_t_maila,	"MAILA",	"mail agent (deprecated)"},
	{ns_t_naptr,	"NAPTR",	"URN Naming Authority"},
	{ns_t_kx,	"KX",		"Key Exchange"},
	{ns_t_cert,	"CERT",		"Certificate"},
	{ns_t_a6,	"A6",		"IPv6 Address"},
	{ns_t_dname,	"DNAME",	"dname"},
	{ns_t_sink,	"SINK",		"Kitchen Sink (experimental)"},
	{ns_t_opt,	"OPT",		"EDNS Options"},
	{ns_t_any,	"ANY",		"\"any\""},
	{ns_t_dlv,	"DLV",		"DNSSEC look-aside validation"},
	{0, 		NULL,		NULL}
};

/*%
 * Names of DNS rcodes.
 */
const struct res_sym __p_rcode_syms[] = {
	{ns_r_noerror,	"NOERROR",		"no error"},
	{ns_r_formerr,	"FORMERR",		"format error"},
	{ns_r_servfail,	"SERVFAIL",		"server failed"},
	{ns_r_nxdomain,	"NXDOMAIN",		"no such domain name"},
	{ns_r_notimpl,	"NOTIMP",		"not implemented"},
	{ns_r_refused,	"REFUSED",		"refused"},
	{ns_r_yxdomain,	"YXDOMAIN",		"domain name exists"},
	{ns_r_yxrrset,	"YXRRSET",		"rrset exists"},
	{ns_r_nxrrset,	"NXRRSET",		"rrset doesn't exist"},
	{ns_r_notauth,	"NOTAUTH",		"not authoritative"},
	{ns_r_notzone,	"NOTZONE",		"Not in zone"},
	{ns_r_max,	"",			""},
	{ns_r_badsig,	"BADSIG",		"bad signature"},
	{ns_r_badkey,	"BADKEY",		"bad key"},
	{ns_r_badtime,	"BADTIME",		"bad time"},
	{0, 		NULL,			NULL}
};

int
sym_ston(const struct res_sym *syms, const char *name, int *success) {
	for ((void)NULL; syms->name != 0; syms++) {
		if (strcasecmp (name, syms->name) == 0) {
			if (success)
				*success = 1;
			return (syms->number);
		}
	}
	if (success)
		*success = 0;
	return (syms->number);		/*%< The default value. */
}

const char *
sym_ntos(const struct res_sym *syms, int number, int *success) {
	char *unname = sym_ntos_unname;

	for ((void)NULL; syms->name != 0; syms++) {
		if (number == syms->number) {
			if (success)
				*success = 1;
			return (syms->name);
		}
	}

	sprintf(unname, "%d", number);		/*%< XXX nonreentrant */
	if (success)
		*success = 0;
	return (unname);
}

const char *
sym_ntop(const struct res_sym *syms, int number, int *success) {
	char *unname = sym_ntop_unname;

	for ((void)NULL; syms->name != 0; syms++) {
		if (number == syms->number) {
			if (success)
				*success = 1;
			return (syms->humanname);
		}
	}
	sprintf(unname, "%d", number);		/*%< XXX nonreentrant */
	if (success)
		*success = 0;
	return (unname);
}

/*%
 * Return a string for the type.
 */
const char *
p_type(int type) {
	int success;
	const char *result;
	static char typebuf[20];

	result = sym_ntos(__p_type_syms, type, &success);
	if (success)
		return (result);
	if (type < 0 || type > 0xffff)
		return ("BADTYPE");
	sprintf(typebuf, "TYPE%d", type);
	return (typebuf);
}

/*%
 * Return a string for the type.
 */
const char *
p_section(int section, int opcode) {
	const struct res_sym *symbols;

	switch (opcode) {
	case ns_o_update:
		symbols = __p_update_section_syms;
		break;
	default:
		symbols = __p_default_section_syms;
		break;
	}
	return (sym_ntos(symbols, section, (int *)0));
}

/*%
 * Return a mnemonic for class.
 */
const char *
p_class(int class) {
	int success;
	const char *result;
	static char classbuf[20];

	result = sym_ntos(__p_class_syms, class, &success);
	if (success)
		return (result);
	if (class < 0 || class > 0xffff)
		return ("BADCLASS");
	sprintf(classbuf, "CLASS%d", class);
	return (classbuf);
}

/*%
 * Return a mnemonic for an option
 */
const char *
p_option(u_long option) {
	char *nbuf = p_option_nbuf;

	switch (option) {
	case RES_INIT:		return "init";
	case RES_DEBUG:		return "debug";
	case RES_AAONLY:	return "aaonly(unimpl)";
	case RES_USEVC:		return "usevc";
	case RES_PRIMARY:	return "primry(unimpl)";
	case RES_IGNTC:		return "igntc";
	case RES_RECURSE:	return "recurs";
	case RES_DEFNAMES:	return "defnam";
	case RES_STAYOPEN:	return "styopn";
	case RES_DNSRCH:	return "dnsrch";
	case RES_INSECURE1:	return "insecure1";
	case RES_INSECURE2:	return "insecure2";
	case RES_NOALIASES:	return "noaliases";
	case RES_USE_INET6:	return "inet6";
#ifdef RES_USE_EDNS0	/*%< KAME extension */
	case RES_USE_EDNS0:	return "edns0";
	case RES_NSID:		return "nsid";
#endif
#ifdef RES_USE_DNAME
	case RES_USE_DNAME:	return "dname";
#endif
#ifdef RES_USE_DNSSEC
	case RES_USE_DNSSEC:	return "dnssec";
#endif
#ifdef RES_NOTLDQUERY
	case RES_NOTLDQUERY:	return "no-tld-query";
#endif
#ifdef RES_NO_NIBBLE2
	case RES_NO_NIBBLE2:	return "no-nibble2";
#endif
				/* XXX nonreentrant */
	default:		sprintf(nbuf, "?0x%lx?", (u_long)option);
				return (nbuf);
	}
}

/*%
 * Return a mnemonic for a time to live.
 */
const char *
p_time(u_int32_t value) {
	char *nbuf = p_time_nbuf;

	if (ns_format_ttl(value, nbuf, sizeof nbuf) < 0)
		sprintf(nbuf, "%u", value);
	return (nbuf);
}

/*%
 * Return a string for the rcode.
 */
const char *
p_rcode(int rcode) {
	return (sym_ntos(__p_rcode_syms, rcode, (int *)0));
}

/*%
 * Return a string for a res_sockaddr_union.
 */
const char *
p_sockun(union res_sockaddr_union u, char *buf, size_t size) {
	char ret[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:123.123.123.123"];

	switch (u.sin.sin_family) {
	case AF_INET:
		inet_ntop(AF_INET, &u.sin.sin_addr, ret, sizeof ret);
		break;
#ifdef HAS_INET6_STRUCTS
	case AF_INET6:
		inet_ntop(AF_INET6, &u.sin6.sin6_addr, ret, sizeof ret);
		break;
#endif
	default:
		sprintf(ret, "[af%d]", u.sin.sin_family);
		break;
	}
	if (size > 0U) {
		strncpy(buf, ret, size - 1);
		buf[size - 1] = '0';
	}
	return (buf);
}

/*%
 * routines to convert between on-the-wire RR format and zone file format.
 * Does not contain conversion to/from decimal degrees; divide or multiply
 * by 60*60*1000 for that.
 */

static unsigned int poweroften[10] = {1, 10, 100, 1000, 10000, 100000,
				      1000000,10000000,100000000,1000000000};

/*% takes an XeY precision/size value, returns a string representation. */
static const char *
precsize_ntoa(u_int8_t prec)
{
	char *retbuf = precsize_ntoa_retbuf;
	unsigned long val;
	int mantissa, exponent;

	mantissa = (int)((prec >> 4) & 0x0f) % 10;
	exponent = (int)((prec >> 0) & 0x0f) % 10;

	val = mantissa * poweroften[exponent];

	(void) sprintf(retbuf, "%lu.%.2lu", val/100, val%100);
	return (retbuf);
}

/*% converts ascii size/precision X * 10**Y(cm) to 0xXY.  moves pointer.  */
static u_int8_t
precsize_aton(const char **strptr) {
	unsigned int mval = 0, cmval = 0;
	u_int8_t retval = 0;
	const char *cp;
	int exponent;
	int mantissa;

	cp = *strptr;

	while (isdigit((unsigned char)*cp))
		mval = mval * 10 + (*cp++ - '0');

	if (*cp == '.') {		/*%< centimeters */
		cp++;
		if (isdigit((unsigned char)*cp)) {
			cmval = (*cp++ - '0') * 10;
			if (isdigit((unsigned char)*cp)) {
				cmval += (*cp++ - '0');
			}
		}
	}
	cmval = (mval * 100) + cmval;

	for (exponent = 0; exponent < 9; exponent++)
		if (cmval < poweroften[exponent+1])
			break;

	mantissa = cmval / poweroften[exponent];
	if (mantissa > 9)
		mantissa = 9;

	retval = (mantissa << 4) | exponent;

	*strptr = cp;

	return (retval);
}

/*% converts ascii lat/lon to unsigned encoded 32-bit number.  moves pointer. */
static u_int32_t
latlon2ul(const char **latlonstrptr, int *which) {
	const char *cp;
	u_int32_t retval;
	int deg = 0, min = 0, secs = 0, secsfrac = 0;

	cp = *latlonstrptr;

	while (isdigit((unsigned char)*cp))
		deg = deg * 10 + (*cp++ - '0');

	while (isspace((unsigned char)*cp))
		cp++;

	if (!(isdigit((unsigned char)*cp)))
		goto fndhemi;

	while (isdigit((unsigned char)*cp))
		min = min * 10 + (*cp++ - '0');

	while (isspace((unsigned char)*cp))
		cp++;

	if (!(isdigit((unsigned char)*cp)))
		goto fndhemi;

	while (isdigit((unsigned char)*cp))
		secs = secs * 10 + (*cp++ - '0');

	if (*cp == '.') {		/*%< decimal seconds */
		cp++;
		if (isdigit((unsigned char)*cp)) {
			secsfrac = (*cp++ - '0') * 100;
			if (isdigit((unsigned char)*cp)) {
				secsfrac += (*cp++ - '0') * 10;
				if (isdigit((unsigned char)*cp)) {
					secsfrac += (*cp++ - '0');
				}
			}
		}
	}

	while (!isspace((unsigned char)*cp))	/*%< if any trailing garbage */
		cp++;

	while (isspace((unsigned char)*cp))
		cp++;

 fndhemi:
	switch (*cp) {
	case 'N': case 'n':
	case 'E': case 'e':
		retval = ((unsigned)1<<31)
			+ (((((deg * 60) + min) * 60) + secs) * 1000)
			+ secsfrac;
		break;
	case 'S': case 's':
	case 'W': case 'w':
		retval = ((unsigned)1<<31)
			- (((((deg * 60) + min) * 60) + secs) * 1000)
			- secsfrac;
		break;
	default:
		retval = 0;	/*%< invalid value -- indicates error */
		break;
	}

	switch (*cp) {
	case 'N': case 'n':
	case 'S': case 's':
		*which = 1;	/*%< latitude */
		break;
	case 'E': case 'e':
	case 'W': case 'w':
		*which = 2;	/*%< longitude */
		break;
	default:
		*which = 0;	/*%< error */
		break;
	}

	cp++;			/*%< skip the hemisphere */
	while (!isspace((unsigned char)*cp))	/*%< if any trailing garbage */
		cp++;

	while (isspace((unsigned char)*cp))	/*%< move to next field */
		cp++;

	*latlonstrptr = cp;

	return (retval);
}

/*%
 * converts a zone file representation in a string to an RDATA on-the-wire
 * representation. */
int
loc_aton(const char *ascii, u_char *binary)
{
	const char *cp, *maxcp;
	u_char *bcp;

	u_int32_t latit = 0, longit = 0, alt = 0;
	u_int32_t lltemp1 = 0, lltemp2 = 0;
	int altmeters = 0, altfrac = 0, altsign = 1;
	u_int8_t hp = 0x16;	/*%< default = 1e6 cm = 10000.00m = 10km */
	u_int8_t vp = 0x13;	/*%< default = 1e3 cm = 10.00m */
	u_int8_t siz = 0x12;	/*%< default = 1e2 cm = 1.00m */
	int which1 = 0, which2 = 0;

	cp = ascii;
	maxcp = cp + strlen(ascii);

	lltemp1 = latlon2ul(&cp, &which1);

	lltemp2 = latlon2ul(&cp, &which2);

	switch (which1 + which2) {
	case 3:			/*%< 1 + 2, the only valid combination */
		if ((which1 == 1) && (which2 == 2)) { /*%< normal case */
			latit = lltemp1;
			longit = lltemp2;
		} else if ((which1 == 2) && (which2 == 1)) { /*%< reversed */
			longit = lltemp1;
			latit = lltemp2;
		} else {	/*%< some kind of brokenness */
			return (0);
		}
		break;
	default:		/*%< we didn't get one of each */
		return (0);
	}

	/* altitude */
	if (*cp == '-') {
		altsign = -1;
		cp++;
	}

	if (*cp == '+')
		cp++;

	while (isdigit((unsigned char)*cp))
		altmeters = altmeters * 10 + (*cp++ - '0');

	if (*cp == '.') {		/*%< decimal meters */
		cp++;
		if (isdigit((unsigned char)*cp)) {
			altfrac = (*cp++ - '0') * 10;
			if (isdigit((unsigned char)*cp)) {
				altfrac += (*cp++ - '0');
			}
		}
	}

	alt = (10000000 + (altsign * (altmeters * 100 + altfrac)));

	while (!isspace((unsigned char)*cp) && (cp < maxcp)) /*%< if trailing garbage or m */
		cp++;

	while (isspace((unsigned char)*cp) && (cp < maxcp))
		cp++;

	if (cp >= maxcp)
		goto defaults;

	siz = precsize_aton(&cp);

	while (!isspace((unsigned char)*cp) && (cp < maxcp))	/*%< if trailing garbage or m */
		cp++;

	while (isspace((unsigned char)*cp) && (cp < maxcp))
		cp++;

	if (cp >= maxcp)
		goto defaults;

	hp = precsize_aton(&cp);

	while (!isspace((unsigned char)*cp) && (cp < maxcp))	/*%< if trailing garbage or m */
		cp++;

	while (isspace((unsigned char)*cp) && (cp < maxcp))
		cp++;

	if (cp >= maxcp)
		goto defaults;

	vp = precsize_aton(&cp);

 defaults:

	bcp = binary;
	*bcp++ = (u_int8_t) 0;	/*%< version byte */
	*bcp++ = siz;
	*bcp++ = hp;
	*bcp++ = vp;
	PUTLONG(latit,bcp);
	PUTLONG(longit,bcp);
	PUTLONG(alt,bcp);

	return (16);		/*%< size of RR in octets */
}

/*% takes an on-the-wire LOC RR and formats it in a human readable format. */
const char *
loc_ntoa(const u_char *binary, char *ascii)
{
	static const char *error = "?";
	static char tmpbuf[sizeof
"1000 60 60.000 N 1000 60 60.000 W -12345678.00m 90000000.00m 90000000.00m 90000000.00m"];
	const u_char *cp = binary;

	int latdeg, latmin, latsec, latsecfrac;
	int longdeg, longmin, longsec, longsecfrac;
	char northsouth, eastwest;
	const char *altsign;
	int altmeters, altfrac;

	const u_int32_t referencealt = 100000 * 100;

	int32_t latval, longval, altval;
	u_int32_t templ;
	u_int8_t sizeval, hpval, vpval, versionval;

	char *sizestr, *hpstr, *vpstr;

	versionval = *cp++;

	if (ascii == NULL)
		ascii = tmpbuf;

	if (versionval) {
		(void) sprintf(ascii, "; error: unknown LOC RR version");
		return (ascii);
	}

	sizeval = *cp++;

	hpval = *cp++;
	vpval = *cp++;

	GETLONG(templ, cp);
	latval = (templ - ((unsigned)1<<31));

	GETLONG(templ, cp);
	longval = (templ - ((unsigned)1<<31));

	GETLONG(templ, cp);
	if (templ < referencealt) { /*%< below WGS 84 spheroid */
		altval = referencealt - templ;
		altsign = "-";
	} else {
		altval = templ - referencealt;
		altsign = "";
	}

	if (latval < 0) {
		northsouth = 'S';
		latval = -latval;
	} else
		northsouth = 'N';

	latsecfrac = latval % 1000;
	latval = latval / 1000;
	latsec = latval % 60;
	latval = latval / 60;
	latmin = latval % 60;
	latval = latval / 60;
	latdeg = latval;

	if (longval < 0) {
		eastwest = 'W';
		longval = -longval;
	} else
		eastwest = 'E';

	longsecfrac = longval % 1000;
	longval = longval / 1000;
	longsec = longval % 60;
	longval = longval / 60;
	longmin = longval % 60;
	longval = longval / 60;
	longdeg = longval;

	altfrac = altval % 100;
	altmeters = (altval / 100);

	sizestr = strdup(precsize_ntoa(sizeval));
	hpstr = strdup(precsize_ntoa(hpval));
	vpstr = strdup(precsize_ntoa(vpval));

	sprintf(ascii,
	    "%d %.2d %.2d.%.3d %c %d %.2d %.2d.%.3d %c %s%d.%.2dm %sm %sm %sm",
		latdeg, latmin, latsec, latsecfrac, northsouth,
		longdeg, longmin, longsec, longsecfrac, eastwest,
		altsign, altmeters, altfrac,
		(sizestr != NULL) ? sizestr : error,
		(hpstr != NULL) ? hpstr : error,
		(vpstr != NULL) ? vpstr : error);

	if (sizestr != NULL)
		free(sizestr);
	if (hpstr != NULL)
		free(hpstr);
	if (vpstr != NULL)
		free(vpstr);

	return (ascii);
}


/*% Return the number of DNS hierarchy levels in the name. */
int
dn_count_labels(const char *name) {
	int i, len, count;

	len = strlen(name);
	for (i = 0, count = 0; i < len; i++) {
		/* XXX need to check for \. or use named's nlabels(). */
		if (name[i] == '.')
			count++;
	}

	/* don't count initial wildcard */
	if (name[0] == '*')
		if (count)
			count--;

	/* don't count the null label for root. */
	/* if terminating '.' not found, must adjust */
	/* count to include last label */
	if (len > 0 && name[len-1] != '.')
		count++;
	return (count);
}

/*%
 * Make dates expressed in seconds-since-Jan-1-1970 easy to read.
 * SIG records are required to be printed like this, by the Secure DNS RFC.
 */
char *
p_secstodate (u_long secs) {
	char *output = p_secstodate_output;
	time_t clock = secs;
	struct tm *time;
#ifdef HAVE_TIME_R
	struct tm res;

	time = gmtime_r(&clock, &res);
#else
	time = gmtime(&clock);
#endif
	time->tm_year += 1900;
	time->tm_mon += 1;
	sprintf(output, "%04d%02d%02d%02d%02d%02d",
		time->tm_year, time->tm_mon, time->tm_mday,
		time->tm_hour, time->tm_min, time->tm_sec);
	return (output);
}

u_int16_t
res_nametoclass(const char *buf, int *successp) {
	unsigned long result;
	char *endptr;
	int success;

	result = sym_ston(__p_class_syms, buf, &success);
	if (success)
		goto done;

	if (strncasecmp(buf, "CLASS", 5) != 0 ||
	    !isdigit((unsigned char)buf[5]))
		goto done;
	errno = 0;
	result = strtoul(buf + 5, &endptr, 10);
	if (errno == 0 && *endptr == '\0' && result <= 0xffffU)
		success = 1;
 done:
	if (successp)
		*successp = success;
	return (result);
}

u_int16_t
res_nametotype(const char *buf, int *successp) {
	unsigned long result;
	char *endptr;
	int success;

	result = sym_ston(__p_type_syms, buf, &success);
	if (success)
		goto done;

	if (strncasecmp(buf, "type", 4) != 0 ||
	    !isdigit((unsigned char)buf[4]))
		goto done;
	errno = 0;
	result = strtoul(buf + 4, &endptr, 10);
	if (errno == 0 && *endptr == '\0' && result <= 0xffffU)
		success = 1;
 done:
	if (successp)
		*successp = success;
	return (result);
}

/*
 * Weak aliases for applications that use certain private entry points,
 * and fail to include <resolv.h>.
 */
#undef fp_resstat
__weak_reference(__fp_resstat, fp_resstat);
#undef p_fqnname
__weak_reference(__p_fqnname, p_fqnname);
#undef sym_ston
__weak_reference(__sym_ston, sym_ston);
#undef sym_ntos
__weak_reference(__sym_ntos, sym_ntos);
#undef sym_ntop
__weak_reference(__sym_ntop, sym_ntop);
#undef dn_count_labels
__weak_reference(__dn_count_labels, dn_count_labels);
#undef p_secstodate
__weak_reference(__p_secstodate, p_secstodate);

/*! \file */
