/*      $OpenBSD: ath.c,v 1.126 2025/08/01 20:39:26 stsp Exp $  */
/*	$NetBSD: ath.c,v 1.37 2004/08/18 21:59:39 dyoung Exp $	*/

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
 */

/*
 * Driver for the Atheros Wireless LAN controller.
 *
 * This software is derived from work of Atsushi Onoe; his contribution
 * is greatly appreciated. It has been modified for OpenBSD to use an
 * open source HAL instead of the original binary-only HAL. 
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/timeout.h>
#include <sys/gpio.h>
#include <sys/endian.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#if NBPFILTER > 0
#include <net/bpf.h>
#endif
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_rssadapt.h>

#include <dev/pci/pcidevs.h>
#include <dev/gpio/gpiovar.h>

#include <dev/ic/athvar.h>

int	ath_init(struct ifnet *);
int	ath_init1(struct ath_softc *);
int	ath_intr1(struct ath_softc *);
void	ath_stop(struct ifnet *);
void	ath_start(struct ifnet *);
void	ath_reset(struct ath_softc *, int);
int	ath_media_change(struct ifnet *);
void	ath_watchdog(struct ifnet *);
int	ath_ioctl(struct ifnet *, u_long, caddr_t);
void	ath_fatal_proc(void *, int);
void	ath_rxorn_proc(void *, int);
void	ath_bmiss_proc(void *, int);
int	ath_initkeytable(struct ath_softc *);
void    ath_mcastfilter_accum(caddr_t, u_int32_t (*)[2]);
void    ath_mcastfilter_compute(struct ath_softc *, u_int32_t (*)[2]);
u_int32_t ath_calcrxfilter(struct ath_softc *);
void	ath_mode_init(struct ath_softc *);
#ifndef IEEE80211_STA_ONLY
int	ath_beacon_alloc(struct ath_softc *, struct ieee80211_node *);
void	ath_beacon_proc(void *, int);
void	ath_beacon_free(struct ath_softc *);
#endif
void	ath_beacon_config(struct ath_softc *);
int	ath_desc_alloc(struct ath_softc *);
void	ath_desc_free(struct ath_softc *);
struct ieee80211_node *ath_node_alloc(struct ieee80211com *);
struct mbuf *ath_getmbuf(int, int, u_int);
void	ath_node_free(struct ieee80211com *, struct ieee80211_node *);
void	ath_node_copy(struct ieee80211com *,
	    struct ieee80211_node *, const struct ieee80211_node *);
u_int8_t ath_node_getrssi(struct ieee80211com *,
	    const struct ieee80211_node *);
int	ath_rxbuf_init(struct ath_softc *, struct ath_buf *);
void	ath_rx_proc(void *, int);
int	ath_tx_start(struct ath_softc *, struct ieee80211_node *,
	    struct ath_buf *, struct mbuf *);
void	ath_tx_proc(void *, int);
int	ath_chan_set(struct ath_softc *, struct ieee80211_node *);
void	ath_draintxq(struct ath_softc *);
void	ath_stoprecv(struct ath_softc *);
int	ath_startrecv(struct ath_softc *);
void	ath_next_scan(void *);
int	ath_set_slot_time(struct ath_softc *);
void	ath_calibrate(void *);
void	ath_ledstate(struct ath_softc *, enum ieee80211_state);
int	ath_newstate(struct ieee80211com *, enum ieee80211_state, int);
void	ath_newassoc(struct ieee80211com *,
	    struct ieee80211_node *, int);
int	ath_getchannels(struct ath_softc *, HAL_BOOL outdoor,
	    HAL_BOOL xchanmode);
int	ath_rate_setup(struct ath_softc *sc, u_int mode);
void	ath_setcurmode(struct ath_softc *, enum ieee80211_phymode);
void	ath_rssadapt_updatenode(void *, struct ieee80211_node *);
void	ath_rssadapt_updatestats(void *);
#ifndef IEEE80211_STA_ONLY
void	ath_recv_mgmt(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, struct ieee80211_rxinfo *, int);
#endif
void	ath_disable(struct ath_softc *);

int	ath_gpio_attach(struct ath_softc *, u_int16_t);
int	ath_gpio_pin_read(void *, int);
void	ath_gpio_pin_write(void *, int, int);
void	ath_gpio_pin_ctl(void *, int, int);

#ifdef AR_DEBUG
void	ath_printrxbuf(struct ath_buf *, int);
void	ath_printtxbuf(struct ath_buf *, int);
int ath_debug = 0;
#endif

int ath_dwelltime = 200;		/* 5 channels/second */
int ath_calinterval = 30;		/* calibrate every 30 secs */
int ath_outdoor = AH_TRUE;		/* outdoor operation */
int ath_xchanmode = AH_TRUE;		/* enable extended channels */
int ath_softcrypto = 1;			/* 1=enable software crypto */

struct cfdriver ath_cd = {
	NULL, "ath", DV_IFNET
};

int
ath_activate(struct device *self, int act)
{
	struct ath_softc *sc = (struct ath_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING) {
			ath_stop(ifp);
			if (sc->sc_power != NULL)
				(*sc->sc_power)(sc, act);
		}
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP) {
			ath_init(ifp);
			if (ifp->if_flags & IFF_RUNNING)
				ath_start(ifp);
		}
		break;
	}
	return 0;
}

int
ath_enable(struct ath_softc *sc)
{
	if (ATH_IS_ENABLED(sc) == 0) {
		if (sc->sc_enable != NULL && (*sc->sc_enable)(sc) != 0) {
			printf("%s: device enable failed\n",
				sc->sc_dev.dv_xname);
			return (EIO);
		}
		sc->sc_flags |= ATH_ENABLED;
	}
	return (0);
}

void
ath_disable(struct ath_softc *sc)
{
	if (!ATH_IS_ENABLED(sc))
		return;
	if (sc->sc_disable != NULL)
		(*sc->sc_disable)(sc);
	sc->sc_flags &= ~ATH_ENABLED;
}

