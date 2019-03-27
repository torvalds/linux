/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for Realtek RTL8188CE-VAU/RTL8188CUS/RTL8188EU/RTL8188RU/RTL8192CU/RTL8812AU/RTL8821AU.
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>
#include <sys/firmware.h>
#include <sys/kdb.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_beacon.h>
#include <dev/rtwn/if_rtwn_calib.h>
#include <dev/rtwn/if_rtwn_cam.h>
#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_efuse.h>
#include <dev/rtwn/if_rtwn_fw.h>
#include <dev/rtwn/if_rtwn_ridx.h>
#include <dev/rtwn/if_rtwn_rx.h>
#include <dev/rtwn/if_rtwn_task.h>
#include <dev/rtwn/if_rtwn_tx.h>

#include <dev/rtwn/rtl8192c/r92c_reg.h>


static void		rtwn_radiotap_attach(struct rtwn_softc *);
static void		rtwn_vap_decrement_counters(struct rtwn_softc *,
			    enum ieee80211_opmode, int);
static void		rtwn_set_ic_opmode(struct rtwn_softc *);
static struct ieee80211vap *rtwn_vap_create(struct ieee80211com *,
			    const char [IFNAMSIZ], int, enum ieee80211_opmode,
			    int, const uint8_t [IEEE80211_ADDR_LEN],
			    const uint8_t [IEEE80211_ADDR_LEN]);
static void		rtwn_vap_delete(struct ieee80211vap *);
static int		rtwn_read_chipid(struct rtwn_softc *);
static int		rtwn_ioctl_reset(struct ieee80211vap *, u_long);
static void		rtwn_set_media_status(struct rtwn_softc *,
			    union sec_param *);
#ifndef RTWN_WITHOUT_UCODE
static int		rtwn_tx_fwpkt_check(struct rtwn_softc *,
			    struct ieee80211vap *);
static int		rtwn_construct_nulldata(struct rtwn_softc *,
			    struct ieee80211vap *, uint8_t *, int);
static int		rtwn_push_nulldata(struct rtwn_softc *,
			    struct ieee80211vap *);
static void		rtwn_pwrmode_init(void *);
static void		rtwn_set_pwrmode_cb(struct rtwn_softc *,
			    union sec_param *);
#endif
static void		rtwn_tsf_sync_adhoc(void *);
static void		rtwn_tsf_sync_adhoc_task(void *, int);
static void		rtwn_tsf_sync_enable(struct rtwn_softc *,
			    struct ieee80211vap *);
static void		rtwn_set_ack_preamble(struct rtwn_softc *);
static void		rtwn_set_mode(struct rtwn_softc *, uint8_t, int);
static int		rtwn_monitor_newstate(struct ieee80211vap *,
			    enum ieee80211_state, int);
static int		rtwn_newstate(struct ieee80211vap *,
			    enum ieee80211_state, int);
static void		rtwn_calc_basicrates(struct rtwn_softc *);
static int		rtwn_run(struct rtwn_softc *,
			    struct ieee80211vap *);
#ifndef D4054
static void		rtwn_watchdog(void *);
#endif
static void		rtwn_parent(struct ieee80211com *);
static int		rtwn_dma_init(struct rtwn_softc *);
static int		rtwn_mac_init(struct rtwn_softc *);
static void		rtwn_mrr_init(struct rtwn_softc *);
static void		rtwn_scan_start(struct ieee80211com *);
static void		rtwn_scan_curchan(struct ieee80211_scan_state *,
			    unsigned long);
static void		rtwn_scan_end(struct ieee80211com *);
static void		rtwn_getradiocaps(struct ieee80211com *, int, int *,
			    struct ieee80211_channel[]);
static void		rtwn_update_chw(struct ieee80211com *);
static void		rtwn_set_channel(struct ieee80211com *);
static int		rtwn_wme_update(struct ieee80211com *);
static void		rtwn_update_slot(struct ieee80211com *);
static void		rtwn_update_slot_cb(struct rtwn_softc *,
			    union sec_param *);
static void		rtwn_update_aifs(struct rtwn_softc *, uint8_t);
static void		rtwn_update_promisc(struct ieee80211com *);
static void		rtwn_update_mcast(struct ieee80211com *);
static int		rtwn_set_bssid(struct rtwn_softc *,
			    const uint8_t *, int);
static int		rtwn_set_macaddr(struct rtwn_softc *,
			    const uint8_t *, int);
static struct ieee80211_node *rtwn_node_alloc(struct ieee80211vap *,
			    const uint8_t mac[IEEE80211_ADDR_LEN]);
static void		rtwn_newassoc(struct ieee80211_node *, int);
static void		rtwn_node_free(struct ieee80211_node *);
static void		rtwn_init_beacon_reg(struct rtwn_softc *);
static int		rtwn_init(struct rtwn_softc *);
static void		rtwn_stop(struct rtwn_softc *);

MALLOC_DEFINE(M_RTWN_PRIV, "rtwn_priv", "rtwn driver private state");

static const uint16_t wme2reg[] =
	{ R92C_EDCA_BE_PARAM, R92C_EDCA_BK_PARAM,
	  R92C_EDCA_VI_PARAM, R92C_EDCA_VO_PARAM };

int
rtwn_attach(struct rtwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int error;

	sc->cur_bcnq_id = RTWN_VAP_ID_INVALID;

	RTWN_NT_LOCK_INIT(sc);
	rtwn_cmdq_init(sc);
#ifndef D4054
	callout_init_mtx(&sc->sc_watchdog_to, &sc->sc_mtx, 0);
#endif
	callout_init(&sc->sc_calib_to, 0);
	callout_init(&sc->sc_pwrmode_init, 0);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	RTWN_LOCK(sc);
	error = rtwn_read_chipid(sc);
	RTWN_UNLOCK(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "unsupported test chip\n");
		goto detach;
	}

	error = rtwn_read_rom(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "%s: cannot read rom, error %d\n",
		    __func__, error);
		goto detach;
	}

	if (sc->macid_limit > RTWN_MACID_LIMIT) {
		device_printf(sc->sc_dev,
		    "macid limit will be reduced from %d to %d\n",
		    sc->macid_limit, RTWN_MACID_LIMIT);
		sc->macid_limit = RTWN_MACID_LIMIT;
	}
	if (sc->cam_entry_limit > RTWN_CAM_ENTRY_LIMIT) {
		device_printf(sc->sc_dev,
		    "cam entry limit will be reduced from %d to %d\n",
		    sc->cam_entry_limit, RTWN_CAM_ENTRY_LIMIT);
		sc->cam_entry_limit = RTWN_CAM_ENTRY_LIMIT;
	}
	if (sc->txdesc_len > RTWN_TX_DESC_SIZE) {
		device_printf(sc->sc_dev,
		    "adjust size for Tx descriptor (current %d, needed %d)\n",
		    RTWN_TX_DESC_SIZE, sc->txdesc_len);
		goto detach;
	}

	device_printf(sc->sc_dev, "MAC/BB %s, RF 6052 %dT%dR\n",
	    sc->name, sc->ntxchains, sc->nrxchains);

	ic->ic_softc = sc;
	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */

	/* set device capabilities */
	ic->ic_caps =
		  IEEE80211_C_STA		/* station mode */
		| IEEE80211_C_MONITOR		/* monitor mode */
		| IEEE80211_C_IBSS		/* adhoc mode */
		| IEEE80211_C_HOSTAP		/* hostap mode */
#if 0	/* TODO: HRPWM register setup */
#ifndef RTWN_WITHOUT_UCODE
		| IEEE80211_C_PMGT		/* Station-side power mgmt */
#endif
#endif
		| IEEE80211_C_SHPREAMBLE	/* short preamble supported */
		| IEEE80211_C_SHSLOT		/* short slot time supported */
#if 0
		| IEEE80211_C_BGSCAN		/* capable of bg scanning */
