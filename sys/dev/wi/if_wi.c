/*-
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Lucent WaveLAN/IEEE 802.11 PCMCIA driver.
 *
 * Original FreeBSD driver written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The WaveLAN/IEEE adapter is the second generation of the WaveLAN
 * from Lucent. Unlike the older cards, the new ones are programmed
 * entirely via a firmware-driven controller called the Hermes.
 * Unfortunately, Lucent will not release the Hermes programming manual
 * without an NDA (if at all). What they do release is an API library
 * called the HCF (Hardware Control Functions) which is supposed to
 * do the device-specific operations of a device driver for you. The
 * publicly available version of the HCF library (the 'HCF Light') is 
 * a) extremely gross, b) lacks certain features, particularly support
 * for 802.11 frames, and c) is contaminated by the GNU Public License.
 *
 * This driver does not use the HCF or HCF Light at all. Instead, it
 * programs the Hermes controller directly, using information gleaned
 * from the HCF Light code and corresponding documentation.
 *
 * This driver supports the ISA, PCMCIA and PCI versions of the Lucent
 * WaveLan cards (based on the Hermes chipset), as well as the newer
 * Prism 2 chipsets with firmware from Intersil and Symbol.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#define WI_HERMES_STATS_WAR	/* Work around stats counter bug. */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/random.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/atomic.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_radiotap.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>

#include <net/bpf.h>

#include <dev/wi/if_wavelan_ieee.h>
#include <dev/wi/if_wireg.h>
#include <dev/wi/if_wivar.h>

static struct ieee80211vap *wi_vap_create(struct ieee80211com *,
		    const char [IFNAMSIZ], int, enum ieee80211_opmode, int,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    const uint8_t [IEEE80211_ADDR_LEN]);
static void wi_vap_delete(struct ieee80211vap *vap);
static int  wi_transmit(struct ieee80211com *, struct mbuf *);
static void wi_start(struct wi_softc *);
static int  wi_start_tx(struct wi_softc *, struct wi_frame *, struct mbuf *);
static int  wi_raw_xmit(struct ieee80211_node *, struct mbuf *,
		const struct ieee80211_bpf_params *);
static int  wi_newstate_sta(struct ieee80211vap *, enum ieee80211_state, int);
static int  wi_newstate_hostap(struct ieee80211vap *, enum ieee80211_state,
		int);
static void wi_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m,
		int subtype, const struct ieee80211_rx_stats *rxs,
		int rssi, int nf);
static int  wi_reset(struct wi_softc *);
static void wi_watchdog(void *);
static void wi_parent(struct ieee80211com *);
static void wi_media_status(struct ifnet *, struct ifmediareq *);
static void wi_rx_intr(struct wi_softc *);
static void wi_tx_intr(struct wi_softc *);
static void wi_tx_ex_intr(struct wi_softc *);

static void wi_info_intr(struct wi_softc *);

static int  wi_write_txrate(struct wi_softc *, struct ieee80211vap *);
static int  wi_write_wep(struct wi_softc *, struct ieee80211vap *);
static int  wi_write_multi(struct wi_softc *);
static void wi_update_mcast(struct ieee80211com *);
static void wi_update_promisc(struct ieee80211com *);
static int  wi_alloc_fid(struct wi_softc *, int, int *);
static void wi_read_nicid(struct wi_softc *);
static int  wi_write_ssid(struct wi_softc *, int, u_int8_t *, int);

static int  wi_cmd(struct wi_softc *, int, int, int, int);
static int  wi_seek_bap(struct wi_softc *, int, int);
static int  wi_read_bap(struct wi_softc *, int, int, void *, int);
static int  wi_write_bap(struct wi_softc *, int, int, const void *, int);
static int  wi_mwrite_bap(struct wi_softc *, int, int, struct mbuf *, int);
static int  wi_read_rid(struct wi_softc *, int, void *, int *);
static int  wi_write_rid(struct wi_softc *, int, const void *, int);
static int  wi_write_appie(struct wi_softc *, int, const struct ieee80211_appie *);
static u_int16_t wi_read_chanmask(struct wi_softc *);

static void wi_scan_start(struct ieee80211com *);
static void wi_scan_end(struct ieee80211com *);
static void wi_getradiocaps(struct ieee80211com *, int, int *,
		struct ieee80211_channel[]);
static void wi_set_channel(struct ieee80211com *);
	
static __inline int
wi_write_val(struct wi_softc *sc, int rid, u_int16_t val)
{

	val = htole16(val);
	return wi_write_rid(sc, rid, &val, sizeof(val));
}

static SYSCTL_NODE(_hw, OID_AUTO, wi, CTLFLAG_RD, 0,
	    "Wireless driver parameters");

static	struct timeval lasttxerror;	/* time of last tx error msg */
static	int curtxeps;			/* current tx error msgs/sec */
static	int wi_txerate = 0;		/* tx error rate: max msgs/sec */
SYSCTL_INT(_hw_wi, OID_AUTO, txerate, CTLFLAG_RW, &wi_txerate,
	    0, "max tx error msgs/sec; 0 to disable msgs");

#define	WI_DEBUG
#ifdef WI_DEBUG
static	int wi_debug = 0;
SYSCTL_INT(_hw_wi, OID_AUTO, debug, CTLFLAG_RW, &wi_debug,
	    0, "control debugging printfs");
#define	DPRINTF(X)	if (wi_debug) printf X
#else
#define	DPRINTF(X)
#endif

#define WI_INTRS	(WI_EV_RX | WI_EV_ALLOC | WI_EV_INFO)

struct wi_card_ident wi_card_ident[] = {
	/* CARD_ID			CARD_NAME		FIRM_TYPE */
	{ WI_NIC_LUCENT_ID,		WI_NIC_LUCENT_STR,	WI_LUCENT },
	{ WI_NIC_SONY_ID,		WI_NIC_SONY_STR,	WI_LUCENT },
	{ WI_NIC_LUCENT_EMB_ID,		WI_NIC_LUCENT_EMB_STR,	WI_LUCENT },
	{ WI_NIC_EVB2_ID,		WI_NIC_EVB2_STR,	WI_INTERSIL },
	{ WI_NIC_HWB3763_ID,		WI_NIC_HWB3763_STR,	WI_INTERSIL },
	{ WI_NIC_HWB3163_ID,		WI_NIC_HWB3163_STR,	WI_INTERSIL },
	{ WI_NIC_HWB3163B_ID,		WI_NIC_HWB3163B_STR,	WI_INTERSIL },
	{ WI_NIC_EVB3_ID,		WI_NIC_EVB3_STR,	WI_INTERSIL },
	{ WI_NIC_HWB1153_ID,		WI_NIC_HWB1153_STR,	WI_INTERSIL },
	{ WI_NIC_P2_SST_ID,		WI_NIC_P2_SST_STR,	WI_INTERSIL },
	{ WI_NIC_EVB2_SST_ID,		WI_NIC_EVB2_SST_STR,	WI_INTERSIL },
	{ WI_NIC_3842_EVA_ID,		WI_NIC_3842_EVA_STR,	WI_INTERSIL },
	{ WI_NIC_3842_PCMCIA_AMD_ID,	WI_NIC_3842_PCMCIA_STR,	WI_INTERSIL },
	{ WI_NIC_3842_PCMCIA_SST_ID,	WI_NIC_3842_PCMCIA_STR,	WI_INTERSIL },
	{ WI_NIC_3842_PCMCIA_ATL_ID,	WI_NIC_3842_PCMCIA_STR,	WI_INTERSIL },
	{ WI_NIC_3842_PCMCIA_ATS_ID,	WI_NIC_3842_PCMCIA_STR,	WI_INTERSIL },
	{ WI_NIC_3842_MINI_AMD_ID,	WI_NIC_3842_MINI_STR,	WI_INTERSIL },
	{ WI_NIC_3842_MINI_SST_ID,	WI_NIC_3842_MINI_STR,	WI_INTERSIL },
	{ WI_NIC_3842_MINI_ATL_ID,	WI_NIC_3842_MINI_STR,	WI_INTERSIL },
	{ WI_NIC_3842_MINI_ATS_ID,	WI_NIC_3842_MINI_STR,	WI_INTERSIL },
	{ WI_NIC_3842_PCI_AMD_ID,	WI_NIC_3842_PCI_STR,	WI_INTERSIL },
	{ WI_NIC_3842_PCI_SST_ID,	WI_NIC_3842_PCI_STR,	WI_INTERSIL },
	{ WI_NIC_3842_PCI_ATS_ID,	WI_NIC_3842_PCI_STR,	WI_INTERSIL },
	{ WI_NIC_3842_PCI_ATL_ID,	WI_NIC_3842_PCI_STR,	WI_INTERSIL },
	{ WI_NIC_P3_PCMCIA_AMD_ID,	WI_NIC_P3_PCMCIA_STR,	WI_INTERSIL },
	{ WI_NIC_P3_PCMCIA_SST_ID,	WI_NIC_P3_PCMCIA_STR,	WI_INTERSIL },
	{ WI_NIC_P3_PCMCIA_ATL_ID,	WI_NIC_P3_PCMCIA_STR,	WI_INTERSIL },
	{ WI_NIC_P3_PCMCIA_ATS_ID,	WI_NIC_P3_PCMCIA_STR,	WI_INTERSIL },
	{ WI_NIC_P3_MINI_AMD_ID,	WI_NIC_P3_MINI_STR,	WI_INTERSIL },
	{ WI_NIC_P3_MINI_SST_ID,	WI_NIC_P3_MINI_STR,	WI_INTERSIL },
	{ WI_NIC_P3_MINI_ATL_ID,	WI_NIC_P3_MINI_STR,	WI_INTERSIL },
	{ WI_NIC_P3_MINI_ATS_ID,	WI_NIC_P3_MINI_STR,	WI_INTERSIL },
	{ 0,	NULL,	0 },
};