int
ath_attach(u_int16_t devid, struct ath_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ath_hal *ah;
	HAL_STATUS status;
	HAL_TXQ_INFO qinfo;
	int error = 0, i;

	DPRINTF(ATH_DEBUG_ANY, ("%s: devid 0x%x\n", __func__, devid));

	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	sc->sc_flags &= ~ATH_ATTACHED;	/* make sure that it's not attached */

	ah = ath_hal_attach(devid, sc, sc->sc_st, sc->sc_sh,
	    sc->sc_pcie, &status);
	if (ah == NULL) {
		printf("%s: unable to attach hardware; HAL status %d\n",
			ifp->if_xname, status);
		error = ENXIO;
		goto bad;
	}
	if (ah->ah_abi != HAL_ABI_VERSION) {
		printf("%s: HAL ABI mismatch detected (0x%x != 0x%x)\n",
			ifp->if_xname, ah->ah_abi, HAL_ABI_VERSION);
		error = ENXIO;
		goto bad;
	}

	if (ah->ah_single_chip == AH_TRUE) {
		printf("%s: AR%s %u.%u phy %u.%u rf %u.%u", ifp->if_xname,
		    ar5k_printver(AR5K_VERSION_DEV, devid),
		    ah->ah_mac_version, ah->ah_mac_revision,
		    ah->ah_phy_revision >> 4, ah->ah_phy_revision & 0xf,
		    ah->ah_radio_5ghz_revision >> 4,
		    ah->ah_radio_5ghz_revision & 0xf);
	} else {
		printf("%s: AR%s %u.%u phy %u.%u", ifp->if_xname,
		    ar5k_printver(AR5K_VERSION_VER, ah->ah_mac_srev),
		    ah->ah_mac_version, ah->ah_mac_revision,
		    ah->ah_phy_revision >> 4, ah->ah_phy_revision & 0xf);
		printf(" rf%s %u.%u",
		    ar5k_printver(AR5K_VERSION_RAD, ah->ah_radio_5ghz_revision),
		    ah->ah_radio_5ghz_revision >> 4,
		    ah->ah_radio_5ghz_revision & 0xf);
		if (ah->ah_radio_2ghz_revision != 0) {
			printf(" rf%s %u.%u",
			    ar5k_printver(AR5K_VERSION_RAD,
			    ah->ah_radio_2ghz_revision),
			    ah->ah_radio_2ghz_revision >> 4,
			    ah->ah_radio_2ghz_revision & 0xf);
		}
	}
	if (ah->ah_ee_version == AR5K_EEPROM_VERSION_4_7)
		printf(" eeprom 4.7");
	else
		printf(" eeprom %1x.%1x", ah->ah_ee_version >> 12,
		    ah->ah_ee_version & 0xff);

#if 0
	if (ah->ah_radio_5ghz_revision >= AR5K_SREV_RAD_UNSUPP ||
	    ah->ah_radio_2ghz_revision >= AR5K_SREV_RAD_UNSUPP) {
		printf(": RF radio not supported\n");
		error = EOPNOTSUPP;
		goto bad;
	}
#endif

	sc->sc_ah = ah;
	sc->sc_invalid = 0;	/* ready to go, enable interrupt handling */

	/*
	 * Get regulation domain either stored in the EEPROM or defined
	 * as the default value. Some devices are known to have broken
	 * regulation domain values in their EEPROM.
	 */
	ath_hal_get_regdomain(ah, &ah->ah_regdomain);

	/*
	 * Construct channel list based on the current regulation domain.
	 */
	error = ath_getchannels(sc, ath_outdoor, ath_xchanmode);
	if (error != 0)
		goto bad;

	/*
	 * Setup rate tables for all potential media types.
	 */
	ath_rate_setup(sc, IEEE80211_MODE_11A);
	ath_rate_setup(sc, IEEE80211_MODE_11B);
	ath_rate_setup(sc, IEEE80211_MODE_11G);

	error = ath_desc_alloc(sc);
	if (error != 0) {
		printf(": failed to allocate descriptors: %d\n", error);
		goto bad;
	}
	timeout_set(&sc->sc_scan_to, ath_next_scan, sc);
	timeout_set(&sc->sc_cal_to, ath_calibrate, sc);
	timeout_set(&sc->sc_rssadapt_to, ath_rssadapt_updatestats, sc);

	ATH_TASK_INIT(&sc->sc_txtask, ath_tx_proc, sc);
	ATH_TASK_INIT(&sc->sc_rxtask, ath_rx_proc, sc);
	ATH_TASK_INIT(&sc->sc_rxorntask, ath_rxorn_proc, sc);
	ATH_TASK_INIT(&sc->sc_fataltask, ath_fatal_proc, sc);
	ATH_TASK_INIT(&sc->sc_bmisstask, ath_bmiss_proc, sc);
#ifndef IEEE80211_STA_ONLY
	ATH_TASK_INIT(&sc->sc_swbatask, ath_beacon_proc, sc);
#endif

	/*
	 * For now just pre-allocate one data queue and one
	 * beacon queue.  Note that the HAL handles resetting
	 * them at the needed time.  Eventually we'll want to
	 * allocate more tx queues for splitting management
	 * frames and for QOS support.
	 */
	sc->sc_bhalq = ath_hal_setup_tx_queue(ah, HAL_TX_QUEUE_BEACON, NULL);
	if (sc->sc_bhalq == (u_int) -1) {
		printf(": unable to setup a beacon xmit queue!\n");
		goto bad2;
	}

	for (i = 0; i <= HAL_TX_QUEUE_ID_DATA_MAX; i++) {
		bzero(&qinfo, sizeof(qinfo));
		qinfo.tqi_type = HAL_TX_QUEUE_DATA;
		qinfo.tqi_subtype = i; /* should be mapped to WME types */
		sc->sc_txhalq[i] = ath_hal_setup_tx_queue(ah,
		    HAL_TX_QUEUE_DATA, &qinfo);
		if (sc->sc_txhalq[i] == (u_int) -1) {
			printf(": unable to setup a data xmit queue %u!\n", i);
			goto bad2;
		}
	}

	ifp->if_softc = sc;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_start = ath_start;
	ifp->if_watchdog = ath_watchdog;
	ifp->if_ioctl = ath_ioctl;
	ifq_init_maxlen(&ifp->if_snd, ATH_TXBUF * ATH_TXDESC);

	ic->ic_softc = sc;
	ic->ic_newassoc = ath_newassoc;
	/* XXX not right but it's not used anywhere important */
	ic->ic_phytype = IEEE80211_T_OFDM;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_caps = IEEE80211_C_WEP	/* wep supported */
	    | IEEE80211_C_PMGT		/* power management */
#ifndef IEEE80211_STA_ONLY
	    | IEEE80211_C_IBSS		/* ibss, nee adhoc, mode */
	    | IEEE80211_C_HOSTAP	/* hostap mode */
#endif
	    | IEEE80211_C_MONITOR	/* monitor mode */
	    | IEEE80211_C_SHSLOT	/* short slot time supported */
	    | IEEE80211_C_SHPREAMBLE;	/* short preamble supported */
	if (ath_softcrypto)
		ic->ic_caps |= IEEE80211_C_RSN;	/* wpa/rsn supported */

	/*
	 * Not all chips have the VEOL support we want to use with
	 * IBSS beacon; check here for it.
	 */
	sc->sc_veol = ath_hal_has_veol(ah);

	/* get mac address from hardware */
	ath_hal_get_lladdr(ah, ic->ic_myaddr);

	if_attach(ifp);

	/* call MI attach routine. */
	ieee80211_ifattach(ifp);

	/* override default methods */
	ic->ic_node_alloc = ath_node_alloc;
	sc->sc_node_free = ic->ic_node_free;
	ic->ic_node_free = ath_node_free;
	sc->sc_node_copy = ic->ic_node_copy;
	ic->ic_node_copy = ath_node_copy;
	ic->ic_node_getrssi = ath_node_getrssi;
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = ath_newstate;
#ifndef IEEE80211_STA_ONLY
	sc->sc_recv_mgmt = ic->ic_recv_mgmt;
	ic->ic_recv_mgmt = ath_recv_mgmt;
#endif
	ic->ic_max_rssi = AR5K_MAX_RSSI;
	bcopy(etherbroadcastaddr, sc->sc_broadcast_addr, IEEE80211_ADDR_LEN);

	/* complete initialization */
	ieee80211_media_init(ifp, ath_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof(sc->sc_rxtapu);
	bzero(&sc->sc_rxtapu, sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(ATH_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof(sc->sc_txtapu);
	bzero(&sc->sc_txtapu, sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(ATH_TX_RADIOTAP_PRESENT);
#endif

	sc->sc_flags |= ATH_ATTACHED;

	/*
	 * Print regulation domain and the mac address. The regulation domain
	 * will be marked with a * if the EEPROM value has been overwritten.
	 */
	printf(", %s%s, address %s\n",
	    ieee80211_regdomain2name(ah->ah_regdomain),
	    ah->ah_regdomain != ah->ah_regdomain_hw ? "*" : "",
	    ether_sprintf(ic->ic_myaddr));

	if (ath_gpio_attach(sc, devid) == 0)
		sc->sc_flags |= ATH_GPIO;

	return 0;
bad2:
	ath_desc_free(sc);
bad:
	if (ah)
		ath_hal_detach(ah);
	sc->sc_invalid = 1;
	return error;
}

int
ath_detach(struct ath_softc *sc, int flags)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s;

	if ((sc->sc_flags & ATH_ATTACHED) == 0)
		return (0);

	config_detach_children(&sc->sc_dev, flags);

	DPRINTF(ATH_DEBUG_ANY, ("%s: if_flags %x\n", __func__, ifp->if_flags));

	timeout_del(&sc->sc_scan_to);
	timeout_del(&sc->sc_cal_to);
	timeout_del(&sc->sc_rssadapt_to);

	s = splnet();
	ath_stop(ifp);
	ath_desc_free(sc);
	ath_hal_detach(sc->sc_ah);

	ieee80211_ifdetach(ifp);
	if_detach(ifp);

	splx(s);

	return 0;
}

int
ath_intr(void *arg)
{
	return ath_intr1((struct ath_softc *)arg);
}

int
ath_intr1(struct ath_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ath_hal *ah = sc->sc_ah;
	HAL_INT status;

	if (sc->sc_invalid) {
		/*
		 * The hardware is not ready/present, don't touch anything.
		 * Note this can happen early on if the IRQ is shared.
		 */
		DPRINTF(ATH_DEBUG_ANY, ("%s: invalid; ignored\n", __func__));
		return 0;
	}
	if (!ath_hal_is_intr_pending(ah))		/* shared irq, not for us */
		return 0;
	if ((ifp->if_flags & (IFF_RUNNING|IFF_UP)) != (IFF_RUNNING|IFF_UP)) {
		DPRINTF(ATH_DEBUG_ANY, ("%s: if_flags 0x%x\n",
		    __func__, ifp->if_flags));
		ath_hal_get_isr(ah, &status);	/* clear ISR */
		ath_hal_set_intr(ah, 0);		/* disable further intr's */
		return 1; /* XXX */
	}
	ath_hal_get_isr(ah, &status);		/* NB: clears ISR too */
	DPRINTF(ATH_DEBUG_INTR, ("%s: status 0x%x\n", __func__, status));
	status &= sc->sc_imask;			/* discard unasked for bits */
	if (status & HAL_INT_FATAL) {
		sc->sc_stats.ast_hardware++;
		ath_hal_set_intr(ah, 0);		/* disable intr's until reset */
		ATH_TASK_RUN_OR_ENQUEUE(&sc->sc_fataltask);
	} else if (status & HAL_INT_RXORN) {
		sc->sc_stats.ast_rxorn++;
		ath_hal_set_intr(ah, 0);		/* disable intr's until reset */
		ATH_TASK_RUN_OR_ENQUEUE(&sc->sc_rxorntask);
	} else if (status & HAL_INT_MIB) {
		DPRINTF(ATH_DEBUG_INTR,
		    ("%s: resetting MIB counters\n", __func__));
		sc->sc_stats.ast_mib++;
		ath_hal_update_mib_counters(ah, &sc->sc_mib_stats);
	} else {
		if (status & HAL_INT_RXEOL) {
			/*
			 * NB: the hardware should re-read the link when
			 *     RXE bit is written, but it doesn't work at
			 *     least on older hardware revs.
			 */
			sc->sc_stats.ast_rxeol++;
			sc->sc_rxlink = NULL;
		}
		if (status & HAL_INT_TXURN) {
			sc->sc_stats.ast_txurn++;
			/* bump tx trigger level */
			ath_hal_update_tx_triglevel(ah, AH_TRUE);
		}
		if (status & HAL_INT_RX)
			ATH_TASK_RUN_OR_ENQUEUE(&sc->sc_rxtask);
		if (status & HAL_INT_TX)
			ATH_TASK_RUN_OR_ENQUEUE(&sc->sc_txtask);
		if (status & HAL_INT_SWBA)
			ATH_TASK_RUN_OR_ENQUEUE(&sc->sc_swbatask);
		if (status & HAL_INT_BMISS) {
			sc->sc_stats.ast_bmiss++;
			ATH_TASK_RUN_OR_ENQUEUE(&sc->sc_bmisstask);
		}
	}
	return 1;
}

void
ath_fatal_proc(void *arg, int pending)
{
	struct ath_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	if (ifp->if_flags & IFF_DEBUG)
		printf("%s: hardware error; resetting\n", ifp->if_xname);
	ath_reset(sc, 1);
}

void
ath_rxorn_proc(void *arg, int pending)
{
	struct ath_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	if (ifp->if_flags & IFF_DEBUG)
		printf("%s: rx FIFO overrun; resetting\n", ifp->if_xname);
	ath_reset(sc, 1);
}

void
ath_bmiss_proc(void *arg, int pending)
{
	struct ath_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	DPRINTF(ATH_DEBUG_ANY, ("%s: pending %u\n", __func__, pending));
	if (ic->ic_opmode != IEEE80211_M_STA)
		return;
	if (ic->ic_state == IEEE80211_S_RUN) {
		/*
		 * Rather than go directly to scan state, try to
		 * reassociate first.  If that fails then the state
		 * machine will drop us into scanning after timing
		 * out waiting for a probe response.
		 */
		ieee80211_new_state(ic, IEEE80211_S_ASSOC, -1);
	}
}

int
ath_init(struct ifnet *ifp)
{
	return ath_init1((struct ath_softc *)ifp->if_softc);
}

int
ath_init1(struct ath_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_node *ni;
	enum ieee80211_phymode mode;
	struct ath_hal *ah = sc->sc_ah;
	HAL_STATUS status;
	HAL_CHANNEL hchan;
	int error = 0, s;

	DPRINTF(ATH_DEBUG_ANY, ("%s: if_flags 0x%x\n",
	    __func__, ifp->if_flags));

	if ((error = ath_enable(sc)) != 0)
		return error;

	s = splnet();
	/*
	 * Stop anything previously setup.  This is safe
	 * whether this is the first time through or not.
	 */
	ath_stop(ifp);

	/*
	 * Reset the link layer address to the latest value.
	 */
	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	ath_hal_set_lladdr(ah, ic->ic_myaddr);

	/*
	 * The basic interface to setting the hardware in a good
	 * state is ``reset''.  On return the hardware is known to
	 * be powered up and with interrupts disabled.  This must
	 * be followed by initialization of the appropriate bits
	 * and then setup of the interrupt mask.
	 */
	hchan.channel = ic->ic_ibss_chan->ic_freq;
	hchan.channelFlags = ic->ic_ibss_chan->ic_flags;
	if (!ath_hal_reset(ah, ic->ic_opmode, &hchan, AH_TRUE, &status)) {
		printf("%s: unable to reset hardware; hal status %u\n",
			ifp->if_xname, status);
		error = EIO;
		goto done;
	}
	ath_set_slot_time(sc);

	if ((error = ath_initkeytable(sc)) != 0) {
		printf("%s: unable to reset the key cache\n",
		    ifp->if_xname);
		goto done;
	}

	if ((error = ath_startrecv(sc)) != 0) {
		printf("%s: unable to start recv logic\n", ifp->if_xname);
		goto done;
	}

	/*
	 * Enable interrupts.
	 */
	sc->sc_imask = HAL_INT_RX | HAL_INT_TX
	    | HAL_INT_RXEOL | HAL_INT_RXORN
	    | HAL_INT_FATAL | HAL_INT_GLOBAL;
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP)
		sc->sc_imask |= HAL_INT_MIB;
#endif
	ath_hal_set_intr(ah, sc->sc_imask);

	ifp->if_flags |= IFF_RUNNING;
	ic->ic_state = IEEE80211_S_INIT;

	/*
	 * The hardware should be ready to go now so it's safe
	 * to kick the 802.11 state machine as it's likely to
	 * immediately call back to us to send mgmt frames.
	 */
	ni = ic->ic_bss;
	ni->ni_chan = ic->ic_ibss_chan;
	mode = ieee80211_node_abg_mode(ic, ni);
	if (mode != sc->sc_curmode)
		ath_setcurmode(sc, mode);
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	} else {
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	}
done:
	splx(s);
	return error;
}

void
ath_stop(struct ifnet *ifp)
{
	struct ieee80211com *ic = (struct ieee80211com *) ifp;
	struct ath_softc *sc = ifp->if_softc;
	struct ath_hal *ah = sc->sc_ah;
	int s;

	DPRINTF(ATH_DEBUG_ANY, ("%s: invalid %u if_flags 0x%x\n",
	    __func__, sc->sc_invalid, ifp->if_flags));

	s = splnet();
	if (ifp->if_flags & IFF_RUNNING) {
		/*
		 * Shutdown the hardware and driver:
		 *    disable interrupts
		 *    turn off timers
		 *    clear transmit machinery
		 *    clear receive machinery
		 *    drain and release tx queues
		 *    reclaim beacon resources
		 *    reset 802.11 state machine
		 *    power down hardware
		 *
		 * Note that some of this work is not possible if the
		 * hardware is gone (invalid).
		 */
		ifp->if_flags &= ~IFF_RUNNING;
		ifp->if_timer = 0;
		if (!sc->sc_invalid)
			ath_hal_set_intr(ah, 0);
		ath_draintxq(sc);
		if (!sc->sc_invalid) {
			ath_stoprecv(sc);
		} else {
			sc->sc_rxlink = NULL;
		}
		ifq_purge(&ifp->if_snd);
#ifndef IEEE80211_STA_ONLY
		ath_beacon_free(sc);
#endif
		ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
		if (!sc->sc_invalid) {
			ath_hal_set_power(ah, HAL_PM_FULL_SLEEP, 0);
		}
		ath_disable(sc);
	}
	splx(s);
}

/*
 * Reset the hardware w/o losing operational state.  This is
 * basically a more efficient way of doing ath_stop, ath_init,
 * followed by state transitions to the current 802.11
 * operational state.  Used to recover from errors rx overrun
 * and to reset the hardware when rf gain settings must be reset.
 */
void
ath_reset(struct ath_softc *sc, int full)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211_channel *c;
	HAL_STATUS status;
	HAL_CHANNEL hchan;

	/*
	 * Convert to a HAL channel description.
	 */
	c = ic->ic_ibss_chan;
	hchan.channel = c->ic_freq;
	hchan.channelFlags = c->ic_flags;

	ath_hal_set_intr(ah, 0);		/* disable interrupts */
	ath_draintxq(sc);		/* stop xmit side */
	ath_stoprecv(sc);		/* stop recv side */
	/* NB: indicate channel change so we do a full reset */
	if (!ath_hal_reset(ah, ic->ic_opmode, &hchan,
	    full ? AH_TRUE : AH_FALSE, &status)) {
		printf("%s: %s: unable to reset hardware; hal status %u\n",
			ifp->if_xname, __func__, status);
	}
	ath_set_slot_time(sc);
	/* In case channel changed, save as a node channel */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	ath_hal_set_intr(ah, sc->sc_imask);
	if (ath_startrecv(sc) != 0)	/* restart recv */
		printf("%s: %s: unable to start recv logic\n", ifp->if_xname,
		    __func__);
	ath_start(ifp);			/* restart xmit */
	if (ic->ic_state == IEEE80211_S_RUN)
		ath_beacon_config(sc);	/* restart beacons */
}

