/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2008 Marvell Semiconductor, Inc.
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
__FBSDID("$FreeBSD$");

/*
 * Driver for the Marvell 88W8363 Wireless LAN controller.
 */

#include "opt_inet.h"
#include "opt_mwl.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/sysctl.h>
#include <sys/mbuf.h>   
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/callout.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
 
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_llc.h>

#include <net/bpf.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_input.h>
#include <net80211/ieee80211_regdomain.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif /* INET */

#include <dev/mwl/if_mwlvar.h>
#include <dev/mwl/mwldiag.h>

/* idiomatic shorthands: MS = mask+shift, SM = shift+mask */
#define	MS(v,x)	(((v) & x) >> x##_S)
#define	SM(v,x)	(((v) << x##_S) & x)

static struct ieee80211vap *mwl_vap_create(struct ieee80211com *,
		    const char [IFNAMSIZ], int, enum ieee80211_opmode, int,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    const uint8_t [IEEE80211_ADDR_LEN]);
static void	mwl_vap_delete(struct ieee80211vap *);
static int	mwl_setupdma(struct mwl_softc *);
static int	mwl_hal_reset(struct mwl_softc *sc);
static int	mwl_init(struct mwl_softc *);
static void	mwl_parent(struct ieee80211com *);
static int	mwl_reset(struct ieee80211vap *, u_long);
static void	mwl_stop(struct mwl_softc *);
static void	mwl_start(struct mwl_softc *);
static int	mwl_transmit(struct ieee80211com *, struct mbuf *);
static int	mwl_raw_xmit(struct ieee80211_node *, struct mbuf *,
			const struct ieee80211_bpf_params *);
static int	mwl_media_change(struct ifnet *);
static void	mwl_watchdog(void *);
static int	mwl_ioctl(struct ieee80211com *, u_long, void *);
static void	mwl_radar_proc(void *, int);
static void	mwl_chanswitch_proc(void *, int);
static void	mwl_bawatchdog_proc(void *, int);
static int	mwl_key_alloc(struct ieee80211vap *,
			struct ieee80211_key *,
			ieee80211_keyix *, ieee80211_keyix *);
static int	mwl_key_delete(struct ieee80211vap *,
			const struct ieee80211_key *);
static int	mwl_key_set(struct ieee80211vap *,
			const struct ieee80211_key *);
static int	_mwl_key_set(struct ieee80211vap *,
			const struct ieee80211_key *,
			const uint8_t mac[IEEE80211_ADDR_LEN]);
static int	mwl_mode_init(struct mwl_softc *);
static void	mwl_update_mcast(struct ieee80211com *);
static void	mwl_update_promisc(struct ieee80211com *);
static void	mwl_updateslot(struct ieee80211com *);
static int	mwl_beacon_setup(struct ieee80211vap *);
static void	mwl_beacon_update(struct ieee80211vap *, int);
#ifdef MWL_HOST_PS_SUPPORT
static void	mwl_update_ps(struct ieee80211vap *, int);
static int	mwl_set_tim(struct ieee80211_node *, int);
#endif
static int	mwl_dma_setup(struct mwl_softc *);
static void	mwl_dma_cleanup(struct mwl_softc *);
static struct ieee80211_node *mwl_node_alloc(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN]);
static void	mwl_node_cleanup(struct ieee80211_node *);
static void	mwl_node_drain(struct ieee80211_node *);
static void	mwl_node_getsignal(const struct ieee80211_node *,
			int8_t *, int8_t *);
static void	mwl_node_getmimoinfo(const struct ieee80211_node *,
			struct ieee80211_mimo_info *);
static int	mwl_rxbuf_init(struct mwl_softc *, struct mwl_rxbuf *);
static void	mwl_rx_proc(void *, int);
static void	mwl_txq_init(struct mwl_softc *sc, struct mwl_txq *, int);
static int	mwl_tx_setup(struct mwl_softc *, int, int);
static int	mwl_wme_update(struct ieee80211com *);
static void	mwl_tx_cleanupq(struct mwl_softc *, struct mwl_txq *);
static void	mwl_tx_cleanup(struct mwl_softc *);
static uint16_t	mwl_calcformat(uint8_t rate, const struct ieee80211_node *);
static int	mwl_tx_start(struct mwl_softc *, struct ieee80211_node *,
			     struct mwl_txbuf *, struct mbuf *);
static void	mwl_tx_proc(void *, int);
static int	mwl_chan_set(struct mwl_softc *, struct ieee80211_channel *);
static void	mwl_draintxq(struct mwl_softc *);
static void	mwl_cleartxq(struct mwl_softc *, struct ieee80211vap *);
static int	mwl_recv_action(struct ieee80211_node *,
			const struct ieee80211_frame *,
			const uint8_t *, const uint8_t *);
static int	mwl_addba_request(struct ieee80211_node *,
			struct ieee80211_tx_ampdu *, int dialogtoken,
			int baparamset, int batimeout);
static int	mwl_addba_response(struct ieee80211_node *,
			struct ieee80211_tx_ampdu *, int status,
			int baparamset, int batimeout);
static void	mwl_addba_stop(struct ieee80211_node *,
			struct ieee80211_tx_ampdu *);
static int	mwl_startrecv(struct mwl_softc *);
static MWL_HAL_APMODE mwl_getapmode(const struct ieee80211vap *,
			struct ieee80211_channel *);
static int	mwl_setapmode(struct ieee80211vap *, struct ieee80211_channel*);
static void	mwl_scan_start(struct ieee80211com *);
static void	mwl_scan_end(struct ieee80211com *);
static void	mwl_set_channel(struct ieee80211com *);
static int	mwl_peerstadb(struct ieee80211_node *,
			int aid, int staid, MWL_HAL_PEERINFO *pi);
static int	mwl_localstadb(struct ieee80211vap *);
static int	mwl_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static int	allocstaid(struct mwl_softc *sc, int aid);
static void	delstaid(struct mwl_softc *sc, int staid);
static void	mwl_newassoc(struct ieee80211_node *, int);
static void	mwl_agestations(void *);
static int	mwl_setregdomain(struct ieee80211com *,
			struct ieee80211_regdomain *, int,
			struct ieee80211_channel []);
static void	mwl_getradiocaps(struct ieee80211com *, int, int *,
			struct ieee80211_channel []);
static int	mwl_getchannels(struct mwl_softc *);

static void	mwl_sysctlattach(struct mwl_softc *);
static void	mwl_announce(struct mwl_softc *);

SYSCTL_NODE(_hw, OID_AUTO, mwl, CTLFLAG_RD, 0, "Marvell driver parameters");

static	int mwl_rxdesc = MWL_RXDESC;		/* # rx desc's to allocate */
SYSCTL_INT(_hw_mwl, OID_AUTO, rxdesc, CTLFLAG_RW, &mwl_rxdesc,
	    0, "rx descriptors allocated");
static	int mwl_rxbuf = MWL_RXBUF;		/* # rx buffers to allocate */
SYSCTL_INT(_hw_mwl, OID_AUTO, rxbuf, CTLFLAG_RWTUN, &mwl_rxbuf,
	    0, "rx buffers allocated");
static	int mwl_txbuf = MWL_TXBUF;		/* # tx buffers to allocate */
SYSCTL_INT(_hw_mwl, OID_AUTO, txbuf, CTLFLAG_RWTUN, &mwl_txbuf,
	    0, "tx buffers allocated");
static	int mwl_txcoalesce = 8;		/* # tx packets to q before poking f/w*/
SYSCTL_INT(_hw_mwl, OID_AUTO, txcoalesce, CTLFLAG_RWTUN, &mwl_txcoalesce,
	    0, "tx buffers to send at once");
static	int mwl_rxquota = MWL_RXBUF;		/* # max buffers to process */
SYSCTL_INT(_hw_mwl, OID_AUTO, rxquota, CTLFLAG_RWTUN, &mwl_rxquota,
	    0, "max rx buffers to process per interrupt");
static	int mwl_rxdmalow = 3;			/* # min buffers for wakeup */
SYSCTL_INT(_hw_mwl, OID_AUTO, rxdmalow, CTLFLAG_RWTUN, &mwl_rxdmalow,
	    0, "min free rx buffers before restarting traffic");

#ifdef MWL_DEBUG
static	int mwl_debug = 0;
SYSCTL_INT(_hw_mwl, OID_AUTO, debug, CTLFLAG_RWTUN, &mwl_debug,
	    0, "control debugging printfs");
enum {
	MWL_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	MWL_DEBUG_XMIT_DESC	= 0x00000002,	/* xmit descriptors */
	MWL_DEBUG_RECV		= 0x00000004,	/* basic recv operation */
	MWL_DEBUG_RECV_DESC	= 0x00000008,	/* recv descriptors */
	MWL_DEBUG_RESET		= 0x00000010,	/* reset processing */
	MWL_DEBUG_BEACON 	= 0x00000020,	/* beacon handling */
	MWL_DEBUG_INTR		= 0x00000040,	/* ISR */
	MWL_DEBUG_TX_PROC	= 0x00000080,	/* tx ISR proc */
	MWL_DEBUG_RX_PROC	= 0x00000100,	/* rx ISR proc */
	MWL_DEBUG_KEYCACHE	= 0x00000200,	/* key cache management */
	MWL_DEBUG_STATE		= 0x00000400,	/* 802.11 state transitions */
	MWL_DEBUG_NODE		= 0x00000800,	/* node management */
	MWL_DEBUG_RECV_ALL	= 0x00001000,	/* trace all frames (beacons) */
	MWL_DEBUG_TSO		= 0x00002000,	/* TSO processing */
	MWL_DEBUG_AMPDU		= 0x00004000,	/* BA stream handling */
	MWL_DEBUG_ANY		= 0xffffffff
};
#define	IS_BEACON(wh) \
    ((wh->i_fc[0] & (IEEE80211_FC0_TYPE_MASK|IEEE80211_FC0_SUBTYPE_MASK)) == \
	 (IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_BEACON))
#define	IFF_DUMPPKTS_RECV(sc, wh) \
    ((sc->sc_debug & MWL_DEBUG_RECV) && \
      ((sc->sc_debug & MWL_DEBUG_RECV_ALL) || !IS_BEACON(wh)))
#define	IFF_DUMPPKTS_XMIT(sc) \
	(sc->sc_debug & MWL_DEBUG_XMIT)

#define	DPRINTF(sc, m, fmt, ...) do {				\
	if (sc->sc_debug & (m))					\
		printf(fmt, __VA_ARGS__);			\
} while (0)
#define	KEYPRINTF(sc, hk, mac) do {				\
	if (sc->sc_debug & MWL_DEBUG_KEYCACHE)			\
		mwl_keyprint(sc, __func__, hk, mac);		\
} while (0)
static	void mwl_printrxbuf(const struct mwl_rxbuf *bf, u_int ix);
static	void mwl_printtxbuf(const struct mwl_txbuf *bf, u_int qnum, u_int ix);
#else
#define	IFF_DUMPPKTS_RECV(sc, wh)	0
#define	IFF_DUMPPKTS_XMIT(sc)		0
#define	DPRINTF(sc, m, fmt, ...)	do { (void )sc; } while (0)
#define	KEYPRINTF(sc, k, mac)		do { (void )sc; } while (0)
#endif

static MALLOC_DEFINE(M_MWLDEV, "mwldev", "mwl driver dma buffers");

/*
 * Each packet has fixed front matter: a 2-byte length
 * of the payload, followed by a 4-address 802.11 header
 * (regardless of the actual header and always w/o any
 * QoS header).  The payload then follows.
 */
struct mwltxrec {
	uint16_t fwlen;
	struct ieee80211_frame_addr4 wh;
} __packed;

/*
 * Read/Write shorthands for accesses to BAR 0.  Note
 * that all BAR 1 operations are done in the "hal" and
 * there should be no reference to them here.
 */
#ifdef MWL_DEBUG
static __inline uint32_t
RD4(struct mwl_softc *sc, bus_size_t off)
{
	return bus_space_read_4(sc->sc_io0t, sc->sc_io0h, off);
}
#endif

static __inline void
WR4(struct mwl_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->sc_io0t, sc->sc_io0h, off, val);
}

int
mwl_attach(uint16_t devid, struct mwl_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct mwl_hal *mh;
	int error = 0;

	DPRINTF(sc, MWL_DEBUG_ANY, "%s: devid 0x%x\n", __func__, devid);

	/*
	 * Setup the RX free list lock early, so it can be consistently
	 * removed.
	 */
	MWL_RXFREE_INIT(sc);

	mh = mwl_hal_attach(sc->sc_dev, devid,
	    sc->sc_io1h, sc->sc_io1t, sc->sc_dmat);
	if (mh == NULL) {
		device_printf(sc->sc_dev, "unable to attach HAL\n");
		error = EIO;
		goto bad;
	}
	sc->sc_mh = mh;
	/*
	 * Load firmware so we can get setup.  We arbitrarily
	 * pick station firmware; we'll re-load firmware as
	 * needed so setting up the wrong mode isn't a big deal.
	 */
	if (mwl_hal_fwload(mh, NULL) != 0) {
		device_printf(sc->sc_dev, "unable to setup builtin firmware\n");
		error = EIO;
		goto bad1;
	}
	if (mwl_hal_gethwspecs(mh, &sc->sc_hwspecs) != 0) {
		device_printf(sc->sc_dev, "unable to fetch h/w specs\n");
		error = EIO;
		goto bad1;
	}
	error = mwl_getchannels(sc);
	if (error != 0)
		goto bad1;

	sc->sc_txantenna = 0;		/* h/w default */
	sc->sc_rxantenna = 0;		/* h/w default */
	sc->sc_invalid = 0;		/* ready to go, enable int handling */
	sc->sc_ageinterval = MWL_AGEINTERVAL;

	/*
	 * Allocate tx+rx descriptors and populate the lists.
	 * We immediately push the information to the firmware
	 * as otherwise it gets upset.
	 */
	error = mwl_dma_setup(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "failed to setup descriptors: %d\n",
		    error);
		goto bad1;
	}
	error = mwl_setupdma(sc);	/* push to firmware */
	if (error != 0)			/* NB: mwl_setupdma prints msg */
		goto bad1;

	callout_init(&sc->sc_timer, 1);
	callout_init_mtx(&sc->sc_watchdog, &sc->sc_mtx, 0);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	sc->sc_tq = taskqueue_create("mwl_taskq", M_NOWAIT,
		taskqueue_thread_enqueue, &sc->sc_tq);
	taskqueue_start_threads(&sc->sc_tq, 1, PI_NET,
		"%s taskq", device_get_nameunit(sc->sc_dev));

	TASK_INIT(&sc->sc_rxtask, 0, mwl_rx_proc, sc);
	TASK_INIT(&sc->sc_radartask, 0, mwl_radar_proc, sc);
	TASK_INIT(&sc->sc_chanswitchtask, 0, mwl_chanswitch_proc, sc);
	TASK_INIT(&sc->sc_bawatchdogtask, 0, mwl_bawatchdog_proc, sc);

	/* NB: insure BK queue is the lowest priority h/w queue */
	if (!mwl_tx_setup(sc, WME_AC_BK, MWL_WME_AC_BK)) {
		device_printf(sc->sc_dev,
		    "unable to setup xmit queue for %s traffic!\n",
		     ieee80211_wme_acnames[WME_AC_BK]);
		error = EIO;
		goto bad2;
	}
	if (!mwl_tx_setup(sc, WME_AC_BE, MWL_WME_AC_BE) ||
	    !mwl_tx_setup(sc, WME_AC_VI, MWL_WME_AC_VI) ||
	    !mwl_tx_setup(sc, WME_AC_VO, MWL_WME_AC_VO)) {
		/*
		 * Not enough hardware tx queues to properly do WME;
		 * just punt and assign them all to the same h/w queue.
		 * We could do a better job of this if, for example,
		 * we allocate queues when we switch from station to
		 * AP mode.
		 */
		if (sc->sc_ac2q[WME_AC_VI] != NULL)
			mwl_tx_cleanupq(sc, sc->sc_ac2q[WME_AC_VI]);
		if (sc->sc_ac2q[WME_AC_BE] != NULL)
			mwl_tx_cleanupq(sc, sc->sc_ac2q[WME_AC_BE]);
		sc->sc_ac2q[WME_AC_BE] = sc->sc_ac2q[WME_AC_BK];
		sc->sc_ac2q[WME_AC_VI] = sc->sc_ac2q[WME_AC_BK];
		sc->sc_ac2q[WME_AC_VO] = sc->sc_ac2q[WME_AC_BK];
	}
	TASK_INIT(&sc->sc_txtask, 0, mwl_tx_proc, sc);

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(sc->sc_dev);
	/* XXX not right but it's not used anywhere important */
	ic->ic_phytype = IEEE80211_T_OFDM;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_caps =
		  IEEE80211_C_STA		/* station mode supported */
		| IEEE80211_C_HOSTAP		/* hostap mode */
		| IEEE80211_C_MONITOR		/* monitor mode */
#if 0
		| IEEE80211_C_IBSS		/* ibss, nee adhoc, mode */
		| IEEE80211_C_AHDEMO		/* adhoc demo mode */
#endif
		| IEEE80211_C_MBSS		/* mesh point link mode */
		| IEEE80211_C_WDS		/* WDS supported */
		| IEEE80211_C_SHPREAMBLE	/* short preamble supported */
		| IEEE80211_C_SHSLOT		/* short slot time supported */
		| IEEE80211_C_WME		/* WME/WMM supported */
		| IEEE80211_C_BURST		/* xmit bursting supported */
		| IEEE80211_C_WPA		/* capable of WPA1+WPA2 */
		| IEEE80211_C_BGSCAN		/* capable of bg scanning */
		| IEEE80211_C_TXFRAG		/* handle tx frags */
		| IEEE80211_C_TXPMGT		/* capable of txpow mgt */
		| IEEE80211_C_DFS		/* DFS supported */
		;

	ic->ic_htcaps =
		  IEEE80211_HTCAP_SMPS_ENA	/* SM PS mode enabled */
		| IEEE80211_HTCAP_CHWIDTH40	/* 40MHz channel width */
		| IEEE80211_HTCAP_SHORTGI20	/* short GI in 20MHz */
		| IEEE80211_HTCAP_SHORTGI40	/* short GI in 40MHz */
		| IEEE80211_HTCAP_RXSTBC_2STREAM/* 1-2 spatial streams */
#if MWL_AGGR_SIZE == 7935
		| IEEE80211_HTCAP_MAXAMSDU_7935	/* max A-MSDU length */
#else
		| IEEE80211_HTCAP_MAXAMSDU_3839	/* max A-MSDU length */
#endif
#if 0
		| IEEE80211_HTCAP_PSMP		/* PSMP supported */
		| IEEE80211_HTCAP_40INTOLERANT	/* 40MHz intolerant */
#endif
		/* s/w capabilities */
		| IEEE80211_HTC_HT		/* HT operation */
		| IEEE80211_HTC_AMPDU		/* tx A-MPDU */
		| IEEE80211_HTC_AMSDU		/* tx A-MSDU */
		| IEEE80211_HTC_SMPS		/* SMPS available */
		;

	/*
	 * Mark h/w crypto support.
	 * XXX no way to query h/w support.
	 */
	ic->ic_cryptocaps |= IEEE80211_CRYPTO_WEP
			  |  IEEE80211_CRYPTO_AES_CCM
			  |  IEEE80211_CRYPTO_TKIP
			  |  IEEE80211_CRYPTO_TKIPMIC
			  ;
	/*
	 * Transmit requires space in the packet for a special
	 * format transmit record and optional padding between
	 * this record and the payload.  Ask the net80211 layer
	 * to arrange this when encapsulating packets so we can
	 * add it efficiently. 
	 */
	ic->ic_headroom = sizeof(struct mwltxrec) -
		sizeof(struct ieee80211_frame);

	IEEE80211_ADDR_COPY(ic->ic_macaddr, sc->sc_hwspecs.macAddr);

	/* call MI attach routine. */
	ieee80211_ifattach(ic);
	ic->ic_setregdomain = mwl_setregdomain;
	ic->ic_getradiocaps = mwl_getradiocaps;
	/* override default methods */
	ic->ic_raw_xmit = mwl_raw_xmit;
	ic->ic_newassoc = mwl_newassoc;
	ic->ic_updateslot = mwl_updateslot;
	ic->ic_update_mcast = mwl_update_mcast;
	ic->ic_update_promisc = mwl_update_promisc;
	ic->ic_wme.wme_update = mwl_wme_update;
	ic->ic_transmit = mwl_transmit;
	ic->ic_ioctl = mwl_ioctl;
	ic->ic_parent = mwl_parent;

	ic->ic_node_alloc = mwl_node_alloc;
	sc->sc_node_cleanup = ic->ic_node_cleanup;
	ic->ic_node_cleanup = mwl_node_cleanup;
	sc->sc_node_drain = ic->ic_node_drain;
	ic->ic_node_drain = mwl_node_drain;
	ic->ic_node_getsignal = mwl_node_getsignal;
	ic->ic_node_getmimoinfo = mwl_node_getmimoinfo;

	ic->ic_scan_start = mwl_scan_start;
	ic->ic_scan_end = mwl_scan_end;
	ic->ic_set_channel = mwl_set_channel;

	sc->sc_recv_action = ic->ic_recv_action;
	ic->ic_recv_action = mwl_recv_action;
	sc->sc_addba_request = ic->ic_addba_request;
	ic->ic_addba_request = mwl_addba_request;
	sc->sc_addba_response = ic->ic_addba_response;
	ic->ic_addba_response = mwl_addba_response;
	sc->sc_addba_stop = ic->ic_addba_stop;
	ic->ic_addba_stop = mwl_addba_stop;

	ic->ic_vap_create = mwl_vap_create;
	ic->ic_vap_delete = mwl_vap_delete;

	ieee80211_radiotap_attach(ic,
	    &sc->sc_tx_th.wt_ihdr, sizeof(sc->sc_tx_th),
		MWL_TX_RADIOTAP_PRESENT,
	    &sc->sc_rx_th.wr_ihdr, sizeof(sc->sc_rx_th),
		MWL_RX_RADIOTAP_PRESENT);
	/*
	 * Setup dynamic sysctl's now that country code and
	 * regdomain are available from the hal.
	 */
	mwl_sysctlattach(sc);

	if (bootverbose)
		ieee80211_announce(ic);
	mwl_announce(sc);
	return 0;
bad2:
	mwl_dma_cleanup(sc);
bad1:
	mwl_hal_detach(mh);
