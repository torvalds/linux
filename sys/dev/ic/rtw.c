/*	$OpenBSD: rtw.c,v 1.103 2022/04/21 21:03:02 stsp Exp $	*/
/*	$NetBSD: rtw.c,v 1.29 2004/12/27 19:49:16 dyoung Exp $ */

/*-
 * Copyright (c) 2004, 2005 David Young.  All rights reserved.
 *
 * Programmed for NetBSD by David Young.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of David Young may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY David Young ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL David
 * Young BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */
/*
 * Device driver for the Realtek RTL8180 802.11 MAC/BBP.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/intr.h>	/* splnet */

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/ic/rtwreg.h>
#include <dev/ic/rtwvar.h>
#include <dev/ic/max2820reg.h>
#include <dev/ic/sa2400reg.h>
#include <dev/ic/si4136reg.h>
#include <dev/ic/rtl8225reg.h>
#include <dev/ic/smc93cx6var.h>

int rtw_rfprog_fallback = 0;
int rtw_do_chip_reset = 0;
int rtw_dwelltime = 200;	/* milliseconds per channel */
int rtw_macbangbits_timeout = 100;

#ifdef RTW_DEBUG
int rtw_debug = 0;
int rtw_rxbufs_limit = RTW_RXQLEN;
#endif /* RTW_DEBUG */

void	 rtw_start(struct ifnet *);
void	 rtw_txdesc_blk_init_all(struct rtw_txdesc_blk *);
void	 rtw_txsoft_blk_init_all(struct rtw_txsoft_blk *);
void	 rtw_txdesc_blk_init(struct rtw_txdesc_blk *);
void	 rtw_txdescs_sync(struct rtw_txdesc_blk *, u_int, u_int, int);
void	 rtw_txring_fixup(struct rtw_softc *);
void	 rtw_rxbufs_release(bus_dma_tag_t, struct rtw_rxsoft *);
void	 rtw_rxdesc_init(struct rtw_rxdesc_blk *, struct rtw_rxsoft *, int, int);
void	 rtw_rxring_fixup(struct rtw_softc *);
void	 rtw_io_enable(struct rtw_regs *, u_int8_t, int);
void	 rtw_intr_rx(struct rtw_softc *, u_int16_t);
#ifndef IEEE80211_STA_ONLY
void	 rtw_intr_beacon(struct rtw_softc *, u_int16_t);
void	 rtw_intr_atim(struct rtw_softc *);
#endif
void	 rtw_transmit_config(struct rtw_softc *);
void	 rtw_pktfilt_load(struct rtw_softc *);
void	 rtw_start(struct ifnet *);
void	 rtw_watchdog(struct ifnet *);
void	 rtw_next_scan(void *);
#ifndef IEEE80211_STA_ONLY
void	 rtw_recv_mgmt(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, struct ieee80211_rxinfo *, int);
#endif
struct ieee80211_node *rtw_node_alloc(struct ieee80211com *);
void	 rtw_node_free(struct ieee80211com *, struct ieee80211_node *);
void	 rtw_media_status(struct ifnet *, struct ifmediareq *);
void	 rtw_txsoft_blk_cleanup_all(struct rtw_softc *);
void	 rtw_txdesc_blk_setup(struct rtw_txdesc_blk *, struct rtw_txdesc *,
	    u_int, bus_addr_t, bus_addr_t);
void	 rtw_txdesc_blk_setup_all(struct rtw_softc *);
void	 rtw_intr_tx(struct rtw_softc *, u_int16_t);
void	 rtw_intr_ioerror(struct rtw_softc *, u_int16_t);
void	 rtw_intr_timeout(struct rtw_softc *);
void	 rtw_stop(struct ifnet *, int);
void	 rtw_maxim_pwrstate(struct rtw_regs *, enum rtw_pwrstate, int, int);
void	 rtw_philips_pwrstate(struct rtw_regs *, enum rtw_pwrstate, int, int);
void	 rtw_rtl_pwrstate(struct rtw_regs *, enum rtw_pwrstate, int, int);
void	 rtw_pwrstate0(struct rtw_softc *, enum rtw_pwrstate, int, int);
void	 rtw_join_bss(struct rtw_softc *, u_int8_t *, u_int16_t);
void	 rtw_set_access1(struct rtw_regs *, enum rtw_access);
int	 rtw_srom_parse(struct rtw_softc *);
int	 rtw_srom_read(struct rtw_regs *, u_int32_t, struct rtw_srom *,
	    const char *);
void	 rtw_set_rfprog(struct rtw_regs *, int, const char *);
u_int8_t rtw_chan2txpower(struct rtw_srom *, struct ieee80211com *,
	    struct ieee80211_channel *);
int	 rtw_txsoft_blk_init(struct rtw_txsoft_blk *);
int	 rtw_rxsoft_init_all(bus_dma_tag_t, struct rtw_rxsoft *,
	    int *, const char *);
void	 rtw_txsoft_release(bus_dma_tag_t, struct ieee80211com *,
	    struct rtw_txsoft *);
void	 rtw_txsofts_release(bus_dma_tag_t, struct ieee80211com *,
	    struct rtw_txsoft_blk *);
void	 rtw_hwring_setup(struct rtw_softc *);
int	 rtw_swring_setup(struct rtw_softc *);
void	 rtw_txdescs_reset(struct rtw_softc *);
void	 rtw_rfmd_pwrstate(struct rtw_regs *, enum rtw_pwrstate, int, int);
int	 rtw_pwrstate(struct rtw_softc *, enum rtw_pwrstate);
int	 rtw_tune(struct rtw_softc *);
void	 rtw_set_nettype(struct rtw_softc *, enum ieee80211_opmode);
int	 rtw_compute_duration1(int, int, uint32_t, int, struct rtw_duration *);
int	 rtw_compute_duration(struct ieee80211_frame *, int, uint32_t, int,
	    int, struct rtw_duration *, struct rtw_duration *, int *, int);
int	 rtw_init(struct ifnet *);
int	 rtw_ioctl(struct ifnet *, u_long, caddr_t);
int	 rtw_seg_too_short(bus_dmamap_t);
struct mbuf *rtw_dmamap_load_txbuf(bus_dma_tag_t, bus_dmamap_t, struct mbuf *,
	    u_int, short *, const char *);
int	 rtw_newstate(struct ieee80211com *, enum ieee80211_state, int);
int	 rtw_media_change(struct ifnet *);
int	 rtw_txsoft_blk_setup_all(struct rtw_softc *);
int	 rtw_rf_attach(struct rtw_softc *, int);
u_int8_t rtw_check_phydelay(struct rtw_regs *, u_int32_t);
int	 rtw_chip_reset1(struct rtw_regs *, const char *);
int	 rtw_chip_reset(struct rtw_regs *, const char *);
int	 rtw_recall_eeprom(struct rtw_regs *, const char *);
int	 rtw_reset(struct rtw_softc *);
void	 rtw_reset_oactive(struct rtw_softc *);
int	 rtw_txdesc_dmamaps_create(bus_dma_tag_t, struct rtw_txsoft *, u_int);
int	 rtw_rxdesc_dmamaps_create(bus_dma_tag_t, struct rtw_rxsoft *, u_int);
void	 rtw_rxdesc_dmamaps_destroy(bus_dma_tag_t, struct rtw_rxsoft *, u_int);
void	 rtw_txdesc_dmamaps_destroy(bus_dma_tag_t, struct rtw_txsoft *, u_int);
void	 rtw_identify_country(struct rtw_regs *, enum rtw_locale *);
int	 rtw_identify_sta(struct rtw_regs *, u_int8_t (*)[], const char *);
void	 rtw_rxdescs_sync(struct rtw_rxdesc_blk *, int, int, int);
int	 rtw_rxsoft_alloc(bus_dma_tag_t, struct rtw_rxsoft *);
void	 rtw_collect_txpkt(struct rtw_softc *, struct rtw_txdesc_blk *,
	    struct rtw_txsoft *, int);
void	 rtw_collect_txring(struct rtw_softc *, struct rtw_txsoft_blk *,
	    struct rtw_txdesc_blk *, int);
void	 rtw_suspend_ticks(struct rtw_softc *);
void	 rtw_resume_ticks(struct rtw_softc *);
void	 rtw_enable_interrupts(struct rtw_softc *);
int	 rtw_dequeue(struct ifnet *, struct rtw_txsoft_blk **,
	    struct rtw_txdesc_blk **, struct mbuf **,
	    struct ieee80211_node **);
int	 rtw_txsoft_blk_setup(struct rtw_txsoft_blk *, u_int);
void	 rtw_rxdesc_init_all(struct rtw_rxdesc_blk *, struct rtw_rxsoft *,
	    int);
int	 rtw_txring_choose(struct rtw_softc *, struct rtw_txsoft_blk **,
	    struct rtw_txdesc_blk **, int);
u_int	 rtw_txring_next(struct rtw_regs *, struct rtw_txdesc_blk *);
struct mbuf *rtw_80211_dequeue(struct rtw_softc *, struct mbuf_queue *, int,
	    struct rtw_txsoft_blk **, struct rtw_txdesc_blk **,
	    struct ieee80211_node **);
uint64_t rtw_tsf_extend(struct rtw_regs *, u_int32_t);
#ifndef IEEE80211_STA_ONLY
void	 rtw_ibss_merge(struct rtw_softc *, struct ieee80211_node *,
	    u_int32_t);
#endif
void	 rtw_idle(struct rtw_regs *);
void	 rtw_led_attach(struct rtw_led_state *, void *);
void	 rtw_led_init(struct rtw_regs *);
void	 rtw_led_slowblink(void *);
void	 rtw_led_fastblink(void *);
void	 rtw_led_set(struct rtw_led_state *, struct rtw_regs *, u_int);
void	 rtw_led_newstate(struct rtw_softc *, enum ieee80211_state);

int	 rtw_phy_init(struct rtw_softc *);
int	 rtw_bbp_preinit(struct rtw_regs *, u_int, int, u_int);
int	 rtw_bbp_init(struct rtw_regs *, struct rtw_bbpset *, int,
	    int, u_int8_t, u_int);
void	 rtw_verify_syna(u_int, u_int32_t);
int	 rtw_sa2400_pwrstate(struct rtw_softc *, enum rtw_pwrstate);
int	 rtw_sa2400_txpower(struct rtw_softc *, u_int8_t);
int	 rtw_sa2400_tune(struct rtw_softc *, u_int);
int	 rtw_sa2400_vcocal_start(struct rtw_softc *, int);
int	 rtw_sa2400_vco_calibration(struct rtw_softc *);
int	 rtw_sa2400_filter_calibration(struct rtw_softc *);
int	 rtw_sa2400_dc_calibration(struct rtw_softc *);
int	 rtw_sa2400_calibrate(struct rtw_softc *, u_int);
int	 rtw_sa2400_init(struct rtw_softc *, u_int, u_int8_t,
	    enum rtw_pwrstate);
int	 rtw_max2820_pwrstate(struct rtw_softc *, enum rtw_pwrstate);
int	 rtw_max2820_init(struct rtw_softc *, u_int, u_int8_t,
	    enum rtw_pwrstate);
int	 rtw_max2820_txpower(struct rtw_softc *, u_int8_t);
int	 rtw_max2820_tune(struct rtw_softc *, u_int);
int	 rtw_rtl8225_pwrstate(struct rtw_softc *, enum rtw_pwrstate);
int	 rtw_rtl8225_init(struct rtw_softc *, u_int, u_int8_t,
	    enum rtw_pwrstate);
int	 rtw_rtl8225_txpower(struct rtw_softc *, u_int8_t);
int	 rtw_rtl8225_tune(struct rtw_softc *, u_int);
int	 rtw_rtl8255_pwrstate(struct rtw_softc *, enum rtw_pwrstate);
int	 rtw_rtl8255_init(struct rtw_softc *, u_int, u_int8_t,
	    enum rtw_pwrstate);
int	 rtw_rtl8255_txpower(struct rtw_softc *, u_int8_t);
int	 rtw_rtl8255_tune(struct rtw_softc *, u_int);
int	 rtw_grf5101_pwrstate(struct rtw_softc *, enum rtw_pwrstate);
int	 rtw_grf5101_init(struct rtw_softc *, u_int, u_int8_t,
	    enum rtw_pwrstate);
int	 rtw_grf5101_txpower(struct rtw_softc *, u_int8_t);
int	 rtw_grf5101_tune(struct rtw_softc *, u_int);
int	 rtw_rf_hostwrite(struct rtw_softc *, u_int, u_int32_t);
int	 rtw_rf_macwrite(struct rtw_softc *, u_int, u_int32_t);
int	 rtw_bbp_write(struct rtw_regs *, u_int, u_int);
u_int32_t rtw_grf5101_host_crypt(u_int, u_int32_t);
u_int32_t rtw_maxim_swizzle(u_int, uint32_t);
u_int32_t rtw_grf5101_mac_crypt(u_int, u_int32_t);
void	 rtw_rf_hostbangbits(struct rtw_regs *, u_int32_t, int, u_int);
void	 rtw_rf_rtl8225_hostbangbits(struct rtw_regs *, u_int32_t, int, u_int);
int	 rtw_rf_macbangbits(struct rtw_regs *, u_int32_t);

u_int8_t rtw_read8(void *, u_int32_t);
u_int16_t rtw_read16(void *, u_int32_t);
u_int32_t rtw_read32(void *, u_int32_t);
void	 rtw_write8(void *, u_int32_t, u_int8_t);
void	 rtw_write16(void *, u_int32_t, u_int16_t);
void	 rtw_write32(void *, u_int32_t, u_int32_t);
void	 rtw_barrier(void *, u_int32_t, u_int32_t, int);

#ifdef RTW_DEBUG
void	 rtw_print_txdesc(struct rtw_softc *, const char *,
	    struct rtw_txsoft *, struct rtw_txdesc_blk *, int);
const char *rtw_access_string(enum rtw_access);
void	 rtw_dump_rings(struct rtw_softc *);
void	 rtw_print_txdesc(struct rtw_softc *, const char *,
	    struct rtw_txsoft *, struct rtw_txdesc_blk *, int);
#endif

struct cfdriver rtw_cd = {
	NULL, "rtw", DV_IFNET
};

void
rtw_continuous_tx_enable(struct rtw_softc *sc, int enable)
{
	struct rtw_regs *regs = &sc->sc_regs;

	u_int32_t tcr;
	tcr = RTW_READ(regs, RTW_TCR);
	tcr &= ~RTW_TCR_LBK_MASK;
	if (enable)
		tcr |= RTW_TCR_LBK_CONT;
	else
		tcr |= RTW_TCR_LBK_NORMAL;
	RTW_WRITE(regs, RTW_TCR, tcr);
	RTW_SYNC(regs, RTW_TCR, RTW_TCR);
	rtw_set_access(regs, RTW_ACCESS_ANAPARM);
	rtw_txdac_enable(sc, !enable);
	rtw_set_access(regs, RTW_ACCESS_ANAPARM);/* XXX Voodoo from Linux. */
	rtw_set_access(regs, RTW_ACCESS_NONE);
}

#ifdef RTW_DEBUG
const char *
rtw_access_string(enum rtw_access access)
{
	switch (access) {
	case RTW_ACCESS_NONE:
		return "none";
	case RTW_ACCESS_CONFIG:
		return "config";
	case RTW_ACCESS_ANAPARM:
		return "anaparm";
	default:
		return "unknown";
	}
}
#endif

void
rtw_set_access1(struct rtw_regs *regs, enum rtw_access naccess)
{
	KASSERT(naccess >= RTW_ACCESS_NONE && naccess <= RTW_ACCESS_ANAPARM);
	KASSERT(regs->r_access >= RTW_ACCESS_NONE &&
	    regs->r_access <= RTW_ACCESS_ANAPARM);

	if (naccess == regs->r_access)
		return;

	switch (naccess) {
	case RTW_ACCESS_NONE:
		switch (regs->r_access) {
		case RTW_ACCESS_ANAPARM:
			rtw_anaparm_enable(regs, 0);
			/*FALLTHROUGH*/
		case RTW_ACCESS_CONFIG:
			rtw_config0123_enable(regs, 0);
			/*FALLTHROUGH*/
		case RTW_ACCESS_NONE:
			break;
		}
		break;
	case RTW_ACCESS_CONFIG:
		switch (regs->r_access) {
		case RTW_ACCESS_NONE:
			rtw_config0123_enable(regs, 1);
			/*FALLTHROUGH*/
		case RTW_ACCESS_CONFIG:
			break;
		case RTW_ACCESS_ANAPARM:
			rtw_anaparm_enable(regs, 0);
			break;
		}
		break;
	case RTW_ACCESS_ANAPARM:
		switch (regs->r_access) {
		case RTW_ACCESS_NONE:
			rtw_config0123_enable(regs, 1);
			/*FALLTHROUGH*/
		case RTW_ACCESS_CONFIG:
			rtw_anaparm_enable(regs, 1);
			/*FALLTHROUGH*/
		case RTW_ACCESS_ANAPARM:
			break;
		}
		break;
	}
}

void
rtw_set_access(struct rtw_regs *regs, enum rtw_access access)
{
	rtw_set_access1(regs, access);
	RTW_DPRINTF(RTW_DEBUG_ACCESS,
	    ("%s: access %s -> %s\n",__func__,
	    rtw_access_string(regs->r_access),
	    rtw_access_string(access)));
	regs->r_access = access;
}

/*
 * Enable registers, switch register banks.
 */
void
rtw_config0123_enable(struct rtw_regs *regs, int enable)
{
	u_int8_t ecr;
	ecr = RTW_READ8(regs, RTW_9346CR);
	ecr &= ~(RTW_9346CR_EEM_MASK | RTW_9346CR_EECS | RTW_9346CR_EESK);
	if (enable)
		ecr |= RTW_9346CR_EEM_CONFIG;
	else {
		RTW_WBW(regs, RTW_9346CR, MAX(RTW_CONFIG0, RTW_CONFIG3));
		ecr |= RTW_9346CR_EEM_NORMAL;
	}
	RTW_WRITE8(regs, RTW_9346CR, ecr);
	RTW_SYNC(regs, RTW_9346CR, RTW_9346CR);
}

/* requires rtw_config0123_enable(, 1) */
void
rtw_anaparm_enable(struct rtw_regs *regs, int enable)
{
	u_int8_t cfg3;

	cfg3 = RTW_READ8(regs, RTW_CONFIG3);
	cfg3 |= RTW_CONFIG3_CLKRUNEN;
	if (enable)
		cfg3 |= RTW_CONFIG3_PARMEN;
	else
		cfg3 &= ~RTW_CONFIG3_PARMEN;
	RTW_WRITE8(regs, RTW_CONFIG3, cfg3);
	RTW_SYNC(regs, RTW_CONFIG3, RTW_CONFIG3);
}

/* requires rtw_anaparm_enable(, 1) */
void
rtw_txdac_enable(struct rtw_softc *sc, int enable)
{
	u_int32_t anaparm;
	struct rtw_regs *regs = &sc->sc_regs;

	anaparm = RTW_READ(regs, RTW_ANAPARM_0);
	if (enable)
		anaparm &= ~RTW_ANAPARM_TXDACOFF;
	else
		anaparm |= RTW_ANAPARM_TXDACOFF;
	RTW_WRITE(regs, RTW_ANAPARM_0, anaparm);
	RTW_SYNC(regs, RTW_ANAPARM_0, RTW_ANAPARM_0);
}

int
rtw_chip_reset1(struct rtw_regs *regs, const char *dvname)
{
	u_int8_t cr;
	int i;

	RTW_WRITE8(regs, RTW_CR, RTW_CR_RST);

	RTW_WBR(regs, RTW_CR, RTW_CR);

	for (i = 0; i < 1000; i++) {
		if ((cr = RTW_READ8(regs, RTW_CR) & RTW_CR_RST) == 0) {
			RTW_DPRINTF(RTW_DEBUG_RESET,
			    ("%s: reset in %dus\n", dvname, i));
			return 0;
		}
		RTW_RBR(regs, RTW_CR, RTW_CR);
		DELAY(10); /* 10us */
	}

	printf("\n%s: reset failed\n", dvname);
	return ETIMEDOUT;
}

int
rtw_chip_reset(struct rtw_regs *regs, const char *dvname)
{
	uint32_t tcr;

	/* from Linux driver */
	tcr = RTW_TCR_CWMIN | RTW_TCR_MXDMA_2048 |
	    LSHIFT(7, RTW_TCR_SRL_MASK) | LSHIFT(7, RTW_TCR_LRL_MASK);

	RTW_WRITE(regs, RTW_TCR, tcr);

	RTW_WBW(regs, RTW_CR, RTW_TCR);

	return rtw_chip_reset1(regs, dvname);
}

int
rtw_recall_eeprom(struct rtw_regs *regs, const char *dvname)
{
	int i;
	u_int8_t ecr;

	ecr = RTW_READ8(regs, RTW_9346CR);
	ecr = (ecr & ~RTW_9346CR_EEM_MASK) | RTW_9346CR_EEM_AUTOLOAD;
	RTW_WRITE8(regs, RTW_9346CR, ecr);

	RTW_WBR(regs, RTW_9346CR, RTW_9346CR);

	/* wait 10ms for completion */
	for (i = 0; i < 50; i++) {
		ecr = RTW_READ8(regs, RTW_9346CR);
		if ((ecr & RTW_9346CR_EEM_MASK) == RTW_9346CR_EEM_NORMAL) {
			RTW_DPRINTF(RTW_DEBUG_RESET,
			    ("%s: recall EEPROM in %dus\n", dvname, i * 200));
			return (0);
		}
		RTW_RBR(regs, RTW_9346CR, RTW_9346CR);
		DELAY(200);
	}

	printf("\n%s: could not recall EEPROM in %dus\n", dvname, i * 200);

	return (ETIMEDOUT);
}

int
rtw_reset(struct rtw_softc *sc)
{
	int rc;
	uint8_t config1;

	if ((rc = rtw_chip_reset(&sc->sc_regs, sc->sc_dev.dv_xname)) != 0)
		return rc;

	if ((rc = rtw_recall_eeprom(&sc->sc_regs, sc->sc_dev.dv_xname)) != 0)
		;

	config1 = RTW_READ8(&sc->sc_regs, RTW_CONFIG1);
	RTW_WRITE8(&sc->sc_regs, RTW_CONFIG1, config1 & ~RTW_CONFIG1_PMEN);
	/* TBD turn off maximum power saving? */

	return 0;
}

int
rtw_txdesc_dmamaps_create(bus_dma_tag_t dmat, struct rtw_txsoft *descs,
    u_int ndescs)
{
	int i, rc = 0;
	for (i = 0; i < ndescs; i++) {
		rc = bus_dmamap_create(dmat, MCLBYTES, RTW_MAXPKTSEGS, MCLBYTES,
		    0, 0, &descs[i].ts_dmamap);
		if (rc != 0)
			break;
	}
	return rc;
}

int
rtw_rxdesc_dmamaps_create(bus_dma_tag_t dmat, struct rtw_rxsoft *descs,
    u_int ndescs)
{
	int i, rc = 0;
	for (i = 0; i < ndescs; i++) {
		rc = bus_dmamap_create(dmat, MCLBYTES, 1, MCLBYTES, 0, 0,
		    &descs[i].rs_dmamap);
		if (rc != 0)
			break;
	}
	return rc;
}

void
rtw_rxdesc_dmamaps_destroy(bus_dma_tag_t dmat, struct rtw_rxsoft *descs,
    u_int ndescs)
{
	int i;
	for (i = 0; i < ndescs; i++) {
		if (descs[i].rs_dmamap != NULL)
			bus_dmamap_destroy(dmat, descs[i].rs_dmamap);
	}
}

void
rtw_txdesc_dmamaps_destroy(bus_dma_tag_t dmat, struct rtw_txsoft *descs,
    u_int ndescs)
{
	int i;
	for (i = 0; i < ndescs; i++) {
		if (descs[i].ts_dmamap != NULL)
			bus_dmamap_destroy(dmat, descs[i].ts_dmamap);
	}
}

