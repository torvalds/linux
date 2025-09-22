/*	$OpenBSD: if_wi.c,v 1.177 2022/07/14 13:46:24 bluhm Exp $	*/

/*
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
 *
 *	From: if_wi.c,v 1.7 1999/07/04 14:40:22 wpaul Exp $
 */

/*
 * Lucent WaveLAN/IEEE 802.11 driver for OpenBSD.
 *
 * Originally written by Bill Paul <wpaul@ctr.columbia.edu>
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
 */

#define WI_HERMES_AUTOINC_WAR	/* Work around data write autoinc bug. */
#define WI_HERMES_STATS_WAR	/* Work around stats counter bug. */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>

#include <dev/ic/if_wireg.h>
#include <dev/ic/if_wi_ieee.h>
#include <dev/ic/if_wivar.h>

#include <crypto/arc4.h>

#define BPFATTACH(if_bpf,if,dlt,sz)
#define STATIC

#ifdef WIDEBUG

u_int32_t	widebug = WIDEBUG;

#define WID_INTR	0x01
#define WID_START	0x02
#define WID_IOCTL	0x04
#define WID_INIT	0x08
#define WID_STOP	0x10
#define WID_RESET	0x20

#define DPRINTF(mask,args) if (widebug & (mask)) printf args;

#else	/* !WIDEBUG */
#define DPRINTF(mask,args)
#endif	/* WIDEBUG */

#ifdef foo
static u_int8_t	wi_mcast_addr[6] = { 0x01, 0x60, 0x1D, 0x00, 0x01, 0x00 };
#endif

STATIC void wi_reset(struct wi_softc *);
STATIC int wi_ioctl(struct ifnet *, u_long, caddr_t);
STATIC void wi_init_io(struct wi_softc *);
STATIC void wi_start(struct ifnet *);
STATIC void wi_watchdog(struct ifnet *);
STATIC void wi_rxeof(struct wi_softc *);
STATIC void wi_txeof(struct wi_softc *, int);
STATIC void wi_update_stats(struct wi_softc *);
STATIC void wi_setmulti(struct wi_softc *);

STATIC int wi_cmd_io(struct wi_softc *, int, int, int, int);
STATIC int wi_read_record_io(struct wi_softc *, struct wi_ltv_gen *);
STATIC int wi_write_record_io(struct wi_softc *, struct wi_ltv_gen *);
STATIC int wi_read_data_io(struct wi_softc *, int,
					int, caddr_t, int);
STATIC int wi_write_data_io(struct wi_softc *, int,
					int, caddr_t, int);
STATIC int wi_seek(struct wi_softc *, int, int, int);

STATIC void wi_inquire(void *);
STATIC int wi_setdef(struct wi_softc *, struct wi_req *);
STATIC void wi_get_id(struct wi_softc *);

STATIC int wi_media_change(struct ifnet *);
STATIC void wi_media_status(struct ifnet *, struct ifmediareq *);

STATIC int wi_set_ssid(struct ieee80211_nwid *, u_int8_t *, int);
STATIC int wi_set_nwkey(struct wi_softc *, struct ieee80211_nwkey *);
STATIC int wi_get_nwkey(struct wi_softc *, struct ieee80211_nwkey *);
STATIC int wi_sync_media(struct wi_softc *, int, int);
STATIC int wi_set_pm(struct wi_softc *, struct ieee80211_power *);
STATIC int wi_get_pm(struct wi_softc *, struct ieee80211_power *);
STATIC int wi_set_txpower(struct wi_softc *, struct ieee80211_txpower *);
STATIC int wi_get_txpower(struct wi_softc *, struct ieee80211_txpower *);

STATIC int wi_get_debug(struct wi_softc *, struct wi_req *);
STATIC int wi_set_debug(struct wi_softc *, struct wi_req *);

STATIC void wi_do_hostencrypt(struct wi_softc *, caddr_t, int);                
STATIC int wi_do_hostdecrypt(struct wi_softc *, caddr_t, int);

STATIC int wi_alloc_nicmem_io(struct wi_softc *, int, int *);
STATIC int wi_get_fid_io(struct wi_softc *sc, int fid);
STATIC void wi_intr_enable(struct wi_softc *sc, int mode);
STATIC void wi_intr_ack(struct wi_softc *sc, int mode);
void	 wi_scan_timeout(void *);

/* Autoconfig definition of driver back-end */
struct cfdriver wi_cd = {
	NULL, "wi", DV_IFNET
};

const struct wi_card_ident wi_card_ident[] = {
	WI_CARD_IDS
};

struct wi_funcs wi_func_io = {
        wi_cmd_io,
        wi_read_record_io,
        wi_write_record_io,
        wi_alloc_nicmem_io,
        wi_read_data_io,
        wi_write_data_io,
        wi_get_fid_io,
        wi_init_io,

        wi_start,
        wi_ioctl,
        wi_watchdog,
        wi_inquire,
};

int
wi_attach(struct wi_softc *sc, struct wi_funcs *funcs)
{
	struct ieee80211com	*ic;
	struct ifnet		*ifp;
	struct wi_ltv_macaddr	mac;
	struct wi_ltv_rates	rates;
	struct wi_ltv_gen	gen;
	int			error;

	ic = &sc->sc_ic;
	ifp = &ic->ic_if;

	sc->sc_funcs = funcs;
	sc->wi_cmd_count = 500;

	wi_reset(sc);

	/* Read the station address. */
	mac.wi_type = WI_RID_MAC_NODE;
	mac.wi_len = 4;
	error = wi_read_record(sc, (struct wi_ltv_gen *)&mac);
	if (error) {
		printf(": unable to read station address\n");
		return (error);
	}
	bcopy(&mac.wi_mac_addr, &ic->ic_myaddr, IEEE80211_ADDR_LEN);

	wi_get_id(sc);
	printf("address %s", ether_sprintf(ic->ic_myaddr));

	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = funcs->f_ioctl;
	ifp->if_start = funcs->f_start;
	ifp->if_watchdog = funcs->f_watchdog;

	(void)wi_set_ssid(&sc->wi_node_name, WI_DEFAULT_NODENAME,
	    sizeof(WI_DEFAULT_NODENAME) - 1);
	(void)wi_set_ssid(&sc->wi_net_name, WI_DEFAULT_NETNAME,
	    sizeof(WI_DEFAULT_NETNAME) - 1);
	(void)wi_set_ssid(&sc->wi_ibss_name, WI_DEFAULT_IBSS,
	    sizeof(WI_DEFAULT_IBSS) - 1);

	sc->wi_portnum = WI_DEFAULT_PORT;
	sc->wi_ptype = WI_PORTTYPE_BSS;
	sc->wi_ap_density = WI_DEFAULT_AP_DENSITY;
	sc->wi_rts_thresh = WI_DEFAULT_RTS_THRESH;
	sc->wi_tx_rate = WI_DEFAULT_TX_RATE;
	sc->wi_max_data_len = WI_DEFAULT_DATALEN;
	sc->wi_create_ibss = WI_DEFAULT_CREATE_IBSS;
	sc->wi_pm_enabled = WI_DEFAULT_PM_ENABLED;
	sc->wi_max_sleep = WI_DEFAULT_MAX_SLEEP;
	sc->wi_roaming = WI_DEFAULT_ROAMING;
	sc->wi_authtype = WI_DEFAULT_AUTHTYPE;
	sc->wi_diversity = WI_DEFAULT_DIVERSITY;
	sc->wi_crypto_algorithm = WI_CRYPTO_FIRMWARE_WEP;

	/*
	 * Read the default channel from the NIC. This may vary
	 * depending on the country where the NIC was purchased, so
	 * we can't hard-code a default and expect it to work for
	 * everyone.
	 */
	gen.wi_type = WI_RID_OWN_CHNL;
	gen.wi_len = 2;
	if (wi_read_record(sc, &gen) == 0)
		sc->wi_channel = letoh16(gen.wi_val);
	else
		sc->wi_channel = 3;

	/*
	 * Set flags based on firmware version.
	 */
	switch (sc->sc_firmware_type) {
	case WI_LUCENT:
		sc->wi_flags |= WI_FLAGS_HAS_ROAMING;
		if (sc->sc_sta_firmware_ver >= 60000)
			sc->wi_flags |= WI_FLAGS_HAS_MOR;
		if (sc->sc_sta_firmware_ver >= 60006) {
			sc->wi_flags |= WI_FLAGS_HAS_IBSS;
			sc->wi_flags |= WI_FLAGS_HAS_CREATE_IBSS;
		}
		sc->wi_ibss_port = htole16(1);
		break;
	case WI_INTERSIL:
		sc->wi_flags |= WI_FLAGS_HAS_ROAMING;
		/* older prism firmware is slow so crank the count */
		if (sc->sc_sta_firmware_ver < 10000)
			sc->wi_cmd_count = 5000;
		else
			sc->wi_cmd_count = 2000;
		if (sc->sc_sta_firmware_ver >= 800) {
#ifndef SMALL_KERNEL
			/*
			 * USB hostap is more pain than it is worth
			 * for now, things would have to be overhauled
			 */
			if ((sc->sc_sta_firmware_ver != 10402) &&
			    (!(sc->wi_flags & WI_FLAGS_BUS_USB)))
				sc->wi_flags |= WI_FLAGS_HAS_HOSTAP;
#endif
			sc->wi_flags |= WI_FLAGS_HAS_IBSS;
			sc->wi_flags |= WI_FLAGS_HAS_CREATE_IBSS;
		}
		if (sc->sc_sta_firmware_ver >= 10603)
			sc->wi_flags |= WI_FLAGS_HAS_ENH_SECURITY;
		sc->wi_ibss_port = htole16(0);
		break;
	case WI_SYMBOL:
		sc->wi_flags |= WI_FLAGS_HAS_DIVERSITY;
		if (sc->sc_sta_firmware_ver >= 20000)
			sc->wi_flags |= WI_FLAGS_HAS_IBSS;
		if (sc->sc_sta_firmware_ver >= 25000)
			sc->wi_flags |= WI_FLAGS_HAS_CREATE_IBSS;
		sc->wi_ibss_port = htole16(4);
		break;
	}

	/*
	 * Find out if we support WEP on this card.
	 */
	gen.wi_type = WI_RID_WEP_AVAIL;
	gen.wi_len = 2;
	if (wi_read_record(sc, &gen) == 0 && gen.wi_val != htole16(0))
		sc->wi_flags |= WI_FLAGS_HAS_WEP;
	timeout_set(&sc->sc_timo, funcs->f_inquire, sc);

	bzero(&sc->wi_stats, sizeof(sc->wi_stats));

	/* Find supported rates. */
	rates.wi_type = WI_RID_DATA_RATES;
	rates.wi_len = sizeof(rates.wi_rates);
	if (wi_read_record(sc, (struct wi_ltv_gen *)&rates) == 0) {
		int i, nrates;

		nrates = letoh16(*(u_int16_t *)rates.wi_rates);
		if (nrates > sizeof(rates.wi_rates) - 2)
			nrates = sizeof(rates.wi_rates) - 2;

		sc->wi_supprates = 0;
		for (i = 0; i < nrates; i++)
			sc->wi_supprates |= rates.wi_rates[2 + i];
	} else
		sc->wi_supprates = WI_SUPPRATES_1M | WI_SUPPRATES_2M |
		    WI_SUPPRATES_5M | WI_SUPPRATES_11M;

	ifmedia_init(&sc->sc_media, 0, wi_media_change, wi_media_status);
#define	ADD(m, c)	ifmedia_add(&sc->sc_media, (m), (c), NULL)
	ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_AUTO, 0, 0), 0);
	ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_AUTO, IFM_IEEE80211_ADHOC, 0), 0);
	if (sc->wi_flags & WI_FLAGS_HAS_IBSS)
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_AUTO, IFM_IEEE80211_IBSS,
		    0), 0);
	if (sc->wi_flags & WI_FLAGS_HAS_CREATE_IBSS)
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_AUTO,
		    IFM_IEEE80211_IBSSMASTER, 0), 0);
	if (sc->wi_flags & WI_FLAGS_HAS_HOSTAP)
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_AUTO,
		    IFM_IEEE80211_HOSTAP, 0), 0);
	if (sc->wi_supprates & WI_SUPPRATES_1M) {
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS1, 0, 0), 0);
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS1,
		    IFM_IEEE80211_ADHOC, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_IBSS)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS1,
			    IFM_IEEE80211_IBSS, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_CREATE_IBSS)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS1,
			    IFM_IEEE80211_IBSSMASTER, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_HOSTAP)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS1,
			    IFM_IEEE80211_HOSTAP, 0), 0);
	}
	if (sc->wi_supprates & WI_SUPPRATES_2M) {
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS2, 0, 0), 0);
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS2,
		    IFM_IEEE80211_ADHOC, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_IBSS)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS2,
			    IFM_IEEE80211_IBSS, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_CREATE_IBSS)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS2,
			    IFM_IEEE80211_IBSSMASTER, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_HOSTAP)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS2,
			    IFM_IEEE80211_HOSTAP, 0), 0);
	}
	if (sc->wi_supprates & WI_SUPPRATES_5M) {
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS5, 0, 0), 0);
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS5,
		    IFM_IEEE80211_ADHOC, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_IBSS)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS5,
			    IFM_IEEE80211_IBSS, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_CREATE_IBSS)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS5,
			    IFM_IEEE80211_IBSSMASTER, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_HOSTAP)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS5,
			    IFM_IEEE80211_HOSTAP, 0), 0);
	}
	if (sc->wi_supprates & WI_SUPPRATES_11M) {
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS11, 0, 0), 0);
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS11,
		    IFM_IEEE80211_ADHOC, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_IBSS)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS11,
			    IFM_IEEE80211_IBSS, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_CREATE_IBSS)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS11,
			    IFM_IEEE80211_IBSSMASTER, 0), 0);
		if (sc->wi_flags & WI_FLAGS_HAS_HOSTAP)
			ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_IEEE80211_DS11,
			    IFM_IEEE80211_HOSTAP, 0), 0);
		ADD(IFM_MAKEWORD(IFM_IEEE80211, IFM_MANUAL, 0, 0), 0);
	}
#undef ADD
	ifmedia_set(&sc->sc_media,
	    IFM_MAKEWORD(IFM_IEEE80211, IFM_AUTO, 0, 0));

	/*
	 * Call MI attach routines.
	 */
	if_attach(ifp);
	memcpy(((struct arpcom *)ifp)->ac_enaddr, ic->ic_myaddr,
	    ETHER_ADDR_LEN);
	ether_ifattach(ifp);
	printf("\n");

	sc->wi_flags |= WI_FLAGS_ATTACHED;

