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

/*! \file
 * \brief
 * Based on the Dynamic DNS reference implementation by Viraj Bais
 * &lt;viraj_bais@ccm.fm.intel.com>
 */

#if !defined(lint) && !defined(SABER)
static const char rcsid[] = "$Id: res_mkupdate.c,v 1.10 2008/12/11 09:59:00 marka Exp $";
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "port_before.h"

#include <sys/param.h>

#include <netinet/in.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>

#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <resolv.h>
#include <res_update.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#ifdef _LIBC
#include <isc/list.h>
#endif

#include "port_after.h"

/* Options.  Leave them on. */
#ifndef	DEBUG
#define	DEBUG
#endif
#define MAXPORT 1024

static int getnum_str(u_char **, u_char *);
static int gethexnum_str(u_char **, u_char *);
static int getword_str(char *, int, u_char **, u_char *);
static int getstr_str(char *, int, u_char **, u_char *);

#define ShrinkBuffer(x)  if ((buflen -= x) < 0) return (-2);

/* Forward. */

#ifdef _LIBC
static
#endif
int res_protocolnumber(const char *);
#ifdef _LIBC
static
#endif
int res_servicenumber(const char *);

/*%
 * Form update packets.
 * Returns the size of the resulting packet if no error
 *
 * On error,
 *	returns 
 *\li              -1 if error in reading a word/number in rdata
 *		   portion for update packets
 *\li		-2 if length of buffer passed is insufficient
 *\li		-3 if zone section is not the first section in
 *		   the linked list, or section order has a problem
 *\li		-4 on a number overflow
 *\li		-5 unknown operation or no records
 */