int
rtw_srom_parse(struct rtw_softc *sc)
{
	int i;
	struct rtw_srom *sr = &sc->sc_srom;
	u_int32_t *flags = &sc->sc_flags;
	u_int8_t *cs_threshold = &sc->sc_csthr;
	int *rfchipid = &sc->sc_rfchipid;
	u_int32_t *rcr = &sc->sc_rcr;
	enum rtw_locale *locale = &sc->sc_locale;
	u_int16_t version;
	u_int8_t mac[IEEE80211_ADDR_LEN];

	*flags &= ~(RTW_F_DIGPHY|RTW_F_DFLANTB|RTW_F_ANTDIV);
	*rcr &= ~(RTW_RCR_ENCS1 | RTW_RCR_ENCS2);

	version = RTW_SR_GET16(sr, RTW_SR_VERSION);
	RTW_DPRINTF(RTW_DEBUG_ATTACH,
	    ("%s: SROM %d.%d\n", sc->sc_dev.dv_xname, version >> 8,
	    version & 0xff));

	if (version <= 0x0101) {
		printf(" is not understood, limping along with defaults ");
		*flags |= (RTW_F_DIGPHY|RTW_F_ANTDIV);
		*cs_threshold = RTW_SR_ENERGYDETTHR_DEFAULT;
		*rcr |= RTW_RCR_ENCS1;
		*rfchipid = RTW_RFCHIPID_PHILIPS;
		return 0;
	}

	for (i = 0; i < IEEE80211_ADDR_LEN; i++)
		mac[i] = RTW_SR_GET(sr, RTW_SR_MAC + i);

	RTW_DPRINTF(RTW_DEBUG_ATTACH,
	    ("%s: EEPROM MAC %s\n", sc->sc_dev.dv_xname, ether_sprintf(mac)));

	*cs_threshold = RTW_SR_GET(sr, RTW_SR_ENERGYDETTHR);

	if ((RTW_SR_GET(sr, RTW_SR_CONFIG2) & RTW8180_CONFIG2_ANT) != 0)
		*flags |= RTW_F_ANTDIV;

	/* Note well: the sense of the RTW_SR_RFPARM_DIGPHY bit seems
	 * to be reversed.
	 */
	if ((RTW_SR_GET(sr, RTW_SR_RFPARM) & RTW_SR_RFPARM_DIGPHY) == 0)
		*flags |= RTW_F_DIGPHY;
	if ((RTW_SR_GET(sr, RTW_SR_RFPARM) & RTW_SR_RFPARM_DFLANTB) != 0)
		*flags |= RTW_F_DFLANTB;

	*rcr |= LSHIFT(MASK_AND_RSHIFT(RTW_SR_GET(sr, RTW_SR_RFPARM),
	    RTW_SR_RFPARM_CS_MASK), RTW_RCR_ENCS1);

	*rfchipid = RTW_SR_GET(sr, RTW_SR_RFCHIPID);

	if (sc->sc_flags & RTW_F_RTL8185) {
		*locale = RTW_LOCALE_UNKNOWN;
		return (0);
	}

	switch (RTW_SR_GET(sr, RTW_SR_CONFIG0) & RTW8180_CONFIG0_GL_MASK) {
	case RTW8180_CONFIG0_GL_USA:
		*locale = RTW_LOCALE_USA;
		break;
	case RTW8180_CONFIG0_GL_EUROPE:
		*locale = RTW_LOCALE_EUROPE;
		break;
	case RTW8180_CONFIG0_GL_JAPAN:
	case RTW8180_CONFIG0_GL_JAPAN2:
		*locale = RTW_LOCALE_JAPAN;
		break;
	default:
		*locale = RTW_LOCALE_UNKNOWN;
		break;
	}
	return 0;
}

/* Returns -1 on failure. */
int
rtw_srom_read(struct rtw_regs *regs, u_int32_t flags, struct rtw_srom *sr,
    const char *dvname)
{
	int rc;
	struct seeprom_descriptor sd;
	u_int8_t ecr;

	bzero(&sd, sizeof(sd));

	ecr = RTW_READ8(regs, RTW_9346CR);

	if ((flags & RTW_F_9356SROM) != 0) {
		RTW_DPRINTF(RTW_DEBUG_ATTACH, ("%s: 93c56 SROM\n", dvname));
		sr->sr_size = 256;
		sd.sd_chip = C56_66;
	} else {
		RTW_DPRINTF(RTW_DEBUG_ATTACH, ("%s: 93c46 SROM\n", dvname));
		sr->sr_size = 128;
		sd.sd_chip = C46;
	}

	ecr &= ~(RTW_9346CR_EEDI | RTW_9346CR_EEDO | RTW_9346CR_EESK |
	    RTW_9346CR_EEM_MASK | RTW_9346CR_EECS);
	ecr |= RTW_9346CR_EEM_PROGRAM;

	RTW_WRITE8(regs, RTW_9346CR, ecr);

	sr->sr_content = malloc(sr->sr_size, M_DEVBUF, M_NOWAIT | M_ZERO);

	if (sr->sr_content == NULL) {
		printf("%s: unable to allocate SROM buffer\n", dvname);
		return ENOMEM;
	}

	/* RTL8180 has a single 8-bit register for controlling the
	 * 93cx6 SROM.  There is no "ready" bit. The RTL8180
	 * input/output sense is the reverse of read_seeprom's.
	 */
	sd.sd_tag = regs->r_bt;
	sd.sd_bsh = regs->r_bh;
	sd.sd_regsize = 1;
	sd.sd_control_offset = RTW_9346CR;
	sd.sd_status_offset = RTW_9346CR;
	sd.sd_dataout_offset = RTW_9346CR;
	sd.sd_CK = RTW_9346CR_EESK;
	sd.sd_CS = RTW_9346CR_EECS;
	sd.sd_DI = RTW_9346CR_EEDO;
	sd.sd_DO = RTW_9346CR_EEDI;
	/* make read_seeprom enter EEPROM read/write mode */ 
	sd.sd_MS = ecr;
	sd.sd_RDY = 0;

	/* TBD bus barriers */
	if (!read_seeprom(&sd, sr->sr_content, 0, sr->sr_size/2)) {
		printf("\n%s: could not read SROM\n", dvname);
		free(sr->sr_content, M_DEVBUF, 0);
		sr->sr_content = NULL;
		return -1;	/* XXX */
	}

	/* end EEPROM read/write mode */ 
	RTW_WRITE8(regs, RTW_9346CR,
	    (ecr & ~RTW_9346CR_EEM_MASK) | RTW_9346CR_EEM_NORMAL);
	RTW_WBRW(regs, RTW_9346CR, RTW_9346CR);

	if ((rc = rtw_recall_eeprom(regs, dvname)) != 0)
		return rc;

#ifdef RTW_DEBUG
	{
		int i;
		RTW_DPRINTF(RTW_DEBUG_ATTACH,
		    ("\n%s: serial ROM:\n\t", dvname));
		for (i = 0; i < sr->sr_size/2; i++) {
			if (((i % 8) == 0) && (i != 0))
				RTW_DPRINTF(RTW_DEBUG_ATTACH, ("\n\t"));
			RTW_DPRINTF(RTW_DEBUG_ATTACH,
			    (" %04x", sr->sr_content[i]));
		}
		RTW_DPRINTF(RTW_DEBUG_ATTACH, ("\n"));
	}
#endif /* RTW_DEBUG */
	return 0;
}

void
rtw_set_rfprog(struct rtw_regs *regs, int rfchipid,
    const char *dvname)
{
	u_int8_t cfg4;
	const char *method;

	cfg4 = RTW_READ8(regs, RTW_CONFIG4) & ~RTW_CONFIG4_RFTYPE_MASK;

	switch (rfchipid) {
	default:
		cfg4 |= LSHIFT(rtw_rfprog_fallback, RTW_CONFIG4_RFTYPE_MASK);
		method = "fallback";
		break;
	case RTW_RFCHIPID_INTERSIL:
		cfg4 |= RTW_CONFIG4_RFTYPE_INTERSIL;
		method = "Intersil";
		break;
	case RTW_RFCHIPID_PHILIPS:
		cfg4 |= RTW_CONFIG4_RFTYPE_PHILIPS;
		method = "Philips";
		break;
	case RTW_RFCHIPID_RFMD2948:
		cfg4 |= RTW_CONFIG4_RFTYPE_RFMD;
		method = "RFMD";
		break;
	}

	RTW_WRITE8(regs, RTW_CONFIG4, cfg4);

	RTW_WBR(regs, RTW_CONFIG4, RTW_CONFIG4);

	RTW_DPRINTF(RTW_DEBUG_INIT,
	    ("%s: %s RF programming method, %#02x\n", dvname, method,
	    RTW_READ8(regs, RTW_CONFIG4)));
}

void
rtw_identify_country(struct rtw_regs *regs, enum rtw_locale *locale)
{
	u_int8_t cfg0 = RTW_READ8(regs, RTW_CONFIG0);

	switch (cfg0 & RTW8180_CONFIG0_GL_MASK) {
	case RTW8180_CONFIG0_GL_USA:
		*locale = RTW_LOCALE_USA;
		break;
	case RTW8180_CONFIG0_GL_JAPAN:
	case RTW8180_CONFIG0_GL_JAPAN2:
		*locale = RTW_LOCALE_JAPAN;
		break;
	case RTW8180_CONFIG0_GL_EUROPE:
		*locale = RTW_LOCALE_EUROPE;
		break;
	default:
		*locale = RTW_LOCALE_UNKNOWN;
		break;
	}
}

int
rtw_identify_sta(struct rtw_regs *regs, u_int8_t (*addr)[IEEE80211_ADDR_LEN],
    const char *dvname)
{
	static const u_int8_t empty_macaddr[IEEE80211_ADDR_LEN] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	u_int32_t idr0 = RTW_READ(regs, RTW_IDR0),
	    idr1 = RTW_READ(regs, RTW_IDR1);

	(*addr)[0] = MASK_AND_RSHIFT(idr0, 0xff);
	(*addr)[1] = MASK_AND_RSHIFT(idr0, 0xff00);
	(*addr)[2] = MASK_AND_RSHIFT(idr0, 0xff0000);
	(*addr)[3] = MASK_AND_RSHIFT(idr0, 0xff000000);

	(*addr)[4] = MASK_AND_RSHIFT(idr1, 0xff);
	(*addr)[5] = MASK_AND_RSHIFT(idr1, 0xff00);

	if (IEEE80211_ADDR_EQ(addr, empty_macaddr)) {
		printf("\n%s: could not get mac address, attach failed\n",
		    dvname);
		return ENXIO;
	}

	printf("address %s\n", ether_sprintf(*addr));

	return 0;
}

u_int8_t
rtw_chan2txpower(struct rtw_srom *sr, struct ieee80211com *ic,
    struct ieee80211_channel *chan)
{
	u_int idx = RTW_SR_TXPOWER1 + ieee80211_chan2ieee(ic, chan) - 1;
	KASSERT2(idx >= RTW_SR_TXPOWER1 && idx <= RTW_SR_TXPOWER14,
	    ("%s: channel %d out of range", __func__,
	     idx - RTW_SR_TXPOWER1 + 1));
	return RTW_SR_GET(sr, idx);
}

void
rtw_txdesc_blk_init_all(struct rtw_txdesc_blk *tdb)
{
	int pri;
	/* nfree: the number of free descriptors in each ring.
	 * The beacon ring is a special case: I do not let the
	 * driver use all of the descriptors on the beacon ring.
	 * The reasons are two-fold:
	 *
	 * (1) A BEACON descriptor's OWN bit is (apparently) not
	 * updated, so the driver cannot easily know if the descriptor
	 * belongs to it, or if it is racing the NIC.  If the NIC
	 * does not OWN every descriptor, then the driver can safely
	 * update the descriptors when RTW_TBDA points at tdb_next.
	 *
	 * (2) I hope that the NIC will process more than one BEACON
	 * descriptor in a single beacon interval, since that will
	 * enable multiple-BSS support.  Since the NIC does not
	 * clear the OWN bit, there is no natural place for it to
	 * stop processing BEACON descriptors.  Maybe it will *not*
	 * stop processing them!  I do not want to chance the NIC
	 * looping around and around a saturated beacon ring, so
	 * I will leave one descriptor unOWNed at all times.
	 */
	u_int nfree[RTW_NTXPRI] =
	    {RTW_NTXDESCLO, RTW_NTXDESCMD, RTW_NTXDESCHI,
	     RTW_NTXDESCBCN - 1};

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		tdb[pri].tdb_nfree = nfree[pri];
		tdb[pri].tdb_next = 0;
	}
}

int
rtw_txsoft_blk_init(struct rtw_txsoft_blk *tsb)
{
	int i;
	struct rtw_txsoft *ts;

	SIMPLEQ_INIT(&tsb->tsb_dirtyq);
	SIMPLEQ_INIT(&tsb->tsb_freeq);
	for (i = 0; i < tsb->tsb_ndesc; i++) {
		ts = &tsb->tsb_desc[i];
		ts->ts_mbuf = NULL;
		SIMPLEQ_INSERT_TAIL(&tsb->tsb_freeq, ts, ts_q);
	}
	tsb->tsb_tx_timer = 0;
	return 0;
}

void
rtw_txsoft_blk_init_all(struct rtw_txsoft_blk *tsb)
{
	int pri;
	for (pri = 0; pri < RTW_NTXPRI; pri++)
		rtw_txsoft_blk_init(&tsb[pri]);
}

void
rtw_rxdescs_sync(struct rtw_rxdesc_blk *rdb, int desc0, int nsync, int ops)
{
	KASSERT(nsync <= rdb->rdb_ndesc);
	/* sync to end of ring */
	if (desc0 + nsync > rdb->rdb_ndesc) {
		bus_dmamap_sync(rdb->rdb_dmat, rdb->rdb_dmamap,
		    offsetof(struct rtw_descs, hd_rx[desc0]),
		    sizeof(struct rtw_rxdesc) * (rdb->rdb_ndesc - desc0), ops);
		nsync -= (rdb->rdb_ndesc - desc0);
		desc0 = 0;
	}

	KASSERT(desc0 < rdb->rdb_ndesc);
	KASSERT(nsync <= rdb->rdb_ndesc);
	KASSERT(desc0 + nsync <= rdb->rdb_ndesc);

	/* sync what remains */
	bus_dmamap_sync(rdb->rdb_dmat, rdb->rdb_dmamap,
	    offsetof(struct rtw_descs, hd_rx[desc0]),
	    sizeof(struct rtw_rxdesc) * nsync, ops);
}

void
rtw_txdescs_sync(struct rtw_txdesc_blk *tdb, u_int desc0, u_int nsync, int ops)
{
	/* sync to end of ring */
	if (desc0 + nsync > tdb->tdb_ndesc) {
		bus_dmamap_sync(tdb->tdb_dmat, tdb->tdb_dmamap,
		    tdb->tdb_ofs + sizeof(struct rtw_txdesc) * desc0,
		    sizeof(struct rtw_txdesc) * (tdb->tdb_ndesc - desc0),
		    ops);
		nsync -= (tdb->tdb_ndesc - desc0);
		desc0 = 0;
	}

	/* sync what remains */
	bus_dmamap_sync(tdb->tdb_dmat, tdb->tdb_dmamap,
	    tdb->tdb_ofs + sizeof(struct rtw_txdesc) * desc0,
	    sizeof(struct rtw_txdesc) * nsync, ops);
}

