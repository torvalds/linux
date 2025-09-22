/*	$OpenBSD: pf.c,v 1.2 2024/07/10 20:33:31 martijn Exp $	*/

/*
 * Copyright (c) 2012 Joel Knight <joel@openbsd.org>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/pfvar.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <event.h>

#include "snmpd.h"

int	 devpf = 0;

size_t 	 buf_esize[PFRB_MAX] = { 0,
	sizeof(struct pfr_table), sizeof(struct pfr_tstats),
	sizeof(struct pfr_addr), sizeof(struct pfr_astats),
	sizeof(struct pfi_kif), sizeof(struct pfioc_trans_e)
};

void
pf_init(void)
{
	if ((devpf = open("/dev/pf", O_RDONLY)) == -1)
		fatal("pf_init");
}

int
pf_get_stats(struct pf_status *s)
{
	extern int	 devpf;

	memset(s, 0, sizeof(*s));
	if (ioctl(devpf, DIOCGETSTATUS, s) == -1) {
		log_warn("DIOCGETSTATUS");
		return (-1);
	}

	return (0);
}

int
pfr_get_astats(struct pfr_table *tbl, struct pfr_astats *addr, int *size,
		int flags)
{
	struct pfioc_table	 io;
	extern int		 devpf;

	if (tbl == NULL || size == NULL || *size < 0 ||
	    (*size && addr == NULL)) 
		return (-1);

	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	io.pfrio_table = *tbl;
	io.pfrio_buffer = addr;
	io.pfrio_esize = sizeof(*addr);
	io.pfrio_size = *size;
	if (ioctl(devpf, DIOCRGETASTATS, &io) == -1) 
		return (-1);
	*size = io.pfrio_size;
	return (0);
}

int
pfr_get_tstats(struct pfr_table *filter, struct pfr_tstats *tbl, int *size,
	int flags)
{
	struct pfioc_table	 io;
	extern int		 devpf;

	if (size == NULL || *size < 0 || (*size && tbl == NULL))
		return (-1);
	bzero(&io, sizeof io);
	io.pfrio_flags = flags;
	if (filter != NULL)
		io.pfrio_table = *filter;
	io.pfrio_buffer = tbl;
	io.pfrio_esize = sizeof(*tbl);
	io.pfrio_size = *size;
	if (ioctl(devpf, DIOCRGETTSTATS, &io) == -1)
		return (-1);
	*size = io.pfrio_size;
	return (0);
}

int
pfr_buf_grow(struct pfr_buffer *b, int minsize)
{
	caddr_t	 p;
	size_t 	 bs;

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
		if (minsize < 0 || (size_t)minsize >= SIZE_MAX / bs) {
			/* msize overflow */
			return (-1);
		}
		p = reallocarray(b->pfrb_caddr, minsize, bs);
		if (p == NULL)
			return (-1);
		bzero(p + b->pfrb_msize * bs, (minsize - b->pfrb_msize) * bs);
		b->pfrb_caddr = p;
		b->pfrb_msize = minsize;
	}
	return (0);
}

const void *
pfr_buf_next(struct pfr_buffer *b, const void *prev)
{
	size_t	 bs;

	if (b == NULL)
		return (NULL);
	if (b->pfrb_size == 0)
		return (NULL);
	if (prev == NULL) 
		return (b->pfrb_caddr);
	bs = buf_esize[b->pfrb_type];
	if ((((const char *)prev)-((char *)b->pfrb_caddr)) / bs >=
	    (size_t)b->pfrb_size-1)
		return (NULL);

	return (((const char *)prev) + bs);
}

int
pfi_get_ifaces(const char *filter, struct pfi_kif *buf, int *size)
{
	struct pfioc_iface	 io;
	extern int		 devpf;

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
	if (ioctl(devpf, DIOCIGETIFACES, &io) == -1)
		return (-1);
	*size = io.pfiio_size;
	return (0);
}