static char *wi_firmware_names[] = { "none", "Hermes", "Intersil", "Symbol" };

devclass_t wi_devclass;

int
wi_attach(device_t dev)
{
	struct wi_softc	*sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	int i, nrates, buflen;
	u_int16_t val;
	u_int8_t ratebuf[2 + IEEE80211_RATE_SIZE];
	struct ieee80211_rateset *rs;
	struct sysctl_ctx_list *sctx;
	struct sysctl_oid *soid;
	static const u_int8_t empty_macaddr[IEEE80211_ADDR_LEN] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	int error;

	sc->sc_firmware_type = WI_NOTYPE;
	sc->wi_cmd_count = 500;
	/* Reset the NIC. */
	if (wi_reset(sc) != 0) {
		wi_free(dev);
		return ENXIO;		/* XXX */
	}

	/* Read NIC identification */
	wi_read_nicid(sc);
	switch (sc->sc_firmware_type) {
	case WI_LUCENT:
		if (sc->sc_sta_firmware_ver < 60006)
			goto reject;
		break;
	case WI_INTERSIL:
		if (sc->sc_sta_firmware_ver < 800)
			goto reject;
		break;
	default:
	reject:
		device_printf(dev, "Sorry, this card is not supported "
		    "(type %d, firmware ver %d)\n",
		    sc->sc_firmware_type, sc->sc_sta_firmware_ver);
		wi_free(dev);
		return EOPNOTSUPP; 
	}

	/* Export info about the device via sysctl */
	sctx = device_get_sysctl_ctx(dev);
	soid = device_get_sysctl_tree(dev);
	SYSCTL_ADD_STRING(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
	    "firmware_type", CTLFLAG_RD,
	    wi_firmware_names[sc->sc_firmware_type], 0,
	    "Firmware type string");
	SYSCTL_ADD_INT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "sta_version",
	    CTLFLAG_RD, &sc->sc_sta_firmware_ver, 0,
	    "Station Firmware version");
	if (sc->sc_firmware_type == WI_INTERSIL)
		SYSCTL_ADD_INT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
		    "pri_version", CTLFLAG_RD, &sc->sc_pri_firmware_ver, 0,
		    "Primary Firmware version");
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "nic_id",
	    CTLFLAG_RD, &sc->sc_nic_id, 0, "NIC id");
	SYSCTL_ADD_STRING(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "nic_name",
	    CTLFLAG_RD, sc->sc_nic_name, 0, "NIC name");

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF | MTX_RECURSE);
	callout_init_mtx(&sc->sc_watchdog, &sc->sc_mtx, 0);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	/*
	 * Read the station address.
	 * And do it twice. I've seen PRISM-based cards that return
	 * an error when trying to read it the first time, which causes
	 * the probe to fail.
	 */
	buflen = IEEE80211_ADDR_LEN;
	error = wi_read_rid(sc, WI_RID_MAC_NODE, &ic->ic_macaddr, &buflen);
	if (error != 0) {
		buflen = IEEE80211_ADDR_LEN;
		error = wi_read_rid(sc, WI_RID_MAC_NODE, &ic->ic_macaddr,
		    &buflen);
	}
	if (error || IEEE80211_ADDR_EQ(&ic->ic_macaddr, empty_macaddr)) {
		if (error != 0)
			device_printf(dev, "mac read failed %d\n", error);
		else {
			device_printf(dev, "mac read failed (all zeros)\n");
			error = ENXIO;
		}
		wi_free(dev);
		return (error);
	}

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(dev);
	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_caps = IEEE80211_C_STA
		    | IEEE80211_C_PMGT
		    | IEEE80211_C_MONITOR
		    ;

	/*
	 * Query the card for available channels and setup the
	 * channel table.  We assume these are all 11b channels.
	 */
	sc->sc_chanmask = wi_read_chanmask(sc);
	wi_getradiocaps(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	/*
	 * Set flags based on firmware version.
	 */
	switch (sc->sc_firmware_type) {
	case WI_LUCENT:
		sc->sc_ntxbuf = 1;
		ic->ic_caps |= IEEE80211_C_IBSS;

		sc->sc_ibss_port = WI_PORTTYPE_BSS;
		sc->sc_monitor_port = WI_PORTTYPE_ADHOC;
		sc->sc_min_rssi = WI_LUCENT_MIN_RSSI;
		sc->sc_max_rssi = WI_LUCENT_MAX_RSSI;
		sc->sc_dbm_offset = WI_LUCENT_DBM_OFFSET;
		break;
	case WI_INTERSIL:
		sc->sc_ntxbuf = WI_NTXBUF;
		sc->sc_flags |= WI_FLAGS_HAS_FRAGTHR
			     |  WI_FLAGS_HAS_ROAMING;
		/*
		 * Old firmware are slow, so give peace a chance.
		 */
		if (sc->sc_sta_firmware_ver < 10000)
			sc->wi_cmd_count = 5000;
		if (sc->sc_sta_firmware_ver > 10101)
			sc->sc_flags |= WI_FLAGS_HAS_DBMADJUST;
		ic->ic_caps |= IEEE80211_C_IBSS;
		/*
		 * version 0.8.3 and newer are the only ones that are known
		 * to currently work.  Earlier versions can be made to work,
		 * at least according to the Linux driver but we require
		 * monitor mode so this is irrelevant.
		 */
		ic->ic_caps |= IEEE80211_C_HOSTAP;
		if (sc->sc_sta_firmware_ver >= 10603)
			sc->sc_flags |= WI_FLAGS_HAS_ENHSECURITY;
		if (sc->sc_sta_firmware_ver >= 10700) {
			/*
			 * 1.7.0+ have the necessary support for sta mode WPA.
			 */
			sc->sc_flags |= WI_FLAGS_HAS_WPASUPPORT;
			ic->ic_caps |= IEEE80211_C_WPA;
		}

		sc->sc_ibss_port = WI_PORTTYPE_IBSS;
		sc->sc_monitor_port = WI_PORTTYPE_APSILENT;
		sc->sc_min_rssi = WI_PRISM_MIN_RSSI;
		sc->sc_max_rssi = WI_PRISM_MAX_RSSI;
		sc->sc_dbm_offset = WI_PRISM_DBM_OFFSET;
		break;
	}

	/*
	 * Find out if we support WEP on this card.
	 */
	buflen = sizeof(val);
	if (wi_read_rid(sc, WI_RID_WEP_AVAIL, &val, &buflen) == 0 &&
	    val != htole16(0))
		ic->ic_cryptocaps |= IEEE80211_CRYPTO_WEP;

	/* Find supported rates. */
	buflen = sizeof(ratebuf);
	rs = &ic->ic_sup_rates[IEEE80211_MODE_11B];
	if (wi_read_rid(sc, WI_RID_DATA_RATES, ratebuf, &buflen) == 0) {
		nrates = le16toh(*(u_int16_t *)ratebuf);
		if (nrates > IEEE80211_RATE_MAXSIZE)
			nrates = IEEE80211_RATE_MAXSIZE;
		rs->rs_nrates = 0;
		for (i = 0; i < nrates; i++)
			if (ratebuf[2+i])
				rs->rs_rates[rs->rs_nrates++] = ratebuf[2+i];
	} else {
		/* XXX fallback on error? */
	}

	buflen = sizeof(val);
	if ((sc->sc_flags & WI_FLAGS_HAS_DBMADJUST) &&
	    wi_read_rid(sc, WI_RID_DBM_ADJUST, &val, &buflen) == 0) {
		sc->sc_dbm_offset = le16toh(val);
	}

	sc->sc_portnum = WI_DEFAULT_PORT;

	ieee80211_ifattach(ic);
	ic->ic_raw_xmit = wi_raw_xmit;
	ic->ic_scan_start = wi_scan_start;
	ic->ic_scan_end = wi_scan_end;
	ic->ic_getradiocaps = wi_getradiocaps;
	ic->ic_set_channel = wi_set_channel;
	ic->ic_vap_create = wi_vap_create;
	ic->ic_vap_delete = wi_vap_delete;
	ic->ic_update_mcast = wi_update_mcast;
	ic->ic_update_promisc = wi_update_promisc;
	ic->ic_transmit = wi_transmit;
	ic->ic_parent = wi_parent;

	ieee80211_radiotap_attach(ic,
	    &sc->sc_tx_th.wt_ihdr, sizeof(sc->sc_tx_th),
		WI_TX_RADIOTAP_PRESENT,
	    &sc->sc_rx_th.wr_ihdr, sizeof(sc->sc_rx_th),
		WI_RX_RADIOTAP_PRESENT);

	if (bootverbose)
		ieee80211_announce(ic);

	error = bus_setup_intr(dev, sc->irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, wi_intr, sc, &sc->wi_intrhand);
	if (error) {
		device_printf(dev, "bus_setup_intr() failed! (%d)\n", error);
		ieee80211_ifdetach(ic);
		wi_free(dev);
		return error;
	}

	return (0);
}

int
wi_detach(device_t dev)
{
	struct wi_softc	*sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;

	WI_LOCK(sc);

	/* check if device was removed */
	sc->wi_gone |= !bus_child_present(dev);

	wi_stop(sc, 0);
	WI_UNLOCK(sc);
	ieee80211_ifdetach(ic);

	bus_teardown_intr(dev, sc->irq, sc->wi_intrhand);
	wi_free(dev);
	mbufq_drain(&sc->sc_snd);
	mtx_destroy(&sc->sc_mtx);
	return (0);
}

