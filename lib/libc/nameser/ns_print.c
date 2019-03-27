/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef lint
static const char rcsid[] = "$Id: ns_print.c,v 1.12 2009/03/03 05:29:58 each Exp $";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Import. */

#include "port_before.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>

#ifdef _LIBC
#include <assert.h>
#define INSIST(cond)	assert(cond)
#else
#include <isc/assertions.h>
#include <isc/dst.h>
#endif
#include <errno.h>
#include <resolv.h>
#include <string.h>
#include <ctype.h>

#include "port_after.h"

#ifdef SPRINTF_CHAR
# define SPRINTF(x) strlen(sprintf/**/x)
#else
# define SPRINTF(x) ((size_t)sprintf x)
#endif

/* Forward. */

static size_t	prune_origin(const char *name, const char *origin);
static int	charstr(const u_char *rdata, const u_char *edata,
			char **buf, size_t *buflen);
static int	addname(const u_char *msg, size_t msglen,
			const u_char **p, const char *origin,
			char **buf, size_t *buflen);
static void	addlen(size_t len, char **buf, size_t *buflen);
static int	addstr(const char *src, size_t len,
		       char **buf, size_t *buflen);
static int	addtab(size_t len, size_t target, int spaced,
		       char **buf, size_t *buflen);

/* Macros. */

#define	T(x) \
	do { \
		if ((x) < 0) \
			return (-1); \
	} while (0)

