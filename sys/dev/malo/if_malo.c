/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Weongyo Jeong <weongyo@freebsd.org>
 * Copyright (c) 2007 Marvell Semiconductor, Inc.
 * Copyright (c) 2007 Sam Leffler, Errno Consulting
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
 */

#include <sys/cdefs.h>
#ifdef __FreeBSD__
__FBSDID("$FreeBSD$");
#endif

#include "opt_malo.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>

#include <net/bpf.h>

#include <dev/malo/if_malo.h>

SYSCTL_NODE(_hw, OID_AUTO, malo, CTLFLAG_RD, 0,
    "Marvell 88w8335 driver parameters");

static	int malo_txcoalesce = 8;	/* # tx pkts to q before poking f/w*/
SYSCTL_INT(_hw_malo, OID_AUTO, txcoalesce, CTLFLAG_RWTUN, &malo_txcoalesce,
	    0, "tx buffers to send at once");
static	int malo_rxbuf = MALO_RXBUF;		/* # rx buffers to allocate */
SYSCTL_INT(_hw_malo, OID_AUTO, rxbuf, CTLFLAG_RWTUN, &malo_rxbuf,
	    0, "rx buffers allocated");
static	int malo_rxquota = MALO_RXBUF;		/* # max buffers to process */
SYSCTL_INT(_hw_malo, OID_AUTO, rxquota, CTLFLAG_RWTUN, &malo_rxquota,
	    0, "max rx buffers to process per interrupt");
static	int malo_txbuf = MALO_TXBUF;		/* # tx buffers to allocate */
SYSCTL_INT(_hw_malo, OID_AUTO, txbuf, CTLFLAG_RWTUN, &malo_txbuf,
	    0, "tx buffers allocated");

#ifdef MALO_DEBUG
static	int malo_debug = 0;
SYSCTL_INT(_hw_malo, OID_AUTO, debug, CTLFLAG_RWTUN, &malo_debug,
	    0, "control debugging printfs");
enum {
	MALO_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	MALO_DEBUG_XMIT_DESC	= 0x00000002,	/* xmit descriptors */
	MALO_DEBUG_RECV		= 0x00000004,	/* basic recv operation */
	MALO_DEBUG_RECV_DESC	= 0x00000008,	/* recv descriptors */
	MALO_DEBUG_RESET	= 0x00000010,	/* reset processing */
	MALO_DEBUG_INTR		= 0x00000040,	/* ISR */
	MALO_DEBUG_TX_PROC	= 0x00000080,	/* tx ISR proc */
	MALO_DEBUG_RX_PROC	= 0x00000100,	/* rx ISR proc */
	MALO_DEBUG_STATE	= 0x00000400,	/* 802.11 state transitions */
	MALO_DEBUG_NODE		= 0x00000800,	/* node management */
	MALO_DEBUG_RECV_ALL	= 0x00001000,	/* trace all frames (beacons) */
	MALO_DEBUG_FW		= 0x00008000,	/* firmware */
	MALO_DEBUG_ANY		= 0xffffffff
};
#define	IS_BEACON(wh)							\
	((wh->i_fc[0] & (IEEE80211_FC0_TYPE_MASK |			\
		IEEE80211_FC0_SUBTYPE_MASK)) ==				\
	 (IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_BEACON))
#define	IFF_DUMPPKTS_RECV(sc, wh)					\
	(((sc->malo_debug & MALO_DEBUG_RECV) &&				\
	  ((sc->malo_debug & MALO_DEBUG_RECV_ALL) || !IS_BEACON(wh))))
#define	IFF_DUMPPKTS_XMIT(sc)						\
	(sc->malo_debug & MALO_DEBUG_XMIT)
#define	DPRINTF(sc, m, fmt, ...) do {				\
	if (sc->malo_debug & (m))				\
		printf(fmt, __VA_ARGS__);			\
} while (0)
#else
#define	DPRINTF(sc, m, fmt, ...) do {				\
	(void) sc;						\
} while (0)
#endif

static MALLOC_DEFINE(M_MALODEV, "malodev", "malo driver dma buffers");

static struct ieee80211vap *malo_vap_create(struct ieee80211com *,
		    const char [IFNAMSIZ], int, enum ieee80211_opmode, int,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    const uint8_t [IEEE80211_ADDR_LEN]);
static  void	malo_vap_delete(struct ieee80211vap *);
static	int	malo_dma_setup(struct malo_softc *);
static	int	malo_setup_hwdma(struct malo_softc *);
static	void	malo_txq_init(struct malo_softc *, struct malo_txq *, int);
static	void	malo_tx_cleanupq(struct malo_softc *, struct malo_txq *);
static	void	malo_parent(struct ieee80211com *);
static	int	malo_transmit(struct ieee80211com *, struct mbuf *);
static	void	malo_start(struct malo_softc *);
static	void	malo_watchdog(void *);
static	void	malo_updateslot(struct ieee80211com *);
static	int	malo_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static	void	malo_scan_start(struct ieee80211com *);
static	void	malo_scan_end(struct ieee80211com *);
static	void	malo_set_channel(struct ieee80211com *);
static	int	malo_raw_xmit(struct ieee80211_node *, struct mbuf *,
		    const struct ieee80211_bpf_params *);
static	void	malo_sysctlattach(struct malo_softc *);
static	void	malo_announce(struct malo_softc *);
static	void	malo_dma_cleanup(struct malo_softc *);
static	void	malo_stop(struct malo_softc *);
static	int	malo_chan_set(struct malo_softc *, struct ieee80211_channel *);
static	int	malo_mode_init(struct malo_softc *);
static	void	malo_tx_proc(void *, int);
static	void	malo_rx_proc(void *, int);
static	void	malo_init(void *);

/*
 * Read/Write shorthands for accesses to BAR 0.  Note that all BAR 1
 * operations are done in the "hal" except getting H/W MAC address at
 * malo_attach and there should be no reference to them here.
 */
static uint32_t
malo_bar0_read4(struct malo_softc *sc, bus_size_t off)
{
	return bus_space_read_4(sc->malo_io0t, sc->malo_io0h, off);
}

static void
malo_bar0_write4(struct malo_softc *sc, bus_size_t off, uint32_t val)
{
	DPRINTF(sc, MALO_DEBUG_FW, "%s: off 0x%jx val 0x%x\n",
	    __func__, (uintmax_t)off, val);

	bus_space_write_4(sc->malo_io0t, sc->malo_io0h, off, val);
}