#if NBPFILTER > 0
	BPFATTACH(&ifp->if_bpf, ifp, DLT_EN10MB, sizeof(struct ether_header));
#endif

	if_addgroup(ifp, "wlan");
	ifp->if_priority = IF_WIRELESS_DEFAULT_PRIORITY;

	wi_init(sc);
	wi_stop(sc);

	return (0);
}

STATIC void
wi_intr_enable(struct wi_softc *sc, int mode)
{
	if (!(sc->wi_flags & WI_FLAGS_BUS_USB))
		CSR_WRITE_2(sc, WI_INT_EN, mode);
}

STATIC void
wi_intr_ack(struct wi_softc *sc, int mode)
{
	if (!(sc->wi_flags & WI_FLAGS_BUS_USB))
		CSR_WRITE_2(sc, WI_EVENT_ACK, mode);
}

int
wi_intr(void *vsc)
{
	struct wi_softc		*sc = vsc;
	struct ifnet		*ifp;
	u_int16_t		status;

	DPRINTF(WID_INTR, ("wi_intr: sc %p\n", sc));

	ifp = &sc->sc_ic.ic_if;

	if (!(sc->wi_flags & WI_FLAGS_ATTACHED) || !(ifp->if_flags & IFF_UP)) {
		CSR_WRITE_2(sc, WI_INT_EN, 0);
		CSR_WRITE_2(sc, WI_EVENT_ACK, 0xffff);
		return (0);
	}

	/* Disable interrupts. */
	CSR_WRITE_2(sc, WI_INT_EN, 0);

	status = CSR_READ_2(sc, WI_EVENT_STAT);
	CSR_WRITE_2(sc, WI_EVENT_ACK, ~WI_INTRS);

	if (status & WI_EV_RX) {
		wi_rxeof(sc);
		CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_RX);
	}

	if (status & WI_EV_TX) {
		wi_txeof(sc, status);
		CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_TX);
	}

	if (status & WI_EV_ALLOC) {
		int			id;
		id = CSR_READ_2(sc, WI_ALLOC_FID);
		CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_ALLOC);
		if (id == sc->wi_tx_data_id)
			wi_txeof(sc, status);
	}

	if (status & WI_EV_INFO) {
		wi_update_stats(sc);
		CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_INFO);
	}

	if (status & WI_EV_TX_EXC) {
		wi_txeof(sc, status);
		CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_TX_EXC);
	}

	if (status & WI_EV_INFO_DROP) {
		CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_INFO_DROP);
	}

	/* Re-enable interrupts. */
	CSR_WRITE_2(sc, WI_INT_EN, WI_INTRS);

	if (status == 0)
		return (0);

	if (!ifq_empty(&ifp->if_snd))
		wi_start(ifp);

	return (1);
}

STATIC int
wi_get_fid_io(struct wi_softc *sc, int fid)
{
	return CSR_READ_2(sc, fid);
}


void
wi_rxeof(struct wi_softc *sc)
{
	struct ifnet		*ifp;
	struct ether_header	*eh;
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct mbuf		*m;
	caddr_t			olddata;
	u_int16_t		ftype;
	int			maxlen;
	int			id;

	ifp = &sc->sc_ic.ic_if;

	id = wi_get_fid(sc, WI_RX_FID);

	if (sc->wi_procframe || sc->wi_debug.wi_monitor) {
		struct wi_frame	*rx_frame;
		int		datlen, hdrlen;

		MGETHDR(m, M_DONTWAIT, MT_DATA);
		if (m == NULL) {
			ifp->if_ierrors++;
			return;
		}
		MCLGET(m, M_DONTWAIT);
		if (!(m->m_flags & M_EXT)) {
			m_freem(m);
			ifp->if_ierrors++;
			return;
		}

		if (wi_read_data(sc, id, 0, mtod(m, caddr_t),
		    sizeof(struct wi_frame))) {
			m_freem(m);
			ifp->if_ierrors++;
			return;
		}

		rx_frame = mtod(m, struct wi_frame *);

		if (rx_frame->wi_status & htole16(WI_STAT_BADCRC)) {
			m_freem(m);
			ifp->if_ierrors++;
			return;
		}

		switch ((letoh16(rx_frame->wi_status) & WI_STAT_MAC_PORT)
		    >> 8) {
		case 7:
			switch (letoh16(rx_frame->wi_frame_ctl) &
			    WI_FCTL_FTYPE) {
			case WI_FTYPE_DATA:
				hdrlen = WI_DATA_HDRLEN;
				datlen = letoh16(rx_frame->wi_dat_len);
				break;
			case WI_FTYPE_MGMT:
				hdrlen = WI_MGMT_HDRLEN;
				datlen = letoh16(rx_frame->wi_dat_len);
				break;
			case WI_FTYPE_CTL:
				hdrlen = WI_CTL_HDRLEN;
				datlen = 0;
				break;
			default:
				printf(WI_PRT_FMT ": received packet of "
				    "unknown type on port 7\n", WI_PRT_ARG(sc));
				m_freem(m);
				ifp->if_ierrors++;
				return;
			}
			break;
		case 0:
			hdrlen = WI_DATA_HDRLEN;
			datlen = letoh16(rx_frame->wi_dat_len);
			break;
		default:
			printf(WI_PRT_FMT ": received packet on invalid port "
			    "(wi_status=0x%x)\n", WI_PRT_ARG(sc),
			    letoh16(rx_frame->wi_status));
			m_freem(m);
			ifp->if_ierrors++;
			return;
		}

		if ((hdrlen + datlen + 2) > MCLBYTES) {
			m_freem(m);
			ifp->if_ierrors++;
			return;
		}

		if (wi_read_data(sc, id, hdrlen, mtod(m, caddr_t) + hdrlen,
		    datlen + 2)) {
			m_freem(m);
			ifp->if_ierrors++;
			return;
		}

		m->m_pkthdr.len = m->m_len = hdrlen + datlen;
	} else {
		struct wi_frame rx_frame;

		/* First read in the frame header */
		if (wi_read_data(sc, id, 0, (caddr_t)&rx_frame,
		    sizeof(rx_frame))) {
			ifp->if_ierrors++;
			return;
		}

		/* Drop undecryptable or packets with receive errors here */
		if (rx_frame.wi_status & htole16(WI_STAT_ERRSTAT)) {
			ifp->if_ierrors++;
			return;
		}

		/* Stash frame type in host byte order for later use */
		ftype = letoh16(rx_frame.wi_frame_ctl) & WI_FCTL_FTYPE;

		MGETHDR(m, M_DONTWAIT, MT_DATA);
		if (m == NULL) {
			ifp->if_ierrors++;
			return;
		}
		MCLGET(m, M_DONTWAIT);
		if (!(m->m_flags & M_EXT)) {
			m_freem(m);
			ifp->if_ierrors++;
			return;
		}

		olddata = m->m_data;
		/* Align the data after the ethernet header */
		m->m_data = (caddr_t)ALIGN(m->m_data +
		    sizeof(struct ether_header)) - sizeof(struct ether_header);

		eh = mtod(m, struct ether_header *);
		maxlen = MCLBYTES - (m->m_data - olddata);

		if (ftype == WI_FTYPE_MGMT &&
		    sc->wi_ptype == WI_PORTTYPE_HOSTAP) {

			u_int16_t rxlen = letoh16(rx_frame.wi_dat_len);

			if ((WI_802_11_OFFSET_RAW + rxlen + 2) > maxlen) {
				printf("%s: oversized mgmt packet received in "
				    "hostap mode (wi_dat_len=%d, "
				    "wi_status=0x%x)\n", sc->sc_dev.dv_xname,
				    rxlen, letoh16(rx_frame.wi_status));
				m_freem(m);
				ifp->if_ierrors++;  
				return;
			}

			/* Put the whole header in there. */
			bcopy(&rx_frame, mtod(m, void *),
			    sizeof(struct wi_frame));
			if (wi_read_data(sc, id, WI_802_11_OFFSET_RAW,
			    mtod(m, caddr_t) + WI_802_11_OFFSET_RAW,
			    rxlen + 2)) {
				m_freem(m);
				if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
					printf("wihap: failed to copy header\n");
				ifp->if_ierrors++;
				return;
			}

			m->m_pkthdr.len = m->m_len =
			    WI_802_11_OFFSET_RAW + rxlen;

			/* XXX: consider giving packet to bhp? */

			wihap_mgmt_input(sc, &rx_frame, m);

			return;
		}

		switch (letoh16(rx_frame.wi_status) & WI_RXSTAT_MSG_TYPE) {
		case WI_STAT_1042:
		case WI_STAT_TUNNEL:
		case WI_STAT_WMP_MSG:
			if ((letoh16(rx_frame.wi_dat_len) + WI_SNAPHDR_LEN) >
			    maxlen) {
				printf(WI_PRT_FMT ": oversized packet received "
				    "(wi_dat_len=%d, wi_status=0x%x)\n",
				    WI_PRT_ARG(sc),
				    letoh16(rx_frame.wi_dat_len),
				    letoh16(rx_frame.wi_status));
				m_freem(m);
				ifp->if_ierrors++;
				return;
			}
			m->m_pkthdr.len = m->m_len =
			    letoh16(rx_frame.wi_dat_len) + WI_SNAPHDR_LEN;

			bcopy(&rx_frame.wi_dst_addr,
			    &eh->ether_dhost, ETHER_ADDR_LEN);
			bcopy(&rx_frame.wi_src_addr,
			    &eh->ether_shost, ETHER_ADDR_LEN);
			bcopy(&rx_frame.wi_type,
			    &eh->ether_type, ETHER_TYPE_LEN);

			if (wi_read_data(sc, id, WI_802_11_OFFSET,
			    mtod(m, caddr_t) + sizeof(struct ether_header),
			    m->m_len + 2)) {
				ifp->if_ierrors++;
				m_freem(m);
				return;
			}
			break;
		default:
			if ((letoh16(rx_frame.wi_dat_len) +
			    sizeof(struct ether_header)) > maxlen) {
				printf(WI_PRT_FMT ": oversized packet received "
				    "(wi_dat_len=%d, wi_status=0x%x)\n",
				    WI_PRT_ARG(sc),
				    letoh16(rx_frame.wi_dat_len),
				    letoh16(rx_frame.wi_status));
				m_freem(m);
				ifp->if_ierrors++;
				return;
			}
			m->m_pkthdr.len = m->m_len =
			    letoh16(rx_frame.wi_dat_len) +
			    sizeof(struct ether_header);

			if (wi_read_data(sc, id, WI_802_3_OFFSET,
			    mtod(m, caddr_t), m->m_len + 2)) {
				m_freem(m);
				ifp->if_ierrors++;
				return;
			}
			break;
		}

		if (sc->wi_use_wep &&
		    rx_frame.wi_frame_ctl & htole16(WI_FCTL_WEP)) {
			int len;

			switch (sc->wi_crypto_algorithm) {
			case WI_CRYPTO_FIRMWARE_WEP:
				break;
			case WI_CRYPTO_SOFTWARE_WEP:
				m_copydata(m, 0, m->m_pkthdr.len,
				    sc->wi_rxbuf);
				len = m->m_pkthdr.len -
				    sizeof(struct ether_header);
				if (wi_do_hostdecrypt(sc, sc->wi_rxbuf +
				    sizeof(struct ether_header), len)) {
					if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
						printf(WI_PRT_FMT ": Error decrypting incoming packet.\n", WI_PRT_ARG(sc));
					m_freem(m);
					ifp->if_ierrors++;  
					return;
				}
				len -= IEEE80211_WEP_IVLEN +
				    IEEE80211_WEP_KIDLEN + IEEE80211_WEP_CRCLEN;
				/*
				 * copy data back to mbufs:
				 * we need to ditch the IV & most LLC/SNAP stuff
				 * (except SNAP type, we're going use that to
				 * overwrite the ethertype in the ether_header)
				 */
				m_copyback(m, sizeof(struct ether_header) -
				    WI_ETHERTYPE_LEN, WI_ETHERTYPE_LEN +
				    (len - WI_SNAPHDR_LEN),
				    sc->wi_rxbuf + sizeof(struct ether_header) +
				    IEEE80211_WEP_IVLEN +
				    IEEE80211_WEP_KIDLEN + WI_SNAPHDR_LEN,
				    M_NOWAIT);
				m_adj(m, -(WI_ETHERTYPE_LEN +
				    IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN +
				    WI_SNAPHDR_LEN));
				break;
			}
		}

		if (sc->wi_ptype == WI_PORTTYPE_HOSTAP) {
			/*
			 * Give host AP code first crack at data packets.
			 * If it decides to handle it (or drop it), it will
			 * return a non-zero.  Otherwise, it is destined for
			 * this host.
			 */
			if (wihap_data_input(sc, &rx_frame, m))
				return;
		}
	}

	/* Receive packet unless in procframe or monitor mode. */
	if (sc->wi_procframe || sc->wi_debug.wi_monitor)
		m_freem(m);
	else {
		ml_enqueue(&ml, m);
		if_input(ifp, &ml);
	}

	return;
}

void
wi_txeof(struct wi_softc *sc, int status)
{
	struct ifnet		*ifp;

	ifp = &sc->sc_ic.ic_if;

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	if (status & WI_EV_TX_EXC)
		ifp->if_oerrors++;

	return;
}

void
wi_inquire(void *xsc)
{
	struct wi_softc		*sc;
	struct ifnet		*ifp;
	int s, rv;

	sc = xsc;
	ifp = &sc->sc_ic.ic_if;

	timeout_add_sec(&sc->sc_timo, 60);

	/* Don't do this while we're transmitting */
	if (ifq_is_oactive(&ifp->if_snd))
		return;

	s = splnet();
	rv = wi_cmd(sc, WI_CMD_INQUIRE, WI_INFO_COUNTERS, 0, 0);
	splx(s);
	if (rv)
		printf(WI_PRT_FMT ": wi_cmd failed with %d\n", WI_PRT_ARG(sc),
		    rv);

	return;
}