void
rtw_rxbufs_release(bus_dma_tag_t dmat, struct rtw_rxsoft *desc)
{
	int i;
	struct rtw_rxsoft *rs;

	for (i = 0; i < RTW_RXQLEN; i++) {
		rs = &desc[i];
		if (rs->rs_mbuf == NULL)
			continue;
		bus_dmamap_sync(dmat, rs->rs_dmamap, 0,
		    rs->rs_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(dmat, rs->rs_dmamap);
		m_freem(rs->rs_mbuf);
		rs->rs_mbuf = NULL;
	}
}

int
rtw_rxsoft_alloc(bus_dma_tag_t dmat, struct rtw_rxsoft *rs)
{
	int rc;
	struct mbuf *m;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return ENOBUFS;

	MCLGET(m, M_DONTWAIT);
	if ((m->m_flags & M_EXT) == 0) {
		m_freem(m);
		return ENOBUFS;
	}

	m->m_pkthdr.len = m->m_len = m->m_ext.ext_size;

	if (rs->rs_mbuf != NULL)
		bus_dmamap_unload(dmat, rs->rs_dmamap);

	rs->rs_mbuf = NULL;

	rc = bus_dmamap_load_mbuf(dmat, rs->rs_dmamap, m, BUS_DMA_NOWAIT);
	if (rc != 0) {
		m_freem(m);
		return -1;
	}

	rs->rs_mbuf = m;

	return 0;
}

int
rtw_rxsoft_init_all(bus_dma_tag_t dmat, struct rtw_rxsoft *desc,
    int *ndesc, const char *dvname)
{
	int i, rc = 0;
	struct rtw_rxsoft *rs;

	for (i = 0; i < RTW_RXQLEN; i++) {
		rs = &desc[i];
		/* we're in rtw_init, so there should be no mbufs allocated */
		KASSERT(rs->rs_mbuf == NULL);
#ifdef RTW_DEBUG
		if (i == rtw_rxbufs_limit) {
			printf("%s: TEST hit %d-buffer limit\n", dvname, i);
			rc = ENOBUFS;
			break;
		}
#endif /* RTW_DEBUG */
		if ((rc = rtw_rxsoft_alloc(dmat, rs)) != 0) {
			printf("%s: rtw_rxsoft_alloc failed, %d buffers, "
			    "rc %d\n", dvname, i, rc);
			break;
		}
	}
	*ndesc = i;
	return rc;
}

void
rtw_rxdesc_init(struct rtw_rxdesc_blk *rdb, struct rtw_rxsoft *rs,
    int idx, int kick)
{
	int is_last = (idx == rdb->rdb_ndesc - 1);
	uint32_t ctl, octl, obuf;
	struct rtw_rxdesc *rd = &rdb->rdb_desc[idx];

	obuf = rd->rd_buf;
	rd->rd_buf = htole32(rs->rs_dmamap->dm_segs[0].ds_addr);

	ctl = LSHIFT(rs->rs_mbuf->m_len, RTW_RXCTL_LENGTH_MASK) |
	    RTW_RXCTL_OWN | RTW_RXCTL_FS | RTW_RXCTL_LS;

	if (is_last)
		ctl |= RTW_RXCTL_EOR;

	octl = rd->rd_ctl;
	rd->rd_ctl = htole32(ctl);

	RTW_DPRINTF(kick ? (RTW_DEBUG_RECV_DESC | RTW_DEBUG_IO_KICK)
	    : RTW_DEBUG_RECV_DESC,
	    ("%s: rd %p buf %08x -> %08x ctl %08x -> %08x\n", __func__, rd,
	    letoh32(obuf), letoh32(rd->rd_buf), letoh32(octl),
	    letoh32(rd->rd_ctl)));

	/* sync the mbuf */
	bus_dmamap_sync(rdb->rdb_dmat, rs->rs_dmamap, 0,
	    rs->rs_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);

	/* sync the descriptor */
	bus_dmamap_sync(rdb->rdb_dmat, rdb->rdb_dmamap,
	    RTW_DESC_OFFSET(hd_rx, idx), sizeof(struct rtw_rxdesc),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
}

void
rtw_rxdesc_init_all(struct rtw_rxdesc_blk *rdb, struct rtw_rxsoft *ctl,
    int kick)
{
	int i;
	struct rtw_rxsoft *rs;

	for (i = 0; i < rdb->rdb_ndesc; i++) {
		rs = &ctl[i];
		rtw_rxdesc_init(rdb, rs, i, kick);
	}
}

void
rtw_io_enable(struct rtw_regs *regs, u_int8_t flags, int enable)
{
	u_int8_t cr;

	RTW_DPRINTF(RTW_DEBUG_IOSTATE, ("%s: %s 0x%02x\n", __func__,
	    enable ? "enable" : "disable", flags));

	cr = RTW_READ8(regs, RTW_CR);

	/* XXX reference source does not enable MULRW */
#if 0
	/* enable PCI Read/Write Multiple */
	cr |= RTW_CR_MULRW;
#endif

	RTW_RBW(regs, RTW_CR, RTW_CR);	/* XXX paranoia? */
	if (enable)
		cr |= flags;
	else
		cr &= ~flags;
	RTW_WRITE8(regs, RTW_CR, cr);
	RTW_SYNC(regs, RTW_CR, RTW_CR);
}

void
rtw_intr_rx(struct rtw_softc *sc, u_int16_t isr)
{
#define	IS_BEACON(__fc0)						\
    ((__fc0 & (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==\
     (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_BEACON))

	static const int ratetbl[4] = {2, 4, 11, 22};	/* convert rates:
							 * hardware -> net80211
							 */
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	u_int next, nproc = 0;
	int hwrate, len, rate, rssi, sq;
	u_int32_t hrssi, hstat, htsfth, htsftl;
	struct rtw_rxdesc *rd;
	struct rtw_rxsoft *rs;
	struct rtw_rxdesc_blk *rdb;
	struct mbuf *m;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct ieee80211_frame *wh;

	rdb = &sc->sc_rxdesc_blk;

	KASSERT(rdb->rdb_next < rdb->rdb_ndesc);

	for (next = rdb->rdb_next; ; next = (next + 1) % rdb->rdb_ndesc) {
		rtw_rxdescs_sync(rdb, next, 1,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
		rd = &rdb->rdb_desc[next];
		rs = &sc->sc_rxsoft[next];

		hstat = letoh32(rd->rd_stat);
		hrssi = letoh32(rd->rd_rssi);
		htsfth = letoh32(rd->rd_tsfth);
		htsftl = letoh32(rd->rd_tsftl);

		RTW_DPRINTF(RTW_DEBUG_RECV_DESC,
		    ("%s: rxdesc[%d] hstat %08x hrssi %08x htsft %08x%08x\n",
		    __func__, next, hstat, hrssi, htsfth, htsftl));

		++nproc;

		/* still belongs to NIC */
		if ((hstat & RTW_RXSTAT_OWN) != 0) {
			if (nproc > 1)
				break;

			/* sometimes the NIC skips to the 0th descriptor */
			rtw_rxdescs_sync(rdb, 0, 1,
			    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
			rd = &rdb->rdb_desc[0];
			if ((rd->rd_stat & htole32(RTW_RXSTAT_OWN)) != 0)
				break;
			RTW_DPRINTF(RTW_DEBUG_BUGS,
			    ("%s: NIC skipped from rxdesc[%u] to rxdesc[0]\n",
			     sc->sc_dev.dv_xname, next));
			next = rdb->rdb_ndesc - 1;
			continue;
		}

#ifdef RTW_DEBUG
#define PRINTSTAT(flag) do { \
	if ((hstat & flag) != 0) { \
		printf("%s" #flag, delim); \
		delim = ","; \
	} \
} while (0)
		if ((rtw_debug & RTW_DEBUG_RECV_DESC) != 0) {
			const char *delim = "<";
			printf("%s: ", sc->sc_dev.dv_xname);
			if ((hstat & RTW_RXSTAT_DEBUG) != 0) {
				printf("status %08x", hstat);
				PRINTSTAT(RTW_RXSTAT_SPLCP);
				PRINTSTAT(RTW_RXSTAT_MAR);
				PRINTSTAT(RTW_RXSTAT_PAR);
				PRINTSTAT(RTW_RXSTAT_BAR);
				PRINTSTAT(RTW_RXSTAT_PWRMGT);
				PRINTSTAT(RTW_RXSTAT_CRC32);
				PRINTSTAT(RTW_RXSTAT_ICV);
				printf(">, ");
			}
		}
#undef PRINTSTAT
#endif /* RTW_DEBUG */

		if ((hstat & RTW_RXSTAT_IOERROR) != 0) {
			printf("%s: DMA error/FIFO overflow %08x, "
			    "rx descriptor %d\n", sc->sc_dev.dv_xname,
			    hstat & RTW_RXSTAT_IOERROR, next);
			sc->sc_if.if_ierrors++;
			goto next;
		}

		len = MASK_AND_RSHIFT(hstat, RTW_RXSTAT_LENGTH_MASK);
		if (len < IEEE80211_MIN_LEN) {
			sc->sc_ic.ic_stats.is_rx_tooshort++;
			goto next;
		}

		/* CRC is included with the packet; trim it off. */
		len -= IEEE80211_CRC_LEN;

		hwrate = MASK_AND_RSHIFT(hstat, RTW_RXSTAT_RATE_MASK);
		if (hwrate >= sizeof(ratetbl) / sizeof(ratetbl[0])) {
			printf("%s: unknown rate #%d\n", sc->sc_dev.dv_xname,
			    MASK_AND_RSHIFT(hstat, RTW_RXSTAT_RATE_MASK));
			sc->sc_if.if_ierrors++;
			goto next;
		}
		rate = ratetbl[hwrate];

#ifdef RTW_DEBUG
		RTW_DPRINTF(RTW_DEBUG_RECV_DESC,
		    ("rate %d.%d Mb/s, time %08x%08x\n", (rate * 5) / 10,
		     (rate * 5) % 10, htsfth, htsftl));
#endif /* RTW_DEBUG */

		if ((hstat & RTW_RXSTAT_RES) != 0 &&
		    sc->sc_ic.ic_opmode != IEEE80211_M_MONITOR)
			goto next;

		/* if bad flags, skip descriptor */
		if ((hstat & RTW_RXSTAT_ONESEG) != RTW_RXSTAT_ONESEG) {
			printf("%s: too many rx segments\n",
			    sc->sc_dev.dv_xname);
			goto next;
		}

		bus_dmamap_sync(sc->sc_dmat, rs->rs_dmamap, 0,
		    rs->rs_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);

		m = rs->rs_mbuf;

		/* if temporarily out of memory, re-use mbuf */
		switch (rtw_rxsoft_alloc(sc->sc_dmat, rs)) {
		case 0:
			break;
		case ENOBUFS:
			printf("%s: rtw_rxsoft_alloc(, %d) failed, "
			    "dropping this packet\n", sc->sc_dev.dv_xname,
			    next);
			goto next;
		default:
			/* XXX shorten rx ring, instead? */
			panic("%s: could not load DMA map",
			    sc->sc_dev.dv_xname);
		}

		if (sc->sc_rfchipid == RTW_RFCHIPID_PHILIPS)
			rssi = MASK_AND_RSHIFT(hrssi, RTW_RXRSSI_RSSI);
		else {
			rssi = MASK_AND_RSHIFT(hrssi, RTW_RXRSSI_IMR_RSSI);
			/* TBD find out each front-end's LNA gain in the
			 * front-end's units
			 */
			if ((hrssi & RTW_RXRSSI_IMR_LNA) == 0)
				rssi |= 0x80;
		}

		sq = MASK_AND_RSHIFT(hrssi, RTW_RXRSSI_SQ);

		/*
		 * Note well: now we cannot recycle the rs_mbuf unless
		 * we restore its original length.
		 */
		m->m_pkthdr.len = m->m_len = len;

		wh = mtod(m, struct ieee80211_frame *);

		if (!IS_BEACON(wh->i_fc[0]))
			sc->sc_led_state.ls_event |= RTW_LED_S_RX;
		/* TBD use _MAR, _BAR, _PAR flags as hints to _find_rxnode? */
		ni = ieee80211_find_rxnode(&sc->sc_ic, wh);

		sc->sc_tsfth = htsfth;

#ifdef RTW_DEBUG
		if ((sc->sc_if.if_flags & (IFF_DEBUG|IFF_LINK2)) ==
		    (IFF_DEBUG|IFF_LINK2)) {
			ieee80211_dump_pkt(mtod(m, uint8_t *), m->m_pkthdr.len,
			    rate, rssi);
		}
#endif /* RTW_DEBUG */

#if NBPFILTER > 0
		if (sc->sc_radiobpf != NULL) {
			struct mbuf mb;
			struct ieee80211com *ic = &sc->sc_ic;
			struct rtw_rx_radiotap_header *rr = &sc->sc_rxtap;

			rr->rr_tsft =
			    htole64(((uint64_t)htsfth << 32) | htsftl);

			rr->rr_flags = 0;
			if ((hstat & RTW_RXSTAT_SPLCP) != 0)
				rr->rr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;

			rr->rr_rate = rate;
			rr->rr_chan_freq =
			    htole16(ic->ic_bss->ni_chan->ic_freq);
			rr->rr_chan_flags =
			    htole16(ic->ic_bss->ni_chan->ic_flags);
			rr->rr_antsignal = rssi;
			rr->rr_barker_lock = htole16(sq);

			mb.m_data = (caddr_t)rr;
			mb.m_len = sizeof(sc->sc_rxtapu);
			mb.m_next = m;
			mb.m_nextpkt = NULL;
			mb.m_type = 0;
			mb.m_flags = 0;
			bpf_mtap(sc->sc_radiobpf, &mb, BPF_DIRECTION_IN);
		}
#endif /* NBPFILTER > 0 */

		memset(&rxi, 0, sizeof(rxi));
		rxi.rxi_rssi = rssi;
		rxi.rxi_tstamp = htsftl;
		ieee80211_inputm(&sc->sc_if, m, ni, &rxi, &ml);
		ieee80211_release_node(&sc->sc_ic, ni);
next:
		rtw_rxdesc_init(rdb, rs, next, 0);
	}
	if_input(&sc->sc_if, &ml);
	rdb->rdb_next = next;

	KASSERT(rdb->rdb_next < rdb->rdb_ndesc);

	return;
#undef IS_BEACON
}

void
rtw_txsoft_release(bus_dma_tag_t dmat, struct ieee80211com *ic,
    struct rtw_txsoft *ts)
{
	struct mbuf *m;
	struct ieee80211_node *ni;

	m = ts->ts_mbuf;
	ni = ts->ts_ni;
	KASSERT(m != NULL);
	KASSERT(ni != NULL);
	ts->ts_mbuf = NULL;
	ts->ts_ni = NULL;

	bus_dmamap_sync(dmat, ts->ts_dmamap, 0, ts->ts_dmamap->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(dmat, ts->ts_dmamap);
	m_freem(m);
	ieee80211_release_node(ic, ni);
}

void
rtw_txsofts_release(bus_dma_tag_t dmat, struct ieee80211com *ic,
    struct rtw_txsoft_blk *tsb)
{
	struct rtw_txsoft *ts;

	while ((ts = SIMPLEQ_FIRST(&tsb->tsb_dirtyq)) != NULL) {
		rtw_txsoft_release(dmat, ic, ts);
		SIMPLEQ_REMOVE_HEAD(&tsb->tsb_dirtyq, ts_q);
		SIMPLEQ_INSERT_TAIL(&tsb->tsb_freeq, ts, ts_q);
	}
	tsb->tsb_tx_timer = 0;
}

void
rtw_collect_txpkt(struct rtw_softc *sc, struct rtw_txdesc_blk *tdb,
    struct rtw_txsoft *ts, int ndesc)
{
	uint32_t hstat;
	int data_retry, rts_retry;
	struct rtw_txdesc *tdn;
	const char *condstring;

	rtw_txsoft_release(sc->sc_dmat, &sc->sc_ic, ts);

	tdb->tdb_nfree += ndesc;

	tdn = &tdb->tdb_desc[ts->ts_last];

	hstat = letoh32(tdn->td_stat);
	rts_retry = MASK_AND_RSHIFT(hstat, RTW_TXSTAT_RTSRETRY_MASK);
	data_retry = MASK_AND_RSHIFT(hstat, RTW_TXSTAT_DRC_MASK);

	sc->sc_if.if_collisions += rts_retry + data_retry;

	if ((hstat & RTW_TXSTAT_TOK) != 0)
		condstring = "ok";
	else {
		sc->sc_if.if_oerrors++;
		condstring = "error";
	}

	DPRINTF(sc, RTW_DEBUG_XMIT_DESC,
	    ("%s: ts %p txdesc[%d, %d] %s tries rts %u data %u\n",
	    sc->sc_dev.dv_xname, ts, ts->ts_first, ts->ts_last,
	    condstring, rts_retry, data_retry));
}

void
rtw_reset_oactive(struct rtw_softc *sc)
{
	int oactive;
	int pri;
	struct rtw_txsoft_blk *tsb;
	struct rtw_txdesc_blk *tdb;
	oactive = ifq_is_oactive(&sc->sc_if.if_snd);
	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		tsb = &sc->sc_txsoft_blk[pri];
		tdb = &sc->sc_txdesc_blk[pri];
		if (!SIMPLEQ_EMPTY(&tsb->tsb_freeq) && tdb->tdb_nfree > 0)
			ifq_set_oactive(&sc->sc_if.if_snd);
	}
	if (oactive != ifq_is_oactive(&sc->sc_if.if_snd)) {
		DPRINTF(sc, RTW_DEBUG_OACTIVE,
		    ("%s: reset OACTIVE\n", __func__));
	}
}

/* Collect transmitted packets. */
void
rtw_collect_txring(struct rtw_softc *sc, struct rtw_txsoft_blk *tsb,
    struct rtw_txdesc_blk *tdb, int force)
{
	int ndesc;
	struct rtw_txsoft *ts;

	while ((ts = SIMPLEQ_FIRST(&tsb->tsb_dirtyq)) != NULL) {
		ndesc = 1 + ts->ts_last - ts->ts_first;
		if (ts->ts_last < ts->ts_first)
			ndesc += tdb->tdb_ndesc;

		KASSERT(ndesc > 0);

		rtw_txdescs_sync(tdb, ts->ts_first, ndesc,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

		if (force) {
			int i;
			for (i = ts->ts_first; ; i = RTW_NEXT_IDX(tdb, i)) {
				tdb->tdb_desc[i].td_stat &=
				    ~htole32(RTW_TXSTAT_OWN);
				if (i == ts->ts_last)
					break;
			}
			rtw_txdescs_sync(tdb, ts->ts_first, ndesc,
			    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
		} else if ((tdb->tdb_desc[ts->ts_last].td_stat &
		    htole32(RTW_TXSTAT_OWN)) != 0)
			break;

		rtw_collect_txpkt(sc, tdb, ts, ndesc);
		SIMPLEQ_REMOVE_HEAD(&tsb->tsb_dirtyq, ts_q);
		SIMPLEQ_INSERT_TAIL(&tsb->tsb_freeq, ts, ts_q);
	}
	/* no more pending transmissions, cancel watchdog */
	if (ts == NULL)
		tsb->tsb_tx_timer = 0;
	rtw_reset_oactive(sc);
}

void
rtw_intr_tx(struct rtw_softc *sc, u_int16_t isr)
{
	int pri;
	struct rtw_txsoft_blk	*tsb;
	struct rtw_txdesc_blk	*tdb;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		tsb = &sc->sc_txsoft_blk[pri];
		tdb = &sc->sc_txdesc_blk[pri];

		rtw_collect_txring(sc, tsb, tdb, 0);

	}

	if ((isr & RTW_INTR_TX) != 0)
		rtw_start(&sc->sc_if);
}

#ifndef IEEE80211_STA_ONLY
void
rtw_intr_beacon(struct rtw_softc *sc, u_int16_t isr)
{
	u_int next;
	uint32_t tsfth, tsftl;
	struct ieee80211com *ic;
	struct rtw_txdesc_blk *tdb = &sc->sc_txdesc_blk[RTW_TXPRIBCN];
	struct rtw_txsoft_blk *tsb = &sc->sc_txsoft_blk[RTW_TXPRIBCN];
	struct mbuf *m;

	tsfth = RTW_READ(&sc->sc_regs, RTW_TSFTRH);
	tsftl = RTW_READ(&sc->sc_regs, RTW_TSFTRL);

	if ((isr & (RTW_INTR_TBDOK|RTW_INTR_TBDER)) != 0) {
		next = rtw_txring_next(&sc->sc_regs, tdb);
		RTW_DPRINTF(RTW_DEBUG_BEACON,
		    ("%s: beacon ring %sprocessed, isr = %#04hx"
		     ", next %u expected %u, %llu\n", __func__,
		     (next == tdb->tdb_next) ? "" : "un", isr, next,
		     tdb->tdb_next, (uint64_t)tsfth << 32 | tsftl));
		if ((RTW_READ8(&sc->sc_regs, RTW_TPPOLL) & RTW_TPPOLL_BQ) == 0){
			rtw_collect_txring(sc, tsb, tdb, 1);
			tdb->tdb_next = 0;
		}
	}
	/* Start beacon transmission. */

	if ((isr & RTW_INTR_BCNINT) != 0 &&
	    sc->sc_ic.ic_state == IEEE80211_S_RUN &&
	    SIMPLEQ_EMPTY(&tsb->tsb_dirtyq)) {
		RTW_DPRINTF(RTW_DEBUG_BEACON,
		    ("%s: beacon prep. time, isr = %#04hx"
		     ", %16llu\n", __func__, isr,
		     (uint64_t)tsfth << 32 | tsftl));
		ic = &sc->sc_ic;
		if ((m = ieee80211_beacon_alloc(ic, ic->ic_bss)) != NULL) {
			RTW_DPRINTF(RTW_DEBUG_BEACON,
			    ("%s: m %p len %u\n", __func__, m, m->m_len));
		}

		if (m == NULL) {
			printf("%s: could not allocate beacon\n",
			    sc->sc_dev.dv_xname);
			return;
		}
		m->m_pkthdr.ph_cookie = ieee80211_ref_node(ic->ic_bss);
		mq_enqueue(&sc->sc_beaconq, m);
		rtw_start(&sc->sc_if);
	}
}

void
rtw_intr_atim(struct rtw_softc *sc)
{
	/* TBD */
	return;
}
#endif	/* IEEE80211_STA_ONLY */

#ifdef RTW_DEBUG
void
rtw_dump_rings(struct rtw_softc *sc)
{
	struct rtw_txdesc_blk *tdb;
	struct rtw_rxdesc *rd;
	struct rtw_rxdesc_blk *rdb;
	int desc, pri;

	if ((rtw_debug & RTW_DEBUG_IO_KICK) == 0)
		return;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		tdb = &sc->sc_txdesc_blk[pri];
		printf("%s: txpri %d ndesc %d nfree %d\n", __func__, pri,
		    tdb->tdb_ndesc, tdb->tdb_nfree);
		for (desc = 0; desc < tdb->tdb_ndesc; desc++)
			rtw_print_txdesc(sc, ".", NULL, tdb, desc);
	}

	rdb = &sc->sc_rxdesc_blk;

	for (desc = 0; desc < RTW_RXQLEN; desc++) {
		rd = &rdb->rdb_desc[desc];
		printf("%s: %sctl %08x rsvd0/rssi %08x buf/tsftl %08x "
		    "rsvd1/tsfth %08x\n", __func__,
		    (desc >= rdb->rdb_ndesc) ? "UNUSED " : "",
		    letoh32(rd->rd_ctl), letoh32(rd->rd_rssi),
		    letoh32(rd->rd_buf), letoh32(rd->rd_tsfth));
	}
}
#endif /* RTW_DEBUG */

void
rtw_hwring_setup(struct rtw_softc *sc)
{
	int pri;
	struct rtw_regs *regs = &sc->sc_regs;
	struct rtw_txdesc_blk *tdb;

	sc->sc_txdesc_blk[RTW_TXPRILO].tdb_basereg = RTW_TLPDA;
	sc->sc_txdesc_blk[RTW_TXPRILO].tdb_base = RTW_RING_BASE(sc, hd_txlo);
	sc->sc_txdesc_blk[RTW_TXPRIMD].tdb_basereg = RTW_TNPDA;
	sc->sc_txdesc_blk[RTW_TXPRIMD].tdb_base = RTW_RING_BASE(sc, hd_txmd);
	sc->sc_txdesc_blk[RTW_TXPRIHI].tdb_basereg = RTW_THPDA;
	sc->sc_txdesc_blk[RTW_TXPRIHI].tdb_base = RTW_RING_BASE(sc, hd_txhi);
	sc->sc_txdesc_blk[RTW_TXPRIBCN].tdb_basereg = RTW_TBDA;
	sc->sc_txdesc_blk[RTW_TXPRIBCN].tdb_base = RTW_RING_BASE(sc, hd_bcn);

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		tdb = &sc->sc_txdesc_blk[pri];
		RTW_WRITE(regs, tdb->tdb_basereg, tdb->tdb_base);
		RTW_DPRINTF(RTW_DEBUG_XMIT_DESC,
		    ("%s: reg[tdb->tdb_basereg] <- %lx\n", __func__,
		     (u_int *)tdb->tdb_base));
	}

	RTW_WRITE(regs, RTW_RDSAR, RTW_RING_BASE(sc, hd_rx));

	RTW_DPRINTF(RTW_DEBUG_RECV_DESC,
	    ("%s: reg[RDSAR] <- %lx\n", __func__,
	     (u_int *)RTW_RING_BASE(sc, hd_rx)));

	RTW_SYNC(regs, RTW_TLPDA, RTW_RDSAR);
}

int
rtw_swring_setup(struct rtw_softc *sc)
{
	int rc, pri;
	struct rtw_rxdesc_blk *rdb;
	struct rtw_txdesc_blk *tdb;

	rtw_txdesc_blk_init_all(&sc->sc_txdesc_blk[0]);

	rtw_txsoft_blk_init_all(&sc->sc_txsoft_blk[0]);

	rdb = &sc->sc_rxdesc_blk;
	if ((rc = rtw_rxsoft_init_all(sc->sc_dmat, sc->sc_rxsoft,
	    &rdb->rdb_ndesc, sc->sc_dev.dv_xname)) != 0 &&
	    rdb->rdb_ndesc == 0) {
		printf("%s: could not allocate rx buffers\n",
		    sc->sc_dev.dv_xname);
		return rc;
	}

	rdb = &sc->sc_rxdesc_blk;
	rtw_rxdescs_sync(rdb, 0, rdb->rdb_ndesc,
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	rtw_rxdesc_init_all(rdb, sc->sc_rxsoft, 1);
	rdb->rdb_next = 0;

	tdb = &sc->sc_txdesc_blk[0];
	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		rtw_txdescs_sync(&tdb[pri], 0, tdb[pri].tdb_ndesc,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	}
	return 0;
}

void
rtw_txdesc_blk_init(struct rtw_txdesc_blk *tdb)
{
	int i;

	bzero(tdb->tdb_desc, sizeof(tdb->tdb_desc[0]) * tdb->tdb_ndesc);
	for (i = 0; i < tdb->tdb_ndesc; i++)
		tdb->tdb_desc[i].td_next = htole32(RTW_NEXT_DESC(tdb, i));
}

u_int
rtw_txring_next(struct rtw_regs *regs, struct rtw_txdesc_blk *tdb)
{
	return (letoh32(RTW_READ(regs, tdb->tdb_basereg)) - tdb->tdb_base) /
	    sizeof(struct rtw_txdesc);
}

void
rtw_txring_fixup(struct rtw_softc *sc)
{
	int pri;
	u_int next;
	struct rtw_txdesc_blk *tdb;
	struct rtw_regs *regs = &sc->sc_regs;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		tdb = &sc->sc_txdesc_blk[pri];
		next = rtw_txring_next(regs, tdb);
		if (tdb->tdb_next == next)
			continue;
		RTW_DPRINTF(RTW_DEBUG_BUGS,
		    ("%s: tx-ring %d expected next %u, read %u\n", __func__,
		    pri, tdb->tdb_next, next));
		tdb->tdb_next = MIN(next, tdb->tdb_ndesc - 1);
	}
}

void
rtw_rxring_fixup(struct rtw_softc *sc)
{
	u_int next;
	uint32_t rdsar;
	struct rtw_rxdesc_blk *rdb;

	rdsar = letoh32(RTW_READ(&sc->sc_regs, RTW_RDSAR));
	next = (rdsar - RTW_RING_BASE(sc, hd_rx)) / sizeof(struct rtw_rxdesc);

	rdb = &sc->sc_rxdesc_blk;
	if (rdb->rdb_next != next) {
		RTW_DPRINTF(RTW_DEBUG_BUGS,
		    ("%s: rx-ring expected next %u, read %u\n", __func__,
		    rdb->rdb_next, next));
		rdb->rdb_next = MIN(next, rdb->rdb_ndesc - 1);
	}
}

void
rtw_txdescs_reset(struct rtw_softc *sc)
{
	int pri;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		rtw_collect_txring(sc, &sc->sc_txsoft_blk[pri],
		    &sc->sc_txdesc_blk[pri], 1);
	}
}

void
rtw_intr_ioerror(struct rtw_softc *sc, u_int16_t isr)
{
	uint8_t cr = 0;
	int xmtr = 0, rcvr = 0;
	struct rtw_regs *regs = &sc->sc_regs;

	if ((isr & RTW_INTR_TXFOVW) != 0) {
		RTW_DPRINTF(RTW_DEBUG_BUGS,
		    ("%s: tx fifo underflow\n", sc->sc_dev.dv_xname));
		rcvr = xmtr = 1;
		cr |= RTW_CR_TE | RTW_CR_RE;
	}

	if ((isr & (RTW_INTR_RDU|RTW_INTR_RXFOVW)) != 0) {
		cr |= RTW_CR_RE;
		rcvr = 1;
	}

	RTW_DPRINTF(RTW_DEBUG_BUGS, ("%s: restarting xmit/recv, isr %hx"
	    "\n", sc->sc_dev.dv_xname, isr));

#ifdef RTW_DEBUG
	rtw_dump_rings(sc);
#endif /* RTW_DEBUG */

	rtw_io_enable(regs, cr, 0);

	/* Collect rx'd packets.  Refresh rx buffers. */
	if (rcvr)
		rtw_intr_rx(sc, 0);
	/* Collect tx'd packets.  XXX let's hope this stops the transmit
	 * timeouts.
	 */
	if (xmtr)
		rtw_txdescs_reset(sc);

	RTW_WRITE16(regs, RTW_IMR, 0);
	RTW_SYNC(regs, RTW_IMR, RTW_IMR);

	if (rtw_do_chip_reset) {
		rtw_chip_reset1(regs, sc->sc_dev.dv_xname);
	}

	rtw_rxdesc_init_all(&sc->sc_rxdesc_blk, &sc->sc_rxsoft[0], 1);

#ifdef RTW_DEBUG
	rtw_dump_rings(sc);
#endif /* RTW_DEBUG */

	RTW_WRITE16(regs, RTW_IMR, sc->sc_inten);
	RTW_SYNC(regs, RTW_IMR, RTW_IMR);
	if (rcvr)
		rtw_rxring_fixup(sc);
	rtw_io_enable(regs, cr, 1);
	if (xmtr)
		rtw_txring_fixup(sc);
}

void
rtw_suspend_ticks(struct rtw_softc *sc)
{
	RTW_DPRINTF(RTW_DEBUG_TIMEOUT,
	    ("%s: suspending ticks\n", sc->sc_dev.dv_xname));
	sc->sc_do_tick = 0;
}

void
rtw_resume_ticks(struct rtw_softc *sc)
{
	u_int32_t tsftrl0, tsftrl1, next_tick;

	tsftrl0 = RTW_READ(&sc->sc_regs, RTW_TSFTRL);

	tsftrl1 = RTW_READ(&sc->sc_regs, RTW_TSFTRL);
	next_tick = tsftrl1 + 1000000;
	RTW_WRITE(&sc->sc_regs, RTW_TINT, next_tick);

	sc->sc_do_tick = 1;

	RTW_DPRINTF(RTW_DEBUG_TIMEOUT,
	    ("%s: resume ticks delta %#08x now %#08x next %#08x\n",
	    sc->sc_dev.dv_xname, tsftrl1 - tsftrl0, tsftrl1, next_tick));
}

void
rtw_intr_timeout(struct rtw_softc *sc)
{
	RTW_DPRINTF(RTW_DEBUG_TIMEOUT, ("%s: timeout\n", sc->sc_dev.dv_xname));
	if (sc->sc_do_tick)
		rtw_resume_ticks(sc);
	return;
}

int
rtw_intr(void *arg)
{
	int i;
	struct rtw_softc *sc = arg;
	struct rtw_regs *regs = &sc->sc_regs;
	u_int16_t isr;

	/*
	 * If the interface isn't running, the interrupt couldn't
	 * possibly have come from us.
	 */
	if ((sc->sc_flags & RTW_F_ENABLED) == 0 ||
	    (sc->sc_if.if_flags & IFF_RUNNING) == 0 ||
	    (sc->sc_dev.dv_flags & DVF_ACTIVE) == 0) {
		RTW_DPRINTF(RTW_DEBUG_INTR, ("%s: stray interrupt\n",
		     sc->sc_dev.dv_xname));
		return (0);
	}

	for (i = 0; i < 10; i++) {
		isr = RTW_READ16(regs, RTW_ISR);

		RTW_WRITE16(regs, RTW_ISR, isr);
		RTW_WBR(regs, RTW_ISR, RTW_ISR);

		if (sc->sc_intr_ack != NULL)
			(*sc->sc_intr_ack)(regs);

		if (isr == 0)
			break;

#ifdef RTW_DEBUG
#define PRINTINTR(flag) do { \
	if ((isr & flag) != 0) { \
		printf("%s" #flag, delim); \
		delim = ","; \
	} \
} while (0)

		if ((rtw_debug & RTW_DEBUG_INTR) != 0 && isr != 0) {
			const char *delim = "<";

			printf("%s: reg[ISR] = %x", sc->sc_dev.dv_xname, isr);

			PRINTINTR(RTW_INTR_TXFOVW);
			PRINTINTR(RTW_INTR_TIMEOUT);
			PRINTINTR(RTW_INTR_BCNINT);
			PRINTINTR(RTW_INTR_ATIMINT);
			PRINTINTR(RTW_INTR_TBDER);
			PRINTINTR(RTW_INTR_TBDOK);
			PRINTINTR(RTW_INTR_THPDER);
			PRINTINTR(RTW_INTR_THPDOK);
			PRINTINTR(RTW_INTR_TNPDER);
			PRINTINTR(RTW_INTR_TNPDOK);
			PRINTINTR(RTW_INTR_RXFOVW);
			PRINTINTR(RTW_INTR_RDU);
			PRINTINTR(RTW_INTR_TLPDER);
			PRINTINTR(RTW_INTR_TLPDOK);
			PRINTINTR(RTW_INTR_RER);
			PRINTINTR(RTW_INTR_ROK);

			printf(">\n");
		}
#undef PRINTINTR
#endif /* RTW_DEBUG */

		if ((isr & RTW_INTR_RX) != 0)
			rtw_intr_rx(sc, isr & RTW_INTR_RX);
		if ((isr & RTW_INTR_TX) != 0)
			rtw_intr_tx(sc, isr & RTW_INTR_TX);
#ifndef IEEE80211_STA_ONLY
		if ((isr & RTW_INTR_BEACON) != 0)
			rtw_intr_beacon(sc, isr & RTW_INTR_BEACON);
		if ((isr & RTW_INTR_ATIMINT) != 0)
			rtw_intr_atim(sc);
#endif
		if ((isr & RTW_INTR_IOERROR) != 0)
			rtw_intr_ioerror(sc, isr & RTW_INTR_IOERROR);
		if ((isr & RTW_INTR_TIMEOUT) != 0)
			rtw_intr_timeout(sc);
	}

	return 1;
}

/* Must be called at splnet. */
void
rtw_stop(struct ifnet *ifp, int disable)
{
	int pri;
	struct rtw_softc *sc = (struct rtw_softc *)ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct rtw_regs *regs = &sc->sc_regs;

	if ((sc->sc_flags & RTW_F_ENABLED) == 0)
		return;

	rtw_suspend_ticks(sc);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);

	if ((sc->sc_flags & RTW_F_INVALID) == 0) {
		/* Disable interrupts. */
		RTW_WRITE16(regs, RTW_IMR, 0);

		RTW_WBW(regs, RTW_TPPOLL, RTW_IMR);

		/* Stop the transmit and receive processes. First stop DMA,
		 * then disable receiver and transmitter.
		 */
		RTW_WRITE8(regs, RTW_TPPOLL, RTW_TPPOLL_SALL);

		RTW_SYNC(regs, RTW_TPPOLL, RTW_IMR);

		rtw_io_enable(&sc->sc_regs, RTW_CR_RE|RTW_CR_TE, 0);
	}

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		rtw_txsofts_release(sc->sc_dmat, &sc->sc_ic,
		    &sc->sc_txsoft_blk[pri]);
	}

	rtw_rxbufs_release(sc->sc_dmat, &sc->sc_rxsoft[0]);

	if (disable)
		rtw_disable(sc);

	/* Mark the interface as not running.  Cancel the watchdog timer. */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	return;
}

#ifdef RTW_DEBUG
const char *
rtw_pwrstate_string(enum rtw_pwrstate power)
{
	switch (power) {
	case RTW_ON:
		return "on";
	case RTW_SLEEP:
		return "sleep";
	case RTW_OFF:
		return "off";
	default:
		return "unknown";
	}
}
#endif

/* XXX For Maxim, I am using the RFMD settings gleaned from the
 * reference driver, plus a magic Maxim "ON" value that comes from
 * the Realtek document "Windows PG for Rtl8180."
 */
void
rtw_maxim_pwrstate(struct rtw_regs *regs, enum rtw_pwrstate power,
    int before_rf, int digphy)
{
	u_int32_t anaparm;

	anaparm = RTW_READ(regs, RTW_ANAPARM_0);
	anaparm &= ~(RTW_ANAPARM_RFPOW_MASK | RTW_ANAPARM_TXDACOFF);

	switch (power) {
	case RTW_OFF:
		if (before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_MAXIM_OFF;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_SLEEP:
		if (!before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_MAXIM_SLEEP;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_ON:
		if (!before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_MAXIM_ON;
		break;
	}
	RTW_DPRINTF(RTW_DEBUG_PWR,
	    ("%s: power state %s, %s RF, reg[ANAPARM] <- %08x\n",
	    __func__, rtw_pwrstate_string(power),
	    (before_rf) ? "before" : "after", anaparm));

	RTW_WRITE(regs, RTW_ANAPARM_0, anaparm);
	RTW_SYNC(regs, RTW_ANAPARM_0, RTW_ANAPARM_0);
}

/* XXX I am using the RFMD settings gleaned from the reference
 * driver.  They agree 
 */
void
rtw_rfmd_pwrstate(struct rtw_regs *regs, enum rtw_pwrstate power,
    int before_rf, int digphy)
{
	u_int32_t anaparm;

	anaparm = RTW_READ(regs, RTW_ANAPARM_0);
	anaparm &= ~(RTW_ANAPARM_RFPOW_MASK | RTW_ANAPARM_TXDACOFF);

	switch (power) {
	case RTW_OFF:
		if (before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_RFMD_OFF;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_SLEEP:
		if (!before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_RFMD_SLEEP;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_ON:
		if (!before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_RFMD_ON;
		break;
	}
	RTW_DPRINTF(RTW_DEBUG_PWR,
	    ("%s: power state %s, %s RF, reg[ANAPARM] <- %08x\n",
	    __func__, rtw_pwrstate_string(power),
	    (before_rf) ? "before" : "after", anaparm));

	RTW_WRITE(regs, RTW_ANAPARM_0, anaparm);
	RTW_SYNC(regs, RTW_ANAPARM_0, RTW_ANAPARM_0);
}

void
rtw_philips_pwrstate(struct rtw_regs *regs, enum rtw_pwrstate power,
    int before_rf, int digphy)
{
	u_int32_t anaparm;

	anaparm = RTW_READ(regs, RTW_ANAPARM_0);
	anaparm &= ~(RTW_ANAPARM_RFPOW_MASK | RTW_ANAPARM_TXDACOFF);

	switch (power) {
	case RTW_OFF:
		if (before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_PHILIPS_OFF;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_SLEEP:
		if (!before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_PHILIPS_SLEEP;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_ON:
		if (!before_rf)
			return;
		if (digphy) {
			anaparm |= RTW_ANAPARM_RFPOW_DIG_PHILIPS_ON;
			/* XXX guess */
			anaparm |= RTW_ANAPARM_TXDACOFF;
		} else
			anaparm |= RTW_ANAPARM_RFPOW_ANA_PHILIPS_ON;
		break;
	}
	RTW_DPRINTF(RTW_DEBUG_PWR,
	    ("%s: power state %s, %s RF, reg[ANAPARM] <- %08x\n",
	    __func__, rtw_pwrstate_string(power),
	    (before_rf) ? "before" : "after", anaparm));

	RTW_WRITE(regs, RTW_ANAPARM_0, anaparm);
	RTW_SYNC(regs, RTW_ANAPARM_0, RTW_ANAPARM_0);
}

void
rtw_rtl_pwrstate(struct rtw_regs *regs, enum rtw_pwrstate power,
    int before_rf, int digphy)
{
	/* empty */
}

void
rtw_pwrstate0(struct rtw_softc *sc, enum rtw_pwrstate power, int before_rf,
    int digphy)
{
	struct rtw_regs *regs = &sc->sc_regs;

	rtw_set_access(regs, RTW_ACCESS_ANAPARM);

	(*sc->sc_pwrstate_cb)(regs, power, before_rf, digphy);

	rtw_set_access(regs, RTW_ACCESS_NONE);

	return;
}

int
rtw_pwrstate(struct rtw_softc *sc, enum rtw_pwrstate power)
{
	int rc;

	RTW_DPRINTF(RTW_DEBUG_PWR,
	    ("%s: %s->%s\n", __func__,
	    rtw_pwrstate_string(sc->sc_pwrstate), rtw_pwrstate_string(power)));

	if (sc->sc_pwrstate == power)
		return 0;

	rtw_pwrstate0(sc, power, 1, sc->sc_flags & RTW_F_DIGPHY);
	rc = (*sc->sc_rf_pwrstate)(sc, power);
	rtw_pwrstate0(sc, power, 0, sc->sc_flags & RTW_F_DIGPHY);

	switch (power) {
	case RTW_ON:
		/* TBD set LEDs */
		break;
	case RTW_SLEEP:
		/* TBD */
		break;
	case RTW_OFF:
		/* TBD */
		break;
	}
	if (rc == 0)
		sc->sc_pwrstate = power;
	else
		sc->sc_pwrstate = RTW_OFF;
	return rc;
}

int
rtw_tune(struct rtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	u_int chan, idx;
	u_int8_t txpower;
	int rc;

	KASSERT(ic->ic_bss->ni_chan != NULL);

	chan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return 0;

	if (chan == sc->sc_cur_chan) {
		RTW_DPRINTF(RTW_DEBUG_TUNE,
		    ("%s: already tuned chan #%d\n", __func__, chan));
		return 0;
	}

	rtw_suspend_ticks(sc);

	rtw_io_enable(&sc->sc_regs, RTW_CR_RE | RTW_CR_TE, 0);

	/* TBD wait for Tx to complete */

	KASSERT((sc->sc_flags & RTW_F_ENABLED) != 0);

	idx = RTW_SR_TXPOWER1 +
	    ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan) - 1;
	KASSERT2(idx >= RTW_SR_TXPOWER1 && idx <= RTW_SR_TXPOWER14,
	    ("%s: channel %d out of range", __func__,
	     idx - RTW_SR_TXPOWER1 + 1));
	txpower =  RTW_SR_GET(&sc->sc_srom, idx);

	if ((rc = rtw_phy_init(sc)) != 0) {
		/* XXX condition on powersaving */
		printf("%s: phy init failed\n", sc->sc_dev.dv_xname);
	}

	sc->sc_cur_chan = chan;

	rtw_io_enable(&sc->sc_regs, RTW_CR_RE | RTW_CR_TE, 1);

	rtw_resume_ticks(sc);

	return rc;
}

void
rtw_disable(struct rtw_softc *sc)
{
	int rc;

	if ((sc->sc_flags & RTW_F_ENABLED) == 0)
		return;

	/* turn off PHY */
	if ((sc->sc_flags & RTW_F_INVALID) == 0 &&
	    (rc = rtw_pwrstate(sc, RTW_OFF)) != 0) {
		printf("%s: failed to turn off PHY (%d)\n",
		    sc->sc_dev.dv_xname, rc);
	}

	if (sc->sc_disable != NULL)
		(*sc->sc_disable)(sc);

	sc->sc_flags &= ~RTW_F_ENABLED;
}

int
rtw_enable(struct rtw_softc *sc)
{
	if ((sc->sc_flags & RTW_F_ENABLED) == 0) {
		if (sc->sc_enable != NULL && (*sc->sc_enable)(sc) != 0) {
			printf("%s: device enable failed\n",
			    sc->sc_dev.dv_xname);
			return (EIO);
		}
		sc->sc_flags |= RTW_F_ENABLED;
	}
	return (0);
}

void
rtw_transmit_config(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	u_int32_t tcr;

	tcr = RTW_READ(regs, RTW_TCR);

	tcr |= RTW_TCR_CWMIN;
	tcr &= ~RTW_TCR_MXDMA_MASK;
	tcr |= RTW_TCR_MXDMA_256;
	if ((sc->sc_flags & RTW_F_RTL8185) == 0)
		tcr |= RTW8180_TCR_SAT;		/* send ACK as fast as possible */
	tcr &= ~RTW_TCR_LBK_MASK;
	tcr |= RTW_TCR_LBK_NORMAL;	/* normal operating mode */

	/* set short/long retry limits */
	tcr &= ~(RTW_TCR_SRL_MASK|RTW_TCR_LRL_MASK);
	tcr |= LSHIFT(4, RTW_TCR_SRL_MASK) | LSHIFT(4, RTW_TCR_LRL_MASK);

	tcr &= ~RTW_TCR_CRC;    /* NIC appends CRC32 */

	RTW_WRITE(regs, RTW_TCR, tcr);
	RTW_SYNC(regs, RTW_TCR, RTW_TCR);
}

void
rtw_enable_interrupts(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;

	sc->sc_inten = RTW_INTR_RX|RTW_INTR_TX|RTW_INTR_BEACON|RTW_INTR_ATIMINT;
	sc->sc_inten |= RTW_INTR_IOERROR|RTW_INTR_TIMEOUT;

	RTW_WRITE16(regs, RTW_IMR, sc->sc_inten);
	RTW_WBW(regs, RTW_IMR, RTW_ISR);
	RTW_WRITE16(regs, RTW_ISR, 0xffff);
	RTW_SYNC(regs, RTW_IMR, RTW_ISR);

	/* XXX necessary? */
	if (sc->sc_intr_ack != NULL)
		(*sc->sc_intr_ack)(regs);
}

void
rtw_set_nettype(struct rtw_softc *sc, enum ieee80211_opmode opmode)
{
	uint8_t msr;

	/* I'm guessing that MSR is protected as CONFIG[0123] are. */
	rtw_set_access(&sc->sc_regs, RTW_ACCESS_CONFIG);

	msr = RTW_READ8(&sc->sc_regs, RTW_MSR) & ~RTW_MSR_NETYPE_MASK;

	switch (opmode) {
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_AHDEMO:
	case IEEE80211_M_IBSS:
		msr |= RTW_MSR_NETYPE_ADHOC_OK;
		break;
	case IEEE80211_M_HOSTAP:
		msr |= RTW_MSR_NETYPE_AP_OK;
		break;
#endif
	case IEEE80211_M_MONITOR:
		/* XXX */
		msr |= RTW_MSR_NETYPE_NOLINK;
		break;
	case IEEE80211_M_STA:
		msr |= RTW_MSR_NETYPE_INFRA_OK;
		break;
	default:
		break;
	}
	RTW_WRITE8(&sc->sc_regs, RTW_MSR, msr);

	rtw_set_access(&sc->sc_regs, RTW_ACCESS_NONE);
}

void
rtw_pktfilt_load(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	struct ieee80211com *ic = &sc->sc_ic;
	struct arpcom *ac = &ic->ic_ac;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int hash;
	u_int32_t hashes[2] = { 0, 0 };
	struct ether_multi *enm;
	struct ether_multistep step;

	/* XXX might be necessary to stop Rx/Tx engines while setting filters */

	sc->sc_rcr &= ~RTW_RCR_PKTFILTER_MASK;
	sc->sc_rcr &= ~(RTW_RCR_MXDMA_MASK | RTW8180_RCR_RXFTH_MASK);

	sc->sc_rcr |= RTW_RCR_PKTFILTER_DEFAULT;
	/* MAC auto-reset PHY (huh?) */
	sc->sc_rcr |= RTW_RCR_ENMARP;
	/* DMA whole Rx packets, only.  Set Tx DMA burst size to 1024 bytes. */
	sc->sc_rcr |= RTW_RCR_MXDMA_1024 | RTW8180_RCR_RXFTH_WHOLE;

	switch (ic->ic_opmode) {
	case IEEE80211_M_MONITOR:
		sc->sc_rcr |= RTW_RCR_MONITOR;
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_AHDEMO:
	case IEEE80211_M_IBSS:
		/* receive broadcasts in our BSS */
		sc->sc_rcr |= RTW_RCR_ADD3;
		break;
#endif
	default:
		break;
	}

	ifp->if_flags &= ~IFF_ALLMULTI;

	/* XXX accept all broadcast if scanning */
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		sc->sc_rcr |= RTW_RCR_AB;	/* accept all broadcast */

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		if (ifp->if_flags & IFF_PROMISC)
			sc->sc_rcr |= RTW_RCR_AB; /* accept all broadcast */
allmulti:
		ifp->if_flags |= IFF_ALLMULTI;
		goto setit;
	}

	/*
	 * Program the 64-bit multicast hash filter.
	 */
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		hash = ether_crc32_be((enm->enm_addrlo),
		    IEEE80211_ADDR_LEN) >> 26;
		hashes[hash >> 5] |= (1 << (hash & 0x1f));
		sc->sc_rcr |= RTW_RCR_AM;
		ETHER_NEXT_MULTI(step, enm);
	}

	/* all bits set => hash is useless */
	if (~(hashes[0] & hashes[1]) == 0)
		goto allmulti;

 setit:
	if (ifp->if_flags & IFF_ALLMULTI) {
		sc->sc_rcr |= RTW_RCR_AM;	/* accept all multicast */
		hashes[0] = hashes[1] = 0xffffffff;
	}

	RTW_WRITE(regs, RTW_MAR0, hashes[0]);
	RTW_WRITE(regs, RTW_MAR1, hashes[1]);
	RTW_WRITE(regs, RTW_RCR, sc->sc_rcr);
	RTW_SYNC(regs, RTW_MAR0, RTW_RCR); /* RTW_MAR0 < RTW_MAR1 < RTW_RCR */

	DPRINTF(sc, RTW_DEBUG_PKTFILT,
	    ("%s: RTW_MAR0 %08x RTW_MAR1 %08x RTW_RCR %08x\n",
	    sc->sc_dev.dv_xname, RTW_READ(regs, RTW_MAR0),
	    RTW_READ(regs, RTW_MAR1), RTW_READ(regs, RTW_RCR)));

	return;
}

/* Must be called at splnet. */
int
rtw_init(struct ifnet *ifp)
{
	struct rtw_softc *sc = (struct rtw_softc *)ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct rtw_regs *regs = &sc->sc_regs;
	int rc = 0;

	if ((rc = rtw_enable(sc)) != 0)
		goto out;

	/* Cancel pending I/O and reset. */
	rtw_stop(ifp, 0);

	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	DPRINTF(sc, RTW_DEBUG_TUNE, ("%s: channel %d freq %d flags 0x%04x\n",
	    __func__, ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan),
	    ic->ic_bss->ni_chan->ic_freq, ic->ic_bss->ni_chan->ic_flags));

	if ((rc = rtw_pwrstate(sc, RTW_OFF)) != 0)
		goto out;

	if ((rc = rtw_swring_setup(sc)) != 0)
		goto out;

	rtw_transmit_config(sc);

	rtw_set_access(regs, RTW_ACCESS_CONFIG);

	RTW_WRITE8(regs, RTW_MSR, 0x0);	/* no link */
	RTW_WBW(regs, RTW_MSR, RTW_BRSR);

	/* long PLCP header, 1Mb/2Mb basic rate */
	if (sc->sc_flags & RTW_F_RTL8185)
		RTW_WRITE16(regs, RTW_BRSR, RTW8185_BRSR_MBR_2MBPS);
	else
		RTW_WRITE16(regs, RTW_BRSR, RTW8180_BRSR_MBR_2MBPS);
	RTW_SYNC(regs, RTW_BRSR, RTW_BRSR);

	rtw_set_access(regs, RTW_ACCESS_ANAPARM);
	rtw_set_access(regs, RTW_ACCESS_NONE);

	/* XXX from reference sources */
	RTW_WRITE(regs, RTW_FEMR, 0xffff);
	RTW_SYNC(regs, RTW_FEMR, RTW_FEMR);

	rtw_set_rfprog(regs, sc->sc_rfchipid, sc->sc_dev.dv_xname);

	RTW_WRITE8(regs, RTW_PHYDELAY, sc->sc_phydelay);
	/* from Linux driver */
	RTW_WRITE8(regs, RTW_CRCOUNT, RTW_CRCOUNT_MAGIC);

	RTW_SYNC(regs, RTW_PHYDELAY, RTW_CRCOUNT);

	rtw_enable_interrupts(sc);

	rtw_pktfilt_load(sc);

	rtw_hwring_setup(sc);

	rtw_io_enable(regs, RTW_CR_RE|RTW_CR_TE, 1);

	ifp->if_flags |= IFF_RUNNING;
	ic->ic_state = IEEE80211_S_INIT;

	RTW_WRITE16(regs, RTW_BSSID16, 0x0);
	RTW_WRITE(regs, RTW_BSSID32, 0x0);

	rtw_resume_ticks(sc);

	rtw_set_nettype(sc, IEEE80211_M_MONITOR);

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		return ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		return ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

out:
	printf("%s: interface not running\n", sc->sc_dev.dv_xname);
	return rc;
}

void
rtw_led_init(struct rtw_regs *regs)
{
	u_int8_t cfg0, cfg1;

	rtw_set_access(regs, RTW_ACCESS_CONFIG);

	cfg0 = RTW_READ8(regs, RTW_CONFIG0);
	cfg0 |= RTW8180_CONFIG0_LEDGPOEN;
	RTW_WRITE8(regs, RTW_CONFIG0, cfg0);

	cfg1 = RTW_READ8(regs, RTW_CONFIG1);
	RTW_DPRINTF(RTW_DEBUG_LED,
	    ("%s: read % from reg[CONFIG1]\n", __func__, cfg1));

	cfg1 &= ~RTW_CONFIG1_LEDS_MASK;
	cfg1 |= RTW_CONFIG1_LEDS_TX_RX;
	RTW_WRITE8(regs, RTW_CONFIG1, cfg1);

	rtw_set_access(regs, RTW_ACCESS_NONE);
}

/* 
 * IEEE80211_S_INIT: 		LED1 off
 *
 * IEEE80211_S_AUTH,
 * IEEE80211_S_ASSOC,
 * IEEE80211_S_SCAN: 		LED1 blinks @ 1 Hz, blinks at 5Hz for tx/rx
 *
 * IEEE80211_S_RUN: 		LED1 on, blinks @ 5Hz for tx/rx
 */
void
rtw_led_newstate(struct rtw_softc *sc, enum ieee80211_state nstate)
{
	struct rtw_led_state *ls;

	ls = &sc->sc_led_state;

	switch (nstate) {
	case IEEE80211_S_INIT:
		rtw_led_init(&sc->sc_regs);
		timeout_del(&ls->ls_slow_ch);
		timeout_del(&ls->ls_fast_ch);
		ls->ls_slowblink = 0;
		ls->ls_actblink = 0;
		ls->ls_default = 0;
		break;
	case IEEE80211_S_SCAN:
		timeout_add_msec(&ls->ls_slow_ch, RTW_LED_SLOW_MSEC);
		timeout_add_msec(&ls->ls_fast_ch, RTW_LED_FAST_MSEC);
		/*FALLTHROUGH*/
	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		ls->ls_default = RTW_LED1;
		ls->ls_actblink = RTW_LED1;
		ls->ls_slowblink = RTW_LED1;
		break;
	case IEEE80211_S_RUN:
		ls->ls_slowblink = 0;
		break;
	}
	rtw_led_set(ls, &sc->sc_regs, sc->sc_hwverid);
}

void
rtw_led_set(struct rtw_led_state *ls, struct rtw_regs *regs, u_int hwverid)
{
	u_int8_t led_condition;
	bus_size_t ofs;
	u_int8_t mask, newval, val;

	led_condition = ls->ls_default;

	if (ls->ls_state & RTW_LED_S_SLOW)
		led_condition ^= ls->ls_slowblink;
	if (ls->ls_state & (RTW_LED_S_RX|RTW_LED_S_TX))
		led_condition ^= ls->ls_actblink;

	RTW_DPRINTF(RTW_DEBUG_LED,
	    ("%s: LED condition %\n", __func__, led_condition));

	switch (hwverid) {
	default:
	case RTW_TCR_HWVERID_RTL8180F:
		ofs = RTW_PSR;
		newval = mask = RTW_PSR_LEDGPO0 | RTW_PSR_LEDGPO1;
		if (led_condition & RTW_LED0)
			newval &= ~RTW_PSR_LEDGPO0;
		if (led_condition & RTW_LED1)
			newval &= ~RTW_PSR_LEDGPO1;
		break;
	case RTW_TCR_HWVERID_RTL8180D:
		ofs = RTW_9346CR;
		mask = RTW_9346CR_EEM_MASK | RTW_9346CR_EEDI | RTW_9346CR_EECS;
		newval = RTW_9346CR_EEM_PROGRAM;
		if (led_condition & RTW_LED0)
			newval |= RTW_9346CR_EEDI;
		if (led_condition & RTW_LED1)
			newval |= RTW_9346CR_EECS;
		break;
	}
	val = RTW_READ8(regs, ofs);
	RTW_DPRINTF(RTW_DEBUG_LED,
	    ("%s: read % from reg[%#02]\n", __func__, val,
	     (u_int *)ofs));
	val &= ~mask;
	val |= newval;
	RTW_WRITE8(regs, ofs, val);
	RTW_DPRINTF(RTW_DEBUG_LED,
	    ("%s: wrote % to reg[%#02]\n", __func__, val,
	     (u_int *)ofs));
	RTW_SYNC(regs, ofs, ofs);
}

void
rtw_led_fastblink(void *arg)
{
	int ostate, s;
	struct rtw_softc *sc = (struct rtw_softc *)arg;
	struct rtw_led_state *ls = &sc->sc_led_state;

	s = splnet();
	ostate = ls->ls_state;
	ls->ls_state ^= ls->ls_event;

	if ((ls->ls_event & RTW_LED_S_TX) == 0)
		ls->ls_state &= ~RTW_LED_S_TX;

	if ((ls->ls_event & RTW_LED_S_RX) == 0)
		ls->ls_state &= ~RTW_LED_S_RX;

	ls->ls_event = 0;

	if (ostate != ls->ls_state)
		rtw_led_set(ls, &sc->sc_regs, sc->sc_hwverid);
	splx(s);

	timeout_add_msec(&ls->ls_fast_ch, RTW_LED_FAST_MSEC);
}

void
rtw_led_slowblink(void *arg)
{
	int s;
	struct rtw_softc *sc = (struct rtw_softc *)arg;
	struct rtw_led_state *ls = &sc->sc_led_state;

	s = splnet();
	ls->ls_state ^= RTW_LED_S_SLOW;
	rtw_led_set(ls, &sc->sc_regs, sc->sc_hwverid);
	splx(s);
	timeout_add_msec(&ls->ls_slow_ch, RTW_LED_SLOW_MSEC);
}

void
rtw_led_attach(struct rtw_led_state *ls, void *arg)
{
	timeout_set(&ls->ls_fast_ch, rtw_led_fastblink, arg);
	timeout_set(&ls->ls_slow_ch, rtw_led_slowblink, arg);
}

int
rtw_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct rtw_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int rc = 0, s;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((sc->sc_flags & RTW_F_ENABLED) != 0) {
				rtw_pktfilt_load(sc);
			} else
				rc = rtw_init(ifp);
		} else if ((sc->sc_flags & RTW_F_ENABLED) != 0)
			rtw_stop(ifp, 1);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (cmd == SIOCADDMULTI)
			rc = ether_addmulti(ifr, &sc->sc_ic.ic_ac);
		else
			rc = ether_delmulti(ifr, &sc->sc_ic.ic_ac);
		if (rc != ENETRESET)
			break;
		if (ifp->if_flags & IFF_RUNNING)
			rtw_pktfilt_load(sc);
		rc = 0;
		break;

	default:
		if ((rc = ieee80211_ioctl(ifp, cmd, data)) == ENETRESET) {
			if ((sc->sc_flags & RTW_F_ENABLED) != 0)
				rc = rtw_init(ifp);
			else
				rc = 0;
		}
		break;
	}

	splx(s);
	return rc;
}

/* Select a transmit ring with at least one h/w and s/w descriptor free.
 * Return 0 on success, -1 on failure.
 */
int
rtw_txring_choose(struct rtw_softc *sc, struct rtw_txsoft_blk **tsbp,
    struct rtw_txdesc_blk **tdbp, int pri)
{
	struct rtw_txsoft_blk *tsb;
	struct rtw_txdesc_blk *tdb;

	KASSERT(pri >= 0 && pri < RTW_NTXPRI);

	tsb = &sc->sc_txsoft_blk[pri];
	tdb = &sc->sc_txdesc_blk[pri];

	if (SIMPLEQ_EMPTY(&tsb->tsb_freeq) || tdb->tdb_nfree == 0) {
		if (tsb->tsb_tx_timer == 0)
			tsb->tsb_tx_timer = 5;
		*tsbp = NULL;
		*tdbp = NULL;
		return -1;
	}
	*tsbp = tsb;
	*tdbp = tdb;
	return 0;
}

struct mbuf *
rtw_80211_dequeue(struct rtw_softc *sc, struct mbuf_queue *ifq, int pri,
    struct rtw_txsoft_blk **tsbp, struct rtw_txdesc_blk **tdbp,
    struct ieee80211_node **nip)
{
	struct mbuf *m;

	if (mq_empty(ifq))
		return NULL;
	if (rtw_txring_choose(sc, tsbp, tdbp, pri) == -1) {
		DPRINTF(sc, RTW_DEBUG_XMIT_RSRC, ("%s: no ring %d descriptor\n",
		    __func__, pri));
		ifq_set_oactive(&sc->sc_if.if_snd);
		sc->sc_if.if_timer = 1;
		return NULL;
	}
	m = mq_dequeue(ifq);
	*nip = m->m_pkthdr.ph_cookie;
	return m;
}

/* Point *mp at the next 802.11 frame to transmit.  Point *tsbp
 * at the driver's selection of transmit control block for the packet.
 */
int
rtw_dequeue(struct ifnet *ifp, struct rtw_txsoft_blk **tsbp,
    struct rtw_txdesc_blk **tdbp, struct mbuf **mp,
    struct ieee80211_node **nip)
{
	struct ieee80211com *ic;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	struct mbuf *m0;
	struct rtw_softc *sc;

	sc = (struct rtw_softc *)ifp->if_softc;
	ic = &sc->sc_ic;

	DPRINTF(sc, RTW_DEBUG_XMIT,
	    ("%s: enter %s\n", sc->sc_dev.dv_xname, __func__));

	if (ic->ic_state == IEEE80211_S_RUN &&
	    (*mp = rtw_80211_dequeue(sc, &sc->sc_beaconq, RTW_TXPRIBCN, tsbp,
	    tdbp, nip)) != NULL) {
		DPRINTF(sc, RTW_DEBUG_XMIT, ("%s: dequeue beacon frame\n",
		    __func__));
		return 0;
	}

	if ((*mp = rtw_80211_dequeue(sc, &ic->ic_mgtq, RTW_TXPRIMD, tsbp,
	    tdbp, nip)) != NULL) {
		DPRINTF(sc, RTW_DEBUG_XMIT, ("%s: dequeue mgt frame\n",
		    __func__));
		return 0;
	}

	if (sc->sc_ic.ic_state != IEEE80211_S_RUN) {
		DPRINTF(sc, RTW_DEBUG_XMIT, ("%s: not running\n", __func__));
		return 0;
	}

	if ((*mp = rtw_80211_dequeue(sc, &ic->ic_pwrsaveq, RTW_TXPRIHI,
	    tsbp, tdbp, nip)) != NULL) {
		DPRINTF(sc, RTW_DEBUG_XMIT, ("%s: dequeue pwrsave frame\n",
		    __func__));
		return 0;
	}

	if (ic->ic_state != IEEE80211_S_RUN) {
		DPRINTF(sc, RTW_DEBUG_XMIT, ("%s: not running\n", __func__));
		return 0;
	}

	*mp = NULL;

	m0 = ifq_deq_begin(&ifp->if_snd);
	if (m0 == NULL) {
		DPRINTF(sc, RTW_DEBUG_XMIT, ("%s: no frame ready\n",
		    __func__));
		return 0;
	}

	if (rtw_txring_choose(sc, tsbp, tdbp, RTW_TXPRIMD) == -1) {
		DPRINTF(sc, RTW_DEBUG_XMIT, ("%s: no descriptor\n", __func__));
		ifq_deq_rollback(&ifp->if_snd, m0);
		ifq_set_oactive(&ifp->if_snd);
		sc->sc_if.if_timer = 1;
		return 0;
	}

	ifq_deq_commit(&ifp->if_snd, m0);
	if (m0 == NULL) {
		DPRINTF(sc, RTW_DEBUG_XMIT, ("%s: no frame/ring ready\n",
		    __func__));
		return 0;
	}
	DPRINTF(sc, RTW_DEBUG_XMIT, ("%s: dequeue data frame\n", __func__));
#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif
	if ((m0 = ieee80211_encap(ifp, m0, nip)) == NULL) {
		DPRINTF(sc, RTW_DEBUG_XMIT,
		    ("%s: encap error\n", __func__));
		ifp->if_oerrors++;
		return -1;
	}

	/* XXX should do WEP in hardware */
	if (ic->ic_flags & IEEE80211_F_WEPON) {
		wh = mtod(m0, struct ieee80211_frame *);
		k = ieee80211_get_txkey(ic, wh, *nip);
		if ((m0 = ieee80211_encrypt(ic, m0, k)) == NULL)
			return -1;
	}

	DPRINTF(sc, RTW_DEBUG_XMIT, ("%s: leave\n", __func__));
	*mp = m0;
	return 0;
}

int
rtw_seg_too_short(bus_dmamap_t dmamap)
{
	int i;
	for (i = 0; i < dmamap->dm_nsegs; i++) {
		if (dmamap->dm_segs[i].ds_len < 4) {
			printf("%s: segment too short\n", __func__);
			return 1;
		}
	}
	return 0;
}

/* TBD factor with atw_start */
struct mbuf *
rtw_dmamap_load_txbuf(bus_dma_tag_t dmat, bus_dmamap_t dmam, struct mbuf *chain,
    u_int ndescfree, short *ifflagsp, const char *dvname)
{
	int first, rc;
	struct mbuf *m, *m0;

	m0 = chain;

	/*
	 * Load the DMA map.  Copy and try (once) again if the packet
	 * didn't fit in the allotted number of segments.
	 */
	for (first = 1;
	     ((rc = bus_dmamap_load_mbuf(dmat, dmam, m0,
	     BUS_DMA_WRITE|BUS_DMA_NOWAIT)) != 0 ||
	     dmam->dm_nsegs > ndescfree || rtw_seg_too_short(dmam)) && first;
	     first = 0) {
		if (rc == 0)
			bus_dmamap_unload(dmat, dmam);
		MGETHDR(m, M_DONTWAIT, MT_DATA);
		if (m == NULL) {
			printf("%s: unable to allocate Tx mbuf\n",
			    dvname);
			break;
		}
		if (m0->m_pkthdr.len > MHLEN) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				printf("%s: cannot allocate Tx cluster\n",
				    dvname);
				m_freem(m);
				break;
			}
		}
		m_copydata(m0, 0, m0->m_pkthdr.len, mtod(m, caddr_t));
		m->m_pkthdr.len = m->m_len = m0->m_pkthdr.len;
		m_freem(m0);
		m0 = m;
		m = NULL;
	}
	if (rc != 0) {
		printf("%s: cannot load Tx buffer, rc = %d\n", dvname, rc);
		m_freem(m0);
		return NULL;
	} else if (rtw_seg_too_short(dmam)) {
		printf("%s: cannot load Tx buffer, segment too short\n",
		    dvname);
		bus_dmamap_unload(dmat, dmam);
		m_freem(m0);
		return NULL;
	} else if (dmam->dm_nsegs > ndescfree) {
		printf("%s: too many tx segments\n", dvname);
		bus_dmamap_unload(dmat, dmam);
		m_freem(m0);
		return NULL;
	}
	return m0;
}


/*
 * Arguments in:
 *
 * paylen:  payload length (no FCS, no WEP header)
 *
 * hdrlen:  header length
 *
 * rate:    MSDU speed, units 500kb/s
 *
 * flags:   IEEE80211_F_SHPREAMBLE (use short preamble),
 *          IEEE80211_F_SHSLOT (use short slot length)
 *
 * Arguments out:
 *
 * d:       802.11 Duration field for RTS,
 *          802.11 Duration field for data frame,
 *          PLCP Length for data frame,
 *          residual octets at end of data slot
 */
int
rtw_compute_duration1(int len, int use_ack, uint32_t flags, int rate,
    struct rtw_duration *d)
{
	int pre, ctsrate;
	int ack, bitlen, data_dur, remainder;

	/* RTS reserves medium for SIFS | CTS | SIFS | (DATA) | SIFS | ACK
	 * DATA reserves medium for SIFS | ACK
	 *
	 * XXXMYC: no ACK on multicast/broadcast or control packets
	 */

	bitlen = len * 8;

	pre = IEEE80211_DUR_DS_SIFS;
	if ((flags & IEEE80211_F_SHPREAMBLE) != 0)
		pre += IEEE80211_DUR_DS_SHORT_PREAMBLE +
		    IEEE80211_DUR_DS_FAST_PLCPHDR;
	else
		pre += IEEE80211_DUR_DS_LONG_PREAMBLE +
		    IEEE80211_DUR_DS_SLOW_PLCPHDR;

	d->d_residue = 0;
	data_dur = (bitlen * 2) / rate;
	remainder = (bitlen * 2) % rate;
	if (remainder != 0) {
		d->d_residue = (rate - remainder) / 16;
		data_dur++;
	}

	switch (rate) {
	case 2:		/* 1 Mb/s */
	case 4:		/* 2 Mb/s */
		/* 1 - 2 Mb/s WLAN: send ACK/CTS at 1 Mb/s */
		ctsrate = 2;
		break;
	case 11:	/* 5.5 Mb/s */
	case 22:	/* 11  Mb/s */
	case 44:	/* 22  Mb/s */
		/* 5.5 - 11 Mb/s WLAN: send ACK/CTS at 2 Mb/s */
		ctsrate = 4;
		break;
	default:
		/* TBD */
		return -1;
	}

	d->d_plcp_len = data_dur;

	ack = (use_ack) ? pre + (IEEE80211_DUR_DS_SLOW_ACK * 2) / ctsrate : 0;

	d->d_rts_dur =
	    pre + (IEEE80211_DUR_DS_SLOW_CTS * 2) / ctsrate +
	    pre + data_dur +
	    ack;

	d->d_data_dur = ack;

	return 0;
}

/*
 * Arguments in:
 *
 * wh:      802.11 header
 *
 * len: packet length 
 *
 * rate:    MSDU speed, units 500kb/s
 *
 * fraglen: fragment length, set to maximum (or higher) for no
 *          fragmentation
 *
 * flags:   IEEE80211_F_WEPON (hardware adds WEP),
 *          IEEE80211_F_SHPREAMBLE (use short preamble),
 *          IEEE80211_F_SHSLOT (use short slot length)
 *
 * Arguments out:
 *
 * d0: 802.11 Duration fields (RTS/Data), PLCP Length, Service fields
 *     of first/only fragment
 *
 * dn: 802.11 Duration fields (RTS/Data), PLCP Length, Service fields
 *     of first/only fragment
 */
int
rtw_compute_duration(struct ieee80211_frame *wh, int len, uint32_t flags,
    int fraglen, int rate, struct rtw_duration *d0, struct rtw_duration *dn,
    int *npktp, int debug)
{
	int ack, rc;
	int firstlen, hdrlen, lastlen, lastlen0, npkt, overlen, paylen;

	if (ieee80211_has_addr4(wh))
		hdrlen = sizeof(struct ieee80211_frame_addr4);
	else
		hdrlen = sizeof(struct ieee80211_frame);

	paylen = len - hdrlen;

	if ((flags & IEEE80211_F_WEPON) != 0)
		overlen = IEEE80211_WEP_TOTLEN + IEEE80211_CRC_LEN;
	else
		overlen = IEEE80211_CRC_LEN;

	npkt = paylen / fraglen;
	lastlen0 = paylen % fraglen;

	if (npkt == 0)			/* no fragments */
		lastlen = paylen + overlen;
	else if (lastlen0 != 0) {	/* a short "tail" fragment */
		lastlen = lastlen0 + overlen;
		npkt++;
	} else				/* full-length "tail" fragment */
		lastlen = fraglen + overlen;

	if (npktp != NULL)
		*npktp = npkt;

	if (npkt > 1)
		firstlen = fraglen + overlen;
	else
		firstlen = paylen + overlen;

	if (debug) {
		printf("%s: npkt %d firstlen %d lastlen0 %d lastlen %d "
		    "fraglen %d overlen %d len %d rate %d flags %08x\n",
		    __func__, npkt, firstlen, lastlen0, lastlen, fraglen,
		    overlen, len, rate, flags);
	}

	ack = !IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (wh->i_fc[1] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL;

	rc = rtw_compute_duration1(firstlen + hdrlen, ack, flags, rate, d0);
	if (rc == -1)
		return rc;

	if (npkt <= 1) {
		*dn = *d0;
		return 0;
	}
	return rtw_compute_duration1(lastlen + hdrlen, ack, flags, rate, dn);
}

#ifdef RTW_DEBUG
void
rtw_print_txdesc(struct rtw_softc *sc, const char *action,
    struct rtw_txsoft *ts, struct rtw_txdesc_blk *tdb, int desc)
{
	struct rtw_txdesc *td = &tdb->tdb_desc[desc];
	DPRINTF(sc, RTW_DEBUG_XMIT_DESC, ("%s: %p %s txdesc[%d] next %#08x "
	    "buf %#08x ctl0 %#08x ctl1 %#08x len %#08x\n",
	    sc->sc_dev.dv_xname, ts, action, desc,
	    letoh32(td->td_buf), letoh32(td->td_next),
	    letoh32(td->td_ctl0), letoh32(td->td_ctl1),
	    letoh32(td->td_len)));
}
#endif /* RTW_DEBUG */

void
rtw_start(struct ifnet *ifp)
{
	uint8_t tppoll;
	int desc, i, lastdesc, npkt, rate;
	uint32_t proto_ctl0, ctl0, ctl1;
	bus_dmamap_t		dmamap;
	struct ieee80211com	*ic;
	struct ieee80211_frame	*wh;
	struct ieee80211_node	*ni;
	struct mbuf		*m0;
	struct rtw_softc	*sc;
	struct rtw_duration	*d0;
	struct rtw_txsoft_blk	*tsb;
	struct rtw_txdesc_blk	*tdb;
	struct rtw_txsoft	*ts;
	struct rtw_txdesc	*td;

	sc = (struct rtw_softc *)ifp->if_softc;
	ic = &sc->sc_ic;

	DPRINTF(sc, RTW_DEBUG_XMIT,
	    ("%s: enter %s\n", sc->sc_dev.dv_xname, __func__));

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		goto out;

	/* XXX do real rate control */
	proto_ctl0 = RTW_TXCTL0_RTSRATE_1MBPS;

	if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) != 0)
		proto_ctl0 |= RTW_TXCTL0_SPLCP;

	for (;;) {
		if (rtw_dequeue(ifp, &tsb, &tdb, &m0, &ni) == -1)
			continue;
		if (m0 == NULL)
			break;
		ts = SIMPLEQ_FIRST(&tsb->tsb_freeq);

		dmamap = ts->ts_dmamap;

		m0 = rtw_dmamap_load_txbuf(sc->sc_dmat, dmamap, m0,
		    tdb->tdb_nfree, &ifp->if_flags, sc->sc_dev.dv_xname);

		if (m0 == NULL || dmamap->dm_nsegs == 0) {
			DPRINTF(sc, RTW_DEBUG_XMIT,
			    ("%s: fail dmamap load\n", __func__));
			goto post_dequeue_err;
		}

		wh = mtod(m0, struct ieee80211_frame *);

		/* XXX do real rate control */
		if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
		    IEEE80211_FC0_TYPE_MGT)
			rate = 2;
		else
			rate = MAX(2, ieee80211_get_rate(ic));

#ifdef RTW_DEBUG
		if ((sc->sc_if.if_flags & (IFF_DEBUG|IFF_LINK2)) ==
		    (IFF_DEBUG|IFF_LINK2)) {
			ieee80211_dump_pkt(mtod(m0, uint8_t *),
			    (dmamap->dm_nsegs == 1) ? m0->m_pkthdr.len
			    : sizeof(wh), rate, 0);
		}
#endif /* RTW_DEBUG */
		ctl0 = proto_ctl0 |
		    LSHIFT(m0->m_pkthdr.len, RTW_TXCTL0_TPKTSIZE_MASK);

		switch (rate) {
		default:
		case 2:
			ctl0 |= RTW_TXCTL0_RATE_1MBPS;
			break;
		case 4:
			ctl0 |= RTW_TXCTL0_RATE_2MBPS;
			break;
		case 11:
			ctl0 |= RTW_TXCTL0_RATE_5MBPS;
			break;
		case 22:
			ctl0 |= RTW_TXCTL0_RATE_11MBPS;
			break;
		}

		/* XXX >= ? Compare after fragmentation? */
		if (m0->m_pkthdr.len > ic->ic_rtsthreshold)
			ctl0 |= RTW_TXCTL0_RTSEN;

		if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
		    IEEE80211_FC0_TYPE_MGT) {
			ctl0 &= ~(RTW_TXCTL0_SPLCP | RTW_TXCTL0_RTSEN);
			if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
			    IEEE80211_FC0_SUBTYPE_BEACON)
				ctl0 |= RTW_TXCTL0_BEACON;
		}

		if (rtw_compute_duration(wh, m0->m_pkthdr.len,
		    ic->ic_flags & ~IEEE80211_F_WEPON, ic->ic_fragthreshold,
		    rate, &ts->ts_d0, &ts->ts_dn, &npkt,
		    (sc->sc_if.if_flags & (IFF_DEBUG|IFF_LINK2)) ==
		    (IFF_DEBUG|IFF_LINK2)) == -1) {
			DPRINTF(sc, RTW_DEBUG_XMIT,
			    ("%s: fail compute duration\n", __func__));
			goto post_load_err;
		}

		d0 = &ts->ts_d0;

		*(uint16_t*)wh->i_dur = htole16(d0->d_data_dur);

		ctl1 = LSHIFT(d0->d_plcp_len, RTW_TXCTL1_LENGTH_MASK) |
		    LSHIFT(d0->d_rts_dur, RTW_TXCTL1_RTSDUR_MASK);

		if (d0->d_residue)
			ctl1 |= RTW_TXCTL1_LENGEXT;

		/* TBD fragmentation */

		ts->ts_first = tdb->tdb_next;

		rtw_txdescs_sync(tdb, ts->ts_first, dmamap->dm_nsegs,
		    BUS_DMASYNC_PREWRITE);

		KASSERT(ts->ts_first < tdb->tdb_ndesc);