static const char base32hex[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUV=0123456789abcdefghijklmnopqrstuv";

/* Public. */

/*%
 *	Convert an RR to presentation format.
 *
 * return:
 *\li	Number of characters written to buf, or -1 (check errno).
 */
int
ns_sprintrr(const ns_msg *handle, const ns_rr *rr,
	    const char *name_ctx, const char *origin,
	    char *buf, size_t buflen)
{
	int n;

	n = ns_sprintrrf(ns_msg_base(*handle), ns_msg_size(*handle),
			 ns_rr_name(*rr), ns_rr_class(*rr), ns_rr_type(*rr),
			 ns_rr_ttl(*rr), ns_rr_rdata(*rr), ns_rr_rdlen(*rr),
			 name_ctx, origin, buf, buflen);
	return (n);
}

/*%
 *	Convert the fields of an RR into presentation format.
 *
 * return:
 *\li	Number of characters written to buf, or -1 (check errno).
 */
int
ns_sprintrrf(const u_char *msg, size_t msglen,
	    const char *name, ns_class class, ns_type type,
	    u_long ttl, const u_char *rdata, size_t rdlen,
	    const char *name_ctx, const char *origin,
	    char *buf, size_t buflen)
{
	const char *obuf = buf;
	const u_char *edata = rdata + rdlen;
	int spaced = 0;

	const char *comment;
	char tmp[100];
	int len, x;

	/*
	 * Owner.
	 */
	if (name_ctx != NULL && ns_samename(name_ctx, name) == 1) {
		T(addstr("\t\t\t", 3, &buf, &buflen));
	} else {
		len = prune_origin(name, origin);
		if (*name == '\0') {
			goto root;
		} else if (len == 0) {
			T(addstr("@\t\t\t", 4, &buf, &buflen));
		} else {
			T(addstr(name, len, &buf, &buflen));
			/* Origin not used or not root, and no trailing dot? */
			if (((origin == NULL || origin[0] == '\0') ||
			    (origin[0] != '.' && origin[1] != '\0' &&
			    name[len] == '\0')) && name[len - 1] != '.') {
 root:
				T(addstr(".", 1, &buf, &buflen));
				len++;
			}
			T(spaced = addtab(len, 24, spaced, &buf, &buflen));
		}
	}

	/*
	 * TTL, Class, Type.
	 */
	T(x = ns_format_ttl(ttl, buf, buflen));
	addlen(x, &buf, &buflen);
	len = SPRINTF((tmp, " %s %s", p_class(class), p_type(type)));
	T(addstr(tmp, len, &buf, &buflen));
	T(spaced = addtab(x + len, 16, spaced, &buf, &buflen));

	/*
	 * RData.
	 */
	switch (type) {
	case ns_t_a:
		if (rdlen != (size_t)NS_INADDRSZ)
			goto formerr;
		(void) inet_ntop(AF_INET, rdata, buf, buflen);
		addlen(strlen(buf), &buf, &buflen);
		break;

	case ns_t_cname:
	case ns_t_mb:
	case ns_t_mg:
	case ns_t_mr:
	case ns_t_ns:
	case ns_t_ptr:
	case ns_t_dname:
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));
		break;

	case ns_t_hinfo:
	case ns_t_isdn:
		/* First word. */
		T(len = charstr(rdata, edata, &buf, &buflen));
		if (len == 0)
			goto formerr;
		rdata += len;
		T(addstr(" ", 1, &buf, &buflen));

		    
		/* Second word, optional in ISDN records. */
		if (type == ns_t_isdn && rdata == edata)
			break;
		    
		T(len = charstr(rdata, edata, &buf, &buflen));
		if (len == 0)
			goto formerr;
		rdata += len;
		break;

	case ns_t_soa: {
		u_long t;

		/* Server name. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));
		T(addstr(" ", 1, &buf, &buflen));

		/* Administrator name. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));
		T(addstr(" (\n", 3, &buf, &buflen));
		spaced = 0;

		if ((edata - rdata) != 5*NS_INT32SZ)
			goto formerr;

		/* Serial number. */
		t = ns_get32(rdata);  rdata += NS_INT32SZ;
		T(addstr("\t\t\t\t\t", 5, &buf, &buflen));
		len = SPRINTF((tmp, "%lu", t));
		T(addstr(tmp, len, &buf, &buflen));
		T(spaced = addtab(len, 16, spaced, &buf, &buflen));
		T(addstr("; serial\n", 9, &buf, &buflen));
		spaced = 0;

		/* Refresh interval. */
		t = ns_get32(rdata);  rdata += NS_INT32SZ;
		T(addstr("\t\t\t\t\t", 5, &buf, &buflen));
		T(len = ns_format_ttl(t, buf, buflen));
		addlen(len, &buf, &buflen);
		T(spaced = addtab(len, 16, spaced, &buf, &buflen));
		T(addstr("; refresh\n", 10, &buf, &buflen));
		spaced = 0;

		/* Retry interval. */
		t = ns_get32(rdata);  rdata += NS_INT32SZ;
		T(addstr("\t\t\t\t\t", 5, &buf, &buflen));
		T(len = ns_format_ttl(t, buf, buflen));
		addlen(len, &buf, &buflen);
		T(spaced = addtab(len, 16, spaced, &buf, &buflen));
		T(addstr("; retry\n", 8, &buf, &buflen));
		spaced = 0;

		/* Expiry. */
		t = ns_get32(rdata);  rdata += NS_INT32SZ;
		T(addstr("\t\t\t\t\t", 5, &buf, &buflen));
		T(len = ns_format_ttl(t, buf, buflen));
		addlen(len, &buf, &buflen);
		T(spaced = addtab(len, 16, spaced, &buf, &buflen));
		T(addstr("; expiry\n", 9, &buf, &buflen));
		spaced = 0;

		/* Minimum TTL. */
		t = ns_get32(rdata);  rdata += NS_INT32SZ;
		T(addstr("\t\t\t\t\t", 5, &buf, &buflen));
		T(len = ns_format_ttl(t, buf, buflen));
		addlen(len, &buf, &buflen);
		T(addstr(" )", 2, &buf, &buflen));
		T(spaced = addtab(len, 16, spaced, &buf, &buflen));
		T(addstr("; minimum\n", 10, &buf, &buflen));

		break;
	    }

	case ns_t_mx:
	case ns_t_afsdb:
	case ns_t_rt:
	case ns_t_kx: {
		u_int t;

		if (rdlen < (size_t)NS_INT16SZ)
			goto formerr;

		/* Priority. */
		t = ns_get16(rdata);
		rdata += NS_INT16SZ;
		len = SPRINTF((tmp, "%u ", t));
		T(addstr(tmp, len, &buf, &buflen));

		/* Target. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));

		break;
	    }

	case ns_t_px: {
		u_int t;

		if (rdlen < (size_t)NS_INT16SZ)
			goto formerr;

		/* Priority. */
		t = ns_get16(rdata);
		rdata += NS_INT16SZ;
		len = SPRINTF((tmp, "%u ", t));
		T(addstr(tmp, len, &buf, &buflen));

		/* Name1. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));
		T(addstr(" ", 1, &buf, &buflen));

		/* Name2. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));

		break;
	    }

	case ns_t_x25:
		T(len = charstr(rdata, edata, &buf, &buflen));
		if (len == 0)
			goto formerr;
		rdata += len;
		break;

	case ns_t_txt:
	case ns_t_spf:
		while (rdata < edata) {
			T(len = charstr(rdata, edata, &buf, &buflen));
			if (len == 0)
				goto formerr;
			rdata += len;
			if (rdata < edata)
				T(addstr(" ", 1, &buf, &buflen));
		}
		break;

	case ns_t_nsap: {
		char t[2+255*3];

		(void) inet_nsap_ntoa(rdlen, rdata, t);
		T(addstr(t, strlen(t), &buf, &buflen));
		break;
	    }

	case ns_t_aaaa:
		if (rdlen != (size_t)NS_IN6ADDRSZ)
			goto formerr;
		(void) inet_ntop(AF_INET6, rdata, buf, buflen);
		addlen(strlen(buf), &buf, &buflen);
		break;

	case ns_t_loc: {
		char t[255];

		/* XXX protocol format checking? */
		(void) loc_ntoa(rdata, t);
		T(addstr(t, strlen(t), &buf, &buflen));
		break;
	    }

	case ns_t_naptr: {
		u_int order, preference;
		char t[50];

		if (rdlen < 2U*NS_INT16SZ)
			goto formerr;

		/* Order, Precedence. */
		order = ns_get16(rdata);	rdata += NS_INT16SZ;
		preference = ns_get16(rdata);	rdata += NS_INT16SZ;
		len = SPRINTF((t, "%u %u ", order, preference));
		T(addstr(t, len, &buf, &buflen));

		/* Flags. */
		T(len = charstr(rdata, edata, &buf, &buflen));
		if (len == 0)
			goto formerr;
		rdata += len;
		T(addstr(" ", 1, &buf, &buflen));

		/* Service. */
		T(len = charstr(rdata, edata, &buf, &buflen));
		if (len == 0)
			goto formerr;
		rdata += len;
		T(addstr(" ", 1, &buf, &buflen));

		/* Regexp. */
		T(len = charstr(rdata, edata, &buf, &buflen));
		if (len < 0)
			return (-1);
		if (len == 0)
			goto formerr;
		rdata += len;
		T(addstr(" ", 1, &buf, &buflen));

		/* Server. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));
		break;
	    }

	case ns_t_srv: {
		u_int priority, weight, port;
		char t[50];

		if (rdlen < 3U*NS_INT16SZ)
			goto formerr;

		/* Priority, Weight, Port. */
		priority = ns_get16(rdata);  rdata += NS_INT16SZ;
		weight   = ns_get16(rdata);  rdata += NS_INT16SZ;
		port     = ns_get16(rdata);  rdata += NS_INT16SZ;
		len = SPRINTF((t, "%u %u %u ", priority, weight, port));
		T(addstr(t, len, &buf, &buflen));

		/* Server. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));
		break;
	    }

	case ns_t_minfo:
	case ns_t_rp:
		/* Name1. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));
		T(addstr(" ", 1, &buf, &buflen));

		/* Name2. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));

		break;

	case ns_t_wks: {
		int n, lcnt;

		if (rdlen < 1U + NS_INT32SZ)
			goto formerr;

		/* Address. */
		(void) inet_ntop(AF_INET, rdata, buf, buflen);
		addlen(strlen(buf), &buf, &buflen);
		rdata += NS_INADDRSZ;

		/* Protocol. */
		len = SPRINTF((tmp, " %u ( ", *rdata));
		T(addstr(tmp, len, &buf, &buflen));
		rdata += NS_INT8SZ;

		/* Bit map. */
		n = 0;
		lcnt = 0;
		while (rdata < edata) {
			u_int c = *rdata++;
			do {
				if (c & 0200) {
					if (lcnt == 0) {
						T(addstr("\n\t\t\t\t", 5,
							 &buf, &buflen));
						lcnt = 10;
						spaced = 0;
					}
					len = SPRINTF((tmp, "%d ", n));
					T(addstr(tmp, len, &buf, &buflen));
					lcnt--;
				}
				c <<= 1;
			} while (++n & 07);
		}
		T(addstr(")", 1, &buf, &buflen));

		break;
	    }

	case ns_t_key:
	case ns_t_dnskey: {
		char base64_key[NS_MD5RSA_MAX_BASE64];
		u_int keyflags, protocol, algorithm, key_id;
		const char *leader;
		int n;

		if (rdlen < 0U + NS_INT16SZ + NS_INT8SZ + NS_INT8SZ)
			goto formerr;

		/* Key flags, Protocol, Algorithm. */
#ifndef _LIBC
		key_id = dst_s_dns_key_id(rdata, edata-rdata);
#else
		key_id = 0;
#endif
		keyflags = ns_get16(rdata);  rdata += NS_INT16SZ;
		protocol = *rdata++;
		algorithm = *rdata++;
		len = SPRINTF((tmp, "0x%04x %u %u",
			       keyflags, protocol, algorithm));
		T(addstr(tmp, len, &buf, &buflen));

		/* Public key data. */
		len = b64_ntop(rdata, edata - rdata,
			       base64_key, sizeof base64_key);
		if (len < 0)
			goto formerr;
		if (len > 15) {
			T(addstr(" (", 2, &buf, &buflen));
			leader = "\n\t\t";
			spaced = 0;
		} else
			leader = " ";
		for (n = 0; n < len; n += 48) {
			T(addstr(leader, strlen(leader), &buf, &buflen));
			T(addstr(base64_key + n, MIN(len - n, 48),
				 &buf, &buflen));
		}
		if (len > 15)
			T(addstr(" )", 2, &buf, &buflen));
		n = SPRINTF((tmp, " ; key_tag= %u", key_id));
		T(addstr(tmp, n, &buf, &buflen));

		break;
	    }

	case ns_t_sig:
	case ns_t_rrsig: {
		char base64_key[NS_MD5RSA_MAX_BASE64];
		u_int type, algorithm, labels, footprint;
		const char *leader;
		u_long t;
		int n;

		if (rdlen < 22U)
			goto formerr;

		/* Type covered, Algorithm, Label count, Original TTL. */
		type = ns_get16(rdata);  rdata += NS_INT16SZ;
		algorithm = *rdata++;
		labels = *rdata++;
		t = ns_get32(rdata);  rdata += NS_INT32SZ;
		len = SPRINTF((tmp, "%s %d %d %lu ",
			       p_type(type), algorithm, labels, t));
		T(addstr(tmp, len, &buf, &buflen));
		if (labels > (u_int)dn_count_labels(name))
			goto formerr;

		/* Signature expiry. */
		t = ns_get32(rdata);  rdata += NS_INT32SZ;
		len = SPRINTF((tmp, "%s ", p_secstodate(t)));
		T(addstr(tmp, len, &buf, &buflen));

		/* Time signed. */
		t = ns_get32(rdata);  rdata += NS_INT32SZ;
		len = SPRINTF((tmp, "%s ", p_secstodate(t)));
		T(addstr(tmp, len, &buf, &buflen));

		/* Signature Footprint. */
		footprint = ns_get16(rdata);  rdata += NS_INT16SZ;
		len = SPRINTF((tmp, "%u ", footprint));
		T(addstr(tmp, len, &buf, &buflen));

		/* Signer's name. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));

		/* Signature. */
		len = b64_ntop(rdata, edata - rdata,
			       base64_key, sizeof base64_key);
		if (len > 15) {
			T(addstr(" (", 2, &buf, &buflen));
			leader = "\n\t\t";
			spaced = 0;
		} else
			leader = " ";
		if (len < 0)
			goto formerr;
		for (n = 0; n < len; n += 48) {
			T(addstr(leader, strlen(leader), &buf, &buflen));
			T(addstr(base64_key + n, MIN(len - n, 48),
				 &buf, &buflen));
		}
		if (len > 15)
			T(addstr(" )", 2, &buf, &buflen));
		break;
	    }

	case ns_t_nxt: {
		int n, c;

		/* Next domain name. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));

		/* Type bit map. */
		n = edata - rdata;
		for (c = 0; c < n*8; c++)
			if (NS_NXT_BIT_ISSET(c, rdata)) {
				len = SPRINTF((tmp, " %s", p_type(c)));
				T(addstr(tmp, len, &buf, &buflen));
			}
		break;
	    }

	case ns_t_cert: {
		u_int c_type, key_tag, alg;
		int n;
		unsigned int siz;
		char base64_cert[8192], tmp[40];
		const char *leader;

		c_type  = ns_get16(rdata); rdata += NS_INT16SZ;
		key_tag = ns_get16(rdata); rdata += NS_INT16SZ;
		alg = (u_int) *rdata++;

		len = SPRINTF((tmp, "%d %d %d ", c_type, key_tag, alg));
		T(addstr(tmp, len, &buf, &buflen));
		siz = (edata-rdata)*4/3 + 4; /* "+4" accounts for trailing \0 */
		if (siz > sizeof(base64_cert) * 3/4) {
			const char *str = "record too long to print";
			T(addstr(str, strlen(str), &buf, &buflen));
		}
		else {
			len = b64_ntop(rdata, edata-rdata, base64_cert, siz);

			if (len < 0)
				goto formerr;
			else if (len > 15) {
				T(addstr(" (", 2, &buf, &buflen));
				leader = "\n\t\t";
				spaced = 0;
			}
			else
				leader = " ";
	
			for (n = 0; n < len; n += 48) {
				T(addstr(leader, strlen(leader),
					 &buf, &buflen));
				T(addstr(base64_cert + n, MIN(len - n, 48),
					 &buf, &buflen));
			}
			if (len > 15)
				T(addstr(" )", 2, &buf, &buflen));
		}
		break;
	    }

	case ns_t_tkey: {
		/* KJD - need to complete this */
		u_long t;
		int mode, err, keysize;

		/* Algorithm name. */
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));
		T(addstr(" ", 1, &buf, &buflen));

		/* Inception. */
		t = ns_get32(rdata);  rdata += NS_INT32SZ;
		len = SPRINTF((tmp, "%s ", p_secstodate(t)));
		T(addstr(tmp, len, &buf, &buflen));

		/* Experation. */
		t = ns_get32(rdata);  rdata += NS_INT32SZ;
		len = SPRINTF((tmp, "%s ", p_secstodate(t)));
		T(addstr(tmp, len, &buf, &buflen));

		/* Mode , Error, Key Size. */
		/* Priority, Weight, Port. */
		mode = ns_get16(rdata);  rdata += NS_INT16SZ;
		err  = ns_get16(rdata);  rdata += NS_INT16SZ;
		keysize  = ns_get16(rdata);  rdata += NS_INT16SZ;
		len = SPRINTF((tmp, "%u %u %u ", mode, err, keysize));
		T(addstr(tmp, len, &buf, &buflen));

		/* XXX need to dump key, print otherdata length & other data */
		break;
	    }

	case ns_t_tsig: {
		/* BEW - need to complete this */
		int n;

		T(len = addname(msg, msglen, &rdata, origin, &buf, &buflen));
		T(addstr(" ", 1, &buf, &buflen));
		rdata += 8; /*%< time */
		n = ns_get16(rdata); rdata += INT16SZ;
		rdata += n; /*%< sig */
		n = ns_get16(rdata); rdata += INT16SZ; /*%< original id */
		sprintf(buf, "%d", ns_get16(rdata));
		rdata += INT16SZ;
		addlen(strlen(buf), &buf, &buflen);
		break;
	    }

	case ns_t_a6: {
		struct in6_addr a;
		int pbyte, pbit;

		/* prefix length */
		if (rdlen == 0U) goto formerr;
		len = SPRINTF((tmp, "%d ", *rdata));
		T(addstr(tmp, len, &buf, &buflen));
		pbit = *rdata;
		if (pbit > 128) goto formerr;
		pbyte = (pbit & ~7) / 8;
		rdata++;

		/* address suffix: provided only when prefix len != 128 */
		if (pbit < 128) {
			if (rdata + pbyte >= edata) goto formerr;
			memset(&a, 0, sizeof(a));
			memcpy(&a.s6_addr[pbyte], rdata, sizeof(a) - pbyte);
			(void) inet_ntop(AF_INET6, &a, buf, buflen);
			addlen(strlen(buf), &buf, &buflen);
			rdata += sizeof(a) - pbyte;
		}

		/* prefix name: provided only when prefix len > 0 */
		if (pbit == 0)
			break;
		if (rdata >= edata) goto formerr;
		T(addstr(" ", 1, &buf, &buflen));
		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));
		
		break;
	    }

	case ns_t_opt: {
		len = SPRINTF((tmp, "%u bytes", class));
		T(addstr(tmp, len, &buf, &buflen));
		break;
	    }

	case ns_t_ds:
	case ns_t_dlv:
	case ns_t_sshfp: {
		u_int t;

		if (type == ns_t_ds || type == ns_t_dlv) {
			if (rdlen < 4U) goto formerr;
			t = ns_get16(rdata);
			rdata += NS_INT16SZ;
			len = SPRINTF((tmp, "%u ", t));
			T(addstr(tmp, len, &buf, &buflen));
		} else
			if (rdlen < 2U) goto formerr;

		len = SPRINTF((tmp, "%u ", *rdata));
		T(addstr(tmp, len, &buf, &buflen));
		rdata++;

		len = SPRINTF((tmp, "%u ", *rdata));
		T(addstr(tmp, len, &buf, &buflen));
		rdata++;

		while (rdata < edata) {
			len = SPRINTF((tmp, "%02X", *rdata));
			T(addstr(tmp, len, &buf, &buflen));
			rdata++;
		}
		break;
	    }

	case ns_t_nsec3:
	case ns_t_nsec3param: {
		u_int t, w, l, j, k, c;
		
		len = SPRINTF((tmp, "%u ", *rdata));
		T(addstr(tmp, len, &buf, &buflen));
		rdata++;

		len = SPRINTF((tmp, "%u ", *rdata));
		T(addstr(tmp, len, &buf, &buflen));
		rdata++;

		t = ns_get16(rdata);
		rdata += NS_INT16SZ;
		len = SPRINTF((tmp, "%u ", t));
		T(addstr(tmp, len, &buf, &buflen));

		t = *rdata++;
		if (t == 0) {
			T(addstr("-", 1, &buf, &buflen));
		} else {
			while (t-- > 0) {
				len = SPRINTF((tmp, "%02X", *rdata));
				T(addstr(tmp, len, &buf, &buflen));
				rdata++;
			}
		}
		if (type == ns_t_nsec3param)
			break;
		T(addstr(" ", 1, &buf, &buflen));

		t = *rdata++;
		while (t > 0) {
			switch (t) {
			case 1:
				tmp[0] = base32hex[((rdata[0]>>3)&0x1f)];
				tmp[1] = base32hex[((rdata[0]<<2)&0x1c)];
				tmp[2] = tmp[3] = tmp[4] = '=';
				tmp[5] = tmp[6] = tmp[7] = '=';
				break;
			case 2:
				tmp[0] = base32hex[((rdata[0]>>3)&0x1f)];
				tmp[1] = base32hex[((rdata[0]<<2)&0x1c)|
						   ((rdata[1]>>6)&0x03)];
				tmp[2] = base32hex[((rdata[1]>>1)&0x1f)];
				tmp[3] = base32hex[((rdata[1]<<4)&0x10)];
				tmp[4] = tmp[5] = tmp[6] = tmp[7] = '=';
				break;
			case 3:
				tmp[0] = base32hex[((rdata[0]>>3)&0x1f)];
				tmp[1] = base32hex[((rdata[0]<<2)&0x1c)|
						   ((rdata[1]>>6)&0x03)];
				tmp[2] = base32hex[((rdata[1]>>1)&0x1f)];
				tmp[3] = base32hex[((rdata[1]<<4)&0x10)|
						   ((rdata[2]>>4)&0x0f)];
				tmp[4] = base32hex[((rdata[2]<<1)&0x1e)];
				tmp[5] = tmp[6] = tmp[7] = '=';
				break;
			case 4:
				tmp[0] = base32hex[((rdata[0]>>3)&0x1f)];
				tmp[1] = base32hex[((rdata[0]<<2)&0x1c)|
						   ((rdata[1]>>6)&0x03)];
				tmp[2] = base32hex[((rdata[1]>>1)&0x1f)];
				tmp[3] = base32hex[((rdata[1]<<4)&0x10)|
						   ((rdata[2]>>4)&0x0f)];
				tmp[4] = base32hex[((rdata[2]<<1)&0x1e)|
						   ((rdata[3]>>7)&0x01)];
				tmp[5] = base32hex[((rdata[3]>>2)&0x1f)];
				tmp[6] = base32hex[(rdata[3]<<3)&0x18];
				tmp[7] = '=';
				break;
			default:
				tmp[0] = base32hex[((rdata[0]>>3)&0x1f)];
				tmp[1] = base32hex[((rdata[0]<<2)&0x1c)|
						   ((rdata[1]>>6)&0x03)];
				tmp[2] = base32hex[((rdata[1]>>1)&0x1f)];
				tmp[3] = base32hex[((rdata[1]<<4)&0x10)|
						   ((rdata[2]>>4)&0x0f)];
				tmp[4] = base32hex[((rdata[2]<<1)&0x1e)|
						   ((rdata[3]>>7)&0x01)];
				tmp[5] = base32hex[((rdata[3]>>2)&0x1f)];
				tmp[6] = base32hex[((rdata[3]<<3)&0x18)|
						   ((rdata[4]>>5)&0x07)];
				tmp[7] = base32hex[(rdata[4]&0x1f)];
				break;
			}
			T(addstr(tmp, 8, &buf, &buflen));
			if (t >= 5) {
				rdata += 5;
				t -= 5;
			} else {
				rdata += t;
				t -= t;
			}
		}

		while (rdata < edata) {
			w = *rdata++;
			l = *rdata++;
			for (j = 0; j < l; j++) {
				if (rdata[j] == 0)
					continue;
				for (k = 0; k < 8; k++) {
					if ((rdata[j] & (0x80 >> k)) == 0)
						continue;
					c = w * 256 + j * 8 + k;
					len = SPRINTF((tmp, " %s", p_type(c)));
					T(addstr(tmp, len, &buf, &buflen));
				}
			}
			rdata += l;
		}
		break;
	    }

	case ns_t_nsec: {
		u_int w, l, j, k, c;

		T(addname(msg, msglen, &rdata, origin, &buf, &buflen));

		while (rdata < edata) {
			w = *rdata++;
			l = *rdata++;
			for (j = 0; j < l; j++) {
				if (rdata[j] == 0)
					continue;
				for (k = 0; k < 8; k++) {
					if ((rdata[j] & (0x80 >> k)) == 0)
						continue;
					c = w * 256 + j * 8 + k;
					len = SPRINTF((tmp, " %s", p_type(c)));
					T(addstr(tmp, len, &buf, &buflen));
				}
			}
			rdata += l;
		}
		break;
	    }

	case ns_t_dhcid: {
		int n;
		unsigned int siz;
		char base64_dhcid[8192];
		const char *leader;

		siz = (edata-rdata)*4/3 + 4; /* "+4" accounts for trailing \0 */
		if (siz > sizeof(base64_dhcid) * 3/4) {
			const char *str = "record too long to print";
			T(addstr(str, strlen(str), &buf, &buflen));
		} else {
			len = b64_ntop(rdata, edata-rdata, base64_dhcid, siz);
		
			if (len < 0)
				goto formerr;

			else if (len > 15) {
				T(addstr(" (", 2, &buf, &buflen));
				leader = "\n\t\t";
				spaced = 0;
			}
			else
				leader = " ";

			for (n = 0; n < len; n += 48) {
				T(addstr(leader, strlen(leader),
					 &buf, &buflen));
				T(addstr(base64_dhcid + n, MIN(len - n, 48),
					 &buf, &buflen));
			}
			if (len > 15)
				T(addstr(" )", 2, &buf, &buflen));
		}
		break;
	}

	case ns_t_ipseckey: {
		int n;
		unsigned int siz;
		char base64_key[8192];
		const char *leader;
	
		if (rdlen < 2)
			goto formerr;

		switch (rdata[1]) {
		case 0:
		case 3:
			if (rdlen < 3)
				goto formerr;
			break;
		case 1:
			if (rdlen < 7)
				goto formerr;
			break;
		case 2:
			if (rdlen < 19)
				goto formerr;
			break;
		default:
			comment = "unknown IPSECKEY gateway type";
			goto hexify;
		}

		len = SPRINTF((tmp, "%u ", *rdata));
		T(addstr(tmp, len, &buf, &buflen));
		rdata++;

		len = SPRINTF((tmp, "%u ", *rdata));
		T(addstr(tmp, len, &buf, &buflen));
		rdata++;
		
		len = SPRINTF((tmp, "%u ", *rdata));
		T(addstr(tmp, len, &buf, &buflen));
		rdata++;

		switch (rdata[-2]) {
		case 0:
			T(addstr(".", 1, &buf, &buflen));
			break;
		case 1:
			(void) inet_ntop(AF_INET, rdata, buf, buflen);
			addlen(strlen(buf), &buf, &buflen);
			rdata += 4;
			break;
		case 2:
			(void) inet_ntop(AF_INET6, rdata, buf, buflen);
			addlen(strlen(buf), &buf, &buflen);
			rdata += 16;
			break;
		case 3:
			T(addname(msg, msglen, &rdata, origin, &buf, &buflen));
			break;
		}

		if (rdata >= edata)
			break;

		siz = (edata-rdata)*4/3 + 4; /* "+4" accounts for trailing \0 */
		if (siz > sizeof(base64_key) * 3/4) {
			const char *str = "record too long to print";
			T(addstr(str, strlen(str), &buf, &buflen));
		} else {
			len = b64_ntop(rdata, edata-rdata, base64_key, siz);

			if (len < 0)
				goto formerr;

			else if (len > 15) {
				T(addstr(" (", 2, &buf, &buflen));
				leader = "\n\t\t";
				spaced = 0;
			}
			else
				leader = " ";

			for (n = 0; n < len; n += 48) {
				T(addstr(leader, strlen(leader),
					 &buf, &buflen));
				T(addstr(base64_key + n, MIN(len - n, 48),
					 &buf, &buflen));
			}
			if (len > 15)
				T(addstr(" )", 2, &buf, &buflen));
		}
	}

	case ns_t_hip: {
		unsigned int i, hip_len, algorithm, key_len;
		char base64_key[NS_MD5RSA_MAX_BASE64];
		unsigned int siz;
		const char *leader = "\n\t\t\t\t\t";
		
		hip_len = *rdata++;
		algorithm = *rdata++;
		key_len = ns_get16(rdata);
		rdata += NS_INT16SZ;

		siz = key_len*4/3 + 4; /* "+4" accounts for trailing \0 */
		if (siz > sizeof(base64_key) * 3/4) {
			const char *str = "record too long to print";
			T(addstr(str, strlen(str), &buf, &buflen));
		} else {
			len = sprintf(tmp, "( %u ", algorithm);
			T(addstr(tmp, len, &buf, &buflen));

			for (i = 0; i < hip_len; i++) {
				len = sprintf(tmp, "%02X", *rdata);
				T(addstr(tmp, len, &buf, &buflen));
				rdata++;
			}
			T(addstr(leader, strlen(leader), &buf, &buflen));

			len = b64_ntop(rdata, key_len, base64_key, siz);
			if (len < 0)
				goto formerr;

			T(addstr(base64_key, len, &buf, &buflen));
				
			rdata += key_len;
			while (rdata < edata) {
				T(addstr(leader, strlen(leader), &buf, &buflen));
				T(addname(msg, msglen, &rdata, origin,
					  &buf, &buflen));
			}
			T(addstr(" )", 2, &buf, &buflen));
		}
		break;
	}

	default:
		comment = "unknown RR type";
		goto hexify;
	}
	return (buf - obuf);
 formerr:
	comment = "RR format error";
 hexify: {
	int n, m;
	char *p;

	len = SPRINTF((tmp, "\\# %u%s\t; %s", (unsigned)(edata - rdata),
		       rdlen != 0U ? " (" : "", comment));
	T(addstr(tmp, len, &buf, &buflen));
	while (rdata < edata) {
		p = tmp;
		p += SPRINTF((p, "\n\t"));
		spaced = 0;
		n = MIN(16, edata - rdata);
		for (m = 0; m < n; m++)
			p += SPRINTF((p, "%02x ", rdata[m]));
		T(addstr(tmp, p - tmp, &buf, &buflen));
		if (n < 16) {
			T(addstr(")", 1, &buf, &buflen));
			T(addtab(p - tmp + 1, 48, spaced, &buf, &buflen));
		}
		p = tmp;
		p += SPRINTF((p, "; "));
		for (m = 0; m < n; m++)
			*p++ = (isascii(rdata[m]) && isprint(rdata[m]))
				? rdata[m]
				: '.';
		T(addstr(tmp, p - tmp, &buf, &buflen));
		rdata += n;
	}
	return (buf - obuf);
    }
}

