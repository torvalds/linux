/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

/*
 * Defintions for the Atheros Wireless LAN controller driver.
 */
#ifndef _DEV_ATH_ATHVAR_H
#define _DEV_ATH_ATHVAR_H

#include <machine/atomic.h>

#include <dev/ath/ath_hal/ah.h>
#include <dev/ath/ath_hal/ah_desc.h>
#include <net80211/ieee80211_radiotap.h>
#include <dev/ath/if_athioctl.h>
#include <dev/ath/if_athrate.h>
#ifdef	ATH_DEBUG_ALQ
#include <dev/ath/if_ath_alq.h>
#endif

#define	ATH_TIMEOUT		1000

/*
 * There is a separate TX ath_buf pool for management frames.
 * This ensures that management frames such as probe responses
 * and BAR frames can be transmitted during periods of high
 * TX activity.
 */
#define	ATH_MGMT_TXBUF		32

/*
 * 802.11n requires more TX and RX buffers to do AMPDU.
 */
#ifdef	ATH_ENABLE_11N
#define	ATH_TXBUF	512
#define	ATH_RXBUF	512
#endif

#ifndef ATH_RXBUF
#define	ATH_RXBUF	40		/* number of RX buffers */
#endif
#ifndef ATH_TXBUF
#define	ATH_TXBUF	200		/* number of TX buffers */
#endif
#define	ATH_BCBUF	4		/* number of beacon buffers */

#define	ATH_TXDESC	10		/* number of descriptors per buffer */
#define	ATH_TXMAXTRY	11		/* max number of transmit attempts */
#define	ATH_TXMGTTRY	4		/* xmit attempts for mgt/ctl frames */
#define	ATH_TXINTR_PERIOD 5		/* max number of batched tx descriptors */

#define	ATH_BEACON_AIFS_DEFAULT	 1	/* default aifs for ap beacon q */
#define	ATH_BEACON_CWMIN_DEFAULT 0	/* default cwmin for ap beacon q */
#define	ATH_BEACON_CWMAX_DEFAULT 0	/* default cwmax for ap beacon q */

/*
 * The following bits can be set during the PCI (and perhaps non-PCI
 * later) device probe path.
 *
 * It controls some of the driver and HAL behaviour.
 */

#define	ATH_PCI_CUS198		0x0001
#define	ATH_PCI_CUS230		0x0002
#define	ATH_PCI_CUS217		0x0004
#define	ATH_PCI_CUS252		0x0008
#define	ATH_PCI_WOW		0x0010
#define	ATH_PCI_BT_ANT_DIV	0x0020
#define	ATH_PCI_D3_L1_WAR	0x0040
#define	ATH_PCI_AR9565_1ANT	0x0080
#define	ATH_PCI_AR9565_2ANT	0x0100
#define	ATH_PCI_NO_PLL_PWRSAVE	0x0200
#define	ATH_PCI_KILLER		0x0400

/*
 * The key cache is used for h/w cipher state and also for
 * tracking station state such as the current tx antenna.
 * We also setup a mapping table between key cache slot indices
 * and station state to short-circuit node lookups on rx.
 * Different parts have different size key caches.  We handle
 * up to ATH_KEYMAX entries (could dynamically allocate state).
 */
#define	ATH_KEYMAX	128		/* max key cache size we handle */
#define	ATH_KEYBYTES	(ATH_KEYMAX/NBBY)	/* storage space in bytes */

struct taskqueue;
struct kthread;
struct ath_buf;

#define	ATH_TID_MAX_BUFS	(2 * IEEE80211_AGGR_BAWMAX)

/*
 * Per-TID state
 *
 * Note that TID 16 (WME_NUM_TID+1) is for handling non-QoS frames.
 */
struct ath_tid {
	TAILQ_HEAD(,ath_buf)	tid_q;		/* pending buffers */
	struct ath_node		*an;		/* pointer to parent */
	int			tid;		/* tid */
	int			ac;		/* which AC gets this traffic */
	int			hwq_depth;	/* how many buffers are on HW */
	u_int			axq_depth;	/* SW queue depth */

	struct {
		TAILQ_HEAD(,ath_buf)	tid_q;		/* filtered queue */
		u_int			axq_depth;	/* SW queue depth */
	} filtq;

	/*
	 * Entry on the ath_txq; when there's traffic
	 * to send
	 */
	TAILQ_ENTRY(ath_tid)	axq_qelem;
	int			sched;
	int			paused;	/* >0 if the TID has been paused */

	/*
	 * These are flags - perhaps later collapse
	 * down to a single uint32_t ?
	 */
	int			addba_tx_pending;	/* TX ADDBA pending */
	int			bar_wait;	/* waiting for BAR */
	int			bar_tx;		/* BAR TXed */
	int			isfiltered;	/* is this node currently filtered */

	/*
	 * Is the TID being cleaned up after a transition
	 * from aggregation to non-aggregation?
	 * When this is set to 1, this TID will be paused
	 * and no further traffic will be queued until all
	 * the hardware packets pending for this TID have been
	 * TXed/completed; at which point (non-aggregation)
	 * traffic will resume being TXed.
	 */
	int			cleanup_inprogress;
	/*
	 * How many hardware-queued packets are
	 * waiting to be cleaned up.
	 * This is only valid if cleanup_inprogress is 1.
	 */
	int			incomp;

	/*
	 * The following implements a ring representing
	 * the frames in the current BAW.
	 * To avoid copying the array content each time
	 * the BAW is moved, the baw_head/baw_tail point
	 * to the current BAW begin/end; when the BAW is
	 * shifted the head/tail of the array are also
	 * appropriately shifted.
	 */
	/* active tx buffers, beginning at current BAW */
	struct ath_buf		*tx_buf[ATH_TID_MAX_BUFS];
	/* where the baw head is in the array */
	int			baw_head;
	/* where the BAW tail is in the array */
	int			baw_tail;
};

/* driver-specific node state */
struct ath_node {
	struct ieee80211_node an_node;	/* base class */
	u_int8_t	an_mgmtrix;	/* min h/w rate index */
	u_int8_t	an_mcastrix;	/* mcast h/w rate index */
	uint32_t	an_is_powersave;	/* node is sleeping */
	uint32_t	an_stack_psq;		/* net80211 psq isn't empty */
	uint32_t	an_tim_set;		/* TIM has been set */
	struct ath_buf	*an_ff_buf[WME_NUM_AC]; /* ff staging area */
	struct ath_tid	an_tid[IEEE80211_TID_SIZE];	/* per-TID state */
	char		an_name[32];	/* eg "wlan0_a1" */
	struct mtx	an_mtx;		/* protecting the rate control state */
	uint32_t	an_swq_depth;	/* how many SWQ packets for this
					   node */
	int			clrdmask;	/* has clrdmask been set */
	uint32_t	an_leak_count;	/* How many frames to leak during pause */
	/* variable-length rate control state follows */
};
#define	ATH_NODE(ni)	((struct ath_node *)(ni))
#define	ATH_NODE_CONST(ni)	((const struct ath_node *)(ni))

#define ATH_RSSI_LPF_LEN	10
#define ATH_RSSI_DUMMY_MARKER	0x127
#define ATH_EP_MUL(x, mul)	((x) * (mul))
#define ATH_RSSI_IN(x)		(ATH_EP_MUL((x), HAL_RSSI_EP_MULTIPLIER))
#define ATH_LPF_RSSI(x, y, len) \
    ((x != ATH_RSSI_DUMMY_MARKER) ? (((x) * ((len) - 1) + (y)) / (len)) : (y))
#define ATH_RSSI_LPF(x, y) do {						\
    if ((y) >= -20)							\
    	x = ATH_LPF_RSSI((x), ATH_RSSI_IN((y)), ATH_RSSI_LPF_LEN);	\
} while (0)
#define	ATH_EP_RND(x,mul) \
	((((x)%(mul)) >= ((mul)/2)) ? ((x) + ((mul) - 1)) / (mul) : (x)/(mul))
#define	ATH_RSSI(x)		ATH_EP_RND(x, HAL_RSSI_EP_MULTIPLIER)

typedef enum {
	ATH_BUFTYPE_NORMAL	= 0,
	ATH_BUFTYPE_MGMT	= 1,
} ath_buf_type_t;

struct ath_buf {
	TAILQ_ENTRY(ath_buf)	bf_list;
	struct ath_buf *	bf_next;	/* next buffer in the aggregate */
	int			bf_nseg;
	HAL_STATUS		bf_rxstatus;
	uint16_t		bf_flags;	/* status flags (below) */
	uint16_t		bf_descid;	/* 16 bit descriptor ID */
	struct ath_desc		*bf_desc;	/* virtual addr of desc */
	struct ath_desc_status	bf_status;	/* tx/rx status */
	bus_addr_t		bf_daddr;	/* physical addr of desc */
	bus_dmamap_t		bf_dmamap;	/* DMA map for mbuf chain */
	struct mbuf		*bf_m;		/* mbuf for buf */
	struct ieee80211_node	*bf_node;	/* pointer to the node */
	struct ath_desc		*bf_lastds;	/* last descriptor for comp status */
	struct ath_buf		*bf_last;	/* last buffer in aggregate, or self for non-aggregate */
	bus_size_t		bf_mapsize;
#define	ATH_MAX_SCATTER		ATH_TXDESC	/* max(tx,rx,beacon) desc's */
	bus_dma_segment_t	bf_segs[ATH_MAX_SCATTER];
	uint32_t		bf_nextfraglen;	/* length of next fragment */

	/* Completion function to call on TX complete (fail or not) */
	/*
	 * "fail" here is set to 1 if the queue entries were removed
	 * through a call to ath_tx_draintxq().
	 */
	void(* bf_comp) (struct ath_softc *sc, struct ath_buf *bf, int fail);

	/* This state is kept to support software retries and aggregation */
	struct {
		uint16_t bfs_seqno;	/* sequence number of this packet */
		uint16_t bfs_ndelim;	/* number of delims for padding */

		uint8_t bfs_retries;	/* retry count */
		uint8_t bfs_tid;	/* packet TID (or TID_MAX for no QoS) */
		uint8_t bfs_nframes;	/* number of frames in aggregate */
		uint8_t bfs_pri;	/* packet AC priority */
		uint8_t bfs_tx_queue;	/* destination hardware TX queue */