bad:
	MWL_RXFREE_DESTROY(sc);
	sc->sc_invalid = 1;
	return error;
}

int
mwl_detach(struct mwl_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	MWL_LOCK(sc);
	mwl_stop(sc);
	MWL_UNLOCK(sc);
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
	callout_drain(&sc->sc_watchdog);
	mwl_dma_cleanup(sc);
	MWL_RXFREE_DESTROY(sc);
	mwl_tx_cleanup(sc);
	mwl_hal_detach(sc->sc_mh);
	mbufq_drain(&sc->sc_snd);

	return 0;
}

/*
 * MAC address handling for multiple BSS on the same radio.
 * The first vap uses the MAC address from the EEPROM.  For
 * subsequent vap's we set the U/L bit (bit 1) in the MAC
 * address and use the next six bits as an index.
 */
static void
assign_address(struct mwl_softc *sc, uint8_t mac[IEEE80211_ADDR_LEN], int clone)
{
	int i;

	if (clone && mwl_hal_ismbsscapable(sc->sc_mh)) {
		/* NB: we only do this if h/w supports multiple bssid */
		for (i = 0; i < 32; i++)
			if ((sc->sc_bssidmask & (1<<i)) == 0)
				break;
		if (i != 0)
			mac[0] |= (i << 2)|0x2;
	} else
		i = 0;
	sc->sc_bssidmask |= 1<<i;
	if (i == 0)
		sc->sc_nbssid0++;
}

static void
reclaim_address(struct mwl_softc *sc, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	int i = mac[0] >> 2;
	if (i != 0 || --sc->sc_nbssid0 == 0)
		sc->sc_bssidmask &= ~(1<<i);
}

static struct ieee80211vap *
mwl_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac0[IEEE80211_ADDR_LEN])
{
	struct mwl_softc *sc = ic->ic_softc;
	struct mwl_hal *mh = sc->sc_mh;
	struct ieee80211vap *vap, *apvap;
	struct mwl_hal_vap *hvap;
	struct mwl_vap *mvp;
	uint8_t mac[IEEE80211_ADDR_LEN];

	IEEE80211_ADDR_COPY(mac, mac0);
	switch (opmode) {
	case IEEE80211_M_HOSTAP:
	case IEEE80211_M_MBSS:
		if ((flags & IEEE80211_CLONE_MACADDR) == 0)
			assign_address(sc, mac, flags & IEEE80211_CLONE_BSSID);
		hvap = mwl_hal_newvap(mh, MWL_HAL_AP, mac);
		if (hvap == NULL) {
			if ((flags & IEEE80211_CLONE_MACADDR) == 0)
				reclaim_address(sc, mac);
			return NULL;
		}
		break;
	case IEEE80211_M_STA:
		if ((flags & IEEE80211_CLONE_MACADDR) == 0)
			assign_address(sc, mac, flags & IEEE80211_CLONE_BSSID);
		hvap = mwl_hal_newvap(mh, MWL_HAL_STA, mac);
		if (hvap == NULL) {
			if ((flags & IEEE80211_CLONE_MACADDR) == 0)
				reclaim_address(sc, mac);
			return NULL;
		}
		/* no h/w beacon miss support; always use s/w */
		flags |= IEEE80211_CLONE_NOBEACONS;
		break;
	case IEEE80211_M_WDS:
		hvap = NULL;		/* NB: we use associated AP vap */
		if (sc->sc_napvaps == 0)
			return NULL;	/* no existing AP vap */
		break;
	case IEEE80211_M_MONITOR:
		hvap = NULL;
		break;
	case IEEE80211_M_IBSS:
	case IEEE80211_M_AHDEMO:
	default:
		return NULL;
	}

	mvp = malloc(sizeof(struct mwl_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	mvp->mv_hvap = hvap;
	if (opmode == IEEE80211_M_WDS) {
		/*
		 * WDS vaps must have an associated AP vap; find one.
		 * XXX not right.
		 */
		TAILQ_FOREACH(apvap, &ic->ic_vaps, iv_next)
			if (apvap->iv_opmode == IEEE80211_M_HOSTAP) {
				mvp->mv_ap_hvap = MWL_VAP(apvap)->mv_hvap;
				break;
			}
		KASSERT(mvp->mv_ap_hvap != NULL, ("no ap vap"));
	}
	vap = &mvp->mv_vap;
	ieee80211_vap_setup(ic, vap, name, unit, opmode, flags, bssid);
	/* override with driver methods */
	mvp->mv_newstate = vap->iv_newstate;
	vap->iv_newstate = mwl_newstate;
	vap->iv_max_keyix = 0;	/* XXX */
	vap->iv_key_alloc = mwl_key_alloc;
	vap->iv_key_delete = mwl_key_delete;
	vap->iv_key_set = mwl_key_set;
#ifdef MWL_HOST_PS_SUPPORT
	if (opmode == IEEE80211_M_HOSTAP || opmode == IEEE80211_M_MBSS) {
		vap->iv_update_ps = mwl_update_ps;
		mvp->mv_set_tim = vap->iv_set_tim;
		vap->iv_set_tim = mwl_set_tim;
	}
#endif
	vap->iv_reset = mwl_reset;
	vap->iv_update_beacon = mwl_beacon_update;

	/* override max aid so sta's cannot assoc when we're out of sta id's */
	vap->iv_max_aid = MWL_MAXSTAID;
	/* override default A-MPDU rx parameters */
	vap->iv_ampdu_rxmax = IEEE80211_HTCAP_MAXRXAMPDU_64K;
	vap->iv_ampdu_density = IEEE80211_HTCAP_MPDUDENSITY_4;

	/* complete setup */
	ieee80211_vap_attach(vap, mwl_media_change, ieee80211_media_status,
	    mac);

	switch (vap->iv_opmode) {
	case IEEE80211_M_HOSTAP:
	case IEEE80211_M_MBSS:
	case IEEE80211_M_STA:
		/*
		 * Setup sta db entry for local address.
		 */
		mwl_localstadb(vap);
		if (vap->iv_opmode == IEEE80211_M_HOSTAP ||
		    vap->iv_opmode == IEEE80211_M_MBSS)
			sc->sc_napvaps++;
		else
			sc->sc_nstavaps++;
		break;
	case IEEE80211_M_WDS:
		sc->sc_nwdsvaps++;
		break;
	default:
		break;
	}
	/*
	 * Setup overall operating mode.
	 */
	if (sc->sc_napvaps)
		ic->ic_opmode = IEEE80211_M_HOSTAP;
	else if (sc->sc_nstavaps)
		ic->ic_opmode = IEEE80211_M_STA;
	else
		ic->ic_opmode = opmode;

	return vap;
}

static void
mwl_vap_delete(struct ieee80211vap *vap)
{
	struct mwl_vap *mvp = MWL_VAP(vap);
	struct mwl_softc *sc = vap->iv_ic->ic_softc;
	struct mwl_hal *mh = sc->sc_mh;
	struct mwl_hal_vap *hvap = mvp->mv_hvap;
	enum ieee80211_opmode opmode = vap->iv_opmode;

	/* XXX disallow ap vap delete if WDS still present */
	if (sc->sc_running) {
		/* quiesce h/w while we remove the vap */
		mwl_hal_intrset(mh, 0);		/* disable interrupts */
	}
	ieee80211_vap_detach(vap);
	switch (opmode) {
	case IEEE80211_M_HOSTAP:
	case IEEE80211_M_MBSS:
	case IEEE80211_M_STA:
		KASSERT(hvap != NULL, ("no hal vap handle"));
		(void) mwl_hal_delstation(hvap, vap->iv_myaddr);
		mwl_hal_delvap(hvap);
		if (opmode == IEEE80211_M_HOSTAP || opmode == IEEE80211_M_MBSS)
			sc->sc_napvaps--;
		else
			sc->sc_nstavaps--;
		/* XXX don't do it for IEEE80211_CLONE_MACADDR */
		reclaim_address(sc, vap->iv_myaddr);
		break;
	case IEEE80211_M_WDS:
		sc->sc_nwdsvaps--;
		break;
	default:
		break;
	}
	mwl_cleartxq(sc, vap);
	free(mvp, M_80211_VAP);
	if (sc->sc_running)
		mwl_hal_intrset(mh, sc->sc_imask);
}

void
mwl_suspend(struct mwl_softc *sc)
{

	MWL_LOCK(sc);
	mwl_stop(sc);
	MWL_UNLOCK(sc);
}

void
mwl_resume(struct mwl_softc *sc)
{
	int error = EDOOFUS;

	MWL_LOCK(sc);
	if (sc->sc_ic.ic_nrunning > 0)
		error = mwl_init(sc);
	MWL_UNLOCK(sc);

	if (error == 0)
		ieee80211_start_all(&sc->sc_ic);	/* start all vap's */
}

void
mwl_shutdown(void *arg)
{
	struct mwl_softc *sc = arg;

	MWL_LOCK(sc);
	mwl_stop(sc);
	MWL_UNLOCK(sc);
}

/*
 * Interrupt handler.  Most of the actual processing is deferred.
 */
void
mwl_intr(void *arg)
{
	struct mwl_softc *sc = arg;
	struct mwl_hal *mh = sc->sc_mh;
	uint32_t status;

	if (sc->sc_invalid) {
		/*
		 * The hardware is not ready/present, don't touch anything.
		 * Note this can happen early on if the IRQ is shared.
		 */
		DPRINTF(sc, MWL_DEBUG_ANY, "%s: invalid; ignored\n", __func__);
		return;
	}
	/*
	 * Figure out the reason(s) for the interrupt.
	 */
	mwl_hal_getisr(mh, &status);		/* NB: clears ISR too */
	if (status == 0)			/* must be a shared irq */
		return;

	DPRINTF(sc, MWL_DEBUG_INTR, "%s: status 0x%x imask 0x%x\n",
	    __func__, status, sc->sc_imask);
	if (status & MACREG_A2HRIC_BIT_RX_RDY)
		taskqueue_enqueue(sc->sc_tq, &sc->sc_rxtask);
	if (status & MACREG_A2HRIC_BIT_TX_DONE)
		taskqueue_enqueue(sc->sc_tq, &sc->sc_txtask);
	if (status & MACREG_A2HRIC_BIT_BA_WATCHDOG)
		taskqueue_enqueue(sc->sc_tq, &sc->sc_bawatchdogtask);
	if (status & MACREG_A2HRIC_BIT_OPC_DONE)
		mwl_hal_cmddone(mh);
	if (status & MACREG_A2HRIC_BIT_MAC_EVENT) {
		;
	}
	if (status & MACREG_A2HRIC_BIT_ICV_ERROR) {
		/* TKIP ICV error */
		sc->sc_stats.mst_rx_badtkipicv++;
	}
	if (status & MACREG_A2HRIC_BIT_QUEUE_EMPTY) {
		/* 11n aggregation queue is empty, re-fill */
		;
	}
	if (status & MACREG_A2HRIC_BIT_QUEUE_FULL) {
		;
	}
	if (status & MACREG_A2HRIC_BIT_RADAR_DETECT) {
		/* radar detected, process event */
		taskqueue_enqueue(sc->sc_tq, &sc->sc_radartask);
	}
	if (status & MACREG_A2HRIC_BIT_CHAN_SWITCH) {
		/* DFS channel switch */
		taskqueue_enqueue(sc->sc_tq, &sc->sc_chanswitchtask);
	}
}

static void
mwl_radar_proc(void *arg, int pending)
{
	struct mwl_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	DPRINTF(sc, MWL_DEBUG_ANY, "%s: radar detected, pending %u\n",
	    __func__, pending);

	sc->sc_stats.mst_radardetect++;
	/* XXX stop h/w BA streams? */

	IEEE80211_LOCK(ic);
	ieee80211_dfs_notify_radar(ic, ic->ic_curchan);
	IEEE80211_UNLOCK(ic);
}

static void
mwl_chanswitch_proc(void *arg, int pending)
{
	struct mwl_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	DPRINTF(sc, MWL_DEBUG_ANY, "%s: channel switch notice, pending %u\n",
	    __func__, pending);

	IEEE80211_LOCK(ic);
	sc->sc_csapending = 0;
	ieee80211_csa_completeswitch(ic);
	IEEE80211_UNLOCK(ic);
}

static void
mwl_bawatchdog(const MWL_HAL_BASTREAM *sp)
{
	struct ieee80211_node *ni = sp->data[0];

	/* send DELBA and drop the stream */
	ieee80211_ampdu_stop(ni, sp->data[1], IEEE80211_REASON_UNSPECIFIED);
}

static void
mwl_bawatchdog_proc(void *arg, int pending)
{
	struct mwl_softc *sc = arg;
	struct mwl_hal *mh = sc->sc_mh;
	const MWL_HAL_BASTREAM *sp;
	uint8_t bitmap, n;

	sc->sc_stats.mst_bawatchdog++;

	if (mwl_hal_getwatchdogbitmap(mh, &bitmap) != 0) {
		DPRINTF(sc, MWL_DEBUG_AMPDU,
		    "%s: could not get bitmap\n", __func__);
		sc->sc_stats.mst_bawatchdog_failed++;
		return;
	}
	DPRINTF(sc, MWL_DEBUG_AMPDU, "%s: bitmap 0x%x\n", __func__, bitmap);
	if (bitmap == 0xff) {
		n = 0;
		/* disable all ba streams */
		for (bitmap = 0; bitmap < 8; bitmap++) {
			sp = mwl_hal_bastream_lookup(mh, bitmap);
			if (sp != NULL) {
				mwl_bawatchdog(sp);
				n++;
			}
		}
		if (n == 0) {
			DPRINTF(sc, MWL_DEBUG_AMPDU,
			    "%s: no BA streams found\n", __func__);
			sc->sc_stats.mst_bawatchdog_empty++;
		}
	} else if (bitmap != 0xaa) {
		/* disable a single ba stream */
		sp = mwl_hal_bastream_lookup(mh, bitmap);
		if (sp != NULL) {
			mwl_bawatchdog(sp);
		} else {
			DPRINTF(sc, MWL_DEBUG_AMPDU,
			    "%s: no BA stream %d\n", __func__, bitmap);
			sc->sc_stats.mst_bawatchdog_notfound++;
		}
	}
}

/*
 * Convert net80211 channel to a HAL channel.
 */
static void
mwl_mapchan(MWL_HAL_CHANNEL *hc, const struct ieee80211_channel *chan)
{
	hc->channel = chan->ic_ieee;

	*(uint32_t *)&hc->channelFlags = 0;
	if (IEEE80211_IS_CHAN_2GHZ(chan))
		hc->channelFlags.FreqBand = MWL_FREQ_BAND_2DOT4GHZ;
	else if (IEEE80211_IS_CHAN_5GHZ(chan))
		hc->channelFlags.FreqBand = MWL_FREQ_BAND_5GHZ;
	if (IEEE80211_IS_CHAN_HT40(chan)) {
		hc->channelFlags.ChnlWidth = MWL_CH_40_MHz_WIDTH;
		if (IEEE80211_IS_CHAN_HT40U(chan))
			hc->channelFlags.ExtChnlOffset = MWL_EXT_CH_ABOVE_CTRL_CH;
		else
			hc->channelFlags.ExtChnlOffset = MWL_EXT_CH_BELOW_CTRL_CH;
	} else
		hc->channelFlags.ChnlWidth = MWL_CH_20_MHz_WIDTH;
	/* XXX 10MHz channels */
}

/*
 * Inform firmware of our tx/rx dma setup.  The BAR 0
 * writes below are for compatibility with older firmware.
 * For current firmware we send this information with a
 * cmd block via mwl_hal_sethwdma.
 */
static int
mwl_setupdma(struct mwl_softc *sc)
{
	int error, i;

	sc->sc_hwdma.rxDescRead = sc->sc_rxdma.dd_desc_paddr;
	WR4(sc, sc->sc_hwspecs.rxDescRead, sc->sc_hwdma.rxDescRead);
	WR4(sc, sc->sc_hwspecs.rxDescWrite, sc->sc_hwdma.rxDescRead);

	for (i = 0; i < MWL_NUM_TX_QUEUES-MWL_NUM_ACK_QUEUES; i++) {
		struct mwl_txq *txq = &sc->sc_txq[i];
		sc->sc_hwdma.wcbBase[i] = txq->dma.dd_desc_paddr;
		WR4(sc, sc->sc_hwspecs.wcbBase[i], sc->sc_hwdma.wcbBase[i]);
	}
	sc->sc_hwdma.maxNumTxWcb = mwl_txbuf;
	sc->sc_hwdma.maxNumWCB = MWL_NUM_TX_QUEUES-MWL_NUM_ACK_QUEUES;

	error = mwl_hal_sethwdma(sc->sc_mh, &sc->sc_hwdma);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "unable to setup tx/rx dma; hal status %u\n", error);
		/* XXX */
	}
	return error;
}

/*
 * Inform firmware of tx rate parameters.
 * Called after a channel change.
 */
static int
mwl_setcurchanrates(struct mwl_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const struct ieee80211_rateset *rs;
	MWL_HAL_TXRATE rates;

	memset(&rates, 0, sizeof(rates));
	rs = ieee80211_get_suprates(ic, ic->ic_curchan);
	/* rate used to send management frames */
	rates.MgtRate = rs->rs_rates[0] & IEEE80211_RATE_VAL;
	/* rate used to send multicast frames */
	rates.McastRate = rates.MgtRate;

	return mwl_hal_settxrate_auto(sc->sc_mh, &rates);
}

/*
 * Inform firmware of tx rate parameters.  Called whenever
 * user-settable params change and after a channel change.
 */
static int
mwl_setrates(struct ieee80211vap *vap)
{
	struct mwl_vap *mvp = MWL_VAP(vap);
	struct ieee80211_node *ni = vap->iv_bss;
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	MWL_HAL_TXRATE rates;

	KASSERT(vap->iv_state == IEEE80211_S_RUN, ("state %d", vap->iv_state));

	/*
	 * Update the h/w rate map.
	 * NB: 0x80 for MCS is passed through unchanged
	 */
	memset(&rates, 0, sizeof(rates));
	/* rate used to send management frames */
	rates.MgtRate = tp->mgmtrate;
	/* rate used to send multicast frames */
	rates.McastRate = tp->mcastrate;

	/* while here calculate EAPOL fixed rate cookie */
	mvp->mv_eapolformat = htole16(mwl_calcformat(rates.MgtRate, ni));

	return mwl_hal_settxrate(mvp->mv_hvap,
	    tp->ucastrate != IEEE80211_FIXED_RATE_NONE ?
		RATE_FIXED : RATE_AUTO, &rates);
}

/*
 * Setup a fixed xmit rate cookie for EAPOL frames.
 */
static void
mwl_seteapolformat(struct ieee80211vap *vap)
{
	struct mwl_vap *mvp = MWL_VAP(vap);
	struct ieee80211_node *ni = vap->iv_bss;
	enum ieee80211_phymode mode;
	uint8_t rate;

	KASSERT(vap->iv_state == IEEE80211_S_RUN, ("state %d", vap->iv_state));

	mode = ieee80211_chan2mode(ni->ni_chan);
	/*
	 * Use legacy rates when operating a mixed HT+non-HT bss.
	 * NB: this may violate POLA for sta and wds vap's.
	 */
	if (mode == IEEE80211_MODE_11NA &&
	    (vap->iv_flags_ht & IEEE80211_FHT_PUREN) == 0)
		rate = vap->iv_txparms[IEEE80211_MODE_11A].mgmtrate;
	else if (mode == IEEE80211_MODE_11NG &&
	    (vap->iv_flags_ht & IEEE80211_FHT_PUREN) == 0)
		rate = vap->iv_txparms[IEEE80211_MODE_11G].mgmtrate;
	else
		rate = vap->iv_txparms[mode].mgmtrate;

	mvp->mv_eapolformat = htole16(mwl_calcformat(rate, ni));
}

/*
 * Map SKU+country code to region code for radar bin'ing.
 */
static int
mwl_map2regioncode(const struct ieee80211_regdomain *rd)
{
	switch (rd->regdomain) {
	case SKU_FCC:
	case SKU_FCC3:
		return DOMAIN_CODE_FCC;
	case SKU_CA:
		return DOMAIN_CODE_IC;
	case SKU_ETSI:
	case SKU_ETSI2:
	case SKU_ETSI3:
		if (rd->country == CTRY_SPAIN)
			return DOMAIN_CODE_SPAIN;
		if (rd->country == CTRY_FRANCE || rd->country == CTRY_FRANCE2)
			return DOMAIN_CODE_FRANCE;
		/* XXX force 1.3.1 radar type */
		return DOMAIN_CODE_ETSI_131;
	case SKU_JAPAN:
		return DOMAIN_CODE_MKK;
	case SKU_ROW:
		return DOMAIN_CODE_DGT;	/* Taiwan */
	case SKU_APAC:
	case SKU_APAC2:
	case SKU_APAC3:
		return DOMAIN_CODE_AUS;	/* Australia */
	}
	/* XXX KOREA? */
	return DOMAIN_CODE_FCC;			/* XXX? */
}

static int
mwl_hal_reset(struct mwl_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct mwl_hal *mh = sc->sc_mh;

	mwl_hal_setantenna(mh, WL_ANTENNATYPE_RX, sc->sc_rxantenna);
	mwl_hal_setantenna(mh, WL_ANTENNATYPE_TX, sc->sc_txantenna);
	mwl_hal_setradio(mh, 1, WL_AUTO_PREAMBLE);
	mwl_hal_setwmm(sc->sc_mh, (ic->ic_flags & IEEE80211_F_WME) != 0);
	mwl_chan_set(sc, ic->ic_curchan);
	/* NB: RF/RA performance tuned for indoor mode */
	mwl_hal_setrateadaptmode(mh, 0);
	mwl_hal_setoptimizationlevel(mh,
	    (ic->ic_flags & IEEE80211_F_BURST) != 0);

	mwl_hal_setregioncode(mh, mwl_map2regioncode(&ic->ic_regdomain));

	mwl_hal_setaggampduratemode(mh, 1, 80);		/* XXX */
	mwl_hal_setcfend(mh, 0);			/* XXX */

	return 1;
}

