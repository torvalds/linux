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
#include <sys/fnv_hash.h>
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
#include "common/t4_regs.h"
#include "common/t4_regs_values.h"
#include "common/t4_tcb.h"
#include "t4_l2t.h"
#include "t4_smt.h"

struct filter_entry {
	LIST_ENTRY(filter_entry) link_4t;
	LIST_ENTRY(filter_entry) link_tid;

	uint32_t valid:1;	/* filter allocated and valid */
	uint32_t locked:1;	/* filter is administratively locked or busy */
	uint32_t pending:1;	/* filter action is pending firmware reply */
	int tid;		/* tid of the filter TCB */
	struct l2t_entry *l2te;	/* L2 table entry for DMAC rewrite */
	struct smt_entry *smt;	/* SMT entry for SMAC rewrite */

	struct t4_filter_specification fs;
};

static void free_filter_resources(struct filter_entry *);
static int get_tcamfilter(struct adapter *, struct t4_filter *);
static int get_hashfilter(struct adapter *, struct t4_filter *);
static int set_hashfilter(struct adapter *, struct t4_filter *, uint64_t,
    struct l2t_entry *, struct smt_entry *);
static int del_hashfilter(struct adapter *, struct t4_filter *);
static int configure_hashfilter_tcb(struct adapter *, struct filter_entry *);

static inline bool
separate_hpfilter_region(struct adapter *sc)
{

	return (chip_id(sc) >= CHELSIO_T6);
}

static inline uint32_t
hf_hashfn_4t(struct t4_filter_specification *fs)
{
	struct t4_filter_tuple *ft = &fs->val;
	uint32_t hash;

	if (fs->type) {
		/* IPv6 */
		hash = fnv_32_buf(&ft->sip[0], 16, FNV1_32_INIT);
		hash = fnv_32_buf(&ft->dip[0], 16, hash);
	} else {
		hash = fnv_32_buf(&ft->sip[0], 4, FNV1_32_INIT);
		hash = fnv_32_buf(&ft->dip[0], 4, hash);
	}
	hash = fnv_32_buf(&ft->sport, sizeof(ft->sport), hash);
	hash = fnv_32_buf(&ft->dport, sizeof(ft->dport), hash);

	return (hash);
}

static inline uint32_t
hf_hashfn_tid(int tid)
{

	return (fnv_32_buf(&tid, sizeof(tid), FNV1_32_INIT));
}

static int
alloc_hftid_hash(struct tid_info *t, int flags)
{
	int n;

	MPASS(t->ntids > 0);
	MPASS(t->hftid_hash_4t == NULL);
	MPASS(t->hftid_hash_tid == NULL);

	n = max(t->ntids / 1024, 16);
	t->hftid_hash_4t = hashinit_flags(n, M_CXGBE, &t->hftid_4t_mask, flags);
	if (t->hftid_hash_4t == NULL)
		return (ENOMEM);
	t->hftid_hash_tid = hashinit_flags(n, M_CXGBE, &t->hftid_tid_mask,
	    flags);
	if (t->hftid_hash_tid == NULL) {
		hashdestroy(t->hftid_hash_4t, M_CXGBE, t->hftid_4t_mask);
		t->hftid_hash_4t = NULL;
		return (ENOMEM);
	}

	mtx_init(&t->hftid_lock, "T4 hashfilters", 0, MTX_DEF);
	cv_init(&t->hftid_cv, "t4hfcv");

	return (0);
}

void
free_hftid_hash(struct tid_info *t)
{
	struct filter_entry *f, *ftmp;
	LIST_HEAD(, filter_entry) *head;
	int i;
#ifdef INVARIANTS
	int n = 0;
#endif

	if (t->tids_in_use > 0) {
		/* Remove everything from the tid hash. */
		head = t->hftid_hash_tid;
		for (i = 0; i <= t->hftid_tid_mask; i++) {
			LIST_FOREACH_SAFE(f, &head[i], link_tid, ftmp) {
				LIST_REMOVE(f, link_tid);
			}
		}

		/* Remove and then free each filter in the 4t hash. */
		head = t->hftid_hash_4t;
		for (i = 0; i <= t->hftid_4t_mask; i++) {
			LIST_FOREACH_SAFE(f, &head[i], link_4t, ftmp) {
#ifdef INVARIANTS
				n += f->fs.type ? 2 : 1;
#endif
				LIST_REMOVE(f, link_4t);
				free(f, M_CXGBE);
			}
		}
		MPASS(t->tids_in_use == n);
		t->tids_in_use = 0;
	}

	if (t->hftid_hash_4t) {
		hashdestroy(t->hftid_hash_4t, M_CXGBE, t->hftid_4t_mask);
		t->hftid_hash_4t = NULL;
	}
	if (t->hftid_hash_tid) {
		hashdestroy(t->hftid_hash_tid, M_CXGBE, t->hftid_tid_mask);
		t->hftid_hash_tid = NULL;
	}
	if (mtx_initialized(&t->hftid_lock)) {
		mtx_destroy(&t->hftid_lock);
		cv_destroy(&t->hftid_cv);
	}
}

static void
insert_hf(struct adapter *sc, struct filter_entry *f, uint32_t hash)
{
	struct tid_info *t = &sc->tids;
	LIST_HEAD(, filter_entry) *head = t->hftid_hash_4t;

	MPASS(head != NULL);
	if (hash == 0)
		hash = hf_hashfn_4t(&f->fs);
	LIST_INSERT_HEAD(&head[hash & t->hftid_4t_mask], f, link_4t);
	atomic_add_int(&t->tids_in_use, f->fs.type ? 2 : 1);
}

static void
insert_hftid(struct adapter *sc, struct filter_entry *f)
{
	struct tid_info *t = &sc->tids;
	LIST_HEAD(, filter_entry) *head = t->hftid_hash_tid;
	uint32_t hash;

	MPASS(f->tid >= t->tid_base);
	MPASS(f->tid - t->tid_base < t->ntids);
	mtx_assert(&t->hftid_lock, MA_OWNED);

	hash = hf_hashfn_tid(f->tid);
	LIST_INSERT_HEAD(&head[hash & t->hftid_tid_mask], f, link_tid);
}

static bool
filter_eq(struct t4_filter_specification *fs1,
    struct t4_filter_specification *fs2)
{
	int n;

	MPASS(fs1->hash && fs2->hash);

	if (fs1->type != fs2->type)
		return (false);

	n = fs1->type ? 16 : 4;
	if (bcmp(&fs1->val.sip[0], &fs2->val.sip[0], n) ||
	    bcmp(&fs1->val.dip[0], &fs2->val.dip[0], n) ||
	    fs1->val.sport != fs2->val.sport ||
	    fs1->val.dport != fs2->val.dport)
		return (false);

	/*
	 * We know the masks are the same because all hashfilter masks have to
	 * conform to the global tp->hash_filter_mask and the driver has
	 * verified that already.
	 */

	if ((fs1->mask.pfvf_vld || fs1->mask.ovlan_vld) &&
	    fs1->val.vnic != fs2->val.vnic)
		return (false);
	if (fs1->mask.vlan_vld && fs1->val.vlan != fs2->val.vlan)
		return (false);
	if (fs1->mask.macidx && fs1->val.macidx != fs2->val.macidx)
		return (false);
	if (fs1->mask.frag && fs1->val.frag != fs2->val.frag)
		return (false);
	if (fs1->mask.matchtype && fs1->val.matchtype != fs2->val.matchtype)
		return (false);
	if (fs1->mask.iport && fs1->val.iport != fs2->val.iport)
		return (false);
	if (fs1->mask.fcoe && fs1->val.fcoe != fs2->val.fcoe)
		return (false);
	if (fs1->mask.proto && fs1->val.proto != fs2->val.proto)
		return (false);
	if (fs1->mask.tos && fs1->val.tos != fs2->val.tos)
		return (false);
	if (fs1->mask.ethtype && fs1->val.ethtype != fs2->val.ethtype)
		return (false);

	return (true);
}

static struct filter_entry *
lookup_hf(struct adapter *sc, struct t4_filter_specification *fs, uint32_t hash)
{
	struct tid_info *t = &sc->tids;
	LIST_HEAD(, filter_entry) *head = t->hftid_hash_4t;
	struct filter_entry *f;

	mtx_assert(&t->hftid_lock, MA_OWNED);
	MPASS(head != NULL);

	if (hash == 0)
		hash = hf_hashfn_4t(fs);

	LIST_FOREACH(f, &head[hash & t->hftid_4t_mask], link_4t) {
		if (filter_eq(&f->fs, fs))
			return (f);
	}

	return (NULL);
}

static struct filter_entry *
lookup_hftid(struct adapter *sc, int tid)
{
	struct tid_info *t = &sc->tids;
	LIST_HEAD(, filter_entry) *head = t->hftid_hash_tid;
	struct filter_entry *f;
	uint32_t hash;

	mtx_assert(&t->hftid_lock, MA_OWNED);
	MPASS(head != NULL);

	hash = hf_hashfn_tid(tid);
	LIST_FOREACH(f, &head[hash & t->hftid_tid_mask], link_tid) {
		if (f->tid == tid)
			return (f);
	}

	return (NULL);
}

static void
remove_hf(struct adapter *sc, struct filter_entry *f)
{
	struct tid_info *t = &sc->tids;

	mtx_assert(&t->hftid_lock, MA_OWNED);

	LIST_REMOVE(f, link_4t);
	atomic_subtract_int(&t->tids_in_use, f->fs.type ? 2 : 1);
}