#if NBPFILTER > 0
		if (ic->ic_rawbpf != NULL)
			bpf_mtap((caddr_t)ic->ic_rawbpf, m0,
			    BPF_DIRECTION_OUT);

		if (sc->sc_radiobpf != NULL) {
			struct mbuf mb;
			struct rtw_tx_radiotap_header *rt = &sc->sc_txtap;

			rt->rt_flags = 0;
			rt->rt_rate = rate;
			rt->rt_chan_freq =
			    htole16(ic->ic_bss->ni_chan->ic_freq);
			rt->rt_chan_flags =
			    htole16(ic->ic_bss->ni_chan->ic_flags);

			mb.m_data = (caddr_t)rt;
			mb.m_len = sizeof(sc->sc_txtapu);
			mb.m_next = m0;
			mb.m_nextpkt = NULL;
			mb.m_type = 0;
			mb.m_flags = 0;
			bpf_mtap(sc->sc_radiobpf, &mb, BPF_DIRECTION_OUT);

		}
#endif /* NBPFILTER > 0 */

		for (i = 0, lastdesc = desc = ts->ts_first;
		     i < dmamap->dm_nsegs;
		     i++, desc = RTW_NEXT_IDX(tdb, desc)) {
			if (dmamap->dm_segs[i].ds_len > RTW_TXLEN_LENGTH_MASK) {
				DPRINTF(sc, RTW_DEBUG_XMIT_DESC,
				    ("%s: seg too long\n", __func__));
				goto post_load_err;
			}
			td = &tdb->tdb_desc[desc];
			td->td_ctl0 = htole32(ctl0);
			if (i != 0)
				td->td_ctl0 |= htole32(RTW_TXCTL0_OWN);
			td->td_ctl1 = htole32(ctl1);
			td->td_buf = htole32(dmamap->dm_segs[i].ds_addr);
			td->td_len = htole32(dmamap->dm_segs[i].ds_len);
			lastdesc = desc;
#ifdef RTW_DEBUG
			rtw_print_txdesc(sc, "load", ts, tdb, desc);
#endif /* RTW_DEBUG */
		}

		KASSERT(desc < tdb->tdb_ndesc);

		ts->ts_ni = ni;
		ts->ts_mbuf = m0;
		ts->ts_last = lastdesc;
		tdb->tdb_desc[ts->ts_last].td_ctl0 |= htole32(RTW_TXCTL0_LS);
		tdb->tdb_desc[ts->ts_first].td_ctl0 |=
		    htole32(RTW_TXCTL0_FS);

