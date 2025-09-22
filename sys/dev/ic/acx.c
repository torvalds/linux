/*	$OpenBSD: acx.c,v 1.128 2023/11/10 15:51:20 bluhm Exp $ */

/*
 * Copyright (c) 2006 Jonathan Gray <jsg@openbsd.org>
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

/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 2003-2004 wlan.kewl.org Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *    This product includes software developed by the wlan.kewl.org Project.
 *
 * 4. Neither the name of the wlan.kewl.org Project nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE wlan.kewl.org Project BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/ic/acxvar.h>
#include <dev/ic/acxreg.h>

#ifdef ACX_DEBUG
int acxdebug = 0;
#endif

int	 acx_attach(struct acx_softc *);
int	 acx_detach(void *);

int	 acx_init(struct ifnet *);
int	 acx_stop(struct acx_softc *);
void	 acx_init_info_reg(struct acx_softc *);
int	 acx_config(struct acx_softc *);
int	 acx_read_config(struct acx_softc *, struct acx_config *);
int	 acx_write_config(struct acx_softc *, struct acx_config *);
int	 acx_rx_config(struct acx_softc *);
int	 acx_set_crypt_keys(struct acx_softc *);
void	 acx_next_scan(void *);

void	 acx_start(struct ifnet *);
void	 acx_watchdog(struct ifnet *);

int	 acx_ioctl(struct ifnet *, u_long, caddr_t);

int	 acx_intr(void *);
void	 acx_disable_intr(struct acx_softc *);
void	 acx_enable_intr(struct acx_softc *);
void	 acx_txeof(struct acx_softc *);
void	 acx_txerr(struct acx_softc *, uint8_t);
void	 acx_rxeof(struct acx_softc *);

int	 acx_dma_alloc(struct acx_softc *);
void	 acx_dma_free(struct acx_softc *);
void	 acx_init_tx_ring(struct acx_softc *);
int	 acx_init_rx_ring(struct acx_softc *);
int	 acx_newbuf(struct acx_softc *, struct acx_rxbuf *, int);
int	 acx_encap(struct acx_softc *, struct acx_txbuf *,
	     struct mbuf *, struct ieee80211_node *, int);

int	 acx_reset(struct acx_softc *);

int	 acx_set_null_tmplt(struct acx_softc *);
int	 acx_set_probe_req_tmplt(struct acx_softc *, const char *, int);
#ifndef IEEE80211_STA_ONLY
int	 acx_set_probe_resp_tmplt(struct acx_softc *, struct ieee80211_node *);
int	 acx_beacon_locate(struct mbuf *, u_int8_t);
int	 acx_set_beacon_tmplt(struct acx_softc *, struct ieee80211_node *);
#endif

int	 acx_read_eeprom(struct acx_softc *, uint32_t, uint8_t *);
int	 acx_read_phyreg(struct acx_softc *, uint32_t, uint8_t *);
const char *	acx_get_rf(int);
int	 acx_get_maxrssi(int);

int	 acx_load_firmware(struct acx_softc *, uint32_t,
	     const uint8_t *, int);
int	 acx_load_radio_firmware(struct acx_softc *, const char *);
int	 acx_load_base_firmware(struct acx_softc *, const char *);

struct ieee80211_node
	*acx_node_alloc(struct ieee80211com *);
int	 acx_newstate(struct ieee80211com *, enum ieee80211_state, int);

void	 acx_init_cmd_reg(struct acx_softc *);
int	 acx_join_bss(struct acx_softc *, uint8_t, struct ieee80211_node *);
int	 acx_set_channel(struct acx_softc *, uint8_t);
int	 acx_init_radio(struct acx_softc *, uint32_t, uint32_t);

void	 acx_iter_func(void *, struct ieee80211_node *);
void	 acx_amrr_timeout(void *);
void	 acx_newassoc(struct ieee80211com *, struct ieee80211_node *, int);
#ifndef IEEE80211_STA_ONLY
void	 acx_set_tim(struct ieee80211com *, int, int);
#endif

int		acx_beacon_intvl = 100;	/* 100 TU */

/*
 * Possible values for the second parameter of acx_join_bss()
 */
#define ACX_MODE_ADHOC	0
#define ACX_MODE_UNUSED	1
#define ACX_MODE_STA	2
#define ACX_MODE_AP	3

struct cfdriver acx_cd = {
	NULL, "acx", DV_IFNET
};

