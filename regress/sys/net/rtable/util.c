
/*
 * Copyright (c) 2015 Martin Pieuchot
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
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

#define _KERNEL
#include <sys/refcnt.h>

#include <stdint.h>
#include <sys/rwlock.h>
#undef _KERNEL

#include "srp_compat.h"
#include "smr_compat.h"

#include <sys/socket.h>
#include <sys/domain.h>
#include <sys/queue.h>
#include <sys/srp.h>

#include <net/rtable.h>
#define _KERNEL
#include <net/route.h>
#undef _KERNEL

#include <netinet/in.h>
#include <arpa/inet.h>

#include <assert.h>
#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

struct sockaddr *rt_plen2mask(const struct rtentry *, struct sockaddr_in6 *);

struct domain inetdomain = {
  .dom_family		= AF_INET,
  .dom_name		= "inet",
  .dom_init		= NULL,
  .dom_externalize	= NULL,
  .dom_dispose		= NULL,
  .dom_protosw		= NULL,
  .dom_protoswNPROTOSW	= NULL,
  .dom_rtoffset		= offsetof(struct sockaddr_in, sin_addr),
  .dom_maxplen		= 32,
};

struct domain inet6domain = {
  .dom_family		= AF_INET6,
  .dom_name		= "inet6",
  .dom_init		= NULL,
  .dom_externalize	= NULL,
  .dom_dispose		= NULL,
  .dom_protosw		= NULL,
  .dom_protoswNPROTOSW	= NULL,
  .dom_rtoffset		= offsetof(struct sockaddr_in6, sin6_addr),
  .dom_maxplen		= 128,
};

struct domain *domains[] = { &inetdomain, &inet6domain, NULL };

/*
 * Insert a route from a string containing a destination: "192.168.1/24"
 */
void
route_insert(unsigned int rid, sa_family_t af, char *string)
{
	struct sockaddr_storage	 ss, ms;
	struct sockaddr		*ndst, *dst = (struct sockaddr *)&ss;
	struct sockaddr		*mask = (struct sockaddr *)&ms;
	struct rtentry		*rt, *nrt;
	char			 ip[INET6_ADDRSTRLEN];
	int			 plen, error;

	rt = calloc(1, sizeof(*rt));
	if (rt == NULL)
		errx(1, "out of memory");
	refcnt_init(&rt->rt_refcnt);

	plen = inet_net_ptosa(af, string, dst, mask);
	if (plen == -1)
		err(1, "wrong line: %s", string);

	/* Normalize sockaddr a la rtrequest1(9) */
	ndst = malloc(dst->sa_len);
	if (ndst == NULL)
		errx(1, "out of memory");
	rt_maskedcopy(dst, ndst, mask);

	if ((error = rtable_insert(rid, ndst, mask, NULL, 0, rt)) != 0) {
		inet_net_satop(af, ndst, plen, ip, sizeof(ip));
		errx(1, "can't add route: %s, %s", ip, strerror(error));
	}
	nrt = rtable_lookup(rid, dst, mask, NULL, RTP_ANY);
	if (nrt != rt) {
		inet_net_satop(af, rt_key(rt), plen, ip, sizeof(ip));
		errx(1, "added route not found: %s", ip);
	}
	rtfree(rt);
	rtfree(nrt);
}

/*
 * Delete a route from a string containing a destination: "192.168.1/24"
 */
void
route_delete(unsigned int rid, sa_family_t af, char *string)
{
	struct sockaddr_storage	 ss, ms;
	struct sockaddr		*dst = (struct sockaddr *)&ss;
	struct sockaddr		*mask = (struct sockaddr *)&ms;
	struct rtentry		*rt, *nrt;
	char			 ip[INET6_ADDRSTRLEN];
	int			 plen, error;

	plen = inet_net_ptosa(af, string, dst, mask);
	if (plen == -1)
		err(1, "wrong line: %s", string);

	rt = rtable_lookup(0, dst, mask, NULL, RTP_ANY);
	if (rt == NULL) {
		inet_net_satop(af, dst, plen, ip, sizeof(ip));
		errx(1, "can't find route: %s", ip);
	}

	assert(memcmp(rt_key(rt), dst, dst->sa_len) == 0);
	assert(rt_plen(rt) == rtable_satoplen(af, mask));

	if ((error = rtable_delete(0, dst, mask, rt)) != 0) {
		inet_net_satop(af, dst, plen, ip, sizeof(ip));
		errx(1, "can't rm route: %s, %s", ip, strerror(error));
	}

	nrt = rtable_lookup(0, dst, mask, NULL, RTP_ANY);
	if (nrt != NULL) {
		char ip0[INET6_ADDRSTRLEN];
		inet_net_satop(af, rt_key(nrt), plen, ip, sizeof(ip));
		inet_net_satop(af, rt_key(rt), plen, ip0, sizeof(ip0));
		errx(1, "found: %s after deleting: %s", ip, ip0);
	}

	rtfree(rt);
	assert(refcnt_read(&rt->rt_refcnt) == 0);

	free(rt_key(rt));
	free(rt);
}