static struct ieee80211vap *
wi_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct wi_softc *sc = ic->ic_softc;
	struct wi_vap *wvp;
	struct ieee80211vap *vap;

	if (!TAILQ_EMPTY(&ic->ic_vaps))		/* only one at a time */
		return NULL;
	wvp = malloc(sizeof(struct wi_vap), M_80211_VAP, M_WAITOK | M_ZERO);

	vap = &wvp->wv_vap;
	ieee80211_vap_setup(ic, vap, name, unit, opmode, flags, bssid);

	vap->iv_max_aid = WI_MAX_AID;

	switch (opmode) {
	case IEEE80211_M_STA:
		sc->sc_porttype = WI_PORTTYPE_BSS;
		wvp->wv_newstate = vap->iv_newstate;
		vap->iv_newstate = wi_newstate_sta;
		/* need to filter mgt frames to avoid confusing state machine */
		wvp->wv_recv_mgmt = vap->iv_recv_mgmt;
		vap->iv_recv_mgmt = wi_recv_mgmt;
		break;
	case IEEE80211_M_IBSS:
		sc->sc_porttype = sc->sc_ibss_port;
		wvp->wv_newstate = vap->iv_newstate;
		vap->iv_newstate = wi_newstate_sta;
		break;
	case IEEE80211_M_AHDEMO:
		sc->sc_porttype = WI_PORTTYPE_ADHOC;
		break;
	case IEEE80211_M_HOSTAP:
		sc->sc_porttype = WI_PORTTYPE_HOSTAP;
		wvp->wv_newstate = vap->iv_newstate;
		vap->iv_newstate = wi_newstate_hostap;
		break;
	case IEEE80211_M_MONITOR:
		sc->sc_porttype = sc->sc_monitor_port;
		break;
	default:
		break;
	}

	/* complete setup */
	ieee80211_vap_attach(vap, ieee80211_media_change, wi_media_status, mac);
	ic->ic_opmode = opmode;
	return vap;
}

static void
wi_vap_delete(struct ieee80211vap *vap)
{
	struct wi_vap *wvp = WI_VAP(vap);

	ieee80211_vap_detach(vap);
	free(wvp, M_80211_VAP);
}

int
wi_shutdown(device_t dev)
{
	struct wi_softc *sc = device_get_softc(dev);

	WI_LOCK(sc);
	wi_stop(sc, 1);
	WI_UNLOCK(sc);
	return (0);
}

void
wi_intr(void *arg)
{
	struct wi_softc *sc = arg;
	u_int16_t status;

	WI_LOCK(sc);

	if (sc->wi_gone || !sc->sc_enabled ||
	    (sc->sc_flags & WI_FLAGS_RUNNING) == 0) {
		CSR_WRITE_2(sc, WI_INT_EN, 0);
		CSR_WRITE_2(sc, WI_EVENT_ACK, 0xFFFF);
		WI_UNLOCK(sc);
		return;
	}

	/* Disable interrupts. */
	CSR_WRITE_2(sc, WI_INT_EN, 0);

	status = CSR_READ_2(sc, WI_EVENT_STAT);
	if (status & WI_EV_RX)
		wi_rx_intr(sc);
	if (status & WI_EV_ALLOC)
		wi_tx_intr(sc);
	if (status & WI_EV_TX_EXC)
		wi_tx_ex_intr(sc);
	if (status & WI_EV_INFO)
		wi_info_intr(sc);
	if (mbufq_first(&sc->sc_snd) != NULL)
		wi_start(sc);

	/* Re-enable interrupts. */
	CSR_WRITE_2(sc, WI_INT_EN, WI_INTRS);

	WI_UNLOCK(sc);

	return;
}

static void
wi_enable(struct wi_softc *sc)
{
	/* Enable interrupts */
	CSR_WRITE_2(sc, WI_INT_EN, WI_INTRS);

	/* enable port */
	wi_cmd(sc, WI_CMD_ENABLE | sc->sc_portnum, 0, 0, 0);
	sc->sc_enabled = 1;
}

static int
wi_setup_locked(struct wi_softc *sc, int porttype, int mode,
	const uint8_t mac[IEEE80211_ADDR_LEN])
{
	int i;

	wi_reset(sc);

	wi_write_val(sc, WI_RID_PORTTYPE, porttype);
	wi_write_val(sc, WI_RID_CREATE_IBSS, mode);
	wi_write_val(sc, WI_RID_MAX_DATALEN, 2304);
	/* XXX IEEE80211_BPF_NOACK wants 0 */
	wi_write_val(sc, WI_RID_ALT_RETRY_CNT, 2);
	if (sc->sc_flags & WI_FLAGS_HAS_ROAMING)
		wi_write_val(sc, WI_RID_ROAMING_MODE, 3); /* NB: disabled */

	wi_write_rid(sc, WI_RID_MAC_NODE, mac, IEEE80211_ADDR_LEN);

	/* Allocate fids for the card */
	sc->sc_buflen = IEEE80211_MAX_LEN + sizeof(struct wi_frame);
	for (i = 0; i < sc->sc_ntxbuf; i++) {
		int error = wi_alloc_fid(sc, sc->sc_buflen,
		    &sc->sc_txd[i].d_fid);
		if (error) {
			device_printf(sc->sc_dev,
			    "tx buffer allocation failed (error %u)\n",
			    error);
			return error;
		}
		sc->sc_txd[i].d_len = 0;
	}
	sc->sc_txcur = sc->sc_txnext = 0;

	return 0;
}

void
wi_init(struct wi_softc *sc)
{
	int wasenabled;

	WI_LOCK_ASSERT(sc);

	wasenabled = sc->sc_enabled;
	if (wasenabled)
		wi_stop(sc, 1);

	if (wi_setup_locked(sc, sc->sc_porttype, 3,
	    sc->sc_ic.ic_macaddr) != 0) {
		device_printf(sc->sc_dev, "interface not running\n");
		wi_stop(sc, 1);
		return;
	}

	sc->sc_flags |= WI_FLAGS_RUNNING;

	callout_reset(&sc->sc_watchdog, hz, wi_watchdog, sc);

	wi_enable(sc);			/* Enable desired port */
}

void
wi_stop(struct wi_softc *sc, int disable)
{

	WI_LOCK_ASSERT(sc);

	if (sc->sc_enabled && !sc->wi_gone) {
		CSR_WRITE_2(sc, WI_INT_EN, 0);
		wi_cmd(sc, WI_CMD_DISABLE | sc->sc_portnum, 0, 0, 0);
		if (disable)
			sc->sc_enabled = 0;
	} else if (sc->wi_gone && disable)	/* gone --> not enabled */
		sc->sc_enabled = 0;

	callout_stop(&sc->sc_watchdog);
	sc->sc_tx_timer = 0;
	sc->sc_false_syns = 0;

	sc->sc_flags &= ~WI_FLAGS_RUNNING;
}

static void
wi_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct wi_softc *sc = ic->ic_softc;
	u_int8_t bands[IEEE80211_MODE_BYTES];
	int i;

	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);

	for (i = 1; i < 16; i++) {
		if (sc->sc_chanmask & (1 << i)) {
			/* XXX txpowers? */
			ieee80211_add_channel(chans, maxchans, nchans,
			    i, 0, 0, 0, bands);
		}
	}
}

static void
wi_set_channel(struct ieee80211com *ic)
{
	struct wi_softc *sc = ic->ic_softc;

	DPRINTF(("%s: channel %d, %sscanning\n", __func__,
	    ieee80211_chan2ieee(ic, ic->ic_curchan),
	    ic->ic_flags & IEEE80211_F_SCAN ? "" : "!"));

	WI_LOCK(sc);
	wi_write_val(sc, WI_RID_OWN_CHNL,
	    ieee80211_chan2ieee(ic, ic->ic_curchan));
	WI_UNLOCK(sc);
}

static void
wi_scan_start(struct ieee80211com *ic)
{
	struct wi_softc *sc = ic->ic_softc;
	struct ieee80211_scan_state *ss = ic->ic_scan;

	DPRINTF(("%s\n", __func__));

	WI_LOCK(sc);
	/*
	 * Switch device to monitor mode.
	 */
	wi_write_val(sc, WI_RID_PORTTYPE, sc->sc_monitor_port);
	if (sc->sc_firmware_type == WI_INTERSIL) {
		wi_cmd(sc, WI_CMD_DISABLE | WI_PORT0, 0, 0, 0);
		wi_cmd(sc, WI_CMD_ENABLE | WI_PORT0, 0, 0, 0);
	}
	/* force full dwell time to compensate for firmware overhead */
	ss->ss_mindwell = ss->ss_maxdwell = msecs_to_ticks(400);
	WI_UNLOCK(sc);

}

static void
wi_scan_end(struct ieee80211com *ic)
{
	struct wi_softc *sc = ic->ic_softc;

	DPRINTF(("%s: restore port type %d\n", __func__, sc->sc_porttype));

	WI_LOCK(sc);
	wi_write_val(sc, WI_RID_PORTTYPE, sc->sc_porttype);
	if (sc->sc_firmware_type == WI_INTERSIL) {
		wi_cmd(sc, WI_CMD_DISABLE | WI_PORT0, 0, 0, 0);
		wi_cmd(sc, WI_CMD_ENABLE | WI_PORT0, 0, 0, 0);
	}
	WI_UNLOCK(sc);
}