void
ath_start(struct ifnet *ifp)
{
	struct ath_softc *sc = ifp->if_softc;
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct ath_buf *bf;
	struct mbuf *m;
	struct ieee80211_frame *wh;
	int s;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd) ||
	    sc->sc_invalid)
		return;
	for (;;) {
		/*
		 * Grab a TX buffer and associated resources.
		 */
		s = splnet();
		bf = TAILQ_FIRST(&sc->sc_txbuf);
		if (bf != NULL)
			TAILQ_REMOVE(&sc->sc_txbuf, bf, bf_list);
		splx(s);
		if (bf == NULL) {
			DPRINTF(ATH_DEBUG_ANY, ("%s: out of xmit buffers\n",
			    __func__));
			sc->sc_stats.ast_tx_qstop++;
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
		/*
		 * Poll the management queue for frames; they
		 * have priority over normal data frames.
		 */
		m = mq_dequeue(&ic->ic_mgtq);
		if (m == NULL) {
			/*
			 * No data frames go out unless we're associated.
			 */
			if (ic->ic_state != IEEE80211_S_RUN) {
				DPRINTF(ATH_DEBUG_ANY,
				    ("%s: ignore data packet, state %u\n",
				    __func__, ic->ic_state));
				sc->sc_stats.ast_tx_discard++;
				s = splnet();
				TAILQ_INSERT_TAIL(&sc->sc_txbuf, bf, bf_list);
				splx(s);
				break;
			}
			m = ifq_dequeue(&ifp->if_snd);
			if (m == NULL) {
				s = splnet();
				TAILQ_INSERT_TAIL(&sc->sc_txbuf, bf, bf_list);
				splx(s);
				break;
			}

#if NBPFILTER > 0
			if (ifp->if_bpf)
				bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

			/*
			 * Encapsulate the packet in prep for transmission.
			 */
			m = ieee80211_encap(ifp, m, &ni);
			if (m == NULL) {
				DPRINTF(ATH_DEBUG_ANY,
				    ("%s: encapsulation failure\n",
				    __func__));
				sc->sc_stats.ast_tx_encap++;
				goto bad;
			}
			wh = mtod(m, struct ieee80211_frame *);
		} else {
			ni = m->m_pkthdr.ph_cookie;

			wh = mtod(m, struct ieee80211_frame *);
			if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
			    IEEE80211_FC0_SUBTYPE_PROBE_RESP) {
				/* fill time stamp */
				u_int64_t tsf;
				u_int32_t *tstamp;

				tsf = ath_hal_get_tsf64(ah);
				/* XXX: adjust 100us delay to xmit */
				tsf += 100;
				tstamp = (u_int32_t *)&wh[1];
				tstamp[0] = htole32(tsf & 0xffffffff);
				tstamp[1] = htole32(tsf >> 32);
			}
			sc->sc_stats.ast_tx_mgmt++;
		}

		if (ath_tx_start(sc, ni, bf, m)) {
	bad:
			s = splnet();
			TAILQ_INSERT_TAIL(&sc->sc_txbuf, bf, bf_list);
			splx(s);
			ifp->if_oerrors++;
			if (ni != NULL)
				ieee80211_release_node(ic, ni);
			continue;
		}

		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
}

int
ath_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_RUNNING|IFF_UP)) ==
		    (IFF_RUNNING|IFF_UP))
			ath_init(ifp);		/* XXX lose error */
		error = 0;
	}
	return error;
}

void
ath_watchdog(struct ifnet *ifp)
{
	struct ath_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;
	if ((ifp->if_flags & IFF_RUNNING) == 0 || sc->sc_invalid)
		return;
	if (sc->sc_tx_timer) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", ifp->if_xname);
			ath_reset(sc, 1);
			ifp->if_oerrors++;
			sc->sc_stats.ast_watchdog++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
ath_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ath_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0, s;

	s = splnet();
	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				/*
				 * To avoid rescanning another access point,
				 * do not call ath_init() here.  Instead,
				 * only reflect promisc mode settings.
				 */
				ath_mode_init(sc);
			} else {
				/*
				 * Beware of being called during detach to
				 * reset promiscuous mode.  In that case we
				 * will still be marked UP but not RUNNING.
				 * However trying to re-init the interface
				 * is the wrong thing to do as we've already
				 * torn down much of our state.  There's
				 * probably a better way to deal with this.
				 */
				if (!sc->sc_invalid)
					ath_init(ifp);	/* XXX lose error */
			}
		} else
			ath_stop(ifp);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = (cmd == SIOCADDMULTI) ?
		    ether_addmulti(ifr, &sc->sc_ic.ic_ac) :
		    ether_delmulti(ifr, &sc->sc_ic.ic_ac);
		if (error == ENETRESET) {
			if (ifp->if_flags & IFF_RUNNING)
				ath_mode_init(sc);
			error = 0;
		}
		break;
	case SIOCGATHSTATS:
		error = copyout(&sc->sc_stats,
		    ifr->ifr_data, sizeof (sc->sc_stats));
		break;
	default:
		error = ieee80211_ioctl(ifp, cmd, data);
		if (error == ENETRESET) {
			if ((ifp->if_flags & (IFF_RUNNING|IFF_UP)) ==
			    (IFF_RUNNING|IFF_UP)) {
				if (ic->ic_opmode != IEEE80211_M_MONITOR)
					ath_init(ifp);	/* XXX lose error */
				else
					ath_reset(sc, 1);
			}
			error = 0;
		}
		break;
	}
	splx(s);
	return error;
}

/*
 * Fill the hardware key cache with key entries.
 */
int
ath_initkeytable(struct ath_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ath_hal *ah = sc->sc_ah;
	int i;

	if (ath_softcrypto) {
		/*
		 * Disable the hardware crypto engine and reset the key cache
		 * to allow software crypto operation for WEP/RSN/WPA2
		 */
		if (ic->ic_flags & (IEEE80211_F_WEPON|IEEE80211_F_RSNON))
			(void)ath_hal_softcrypto(ah, AH_TRUE);
		else
			(void)ath_hal_softcrypto(ah, AH_FALSE);
		return (0);
	}

	/* WEP is disabled, we only support WEP in hardware yet */
	if ((ic->ic_flags & IEEE80211_F_WEPON) == 0)
		return (0);

	/*
	 * Setup the hardware after reset: the key cache is filled as
	 * needed and the receive engine is set going.  Frame transmit
	 * is handled entirely in the frame output path; there's nothing
	 * to do here except setup the interrupt mask.
	 */

	/* XXX maybe should reset all keys when !WEPON */
	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		struct ieee80211_key *k = &ic->ic_nw_keys[i];
		if (k->k_len == 0)
			ath_hal_reset_key(ah, i);
		else {
			HAL_KEYVAL hk;

			bzero(&hk, sizeof(hk));
			/*
			 * Pad the key to a supported key length. It
			 * is always a good idea to use full-length
			 * keys without padded zeros but this seems
			 * to be the default behaviour used by many
			 * implementations.
			 */
			if (k->k_cipher == IEEE80211_CIPHER_WEP40)
				hk.wk_len = AR5K_KEYVAL_LENGTH_40;
			else if (k->k_cipher == IEEE80211_CIPHER_WEP104)
				hk.wk_len = AR5K_KEYVAL_LENGTH_104;
			else
				return (EINVAL);
			bcopy(k->k_key, hk.wk_key, hk.wk_len);

			if (ath_hal_set_key(ah, i, &hk) != AH_TRUE)
				return (EINVAL);
		}
	}

	return (0);
}

void
ath_mcastfilter_accum(caddr_t dl, u_int32_t (*mfilt)[2])
{
	u_int32_t val;
	u_int8_t pos;

	val = LE_READ_4(dl + 0);
	pos = (val >> 18) ^ (val >> 12) ^ (val >> 6) ^ val;
	val = LE_READ_4(dl + 3);
	pos ^= (val >> 18) ^ (val >> 12) ^ (val >> 6) ^ val;
	pos &= 0x3f;
	(*mfilt)[pos / 32] |= (1 << (pos % 32));
}

void
ath_mcastfilter_compute(struct ath_softc *sc, u_int32_t (*mfilt)[2])
{
	struct arpcom *ac = &sc->sc_ic.ic_ac;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct ether_multi *enm;
	struct ether_multistep estep;

	if (ac->ac_multirangecnt > 0) {
		/* XXX Punt on ranges. */
		(*mfilt)[0] = (*mfilt)[1] = ~((u_int32_t)0);
		ifp->if_flags |= IFF_ALLMULTI;
		return;
	}

	ETHER_FIRST_MULTI(estep, ac, enm);
	while (enm != NULL) {
		ath_mcastfilter_accum(enm->enm_addrlo, mfilt);
		ETHER_NEXT_MULTI(estep, enm);
	}
	ifp->if_flags &= ~IFF_ALLMULTI;
}

/*
 * Calculate the receive filter according to the
 * operating mode and state:
 *
 * o always accept unicast, broadcast, and multicast traffic
 * o maintain current state of phy error reception
 * o probe request frames are accepted only when operating in
 *   hostap, adhoc, or monitor modes
 * o enable promiscuous mode according to the interface state
 * o accept beacons:
 *   - when operating in adhoc mode so the 802.11 layer creates
 *     node table entries for peers,
 *   - when operating in station mode for collecting rssi data when
 *     the station is otherwise quiet, or
 *   - when scanning
 */
u_int32_t
ath_calcrxfilter(struct ath_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ath_hal *ah = sc->sc_ah;
	struct ifnet *ifp = &ic->ic_if;
	u_int32_t rfilt;

	rfilt = (ath_hal_get_rx_filter(ah) & HAL_RX_FILTER_PHYERR)
	    | HAL_RX_FILTER_UCAST | HAL_RX_FILTER_BCAST | HAL_RX_FILTER_MCAST;
	if (ic->ic_opmode != IEEE80211_M_STA)
		rfilt |= HAL_RX_FILTER_PROBEREQ;
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode != IEEE80211_M_AHDEMO)
#endif
		rfilt |= HAL_RX_FILTER_BEACON;
	if (ifp->if_flags & IFF_PROMISC)
		rfilt |= HAL_RX_FILTER_PROM;
	return rfilt;
}

void
ath_mode_init(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	u_int32_t rfilt, mfilt[2];

	/* configure rx filter */
	rfilt = ath_calcrxfilter(sc);
	ath_hal_set_rx_filter(ah, rfilt);

	/* configure operational mode */
	ath_hal_set_opmode(ah);

	/* calculate and install multicast filter */
	mfilt[0] = mfilt[1] = 0;
	ath_mcastfilter_compute(sc, &mfilt);
	ath_hal_set_mcast_filter(ah, mfilt[0], mfilt[1]);
	DPRINTF(ATH_DEBUG_MODE, ("%s: RX filter 0x%x, MC filter %08x:%08x\n",
	    __func__, rfilt, mfilt[0], mfilt[1]));
}

struct mbuf *
ath_getmbuf(int flags, int type, u_int pktlen)
{
	struct mbuf *m;

	KASSERT(pktlen <= MCLBYTES, ("802.11 packet too large: %u", pktlen));
	MGETHDR(m, flags, type);
	if (m != NULL && pktlen > MHLEN) {
		MCLGET(m, flags);
		if ((m->m_flags & M_EXT) == 0) {
			m_free(m);
			m = NULL;
		}
	}
	return m;
}

#ifndef IEEE80211_STA_ONLY
int
ath_beacon_alloc(struct ath_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ath_hal *ah = sc->sc_ah;
	struct ath_buf *bf;
	struct ath_desc *ds;
	struct mbuf *m;
	int error;
	u_int8_t rate;
	const HAL_RATE_TABLE *rt;
	u_int flags = 0;

	bf = sc->sc_bcbuf;
	if (bf->bf_m != NULL) {
		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
		m_freem(bf->bf_m);
		bf->bf_m = NULL;
		bf->bf_node = NULL;
	}
	/*
	 * NB: the beacon data buffer must be 32-bit aligned;
	 * we assume the mbuf routines will return us something
	 * with this alignment (perhaps should assert).
	 */
	m = ieee80211_beacon_alloc(ic, ni);
	if (m == NULL) {
		DPRINTF(ATH_DEBUG_BEACON, ("%s: cannot get mbuf/cluster\n",
		    __func__));
		sc->sc_stats.ast_be_nombuf++;
		return ENOMEM;
	}

	DPRINTF(ATH_DEBUG_BEACON, ("%s: m %p len %u\n", __func__, m, m->m_len));
	error = bus_dmamap_load_mbuf(sc->sc_dmat, bf->bf_dmamap, m,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		m_freem(m);
		return error;
	}
	KASSERT(bf->bf_nseg == 1,
		("%s: multi-segment packet; nseg %u", __func__, bf->bf_nseg));
	bf->bf_m = m;

	/* setup descriptors */
	ds = bf->bf_desc;
	bzero(ds, sizeof(struct ath_desc));

	if (ic->ic_opmode == IEEE80211_M_IBSS && sc->sc_veol) {
		ds->ds_link = bf->bf_daddr;	/* link to self */
		flags |= HAL_TXDESC_VEOL;
	} else {
		ds->ds_link = 0;
	}
	ds->ds_data = bf->bf_segs[0].ds_addr;

	DPRINTF(ATH_DEBUG_ANY, ("%s: segaddr %p seglen %u\n", __func__,
	    (caddr_t)bf->bf_segs[0].ds_addr, (u_int)bf->bf_segs[0].ds_len));

	/*
	 * Calculate rate code.
	 * XXX everything at min xmit rate
	 */
	rt = sc->sc_currates;
	KASSERT(rt != NULL, ("no rate table, mode %u", sc->sc_curmode));
	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE) {
		rate = rt->info[0].rateCode | rt->info[0].shortPreamble;
	} else {
		rate = rt->info[0].rateCode;
	}

	flags = HAL_TXDESC_NOACK;
	if (ic->ic_opmode == IEEE80211_M_IBSS)
		flags |= HAL_TXDESC_VEOL;

	if (!ath_hal_setup_tx_desc(ah, ds
		, m->m_pkthdr.len + IEEE80211_CRC_LEN	/* packet length */
		, sizeof(struct ieee80211_frame)	/* header length */
		, HAL_PKT_TYPE_BEACON		/* Atheros packet type */
		, 60				/* txpower XXX */
		, rate, 1			/* series 0 rate/tries */
		, HAL_TXKEYIX_INVALID		/* no encryption */
		, 0				/* antenna mode */
		, flags				/* no ack for beacons */
		, 0				/* rts/cts rate */
		, 0				/* rts/cts duration */
	)) {
		printf("%s: ath_hal_setup_tx_desc failed\n", __func__);
		return -1;
	}
	/* NB: beacon's BufLen must be a multiple of 4 bytes */
	/* XXX verify mbuf data area covers this roundup */
	if (!ath_hal_fill_tx_desc(ah, ds
		, roundup(bf->bf_segs[0].ds_len, 4)	/* buffer length */
		, AH_TRUE				/* first segment */
		, AH_TRUE				/* last segment */
	)) {
		printf("%s: ath_hal_fill_tx_desc failed\n", __func__);
		return -1;
	}

	/* XXX it is not appropriate to bus_dmamap_sync? -dcy */

	return 0;
}