int
res_nmkupdate(res_state statp, ns_updrec *rrecp_in, u_char *buf, int buflen) {
	ns_updrec *rrecp_start = rrecp_in;
	HEADER *hp;
	u_char *cp, *sp2, *startp, *endp;
	int n, i, soanum, multiline;
	ns_updrec *rrecp;
	struct in_addr ina;
	struct in6_addr in6a;
        char buf2[MAXDNAME];
	u_char buf3[MAXDNAME];
	int section, numrrs = 0, counts[ns_s_max];
	u_int16_t rtype, rclass;
	u_int32_t n1, rttl;
	u_char *dnptrs[20], **dpp, **lastdnptr;
#ifndef _LIBC
	int siglen;
#endif
	int keylen, certlen;

	/*
	 * Initialize header fields.
	 */
	if ((buf == NULL) || (buflen < HFIXEDSZ))
		return (-1);
	memset(buf, 0, HFIXEDSZ);
	hp = (HEADER *) buf;
	statp->id = res_nrandomid(statp);
	hp->id = htons(statp->id);
	hp->opcode = ns_o_update;
	hp->rcode = NOERROR;
	cp = buf + HFIXEDSZ;
	buflen -= HFIXEDSZ;
	dpp = dnptrs;
	*dpp++ = buf;
	*dpp++ = NULL;
	lastdnptr = dnptrs + nitems(dnptrs);

	if (rrecp_start == NULL)
		return (-5);
	else if (rrecp_start->r_section != S_ZONE)
		return (-3);

	memset(counts, 0, sizeof counts);
	for (rrecp = rrecp_start; rrecp; rrecp = NEXT(rrecp, r_glink)) {
		numrrs++;
                section = rrecp->r_section;
		if (section < 0 || section >= ns_s_max)
			return (-1);
		counts[section]++;
		for (i = section + 1; i < ns_s_max; i++)
			if (counts[i])
				return (-3);
		rtype = rrecp->r_type;
		rclass = rrecp->r_class;
		rttl = rrecp->r_ttl;
		/* overload class and type */
		if (section == S_PREREQ) {
			rttl = 0;
			switch (rrecp->r_opcode) {
			case YXDOMAIN:
				rclass = C_ANY;
				rtype = T_ANY;
				rrecp->r_size = 0;
				break;
			case NXDOMAIN:
				rclass = C_NONE;
				rtype = T_ANY;
				rrecp->r_size = 0;
				break;
			case NXRRSET:
				rclass = C_NONE;
				rrecp->r_size = 0;
				break;
			case YXRRSET:
				if (rrecp->r_size == 0)
					rclass = C_ANY;
				break;
			default:
				fprintf(stderr,
					"res_mkupdate: incorrect opcode: %d\n",
					rrecp->r_opcode);
				fflush(stderr);
				return (-1);
			}
		} else if (section == S_UPDATE) {
			switch (rrecp->r_opcode) {
			case DELETE:
				rclass = rrecp->r_size == 0 ? C_ANY : C_NONE;
				break;
			case ADD:
				break;
			default:
				fprintf(stderr,
					"res_mkupdate: incorrect opcode: %d\n",
					rrecp->r_opcode);
				fflush(stderr);
				return (-1);
			}
		}

		/*
		 * XXX	appending default domain to owner name is omitted,
		 *	fqdn must be provided
		 */
		if ((n = dn_comp(rrecp->r_dname, cp, buflen, dnptrs,
				 lastdnptr)) < 0)
			return (-1);
		cp += n;
		ShrinkBuffer(n + 2*INT16SZ);
		PUTSHORT(rtype, cp);
		PUTSHORT(rclass, cp);
		if (section == S_ZONE) {
			if (numrrs != 1 || rrecp->r_type != T_SOA)
				return (-3);
			continue;
		}
		ShrinkBuffer(INT32SZ + INT16SZ);
		PUTLONG(rttl, cp);
		sp2 = cp;  /*%< save pointer to length byte */
		cp += INT16SZ;
		if (rrecp->r_size == 0) {
			if (section == S_UPDATE && rclass != C_ANY)
				return (-1);
			else {
				PUTSHORT(0, sp2);
				continue;
			}
		}
		startp = rrecp->r_data;
		endp = startp + rrecp->r_size - 1;
		/* XXX this should be done centrally. */
		switch (rrecp->r_type) {
		case T_A:
			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			if (!inet_aton(buf2, &ina))
				return (-1);
			n1 = ntohl(ina.s_addr);
			ShrinkBuffer(INT32SZ);
			PUTLONG(n1, cp);
			break;
		case T_CNAME:
		case T_MB:
		case T_MG:
		case T_MR:
		case T_NS:
		case T_PTR:
		case ns_t_dname:
			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			n = dn_comp(buf2, cp, buflen, dnptrs, lastdnptr);
			if (n < 0)
				return (-1);
			cp += n;
			ShrinkBuffer(n);
			break;
		case T_MINFO:
		case T_SOA:
		case T_RP:
			for (i = 0; i < 2; i++) {
				if (!getword_str(buf2, sizeof buf2, &startp,
						 endp))
				return (-1);
				n = dn_comp(buf2, cp, buflen,
					    dnptrs, lastdnptr);
				if (n < 0)
					return (-1);
				cp += n;
				ShrinkBuffer(n);
			}
			if (rrecp->r_type == T_SOA) {
				ShrinkBuffer(5 * INT32SZ);
				while (isspace(*startp) || !*startp)
					startp++;
				if (*startp == '(') {
					multiline = 1;
					startp++;
				} else
					multiline = 0;
				/* serial, refresh, retry, expire, minimum */
				for (i = 0; i < 5; i++) {
					soanum = getnum_str(&startp, endp);
					if (soanum < 0)
						return (-1);
					PUTLONG(soanum, cp);
				}
				if (multiline) {
					while (isspace(*startp) || !*startp)
						startp++;
					if (*startp != ')')
						return (-1);
				}
			}
			break;
		case T_MX:
		case T_AFSDB:
		case T_RT:
			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(INT16SZ);
			PUTSHORT(n, cp);
			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			n = dn_comp(buf2, cp, buflen, dnptrs, lastdnptr);
			if (n < 0)
				return (-1);
			cp += n;
			ShrinkBuffer(n);
			break;
		case T_SRV:
			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(INT16SZ);
			PUTSHORT(n, cp);

			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(INT16SZ);
			PUTSHORT(n, cp);

			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(INT16SZ);
			PUTSHORT(n, cp);

			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			n = dn_comp(buf2, cp, buflen, NULL, NULL);
			if (n < 0)
				return (-1);
			cp += n;
			ShrinkBuffer(n);
			break;
		case T_PX:
			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			PUTSHORT(n, cp);
			ShrinkBuffer(INT16SZ);
			for (i = 0; i < 2; i++) {
				if (!getword_str(buf2, sizeof buf2, &startp,
						 endp))
					return (-1);
				n = dn_comp(buf2, cp, buflen, dnptrs,
					    lastdnptr);
				if (n < 0)
					return (-1);
				cp += n;
				ShrinkBuffer(n);
			}
			break;
		case T_WKS: {
			char bm[MAXPORT/8];
			unsigned int maxbm = 0;

			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			if (!inet_aton(buf2, &ina))
				return (-1);
			n1 = ntohl(ina.s_addr);
			ShrinkBuffer(INT32SZ);
			PUTLONG(n1, cp);

			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			if ((i = res_protocolnumber(buf2)) < 0)
				return (-1);
			ShrinkBuffer(1);
			*cp++ = i & 0xff;
			 
			for (i = 0; i < MAXPORT/8 ; i++)
				bm[i] = 0;

			while (getword_str(buf2, sizeof buf2, &startp, endp)) {
				if ((n = res_servicenumber(buf2)) <= 0)
					return (-1);

				if (n < MAXPORT) {
					bm[n/8] |= (0x80>>(n%8));
					if ((unsigned)n > maxbm)
						maxbm = n;
				} else
					return (-1);
			}
			maxbm = maxbm/8 + 1;
			ShrinkBuffer(maxbm);
			memcpy(cp, bm, maxbm);
			cp += maxbm;
			break;
		}
		case T_HINFO:
			for (i = 0; i < 2; i++) {
				if ((n = getstr_str(buf2, sizeof buf2,
						&startp, endp)) < 0)
					return (-1);
				if (n > 255)
					return (-1);
				ShrinkBuffer(n+1);
				*cp++ = n;
				memcpy(cp, buf2, n);
				cp += n;
			}
			break;
		case T_TXT:
			for (;;) {
				if ((n = getstr_str(buf2, sizeof buf2,
						&startp, endp)) < 0) {
					if (cp != (sp2 + INT16SZ))
						break;
					return (-1);
				}
				if (n > 255)
					return (-1);
				ShrinkBuffer(n+1);
				*cp++ = n;
				memcpy(cp, buf2, n);
				cp += n;
			}
			break;
		case T_X25:
			/* RFC1183 */
			if ((n = getstr_str(buf2, sizeof buf2, &startp,
					 endp)) < 0)
				return (-1);
			if (n > 255)
				return (-1);
			ShrinkBuffer(n+1);
			*cp++ = n;
			memcpy(cp, buf2, n);
			cp += n;
			break;
		case T_ISDN:
			/* RFC1183 */
			if ((n = getstr_str(buf2, sizeof buf2, &startp,
					 endp)) < 0)
				return (-1);
			if ((n > 255) || (n == 0))
				return (-1);
			ShrinkBuffer(n+1);
			*cp++ = n;
			memcpy(cp, buf2, n);
			cp += n;
			if ((n = getstr_str(buf2, sizeof buf2, &startp,
					 endp)) < 0)
				n = 0;
			if (n > 255)
				return (-1);
			ShrinkBuffer(n+1);
			*cp++ = n;
			memcpy(cp, buf2, n);
			cp += n;
			break;
		case T_NSAP:
			if ((n = inet_nsap_addr((char *)startp, (u_char *)buf2, sizeof(buf2))) != 0) {
				ShrinkBuffer(n);
				memcpy(cp, buf2, n);
				cp += n;
			} else {
				return (-1);
			}
			break;
		case T_LOC:
			if ((n = loc_aton((char *)startp, (u_char *)buf2)) != 0) {
				ShrinkBuffer(n);
				memcpy(cp, buf2, n);
				cp += n;
			} else
				return (-1);
			break;
		case ns_t_sig:
#ifdef _LIBC
			return (-1);
#else
		    {
			int sig_type, success, dateerror;
			u_int32_t exptime, timesigned;

			/* type */
			if ((n = getword_str(buf2, sizeof buf2,
					     &startp, endp)) < 0)
				return (-1);
			sig_type = sym_ston(__p_type_syms, buf2, &success);
			if (!success || sig_type == ns_t_any)
				return (-1);
			ShrinkBuffer(INT16SZ);
			PUTSHORT(sig_type, cp);
			/* alg */
			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(1);
			*cp++ = n;
			/* labels */
			n = getnum_str(&startp, endp);
			if (n <= 0 || n > 255)
				return (-1);
			ShrinkBuffer(1);
			*cp++ = n;
			/* ottl  & expire */
			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			exptime = ns_datetosecs(buf2, &dateerror);
			if (!dateerror) {
				ShrinkBuffer(INT32SZ);
				PUTLONG(rttl, cp);
			}
			else {
				char *ulendp;
				u_int32_t ottl;

				errno = 0;
				ottl = strtoul(buf2, &ulendp, 10);
				if (errno != 0 ||
				    (ulendp != NULL && *ulendp != '\0'))
					return (-1);
				ShrinkBuffer(INT32SZ);
				PUTLONG(ottl, cp);
				if (!getword_str(buf2, sizeof buf2, &startp,
						 endp))
					return (-1);
				exptime = ns_datetosecs(buf2, &dateerror);
				if (dateerror)
					return (-1);
			}
			/* expire */
			ShrinkBuffer(INT32SZ);
			PUTLONG(exptime, cp);
			/* timesigned */
			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			timesigned = ns_datetosecs(buf2, &dateerror);
			if (!dateerror) {
				ShrinkBuffer(INT32SZ);
				PUTLONG(timesigned, cp);
			}
			else
				return (-1);
			/* footprint */
			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(INT16SZ);
			PUTSHORT(n, cp);
			/* signer name */
			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			n = dn_comp(buf2, cp, buflen, dnptrs, lastdnptr);
			if (n < 0)
				return (-1);
			cp += n;
			ShrinkBuffer(n);
			/* sig */
			if ((n = getword_str(buf2, sizeof buf2,
					     &startp, endp)) < 0)
				return (-1);
			siglen = b64_pton(buf2, buf3, sizeof(buf3));
			if (siglen < 0)
				return (-1);
			ShrinkBuffer(siglen);
			memcpy(cp, buf3, siglen);
			cp += siglen;
			break;
		    }
#endif
		case ns_t_key:
			/* flags */
			n = gethexnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(INT16SZ);
			PUTSHORT(n, cp);
			/* proto */
			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(1);
			*cp++ = n;
			/* alg */
			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(1);
			*cp++ = n;
			/* key */
			if ((n = getword_str(buf2, sizeof buf2,
					     &startp, endp)) < 0)
				return (-1);
			keylen = b64_pton(buf2, buf3, sizeof(buf3));
			if (keylen < 0)
				return (-1);
			ShrinkBuffer(keylen);
			memcpy(cp, buf3, keylen);
			cp += keylen;
			break;
		case ns_t_nxt:
		    {
			int success, nxt_type;
			u_char data[32];
			int maxtype;

			/* next name */
			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			n = dn_comp(buf2, cp, buflen, NULL, NULL);
			if (n < 0)
				return (-1);
			cp += n;
			ShrinkBuffer(n);
			maxtype = 0;
			memset(data, 0, sizeof data);
			for (;;) {
				if (!getword_str(buf2, sizeof buf2, &startp,
						 endp))
					break;
				nxt_type = sym_ston(__p_type_syms, buf2,
						    &success);
				if (!success || !ns_t_rr_p(nxt_type))
					return (-1);
				NS_NXT_BIT_SET(nxt_type, data);
				if (nxt_type > maxtype)
					maxtype = nxt_type;
			}
			n = maxtype/NS_NXT_BITS+1;
			ShrinkBuffer(n);
			memcpy(cp, data, n);
			cp += n;
			break;
		    }
		case ns_t_cert:
			/* type */
			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(INT16SZ);
			PUTSHORT(n, cp);
			/* key tag */
			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(INT16SZ);
			PUTSHORT(n, cp);
			/* alg */
			n = getnum_str(&startp, endp);
			if (n < 0)
				return (-1);
			ShrinkBuffer(1);
			*cp++ = n;
			/* cert */
			if ((n = getword_str(buf2, sizeof buf2,
					     &startp, endp)) < 0)
				return (-1);
			certlen = b64_pton(buf2, buf3, sizeof(buf3));
			if (certlen < 0)
				return (-1);
			ShrinkBuffer(certlen);
			memcpy(cp, buf3, certlen);
			cp += certlen;
			break;
		case ns_t_aaaa:
			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			if (inet_pton(AF_INET6, buf2, &in6a) <= 0)
				return (-1);
			ShrinkBuffer(NS_IN6ADDRSZ);
			memcpy(cp, &in6a, NS_IN6ADDRSZ);
			cp += NS_IN6ADDRSZ;
			break;
		case ns_t_naptr:
			/* Order Preference Flags Service Replacement Regexp */
			/* Order */
			n = getnum_str(&startp, endp);
			if (n < 0 || n > 65535)
				return (-1);
			ShrinkBuffer(INT16SZ);
			PUTSHORT(n, cp);
			/* Preference */
			n = getnum_str(&startp, endp);
			if (n < 0 || n > 65535)
				return (-1);
			ShrinkBuffer(INT16SZ);
			PUTSHORT(n, cp);
			/* Flags */
			if ((n = getstr_str(buf2, sizeof buf2,
					&startp, endp)) < 0) {
				return (-1);
			}
			if (n > 255)
				return (-1);
			ShrinkBuffer(n+1);
			*cp++ = n;
			memcpy(cp, buf2, n);
			cp += n;
			/* Service Classes */
			if ((n = getstr_str(buf2, sizeof buf2,
					&startp, endp)) < 0) {
				return (-1);
			}
			if (n > 255)
				return (-1);
			ShrinkBuffer(n+1);
			*cp++ = n;
			memcpy(cp, buf2, n);
			cp += n;
			/* Pattern */
			if ((n = getstr_str(buf2, sizeof buf2,
					&startp, endp)) < 0) {
				return (-1);
			}
			if (n > 255)
				return (-1);
			ShrinkBuffer(n+1);
			*cp++ = n;
			memcpy(cp, buf2, n);
			cp += n;
			/* Replacement */
			if (!getword_str(buf2, sizeof buf2, &startp, endp))
				return (-1);
			n = dn_comp(buf2, cp, buflen, NULL, NULL);
			if (n < 0)
				return (-1);
			cp += n;
			ShrinkBuffer(n);
			break;
		default:
			return (-1);
		} /*switch*/
		n = (u_int16_t)((cp - sp2) - INT16SZ);
		PUTSHORT(n, sp2);
	} /*for*/
		
	hp->qdcount = htons(counts[0]);
	hp->ancount = htons(counts[1]);
	hp->nscount = htons(counts[2]);
	hp->arcount = htons(counts[3]);
	return (cp - buf);
}