/*
 * Lookup a route from a string containing a destination: "192.168.1/24"
 */
void
route_lookup(unsigned int rid, sa_family_t af, char *string)
{
	struct sockaddr_storage	 ss, ms;
	struct sockaddr		*dst = (struct sockaddr *)&ss;
	struct sockaddr		*mask = (struct sockaddr *)&ms;
	struct rtentry		*rt;
	char			 ip[INET6_ADDRSTRLEN];
	int			 plen;

	plen = inet_net_ptosa(af, string, dst, mask);
	if (plen == -1)
		err(1, "wrong line: %s", string);

	rt = rtable_lookup(0, dst, mask, NULL, RTP_ANY);
	if (rt == NULL) {
		inet_net_satop(af, dst, plen, ip, sizeof(ip));
		errx(1, "%s not found", ip);
	}
	assert(memcmp(rt_key(rt), dst, dst->sa_len) == 0);
	assert(rt_plen(rt) == rtable_satoplen(af, mask));

	rtfree(rt);
}

int
do_from_file(unsigned int rid, sa_family_t af, char *filename,
    void (*func)(unsigned int, sa_family_t, char *))
{
	FILE			*fp;
	char			*buf;
	size_t			 len;
	int			 lines = 0;

	if ((fp = fopen(filename, "r")) == NULL)
		errx(1, "No such file: %s", filename);

	while ((buf = fgetln(fp, &len)) != NULL) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';

		(*func)(rid, af, buf);
		lines++;
	}
	fclose(fp);

	return (lines);
}

int
rtentry_dump(struct rtentry *rt, void *w, unsigned int rid)
{
	char			 dest[INET6_ADDRSTRLEN];
	sa_family_t		 af = rt_key(rt)->sa_family;

	inet_net_satop(af, rt_key(rt), rt_plen(rt), dest, sizeof(dest));
	printf("%s\n", dest);

	return (0);
}

int
rtentry_delete(struct rtentry *rt, void *w, unsigned int rid)
{
	char			 dest[INET6_ADDRSTRLEN];
	sa_family_t		 af = rt_key(rt)->sa_family;
	struct sockaddr_in6	 sa_mask;
	struct sockaddr		*mask = rt_plen2mask(rt, &sa_mask);
	int			 error;
	unsigned int		 refs;

	assert(rt_plen(rt) == rtable_satoplen(af, mask));
	refs = refcnt_read(&rt->rt_refcnt);
	assert(refs > 0);
	if ((error = rtable_delete(0, rt_key(rt), mask, rt)) != 0) {
		inet_net_satop(af, rt_key(rt), rt_plen(rt), dest, sizeof(dest));
		errx(1, "can't rm route: %s, %s", dest, strerror(error));
	}
	assert(refcnt_read(&rt->rt_refcnt) == refs - 1);

	return (0);
}

void
rt_maskedcopy(struct sockaddr *src, struct sockaddr *dst,
    struct sockaddr *netmask)
{
	uint8_t	*cp1 = (uint8_t *)src;
	uint8_t	*cp2 = (uint8_t *)dst;
	uint8_t	*cp3 = (uint8_t *)netmask;
	uint8_t	*cplim = cp2 + *cp3;
	uint8_t	*cplim2 = cp2 + *cp1;