#endif
		| IEEE80211_C_WPA		/* 802.11i */
		| IEEE80211_C_WME		/* 802.11e */
		| IEEE80211_C_SWAMSDUTX		/* Do software A-MSDU TX */
		| IEEE80211_C_FF		/* Atheros fast-frames */
		;

	if (sc->sc_hwcrypto != RTWN_CRYPTO_SW) {
		ic->ic_cryptocaps =
		    IEEE80211_CRYPTO_WEP |
		    IEEE80211_CRYPTO_TKIP |
		    IEEE80211_CRYPTO_AES_CCM;
	}

	ic->ic_htcaps =
	      IEEE80211_HTCAP_SHORTGI20		/* short GI in 20MHz */
	    | IEEE80211_HTCAP_MAXAMSDU_3839	/* max A-MSDU length */
	    | IEEE80211_HTCAP_SMPS_OFF		/* SM PS mode disabled */
	    /* s/w capabilities */
	    | IEEE80211_HTC_HT			/* HT operation */
	    | IEEE80211_HTC_AMPDU		/* A-MPDU tx */
	    | IEEE80211_HTC_AMSDU		/* A-MSDU tx */
	    ;

	if (sc->sc_ht40) {
		ic->ic_htcaps |=
		      IEEE80211_HTCAP_CHWIDTH40	/* 40 MHz channel width */
		    | IEEE80211_HTCAP_SHORTGI40	/* short GI in 40MHz */
		    ;
	}

	ic->ic_txstream = sc->ntxchains;
	ic->ic_rxstream = sc->nrxchains;

	/* Enable TX watchdog */
#ifdef D4054
	ic->ic_flags_ext |= IEEE80211_FEXT_WATCHDOG;
#endif

	/* Adjust capabilities. */
	rtwn_adj_devcaps(sc);

	rtwn_getradiocaps(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	/* XXX TODO: setup regdomain if R92C_CHANNEL_PLAN_BY_HW bit is set. */

	ieee80211_ifattach(ic);
	ic->ic_raw_xmit = rtwn_raw_xmit;
	ic->ic_scan_start = rtwn_scan_start;
	sc->sc_scan_curchan = ic->ic_scan_curchan;
	ic->ic_scan_curchan = rtwn_scan_curchan;
	ic->ic_scan_end = rtwn_scan_end;
	ic->ic_getradiocaps = rtwn_getradiocaps;
	ic->ic_update_chw = rtwn_update_chw;
	ic->ic_set_channel = rtwn_set_channel;
	ic->ic_transmit = rtwn_transmit;
	ic->ic_parent = rtwn_parent;
	ic->ic_vap_create = rtwn_vap_create;
	ic->ic_vap_delete = rtwn_vap_delete;
	ic->ic_wme.wme_update = rtwn_wme_update;
	ic->ic_updateslot = rtwn_update_slot;
	ic->ic_update_promisc = rtwn_update_promisc;
	ic->ic_update_mcast = rtwn_update_mcast;
	ic->ic_node_alloc = rtwn_node_alloc;
	ic->ic_newassoc = rtwn_newassoc;
	sc->sc_node_free = ic->ic_node_free;
	ic->ic_node_free = rtwn_node_free;

	rtwn_postattach(sc);
	rtwn_radiotap_attach(sc);

	if (bootverbose)
		ieee80211_announce(ic);

	return (0);

detach:
	return (ENXIO);			/* failure */
}

static void
rtwn_radiotap_attach(struct rtwn_softc *sc)
{
	struct rtwn_rx_radiotap_header *rxtap = &sc->sc_rxtap;
	struct rtwn_tx_radiotap_header *txtap = &sc->sc_txtap;

	ieee80211_radiotap_attach(&sc->sc_ic,
	    &txtap->wt_ihdr, sizeof(*txtap), RTWN_TX_RADIOTAP_PRESENT,
	    &rxtap->wr_ihdr, sizeof(*rxtap), RTWN_RX_RADIOTAP_PRESENT);
}

void
rtwn_sysctlattach(struct rtwn_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);

#if 1
	sc->sc_ht40 = 0;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "ht40", CTLFLAG_RDTUN, &sc->sc_ht40,
	    sc->sc_ht40, "Enable 40 MHz mode support");
#endif

#ifdef RTWN_DEBUG
	SYSCTL_ADD_U32(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "debug", CTLFLAG_RWTUN, &sc->sc_debug, sc->sc_debug,
	    "Control debugging printfs");
#endif

	sc->sc_hwcrypto = RTWN_CRYPTO_PAIR;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "hwcrypto", CTLFLAG_RDTUN, &sc->sc_hwcrypto,
	    sc->sc_hwcrypto, "Enable h/w crypto: "
	    "0 - disable, 1 - pairwise keys, 2 - all keys");
	if (sc->sc_hwcrypto >= RTWN_CRYPTO_MAX)
		sc->sc_hwcrypto = RTWN_CRYPTO_FULL;

	sc->sc_ratectl_sysctl = RTWN_RATECTL_NET80211;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "ratectl", CTLFLAG_RDTUN, &sc->sc_ratectl_sysctl,
	    sc->sc_ratectl_sysctl, "Select rate control mechanism: "
	    "0 - disabled, 1 - via net80211, 2 - via firmware");
	if (sc->sc_ratectl_sysctl >= RTWN_RATECTL_MAX)
		sc->sc_ratectl_sysctl = RTWN_RATECTL_FW;

	sc->sc_ratectl = sc->sc_ratectl_sysctl;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "ratectl_selected", CTLFLAG_RD, &sc->sc_ratectl,
	    sc->sc_ratectl,
	    "Currently selected rate control mechanism (by the driver)");
}

void
rtwn_detach(struct rtwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_softc == sc) {
		/* Stop command queue. */
		RTWN_CMDQ_LOCK(sc);
		sc->sc_detached = 1;
		RTWN_CMDQ_UNLOCK(sc);

		ieee80211_draintask(ic, &sc->cmdq_task);
		ieee80211_ifdetach(ic);
	}

	rtwn_cmdq_destroy(sc);
	if (RTWN_NT_LOCK_INITIALIZED(sc))
		RTWN_NT_LOCK_DESTROY(sc);
}

void
rtwn_suspend(struct rtwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	ieee80211_suspend_all(ic);
}

void
rtwn_resume(struct rtwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	ieee80211_resume_all(ic);
}

static void
rtwn_vap_decrement_counters(struct rtwn_softc *sc,
    enum ieee80211_opmode opmode, int id)
{

	RTWN_ASSERT_LOCKED(sc);

	if (id != RTWN_VAP_ID_INVALID) {
		KASSERT(id == 0 || id == 1, ("wrong vap id %d!\n", id));
		KASSERT(sc->vaps[id] != NULL, ("vap pointer is NULL\n"));
		sc->vaps[id] = NULL;
	}

	switch (opmode) {
	case IEEE80211_M_HOSTAP:
		sc->ap_vaps--;
		/* FALLTHROUGH */
	case IEEE80211_M_IBSS:
		sc->bcn_vaps--;
		/* FALLTHROUGH */
	case IEEE80211_M_STA:
		sc->nvaps--;
		break;
	case IEEE80211_M_MONITOR:
		sc->mon_vaps--;
		break;
	default:
		KASSERT(0, ("wrong opmode %d\n", opmode));
		break;
	}

	KASSERT(sc->vaps_running >= 0 && sc->monvaps_running >= 0,
	    ("number of running vaps is negative (vaps %d, monvaps %d)\n",
	    sc->vaps_running, sc->monvaps_running));
	KASSERT(sc->vaps_running - sc->monvaps_running <= RTWN_PORT_COUNT,
	    ("number of running vaps is too big (vaps %d, monvaps %d)\n",
	    sc->vaps_running, sc->monvaps_running));

	KASSERT(sc->nvaps >= 0 && sc->nvaps <= RTWN_PORT_COUNT,
	    ("wrong value %d for nvaps\n", sc->nvaps));
	KASSERT(sc->mon_vaps >= 0, ("mon_vaps is negative (%d)\n",
	    sc->mon_vaps));
	KASSERT(sc->bcn_vaps >= 0 && ((RTWN_CHIP_HAS_BCNQ1(sc) &&
	    sc->bcn_vaps <= RTWN_PORT_COUNT) || sc->bcn_vaps <= 1),
	    ("bcn_vaps value %d is wrong\n", sc->bcn_vaps));
	KASSERT(sc->ap_vaps >= 0 && ((RTWN_CHIP_HAS_BCNQ1(sc) &&
	    sc->ap_vaps <= RTWN_PORT_COUNT) || sc->ap_vaps <= 1),
	    ("ap_vaps value %d is wrong\n", sc->ap_vaps));
}

static void
rtwn_set_ic_opmode(struct rtwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	RTWN_ASSERT_LOCKED(sc);

	/* for ieee80211_reset_erp() */
	if (sc->bcn_vaps - sc->ap_vaps > 0)
		ic->ic_opmode = IEEE80211_M_IBSS;
	else if (sc->ap_vaps > 0)
		ic->ic_opmode = IEEE80211_M_HOSTAP;
	else if (sc->nvaps > 0)
		ic->ic_opmode = IEEE80211_M_STA;
	else
		ic->ic_opmode = IEEE80211_M_MONITOR;
}

