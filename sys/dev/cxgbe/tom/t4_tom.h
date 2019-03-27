/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2015 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: Navdeep Parhar <np@FreeBSD.org>
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

#ifndef __T4_TOM_H__
#define __T4_TOM_H__
#include <sys/vmem.h>
#include "tom/t4_tls.h"

#define LISTEN_HASH_SIZE 32

/*
 * Min receive window.  We want it to be large enough to accommodate receive
 * coalescing, handle jumbo frames, and not trigger sender SWS avoidance.
 */
#define MIN_RCV_WND (24 * 1024U)

/*
 * Max receive window supported by HW in bytes.  Only a small part of it can
 * be set through option0, the rest needs to be set through RX_DATA_ACK.
 */
#define MAX_RCV_WND ((1U << 27) - 1)

#define	DDP_RSVD_WIN (16 * 1024U)
#define	SB_DDP_INDICATE	SB_IN_TOE	/* soreceive must respond to indicate */

#define USE_DDP_RX_FLOW_CONTROL

#define PPOD_SZ(n)	((n) * sizeof(struct pagepod))
#define PPOD_SIZE	(PPOD_SZ(1))

/* TOE PCB flags */
enum {
	TPF_ATTACHED	   = (1 << 0),	/* a tcpcb refers to this toepcb */
	TPF_FLOWC_WR_SENT  = (1 << 1),	/* firmware flow context WR sent */
	TPF_TX_DATA_SENT   = (1 << 2),	/* some data sent */
	TPF_TX_SUSPENDED   = (1 << 3),	/* tx suspended for lack of resources */
	TPF_SEND_FIN	   = (1 << 4),	/* send FIN after all pending data */
	TPF_FIN_SENT	   = (1 << 5),	/* FIN has been sent */
	TPF_ABORT_SHUTDOWN = (1 << 6),	/* connection abort is in progress */
	TPF_CPL_PENDING    = (1 << 7),	/* haven't received the last CPL */
	TPF_SYNQE	   = (1 << 8),	/* synq_entry, not really a toepcb */
	TPF_SYNQE_EXPANDED = (1 << 9),	/* toepcb ready, tid context updated */
	TPF_FORCE_CREDITS  = (1 << 10), /* always send credits */
};

enum {
	DDP_OK		= (1 << 0),	/* OK to turn on DDP */
	DDP_SC_REQ	= (1 << 1),	/* state change (on/off) requested */
	DDP_ON		= (1 << 2),	/* DDP is turned on */
	DDP_BUF0_ACTIVE	= (1 << 3),	/* buffer 0 in use (not invalidated) */
	DDP_BUF1_ACTIVE	= (1 << 4),	/* buffer 1 in use (not invalidated) */
	DDP_TASK_ACTIVE = (1 << 5),	/* requeue task is queued / running */
	DDP_DEAD	= (1 << 6),	/* toepcb is shutting down */
};

struct sockopt;
struct offload_settings;

struct ofld_tx_sdesc {
	uint32_t plen;		/* payload length */
	uint8_t tx_credits;	/* firmware tx credits (unit is 16B) */
	void *iv_buffer;	/* optional buffer holding IVs for TLS */
};

struct ppod_region {
	u_int pr_start;
	u_int pr_len;
	u_int pr_page_shift[4];
	uint32_t pr_tag_mask;		/* hardware tagmask for this region. */
	uint32_t pr_invalid_bit;	/* OR with this to invalidate tag. */
	uint32_t pr_alias_mask;		/* AND with tag to get alias bits. */
	u_int pr_alias_shift;		/* shift this much for first alias bit. */
	vmem_t *pr_arena;
};

struct ppod_reservation {
	struct ppod_region *prsv_pr;
	uint32_t prsv_tag;		/* Full tag: pgsz, alias, tag, color */
	u_int prsv_nppods;
};

struct pageset {
	TAILQ_ENTRY(pageset) link;
	vm_page_t *pages;
	int npages;
	int flags;
	int offset;		/* offset in first page */
	int len;
	struct ppod_reservation prsv;
	struct vmspace *vm;
	vm_offset_t start;
	u_int vm_timestamp;
};

