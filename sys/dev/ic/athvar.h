/*      $OpenBSD: athvar.h,v 1.36 2023/03/26 08:45:27 jsg Exp $  */
/*	$NetBSD: athvar.h,v 1.10 2004/08/10 01:03:53 dyoung Exp $	*/

/*-
 * Copyright (c) 2002-2004 Sam Leffler, Errno Consulting
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
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
 * $FreeBSD: src/sys/dev/ath/if_athvar.h,v 1.14 2004/04/03 03:33:02 sam Exp $
 */

/*
 * Definitions for the Atheros Wireless LAN controller driver.
 */
#ifndef _DEV_ATH_ATHVAR_H
#define _DEV_ATH_ATHVAR_H

#ifdef _KERNEL

#include <net80211/ieee80211_radiotap.h>
#include <dev/ic/ar5xxx.h>

#include "bpfilter.h"

#ifdef notyet
#include "gpio.h"
#endif

#define	ATH_TIMEOUT		1000

#define	ATH_RXBUF	40		/* number of RX buffers */
#define	ATH_TXBUF	60		/* number of TX buffers */
#define	ATH_TXDESC	8		/* number of descriptors per buffer */
#define ATH_MAXGPIO	10		/* maximal number of gpio pins */

struct ath_recv_hist {
	int		arh_ticks;	/* sample time by system clock */
	u_int8_t	arh_rssi;	/* rssi */
	u_int8_t	arh_antenna;	/* antenna */
};
#define	ATH_RHIST_SIZE		16	/* number of samples */
#define	ATH_RHIST_NOTIME	(~0)

/*
 * Ioctl-related definitions for the Atheros Wireless LAN controller driver.
 */
struct ath_stats {
	u_int32_t	ast_watchdog;	/* device reset by watchdog */
	u_int32_t	ast_hardware;	/* fatal hardware error interrupts */
	u_int32_t	ast_bmiss;	/* beacon miss interrupts */
	u_int32_t	ast_mib;	/* MIB counter interrupts */
	u_int32_t	ast_rxorn;	/* rx overrun interrupts */
	u_int32_t	ast_rxeol;	/* rx eol interrupts */
	u_int32_t	ast_txurn;	/* tx underrun interrupts */
	u_int32_t	ast_intrcoal;	/* interrupts coalesced */
	u_int32_t	ast_tx_mgmt;	/* management frames transmitted */
	u_int32_t	ast_tx_discard;	/* frames discarded prior to assoc */
	u_int32_t	ast_tx_qstop;	/* output stopped 'cuz no buffer */
	u_int32_t	ast_tx_encap;	/* tx encapsulation failed */
	u_int32_t	ast_tx_nonode;	/* tx failed 'cuz no node */
	u_int32_t	ast_tx_nombuf;	/* tx failed 'cuz no mbuf */
	u_int32_t	ast_tx_nomcl;	/* tx failed 'cuz no cluster */
	u_int32_t	ast_tx_linear;	/* tx linearized to cluster */
	u_int32_t	ast_tx_nodata;	/* tx discarded empty frame */
	u_int32_t	ast_tx_busdma;	/* tx failed for dma resrcs */
	u_int32_t	ast_tx_xretries;/* tx failed 'cuz too many retries */
	u_int32_t	ast_tx_fifoerr;	/* tx failed 'cuz FIFO underrun */
	u_int32_t	ast_tx_filtered;/* tx failed 'cuz xmit filtered */
	u_int32_t	ast_tx_shortretry;/* tx on-chip retries (short) */
	u_int32_t	ast_tx_longretry;/* tx on-chip retries (long) */
	u_int32_t	ast_tx_badrate;	/* tx failed 'cuz bogus xmit rate */
	u_int32_t	ast_tx_noack;	/* tx frames with no ack marked */
	u_int32_t	ast_tx_rts;	/* tx frames with rts enabled */
	u_int32_t	ast_tx_cts;	/* tx frames with cts enabled */
	u_int32_t	ast_tx_shortpre;/* tx frames with short preamble */
	u_int32_t	ast_tx_altrate;	/* tx frames with alternate rate */
	u_int32_t	ast_tx_protect;	/* tx frames with protection */
	u_int32_t	ast_rx_nombuf;	/* rx setup failed 'cuz no mbuf */
	u_int32_t	ast_rx_busdma;	/* rx setup failed for dma resrcs */
	u_int32_t	ast_rx_orn;	/* rx failed 'cuz of desc overrun */
	u_int32_t	ast_rx_crcerr;	/* rx failed 'cuz of bad CRC */
	u_int32_t	ast_rx_fifoerr;	/* rx failed 'cuz of FIFO overrun */
	u_int32_t	ast_rx_badcrypt;/* rx failed 'cuz decryption */
	u_int32_t	ast_rx_phyerr;	/* rx failed 'cuz of PHY err */
	u_int32_t	ast_rx_phy[32];	/* rx PHY error per-code counts */
	u_int32_t	ast_rx_tooshort;/* rx discarded 'cuz frame too short */
	u_int32_t	ast_rx_toobig;	/* rx discarded 'cuz frame too large */
	u_int32_t	ast_rx_ctl;	/* rx discarded 'cuz ctl frame */
	u_int32_t	ast_be_nombuf;	/* beacon setup failed 'cuz no mbuf */
	u_int32_t	ast_per_cal;	/* periodic calibration calls */
	u_int32_t	ast_per_calfail;/* periodic calibration failed */
	u_int32_t	ast_per_rfgain;	/* periodic calibration rfgain reset */
	u_int32_t	ast_rate_calls;	/* rate control checks */
	u_int32_t	ast_rate_raise;	/* rate control raised xmit rate */
	u_int32_t	ast_rate_drop;	/* rate control dropped xmit rate */
};