static struct ieee80211vap *
rtwn_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct rtwn_softc *sc = ic->ic_softc;
	struct rtwn_vap *uvp;
	struct ieee80211vap *vap;
	int id = RTWN_VAP_ID_INVALID;

	RTWN_LOCK(sc);
	KASSERT(sc->nvaps <= RTWN_PORT_COUNT,
	    ("nvaps overflow (%d > %d)\n", sc->nvaps, RTWN_PORT_COUNT));
	KASSERT(sc->ap_vaps <= RTWN_PORT_COUNT,
	    ("ap_vaps overflow (%d > %d)\n", sc->ap_vaps, RTWN_PORT_COUNT));
	KASSERT(sc->bcn_vaps <= RTWN_PORT_COUNT,
	    ("bcn_vaps overflow (%d > %d)\n", sc->bcn_vaps, RTWN_PORT_COUNT));

	if (opmode != IEEE80211_M_MONITOR) {
		switch (sc->nvaps) {
		case 0:
			id = 0;
			break;
		case 1:
			if (sc->vaps[1] == NULL)
				id = 1;
			else if (sc->vaps[0] == NULL)
				id = 0;
			KASSERT(id != RTWN_VAP_ID_INVALID,
			    ("no free ports left\n"));
			break;
		case 2:
		default:
			goto fail;
		}

		if (opmode == IEEE80211_M_IBSS ||
		    opmode == IEEE80211_M_HOSTAP) {
			if ((sc->bcn_vaps == 1 && !RTWN_CHIP_HAS_BCNQ1(sc)) ||
			    sc->bcn_vaps == RTWN_PORT_COUNT)
				goto fail;
		}
	}

	switch (opmode) {
	case IEEE80211_M_HOSTAP:
		sc->ap_vaps++;
		/* FALLTHROUGH */
	case IEEE80211_M_IBSS:
		sc->bcn_vaps++;
		/* FALLTHROUGH */
	case IEEE80211_M_STA:
		sc->nvaps++;
		break;
	case IEEE80211_M_MONITOR:
		sc->mon_vaps++;
		break;
	default:
		KASSERT(0, ("unknown opmode %d\n", opmode));
		goto fail;
	}
	RTWN_UNLOCK(sc);

	uvp = malloc(sizeof(struct rtwn_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	uvp->id = id;
	if (id != RTWN_VAP_ID_INVALID) {
		RTWN_LOCK(sc);
		sc->vaps[id] = uvp;
		RTWN_UNLOCK(sc);
	}
	vap = &uvp->vap;
	/* enable s/w bmiss handling for sta mode */

	if (ieee80211_vap_setup(ic, vap, name, unit, opmode,
	    flags | IEEE80211_CLONE_NOBEACONS, bssid) != 0) {
		/* out of memory */
		free(uvp, M_80211_VAP);

		RTWN_LOCK(sc);
		rtwn_vap_decrement_counters(sc, opmode, id);
		RTWN_UNLOCK(sc);

		return (NULL);
	}

	rtwn_beacon_init(sc, &uvp->bcn_desc.txd[0], uvp->id);
	rtwn_vap_preattach(sc, vap);

	/* override state transition machine */
	uvp->newstate = vap->iv_newstate;
	if (opmode == IEEE80211_M_MONITOR)
		vap->iv_newstate = rtwn_monitor_newstate;
	else
		vap->iv_newstate = rtwn_newstate;
	vap->iv_update_beacon = rtwn_update_beacon;
	vap->iv_reset = rtwn_ioctl_reset;
	vap->iv_key_alloc = rtwn_key_alloc;
	vap->iv_key_set = rtwn_key_set;
	vap->iv_key_delete = rtwn_key_delete;
	vap->iv_max_aid = sc->macid_limit;

	/* 802.11n parameters */
	vap->iv_ampdu_density = IEEE80211_HTCAP_MPDUDENSITY_16;
	vap->iv_ampdu_rxmax = IEEE80211_HTCAP_MAXRXAMPDU_64K;

	TIMEOUT_TASK_INIT(taskqueue_thread, &uvp->tx_beacon_csa, 0,
	    rtwn_tx_beacon_csa, vap);
	if (opmode == IEEE80211_M_IBSS) {
		uvp->recv_mgmt = vap->iv_recv_mgmt;
		vap->iv_recv_mgmt = rtwn_adhoc_recv_mgmt;
		TASK_INIT(&uvp->tsf_sync_adhoc_task, 0,
		    rtwn_tsf_sync_adhoc_task, vap);
		callout_init(&uvp->tsf_sync_adhoc, 0);
	}

	/*
	 * NB: driver can select net80211 RA even when user requests
	 * another mechanism.
	 */
	ieee80211_ratectl_init(vap);

	/* complete setup */
	ieee80211_vap_attach(vap, ieee80211_media_change,
	    ieee80211_media_status, mac);

	RTWN_LOCK(sc);
	rtwn_set_ic_opmode(sc);
	if (sc->sc_flags & RTWN_RUNNING) {
		if (uvp->id != RTWN_VAP_ID_INVALID)
			rtwn_set_macaddr(sc, vap->iv_myaddr, uvp->id);

		rtwn_rxfilter_update(sc);
	}
	RTWN_UNLOCK(sc);

	return (vap);

fail:
	RTWN_UNLOCK(sc);
	return (NULL);
}

static void
rtwn_vap_delete(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct rtwn_softc *sc = ic->ic_softc;
	struct rtwn_vap *uvp = RTWN_VAP(vap);

	/* Put vap into INIT state + stop device if needed. */
	ieee80211_stop(vap);
	ieee80211_draintask(ic, &vap->iv_nstate_task);
	ieee80211_draintask(ic, &ic->ic_parent_task);

	RTWN_LOCK(sc);
	/* Cancel any unfinished Tx. */
	rtwn_reset_lists(sc, vap);
	if (uvp->bcn_mbuf != NULL)
		m_freem(uvp->bcn_mbuf);
	rtwn_vap_decrement_counters(sc, vap->iv_opmode, uvp->id);
	rtwn_set_ic_opmode(sc);
	if (sc->sc_flags & RTWN_RUNNING)
		rtwn_rxfilter_update(sc);
	RTWN_UNLOCK(sc);

	if (vap->iv_opmode == IEEE80211_M_IBSS) {
		ieee80211_draintask(ic, &uvp->tsf_sync_adhoc_task);
		callout_drain(&uvp->tsf_sync_adhoc);
	}

	ieee80211_ratectl_deinit(vap);
	ieee80211_vap_detach(vap);
	free(uvp, M_80211_VAP);
}

static int
rtwn_read_chipid(struct rtwn_softc *sc)
{
	uint32_t reg;

	reg = rtwn_read_4(sc, R92C_SYS_CFG);
	if (reg & R92C_SYS_CFG_TRP_VAUX_EN)	/* test chip */
		return (EOPNOTSUPP);

	rtwn_read_chipid_vendor(sc, reg);

	return (0);
}

static int
rtwn_ioctl_reset(struct ieee80211vap *vap, u_long cmd)
{
	int error;

	switch (cmd) {
#ifndef RTWN_WITHOUT_UCODE
	case IEEE80211_IOC_POWERSAVE:
	case IEEE80211_IOC_POWERSAVESLEEP:
	{
		struct rtwn_softc *sc = vap->iv_ic->ic_softc;
		struct rtwn_vap *uvp = RTWN_VAP(vap);

		if (vap->iv_opmode == IEEE80211_M_STA && uvp->id == 0) {
			RTWN_LOCK(sc);
			if (sc->sc_flags & RTWN_RUNNING)
				error = rtwn_set_pwrmode(sc, vap, 1);
			else
				error = 0;
			RTWN_UNLOCK(sc);
			if (error != 0)
				error = ENETRESET;
		} else
			error = EOPNOTSUPP;
		break;
	}
#endif
	case IEEE80211_IOC_SHORTGI:
	case IEEE80211_IOC_RTSTHRESHOLD:
	case IEEE80211_IOC_PROTMODE:
	case IEEE80211_IOC_HTPROTMODE:
	case IEEE80211_IOC_LDPC:
		error = 0;
		break;
	default:
		error = ENETRESET;
		break;
	}

	return (error);
}

static void
rtwn_set_media_status(struct rtwn_softc *sc, union sec_param *data)
{
	sc->sc_set_media_status(sc, data->macid);
}

#ifndef RTWN_WITHOUT_UCODE
static int
rtwn_tx_fwpkt_check(struct rtwn_softc *sc, struct ieee80211vap *vap)
{
	int ntries, error;

	for (ntries = 0; ntries < 5; ntries++) {
		error = rtwn_push_nulldata(sc, vap);
		if (error == 0)
			break;
	}
	if (ntries == 5) {
		device_printf(sc->sc_dev,
		    "%s: cannot push f/w frames into chip, error %d!\n",
		    __func__, error);
		return (error);
	}

	return (0);
}

static int
rtwn_construct_nulldata(struct rtwn_softc *sc, struct ieee80211vap *vap,
    uint8_t *ptr, int qos)
{
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct ieee80211com *ic = &sc->sc_ic;
	struct rtwn_tx_desc_common *txd;
	struct ieee80211_frame *wh;
	int pktlen;

	/* XXX obtain from net80211 */
	wh = (struct ieee80211_frame *)(ptr + sc->txdesc_len);
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_DATA;
	wh->i_fc[1] = IEEE80211_FC1_DIR_TODS;
	IEEE80211_ADDR_COPY(wh->i_addr1, vap->iv_bss->ni_bssid);
	IEEE80211_ADDR_COPY(wh->i_addr2, vap->iv_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, vap->iv_bss->ni_macaddr);

	txd = (struct rtwn_tx_desc_common *)ptr;
	txd->offset = sc->txdesc_len;
	pktlen = sc->txdesc_len;
	if (qos) {
		struct ieee80211_qosframe *qwh;
		const int tid = WME_AC_TO_TID(WME_AC_BE);

		qwh = (struct ieee80211_qosframe *)wh;
		qwh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_QOS_NULL;
		qwh->i_qos[0] = tid & IEEE80211_QOS_TID;

		txd->pktlen = htole16(sizeof(struct ieee80211_qosframe));
		pktlen += sizeof(struct ieee80211_qosframe);
	} else {
		wh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_NODATA;

		txd->pktlen = htole16(sizeof(struct ieee80211_frame));
		pktlen += sizeof(struct ieee80211_frame);
	}

	rtwn_fill_tx_desc_null(sc, ptr,
	    ic->ic_curmode == IEEE80211_MODE_11B, qos, uvp->id);

	return (pktlen);
}

static int
rtwn_push_nulldata(struct rtwn_softc *sc, struct ieee80211vap *vap)
{
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_channel *c = ic->ic_curchan;
	struct mbuf *m;
	uint8_t *ptr;
	int required_size, bcn_size, null_size, null_data, error;

	if (!(sc->sc_flags & RTWN_FW_LOADED))
		return (0);	/* requires firmware */

	KASSERT(sc->page_size > 0, ("page size was not set!\n"));

	/* Leave some space for beacon (multi-vap) */
	bcn_size = roundup(RTWN_BCN_MAX_SIZE, sc->page_size);
	/* 1 page for Null Data + 1 page for Qos Null Data frames. */
	required_size = bcn_size + sc->page_size * 2;

	m = m_get2(required_size, M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOMEM);

	/* Setup beacon descriptor. */
	rtwn_beacon_set_rate(sc, &uvp->bcn_desc.txd[0],
	    IEEE80211_IS_CHAN_5GHZ(c));

	ptr = mtod(m, uint8_t *);
	memset(ptr, 0, required_size - sc->txdesc_len);

	/* Construct Null Data frame. */
	ptr += bcn_size - sc->txdesc_len;
	null_size = rtwn_construct_nulldata(sc, vap, ptr, 0);
	KASSERT(null_size < sc->page_size,
	    ("recalculate size for Null Data frame\n"));

	/* Construct Qos Null Data frame. */
	ptr += roundup(null_size, sc->page_size);
	null_size = rtwn_construct_nulldata(sc, vap, ptr, 1);
	KASSERT(null_size < sc->page_size,
	    ("recalculate size for Qos Null Data frame\n"));

	/* Do not try to detect a beacon here. */
	rtwn_setbits_1_shift(sc, R92C_CR, 0, R92C_CR_ENSWBCN, 1);
	rtwn_setbits_1_shift(sc, R92C_FWHW_TXQ_CTRL,
	    R92C_FWHW_TXQ_CTRL_REAL_BEACON, 0, 2);

	if (uvp->bcn_mbuf != NULL) {
		rtwn_beacon_unload(sc, uvp->id);
		m_freem(uvp->bcn_mbuf);
	}

	m->m_pkthdr.len = m->m_len = required_size - sc->txdesc_len;
	uvp->bcn_mbuf = m;

	error = rtwn_tx_beacon_check(sc, uvp);
	if (error != 0) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_BEACON,
		    "%s: frame was not recognized!\n", __func__);
		goto fail;
	}

	/* Setup addresses in firmware. */
	null_data = howmany(bcn_size, sc->page_size);
	error = rtwn_set_rsvd_page(sc, 0, null_data, null_data + 1);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: CMD_RSVD_PAGE was not sent, error %d\n",
		    __func__, error);
		goto fail;
	}