TAILQ_HEAD(pagesetq, pageset);

#define	PS_WIRED		0x0001	/* Pages wired rather than held. */
#define	PS_PPODS_WRITTEN	0x0002	/* Page pods written to the card. */

#define	EXT_FLAG_AIOTX		EXT_FLAG_VENDOR1

#define	IS_AIOTX_MBUF(m)						\
	((m)->m_flags & M_EXT && (m)->m_ext.ext_flags & EXT_FLAG_AIOTX)

struct ddp_buffer {
	struct pageset *ps;

	struct kaiocb *job;
	int cancel_pending;
};

struct ddp_pcb {
	u_int flags;
	struct ddp_buffer db[2];
	TAILQ_HEAD(, pageset) cached_pagesets;
	TAILQ_HEAD(, kaiocb) aiojobq;
	u_int waiting_count;
	u_int active_count;
	u_int cached_count;
	int active_id;	/* the currently active DDP buffer */
	struct task requeue_task;
	struct kaiocb *queueing;
	struct mtx lock;
};

struct aiotx_buffer {
	struct pageset ps;
	struct kaiocb *job;
	int refcount;
};

struct toepcb {
	TAILQ_ENTRY(toepcb) link; /* toep_list */
	u_int flags;		/* miscellaneous flags */
	int refcount;
	struct tom_data *td;
	struct inpcb *inp;	/* backpointer to host stack's PCB */
	struct vnet *vnet;
	struct vi_info *vi;	/* virtual interface */
	struct sge_wrq *ofld_txq;
	struct sge_ofld_rxq *ofld_rxq;
	struct sge_wrq *ctrlq;
	struct l2t_entry *l2te;	/* L2 table entry used by this connection */
	struct clip_entry *ce;	/* CLIP table entry used by this tid */
	int tid;		/* Connection identifier */
	int tc_idx;		/* traffic class that this tid is bound to */

	/* tx credit handling */
	u_int tx_total;		/* total tx WR credits (in 16B units) */
	u_int tx_credits;	/* tx WR credits (in 16B units) available */
	u_int tx_nocompl;	/* tx WR credits since last compl request */
	u_int plen_nocompl;	/* payload since last compl request */

	/* rx credit handling */
	u_int sb_cc;		/* last noted value of so_rcv->sb_cc */
	int rx_credits;		/* rx credits (in bytes) to be returned to hw */

	u_int ulp_mode;	/* ULP mode */
	void *ulpcb;
	void *ulpcb2;
	struct mbufq ulp_pduq;	/* PDUs waiting to be sent out. */
	struct mbufq ulp_pdu_reclaimq;

	struct ddp_pcb ddp;
	struct tls_ofld_info tls;

	TAILQ_HEAD(, kaiocb) aiotx_jobq;
	struct task aiotx_task;
	bool aiotx_task_active;

	/* Tx software descriptor */
	uint8_t txsd_total;
	uint8_t txsd_pidx;
	uint8_t txsd_cidx;
	uint8_t txsd_avail;
	struct ofld_tx_sdesc txsd[];
};

#define	DDP_LOCK(toep)		mtx_lock(&(toep)->ddp.lock)
#define	DDP_UNLOCK(toep)	mtx_unlock(&(toep)->ddp.lock)
#define	DDP_ASSERT_LOCKED(toep)	mtx_assert(&(toep)->ddp.lock, MA_OWNED)

struct flowc_tx_params {
	uint32_t snd_nxt;
	uint32_t rcv_nxt;
	unsigned int snd_space;
	unsigned int mss;
};

/*
 * Compressed state for embryonic connections for a listener.
 */