static void
remove_hftid(struct adapter *sc, struct filter_entry *f)
{
#ifdef INVARIANTS
	struct tid_info *t = &sc->tids;

	mtx_assert(&t->hftid_lock, MA_OWNED);
#endif

	LIST_REMOVE(f, link_tid);
}

static uint32_t
mode_to_fconf(uint32_t mode)
{
	uint32_t fconf = 0;

	if (mode & T4_FILTER_IP_FRAGMENT)
		fconf |= F_FRAGMENTATION;

	if (mode & T4_FILTER_MPS_HIT_TYPE)
		fconf |= F_MPSHITTYPE;

	if (mode & T4_FILTER_MAC_IDX)
		fconf |= F_MACMATCH;

	if (mode & T4_FILTER_ETH_TYPE)
		fconf |= F_ETHERTYPE;

	if (mode & T4_FILTER_IP_PROTO)
		fconf |= F_PROTOCOL;

	if (mode & T4_FILTER_IP_TOS)
		fconf |= F_TOS;

	if (mode & T4_FILTER_VLAN)
		fconf |= F_VLAN;

	if (mode & T4_FILTER_VNIC)
		fconf |= F_VNIC_ID;

	if (mode & T4_FILTER_PORT)
		fconf |= F_PORT;

	if (mode & T4_FILTER_FCoE)
		fconf |= F_FCOE;

	return (fconf);
}

static uint32_t
mode_to_iconf(uint32_t mode)
{

	if (mode & T4_FILTER_IC_VNIC)
		return (F_VNIC);
	return (0);
}

static int
check_fspec_against_fconf_iconf(struct adapter *sc,
    struct t4_filter_specification *fs)
{
	struct tp_params *tpp = &sc->params.tp;
	uint32_t fconf = 0;

	if (fs->val.frag || fs->mask.frag)
		fconf |= F_FRAGMENTATION;

	if (fs->val.matchtype || fs->mask.matchtype)
		fconf |= F_MPSHITTYPE;

	if (fs->val.macidx || fs->mask.macidx)
		fconf |= F_MACMATCH;

	if (fs->val.ethtype || fs->mask.ethtype)
		fconf |= F_ETHERTYPE;

	if (fs->val.proto || fs->mask.proto)
		fconf |= F_PROTOCOL;

	if (fs->val.tos || fs->mask.tos)
		fconf |= F_TOS;

	if (fs->val.vlan_vld || fs->mask.vlan_vld)
		fconf |= F_VLAN;

	if (fs->val.ovlan_vld || fs->mask.ovlan_vld) {
		fconf |= F_VNIC_ID;
		if (tpp->ingress_config & F_VNIC)
			return (EINVAL);
	}

	if (fs->val.pfvf_vld || fs->mask.pfvf_vld) {
		fconf |= F_VNIC_ID;
		if ((tpp->ingress_config & F_VNIC) == 0)
			return (EINVAL);
	}

	if (fs->val.iport || fs->mask.iport)
		fconf |= F_PORT;

	if (fs->val.fcoe || fs->mask.fcoe)
		fconf |= F_FCOE;

	if ((tpp->vlan_pri_map | fconf) != tpp->vlan_pri_map)
		return (E2BIG);

	return (0);
}

int
get_filter_mode(struct adapter *sc, uint32_t *mode)
{
	struct tp_params *tp = &sc->params.tp;
	uint64_t mask;

	/* Non-zero incoming value in mode means "hashfilter mode". */
	mask = *mode ? tp->hash_filter_mask : UINT64_MAX;

	/* Always */
	*mode = T4_FILTER_IPv4 | T4_FILTER_IPv6 | T4_FILTER_IP_SADDR |
	    T4_FILTER_IP_DADDR | T4_FILTER_IP_SPORT | T4_FILTER_IP_DPORT;

#define CHECK_FIELD(fconf_bit, field_shift, field_mask, mode_bit)  do { \
	if (tp->vlan_pri_map & (fconf_bit)) { \
		MPASS(tp->field_shift >= 0); \
		if ((mask >> tp->field_shift & field_mask) == field_mask) \
		*mode |= (mode_bit); \
	} \
} while (0)

	CHECK_FIELD(F_FRAGMENTATION, frag_shift, M_FT_FRAGMENTATION, T4_FILTER_IP_FRAGMENT);
	CHECK_FIELD(F_MPSHITTYPE, matchtype_shift, M_FT_MPSHITTYPE, T4_FILTER_MPS_HIT_TYPE);
	CHECK_FIELD(F_MACMATCH, macmatch_shift, M_FT_MACMATCH, T4_FILTER_MAC_IDX);
	CHECK_FIELD(F_ETHERTYPE, ethertype_shift, M_FT_ETHERTYPE, T4_FILTER_ETH_TYPE);
	CHECK_FIELD(F_PROTOCOL, protocol_shift, M_FT_PROTOCOL, T4_FILTER_IP_PROTO);
	CHECK_FIELD(F_TOS, tos_shift, M_FT_TOS, T4_FILTER_IP_TOS);
	CHECK_FIELD(F_VLAN, vlan_shift, M_FT_VLAN, T4_FILTER_VLAN);
	CHECK_FIELD(F_VNIC_ID, vnic_shift, M_FT_VNIC_ID , T4_FILTER_VNIC);
	if (tp->ingress_config & F_VNIC)
		*mode |= T4_FILTER_IC_VNIC;
	CHECK_FIELD(F_PORT, port_shift, M_FT_PORT , T4_FILTER_PORT);
	CHECK_FIELD(F_FCOE, fcoe_shift, M_FT_FCOE , T4_FILTER_FCoE);
#undef CHECK_FIELD

	return (0);
}

int
set_filter_mode(struct adapter *sc, uint32_t mode)
{
	struct tp_params *tpp = &sc->params.tp;
	uint32_t fconf, iconf;
	int rc;

	iconf = mode_to_iconf(mode);
	if ((iconf ^ tpp->ingress_config) & F_VNIC) {
		/*
		 * For now we just complain if A_TP_INGRESS_CONFIG is not
		 * already set to the correct value for the requested filter
		 * mode.  It's not clear if it's safe to write to this register
		 * on the fly.  (And we trust the cached value of the register).
		 *
		 * check_fspec_against_fconf_iconf and other code that looks at
		 * tp->vlan_pri_map and tp->ingress_config needs to be reviewed
		 * thorougly before allowing dynamic filter mode changes.
		 */
		return (EBUSY);
	}

	fconf = mode_to_fconf(mode);

	rc = begin_synchronized_op(sc, NULL, HOLD_LOCK | SLEEP_OK | INTR_OK,
	    "t4setfm");
	if (rc)
		return (rc);

	if (sc->tids.ftids_in_use > 0 || sc->tids.hpftids_in_use > 0) {
		rc = EBUSY;
		goto done;
	}

#ifdef TCP_OFFLOAD
	if (uld_active(sc, ULD_TOM)) {
		rc = EBUSY;
		goto done;
	}
#endif

	rc = -t4_set_filter_mode(sc, fconf, true);
done:
	end_synchronized_op(sc, LOCK_HELD);
	return (rc);
}

static inline uint64_t
get_filter_hits(struct adapter *sc, uint32_t tid)
{
	uint32_t tcb_addr;

	tcb_addr = t4_read_reg(sc, A_TP_CMM_TCB_BASE) + tid * TCB_SIZE;

	if (is_t4(sc)) {
		uint64_t hits;

		read_via_memwin(sc, 0, tcb_addr + 16, (uint32_t *)&hits, 8);
		return (be64toh(hits));
	} else {
		uint32_t hits;

		read_via_memwin(sc, 0, tcb_addr + 24, &hits, 4);
		return (be32toh(hits));
	}
}

int
get_filter(struct adapter *sc, struct t4_filter *t)
{
	if (t->fs.hash)
		return (get_hashfilter(sc, t));
	else
		return (get_tcamfilter(sc, t));
}

static int
set_tcamfilter(struct adapter *sc, struct t4_filter *t, struct l2t_entry *l2te,
    struct smt_entry *smt)
{
	struct filter_entry *f;
	struct fw_filter2_wr *fwr;
	u_int vnic_vld, vnic_vld_mask;
	struct wrq_cookie cookie;
	int i, rc, busy, locked;
	u_int tid;
	const int ntids = t->fs.type ? 4 : 1;

	MPASS(!t->fs.hash);
	/* Already validated against fconf, iconf */
	MPASS((t->fs.val.pfvf_vld & t->fs.val.ovlan_vld) == 0);
	MPASS((t->fs.mask.pfvf_vld & t->fs.mask.ovlan_vld) == 0);

	if (separate_hpfilter_region(sc) && t->fs.prio) {
		MPASS(t->idx < sc->tids.nhpftids);
		f = &sc->tids.hpftid_tab[t->idx];
		tid = sc->tids.hpftid_base + t->idx;
	} else {
		MPASS(t->idx < sc->tids.nftids);
		f = &sc->tids.ftid_tab[t->idx];
		tid = sc->tids.ftid_base + t->idx;
	}
	rc = busy = locked = 0;
	mtx_lock(&sc->tids.ftid_lock);
	for (i = 0; i < ntids; i++) {
		busy += f[i].pending + f[i].valid;
		locked += f[i].locked;
	}
	if (locked > 0)
		rc = EPERM;
	else if (busy > 0)
		rc = EBUSY;
	else {
		int len16;

		if (sc->params.filter2_wr_support)
			len16 = howmany(sizeof(struct fw_filter2_wr), 16);
		else
			len16 = howmany(sizeof(struct fw_filter_wr), 16);
		fwr = start_wrq_wr(&sc->sge.ctrlq[0], len16, &cookie);
		if (__predict_false(fwr == NULL))
			rc = ENOMEM;
		else {
			f->pending = 1;
			if (separate_hpfilter_region(sc) && t->fs.prio)
				sc->tids.hpftids_in_use++;
			else
				sc->tids.ftids_in_use++;
		}
	}
	mtx_unlock(&sc->tids.ftid_lock);
	if (rc != 0)
		return (rc);