/*
 * Radio capture format.
 */
#define ATH_RX_RADIOTAP_PRESENT (		\
	(1 << IEEE80211_RADIOTAP_FLAGS)		| \
	(1 << IEEE80211_RADIOTAP_RATE)		| \
	(1 << IEEE80211_RADIOTAP_CHANNEL)	| \
	(1 << IEEE80211_RADIOTAP_ANTENNA)	| \
	(1 << IEEE80211_RADIOTAP_RSSI)		| \
	0)

struct ath_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	u_int8_t	wr_flags;
	u_int8_t	wr_rate;
	u_int16_t	wr_chan_freq;
	u_int16_t	wr_chan_flags;
	u_int8_t	wr_antenna;
	u_int8_t	wr_rssi;
	u_int8_t	wr_max_rssi;
} __packed;

#define ATH_TX_RADIOTAP_PRESENT (		\
	(1 << IEEE80211_RADIOTAP_FLAGS)		| \
	(1 << IEEE80211_RADIOTAP_RATE)		| \
	(1 << IEEE80211_RADIOTAP_CHANNEL)	| \
	(1 << IEEE80211_RADIOTAP_DBM_TX_POWER)	| \
	(1 << IEEE80211_RADIOTAP_ANTENNA)	| \
	0)

struct ath_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	u_int8_t	wt_flags;
	u_int8_t	wt_rate;
	u_int16_t	wt_chan_freq;
	u_int16_t	wt_chan_flags;
	u_int8_t	wt_txpower;
	u_int8_t	wt_antenna;
} __packed;

/* 
 * driver-specific node 
 */
struct ath_node {
	struct ieee80211_node		an_node;	/* base class */
	struct ieee80211_rssadapt	an_rssadapt;	/* rate adaption */
	u_int				an_tx_antenna;	/* antenna for last good frame */
	u_int				an_rx_antenna;	/* antenna for last rcvd frame */
	struct ath_recv_hist		an_rx_hist[ATH_RHIST_SIZE];
	u_int				an_rx_hist_next;/* index of next ``free entry'' */
};
#define	ATH_NODE(_n)	((struct ath_node *)(_n))