int
malo_attach(uint16_t devid, struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->malo_ic;
	struct malo_hal *mh;
	int error;
	uint8_t bands[IEEE80211_MODE_BYTES];

	MALO_LOCK_INIT(sc);
	callout_init_mtx(&sc->malo_watchdog_timer, &sc->malo_mtx, 0);
	mbufq_init(&sc->malo_snd, ifqmaxlen);

	mh = malo_hal_attach(sc->malo_dev, devid,
	    sc->malo_io1h, sc->malo_io1t, sc->malo_dmat);
	if (mh == NULL) {
		device_printf(sc->malo_dev, "unable to attach HAL\n");
		error = EIO;
		goto bad;
	}
	sc->malo_mh = mh;

	/*
	 * Load firmware so we can get setup.  We arbitrarily pick station
	 * firmware; we'll re-load firmware as needed so setting up
	 * the wrong mode isn't a big deal.
	 */
	error = malo_hal_fwload(mh, "malo8335-h", "malo8335-m");
	if (error != 0) {
		device_printf(sc->malo_dev, "unable to setup firmware\n");
		goto bad1;
	}
	/* XXX gethwspecs() extracts correct informations?  not maybe!  */
	error = malo_hal_gethwspecs(mh, &sc->malo_hwspecs);
	if (error != 0) {
		device_printf(sc->malo_dev, "unable to fetch h/w specs\n");
		goto bad1;
	}

	DPRINTF(sc, MALO_DEBUG_FW,
	    "malo_hal_gethwspecs: hwversion 0x%x hostif 0x%x"
	    "maxnum_wcb 0x%x maxnum_mcaddr 0x%x maxnum_tx_wcb 0x%x"
	    "regioncode 0x%x num_antenna 0x%x fw_releasenum 0x%x"
	    "wcbbase0 0x%x rxdesc_read 0x%x rxdesc_write 0x%x"
	    "ul_fw_awakecookie 0x%x w[4] = %x %x %x %x",
	    sc->malo_hwspecs.hwversion,
	    sc->malo_hwspecs.hostinterface, sc->malo_hwspecs.maxnum_wcb,
	    sc->malo_hwspecs.maxnum_mcaddr, sc->malo_hwspecs.maxnum_tx_wcb,
	    sc->malo_hwspecs.regioncode, sc->malo_hwspecs.num_antenna,
	    sc->malo_hwspecs.fw_releasenum, sc->malo_hwspecs.wcbbase0,
	    sc->malo_hwspecs.rxdesc_read, sc->malo_hwspecs.rxdesc_write,
	    sc->malo_hwspecs.ul_fw_awakecookie,
	    sc->malo_hwspecs.wcbbase[0], sc->malo_hwspecs.wcbbase[1],
	    sc->malo_hwspecs.wcbbase[2], sc->malo_hwspecs.wcbbase[3]);

	/* NB: firmware looks that it does not export regdomain info API.  */
	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);
	setbit(bands, IEEE80211_MODE_11G);
	ieee80211_init_channels(ic, NULL, bands);

	sc->malo_txantenna = 0x2;	/* h/w default */
	sc->malo_rxantenna = 0xffff;	/* h/w default */

	/*
	 * Allocate tx + rx descriptors and populate the lists.
	 * We immediately push the information to the firmware
	 * as otherwise it gets upset.
	 */
	error = malo_dma_setup(sc);
	if (error != 0) {
		device_printf(sc->malo_dev,
		    "failed to setup descriptors: %d\n", error);
		goto bad1;
	}
	error = malo_setup_hwdma(sc);	/* push to firmware */
	if (error != 0)			/* NB: malo_setupdma prints msg */
		goto bad2;

	sc->malo_tq = taskqueue_create_fast("malo_taskq", M_NOWAIT,
		taskqueue_thread_enqueue, &sc->malo_tq);
	taskqueue_start_threads(&sc->malo_tq, 1, PI_NET,
		"%s taskq", device_get_nameunit(sc->malo_dev));

	TASK_INIT(&sc->malo_rxtask, 0, malo_rx_proc, sc);
	TASK_INIT(&sc->malo_txtask, 0, malo_tx_proc, sc);

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(sc->malo_dev);
	/* XXX not right but it's not used anywhere important */
	ic->ic_phytype = IEEE80211_T_OFDM;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_caps =
	      IEEE80211_C_STA			/* station mode supported */
	    | IEEE80211_C_BGSCAN		/* capable of bg scanning */
	    | IEEE80211_C_MONITOR		/* monitor mode */
	    | IEEE80211_C_SHPREAMBLE		/* short preamble supported */
	    | IEEE80211_C_SHSLOT		/* short slot time supported */
	    | IEEE80211_C_TXPMGT		/* capable of txpow mgt */
	    | IEEE80211_C_WPA			/* capable of WPA1+WPA2 */
	    ;
	IEEE80211_ADDR_COPY(ic->ic_macaddr, sc->malo_hwspecs.macaddr);

	/*
	 * Transmit requires space in the packet for a special format transmit
	 * record and optional padding between this record and the payload.
	 * Ask the net80211 layer to arrange this when encapsulating
	 * packets so we can add it efficiently. 
	 */
	ic->ic_headroom = sizeof(struct malo_txrec) -
		sizeof(struct ieee80211_frame);

	/* call MI attach routine. */
	ieee80211_ifattach(ic);
	/* override default methods */
	ic->ic_vap_create = malo_vap_create;
	ic->ic_vap_delete = malo_vap_delete;
	ic->ic_raw_xmit = malo_raw_xmit;
	ic->ic_updateslot = malo_updateslot;
	ic->ic_scan_start = malo_scan_start;
	ic->ic_scan_end = malo_scan_end;
	ic->ic_set_channel = malo_set_channel;
	ic->ic_parent = malo_parent;
	ic->ic_transmit = malo_transmit;

	sc->malo_invalid = 0;		/* ready to go, enable int handling */

	ieee80211_radiotap_attach(ic,
	    &sc->malo_tx_th.wt_ihdr, sizeof(sc->malo_tx_th),
		MALO_TX_RADIOTAP_PRESENT,
	    &sc->malo_rx_th.wr_ihdr, sizeof(sc->malo_rx_th),
		MALO_RX_RADIOTAP_PRESENT);

	/*
	 * Setup dynamic sysctl's.
	 */
	malo_sysctlattach(sc);

	if (bootverbose)
		ieee80211_announce(ic);
	malo_announce(sc);

	return 0;
bad2:
	malo_dma_cleanup(sc);
bad1:
	malo_hal_detach(mh);
bad:
	sc->malo_invalid = 1;

	return error;
}

static struct ieee80211vap *
malo_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct malo_softc *sc = ic->ic_softc;
	struct malo_vap *mvp;
	struct ieee80211vap *vap;

	if (!TAILQ_EMPTY(&ic->ic_vaps)) {
		device_printf(sc->malo_dev, "multiple vaps not supported\n");
		return NULL;
	}
	switch (opmode) {
	case IEEE80211_M_STA:
		if (opmode == IEEE80211_M_STA)
			flags |= IEEE80211_CLONE_NOBEACONS;
		/* fall thru... */
	case IEEE80211_M_MONITOR:
		break;
	default:
		device_printf(sc->malo_dev, "%s mode not supported\n",
		    ieee80211_opmode_name[opmode]);
		return NULL;		/* unsupported */
	}
	mvp = malloc(sizeof(struct malo_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &mvp->malo_vap;
	ieee80211_vap_setup(ic, vap, name, unit, opmode, flags, bssid);

	/* override state transition machine */
	mvp->malo_newstate = vap->iv_newstate;
	vap->iv_newstate = malo_newstate;

	/* complete setup */
	ieee80211_vap_attach(vap,
	    ieee80211_media_change, ieee80211_media_status, mac);
	ic->ic_opmode = opmode;
	return vap;
}

static void
malo_vap_delete(struct ieee80211vap *vap)
{
	struct malo_vap *mvp = MALO_VAP(vap);

	ieee80211_vap_detach(vap);
	free(mvp, M_80211_VAP);
}

int
malo_intr(void *arg)
{
	struct malo_softc *sc = arg;
	struct malo_hal *mh = sc->malo_mh;
	uint32_t status;

	if (sc->malo_invalid) {
		/*
		 * The hardware is not ready/present, don't touch anything.
		 * Note this can happen early on if the IRQ is shared.
		 */
		DPRINTF(sc, MALO_DEBUG_ANY, "%s: invalid; ignored\n", __func__);
		return (FILTER_STRAY);
	}

	/*
	 * Figure out the reason(s) for the interrupt.
	 */
	malo_hal_getisr(mh, &status);		/* NB: clears ISR too */
	if (status == 0)			/* must be a shared irq */
		return (FILTER_STRAY);

	DPRINTF(sc, MALO_DEBUG_INTR, "%s: status 0x%x imask 0x%x\n",
	    __func__, status, sc->malo_imask);

	if (status & MALO_A2HRIC_BIT_RX_RDY)
		taskqueue_enqueue(sc->malo_tq, &sc->malo_rxtask);
	if (status & MALO_A2HRIC_BIT_TX_DONE)
		taskqueue_enqueue(sc->malo_tq, &sc->malo_txtask);
	if (status & MALO_A2HRIC_BIT_OPC_DONE)
		malo_hal_cmddone(mh);
	if (status & MALO_A2HRIC_BIT_MAC_EVENT)
		;
	if (status & MALO_A2HRIC_BIT_RX_PROBLEM)
		;
	if (status & MALO_A2HRIC_BIT_ICV_ERROR) {
		/* TKIP ICV error */
		sc->malo_stats.mst_rx_badtkipicv++;
	}
#ifdef MALO_DEBUG
	if (((status | sc->malo_imask) ^ sc->malo_imask) != 0)
		DPRINTF(sc, MALO_DEBUG_INTR,
		    "%s: can't handle interrupt status 0x%x\n",
		    __func__, status);
#endif
	return (FILTER_HANDLED);
}

static void
malo_load_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	bus_addr_t *paddr = (bus_addr_t*) arg;

	KASSERT(error == 0, ("error %u on bus_dma callback", error));

	*paddr = segs->ds_addr;
}

static int
malo_desc_setup(struct malo_softc *sc, const char *name,
    struct malo_descdma *dd,
    int nbuf, size_t bufsize, int ndesc, size_t descsize)
{
	int error;
	uint8_t *ds;

	DPRINTF(sc, MALO_DEBUG_RESET,
	    "%s: %s DMA: %u bufs (%ju) %u desc/buf (%ju)\n",
	    __func__, name, nbuf, (uintmax_t) bufsize,
	    ndesc, (uintmax_t) descsize);
	
	dd->dd_name = name;
	dd->dd_desc_len = nbuf * ndesc * descsize;

	/*
	 * Setup DMA descriptor area.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->malo_dev),/* parent */
		       PAGE_SIZE, 0,		/* alignment, bounds */
		       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		       BUS_SPACE_MAXADDR,	/* highaddr */
		       NULL, NULL,		/* filter, filterarg */
		       dd->dd_desc_len,		/* maxsize */
		       1,			/* nsegments */
		       dd->dd_desc_len,		/* maxsegsize */
		       BUS_DMA_ALLOCNOW,	/* flags */
		       NULL,			/* lockfunc */
		       NULL,			/* lockarg */
		       &dd->dd_dmat);
	if (error != 0) {
		device_printf(sc->malo_dev, "cannot allocate %s DMA tag\n",
		    dd->dd_name);
		return error;
	}
	
	/* allocate descriptors */
	error = bus_dmamem_alloc(dd->dd_dmat, (void**) &dd->dd_desc,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT, &dd->dd_dmamap);
	if (error != 0) {
		device_printf(sc->malo_dev,
		    "unable to alloc memory for %u %s descriptors, "
		    "error %u\n", nbuf * ndesc, dd->dd_name, error);
		goto fail1;
	}

	error = bus_dmamap_load(dd->dd_dmat, dd->dd_dmamap,
	    dd->dd_desc, dd->dd_desc_len,
	    malo_load_cb, &dd->dd_desc_paddr, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->malo_dev,
		    "unable to map %s descriptors, error %u\n",
		    dd->dd_name, error);
		goto fail2;
	}
	
	ds = dd->dd_desc;
	memset(ds, 0, dd->dd_desc_len);
	DPRINTF(sc, MALO_DEBUG_RESET,
	    "%s: %s DMA map: %p (%lu) -> 0x%jx (%lu)\n",
	    __func__, dd->dd_name, ds, (u_long) dd->dd_desc_len,
	    (uintmax_t) dd->dd_desc_paddr, /*XXX*/ (u_long) dd->dd_desc_len);

	return 0;