/*%
 * Get a whitespace delimited word from a string (not file)
 * into buf. modify the start pointer to point after the
 * word in the string.
 */
static int
getword_str(char *buf, int size, u_char **startpp, u_char *endp) {
        char *cp;
        int c;
 
        for (cp = buf; *startpp <= endp; ) {
                c = **startpp;
                if (isspace(c) || c == '\0') {
                        if (cp != buf) /*%< trailing whitespace */
                                break;
                        else { /*%< leading whitespace */
                                (*startpp)++;
                                continue;
                        }
                }
                (*startpp)++;
                if (cp >= buf+size-1)
                        break;
                *cp++ = (u_char)c;
        }
        *cp = '\0';
        return (cp != buf);
}

/*%
 * get a white spae delimited string from memory.  Process quoted strings
 * and \\DDD escapes.  Return length or -1 on error.  Returned string may
 * contain nulls.
 */
static char digits[] = "0123456789";
static int
getstr_str(char *buf, int size, u_char **startpp, u_char *endp) {
        char *cp;
        int c, c1 = 0;
	int inquote = 0;
	int seen_quote = 0;
	int escape = 0;
	int dig = 0;
 
	for (cp = buf; *startpp <= endp; ) {
                if ((c = **startpp) == '\0')
			break;
		/* leading white space */
		if ((cp == buf) && !seen_quote && isspace(c)) {
			(*startpp)++;
			continue;
		}

		switch (c) {
		case '\\':
			if (!escape)  {
				escape = 1;
				dig = 0;
				c1 = 0;
				(*startpp)++;
				continue;
			} 
			goto do_escape;
		case '"':
			if (!escape) {
				inquote = !inquote;
				seen_quote = 1;
				(*startpp)++;
				continue;
			}
			/* fall through */
		default:
		do_escape:
			if (escape) {
				switch (c) {
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					c1 = c1 * 10 + 
						(strchr(digits, c) - digits);

					if (++dig == 3) {
						c = c1 &0xff;
						break;
					}
					(*startpp)++;
					continue;
				}
				escape = 0;
			} else if (!inquote && isspace(c))
				goto done;
			if (cp >= buf+size-1)
				goto done;
			*cp++ = (u_char)c;
			(*startpp)++;
		}
	}
 done:
	*cp = '\0';
	return ((cp == buf)?  (seen_quote? 0: -1): (cp - buf));
}