#ifdef RTW_DEBUG
		rtw_print_txdesc(sc, "FS on", ts, tdb, ts->ts_first);
		rtw_print_txdesc(sc, "LS on", ts, tdb, ts->ts_last);
#endif /* RTW_DEBUG */

		tdb->tdb_nfree -= dmamap->dm_nsegs;
		tdb->tdb_next = desc;

		rtw_txdescs_sync(tdb, ts->ts_first, dmamap->dm_nsegs,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		tdb->tdb_desc[ts->ts_first].td_ctl0 |=
		    htole32(RTW_TXCTL0_OWN);

#ifdef RTW_DEBUG
		rtw_print_txdesc(sc, "OWN on", ts, tdb, ts->ts_first);
#endif /* RTW_DEBUG */

		rtw_txdescs_sync(tdb, ts->ts_first, 1,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		SIMPLEQ_REMOVE_HEAD(&tsb->tsb_freeq, ts_q);
		SIMPLEQ_INSERT_TAIL(&tsb->tsb_dirtyq, ts, ts_q);

		if (tsb != &sc->sc_txsoft_blk[RTW_TXPRIBCN])
			sc->sc_led_state.ls_event |= RTW_LED_S_TX;
		tsb->tsb_tx_timer = 5;
		ifp->if_timer = 1;
		tppoll = RTW_READ8(&sc->sc_regs, RTW_TPPOLL);
		tppoll &= ~RTW_TPPOLL_SALL;
		tppoll |= tsb->tsb_poll & RTW_TPPOLL_ALL;
		RTW_WRITE8(&sc->sc_regs, RTW_TPPOLL, tppoll);
		RTW_SYNC(&sc->sc_regs, RTW_TPPOLL, RTW_TPPOLL);
	}
out:
	DPRINTF(sc, RTW_DEBUG_XMIT, ("%s: leave\n", __func__));
	return;
post_load_err:
	bus_dmamap_unload(sc->sc_dmat, dmamap);
	m_freem(m0);
post_dequeue_err:
	ieee80211_release_node(&sc->sc_ic, ni);
	return;
}