void
wi_update_stats(struct wi_softc *sc)
{
	struct wi_ltv_gen	gen;
	u_int16_t		id;
	struct ifnet		*ifp;
	u_int32_t		*ptr;
	int			len, i;
	u_int16_t		t;

	ifp = &sc->sc_ic.ic_if;

	id = wi_get_fid(sc, WI_INFO_FID);

	wi_read_data(sc, id, 0, (char *)&gen, 4);

	if (gen.wi_type == htole16(WI_INFO_SCAN_RESULTS)) {
		sc->wi_scanbuf_len = letoh16(gen.wi_len);
		wi_read_data(sc, id, 4, (caddr_t)sc->wi_scanbuf,
		    sc->wi_scanbuf_len * 2);
		return;
	} else if (gen.wi_type != htole16(WI_INFO_COUNTERS))
		return;

	/* Some card versions have a larger stats structure */
	len = (letoh16(gen.wi_len) - 1 < sizeof(sc->wi_stats) / 4) ?
	    letoh16(gen.wi_len) - 1 : sizeof(sc->wi_stats) / 4;

	ptr = (u_int32_t *)&sc->wi_stats;

	for (i = 0; i < len; i++) {
		if (sc->wi_flags & WI_FLAGS_BUS_USB) {
			wi_read_data(sc, id, 4 + i*2, (char *)&t, 2);
			t = letoh16(t);
		} else 
			t = CSR_READ_2(sc, WI_DATA1);
#ifdef WI_HERMES_STATS_WAR
		if (t > 0xF000)
			t = ~t & 0xFFFF;
#endif
		ptr[i] += t;
	}

	ifp->if_collisions = sc->wi_stats.wi_tx_single_retries +
	    sc->wi_stats.wi_tx_multi_retries +
	    sc->wi_stats.wi_tx_retry_limit;

	return;
}

STATIC int
wi_cmd_io(struct wi_softc *sc, int cmd, int val0, int val1, int val2)
{
	int			i, s = 0;

	/* Wait for the busy bit to clear. */
	for (i = sc->wi_cmd_count; i--; DELAY(1000)) {
		if (!(CSR_READ_2(sc, WI_COMMAND) & WI_CMD_BUSY))
			break;
	}
	if (i < 0) {
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf(WI_PRT_FMT ": wi_cmd_io: busy bit won't clear\n",
			    WI_PRT_ARG(sc));
		return(ETIMEDOUT);
	}

	CSR_WRITE_2(sc, WI_PARAM0, val0);
	CSR_WRITE_2(sc, WI_PARAM1, val1);
	CSR_WRITE_2(sc, WI_PARAM2, val2);
	CSR_WRITE_2(sc, WI_COMMAND, cmd);

	for (i = WI_TIMEOUT; i--; DELAY(WI_DELAY)) {
		/*
		 * Wait for 'command complete' bit to be
		 * set in the event status register.
		 */
		s = CSR_READ_2(sc, WI_EVENT_STAT) & WI_EV_CMD;
		if (s) {
			/* Ack the event and read result code. */
			s = CSR_READ_2(sc, WI_STATUS);
			CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_CMD);
			if (s & WI_STAT_CMD_RESULT)
				return(EIO);
			break;
		}
	}

	if (i < 0) {
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf(WI_PRT_FMT
			    ": timeout in wi_cmd 0x%04x; event status 0x%04x\n",
			    WI_PRT_ARG(sc), cmd, s);
		return(ETIMEDOUT);
	}

	return(0);
}

STATIC void
wi_reset(struct wi_softc *sc)
{
	int error, tries = 3;

	DPRINTF(WID_RESET, ("wi_reset: sc %p\n", sc));

	/* Symbol firmware cannot be initialized more than once. */
	if (sc->sc_firmware_type == WI_SYMBOL) {
		if (sc->wi_flags & WI_FLAGS_INITIALIZED)
			return;
		tries = 1;
	}

	for (; tries--; DELAY(WI_DELAY * 1000)) {
		if ((error = wi_cmd(sc, WI_CMD_INI, 0, 0, 0)) == 0)
			break;
	}
	if (tries < 0) {
		printf(WI_PRT_FMT ": init failed\n", WI_PRT_ARG(sc));
		return;
	}
	sc->wi_flags |= WI_FLAGS_INITIALIZED;

	wi_intr_enable(sc, 0);
	wi_intr_ack(sc, 0xffff);

	/* Calibrate timer. */
	WI_SETVAL(WI_RID_TICK_TIME, 8);

	return;
}

STATIC void
wi_cor_reset(struct wi_softc *sc)
{
	u_int8_t cor_value;

	DPRINTF(WID_RESET, ("wi_cor_reset: sc %p\n", sc));

	/*
	 * Do a soft reset of the card; this is required for Symbol cards.
	 * This shouldn't hurt other cards but there have been reports
	 * of the COR reset messing up old Lucent firmware revisions so
	 * we avoid soft reset on Lucent cards for now.
	 */
	if (sc->sc_firmware_type != WI_LUCENT) {
		cor_value = bus_space_read_1(sc->wi_ltag, sc->wi_lhandle,
		    sc->wi_cor_offset);
		bus_space_write_1(sc->wi_ltag, sc->wi_lhandle,
		    sc->wi_cor_offset, (cor_value | WI_COR_SOFT_RESET));
		DELAY(1000);
		bus_space_write_1(sc->wi_ltag, sc->wi_lhandle,
		    sc->wi_cor_offset, (cor_value & ~WI_COR_SOFT_RESET));
		DELAY(1000);
	}

	return;
}

/*
 * Read an LTV record from the NIC.
 */
STATIC int
wi_read_record_io(struct wi_softc *sc, struct wi_ltv_gen *ltv)
{
	u_int8_t		*ptr;
	int			len, code;
	struct wi_ltv_gen	*oltv, p2ltv;

	if (sc->sc_firmware_type != WI_LUCENT) {
		oltv = ltv;
		switch (ltv->wi_type) {
		case WI_RID_ENCRYPTION:
			p2ltv.wi_type = WI_RID_P2_ENCRYPTION;
			p2ltv.wi_len = 2;
			ltv = &p2ltv;
			break;
		case WI_RID_TX_CRYPT_KEY:
			if (ltv->wi_val > WI_NLTV_KEYS)
				return (EINVAL);
			p2ltv.wi_type = WI_RID_P2_TX_CRYPT_KEY;
			p2ltv.wi_len = 2;
			ltv = &p2ltv;
			break;
		}
	}

	/* Tell the NIC to enter record read mode. */
	if (wi_cmd(sc, WI_CMD_ACCESS|WI_ACCESS_READ, ltv->wi_type, 0, 0))
		return(EIO);

	/* Seek to the record. */
	if (wi_seek(sc, ltv->wi_type, 0, WI_BAP1))
		return(EIO);

	/*
	 * Read the length and record type and make sure they
	 * match what we expect (this verifies that we have enough
	 * room to hold all of the returned data).
	 */
	len = CSR_READ_2(sc, WI_DATA1);
	if (len > ltv->wi_len)
		return(ENOSPC);
	code = CSR_READ_2(sc, WI_DATA1);
	if (code != ltv->wi_type)
		return(EIO);

	ltv->wi_len = len;
	ltv->wi_type = code;

	/* Now read the data. */
	ptr = (u_int8_t *)&ltv->wi_val;
	if (ltv->wi_len > 1)
		CSR_READ_RAW_2(sc, WI_DATA1, ptr, (ltv->wi_len-1)*2);

	if (ltv->wi_type == WI_RID_PORTTYPE && sc->wi_ptype == WI_PORTTYPE_IBSS
	    && ltv->wi_val == sc->wi_ibss_port) {
		/*
		 * Convert vendor IBSS port type to WI_PORTTYPE_IBSS.
		 * Since Lucent uses port type 1 for BSS *and* IBSS we
		 * have to rely on wi_ptype to distinguish this for us.
		 */
		ltv->wi_val = htole16(WI_PORTTYPE_IBSS);
	} else if (sc->sc_firmware_type != WI_LUCENT) {
		int v;

		switch (oltv->wi_type) {
		case WI_RID_TX_RATE:
		case WI_RID_CUR_TX_RATE:
			switch (letoh16(ltv->wi_val)) {
			case 1: v = 1; break;
			case 2: v = 2; break;
			case 3:	v = 6; break;
			case 4: v = 5; break;
			case 7: v = 7; break;
			case 8: v = 11; break;
			case 15: v = 3; break;
			default: v = 0x100 + letoh16(ltv->wi_val); break;
			}
			oltv->wi_val = htole16(v);
			break;
		case WI_RID_ENCRYPTION:
			oltv->wi_len = 2;
			if (ltv->wi_val & htole16(0x01))
				oltv->wi_val = htole16(1);
			else
				oltv->wi_val = htole16(0);
			break;
		case WI_RID_TX_CRYPT_KEY:
		case WI_RID_CNFAUTHMODE:
			oltv->wi_len = 2;
			oltv->wi_val = ltv->wi_val;
			break;
		}
	}

	return(0);
}

/*
 * Same as read, except we inject data instead of reading it.
 */
STATIC int
wi_write_record_io(struct wi_softc *sc, struct wi_ltv_gen *ltv)
{
	u_int8_t		*ptr;
	u_int16_t		val = 0;
	int			i;
	struct wi_ltv_gen	p2ltv;

	if (ltv->wi_type == WI_RID_PORTTYPE &&
	    letoh16(ltv->wi_val) == WI_PORTTYPE_IBSS) {
		/* Convert WI_PORTTYPE_IBSS to vendor IBSS port type. */
		p2ltv.wi_type = WI_RID_PORTTYPE;
		p2ltv.wi_len = 2;
		p2ltv.wi_val = sc->wi_ibss_port;
		ltv = &p2ltv;
	} else if (sc->sc_firmware_type != WI_LUCENT) {
		int v;

		switch (ltv->wi_type) {
		case WI_RID_TX_RATE:
			p2ltv.wi_type = WI_RID_TX_RATE;
			p2ltv.wi_len = 2;
			switch (letoh16(ltv->wi_val)) {
			case 1: v = 1; break;
			case 2: v = 2; break;
			case 3:	v = 15; break;
			case 5: v = 4; break;
			case 6: v = 3; break;
			case 7: v = 7; break;
			case 11: v = 8; break;
			default: return EINVAL;
			}
			p2ltv.wi_val = htole16(v);
			ltv = &p2ltv;
			break;
		case WI_RID_ENCRYPTION:
			p2ltv.wi_type = WI_RID_P2_ENCRYPTION;
			p2ltv.wi_len = 2;
			if (ltv->wi_val & htole16(0x01)) {
				val = PRIVACY_INVOKED;
				/*
				 * If using shared key WEP we must set the
				 * EXCLUDE_UNENCRYPTED bit.  Symbol cards
				 * need this bit set even when not using
				 * shared key. We can't just test for
				 * IEEE80211_AUTH_SHARED since Symbol cards
				 * have 2 shared key modes.
				 */
				if (sc->wi_authtype != IEEE80211_AUTH_OPEN ||
				    sc->sc_firmware_type == WI_SYMBOL)
					val |= EXCLUDE_UNENCRYPTED;

				switch (sc->wi_crypto_algorithm) {
				case WI_CRYPTO_FIRMWARE_WEP:
					/*
					 * TX encryption is broken in
					 * Host AP mode.
					 */
					if (sc->wi_ptype == WI_PORTTYPE_HOSTAP)
						val |= HOST_ENCRYPT;
					break;
				case WI_CRYPTO_SOFTWARE_WEP:
					val |= HOST_ENCRYPT|HOST_DECRYPT;
					break;
				}
				p2ltv.wi_val = htole16(val);
			} else
				p2ltv.wi_val = htole16(HOST_ENCRYPT | HOST_DECRYPT);
			ltv = &p2ltv;
			break;
		case WI_RID_TX_CRYPT_KEY:
			if (ltv->wi_val > WI_NLTV_KEYS)
				return (EINVAL);
			p2ltv.wi_type = WI_RID_P2_TX_CRYPT_KEY;
			p2ltv.wi_len = 2;
			p2ltv.wi_val = ltv->wi_val;
			ltv = &p2ltv;
			break;
		case WI_RID_DEFLT_CRYPT_KEYS: {
				int error;
				int keylen;
				struct wi_ltv_str ws;
				struct wi_ltv_keys *wk = (struct wi_ltv_keys *)ltv;

				keylen = wk->wi_keys[sc->wi_tx_key].wi_keylen;
				keylen = letoh16(keylen);

				for (i = 0; i < 4; i++) {
					bzero(&ws, sizeof(ws));
					ws.wi_len = (keylen > 5) ? 8 : 4;
					ws.wi_type = WI_RID_P2_CRYPT_KEY0 + i;
					bcopy(&wk->wi_keys[i].wi_keydat,
					    ws.wi_str, keylen);
					error = wi_write_record(sc,
					    (struct wi_ltv_gen *)&ws);
					if (error)
						return (error);
				}
			}
			return (0);
		}
	}

	if (wi_seek(sc, ltv->wi_type, 0, WI_BAP1))
		return(EIO);

	CSR_WRITE_2(sc, WI_DATA1, ltv->wi_len);
	CSR_WRITE_2(sc, WI_DATA1, ltv->wi_type);

	ptr = (u_int8_t *)&ltv->wi_val;
	if (ltv->wi_len > 1)
		CSR_WRITE_RAW_2(sc, WI_DATA1, ptr, (ltv->wi_len-1) *2);

	if (wi_cmd(sc, WI_CMD_ACCESS|WI_ACCESS_WRITE, ltv->wi_type, 0, 0))
		return(EIO);

	return(0);
}

STATIC int
wi_seek(struct wi_softc *sc, int id, int off, int chan)
{
	int			i;
	int			selreg, offreg;

	switch (chan) {
	case WI_BAP0:
		selreg = WI_SEL0;
		offreg = WI_OFF0;
		break;
	case WI_BAP1:
		selreg = WI_SEL1;
		offreg = WI_OFF1;
		break;
	default:
		printf(WI_PRT_FMT ": invalid data path: %x\n", WI_PRT_ARG(sc),
		    chan);
		return(EIO);
	}

	CSR_WRITE_2(sc, selreg, id);
	CSR_WRITE_2(sc, offreg, off);

	for (i = WI_TIMEOUT; i--; DELAY(1))
		if (!(CSR_READ_2(sc, offreg) & (WI_OFF_BUSY|WI_OFF_ERR)))
			break;

	if (i < 0)
		return(ETIMEDOUT);

	return(0);
}

STATIC int
wi_read_data_io(struct wi_softc *sc, int id, int off, caddr_t buf, int len)
{
	u_int8_t		*ptr;

	if (wi_seek(sc, id, off, WI_BAP1))
		return(EIO);

	ptr = (u_int8_t *)buf;
	CSR_READ_RAW_2(sc, WI_DATA1, ptr, len);

	return(0);
}