int
acx_attach(struct acx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int i, error;

	/* Initialize channel scanning timer */
	timeout_set(&sc->sc_chanscan_timer, acx_next_scan, sc);

	/* Allocate busdma stuffs */
	error = acx_dma_alloc(sc);
	if (error) {
		printf("%s: attach failed, could not allocate DMA!\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	/* Reset Hardware */
	error = acx_reset(sc);
	if (error) {
		printf("%s: attach failed, could not reset device!\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	/* Disable interrupts before firmware is loaded */
	acx_disable_intr(sc);

	/* Get radio type and form factor */
#define EEINFO_RETRY_MAX	50
	for (i = 0; i < EEINFO_RETRY_MAX; ++i) {
		uint16_t ee_info;

		ee_info = CSR_READ_2(sc, ACXREG_EEPROM_INFO);
		if (ACX_EEINFO_HAS_RADIO_TYPE(ee_info)) {
			sc->sc_form_factor = ACX_EEINFO_FORM_FACTOR(ee_info);
			sc->sc_radio_type = ACX_EEINFO_RADIO_TYPE(ee_info);
			break;
		}
		DELAY(10000);
	}
	if (i == EEINFO_RETRY_MAX) {
		printf("%s: attach failed, could not get radio type!\n",
		    sc->sc_dev.dv_xname);
		return (ENXIO);
	}
#undef EEINFO_RETRY_MAX

#ifdef DUMP_EEPROM
	for (i = 0; i < 0x40; ++i) {
		uint8_t val;

		error = acx_read_eeprom(sc, i, &val);
		if (error)
			return (error);
		if (i % 10 == 0)
			printf("\n");
		printf("%02x ", val);
	}
	printf("\n");
#endif	/* DUMP_EEPROM */

	/* Get EEPROM version */
	error = acx_read_eeprom(sc, ACX_EE_VERSION_OFS, &sc->sc_eeprom_ver);
	if (error) {
		printf("%s: attach failed, could not get EEPROM version!\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	ifp->if_softc = sc;
	ifp->if_ioctl = acx_ioctl;
	ifp->if_start = acx_start;
	ifp->if_watchdog = acx_watchdog;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifq_init_maxlen(&ifp->if_snd, IFQ_MAXLEN);

	/* Set channels */
	for (i = 1; i <= 14; ++i) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags = sc->chip_chan_flags;
	}

	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;

	/*
	 * NOTE: Don't overwrite ic_caps set by chip specific code
	 */
	ic->ic_caps =
	    IEEE80211_C_WEP |			/* WEP */
	    IEEE80211_C_MONITOR |		/* Monitor mode */
#ifndef IEEE80211_STA_ONLY
	    IEEE80211_C_IBSS |			/* IBSS mode */
	    IEEE80211_C_HOSTAP |		/* Access Point */
	    IEEE80211_C_APPMGT |		/* AP Power Mgmt */
#endif
	    IEEE80211_C_SHPREAMBLE;		/* Short preamble */

	/* Get station id */
	for (i = 0; i < IEEE80211_ADDR_LEN; ++i) {
		error = acx_read_eeprom(sc, sc->chip_ee_eaddr_ofs - i,
		    &ic->ic_myaddr[i]);
		if (error) {
			printf("%s: attach failed, could not get station id\n",
			    sc->sc_dev.dv_xname);
			return error;
		}
	}

	printf("%s: %s, radio %s (0x%02x), EEPROM ver %u, address %s\n",
	    sc->sc_dev.dv_xname,
	    (sc->sc_flags & ACX_FLAG_ACX111) ? "ACX111" : "ACX100",
	    acx_get_rf(sc->sc_radio_type), sc->sc_radio_type,
	    sc->sc_eeprom_ver, ether_sprintf(ic->ic_myaddr));

	if_attach(ifp);
	ieee80211_ifattach(ifp);

	/* Override node alloc */
	ic->ic_node_alloc = acx_node_alloc;
	ic->ic_newassoc = acx_newassoc;

#ifndef IEEE80211_STA_ONLY
	/* Override set TIM */
	ic->ic_set_tim = acx_set_tim;
#endif

	/* Override newstate */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = acx_newstate;

	/* Set maximal rssi */
	ic->ic_max_rssi = acx_get_maxrssi(sc->sc_radio_type);

	ieee80211_media_init(ifp, ieee80211_media_change,
	    ieee80211_media_status);

	/* AMRR rate control */
	sc->amrr.amrr_min_success_threshold = 1;
	sc->amrr.amrr_max_success_threshold = 15;
	timeout_set(&sc->amrr_ch, acx_amrr_timeout, sc);

	sc->sc_long_retry_limit = 4;
	sc->sc_short_retry_limit = 7;
	sc->sc_msdu_lifetime = 4096;

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + 64);

	sc->sc_rxtap_len = sizeof(sc->sc_rxtapu);
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(ACX_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof(sc->sc_txtapu);
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(ACX_TX_RADIOTAP_PRESENT);
#endif

	return (0);
}

int
acx_detach(void *xsc)
{
	struct acx_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	acx_stop(sc);
	ieee80211_ifdetach(ifp);
	if_detach(ifp);

	acx_dma_free(sc);

	return (0);
}

int
acx_init(struct ifnet *ifp)
{
	struct acx_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	char fname[] = "tiacx111c16";
	int error, combined = 0;

	error = acx_stop(sc);
	if (error)
		return (EIO);

	/* enable card if possible */
	if (sc->sc_enable != NULL) {
		error = (*sc->sc_enable)(sc);
		if (error)
			return (EIO);
	}

	acx_init_tx_ring(sc);

	error = acx_init_rx_ring(sc);
	if (error) {
		printf("%s: can't initialize RX ring\n",
		    sc->sc_dev.dv_xname);
		goto back;
	}

	if (sc->sc_flags & ACX_FLAG_ACX111) {
		snprintf(fname, sizeof(fname), "tiacx111c%02X",
		    sc->sc_radio_type);
		error = acx_load_base_firmware(sc, fname);

		if (!error)
			combined = 1;
	}

	if (!combined) {
		snprintf(fname, sizeof(fname), "tiacx%s",
		    (sc->sc_flags & ACX_FLAG_ACX111) ? "111" : "100");
		error = acx_load_base_firmware(sc, fname);
	}

	if (error)
		goto back;

	/*
	 * Initialize command and information registers
	 * NOTE: This should be done after base firmware is loaded
	 */
	acx_init_cmd_reg(sc);
	acx_init_info_reg(sc);

	sc->sc_flags |= ACX_FLAG_FW_LOADED;

	if (!combined) {
		snprintf(fname, sizeof(fname), "tiacx%sr%02X",
		    (sc->sc_flags & ACX_FLAG_ACX111) ? "111" : "100",
		    sc->sc_radio_type);
		error = acx_load_radio_firmware(sc, fname);

		if (error)
			goto back;
	}

	error = sc->chip_init(sc);
	if (error)
		goto back;

	/* Get and set device various configuration */
	error = acx_config(sc);
	if (error)
		goto back;

	/* Setup crypto stuffs */
	if (sc->sc_ic.ic_flags & IEEE80211_F_WEPON) {
		error = acx_set_crypt_keys(sc);
		if (error)
			goto back;
	}

	/* Turn on power led */
	CSR_CLRB_2(sc, ACXREG_GPIO_OUT, sc->chip_gpio_pled);

	acx_enable_intr(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		/* start background scanning */
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	else
		/* in monitor mode change directly into run state */
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);

	return (0);
back:
	acx_stop(sc);
	return (error);
}

void
acx_init_info_reg(struct acx_softc *sc)
{
	sc->sc_info = CSR_READ_4(sc, ACXREG_INFO_REG_OFFSET);
	sc->sc_info_param = sc->sc_info + ACX_INFO_REG_SIZE;
}

int
acx_set_crypt_keys(struct acx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_conf_wep_txkey wep_txkey;
	int i, error, got_wk = 0;

	for (i = 0; i < IEEE80211_WEP_NKID; ++i) {
		struct ieee80211_key *k = &ic->ic_nw_keys[i];

		if (k->k_len == 0)
			continue;

		if (sc->chip_hw_crypt) {
			error = sc->chip_set_wepkey(sc, k, i);
			if (error)
				return (error);
			got_wk = 1;
		}
	}

	if (!got_wk)
		return (0);

	/* Set current WEP key index */
	wep_txkey.wep_txkey = ic->ic_wep_txkey;
	if (acx_set_conf(sc, ACX_CONF_WEP_TXKEY, &wep_txkey,
	    sizeof(wep_txkey)) != 0) {
		printf("%s: set WEP txkey failed\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}

	return (0);
}

void
acx_next_scan(void *arg)
{
	struct acx_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);
}

int
acx_stop(struct acx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct acx_buf_data *bd = &sc->sc_buf_data;
	struct acx_ring_data *rd = &sc->sc_ring_data;
	int i, error;

	sc->sc_firmware_ver = 0;
	sc->sc_hardware_id = 0;

	/* Reset hardware */
	error = acx_reset(sc);
	if (error)
		return (error);

	/* Firmware no longer functions after hardware reset */
	sc->sc_flags &= ~ACX_FLAG_FW_LOADED;

	acx_disable_intr(sc);

	/* Stop background scanning */
	timeout_del(&sc->sc_chanscan_timer);

	/* Turn off power led */
	CSR_SETB_2(sc, ACXREG_GPIO_OUT, sc->chip_gpio_pled);

	/* Free TX mbuf */
	for (i = 0; i < ACX_TX_DESC_CNT; ++i) {
		struct acx_txbuf *buf;
		struct ieee80211_node *ni;

		buf = &bd->tx_buf[i];

		if (buf->tb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, buf->tb_mbuf_dmamap);
			m_freem(buf->tb_mbuf);
			buf->tb_mbuf = NULL;
		}

		ni = (struct ieee80211_node *)buf->tb_node;
		if (ni != NULL)
			ieee80211_release_node(ic, ni);
		buf->tb_node = NULL;
	}

	/* Clear TX host descriptors */
	bzero(rd->tx_ring, ACX_TX_RING_SIZE);

	/* Free RX mbuf */
	for (i = 0; i < ACX_RX_DESC_CNT; ++i) {
		if (bd->rx_buf[i].rb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat,
			    bd->rx_buf[i].rb_mbuf_dmamap);
			m_freem(bd->rx_buf[i].rb_mbuf);
			bd->rx_buf[i].rb_mbuf = NULL;
		}
	}

	/* Clear RX host descriptors */
	bzero(rd->rx_ring, ACX_RX_RING_SIZE);

	sc->sc_txtimer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ieee80211_new_state(&sc->sc_ic, IEEE80211_S_INIT, -1);

	/* disable card if possible */
	if (sc->sc_disable != NULL)
		(*sc->sc_disable)(sc);

	return (0);
}

int
acx_config(struct acx_softc *sc)
{
	struct acx_config conf;
	int error;

	error = acx_read_config(sc, &conf);
	if (error)
		return (error);

	error = acx_write_config(sc, &conf);
	if (error)
		return (error);

	error = acx_rx_config(sc);
	if (error)
		return (error);

	if (acx_set_probe_req_tmplt(sc, "", 0) != 0) {
		printf("%s: can't set probe req template "
		    "(empty ssid)\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}

	/* XXX for PM?? */
	if (acx_set_null_tmplt(sc) != 0) {
		printf("%s: can't set null data template\n",
		    sc->sc_dev.dv_xname);
		return (ENXIO);
	}

	return (0);
}

int
acx_read_config(struct acx_softc *sc, struct acx_config *conf)
{
	struct acx_conf_regdom reg_dom;
	struct acx_conf_antenna ant;
	struct acx_conf_fwrev fw_rev;
	uint32_t fw_rev_no;
	uint8_t sen;
	int error;

	/* Get region domain */
	if (acx_get_conf(sc, ACX_CONF_REGDOM, &reg_dom, sizeof(reg_dom)) != 0) {
		printf("%s: can't get region domain\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}
	conf->regdom = reg_dom.regdom;
	DPRINTF(("%s: regdom %02x\n", sc->sc_dev.dv_xname, reg_dom.regdom));

	/* Get antenna */
	if (acx_get_conf(sc, ACX_CONF_ANTENNA, &ant, sizeof(ant)) != 0) {
		printf("%s: can't get antenna\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}
	conf->antenna = ant.antenna;
	DPRINTF(("%s: antenna %02x\n", sc->sc_dev.dv_xname, ant.antenna));

	/* Get sensitivity XXX not used */
	if (sc->sc_radio_type == ACX_RADIO_TYPE_MAXIM ||
	    sc->sc_radio_type == ACX_RADIO_TYPE_RFMD ||
	    sc->sc_radio_type == ACX_RADIO_TYPE_RALINK) {
		error = acx_read_phyreg(sc, ACXRV_PHYREG_SENSITIVITY, &sen);
		if (error) {
			printf("%s: can't get sensitivity\n",
			    sc->sc_dev.dv_xname);
			return (error);
		}
	} else
		sen = 0;
	DPRINTF(("%s: sensitivity %02x\n", sc->sc_dev.dv_xname, sen));

	/* Get firmware revision */
	if (acx_get_conf(sc, ACX_CONF_FWREV, &fw_rev, sizeof(fw_rev)) != 0) {
		printf("%s: can't get firmware revision\n",
		    sc->sc_dev.dv_xname);
		return (ENXIO);
	}

	if (strncmp(fw_rev.fw_rev, "Rev ", 4) != 0) {
		printf("%s: strange revision string -- %s\n",
		    sc->sc_dev.dv_xname, fw_rev.fw_rev);
		fw_rev_no = 0x01090407;
	} else {
		/*
		 *  01234
		 * "Rev xx.xx.xx.xx"
		 *      ^ Start from here
		 */
		fw_rev_no  = fw_rev.fw_rev[0] << 24;
		fw_rev_no |= fw_rev.fw_rev[1] << 16;
		fw_rev_no |= fw_rev.fw_rev[2] <<  8;
		fw_rev_no |= fw_rev.fw_rev[3];
	}
	sc->sc_firmware_ver = fw_rev_no;
	sc->sc_hardware_id = letoh32(fw_rev.hw_id);
	DPRINTF(("%s: fw rev %08x, hw id %08x\n",
	    sc->sc_dev.dv_xname, sc->sc_firmware_ver, sc->sc_hardware_id));

	if (sc->chip_read_config != NULL) {
		error = sc->chip_read_config(sc, conf);
		if (error)
			return (error);
	}

	return (0);
}

int
acx_write_config(struct acx_softc *sc, struct acx_config *conf)
{
	struct acx_conf_nretry_short sretry;
	struct acx_conf_nretry_long lretry;
	struct acx_conf_msdu_lifetime msdu_lifetime;
	struct acx_conf_rate_fallback rate_fb;
	struct acx_conf_antenna ant;
	struct acx_conf_regdom reg_dom;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int error;

	/* Set number of long/short retry */
	sretry.nretry = sc->sc_short_retry_limit;
	if (acx_set_conf(sc, ACX_CONF_NRETRY_SHORT, &sretry,
	    sizeof(sretry)) != 0) {
		printf("%s: can't set short retry limit\n", ifp->if_xname);
		return (ENXIO);
	}

	lretry.nretry = sc->sc_long_retry_limit;
	if (acx_set_conf(sc, ACX_CONF_NRETRY_LONG, &lretry,
	    sizeof(lretry)) != 0) {
		printf("%s: can't set long retry limit\n", ifp->if_xname);
		return (ENXIO);
	}

	/* Set MSDU lifetime */
	msdu_lifetime.lifetime = htole32(sc->sc_msdu_lifetime);
	if (acx_set_conf(sc, ACX_CONF_MSDU_LIFETIME, &msdu_lifetime,
	    sizeof(msdu_lifetime)) != 0) {
		printf("%s: can't set MSDU lifetime\n", ifp->if_xname);
		return (ENXIO);
	}

	/* Enable rate fallback */
	rate_fb.ratefb_enable = 1;
	if (acx_set_conf(sc, ACX_CONF_RATE_FALLBACK, &rate_fb,
	    sizeof(rate_fb)) != 0) {
		printf("%s: can't enable rate fallback\n", ifp->if_xname);
		return (ENXIO);
	}

	/* Set antenna */
	ant.antenna = conf->antenna;
	if (acx_set_conf(sc, ACX_CONF_ANTENNA, &ant, sizeof(ant)) != 0) {
		printf("%s: can't set antenna\n", ifp->if_xname);
		return (ENXIO);
	}

	/* Set region domain */
	reg_dom.regdom = conf->regdom;
	if (acx_set_conf(sc, ACX_CONF_REGDOM, &reg_dom, sizeof(reg_dom)) != 0) {
		printf("%s: can't set region domain\n", ifp->if_xname);
		return (ENXIO);
	}

	if (sc->chip_write_config != NULL) {
		error = sc->chip_write_config(sc, conf);
		if (error)
			return (error);
	}

	return (0);
}

int
acx_rx_config(struct acx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_conf_rxopt rx_opt;

	/* tell the RX receiver what frames we want to have */
	rx_opt.opt1 = htole16(RXOPT1_INCL_RXBUF_HDR);
	rx_opt.opt2 = htole16(
	    RXOPT2_RECV_ASSOC_REQ |
	    RXOPT2_RECV_AUTH |
	    RXOPT2_RECV_BEACON |
	    RXOPT2_RECV_CF |
	    RXOPT2_RECV_CTRL |
	    RXOPT2_RECV_DATA |
	    RXOPT2_RECV_MGMT |
	    RXOPT2_RECV_PROBE_REQ |
	    RXOPT2_RECV_PROBE_RESP |
	    RXOPT2_RECV_OTHER);

	/* in monitor mode go promiscuous */
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		rx_opt.opt1 |= RXOPT1_PROMISC;
		rx_opt.opt2 |= RXOPT2_RECV_BROKEN | RXOPT2_RECV_ACK;
	} else
		rx_opt.opt1 |= RXOPT1_FILT_FDEST;

	/* finally set the RX options */
	if (acx_set_conf(sc, ACX_CONF_RXOPT, &rx_opt, sizeof(rx_opt)) != 0) {
		printf("%s: can not set RX options!\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}

	return (0);
}

int
acx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct acx_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s, error = 0;
	uint8_t chan;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				error = acx_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				error = acx_stop(sc);
		}
		break;
	case SIOCS80211CHANNEL:
		/* allow fast channel switching in monitor mode */
		error = ieee80211_ioctl(ifp, cmd, data);
		if (error == ENETRESET &&
		    ic->ic_opmode == IEEE80211_M_MONITOR) {
			if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
			    (IFF_UP | IFF_RUNNING)) {
				ic->ic_bss->ni_chan = ic->ic_ibss_chan;
				chan = ieee80211_chan2ieee(ic,
				    ic->ic_bss->ni_chan);
				(void)acx_set_channel(sc, chan);
			}
			error = 0;
		}
		break;
	default:
		error = ieee80211_ioctl(ifp, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_RUNNING | IFF_UP)) ==
		    (IFF_RUNNING | IFF_UP))
			error = acx_init(ifp);
		else
			error = 0;
	}

	splx(s);

	return (error);
}

void
acx_start(struct ifnet *ifp)
{
	struct acx_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_buf_data *bd = &sc->sc_buf_data;
	struct acx_txbuf *buf;
	int trans, idx;

	if ((sc->sc_flags & ACX_FLAG_FW_LOADED) == 0 ||
	    (ifp->if_flags & IFF_RUNNING) == 0 ||
	    ifq_is_oactive(&ifp->if_snd))
		return;

	/*
	 * NOTE:
	 * We can't start from a random position that TX descriptor
	 * is free, since hardware will be confused by that.
	 * We have to follow the order of the TX ring.
	 */
	idx = bd->tx_free_start;
	trans = 0;
	for (buf = &bd->tx_buf[idx]; buf->tb_mbuf == NULL;
	     buf = &bd->tx_buf[idx]) {
		struct ieee80211_frame *wh;
		struct ieee80211_node *ni = NULL;
		struct mbuf *m;
		int rate;

		m = mq_dequeue(&ic->ic_mgtq);
		/* first dequeue management frames */
		if (m != NULL) {
			ni = m->m_pkthdr.ph_cookie;

			/*
			 * probe response mgmt frames are handled by the
			 * firmware already.  So, don't send them twice.
			 */
			wh = mtod(m, struct ieee80211_frame *);
			if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
			    IEEE80211_FC0_SUBTYPE_PROBE_RESP) {
				if (ni != NULL)
					ieee80211_release_node(ic, ni);
                                m_freem(m);
                                continue;
			}

			/*
			 * mgmt frames are sent at the lowest available
			 * bit-rate.
			 */
			rate = ni->ni_rates.rs_rates[0];
			rate &= IEEE80211_RATE_VAL;
		} else {
			struct ether_header *eh;

			/* then dequeue packets on the powersave queue */
			m = mq_dequeue(&ic->ic_pwrsaveq);
			if (m != NULL) {
				ni = m->m_pkthdr.ph_cookie;
				goto encapped;
			} else {
				m = ifq_dequeue(&ifp->if_snd);
				if (m == NULL)
					break;
			}
			if (ic->ic_state != IEEE80211_S_RUN) {
				DPRINTF(("%s: data packet dropped due to "
				    "not RUN.  Current state %d\n",
				    ifp->if_xname, ic->ic_state));
				m_freem(m);
				break;
			}

			if (m->m_len < sizeof(struct ether_header)) {
				m = m_pullup(m, sizeof(struct ether_header));
				if (m == NULL) {
					ifp->if_oerrors++;
					continue;
				}
			}
			eh = mtod(m, struct ether_header *);

			/* TODO power save */

#if NBPFILTER > 0
			if (ifp->if_bpf != NULL)
				bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

			if ((m = ieee80211_encap(ifp, m, &ni)) == NULL) {
				ifp->if_oerrors++;
				continue;
			}
encapped:
			if (ic->ic_fixed_rate != -1) {
				rate = ic->ic_sup_rates[ic->ic_curmode].
				    rs_rates[ic->ic_fixed_rate];
			} else
				rate = ni->ni_rates.rs_rates[ni->ni_txrate];
			rate &= IEEE80211_RATE_VAL;
		} 

#if NBPFILTER > 0
		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_OUT);
#endif

		wh = mtod(m, struct ieee80211_frame *);
		if ((wh->i_fc[1] & IEEE80211_FC1_WEP) && !sc->chip_hw_crypt) {
			struct ieee80211_key *k;

			k = ieee80211_get_txkey(ic, wh, ni);
			if ((m = ieee80211_encrypt(ic, m, k)) == NULL) {
				ieee80211_release_node(ic, ni);
				ifp->if_oerrors++;
				continue;
			}
		}

#if NBPFILTER > 0
		if (sc->sc_drvbpf != NULL) {
			struct mbuf mb;
			struct acx_tx_radiotap_hdr *tap = &sc->sc_txtap;

			tap->wt_flags = 0;
			tap->wt_rate = rate;
			tap->wt_chan_freq =
			    htole16(ic->ic_bss->ni_chan->ic_freq);
			tap->wt_chan_flags =
			    htole16(ic->ic_bss->ni_chan->ic_flags);

			mb.m_data = (caddr_t)tap;
			mb.m_len = sc->sc_txtap_len;
			mb.m_next = m;
			mb.m_nextpkt = NULL;
			mb.m_type = 0;
			mb.m_flags = 0;
			bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_OUT);
		}
#endif

		if (acx_encap(sc, buf, m, ni, rate) != 0) {
			/*
			 * NOTE: `m' will be freed in acx_encap()
			 * if we reach here.
			 */
			if (ni != NULL)
				ieee80211_release_node(ic, ni);
			ifp->if_oerrors++;
			continue;
		}

		/*
		 * NOTE:
		 * 1) `m' should not be touched after acx_encap()
		 * 2) `node' will be used to do TX rate control during
		 *    acx_txeof(), so it is not freed here.  acx_txeof()
		 *    will free it for us
		 */
		trans++;
		bd->tx_used_count++;
		idx = (idx + 1) % ACX_TX_DESC_CNT;
	}
	bd->tx_free_start = idx;

	if (bd->tx_used_count == ACX_TX_DESC_CNT)
		ifq_set_oactive(&ifp->if_snd);

	if (trans && sc->sc_txtimer == 0)
		sc->sc_txtimer = 5;
	ifp->if_timer = 1;
}

void
acx_watchdog(struct ifnet *ifp)
{
	struct acx_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	if (sc->sc_txtimer) {
		if (--sc->sc_txtimer == 0) {
			printf("%s: watchdog timeout\n", ifp->if_xname);
			acx_init(ifp);
			ifp->if_oerrors++;
			return;
		} else
			ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
acx_intr(void *arg)
{
	struct acx_softc *sc = arg;
	uint16_t intr_status;

	if ((sc->sc_flags & ACX_FLAG_FW_LOADED) == 0)
		return (0);

	intr_status = CSR_READ_2(sc, ACXREG_INTR_STATUS_CLR);
	if (intr_status == ACXRV_INTR_ALL) {
		/* not our interrupt */
		return (0);
	}

	/* Acknowledge all interrupts */
	CSR_WRITE_2(sc, ACXREG_INTR_ACK, intr_status);

	intr_status &= sc->chip_intr_enable;
	if (intr_status == 0) {
		/* not interrupts we care about */
		return (1);
	}

#ifndef IEEE80211_STA_ONLY
	if (intr_status & ACXRV_INTR_DTIM)
		ieee80211_notify_dtim(&sc->sc_ic);
#endif

	if (intr_status & ACXRV_INTR_TX_FINI)
		acx_txeof(sc);

	if (intr_status & ACXRV_INTR_RX_FINI)
		acx_rxeof(sc);

	return (1);
}

void
acx_disable_intr(struct acx_softc *sc)
{
	CSR_WRITE_2(sc, ACXREG_INTR_MASK, sc->chip_intr_disable);
	CSR_WRITE_2(sc, ACXREG_EVENT_MASK, 0);
}

void
acx_enable_intr(struct acx_softc *sc)
{
	/* Mask out interrupts that are not in the enable set */
	CSR_WRITE_2(sc, ACXREG_INTR_MASK, ~sc->chip_intr_enable);
	CSR_WRITE_2(sc, ACXREG_EVENT_MASK, ACXRV_EVENT_DISABLE);
}

void
acx_txeof(struct acx_softc *sc)
{
	struct acx_buf_data *bd;
	struct acx_txbuf *buf;
	struct ifnet *ifp;
	int idx;

	ifp = &sc->sc_ic.ic_if;

	bd = &sc->sc_buf_data;
	idx = bd->tx_used_start;
	for (buf = &bd->tx_buf[idx]; buf->tb_mbuf != NULL;
	     buf = &bd->tx_buf[idx]) {
		uint8_t ctrl, error;

		ctrl = FW_TXDESC_GETFIELD_1(sc, buf, f_tx_ctrl);
		if ((ctrl & (DESC_CTRL_HOSTOWN | DESC_CTRL_ACXDONE)) !=
		    (DESC_CTRL_HOSTOWN | DESC_CTRL_ACXDONE))
			break;

		bus_dmamap_unload(sc->sc_dmat, buf->tb_mbuf_dmamap);
		m_freem(buf->tb_mbuf);
		buf->tb_mbuf = NULL;

		error = FW_TXDESC_GETFIELD_1(sc, buf, f_tx_error);
		if (error) {
			acx_txerr(sc, error);
			ifp->if_oerrors++;
		}

		/* Update rate control statistics for the node */
		if (buf->tb_node != NULL) {
			struct ieee80211com *ic;
			struct ieee80211_node *ni;
			struct acx_node *wn;
			int ntries;

			ic = &sc->sc_ic;
			ni = (struct ieee80211_node *)buf->tb_node;
			wn = (struct acx_node *)ni;
			ntries = FW_TXDESC_GETFIELD_1(sc, buf, f_tx_rts_fail) +
			    FW_TXDESC_GETFIELD_1(sc, buf, f_tx_ack_fail);

			wn->amn.amn_txcnt++;
			if (ntries > 0) {
				DPRINTFN(2, ("%s: tx intr ntries %d\n",
				    sc->sc_dev.dv_xname, ntries));
				wn->amn.amn_retrycnt++;
			}

			ieee80211_release_node(ic, ni);
			buf->tb_node = NULL;
		}

		FW_TXDESC_SETFIELD_1(sc, buf, f_tx_ctrl, DESC_CTRL_HOSTOWN);

		bd->tx_used_count--;

		idx = (idx + 1) % ACX_TX_DESC_CNT;
	}
	bd->tx_used_start = idx;

	sc->sc_txtimer = bd->tx_used_count == 0 ? 0 : 5;

	if (bd->tx_used_count != ACX_TX_DESC_CNT) {
		ifq_clr_oactive(&ifp->if_snd);
		acx_start(ifp);
	}
}

void
acx_txerr(struct acx_softc *sc, uint8_t err)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct acx_stats *stats = &sc->sc_stats;

	if (err == DESC_ERR_EXCESSIVE_RETRY) {
		/*
		 * This a common error (see comment below),
		 * so print it using DPRINTF().
		 */
		DPRINTF(("%s: TX failed -- excessive retry\n",
		    sc->sc_dev.dv_xname));
	} else
		printf("%s: TX failed -- ", ifp->if_xname);

	/*
	 * Although `err' looks like bitmask, it never
	 * has multiple bits set.
	 */
	switch (err) {
#if 0
	case DESC_ERR_OTHER_FRAG:
		/* XXX what's this */
		printf("error in other fragment\n");
		stats->err_oth_frag++;
		break;
#endif
	case DESC_ERR_ABORT:
		printf("aborted\n");
		stats->err_abort++;
		break;
	case DESC_ERR_PARAM:
		printf("wrong parameters in descriptor\n");
		stats->err_param++;
		break;
	case DESC_ERR_NO_WEPKEY:
		printf("WEP key missing\n");
		stats->err_no_wepkey++;
		break;
	case DESC_ERR_MSDU_TIMEOUT:
		printf("MSDU life timeout\n");
		stats->err_msdu_timeout++;
		break;
	case DESC_ERR_EXCESSIVE_RETRY:
		/*
		 * Possible causes:
		 * 1) Distance is too long
		 * 2) Transmit failed (e.g. no MAC level ACK)
		 * 3) Chip overheated (this should be rare)
		 */
		stats->err_ex_retry++;
		break;
	case DESC_ERR_BUF_OVERFLOW:
		printf("buffer overflow\n");
		stats->err_buf_oflow++;
		break;
	case DESC_ERR_DMA:
		printf("DMA error\n");
		stats->err_dma++;
		break;
	default:
		printf("unknown error %d\n", err);
		stats->err_unkn++;
		break;
	}
}

void
acx_rxeof(struct acx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_ring_data *rd = &sc->sc_ring_data;
	struct acx_buf_data *bd = &sc->sc_buf_data;
	struct ifnet *ifp = &ic->ic_if;
	int idx, ready;

	bus_dmamap_sync(sc->sc_dmat, rd->rx_ring_dmamap, 0,
	    rd->rx_ring_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);

	/*
	 * Locate first "ready" rx buffer,
	 * start from last stopped position.
	 */
	idx = bd->rx_scan_start;
	ready = 0;
	do {
		struct acx_rxbuf *buf;

		buf = &bd->rx_buf[idx];
		if ((buf->rb_desc->h_ctrl & htole16(DESC_CTRL_HOSTOWN)) &&
		    (buf->rb_desc->h_status & htole32(DESC_STATUS_FULL))) {
			ready = 1;
			break;
		}
		idx = (idx + 1) % ACX_RX_DESC_CNT;
	} while (idx != bd->rx_scan_start);

	if (!ready)
		return;

	/*
	 * NOTE: don't mess up `idx' here, it will
	 * be used in the following code.
	 */
	do {
		struct acx_rxbuf_hdr *head;
		struct acx_rxbuf *buf;
		struct mbuf *m;
		struct ieee80211_rxinfo rxi;
		uint32_t desc_status;
		uint16_t desc_ctrl;
		int len, error;

		buf = &bd->rx_buf[idx];

		desc_ctrl = letoh16(buf->rb_desc->h_ctrl);
		desc_status = letoh32(buf->rb_desc->h_status);
		if (!(desc_ctrl & DESC_CTRL_HOSTOWN) ||
		    !(desc_status & DESC_STATUS_FULL))
			break;

		bus_dmamap_sync(sc->sc_dmat, buf->rb_mbuf_dmamap, 0,
		    buf->rb_mbuf_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);

		m = buf->rb_mbuf;

		error = acx_newbuf(sc, buf, 0);
		if (error) {
			ifp->if_ierrors++;
			goto next;
		}

		head = mtod(m, struct acx_rxbuf_hdr *);

		len = letoh16(head->rbh_len) & ACX_RXBUF_LEN_MASK;
		if (len >= sizeof(struct ieee80211_frame_min) &&
		    len < MCLBYTES) {
			struct ieee80211_frame *wh;
			struct ieee80211_node *ni;

			m_adj(m, sizeof(struct acx_rxbuf_hdr) +
			    sc->chip_rxbuf_exhdr);
			wh = mtod(m, struct ieee80211_frame *);

			memset(&rxi, 0, sizeof(rxi));
			if ((wh->i_fc[1] & IEEE80211_FC1_WEP) &&
			    sc->chip_hw_crypt) {
				/* Short circuit software WEP */
				wh->i_fc[1] &= ~IEEE80211_FC1_WEP;

				/* Do chip specific RX buffer processing */
				if (sc->chip_proc_wep_rxbuf != NULL) {
					sc->chip_proc_wep_rxbuf(sc, m, &len);
					wh = mtod(m, struct ieee80211_frame *);
				}
				rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
			}

			m->m_len = m->m_pkthdr.len = len;

#if NBPFILTER > 0
			if (sc->sc_drvbpf != NULL) {
				struct mbuf mb;
				struct acx_rx_radiotap_hdr *tap = &sc->sc_rxtap;

				tap->wr_flags = 0;
				tap->wr_chan_freq =
				    htole16(ic->ic_bss->ni_chan->ic_freq);
				tap->wr_chan_flags =
				    htole16(ic->ic_bss->ni_chan->ic_flags);
				tap->wr_rssi = head->rbh_level;
				tap->wr_max_rssi = ic->ic_max_rssi;

				mb.m_data = (caddr_t)tap;
				mb.m_len = sc->sc_rxtap_len;
				mb.m_next = m;
				mb.m_nextpkt = NULL;
				mb.m_type = 0;
				mb.m_flags = 0;
				bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_IN);
			}
#endif

			ni = ieee80211_find_rxnode(ic, wh);

			rxi.rxi_rssi = head->rbh_level;
			rxi.rxi_tstamp = letoh32(head->rbh_time);
			ieee80211_input(ifp, m, ni, &rxi);

			ieee80211_release_node(ic, ni);
		} else {
			m_freem(m);
			ifp->if_ierrors++;
		}

next:
		buf->rb_desc->h_ctrl = htole16(desc_ctrl & ~DESC_CTRL_HOSTOWN);
		buf->rb_desc->h_status = 0;
		bus_dmamap_sync(sc->sc_dmat, rd->rx_ring_dmamap, 0,
		    rd->rx_ring_dmamap->dm_mapsize, BUS_DMASYNC_PREWRITE);

		idx = (idx + 1) % ACX_RX_DESC_CNT;
	} while (idx != bd->rx_scan_start);

	/*
	 * Record the position so that next
	 * time we can start from it.
	 */
	bd->rx_scan_start = idx;
}

int
acx_reset(struct acx_softc *sc)
{
	uint16_t reg;

	/* Halt ECPU */
	CSR_SETB_2(sc, ACXREG_ECPU_CTRL, ACXRV_ECPU_HALT);

	/* Software reset */
	reg = CSR_READ_2(sc, ACXREG_SOFT_RESET);
	CSR_WRITE_2(sc, ACXREG_SOFT_RESET, reg | ACXRV_SOFT_RESET);
	DELAY(100);
	CSR_WRITE_2(sc, ACXREG_SOFT_RESET, reg);

	/* Initialize EEPROM */
	CSR_SETB_2(sc, ACXREG_EEPROM_INIT, ACXRV_EEPROM_INIT);
	DELAY(50000);

	/* Test whether ECPU is stopped */
	reg = CSR_READ_2(sc, ACXREG_ECPU_CTRL);
	if (!(reg & ACXRV_ECPU_HALT)) {
		printf("%s: can't halt ECPU\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}

	return (0);
}

int
acx_read_eeprom(struct acx_softc *sc, uint32_t offset, uint8_t *val)
{
	int i;

	CSR_WRITE_4(sc, ACXREG_EEPROM_CONF, 0);
	CSR_WRITE_4(sc, ACXREG_EEPROM_ADDR, offset);
	CSR_WRITE_4(sc, ACXREG_EEPROM_CTRL, ACXRV_EEPROM_READ);

#define EE_READ_RETRY_MAX	100
	for (i = 0; i < EE_READ_RETRY_MAX; ++i) {
		if (CSR_READ_2(sc, ACXREG_EEPROM_CTRL) == 0)
			break;
		DELAY(10000);
	}
	if (i == EE_READ_RETRY_MAX) {
		printf("%s: can't read EEPROM offset %x (timeout)\n",
		    sc->sc_dev.dv_xname, offset);
		return (ETIMEDOUT);
	}
#undef EE_READ_RETRY_MAX

	*val = CSR_READ_1(sc, ACXREG_EEPROM_DATA);

	return (0);
}

int
acx_read_phyreg(struct acx_softc *sc, uint32_t reg, uint8_t *val)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int i;

	CSR_WRITE_4(sc, ACXREG_PHY_ADDR, reg);
	CSR_WRITE_4(sc, ACXREG_PHY_CTRL, ACXRV_PHY_READ);

#define PHY_READ_RETRY_MAX	100
	for (i = 0; i < PHY_READ_RETRY_MAX; ++i) {
		if (CSR_READ_4(sc, ACXREG_PHY_CTRL) == 0)
			break;
		DELAY(10000);
	}
	if (i == PHY_READ_RETRY_MAX) {
		printf("%s: can't read phy reg %x (timeout)\n",
		    ifp->if_xname, reg);
		return (ETIMEDOUT);
	}
#undef PHY_READ_RETRY_MAX

	*val = CSR_READ_1(sc, ACXREG_PHY_DATA);

	return (0);
}

void
acx_write_phyreg(struct acx_softc *sc, uint32_t reg, uint8_t val)
{
	CSR_WRITE_4(sc, ACXREG_PHY_DATA, val);
	CSR_WRITE_4(sc, ACXREG_PHY_ADDR, reg);
	CSR_WRITE_4(sc, ACXREG_PHY_CTRL, ACXRV_PHY_WRITE);
}

int
acx_load_base_firmware(struct acx_softc *sc, const char *name)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int i, error;
	uint8_t *ucode;
	size_t size;

	error = loadfirmware(name, &ucode, &size);

	if (error != 0) {
		printf("%s: error %d, could not read firmware %s\n",
		    ifp->if_xname, error, name);
		return (EIO);
	}

	/* Load base firmware */
	error = acx_load_firmware(sc, 0, ucode, size);

	free(ucode, M_DEVBUF, size);

	if (error) {
		printf("%s: can't load base firmware\n", ifp->if_xname);
		return error;
	}
	DPRINTF(("%s: base firmware loaded\n", sc->sc_dev.dv_xname));

	/* Start ECPU */
	CSR_WRITE_2(sc, ACXREG_ECPU_CTRL, ACXRV_ECPU_START);

	/* Wait for ECPU to be up */
	for (i = 0; i < 500; ++i) {
		uint16_t reg;

		reg = CSR_READ_2(sc, ACXREG_INTR_STATUS);
		if (reg & ACXRV_INTR_FCS_THRESH) {
			CSR_WRITE_2(sc, ACXREG_INTR_ACK, ACXRV_INTR_FCS_THRESH);
			return (0);
		}
		DELAY(10000);
	}

	printf("%s: can't initialize ECPU (timeout)\n", ifp->if_xname);

	return (ENXIO);
}

int
acx_load_radio_firmware(struct acx_softc *sc, const char *name)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct acx_conf_mmap mem_map;
	uint32_t radio_fw_ofs;
	int error;
	uint8_t *ucode;
	size_t size;

	error = loadfirmware(name, &ucode, &size);

	if (error != 0) {
		printf("%s: error %d, could not read firmware %s\n",
		    ifp->if_xname, error, name);
		return (EIO);
	}

	/*
	 * Get the position, where base firmware is loaded, so that
	 * radio firmware can be loaded after it.
	 */
	if (acx_get_conf(sc, ACX_CONF_MMAP, &mem_map, sizeof(mem_map)) != 0) {
		free(ucode, M_DEVBUF, size);
		return (ENXIO);
	}
	radio_fw_ofs = letoh32(mem_map.code_end);

	/* Put ECPU into sleeping state, before loading radio firmware */
	if (acx_exec_command(sc, ACXCMD_SLEEP, NULL, 0, NULL, 0) != 0) {
		free(ucode, M_DEVBUF, size);
		return (ENXIO);
	}

	/* Load radio firmware */
	error = acx_load_firmware(sc, radio_fw_ofs, ucode, size);

	free(ucode, M_DEVBUF, size);

	if (error) {
		printf("%s: can't load radio firmware\n", ifp->if_xname);
		return (ENXIO);
	}
	DPRINTF(("%s: radio firmware loaded\n", sc->sc_dev.dv_xname));

	/* Wake up sleeping ECPU, after radio firmware is loaded */
	if (acx_exec_command(sc, ACXCMD_WAKEUP, NULL, 0, NULL, 0) != 0)
		return (ENXIO);

	/* Initialize radio */
	if (acx_init_radio(sc, radio_fw_ofs, size) != 0)
		return (ENXIO);

	/* Verify radio firmware's loading position */
	if (acx_get_conf(sc, ACX_CONF_MMAP, &mem_map, sizeof(mem_map)) != 0)
		return (ENXIO);

	if (letoh32(mem_map.code_end) != radio_fw_ofs + size) {
		printf("%s: loaded radio firmware position mismatch\n",
		    ifp->if_xname);
		return (ENXIO);
	}

	DPRINTF(("%s: radio firmware initialized\n", sc->sc_dev.dv_xname));

	return (0);
}

int
acx_load_firmware(struct acx_softc *sc, uint32_t offset, const uint8_t *data,
    int data_len)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	const uint32_t *fw;
	u_int32_t csum = 0;
	int i, fw_len;

	for (i = 4; i < data_len; i++)
		csum += data[i];

	fw = (const uint32_t *)data;

	if (*fw != htole32(csum)) {
		printf("%s: firmware checksum 0x%x does not match 0x%x!\n",
		    ifp->if_xname, *fw, htole32(csum));
		return (ENXIO);
	}

	/* skip csum + length */
	data += 8;
	data_len -= 8;

	fw = (const uint32_t *)data;
	fw_len = data_len / sizeof(uint32_t);

	/*
	 * LOADFW_AUTO_INC only works with some older firmware:
	 * 1) acx100's firmware
	 * 2) acx111's firmware whose rev is 0x00010011
	 */

	/* Load firmware */
	CSR_WRITE_4(sc, ACXREG_FWMEM_START, ACXRV_FWMEM_START_OP);
#ifndef LOADFW_AUTO_INC
	CSR_WRITE_4(sc, ACXREG_FWMEM_CTRL, 0);
#else
	CSR_WRITE_4(sc, ACXREG_FWMEM_CTRL, ACXRV_FWMEM_ADDR_AUTOINC);
	CSR_WRITE_4(sc, ACXREG_FWMEM_ADDR, offset);
#endif

	for (i = 0; i < fw_len; ++i) {
#ifndef LOADFW_AUTO_INC
		CSR_WRITE_4(sc, ACXREG_FWMEM_ADDR, offset + (i * 4));
#endif
		CSR_WRITE_4(sc, ACXREG_FWMEM_DATA, betoh32(fw[i]));
	}

	/* Verify firmware */
	CSR_WRITE_4(sc, ACXREG_FWMEM_START, ACXRV_FWMEM_START_OP);
#ifndef LOADFW_AUTO_INC
	CSR_WRITE_4(sc, ACXREG_FWMEM_CTRL, 0);
#else
	CSR_WRITE_4(sc, ACXREG_FWMEM_CTRL, ACXRV_FWMEM_ADDR_AUTOINC);
	CSR_WRITE_4(sc, ACXREG_FWMEM_ADDR, offset);
#endif

	for (i = 0; i < fw_len; ++i) {
		uint32_t val;

#ifndef LOADFW_AUTO_INC
		CSR_WRITE_4(sc, ACXREG_FWMEM_ADDR, offset + (i * 4));
#endif
		val = CSR_READ_4(sc, ACXREG_FWMEM_DATA);
		if (betoh32(fw[i]) != val) {
			printf("%s: firmware mismatch fw %08x  loaded %08x\n",
			    ifp->if_xname, fw[i], val);
			return (ENXIO);
		}
	}

	return (0);
}