static int
mwl_init(struct mwl_softc *sc)
{
	struct mwl_hal *mh = sc->sc_mh;
	int error = 0;

	MWL_LOCK_ASSERT(sc);

	/*
	 * Stop anything previously setup.  This is safe
	 * whether this is the first time through or not.
	 */
	mwl_stop(sc);

	/*
	 * Push vap-independent state to the firmware.
	 */
	if (!mwl_hal_reset(sc)) {
		device_printf(sc->sc_dev, "unable to reset hardware\n");
		return EIO;
	}

	/*
	 * Setup recv (once); transmit is already good to go.
	 */
	error = mwl_startrecv(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "unable to start recv logic\n");
		return error;
	}

	/*
	 * Enable interrupts.
	 */
	sc->sc_imask = MACREG_A2HRIC_BIT_RX_RDY
		     | MACREG_A2HRIC_BIT_TX_DONE
		     | MACREG_A2HRIC_BIT_OPC_DONE
#if 0
		     | MACREG_A2HRIC_BIT_MAC_EVENT
#endif
		     | MACREG_A2HRIC_BIT_ICV_ERROR
		     | MACREG_A2HRIC_BIT_RADAR_DETECT
		     | MACREG_A2HRIC_BIT_CHAN_SWITCH
#if 0
		     | MACREG_A2HRIC_BIT_QUEUE_EMPTY
#endif
		     | MACREG_A2HRIC_BIT_BA_WATCHDOG
		     | MACREQ_A2HRIC_BIT_TX_ACK
		     ;

	sc->sc_running = 1;
	mwl_hal_intrset(mh, sc->sc_imask);
	callout_reset(&sc->sc_watchdog, hz, mwl_watchdog, sc);

	return 0;
}

static void
mwl_stop(struct mwl_softc *sc)
{

	MWL_LOCK_ASSERT(sc);
	if (sc->sc_running) {
		/*
		 * Shutdown the hardware and driver.
		 */
		sc->sc_running = 0;
		callout_stop(&sc->sc_watchdog);
		sc->sc_tx_timer = 0;
		mwl_draintxq(sc);
	}
}

static int
mwl_reset_vap(struct ieee80211vap *vap, int state)
{
	struct mwl_hal_vap *hvap = MWL_VAP(vap)->mv_hvap;
	struct ieee80211com *ic = vap->iv_ic;

	if (state == IEEE80211_S_RUN)
		mwl_setrates(vap);
	/* XXX off by 1? */
	mwl_hal_setrtsthreshold(hvap, vap->iv_rtsthreshold);
	/* XXX auto? 20/40 split? */
	mwl_hal_sethtgi(hvap, (vap->iv_flags_ht &
	    (IEEE80211_FHT_SHORTGI20|IEEE80211_FHT_SHORTGI40)) ? 1 : 0);
	mwl_hal_setnprot(hvap, ic->ic_htprotmode == IEEE80211_PROT_NONE ?
	    HTPROTECT_NONE : HTPROTECT_AUTO);
	/* XXX txpower cap */

	/* re-setup beacons */
	if (state == IEEE80211_S_RUN &&
	    (vap->iv_opmode == IEEE80211_M_HOSTAP ||
	     vap->iv_opmode == IEEE80211_M_MBSS ||
	     vap->iv_opmode == IEEE80211_M_IBSS)) {
		mwl_setapmode(vap, vap->iv_bss->ni_chan);
		mwl_hal_setnprotmode(hvap,
		    MS(ic->ic_curhtprotmode, IEEE80211_HTINFO_OPMODE));
		return mwl_beacon_setup(vap);
	}
	return 0;
}

/*
 * Reset the hardware w/o losing operational state.
 * Used to reset or reload hardware state for a vap.
 */
static int
mwl_reset(struct ieee80211vap *vap, u_long cmd)
{
	struct mwl_hal_vap *hvap = MWL_VAP(vap)->mv_hvap;
	int error = 0;

	if (hvap != NULL) {			/* WDS, MONITOR, etc. */
		struct ieee80211com *ic = vap->iv_ic;
		struct mwl_softc *sc = ic->ic_softc;
		struct mwl_hal *mh = sc->sc_mh;

		/* XXX handle DWDS sta vap change */
		/* XXX do we need to disable interrupts? */
		mwl_hal_intrset(mh, 0);		/* disable interrupts */
		error = mwl_reset_vap(vap, vap->iv_state);
		mwl_hal_intrset(mh, sc->sc_imask);
	}
	return error;
}

/*
 * Allocate a tx buffer for sending a frame.  The
 * packet is assumed to have the WME AC stored so
 * we can use it to select the appropriate h/w queue.
 */
static struct mwl_txbuf *
mwl_gettxbuf(struct mwl_softc *sc, struct mwl_txq *txq)
{
	struct mwl_txbuf *bf;

	/*
	 * Grab a TX buffer and associated resources.
	 */
	MWL_TXQ_LOCK(txq);
	bf = STAILQ_FIRST(&txq->free);
	if (bf != NULL) {
		STAILQ_REMOVE_HEAD(&txq->free, bf_list);
		txq->nfree--;
	}
	MWL_TXQ_UNLOCK(txq);
	if (bf == NULL)
		DPRINTF(sc, MWL_DEBUG_XMIT,
		    "%s: out of xmit buffers on q %d\n", __func__, txq->qnum);
	return bf;
}

/*
 * Return a tx buffer to the queue it came from.  Note there
 * are two cases because we must preserve the order of buffers
 * as it reflects the fixed order of descriptors in memory
 * (the firmware pre-fetches descriptors so we cannot reorder).
 */
static void
mwl_puttxbuf_head(struct mwl_txq *txq, struct mwl_txbuf *bf)
{
	bf->bf_m = NULL;
	bf->bf_node = NULL;
	MWL_TXQ_LOCK(txq);
	STAILQ_INSERT_HEAD(&txq->free, bf, bf_list);
	txq->nfree++;
	MWL_TXQ_UNLOCK(txq);
}

static void
mwl_puttxbuf_tail(struct mwl_txq *txq, struct mwl_txbuf *bf)
{
	bf->bf_m = NULL;
	bf->bf_node = NULL;
	MWL_TXQ_LOCK(txq);
	STAILQ_INSERT_TAIL(&txq->free, bf, bf_list);
	txq->nfree++;
	MWL_TXQ_UNLOCK(txq);
}

static int
mwl_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct mwl_softc *sc = ic->ic_softc;
	int error;

	MWL_LOCK(sc);
	if (!sc->sc_running) {
		MWL_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		MWL_UNLOCK(sc);
		return (error);
	}
	mwl_start(sc);
	MWL_UNLOCK(sc);
	return (0);
}

static void
mwl_start(struct mwl_softc *sc)
{
	struct ieee80211_node *ni;
	struct mwl_txbuf *bf;
	struct mbuf *m;
	struct mwl_txq *txq = NULL;	/* XXX silence gcc */
	int nqueued;

	MWL_LOCK_ASSERT(sc);
	if (!sc->sc_running || sc->sc_invalid)
		return;
	nqueued = 0;
	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		/*
		 * Grab the node for the destination.
		 */
		ni = (struct ieee80211_node *) m->m_pkthdr.rcvif;
		KASSERT(ni != NULL, ("no node"));
		m->m_pkthdr.rcvif = NULL;	/* committed, clear ref */
		/*
		 * Grab a TX buffer and associated resources.
		 * We honor the classification by the 802.11 layer.
		 */
		txq = sc->sc_ac2q[M_WME_GETAC(m)];
		bf = mwl_gettxbuf(sc, txq);
		if (bf == NULL) {
			m_freem(m);
			ieee80211_free_node(ni);
#ifdef MWL_TX_NODROP
			sc->sc_stats.mst_tx_qstop++;
			break;
#else
			DPRINTF(sc, MWL_DEBUG_XMIT,
			    "%s: tail drop on q %d\n", __func__, txq->qnum);
			sc->sc_stats.mst_tx_qdrop++;
			continue;
#endif /* MWL_TX_NODROP */
		}

		/*
		 * Pass the frame to the h/w for transmission.
		 */
		if (mwl_tx_start(sc, ni, bf, m)) {
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			mwl_puttxbuf_head(txq, bf);
			ieee80211_free_node(ni);
			continue;
		}
		nqueued++;
		if (nqueued >= mwl_txcoalesce) {
			/*
			 * Poke the firmware to process queued frames;
			 * see below about (lack of) locking.
			 */
			nqueued = 0;
			mwl_hal_txstart(sc->sc_mh, 0/*XXX*/);
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
		 * mwl_tx_start is an optimization to avoid poking the
		 * firmware for each packet.
		 *
		 * NB: the queue id isn't used so 0 is ok.
		 */
		mwl_hal_txstart(sc->sc_mh, 0/*XXX*/);
	}
}

static int
mwl_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
	const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct mwl_softc *sc = ic->ic_softc;
	struct mwl_txbuf *bf;
	struct mwl_txq *txq;

	if (!sc->sc_running || sc->sc_invalid) {
		m_freem(m);
		return ENETDOWN;
	}
	/*
	 * Grab a TX buffer and associated resources.
	 * Note that we depend on the classification
	 * by the 802.11 layer to get to the right h/w
	 * queue.  Management frames must ALWAYS go on
	 * queue 1 but we cannot just force that here
	 * because we may receive non-mgt frames.
	 */
	txq = sc->sc_ac2q[M_WME_GETAC(m)];
	bf = mwl_gettxbuf(sc, txq);
	if (bf == NULL) {
		sc->sc_stats.mst_tx_qstop++;
		m_freem(m);
		return ENOBUFS;
	}
	/*
	 * Pass the frame to the h/w for transmission.
	 */
	if (mwl_tx_start(sc, ni, bf, m)) {
		mwl_puttxbuf_head(txq, bf);

		return EIO;		/* XXX */
	}
	/*
	 * NB: We don't need to lock against tx done because
	 * this just prods the firmware to check the transmit
	 * descriptors.  The firmware will also start fetching
	 * descriptors by itself if it notices new ones are
	 * present when it goes to deliver a tx done interrupt
	 * to the host. So if we race with tx done processing
	 * it's ok.  Delivering the kick here rather than in
	 * mwl_tx_start is an optimization to avoid poking the
	 * firmware for each packet.
	 *
	 * NB: the queue id isn't used so 0 is ok.
	 */
	mwl_hal_txstart(sc->sc_mh, 0/*XXX*/);
	return 0;
}

static int
mwl_media_change(struct ifnet *ifp)
{
	struct ieee80211vap *vap = ifp->if_softc;
	int error;

	error = ieee80211_media_change(ifp);
	/* NB: only the fixed rate can change and that doesn't need a reset */
	if (error == ENETRESET) {
		mwl_setrates(vap);
		error = 0;
	}
	return error;
}

#ifdef MWL_DEBUG
static void
mwl_keyprint(struct mwl_softc *sc, const char *tag,
	const MWL_HAL_KEYVAL *hk, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	static const char *ciphers[] = {
		"WEP",
		"TKIP",
		"AES-CCM",
	};
	int i, n;

	printf("%s: [%u] %-7s", tag, hk->keyIndex, ciphers[hk->keyTypeId]);
	for (i = 0, n = hk->keyLen; i < n; i++)
		printf(" %02x", hk->key.aes[i]);
	printf(" mac %s", ether_sprintf(mac));
	if (hk->keyTypeId == KEY_TYPE_ID_TKIP) {
		printf(" %s", "rxmic");
		for (i = 0; i < sizeof(hk->key.tkip.rxMic); i++)
			printf(" %02x", hk->key.tkip.rxMic[i]);
		printf(" txmic");
		for (i = 0; i < sizeof(hk->key.tkip.txMic); i++)
			printf(" %02x", hk->key.tkip.txMic[i]);
	}
	printf(" flags 0x%x\n", hk->keyFlags);
}
#endif

/*
 * Allocate a key cache slot for a unicast key.  The
 * firmware handles key allocation and every station is
 * guaranteed key space so we are always successful.
 */
static int
mwl_key_alloc(struct ieee80211vap *vap, struct ieee80211_key *k,
	ieee80211_keyix *keyix, ieee80211_keyix *rxkeyix)
{
	struct mwl_softc *sc = vap->iv_ic->ic_softc;

	if (k->wk_keyix != IEEE80211_KEYIX_NONE ||
	    (k->wk_flags & IEEE80211_KEY_GROUP)) {
		if (!(&vap->iv_nw_keys[0] <= k &&
		      k < &vap->iv_nw_keys[IEEE80211_WEP_NKID])) {
			/* should not happen */
			DPRINTF(sc, MWL_DEBUG_KEYCACHE,
				"%s: bogus group key\n", __func__);
			return 0;
		}
		/* give the caller what they requested */
		*keyix = *rxkeyix = ieee80211_crypto_get_key_wepidx(vap, k);
	} else {
		/*
		 * Firmware handles key allocation.
		 */
		*keyix = *rxkeyix = 0;
	}
	return 1;
}

/*
 * Delete a key entry allocated by mwl_key_alloc.
 */
static int
mwl_key_delete(struct ieee80211vap *vap, const struct ieee80211_key *k)
{
	struct mwl_softc *sc = vap->iv_ic->ic_softc;
	struct mwl_hal_vap *hvap = MWL_VAP(vap)->mv_hvap;
	MWL_HAL_KEYVAL hk;
	const uint8_t bcastaddr[IEEE80211_ADDR_LEN] =
	    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	if (hvap == NULL) {
		if (vap->iv_opmode != IEEE80211_M_WDS) {
			/* XXX monitor mode? */
			DPRINTF(sc, MWL_DEBUG_KEYCACHE,
			    "%s: no hvap for opmode %d\n", __func__,
			    vap->iv_opmode);
			return 0;
		}
		hvap = MWL_VAP(vap)->mv_ap_hvap;
	}

	DPRINTF(sc, MWL_DEBUG_KEYCACHE, "%s: delete key %u\n",
	    __func__, k->wk_keyix);

	memset(&hk, 0, sizeof(hk));
	hk.keyIndex = k->wk_keyix;
	switch (k->wk_cipher->ic_cipher) {
	case IEEE80211_CIPHER_WEP:
		hk.keyTypeId = KEY_TYPE_ID_WEP;
		break;
	case IEEE80211_CIPHER_TKIP:
		hk.keyTypeId = KEY_TYPE_ID_TKIP;
		break;
	case IEEE80211_CIPHER_AES_CCM:
		hk.keyTypeId = KEY_TYPE_ID_AES;
		break;
	default:
		/* XXX should not happen */
		DPRINTF(sc, MWL_DEBUG_KEYCACHE, "%s: unknown cipher %d\n",
		    __func__, k->wk_cipher->ic_cipher);
		return 0;
	}
	return (mwl_hal_keyreset(hvap, &hk, bcastaddr) == 0);	/*XXX*/
}

static __inline int
addgroupflags(MWL_HAL_KEYVAL *hk, const struct ieee80211_key *k)
{
	if (k->wk_flags & IEEE80211_KEY_GROUP) {
		if (k->wk_flags & IEEE80211_KEY_XMIT)
			hk->keyFlags |= KEY_FLAG_TXGROUPKEY;
		if (k->wk_flags & IEEE80211_KEY_RECV)
			hk->keyFlags |= KEY_FLAG_RXGROUPKEY;
		return 1;
	} else
		return 0;
}

/*
 * Set the key cache contents for the specified key.  Key cache
 * slot(s) must already have been allocated by mwl_key_alloc.
 */
static int
mwl_key_set(struct ieee80211vap *vap, const struct ieee80211_key *k)
{
	return (_mwl_key_set(vap, k, k->wk_macaddr));
}

static int
_mwl_key_set(struct ieee80211vap *vap, const struct ieee80211_key *k,
	const uint8_t mac[IEEE80211_ADDR_LEN])
{
#define	GRPXMIT	(IEEE80211_KEY_XMIT | IEEE80211_KEY_GROUP)
/* NB: static wep keys are marked GROUP+tx/rx; GTK will be tx or rx */
#define	IEEE80211_IS_STATICKEY(k) \
	(((k)->wk_flags & (GRPXMIT|IEEE80211_KEY_RECV)) == \
	 (GRPXMIT|IEEE80211_KEY_RECV))
	struct mwl_softc *sc = vap->iv_ic->ic_softc;
	struct mwl_hal_vap *hvap = MWL_VAP(vap)->mv_hvap;
	const struct ieee80211_cipher *cip = k->wk_cipher;
	const uint8_t *macaddr;
	MWL_HAL_KEYVAL hk;

	KASSERT((k->wk_flags & IEEE80211_KEY_SWCRYPT) == 0,
		("s/w crypto set?"));

	if (hvap == NULL) {
		if (vap->iv_opmode != IEEE80211_M_WDS) {
			/* XXX monitor mode? */
			DPRINTF(sc, MWL_DEBUG_KEYCACHE,
			    "%s: no hvap for opmode %d\n", __func__,
			    vap->iv_opmode);
			return 0;
		}
		hvap = MWL_VAP(vap)->mv_ap_hvap;
	}
	memset(&hk, 0, sizeof(hk));
	hk.keyIndex = k->wk_keyix;
	switch (cip->ic_cipher) {
	case IEEE80211_CIPHER_WEP:
		hk.keyTypeId = KEY_TYPE_ID_WEP;
		hk.keyLen = k->wk_keylen;
		if (k->wk_keyix == vap->iv_def_txkey)
			hk.keyFlags = KEY_FLAG_WEP_TXKEY;
		if (!IEEE80211_IS_STATICKEY(k)) {
			/* NB: WEP is never used for the PTK */
			(void) addgroupflags(&hk, k);
		}
		break;
	case IEEE80211_CIPHER_TKIP:
		hk.keyTypeId = KEY_TYPE_ID_TKIP;
		hk.key.tkip.tsc.high = (uint32_t)(k->wk_keytsc >> 16);
		hk.key.tkip.tsc.low = (uint16_t)k->wk_keytsc;
		hk.keyFlags = KEY_FLAG_TSC_VALID | KEY_FLAG_MICKEY_VALID;
		hk.keyLen = k->wk_keylen + IEEE80211_MICBUF_SIZE;
		if (!addgroupflags(&hk, k))
			hk.keyFlags |= KEY_FLAG_PAIRWISE;
		break;
	case IEEE80211_CIPHER_AES_CCM:
		hk.keyTypeId = KEY_TYPE_ID_AES;
		hk.keyLen = k->wk_keylen;
		if (!addgroupflags(&hk, k))
			hk.keyFlags |= KEY_FLAG_PAIRWISE;
		break;
	default:
		/* XXX should not happen */
		DPRINTF(sc, MWL_DEBUG_KEYCACHE, "%s: unknown cipher %d\n",
		    __func__, k->wk_cipher->ic_cipher);
		return 0;
	}
	/*
	 * NB: tkip mic keys get copied here too; the layout
	 *     just happens to match that in ieee80211_key.
	 */
	memcpy(hk.key.aes, k->wk_key, hk.keyLen);

	/*
	 * Locate address of sta db entry for writing key;
	 * the convention unfortunately is somewhat different
	 * than how net80211, hostapd, and wpa_supplicant think.
	 */
	if (vap->iv_opmode == IEEE80211_M_STA) {
		/*
		 * NB: keys plumbed before the sta reaches AUTH state
		 * will be discarded or written to the wrong sta db
		 * entry because iv_bss is meaningless.  This is ok
		 * (right now) because we handle deferred plumbing of
		 * WEP keys when the sta reaches AUTH state.
		 */
		macaddr = vap->iv_bss->ni_bssid;
		if ((k->wk_flags & IEEE80211_KEY_GROUP) == 0) {
			/* XXX plumb to local sta db too for static key wep */
			mwl_hal_keyset(hvap, &hk, vap->iv_myaddr);
		}
	} else if (vap->iv_opmode == IEEE80211_M_WDS &&
	    vap->iv_state != IEEE80211_S_RUN) {
		/*
		 * Prior to RUN state a WDS vap will not it's BSS node
		 * setup so we will plumb the key to the wrong mac
		 * address (it'll be our local address).  Workaround
		 * this for the moment by grabbing the correct address.
		 */
		macaddr = vap->iv_des_bssid;
	} else if ((k->wk_flags & GRPXMIT) == GRPXMIT)
		macaddr = vap->iv_myaddr;
	else
		macaddr = mac;
	KEYPRINTF(sc, &hk, macaddr);
	return (mwl_hal_keyset(hvap, &hk, macaddr) == 0);
#undef IEEE80211_IS_STATICKEY
#undef GRPXMIT
}

/*
 * Set the multicast filter contents into the hardware.
 * XXX f/w has no support; just defer to the os.
 */
static void
mwl_setmcastfilter(struct mwl_softc *sc)
{
#if 0
	struct ether_multi *enm;
	struct ether_multistep estep;
	uint8_t macs[IEEE80211_ADDR_LEN*MWL_HAL_MCAST_MAX];/* XXX stack use */
	uint8_t *mp;
	int nmc;

	mp = macs;
	nmc = 0;
	ETHER_FIRST_MULTI(estep, &sc->sc_ec, enm);
	while (enm != NULL) {
		/* XXX Punt on ranges. */
		if (nmc == MWL_HAL_MCAST_MAX ||
		    !IEEE80211_ADDR_EQ(enm->enm_addrlo, enm->enm_addrhi)) {
			ifp->if_flags |= IFF_ALLMULTI;
			return;
		}
		IEEE80211_ADDR_COPY(mp, enm->enm_addrlo);
		mp += IEEE80211_ADDR_LEN, nmc++;
		ETHER_NEXT_MULTI(estep, enm);
	}
	ifp->if_flags &= ~IFF_ALLMULTI;
	mwl_hal_setmcast(sc->sc_mh, nmc, macs);
#endif
}

static int
mwl_mode_init(struct mwl_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct mwl_hal *mh = sc->sc_mh;

	mwl_hal_setpromisc(mh, ic->ic_promisc > 0);
	mwl_setmcastfilter(sc);

	return 0;
}

/*
 * Callback from the 802.11 layer after a multicast state change.
 */
static void
mwl_update_mcast(struct ieee80211com *ic)
{
	struct mwl_softc *sc = ic->ic_softc;

	mwl_setmcastfilter(sc);
}

/*
 * Callback from the 802.11 layer after a promiscuous mode change.
 * Note this interface does not check the operating mode as this
 * is an internal callback and we are expected to honor the current
 * state (e.g. this is used for setting the interface in promiscuous
 * mode when operating in hostap mode to do ACS).
 */
static void
mwl_update_promisc(struct ieee80211com *ic)
{
	struct mwl_softc *sc = ic->ic_softc;

	mwl_hal_setpromisc(sc->sc_mh, ic->ic_promisc > 0);
}

/*
 * Callback from the 802.11 layer to update the slot time
 * based on the current setting.  We use it to notify the
 * firmware of ERP changes and the f/w takes care of things
 * like slot time and preamble.
 */