static void
wi_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m,
	int subtype, const struct ieee80211_rx_stats *rxs, int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;

	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_AUTH:
	case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
	case IEEE80211_FC0_SUBTYPE_REASSOC_RESP:
		/* NB: filter frames that trigger state changes */
		return;
	}
	WI_VAP(vap)->wv_recv_mgmt(ni, m, subtype, rxs, rssi, nf);
}

static int
wi_newstate_sta(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *bss;
	struct wi_softc *sc = ic->ic_softc;

	DPRINTF(("%s: %s -> %s\n", __func__,
		ieee80211_state_name[vap->iv_state],
		ieee80211_state_name[nstate]));

	if (nstate == IEEE80211_S_AUTH) {
		WI_LOCK(sc);
		wi_setup_locked(sc, WI_PORTTYPE_BSS, 3, vap->iv_myaddr);

		if (vap->iv_flags & IEEE80211_F_PMGTON) {
			wi_write_val(sc, WI_RID_MAX_SLEEP, ic->ic_lintval);
			wi_write_val(sc, WI_RID_PM_ENABLED, 1);
		}
		wi_write_val(sc, WI_RID_RTS_THRESH, vap->iv_rtsthreshold);
		if (sc->sc_flags & WI_FLAGS_HAS_FRAGTHR)
			wi_write_val(sc, WI_RID_FRAG_THRESH,
			    vap->iv_fragthreshold);
		wi_write_txrate(sc, vap);

		bss = vap->iv_bss;
		wi_write_ssid(sc, WI_RID_DESIRED_SSID, bss->ni_essid, bss->ni_esslen);
		wi_write_val(sc, WI_RID_OWN_CHNL,
		    ieee80211_chan2ieee(ic, bss->ni_chan));

		/* Configure WEP. */
		if (ic->ic_cryptocaps & IEEE80211_CRYPTO_WEP)
			wi_write_wep(sc, vap);
		else
			sc->sc_encryption = 0;

		if ((sc->sc_flags & WI_FLAGS_HAS_WPASUPPORT) &&
		    (vap->iv_flags & IEEE80211_F_WPA)) {
			wi_write_val(sc, WI_RID_WPA_HANDLING, 1);
			if (vap->iv_appie_wpa != NULL)
				wi_write_appie(sc, WI_RID_WPA_DATA,
				    vap->iv_appie_wpa);
		}

		wi_enable(sc);		/* enable port */

		/* Lucent firmware does not support the JOIN RID. */
		if (sc->sc_firmware_type == WI_INTERSIL) {
			struct wi_joinreq join;

			memset(&join, 0, sizeof(join));
			IEEE80211_ADDR_COPY(&join.wi_bssid, bss->ni_bssid);
			join.wi_chan = htole16(
			    ieee80211_chan2ieee(ic, bss->ni_chan));
			wi_write_rid(sc, WI_RID_JOIN_REQ, &join, sizeof(join));
		}
		WI_UNLOCK(sc);

		/*
		 * NB: don't go through 802.11 layer, it'll send auth frame;
		 * instead we drive the state machine from the link status
		 * notification we get on association.
		 */
		vap->iv_state = nstate;
		return (0);
	}
	return WI_VAP(vap)->wv_newstate(vap, nstate, arg);
}

static int
wi_newstate_hostap(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *bss;
	struct wi_softc *sc = ic->ic_softc;
	int error;

	DPRINTF(("%s: %s -> %s\n", __func__,
		ieee80211_state_name[vap->iv_state],
		ieee80211_state_name[nstate]));

	error = WI_VAP(vap)->wv_newstate(vap, nstate, arg);
	if (error == 0 && nstate == IEEE80211_S_RUN) {
		WI_LOCK(sc);
		wi_setup_locked(sc, WI_PORTTYPE_HOSTAP, 0, vap->iv_myaddr);

		bss = vap->iv_bss;
		wi_write_ssid(sc, WI_RID_OWN_SSID,
		    bss->ni_essid, bss->ni_esslen);
		wi_write_val(sc, WI_RID_OWN_CHNL,
		    ieee80211_chan2ieee(ic, bss->ni_chan));
		wi_write_val(sc, WI_RID_BASIC_RATE, 0x3);
		wi_write_val(sc, WI_RID_SUPPORT_RATE, 0xf);
		wi_write_txrate(sc, vap);

		wi_write_val(sc, WI_RID_OWN_BEACON_INT, bss->ni_intval);
		wi_write_val(sc, WI_RID_DTIM_PERIOD, vap->iv_dtim_period);

		wi_write_val(sc, WI_RID_RTS_THRESH, vap->iv_rtsthreshold);
		if (sc->sc_flags & WI_FLAGS_HAS_FRAGTHR)
			wi_write_val(sc, WI_RID_FRAG_THRESH,
			    vap->iv_fragthreshold);

		if ((sc->sc_flags & WI_FLAGS_HAS_ENHSECURITY) &&
		    (vap->iv_flags & IEEE80211_F_HIDESSID)) {
			/*
			 * bit 0 means hide SSID in beacons,
			 * bit 1 means don't respond to bcast probe req
			 */
			wi_write_val(sc, WI_RID_ENH_SECURITY, 0x3);
		}

		if ((sc->sc_flags & WI_FLAGS_HAS_WPASUPPORT) &&
		    (vap->iv_flags & IEEE80211_F_WPA) && 
		    vap->iv_appie_wpa != NULL)
			wi_write_appie(sc, WI_RID_WPA_DATA, vap->iv_appie_wpa);

		wi_write_val(sc, WI_RID_PROMISC, 0);

		/* Configure WEP. */
		if (ic->ic_cryptocaps & IEEE80211_CRYPTO_WEP)
			wi_write_wep(sc, vap);
		else
			sc->sc_encryption = 0;

		wi_enable(sc);		/* enable port */
		WI_UNLOCK(sc);
	}
	return error;
}

static int
wi_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct wi_softc *sc = ic->ic_softc;
	int error;

	WI_LOCK(sc);
	if ((sc->sc_flags & WI_FLAGS_RUNNING) == 0) {
		WI_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		WI_UNLOCK(sc);
		return (error);
	}
	wi_start(sc);
	WI_UNLOCK(sc);
	return (0);
}

static void
wi_start(struct wi_softc *sc)
{
	struct ieee80211_node *ni;
	struct ieee80211_frame *wh;
	struct mbuf *m0;
	struct ieee80211_key *k;
	struct wi_frame frmhdr;
	const struct llc *llc;
	int cur;

	WI_LOCK_ASSERT(sc);

	if (sc->wi_gone)
		return;

	memset(&frmhdr, 0, sizeof(frmhdr));
	cur = sc->sc_txnext;
	while (sc->sc_txd[cur].d_len == 0 &&
	    (m0 = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *) m0->m_pkthdr.rcvif;

		/* reconstruct 802.3 header */
		wh = mtod(m0, struct ieee80211_frame *);
		switch (wh->i_fc[1]) {
		case IEEE80211_FC1_DIR_TODS:
			IEEE80211_ADDR_COPY(frmhdr.wi_ehdr.ether_shost,
			    wh->i_addr2);
			IEEE80211_ADDR_COPY(frmhdr.wi_ehdr.ether_dhost,
			    wh->i_addr3);
			break;
		case IEEE80211_FC1_DIR_NODS:
			IEEE80211_ADDR_COPY(frmhdr.wi_ehdr.ether_shost,
			    wh->i_addr2);
			IEEE80211_ADDR_COPY(frmhdr.wi_ehdr.ether_dhost,
			    wh->i_addr1);
			break;
		case IEEE80211_FC1_DIR_FROMDS:
			IEEE80211_ADDR_COPY(frmhdr.wi_ehdr.ether_shost,
			    wh->i_addr3);
			IEEE80211_ADDR_COPY(frmhdr.wi_ehdr.ether_dhost,
			    wh->i_addr1);
			break;
		}
		llc = (const struct llc *)(
		    mtod(m0, const uint8_t *) + ieee80211_hdrsize(wh));
		frmhdr.wi_ehdr.ether_type = llc->llc_snap.ether_type;
		frmhdr.wi_tx_ctl = htole16(WI_ENC_TX_802_11|WI_TXCNTL_TX_EX);
		if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
			k = ieee80211_crypto_encap(ni, m0);
			if (k == NULL) {
				ieee80211_free_node(ni);
				m_freem(m0);
				continue;
			}
			frmhdr.wi_tx_ctl |= htole16(WI_TXCNTL_NOCRYPT);
		}

		if (ieee80211_radiotap_active_vap(ni->ni_vap)) {
			sc->sc_tx_th.wt_rate = ni->ni_txrate;
			ieee80211_radiotap_tx(ni->ni_vap, m0);
		}

		m_copydata(m0, 0, sizeof(struct ieee80211_frame),
		    (caddr_t)&frmhdr.wi_whdr);
		m_adj(m0, sizeof(struct ieee80211_frame));
		frmhdr.wi_dat_len = htole16(m0->m_pkthdr.len);
		ieee80211_free_node(ni);
		if (wi_start_tx(sc, &frmhdr, m0))
			continue;

		sc->sc_txnext = cur = (cur + 1) % sc->sc_ntxbuf;
	}
}