/*%
 * Get a whitespace delimited base 16 number from a string (not file) into buf
 * update the start pointer to point after the number in the string.
 */
static int
gethexnum_str(u_char **startpp, u_char *endp) {
        int c, n;
        int seendigit = 0;
        int m = 0;

	if (*startpp + 2 >= endp || strncasecmp((char *)*startpp, "0x", 2) != 0)
		return getnum_str(startpp, endp);
	(*startpp)+=2;
        for (n = 0; *startpp <= endp; ) {
                c = **startpp;
                if (isspace(c) || c == '\0') {
                        if (seendigit) /*%< trailing whitespace */
                                break;
                        else { /*%< leading whitespace */
                                (*startpp)++;
                                continue;
                        }
                }
                if (c == ';') {
                        while ((*startpp <= endp) &&
			       ((c = **startpp) != '\n'))
					(*startpp)++;
                        if (seendigit)
                                break;
                        continue;
                }
                if (!isxdigit(c)) {
                        if (c == ')' && seendigit) {
                                (*startpp)--;
                                break;
                        }
			return (-1);
                }        
                (*startpp)++;
		if (isdigit(c))
	                n = n * 16 + (c - '0');
		else
			n = n * 16 + (tolower(c) - 'a' + 10);
                seendigit = 1;
        }
        return (n + m);
}

