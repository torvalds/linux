/*	$OpenBSD: sdl.c,v 1.25 2022/12/26 20:06:43 jmc Exp $ */

/*
 * Copyright (c) 2003-2007 Bob Beck.  All rights reserved.
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
 * sdl.c - Implement spamd source lists
 *
 * This consists of everything we need to do to determine which lists
 * someone is on. Spamd gets the connecting address, and looks it up
 * against all lists to determine what deferral messages to feed back
 * to the connecting machine. - The redirection to spamd will happen
 * from pf in the kernel, first match will divert to us. Spamd (along with
 * setup) must keep track of *all* matches, so as to tell someone all the
 * lists that they are on.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdl.h"

static void sdl_free(struct sdlist *);
static void sdl_clear(struct sdlist *);

extern int debug;
struct sdlist *blacklists = NULL;
int blc = 0, blu = 0;

static int
compar_v4(const void *va, const void *vb)
{
	const struct sdentry_v4 *a = va;
	const struct sdentry_v4 *b = vb;
	struct in_addr aa;
	struct in_addr bb;

	/* The mask has already been applied. */
	aa.s_addr = ntohl(a->sda.s_addr);
	bb.s_addr = ntohl(b->sda.s_addr);

	if (aa.s_addr > bb.s_addr)
		return (1);
	if (aa.s_addr < bb.s_addr)
		return (-1);
	return (0);
}

static int
compar_v6(const void *va, const void *vb)
{
	const struct sdentry_v6 *a = va;
	const struct sdentry_v6 *b = vb;
	struct sdaddr_v6 aa;
	struct sdaddr_v6 bb;

	/* The mask has already been applied. */
	aa.addr32[0] = ntohl(a->sda.addr32[0]);
	aa.addr32[1] = ntohl(a->sda.addr32[1]);
	aa.addr32[2] = ntohl(a->sda.addr32[2]);
	aa.addr32[3] = ntohl(a->sda.addr32[3]);

	bb.addr32[0] = ntohl(b->sda.addr32[0]);
	bb.addr32[1] = ntohl(b->sda.addr32[1]);
	bb.addr32[2] = ntohl(b->sda.addr32[2]);
	bb.addr32[3] = ntohl(b->sda.addr32[3]);

	if (aa.addr32[0] > bb.addr32[0])
		return (1);
	if (aa.addr32[0] < bb.addr32[0])
		return (-1);
	if (aa.addr32[1] > bb.addr32[1])
		return (1);
	if (aa.addr32[1] < bb.addr32[1])
		return (-1);
	if (aa.addr32[2] > bb.addr32[2])
		return (1);
	if (aa.addr32[2] < bb.addr32[2])
		return (-1);
	if (aa.addr32[3] > bb.addr32[3])
		return (1);
	if (aa.addr32[3] < bb.addr32[3])
		return (-1);
	return (0);
}