struct ath_buf {
	TAILQ_ENTRY(ath_buf)		bf_list;
	bus_dmamap_t			bf_dmamap;	/* DMA map of the buffer */
#define bf_nseg				bf_dmamap->dm_nsegs
#define bf_mapsize			bf_dmamap->dm_mapsize
#define bf_segs				bf_dmamap->dm_segs
	struct ath_desc			*bf_desc;	/* virtual addr of desc */
	bus_addr_t			bf_daddr;	/* physical addr of desc */
	struct mbuf			*bf_m;		/* mbuf for buf */
	struct ieee80211_node		*bf_node;	/* pointer to the node */
	struct ieee80211_rssdesc	bf_id;
#define	ATH_MAX_SCATTER			64
};

typedef struct ath_task {
	void	(*t_func)(void*, int);
	void	*t_context;
} ath_task_t;

struct ath_softc {
	struct device		sc_dev;
	struct ieee80211com	sc_ic;		/* IEEE 802.11 common */
	int			(*sc_enable)(struct ath_softc *);
	void			(*sc_disable)(struct ath_softc *);
	void			(*sc_power)(struct ath_softc *, int);
	int			(*sc_newstate)(struct ieee80211com *,
					enum ieee80211_state, int);
	void			(*sc_node_free)(struct ieee80211com *,
					struct ieee80211_node *);
	void			(*sc_node_copy)(struct ieee80211com *,
					struct ieee80211_node *,
					const struct ieee80211_node *);
	void			(*sc_recv_mgmt)(struct ieee80211com *,
				    struct mbuf *, struct ieee80211_node *,
				    struct ieee80211_rxinfo *, int);
	bus_space_tag_t		sc_st;		/* bus space tag */
	bus_space_handle_t	sc_sh;		/* bus space handle */
	bus_size_t		sc_ss;		/* bus space size */
	bus_dma_tag_t		sc_dmat;	/* bus DMA tag */
	struct ath_hal		*sc_ah;		/* Atheros HAL */
	unsigned int		sc_invalid : 1,	/* disable hardware accesses */
				sc_doani : 1,	/* dynamic noise immunity */
				sc_veol : 1,	/* tx VEOL support */
				sc_softled : 1,	/* GPIO software LED */
				sc_probing : 1,	/* probing AP on beacon miss */
				sc_pcie : 1;	/* indicates PCI Express */
	u_int			sc_nchan;	/* number of valid channels */
	const HAL_RATE_TABLE	*sc_rates[IEEE80211_MODE_MAX];
	const HAL_RATE_TABLE	*sc_currates;	/* current rate table */
	enum ieee80211_phymode	sc_curmode;	/* current phy mode */
	u_int8_t		sc_rixmap[256];	/* IEEE to h/w rate table ix */
	u_int8_t		sc_hwmap[32];	/* h/w rate ix to IEEE table */
	HAL_INT			sc_imask;	/* interrupt mask copy */

#if NBPFILTER > 0
	caddr_t			sc_drvbpf;

	union {
		struct ath_rx_radiotap_header	th;
		uint8_t				pad[IEEE80211_RADIOTAP_HDRLEN];
	}			sc_rxtapu;
#define sc_rxtap		sc_rxtapu.th
	int			sc_rxtap_len;

	union {
		struct ath_tx_radiotap_header	th;
		uint8_t				pad[IEEE80211_RADIOTAP_HDRLEN];
	}			sc_txtapu;
#define sc_txtap		sc_txtapu.th
	int			sc_txtap_len;
#endif

	struct ath_desc		*sc_desc;	/* TX/RX descriptors */
	bus_dma_segment_t	sc_dseg;
	int			sc_dnseg;	/* number of segments */
	bus_dmamap_t		sc_ddmamap;	/* DMA map for descriptors */
	bus_addr_t		sc_desc_paddr;	/* physical addr of sc_desc */
	bus_addr_t		sc_desc_len;	/* size of sc_desc */

	ath_task_t		sc_fataltask;	/* fatal int processing */
	ath_task_t		sc_rxorntask;	/* rxorn int processing */

	TAILQ_HEAD(, ath_buf)	sc_rxbuf;	/* receive buffer */
	u_int32_t		*sc_rxlink;	/* link ptr in last RX desc */
	ath_task_t		sc_rxtask;	/* rx int processing */