struct synq_entry {
	struct listen_ctx *lctx;	/* backpointer to listen ctx */
	struct mbuf *syn;
	int flags;			/* same as toepcb's tp_flags */
	volatile int ok_to_respond;
	volatile u_int refcnt;
	int tid;
	uint32_t iss;
	uint32_t irs;
	uint32_t ts;
	uint16_t txqid;
	uint16_t rxqid;
	uint16_t l2e_idx;
	uint16_t ulp_mode;
	uint16_t rcv_bufsize;
	__be16 tcp_opt; /* from cpl_pass_establish */
	struct toepcb *toep;
};

/* listen_ctx flags */
#define LCTX_RPL_PENDING 1	/* waiting for a CPL_PASS_OPEN_RPL */

struct listen_ctx {
	LIST_ENTRY(listen_ctx) link;	/* listen hash linkage */
	volatile int refcount;
	int stid;
	struct stid_region stid_region;
	int flags;
	struct inpcb *inp;		/* listening socket's inp */
	struct vnet *vnet;
	struct sge_wrq *ctrlq;
	struct sge_ofld_rxq *ofld_rxq;
	struct clip_entry *ce;
};

struct tom_data {
	struct toedev tod;

	/* toepcb's associated with this TOE device */
	struct mtx toep_list_lock;
	TAILQ_HEAD(, toepcb) toep_list;

	struct mtx lctx_hash_lock;
	LIST_HEAD(, listen_ctx) *listen_hash;
	u_long listen_mask;
	int lctx_count;		/* # of lctx in the hash table */

	struct ppod_region pr;

	/* WRs that will not be sent to the chip because L2 resolution failed */
	struct mtx unsent_wr_lock;
	STAILQ_HEAD(, wrqe) unsent_wr_list;
	struct task reclaim_wr_resources;
};

static inline struct tom_data *
tod_td(struct toedev *tod)
{

	return (__containerof(tod, struct tom_data, tod));
}

static inline struct adapter *
td_adapter(struct tom_data *td)
{

	return (td->tod.tod_softc);
}

static inline void
set_mbuf_ulp_submode(struct mbuf *m, uint8_t ulp_submode)
{

	M_ASSERTPKTHDR(m);
	m->m_pkthdr.PH_per.eight[0] = ulp_submode;
}

static inline uint8_t
mbuf_ulp_submode(struct mbuf *m)
{

	M_ASSERTPKTHDR(m);
	return (m->m_pkthdr.PH_per.eight[0]);
}

/* t4_tom.c */
struct toepcb *alloc_toepcb(struct vi_info *, int, int, int);
struct toepcb *hold_toepcb(struct toepcb *);
void free_toepcb(struct toepcb *);
void offload_socket(struct socket *, struct toepcb *);
void undo_offload_socket(struct socket *);
void final_cpl_received(struct toepcb *);
void insert_tid(struct adapter *, int, void *, int);
void *lookup_tid(struct adapter *, int);
void update_tid(struct adapter *, int, void *);
void remove_tid(struct adapter *, int, int);
int find_best_mtu_idx(struct adapter *, struct in_conninfo *,
    struct offload_settings *);
u_long select_rcv_wnd(struct socket *);
int select_rcv_wscale(void);
uint64_t calc_opt0(struct socket *, struct vi_info *, struct l2t_entry *,
    int, int, int, int, struct offload_settings *);
uint64_t select_ntuple(struct vi_info *, struct l2t_entry *);
int select_ulp_mode(struct socket *, struct adapter *,
    struct offload_settings *);
void set_ulp_mode(struct toepcb *, int);
int negative_advice(int);

/* t4_connect.c */
void t4_init_connect_cpl_handlers(void);
void t4_uninit_connect_cpl_handlers(void);
int t4_connect(struct toedev *, struct socket *, struct rtentry *,
    struct sockaddr *);
void act_open_failure_cleanup(struct adapter *, u_int, u_int);

/* t4_listen.c */
void t4_init_listen_cpl_handlers(void);
void t4_uninit_listen_cpl_handlers(void);
int t4_listen_start(struct toedev *, struct tcpcb *);
int t4_listen_stop(struct toedev *, struct tcpcb *);
void t4_syncache_added(struct toedev *, void *);
void t4_syncache_removed(struct toedev *, void *);
int t4_syncache_respond(struct toedev *, void *, struct mbuf *);
int do_abort_req_synqe(struct sge_iq *, const struct rss_header *,
    struct mbuf *);
