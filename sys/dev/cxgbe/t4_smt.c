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
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/eventhandler.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/sbuf.h>
#include <netinet/in.h>

#include "common/common.h"
#include "common/t4_msg.h"
#include "t4_smt.h"

/*
 * Module locking notes:  There is a RW lock protecting the SMAC table as a
 * whole plus a spinlock per SMT entry.  Entry lookups and allocations happen
 * under the protection of the table lock, individual entry changes happen
 * while holding that entry's spinlock.  The table lock nests outside the
 * entry locks.  Allocations of new entries take the table lock as writers so
 * no other lookups can happen while allocating new entries.  Entry updates
 * take the table lock as readers so multiple entries can be updated in
 * parallel.  An SMT entry can be dropped by decrementing its reference count
 * and therefore can happen in parallel with entry allocation but no entry
 * can change state or increment its ref count during allocation as both of
 * these perform lookups.
 *
 * Note: We do not take references to ifnets in this module because both
 * the TOE and the sockets already hold references to the interfaces and the
 * lifetime of an SMT entry is fully contained in the lifetime of the TOE.
 */

/*
 * Allocate a free SMT entry.  Must be called with smt_data.lock held.
 */
struct smt_entry *
t4_find_or_alloc_sme(struct smt_data *s, uint8_t *smac)
{
	struct smt_entry *end, *e;
	struct smt_entry *first_free = NULL;

	rw_assert(&s->lock, RA_WLOCKED);
	for (e = &s->smtab[0], end = &s->smtab[s->smt_size]; e != end; ++e) {
		if (atomic_load_acq_int(&e->refcnt) == 0) {
			if (!first_free)
				first_free = e;
		} else {
			if (e->state == SMT_STATE_SWITCHING) {
				/*
				 * This entry is actually in use. See if we can
				 * re-use it?
				 */
				if (memcmp(e->smac, smac, ETHER_ADDR_LEN) == 0)
					goto found_reuse;
			}
		}
	}
	if (first_free) {
		e = first_free;
		goto found;
	}
	return NULL;

found:
	e->state = SMT_STATE_UNUSED;
found_reuse:
	atomic_add_int(&e->refcnt, 1);
	return e;
}

/*
 * Write an SMT entry.  Must be called with the entry locked.
 */
int
t4_write_sme(struct smt_entry *e)
{
	struct smt_data *s;
	struct sge_wrq *wrq;
	struct adapter *sc;
	struct wrq_cookie cookie;
	struct cpl_smt_write_req *req;
	struct cpl_t6_smt_write_req *t6req;
	u8 row;

	mtx_assert(&e->lock, MA_OWNED);

	MPASS(e->wrq != NULL);
	wrq = e->wrq;
	sc = wrq->adapter;
	MPASS(wrq->adapter != NULL);
	s = sc->smt;


	if (chip_id(sc) <= CHELSIO_T5) {
		/* Source MAC Table (SMT) contains 256 SMAC entries
		 * organized in 128 rows of 2 entries each.
		 */
		req = start_wrq_wr(wrq, howmany(sizeof(*req), 16), &cookie);
		if (req == NULL)
			return (ENOMEM);
		INIT_TP_WR(req, 0);
		/* Each row contains an SMAC pair.
		 * LSB selects the SMAC entry within a row
		 */
		row = (e->idx >> 1);
		if (e->idx & 1) {
			req->pfvf1 = 0x0;
			memcpy(req->src_mac1, e->smac, ETHER_ADDR_LEN);
			/* fill pfvf0/src_mac0 with entry
			 * at prev index from smt-tab.
			 */
			req->pfvf0 = 0x0;
			memcpy(req->src_mac0, s->smtab[e->idx - 1].smac,
					ETHER_ADDR_LEN);
		} else {
			req->pfvf0 = 0x0;
			memcpy(req->src_mac0, e->smac, ETHER_ADDR_LEN);
			/* fill pfvf1/src_mac1 with entry
			 * at next index from smt-tab
			 */
			req->pfvf1 = 0x0;
			memcpy(req->src_mac1, s->smtab[e->idx + 1].smac,
					ETHER_ADDR_LEN);
		}
	} else {
		/* Source MAC Table (SMT) contains 256 SMAC entries */
		t6req = start_wrq_wr(wrq, howmany(sizeof(*t6req), 16), &cookie);
		if (t6req == NULL)
			return (ENOMEM);
		INIT_TP_WR(t6req, 0);
		req = (struct cpl_smt_write_req *)t6req;

		/* fill pfvf0/src_mac0 from smt-tab */
		req->pfvf0 = 0x0;
		memcpy(req->src_mac0, s->smtab[e->idx].smac, ETHER_ADDR_LEN);
		row = e->idx;
	}
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SMT_WRITE_REQ, e->idx |
					V_TID_QID(e->iqid)));
	req->params = htonl(V_SMTW_NORPL(0) |
			V_SMTW_IDX(row) |
			V_SMTW_OVLAN_IDX(0));

	commit_wrq_wr(wrq, req, &cookie);

	return (0);
}