/*
 * According to the comments in the HCF Light code, there is a bug in
 * the Hermes (or possibly in certain Hermes firmware revisions) where
 * the chip's internal autoincrement counter gets thrown off during
 * data writes: the autoincrement is missed, causing one data word to
 * be overwritten and subsequent words to be written to the wrong memory
 * locations. The end result is that we could end up transmitting bogus
 * frames without realizing it. The workaround for this is to write a
 * couple of extra guard words after the end of the transfer, then
 * attempt to read then back. If we fail to locate the guard words where
 * we expect them, we preform the transfer over again.
 */
STATIC int
wi_write_data_io(struct wi_softc *sc, int id, int off, caddr_t buf, int len)
{
	u_int8_t		*ptr;

#ifdef WI_HERMES_AUTOINC_WAR
again:
#endif

	if (wi_seek(sc, id, off, WI_BAP0))
		return(EIO);

	ptr = (u_int8_t *)buf;
	CSR_WRITE_RAW_2(sc, WI_DATA0, ptr, len);

#ifdef WI_HERMES_AUTOINC_WAR
	CSR_WRITE_2(sc, WI_DATA0, 0x1234);
	CSR_WRITE_2(sc, WI_DATA0, 0x5678);

	if (wi_seek(sc, id, off + len, WI_BAP0))
		return(EIO);

	if (CSR_READ_2(sc, WI_DATA0) != 0x1234 ||
	    CSR_READ_2(sc, WI_DATA0) != 0x5678)
		goto again;
#endif

	return(0);
}

/*
 * Allocate a region of memory inside the NIC and zero
 * it out.
 */
STATIC int
wi_alloc_nicmem_io(struct wi_softc *sc, int len, int *id)
{
	int			i;

	if (wi_cmd(sc, WI_CMD_ALLOC_MEM, len, 0, 0)) {
		printf(WI_PRT_FMT ": failed to allocate %d bytes on NIC\n",
		    WI_PRT_ARG(sc), len);
		return(ENOMEM);
	}

	for (i = WI_TIMEOUT; i--; DELAY(1)) {
		if (CSR_READ_2(sc, WI_EVENT_STAT) & WI_EV_ALLOC)
			break;
	}

	if (i < 0)
		return(ETIMEDOUT);

	*id = CSR_READ_2(sc, WI_ALLOC_FID);
	CSR_WRITE_2(sc, WI_EVENT_ACK, WI_EV_ALLOC);

	if (wi_seek(sc, *id, 0, WI_BAP0))
		return(EIO);

	for (i = 0; i < len / 2; i++)
		CSR_WRITE_2(sc, WI_DATA0, 0);

	return(0);
}

STATIC void
wi_setmulti(struct wi_softc *sc)
{
	struct arpcom		*ac = &sc->sc_ic.ic_ac;
	struct ifnet		*ifp;
	int			i = 0;
	struct wi_ltv_mcast	mcast;
	struct ether_multistep	step;
	struct ether_multi	*enm;

	ifp = &sc->sc_ic.ic_if;

	bzero(&mcast, sizeof(mcast));

	mcast.wi_type = WI_RID_MCAST_LIST;
	mcast.wi_len = ((ETHER_ADDR_LEN / 2) * 16) + 1;

	if (ac->ac_multirangecnt > 0)
		ifp->if_flags |= IFF_ALLMULTI;

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		wi_write_record(sc, (struct wi_ltv_gen *)&mcast);
		return;
	}

	ETHER_FIRST_MULTI(step, &sc->sc_ic.ic_ac, enm);
	while (enm != NULL) {
		if (i >= 16) {
			bzero(&mcast, sizeof(mcast));
			break;
		}

		bcopy(enm->enm_addrlo, &mcast.wi_mcast[i], ETHER_ADDR_LEN);
		i++;
		ETHER_NEXT_MULTI(step, enm);
	}

	mcast.wi_len = (i * 3) + 1;
	wi_write_record(sc, (struct wi_ltv_gen *)&mcast);

	return;
}

