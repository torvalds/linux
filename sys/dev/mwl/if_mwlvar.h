/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2009 Marvell Semiconductor, Inc.
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
 * Definitions for the Marvell 88W8363 Wireless LAN controller.
 */
#ifndef _DEV_MWL_MVVAR_H
#define _DEV_MWL_MVVAR_H

#include <sys/endian.h>
#include <sys/bus.h>
#include <net80211/ieee80211_radiotap.h>
#include <dev/mwl/mwlhal.h>
#include <dev/mwl/mwlreg.h>
#include <dev/mwl/if_mwlioctl.h>

#ifndef MWL_TXBUF
#define MWL_TXBUF	256		/* number of TX descriptors/buffers */
#endif
#ifndef MWL_TXACKBUF
#define MWL_TXACKBUF	(MWL_TXBUF/2)	/* number of TX ACK desc's/buffers */
#endif
#ifndef MWL_RXDESC
#define MWL_RXDESC	256		/* number of RX descriptors */
#endif
#ifndef MWL_RXBUF
#define MWL_RXBUF	((5*MWL_RXDESC)/2)/* number of RX dma buffers */
#endif
#ifndef MWL_MAXBA
#define	MWL_MAXBA	2		/* max BA streams/sta */
#endif

#ifdef MWL_SGDMA_SUPPORT
#define	MWL_TXDESC	6		/* max tx descriptors/segments */
#else
#define	MWL_TXDESC	1		/* max tx descriptors/segments */
#endif
#ifndef MWL_AGGR_SIZE
#define	MWL_AGGR_SIZE	3839		/* max tx aggregation size */
#endif
#define	MWL_AGEINTERVAL	1		/* poke f/w every sec to age q's */ 
#define	MWL_MAXSTAID	64		/* max of 64 stations */

/*
 * DMA state for tx/rx descriptors.
 */

/*
 * Software backed version of tx/rx descriptors.  We keep
 * the software state out of the h/w descriptor structure
 * so that may be allocated in uncached memory w/o paying
 * performance hit.
 */
struct mwl_txbuf {
	STAILQ_ENTRY(mwl_txbuf) bf_list;
	void 		*bf_desc;	/* h/w descriptor */
	bus_addr_t	bf_daddr;	/* physical addr of desc */
	bus_dmamap_t	bf_dmamap;	/* DMA map for descriptors */
	int		bf_nseg;
	bus_dma_segment_t bf_segs[MWL_TXDESC];
	struct mbuf	*bf_m;
	struct ieee80211_node *bf_node;
	struct mwl_txq	*bf_txq;		/* backpointer to tx q/ring */
};
typedef STAILQ_HEAD(, mwl_txbuf) mwl_txbufhead;

/*
 * Common "base class" for tx/rx descriptor resources
 * allocated using the bus dma api.
 */
struct mwl_descdma {
	const char*		dd_name;
	void			*dd_desc;	/* descriptors */
	bus_addr_t		dd_desc_paddr;	/* physical addr of dd_desc */
	bus_size_t		dd_desc_len;	/* size of dd_desc */
	bus_dma_segment_t	dd_dseg;
	int			dd_dnseg;	/* number of segments */
	bus_dma_tag_t		dd_dmat;	/* bus DMA tag */
	bus_dmamap_t		dd_dmamap;	/* DMA map for descriptors */
	void			*dd_bufptr;	/* associated buffers */
};

/*
 * TX/RX ring definitions.  There are 4 tx rings, one
 * per AC, and 1 rx ring.  Note carefully that transmit
 * descriptors are treated as a contiguous chunk and the
 * firmware pre-fetches descriptors.  This means that we
 * must preserve order when moving descriptors between
 * the active+free lists; otherwise we may stall transmit.
 */
struct mwl_txq {
	struct mwl_descdma dma;		/* bus dma resources */
	struct mtx	lock;		/* tx q lock */
	char		name[12];	/* e.g. "mwl0_txq4" */
	int		qnum;		/* f/w q number */
	int		txpri;		/* f/w tx priority */
	int		nfree;		/* # buffers on free list */
	mwl_txbufhead	free;		/* queue of free buffers */
	mwl_txbufhead	active;		/* queue of active buffers */
};

