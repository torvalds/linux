/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Chelsio Communications, Inc.
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
 *
 * $FreeBSD$
 *
 */

#ifndef __T4_SMT_H
#define __T4_SMT_H

/* identifies sync vs async SMT_WRITE_REQs */
#define S_SYNC_WR    12
#define V_SYNC_WR(x) ((x) << S_SYNC_WR)
#define F_SYNC_WR    V_SYNC_WR(1)

enum { SMT_SIZE = 256 };     /* # of SMT entries */

enum {
	SMT_STATE_SWITCHING,	/* entry is being used by a switching filter */
	SMT_STATE_UNUSED,	/* entry not in use */
	SMT_STATE_ERROR		/* entry is in error state */
};

struct smt_entry {
	uint16_t state;			/* entry state */
	uint16_t idx;			/* entry index */
	uint32_t iqid;                  /* iqid for reply to write_sme */
	struct sge_wrq *wrq;            /* queue to use for write_sme */
	uint16_t pfvf;			/* pfvf number */
	volatile int refcnt;		/* entry reference count */
	uint8_t smac[ETHER_ADDR_LEN];	/* source MAC address */
	struct mtx lock;
};

struct smt_data {
	struct rwlock lock;
	u_int smt_size;
	struct smt_entry smtab[];
};


int t4_init_smt(struct adapter *, int);
int t4_free_smt(struct smt_data *);
struct smt_entry *t4_find_or_alloc_sme(struct smt_data *, uint8_t *);
struct smt_entry *t4_smt_alloc_switching(struct smt_data *, uint8_t *);
int t4_smt_set_switching(struct adapter *, struct smt_entry *,
					uint16_t, uint8_t *);
int t4_write_sme(struct smt_entry *);
int do_smt_write_rpl(struct sge_iq *, const struct rss_header *, struct mbuf *);

static inline void
t4_smt_release(struct smt_entry *e)
{
	MPASS(e != NULL);
	if (atomic_fetchadd_int(&e->refcnt, -1) == 1) {
		mtx_lock(&e->lock);
		e->state = SMT_STATE_UNUSED;
		mtx_unlock(&e->lock);
	}

}

int sysctl_smt(SYSCTL_HANDLER_ARGS);

#endif  /* __T4_SMT_H */