fail:
	/* Re-enable beacon detection. */
	rtwn_setbits_1_shift(sc, R92C_FWHW_TXQ_CTRL,
	    0, R92C_FWHW_TXQ_CTRL_REAL_BEACON, 2);
	rtwn_setbits_1_shift(sc, R92C_CR, R92C_CR_ENSWBCN, 0, 1);

	/* Restore beacon (if present). */
	if (sc->bcn_vaps > 0 && sc->vaps[!uvp->id] != NULL) {
		struct rtwn_vap *uvp2 = sc->vaps[!uvp->id];

		if (uvp2->curr_mode != R92C_MSR_NOLINK)
			error = rtwn_tx_beacon_check(sc, uvp2);
	}

	return (error);
}

static void
rtwn_pwrmode_init(void *arg)
{
	struct rtwn_softc *sc = arg;

	rtwn_cmd_sleepable(sc, NULL, 0, rtwn_set_pwrmode_cb);
}

static void
rtwn_set_pwrmode_cb(struct rtwn_softc *sc, union sec_param *data)
{
	struct ieee80211vap *vap = &sc->vaps[0]->vap;

	if (vap != NULL)
		rtwn_set_pwrmode(sc, vap, 1);
}
#endif

static void
rtwn_tsf_sync_adhoc(void *arg)
{
	struct ieee80211vap *vap = arg;
	struct ieee80211com *ic = vap->iv_ic;
	struct rtwn_vap *uvp = RTWN_VAP(vap);

	if (uvp->curr_mode != R92C_MSR_NOLINK) {
		/* Do it in process context. */
		ieee80211_runtask(ic, &uvp->tsf_sync_adhoc_task);
	}
}

/*
 * Workaround for TSF synchronization:
 * when BSSID filter in IBSS mode is not set
 * (and TSF synchronization is enabled), then any beacon may update it.
 * This routine synchronizes it when BSSID matching is enabled (IBSS merge
 * is not possible during this period).
 *
 * NOTE: there is no race with rtwn_newstate(), since it uses the same
 * taskqueue.
 */
static void
rtwn_tsf_sync_adhoc_task(void *arg, int pending)
{
	struct ieee80211vap *vap = arg;
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct rtwn_softc *sc = vap->iv_ic->ic_softc;
	struct ieee80211_node *ni;

	RTWN_LOCK(sc);
	ni = ieee80211_ref_node(vap->iv_bss);

	/* Accept beacons with the same BSSID. */
	rtwn_set_rx_bssid_all(sc, 0);

	/* Deny RCR updates. */
	sc->sc_flags |= RTWN_RCR_LOCKED;

	/* Enable synchronization. */
	rtwn_setbits_1(sc, R92C_BCN_CTRL(uvp->id),
	    R92C_BCN_CTRL_DIS_TSF_UDT0, 0);

	/* Synchronize. */
	rtwn_delay(sc, ni->ni_intval * 5 * 1000);

	/* Disable synchronization. */
	rtwn_setbits_1(sc, R92C_BCN_CTRL(uvp->id),
	    0, R92C_BCN_CTRL_DIS_TSF_UDT0);

	/* Accept all beacons. */
	sc->sc_flags &= ~RTWN_RCR_LOCKED;
	rtwn_set_rx_bssid_all(sc, 1);

	/* Schedule next TSF synchronization. */
	callout_reset(&uvp->tsf_sync_adhoc, 60*hz, rtwn_tsf_sync_adhoc, vap);

	ieee80211_free_node(ni);
	RTWN_UNLOCK(sc);
}