fail2:
	bus_dmamem_free(dd->dd_dmat, dd->dd_desc, dd->dd_dmamap);
fail1:
	bus_dma_tag_destroy(dd->dd_dmat);
	memset(dd, 0, sizeof(*dd));
	return error;
}

#define	DS2PHYS(_dd, _ds) \
	((_dd)->dd_desc_paddr + ((caddr_t)(_ds) - (caddr_t)(_dd)->dd_desc))

static int
malo_rxdma_setup(struct malo_softc *sc)
{
	int error, bsize, i;
	struct malo_rxbuf *bf;
	struct malo_rxdesc *ds;

	error = malo_desc_setup(sc, "rx", &sc->malo_rxdma,
	    malo_rxbuf, sizeof(struct malo_rxbuf),
	    1, sizeof(struct malo_rxdesc));
	if (error != 0)
		return error;

	/*
	 * Allocate rx buffers and set them up.
	 */
	bsize = malo_rxbuf * sizeof(struct malo_rxbuf);
	bf = malloc(bsize, M_MALODEV, M_NOWAIT | M_ZERO);
	if (bf == NULL) {
		device_printf(sc->malo_dev,
		    "malloc of %u rx buffers failed\n", bsize);
		return error;
	}
	sc->malo_rxdma.dd_bufptr = bf;
	
	STAILQ_INIT(&sc->malo_rxbuf);
	ds = sc->malo_rxdma.dd_desc;
	for (i = 0; i < malo_rxbuf; i++, bf++, ds++) {
		bf->bf_desc = ds;
		bf->bf_daddr = DS2PHYS(&sc->malo_rxdma, ds);
		error = bus_dmamap_create(sc->malo_dmat, BUS_DMA_NOWAIT,
		    &bf->bf_dmamap);
		if (error != 0) {
			device_printf(sc->malo_dev,
			    "%s: unable to dmamap for rx buffer, error %d\n",
			    __func__, error);
			return error;
		}
		/* NB: tail is intentional to preserve descriptor order */
		STAILQ_INSERT_TAIL(&sc->malo_rxbuf, bf, bf_list);
	}
	return 0;
}

static int
malo_txdma_setup(struct malo_softc *sc, struct malo_txq *txq)
{
	int error, bsize, i;
	struct malo_txbuf *bf;
	struct malo_txdesc *ds;

	error = malo_desc_setup(sc, "tx", &txq->dma,
	    malo_txbuf, sizeof(struct malo_txbuf),
	    MALO_TXDESC, sizeof(struct malo_txdesc));
	if (error != 0)
		return error;
	
	/* allocate and setup tx buffers */
	bsize = malo_txbuf * sizeof(struct malo_txbuf);
	bf = malloc(bsize, M_MALODEV, M_NOWAIT | M_ZERO);
	if (bf == NULL) {
		device_printf(sc->malo_dev, "malloc of %u tx buffers failed\n",
		    malo_txbuf);
		return ENOMEM;
	}
	txq->dma.dd_bufptr = bf;
	
	STAILQ_INIT(&txq->free);
	txq->nfree = 0;
	ds = txq->dma.dd_desc;
	for (i = 0; i < malo_txbuf; i++, bf++, ds += MALO_TXDESC) {
		bf->bf_desc = ds;
		bf->bf_daddr = DS2PHYS(&txq->dma, ds);
		error = bus_dmamap_create(sc->malo_dmat, BUS_DMA_NOWAIT,
		    &bf->bf_dmamap);
		if (error != 0) {
			device_printf(sc->malo_dev,
			    "unable to create dmamap for tx "
			    "buffer %u, error %u\n", i, error);
			return error;
		}
		STAILQ_INSERT_TAIL(&txq->free, bf, bf_list);
		txq->nfree++;
	}

	return 0;
}

static void
malo_desc_cleanup(struct malo_softc *sc, struct malo_descdma *dd)
{
	bus_dmamap_unload(dd->dd_dmat, dd->dd_dmamap);
	bus_dmamem_free(dd->dd_dmat, dd->dd_desc, dd->dd_dmamap);
	bus_dma_tag_destroy(dd->dd_dmat);

	memset(dd, 0, sizeof(*dd));
}

static void
malo_rxdma_cleanup(struct malo_softc *sc)
{
	struct malo_rxbuf *bf;

	STAILQ_FOREACH(bf, &sc->malo_rxbuf, bf_list) {
		if (bf->bf_m != NULL) {
			m_freem(bf->bf_m);
			bf->bf_m = NULL;
		}
		if (bf->bf_dmamap != NULL) {
			bus_dmamap_destroy(sc->malo_dmat, bf->bf_dmamap);
			bf->bf_dmamap = NULL;
		}
	}
	STAILQ_INIT(&sc->malo_rxbuf);
	if (sc->malo_rxdma.dd_bufptr != NULL) {
		free(sc->malo_rxdma.dd_bufptr, M_MALODEV);
		sc->malo_rxdma.dd_bufptr = NULL;
	}
	if (sc->malo_rxdma.dd_desc_len != 0)
		malo_desc_cleanup(sc, &sc->malo_rxdma);
}

static void
malo_txdma_cleanup(struct malo_softc *sc, struct malo_txq *txq)
{
	struct malo_txbuf *bf;
	struct ieee80211_node *ni;

	STAILQ_FOREACH(bf, &txq->free, bf_list) {
		if (bf->bf_m != NULL) {
			m_freem(bf->bf_m);
			bf->bf_m = NULL;
		}
		ni = bf->bf_node;
		bf->bf_node = NULL;
		if (ni != NULL) {
			/*
			 * Reclaim node reference.
			 */
			ieee80211_free_node(ni);
		}
		if (bf->bf_dmamap != NULL) {
			bus_dmamap_destroy(sc->malo_dmat, bf->bf_dmamap);
			bf->bf_dmamap = NULL;
		}
	}
	STAILQ_INIT(&txq->free);
	txq->nfree = 0;
	if (txq->dma.dd_bufptr != NULL) {
		free(txq->dma.dd_bufptr, M_MALODEV);
		txq->dma.dd_bufptr = NULL;
	}
	if (txq->dma.dd_desc_len != 0)
		malo_desc_cleanup(sc, &txq->dma);
}

static void
malo_dma_cleanup(struct malo_softc *sc)
{
	int i;

	for (i = 0; i < MALO_NUM_TX_QUEUES; i++)
		malo_txdma_cleanup(sc, &sc->malo_txq[i]);

	malo_rxdma_cleanup(sc);
}

static int
malo_dma_setup(struct malo_softc *sc)
{
	int error, i;

	/* rxdma initializing.  */
	error = malo_rxdma_setup(sc);
	if (error != 0)
		return error;

	/* NB: we just have 1 tx queue now.  */
	for (i = 0; i < MALO_NUM_TX_QUEUES; i++) {
		error = malo_txdma_setup(sc, &sc->malo_txq[i]);
		if (error != 0) {
			malo_dma_cleanup(sc);

			return error;
		}

		malo_txq_init(sc, &sc->malo_txq[i], i);
	}

	return 0;
}

static void
malo_hal_set_rxtxdma(struct malo_softc *sc)
{
	int i;

	malo_bar0_write4(sc, sc->malo_hwspecs.rxdesc_read,
	    sc->malo_hwdma.rxdesc_read);
	malo_bar0_write4(sc, sc->malo_hwspecs.rxdesc_write,
	    sc->malo_hwdma.rxdesc_read);

	for (i = 0; i < MALO_NUM_TX_QUEUES; i++) {
		malo_bar0_write4(sc,
		    sc->malo_hwspecs.wcbbase[i], sc->malo_hwdma.wcbbase[i]);
	}
}

/*
 * Inform firmware of our tx/rx dma setup.  The BAR 0 writes below are
 * for compatibility with older firmware.  For current firmware we send
 * this information with a cmd block via malo_hal_sethwdma.
 */
static int
malo_setup_hwdma(struct malo_softc *sc)
{
	int i;
	struct malo_txq *txq;

	sc->malo_hwdma.rxdesc_read = sc->malo_rxdma.dd_desc_paddr;

	for (i = 0; i < MALO_NUM_TX_QUEUES; i++) {
		txq = &sc->malo_txq[i];
		sc->malo_hwdma.wcbbase[i] = txq->dma.dd_desc_paddr;
	}
	sc->malo_hwdma.maxnum_txwcb = malo_txbuf;
	sc->malo_hwdma.maxnum_wcb = MALO_NUM_TX_QUEUES;

	malo_hal_set_rxtxdma(sc);

	return 0;
}

static void
malo_txq_init(struct malo_softc *sc, struct malo_txq *txq, int qnum)
{
	struct malo_txbuf *bf, *bn;
	struct malo_txdesc *ds;

	MALO_TXQ_LOCK_INIT(sc, txq);
	txq->qnum = qnum;
	txq->txpri = 0;	/* XXX */

	STAILQ_FOREACH(bf, &txq->free, bf_list) {
		bf->bf_txq = txq;

		ds = bf->bf_desc;
		bn = STAILQ_NEXT(bf, bf_list);
		if (bn == NULL)
			bn = STAILQ_FIRST(&txq->free);
		ds->physnext = htole32(bn->bf_daddr);
	}
	STAILQ_INIT(&txq->active);
}

/*
 * Reclaim resources for a setup queue.
 */
static void
malo_tx_cleanupq(struct malo_softc *sc, struct malo_txq *txq)
{
	/* XXX hal work? */
	MALO_TXQ_LOCK_DESTROY(txq);
}

/*
 * Allocate a tx buffer for sending a frame.
 */