void
ath_beacon_proc(void *arg, int pending)
{
	struct ath_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ath_buf *bf = sc->sc_bcbuf;
	struct ath_hal *ah = sc->sc_ah;

	DPRINTF(ATH_DEBUG_BEACON_PROC, ("%s: pending %u\n", __func__, pending));
	if (ic->ic_opmode == IEEE80211_M_STA ||
	    bf == NULL || bf->bf_m == NULL) {
		DPRINTF(ATH_DEBUG_ANY, ("%s: ic_flags=%x bf=%p bf_m=%p\n",
		    __func__, ic->ic_flags, bf, bf ? bf->bf_m : NULL));
		return;
	}
	/* TODO: update beacon to reflect PS poll state */
	if (!ath_hal_stop_tx_dma(ah, sc->sc_bhalq)) {
		DPRINTF(ATH_DEBUG_ANY, ("%s: beacon queue %u did not stop?\n",
		    __func__, sc->sc_bhalq));
	}
	bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap, 0,
	    bf->bf_dmamap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	ath_hal_put_tx_buf(ah, sc->sc_bhalq, bf->bf_daddr);
	ath_hal_tx_start(ah, sc->sc_bhalq);
	DPRINTF(ATH_DEBUG_BEACON_PROC,
	    ("%s: TXDP%u = %p (%p)\n", __func__,
	    sc->sc_bhalq, (caddr_t)bf->bf_daddr, bf->bf_desc));
}

void
ath_beacon_free(struct ath_softc *sc)
{
	struct ath_buf *bf = sc->sc_bcbuf;

	if (bf->bf_m != NULL) {
		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
		m_freem(bf->bf_m);
		bf->bf_m = NULL;
		bf->bf_node = NULL;
	}
}
#endif	/* IEEE80211_STA_ONLY */

/*
 * Configure the beacon and sleep timers.
 *
 * When operating as an AP this resets the TSF and sets
 * up the hardware to notify us when we need to issue beacons.
 *
 * When operating in station mode this sets up the beacon
 * timers according to the timestamp of the last received
 * beacon and the current TSF, configures PCF and DTIM
 * handling, programs the sleep registers so the hardware
 * will wakeup in time to receive beacons, and configures
 * the beacon miss handling so we'll receive a BMISS
 * interrupt when we stop seeing beacons from the AP
 * we've associated with.
 */
void
ath_beacon_config(struct ath_softc *sc)
{
#define MS_TO_TU(x)	(((x) * 1000) / 1024)
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	u_int32_t nexttbtt, intval;

	nexttbtt = (LE_READ_4(ni->ni_tstamp + 4) << 22) |
	    (LE_READ_4(ni->ni_tstamp) >> 10);
	intval = MAX(1, ni->ni_intval) & HAL_BEACON_PERIOD;
	if (nexttbtt == 0) {	/* e.g. for ap mode */
		nexttbtt = intval;
	} else if (intval) {
		nexttbtt = roundup(nexttbtt, intval);
	}
	DPRINTF(ATH_DEBUG_BEACON, ("%s: intval %u nexttbtt %u\n",
	    __func__, ni->ni_intval, nexttbtt));
	if (ic->ic_opmode == IEEE80211_M_STA) {
		HAL_BEACON_STATE bs;

		/* NB: no PCF support right now */
		bzero(&bs, sizeof(bs));
		bs.bs_intval = intval;
		bs.bs_nexttbtt = nexttbtt;
		bs.bs_dtimperiod = bs.bs_intval;
		bs.bs_nextdtim = nexttbtt;
		/*
		 * Calculate the number of consecutive beacons to miss
		 * before taking a BMISS interrupt. 
		 * Note that we clamp the result to at most 7 beacons.
		 */
		bs.bs_bmissthreshold = ic->ic_bmissthres;
		if (bs.bs_bmissthreshold > 7) {
			bs.bs_bmissthreshold = 7;
		} else if (bs.bs_bmissthreshold <= 0) {
			bs.bs_bmissthreshold = 1;
		}

		/*
		 * Calculate sleep duration.  The configuration is
		 * given in ms.  We insure a multiple of the beacon
		 * period is used.  Also, if the sleep duration is
		 * greater than the DTIM period then it makes senses
		 * to make it a multiple of that.
		 *
		 * XXX fixed at 100ms
		 */
		bs.bs_sleepduration =
			roundup(MS_TO_TU(100), bs.bs_intval);
		if (bs.bs_sleepduration > bs.bs_dtimperiod) {
			bs.bs_sleepduration =
			    roundup(bs.bs_sleepduration, bs.bs_dtimperiod);
		}

		DPRINTF(ATH_DEBUG_BEACON,
		    ("%s: intval %u nexttbtt %u dtim %u nextdtim %u bmiss %u"
		    " sleep %u\n"
		    , __func__
		    , bs.bs_intval
		    , bs.bs_nexttbtt
		    , bs.bs_dtimperiod
		    , bs.bs_nextdtim
		    , bs.bs_bmissthreshold
		    , bs.bs_sleepduration
		));
		ath_hal_set_intr(ah, 0);
		ath_hal_set_beacon_timers(ah, &bs, 0/*XXX*/, 0, 0);
		sc->sc_imask |= HAL_INT_BMISS;
		ath_hal_set_intr(ah, sc->sc_imask);
	}
#ifndef IEEE80211_STA_ONLY
	else {
		ath_hal_set_intr(ah, 0);
		if (nexttbtt == intval)
			intval |= HAL_BEACON_RESET_TSF;
		if (ic->ic_opmode == IEEE80211_M_IBSS) {
			/*
			 * In IBSS mode enable the beacon timers but only
			 * enable SWBA interrupts if we need to manually
			 * prepare beacon frames. Otherwise we use a
			 * self-linked tx descriptor and let the hardware
			 * deal with things.
			 */
			intval |= HAL_BEACON_ENA;
			if (!sc->sc_veol)
				sc->sc_imask |= HAL_INT_SWBA;
		} else if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
			/*
			 * In AP mode we enable the beacon timers and
			 * SWBA interrupts to prepare beacon frames.
			 */
			intval |= HAL_BEACON_ENA;
			sc->sc_imask |= HAL_INT_SWBA;	/* beacon prepare */
		}
		ath_hal_init_beacon(ah, nexttbtt, intval);
		ath_hal_set_intr(ah, sc->sc_imask);
		/*
		 * When using a self-linked beacon descriptor in IBBS
		 * mode load it once here.
		 */
		if (ic->ic_opmode == IEEE80211_M_IBSS && sc->sc_veol)
			ath_beacon_proc(sc, 0);
	}
#endif
}

int
ath_desc_alloc(struct ath_softc *sc)
{
	int i, bsize, error = -1;
	struct ath_desc *ds;
	struct ath_buf *bf;

	/* allocate descriptors */
	sc->sc_desc_len = sizeof(struct ath_desc) *
				(ATH_TXBUF * ATH_TXDESC + ATH_RXBUF + 1);
	if ((error = bus_dmamem_alloc(sc->sc_dmat, sc->sc_desc_len, PAGE_SIZE,
	    0, &sc->sc_dseg, 1, &sc->sc_dnseg, 0)) != 0) {
		printf("%s: unable to allocate control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail0;
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, &sc->sc_dseg, sc->sc_dnseg,
	    sc->sc_desc_len, (caddr_t *)&sc->sc_desc, BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail1;
	}

	if ((error = bus_dmamap_create(sc->sc_dmat, sc->sc_desc_len, 1,
	    sc->sc_desc_len, 0, 0, &sc->sc_ddmamap)) != 0) {
		printf("%s: unable to create control data DMA map, "
		    "error = %d\n", sc->sc_dev.dv_xname, error);
		goto fail2;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_ddmamap, sc->sc_desc,
	    sc->sc_desc_len, NULL, 0)) != 0) {
		printf("%s: unable to load control data DMA map, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail3;
	}

	ds = sc->sc_desc;
	sc->sc_desc_paddr = sc->sc_ddmamap->dm_segs[0].ds_addr;

	DPRINTF(ATH_DEBUG_XMIT_DESC|ATH_DEBUG_RECV_DESC,
	    ("ath_desc_alloc: DMA map: %p (%lu) -> %p (%lu)\n",
	    ds, (u_long)sc->sc_desc_len,
	    (caddr_t) sc->sc_desc_paddr, /*XXX*/ (u_long) sc->sc_desc_len));

	/* allocate buffers */
	bsize = sizeof(struct ath_buf) * (ATH_TXBUF + ATH_RXBUF + 1);
	bf = malloc(bsize, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (bf == NULL) {
		printf("%s: unable to allocate Tx/Rx buffers\n",
		    sc->sc_dev.dv_xname);
		error = ENOMEM;
		goto fail3;
	}
	sc->sc_bufptr = bf;

	TAILQ_INIT(&sc->sc_rxbuf);
	for (i = 0; i < ATH_RXBUF; i++, bf++, ds++) {
		bf->bf_desc = ds;
		bf->bf_daddr = sc->sc_desc_paddr +
		    ((caddr_t)ds - (caddr_t)sc->sc_desc);
		if ((error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, 0, &bf->bf_dmamap)) != 0) {
			printf("%s: unable to create Rx dmamap, error = %d\n",
			    sc->sc_dev.dv_xname, error);
			goto fail4;
		}
		TAILQ_INSERT_TAIL(&sc->sc_rxbuf, bf, bf_list);
	}

	TAILQ_INIT(&sc->sc_txbuf);
	for (i = 0; i < ATH_TXBUF; i++, bf++, ds += ATH_TXDESC) {
		bf->bf_desc = ds;
		bf->bf_daddr = sc->sc_desc_paddr +
		    ((caddr_t)ds - (caddr_t)sc->sc_desc);
		if ((error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    ATH_TXDESC, MCLBYTES, 0, 0, &bf->bf_dmamap)) != 0) {
			printf("%s: unable to create Tx dmamap, error = %d\n",
			    sc->sc_dev.dv_xname, error);
			goto fail5;
		}
		TAILQ_INSERT_TAIL(&sc->sc_txbuf, bf, bf_list);
	}
	TAILQ_INIT(&sc->sc_txq);

	/* beacon buffer */
	bf->bf_desc = ds;
	bf->bf_daddr = sc->sc_desc_paddr + ((caddr_t)ds - (caddr_t)sc->sc_desc);
	if ((error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES, 0, 0,
	    &bf->bf_dmamap)) != 0) {
		printf("%s: unable to create beacon dmamap, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail5;
	}
	sc->sc_bcbuf = bf;
	return 0;

fail5:
	for (i = ATH_RXBUF; i < ATH_RXBUF + ATH_TXBUF; i++) {
		if (sc->sc_bufptr[i].bf_dmamap == NULL)
			continue;
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_bufptr[i].bf_dmamap);
	}
fail4:
	for (i = 0; i < ATH_RXBUF; i++) {
		if (sc->sc_bufptr[i].bf_dmamap == NULL)
			continue;
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_bufptr[i].bf_dmamap);
	}
fail3:
	bus_dmamap_unload(sc->sc_dmat, sc->sc_ddmamap);
fail2:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_ddmamap);
	sc->sc_ddmamap = NULL;
fail1:
	bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_desc, sc->sc_desc_len);
fail0:
	bus_dmamem_free(sc->sc_dmat, &sc->sc_dseg, sc->sc_dnseg);
	return error;
}

void
ath_desc_free(struct ath_softc *sc)
{
	struct ath_buf *bf;

	bus_dmamap_unload(sc->sc_dmat, sc->sc_ddmamap);
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_ddmamap);
	bus_dmamem_free(sc->sc_dmat, &sc->sc_dseg, sc->sc_dnseg);

	TAILQ_FOREACH(bf, &sc->sc_txq, bf_list) {
		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
		bus_dmamap_destroy(sc->sc_dmat, bf->bf_dmamap);
		m_freem(bf->bf_m);
	}
	TAILQ_FOREACH(bf, &sc->sc_txbuf, bf_list)
		bus_dmamap_destroy(sc->sc_dmat, bf->bf_dmamap);
	TAILQ_FOREACH(bf, &sc->sc_rxbuf, bf_list) {
		if (bf->bf_m) {
			bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
			bus_dmamap_destroy(sc->sc_dmat, bf->bf_dmamap);
			m_freem(bf->bf_m);
			bf->bf_m = NULL;
		}
	}
	if (sc->sc_bcbuf != NULL) {
		bus_dmamap_unload(sc->sc_dmat, sc->sc_bcbuf->bf_dmamap);
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_bcbuf->bf_dmamap);
		sc->sc_bcbuf = NULL;
	}

	TAILQ_INIT(&sc->sc_rxbuf);
	TAILQ_INIT(&sc->sc_txbuf);
	TAILQ_INIT(&sc->sc_txq);
	free(sc->sc_bufptr, M_DEVBUF, 0);
	sc->sc_bufptr = NULL;
}