static int
wi_start_tx(struct wi_softc *sc, struct wi_frame *frmhdr, struct mbuf *m0)
{
	int cur = sc->sc_txnext;
	int fid, off, error;

	fid = sc->sc_txd[cur].d_fid;
	off = sizeof(*frmhdr);
	error = wi_write_bap(sc, fid, 0, frmhdr, sizeof(*frmhdr)) != 0
	     || wi_mwrite_bap(sc, fid, off, m0, m0->m_pkthdr.len) != 0;
	m_freem(m0);
	if (error) {
		counter_u64_add(sc->sc_ic.ic_oerrors, 1);
		return -1;
	}
	sc->sc_txd[cur].d_len = off;
	if (sc->sc_txcur == cur) {
		if (wi_cmd(sc, WI_CMD_TX | WI_RECLAIM, fid, 0, 0)) {
			device_printf(sc->sc_dev, "xmit failed\n");
			sc->sc_txd[cur].d_len = 0;
			return -1;
		}
		sc->sc_tx_timer = 5;
	}
	return 0;
}

static int
wi_raw_xmit(struct ieee80211_node *ni, struct mbuf *m0,
	    const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct wi_softc	*sc = ic->ic_softc;
	struct ieee80211_key *k;
	struct ieee80211_frame *wh;
	struct wi_frame frmhdr;
	int cur;
	int rc = 0;

	WI_LOCK(sc);

	if (sc->wi_gone) {
		rc = ENETDOWN;
		goto out;
	}
	memset(&frmhdr, 0, sizeof(frmhdr));
	cur = sc->sc_txnext;
	if (sc->sc_txd[cur].d_len != 0) {
		rc = ENOBUFS;
		goto out;
	}
	m0->m_pkthdr.rcvif = NULL;

	m_copydata(m0, 4, ETHER_ADDR_LEN * 2,
	    (caddr_t)&frmhdr.wi_ehdr);
	frmhdr.wi_ehdr.ether_type = 0;
	wh = mtod(m0, struct ieee80211_frame *);
			
	frmhdr.wi_tx_ctl = htole16(WI_ENC_TX_802_11|WI_TXCNTL_TX_EX);
	if (params && (params->ibp_flags & IEEE80211_BPF_NOACK))
		frmhdr.wi_tx_ctl |= htole16(WI_TXCNTL_ALTRTRY);
	if ((wh->i_fc[1] & IEEE80211_FC1_PROTECTED) &&
	    (!params || (params && (params->ibp_flags & IEEE80211_BPF_CRYPTO)))) {
		k = ieee80211_crypto_encap(ni, m0);
		if (k == NULL) {
			rc = ENOMEM;
			goto out;
		}
		frmhdr.wi_tx_ctl |= htole16(WI_TXCNTL_NOCRYPT);
	}
	if (ieee80211_radiotap_active_vap(vap)) {
		sc->sc_tx_th.wt_rate = ni->ni_txrate;
		ieee80211_radiotap_tx(vap, m0);
	}
	m_copydata(m0, 0, sizeof(struct ieee80211_frame),
	    (caddr_t)&frmhdr.wi_whdr);
	m_adj(m0, sizeof(struct ieee80211_frame));
	frmhdr.wi_dat_len = htole16(m0->m_pkthdr.len);
	if (wi_start_tx(sc, &frmhdr, m0) < 0) {
		m0 = NULL;
		rc = EIO;
		goto out;
	}
	m0 = NULL;
	ieee80211_free_node(ni);

	sc->sc_txnext = cur = (cur + 1) % sc->sc_ntxbuf;
out:
	WI_UNLOCK(sc);

	if (m0 != NULL)
		m_freem(m0);
	return rc;
}

static int
wi_reset(struct wi_softc *sc)
{
#define WI_INIT_TRIES 3
	int i, error = 0;

	for (i = 0; i < WI_INIT_TRIES; i++) {
		error = wi_cmd(sc, WI_CMD_INI, 0, 0, 0);
		if (error == 0)
			break;
		DELAY(WI_DELAY * 1000);
	}
	sc->sc_reset = 1;
	if (i == WI_INIT_TRIES) {
		device_printf(sc->sc_dev, "reset failed\n");
		return error;
	}

	CSR_WRITE_2(sc, WI_INT_EN, 0);
	CSR_WRITE_2(sc, WI_EVENT_ACK, 0xFFFF);

	/* Calibrate timer. */
	wi_write_val(sc, WI_RID_TICK_TIME, 8);

	return 0;
#undef WI_INIT_TRIES
}

static void
wi_watchdog(void *arg)
{
	struct wi_softc	*sc = arg;

	WI_LOCK_ASSERT(sc);

	if (!sc->sc_enabled)
		return;

	if (sc->sc_tx_timer && --sc->sc_tx_timer == 0) {
		device_printf(sc->sc_dev, "device timeout\n");
		counter_u64_add(sc->sc_ic.ic_oerrors, 1);
		wi_init(sc);
		return;
	}
	callout_reset(&sc->sc_watchdog, hz, wi_watchdog, sc);
}

static void
wi_parent(struct ieee80211com *ic)
{
	struct wi_softc *sc = ic->ic_softc;
	int startall = 0;

	WI_LOCK(sc);
	/*
	 * Can't do promisc and hostap at the same time.  If all that's
	 * changing is the promisc flag, try to short-circuit a call to
	 * wi_init() by just setting PROMISC in the hardware.
	 */
	if (ic->ic_nrunning > 0) {
		if (ic->ic_opmode != IEEE80211_M_HOSTAP &&
		    sc->sc_flags & WI_FLAGS_RUNNING) {
			if (ic->ic_promisc > 0 &&
			    (sc->sc_flags & WI_FLAGS_PROMISC) == 0) {
				wi_write_val(sc, WI_RID_PROMISC, 1);
				sc->sc_flags |= WI_FLAGS_PROMISC;
			} else if (ic->ic_promisc == 0 &&
			    (sc->sc_flags & WI_FLAGS_PROMISC) != 0) {
				wi_write_val(sc, WI_RID_PROMISC, 0);
				sc->sc_flags &= ~WI_FLAGS_PROMISC;
			} else {
				wi_init(sc);
				startall = 1;
			}
		} else {
			wi_init(sc);
			startall = 1;
		}
	} else if (sc->sc_flags & WI_FLAGS_RUNNING) {
		wi_stop(sc, 1);
		sc->wi_gone = 0;
	}
	WI_UNLOCK(sc);
	if (startall)
		ieee80211_start_all(ic);
}

static void
wi_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct ieee80211vap *vap = ifp->if_softc;
	struct ieee80211com *ic = vap->iv_ic;
	struct wi_softc *sc = ic->ic_softc;
	u_int16_t val;
	int rate, len;

	len = sizeof(val);
	if (sc->sc_enabled &&
	    wi_read_rid(sc, WI_RID_CUR_TX_RATE, &val, &len) == 0 &&
	    len == sizeof(val)) {
		/* convert to 802.11 rate */
		val = le16toh(val);
		rate = val * 2;
		if (sc->sc_firmware_type == WI_LUCENT) {
			if (rate == 10)
				rate = 11;	/* 5.5Mbps */
		} else {
			if (rate == 4*2)
				rate = 11;	/* 5.5Mbps */
			else if (rate == 8*2)
				rate = 22;	/* 11Mbps */
		}
		vap->iv_bss->ni_txrate = rate;
	}
	ieee80211_media_status(ifp, imr);
}

static void
wi_sync_bssid(struct wi_softc *sc, u_int8_t new_bssid[IEEE80211_ADDR_LEN])
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct ieee80211_node *ni = vap->iv_bss;

	if (IEEE80211_ADDR_EQ(new_bssid, ni->ni_bssid))
		return;

	DPRINTF(("wi_sync_bssid: bssid %s -> ", ether_sprintf(ni->ni_bssid)));
	DPRINTF(("%s ?\n", ether_sprintf(new_bssid)));

	/* In promiscuous mode, the BSSID field is not a reliable
	 * indicator of the firmware's BSSID. Damp spurious
	 * change-of-BSSID indications.
	 */
	if (ic->ic_promisc > 0 &&
	    !ppsratecheck(&sc->sc_last_syn, &sc->sc_false_syns,
	                 WI_MAX_FALSE_SYNS))
		return;

	sc->sc_false_syns = MAX(0, sc->sc_false_syns - 1);
#if 0
	/*
	 * XXX hack; we should create a new node with the new bssid
	 * and replace the existing ic_bss with it but since we don't
	 * process management frames to collect state we cheat by
	 * reusing the existing node as we know wi_newstate will be
	 * called and it will overwrite the node state.
	 */
	ieee80211_sta_join(ic, ieee80211_ref_node(ni));
#endif
}