	u_int			sc_txhalq[HAL_NUM_TX_QUEUES];	/* HAL q for outgoing frames */
	u_int32_t		*sc_txlink;	/* link ptr in last TX desc */
	int			sc_tx_timer;	/* transmit timeout */
	TAILQ_HEAD(, ath_buf)	sc_txbuf;	/* transmit buffer */
	TAILQ_HEAD(, ath_buf)	sc_txq;		/* transmitting queue */
	ath_task_t		sc_txtask;	/* tx int processing */

	u_int			sc_bhalq;	/* HAL q for outgoing beacons */
	struct ath_buf		*sc_bcbuf;	/* beacon buffer */
	struct ath_buf		*sc_bufptr;	/* allocated buffer ptr */
	ath_task_t		sc_swbatask;	/* swba int processing */
	ath_task_t		sc_bmisstask;	/* bmiss int processing */

	struct timeval		sc_last_ch;
	struct timeout		sc_cal_to;
	struct timeval		sc_last_beacon;
	struct timeout		sc_scan_to;
	struct timeout		sc_rssadapt_to;
	struct ath_stats	sc_stats;	/* interface statistics */
	HAL_MIB_STATS		sc_mib_stats;	/* MIB counter statistics */

	u_int			sc_flags;	/* misc flags */

	u_int8_t                sc_broadcast_addr[IEEE80211_ADDR_LEN];

	struct gpio_chipset_tag sc_gpio_gc;	/* gpio(4) framework */
	gpio_pin_t		sc_gpio_pins[ATH_MAXGPIO];
};

/* unaligned little endian access */     
#define LE_READ_2(p)							\
	((u_int16_t)							\
	 ((((u_int8_t *)(p))[0]      ) | (((u_int8_t *)(p))[1] <<  8)))
#define LE_READ_4(p)							\
	((u_int32_t)							\
	 ((((u_int8_t *)(p))[0]      ) | (((u_int8_t *)(p))[1] <<  8) |	\
	 (((u_int8_t *)(p))[2] << 16) | (((u_int8_t *)(p))[3] << 24)))

#ifdef AR_DEBUG
enum {
	ATH_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	ATH_DEBUG_XMIT_DESC	= 0x00000002,	/* xmit descriptors */
	ATH_DEBUG_RECV		= 0x00000004,	/* basic recv operation */
	ATH_DEBUG_RECV_DESC	= 0x00000008,	/* recv descriptors */
	ATH_DEBUG_RATE		= 0x00000010,	/* rate control */
	ATH_DEBUG_RESET		= 0x00000020,	/* reset processing */
	ATH_DEBUG_MODE		= 0x00000040,	/* mode init/setup */
	ATH_DEBUG_BEACON	= 0x00000080,	/* beacon handling */
	ATH_DEBUG_WATCHDOG	= 0x00000100,	/* watchdog timeout */
	ATH_DEBUG_INTR		= 0x00001000,	/* ISR */
	ATH_DEBUG_TX_PROC	= 0x00002000,	/* tx ISR proc */
	ATH_DEBUG_RX_PROC	= 0x00004000,	/* rx ISR proc */
	ATH_DEBUG_BEACON_PROC	= 0x00008000,	/* beacon ISR proc */
	ATH_DEBUG_CALIBRATE	= 0x00010000,	/* periodic calibration */
	ATH_DEBUG_ANY		= 0xffffffff
};
#define	IFF_DUMPPKTS(_ifp, _m) \
	((ath_debug & _m) || \
	    ((_ifp)->if_flags & (IFF_DEBUG|IFF_LINK2)) == (IFF_DEBUG|IFF_LINK2))
#define	DPRINTF(_m,X)	if (ath_debug & (_m)) printf X
#else
#define	IFF_DUMPPKTS(_ifp, _m) \
	(((_ifp)->if_flags & (IFF_DEBUG|IFF_LINK2)) == (IFF_DEBUG|IFF_LINK2))
#define	DPRINTF(_m, X)
#endif

/*
 * Wrapper code
 */
#undef KASSERT
#define KASSERT(cond, complaint) if (!(cond)) panic complaint

#define	ATH_ATTACHED		0x0001		/* attach has succeeded */
#define ATH_ENABLED		0x0002		/* chip is enabled */
#define ATH_GPIO		0x0004		/* gpio device attached */