int
sdl_add(char *sdname, char *sdstring, char **v4, u_int nv4, char **v6, u_int nv6)
{
	int i, idx = -1;
	char astring[40];
	char *addr = NULL;
	unsigned int maskbits;

	/*
	 * if a blacklist of same tag name is already there, replace it,
	 * otherwise append.
	 */
	for (i = 0; i < blu; i++) {
		if (strcmp(blacklists[i].tag, sdname) == 0) {
			idx = i;
			break;
		}
	}
	if (idx != -1) {
		if (debug > 0)
			printf("replacing list %s; %u new entries\n",
			    blacklists[idx].tag, nv4 + nv6);
		sdl_free(&blacklists[idx]);
	} else {
		if (debug > 0)
			printf("adding list %s; %u entries\n", sdname, nv4 + nv6);
		if (blu == blc) {
			struct sdlist *tmp;

			tmp = reallocarray(blacklists, blc + 128,
			    sizeof(struct sdlist));
			if (tmp == NULL)
				return (-1);
			blacklists = tmp;
			blc += 128;
			sdl_clear(&blacklists[blu]);
		}
		idx = blu;
	}

	if ((blacklists[idx].tag = strdup(sdname)) == NULL)
		goto misc_error;
	if ((blacklists[idx].string = strdup(sdstring)) == NULL)
		goto misc_error;

	/*
	 * Cycle through addrs by family, converting. We assume they are
	 * correctly formatted v4 and v6 addrs, if they don't all convert
	 * correctly, the add fails. Each address should be address/maskbits.
	 */
	if (nv4 != 0) {
		blacklists[idx].v4.naddrs = nv4;
		blacklists[idx].v4.addrs = reallocarray(NULL, nv4,
		    sizeof(struct sdentry_v4));
		if (blacklists[idx].v4.addrs == NULL)
			goto misc_error;
		for (i = 0; i < nv4; i++) {
			struct in_addr *m, *n;
			int j;

			n = &blacklists[idx].v4.addrs[i].sda;
			m = &blacklists[idx].v4.addrs[i].sdm;

			addr = v4[i];
			j = sscanf(addr, "%15[^/]/%u", astring, &maskbits);
			if (j != 2)
				goto parse_error;
			/*
			 * sanity check! we don't allow a 0 mask -
			 * don't blacklist the entire net.
			 */
			if (maskbits == 0 || maskbits > 32)
				goto parse_error;
			j = inet_pton(AF_INET, astring, n);
			if (j != 1)
				goto parse_error;
			if (debug > 0)
				printf("added %s/%u\n", astring, maskbits);

			/* set mask. */
			m->s_addr = 0xffffffffU << (32 - maskbits);
			m->s_addr = htonl(m->s_addr);

			/* mask off address bits that won't ever be used */
			n->s_addr = n->s_addr & m->s_addr;
		}
		/* spamd-setup output is sorted in host byte order */
		mergesort(blacklists[idx].v4.addrs, nv4,
		    sizeof(struct sdentry_v4), compar_v4);
	}
	if (nv6 != 0) {
		blacklists[idx].v6.naddrs = nv6;
		blacklists[idx].v6.addrs = reallocarray(NULL, nv6,
		    sizeof(struct sdentry_v6));
		if (blacklists[idx].v6.addrs == NULL)
			goto misc_error;

		for (i = 0; i < nv6; i++) {
			int j, k;
			struct sdaddr_v6 *m, *n;

			n = &blacklists[idx].v6.addrs[i].sda;
			m = &blacklists[idx].v6.addrs[i].sdm;

			addr = v6[i];
			j = sscanf(addr, "%39[^/]/%u", astring, &maskbits);
			if (j != 2)
				goto parse_error;
			/*
			 * sanity check! we don't allow a 0 mask -
			 * don't blacklist the entire net.
			 */
			if (maskbits == 0 || maskbits > 128)
				goto parse_error;
			j = inet_pton(AF_INET6, astring, n);
			if (j != 1)
				goto parse_error;
			if (debug > 0)
				printf("added %s/%u\n", astring, maskbits);

			/* set mask, borrowed from pf */
			k = 0;
			for (j = 0; j < 4; j++)
				m->addr32[j] = 0;
			while (maskbits >= 32) {
				m->addr32[k++] = 0xffffffffU;
				maskbits -= 32;
			}
			for (j = 31; j > 31 - maskbits; --j)
				m->addr32[k] |= (1 << j);
			if (maskbits)
				m->addr32[k] = htonl(m->addr32[k]);

			/* mask off address bits that won't ever be used */
			for (j = 0; j < 4; j++)
				n->addr32[j] = n->addr32[j] & m->addr32[j];
		}
		/* spamd-setup output is sorted in host byte order */
		mergesort(blacklists[idx].v6.addrs, nv6,
		    sizeof(struct sdentry_v6), compar_v6);
	}
	if (idx == blu) {
		blu++;
		sdl_clear(&blacklists[blu]);
	}
	return (0);
 parse_error:
	if (debug > 0)
		printf("sdl_add: parse error, \"%s\"\n", addr);
 misc_error:
	sdl_free(&blacklists[idx]);
	if (idx != blu) {
		memmove(&blacklists[idx], &blacklists[idx + 1],
		    (blu - idx) * sizeof(*blacklists));
		blu--;
	}
	return (-1);
}