static struct malo_txbuf *
malo_getbuf(struct malo_softc *sc, struct malo_txq *txq)
{
	struct malo_txbuf *bf;

	MALO_TXQ_LOCK(txq);
	bf = STAILQ_FIRST(&txq->free);
	if (bf != NULL) {
		STAILQ_REMOVE_HEAD(&txq->free, bf_list);
		txq->nfree--;
	}
	MALO_TXQ_UNLOCK(txq);
	if (bf == NULL) {
		DPRINTF(sc, MALO_DEBUG_XMIT,
		    "%s: out of xmit buffers on q %d\n", __func__, txq->qnum);
		sc->malo_stats.mst_tx_qstop++;
	}
	return bf;
}

static int
malo_tx_dmasetup(struct malo_softc *sc, struct malo_txbuf *bf, struct mbuf *m0)
{
	struct mbuf *m;
	int error;

	/*
	 * Load the DMA map so any coalescing is done.  This also calculates
	 * the number of descriptors we need.
	 */
	error = bus_dmamap_load_mbuf_sg(sc->malo_dmat, bf->bf_dmamap, m0,
				     bf->bf_segs, &bf->bf_nseg,
				     BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		/* XXX packet requires too many descriptors */
		bf->bf_nseg = MALO_TXDESC + 1;
	} else if (error != 0) {
		sc->malo_stats.mst_tx_busdma++;
		m_freem(m0);
		return error;
	}
	/*
	 * Discard null packets and check for packets that require too many
	 * TX descriptors.  We try to convert the latter to a cluster.
	 */
	if (error == EFBIG) {		/* too many desc's, linearize */
		sc->malo_stats.mst_tx_linear++;
		m = m_defrag(m0, M_NOWAIT);
		if (m == NULL) {
			m_freem(m0);
			sc->malo_stats.mst_tx_nombuf++;
			return ENOMEM;
		}
		m0 = m;
		error = bus_dmamap_load_mbuf_sg(sc->malo_dmat, bf->bf_dmamap, m0,
					     bf->bf_segs, &bf->bf_nseg,
					     BUS_DMA_NOWAIT);
		if (error != 0) {
			sc->malo_stats.mst_tx_busdma++;
			m_freem(m0);
			return error;
		}
		KASSERT(bf->bf_nseg <= MALO_TXDESC,
		    ("too many segments after defrag; nseg %u", bf->bf_nseg));
	} else if (bf->bf_nseg == 0) {		/* null packet, discard */
		sc->malo_stats.mst_tx_nodata++;
		m_freem(m0);
		return EIO;
	}
	DPRINTF(sc, MALO_DEBUG_XMIT, "%s: m %p len %u\n",
		__func__, m0, m0->m_pkthdr.len);
	bus_dmamap_sync(sc->malo_dmat, bf->bf_dmamap, BUS_DMASYNC_PREWRITE);
	bf->bf_m = m0;

	return 0;
}

#ifdef MALO_DEBUG
static void
malo_printrxbuf(const struct malo_rxbuf *bf, u_int ix)
{
	const struct malo_rxdesc *ds = bf->bf_desc;
	uint32_t status = le32toh(ds->status);
	
	printf("R[%2u] (DS.V:%p DS.P:0x%jx) NEXT:%08x DATA:%08x RC:%02x%s\n"
	    "      STAT:%02x LEN:%04x SNR:%02x NF:%02x CHAN:%02x"
	    " RATE:%02x QOS:%04x\n", ix, ds, (uintmax_t)bf->bf_daddr,
	    le32toh(ds->physnext), le32toh(ds->physbuffdata),
	    ds->rxcontrol, 
	    ds->rxcontrol != MALO_RXD_CTRL_DRIVER_OWN ?
	        "" : (status & MALO_RXD_STATUS_OK) ? " *" : " !",
	    ds->status, le16toh(ds->pktlen), ds->snr, ds->nf, ds->channel,
	    ds->rate, le16toh(ds->qosctrl));
}

static void
malo_printtxbuf(const struct malo_txbuf *bf, u_int qnum, u_int ix)
{
	const struct malo_txdesc *ds = bf->bf_desc;
	uint32_t status = le32toh(ds->status);
	
	printf("Q%u[%3u]", qnum, ix);
	printf(" (DS.V:%p DS.P:0x%jx)\n", ds, (uintmax_t)bf->bf_daddr);
	printf("    NEXT:%08x DATA:%08x LEN:%04x STAT:%08x%s\n",
	    le32toh(ds->physnext),
	    le32toh(ds->pktptr), le16toh(ds->pktlen), status,
	    status & MALO_TXD_STATUS_USED ?
	    "" : (status & 3) != 0 ? " *" : " !");
	printf("    RATE:%02x PRI:%x QOS:%04x SAP:%08x FORMAT:%04x\n",
	    ds->datarate, ds->txpriority, le16toh(ds->qosctrl),
	    le32toh(ds->sap_pktinfo), le16toh(ds->format));
#if 0
	{
		const uint8_t *cp = (const uint8_t *) ds;
		int i;
		for (i = 0; i < sizeof(struct malo_txdesc); i++) {
			printf("%02x ", cp[i]);
			if (((i+1) % 16) == 0)
				printf("\n");
		}
		printf("\n");
	}
#endif
}
#endif /* MALO_DEBUG */

static __inline void
malo_updatetxrate(struct ieee80211_node *ni, int rix)
{
	static const int ieeerates[] =
	    { 2, 4, 11, 22, 44, 12, 18, 24, 36, 48, 96, 108 };
	if (rix < nitems(ieeerates))
		ni->ni_txrate = ieeerates[rix];
}

static int
malo_fix2rate(int fix_rate)
{
	static const int rates[] =
	    { 2, 4, 11, 22, 12, 18, 24, 36, 48, 96, 108 };
	return (fix_rate < nitems(rates) ? rates[fix_rate] : 0);
}

/* idiomatic shorthands: MS = mask+shift, SM = shift+mask */
#define	MS(v,x)			(((v) & x) >> x##_S)
#define	SM(v,x)			(((v) << x##_S) & x)

/*
 * Process completed xmit descriptors from the specified queue.
 */