#define	ATH_IS_ENABLED(sc)	((sc)->sc_flags & ATH_ENABLED)

#define	ATH_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->sc_mtx, device_get_nameunit((_sc)->sc_dev), \
		 MTX_NETWORK_LOCK, MTX_DEF | MTX_RECURSE)
#define	ATH_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx)
#define	ATH_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	ATH_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	ATH_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)

#define	ATH_TXBUF_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->sc_txbuflock, \
		device_get_nameunit((_sc)->sc_dev), "xmit buf q", MTX_DEF)
#define	ATH_TXBUF_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_txbuflock)
#define	ATH_TXBUF_LOCK(_sc)		mtx_lock(&(_sc)->sc_txbuflock)
#define	ATH_TXBUF_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_txbuflock)
#define	ATH_TXBUF_LOCK_ASSERT(_sc) \
	mtx_assert(&(_sc)->sc_txbuflock, MA_OWNED)

#define	ATH_TXQ_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->sc_txqlock, \
		device_get_nameunit((_sc)->sc_dev), "xmit q", MTX_DEF)
#define	ATH_TXQ_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_txqlock)
#define	ATH_TXQ_LOCK(_sc)		mtx_lock(&(_sc)->sc_txqlock)
#define	ATH_TXQ_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_txqlock)
#define	ATH_TXQ_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_txqlock, MA_OWNED)

#define ATH_TICKS() (ticks)
#define ATH_CALLOUT_INIT(chp) callout_init((chp))
#define ATH_TASK_INIT(task, func, context)	\
	do {					\
		(task)->t_func = (func);	\
		(task)->t_context = (context);	\
	} while (0)
#define ATH_TASK_RUN_OR_ENQUEUE(task) ((*(task)->t_func)((task)->t_context, 1))

typedef unsigned long u_intptr_t;

int	ath_attach(u_int16_t, struct ath_softc *);
int	ath_detach(struct ath_softc *, int);
int	ath_enable(struct ath_softc *);
int	ath_activate(struct device *, int);
int	ath_intr(void *);
int	ath_enable(struct ath_softc *);

/*
 * HAL definitions to comply with local coding convention.
 */
#define	ath_hal_reset(_ah, _opmode, _chan, _outdoor, _pstatus) \
	((*(_ah)->ah_reset)((_ah), (_opmode), (_chan), (_outdoor), (_pstatus)))
#define	ath_hal_get_rate_table(_ah, _mode) \
	((*(_ah)->ah_get_rate_table)((_ah), (_mode)))
#define	ath_hal_get_lladdr(_ah, _mac) \
	((*(_ah)->ah_get_lladdr)((_ah), (_mac)))
#define	ath_hal_set_lladdr(_ah, _mac) \
	((*(_ah)->ah_set_lladdr)((_ah), (_mac)))
#define	ath_hal_set_intr(_ah, _mask) \
	((*(_ah)->ah_set_intr)((_ah), (_mask)))
#define	ath_hal_get_intr(_ah) \
	((*(_ah)->ah_get_intr)((_ah)))
#define	ath_hal_is_intr_pending(_ah) \
	((*(_ah)->ah_is_intr_pending)((_ah)))
#define	ath_hal_get_isr(_ah, _pmask) \
	((*(_ah)->ah_get_isr)((_ah), (_pmask)))
#define	ath_hal_update_tx_triglevel(_ah, _inc) \
	((*(_ah)->ah_update_tx_triglevel)((_ah), (_inc)))
#define	ath_hal_set_power(_ah, _mode, _sleepduration) \
	((*(_ah)->ah_set_power)((_ah), (_mode), AH_TRUE, (_sleepduration)))
#define	ath_hal_reset_key(_ah, _ix) \
	((*(_ah)->ah_reset_key)((_ah), (_ix)))
#define	ath_hal_set_key(_ah, _ix, _pk) \
	((*(_ah)->ah_set_key)((_ah), (_ix), (_pk), NULL, AH_FALSE))