struct ieee80211_node *
acx_node_alloc(struct ieee80211com *ic)
{
	struct acx_node *wn;

	wn = malloc(sizeof(*wn), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (wn == NULL)
		return (NULL);

	return ((struct ieee80211_node *)wn);
}

int
acx_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct acx_softc *sc = ic->ic_if.if_softc;
	struct ifnet *ifp = &ic->ic_if;
	int error = 0;

	timeout_del(&sc->amrr_ch);

	switch (nstate) {
	case IEEE80211_S_INIT:
		break;
	case IEEE80211_S_SCAN: {
			uint8_t chan;

			chan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
			if (acx_set_channel(sc, chan) != 0) {
				error = 1;
				goto back;
			}

			/* 200ms => 5 channels per second */
			timeout_add_msec(&sc->sc_chanscan_timer, 200);
		}
		break;
	case IEEE80211_S_AUTH:
		if (ic->ic_opmode == IEEE80211_M_STA) {
			struct ieee80211_node *ni;
#ifdef ACX_DEBUG
			int i;
#endif

			ni = ic->ic_bss;

			if (acx_join_bss(sc, ACX_MODE_STA, ni) != 0) {
				printf("%s: join BSS failed\n", ifp->if_xname);
				error = 1;
				goto back;
			}

			DPRINTF(("%s: join BSS\n", sc->sc_dev.dv_xname));
			if (ic->ic_state == IEEE80211_S_ASSOC) {
				DPRINTF(("%s: change from assoc to run\n",
				    sc->sc_dev.dv_xname));
				ic->ic_state = IEEE80211_S_RUN;
			}

#ifdef ACX_DEBUG
			printf("%s: AP rates: ", sc->sc_dev.dv_xname);
			for (i = 0; i < ni->ni_rates.rs_nrates; ++i)
				printf("%d ", ni->ni_rates.rs_rates[i]);
			ieee80211_print_essid(ni->ni_essid, ni->ni_esslen);
			printf(" %s\n", ether_sprintf(ni->ni_bssid));
#endif
		}
		break;
	case IEEE80211_S_RUN:
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_IBSS ||
		    ic->ic_opmode == IEEE80211_M_HOSTAP) {
			struct ieee80211_node *ni;
			uint8_t chan;

			ni = ic->ic_bss;
			chan = ieee80211_chan2ieee(ic, ni->ni_chan);

			error = 1;

			if (acx_set_channel(sc, chan) != 0)
				goto back;

			if (acx_set_beacon_tmplt(sc, ni) != 0) {
				printf("%s: set beacon template failed\n",
				    ifp->if_xname);
				goto back;
			}

			if (acx_set_probe_resp_tmplt(sc, ni) != 0) {
				printf("%s: set probe response template "
				    "failed\n", ifp->if_xname);
				goto back;
			}

			if (ic->ic_opmode == IEEE80211_M_IBSS) {
				if (acx_join_bss(sc, ACX_MODE_ADHOC, ni) != 0) {
					printf("%s: join IBSS failed\n",
					    ifp->if_xname);
					goto back;
				}
			} else {
				if (acx_join_bss(sc, ACX_MODE_AP, ni) != 0) {
					printf("%s: join HOSTAP failed\n",
					    ifp->if_xname);
					goto back;
				}
			}

			DPRINTF(("%s: join IBSS\n", sc->sc_dev.dv_xname));
			error = 0;
		}
#endif
		/* fake a join to init the tx rate */
		if (ic->ic_opmode == IEEE80211_M_STA)
			acx_newassoc(ic, ic->ic_bss, 1);

		/* start automatic rate control timer */
		if (ic->ic_fixed_rate == -1)
			timeout_add_msec(&sc->amrr_ch, 500);
		break;
	default:
		break;
	}