#define	MWL_TXQ_LOCK_INIT(_sc, _tq) do { \
	snprintf((_tq)->name, sizeof((_tq)->name), "%s_txq%u", \
		device_get_nameunit((_sc)->sc_dev), (_tq)->qnum); \
	mtx_init(&(_tq)->lock, (_tq)->name, NULL, MTX_DEF); \
} while (0)
#define	MWL_TXQ_LOCK_DESTROY(_tq)	mtx_destroy(&(_tq)->lock)
#define	MWL_TXQ_LOCK(_tq)		mtx_lock(&(_tq)->lock)
#define	MWL_TXQ_UNLOCK(_tq)		mtx_unlock(&(_tq)->lock)
#define	MWL_TXQ_LOCK_ASSERT(_tq)	mtx_assert(&(_tq)->lock, MA_OWNED)

#define	MWL_TXDESC_SYNC(txq, ds, how) do { \
	bus_dmamap_sync((txq)->dma.dd_dmat, (txq)->dma.dd_dmamap, how); \
} while(0)

/*
 * RX dma buffers that are not in use are kept on a list.
 */
struct mwl_jumbo {
	SLIST_ENTRY(mwl_jumbo) next;
};
typedef SLIST_HEAD(, mwl_jumbo) mwl_jumbohead;

#define	MWL_JUMBO_DATA2BUF(_data)	((struct mwl_jumbo *)(_data))
#define	MWL_JUMBO_BUF2DATA(_buf)		((uint8_t *)(_buf))
#define	MWL_JUMBO_OFFSET(_sc, _data) \
	(((const uint8_t *)(_data)) - (const uint8_t *)((_sc)->sc_rxmem))
#define	MWL_JUMBO_DMA_ADDR(_sc, _data) \
	((_sc)->sc_rxmem_paddr + MWL_JUMBO_OFFSET(_sc, _data))

struct mwl_rxbuf {
	STAILQ_ENTRY(mwl_rxbuf) bf_list;
	void 		*bf_desc;	/* h/w descriptor */
	bus_addr_t	bf_daddr;	/* physical addr of desc */
	uint8_t		*bf_data;	/* rx data area */
};
typedef STAILQ_HEAD(, mwl_rxbuf) mwl_rxbufhead;

#define	MWL_RXDESC_SYNC(sc, ds, how) do { \
	bus_dmamap_sync((sc)->sc_rxdma.dd_dmat, (sc)->sc_rxdma.dd_dmamap, how);\
} while (0)

/*
 * BA stream state.  One of these is setup for each stream
 * allocated/created for use.  We pre-allocate the h/w stream
 * before sending ADDBA request then complete the setup when
 * get ADDBA response (success).  The completed state is setup
 * to optimize the fast path in mwl_txstart--we precalculate
 * the QoS control bits in the outbound frame and use those
 * to identify which BA stream to use (assigning the h/w q to
 * the TxPriority field of the descriptor).
 *
 * NB: Each station may have at most MWL_MAXBA streams at one time.  
 */
struct mwl_bastate {
	uint16_t	qos;		/* QoS ctl for BA stream */
	uint8_t		txq;		/* h/w q for BA stream */
	const MWL_HAL_BASTREAM *bastream; /* A-MPDU BA stream */
};

static __inline__ void
mwl_bastream_setup(struct mwl_bastate *bas, int tid, int txq)
{
	bas->txq = txq;
	bas->qos = htole16(tid | IEEE80211_QOS_ACKPOLICY_BA);
}

static __inline__ void
mwl_bastream_free(struct mwl_bastate *bas)
{
	bas->qos = 0;
	bas->bastream = NULL;
	/* NB: don't need to clear txq */
}