int
pfi_get(struct pfr_buffer *b, const char *filter)
{
	bzero(b, sizeof(struct pfr_buffer));
	b->pfrb_type = PFRB_IFACES;
	for (;;) {
		pfr_buf_grow(b, 0);
		b->pfrb_size = b->pfrb_msize;
		if (pfi_get_ifaces(filter, b->pfrb_caddr, &(b->pfrb_size)))
			return (1);
		if (b->pfrb_size < b->pfrb_msize)
			break;
	}

	return (0);
}

int
pfi_count(void)
{
	struct pfr_buffer 	 b;
	const struct pfi_kif 	*p;
	int			 c = 0;

	if (pfi_get(&b, NULL)) {
		free(b.pfrb_caddr);
		return (-1);
	}

	PFRB_FOREACH(p, &b)
		c++;

	free(b.pfrb_caddr);
	return (c);
}

int
pfi_get_if(struct pfi_kif *rp, int idx)
{
	struct pfr_buffer	 b;
	const struct pfi_kif	*p;
	int			 i = 1;

	if (pfi_get(&b, NULL)) {
		free(b.pfrb_caddr);
		return (-1);
	}

	PFRB_FOREACH(p, &b) {
		if (i == idx)
			break;
		i++;
	}

	if (p == NULL) {
		free(b.pfrb_caddr);
		return (-1);
	}

	bcopy(p, rp, sizeof(struct pfi_kif));
	free(b.pfrb_caddr);

	return (0);
}

int
pft_get(struct pfr_buffer *b, struct pfr_table *filter)
{
	bzero(b, sizeof(struct pfr_buffer));
	b->pfrb_type = PFRB_TSTATS;
	
	for (;;) {
		pfr_buf_grow(b, b->pfrb_size);
		b->pfrb_size = b->pfrb_msize;
		if (pfr_get_tstats(filter, b->pfrb_caddr, &(b->pfrb_size), 0))
			return (1);
		if (b->pfrb_size <= b->pfrb_msize)
			break;
	}

	return (0);
}

int
pft_get_table(struct pfr_tstats *rts, int idx)
{
	struct pfr_buffer	 b;
	const struct pfr_tstats	*ts;
	int			 i = 1;

	if (pft_get(&b, NULL)) {
		free(b.pfrb_caddr);
		return (-1);
	}
 
	PFRB_FOREACH(ts, &b) {
		if (!(ts->pfrts_flags & PFR_TFLAG_ACTIVE))
			continue;
		if (i == idx)
			break;
		i++;
	}

	if (ts == NULL) {
		free(b.pfrb_caddr);
		return (-1);
	}

	bcopy(ts, rts, sizeof(struct pfr_tstats));
	free(b.pfrb_caddr);

	return (0);
}

int
pft_count(void)
{
	struct pfr_buffer	 b;
	const struct pfr_tstats	*ts;
	int			 c = 0;

	if (pft_get(&b, NULL)) {
		free(b.pfrb_caddr);
		return (-1);
	}

	PFRB_FOREACH(ts, &b) {
		if (!(ts->pfrts_flags & PFR_TFLAG_ACTIVE))
			continue;
		c++;
	}

	free(b.pfrb_caddr);
	return (c);
}

int
pfta_get(struct pfr_buffer *b, struct pfr_table *filter)
{
	bzero(b, sizeof(struct pfr_buffer));
	b->pfrb_type = PFRB_ASTATS;

	for (;;) {
		pfr_buf_grow(b, b->pfrb_size);
		b->pfrb_size = b->pfrb_msize;
		if (pfr_get_astats(filter, b->pfrb_caddr, &(b->pfrb_size), 0)) {
			return (1);
		}
		if (b->pfrb_size <= b->pfrb_msize)
			break;
	}
	
	return (0);
}

