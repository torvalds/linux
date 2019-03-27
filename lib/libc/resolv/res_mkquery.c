/*-
 * SPDX-License-Identifier: (ISC AND BSD-3-Clause)
 *
 * Portions Copyright (C) 2004, 2005, 2008  Internet Systems Consortium, Inc. ("ISC")
 * Portions Copyright (C) 1996, 1997, 1988, 1999, 2001, 2003  Internet Software Consortium.
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
 * Copyright (c) 1985, 1993
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

#if defined(LIBC_SCCS) && !defined(lint)
static const char sccsid[] = "@(#)res_mkquery.c	8.1 (Berkeley) 6/4/93";
static const char rcsid[] = "$Id: res_mkquery.c,v 1.10 2008/12/11 09:59:00 marka Exp $";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "port_before.h"
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <resolv.h>
#include <stdio.h>
#include <string.h>
#include "port_after.h"

/* Options.  Leave them on. */
#ifndef	DEBUG
#define	DEBUG
#endif

extern const char *_res_opcodes[];

/*%
 * Form all types of queries.
 * Returns the size of the result or -1.
 */
int
res_nmkquery(res_state statp,
	     int op,			/*!< opcode of query  */
	     const char *dname,		/*!< domain name  */
	     int class, int type,	/*!< class and type of query  */
	     const u_char *data,	/*!< resource record data  */
	     int datalen,		/*!< length of data  */
	     const u_char *newrr_in,	/*!< new rr for modify or append  */
	     u_char *buf,		/*!< buffer to put query  */
	     int buflen)		/*!< size of buffer  */
{
	HEADER *hp;
	u_char *cp, *ep;
	int n;
	u_char *dnptrs[20], **dpp, **lastdnptr;

	UNUSED(newrr_in);

#ifdef DEBUG
	if (statp->options & RES_DEBUG)
		printf(";; res_nmkquery(%s, %s, %s, %s)\n",
		       _res_opcodes[op], dname, p_class(class), p_type(type));
#endif
	/*
	 * Initialize header fields.
	 */
	if ((buf == NULL) || (buflen < HFIXEDSZ))
		return (-1);
	memset(buf, 0, HFIXEDSZ);
	hp = (HEADER *) buf;
	statp->id = res_nrandomid(statp);
	hp->id = htons(statp->id);
	hp->opcode = op;
	hp->rd = (statp->options & RES_RECURSE) != 0U;
	hp->rcode = NOERROR;
	cp = buf + HFIXEDSZ;
	ep = buf + buflen;
	dpp = dnptrs;
	*dpp++ = buf;
	*dpp++ = NULL;
	lastdnptr = dnptrs + nitems(dnptrs);
	/*
	 * perform opcode specific processing
	 */
	switch (op) {
	case QUERY:	/*FALLTHROUGH*/
	case NS_NOTIFY_OP:
		if (ep - cp < QFIXEDSZ)
			return (-1);
		if ((n = dn_comp(dname, cp, ep - cp - QFIXEDSZ, dnptrs,
		    lastdnptr)) < 0)
			return (-1);
		cp += n;
		ns_put16(type, cp);
		cp += INT16SZ;
		ns_put16(class, cp);
		cp += INT16SZ;
		hp->qdcount = htons(1);
		if (op == QUERY || data == NULL)
			break;
		/*
		 * Make an additional record for completion domain.
		 */
		if ((ep - cp) < RRFIXEDSZ)
			return (-1);
		n = dn_comp((const char *)data, cp, ep - cp - RRFIXEDSZ,
			    dnptrs, lastdnptr);
		if (n < 0)
			return (-1);
		cp += n;
		ns_put16(T_NULL, cp);
		cp += INT16SZ;
		ns_put16(class, cp);
		cp += INT16SZ;
		ns_put32(0, cp);
		cp += INT32SZ;
		ns_put16(0, cp);
		cp += INT16SZ;
		hp->arcount = htons(1);
		break;

	case IQUERY:
		/*
		 * Initialize answer section
		 */
		if (ep - cp < 1 + RRFIXEDSZ + datalen)
			return (-1);
		*cp++ = '\0';	/*%< no domain name */
		ns_put16(type, cp);
		cp += INT16SZ;
		ns_put16(class, cp);
		cp += INT16SZ;
		ns_put32(0, cp);
		cp += INT32SZ;
		ns_put16(datalen, cp);
		cp += INT16SZ;
		if (datalen) {
			memcpy(cp, data, datalen);
			cp += datalen;
		}
		hp->ancount = htons(1);
		break;

	default:
		return (-1);
	}
	return (cp - buf);
}