struct ieee80211_node *
ath_node_alloc(struct ieee80211com *ic)
{
	struct ath_node *an;

	an = malloc(sizeof(*an), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (an) {
		int i;
		for (i = 0; i < ATH_RHIST_SIZE; i++)
			an->an_rx_hist[i].arh_ticks = ATH_RHIST_NOTIME;
		an->an_rx_hist_next = ATH_RHIST_SIZE-1;
		return &an->an_node;
	} else
		return NULL;
}

void
ath_node_free(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ath_softc *sc = ic->ic_if.if_softc;
	struct ath_buf *bf;

	TAILQ_FOREACH(bf, &sc->sc_txq, bf_list) {
		if (bf->bf_node == ni)
			bf->bf_node = NULL;
	}
	(*sc->sc_node_free)(ic, ni);
}

void
ath_node_copy(struct ieee80211com *ic,
	struct ieee80211_node *dst, const struct ieee80211_node *src)
{
	struct ath_softc *sc = ic->ic_if.if_softc;

	bcopy(&src[1], &dst[1],
		sizeof(struct ath_node) - sizeof(struct ieee80211_node));
	(*sc->sc_node_copy)(ic, dst, src);
}

u_int8_t
ath_node_getrssi(struct ieee80211com *ic, const struct ieee80211_node *ni)
{
	const struct ath_node *an = ATH_NODE(ni);
	int i, now, nsamples, rssi;

	/*
	 * Calculate the average over the last second of sampled data.
	 */
	now = ATH_TICKS();
	nsamples = 0;
	rssi = 0;
	i = an->an_rx_hist_next;
	do {
		const struct ath_recv_hist *rh = &an->an_rx_hist[i];
		if (rh->arh_ticks == ATH_RHIST_NOTIME)
			goto done;
		if (now - rh->arh_ticks > hz)
			goto done;
		rssi += rh->arh_rssi;
		nsamples++;
		if (i == 0) {
			i = ATH_RHIST_SIZE-1;
		} else {
			i--;
		}
	} while (i != an->an_rx_hist_next);
done:
	/*
	 * Return either the average or the last known
	 * value if there is no recent data.
	 */
	return (nsamples ? rssi / nsamples : an->an_rx_hist[i].arh_rssi);
}

int
ath_rxbuf_init(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ath_hal *ah = sc->sc_ah;
	int error;
	struct mbuf *m;
	struct ath_desc *ds;

	m = bf->bf_m;
	if (m == NULL) {
		/*
		 * NB: by assigning a page to the rx dma buffer we
		 * implicitly satisfy the Atheros requirement that
		 * this buffer be cache-line-aligned and sized to be
		 * multiple of the cache line size.  Not doing this
		 * causes weird stuff to happen (for the 5210 at least).
		 */
		m = ath_getmbuf(M_DONTWAIT, MT_DATA, MCLBYTES);
		if (m == NULL) {
			DPRINTF(ATH_DEBUG_ANY,
			    ("%s: no mbuf/cluster\n", __func__));
			sc->sc_stats.ast_rx_nombuf++;
			return ENOMEM;
		}
		bf->bf_m = m;
		m->m_pkthdr.len = m->m_len = m->m_ext.ext_size;

		error = bus_dmamap_load_mbuf(sc->sc_dmat, bf->bf_dmamap, m,
		    BUS_DMA_NOWAIT);
		if (error != 0) {
			DPRINTF(ATH_DEBUG_ANY,
			    ("%s: ath_bus_dmamap_load_mbuf failed;"
			    " error %d\n", __func__, error));
			sc->sc_stats.ast_rx_busdma++;
			return error;
		}
		KASSERT(bf->bf_nseg == 1,
			("ath_rxbuf_init: multi-segment packet; nseg %u",
			bf->bf_nseg));
	}
	bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap, 0,
	    bf->bf_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);

	/*
	 * Setup descriptors.  For receive we always terminate
	 * the descriptor list with a self-linked entry so we'll
	 * not get overrun under high load (as can happen with a
	 * 5212 when ANI processing enables PHY errors).
	 *
	 * To insure the last descriptor is self-linked we create
	 * each descriptor as self-linked and add it to the end.  As
	 * each additional descriptor is added the previous self-linked
	 * entry is ``fixed'' naturally.  This should be safe even
	 * if DMA is happening.  When processing RX interrupts we
	 * never remove/process the last, self-linked, entry on the
	 * descriptor list.  This insures the hardware always has
	 * someplace to write a new frame.
	 */
	ds = bf->bf_desc;
	bzero(ds, sizeof(struct ath_desc));
#ifndef IEEE80211_STA_ONLY
	if (sc->sc_ic.ic_opmode != IEEE80211_M_HOSTAP)
		ds->ds_link = bf->bf_daddr;	/* link to self */
#endif
	ds->ds_data = bf->bf_segs[0].ds_addr;
	ath_hal_setup_rx_desc(ah, ds
		, m->m_len		/* buffer size */
		, 0
	);

	if (sc->sc_rxlink != NULL)
		*sc->sc_rxlink = bf->bf_daddr;
	sc->sc_rxlink = &ds->ds_link;
	return 0;
}

void
ath_rx_proc(void *arg, int npending)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
#define	PA2DESC(_sc, _pa) \
	((struct ath_desc *)((caddr_t)(_sc)->sc_desc + \
		((_pa) - (_sc)->sc_desc_paddr)))
	struct ath_softc *sc = arg;
	struct ath_buf *bf;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ath_hal *ah = sc->sc_ah;
	struct ath_desc *ds;
	struct mbuf *m;
	struct ieee80211_frame *wh;
	struct ieee80211_frame whbuf;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct ath_node *an;
	struct ath_recv_hist *rh;
	int len;
	u_int phyerr;
	HAL_STATUS status;

	DPRINTF(ATH_DEBUG_RX_PROC, ("%s: pending %u\n", __func__, npending));
	do {
		bf = TAILQ_FIRST(&sc->sc_rxbuf);
		if (bf == NULL) {		/* NB: shouldn't happen */
			printf("%s: ath_rx_proc: no buffer!\n", ifp->if_xname);
			break;
		}
		ds = bf->bf_desc;
		if (ds->ds_link == bf->bf_daddr) {
			/* NB: never process the self-linked entry at the end */
			break;
		}
		m = bf->bf_m;
		if (m == NULL) {		/* NB: shouldn't happen */
			printf("%s: ath_rx_proc: no mbuf!\n", ifp->if_xname);
			continue;
		}
		/* XXX sync descriptor memory */
		/*
		 * Must provide the virtual address of the current
		 * descriptor, the physical address, and the virtual
		 * address of the next descriptor in the h/w chain.
		 * This allows the HAL to look ahead to see if the
		 * hardware is done with a descriptor by checking the
		 * done bit in the following descriptor and the address
		 * of the current descriptor the DMA engine is working
		 * on.  All this is necessary because of our use of
		 * a self-linked list to avoid rx overruns.
		 */
		status = ath_hal_proc_rx_desc(ah, ds,
		    bf->bf_daddr, PA2DESC(sc, ds->ds_link));
#ifdef AR_DEBUG
		if (ath_debug & ATH_DEBUG_RECV_DESC)
		    ath_printrxbuf(bf, status == HAL_OK);
#endif
		if (status == HAL_EINPROGRESS)
			break;
		TAILQ_REMOVE(&sc->sc_rxbuf, bf, bf_list);

		if (ds->ds_rxstat.rs_more) {
			/*
			 * Frame spans multiple descriptors; this
			 * cannot happen yet as we don't support
			 * jumbograms.  If not in monitor mode,
			 * discard the frame.
			 */

			/* 
			 * Enable this if you want to see error
			 * frames in Monitor mode.
			 */
#ifdef ERROR_FRAMES
			if (ic->ic_opmode != IEEE80211_M_MONITOR) {
				/* XXX statistic */
				goto rx_next;
			}
#endif
			/* fall thru for monitor mode handling... */

		} else if (ds->ds_rxstat.rs_status != 0) {
			if (ds->ds_rxstat.rs_status & HAL_RXERR_CRC)
				sc->sc_stats.ast_rx_crcerr++;
			if (ds->ds_rxstat.rs_status & HAL_RXERR_FIFO)
				sc->sc_stats.ast_rx_fifoerr++;
			if (ds->ds_rxstat.rs_status & HAL_RXERR_DECRYPT)
				sc->sc_stats.ast_rx_badcrypt++;
			if (ds->ds_rxstat.rs_status & HAL_RXERR_PHY) {
				sc->sc_stats.ast_rx_phyerr++;
				phyerr = ds->ds_rxstat.rs_phyerr & 0x1f;
				sc->sc_stats.ast_rx_phy[phyerr]++;
			}

			/*
			 * reject error frames, we normally don't want
			 * to see them in monitor mode.
			 */
			if ((ds->ds_rxstat.rs_status & HAL_RXERR_DECRYPT ) ||
			    (ds->ds_rxstat.rs_status & HAL_RXERR_PHY))
			    goto rx_next;

			/*
			 * In monitor mode, allow through packets that
			 * cannot be decrypted
			 */
			if ((ds->ds_rxstat.rs_status & ~HAL_RXERR_DECRYPT) ||
			    sc->sc_ic.ic_opmode != IEEE80211_M_MONITOR)
				goto rx_next;
		}

		len = ds->ds_rxstat.rs_datalen;
		if (len < IEEE80211_MIN_LEN) {
			DPRINTF(ATH_DEBUG_RECV, ("%s: short packet %d\n",
			    __func__, len));
			sc->sc_stats.ast_rx_tooshort++;
			goto rx_next;
		}

		bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap, 0,
		    bf->bf_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);

		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
		bf->bf_m = NULL;
		m->m_pkthdr.len = m->m_len = len;

#if NBPFILTER > 0
		if (sc->sc_drvbpf) {
			sc->sc_rxtap.wr_flags = IEEE80211_RADIOTAP_F_FCS;
			sc->sc_rxtap.wr_rate =
			    sc->sc_hwmap[ds->ds_rxstat.rs_rate] &
			    IEEE80211_RATE_VAL;
			sc->sc_rxtap.wr_antenna = ds->ds_rxstat.rs_antenna;
			sc->sc_rxtap.wr_rssi = ds->ds_rxstat.rs_rssi;
			sc->sc_rxtap.wr_max_rssi = ic->ic_max_rssi;

			bpf_mtap_hdr(sc->sc_drvbpf, &sc->sc_rxtap,
			    sc->sc_rxtap_len, m, BPF_DIRECTION_IN);
		}
#endif
		m_adj(m, -IEEE80211_CRC_LEN);
		wh = mtod(m, struct ieee80211_frame *);
		memset(&rxi, 0, sizeof(rxi));
		if (!ath_softcrypto && (wh->i_fc[1] & IEEE80211_FC1_WEP)) {
			/*
			 * WEP is decrypted by hardware. Clear WEP bit
			 * and trim WEP header for ieee80211_inputm().
			 */
			wh->i_fc[1] &= ~IEEE80211_FC1_WEP;
			bcopy(wh, &whbuf, sizeof(whbuf));
			m_adj(m, IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN);
			wh = mtod(m, struct ieee80211_frame *);
			bcopy(&whbuf, wh, sizeof(whbuf));
			/*
			 * Also trim WEP ICV from the tail.
			 */
			m_adj(m, -IEEE80211_WEP_CRCLEN);
			/*
			 * The header has probably moved.
			 */
			wh = mtod(m, struct ieee80211_frame *);

			rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
		}

		/*
		 * Locate the node for sender, track state, and
		 * then pass this node (referenced) up to the 802.11
		 * layer for its use.
		 */
		ni = ieee80211_find_rxnode(ic, wh);

		/*
		 * Record driver-specific state.
		 */
		an = ATH_NODE(ni);
		if (++(an->an_rx_hist_next) == ATH_RHIST_SIZE)
			an->an_rx_hist_next = 0;
		rh = &an->an_rx_hist[an->an_rx_hist_next];
		rh->arh_ticks = ATH_TICKS();
		rh->arh_rssi = ds->ds_rxstat.rs_rssi;
		rh->arh_antenna = ds->ds_rxstat.rs_antenna;

		/*
		 * Send frame up for processing.
		 */
		rxi.rxi_rssi = ds->ds_rxstat.rs_rssi;
		rxi.rxi_tstamp = ds->ds_rxstat.rs_tstamp;
		ieee80211_inputm(ifp, m, ni, &rxi, &ml);

		/* Handle the rate adaption */
		ieee80211_rssadapt_input(ic, ni, &an->an_rssadapt,
		    ds->ds_rxstat.rs_rssi);

		/*
		 * The frame may have caused the node to be marked for
		 * reclamation (e.g. in response to a DEAUTH message)
		 * so use release_node here instead of unref_node.
		 */
		ieee80211_release_node(ic, ni);

	rx_next:
		TAILQ_INSERT_TAIL(&sc->sc_rxbuf, bf, bf_list);
	} while (ath_rxbuf_init(sc, bf) == 0);

	if_input(ifp, &ml);

	ath_hal_set_rx_signal(ah);		/* rx signal state monitoring */
	ath_hal_start_rx(ah);			/* in case of RXEOL */
#undef PA2DESC
}

/*
 * XXX Size of an ACK control frame in bytes.
 */
#define	IEEE80211_ACK_SIZE	(2+2+IEEE80211_ADDR_LEN+4)