	/*
	 * Can't fail now.  A set-filter WR will definitely be sent.
	 */

	f->tid = tid;
	f->fs = t->fs;
	f->l2te = l2te;
	f->smt = smt;

	if (t->fs.val.pfvf_vld || t->fs.val.ovlan_vld)
		vnic_vld = 1;
	else
		vnic_vld = 0;
	if (t->fs.mask.pfvf_vld || t->fs.mask.ovlan_vld)
		vnic_vld_mask = 1;
	else
		vnic_vld_mask = 0;

	bzero(fwr, sizeof(*fwr));
	if (sc->params.filter2_wr_support)
		fwr->op_pkd = htobe32(V_FW_WR_OP(FW_FILTER2_WR));
	else
		fwr->op_pkd = htobe32(V_FW_WR_OP(FW_FILTER_WR));
	fwr->len16_pkd = htobe32(FW_LEN16(*fwr));
	fwr->tid_to_iq =
	    htobe32(V_FW_FILTER_WR_TID(f->tid) |
		V_FW_FILTER_WR_RQTYPE(f->fs.type) |
		V_FW_FILTER_WR_NOREPLY(0) |
		V_FW_FILTER_WR_IQ(f->fs.iq));
	fwr->del_filter_to_l2tix =
	    htobe32(V_FW_FILTER_WR_RPTTID(f->fs.rpttid) |
		V_FW_FILTER_WR_DROP(f->fs.action == FILTER_DROP) |
		V_FW_FILTER_WR_DIRSTEER(f->fs.dirsteer) |
		V_FW_FILTER_WR_MASKHASH(f->fs.maskhash) |
		V_FW_FILTER_WR_DIRSTEERHASH(f->fs.dirsteerhash) |
		V_FW_FILTER_WR_LPBK(f->fs.action == FILTER_SWITCH) |
		V_FW_FILTER_WR_DMAC(f->fs.newdmac) |
		V_FW_FILTER_WR_SMAC(f->fs.newsmac) |
		V_FW_FILTER_WR_INSVLAN(f->fs.newvlan == VLAN_INSERT ||
		    f->fs.newvlan == VLAN_REWRITE) |
		V_FW_FILTER_WR_RMVLAN(f->fs.newvlan == VLAN_REMOVE ||
		    f->fs.newvlan == VLAN_REWRITE) |
		V_FW_FILTER_WR_HITCNTS(f->fs.hitcnts) |
		V_FW_FILTER_WR_TXCHAN(f->fs.eport) |
		V_FW_FILTER_WR_PRIO(f->fs.prio) |
		V_FW_FILTER_WR_L2TIX(f->l2te ? f->l2te->idx : 0));
	fwr->ethtype = htobe16(f->fs.val.ethtype);
	fwr->ethtypem = htobe16(f->fs.mask.ethtype);
	fwr->frag_to_ovlan_vldm =
	    (V_FW_FILTER_WR_FRAG(f->fs.val.frag) |
		V_FW_FILTER_WR_FRAGM(f->fs.mask.frag) |
		V_FW_FILTER_WR_IVLAN_VLD(f->fs.val.vlan_vld) |
		V_FW_FILTER_WR_OVLAN_VLD(vnic_vld) |
		V_FW_FILTER_WR_IVLAN_VLDM(f->fs.mask.vlan_vld) |
		V_FW_FILTER_WR_OVLAN_VLDM(vnic_vld_mask));
	fwr->smac_sel = 0;
	fwr->rx_chan_rx_rpl_iq = htobe16(V_FW_FILTER_WR_RX_CHAN(0) |
	    V_FW_FILTER_WR_RX_RPL_IQ(sc->sge.fwq.abs_id));
	fwr->maci_to_matchtypem =
	    htobe32(V_FW_FILTER_WR_MACI(f->fs.val.macidx) |
		V_FW_FILTER_WR_MACIM(f->fs.mask.macidx) |
		V_FW_FILTER_WR_FCOE(f->fs.val.fcoe) |
		V_FW_FILTER_WR_FCOEM(f->fs.mask.fcoe) |
		V_FW_FILTER_WR_PORT(f->fs.val.iport) |
		V_FW_FILTER_WR_PORTM(f->fs.mask.iport) |
		V_FW_FILTER_WR_MATCHTYPE(f->fs.val.matchtype) |
		V_FW_FILTER_WR_MATCHTYPEM(f->fs.mask.matchtype));
	fwr->ptcl = f->fs.val.proto;
	fwr->ptclm = f->fs.mask.proto;
	fwr->ttyp = f->fs.val.tos;
	fwr->ttypm = f->fs.mask.tos;
	fwr->ivlan = htobe16(f->fs.val.vlan);
	fwr->ivlanm = htobe16(f->fs.mask.vlan);
	fwr->ovlan = htobe16(f->fs.val.vnic);
	fwr->ovlanm = htobe16(f->fs.mask.vnic);
	bcopy(f->fs.val.dip, fwr->lip, sizeof (fwr->lip));
	bcopy(f->fs.mask.dip, fwr->lipm, sizeof (fwr->lipm));
	bcopy(f->fs.val.sip, fwr->fip, sizeof (fwr->fip));
	bcopy(f->fs.mask.sip, fwr->fipm, sizeof (fwr->fipm));
	fwr->lp = htobe16(f->fs.val.dport);
	fwr->lpm = htobe16(f->fs.mask.dport);
	fwr->fp = htobe16(f->fs.val.sport);
	fwr->fpm = htobe16(f->fs.mask.sport);
	/* sma = 0 tells the fw to use SMAC_SEL for source MAC address */
	bzero(fwr->sma, sizeof (fwr->sma));
	if (sc->params.filter2_wr_support) {
		fwr->filter_type_swapmac =
		    V_FW_FILTER2_WR_SWAPMAC(f->fs.swapmac);
		fwr->natmode_to_ulp_type =
		    V_FW_FILTER2_WR_ULP_TYPE(f->fs.nat_mode ?
			ULP_MODE_TCPDDP : ULP_MODE_NONE) |
		    V_FW_FILTER2_WR_NATFLAGCHECK(f->fs.nat_flag_chk) |
		    V_FW_FILTER2_WR_NATMODE(f->fs.nat_mode);
		memcpy(fwr->newlip, f->fs.nat_dip, sizeof(fwr->newlip));
		memcpy(fwr->newfip, f->fs.nat_sip, sizeof(fwr->newfip));
		fwr->newlport = htobe16(f->fs.nat_dport);
		fwr->newfport = htobe16(f->fs.nat_sport);
		fwr->natseqcheck = htobe32(f->fs.nat_seq_chk);
	}
	commit_wrq_wr(&sc->sge.ctrlq[0], fwr, &cookie);

	/* Wait for response. */
	mtx_lock(&sc->tids.ftid_lock);
	for (;;) {
		if (f->pending == 0) {
			rc = f->valid ? 0 : EIO;
			break;
		}
		if (cv_wait_sig(&sc->tids.ftid_cv, &sc->tids.ftid_lock) != 0) {
			rc = EINPROGRESS;
			break;
		}
	}
	mtx_unlock(&sc->tids.ftid_lock);
	return (rc);
}

static int
hashfilter_ntuple(struct adapter *sc, const struct t4_filter_specification *fs,
    uint64_t *ftuple)
{
	struct tp_params *tp = &sc->params.tp;
	uint64_t fmask;

	*ftuple = fmask = 0;

	/*
	 * Initialize each of the fields which we care about which are present
	 * in the Compressed Filter Tuple.
	 */
	if (tp->vlan_shift >= 0 && fs->mask.vlan) {
		*ftuple |= (F_FT_VLAN_VLD | fs->val.vlan) << tp->vlan_shift;
		fmask |= M_FT_VLAN << tp->vlan_shift;
	}

	if (tp->port_shift >= 0 && fs->mask.iport) {
		*ftuple |= (uint64_t)fs->val.iport << tp->port_shift;
		fmask |= M_FT_PORT << tp->port_shift;
	}

	if (tp->protocol_shift >= 0 && fs->mask.proto) {
		*ftuple |= (uint64_t)fs->val.proto << tp->protocol_shift;
		fmask |= M_FT_PROTOCOL << tp->protocol_shift;
	}

	if (tp->tos_shift >= 0 && fs->mask.tos) {
		*ftuple |= (uint64_t)(fs->val.tos) << tp->tos_shift;
		fmask |= M_FT_TOS << tp->tos_shift;
	}

	if (tp->vnic_shift >= 0 && fs->mask.vnic) {
		/* F_VNIC in ingress config was already validated. */
		if (tp->ingress_config & F_VNIC)
			MPASS(fs->mask.pfvf_vld);
		else
			MPASS(fs->mask.ovlan_vld);

		*ftuple |= ((1ULL << 16) | fs->val.vnic) << tp->vnic_shift;
		fmask |= M_FT_VNIC_ID << tp->vnic_shift;
	}

	if (tp->macmatch_shift >= 0 && fs->mask.macidx) {
		*ftuple |= (uint64_t)(fs->val.macidx) << tp->macmatch_shift;
		fmask |= M_FT_MACMATCH << tp->macmatch_shift;
	}

	if (tp->ethertype_shift >= 0 && fs->mask.ethtype) {
		*ftuple |= (uint64_t)(fs->val.ethtype) << tp->ethertype_shift;
		fmask |= M_FT_ETHERTYPE << tp->ethertype_shift;
	}

	if (tp->matchtype_shift >= 0 && fs->mask.matchtype) {
		*ftuple |= (uint64_t)(fs->val.matchtype) << tp->matchtype_shift;
		fmask |= M_FT_MPSHITTYPE << tp->matchtype_shift;
	}

	if (tp->frag_shift >= 0 && fs->mask.frag) {
		*ftuple |= (uint64_t)(fs->val.frag) << tp->frag_shift;
		fmask |= M_FT_FRAGMENTATION << tp->frag_shift;
	}

	if (tp->fcoe_shift >= 0 && fs->mask.fcoe) {
		*ftuple |= (uint64_t)(fs->val.fcoe) << tp->fcoe_shift;
		fmask |= M_FT_FCOE << tp->fcoe_shift;
	}

	/* A hashfilter must conform to the filterMask. */
	if (fmask != tp->hash_filter_mask)
		return (EINVAL);

	return (0);
}