		u_int32_t bfs_aggr:1,		/* part of aggregate? */
		    bfs_aggrburst:1,	/* part of aggregate burst? */
		    bfs_isretried:1,	/* retried frame? */
		    bfs_dobaw:1,	/* actually check against BAW? */
		    bfs_addedbaw:1,	/* has been added to the BAW */
		    bfs_shpream:1,	/* use short preamble */
		    bfs_istxfrag:1,	/* is fragmented */
		    bfs_ismrr:1,	/* do multi-rate TX retry */
		    bfs_doprot:1,	/* do RTS/CTS based protection */
		    bfs_doratelookup:1;	/* do rate lookup before each TX */

		/*
		 * These fields are passed into the
		 * descriptor setup functions.
		 */

		/* Make this an 8 bit value? */
		HAL_PKT_TYPE bfs_atype;	/* packet type */

		uint32_t bfs_pktlen;	/* length of this packet */

		uint16_t bfs_hdrlen;	/* length of this packet header */
		uint16_t bfs_al;	/* length of aggregate */

		uint16_t bfs_txflags;	/* HAL (tx) descriptor flags */
		uint8_t bfs_txrate0;	/* first TX rate */
		uint8_t bfs_try0;		/* first try count */

		uint16_t bfs_txpower;	/* tx power */
		uint8_t bfs_ctsrate0;	/* Non-zero - use this as ctsrate */
		uint8_t bfs_ctsrate;	/* CTS rate */

		/* 16 bit? */
		int32_t bfs_keyix;		/* crypto key index */
		int32_t bfs_txantenna;	/* TX antenna config */

		/* Make this an 8 bit value? */
		enum ieee80211_protmode bfs_protmode;

		/* 16 bit? */
		uint32_t bfs_ctsduration;	/* CTS duration (pre-11n NICs) */
		struct ath_rc_series bfs_rc[ATH_RC_NUM];	/* non-11n TX series */
	} bf_state;
};
typedef TAILQ_HEAD(ath_bufhead_s, ath_buf) ath_bufhead;

#define	ATH_BUF_MGMT	0x00000001	/* (tx) desc is a mgmt desc */
#define	ATH_BUF_BUSY	0x00000002	/* (tx) desc owned by h/w */
#define	ATH_BUF_FIFOEND	0x00000004
#define	ATH_BUF_FIFOPTR	0x00000008
#define	ATH_BUF_TOA_PROBE	0x00000010	/* ToD/ToA exchange probe */

#define	ATH_BUF_FLAGS_CLONE	(ATH_BUF_MGMT | ATH_BUF_TOA_PROBE)

/*
 * DMA state for tx/rx descriptors.
 */
struct ath_descdma {
	const char*		dd_name;
	struct ath_desc		*dd_desc;	/* descriptors */
	int			dd_descsize;	/* size of single descriptor */
	bus_addr_t		dd_desc_paddr;	/* physical addr of dd_desc */
	bus_size_t		dd_desc_len;	/* size of dd_desc */
	bus_dma_segment_t	dd_dseg;
	bus_dma_tag_t		dd_dmat;	/* bus DMA tag */
	bus_dmamap_t		dd_dmamap;	/* DMA map for descriptors */
	struct ath_buf		*dd_bufptr;	/* associated buffers */
};

/*
 * Data transmit queue state.  One of these exists for each
 * hardware transmit queue.  Packets sent to us from above
 * are assigned to queues based on their priority.  Not all
 * devices support a complete set of hardware transmit queues.
 * For those devices the array sc_ac2q will map multiple
 * priorities to fewer hardware queues (typically all to one
 * hardware queue).
 */
struct ath_txq {
	struct ath_softc	*axq_softc;	/* Needed for scheduling */
	u_int			axq_qnum;	/* hardware q number */
#define	ATH_TXQ_SWQ	(HAL_NUM_TX_QUEUES+1)	/* qnum for s/w only queue */
	u_int			axq_ac;		/* WME AC */
	u_int			axq_flags;
//#define	ATH_TXQ_PUTPENDING	0x0001		/* ath_hal_puttxbuf pending */
#define	ATH_TXQ_PUTRUNNING	0x0002		/* ath_hal_puttxbuf has been called */
	u_int			axq_depth;	/* queue depth (stat only) */
	u_int			axq_aggr_depth;	/* how many aggregates are queued */
	u_int			axq_intrcnt;	/* interrupt count */
	u_int32_t		*axq_link;	/* link ptr in last TX desc */
	TAILQ_HEAD(axq_q_s, ath_buf)	axq_q;		/* transmit queue */
	struct mtx		axq_lock;	/* lock on q and link */

	/*
	 * This is the FIFO staging buffer when doing EDMA.
	 *
	 * For legacy chips, we just push the head pointer to
	 * the hardware and we ignore this list.
	 *
	 * For EDMA, the staging buffer is treated as normal;
	 * when it's time to push a list of frames to the hardware
	 * we move that list here and we stamp buffers with
	 * flags to identify the beginning/end of that particular
	 * FIFO entry.
	 */
	struct {
		TAILQ_HEAD(axq_q_f_s, ath_buf)	axq_q;
		u_int				axq_depth;	/* how many frames (1 per legacy, 1 per A-MPDU list) are in the FIFO queue */
	} fifo;
	u_int			axq_fifo_depth;	/* how many FIFO slots are active */

	/*
	 * XXX the holdingbf field is protected by the TXBUF lock
	 * for now, NOT the TXQ lock.
	 *
	 * Architecturally, it would likely be better to move
	 * the holdingbf field to a separate array in ath_softc
	 * just to highlight that it's not protected by the normal
	 * TX path lock.
	 */
	struct ath_buf		*axq_holdingbf;	/* holding TX buffer */
	char			axq_name[12];	/* e.g. "ath0_txq4" */

	/* Per-TID traffic queue for software -> hardware TX */
	/*
	 * This is protected by the general TX path lock, not (for now)
	 * by the TXQ lock.
	 */
	TAILQ_HEAD(axq_t_s,ath_tid)	axq_tidq;
};

#define	ATH_TXQ_LOCK_INIT(_sc, _tq) do { \
	    snprintf((_tq)->axq_name, sizeof((_tq)->axq_name), "%s_txq%u", \
	      device_get_nameunit((_sc)->sc_dev), (_tq)->axq_qnum); \
	    mtx_init(&(_tq)->axq_lock, (_tq)->axq_name, NULL, MTX_DEF); \
	} while (0)
#define	ATH_TXQ_LOCK_DESTROY(_tq)	mtx_destroy(&(_tq)->axq_lock)
#define	ATH_TXQ_LOCK(_tq)		mtx_lock(&(_tq)->axq_lock)
#define	ATH_TXQ_UNLOCK(_tq)		mtx_unlock(&(_tq)->axq_lock)
#define	ATH_TXQ_LOCK_ASSERT(_tq)	mtx_assert(&(_tq)->axq_lock, MA_OWNED)
#define	ATH_TXQ_UNLOCK_ASSERT(_tq)	mtx_assert(&(_tq)->axq_lock,	\
					    MA_NOTOWNED)


#define	ATH_NODE_LOCK(_an)		mtx_lock(&(_an)->an_mtx)
#define	ATH_NODE_UNLOCK(_an)		mtx_unlock(&(_an)->an_mtx)
#define	ATH_NODE_LOCK_ASSERT(_an)	mtx_assert(&(_an)->an_mtx, MA_OWNED)
#define	ATH_NODE_UNLOCK_ASSERT(_an)	mtx_assert(&(_an)->an_mtx,	\
					    MA_NOTOWNED)

/*
 * These are for the hardware queue.
 */
#define ATH_TXQ_INSERT_HEAD(_tq, _elm, _field) do { \
	TAILQ_INSERT_HEAD(&(_tq)->axq_q, (_elm), _field); \
	(_tq)->axq_depth++; \
} while (0)
#define ATH_TXQ_INSERT_TAIL(_tq, _elm, _field) do { \
	TAILQ_INSERT_TAIL(&(_tq)->axq_q, (_elm), _field); \
	(_tq)->axq_depth++; \
} while (0)
#define ATH_TXQ_REMOVE(_tq, _elm, _field) do { \
	TAILQ_REMOVE(&(_tq)->axq_q, _elm, _field); \
	(_tq)->axq_depth--; \
} while (0)
#define	ATH_TXQ_FIRST(_tq)		TAILQ_FIRST(&(_tq)->axq_q)
#define	ATH_TXQ_LAST(_tq, _field)	TAILQ_LAST(&(_tq)->axq_q, _field)

/*
 * These are for the TID software queue.
 */
#define ATH_TID_INSERT_HEAD(_tq, _elm, _field) do { \
	TAILQ_INSERT_HEAD(&(_tq)->tid_q, (_elm), _field); \
	(_tq)->axq_depth++; \
	(_tq)->an->an_swq_depth++; \
} while (0)
#define ATH_TID_INSERT_TAIL(_tq, _elm, _field) do { \
	TAILQ_INSERT_TAIL(&(_tq)->tid_q, (_elm), _field); \
	(_tq)->axq_depth++; \
	(_tq)->an->an_swq_depth++; \
} while (0)
#define ATH_TID_REMOVE(_tq, _elm, _field) do { \
	TAILQ_REMOVE(&(_tq)->tid_q, _elm, _field); \
	(_tq)->axq_depth--; \
	(_tq)->an->an_swq_depth--; \
} while (0)
#define	ATH_TID_FIRST(_tq)		TAILQ_FIRST(&(_tq)->tid_q)
#define	ATH_TID_LAST(_tq, _field)	TAILQ_LAST(&(_tq)->tid_q, _field)

/*
 * These are for the TID filtered frame queue
 */
#define ATH_TID_FILT_INSERT_HEAD(_tq, _elm, _field) do { \
	TAILQ_INSERT_HEAD(&(_tq)->filtq.tid_q, (_elm), _field); \
	(_tq)->axq_depth++; \
	(_tq)->an->an_swq_depth++; \
} while (0)
#define ATH_TID_FILT_INSERT_TAIL(_tq, _elm, _field) do { \
	TAILQ_INSERT_TAIL(&(_tq)->filtq.tid_q, (_elm), _field); \
	(_tq)->axq_depth++; \
	(_tq)->an->an_swq_depth++; \
} while (0)
#define ATH_TID_FILT_REMOVE(_tq, _elm, _field) do { \
	TAILQ_REMOVE(&(_tq)->filtq.tid_q, _elm, _field); \
	(_tq)->axq_depth--; \
	(_tq)->an->an_swq_depth--; \
} while (0)
#define	ATH_TID_FILT_FIRST(_tq)		TAILQ_FIRST(&(_tq)->filtq.tid_q)
#define	ATH_TID_FILT_LAST(_tq, _field)	TAILQ_LAST(&(_tq)->filtq.tid_q,_field)