#ifdef RES_USE_EDNS0
/* attach OPT pseudo-RR, as documented in RFC2671 (EDNS0). */

int
res_nopt(res_state statp,
	 int n0,		/*%< current offset in buffer */
	 u_char *buf,		/*%< buffer to put query */
	 int buflen,		/*%< size of buffer */
	 int anslen)		/*%< UDP answer buffer size */
{
	HEADER *hp;
	u_char *cp, *ep;
	u_int16_t flags = 0;

#ifdef DEBUG
	if ((statp->options & RES_DEBUG) != 0U)
		printf(";; res_nopt()\n");
#endif

	hp = (HEADER *) buf;
	cp = buf + n0;
	ep = buf + buflen;

	if ((ep - cp) < 1 + RRFIXEDSZ)
		return (-1);

	*cp++ = 0;				/*%< "." */
	ns_put16(ns_t_opt, cp);			/*%< TYPE */
	cp += INT16SZ;
	if (anslen > 0xffff)
		anslen = 0xffff;		/* limit to 16bit value */
	ns_put16(anslen & 0xffff, cp);		/*%< CLASS = UDP payload size */
	cp += INT16SZ;
	*cp++ = NOERROR;			/*%< extended RCODE */
	*cp++ = 0;				/*%< EDNS version */

	if (statp->options & RES_USE_DNSSEC) {
#ifdef DEBUG
		if (statp->options & RES_DEBUG)
			printf(";; res_opt()... ENDS0 DNSSEC\n");
#endif
		flags |= NS_OPT_DNSSEC_OK;
	}
	ns_put16(flags, cp);
	cp += INT16SZ;

	ns_put16(0U, cp);			/*%< RDLEN */
	cp += INT16SZ;

	hp->arcount = htons(ntohs(hp->arcount) + 1);

	return (cp - buf);
}

/*
 * Construct variable data (RDATA) block for OPT psuedo-RR, append it
 * to the buffer, then update the RDLEN field (previously set to zero by
 * res_nopt()) with the new RDATA length.
 */
int
res_nopt_rdata(res_state statp,
	  int n0,	 	/*%< current offset in buffer */
	  u_char *buf,	 	/*%< buffer to put query */
	  int buflen,		/*%< size of buffer */
	  u_char *rdata,	/*%< ptr to start of opt rdata */
	  u_short code,		/*%< OPTION-CODE */
	  u_short len,		/*%< OPTION-LENGTH */
	  u_char *data)		/*%< OPTION_DATA */
{
	register u_char *cp, *ep;

#ifdef DEBUG
	if ((statp->options & RES_DEBUG) != 0U)
		printf(";; res_nopt_rdata()\n");
#endif

	cp = buf + n0;
	ep = buf + buflen;

	if ((ep - cp) < (4 + len))
		return (-1);

	if (rdata < (buf + 2) || rdata >= ep)
		return (-1);

	ns_put16(code, cp);
	cp += INT16SZ;

	ns_put16(len, cp);
	cp += INT16SZ;

	memcpy(cp, data, len);
	cp += len;

	len = cp - rdata;
	ns_put16(len, rdata - 2);	/* Update RDLEN field */

	return (cp - buf);
}
#endif

/*! \file */