static __noinline void
wi_rx_intr(struct wi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct wi_frame frmhdr;
	struct mbuf *m;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	int fid, len, off;
	u_int8_t dir;
	u_int16_t status;
	int8_t rssi, nf;

	fid = CSR_READ_2(sc, WI_RX_FID);

	/* First read in the frame header */
	if (wi_read_bap(sc, fid, 0, &frmhdr, sizeof(frmhdr))) {
		CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_RX);
		counter_u64_add(ic->ic_ierrors, 1);
		DPRINTF(("wi_rx_intr: read fid %x failed\n", fid));
		return;
	}

	/*
	 * Drop undecryptable or packets with receive errors here
	 */
	status = le16toh(frmhdr.wi_status);
	if (status & WI_STAT_ERRSTAT) {
		CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_RX);
		counter_u64_add(ic->ic_ierrors, 1);
		DPRINTF(("wi_rx_intr: fid %x error status %x\n", fid, status));
		return;
	}

	len = le16toh(frmhdr.wi_dat_len);
	off = ALIGN(sizeof(struct ieee80211_frame));

	/*
	 * Sometimes the PRISM2.x returns bogusly large frames. Except
	 * in monitor mode, just throw them away.
	 */
	if (off + len > MCLBYTES) {
		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_RX);
			counter_u64_add(ic->ic_ierrors, 1);
			DPRINTF(("wi_rx_intr: oversized packet\n"));
			return;
		} else
			len = 0;
	}

	if (off + len > MHLEN)
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	else
		m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL) {
		CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_RX);
		counter_u64_add(ic->ic_ierrors, 1);
		DPRINTF(("wi_rx_intr: MGET failed\n"));
		return;
	}
	m->m_data += off - sizeof(struct ieee80211_frame);
	memcpy(m->m_data, &frmhdr.wi_whdr, sizeof(struct ieee80211_frame));
	wi_read_bap(sc, fid, sizeof(frmhdr),
	    m->m_data + sizeof(struct ieee80211_frame), len);
	m->m_pkthdr.len = m->m_len = sizeof(struct ieee80211_frame) + len;

	CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_RX);

	rssi = frmhdr.wi_rx_signal;
	nf = frmhdr.wi_rx_silence;
	if (ieee80211_radiotap_active(ic)) {
		struct wi_rx_radiotap_header *tap = &sc->sc_rx_th;
		uint32_t rstamp;

		rstamp = (le16toh(frmhdr.wi_rx_tstamp0) << 16) |
		    le16toh(frmhdr.wi_rx_tstamp1);
		tap->wr_tsf = htole64((uint64_t)rstamp);
		/* XXX replace divide by table */
		tap->wr_rate = frmhdr.wi_rx_rate / 5;
		tap->wr_flags = 0;
		if (frmhdr.wi_status & WI_STAT_PCF)
			tap->wr_flags |= IEEE80211_RADIOTAP_F_CFP;
		if (m->m_flags & M_WEP)
			tap->wr_flags |= IEEE80211_RADIOTAP_F_WEP;
		tap->wr_antsignal = rssi;
		tap->wr_antnoise = nf;
	}

	/* synchronize driver's BSSID with firmware's BSSID */
	wh = mtod(m, struct ieee80211_frame *);
	dir = wh->i_fc[1] & IEEE80211_FC1_DIR_MASK;
	if (ic->ic_opmode == IEEE80211_M_IBSS && dir == IEEE80211_FC1_DIR_NODS)
		wi_sync_bssid(sc, wh->i_addr3);

	WI_UNLOCK(sc);

	ni = ieee80211_find_rxnode(ic, mtod(m, struct ieee80211_frame_min *));
	if (ni != NULL) {
		(void) ieee80211_input(ni, m, rssi, nf);
		ieee80211_free_node(ni);
	} else
		(void) ieee80211_input_all(ic, m, rssi, nf);

	WI_LOCK(sc);
}

static __noinline void
wi_tx_ex_intr(struct wi_softc *sc)
{
	struct wi_frame frmhdr;
	int fid;

	fid = CSR_READ_2(sc, WI_TX_CMP_FID);
	/* Read in the frame header */
	if (wi_read_bap(sc, fid, 0, &frmhdr, sizeof(frmhdr)) == 0) {
		u_int16_t status = le16toh(frmhdr.wi_status);
		/*
		 * Spontaneous station disconnects appear as xmit
		 * errors.  Don't announce them and/or count them
		 * as an output error.
		 */
		if ((status & WI_TXSTAT_DISCONNECT) == 0) {
			if (ppsratecheck(&lasttxerror, &curtxeps, wi_txerate)) {
				device_printf(sc->sc_dev, "tx failed");
				if (status & WI_TXSTAT_RET_ERR)
					printf(", retry limit exceeded");
				if (status & WI_TXSTAT_AGED_ERR)
					printf(", max transmit lifetime exceeded");
				if (status & WI_TXSTAT_DISCONNECT)
					printf(", port disconnected");
				if (status & WI_TXSTAT_FORM_ERR)
					printf(", invalid format (data len %u src %6D)",
						le16toh(frmhdr.wi_dat_len),
						frmhdr.wi_ehdr.ether_shost, ":");
				if (status & ~0xf)
					printf(", status=0x%x", status);
				printf("\n");
			}
			counter_u64_add(sc->sc_ic.ic_oerrors, 1);
		} else
			DPRINTF(("port disconnected\n"));
	} else
		DPRINTF(("wi_tx_ex_intr: read fid %x failed\n", fid));
	CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_TX_EXC);
}

static __noinline void
wi_tx_intr(struct wi_softc *sc)
{
	int fid, cur;

	if (sc->wi_gone)
		return;

	fid = CSR_READ_2(sc, WI_ALLOC_FID);
	CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_ALLOC);

	cur = sc->sc_txcur;
	if (sc->sc_txd[cur].d_fid != fid) {
		device_printf(sc->sc_dev, "bad alloc %x != %x, cur %d nxt %d\n",
		    fid, sc->sc_txd[cur].d_fid, cur, sc->sc_txnext);
		return;
	}
	sc->sc_tx_timer = 0;
	sc->sc_txd[cur].d_len = 0;
	sc->sc_txcur = cur = (cur + 1) % sc->sc_ntxbuf;
	if (sc->sc_txd[cur].d_len != 0) {
		if (wi_cmd(sc, WI_CMD_TX | WI_RECLAIM, sc->sc_txd[cur].d_fid,
		    0, 0)) {
			device_printf(sc->sc_dev, "xmit failed\n");
			sc->sc_txd[cur].d_len = 0;
		} else {
			sc->sc_tx_timer = 5;
		}
	}
}

static __noinline void
wi_info_intr(struct wi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	int i, fid, len, off;
	u_int16_t ltbuf[2];
	u_int16_t stat;
	u_int32_t *ptr;

	fid = CSR_READ_2(sc, WI_INFO_FID);
	wi_read_bap(sc, fid, 0, ltbuf, sizeof(ltbuf));

	switch (le16toh(ltbuf[1])) {
	case WI_INFO_LINK_STAT:
		wi_read_bap(sc, fid, sizeof(ltbuf), &stat, sizeof(stat));
		DPRINTF(("wi_info_intr: LINK_STAT 0x%x\n", le16toh(stat)));

		if (vap == NULL)
			goto finish;

		switch (le16toh(stat)) {
		case WI_INFO_LINK_STAT_CONNECTED:
			if (vap->iv_state == IEEE80211_S_RUN &&
			    vap->iv_opmode != IEEE80211_M_IBSS)
				break;
			/* fall thru... */
		case WI_INFO_LINK_STAT_AP_CHG:
			IEEE80211_LOCK(ic);
			vap->iv_bss->ni_associd = 1 | 0xc000;	/* NB: anything will do */
			ieee80211_new_state(vap, IEEE80211_S_RUN, 0);
			IEEE80211_UNLOCK(ic);
			break;
		case WI_INFO_LINK_STAT_AP_INR:
			break;
		case WI_INFO_LINK_STAT_DISCONNECTED:
			/* we dropped off the net; e.g. due to deauth/disassoc */
			IEEE80211_LOCK(ic);
			vap->iv_bss->ni_associd = 0;
			vap->iv_stats.is_rx_deauth++;
			ieee80211_new_state(vap, IEEE80211_S_SCAN, 0);
			IEEE80211_UNLOCK(ic);
			break;
		case WI_INFO_LINK_STAT_AP_OOR:
			/* XXX does this need to be per-vap? */
			ieee80211_beacon_miss(ic);
			break;
		case WI_INFO_LINK_STAT_ASSOC_FAILED:
			if (vap->iv_opmode == IEEE80211_M_STA)
				ieee80211_new_state(vap, IEEE80211_S_SCAN,
				    IEEE80211_SCAN_FAIL_TIMEOUT);
			break;
		}
		break;
	case WI_INFO_COUNTERS:
		/* some card versions have a larger stats structure */
		len = min(le16toh(ltbuf[0]) - 1, sizeof(sc->sc_stats) / 4);
		ptr = (u_int32_t *)&sc->sc_stats;
		off = sizeof(ltbuf);
		for (i = 0; i < len; i++, off += 2, ptr++) {
			wi_read_bap(sc, fid, off, &stat, sizeof(stat));
#ifdef WI_HERMES_STATS_WAR
			if (stat & 0xf000)
				stat = ~stat;
#endif
			*ptr += stat;
		}
		break;
	default:
		DPRINTF(("wi_info_intr: got fid %x type %x len %d\n", fid,
		    le16toh(ltbuf[1]), le16toh(ltbuf[0])));
		break;
	}
finish:
	CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_INFO);
}

static int
wi_write_multi(struct wi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap;
	struct wi_mcast mlist;
	int n;

	if (ic->ic_allmulti > 0 || ic->ic_promisc > 0) {
allmulti:
		memset(&mlist, 0, sizeof(mlist));
		return wi_write_rid(sc, WI_RID_MCAST_LIST, &mlist,
		    sizeof(mlist));
	}

	n = 0;
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
		struct ifnet *ifp;
		struct ifmultiaddr *ifma;

		ifp = vap->iv_ifp;
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			if (n >= 16)
				goto allmulti;
			IEEE80211_ADDR_COPY(&mlist.wi_mcast[n],
			    (LLADDR((struct sockaddr_dl *)ifma->ifma_addr)));
			n++;
		}
		if_maddr_runlock(ifp);
	}
	return wi_write_rid(sc, WI_RID_MCAST_LIST, &mlist,
	    IEEE80211_ADDR_LEN * n);
}

static void
wi_update_mcast(struct ieee80211com *ic)
{

	wi_write_multi(ic->ic_softc);
}