static void
mwl_updateslot(struct ieee80211com *ic)
{
	struct mwl_softc *sc = ic->ic_softc;
	struct mwl_hal *mh = sc->sc_mh;
	int prot;

	/* NB: can be called early; suppress needless cmds */
	if (!sc->sc_running)
		return;

	/*
	 * Calculate the ERP flags.  The firwmare will use
	 * this to carry out the appropriate measures.
	 */
	prot = 0;
	if (IEEE80211_IS_CHAN_ANYG(ic->ic_curchan)) {
		if ((ic->ic_flags & IEEE80211_F_SHSLOT) == 0)
			prot |= IEEE80211_ERP_NON_ERP_PRESENT;
		if (ic->ic_flags & IEEE80211_F_USEPROT)
			prot |= IEEE80211_ERP_USE_PROTECTION;
		if (ic->ic_flags & IEEE80211_F_USEBARKER)
			prot |= IEEE80211_ERP_LONG_PREAMBLE;
	}

	DPRINTF(sc, MWL_DEBUG_RESET,
	    "%s: chan %u MHz/flags 0x%x %s slot, (prot 0x%x ic_flags 0x%x)\n",
	    __func__, ic->ic_curchan->ic_freq, ic->ic_curchan->ic_flags,
	    ic->ic_flags & IEEE80211_F_SHSLOT ? "short" : "long", prot,
	    ic->ic_flags);

	mwl_hal_setgprot(mh, prot);
}

/*
 * Setup the beacon frame.
 */
static int
mwl_beacon_setup(struct ieee80211vap *vap)
{
	struct mwl_hal_vap *hvap = MWL_VAP(vap)->mv_hvap;
	struct ieee80211_node *ni = vap->iv_bss;
	struct mbuf *m;

	m = ieee80211_beacon_alloc(ni);
	if (m == NULL)
		return ENOBUFS;
	mwl_hal_setbeacon(hvap, mtod(m, const void *), m->m_len);
	m_free(m);

	return 0;
}

/*
 * Update the beacon frame in response to a change.
 */
static void
mwl_beacon_update(struct ieee80211vap *vap, int item)
{
	struct mwl_hal_vap *hvap = MWL_VAP(vap)->mv_hvap;
	struct ieee80211com *ic = vap->iv_ic;

	KASSERT(hvap != NULL, ("no beacon"));
	switch (item) {
	case IEEE80211_BEACON_ERP:
		mwl_updateslot(ic);
		break;
	case IEEE80211_BEACON_HTINFO:
		mwl_hal_setnprotmode(hvap,
		    MS(ic->ic_curhtprotmode, IEEE80211_HTINFO_OPMODE));
		break;
	case IEEE80211_BEACON_CAPS:
	case IEEE80211_BEACON_WME:
	case IEEE80211_BEACON_APPIE:
	case IEEE80211_BEACON_CSA:
		break;
	case IEEE80211_BEACON_TIM:
		/* NB: firmware always forms TIM */
		return;
	}
	/* XXX retain beacon frame and update */
	mwl_beacon_setup(vap);
}

static void
mwl_load_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	bus_addr_t *paddr = (bus_addr_t*) arg;
	KASSERT(error == 0, ("error %u on bus_dma callback", error));
	*paddr = segs->ds_addr;
}

#ifdef MWL_HOST_PS_SUPPORT
/*
 * Handle power save station occupancy changes.
 */
static void
mwl_update_ps(struct ieee80211vap *vap, int nsta)
{
	struct mwl_vap *mvp = MWL_VAP(vap);

	if (nsta == 0 || mvp->mv_last_ps_sta == 0)
		mwl_hal_setpowersave_bss(mvp->mv_hvap, nsta);
	mvp->mv_last_ps_sta = nsta;
}

/*
 * Handle associated station power save state changes.
 */
static int
mwl_set_tim(struct ieee80211_node *ni, int set)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct mwl_vap *mvp = MWL_VAP(vap);

	if (mvp->mv_set_tim(ni, set)) {		/* NB: state change */
		mwl_hal_setpowersave_sta(mvp->mv_hvap,
		    IEEE80211_AID(ni->ni_associd), set);
		return 1;
	} else
		return 0;
}
#endif /* MWL_HOST_PS_SUPPORT */

static int
mwl_desc_setup(struct mwl_softc *sc, const char *name,
	struct mwl_descdma *dd,
	int nbuf, size_t bufsize, int ndesc, size_t descsize)
{
	uint8_t *ds;
	int error;

	DPRINTF(sc, MWL_DEBUG_RESET,
	    "%s: %s DMA: %u bufs (%ju) %u desc/buf (%ju)\n",
	    __func__, name, nbuf, (uintmax_t) bufsize,
	    ndesc, (uintmax_t) descsize);

	dd->dd_name = name;
	dd->dd_desc_len = nbuf * ndesc * descsize;

	/*
	 * Setup DMA descriptor area.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev),	/* parent */
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
		device_printf(sc->sc_dev, "cannot allocate %s DMA tag\n", dd->dd_name);
		return error;
	}

	/* allocate descriptors */
	error = bus_dmamem_alloc(dd->dd_dmat, (void**) &dd->dd_desc,
				 BUS_DMA_NOWAIT | BUS_DMA_COHERENT, 
				 &dd->dd_dmamap);
	if (error != 0) {
		device_printf(sc->sc_dev, "unable to alloc memory for %u %s descriptors, "
			"error %u\n", nbuf * ndesc, dd->dd_name, error);
		goto fail1;
	}

	error = bus_dmamap_load(dd->dd_dmat, dd->dd_dmamap,
				dd->dd_desc, dd->dd_desc_len,
				mwl_load_cb, &dd->dd_desc_paddr,
				BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sc_dev, "unable to map %s descriptors, error %u\n",
			dd->dd_name, error);
		goto fail2;
	}

	ds = dd->dd_desc;
	memset(ds, 0, dd->dd_desc_len);
	DPRINTF(sc, MWL_DEBUG_RESET,
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
#undef DS2PHYS
}

static void
mwl_desc_cleanup(struct mwl_softc *sc, struct mwl_descdma *dd)
{
	bus_dmamap_unload(dd->dd_dmat, dd->dd_dmamap);
	bus_dmamem_free(dd->dd_dmat, dd->dd_desc, dd->dd_dmamap);
	bus_dma_tag_destroy(dd->dd_dmat);

	memset(dd, 0, sizeof(*dd));
}

/* 
 * Construct a tx q's free list.  The order of entries on
 * the list must reflect the physical layout of tx descriptors
 * because the firmware pre-fetches descriptors.
 *
 * XXX might be better to use indices into the buffer array.
 */
static void
mwl_txq_reset(struct mwl_softc *sc, struct mwl_txq *txq)
{
	struct mwl_txbuf *bf;
	int i;

	bf = txq->dma.dd_bufptr;
	STAILQ_INIT(&txq->free);
	for (i = 0; i < mwl_txbuf; i++, bf++)
		STAILQ_INSERT_TAIL(&txq->free, bf, bf_list);
	txq->nfree = i;
}

#define	DS2PHYS(_dd, _ds) \
	((_dd)->dd_desc_paddr + ((caddr_t)(_ds) - (caddr_t)(_dd)->dd_desc))

static int
mwl_txdma_setup(struct mwl_softc *sc, struct mwl_txq *txq)
{
	int error, bsize, i;
	struct mwl_txbuf *bf;
	struct mwl_txdesc *ds;

	error = mwl_desc_setup(sc, "tx", &txq->dma,
			mwl_txbuf, sizeof(struct mwl_txbuf),
			MWL_TXDESC, sizeof(struct mwl_txdesc));
	if (error != 0)
		return error;

	/* allocate and setup tx buffers */
	bsize = mwl_txbuf * sizeof(struct mwl_txbuf);
	bf = malloc(bsize, M_MWLDEV, M_NOWAIT | M_ZERO);
	if (bf == NULL) {
		device_printf(sc->sc_dev, "malloc of %u tx buffers failed\n",
			mwl_txbuf);
		return ENOMEM;
	}
	txq->dma.dd_bufptr = bf;

	ds = txq->dma.dd_desc;
	for (i = 0; i < mwl_txbuf; i++, bf++, ds += MWL_TXDESC) {
		bf->bf_desc = ds;
		bf->bf_daddr = DS2PHYS(&txq->dma, ds);
		error = bus_dmamap_create(sc->sc_dmat, BUS_DMA_NOWAIT,
				&bf->bf_dmamap);
		if (error != 0) {
			device_printf(sc->sc_dev, "unable to create dmamap for tx "
				"buffer %u, error %u\n", i, error);
			return error;
		}
	}
	mwl_txq_reset(sc, txq);
	return 0;
}

static void
mwl_txdma_cleanup(struct mwl_softc *sc, struct mwl_txq *txq)
{
	struct mwl_txbuf *bf;
	int i;

	bf = txq->dma.dd_bufptr;
	for (i = 0; i < mwl_txbuf; i++, bf++) {
		KASSERT(bf->bf_m == NULL, ("mbuf on free list"));
		KASSERT(bf->bf_node == NULL, ("node on free list"));
		if (bf->bf_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_dmat, bf->bf_dmamap);
	}
	STAILQ_INIT(&txq->free);
	txq->nfree = 0;
	if (txq->dma.dd_bufptr != NULL) {
		free(txq->dma.dd_bufptr, M_MWLDEV);
		txq->dma.dd_bufptr = NULL;
	}
	if (txq->dma.dd_desc_len != 0)
		mwl_desc_cleanup(sc, &txq->dma);
}

static int
mwl_rxdma_setup(struct mwl_softc *sc)
{
	int error, jumbosize, bsize, i;
	struct mwl_rxbuf *bf;
	struct mwl_jumbo *rbuf;
	struct mwl_rxdesc *ds;
	caddr_t data;

	error = mwl_desc_setup(sc, "rx", &sc->sc_rxdma,
			mwl_rxdesc, sizeof(struct mwl_rxbuf),
			1, sizeof(struct mwl_rxdesc));
	if (error != 0)
		return error;

	/*
	 * Receive is done to a private pool of jumbo buffers.
	 * This allows us to attach to mbuf's and avoid re-mapping
	 * memory on each rx we post.  We allocate a large chunk
	 * of memory and manage it in the driver.  The mbuf free
	 * callback method is used to reclaim frames after sending
	 * them up the stack.  By default we allocate 2x the number of
	 * rx descriptors configured so we have some slop to hold
	 * us while frames are processed.
	 */
	if (mwl_rxbuf < 2*mwl_rxdesc) {
		device_printf(sc->sc_dev,
		    "too few rx dma buffers (%d); increasing to %d\n",
		    mwl_rxbuf, 2*mwl_rxdesc);
		mwl_rxbuf = 2*mwl_rxdesc;
	}
	jumbosize = roundup(MWL_AGGR_SIZE, PAGE_SIZE);
	sc->sc_rxmemsize = mwl_rxbuf*jumbosize;

	error = bus_dma_tag_create(sc->sc_dmat,	/* parent */
		       PAGE_SIZE, 0,		/* alignment, bounds */
		       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		       BUS_SPACE_MAXADDR,	/* highaddr */
		       NULL, NULL,		/* filter, filterarg */
		       sc->sc_rxmemsize,	/* maxsize */
		       1,			/* nsegments */
		       sc->sc_rxmemsize,	/* maxsegsize */
		       BUS_DMA_ALLOCNOW,	/* flags */
		       NULL,			/* lockfunc */
		       NULL,			/* lockarg */
		       &sc->sc_rxdmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create rx DMA tag\n");
		return error;
	}

	error = bus_dmamem_alloc(sc->sc_rxdmat, (void**) &sc->sc_rxmem,
				 BUS_DMA_NOWAIT | BUS_DMA_COHERENT, 
				 &sc->sc_rxmap);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not alloc %ju bytes of rx DMA memory\n",
		    (uintmax_t) sc->sc_rxmemsize);
		return error;
	}

	error = bus_dmamap_load(sc->sc_rxdmat, sc->sc_rxmap,
				sc->sc_rxmem, sc->sc_rxmemsize,
				mwl_load_cb, &sc->sc_rxmem_paddr,
				BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not load rx DMA map\n");
		return error;
	}

	/*
	 * Allocate rx buffers and set them up.
	 */
	bsize = mwl_rxdesc * sizeof(struct mwl_rxbuf);
	bf = malloc(bsize, M_MWLDEV, M_NOWAIT | M_ZERO);
	if (bf == NULL) {
		device_printf(sc->sc_dev, "malloc of %u rx buffers failed\n", bsize);
		return error;
	}
	sc->sc_rxdma.dd_bufptr = bf;

	STAILQ_INIT(&sc->sc_rxbuf);
	ds = sc->sc_rxdma.dd_desc;
	for (i = 0; i < mwl_rxdesc; i++, bf++, ds++) {
		bf->bf_desc = ds;
		bf->bf_daddr = DS2PHYS(&sc->sc_rxdma, ds);
		/* pre-assign dma buffer */
		bf->bf_data = ((uint8_t *)sc->sc_rxmem) + (i*jumbosize);
		/* NB: tail is intentional to preserve descriptor order */
		STAILQ_INSERT_TAIL(&sc->sc_rxbuf, bf, bf_list);
	}

	/*
	 * Place remainder of dma memory buffers on the free list.
	 */
	SLIST_INIT(&sc->sc_rxfree);
	for (; i < mwl_rxbuf; i++) {
		data = ((uint8_t *)sc->sc_rxmem) + (i*jumbosize);
		rbuf = MWL_JUMBO_DATA2BUF(data);
		SLIST_INSERT_HEAD(&sc->sc_rxfree, rbuf, next);
		sc->sc_nrxfree++;
	}
	return 0;
}
#undef DS2PHYS

static void
mwl_rxdma_cleanup(struct mwl_softc *sc)
{
	if (sc->sc_rxmem_paddr != 0) {
		bus_dmamap_unload(sc->sc_rxdmat, sc->sc_rxmap);
		sc->sc_rxmem_paddr = 0;
	}
	if (sc->sc_rxmem != NULL) {
		bus_dmamem_free(sc->sc_rxdmat, sc->sc_rxmem, sc->sc_rxmap);
		sc->sc_rxmem = NULL;
	}
	if (sc->sc_rxdma.dd_bufptr != NULL) {
		free(sc->sc_rxdma.dd_bufptr, M_MWLDEV);
		sc->sc_rxdma.dd_bufptr = NULL;
	}
	if (sc->sc_rxdma.dd_desc_len != 0)
		mwl_desc_cleanup(sc, &sc->sc_rxdma);
}

static int
mwl_dma_setup(struct mwl_softc *sc)
{
	int error, i;

	error = mwl_rxdma_setup(sc);
	if (error != 0) {
		mwl_rxdma_cleanup(sc);
		return error;
	}

	for (i = 0; i < MWL_NUM_TX_QUEUES; i++) {
		error = mwl_txdma_setup(sc, &sc->sc_txq[i]);
		if (error != 0) {
			mwl_dma_cleanup(sc);
			return error;
		}
	}
	return 0;
}

static void
mwl_dma_cleanup(struct mwl_softc *sc)
{
	int i;

	for (i = 0; i < MWL_NUM_TX_QUEUES; i++)
		mwl_txdma_cleanup(sc, &sc->sc_txq[i]);
	mwl_rxdma_cleanup(sc);
}

static struct ieee80211_node *
mwl_node_alloc(struct ieee80211vap *vap, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct ieee80211com *ic = vap->iv_ic;
	struct mwl_softc *sc = ic->ic_softc;
	const size_t space = sizeof(struct mwl_node);
	struct mwl_node *mn;

	mn = malloc(space, M_80211_NODE, M_NOWAIT|M_ZERO);
	if (mn == NULL) {
		/* XXX stat+msg */
		return NULL;
	}
	DPRINTF(sc, MWL_DEBUG_NODE, "%s: mn %p\n", __func__, mn);
	return &mn->mn_node;
}

static void
mwl_node_cleanup(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
        struct mwl_softc *sc = ic->ic_softc;
	struct mwl_node *mn = MWL_NODE(ni);

	DPRINTF(sc, MWL_DEBUG_NODE, "%s: ni %p ic %p staid %d\n",
	    __func__, ni, ni->ni_ic, mn->mn_staid);

	if (mn->mn_staid != 0) {
		struct ieee80211vap *vap = ni->ni_vap;

		if (mn->mn_hvap != NULL) {
			if (vap->iv_opmode == IEEE80211_M_STA)
				mwl_hal_delstation(mn->mn_hvap, vap->iv_myaddr);
			else
				mwl_hal_delstation(mn->mn_hvap, ni->ni_macaddr);
		}
		/*
		 * NB: legacy WDS peer sta db entry is installed using
		 * the associate ap's hvap; use it again to delete it.
		 * XXX can vap be NULL?
		 */
		else if (vap->iv_opmode == IEEE80211_M_WDS &&
		    MWL_VAP(vap)->mv_ap_hvap != NULL)
			mwl_hal_delstation(MWL_VAP(vap)->mv_ap_hvap,
			    ni->ni_macaddr);
		delstaid(sc, mn->mn_staid);
		mn->mn_staid = 0;
	}
	sc->sc_node_cleanup(ni);
}

/*
 * Reclaim rx dma buffers from packets sitting on the ampdu
 * reorder queue for a station.  We replace buffers with a
 * system cluster (if available).
 */
static void
mwl_ampdu_rxdma_reclaim(struct ieee80211_rx_ampdu *rap)
{
#if 0
	int i, n, off;
	struct mbuf *m;
	void *cl;

	n = rap->rxa_qframes;
	for (i = 0; i < rap->rxa_wnd && n > 0; i++) {
		m = rap->rxa_m[i];
		if (m == NULL)
			continue;
		n--;
		/* our dma buffers have a well-known free routine */
		if ((m->m_flags & M_EXT) == 0 ||
		    m->m_ext.ext_free != mwl_ext_free)
			continue;
		/*
		 * Try to allocate a cluster and move the data.
		 */
		off = m->m_data - m->m_ext.ext_buf;
		if (off + m->m_pkthdr.len > MCLBYTES) {
			/* XXX no AMSDU for now */
			continue;
		}
		cl = pool_cache_get_paddr(&mclpool_cache, 0,
		    &m->m_ext.ext_paddr);
		if (cl != NULL) {
			/*
			 * Copy the existing data to the cluster, remove
			 * the rx dma buffer, and attach the cluster in
			 * its place.  Note we preserve the offset to the
			 * data so frames being bridged can still prepend
			 * their headers without adding another mbuf.
			 */
			memcpy((caddr_t) cl + off, m->m_data, m->m_pkthdr.len);
			MEXTREMOVE(m);
			MEXTADD(m, cl, MCLBYTES, 0, NULL, &mclpool_cache);
			/* setup mbuf like _MCLGET does */
			m->m_flags |= M_CLUSTER | M_EXT_RW;
			_MOWNERREF(m, M_EXT | M_CLUSTER);
			/* NB: m_data is clobbered by MEXTADDR, adjust */
			m->m_data += off;
		}
	}
#endif
}

/*
 * Callback to reclaim resources.  We first let the
 * net80211 layer do it's thing, then if we are still
 * blocked by a lack of rx dma buffers we walk the ampdu
 * reorder q's to reclaim buffers by copying to a system
 * cluster.
 */
static void
mwl_node_drain(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
        struct mwl_softc *sc = ic->ic_softc;
	struct mwl_node *mn = MWL_NODE(ni);

	DPRINTF(sc, MWL_DEBUG_NODE, "%s: ni %p vap %p staid %d\n",
	    __func__, ni, ni->ni_vap, mn->mn_staid);

	/* NB: call up first to age out ampdu q's */
	sc->sc_node_drain(ni);

	/* XXX better to not check low water mark? */
	if (sc->sc_rxblocked && mn->mn_staid != 0 &&
	    (ni->ni_flags & IEEE80211_NODE_HT)) {
		uint8_t tid;
		/*
		 * Walk the reorder q and reclaim rx dma buffers by copying
		 * the packet contents into clusters.
		 */
		for (tid = 0; tid < WME_NUM_TID; tid++) {
			struct ieee80211_rx_ampdu *rap;

			rap = &ni->ni_rx_ampdu[tid];
			if ((rap->rxa_flags & IEEE80211_AGGR_XCHGPEND) == 0)
				continue;
			if (rap->rxa_qframes)
				mwl_ampdu_rxdma_reclaim(rap);
		}
	}
}

static void
mwl_node_getsignal(const struct ieee80211_node *ni, int8_t *rssi, int8_t *noise)
{
	*rssi = ni->ni_ic->ic_node_getrssi(ni);
#ifdef MWL_ANT_INFO_SUPPORT
#if 0
	/* XXX need to smooth data */
	*noise = -MWL_NODE_CONST(ni)->mn_ai.nf;
#else
	*noise = -95;		/* XXX */
#endif
#else
	*noise = -95;		/* XXX */
#endif
}

/*
 * Convert Hardware per-antenna rssi info to common format:
 * Let a1, a2, a3 represent the amplitudes per chain
 * Let amax represent max[a1, a2, a3]
 * Rssi1_dBm = RSSI_dBm + 20*log10(a1/amax)
 * Rssi1_dBm = RSSI_dBm + 20*log10(a1) - 20*log10(amax)
 * We store a table that is 4*20*log10(idx) - the extra 4 is to store or
 * maintain some extra precision.
 *
 * Values are stored in .5 db format capped at 127.
 */
static void
mwl_node_getmimoinfo(const struct ieee80211_node *ni,
	struct ieee80211_mimo_info *mi)
{
#define	CVT(_dst, _src) do {						\
	(_dst) = rssi + ((logdbtbl[_src] - logdbtbl[rssi_max]) >> 2);	\
	(_dst) = (_dst) > 64 ? 127 : ((_dst) << 1);			\
} while (0)
	static const int8_t logdbtbl[32] = {
	       0,   0,  24,  38,  48,  56,  62,  68, 
	      72,  76,  80,  83,  86,  89,  92,  94, 
	      96,  98, 100, 102, 104, 106, 107, 109, 
	     110, 112, 113, 115, 116, 117, 118, 119
	};
	const struct mwl_node *mn = MWL_NODE_CONST(ni);
	uint8_t rssi = mn->mn_ai.rsvd1/2;		/* XXX */
	uint32_t rssi_max;

	rssi_max = mn->mn_ai.rssi_a;
	if (mn->mn_ai.rssi_b > rssi_max)
		rssi_max = mn->mn_ai.rssi_b;
	if (mn->mn_ai.rssi_c > rssi_max)
		rssi_max = mn->mn_ai.rssi_c;

	CVT(mi->ch[0].rssi[0], mn->mn_ai.rssi_a);
	CVT(mi->ch[1].rssi[0], mn->mn_ai.rssi_b);
	CVT(mi->ch[2].rssi[0], mn->mn_ai.rssi_c);

	mi->ch[0].noise[0] = mn->mn_ai.nf_a;
	mi->ch[1].noise[0] = mn->mn_ai.nf_b;
	mi->ch[2].noise[0] = mn->mn_ai.nf_c;