back:
	if (error) {
		/* XXX */
		nstate = IEEE80211_S_INIT;
		arg = -1;
	}

	return (sc->sc_newstate(ic, nstate, arg));
}

int
acx_init_tmplt_ordered(struct acx_softc *sc)
{
	union {
		struct acx_tmplt_beacon		beacon;
		struct acx_tmplt_null_data	null;
		struct acx_tmplt_probe_req	preq;
		struct acx_tmplt_probe_resp	presp;
		struct acx_tmplt_tim		tim;
	} data;

	bzero(&data, sizeof(data));
	/*
	 * NOTE:
	 * Order of templates initialization:
	 * 1) Probe request
	 * 2) NULL data
	 * 3) Beacon
	 * 4) TIM
	 * 5) Probe response
	 * Above order is critical to get a correct memory map.
	 */
	if (acx_set_tmplt(sc, ACXCMD_TMPLT_PROBE_REQ, &data.preq,
	    sizeof(data.preq)) != 0)
		return (1);

	if (acx_set_tmplt(sc, ACXCMD_TMPLT_NULL_DATA, &data.null,
	    sizeof(data.null)) != 0)
		return (1);

	if (acx_set_tmplt(sc, ACXCMD_TMPLT_BEACON, &data.beacon,
	    sizeof(data.beacon)) != 0)
		return (1);

	if (acx_set_tmplt(sc, ACXCMD_TMPLT_TIM, &data.tim,
	    sizeof(data.tim)) != 0)
		return (1);

	if (acx_set_tmplt(sc, ACXCMD_TMPLT_PROBE_RESP, &data.presp,
	    sizeof(data.presp)) != 0)
		return (1);

	return (0);
}