STATIC int
wi_setdef(struct wi_softc *sc, struct wi_req *wreq)
{
	struct ifnet		*ifp;
	int error = 0;

	ifp = &sc->sc_ic.ic_if;

	switch(wreq->wi_type) {
	case WI_RID_MAC_NODE:
		bcopy(&wreq->wi_val, LLADDR(ifp->if_sadl), ETHER_ADDR_LEN);
		bcopy(&wreq->wi_val, &sc->sc_ic.ic_myaddr, ETHER_ADDR_LEN);
		break;
	case WI_RID_PORTTYPE:
		error = wi_sync_media(sc, letoh16(wreq->wi_val[0]),
		    sc->wi_tx_rate);
		break;
	case WI_RID_TX_RATE:
		error = wi_sync_media(sc, sc->wi_ptype,
		    letoh16(wreq->wi_val[0]));
		break;
	case WI_RID_MAX_DATALEN:
		sc->wi_max_data_len = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_RTS_THRESH:
		sc->wi_rts_thresh = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_SYSTEM_SCALE:
		sc->wi_ap_density = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_CREATE_IBSS:
		sc->wi_create_ibss = letoh16(wreq->wi_val[0]);
		error = wi_sync_media(sc, sc->wi_ptype, sc->wi_tx_rate);
		break;
	case WI_RID_OWN_CHNL:
		sc->wi_channel = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_NODENAME:
		error = wi_set_ssid(&sc->wi_node_name,
		    (u_int8_t *)&wreq->wi_val[1], letoh16(wreq->wi_val[0]));
		break;
	case WI_RID_DESIRED_SSID:
		error = wi_set_ssid(&sc->wi_net_name,
		    (u_int8_t *)&wreq->wi_val[1], letoh16(wreq->wi_val[0]));
		break;
	case WI_RID_OWN_SSID:
		error = wi_set_ssid(&sc->wi_ibss_name,
		    (u_int8_t *)&wreq->wi_val[1], letoh16(wreq->wi_val[0]));
		break;
	case WI_RID_PM_ENABLED:
		sc->wi_pm_enabled = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_MICROWAVE_OVEN:
		sc->wi_mor_enabled = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_MAX_SLEEP:
		sc->wi_max_sleep = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_CNFAUTHMODE:
		sc->wi_authtype = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_ROAMING_MODE:
		sc->wi_roaming = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_SYMBOL_DIVERSITY:
		sc->wi_diversity = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_ENH_SECURITY:
		sc->wi_enh_security = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_ENCRYPTION:
		sc->wi_use_wep = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_TX_CRYPT_KEY:
		sc->wi_tx_key = letoh16(wreq->wi_val[0]);
		break;
	case WI_RID_DEFLT_CRYPT_KEYS:
		bcopy(wreq, &sc->wi_keys, sizeof(struct wi_ltv_keys));
		break;
	case WI_FRID_CRYPTO_ALG:
		switch (letoh16(wreq->wi_val[0])) {
		case WI_CRYPTO_FIRMWARE_WEP:
			sc->wi_crypto_algorithm = WI_CRYPTO_FIRMWARE_WEP;
			break;
		case WI_CRYPTO_SOFTWARE_WEP:
			sc->wi_crypto_algorithm = WI_CRYPTO_SOFTWARE_WEP;
			break;
		default:
			printf(WI_PRT_FMT ": unsupported crypto algorithm %d\n",
			    WI_PRT_ARG(sc), letoh16(wreq->wi_val[0]));
			error = EINVAL;
		}
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

STATIC int
wi_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	int			s, error = 0, i, j, len;
	struct wi_softc		*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	struct proc		*p = curproc;
	struct wi_scan_res	*res;
	struct wi_scan_p2_hdr	*p2;
	struct wi_req		*wreq = NULL;
	u_int32_t		flags;
	struct ieee80211_nwid		*nwidp = NULL;
	struct ieee80211_nodereq_all	*na;
	struct ieee80211_bssid		*bssid;

	s = splnet();
	if (!(sc->wi_flags & WI_FLAGS_ATTACHED)) {
		error = ENODEV;
		goto fail;
	}

	/*
	 * Prevent processes from entering this function while another
	 * process is tsleep'ing in it.
	 */
	while ((sc->wi_flags & WI_FLAGS_BUSY) && error == 0)
		error = tsleep_nsec(&sc->wi_flags, PCATCH, "wiioc", INFSLP);
	if (error != 0) {
		splx(s);
		return error;
	}
	sc->wi_flags |= WI_FLAGS_BUSY;


	DPRINTF (WID_IOCTL, ("wi_ioctl: command %lu data %p\n",
	    command, data));

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		wi_init(sc);
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->wi_if_flags & IFF_PROMISC)) {
				if (sc->wi_ptype != WI_PORTTYPE_HOSTAP)
					WI_SETVAL(WI_RID_PROMISC, 1);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->wi_if_flags & IFF_PROMISC) {
				if (sc->wi_ptype != WI_PORTTYPE_HOSTAP)
					WI_SETVAL(WI_RID_PROMISC, 0);
			} else
				wi_init(sc);
		} else if (ifp->if_flags & IFF_RUNNING)
			wi_stop(sc);
		sc->wi_if_flags = ifp->if_flags;
		error = 0;
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, command);
		break;
	case SIOCGWAVELAN:
		wreq = malloc(sizeof *wreq, M_DEVBUF, M_WAITOK | M_ZERO);
		error = copyin(ifr->ifr_data, wreq, sizeof(*wreq));
		if (error)
			break;
		if (wreq->wi_len > WI_MAX_DATALEN) {
			error = EINVAL;
			break;
		}
		switch (wreq->wi_type) {
		case WI_RID_IFACE_STATS:
			/* XXX native byte order */
			bcopy(&sc->wi_stats, &wreq->wi_val,
			    sizeof(sc->wi_stats));
			wreq->wi_len = (sizeof(sc->wi_stats) / 2) + 1;
			break;
		case WI_RID_DEFLT_CRYPT_KEYS:
			/* For non-root user, return all-zeroes keys */
			if (suser(p))
				bzero(wreq, sizeof(struct wi_ltv_keys));
			else
				bcopy(&sc->wi_keys, wreq,
					sizeof(struct wi_ltv_keys));
			break;
		case WI_RID_PROCFRAME:
			wreq->wi_len = 2;
			wreq->wi_val[0] = htole16(sc->wi_procframe);
			break;
		case WI_RID_PRISM2:
			wreq->wi_len = 2;
			wreq->wi_val[0] = htole16(sc->sc_firmware_type ==
			    WI_LUCENT ? 0 : 1);
			break;
		case WI_FRID_CRYPTO_ALG:
			wreq->wi_val[0] =
			    htole16((u_int16_t)sc->wi_crypto_algorithm);
			wreq->wi_len = 1;
			break;
		case WI_RID_SCAN_RES:
			if (sc->sc_firmware_type == WI_LUCENT) {
				memcpy((char *)wreq->wi_val,
				    (char *)sc->wi_scanbuf,
				    sc->wi_scanbuf_len * 2);
				wreq->wi_len = sc->wi_scanbuf_len;
				break;
			}
			/* FALLTHROUGH */
		default:
			if (wi_read_record(sc, (struct wi_ltv_gen *)wreq)) {
				error = EINVAL;
			}
			break;
		}
		error = copyout(wreq, ifr->ifr_data, sizeof(*wreq));
		break;
	case SIOCSWAVELAN:
		if ((error = suser(curproc)) != 0)
			break;
		wreq = malloc(sizeof *wreq, M_DEVBUF, M_WAITOK | M_ZERO);
		error = copyin(ifr->ifr_data, wreq, sizeof(*wreq));
		if (error)
			break;
		error = EINVAL;
		if (wreq->wi_len > WI_MAX_DATALEN)
			break;
		switch (wreq->wi_type) {
		case WI_RID_IFACE_STATS:
			break;
		case WI_RID_MGMT_XMIT:
			error = wi_mgmt_xmit(sc, (caddr_t)&wreq->wi_val,
			    wreq->wi_len);
			break;
		case WI_RID_PROCFRAME:
			sc->wi_procframe = letoh16(wreq->wi_val[0]);
			error = 0;
			break;
		case WI_RID_SCAN_REQ:
			error = 0;
			if (sc->sc_firmware_type == WI_LUCENT)
				wi_cmd(sc, WI_CMD_INQUIRE,
				    WI_INFO_SCAN_RESULTS, 0, 0);
			else
				error = wi_write_record(sc,
				    (struct wi_ltv_gen *)wreq);
			break;
		case WI_FRID_CRYPTO_ALG:
			if (sc->sc_firmware_type != WI_LUCENT) {
				error = wi_setdef(sc, wreq);
				if (!error && (ifp->if_flags & IFF_UP))
					wi_init(sc);
			}
			break;
		case WI_RID_SYMBOL_DIVERSITY:
		case WI_RID_ROAMING_MODE:
		case WI_RID_CREATE_IBSS:
		case WI_RID_MICROWAVE_OVEN:
		case WI_RID_OWN_SSID:
		case WI_RID_ENH_SECURITY:
			/*
			 * Check for features that may not be supported
			 * (must be just before default case).
			 */
			if ((wreq->wi_type == WI_RID_SYMBOL_DIVERSITY &&
			    !(sc->wi_flags & WI_FLAGS_HAS_DIVERSITY)) ||
			    (wreq->wi_type == WI_RID_ROAMING_MODE &&
			    !(sc->wi_flags & WI_FLAGS_HAS_ROAMING)) ||
			    (wreq->wi_type == WI_RID_CREATE_IBSS &&
			    !(sc->wi_flags & WI_FLAGS_HAS_CREATE_IBSS)) ||
			    (wreq->wi_type == WI_RID_MICROWAVE_OVEN &&
			    !(sc->wi_flags & WI_FLAGS_HAS_MOR)) ||
			    (wreq->wi_type == WI_RID_ENH_SECURITY &&
			    !(sc->wi_flags & WI_FLAGS_HAS_ENH_SECURITY)) ||
			    (wreq->wi_type == WI_RID_OWN_SSID &&
			    wreq->wi_len != 0))
				break;
			/* FALLTHROUGH */
		default:
			error = wi_write_record(sc, (struct wi_ltv_gen *)wreq);
			if (!error)
				error = wi_setdef(sc, wreq);
			if (!error && (ifp->if_flags & IFF_UP))
				wi_init(sc);
		}
		break;
	case SIOCGPRISM2DEBUG:
		wreq = malloc(sizeof *wreq, M_DEVBUF, M_WAITOK | M_ZERO);
		error = copyin(ifr->ifr_data, wreq, sizeof(*wreq));
		if (error)
			break;
		if (!(ifp->if_flags & IFF_RUNNING) ||
		    sc->sc_firmware_type == WI_LUCENT) {
			error = EIO;
			break;
		}
		error = wi_get_debug(sc, wreq);
		if (error == 0)
			error = copyout(wreq, ifr->ifr_data, sizeof(*wreq));
		break;
	case SIOCSPRISM2DEBUG:
		if ((error = suser(curproc)) != 0)
			break;
		wreq = malloc(sizeof *wreq, M_DEVBUF, M_WAITOK | M_ZERO);
		error = copyin(ifr->ifr_data, wreq, sizeof(*wreq));
		if (error)
			break;
		error = wi_set_debug(sc, wreq);
		break;
	case SIOCG80211NWID:
		if ((ifp->if_flags & IFF_UP) && sc->wi_net_name.i_len > 0) {
			/* Return the desired ID */
			error = copyout(&sc->wi_net_name, ifr->ifr_data,
			    sizeof(sc->wi_net_name));
		} else {
			wreq = malloc(sizeof *wreq, M_DEVBUF, M_WAITOK|M_ZERO);
			wreq->wi_type = WI_RID_CURRENT_SSID;
			wreq->wi_len = WI_MAX_DATALEN;
			if (wi_read_record(sc, (struct wi_ltv_gen *)wreq) ||
			    letoh16(wreq->wi_val[0]) > IEEE80211_NWID_LEN)
				error = EINVAL;
			else {
				nwidp = malloc(sizeof *nwidp, M_DEVBUF,
				    M_WAITOK | M_ZERO);
				wi_set_ssid(nwidp, (u_int8_t *)&wreq->wi_val[1],
				    letoh16(wreq->wi_val[0]));
				error = copyout(nwidp, ifr->ifr_data,
				    sizeof(*nwidp));
			}
		}
		break;
	case SIOCS80211NWID:
		if ((error = suser(curproc)) != 0)
			break;
		nwidp = malloc(sizeof *nwidp, M_DEVBUF, M_WAITOK);
		error = copyin(ifr->ifr_data, nwidp, sizeof(*nwidp));
		if (error)
			break;
		if (nwidp->i_len > IEEE80211_NWID_LEN) {
			error = EINVAL;
			break;
		}
		if (sc->wi_net_name.i_len == nwidp->i_len &&
		    memcmp(sc->wi_net_name.i_nwid, nwidp->i_nwid, nwidp->i_len) == 0)
			break;
		wi_set_ssid(&sc->wi_net_name, nwidp->i_nwid, nwidp->i_len);
		WI_SETSTR(WI_RID_DESIRED_SSID, sc->wi_net_name);
		if (ifp->if_flags & IFF_UP)
			/* Reinitialize WaveLAN. */
			wi_init(sc);
		break;
	case SIOCS80211NWKEY:
		if ((error = suser(curproc)) != 0)
			break;
		error = wi_set_nwkey(sc, (struct ieee80211_nwkey *)data);
		break;
	case SIOCG80211NWKEY:
		error = wi_get_nwkey(sc, (struct ieee80211_nwkey *)data);
		break;
	case SIOCS80211POWER:
		if ((error = suser(curproc)) != 0)
			break;
		error = wi_set_pm(sc, (struct ieee80211_power *)data);
		break;
	case SIOCG80211POWER:
		error = wi_get_pm(sc, (struct ieee80211_power *)data);
		break;
	case SIOCS80211TXPOWER:
		if ((error = suser(curproc)) != 0)
			break;
		error = wi_set_txpower(sc, (struct ieee80211_txpower *)data);
		break;
	case SIOCG80211TXPOWER:
		error = wi_get_txpower(sc, (struct ieee80211_txpower *)data);
		break;
	case SIOCS80211CHANNEL:
		if ((error = suser(curproc)) != 0)
			break;
		if (((struct ieee80211chanreq *)data)->i_channel > 14) {
			error = EINVAL;
			break;
		}
		wreq = malloc(sizeof *wreq, M_DEVBUF, M_WAITOK | M_ZERO);
		wreq->wi_type = WI_RID_OWN_CHNL;
		wreq->wi_val[0] =
		    htole16(((struct ieee80211chanreq *)data)->i_channel);
		error = wi_setdef(sc, wreq);
		if (!error && (ifp->if_flags & IFF_UP))
			wi_init(sc);
		break;
	case SIOCG80211CHANNEL:
		wreq = malloc(sizeof *wreq, M_DEVBUF, M_WAITOK | M_ZERO);
		wreq->wi_type = WI_RID_CURRENT_CHAN;
		wreq->wi_len = WI_MAX_DATALEN;
		if (wi_read_record(sc, (struct wi_ltv_gen *)wreq)) {
			error = EINVAL;
			break;
		}
		((struct ieee80211chanreq *)data)->i_channel =
		    letoh16(wreq->wi_val[0]);
		break;
	case SIOCG80211BSSID:
		bssid = (struct ieee80211_bssid *)data;
		wreq = malloc(sizeof *wreq, M_DEVBUF, M_WAITOK | M_ZERO);
		wreq->wi_type = WI_RID_CURRENT_BSSID;
		wreq->wi_len = WI_MAX_DATALEN;
		if (wi_read_record(sc, (struct wi_ltv_gen *)wreq)) {
			error = EINVAL;
			break;
		}
		IEEE80211_ADDR_COPY(bssid->i_bssid, wreq->wi_val);
		break;
	case SIOCS80211SCAN:
		if ((error = suser(curproc)) != 0)
			break;
		if (sc->wi_ptype == WI_PORTTYPE_HOSTAP)
			break;
		if ((ifp->if_flags & IFF_UP) == 0) {
			error = ENETDOWN;
			break;
		}
		if (sc->sc_firmware_type == WI_LUCENT) {
			wi_cmd(sc, WI_CMD_INQUIRE,
			    WI_INFO_SCAN_RESULTS, 0, 0);
		} else {
			wreq = malloc(sizeof *wreq, M_DEVBUF, M_WAITOK|M_ZERO);
			wreq->wi_len = 3;
			wreq->wi_type = WI_RID_SCAN_REQ;
			wreq->wi_val[0] = 0x3FFF;
			wreq->wi_val[1] = 0x000F;

			error = wi_write_record(sc,
			    (struct wi_ltv_gen *)wreq);
			if (error)
				break;
		}
		sc->wi_scan_lock = 0;
		timeout_set(&sc->wi_scan_timeout, wi_scan_timeout, sc);
		len = WI_WAVELAN_RES_TIMEOUT;
		if (sc->wi_flags & WI_FLAGS_BUS_USB) {
			/* Use a longer timeout for wi@usb */
			len = WI_WAVELAN_RES_TIMEOUT * 4;
		}
		timeout_add(&sc->wi_scan_timeout, len);

		/* Let the userspace process wait for completion */
		error = tsleep_nsec(&sc->wi_scan_lock, PCATCH, "wiscan",
		    SEC_TO_NSEC(IEEE80211_SCAN_TIMEOUT));
		break;
	case SIOCG80211ALLNODES:
	    {
		struct ieee80211_nodereq	*nr = NULL;

		if ((error = suser(curproc)) != 0)
			break;
		na = (struct ieee80211_nodereq_all *)data;
		if (sc->wi_ptype == WI_PORTTYPE_HOSTAP) {
			/* List all associated stations */
			error = wihap_ioctl(sc, command, data);
			break;
		}
		wreq = malloc(sizeof *wreq, M_DEVBUF, M_WAITOK | M_ZERO);
		wreq->wi_len = WI_MAX_DATALEN;
		wreq->wi_type = WI_RID_SCAN_RES;
		if (sc->sc_firmware_type == WI_LUCENT) {
			bcopy(sc->wi_scanbuf, wreq->wi_val,
			    sc->wi_scanbuf_len * 2);
			wreq->wi_len = sc->wi_scanbuf_len;
			i = 0;
			len = WI_WAVELAN_RES_SIZE;
		} else {
			if (wi_read_record(sc, (struct wi_ltv_gen *)wreq)) {
				error = EINVAL;
				break;
			}
			p2 = (struct wi_scan_p2_hdr *)wreq->wi_val;
			if (p2->wi_reason == 0)
				break;
			i = sizeof(*p2);
			len = WI_PRISM2_RES_SIZE;
		}

		for (na->na_nodes = j = 0; (i < (wreq->wi_len * 2) - len) &&
		    (na->na_size >= j + sizeof(struct ieee80211_nodereq));
		    i += len) {

			if (nr == NULL)
				nr = malloc(sizeof *nr, M_DEVBUF, M_WAITOK);
			res = (struct wi_scan_res *)((char *)wreq->wi_val + i);
			if (res == NULL)
				break;

			bzero(nr, sizeof(*nr));
			IEEE80211_ADDR_COPY(nr->nr_macaddr, res->wi_bssid);
			IEEE80211_ADDR_COPY(nr->nr_bssid, res->wi_bssid);
			nr->nr_channel = letoh16(res->wi_chan);
			nr->nr_chan_flags = IEEE80211_CHAN_B;
			nr->nr_rssi = letoh16(res->wi_signal);
			nr->nr_max_rssi = 0; /* XXX */
			nr->nr_nwid_len = letoh16(res->wi_ssid_len);
			bcopy(res->wi_ssid, nr->nr_nwid, nr->nr_nwid_len);
			nr->nr_intval = letoh16(res->wi_interval);
			nr->nr_capinfo = letoh16(res->wi_capinfo);
			nr->nr_txrate = res->wi_rate == WI_WAVELAN_RES_1M ? 2 :
			    (res->wi_rate == WI_WAVELAN_RES_2M ? 4 :
			    (res->wi_rate == WI_WAVELAN_RES_5M ? 11 :
			    (res->wi_rate == WI_WAVELAN_RES_11M ? 22 : 0)));
			nr->nr_nrates = 0;
			while (res->wi_srates[nr->nr_nrates] != 0) {
				nr->nr_rates[nr->nr_nrates] =
				    res->wi_srates[nr->nr_nrates] &
				    WI_VAR_SRATES_MASK;
				nr->nr_nrates++;
			}
			nr->nr_flags = 0;
			if (bcmp(nr->nr_macaddr, nr->nr_bssid,
			    IEEE80211_ADDR_LEN) == 0)
				nr->nr_flags |= IEEE80211_NODEREQ_AP;

			error = copyout(nr, (caddr_t)na->na_node + j,
			    sizeof(struct ieee80211_nodereq));
			if (error)
				break;
			j += sizeof(struct ieee80211_nodereq);
			na->na_nodes++;
		}
		if (nr)
			free(nr, M_DEVBUF, 0);
		break;
	    }
	case SIOCG80211FLAGS:
		if (sc->wi_ptype != WI_PORTTYPE_HOSTAP)
			break;
		ifr->ifr_flags = 0;
		if (sc->wi_flags & WI_FLAGS_HAS_ENH_SECURITY) {
			wreq = malloc(sizeof *wreq, M_DEVBUF, M_WAITOK|M_ZERO);
			wreq->wi_len = WI_MAX_DATALEN;
			wreq->wi_type = WI_RID_ENH_SECURITY;
			if (wi_read_record(sc, (struct wi_ltv_gen *)wreq)) {
				error = EINVAL;
				break;
			}
			sc->wi_enh_security = letoh16(wreq->wi_val[0]);
			if (sc->wi_enh_security == WI_HIDESSID_IGNPROBES)
				ifr->ifr_flags |= IEEE80211_F_HIDENWID;
		}
		break;
	case SIOCS80211FLAGS:
		if ((error = suser(curproc)) != 0)
			break;
		if (sc->wi_ptype != WI_PORTTYPE_HOSTAP) {
			error = EINVAL;
			break;
		}
		flags = (u_int32_t)ifr->ifr_flags;
		if (sc->wi_flags & WI_FLAGS_HAS_ENH_SECURITY) {
			sc->wi_enh_security = (flags & IEEE80211_F_HIDENWID) ?
			    WI_HIDESSID_IGNPROBES : 0;
			WI_SETVAL(WI_RID_ENH_SECURITY, sc->wi_enh_security);
		}
		break;
	case SIOCHOSTAP_ADD:
	case SIOCHOSTAP_DEL:
	case SIOCHOSTAP_GET:
	case SIOCHOSTAP_GETALL:
	case SIOCHOSTAP_GFLAGS:
	case SIOCHOSTAP_SFLAGS:
		/* Send all Host AP specific ioctl's to Host AP code. */
		error = wihap_ioctl(sc, command, data);
		break;
	default:
		error = ether_ioctl(ifp, &sc->sc_ic.ic_ac, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			wi_setmulti(sc);
		error = 0;
	}

	if (wreq)
		free(wreq, M_DEVBUF, 0);
	if (nwidp)
		free(nwidp, M_DEVBUF, 0);

fail:
	sc->wi_flags &= ~WI_FLAGS_BUSY;
	wakeup(&sc->wi_flags);
	splx(s);
	return(error);
}

void
wi_scan_timeout(void *arg)
{
	struct wi_softc		*sc = (struct wi_softc *)arg;
	struct wi_req		wreq;

	if (sc->wi_scan_lock++ < WI_WAVELAN_RES_TRIES &&
	    sc->sc_firmware_type != WI_LUCENT &&
	    (sc->wi_flags & WI_FLAGS_BUS_USB) == 0) {
		/*
		 * The Prism2/2.5/3 chipsets will set an extra field in the
		 * scan result if the scan request has been completed by the
		 * firmware. This allows to poll for completion and to
		 * wait for some more time if the scan is still in progress.
		 *
		 * XXX This doesn't work with wi@usb because it isn't safe
		 * to call wi_read_record_usb() while being in the timeout
		 * handler.
		 */
		wreq.wi_len = WI_MAX_DATALEN;
		wreq.wi_type = WI_RID_SCAN_RES;

		if (wi_read_record(sc, (struct wi_ltv_gen *)&wreq) == 0 &&
		    ((struct wi_scan_p2_hdr *)wreq.wi_val)->wi_reason == 0) {
			/* Wait some more time for scan completion */
			timeout_add(&sc->wi_scan_timeout, WI_WAVELAN_RES_TIMEOUT);
			return;
		}
	}

	if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
		printf(WI_PRT_FMT ": wi_scan_timeout: %d tries\n",
		    WI_PRT_ARG(sc), sc->wi_scan_lock);

	/* Wakeup the userland */
	wakeup(&sc->wi_scan_lock);	
	sc->wi_scan_lock = 0;
}