int
ath_tx_start(struct ath_softc *sc, struct ieee80211_node *ni,
    struct ath_buf *bf, struct mbuf *m0)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ath_hal *ah = sc->sc_ah;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int i, error, iswep, hdrlen, pktlen, len, s, tries;
	u_int8_t rix, cix, txrate, ctsrate;
	struct ath_desc *ds;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	u_int32_t iv;
	u_int8_t *ivp;
	u_int8_t hdrbuf[sizeof(struct ieee80211_frame) +
	    IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN];
	u_int subtype, flags, ctsduration, antenna;
	HAL_PKT_TYPE atype;
	const HAL_RATE_TABLE *rt;
	HAL_BOOL shortPreamble;
	struct ath_node *an;
	u_int8_t hwqueue = HAL_TX_QUEUE_ID_DATA_MIN;

	wh = mtod(m0, struct ieee80211_frame *);
	iswep = wh->i_fc[1] & IEEE80211_FC1_PROTECTED;
	hdrlen = sizeof(struct ieee80211_frame);
	pktlen = m0->m_pkthdr.len;

	if (ath_softcrypto && iswep) {
		k = ieee80211_get_txkey(ic, wh, ni);	
		if ((m0 = ieee80211_encrypt(ic, m0, k)) == NULL)
			return ENOMEM;
		wh = mtod(m0, struct ieee80211_frame *);

		/* reset len in case we got a new mbuf */
		pktlen = m0->m_pkthdr.len;
	} else if (!ath_softcrypto && iswep) {
		bcopy(mtod(m0, caddr_t), hdrbuf, hdrlen);
		m_adj(m0, hdrlen);
		M_PREPEND(m0, sizeof(hdrbuf), M_DONTWAIT);
		if (m0 == NULL) {
			sc->sc_stats.ast_tx_nombuf++;
			return ENOMEM;
		}
		ivp = hdrbuf + hdrlen;
		wh = mtod(m0, struct ieee80211_frame *);
		/*
		 * XXX
		 * IV must not duplicate during the lifetime of the key.
		 * But no mechanism to renew keys is defined in IEEE 802.11
		 * for WEP.  And the IV may be duplicated at other stations
		 * because the session key itself is shared.  So we use a
		 * pseudo random IV for now, though it is not the right way.
		 *
		 * NB: Rather than use a strictly random IV we select a
		 * random one to start and then increment the value for
		 * each frame.  This is an explicit tradeoff between
		 * overhead and security.  Given the basic insecurity of
		 * WEP this seems worthwhile.
		 */

		/*
		 * Skip 'bad' IVs from Fluhrer/Mantin/Shamir:
		 * (B, 255, N) with 3 <= B < 16 and 0 <= N <= 255
		 */
		iv = ic->ic_iv;
		if ((iv & 0xff00) == 0xff00) {
			int B = (iv & 0xff0000) >> 16;
			if (3 <= B && B < 16)
				iv = (B+1) << 16;
		}
		ic->ic_iv = iv + 1;

		/*
		 * NB: Preserve byte order of IV for packet
		 *     sniffers; it doesn't matter otherwise.
		 */
#if BYTE_ORDER == BIG_ENDIAN
		ivp[0] = iv >> 0;
		ivp[1] = iv >> 8;
		ivp[2] = iv >> 16;
#else
		ivp[2] = iv >> 0;
		ivp[1] = iv >> 8;
		ivp[0] = iv >> 16;
#endif
		ivp[3] = ic->ic_wep_txkey << 6; /* Key ID and pad */
		bcopy(hdrbuf, mtod(m0, caddr_t), sizeof(hdrbuf));
		/*
		 * The length of hdrlen and pktlen must be increased for WEP
		 */
		len = IEEE80211_WEP_IVLEN +
		    IEEE80211_WEP_KIDLEN +
		    IEEE80211_WEP_CRCLEN;
		hdrlen += len;
		pktlen += len;
	}
	pktlen += IEEE80211_CRC_LEN;

	/*
	 * Load the DMA map so any coalescing is done.  This
	 * also calculates the number of descriptors we need.
	 */
	error = bus_dmamap_load_mbuf(sc->sc_dmat, bf->bf_dmamap, m0,
	    BUS_DMA_NOWAIT);
	/*
	 * Discard null packets and check for packets that
	 * require too many TX descriptors.  We try to convert
	 * the latter to a cluster.
	 */
	if (error == EFBIG) {		/* too many desc's, linearize */
		sc->sc_stats.ast_tx_linear++;
		if (m_defrag(m0, M_DONTWAIT)) {
			sc->sc_stats.ast_tx_nomcl++;
			m_freem(m0);
			return ENOMEM;
		}
		error = bus_dmamap_load_mbuf(sc->sc_dmat, bf->bf_dmamap, m0,
		    BUS_DMA_NOWAIT);
		if (error != 0) {
			sc->sc_stats.ast_tx_busdma++;
			m_freem(m0);
			return error;
		}
		KASSERT(bf->bf_nseg == 1,
			("ath_tx_start: packet not one segment; nseg %u",
			bf->bf_nseg));
	} else if (error != 0) {
		sc->sc_stats.ast_tx_busdma++;
		m_freem(m0);
		return error;
	} else if (bf->bf_nseg == 0) {		/* null packet, discard */
		sc->sc_stats.ast_tx_nodata++;
		m_freem(m0);
		return EIO;
	}
	DPRINTF(ATH_DEBUG_XMIT, ("%s: m %p len %u\n", __func__, m0, pktlen));
	bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap, 0,
	    bf->bf_dmamap->dm_mapsize, BUS_DMASYNC_PREWRITE);
	bf->bf_m = m0;
	bf->bf_node = ni;			/* NB: held reference */
	an = ATH_NODE(ni);

	/* setup descriptors */
	ds = bf->bf_desc;
	rt = sc->sc_currates;
	KASSERT(rt != NULL, ("no rate table, mode %u", sc->sc_curmode));

	/*
	 * Calculate Atheros packet type from IEEE80211 packet header
	 * and setup for rate calculations.
	 */
	bf->bf_id.id_node = NULL;
	atype = HAL_PKT_TYPE_NORMAL;			/* default */
	switch (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) {
	case IEEE80211_FC0_TYPE_MGT:
		subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
		if (subtype == IEEE80211_FC0_SUBTYPE_BEACON) {
			atype = HAL_PKT_TYPE_BEACON;
		} else if (subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP) {
			atype = HAL_PKT_TYPE_PROBE_RESP;
		} else if (subtype == IEEE80211_FC0_SUBTYPE_ATIM) {
			atype = HAL_PKT_TYPE_ATIM;
		}
		rix = 0;			/* XXX lowest rate */
		break;
	case IEEE80211_FC0_TYPE_CTL:
		subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
		if (subtype == IEEE80211_FC0_SUBTYPE_PS_POLL)
			atype = HAL_PKT_TYPE_PSPOLL;
		rix = 0;			/* XXX lowest rate */
		break;
	default:
		/* remember link conditions for rate adaptation algorithm */
		if (ic->ic_fixed_rate == -1) {
			bf->bf_id.id_len = m0->m_pkthdr.len;
			bf->bf_id.id_rateidx = ni->ni_txrate;
			bf->bf_id.id_node = ni;
			bf->bf_id.id_rssi = ath_node_getrssi(ic, ni);
		}
		ni->ni_txrate = ieee80211_rssadapt_choose(&an->an_rssadapt,
		    &ni->ni_rates, wh, m0->m_pkthdr.len, ic->ic_fixed_rate,
		    ifp->if_xname, 0);
		rix = sc->sc_rixmap[ni->ni_rates.rs_rates[ni->ni_txrate] &
		    IEEE80211_RATE_VAL];
		if (rix == 0xff) {
			printf("%s: bogus xmit rate 0x%x (idx 0x%x)\n",
			    ifp->if_xname, ni->ni_rates.rs_rates[ni->ni_txrate],
			    ni->ni_txrate);
			sc->sc_stats.ast_tx_badrate++;
			m_freem(m0);
			return EIO;
		}
		break;
	}

	/*
	 * NB: the 802.11 layer marks whether or not we should
	 * use short preamble based on the current mode and
	 * negotiated parameters.
	 */
	if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
	    (ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_PREAMBLE)) {
		txrate = rt->info[rix].rateCode | rt->info[rix].shortPreamble;
		shortPreamble = AH_TRUE;
		sc->sc_stats.ast_tx_shortpre++;
	} else {
		txrate = rt->info[rix].rateCode;
		shortPreamble = AH_FALSE;
	}

	/*
	 * Calculate miscellaneous flags.
	 */
	flags = HAL_TXDESC_CLRDMASK;		/* XXX needed for wep errors */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= HAL_TXDESC_NOACK;	/* no ack on broad/multicast */
		sc->sc_stats.ast_tx_noack++;
	} else if (pktlen > ic->ic_rtsthreshold) {
		flags |= HAL_TXDESC_RTSENA;	/* RTS based on frame length */
		sc->sc_stats.ast_tx_rts++;
	}

	/*
	 * Calculate duration.  This logically belongs in the 802.11
	 * layer but it lacks sufficient information to calculate it.
	 */
	if ((flags & HAL_TXDESC_NOACK) == 0 &&
	    (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL) {
		u_int16_t dur;
		/*
		 * XXX not right with fragmentation.
		 */
		dur = ath_hal_computetxtime(ah, rt, IEEE80211_ACK_SIZE,
				rix, shortPreamble);
		*((u_int16_t*) wh->i_dur) = htole16(dur);
	}

	/*
	 * Calculate RTS/CTS rate and duration if needed.
	 */
	ctsduration = 0;
	if (flags & (HAL_TXDESC_RTSENA|HAL_TXDESC_CTSENA)) {
		/*
		 * CTS transmit rate is derived from the transmit rate
		 * by looking in the h/w rate table.  We must also factor
		 * in whether or not a short preamble is to be used.
		 */
		cix = rt->info[rix].controlRate;
		ctsrate = rt->info[cix].rateCode;
		if (shortPreamble)
			ctsrate |= rt->info[cix].shortPreamble;
		/*
		 * Compute the transmit duration based on the size
		 * of an ACK frame.  We call into the HAL to do the
		 * computation since it depends on the characteristics
		 * of the actual PHY being used.
		 */
		if (flags & HAL_TXDESC_RTSENA) {	/* SIFS + CTS */
			ctsduration += ath_hal_computetxtime(ah,
				rt, IEEE80211_ACK_SIZE, cix, shortPreamble);
		}
		/* SIFS + data */
		ctsduration += ath_hal_computetxtime(ah,
			rt, pktlen, rix, shortPreamble);
		if ((flags & HAL_TXDESC_NOACK) == 0) {	/* SIFS + ACK */
			ctsduration += ath_hal_computetxtime(ah,
				rt, IEEE80211_ACK_SIZE, cix, shortPreamble);
		}
	} else
		ctsrate = 0;

	/*
	 * For now use the antenna on which the last good
	 * frame was received on.  We assume this field is
	 * initialized to 0 which gives us ``auto'' or the
	 * ``default'' antenna.
	 */
	if (an->an_tx_antenna) {
		antenna = an->an_tx_antenna;
	} else {
		antenna = an->an_rx_hist[an->an_rx_hist_next].arh_antenna;
	}

#if NBPFILTER > 0
	if (ic->ic_rawbpf)
		bpf_mtap(ic->ic_rawbpf, m0, BPF_DIRECTION_OUT);

	if (sc->sc_drvbpf) {
		sc->sc_txtap.wt_flags = 0;
		if (shortPreamble)
			sc->sc_txtap.wt_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
		if (!ath_softcrypto && iswep)
			sc->sc_txtap.wt_flags |= IEEE80211_RADIOTAP_F_WEP;
		sc->sc_txtap.wt_rate = ni->ni_rates.rs_rates[ni->ni_txrate] &
		    IEEE80211_RATE_VAL;
		sc->sc_txtap.wt_txpower = 30;
		sc->sc_txtap.wt_antenna = antenna;

		bpf_mtap_hdr(sc->sc_drvbpf, &sc->sc_txtap, sc->sc_txtap_len,
		    m0, BPF_DIRECTION_OUT);
	}
#endif

	/*
	 * Formulate first tx descriptor with tx controls.
	 */
	tries = IEEE80211_IS_MULTICAST(wh->i_addr1) ? 1 : 15;
	/* XXX check return value? */
	ath_hal_setup_tx_desc(ah, ds
		, pktlen		/* packet length */
		, hdrlen		/* header length */
		, atype			/* Atheros packet type */
		, 60			/* txpower XXX */
		, txrate, tries		/* series 0 rate/tries */
		, iswep ? sc->sc_ic.ic_wep_txkey : HAL_TXKEYIX_INVALID
		, antenna		/* antenna mode */
		, flags			/* flags */
		, ctsrate		/* rts/cts rate */
		, ctsduration		/* rts/cts duration */
	);
#ifdef notyet
	ath_hal_setup_xtx_desc(ah, ds
		, AH_FALSE		/* short preamble */
		, 0, 0			/* series 1 rate/tries */
		, 0, 0			/* series 2 rate/tries */
		, 0, 0			/* series 3 rate/tries */
	);
#endif
	/*
	 * Fillin the remainder of the descriptor info.
	 */
	for (i = 0; i < bf->bf_nseg; i++, ds++) {
		ds->ds_data = bf->bf_segs[i].ds_addr;
		if (i == bf->bf_nseg - 1) {
			ds->ds_link = 0;
		} else {
			ds->ds_link = bf->bf_daddr + sizeof(*ds) * (i + 1);
		}
		ath_hal_fill_tx_desc(ah, ds
			, bf->bf_segs[i].ds_len	/* segment length */
			, i == 0		/* first segment */
			, i == bf->bf_nseg - 1	/* last segment */
		);
		DPRINTF(ATH_DEBUG_XMIT,
		    ("%s: %d: %08x %08x %08x %08x %08x %08x\n",
		    __func__, i, ds->ds_link, ds->ds_data,
		    ds->ds_ctl0, ds->ds_ctl1, ds->ds_hw[0], ds->ds_hw[1]));
	}

	/*
	 * Insert the frame on the outbound list and
	 * pass it on to the hardware.
	 */
	s = splnet();
	TAILQ_INSERT_TAIL(&sc->sc_txq, bf, bf_list);
	if (sc->sc_txlink == NULL) {
		ath_hal_put_tx_buf(ah, sc->sc_txhalq[hwqueue], bf->bf_daddr);
		DPRINTF(ATH_DEBUG_XMIT, ("%s: TXDP0 = %p (%p)\n", __func__,
		    (caddr_t)bf->bf_daddr, bf->bf_desc));
	} else {
		*sc->sc_txlink = bf->bf_daddr;
		DPRINTF(ATH_DEBUG_XMIT, ("%s: link(%p)=%p (%p)\n", __func__,
		    sc->sc_txlink, (caddr_t)bf->bf_daddr, bf->bf_desc));
	}
	sc->sc_txlink = &bf->bf_desc[bf->bf_nseg - 1].ds_link;
	splx(s);

	ath_hal_tx_start(ah, sc->sc_txhalq[hwqueue]);
	return 0;
}