#undef CVT
}

static __inline void *
mwl_getrxdma(struct mwl_softc *sc)
{
	struct mwl_jumbo *buf;
	void *data;

	/*
	 * Allocate from jumbo pool.
	 */
	MWL_RXFREE_LOCK(sc);
	buf = SLIST_FIRST(&sc->sc_rxfree);
	if (buf == NULL) {
		DPRINTF(sc, MWL_DEBUG_ANY,
		    "%s: out of rx dma buffers\n", __func__);
		sc->sc_stats.mst_rx_nodmabuf++;
		data = NULL;
	} else {
		SLIST_REMOVE_HEAD(&sc->sc_rxfree, next);
		sc->sc_nrxfree--;
		data = MWL_JUMBO_BUF2DATA(buf);
	}
	MWL_RXFREE_UNLOCK(sc);
	return data;
}

static __inline void
mwl_putrxdma(struct mwl_softc *sc, void *data)
{
	struct mwl_jumbo *buf;

	/* XXX bounds check data */
	MWL_RXFREE_LOCK(sc);
	buf = MWL_JUMBO_DATA2BUF(data);
	SLIST_INSERT_HEAD(&sc->sc_rxfree, buf, next);
	sc->sc_nrxfree++;
	MWL_RXFREE_UNLOCK(sc);
}

static int
mwl_rxbuf_init(struct mwl_softc *sc, struct mwl_rxbuf *bf)
{
	struct mwl_rxdesc *ds;

	ds = bf->bf_desc;
	if (bf->bf_data == NULL) {
		bf->bf_data = mwl_getrxdma(sc);
		if (bf->bf_data == NULL) {
			/* mark descriptor to be skipped */
			ds->RxControl = EAGLE_RXD_CTRL_OS_OWN;
			/* NB: don't need PREREAD */
			MWL_RXDESC_SYNC(sc, ds, BUS_DMASYNC_PREWRITE);
			sc->sc_stats.mst_rxbuf_failed++;
			return ENOMEM;
		}
	}
	/*
	 * NB: DMA buffer contents is known to be unmodified
	 *     so there's no need to flush the data cache.
	 */

	/*
	 * Setup descriptor.
	 */
	ds->QosCtrl = 0;
	ds->RSSI = 0;
	ds->Status = EAGLE_RXD_STATUS_IDLE;
	ds->Channel = 0;
	ds->PktLen = htole16(MWL_AGGR_SIZE);
	ds->SQ2 = 0;
	ds->pPhysBuffData = htole32(MWL_JUMBO_DMA_ADDR(sc, bf->bf_data));
	/* NB: don't touch pPhysNext, set once */
	ds->RxControl = EAGLE_RXD_CTRL_DRIVER_OWN;
	MWL_RXDESC_SYNC(sc, ds, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return 0;
}

static void
mwl_ext_free(struct mbuf *m)
{
	struct mwl_softc *sc = m->m_ext.ext_arg1;

	/* XXX bounds check data */
	mwl_putrxdma(sc, m->m_ext.ext_buf);
	/*
	 * If we were previously blocked by a lack of rx dma buffers
	 * check if we now have enough to restart rx interrupt handling.
	 * NB: we know we are called at splvm which is above splnet.
	 */
	if (sc->sc_rxblocked && sc->sc_nrxfree > mwl_rxdmalow) {
		sc->sc_rxblocked = 0;
		mwl_hal_intrset(sc->sc_mh, sc->sc_imask);
	}
}

struct mwl_frame_bar {
	u_int8_t	i_fc[2];
	u_int8_t	i_dur[2];
	u_int8_t	i_ra[IEEE80211_ADDR_LEN];
	u_int8_t	i_ta[IEEE80211_ADDR_LEN];
	/* ctl, seq, FCS */
} __packed;

/*
 * Like ieee80211_anyhdrsize, but handles BAR frames
 * specially so the logic below to piece the 802.11
 * header together works.
 */
static __inline int
mwl_anyhdrsize(const void *data)
{
	const struct ieee80211_frame *wh = data;

	if ((wh->i_fc[0]&IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_CTL) {
		switch (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
		case IEEE80211_FC0_SUBTYPE_CTS:
		case IEEE80211_FC0_SUBTYPE_ACK:
			return sizeof(struct ieee80211_frame_ack);
		case IEEE80211_FC0_SUBTYPE_BAR:
			return sizeof(struct mwl_frame_bar);
		}
		return sizeof(struct ieee80211_frame_min);
	} else
		return ieee80211_hdrsize(data);
}

static void
mwl_handlemicerror(struct ieee80211com *ic, const uint8_t *data)
{
	const struct ieee80211_frame *wh;
	struct ieee80211_node *ni;

	wh = (const struct ieee80211_frame *)(data + sizeof(uint16_t));
	ni = ieee80211_find_rxnode(ic, (const struct ieee80211_frame_min *) wh);
	if (ni != NULL) {
		ieee80211_notify_michael_failure(ni->ni_vap, wh, 0);
		ieee80211_free_node(ni);
	}
}

/*
 * Convert hardware signal strength to rssi.  The value
 * provided by the device has the noise floor added in;
 * we need to compensate for this but we don't have that
 * so we use a fixed value.
 *
 * The offset of 8 is good for both 2.4 and 5GHz.  The LNA
 * offset is already set as part of the initial gain.  This
 * will give at least +/- 3dB for 2.4GHz and +/- 5dB for 5GHz.
 */
static __inline int
cvtrssi(uint8_t ssi)
{
	int rssi = (int) ssi + 8;
	/* XXX hack guess until we have a real noise floor */
	rssi = 2*(87 - rssi);	/* NB: .5 dBm units */
	return (rssi < 0 ? 0 : rssi > 127 ? 127 : rssi);
}

static void
mwl_rx_proc(void *arg, int npending)
{
	struct mwl_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct mwl_rxbuf *bf;
	struct mwl_rxdesc *ds;
	struct mbuf *m;
	struct ieee80211_qosframe *wh;
	struct ieee80211_node *ni;
	struct mwl_node *mn;
	int off, len, hdrlen, pktlen, rssi, ntodo;
	uint8_t *data, status;
	void *newdata;
	int16_t nf;

	DPRINTF(sc, MWL_DEBUG_RX_PROC, "%s: pending %u rdptr 0x%x wrptr 0x%x\n",
	    __func__, npending, RD4(sc, sc->sc_hwspecs.rxDescRead),
	    RD4(sc, sc->sc_hwspecs.rxDescWrite));
	nf = -96;			/* XXX */
	bf = sc->sc_rxnext;
	for (ntodo = mwl_rxquota; ntodo > 0; ntodo--) {
		if (bf == NULL)
			bf = STAILQ_FIRST(&sc->sc_rxbuf);
		ds = bf->bf_desc;
		data = bf->bf_data;
		if (data == NULL) {
			/*
			 * If data allocation failed previously there
			 * will be no buffer; try again to re-populate it.
			 * Note the firmware will not advance to the next
			 * descriptor with a dma buffer so we must mimic
			 * this or we'll get out of sync.
			 */ 
			DPRINTF(sc, MWL_DEBUG_ANY,
			    "%s: rx buf w/o dma memory\n", __func__);
			(void) mwl_rxbuf_init(sc, bf);
			sc->sc_stats.mst_rx_dmabufmissing++;
			break;
		}
		MWL_RXDESC_SYNC(sc, ds,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		if (ds->RxControl != EAGLE_RXD_CTRL_DMA_OWN)
			break;
#ifdef MWL_DEBUG
		if (sc->sc_debug & MWL_DEBUG_RECV_DESC)
			mwl_printrxbuf(bf, 0);
#endif
		status = ds->Status;
		if (status & EAGLE_RXD_STATUS_DECRYPT_ERR_MASK) {
			counter_u64_add(ic->ic_ierrors, 1);
			sc->sc_stats.mst_rx_crypto++;
			/*
			 * NB: Check EAGLE_RXD_STATUS_GENERAL_DECRYPT_ERR
			 *     for backwards compatibility.
			 */
			if (status != EAGLE_RXD_STATUS_GENERAL_DECRYPT_ERR &&
			    (status & EAGLE_RXD_STATUS_TKIP_MIC_DECRYPT_ERR)) {
				/*
				 * MIC error, notify upper layers.
				 */
				bus_dmamap_sync(sc->sc_rxdmat, sc->sc_rxmap,
				    BUS_DMASYNC_POSTREAD);
				mwl_handlemicerror(ic, data);
				sc->sc_stats.mst_rx_tkipmic++;
			}
			/* XXX too painful to tap packets */
			goto rx_next;
		}
		/*
		 * Sync the data buffer.
		 */
		len = le16toh(ds->PktLen);
		bus_dmamap_sync(sc->sc_rxdmat, sc->sc_rxmap, BUS_DMASYNC_POSTREAD);
		/*
		 * The 802.11 header is provided all or in part at the front;
		 * use it to calculate the true size of the header that we'll
		 * construct below.  We use this to figure out where to copy
		 * payload prior to constructing the header.
		 */
		hdrlen = mwl_anyhdrsize(data + sizeof(uint16_t));
		off = sizeof(uint16_t) + sizeof(struct ieee80211_frame_addr4);

		/* calculate rssi early so we can re-use for each aggregate */
		rssi = cvtrssi(ds->RSSI);

		pktlen = hdrlen + (len - off);
		/*
		 * NB: we know our frame is at least as large as
		 * IEEE80211_MIN_LEN because there is a 4-address
		 * frame at the front.  Hence there's no need to
		 * vet the packet length.  If the frame in fact
		 * is too small it should be discarded at the
		 * net80211 layer.
		 */

		/*
		 * Attach dma buffer to an mbuf.  We tried
		 * doing this based on the packet size (i.e.
		 * copying small packets) but it turns out to
		 * be a net loss.  The tradeoff might be system
		 * dependent (cache architecture is important).
		 */
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (m == NULL) {
			DPRINTF(sc, MWL_DEBUG_ANY,
			    "%s: no rx mbuf\n", __func__);
			sc->sc_stats.mst_rx_nombuf++;
			goto rx_next;
		}
		/*
		 * Acquire the replacement dma buffer before
		 * processing the frame.  If we're out of dma
		 * buffers we disable rx interrupts and wait
		 * for the free pool to reach mlw_rxdmalow buffers
		 * before starting to do work again.  If the firmware
		 * runs out of descriptors then it will toss frames
		 * which is better than our doing it as that can
		 * starve our processing.  It is also important that
		 * we always process rx'd frames in case they are
		 * A-MPDU as otherwise the host's view of the BA
		 * window may get out of sync with the firmware.
		 */
		newdata = mwl_getrxdma(sc);
		if (newdata == NULL) {
			/* NB: stat+msg in mwl_getrxdma */
			m_free(m);
			/* disable RX interrupt and mark state */
			mwl_hal_intrset(sc->sc_mh,
			    sc->sc_imask &~ MACREG_A2HRIC_BIT_RX_RDY);
			sc->sc_rxblocked = 1;
			ieee80211_drain(ic);
			/* XXX check rxblocked and immediately start again? */
			goto rx_stop;
		}
		bf->bf_data = newdata;
		/*
		 * Attach the dma buffer to the mbuf;
		 * mwl_rxbuf_init will re-setup the rx
		 * descriptor using the replacement dma
		 * buffer we just installed above.
		 */
		m_extadd(m, data, MWL_AGGR_SIZE, mwl_ext_free, sc, NULL, 0,
		    EXT_NET_DRV);
		m->m_data += off - hdrlen;
		m->m_pkthdr.len = m->m_len = pktlen;
		/* NB: dma buffer assumed read-only */

		/*
		 * Piece 802.11 header together.
		 */
		wh = mtod(m, struct ieee80211_qosframe *);
		/* NB: don't need to do this sometimes but ... */
		/* XXX special case so we can memcpy after m_devget? */
		ovbcopy(data + sizeof(uint16_t), wh, hdrlen);
		if (IEEE80211_QOS_HAS_SEQ(wh))
			*(uint16_t *)ieee80211_getqos(wh) = ds->QosCtrl;
		/*
		 * The f/w strips WEP header but doesn't clear
		 * the WEP bit; mark the packet with M_WEP so
		 * net80211 will treat the data as decrypted.
		 * While here also clear the PWR_MGT bit since
		 * power save is handled by the firmware and
		 * passing this up will potentially cause the
		 * upper layer to put a station in power save
		 * (except when configured with MWL_HOST_PS_SUPPORT).
		 */
		if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED)
			m->m_flags |= M_WEP;
#ifdef MWL_HOST_PS_SUPPORT
		wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
#else
		wh->i_fc[1] &= ~(IEEE80211_FC1_PROTECTED |
		    IEEE80211_FC1_PWR_MGT);
#endif

		if (ieee80211_radiotap_active(ic)) {
			struct mwl_rx_radiotap_header *tap = &sc->sc_rx_th;

			tap->wr_flags = 0;
			tap->wr_rate = ds->Rate;
			tap->wr_antsignal = rssi + nf;
			tap->wr_antnoise = nf;
		}
		if (IFF_DUMPPKTS_RECV(sc, wh)) {
			ieee80211_dump_pkt(ic, mtod(m, caddr_t),
			    len, ds->Rate, rssi);
		}
		/* dispatch */
		ni = ieee80211_find_rxnode(ic,
		    (const struct ieee80211_frame_min *) wh);
		if (ni != NULL) {
			mn = MWL_NODE(ni);
#ifdef MWL_ANT_INFO_SUPPORT
			mn->mn_ai.rssi_a = ds->ai.rssi_a;
			mn->mn_ai.rssi_b = ds->ai.rssi_b;
			mn->mn_ai.rssi_c = ds->ai.rssi_c;
			mn->mn_ai.rsvd1 = rssi;
#endif
			/* tag AMPDU aggregates for reorder processing */
			if (ni->ni_flags & IEEE80211_NODE_HT)
				m->m_flags |= M_AMPDU;
			(void) ieee80211_input(ni, m, rssi, nf);
			ieee80211_free_node(ni);
		} else
			(void) ieee80211_input_all(ic, m, rssi, nf);
rx_next:
		/* NB: ignore ENOMEM so we process more descriptors */
		(void) mwl_rxbuf_init(sc, bf);
		bf = STAILQ_NEXT(bf, bf_list);
	}
rx_stop:
	sc->sc_rxnext = bf;

	if (mbufq_first(&sc->sc_snd) != NULL) {
		/* NB: kick fw; the tx thread may have been preempted */
		mwl_hal_txstart(sc->sc_mh, 0);
		mwl_start(sc);
	}
}

static void
mwl_txq_init(struct mwl_softc *sc, struct mwl_txq *txq, int qnum)
{
	struct mwl_txbuf *bf, *bn;
	struct mwl_txdesc *ds;

	MWL_TXQ_LOCK_INIT(sc, txq);
	txq->qnum = qnum;
	txq->txpri = 0;	/* XXX */
#if 0
	/* NB: q setup by mwl_txdma_setup XXX */
	STAILQ_INIT(&txq->free);
#endif
	STAILQ_FOREACH(bf, &txq->free, bf_list) {
		bf->bf_txq = txq;

		ds = bf->bf_desc;
		bn = STAILQ_NEXT(bf, bf_list);
		if (bn == NULL)
			bn = STAILQ_FIRST(&txq->free);
		ds->pPhysNext = htole32(bn->bf_daddr);
	}
	STAILQ_INIT(&txq->active);
}

/*
 * Setup a hardware data transmit queue for the specified
 * access control.  We record the mapping from ac's
 * to h/w queues for use by mwl_tx_start.
 */
static int
mwl_tx_setup(struct mwl_softc *sc, int ac, int mvtype)
{
	struct mwl_txq *txq;

	if (ac >= nitems(sc->sc_ac2q)) {
		device_printf(sc->sc_dev, "AC %u out of range, max %zu!\n",
			ac, nitems(sc->sc_ac2q));
		return 0;
	}
	if (mvtype >= MWL_NUM_TX_QUEUES) {
		device_printf(sc->sc_dev, "mvtype %u out of range, max %u!\n",
			mvtype, MWL_NUM_TX_QUEUES);
		return 0;
	}
	txq = &sc->sc_txq[mvtype];
	mwl_txq_init(sc, txq, mvtype);
	sc->sc_ac2q[ac] = txq;
	return 1;
}

/*
 * Update WME parameters for a transmit queue.
 */
static int
mwl_txq_update(struct mwl_softc *sc, int ac)
{
#define	MWL_EXPONENT_TO_VALUE(v)	((1<<v)-1)
	struct ieee80211com *ic = &sc->sc_ic;
	struct chanAccParams chp;
	struct mwl_txq *txq = sc->sc_ac2q[ac];
	struct wmeParams *wmep;
	struct mwl_hal *mh = sc->sc_mh;
	int aifs, cwmin, cwmax, txoplim;

	ieee80211_wme_ic_getparams(ic, &chp);
	wmep = &chp.cap_wmeParams[ac];

	aifs = wmep->wmep_aifsn;
	/* XXX in sta mode need to pass log values for cwmin/max */
	cwmin = MWL_EXPONENT_TO_VALUE(wmep->wmep_logcwmin);
	cwmax = MWL_EXPONENT_TO_VALUE(wmep->wmep_logcwmax);
	txoplim = wmep->wmep_txopLimit;		/* NB: units of 32us */

	if (mwl_hal_setedcaparams(mh, txq->qnum, cwmin, cwmax, aifs, txoplim)) {
		device_printf(sc->sc_dev, "unable to update hardware queue "
			"parameters for %s traffic!\n",
			ieee80211_wme_acnames[ac]);
		return 0;
	}
	return 1;
#undef MWL_EXPONENT_TO_VALUE
}

/*
 * Callback from the 802.11 layer to update WME parameters.
 */
static int
mwl_wme_update(struct ieee80211com *ic)
{
	struct mwl_softc *sc = ic->ic_softc;

	return !mwl_txq_update(sc, WME_AC_BE) ||
	    !mwl_txq_update(sc, WME_AC_BK) ||
	    !mwl_txq_update(sc, WME_AC_VI) ||
	    !mwl_txq_update(sc, WME_AC_VO) ? EIO : 0;
}

/*
 * Reclaim resources for a setup queue.
 */
static void
mwl_tx_cleanupq(struct mwl_softc *sc, struct mwl_txq *txq)
{
	/* XXX hal work? */
	MWL_TXQ_LOCK_DESTROY(txq);
}

/*
 * Reclaim all tx queue resources.
 */
static void
mwl_tx_cleanup(struct mwl_softc *sc)
{
	int i;

	for (i = 0; i < MWL_NUM_TX_QUEUES; i++)
		mwl_tx_cleanupq(sc, &sc->sc_txq[i]);
}

static int
mwl_tx_dmasetup(struct mwl_softc *sc, struct mwl_txbuf *bf, struct mbuf *m0)
{
	struct mbuf *m;
	int error;

	/*
	 * Load the DMA map so any coalescing is done.  This
	 * also calculates the number of descriptors we need.
	 */
	error = bus_dmamap_load_mbuf_sg(sc->sc_dmat, bf->bf_dmamap, m0,
				     bf->bf_segs, &bf->bf_nseg,
				     BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		/* XXX packet requires too many descriptors */
		bf->bf_nseg = MWL_TXDESC+1;
	} else if (error != 0) {
		sc->sc_stats.mst_tx_busdma++;
		m_freem(m0);
		return error;
	}
	/*
	 * Discard null packets and check for packets that
	 * require too many TX descriptors.  We try to convert
	 * the latter to a cluster.
	 */
	if (error == EFBIG) {		/* too many desc's, linearize */
		sc->sc_stats.mst_tx_linear++;
#if MWL_TXDESC > 1
		m = m_collapse(m0, M_NOWAIT, MWL_TXDESC);
#else
		m = m_defrag(m0, M_NOWAIT);
#endif
		if (m == NULL) {
			m_freem(m0);
			sc->sc_stats.mst_tx_nombuf++;
			return ENOMEM;
		}
		m0 = m;
		error = bus_dmamap_load_mbuf_sg(sc->sc_dmat, bf->bf_dmamap, m0,
					     bf->bf_segs, &bf->bf_nseg,
					     BUS_DMA_NOWAIT);
		if (error != 0) {
			sc->sc_stats.mst_tx_busdma++;
			m_freem(m0);
			return error;
		}
		KASSERT(bf->bf_nseg <= MWL_TXDESC,
		    ("too many segments after defrag; nseg %u", bf->bf_nseg));
	} else if (bf->bf_nseg == 0) {		/* null packet, discard */
		sc->sc_stats.mst_tx_nodata++;
		m_freem(m0);
		return EIO;
	}
	DPRINTF(sc, MWL_DEBUG_XMIT, "%s: m %p len %u\n",
		__func__, m0, m0->m_pkthdr.len);
	bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap, BUS_DMASYNC_PREWRITE);
	bf->bf_m = m0;

	return 0;
}

static __inline int
mwl_cvtlegacyrate(int rate)
{
	switch (rate) {
	case 2:	 return 0;
	case 4:	 return 1;
	case 11: return 2;
	case 22: return 3;
	case 44: return 4;
	case 12: return 5;
	case 18: return 6;
	case 24: return 7;
	case 36: return 8;
	case 48: return 9;
	case 72: return 10;
	case 96: return 11;
	case 108:return 12;
	}
	return 0;
}

/*
 * Calculate fixed tx rate information per client state;
 * this value is suitable for writing to the Format field
 * of a tx descriptor.
 */
static uint16_t
mwl_calcformat(uint8_t rate, const struct ieee80211_node *ni)
{
	uint16_t fmt;

	fmt = SM(3, EAGLE_TXD_ANTENNA)
	    | (IEEE80211_IS_CHAN_HT40D(ni->ni_chan) ?
		EAGLE_TXD_EXTCHAN_LO : EAGLE_TXD_EXTCHAN_HI);
	if (rate & IEEE80211_RATE_MCS) {	/* HT MCS */
		fmt |= EAGLE_TXD_FORMAT_HT
		    /* NB: 0x80 implicitly stripped from ucastrate */
		    | SM(rate, EAGLE_TXD_RATE);
		/* XXX short/long GI may be wrong; re-check */
		if (IEEE80211_IS_CHAN_HT40(ni->ni_chan)) {
			fmt |= EAGLE_TXD_CHW_40
			    | (ni->ni_htcap & IEEE80211_HTCAP_SHORTGI40 ?
			        EAGLE_TXD_GI_SHORT : EAGLE_TXD_GI_LONG);
		} else {
			fmt |= EAGLE_TXD_CHW_20
			    | (ni->ni_htcap & IEEE80211_HTCAP_SHORTGI20 ?
			        EAGLE_TXD_GI_SHORT : EAGLE_TXD_GI_LONG);
		}
	} else {			/* legacy rate */
		fmt |= EAGLE_TXD_FORMAT_LEGACY
		    | SM(mwl_cvtlegacyrate(rate), EAGLE_TXD_RATE)
		    | EAGLE_TXD_CHW_20
		    /* XXX iv_flags & IEEE80211_F_SHPREAMBLE? */
		    | (ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_PREAMBLE ?
			EAGLE_TXD_PREAMBLE_SHORT : EAGLE_TXD_PREAMBLE_LONG);
	}
	return fmt;
}

static int
mwl_tx_start(struct mwl_softc *sc, struct ieee80211_node *ni, struct mwl_txbuf *bf,
    struct mbuf *m0)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	int error, iswep, ismcast;
	int hdrlen, copyhdrlen, pktlen;
	struct mwl_txdesc *ds;
	struct mwl_txq *txq;
	struct ieee80211_frame *wh;
	struct mwltxrec *tr;
	struct mwl_node *mn;
	uint16_t qos;
#if MWL_TXDESC > 1
	int i;
#endif

	wh = mtod(m0, struct ieee80211_frame *);
	iswep = wh->i_fc[1] & IEEE80211_FC1_PROTECTED;
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);
	hdrlen = ieee80211_anyhdrsize(wh);
	copyhdrlen = hdrlen;
	pktlen = m0->m_pkthdr.len;
	if (IEEE80211_QOS_HAS_SEQ(wh)) {
		qos = *(uint16_t *)ieee80211_getqos(wh);
		if (IEEE80211_IS_DSTODS(wh))
			copyhdrlen -= sizeof(qos);
	} else
		qos = 0;

	if (iswep) {
		const struct ieee80211_cipher *cip;
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
		cip = k->wk_cipher;
		pktlen += cip->ic_header + cip->ic_miclen + cip->ic_trailer;

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		sc->sc_tx_th.wt_flags = 0;	/* XXX */
		if (iswep)
			sc->sc_tx_th.wt_flags |= IEEE80211_RADIOTAP_F_WEP;
#if 0
		sc->sc_tx_th.wt_rate = ds->DataRate;
#endif
		sc->sc_tx_th.wt_txpower = ni->ni_txpower;
		sc->sc_tx_th.wt_antenna = sc->sc_txantenna;

		ieee80211_radiotap_tx(vap, m0);
	}
	/*
	 * Copy up/down the 802.11 header; the firmware requires
	 * we present a 2-byte payload length followed by a
	 * 4-address header (w/o QoS), followed (optionally) by
	 * any WEP/ExtIV header (but only filled in for CCMP).
	 * We are assured the mbuf has sufficient headroom to
	 * prepend in-place by the setup of ic_headroom in
	 * mwl_attach.
	 */
	if (hdrlen < sizeof(struct mwltxrec)) {
		const int space = sizeof(struct mwltxrec) - hdrlen;
		if (M_LEADINGSPACE(m0) < space) {
			/* NB: should never happen */
			device_printf(sc->sc_dev,
			    "not enough headroom, need %d found %zd, "
			    "m_flags 0x%x m_len %d\n",
			    space, M_LEADINGSPACE(m0), m0->m_flags, m0->m_len);
			ieee80211_dump_pkt(ic,
			    mtod(m0, const uint8_t *), m0->m_len, 0, -1);
			m_freem(m0);
			sc->sc_stats.mst_tx_noheadroom++;
			return EIO;
		}
		M_PREPEND(m0, space, M_NOWAIT);
	}
	tr = mtod(m0, struct mwltxrec *);
	if (wh != (struct ieee80211_frame *) &tr->wh)
		ovbcopy(wh, &tr->wh, hdrlen);
	/*
	 * Note: the "firmware length" is actually the length
	 * of the fully formed "802.11 payload".  That is, it's
	 * everything except for the 802.11 header.  In particular
	 * this includes all crypto material including the MIC!
	 */
	tr->fwlen = htole16(pktlen - hdrlen);

	/*
	 * Load the DMA map so any coalescing is done.  This
	 * also calculates the number of descriptors we need.
	 */
	error = mwl_tx_dmasetup(sc, bf, m0);
	if (error != 0) {
		/* NB: stat collected in mwl_tx_dmasetup */
		DPRINTF(sc, MWL_DEBUG_XMIT,
		    "%s: unable to setup dma\n", __func__);
		return error;
	}
	bf->bf_node = ni;			/* NB: held reference */
	m0 = bf->bf_m;				/* NB: may have changed */
	tr = mtod(m0, struct mwltxrec *);
	wh = (struct ieee80211_frame *)&tr->wh;

	/*
	 * Formulate tx descriptor.
	 */
	ds = bf->bf_desc;
	txq = bf->bf_txq;

	ds->QosCtrl = qos;			/* NB: already little-endian */