STATIC void
wi_init_io(struct wi_softc *sc)
{
	struct ifnet		*ifp = &sc->sc_ic.ic_ac.ac_if;
	int			s;
	struct wi_ltv_macaddr	mac;
	int			id = 0;

	if (!(sc->wi_flags & WI_FLAGS_ATTACHED))
		return;

	DPRINTF(WID_INIT, ("wi_init: sc %p\n", sc));

	s = splnet();

	if (ifp->if_flags & IFF_RUNNING)
		wi_stop(sc);

	wi_reset(sc);

	/* Program max data length. */
	WI_SETVAL(WI_RID_MAX_DATALEN, sc->wi_max_data_len);

	/* Set the port type. */
	WI_SETVAL(WI_RID_PORTTYPE, sc->wi_ptype);

	/* Enable/disable IBSS creation. */
	WI_SETVAL(WI_RID_CREATE_IBSS, sc->wi_create_ibss);

	/* Program the RTS/CTS threshold. */
	WI_SETVAL(WI_RID_RTS_THRESH, sc->wi_rts_thresh);

	/* Program the TX rate */
	WI_SETVAL(WI_RID_TX_RATE, sc->wi_tx_rate);

	/* Access point density */
	WI_SETVAL(WI_RID_SYSTEM_SCALE, sc->wi_ap_density);

	/* Power Management Enabled */
	WI_SETVAL(WI_RID_PM_ENABLED, sc->wi_pm_enabled);

	/* Power Management Max Sleep */
	WI_SETVAL(WI_RID_MAX_SLEEP, sc->wi_max_sleep);

	/* Set Enhanced Security if supported. */
	if (sc->wi_flags & WI_FLAGS_HAS_ENH_SECURITY)
		WI_SETVAL(WI_RID_ENH_SECURITY, sc->wi_enh_security);

	/* Set Roaming Mode unless this is a Symbol card. */
	if (sc->wi_flags & WI_FLAGS_HAS_ROAMING)
		WI_SETVAL(WI_RID_ROAMING_MODE, sc->wi_roaming);

	/* Set Antenna Diversity if this is a Symbol card. */
	if (sc->wi_flags & WI_FLAGS_HAS_DIVERSITY)
		WI_SETVAL(WI_RID_SYMBOL_DIVERSITY, sc->wi_diversity);

	/* Specify the network name */
	WI_SETSTR(WI_RID_DESIRED_SSID, sc->wi_net_name);

	/* Specify the IBSS name */
	if (sc->wi_net_name.i_len != 0 && (sc->wi_ptype == WI_PORTTYPE_HOSTAP ||
	    (sc->wi_create_ibss && sc->wi_ptype == WI_PORTTYPE_IBSS)))
		WI_SETSTR(WI_RID_OWN_SSID, sc->wi_net_name);
	else
		WI_SETSTR(WI_RID_OWN_SSID, sc->wi_ibss_name);

	/* Specify the frequency to use */
	WI_SETVAL(WI_RID_OWN_CHNL, sc->wi_channel);

	/* Program the nodename. */
	WI_SETSTR(WI_RID_NODENAME, sc->wi_node_name);

	/* Set our MAC address. */
	mac.wi_len = 4;
	mac.wi_type = WI_RID_MAC_NODE;
	bcopy(LLADDR(ifp->if_sadl), &sc->sc_ic.ic_myaddr, ETHER_ADDR_LEN);
	bcopy(&sc->sc_ic.ic_myaddr, &mac.wi_mac_addr, ETHER_ADDR_LEN);
	wi_write_record(sc, (struct wi_ltv_gen *)&mac);

	/*
	 * Initialize promisc mode.
	 *	Being in the Host-AP mode causes
	 *	great deal of pain if promisc mode is set.
	 *	Therefore we avoid confusing the firmware
	 *	and always reset promisc mode in Host-AP regime,
	 *	it shows us all the packets anyway.
	 */
	if (sc->wi_ptype != WI_PORTTYPE_HOSTAP && ifp->if_flags & IFF_PROMISC)
		WI_SETVAL(WI_RID_PROMISC, 1);
	else
		WI_SETVAL(WI_RID_PROMISC, 0);

	/* Configure WEP. */
	if (sc->wi_flags & WI_FLAGS_HAS_WEP) {
		WI_SETVAL(WI_RID_ENCRYPTION, sc->wi_use_wep);
		WI_SETVAL(WI_RID_TX_CRYPT_KEY, sc->wi_tx_key);
		sc->wi_keys.wi_len = (sizeof(struct wi_ltv_keys) / 2) + 1;
		sc->wi_keys.wi_type = WI_RID_DEFLT_CRYPT_KEYS;
		wi_write_record(sc, (struct wi_ltv_gen *)&sc->wi_keys);
		if (sc->sc_firmware_type != WI_LUCENT && sc->wi_use_wep) {
			/*
			 * HWB3163 EVAL-CARD Firmware version less than 0.8.2.
			 *
			 * If promiscuous mode is disabled, the Prism2 chip
			 * does not work with WEP .
			 * I'm currently investigating the details of this.
			 * (ichiro@netbsd.org)
			 */
			 if (sc->sc_firmware_type == WI_INTERSIL &&
			    sc->sc_sta_firmware_ver < 802 ) {
				/* firm ver < 0.8.2 */
				WI_SETVAL(WI_RID_PROMISC, 1);
			 }
			 WI_SETVAL(WI_RID_CNFAUTHMODE, sc->wi_authtype);
		}
	}

	/* Set multicast filter. */
	wi_setmulti(sc);

	/* Enable desired port */
	wi_cmd(sc, WI_CMD_ENABLE | sc->wi_portnum, 0, 0, 0);

	if (wi_alloc_nicmem(sc, ETHER_MAX_LEN + sizeof(struct wi_frame) + 8, &id))
		printf(WI_PRT_FMT ": tx buffer allocation failed\n",
		    WI_PRT_ARG(sc));
	sc->wi_tx_data_id = id;

	if (wi_alloc_nicmem(sc, ETHER_MAX_LEN + sizeof(struct wi_frame) + 8, &id))
		printf(WI_PRT_FMT ": mgmt. buffer allocation failed\n",
		    WI_PRT_ARG(sc));
	sc->wi_tx_mgmt_id = id;

	/* Set txpower */
	if (sc->wi_flags & WI_FLAGS_TXPOWER)
		wi_set_txpower(sc, NULL);

	/* enable interrupts */
	wi_intr_enable(sc, WI_INTRS);

        wihap_init(sc);

	splx(s);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_add_sec(&sc->sc_timo, 60);

	return;
}

STATIC void
wi_do_hostencrypt(struct wi_softc *sc, caddr_t buf, int len)
{
	u_int32_t crc, klen;
	u_int8_t key[RC4KEYLEN];
	u_int8_t *dat;
	struct rc4_ctx ctx;

	if (!sc->wi_icv_flag) {
		sc->wi_icv = arc4random();
		sc->wi_icv_flag++;
        } else
		sc->wi_icv++;
	/*
	 * Skip 'bad' IVs from Fluhrer/Mantin/Shamir:
	 * (B, 255, N) with 3 <= B < 8
	 */
	if (sc->wi_icv >= 0x03ff00 &&
            (sc->wi_icv & 0xf8ff00) == 0x00ff00)
                sc->wi_icv += 0x000100;

	/* prepend 24bit IV to tx key, byte order does not matter */
	bzero(key, sizeof(key));
	key[0] = sc->wi_icv >> 16;
	key[1] = sc->wi_icv >> 8;
	key[2] = sc->wi_icv;

	klen = letoh16(sc->wi_keys.wi_keys[sc->wi_tx_key].wi_keylen);
	bcopy(&sc->wi_keys.wi_keys[sc->wi_tx_key].wi_keydat,
	    key + IEEE80211_WEP_IVLEN, klen);
	klen = (klen > IEEE80211_WEP_KEYLEN) ? RC4KEYLEN : RC4KEYLEN / 2;

	/* rc4 keysetup */
	rc4_keysetup(&ctx, key, klen);

	/* output: IV, tx keyid, rc4(data), rc4(crc32(data)) */
	dat = buf;
	dat[0] = key[0];
	dat[1] = key[1];
	dat[2] = key[2];
	dat[3] = sc->wi_tx_key << 6;		/* pad and keyid */
	dat += 4;

	/* compute crc32 over data and encrypt */
	crc = ~ether_crc32_le(dat, len);
	rc4_crypt(&ctx, dat, dat, len);
	dat += len;

	/* append little-endian crc32 and encrypt */
	dat[0] = crc;
	dat[1] = crc >> 8;
	dat[2] = crc >> 16;
	dat[3] = crc >> 24;
	rc4_crypt(&ctx, dat, dat, IEEE80211_WEP_CRCLEN);
}

STATIC int
wi_do_hostdecrypt(struct wi_softc *sc, caddr_t buf, int len)
{
	u_int32_t crc, klen, kid;
	u_int8_t key[RC4KEYLEN];
	u_int8_t *dat;
	struct rc4_ctx ctx;

	if (len < IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN +
	    IEEE80211_WEP_CRCLEN)
		return -1;
	len -= (IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN +
	    IEEE80211_WEP_CRCLEN);

	dat = buf;

	bzero(key, sizeof(key));
	key[0] = dat[0];
	key[1] = dat[1];
	key[2] = dat[2];
	kid = (dat[3] >> 6) % 4;
	dat += 4;

	klen = letoh16(sc->wi_keys.wi_keys[kid].wi_keylen);
	bcopy(&sc->wi_keys.wi_keys[kid].wi_keydat,
	    key + IEEE80211_WEP_IVLEN, klen);
	klen = (klen > IEEE80211_WEP_KEYLEN) ? RC4KEYLEN : RC4KEYLEN / 2;

	/* rc4 keysetup */
	rc4_keysetup(&ctx, key, klen);

	/* decrypt and compute crc32 over data */
	rc4_crypt(&ctx, dat, dat, len);
	crc = ~ether_crc32_le(dat, len);
	dat += len;

	/* decrypt little-endian crc32 and verify */
	rc4_crypt(&ctx, dat, dat, IEEE80211_WEP_CRCLEN);

	if ((dat[0] != crc) && (dat[1] != crc >> 8) &&
	    (dat[2] != crc >> 16) && (dat[3] != crc >> 24)) {
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf(WI_PRT_FMT ": wi_do_hostdecrypt: iv mismatch: "
			    "0x%02x%02x%02x%02x vs. 0x%x\n", WI_PRT_ARG(sc),
			    dat[3], dat[2], dat[1], dat[0], crc);
		return -1;
	}

	return 0;
}

void
wi_start(struct ifnet *ifp)
{
	struct wi_softc		*sc;
	struct mbuf		*m0;
	struct wi_frame		tx_frame;
	struct ether_header	*eh;
	int			id, hostencrypt = 0;

	sc = ifp->if_softc;

	DPRINTF(WID_START, ("wi_start: ifp %p sc %p\n", ifp, sc));

	if (!(sc->wi_flags & WI_FLAGS_ATTACHED))
		return;

	if (ifq_is_oactive(&ifp->if_snd))
		return;

nextpkt:
	m0 = ifq_dequeue(&ifp->if_snd);
	if (m0 == NULL)
		return;

	bzero(&tx_frame, sizeof(tx_frame));
	tx_frame.wi_frame_ctl = htole16(WI_FTYPE_DATA | WI_STYPE_DATA);
	id = sc->wi_tx_data_id;
	eh = mtod(m0, struct ether_header *);

	if (sc->wi_ptype == WI_PORTTYPE_HOSTAP) {
		if (!wihap_check_tx(&sc->wi_hostap_info, eh->ether_dhost,
		    &tx_frame.wi_tx_rate) && !(ifp->if_flags & IFF_PROMISC)) {
			if (ifp->if_flags & IFF_DEBUG)
				printf(WI_PRT_FMT
				    ": wi_start: dropping unassoc dst %s\n",
				    WI_PRT_ARG(sc),
				    ether_sprintf(eh->ether_dhost));
			m_freem(m0);
			goto nextpkt;
		}
	}

	/*
	 * Use RFC1042 encoding for IP and ARP datagrams,
	 * 802.3 for anything else.
	 */
	if (eh->ether_type == htons(ETHERTYPE_IP) ||
	    eh->ether_type == htons(ETHERTYPE_ARP) ||
	    eh->ether_type == htons(ETHERTYPE_REVARP) ||
	    eh->ether_type == htons(ETHERTYPE_IPV6)) {
		bcopy(&eh->ether_dhost,
		    &tx_frame.wi_addr1, ETHER_ADDR_LEN);
		if (sc->wi_ptype == WI_PORTTYPE_HOSTAP) {
			tx_frame.wi_tx_ctl = htole16(WI_ENC_TX_MGMT); /* XXX */
			tx_frame.wi_frame_ctl |= htole16(WI_FCTL_FROMDS);
			bcopy(&sc->sc_ic.ic_myaddr,
			    &tx_frame.wi_addr2, ETHER_ADDR_LEN);
			bcopy(&eh->ether_shost,
			    &tx_frame.wi_addr3, ETHER_ADDR_LEN);
			if (sc->wi_use_wep)
				hostencrypt = 1;
		} else if (sc->wi_ptype == WI_PORTTYPE_BSS && sc->wi_use_wep &&
		    sc->wi_crypto_algorithm != WI_CRYPTO_FIRMWARE_WEP) {
			tx_frame.wi_tx_ctl = htole16(WI_ENC_TX_MGMT); /* XXX */
			tx_frame.wi_frame_ctl |= htole16(WI_FCTL_TODS);
			bcopy(&sc->sc_ic.ic_myaddr,
			    &tx_frame.wi_addr2, ETHER_ADDR_LEN);
			bcopy(&eh->ether_dhost,
			    &tx_frame.wi_addr3, ETHER_ADDR_LEN);
			hostencrypt = 1;
		} else
			bcopy(&eh->ether_shost,
			    &tx_frame.wi_addr2, ETHER_ADDR_LEN);
		bcopy(&eh->ether_dhost, &tx_frame.wi_dst_addr, ETHER_ADDR_LEN);
		bcopy(&eh->ether_shost, &tx_frame.wi_src_addr, ETHER_ADDR_LEN);

		tx_frame.wi_dat_len = m0->m_pkthdr.len - WI_SNAPHDR_LEN;
		tx_frame.wi_dat[0] = htons(WI_SNAP_WORD0);
		tx_frame.wi_dat[1] = htons(WI_SNAP_WORD1);
		tx_frame.wi_len = htons(m0->m_pkthdr.len - WI_SNAPHDR_LEN);
		tx_frame.wi_type = eh->ether_type;

		if (hostencrypt) {

			/* Do host encryption. */
			tx_frame.wi_frame_ctl |= htole16(WI_FCTL_WEP);
			bcopy(&tx_frame.wi_dat[0], &sc->wi_txbuf[4], 6);
			bcopy(&tx_frame.wi_type, &sc->wi_txbuf[10], 2);

			m_copydata(m0, sizeof(struct ether_header),
			    m0->m_pkthdr.len - sizeof(struct ether_header),
			    &sc->wi_txbuf[12]);

			wi_do_hostencrypt(sc, (caddr_t)&sc->wi_txbuf,
			    tx_frame.wi_dat_len);

			tx_frame.wi_dat_len += IEEE80211_WEP_IVLEN +
			    IEEE80211_WEP_KIDLEN + IEEE80211_WEP_CRCLEN;

			tx_frame.wi_dat_len = htole16(tx_frame.wi_dat_len);
			wi_write_data(sc, id, 0, (caddr_t)&tx_frame,
			    sizeof(struct wi_frame));
			wi_write_data(sc, id, WI_802_11_OFFSET_RAW,
			    (caddr_t)&sc->wi_txbuf,
			    (m0->m_pkthdr.len -
			     sizeof(struct ether_header)) + 18);
		} else {
			m_copydata(m0, sizeof(struct ether_header),
			    m0->m_pkthdr.len - sizeof(struct ether_header),
			    &sc->wi_txbuf);

			tx_frame.wi_dat_len = htole16(tx_frame.wi_dat_len);
			wi_write_data(sc, id, 0, (caddr_t)&tx_frame,
			    sizeof(struct wi_frame));
			wi_write_data(sc, id, WI_802_11_OFFSET,
			    (caddr_t)&sc->wi_txbuf,
			    (m0->m_pkthdr.len -
			     sizeof(struct ether_header)) + 2);
		}
	} else {
		tx_frame.wi_dat_len = htole16(m0->m_pkthdr.len);

		if (sc->wi_ptype == WI_PORTTYPE_HOSTAP && sc->wi_use_wep) {

			/* Do host encryption. (XXX - not implemented) */
			printf(WI_PRT_FMT
			    ": host encrypt not implemented for 802.3\n",
			    WI_PRT_ARG(sc));
		} else {
			m_copydata(m0, 0, m0->m_pkthdr.len, &sc->wi_txbuf);

			wi_write_data(sc, id, 0, (caddr_t)&tx_frame,
			    sizeof(struct wi_frame));
			wi_write_data(sc, id, WI_802_3_OFFSET,
			    (caddr_t)&sc->wi_txbuf, m0->m_pkthdr.len + 2);
		}
	}

#if NBPFILTER > 0
	/*
	 * If there's a BPF listener, bounce a copy of
	 * this frame to him.
	 */
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif

	m_freem(m0);

	ifq_set_oactive(&ifp->if_snd);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;

	if (wi_cmd(sc, WI_CMD_TX|WI_RECLAIM, id, 0, 0))
		printf(WI_PRT_FMT ": wi_start: xmit failed\n", WI_PRT_ARG(sc));

	return;
}