struct ath_vap {
	struct ieee80211vap av_vap;	/* base class */
	int		av_bslot;	/* beacon slot index */
	struct ath_buf	*av_bcbuf;	/* beacon buffer */
	struct ath_txq	av_mcastq;	/* buffered mcast s/w queue */

	void		(*av_recv_mgmt)(struct ieee80211_node *,
				struct mbuf *, int,
				const struct ieee80211_rx_stats *, int, int);
	int		(*av_newstate)(struct ieee80211vap *,
				enum ieee80211_state, int);
	void		(*av_bmiss)(struct ieee80211vap *);
	void		(*av_node_ps)(struct ieee80211_node *, int);
	int		(*av_set_tim)(struct ieee80211_node *, int);
	void		(*av_recv_pspoll)(struct ieee80211_node *,
				struct mbuf *);
	struct ieee80211_quiet_ie	quiet_ie;
};
#define	ATH_VAP(vap)	((struct ath_vap *)(vap))

struct taskqueue;
struct ath_tx99;

/*
 * Whether to reset the TX/RX queue with or without
 * a queue flush.
 */
typedef enum {
	ATH_RESET_DEFAULT = 0,
	ATH_RESET_NOLOSS = 1,
	ATH_RESET_FULL = 2,
} ATH_RESET_TYPE;

struct ath_rx_methods {
	void		(*recv_sched_queue)(struct ath_softc *sc,
			    HAL_RX_QUEUE q, int dosched);
	void		(*recv_sched)(struct ath_softc *sc, int dosched);
	void		(*recv_stop)(struct ath_softc *sc, int dodelay);
	int		(*recv_start)(struct ath_softc *sc);
	void		(*recv_flush)(struct ath_softc *sc);
	void		(*recv_tasklet)(void *arg, int npending);
	int		(*recv_rxbuf_init)(struct ath_softc *sc,
			    struct ath_buf *bf);
	int		(*recv_setup)(struct ath_softc *sc);
	int		(*recv_teardown)(struct ath_softc *sc);
};

/*
 * Represent the current state of the RX FIFO.
 */
struct ath_rx_edma {
	struct ath_buf	**m_fifo;
	int		m_fifolen;
	int		m_fifo_head;
	int		m_fifo_tail;
	int		m_fifo_depth;
	struct mbuf	*m_rxpending;
	struct ath_buf	*m_holdbf;
};

struct ath_tx_edma_fifo {
	struct ath_buf	**m_fifo;
	int		m_fifolen;
	int		m_fifo_head;
	int		m_fifo_tail;
	int		m_fifo_depth;
};

struct ath_tx_methods {
	int		(*xmit_setup)(struct ath_softc *sc);
	int		(*xmit_teardown)(struct ath_softc *sc);
	void		(*xmit_attach_comp_func)(struct ath_softc *sc);

	void		(*xmit_dma_restart)(struct ath_softc *sc,
			    struct ath_txq *txq);
	void		(*xmit_handoff)(struct ath_softc *sc,
			    struct ath_txq *txq, struct ath_buf *bf);
	void		(*xmit_drain)(struct ath_softc *sc,
			    ATH_RESET_TYPE reset_type);
};

struct ath_softc {
	struct ieee80211com	sc_ic;
	struct ath_stats	sc_stats;	/* device statistics */
	struct ath_tx_aggr_stats	sc_aggr_stats;
	struct ath_intr_stats	sc_intr_stats;
	uint64_t		sc_debug;
	uint64_t		sc_ktrdebug;
	int			sc_nvaps;	/* # vaps */
	int			sc_nstavaps;	/* # station vaps */
	int			sc_nmeshvaps;	/* # mbss vaps */
	u_int8_t		sc_hwbssidmask[IEEE80211_ADDR_LEN];
	u_int8_t		sc_nbssid0;	/* # vap's using base mac */
	uint32_t		sc_bssidmask;	/* bssid mask */

	struct ath_rx_methods	sc_rx;
	struct ath_rx_edma	sc_rxedma[HAL_NUM_RX_QUEUES];	/* HP/LP queues */
	ath_bufhead		sc_rx_rxlist[HAL_NUM_RX_QUEUES];	/* deferred RX completion */
	struct ath_tx_methods	sc_tx;
	struct ath_tx_edma_fifo	sc_txedma[HAL_NUM_TX_QUEUES];

	/*
	 * This is (currently) protected by the TX queue lock;
	 * it should migrate to a separate lock later
	 * so as to minimise contention.
	 */
	ath_bufhead		sc_txbuf_list;

	int			sc_rx_statuslen;
	int			sc_tx_desclen;
	int			sc_tx_statuslen;
	int			sc_tx_nmaps;	/* Number of TX maps */
	int			sc_edma_bufsize;
	int			sc_rx_stopped;	/* XXX only for EDMA */
	int			sc_rx_resetted;	/* XXX only for EDMA */

	void 			(*sc_node_cleanup)(struct ieee80211_node *);
	void 			(*sc_node_free)(struct ieee80211_node *);
	device_t		sc_dev;
	HAL_BUS_TAG		sc_st;		/* bus space tag */
	HAL_BUS_HANDLE		sc_sh;		/* bus space handle */
	bus_dma_tag_t		sc_dmat;	/* bus DMA tag */
	struct mtx		sc_mtx;		/* master lock (recursive) */
	struct mtx		sc_pcu_mtx;	/* PCU access mutex */
	char			sc_pcu_mtx_name[32];
	struct mtx		sc_rx_mtx;	/* RX access mutex */
	char			sc_rx_mtx_name[32];
	struct mtx		sc_tx_mtx;	/* TX handling/comp mutex */
	char			sc_tx_mtx_name[32];
	struct mtx		sc_tx_ic_mtx;	/* TX queue mutex */
	char			sc_tx_ic_mtx_name[32];
	struct taskqueue	*sc_tq;		/* private task queue */
	struct ath_hal		*sc_ah;		/* Atheros HAL */
	struct ath_ratectrl	*sc_rc;		/* tx rate control support */
	struct ath_tx99		*sc_tx99;	/* tx99 adjunct state */
	void			(*sc_setdefantenna)(struct ath_softc *, u_int);

	/*
	 * First set of flags.
	 */
	uint32_t		sc_invalid  : 1,/* disable hardware accesses */
				sc_mrretry  : 1,/* multi-rate retry support */
				sc_mrrprot  : 1,/* MRR + protection support */
				sc_softled  : 1,/* enable LED gpio status */
				sc_hardled  : 1,/* enable MAC LED status */
				sc_splitmic : 1,/* split TKIP MIC keys */
				sc_needmib  : 1,/* enable MIB stats intr */
				sc_diversity: 1,/* enable rx diversity */
				sc_hasveol  : 1,/* tx VEOL support */
				sc_ledstate : 1,/* LED on/off state */
				sc_blinking : 1,/* LED blink operation active */
				sc_mcastkey : 1,/* mcast key cache search */
				sc_scanning : 1,/* scanning active */
				sc_syncbeacon:1,/* sync/resync beacon timers */
				sc_hasclrkey: 1,/* CLR key supported */
				sc_xchanmode: 1,/* extended channel mode */
				sc_outdoor  : 1,/* outdoor operation */
				sc_dturbo   : 1,/* dynamic turbo in use */
				sc_hasbmask : 1,/* bssid mask support */
				sc_hasbmatch: 1,/* bssid match disable support*/
				sc_hastsfadd: 1,/* tsf adjust support */
				sc_beacons  : 1,/* beacons running */
				sc_swbmiss  : 1,/* sta mode using sw bmiss */
				sc_stagbeacons:1,/* use staggered beacons */
				sc_wmetkipmic:1,/* can do WME+TKIP MIC */
				sc_resume_up: 1,/* on resume, start all vaps */
				sc_tdma	    : 1,/* TDMA in use */
				sc_setcca   : 1,/* set/clr CCA with TDMA */
				sc_resetcal : 1,/* reset cal state next trip */
				sc_rxslink  : 1,/* do self-linked final descriptor */
				sc_rxtsf32  : 1,/* RX dec TSF is 32 bits */
				sc_isedma   : 1,/* supports EDMA */
				sc_do_mybeacon : 1; /* supports mybeacon */

	/*
	 * Second set of flags.
	 */
	u_int32_t		sc_running  : 1,	/* initialized */
				sc_use_ent  : 1,
				sc_rx_stbc  : 1,
				sc_tx_stbc  : 1,
				sc_has_ldpc : 1,
				sc_hasenforcetxop : 1, /* support enforce TxOP */
				sc_hasdivcomb : 1,     /* RX diversity combining */
				sc_rx_lnamixer : 1,    /* RX using LNA mixing */
				sc_btcoex_mci : 1;     /* MCI bluetooth coex */

	int			sc_cabq_enable;	/* Enable cabq transmission */

	/*
	 * Enterprise mode configuration for AR9380 and later chipsets.
	 */
	uint32_t		sc_ent_cfg;

	uint32_t		sc_eerd;	/* regdomain from EEPROM */
	uint32_t		sc_eecc;	/* country code from EEPROM */
						/* rate tables */
	const HAL_RATE_TABLE	*sc_rates[IEEE80211_MODE_MAX];
	const HAL_RATE_TABLE	*sc_currates;	/* current rate table */
	enum ieee80211_phymode	sc_curmode;	/* current phy mode */
	HAL_OPMODE		sc_opmode;	/* current operating mode */
	u_int16_t		sc_curtxpow;	/* current tx power limit */
	u_int16_t		sc_curaid;	/* current association id */
	struct ieee80211_channel *sc_curchan;	/* current installed channel */
	u_int8_t		sc_curbssid[IEEE80211_ADDR_LEN];
	u_int8_t		sc_rixmap[256];	/* IEEE to h/w rate table ix */
	struct {
		u_int8_t	ieeerate;	/* IEEE rate */
		u_int8_t	rxflags;	/* radiotap rx flags */
		u_int8_t	txflags;	/* radiotap tx flags */
		u_int16_t	ledon;		/* softled on time */
		u_int16_t	ledoff;		/* softled off time */
	} sc_hwmap[32];				/* h/w rate ix mappings */
	u_int8_t		sc_protrix;	/* protection rate index */
	u_int8_t		sc_lastdatarix;	/* last data frame rate index */
	u_int			sc_mcastrate;	/* ieee rate for mcastrateix */
	u_int			sc_fftxqmin;	/* min frames before staging */
	u_int			sc_fftxqmax;	/* max frames before drop */
	u_int			sc_txantenna;	/* tx antenna (fixed or auto) */