/*
 * Check the QoS control bits from an outbound frame against the
 * value calculated when a BA stream is setup (above).  We need
 * to match the TID and also the ACK policy so we only match AMPDU
 * frames.  The bits from the frame are assumed in network byte
 * order, hence the potential byte swap.
 */
static __inline__ int
mwl_bastream_match(const struct mwl_bastate *bas, uint16_t qos)
{
	return (qos & htole16(IEEE80211_QOS_TID|IEEE80211_QOS_ACKPOLICY)) ==
	    bas->qos;
}

/* driver-specific node state */
struct mwl_node {
	struct ieee80211_node mn_node;	/* base class */
	struct mwl_ant_info mn_ai;	/* antenna info */
	uint32_t	mn_avgrssi;	/* average rssi over all rx frames */
	uint16_t	mn_staid;	/* firmware station id */
	struct mwl_bastate mn_ba[MWL_MAXBA];
	struct mwl_hal_vap *mn_hvap;	/* hal vap handle */
};
#define	MWL_NODE(ni)		((struct mwl_node *)(ni))
#define	MWL_NODE_CONST(ni)	((const struct mwl_node *)(ni))

/*
 * Driver-specific vap state.
 */
struct mwl_vap {
	struct ieee80211vap mv_vap;		/* base class */
	struct mwl_hal_vap *mv_hvap;		/* hal vap handle */
	struct mwl_hal_vap *mv_ap_hvap;		/* ap hal vap handle for wds */
	uint16_t	mv_last_ps_sta;		/* last count of ps sta's */
	uint16_t	mv_eapolformat;		/* fixed tx rate for EAPOL */
	int		(*mv_newstate)(struct ieee80211vap *,
				    enum ieee80211_state, int);
	int		(*mv_set_tim)(struct ieee80211_node *, int);
};
#define	MWL_VAP(vap)	((struct mwl_vap *)(vap))
#define	MWL_VAP_CONST(vap)	((const struct mwl_vap *)(vap))

struct mwl_softc {
	struct ieee80211com	sc_ic;
	struct mbufq		sc_snd;
	struct mwl_stats	sc_stats;	/* interface statistics */
	int			sc_debug;
	device_t		sc_dev;
	bus_dma_tag_t		sc_dmat;	/* bus DMA tag */
	bus_space_handle_t	sc_io0h;	/* BAR 0 */
	bus_space_tag_t		sc_io0t;
	bus_space_handle_t	sc_io1h;	/* BAR 1 */
	bus_space_tag_t		sc_io1t;
	struct mtx		sc_mtx;		/* master lock (recursive) */
	struct taskqueue	*sc_tq;		/* private task queue */
	struct callout	sc_watchdog;
	int			sc_tx_timer;
	unsigned int		sc_running : 1,
				sc_invalid : 1,	/* disable hardware accesses */
				sc_recvsetup:1,	/* recv setup */
				sc_csapending:1,/* 11h channel switch pending */
				sc_radarena : 1,/* radar detection enabled */
				sc_rxblocked: 1;/* rx waiting for dma buffers */

	struct mwl_hal		*sc_mh;		/* h/w access layer */
	struct mwl_hal_vap	*sc_hvap;	/* hal vap handle */
	struct mwl_hal_hwspec	sc_hwspecs;	/* h/w capabilities */
	uint32_t		sc_fwrelease;	/* release # of loaded f/w */
	struct mwl_hal_txrxdma	sc_hwdma;	/* h/w dma setup */
	uint32_t		sc_imask;	/* interrupt mask copy */
	enum ieee80211_phymode	sc_curmode;
	u_int16_t		sc_curaid;	/* current association id */
	u_int8_t		sc_curbssid[IEEE80211_ADDR_LEN];
	MWL_HAL_CHANNEL		sc_curchan;
	MWL_HAL_TXRATE_HANDLING	sc_txratehandling;
	u_int16_t		sc_rxantenna;	/* rx antenna */
	u_int16_t		sc_txantenna;	/* tx antenna */
	uint8_t			sc_napvaps;	/* # ap mode vaps */
	uint8_t			sc_nwdsvaps;	/* # wds mode vaps */
	uint8_t			sc_nstavaps;	/* # sta mode vaps */
	uint8_t			sc_ndwdsvaps;	/* # sta mode dwds vaps */
	uint8_t			sc_nbssid0;	/* # vap's using base mac */
	uint32_t		sc_bssidmask;	/* bssid mask */