static bool
is_4tuple_specified(struct t4_filter_specification *fs)
{
	int i;
	const int n = fs->type ? 16 : 4;

	if (fs->mask.sport != 0xffff || fs->mask.dport != 0xffff)
		return (false);

	for (i = 0; i < n; i++) {
		if (fs->mask.sip[i] != 0xff)
			return (false);
		if (fs->mask.dip[i] != 0xff)
			return (false);
	}

	return (true);
}

int
set_filter(struct adapter *sc, struct t4_filter *t)
{
	struct tid_info *ti = &sc->tids;
	struct l2t_entry *l2te = NULL;
	struct smt_entry *smt = NULL;
	uint64_t ftuple;
	int rc;

	/*
	 * Basic filter checks first.
	 */

	if (t->fs.hash) {
		if (!is_hashfilter(sc) || ti->ntids == 0)
			return (ENOTSUP);
		/* Hardware, not user, selects a tid for hashfilters. */
		if (t->idx != (uint32_t)-1)
			return (EINVAL);
		/* T5 can't count hashfilter hits. */
		if (is_t5(sc) && t->fs.hitcnts)
			return (EINVAL);
		if (!is_4tuple_specified(&t->fs))
			return (EINVAL);
		rc = hashfilter_ntuple(sc, &t->fs, &ftuple);
		if (rc != 0)
			return (rc);
	} else {
		if (separate_hpfilter_region(sc) && t->fs.prio) {
			if (ti->nhpftids == 0)
				return (ENOTSUP);
			if (t->idx >= ti->nhpftids)
				return (EINVAL);
		} else {
			if (ti->nftids == 0)
				return (ENOTSUP);
			if (t->idx >= ti->nftids)
				return (EINVAL);
		}
		/* IPv6 filter idx must be 4 aligned */
		if (t->fs.type == 1 &&
		    ((t->idx & 0x3) || t->idx + 4 >= ti->nftids))
			return (EINVAL);
	}

	/* T4 doesn't support VLAN tag removal or rewrite, swapmac, and NAT. */
	if (is_t4(sc) && t->fs.action == FILTER_SWITCH &&
	    (t->fs.newvlan == VLAN_REMOVE || t->fs.newvlan == VLAN_REWRITE ||
	    t->fs.swapmac || t->fs.nat_mode))
		return (ENOTSUP);

	if (t->fs.action == FILTER_SWITCH && t->fs.eport >= sc->params.nports)
		return (EINVAL);
	if (t->fs.val.iport >= sc->params.nports)
		return (EINVAL);

	/* Can't specify an iqid/rss_info if not steering. */
	if (!t->fs.dirsteer && !t->fs.dirsteerhash && !t->fs.maskhash && t->fs.iq)
		return (EINVAL);

	/* Validate against the global filter mode and ingress config */
	rc = check_fspec_against_fconf_iconf(sc, &t->fs);
	if (rc != 0)
		return (rc);

	/*
	 * Basic checks passed.  Make sure the queues and tid tables are setup.
	 */

	rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4setf");
	if (rc)
		return (rc);
	if (!(sc->flags & FULL_INIT_DONE) &&
	    ((rc = adapter_full_init(sc)) != 0)) {
		end_synchronized_op(sc, 0);
		return (rc);
	}
	if (t->fs.hash) {
		if (__predict_false(ti->hftid_hash_4t == NULL)) {
			rc = alloc_hftid_hash(&sc->tids, HASH_NOWAIT);
			if (rc != 0)
				goto done;
		}
		if (__predict_false(sc->tids.atid_tab == NULL)) {
			rc = alloc_atid_tab(&sc->tids, M_NOWAIT);
			if (rc != 0)
				goto done;
		}
	} else if (separate_hpfilter_region(sc) && t->fs.prio &&
	    __predict_false(ti->hpftid_tab == NULL)) {
		MPASS(ti->nhpftids != 0);
		KASSERT(ti->hpftids_in_use == 0,
		    ("%s: no memory allocated but hpftids_in_use is %u",
		    __func__, ti->hpftids_in_use));
		ti->hpftid_tab = malloc(sizeof(struct filter_entry) *
		    ti->nhpftids, M_CXGBE, M_NOWAIT | M_ZERO);
		if (ti->hpftid_tab == NULL) {
			rc = ENOMEM;
			goto done;
		}
		if (!mtx_initialized(&sc->tids.ftid_lock)) {
			mtx_init(&ti->ftid_lock, "T4 filters", 0, MTX_DEF);
			cv_init(&ti->ftid_cv, "t4fcv");
		}
	} else if (__predict_false(ti->ftid_tab == NULL)) {
		MPASS(ti->nftids != 0);
		KASSERT(ti->ftids_in_use == 0,
		    ("%s: no memory allocated but ftids_in_use is %u",
		    __func__, ti->ftids_in_use));
		ti->ftid_tab = malloc(sizeof(struct filter_entry) * ti->nftids,
		    M_CXGBE, M_NOWAIT | M_ZERO);
		if (ti->ftid_tab == NULL) {
			rc = ENOMEM;
			goto done;
		}
		if (!mtx_initialized(&sc->tids.ftid_lock)) {
			mtx_init(&ti->ftid_lock, "T4 filters", 0, MTX_DEF);
			cv_init(&ti->ftid_cv, "t4fcv");
		}
	}
done:
	end_synchronized_op(sc, 0);
	if (rc != 0)
		return (rc);

	/*
	 * Allocate L2T entry, SMT entry, etc.
	 */

	if (t->fs.newdmac || t->fs.newvlan) {
		/* This filter needs an L2T entry; allocate one. */
		l2te = t4_l2t_alloc_switching(sc, t->fs.vlan, t->fs.eport,
		    t->fs.dmac);
		if (__predict_false(l2te == NULL)) {
			rc = EAGAIN;
			goto error;
		}
	}

	if (t->fs.newsmac) {
		/* This filter needs an SMT entry; allocate one. */
		smt = t4_smt_alloc_switching(sc->smt, t->fs.smac);
		if (__predict_false(smt == NULL)) {
			rc = EAGAIN;
			goto error;
		}
		rc = t4_smt_set_switching(sc, smt, 0x0, t->fs.smac);
		if (rc)
			goto error;
	}

	if (t->fs.hash)
		rc = set_hashfilter(sc, t, ftuple, l2te, smt);
	else
		rc = set_tcamfilter(sc, t, l2te, smt);

	if (rc != 0 && rc != EINPROGRESS) {
error:
		if (l2te)
			t4_l2t_release(l2te);
		if (smt)
			t4_smt_release(smt);
	}
	return (rc);
}

static int
del_tcamfilter(struct adapter *sc, struct t4_filter *t)
{
	struct filter_entry *f;
	struct fw_filter_wr *fwr;
	struct wrq_cookie cookie;
	int rc, nfilters;
#ifdef INVARIANTS
	u_int tid_base;
#endif

	mtx_lock(&sc->tids.ftid_lock);
	if (separate_hpfilter_region(sc) && t->fs.prio) {
		nfilters = sc->tids.nhpftids;
		f = sc->tids.hpftid_tab;
#ifdef INVARIANTS
		tid_base = sc->tids.hpftid_base;
#endif
	} else {
		nfilters = sc->tids.nftids;
		f = sc->tids.ftid_tab;
#ifdef INVARIANTS
		tid_base = sc->tids.ftid_base;
#endif
	}
	MPASS(f != NULL);	/* Caller checked this. */
	if (t->idx >= nfilters) {
		rc = EINVAL;
		goto done;
	}
	f += t->idx;

	if (f->locked) {
		rc = EPERM;
		goto done;
	}
	if (f->pending) {
		rc = EBUSY;
		goto done;
	}
	if (f->valid == 0) {
		rc = EINVAL;
		goto done;
	}
	MPASS(f->tid == tid_base + t->idx);
	fwr = start_wrq_wr(&sc->sge.ctrlq[0], howmany(sizeof(*fwr), 16), &cookie);
	if (fwr == NULL) {
		rc = ENOMEM;
		goto done;
	}

	bzero(fwr, sizeof (*fwr));
	t4_mk_filtdelwr(f->tid, fwr, sc->sge.fwq.abs_id);
	f->pending = 1;
	commit_wrq_wr(&sc->sge.ctrlq[0], fwr, &cookie);
	t->fs = f->fs;	/* extra info for the caller */

	for (;;) {
		if (f->pending == 0) {
			rc = f->valid ? EIO : 0;
			break;
		}
		if (cv_wait_sig(&sc->tids.ftid_cv, &sc->tids.ftid_lock) != 0) {
			rc = EINPROGRESS;
			break;
		}
	}
done:
	mtx_unlock(&sc->tids.ftid_lock);
	return (rc);
}