void
ath_tx_proc(void *arg, int npending)
{
	struct ath_softc *sc = arg;
	struct ath_hal *ah = sc->sc_ah;
	struct ath_buf *bf;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ath_desc *ds;
	struct ieee80211_node *ni;
	struct ath_node *an;
	int sr, lr, s;
	HAL_STATUS status;

	for (;;) {
		s = splnet();
		bf = TAILQ_FIRST(&sc->sc_txq);
		if (bf == NULL) {
			sc->sc_txlink = NULL;
			splx(s);
			break;
		}
		/* only the last descriptor is needed */
		ds = &bf->bf_desc[bf->bf_nseg - 1];
		status = ath_hal_proc_tx_desc(ah, ds);
#ifdef AR_DEBUG
		if (ath_debug & ATH_DEBUG_XMIT_DESC)
			ath_printtxbuf(bf, status == HAL_OK);
#endif
		if (status == HAL_EINPROGRESS) {
			splx(s);
			break;
		}
		TAILQ_REMOVE(&sc->sc_txq, bf, bf_list);
		splx(s);

		ni = bf->bf_node;
		if (ni != NULL) {
			an = (struct ath_node *) ni;
			if (ds->ds_txstat.ts_status == 0) {
				if (bf->bf_id.id_node != NULL)
					ieee80211_rssadapt_raise_rate(ic,
					    &an->an_rssadapt, &bf->bf_id);
				an->an_tx_antenna = ds->ds_txstat.ts_antenna;
			} else {
				if (bf->bf_id.id_node != NULL)
					ieee80211_rssadapt_lower_rate(ic, ni,
					    &an->an_rssadapt, &bf->bf_id);
				if (ds->ds_txstat.ts_status & HAL_TXERR_XRETRY)
					sc->sc_stats.ast_tx_xretries++;
				if (ds->ds_txstat.ts_status & HAL_TXERR_FIFO)
					sc->sc_stats.ast_tx_fifoerr++;
				if (ds->ds_txstat.ts_status & HAL_TXERR_FILT)
					sc->sc_stats.ast_tx_filtered++;
				an->an_tx_antenna = 0;	/* invalidate */
			}
			sr = ds->ds_txstat.ts_shortretry;
			lr = ds->ds_txstat.ts_longretry;
			sc->sc_stats.ast_tx_shortretry += sr;
			sc->sc_stats.ast_tx_longretry += lr;
			/*
			 * Reclaim reference to node.
			 *
			 * NB: the node may be reclaimed here if, for example
			 *     this is a DEAUTH message that was sent and the
			 *     node was timed out due to inactivity.
			 */
			ieee80211_release_node(ic, ni);
		}
		bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap, 0,
		    bf->bf_dmamap->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
		m_freem(bf->bf_m);
		bf->bf_m = NULL;
		bf->bf_node = NULL;

		s = splnet();
		TAILQ_INSERT_TAIL(&sc->sc_txbuf, bf, bf_list);
		splx(s);
	}
	ifq_clr_oactive(&ifp->if_snd);
	sc->sc_tx_timer = 0;

	ath_start(ifp);
}

/*
 * Drain the transmit queue and reclaim resources.
 */
void
ath_draintxq(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_node *ni;
	struct ath_buf *bf;
	int s, i;

	/* XXX return value */
	if (!sc->sc_invalid) {
		for (i = 0; i <= HAL_TX_QUEUE_ID_DATA_MAX; i++) {
			/* don't touch the hardware if marked invalid */
			(void) ath_hal_stop_tx_dma(ah, sc->sc_txhalq[i]);
			DPRINTF(ATH_DEBUG_RESET,
			    ("%s: tx queue %d (%p), link %p\n", __func__, i,
			    (caddr_t)(u_intptr_t)ath_hal_get_tx_buf(ah,
			    sc->sc_txhalq[i]), sc->sc_txlink));
		}
		(void) ath_hal_stop_tx_dma(ah, sc->sc_bhalq);
		DPRINTF(ATH_DEBUG_RESET,
		    ("%s: beacon queue (%p)\n", __func__,
		    (caddr_t)(u_intptr_t)ath_hal_get_tx_buf(ah, sc->sc_bhalq)));
	}
	for (;;) {
		s = splnet();
		bf = TAILQ_FIRST(&sc->sc_txq);
		if (bf == NULL) {
			sc->sc_txlink = NULL;
			splx(s);
			break;
		}
		TAILQ_REMOVE(&sc->sc_txq, bf, bf_list);
		splx(s);
#ifdef AR_DEBUG
		if (ath_debug & ATH_DEBUG_RESET) {
			ath_printtxbuf(bf,
			    ath_hal_proc_tx_desc(ah, bf->bf_desc) == HAL_OK);
		}
#endif /* AR_DEBUG */
		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
		m_freem(bf->bf_m);
		bf->bf_m = NULL;
		ni = bf->bf_node;
		bf->bf_node = NULL;
		s = splnet();
		if (ni != NULL) {
			/*
			 * Reclaim node reference.
			 */
			ieee80211_release_node(ic, ni);
		}
		TAILQ_INSERT_TAIL(&sc->sc_txbuf, bf, bf_list);
		splx(s);
	}
	ifq_clr_oactive(&ifp->if_snd);
	sc->sc_tx_timer = 0;
}

/*
 * Disable the receive h/w in preparation for a reset.
 */
void
ath_stoprecv(struct ath_softc *sc)
{
#define	PA2DESC(_sc, _pa) \
	((struct ath_desc *)((caddr_t)(_sc)->sc_desc + \
		((_pa) - (_sc)->sc_desc_paddr)))
	struct ath_hal *ah = sc->sc_ah;

	ath_hal_stop_pcu_recv(ah);	/* disable PCU */
	ath_hal_set_rx_filter(ah, 0);	/* clear recv filter */
	ath_hal_stop_rx_dma(ah);	/* disable DMA engine */
#ifdef AR_DEBUG
	if (ath_debug & ATH_DEBUG_RESET) {
		struct ath_buf *bf;

		printf("%s: rx queue %p, link %p\n", __func__,
		    (caddr_t)(u_intptr_t)ath_hal_get_rx_buf(ah), sc->sc_rxlink);
		TAILQ_FOREACH(bf, &sc->sc_rxbuf, bf_list) {
			struct ath_desc *ds = bf->bf_desc;
			if (ath_hal_proc_rx_desc(ah, ds, bf->bf_daddr,
			    PA2DESC(sc, ds->ds_link)) == HAL_OK)
				ath_printrxbuf(bf, 1);
		}
	}
#endif
	sc->sc_rxlink = NULL;		/* just in case */
#undef PA2DESC
}

/*
 * Enable the receive h/w following a reset.
 */
int
ath_startrecv(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath_buf *bf;

	sc->sc_rxlink = NULL;
	TAILQ_FOREACH(bf, &sc->sc_rxbuf, bf_list) {
		int error = ath_rxbuf_init(sc, bf);
		if (error != 0) {
			DPRINTF(ATH_DEBUG_RECV,
			    ("%s: ath_rxbuf_init failed %d\n",
			    __func__, error));
			return error;
		}
	}

	bf = TAILQ_FIRST(&sc->sc_rxbuf);
	ath_hal_put_rx_buf(ah, bf->bf_daddr);
	ath_hal_start_rx(ah);		/* enable recv descriptors */
	ath_mode_init(sc);		/* set filters, etc. */
	ath_hal_start_rx_pcu(ah);	/* re-enable PCU/DMA engine */
	return 0;
}

/*
 * Set/change channels.  If the channel is really being changed,
 * it's done by resetting the chip.  To accomplish this we must
 * first cleanup any pending DMA, then restart stuff after a la
 * ath_init.
 */
int
ath_chan_set(struct ath_softc *sc, struct ieee80211_node *ni)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_channel *chan = ni->ni_chan;

	DPRINTF(ATH_DEBUG_ANY, ("%s: %u (%u MHz) -> %u (%u MHz)\n", __func__,
	    ieee80211_chan2ieee(ic, ic->ic_ibss_chan),
	    ic->ic_ibss_chan->ic_freq,
	    ieee80211_chan2ieee(ic, chan), chan->ic_freq));
	if (chan != ic->ic_ibss_chan) {
		HAL_STATUS status;
		HAL_CHANNEL hchan;
		enum ieee80211_phymode mode;

		/*
		 * To switch channels clear any pending DMA operations;
		 * wait long enough for the RX fifo to drain, reset the
		 * hardware at the new frequency, and then re-enable
		 * the relevant bits of the h/w.
		 */
		ath_hal_set_intr(ah, 0);		/* disable interrupts */
		ath_draintxq(sc);		/* clear pending tx frames */
		ath_stoprecv(sc);		/* turn off frame recv */
		/*
		 * Convert to a HAL channel description.
		 */
		hchan.channel = chan->ic_freq;
		hchan.channelFlags = chan->ic_flags;
		if (!ath_hal_reset(ah, ic->ic_opmode, &hchan, AH_TRUE,
		    &status)) {
			printf("%s: ath_chan_set: unable to reset "
				"channel %u (%u MHz)\n", ifp->if_xname,
				ieee80211_chan2ieee(ic, chan), chan->ic_freq);
			return EIO;
		}
		ath_set_slot_time(sc);
		/*
		 * Re-enable rx framework.
		 */
		if (ath_startrecv(sc) != 0) {
			printf("%s: ath_chan_set: unable to restart recv "
			    "logic\n", ifp->if_xname);
			return EIO;
		}

#if NBPFILTER > 0
		/*
		 * Update BPF state.
		 */
		sc->sc_txtap.wt_chan_freq = sc->sc_rxtap.wr_chan_freq =
		    htole16(chan->ic_freq);
		sc->sc_txtap.wt_chan_flags = sc->sc_rxtap.wr_chan_flags =
		    htole16(chan->ic_flags);
#endif

		/*
		 * Change channels and update the h/w rate map
		 * if we're switching; e.g. 11a to 11b/g.
		 */
		ic->ic_ibss_chan = chan;
		mode = ieee80211_node_abg_mode(ic, ni);
		if (mode != sc->sc_curmode)
			ath_setcurmode(sc, mode);

		/*
		 * Re-enable interrupts.
		 */
		ath_hal_set_intr(ah, sc->sc_imask);
	}
	return 0;
}

void
ath_next_scan(void *arg)
{
	struct ath_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	/* don't call ath_start w/o network interrupts blocked */
	s = splnet();

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);
	splx(s);
}

int
ath_set_slot_time(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		return (ath_hal_set_slot_time(ah, HAL_SLOT_TIME_9));

	return (0);
}

/*
 * Periodically recalibrate the PHY to account
 * for temperature/environment changes.
 */
void
ath_calibrate(void *arg)
{
	struct ath_softc *sc = arg;
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c;
	HAL_CHANNEL hchan;
	int s;

	sc->sc_stats.ast_per_cal++;

	/*
	 * Convert to a HAL channel description.
	 */
	c = ic->ic_ibss_chan;
	hchan.channel = c->ic_freq;
	hchan.channelFlags = c->ic_flags;

	s = splnet();
	DPRINTF(ATH_DEBUG_CALIBRATE,
	    ("%s: channel %u/%x\n", __func__, c->ic_freq, c->ic_flags));

	if (ath_hal_get_rf_gain(ah) == HAL_RFGAIN_NEED_CHANGE) {
		/*
		 * Rfgain is out of bounds, reset the chip
		 * to load new gain values.
		 */
		sc->sc_stats.ast_per_rfgain++;
		ath_reset(sc, 1);
	}
	if (!ath_hal_calibrate(ah, &hchan)) {
		DPRINTF(ATH_DEBUG_ANY,
		    ("%s: calibration of channel %u failed\n",
		    __func__, c->ic_freq));
		sc->sc_stats.ast_per_calfail++;
	}
	timeout_add_sec(&sc->sc_cal_to, ath_calinterval);
	splx(s);
}

void
ath_ledstate(struct ath_softc *sc, enum ieee80211_state state)
{
	HAL_LED_STATE led = HAL_LED_INIT;
	u_int32_t softled = AR5K_SOFTLED_OFF;

	switch (state) {
	case IEEE80211_S_INIT:
		break;
	case IEEE80211_S_SCAN:
		led = HAL_LED_SCAN;
		break;
	case IEEE80211_S_AUTH:
		led = HAL_LED_AUTH;
		break;
	case IEEE80211_S_ASSOC:
		led = HAL_LED_ASSOC;
		softled = AR5K_SOFTLED_ON;
		break;
	case IEEE80211_S_RUN:
		led = HAL_LED_RUN;
		softled = AR5K_SOFTLED_ON;
		break;
	}

	ath_hal_set_ledstate(sc->sc_ah, led);
	if (sc->sc_softled) {
		ath_hal_set_gpio_output(sc->sc_ah, AR5K_SOFTLED_PIN);
		ath_hal_set_gpio(sc->sc_ah, AR5K_SOFTLED_PIN, softled);
	}
}