/*
 * Allocate an SMT entry for use by a switching rule.
 */
struct smt_entry *
t4_smt_alloc_switching(struct smt_data *s, uint8_t *smac)
{
	struct smt_entry *e;

	MPASS(s != NULL);
	rw_wlock(&s->lock);
	e = t4_find_or_alloc_sme(s, smac);
	rw_wunlock(&s->lock);
	return e;
}

/*
 * Sets/updates the contents of a switching SMT entry that has been allocated
 * with an earlier call to @t4_smt_alloc_switching.
 */
int
t4_smt_set_switching(struct adapter *sc, struct smt_entry *e, uint16_t pfvf,
								uint8_t *smac)
{
	int rc = 0;

	if (atomic_load_acq_int(&e->refcnt) == 1) {
		/* Setup the entry for the first time */
		mtx_lock(&e->lock);
		e->wrq = &sc->sge.ctrlq[0];
		e->iqid = sc->sge.fwq.abs_id;
		e->pfvf =  pfvf;
		e->state = SMT_STATE_SWITCHING;
		memcpy(e->smac, smac, ETHER_ADDR_LEN);
		rc = t4_write_sme(e);
		mtx_unlock(&e->lock);
	}

	return (rc);
}

int
t4_init_smt(struct adapter *sc, int flags)
{
	int i, smt_size;
	struct smt_data *s;

	smt_size = SMT_SIZE;
	s = malloc(sizeof(*s) + smt_size * sizeof (struct smt_entry), M_CXGBE,
	    M_ZERO | flags);
	if (!s)
		return (ENOMEM);

	s->smt_size = smt_size;
	rw_init(&s->lock, "SMT");

	for (i = 0; i < smt_size; i++) {
		struct smt_entry *e = &s->smtab[i];

		e->idx = i;
		e->state = SMT_STATE_UNUSED;
		mtx_init(&e->lock, "SMT_E", NULL, MTX_DEF);
		atomic_store_rel_int(&e->refcnt, 0);
	}

	sc->smt = s;

	return (0);
}

int
t4_free_smt(struct smt_data *s)
{
	int i;

	for (i = 0; i < s->smt_size; i++)
		mtx_destroy(&s->smtab[i].lock);
	rw_destroy(&s->lock);
	free(s, M_CXGBE);

	return (0);
}

int
do_smt_write_rpl(struct sge_iq *iq, const struct rss_header *rss,
		struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_smt_write_rpl *rpl = (const void *)(rss + 1);
	unsigned int tid = GET_TID(rpl);
	unsigned int smtidx = G_TID_TID(tid);

	if (__predict_false(rpl->status != CPL_ERR_NONE)) {
		struct smt_entry *e = &sc->smt->smtab[smtidx];
		log(LOG_ERR,
		    "Unexpected SMT_WRITE_RPL (%u) for entry at hw_idx %u\n",
		    rpl->status, smtidx);
		mtx_lock(&e->lock);
		e->state = SMT_STATE_ERROR;
		mtx_unlock(&e->lock);
		return (EINVAL);
	}

	return (0);
}

static char
smt_state(const struct smt_entry *e)
{
	switch (e->state) {
	case SMT_STATE_SWITCHING: return 'X';
	case SMT_STATE_ERROR: return 'E';
	default: return 'U';
	}
}

int
sysctl_smt(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct smt_data *smt = sc->smt;
	struct smt_entry *e;
	struct sbuf *sb;
	int rc, i, header = 0;

	if (smt == NULL)
		return (ENXIO);

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, SMT_SIZE, req);
	if (sb == NULL)
		return (ENOMEM);

	e = &smt->smtab[0];
	for (i = 0; i < smt->smt_size; i++, e++) {
		mtx_lock(&e->lock);
		if (e->state == SMT_STATE_UNUSED)
			goto skip;

		if (header == 0) {
			sbuf_printf(sb, " Idx "
			    "Ethernet address  State Users");
			header = 1;
		}
		sbuf_printf(sb, "\n%4u %02x:%02x:%02x:%02x:%02x:%02x "
			   "%c   %5u",
			   e->idx, e->smac[0], e->smac[1], e->smac[2],
			   e->smac[3], e->smac[4], e->smac[5],
			   smt_state(e), atomic_load_acq_int(&e->refcnt));
skip:
		mtx_unlock(&e->lock);
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}