int
del_filter(struct adapter *sc, struct t4_filter *t)
{

	/* No filters possible if not initialized yet. */
	if (!(sc->flags & FULL_INIT_DONE))
		return (EINVAL);

	/*
	 * The checks for tid tables ensure that the locks that del_* will reach
	 * for are initialized.
	 */
	if (t->fs.hash) {
		if (sc->tids.hftid_hash_4t != NULL)
			return (del_hashfilter(sc, t));
	} else if (separate_hpfilter_region(sc) && t->fs.prio) {
		if (sc->tids.hpftid_tab != NULL)
			return (del_tcamfilter(sc, t));
	} else {
		if (sc->tids.ftid_tab != NULL)
			return (del_tcamfilter(sc, t));
	}

	return (EINVAL);
}

/*
 * Release secondary resources associated with the filter.
 */
static void
free_filter_resources(struct filter_entry *f)
{

	if (f->l2te) {
		t4_l2t_release(f->l2te);
		f->l2te = NULL;
	}
	if (f->smt) {
		t4_smt_release(f->smt);
		f->smt = NULL;
	}
}

static int
set_tcb_field(struct adapter *sc, u_int tid, uint16_t word, uint64_t mask,
    uint64_t val, int no_reply)
{
	struct wrq_cookie cookie;
	struct cpl_set_tcb_field *req;

	req = start_wrq_wr(&sc->sge.ctrlq[0], howmany(sizeof(*req), 16), &cookie);
	if (req == NULL)
		return (ENOMEM);
	bzero(req, sizeof(*req));
	INIT_TP_WR_MIT_CPL(req, CPL_SET_TCB_FIELD, tid);
	if (no_reply == 0) {
		req->reply_ctrl = htobe16(V_QUEUENO(sc->sge.fwq.abs_id) |
		    V_NO_REPLY(0));
	} else
		req->reply_ctrl = htobe16(V_NO_REPLY(1));
	req->word_cookie = htobe16(V_WORD(word) | V_COOKIE(CPL_COOKIE_HASHFILTER));
	req->mask = htobe64(mask);
	req->val = htobe64(val);
	commit_wrq_wr(&sc->sge.ctrlq[0], req, &cookie);

	return (0);
}

/* Set one of the t_flags bits in the TCB. */
static inline int
set_tcb_tflag(struct adapter *sc, int tid, u_int bit_pos, u_int val,
    u_int no_reply)
{

	return (set_tcb_field(sc, tid,  W_TCB_T_FLAGS, 1ULL << bit_pos,
	    (uint64_t)val << bit_pos, no_reply));
}

int
t4_filter_rpl(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_set_tcb_rpl *rpl = (const void *)(rss + 1);
	u_int tid = GET_TID(rpl);
	u_int rc, idx;
	struct filter_entry *f;

	KASSERT(m == NULL, ("%s: payload with opcode %02x", __func__,
	    rss->opcode));


	if (is_hpftid(sc, tid)) {
		idx = tid - sc->tids.hpftid_base;
		f = &sc->tids.hpftid_tab[idx];
	} else if (is_ftid(sc, tid)) {
		idx = tid - sc->tids.ftid_base;
		f = &sc->tids.ftid_tab[idx];
	} else
		panic("%s: FW reply for invalid TID %d.", __func__, tid);

	MPASS(f->tid == tid);
	rc = G_COOKIE(rpl->cookie);

	mtx_lock(&sc->tids.ftid_lock);
	KASSERT(f->pending, ("%s: reply %d for filter[%u] that isn't pending.",
	    __func__, rc, tid));
	switch(rc) {
	case FW_FILTER_WR_FLT_ADDED:
		/* set-filter succeeded */
		f->valid = 1;
		if (f->fs.newsmac) {
			MPASS(f->smt != NULL);
			set_tcb_tflag(sc, f->tid, S_TF_CCTRL_CWR, 1, 1);
			set_tcb_field(sc, f->tid, W_TCB_SMAC_SEL,
			    V_TCB_SMAC_SEL(M_TCB_SMAC_SEL),
			    V_TCB_SMAC_SEL(f->smt->idx), 1);
			/* XXX: wait for reply to TCB update before !pending */
		}
		break;
	case FW_FILTER_WR_FLT_DELETED:
		/* del-filter succeeded */
		MPASS(f->valid == 1);
		f->valid = 0;
		/* Fall through */
	case FW_FILTER_WR_SMT_TBL_FULL:
		/* set-filter failed due to lack of SMT space. */
		MPASS(f->valid == 0);
		free_filter_resources(f);
		if (separate_hpfilter_region(sc) && f->fs.prio)
			sc->tids.hpftids_in_use--;
		else
			sc->tids.ftids_in_use--;
		break;
	case FW_FILTER_WR_SUCCESS:
	case FW_FILTER_WR_EINVAL:
	default:
		panic("%s: unexpected reply %d for filter[%d].", __func__, rc,
		    idx);
	}
	f->pending = 0;
	cv_broadcast(&sc->tids.ftid_cv);
	mtx_unlock(&sc->tids.ftid_lock);

	return (0);
}

/*
 * This is the reply to the Active Open that created the filter.  Additional TCB
 * updates may be required to complete the filter configuration.
 */
int
t4_hashfilter_ao_rpl(struct sge_iq *iq, const struct rss_header *rss,
    struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_act_open_rpl *cpl = (const void *)(rss + 1);
	u_int atid = G_TID_TID(G_AOPEN_ATID(be32toh(cpl->atid_status)));
	u_int status = G_AOPEN_STATUS(be32toh(cpl->atid_status));
	struct filter_entry *f = lookup_atid(sc, atid);

	KASSERT(m == NULL, ("%s: wasn't expecting payload", __func__));

	mtx_lock(&sc->tids.hftid_lock);
	KASSERT(f->pending, ("%s: hashfilter[%p] isn't pending.", __func__, f));
	KASSERT(f->tid == -1, ("%s: hashfilter[%p] has tid %d already.",
	    __func__, f, f->tid));
	if (status == CPL_ERR_NONE) {
		f->tid = GET_TID(cpl);
		MPASS(lookup_hftid(sc, f->tid) == NULL);
		insert_hftid(sc, f);
		/*
		 * Leave the filter pending until it is fully set up, which will
		 * be indicated by the reply to the last TCB update.  No need to
		 * unblock the ioctl thread either.
		 */
		if (configure_hashfilter_tcb(sc, f) == EINPROGRESS)
			goto done;
		f->valid = 1;
		f->pending = 0;
	} else {
		/* provide errno instead of tid to ioctl */
		f->tid = act_open_rpl_status_to_errno(status);
		f->valid = 0;
		f->pending = 0;
		if (act_open_has_tid(status))
			release_tid(sc, GET_TID(cpl), &sc->sge.ctrlq[0]);
		free_filter_resources(f);
		remove_hf(sc, f);
		if (f->locked == 0)
			free(f, M_CXGBE);
	}
	cv_broadcast(&sc->tids.hftid_cv);
done:
	mtx_unlock(&sc->tids.hftid_lock);

	free_atid(sc, atid);
	return (0);
}

int
t4_hashfilter_tcb_rpl(struct sge_iq *iq, const struct rss_header *rss,
    struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_set_tcb_rpl *rpl = (const void *)(rss + 1);
	u_int tid = GET_TID(rpl);
	struct filter_entry *f;

	mtx_lock(&sc->tids.hftid_lock);
	f = lookup_hftid(sc, tid);
	KASSERT(f->tid == tid, ("%s: filter tid mismatch", __func__));
	KASSERT(f->pending, ("%s: hashfilter %p [%u] isn't pending.", __func__,
	    f, tid));
	KASSERT(f->valid == 0, ("%s: hashfilter %p [%u] is valid already.",
	    __func__, f, tid));
	f->pending = 0;
	if (rpl->status == 0) {
		f->valid = 1;
	} else {
		f->tid = EIO;
		f->valid = 0;
		free_filter_resources(f);
		remove_hftid(sc, f);
		remove_hf(sc, f);
		release_tid(sc, tid, &sc->sge.ctrlq[0]);
		if (f->locked == 0)
			free(f, M_CXGBE);
	}
	cv_broadcast(&sc->tids.hftid_cv);
	mtx_unlock(&sc->tids.hftid_lock);

	return (0);
}