void
rtw_idle(struct rtw_regs *regs)
{
	int active;

	/* request stop DMA; wait for packets to stop transmitting. */

	RTW_WRITE8(regs, RTW_TPPOLL, RTW_TPPOLL_SALL);
	RTW_WBR(regs, RTW_TPPOLL, RTW_TPPOLL);

	for (active = 0; active < 300 &&
	     (RTW_READ8(regs, RTW_TPPOLL) & RTW_TPPOLL_ACTIVE) != 0; active++)
		DELAY(10);
	RTW_DPRINTF(RTW_DEBUG_BUGS,
	    ("%s: transmit DMA idle in %dus\n", __func__, active * 10));
}

void
rtw_watchdog(struct ifnet *ifp)
{
	int pri, tx_timeouts = 0;
	struct rtw_softc *sc;
	struct rtw_txsoft_blk *tsb;

	sc = ifp->if_softc;

	ifp->if_timer = 0;

	if ((sc->sc_flags & RTW_F_ENABLED) == 0)
		return;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		tsb = &sc->sc_txsoft_blk[pri];

		if (tsb->tsb_tx_timer == 0)
			continue;
		else if (--tsb->tsb_tx_timer == 0) {
			if (SIMPLEQ_EMPTY(&tsb->tsb_dirtyq))
				continue;
			RTW_DPRINTF(RTW_DEBUG_BUGS,
			    ("%s: transmit timeout, priority %d\n",
			    ifp->if_xname, pri));
			ifp->if_oerrors++;
			tx_timeouts++;
		} else
			ifp->if_timer = 1;
	}

	if (tx_timeouts > 0) {
		/* Stop Tx DMA, disable xmtr, flush Tx rings, enable xmtr,
		 * reset s/w tx-ring pointers, and start transmission.
		 *
		 * TBD Stop/restart just the broken rings?
		 */
		rtw_idle(&sc->sc_regs);
		rtw_io_enable(&sc->sc_regs, RTW_CR_TE, 0);
		rtw_txdescs_reset(sc);
		rtw_io_enable(&sc->sc_regs, RTW_CR_TE, 1);
		rtw_txring_fixup(sc);
		rtw_start(ifp);
	}
	ieee80211_watchdog(ifp);
}

void
rtw_next_scan(void *arg)
{
	struct rtw_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	/* don't call rtw_start w/o network interrupts blocked */
	s = splnet();
	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);
	splx(s);
}

void
rtw_join_bss(struct rtw_softc *sc, u_int8_t *bssid, u_int16_t intval0)
{
	uint16_t bcnitv, bintritv, intval;
	int i;
	struct rtw_regs *regs = &sc->sc_regs;

	for (i = 0; i < IEEE80211_ADDR_LEN; i++)
		RTW_WRITE8(regs, RTW_BSSID + i, bssid[i]);

	RTW_SYNC(regs, RTW_BSSID16, RTW_BSSID32);

	rtw_set_access(regs, RTW_ACCESS_CONFIG);

	intval = MIN(intval0, PRESHIFT(RTW_BCNITV_BCNITV_MASK));

	bcnitv = RTW_READ16(regs, RTW_BCNITV) & ~RTW_BCNITV_BCNITV_MASK;
	bcnitv |= LSHIFT(intval, RTW_BCNITV_BCNITV_MASK);
	RTW_WRITE16(regs, RTW_BCNITV, bcnitv);
	/* interrupt host 1ms before the TBTT */
	bintritv = RTW_READ16(regs, RTW_BINTRITV) & ~RTW_BINTRITV_BINTRITV;
	bintritv |= LSHIFT(1000, RTW_BINTRITV_BINTRITV);
	RTW_WRITE16(regs, RTW_BINTRITV, bintritv);
	/* magic from Linux */
	RTW_WRITE16(regs, RTW_ATIMWND, LSHIFT(1, RTW_ATIMWND_ATIMWND));
	RTW_WRITE16(regs, RTW_ATIMTRITV, LSHIFT(2, RTW_ATIMTRITV_ATIMTRITV));
	rtw_set_access(regs, RTW_ACCESS_NONE);

	/* TBD WEP */
	RTW_WRITE8(regs, RTW8180_SCR, 0);

	rtw_io_enable(regs, RTW_CR_RE | RTW_CR_TE, 1);
}

/* Synchronize the hardware state with the software state. */
int
rtw_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ifnet *ifp = &ic->ic_if;
	struct rtw_softc *sc = ifp->if_softc;
	enum ieee80211_state ostate;
	int error;

	ostate = ic->ic_state;

	rtw_led_newstate(sc, nstate);

	if (nstate == IEEE80211_S_INIT) {
		timeout_del(&sc->sc_scan_to);
		sc->sc_cur_chan = IEEE80211_CHAN_ANY;
		return (*sc->sc_mtbl.mt_newstate)(ic, nstate, arg);
	}

	if (ostate == IEEE80211_S_INIT && nstate != IEEE80211_S_INIT)
		rtw_pwrstate(sc, RTW_ON);

	if ((error = rtw_tune(sc)) != 0)
		return error;

	switch (nstate) {
	case IEEE80211_S_INIT:
		panic("%s: unexpected state IEEE80211_S_INIT", __func__);
		break;
	case IEEE80211_S_SCAN:
		if (ostate != IEEE80211_S_SCAN) {
			bzero(ic->ic_bss->ni_bssid, IEEE80211_ADDR_LEN);
			rtw_set_nettype(sc, IEEE80211_M_MONITOR);
		}

		timeout_add_msec(&sc->sc_scan_to, rtw_dwelltime);

		break;
	case IEEE80211_S_RUN:
		switch (ic->ic_opmode) {
#ifndef IEEE80211_STA_ONLY
		case IEEE80211_M_HOSTAP:
		case IEEE80211_M_IBSS:
			rtw_set_nettype(sc, IEEE80211_M_MONITOR);
			/*FALLTHROUGH*/
		case IEEE80211_M_AHDEMO:
#endif
		case IEEE80211_M_STA:
			rtw_join_bss(sc, ic->ic_bss->ni_bssid,
			    ic->ic_bss->ni_intval);
			break;
		default:
			break;
		}
		rtw_set_nettype(sc, ic->ic_opmode);
		break;
	case IEEE80211_S_ASSOC:
	case IEEE80211_S_AUTH:
		break;
	}

	if (nstate != IEEE80211_S_SCAN)
		timeout_del(&sc->sc_scan_to);

	return (*sc->sc_mtbl.mt_newstate)(ic, nstate, arg);
}

/* Extend a 32-bit TSF timestamp to a 64-bit timestamp. */
uint64_t
rtw_tsf_extend(struct rtw_regs *regs, u_int32_t rstamp)
{
	u_int32_t tsftl, tsfth;

	tsfth = RTW_READ(regs, RTW_TSFTRH);
	tsftl = RTW_READ(regs, RTW_TSFTRL);
	if (tsftl < rstamp)	/* Compensate for rollover. */
		tsfth--;
	return ((u_int64_t)tsfth << 32) | rstamp;
}

#ifndef IEEE80211_STA_ONLY
void
rtw_ibss_merge(struct rtw_softc *sc, struct ieee80211_node *ni,
    u_int32_t rstamp)
{
	u_int8_t tppoll;
	struct ieee80211com *ic = &sc->sc_ic;

	if (ieee80211_ibss_merge(ic, ni,
	    rtw_tsf_extend(&sc->sc_regs, rstamp)) == ENETRESET) {
		/* Stop beacon queue.  Kick state machine to synchronize
		 * with the new IBSS.
		 */
		tppoll = RTW_READ8(&sc->sc_regs, RTW_TPPOLL);
		tppoll |= RTW_TPPOLL_SBQ;
		RTW_WRITE8(&sc->sc_regs, RTW_TPPOLL, tppoll);
		(void)ieee80211_new_state(&sc->sc_ic, IEEE80211_S_RUN, -1);
	}
	return;
}

void
rtw_recv_mgmt(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, struct ieee80211_rxinfo *rxi, int subtype)
{
	struct rtw_softc *sc = (struct rtw_softc*)ic->ic_softc;

	(*sc->sc_mtbl.mt_recv_mgmt)(ic, m, ni, rxi, subtype);

	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
	case IEEE80211_FC0_SUBTYPE_BEACON:
		if (ic->ic_opmode != IEEE80211_M_IBSS ||
		    ic->ic_state != IEEE80211_S_RUN)
			return;
		rtw_ibss_merge(sc, ni, rxi->rxi_tstamp);
		break;
	default:
		break;
	}
	return;
}
#endif	/* IEEE80211_STA_ONLY */

struct ieee80211_node *
rtw_node_alloc(struct ieee80211com *ic)
{
	struct rtw_softc *sc = (struct rtw_softc *)ic->ic_if.if_softc;
	struct ieee80211_node *ni = (*sc->sc_mtbl.mt_node_alloc)(ic);

	DPRINTF(sc, RTW_DEBUG_NODE,
	    ("%s: alloc node %p\n", sc->sc_dev.dv_xname, ni));
	return ni;
}

void
rtw_node_free(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct rtw_softc *sc = (struct rtw_softc *)ic->ic_if.if_softc;

	DPRINTF(sc, RTW_DEBUG_NODE,
	    ("%s: freeing node %p %s\n", sc->sc_dev.dv_xname, ni,
	    ether_sprintf(ni->ni_bssid)));
	(*sc->sc_mtbl.mt_node_free)(ic, ni);
}

int
rtw_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_RUNNING|IFF_UP)) ==
		    (IFF_RUNNING|IFF_UP))
			rtw_init(ifp);		/* XXX lose error */
		error = 0;
	}
	return error;
}

void
rtw_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct rtw_softc *sc = ifp->if_softc;

	if ((sc->sc_flags & RTW_F_ENABLED) == 0) {
		imr->ifm_active = IFM_IEEE80211 | IFM_NONE;
		imr->ifm_status = 0;
		return;
	}
	ieee80211_media_status(ifp, imr);
}

int
rtw_activate(struct device *self, int act)
{
	struct rtw_softc *sc = (struct rtw_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING) {
			rtw_stop(ifp, 1);
			if (sc->sc_power != NULL)
				(*sc->sc_power)(sc, act);
		}
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP) {
			if (sc->sc_power != NULL)
				(*sc->sc_power)(sc, act);
			rtw_init(ifp);
		}
		break;
	}
	return 0;
}

int
rtw_txsoft_blk_setup(struct rtw_txsoft_blk *tsb, u_int qlen)
{
	SIMPLEQ_INIT(&tsb->tsb_dirtyq);
	SIMPLEQ_INIT(&tsb->tsb_freeq);
	tsb->tsb_ndesc = qlen;
	tsb->tsb_desc = mallocarray(qlen, sizeof(*tsb->tsb_desc), M_DEVBUF,
	    M_NOWAIT);
	if (tsb->tsb_desc == NULL)
		return ENOMEM;
	return 0;
}

void
rtw_txsoft_blk_cleanup_all(struct rtw_softc *sc)
{
	int pri;
	struct rtw_txsoft_blk *tsb;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		tsb = &sc->sc_txsoft_blk[pri];
		free(tsb->tsb_desc, M_DEVBUF, 0);
		tsb->tsb_desc = NULL;
	}
}

int
rtw_txsoft_blk_setup_all(struct rtw_softc *sc)
{
	int pri, rc = 0;
	int qlen[RTW_NTXPRI] =
	     {RTW_TXQLENLO, RTW_TXQLENMD, RTW_TXQLENHI, RTW_TXQLENBCN};
	struct rtw_txsoft_blk *tsbs;

	tsbs = sc->sc_txsoft_blk;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		rc = rtw_txsoft_blk_setup(&tsbs[pri], qlen[pri]);
		if (rc != 0)
			break;
	}
	tsbs[RTW_TXPRILO].tsb_poll = RTW_TPPOLL_LPQ | RTW_TPPOLL_SLPQ;
	tsbs[RTW_TXPRIMD].tsb_poll = RTW_TPPOLL_NPQ | RTW_TPPOLL_SNPQ;
	tsbs[RTW_TXPRIHI].tsb_poll = RTW_TPPOLL_HPQ | RTW_TPPOLL_SHPQ;
	tsbs[RTW_TXPRIBCN].tsb_poll = RTW_TPPOLL_BQ | RTW_TPPOLL_SBQ;
	return rc;
}

void
rtw_txdesc_blk_setup(struct rtw_txdesc_blk *tdb, struct rtw_txdesc *desc,
    u_int ndesc, bus_addr_t ofs, bus_addr_t physbase)
{
	tdb->tdb_ndesc = ndesc;
	tdb->tdb_desc = desc;
	tdb->tdb_physbase = physbase;
	tdb->tdb_ofs = ofs;

	bzero(tdb->tdb_desc, sizeof(tdb->tdb_desc[0]) * tdb->tdb_ndesc);

	rtw_txdesc_blk_init(tdb);
	tdb->tdb_next = 0;
}

void
rtw_txdesc_blk_setup_all(struct rtw_softc *sc)
{
	rtw_txdesc_blk_setup(&sc->sc_txdesc_blk[RTW_TXPRILO],
	    &sc->sc_descs->hd_txlo[0], RTW_NTXDESCLO,
	    RTW_RING_OFFSET(hd_txlo), RTW_RING_BASE(sc, hd_txlo));

	rtw_txdesc_blk_setup(&sc->sc_txdesc_blk[RTW_TXPRIMD],
	    &sc->sc_descs->hd_txmd[0], RTW_NTXDESCMD,
	    RTW_RING_OFFSET(hd_txmd), RTW_RING_BASE(sc, hd_txmd));

	rtw_txdesc_blk_setup(&sc->sc_txdesc_blk[RTW_TXPRIHI],
	    &sc->sc_descs->hd_txhi[0], RTW_NTXDESCHI,
	    RTW_RING_OFFSET(hd_txhi), RTW_RING_BASE(sc, hd_txhi));

	rtw_txdesc_blk_setup(&sc->sc_txdesc_blk[RTW_TXPRIBCN],
	    &sc->sc_descs->hd_bcn[0], RTW_NTXDESCBCN,
	    RTW_RING_OFFSET(hd_bcn), RTW_RING_BASE(sc, hd_bcn));
}

int
rtw_rf_attach(struct rtw_softc *sc, int rfchipid)
{
	struct rtw_bbpset *bb = &sc->sc_bbpset;
	int notsup = 0;
	const char *rfname, *paname = NULL;
	char scratch[sizeof("unknown 0xXX")];

	switch (rfchipid) {
	case RTW_RFCHIPID_RTL8225:
		rfname = "RTL8225";
		sc->sc_pwrstate_cb = rtw_rtl_pwrstate;
		sc->sc_rf_init = rtw_rtl8255_init;
		sc->sc_rf_pwrstate = rtw_rtl8225_pwrstate;
		sc->sc_rf_tune = rtw_rtl8225_tune;
		sc->sc_rf_txpower = rtw_rtl8225_txpower;
		break;
	case RTW_RFCHIPID_RTL8255:
		rfname = "RTL8255";
		sc->sc_pwrstate_cb = rtw_rtl_pwrstate;
		sc->sc_rf_init = rtw_rtl8255_init;
		sc->sc_rf_pwrstate = rtw_rtl8255_pwrstate;
		sc->sc_rf_tune = rtw_rtl8255_tune;
		sc->sc_rf_txpower = rtw_rtl8255_txpower;
		break;
	case RTW_RFCHIPID_MAXIM2820:
		rfname = "MAX2820";	/* guess */
		paname = "MAX2422";	/* guess */
		/* XXX magic */
		bb->bb_antatten = RTW_BBP_ANTATTEN_MAXIM_MAGIC;
		bb->bb_chestlim =	0x00;
		bb->bb_chsqlim =	0x9f;
		bb->bb_ifagcdet =	0x64;
		bb->bb_ifagcini =	0x90;
		bb->bb_ifagclimit =	0x1a;
		bb->bb_lnadet =		0xf8;
		bb->bb_sys1 =		0x88;
		bb->bb_sys2 =		0x47;
		bb->bb_sys3 =		0x9b;
		bb->bb_trl =		0x88;
		bb->bb_txagc =		0x08;
		sc->sc_pwrstate_cb = rtw_maxim_pwrstate;
		sc->sc_rf_init = rtw_max2820_init;
		sc->sc_rf_pwrstate = rtw_max2820_pwrstate;
		sc->sc_rf_tune = rtw_max2820_tune;
		sc->sc_rf_txpower = rtw_max2820_txpower;
		break;
	case RTW_RFCHIPID_PHILIPS:
		rfname = "SA2400A";
		paname = "SA2411";
		/* XXX magic */
		bb->bb_antatten = RTW_BBP_ANTATTEN_PHILIPS_MAGIC;
		bb->bb_chestlim =	0x00;
		bb->bb_chsqlim =	0xa0;
		bb->bb_ifagcdet =	0x64;
		bb->bb_ifagcini =	0x90;
		bb->bb_ifagclimit =	0x1a;
		bb->bb_lnadet =		0xe0;
		bb->bb_sys1 =		0x98;
		bb->bb_sys2 =		0x47;
		bb->bb_sys3 =		0x90;
		bb->bb_trl =		0x88;
		bb->bb_txagc =		0x38;
		sc->sc_pwrstate_cb = rtw_philips_pwrstate;
		sc->sc_rf_init = rtw_sa2400_init;
		sc->sc_rf_pwrstate = rtw_sa2400_pwrstate;
		sc->sc_rf_tune = rtw_sa2400_tune;
		sc->sc_rf_txpower = rtw_sa2400_txpower;
		break;
	case RTW_RFCHIPID_RFMD2948:
		/* this is the same front-end as an atw(4)! */
		rfname = "RFMD RF2948B, "	/* mentioned in Realtek docs */
			 "LNA: RFMD RF2494, "	/* mentioned in Realtek docs */
			 "SYN: Silicon Labs Si4126";	 /* inferred from
							  * reference driver
							  */
		paname = "RF2189";		/* mentioned in Realtek docs */
		/* XXX RFMD has no RF constructor */
		sc->sc_pwrstate_cb = rtw_rfmd_pwrstate;
		notsup =  1;
		break;
	case RTW_RFCHIPID_GCT:		/* this combo seen in the wild */
		rfname = "GRF5101";
		paname = "WS9901";
		/* XXX magic */
		bb->bb_antatten = RTW_BBP_ANTATTEN_GCT_MAGIC;
		bb->bb_chestlim =	0x00;
		bb->bb_chsqlim =	0xa0;
		bb->bb_ifagcdet =	0x64;
		bb->bb_ifagcini =	0x90;
		bb->bb_ifagclimit =	0x1e;
		bb->bb_lnadet =		0xc0;
		bb->bb_sys1 =		0xa8;
		bb->bb_sys2 =		0x47;
		bb->bb_sys3 =		0x9b;
		bb->bb_trl =		0x88;
		bb->bb_txagc =		0x08;
		sc->sc_pwrstate_cb = rtw_maxim_pwrstate;
		sc->sc_rf_init = rtw_grf5101_init;
		sc->sc_rf_pwrstate = rtw_grf5101_pwrstate;
		sc->sc_rf_tune = rtw_grf5101_tune;
		sc->sc_rf_txpower = rtw_grf5101_txpower;
		break;
	case RTW_RFCHIPID_INTERSIL:
		rfname = "HFA3873";	/* guess */
		paname = "Intersil <unknown>";
		notsup = 1;
		break;
	default:
		snprintf(scratch, sizeof(scratch), "unknown 0x%02x", rfchipid);
		rfname = scratch;
		notsup = 1;
	}

	printf("radio %s, ", rfname);
	if (paname != NULL)
		printf("amp %s, ", paname);

	return (notsup);
}

/* Revision C and later use a different PHY delay setting than
 * revisions A and B.
 */
u_int8_t
rtw_check_phydelay(struct rtw_regs *regs, u_int32_t rcr0)
{
#define REVAB (RTW_RCR_MXDMA_UNLIMITED | RTW_RCR_AICV)
#define REVC (REVAB | RTW8180_RCR_RXFTH_WHOLE)

	u_int8_t phydelay = LSHIFT(0x6, RTW_PHYDELAY_PHYDELAY);

	RTW_WRITE(regs, RTW_RCR, REVAB);
	RTW_WBW(regs, RTW_RCR, RTW_RCR);
	RTW_WRITE(regs, RTW_RCR, REVC);

	RTW_WBR(regs, RTW_RCR, RTW_RCR);
	if ((RTW_READ(regs, RTW_RCR) & REVC) == REVC)
		phydelay |= RTW_PHYDELAY_REVC_MAGIC;

	RTW_WRITE(regs, RTW_RCR, rcr0);	/* restore RCR */
	RTW_SYNC(regs, RTW_RCR, RTW_RCR);

	return phydelay;
#undef REVC
}