static int
malo_tx_processq(struct malo_softc *sc, struct malo_txq *txq)
{
	struct malo_txbuf *bf;
	struct malo_txdesc *ds;
	struct ieee80211_node *ni;
	int nreaped;
	uint32_t status;

	DPRINTF(sc, MALO_DEBUG_TX_PROC, "%s: tx queue %u\n",
	    __func__, txq->qnum);
	for (nreaped = 0;; nreaped++) {
		MALO_TXQ_LOCK(txq);
		bf = STAILQ_FIRST(&txq->active);
		if (bf == NULL) {
			MALO_TXQ_UNLOCK(txq);
			break;
		}
		ds = bf->bf_desc;
		MALO_TXDESC_SYNC(txq, ds,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		if (ds->status & htole32(MALO_TXD_STATUS_FW_OWNED)) {
			MALO_TXQ_UNLOCK(txq);
			break;
		}
		STAILQ_REMOVE_HEAD(&txq->active, bf_list);
		MALO_TXQ_UNLOCK(txq);

#ifdef MALO_DEBUG
		if (sc->malo_debug & MALO_DEBUG_XMIT_DESC)
			malo_printtxbuf(bf, txq->qnum, nreaped);
#endif
		ni = bf->bf_node;
		if (ni != NULL) {
			status = le32toh(ds->status);
			if (status & MALO_TXD_STATUS_OK) {
				uint16_t format = le16toh(ds->format);
				uint8_t txant = MS(format, MALO_TXD_ANTENNA);

				sc->malo_stats.mst_ant_tx[txant]++;
				if (status & MALO_TXD_STATUS_OK_RETRY)
					sc->malo_stats.mst_tx_retries++;
				if (status & MALO_TXD_STATUS_OK_MORE_RETRY)
					sc->malo_stats.mst_tx_mretries++;
				malo_updatetxrate(ni, ds->datarate);
				sc->malo_stats.mst_tx_rate = ds->datarate;
			} else {
				if (status & MALO_TXD_STATUS_FAILED_LINK_ERROR)
					sc->malo_stats.mst_tx_linkerror++;
				if (status & MALO_TXD_STATUS_FAILED_XRETRY)
					sc->malo_stats.mst_tx_xretries++;
				if (status & MALO_TXD_STATUS_FAILED_AGING)
					sc->malo_stats.mst_tx_aging++;
			}
			/* XXX strip fw len in case header inspected */
			m_adj(bf->bf_m, sizeof(uint16_t));
			ieee80211_tx_complete(ni, bf->bf_m, 
			    (status & MALO_TXD_STATUS_OK) == 0);
		} else
			m_freem(bf->bf_m);

		ds->status = htole32(MALO_TXD_STATUS_IDLE);
		ds->pktlen = htole32(0);

		bus_dmamap_sync(sc->malo_dmat, bf->bf_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->malo_dmat, bf->bf_dmamap);
		bf->bf_m = NULL;
		bf->bf_node = NULL;

		MALO_TXQ_LOCK(txq);
		STAILQ_INSERT_TAIL(&txq->free, bf, bf_list);
		txq->nfree++;
		MALO_TXQ_UNLOCK(txq);
	}
	return nreaped;
}

/*
 * Deferred processing of transmit interrupt.
 */
static void
malo_tx_proc(void *arg, int npending)
{
	struct malo_softc *sc = arg;
	int i, nreaped;

	/*
	 * Process each active queue.
	 */
	nreaped = 0;
	MALO_LOCK(sc);
	for (i = 0; i < MALO_NUM_TX_QUEUES; i++) {
		if (!STAILQ_EMPTY(&sc->malo_txq[i].active))
			nreaped += malo_tx_processq(sc, &sc->malo_txq[i]);
	}

	if (nreaped != 0) {
		sc->malo_timer = 0;
		malo_start(sc);
	}
	MALO_UNLOCK(sc);
}

static int
malo_tx_start(struct malo_softc *sc, struct ieee80211_node *ni,
    struct malo_txbuf *bf, struct mbuf *m0)
{
#define	IS_DATA_FRAME(wh)						\
	((wh->i_fc[0] & (IEEE80211_FC0_TYPE_MASK)) == IEEE80211_FC0_TYPE_DATA)
	int error, ismcast, iswep;
	int copyhdrlen, hdrlen, pktlen;
	struct ieee80211_frame *wh;
	struct ieee80211com *ic = &sc->malo_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct malo_txdesc *ds;
	struct malo_txrec *tr;
	struct malo_txq *txq;
	uint16_t qos;

	wh = mtod(m0, struct ieee80211_frame *);
	iswep = wh->i_fc[1] & IEEE80211_FC1_PROTECTED;
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);
	copyhdrlen = hdrlen = ieee80211_anyhdrsize(wh);
	pktlen = m0->m_pkthdr.len;
	if (IEEE80211_QOS_HAS_SEQ(wh)) {
		qos = *(uint16_t *)ieee80211_getqos(wh);
		if (IEEE80211_IS_DSTODS(wh))
			copyhdrlen -= sizeof(qos);
	} else
		qos = 0;

	if (iswep) {
		struct ieee80211_key *k;

		/*
		 * Construct the 802.11 header+trailer for an encrypted
		 * frame. The only reason this can fail is because of an
		 * unknown or unsupported cipher/key type.
		 *
		 * NB: we do this even though the firmware will ignore
		 *     what we've done for WEP and TKIP as we need the
		 *     ExtIV filled in for CCMP and this also adjusts
		 *     the headers which simplifies our work below.
		 */
		k = ieee80211_crypto_encap(ni, m0);
		if (k == NULL) {
			/*
			 * This can happen when the key is yanked after the
			 * frame was queued.  Just discard the frame; the
			 * 802.11 layer counts failures and provides
			 * debugging/diagnostics.
			 */
			m_freem(m0);
			return EIO;
		}

		/*
		 * Adjust the packet length for the crypto additions
		 * done during encap and any other bits that the f/w
		 * will add later on.
		 */
		pktlen = m0->m_pkthdr.len;

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		sc->malo_tx_th.wt_flags = 0;	/* XXX */
		if (iswep)
			sc->malo_tx_th.wt_flags |= IEEE80211_RADIOTAP_F_WEP;
		sc->malo_tx_th.wt_txpower = ni->ni_txpower;
		sc->malo_tx_th.wt_antenna = sc->malo_txantenna;

		ieee80211_radiotap_tx(vap, m0);
	}

	/*
	 * Copy up/down the 802.11 header; the firmware requires
	 * we present a 2-byte payload length followed by a
	 * 4-address header (w/o QoS), followed (optionally) by
	 * any WEP/ExtIV header (but only filled in for CCMP).
	 * We are assured the mbuf has sufficient headroom to
	 * prepend in-place by the setup of ic_headroom in
	 * malo_attach.
	 */
	if (hdrlen < sizeof(struct malo_txrec)) {
		const int space = sizeof(struct malo_txrec) - hdrlen;
		if (M_LEADINGSPACE(m0) < space) {
			/* NB: should never happen */
			device_printf(sc->malo_dev,
			    "not enough headroom, need %d found %zd, "
			    "m_flags 0x%x m_len %d\n",
			    space, M_LEADINGSPACE(m0), m0->m_flags, m0->m_len);
			ieee80211_dump_pkt(ic,
			    mtod(m0, const uint8_t *), m0->m_len, 0, -1);
			m_freem(m0);
			/* XXX stat */
			return EIO;
		}
		M_PREPEND(m0, space, M_NOWAIT);
	}
	tr = mtod(m0, struct malo_txrec *);
	if (wh != (struct ieee80211_frame *) &tr->wh)
		ovbcopy(wh, &tr->wh, hdrlen);
	/*
	 * Note: the "firmware length" is actually the length of the fully
	 * formed "802.11 payload".  That is, it's everything except for
	 * the 802.11 header.  In particular this includes all crypto
	 * material including the MIC!
	 */
	tr->fwlen = htole16(pktlen - hdrlen);

	/*
	 * Load the DMA map so any coalescing is done.  This
	 * also calculates the number of descriptors we need.
	 */
	error = malo_tx_dmasetup(sc, bf, m0);
	if (error != 0)
		return error;
	bf->bf_node = ni;			/* NB: held reference */
	m0 = bf->bf_m;				/* NB: may have changed */
	tr = mtod(m0, struct malo_txrec *);
	wh = (struct ieee80211_frame *)&tr->wh;

	/*
	 * Formulate tx descriptor.
	 */
	ds = bf->bf_desc;
	txq = bf->bf_txq;

	ds->qosctrl = qos;			/* NB: already little-endian */
	ds->pktptr = htole32(bf->bf_segs[0].ds_addr);
	ds->pktlen = htole16(bf->bf_segs[0].ds_len);
	/* NB: pPhysNext setup once, don't touch */
	ds->datarate = IS_DATA_FRAME(wh) ? 1 : 0;
	ds->sap_pktinfo = 0;
	ds->format = 0;

	/*
	 * Select transmit rate.
	 */
	switch (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) {
	case IEEE80211_FC0_TYPE_MGT:
		sc->malo_stats.mst_tx_mgmt++;
		/* fall thru... */
	case IEEE80211_FC0_TYPE_CTL:
		ds->txpriority = 1;
		break;
	case IEEE80211_FC0_TYPE_DATA:
		ds->txpriority = txq->qnum;
		break;
	default:
		device_printf(sc->malo_dev, "bogus frame type 0x%x (%s)\n",
			wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK, __func__);
		/* XXX statistic */
		m_freem(m0);
		return EIO;
	}

#ifdef MALO_DEBUG
	if (IFF_DUMPPKTS_XMIT(sc))
		ieee80211_dump_pkt(ic,
		    mtod(m0, const uint8_t *)+sizeof(uint16_t),
		    m0->m_len - sizeof(uint16_t), ds->datarate, -1);
#endif

	MALO_TXQ_LOCK(txq);
	if (!IS_DATA_FRAME(wh))
		ds->status |= htole32(1);
	ds->status |= htole32(MALO_TXD_STATUS_FW_OWNED);
	STAILQ_INSERT_TAIL(&txq->active, bf, bf_list);
	MALO_TXDESC_SYNC(txq, ds, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	sc->malo_timer = 5;
	MALO_TXQ_UNLOCK(txq);
	return 0;
}

static int
malo_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct malo_softc *sc = ic->ic_softc;
	int error;

	MALO_LOCK(sc);
	if (!sc->malo_running) {
		MALO_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->malo_snd, m);
	if (error) {
		MALO_UNLOCK(sc);
		return (error);
	}
	malo_start(sc);
	MALO_UNLOCK(sc);
	return (0);
}

static void
malo_start(struct malo_softc *sc)
{
	struct ieee80211_node *ni;
	struct malo_txq *txq = &sc->malo_txq[0];
	struct malo_txbuf *bf = NULL;
	struct mbuf *m;
	int nqueued = 0;

	MALO_LOCK_ASSERT(sc);

	if (!sc->malo_running || sc->malo_invalid)
		return;

	while ((m = mbufq_dequeue(&sc->malo_snd)) != NULL) {
		ni = (struct ieee80211_node *) m->m_pkthdr.rcvif;
		bf = malo_getbuf(sc, txq);
		if (bf == NULL) {
			mbufq_prepend(&sc->malo_snd, m);
			sc->malo_stats.mst_tx_qstop++;
			break;
		}
		/*
		 * Pass the frame to the h/w for transmission.
		 */
		if (malo_tx_start(sc, ni, bf, m)) {
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			if (bf != NULL) {
				bf->bf_m = NULL;
				bf->bf_node = NULL;
				MALO_TXQ_LOCK(txq);
				STAILQ_INSERT_HEAD(&txq->free, bf, bf_list);
				MALO_TXQ_UNLOCK(txq);
			}
			ieee80211_free_node(ni);
			continue;
		}
		nqueued++;

		if (nqueued >= malo_txcoalesce) {
			/*
			 * Poke the firmware to process queued frames;
			 * see below about (lack of) locking.
			 */
			nqueued = 0;
			malo_hal_txstart(sc->malo_mh, 0/*XXX*/);
		}
	}

	if (nqueued) {
		/*
		 * NB: We don't need to lock against tx done because
		 * this just prods the firmware to check the transmit
		 * descriptors.  The firmware will also start fetching
		 * descriptors by itself if it notices new ones are
		 * present when it goes to deliver a tx done interrupt
		 * to the host. So if we race with tx done processing
		 * it's ok.  Delivering the kick here rather than in
		 * malo_tx_start is an optimization to avoid poking the
		 * firmware for each packet.
		 *
		 * NB: the queue id isn't used so 0 is ok.
		 */
		malo_hal_txstart(sc->malo_mh, 0/*XXX*/);
	}
}