	HAL_INT			sc_imask;	/* interrupt mask copy */

	/*
	 * These are modified in the interrupt handler as well as
	 * the task queues and other contexts. Thus these must be
	 * protected by a mutex, or they could clash.
	 *
	 * For now, access to these is behind the ATH_LOCK,
	 * just to save time.
	 */
	uint32_t		sc_txq_active;	/* bitmap of active TXQs */
	uint32_t		sc_kickpcu;	/* whether to kick the PCU */
	uint32_t		sc_rxproc_cnt;	/* In RX processing */
	uint32_t		sc_txproc_cnt;	/* In TX processing */
	uint32_t		sc_txstart_cnt;	/* In TX output (raw/start) */
	uint32_t		sc_inreset_cnt;	/* In active reset/chanchange */
	uint32_t		sc_txrx_cnt;	/* refcount on stop/start'ing TX */
	uint32_t		sc_intr_cnt;	/* refcount on interrupt handling */

	u_int			sc_keymax;	/* size of key cache */
	u_int8_t		sc_keymap[ATH_KEYBYTES];/* key use bit map */

	/*
	 * Software based LED blinking
	 */
	u_int			sc_ledpin;	/* GPIO pin for driving LED */
	u_int			sc_ledon;	/* pin setting for LED on */
	u_int			sc_ledidle;	/* idle polling interval */
	int			sc_ledevent;	/* time of last LED event */
	u_int8_t		sc_txrix;	/* current tx rate for LED */
	u_int16_t		sc_ledoff;	/* off time for current blink */
	struct callout		sc_ledtimer;	/* led off timer */

	/*
	 * Hardware based LED blinking
	 */
	int			sc_led_pwr_pin;	/* MAC power LED GPIO pin */
	int			sc_led_net_pin;	/* MAC network LED GPIO pin */

	u_int			sc_rfsilentpin;	/* GPIO pin for rfkill int */
	u_int			sc_rfsilentpol;	/* pin setting for rfkill on */

	struct ath_descdma	sc_rxdma;	/* RX descriptors */
	ath_bufhead		sc_rxbuf;	/* receive buffer */
	u_int32_t		*sc_rxlink;	/* link ptr in last RX desc */
	struct task		sc_rxtask;	/* rx int processing */
	u_int8_t		sc_defant;	/* current default antenna */
	u_int8_t		sc_rxotherant;	/* rx's on non-default antenna*/
	u_int64_t		sc_lastrx;	/* tsf at last rx'd frame */
	struct ath_rx_status	*sc_lastrs;	/* h/w status of last rx */
	struct ath_rx_radiotap_header sc_rx_th;
	int			sc_rx_th_len;
	u_int			sc_monpass;	/* frames to pass in mon.mode */

	struct ath_descdma	sc_txdma;	/* TX descriptors */
	uint16_t		sc_txbuf_descid;
	ath_bufhead		sc_txbuf;	/* transmit buffer */
	int			sc_txbuf_cnt;	/* how many buffers avail */
	struct ath_descdma	sc_txdma_mgmt;	/* mgmt TX descriptors */
	ath_bufhead		sc_txbuf_mgmt;	/* mgmt transmit buffer */
	struct ath_descdma	sc_txsdma;	/* EDMA TX status desc's */
	struct mtx		sc_txbuflock;	/* txbuf lock */
	char			sc_txname[12];	/* e.g. "ath0_buf" */
	u_int			sc_txqsetup;	/* h/w queues setup */
	u_int			sc_txintrperiod;/* tx interrupt batching */
	struct ath_txq		sc_txq[HAL_NUM_TX_QUEUES];
	struct ath_txq		*sc_ac2q[5];	/* WME AC -> h/w q map */ 
	struct task		sc_txtask;	/* tx int processing */
	struct task		sc_txqtask;	/* tx proc processing */

	struct ath_descdma	sc_txcompdma;	/* TX EDMA completion */
	struct mtx		sc_txcomplock;	/* TX EDMA completion lock */
	char			sc_txcompname[12];	/* eg ath0_txcomp */

	int			sc_wd_timer;	/* count down for wd timer */
	struct callout		sc_wd_ch;	/* tx watchdog timer */
	struct ath_tx_radiotap_header sc_tx_th;
	int			sc_tx_th_len;

	struct ath_descdma	sc_bdma;	/* beacon descriptors */
	ath_bufhead		sc_bbuf;	/* beacon buffers */
	u_int			sc_bhalq;	/* HAL q for outgoing beacons */
	u_int			sc_bmisscount;	/* missed beacon transmits */
	u_int32_t		sc_ant_tx[8];	/* recent tx frames/antenna */
	struct ath_txq		*sc_cabq;	/* tx q for cab frames */
	struct task		sc_bmisstask;	/* bmiss int processing */
	struct task		sc_bstucktask;	/* stuck beacon processing */
	struct task		sc_resettask;	/* interface reset task */
	struct task		sc_fataltask;	/* fatal task */
	enum {
		OK,				/* no change needed */
		UPDATE,				/* update pending */
		COMMIT				/* beacon sent, commit change */
	} sc_updateslot;			/* slot time update fsm */
	int			sc_slotupdate;	/* slot to advance fsm */
	struct ieee80211vap	*sc_bslot[ATH_BCBUF];
	int			sc_nbcnvaps;	/* # vaps with beacons */

	struct callout		sc_cal_ch;	/* callout handle for cals */
	int			sc_lastlongcal;	/* last long cal completed */
	int			sc_lastcalreset;/* last cal reset done */
	int			sc_lastani;	/* last ANI poll */
	int			sc_lastshortcal;	/* last short calibration */
	HAL_BOOL		sc_doresetcal;	/* Yes, we're doing a reset cal atm */
	HAL_NODE_STATS		sc_halstats;	/* station-mode rssi stats */
	u_int			sc_tdmadbaprep;	/* TDMA DBA prep time */
	u_int			sc_tdmaswbaprep;/* TDMA SWBA prep time */
	u_int			sc_tdmaswba;	/* TDMA SWBA counter */
	u_int32_t		sc_tdmabintval;	/* TDMA beacon interval (TU) */
	u_int32_t		sc_tdmaguard;	/* TDMA guard time (usec) */
	u_int			sc_tdmaslotlen;	/* TDMA slot length (usec) */
	u_int32_t		sc_avgtsfdeltap;/* TDMA slot adjust (+) */
	u_int32_t		sc_avgtsfdeltam;/* TDMA slot adjust (-) */
	uint16_t		*sc_eepromdata;	/* Local eeprom data, if AR9100 */
	uint32_t		sc_txchainmask;	/* hardware TX chainmask */
	uint32_t		sc_rxchainmask;	/* hardware RX chainmask */
	uint32_t		sc_cur_txchainmask;	/* currently configured TX chainmask */
	uint32_t		sc_cur_rxchainmask;	/* currently configured RX chainmask */
	uint32_t		sc_rts_aggr_limit;	/* TX limit on RTS aggregates */
	int			sc_aggr_limit;	/* TX limit on all aggregates */
	int			sc_delim_min_pad;	/* Minimum delimiter count */

	/* Queue limits */

	/*
	 * To avoid queue starvation in congested conditions,
	 * these parameters tune the maximum number of frames
	 * queued to the data/mcastq before they're dropped.
	 *
	 * This is to prevent:
	 * + a single destination overwhelming everything, including
	 *   management/multicast frames;
	 * + multicast frames overwhelming everything (when the
	 *   air is sufficiently busy that cabq can't drain.)
	 * + A node in powersave shouldn't be allowed to exhaust
	 *   all available mbufs;
	 *
	 * These implement:
	 * + data_minfree is the maximum number of free buffers
	 *   overall to successfully allow a data frame.
	 *
	 * + mcastq_maxdepth is the maximum depth allowed of the cabq.
	 */
	int			sc_txq_node_maxdepth;
	int			sc_txq_data_minfree;
	int			sc_txq_mcastq_maxdepth;
	int			sc_txq_node_psq_maxdepth;

	/*
	 * Software queue twiddles
	 *
	 * hwq_limit_nonaggr:
	 *		when to begin limiting non-aggregate frames to the
	 *		hardware queue, regardless of the TID.
	 * hwq_limit_aggr:
	 *		when to begin limiting A-MPDU frames to the
	 *		hardware queue, regardless of the TID.
	 * tid_hwq_lo:	how low the per-TID hwq count has to be before the
	 *		TID will be scheduled again
	 * tid_hwq_hi:	how many frames to queue to the HWQ before the TID
	 *		stops being scheduled.
	 */
	int			sc_hwq_limit_nonaggr;
	int			sc_hwq_limit_aggr;
	int			sc_tid_hwq_lo;
	int			sc_tid_hwq_hi;

	/* DFS related state */
	void			*sc_dfs;	/* Used by an optional DFS module */
	int			sc_dodfs;	/* Whether to enable DFS rx filter bits */
	struct task		sc_dfstask;	/* DFS processing task */

	/* Spectral related state */
	void			*sc_spectral;
	int			sc_dospectral;

	/* LNA diversity related state */
	void			*sc_lna_div;
	int			sc_dolnadiv;

	/* ALQ */
#ifdef	ATH_DEBUG_ALQ
	struct if_ath_alq sc_alq;
#endif