int
t4_del_hashfilter_rpl(struct sge_iq *iq, const struct rss_header *rss,
    struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_abort_rpl_rss *cpl = (const void *)(rss + 1);
	unsigned int tid = GET_TID(cpl);
	struct filter_entry *f;

	mtx_lock(&sc->tids.hftid_lock);
	f = lookup_hftid(sc, tid);
	KASSERT(f->tid == tid, ("%s: filter tid mismatch", __func__));
	KASSERT(f->pending, ("%s: hashfilter %p [%u] isn't pending.", __func__,
	    f, tid));
	KASSERT(f->valid, ("%s: hashfilter %p [%u] isn't valid.", __func__, f,
	    tid));
	f->pending = 0;
	if (cpl->status == 0) {
		f->valid = 0;
		free_filter_resources(f);
		remove_hftid(sc, f);
		remove_hf(sc, f);
		release_tid(sc, tid, &sc->sge.ctrlq[0]);
		if (f->locked == 0)
			free(f, M_CXGBE);
	}
	cv_broadcast(&sc->tids.hftid_cv);
	mtx_unlock(&sc->tids.hftid_lock);

	return (0);
}

static int
get_tcamfilter(struct adapter *sc, struct t4_filter *t)
{
	int i, nfilters;
	struct filter_entry *f;
	u_int in_use;
#ifdef INVARIANTS
	u_int tid_base;
#endif

	MPASS(!t->fs.hash);

	if (separate_hpfilter_region(sc) && t->fs.prio) {
		nfilters = sc->tids.nhpftids;
		f = sc->tids.hpftid_tab;
		in_use = sc->tids.hpftids_in_use;
#ifdef INVARIANTS
		tid_base = sc->tids.hpftid_base;
#endif
	} else {
		nfilters = sc->tids.nftids;
		f = sc->tids.ftid_tab;
		in_use = sc->tids.ftids_in_use;
#ifdef INVARIANTS
		tid_base = sc->tids.ftid_base;
#endif
	}

	if (in_use == 0 || f == NULL || t->idx >= nfilters) {
		t->idx = 0xffffffff;
		return (0);
	}

	f += t->idx;
	mtx_lock(&sc->tids.ftid_lock);
	for (i = t->idx; i < nfilters; i++, f++) {
		if (f->valid) {
			MPASS(f->tid == tid_base + i);
			t->idx = i;
			t->l2tidx = f->l2te ? f->l2te->idx : 0;
			t->smtidx = f->smt ? f->smt->idx : 0;
			if (f->fs.hitcnts)
				t->hits = get_filter_hits(sc, f->tid);
			else
				t->hits = UINT64_MAX;
			t->fs = f->fs;

			goto done;
		}
	}
	t->idx = 0xffffffff;
done:
	mtx_unlock(&sc->tids.ftid_lock);
	return (0);
}

static int
get_hashfilter(struct adapter *sc, struct t4_filter *t)
{
	struct tid_info *ti = &sc->tids;
	int tid;
	struct filter_entry *f;
	const int inv_tid = ti->ntids + ti->tid_base;

	MPASS(t->fs.hash);

	if (ti->tids_in_use == 0 || ti->hftid_hash_tid == NULL ||
	    t->idx >= inv_tid) {
		t->idx = 0xffffffff;
		return (0);
	}
	if (t->idx < ti->tid_base)
		t->idx = ti->tid_base;

	mtx_lock(&ti->hftid_lock);
	for (tid = t->idx; tid < inv_tid; tid++) {
		f = lookup_hftid(sc, tid);
		if (f != NULL && f->valid) {
			t->idx = tid;
			t->l2tidx = f->l2te ? f->l2te->idx : 0;
			t->smtidx = f->smt ? f->smt->idx : 0;
			if (f->fs.hitcnts)
				t->hits = get_filter_hits(sc, tid);
			else
				t->hits = UINT64_MAX;
			t->fs = f->fs;

			goto done;
		}
	}
	t->idx = 0xffffffff;
done:
	mtx_unlock(&ti->hftid_lock);
	return (0);
}

static void
mk_act_open_req6(struct adapter *sc, struct filter_entry *f, int atid,
    uint64_t ftuple, struct cpl_act_open_req6 *cpl)
{
	struct cpl_t5_act_open_req6 *cpl5 = (void *)cpl;
	struct cpl_t6_act_open_req6 *cpl6 = (void *)cpl;

	/* Review changes to CPL after cpl_t6_act_open_req if this goes off. */
	MPASS(chip_id(sc) >= CHELSIO_T5 && chip_id(sc) <= CHELSIO_T6);
	MPASS(atid >= 0);

	if (chip_id(sc) == CHELSIO_T5) {
		INIT_TP_WR(cpl5, 0);
	} else {
		INIT_TP_WR(cpl6, 0);
		cpl6->rsvd2 = 0;
		cpl6->opt3 = 0;
	}

	OPCODE_TID(cpl) = htobe32(MK_OPCODE_TID(CPL_ACT_OPEN_REQ6,
	    V_TID_QID(sc->sge.fwq.abs_id) | V_TID_TID(atid) |
	    V_TID_COOKIE(CPL_COOKIE_HASHFILTER)));
	cpl->local_port = htobe16(f->fs.val.dport);
	cpl->peer_port = htobe16(f->fs.val.sport);
	cpl->local_ip_hi = *(uint64_t *)(&f->fs.val.dip);
	cpl->local_ip_lo = *(((uint64_t *)&f->fs.val.dip) + 1);
	cpl->peer_ip_hi = *(uint64_t *)(&f->fs.val.sip);
	cpl->peer_ip_lo = *(((uint64_t *)&f->fs.val.sip) + 1);
	cpl->opt0 = htobe64(V_NAGLE(f->fs.newvlan == VLAN_REMOVE ||
	    f->fs.newvlan == VLAN_REWRITE) | V_DELACK(f->fs.hitcnts) |
	    V_L2T_IDX(f->l2te ? f->l2te->idx : 0) | V_TX_CHAN(f->fs.eport) |
	    V_NO_CONG(f->fs.rpttid) |
	    V_ULP_MODE(f->fs.nat_mode ? ULP_MODE_TCPDDP : ULP_MODE_NONE) |
	    F_TCAM_BYPASS | F_NON_OFFLOAD);

	cpl6->params = htobe64(V_FILTER_TUPLE(ftuple));
	cpl6->opt2 = htobe32(F_RSS_QUEUE_VALID | V_RSS_QUEUE(f->fs.iq) |
	    V_TX_QUEUE(f->fs.nat_mode) | V_WND_SCALE_EN(f->fs.nat_flag_chk) |
	    V_RX_FC_DISABLE(f->fs.nat_seq_chk ? 1 : 0) | F_T5_OPT_2_VALID |
	    F_RX_CHANNEL | V_SACK_EN(f->fs.swapmac) |
	    V_CONG_CNTRL((f->fs.action == FILTER_DROP) | (f->fs.dirsteer << 1)) |
	    V_PACE(f->fs.maskhash | (f->fs.dirsteerhash << 1)));
}

static void
mk_act_open_req(struct adapter *sc, struct filter_entry *f, int atid,
    uint64_t ftuple, struct cpl_act_open_req *cpl)
{
	struct cpl_t5_act_open_req *cpl5 = (void *)cpl;
	struct cpl_t6_act_open_req *cpl6 = (void *)cpl;

	/* Review changes to CPL after cpl_t6_act_open_req if this goes off. */
	MPASS(chip_id(sc) >= CHELSIO_T5 && chip_id(sc) <= CHELSIO_T6);
	MPASS(atid >= 0);

	if (chip_id(sc) == CHELSIO_T5) {
		INIT_TP_WR(cpl5, 0);
	} else {
		INIT_TP_WR(cpl6, 0);
		cpl6->rsvd2 = 0;
		cpl6->opt3 = 0;
	}

	OPCODE_TID(cpl) = htobe32(MK_OPCODE_TID(CPL_ACT_OPEN_REQ,
	    V_TID_QID(sc->sge.fwq.abs_id) | V_TID_TID(atid) |
	    V_TID_COOKIE(CPL_COOKIE_HASHFILTER)));
	cpl->local_port = htobe16(f->fs.val.dport);
	cpl->peer_port = htobe16(f->fs.val.sport);
	cpl->local_ip = f->fs.val.dip[0] | f->fs.val.dip[1] << 8 |
	    f->fs.val.dip[2] << 16 | f->fs.val.dip[3] << 24;
	cpl->peer_ip = f->fs.val.sip[0] | f->fs.val.sip[1] << 8 |
		f->fs.val.sip[2] << 16 | f->fs.val.sip[3] << 24;
	cpl->opt0 = htobe64(V_NAGLE(f->fs.newvlan == VLAN_REMOVE ||
	    f->fs.newvlan == VLAN_REWRITE) | V_DELACK(f->fs.hitcnts) |
	    V_L2T_IDX(f->l2te ? f->l2te->idx : 0) | V_TX_CHAN(f->fs.eport) |
	    V_NO_CONG(f->fs.rpttid) |
	    V_ULP_MODE(f->fs.nat_mode ? ULP_MODE_TCPDDP : ULP_MODE_NONE) |
	    F_TCAM_BYPASS | F_NON_OFFLOAD);

	cpl6->params = htobe64(V_FILTER_TUPLE(ftuple));
	cpl6->opt2 = htobe32(F_RSS_QUEUE_VALID | V_RSS_QUEUE(f->fs.iq) |
	    V_TX_QUEUE(f->fs.nat_mode) | V_WND_SCALE_EN(f->fs.nat_flag_chk) |
	    V_RX_FC_DISABLE(f->fs.nat_seq_chk ? 1 : 0) | F_T5_OPT_2_VALID |
	    F_RX_CHANNEL | V_SACK_EN(f->fs.swapmac) |
	    V_CONG_CNTRL((f->fs.action == FILTER_DROP) | (f->fs.dirsteer << 1)) |
	    V_PACE(f->fs.maskhash | (f->fs.dirsteerhash << 1)));
}