#if MWL_TXDESC == 1
	/*
	 * NB: multiframes should be zero because the descriptors
	 *     are initialized to zero.  This should handle the case
	 *     where the driver is built with MWL_TXDESC=1 but we are
	 *     using firmware with multi-segment support.
	 */
	ds->PktPtr = htole32(bf->bf_segs[0].ds_addr);
	ds->PktLen = htole16(bf->bf_segs[0].ds_len);
#else
	ds->multiframes = htole32(bf->bf_nseg);
	ds->PktLen = htole16(m0->m_pkthdr.len);
	for (i = 0; i < bf->bf_nseg; i++) {
		ds->PktPtrArray[i] = htole32(bf->bf_segs[i].ds_addr);
		ds->PktLenArray[i] = htole16(bf->bf_segs[i].ds_len);
	}
#endif
	/* NB: pPhysNext, DataRate, and SapPktInfo setup once, don't touch */
	ds->Format = 0;
	ds->pad = 0;
	ds->ack_wcb_addr = 0;

	mn = MWL_NODE(ni);
	/*
	 * Select transmit rate.
	 */
	switch (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) {
	case IEEE80211_FC0_TYPE_MGT:
		sc->sc_stats.mst_tx_mgmt++;
		/* fall thru... */
	case IEEE80211_FC0_TYPE_CTL:
		/* NB: assign to BE q to avoid bursting */
		ds->TxPriority = MWL_WME_AC_BE;
		break;
	case IEEE80211_FC0_TYPE_DATA:
		if (!ismcast) {
			const struct ieee80211_txparam *tp = ni->ni_txparms;
			/*
			 * EAPOL frames get forced to a fixed rate and w/o
			 * aggregation; otherwise check for any fixed rate
			 * for the client (may depend on association state).
			 */
			if (m0->m_flags & M_EAPOL) {
				const struct mwl_vap *mvp = MWL_VAP_CONST(vap);
				ds->Format = mvp->mv_eapolformat;
				ds->pad = htole16(
				    EAGLE_TXD_FIXED_RATE | EAGLE_TXD_DONT_AGGR);
			} else if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE) {
				/* XXX pre-calculate per node */
				ds->Format = htole16(
				    mwl_calcformat(tp->ucastrate, ni));
				ds->pad = htole16(EAGLE_TXD_FIXED_RATE);
			}
			/* NB: EAPOL frames will never have qos set */
			if (qos == 0)
				ds->TxPriority = txq->qnum;
#if MWL_MAXBA > 3
			else if (mwl_bastream_match(&mn->mn_ba[3], qos))
				ds->TxPriority = mn->mn_ba[3].txq;
#endif
#if MWL_MAXBA > 2
			else if (mwl_bastream_match(&mn->mn_ba[2], qos))
				ds->TxPriority = mn->mn_ba[2].txq;
#endif
#if MWL_MAXBA > 1
			else if (mwl_bastream_match(&mn->mn_ba[1], qos))
				ds->TxPriority = mn->mn_ba[1].txq;
#endif
#if MWL_MAXBA > 0
			else if (mwl_bastream_match(&mn->mn_ba[0], qos))
				ds->TxPriority = mn->mn_ba[0].txq;
#endif
			else
				ds->TxPriority = txq->qnum;
		} else
			ds->TxPriority = txq->qnum;
		break;
	default:
		device_printf(sc->sc_dev, "bogus frame type 0x%x (%s)\n",
			wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK, __func__);
		sc->sc_stats.mst_tx_badframetype++;
		m_freem(m0);
		return EIO;
	}

	if (IFF_DUMPPKTS_XMIT(sc))
		ieee80211_dump_pkt(ic,
		    mtod(m0, const uint8_t *)+sizeof(uint16_t),
		    m0->m_len - sizeof(uint16_t), ds->DataRate, -1);

	MWL_TXQ_LOCK(txq);
	ds->Status = htole32(EAGLE_TXD_STATUS_FW_OWNED);
	STAILQ_INSERT_TAIL(&txq->active, bf, bf_list);
	MWL_TXDESC_SYNC(txq, ds, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	sc->sc_tx_timer = 5;
	MWL_TXQ_UNLOCK(txq);

	return 0;
}

static __inline int
mwl_cvtlegacyrix(int rix)
{
	static const int ieeerates[] =
	    { 2, 4, 11, 22, 44, 12, 18, 24, 36, 48, 72, 96, 108 };
	return (rix < nitems(ieeerates) ? ieeerates[rix] : 0);
}

/*
 * Process completed xmit descriptors from the specified queue.
 */
static int
mwl_tx_processq(struct mwl_softc *sc, struct mwl_txq *txq)
{
#define	EAGLE_TXD_STATUS_MCAST \
	(EAGLE_TXD_STATUS_MULTICAST_TX | EAGLE_TXD_STATUS_BROADCAST_TX)
	struct ieee80211com *ic = &sc->sc_ic;
	struct mwl_txbuf *bf;
	struct mwl_txdesc *ds;
	struct ieee80211_node *ni;
	struct mwl_node *an;
	int nreaped;
	uint32_t status;

	DPRINTF(sc, MWL_DEBUG_TX_PROC, "%s: tx queue %u\n", __func__, txq->qnum);
	for (nreaped = 0;; nreaped++) {
		MWL_TXQ_LOCK(txq);
		bf = STAILQ_FIRST(&txq->active);
		if (bf == NULL) {
			MWL_TXQ_UNLOCK(txq);
			break;
		}
		ds = bf->bf_desc;
		MWL_TXDESC_SYNC(txq, ds,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		if (ds->Status & htole32(EAGLE_TXD_STATUS_FW_OWNED)) {
			MWL_TXQ_UNLOCK(txq);
			break;
		}
		STAILQ_REMOVE_HEAD(&txq->active, bf_list);
		MWL_TXQ_UNLOCK(txq);

#ifdef MWL_DEBUG
		if (sc->sc_debug & MWL_DEBUG_XMIT_DESC)
			mwl_printtxbuf(bf, txq->qnum, nreaped);
#endif
		ni = bf->bf_node;
		if (ni != NULL) {
			an = MWL_NODE(ni);
			status = le32toh(ds->Status);
			if (status & EAGLE_TXD_STATUS_OK) {
				uint16_t Format = le16toh(ds->Format);
				uint8_t txant = MS(Format, EAGLE_TXD_ANTENNA);

				sc->sc_stats.mst_ant_tx[txant]++;
				if (status & EAGLE_TXD_STATUS_OK_RETRY)
					sc->sc_stats.mst_tx_retries++;
				if (status & EAGLE_TXD_STATUS_OK_MORE_RETRY)
					sc->sc_stats.mst_tx_mretries++;
				if (txq->qnum >= MWL_WME_AC_VO)
					ic->ic_wme.wme_hipri_traffic++;
				ni->ni_txrate = MS(Format, EAGLE_TXD_RATE);
				if ((Format & EAGLE_TXD_FORMAT_HT) == 0) {
					ni->ni_txrate = mwl_cvtlegacyrix(
					    ni->ni_txrate);
				} else
					ni->ni_txrate |= IEEE80211_RATE_MCS;
				sc->sc_stats.mst_tx_rate = ni->ni_txrate;
			} else {
				if (status & EAGLE_TXD_STATUS_FAILED_LINK_ERROR)
					sc->sc_stats.mst_tx_linkerror++;
				if (status & EAGLE_TXD_STATUS_FAILED_XRETRY)
					sc->sc_stats.mst_tx_xretries++;
				if (status & EAGLE_TXD_STATUS_FAILED_AGING)
					sc->sc_stats.mst_tx_aging++;
				if (bf->bf_m->m_flags & M_FF)
					sc->sc_stats.mst_ff_txerr++;
			}
			if (bf->bf_m->m_flags & M_TXCB)
				/* XXX strip fw len in case header inspected */
				m_adj(bf->bf_m, sizeof(uint16_t));
			ieee80211_tx_complete(ni, bf->bf_m,
			    (status & EAGLE_TXD_STATUS_OK) == 0);
		} else
			m_freem(bf->bf_m);
		ds->Status = htole32(EAGLE_TXD_STATUS_IDLE);

		bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);

		mwl_puttxbuf_tail(txq, bf);
	}
	return nreaped;
#undef EAGLE_TXD_STATUS_MCAST
}

/*
 * Deferred processing of transmit interrupt; special-cased
 * for four hardware queues, 0-3.
 */
static void
mwl_tx_proc(void *arg, int npending)
{
	struct mwl_softc *sc = arg;
	int nreaped;

	/*
	 * Process each active queue.
	 */
	nreaped = 0;
	if (!STAILQ_EMPTY(&sc->sc_txq[0].active))
		nreaped += mwl_tx_processq(sc, &sc->sc_txq[0]);
	if (!STAILQ_EMPTY(&sc->sc_txq[1].active))
		nreaped += mwl_tx_processq(sc, &sc->sc_txq[1]);
	if (!STAILQ_EMPTY(&sc->sc_txq[2].active))
		nreaped += mwl_tx_processq(sc, &sc->sc_txq[2]);
	if (!STAILQ_EMPTY(&sc->sc_txq[3].active))
		nreaped += mwl_tx_processq(sc, &sc->sc_txq[3]);

	if (nreaped != 0) {
		sc->sc_tx_timer = 0;
		if (mbufq_first(&sc->sc_snd) != NULL) {
			/* NB: kick fw; the tx thread may have been preempted */
			mwl_hal_txstart(sc->sc_mh, 0);
			mwl_start(sc);
		}
	}
}

static void
mwl_tx_draintxq(struct mwl_softc *sc, struct mwl_txq *txq)
{
	struct ieee80211_node *ni;
	struct mwl_txbuf *bf;
	u_int ix;

	/*
	 * NB: this assumes output has been stopped and
	 *     we do not need to block mwl_tx_tasklet
	 */
	for (ix = 0;; ix++) {
		MWL_TXQ_LOCK(txq);
		bf = STAILQ_FIRST(&txq->active);
		if (bf == NULL) {
			MWL_TXQ_UNLOCK(txq);
			break;
		}
		STAILQ_REMOVE_HEAD(&txq->active, bf_list);
		MWL_TXQ_UNLOCK(txq);
#ifdef MWL_DEBUG
		if (sc->sc_debug & MWL_DEBUG_RESET) {
			struct ieee80211com *ic = &sc->sc_ic;
			const struct mwltxrec *tr =
			    mtod(bf->bf_m, const struct mwltxrec *);
			mwl_printtxbuf(bf, txq->qnum, ix);
			ieee80211_dump_pkt(ic, (const uint8_t *)&tr->wh,
				bf->bf_m->m_len - sizeof(tr->fwlen), 0, -1);
		}
#endif /* MWL_DEBUG */
		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
		ni = bf->bf_node;
		if (ni != NULL) {
			/*
			 * Reclaim node reference.
			 */
			ieee80211_free_node(ni);
		}
		m_freem(bf->bf_m);

		mwl_puttxbuf_tail(txq, bf);
	}
}

/*
 * Drain the transmit queues and reclaim resources.
 */
static void
mwl_draintxq(struct mwl_softc *sc)
{
	int i;

	for (i = 0; i < MWL_NUM_TX_QUEUES; i++)
		mwl_tx_draintxq(sc, &sc->sc_txq[i]);
	sc->sc_tx_timer = 0;
}

#ifdef MWL_DIAGAPI
/*
 * Reset the transmit queues to a pristine state after a fw download.
 */
static void
mwl_resettxq(struct mwl_softc *sc)
{
	int i;

	for (i = 0; i < MWL_NUM_TX_QUEUES; i++)
		mwl_txq_reset(sc, &sc->sc_txq[i]);
}
#endif /* MWL_DIAGAPI */

/*
 * Clear the transmit queues of any frames submitted for the
 * specified vap.  This is done when the vap is deleted so we
 * don't potentially reference the vap after it is gone.
 * Note we cannot remove the frames; we only reclaim the node
 * reference.
 */
static void
mwl_cleartxq(struct mwl_softc *sc, struct ieee80211vap *vap)
{
	struct mwl_txq *txq;
	struct mwl_txbuf *bf;
	int i;

	for (i = 0; i < MWL_NUM_TX_QUEUES; i++) {
		txq = &sc->sc_txq[i];
		MWL_TXQ_LOCK(txq);
		STAILQ_FOREACH(bf, &txq->active, bf_list) {
			struct ieee80211_node *ni = bf->bf_node;
			if (ni != NULL && ni->ni_vap == vap) {
				bf->bf_node = NULL;
				ieee80211_free_node(ni);
			}
		}
		MWL_TXQ_UNLOCK(txq);
	}
}

static int
mwl_recv_action(struct ieee80211_node *ni, const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	struct mwl_softc *sc = ni->ni_ic->ic_softc;
	const struct ieee80211_action *ia;

	ia = (const struct ieee80211_action *) frm;
	if (ia->ia_category == IEEE80211_ACTION_CAT_HT &&
	    ia->ia_action == IEEE80211_ACTION_HT_MIMOPWRSAVE) {
		const struct ieee80211_action_ht_mimopowersave *mps =
		    (const struct ieee80211_action_ht_mimopowersave *) ia;

		mwl_hal_setmimops(sc->sc_mh, ni->ni_macaddr,
		    mps->am_control & IEEE80211_A_HT_MIMOPWRSAVE_ENA,
		    MS(mps->am_control, IEEE80211_A_HT_MIMOPWRSAVE_MODE));
		return 0;
	} else
		return sc->sc_recv_action(ni, wh, frm, efrm);
}

static int
mwl_addba_request(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap,
	int dialogtoken, int baparamset, int batimeout)
{
	struct mwl_softc *sc = ni->ni_ic->ic_softc;
	struct ieee80211vap *vap = ni->ni_vap;
	struct mwl_node *mn = MWL_NODE(ni);
	struct mwl_bastate *bas;

	bas = tap->txa_private;
	if (bas == NULL) {
		const MWL_HAL_BASTREAM *sp;
		/*
		 * Check for a free BA stream slot.
		 */
#if MWL_MAXBA > 3
		if (mn->mn_ba[3].bastream == NULL)
			bas = &mn->mn_ba[3];
		else
#endif
#if MWL_MAXBA > 2
		if (mn->mn_ba[2].bastream == NULL)
			bas = &mn->mn_ba[2];
		else
#endif
#if MWL_MAXBA > 1
		if (mn->mn_ba[1].bastream == NULL)
			bas = &mn->mn_ba[1];
		else
#endif
#if MWL_MAXBA > 0
		if (mn->mn_ba[0].bastream == NULL)
			bas = &mn->mn_ba[0];
		else 
#endif
		{
			/* sta already has max BA streams */
			/* XXX assign BA stream to highest priority tid */
			DPRINTF(sc, MWL_DEBUG_AMPDU,
			    "%s: already has max bastreams\n", __func__);
			sc->sc_stats.mst_ampdu_reject++;
			return 0;
		}
		/* NB: no held reference to ni */
		sp = mwl_hal_bastream_alloc(MWL_VAP(vap)->mv_hvap,
		    (baparamset & IEEE80211_BAPS_POLICY_IMMEDIATE) != 0,
		    ni->ni_macaddr, tap->txa_tid, ni->ni_htparam,
		    ni, tap);
		if (sp == NULL) {
			/*
			 * No available stream, return 0 so no
			 * a-mpdu aggregation will be done.
			 */
			DPRINTF(sc, MWL_DEBUG_AMPDU,
			    "%s: no bastream available\n", __func__);
			sc->sc_stats.mst_ampdu_nostream++;
			return 0;
		}
		DPRINTF(sc, MWL_DEBUG_AMPDU, "%s: alloc bastream %p\n",
		    __func__, sp);
		/* NB: qos is left zero so we won't match in mwl_tx_start */
		bas->bastream = sp;
		tap->txa_private = bas;
	}
	/* fetch current seq# from the firmware; if available */
	if (mwl_hal_bastream_get_seqno(sc->sc_mh, bas->bastream,
	    vap->iv_opmode == IEEE80211_M_STA ? vap->iv_myaddr : ni->ni_macaddr,
	    &tap->txa_start) != 0)
		tap->txa_start = 0;
	return sc->sc_addba_request(ni, tap, dialogtoken, baparamset, batimeout);
}

