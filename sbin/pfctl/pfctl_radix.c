/*	$OpenBSD: pfctl_radix.c,v 1.27 2005/05/21 21:03:58 henning Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2002 Cedric Berger
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/pfvar.h>

#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <err.h>

#include "pfctl.h"

#define BUF_SIZE 256

extern int dev;

static int	 pfr_next_token(char buf[], FILE *);


int
pfr_clr_tables(struct pfr_table *filter, int *ndel, int flags)
{
	struct pfioc_table io;

	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	if (filter != NULL)
		io.pfrio_table = *filter;
	if (ioctl(dev, DIOCRCLRTABLES, &io))
		return (-1);
	if (ndel != NULL)
		*ndel = io.pfrio_ndel;
	return (0);
}

int
pfr_add_tables(struct pfr_table *tbl, int size, int *nadd, int flags)
{
	struct pfioc_table io;

	if (size < 0 || (size && tbl == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_buffer = tbl;
	io.pfrio_esize = sizeof(*tbl);
	io.pfrio_size = size;
	if (ioctl(dev, DIOCRADDTABLES, &io))
		return (-1);
	if (nadd != NULL)
		*nadd = io.pfrio_nadd;
	return (0);
}

int
pfr_del_tables(struct pfr_table *tbl, int size, int *ndel, int flags)
{
	struct pfioc_table io;

	if (size < 0 || (size && tbl == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_buffer = tbl;
	io.pfrio_esize = sizeof(*tbl);
	io.pfrio_size = size;
	if (ioctl(dev, DIOCRDELTABLES, &io))
		return (-1);
	if (ndel != NULL)
		*ndel = io.pfrio_ndel;
	return (0);
}

int
pfr_get_tables(struct pfr_table *filter, struct pfr_table *tbl, int *size,
	int flags)
{
	struct pfioc_table io;

	if (size == NULL || *size < 0 || (*size && tbl == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	if (filter != NULL)
		io.pfrio_table = *filter;
	io.pfrio_buffer = tbl;
	io.pfrio_esize = sizeof(*tbl);
	io.pfrio_size = *size;
	if (ioctl(dev, DIOCRGETTABLES, &io))
		return (-1);
	*size = io.pfrio_size;
	return (0);
}

int
pfr_get_tstats(struct pfr_table *filter, struct pfr_tstats *tbl, int *size,
	int flags)
{
	struct pfioc_table io;

	if (size == NULL || *size < 0 || (*size && tbl == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	if (filter != NULL)
		io.pfrio_table = *filter;
	io.pfrio_buffer = tbl;
	io.pfrio_esize = sizeof(*tbl);
	io.pfrio_size = *size;
	if (ioctl(dev, DIOCRGETTSTATS, &io))
		return (-1);
	*size = io.pfrio_size;
	return (0);
}

int
pfr_clr_addrs(struct pfr_table *tbl, int *ndel, int flags)
{
	struct pfioc_table io;

	if (tbl == NULL) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_table = *tbl;
	if (ioctl(dev, DIOCRCLRADDRS, &io))
		return (-1);
	if (ndel != NULL)
		*ndel = io.pfrio_ndel;
	return (0);
}

int
pfr_add_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int size,
    int *nadd, int flags)
{
	struct pfioc_table io;

	if (tbl == NULL || size < 0 || (size && addr == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_table = *tbl;
	io.pfrio_buffer = addr;
	io.pfrio_esize = sizeof(*addr);
	io.pfrio_size = size;
	if (ioctl(dev, DIOCRADDADDRS, &io))
		return (-1);
	if (nadd != NULL)
		*nadd = io.pfrio_nadd;
	return (0);
}

int
pfr_del_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int size,
    int *ndel, int flags)
{
	struct pfioc_table io;

	if (tbl == NULL || size < 0 || (size && addr == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_table = *tbl;
	io.pfrio_buffer = addr;
	io.pfrio_esize = sizeof(*addr);
	io.pfrio_size = size;
	if (ioctl(dev, DIOCRDELADDRS, &io))
		return (-1);
	if (ndel != NULL)
		*ndel = io.pfrio_ndel;
	return (0);
}

int
pfr_set_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int size,
    int *size2, int *nadd, int *ndel, int *nchange, int flags)
{
	struct pfioc_table io;

	if (tbl == NULL || size < 0 || (size && addr == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_table = *tbl;
	io.pfrio_buffer = addr;
	io.pfrio_esize = sizeof(*addr);
	io.pfrio_size = size;
	io.pfrio_size2 = (size2 != NULL) ? *size2 : 0;
	if (ioctl(dev, DIOCRSETADDRS, &io))
		return (-1);
	if (nadd != NULL)
		*nadd = io.pfrio_nadd;
	if (ndel != NULL)
		*ndel = io.pfrio_ndel;
	if (nchange != NULL)
		*nchange = io.pfrio_nchange;
	if (size2 != NULL)
		*size2 = io.pfrio_size2;
	return (0);
}

int
pfr_get_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int *size,
    int flags)
{
	struct pfioc_table io;

	if (tbl == NULL || size == NULL || *size < 0 ||
	    (*size && addr == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_table = *tbl;
	io.pfrio_buffer = addr;
	io.pfrio_esize = sizeof(*addr);
	io.pfrio_size = *size;
	if (ioctl(dev, DIOCRGETADDRS, &io))
		return (-1);
	*size = io.pfrio_size;
	return (0);
}

int
pfr_get_astats(struct pfr_table *tbl, struct pfr_astats *addr, int *size,
    int flags)
{
	struct pfioc_table io;

	if (tbl == NULL || size == NULL || *size < 0 ||
	    (*size && addr == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_table = *tbl;
	io.pfrio_buffer = addr;
	io.pfrio_esize = sizeof(*addr);
	io.pfrio_size = *size;
	if (ioctl(dev, DIOCRGETASTATS, &io))
		return (-1);
	*size = io.pfrio_size;
	return (0);
}

int
pfr_clr_tstats(struct pfr_table *tbl, int size, int *nzero, int flags)
{
	struct pfioc_table io;

	if (size < 0 || (size && !tbl)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_buffer = tbl;
	io.pfrio_esize = sizeof(*tbl);
	io.pfrio_size = size;
	if (ioctl(dev, DIOCRCLRTSTATS, &io))
		return (-1);
	if (nzero)
		*nzero = io.pfrio_nzero;
	return (0);
}

int
pfr_tst_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int size,
    int *nmatch, int flags)
{
	struct pfioc_table io;

	if (tbl == NULL || size < 0 || (size && addr == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_table = *tbl;
	io.pfrio_buffer = addr;
	io.pfrio_esize = sizeof(*addr);
	io.pfrio_size = size;
	if (ioctl(dev, DIOCRTSTADDRS, &io))
		return (-1);
	if (nmatch)
		*nmatch = io.pfrio_nmatch;
	return (0);
}

int
pfr_ina_define(struct pfr_table *tbl, struct pfr_addr *addr, int size,
    int *nadd, int *naddr, int ticket, int flags)
{
	struct pfioc_table io;

	if (tbl == NULL || size < 0 || (size && addr == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_table = *tbl;
	io.pfrio_buffer = addr;
	io.pfrio_esize = sizeof(*addr);
	io.pfrio_size = size;
	io.pfrio_ticket = ticket;
	if (ioctl(dev, DIOCRINADEFINE, &io))
		return (-1);
	if (nadd != NULL)
		*nadd = io.pfrio_nadd;
	if (naddr != NULL)
		*naddr = io.pfrio_naddr;
	return (0);
}

/* interface management code */