static int
act_open_cpl_len16(struct adapter *sc, int isipv6)
{
	int idx;
	static const int sz_table[3][2] = {
		{
			howmany(sizeof (struct cpl_act_open_req), 16),
			howmany(sizeof (struct cpl_act_open_req6), 16)
		},
		{
			howmany(sizeof (struct cpl_t5_act_open_req), 16),
			howmany(sizeof (struct cpl_t5_act_open_req6), 16)
		},
		{
			howmany(sizeof (struct cpl_t6_act_open_req), 16),
			howmany(sizeof (struct cpl_t6_act_open_req6), 16)
		},
	};

	MPASS(chip_id(sc) >= CHELSIO_T4);
	idx = min(chip_id(sc) - CHELSIO_T4, 2);

	return (sz_table[idx][!!isipv6]);
}

static int
set_hashfilter(struct adapter *sc, struct t4_filter *t, uint64_t ftuple,
    struct l2t_entry *l2te, struct smt_entry *smt)
{
	void *wr;
	struct wrq_cookie cookie;
	struct filter_entry *f;
	int rc, atid = -1;
	uint32_t hash;

	MPASS(t->fs.hash);
	/* Already validated against fconf, iconf */
	MPASS((t->fs.val.pfvf_vld & t->fs.val.ovlan_vld) == 0);
	MPASS((t->fs.mask.pfvf_vld & t->fs.mask.ovlan_vld) == 0);

	hash = hf_hashfn_4t(&t->fs);

	mtx_lock(&sc->tids.hftid_lock);
	if (lookup_hf(sc, &t->fs, hash) != NULL) {
		rc = EEXIST;
		goto done;
	}

	f = malloc(sizeof(*f), M_CXGBE, M_ZERO | M_NOWAIT);
	if (__predict_false(f == NULL)) {
		rc = ENOMEM;
		goto done;
	}
	f->fs = t->fs;
	f->l2te = l2te;
	f->smt = smt;

	atid = alloc_atid(sc, f);
	if (__predict_false(atid) == -1) {
		free(f, M_CXGBE);
		rc = EAGAIN;
		goto done;
	}
	MPASS(atid >= 0);

	wr = start_wrq_wr(&sc->sge.ctrlq[0], act_open_cpl_len16(sc, f->fs.type),
	    &cookie);
	if (wr == NULL) {
		free_atid(sc, atid);
		free(f, M_CXGBE);
		rc = ENOMEM;
		goto done;
	}
	if (f->fs.type)
		mk_act_open_req6(sc, f, atid, ftuple, wr);
	else
		mk_act_open_req(sc, f, atid, ftuple, wr);

	f->locked = 1; /* ithread mustn't free f if ioctl is still around. */
	f->pending = 1;
	f->tid = -1;
	insert_hf(sc, f, hash);
	commit_wrq_wr(&sc->sge.ctrlq[0], wr, &cookie);

	for (;;) {
		MPASS(f->locked);
		if (f->pending == 0) {
			if (f->valid) {
				rc = 0;
				f->locked = 0;
				t->idx = f->tid;
			} else {
				rc = f->tid;
				free(f, M_CXGBE);
			}
			break;
		}
		if (cv_wait_sig(&sc->tids.hftid_cv, &sc->tids.hftid_lock) != 0) {
			f->locked = 0;
			rc = EINPROGRESS;
			break;
		}
	}
done:
	mtx_unlock(&sc->tids.hftid_lock);
	return (rc);
}

/* SET_TCB_FIELD sent as a ULP command looks like this */
#define LEN__SET_TCB_FIELD_ULP (sizeof(struct ulp_txpkt) + \
    sizeof(struct ulptx_idata) + sizeof(struct cpl_set_tcb_field_core))

static void *
mk_set_tcb_field_ulp(struct ulp_txpkt *ulpmc, uint64_t word, uint64_t mask,
		uint64_t val, uint32_t tid, uint32_t qid)
{
	struct ulptx_idata *ulpsc;
	struct cpl_set_tcb_field_core *req;

	ulpmc->cmd_dest = htonl(V_ULPTX_CMD(ULP_TX_PKT) | V_ULP_TXPKT_DEST(0));
	ulpmc->len = htobe32(howmany(LEN__SET_TCB_FIELD_ULP, 16));

	ulpsc = (struct ulptx_idata *)(ulpmc + 1);
	ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_IMM));
	ulpsc->len = htobe32(sizeof(*req));

	req = (struct cpl_set_tcb_field_core *)(ulpsc + 1);
	OPCODE_TID(req) = htobe32(MK_OPCODE_TID(CPL_SET_TCB_FIELD, tid));
	req->reply_ctrl = htobe16(V_NO_REPLY(1) | V_QUEUENO(qid));
	req->word_cookie = htobe16(V_WORD(word) | V_COOKIE(0));
	req->mask = htobe64(mask);
	req->val = htobe64(val);

	ulpsc = (struct ulptx_idata *)(req + 1);
	if (LEN__SET_TCB_FIELD_ULP % 16) {
		ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_NOOP));
		ulpsc->len = htobe32(0);
		return (ulpsc + 1);
	}
	return (ulpsc);
}

/* ABORT_REQ sent as a ULP command looks like this */
#define LEN__ABORT_REQ_ULP (sizeof(struct ulp_txpkt) + \
	sizeof(struct ulptx_idata) + sizeof(struct cpl_abort_req_core))

static void *
mk_abort_req_ulp(struct ulp_txpkt *ulpmc, uint32_t tid)
{
	struct ulptx_idata *ulpsc;
	struct cpl_abort_req_core *req;

	ulpmc->cmd_dest = htonl(V_ULPTX_CMD(ULP_TX_PKT) | V_ULP_TXPKT_DEST(0));
	ulpmc->len = htobe32(howmany(LEN__ABORT_REQ_ULP, 16));

	ulpsc = (struct ulptx_idata *)(ulpmc + 1);
	ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_IMM));
	ulpsc->len = htobe32(sizeof(*req));

	req = (struct cpl_abort_req_core *)(ulpsc + 1);
	OPCODE_TID(req) = htobe32(MK_OPCODE_TID(CPL_ABORT_REQ, tid));
	req->rsvd0 = htonl(0);
	req->rsvd1 = 0;
	req->cmd = CPL_ABORT_NO_RST;

	ulpsc = (struct ulptx_idata *)(req + 1);
	if (LEN__ABORT_REQ_ULP % 16) {
		ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_NOOP));
		ulpsc->len = htobe32(0);
		return (ulpsc + 1);
	}
	return (ulpsc);
}

/* ABORT_RPL sent as a ULP command looks like this */
#define LEN__ABORT_RPL_ULP (sizeof(struct ulp_txpkt) + \
	sizeof(struct ulptx_idata) + sizeof(struct cpl_abort_rpl_core))

static void *
mk_abort_rpl_ulp(struct ulp_txpkt *ulpmc, uint32_t tid)
{
	struct ulptx_idata *ulpsc;
	struct cpl_abort_rpl_core *rpl;

	ulpmc->cmd_dest = htonl(V_ULPTX_CMD(ULP_TX_PKT) | V_ULP_TXPKT_DEST(0));
	ulpmc->len = htobe32(howmany(LEN__ABORT_RPL_ULP, 16));

	ulpsc = (struct ulptx_idata *)(ulpmc + 1);
	ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_IMM));
	ulpsc->len = htobe32(sizeof(*rpl));

	rpl = (struct cpl_abort_rpl_core *)(ulpsc + 1);
	OPCODE_TID(rpl) = htobe32(MK_OPCODE_TID(CPL_ABORT_RPL, tid));
	rpl->rsvd0 = htonl(0);
	rpl->rsvd1 = 0;
	rpl->cmd = CPL_ABORT_NO_RST;

	ulpsc = (struct ulptx_idata *)(rpl + 1);
	if (LEN__ABORT_RPL_ULP % 16) {
		ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_NOOP));
		ulpsc->len = htobe32(0);
		return (ulpsc + 1);
	}
	return (ulpsc);
}

static inline int
del_hashfilter_wrlen(void)
{

	return (sizeof(struct work_request_hdr) +
	    roundup2(LEN__SET_TCB_FIELD_ULP, 16) +
	    roundup2(LEN__ABORT_REQ_ULP, 16) +
	    roundup2(LEN__ABORT_RPL_ULP, 16));
}

static void
mk_del_hashfilter_wr(int tid, struct work_request_hdr *wrh, int wrlen, int qid)
{
	struct ulp_txpkt *ulpmc;

	INIT_ULPTX_WRH(wrh, wrlen, 0, 0);
	ulpmc = (struct ulp_txpkt *)(wrh + 1);
	ulpmc = mk_set_tcb_field_ulp(ulpmc, W_TCB_RSS_INFO,
	    V_TCB_RSS_INFO(M_TCB_RSS_INFO), V_TCB_RSS_INFO(qid), tid, 0);
	ulpmc = mk_abort_req_ulp(ulpmc, tid);
	ulpmc = mk_abort_rpl_ulp(ulpmc, tid);
}