static void
malo_watchdog(void *arg)
{
	struct malo_softc *sc = arg;

	callout_reset(&sc->malo_watchdog_timer, hz, malo_watchdog, sc);
	if (sc->malo_timer == 0 || --sc->malo_timer > 0)
		return;

	if (sc->malo_running && !sc->malo_invalid) {
		device_printf(sc->malo_dev, "watchdog timeout\n");

		/* XXX no way to reset h/w. now  */

		counter_u64_add(sc->malo_ic.ic_oerrors, 1);
		sc->malo_stats.mst_watchdog++;
	}
}

static int
malo_hal_reset(struct malo_softc *sc)
{
	static int first = 0;
	struct ieee80211com *ic = &sc->malo_ic;
	struct malo_hal *mh = sc->malo_mh;

	if (first == 0) {
		/*
		 * NB: when the device firstly is initialized, sometimes
		 * firmware could override rx/tx dma registers so we re-set
		 * these values once.
		 */
		malo_hal_set_rxtxdma(sc);
		first = 1;
	}

	malo_hal_setantenna(mh, MHA_ANTENNATYPE_RX, sc->malo_rxantenna);
	malo_hal_setantenna(mh, MHA_ANTENNATYPE_TX, sc->malo_txantenna);
	malo_hal_setradio(mh, 1, MHP_AUTO_PREAMBLE);
	malo_chan_set(sc, ic->ic_curchan);

	/* XXX needs other stuffs?  */

	return 1;
}

static __inline struct mbuf *
malo_getrxmbuf(struct malo_softc *sc, struct malo_rxbuf *bf)
{
	struct mbuf *m;
	bus_addr_t paddr;
	int error;

	/* XXX don't need mbuf, just dma buffer */
	m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUMPAGESIZE);
	if (m == NULL) {
		sc->malo_stats.mst_rx_nombuf++;	/* XXX */
		return NULL;
	}
	error = bus_dmamap_load(sc->malo_dmat, bf->bf_dmamap,
	    mtod(m, caddr_t), MJUMPAGESIZE,
	    malo_load_cb, &paddr, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->malo_dev,
		    "%s: bus_dmamap_load failed, error %d\n", __func__, error);
		m_freem(m);
		return NULL;
	}
	bf->bf_data = paddr;
	bus_dmamap_sync(sc->malo_dmat, bf->bf_dmamap, BUS_DMASYNC_PREWRITE);

	return m;
}

static int
malo_rxbuf_init(struct malo_softc *sc, struct malo_rxbuf *bf)
{
	struct malo_rxdesc *ds;

	ds = bf->bf_desc;
	if (bf->bf_m == NULL) {
		bf->bf_m = malo_getrxmbuf(sc, bf);
		if (bf->bf_m == NULL) {
			/* mark descriptor to be skipped */
			ds->rxcontrol = MALO_RXD_CTRL_OS_OWN;
			/* NB: don't need PREREAD */
			MALO_RXDESC_SYNC(sc, ds, BUS_DMASYNC_PREWRITE);
			return ENOMEM;
		}
	}

	/*
	 * Setup descriptor.
	 */
	ds->qosctrl = 0;
	ds->snr = 0;
	ds->status = MALO_RXD_STATUS_IDLE;
	ds->channel = 0;
	ds->pktlen = htole16(MALO_RXSIZE);
	ds->nf = 0;
	ds->physbuffdata = htole32(bf->bf_data);
	/* NB: don't touch pPhysNext, set once */
	ds->rxcontrol = MALO_RXD_CTRL_DRIVER_OWN;
	MALO_RXDESC_SYNC(sc, ds, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return 0;
}

/*
 * Setup the rx data structures.  This should only be done once or we may get
 * out of sync with the firmware.
 */
static int
malo_startrecv(struct malo_softc *sc)
{
	struct malo_rxbuf *bf, *prev;
	struct malo_rxdesc *ds;
	
	if (sc->malo_recvsetup == 1) {
		malo_mode_init(sc);		/* set filters, etc. */
		return 0;
	}
	
	prev = NULL;
	STAILQ_FOREACH(bf, &sc->malo_rxbuf, bf_list) {
		int error = malo_rxbuf_init(sc, bf);
		if (error != 0) {
			DPRINTF(sc, MALO_DEBUG_RECV,
			    "%s: malo_rxbuf_init failed %d\n",
			    __func__, error);
			return error;
		}
		if (prev != NULL) {
			ds = prev->bf_desc;
			ds->physnext = htole32(bf->bf_daddr);
		}
		prev = bf;
	}
	if (prev != NULL) {
		ds = prev->bf_desc;
		ds->physnext =
		    htole32(STAILQ_FIRST(&sc->malo_rxbuf)->bf_daddr);
	}

	sc->malo_recvsetup = 1;

	malo_mode_init(sc);		/* set filters, etc. */
	
	return 0;
}

static void
malo_init_locked(struct malo_softc *sc)
{
	struct malo_hal *mh = sc->malo_mh;
	int error;
	
	MALO_LOCK_ASSERT(sc);
	
	/*
	 * Stop anything previously setup.  This is safe whether this is
	 * the first time through or not.
	 */
	malo_stop(sc);

	/*
	 * Push state to the firmware.
	 */
	if (!malo_hal_reset(sc)) {
		device_printf(sc->malo_dev,
		    "%s: unable to reset hardware\n", __func__);
		return;
	}

	/*
	 * Setup recv (once); transmit is already good to go.
	 */
	error = malo_startrecv(sc);
	if (error != 0) {
		device_printf(sc->malo_dev,
		    "%s: unable to start recv logic, error %d\n",
		    __func__, error);
		return;
	}

	/*
	 * Enable interrupts.
	 */
	sc->malo_imask = MALO_A2HRIC_BIT_RX_RDY
	    | MALO_A2HRIC_BIT_TX_DONE
	    | MALO_A2HRIC_BIT_OPC_DONE
	    | MALO_A2HRIC_BIT_MAC_EVENT
	    | MALO_A2HRIC_BIT_RX_PROBLEM
	    | MALO_A2HRIC_BIT_ICV_ERROR
	    | MALO_A2HRIC_BIT_RADAR_DETECT
	    | MALO_A2HRIC_BIT_CHAN_SWITCH;

	sc->malo_running = 1;
	malo_hal_intrset(mh, sc->malo_imask);
	callout_reset(&sc->malo_watchdog_timer, hz, malo_watchdog, sc);
}

static void
malo_init(void *arg)
{
	struct malo_softc *sc = (struct malo_softc *) arg;
	struct ieee80211com *ic = &sc->malo_ic;
	
	MALO_LOCK(sc);
	malo_init_locked(sc);
	MALO_UNLOCK(sc);

	if (sc->malo_running)
		ieee80211_start_all(ic);	/* start all vap's */
}

/*
 * Set the multicast filter contents into the hardware.
 */
static void
malo_setmcastfilter(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->malo_ic;
	struct ieee80211vap *vap;
	uint8_t macs[IEEE80211_ADDR_LEN * MALO_HAL_MCAST_MAX];
	uint8_t *mp;
	int nmc;

	mp = macs;
	nmc = 0;

	if (ic->ic_opmode == IEEE80211_M_MONITOR || ic->ic_allmulti > 0 ||
	    ic->ic_promisc > 0)
		goto all;

	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
		struct ifnet *ifp;
		struct ifmultiaddr *ifma;

		ifp = vap->iv_ifp;
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;

			if (nmc == MALO_HAL_MCAST_MAX) {
				ifp->if_flags |= IFF_ALLMULTI;
				if_maddr_runlock(ifp);
				goto all;
			}
			IEEE80211_ADDR_COPY(mp,
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));

			mp += IEEE80211_ADDR_LEN, nmc++;
		}
		if_maddr_runlock(ifp);
	}

	malo_hal_setmcast(sc->malo_mh, nmc, macs);

all:
	/*
	 * XXX we don't know how to set the f/w for supporting
	 * IFF_ALLMULTI | IFF_PROMISC cases
	 */
	return;
}

static int
malo_mode_init(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->malo_ic;
	struct malo_hal *mh = sc->malo_mh;

	malo_hal_setpromisc(mh, ic->ic_promisc > 0);
	malo_setmcastfilter(sc);

	return ENXIO;
}

static void
malo_tx_draintxq(struct malo_softc *sc, struct malo_txq *txq)
{
	struct ieee80211_node *ni;
	struct malo_txbuf *bf;
	u_int ix;
	
	/*
	 * NB: this assumes output has been stopped and
	 *     we do not need to block malo_tx_tasklet
	 */
	for (ix = 0;; ix++) {
		MALO_TXQ_LOCK(txq);
		bf = STAILQ_FIRST(&txq->active);
		if (bf == NULL) {
			MALO_TXQ_UNLOCK(txq);
			break;
		}
		STAILQ_REMOVE_HEAD(&txq->active, bf_list);
		MALO_TXQ_UNLOCK(txq);
#ifdef MALO_DEBUG
		if (sc->malo_debug & MALO_DEBUG_RESET) {
			struct ieee80211com *ic = &sc->malo_ic;
			const struct malo_txrec *tr =
			    mtod(bf->bf_m, const struct malo_txrec *);
			malo_printtxbuf(bf, txq->qnum, ix);
			ieee80211_dump_pkt(ic, (const uint8_t *)&tr->wh,
			    bf->bf_m->m_len - sizeof(tr->fwlen), 0, -1);
		}
#endif /* MALO_DEBUG */
		bus_dmamap_unload(sc->malo_dmat, bf->bf_dmamap);
		ni = bf->bf_node;
		bf->bf_node = NULL;
		if (ni != NULL) {
			/*
			 * Reclaim node reference.
			 */
			ieee80211_free_node(ni);
		}
		m_freem(bf->bf_m);
		bf->bf_m = NULL;
		
		MALO_TXQ_LOCK(txq);
		STAILQ_INSERT_TAIL(&txq->free, bf, bf_list);
		txq->nfree++;
		MALO_TXQ_UNLOCK(txq);
	}
}