/* Private. */

/*%
 * size_t
 * prune_origin(name, origin)
 *	Find out if the name is at or under the current origin.
 * return:
 *	Number of characters in name before start of origin,
 *	or length of name if origin does not match.
 * notes:
 *	This function should share code with samedomain().
 */
static size_t
prune_origin(const char *name, const char *origin) {
	const char *oname = name;

	while (*name != '\0') {
		if (origin != NULL && ns_samename(name, origin) == 1)
			return (name - oname - (name > oname));
		while (*name != '\0') {
			if (*name == '\\') {
				name++;
				/* XXX need to handle \nnn form. */
				if (*name == '\0')
					break;
			} else if (*name == '.') {
				name++;
				break;
			}
			name++;
		}
	}
	return (name - oname);
}

/*%
 * int
 * charstr(rdata, edata, buf, buflen)
 *	Format a <character-string> into the presentation buffer.
 * return:
 *	Number of rdata octets consumed
 *	0 for protocol format error
 *	-1 for output buffer error
 * side effects:
 *	buffer is advanced on success.
 */
static int
charstr(const u_char *rdata, const u_char *edata, char **buf, size_t *buflen) {
	const u_char *odata = rdata;
	size_t save_buflen = *buflen;
	char *save_buf = *buf;

	if (addstr("\"", 1, buf, buflen) < 0)
		goto enospc;
	if (rdata < edata) {
		int n = *rdata;

		if (rdata + 1 + n <= edata) {
			rdata++;
			while (n-- > 0) {
				if (strchr("\n\"\\", *rdata) != NULL)
					if (addstr("\\", 1, buf, buflen) < 0)
						goto enospc;
				if (addstr((const char *)rdata, 1,
					   buf, buflen) < 0)
					goto enospc;
				rdata++;
			}
		}
	}
	if (addstr("\"", 1, buf, buflen) < 0)
		goto enospc;
	return (rdata - odata);
 enospc:
	errno = ENOSPC;
	*buf = save_buf;
	*buflen = save_buflen;
	return (-1);
}