void
sdl_del(char *sdname)
{
	int i, idx = -1;

	for (i = 0; i < blu; i++) {
		if (strcmp(blacklists[i].tag, sdname) == 0) {
			idx = i;
			break;
		}
	}
	if (idx != -1) {
		if (debug > 0)
			printf("clearing list %s\n", sdname);
		/* Must preserve tag. */
		free(blacklists[idx].string);
		free(blacklists[idx].v4.addrs);
		free(blacklists[idx].v6.addrs);
		blacklists[idx].string = NULL;
		blacklists[idx].v4.addrs = NULL;
		blacklists[idx].v6.addrs = NULL;
		blacklists[idx].v4.naddrs = 0;
		blacklists[idx].v6.naddrs = 0;
	}
}

/*
 * Return 0 if the address a (with mask m) matches address key
 * otherwise return 1 if a > key or -1 if a < key.  It is assumed
 * that address a has been pre-masked out, we only need to mask key.
 */
static int
match_addr_v4(const void *vkey, const void *ventry)
{
	const struct in_addr *k = vkey;
	const struct in_addr *a = &((const struct sdentry_v4 *)ventry)->sda;
	const struct in_addr *m = &((const struct sdentry_v4 *)ventry)->sdm;
	struct in_addr kk;
	struct in_addr aa;

	kk.s_addr = ntohl(k->s_addr & m->s_addr);
	aa.s_addr = ntohl(a->s_addr);
	if (kk.s_addr > aa.s_addr)
		return (1);
	if (kk.s_addr < aa.s_addr)
		return (-1);
	return (0);
}

/*
 * Return 0 if the address a (with mask m) matches address key
 * otherwise return 1 if a > key or -1 if a < key.  It is assumed
 * that address a has been pre-masked out, we only need to mask key.
 */
static int
match_addr_v6(const void *vkey, const void *ventry)
{
	const struct sdaddr_v6 *k = vkey;
	const struct sdaddr_v6 *a = &((const struct sdentry_v6 *)ventry)->sda;
	const struct sdaddr_v6 *m = &((const struct sdentry_v6 *)ventry)->sdm;
	struct sdaddr_v6 kk;
	struct sdaddr_v6 aa;

	kk.addr32[0] = ntohl(k->addr32[0] & m->addr32[0]);
	kk.addr32[1] = ntohl(k->addr32[1] & m->addr32[1]);
	kk.addr32[2] = ntohl(k->addr32[2] & m->addr32[2]);
	kk.addr32[3] = ntohl(k->addr32[3] & m->addr32[3]);

	aa.addr32[0] = ntohl(a->addr32[0]);
	aa.addr32[1] = ntohl(a->addr32[1]);
	aa.addr32[2] = ntohl(a->addr32[2]);
	aa.addr32[3] = ntohl(a->addr32[3]);

	if (kk.addr32[0] > aa.addr32[0])
		return (1);
	if (kk.addr32[0] < aa.addr32[0])
		return (-1);
	if (kk.addr32[1] > aa.addr32[1])
		return (1);
	if (kk.addr32[1] < aa.addr32[1])
		return (-1);
	if (kk.addr32[2] > aa.addr32[2])
		return (1);
	if (kk.addr32[2] < aa.addr32[2])
		return (-1);
	if (kk.addr32[3] > aa.addr32[3])
		return (1);
	if (kk.addr32[3] < aa.addr32[3])
		return (-1);
	return (0);
}