	*cp2++ = *cp1++; *cp2++ = *cp1++; /* copies sa_len & sa_family */
	cp3 += 2;
	if (cplim > cplim2)
		cplim = cplim2;
	while (cp2 < cplim)
		*cp2++ = *cp1++ & *cp3++;
	if (cp2 < cplim2)
		memset(cp2, 0, (unsigned int)(cplim2 - cp2));
}

void
rtref(struct rtentry *rt)
{
	refcnt_take(&rt->rt_refcnt);
}

void
rtfree(struct rtentry *rt)
{
	if (refcnt_rele(&rt->rt_refcnt) == 0)
		return;
}

void
refcnt_init(struct refcnt *r)
{
	r->r_refs = 1;
}

void
refcnt_take(struct refcnt *r)
{
	u_int refs;

	refs = ++r->r_refs;
	assert(refs != 0);
}

int
refcnt_rele(struct refcnt *r)
{
	u_int refs;

	refs = --r->r_refs;
	assert(refs != ~0);
	if (r->r_refs == 0)
		return (1);
	return (0);
}

unsigned int
refcnt_read(const struct refcnt *r)
{
	return (r->r_refs);
}

void
in_prefixlen2mask(struct in_addr *maskp, int plen)
{
	if (plen == 0)
		maskp->s_addr = 0;
	else
		maskp->s_addr = htonl(0xffffffff << (32 - plen));
}

void
in6_prefixlen2mask(struct in6_addr *maskp, int len)
{
	uint8_t maskarray[8] = {0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff};
	int bytelen, bitlen, i;

	assert(0 <= len && len <= 128);

	memset(maskp, 0, sizeof(*maskp));
	bytelen = len / 8;
	bitlen = len % 8;
	for (i = 0; i < bytelen; i++)
		maskp->s6_addr[i] = 0xff;
	/* len == 128 is ok because bitlen == 0 then */
	if (bitlen)
		maskp->s6_addr[bytelen] = maskarray[bitlen - 1];
}

struct sockaddr *
rt_plentosa(sa_family_t af, int plen, struct sockaddr_in6 *sa_mask)
{
	struct sockaddr_in	*sin = (struct sockaddr_in *)sa_mask;
	struct sockaddr_in6	*sin6 = (struct sockaddr_in6 *)sa_mask;

	assert(plen >= 0 || plen == -1);

	if (plen == -1)
		return (NULL);

	memset(sa_mask, 0, sizeof(*sa_mask));

	switch (af) {
	case AF_INET:
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(struct sockaddr_in);
		in_prefixlen2mask(&sin->sin_addr, plen);
		break;
	case AF_INET6:
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(struct sockaddr_in6);
		in6_prefixlen2mask(&sin6->sin6_addr, plen);
		break;
	default:
		return (NULL);
	}

	return ((struct sockaddr *)sa_mask);
}

struct sockaddr *
rt_plen2mask(const struct rtentry *rt, struct sockaddr_in6 *sa_mask)
{
	return (rt_plentosa(rt_key(rt)->sa_family, rt_plen(rt), sa_mask));
}


int
inet_net_ptosa(sa_family_t af, const char *buf, struct sockaddr *sa,
    struct sockaddr *ma)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
	int i, plen;

	switch (af) {
	case AF_INET:
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = af;
		sin->sin_len = sizeof(*sin);
		plen = inet_net_pton(af, buf, &sin->sin_addr,
		    sizeof(sin->sin_addr));
		if (plen == -1 || ma == NULL)
			break;

		sin = (struct sockaddr_in *)ma;
		memset(sin, 0, sizeof(*sin));
		sin->sin_len = sizeof(*sin);
		sin->sin_family = 0;
		in_prefixlen2mask(&sin->sin_addr, plen);
		break;
	case AF_INET6:
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = af;
		sin6->sin6_len = sizeof(*sin6);
		plen = inet_net_pton(af, buf, &sin6->sin6_addr,
		    sizeof(sin6->sin6_addr));
		if (plen == -1 || ma == NULL)
			break;

		sin6 = (struct sockaddr_in6 *)ma;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_len = sizeof(*sin6);
		sin6->sin6_family = 0;
		for (i = 0; i < plen / 8; i++)
			sin6->sin6_addr.s6_addr[i] = 0xff;
		i = plen % 8;
		if (i)
			sin6->sin6_addr.s6_addr[plen / 8] = 0xff00 >> i;
		break;
	default:
		plen = -1;
	}

	return (plen);
}