	/* TX AMPDU handling */
	int			(*sc_addba_request)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *, int, int, int);
	int			(*sc_addba_response)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *, int, int, int);
	void			(*sc_addba_stop)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *);
	void			(*sc_addba_response_timeout)
				    (struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *);
	void			(*sc_bar_response)(struct ieee80211_node *ni,
				    struct ieee80211_tx_ampdu *tap,
				    int status);

	/*
	 * Powersave state tracking.
	 *
	 * target/cur powerstate is the chip power state.
	 * target selfgen state is the self-generated frames
	 *   state.  The chip can be awake but transmitted frames
	 *   can have the PWRMGT bit set to 1 so the destination
	 *   thinks the node is asleep.
	 */
	HAL_POWER_MODE		sc_target_powerstate;
	HAL_POWER_MODE		sc_target_selfgen_state;

	HAL_POWER_MODE		sc_cur_powerstate;

	int			sc_powersave_refcnt;

	/* ATH_PCI_* flags */
	uint32_t		sc_pci_devinfo;

	/* BT coex */
	struct {
		struct ath_descdma buf;

		/* gpm/sched buffer, saved pointers */
		char *sched_buf;
		bus_addr_t sched_paddr;
		char *gpm_buf;
		bus_addr_t gpm_paddr;

		uint32_t wlan_channels[4];
	} sc_btcoex;
};

#define	ATH_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->sc_mtx, device_get_nameunit((_sc)->sc_dev), \
		 NULL, MTX_DEF | MTX_RECURSE)
#define	ATH_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx)
#define	ATH_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	ATH_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	ATH_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)
#define	ATH_UNLOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_NOTOWNED)

/*
 * The TX lock is non-reentrant and serialises the TX frame send
 * and completion operations.
 */
#define	ATH_TX_LOCK_INIT(_sc) do {\
	snprintf((_sc)->sc_tx_mtx_name,				\
	    sizeof((_sc)->sc_tx_mtx_name),				\
	    "%s TX lock",						\
	    device_get_nameunit((_sc)->sc_dev));			\
	mtx_init(&(_sc)->sc_tx_mtx, (_sc)->sc_tx_mtx_name,		\
		 NULL, MTX_DEF);					\
	} while (0)
#define	ATH_TX_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_tx_mtx)
#define	ATH_TX_LOCK(_sc)		mtx_lock(&(_sc)->sc_tx_mtx)
#define	ATH_TX_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_tx_mtx)
#define	ATH_TX_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_tx_mtx,	\
		MA_OWNED)
#define	ATH_TX_UNLOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_tx_mtx,	\
		MA_NOTOWNED)
#define	ATH_TX_TRYLOCK(_sc)	(mtx_owned(&(_sc)->sc_tx_mtx) != 0 &&	\
					mtx_trylock(&(_sc)->sc_tx_mtx))

/*
 * The PCU lock is non-recursive and should be treated as a spinlock.
 * Although currently the interrupt code is run in netisr context and
 * doesn't require this, this may change in the future.
 * Please keep this in mind when protecting certain code paths
 * with the PCU lock.
 *
 * The PCU lock is used to serialise access to the PCU so things such
 * as TX, RX, state change (eg channel change), channel reset and updates
 * from interrupt context (eg kickpcu, txqactive bits) do not clash.
 *
 * Although the current single-thread taskqueue mechanism protects the
 * majority of these situations by simply serialising them, there are
 * a few others which occur at the same time. These include the TX path
 * (which only acquires ATH_LOCK when recycling buffers to the free list),
 * ath_set_channel, the channel scanning API and perhaps quite a bit more.
 */
#define	ATH_PCU_LOCK_INIT(_sc) do {\
	snprintf((_sc)->sc_pcu_mtx_name,				\
	    sizeof((_sc)->sc_pcu_mtx_name),				\
	    "%s PCU lock",						\
	    device_get_nameunit((_sc)->sc_dev));			\
	mtx_init(&(_sc)->sc_pcu_mtx, (_sc)->sc_pcu_mtx_name,		\
		 NULL, MTX_DEF);					\
	} while (0)
#define	ATH_PCU_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_pcu_mtx)
#define	ATH_PCU_LOCK(_sc)		mtx_lock(&(_sc)->sc_pcu_mtx)
#define	ATH_PCU_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_pcu_mtx)
#define	ATH_PCU_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_pcu_mtx,	\
		MA_OWNED)
#define	ATH_PCU_UNLOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_pcu_mtx,	\
		MA_NOTOWNED)

/*
 * The RX lock is primarily a(nother) workaround to ensure that the
 * RX FIFO/list isn't modified by various execution paths.
 * Even though RX occurs in a single context (the ath taskqueue), the
 * RX path can be executed via various reset/channel change paths.
 */
#define	ATH_RX_LOCK_INIT(_sc) do {\
	snprintf((_sc)->sc_rx_mtx_name,					\
	    sizeof((_sc)->sc_rx_mtx_name),				\
	    "%s RX lock",						\
	    device_get_nameunit((_sc)->sc_dev));			\
	mtx_init(&(_sc)->sc_rx_mtx, (_sc)->sc_rx_mtx_name,		\
		 NULL, MTX_DEF);					\
	} while (0)
#define	ATH_RX_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_rx_mtx)
#define	ATH_RX_LOCK(_sc)		mtx_lock(&(_sc)->sc_rx_mtx)
#define	ATH_RX_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_rx_mtx)
#define	ATH_RX_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_rx_mtx,	\
		MA_OWNED)
#define	ATH_RX_UNLOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_rx_mtx,	\
		MA_NOTOWNED)

#define	ATH_TXQ_SETUP(sc, i)	((sc)->sc_txqsetup & (1<<i))

#define	ATH_TXBUF_LOCK_INIT(_sc) do { \
	snprintf((_sc)->sc_txname, sizeof((_sc)->sc_txname), "%s_buf", \
		device_get_nameunit((_sc)->sc_dev)); \
	mtx_init(&(_sc)->sc_txbuflock, (_sc)->sc_txname, NULL, MTX_DEF); \
} while (0)
#define	ATH_TXBUF_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_txbuflock)
#define	ATH_TXBUF_LOCK(_sc)		mtx_lock(&(_sc)->sc_txbuflock)
#define	ATH_TXBUF_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_txbuflock)
#define	ATH_TXBUF_LOCK_ASSERT(_sc) \
	mtx_assert(&(_sc)->sc_txbuflock, MA_OWNED)
#define	ATH_TXBUF_UNLOCK_ASSERT(_sc) \
	mtx_assert(&(_sc)->sc_txbuflock, MA_NOTOWNED)

#define	ATH_TXSTATUS_LOCK_INIT(_sc) do { \
	snprintf((_sc)->sc_txcompname, sizeof((_sc)->sc_txcompname), \
		"%s_buf", \
		device_get_nameunit((_sc)->sc_dev)); \
	mtx_init(&(_sc)->sc_txcomplock, (_sc)->sc_txcompname, NULL, \
		MTX_DEF); \
} while (0)
#define	ATH_TXSTATUS_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_txcomplock)
#define	ATH_TXSTATUS_LOCK(_sc)		mtx_lock(&(_sc)->sc_txcomplock)
#define	ATH_TXSTATUS_UNLOCK(_sc)	mtx_unlock(&(_sc)->sc_txcomplock)
#define	ATH_TXSTATUS_LOCK_ASSERT(_sc) \
	mtx_assert(&(_sc)->sc_txcomplock, MA_OWNED)

int	ath_attach(u_int16_t, struct ath_softc *);
int	ath_detach(struct ath_softc *);
void	ath_resume(struct ath_softc *);
void	ath_suspend(struct ath_softc *);
void	ath_shutdown(struct ath_softc *);
void	ath_intr(void *);

/*
 * HAL definitions to comply with local coding convention.
 */
#define	ath_hal_detach(_ah) \
	((*(_ah)->ah_detach)((_ah)))
#define	ath_hal_reset(_ah, _opmode, _chan, _fullreset, _resettype, _pstatus) \
	((*(_ah)->ah_reset)((_ah), (_opmode), (_chan), (_fullreset), \
	    (_resettype), (_pstatus)))
#define	ath_hal_macversion(_ah) \
	(((_ah)->ah_macVersion << 4) | ((_ah)->ah_macRev))
#define	ath_hal_getratetable(_ah, _mode) \
	((*(_ah)->ah_getRateTable)((_ah), (_mode)))
#define	ath_hal_getmac(_ah, _mac) \
	((*(_ah)->ah_getMacAddress)((_ah), (_mac)))
#define	ath_hal_setmac(_ah, _mac) \
	((*(_ah)->ah_setMacAddress)((_ah), (_mac)))
#define	ath_hal_getbssidmask(_ah, _mask) \
	((*(_ah)->ah_getBssIdMask)((_ah), (_mask)))
#define	ath_hal_setbssidmask(_ah, _mask) \
	((*(_ah)->ah_setBssIdMask)((_ah), (_mask)))
#define	ath_hal_intrset(_ah, _mask) \
	((*(_ah)->ah_setInterrupts)((_ah), (_mask)))
#define	ath_hal_intrget(_ah) \
	((*(_ah)->ah_getInterrupts)((_ah)))
#define	ath_hal_intrpend(_ah) \
	((*(_ah)->ah_isInterruptPending)((_ah)))
#define	ath_hal_getisr(_ah, _pmask) \
	((*(_ah)->ah_getPendingInterrupts)((_ah), (_pmask)))
#define	ath_hal_updatetxtriglevel(_ah, _inc) \
	((*(_ah)->ah_updateTxTrigLevel)((_ah), (_inc)))
#define	ath_hal_setpower(_ah, _mode) \
	((*(_ah)->ah_setPowerMode)((_ah), (_mode), AH_TRUE))
#define	ath_hal_setselfgenpower(_ah, _mode) \
	((*(_ah)->ah_setPowerMode)((_ah), (_mode), AH_FALSE))
#define	ath_hal_keycachesize(_ah) \
	((*(_ah)->ah_getKeyCacheSize)((_ah)))
#define	ath_hal_keyreset(_ah, _ix) \
	((*(_ah)->ah_resetKeyCacheEntry)((_ah), (_ix)))
#define	ath_hal_keyset(_ah, _ix, _pk, _mac) \
	((*(_ah)->ah_setKeyCacheEntry)((_ah), (_ix), (_pk), (_mac), AH_FALSE))
#define	ath_hal_keyisvalid(_ah, _ix) \
	(((*(_ah)->ah_isKeyCacheEntryValid)((_ah), (_ix))))
#define	ath_hal_keysetmac(_ah, _ix, _mac) \
	((*(_ah)->ah_setKeyCacheEntryMac)((_ah), (_ix), (_mac)))
