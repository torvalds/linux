/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 Seccuris Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract to
 * Seccuris Inc.
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
 *
 * Copyright (c) 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
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
 *
 *      @(#)bpf.c	8.4 (Berkeley) 1/9/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bpf.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/bpf.h>
#include <net/bpf_buffer.h>
#include <net/bpfdesc.h>

/*
 * Implement historical kernel memory buffering model for BPF: two malloc(9)
 * kernel buffers are hung off of the descriptor.  The size is fixed prior to
 * attaching to an ifnet, ad cannot be changed after that.  read(2) simply
 * copies the data to user space using uiomove(9).
 */

static int bpf_bufsize = 4096;
SYSCTL_INT(_net_bpf, OID_AUTO, bufsize, CTLFLAG_RW,
    &bpf_bufsize, 0, "Default capture buffer size in bytes");
static int bpf_maxbufsize = BPF_MAXBUFSIZE;
SYSCTL_INT(_net_bpf, OID_AUTO, maxbufsize, CTLFLAG_RW,
    &bpf_maxbufsize, 0, "Maximum capture buffer in bytes");

/*
 * Simple data copy to the current kernel buffer.
 */
void
bpf_buffer_append_bytes(struct bpf_d *d, caddr_t buf, u_int offset,
    void *src, u_int len)
{
	u_char *src_bytes;

	src_bytes = (u_char *)src;
	bcopy(src_bytes, buf + offset, len);
}

/*
 * Scatter-gather data copy from an mbuf chain to the current kernel buffer.
 */
void
bpf_buffer_append_mbuf(struct bpf_d *d, caddr_t buf, u_int offset, void *src,
    u_int len)
{
	const struct mbuf *m;
	u_char *dst;
	u_int count;

	m = (struct mbuf *)src;
	dst = (u_char *)buf + offset;
	while (len > 0) {
		if (m == NULL)
			panic("bpf_mcopy");
		count = min(m->m_len, len);
		bcopy(mtod(m, void *), dst, count);
		m = m->m_next;
		dst += count;
		len -= count;
	}
}

/*
 * Free BPF kernel buffers on device close.
 */
void
bpf_buffer_free(struct bpf_d *d)
{

	if (d->bd_sbuf != NULL)
		free(d->bd_sbuf, M_BPF);
	if (d->bd_hbuf != NULL)
		free(d->bd_hbuf, M_BPF);
	if (d->bd_fbuf != NULL)
		free(d->bd_fbuf, M_BPF);

#ifdef INVARIANTS
	d->bd_sbuf = d->bd_hbuf = d->bd_fbuf = (caddr_t)~0;
#endif
}

/*
 * This is a historical initialization that occurs when the BPF descriptor is
 * first opened.  It does not imply selection of a buffer mode, so we don't
 * allocate buffers here.
 */
void
bpf_buffer_init(struct bpf_d *d)
{

	d->bd_bufsize = bpf_bufsize;
}

/*
 * Allocate or resize buffers.
 */
int
bpf_buffer_ioctl_sblen(struct bpf_d *d, u_int *i)
{
	u_int size;
	caddr_t fbuf, sbuf;

	size = *i;
	if (size > bpf_maxbufsize)
		*i = size = bpf_maxbufsize;
	else if (size < BPF_MINBUFSIZE)
		*i = size = BPF_MINBUFSIZE;

	/* Allocate buffers immediately */
	fbuf = (caddr_t)malloc(size, M_BPF, M_WAITOK);
	sbuf = (caddr_t)malloc(size, M_BPF, M_WAITOK);

	BPFD_LOCK(d);
	if (d->bd_bif != NULL) {
		/* Interface already attached, unable to change buffers */
		BPFD_UNLOCK(d);
		free(fbuf, M_BPF);
		free(sbuf, M_BPF);
		return (EINVAL);
	}

	/* Free old buffers if set */
	if (d->bd_fbuf != NULL)
		free(d->bd_fbuf, M_BPF);
	if (d->bd_sbuf != NULL)
		free(d->bd_sbuf, M_BPF);

	/* Fill in new data */
	d->bd_bufsize = size;
	d->bd_fbuf = fbuf;
	d->bd_sbuf = sbuf;

	d->bd_hbuf = NULL;
	d->bd_slen = 0;
	d->bd_hlen = 0;

	BPFD_UNLOCK(d);
	return (0);
}

/*
 * Copy buffer storage to user space in read().
 */
int
bpf_buffer_uiomove(struct bpf_d *d, caddr_t buf, u_int len, struct uio *uio)
{

	return (uiomove(buf, len, uio));
}