/*%
 * Get a whitespace delimited base 10 number from a string (not file) into buf
 * update the start pointer to point after the number in the string.
 */
static int
getnum_str(u_char **startpp, u_char *endp) {
        int c, n;
        int seendigit = 0;
        int m = 0;

        for (n = 0; *startpp <= endp; ) {
                c = **startpp;
                if (isspace(c) || c == '\0') {
                        if (seendigit) /*%< trailing whitespace */
                                break;
                        else { /*%< leading whitespace */
                                (*startpp)++;
                                continue;
                        }
                }
                if (c == ';') {
                        while ((*startpp <= endp) &&
			       ((c = **startpp) != '\n'))
					(*startpp)++;
                        if (seendigit)
                                break;
                        continue;
                }
                if (!isdigit(c)) {
                        if (c == ')' && seendigit) {
                                (*startpp)--;
                                break;
                        }
			return (-1);
                }        
                (*startpp)++;
                n = n * 10 + (c - '0');
                seendigit = 1;
        }
        return (n + m);
}

/*%
 * Allocate a resource record buffer & save rr info.
 */
ns_updrec *
res_mkupdrec(int section, const char *dname,
	     u_int class, u_int type, u_long ttl) {
	ns_updrec *rrecp = (ns_updrec *)calloc(1, sizeof(ns_updrec));

	if (!rrecp || !(rrecp->r_dname = strdup(dname))) {
		if (rrecp)
			free((char *)rrecp);
		return (NULL);
	}
	INIT_LINK(rrecp, r_link);
	INIT_LINK(rrecp, r_glink);
 	rrecp->r_class = (ns_class)class;
	rrecp->r_type = (ns_type)type;
	rrecp->r_ttl = ttl;
	rrecp->r_section = (ns_sect)section;
	return (rrecp);
}