#define	ath_hal_getrxfilter(_ah) \
	((*(_ah)->ah_getRxFilter)((_ah)))
#define	ath_hal_setrxfilter(_ah, _filter) \
	((*(_ah)->ah_setRxFilter)((_ah), (_filter)))
#define	ath_hal_setmcastfilter(_ah, _mfilt0, _mfilt1) \
	((*(_ah)->ah_setMulticastFilter)((_ah), (_mfilt0), (_mfilt1)))
#define	ath_hal_waitforbeacon(_ah, _bf) \
	((*(_ah)->ah_waitForBeaconDone)((_ah), (_bf)->bf_daddr))
#define	ath_hal_putrxbuf(_ah, _bufaddr, _rxq) \
	((*(_ah)->ah_setRxDP)((_ah), (_bufaddr), (_rxq)))
/* NB: common across all chips */
#define	AR_TSF_L32	0x804c	/* MAC local clock lower 32 bits */
#define	ath_hal_gettsf32(_ah) \
	OS_REG_READ(_ah, AR_TSF_L32)
#define	ath_hal_gettsf64(_ah) \
	((*(_ah)->ah_getTsf64)((_ah)))
#define	ath_hal_settsf64(_ah, _val) \
	((*(_ah)->ah_setTsf64)((_ah), (_val)))
#define	ath_hal_resettsf(_ah) \
	((*(_ah)->ah_resetTsf)((_ah)))
#define	ath_hal_rxena(_ah) \
	((*(_ah)->ah_enableReceive)((_ah)))
#define	ath_hal_puttxbuf(_ah, _q, _bufaddr) \
	((*(_ah)->ah_setTxDP)((_ah), (_q), (_bufaddr)))
#define	ath_hal_gettxbuf(_ah, _q) \
	((*(_ah)->ah_getTxDP)((_ah), (_q)))
#define	ath_hal_numtxpending(_ah, _q) \
	((*(_ah)->ah_numTxPending)((_ah), (_q)))
#define	ath_hal_getrxbuf(_ah, _rxq) \
	((*(_ah)->ah_getRxDP)((_ah), (_rxq)))
#define	ath_hal_txstart(_ah, _q) \
	((*(_ah)->ah_startTxDma)((_ah), (_q)))
#define	ath_hal_setchannel(_ah, _chan) \
	((*(_ah)->ah_setChannel)((_ah), (_chan)))
#define	ath_hal_calibrate(_ah, _chan, _iqcal) \
	((*(_ah)->ah_perCalibration)((_ah), (_chan), (_iqcal)))
#define	ath_hal_calibrateN(_ah, _chan, _lcal, _isdone) \
	((*(_ah)->ah_perCalibrationN)((_ah), (_chan), 0x1, (_lcal), (_isdone)))
#define	ath_hal_calreset(_ah, _chan) \
	((*(_ah)->ah_resetCalValid)((_ah), (_chan)))
#define	ath_hal_setledstate(_ah, _state) \
	((*(_ah)->ah_setLedState)((_ah), (_state)))
#define	ath_hal_beaconinit(_ah, _nextb, _bperiod) \
	((*(_ah)->ah_beaconInit)((_ah), (_nextb), (_bperiod)))
#define	ath_hal_beaconreset(_ah) \
	((*(_ah)->ah_resetStationBeaconTimers)((_ah)))
#define	ath_hal_beaconsettimers(_ah, _bt) \
	((*(_ah)->ah_setBeaconTimers)((_ah), (_bt)))
#define	ath_hal_beacontimers(_ah, _bs) \
	((*(_ah)->ah_setStationBeaconTimers)((_ah), (_bs)))
#define	ath_hal_getnexttbtt(_ah) \
	((*(_ah)->ah_getNextTBTT)((_ah)))
#define	ath_hal_setassocid(_ah, _bss, _associd) \
	((*(_ah)->ah_writeAssocid)((_ah), (_bss), (_associd)))
#define	ath_hal_phydisable(_ah) \
	((*(_ah)->ah_phyDisable)((_ah)))
#define	ath_hal_setopmode(_ah) \
	((*(_ah)->ah_setPCUConfig)((_ah)))
#define	ath_hal_stoptxdma(_ah, _qnum) \
	((*(_ah)->ah_stopTxDma)((_ah), (_qnum)))
#define	ath_hal_stoppcurecv(_ah) \
	((*(_ah)->ah_stopPcuReceive)((_ah)))
#define	ath_hal_startpcurecv(_ah) \
	((*(_ah)->ah_startPcuReceive)((_ah)))
#define	ath_hal_stopdmarecv(_ah) \
	((*(_ah)->ah_stopDmaReceive)((_ah)))
#define	ath_hal_getdiagstate(_ah, _id, _indata, _insize, _outdata, _outsize) \
	((*(_ah)->ah_getDiagState)((_ah), (_id), \
		(_indata), (_insize), (_outdata), (_outsize)))
#define	ath_hal_getfatalstate(_ah, _outdata, _outsize) \
	ath_hal_getdiagstate(_ah, 29, NULL, 0, (_outdata), _outsize)
#define	ath_hal_setuptxqueue(_ah, _type, _irq) \
	((*(_ah)->ah_setupTxQueue)((_ah), (_type), (_irq)))
#define	ath_hal_resettxqueue(_ah, _q) \
	((*(_ah)->ah_resetTxQueue)((_ah), (_q)))
#define	ath_hal_releasetxqueue(_ah, _q) \
	((*(_ah)->ah_releaseTxQueue)((_ah), (_q)))
#define	ath_hal_gettxqueueprops(_ah, _q, _qi) \
	((*(_ah)->ah_getTxQueueProps)((_ah), (_q), (_qi)))
#define	ath_hal_settxqueueprops(_ah, _q, _qi) \
	((*(_ah)->ah_setTxQueueProps)((_ah), (_q), (_qi)))
/* NB: common across all chips */
#define	AR_Q_TXE	0x0840	/* MAC Transmit Queue enable */
#define	ath_hal_txqenabled(_ah, _qnum) \
	(OS_REG_READ(_ah, AR_Q_TXE) & (1<<(_qnum)))
#define	ath_hal_getrfgain(_ah) \
	((*(_ah)->ah_getRfGain)((_ah)))
#define	ath_hal_getdefantenna(_ah) \
	((*(_ah)->ah_getDefAntenna)((_ah)))
#define	ath_hal_setdefantenna(_ah, _ant) \
	((*(_ah)->ah_setDefAntenna)((_ah), (_ant)))
#define	ath_hal_rxmonitor(_ah, _arg, _chan) \
	((*(_ah)->ah_rxMonitor)((_ah), (_arg), (_chan)))
#define	ath_hal_ani_poll(_ah, _chan) \
	((*(_ah)->ah_aniPoll)((_ah), (_chan)))
#define	ath_hal_mibevent(_ah, _stats) \
	((*(_ah)->ah_procMibEvent)((_ah), (_stats)))
#define	ath_hal_setslottime(_ah, _us) \
	((*(_ah)->ah_setSlotTime)((_ah), (_us)))
#define	ath_hal_getslottime(_ah) \
	((*(_ah)->ah_getSlotTime)((_ah)))
#define	ath_hal_setacktimeout(_ah, _us) \
	((*(_ah)->ah_setAckTimeout)((_ah), (_us)))
#define	ath_hal_getacktimeout(_ah) \
	((*(_ah)->ah_getAckTimeout)((_ah)))
#define	ath_hal_setctstimeout(_ah, _us) \
	((*(_ah)->ah_setCTSTimeout)((_ah), (_us)))
#define	ath_hal_getctstimeout(_ah) \
	((*(_ah)->ah_getCTSTimeout)((_ah)))
#define	ath_hal_getcapability(_ah, _cap, _param, _result) \
	((*(_ah)->ah_getCapability)((_ah), (_cap), (_param), (_result)))
#define	ath_hal_setcapability(_ah, _cap, _param, _v, _status) \
	((*(_ah)->ah_setCapability)((_ah), (_cap), (_param), (_v), (_status)))
#define	ath_hal_ciphersupported(_ah, _cipher) \
	(ath_hal_getcapability(_ah, HAL_CAP_CIPHER, _cipher, NULL) == HAL_OK)
#define	ath_hal_getregdomain(_ah, _prd) \
	(ath_hal_getcapability(_ah, HAL_CAP_REG_DMN, 0, (_prd)) == HAL_OK)
#define	ath_hal_setregdomain(_ah, _rd) \
	ath_hal_setcapability(_ah, HAL_CAP_REG_DMN, 0, _rd, NULL)
#define	ath_hal_getcountrycode(_ah, _pcc) \
	(*(_pcc) = (_ah)->ah_countryCode)
#define	ath_hal_gettkipmic(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_TKIP_MIC, 1, NULL) == HAL_OK)
#define	ath_hal_settkipmic(_ah, _v) \
	ath_hal_setcapability(_ah, HAL_CAP_TKIP_MIC, 1, _v, NULL)
#define	ath_hal_hastkipsplit(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_TKIP_SPLIT, 0, NULL) == HAL_OK)
#define	ath_hal_gettkipsplit(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_TKIP_SPLIT, 1, NULL) == HAL_OK)
#define	ath_hal_settkipsplit(_ah, _v) \
	ath_hal_setcapability(_ah, HAL_CAP_TKIP_SPLIT, 1, _v, NULL)
#define	ath_hal_haswmetkipmic(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_WME_TKIPMIC, 0, NULL) == HAL_OK)
#define	ath_hal_hwphycounters(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_PHYCOUNTERS, 0, NULL) == HAL_OK)
#define	ath_hal_hasdiversity(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_DIVERSITY, 0, NULL) == HAL_OK)
#define	ath_hal_getdiversity(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_DIVERSITY, 1, NULL) == HAL_OK)
#define	ath_hal_setdiversity(_ah, _v) \
	ath_hal_setcapability(_ah, HAL_CAP_DIVERSITY, 1, _v, NULL)
#define	ath_hal_getantennaswitch(_ah) \
	((*(_ah)->ah_getAntennaSwitch)((_ah)))
#define	ath_hal_setantennaswitch(_ah, _v) \
	((*(_ah)->ah_setAntennaSwitch)((_ah), (_v)))
#define	ath_hal_getdiag(_ah, _pv) \
	(ath_hal_getcapability(_ah, HAL_CAP_DIAG, 0, _pv) == HAL_OK)