static void
rtwn_tsf_sync_enable(struct rtwn_softc *sc, struct ieee80211vap *vap)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rtwn_vap *uvp = RTWN_VAP(vap);

	/* Reset TSF. */
	rtwn_write_1(sc, R92C_DUAL_TSF_RST, R92C_DUAL_TSF_RESET(uvp->id));

	switch (vap->iv_opmode) {
	case IEEE80211_M_STA:
		/* Enable TSF synchronization. */
		rtwn_setbits_1(sc, R92C_BCN_CTRL(uvp->id),
		    R92C_BCN_CTRL_DIS_TSF_UDT0, 0);
		break;
	case IEEE80211_M_IBSS:
		ieee80211_runtask(ic, &uvp->tsf_sync_adhoc_task);
		/* FALLTHROUGH */
	case IEEE80211_M_HOSTAP:
		/* Enable beaconing. */
		rtwn_beacon_enable(sc, uvp->id, 1);
		break;
	default:
		device_printf(sc->sc_dev, "undefined opmode %d\n",
		    vap->iv_opmode);
		return;
	}
}

static void
rtwn_set_ack_preamble(struct rtwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t reg;

	reg = rtwn_read_4(sc, R92C_WMAC_TRXPTCL_CTL);
	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		reg |= R92C_WMAC_TRXPTCL_SHPRE;
	else
		reg &= ~R92C_WMAC_TRXPTCL_SHPRE;
	rtwn_write_4(sc, R92C_WMAC_TRXPTCL_CTL, reg);
}

static void
rtwn_set_mode(struct rtwn_softc *sc, uint8_t mode, int id)
{

	rtwn_setbits_1(sc, R92C_MSR, R92C_MSR_MASK << id * 2, mode << id * 2);
	if (sc->vaps[id] != NULL)
		sc->vaps[id]->curr_mode = mode;
}

static int
rtwn_monitor_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate,
    int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct rtwn_softc *sc = ic->ic_softc;
	struct rtwn_vap *uvp = RTWN_VAP(vap);

	RTWN_DPRINTF(sc, RTWN_DEBUG_STATE, "%s -> %s\n",
	    ieee80211_state_name[vap->iv_state],
	    ieee80211_state_name[nstate]);

	if (vap->iv_state != nstate) {
		IEEE80211_UNLOCK(ic);
		RTWN_LOCK(sc);

		switch (nstate) {
		case IEEE80211_S_INIT:
			sc->vaps_running--;
			sc->monvaps_running--;

			if (sc->vaps_running == 0) {
				/* Turn link LED off. */
				rtwn_set_led(sc, RTWN_LED_LINK, 0);
			}
			break;
		case IEEE80211_S_RUN:
			sc->vaps_running++;
			sc->monvaps_running++;

			if (sc->vaps_running == 1) {
				/* Turn link LED on. */
				rtwn_set_led(sc, RTWN_LED_LINK, 1);
			}
			break;
		default:
			/* NOTREACHED */
			break;
		}

		RTWN_UNLOCK(sc);
		IEEE80211_LOCK(ic);
	}

	return (uvp->newstate(vap, nstate, arg));
}

static int
rtwn_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct rtwn_softc *sc = ic->ic_softc;
	enum ieee80211_state ostate;
	int error, early_newstate;

	ostate = vap->iv_state;
	RTWN_DPRINTF(sc, RTWN_DEBUG_STATE, "%s -> %s\n",
	    ieee80211_state_name[ostate], ieee80211_state_name[nstate]);

	if (vap->iv_bss->ni_chan == IEEE80211_CHAN_ANYC &&
	    ostate == IEEE80211_S_INIT && nstate == IEEE80211_S_RUN) {
		/* need to call iv_newstate() firstly */
		error = uvp->newstate(vap, nstate, arg);
		if (error != 0)
			return (error);

		early_newstate = 1;
	} else
		early_newstate = 0;

	if (ostate == IEEE80211_S_CSA) {
		taskqueue_cancel_timeout(taskqueue_thread,
		    &uvp->tx_beacon_csa, NULL);

		/*
		 * In multi-vap case second counter may not be cleared
		 * properly.
		 */
		vap->iv_csa_count = 0;
	}
	IEEE80211_UNLOCK(ic);
	RTWN_LOCK(sc);

	if (ostate == IEEE80211_S_CSA) {
		/* Unblock all queues (multi-vap case). */
		rtwn_write_1(sc, R92C_TXPAUSE, 0);
	}

	if ((ostate == IEEE80211_S_RUN && nstate != IEEE80211_S_CSA) ||
	    ostate == IEEE80211_S_CSA) {
		sc->vaps_running--;

		/* Set media status to 'No Link'. */
		rtwn_set_mode(sc, R92C_MSR_NOLINK, uvp->id);

		if (vap->iv_opmode == IEEE80211_M_IBSS) {
			/* Stop periodical TSF synchronization. */
			callout_stop(&uvp->tsf_sync_adhoc);
		}

		/* Disable TSF synchronization / beaconing. */
		rtwn_beacon_enable(sc, uvp->id, 0);
		rtwn_setbits_1(sc, R92C_BCN_CTRL(uvp->id),
		    0, R92C_BCN_CTRL_DIS_TSF_UDT0);

		/* NB: monitor mode vaps are using port 0. */
		if (uvp->id != 0 || sc->monvaps_running == 0) {
			/* Reset TSF. */
			rtwn_write_1(sc, R92C_DUAL_TSF_RST,
			    R92C_DUAL_TSF_RESET(uvp->id));
		}

#ifndef RTWN_WITHOUT_UCODE
		if ((ic->ic_caps & IEEE80211_C_PMGT) != 0 && uvp->id == 0) {
			/* Disable power management. */
			callout_stop(&sc->sc_pwrmode_init);
			rtwn_set_pwrmode(sc, vap, 0);
		}
#endif
		if (sc->vaps_running - sc->monvaps_running > 0) {
			/* Recalculate basic rates bitmap. */
			rtwn_calc_basicrates(sc);
		}

		if (sc->vaps_running == sc->monvaps_running) {
			/* Stop calibration. */
			callout_stop(&sc->sc_calib_to);

			/* Stop Rx of data frames. */
			rtwn_write_2(sc, R92C_RXFLTMAP2, 0);

			/* Reset EDCA parameters. */
			rtwn_write_4(sc, R92C_EDCA_VO_PARAM, 0x002f3217);
			rtwn_write_4(sc, R92C_EDCA_VI_PARAM, 0x005e4317);
			rtwn_write_4(sc, R92C_EDCA_BE_PARAM, 0x00105320);
			rtwn_write_4(sc, R92C_EDCA_BK_PARAM, 0x0000a444);

			if (sc->vaps_running == 0) {
				/* Turn link LED off. */
				rtwn_set_led(sc, RTWN_LED_LINK, 0);
			}
		}
	}

	error = 0;
	switch (nstate) {
	case IEEE80211_S_SCAN:
		/* Pause AC Tx queues. */
		if (sc->vaps_running == 0)
			rtwn_setbits_1(sc, R92C_TXPAUSE, 0, R92C_TX_QUEUE_AC);
		break;
	case IEEE80211_S_RUN:
		error = rtwn_run(sc, vap);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not move to RUN state\n", __func__);
			break;
		}

		sc->vaps_running++;
		break;
	case IEEE80211_S_CSA:
		/* Block all Tx queues (except beacon queue). */
		rtwn_setbits_1(sc, R92C_TXPAUSE, 0,
		    R92C_TX_QUEUE_AC | R92C_TX_QUEUE_MGT | R92C_TX_QUEUE_HIGH);
		break;
	default:
		break;
	}

	RTWN_UNLOCK(sc);
	IEEE80211_LOCK(ic);
	if (error != 0)
		return (error);

	return (early_newstate ? 0 : uvp->newstate(vap, nstate, arg));
}

static void
rtwn_calc_basicrates(struct rtwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t basicrates;
	int i;

	RTWN_ASSERT_LOCKED(sc);

	if (ic->ic_flags & IEEE80211_F_SCAN)
		return;		/* will be done by rtwn_scan_end(). */

	basicrates = 0;
	for (i = 0; i < nitems(sc->vaps); i++) {
		struct rtwn_vap *rvp;
		struct ieee80211vap *vap;
		struct ieee80211_node *ni;
		uint32_t rates;

		rvp = sc->vaps[i];
		if (rvp == NULL || rvp->curr_mode == R92C_MSR_NOLINK)
			continue;

		vap = &rvp->vap;
		if (vap->iv_bss == NULL)
			continue;

		ni = ieee80211_ref_node(vap->iv_bss);
		rtwn_get_rates(sc, &ni->ni_rates, NULL, &rates, NULL, 1);
		basicrates |= rates;
		ieee80211_free_node(ni);
	}

	if (basicrates == 0)
		return;

	/* XXX initial RTS rate? */
	rtwn_set_basicrates(sc, basicrates);
}