static void
malo_stop(struct malo_softc *sc)
{
	struct malo_hal *mh = sc->malo_mh;
	int i;

	DPRINTF(sc, MALO_DEBUG_ANY, "%s: invalid %u running %u\n",
	    __func__, sc->malo_invalid, sc->malo_running);

	MALO_LOCK_ASSERT(sc);

	if (!sc->malo_running)
		return;

	/*
	 * Shutdown the hardware and driver:
	 *    disable interrupts
	 *    turn off the radio
	 *    drain and release tx queues
	 *
	 * Note that some of this work is not possible if the hardware
	 * is gone (invalid).
	 */
	sc->malo_running = 0;
	callout_stop(&sc->malo_watchdog_timer);
	sc->malo_timer = 0;
	/* disable interrupt.  */
	malo_hal_intrset(mh, 0);
	/* turn off the radio.  */
	malo_hal_setradio(mh, 0, MHP_AUTO_PREAMBLE);

	/* drain and release tx queues.  */
	for (i = 0; i < MALO_NUM_TX_QUEUES; i++)
		malo_tx_draintxq(sc, &sc->malo_txq[i]);
}

static void
malo_parent(struct ieee80211com *ic)
{
	struct malo_softc *sc = ic->ic_softc;
	int startall = 0;

	MALO_LOCK(sc);
	if (ic->ic_nrunning > 0) {
		/*
		 * Beware of being called during attach/detach
		 * to reset promiscuous mode.  In that case we
		 * will still be marked UP but not RUNNING.
		 * However trying to re-init the interface
		 * is the wrong thing to do as we've already
		 * torn down much of our state.  There's
		 * probably a better way to deal with this.
		 */
		if (!sc->malo_running && !sc->malo_invalid) {
			malo_init(sc);
			startall = 1;
		}
		/*
		 * To avoid rescanning another access point,
		 * do not call malo_init() here.  Instead,
		 * only reflect promisc mode settings.
		 */
		malo_mode_init(sc);
	} else if (sc->malo_running)
		malo_stop(sc);
	MALO_UNLOCK(sc);
	if (startall)
		ieee80211_start_all(ic);
}

/*
 * Callback from the 802.11 layer to update the slot time
 * based on the current setting.  We use it to notify the
 * firmware of ERP changes and the f/w takes care of things
 * like slot time and preamble.
 */
static void
malo_updateslot(struct ieee80211com *ic)
{
	struct malo_softc *sc = ic->ic_softc;
	struct malo_hal *mh = sc->malo_mh;
	int error;
	
	/* NB: can be called early; suppress needless cmds */
	if (!sc->malo_running)
		return;

	DPRINTF(sc, MALO_DEBUG_RESET,
	    "%s: chan %u MHz/flags 0x%x %s slot, (ic_flags 0x%x)\n",
	    __func__, ic->ic_curchan->ic_freq, ic->ic_curchan->ic_flags,
	    ic->ic_flags & IEEE80211_F_SHSLOT ? "short" : "long", ic->ic_flags);

	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		error = malo_hal_set_slot(mh, 1);
	else
		error = malo_hal_set_slot(mh, 0);

	if (error != 0)
		device_printf(sc->malo_dev, "setting %s slot failed\n",
			ic->ic_flags & IEEE80211_F_SHSLOT ? "short" : "long");
}

static int
malo_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct malo_softc *sc = ic->ic_softc;
	struct malo_hal *mh = sc->malo_mh;
	int error;

	DPRINTF(sc, MALO_DEBUG_STATE, "%s: %s -> %s\n", __func__,
	    ieee80211_state_name[vap->iv_state],
	    ieee80211_state_name[nstate]);

	/*
	 * Invoke the net80211 layer first so iv_bss is setup.
	 */
	error = MALO_VAP(vap)->malo_newstate(vap, nstate, arg);
	if (error != 0)
		return error;

	if (nstate == IEEE80211_S_RUN && vap->iv_state != IEEE80211_S_RUN) {
		struct ieee80211_node *ni = vap->iv_bss;
		enum ieee80211_phymode mode = ieee80211_chan2mode(ni->ni_chan);
		const struct ieee80211_txparam *tp = &vap->iv_txparms[mode];

		DPRINTF(sc, MALO_DEBUG_STATE,
		    "%s: %s(RUN): iv_flags 0x%08x bintvl %d bssid %s "
		    "capinfo 0x%04x chan %d associd 0x%x mode %d rate %d\n",
		    vap->iv_ifp->if_xname, __func__, vap->iv_flags,
		    ni->ni_intval, ether_sprintf(ni->ni_bssid), ni->ni_capinfo,
		    ieee80211_chan2ieee(ic, ic->ic_curchan),
		    ni->ni_associd, mode, tp->ucastrate);

		malo_hal_setradio(mh, 1,
		    (ic->ic_flags & IEEE80211_F_SHPREAMBLE) ?
			MHP_SHORT_PREAMBLE : MHP_LONG_PREAMBLE);
		malo_hal_setassocid(sc->malo_mh, ni->ni_bssid, ni->ni_associd);
		malo_hal_set_rate(mh, mode, 
		   tp->ucastrate == IEEE80211_FIXED_RATE_NONE ?
		       0 : malo_fix2rate(tp->ucastrate));
	}
	return 0;
}

static int
malo_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
	const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct malo_softc *sc = ic->ic_softc;
	struct malo_txbuf *bf;
	struct malo_txq *txq;

	if (!sc->malo_running || sc->malo_invalid) {
		m_freem(m);
		return ENETDOWN;
	}

	/*
	 * Grab a TX buffer and associated resources.  Note that we depend
	 * on the classification by the 802.11 layer to get to the right h/w
	 * queue.  Management frames must ALWAYS go on queue 1 but we
	 * cannot just force that here because we may receive non-mgt frames.
	 */
	txq = &sc->malo_txq[0];
	bf = malo_getbuf(sc, txq);
	if (bf == NULL) {
		m_freem(m);
		return ENOBUFS;
	}

	/*
	 * Pass the frame to the h/w for transmission.
	 */
	if (malo_tx_start(sc, ni, bf, m) != 0) {
		bf->bf_m = NULL;
		bf->bf_node = NULL;
		MALO_TXQ_LOCK(txq);
		STAILQ_INSERT_HEAD(&txq->free, bf, bf_list);
		txq->nfree++;
		MALO_TXQ_UNLOCK(txq);

		return EIO;		/* XXX */
	}

	/*
	 * NB: We don't need to lock against tx done because this just
	 * prods the firmware to check the transmit descriptors.  The firmware
	 * will also start fetching descriptors by itself if it notices
	 * new ones are present when it goes to deliver a tx done interrupt
	 * to the host. So if we race with tx done processing it's ok.
	 * Delivering the kick here rather than in malo_tx_start is
	 * an optimization to avoid poking the firmware for each packet.
	 *
	 * NB: the queue id isn't used so 0 is ok.
	 */
	malo_hal_txstart(sc->malo_mh, 0/*XXX*/);

	return 0;
}

static void
malo_sysctlattach(struct malo_softc *sc)
{
#ifdef	MALO_DEBUG
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->malo_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->malo_dev);

	sc->malo_debug = malo_debug;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"debug", CTLFLAG_RW, &sc->malo_debug, 0,
		"control debugging printfs");
#endif
}

static void
malo_announce(struct malo_softc *sc)
{

	device_printf(sc->malo_dev,
		"versions [hw %d fw %d.%d.%d.%d] (regioncode %d)\n",
		sc->malo_hwspecs.hwversion,
		(sc->malo_hwspecs.fw_releasenum >> 24) & 0xff,
		(sc->malo_hwspecs.fw_releasenum >> 16) & 0xff,
		(sc->malo_hwspecs.fw_releasenum >> 8) & 0xff,
		(sc->malo_hwspecs.fw_releasenum >> 0) & 0xff,
		sc->malo_hwspecs.regioncode);

	if (bootverbose || malo_rxbuf != MALO_RXBUF)
		device_printf(sc->malo_dev,
		    "using %u rx buffers\n", malo_rxbuf);
	if (bootverbose || malo_txbuf != MALO_TXBUF)
		device_printf(sc->malo_dev,
		    "using %u tx buffers\n", malo_txbuf);
}

/*
 * Convert net80211 channel to a HAL channel.
 */
static void
malo_mapchan(struct malo_hal_channel *hc, const struct ieee80211_channel *chan)
{
	hc->channel = chan->ic_ieee;

	*(uint32_t *)&hc->flags = 0;
	if (IEEE80211_IS_CHAN_2GHZ(chan))
		hc->flags.freqband = MALO_FREQ_BAND_2DOT4GHZ;
}

/*
 * Set/change channels.  If the channel is really being changed,
 * it's done by reseting the chip.  To accomplish this we must
 * first cleanup any pending DMA, then restart stuff after a la
 * malo_init.
 */