/*%
 * Free a resource record buffer created by res_mkupdrec.
 */
void
res_freeupdrec(ns_updrec *rrecp) {
	/* Note: freeing r_dp is the caller's responsibility. */
	if (rrecp->r_dname != NULL)
		free(rrecp->r_dname);
	free(rrecp);
}

struct valuelist {
	struct valuelist *	next;
	struct valuelist *	prev;
	char *			name;
	char *			proto;
	int			port;
};
static struct valuelist *servicelist, *protolist;

static void
res_buildservicelist(void) {
	struct servent *sp;
	struct valuelist *slp;

#ifdef MAYBE_HESIOD
	setservent(0);
#else
	setservent(1);
#endif
	while ((sp = getservent()) != NULL) {
		slp = (struct valuelist *)malloc(sizeof(struct valuelist));
		if (!slp)
			break;
		slp->name = strdup(sp->s_name);
		slp->proto = strdup(sp->s_proto);
		if ((slp->name == NULL) || (slp->proto == NULL)) {
			if (slp->name) free(slp->name);
			if (slp->proto) free(slp->proto);
			free(slp);
			break;
		}
		slp->port = ntohs((u_int16_t)sp->s_port);  /*%< host byt order */
		slp->next = servicelist;
		slp->prev = NULL;
		if (servicelist)
			servicelist->prev = slp;
		servicelist = slp;
	}
	endservent();
}

