/*	$OpenBSD: addr_range.c,v 1.8 2024/08/22 07:56:47 florian Exp $ */
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * addr_range.c - Parser/aggregator for varios address/network expressions.
 *
 *	Input:  192.168.0.0/24 192.168.1.0/24
 *	Output: 192.168.0.0/23
 *
 *	Input:  192.168.0.128-192.168.0.255
 *	Output: 192.168.0.128/25
 *
 * Notice:
 *	- Byte order of 'addr' and 'mask' (members of struct in_addr_range)
 *	  are host byte order.
 *	- Parsing address range like 192.168.0.0-192.168.255.255 costs much of
 *	  cpu/memory.
 *
 * Example code:
 *
 *	struct in_addr_range *list = NULL, *cur;
 *
 *	in_addr_range_list_add(&list, "192.168.0.128-192.168.0.255");
 *	in_addr_range_list_add(&list, "192.168.1.128-192.168.1.255");
 *	for (cur = list; cur != NULL; cur = cur->next) {
 *		// do something
 *		struct in_addr a, m;
 *		a.s_addr = htonl(cur->addr);
 *		m.s_addr = htonl(cur->mask);
 *	}
 *	in_addr_range_list_remove_all(&list);
 *
 * Author:
 *	Yasuoka Masahiko <yasuoka@iij.ad.jp>
 *
 * $Id: addr_range.c,v 1.8 2024/08/22 07:56:47 florian Exp $
 */
#ifdef ADDR_RANGE_DEBUG
#define IIJDEBUG
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef IIJDEBUG
#include <debugutil.h>
static int saved_errno;
#define	INT4_CHARS(x) \
	((x) & 0xff000000) >> 24, ((x) & 0x00ff0000) >> 16, \
	((x) & 0x0000ff00) >> 8,  ((x) & 0x000000ff)
#endif
#include "addr_range.h"

static void  in_addr_range_list_uniq(struct in_addr_range **);
static int   in_addr_range_list_add0(struct in_addr_range **, u_int32_t, u_int32_t);
static int   bitmask2masklen(u_int32_t);

struct in_addr_range *
in_addr_range_create()
{
	struct in_addr_range *_this;
	if ((_this = malloc(sizeof(struct in_addr_range))) == NULL)
		return NULL;
	memset(_this, 0xff, sizeof(struct in_addr_range));
	_this->next = NULL;
	return _this;
}


void
in_addr_range_destroy(struct in_addr_range *_this)
{
	free(_this);
}

void
in_addr_range_list_remove_all(struct in_addr_range **list)
{
	struct in_addr_range *cur0, *cur;

	cur = *list;
	while (cur != NULL) {
		cur0 = cur;
		cur = cur->next;
		in_addr_range_destroy(cur0);
	}
	*list = NULL;
}

static void
in_addr_range_list_uniq(struct in_addr_range **list)
{
	u_int32_t m0;
	struct in_addr_range **cur, *a, *b;

restart:
	for (cur = list; *cur != NULL; cur = &(*cur)->next) {
		if ((*cur)->next == NULL)
			continue;
		a = *cur;
		b = (*cur)->next;
		m0 = 0xffffffff ^ a->mask;
		if (a->mask == b->mask && (
		    a->addr - b->addr == m0 + 1 ||
		    b->addr - a->addr == m0 + 1)) {
			m0 = m0 * 2 + 1;
			m0 ^= 0xffffffff;
			if ((a->addr & m0) != (b->addr & m0))
				continue;
			if (a->addr > b->addr) {
#ifdef IIJDEBUG
		log_printf(LOG_DL_2,
		    "%d.%d.%d.%d/%d + %d.%d.%d.%d/%d = %d.%d.%d.%d/%d"
		    , INT4_CHARS(a->addr), bitmask2masklen(a->mask)
		    , INT4_CHARS(b->addr), bitmask2masklen(b->mask)
		    , INT4_CHARS(b->addr), bitmask2masklen(m0)
		    );
#endif
				b->mask = m0;
				*cur = b;
				in_addr_range_destroy(a);
			} else {
#ifdef IIJDEBUG
		log_printf(LOG_DL_2,
		    "==>%d.%d.%d.%d/%d + %d.%d.%d.%d/%d = %d.%d.%d.%d/%d"
		    , INT4_CHARS(a->addr), bitmask2masklen(a->mask)
		    , INT4_CHARS(b->addr), bitmask2masklen(b->mask)
		    , INT4_CHARS(a->addr), bitmask2masklen(m0)
		    );
#endif
				a->mask = m0;
				(*cur)->next = b->next;
				in_addr_range_destroy(b);
			}
			goto restart;
		}
	}
}