int
pfi_get_ifaces(const char *filter, struct pfi_kif *buf, int *size)
{
	struct pfioc_iface io;

	if (size == NULL || *size < 0 || (*size && buf == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	if (filter != NULL)
		if (strlcpy(io.pfiio_name, filter, sizeof(io.pfiio_name)) >=
		    sizeof(io.pfiio_name)) {
			errno = EINVAL;
			return (-1);
		}
	io.pfiio_buffer = buf;
	io.pfiio_esize = sizeof(*buf);
	io.pfiio_size = *size;
	if (ioctl(dev, DIOCIGETIFACES, &io))
		return (-1);
	*size = io.pfiio_size;
	return (0);
}

/* buffer management code */

const size_t buf_esize[PFRB_MAX] = { 0,
	sizeof(struct pfr_table), sizeof(struct pfr_tstats),
	sizeof(struct pfr_addr), sizeof(struct pfr_astats),
	sizeof(struct pfi_kif), sizeof(struct pfioc_trans_e)
};

/*
 * add one element to the buffer
 */
int
pfr_buf_add(struct pfr_buffer *b, const void *e)
{
	size_t bs;

	if (b == NULL || b->pfrb_type <= 0 || b->pfrb_type >= PFRB_MAX ||
	    e == NULL) {
		errno = EINVAL;
		return (-1);
	}
	bs = buf_esize[b->pfrb_type];
	if (b->pfrb_size == b->pfrb_msize)
		if (pfr_buf_grow(b, 0))
			return (-1);
	memcpy(((caddr_t)b->pfrb_caddr) + bs * b->pfrb_size, e, bs);
	b->pfrb_size++;
	return (0);
}

/*
 * return next element of the buffer (or first one if prev is NULL)
 * see PFRB_FOREACH macro
 */
void *
pfr_buf_next(struct pfr_buffer *b, const void *prev)
{
	size_t bs;

	if (b == NULL || b->pfrb_type <= 0 || b->pfrb_type >= PFRB_MAX)
		return (NULL);
	if (b->pfrb_size == 0)
		return (NULL);
	if (prev == NULL)
		return (b->pfrb_caddr);
	bs = buf_esize[b->pfrb_type];
	if ((((caddr_t)prev)-((caddr_t)b->pfrb_caddr)) / bs >= b->pfrb_size-1)
		return (NULL);
	return (((caddr_t)prev) + bs);
}

/*
 * minsize:
 *    0: make the buffer somewhat bigger
 *    n: make room for "n" entries in the buffer
 */
int
pfr_buf_grow(struct pfr_buffer *b, int minsize)
{
	caddr_t p;
	size_t bs;

	if (b == NULL || b->pfrb_type <= 0 || b->pfrb_type >= PFRB_MAX) {
		errno = EINVAL;
		return (-1);
	}
	if (minsize != 0 && minsize <= b->pfrb_msize)
		return (0);
	bs = buf_esize[b->pfrb_type];
	if (!b->pfrb_msize) {
		if (minsize < 64)
			minsize = 64;
		b->pfrb_caddr = calloc(bs, minsize);
		if (b->pfrb_caddr == NULL)
			return (-1);
		b->pfrb_msize = minsize;
	} else {
		if (minsize == 0)
			minsize = b->pfrb_msize * 2;
		if (minsize < 0 || minsize >= SIZE_T_MAX / bs) {
			/* msize overflow */
			errno = ENOMEM;
			return (-1);
		}
		p = realloc(b->pfrb_caddr, minsize * bs);
		if (p == NULL)
			return (-1);
		bzero(p + b->pfrb_msize * bs, (minsize - b->pfrb_msize) * bs);
		b->pfrb_caddr = p;
		b->pfrb_msize = minsize;
	}
	return (0);
}

/*
 * reset buffer and free memory.
 */
void
pfr_buf_clear(struct pfr_buffer *b)
{
	if (b == NULL)
		return;
	if (b->pfrb_caddr != NULL)
		free(b->pfrb_caddr);
	b->pfrb_caddr = NULL;
	b->pfrb_size = b->pfrb_msize = 0;
}

int
pfr_buf_load(struct pfr_buffer *b, char *file, int nonetwork,
    int (*append_addr)(struct pfr_buffer *, char *, int))
{
	FILE	*fp;
	char	 buf[BUF_SIZE];
	int	 rv;

	if (file == NULL)
		return (0);
	if (!strcmp(file, "-"))
		fp = stdin;
	else {
		fp = pfctl_fopen(file, "r");
		if (fp == NULL)
			return (-1);
	}
	while ((rv = pfr_next_token(buf, fp)) == 1)
		if (append_addr(b, buf, nonetwork)) {
			rv = -1;
			break;
		}
	if (fp != stdin)
		fclose(fp);
	return (rv);
}

int
pfr_next_token(char buf[BUF_SIZE], FILE *fp)
{
	static char	next_ch = ' ';
	int		i = 0;

	for (;;) {
		/* skip spaces */
		while (isspace(next_ch) && !feof(fp))
			next_ch = fgetc(fp);
		/* remove from '#' until end of line */
		if (next_ch == '#')
			while (!feof(fp)) {
				next_ch = fgetc(fp);
				if (next_ch == '\n')
					break;
			}
		else
			break;
	}
	if (feof(fp)) {
		next_ch = ' ';
		return (0);
	}
	do {
		if (i < BUF_SIZE)
			buf[i++] = next_ch;
		next_ch = fgetc(fp);
	} while (!feof(fp) && !isspace(next_ch));
	if (i >= BUF_SIZE) {
		errno = EINVAL;
		return (-1);
	}
	buf[i] = '\0';
	return (1);
}

char *
pfr_strerror(int errnum)
{
	switch (errnum) {
	case ESRCH:
		return "Table does not exist";
	case ENOENT:
		return "Anchor or Ruleset does not exist";
	default:
		return strerror(errnum);
	}
}