#ifndef _LIBC
void
res_destroyservicelist() {
	struct valuelist *slp, *slp_next;

	for (slp = servicelist; slp != NULL; slp = slp_next) {
		slp_next = slp->next;
		free(slp->name);
		free(slp->proto);
		free(slp);
	}
	servicelist = (struct valuelist *)0;
}
#endif

#ifdef _LIBC
static
#endif
void
res_buildprotolist(void) {
	struct protoent *pp;
	struct valuelist *slp;

#ifdef MAYBE_HESIOD
	setprotoent(0);
#else
	setprotoent(1);
#endif
	while ((pp = getprotoent()) != NULL) {
		slp = (struct valuelist *)malloc(sizeof(struct valuelist));
		if (!slp)
			break;
		slp->name = strdup(pp->p_name);
		if (slp->name == NULL) {
			free(slp);
			break;
		}
		slp->port = pp->p_proto;	/*%< host byte order */
		slp->next = protolist;
		slp->prev = NULL;
		if (protolist)
			protolist->prev = slp;
		protolist = slp;
	}
	endprotoent();
}

#ifndef _LIBC
void
res_destroyprotolist(void) {
	struct valuelist *plp, *plp_next;

	for (plp = protolist; plp != NULL; plp = plp_next) {
		plp_next = plp->next;
		free(plp->name);
		free(plp);
	}
	protolist = (struct valuelist *)0;
}
#endif