int do_abort_rpl_synqe(struct sge_iq *, const struct rss_header *,
    struct mbuf *);
void t4_offload_socket(struct toedev *, void *, struct socket *);
void synack_failure_cleanup(struct adapter *, int);

/* t4_cpl_io.c */
void aiotx_init_toep(struct toepcb *);
int t4_aio_queue_aiotx(struct socket *, struct kaiocb *);
void t4_init_cpl_io_handlers(void);
void t4_uninit_cpl_io_handlers(void);
void send_abort_rpl(struct adapter *, struct sge_wrq *, int , int);
void send_flowc_wr(struct toepcb *, struct flowc_tx_params *);
void send_reset(struct adapter *, struct toepcb *, uint32_t);
int send_rx_credits(struct adapter *, struct toepcb *, int);
void send_rx_modulate(struct adapter *, struct toepcb *);
void make_established(struct toepcb *, uint32_t, uint32_t, uint16_t);
int t4_close_conn(struct adapter *, struct toepcb *);
void t4_rcvd(struct toedev *, struct tcpcb *);
void t4_rcvd_locked(struct toedev *, struct tcpcb *);
int t4_tod_output(struct toedev *, struct tcpcb *);
int t4_send_fin(struct toedev *, struct tcpcb *);
int t4_send_rst(struct toedev *, struct tcpcb *);
void t4_set_tcb_field(struct adapter *, struct sge_wrq *, struct toepcb *,
    uint16_t, uint64_t, uint64_t, int, int);
void t4_push_frames(struct adapter *sc, struct toepcb *toep, int drop);
void t4_push_pdus(struct adapter *sc, struct toepcb *toep, int drop);

/* t4_ddp.c */
int t4_init_ppod_region(struct ppod_region *, struct t4_range *, u_int,
    const char *);
void t4_free_ppod_region(struct ppod_region *);
int t4_alloc_page_pods_for_ps(struct ppod_region *, struct pageset *);
int t4_alloc_page_pods_for_buf(struct ppod_region *, vm_offset_t, int,
    struct ppod_reservation *);
int t4_write_page_pods_for_ps(struct adapter *, struct sge_wrq *, int,
    struct pageset *);
int t4_write_page_pods_for_buf(struct adapter *, struct sge_wrq *, int tid,
    struct ppod_reservation *, vm_offset_t, int);
void t4_free_page_pods(struct ppod_reservation *);
int t4_soreceive_ddp(struct socket *, struct sockaddr **, struct uio *,
    struct mbuf **, struct mbuf **, int *);
int t4_aio_queue_ddp(struct socket *, struct kaiocb *);
void t4_ddp_mod_load(void);
void t4_ddp_mod_unload(void);
void ddp_assert_empty(struct toepcb *);
void ddp_init_toep(struct toepcb *);
void ddp_uninit_toep(struct toepcb *);
void ddp_queue_toep(struct toepcb *);
void release_ddp_resources(struct toepcb *toep);
void handle_ddp_close(struct toepcb *, struct tcpcb *, uint32_t);
void handle_ddp_indicate(struct toepcb *);
void insert_ddp_data(struct toepcb *, uint32_t);
const struct offload_settings *lookup_offload_policy(struct adapter *, int,
    struct mbuf *, uint16_t, struct inpcb *);

/* t4_tls.c */
bool can_tls_offload(struct adapter *);
int t4_ctloutput_tls(struct socket *, struct sockopt *);
void t4_push_tls_records(struct adapter *, struct toepcb *, int);
void t4_tls_mod_load(void);
void t4_tls_mod_unload(void);
void tls_establish(struct toepcb *);
void tls_init_toep(struct toepcb *);
int tls_rx_key(struct toepcb *);
void tls_stop_handshake_timer(struct toepcb *);
int tls_tx_key(struct toepcb *);
void tls_uninit_toep(struct toepcb *);

#endif