void
rtw_attach(struct rtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rtw_txsoft_blk *tsb;
	struct rtw_mtbl *mtbl;
	struct rtw_srom *sr;
	const char *vername;
	struct ifnet *ifp;
	char scratch[sizeof("unknown 0xXXXXXXXX")];
	int pri, rc, i;


	/* Use default DMA memory access */
	if (sc->sc_regs.r_read8 == NULL) {
		sc->sc_regs.r_read8 = rtw_read8;
		sc->sc_regs.r_read16 = rtw_read16;
		sc->sc_regs.r_read32 = rtw_read32;
		sc->sc_regs.r_write8 = rtw_write8;
		sc->sc_regs.r_write16 = rtw_write16;
		sc->sc_regs.r_write32 = rtw_write32;
		sc->sc_regs.r_barrier = rtw_barrier;
	}

	sc->sc_hwverid = RTW_READ(&sc->sc_regs, RTW_TCR) & RTW_TCR_HWVERID_MASK;
	switch (sc->sc_hwverid) {
	case RTW_TCR_HWVERID_RTL8185:
		vername = "RTL8185";
		sc->sc_flags |= RTW_F_RTL8185;
		break;
	case RTW_TCR_HWVERID_RTL8180F:
		vername = "RTL8180F";
		break;
	case RTW_TCR_HWVERID_RTL8180D:
		vername = "RTL8180D";
		break;
	default:
		snprintf(scratch, sizeof(scratch), "unknown 0x%08x",
		    sc->sc_hwverid);
		vername = scratch;
		break;
	}

	printf("%s: ver %s, ", sc->sc_dev.dv_xname, vername);

	rc = bus_dmamem_alloc(sc->sc_dmat, sizeof(struct rtw_descs),
	    RTW_DESC_ALIGNMENT, 0, &sc->sc_desc_segs, 1, &sc->sc_desc_nsegs,
	    0);

	if (rc != 0) {
		printf("\n%s: could not allocate hw descriptors, error %d\n",
		     sc->sc_dev.dv_xname, rc);
		goto fail0;
	}

	rc = bus_dmamem_map(sc->sc_dmat, &sc->sc_desc_segs,
	    sc->sc_desc_nsegs, sizeof(struct rtw_descs),
	    (caddr_t*)&sc->sc_descs, BUS_DMA_COHERENT);

	if (rc != 0) {
		printf("\n%s: can't map hw descriptors, error %d\n",
		    sc->sc_dev.dv_xname, rc);
		goto fail1;
	}

	rc = bus_dmamap_create(sc->sc_dmat, sizeof(struct rtw_descs), 1,
	    sizeof(struct rtw_descs), 0, 0, &sc->sc_desc_dmamap);

	if (rc != 0) {
		printf("\n%s: could not create DMA map for hw descriptors, "
		    "error %d\n", sc->sc_dev.dv_xname, rc);
		goto fail2;
	}

	sc->sc_rxdesc_blk.rdb_dmat = sc->sc_dmat;
	sc->sc_rxdesc_blk.rdb_dmamap = sc->sc_desc_dmamap;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		sc->sc_txdesc_blk[pri].tdb_dmat = sc->sc_dmat;
		sc->sc_txdesc_blk[pri].tdb_dmamap = sc->sc_desc_dmamap;
	}

	rc = bus_dmamap_load(sc->sc_dmat, sc->sc_desc_dmamap, sc->sc_descs,
	    sizeof(struct rtw_descs), NULL, 0);

	if (rc != 0) {
		printf("\n%s: could not load DMA map for hw descriptors, "
		    "error %d\n", sc->sc_dev.dv_xname, rc);
		goto fail3;
	}

	if (rtw_txsoft_blk_setup_all(sc) != 0)
		goto fail4;

	rtw_txdesc_blk_setup_all(sc);

	sc->sc_rxdesc_blk.rdb_desc = &sc->sc_descs->hd_rx[0];

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		tsb = &sc->sc_txsoft_blk[pri];

		if ((rc = rtw_txdesc_dmamaps_create(sc->sc_dmat,
		    &tsb->tsb_desc[0], tsb->tsb_ndesc)) != 0) {
			printf("\n%s: could not load DMA map for "
			    "hw tx descriptors, error %d\n",
			    sc->sc_dev.dv_xname, rc);
			goto fail5;
		}
	}

	if ((rc = rtw_rxdesc_dmamaps_create(sc->sc_dmat, &sc->sc_rxsoft[0],
	    RTW_RXQLEN)) != 0) {
		printf("\n%s: could not load DMA map for hw rx descriptors, "
		    "error %d\n", sc->sc_dev.dv_xname, rc);
		goto fail6;
	}

	/* Reset the chip to a known state. */
	if (rtw_reset(sc) != 0)
		goto fail7;

	sc->sc_rcr = RTW_READ(&sc->sc_regs, RTW_RCR);

	if ((sc->sc_rcr & RTW_RCR_9356SEL) != 0)
		sc->sc_flags |= RTW_F_9356SROM;

	if (rtw_srom_read(&sc->sc_regs, sc->sc_flags, &sc->sc_srom,
	    sc->sc_dev.dv_xname) != 0)
		goto fail7;

	if (rtw_srom_parse(sc) != 0) {
		printf("\n%s: attach failed, malformed serial ROM\n",
		    sc->sc_dev.dv_xname);
		goto fail8;
	}

	RTW_DPRINTF(RTW_DEBUG_ATTACH, ("%s: %s PHY\n", sc->sc_dev.dv_xname,
	    ((sc->sc_flags & RTW_F_DIGPHY) != 0) ? "digital" : "analog"));

	RTW_DPRINTF(RTW_DEBUG_ATTACH, ("%s: CS threshold %u\n",
	    sc->sc_dev.dv_xname, sc->sc_csthr));

	if ((rtw_rf_attach(sc, sc->sc_rfchipid)) != 0) {
		printf("\n%s: attach failed, could not attach RF\n",
		    sc->sc_dev.dv_xname);
		goto fail8;
	}

	sc->sc_phydelay = rtw_check_phydelay(&sc->sc_regs, sc->sc_rcr);

	RTW_DPRINTF(RTW_DEBUG_ATTACH,
	    ("%s: PHY delay %d\n", sc->sc_dev.dv_xname, sc->sc_phydelay));

	if (sc->sc_locale == RTW_LOCALE_UNKNOWN)
		rtw_identify_country(&sc->sc_regs, &sc->sc_locale);

	for (i = 1; i <= 14; i++) {
		sc->sc_ic.ic_channels[i].ic_flags = IEEE80211_CHAN_B;
		sc->sc_ic.ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, sc->sc_ic.ic_channels[i].ic_flags);
	}

	if (rtw_identify_sta(&sc->sc_regs, &sc->sc_ic.ic_myaddr,
	    sc->sc_dev.dv_xname) != 0)
		goto fail8;

	ifp = &sc->sc_if;
	(void)memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_ioctl = rtw_ioctl;
	ifp->if_start = rtw_start;
	ifp->if_watchdog = rtw_watchdog;


	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_caps = IEEE80211_C_PMGT | IEEE80211_C_MONITOR | IEEE80211_C_WEP;
#ifndef IEEE80211_STA_ONLY
	ic->ic_caps |= IEEE80211_C_HOSTAP | IEEE80211_C_IBSS;
#endif
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;

	rtw_led_attach(&sc->sc_led_state, (void *)sc);

	/*
	 * Call MI attach routines.
	 */
	if_attach(&sc->sc_if);
	ieee80211_ifattach(&sc->sc_if);

	mtbl = &sc->sc_mtbl;
	mtbl->mt_newstate = ic->ic_newstate;
	ic->ic_newstate = rtw_newstate;

#ifndef IEEE80211_STA_ONLY
	mtbl->mt_recv_mgmt = ic->ic_recv_mgmt;
	ic->ic_recv_mgmt = rtw_recv_mgmt;
#endif

	mtbl->mt_node_free = ic->ic_node_free;
	ic->ic_node_free = rtw_node_free;

	mtbl->mt_node_alloc = ic->ic_node_alloc;
	ic->ic_node_alloc = rtw_node_alloc;

	/* possibly we should fill in our own sc_send_prresp, since
	 * the RTL8180 is probably sending probe responses in ad hoc
	 * mode.
	 */

	/* complete initialization */
	ieee80211_media_init(&sc->sc_if, rtw_media_change, rtw_media_status);
	timeout_set(&sc->sc_scan_to, rtw_next_scan, sc);

#if NBPFILTER > 0
	bzero(&sc->sc_rxtapu, sizeof(sc->sc_rxtapu));
	sc->sc_rxtap.rr_ihdr.it_len = sizeof(sc->sc_rxtapu);
	sc->sc_rxtap.rr_ihdr.it_present = RTW_RX_RADIOTAP_PRESENT;

	bzero(&sc->sc_txtapu, sizeof(sc->sc_txtapu));
	sc->sc_txtap.rt_ihdr.it_len = sizeof(sc->sc_txtapu);
	sc->sc_txtap.rt_ihdr.it_present = RTW_TX_RADIOTAP_PRESENT;

	bpfattach(&sc->sc_radiobpf, &sc->sc_ic.ic_if, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + 64);
#endif
	return;

fail8:
	sr = &sc->sc_srom;
	if (sr->sr_content != NULL) {
		free(sr->sr_content, M_DEVBUF, sr->sr_size);
		sr->sr_content = NULL;
	}
	sr->sr_size = 0;

fail7:
	rtw_rxdesc_dmamaps_destroy(sc->sc_dmat, &sc->sc_rxsoft[0],
	    RTW_RXQLEN);

fail6:
	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		rtw_txdesc_dmamaps_destroy(sc->sc_dmat,
		    sc->sc_txsoft_blk[pri].tsb_desc,
		    sc->sc_txsoft_blk[pri].tsb_ndesc);
	}

fail5:
	rtw_txsoft_blk_cleanup_all(sc);

fail4:
	bus_dmamap_unload(sc->sc_dmat, sc->sc_desc_dmamap);
fail3:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_desc_dmamap);
fail2:
	bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_descs,
	    sizeof(struct rtw_descs));
fail1:
	bus_dmamem_free(sc->sc_dmat, &sc->sc_desc_segs,
	    sc->sc_desc_nsegs);
fail0:
	return;
}

int
rtw_detach(struct rtw_softc *sc)
{
	sc->sc_flags |= RTW_F_INVALID;

	timeout_del(&sc->sc_scan_to);

	rtw_stop(&sc->sc_if, 1);

	ieee80211_ifdetach(&sc->sc_if);
	if_detach(&sc->sc_if);

	return 0;
}

/*
 * PHY specific functions
 */

int
rtw_bbp_preinit(struct rtw_regs *regs, u_int antatten0, int dflantb,
    u_int freq)
{
	u_int antatten = antatten0;
	if (dflantb)
		antatten |= RTW_BBP_ANTATTEN_DFLANTB;
	if (freq == 2484) /* channel 14 */
		antatten |= RTW_BBP_ANTATTEN_CHAN14;
	return rtw_bbp_write(regs, RTW_BBP_ANTATTEN, antatten);
}

int
rtw_bbp_init(struct rtw_regs *regs, struct rtw_bbpset *bb, int antdiv,
    int dflantb, u_int8_t cs_threshold, u_int freq)
{
	int rc;
	u_int32_t sys2, sys3;

	sys2 = bb->bb_sys2;
	if (antdiv)
		sys2 |= RTW_BBP_SYS2_ANTDIV;
	sys3 = bb->bb_sys3 |
	    LSHIFT(cs_threshold, RTW_BBP_SYS3_CSTHRESH_MASK);

#define	RTW_BBP_WRITE_OR_RETURN(reg, val) \
	if ((rc = rtw_bbp_write(regs, reg, val)) != 0) \
		return rc;

	RTW_BBP_WRITE_OR_RETURN(RTW_BBP_SYS1,		bb->bb_sys1);
	RTW_BBP_WRITE_OR_RETURN(RTW_BBP_TXAGC,		bb->bb_txagc);
	RTW_BBP_WRITE_OR_RETURN(RTW_BBP_LNADET,		bb->bb_lnadet);
	RTW_BBP_WRITE_OR_RETURN(RTW_BBP_IFAGCINI,	bb->bb_ifagcini);
	RTW_BBP_WRITE_OR_RETURN(RTW_BBP_IFAGCLIMIT,	bb->bb_ifagclimit);
	RTW_BBP_WRITE_OR_RETURN(RTW_BBP_IFAGCDET,	bb->bb_ifagcdet);

	if ((rc = rtw_bbp_preinit(regs, bb->bb_antatten, dflantb, freq)) != 0)
		return rc;

	RTW_BBP_WRITE_OR_RETURN(RTW_BBP_TRL,		bb->bb_trl);
	RTW_BBP_WRITE_OR_RETURN(RTW_BBP_SYS2,		sys2);
	RTW_BBP_WRITE_OR_RETURN(RTW_BBP_SYS3,		sys3);
	RTW_BBP_WRITE_OR_RETURN(RTW_BBP_CHESTLIM,	bb->bb_chestlim);
	RTW_BBP_WRITE_OR_RETURN(RTW_BBP_CHSQLIM,	bb->bb_chsqlim);
	return 0;
}

int
rtw_sa2400_txpower(struct rtw_softc *sc, u_int8_t opaque_txpower)
{
	return rtw_rf_macwrite(sc, SA2400_TX, opaque_txpower);
}

/* make sure we're using the same settings as the reference driver */
void
rtw_verify_syna(u_int freq, u_int32_t val)
{
	u_int32_t expected_val = ~val;

	switch (freq) {
	case 2412:
		expected_val = 0x0000096c; /* ch 1 */
		break;
	case 2417:
		expected_val = 0x00080970; /* ch 2 */
		break;
	case 2422:
		expected_val = 0x00100974; /* ch 3 */
		break;
	case 2427:
		expected_val = 0x00180978; /* ch 4 */
		break;
	case 2432:
		expected_val = 0x00000980; /* ch 5 */
		break;
	case 2437:
		expected_val = 0x00080984; /* ch 6 */
		break;
	case 2442:
		expected_val = 0x00100988; /* ch 7 */
		break;
	case 2447:
		expected_val = 0x0018098c; /* ch 8 */
		break;
	case 2452:
		expected_val = 0x00000994; /* ch 9 */
		break;
	case 2457:
		expected_val = 0x00080998; /* ch 10 */
		break;
	case 2462:
		expected_val = 0x0010099c; /* ch 11 */
		break;
	case 2467:
		expected_val = 0x001809a0; /* ch 12 */
		break;
	case 2472:
		expected_val = 0x000009a8; /* ch 13 */
		break;
	case 2484:
		expected_val = 0x000009b4; /* ch 14 */
		break;
	}
	KASSERT(val == expected_val);
}

/* freq is in MHz */
int
rtw_sa2400_tune(struct rtw_softc *sc, u_int freq)
{
	int rc;
	u_int32_t syna, synb, sync;

	/* XO = 44MHz, R = 11, hence N is in units of XO / R = 4MHz.
	 *
	 * The channel spacing (5MHz) is not divisible by 4MHz, so
	 * we set the fractional part of N to compensate.
	 */
	int n = freq / 4, nf = (freq % 4) * 2;

	syna = LSHIFT(nf, SA2400_SYNA_NF_MASK) | LSHIFT(n, SA2400_SYNA_N_MASK);
	rtw_verify_syna(freq, syna);

	/* Divide the 44MHz crystal down to 4MHz. Set the fractional
	 * compensation charge pump value to agree with the fractional
	 * modulus.
	 */
	synb = LSHIFT(11, SA2400_SYNB_R_MASK) | SA2400_SYNB_L_NORMAL |
	    SA2400_SYNB_ON | SA2400_SYNB_ONE |
	    LSHIFT(80, SA2400_SYNB_FC_MASK); /* agrees w/ SA2400_SYNA_FM = 0 */

	sync = SA2400_SYNC_CP_NORMAL;

	if ((rc = rtw_rf_macwrite(sc, SA2400_SYNA, syna)) != 0)
		return rc;
	if ((rc = rtw_rf_macwrite(sc, SA2400_SYNB, synb)) != 0)
		return rc;
	if ((rc = rtw_rf_macwrite(sc, SA2400_SYNC, sync)) != 0)
		return rc;
	return rtw_rf_macwrite(sc, SA2400_SYND, 0x0);
}

int
rtw_sa2400_pwrstate(struct rtw_softc *sc, enum rtw_pwrstate power)
{
	u_int32_t opmode;
	opmode = SA2400_OPMODE_DEFAULTS;
	switch (power) {
	case RTW_ON:
		opmode |= SA2400_OPMODE_MODE_TXRX;
		break;
	case RTW_SLEEP:
		opmode |= SA2400_OPMODE_MODE_WAIT;
		break;
	case RTW_OFF:
		opmode |= SA2400_OPMODE_MODE_SLEEP;
		break;
	}

	if (sc->sc_flags & RTW_F_DIGPHY)
		opmode |= SA2400_OPMODE_DIGIN;

	return rtw_rf_macwrite(sc, SA2400_OPMODE, opmode);
}

int
rtw_sa2400_vcocal_start(struct rtw_softc *sc, int start)
{
	u_int32_t opmode;

	opmode = SA2400_OPMODE_DEFAULTS;
	if (start)
		opmode |= SA2400_OPMODE_MODE_VCOCALIB;
	else
		opmode |= SA2400_OPMODE_MODE_SLEEP;

	if (sc->sc_flags & RTW_F_DIGPHY)
		opmode |= SA2400_OPMODE_DIGIN;

	return rtw_rf_macwrite(sc, SA2400_OPMODE, opmode);
}

int
rtw_sa2400_vco_calibration(struct rtw_softc *sc)
{
	int rc;
	/* calibrate VCO */
	if ((rc = rtw_sa2400_vcocal_start(sc, 1)) != 0)
		return rc;
	DELAY(2200);	/* 2.2 milliseconds */
	/* XXX superfluous: SA2400 automatically entered SLEEP mode. */
	return rtw_sa2400_vcocal_start(sc, 0);
}

int
rtw_sa2400_filter_calibration(struct rtw_softc *sc)
{
	u_int32_t opmode;

	opmode = SA2400_OPMODE_DEFAULTS | SA2400_OPMODE_MODE_FCALIB;
	if (sc->sc_flags & RTW_F_DIGPHY)
		opmode |= SA2400_OPMODE_DIGIN;

	return rtw_rf_macwrite(sc, SA2400_OPMODE, opmode);
}

int
rtw_sa2400_dc_calibration(struct rtw_softc *sc)
{
	int rc;
	u_int32_t dccal;

	rtw_continuous_tx_enable(sc, 1);

	dccal = SA2400_OPMODE_DEFAULTS | SA2400_OPMODE_MODE_TXRX;

	rc = rtw_rf_macwrite(sc, SA2400_OPMODE, dccal);

	if (rc != 0)
		return rc;

	DELAY(5);	/* DCALIB after being in Tx mode for 5
			 * microseconds
			 */

	dccal &= ~SA2400_OPMODE_MODE_MASK;
	dccal |= SA2400_OPMODE_MODE_DCALIB;

	rc = rtw_rf_macwrite(sc, SA2400_OPMODE, dccal);
	if (rc != 0)
		return rc;

	DELAY(20);	/* calibration takes at most 20 microseconds */

	rtw_continuous_tx_enable(sc, 0);

	return 0;
}

int
rtw_sa2400_calibrate(struct rtw_softc *sc, u_int freq)
{
	int i, rc;

	/* XXX reference driver calibrates VCO twice. Is it a bug? */
	for (i = 0; i < 2; i++) {
		if ((rc = rtw_sa2400_vco_calibration(sc)) != 0)
			return rc;
	}
	/* VCO calibration erases synthesizer registers, so re-tune */
	if ((rc = rtw_sa2400_tune(sc, freq)) != 0)
		return rc;
	if ((rc = rtw_sa2400_filter_calibration(sc)) != 0)
		return rc;
	/* analog PHY needs DC calibration */
	if (!(sc->sc_flags & RTW_F_DIGPHY))
		return rtw_sa2400_dc_calibration(sc);
	return 0;
}

int
rtw_sa2400_init(struct rtw_softc *sc, u_int freq, u_int8_t opaque_txpower,
    enum rtw_pwrstate power)
{
	int rc;
	u_int32_t agc, manrx;

	if ((rc = rtw_sa2400_txpower(sc, opaque_txpower)) != 0)
		return rc;

	/* skip configuration if it's time to sleep or to power-down. */
	if (power == RTW_SLEEP || power == RTW_OFF)
		return rtw_sa2400_pwrstate(sc, power);

	/* go to sleep for configuration */
	if ((rc = rtw_sa2400_pwrstate(sc, RTW_SLEEP)) != 0)
		return rc;

	if ((rc = rtw_sa2400_tune(sc, freq)) != 0)
		return rc;

	agc = LSHIFT(25, SA2400_AGC_MAXGAIN_MASK);
	agc |= LSHIFT(7, SA2400_AGC_BBPDELAY_MASK);
	agc |= LSHIFT(15, SA2400_AGC_LNADELAY_MASK);
	agc |= LSHIFT(27, SA2400_AGC_RXONDELAY_MASK);

	if ((rc = rtw_rf_macwrite(sc, SA2400_AGC, agc)) != 0)
		return rc;

	/* XXX we are not supposed to be in RXMGC mode when we do this? */
	manrx = SA2400_MANRX_AHSN;
	manrx |= SA2400_MANRX_TEN;
	manrx |= LSHIFT(1023, SA2400_MANRX_RXGAIN_MASK);

	if ((rc = rtw_rf_macwrite(sc, SA2400_MANRX, manrx)) != 0)
		return rc;

	if ((rc = rtw_sa2400_calibrate(sc, freq)) != 0)
		return rc;

	/* enter Tx/Rx mode */
	return rtw_sa2400_pwrstate(sc, power);
}

/* freq is in MHz */
int
rtw_max2820_tune(struct rtw_softc *sc, u_int freq)
{
	if (freq < 2400 || freq > 2499)
		return -1;

	return rtw_rf_hostwrite(sc, MAX2820_CHANNEL,
	    LSHIFT(freq - 2400, MAX2820_CHANNEL_CF_MASK));
}

int
rtw_max2820_init(struct rtw_softc *sc, u_int freq, u_int8_t opaque_txpower,
    enum rtw_pwrstate power)
{
	int rc;

	if ((rc = rtw_rf_hostwrite(sc, MAX2820_TEST,
	    MAX2820_TEST_DEFAULT)) != 0)
		return rc;

	if ((rc = rtw_rf_hostwrite(sc, MAX2820_ENABLE,
	    MAX2820_ENABLE_DEFAULT)) != 0)
		return rc;

	/* skip configuration if it's time to sleep or to power-down. */
	if ((rc = rtw_max2820_pwrstate(sc, power)) != 0)
		return rc;
	else if (power == RTW_OFF || power == RTW_SLEEP)
		return 0;

	if ((rc = rtw_rf_hostwrite(sc, MAX2820_SYNTH,
	    MAX2820_SYNTH_R_44MHZ)) != 0)
		return rc;

	if ((rc = rtw_max2820_tune(sc, freq)) != 0)
		return rc;

	/* XXX The MAX2820 datasheet indicates that 1C and 2C should not
	 * be changed from 7, however, the reference driver sets them
	 * to 4 and 1, respectively.
	 */
	if ((rc = rtw_rf_hostwrite(sc, MAX2820_RECEIVE,
	    MAX2820_RECEIVE_DL_DEFAULT |
	    LSHIFT(4, MAX2820A_RECEIVE_1C_MASK) |
	    LSHIFT(1, MAX2820A_RECEIVE_2C_MASK))) != 0)
		return rc;

	return rtw_rf_hostwrite(sc, MAX2820_TRANSMIT,
	    MAX2820_TRANSMIT_PA_DEFAULT);
}