int
acx_dma_alloc(struct acx_softc *sc)
{
	struct acx_ring_data *rd = &sc->sc_ring_data;
	struct acx_buf_data *bd = &sc->sc_buf_data;
	int i, error, nsegs;

	/* Allocate DMA stuffs for RX descriptors  */
	error = bus_dmamap_create(sc->sc_dmat, ACX_RX_RING_SIZE, 1,
	    ACX_RX_RING_SIZE, 0, BUS_DMA_NOWAIT, &rd->rx_ring_dmamap);

	if (error) {
		printf("%s: can't create rx ring dma tag\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_alloc(sc->sc_dmat, ACX_RX_RING_SIZE, PAGE_SIZE,
	    0, &rd->rx_ring_seg, 1, &nsegs, BUS_DMA_NOWAIT);

	if (error != 0) {
		printf("%s: can't allocate rx ring dma memory\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_map(sc->sc_dmat, &rd->rx_ring_seg, nsegs,
	    ACX_RX_RING_SIZE, (caddr_t *)&rd->rx_ring,
	    BUS_DMA_NOWAIT);

	if (error != 0) {
		printf("%s: can't map rx desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamap_load(sc->sc_dmat, rd->rx_ring_dmamap,
	    rd->rx_ring, ACX_RX_RING_SIZE, NULL, BUS_DMA_WAITOK);

	if (error) {
		printf("%s: can't get rx ring dma address\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat, &rd->rx_ring_seg, 1);
		return (error);
	}

	rd->rx_ring_paddr = rd->rx_ring_dmamap->dm_segs[0].ds_addr;

	/* Allocate DMA stuffs for TX descriptors */
	error = bus_dmamap_create(sc->sc_dmat, ACX_TX_RING_SIZE, 1,
	    ACX_TX_RING_SIZE, 0, BUS_DMA_NOWAIT, &rd->tx_ring_dmamap);

	if (error) {
		printf("%s: can't create tx ring dma tag\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_alloc(sc->sc_dmat, ACX_TX_RING_SIZE, PAGE_SIZE,
	    0, &rd->tx_ring_seg, 1, &nsegs, BUS_DMA_NOWAIT);

	if (error) {
		printf("%s: can't allocate tx ring dma memory\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_map(sc->sc_dmat, &rd->tx_ring_seg, nsegs,
	    ACX_TX_RING_SIZE, (caddr_t *)&rd->tx_ring, BUS_DMA_NOWAIT);

	if (error != 0) {
		printf("%s: can't map tx desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamap_load(sc->sc_dmat, rd->tx_ring_dmamap,
	    rd->tx_ring, ACX_TX_RING_SIZE, NULL, BUS_DMA_WAITOK);

	if (error) {
		printf("%s: can't get tx ring dma address\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat, &rd->tx_ring_seg, 1);
		return (error);
	}

	rd->tx_ring_paddr = rd->tx_ring_dmamap->dm_segs[0].ds_addr;

	/* Create a spare RX DMA map */
	error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
	    0, 0, &bd->mbuf_tmp_dmamap);

	if (error) {
		printf("%s: can't create tmp mbuf dma map\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	/* Create DMA map for RX mbufs */
	for (i = 0; i < ACX_RX_DESC_CNT; ++i) {
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, 0, &bd->rx_buf[i].rb_mbuf_dmamap);
		if (error) {
			printf("%s: can't create rx mbuf dma map (%d)\n",
			    sc->sc_dev.dv_xname, i);
			return (error);
		}
		bd->rx_buf[i].rb_desc = &rd->rx_ring[i];
	}

	/* Create DMA map for TX mbufs */
	for (i = 0; i < ACX_TX_DESC_CNT; ++i) {
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, 0, &bd->tx_buf[i].tb_mbuf_dmamap);
		if (error) {
			printf("%s: can't create tx mbuf dma map (%d)\n",
			    sc->sc_dev.dv_xname, i);
			return (error);
		}
		bd->tx_buf[i].tb_desc1 = &rd->tx_ring[i * 2];
		bd->tx_buf[i].tb_desc2 = &rd->tx_ring[(i * 2) + 1];
	}

	return (0);
}

void
acx_dma_free(struct acx_softc *sc)
{
	struct acx_ring_data *rd = &sc->sc_ring_data;
	struct acx_buf_data *bd = &sc->sc_buf_data;
	int i;

	if (rd->rx_ring != NULL) {
		bus_dmamap_unload(sc->sc_dmat, rd->rx_ring_dmamap);
		bus_dmamem_free(sc->sc_dmat, &rd->rx_ring_seg, 1);
	}

	if (rd->tx_ring != NULL) {
		bus_dmamap_unload(sc->sc_dmat, rd->tx_ring_dmamap);
		bus_dmamem_free(sc->sc_dmat, &rd->tx_ring_seg, 1);
	}

	for (i = 0; i < ACX_RX_DESC_CNT; ++i) {
		if (bd->rx_buf[i].rb_desc != NULL) {
			if (bd->rx_buf[i].rb_mbuf != NULL) {
				bus_dmamap_unload(sc->sc_dmat,
				    bd->rx_buf[i].rb_mbuf_dmamap);
				m_freem(bd->rx_buf[i].rb_mbuf);
			}
			bus_dmamap_destroy(sc->sc_dmat,
			    bd->rx_buf[i].rb_mbuf_dmamap);
		}
	}

	for (i = 0; i < ACX_TX_DESC_CNT; ++i) {
		if (bd->tx_buf[i].tb_desc1 != NULL) {
			if (bd->tx_buf[i].tb_mbuf != NULL) {
				bus_dmamap_unload(sc->sc_dmat,
				    bd->tx_buf[i].tb_mbuf_dmamap);
				m_freem(bd->tx_buf[i].tb_mbuf);
			}
			bus_dmamap_destroy(sc->sc_dmat,
			    bd->tx_buf[i].tb_mbuf_dmamap);
		}
	}

	if (bd->mbuf_tmp_dmamap != NULL)
		bus_dmamap_destroy(sc->sc_dmat, bd->mbuf_tmp_dmamap);
}

void
acx_init_tx_ring(struct acx_softc *sc)
{
	struct acx_ring_data *rd;
	struct acx_buf_data *bd;
	uint32_t paddr;
	int i;

	rd = &sc->sc_ring_data;
	paddr = rd->tx_ring_paddr;
	for (i = 0; i < (ACX_TX_DESC_CNT * 2) - 1; ++i) {
		paddr += sizeof(struct acx_host_desc);

		bzero(&rd->tx_ring[i], sizeof(struct acx_host_desc));
		rd->tx_ring[i].h_ctrl = htole16(DESC_CTRL_HOSTOWN);

		if (i == (ACX_TX_DESC_CNT * 2) - 1)
			rd->tx_ring[i].h_next_desc = htole32(rd->tx_ring_paddr);
		else
			rd->tx_ring[i].h_next_desc = htole32(paddr);
	}

	bus_dmamap_sync(sc->sc_dmat, rd->tx_ring_dmamap, 0,
	    rd->tx_ring_dmamap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	bd = &sc->sc_buf_data;
	bd->tx_free_start = 0;
	bd->tx_used_start = 0;
	bd->tx_used_count = 0;
}

int
acx_init_rx_ring(struct acx_softc *sc)
{
	struct acx_ring_data *rd;
	struct acx_buf_data *bd;
	uint32_t paddr;
	int i;

	bd = &sc->sc_buf_data;
	rd = &sc->sc_ring_data;
	paddr = rd->rx_ring_paddr;

	for (i = 0; i < ACX_RX_DESC_CNT; ++i) {
		int error;

		paddr += sizeof(struct acx_host_desc);
		bzero(&rd->rx_ring[i], sizeof(struct acx_host_desc));

		error = acx_newbuf(sc, &bd->rx_buf[i], 1);
		if (error)
			return (error);

		if (i == ACX_RX_DESC_CNT - 1)
			rd->rx_ring[i].h_next_desc = htole32(rd->rx_ring_paddr);
		else
			rd->rx_ring[i].h_next_desc = htole32(paddr);
	}

	bus_dmamap_sync(sc->sc_dmat, rd->rx_ring_dmamap, 0,
	    rd->rx_ring_dmamap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	bd->rx_scan_start = 0;

	return (0);
}

int
acx_newbuf(struct acx_softc *sc, struct acx_rxbuf *rb, int wait)
{
	struct acx_buf_data *bd;
	struct mbuf *m;
	bus_dmamap_t map;
	uint32_t paddr;
	int error;

	bd = &sc->sc_buf_data;

	MGETHDR(m, wait ? M_WAITOK : M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);

	MCLGET(m, wait ? M_WAITOK : M_DONTWAIT);
	if (!(m->m_flags & M_EXT)) {
		m_freem(m);
		return (ENOBUFS);
	}

	m->m_len = m->m_pkthdr.len = MCLBYTES;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, bd->mbuf_tmp_dmamap, m,
	    wait ? BUS_DMA_WAITOK : BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		printf("%s: can't map rx mbuf %d\n",
		    sc->sc_dev.dv_xname, error);
		return (error);
	}

	/* Unload originally mapped mbuf */
	if (rb->rb_mbuf != NULL)
		bus_dmamap_unload(sc->sc_dmat, rb->rb_mbuf_dmamap);

	/* Swap this dmamap with tmp dmamap */
	map = rb->rb_mbuf_dmamap;
	rb->rb_mbuf_dmamap = bd->mbuf_tmp_dmamap;
	bd->mbuf_tmp_dmamap = map;
	paddr = rb->rb_mbuf_dmamap->dm_segs[0].ds_addr;

	rb->rb_mbuf = m;
	rb->rb_desc->h_data_paddr = htole32(paddr);
	rb->rb_desc->h_data_len = htole16(m->m_len);

	bus_dmamap_sync(sc->sc_dmat, rb->rb_mbuf_dmamap, 0,
	    rb->rb_mbuf_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);

	return (0);
}

int
acx_encap(struct acx_softc *sc, struct acx_txbuf *txbuf, struct mbuf *m,
    struct ieee80211_node *ni, int rate)
{
	struct acx_ring_data *rd = &sc->sc_ring_data;
	struct acx_node *node = (struct acx_node *)ni;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t paddr;
	uint8_t ctrl;
	int error;

	if (txbuf->tb_mbuf != NULL)
		panic("free TX buf has mbuf installed");

	if (m->m_pkthdr.len > MCLBYTES) {
		printf("%s: mbuf too big\n", ifp->if_xname);
		error = E2BIG;
		goto back;
	} else if (m->m_pkthdr.len < ACX_FRAME_HDRLEN) {
		printf("%s: mbuf too small\n", ifp->if_xname);
		error = EINVAL;
		goto back;
	}

	error = bus_dmamap_load_mbuf(sc->sc_dmat, txbuf->tb_mbuf_dmamap, m,
	    BUS_DMA_NOWAIT);

	if (error && error != EFBIG) {
		printf("%s: can't map tx mbuf1 %d\n",
		    sc->sc_dev.dv_xname, error);
		goto back;
	}

	if (error) {	/* error == EFBIG */
		/* too many fragments, linearize */
		if (m_defrag(m, M_DONTWAIT)) {
			printf("%s: can't defrag tx mbuf\n", ifp->if_xname);
			goto back;
		}
		error = bus_dmamap_load_mbuf(sc->sc_dmat,
		    txbuf->tb_mbuf_dmamap, m, BUS_DMA_NOWAIT);
		if (error) {
			printf("%s: can't map tx mbuf2 %d\n",
			    sc->sc_dev.dv_xname, error);
			goto back;
		}
	}

	error = 0;

	bus_dmamap_sync(sc->sc_dmat, txbuf->tb_mbuf_dmamap, 0,
	    txbuf->tb_mbuf_dmamap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	txbuf->tb_mbuf = m;
	txbuf->tb_node = node;
	txbuf->tb_rate = rate;

	/*
	 * TX buffers are accessed in following way:
	 * acx_fw_txdesc -> acx_host_desc -> buffer
	 *
	 * It is quite strange that acx also queries acx_host_desc next to
	 * the one we have assigned to acx_fw_txdesc even if first one's
	 * acx_host_desc.h_data_len == acx_fw_txdesc.f_tx_len
	 *
	 * So we allocate two acx_host_desc for one acx_fw_txdesc and
	 * assign the first acx_host_desc to acx_fw_txdesc
	 *
	 * For acx111
	 * host_desc1.h_data_len = buffer_len
	 * host_desc2.h_data_len = buffer_len - mac_header_len
	 *
	 * For acx100
	 * host_desc1.h_data_len = mac_header_len
	 * host_desc2.h_data_len = buffer_len - mac_header_len
	 */
	paddr = txbuf->tb_mbuf_dmamap->dm_segs[0].ds_addr;
	txbuf->tb_desc1->h_data_paddr = htole32(paddr);
	txbuf->tb_desc2->h_data_paddr = htole32(paddr + ACX_FRAME_HDRLEN);

	txbuf->tb_desc1->h_data_len =
	    htole16(sc->chip_txdesc1_len ? sc->chip_txdesc1_len
	    : m->m_pkthdr.len);
	txbuf->tb_desc2->h_data_len =
	    htole16(m->m_pkthdr.len - ACX_FRAME_HDRLEN);

	/*
	 * NOTE:
	 * We can't simply assign f_tx_ctrl, we will first read it back
	 * and change it bit by bit
	 */
	ctrl = FW_TXDESC_GETFIELD_1(sc, txbuf, f_tx_ctrl);
	ctrl |= sc->chip_fw_txdesc_ctrl; /* extra chip specific flags */
	ctrl &= ~(DESC_CTRL_HOSTOWN | DESC_CTRL_ACXDONE);

	FW_TXDESC_SETFIELD_2(sc, txbuf, f_tx_len, m->m_pkthdr.len);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_error, 0);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_ack_fail, 0);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_rts_fail, 0);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_rts_ok, 0);
	sc->chip_set_fw_txdesc_rate(sc, txbuf, rate);

	txbuf->tb_desc1->h_ctrl = 0;
	txbuf->tb_desc2->h_ctrl = 0;
	bus_dmamap_sync(sc->sc_dmat, rd->tx_ring_dmamap, 0,
	    rd->tx_ring_dmamap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_ctrl2, 0);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_ctrl, ctrl);

	/* Tell chip to inform us about TX completion */
	CSR_WRITE_2(sc, ACXREG_INTR_TRIG, ACXRV_TRIG_TX_FINI);
back:
	if (error)
		m_freem(m);

	return (error);
}

int
acx_set_null_tmplt(struct acx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_tmplt_null_data n;
	struct ieee80211_frame *wh;

	bzero(&n, sizeof(n));

	wh = &n.data;
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_DATA |
	    IEEE80211_FC0_SUBTYPE_NODATA;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	IEEE80211_ADDR_COPY(wh->i_addr1, etherbroadcastaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, etherbroadcastaddr);

	return (acx_set_tmplt(sc, ACXCMD_TMPLT_NULL_DATA, &n, sizeof(n)));
}

int
acx_set_probe_req_tmplt(struct acx_softc *sc, const char *ssid, int ssid_len)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_tmplt_probe_req req;
	struct ieee80211_frame *wh;
	struct ieee80211_rateset *rs;
	uint8_t *frm;
	int len;

	bzero(&req, sizeof(req));

	wh = &req.data.u_data.f;
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
	    IEEE80211_FC0_SUBTYPE_PROBE_REQ;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	IEEE80211_ADDR_COPY(wh->i_addr1, etherbroadcastaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, etherbroadcastaddr);

	frm = req.data.u_data.var;
	frm = ieee80211_add_ssid(frm, ssid, ssid_len);
	rs = &ic->ic_sup_rates[sc->chip_phymode];
	frm = ieee80211_add_rates(frm, rs);
	if (rs->rs_nrates > IEEE80211_RATE_SIZE)
		frm = ieee80211_add_xrates(frm, rs);
	len = frm - req.data.u_data.var;

	return (acx_set_tmplt(sc, ACXCMD_TMPLT_PROBE_REQ, &req,
	    ACX_TMPLT_PROBE_REQ_SIZ(len)));
}

#ifndef IEEE80211_STA_ONLY
struct mbuf *ieee80211_get_probe_resp(struct ieee80211com *,
    struct ieee80211_node *);

int
acx_set_probe_resp_tmplt(struct acx_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_tmplt_probe_resp resp;
	struct ieee80211_frame *wh;
	struct mbuf *m;
	int len;

	bzero(&resp, sizeof(resp));

	m = ieee80211_get_probe_resp(ic, ni);
	if (m == NULL)
		return (1);
	M_PREPEND(m, sizeof(struct ieee80211_frame), M_DONTWAIT);
	if (m == NULL)
		return (1);
	wh = mtod(m, struct ieee80211_frame *);
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
	    IEEE80211_FC0_SUBTYPE_PROBE_RESP;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	*(u_int16_t *)&wh->i_dur[0] = 0;
	IEEE80211_ADDR_COPY(wh->i_addr1, ni->ni_macaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, ni->ni_bssid);
	*(u_int16_t *)wh->i_seq = 0;

	m_copydata(m, 0, m->m_pkthdr.len, &resp.data);
	len = m->m_pkthdr.len + sizeof(resp.size);
	m_freem(m); 

	return (acx_set_tmplt(sc, ACXCMD_TMPLT_PROBE_RESP, &resp, len));
}

int
acx_beacon_locate(struct mbuf *m, u_int8_t type)
{
	int off;
	u_int8_t *frm;
	/*
	 * beacon frame format
	 *	[8] time stamp
	 *	[2] beacon interval
	 *	[2] capability information
	 *	from here on [tlv] values
	 */

	if (m->m_len != m->m_pkthdr.len)
		panic("beacon not in contiguous mbuf");

	off = sizeof(struct ieee80211_frame) + 8 + 2 + 2;
	frm = mtod(m, u_int8_t *);
	for (; off + 1 < m->m_len; off += frm[off + 1] + 2) {
		if (frm[off] == type)
			return (off);
	}
	return (-1);
}

int
acx_set_beacon_tmplt(struct acx_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_tmplt_beacon beacon;
	struct acx_tmplt_tim tim;
	struct mbuf *m;
	int len, off;

	bzero(&beacon, sizeof(beacon));
	bzero(&tim, sizeof(tim));

	m = ieee80211_beacon_alloc(ic, ni);
	if (m == NULL)
		return (1);

	off = acx_beacon_locate(m, IEEE80211_ELEMID_TIM);
	if (off < 0) {
		m_free(m);
		return (1);
	}

	m_copydata(m, 0, off, &beacon.data);
	len = off + sizeof(beacon.size);

	if (acx_set_tmplt(sc, ACXCMD_TMPLT_BEACON, &beacon, len) != 0) {
		m_freem(m);
		return (1);
	}

	len = m->m_pkthdr.len - off;
	if (len == 0) {
		/* no TIM field */
		m_freem(m);
		return (0);
	}

	m_copydata(m, off, len, &tim.data);
	len += sizeof(beacon.size);
	m_freem(m);

	return (acx_set_tmplt(sc, ACXCMD_TMPLT_TIM, &tim, len));
}
#endif	/* IEEE80211_STA_ONLY */

void
acx_init_cmd_reg(struct acx_softc *sc)
{
	sc->sc_cmd = CSR_READ_4(sc, ACXREG_CMD_REG_OFFSET);
	sc->sc_cmd_param = sc->sc_cmd + ACX_CMD_REG_SIZE;

	/* Clear command & status */
	CMD_WRITE_4(sc, 0);
}

int
acx_join_bss(struct acx_softc *sc, uint8_t mode, struct ieee80211_node *node)
{
	uint8_t bj_buf[BSS_JOIN_BUFLEN];
	struct bss_join_hdr *bj;
	int i, dtim_intvl;

	bzero(bj_buf, sizeof(bj_buf));
	bj = (struct bss_join_hdr *)bj_buf;

	for (i = 0; i < IEEE80211_ADDR_LEN; ++i)
		bj->bssid[i] = node->ni_bssid[IEEE80211_ADDR_LEN - i - 1];

	bj->beacon_intvl = htole16(acx_beacon_intvl);

	/* TODO tunable */
#ifndef IEEE80211_STA_ONLY
	if (sc->sc_ic.ic_opmode == IEEE80211_M_IBSS)
		dtim_intvl = 1;
	else
#endif
		dtim_intvl = 10;
	sc->chip_set_bss_join_param(sc, bj->chip_spec, dtim_intvl);

	bj->ndata_txrate = ACX_NDATA_TXRATE_1;
	bj->ndata_txopt = 0;
	bj->mode = mode;
	bj->channel = ieee80211_chan2ieee(&sc->sc_ic, node->ni_chan);
	bj->esslen = node->ni_esslen;
	bcopy(node->ni_essid, bj->essid, node->ni_esslen);

	DPRINTF(("%s: join BSS/IBSS on channel %d\n", sc->sc_dev.dv_xname,
	    bj->channel));
	return (acx_exec_command(sc, ACXCMD_JOIN_BSS,
	    bj, BSS_JOIN_PARAM_SIZE(bj), NULL, 0));
}

int
acx_set_channel(struct acx_softc *sc, uint8_t chan)
{
	if (acx_exec_command(sc, ACXCMD_ENABLE_TXCHAN, &chan, sizeof(chan),
	    NULL, 0) != 0) {
		DPRINTF(("%s: setting TX channel %d failed\n",
		    sc->sc_dev.dv_xname, chan));
		return (ENXIO);
	}

	if (acx_exec_command(sc, ACXCMD_ENABLE_RXCHAN, &chan, sizeof(chan),
	    NULL, 0) != 0) {
		DPRINTF(("%s: setting RX channel %d failed\n",
		    sc->sc_dev.dv_xname, chan));
		return (ENXIO);
	}

	return (0);
}

int
acx_get_conf(struct acx_softc *sc, uint16_t conf_id, void *conf,
    uint16_t conf_len)
{
	struct acx_conf *confcom;

	if (conf_len < sizeof(*confcom)) {
		printf("%s: %s configure data is too short\n",
		    sc->sc_dev.dv_xname, __func__);
		return (1);
	}

	confcom = conf;
	confcom->conf_id = htole16(conf_id);
	confcom->conf_data_len = htole16(conf_len - sizeof(*confcom));

	return (acx_exec_command(sc, ACXCMD_GET_CONF, confcom, sizeof(*confcom),
	    conf, conf_len));
}

int
acx_set_conf(struct acx_softc *sc, uint16_t conf_id, void *conf,
    uint16_t conf_len)
{
	struct acx_conf *confcom;

	if (conf_len < sizeof(*confcom)) {
		printf("%s: %s configure data is too short\n",
		    sc->sc_dev.dv_xname, __func__);
		return (1);
	}

	confcom = conf;
	confcom->conf_id = htole16(conf_id);
	confcom->conf_data_len = htole16(conf_len - sizeof(*confcom));

	return (acx_exec_command(sc, ACXCMD_SET_CONF, conf, conf_len, NULL, 0));
}

int
acx_set_tmplt(struct acx_softc *sc, uint16_t cmd, void *tmplt,
    uint16_t tmplt_len)
{
	uint16_t *size;

	if (tmplt_len < sizeof(*size)) {
		printf("%s: %s template is too short\n",
		    sc->sc_dev.dv_xname, __func__);
		return (1);
	}

	size = tmplt;
	*size = htole16(tmplt_len - sizeof(*size));

	return (acx_exec_command(sc, cmd, tmplt, tmplt_len, NULL, 0));
}

int
acx_init_radio(struct acx_softc *sc, uint32_t radio_ofs, uint32_t radio_len)
{
	struct radio_init r;

	r.radio_ofs = htole32(radio_ofs);
	r.radio_len = htole32(radio_len);

	return (acx_exec_command(sc, ACXCMD_INIT_RADIO, &r, sizeof(r), NULL,
	    0));
}

int
acx_exec_command(struct acx_softc *sc, uint16_t cmd, void *param,
    uint16_t param_len, void *result, uint16_t result_len)
{
	uint16_t status;
	int i, ret;

	if ((sc->sc_flags & ACX_FLAG_FW_LOADED) == 0) {
		printf("%s: cmd 0x%04x failed (base firmware not loaded)\n",
		    sc->sc_dev.dv_xname, cmd);
		return (1);
	}

	ret = 0;

	if (param != NULL && param_len != 0) {
		/* Set command param */
		CMDPRM_WRITE_REGION_1(sc, param, param_len);
	}

	/* Set command */
	CMD_WRITE_4(sc, cmd);

	/* Exec command */
	CSR_WRITE_2(sc, ACXREG_INTR_TRIG, ACXRV_TRIG_CMD_FINI);
	DELAY(50);

	/* Wait for command to complete */
	if (cmd == ACXCMD_INIT_RADIO) {
		/* radio initialization is extremely long */
		tsleep_nsec(&cmd, 0, "rdinit", MSEC_TO_NSEC(300));
	}

#define CMDWAIT_RETRY_MAX	1000
	for (i = 0; i < CMDWAIT_RETRY_MAX; ++i) {
		uint16_t reg;

		reg = CSR_READ_2(sc, ACXREG_INTR_STATUS);
		if (reg & ACXRV_INTR_CMD_FINI) {
			CSR_WRITE_2(sc, ACXREG_INTR_ACK, ACXRV_INTR_CMD_FINI);
			break;
		}
		DELAY(50);
	}
	if (i == CMDWAIT_RETRY_MAX) {
		printf("%s: cmd %04x failed (timeout)\n",
		    sc->sc_dev.dv_xname, cmd);
		ret = 1;
		goto back;
	}
#undef CMDWAIT_RETRY_MAX

	/* Get command exec status */
	status = (CMD_READ_4(sc) >> ACX_CMD_STATUS_SHIFT);
	if (status != ACX_CMD_STATUS_OK) {
		DPRINTF(("%s: cmd %04x failed\n", sc->sc_dev.dv_xname, cmd));
		ret = 1;
		goto back;
	}

	if (result != NULL && result_len != 0) {
		/* Get command result */
		CMDPRM_READ_REGION_1(sc, result, result_len);
	}

back:
	CMD_WRITE_4(sc, 0);

	return (ret);
}

const char *
acx_get_rf(int rev)
{
	switch (rev) {
	case ACX_RADIO_TYPE_MAXIM:	return "MAX2820";
	case ACX_RADIO_TYPE_RFMD:	return "RFMD";
	case ACX_RADIO_TYPE_RALINK:	return "Ralink";
	case ACX_RADIO_TYPE_RADIA:	return "Radia";
	default:			return "unknown";
	}
}

int
acx_get_maxrssi(int radio)
{
	switch (radio) {
	case ACX_RADIO_TYPE_MAXIM:	return ACX_RADIO_RSSI_MAXIM;
	case ACX_RADIO_TYPE_RFMD:	return ACX_RADIO_RSSI_RFMD;
	case ACX_RADIO_TYPE_RALINK:	return ACX_RADIO_RSSI_RALINK;
	case ACX_RADIO_TYPE_RADIA:	return ACX_RADIO_RSSI_RADIA;
	default:			return ACX_RADIO_RSSI_UNKN;
	}
}

void
acx_iter_func(void *arg, struct ieee80211_node *ni)
{
	struct acx_softc *sc = arg;
	struct acx_node *wn = (struct acx_node *)ni;

	ieee80211_amrr_choose(&sc->amrr, ni, &wn->amn);
}

void
acx_amrr_timeout(void *arg)
{
	struct acx_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_opmode == IEEE80211_M_STA)
		acx_iter_func(sc, ic->ic_bss);
	else
		ieee80211_iterate_nodes(ic, acx_iter_func, sc);

	timeout_add_msec(&sc->amrr_ch, 500);
}

void
acx_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	struct acx_softc *sc = ic->ic_if.if_softc;
	int i;

	ieee80211_amrr_node_init(&sc->amrr, &((struct acx_node *)ni)->amn);

	/* set rate to some reasonable initial value */
	for (i = ni->ni_rates.rs_nrates - 1;
	    i > 0 && (ni->ni_rates.rs_rates[i] & IEEE80211_RATE_VAL) > 72;
	    i--);
	ni->ni_txrate = i;
}

#ifndef IEEE80211_STA_ONLY
void
acx_set_tim(struct ieee80211com *ic, int aid, int set)
{
	struct acx_softc *sc = ic->ic_if.if_softc;
	struct acx_tmplt_tim tim;
	u_int8_t *ep;

	ieee80211_set_tim(ic, aid, set);

	bzero(&tim, sizeof(tim));
	ep = ieee80211_add_tim(tim.data.u_mem, ic);

	acx_set_tmplt(sc, ACXCMD_TMPLT_TIM, &tim, ep - (u_int8_t *)&tim);
}
#endif