#define	ath_hal_is_key_valid(_ah, _ix) \
	(((*(_ah)->ah_is_key_valid)((_ah), (_ix))))
#define	ath_hal_set_key_lladdr(_ah, _ix, _mac) \
	((*(_ah)->ah_set_key_lladdr)((_ah), (_ix), (_mac)))
#define	ath_hal_softcrypto(_ah, _val ) \
	((*(_ah)->ah_softcrypto)((_ah), (_val)))
#define	ath_hal_get_rx_filter(_ah) \
	((*(_ah)->ah_get_rx_filter)((_ah)))
#define	ath_hal_set_rx_filter(_ah, _filter) \
	((*(_ah)->ah_set_rx_filter)((_ah), (_filter)))
#define	ath_hal_set_mcast_filter(_ah, _mfilt0, _mfilt1) \
	((*(_ah)->ah_set_mcast_filter)((_ah), (_mfilt0), (_mfilt1)))
#define	ath_hal_wait_for_beacon(_ah, _bf) \
	((*(_ah)->ah_wait_for_beacon)((_ah), (_bf)->bf_daddr))
#define	ath_hal_put_rx_buf(_ah, _bufaddr) \
	((*(_ah)->ah_put_rx_buf)((_ah), (_bufaddr)))
#define	ath_hal_get_tsf32(_ah) \
	((*(_ah)->ah_get_tsf32)((_ah)))
#define	ath_hal_get_tsf64(_ah) \
	((*(_ah)->ah_get_tsf64)((_ah)))
#define	ath_hal_reset_tsf(_ah) \
	((*(_ah)->ah_reset_tsf)((_ah)))
#define	ath_hal_start_rx(_ah) \
	((*(_ah)->ah_start_rx)((_ah)))
#define	ath_hal_put_tx_buf(_ah, _q, _bufaddr) \
	((*(_ah)->ah_put_tx_buf)((_ah), (_q), (_bufaddr)))
#define	ath_hal_get_tx_buf(_ah, _q) \
	((*(_ah)->ah_get_tx_buf)((_ah), (_q)))
#define	ath_hal_get_rx_buf(_ah) \
	((*(_ah)->ah_get_rx_buf)((_ah)))
#define	ath_hal_tx_start(_ah, _q) \
	((*(_ah)->ah_tx_start)((_ah), (_q)))
#define	ath_hal_setchannel(_ah, _chan) \
	((*(_ah)->ah_setchannel)((_ah), (_chan)))
#define	ath_hal_calibrate(_ah, _chan) \
	((*(_ah)->ah_calibrate)((_ah), (_chan)))
#define	ath_hal_set_ledstate(_ah, _state) \
	((*(_ah)->ah_set_ledstate)((_ah), (_state)))
#define	ath_hal_init_beacon(_ah, _nextb, _bperiod) \
	((*(_ah)->ah_init_beacon)((_ah), (_nextb), (_bperiod)))
#define	ath_hal_reset_beacon(_ah) \
	((*(_ah)->ah_reset_beacon)((_ah)))
#define	ath_hal_set_beacon_timers(_ah, _bs, _tsf, _dc, _cc) \
	((*(_ah)->ah_set_beacon_timers)((_ah), (_bs), (_tsf), \
	 (_dc), (_cc)))
#define	ath_hal_set_associd(_ah, _bss, _associd) \
	((*(_ah)->ah_set_associd)((_ah), (_bss), (_associd), 0))
#define	ath_hal_get_regdomain(_ah, _prd) \
	(*(_prd) = (_ah)->ah_get_regdomain(_ah))
#define	ath_hal_detach(_ah) \
	((*(_ah)->ah_detach)(_ah))
#define ath_hal_set_slot_time(_ah, _t) \
	((*(_ah)->ah_set_slot_time)(_ah, _t))
#define ath_hal_set_gpio_output(_ah, _gpio) \
	((*(_ah)->ah_set_gpio_output)((_ah), (_gpio)))
#define ath_hal_set_gpio_input(_ah, _gpio) \
	((*(_ah)->ah_set_gpio_input)((_ah), (_gpio)))
#define ath_hal_get_gpio(_ah, _gpio) \
	((*(_ah)->ah_get_gpio)((_ah), (_gpio)))
