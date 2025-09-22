/*	$OpenBSD: net.c,v 1.20 2015/10/26 14:48:54 mmcc Exp $	*/
/*	$NetBSD: net.c,v 1.14 1996/10/13 02:29:02 christos Exp $	*/

/*
 * Copyright (c) 1992 Regents of the University of California.
 * All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *
 * @(#) Header: net.c,v 1.9 93/08/06 19:32:15 leres Exp  (LBL)
 */

#include <sys/param.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>

#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

#include "stand.h"
#include "net.h"

/*
 * Send a packet and wait for a reply, with exponential backoff.
 *
 * The send routine must return the actual number of bytes written.
 *
 * The receive routine can indicate success by returning the number of
 * bytes read; it can return 0 to indicate EOF; it can return -1 with a
 * non-zero errno to indicate failure; finally, it can return -1 with a
 * zero errno to indicate it isn't done yet.
 */
ssize_t
sendrecv(struct iodesc *d, ssize_t (*sproc)(struct iodesc *, void *, size_t),
    void *sbuf, size_t ssize,
    ssize_t (*rproc)(struct iodesc *, void *, size_t, time_t),
    void *rbuf, size_t rsize)
{
	ssize_t cc;
	time_t t, tmo, tlast;
	long tleft;

#ifdef NET_DEBUG
	if (debug)
		printf("sendrecv: called\n");
#endif

	tmo = MINTMO;
	tlast = tleft = 0;
	t = getsecs();
	for (;;) {
		if (tleft <= 0) {
			if (tmo >= MAXTMO) {
				errno = ETIMEDOUT;
				return -1;
			}
			cc = (*sproc)(d, sbuf, ssize);
			if (cc < 0 || (size_t)cc < ssize)
				panic("sendrecv: short write! (%d < %d)",
				    cc, ssize);

			tleft = tmo;
			tmo <<= 1;
			if (tmo > MAXTMO)
				tmo = MAXTMO;
			tlast = t;
		}

		/* Try to get a packet and process it. */
		cc = (*rproc)(d, rbuf, rsize, tleft);
		/* Return on data, EOF or real error. */
		if (cc != -1 || errno != 0)
			return (cc);

		/* Timed out or didn't get the packet we're waiting for */
		t = getsecs();
		tleft -= t - tlast;
		tlast = t;
	}
}

/*
 * Like inet_addr() in the C library, but we only accept base-10.
 * Return values are in network order.
 */
u_int32_t
inet_addr(const char *cp)
{
	u_long val;
	int n;
	char c;
	u_int parts[4];
	u_int *pp = parts;

	for (;;) {
		/*
		 * Collect number up to ``.''.
		 * Values are specified as for C:
		 * 0x=hex, 0=octal, other=decimal.
		 */
		val = 0;
		while ((c = *cp) != '\0') {
			if (c >= '0' && c <= '9') {
				val = (val * 10) + (c - '0');
				cp++;
				continue;
			}
			break;
		}
		if (*cp == '.') {
			/*
			 * Internet format:
			 *	a.b.c.d
			 *	a.b.c	(with c treated as 16-bits)
			 *	a.b	(with b treated as 24 bits)
			 */
			if (pp >= parts + 3 || val > 0xff)
				goto bad;
			*pp++ = val, cp++;
		} else
			break;
	}
	/*
	 * Check for trailing characters.
	 */
	if (*cp != '\0')
		goto bad;

	/*
	 * Concoct the address according to
	 * the number of parts specified.
	 */
	n = pp - parts + 1;
	switch (n) {

	case 1:				/* a -- 32 bits */
		break;

	case 2:				/* a.b -- 8.24 bits */
		if (val > 0xffffff)
			goto bad;
		val |= parts[0] << 24;
		break;

	case 3:				/* a.b.c -- 8.8.16 bits */
		if (val > 0xffff)
			goto bad;
		val |= (parts[0] << 24) | (parts[1] << 16);
		break;

	case 4:				/* a.b.c.d -- 8.8.8.8 bits */
		if (val > 0xff)
			goto bad;
		val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
		break;
	}

	return (htonl(val));
 bad:
	return (htonl(INADDR_NONE));
}

const char *
inet_ntoa(struct in_addr ia)
{
	return (intoa(ia.s_addr));
}

/* Similar to inet_ntoa() */
const char *
intoa(u_int32_t addr)
{
	char *cp;
	u_int byte;
	int n;
	static char buf[sizeof(".255.255.255.255")];

	addr = ntohl(addr);
	cp = &buf[sizeof buf];
	*--cp = '\0';

	n = 4;
	do {
		byte = addr & 0xff;
		*--cp = byte % 10 + '0';
		byte /= 10;
		if (byte > 0) {
			*--cp = byte % 10 + '0';
			byte /= 10;
			if (byte > 0)
				*--cp = byte + '0';
		}
		*--cp = '.';
		addr >>= 8;
	} while (--n > 0);

	return (cp+1);
}

static const char *
number(const char *s, int *n)
{
	for (*n = 0; isdigit((unsigned char)*s); s++)
		*n = (*n * 10) + *s - '0';
	return s;
}

u_int32_t
ip_convertaddr(const char *p)
{
#define IP_ANYADDR	0
	u_int32_t addr = 0, n;

	if (p == NULL || *p == '\0')
		return IP_ANYADDR;
	p = number(p, &n);
	addr |= (n << 24) & 0xff000000;
	if (*p == '\0' || *p++ != '.')
		return IP_ANYADDR;
	p = number(p, &n);
	addr |= (n << 16) & 0xff0000;
	if (*p == '\0' || *p++ != '.')
		return IP_ANYADDR;
	p = number(p, &n);
	addr |= (n << 8) & 0xff00;
	if (*p == '\0' || *p++ != '.')
		return IP_ANYADDR;
	p = number(p, &n);
	addr |= n & 0xff;
	if (*p != '\0')
		return IP_ANYADDR;

	return htonl(addr);
}