#define grow_sdlist(sd, c, l) do {					       \
	if (c == l) {							       \
		struct sdlist **tmp;					       \
									       \
		tmp = reallocarray(sd, l + 128, sizeof(struct sdlist *));      \
		if (tmp == NULL) {					       \
			/*						       \
			 * XXX out of memory - return what we have	       \
			 */						       \
			return (sdnew);					       \
		}							       \
		sd = tmp;						       \
		l += 128;						       \
	}								       \
} while (0)

static struct sdlist **
sdl_lookup_v4(struct sdlist *sdl, struct in_addr *src)
{
	int matches = 0;
	int sdnewlen = 0;
	struct sdlist **sdnew = NULL;

	while (sdl->tag != NULL) {
		if (bsearch(src, sdl->v4.addrs, sdl->v4.naddrs,
		    sizeof(struct sdentry_v4), match_addr_v4) != NULL) {
			grow_sdlist(sdnew, matches, sdnewlen);
			sdnew[matches] = sdl;
			matches++;
			sdnew[matches] = NULL;
			break;
		}
		sdl++;
	}
	return (sdnew);
}

static struct sdlist **
sdl_lookup_v6(struct sdlist *sdl, struct sdaddr_v6 *src)
{
	int matches = 0;
	int sdnewlen = 0;
	struct sdlist **sdnew = NULL;

	while (sdl->tag != NULL) {
		if (bsearch(src, sdl->v6.addrs, sdl->v6.naddrs,
		    sizeof(struct sdentry_v6), match_addr_v6) != NULL) {
			grow_sdlist(sdnew, matches, sdnewlen);
			sdnew[matches] = sdl;
			matches++;
			sdnew[matches] = NULL;
			break;
		}
		sdl++;
	}
	return (sdnew);
}

/*
 * Given an address and address family
 * return list of pointers to matching nodes. or NULL if none.
 */
struct sdlist **
sdl_lookup(struct sdlist *head, int af, void *src)
{
	if (head == NULL)
		return (NULL);

	switch (af) {
	case AF_INET:
		return (sdl_lookup_v4(head, src));
	case AF_INET6:
		return (sdl_lookup_v6(head, src));
	default:
		return (NULL);
	}
}

static int
sdl_check_v4(struct sdlist *sdl, struct in_addr *src)
{
	while (sdl->tag != NULL) {
		if (bsearch(src, sdl->v4.addrs, sdl->v4.naddrs,
		    sizeof(struct sdentry_v4), match_addr_v4) != NULL)
			return (1);
		sdl++;
	}
	return (0);
}

static int
sdl_check_v6(struct sdlist *sdl, struct sdaddr_v6 *src)
{
	while (sdl->tag != NULL) {
		if (bsearch(src, sdl->v6.addrs, sdl->v6.naddrs,
		    sizeof(struct sdentry_v6), match_addr_v6) != NULL)
			return (1);
		sdl++;
	}
	return (0);
}

/*
 * Given an address and address family
 * returns 1 if address is on a blacklist, else 0.
 */
int
sdl_check(struct sdlist *head, int af, void *src)
{
	if (head == NULL)
		return (0);

	switch (af) {
	case AF_INET:
		return (sdl_check_v4(head, src));
	case AF_INET6:
		return (sdl_check_v6(head, src));
	default:
		return (0);
	}
}

static void
sdl_free(struct sdlist *sdl)
{
	free(sdl->tag);
	free(sdl->string);
	free(sdl->v4.addrs);
	free(sdl->v6.addrs);
	sdl_clear(sdl);
}

static void
sdl_clear(struct sdlist *sdl)
{
	sdl->tag = NULL;
	sdl->string = NULL;
	sdl->v4.addrs = NULL;
	sdl->v4.naddrs = 0;
	sdl->v6.addrs = NULL;
	sdl->v6.naddrs = 0;
}