/*
 * Only compare the address fields, we cannot use memcmp(3) because
 * the radix tree abuses the first fields of the mask sockaddr for
 * a different purpose.
 */
int
maskcmp(sa_family_t af, struct sockaddr *sa1, struct sockaddr *sa2)
{
	struct sockaddr_in *sin1, *sin2;
	struct sockaddr_in6 *sin61, *sin62;
	int len;

	switch (af) {
	case AF_INET:
		sin1 = (struct sockaddr_in *)sa1;
		sin2 = (struct sockaddr_in *)sa2;
		len = sizeof(sin1->sin_addr);
		return memcmp(&sin1->sin_addr, &sin2->sin_addr, len);
	case AF_INET6:
		sin61 = (struct sockaddr_in6 *)sa1;
		sin62 = (struct sockaddr_in6 *)sa2;
		len = sizeof(sin61->sin6_addr);
		return memcmp(&sin61->sin6_addr, &sin62->sin6_addr, len);
	default:
		return (-1);
	}
}

char *
inet_net_satop(sa_family_t af, struct sockaddr *sa, int plen, char *buf,
    size_t len)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

	switch (af) {
	case AF_INET:
		return inet_net_ntop(af, &sin->sin_addr, plen, buf, len);
	case AF_INET6:
		return inet_net_ntop(af, &sin6->sin6_addr, plen, buf, len);
	default:
		return (NULL);
	}
}

/* Give some jitter to hash, to avoid synchronization between routers. */
static uint32_t		rt_hashjitter;

/*
 * Originated from bridge_hash() in if_bridge.c
 */
#define mix(a, b, c) do {						\
	a -= b; a -= c; a ^= (c >> 13);					\
	b -= c; b -= a; b ^= (a << 8);					\
	c -= a; c -= b; c ^= (b >> 13);					\
	a -= b; a -= c; a ^= (c >> 12);					\
	b -= c; b -= a; b ^= (a << 16);					\
	c -= a; c -= b; c ^= (b >> 5);					\
	a -= b; a -= c; a ^= (c >> 3);					\
	b -= c; b -= a; b ^= (a << 10);					\
	c -= a; c -= b; c ^= (b >> 15);					\
} while (0)

int
rt_hash(struct rtentry *rt, const struct sockaddr *dst, uint32_t *src)
{
	uint32_t a, b, c;

	while (rt_hashjitter == 0)
		rt_hashjitter = arc4random();

	if (src == NULL)
		return (-1);

	a = b = 0x9e3779b9;
	c = rt_hashjitter;

	switch (dst->sa_family) {
	case AF_INET:
	    {
		struct sockaddr_in *sin;

		sin = satosin(dst);
		a += sin->sin_addr.s_addr;
		b += (src != NULL) ? src[0] : 0;
		mix(a, b, c);
		break;
	    }
#ifdef INET6
	case AF_INET6:
	    {
		struct sockaddr_in6 *sin6;

		sin6 = satosin6(dst);
		a += sin6->sin6_addr.s6_addr32[0];
		b += sin6->sin6_addr.s6_addr32[2];
		c += (src != NULL) ? src[0] : 0;
		mix(a, b, c);
		a += sin6->sin6_addr.s6_addr32[1];
		b += sin6->sin6_addr.s6_addr32[3];
		c += (src != NULL) ? src[1] : 0;
		mix(a, b, c);
		a += sin6->sin6_addr.s6_addr32[2];
		b += sin6->sin6_addr.s6_addr32[1];
		c += (src != NULL) ? src[2] : 0;
		mix(a, b, c);
		a += sin6->sin6_addr.s6_addr32[3];
		b += sin6->sin6_addr.s6_addr32[0];
		c += (src != NULL) ? src[3] : 0;
		mix(a, b, c);
		break;
	    }
#endif /* INET6 */
	}

	return (c & 0xffff);
}

void
rt_timer_init(void)
{
}

/*
 * SMR stuff
 */

unsigned int smr_crit;

void
smr_read_enter(void)
{
	smr_crit++;
}

void
smr_read_leave(void)
{
	smr_crit--;
}

void
SMR_ASSERT_CRITICAL(void)
{
	assert(smr_crit > 0);
}