static int
mwl_addba_response(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap,
	int code, int baparamset, int batimeout)
{
	struct mwl_softc *sc = ni->ni_ic->ic_softc;
	struct mwl_bastate *bas;

	bas = tap->txa_private;
	if (bas == NULL) {
		/* XXX should not happen */
		DPRINTF(sc, MWL_DEBUG_AMPDU,
		    "%s: no BA stream allocated, TID %d\n",
		    __func__, tap->txa_tid);
		sc->sc_stats.mst_addba_nostream++;
		return 0;
	}
	if (code == IEEE80211_STATUS_SUCCESS) {
		struct ieee80211vap *vap = ni->ni_vap;
		int bufsiz, error;

		/*
		 * Tell the firmware to setup the BA stream;
		 * we know resources are available because we
		 * pre-allocated one before forming the request.
		 */
		bufsiz = MS(baparamset, IEEE80211_BAPS_BUFSIZ);
		if (bufsiz == 0)
			bufsiz = IEEE80211_AGGR_BAWMAX;
		error = mwl_hal_bastream_create(MWL_VAP(vap)->mv_hvap,
		    bas->bastream, bufsiz, bufsiz, tap->txa_start);
		if (error != 0) {
			/*
			 * Setup failed, return immediately so no a-mpdu
			 * aggregation will be done.
			 */
			mwl_hal_bastream_destroy(sc->sc_mh, bas->bastream);
			mwl_bastream_free(bas);
			tap->txa_private = NULL;

			DPRINTF(sc, MWL_DEBUG_AMPDU,
			    "%s: create failed, error %d, bufsiz %d TID %d "
			    "htparam 0x%x\n", __func__, error, bufsiz,
			    tap->txa_tid, ni->ni_htparam);
			sc->sc_stats.mst_bacreate_failed++;
			return 0;
		}
		/* NB: cache txq to avoid ptr indirect */
		mwl_bastream_setup(bas, tap->txa_tid, bas->bastream->txq);
		DPRINTF(sc, MWL_DEBUG_AMPDU,
		    "%s: bastream %p assigned to txq %d TID %d bufsiz %d "
		    "htparam 0x%x\n", __func__, bas->bastream,
		    bas->txq, tap->txa_tid, bufsiz, ni->ni_htparam);
	} else {
		/*
		 * Other side NAK'd us; return the resources.
		 */
		DPRINTF(sc, MWL_DEBUG_AMPDU,
		    "%s: request failed with code %d, destroy bastream %p\n",
		    __func__, code, bas->bastream);
		mwl_hal_bastream_destroy(sc->sc_mh, bas->bastream);
		mwl_bastream_free(bas);
		tap->txa_private = NULL;
	}
	/* NB: firmware sends BAR so we don't need to */
	return sc->sc_addba_response(ni, tap, code, baparamset, batimeout);
}

static void
mwl_addba_stop(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap)
{
	struct mwl_softc *sc = ni->ni_ic->ic_softc;
	struct mwl_bastate *bas;

	bas = tap->txa_private;
	if (bas != NULL) {
		DPRINTF(sc, MWL_DEBUG_AMPDU, "%s: destroy bastream %p\n",
		    __func__, bas->bastream);
		mwl_hal_bastream_destroy(sc->sc_mh, bas->bastream);
		mwl_bastream_free(bas);
		tap->txa_private = NULL;
	}
	sc->sc_addba_stop(ni, tap);
}

/*
 * Setup the rx data structures.  This should only be
 * done once or we may get out of sync with the firmware.
 */
static int
mwl_startrecv(struct mwl_softc *sc)
{
	if (!sc->sc_recvsetup) {
		struct mwl_rxbuf *bf, *prev;
		struct mwl_rxdesc *ds;

		prev = NULL;
		STAILQ_FOREACH(bf, &sc->sc_rxbuf, bf_list) {
			int error = mwl_rxbuf_init(sc, bf);
			if (error != 0) {
				DPRINTF(sc, MWL_DEBUG_RECV,
					"%s: mwl_rxbuf_init failed %d\n",
					__func__, error);
				return error;
			}
			if (prev != NULL) {
				ds = prev->bf_desc;
				ds->pPhysNext = htole32(bf->bf_daddr);
			}
			prev = bf;
		}
		if (prev != NULL) {
			ds = prev->bf_desc;
			ds->pPhysNext =
			    htole32(STAILQ_FIRST(&sc->sc_rxbuf)->bf_daddr);
		}
		sc->sc_recvsetup = 1;
	}
	mwl_mode_init(sc);		/* set filters, etc. */
	return 0;
}

static MWL_HAL_APMODE
mwl_getapmode(const struct ieee80211vap *vap, struct ieee80211_channel *chan)
{
	MWL_HAL_APMODE mode;

	if (IEEE80211_IS_CHAN_HT(chan)) {
		if (vap->iv_flags_ht & IEEE80211_FHT_PUREN)
			mode = AP_MODE_N_ONLY;
		else if (IEEE80211_IS_CHAN_5GHZ(chan))
			mode = AP_MODE_AandN;
		else if (vap->iv_flags & IEEE80211_F_PUREG)
			mode = AP_MODE_GandN;
		else
			mode = AP_MODE_BandGandN;
	} else if (IEEE80211_IS_CHAN_ANYG(chan)) {
		if (vap->iv_flags & IEEE80211_F_PUREG)
			mode = AP_MODE_G_ONLY;
		else
			mode = AP_MODE_MIXED;
	} else if (IEEE80211_IS_CHAN_B(chan))
		mode = AP_MODE_B_ONLY;
	else if (IEEE80211_IS_CHAN_A(chan))
		mode = AP_MODE_A_ONLY;
	else
		mode = AP_MODE_MIXED;		/* XXX should not happen? */
	return mode;
}

static int
mwl_setapmode(struct ieee80211vap *vap, struct ieee80211_channel *chan)
{
	struct mwl_hal_vap *hvap = MWL_VAP(vap)->mv_hvap;
	return mwl_hal_setapmode(hvap, mwl_getapmode(vap, chan));
}

/*
 * Set/change channels.
 */
static int
mwl_chan_set(struct mwl_softc *sc, struct ieee80211_channel *chan)
{
	struct mwl_hal *mh = sc->sc_mh;
	struct ieee80211com *ic = &sc->sc_ic;
	MWL_HAL_CHANNEL hchan;
	int maxtxpow;

	DPRINTF(sc, MWL_DEBUG_RESET, "%s: chan %u MHz/flags 0x%x\n",
	    __func__, chan->ic_freq, chan->ic_flags);

	/*
	 * Convert to a HAL channel description with
	 * the flags constrained to reflect the current
	 * operating mode.
	 */
	mwl_mapchan(&hchan, chan);
	mwl_hal_intrset(mh, 0);		/* disable interrupts */
#if 0
	mwl_draintxq(sc);		/* clear pending tx frames */
#endif
	mwl_hal_setchannel(mh, &hchan);
	/*
	 * Tx power is cap'd by the regulatory setting and
	 * possibly a user-set limit.  We pass the min of
	 * these to the hal to apply them to the cal data
	 * for this channel.
	 * XXX min bound?
	 */
	maxtxpow = 2*chan->ic_maxregpower;
	if (maxtxpow > ic->ic_txpowlimit)
		maxtxpow = ic->ic_txpowlimit;
	mwl_hal_settxpower(mh, &hchan, maxtxpow / 2);
	/* NB: potentially change mcast/mgt rates */
	mwl_setcurchanrates(sc);

	/*
	 * Update internal state.
	 */
	sc->sc_tx_th.wt_chan_freq = htole16(chan->ic_freq);
	sc->sc_rx_th.wr_chan_freq = htole16(chan->ic_freq);
	if (IEEE80211_IS_CHAN_A(chan)) {
		sc->sc_tx_th.wt_chan_flags = htole16(IEEE80211_CHAN_A);
		sc->sc_rx_th.wr_chan_flags = htole16(IEEE80211_CHAN_A);
	} else if (IEEE80211_IS_CHAN_ANYG(chan)) {
		sc->sc_tx_th.wt_chan_flags = htole16(IEEE80211_CHAN_G);
		sc->sc_rx_th.wr_chan_flags = htole16(IEEE80211_CHAN_G);
	} else {
		sc->sc_tx_th.wt_chan_flags = htole16(IEEE80211_CHAN_B);
		sc->sc_rx_th.wr_chan_flags = htole16(IEEE80211_CHAN_B);
	}
	sc->sc_curchan = hchan;
	mwl_hal_intrset(mh, sc->sc_imask);

	return 0;
}

static void
mwl_scan_start(struct ieee80211com *ic)
{
	struct mwl_softc *sc = ic->ic_softc;

	DPRINTF(sc, MWL_DEBUG_STATE, "%s\n", __func__);
}

static void
mwl_scan_end(struct ieee80211com *ic)
{
	struct mwl_softc *sc = ic->ic_softc;

	DPRINTF(sc, MWL_DEBUG_STATE, "%s\n", __func__);
}

static void
mwl_set_channel(struct ieee80211com *ic)
{
	struct mwl_softc *sc = ic->ic_softc;

	(void) mwl_chan_set(sc, ic->ic_curchan);
}

/* 
 * Handle a channel switch request.  We inform the firmware
 * and mark the global state to suppress various actions.
 * NB: we issue only one request to the fw; we may be called
 * multiple times if there are multiple vap's.
 */
static void
mwl_startcsa(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct mwl_softc *sc = ic->ic_softc;
	MWL_HAL_CHANNEL hchan;

	if (sc->sc_csapending)
		return;

	mwl_mapchan(&hchan, ic->ic_csa_newchan);
	/* 1 =>'s quiet channel */
	mwl_hal_setchannelswitchie(sc->sc_mh, &hchan, 1, ic->ic_csa_count);
	sc->sc_csapending = 1;
}

/*
 * Plumb any static WEP key for the station.  This is
 * necessary as we must propagate the key from the
 * global key table of the vap to each sta db entry.
 */
static void
mwl_setanywepkey(struct ieee80211vap *vap, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	if ((vap->iv_flags & (IEEE80211_F_PRIVACY|IEEE80211_F_WPA)) ==
		IEEE80211_F_PRIVACY &&
	    vap->iv_def_txkey != IEEE80211_KEYIX_NONE &&
	    vap->iv_nw_keys[vap->iv_def_txkey].wk_keyix != IEEE80211_KEYIX_NONE)
		(void) _mwl_key_set(vap, &vap->iv_nw_keys[vap->iv_def_txkey],
				    mac);
}

static int
mwl_peerstadb(struct ieee80211_node *ni, int aid, int staid, MWL_HAL_PEERINFO *pi)
{
#define	WME(ie) ((const struct ieee80211_wme_info *) ie)
	struct ieee80211vap *vap = ni->ni_vap;
	struct mwl_hal_vap *hvap;
	int error;

	if (vap->iv_opmode == IEEE80211_M_WDS) {
		/*
		 * WDS vap's do not have a f/w vap; instead they piggyback
		 * on an AP vap and we must install the sta db entry and
		 * crypto state using that AP's handle (the WDS vap has none).
		 */
		hvap = MWL_VAP(vap)->mv_ap_hvap;
	} else
		hvap = MWL_VAP(vap)->mv_hvap;
	error = mwl_hal_newstation(hvap, ni->ni_macaddr,
	    aid, staid, pi,
	    ni->ni_flags & (IEEE80211_NODE_QOS | IEEE80211_NODE_HT),
	    ni->ni_ies.wme_ie != NULL ? WME(ni->ni_ies.wme_ie)->wme_info : 0);
	if (error == 0) {
		/*
		 * Setup security for this station.  For sta mode this is
		 * needed even though do the same thing on transition to
		 * AUTH state because the call to mwl_hal_newstation
		 * clobbers the crypto state we setup.
		 */
		mwl_setanywepkey(vap, ni->ni_macaddr);
	}
	return error;
#undef WME
}

static void
mwl_setglobalkeys(struct ieee80211vap *vap)
{
	struct ieee80211_key *wk;

	wk = &vap->iv_nw_keys[0];
	for (; wk < &vap->iv_nw_keys[IEEE80211_WEP_NKID]; wk++)
		if (wk->wk_keyix != IEEE80211_KEYIX_NONE)
			(void) _mwl_key_set(vap, wk, vap->iv_myaddr);
}

/*
 * Convert a legacy rate set to a firmware bitmask.
 */
static uint32_t
get_rate_bitmap(const struct ieee80211_rateset *rs)
{
	uint32_t rates;
	int i;

	rates = 0;
	for (i = 0; i < rs->rs_nrates; i++)
		switch (rs->rs_rates[i] & IEEE80211_RATE_VAL) {
		case 2:	  rates |= 0x001; break;
		case 4:	  rates |= 0x002; break;
		case 11:  rates |= 0x004; break;
		case 22:  rates |= 0x008; break;
		case 44:  rates |= 0x010; break;
		case 12:  rates |= 0x020; break;
		case 18:  rates |= 0x040; break;
		case 24:  rates |= 0x080; break;
		case 36:  rates |= 0x100; break;
		case 48:  rates |= 0x200; break;
		case 72:  rates |= 0x400; break;
		case 96:  rates |= 0x800; break;
		case 108: rates |= 0x1000; break;
		}
	return rates;
}

/*
 * Construct an HT firmware bitmask from an HT rate set.
 */
static uint32_t
get_htrate_bitmap(const struct ieee80211_htrateset *rs)
{
	uint32_t rates;
	int i;

	rates = 0;
	for (i = 0; i < rs->rs_nrates; i++) {
		if (rs->rs_rates[i] < 16)
			rates |= 1<<rs->rs_rates[i];
	}
	return rates;
}

/*
 * Craft station database entry for station.
 * NB: use host byte order here, the hal handles byte swapping.
 */
static MWL_HAL_PEERINFO *
mkpeerinfo(MWL_HAL_PEERINFO *pi, const struct ieee80211_node *ni)
{
	const struct ieee80211vap *vap = ni->ni_vap;

	memset(pi, 0, sizeof(*pi));
	pi->LegacyRateBitMap = get_rate_bitmap(&ni->ni_rates);
	pi->CapInfo = ni->ni_capinfo;
	if (ni->ni_flags & IEEE80211_NODE_HT) {
		/* HT capabilities, etc */
		pi->HTCapabilitiesInfo = ni->ni_htcap;
		/* XXX pi.HTCapabilitiesInfo */
	        pi->MacHTParamInfo = ni->ni_htparam;	
		pi->HTRateBitMap = get_htrate_bitmap(&ni->ni_htrates);
		pi->AddHtInfo.ControlChan = ni->ni_htctlchan;
		pi->AddHtInfo.AddChan = ni->ni_ht2ndchan;
		pi->AddHtInfo.OpMode = ni->ni_htopmode;
		pi->AddHtInfo.stbc = ni->ni_htstbc;

		/* constrain according to local configuration */
		if ((vap->iv_flags_ht & IEEE80211_FHT_SHORTGI40) == 0)
			pi->HTCapabilitiesInfo &= ~IEEE80211_HTCAP_SHORTGI40;
		if ((vap->iv_flags_ht & IEEE80211_FHT_SHORTGI20) == 0)
			pi->HTCapabilitiesInfo &= ~IEEE80211_HTCAP_SHORTGI20;
		if (ni->ni_chw != 40)
			pi->HTCapabilitiesInfo &= ~IEEE80211_HTCAP_CHWIDTH40;
	}
	return pi;
}

/*
 * Re-create the local sta db entry for a vap to ensure
 * up to date WME state is pushed to the firmware.  Because
 * this resets crypto state this must be followed by a
 * reload of any keys in the global key table.
 */
static int
mwl_localstadb(struct ieee80211vap *vap)
{
#define	WME(ie) ((const struct ieee80211_wme_info *) ie)
	struct mwl_hal_vap *hvap = MWL_VAP(vap)->mv_hvap;
	struct ieee80211_node *bss;
	MWL_HAL_PEERINFO pi;
	int error;

	switch (vap->iv_opmode) {
	case IEEE80211_M_STA:
		bss = vap->iv_bss;
		error = mwl_hal_newstation(hvap, vap->iv_myaddr, 0, 0,
		    vap->iv_state == IEEE80211_S_RUN ?
			mkpeerinfo(&pi, bss) : NULL,
		    (bss->ni_flags & (IEEE80211_NODE_QOS | IEEE80211_NODE_HT)),
		    bss->ni_ies.wme_ie != NULL ?
			WME(bss->ni_ies.wme_ie)->wme_info : 0);
		if (error == 0)
			mwl_setglobalkeys(vap);
		break;
	case IEEE80211_M_HOSTAP:
	case IEEE80211_M_MBSS:
		error = mwl_hal_newstation(hvap, vap->iv_myaddr,
		    0, 0, NULL, vap->iv_flags & IEEE80211_F_WME, 0);
		if (error == 0)
			mwl_setglobalkeys(vap);
		break;
	default:
		error = 0;
		break;
	}
	return error;
#undef WME
}

static int
mwl_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct mwl_vap *mvp = MWL_VAP(vap);
	struct mwl_hal_vap *hvap = mvp->mv_hvap;
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *ni = NULL;
	struct mwl_softc *sc = ic->ic_softc;
	struct mwl_hal *mh = sc->sc_mh;
	enum ieee80211_state ostate = vap->iv_state;
	int error;

	DPRINTF(sc, MWL_DEBUG_STATE, "%s: %s: %s -> %s\n",
	    vap->iv_ifp->if_xname, __func__,
	    ieee80211_state_name[ostate], ieee80211_state_name[nstate]);

	callout_stop(&sc->sc_timer);
	/*
	 * Clear current radar detection state.
	 */
	if (ostate == IEEE80211_S_CAC) {
		/* stop quiet mode radar detection */
		mwl_hal_setradardetection(mh, DR_CHK_CHANNEL_AVAILABLE_STOP);
	} else if (sc->sc_radarena) {
		/* stop in-service radar detection */
		mwl_hal_setradardetection(mh, DR_DFS_DISABLE);
		sc->sc_radarena = 0;
	}
	/*
	 * Carry out per-state actions before doing net80211 work.
	 */
	if (nstate == IEEE80211_S_INIT) {
		/* NB: only ap+sta vap's have a fw entity */
		if (hvap != NULL)
			mwl_hal_stop(hvap);
	} else if (nstate == IEEE80211_S_SCAN) {
		mwl_hal_start(hvap);
		/* NB: this disables beacon frames */
		mwl_hal_setinframode(hvap);
	} else if (nstate == IEEE80211_S_AUTH) {
		/*
		 * Must create a sta db entry in case a WEP key needs to
		 * be plumbed.  This entry will be overwritten if we
		 * associate; otherwise it will be reclaimed on node free.
		 */
		ni = vap->iv_bss;
		MWL_NODE(ni)->mn_hvap = hvap;
		(void) mwl_peerstadb(ni, 0, 0, NULL);
	} else if (nstate == IEEE80211_S_CSA) {
		/* XXX move to below? */
		if (vap->iv_opmode == IEEE80211_M_HOSTAP ||
		    vap->iv_opmode == IEEE80211_M_MBSS)
			mwl_startcsa(vap);
	} else if (nstate == IEEE80211_S_CAC) {
		/* XXX move to below? */
		/* stop ap xmit and enable quiet mode radar detection */
		mwl_hal_setradardetection(mh, DR_CHK_CHANNEL_AVAILABLE_START);
	}

	/*
	 * Invoke the parent method to do net80211 work.
	 */
	error = mvp->mv_newstate(vap, nstate, arg);

	/*
	 * Carry out work that must be done after net80211 runs;
	 * this work requires up to date state (e.g. iv_bss).
	 */
	if (error == 0 && nstate == IEEE80211_S_RUN) {
		/* NB: collect bss node again, it may have changed */
		ni = vap->iv_bss;

		DPRINTF(sc, MWL_DEBUG_STATE,
		    "%s: %s(RUN): iv_flags 0x%08x bintvl %d bssid %s "
		    "capinfo 0x%04x chan %d\n",
		    vap->iv_ifp->if_xname, __func__, vap->iv_flags,
		    ni->ni_intval, ether_sprintf(ni->ni_bssid), ni->ni_capinfo,
		    ieee80211_chan2ieee(ic, ic->ic_curchan));

		/*
		 * Recreate local sta db entry to update WME/HT state.
		 */
		mwl_localstadb(vap);
		switch (vap->iv_opmode) {
		case IEEE80211_M_HOSTAP:
		case IEEE80211_M_MBSS:
			if (ostate == IEEE80211_S_CAC) {
				/* enable in-service radar detection */
				mwl_hal_setradardetection(mh,
				    DR_IN_SERVICE_MONITOR_START);
				sc->sc_radarena = 1;
			}
			/*
			 * Allocate and setup the beacon frame
			 * (and related state).
			 */
			error = mwl_reset_vap(vap, IEEE80211_S_RUN);
			if (error != 0) {
				DPRINTF(sc, MWL_DEBUG_STATE,
				    "%s: beacon setup failed, error %d\n",
				    __func__, error);
				goto bad;
			}
			/* NB: must be after setting up beacon */
			mwl_hal_start(hvap);
			break;
		case IEEE80211_M_STA:
			DPRINTF(sc, MWL_DEBUG_STATE, "%s: %s: aid 0x%x\n",
			    vap->iv_ifp->if_xname, __func__, ni->ni_associd);
			/*
			 * Set state now that we're associated.
			 */
			mwl_hal_setassocid(hvap, ni->ni_bssid, ni->ni_associd);
			mwl_setrates(vap);
			mwl_hal_setrtsthreshold(hvap, vap->iv_rtsthreshold);
			if ((vap->iv_flags & IEEE80211_F_DWDS) &&
			    sc->sc_ndwdsvaps++ == 0)
				mwl_hal_setdwds(mh, 1);
			break;
		case IEEE80211_M_WDS:
			DPRINTF(sc, MWL_DEBUG_STATE, "%s: %s: bssid %s\n",
			    vap->iv_ifp->if_xname, __func__,
			    ether_sprintf(ni->ni_bssid));
			mwl_seteapolformat(vap);
			break;
		default:
			break;
		}
		/*
		 * Set CS mode according to operating channel;
		 * this mostly an optimization for 5GHz.
		 *
		 * NB: must follow mwl_hal_start which resets csmode
		 */
		if (IEEE80211_IS_CHAN_5GHZ(ic->ic_bsschan))
			mwl_hal_setcsmode(mh, CSMODE_AGGRESSIVE);
		else
			mwl_hal_setcsmode(mh, CSMODE_AUTO_ENA);
		/*
		 * Start timer to prod firmware.
		 */
		if (sc->sc_ageinterval != 0)
			callout_reset(&sc->sc_timer, sc->sc_ageinterval*hz,
			    mwl_agestations, sc);
	} else if (nstate == IEEE80211_S_SLEEP) {
		/* XXX set chip in power save */
	} else if ((vap->iv_flags & IEEE80211_F_DWDS) &&
	    --sc->sc_ndwdsvaps == 0)
		mwl_hal_setdwds(mh, 0);
bad:
	return error;
}

/*
 * Manage station id's; these are separate from AID's
 * as AID's may have values out of the range of possible
 * station id's acceptable to the firmware.
 */