#define	ath_hal_setdiag(_ah, _v) \
	ath_hal_setcapability(_ah, HAL_CAP_DIAG, 0, _v, NULL)
#define	ath_hal_getnumtxqueues(_ah, _pv) \
	(ath_hal_getcapability(_ah, HAL_CAP_NUM_TXQUEUES, 0, _pv) == HAL_OK)
#define	ath_hal_hasveol(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_VEOL, 0, NULL) == HAL_OK)
#define	ath_hal_hastxpowlimit(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_TXPOW, 0, NULL) == HAL_OK)
#define	ath_hal_settxpowlimit(_ah, _pow) \
	((*(_ah)->ah_setTxPowerLimit)((_ah), (_pow)))
#define	ath_hal_gettxpowlimit(_ah, _ppow) \
	(ath_hal_getcapability(_ah, HAL_CAP_TXPOW, 1, _ppow) == HAL_OK)
#define	ath_hal_getmaxtxpow(_ah, _ppow) \
	(ath_hal_getcapability(_ah, HAL_CAP_TXPOW, 2, _ppow) == HAL_OK)
#define	ath_hal_gettpscale(_ah, _scale) \
	(ath_hal_getcapability(_ah, HAL_CAP_TXPOW, 3, _scale) == HAL_OK)
#define	ath_hal_settpscale(_ah, _v) \
	ath_hal_setcapability(_ah, HAL_CAP_TXPOW, 3, _v, NULL)
#define	ath_hal_hastpc(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_TPC, 0, NULL) == HAL_OK)
#define	ath_hal_gettpc(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_TPC, 1, NULL) == HAL_OK)
#define	ath_hal_settpc(_ah, _v) \
	ath_hal_setcapability(_ah, HAL_CAP_TPC, 1, _v, NULL)
#define	ath_hal_hasbursting(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_BURST, 0, NULL) == HAL_OK)
#define	ath_hal_setmcastkeysearch(_ah, _v) \
	ath_hal_setcapability(_ah, HAL_CAP_MCAST_KEYSRCH, 0, _v, NULL)
#define	ath_hal_hasmcastkeysearch(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_MCAST_KEYSRCH, 0, NULL) == HAL_OK)
#define	ath_hal_getmcastkeysearch(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_MCAST_KEYSRCH, 1, NULL) == HAL_OK)
#define	ath_hal_hasfastframes(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_FASTFRAME, 0, NULL) == HAL_OK)
#define	ath_hal_hasbssidmask(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_BSSIDMASK, 0, NULL) == HAL_OK)
#define	ath_hal_hasbssidmatch(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_BSSIDMATCH, 0, NULL) == HAL_OK)
#define	ath_hal_hastsfadjust(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_TSF_ADJUST, 0, NULL) == HAL_OK)
#define	ath_hal_gettsfadjust(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_TSF_ADJUST, 1, NULL) == HAL_OK)
#define	ath_hal_settsfadjust(_ah, _onoff) \
	ath_hal_setcapability(_ah, HAL_CAP_TSF_ADJUST, 1, _onoff, NULL)
#define	ath_hal_hasrfsilent(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_RFSILENT, 0, NULL) == HAL_OK)
#define	ath_hal_getrfkill(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_RFSILENT, 1, NULL) == HAL_OK)
#define	ath_hal_setrfkill(_ah, _onoff) \
	ath_hal_setcapability(_ah, HAL_CAP_RFSILENT, 1, _onoff, NULL)
#define	ath_hal_getrfsilent(_ah, _prfsilent) \
	(ath_hal_getcapability(_ah, HAL_CAP_RFSILENT, 2, _prfsilent) == HAL_OK)
#define	ath_hal_setrfsilent(_ah, _rfsilent) \
	ath_hal_setcapability(_ah, HAL_CAP_RFSILENT, 2, _rfsilent, NULL)
#define	ath_hal_gettpack(_ah, _ptpack) \
	(ath_hal_getcapability(_ah, HAL_CAP_TPC_ACK, 0, _ptpack) == HAL_OK)
#define	ath_hal_settpack(_ah, _tpack) \
	ath_hal_setcapability(_ah, HAL_CAP_TPC_ACK, 0, _tpack, NULL)
#define	ath_hal_gettpcts(_ah, _ptpcts) \
	(ath_hal_getcapability(_ah, HAL_CAP_TPC_CTS, 0, _ptpcts) == HAL_OK)
#define	ath_hal_settpcts(_ah, _tpcts) \
	ath_hal_setcapability(_ah, HAL_CAP_TPC_CTS, 0, _tpcts, NULL)
#define	ath_hal_hasintmit(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_INTMIT, \
	HAL_CAP_INTMIT_PRESENT, NULL) == HAL_OK)
#define	ath_hal_getintmit(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_INTMIT, \
	HAL_CAP_INTMIT_ENABLE, NULL) == HAL_OK)
#define	ath_hal_setintmit(_ah, _v) \
	ath_hal_setcapability(_ah, HAL_CAP_INTMIT, \
	HAL_CAP_INTMIT_ENABLE, _v, NULL)
#define	ath_hal_hasmybeacon(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_DO_MYBEACON, 1, NULL) == HAL_OK)

#define	ath_hal_hasenforcetxop(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_ENFORCE_TXOP, 0, NULL) == HAL_OK)
#define	ath_hal_getenforcetxop(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_ENFORCE_TXOP, 1, NULL) == HAL_OK)
#define	ath_hal_setenforcetxop(_ah, _v) \
	ath_hal_setcapability(_ah, HAL_CAP_ENFORCE_TXOP, 1, _v, NULL)

#define	ath_hal_hasrxlnamixer(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_RX_LNA_MIXING, 0, NULL) == HAL_OK)

#define	ath_hal_hasdivantcomb(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_ANT_DIV_COMB, 0, NULL) == HAL_OK)
#define	ath_hal_hasldpc(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_LDPC, 0, NULL) == HAL_OK)
#define	ath_hal_hasldpcwar(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_LDPCWAR, 0, NULL) == HAL_OK)

/* EDMA definitions */
#define	ath_hal_hasedma(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_ENHANCED_DMA_SUPPORT,	\
	0, NULL) == HAL_OK)
#define	ath_hal_getrxfifodepth(_ah, _qtype, _req) \
	(ath_hal_getcapability(_ah, HAL_CAP_RXFIFODEPTH, _qtype, _req)	\
	== HAL_OK)
#define	ath_hal_getntxmaps(_ah, _req) \
	(ath_hal_getcapability(_ah, HAL_CAP_NUM_TXMAPS, 0, _req)	\
	== HAL_OK)
#define	ath_hal_gettxdesclen(_ah, _req) \
	(ath_hal_getcapability(_ah, HAL_CAP_TXDESCLEN, 0, _req)		\
	== HAL_OK)
#define	ath_hal_gettxstatuslen(_ah, _req) \
	(ath_hal_getcapability(_ah, HAL_CAP_TXSTATUSLEN, 0, _req)	\
	== HAL_OK)
#define	ath_hal_getrxstatuslen(_ah, _req) \
	(ath_hal_getcapability(_ah, HAL_CAP_RXSTATUSLEN, 0, _req)	\
	== HAL_OK)
#define	ath_hal_setrxbufsize(_ah, _req) \
	(ath_hal_setcapability(_ah, HAL_CAP_RXBUFSIZE, 0, _req, NULL)	\
	== HAL_OK)

#define	ath_hal_getchannoise(_ah, _c) \
	((*(_ah)->ah_getChanNoise)((_ah), (_c)))

/* 802.11n HAL methods */
#define	ath_hal_getrxchainmask(_ah, _prxchainmask) \
	(ath_hal_getcapability(_ah, HAL_CAP_RX_CHAINMASK, 0, _prxchainmask))
#define	ath_hal_gettxchainmask(_ah, _ptxchainmask) \
	(ath_hal_getcapability(_ah, HAL_CAP_TX_CHAINMASK, 0, _ptxchainmask))
#define	ath_hal_setrxchainmask(_ah, _rx) \
	(ath_hal_setcapability(_ah, HAL_CAP_RX_CHAINMASK, 1, _rx, NULL))
#define	ath_hal_settxchainmask(_ah, _tx) \
	(ath_hal_setcapability(_ah, HAL_CAP_TX_CHAINMASK, 1, _tx, NULL))
#define	ath_hal_split4ktrans(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_SPLIT_4KB_TRANS, \
	0, NULL) == HAL_OK)
#define	ath_hal_self_linked_final_rxdesc(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_RXDESC_SELFLINK, \
	0, NULL) == HAL_OK)
#define	ath_hal_gtxto_supported(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_GTXTO, 0, NULL) == HAL_OK)
#define	ath_hal_get_rx_tsf_prec(_ah, _pr) \
	(ath_hal_getcapability((_ah), HAL_CAP_RXTSTAMP_PREC, 0, (_pr)) \
	    == HAL_OK)
#define	ath_hal_get_tx_tsf_prec(_ah, _pr) \
	(ath_hal_getcapability((_ah), HAL_CAP_TXTSTAMP_PREC, 0, (_pr)) \
	    == HAL_OK)
#define	ath_hal_setuprxdesc(_ah, _ds, _size, _intreq) \
	((*(_ah)->ah_setupRxDesc)((_ah), (_ds), (_size), (_intreq)))
#define	ath_hal_rxprocdesc(_ah, _ds, _dspa, _dsnext, _rs) \
	((*(_ah)->ah_procRxDesc)((_ah), (_ds), (_dspa), (_dsnext), 0, (_rs)))
#define	ath_hal_setuptxdesc(_ah, _ds, _plen, _hlen, _atype, _txpow, \
		_txr0, _txtr0, _keyix, _ant, _flags, \
		_rtsrate, _rtsdura) \
	((*(_ah)->ah_setupTxDesc)((_ah), (_ds), (_plen), (_hlen), (_atype), \
		(_txpow), (_txr0), (_txtr0), (_keyix), (_ant), \
		(_flags), (_rtsrate), (_rtsdura), 0, 0, 0))
#define	ath_hal_setupxtxdesc(_ah, _ds, \
		_txr1, _txtr1, _txr2, _txtr2, _txr3, _txtr3) \
	((*(_ah)->ah_setupXTxDesc)((_ah), (_ds), \
		(_txr1), (_txtr1), (_txr2), (_txtr2), (_txr3), (_txtr3)))