int
rtw_max2820_txpower(struct rtw_softc *sc, u_int8_t opaque_txpower)
{
	/* TBD */
	return 0;
}

int
rtw_max2820_pwrstate(struct rtw_softc *sc, enum rtw_pwrstate power)
{
	uint32_t enable;

	switch (power) {
	case RTW_OFF:
	case RTW_SLEEP:
	default:
		enable = 0x0;
		break;
	case RTW_ON:
		enable = MAX2820_ENABLE_DEFAULT;
		break;
	}
	return rtw_rf_hostwrite(sc, MAX2820_ENABLE, enable);
}

int
rtw_grf5101_init(struct rtw_softc *sc, u_int freq, u_int8_t opaque_txpower,
    enum rtw_pwrstate power)
{
	int rc;

	/*
	 * These values have been derived from the rtl8180-sa2400 Linux driver.
	 * It is unknown what they all do, GCT refuse to release any documentation
	 * so these are more than likely sub optimal settings
	 */

	rtw_rf_macwrite(sc, 0x01, 0x1a23);
	rtw_rf_macwrite(sc, 0x02, 0x4971);
	rtw_rf_macwrite(sc, 0x03, 0x41de);
	rtw_rf_macwrite(sc, 0x04, 0x2d80);

	rtw_rf_macwrite(sc, 0x05, 0x61ff);

	rtw_rf_macwrite(sc, 0x06, 0x0);

	rtw_rf_macwrite(sc, 0x08, 0x7533);
	rtw_rf_macwrite(sc, 0x09, 0xc401);
	rtw_rf_macwrite(sc, 0x0a, 0x0);
	rtw_rf_macwrite(sc, 0x0c, 0x1c7);
	rtw_rf_macwrite(sc, 0x0d, 0x29d3);
	rtw_rf_macwrite(sc, 0x0e, 0x2e8);
	rtw_rf_macwrite(sc, 0x10, 0x192);
	rtw_rf_macwrite(sc, 0x11, 0x248);
	rtw_rf_macwrite(sc, 0x12, 0x0);
	rtw_rf_macwrite(sc, 0x13, 0x20c4);
	rtw_rf_macwrite(sc, 0x14, 0xf4fc);
	rtw_rf_macwrite(sc, 0x15, 0x0);
	rtw_rf_macwrite(sc, 0x16, 0x1500);

	if ((rc = rtw_grf5101_txpower(sc, opaque_txpower)) != 0)
		return rc;

	if ((rc = rtw_grf5101_tune(sc, freq)) != 0)
		return rc;

	return (0);
}

int
rtw_grf5101_tune(struct rtw_softc *sc, u_int freq)
{
	struct ieee80211com *ic = &sc->sc_ic;
	u_int channel = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);

	/* set channel */
	rtw_rf_macwrite(sc, 0x07, 0);
	rtw_rf_macwrite(sc, 0x0b, channel - 1);
	rtw_rf_macwrite(sc, 0x07, 0x1000);

	return (0);
}

int
rtw_grf5101_txpower(struct rtw_softc *sc, u_int8_t opaque_txpower)
{
	rtw_rf_macwrite(sc, 0x15, 0);
	rtw_rf_macwrite(sc, 0x06, opaque_txpower);
	rtw_rf_macwrite(sc, 0x15, 0x10);
	rtw_rf_macwrite(sc, 0x15, 0x00);

	return (0);
}

int
rtw_grf5101_pwrstate(struct rtw_softc *sc, enum rtw_pwrstate power)
{
	switch (power) {
	case RTW_OFF:
		/* FALLTHROUGH */
	case RTW_SLEEP:
		rtw_rf_macwrite(sc, 0x07, 0x0000);
		rtw_rf_macwrite(sc, 0x1f, 0x0045);
		rtw_rf_macwrite(sc, 0x1f, 0x0005);
		rtw_rf_macwrite(sc, 0x00, 0x08e4);
		break;
	case RTW_ON:
		rtw_rf_macwrite(sc, 0x1f, 0x0001);
		DELAY(10);
		rtw_rf_macwrite(sc, 0x1f, 0x0001);
		DELAY(10);
		rtw_rf_macwrite(sc, 0x1f, 0x0041);
		DELAY(10);
		rtw_rf_macwrite(sc, 0x1f, 0x0061);
		DELAY(10);
		rtw_rf_macwrite(sc, 0x00, 0x0ae4);
		DELAY(10);
		rtw_rf_macwrite(sc, 0x07, 0x1000);
		DELAY(100);
		break;
	}

	return 0;
}

int
rtw_rtl8225_pwrstate(struct rtw_softc *sc, enum rtw_pwrstate power)
{
	return (0);
}

int
rtw_rtl8225_init(struct rtw_softc *sc, u_int freq, u_int8_t opaque_txpower,
    enum rtw_pwrstate power)
{
	return (0);
}

int
rtw_rtl8225_txpower(struct rtw_softc *sc, u_int8_t opaque_txpower)
{
	return (0);
}

int
rtw_rtl8225_tune(struct rtw_softc *sc, u_int freq)
{
	return (0);
}

int
rtw_rtl8255_pwrstate(struct rtw_softc *sc, enum rtw_pwrstate power)
{
	return (0);
}

int
rtw_rtl8255_init(struct rtw_softc *sc, u_int freq, u_int8_t opaque_txpower,
    enum rtw_pwrstate power)
{
	return (0);
}

int
rtw_rtl8255_txpower(struct rtw_softc *sc, u_int8_t opaque_txpower)
{
	return (0);
}

int
rtw_rtl8255_tune(struct rtw_softc *sc, u_int freq)
{
	return (0);
}

int
rtw_phy_init(struct rtw_softc *sc)
{
	int rc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct rtw_regs *regs = &sc->sc_regs;
	int antdiv = sc->sc_flags & RTW_F_ANTDIV;
	int dflantb = sc->sc_flags & RTW_F_DFLANTB;
	u_int freq = ic->ic_bss->ni_chan->ic_freq;	/* freq is in MHz */
	u_int8_t opaque_txpower = rtw_chan2txpower(&sc->sc_srom, ic,
	    ic->ic_bss->ni_chan);
	u_int8_t cs_threshold = sc->sc_csthr;
	enum rtw_pwrstate power = RTW_ON;

	RTW_DPRINTF(RTW_DEBUG_PHY,
	    ("%s: txpower %u csthresh %u freq %u antdiv %u dflantb %u "
	     "pwrstate %s\n", __func__, opaque_txpower, cs_threshold, freq,
	     antdiv, dflantb, rtw_pwrstate_string(power)));

	/* XXX is this really necessary? */
	if ((rc = (*sc->sc_rf_txpower)(sc, opaque_txpower)) != 0)
		return rc;
	if ((rc = rtw_bbp_preinit(regs, sc->sc_bbpset.bb_antatten, dflantb,
	    freq)) != 0)
		return rc;
	if ((rc = (*sc->sc_rf_tune)(sc, freq)) != 0)
		return rc;
	/* initialize RF  */
	if ((rc = (*sc->sc_rf_init)(sc, freq, opaque_txpower, power)) != 0)
		return rc;
#if 0	/* what is this redundant tx power setting here for? */
	if ((rc = (*sc->sc_rf_txpower)(sc, opaque_txpower)) != 0)
		return rc;
#endif
	return rtw_bbp_init(regs, &sc->sc_bbpset, antdiv, dflantb,
	    cs_threshold, freq);
}

/*
 * Generic PHY I/O functions
 */

int
rtw_bbp_write(struct rtw_regs *regs, u_int addr, u_int val)
{
#define	BBP_WRITE_ITERS	50
#define	BBP_WRITE_DELAY	1
	int i;
	u_int32_t wrbbp, rdbbp;

	RTW_DPRINTF(RTW_DEBUG_PHYIO,
	    ("%s: bbp[%u] <- %u\n", __func__, addr, val));

	KASSERT((addr & ~PRESHIFT(RTW_BB_ADDR_MASK)) == 0);
	KASSERT((val & ~PRESHIFT(RTW_BB_WR_MASK)) == 0);

	wrbbp = LSHIFT(addr, RTW_BB_ADDR_MASK) | RTW_BB_WREN |
	    LSHIFT(val, RTW_BB_WR_MASK) | RTW_BB_RD_MASK,

	rdbbp = LSHIFT(addr, RTW_BB_ADDR_MASK) |
	    RTW_BB_WR_MASK | RTW_BB_RD_MASK;

	RTW_DPRINTF(RTW_DEBUG_PHYIO,
	    ("%s: rdbbp = %#08x, wrbbp = %#08x\n", __func__, rdbbp, wrbbp));

	for (i = BBP_WRITE_ITERS; --i >= 0; ) {
		RTW_RBW(regs, RTW_BB, RTW_BB);
		RTW_WRITE(regs, RTW_BB, wrbbp);
		RTW_SYNC(regs, RTW_BB, RTW_BB);
		RTW_WRITE(regs, RTW_BB, rdbbp);
		RTW_SYNC(regs, RTW_BB, RTW_BB);
		delay(BBP_WRITE_DELAY);	/* 1 microsecond */
		if (MASK_AND_RSHIFT(RTW_READ(regs, RTW_BB),
		    RTW_BB_RD_MASK) == val) {
			RTW_DPRINTF(RTW_DEBUG_PHYIO,
			    ("%s: finished in %dus\n", __func__,
			    BBP_WRITE_DELAY * (BBP_WRITE_ITERS - i)));
			return 0;
		}
		delay(BBP_WRITE_DELAY);	/* again */
	}
	printf("%s: timeout\n", __func__);
	return -1;
}

/* Help rtw_rf_hostwrite bang bits to RF over 3-wire interface. */
void
rtw_rf_hostbangbits(struct rtw_regs *regs, u_int32_t bits, int lo_to_hi,
    u_int nbits)
{
	int i;
	u_int32_t mask, reg;

	KASSERT(nbits <= 32);

	RTW_DPRINTF(RTW_DEBUG_PHYIO,
	    ("%s: %u bits, %#08x, %s\n", __func__, nbits, bits,
	    (lo_to_hi) ? "lo to hi" : "hi to lo"));

	reg = RTW8180_PHYCFG_HST;
	RTW_WRITE(regs, RTW8180_PHYCFG, reg);
	RTW_SYNC(regs, RTW8180_PHYCFG, RTW8180_PHYCFG);

	if (lo_to_hi)
		mask = 0x1;
	else
		mask = 1 << (nbits - 1);

	for (i = 0; i < nbits; i++) {
		RTW_DPRINTF(RTW_DEBUG_PHYBITIO,
		    ("%s: bits %#08x mask %#08x -> bit %#08x\n",
		    __func__, bits, mask, bits & mask));

		if ((bits & mask) != 0)
			reg |= RTW8180_PHYCFG_HST_DATA;
		else
			reg &= ~RTW8180_PHYCFG_HST_DATA;

		reg |= RTW8180_PHYCFG_HST_CLK;
		RTW_WRITE(regs, RTW8180_PHYCFG, reg);
		RTW_SYNC(regs, RTW8180_PHYCFG, RTW8180_PHYCFG);

		DELAY(2);	/* arbitrary delay */

		reg &= ~RTW8180_PHYCFG_HST_CLK;
		RTW_WRITE(regs, RTW8180_PHYCFG, reg);
		RTW_SYNC(regs, RTW8180_PHYCFG, RTW8180_PHYCFG);

		if (lo_to_hi)
			mask <<= 1;
		else
			mask >>= 1;
	}

	reg |= RTW8180_PHYCFG_HST_EN;
	KASSERT((reg & RTW8180_PHYCFG_HST_CLK) == 0);
	RTW_WRITE(regs, RTW8180_PHYCFG, reg);
	RTW_SYNC(regs, RTW8180_PHYCFG, RTW8180_PHYCFG);
}

#if 0
void
rtw_rf_rtl8225_hostbangbits(struct rtw_regs *regs, u_int32_t bits, int lo_to_hi,
    u_int nbits)
{
	int i;
	u_int8_t page;
	u_int16_t reg0, reg1, reg2;
	u_int32_t mask;

	/* enable page 0 */
	page = RTW_READ8(regs, RTW_PSR);
	RTW_WRITE8(regs, RTW_PSR, page & ~RTW_PSR_PSEN);

	/* enable RF access */
	reg0 = RTW_READ16(regs, RTW8185_RFPINSOUTPUT) &
	    RTW8185_RFPINSOUTPUT_MASK;
	reg1 = RTW_READ16(regs, RTW8185_RFPINSENABLE);
	RTW_WRITE16(regs, RTW8185_RFPINSENABLE,
	    RTW8185_RFPINSENABLE_ENABLE | reg0);
	reg2 = RTW_READ16(regs, RTW8185_RFPINSSELECT);
	RTW_WRITE16(regs, RTW8185_RFPINSSELECT,
	    RTW8185_RFPINSSELECT_ENABLE | reg1 /* XXX | SW_GPIO_CTL */);
	DELAY(10);

	RTW_WRITE16(regs, RTW8185_RFPINSOUTPUT, reg0);
	DELAY(10);

	if (lo_to_hi)
		mask = 0x1;
	else
		mask = 1 << (nbits - 1);

	for (i = 0; i < nbits; i++) {
		RTW_DPRINTF(RTW_DEBUG_PHYBITIO,
		    ("%s: bits %#08x mask %#08x -> bit %#08x\n",
		    __func__, bits, mask, bits & mask));

		if ((bits & mask) != 0)
			reg |= RTW8180_PHYCFG_HST_DATA;
		else
			reg &= ~RTW8180_PHYCFG_HST_DATA;

		reg |= RTW8180_PHYCFG_HST_CLK;
		RTW_WRITE(regs, RTW8180_PHYCFG, reg);
		RTW_SYNC(regs, RTW8180_PHYCFG, RTW8180_PHYCFG);

		DELAY(2);	/* arbitrary delay */

		reg &= ~RTW8180_PHYCFG_HST_CLK;
		RTW_WRITE(regs, RTW8180_PHYCFG, reg);
		RTW_SYNC(regs, RTW8180_PHYCFG, RTW8180_PHYCFG);

		if (lo_to_hi)
			mask <<= 1;
		else
			mask >>= 1;
	}

	/* reset the page */
	RTW_WRITE8(regs, RTW_PSR, page);
}
#endif

/* Help rtw_rf_macwrite: tell MAC to bang bits to RF over the 3-wire
 * interface.
 */
int
rtw_rf_macbangbits(struct rtw_regs *regs, u_int32_t reg)
{
	int i;

	RTW_DPRINTF(RTW_DEBUG_PHY, ("%s: %#08x\n", __func__, reg));

	RTW_WRITE(regs, RTW8180_PHYCFG, RTW8180_PHYCFG_MAC_POLL | reg);

	RTW_WBR(regs, RTW8180_PHYCFG, RTW8180_PHYCFG);

	for (i = rtw_macbangbits_timeout; --i >= 0; delay(1)) {
		if ((RTW_READ(regs, RTW8180_PHYCFG) &
		    RTW8180_PHYCFG_MAC_POLL) == 0) {
			RTW_DPRINTF(RTW_DEBUG_PHY,
			    ("%s: finished in %dus\n", __func__,
			    rtw_macbangbits_timeout - i));
			return 0;
		}
		RTW_RBR(regs, RTW8180_PHYCFG, RTW8180_PHYCFG);
	}

	printf("%s: RTW8180_PHYCFG_MAC_POLL still set.\n", __func__);
	return -1;
}

u_int32_t
rtw_grf5101_host_crypt(u_int addr, u_int32_t val)
{
	/* TBD */
	return 0;
}

u_int32_t
rtw_grf5101_mac_crypt(u_int addr, u_int32_t val)
{
	u_int32_t data_and_addr;
#define EXTRACT_NIBBLE(d, which) (((d) >> (4 * (which))) & 0xf)
	static u_int8_t caesar[16] = {
		0x0, 0x8, 0x4, 0xc,
		0x2, 0xa, 0x6, 0xe,
		0x1, 0x9, 0x5, 0xd,
		0x3, 0xb, 0x7, 0xf
	};
	data_and_addr =
	    caesar[EXTRACT_NIBBLE(val, 2)] |
	    (caesar[EXTRACT_NIBBLE(val, 1)] <<  4) |
	    (caesar[EXTRACT_NIBBLE(val, 0)] <<  8) |
	    (caesar[(addr >> 1) & 0xf]      << 12) |
	    ((addr & 0x1)                   << 16) |
	    (caesar[EXTRACT_NIBBLE(val, 3)] << 24);
	return LSHIFT(data_and_addr, RTW8180_PHYCFG_MAC_PHILIPS_ADDR_MASK |
	    RTW8180_PHYCFG_MAC_PHILIPS_DATA_MASK);
#undef EXTRACT_NIBBLE
}

/* Bang bits over the 3-wire interface. */
int
rtw_rf_hostwrite(struct rtw_softc *sc, u_int addr, u_int32_t val)
{
	u_int nbits;
	int lo_to_hi;
	u_int32_t bits;
	void(*rf_bangbits)(struct rtw_regs *, u_int32_t, int, u_int) =
	    rtw_rf_hostbangbits;

	RTW_DPRINTF(RTW_DEBUG_PHYIO, ("%s: [%u] <- %#08x\n", __func__,
	    addr, val));

	switch (sc->sc_rfchipid) {
	case RTW_RFCHIPID_MAXIM2820:
		nbits = 16;
		lo_to_hi = 0;
		bits = LSHIFT(val, MAX2820_TWI_DATA_MASK) |
		    LSHIFT(addr, MAX2820_TWI_ADDR_MASK);
		break;
	case RTW_RFCHIPID_PHILIPS:
		KASSERT((addr & ~PRESHIFT(SA2400_TWI_ADDR_MASK)) == 0);
		KASSERT((val & ~PRESHIFT(SA2400_TWI_DATA_MASK)) == 0);
		bits = LSHIFT(val, SA2400_TWI_DATA_MASK) |
		    LSHIFT(addr, SA2400_TWI_ADDR_MASK) | SA2400_TWI_WREN;
		nbits = 32;
		lo_to_hi = 1;
		break;
	case RTW_RFCHIPID_GCT:
		KASSERT((addr & ~PRESHIFT(SI4126_TWI_ADDR_MASK)) == 0);
		KASSERT((val & ~PRESHIFT(SI4126_TWI_DATA_MASK)) == 0);
		bits = rtw_grf5101_host_crypt(addr, val);
		nbits = 21;
		lo_to_hi = 1;
		break;
	case RTW_RFCHIPID_RFMD2948:
		KASSERT((addr & ~PRESHIFT(SI4126_TWI_ADDR_MASK)) == 0);
		KASSERT((val & ~PRESHIFT(SI4126_TWI_DATA_MASK)) == 0);
		bits = LSHIFT(val, SI4126_TWI_DATA_MASK) |
		    LSHIFT(addr, SI4126_TWI_ADDR_MASK);
		nbits = 22;
		lo_to_hi = 0;
		break;
	case RTW_RFCHIPID_RTL8225:
	case RTW_RFCHIPID_RTL8255:
		nbits = 16;
		lo_to_hi = 0;
		bits = LSHIFT(val, RTL8225_TWI_DATA_MASK) |
		    LSHIFT(addr, RTL8225_TWI_ADDR_MASK);

		/* the RTL8225 uses a slightly modified RF interface */
		rf_bangbits = rtw_rf_hostbangbits;
		break;
	case RTW_RFCHIPID_INTERSIL:
	default:
		printf("%s: unknown rfchipid %d\n", __func__, sc->sc_rfchipid);
		return -1;
	}

	(*rf_bangbits)(&sc->sc_regs, bits, lo_to_hi, nbits);

	return 0;
}

u_int32_t
rtw_maxim_swizzle(u_int addr, u_int32_t val)
{
	u_int32_t hidata, lodata;

	KASSERT((val & ~(RTW_MAXIM_LODATA_MASK|RTW_MAXIM_HIDATA_MASK)) == 0);
	lodata = MASK_AND_RSHIFT(val, RTW_MAXIM_LODATA_MASK);
	hidata = MASK_AND_RSHIFT(val, RTW_MAXIM_HIDATA_MASK);
	return LSHIFT(lodata, RTW8180_PHYCFG_MAC_MAXIM_LODATA_MASK) |
	    LSHIFT(hidata, RTW8180_PHYCFG_MAC_MAXIM_HIDATA_MASK) |
	    LSHIFT(addr, RTW8180_PHYCFG_MAC_MAXIM_ADDR_MASK);
}

/* Tell the MAC what to bang over the 3-wire interface. */
int
rtw_rf_macwrite(struct rtw_softc *sc, u_int addr, u_int32_t val)
{
	u_int32_t reg;

	RTW_DPRINTF(RTW_DEBUG_PHYIO, ("%s: %s[%u] <- %#08x\n", __func__,
	    addr, val));

	switch (sc->sc_rfchipid) {
	case RTW_RFCHIPID_GCT:
		reg = rtw_grf5101_mac_crypt(addr, val);
		break;
	case RTW_RFCHIPID_MAXIM2820:
		reg = rtw_maxim_swizzle(addr, val);
		break;
	default:		/* XXX */
	case RTW_RFCHIPID_PHILIPS:
		KASSERT((addr &
		    ~PRESHIFT(RTW8180_PHYCFG_MAC_PHILIPS_ADDR_MASK)) == 0);
		KASSERT((val &
		    ~PRESHIFT(RTW8180_PHYCFG_MAC_PHILIPS_DATA_MASK)) == 0);

		reg = LSHIFT(addr, RTW8180_PHYCFG_MAC_PHILIPS_ADDR_MASK) |
		    LSHIFT(val, RTW8180_PHYCFG_MAC_PHILIPS_DATA_MASK);
	}

	switch (sc->sc_rfchipid) {
	case RTW_RFCHIPID_GCT:
	case RTW_RFCHIPID_MAXIM2820:
	case RTW_RFCHIPID_RFMD2948:
		reg |= RTW8180_PHYCFG_MAC_RFTYPE_RFMD;
		break;
	case RTW_RFCHIPID_INTERSIL:
		reg |= RTW8180_PHYCFG_MAC_RFTYPE_INTERSIL;
		break;
	case RTW_RFCHIPID_PHILIPS:
		reg |= RTW8180_PHYCFG_MAC_RFTYPE_PHILIPS;
		break;
	default:
		printf("%s: unknown rfchipid %d\n", __func__, sc->sc_rfchipid);
		return -1;
	}

	return rtw_rf_macbangbits(&sc->sc_regs, reg);
}


u_int8_t
rtw_read8(void *arg, u_int32_t off)
{
	struct rtw_regs *regs = (struct rtw_regs *)arg;
	return (bus_space_read_1(regs->r_bt, regs->r_bh, off));
}

u_int16_t
rtw_read16(void *arg, u_int32_t off)
{
	struct rtw_regs *regs = (struct rtw_regs *)arg;
	return (bus_space_read_2(regs->r_bt, regs->r_bh, off));
}

u_int32_t
rtw_read32(void *arg, u_int32_t off)
{
	struct rtw_regs *regs = (struct rtw_regs *)arg;
	return (bus_space_read_4(regs->r_bt, regs->r_bh, off));
}

void
rtw_write8(void *arg, u_int32_t off, u_int8_t val)
{
	struct rtw_regs *regs = (struct rtw_regs *)arg;
	bus_space_write_1(regs->r_bt, regs->r_bh, off, val);
}

void
rtw_write16(void *arg, u_int32_t off, u_int16_t val)
{
	struct rtw_regs *regs = (struct rtw_regs *)arg;
	bus_space_write_2(regs->r_bt, regs->r_bh, off, val);
}

void
rtw_write32(void *arg, u_int32_t off, u_int32_t val)
{
	struct rtw_regs *regs = (struct rtw_regs *)arg;
	bus_space_write_4(regs->r_bt, regs->r_bh, off, val);
}

void
rtw_barrier(void *arg, u_int32_t reg0, u_int32_t reg1, int flags)
{
	struct rtw_regs *regs = (struct rtw_regs *)arg;
	bus_space_barrier(regs->r_bt, regs->r_bh, MIN(reg0, reg1),
	    MAX(reg0, reg1) - MIN(reg0, reg1) + 4, flags);
}