#define ath_hal_set_gpio(_ah, _gpio, _b) \
	((*(_ah)->ah_set_gpio)((_ah), (_gpio), (_b)))
#define ath_hal_set_gpio_intr(_ah, _gpio, _b) \
	((*(_ah)->ah_set_gpio_intr)((_ah), (_gpio), (_b)))

#define	ath_hal_set_opmode(_ah) \
	((*(_ah)->ah_set_opmode)((_ah)))
#define	ath_hal_stop_tx_dma(_ah, _qnum) \
	((*(_ah)->ah_stop_tx_dma)((_ah), (_qnum)))
#define	ath_hal_stop_pcu_recv(_ah) \
	((*(_ah)->ah_stop_pcu_recv)((_ah)))
#define	ath_hal_start_rx_pcu(_ah) \
	((*(_ah)->ah_start_rx_pcu)((_ah)))
#define	ath_hal_stop_rx_dma(_ah) \
	((*(_ah)->ah_stop_rx_dma)((_ah)))
#define	ath_hal_get_diag_state(_ah, _id, _indata, _insize, _outdata, _outsize) \
	((*(_ah)->ah_get_diag_state)((_ah), (_id), \
	 (_indata), (_insize), (_outdata), (_outsize)))

#define	ath_hal_setup_tx_queue(_ah, _type, _qinfo) \
	((*(_ah)->ah_setup_tx_queue)((_ah), (_type), (_qinfo)))
#define	ath_hal_reset_tx_queue(_ah, _q) \
	((*(_ah)->ah_reset_tx_queue)((_ah), (_q)))
#define	ath_hal_release_tx_queue(_ah, _q) \
	((*(_ah)->ah_release_tx_queue)((_ah), (_q)))
#define	ath_hal_has_veol(_ah) \
	((*(_ah)->ah_has_veol)((_ah)))
#define ath_hal_update_mib_counters(_ah, _stats) \
	((*(_ah)->ah_update_mib_counters)((_ah), (_stats)))
#define	ath_hal_get_rf_gain(_ah) \
	((*(_ah)->ah_get_rf_gain)((_ah)))
#define	ath_hal_set_rx_signal(_ah) \
	((*(_ah)->ah_set_rx_signal)((_ah)))

#define	ath_hal_setup_rx_desc(_ah, _ds, _size, _intreq) \
	((*(_ah)->ah_setup_rx_desc)((_ah), (_ds), (_size), (_intreq)))
#define	ath_hal_proc_rx_desc(_ah, _ds, _dspa, _dsnext) \
	((*(_ah)->ah_proc_rx_desc)((_ah), (_ds), (_dspa), (_dsnext)))
#define	ath_hal_setup_tx_desc(_ah, _ds, _plen, _hlen, _atype, _txpow, \
	    _txr0, _txtr0, _keyix, _ant, _flags, \
	    _rtsrate, _rtsdura) \
	((*(_ah)->ah_setup_tx_desc)((_ah), (_ds), (_plen), (_hlen), (_atype), \
	    (_txpow), (_txr0), (_txtr0), (_keyix), (_ant), \
	    (_flags), (_rtsrate), (_rtsdura)))
#define	ath_hal_setup_xtx_desc(_ah, _ds, \
	    _txr1, _txtr1, _txr2, _txtr2, _txr3, _txtr3) \
	((*(_ah)->ah_setup_xtx_desc)((_ah), (_ds), \
	    (_txr1), (_txtr1), (_txr2), (_txtr2), (_txr3), (_txtr3)))
#define	ath_hal_fill_tx_desc(_ah, _ds, _l, _first, _last) \
	((*(_ah)->ah_fill_tx_desc)((_ah), (_ds), (_l), (_first), (_last)))
#define	ath_hal_proc_tx_desc(_ah, _ds) \
	((*(_ah)->ah_proc_tx_desc)((_ah), (_ds)))

#endif /* _KERNEL */

#define	SIOCGATHSTATS	_IOWR('i', 137, struct ifreq)

#endif /* _DEV_ATH_ATHVAR_H */