static int
rtwn_run(struct rtwn_softc *sc, struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct ieee80211_node *ni;
	uint8_t mode;
	int error;

	RTWN_ASSERT_LOCKED(sc);

	error = 0;
	ni = ieee80211_ref_node(vap->iv_bss);

	if (ic->ic_bsschan == IEEE80211_CHAN_ANYC ||
	    ni->ni_chan == IEEE80211_CHAN_ANYC) {
		error = EINVAL;
		goto fail;
	}

	switch (vap->iv_opmode) {
	case IEEE80211_M_STA:
		mode = R92C_MSR_INFRA;
		break;
	case IEEE80211_M_IBSS:
		mode = R92C_MSR_ADHOC;
		break;
	case IEEE80211_M_HOSTAP:
		mode = R92C_MSR_AP;
		break;
	default:
		KASSERT(0, ("undefined opmode %d\n", vap->iv_opmode));
		error = EINVAL;
		goto fail;
	}

	/* Set media status to 'Associated'. */
	rtwn_set_mode(sc, mode, uvp->id);

	/* Set AssocID. */
	/* XXX multi-vap? */
	rtwn_write_2(sc, R92C_BCN_PSR_RPT,
	    0xc000 | IEEE80211_NODE_AID(ni));

	/* Set BSSID. */
	rtwn_set_bssid(sc, ni->ni_bssid, uvp->id);

	/* Set beacon interval. */
	rtwn_write_2(sc, R92C_BCN_INTERVAL(uvp->id), ni->ni_intval);

	if (sc->vaps_running == sc->monvaps_running) {
		/* Enable Rx of data frames. */
		rtwn_write_2(sc, R92C_RXFLTMAP2, 0xffff);

		/* Flush all AC queues. */
		rtwn_write_1(sc, R92C_TXPAUSE, 0);
	}

#ifndef RTWN_WITHOUT_UCODE
	/* Upload (QoS) Null Data frame to firmware. */
	/* Note: do this for port 0 only. */
	if ((ic->ic_caps & IEEE80211_C_PMGT) != 0 &&
	    vap->iv_opmode == IEEE80211_M_STA && uvp->id == 0) {
		error = rtwn_tx_fwpkt_check(sc, vap);
		if (error != 0)
			goto fail;

		/* Setup power management. */
		/*
		 * NB: it will be enabled immediately - delay it,
		 * so 4-Way handshake will not be interrupted.
		 */
		callout_reset(&sc->sc_pwrmode_init, 5*hz,
		    rtwn_pwrmode_init, sc);
	}
#endif

	/* Enable TSF synchronization. */
	rtwn_tsf_sync_enable(sc, vap);

	if (vap->iv_opmode == IEEE80211_M_HOSTAP ||
	    vap->iv_opmode == IEEE80211_M_IBSS) {
		error = rtwn_setup_beacon(sc, ni);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "unable to push beacon into the chip, "
			    "error %d\n", error);
			goto fail;
		}
	}

	/* Set ACK preamble type. */
	rtwn_set_ack_preamble(sc);

	/* Set basic rates mask. */
	rtwn_calc_basicrates(sc);

#ifdef RTWN_TODO
	rtwn_write_1(sc, R92C_SIFS_CCK + 1, 10);
	rtwn_write_1(sc, R92C_SIFS_OFDM + 1, 10);
	rtwn_write_1(sc, R92C_SPEC_SIFS + 1, 10);
	rtwn_write_1(sc, R92C_MAC_SPEC_SIFS + 1, 10);
	rtwn_write_1(sc, R92C_R2T_SIFS + 1, 10);
	rtwn_write_1(sc, R92C_T2T_SIFS + 1, 10);
#endif

	if (sc->vaps_running == sc->monvaps_running) {
		/* Reset temperature calibration state machine. */
		sc->sc_flags &= ~RTWN_TEMP_MEASURED;
		sc->thcal_temp = sc->thermal_meter;

		/* Start periodic calibration. */
		callout_reset(&sc->sc_calib_to, 2*hz, rtwn_calib_to,
		    sc);

		if (sc->vaps_running == 0) {
			/* Turn link LED on. */
			rtwn_set_led(sc, RTWN_LED_LINK, 1);
		}
	}

fail:
	ieee80211_free_node(ni);

	return (error);
}

#ifndef D4054
static void
rtwn_watchdog(void *arg)
{
	struct rtwn_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	RTWN_ASSERT_LOCKED(sc);

	KASSERT(sc->sc_flags & RTWN_RUNNING, ("not running"));

	if (sc->sc_tx_timer != 0 && --sc->sc_tx_timer == 0) {
		ic_printf(ic, "device timeout\n");
		ieee80211_restart_all(ic);
		return;
	}
	callout_reset(&sc->sc_watchdog_to, hz, rtwn_watchdog, sc);
}
#endif

static void
rtwn_parent(struct ieee80211com *ic)
{
	struct rtwn_softc *sc = ic->ic_softc;
	struct ieee80211vap *vap;

	if (ic->ic_nrunning > 0) {
		if (rtwn_init(sc) != 0) {
			IEEE80211_LOCK(ic);
			TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next)
				ieee80211_stop_locked(vap);
			IEEE80211_UNLOCK(ic);
		} else
			ieee80211_start_all(ic);
	} else
		rtwn_stop(sc);
}

static int
rtwn_dma_init(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)
	uint16_t reg;
	uint8_t tx_boundary;
	int error;

	/* Initialize LLT table. */
	error = rtwn_llt_init(sc);
	if (error != 0)
		return (error);

	/* Set the number of pages for each queue. */
	RTWN_DPRINTF(sc, RTWN_DEBUG_RESET,
	    "%s: pages per queue: high %d, normal %d, low %d, public %d\n",
	    __func__, sc->nhqpages, sc->nnqpages, sc->nlqpages,
	    sc->npubqpages);

	RTWN_CHK(rtwn_write_1(sc, R92C_RQPN_NPQ, sc->nnqpages));
	RTWN_CHK(rtwn_write_4(sc, R92C_RQPN,
	    /* Set number of pages for public queue. */
	    SM(R92C_RQPN_PUBQ, sc->npubqpages) |
	    /* Set number of pages for high priority queue. */
	    SM(R92C_RQPN_HPQ, sc->nhqpages) |
	    /* Set number of pages for low priority queue. */
	    SM(R92C_RQPN_LPQ, sc->nlqpages) |
	    /* Load values. */
	    R92C_RQPN_LD));

	/* Initialize TX buffer boundary. */
	KASSERT(sc->page_count < 255 && sc->page_count > 0,
	    ("page_count is %d\n", sc->page_count));
	tx_boundary = sc->page_count + 1;
	RTWN_CHK(rtwn_write_1(sc, R92C_TXPKTBUF_BCNQ_BDNY, tx_boundary));
	RTWN_CHK(rtwn_write_1(sc, R92C_TXPKTBUF_MGQ_BDNY, tx_boundary));
	RTWN_CHK(rtwn_write_1(sc, R92C_TXPKTBUF_WMAC_LBK_BF_HD, tx_boundary));
	RTWN_CHK(rtwn_write_1(sc, R92C_TRXFF_BNDY, tx_boundary));
	RTWN_CHK(rtwn_write_1(sc, R92C_TDECTRL + 1, tx_boundary));

	error = rtwn_init_bcnq1_boundary(sc);
	if (error != 0)
		return (error);

	/* Set queue to USB pipe mapping. */
	/* Note: PCIe devices are using some magic number here. */
	reg = rtwn_get_qmap(sc);
	RTWN_CHK(rtwn_setbits_2(sc, R92C_TRXDMA_CTRL,
	    R92C_TRXDMA_CTRL_QMAP_M, reg));

	/* Configure Tx/Rx DMA (PCIe). */
	rtwn_set_desc_addr(sc);

	/* Set Tx/Rx transfer page boundary. */
	RTWN_CHK(rtwn_write_2(sc, R92C_TRXFF_BNDY + 2,
	    sc->rx_dma_size - 1));

	/* Set Tx/Rx transfer page size. */
	rtwn_set_page_size(sc);

	return (0);
}

static int
rtwn_mac_init(struct rtwn_softc *sc)
{
	int i, error;

	/* Write MAC initialization values. */
	for (i = 0; i < sc->mac_size; i++) {
		error = rtwn_write_1(sc, sc->mac_prog[i].reg,
		    sc->mac_prog[i].val);
		if (error != 0)
			return (error);
	}

	return (0);
}

