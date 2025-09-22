/*	$OpenBSD: ip6opt.c,v 1.10 2020/01/22 07:52:37 deraadt Exp $	*/
/*	$KAME: ip6opt.c,v 1.18 2005/06/15 07:11:35 keiichi Exp $	*/

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

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip6.h>

#include <string.h>
#include <stdio.h>

static int ip6optlen(u_int8_t *opt, u_int8_t *lim);

/*
 * Calculate the length of a given IPv6 option. Also checks
 * if the option is safely stored in user's buffer according to the
 * calculated length and the limitation of the buffer.
 */
static int
ip6optlen(u_int8_t *opt, u_int8_t *lim)
{
	int optlen;

	if (*opt == IP6OPT_PAD1)
		optlen = 1;
	else {
		/* is there enough space to store type and len? */
		if (opt + 2 > lim)
			return (0);
		optlen = *(opt + 1) + 2;
	}
	if (opt + optlen <= lim)
		return (optlen);

	return (0);
}

/*
 * The following functions are defined in RFC3542, which is a successor
 * of RFC2292.
 */

int
inet6_opt_init(void *extbuf, socklen_t extlen)
{
	struct ip6_ext *ext = (struct ip6_ext *)extbuf;

	if (ext) {
		if (extlen <= 0 || (extlen % 8))
			return (-1);
		ext->ip6e_len = (extlen >> 3) - 1;
	}

	return (2);		/* sizeof the next and the length fields */
}

int
inet6_opt_append(void *extbuf, socklen_t extlen, int offset, u_int8_t type,
		 socklen_t len, u_int8_t align, void **databufp)
{
	int currentlen = offset, padlen = 0;

	/*
	 * The option type must have a value from 2 to 255, inclusive.
	 * (0 and 1 are reserved for the Pad1 and PadN options, respectively.)
	 */
#if 0 /* always false */
	if (type < 2 || type > 255)
#else
	if (type < 2)
#endif
		return (-1);

	/*
	 * The option data length must have a value between 0 and 255,
	 * inclusive, and is the length of the option data that follows.
	 */
	if (len > 255)
		return (-1);

	/*
	 * The align parameter must have a value of 1, 2, 4, or 8.
	 * The align value can not exceed the value of len.
	 */
	if (align != 1 && align != 2 && align != 4 && align != 8)
		return (-1);
	if (align > len)
		return (-1);

	/* Calculate the padding length. */
	currentlen += 2 + len;	/* 2 means "type + len" */
	if (currentlen % align)
		padlen = align - (currentlen % align);

	/* The option must fit in the extension header buffer. */
	currentlen += padlen;
	if (extlen &&		/* XXX: right? */
	    currentlen > extlen)
		return (-1);

	if (extbuf) {
		u_int8_t *optp = (u_int8_t *)extbuf + offset;

		if (padlen == 1) {
			/* insert a Pad1 option */
			*optp = IP6OPT_PAD1;
			optp++;
		} else if (padlen > 0) {
			/* insert a PadN option for alignment */
			*optp++ = IP6OPT_PADN;
			*optp++ = padlen - 2;
			memset(optp, 0, padlen - 2);
			optp += (padlen - 2);
		}

		*optp++ = type;
		*optp++ = len;

		*databufp = optp;
	}

	return (currentlen);
}

int
inet6_opt_finish(void *extbuf, socklen_t extlen, int offset)
{
	int updatelen = offset > 0 ? (1 + ((offset - 1) | 7)) : 0;

	if (extbuf) {
		u_int8_t *padp;
		int padlen = updatelen - offset;

		if (updatelen > extlen)
			return (-1);

		padp = (u_int8_t *)extbuf + offset;
		if (padlen == 1)
			*padp = IP6OPT_PAD1;
		else if (padlen > 0) {
			*padp++ = IP6OPT_PADN;
			*padp++ = (padlen - 2);
			memset(padp, 0, padlen - 2);
		}
	}

	return (updatelen);
}

int
inet6_opt_set_val(void *databuf, int offset, void *val, socklen_t vallen)
{

	memcpy((u_int8_t *)databuf + offset, val, vallen);
	return (offset + vallen);
}

int
inet6_opt_next(void *extbuf, socklen_t extlen, int offset, u_int8_t *typep,
	       socklen_t *lenp, void **databufp)
{
	u_int8_t *optp, *lim;
	int optlen;

	/* Validate extlen. XXX: is the variable really necessary?? */
	if (extlen == 0 || (extlen % 8))
		return (-1);
	lim = (u_int8_t *)extbuf + extlen;

	/*
	 * If this is the first time this function called for this options
	 * header, simply return the 1st option.
	 * Otherwise, search the option list for the next option.
	 */
	if (offset == 0)
		optp = (u_int8_t *)((struct ip6_hbh *)extbuf + 1);
	else
		optp = (u_int8_t *)extbuf + offset;

	/* Find the next option skipping any padding options. */
	while (optp < lim) {
		switch(*optp) {
		case IP6OPT_PAD1:
			optp++;
			break;
		case IP6OPT_PADN:
			if ((optlen = ip6optlen(optp, lim)) == 0)
				goto optend;
			optp += optlen;
			break;
		default:	/* found */
			if ((optlen = ip6optlen(optp, lim)) == 0)
				goto optend;
			*typep = *optp;
			*lenp = optlen - 2;
			*databufp = optp + 2;
			return (optp + optlen - (u_int8_t *)extbuf);
		}
	}

  optend:
	*databufp = NULL; /* for safety */
	return (-1);
}

int
inet6_opt_find(void *extbuf, socklen_t extlen, int offset, u_int8_t type,
	       socklen_t *lenp, void **databufp)
{
	u_int8_t *optp, *lim;
	int optlen;

	/* Validate extlen. XXX: is the variable really necessary?? */
	if (extlen == 0 || (extlen % 8))
		return (-1);
	lim = (u_int8_t *)extbuf + extlen;

	/*
	 * If this is the first time this function called for this options
	 * header, simply return the 1st option.
	 * Otherwise, search the option list for the next option.
	 */
	if (offset == 0)
		optp = (u_int8_t *)((struct ip6_hbh *)extbuf + 1);
	else
		optp = (u_int8_t *)extbuf + offset;

	/* Find the specified option */
	while (optp < lim) {
		if ((optlen = ip6optlen(optp, lim)) == 0)
			goto optend;

		if (*optp == type) { /* found */
			*lenp = optlen - 2;
			*databufp = optp + 2;
			return (optp + optlen - (u_int8_t *)extbuf);
		}

		optp += optlen;
	}

  optend:
	*databufp = NULL; /* for safety */
	return (-1);
}

int
inet6_opt_get_val(void *databuf, int offset, void *val, socklen_t vallen)
{

	/* we can't assume alignment here */
	memcpy(val, (u_int8_t *)databuf + offset, vallen);

	return (offset + vallen);
}