static int
addname(const u_char *msg, size_t msglen,
	const u_char **pp, const char *origin,
	char **buf, size_t *buflen)
{
	size_t newlen, save_buflen = *buflen;
	char *save_buf = *buf;
	int n;

	n = dn_expand(msg, msg + msglen, *pp, *buf, *buflen);
	if (n < 0)
		goto enospc;	/*%< Guess. */
	newlen = prune_origin(*buf, origin);
	if (**buf == '\0') {
		goto root;
	} else if (newlen == 0U) {
		/* Use "@" instead of name. */
		if (newlen + 2 > *buflen)
			goto enospc;        /* No room for "@\0". */
		(*buf)[newlen++] = '@';
		(*buf)[newlen] = '\0';
	} else {
		if (((origin == NULL || origin[0] == '\0') ||
		    (origin[0] != '.' && origin[1] != '\0' &&
		    (*buf)[newlen] == '\0')) && (*buf)[newlen - 1] != '.') {
			/* No trailing dot. */
 root:
			if (newlen + 2 > *buflen)
				goto enospc;	/* No room for ".\0". */
			(*buf)[newlen++] = '.';
			(*buf)[newlen] = '\0';
		}
	}
	*pp += n;
	addlen(newlen, buf, buflen);
	**buf = '\0';
	return (newlen);
 enospc:
	errno = ENOSPC;
	*buf = save_buf;
	*buflen = save_buflen;
	return (-1);
}

static void
addlen(size_t len, char **buf, size_t *buflen) {
	INSIST(len <= *buflen);
	*buf += len;
	*buflen -= len;
}

static int
addstr(const char *src, size_t len, char **buf, size_t *buflen) {
	if (len >= *buflen) {
		errno = ENOSPC;
		return (-1);
	}
	memcpy(*buf, src, len);
	addlen(len, buf, buflen);
	**buf = '\0';
	return (0);
}

static int
addtab(size_t len, size_t target, int spaced, char **buf, size_t *buflen) {
	size_t save_buflen = *buflen;
	char *save_buf = *buf;
	int t;

	if (spaced || len >= target - 1) {
		T(addstr("  ", 2, buf, buflen));
		spaced = 1;
	} else {
		for (t = (target - len - 1) / 8; t >= 0; t--)
			if (addstr("\t", 1, buf, buflen) < 0) {
				*buflen = save_buflen;
				*buf = save_buf;
				return (-1);
			}
		spaced = 0;
	}
	return (spaced);
}

/*! \file */