#define	ath_hal_filltxdesc(_ah, _ds, _b, _l, _did, _qid, _first, _last, _ds0) \
	((*(_ah)->ah_fillTxDesc)((_ah), (_ds), (_b), (_l), (_did), (_qid), \
		(_first), (_last), (_ds0)))
#define	ath_hal_txprocdesc(_ah, _ds, _ts) \
	((*(_ah)->ah_procTxDesc)((_ah), (_ds), (_ts)))
#define	ath_hal_gettxintrtxqs(_ah, _txqs) \
	((*(_ah)->ah_getTxIntrQueue)((_ah), (_txqs)))
#define ath_hal_gettxcompletionrates(_ah, _ds, _rates, _tries) \
	((*(_ah)->ah_getTxCompletionRates)((_ah), (_ds), (_rates), (_tries)))
#define ath_hal_settxdesclink(_ah, _ds, _link) \
	((*(_ah)->ah_setTxDescLink)((_ah), (_ds), (_link)))
#define ath_hal_gettxdesclink(_ah, _ds, _link) \
	((*(_ah)->ah_getTxDescLink)((_ah), (_ds), (_link)))
#define ath_hal_gettxdesclinkptr(_ah, _ds, _linkptr) \
	((*(_ah)->ah_getTxDescLinkPtr)((_ah), (_ds), (_linkptr)))
#define	ath_hal_setuptxstatusring(_ah, _tsstart, _tspstart, _size) \
	((*(_ah)->ah_setupTxStatusRing)((_ah), (_tsstart), (_tspstart), \
		(_size)))
#define	ath_hal_gettxrawtxdesc(_ah, _txstatus) \
	((*(_ah)->ah_getTxRawTxDesc)((_ah), (_txstatus)))

#define	ath_hal_setupfirsttxdesc(_ah, _ds, _aggrlen, _flags, _txpower, \
		_txr0, _txtr0, _antm, _rcr, _rcd) \
	((*(_ah)->ah_setupFirstTxDesc)((_ah), (_ds), (_aggrlen), (_flags), \
	(_txpower), (_txr0), (_txtr0), (_antm), (_rcr), (_rcd)))
#define	ath_hal_chaintxdesc(_ah, _ds, _bl, _sl, _pktlen, _hdrlen, _type, \
	_keyix, _cipher, _delims, _first, _last, _lastaggr) \
	((*(_ah)->ah_chainTxDesc)((_ah), (_ds), (_bl), (_sl), \
	(_pktlen), (_hdrlen), (_type), (_keyix), (_cipher), (_delims), \
	(_first), (_last), (_lastaggr)))
#define	ath_hal_setuplasttxdesc(_ah, _ds, _ds0) \
	((*(_ah)->ah_setupLastTxDesc)((_ah), (_ds), (_ds0)))

#define	ath_hal_set11nratescenario(_ah, _ds, _dur, _rt, _series, _ns, _flags) \
	((*(_ah)->ah_set11nRateScenario)((_ah), (_ds), (_dur), (_rt), \
	(_series), (_ns), (_flags)))

#define	ath_hal_set11n_aggr_first(_ah, _ds, _len, _num) \
	((*(_ah)->ah_set11nAggrFirst)((_ah), (_ds), (_len), (_num)))
#define	ath_hal_set11n_aggr_middle(_ah, _ds, _num) \
	((*(_ah)->ah_set11nAggrMiddle)((_ah), (_ds), (_num)))
#define	ath_hal_set11n_aggr_last(_ah, _ds) \
	((*(_ah)->ah_set11nAggrLast)((_ah), (_ds)))

#define	ath_hal_set11nburstduration(_ah, _ds, _dur) \
	((*(_ah)->ah_set11nBurstDuration)((_ah), (_ds), (_dur)))
#define	ath_hal_clr11n_aggr(_ah, _ds) \
	((*(_ah)->ah_clr11nAggr)((_ah), (_ds)))
#define	ath_hal_set11n_virtmorefrag(_ah, _ds, _v) \
	((*(_ah)->ah_set11nVirtMoreFrag)((_ah), (_ds), (_v)))

#define	ath_hal_gpioCfgOutput(_ah, _gpio, _type) \
	((*(_ah)->ah_gpioCfgOutput)((_ah), (_gpio), (_type)))
#define	ath_hal_gpioset(_ah, _gpio, _b) \
	((*(_ah)->ah_gpioSet)((_ah), (_gpio), (_b)))
#define	ath_hal_gpioget(_ah, _gpio) \
	((*(_ah)->ah_gpioGet)((_ah), (_gpio)))
#define	ath_hal_gpiosetintr(_ah, _gpio, _b) \
	((*(_ah)->ah_gpioSetIntr)((_ah), (_gpio), (_b)))

/*
 * PCIe suspend/resume/poweron/poweroff related macros
 */
#define	ath_hal_enablepcie(_ah, _restore, _poweroff) \
	((*(_ah)->ah_configPCIE)((_ah), (_restore), (_poweroff)))
#define	ath_hal_disablepcie(_ah) \
	((*(_ah)->ah_disablePCIE)((_ah)))

/*
 * This is badly-named; you need to set the correct parameters
 * to begin to receive useful radar events; and even then
 * it doesn't "enable" DFS. See the ath_dfs/null/ module for
 * more information.
 */
#define	ath_hal_enabledfs(_ah, _param) \
	((*(_ah)->ah_enableDfs)((_ah), (_param)))
#define	ath_hal_getdfsthresh(_ah, _param) \
	((*(_ah)->ah_getDfsThresh)((_ah), (_param)))
#define	ath_hal_getdfsdefaultthresh(_ah, _param) \
	((*(_ah)->ah_getDfsDefaultThresh)((_ah), (_param)))
#define	ath_hal_procradarevent(_ah, _rxs, _fulltsf, _buf, _event) \
	((*(_ah)->ah_procRadarEvent)((_ah), (_rxs), (_fulltsf), \
	(_buf), (_event)))
#define	ath_hal_is_fast_clock_enabled(_ah) \
	((*(_ah)->ah_isFastClockEnabled)((_ah)))
#define	ath_hal_radar_wait(_ah, _chan) \
	((*(_ah)->ah_radarWait)((_ah), (_chan)))
#define	ath_hal_get_mib_cycle_counts(_ah, _sample) \
	((*(_ah)->ah_getMibCycleCounts)((_ah), (_sample)))
#define	ath_hal_get_chan_ext_busy(_ah) \
	((*(_ah)->ah_get11nExtBusy)((_ah)))
#define	ath_hal_setchainmasks(_ah, _txchainmask, _rxchainmask) \
	((*(_ah)->ah_setChainMasks)((_ah), (_txchainmask), (_rxchainmask)))
#define	ath_hal_set_quiet(_ah, _p, _d, _o, _f) \
	((*(_ah)->ah_setQuiet)((_ah), (_p), (_d), (_o), (_f)))

#define	ath_hal_spectral_supported(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_SPECTRAL_SCAN, 0, NULL) == HAL_OK)
#define	ath_hal_spectral_get_config(_ah, _p) \
	((*(_ah)->ah_spectralGetConfig)((_ah), (_p)))
#define	ath_hal_spectral_configure(_ah, _p) \
	((*(_ah)->ah_spectralConfigure)((_ah), (_p)))
#define	ath_hal_spectral_start(_ah) \
	((*(_ah)->ah_spectralStart)((_ah)))
#define	ath_hal_spectral_stop(_ah) \
	((*(_ah)->ah_spectralStop)((_ah)))

#define	ath_hal_btcoex_supported(_ah) \
	(ath_hal_getcapability(_ah, HAL_CAP_BT_COEX, 0, NULL) == HAL_OK)
#define	ath_hal_btcoex_set_info(_ah, _info) \
	((*(_ah)->ah_btCoexSetInfo)((_ah), (_info)))
#define	ath_hal_btcoex_set_config(_ah, _cfg) \
	((*(_ah)->ah_btCoexSetConfig)((_ah), (_cfg)))
#define	ath_hal_btcoex_set_qcu_thresh(_ah, _qcuid) \
	((*(_ah)->ah_btCoexSetQcuThresh)((_ah), (_qcuid)))
#define	ath_hal_btcoex_set_weights(_ah, _weight) \
	((*(_ah)->ah_btCoexSetWeights)((_ah), (_weight)))
#define	ath_hal_btcoex_set_bmiss_thresh(_ah, _thr) \
	((*(_ah)->ah_btCoexSetBmissThresh)((_ah), (_thr)))
#define	ath_hal_btcoex_set_parameter(_ah, _attrib, _val) \
	((*(_ah)->ah_btCoexSetParameter)((_ah), (_attrib), (_val)))
#define	ath_hal_btcoex_enable(_ah) \
	((*(_ah)->ah_btCoexEnable)((_ah)))
#define	ath_hal_btcoex_disable(_ah) \
	((*(_ah)->ah_btCoexDisable)((_ah)))

#define	ath_hal_btcoex_mci_setup(_ah, _gp, _gb, _gl, _sp) \
	((*(_ah)->ah_btMciSetup)((_ah), (_gp), (_gb), (_gl), (_sp)))
#define	ath_hal_btcoex_mci_send_message(_ah, _h, _f, _p, _l, _wd, _cbt) \
	((*(_ah)->ah_btMciSendMessage)((_ah), (_h), (_f), (_p), (_l), (_wd), (_cbt)))
#define	ath_hal_btcoex_mci_get_interrupt(_ah, _mi, _mm) \
	((*(_ah)->ah_btMciGetInterrupt)((_ah), (_mi), (_mm)))
#define	ath_hal_btcoex_mci_state(_ah, _st, _pd) \
	((*(_ah)->ah_btMciState)((_ah), (_st), (_pd)))
#define	ath_hal_btcoex_mci_detach(_ah) \
	((*(_ah)->ah_btMciDetach)((_ah)))

#define	ath_hal_div_comb_conf_get(_ah, _conf) \
	((*(_ah)->ah_divLnaConfGet)((_ah), (_conf)))
#define	ath_hal_div_comb_conf_set(_ah, _conf) \
	((*(_ah)->ah_divLnaConfSet)((_ah), (_conf)))

#endif /* _DEV_ATH_ATHVAR_H */