static void
rtwn_mrr_init(struct rtwn_softc *sc)
{
	int i;

	/* Drop rate index by 1 per retry. */
	for (i = 0; i < R92C_DARFRC_SIZE; i++) {
		rtwn_write_1(sc, R92C_DARFRC + i, i + 1);
		rtwn_write_1(sc, R92C_RARFRC + i, i + 1);
	}
}

static void
rtwn_scan_start(struct ieee80211com *ic)
{
	struct rtwn_softc *sc = ic->ic_softc;

	RTWN_LOCK(sc);
	/* Pause beaconing. */
	rtwn_setbits_1(sc, R92C_TXPAUSE, 0, R92C_TX_QUEUE_BCN);
	/* Receive beacons / probe responses from any BSSID. */
	if (sc->bcn_vaps == 0)
		rtwn_set_rx_bssid_all(sc, 1);
	RTWN_UNLOCK(sc);
}

static void
rtwn_scan_curchan(struct ieee80211_scan_state *ss, unsigned long maxdwell)
{
	struct rtwn_softc *sc = ss->ss_ic->ic_softc;

	/* Make link LED blink during scan. */
	RTWN_LOCK(sc);
	rtwn_set_led(sc, RTWN_LED_LINK, !sc->ledlink);
	RTWN_UNLOCK(sc);

	sc->sc_scan_curchan(ss, maxdwell);
}

static void
rtwn_scan_end(struct ieee80211com *ic)
{
	struct rtwn_softc *sc = ic->ic_softc;

	RTWN_LOCK(sc);
	/* Restore limitations. */
	if (ic->ic_promisc == 0 && sc->bcn_vaps == 0)
		rtwn_set_rx_bssid_all(sc, 0);

	/* Restore LED state. */
	rtwn_set_led(sc, RTWN_LED_LINK, (sc->vaps_running != 0));

	/* Restore basic rates mask. */
	rtwn_calc_basicrates(sc);

	/* Resume beaconing. */
	rtwn_setbits_1(sc, R92C_TXPAUSE, R92C_TX_QUEUE_BCN, 0);
	RTWN_UNLOCK(sc);
}

static void
rtwn_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct rtwn_softc *sc = ic->ic_softc;
	uint8_t bands[IEEE80211_MODE_BYTES];
	int i;

	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);
	setbit(bands, IEEE80211_MODE_11G);
	setbit(bands, IEEE80211_MODE_11NG);
	ieee80211_add_channels_default_2ghz(chans, maxchans, nchans,
	    bands, !!(ic->ic_htcaps & IEEE80211_HTCAP_CHWIDTH40));

	/* XXX workaround add_channel_list() limitations */
	setbit(bands, IEEE80211_MODE_11A);
	setbit(bands, IEEE80211_MODE_11NA);
	for (i = 0; i < nitems(sc->chan_num_5ghz); i++) {
		if (sc->chan_num_5ghz[i] == 0)
			continue;

		ieee80211_add_channel_list_5ghz(chans, maxchans, nchans,
		    sc->chan_list_5ghz[i], sc->chan_num_5ghz[i], bands,
		    !!(ic->ic_htcaps & IEEE80211_HTCAP_CHWIDTH40));
	}
}

static void
rtwn_update_chw(struct ieee80211com *ic)
{
}

static void
rtwn_set_channel(struct ieee80211com *ic)
{
	struct rtwn_softc *sc = ic->ic_softc;
	struct ieee80211_channel *c = ic->ic_curchan;

	RTWN_LOCK(sc);
	rtwn_set_chan(sc, c);
	RTWN_UNLOCK(sc);
}

static int
rtwn_wme_update(struct ieee80211com *ic)
{
	struct chanAccParams chp;
	struct ieee80211_channel *c = ic->ic_curchan;
	struct rtwn_softc *sc = ic->ic_softc;
	struct wmeParams *wmep = sc->cap_wmeParams;
	uint8_t aifs, acm, slottime;
	int ac;

	ieee80211_wme_ic_getparams(ic, &chp);

	/* Prevent possible races. */
	IEEE80211_LOCK(ic);	/* XXX */
	RTWN_LOCK(sc);
	memcpy(wmep, chp.cap_wmeParams, sizeof(sc->cap_wmeParams));
	RTWN_UNLOCK(sc);
	IEEE80211_UNLOCK(ic);

	acm = 0;
	slottime = IEEE80211_GET_SLOTTIME(ic);

	RTWN_LOCK(sc);
	for (ac = WME_AC_BE; ac < WME_NUM_AC; ac++) {
		/* AIFS[AC] = AIFSN[AC] * aSlotTime + aSIFSTime. */
		aifs = wmep[ac].wmep_aifsn * slottime +
		    (IEEE80211_IS_CHAN_5GHZ(c) ?
			IEEE80211_DUR_OFDM_SIFS : IEEE80211_DUR_SIFS);
		rtwn_write_4(sc, wme2reg[ac],
		    SM(R92C_EDCA_PARAM_TXOP, wmep[ac].wmep_txopLimit) |
		    SM(R92C_EDCA_PARAM_ECWMIN, wmep[ac].wmep_logcwmin) |
		    SM(R92C_EDCA_PARAM_ECWMAX, wmep[ac].wmep_logcwmax) |
		    SM(R92C_EDCA_PARAM_AIFS, aifs));
		if (ac != WME_AC_BE)
			acm |= wmep[ac].wmep_acm << ac;
	}

	if (acm != 0)
		acm |= R92C_ACMHWCTRL_EN;
	rtwn_setbits_1(sc, R92C_ACMHWCTRL, R92C_ACMHWCTRL_ACM_MASK, acm);
	RTWN_UNLOCK(sc);

	return 0;
}

static void
rtwn_update_slot(struct ieee80211com *ic)
{
	rtwn_cmd_sleepable(ic->ic_softc, NULL, 0, rtwn_update_slot_cb);
}

static void
rtwn_update_slot_cb(struct rtwn_softc *sc, union sec_param *data)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t slottime;

	slottime = IEEE80211_GET_SLOTTIME(ic);

	RTWN_DPRINTF(sc, RTWN_DEBUG_STATE, "%s: setting slot time to %uus\n",
	    __func__, slottime);

	rtwn_write_1(sc, R92C_SLOT, slottime);
	rtwn_update_aifs(sc, slottime);
}

static void
rtwn_update_aifs(struct rtwn_softc *sc, uint8_t slottime)
{
	struct ieee80211_channel *c = sc->sc_ic.ic_curchan;
	const struct wmeParams *wmep = sc->cap_wmeParams;
	uint8_t aifs, ac;

	for (ac = WME_AC_BE; ac < WME_NUM_AC; ac++) {
		/* AIFS[AC] = AIFSN[AC] * aSlotTime + aSIFSTime. */
		aifs = wmep[ac].wmep_aifsn * slottime +
		    (IEEE80211_IS_CHAN_5GHZ(c) ?
			IEEE80211_DUR_OFDM_SIFS : IEEE80211_DUR_SIFS);
		rtwn_write_1(sc, wme2reg[ac], aifs);
	}
}

static void
rtwn_update_promisc(struct ieee80211com *ic)
{
	struct rtwn_softc *sc = ic->ic_softc;

	RTWN_LOCK(sc);
	if (sc->sc_flags & RTWN_RUNNING)
		rtwn_set_promisc(sc);
	RTWN_UNLOCK(sc);
}

static void
rtwn_update_mcast(struct ieee80211com *ic)
{
	struct rtwn_softc *sc = ic->ic_softc;

	RTWN_LOCK(sc);
	if (sc->sc_flags & RTWN_RUNNING)
		rtwn_set_multi(sc);
	RTWN_UNLOCK(sc);
}

static int
rtwn_set_bssid(struct rtwn_softc *sc, const uint8_t *bssid, int id)
{
	int error;

	error = rtwn_write_4(sc, R92C_BSSID(id), le32dec(&bssid[0]));
	if (error != 0)
		return (error);
	error = rtwn_write_2(sc, R92C_BSSID(id) + 4, le16dec(&bssid[4]));

	return (error);
}

static int
rtwn_set_macaddr(struct rtwn_softc *sc, const uint8_t *addr, int id)
{
	int error;

	error = rtwn_write_4(sc, R92C_MACID(id), le32dec(&addr[0]));
	if (error != 0)
		return (error);
	error = rtwn_write_2(sc, R92C_MACID(id) + 4, le16dec(&addr[4]));

	return (error);
}