static int
findservice(const char *s, struct valuelist **list) {
	struct valuelist *lp = *list;
	int n;

	for (; lp != NULL; lp = lp->next)
		if (strcasecmp(lp->name, s) == 0) {
			if (lp != *list) {
				lp->prev->next = lp->next;
				if (lp->next)
					lp->next->prev = lp->prev;
				(*list)->prev = lp;
				lp->next = *list;
				*list = lp;
			}
			return (lp->port);	/*%< host byte order */
		}
	if (sscanf(s, "%d", &n) != 1 || n <= 0)
		n = -1;
	return (n);
}

/*%
 * Convert service name or (ascii) number to int.
 */
#ifdef _LIBC
static
#endif
int
res_servicenumber(const char *p) {
	if (servicelist == (struct valuelist *)0)
		res_buildservicelist();
	return (findservice(p, &servicelist));
}

/*%
 * Convert protocol name or (ascii) number to int.
 */
#ifdef _LIBC
static
#endif
int
res_protocolnumber(const char *p) {
	if (protolist == (struct valuelist *)0)
		res_buildprotolist();
	return (findservice(p, &protolist));
}

#ifndef _LIBC
static struct servent *
cgetservbyport(u_int16_t port, const char *proto) {	/*%< Host byte order. */
	struct valuelist **list = &servicelist;
	struct valuelist *lp = *list;
	static struct servent serv;

	port = ntohs(port);
	for (; lp != NULL; lp = lp->next) {
		if (port != (u_int16_t)lp->port)	/*%< Host byte order. */
			continue;
		if (strcasecmp(lp->proto, proto) == 0) {
			if (lp != *list) {
				lp->prev->next = lp->next;
				if (lp->next)
					lp->next->prev = lp->prev;
				(*list)->prev = lp;
				lp->next = *list;
				*list = lp;
			}
			serv.s_name = lp->name;
			serv.s_port = htons((u_int16_t)lp->port);
			serv.s_proto = lp->proto;
			return (&serv);
		}
	}
	return (0);
}

static struct protoent *
cgetprotobynumber(int proto) {				/*%< Host byte order. */
	struct valuelist **list = &protolist;
	struct valuelist *lp = *list;
	static struct protoent prot;

	for (; lp != NULL; lp = lp->next)
		if (lp->port == proto) {		/*%< Host byte order. */
			if (lp != *list) {
				lp->prev->next = lp->next;
				if (lp->next)
					lp->next->prev = lp->prev;
				(*list)->prev = lp;
				lp->next = *list;
				*list = lp;
			}
			prot.p_name = lp->name;
			prot.p_proto = lp->port;	/*%< Host byte order. */
			return (&prot);
		}
	return (0);
}

const char *
res_protocolname(int num) {
	static char number[8];
	struct protoent *pp;

	if (protolist == (struct valuelist *)0)
		res_buildprotolist();
	pp = cgetprotobynumber(num);
	if (pp == NULL)  {
		(void) sprintf(number, "%d", num);
		return (number);
	}
	return (pp->p_name);
}

const char *
res_servicename(u_int16_t port, const char *proto) {	/*%< Host byte order. */
	static char number[8];
	struct servent *ss;

	if (servicelist == (struct valuelist *)0)
		res_buildservicelist();
	ss = cgetservbyport(htons(port), proto);
	if (ss == NULL)  {
		(void) sprintf(number, "%d", port);
		return (number);
	}
	return (ss->s_name);
}
#endif