static int
allocstaid(struct mwl_softc *sc, int aid)
{
	int staid;

	if (!(0 < aid && aid < MWL_MAXSTAID) || isset(sc->sc_staid, aid)) {
		/* NB: don't use 0 */
		for (staid = 1; staid < MWL_MAXSTAID; staid++)
			if (isclr(sc->sc_staid, staid))
				break;
	} else
		staid = aid;
	setbit(sc->sc_staid, staid);
	return staid;
}

static void
delstaid(struct mwl_softc *sc, int staid)
{
	clrbit(sc->sc_staid, staid);
}

/*
 * Setup driver-specific state for a newly associated node.
 * Note that we're called also on a re-associate, the isnew
 * param tells us if this is the first time or not.
 */
static void
mwl_newassoc(struct ieee80211_node *ni, int isnew)
{
	struct ieee80211vap *vap = ni->ni_vap;
        struct mwl_softc *sc = vap->iv_ic->ic_softc;
	struct mwl_node *mn = MWL_NODE(ni);
	MWL_HAL_PEERINFO pi;
	uint16_t aid;
	int error;

	aid = IEEE80211_AID(ni->ni_associd);
	if (isnew) {
		mn->mn_staid = allocstaid(sc, aid);
		mn->mn_hvap = MWL_VAP(vap)->mv_hvap;
	} else {
		mn = MWL_NODE(ni);
		/* XXX reset BA stream? */
	}
	DPRINTF(sc, MWL_DEBUG_NODE, "%s: mac %s isnew %d aid %d staid %d\n",
	    __func__, ether_sprintf(ni->ni_macaddr), isnew, aid, mn->mn_staid);
	error = mwl_peerstadb(ni, aid, mn->mn_staid, mkpeerinfo(&pi, ni));
	if (error != 0) {
		DPRINTF(sc, MWL_DEBUG_NODE,
		    "%s: error %d creating sta db entry\n",
		    __func__, error);
		/* XXX how to deal with error? */
	}
}

/*
 * Periodically poke the firmware to age out station state
 * (power save queues, pending tx aggregates).
 */
static void
mwl_agestations(void *arg)
{
	struct mwl_softc *sc = arg;

	mwl_hal_setkeepalive(sc->sc_mh);
	if (sc->sc_ageinterval != 0)		/* NB: catch dynamic changes */
		callout_schedule(&sc->sc_timer, sc->sc_ageinterval*hz);
}

static const struct mwl_hal_channel *
findhalchannel(const MWL_HAL_CHANNELINFO *ci, int ieee)
{
	int i;

	for (i = 0; i < ci->nchannels; i++) {
		const struct mwl_hal_channel *hc = &ci->channels[i];
		if (hc->ieee == ieee)
			return hc;
	}
	return NULL;
}

static int
mwl_setregdomain(struct ieee80211com *ic, struct ieee80211_regdomain *rd,
	int nchan, struct ieee80211_channel chans[])
{
	struct mwl_softc *sc = ic->ic_softc;
	struct mwl_hal *mh = sc->sc_mh;
	const MWL_HAL_CHANNELINFO *ci;
	int i;

	for (i = 0; i < nchan; i++) {
		struct ieee80211_channel *c = &chans[i];
		const struct mwl_hal_channel *hc;

		if (IEEE80211_IS_CHAN_2GHZ(c)) {
			mwl_hal_getchannelinfo(mh, MWL_FREQ_BAND_2DOT4GHZ,
			    IEEE80211_IS_CHAN_HT40(c) ?
				MWL_CH_40_MHz_WIDTH : MWL_CH_20_MHz_WIDTH, &ci);
		} else if (IEEE80211_IS_CHAN_5GHZ(c)) {
			mwl_hal_getchannelinfo(mh, MWL_FREQ_BAND_5GHZ,
			    IEEE80211_IS_CHAN_HT40(c) ?
				MWL_CH_40_MHz_WIDTH : MWL_CH_20_MHz_WIDTH, &ci);
		} else {
			device_printf(sc->sc_dev,
			    "%s: channel %u freq %u/0x%x not 2.4/5GHz\n",
			    __func__, c->ic_ieee, c->ic_freq, c->ic_flags);
			return EINVAL;
		}
		/* 
		 * Verify channel has cal data and cap tx power.
		 */
		hc = findhalchannel(ci, c->ic_ieee);
		if (hc != NULL) {
			if (c->ic_maxpower > 2*hc->maxTxPow)
				c->ic_maxpower = 2*hc->maxTxPow;
			goto next;
		}
		if (IEEE80211_IS_CHAN_HT40(c)) {
			/*
			 * Look for the extension channel since the
			 * hal table only has the primary channel.
			 */
			hc = findhalchannel(ci, c->ic_extieee);
			if (hc != NULL) {
				if (c->ic_maxpower > 2*hc->maxTxPow)
					c->ic_maxpower = 2*hc->maxTxPow;
				goto next;
			}
		}
		device_printf(sc->sc_dev,
		    "%s: no cal data for channel %u ext %u freq %u/0x%x\n",
		    __func__, c->ic_ieee, c->ic_extieee,
		    c->ic_freq, c->ic_flags);
		return EINVAL;
	next:
		;
	}
	return 0;
}

#define	IEEE80211_CHAN_HTG	(IEEE80211_CHAN_HT|IEEE80211_CHAN_G)
#define	IEEE80211_CHAN_HTA	(IEEE80211_CHAN_HT|IEEE80211_CHAN_A)

static void
addht40channels(struct ieee80211_channel chans[], int maxchans, int *nchans,
	const MWL_HAL_CHANNELINFO *ci, int flags)
{
	int i, error;

	for (i = 0; i < ci->nchannels; i++) {
		const struct mwl_hal_channel *hc = &ci->channels[i];

		error = ieee80211_add_channel_ht40(chans, maxchans, nchans,
		    hc->ieee, hc->maxTxPow, flags);
		if (error != 0 && error != ENOENT)
			break;
	}
}

static void
addchannels(struct ieee80211_channel chans[], int maxchans, int *nchans,
	const MWL_HAL_CHANNELINFO *ci, const uint8_t bands[])
{
	int i, error;

	error = 0;
	for (i = 0; i < ci->nchannels && error == 0; i++) {
		const struct mwl_hal_channel *hc = &ci->channels[i];

		error = ieee80211_add_channel(chans, maxchans, nchans,
		    hc->ieee, hc->freq, hc->maxTxPow, 0, bands);
	}
}

static void
getchannels(struct mwl_softc *sc, int maxchans, int *nchans,
	struct ieee80211_channel chans[])
{
	const MWL_HAL_CHANNELINFO *ci;
	uint8_t bands[IEEE80211_MODE_BYTES];

	/*
	 * Use the channel info from the hal to craft the
	 * channel list.  Note that we pass back an unsorted
	 * list; the caller is required to sort it for us
	 * (if desired).
	 */
	*nchans = 0;
	if (mwl_hal_getchannelinfo(sc->sc_mh,
	    MWL_FREQ_BAND_2DOT4GHZ, MWL_CH_20_MHz_WIDTH, &ci) == 0) {
		memset(bands, 0, sizeof(bands));
		setbit(bands, IEEE80211_MODE_11B);
		setbit(bands, IEEE80211_MODE_11G);
		setbit(bands, IEEE80211_MODE_11NG);
		addchannels(chans, maxchans, nchans, ci, bands);
	}
	if (mwl_hal_getchannelinfo(sc->sc_mh,
	    MWL_FREQ_BAND_5GHZ, MWL_CH_20_MHz_WIDTH, &ci) == 0) {
		memset(bands, 0, sizeof(bands));
		setbit(bands, IEEE80211_MODE_11A);
		setbit(bands, IEEE80211_MODE_11NA);
		addchannels(chans, maxchans, nchans, ci, bands);
	}
	if (mwl_hal_getchannelinfo(sc->sc_mh,
	    MWL_FREQ_BAND_2DOT4GHZ, MWL_CH_40_MHz_WIDTH, &ci) == 0)
		addht40channels(chans, maxchans, nchans, ci, IEEE80211_CHAN_HTG);
	if (mwl_hal_getchannelinfo(sc->sc_mh,
	    MWL_FREQ_BAND_5GHZ, MWL_CH_40_MHz_WIDTH, &ci) == 0)
		addht40channels(chans, maxchans, nchans, ci, IEEE80211_CHAN_HTA);
}

static void
mwl_getradiocaps(struct ieee80211com *ic,
	int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct mwl_softc *sc = ic->ic_softc;

	getchannels(sc, maxchans, nchans, chans);
}

static int
mwl_getchannels(struct mwl_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/*
	 * Use the channel info from the hal to craft the
	 * channel list for net80211.  Note that we pass up
	 * an unsorted list; net80211 will sort it for us.
	 */
	memset(ic->ic_channels, 0, sizeof(ic->ic_channels));
	ic->ic_nchans = 0;
	getchannels(sc, IEEE80211_CHAN_MAX, &ic->ic_nchans, ic->ic_channels);

	ic->ic_regdomain.regdomain = SKU_DEBUG;
	ic->ic_regdomain.country = CTRY_DEFAULT;
	ic->ic_regdomain.location = 'I';
	ic->ic_regdomain.isocc[0] = ' ';	/* XXX? */
	ic->ic_regdomain.isocc[1] = ' ';
	return (ic->ic_nchans == 0 ? EIO : 0);
}
#undef IEEE80211_CHAN_HTA
#undef IEEE80211_CHAN_HTG

#ifdef MWL_DEBUG
static void
mwl_printrxbuf(const struct mwl_rxbuf *bf, u_int ix)
{
	const struct mwl_rxdesc *ds = bf->bf_desc;
	uint32_t status = le32toh(ds->Status);

	printf("R[%2u] (DS.V:%p DS.P:0x%jx) NEXT:%08x DATA:%08x RC:%02x%s\n"
	       "      STAT:%02x LEN:%04x RSSI:%02x CHAN:%02x RATE:%02x QOS:%04x HT:%04x\n",
	    ix, ds, (uintmax_t)bf->bf_daddr, le32toh(ds->pPhysNext),
	    le32toh(ds->pPhysBuffData), ds->RxControl, 
	    ds->RxControl != EAGLE_RXD_CTRL_DRIVER_OWN ?
	        "" : (status & EAGLE_RXD_STATUS_OK) ? " *" : " !",
	    ds->Status, le16toh(ds->PktLen), ds->RSSI, ds->Channel,
	    ds->Rate, le16toh(ds->QosCtrl), le16toh(ds->HtSig2));
}

static void
mwl_printtxbuf(const struct mwl_txbuf *bf, u_int qnum, u_int ix)
{
	const struct mwl_txdesc *ds = bf->bf_desc;
	uint32_t status = le32toh(ds->Status);

	printf("Q%u[%3u]", qnum, ix);
	printf(" (DS.V:%p DS.P:0x%jx)\n", ds, (uintmax_t)bf->bf_daddr);
	printf("    NEXT:%08x DATA:%08x LEN:%04x STAT:%08x%s\n",
	    le32toh(ds->pPhysNext),
	    le32toh(ds->PktPtr), le16toh(ds->PktLen), status,
	    status & EAGLE_TXD_STATUS_USED ?
		"" : (status & 3) != 0 ? " *" : " !");
	printf("    RATE:%02x PRI:%x QOS:%04x SAP:%08x FORMAT:%04x\n",
	    ds->DataRate, ds->TxPriority, le16toh(ds->QosCtrl),
	    le32toh(ds->SapPktInfo), le16toh(ds->Format));
#if MWL_TXDESC > 1
	printf("    MULTIFRAMES:%u LEN:%04x %04x %04x %04x %04x %04x\n"
	    , le32toh(ds->multiframes)
	    , le16toh(ds->PktLenArray[0]), le16toh(ds->PktLenArray[1])
	    , le16toh(ds->PktLenArray[2]), le16toh(ds->PktLenArray[3])
	    , le16toh(ds->PktLenArray[4]), le16toh(ds->PktLenArray[5])
	);
	printf("    DATA:%08x %08x %08x %08x %08x %08x\n"
	    , le32toh(ds->PktPtrArray[0]), le32toh(ds->PktPtrArray[1])
	    , le32toh(ds->PktPtrArray[2]), le32toh(ds->PktPtrArray[3])
	    , le32toh(ds->PktPtrArray[4]), le32toh(ds->PktPtrArray[5])
	);
#endif
#if 0
{ const uint8_t *cp = (const uint8_t *) ds;
  int i;
  for (i = 0; i < sizeof(struct mwl_txdesc); i++) {
	printf("%02x ", cp[i]);
	if (((i+1) % 16) == 0)
		printf("\n");
  }
  printf("\n");
}
#endif
}
#endif /* MWL_DEBUG */

#if 0
static void
mwl_txq_dump(struct mwl_txq *txq)
{
	struct mwl_txbuf *bf;
	int i = 0;

	MWL_TXQ_LOCK(txq);
	STAILQ_FOREACH(bf, &txq->active, bf_list) {
		struct mwl_txdesc *ds = bf->bf_desc;
		MWL_TXDESC_SYNC(txq, ds,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
#ifdef MWL_DEBUG
		mwl_printtxbuf(bf, txq->qnum, i);
#endif
		i++;
	}
	MWL_TXQ_UNLOCK(txq);
}
#endif

static void
mwl_watchdog(void *arg)
{
	struct mwl_softc *sc = arg;

	callout_reset(&sc->sc_watchdog, hz, mwl_watchdog, sc);
	if (sc->sc_tx_timer == 0 || --sc->sc_tx_timer > 0)
		return;

	if (sc->sc_running && !sc->sc_invalid) {
		if (mwl_hal_setkeepalive(sc->sc_mh))
			device_printf(sc->sc_dev,
			    "transmit timeout (firmware hung?)\n");
		else
			device_printf(sc->sc_dev,
			    "transmit timeout\n");
#if 0
		mwl_reset(sc);
mwl_txq_dump(&sc->sc_txq[0]);/*XXX*/
#endif
		counter_u64_add(sc->sc_ic.ic_oerrors, 1);
		sc->sc_stats.mst_watchdog++;
	}
}

#ifdef MWL_DIAGAPI
/*
 * Diagnostic interface to the HAL.  This is used by various
 * tools to do things like retrieve register contents for
 * debugging.  The mechanism is intentionally opaque so that
 * it can change frequently w/o concern for compatibility.
 */
static int
mwl_ioctl_diag(struct mwl_softc *sc, struct mwl_diag *md)
{
	struct mwl_hal *mh = sc->sc_mh;
	u_int id = md->md_id & MWL_DIAG_ID;
	void *indata = NULL;
	void *outdata = NULL;
	u_int32_t insize = md->md_in_size;
	u_int32_t outsize = md->md_out_size;
	int error = 0;

	if (md->md_id & MWL_DIAG_IN) {
		/*
		 * Copy in data.
		 */
		indata = malloc(insize, M_TEMP, M_NOWAIT);
		if (indata == NULL) {
			error = ENOMEM;
			goto bad;
		}
		error = copyin(md->md_in_data, indata, insize);
		if (error)
			goto bad;
	}
	if (md->md_id & MWL_DIAG_DYN) {
		/*
		 * Allocate a buffer for the results (otherwise the HAL
		 * returns a pointer to a buffer where we can read the
		 * results).  Note that we depend on the HAL leaving this
		 * pointer for us to use below in reclaiming the buffer;
		 * may want to be more defensive.
		 */
		outdata = malloc(outsize, M_TEMP, M_NOWAIT);
		if (outdata == NULL) {
			error = ENOMEM;
			goto bad;
		}
	}
	if (mwl_hal_getdiagstate(mh, id, indata, insize, &outdata, &outsize)) {
		if (outsize < md->md_out_size)
			md->md_out_size = outsize;
		if (outdata != NULL)
			error = copyout(outdata, md->md_out_data,
					md->md_out_size);
	} else {
		error = EINVAL;
	}
bad:
	if ((md->md_id & MWL_DIAG_IN) && indata != NULL)
		free(indata, M_TEMP);
	if ((md->md_id & MWL_DIAG_DYN) && outdata != NULL)
		free(outdata, M_TEMP);
	return error;
}

static int
mwl_ioctl_reset(struct mwl_softc *sc, struct mwl_diag *md)
{
	struct mwl_hal *mh = sc->sc_mh;
	int error;

	MWL_LOCK_ASSERT(sc);

	if (md->md_id == 0 && mwl_hal_fwload(mh, NULL) != 0) {
		device_printf(sc->sc_dev, "unable to load firmware\n");
		return EIO;
	}
	if (mwl_hal_gethwspecs(mh, &sc->sc_hwspecs) != 0) {
		device_printf(sc->sc_dev, "unable to fetch h/w specs\n");
		return EIO;
	}
	error = mwl_setupdma(sc);
	if (error != 0) {
		/* NB: mwl_setupdma prints a msg */
		return error;
	}
	/*
	 * Reset tx/rx data structures; after reload we must
	 * re-start the driver's notion of the next xmit/recv.
	 */
	mwl_draintxq(sc);		/* clear pending frames */
	mwl_resettxq(sc);		/* rebuild tx q lists */
	sc->sc_rxnext = NULL;		/* force rx to start at the list head */
	return 0;
}
#endif /* MWL_DIAGAPI */

static void
mwl_parent(struct ieee80211com *ic)
{
	struct mwl_softc *sc = ic->ic_softc;
	int startall = 0;

	MWL_LOCK(sc);
	if (ic->ic_nrunning > 0) {
		if (sc->sc_running) {
			/*
			 * To avoid rescanning another access point,
			 * do not call mwl_init() here.  Instead,
			 * only reflect promisc mode settings.
			 */
			mwl_mode_init(sc);
		} else {
			/*
			 * Beware of being called during attach/detach
			 * to reset promiscuous mode.  In that case we
			 * will still be marked UP but not RUNNING.
			 * However trying to re-init the interface
			 * is the wrong thing to do as we've already
			 * torn down much of our state.  There's
			 * probably a better way to deal with this.
			 */
			if (!sc->sc_invalid) {
				mwl_init(sc);	/* XXX lose error */
				startall = 1;
			}
		}
	} else
		mwl_stop(sc);
	MWL_UNLOCK(sc);
	if (startall)
		ieee80211_start_all(ic);
}

static int
mwl_ioctl(struct ieee80211com *ic, u_long cmd, void *data)
{
	struct mwl_softc *sc = ic->ic_softc;
	struct ifreq *ifr = data;
	int error = 0;

	switch (cmd) {
	case SIOCGMVSTATS:
		mwl_hal_gethwstats(sc->sc_mh, &sc->sc_stats.hw_stats);
#if 0
		/* NB: embed these numbers to get a consistent view */
		sc->sc_stats.mst_tx_packets =
		    ifp->if_get_counter(ifp, IFCOUNTER_OPACKETS);
		sc->sc_stats.mst_rx_packets =
		    ifp->if_get_counter(ifp, IFCOUNTER_IPACKETS);
#endif
		/*
		 * NB: Drop the softc lock in case of a page fault;
		 * we'll accept any potential inconsisentcy in the
		 * statistics.  The alternative is to copy the data
		 * to a local structure.
		 */
		return (copyout(&sc->sc_stats, ifr_data_get_ptr(ifr),
		    sizeof (sc->sc_stats)));
#ifdef MWL_DIAGAPI
	case SIOCGMVDIAG:
		/* XXX check privs */
		return mwl_ioctl_diag(sc, (struct mwl_diag *) ifr);
	case SIOCGMVRESET:
		/* XXX check privs */
		MWL_LOCK(sc);
		error = mwl_ioctl_reset(sc,(struct mwl_diag *) ifr); 
		MWL_UNLOCK(sc);
		break;
#endif /* MWL_DIAGAPI */
	default:
		error = ENOTTY;
		break;
	}
	return (error);
}

#ifdef	MWL_DEBUG
static int
mwl_sysctl_debug(SYSCTL_HANDLER_ARGS)
{
	struct mwl_softc *sc = arg1;
	int debug, error;

	debug = sc->sc_debug | (mwl_hal_getdebug(sc->sc_mh) << 24);
	error = sysctl_handle_int(oidp, &debug, 0, req);
	if (error || !req->newptr)
		return error;
	mwl_hal_setdebug(sc->sc_mh, debug >> 24);
	sc->sc_debug = debug & 0x00ffffff;
	return 0;
}
#endif /* MWL_DEBUG */

static void
mwl_sysctlattach(struct mwl_softc *sc)
{
#ifdef	MWL_DEBUG
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);

	sc->sc_debug = mwl_debug;
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"debug", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		mwl_sysctl_debug, "I", "control debugging printfs");
#endif
}

/*
 * Announce various information on device/driver attach.
 */
static void
mwl_announce(struct mwl_softc *sc)
{

	device_printf(sc->sc_dev, "Rev A%d hardware, v%d.%d.%d.%d firmware (regioncode %d)\n",
		sc->sc_hwspecs.hwVersion,
		(sc->sc_hwspecs.fwReleaseNumber>>24) & 0xff,
		(sc->sc_hwspecs.fwReleaseNumber>>16) & 0xff,
		(sc->sc_hwspecs.fwReleaseNumber>>8) & 0xff,
		(sc->sc_hwspecs.fwReleaseNumber>>0) & 0xff,
		sc->sc_hwspecs.regionCode);
	sc->sc_fwrelease = sc->sc_hwspecs.fwReleaseNumber;

	if (bootverbose) {
		int i;
		for (i = 0; i <= WME_AC_VO; i++) {
			struct mwl_txq *txq = sc->sc_ac2q[i];
			device_printf(sc->sc_dev, "Use hw queue %u for %s traffic\n",
				txq->qnum, ieee80211_wme_acnames[i]);
		}
	}
	if (bootverbose || mwl_rxdesc != MWL_RXDESC)
		device_printf(sc->sc_dev, "using %u rx descriptors\n", mwl_rxdesc);
	if (bootverbose || mwl_rxbuf != MWL_RXBUF)
		device_printf(sc->sc_dev, "using %u rx buffers\n", mwl_rxbuf);
	if (bootverbose || mwl_txbuf != MWL_TXBUF)
		device_printf(sc->sc_dev, "using %u tx buffers\n", mwl_txbuf);
	if (bootverbose && mwl_hal_ismbsscapable(sc->sc_mh))
		device_printf(sc->sc_dev, "multi-bss support\n");
#ifdef MWL_TX_NODROP
	if (bootverbose)
		device_printf(sc->sc_dev, "no tx drop\n");
#endif
}