static struct ieee80211_node *
rtwn_node_alloc(struct ieee80211vap *vap,
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct rtwn_node *un;

	un = malloc(sizeof (struct rtwn_node), M_80211_NODE,
	    M_NOWAIT | M_ZERO);

	if (un == NULL)
		return NULL;

	un->id = RTWN_MACID_UNDEFINED;
	un->avg_pwdb = -1;

	return &un->ni;
}

static void
rtwn_newassoc(struct ieee80211_node *ni, int isnew __unused)
{
	struct rtwn_softc *sc = ni->ni_ic->ic_softc;
	struct rtwn_node *un = RTWN_NODE(ni);
	int id;

	if (un->id != RTWN_MACID_UNDEFINED)
		return;

	RTWN_NT_LOCK(sc);
	for (id = 0; id <= sc->macid_limit; id++) {
		if (id != RTWN_MACID_BC && sc->node_list[id] == NULL) {
			un->id = id;
			sc->node_list[id] = ni;
			break;
		}
	}
	RTWN_NT_UNLOCK(sc);

	if (id > sc->macid_limit) {
		device_printf(sc->sc_dev, "%s: node table is full\n",
		    __func__);
		return;
	}

	/* Notify firmware. */
	id |= RTWN_MACID_VALID;
	rtwn_cmd_sleepable(sc, &id, sizeof(id), rtwn_set_media_status);
}

static void
rtwn_node_free(struct ieee80211_node *ni)
{
	struct rtwn_softc *sc = ni->ni_ic->ic_softc;
	struct rtwn_node *un = RTWN_NODE(ni);

	RTWN_NT_LOCK(sc);
	if (un->id != RTWN_MACID_UNDEFINED) {
		sc->node_list[un->id] = NULL;
		rtwn_cmd_sleepable(sc, &un->id, sizeof(un->id),
		    rtwn_set_media_status);
	}
	RTWN_NT_UNLOCK(sc);

	sc->sc_node_free(ni);
}

static void
rtwn_init_beacon_reg(struct rtwn_softc *sc)
{
	rtwn_write_1(sc, R92C_BCN_CTRL(0), R92C_BCN_CTRL_DIS_TSF_UDT0);
	rtwn_write_1(sc, R92C_BCN_CTRL(1), R92C_BCN_CTRL_DIS_TSF_UDT0);
	rtwn_write_2(sc, R92C_TBTT_PROHIBIT, 0x6404);
	rtwn_write_1(sc, R92C_DRVERLYINT, 0x05);
	rtwn_write_1(sc, R92C_BCNDMATIM, 0x02);
	rtwn_write_2(sc, R92C_BCNTCFG, 0x660f);
}

static int
rtwn_init(struct rtwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int i, error;

	RTWN_LOCK(sc);
	if (sc->sc_flags & RTWN_RUNNING) {
		RTWN_UNLOCK(sc);
		return (0);
	}
	sc->sc_flags |= RTWN_STARTED;

	/* Power on adapter. */
	error = rtwn_power_on(sc);
	if (error != 0)
		goto fail;

#ifndef RTWN_WITHOUT_UCODE
	/* Load 8051 microcode. */
	error = rtwn_load_firmware(sc);
	if (error == 0)
		sc->sc_flags |= RTWN_FW_LOADED;

	/* Init firmware commands ring. */
	sc->fwcur = 0;
#endif

	/* Initialize MAC block. */
	error = rtwn_mac_init(sc);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: error while initializing MAC block\n", __func__);
		goto fail;
	}

	/* Initialize DMA. */
	error = rtwn_dma_init(sc);
	if (error != 0)
		goto fail;

	/* Drop incorrect TX (USB). */
	rtwn_drop_incorrect_tx(sc);

	/* Set info size in Rx descriptors (in 64-bit words). */
	rtwn_write_1(sc, R92C_RX_DRVINFO_SZ, R92C_RX_DRVINFO_SZ_DEF);

	/* Init interrupts. */
	rtwn_init_intr(sc);

	for (i = 0; i < nitems(sc->vaps); i++) {
		struct rtwn_vap *uvp = sc->vaps[i];

		/* Set initial network type. */
		rtwn_set_mode(sc, R92C_MSR_NOLINK, i);

		if (uvp == NULL)
			continue;

		/* Set MAC address. */
		error = rtwn_set_macaddr(sc, uvp->vap.iv_myaddr, uvp->id);
		if (error != 0)
			goto fail;
	}

	/* Initialize Rx filter. */
	rtwn_rxfilter_init(sc);

	/* Set short/long retry limits. */
	rtwn_write_2(sc, R92C_RL,
	    SM(R92C_RL_SRL, 0x30) | SM(R92C_RL_LRL, 0x30));

	/* Initialize EDCA parameters. */
	rtwn_init_edca(sc);

	rtwn_setbits_1(sc, R92C_FWHW_TXQ_CTRL, 0,
	    R92C_FWHW_TXQ_CTRL_AMPDU_RTY_NEW);
	/* Set ACK timeout. */
	rtwn_write_1(sc, R92C_ACKTO, sc->ackto);

	/* Setup aggregation. */
	/* Tx aggregation. */
	rtwn_init_tx_agg(sc);
	rtwn_init_rx_agg(sc);

	/* Initialize beacon parameters. */
	rtwn_init_beacon_reg(sc);

	/* Init A-MPDU parameters. */
	rtwn_init_ampdu(sc);

	/* Init MACTXEN / MACRXEN after setting RxFF boundary. */
	rtwn_setbits_1(sc, R92C_CR, 0, R92C_CR_MACTXEN | R92C_CR_MACRXEN);

	/* Initialize BB/RF blocks. */
	rtwn_init_bb(sc);
	rtwn_init_rf(sc);

	/* Initialize wireless band. */
	rtwn_set_chan(sc, ic->ic_curchan);

	/* Clear per-station keys table. */
	rtwn_init_cam(sc);

	/* Enable decryption / encryption. */
	rtwn_init_seccfg(sc);

	/* Install static keys (if any). */
	for (i = 0; i < nitems(sc->vaps); i++) {
		if (sc->vaps[i] != NULL) {
			error = rtwn_init_static_keys(sc, sc->vaps[i]);
			if (error != 0)
				goto fail;
		}
	}

	/* Initialize antenna selection. */
	rtwn_init_antsel(sc);

	/* Enable hardware sequence numbering. */
	rtwn_write_1(sc, R92C_HWSEQ_CTRL, R92C_TX_QUEUE_ALL);

	/* Disable BAR. */
	rtwn_write_4(sc, R92C_BAR_MODE_CTRL, 0x0201ffff);

	/* NAV limit. */
	rtwn_write_1(sc, R92C_NAV_UPPER, 0);

	/* Initialize GPIO setting. */
	rtwn_setbits_1(sc, R92C_GPIO_MUXCFG, R92C_GPIO_MUXCFG_ENBT, 0);

	/* Initialize MRR. */
	rtwn_mrr_init(sc);

	/* Device-specific post initialization. */
	rtwn_post_init(sc);

	rtwn_start_xfers(sc);

#ifndef D4054
	callout_reset(&sc->sc_watchdog_to, hz, rtwn_watchdog, sc);
#endif

	sc->sc_flags |= RTWN_RUNNING;
fail:
	RTWN_UNLOCK(sc);

	return (error);
}

static void
rtwn_stop(struct rtwn_softc *sc)
{

	RTWN_LOCK(sc);
	if (!(sc->sc_flags & RTWN_STARTED)) {
		RTWN_UNLOCK(sc);
		return;
	}

#ifndef D4054
	callout_stop(&sc->sc_watchdog_to);
	sc->sc_tx_timer = 0;
#endif
	sc->sc_flags &= ~(RTWN_STARTED | RTWN_RUNNING | RTWN_FW_LOADED);
	sc->sc_flags &= ~RTWN_TEMP_MEASURED;
	sc->fwver = 0;
	sc->thcal_temp = 0;
	sc->cur_bcnq_id = RTWN_VAP_ID_INVALID;
	bzero(&sc->last_physt, sizeof(sc->last_physt));

#ifdef D4054
	ieee80211_tx_watchdog_stop(&sc->sc_ic);
#endif

	rtwn_abort_xfers(sc);
	rtwn_drain_mbufq(sc);
	rtwn_power_off(sc);
	rtwn_reset_lists(sc, NULL);
	RTWN_UNLOCK(sc);
}

MODULE_VERSION(rtwn, 2);
MODULE_DEPEND(rtwn, wlan, 1, 1, 1);
#ifndef RTWN_WITHOUT_UCODE
MODULE_DEPEND(rtwn, firmware, 1, 1, 1);
#endif
