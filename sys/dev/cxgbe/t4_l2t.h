/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Chelsio Communications, Inc.
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

#ifndef __T4_L2T_H
#define __T4_L2T_H

/* identifies sync vs async L2T_WRITE_REQs */
#define S_SYNC_WR    12
#define V_SYNC_WR(x) ((x) << S_SYNC_WR)
#define F_SYNC_WR    V_SYNC_WR(1)

enum { L2T_SIZE = 4096 };     /* # of L2T entries */

enum {
	L2T_STATE_VALID,	/* entry is up to date */
	L2T_STATE_STALE,	/* entry may be used but needs revalidation */
	L2T_STATE_RESOLVING,	/* entry needs address resolution */
	L2T_STATE_FAILED,	/* failed to resolve */
	L2T_STATE_SYNC_WRITE,	/* synchronous write of entry underway */

	/* when state is one of the below the entry is not hashed */
	L2T_STATE_SWITCHING,	/* entry is being used by a switching filter */
	L2T_STATE_UNUSED	/* entry not in use */
};

/*
 * Each L2T entry plays multiple roles.  First of all, it keeps state for the
 * corresponding entry of the HW L2 table and maintains a queue of offload
 * packets awaiting address resolution.  Second, it is a node of a hash table
 * chain, where the nodes of the chain are linked together through their next
 * pointer.  Finally, each node is a bucket of a hash table, pointing to the
 * first element in its chain through its first pointer.
 */
struct l2t_entry {
	uint16_t state;			/* entry state */
	uint16_t idx;			/* entry index */
	uint32_t addr[4];		/* next hop IP or IPv6 address */
	uint32_t iqid;			/* iqid for reply to write_l2e */
	struct sge_wrq *wrq;		/* queue to use for write_l2e */
	struct ifnet *ifp;		/* outgoing interface */
	uint16_t smt_idx;		/* SMT index */
	uint16_t vlan;			/* VLAN TCI (id: 0-11, prio: 13-15) */
	struct l2t_entry *first;	/* start of hash chain */
	struct l2t_entry *next;		/* next l2t_entry on chain */
	STAILQ_HEAD(, wrqe) wr_list;	/* list of WRs awaiting resolution */
	struct mtx lock;
	volatile int refcnt;		/* entry reference count */
	uint16_t hash;			/* hash bucket the entry is on */
	uint8_t ipv6;			/* entry is for an IPv6 address */
	uint8_t lport;			/* associated offload logical port */
	uint8_t dmac[ETHER_ADDR_LEN];	/* next hop's MAC address */
};

struct l2t_data {
	struct rwlock lock;
	u_int l2t_size;
	volatile int nfree;	/* number of free entries */
	struct l2t_entry *rover;/* starting point for next allocation */
	struct l2t_entry l2tab[];
};


int t4_init_l2t(struct adapter *, int);
int t4_free_l2t(struct l2t_data *);
struct l2t_entry *t4_alloc_l2e(struct l2t_data *);
struct l2t_entry *t4_l2t_alloc_switching(struct adapter *, uint16_t, uint8_t,
    uint8_t *);
int t4_l2t_set_switching(struct adapter *, struct l2t_entry *, uint16_t,
    uint8_t, uint8_t *);
int t4_write_l2e(struct l2t_entry *, int);
int do_l2t_write_rpl(struct sge_iq *, const struct rss_header *, struct mbuf *);

static inline void
t4_l2t_release(struct l2t_entry *e)
{
	struct l2t_data *d = __containerof(e, struct l2t_data, l2tab[e->idx]);

	if (atomic_fetchadd_int(&e->refcnt, -1) == 1)
		atomic_add_int(&d->nfree, 1);
}

int sysctl_l2t(SYSCTL_HANDLER_ARGS);

#endif  /* __T4_L2T_H */