static void
wi_update_promisc(struct ieee80211com *ic)
{
	struct wi_softc *sc = ic->ic_softc;

	WI_LOCK(sc);
	/* XXX handle WEP special case handling? */
	wi_write_val(sc, WI_RID_PROMISC, 
	    (ic->ic_opmode == IEEE80211_M_MONITOR ||
	     (ic->ic_promisc > 0)));
	WI_UNLOCK(sc);
}

static void
wi_read_nicid(struct wi_softc *sc)
{
	struct wi_card_ident *id;
	char *p;
	int len;
	u_int16_t ver[4];

	/* getting chip identity */
	memset(ver, 0, sizeof(ver));
	len = sizeof(ver);
	wi_read_rid(sc, WI_RID_CARD_ID, ver, &len);

	sc->sc_firmware_type = WI_NOTYPE;
	sc->sc_nic_id = le16toh(ver[0]);
	for (id = wi_card_ident; id->card_name != NULL; id++) {
		if (sc->sc_nic_id == id->card_id) {
			sc->sc_nic_name = id->card_name;
			sc->sc_firmware_type = id->firm_type;
			break;
		}
	}
	if (sc->sc_firmware_type == WI_NOTYPE) {
		if (sc->sc_nic_id & 0x8000) {
			sc->sc_firmware_type = WI_INTERSIL;
			sc->sc_nic_name = "Unknown Prism chip";
		} else {
			sc->sc_firmware_type = WI_LUCENT;
			sc->sc_nic_name = "Unknown Lucent chip";
		}
	}
	if (bootverbose)
		device_printf(sc->sc_dev, "using %s\n", sc->sc_nic_name);

	/* get primary firmware version (Only Prism chips) */
	if (sc->sc_firmware_type != WI_LUCENT) {
		memset(ver, 0, sizeof(ver));
		len = sizeof(ver);
		wi_read_rid(sc, WI_RID_PRI_IDENTITY, ver, &len);
		sc->sc_pri_firmware_ver = le16toh(ver[2]) * 10000 +
		    le16toh(ver[3]) * 100 + le16toh(ver[1]);
	}

	/* get station firmware version */
	memset(ver, 0, sizeof(ver));
	len = sizeof(ver);
	wi_read_rid(sc, WI_RID_STA_IDENTITY, ver, &len);
	sc->sc_sta_firmware_ver = le16toh(ver[2]) * 10000 +
	    le16toh(ver[3]) * 100 + le16toh(ver[1]);
	if (sc->sc_firmware_type == WI_INTERSIL &&
	    (sc->sc_sta_firmware_ver == 10102 ||
	     sc->sc_sta_firmware_ver == 20102)) {
		char ident[12];
		memset(ident, 0, sizeof(ident));
		len = sizeof(ident);
		/* value should be the format like "V2.00-11" */
		if (wi_read_rid(sc, WI_RID_SYMBOL_IDENTITY, ident, &len) == 0 &&
		    *(p = (char *)ident) >= 'A' &&
		    p[2] == '.' && p[5] == '-' && p[8] == '\0') {
			sc->sc_firmware_type = WI_SYMBOL;
			sc->sc_sta_firmware_ver = (p[1] - '0') * 10000 +
			    (p[3] - '0') * 1000 + (p[4] - '0') * 100 +
			    (p[6] - '0') * 10 + (p[7] - '0');
		}
	}
	if (bootverbose) {
		device_printf(sc->sc_dev, "%s Firmware: ",
		    wi_firmware_names[sc->sc_firmware_type]);
		if (sc->sc_firmware_type != WI_LUCENT)	/* XXX */
			printf("Primary (%u.%u.%u), ",
			    sc->sc_pri_firmware_ver / 10000,
			    (sc->sc_pri_firmware_ver % 10000) / 100,
			    sc->sc_pri_firmware_ver % 100);
		printf("Station (%u.%u.%u)\n",
		    sc->sc_sta_firmware_ver / 10000,
		    (sc->sc_sta_firmware_ver % 10000) / 100,
		    sc->sc_sta_firmware_ver % 100);
	}
}

static int
wi_write_ssid(struct wi_softc *sc, int rid, u_int8_t *buf, int buflen)
{
	struct wi_ssid ssid;

	if (buflen > IEEE80211_NWID_LEN)
		return ENOBUFS;
	memset(&ssid, 0, sizeof(ssid));
	ssid.wi_len = htole16(buflen);
	memcpy(ssid.wi_ssid, buf, buflen);
	return wi_write_rid(sc, rid, &ssid, sizeof(ssid));
}

static int
wi_write_txrate(struct wi_softc *sc, struct ieee80211vap *vap)
{
	static const uint16_t lucent_rates[12] = {
	    [ 0] = 3,	/* auto */
	    [ 1] = 1,	/* 1Mb/s */
	    [ 2] = 2,	/* 2Mb/s */
	    [ 5] = 4,	/* 5.5Mb/s */
	    [11] = 5	/* 11Mb/s */
	};
	static const uint16_t intersil_rates[12] = {
	    [ 0] = 0xf,	/* auto */
	    [ 1] = 0,	/* 1Mb/s */
	    [ 2] = 1,	/* 2Mb/s */
	    [ 5] = 2,	/* 5.5Mb/s */
	    [11] = 3,	/* 11Mb/s */
	};
	const uint16_t *rates = sc->sc_firmware_type == WI_LUCENT ?
	    lucent_rates : intersil_rates;
	struct ieee80211com *ic = vap->iv_ic;
	const struct ieee80211_txparam *tp;

	tp = &vap->iv_txparms[ieee80211_chan2mode(ic->ic_bsschan)];
	return wi_write_val(sc, WI_RID_TX_RATE,
	    (tp->ucastrate == IEEE80211_FIXED_RATE_NONE ?
		rates[0] : rates[tp->ucastrate / 2]));
}

static int
wi_write_wep(struct wi_softc *sc, struct ieee80211vap *vap)
{
	int error = 0;
	int i, keylen;
	u_int16_t val;
	struct wi_key wkey[IEEE80211_WEP_NKID];

	switch (sc->sc_firmware_type) {
	case WI_LUCENT:
		val = (vap->iv_flags & IEEE80211_F_PRIVACY) ? 1 : 0;
		error = wi_write_val(sc, WI_RID_ENCRYPTION, val);
		if (error)
			break;
		if ((vap->iv_flags & IEEE80211_F_PRIVACY) == 0)
			break;
		error = wi_write_val(sc, WI_RID_TX_CRYPT_KEY, vap->iv_def_txkey);
		if (error)
			break;
		memset(wkey, 0, sizeof(wkey));
		for (i = 0; i < IEEE80211_WEP_NKID; i++) {
			keylen = vap->iv_nw_keys[i].wk_keylen;
			wkey[i].wi_keylen = htole16(keylen);
			memcpy(wkey[i].wi_keydat, vap->iv_nw_keys[i].wk_key,
			    keylen);
		}
		error = wi_write_rid(sc, WI_RID_DEFLT_CRYPT_KEYS,
		    wkey, sizeof(wkey));
		sc->sc_encryption = 0;
		break;

	case WI_INTERSIL:
		val = HOST_ENCRYPT | HOST_DECRYPT;
		if (vap->iv_flags & IEEE80211_F_PRIVACY) {
			/*
			 * ONLY HWB3163 EVAL-CARD Firmware version
			 * less than 0.8 variant2
			 *
			 *   If promiscuous mode disable, Prism2 chip
			 *  does not work with WEP .
			 * It is under investigation for details.
			 * (ichiro@netbsd.org)
			 */
			if (sc->sc_sta_firmware_ver < 802 ) {
				/* firm ver < 0.8 variant 2 */
				wi_write_val(sc, WI_RID_PROMISC, 1);
			}
			wi_write_val(sc, WI_RID_CNFAUTHMODE,
			    vap->iv_bss->ni_authmode);
			val |= PRIVACY_INVOKED;
		} else {
			wi_write_val(sc, WI_RID_CNFAUTHMODE, IEEE80211_AUTH_OPEN);
		}
		error = wi_write_val(sc, WI_RID_P2_ENCRYPTION, val);
		if (error)
			break;
		sc->sc_encryption = val;
		if ((val & PRIVACY_INVOKED) == 0)
			break;
		error = wi_write_val(sc, WI_RID_P2_TX_CRYPT_KEY, vap->iv_def_txkey);
		break;
	}
	return error;
}

static int
wi_cmd(struct wi_softc *sc, int cmd, int val0, int val1, int val2)
{
	int i, s = 0;

	if (sc->wi_gone)
		return (ENODEV);

	/* wait for the busy bit to clear */
	for (i = sc->wi_cmd_count; i > 0; i--) {	/* 500ms */
		if (!(CSR_READ_2(sc, WI_COMMAND) & WI_CMD_BUSY))
			break;
		DELAY(1*1000);	/* 1ms */
	}
	if (i == 0) {
		device_printf(sc->sc_dev, "%s: busy bit won't clear, cmd 0x%x\n",
		   __func__, cmd);
		sc->wi_gone = 1;
		return(ETIMEDOUT);
	}

	CSR_WRITE_2(sc, WI_PARAM0, val0);
	CSR_WRITE_2(sc, WI_PARAM1, val1);
	CSR_WRITE_2(sc, WI_PARAM2, val2);
	CSR_WRITE_2(sc, WI_COMMAND, cmd);

	if (cmd == WI_CMD_INI) {
		/* XXX: should sleep here. */
		DELAY(100*1000);		/* 100ms delay for init */
	}
	for (i = 0; i < WI_TIMEOUT; i++) {
		/*
		 * Wait for 'command complete' bit to be
		 * set in the event status register.
		 */
		s = CSR_READ_2(sc, WI_EVENT_STAT);
		if (s & WI_EV_CMD) {
			/* Ack the event and read result code. */
			s = CSR_READ_2(sc, WI_STATUS);
			CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_CMD);
			if (s & WI_STAT_CMD_RESULT) {
				return(EIO);
			}
			break;
		}
		DELAY(WI_DELAY);
	}

	if (i == WI_TIMEOUT) {
		device_printf(sc->sc_dev, "%s: timeout on cmd 0x%04x; "
		    "event status 0x%04x\n", __func__, cmd, s);
		if (s == 0xffff)
			sc->wi_gone = 1;
		return(ETIMEDOUT);
	}
	return (0);
}