	void			(*sc_recv_mgmt)(struct ieee80211com *,
				    struct mbuf *,
				    struct ieee80211_node *,
				    int, int, int, u_int32_t);
	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);
	void 			(*sc_node_cleanup)(struct ieee80211_node *);
	void 			(*sc_node_drain)(struct ieee80211_node *);
	int			(*sc_recv_action)(struct ieee80211_node *,
				    const struct ieee80211_frame *,
				    const uint8_t *, const uint8_t *);
	int			(*sc_addba_request)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *,
				    int dialogtoken, int baparamset,
				    int batimeout);
	int			(*sc_addba_response)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *,
				    int status, int baparamset,
				    int batimeout);
	void			(*sc_addba_stop)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *);

	struct mwl_descdma	sc_rxdma;	/* rx bus dma resources */
	mwl_rxbufhead		sc_rxbuf;	/* rx buffers */
	struct mwl_rxbuf	*sc_rxnext;	/* next rx buffer to process */
	struct task		sc_rxtask;	/* rx int processing */
	void			*sc_rxmem;	/* rx dma buffer pool */
	bus_dma_tag_t		sc_rxdmat;	/* rx bus DMA tag */
	bus_size_t		sc_rxmemsize;	/* rx dma buffer pool size */
	bus_dmamap_t		sc_rxmap;	/* map for rx dma buffers */
	bus_addr_t		sc_rxmem_paddr;	/* physical addr of sc_rxmem */
	mwl_jumbohead		sc_rxfree;	/* list of free dma buffers */
	int			sc_nrxfree;	/* # buffers on rx free list */
	struct mtx		sc_rxlock;	/* lock on sc_rxfree */

	struct mwl_txq		sc_txq[MWL_NUM_TX_QUEUES];
	struct mwl_txq		*sc_ac2q[5];	/* WME AC -> h/w q map */
	struct mbuf		*sc_aggrq;	/* aggregation q */
	struct task		sc_txtask;	/* tx int processing */
	struct task		sc_bawatchdogtask;/* BA watchdog processing */

	struct task		sc_radartask;	/* radar detect processing */
	struct task		sc_chanswitchtask;/* chan switch processing */

	uint8_t			sc_staid[MWL_MAXSTAID/NBBY];
	int			sc_ageinterval;
	struct callout		sc_timer;	/* periodic work */

	struct mwl_tx_radiotap_header sc_tx_th;
	struct mwl_rx_radiotap_header sc_rx_th;
};

#define	MWL_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->sc_mtx, device_get_nameunit((_sc)->sc_dev), \
		 NULL, MTX_DEF | MTX_RECURSE)
#define	MWL_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx)
#define	MWL_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	MWL_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	MWL_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)

#define	MWL_RXFREE_INIT(_sc) \
	mtx_init(&(_sc)->sc_rxlock, device_get_nameunit((_sc)->sc_dev), \
		 NULL, MTX_DEF)
#define	MWL_RXFREE_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_rxlock)
#define	MWL_RXFREE_LOCK(_sc)	mtx_lock(&(_sc)->sc_rxlock)
#define	MWL_RXFREE_UNLOCK(_sc)	mtx_unlock(&(_sc)->sc_rxlock)
#define	MWL_RXFREE_ASSERT(_sc)	mtx_assert(&(_sc)->sc_rxlock, MA_OWNED)

int	mwl_attach(u_int16_t, struct mwl_softc *);
int	mwl_detach(struct mwl_softc *);
void	mwl_resume(struct mwl_softc *);
void	mwl_suspend(struct mwl_softc *);
void	mwl_shutdown(void *);
void	mwl_intr(void *);

#endif /* _DEV_MWL_MVVAR_H */