STATIC int
wi_mgmt_xmit(struct wi_softc *sc, caddr_t data, int len)
{
	struct wi_frame		tx_frame;
	int			id;
	struct wi_80211_hdr	*hdr;
	caddr_t			dptr;

	if (!(sc->wi_flags & WI_FLAGS_ATTACHED))
		return(ENODEV);

	hdr = (struct wi_80211_hdr *)data;
	dptr = data + sizeof(struct wi_80211_hdr);

	bzero(&tx_frame, sizeof(tx_frame));
	id = sc->wi_tx_mgmt_id;

	bcopy(hdr, &tx_frame.wi_frame_ctl, sizeof(struct wi_80211_hdr));

	tx_frame.wi_tx_ctl = htole16(WI_ENC_TX_MGMT);
	tx_frame.wi_dat_len = len - sizeof(struct wi_80211_hdr);
	tx_frame.wi_len = htole16(tx_frame.wi_dat_len);

	tx_frame.wi_dat_len = htole16(tx_frame.wi_dat_len);
	wi_write_data(sc, id, 0, (caddr_t)&tx_frame, sizeof(struct wi_frame));
	wi_write_data(sc, id, WI_802_11_OFFSET_RAW, dptr,
	    (len - sizeof(struct wi_80211_hdr)) + 2);

	if (wi_cmd(sc, WI_CMD_TX|WI_RECLAIM, id, 0, 0)) {
		printf(WI_PRT_FMT ": wi_mgmt_xmit: xmit failed\n",
		    WI_PRT_ARG(sc));
		/*
		 * Hostile stations or corrupt frames may crash the card
		 * and cause the kernel to get stuck printing complaints.
		 * Reset the card and hope the problem goes away.
		 */
		wi_reset(sc);
		return(EIO);
	}

	return(0);
}

void
wi_stop(struct wi_softc *sc)
{
	struct ifnet		*ifp;

	wihap_shutdown(sc);

	if (!(sc->wi_flags & WI_FLAGS_ATTACHED))
		return;

	DPRINTF(WID_STOP, ("wi_stop: sc %p\n", sc));

	timeout_del(&sc->sc_timo);

	ifp = &sc->sc_ic.ic_if;

	wi_intr_enable(sc, 0);
	wi_cmd(sc, WI_CMD_DISABLE|sc->wi_portnum, 0, 0, 0);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	return;
}


void
wi_watchdog(struct ifnet *ifp)
{
	struct wi_softc		*sc;

	sc = ifp->if_softc;

	printf(WI_PRT_FMT ": device timeout\n", WI_PRT_ARG(sc));

	wi_cor_reset(sc);
	wi_init(sc);

	ifp->if_oerrors++;

	return;
}

void
wi_detach(struct wi_softc *sc)
{
	struct ifnet *ifp;
	ifp = &sc->sc_ic.ic_if;

	if (ifp->if_flags & IFF_RUNNING)
		wi_stop(sc);
	
	if (sc->wi_flags & WI_FLAGS_ATTACHED) {
		sc->wi_flags &= ~WI_FLAGS_ATTACHED;
	}
}

STATIC void
wi_get_id(struct wi_softc *sc)
{
	struct wi_ltv_ver		ver;
	const struct wi_card_ident	*id;
	u_int16_t			pri_fw_ver[3];
	const char			*card_name;
	u_int16_t			card_id;

	/* get chip identity */
	bzero(&ver, sizeof(ver));
	ver.wi_type = WI_RID_CARD_ID;
	ver.wi_len = 5;
	wi_read_record(sc, (struct wi_ltv_gen *)&ver);
	card_id = letoh16(ver.wi_ver[0]);
	for (id = wi_card_ident; id->firm_type != WI_NOTYPE; id++) {
		if (card_id == id->card_id)
			break;
	}
	if (id->firm_type != WI_NOTYPE) {
		sc->sc_firmware_type = id->firm_type;
		card_name = id->card_name;
	} else if (ver.wi_ver[0] & htole16(0x8000)) {
		sc->sc_firmware_type = WI_INTERSIL;
		card_name = "Unknown PRISM2 chip";
	} else {
		sc->sc_firmware_type = WI_LUCENT;
	}

	/* get primary firmware version (XXX - how to do Lucent?) */
	if (sc->sc_firmware_type != WI_LUCENT) {
		bzero(&ver, sizeof(ver));
		ver.wi_type = WI_RID_PRI_IDENTITY;
		ver.wi_len = 5;
		wi_read_record(sc, (struct wi_ltv_gen *)&ver);
		pri_fw_ver[0] = letoh16(ver.wi_ver[2]);
		pri_fw_ver[1] = letoh16(ver.wi_ver[3]);
		pri_fw_ver[2] = letoh16(ver.wi_ver[1]);
	}

	/* get station firmware version */
	bzero(&ver, sizeof(ver));
	ver.wi_type = WI_RID_STA_IDENTITY;
	ver.wi_len = 5;
	wi_read_record(sc, (struct wi_ltv_gen *)&ver);
	ver.wi_ver[1] = letoh16(ver.wi_ver[1]);
	ver.wi_ver[2] = letoh16(ver.wi_ver[2]);
	ver.wi_ver[3] = letoh16(ver.wi_ver[3]);
	sc->sc_sta_firmware_ver = ver.wi_ver[2] * 10000 +
	    ver.wi_ver[3] * 100 + ver.wi_ver[1];

	if (sc->sc_firmware_type == WI_INTERSIL &&
	    (sc->sc_sta_firmware_ver == 10102 || sc->sc_sta_firmware_ver == 20102)) {
		struct wi_ltv_str sver;
		char *p;

		bzero(&sver, sizeof(sver));
		sver.wi_type = WI_RID_SYMBOL_IDENTITY;
		sver.wi_len = 7;
		/* value should be something like "V2.00-11" */
		if (wi_read_record(sc, (struct wi_ltv_gen *)&sver) == 0 &&
		    *(p = (char *)sver.wi_str) >= 'A' &&
		    p[2] == '.' && p[5] == '-' && p[8] == '\0') {
			sc->sc_firmware_type = WI_SYMBOL;
			sc->sc_sta_firmware_ver = (p[1] - '0') * 10000 +
			    (p[3] - '0') * 1000 + (p[4] - '0') * 100 +
			    (p[6] - '0') * 10 + (p[7] - '0');
		}
	}

	if (sc->sc_firmware_type == WI_LUCENT) {
		printf("%s: Firmware %d.%02d variant %d, ", WI_PRT_ARG(sc),
		    ver.wi_ver[2], ver.wi_ver[3], ver.wi_ver[1]);
	} else {
		printf("%s: %s%s (0x%04x), Firmware %d.%d.%d (primary), %d.%d.%d (station), ",
		    WI_PRT_ARG(sc),
		    sc->sc_firmware_type == WI_SYMBOL ? "Symbol " : "",
		    card_name, card_id, pri_fw_ver[0], pri_fw_ver[1],
		    pri_fw_ver[2], sc->sc_sta_firmware_ver / 10000,
		    (sc->sc_sta_firmware_ver % 10000) / 100,
		    sc->sc_sta_firmware_ver % 100);
	}
}

STATIC int
wi_sync_media(struct wi_softc *sc, int ptype, int txrate)
{
	uint64_t media = sc->sc_media.ifm_cur->ifm_media;
	uint64_t options = IFM_OPTIONS(media);
	uint64_t subtype;

	switch (txrate) {
	case 1:
		subtype = IFM_IEEE80211_DS1;
		break;
	case 2:
		subtype = IFM_IEEE80211_DS2;
		break;
	case 3:
		subtype = IFM_AUTO;
		break;
	case 5:
		subtype = IFM_IEEE80211_DS5;
		break;
	case 11:
		subtype = IFM_IEEE80211_DS11;
		break;
	default:
		subtype = IFM_MANUAL;		/* Unable to represent */
		break;
	}

	options &= ~IFM_OMASK;
	switch (ptype) {
	case WI_PORTTYPE_BSS:
		/* default port type */
		break;
	case WI_PORTTYPE_ADHOC:
		options |= IFM_IEEE80211_ADHOC;
		break;
	case WI_PORTTYPE_HOSTAP:
		options |= IFM_IEEE80211_HOSTAP;
		break;
	case WI_PORTTYPE_IBSS:
		if (sc->wi_create_ibss)
			options |= IFM_IEEE80211_IBSSMASTER;
		else
			options |= IFM_IEEE80211_IBSS;
		break;
	default:
		subtype = IFM_MANUAL;		/* Unable to represent */
		break;
	}
	media = IFM_MAKEWORD(IFM_TYPE(media), subtype, options,
	IFM_INST(media));
	if (!ifmedia_match(&sc->sc_media, media, sc->sc_media.ifm_mask))
		return (EINVAL);
	ifmedia_set(&sc->sc_media, media);
	sc->wi_ptype = ptype;
	sc->wi_tx_rate = txrate;
	return (0);
}

STATIC int
wi_media_change(struct ifnet *ifp)
{
	struct wi_softc *sc = ifp->if_softc;
	int otype = sc->wi_ptype;
	int orate = sc->wi_tx_rate;
	int ocreate_ibss = sc->wi_create_ibss;

	if ((sc->sc_media.ifm_cur->ifm_media & IFM_IEEE80211_HOSTAP) &&
	    sc->sc_firmware_type != WI_INTERSIL)
		return (EINVAL);

	sc->wi_create_ibss = 0;

	switch (sc->sc_media.ifm_cur->ifm_media & IFM_OMASK) {
	case 0:
		sc->wi_ptype = WI_PORTTYPE_BSS;
		break;
	case IFM_IEEE80211_ADHOC:
		sc->wi_ptype = WI_PORTTYPE_ADHOC;
		break;
	case IFM_IEEE80211_HOSTAP:
		sc->wi_ptype = WI_PORTTYPE_HOSTAP;
		break;
	case IFM_IEEE80211_IBSSMASTER:
	case IFM_IEEE80211_IBSSMASTER|IFM_IEEE80211_IBSS:
		if (!(sc->wi_flags & WI_FLAGS_HAS_CREATE_IBSS))
			return (EINVAL);
		sc->wi_create_ibss = 1;
		/* FALLTHROUGH */
	case IFM_IEEE80211_IBSS:
		sc->wi_ptype = WI_PORTTYPE_IBSS;
		break;
	default:
		/* Invalid combination. */
		return (EINVAL);
	}

	switch (IFM_SUBTYPE(sc->sc_media.ifm_cur->ifm_media)) {
	case IFM_IEEE80211_DS1:
		sc->wi_tx_rate = 1;
		break;
	case IFM_IEEE80211_DS2:
		sc->wi_tx_rate = 2;
		break;
	case IFM_AUTO:
		sc->wi_tx_rate = 3;
		break;
	case IFM_IEEE80211_DS5:
		sc->wi_tx_rate = 5;
		break;
	case IFM_IEEE80211_DS11:
		sc->wi_tx_rate = 11;
		break;
	}

	if (sc->sc_ic.ic_if.if_flags & IFF_UP) {
		if (otype != sc->wi_ptype || orate != sc->wi_tx_rate ||
		    ocreate_ibss != sc->wi_create_ibss)
			wi_init(sc);
	}

	ifp->if_baudrate = ifmedia_baudrate(sc->sc_media.ifm_cur->ifm_media);

	return (0);
}

