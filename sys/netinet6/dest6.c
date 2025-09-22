/*	$OpenBSD: dest6.c,v 1.24 2025/07/08 00:47:41 jsg Exp $	*/
/*	$KAME: dest6.c,v 1.25 2001/02/22 01:39:16 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>

/*
 * Destination options header processing.
 */
int
dest6_input(struct mbuf **mp, int *offp, int proto, int af,
    struct netstack *ns)
{
	int off = *offp, dstoptlen, optlen;
	struct ip6_dest *dstopts;
	u_int8_t *opt;

	/* validation of the length of the header */
	dstopts = ip6_exthdr_get(mp, off, sizeof(*dstopts));
	if (dstopts == NULL)
		return IPPROTO_DONE;
	dstoptlen = (dstopts->ip6d_len + 1) << 3;

	dstopts = ip6_exthdr_get(mp, off, dstoptlen);
	if (dstopts == NULL)
		return IPPROTO_DONE;
	off += dstoptlen;
	dstoptlen -= sizeof(struct ip6_dest);
	opt = (u_int8_t *)dstopts + sizeof(struct ip6_dest);

	/* search header for all options. */
	for (optlen = 0; dstoptlen > 0; dstoptlen -= optlen, opt += optlen) {
		if (*opt != IP6OPT_PAD1 &&
		    (dstoptlen < IP6OPT_MINLEN || *(opt + 1) + 2 > dstoptlen)) {
			ip6stat_inc(ip6s_toosmall);
			goto bad;
		}

		switch (*opt) {
		case IP6OPT_PAD1:
			optlen = 1;
			break;
		case IP6OPT_PADN:
			optlen = *(opt + 1) + 2;
			break;
		default:		/* unknown option */
			optlen = ip6_unknown_opt(mp, opt,
			    opt - mtod(*mp, u_int8_t *));
			if (optlen == -1)
				return (IPPROTO_DONE);
			optlen += 2;
			break;
		}
	}

	*offp = off;
	return (dstopts->ip6d_nxt);

  bad:
	m_freemp(mp);
	return (IPPROTO_DONE);
}