int
in_addr_range_list_includes(struct in_addr_range **list, struct in_addr *addr)
{
	struct in_addr_range *cur;
	u_int32_t addr0 = ntohl(addr->s_addr);

	for (cur = *list; cur != NULL; cur = cur->next) {
		if ((addr0 & cur->mask) == (cur->addr & cur->mask))
			return 1;
	}
	return 0;
}

static int
in_addr_range_list_add0(struct in_addr_range **list, u_int32_t addr,
    u_int32_t mask)
{
	struct in_addr_range *ent;
	struct in_addr_range **cur;

	if ((ent = in_addr_range_create()) == NULL)
		return -1;
	ent->addr = addr;
	ent->mask = mask;

	for (cur = list; *cur != NULL; cur = &(*cur)->next) {
		if ((ent->addr & (*cur)->mask) ==
		    ((*cur)->addr & (*cur)->mask)) {
			in_addr_range_destroy(ent);
			in_addr_range_list_uniq(list);
			return 0;
		}
		if ((ent->addr & ent->mask) == ((*cur)->addr & ent->mask)) {
			ent->next = (*cur)->next;
			free(*cur);
			*cur = ent;
			in_addr_range_list_uniq(list);
			return 0;
		}
		if ((*cur)->addr > ent->addr)
			break;
	}
	if (cur != NULL) {
		ent->next = *cur;
		*cur = ent;
		in_addr_range_list_uniq(list);
	}
	return 0;
}

int
in_addr_range_list_add(struct in_addr_range **list, const char *str)
{
	int is_range = 0, is_masklen = 0, is_maskaddr = 0, mask;
	char *p0, *p1;
	struct in_addr a0, a1;
	u_int32_t i0, i1;

	if ((p0 = strdup(str)) == NULL) {
#ifdef IIJDEBUG
		saved_errno = errno;
		log_printf(LOG_DL_1, "malloc() failed: %m");
		errno = saved_errno;
#endif
		return -1;
	}
	if ((p1 = strchr(p0, '-')) != NULL) {
		*p1++ = '\0';
		is_range = 1;
	} else if ((p1 = strchr(p0, '/')) != NULL) {
		*p1++ = '\0';

		if (sscanf(p1, "%d", &mask) != 1) {
#ifdef IIJDEBUG
			saved_errno = errno;
			log_printf(LOG_DL_1, "sscanf(%s) failed: %m",
			    p1);
			errno = saved_errno;
#endif
			free(p0);
			return -1;
		}
		if (mask < 0 || 32 < mask) {
#ifdef IIJDEBUG
			log_printf(LOG_DL_1, "must be 0 <= masklen <= 32: %d",
			    mask);
			errno = EINVAL;
#endif
			free(p0);
			return -1;
		}
		is_masklen = 1;
	} else if ((p1 = strchr(p0, ':')) != NULL) {
		*p1++ = '\0';
		is_maskaddr = 1;
	}

	if (inet_pton(AF_INET, p0, &a0) != 1) {
		if (errno == 0)
			errno = EINVAL;
#ifdef IIJDEBUG
		saved_errno = errno;
		log_printf(LOG_DL_1, "inet_pton(%s) failed: %m", p0);
		errno = saved_errno;
#endif
		free(p0);
		return -1;
	}
	if ((is_range || is_maskaddr) && inet_pton(AF_INET, p1, &a1) != 1) {
		if (errno == 0)
			errno = EINVAL;
#ifdef IIJDEBUG
		saved_errno = errno;
		log_printf(LOG_DL_1, "inet_pton(%s) failed: %m", p1);
		errno = saved_errno;
#endif
		free(p0);
		return -1;
	}
	free(p0);
	if (is_range) {
		i0 = ntohl(a0.s_addr);
		i1 = ntohl(a1.s_addr);
		for (; i0 <= i1; i0++)
			in_addr_range_list_add0(list, i0, 0xffffffff);
	} else if (is_masklen) {
		i0 = ntohl(a0.s_addr);
		if (mask == 0)
			i1 = 0x0;
		else
			i1 = 0xffffffffL << (32 - mask);
		if ((i0 & i1) != i0) {
#ifdef IIJDEBUG
			log_printf(LOG_DL_1, "invalid addr/mask pair: "
			    "%d.%d.%d.%d/%d",
			    INT4_CHARS(i0), bitmask2masklen(i1));
#endif
			errno = EINVAL;
			return -1;
		}
		in_addr_range_list_add0(list, i0, i1);
	} else if (is_maskaddr) {
		i0 = ntohl(a0.s_addr);
		i1 = ntohl(a1.s_addr);
		if ((i0 & i1) != i0 || bitmask2masklen(i1) < 0) {
#ifdef IIJDEBUG
			log_printf(LOG_DL_1, "invalid addr/mask pair: "
			    "%d.%d.%d.%d/%d",
			    INT4_CHARS(i0), bitmask2masklen(i1));
#endif
			errno = EINVAL;
			return -1;
		}
		in_addr_range_list_add0(list, i0, i1);
	} else {
		i0 = ntohl(a0.s_addr);
		in_addr_range_list_add0(list, i0, 0xffffffff);
	}

	return 0;
}