STATIC void
wi_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct wi_softc *sc = ifp->if_softc;
	struct wi_req wreq;

	if (!(sc->sc_ic.ic_if.if_flags & IFF_UP)) {
		imr->ifm_active = IFM_IEEE80211|IFM_NONE;
		imr->ifm_status = 0;
		return;
	}

	if (sc->wi_tx_rate == 3) {
		imr->ifm_active = IFM_IEEE80211|IFM_AUTO;

		wreq.wi_type = WI_RID_CUR_TX_RATE;
		wreq.wi_len = WI_MAX_DATALEN;
		if (wi_read_record(sc, (struct wi_ltv_gen *)&wreq) == 0) {
			switch (letoh16(wreq.wi_val[0])) {
			case 1:
				imr->ifm_active |= IFM_IEEE80211_DS1;
				break;
			case 2:
				imr->ifm_active |= IFM_IEEE80211_DS2;
				break;
			case 6:
				imr->ifm_active |= IFM_IEEE80211_DS5;
				break;
			case 11:
				imr->ifm_active |= IFM_IEEE80211_DS11;
				break;
			}
		}
	} else {
		imr->ifm_active = sc->sc_media.ifm_cur->ifm_media;
	}

	imr->ifm_status = IFM_AVALID;
	switch (sc->wi_ptype) {
	case WI_PORTTYPE_ADHOC:
	case WI_PORTTYPE_IBSS:
		/*
		 * XXX: It would be nice if we could give some actually
		 * useful status like whether we joined another IBSS or
		 * created one ourselves.
		 */
		/* FALLTHROUGH */
	case WI_PORTTYPE_HOSTAP:
		imr->ifm_status |= IFM_ACTIVE;
		break;
	default:
		wreq.wi_type = WI_RID_COMMQUAL;
		wreq.wi_len = WI_MAX_DATALEN;
		if (wi_read_record(sc, (struct wi_ltv_gen *)&wreq) == 0 &&
		    letoh16(wreq.wi_val[0]) != 0)
			imr->ifm_status |= IFM_ACTIVE;
	}
}

STATIC int
wi_set_nwkey(struct wi_softc *sc, struct ieee80211_nwkey *nwkey)
{
	int i, len, error;
	struct wi_req wreq;
	struct wi_ltv_keys *wk = (struct wi_ltv_keys *)&wreq;

	if (!(sc->wi_flags & WI_FLAGS_HAS_WEP))
		return ENODEV;
	if (nwkey->i_defkid <= 0 || nwkey->i_defkid > IEEE80211_WEP_NKID)
		return EINVAL;
	memcpy(wk, &sc->wi_keys, sizeof(*wk));
	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		if (nwkey->i_key[i].i_keydat == NULL)
			continue;
		len = nwkey->i_key[i].i_keylen;
		if (len > sizeof(wk->wi_keys[i].wi_keydat))
			return EINVAL;
		error = copyin(nwkey->i_key[i].i_keydat,
		    wk->wi_keys[i].wi_keydat, len);
		if (error)
			return error;
		wk->wi_keys[i].wi_keylen = htole16(len);
	}

	wk->wi_len = (sizeof(*wk) / 2) + 1;
	wk->wi_type = WI_RID_DEFLT_CRYPT_KEYS;
	if (sc->sc_ic.ic_if.if_flags & IFF_UP) {
		error = wi_write_record(sc, (struct wi_ltv_gen *)&wreq);
		if (error)
			return error;
	}
	if ((error = wi_setdef(sc, &wreq)))
		return (error);

	wreq.wi_len = 2;
	wreq.wi_type = WI_RID_TX_CRYPT_KEY;
	wreq.wi_val[0] = htole16(nwkey->i_defkid - 1);
	if (sc->sc_ic.ic_if.if_flags & IFF_UP) {
		error = wi_write_record(sc, (struct wi_ltv_gen *)&wreq);
		if (error)
			return error;
	}
	if ((error = wi_setdef(sc, &wreq)))
		return (error);

	wreq.wi_type = WI_RID_ENCRYPTION;
	wreq.wi_val[0] = htole16(nwkey->i_wepon);
	if (sc->sc_ic.ic_if.if_flags & IFF_UP) {
		error = wi_write_record(sc, (struct wi_ltv_gen *)&wreq);
		if (error)
			return error;
	}
	if ((error = wi_setdef(sc, &wreq)))
		return (error);

	if (sc->sc_ic.ic_if.if_flags & IFF_UP)
		wi_init(sc);
	return 0;
}

STATIC int
wi_get_nwkey(struct wi_softc *sc, struct ieee80211_nwkey *nwkey)
{
	int i;

	if (!(sc->wi_flags & WI_FLAGS_HAS_WEP))
		return ENODEV;
	nwkey->i_wepon = sc->wi_use_wep;
	nwkey->i_defkid = sc->wi_tx_key + 1;

	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		if (nwkey->i_key[i].i_keydat == NULL)
			continue;
		/* do not show any keys to userland */
		return EPERM;
	}
	return 0;
}

STATIC int
wi_set_pm(struct wi_softc *sc, struct ieee80211_power *power)
{

	sc->wi_pm_enabled = power->i_enabled;
	sc->wi_max_sleep = power->i_maxsleep;

	if (sc->sc_ic.ic_if.if_flags & IFF_UP)
		wi_init(sc);

	return (0);
}

STATIC int
wi_get_pm(struct wi_softc *sc, struct ieee80211_power *power)
{

	power->i_enabled = sc->wi_pm_enabled;
	power->i_maxsleep = sc->wi_max_sleep;

	return (0);
}

STATIC int
wi_set_txpower(struct wi_softc *sc, struct ieee80211_txpower *txpower)
{
	u_int16_t	cmd;
	u_int16_t	power;
	int8_t		tmp;
	int		error;
	int		alc;

	if (txpower == NULL) {
		if (!(sc->wi_flags & WI_FLAGS_TXPOWER))
			return (EINVAL);
		alc = 0;		/* disable ALC */
	} else {
		if (txpower->i_mode == IEEE80211_TXPOWER_MODE_AUTO) {
			alc = 1;	/* enable ALC */
			sc->wi_flags &= ~WI_FLAGS_TXPOWER;
		} else {
			alc = 0;	/* disable ALC */
			sc->wi_flags |= WI_FLAGS_TXPOWER;
			sc->wi_txpower = txpower->i_val;
		}
	}	

	/* Set ALC */
	cmd = WI_CMD_DEBUG | (WI_DEBUG_CONFBITS << 8);
	if ((error = wi_cmd(sc, cmd, alc, 0x8, 0)) != 0)
		return (error);

	/* No need to set the TX power value if ALC is enabled */
	if (alc)
		return (0);

	/* Convert dBM to internal TX power value */
	if (sc->wi_txpower > 20)
		power = 128;
	else if (sc->wi_txpower < -43)
		power = 127;
	else {
		tmp = sc->wi_txpower;
		tmp = -12 - tmp;
		tmp <<= 2;

		power = (u_int16_t)tmp;
	}

	/* Set manual TX power */
	cmd = WI_CMD_WRITE_MIF;
	if ((error = wi_cmd(sc, cmd,
		 WI_HFA384X_CR_MANUAL_TX_POWER, power, 0)) != 0)
		return (error);

	if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
		printf("%s: %u (%d dBm)\n", sc->sc_dev.dv_xname, power,
		    sc->wi_txpower);

	return (0);
}

STATIC int
wi_get_txpower(struct wi_softc *sc, struct ieee80211_txpower *txpower)
{
	u_int16_t	cmd;
	u_int16_t	power;
	int8_t		tmp;
	int		error;

	if (sc->wi_flags & WI_FLAGS_BUS_USB)
		return (EOPNOTSUPP);

	/* Get manual TX power */
	cmd = WI_CMD_READ_MIF;
	if ((error = wi_cmd(sc, cmd,
		 WI_HFA384X_CR_MANUAL_TX_POWER, 0, 0)) != 0)
		return (error);

	power = CSR_READ_2(sc, WI_RESP0);

	/* Convert internal TX power value to dBM */
	if (power > 255)
		txpower->i_val = 255;
	else {
		tmp = power;
		tmp >>= 2;
		txpower->i_val = (u_int16_t)(-12 - tmp);
	}

	if (sc->wi_flags & WI_FLAGS_TXPOWER)
		txpower->i_mode = IEEE80211_TXPOWER_MODE_FIXED;
	else
		txpower->i_mode = IEEE80211_TXPOWER_MODE_AUTO;
	
	return (0);
}

STATIC int
wi_set_ssid(struct ieee80211_nwid *ws, u_int8_t *id, int len)
{

	if (len > IEEE80211_NWID_LEN)
		return (EINVAL);
	ws->i_len = len;
	memcpy(ws->i_nwid, id, len);
	return (0);
}

STATIC int
wi_get_debug(struct wi_softc *sc, struct wi_req *wreq)
{
	int			error = 0;

	wreq->wi_len = 1;

	switch (wreq->wi_type) {
	case WI_DEBUG_SLEEP:
		wreq->wi_len++;
		wreq->wi_val[0] = htole16(sc->wi_debug.wi_sleep);
		break;
	case WI_DEBUG_DELAYSUPP:
		wreq->wi_len++;
		wreq->wi_val[0] = htole16(sc->wi_debug.wi_delaysupp);
		break;
	case WI_DEBUG_TXSUPP:
		wreq->wi_len++;
		wreq->wi_val[0] = htole16(sc->wi_debug.wi_txsupp);
		break;
	case WI_DEBUG_MONITOR:
		wreq->wi_len++;
		wreq->wi_val[0] = htole16(sc->wi_debug.wi_monitor);
		break;
	case WI_DEBUG_LEDTEST:
		wreq->wi_len += 3;
		wreq->wi_val[0] = htole16(sc->wi_debug.wi_ledtest);
		wreq->wi_val[1] = htole16(sc->wi_debug.wi_ledtest_param0);
		wreq->wi_val[2] = htole16(sc->wi_debug.wi_ledtest_param1);
		break;
	case WI_DEBUG_CONTTX:
		wreq->wi_len += 2;
		wreq->wi_val[0] = htole16(sc->wi_debug.wi_conttx);
		wreq->wi_val[1] = htole16(sc->wi_debug.wi_conttx_param0);
		break;
	case WI_DEBUG_CONTRX:
		wreq->wi_len++;
		wreq->wi_val[0] = htole16(sc->wi_debug.wi_contrx);
		break;
	case WI_DEBUG_SIGSTATE:
		wreq->wi_len += 2;
		wreq->wi_val[0] = htole16(sc->wi_debug.wi_sigstate);
		wreq->wi_val[1] = htole16(sc->wi_debug.wi_sigstate_param0);
		break;
	case WI_DEBUG_CONFBITS:
		wreq->wi_len += 2;
		wreq->wi_val[0] = htole16(sc->wi_debug.wi_confbits);
		wreq->wi_val[1] = htole16(sc->wi_debug.wi_confbits_param0);
		break;
	default:
		error = EIO;
		break;
	}

	return (error);
}

STATIC int
wi_set_debug(struct wi_softc *sc, struct wi_req *wreq)
{
	int				error = 0;
	u_int16_t			cmd, param0 = 0, param1 = 0;

	switch (wreq->wi_type) {
	case WI_DEBUG_RESET:
	case WI_DEBUG_INIT:
	case WI_DEBUG_CALENABLE:
		break;
	case WI_DEBUG_SLEEP:
		sc->wi_debug.wi_sleep = 1;
		break;
	case WI_DEBUG_WAKE:
		sc->wi_debug.wi_sleep = 0;
		break;
	case WI_DEBUG_CHAN:
		param0 = letoh16(wreq->wi_val[0]);
		break;
	case WI_DEBUG_DELAYSUPP:
		sc->wi_debug.wi_delaysupp = 1;
		break;
	case WI_DEBUG_TXSUPP:
		sc->wi_debug.wi_txsupp = 1;
		break;
	case WI_DEBUG_MONITOR:
		sc->wi_debug.wi_monitor = 1;
		break;
	case WI_DEBUG_LEDTEST:
		param0 = letoh16(wreq->wi_val[0]);
		param1 = letoh16(wreq->wi_val[1]);
		sc->wi_debug.wi_ledtest = 1;
		sc->wi_debug.wi_ledtest_param0 = param0;
		sc->wi_debug.wi_ledtest_param1 = param1;
		break;
	case WI_DEBUG_CONTTX:
		param0 = letoh16(wreq->wi_val[0]);
		sc->wi_debug.wi_conttx = 1;
		sc->wi_debug.wi_conttx_param0 = param0;
		break;
	case WI_DEBUG_STOPTEST:
		sc->wi_debug.wi_delaysupp = 0;
		sc->wi_debug.wi_txsupp = 0;
		sc->wi_debug.wi_monitor = 0;
		sc->wi_debug.wi_ledtest = 0;
		sc->wi_debug.wi_ledtest_param0 = 0;
		sc->wi_debug.wi_ledtest_param1 = 0;
		sc->wi_debug.wi_conttx = 0;
		sc->wi_debug.wi_conttx_param0 = 0;
		sc->wi_debug.wi_contrx = 0;
		sc->wi_debug.wi_sigstate = 0;
		sc->wi_debug.wi_sigstate_param0 = 0;
		break;
	case WI_DEBUG_CONTRX:
		sc->wi_debug.wi_contrx = 1;
		break;
	case WI_DEBUG_SIGSTATE:
		param0 = letoh16(wreq->wi_val[0]);
		sc->wi_debug.wi_sigstate = 1;
		sc->wi_debug.wi_sigstate_param0 = param0;
		break;
	case WI_DEBUG_CONFBITS:
		param0 = letoh16(wreq->wi_val[0]);
		param1 = letoh16(wreq->wi_val[1]);
		sc->wi_debug.wi_confbits = param0;
		sc->wi_debug.wi_confbits_param0 = param1;
		break;
	default:
		error = EIO;
		break;
	}

	if (error)
		return (error);

	cmd = WI_CMD_DEBUG | (wreq->wi_type << 8);
	error = wi_cmd(sc, cmd, param0, param1, 0);

	return (error);
}