static int
del_hashfilter(struct adapter *sc, struct t4_filter *t)
{
	struct tid_info *ti = &sc->tids;
	void *wr;
	struct filter_entry *f;
	struct wrq_cookie cookie;
	int rc;
	const int wrlen = del_hashfilter_wrlen();
	const int inv_tid = ti->ntids + ti->tid_base;

	MPASS(sc->tids.hftid_hash_4t != NULL);
	MPASS(sc->tids.ntids > 0);

	if (t->idx < sc->tids.tid_base || t->idx >= inv_tid)
		return (EINVAL);

	mtx_lock(&ti->hftid_lock);
	f = lookup_hftid(sc, t->idx);
	if (f == NULL || f->valid == 0) {
		rc = EINVAL;
		goto done;
	}
	MPASS(f->tid == t->idx);
	if (f->locked) {
		rc = EPERM;
		goto done;
	}
	if (f->pending) {
		rc = EBUSY;
		goto done;
	}
	wr = start_wrq_wr(&sc->sge.ctrlq[0], howmany(wrlen, 16), &cookie);
	if (wr == NULL) {
		rc = ENOMEM;
		goto done;
	}

	mk_del_hashfilter_wr(t->idx, wr, wrlen, sc->sge.fwq.abs_id);
	f->locked = 1;
	f->pending = 1;
	commit_wrq_wr(&sc->sge.ctrlq[0], wr, &cookie);
	t->fs = f->fs;	/* extra info for the caller */

	for (;;) {
		MPASS(f->locked);
		if (f->pending == 0) {
			if (f->valid) {
				f->locked = 0;
				rc = EIO;
			} else {
				rc = 0;
				free(f, M_CXGBE);
			}
			break;
		}
		if (cv_wait_sig(&ti->hftid_cv, &ti->hftid_lock) != 0) {
			f->locked = 0;
			rc = EINPROGRESS;
			break;
		}
	}
done:
	mtx_unlock(&ti->hftid_lock);
	return (rc);
}

#define WORD_MASK       0xffffffff
static void
set_nat_params(struct adapter *sc, struct filter_entry *f, const bool dip,
    const bool sip, const bool dp, const bool sp)
{

	if (dip) {
		if (f->fs.type) {
			set_tcb_field(sc, f->tid, W_TCB_SND_UNA_RAW, WORD_MASK,
			    f->fs.nat_dip[15] | f->fs.nat_dip[14] << 8 |
			    f->fs.nat_dip[13] << 16 | f->fs.nat_dip[12] << 24, 1);

			set_tcb_field(sc, f->tid,
			    W_TCB_SND_UNA_RAW + 1, WORD_MASK,
			    f->fs.nat_dip[11] | f->fs.nat_dip[10] << 8 |
			    f->fs.nat_dip[9] << 16 | f->fs.nat_dip[8] << 24, 1);

			set_tcb_field(sc, f->tid,
			    W_TCB_SND_UNA_RAW + 2, WORD_MASK,
			    f->fs.nat_dip[7] | f->fs.nat_dip[6] << 8 |
			    f->fs.nat_dip[5] << 16 | f->fs.nat_dip[4] << 24, 1);

			set_tcb_field(sc, f->tid,
			    W_TCB_SND_UNA_RAW + 3, WORD_MASK,
			    f->fs.nat_dip[3] | f->fs.nat_dip[2] << 8 |
			    f->fs.nat_dip[1] << 16 | f->fs.nat_dip[0] << 24, 1);
		} else {
			set_tcb_field(sc, f->tid,
			    W_TCB_RX_FRAG3_LEN_RAW, WORD_MASK,
			    f->fs.nat_dip[3] | f->fs.nat_dip[2] << 8 |
			    f->fs.nat_dip[1] << 16 | f->fs.nat_dip[0] << 24, 1);
		}
	}

	if (sip) {
		if (f->fs.type) {
			set_tcb_field(sc, f->tid,
			    W_TCB_RX_FRAG2_PTR_RAW, WORD_MASK,
			    f->fs.nat_sip[15] | f->fs.nat_sip[14] << 8 |
			    f->fs.nat_sip[13] << 16 | f->fs.nat_sip[12] << 24, 1);

			set_tcb_field(sc, f->tid,
			    W_TCB_RX_FRAG2_PTR_RAW + 1, WORD_MASK,
			    f->fs.nat_sip[11] | f->fs.nat_sip[10] << 8 |
			    f->fs.nat_sip[9] << 16 | f->fs.nat_sip[8] << 24, 1);

			set_tcb_field(sc, f->tid,
			    W_TCB_RX_FRAG2_PTR_RAW + 2, WORD_MASK,
			    f->fs.nat_sip[7] | f->fs.nat_sip[6] << 8 |
			    f->fs.nat_sip[5] << 16 | f->fs.nat_sip[4] << 24, 1);

			set_tcb_field(sc, f->tid,
			    W_TCB_RX_FRAG2_PTR_RAW + 3, WORD_MASK,
			    f->fs.nat_sip[3] | f->fs.nat_sip[2] << 8 |
			    f->fs.nat_sip[1] << 16 | f->fs.nat_sip[0] << 24, 1);

		} else {
			set_tcb_field(sc, f->tid,
			    W_TCB_RX_FRAG3_START_IDX_OFFSET_RAW, WORD_MASK,
			    f->fs.nat_sip[3] | f->fs.nat_sip[2] << 8 |
			    f->fs.nat_sip[1] << 16 | f->fs.nat_sip[0] << 24, 1);
		}
	}

	set_tcb_field(sc, f->tid, W_TCB_PDU_HDR_LEN, WORD_MASK,
	    (dp ? f->fs.nat_dport : 0) | (sp ? f->fs.nat_sport << 16 : 0), 1);
}

/*
 * Returns EINPROGRESS to indicate that at least one TCB update was sent and the
 * last of the series of updates requested a reply.  The reply informs the
 * driver that the filter is fully setup.
 */
static int
configure_hashfilter_tcb(struct adapter *sc, struct filter_entry *f)
{
	int updated = 0;

	MPASS(f->tid < sc->tids.ntids);
	MPASS(f->fs.hash);
	MPASS(f->pending);
	MPASS(f->valid == 0);

	if (f->fs.newdmac) {
		set_tcb_tflag(sc, f->tid, S_TF_CCTRL_ECE, 1, 1);
		updated++;
	}

	if (f->fs.newvlan == VLAN_INSERT || f->fs.newvlan == VLAN_REWRITE) {
		set_tcb_tflag(sc, f->tid, S_TF_CCTRL_RFR, 1, 1);
		updated++;
	}

	if (f->fs.newsmac) {
		MPASS(f->smt != NULL);
		set_tcb_tflag(sc, f->tid, S_TF_CCTRL_CWR, 1, 1);
		set_tcb_field(sc, f->tid, W_TCB_SMAC_SEL,
		    V_TCB_SMAC_SEL(M_TCB_SMAC_SEL), V_TCB_SMAC_SEL(f->smt->idx),
		    1);
		updated++;
	}

	switch(f->fs.nat_mode) {
	case NAT_MODE_NONE:
		break;
	case NAT_MODE_DIP:
		set_nat_params(sc, f, true, false, false, false);
		updated++;
		break;
	case NAT_MODE_DIP_DP:
		set_nat_params(sc, f, true, false, true, false);
		updated++;
		break;
	case NAT_MODE_DIP_DP_SIP:
		set_nat_params(sc, f, true, true, true, false);
		updated++;
		break;
	case NAT_MODE_DIP_DP_SP:
		set_nat_params(sc, f, true, false, true, true);
		updated++;
		break;
	case NAT_MODE_SIP_SP:
		set_nat_params(sc, f, false, true, false, true);
		updated++;
		break;
	case NAT_MODE_DIP_SIP_SP:
		set_nat_params(sc, f, true, true, false, true);
		updated++;
		break;
	case NAT_MODE_ALL:
		set_nat_params(sc, f, true, true, true, true);
		updated++;
		break;
	default:
		MPASS(0);	/* should have been validated earlier */
		break;

	}

	if (f->fs.nat_seq_chk) {
		set_tcb_field(sc, f->tid, W_TCB_RCV_NXT,
		    V_TCB_RCV_NXT(M_TCB_RCV_NXT),
		    V_TCB_RCV_NXT(f->fs.nat_seq_chk), 1);
		updated++;
	}

	if (is_t5(sc) && f->fs.action == FILTER_DROP) {
		/*
		 * Migrating = 1, Non-offload = 0 to get a T5 hashfilter to drop.
		 */
		set_tcb_field(sc, f->tid, W_TCB_T_FLAGS, V_TF_NON_OFFLOAD(1) |
		    V_TF_MIGRATING(1), V_TF_MIGRATING(1), 1);
		updated++;
	}

	/*
	 * Enable switching after all secondary resources (L2T entry, SMT entry,
	 * etc.) are setup so that any switched packet will use correct
	 * values.
	 */
	if (f->fs.action == FILTER_SWITCH) {
		set_tcb_tflag(sc, f->tid, S_TF_CCTRL_ECN, 1, 1);
		updated++;
	}

	if (f->fs.hitcnts || updated > 0) {
		set_tcb_field(sc, f->tid, W_TCB_TIMESTAMP,
		    V_TCB_TIMESTAMP(M_TCB_TIMESTAMP) |
		    V_TCB_T_RTT_TS_RECENT_AGE(M_TCB_T_RTT_TS_RECENT_AGE),
		    V_TCB_TIMESTAMP(0ULL) | V_TCB_T_RTT_TS_RECENT_AGE(0ULL), 0);
		return (EINPROGRESS);
	}

	return (0);
}