static int bitmask2masklen(u_int32_t mask)
{
    switch(mask) {
    case 0x00000000:  return  0;
    case 0x80000000:  return  1;
    case 0xC0000000:  return  2;
    case 0xE0000000:  return  3;
    case 0xF0000000:  return  4;
    case 0xF8000000:  return  5;
    case 0xFC000000:  return  6;
    case 0xFE000000:  return  7;
    case 0xFF000000:  return  8;
    case 0xFF800000:  return  9;
    case 0xFFC00000:  return 10;
    case 0xFFE00000:  return 11;
    case 0xFFF00000:  return 12;
    case 0xFFF80000:  return 13;
    case 0xFFFC0000:  return 14;
    case 0xFFFE0000:  return 15;
    case 0xFFFF0000:  return 16;
    case 0xFFFF8000:  return 17;
    case 0xFFFFC000:  return 18;
    case 0xFFFFE000:  return 19;
    case 0xFFFFF000:  return 20;
    case 0xFFFFF800:  return 21;
    case 0xFFFFFC00:  return 22;
    case 0xFFFFFE00:  return 23;
    case 0xFFFFFF00:  return 24;
    case 0xFFFFFF80:  return 25;
    case 0xFFFFFFC0:  return 26;
    case 0xFFFFFFE0:  return 27;
    case 0xFFFFFFF0:  return 28;
    case 0xFFFFFFF8:  return 29;
    case 0xFFFFFFFC:  return 30;
    case 0xFFFFFFFE:  return 31;
    case 0xFFFFFFFF:  return 32;
    }
    return -1;
}

#ifdef ADDR_RANGE_DEBUG
#include <unistd.h>

static void usage(void);

static void
usage()
{
	fprintf(stderr,
	    "usage: addr_range [-d] [addr_exp]...\n"
	    "\taddr_exp: 192.168.0.1 (equals 192.168.0.1/32) or \n"
	    "\t          192.168.32.1-192.168.32.255 or \n"
	    "\t          192.168.4.0:255.255.254.0 or \n"
	    "\t          10.0.0.1/24\n"
	    );
}

int
main(int argc, char *argv[])
{
	int i, ch;
	struct in_addr_range *list = NULL, *cur;

	debugfp = stderr;
	debuglevel = 0;

	while ((ch = getopt(argc, argv, "d")) != -1) {
		switch (ch) {
		case 'd':
			debuglevel++;
			break;
		default:
			usage();
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc <= 0) {
		usage();
		exit(1);
	}

	for (i = 0; i < argc; i++) {
		if (in_addr_range_list_add(&list, argv[i])) {
			perror(argv[i]);
		}
	}
	for (cur = list; cur != NULL; cur = cur->next) {
		fprintf(stderr, "%d.%d.%d.%d/%d\n"
		    , INT4_CHARS(cur->addr), bitmask2masklen(cur->mask)
		    );
	}
	in_addr_range_list_remove_all(&list);

	return 0;
}
#endif