int
pfta_get_addr(struct pfr_astats *ras, int tblidx)
{
	struct pfr_buffer	 ba;
	struct pfr_tstats	 ts;
	struct pfr_table	 filter;
	const struct pfr_astats	*as;

	if (pft_get_table(&ts, tblidx))
		return (-1);

	bzero(&filter, sizeof(filter));
	if (strlcpy(filter.pfrt_name, ts.pfrts_name,
	    sizeof(filter.pfrt_name)) >= sizeof(filter.pfrt_name)) {
		return (-1);
	}

	if (pfta_get(&ba, &filter) || ba.pfrb_size == 0) {
		free(ba.pfrb_caddr);
		return (-1);
	}

	PFRB_FOREACH(as, &ba) {
		if (as->pfras_a.pfra_af != AF_INET)
			continue;
		if ((memcmp(&as->pfras_a.pfra_ip4addr, &ras->pfras_a.pfra_ip4addr,
		    sizeof(as->pfras_a.pfra_ip4addr)) == 0)
		    && (as->pfras_a.pfra_net == ras->pfras_a.pfra_net))
			break;
	}

	if (as == NULL) {
		free(ba.pfrb_caddr);
		return (-1);
	}

	bcopy(as, ras, sizeof(struct pfr_astats));
	free(ba.pfrb_caddr);

	return (0);
}

int
pfta_get_nextaddr(struct pfr_astats *ras, int *tblidx)
{
	struct pfr_buffer	 ba;
	struct pfr_tstats	 ts;
	struct pfr_table	 filter;
	const struct pfr_astats	*as;
	int			 i, found = 0, cmp;

	ba.pfrb_caddr = NULL;

	for (i = *tblidx; !pft_get_table(&ts, i); i++) {
		bzero(&filter, sizeof(filter));
		if (strlcpy(filter.pfrt_name, ts.pfrts_name,
		    sizeof(filter.pfrt_name)) >= sizeof(filter.pfrt_name))
			goto fail;

		if (pfta_get(&ba, &filter) || ba.pfrb_size == 0)
			goto fail;

		PFRB_FOREACH(as, &ba) {
			if (as->pfras_a.pfra_af != AF_INET)
				continue;
			if (found)
				goto found;
			cmp = memcmp(&as->pfras_a.pfra_ip4addr,
			    &ras->pfras_a.pfra_ip4addr,
			    sizeof(as->pfras_a.pfra_ip4addr));
			if (cmp == 0) {
				if (as->pfras_a.pfra_net ==
				    ras->pfras_a.pfra_net)
					found = 1;
				if (as->pfras_a.pfra_net >
				    ras->pfras_a.pfra_net)
					goto found;
			} else if (cmp > 0)
				goto found;
		}

		free(ba.pfrb_caddr);
		ba.pfrb_caddr = NULL;
	}


 fail:
	free(ba.pfrb_caddr);

	return (-1);

 found:
	bcopy(as, ras, sizeof(struct pfr_astats));
	*tblidx = i;

	free(ba.pfrb_caddr);

	return (0);
}

int
pfta_get_first(struct pfr_astats *ras)
{
	struct pfr_buffer	 ba;
	struct pfr_tstats	 ts;
	struct pfr_table	 filter;
	const struct pfr_astats	*as;

	if (pft_get_table(&ts, 1))
		return (-1);

	bzero(&filter, sizeof(filter));
	if (strlcpy(filter.pfrt_name, ts.pfrts_name,
	    sizeof(filter.pfrt_name)) >= sizeof(filter.pfrt_name)) {
		return (-1);
	}

	if (pfta_get(&ba, &filter) || ba.pfrb_size == 0) {
		free(ba.pfrb_caddr);
		return (-1);
	}

	/* take the first AF_INET addr */
	PFRB_FOREACH(as, &ba) {
		if (as->pfras_a.pfra_af != AF_INET)
			continue;
		break;
	}

	if (as == NULL) {
		free(ba.pfrb_caddr);
		return (-1);
	}

	bcopy(as, ras, sizeof(struct pfr_astats));
	free(ba.pfrb_caddr);

	return (0);
}

