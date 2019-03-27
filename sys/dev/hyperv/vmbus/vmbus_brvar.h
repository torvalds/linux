/*-
 * Copyright (c) 2009-2012,2016 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VMBUS_BRVAR_H_
#define _VMBUS_BRVAR_H_

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/_iovec.h>

struct vmbus_br {
	struct vmbus_bufring	*vbr;
	uint32_t		vbr_dsize;	/* total data size */
};

#define vbr_windex		vbr->br_windex
#define vbr_rindex		vbr->br_rindex
#define vbr_imask		vbr->br_imask
#define vbr_data		vbr->br_data

struct vmbus_rxbr {
	struct mtx		rxbr_lock;
	struct vmbus_br		rxbr;
};

#define rxbr_windex		rxbr.vbr_windex
#define rxbr_rindex		rxbr.vbr_rindex
#define rxbr_imask		rxbr.vbr_imask
#define rxbr_data		rxbr.vbr_data
#define rxbr_dsize		rxbr.vbr_dsize

struct vmbus_txbr {
	struct mtx		txbr_lock;
	struct vmbus_br		txbr;
};

#define txbr_windex		txbr.vbr_windex
#define txbr_rindex		txbr.vbr_rindex
#define txbr_imask		txbr.vbr_imask
#define txbr_data		txbr.vbr_data
#define txbr_dsize		txbr.vbr_dsize

struct sysctl_ctx_list;
struct sysctl_oid;

static __inline int
vmbus_txbr_maxpktsz(const struct vmbus_txbr *tbr)
{

	/*
	 * - 64 bits for the trailing start index (- sizeof(uint64_t)).
	 * - The rindex and windex can't be same (- 1).  See
	 *   the comment near vmbus_bufring.br_{r,w}index.
	 */
	return (tbr->txbr_dsize - sizeof(uint64_t) - 1);
}

static __inline bool
vmbus_txbr_empty(const struct vmbus_txbr *tbr)
{

	return (tbr->txbr_windex == tbr->txbr_rindex ? true : false);
}

static __inline bool
vmbus_rxbr_empty(const struct vmbus_rxbr *rbr)
{

	return (rbr->rxbr_windex == rbr->rxbr_rindex ? true : false);
}

static __inline int
vmbus_br_nelem(int br_size, int elem_size)
{

	/* Strip bufring header */
	br_size -= sizeof(struct vmbus_bufring);
	/* Add per-element trailing index */
	elem_size += sizeof(uint64_t);
	return (br_size / elem_size);
}

void		vmbus_br_sysctl_create(struct sysctl_ctx_list *ctx,
		    struct sysctl_oid *br_tree, struct vmbus_br *br,
		    const char *name);

void		vmbus_rxbr_init(struct vmbus_rxbr *rbr);
void		vmbus_rxbr_deinit(struct vmbus_rxbr *rbr);
void		vmbus_rxbr_setup(struct vmbus_rxbr *rbr, void *buf, int blen);
int		vmbus_rxbr_peek(struct vmbus_rxbr *rbr, void *data, int dlen);
int		vmbus_rxbr_read(struct vmbus_rxbr *rbr, void *data, int dlen,
		    uint32_t skip);
void		vmbus_rxbr_intr_mask(struct vmbus_rxbr *rbr);
uint32_t	vmbus_rxbr_intr_unmask(struct vmbus_rxbr *rbr);

void		vmbus_txbr_init(struct vmbus_txbr *tbr);
void		vmbus_txbr_deinit(struct vmbus_txbr *tbr);
void		vmbus_txbr_setup(struct vmbus_txbr *tbr, void *buf, int blen);
int		vmbus_txbr_write(struct vmbus_txbr *tbr,
		    const struct iovec iov[], int iovlen, boolean_t *need_sig);

#endif  /* _VMBUS_BRVAR_H_ */