int
ath_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ath_softc *sc = ifp->if_softc;
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211_node *ni;
	const u_int8_t *bssid;
	int error, i;

	u_int32_t rfilt;

	DPRINTF(ATH_DEBUG_ANY, ("%s: %s -> %s\n", __func__,
	    ieee80211_state_name[ic->ic_state],
	    ieee80211_state_name[nstate]));

	timeout_del(&sc->sc_scan_to);
	timeout_del(&sc->sc_cal_to);
	ath_ledstate(sc, nstate);

	if (nstate == IEEE80211_S_INIT) {
		timeout_del(&sc->sc_rssadapt_to);
		sc->sc_imask &= ~(HAL_INT_SWBA | HAL_INT_BMISS);
		ath_hal_set_intr(ah, sc->sc_imask);
		return (*sc->sc_newstate)(ic, nstate, arg);
	}
	ni = ic->ic_bss;
	error = ath_chan_set(sc, ni);
	if (error != 0)
		goto bad;
	rfilt = ath_calcrxfilter(sc);
	if (nstate == IEEE80211_S_SCAN ||
	    ic->ic_opmode == IEEE80211_M_MONITOR) {
		bssid = sc->sc_broadcast_addr;
	} else {
		bssid = ni->ni_bssid;
	}
	ath_hal_set_rx_filter(ah, rfilt);
	DPRINTF(ATH_DEBUG_ANY, ("%s: RX filter 0x%x bssid %s\n",
	    __func__, rfilt, ether_sprintf((u_char*)bssid)));

	if (nstate == IEEE80211_S_RUN && ic->ic_opmode == IEEE80211_M_STA) {
		ath_hal_set_associd(ah, bssid, ni->ni_associd);
	} else {
		ath_hal_set_associd(ah, bssid, 0);
	}

	if (!ath_softcrypto && (ic->ic_flags & IEEE80211_F_WEPON)) {
		for (i = 0; i < IEEE80211_WEP_NKID; i++) {
			if (ath_hal_is_key_valid(ah, i))
				ath_hal_set_key_lladdr(ah, i, bssid);
		}
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		/* nothing to do */
	} else if (nstate == IEEE80211_S_RUN) {
		DPRINTF(ATH_DEBUG_ANY, ("%s(RUN): "
		    "ic_flags=0x%08x iv=%d bssid=%s "
		    "capinfo=0x%04x chan=%d\n",
		    __func__,
		    ic->ic_flags,
		    ni->ni_intval,
		    ether_sprintf(ni->ni_bssid),
		    ni->ni_capinfo,
		    ieee80211_chan2ieee(ic, ni->ni_chan)));

		/*
		 * Allocate and setup the beacon frame for AP or adhoc mode.
		 */
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_IBSS) {
			error = ath_beacon_alloc(sc, ni);
			if (error != 0)
				goto bad;
		}
#endif
		/*
		 * Configure the beacon and sleep timers.
		 */
		ath_beacon_config(sc);
	} else {
		sc->sc_imask &= ~(HAL_INT_SWBA | HAL_INT_BMISS);
		ath_hal_set_intr(ah, sc->sc_imask);
	}

	/*
	 * Invoke the parent method to complete the work.
	 */
	error = (*sc->sc_newstate)(ic, nstate, arg);

	if (nstate == IEEE80211_S_RUN) {
		/* start periodic recalibration timer */
		timeout_add_sec(&sc->sc_cal_to, ath_calinterval);

		if (ic->ic_opmode != IEEE80211_M_MONITOR)
			timeout_add_msec(&sc->sc_rssadapt_to, 100);
	} else if (nstate == IEEE80211_S_SCAN) {
		/* start ap/neighbor scan timer */
		timeout_add_msec(&sc->sc_scan_to, ath_dwelltime);
	}
bad:
	return error;
}

#ifndef IEEE80211_STA_ONLY
void
ath_recv_mgmt(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, struct ieee80211_rxinfo *rxi, int subtype)
{
	struct ath_softc *sc = (struct ath_softc*)ic->ic_softc;
	struct ath_hal *ah = sc->sc_ah;

	(*sc->sc_recv_mgmt)(ic, m, ni, rxi, subtype);

	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
	case IEEE80211_FC0_SUBTYPE_BEACON:
		if (ic->ic_opmode != IEEE80211_M_IBSS ||
		    ic->ic_state != IEEE80211_S_RUN)
			break;
		if (ieee80211_ibss_merge(ic, ni, ath_hal_get_tsf64(ah)) ==
		    ENETRESET)
			ath_hal_set_associd(ah, ic->ic_bss->ni_bssid, 0);
		break;
	default:
		break;
	}
	return;
}
#endif

/*
 * Setup driver-specific state for a newly associated node.
 * Note that we're called also on a re-associate, the isnew
 * param tells us if this is the first time or not.
 */
void
ath_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		return;
}

int
ath_getchannels(struct ath_softc *sc, HAL_BOOL outdoor, HAL_BOOL xchanmode)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ath_hal *ah = sc->sc_ah;
	HAL_CHANNEL *chans;
	int i, ix, nchan;

	sc->sc_nchan = 0;
	chans = malloc(IEEE80211_CHAN_MAX * sizeof(HAL_CHANNEL),
			M_TEMP, M_NOWAIT);
	if (chans == NULL) {
		printf("%s: unable to allocate channel table\n", ifp->if_xname);
		return ENOMEM;
	}
	if (!ath_hal_init_channels(ah, chans, IEEE80211_CHAN_MAX, &nchan,
	    HAL_MODE_ALL, outdoor, xchanmode)) {
		printf("%s: unable to collect channel list from hal\n",
		    ifp->if_xname);
		free(chans, M_TEMP, 0);
		return EINVAL;
	}

	/*
	 * Convert HAL channels to ieee80211 ones and insert
	 * them in the table according to their channel number.
	 */
	for (i = 0; i < nchan; i++) {
		HAL_CHANNEL *c = &chans[i];
		ix = ieee80211_mhz2ieee(c->channel, c->channelFlags);
		if (ix > IEEE80211_CHAN_MAX) {
			printf("%s: bad hal channel %u (%u/%x) ignored\n",
				ifp->if_xname, ix, c->channel, c->channelFlags);
			continue;
		}
		DPRINTF(ATH_DEBUG_ANY,
		    ("%s: HAL channel %d/%d freq %d flags %#04x idx %d\n",
		    sc->sc_dev.dv_xname, i, nchan, c->channel, c->channelFlags,
		    ix));
		/* NB: flags are known to be compatible */
		if (ic->ic_channels[ix].ic_freq == 0) {
			ic->ic_channels[ix].ic_freq = c->channel;
			ic->ic_channels[ix].ic_flags = c->channelFlags;
		} else {
			/* channels overlap; e.g. 11g and 11b */
			ic->ic_channels[ix].ic_flags |= c->channelFlags;
		}
		/* count valid channels */
		sc->sc_nchan++;
	}
	free(chans, M_TEMP, 0);

	if (sc->sc_nchan < 1) {
		printf("%s: no valid channels for regdomain %s(%u)\n",
		    ifp->if_xname, ieee80211_regdomain2name(ah->ah_regdomain),
		    ah->ah_regdomain);
		return ENOENT;
	}

	/* set an initial channel */
	ic->ic_ibss_chan = &ic->ic_channels[0];

	return 0;
}

int
ath_rate_setup(struct ath_softc *sc, u_int mode)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211com *ic = &sc->sc_ic;
	const HAL_RATE_TABLE *rt;
	struct ieee80211_rateset *rs;
	int i, maxrates;

	switch (mode) {
	case IEEE80211_MODE_11A:
		sc->sc_rates[mode] = ath_hal_get_rate_table(ah, HAL_MODE_11A);
		break;
	case IEEE80211_MODE_11B:
		sc->sc_rates[mode] = ath_hal_get_rate_table(ah, HAL_MODE_11B);
		break;
	case IEEE80211_MODE_11G:
		sc->sc_rates[mode] = ath_hal_get_rate_table(ah, HAL_MODE_11G);
		break;
	default:
		DPRINTF(ATH_DEBUG_ANY,
		    ("%s: invalid mode %u\n", __func__, mode));
		return 0;
	}
	rt = sc->sc_rates[mode];
	if (rt == NULL)
		return 0;
	if (rt->rateCount > IEEE80211_RATE_MAXSIZE) {
		DPRINTF(ATH_DEBUG_ANY,
		    ("%s: rate table too small (%u > %u)\n",
		    __func__, rt->rateCount, IEEE80211_RATE_MAXSIZE));
		maxrates = IEEE80211_RATE_MAXSIZE;
	} else {
		maxrates = rt->rateCount;
	}
	rs = &ic->ic_sup_rates[mode];
	for (i = 0; i < maxrates; i++)
		rs->rs_rates[i] = rt->info[i].dot11Rate;
	rs->rs_nrates = maxrates;
	return 1;
}

void
ath_setcurmode(struct ath_softc *sc, enum ieee80211_phymode mode)
{
	const HAL_RATE_TABLE *rt;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	int i;

	memset(sc->sc_rixmap, 0xff, sizeof(sc->sc_rixmap));
	rt = sc->sc_rates[mode];
	KASSERT(rt != NULL, ("no h/w rate set for phy mode %u", mode));
	for (i = 0; i < rt->rateCount; i++)
		sc->sc_rixmap[rt->info[i].dot11Rate & IEEE80211_RATE_VAL] = i;
	bzero(sc->sc_hwmap, sizeof(sc->sc_hwmap));
	for (i = 0; i < 32; i++)
		sc->sc_hwmap[i] = rt->info[rt->rateCodeToIndex[i]].dot11Rate;
	sc->sc_currates = rt;
	sc->sc_curmode = mode;
	ni = ic->ic_bss;
	ni->ni_rates.rs_nrates = sc->sc_currates->rateCount;
	if (ni->ni_txrate >= ni->ni_rates.rs_nrates)
		ni->ni_txrate = 0;
}

void
ath_rssadapt_updatenode(void *arg, struct ieee80211_node *ni)
{
	struct ath_node *an = ATH_NODE(ni);

	ieee80211_rssadapt_updatestats(&an->an_rssadapt);
}

void
ath_rssadapt_updatestats(void *arg)
{
	struct ath_softc *sc = (struct ath_softc *)arg;
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_opmode == IEEE80211_M_STA) {
		ath_rssadapt_updatenode(arg, ic->ic_bss);
	} else {
		ieee80211_iterate_nodes(ic, ath_rssadapt_updatenode, arg);
	}

	timeout_add_msec(&sc->sc_rssadapt_to, 100);
}

#ifdef AR_DEBUG
void
ath_printrxbuf(struct ath_buf *bf, int done)
{
	struct ath_desc *ds;
	int i;

	for (i = 0, ds = bf->bf_desc; i < bf->bf_nseg; i++, ds++) {
		printf("R%d (%p %p) %08x %08x %08x %08x %08x %08x %c\n",
		    i, ds, (struct ath_desc *)bf->bf_daddr + i,
		    ds->ds_link, ds->ds_data,
		    ds->ds_ctl0, ds->ds_ctl1,
		    ds->ds_hw[0], ds->ds_hw[1],
		    !done ? ' ' : (ds->ds_rxstat.rs_status == 0) ? '*' : '!');
	}
}

void
ath_printtxbuf(struct ath_buf *bf, int done)
{
	struct ath_desc *ds;
	int i;

	for (i = 0, ds = bf->bf_desc; i < bf->bf_nseg; i++, ds++) {
		printf("T%d (%p %p) "
		    "%08x %08x %08x %08x %08x %08x %08x %08x %c\n",
		    i, ds, (struct ath_desc *)bf->bf_daddr + i,
		    ds->ds_link, ds->ds_data,
		    ds->ds_ctl0, ds->ds_ctl1,
		    ds->ds_hw[0], ds->ds_hw[1], ds->ds_hw[2], ds->ds_hw[3],
		    !done ? ' ' : (ds->ds_txstat.ts_status == 0) ? '*' : '!');
	}
}
#endif /* AR_DEBUG */

int
ath_gpio_attach(struct ath_softc *sc, u_int16_t devid)
{
	struct ath_hal *ah = sc->sc_ah;
	struct gpiobus_attach_args gba;
	int i;

	if (ah->ah_gpio_npins < 1)
		return 0;

	/* Initialize gpio pins array */
	for (i = 0; i < ah->ah_gpio_npins && i < AR5K_MAX_GPIO; i++) {
		sc->sc_gpio_pins[i].pin_num = i;
		sc->sc_gpio_pins[i].pin_caps = GPIO_PIN_INPUT |
		    GPIO_PIN_OUTPUT;

		/* Set pin mode to input */
		ath_hal_set_gpio_input(ah, i);
		sc->sc_gpio_pins[i].pin_flags = GPIO_PIN_INPUT;

		/* Get pin input */
		sc->sc_gpio_pins[i].pin_state = ath_hal_get_gpio(ah, i) ?
		    GPIO_PIN_HIGH : GPIO_PIN_LOW;
	}

	/* Enable GPIO-controlled software LED if available */
	if ((ah->ah_version == AR5K_AR5211) ||
	    (devid == PCI_PRODUCT_ATHEROS_AR5212_IBM)) {
		sc->sc_softled = 1;
		ath_hal_set_gpio_output(ah, AR5K_SOFTLED_PIN);
		ath_hal_set_gpio(ah, AR5K_SOFTLED_PIN, AR5K_SOFTLED_OFF);
	}

	/* Create gpio controller tag */
	sc->sc_gpio_gc.gp_cookie = sc;
	sc->sc_gpio_gc.gp_pin_read = ath_gpio_pin_read;
	sc->sc_gpio_gc.gp_pin_write = ath_gpio_pin_write;
	sc->sc_gpio_gc.gp_pin_ctl = ath_gpio_pin_ctl;

	gba.gba_name = "gpio";
	gba.gba_gc = &sc->sc_gpio_gc;
	gba.gba_pins = sc->sc_gpio_pins;
	gba.gba_npins = ah->ah_gpio_npins;

#ifdef notyet
#if NGPIO > 0
	if (config_found(&sc->sc_dev, &gba, gpiobus_print) == NULL)
		return (ENODEV);
#endif
#endif

	return (0);
}

int
ath_gpio_pin_read(void *arg, int pin)
{
	struct ath_softc *sc = arg;
	struct ath_hal *ah = sc->sc_ah;
	return (ath_hal_get_gpio(ah, pin) ? GPIO_PIN_HIGH : GPIO_PIN_LOW);
}

void
ath_gpio_pin_write(void *arg, int pin, int value)
{
	struct ath_softc *sc = arg;
	struct ath_hal *ah = sc->sc_ah;
	ath_hal_set_gpio(ah, pin, value ? GPIO_PIN_HIGH : GPIO_PIN_LOW);
}

void
ath_gpio_pin_ctl(void *arg, int pin, int flags)
{
	struct ath_softc *sc = arg;
	struct ath_hal *ah = sc->sc_ah;

	if (flags & GPIO_PIN_INPUT) {
		ath_hal_set_gpio_input(ah, pin);
	} else if (flags & GPIO_PIN_OUTPUT) {
		ath_hal_set_gpio_output(ah, pin);
	}
}