static int
wi_seek_bap(struct wi_softc *sc, int id, int off)
{
	int i, status;

	CSR_WRITE_2(sc, WI_SEL0, id);
	CSR_WRITE_2(sc, WI_OFF0, off);

	for (i = 0; ; i++) {
		status = CSR_READ_2(sc, WI_OFF0);
		if ((status & WI_OFF_BUSY) == 0)
			break;
		if (i == WI_TIMEOUT) {
			device_printf(sc->sc_dev, "%s: timeout, id %x off %x\n",
			    __func__, id, off);
			sc->sc_bap_off = WI_OFF_ERR;	/* invalidate */
			if (status == 0xffff)
				sc->wi_gone = 1;
			return ETIMEDOUT;
		}
		DELAY(1);
	}
	if (status & WI_OFF_ERR) {
		device_printf(sc->sc_dev, "%s: error, id %x off %x\n",
		    __func__, id, off);
		sc->sc_bap_off = WI_OFF_ERR;	/* invalidate */
		return EIO;
	}
	sc->sc_bap_id = id;
	sc->sc_bap_off = off;
	return 0;
}

static int
wi_read_bap(struct wi_softc *sc, int id, int off, void *buf, int buflen)
{
	int error, cnt;

	if (buflen == 0)
		return 0;
	if (id != sc->sc_bap_id || off != sc->sc_bap_off) {
		if ((error = wi_seek_bap(sc, id, off)) != 0)
			return error;
	}
	cnt = (buflen + 1) / 2;
	CSR_READ_MULTI_STREAM_2(sc, WI_DATA0, (u_int16_t *)buf, cnt);
	sc->sc_bap_off += cnt * 2;
	return 0;
}

static int
wi_write_bap(struct wi_softc *sc, int id, int off, const void *buf, int buflen)
{
	int error, cnt;

	if (buflen == 0)
		return 0;

	if (id != sc->sc_bap_id || off != sc->sc_bap_off) {
		if ((error = wi_seek_bap(sc, id, off)) != 0)
			return error;
	}
	cnt = (buflen + 1) / 2;
	CSR_WRITE_MULTI_STREAM_2(sc, WI_DATA0, (const uint16_t *)buf, cnt);
	sc->sc_bap_off += cnt * 2;

	return 0;
}

static int
wi_mwrite_bap(struct wi_softc *sc, int id, int off, struct mbuf *m0, int totlen)
{
	int error, len;
	struct mbuf *m;

	for (m = m0; m != NULL && totlen > 0; m = m->m_next) {
		if (m->m_len == 0)
			continue;

		len = min(m->m_len, totlen);

		if (((u_long)m->m_data) % 2 != 0 || len % 2 != 0) {
			m_copydata(m, 0, totlen, (caddr_t)&sc->sc_txbuf);
			return wi_write_bap(sc, id, off, (caddr_t)&sc->sc_txbuf,
			    totlen);
		}

		if ((error = wi_write_bap(sc, id, off, m->m_data, len)) != 0)
			return error;

		off += m->m_len;
		totlen -= len;
	}
	return 0;
}

static int
wi_alloc_fid(struct wi_softc *sc, int len, int *idp)
{
	int i;

	if (wi_cmd(sc, WI_CMD_ALLOC_MEM, len, 0, 0)) {
		device_printf(sc->sc_dev, "%s: failed to allocate %d bytes on NIC\n",
		    __func__, len);
		return ENOMEM;
	}

	for (i = 0; i < WI_TIMEOUT; i++) {
		if (CSR_READ_2(sc, WI_EVENT_STAT) & WI_EV_ALLOC)
			break;
		DELAY(1);
	}
	if (i == WI_TIMEOUT) {
		device_printf(sc->sc_dev, "%s: timeout in alloc\n", __func__);
		return ETIMEDOUT;
	}
	*idp = CSR_READ_2(sc, WI_ALLOC_FID);
	CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_ALLOC);
	return 0;
}

static int
wi_read_rid(struct wi_softc *sc, int rid, void *buf, int *buflenp)
{
	int error, len;
	u_int16_t ltbuf[2];

	/* Tell the NIC to enter record read mode. */
	error = wi_cmd(sc, WI_CMD_ACCESS | WI_ACCESS_READ, rid, 0, 0);
	if (error)
		return error;

	error = wi_read_bap(sc, rid, 0, ltbuf, sizeof(ltbuf));
	if (error)
		return error;

	if (le16toh(ltbuf[1]) != rid) {
		device_printf(sc->sc_dev, "record read mismatch, rid=%x, got=%x\n",
		    rid, le16toh(ltbuf[1]));
		return EIO;
	}
	len = (le16toh(ltbuf[0]) - 1) * 2;	 /* already got rid */
	if (*buflenp < len) {
		device_printf(sc->sc_dev, "record buffer is too small, "
		    "rid=%x, size=%d, len=%d\n",
		    rid, *buflenp, len);
		return ENOSPC;
	}
	*buflenp = len;
	return wi_read_bap(sc, rid, sizeof(ltbuf), buf, len);
}

static int
wi_write_rid(struct wi_softc *sc, int rid, const void *buf, int buflen)
{
	int error;
	u_int16_t ltbuf[2];

	ltbuf[0] = htole16((buflen + 1) / 2 + 1);	 /* includes rid */
	ltbuf[1] = htole16(rid);

	error = wi_write_bap(sc, rid, 0, ltbuf, sizeof(ltbuf));
	if (error) {
		device_printf(sc->sc_dev, "%s: bap0 write failure, rid 0x%x\n",
		    __func__, rid);
		return error;
	}
	error = wi_write_bap(sc, rid, sizeof(ltbuf), buf, buflen);
	if (error) {
		device_printf(sc->sc_dev, "%s: bap1 write failure, rid 0x%x\n",
		    __func__, rid);
		return error;
	}

	return wi_cmd(sc, WI_CMD_ACCESS | WI_ACCESS_WRITE, rid, 0, 0);
}

static int
wi_write_appie(struct wi_softc *sc, int rid, const struct ieee80211_appie *ie)
{
	/* NB: 42 bytes is probably ok to have on the stack */
	char buf[sizeof(uint16_t) + 40];

	if (ie->ie_len > 40)
		return EINVAL;
	/* NB: firmware requires 16-bit ie length before ie data */
	*(uint16_t *) buf = htole16(ie->ie_len);
	memcpy(buf + sizeof(uint16_t), ie->ie_data, ie->ie_len);
	return wi_write_rid(sc, rid, buf, ie->ie_len + sizeof(uint16_t));
}

static u_int16_t
wi_read_chanmask(struct wi_softc *sc)
{
	u_int16_t val;
	int buflen;

	buflen = sizeof(val);
	if (wi_read_rid(sc, WI_RID_CHANNEL_LIST, &val, &buflen) != 0)
		val = htole16(0x1fff);	/* assume 1-13 */
	KASSERT(val != 0, ("%s: no available channels listed!", __func__));

	val <<= 1;			/* shift for base 1 indices */

	return (val);
}

int
wi_alloc(device_t dev, int rid)
{
	struct wi_softc	*sc = device_get_softc(dev);

	if (sc->wi_bus_type != WI_BUS_PCI_NATIVE) {
		sc->iobase_rid = rid;
		sc->iobase = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
		    &sc->iobase_rid, (1 << 6),
		    rman_make_alignment_flags(1 << 6) | RF_ACTIVE);
		if (sc->iobase == NULL) {
			device_printf(dev, "No I/O space?!\n");
			return ENXIO;
		}

		sc->wi_io_addr = rman_get_start(sc->iobase);
		sc->wi_btag = rman_get_bustag(sc->iobase);
		sc->wi_bhandle = rman_get_bushandle(sc->iobase);
	} else {
		sc->mem_rid = rid;
		sc->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
		    &sc->mem_rid, RF_ACTIVE);
		if (sc->mem == NULL) {
			device_printf(dev, "No Mem space on prism2.5?\n");
			return ENXIO;
		}

		sc->wi_btag = rman_get_bustag(sc->mem);
		sc->wi_bhandle = rman_get_bushandle(sc->mem);
	}

	sc->irq_rid = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
	    RF_ACTIVE |
	    ((sc->wi_bus_type == WI_BUS_PCCARD) ? 0 : RF_SHAREABLE));
	if (sc->irq == NULL) {
		wi_free(dev);
		device_printf(dev, "No irq?!\n");
		return ENXIO;
	}

	sc->sc_dev = dev;
	sc->sc_unit = device_get_unit(dev);
	return 0;
}

void
wi_free(device_t dev)
{
	struct wi_softc	*sc = device_get_softc(dev);

	if (sc->iobase != NULL) {
		bus_release_resource(dev, SYS_RES_IOPORT, sc->iobase_rid, sc->iobase);
		sc->iobase = NULL;
	}
	if (sc->irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq);
		sc->irq = NULL;
	}
	if (sc->mem != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem);
		sc->mem = NULL;
	}
}