static int
malo_chan_set(struct malo_softc *sc, struct ieee80211_channel *chan)
{
	struct malo_hal *mh = sc->malo_mh;
	struct malo_hal_channel hchan;

	DPRINTF(sc, MALO_DEBUG_RESET, "%s: chan %u MHz/flags 0x%x\n",
	    __func__, chan->ic_freq, chan->ic_flags);

	/*
	 * Convert to a HAL channel description with the flags constrained
	 * to reflect the current operating mode.
	 */
	malo_mapchan(&hchan, chan);
	malo_hal_intrset(mh, 0);		/* disable interrupts */
	malo_hal_setchannel(mh, &hchan);
	malo_hal_settxpower(mh, &hchan);

	/*
	 * Update internal state.
	 */
	sc->malo_tx_th.wt_chan_freq = htole16(chan->ic_freq);
	sc->malo_rx_th.wr_chan_freq = htole16(chan->ic_freq);
	if (IEEE80211_IS_CHAN_ANYG(chan)) {
		sc->malo_tx_th.wt_chan_flags = htole16(IEEE80211_CHAN_G);
		sc->malo_rx_th.wr_chan_flags = htole16(IEEE80211_CHAN_G);
	} else {
		sc->malo_tx_th.wt_chan_flags = htole16(IEEE80211_CHAN_B);
		sc->malo_rx_th.wr_chan_flags = htole16(IEEE80211_CHAN_B);
	}
	sc->malo_curchan = hchan;
	malo_hal_intrset(mh, sc->malo_imask);

	return 0;
}

static void
malo_scan_start(struct ieee80211com *ic)
{
	struct malo_softc *sc = ic->ic_softc;

	DPRINTF(sc, MALO_DEBUG_STATE, "%s\n", __func__);
}

static void
malo_scan_end(struct ieee80211com *ic)
{
	struct malo_softc *sc = ic->ic_softc;

	DPRINTF(sc, MALO_DEBUG_STATE, "%s\n", __func__);
}

static void
malo_set_channel(struct ieee80211com *ic)
{
	struct malo_softc *sc = ic->ic_softc;

	(void) malo_chan_set(sc, ic->ic_curchan);
}

static void
malo_rx_proc(void *arg, int npending)
{
	struct malo_softc *sc = arg;
	struct ieee80211com *ic = &sc->malo_ic;
	struct malo_rxbuf *bf;
	struct malo_rxdesc *ds;
	struct mbuf *m, *mnew;
	struct ieee80211_qosframe *wh;
	struct ieee80211_node *ni;
	int off, len, hdrlen, pktlen, rssi, ntodo;
	uint8_t *data, status;
	uint32_t readptr, writeptr;

	DPRINTF(sc, MALO_DEBUG_RX_PROC,
	    "%s: pending %u rdptr(0x%x) 0x%x wrptr(0x%x) 0x%x\n",
	    __func__, npending,
	    sc->malo_hwspecs.rxdesc_read,
	    malo_bar0_read4(sc, sc->malo_hwspecs.rxdesc_read),
	    sc->malo_hwspecs.rxdesc_write,
	    malo_bar0_read4(sc, sc->malo_hwspecs.rxdesc_write));

	readptr = malo_bar0_read4(sc, sc->malo_hwspecs.rxdesc_read);
	writeptr = malo_bar0_read4(sc, sc->malo_hwspecs.rxdesc_write);
	if (readptr == writeptr)
		return;

	bf = sc->malo_rxnext;
	for (ntodo = malo_rxquota; ntodo > 0 && readptr != writeptr; ntodo--) {
		if (bf == NULL) {
			bf = STAILQ_FIRST(&sc->malo_rxbuf);
			break;
		}
		ds = bf->bf_desc;
		if (bf->bf_m == NULL) {
			/*
			 * If data allocation failed previously there
			 * will be no buffer; try again to re-populate it.
			 * Note the firmware will not advance to the next
			 * descriptor with a dma buffer so we must mimic
			 * this or we'll get out of sync.
			 */ 
			DPRINTF(sc, MALO_DEBUG_ANY,
			    "%s: rx buf w/o dma memory\n", __func__);
			(void)malo_rxbuf_init(sc, bf);
			break;
		}
		MALO_RXDESC_SYNC(sc, ds,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		if (ds->rxcontrol != MALO_RXD_CTRL_DMA_OWN)
			break;

		readptr = le32toh(ds->physnext);

#ifdef MALO_DEBUG
		if (sc->malo_debug & MALO_DEBUG_RECV_DESC)
			malo_printrxbuf(bf, 0);
#endif
		status = ds->status;
		if (status & MALO_RXD_STATUS_DECRYPT_ERR_MASK) {
			counter_u64_add(ic->ic_ierrors, 1);
			goto rx_next;
		}
		/*
		 * Sync the data buffer.
		 */
		len = le16toh(ds->pktlen);
		bus_dmamap_sync(sc->malo_dmat, bf->bf_dmamap,
		    BUS_DMASYNC_POSTREAD);
		/*
		 * The 802.11 header is provided all or in part at the front;
		 * use it to calculate the true size of the header that we'll
		 * construct below.  We use this to figure out where to copy
		 * payload prior to constructing the header.
		 */
		m = bf->bf_m;
		data = mtod(m, uint8_t *);
		hdrlen = ieee80211_anyhdrsize(data + sizeof(uint16_t));
		off = sizeof(uint16_t) + sizeof(struct ieee80211_frame_addr4);

		/*
		 * Calculate RSSI. XXX wrong
		 */
		rssi = 2 * ((int) ds->snr - ds->nf);	/* NB: .5 dBm  */
		if (rssi > 100)
			rssi = 100;

		pktlen = hdrlen + (len - off);
		/*
		 * NB: we know our frame is at least as large as
		 * IEEE80211_MIN_LEN because there is a 4-address frame at
		 * the front.  Hence there's no need to vet the packet length.
		 * If the frame in fact is too small it should be discarded
		 * at the net80211 layer.
		 */

		/* XXX don't need mbuf, just dma buffer */
		mnew = malo_getrxmbuf(sc, bf);
		if (mnew == NULL) {
			counter_u64_add(ic->ic_ierrors, 1);
			goto rx_next;
		}
		/*
		 * Attach the dma buffer to the mbuf; malo_rxbuf_init will
		 * re-setup the rx descriptor using the replacement dma
		 * buffer we just installed above.
		 */
		bf->bf_m = mnew;
		m->m_data += off - hdrlen;
		m->m_pkthdr.len = m->m_len = pktlen;

		/*
		 * Piece 802.11 header together.
		 */
		wh = mtod(m, struct ieee80211_qosframe *);
		/* NB: don't need to do this sometimes but ... */
		/* XXX special case so we can memcpy after m_devget? */
		ovbcopy(data + sizeof(uint16_t), wh, hdrlen);
		if (IEEE80211_QOS_HAS_SEQ(wh))
			*(uint16_t *)ieee80211_getqos(wh) = ds->qosctrl;
		if (ieee80211_radiotap_active(ic)) {
			sc->malo_rx_th.wr_flags = 0;
			sc->malo_rx_th.wr_rate = ds->rate;
			sc->malo_rx_th.wr_antsignal = rssi;
			sc->malo_rx_th.wr_antnoise = ds->nf;
		}
#ifdef MALO_DEBUG
		if (IFF_DUMPPKTS_RECV(sc, wh)) {
			ieee80211_dump_pkt(ic, mtod(m, caddr_t),
			    len, ds->rate, rssi);
		}
#endif
		/* dispatch */
		ni = ieee80211_find_rxnode(ic,
		    (struct ieee80211_frame_min *)wh);
		if (ni != NULL) {
			(void) ieee80211_input(ni, m, rssi, ds->nf);
			ieee80211_free_node(ni);
		} else
			(void) ieee80211_input_all(ic, m, rssi, ds->nf);
rx_next:
		/* NB: ignore ENOMEM so we process more descriptors */
		(void) malo_rxbuf_init(sc, bf);
		bf = STAILQ_NEXT(bf, bf_list);
	}
	
	malo_bar0_write4(sc, sc->malo_hwspecs.rxdesc_read, readptr);
	sc->malo_rxnext = bf;

	if (mbufq_first(&sc->malo_snd) != NULL)
		malo_start(sc);
}

/*
 * Reclaim all tx queue resources.
 */
static void
malo_tx_cleanup(struct malo_softc *sc)
{
	int i;

	for (i = 0; i < MALO_NUM_TX_QUEUES; i++)
		malo_tx_cleanupq(sc, &sc->malo_txq[i]);
}

int
malo_detach(struct malo_softc *sc)
{
	struct ieee80211com *ic = &sc->malo_ic;

	malo_stop(sc);

	if (sc->malo_tq != NULL) {
		taskqueue_drain(sc->malo_tq, &sc->malo_rxtask);
		taskqueue_drain(sc->malo_tq, &sc->malo_txtask);
		taskqueue_free(sc->malo_tq);
		sc->malo_tq = NULL;
	}

	/*
	 * NB: the order of these is important:
	 * o call the 802.11 layer before detaching the hal to
	 *   insure callbacks into the driver to delete global
	 *   key cache entries can be handled
	 * o reclaim the tx queue data structures after calling
	 *   the 802.11 layer as we'll get called back to reclaim
	 *   node state and potentially want to use them
	 * o to cleanup the tx queues the hal is called, so detach
	 *   it last
	 * Other than that, it's straightforward...
	 */
	ieee80211_ifdetach(ic);
	callout_drain(&sc->malo_watchdog_timer);
	malo_dma_cleanup(sc);
	malo_tx_cleanup(sc);
	malo_hal_detach(sc->malo_mh);
	mbufq_drain(&sc->malo_snd);
	MALO_LOCK_DESTROY(sc);

	return 0;
}

void
malo_shutdown(struct malo_softc *sc)
{

	malo_stop(sc);
}

void
malo_suspend(struct malo_softc *sc)
{

	malo_stop(sc);
}

void
malo_resume(struct malo_softc *sc)
{

	if (sc->malo_ic.ic_nrunning > 0)
		malo_init(sc);
}
