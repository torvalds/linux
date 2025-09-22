/*	$OpenBSD: bwi.c,v 1.135 2024/04/13 23:44:11 jsg Exp $	*/

/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/dev/netif/bwi/bwimac.c,v 1.1 2007/09/08 06:15:54 sephe Exp $
 */

#include "bpfilter.h"

#include <sys/param.h>

#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/ic/bwireg.h>
#include <dev/ic/bwivar.h>

#include <uvm/uvm_extern.h>

#ifdef BWI_DEBUG
int bwi_debug = 1;
#define DPRINTF(l, x...)	do { if ((l) <= bwi_debug) printf(x); } while (0)
#else
#define DPRINTF(l, x...)
#endif

/* XXX temporary porting goop */
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

/* XXX does not belong here */
#define IEEE80211_OFDM_PLCP_RATE_MASK	0x0000000f
#define IEEE80211_OFDM_PLCP_LEN_MASK	0x0001ffe0

/*
 * Contention window (slots).
 */
#define IEEE80211_CW_MAX	1023	/* aCWmax */
#define IEEE80211_CW_MIN_0	31	/* DS/CCK aCWmin, ERP aCWmin(0) */
#define IEEE80211_CW_MIN_1	15	/* OFDM aCWmin, ERP aCWmin(1) */

#define __unused __attribute__((__unused__))

extern int ticks;

/* XXX end porting goop */

/* MAC */
struct bwi_retry_lim {
	uint16_t	shretry;
	uint16_t	shretry_fb;
	uint16_t	lgretry;
	uint16_t	lgretry_fb;
};

struct bwi_clock_freq {
	uint		clkfreq_min;
	uint		clkfreq_max;
};

/* XXX does not belong here */
struct ieee80211_ds_plcp_hdr {
	uint8_t		i_signal;
	uint8_t		i_service;
	uint16_t	i_length;
	uint16_t	i_crc;
} __packed;

enum bwi_modtype {
	IEEE80211_MODTYPE_DS	= 0,	/* DS/CCK modulation */
	IEEE80211_MODTYPE_PBCC	= 1,	/* PBCC modulation */
	IEEE80211_MODTYPE_OFDM	= 2	/* OFDM modulation */
};
#define IEEE80211_MODTYPE_CCK   IEEE80211_MODTYPE_DS

/* MAC */
void		 bwi_tmplt_write_4(struct bwi_mac *, uint32_t, uint32_t);
void		 bwi_hostflags_write(struct bwi_mac *, uint64_t);
uint64_t	 bwi_hostflags_read(struct bwi_mac *);
uint16_t	 bwi_memobj_read_2(struct bwi_mac *, uint16_t, uint16_t);
uint32_t	 bwi_memobj_read_4(struct bwi_mac *, uint16_t, uint16_t);
void		 bwi_memobj_write_2(struct bwi_mac *, uint16_t, uint16_t,
		     uint16_t);
void		 bwi_memobj_write_4(struct bwi_mac *, uint16_t, uint16_t,
		     uint32_t);
int		 bwi_mac_lateattach(struct bwi_mac *);
int		 bwi_mac_init(struct bwi_mac *);
void		 bwi_mac_reset(struct bwi_mac *, int);
void		 bwi_mac_set_tpctl_11bg(struct bwi_mac *,
		     const struct bwi_tpctl *);
int		 bwi_mac_test(struct bwi_mac *);
void		 bwi_mac_setup_tpctl(struct bwi_mac *);
void		 bwi_mac_dummy_xmit(struct bwi_mac *);
void		 bwi_mac_init_tpctl_11bg(struct bwi_mac *);
void		 bwi_mac_detach(struct bwi_mac *);
int		 bwi_get_firmware(const char *, const uint8_t *, size_t,
		     size_t *, size_t *);
int		 bwi_fwimage_is_valid(struct bwi_softc *, uint8_t *,
		     size_t, char *, uint8_t);
int		 bwi_mac_fw_alloc(struct bwi_mac *);
void		 bwi_mac_fw_free(struct bwi_mac *);
int		 bwi_mac_fw_load(struct bwi_mac *);
int		 bwi_mac_gpio_init(struct bwi_mac *);
int		 bwi_mac_gpio_fini(struct bwi_mac *);
int		 bwi_mac_fw_load_iv(struct bwi_mac *, uint8_t *, size_t);
int		 bwi_mac_fw_init(struct bwi_mac *);
void		 bwi_mac_opmode_init(struct bwi_mac *);
void		 bwi_mac_hostflags_init(struct bwi_mac *);
void		 bwi_mac_bss_param_init(struct bwi_mac *);
void		 bwi_mac_set_retry_lim(struct bwi_mac *,
		     const struct bwi_retry_lim *);
void		 bwi_mac_set_ackrates(struct bwi_mac *,
		     const struct ieee80211_rateset *);
int		 bwi_mac_start(struct bwi_mac *);
int		 bwi_mac_stop(struct bwi_mac *);
int		 bwi_mac_config_ps(struct bwi_mac *);
void		 bwi_mac_reset_hwkeys(struct bwi_mac *);
void		 bwi_mac_shutdown(struct bwi_mac *);
int		 bwi_mac_get_property(struct bwi_mac *);
void		 bwi_mac_updateslot(struct bwi_mac *, int);
int		 bwi_mac_attach(struct bwi_softc *, int, uint8_t);
void		 bwi_mac_balance_atten(int *, int *);
void		 bwi_mac_adjust_tpctl(struct bwi_mac *, int, int);
void		 bwi_mac_calibrate_txpower(struct bwi_mac *,
		     enum bwi_txpwrcb_type);
void		 bwi_mac_lock(struct bwi_mac *);
void		 bwi_mac_unlock(struct bwi_mac *);
void		 bwi_mac_set_promisc(struct bwi_mac *, int);

/* PHY */
void		 bwi_phy_write(struct bwi_mac *, uint16_t, uint16_t);
uint16_t	 bwi_phy_read(struct bwi_mac *, uint16_t);
int		 bwi_phy_attach(struct bwi_mac *);
void		 bwi_phy_set_bbp_atten(struct bwi_mac *, uint16_t);
int		 bwi_phy_calibrate(struct bwi_mac *);
void		 bwi_tbl_write_2(struct bwi_mac *mac, uint16_t, uint16_t);
void		 bwi_tbl_write_4(struct bwi_mac *mac, uint16_t, uint32_t);
void		 bwi_nrssi_write(struct bwi_mac *, uint16_t, int16_t);
int16_t		 bwi_nrssi_read(struct bwi_mac *, uint16_t);
void		 bwi_phy_init_11a(struct bwi_mac *);
void		 bwi_phy_init_11g(struct bwi_mac *);
void		 bwi_phy_init_11b_rev2(struct bwi_mac *);
void		 bwi_phy_init_11b_rev4(struct bwi_mac *);
void		 bwi_phy_init_11b_rev5(struct bwi_mac *);
void		 bwi_phy_init_11b_rev6(struct bwi_mac *);
void		 bwi_phy_config_11g(struct bwi_mac *);
void		 bwi_phy_config_agc(struct bwi_mac *);
void		 bwi_set_gains(struct bwi_mac *, const struct bwi_gains *);
void		 bwi_phy_clear_state(struct bwi_phy *);

/* RF */
int16_t		 bwi_nrssi_11g(struct bwi_mac *);
struct bwi_rf_lo
		*bwi_get_rf_lo(struct bwi_mac *, uint16_t, uint16_t);
int		 bwi_rf_lo_isused(struct bwi_mac *, const struct bwi_rf_lo *);
void		 bwi_rf_write(struct bwi_mac *, uint16_t, uint16_t);
uint16_t	 bwi_rf_read(struct bwi_mac *, uint16_t);
int		 bwi_rf_attach(struct bwi_mac *);
void		 bwi_rf_set_chan(struct bwi_mac *, uint, int);
void		 bwi_rf_get_gains(struct bwi_mac *);
void		 bwi_rf_init(struct bwi_mac *);
void		 bwi_rf_off_11a(struct bwi_mac *);
void		 bwi_rf_off_11bg(struct bwi_mac *);
void		 bwi_rf_off_11g_rev5(struct bwi_mac *);
void		 bwi_rf_workaround(struct bwi_mac *, uint);
struct bwi_rf_lo
		*bwi_rf_lo_find(struct bwi_mac *, const struct bwi_tpctl *);
void		 bwi_rf_lo_adjust(struct bwi_mac *, const struct bwi_tpctl *);
void		 bwi_rf_lo_write(struct bwi_mac *, const struct bwi_rf_lo *);
int		 bwi_rf_gain_max_reached(struct bwi_mac *, int);
uint16_t	 bwi_bitswap4(uint16_t);
uint16_t	 bwi_phy812_value(struct bwi_mac *, uint16_t);
void		 bwi_rf_init_bcm2050(struct bwi_mac *);
uint16_t	 bwi_rf_calibval(struct bwi_mac *);
int32_t		 _bwi_adjust_devide(int32_t, int32_t);
int		 bwi_rf_calc_txpower(int8_t *, uint8_t, const int16_t[]);
int		 bwi_rf_map_txpower(struct bwi_mac *);
void		 bwi_rf_lo_update_11g(struct bwi_mac *);
uint32_t	 bwi_rf_lo_devi_measure(struct bwi_mac *, uint16_t);
uint16_t	 bwi_rf_get_tp_ctrl2(struct bwi_mac *);
uint8_t		 _bwi_rf_lo_update_11g(struct bwi_mac *, uint16_t);
void		 bwi_rf_lo_measure_11g(struct bwi_mac *,
		     const struct bwi_rf_lo *, struct bwi_rf_lo *, uint8_t);
void		 bwi_rf_calc_nrssi_slope_11b(struct bwi_mac *);
void		 bwi_rf_set_nrssi_ofs_11g(struct bwi_mac *);
void		 bwi_rf_calc_nrssi_slope_11g(struct bwi_mac *);
void		 bwi_rf_init_sw_nrssi_table(struct bwi_mac *);
void		 bwi_rf_init_hw_nrssi_table(struct bwi_mac *, uint16_t);
void		 bwi_rf_set_nrssi_thr_11b(struct bwi_mac *);
int32_t		 _nrssi_threshold(const struct bwi_rf *, int32_t);
void		 bwi_rf_set_nrssi_thr_11g(struct bwi_mac *);
void		 bwi_rf_clear_tssi(struct bwi_mac *);
void		 bwi_rf_clear_state(struct bwi_rf *);
void		 bwi_rf_on_11a(struct bwi_mac *);
void		 bwi_rf_on_11bg(struct bwi_mac *);
void		 bwi_rf_set_ant_mode(struct bwi_mac *, int);
int		 bwi_rf_get_latest_tssi(struct bwi_mac *, int8_t[], uint16_t);
int		 bwi_rf_tssi2dbm(struct bwi_mac *, int8_t, int8_t *);
int		 bwi_rf_calc_rssi_bcm2050(struct bwi_mac *,
		     const struct bwi_rxbuf_hdr *);
int		 bwi_rf_calc_rssi_bcm2053(struct bwi_mac *,
		     const struct bwi_rxbuf_hdr *);
int		 bwi_rf_calc_rssi_bcm2060(struct bwi_mac *,
		     const struct bwi_rxbuf_hdr *);
uint16_t	 bwi_rf_lo_measure_11b(struct bwi_mac *);
void		 bwi_rf_lo_update_11b(struct bwi_mac *);

/* INTERFACE */
uint16_t	 bwi_read_sprom(struct bwi_softc *, uint16_t);
void		 bwi_setup_desc32(struct bwi_softc *, struct bwi_desc32 *, int,
		     int, bus_addr_t, int, int);
void		 bwi_power_on(struct bwi_softc *, int);
int		 bwi_power_off(struct bwi_softc *, int);
int		 bwi_regwin_switch(struct bwi_softc *, struct bwi_regwin *,
		     struct bwi_regwin **);
int		 bwi_regwin_select(struct bwi_softc *, int);
void		 bwi_regwin_info(struct bwi_softc *, uint16_t *, uint8_t *);
void		 bwi_led_attach(struct bwi_softc *);
void		 bwi_led_newstate(struct bwi_softc *, enum ieee80211_state);
uint16_t	 bwi_led_onoff(struct bwi_led *, uint16_t, int);
void		 bwi_led_event(struct bwi_softc *, int);
void		 bwi_led_blink_start(struct bwi_softc *, int, int);
void		 bwi_led_blink_next(void *);
void		 bwi_led_blink_end(void *);
int		 bwi_bbp_attach(struct bwi_softc *);
int		 bwi_bus_init(struct bwi_softc *, struct bwi_mac *);
void		 bwi_get_card_flags(struct bwi_softc *);
void		 bwi_get_eaddr(struct bwi_softc *, uint16_t, uint8_t *);
void		 bwi_get_clock_freq(struct bwi_softc *,
		     struct bwi_clock_freq *);
int		 bwi_set_clock_mode(struct bwi_softc *, enum bwi_clock_mode);
int		 bwi_set_clock_delay(struct bwi_softc *);
int		 bwi_ioctl(struct ifnet *, u_long, caddr_t);
void		 bwi_start(struct ifnet *);
void		 bwi_watchdog(struct ifnet *);
void		 bwi_newstate_begin(struct bwi_softc *, enum ieee80211_state);
void		 bwi_init_statechg(struct bwi_softc *, int);
int		 bwi_newstate(struct ieee80211com *, enum ieee80211_state, int);
int		 bwi_media_change(struct ifnet *);
void		 bwi_iter_func(void *, struct ieee80211_node *);
void		 bwi_amrr_timeout(void *);
void		 bwi_newassoc(struct ieee80211com *, struct ieee80211_node *,
		     int);
struct ieee80211_node
		*bwi_node_alloc(struct ieee80211com *ic);
int		 bwi_dma_alloc(struct bwi_softc *);
void		 bwi_dma_free(struct bwi_softc *);
int		 bwi_dma_ring_alloc(struct bwi_softc *,
		     struct bwi_ring_data *, bus_size_t, uint32_t);
int		 bwi_dma_txstats_alloc(struct bwi_softc *, uint32_t,
		     bus_size_t);
void		 bwi_dma_txstats_free(struct bwi_softc *);
int		 bwi_dma_mbuf_create30(struct bwi_softc *);
int		 bwi_dma_mbuf_create(struct bwi_softc *);
void		 bwi_dma_mbuf_destroy(struct bwi_softc *, int, int);
void		 bwi_enable_intrs(struct bwi_softc *, uint32_t);
void		 bwi_disable_intrs(struct bwi_softc *, uint32_t);
int		 bwi_init_tx_ring32(struct bwi_softc *, int);
void		 bwi_init_rxdesc_ring32(struct bwi_softc *, uint32_t,
		     bus_addr_t, int, int);
int		 bwi_init_rx_ring32(struct bwi_softc *);
int		 bwi_init_txstats32(struct bwi_softc *);
void		 bwi_setup_rx_desc32(struct bwi_softc *, int, bus_addr_t, int);
void		 bwi_setup_tx_desc32(struct bwi_softc *, struct bwi_ring_data *,
		     int, bus_addr_t, int);
int		 bwi_newbuf30(struct bwi_softc *, int, int);
int		 bwi_newbuf(struct bwi_softc *, int, int);
void		 bwi_set_addr_filter(struct bwi_softc *, uint16_t,
		     const uint8_t *);
int		 bwi_set_chan(struct bwi_softc *, uint8_t);
void		 bwi_next_scan(void *);
int		 bwi_rxeof(struct bwi_softc *, int);
int		 bwi_rxeof32(struct bwi_softc *);
void		 bwi_reset_rx_ring32(struct bwi_softc *, uint32_t);
void		 bwi_free_txstats32(struct bwi_softc *);
void		 bwi_free_rx_ring32(struct bwi_softc *);
void		 bwi_free_tx_ring32(struct bwi_softc *, int);
uint8_t		 bwi_plcp2rate(uint32_t, enum ieee80211_phymode);
void		 bwi_ofdm_plcp_header(uint32_t *, int, uint8_t);
void		 bwi_ds_plcp_header(struct ieee80211_ds_plcp_hdr *, int,
		     uint8_t);
void		 bwi_plcp_header(void *, int, uint8_t);
int		 bwi_encap(struct bwi_softc *, int, struct mbuf *,
		     struct ieee80211_node *);
void		 bwi_start_tx32(struct bwi_softc *, uint32_t, int);
void		 bwi_txeof_status32(struct bwi_softc *);
void		 _bwi_txeof(struct bwi_softc *, uint16_t);
void		 bwi_txeof_status(struct bwi_softc *, int);
void		 bwi_txeof(struct bwi_softc *);
int		 bwi_bbp_power_on(struct bwi_softc *, enum bwi_clock_mode);
void		 bwi_bbp_power_off(struct bwi_softc *);
int		 bwi_get_pwron_delay(struct bwi_softc *sc);
int		 bwi_bus_attach(struct bwi_softc *);
const char 	*bwi_regwin_name(const struct bwi_regwin *);
int		 bwi_regwin_is_enabled(struct bwi_softc *, struct bwi_regwin *);
uint32_t	 bwi_regwin_disable_bits(struct bwi_softc *);
void		 bwi_regwin_enable(struct bwi_softc *, struct bwi_regwin *,
		     uint32_t);
void		 bwi_regwin_disable(struct bwi_softc *, struct bwi_regwin *,
		     uint32_t);
void		 bwi_set_bssid(struct bwi_softc *, const uint8_t *);
void		 bwi_updateslot(struct ieee80211com *);
void		 bwi_calibrate(void *);
int		 bwi_calc_rssi(struct bwi_softc *,
		     const struct bwi_rxbuf_hdr *);
uint8_t		 bwi_ack_rate(struct ieee80211_node *, uint8_t);
uint16_t	 bwi_txtime(struct ieee80211com *, struct ieee80211_node *,
		     uint, uint8_t, uint32_t);
enum bwi_modtype
		 bwi_rate2modtype(uint8_t);


static const uint8_t bwi_sup_macrev[] = { 2, 4, 5, 6, 7, 9, 10 };

#define SUP_BPHY(num)	{ .rev = num, .init = bwi_phy_init_11b_rev##num }

static const struct {
	uint8_t	rev;
	void	(*init)(struct bwi_mac *);
} bwi_sup_bphy[] = {
	SUP_BPHY(2),
	SUP_BPHY(4),
	SUP_BPHY(5),
	SUP_BPHY(6)
};

#undef SUP_BPHY

#define BWI_PHYTBL_WRSSI	0x1000
#define BWI_PHYTBL_NOISE_SCALE	0x1400
#define BWI_PHYTBL_NOISE	0x1800
#define BWI_PHYTBL_ROTOR	0x2000
#define BWI_PHYTBL_DELAY	0x2400
#define BWI_PHYTBL_RSSI		0x4000
#define BWI_PHYTBL_SIGMA_SQ	0x5000
#define BWI_PHYTBL_WRSSI_REV1	0x5400
#define BWI_PHYTBL_FREQ		0x5800

static const uint16_t	bwi_phy_freq_11g_rev1[] =
	{ BWI_PHY_FREQ_11G_REV1 };
static const uint16_t	bwi_phy_noise_11g_rev1[] =
	{ BWI_PHY_NOISE_11G_REV1 };
static const uint16_t	bwi_phy_noise_11g[] =
	{ BWI_PHY_NOISE_11G };
static const uint32_t	bwi_phy_rotor_11g_rev1[] =
	{ BWI_PHY_ROTOR_11G_REV1 };
static const uint16_t	bwi_phy_noise_scale_11g_rev2[] =
	{ BWI_PHY_NOISE_SCALE_11G_REV2 };
static const uint16_t	bwi_phy_noise_scale_11g_rev7[] =
	{ BWI_PHY_NOISE_SCALE_11G_REV7 };
static const uint16_t	bwi_phy_noise_scale_11g[] =
	{ BWI_PHY_NOISE_SCALE_11G };
static const uint16_t	bwi_phy_sigma_sq_11g_rev2[] =
	{ BWI_PHY_SIGMA_SQ_11G_REV2 };
static const uint16_t	bwi_phy_sigma_sq_11g_rev7[] =
	{ BWI_PHY_SIGMA_SQ_11G_REV7 };
static const uint32_t	bwi_phy_delay_11g_rev1[] =
	{ BWI_PHY_DELAY_11G_REV1 };

/* RF */
#define RF_LO_WRITE(mac, lo)	bwi_rf_lo_write((mac), (lo))

#define BWI_RF_2GHZ_CHAN(chan) \
	(ieee80211_ieee2mhz((chan), IEEE80211_CHAN_2GHZ) - 2400)

#define BWI_DEFAULT_IDLE_TSSI	52

struct rf_saveregs {
	uint16_t	phy_01;
	uint16_t	phy_03;
	uint16_t	phy_0a;
	uint16_t	phy_15;
	uint16_t	phy_2a;
	uint16_t	phy_30;
	uint16_t	phy_35;
	uint16_t	phy_60;
	uint16_t	phy_429;
	uint16_t	phy_802;
	uint16_t	phy_811;
	uint16_t	phy_812;
	uint16_t	phy_814;
	uint16_t	phy_815;

	uint16_t	rf_43;
	uint16_t	rf_52;
	uint16_t	rf_7a;
};

#define SAVE_RF_REG(mac, regs, n)	(regs)->rf_##n = RF_READ((mac), 0x##n)
#define RESTORE_RF_REG(mac, regs, n)	RF_WRITE((mac), 0x##n, (regs)->rf_##n)

#define SAVE_PHY_REG(mac, regs, n)	(regs)->phy_##n = PHY_READ((mac), 0x##n)
#define RESTORE_PHY_REG(mac, regs, n)	PHY_WRITE((mac), 0x##n, (regs)->phy_##n)

static const int8_t	bwi_txpower_map_11b[BWI_TSSI_MAX] =
	{ BWI_TXPOWER_MAP_11B };
static const int8_t	bwi_txpower_map_11g[BWI_TSSI_MAX] =
	{ BWI_TXPOWER_MAP_11G };

/* IF_BWI */

struct bwi_myaddr_bssid {
	uint8_t		myaddr[IEEE80211_ADDR_LEN];
	uint8_t		bssid[IEEE80211_ADDR_LEN];
} __packed;

#define IEEE80211_DS_PLCP_SERVICE_LOCKED	0x04
#define IEEE80211_DS_PLCL_SERVICE_PBCC		0x08
#define IEEE80211_DS_PLCP_SERVICE_LENEXT5	0x20
#define IEEE80211_DS_PLCP_SERVICE_LENEXT6	0x40
#define IEEE80211_DS_PLCP_SERVICE_LENEXT7	0x80

struct cfdriver bwi_cd = {
	NULL, "bwi", DV_IFNET
};

static const struct {
	uint16_t	did_min;
	uint16_t	did_max;
	uint16_t	bbp_id;
} bwi_bbpid_map[] = {
	{ 0x4301, 0x4301, 0x4301 },
	{ 0x4305, 0x4307, 0x4307 },
	{ 0x4402, 0x4403, 0x4402 },
	{ 0x4610, 0x4615, 0x4610 },
	{ 0x4710, 0x4715, 0x4710 },
	{ 0x4720, 0x4725, 0x4309 }
};

static const struct {
	uint16_t	bbp_id;
	int		nregwin;
} bwi_regwin_count[] = {
	{ 0x4301, 5 },
	{ 0x4306, 6 },
	{ 0x4307, 5 },
	{ 0x4310, 8 },
	{ 0x4401, 3 },
	{ 0x4402, 3 },
	{ 0x4610, 9 },
	{ 0x4704, 9 },
	{ 0x4710, 9 },
	{ 0x5365, 7 }
};

#define CLKSRC(src) 				\
[BWI_CLKSRC_ ## src] = {			\
	.freq_min = BWI_CLKSRC_ ##src## _FMIN,	\
	.freq_max = BWI_CLKSRC_ ##src## _FMAX	\
}

static const struct {
	uint	freq_min;
	uint	freq_max;
} bwi_clkfreq[BWI_CLKSRC_MAX] = {
	CLKSRC(LP_OSC),
	CLKSRC(CS_OSC),
	CLKSRC(PCI)
};

#undef CLKSRC

#define VENDOR_LED_ACT(vendor)				\
{							\
	.vid = PCI_VENDOR_##vendor,			\
	.led_act = { BWI_VENDOR_LED_ACT_##vendor }	\
}

const struct {
	uint16_t	vid;
	uint8_t		led_act[BWI_LED_MAX];
} bwi_vendor_led_act[] = {
	VENDOR_LED_ACT(COMPAQ),
	VENDOR_LED_ACT(LINKSYS)
};

const uint8_t bwi_default_led_act[BWI_LED_MAX] =
	{ BWI_VENDOR_LED_ACT_DEFAULT };

#undef VENDOR_LED_ACT

const struct {
	int	on_dur;
	int	off_dur;
} bwi_led_duration[109] = {
	{ 400, 100 }, {   0,   0 }, { 150 ,  75 }, {   0,   0 }, {  90,  45 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {  66,  34 }, {  53,   26 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {  42,  21 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {  35,   17 }, {   0,   0 }, {  32,  16 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {  21,  10 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {  16,   8 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {  11,    5 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   9,   4 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   0,   0 }, {   0,   0 },
	{   0,   0 }, {   0,   0 }, {   0,    0 }, {   7,   3 }
};

static const uint8_t bwi_zero_addr[IEEE80211_ADDR_LEN];


/* CODE */

int
bwi_intr(void *xsc)
{
	struct bwi_softc *sc = xsc;
	struct bwi_mac *mac;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t intr_status;
	uint32_t txrx_intr_status[BWI_TXRX_NRING];
	int i, txrx_error, tx = 0, rx_data = -1;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return (0);

	/*
	 * Get interrupt status
	 */
	intr_status = CSR_READ_4(sc, BWI_MAC_INTR_STATUS);
	if (intr_status == 0xffffffff)	/* Not for us */
		return (0);

	intr_status &= CSR_READ_4(sc, BWI_MAC_INTR_MASK);
	if (intr_status == 0)		/* Nothing is interesting */
		return (0);

	DPRINTF(2, "%s: intr status 0x%08x\n",
	    sc->sc_dev.dv_xname, intr_status);

	KASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	txrx_error = 0;

	for (i = 0; i < BWI_TXRX_NRING; ++i) {
		uint32_t mask;

		if (BWI_TXRX_IS_RX(i))
			mask = BWI_TXRX_RX_INTRS;
		else
			mask = BWI_TXRX_TX_INTRS;

		txrx_intr_status[i] =
		    CSR_READ_4(sc, BWI_TXRX_INTR_STATUS(i)) & mask;

		if (txrx_intr_status[i] & BWI_TXRX_INTR_ERROR) {
			printf("%s: intr fatal TX/RX (%d) error 0x%08x\n",
			    sc->sc_dev.dv_xname, i, txrx_intr_status[i]);
			txrx_error = 1;
		}
	}

	/*
	 * Acknowledge interrupt
	 */
	CSR_WRITE_4(sc, BWI_MAC_INTR_STATUS, intr_status);

	for (i = 0; i < BWI_TXRX_NRING; ++i)
		CSR_WRITE_4(sc, BWI_TXRX_INTR_STATUS(i), txrx_intr_status[i]);

	/* Disable all interrupts */
	bwi_disable_intrs(sc, BWI_ALL_INTRS);

	if (intr_status & BWI_INTR_PHY_TXERR) {
		if (mac->mac_flags & BWI_MAC_F_PHYE_RESET) {
			printf("intr PHY TX error\n");
			/* XXX to netisr0? */
			bwi_init_statechg(sc, 0);
			return (1);
		}
	}

	if (txrx_error) {
		/* TODO: reset device */
	}

	if (intr_status & BWI_INTR_TBTT)
		bwi_mac_config_ps(mac);

	if (intr_status & BWI_INTR_EO_ATIM)
		printf("%s: EO_ATIM\n", sc->sc_dev.dv_xname);

	if (intr_status & BWI_INTR_PMQ) {
		for (;;) {
			if ((CSR_READ_4(sc, BWI_MAC_PS_STATUS) & 0x8) == 0)
				break;
		}
		CSR_WRITE_2(sc, BWI_MAC_PS_STATUS, 0x2);
	}

	if (intr_status & BWI_INTR_NOISE)
		printf("%s: intr noise\n", sc->sc_dev.dv_xname);

	if (txrx_intr_status[0] & BWI_TXRX_INTR_RX)
		rx_data = sc->sc_rxeof(sc);

	if (txrx_intr_status[3] & BWI_TXRX_INTR_RX) {
		sc->sc_txeof_status(sc);
		tx = 1;
	}

	if (intr_status & BWI_INTR_TX_DONE) {
		bwi_txeof(sc);
		tx = 1;
	}

	/* Re-enable interrupts */
	bwi_enable_intrs(sc, BWI_INIT_INTRS);

	if (sc->sc_blink_led != NULL && sc->sc_led_blink) {
		int evt = BWI_LED_EVENT_NONE;

		if (tx && rx_data > 0) {
			if (sc->sc_rx_rate > sc->sc_tx_rate)
				evt = BWI_LED_EVENT_RX;
			else
				evt = BWI_LED_EVENT_TX;
		} else if (tx) {
			evt = BWI_LED_EVENT_TX;
		} else if (rx_data > 0) {
			evt = BWI_LED_EVENT_RX;
		} else if (rx_data == 0) {
			evt = BWI_LED_EVENT_POLL;
		}

		if (evt != BWI_LED_EVENT_NONE)
			bwi_led_event(sc, evt);
	}

	return (1);
}

int
bwi_attach(struct bwi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct bwi_mac *mac;
	struct bwi_phy *phy;
	int i, error;

	DPRINTF(1, "\n");

	/* Initialize LED vars */
	sc->sc_led_idle = (2350 * hz) / 1000;
	sc->sc_led_blink = 1;

	/* AMRR rate control */
	sc->sc_amrr.amrr_min_success_threshold = 1;
	sc->sc_amrr.amrr_max_success_threshold = 15;
	timeout_set(&sc->sc_amrr_ch, bwi_amrr_timeout, sc);

	timeout_set(&sc->sc_scan_ch, bwi_next_scan, sc);
	timeout_set(&sc->sc_calib_ch, bwi_calibrate, sc);

	bwi_power_on(sc, 1);

	error = bwi_bbp_attach(sc);
	if (error)
		goto fail;

	error = bwi_bbp_power_on(sc, BWI_CLOCK_MODE_FAST);
	if (error)
		goto fail;

	if (BWI_REGWIN_EXIST(&sc->sc_com_regwin)) {
		error = bwi_set_clock_delay(sc);
		if (error)
			goto fail;

		error = bwi_set_clock_mode(sc, BWI_CLOCK_MODE_FAST);
		if (error)
			goto fail;

		error = bwi_get_pwron_delay(sc);
		if (error)
			goto fail;
	}

	error = bwi_bus_attach(sc);
	if (error)
		goto fail;

	bwi_get_card_flags(sc);

	bwi_led_attach(sc);

	for (i = 0; i < sc->sc_nmac; ++i) {
		struct bwi_regwin *old;

		mac = &sc->sc_mac[i];
		error = bwi_regwin_switch(sc, &mac->mac_regwin, &old);
		if (error)
			goto fail;

		error = bwi_mac_lateattach(mac);
		if (error)
			goto fail;

		error = bwi_regwin_switch(sc, old, NULL);
		if (error)
			goto fail;
	}

	/*
	 * XXX First MAC is known to exist
	 * TODO2
	 */
	mac = &sc->sc_mac[0];
	phy = &mac->mac_phy;

	bwi_bbp_power_off(sc);

	error = bwi_dma_alloc(sc);
	if (error)
		goto fail;

	/* setup interface */
	ifp->if_softc = sc;
	ifp->if_ioctl = bwi_ioctl;
	ifp->if_start = bwi_start;
	ifp->if_watchdog = bwi_watchdog;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifq_init_maxlen(&ifp->if_snd, IFQ_MAXLEN);

	/* Get locale */
	sc->sc_locale = __SHIFTOUT(bwi_read_sprom(sc, BWI_SPROM_CARD_INFO),
	    BWI_SPROM_CARD_INFO_LOCALE);
	DPRINTF(1, "%s: locale: %d\n", sc->sc_dev.dv_xname, sc->sc_locale);

	/*
	 * Setup ratesets, phytype, channels and get MAC address
	 */
	if (phy->phy_mode == IEEE80211_MODE_11B ||
	    phy->phy_mode == IEEE80211_MODE_11G) {
	    	uint16_t chan_flags;

		ic->ic_sup_rates[IEEE80211_MODE_11B] =
		    ieee80211_std_rateset_11b;

		if (phy->phy_mode == IEEE80211_MODE_11B) {
			chan_flags = IEEE80211_CHAN_B;
			ic->ic_phytype = IEEE80211_T_DS;
		} else {
			chan_flags = IEEE80211_CHAN_CCK |
			    IEEE80211_CHAN_OFDM |
			    IEEE80211_CHAN_DYN |
			    IEEE80211_CHAN_2GHZ;
			ic->ic_phytype = IEEE80211_T_OFDM;
			ic->ic_sup_rates[IEEE80211_MODE_11G] =
			    ieee80211_std_rateset_11g;
		}

		/* XXX depend on locale */
		for (i = 1; i <= 14; ++i) {
			ic->ic_channels[i].ic_freq =
				ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
			ic->ic_channels[i].ic_flags = chan_flags;
		}

		bwi_get_eaddr(sc, BWI_SPROM_11BG_EADDR, ic->ic_myaddr);
		if (IEEE80211_IS_MULTICAST(ic->ic_myaddr)) {
			bwi_get_eaddr(sc, BWI_SPROM_11A_EADDR, ic->ic_myaddr);
			if (IEEE80211_IS_MULTICAST(ic->ic_myaddr)) {
				printf("%s: invalid MAC address: %s\n",
				    sc->sc_dev.dv_xname,
				    ether_sprintf(ic->ic_myaddr));
			}
		}
	} else if (phy->phy_mode == IEEE80211_MODE_11A) {
		/* TODO: 11A */
		error = ENXIO;
		goto fail;
	} else
		panic("unknown phymode %d", phy->phy_mode);

	printf(", address %s\n", ether_sprintf(ic->ic_myaddr));

	sc->sc_fw_version = BWI_FW_VERSION3;
	sc->sc_dwell_time = 200;

	ic->ic_caps = IEEE80211_C_SHSLOT |
	    IEEE80211_C_SHPREAMBLE |
	    IEEE80211_C_WEP |
	    IEEE80211_C_RSN |
	    IEEE80211_C_MONITOR;
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_opmode = IEEE80211_M_STA;

	ic->ic_updateslot = bwi_updateslot;

	if_attach(ifp);
	ieee80211_ifattach(ifp);

	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = bwi_newstate;
	ic->ic_newassoc = bwi_newassoc;
	ic->ic_node_alloc = bwi_node_alloc;

	ieee80211_media_init(ifp, bwi_media_change, ieee80211_media_status);

	if (error) {
		ieee80211_ifdetach(ifp);
		goto fail;
	}

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + 64);

	sc->sc_rxtap_len = sizeof(sc->sc_rxtapu);
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(BWI_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof(sc->sc_txtapu);
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(BWI_TX_RADIOTAP_PRESENT);
#endif

	return (0);
fail:
	return (error);
}

int
bwi_detach(void *arg)
{
	struct bwi_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s, i;

	bwi_stop(sc, 1);
	ieee80211_ifdetach(ifp);
	if_detach(ifp);

	for (i = 0; i < sc->sc_nmac; ++i)
		bwi_mac_detach(&sc->sc_mac[i]);

	s = splvm();
	bwi_dma_free(sc);
	splx(s);
	bwi_dma_mbuf_destroy(sc, BWI_TX_NRING, 1);

	return (0);
}

/* MAC */

void
bwi_tmplt_write_4(struct bwi_mac *mac, uint32_t ofs, uint32_t val)
{
	struct bwi_softc *sc = mac->mac_sc;

	if (mac->mac_flags & BWI_MAC_F_BSWAP)
		val = swap32(val);

	CSR_WRITE_4(sc, BWI_MAC_TMPLT_CTRL, ofs);
	CSR_WRITE_4(sc, BWI_MAC_TMPLT_DATA, val);
}

void
bwi_hostflags_write(struct bwi_mac *mac, uint64_t flags)
{
	uint64_t val;

	val = flags & 0xffff;
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_HFLAGS_LO, val);

	val = (flags >> 16) & 0xffff;
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_HFLAGS_MI, val);

	/* HI has unclear meaning, so leave it as it is */
}

uint64_t
bwi_hostflags_read(struct bwi_mac *mac)
{
	uint64_t flags, val;

	/* HI has unclear meaning, so don't touch it */
	flags = 0;

	val = MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_HFLAGS_MI);
	flags |= val << 16;

	val = MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_HFLAGS_LO);
	flags |= val;

	return (flags);
}

uint16_t
bwi_memobj_read_2(struct bwi_mac *mac, uint16_t obj_id, uint16_t ofs0)
{
	struct bwi_softc *sc = mac->mac_sc;
	uint32_t data_reg;
	int ofs;

	data_reg = BWI_MOBJ_DATA;
	ofs = ofs0 / 4;

	if (ofs0 % 4 != 0)
		data_reg = BWI_MOBJ_DATA_UNALIGN;

	CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
	return (CSR_READ_2(sc, data_reg));
}

uint32_t
bwi_memobj_read_4(struct bwi_mac *mac, uint16_t obj_id, uint16_t ofs0)
{
	struct bwi_softc *sc = mac->mac_sc;
	int ofs;

	ofs = ofs0 / 4;
	if (ofs0 % 4 != 0) {
		uint32_t ret;

		CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
		ret = CSR_READ_2(sc, BWI_MOBJ_DATA_UNALIGN);
		ret <<= 16;

		CSR_WRITE_4(sc, BWI_MOBJ_CTRL,
		    BWI_MOBJ_CTRL_VAL(obj_id, ofs + 1));
		ret |= CSR_READ_2(sc, BWI_MOBJ_DATA);

		return (ret);
	} else {
		CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
		return (CSR_READ_4(sc, BWI_MOBJ_DATA));
	}
}

void
bwi_memobj_write_2(struct bwi_mac *mac, uint16_t obj_id, uint16_t ofs0,
    uint16_t v)
{
	struct bwi_softc *sc = mac->mac_sc;
	uint32_t data_reg;
	int ofs;

	data_reg = BWI_MOBJ_DATA;
	ofs = ofs0 / 4;

	if (ofs0 % 4 != 0)
		data_reg = BWI_MOBJ_DATA_UNALIGN;

	CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
	CSR_WRITE_2(sc, data_reg, v);
}

void
bwi_memobj_write_4(struct bwi_mac *mac, uint16_t obj_id, uint16_t ofs0,
    uint32_t v)
{
	struct bwi_softc *sc = mac->mac_sc;
	int ofs;

	ofs = ofs0 / 4;
	if (ofs0 % 4 != 0) {
		CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
		CSR_WRITE_2(sc, BWI_MOBJ_DATA_UNALIGN, v >> 16);
		CSR_WRITE_4(sc, BWI_MOBJ_CTRL,
		    BWI_MOBJ_CTRL_VAL(obj_id, ofs + 1));
		CSR_WRITE_2(sc, BWI_MOBJ_DATA, v & 0xffff);
	} else {
		CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
		CSR_WRITE_4(sc, BWI_MOBJ_DATA, v);
	}
}

int
bwi_mac_lateattach(struct bwi_mac *mac)
{
	int error;

	if (mac->mac_rev >= 5)
		CSR_READ_4(mac->mac_sc, BWI_STATE_HI); /* dummy read */

	bwi_mac_reset(mac, 1);

	error = bwi_phy_attach(mac);
	if (error)
		return (error);

	error = bwi_rf_attach(mac);
	if (error)
		return (error);

	/* Link 11B/G PHY, unlink 11A PHY */
	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11A)
		bwi_mac_reset(mac, 0);
	else
		bwi_mac_reset(mac, 1);

	error = bwi_mac_test(mac);
	if (error)
		return (error);

	error = bwi_mac_get_property(mac);
	if (error)
		return (error);

	error = bwi_rf_map_txpower(mac);
	if (error)
		return (error);

	bwi_rf_off(mac);
	CSR_WRITE_2(mac->mac_sc, BWI_BBP_ATTEN, BWI_BBP_ATTEN_MAGIC);
	bwi_regwin_disable(mac->mac_sc, &mac->mac_regwin, 0);

	return (0);
}

int
bwi_mac_init(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	int error, i;

	/* Clear MAC/PHY/RF states */
	bwi_mac_setup_tpctl(mac);
	bwi_rf_clear_state(&mac->mac_rf);
	bwi_phy_clear_state(&mac->mac_phy);

	/* Enable MAC and linked it to PHY */
	if (!bwi_regwin_is_enabled(sc, &mac->mac_regwin))
		bwi_mac_reset(mac, 1);

	/* Initialize backplane */
	error = bwi_bus_init(sc, mac);
	if (error)
		return (error);

	/* XXX work around for hardware bugs? */
	if (sc->sc_bus_regwin.rw_rev <= 5 &&
	    sc->sc_bus_regwin.rw_type != BWI_REGWIN_T_BUSPCIE) {
		CSR_SETBITS_4(sc, BWI_CONF_LO,
		__SHIFTIN(BWI_CONF_LO_SERVTO, BWI_CONF_LO_SERVTO_MASK) |
		__SHIFTIN(BWI_CONF_LO_REQTO, BWI_CONF_LO_REQTO_MASK));
	}

	/* Calibrate PHY */
	error = bwi_phy_calibrate(mac);
	if (error) {
		printf("%s: PHY calibrate failed\n", sc->sc_dev.dv_xname);
		return (error);
	}

	/* Prepare to initialize firmware */
	CSR_WRITE_4(sc, BWI_MAC_STATUS,
	    BWI_MAC_STATUS_UCODE_JUMP0 |
	    BWI_MAC_STATUS_IHREN);

	/*
	 * Load and initialize firmwares
	 */
	error = bwi_mac_fw_alloc(mac);
	if (error)
		return (error);

	error = bwi_mac_fw_load(mac);
	if (error)
		return (error);

	error = bwi_mac_gpio_init(mac);
	if (error)
		return (error);

	error = bwi_mac_fw_init(mac);
	if (error)
		return (error);

	/*
	 * Turn on RF
	 */
	bwi_rf_on(mac);

	/* TODO: LED, hardware rf enabled is only related to LED setting */

	/*
	 * Initialize PHY
	 */
	CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0);
	bwi_phy_init(mac);

	/* TODO: interference mitigation */

	/*
	 * Setup antenna mode
	 */
	bwi_rf_set_ant_mode(mac, mac->mac_rf.rf_ant_mode);

	/*
	 * Initialize operation mode (RX configuration)
	 */
	bwi_mac_opmode_init(mac);

	/* XXX what's these */
	if (mac->mac_rev < 3) {
		CSR_WRITE_2(sc, 0x60e, 0);
		CSR_WRITE_2(sc, 0x610, 0x8000);
		CSR_WRITE_2(sc, 0x604, 0);
		CSR_WRITE_2(sc, 0x606, 0x200);
	} else {
		CSR_WRITE_4(sc, 0x188, 0x80000000);
		CSR_WRITE_4(sc, 0x18c, 0x2000000);
	}

	/*
	 * Initialize TX/RX interrupts' mask
	 */
	CSR_WRITE_4(sc, BWI_MAC_INTR_STATUS, BWI_INTR_TIMER1);
	for (i = 0; i < BWI_TXRX_NRING; ++i) {
		uint32_t intrs;

		if (BWI_TXRX_IS_RX(i))
			intrs = BWI_TXRX_RX_INTRS;
		else
			intrs = BWI_TXRX_TX_INTRS;
		CSR_WRITE_4(sc, BWI_TXRX_INTR_MASK(i), intrs);
	}

	/* XXX what's this */
	CSR_SETBITS_4(sc, BWI_STATE_LO, 0x100000);

	/* Setup MAC power up delay */
	CSR_WRITE_2(sc, BWI_MAC_POWERUP_DELAY, sc->sc_pwron_delay);

	/* Set MAC regwin revision */
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_MACREV, mac->mac_rev);

	/*
	 * Initialize host flags
	 */
	bwi_mac_hostflags_init(mac);

	/*
	 * Initialize BSS parameters
	 */
	bwi_mac_bss_param_init(mac);

	/*
	 * Initialize TX rings
	 */
	for (i = 0; i < BWI_TX_NRING; ++i) {
		error = sc->sc_init_tx_ring(sc, i);
		if (error) {
			printf("%s: can't initialize %dth TX ring\n",
			    sc->sc_dev.dv_xname, i);
			return (error);
		}
	}

	/*
	 * Initialize RX ring
	 */
	error = sc->sc_init_rx_ring(sc);
	if (error) {
		printf("%s: can't initialize RX ring\n", sc->sc_dev.dv_xname);
		return (error);
	}

	/*
	 * Initialize TX stats if the current MAC uses that
	 */
	if (mac->mac_flags & BWI_MAC_F_HAS_TXSTATS) {
		error = sc->sc_init_txstats(sc);
		if (error) {
			printf("%s: can't initialize TX stats ring\n",
			    sc->sc_dev.dv_xname);
			return (error);
		}
	}

	/* XXX what's these */
	CSR_WRITE_2(sc, 0x612, 0x50);	/* Force Pre-TBTT to 80? */
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, 0x416, 0x50);
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, 0x414, 0x1f4);

	mac->mac_flags |= BWI_MAC_F_INITED;

	return (0);
}

void
bwi_mac_reset(struct bwi_mac *mac, int link_phy)
{
	struct bwi_softc *sc = mac->mac_sc;
	uint32_t flags, state_lo, status;

	flags = BWI_STATE_LO_FLAG_PHYRST | BWI_STATE_LO_FLAG_PHYCLKEN;
	if (link_phy)
		flags |= BWI_STATE_LO_FLAG_PHYLNK;
	bwi_regwin_enable(sc, &mac->mac_regwin, flags);
	DELAY(2000);

	state_lo = CSR_READ_4(sc, BWI_STATE_LO);
	state_lo |= BWI_STATE_LO_GATED_CLOCK;
	state_lo &= ~__SHIFTIN(BWI_STATE_LO_FLAG_PHYRST,
			       BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);
	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1000);

	state_lo &= ~BWI_STATE_LO_GATED_CLOCK;
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);
	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1000);

	CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0);

	status = CSR_READ_4(sc, BWI_MAC_STATUS);
	status |= BWI_MAC_STATUS_IHREN;
	if (link_phy)
		status |= BWI_MAC_STATUS_PHYLNK;
	else
		status &= ~BWI_MAC_STATUS_PHYLNK;
	CSR_WRITE_4(sc, BWI_MAC_STATUS, status);

	if (link_phy) {
		DPRINTF(1, "%s: PHY is linked\n", sc->sc_dev.dv_xname);
		mac->mac_phy.phy_flags |= BWI_PHY_F_LINKED;
	} else {
		DPRINTF(1, "%s: PHY is unlinked\n", sc->sc_dev.dv_xname);
		mac->mac_phy.phy_flags &= ~BWI_PHY_F_LINKED;
	}
}

void
bwi_mac_set_tpctl_11bg(struct bwi_mac *mac, const struct bwi_tpctl *new_tpctl)
{
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_tpctl *tpctl = &mac->mac_tpctl;

	if (new_tpctl != NULL) {
		KASSERT(new_tpctl->bbp_atten <= BWI_BBP_ATTEN_MAX);
		KASSERT(new_tpctl->rf_atten <=
		    (rf->rf_rev < 6 ? BWI_RF_ATTEN_MAX0
		    : BWI_RF_ATTEN_MAX1));
		KASSERT(new_tpctl->tp_ctrl1 <= BWI_TPCTL1_MAX);

		tpctl->bbp_atten = new_tpctl->bbp_atten;
		tpctl->rf_atten = new_tpctl->rf_atten;
		tpctl->tp_ctrl1 = new_tpctl->tp_ctrl1;
	}

	/* Set BBP attenuation */
	bwi_phy_set_bbp_atten(mac, tpctl->bbp_atten);

	/* Set RF attenuation */
	RF_WRITE(mac, BWI_RFR_ATTEN, tpctl->rf_atten);
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_RF_ATTEN,
	    tpctl->rf_atten);

	/* Set TX power */
	if (rf->rf_type == BWI_RF_T_BCM2050) {
		RF_FILT_SETBITS(mac, BWI_RFR_TXPWR, ~BWI_RFR_TXPWR1_MASK,
		    __SHIFTIN(tpctl->tp_ctrl1, BWI_RFR_TXPWR1_MASK));
	}

	/* Adjust RF Local Oscillator */
	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11G)
		bwi_rf_lo_adjust(mac, tpctl);
}

int
bwi_mac_test(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	uint32_t orig_val, val;

#define TEST_VAL1	0xaa5555aa
#define TEST_VAL2	0x55aaaa55
	/* Save it for later restoring */
	orig_val = MOBJ_READ_4(mac, BWI_COMM_MOBJ, 0);

	/* Test 1 */
	MOBJ_WRITE_4(mac, BWI_COMM_MOBJ, 0, TEST_VAL1);
	val = MOBJ_READ_4(mac, BWI_COMM_MOBJ, 0);
	if (val != TEST_VAL1) {
		printf("%s: TEST1 failed\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}

	/* Test 2 */
	MOBJ_WRITE_4(mac, BWI_COMM_MOBJ, 0, TEST_VAL2);
	val = MOBJ_READ_4(mac, BWI_COMM_MOBJ, 0);
	if (val != TEST_VAL2) {
		printf("%s: TEST2 failed\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}

	/* Restore to the original value */
	MOBJ_WRITE_4(mac, BWI_COMM_MOBJ, 0, orig_val);

	val = CSR_READ_4(sc, BWI_MAC_STATUS);
	if ((val & ~BWI_MAC_STATUS_PHYLNK) != BWI_MAC_STATUS_IHREN) {
		printf("%s: %s failed, MAC status 0x%08x\n",
		    sc->sc_dev.dv_xname, __func__, val);
		return (ENXIO);
	}

	val = CSR_READ_4(sc, BWI_MAC_INTR_STATUS);
	if (val != 0) {
		printf("%s: %s failed, intr status %08x\n",
		    sc->sc_dev.dv_xname, __func__, val);
		return (ENXIO);
	}
#undef TEST_VAL2
#undef TEST_VAL1

	return (0);
}

void
bwi_mac_setup_tpctl(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_tpctl *tpctl = &mac->mac_tpctl;

	/* Calc BBP attenuation */
	if (rf->rf_type == BWI_RF_T_BCM2050 && rf->rf_rev < 6)
		tpctl->bbp_atten = 0;
	else
		tpctl->bbp_atten = 2;

	/* Calc TX power CTRL1?? */
	tpctl->tp_ctrl1 = 0;
	if (rf->rf_type == BWI_RF_T_BCM2050) {
		if (rf->rf_rev == 1)
			tpctl->tp_ctrl1 = 3;
		else if (rf->rf_rev < 6)
			tpctl->tp_ctrl1 = 2;
		else if (rf->rf_rev == 8)
			tpctl->tp_ctrl1 = 1;
	}

	/* Empty TX power CTRL2?? */
	tpctl->tp_ctrl2 = 0xffff;

	/*
	 * Calc RF attenuation
	 */
	if (phy->phy_mode == IEEE80211_MODE_11A) {
		tpctl->rf_atten = 0x60;
		goto back;
	}

	if (BWI_IS_BRCM_BCM4309G(sc) && sc->sc_pci_revid < 0x51) {
		tpctl->rf_atten = sc->sc_pci_revid < 0x43 ? 2 : 3;
		goto back;
	}

	tpctl->rf_atten = 5;

	if (rf->rf_type != BWI_RF_T_BCM2050) {
		if (rf->rf_type == BWI_RF_T_BCM2053 && rf->rf_rev == 1)
			tpctl->rf_atten = 6;
		goto back;
	}

	/*
	 * NB: If we reaches here and the card is BRCM_BCM4309G,
	 *     then the card's PCI revision must >= 0x51
	 */

	/* BCM2050 RF */
	switch (rf->rf_rev) {
	case 1:
		if (phy->phy_mode == IEEE80211_MODE_11G) {
			if (BWI_IS_BRCM_BCM4309G(sc) || BWI_IS_BRCM_BU4306(sc))
				tpctl->rf_atten = 3;
			else
				tpctl->rf_atten = 1;
		} else {
			if (BWI_IS_BRCM_BCM4309G(sc))
				tpctl->rf_atten = 7;
			else
				tpctl->rf_atten = 6;
		}
		break;
	case 2:
		if (phy->phy_mode == IEEE80211_MODE_11G) {
			/*
			 * NOTE: Order of following conditions is critical
			 */
			if (BWI_IS_BRCM_BCM4309G(sc))
				tpctl->rf_atten = 3;
			else if (BWI_IS_BRCM_BU4306(sc))
				tpctl->rf_atten = 5;
			else if (sc->sc_bbp_id == BWI_BBPID_BCM4320)
				tpctl->rf_atten = 4;
			else
				tpctl->rf_atten = 3;
		} else {
			tpctl->rf_atten = 6;
		}
		break;
	case 4:
	case 5:
		tpctl->rf_atten = 1;
		break;
	case 8:
		tpctl->rf_atten = 0x1a;
		break;
	}
back:
	DPRINTF(1, "%s: bbp atten: %u, rf atten: %u, ctrl1: %u, ctrl2: %u\n",
	    sc->sc_dev.dv_xname, tpctl->bbp_atten, tpctl->rf_atten,
	    tpctl->tp_ctrl1, tpctl->tp_ctrl2);
}

void
bwi_mac_dummy_xmit(struct bwi_mac *mac)
{
#define PACKET_LEN	5
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	const uint32_t *packet;
	uint16_t val_50c;
	int wait_max, i;

	static const uint32_t	packet_11a[PACKET_LEN] =
	    { 0x000201cc, 0x00d40000, 0x00000000, 0x01000000, 0x00000000 };
	static const uint32_t	packet_11bg[PACKET_LEN] =
	    { 0x000b846e, 0x00d40000, 0x00000000, 0x01000000, 0x00000000 };

	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11A) {
		wait_max = 30;
		packet = packet_11a;
		val_50c = 1;
	} else {
		wait_max = 250;
		packet = packet_11bg;
		val_50c = 0;
	}

	for (i = 0; i < PACKET_LEN; ++i)
		TMPLT_WRITE_4(mac, i * 4, packet[i]);

	CSR_READ_4(sc, BWI_MAC_STATUS);	/* dummy read */

	CSR_WRITE_2(sc, 0x568, 0);
	CSR_WRITE_2(sc, 0x7c0, 0);
	CSR_WRITE_2(sc, 0x50c, val_50c);
	CSR_WRITE_2(sc, 0x508, 0);
	CSR_WRITE_2(sc, 0x50a, 0);
	CSR_WRITE_2(sc, 0x54c, 0);
	CSR_WRITE_2(sc, 0x56a, 0x14);
	CSR_WRITE_2(sc, 0x568, 0x826);
	CSR_WRITE_2(sc, 0x500, 0);
	CSR_WRITE_2(sc, 0x502, 0x30);

	if (rf->rf_type == BWI_RF_T_BCM2050 && rf->rf_rev <= 5)
		RF_WRITE(mac, 0x51, 0x17);

	for (i = 0; i < wait_max; ++i) {
		if (CSR_READ_2(sc, 0x50e) & 0x80)
			break;
		DELAY(10);
	}
	for (i = 0; i < 10; ++i) {
		if (CSR_READ_2(sc, 0x50e) & 0x400)
			break;
		DELAY(10);
	}
	for (i = 0; i < 10; ++i) {
		if ((CSR_READ_2(sc, 0x690) & 0x100) == 0)
			break;
		DELAY(10);
	}

	if (rf->rf_type == BWI_RF_T_BCM2050 && rf->rf_rev <= 5)
		RF_WRITE(mac, 0x51, 0x37);
#undef PACKET_LEN
}

void
bwi_mac_init_tpctl_11bg(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_tpctl tpctl_orig;
	int restore_tpctl = 0;

	KASSERT(phy->phy_mode != IEEE80211_MODE_11A);

	if (BWI_IS_BRCM_BU4306(sc))
		return;

	PHY_WRITE(mac, 0x28, 0x8018);
	CSR_CLRBITS_2(sc, BWI_BBP_ATTEN, 0x20);

	if (phy->phy_mode == IEEE80211_MODE_11G) {
		if ((phy->phy_flags & BWI_PHY_F_LINKED) == 0)
			return;
		PHY_WRITE(mac, 0x47a, 0xc111);
	}
	if (mac->mac_flags & BWI_MAC_F_TPCTL_INITED)
		return;

	if (phy->phy_mode == IEEE80211_MODE_11B && phy->phy_rev >= 2 &&
	    rf->rf_type == BWI_RF_T_BCM2050) {
		RF_SETBITS(mac, 0x76, 0x84);
	} else {
		struct bwi_tpctl tpctl;

		/* Backup original TX power control variables */
		bcopy(&mac->mac_tpctl, &tpctl_orig, sizeof(tpctl_orig));
		restore_tpctl = 1;

		bcopy(&mac->mac_tpctl, &tpctl, sizeof(tpctl));
		tpctl.bbp_atten = 11;
		tpctl.tp_ctrl1 = 0;
#ifdef notyet
		if (rf->rf_rev >= 6 && rf->rf_rev <= 8)
			tpctl.rf_atten = 31;
		else
#endif
			tpctl.rf_atten = 9;

		bwi_mac_set_tpctl_11bg(mac, &tpctl);
	}

	bwi_mac_dummy_xmit(mac);

	mac->mac_flags |= BWI_MAC_F_TPCTL_INITED;
	rf->rf_base_tssi = PHY_READ(mac, 0x29);
	DPRINTF(1, "%s: base tssi %d\n", sc->sc_dev.dv_xname, rf->rf_base_tssi);

	if (abs(rf->rf_base_tssi - rf->rf_idle_tssi) >= 20) {
		printf("%s: base tssi measure failed\n", sc->sc_dev.dv_xname);
		mac->mac_flags |= BWI_MAC_F_TPCTL_ERROR;
	}

	if (restore_tpctl)
		bwi_mac_set_tpctl_11bg(mac, &tpctl_orig);
	else
		RF_CLRBITS(mac, 0x76, 0x84);

	bwi_rf_clear_tssi(mac);
}

void
bwi_mac_detach(struct bwi_mac *mac)
{
	bwi_mac_fw_free(mac);
}

int
bwi_get_firmware(const char *name, const uint8_t *ucode, size_t size_ucode,
    size_t *size, size_t *offset)
{
	int i, nfiles, off = 0, ret = 1;
	struct fwheader *h;

	if ((h = malloc(sizeof(struct fwheader), M_DEVBUF, M_NOWAIT)) == NULL)
		return (ret);

	/* get number of firmware files */
	bcopy(ucode, &nfiles, sizeof(nfiles));
	nfiles = ntohl(nfiles);
	off += sizeof(nfiles);

	/* parse header and search the firmware */
	for (i = 0; i < nfiles && off < size_ucode; i++) {
		bzero(h, sizeof(struct fwheader));
		bcopy(ucode + off, h, sizeof(struct fwheader));
		off += sizeof(struct fwheader);

		if (strcmp(name, h->filename) == 0) {
			ret = 0;
			*size = ntohl(h->filesize);
			*offset = ntohl(h->fileoffset);
			break;
		}
	}

	free(h, M_DEVBUF, sizeof *h);

	return (ret);
}

int
bwi_fwimage_is_valid(struct bwi_softc *sc, uint8_t *fw, size_t fw_len,
    char *fw_name, uint8_t fw_type)
{
	const struct bwi_fwhdr *hdr;

	if (fw_len < sizeof(*hdr)) {
		printf("%s: invalid firmware (%s): invalid size %zu\n",
		    sc->sc_dev.dv_xname, fw_name, fw_len);
		return (1);
	}

	hdr = (const struct bwi_fwhdr *)fw;

	if (fw_type != BWI_FW_T_IV) {
		/*
		 * Don't verify IV's size, it has different meaning
		 */
		if (betoh32(hdr->fw_size) != fw_len - sizeof(*hdr)) {
			printf("%s: invalid firmware (%s): size mismatch, "
			    "fw %u, real %zu\n",
			    sc->sc_dev.dv_xname,
			    fw_name,
			    betoh32(hdr->fw_size),
			    fw_len - sizeof(*hdr));
			return (1);
		}
	}

	if (hdr->fw_type != fw_type) {
		printf("%s: invalid firmware (%s): type mismatch, "
		    "fw \'%c\', target \'%c\'\n",
		    sc->sc_dev.dv_xname, fw_name, hdr->fw_type, fw_type);
		return (1);
	}

	if (hdr->fw_gen != BWI_FW_GEN_1) {
		printf("%s: invalid firmware (%s): wrong generation, "
		    "fw %d, target %d\n",
		    sc->sc_dev.dv_xname, fw_name, hdr->fw_gen, BWI_FW_GEN_1);
		return (1);
	}

	return (0);
}

int
bwi_mac_fw_alloc(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	char *name = "bwi-airforce";
	size_t offset;
	char fwname[64];
	int idx, error;

	if (mac->mac_fw == NULL) {
		error = loadfirmware(name, &mac->mac_fw, &mac->mac_fw_size);
		if (error != 0) {
			printf("%s: error %d, could not read firmware %s\n",
			    sc->sc_dev.dv_xname, error, name);
			mac->mac_fw = NULL;
			return (EIO);
		}
	}

	if (mac->mac_ucode == NULL) {
		snprintf(fwname, sizeof(fwname), "ucode%d.fw",
		    mac->mac_rev >= 5 ? 5 : mac->mac_rev);

		error = bwi_get_firmware(fwname, mac->mac_fw, mac->mac_fw_size,
		    &mac->mac_ucode_size, &offset);
		if (error != 0) {
			printf("%s: error %d, could not read firmware %s!\n",
			    sc->sc_dev.dv_xname, error, fwname);
			return (ENOMEM);
		}
		mac->mac_ucode = (mac->mac_fw + offset);
		DPRINTF(1, "%s: loaded firmware file %s\n",
		    sc->sc_dev.dv_xname, fwname);

		if (bwi_fwimage_is_valid(sc, mac->mac_ucode,
		    mac->mac_ucode_size, fwname, BWI_FW_T_UCODE))
			return (EINVAL);
	}

	if (mac->mac_pcm == NULL) {
		snprintf(fwname, sizeof(fwname), "pcm%d.fw",
		    mac->mac_rev < 5 ? 4 : 5);

		error = bwi_get_firmware(fwname, mac->mac_fw, mac->mac_fw_size,
		    &mac->mac_pcm_size, &offset);
		if (error != 0) {
			printf("%s: error %d, could not read firmware %s!\n",
			    sc->sc_dev.dv_xname, error, fwname);
			return (ENOMEM);
		}
		mac->mac_pcm = (mac->mac_fw + offset);
		DPRINTF(1, "%s: loaded firmware file %s\n",
		    sc->sc_dev.dv_xname, fwname);

		if (bwi_fwimage_is_valid(sc, mac->mac_pcm,
		    mac->mac_pcm_size, fwname, BWI_FW_T_PCM))
			return (EINVAL);
	}

	if (mac->mac_iv == NULL) {
		/* TODO: 11A */
		if (mac->mac_rev == 2 || mac->mac_rev == 4) {
			idx = 2;
		} else if (mac->mac_rev >= 5 && mac->mac_rev <= 10) {
			idx = 5;
		} else {
			printf("%s: no suitable IV for MAC rev %d\n",
			    sc->sc_dev.dv_xname, mac->mac_rev);
			return (ENODEV);
		}

		snprintf(fwname, sizeof(fwname), "b0g0initvals%d.fw", idx);

		error = bwi_get_firmware(fwname, mac->mac_fw, mac->mac_fw_size,
		    &mac->mac_iv_size, &offset);
		if (error != 0) {
			printf("%s: error %d, could not read firmware %s!\n",
			    sc->sc_dev.dv_xname, error, fwname);
			return (ENOMEM);
		}
		mac->mac_iv = (mac->mac_fw + offset);
		DPRINTF(1, "%s: loaded firmware file %s\n",
		    sc->sc_dev.dv_xname, fwname);

		if (bwi_fwimage_is_valid(sc, mac->mac_iv,
		    mac->mac_iv_size, fwname, BWI_FW_T_IV))
			return (EINVAL);
	}

	if (mac->mac_iv_ext == NULL) {
		/* TODO: 11A */
		if (mac->mac_rev == 2 || mac->mac_rev == 4 ||
		    mac->mac_rev >= 11) {
			/* No extended IV */
			goto back;
		} else if (mac->mac_rev >= 5 && mac->mac_rev <= 10) {
			idx = 5;
		} else {
			printf("%s: no suitable ExtIV for MAC rev %d\n",
			    sc->sc_dev.dv_xname, mac->mac_rev);
			return (ENODEV);
		}

		snprintf(fwname, sizeof(fwname), "b0g0bsinitvals%d.fw", idx);

		error = bwi_get_firmware(fwname, mac->mac_fw, mac->mac_fw_size,
		    &mac->mac_iv_ext_size, &offset);
		if (error != 0) {
			printf("%s: error %d, could not read firmware %s!\n",
			    sc->sc_dev.dv_xname, error, fwname);
			return (ENOMEM);
		}
		mac->mac_iv_ext = (mac->mac_fw + offset);
		DPRINTF(1, "%s: loaded firmware file %s\n",
		    sc->sc_dev.dv_xname, fwname);

		if (bwi_fwimage_is_valid(sc, mac->mac_iv_ext,
		    mac->mac_iv_ext_size, fwname, BWI_FW_T_IV))
			return (EINVAL);
	}

back:
	return (0);
}

void
bwi_mac_fw_free(struct bwi_mac *mac)
{
	if (mac->mac_fw != NULL) {
		free(mac->mac_fw, M_DEVBUF, mac->mac_fw_size);
		mac->mac_fw = NULL;
	}
}

int
bwi_mac_fw_load(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	uint16_t fw_rev;
	const uint32_t *fw;
	int fw_len, i, error = 0;

	/*
	 * Load FW image
	 */
	fw = (const uint32_t *)(mac->mac_ucode + BWI_FWHDR_SZ);
	fw_len = (mac->mac_ucode_size - BWI_FWHDR_SZ) / sizeof(uint32_t);

	CSR_WRITE_4(sc, BWI_MOBJ_CTRL,
	    BWI_MOBJ_CTRL_VAL(BWI_FW_UCODE_MOBJ | BWI_WR_MOBJ_AUTOINC, 0));
	for (i = 0; i < fw_len; ++i) {
		CSR_WRITE_4(sc, BWI_MOBJ_DATA, betoh32(fw[i]));
		DELAY(10);
	}

	/*
	 * Load PCM image
	 */
	fw = (const uint32_t *)(mac->mac_pcm + BWI_FWHDR_SZ);
	fw_len = (mac->mac_pcm_size - BWI_FWHDR_SZ) / sizeof(uint32_t);

	CSR_WRITE_4(sc, BWI_MOBJ_CTRL,
	    BWI_MOBJ_CTRL_VAL(BWI_FW_PCM_MOBJ, 0x01ea));
	CSR_WRITE_4(sc, BWI_MOBJ_DATA, 0x4000);

	CSR_WRITE_4(sc, BWI_MOBJ_CTRL,
	    BWI_MOBJ_CTRL_VAL(BWI_FW_PCM_MOBJ, 0x01eb));
	for (i = 0; i < fw_len; ++i) {
		CSR_WRITE_4(sc, BWI_MOBJ_DATA, betoh32(fw[i]));
		DELAY(10);
	}

	CSR_WRITE_4(sc, BWI_MAC_INTR_STATUS, BWI_ALL_INTRS);
	CSR_WRITE_4(sc, BWI_MAC_STATUS,
	    BWI_MAC_STATUS_UCODE_START |
	    BWI_MAC_STATUS_IHREN |
	    BWI_MAC_STATUS_INFRA);

#define NRETRY	200
	for (i = 0; i < NRETRY; ++i) {
		uint32_t intr_status;

		intr_status = CSR_READ_4(sc, BWI_MAC_INTR_STATUS);
		if (intr_status == BWI_INTR_READY)
			break;
		DELAY(10);
	}
	if (i == NRETRY) {
		printf("%s: firmware (fw & pcm) loading timed out\n",
		    sc->sc_dev.dv_xname);
		error = ETIMEDOUT;
		goto out;
	}
#undef NRETRY

	CSR_READ_4(sc, BWI_MAC_INTR_STATUS);	/* dummy read */

	fw_rev = MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_FWREV);
	if (fw_rev > BWI_FW_VERSION3_REVMAX) {
		printf("%s: firmware version 4 is not supported yet\n",
		    sc->sc_dev.dv_xname);
		error = ENODEV;
		goto out;
	}

	DPRINTF(1, "%s: firmware rev 0x%04x, patch level 0x%04x\n",
	    sc->sc_dev.dv_xname, fw_rev,
	    MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_FWPATCHLV));

out:
	return (error);
}

int
bwi_mac_gpio_init(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_regwin *old, *gpio_rw;
	uint32_t filt, bits;
	int error;

	CSR_CLRBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_GPOSEL_MASK);
	/* TODO: LED */

	CSR_SETBITS_2(sc, BWI_MAC_GPIO_MASK, 0xf);

	filt = 0x1f;
	bits = 0xf;
	if (sc->sc_bbp_id == BWI_BBPID_BCM4301) {
		filt |= 0x60;
		bits |= 0x60;
	}
	if (sc->sc_card_flags & BWI_CARD_F_PA_GPIO9) {
		CSR_SETBITS_2(sc, BWI_MAC_GPIO_MASK, 0x200);
		filt |= 0x200;
		bits |= 0x200;
	}

	gpio_rw = BWI_GPIO_REGWIN(sc);
	error = bwi_regwin_switch(sc, gpio_rw, &old);
	if (error)
		return (error);

	CSR_FILT_SETBITS_4(sc, BWI_GPIO_CTRL, filt, bits);

	return (bwi_regwin_switch(sc, old, NULL));
}

int
bwi_mac_gpio_fini(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_regwin *old, *gpio_rw;
	int error;

	gpio_rw = BWI_GPIO_REGWIN(sc);
	error = bwi_regwin_switch(sc, gpio_rw, &old);
	if (error)
		return (error);

	CSR_WRITE_4(sc, BWI_GPIO_CTRL, 0);

	return (bwi_regwin_switch(sc, old, NULL));
}

int
bwi_mac_fw_load_iv(struct bwi_mac *mac, uint8_t *fw, size_t fw_len)
{
	struct bwi_softc *sc = mac->mac_sc;
	const struct bwi_fwhdr *hdr;
	const struct bwi_fw_iv *iv;
	int n, i, iv_img_size;

	/* Get the number of IVs in the IV image */
	hdr = (const struct bwi_fwhdr *)fw;
	n = betoh32(hdr->fw_iv_cnt);
	DPRINTF(1, "%s: IV count %d\n", sc->sc_dev.dv_xname, n);

	/* Calculate the IV image size, for later sanity check */
	iv_img_size = fw_len - sizeof(*hdr);

	/* Locate the first IV */
	iv = (const struct bwi_fw_iv *)(fw + sizeof(*hdr));

	for (i = 0; i < n; ++i) {
		uint16_t iv_ofs, ofs;
		int sz = 0;

		if (iv_img_size < sizeof(iv->iv_ofs)) {
			printf("%s: invalid IV image, ofs\n",
			    sc->sc_dev.dv_xname);
			return (EINVAL);
		}
		iv_img_size -= sizeof(iv->iv_ofs);
		sz += sizeof(iv->iv_ofs);

		iv_ofs = betoh16(iv->iv_ofs);

		ofs = __SHIFTOUT(iv_ofs, BWI_FW_IV_OFS_MASK);
		if (ofs >= 0x1000) {
			printf("%s: invalid ofs (0x%04x) for %dth iv\n",
			    sc->sc_dev.dv_xname, ofs, i);
			return (EINVAL);
		}

		if (iv_ofs & BWI_FW_IV_IS_32BIT) {
			uint32_t val32;

			if (iv_img_size < sizeof(iv->iv_val.val32)) {
				printf("%s: invalid IV image, val32\n",
				    sc->sc_dev.dv_xname);
				return (EINVAL);
			}
			iv_img_size -= sizeof(iv->iv_val.val32);
			sz += sizeof(iv->iv_val.val32);

			val32 = betoh32(iv->iv_val.val32);
			CSR_WRITE_4(sc, ofs, val32);
		} else {
			uint16_t val16;

			if (iv_img_size < sizeof(iv->iv_val.val16)) {
				printf("%s: invalid IV image, val16\n",
				    sc->sc_dev.dv_xname);
				return (EINVAL);
			}
			iv_img_size -= sizeof(iv->iv_val.val16);
			sz += sizeof(iv->iv_val.val16);

			val16 = betoh16(iv->iv_val.val16);
			CSR_WRITE_2(sc, ofs, val16);
		}

		iv = (const struct bwi_fw_iv *)((const uint8_t *)iv + sz);
	}

	if (iv_img_size != 0) {
		printf("%s: invalid IV image, size left %d\n",
		    sc->sc_dev.dv_xname, iv_img_size);
		return (EINVAL);
	}

	return (0);
}

int
bwi_mac_fw_init(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	int error;

	error = bwi_mac_fw_load_iv(mac, mac->mac_iv, mac->mac_iv_size);
	if (error) {
		printf("%s: load IV failed\n", sc->sc_dev.dv_xname);
		return (error);
	}

	if (mac->mac_iv_ext != NULL) {
		error = bwi_mac_fw_load_iv(mac, mac->mac_iv_ext,
		    mac->mac_iv_ext_size);
		if (error)
			printf("%s: load ExtIV failed\n", sc->sc_dev.dv_xname);
	}

	return (error);
}

void
bwi_mac_opmode_init(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t mac_status;
	uint16_t pre_tbtt;

	CSR_CLRBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_INFRA);
	CSR_SETBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_INFRA);
	CSR_SETBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_PASS_BCN);

	/* Set probe resp timeout to infinite */
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_PROBE_RESP_TO, 0);

	/*
	 * TODO: factor out following part
	 */

	mac_status = CSR_READ_4(sc, BWI_MAC_STATUS);
	mac_status &= ~(BWI_MAC_STATUS_OPMODE_HOSTAP |
	    BWI_MAC_STATUS_PASS_CTL |
	    BWI_MAC_STATUS_PASS_BADPLCP |
	    BWI_MAC_STATUS_PASS_BADFCS |
	    BWI_MAC_STATUS_PROMISC);
	mac_status |= BWI_MAC_STATUS_INFRA;

	/* Always turn on PROMISC on old hardware */
	if (mac->mac_rev < 5)
		mac_status |= BWI_MAC_STATUS_PROMISC;

	switch (ic->ic_opmode) {
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
		mac_status &= ~BWI_MAC_STATUS_INFRA;
		break;
	case IEEE80211_M_HOSTAP:
		mac_status |= BWI_MAC_STATUS_OPMODE_HOSTAP;
		break;
#endif
	case IEEE80211_M_MONITOR:
#if 0
		/* Do you want data from your microwave oven? */
		mac_status |= BWI_MAC_STATUS_PASS_CTL |
			      BWI_MAC_STATUS_PASS_BADPLCP |
			      BWI_MAC_STATUS_PASS_BADFCS;
#else
		mac_status |= BWI_MAC_STATUS_PASS_CTL;
#endif
		/* Promisc? */
		break;
	default:
		break;
	}

	if (ic->ic_if.if_flags & IFF_PROMISC)
		mac_status |= BWI_MAC_STATUS_PROMISC;

	CSR_WRITE_4(sc, BWI_MAC_STATUS, mac_status);

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode != IEEE80211_M_IBSS &&
	    ic->ic_opmode != IEEE80211_M_HOSTAP) {
#endif
		if (sc->sc_bbp_id == BWI_BBPID_BCM4306 && sc->sc_bbp_rev == 3)
			pre_tbtt = 100;
		else
			pre_tbtt = 50;
#ifndef IEEE80211_STA_ONLY
	} else
		pre_tbtt = 2;
#endif
	CSR_WRITE_2(sc, BWI_MAC_PRE_TBTT, pre_tbtt);
}

void
bwi_mac_hostflags_init(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	uint64_t host_flags;

	if (phy->phy_mode == IEEE80211_MODE_11A)
		return;

	host_flags = HFLAGS_READ(mac);
	host_flags |= BWI_HFLAG_SYM_WA;

	if (phy->phy_mode == IEEE80211_MODE_11G) {
		if (phy->phy_rev == 1)
			host_flags |= BWI_HFLAG_GDC_WA;
		if (sc->sc_card_flags & BWI_CARD_F_PA_GPIO9)
			host_flags |= BWI_HFLAG_OFDM_PA;
	} else if (phy->phy_mode == IEEE80211_MODE_11B) {
		if (phy->phy_rev >= 2 && rf->rf_type == BWI_RF_T_BCM2050)
			host_flags &= ~BWI_HFLAG_GDC_WA;
	} else {
		panic("unknown PHY mode %u", phy->phy_mode);
	}

	HFLAGS_WRITE(mac, host_flags);
}

void
bwi_mac_bss_param_init(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_retry_lim lim;
	uint16_t cw_min;

	/*
	 * Set short/long retry limits
	 */
	bzero(&lim, sizeof(lim));
	lim.shretry = BWI_SHRETRY;
	lim.shretry_fb = BWI_SHRETRY_FB;
	lim.lgretry = BWI_LGRETRY;
	lim.lgretry_fb = BWI_LGRETRY_FB;
	bwi_mac_set_retry_lim(mac, &lim);

	/*
	 * Implicitly prevent firmware from sending probe response
	 * by setting its "probe response timeout" to 1us.
	 */
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_PROBE_RESP_TO, 1);

	/*
	 * XXX MAC level acknowledge and CW min/max should depend
	 * on the char rateset of the IBSS/BSS to join.
	 */

	/*
	 * Set MAC level acknowledge rates
	 */
	bwi_mac_set_ackrates(mac, &sc->sc_ic.ic_sup_rates[phy->phy_mode]);

	/*
	 * Set CW min
	 */
	if (phy->phy_mode == IEEE80211_MODE_11B)
		cw_min = IEEE80211_CW_MIN_0;
	else
		cw_min = IEEE80211_CW_MIN_1;
	MOBJ_WRITE_2(mac, BWI_80211_MOBJ, BWI_80211_MOBJ_CWMIN, cw_min);

	/*
	 * Set CW max
	 */
	MOBJ_WRITE_2(mac, BWI_80211_MOBJ, BWI_80211_MOBJ_CWMAX,
	    IEEE80211_CW_MAX);
}

void
bwi_mac_set_retry_lim(struct bwi_mac *mac, const struct bwi_retry_lim *lim)
{
	/* Short/Long retry limit */
	MOBJ_WRITE_2(mac, BWI_80211_MOBJ, BWI_80211_MOBJ_SHRETRY,
	    lim->shretry);
	MOBJ_WRITE_2(mac, BWI_80211_MOBJ, BWI_80211_MOBJ_LGRETRY,
	    lim->lgretry);

	/* Short/Long retry fallback limit */
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_SHRETRY_FB,
	    lim->shretry_fb);
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_LGRETEY_FB,
	    lim->lgretry_fb);
}

void
bwi_mac_set_ackrates(struct bwi_mac *mac, const struct ieee80211_rateset *rs)
{
	struct bwi_softc *sc;
	int i;

	sc = mac->mac_sc;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	/* XXX not standard conforming */
	for (i = 0; i < rs->rs_nrates; ++i) {
		enum bwi_modtype modtype;
		uint16_t ofs;

		modtype = bwi_rate2modtype(rs->rs_rates[i]);
		switch (modtype) {
		case IEEE80211_MODTYPE_DS:
			ofs = 0x4c0;
			ofs += (ieee80211_rate2plcp(rs->rs_rates[i],
			    IEEE80211_MODE_11B) & 0xf) * 2;
			break;
		case IEEE80211_MODTYPE_OFDM:
			ofs = 0x480;
			ofs += (ieee80211_rate2plcp(rs->rs_rates[i],
			    IEEE80211_MODE_11G) & 0xf) * 2;
			break;
		default:
			panic("unsupported modtype %u", modtype);
		}

		MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, ofs + 0x20,
		    MOBJ_READ_2(mac, BWI_COMM_MOBJ, ofs));
	}
}

int
bwi_mac_start(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;

	CSR_SETBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_ENABLE);
	CSR_WRITE_4(sc, BWI_MAC_INTR_STATUS, BWI_INTR_READY);

	/* Flush pending bus writes */
	CSR_READ_4(sc, BWI_MAC_STATUS);
	CSR_READ_4(sc, BWI_MAC_INTR_STATUS);

	return (bwi_mac_config_ps(mac));
}

int
bwi_mac_stop(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	int error, i;

	error = bwi_mac_config_ps(mac);
	if (error)
		return (error);

	CSR_CLRBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_ENABLE);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_MAC_STATUS);

#define NRETRY	10000
	for (i = 0; i < NRETRY; ++i) {
		if (CSR_READ_4(sc, BWI_MAC_INTR_STATUS) & BWI_INTR_READY)
			break;
		DELAY(1);
	}
	if (i == NRETRY) {
		printf("%s: can't stop MAC\n", sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}
#undef NRETRY

	return (0);
}

int
bwi_mac_config_ps(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	uint32_t status;

	status = CSR_READ_4(sc, BWI_MAC_STATUS);

	status &= ~BWI_MAC_STATUS_HW_PS;
	status |= BWI_MAC_STATUS_WAKEUP;
	CSR_WRITE_4(sc, BWI_MAC_STATUS, status);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_MAC_STATUS);

	if (mac->mac_rev >= 5) {
		int i;

#define NRETRY	100
		for (i = 0; i < NRETRY; ++i) {
			if (MOBJ_READ_2(mac, BWI_COMM_MOBJ,
			    BWI_COMM_MOBJ_UCODE_STATE) != BWI_UCODE_STATE_PS)
				break;
			DELAY(10);
		}
		if (i == NRETRY) {
			printf("%s: config PS failed\n", sc->sc_dev.dv_xname);
			return (ETIMEDOUT);
		}
#undef NRETRY
	}
	return (0);
}

void
bwi_mac_reset_hwkeys(struct bwi_mac *mac)
{
	/* TODO: firmware crypto */
	MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_KEYTABLE_OFS);
}

void
bwi_mac_shutdown(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	int i;

	if (mac->mac_flags & BWI_MAC_F_HAS_TXSTATS)
		sc->sc_free_txstats(sc);

	sc->sc_free_rx_ring(sc);

	for (i = 0; i < BWI_TX_NRING; ++i)
		sc->sc_free_tx_ring(sc, i);

	bwi_rf_off(mac);

	/* TODO: LED */

	bwi_mac_gpio_fini(mac);

	bwi_rf_off(mac); /* XXX again */
	CSR_WRITE_2(sc, BWI_BBP_ATTEN, BWI_BBP_ATTEN_MAGIC);
	bwi_regwin_disable(sc, &mac->mac_regwin, 0);

	mac->mac_flags &= ~BWI_MAC_F_INITED;
}

int
bwi_mac_get_property(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	enum bwi_bus_space old_bus_space;
	uint32_t val;

	/*
	 * Byte swap
	 */
	val = CSR_READ_4(sc, BWI_MAC_STATUS);
	if (val & BWI_MAC_STATUS_BSWAP) {
		DPRINTF(1, "%s: need byte swap\n", sc->sc_dev.dv_xname);
		mac->mac_flags |= BWI_MAC_F_BSWAP;
	}

	/*
	 * DMA address space
	 */
	old_bus_space = sc->sc_bus_space;

	val = CSR_READ_4(sc, BWI_STATE_HI);
	if (__SHIFTOUT(val, BWI_STATE_HI_FLAGS_MASK) &
	    BWI_STATE_HI_FLAG_64BIT) {
		/* 64bit address */
		sc->sc_bus_space = BWI_BUS_SPACE_64BIT;
		printf(": 64bit bus space not supported\n");
		return (ENODEV);
	} else {
		uint32_t txrx_reg = BWI_TXRX_CTRL_BASE + BWI_TX32_CTRL;

		CSR_WRITE_4(sc, txrx_reg, BWI_TXRX32_CTRL_ADDRHI_MASK);
		if (CSR_READ_4(sc, txrx_reg) & BWI_TXRX32_CTRL_ADDRHI_MASK) {
			/* 32bit address */
			sc->sc_bus_space = BWI_BUS_SPACE_32BIT;
			DPRINTF(1, "%s: 32bit bus space\n",
			    sc->sc_dev.dv_xname);
		} else {
			/* 30bit address */
			sc->sc_bus_space = BWI_BUS_SPACE_30BIT;
			DPRINTF(1, "%s: 30bit bus space\n",
			    sc->sc_dev.dv_xname);
		}
	}

	if (old_bus_space != 0 && old_bus_space != sc->sc_bus_space) {
		printf("%s: MACs bus space mismatch!\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}

	return (0);
}

void
bwi_mac_updateslot(struct bwi_mac *mac, int shslot)
{
	struct bwi_softc *sc;
	uint16_t slot_time;

	sc = mac->mac_sc;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11B)
		return;

	if (shslot)
		slot_time = IEEE80211_DUR_DS_SHSLOT;
	else
		slot_time = IEEE80211_DUR_DS_SLOT;

	CSR_WRITE_2(mac->mac_sc, BWI_MAC_SLOTTIME,
	    slot_time + BWI_MAC_SLOTTIME_ADJUST);
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_SLOTTIME, slot_time);
}

int
bwi_mac_attach(struct bwi_softc *sc, int id, uint8_t rev)
{
	struct bwi_mac *mac;
	int i;

	KASSERT(sc->sc_nmac <= BWI_MAC_MAX && sc->sc_nmac >= 0);

	if (sc->sc_nmac == BWI_MAC_MAX) {
		printf("%s: too many MACs\n", sc->sc_dev.dv_xname);
		return (0);
	}

	/*
	 * More than one MAC is only supported by BCM4309
	 */
	if (sc->sc_nmac != 0 &&
	    sc->sc_pci_did != PCI_PRODUCT_BROADCOM_BCM4309) {
		DPRINTF(1, "%s: ignore second MAC\n", sc->sc_dev.dv_xname);
		return (0);
	}

	mac = &sc->sc_mac[sc->sc_nmac];

	/* XXX will this happen? */
	if (BWI_REGWIN_EXIST(&mac->mac_regwin)) {
		printf("%s: %dth MAC already attached\n",
		    sc->sc_dev.dv_xname, sc->sc_nmac);
		return (0);
	}

	/*
	 * Test whether the revision of this MAC is supported
	 */
	for (i = 0; i < nitems(bwi_sup_macrev); ++i) {
		if (bwi_sup_macrev[i] == rev)
			break;
	}
	if (i == nitems(bwi_sup_macrev)) {
		printf("%s: MAC rev %u is not supported\n",
		    sc->sc_dev.dv_xname, rev);
		return (ENXIO);
	}

	BWI_CREATE_MAC(mac, sc, id, rev);
	sc->sc_nmac++;

	if (mac->mac_rev < 5) {
		mac->mac_flags |= BWI_MAC_F_HAS_TXSTATS;
		DPRINTF(1, "%s: has TX stats\n", sc->sc_dev.dv_xname);
	} else {
		mac->mac_flags |= BWI_MAC_F_PHYE_RESET;
	}

	return (0);
}

void
bwi_mac_balance_atten(int *bbp_atten0, int *rf_atten0)
{
	int bbp_atten, rf_atten, rf_atten_lim = -1;

	bbp_atten = *bbp_atten0;
	rf_atten = *rf_atten0;

	/*
	 * RF attenuation affects TX power BWI_RF_ATTEN_FACTOR times
	 * as much as BBP attenuation, so we try our best to keep RF
	 * attenuation within range.  BBP attenuation will be clamped
	 * later if it is out of range during balancing.
	 *
	 * BWI_RF_ATTEN_MAX0 is used as RF attenuation upper limit.
	 */

	/*
	 * Use BBP attenuation to balance RF attenuation
	 */
	if (rf_atten < 0)
		rf_atten_lim = 0;
	else if (rf_atten > BWI_RF_ATTEN_MAX0)
		rf_atten_lim = BWI_RF_ATTEN_MAX0;

	if (rf_atten_lim >= 0) {
		bbp_atten += (BWI_RF_ATTEN_FACTOR * (rf_atten - rf_atten_lim));
		rf_atten = rf_atten_lim;
	}

	/*
	 * If possible, use RF attenuation to balance BBP attenuation
	 * NOTE: RF attenuation is still kept within range.
	 */
	while (rf_atten < BWI_RF_ATTEN_MAX0 && bbp_atten > BWI_BBP_ATTEN_MAX) {
		bbp_atten -= BWI_RF_ATTEN_FACTOR;
		++rf_atten;
	}
	while (rf_atten > 0 && bbp_atten < 0) {
		bbp_atten += BWI_RF_ATTEN_FACTOR;
		--rf_atten;
	}

	/* RF attenuation MUST be within range */
	KASSERT(rf_atten >= 0 && rf_atten <= BWI_RF_ATTEN_MAX0);

	/*
	 * Clamp BBP attenuation
	 */
	if (bbp_atten < 0)
		bbp_atten = 0;
	else if (bbp_atten > BWI_BBP_ATTEN_MAX)
		bbp_atten = BWI_BBP_ATTEN_MAX;

	*rf_atten0 = rf_atten;
	*bbp_atten0 = bbp_atten;
}

void
bwi_mac_adjust_tpctl(struct bwi_mac *mac, int rf_atten_adj, int bbp_atten_adj)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_tpctl tpctl;
	int bbp_atten, rf_atten, tp_ctrl1;

	bcopy(&mac->mac_tpctl, &tpctl, sizeof(tpctl));

	/* NOTE: Use signed value to do calculation */
	bbp_atten = tpctl.bbp_atten;
	rf_atten = tpctl.rf_atten;
	tp_ctrl1 = tpctl.tp_ctrl1;

	bbp_atten += bbp_atten_adj;
	rf_atten += rf_atten_adj;

	bwi_mac_balance_atten(&bbp_atten, &rf_atten);

	if (rf->rf_type == BWI_RF_T_BCM2050 && rf->rf_rev == 2) {
		if (rf_atten <= 1) {
			if (tp_ctrl1 == 0) {
				tp_ctrl1 = 3;
				bbp_atten += 2;
				rf_atten += 2;
			} else if (sc->sc_card_flags & BWI_CARD_F_PA_GPIO9) {
				bbp_atten +=
				(BWI_RF_ATTEN_FACTOR * (rf_atten - 2));
				rf_atten = 2;
			}
		} else if (rf_atten > 4 && tp_ctrl1 != 0) {
			tp_ctrl1 = 0;
			if (bbp_atten < 3) {
				bbp_atten += 2;
				rf_atten -= 3;
			} else {
				bbp_atten -= 2;
				rf_atten -= 2;
			}
		}
		bwi_mac_balance_atten(&bbp_atten, &rf_atten);
	}

	tpctl.bbp_atten = bbp_atten;
	tpctl.rf_atten = rf_atten;
	tpctl.tp_ctrl1 = tp_ctrl1;

	bwi_mac_lock(mac);
	bwi_mac_set_tpctl_11bg(mac, &tpctl);
	bwi_mac_unlock(mac);
}

/*
 * http://bcm-specs.sipsolutions.net/RecalculateTransmissionPower
 */
void
bwi_mac_calibrate_txpower(struct bwi_mac *mac, enum bwi_txpwrcb_type type)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	int8_t tssi[4], tssi_avg, cur_txpwr;
	int error, i, ofdm_tssi;
	int txpwr_diff, rf_atten_adj, bbp_atten_adj;

	if (mac->mac_flags & BWI_MAC_F_TPCTL_ERROR) {
		DPRINTF(1, "%s: tpctl error happened, can't set txpower\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	if (BWI_IS_BRCM_BU4306(sc)) {
		DPRINTF(1, "%s: BU4306, can't set txpower\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * Save latest TSSI and reset the related memory objects
	 */
	ofdm_tssi = 0;
	error = bwi_rf_get_latest_tssi(mac, tssi, BWI_COMM_MOBJ_TSSI_DS);
	if (error) {
		DPRINTF(1, "%s: no DS tssi\n", sc->sc_dev.dv_xname);

		if (mac->mac_phy.phy_mode == IEEE80211_MODE_11B) {
			if (type == BWI_TXPWR_FORCE) {
				rf_atten_adj = 0;
				bbp_atten_adj = 1;
				goto calib;
			} else {
				return;
			}
		}

		error = bwi_rf_get_latest_tssi(mac, tssi,
		    BWI_COMM_MOBJ_TSSI_OFDM);
		if (error) {
			DPRINTF(1, "%s: no OFDM tssi\n", sc->sc_dev.dv_xname);
			if (type == BWI_TXPWR_FORCE) {
				rf_atten_adj = 0;
				bbp_atten_adj = 1;
				goto calib;
			} else {
				return;
			}
		}

		for (i = 0; i < 4; ++i) {
			tssi[i] += 0x20;
			tssi[i] &= 0x3f;
		}
		ofdm_tssi = 1;
	}
	bwi_rf_clear_tssi(mac);

	DPRINTF(1, "%s: tssi0 %d, tssi1 %d, tssi2 %d, tssi3 %d\n",
	    sc->sc_dev.dv_xname, tssi[0], tssi[1], tssi[2], tssi[3]);

	/*
	 * Calculate RF/BBP attenuation adjustment based on
	 * the difference between desired TX power and sampled
	 * TX power.
	 */
	/* +8 == "each incremented by 1/2" */
	tssi_avg = (tssi[0] + tssi[1] + tssi[2] + tssi[3] + 8) / 4;
	if (ofdm_tssi && (HFLAGS_READ(mac) & BWI_HFLAG_PWR_BOOST_DS))
		tssi_avg -= 13;

	DPRINTF(1, "%s: tssi avg %d\n", sc->sc_dev.dv_xname, tssi_avg);

	error = bwi_rf_tssi2dbm(mac, tssi_avg, &cur_txpwr);
	if (error)
		return;
	DPRINTF(1, "%s: current txpower %d\n", sc->sc_dev.dv_xname, cur_txpwr);

	txpwr_diff = rf->rf_txpower_max - cur_txpwr; /* XXX ni_txpower */

	rf_atten_adj = -howmany(txpwr_diff, 8);

	if (type == BWI_TXPWR_INIT) {
		/*
		 * Move toward EEPROM max TX power as fast as we can
		 */
		bbp_atten_adj = -txpwr_diff;
	} else {
		bbp_atten_adj = -(txpwr_diff / 2);
	}
	bbp_atten_adj -= (BWI_RF_ATTEN_FACTOR * rf_atten_adj);

	if (rf_atten_adj == 0 && bbp_atten_adj == 0) {
		DPRINTF(1, "%s: no need to adjust RF/BBP attenuation\n",
		    sc->sc_dev.dv_xname);
		/* TODO: LO */
		return;
	}

calib:
	DPRINTF(1, "%s: rf atten adjust %d, bbp atten adjust %d\n",
	    sc->sc_dev.dv_xname, rf_atten_adj, bbp_atten_adj);
	bwi_mac_adjust_tpctl(mac, rf_atten_adj, bbp_atten_adj);
	/* TODO: LO */
}

void
bwi_mac_lock(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;

	KASSERT((mac->mac_flags & BWI_MAC_F_LOCKED) == 0);

	if (mac->mac_rev < 3)
		bwi_mac_stop(mac);
	else
#ifndef IEEE80211_STA_ONLY
	if (sc->sc_ic.ic_opmode != IEEE80211_M_HOSTAP)
#endif
		bwi_mac_config_ps(mac);

	CSR_SETBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_RFLOCK);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_MAC_STATUS);
	DELAY(10);

	mac->mac_flags |= BWI_MAC_F_LOCKED;
}

void
bwi_mac_unlock(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;

	KASSERT(mac->mac_flags & BWI_MAC_F_LOCKED);

	CSR_READ_2(sc, BWI_PHYINFO); /* dummy read */

	CSR_CLRBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_RFLOCK);

	if (mac->mac_rev < 3)
		bwi_mac_start(mac);
	else
#ifndef IEEE80211_STA_ONLY
	if (sc->sc_ic.ic_opmode != IEEE80211_M_HOSTAP)
#endif
		bwi_mac_config_ps(mac);

	mac->mac_flags &= ~BWI_MAC_F_LOCKED;
}

void
bwi_mac_set_promisc(struct bwi_mac *mac, int promisc)
{
	struct bwi_softc *sc = mac->mac_sc;

	if (mac->mac_rev < 5) /* Promisc is always on */
		return;

	if (promisc)
		CSR_SETBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_PROMISC);
	else
		CSR_CLRBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_PROMISC);
}

/* PHY */

void
bwi_phy_write(struct bwi_mac *mac, uint16_t ctrl, uint16_t data)
{
	struct bwi_softc *sc = mac->mac_sc;

	/* TODO: 11A */
	CSR_WRITE_2(sc, BWI_PHY_CTRL, ctrl);
	CSR_WRITE_2(sc, BWI_PHY_DATA, data);
}

uint16_t
bwi_phy_read(struct bwi_mac *mac, uint16_t ctrl)
{
	struct bwi_softc *sc = mac->mac_sc;

	/* TODO: 11A */
	CSR_WRITE_2(sc, BWI_PHY_CTRL, ctrl);
	return (CSR_READ_2(sc, BWI_PHY_DATA));
}

int
bwi_phy_attach(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	uint8_t phyrev, phytype, phyver;
	uint16_t val;
	int i;

	/* Get PHY type/revision/version */
	val = CSR_READ_2(sc, BWI_PHYINFO);
	phyrev = __SHIFTOUT(val, BWI_PHYINFO_REV_MASK);
	phytype = __SHIFTOUT(val, BWI_PHYINFO_TYPE_MASK);
	phyver = __SHIFTOUT(val, BWI_PHYINFO_VER_MASK);
	DPRINTF(1, "%s: PHY type %d, rev %d, ver %d\n",
	    sc->sc_dev.dv_xname, phytype, phyrev, phyver);

	/*
	 * Verify whether the revision of the PHY type is supported
	 * Convert PHY type to ieee80211_phymode
	 */
	switch (phytype) {
	case BWI_PHYINFO_TYPE_11A:
		if (phyrev >= 4) {
			printf("%s: unsupported 11A PHY, rev %u\n",
			    sc->sc_dev.dv_xname, phyrev);
			return (ENXIO);
		}
		phy->phy_init = bwi_phy_init_11a;
		phy->phy_mode = IEEE80211_MODE_11A;
		phy->phy_tbl_ctrl = BWI_PHYR_TBL_CTRL_11A;
		phy->phy_tbl_data_lo = BWI_PHYR_TBL_DATA_LO_11A;
		phy->phy_tbl_data_hi = BWI_PHYR_TBL_DATA_HI_11A;
		break;
	case BWI_PHYINFO_TYPE_11B:
		for (i = 0; i < nitems(bwi_sup_bphy); ++i) {
			if (phyrev == bwi_sup_bphy[i].rev) {
				phy->phy_init = bwi_sup_bphy[i].init;
				break;
			}
		}
		if (i == nitems(bwi_sup_bphy)) {
			printf("%s: unsupported 11B PHY, rev %u\n",
			    sc->sc_dev.dv_xname, phyrev);
			return (ENXIO);
		}
		phy->phy_mode = IEEE80211_MODE_11B;
		break;
	case BWI_PHYINFO_TYPE_11G:
		if (phyrev > 8) {
			printf("%s: unsupported 11G PHY, rev %u\n",
			    sc->sc_dev.dv_xname, phyrev);
			return (ENXIO);
		}
		phy->phy_init = bwi_phy_init_11g;
		phy->phy_mode = IEEE80211_MODE_11G;
		phy->phy_tbl_ctrl = BWI_PHYR_TBL_CTRL_11G;
		phy->phy_tbl_data_lo = BWI_PHYR_TBL_DATA_LO_11G;
		phy->phy_tbl_data_hi = BWI_PHYR_TBL_DATA_HI_11G;
		break;
	default:
		printf("%s: unsupported PHY type %d\n",
		    sc->sc_dev.dv_xname, phytype);
		return (ENXIO);
	}
	phy->phy_rev = phyrev;
	phy->phy_version = phyver;

	return (0);
}

void
bwi_phy_set_bbp_atten(struct bwi_mac *mac, uint16_t bbp_atten)
{
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t mask = 0x000f;

	if (phy->phy_version == 0) {
		CSR_FILT_SETBITS_2(mac->mac_sc, BWI_BBP_ATTEN, ~mask,
		    __SHIFTIN(bbp_atten, mask));
	} else {
		if (phy->phy_version > 1)
			mask <<= 2;
		else
			mask <<= 3;
		PHY_FILT_SETBITS(mac, BWI_PHYR_BBP_ATTEN, ~mask,
		    __SHIFTIN(bbp_atten, mask));
	}
}

int
bwi_phy_calibrate(struct bwi_mac *mac)
{
	struct bwi_phy *phy = &mac->mac_phy;

	/* Dummy read */
	CSR_READ_4(mac->mac_sc, BWI_MAC_STATUS);

	/* Don't re-init */
	if (phy->phy_flags & BWI_PHY_F_CALIBRATED)
		return (0);

	if (phy->phy_mode == IEEE80211_MODE_11G && phy->phy_rev == 1) {
		bwi_mac_reset(mac, 0);
		bwi_phy_init_11g(mac);
		bwi_mac_reset(mac, 1);
	}

	phy->phy_flags |= BWI_PHY_F_CALIBRATED;

	return (0);
}

void
bwi_tbl_write_2(struct bwi_mac *mac, uint16_t ofs, uint16_t data)
{
	struct bwi_phy *phy = &mac->mac_phy;

	KASSERT(phy->phy_tbl_ctrl != 0 && phy->phy_tbl_data_lo != 0);
	PHY_WRITE(mac, phy->phy_tbl_ctrl, ofs);
	PHY_WRITE(mac, phy->phy_tbl_data_lo, data);
}

void
bwi_tbl_write_4(struct bwi_mac *mac, uint16_t ofs, uint32_t data)
{
	struct bwi_phy *phy = &mac->mac_phy;

	KASSERT(phy->phy_tbl_data_lo != 0 && phy->phy_tbl_data_hi != 0 &&
	    phy->phy_tbl_ctrl != 0);

	PHY_WRITE(mac, phy->phy_tbl_ctrl, ofs);
	PHY_WRITE(mac, phy->phy_tbl_data_hi, data >> 16);
	PHY_WRITE(mac, phy->phy_tbl_data_lo, data & 0xffff);
}

void
bwi_nrssi_write(struct bwi_mac *mac, uint16_t ofs, int16_t data)
{
	PHY_WRITE(mac, BWI_PHYR_NRSSI_CTRL, ofs);
	PHY_WRITE(mac, BWI_PHYR_NRSSI_DATA, (uint16_t)data);
}

int16_t
bwi_nrssi_read(struct bwi_mac *mac, uint16_t ofs)
{
	PHY_WRITE(mac, BWI_PHYR_NRSSI_CTRL, ofs);
	return ((int16_t)PHY_READ(mac, BWI_PHYR_NRSSI_DATA));
}

void
bwi_phy_init_11a(struct bwi_mac *mac)
{
	/* TODO: 11A */
}

void
bwi_phy_init_11g(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	const struct bwi_tpctl *tpctl = &mac->mac_tpctl;

	if (phy->phy_rev == 1)
		bwi_phy_init_11b_rev5(mac);
	else
		bwi_phy_init_11b_rev6(mac);

	if (phy->phy_rev >= 2 || (phy->phy_flags & BWI_PHY_F_LINKED))
		bwi_phy_config_11g(mac);

	if (phy->phy_rev >= 2) {
		PHY_WRITE(mac, 0x814, 0);
		PHY_WRITE(mac, 0x815, 0);

		if (phy->phy_rev == 2) {
			PHY_WRITE(mac, 0x811, 0);
			PHY_WRITE(mac, 0x15, 0xc0);
		} else if (phy->phy_rev > 5) {
			PHY_WRITE(mac, 0x811, 0x400);
			PHY_WRITE(mac, 0x15, 0xc0);
		}
	}

	if (phy->phy_rev >= 2 || (phy->phy_flags & BWI_PHY_F_LINKED)) {
		uint16_t val;

		val = PHY_READ(mac, 0x400) & 0xff;
		if (val == 3 || val == 5) {
			PHY_WRITE(mac, 0x4c2, 0x1816);
			PHY_WRITE(mac, 0x4c3, 0x8006);
			if (val == 5) {
				PHY_FILT_SETBITS(mac, 0x4cc,
						 0xff, 0x1f00);
			}
		}
	}

	if ((phy->phy_rev <= 2 && (phy->phy_flags & BWI_PHY_F_LINKED)) ||
	    phy->phy_rev >= 2)
		PHY_WRITE(mac, 0x47e, 0x78);

	if (rf->rf_rev == 8) {
		PHY_SETBITS(mac, 0x801, 0x80);
		PHY_SETBITS(mac, 0x43e, 0x4);
	}

	if (phy->phy_rev >= 2 && (phy->phy_flags & BWI_PHY_F_LINKED))
		bwi_rf_get_gains(mac);

	if (rf->rf_rev != 8)
		bwi_rf_init(mac);

	if (tpctl->tp_ctrl2 == 0xffff) {
		bwi_rf_lo_update(mac);
	} else {
		if (rf->rf_type == BWI_RF_T_BCM2050 && rf->rf_rev == 8) {
			RF_WRITE(mac, 0x52,
			    (tpctl->tp_ctrl1 << 4) | tpctl->tp_ctrl2);
		} else {
			RF_FILT_SETBITS(mac, 0x52, 0xfff0, tpctl->tp_ctrl2);
		}

		if (phy->phy_rev >= 6) {
			PHY_FILT_SETBITS(mac, 0x36, 0xfff,
			    tpctl->tp_ctrl2 << 12);
		}

		if (sc->sc_card_flags & BWI_CARD_F_PA_GPIO9)
			PHY_WRITE(mac, 0x2e, 0x8075);
		else
			PHY_WRITE(mac, 0x2e, 0x807f);

		if (phy->phy_rev < 2)
			PHY_WRITE(mac, 0x2f, 0x101);
		else
			PHY_WRITE(mac, 0x2f, 0x202);
	}

	if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		bwi_rf_lo_adjust(mac, tpctl);
		PHY_WRITE(mac, 0x80f, 0x8078);
	}

	if ((sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) == 0) {
		bwi_rf_init_hw_nrssi_table(mac, 0xffff /* XXX */);
		bwi_rf_set_nrssi_thr(mac);
	} else if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		if (rf->rf_nrssi[0] == BWI_INVALID_NRSSI) {
			KASSERT(rf->rf_nrssi[1] == BWI_INVALID_NRSSI);
			bwi_rf_calc_nrssi_slope(mac);
		} else {
			KASSERT(rf->rf_nrssi[1] != BWI_INVALID_NRSSI);
			bwi_rf_set_nrssi_thr(mac);
		}
	}

	if (rf->rf_rev == 8)
		PHY_WRITE(mac, 0x805, 0x3230);

	bwi_mac_init_tpctl_11bg(mac);

	if (sc->sc_bbp_id == BWI_BBPID_BCM4306 && sc->sc_bbp_pkg == 2) {
		PHY_CLRBITS(mac, 0x429, 0x4000);
		PHY_CLRBITS(mac, 0x4c3, 0x8000);
	}
}

void
bwi_phy_init_11b_rev2(struct bwi_mac *mac)
{
	struct bwi_softc *sc;

	sc = mac->mac_sc;

	/* TODO: 11B */
	printf("%s: %s is not implemented yet\n",
	    sc->sc_dev.dv_xname, __func__);
}

void
bwi_phy_init_11b_rev4(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	uint16_t val, ofs;
	u_int chan;

	CSR_WRITE_2(sc, BWI_BPHY_CTRL, BWI_BPHY_CTRL_INIT);

	PHY_WRITE(mac, 0x20, 0x301c);
	PHY_WRITE(mac, 0x26, 0);
	PHY_WRITE(mac, 0x30, 0xc6);
	PHY_WRITE(mac, 0x88, 0x3e00);

	for (ofs = 0, val = 0x3c3d; ofs < 30; ++ofs, val -= 0x202)
		PHY_WRITE(mac, 0x89 + ofs, val);

	CSR_WRITE_2(sc, BWI_PHY_MAGIC_REG1, BWI_PHY_MAGIC_REG1_VAL1);

	chan = rf->rf_curchan;
	if (chan == IEEE80211_CHAN_ANY)
		chan = 6;	/* Force to channel 6 */
	bwi_rf_set_chan(mac, chan, 0);

	if (rf->rf_type != BWI_RF_T_BCM2050) {
		RF_WRITE(mac, 0x75, 0x80);
		RF_WRITE(mac, 0x79, 0x81);
	}

	RF_WRITE(mac, 0x50, 0x20);
	RF_WRITE(mac, 0x50, 0x23);

	if (rf->rf_type == BWI_RF_T_BCM2050) {
		RF_WRITE(mac, 0x50, 0x20);
		RF_WRITE(mac, 0x5a, 0x70);
		RF_WRITE(mac, 0x5b, 0x7b);
		RF_WRITE(mac, 0x5c, 0xb0);
		RF_WRITE(mac, 0x7a, 0xf);
		PHY_WRITE(mac, 0x38, 0x677);
		bwi_rf_init_bcm2050(mac);
	}

	PHY_WRITE(mac, 0x14, 0x80);
	PHY_WRITE(mac, 0x32, 0xca);
	if (rf->rf_type == BWI_RF_T_BCM2050)
		PHY_WRITE(mac, 0x32, 0xe0);
	PHY_WRITE(mac, 0x35, 0x7c2);

	bwi_rf_lo_update(mac);

	PHY_WRITE(mac, 0x26, 0xcc00);
	if (rf->rf_type == BWI_RF_T_BCM2050)
		PHY_WRITE(mac, 0x26, 0xce00);

	CSR_WRITE_2(sc, BWI_RF_CHAN_EX, 0x1100);

	PHY_WRITE(mac, 0x2a, 0x88a3);
	if (rf->rf_type == BWI_RF_T_BCM2050)
		PHY_WRITE(mac, 0x2a, 0x88c2);

	bwi_mac_set_tpctl_11bg(mac, NULL);
	if (sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) {
		bwi_rf_calc_nrssi_slope(mac);
		bwi_rf_set_nrssi_thr(mac);
	}
	bwi_mac_init_tpctl_11bg(mac);
}

void
bwi_phy_init_11b_rev5(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	uint orig_chan;

	if (phy->phy_version == 1)
		RF_SETBITS(mac, 0x7a, 0x50);

	if (sc->sc_pci_subvid != PCI_VENDOR_BROADCOM &&
	    sc->sc_pci_subdid != BWI_PCI_SUBDEVICE_BU4306) {
		uint16_t ofs, val;

		val = 0x2120;
		for (ofs = 0xa8; ofs < 0xc7; ++ofs) {
			PHY_WRITE(mac, ofs, val);
			val += 0x202;
		}
	}

	PHY_FILT_SETBITS(mac, 0x35, 0xf0ff, 0x700);

	if (rf->rf_type == BWI_RF_T_BCM2050)
		PHY_WRITE(mac, 0x38, 0x667);

	if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		if (rf->rf_type == BWI_RF_T_BCM2050) {
			RF_SETBITS(mac, 0x7a, 0x20);
			RF_SETBITS(mac, 0x51, 0x4);
		}

		CSR_WRITE_2(sc, BWI_RF_ANTDIV, 0);

		PHY_SETBITS(mac, 0x802, 0x100);
		PHY_SETBITS(mac, 0x42b, 0x2000);
		PHY_WRITE(mac, 0x1c, 0x186a);

		PHY_FILT_SETBITS(mac, 0x13, 0xff, 0x1900);
		PHY_FILT_SETBITS(mac, 0x35, 0xffc0, 0x64);
		PHY_FILT_SETBITS(mac, 0x5d, 0xff80, 0xa);
	}

	/* TODO: bad_frame_preempt? */

	if (phy->phy_version == 1) {
	    	PHY_WRITE(mac, 0x26, 0xce00);
		PHY_WRITE(mac, 0x21, 0x3763);
		PHY_WRITE(mac, 0x22, 0x1bc3);
		PHY_WRITE(mac, 0x23, 0x6f9);
		PHY_WRITE(mac, 0x24, 0x37e);
	} else
		PHY_WRITE(mac, 0x26, 0xcc00);
	PHY_WRITE(mac, 0x30, 0xc6);

	CSR_WRITE_2(sc, BWI_BPHY_CTRL, BWI_BPHY_CTRL_INIT);

	if (phy->phy_version == 1)
		PHY_WRITE(mac, 0x20, 0x3e1c);
	else
		PHY_WRITE(mac, 0x20, 0x301c);

	if (phy->phy_version == 0)
		CSR_WRITE_2(sc, BWI_PHY_MAGIC_REG1, BWI_PHY_MAGIC_REG1_VAL1);

	/* Force to channel 7 */
	orig_chan = rf->rf_curchan;
	bwi_rf_set_chan(mac, 7, 0);

	if (rf->rf_type != BWI_RF_T_BCM2050) {
		RF_WRITE(mac, 0x75, 0x80);
		RF_WRITE(mac, 0x79, 0x81);
	}

	RF_WRITE(mac, 0x50, 0x20);
	RF_WRITE(mac, 0x50, 0x23);

	if (rf->rf_type == BWI_RF_T_BCM2050) {
		RF_WRITE(mac, 0x50, 0x20);
		RF_WRITE(mac, 0x5a, 0x70);
	}

	RF_WRITE(mac, 0x5b, 0x7b);
	RF_WRITE(mac, 0x5c, 0xb0);
	RF_SETBITS(mac, 0x7a, 0x7);

	bwi_rf_set_chan(mac, orig_chan, 0);

	PHY_WRITE(mac, 0x14, 0x80);
	PHY_WRITE(mac, 0x32, 0xca);
	PHY_WRITE(mac, 0x2a, 0x88a3);

	bwi_mac_set_tpctl_11bg(mac, NULL);

	if (rf->rf_type == BWI_RF_T_BCM2050)
		RF_WRITE(mac, 0x5d, 0xd);

	CSR_FILT_SETBITS_2(sc, BWI_PHY_MAGIC_REG1, 0xffc0, 0x4);
}

void
bwi_phy_init_11b_rev6(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t val, ofs;
	uint orig_chan;

	PHY_WRITE(mac, 0x3e, 0x817a);
	RF_SETBITS(mac, 0x7a, 0x58);

	if (rf->rf_rev == 4 || rf->rf_rev == 5) {
		RF_WRITE(mac, 0x51, 0x37);
		RF_WRITE(mac, 0x52, 0x70);
		RF_WRITE(mac, 0x53, 0xb3);
		RF_WRITE(mac, 0x54, 0x9b);
		RF_WRITE(mac, 0x5a, 0x88);
		RF_WRITE(mac, 0x5b, 0x88);
		RF_WRITE(mac, 0x5d, 0x88);
		RF_WRITE(mac, 0x5e, 0x88);
		RF_WRITE(mac, 0x7d, 0x88);
		HFLAGS_SETBITS(mac, BWI_HFLAG_MAGIC1);
	} else if (rf->rf_rev == 8) {
		RF_WRITE(mac, 0x51, 0);
		RF_WRITE(mac, 0x52, 0x40);
		RF_WRITE(mac, 0x53, 0xb7);
		RF_WRITE(mac, 0x54, 0x98);
		RF_WRITE(mac, 0x5a, 0x88);
		RF_WRITE(mac, 0x5b, 0x6b);
		RF_WRITE(mac, 0x5c, 0xf);
		if (sc->sc_card_flags & BWI_CARD_F_ALT_IQ) {
			RF_WRITE(mac, 0x5d, 0xfa);
			RF_WRITE(mac, 0x5e, 0xd8);
		} else {
			RF_WRITE(mac, 0x5d, 0xf5);
			RF_WRITE(mac, 0x5e, 0xb8);
		}
		RF_WRITE(mac, 0x73, 0x3);
		RF_WRITE(mac, 0x7d, 0xa8);
		RF_WRITE(mac, 0x7c, 0x1);
		RF_WRITE(mac, 0x7e, 0x8);
	}

	val = 0x1e1f;
	for (ofs = 0x88; ofs < 0x98; ++ofs) {
		PHY_WRITE(mac, ofs, val);
		val -= 0x202;
	}

	val = 0x3e3f;
	for (ofs = 0x98; ofs < 0xa8; ++ofs) {
		PHY_WRITE(mac, ofs, val);
		val -= 0x202;
	}

	val = 0x2120;
	for (ofs = 0xa8; ofs < 0xc8; ++ofs) {
		PHY_WRITE(mac, ofs, (val & 0x3f3f));
		val += 0x202;

		/* XXX: delay 10 us to avoid PCI parity errors with BCM4318 */
		DELAY(10);
	}

	if (phy->phy_mode == IEEE80211_MODE_11G) {
		RF_SETBITS(mac, 0x7a, 0x20);
		RF_SETBITS(mac, 0x51, 0x4);
		PHY_SETBITS(mac, 0x802, 0x100);
		PHY_SETBITS(mac, 0x42b, 0x2000);
		PHY_WRITE(mac, 0x5b, 0);
		PHY_WRITE(mac, 0x5c, 0);
	}

	/* Force to channel 7 */
	orig_chan = rf->rf_curchan;
	if (orig_chan >= 8)
		bwi_rf_set_chan(mac, 1, 0);
	else
		bwi_rf_set_chan(mac, 13, 0);

	RF_WRITE(mac, 0x50, 0x20);
	RF_WRITE(mac, 0x50, 0x23);

	DELAY(40);

	if (rf->rf_rev < 6 || rf->rf_rev == 8) {
		RF_SETBITS(mac, 0x7c, 0x2);
		RF_WRITE(mac, 0x50, 0x20);
	}
	if (rf->rf_rev <= 2) {
		RF_WRITE(mac, 0x7c, 0x20);
		RF_WRITE(mac, 0x5a, 0x70);
		RF_WRITE(mac, 0x5b, 0x7b);
		RF_WRITE(mac, 0x5c, 0xb0);
	}

	RF_FILT_SETBITS(mac, 0x7a, 0xf8, 0x7);

	bwi_rf_set_chan(mac, orig_chan, 0);

	PHY_WRITE(mac, 0x14, 0x200);
	if (rf->rf_rev >= 6)
		PHY_WRITE(mac, 0x2a, 0x88c2);
	else
		PHY_WRITE(mac, 0x2a, 0x8ac0);
	PHY_WRITE(mac, 0x38, 0x668);

	bwi_mac_set_tpctl_11bg(mac, NULL);

	if (rf->rf_rev <= 5) {
		PHY_FILT_SETBITS(mac, 0x5d, 0xff80, 0x3);
		if (rf->rf_rev <= 2)
			RF_WRITE(mac, 0x5d, 0xd);
	}

	if (phy->phy_version == 4) {
		CSR_WRITE_2(sc, BWI_PHY_MAGIC_REG1, BWI_PHY_MAGIC_REG1_VAL2);
		PHY_CLRBITS(mac, 0x61, 0xf000);
	} else {
		PHY_FILT_SETBITS(mac, 0x2, 0xffc0, 0x4);
	}

	if (phy->phy_mode == IEEE80211_MODE_11B) {
		CSR_WRITE_2(sc, BWI_BBP_ATTEN, BWI_BBP_ATTEN_MAGIC2);
		PHY_WRITE(mac, 0x16, 0x410);
		PHY_WRITE(mac, 0x17, 0x820);
		PHY_WRITE(mac, 0x62, 0x7);

		bwi_rf_init_bcm2050(mac);
		bwi_rf_lo_update(mac);
		if (sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) {
			bwi_rf_calc_nrssi_slope(mac);
			bwi_rf_set_nrssi_thr(mac);
		}
		bwi_mac_init_tpctl_11bg(mac);
	} else
		CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0);
}

void
bwi_phy_config_11g(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	const uint16_t *tbl;
	uint16_t wrd_ofs1, wrd_ofs2;
	int i, n;

	if (phy->phy_rev == 1) {
		PHY_WRITE(mac, 0x406, 0x4f19);
		PHY_FILT_SETBITS(mac, 0x429, 0xfc3f, 0x340);
		PHY_WRITE(mac, 0x42c, 0x5a);
		PHY_WRITE(mac, 0x427, 0x1a);

		/* Fill frequency table */
		for (i = 0; i < nitems(bwi_phy_freq_11g_rev1); ++i) {
			bwi_tbl_write_2(mac, BWI_PHYTBL_FREQ + i,
			    bwi_phy_freq_11g_rev1[i]);
		}

		/* Fill noise table */
		for (i = 0; i < nitems(bwi_phy_noise_11g_rev1); ++i) {
			bwi_tbl_write_2(mac, BWI_PHYTBL_NOISE + i,
			    bwi_phy_noise_11g_rev1[i]);
		}

		/* Fill rotor table */
		for (i = 0; i < nitems(bwi_phy_rotor_11g_rev1); ++i) {
			/* NB: data length is 4 bytes */
			bwi_tbl_write_4(mac, BWI_PHYTBL_ROTOR + i,
			    bwi_phy_rotor_11g_rev1[i]);
		}
	} else {
		bwi_nrssi_write(mac, 0xba98, (int16_t)0x7654); /* XXX */

		if (phy->phy_rev == 2) {
			PHY_WRITE(mac, 0x4c0, 0x1861);
			PHY_WRITE(mac, 0x4c1, 0x271);
		} else if (phy->phy_rev > 2) {
			PHY_WRITE(mac, 0x4c0, 0x98);
			PHY_WRITE(mac, 0x4c1, 0x70);
			PHY_WRITE(mac, 0x4c9, 0x80);
		}
		PHY_SETBITS(mac, 0x42b, 0x800);

		/* Fill RSSI table */
		for (i = 0; i < 64; ++i)
			bwi_tbl_write_2(mac, BWI_PHYTBL_RSSI + i, i);

		/* Fill noise table */
		for (i = 0; i < nitems(bwi_phy_noise_11g); ++i) {
			bwi_tbl_write_2(mac, BWI_PHYTBL_NOISE + i,
			    bwi_phy_noise_11g[i]);
		}
	}

	/*
	 * Fill noise scale table
	 */
	if (phy->phy_rev <= 2) {
		tbl = bwi_phy_noise_scale_11g_rev2;
		n = nitems(bwi_phy_noise_scale_11g_rev2);
	} else if (phy->phy_rev >= 7 && (PHY_READ(mac, 0x449) & 0x200)) {
		tbl = bwi_phy_noise_scale_11g_rev7;
		n = nitems(bwi_phy_noise_scale_11g_rev7);
	} else {
		tbl = bwi_phy_noise_scale_11g;
		n = nitems(bwi_phy_noise_scale_11g);
	}
	for (i = 0; i < n; ++i)
		bwi_tbl_write_2(mac, BWI_PHYTBL_NOISE_SCALE + i, tbl[i]);

	/*
	 * Fill sigma square table
	 */
	if (phy->phy_rev == 2) {
		tbl = bwi_phy_sigma_sq_11g_rev2;
		n = nitems(bwi_phy_sigma_sq_11g_rev2);
	} else if (phy->phy_rev > 2 && phy->phy_rev <= 8) {
		tbl = bwi_phy_sigma_sq_11g_rev7;
		n = nitems(bwi_phy_sigma_sq_11g_rev7);
	} else {
		tbl = NULL;
		n = 0;
	}
	for (i = 0; i < n; ++i)
		bwi_tbl_write_2(mac, BWI_PHYTBL_SIGMA_SQ + i, tbl[i]);

	if (phy->phy_rev == 1) {
		/* Fill delay table */
		for (i = 0; i < nitems(bwi_phy_delay_11g_rev1); ++i) {
			bwi_tbl_write_4(mac, BWI_PHYTBL_DELAY + i,
			    bwi_phy_delay_11g_rev1[i]);
		}

		/* Fill WRSSI (Wide-Band RSSI) table */
		for (i = 4; i < 20; ++i)
			bwi_tbl_write_2(mac, BWI_PHYTBL_WRSSI_REV1 + i, 0x20);

		bwi_phy_config_agc(mac);

		wrd_ofs1 = 0x5001;
		wrd_ofs2 = 0x5002;
	} else {
		/* Fill WRSSI (Wide-Band RSSI) table */
		for (i = 0; i < 0x20; ++i)
			bwi_tbl_write_2(mac, BWI_PHYTBL_WRSSI + i, 0x820);

		bwi_phy_config_agc(mac);

		PHY_READ(mac, 0x400);	/* Dummy read */
		PHY_WRITE(mac, 0x403, 0x1000);
		bwi_tbl_write_2(mac, 0x3c02, 0xf);
		bwi_tbl_write_2(mac, 0x3c03, 0x14);

		wrd_ofs1 = 0x401;
		wrd_ofs2 = 0x402;
	}

	if (!(BWI_IS_BRCM_BU4306(sc) && sc->sc_pci_revid == 0x17)) {
		bwi_tbl_write_2(mac, wrd_ofs1, 0x2);
		bwi_tbl_write_2(mac, wrd_ofs2, 0x1);
	}

	/* phy->phy_flags & BWI_PHY_F_LINKED ? */
	if (sc->sc_card_flags & BWI_CARD_F_PA_GPIO9)
		PHY_WRITE(mac, 0x46e, 0x3cf);
}
#undef N

/*
 * Configure Automatic Gain Controller
 */
void
bwi_phy_config_agc(struct bwi_mac *mac)
{
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t ofs;

	ofs = phy->phy_rev == 1 ? 0x4c00 : 0;

	bwi_tbl_write_2(mac, ofs, 0xfe);
	bwi_tbl_write_2(mac, ofs + 1, 0xd);
	bwi_tbl_write_2(mac, ofs + 2, 0x13);
	bwi_tbl_write_2(mac, ofs + 3, 0x19);

	if (phy->phy_rev == 1) {
		bwi_tbl_write_2(mac, 0x1800, 0x2710);
		bwi_tbl_write_2(mac, 0x1801, 0x9b83);
		bwi_tbl_write_2(mac, 0x1802, 0x9b83);
		bwi_tbl_write_2(mac, 0x1803, 0xf8d);
		PHY_WRITE(mac, 0x455, 0x4);
	}

	PHY_FILT_SETBITS(mac, 0x4a5, 0xff, 0x5700);
	PHY_FILT_SETBITS(mac, 0x41a, 0xff80, 0xf);
	PHY_FILT_SETBITS(mac, 0x41a, 0xc07f, 0x2b80);
	PHY_FILT_SETBITS(mac, 0x48c, 0xf0ff, 0x300);

	RF_SETBITS(mac, 0x7a, 0x8);

	PHY_FILT_SETBITS(mac, 0x4a0, 0xfff0, 0x8);
	PHY_FILT_SETBITS(mac, 0x4a1, 0xf0ff, 0x600);
	PHY_FILT_SETBITS(mac, 0x4a2, 0xf0ff, 0x700);
	PHY_FILT_SETBITS(mac, 0x4a0, 0xf0ff, 0x100);

	if (phy->phy_rev == 1)
		PHY_FILT_SETBITS(mac, 0x4a2, 0xfff0, 0x7);

	PHY_FILT_SETBITS(mac, 0x488, 0xff00, 0x1c);
	PHY_FILT_SETBITS(mac, 0x488, 0xc0ff, 0x200);
	PHY_FILT_SETBITS(mac, 0x496, 0xff00, 0x1c);
	PHY_FILT_SETBITS(mac, 0x489, 0xff00, 0x20);
	PHY_FILT_SETBITS(mac, 0x489, 0xc0ff, 0x200);
	PHY_FILT_SETBITS(mac, 0x482, 0xff00, 0x2e);
	PHY_FILT_SETBITS(mac, 0x496, 0xff, 0x1a00);
	PHY_FILT_SETBITS(mac, 0x481, 0xff00, 0x28);
	PHY_FILT_SETBITS(mac, 0x481, 0xff, 0x2c00);

	if (phy->phy_rev == 1) {
		PHY_WRITE(mac, 0x430, 0x92b);
		PHY_FILT_SETBITS(mac, 0x41b, 0xffe1, 0x2);
	} else {
		PHY_CLRBITS(mac, 0x41b, 0x1e);
		PHY_WRITE(mac, 0x41f, 0x287a);
		PHY_FILT_SETBITS(mac, 0x420, 0xfff0, 0x4);

		if (phy->phy_rev >= 6) {
			PHY_WRITE(mac, 0x422, 0x287a);
			PHY_FILT_SETBITS(mac, 0x420, 0xfff, 0x3000);
		}
	}

	PHY_FILT_SETBITS(mac, 0x4a8, 0x8080, 0x7874);
	PHY_WRITE(mac, 0x48e, 0x1c00);

	if (phy->phy_rev == 1) {
		PHY_FILT_SETBITS(mac, 0x4ab, 0xf0ff, 0x600);
		PHY_WRITE(mac, 0x48b, 0x5e);
		PHY_FILT_SETBITS(mac, 0x48c, 0xff00, 0x1e);
		PHY_WRITE(mac, 0x48d, 0x2);
	}

	bwi_tbl_write_2(mac, ofs + 0x800, 0);
	bwi_tbl_write_2(mac, ofs + 0x801, 7);
	bwi_tbl_write_2(mac, ofs + 0x802, 16);
	bwi_tbl_write_2(mac, ofs + 0x803, 28);

	if (phy->phy_rev >= 6) {
		PHY_CLRBITS(mac, 0x426, 0x3);
		PHY_CLRBITS(mac, 0x426, 0x1000);
	}
}

void
bwi_set_gains(struct bwi_mac *mac, const struct bwi_gains *gains)
{
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t tbl_gain_ofs1, tbl_gain_ofs2, tbl_gain;
	int i;

	if (phy->phy_rev <= 1) {
		tbl_gain_ofs1 = 0x5000;
		tbl_gain_ofs2 = tbl_gain_ofs1 + 16;
	} else {
		tbl_gain_ofs1 = 0x400;
		tbl_gain_ofs2 = tbl_gain_ofs1 + 8;
	}

	for (i = 0; i < 4; ++i) {
		if (gains != NULL) {
			tbl_gain = gains->tbl_gain1;
		} else {
			/* Bit swap */
			tbl_gain = (i & 0x1) << 1;
			tbl_gain |= (i & 0x2) >> 1;
		}
		bwi_tbl_write_2(mac, tbl_gain_ofs1 + i, tbl_gain);
	}

	for (i = 0; i < 16; ++i) {
		if (gains != NULL)
			tbl_gain = gains->tbl_gain2;
		else
			tbl_gain = i;
		bwi_tbl_write_2(mac, tbl_gain_ofs2 + i, tbl_gain);
	}

	if (gains == NULL || (gains != NULL && gains->phy_gain != -1)) {
		uint16_t phy_gain1, phy_gain2;

		if (gains != NULL) {
			phy_gain1 =
			((uint16_t)gains->phy_gain << 14) |
			((uint16_t)gains->phy_gain << 6);
			phy_gain2 = phy_gain1;
		} else {
			phy_gain1 = 0x4040;
			phy_gain2 = 0x4000;
		}
		PHY_FILT_SETBITS(mac, 0x4a0, 0xbfbf, phy_gain1);
		PHY_FILT_SETBITS(mac, 0x4a1, 0xbfbf, phy_gain1);
		PHY_FILT_SETBITS(mac, 0x4a2, 0xbfbf, phy_gain2);
	}
	bwi_mac_dummy_xmit(mac);
}

void
bwi_phy_clear_state(struct bwi_phy *phy)
{
	phy->phy_flags &= ~BWI_CLEAR_PHY_FLAGS;
}

/* RF */

int16_t
bwi_nrssi_11g(struct bwi_mac *mac)
{
	int16_t val;

#define NRSSI_11G_MASK		0x3f00
	val = (int16_t)__SHIFTOUT(PHY_READ(mac, 0x47f), NRSSI_11G_MASK);
	if (val >= 32)
		val -= 64;

	return (val);
#undef NRSSI_11G_MASK
}

struct bwi_rf_lo *
bwi_get_rf_lo(struct bwi_mac *mac, uint16_t rf_atten, uint16_t bbp_atten)
{
	int n;

	n = rf_atten + (14 * (bbp_atten / 2));
	KASSERT(n < BWI_RFLO_MAX);

	return (&mac->mac_rf.rf_lo[n]);
}

int
bwi_rf_lo_isused(struct bwi_mac *mac, const struct bwi_rf_lo *lo)
{
	struct bwi_rf *rf = &mac->mac_rf;
	int idx;

	idx = lo - rf->rf_lo;
	KASSERT(idx >= 0 && idx < BWI_RFLO_MAX);

	return (isset(rf->rf_lo_used, idx));
}

void
bwi_rf_write(struct bwi_mac *mac, uint16_t ctrl, uint16_t data)
{
	struct bwi_softc *sc = mac->mac_sc;

	CSR_WRITE_2(sc, BWI_RF_CTRL, ctrl);
	CSR_WRITE_2(sc, BWI_RF_DATA_LO, data);
}

uint16_t
bwi_rf_read(struct bwi_mac *mac, uint16_t ctrl)
{
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_softc *sc = mac->mac_sc;

	ctrl |= rf->rf_ctrl_rd;
	if (rf->rf_ctrl_adj) {
		/* XXX */
		if (ctrl < 0x70)
			ctrl += 0x80;
		else if (ctrl < 0x80)
			ctrl += 0x70;
	}

	CSR_WRITE_2(sc, BWI_RF_CTRL, ctrl);
	return (CSR_READ_2(sc, BWI_RF_DATA_LO));
}

int
bwi_rf_attach(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	uint16_t type, manu;
	uint8_t rev;

	/*
	 * Get RF manufacture/type/revision
	 */
	if (sc->sc_bbp_id == BWI_BBPID_BCM4317) {
		/*
		 * Fake a BCM2050 RF
		 */
		manu = BWI_RF_MANUFACT_BCM;
		type = BWI_RF_T_BCM2050;
		if (sc->sc_bbp_rev == 0)
			rev = 3;
		else if (sc->sc_bbp_rev == 1)
			rev = 4;
		else
			rev = 5;
	} else {
		uint32_t val;

		CSR_WRITE_2(sc, BWI_RF_CTRL, BWI_RF_CTRL_RFINFO);
		val = CSR_READ_2(sc, BWI_RF_DATA_HI);
		val <<= 16;

		CSR_WRITE_2(sc, BWI_RF_CTRL, BWI_RF_CTRL_RFINFO);
		val |= CSR_READ_2(sc, BWI_RF_DATA_LO);

		manu = __SHIFTOUT(val, BWI_RFINFO_MANUFACT_MASK);
		type = __SHIFTOUT(val, BWI_RFINFO_TYPE_MASK);
		rev = __SHIFTOUT(val, BWI_RFINFO_REV_MASK);
	}
	DPRINTF(1, "%s: RF manu 0x%03x, type 0x%04x, rev %u\n",
	    sc->sc_dev.dv_xname, manu, type, rev);

	/*
	 * Verify whether the RF is supported
	 */
	rf->rf_ctrl_rd = 0;
	rf->rf_ctrl_adj = 0;
	switch (phy->phy_mode) {
	case IEEE80211_MODE_11A:
		if (manu != BWI_RF_MANUFACT_BCM ||
		    type != BWI_RF_T_BCM2060 ||
		    rev != 1) {
			printf("%s: only BCM2060 rev 1 RF is supported for "
			    "11A PHY\n", sc->sc_dev.dv_xname);
			return (ENXIO);
		}
		rf->rf_ctrl_rd = BWI_RF_CTRL_RD_11A;
		rf->rf_on = bwi_rf_on_11a;
		rf->rf_off = bwi_rf_off_11a;
		rf->rf_calc_rssi = bwi_rf_calc_rssi_bcm2060;
		break;
	case IEEE80211_MODE_11B:
		if (type == BWI_RF_T_BCM2050) {
			rf->rf_ctrl_rd = BWI_RF_CTRL_RD_11BG;
			rf->rf_calc_rssi = bwi_rf_calc_rssi_bcm2050;
		} else if (type == BWI_RF_T_BCM2053) {
			rf->rf_ctrl_adj = 1;
			rf->rf_calc_rssi = bwi_rf_calc_rssi_bcm2053;
		} else {
			printf("%s: only BCM2050/BCM2053 RF is supported "
			    "for supported for 11B PHY\n", sc->sc_dev.dv_xname);
			return (ENXIO);
		}
		rf->rf_on = bwi_rf_on_11bg;
		rf->rf_off = bwi_rf_off_11bg;
		rf->rf_calc_nrssi_slope = bwi_rf_calc_nrssi_slope_11b;
		rf->rf_set_nrssi_thr = bwi_rf_set_nrssi_thr_11b;
		if (phy->phy_rev == 6)
			rf->rf_lo_update = bwi_rf_lo_update_11g;
		else
			rf->rf_lo_update = bwi_rf_lo_update_11b;
		break;
	case IEEE80211_MODE_11G:
		if (type != BWI_RF_T_BCM2050) {
			printf("%s: only BCM2050 RF is supported for 11G "
			    "PHY\n", sc->sc_dev.dv_xname);
			return (ENXIO);
		}
		rf->rf_ctrl_rd = BWI_RF_CTRL_RD_11BG;
		rf->rf_on = bwi_rf_on_11bg;
		if (mac->mac_rev >= 5)
			rf->rf_off = bwi_rf_off_11g_rev5;
		else
			rf->rf_off = bwi_rf_off_11bg;
		rf->rf_calc_nrssi_slope = bwi_rf_calc_nrssi_slope_11g;
		rf->rf_set_nrssi_thr = bwi_rf_set_nrssi_thr_11g;
		rf->rf_calc_rssi = bwi_rf_calc_rssi_bcm2050;
		rf->rf_lo_update = bwi_rf_lo_update_11g;
		break;
	default:
		printf("%s: unsupported PHY mode\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}

	rf->rf_type = type;
	rf->rf_rev = rev;
	rf->rf_manu = manu;
	rf->rf_curchan = IEEE80211_CHAN_ANY;
	rf->rf_ant_mode = BWI_ANT_MODE_AUTO;

	return (0);
}

void
bwi_rf_set_chan(struct bwi_mac *mac, uint chan, int work_around)
{
	struct bwi_softc *sc = mac->mac_sc;

	if (chan == IEEE80211_CHAN_ANY)
		return;

	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_CHAN, chan);

	/* TODO: 11A */

	if (work_around)
		bwi_rf_workaround(mac, chan);

	CSR_WRITE_2(sc, BWI_RF_CHAN, BWI_RF_2GHZ_CHAN(chan));

	if (chan == 14) {
		if (sc->sc_locale == BWI_SPROM_LOCALE_JAPAN)
			HFLAGS_CLRBITS(mac, BWI_HFLAG_NOT_JAPAN);
		else
			HFLAGS_SETBITS(mac, BWI_HFLAG_NOT_JAPAN);
		CSR_SETBITS_2(sc, BWI_RF_CHAN_EX, (1 << 11)); /* XXX */
	} else {
		CSR_CLRBITS_2(sc, BWI_RF_CHAN_EX, 0x840); /* XXX */
	}
	DELAY(8000);	/* DELAY(2000); */

	mac->mac_rf.rf_curchan = chan;
}

void
bwi_rf_get_gains(struct bwi_mac *mac)
{
#define SAVE_PHY_MAX	15
#define SAVE_RF_MAX	3
	struct bwi_softc *sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	uint16_t save_phy[SAVE_PHY_MAX];
	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t trsw;
	int i, j, loop1_max, loop1, loop2;

	static const uint16_t save_rf_regs[SAVE_RF_MAX] =
	    { 0x52, 0x43, 0x7a };
	static const uint16_t save_phy_regs[SAVE_PHY_MAX] = {
	    0x0429, 0x0001, 0x0811, 0x0812,
	    0x0814, 0x0815, 0x005a, 0x0059,
	    0x0058, 0x000a, 0x0003, 0x080f,
	    0x0810, 0x002b, 0x0015
	};

	sc = mac->mac_sc;

	/*
	 * Save PHY/RF registers for later restoration
	 */
	for (i = 0; i < SAVE_PHY_MAX; ++i)
		save_phy[i] = PHY_READ(mac, save_phy_regs[i]);
	PHY_READ(mac, 0x2d); /* dummy read */

	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = RF_READ(mac, save_rf_regs[i]);

	PHY_CLRBITS(mac, 0x429, 0xc000);
	PHY_SETBITS(mac, 0x1, 0x8000);

	PHY_SETBITS(mac, 0x811, 0x2);
	PHY_CLRBITS(mac, 0x812, 0x2);
	PHY_SETBITS(mac, 0x811, 0x1);
	PHY_CLRBITS(mac, 0x812, 0x1);

	PHY_SETBITS(mac, 0x814, 0x1);
	PHY_CLRBITS(mac, 0x815, 0x1);
	PHY_SETBITS(mac, 0x814, 0x2);
	PHY_CLRBITS(mac, 0x815, 0x2);

	PHY_SETBITS(mac, 0x811, 0xc);
	PHY_SETBITS(mac, 0x812, 0xc);
	PHY_SETBITS(mac, 0x811, 0x30);
	PHY_FILT_SETBITS(mac, 0x812, 0xffcf, 0x10);

	PHY_WRITE(mac, 0x5a, 0x780);
	PHY_WRITE(mac, 0x59, 0xc810);
	PHY_WRITE(mac, 0x58, 0xd);
	PHY_SETBITS(mac, 0xa, 0x2000);

	PHY_SETBITS(mac, 0x814, 0x4);
	PHY_CLRBITS(mac, 0x815, 0x4);

	PHY_FILT_SETBITS(mac, 0x3, 0xff9f, 0x40);

	if (rf->rf_rev == 8) {
		loop1_max = 15;
		RF_WRITE(mac, 0x43, loop1_max);
	} else {
		loop1_max = 9;
	    	RF_WRITE(mac, 0x52, 0x0);
		RF_FILT_SETBITS(mac, 0x43, 0xfff0, loop1_max);
	}

	bwi_phy_set_bbp_atten(mac, 11);

	if (phy->phy_rev >= 3)
		PHY_WRITE(mac, 0x80f, 0xc020);
	else
		PHY_WRITE(mac, 0x80f, 0x8020);
	PHY_WRITE(mac, 0x810, 0);

	PHY_FILT_SETBITS(mac, 0x2b, 0xffc0, 0x1);
	PHY_FILT_SETBITS(mac, 0x2b, 0xc0ff, 0x800);
	PHY_SETBITS(mac, 0x811, 0x100);
	PHY_CLRBITS(mac, 0x812, 0x3000);

	if ((mac->mac_sc->sc_card_flags & BWI_CARD_F_EXT_LNA) &&
	    phy->phy_rev >= 7) {
		PHY_SETBITS(mac, 0x811, 0x800);
		PHY_SETBITS(mac, 0x812, 0x8000);
	}
	RF_CLRBITS(mac, 0x7a, 0xff08);

	/*
	 * Find out 'loop1/loop2', which will be used to calculate
	 * max loopback gain later
	 */
	j = 0;
	for (i = 0; i < loop1_max; ++i) {
		for (j = 0; j < 16; ++j) {
			RF_WRITE(mac, 0x43, i);

			if (bwi_rf_gain_max_reached(mac, j))
				goto loop1_exit;
		}
	}
loop1_exit:
	loop1 = i;
	loop2 = j;

	/*
	 * Find out 'trsw', which will be used to calculate
	 * TRSW(TX/RX switch) RX gain later
	 */
	if (loop2 >= 8) {
		PHY_SETBITS(mac, 0x812, 0x30);
		trsw = 0x1b;
		for (i = loop2 - 8; i < 16; ++i) {
			trsw -= 3;
			if (bwi_rf_gain_max_reached(mac, i))
				break;
		}
	} else {
		trsw = 0x18;
	}

	/*
	 * Restore saved PHY/RF registers
	 */
	/* First 4 saved PHY registers need special processing */
	for (i = 4; i < SAVE_PHY_MAX; ++i)
		PHY_WRITE(mac, save_phy_regs[i], save_phy[i]);

	bwi_phy_set_bbp_atten(mac, mac->mac_tpctl.bbp_atten);

	for (i = 0; i < SAVE_RF_MAX; ++i)
		RF_WRITE(mac, save_rf_regs[i], save_rf[i]);

	PHY_WRITE(mac, save_phy_regs[2], save_phy[2] | 0x3);
	DELAY(10);
	PHY_WRITE(mac, save_phy_regs[2], save_phy[2]);
	PHY_WRITE(mac, save_phy_regs[3], save_phy[3]);
	PHY_WRITE(mac, save_phy_regs[0], save_phy[0]);
	PHY_WRITE(mac, save_phy_regs[1], save_phy[1]);

	/*
	 * Calculate gains
	 */
	rf->rf_lo_gain = (loop2 * 6) - (loop1 * 4) - 11;
	rf->rf_rx_gain = trsw * 2;
	DPRINTF(1, "%s: lo gain: %u, rx gain: %u\n",
	    sc->sc_dev.dv_xname, rf->rf_lo_gain, rf->rf_rx_gain);

#undef SAVE_RF_MAX
#undef SAVE_PHY_MAX
}

void
bwi_rf_init(struct bwi_mac *mac)
{
	struct bwi_rf *rf = &mac->mac_rf;

	if (rf->rf_type == BWI_RF_T_BCM2060) {
		/* TODO: 11A */
	} else {
		if (rf->rf_flags & BWI_RF_F_INITED)
			RF_WRITE(mac, 0x78, rf->rf_calib);
		else
			bwi_rf_init_bcm2050(mac);
	}
}

void
bwi_rf_off_11a(struct bwi_mac *mac)
{
	RF_WRITE(mac, 0x4, 0xff);
	RF_WRITE(mac, 0x5, 0xfb);

	PHY_SETBITS(mac, 0x10, 0x8);
	PHY_SETBITS(mac, 0x11, 0x8);

	PHY_WRITE(mac, 0x15, 0xaa00);
}

void
bwi_rf_off_11bg(struct bwi_mac *mac)
{
	PHY_WRITE(mac, 0x15, 0xaa00);
}

void
bwi_rf_off_11g_rev5(struct bwi_mac *mac)
{
	PHY_SETBITS(mac, 0x811, 0x8c);
	PHY_CLRBITS(mac, 0x812, 0x8c);
}

void
bwi_rf_workaround(struct bwi_mac *mac, uint chan)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;

	if (chan == IEEE80211_CHAN_ANY) {
		printf("%s: %s invalid channel!\n",
		    sc->sc_dev.dv_xname, __func__);
		return;
	}

	if (rf->rf_type != BWI_RF_T_BCM2050 || rf->rf_rev >= 6)
		return;

	if (chan <= 10)
		CSR_WRITE_2(sc, BWI_RF_CHAN, BWI_RF_2GHZ_CHAN(chan + 4));
	else
		CSR_WRITE_2(sc, BWI_RF_CHAN, BWI_RF_2GHZ_CHAN(1));
	DELAY(1000);
	CSR_WRITE_2(sc, BWI_RF_CHAN, BWI_RF_2GHZ_CHAN(chan));
}

struct bwi_rf_lo *
bwi_rf_lo_find(struct bwi_mac *mac, const struct bwi_tpctl *tpctl)
{
	uint16_t rf_atten, bbp_atten;
	int remap_rf_atten;

	remap_rf_atten = 1;
	if (tpctl == NULL) {
		bbp_atten = 2;
		rf_atten = 3;
	} else {
		if (tpctl->tp_ctrl1 == 3)
			remap_rf_atten = 0;

		bbp_atten = tpctl->bbp_atten;
		rf_atten = tpctl->rf_atten;

		if (bbp_atten > 6)
			bbp_atten = 6;
	}

	if (remap_rf_atten) {
#define MAP_MAX	10
		static const uint16_t map[MAP_MAX] =
		{ 11, 10, 11, 12, 13, 12, 13, 12, 13, 12 };
#if 0
		KASSERT(rf_atten < MAP_MAX);
		rf_atten = map[rf_atten];
#else
		if (rf_atten >= MAP_MAX) {
			rf_atten = 0;	/* XXX */
		} else {
			rf_atten = map[rf_atten];
		}
#endif
#undef MAP_MAX
	}

	return (bwi_get_rf_lo(mac, rf_atten, bbp_atten));
}

void
bwi_rf_lo_adjust(struct bwi_mac *mac, const struct bwi_tpctl *tpctl)
{
	const struct bwi_rf_lo *lo;

	lo = bwi_rf_lo_find(mac, tpctl);
	RF_LO_WRITE(mac, lo);
}

void
bwi_rf_lo_write(struct bwi_mac *mac, const struct bwi_rf_lo *lo)
{
	uint16_t val;

	val = (uint8_t)lo->ctrl_lo;
	val |= ((uint8_t)lo->ctrl_hi) << 8;

	PHY_WRITE(mac, BWI_PHYR_RF_LO, val);
}

int
bwi_rf_gain_max_reached(struct bwi_mac *mac, int idx)
{
	PHY_FILT_SETBITS(mac, 0x812, 0xf0ff, idx << 8);
	PHY_FILT_SETBITS(mac, 0x15, 0xfff, 0xa000);
	PHY_SETBITS(mac, 0x15, 0xf000);

	DELAY(20);

	return ((PHY_READ(mac, 0x2d) >= 0xdfc));
}

/* XXX use bitmap array */
uint16_t
bwi_bitswap4(uint16_t val)
{
	uint16_t ret;

	ret = (val & 0x8) >> 3;
	ret |= (val & 0x4) >> 1;
	ret |= (val & 0x2) << 1;
	ret |= (val & 0x1) << 3;

	return (ret);
}

uint16_t
bwi_phy812_value(struct bwi_mac *mac, uint16_t lpd)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	uint16_t lo_gain, ext_lna, loop;

	if ((phy->phy_flags & BWI_PHY_F_LINKED) == 0)
		return (0);

	lo_gain = rf->rf_lo_gain;
	if (rf->rf_rev == 8)
		lo_gain += 0x3e;
	else
		lo_gain += 0x26;

	if (lo_gain >= 0x46) {
		lo_gain -= 0x46;
		ext_lna = 0x3000;
	} else if (lo_gain >= 0x3a) {
		lo_gain -= 0x3a;
		ext_lna = 0x1000;
	} else if (lo_gain >= 0x2e) {
		lo_gain -= 0x2e;
		ext_lna = 0x2000;
	} else {
		lo_gain -= 0x10;
		ext_lna = 0;
	}

	for (loop = 0; loop < 16; ++loop) {
		lo_gain -= (6 * loop);
		if (lo_gain < 6)
			break;
	}

	if (phy->phy_rev >= 7 && (sc->sc_card_flags & BWI_CARD_F_EXT_LNA)) {
		if (ext_lna)
			ext_lna |= 0x8000;
		ext_lna |= (loop << 8);
		switch (lpd) {
		case 0x011:
			return (0x8f92);
		case 0x001:
			return ((0x8092 | ext_lna));
		case 0x101:
			return ((0x2092 | ext_lna));
		case 0x100:
			return ((0x2093 | ext_lna));
		default:
			panic("unsupported lpd");
		}
	} else {
		ext_lna |= (loop << 8);
		switch (lpd) {
		case 0x011:
			return (0xf92);
		case 0x001:
		case 0x101:
			return ((0x92 | ext_lna));
		case 0x100:
			return ((0x93 | ext_lna));
		default:
			panic("unsupported lpd");
		}
	}

	panic("never reached");

	return (0);
}

void
bwi_rf_init_bcm2050(struct bwi_mac *mac)
{
#define SAVE_RF_MAX		3
#define SAVE_PHY_COMM_MAX	4
#define SAVE_PHY_11G_MAX	6
	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t save_phy_comm[SAVE_PHY_COMM_MAX];
	uint16_t save_phy_11g[SAVE_PHY_11G_MAX];
	uint16_t phyr_35, phyr_30 = 0, rfr_78, phyr_80f = 0, phyr_810 = 0;
	uint16_t bphy_ctrl = 0, bbp_atten, rf_chan_ex;
	uint16_t phy812_val;
	uint16_t calib;
	uint32_t test_lim, test;
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	int i;

	static const uint16_t save_rf_regs[SAVE_RF_MAX] =
	    { 0x0043, 0x0051, 0x0052 };
	static const uint16_t save_phy_regs_comm[SAVE_PHY_COMM_MAX] =
	    { 0x0015, 0x005a, 0x0059, 0x0058 };
	static const uint16_t save_phy_regs_11g[SAVE_PHY_11G_MAX] =
	    { 0x0811, 0x0812, 0x0814, 0x0815, 0x0429, 0x0802 };

	/*
	 * Save registers for later restoring
	 */
	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = RF_READ(mac, save_rf_regs[i]);
	for (i = 0; i < SAVE_PHY_COMM_MAX; ++i)
		save_phy_comm[i] = PHY_READ(mac, save_phy_regs_comm[i]);

	if (phy->phy_mode == IEEE80211_MODE_11B) {
		phyr_30 = PHY_READ(mac, 0x30);
		bphy_ctrl = CSR_READ_2(sc, BWI_BPHY_CTRL);

		PHY_WRITE(mac, 0x30, 0xff);
		CSR_WRITE_2(sc, BWI_BPHY_CTRL, 0x3f3f);
	} else if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		for (i = 0; i < SAVE_PHY_11G_MAX; ++i) {
			save_phy_11g[i] =
			    PHY_READ(mac, save_phy_regs_11g[i]);
		}

		PHY_SETBITS(mac, 0x814, 0x3);
		PHY_CLRBITS(mac, 0x815, 0x3);
		PHY_CLRBITS(mac, 0x429, 0x8000);
		PHY_CLRBITS(mac, 0x802, 0x3);

		phyr_80f = PHY_READ(mac, 0x80f);
		phyr_810 = PHY_READ(mac, 0x810);

		if (phy->phy_rev >= 3)
			PHY_WRITE(mac, 0x80f, 0xc020);
		else
			PHY_WRITE(mac, 0x80f, 0x8020);
		PHY_WRITE(mac, 0x810, 0);

		phy812_val = bwi_phy812_value(mac, 0x011);
		PHY_WRITE(mac, 0x812, phy812_val);
		if (phy->phy_rev < 7 ||
		    (sc->sc_card_flags & BWI_CARD_F_EXT_LNA) == 0)
			PHY_WRITE(mac, 0x811, 0x1b3);
		else
			PHY_WRITE(mac, 0x811, 0x9b3);
	}
	CSR_SETBITS_2(sc, BWI_RF_ANTDIV, 0x8000);

	phyr_35 = PHY_READ(mac, 0x35);
	PHY_CLRBITS(mac, 0x35, 0x80);

	bbp_atten = CSR_READ_2(sc, BWI_BBP_ATTEN);
	rf_chan_ex = CSR_READ_2(sc, BWI_RF_CHAN_EX);

	if (phy->phy_version == 0) {
		CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0x122);
	} else {
		if (phy->phy_version >= 2)
			PHY_FILT_SETBITS(mac, 0x3, 0xffbf, 0x40);
		CSR_SETBITS_2(sc, BWI_RF_CHAN_EX, 0x2000);
	}

	calib = bwi_rf_calibval(mac);

	if (phy->phy_mode == IEEE80211_MODE_11B)
		RF_WRITE(mac, 0x78, 0x26);

	if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		phy812_val = bwi_phy812_value(mac, 0x011);
		PHY_WRITE(mac, 0x812, phy812_val);
	}

	PHY_WRITE(mac, 0x15, 0xbfaf);
	PHY_WRITE(mac, 0x2b, 0x1403);

	if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		phy812_val = bwi_phy812_value(mac, 0x001);
		PHY_WRITE(mac, 0x812, phy812_val);
	}

	PHY_WRITE(mac, 0x15, 0xbfa0);

	RF_SETBITS(mac, 0x51, 0x4);
	if (rf->rf_rev == 8)
		RF_WRITE(mac, 0x43, 0x1f);
	else {
		RF_WRITE(mac, 0x52, 0);
		RF_FILT_SETBITS(mac, 0x43, 0xfff0, 0x9);
	}

	test_lim = 0;
	PHY_WRITE(mac, 0x58, 0);
	for (i = 0; i < 16; ++i) {
		PHY_WRITE(mac, 0x5a, 0x480);
		PHY_WRITE(mac, 0x59, 0xc810);

		PHY_WRITE(mac, 0x58, 0xd);
		if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
			phy812_val = bwi_phy812_value(mac, 0x101);
			PHY_WRITE(mac, 0x812, phy812_val);
		}
		PHY_WRITE(mac, 0x15, 0xafb0);
		DELAY(10);

		if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
			phy812_val = bwi_phy812_value(mac, 0x101);
			PHY_WRITE(mac, 0x812, phy812_val);
		}
		PHY_WRITE(mac, 0x15, 0xefb0);
		DELAY(10);

		if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
			phy812_val = bwi_phy812_value(mac, 0x100);
			PHY_WRITE(mac, 0x812, phy812_val);
		}
		PHY_WRITE(mac, 0x15, 0xfff0);
		DELAY(20);

		test_lim += PHY_READ(mac, 0x2d);

		PHY_WRITE(mac, 0x58, 0);
		if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
			phy812_val = bwi_phy812_value(mac, 0x101);
			PHY_WRITE(mac, 0x812, phy812_val);
		}
		PHY_WRITE(mac, 0x15, 0xafb0);
	}
	++test_lim;
	test_lim >>= 9;

	DELAY(10);

	test = 0;
	PHY_WRITE(mac, 0x58, 0);
	for (i = 0; i < 16; ++i) {
		int j;

		rfr_78 = (bwi_bitswap4(i) << 1) | 0x20;
		RF_WRITE(mac, 0x78, rfr_78);
		DELAY(10);

		/* NB: This block is slight different than the above one */
		for (j = 0; j < 16; ++j) {
			PHY_WRITE(mac, 0x5a, 0xd80);
			PHY_WRITE(mac, 0x59, 0xc810);

			PHY_WRITE(mac, 0x58, 0xd);
			if ((phy->phy_flags & BWI_PHY_F_LINKED) ||
			    phy->phy_rev >= 2) {
				phy812_val = bwi_phy812_value(mac, 0x101);
				PHY_WRITE(mac, 0x812, phy812_val);
			}
			PHY_WRITE(mac, 0x15, 0xafb0);
			DELAY(10);

			if ((phy->phy_flags & BWI_PHY_F_LINKED) ||
			    phy->phy_rev >= 2) {
				phy812_val = bwi_phy812_value(mac, 0x101);
				PHY_WRITE(mac, 0x812, phy812_val);
			}
			PHY_WRITE(mac, 0x15, 0xefb0);
			DELAY(10);

			if ((phy->phy_flags & BWI_PHY_F_LINKED) ||
			    phy->phy_rev >= 2) {
				phy812_val = bwi_phy812_value(mac, 0x100);
				PHY_WRITE(mac, 0x812, phy812_val);
			}
			PHY_WRITE(mac, 0x15, 0xfff0);
			DELAY(10);

			test += PHY_READ(mac, 0x2d);

			PHY_WRITE(mac, 0x58, 0);
			if ((phy->phy_flags & BWI_PHY_F_LINKED) ||
			    phy->phy_rev >= 2) {
				phy812_val = bwi_phy812_value(mac, 0x101);
				PHY_WRITE(mac, 0x812, phy812_val);
			}
			PHY_WRITE(mac, 0x15, 0xafb0);
		}

		++test;
		test >>= 8;

		if (test > test_lim)
			break;
	}
	if (i > 15)
		rf->rf_calib = rfr_78;
	else
		rf->rf_calib = calib;
	if (rf->rf_calib != 0xffff) {
		DPRINTF(1, "%s: RF calibration value: 0x%04x\n",
		    sc->sc_dev.dv_xname, rf->rf_calib);
		rf->rf_flags |= BWI_RF_F_INITED;
	}

	/*
	 * Restore trashes registers
	 */
	PHY_WRITE(mac, save_phy_regs_comm[0], save_phy_comm[0]);

	for (i = 0; i < SAVE_RF_MAX; ++i) {
		int pos = (i + 1) % SAVE_RF_MAX;

		RF_WRITE(mac, save_rf_regs[pos], save_rf[pos]);
	}
	for (i = 1; i < SAVE_PHY_COMM_MAX; ++i)
		PHY_WRITE(mac, save_phy_regs_comm[i], save_phy_comm[i]);

	CSR_WRITE_2(sc, BWI_BBP_ATTEN, bbp_atten);
	if (phy->phy_version != 0)
		CSR_WRITE_2(sc, BWI_RF_CHAN_EX, rf_chan_ex);

	PHY_WRITE(mac, 0x35, phyr_35);
	bwi_rf_workaround(mac, rf->rf_curchan);

	if (phy->phy_mode == IEEE80211_MODE_11B) {
		PHY_WRITE(mac, 0x30, phyr_30);
		CSR_WRITE_2(sc, BWI_BPHY_CTRL, bphy_ctrl);
	} else if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		/* XXX Spec only says when PHY is linked (gmode) */
		CSR_CLRBITS_2(sc, BWI_RF_ANTDIV, 0x8000);

		for (i = 0; i < SAVE_PHY_11G_MAX; ++i) {
			PHY_WRITE(mac, save_phy_regs_11g[i],
				  save_phy_11g[i]);
		}

		PHY_WRITE(mac, 0x80f, phyr_80f);
		PHY_WRITE(mac, 0x810, phyr_810);
	}

#undef SAVE_PHY_11G_MAX
#undef SAVE_PHY_COMM_MAX
#undef SAVE_RF_MAX
}

uint16_t
bwi_rf_calibval(struct bwi_mac *mac)
{
	uint16_t val, calib;
	int idx;

	/* http://bcm-specs.sipsolutions.net/RCCTable */
	static const uint16_t rf_calibvals[] = {
		0x2, 0x3, 0x1, 0xf, 0x6, 0x7, 0x5, 0xf,
		0xa, 0xb, 0x9, 0xf, 0xe, 0xf, 0xd, 0xf
	};

	val = RF_READ(mac, BWI_RFR_BBP_ATTEN);
	idx = __SHIFTOUT(val, BWI_RFR_BBP_ATTEN_CALIB_IDX);
	KASSERT(idx < (int)(sizeof(rf_calibvals) / sizeof(rf_calibvals[0])));

	calib = rf_calibvals[idx] << 1;
	if (val & BWI_RFR_BBP_ATTEN_CALIB_BIT)
		calib |= 0x1;
	calib |= 0x20;

	return (calib);
}

int32_t
_bwi_adjust_devide(int32_t num, int32_t den)
{
	if (num < 0)
		return ((num / den));
	else
		return ((num + den / 2) / den);
}

/*
 * http://bcm-specs.sipsolutions.net/TSSI_to_DBM_Table
 * "calculating table entries"
 */
int
bwi_rf_calc_txpower(int8_t *txpwr, uint8_t idx, const int16_t pa_params[])
{
	int32_t m1, m2, f, dbm;
	int i;

	m1 = _bwi_adjust_devide(16 * pa_params[0] + idx * pa_params[1], 32);
	m2 = imax(_bwi_adjust_devide(32768 + idx * pa_params[2], 256), 1);

#define ITER_MAX	16
	f = 256;
	for (i = 0; i < ITER_MAX; ++i) {
		int32_t q, d;

		q = _bwi_adjust_devide(
		    f * 4096 - _bwi_adjust_devide(m2 * f, 16) * f, 2048);
		d = abs(q - f);
		f = q;

		if (d < 2)
			break;
	}
	if (i == ITER_MAX)
		return (EINVAL);
#undef ITER_MAX

	dbm = _bwi_adjust_devide(m1 * f, 8192);
	if (dbm < -127)
		dbm = -127;
	else if (dbm > 128)
		dbm = 128;

	*txpwr = dbm;

	return (0);
}

int
bwi_rf_map_txpower(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t sprom_ofs, val, mask;
	int16_t pa_params[3];
	int error = 0, i, ant_gain, reg_txpower_max;

	/*
	 * Find out max TX power
	 */
	val = bwi_read_sprom(sc, BWI_SPROM_MAX_TXPWR);
	if (phy->phy_mode == IEEE80211_MODE_11A) {
		rf->rf_txpower_max = __SHIFTOUT(val,
		    BWI_SPROM_MAX_TXPWR_MASK_11A);
	} else {
		rf->rf_txpower_max = __SHIFTOUT(val,
		    BWI_SPROM_MAX_TXPWR_MASK_11BG);

		if ((sc->sc_card_flags & BWI_CARD_F_PA_GPIO9) &&
		    phy->phy_mode == IEEE80211_MODE_11G)
			rf->rf_txpower_max -= 3;
	}
	if (rf->rf_txpower_max <= 0) {
		printf("%s: invalid max txpower in sprom\n",
		    sc->sc_dev.dv_xname);
		rf->rf_txpower_max = 74;
	}
	DPRINTF(1, "%s: max txpower from sprom: %d dBm\n",
	    sc->sc_dev.dv_xname, rf->rf_txpower_max);

	/*
	 * Find out region/domain max TX power, which is adjusted
	 * by antenna gain and 1.5 dBm fluctuation as mentioned
	 * in v3 spec.
	 */
	val = bwi_read_sprom(sc, BWI_SPROM_ANT_GAIN);
	if (phy->phy_mode == IEEE80211_MODE_11A)
		ant_gain = __SHIFTOUT(val, BWI_SPROM_ANT_GAIN_MASK_11A);
	else
		ant_gain = __SHIFTOUT(val, BWI_SPROM_ANT_GAIN_MASK_11BG);
	if (ant_gain == 0xff) {
		/* XXX why this always invalid? */
		DPRINTF(1, "%s: invalid antenna gain in sprom\n",
		    sc->sc_dev.dv_xname);
		ant_gain = 2;
	}
	ant_gain *= 4;
	DPRINTF(1, "%s: ant gain %d dBm\n", sc->sc_dev.dv_xname, ant_gain);

	reg_txpower_max = 90 - ant_gain - 6;	/* XXX magic number */
	DPRINTF(1, "%s: region/domain max txpower %d dBm\n",
	    sc->sc_dev.dv_xname, reg_txpower_max);

	/*
	 * Force max TX power within region/domain TX power limit
	 */
	if (rf->rf_txpower_max > reg_txpower_max)
		rf->rf_txpower_max = reg_txpower_max;
	DPRINTF(1, "%s: max txpower %d dBm\n",
	    sc->sc_dev.dv_xname, rf->rf_txpower_max);

	/*
	 * Create TSSI to TX power mapping
	 */

	if (sc->sc_bbp_id == BWI_BBPID_BCM4301 &&
	    rf->rf_type != BWI_RF_T_BCM2050) {
		rf->rf_idle_tssi0 = BWI_DEFAULT_IDLE_TSSI;
		bcopy(bwi_txpower_map_11b, rf->rf_txpower_map0,
		      sizeof(rf->rf_txpower_map0));
		goto back;
	}

#define IS_VALID_PA_PARAM(p)	((p) != 0 && (p) != -1)
	/*
	 * Extract PA parameters
	 */
	if (phy->phy_mode == IEEE80211_MODE_11A)
		sprom_ofs = BWI_SPROM_PA_PARAM_11A;
	else
		sprom_ofs = BWI_SPROM_PA_PARAM_11BG;
	for (i = 0; i < nitems(pa_params); ++i)
		pa_params[i] = (int16_t)bwi_read_sprom(sc, sprom_ofs + (i * 2));

	for (i = 0; i < nitems(pa_params); ++i) {
		/*
		 * If one of the PA parameters from SPROM is not valid,
		 * fall back to the default values, if there are any.
		 */
		if (!IS_VALID_PA_PARAM(pa_params[i])) {
			const int8_t *txpower_map;

			if (phy->phy_mode == IEEE80211_MODE_11A) {
				printf("%s: no tssi2dbm table for 11a PHY\n",
				    sc->sc_dev.dv_xname);
				return (ENXIO);
			}

			if (phy->phy_mode == IEEE80211_MODE_11G) {
				DPRINTF(1, "%s: use default 11g TSSI map\n",
				    sc->sc_dev.dv_xname);
				txpower_map = bwi_txpower_map_11g;
			} else {
				txpower_map = bwi_txpower_map_11b;
			}

			rf->rf_idle_tssi0 = BWI_DEFAULT_IDLE_TSSI;
			bcopy(txpower_map, rf->rf_txpower_map0,
			      sizeof(rf->rf_txpower_map0));
			goto back;
		}
	}

	/*
	 * All of the PA parameters from SPROM are valid.
	 */

	/*
	 * Extract idle TSSI from SPROM.
	 */
	val = bwi_read_sprom(sc, BWI_SPROM_IDLE_TSSI);
	DPRINTF(1, "%s: sprom idle tssi: 0x%04x\n", sc->sc_dev.dv_xname, val);

	if (phy->phy_mode == IEEE80211_MODE_11A)
		mask = BWI_SPROM_IDLE_TSSI_MASK_11A;
	else
		mask = BWI_SPROM_IDLE_TSSI_MASK_11BG;

	rf->rf_idle_tssi0 = (int)__SHIFTOUT(val, mask);
	if (!IS_VALID_PA_PARAM(rf->rf_idle_tssi0))
		rf->rf_idle_tssi0 = 62;

#undef IS_VALID_PA_PARAM

	/*
	 * Calculate TX power map, which is indexed by TSSI
	 */
	DPRINTF(1, "%s: TSSI-TX power map:\n", sc->sc_dev.dv_xname);
	for (i = 0; i < BWI_TSSI_MAX; ++i) {
		error = bwi_rf_calc_txpower(&rf->rf_txpower_map0[i], i,
					    pa_params);
		if (error) {
			printf("%s: bwi_rf_calc_txpower failed\n",
			    sc->sc_dev.dv_xname);
			break;
		}
		if (i != 0 && i % 8 == 0)
			DPRINTF(1, "\n");
		DPRINTF(1, "%d ", rf->rf_txpower_map0[i]);
	}
	DPRINTF(1, "\n");
back:
	DPRINTF(1, "%s: idle tssi0: %d\n",
	    sc->sc_dev.dv_xname, rf->rf_idle_tssi0);

	return (error);
}

void
bwi_rf_lo_update_11g(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_tpctl *tpctl = &mac->mac_tpctl;
	struct rf_saveregs regs;
	uint16_t ant_div, chan_ex;
	uint8_t devi_ctrl;
	uint orig_chan;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	/*
	 * Save RF/PHY registers for later restoration
	 */
	orig_chan = rf->rf_curchan;
	bzero(&regs, sizeof(regs));

	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		SAVE_PHY_REG(mac, &regs, 429);
		SAVE_PHY_REG(mac, &regs, 802);

		PHY_WRITE(mac, 0x429, regs.phy_429 & 0x7fff);
		PHY_WRITE(mac, 0x802, regs.phy_802 & 0xfffc);
	}

	ant_div = CSR_READ_2(sc, BWI_RF_ANTDIV);
	CSR_WRITE_2(sc, BWI_RF_ANTDIV, ant_div | 0x8000);
	chan_ex = CSR_READ_2(sc, BWI_RF_CHAN_EX);

	SAVE_PHY_REG(mac, &regs, 15);
	SAVE_PHY_REG(mac, &regs, 2a);
	SAVE_PHY_REG(mac, &regs, 35);
	SAVE_PHY_REG(mac, &regs, 60);
	SAVE_RF_REG(mac, &regs, 43);
	SAVE_RF_REG(mac, &regs, 7a);
	SAVE_RF_REG(mac, &regs, 52);
	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		SAVE_PHY_REG(mac, &regs, 811);
		SAVE_PHY_REG(mac, &regs, 812);
		SAVE_PHY_REG(mac, &regs, 814);
		SAVE_PHY_REG(mac, &regs, 815);
	}

	/* Force to channel 6 */
	bwi_rf_set_chan(mac, 6, 0);

	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		PHY_WRITE(mac, 0x429, regs.phy_429 & 0x7fff);
		PHY_WRITE(mac, 0x802, regs.phy_802 & 0xfffc);
		bwi_mac_dummy_xmit(mac);
	}
	RF_WRITE(mac, 0x43, 0x6);

	bwi_phy_set_bbp_atten(mac, 2);

	CSR_WRITE_2(sc, BWI_RF_CHAN_EX, 0);

	PHY_WRITE(mac, 0x2e, 0x7f);
	PHY_WRITE(mac, 0x80f, 0x78);
	PHY_WRITE(mac, 0x35, regs.phy_35 & 0xff7f);
	RF_WRITE(mac, 0x7a, regs.rf_7a & 0xfff0);
	PHY_WRITE(mac, 0x2b, 0x203);
	PHY_WRITE(mac, 0x2a, 0x8a3);

	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		PHY_WRITE(mac, 0x814, regs.phy_814 | 0x3);
		PHY_WRITE(mac, 0x815, regs.phy_815 & 0xfffc);
		PHY_WRITE(mac, 0x811, 0x1b3);
		PHY_WRITE(mac, 0x812, 0xb2);
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		tpctl->tp_ctrl2 = bwi_rf_get_tp_ctrl2(mac);
	PHY_WRITE(mac, 0x80f, 0x8078);

	/*
	 * Measure all RF LO
	 */
	devi_ctrl = _bwi_rf_lo_update_11g(mac, regs.rf_7a);

	/*
	 * Restore saved RF/PHY registers
	 */
	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		PHY_WRITE(mac, 0x15, 0xe300);
		PHY_WRITE(mac, 0x812, (devi_ctrl << 8) | 0xa0);
		DELAY(5);
		PHY_WRITE(mac, 0x812, (devi_ctrl << 8) | 0xa2);
		DELAY(2);
		PHY_WRITE(mac, 0x812, (devi_ctrl << 8) | 0xa3);
	} else
		PHY_WRITE(mac, 0x15, devi_ctrl | 0xefa0);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		tpctl = NULL;
	bwi_rf_lo_adjust(mac, tpctl);

	PHY_WRITE(mac, 0x2e, 0x807f);
	if (phy->phy_flags & BWI_PHY_F_LINKED)
		PHY_WRITE(mac, 0x2f, 0x202);
	else
		PHY_WRITE(mac, 0x2f, 0x101);

	CSR_WRITE_2(sc, BWI_RF_CHAN_EX, chan_ex);

	RESTORE_PHY_REG(mac, &regs, 15);
	RESTORE_PHY_REG(mac, &regs, 2a);
	RESTORE_PHY_REG(mac, &regs, 35);
	RESTORE_PHY_REG(mac, &regs, 60);

	RESTORE_RF_REG(mac, &regs, 43);
	RESTORE_RF_REG(mac, &regs, 7a);

	regs.rf_52 &= 0xf0;
	regs.rf_52 |= (RF_READ(mac, 0x52) & 0xf);
	RF_WRITE(mac, 0x52, regs.rf_52);

	CSR_WRITE_2(sc, BWI_RF_ANTDIV, ant_div);

	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		RESTORE_PHY_REG(mac, &regs, 811);
		RESTORE_PHY_REG(mac, &regs, 812);
		RESTORE_PHY_REG(mac, &regs, 814);
		RESTORE_PHY_REG(mac, &regs, 815);
		RESTORE_PHY_REG(mac, &regs, 429);
		RESTORE_PHY_REG(mac, &regs, 802);
	}

	bwi_rf_set_chan(mac, orig_chan, 1);
}

uint32_t
bwi_rf_lo_devi_measure(struct bwi_mac *mac, uint16_t ctrl)
{
	struct bwi_phy *phy = &mac->mac_phy;
	uint32_t devi = 0;
	int i;

	if (phy->phy_flags & BWI_PHY_F_LINKED)
		ctrl <<= 8;

	for (i = 0; i < 8; ++i) {
		if (phy->phy_flags & BWI_PHY_F_LINKED) {
			PHY_WRITE(mac, 0x15, 0xe300);
			PHY_WRITE(mac, 0x812, ctrl | 0xb0);
			DELAY(5);
			PHY_WRITE(mac, 0x812, ctrl | 0xb2);
			DELAY(2);
			PHY_WRITE(mac, 0x812, ctrl | 0xb3);
			DELAY(4);
			PHY_WRITE(mac, 0x15, 0xf300);
		} else {
			PHY_WRITE(mac, 0x15, ctrl | 0xefa0);
			DELAY(2);
			PHY_WRITE(mac, 0x15, ctrl | 0xefe0);
			DELAY(4);
			PHY_WRITE(mac, 0x15, ctrl | 0xffe0);
		}
		DELAY(8);
		devi += PHY_READ(mac, 0x2d);
	}

	return (devi);
}

uint16_t
bwi_rf_get_tp_ctrl2(struct bwi_mac *mac)
{
	uint32_t devi_min;
	uint16_t tp_ctrl2 = 0;
	int i;

	RF_WRITE(mac, 0x52, 0);
	DELAY(10);
	devi_min = bwi_rf_lo_devi_measure(mac, 0);

	for (i = 0; i < 16; ++i) {
		uint32_t devi;

		RF_WRITE(mac, 0x52, i);
		DELAY(10);
		devi = bwi_rf_lo_devi_measure(mac, 0);

		if (devi < devi_min) {
			devi_min = devi;
			tp_ctrl2 = i;
		}
	}

	return (tp_ctrl2);
}

uint8_t
_bwi_rf_lo_update_11g(struct bwi_mac *mac, uint16_t orig_rf7a)
{
#define RF_ATTEN_LISTSZ	14
#define BBP_ATTEN_MAX	4	/* half */
	struct ifnet *ifp = &mac->mac_sc->sc_ic.ic_if;
	struct bwi_rf_lo lo_save, *lo;
	uint8_t devi_ctrl = 0;
	int idx, adj_rf7a = 0;

	static const int rf_atten_list[RF_ATTEN_LISTSZ] =
	    { 3, 1, 5, 7, 9, 2, 0, 4, 6, 8, 1, 2, 3, 4 };
	static const int rf_atten_init_list[RF_ATTEN_LISTSZ] =
            { 0, 3, 1, 5, 7, 3, 2, 0, 4, 6, -1, -1, -1, -1 };
	static const int rf_lo_measure_order[RF_ATTEN_LISTSZ] =
	    { 3, 1, 5, 7, 9, 2, 0, 4, 6, 8, 10, 11, 12, 13 };

	bzero(&lo_save, sizeof(lo_save));
	for (idx = 0; idx < RF_ATTEN_LISTSZ; ++idx) {
		int init_rf_atten = rf_atten_init_list[idx];
		int rf_atten = rf_atten_list[idx];
		int bbp_atten;

		for (bbp_atten = 0; bbp_atten < BBP_ATTEN_MAX; ++bbp_atten) {
			uint16_t tp_ctrl2, rf7a;

			if ((ifp->if_flags & IFF_RUNNING) == 0) {
				if (idx == 0) {
					bzero(&lo_save, sizeof(lo_save));
				} else if (init_rf_atten < 0) {
					lo = bwi_get_rf_lo(mac,
					    rf_atten, 2 * bbp_atten);
					bcopy(lo, &lo_save, sizeof(lo_save));
				} else {
					lo = bwi_get_rf_lo(mac,
					    init_rf_atten, 0);
					bcopy(lo, &lo_save, sizeof(lo_save));
				}

				devi_ctrl = 0;
				adj_rf7a = 0;

				/*
				 * XXX
				 * Linux driver overflows 'val'
				 */
				if (init_rf_atten >= 0) {
					int val;

					val = rf_atten * 2 + bbp_atten;
					if (val > 14) {
						adj_rf7a = 1;
						if (val > 17)
							devi_ctrl = 1;
						if (val > 19)
							devi_ctrl = 2;
					}
				}
			} else {
				lo = bwi_get_rf_lo(mac,
					rf_atten, 2 * bbp_atten);
				if (!bwi_rf_lo_isused(mac, lo))
					continue;
				bcopy(lo, &lo_save, sizeof(lo_save));

				devi_ctrl = 3;
				adj_rf7a = 0;
			}

			RF_WRITE(mac, BWI_RFR_ATTEN, rf_atten);

			tp_ctrl2 = mac->mac_tpctl.tp_ctrl2;
			if (init_rf_atten < 0)
				tp_ctrl2 |= (3 << 4);
			RF_WRITE(mac, BWI_RFR_TXPWR, tp_ctrl2);

			DELAY(10);

			bwi_phy_set_bbp_atten(mac, bbp_atten * 2);

			rf7a = orig_rf7a & 0xfff0;
			if (adj_rf7a)
				rf7a |= 0x8;
			RF_WRITE(mac, 0x7a, rf7a);

			lo = bwi_get_rf_lo(mac,
				rf_lo_measure_order[idx], bbp_atten * 2);
			bwi_rf_lo_measure_11g(mac, &lo_save, lo, devi_ctrl);
		}
	}

	return (devi_ctrl);

#undef RF_ATTEN_LISTSZ
#undef BBP_ATTEN_MAX
}

void
bwi_rf_lo_measure_11g(struct bwi_mac *mac, const struct bwi_rf_lo *src_lo,
    struct bwi_rf_lo *dst_lo, uint8_t devi_ctrl)
{
#define LO_ADJUST_MIN	1
#define LO_ADJUST_MAX	8
#define LO_ADJUST(hi, lo)	{ .ctrl_hi = hi, .ctrl_lo = lo }
	static const struct bwi_rf_lo rf_lo_adjust[LO_ADJUST_MAX] = {
		LO_ADJUST(1,	1),
		LO_ADJUST(1,	0),
		LO_ADJUST(1,	-1),
		LO_ADJUST(0,	-1),
		LO_ADJUST(-1,	-1),
		LO_ADJUST(-1,	0),
		LO_ADJUST(-1,	1),
		LO_ADJUST(0,	1)
	};
#undef LO_ADJUST

	struct bwi_rf_lo lo_min;
	uint32_t devi_min;
	int found, loop_count, adjust_state;

	bcopy(src_lo, &lo_min, sizeof(lo_min));
	RF_LO_WRITE(mac, &lo_min);
	devi_min = bwi_rf_lo_devi_measure(mac, devi_ctrl);

	loop_count = 12;	/* XXX */
	adjust_state = 0;
	do {
		struct bwi_rf_lo lo_base;
		int i, fin;

		found = 0;
		if (adjust_state == 0) {
			i = LO_ADJUST_MIN;
			fin = LO_ADJUST_MAX;
		} else if (adjust_state % 2 == 0) {
			i = adjust_state - 1;
			fin = adjust_state + 1;
		} else {
			i = adjust_state - 2;
			fin = adjust_state + 2;
		}

		if (i < LO_ADJUST_MIN)
			i += LO_ADJUST_MAX;
		KASSERT(i <= LO_ADJUST_MAX && i >= LO_ADJUST_MIN);

		if (fin > LO_ADJUST_MAX)
			fin -= LO_ADJUST_MAX;
		KASSERT(fin <= LO_ADJUST_MAX && fin >= LO_ADJUST_MIN);

		bcopy(&lo_min, &lo_base, sizeof(lo_base));
		for (;;) {
			struct bwi_rf_lo lo;

			lo.ctrl_hi = lo_base.ctrl_hi +
				rf_lo_adjust[i - 1].ctrl_hi;
			lo.ctrl_lo = lo_base.ctrl_lo +
				rf_lo_adjust[i - 1].ctrl_lo;

			if (abs(lo.ctrl_lo) < 9 && abs(lo.ctrl_hi) < 9) {
				uint32_t devi;

				RF_LO_WRITE(mac, &lo);
				devi = bwi_rf_lo_devi_measure(mac, devi_ctrl);
				if (devi < devi_min) {
					devi_min = devi;
					adjust_state = i;
					found = 1;
					bcopy(&lo, &lo_min, sizeof(lo_min));
				}
			}
			if (i == fin)
				break;
			if (i == LO_ADJUST_MAX)
				i = LO_ADJUST_MIN;
			else
				++i;
		}
	} while (loop_count-- && found);

	bcopy(&lo_min, dst_lo, sizeof(*dst_lo));

#undef LO_ADJUST_MIN
#undef LO_ADJUST_MAX
}

void
bwi_rf_calc_nrssi_slope_11b(struct bwi_mac *mac)
{
#define SAVE_RF_MAX	3
#define SAVE_PHY_MAX	8
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t save_phy[SAVE_PHY_MAX];
	uint16_t ant_div, bbp_atten, chan_ex;
	int16_t nrssi[2];
	int i;

	static const uint16_t save_rf_regs[SAVE_RF_MAX] =
	    { 0x7a, 0x52, 0x43 };
	static const uint16_t save_phy_regs[SAVE_PHY_MAX] =
	    { 0x30, 0x26, 0x15, 0x2a, 0x20, 0x5a, 0x59, 0x58 };

	/*
	 * Save RF/PHY registers for later restoration
	 */
	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = RF_READ(mac, save_rf_regs[i]);
	for (i = 0; i < SAVE_PHY_MAX; ++i)
		save_phy[i] = PHY_READ(mac, save_phy_regs[i]);

	ant_div = CSR_READ_2(sc, BWI_RF_ANTDIV);
	bbp_atten = CSR_READ_2(sc, BWI_BBP_ATTEN);
	chan_ex = CSR_READ_2(sc, BWI_RF_CHAN_EX);

	/*
	 * Calculate nrssi0
	 */
	if (phy->phy_rev >= 5)
		RF_CLRBITS(mac, 0x7a, 0xff80);
	else
		RF_CLRBITS(mac, 0x7a, 0xfff0);
	PHY_WRITE(mac, 0x30, 0xff);

	CSR_WRITE_2(sc, BWI_BPHY_CTRL, 0x7f7f);

	PHY_WRITE(mac, 0x26, 0);
	PHY_SETBITS(mac, 0x15, 0x20);
	PHY_WRITE(mac, 0x2a, 0x8a3);
	RF_SETBITS(mac, 0x7a, 0x80);

	nrssi[0] = (int16_t)PHY_READ(mac, 0x27);

	/*
	 * Calculate nrssi1
	 */
	RF_CLRBITS(mac, 0x7a, 0xff80);
	if (phy->phy_version >= 2)
		CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0x40);
	else if (phy->phy_version == 0)
		CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0x122);
	else
		CSR_CLRBITS_2(sc, BWI_RF_CHAN_EX, 0xdfff);

	PHY_WRITE(mac, 0x20, 0x3f3f);
	PHY_WRITE(mac, 0x15, 0xf330);

	RF_WRITE(mac, 0x5a, 0x60);
	RF_CLRBITS(mac, 0x43, 0xff0f);

	PHY_WRITE(mac, 0x5a, 0x480);
	PHY_WRITE(mac, 0x59, 0x810);
	PHY_WRITE(mac, 0x58, 0xd);

	DELAY(20);

	nrssi[1] = (int16_t)PHY_READ(mac, 0x27);

	/*
	 * Restore saved RF/PHY registers
	 */
	PHY_WRITE(mac, save_phy_regs[0], save_phy[0]);
	RF_WRITE(mac, save_rf_regs[0], save_rf[0]);

	CSR_WRITE_2(sc, BWI_RF_ANTDIV, ant_div);

	for (i = 1; i < 4; ++i)
		PHY_WRITE(mac, save_phy_regs[i], save_phy[i]);

	bwi_rf_workaround(mac, rf->rf_curchan);

	if (phy->phy_version != 0)
		CSR_WRITE_2(sc, BWI_RF_CHAN_EX, chan_ex);

	for (; i < SAVE_PHY_MAX; ++i)
		PHY_WRITE(mac, save_phy_regs[i], save_phy[i]);

	for (i = 1; i < SAVE_RF_MAX; ++i)
		RF_WRITE(mac, save_rf_regs[i], save_rf[i]);

	/*
	 * Install calculated narrow RSSI values
	 */
	if (nrssi[0] == nrssi[1])
		rf->rf_nrssi_slope = 0x10000;
	else
		rf->rf_nrssi_slope = 0x400000 / (nrssi[0] - nrssi[1]);
	if (nrssi[0] <= -4) {
		rf->rf_nrssi[0] = nrssi[0];
		rf->rf_nrssi[1] = nrssi[1];
	}

#undef SAVE_RF_MAX
#undef SAVE_PHY_MAX
}

void
bwi_rf_set_nrssi_ofs_11g(struct bwi_mac *mac)
{
#define SAVE_RF_MAX		2
#define SAVE_PHY_COMM_MAX	10
#define SAVE_PHY6_MAX		8
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t save_phy_comm[SAVE_PHY_COMM_MAX];
	uint16_t save_phy6[SAVE_PHY6_MAX];
	uint16_t rf7b = 0xffff;
	int16_t nrssi;
	int i, phy6_idx = 0;

	static const uint16_t save_rf_regs[SAVE_RF_MAX] = { 0x7a, 0x43 };
	static const uint16_t save_phy_comm_regs[SAVE_PHY_COMM_MAX] = {
		0x0001, 0x0811, 0x0812, 0x0814,
		0x0815, 0x005a, 0x0059, 0x0058,
		0x000a, 0x0003
	};
	static const uint16_t save_phy6_regs[SAVE_PHY6_MAX] = {
		0x002e, 0x002f, 0x080f, 0x0810,
		0x0801, 0x0060, 0x0014, 0x0478
	};

	for (i = 0; i < SAVE_PHY_COMM_MAX; ++i)
		save_phy_comm[i] = PHY_READ(mac, save_phy_comm_regs[i]);
	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = RF_READ(mac, save_rf_regs[i]);

	PHY_CLRBITS(mac, 0x429, 0x8000);
	PHY_FILT_SETBITS(mac, 0x1, 0x3fff, 0x4000);
	PHY_SETBITS(mac, 0x811, 0xc);
	PHY_FILT_SETBITS(mac, 0x812, 0xfff3, 0x4);
	PHY_CLRBITS(mac, 0x802, 0x3);

	if (phy->phy_rev >= 6) {
		for (i = 0; i < SAVE_PHY6_MAX; ++i)
			save_phy6[i] = PHY_READ(mac, save_phy6_regs[i]);

		PHY_WRITE(mac, 0x2e, 0);
		PHY_WRITE(mac, 0x2f, 0);
		PHY_WRITE(mac, 0x80f, 0);
		PHY_WRITE(mac, 0x810, 0);
		PHY_SETBITS(mac, 0x478, 0x100);
		PHY_SETBITS(mac, 0x801, 0x40);
		PHY_SETBITS(mac, 0x60, 0x40);
		PHY_SETBITS(mac, 0x14, 0x200);
	}

	RF_SETBITS(mac, 0x7a, 0x70);
	RF_SETBITS(mac, 0x7a, 0x80);

	DELAY(30);

	nrssi = bwi_nrssi_11g(mac);
	if (nrssi == 31) {
		for (i = 7; i >= 4; --i) {
			RF_WRITE(mac, 0x7b, i);
			DELAY(20);
			nrssi = bwi_nrssi_11g(mac);
			if (nrssi < 31 && rf7b == 0xffff)
				rf7b = i;
		}
		if (rf7b == 0xffff)
			rf7b = 4;
	} else {
		struct bwi_gains gains;

		RF_CLRBITS(mac, 0x7a, 0xff80);

		PHY_SETBITS(mac, 0x814, 0x1);
		PHY_CLRBITS(mac, 0x815, 0x1);
		PHY_SETBITS(mac, 0x811, 0xc);
		PHY_SETBITS(mac, 0x812, 0xc);
		PHY_SETBITS(mac, 0x811, 0x30);
		PHY_SETBITS(mac, 0x812, 0x30);
		PHY_WRITE(mac, 0x5a, 0x480);
		PHY_WRITE(mac, 0x59, 0x810);
		PHY_WRITE(mac, 0x58, 0xd);
		if (phy->phy_version == 0)
			PHY_WRITE(mac, 0x3, 0x122);
		else
			PHY_SETBITS(mac, 0xa, 0x2000);
		PHY_SETBITS(mac, 0x814, 0x4);
		PHY_CLRBITS(mac, 0x815, 0x4);
		PHY_FILT_SETBITS(mac, 0x3, 0xff9f, 0x40);
		RF_SETBITS(mac, 0x7a, 0xf);

		bzero(&gains, sizeof(gains));
		gains.tbl_gain1 = 3;
		gains.tbl_gain2 = 0;
		gains.phy_gain = 1;
		bwi_set_gains(mac, &gains);

		RF_FILT_SETBITS(mac, 0x43, 0xf0, 0xf);
		DELAY(30);

		nrssi = bwi_nrssi_11g(mac);
		if (nrssi == -32) {
			for (i = 0; i < 4; ++i) {
				RF_WRITE(mac, 0x7b, i);
				DELAY(20);
				nrssi = bwi_nrssi_11g(mac);
				if (nrssi > -31 && rf7b == 0xffff)
					rf7b = i;
			}
			if (rf7b == 0xffff)
				rf7b = 3;
		} else {
			rf7b = 0;
		}
	}
	RF_WRITE(mac, 0x7b, rf7b);

	/*
	 * Restore saved RF/PHY registers
	 */
	if (phy->phy_rev >= 6) {
		for (phy6_idx = 0; phy6_idx < 4; ++phy6_idx) {
			PHY_WRITE(mac, save_phy6_regs[phy6_idx],
			    save_phy6[phy6_idx]);
		}
	}

	/* Saved PHY registers 0, 1, 2 are handled later */
	for (i = 3; i < SAVE_PHY_COMM_MAX; ++i)
		PHY_WRITE(mac, save_phy_comm_regs[i], save_phy_comm[i]);

	for (i = SAVE_RF_MAX - 1; i >= 0; --i)
		RF_WRITE(mac, save_rf_regs[i], save_rf[i]);

	PHY_SETBITS(mac, 0x802, 0x3);
	PHY_SETBITS(mac, 0x429, 0x8000);

	bwi_set_gains(mac, NULL);

	if (phy->phy_rev >= 6) {
		for (; phy6_idx < SAVE_PHY6_MAX; ++phy6_idx) {
			PHY_WRITE(mac, save_phy6_regs[phy6_idx],
			    save_phy6[phy6_idx]);
		}
	}

	PHY_WRITE(mac, save_phy_comm_regs[0], save_phy_comm[0]);
	PHY_WRITE(mac, save_phy_comm_regs[2], save_phy_comm[2]);
	PHY_WRITE(mac, save_phy_comm_regs[1], save_phy_comm[1]);

#undef SAVE_RF_MAX
#undef SAVE_PHY_COMM_MAX
#undef SAVE_PHY6_MAX
}

void
bwi_rf_calc_nrssi_slope_11g(struct bwi_mac *mac)
{
#define SAVE_RF_MAX		3
#define SAVE_PHY_COMM_MAX	4
#define SAVE_PHY3_MAX		8
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t save_phy_comm[SAVE_PHY_COMM_MAX];
	uint16_t save_phy3[SAVE_PHY3_MAX];
	uint16_t ant_div, bbp_atten, chan_ex;
	struct bwi_gains gains;
	int16_t nrssi[2];
	int i, phy3_idx = 0;

	static const uint16_t save_rf_regs[SAVE_RF_MAX] =
	    { 0x7a, 0x52, 0x43 };
	static const uint16_t save_phy_comm_regs[SAVE_PHY_COMM_MAX] =
	    { 0x15, 0x5a, 0x59, 0x58 };
	static const uint16_t save_phy3_regs[SAVE_PHY3_MAX] = {
		0x002e, 0x002f, 0x080f, 0x0810,
		0x0801, 0x0060, 0x0014, 0x0478
	};

	if (rf->rf_rev >= 9)
		return;
	else if (rf->rf_rev == 8)
		bwi_rf_set_nrssi_ofs_11g(mac);

	PHY_CLRBITS(mac, 0x429, 0x8000);
	PHY_CLRBITS(mac, 0x802, 0x3);

	/*
	 * Save RF/PHY registers for later restoration
	 */
	ant_div = CSR_READ_2(sc, BWI_RF_ANTDIV);
	CSR_SETBITS_2(sc, BWI_RF_ANTDIV, 0x8000);

	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = RF_READ(mac, save_rf_regs[i]);
	for (i = 0; i < SAVE_PHY_COMM_MAX; ++i)
		save_phy_comm[i] = PHY_READ(mac, save_phy_comm_regs[i]);

	bbp_atten = CSR_READ_2(sc, BWI_BBP_ATTEN);
	chan_ex = CSR_READ_2(sc, BWI_RF_CHAN_EX);

	if (phy->phy_rev >= 3) {
		for (i = 0; i < SAVE_PHY3_MAX; ++i)
			save_phy3[i] = PHY_READ(mac, save_phy3_regs[i]);

		PHY_WRITE(mac, 0x2e, 0);
		PHY_WRITE(mac, 0x810, 0);

		if (phy->phy_rev == 4 || phy->phy_rev == 6 ||
		    phy->phy_rev == 7) {
			PHY_SETBITS(mac, 0x478, 0x100);
			PHY_SETBITS(mac, 0x810, 0x40);
		} else if (phy->phy_rev == 3 || phy->phy_rev == 5)
			PHY_CLRBITS(mac, 0x810, 0x40);

		PHY_SETBITS(mac, 0x60, 0x40);
		PHY_SETBITS(mac, 0x14, 0x200);
	}

	/*
	 * Calculate nrssi0
	 */
	RF_SETBITS(mac, 0x7a, 0x70);

	bzero(&gains, sizeof(gains));
	gains.tbl_gain1 = 0;
	gains.tbl_gain2 = 8;
	gains.phy_gain = 0;
	bwi_set_gains(mac, &gains);

	RF_CLRBITS(mac, 0x7a, 0xff08);
	if (phy->phy_rev >= 2) {
		PHY_FILT_SETBITS(mac, 0x811, 0xffcf, 0x30);
		PHY_FILT_SETBITS(mac, 0x812, 0xffcf, 0x10);
	}

	RF_SETBITS(mac, 0x7a, 0x80);
	DELAY(20);
	nrssi[0] = bwi_nrssi_11g(mac);

	/*
	 * Calculate nrssi1
	 */
	RF_CLRBITS(mac, 0x7a, 0xff80);
	if (phy->phy_version >= 2)
		PHY_FILT_SETBITS(mac, 0x3, 0xff9f, 0x40);
	CSR_SETBITS_2(sc, BWI_RF_CHAN_EX, 0x2000);

	RF_SETBITS(mac, 0x7a, 0xf);
	PHY_WRITE(mac, 0x15, 0xf330);
	if (phy->phy_rev >= 2) {
		PHY_FILT_SETBITS(mac, 0x812, 0xffcf, 0x20);
		PHY_FILT_SETBITS(mac, 0x811, 0xffcf, 0x20);
	}

	bzero(&gains, sizeof(gains));
	gains.tbl_gain1 = 3;
	gains.tbl_gain2 = 0;
	gains.phy_gain = 1;
	bwi_set_gains(mac, &gains);

	if (rf->rf_rev == 8) {
		RF_WRITE(mac, 0x43, 0x1f);
	} else {
		RF_FILT_SETBITS(mac, 0x52, 0xff0f, 0x60);
		RF_FILT_SETBITS(mac, 0x43, 0xfff0, 0x9);
	}
	PHY_WRITE(mac, 0x5a, 0x480);
	PHY_WRITE(mac, 0x59, 0x810);
	PHY_WRITE(mac, 0x58, 0xd);
	DELAY(20);

	nrssi[1] = bwi_nrssi_11g(mac);

	/*
	 * Install calculated narrow RSSI values
	 */
	if (nrssi[1] == nrssi[0])
		rf->rf_nrssi_slope = 0x10000;
	else
		rf->rf_nrssi_slope = 0x400000 / (nrssi[0] - nrssi[1]);
	if (nrssi[0] >= -4) {
		rf->rf_nrssi[0] = nrssi[1];
		rf->rf_nrssi[1] = nrssi[0];
	}

	/*
	 * Restore saved RF/PHY registers
	 */
	if (phy->phy_rev >= 3) {
		for (phy3_idx = 0; phy3_idx < 4; ++phy3_idx) {
			PHY_WRITE(mac, save_phy3_regs[phy3_idx],
				  save_phy3[phy3_idx]);
		}
	}
	if (phy->phy_rev >= 2) {
		PHY_CLRBITS(mac, 0x812, 0x30);
		PHY_CLRBITS(mac, 0x811, 0x30);
	}

	for (i = 0; i < SAVE_RF_MAX; ++i)
		RF_WRITE(mac, save_rf_regs[i], save_rf[i]);

	CSR_WRITE_2(sc, BWI_RF_ANTDIV, ant_div);
	CSR_WRITE_2(sc, BWI_BBP_ATTEN, bbp_atten);
	CSR_WRITE_2(sc, BWI_RF_CHAN_EX, chan_ex);

	for (i = 0; i < SAVE_PHY_COMM_MAX; ++i)
		PHY_WRITE(mac, save_phy_comm_regs[i], save_phy_comm[i]);

	bwi_rf_workaround(mac, rf->rf_curchan);
	PHY_SETBITS(mac, 0x802, 0x3);
	bwi_set_gains(mac, NULL);
	PHY_SETBITS(mac, 0x429, 0x8000);

	if (phy->phy_rev >= 3) {
		for (; phy3_idx < SAVE_PHY3_MAX; ++phy3_idx) {
			PHY_WRITE(mac, save_phy3_regs[phy3_idx],
			    save_phy3[phy3_idx]);
		}
	}

	bwi_rf_init_sw_nrssi_table(mac);
	bwi_rf_set_nrssi_thr_11g(mac);

#undef SAVE_RF_MAX
#undef SAVE_PHY_COMM_MAX
#undef SAVE_PHY3_MAX
}

void
bwi_rf_init_sw_nrssi_table(struct bwi_mac *mac)
{
	struct bwi_rf *rf = &mac->mac_rf;
	int d, i;

	d = 0x1f - rf->rf_nrssi[0];
	for (i = 0; i < BWI_NRSSI_TBLSZ; ++i) {
		int val;

		val = (((i - d) * rf->rf_nrssi_slope) / 0x10000) + 0x3a;
		if (val < 0)
			val = 0;
		else if (val > 0x3f)
			val = 0x3f;

		rf->rf_nrssi_table[i] = val;
	}
}

void
bwi_rf_init_hw_nrssi_table(struct bwi_mac *mac, uint16_t adjust)
{
	int i;

	for (i = 0; i < BWI_NRSSI_TBLSZ; ++i) {
		int16_t val;

		val = bwi_nrssi_read(mac, i);

		val -= adjust;
		if (val < -32)
			val = -32;
		else if (val > 31)
			val = 31;

		bwi_nrssi_write(mac, i, val);
	}
}

void
bwi_rf_set_nrssi_thr_11b(struct bwi_mac *mac)
{
	struct bwi_rf *rf = &mac->mac_rf;
	int32_t thr;

	if (rf->rf_type != BWI_RF_T_BCM2050 ||
	    (mac->mac_sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) == 0)
		return;

	/*
	 * Calculate nrssi threshold
	 */
	if (rf->rf_rev >= 6) {
		thr = (rf->rf_nrssi[1] - rf->rf_nrssi[0]) * 32;
		thr += 20 * (rf->rf_nrssi[0] + 1);
		thr /= 40;
	} else {
		thr = rf->rf_nrssi[1] - 5;
	}
	if (thr < 0)
		thr = 0;
	else if (thr > 0x3e)
		thr = 0x3e;

	PHY_READ(mac, BWI_PHYR_NRSSI_THR_11B);	/* dummy read */
	PHY_WRITE(mac, BWI_PHYR_NRSSI_THR_11B, (((uint16_t)thr) << 8) | 0x1c);

	if (rf->rf_rev >= 6) {
		PHY_WRITE(mac, 0x87, 0xe0d);
		PHY_WRITE(mac, 0x86, 0xc0b);
		PHY_WRITE(mac, 0x85, 0xa09);
		PHY_WRITE(mac, 0x84, 0x808);
		PHY_WRITE(mac, 0x83, 0x808);
		PHY_WRITE(mac, 0x82, 0x604);
		PHY_WRITE(mac, 0x81, 0x302);
		PHY_WRITE(mac, 0x80, 0x100);
	}
}

int32_t
_nrssi_threshold(const struct bwi_rf *rf, int32_t val)
{
	val *= (rf->rf_nrssi[1] - rf->rf_nrssi[0]);
	val += (rf->rf_nrssi[0] << 6);
	if (val < 32)
		val += 31;
	else
		val += 32;
	val >>= 6;
	if (val < -31)
		val = -31;
	else if (val > 31)
		val = 31;

	return (val);
}

void
bwi_rf_set_nrssi_thr_11g(struct bwi_mac *mac)
{
	int32_t thr1, thr2;
	uint16_t thr;

	/*
	 * Find the two nrssi thresholds
	 */
	if ((mac->mac_phy.phy_flags & BWI_PHY_F_LINKED) == 0 ||
	    (mac->mac_sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) == 0) {
	    	int16_t nrssi;

		nrssi = bwi_nrssi_read(mac, 0x20);
		if (nrssi >= 32)
			nrssi -= 64;

		if (nrssi < 3) {
			thr1 = 0x2b;
			thr2 = 0x27;
		} else {
			thr1 = 0x2d;
			thr2 = 0x2b;
		}
	} else {
		/* TODO Interfere mode */
		thr1 = _nrssi_threshold(&mac->mac_rf, 0x11);
		thr2 = _nrssi_threshold(&mac->mac_rf, 0xe);
	}

#define NRSSI_THR1_MASK		0x003f
#define NRSSI_THR2_MASK		0x0fc0
	thr = __SHIFTIN((uint32_t)thr1, NRSSI_THR1_MASK) |
	    __SHIFTIN((uint32_t)thr2, NRSSI_THR2_MASK);
	PHY_FILT_SETBITS(mac, BWI_PHYR_NRSSI_THR_11G, 0xf000, thr);
#undef NRSSI_THR1_MASK
#undef NRSSI_THR2_MASK
}

void
bwi_rf_clear_tssi(struct bwi_mac *mac)
{
	/* XXX use function pointer */
	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11A) {
		/* TODO: 11A */
	} else {
		uint16_t val;
		int i;

		val = __SHIFTIN(BWI_INVALID_TSSI, BWI_LO_TSSI_MASK) |
		    __SHIFTIN(BWI_INVALID_TSSI, BWI_HI_TSSI_MASK);

		for (i = 0; i < 2; ++i) {
			MOBJ_WRITE_2(mac, BWI_COMM_MOBJ,
			    BWI_COMM_MOBJ_TSSI_DS + (i * 2), val);
		}

		for (i = 0; i < 2; ++i) {
			MOBJ_WRITE_2(mac, BWI_COMM_MOBJ,
			    BWI_COMM_MOBJ_TSSI_OFDM + (i * 2), val);
		}
	}
}

void
bwi_rf_clear_state(struct bwi_rf *rf)
{
	int i;

	rf->rf_flags &= ~BWI_RF_CLEAR_FLAGS;
	bzero(rf->rf_lo, sizeof(rf->rf_lo));
	bzero(rf->rf_lo_used, sizeof(rf->rf_lo_used));

	rf->rf_nrssi_slope = 0;
	rf->rf_nrssi[0] = BWI_INVALID_NRSSI;
	rf->rf_nrssi[1] = BWI_INVALID_NRSSI;

	for (i = 0; i < BWI_NRSSI_TBLSZ; ++i)
		rf->rf_nrssi_table[i] = i;

	rf->rf_lo_gain = 0;
	rf->rf_rx_gain = 0;

	bcopy(rf->rf_txpower_map0, rf->rf_txpower_map,
	      sizeof(rf->rf_txpower_map));
	rf->rf_idle_tssi = rf->rf_idle_tssi0;
}

void
bwi_rf_on_11a(struct bwi_mac *mac)
{
	/* TODO: 11A */
}

void
bwi_rf_on_11bg(struct bwi_mac *mac)
{
	struct bwi_phy *phy = &mac->mac_phy;

	PHY_WRITE(mac, 0x15, 0x8000);
	PHY_WRITE(mac, 0x15, 0xcc00);
	if (phy->phy_flags & BWI_PHY_F_LINKED)
		PHY_WRITE(mac, 0x15, 0xc0);
	else
		PHY_WRITE(mac, 0x15, 0);

	bwi_rf_set_chan(mac, 6 /* XXX */, 1);
}

void
bwi_rf_set_ant_mode(struct bwi_mac *mac, int ant_mode)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t val;

	KASSERT(ant_mode == BWI_ANT_MODE_0 ||
	    ant_mode == BWI_ANT_MODE_1 ||
	    ant_mode == BWI_ANT_MODE_AUTO);

	HFLAGS_CLRBITS(mac, BWI_HFLAG_AUTO_ANTDIV);

	if (phy->phy_mode == IEEE80211_MODE_11B) {
		/* NOTE: v4/v3 conflicts, take v3 */
		if (mac->mac_rev == 2)
			val = BWI_ANT_MODE_AUTO;
		else
			val = ant_mode;
		val <<= 7;
		PHY_FILT_SETBITS(mac, 0x3e2, 0xfe7f, val);
	} else {	/* 11a/g */
		/* XXX reg/value naming */
		val = ant_mode << 7;
		PHY_FILT_SETBITS(mac, 0x401, 0x7e7f, val);

		if (ant_mode == BWI_ANT_MODE_AUTO)
			PHY_CLRBITS(mac, 0x42b, 0x100);

		if (phy->phy_mode == IEEE80211_MODE_11A) {
			/* TODO: 11A */
		} else {	/* 11g */
			if (ant_mode == BWI_ANT_MODE_AUTO)
				PHY_SETBITS(mac, 0x48c, 0x2000);
			else
				PHY_CLRBITS(mac, 0x48c, 0x2000);

			if (phy->phy_rev >= 2) {
				PHY_SETBITS(mac, 0x461, 0x10);
				PHY_FILT_SETBITS(mac, 0x4ad, 0xff00, 0x15);
				if (phy->phy_rev == 2) {
					PHY_WRITE(mac, 0x427, 0x8);
				} else {
					PHY_FILT_SETBITS(mac, 0x427,
							 0xff00, 0x8);
				}

				if (phy->phy_rev >= 6)
					PHY_WRITE(mac, 0x49b, 0xdc);
			}
		}
	}

	/* XXX v4 set AUTO_ANTDIV unconditionally */
	if (ant_mode == BWI_ANT_MODE_AUTO)
		HFLAGS_SETBITS(mac, BWI_HFLAG_AUTO_ANTDIV);

	val = ant_mode << 8;
	MOBJ_FILT_SETBITS_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_TX_BEACON,
	    0xfc3f, val);
	MOBJ_FILT_SETBITS_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_TX_ACK,
	    0xfc3f, val);
	MOBJ_FILT_SETBITS_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_TX_PROBE_RESP,
	    0xfc3f, val);

	/* XXX what's these */
	if (phy->phy_mode == IEEE80211_MODE_11B)
		CSR_SETBITS_2(sc, 0x5e, 0x4);

	CSR_WRITE_4(sc, 0x100, 0x1000000);
	if (mac->mac_rev < 5)
		CSR_WRITE_4(sc, 0x10c, 0x1000000);

	mac->mac_rf.rf_ant_mode = ant_mode;
}

int
bwi_rf_get_latest_tssi(struct bwi_mac *mac, int8_t tssi[], uint16_t ofs)
{
	int i;

	for (i = 0; i < 4; ) {
		uint16_t val;

		val = MOBJ_READ_2(mac, BWI_COMM_MOBJ, ofs + i);
		tssi[i++] = (int8_t)__SHIFTOUT(val, BWI_LO_TSSI_MASK);
		tssi[i++] = (int8_t)__SHIFTOUT(val, BWI_HI_TSSI_MASK);
	}

	for (i = 0; i < 4; ++i) {
		if (tssi[i] == BWI_INVALID_TSSI)
			return (EINVAL);
	}

	return (0);
}

int
bwi_rf_tssi2dbm(struct bwi_mac *mac, int8_t tssi, int8_t *txpwr)
{
	struct bwi_rf *rf = &mac->mac_rf;
	int pwr_idx;

	pwr_idx = rf->rf_idle_tssi + (int)tssi - rf->rf_base_tssi;
#if 0
	if (pwr_idx < 0 || pwr_idx >= BWI_TSSI_MAX)
		return EINVAL;
#else
	if (pwr_idx < 0)
		pwr_idx = 0;
	else if (pwr_idx >= BWI_TSSI_MAX)
		pwr_idx = BWI_TSSI_MAX - 1;
#endif
	*txpwr = rf->rf_txpower_map[pwr_idx];

	return (0);
}

int
bwi_rf_calc_rssi_bcm2050(struct bwi_mac *mac, const struct bwi_rxbuf_hdr *hdr)
{
	uint16_t flags1, flags3;
	int rssi, lna_gain;

	rssi = hdr->rxh_rssi;
	flags1 = letoh16(hdr->rxh_flags1);
	flags3 = letoh16(hdr->rxh_flags3);

#define NEW_BCM2050_RSSI
#ifdef NEW_BCM2050_RSSI
	if (flags1 & BWI_RXH_F1_OFDM) {
		if (rssi > 127)
			rssi -= 256;
		if (flags3 & BWI_RXH_F3_BCM2050_RSSI)
			rssi += 17;
		else
			rssi -= 4;
		return (rssi);
	}

	if (mac->mac_sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) {
		struct bwi_rf *rf = &mac->mac_rf;

		if (rssi >= BWI_NRSSI_TBLSZ)
			rssi = BWI_NRSSI_TBLSZ - 1;

		rssi = ((31 - (int)rf->rf_nrssi_table[rssi]) * -131) / 128;
		rssi -= 67;
	} else {
		rssi = ((31 - rssi) * -149) / 128;
		rssi -= 68;
	}

	if (mac->mac_phy.phy_mode != IEEE80211_MODE_11G)
		return (rssi);

	if (flags3 & BWI_RXH_F3_BCM2050_RSSI)
		rssi += 20;

	lna_gain = __SHIFTOUT(letoh16(hdr->rxh_phyinfo),
	    BWI_RXH_PHYINFO_LNAGAIN);
	DPRINTF(3, "lna_gain %d, phyinfo 0x%04x\n",
	    lna_gain, letoh16(hdr->rxh_phyinfo));
	switch (lna_gain) {
	case 0:
		rssi += 27;
		break;
	case 1:
		rssi += 6;
		break;
	case 2:
		rssi += 12;
		break;
	case 3:
		/*
		 * XXX
		 * According to v3 spec, we should do _nothing_ here,
		 * but it seems that the result RSSI will be too low
		 * (relative to what ath(4) says).  Raise it a little
		 * bit.
		 */
		rssi += 5;
		break;
	default:
		panic("impossible lna gain %d", lna_gain);
	}
#else	/* !NEW_BCM2050_RSSI */
	lna_gain = 0; /* shut up gcc warning */

	if (flags1 & BWI_RXH_F1_OFDM) {
		if (rssi > 127)
			rssi -= 256;
		rssi = (rssi * 73) / 64;

		if (flags3 & BWI_RXH_F3_BCM2050_RSSI)
			rssi += 25;
		else
			rssi -= 3;
		return (rssi);
	}

	if (mac->mac_sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) {
		struct bwi_rf *rf = &mac->mac_rf;

		if (rssi >= BWI_NRSSI_TBLSZ)
			rssi = BWI_NRSSI_TBLSZ - 1;

		rssi = ((31 - (int)rf->rf_nrssi_table[rssi]) * -131) / 128;
		rssi -= 57;
	} else {
		rssi = ((31 - rssi) * -149) / 128;
		rssi -= 68;
	}

	if (mac->mac_phy.phy_mode != IEEE80211_MODE_11G)
		return (rssi);

	if (flags3 & BWI_RXH_F3_BCM2050_RSSI)
		rssi += 25;
#endif	/* NEW_BCM2050_RSSI */
	return (rssi);
}

int
bwi_rf_calc_rssi_bcm2053(struct bwi_mac *mac, const struct bwi_rxbuf_hdr *hdr)
{
	uint16_t flags1;
	int rssi;

	rssi = (((int)hdr->rxh_rssi - 11) * 103) / 64;

	flags1 = letoh16(hdr->rxh_flags1);
	if (flags1 & BWI_RXH_F1_BCM2053_RSSI)
		rssi -= 109;
	else
		rssi -= 83;

	return (rssi);
}

int
bwi_rf_calc_rssi_bcm2060(struct bwi_mac *mac, const struct bwi_rxbuf_hdr *hdr)
{
	int rssi;

	rssi = hdr->rxh_rssi;
	if (rssi > 127)
		rssi -= 256;

	return (rssi);
}

uint16_t
bwi_rf_lo_measure_11b(struct bwi_mac *mac)
{
	uint16_t val;
	int i;

	val = 0;
	for (i = 0; i < 10; ++i) {
		PHY_WRITE(mac, 0x15, 0xafa0);
		DELAY(1);
		PHY_WRITE(mac, 0x15, 0xefa0);
		DELAY(10);
		PHY_WRITE(mac, 0x15, 0xffa0);
		DELAY(40);

		val += PHY_READ(mac, 0x2c);
	}

	return (val);
}

void
bwi_rf_lo_update_11b(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct rf_saveregs regs;
	uint16_t rf_val, phy_val, min_val, val;
	uint16_t rf52, bphy_ctrl;
	int i;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	bzero(&regs, sizeof(regs));
	bphy_ctrl = 0;

	/*
	 * Save RF/PHY registers for later restoration
	 */
	SAVE_PHY_REG(mac, &regs, 15);
	rf52 = RF_READ(mac, 0x52) & 0xfff0;
	if (rf->rf_type == BWI_RF_T_BCM2050) {
		SAVE_PHY_REG(mac, &regs, 0a);
		SAVE_PHY_REG(mac, &regs, 2a);
		SAVE_PHY_REG(mac, &regs, 35);
		SAVE_PHY_REG(mac, &regs, 03);
		SAVE_PHY_REG(mac, &regs, 01);
		SAVE_PHY_REG(mac, &regs, 30);

		SAVE_RF_REG(mac, &regs, 43);
		SAVE_RF_REG(mac, &regs, 7a);

		bphy_ctrl = CSR_READ_2(sc, BWI_BPHY_CTRL);

		SAVE_RF_REG(mac, &regs, 52);
		regs.rf_52 &= 0xf0;

		PHY_WRITE(mac, 0x30, 0xff);
		CSR_WRITE_2(sc, BWI_PHY_CTRL, 0x3f3f);
		PHY_WRITE(mac, 0x35, regs.phy_35 & 0xff7f);
		RF_WRITE(mac, 0x7a, regs.rf_7a & 0xfff0);
	}

	PHY_WRITE(mac, 0x15, 0xb000);

	if (rf->rf_type == BWI_RF_T_BCM2050) {
		PHY_WRITE(mac, 0x2b, 0x203);
 		PHY_WRITE(mac, 0x2a, 0x8a3);
 	} else {
		PHY_WRITE(mac, 0x2b, 0x1402);
	}

	/*
	 * Setup RF signal
	 */
	rf_val = 0;
	min_val = 65535;

	for (i = 0; i < 4; ++i) {
		RF_WRITE(mac, 0x52, rf52 | i);
		bwi_rf_lo_measure_11b(mac);	/* Ignore return value */
	}
	for (i = 0; i < 10; ++i) {
 		RF_WRITE(mac, 0x52, rf52 | i);

  		val = bwi_rf_lo_measure_11b(mac) / 10;
		if (val < min_val) {
			min_val = val;
			rf_val = i;
		}
	}
	RF_WRITE(mac, 0x52, rf52 | rf_val);

	/*
	 * Setup PHY signal
	*/
	phy_val = 0;
	min_val = 65535;

	for (i = -4; i < 5; i += 2) {
		int j;

		for (j = -4; j < 5; j += 2) {
			uint16_t phy2f;

			phy2f = (0x100 * i) + j;
			if (j < 0)
				phy2f += 0x100;
			PHY_WRITE(mac, 0x2f, phy2f);

			val = bwi_rf_lo_measure_11b(mac) / 10;
			if (val < min_val) {
				min_val = val;
				phy_val = phy2f;
			}
		}
	}
	PHY_WRITE(mac, 0x2f, phy_val + 0x101);

	/*
	 * Restore saved RF/PHY registers
	 */
	if (rf->rf_type == BWI_RF_T_BCM2050) {
		RESTORE_PHY_REG(mac, &regs, 0a);
		RESTORE_PHY_REG(mac, &regs, 2a);
		RESTORE_PHY_REG(mac, &regs, 35);
		RESTORE_PHY_REG(mac, &regs, 03);
		RESTORE_PHY_REG(mac, &regs, 01);
		RESTORE_PHY_REG(mac, &regs, 30);

		RESTORE_RF_REG(mac, &regs, 43);
		RESTORE_RF_REG(mac, &regs, 7a);

		RF_FILT_SETBITS(mac, 0x52, 0xf, regs.rf_52);

		CSR_WRITE_2(sc, BWI_BPHY_CTRL, bphy_ctrl);
	}
	RESTORE_PHY_REG(mac, &regs, 15);

	bwi_rf_workaround(mac, rf->rf_curchan);
}

/* INTERFACE */

uint16_t
bwi_read_sprom(struct bwi_softc *sc, uint16_t ofs)
{
	return (CSR_READ_2(sc, ofs + BWI_SPROM_START));
}

void
bwi_setup_desc32(struct bwi_softc *sc, struct bwi_desc32 *desc_array,
    int ndesc, int desc_idx, bus_addr_t paddr, int buf_len, int tx)
{
	struct bwi_desc32 *desc = &desc_array[desc_idx];
	uint32_t ctrl, addr, addr_hi, addr_lo;

	if (sc->sc_bus_space == BWI_BUS_SPACE_30BIT && paddr >= 0x40000000)
		panic("bad paddr 0x%lx", (long)paddr);

	addr_lo = __SHIFTOUT(paddr, BWI_DESC32_A_ADDR_MASK);
	addr_hi = __SHIFTOUT(paddr, BWI_DESC32_A_FUNC_MASK);

	addr = __SHIFTIN(addr_lo, BWI_DESC32_A_ADDR_MASK) |
	    __SHIFTIN(BWI_DESC32_A_FUNC_TXRX, BWI_DESC32_A_FUNC_MASK);

	ctrl = __SHIFTIN(buf_len, BWI_DESC32_C_BUFLEN_MASK) |
	     __SHIFTIN(addr_hi, BWI_DESC32_C_ADDRHI_MASK);
	if (desc_idx == ndesc - 1)
		ctrl |= BWI_DESC32_C_EOR;
	if (tx) {
		/* XXX */
		ctrl |= BWI_DESC32_C_FRAME_START |
		    BWI_DESC32_C_FRAME_END |
		    BWI_DESC32_C_INTR;
	}

	desc->addr = htole32(addr);
	desc->ctrl = htole32(ctrl);
}

void
bwi_power_on(struct bwi_softc *sc, int with_pll)
{
	uint32_t gpio_in, gpio_out, gpio_en, status;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	gpio_in = (sc->sc_conf_read)(sc, BWI_PCIR_GPIO_IN);
	if (gpio_in & BWI_PCIM_GPIO_PWR_ON)
		goto back;

	gpio_out = (sc->sc_conf_read)(sc, BWI_PCIR_GPIO_OUT);
	gpio_en = (sc->sc_conf_read)(sc, BWI_PCIR_GPIO_ENABLE);

	gpio_out |= BWI_PCIM_GPIO_PWR_ON;
	gpio_en |= BWI_PCIM_GPIO_PWR_ON;
	if (with_pll) {
		/* Turn off PLL first */
		gpio_out |= BWI_PCIM_GPIO_PLL_PWR_OFF;
		gpio_en |= BWI_PCIM_GPIO_PLL_PWR_OFF;
	}

	(sc->sc_conf_write)(sc, BWI_PCIR_GPIO_OUT, gpio_out);
	(sc->sc_conf_write)(sc, BWI_PCIR_GPIO_ENABLE, gpio_en);
	DELAY(1000);

	if (with_pll) {
		/* Turn on PLL */
		gpio_out &= ~BWI_PCIM_GPIO_PLL_PWR_OFF;
		(sc->sc_conf_write)(sc, BWI_PCIR_GPIO_OUT, gpio_out);
		DELAY(5000);
	}

back:
	/* Clear "Signaled Target Abort" */
	status = (sc->sc_conf_read)(sc, PCI_COMMAND_STATUS_REG);
	status &= ~PCI_STATUS_TARGET_TARGET_ABORT;
	(sc->sc_conf_write)(sc, PCI_COMMAND_STATUS_REG, status);
}

int
bwi_power_off(struct bwi_softc *sc, int with_pll)
{
	uint32_t gpio_out, gpio_en;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	(sc->sc_conf_read)(sc, BWI_PCIR_GPIO_IN); /* dummy read */
	gpio_out = (sc->sc_conf_read)(sc, BWI_PCIR_GPIO_OUT);
	gpio_en = (sc->sc_conf_read)(sc, BWI_PCIR_GPIO_ENABLE);

	gpio_out &= ~BWI_PCIM_GPIO_PWR_ON;
	gpio_en |= BWI_PCIM_GPIO_PWR_ON;
	if (with_pll) {
		gpio_out |= BWI_PCIM_GPIO_PLL_PWR_OFF;
		gpio_en |= BWI_PCIM_GPIO_PLL_PWR_OFF;
	}

	(sc->sc_conf_write)(sc, BWI_PCIR_GPIO_OUT, gpio_out);
	(sc->sc_conf_write)(sc, BWI_PCIR_GPIO_ENABLE, gpio_en);

	return (0);
}

int
bwi_regwin_switch(struct bwi_softc *sc, struct bwi_regwin *rw,
    struct bwi_regwin **old_rw)
{
	int error;

	if (old_rw != NULL)
		*old_rw = NULL;

	if (!BWI_REGWIN_EXIST(rw))
		return (EINVAL);

	if (sc->sc_cur_regwin != rw) {
		error = bwi_regwin_select(sc, rw->rw_id);
		if (error) {
			printf("%s: can't select regwin %d\n",
			    sc->sc_dev.dv_xname, rw->rw_id);
			return (error);
		}
	}

	if (old_rw != NULL)
		*old_rw = sc->sc_cur_regwin;
	sc->sc_cur_regwin = rw;

	return (0);
}

int
bwi_regwin_select(struct bwi_softc *sc, int id)
{
	uint32_t win = BWI_PCIM_REGWIN(id);
	int i;

#define RETRY_MAX	50
	for (i = 0; i < RETRY_MAX; ++i) {
		(sc->sc_conf_write)(sc, BWI_PCIR_SEL_REGWIN, win);
		if ((sc->sc_conf_read)(sc, BWI_PCIR_SEL_REGWIN) == win)
			return (0);
		DELAY(10);
	}
#undef RETRY_MAX

	return (ENXIO);
}

void
bwi_regwin_info(struct bwi_softc *sc, uint16_t *type, uint8_t *rev)
{
	uint32_t val;

	val = CSR_READ_4(sc, BWI_ID_HI);
	*type = BWI_ID_HI_REGWIN_TYPE(val);
	*rev = BWI_ID_HI_REGWIN_REV(val);

	DPRINTF(1, "%s: regwin: type 0x%03x, rev %d, vendor 0x%04x\n",
	    sc->sc_dev.dv_xname,
	    *type, *rev, __SHIFTOUT(val, BWI_ID_HI_REGWIN_VENDOR_MASK));
}

void
bwi_led_attach(struct bwi_softc *sc)
{
	const uint8_t *led_act = NULL;
	uint16_t gpio, val[BWI_LED_MAX];
	int i;

	for (i = 0; i < nitems(bwi_vendor_led_act); ++i) {
		if (sc->sc_pci_subvid == bwi_vendor_led_act[i].vid) {
			led_act = bwi_vendor_led_act[i].led_act;
				break;
		}
	}
	if (led_act == NULL)
		led_act = bwi_default_led_act;

	gpio = bwi_read_sprom(sc, BWI_SPROM_GPIO01);
	val[0] = __SHIFTOUT(gpio, BWI_SPROM_GPIO_0);
	val[1] = __SHIFTOUT(gpio, BWI_SPROM_GPIO_1);

	gpio = bwi_read_sprom(sc, BWI_SPROM_GPIO23);
	val[2] = __SHIFTOUT(gpio, BWI_SPROM_GPIO_2);
	val[3] = __SHIFTOUT(gpio, BWI_SPROM_GPIO_3);

	for (i = 0; i < BWI_LED_MAX; ++i) {
		struct bwi_led *led = &sc->sc_leds[i];

		if (val[i] == 0xff) {
			led->l_act = led_act[i];
		} else {
			if (val[i] & BWI_LED_ACT_LOW)
				led->l_flags |= BWI_LED_F_ACTLOW;
			led->l_act = __SHIFTOUT(val[i], BWI_LED_ACT_MASK);
		}
		led->l_mask = (1 << i);

		if (led->l_act == BWI_LED_ACT_BLINK_SLOW ||
		    led->l_act == BWI_LED_ACT_BLINK_POLL ||
		    led->l_act == BWI_LED_ACT_BLINK) {
		    	led->l_flags |= BWI_LED_F_BLINK;
			if (led->l_act == BWI_LED_ACT_BLINK_POLL)
				led->l_flags |= BWI_LED_F_POLLABLE;
			else if (led->l_act == BWI_LED_ACT_BLINK_SLOW)
				led->l_flags |= BWI_LED_F_SLOW;

			if (sc->sc_blink_led == NULL) {
				sc->sc_blink_led = led;
				if (led->l_flags & BWI_LED_F_SLOW)
					BWI_LED_SLOWDOWN(sc->sc_led_idle);
			}
		}

		DPRINTF(1, "%s: %dth led, act %d, lowact %d\n",
		    sc->sc_dev.dv_xname, i, led->l_act,
		    led->l_flags & BWI_LED_F_ACTLOW);
	}
	timeout_set(&sc->sc_led_blink_next_ch, bwi_led_blink_next, sc);
	timeout_set(&sc->sc_led_blink_end_ch, bwi_led_blink_end, sc);
}

uint16_t
bwi_led_onoff(struct bwi_led *led, uint16_t val, int on)
{
	if (led->l_flags & BWI_LED_F_ACTLOW)
		on = !on;
	if (on)
		val |= led->l_mask;
	else
		val &= ~led->l_mask;

	return (val);
}

void
bwi_led_newstate(struct bwi_softc *sc, enum ieee80211_state nstate)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t val;
	int i;

	if (nstate == IEEE80211_S_INIT) {
		timeout_del(&sc->sc_led_blink_next_ch);
		timeout_del(&sc->sc_led_blink_end_ch);
		sc->sc_led_blinking = 0;
	}

	if ((ic->ic_if.if_flags & IFF_RUNNING) == 0)
		return;

	val = CSR_READ_2(sc, BWI_MAC_GPIO_CTRL);
	for (i = 0; i < BWI_LED_MAX; ++i) {
		struct bwi_led *led = &sc->sc_leds[i];
		int on;

		if (led->l_act == BWI_LED_ACT_UNKN ||
		    led->l_act == BWI_LED_ACT_NULL)
			continue;

		if ((led->l_flags & BWI_LED_F_BLINK) &&
			nstate != IEEE80211_S_INIT)
			continue;

		switch (led->l_act) {
		case BWI_LED_ACT_ON:	/* Always on */
			on = 1;
			break;
		case BWI_LED_ACT_OFF:	/* Always off */
		case BWI_LED_ACT_5GHZ:	/* TODO: 11A */
			on = 0;
			break;
		default:
			on = 1;
			switch (nstate) {
			case IEEE80211_S_INIT:
				on = 0;
				break;
			case IEEE80211_S_RUN:
				if (led->l_act == BWI_LED_ACT_11G &&
				    ic->ic_curmode != IEEE80211_MODE_11G)
					on = 0;
				break;
			default:
				if (led->l_act == BWI_LED_ACT_ASSOC)
					on = 0;
				break;
			}
			break;
		}

		val = bwi_led_onoff(led, val, on);
	}
	CSR_WRITE_2(sc, BWI_MAC_GPIO_CTRL, val);
}

void
bwi_led_event(struct bwi_softc *sc, int event)
{
	struct bwi_led *led = sc->sc_blink_led;
	int rate;

	if (event == BWI_LED_EVENT_POLL) {
		if ((led->l_flags & BWI_LED_F_POLLABLE) == 0)
			return;
		if (ticks - sc->sc_led_ticks < sc->sc_led_idle)
			return;
	}

	sc->sc_led_ticks = ticks;
	if (sc->sc_led_blinking)
		return;

	switch (event) {
	case BWI_LED_EVENT_RX:
		rate = sc->sc_rx_rate;
		break;
	case BWI_LED_EVENT_TX:
		rate = sc->sc_tx_rate;
		break;
	case BWI_LED_EVENT_POLL:
		rate = 0;
		break;
	default:
		panic("unknown LED event %d", event);
		break;
	}
	bwi_led_blink_start(sc, bwi_led_duration[rate].on_dur,
	    bwi_led_duration[rate].off_dur);
}

void
bwi_led_blink_start(struct bwi_softc *sc, int on_dur, int off_dur)
{
	struct bwi_led *led = sc->sc_blink_led;
	uint16_t val;

	val = CSR_READ_2(sc, BWI_MAC_GPIO_CTRL);
	val = bwi_led_onoff(led, val, 1);
	CSR_WRITE_2(sc, BWI_MAC_GPIO_CTRL, val);

	if (led->l_flags & BWI_LED_F_SLOW) {
		BWI_LED_SLOWDOWN(on_dur);
		BWI_LED_SLOWDOWN(off_dur);
	}

	sc->sc_led_blinking = 1;
	sc->sc_led_blink_offdur = off_dur;

	timeout_add(&sc->sc_led_blink_next_ch, on_dur);
}

void
bwi_led_blink_next(void *xsc)
{
	struct bwi_softc *sc = xsc;
	uint16_t val;

	val = CSR_READ_2(sc, BWI_MAC_GPIO_CTRL);
	val = bwi_led_onoff(sc->sc_blink_led, val, 0);
	CSR_WRITE_2(sc, BWI_MAC_GPIO_CTRL, val);

	timeout_add(&sc->sc_led_blink_end_ch, sc->sc_led_blink_offdur);
}

void
bwi_led_blink_end(void *xsc)
{
	struct bwi_softc *sc = xsc;

	sc->sc_led_blinking = 0;
}

int
bwi_bbp_attach(struct bwi_softc *sc)
{
	uint16_t bbp_id, rw_type;
	uint8_t rw_rev;
	uint32_t info;
	int error, nregwin, i;

	/*
	 * Get 0th regwin information
	 * NOTE: 0th regwin should exist
	 */
	error = bwi_regwin_select(sc, 0);
	if (error) {
		printf("%s: can't select regwin 0\n", sc->sc_dev.dv_xname);
		return (error);
	}
	bwi_regwin_info(sc, &rw_type, &rw_rev);

	/*
	 * Find out BBP id
	 */
	bbp_id = 0;
	info = 0;
	if (rw_type == BWI_REGWIN_T_COM) {
		info = CSR_READ_4(sc, BWI_INFO);
		bbp_id = __SHIFTOUT(info, BWI_INFO_BBPID_MASK);

		BWI_CREATE_REGWIN(&sc->sc_com_regwin, 0, rw_type, rw_rev);

		sc->sc_cap = CSR_READ_4(sc, BWI_CAPABILITY);
	} else {
		uint16_t did = sc->sc_pci_did;
		uint8_t revid = sc->sc_pci_revid;

		for (i = 0; i < nitems(bwi_bbpid_map); ++i) {
			if (did >= bwi_bbpid_map[i].did_min &&
			    did <= bwi_bbpid_map[i].did_max) {
				bbp_id = bwi_bbpid_map[i].bbp_id;
				break;
			}
		}
		if (bbp_id == 0) {
			printf("%s: no BBP id for device id 0x%04x\n",
			    sc->sc_dev.dv_xname, did);
			return (ENXIO);
		}

		info = __SHIFTIN(revid, BWI_INFO_BBPREV_MASK) |
		    __SHIFTIN(0, BWI_INFO_BBPPKG_MASK);
	}

	/*
	 * Find out number of regwins
	 */
	nregwin = 0;
	if (rw_type == BWI_REGWIN_T_COM && rw_rev >= 4) {
		nregwin = __SHIFTOUT(info, BWI_INFO_NREGWIN_MASK);
	} else {
		for (i = 0; i < nitems(bwi_regwin_count); ++i) {
			if (bwi_regwin_count[i].bbp_id == bbp_id) {
				nregwin = bwi_regwin_count[i].nregwin;
				break;
			}
		}
		if (nregwin == 0) {
			printf("%s: no number of win for BBP id 0x%04x\n",
			    sc->sc_dev.dv_xname, bbp_id);
			return (ENXIO);
		}
	}

	/* Record BBP id/rev for later using */
	sc->sc_bbp_id = bbp_id;
	sc->sc_bbp_rev = __SHIFTOUT(info, BWI_INFO_BBPREV_MASK);
	sc->sc_bbp_pkg = __SHIFTOUT(info, BWI_INFO_BBPPKG_MASK);
	DPRINTF(1, "%s: BBP id 0x%04x, BBP rev 0x%x, BBP pkg %d\n",
	    sc->sc_dev.dv_xname, sc->sc_bbp_id, sc->sc_bbp_rev, sc->sc_bbp_pkg);
	DPRINTF(1, "%s: nregwin %d, cap 0x%08x\n",
	    sc->sc_dev.dv_xname, nregwin, sc->sc_cap);

	/*
	 * Create rest of the regwins
	 */

	/* Don't re-create common regwin, if it is already created */
	i = BWI_REGWIN_EXIST(&sc->sc_com_regwin) ? 1 : 0;

	for (; i < nregwin; ++i) {
		/*
		 * Get regwin information
		 */
		error = bwi_regwin_select(sc, i);
		if (error) {
			printf("%s: can't select regwin %d\n",
			    sc->sc_dev.dv_xname, i);
			return (error);
		}
		bwi_regwin_info(sc, &rw_type, &rw_rev);

		/*
		 * Try attach:
		 * 1) Bus (PCI/PCIE) regwin
		 * 2) MAC regwin
		 * Ignore rest types of regwin
		 */
		if (rw_type == BWI_REGWIN_T_BUSPCI ||
		    rw_type == BWI_REGWIN_T_BUSPCIE) {
			if (BWI_REGWIN_EXIST(&sc->sc_bus_regwin)) {
				printf("%s: bus regwin already exists\n",
				    sc->sc_dev.dv_xname);
			} else {
				BWI_CREATE_REGWIN(&sc->sc_bus_regwin, i,
				    rw_type, rw_rev);
			}
		} else if (rw_type == BWI_REGWIN_T_MAC) {
			/* XXX ignore return value */
			bwi_mac_attach(sc, i, rw_rev);
		}
	}

	/* At least one MAC should exist */
	if (!BWI_REGWIN_EXIST(&sc->sc_mac[0].mac_regwin)) {
		printf("%s: no MAC was found\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}
	KASSERT(sc->sc_nmac > 0);

	/* Bus regwin must exist */
	if (!BWI_REGWIN_EXIST(&sc->sc_bus_regwin)) {
		printf("%s: no bus regwin was found\n", sc->sc_dev.dv_xname);
		return (ENXIO);
	}

	/* Start with first MAC */
	error = bwi_regwin_switch(sc, &sc->sc_mac[0].mac_regwin, NULL);
	if (error)
		return (error);

	return (0);
}

int
bwi_bus_init(struct bwi_softc *sc, struct bwi_mac *mac)
{
	struct bwi_regwin *old, *bus;
	uint32_t val;
	int error;

	bus = &sc->sc_bus_regwin;
	KASSERT(sc->sc_cur_regwin == &mac->mac_regwin);

	/*
	 * Tell bus to generate requested interrupts
	 */
	if (bus->rw_rev < 6 && bus->rw_type == BWI_REGWIN_T_BUSPCI) {
		/*
		 * NOTE: Read BWI_FLAGS from MAC regwin
		 */
		val = CSR_READ_4(sc, BWI_FLAGS);

		error = bwi_regwin_switch(sc, bus, &old);
		if (error)
			return (error);

		CSR_SETBITS_4(sc, BWI_INTRVEC, (val & BWI_FLAGS_INTR_MASK));
	} else {
		uint32_t mac_mask;

		mac_mask = 1 << mac->mac_id;

		error = bwi_regwin_switch(sc, bus, &old);
		if (error)
			return (error);

		val = (sc->sc_conf_read)(sc, BWI_PCIR_INTCTL);
		val |= mac_mask << 8;
		(sc->sc_conf_write)(sc, BWI_PCIR_INTCTL, val);
	}

	if (sc->sc_flags & BWI_F_BUS_INITED)
		goto back;

	if (bus->rw_type == BWI_REGWIN_T_BUSPCI) {
		/*
		 * Enable prefetch and burst
		 */
		CSR_SETBITS_4(sc, BWI_BUS_CONFIG,
		    BWI_BUS_CONFIG_PREFETCH | BWI_BUS_CONFIG_BURST);

		if (bus->rw_rev < 5) {
			struct bwi_regwin *com = &sc->sc_com_regwin;

			/*
			 * Configure timeouts for bus operation
			 */

			/*
			 * Set service timeout and request timeout
			 */
			CSR_SETBITS_4(sc, BWI_CONF_LO,
			    __SHIFTIN(BWI_CONF_LO_SERVTO,
			    BWI_CONF_LO_SERVTO_MASK) |
			    __SHIFTIN(BWI_CONF_LO_REQTO,
			    BWI_CONF_LO_REQTO_MASK));

			/*
			 * If there is common regwin, we switch to that regwin
			 * and switch back to bus regwin once we have done.
			 */
			if (BWI_REGWIN_EXIST(com)) {
				error = bwi_regwin_switch(sc, com, NULL);
				if (error)
					return (error);
			}

			/* Let bus know what we have changed */
			CSR_WRITE_4(sc, BWI_BUS_ADDR, BWI_BUS_ADDR_MAGIC);
			CSR_READ_4(sc, BWI_BUS_ADDR); /* Flush */
			CSR_WRITE_4(sc, BWI_BUS_DATA, 0);
			CSR_READ_4(sc, BWI_BUS_DATA); /* Flush */

			if (BWI_REGWIN_EXIST(com)) {
				error = bwi_regwin_switch(sc, bus, NULL);
				if (error)
					return (error);
			}
		} else if (bus->rw_rev >= 11) {
			/*
			 * Enable memory read multiple
			 */
			CSR_SETBITS_4(sc, BWI_BUS_CONFIG, BWI_BUS_CONFIG_MRM);
		}
	} else {
		/* TODO: PCIE */
	}

	sc->sc_flags |= BWI_F_BUS_INITED;
back:
	return (bwi_regwin_switch(sc, old, NULL));
}

void
bwi_get_card_flags(struct bwi_softc *sc)
{
	sc->sc_card_flags = bwi_read_sprom(sc, BWI_SPROM_CARD_FLAGS);
	if (sc->sc_card_flags == 0xffff)
		sc->sc_card_flags = 0;

	if (sc->sc_pci_subvid == PCI_VENDOR_APPLE &&
	    sc->sc_pci_subdid == 0x4e && /* XXX */
	    sc->sc_pci_revid > 0x40)
		sc->sc_card_flags |= BWI_CARD_F_PA_GPIO9;

	DPRINTF(1, "%s: card flags 0x%04x\n",
	    sc->sc_dev.dv_xname, sc->sc_card_flags);
}

void
bwi_get_eaddr(struct bwi_softc *sc, uint16_t eaddr_ofs, uint8_t *eaddr)
{
	int i;

	for (i = 0; i < 3; ++i) {
		*((uint16_t *)eaddr + i) =
		    htobe16(bwi_read_sprom(sc, eaddr_ofs + 2 * i));
	}
}

void
bwi_get_clock_freq(struct bwi_softc *sc, struct bwi_clock_freq *freq)
{
	struct bwi_regwin *com;
	uint32_t val;
	uint div;
	int src;

	bzero(freq, sizeof(*freq));
	com = &sc->sc_com_regwin;

	KASSERT(BWI_REGWIN_EXIST(com));
	KASSERT(sc->sc_cur_regwin == com);
	KASSERT(sc->sc_cap & BWI_CAP_CLKMODE);

	/*
	 * Calculate clock frequency
	 */
	src = -1;
	div = 0;
	if (com->rw_rev < 6) {
		val = (sc->sc_conf_read)(sc, BWI_PCIR_GPIO_OUT);
		if (val & BWI_PCIM_GPIO_OUT_CLKSRC) {
			src = BWI_CLKSRC_PCI;
			div = 64;
		} else {
			src = BWI_CLKSRC_CS_OSC;
			div = 32;
		}
	} else if (com->rw_rev < 10) {
		val = CSR_READ_4(sc, BWI_CLOCK_CTRL);

		src = __SHIFTOUT(val, BWI_CLOCK_CTRL_CLKSRC);
		if (src == BWI_CLKSRC_LP_OSC)
			div = 1;
		else {
			div = (__SHIFTOUT(val, BWI_CLOCK_CTRL_FDIV) + 1) << 2;

			/* Unknown source */
			if (src >= BWI_CLKSRC_MAX)
				src = BWI_CLKSRC_CS_OSC;
		}
	} else {
		val = CSR_READ_4(sc, BWI_CLOCK_INFO);

		src = BWI_CLKSRC_CS_OSC;
		div = (__SHIFTOUT(val, BWI_CLOCK_INFO_FDIV) + 1) << 2;
	}

	KASSERT(src >= 0 && src < BWI_CLKSRC_MAX);
	KASSERT(div != 0);

	DPRINTF(1, "%s: clksrc %s\n",
	    sc->sc_dev.dv_xname,
	    src == BWI_CLKSRC_PCI ? "PCI" :
	    (src == BWI_CLKSRC_LP_OSC ? "LP_OSC" : "CS_OSC"));

	freq->clkfreq_min = bwi_clkfreq[src].freq_min / div;
	freq->clkfreq_max = bwi_clkfreq[src].freq_max / div;

	DPRINTF(1, "%s: clkfreq min %u, max %u\n",
	    sc->sc_dev.dv_xname, freq->clkfreq_min, freq->clkfreq_max);
}

int
bwi_set_clock_mode(struct bwi_softc *sc, enum bwi_clock_mode clk_mode)
{
	struct bwi_regwin *old, *com;
	uint32_t clk_ctrl, clk_src;
	int error, pwr_off = 0;

	com = &sc->sc_com_regwin;
	if (!BWI_REGWIN_EXIST(com))
		return (0);

	if (com->rw_rev >= 10 || com->rw_rev < 6)
		return (0);

	/*
	 * For common regwin whose rev is [6, 10), the chip
	 * must be capable to change clock mode.
	 */
	if ((sc->sc_cap & BWI_CAP_CLKMODE) == 0)
		return (0);

	error = bwi_regwin_switch(sc, com, &old);
	if (error)
		return (error);

	if (clk_mode == BWI_CLOCK_MODE_FAST)
		bwi_power_on(sc, 0);	/* Don't turn on PLL */

	clk_ctrl = CSR_READ_4(sc, BWI_CLOCK_CTRL);
	clk_src = __SHIFTOUT(clk_ctrl, BWI_CLOCK_CTRL_CLKSRC);

	switch (clk_mode) {
	case BWI_CLOCK_MODE_FAST:
		clk_ctrl &= ~BWI_CLOCK_CTRL_SLOW;
		clk_ctrl |= BWI_CLOCK_CTRL_IGNPLL;
		break;
	case BWI_CLOCK_MODE_SLOW:
		clk_ctrl |= BWI_CLOCK_CTRL_SLOW;
		break;
	case BWI_CLOCK_MODE_DYN:
		clk_ctrl &= ~(BWI_CLOCK_CTRL_SLOW |
		    BWI_CLOCK_CTRL_IGNPLL |
		    BWI_CLOCK_CTRL_NODYN);
		if (clk_src != BWI_CLKSRC_CS_OSC) {
			clk_ctrl |= BWI_CLOCK_CTRL_NODYN;
			pwr_off = 1;
		}
		break;
	}
	CSR_WRITE_4(sc, BWI_CLOCK_CTRL, clk_ctrl);

	if (pwr_off)
		bwi_power_off(sc, 0);	/* Leave PLL as it is */

	return (bwi_regwin_switch(sc, old, NULL));
}

int
bwi_set_clock_delay(struct bwi_softc *sc)
{
	struct bwi_regwin *old, *com;
	int error;

	com = &sc->sc_com_regwin;
	if (!BWI_REGWIN_EXIST(com))
		return (0);

	error = bwi_regwin_switch(sc, com, &old);
	if (error)
		return (error);

	if (sc->sc_bbp_id == BWI_BBPID_BCM4321) {
		if (sc->sc_bbp_rev == 0)
			CSR_WRITE_4(sc, BWI_CONTROL, BWI_CONTROL_MAGIC0);
		else if (sc->sc_bbp_rev == 1)
			CSR_WRITE_4(sc, BWI_CONTROL, BWI_CONTROL_MAGIC1);
	}

	if (sc->sc_cap & BWI_CAP_CLKMODE) {
		if (com->rw_rev >= 10)
			CSR_FILT_SETBITS_4(sc, BWI_CLOCK_INFO, 0xffff, 0x40000);
		else {
			struct bwi_clock_freq freq;

			bwi_get_clock_freq(sc, &freq);
			CSR_WRITE_4(sc, BWI_PLL_ON_DELAY,
			    howmany(freq.clkfreq_max * 150, 1000000));
			CSR_WRITE_4(sc, BWI_FREQ_SEL_DELAY,
			    howmany(freq.clkfreq_max * 15, 1000000));
		}
	}

	return (bwi_regwin_switch(sc, old, NULL));
}

int
bwi_init(struct ifnet *ifp)
{
	struct bwi_softc *sc = ifp->if_softc;

	bwi_init_statechg(sc, 1);

	return (0);
}

void
bwi_init_statechg(struct bwi_softc *sc, int statechg)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct bwi_mac *mac;
	int error;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	error = bwi_stop(sc, statechg);
	if (error) {
		DPRINTF(1, "%s: can't stop\n", sc->sc_dev.dv_xname);
		return;
	}

	/* power on cardbus socket */
	if (sc->sc_enable != NULL)
		(*sc->sc_enable)(sc);

	bwi_bbp_power_on(sc, BWI_CLOCK_MODE_FAST);

	/* TODO: 2 MAC */

	mac = &sc->sc_mac[0];
	error = bwi_regwin_switch(sc, &mac->mac_regwin, NULL);
	if (error)
		goto back;

	error = bwi_mac_init(mac);
	if (error)
		goto back;

	bwi_bbp_power_on(sc, BWI_CLOCK_MODE_DYN);
	
	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));

	bwi_set_bssid(sc, bwi_zero_addr);	/* Clear BSSID */
	bwi_set_addr_filter(sc, BWI_ADDR_FILTER_MYADDR, ic->ic_myaddr);

	bwi_mac_reset_hwkeys(mac);

	if ((mac->mac_flags & BWI_MAC_F_HAS_TXSTATS) == 0) {
		int i;

#define NRETRY	1000
		/*
		 * Drain any possible pending TX status
		 */
		for (i = 0; i < NRETRY; ++i) {
			if ((CSR_READ_4(sc, BWI_TXSTATUS_0) &
			     BWI_TXSTATUS_0_MORE) == 0)
				break;
			CSR_READ_4(sc, BWI_TXSTATUS_1);
		}
		if (i == NRETRY)
			printf("%s: can't drain TX status\n",
			    sc->sc_dev.dv_xname);
#undef NRETRY
	}

	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11G)
		bwi_mac_updateslot(mac, 1);

	/* Start MAC */
	error = bwi_mac_start(mac);
	if (error)
		goto back;

	/* Enable intrs */
	bwi_enable_intrs(sc, BWI_INIT_INTRS);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (statechg) {
		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		} else {
			ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
		}
	} else {
		ieee80211_new_state(ic, ic->ic_state, -1);
	}

back:
	if (error)
		bwi_stop(sc, 1);
	else
		bwi_start(ifp);
}

int
bwi_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct bwi_softc *sc = ifp->if_softc;
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
				bwi_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				bwi_stop(sc, 1);
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
				bwi_set_chan(sc, chan);
			}
			error = 0;
		}
		break;
	default:
		error = ieee80211_ioctl(ifp, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			bwi_init(ifp);
		error = 0;
	}

	splx(s);

	return (error);
}

void
bwi_start(struct ifnet *ifp)
{
	struct bwi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct bwi_txbuf_data *tbd = &sc->sc_tx_bdata[BWI_TX_DATA_RING];
	int trans, idx;

	if (ifq_is_oactive(&ifp->if_snd) || (ifp->if_flags & IFF_RUNNING) == 0)
		return;

	trans = 0;
	idx = tbd->tbd_idx;

	while (tbd->tbd_buf[idx].tb_mbuf == NULL) {
		struct ieee80211_frame *wh;
		struct ieee80211_node *ni;
		struct ieee80211_key *k;
		struct mbuf *m;
		int mgt_pkt = 0;

		m = mq_dequeue(&ic->ic_mgtq);
		if (m != NULL) {
			ni = m->m_pkthdr.ph_cookie;

			mgt_pkt = 1;
		} else {
			struct ether_header *eh;

			if (ic->ic_state != IEEE80211_S_RUN)
				break;

			m = ifq_dequeue(&ifp->if_snd);
			if (m == NULL)
				break;

			if (m->m_len < sizeof(*eh)) {
				m = m_pullup(m, sizeof(*eh));
				if (m == NULL) {
					ifp->if_oerrors++;
					continue;
				}
			}
			eh = mtod(m, struct ether_header *);

			ni = ieee80211_find_txnode(ic, eh->ether_dhost);
			if (ni == NULL) {
				m_freem(m);
				ifp->if_oerrors++;
				continue;
			}

			/* TODO: PS */
#if NBPFILTER > 0
			if (ifp->if_bpf != NULL)
				bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
			m = ieee80211_encap(ifp, m, &ni);
			if (m == NULL)
				continue;
		}
#if NBPFILTER > 0
		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_OUT);
#endif
		wh = mtod(m, struct ieee80211_frame *);
		if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
			k = ieee80211_get_txkey(ic, wh, ni);
			if ((m = ieee80211_encrypt(ic, m, k)) == NULL)
				return;
		}
		wh = NULL;	/* Catch any invalid use */

		if (mgt_pkt) {
			ieee80211_release_node(ic, ni);
			ni = NULL;
		}

		if (bwi_encap(sc, idx, m, ni) != 0) {
			/* 'm' is freed in bwi_encap() if we reach here */
			if (ni != NULL)
				ieee80211_release_node(ic, ni);
			ifp->if_oerrors++;
			continue;
		}

		trans = 1;
		tbd->tbd_used++;
		idx = (idx + 1) % BWI_TX_NDESC;

		if (tbd->tbd_used + BWI_TX_NSPRDESC >= BWI_TX_NDESC) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
	}
	tbd->tbd_idx = idx;

	if (trans)
		sc->sc_tx_timer = 5;
	ifp->if_timer = 1;
}

void
bwi_watchdog(struct ifnet *ifp)
{
	struct bwi_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	if (sc->sc_tx_timer) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: watchdog timeout\n",
			    sc->sc_dev.dv_xname);
			ifp->if_oerrors++;
			/* TODO */
		} else
			ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

void
bwi_newstate_begin(struct bwi_softc *sc, enum ieee80211_state nstate)
{
	timeout_del(&sc->sc_scan_ch);
	timeout_del(&sc->sc_calib_ch);

	bwi_led_newstate(sc, nstate);

	if (nstate == IEEE80211_S_INIT)
		sc->sc_txpwrcb_type = BWI_TXPWR_INIT;
}

int
bwi_stop(struct bwi_softc *sc, int state_chg)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct bwi_mac *mac;
	int i, error, pwr_off = 0;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	if (state_chg)
		ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
	else
		bwi_newstate_begin(sc, IEEE80211_S_INIT);

	if (ifp->if_flags & IFF_RUNNING) {
		KASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
		mac = (struct bwi_mac *)sc->sc_cur_regwin;

		bwi_disable_intrs(sc, BWI_ALL_INTRS);
		CSR_READ_4(sc, BWI_MAC_INTR_MASK);
		bwi_mac_stop(mac);
	}

	for (i = 0; i < sc->sc_nmac; ++i) {
		struct bwi_regwin *old_rw;

		mac = &sc->sc_mac[i];
		if ((mac->mac_flags & BWI_MAC_F_INITED) == 0)
			continue;

		error = bwi_regwin_switch(sc, &mac->mac_regwin, &old_rw);
		if (error)
			continue;

		bwi_mac_shutdown(mac);
		pwr_off = 1;

		bwi_regwin_switch(sc, old_rw, NULL);
	}

	if (pwr_off)
		bwi_bbp_power_off(sc);

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* power off cardbus socket */
	if (sc->sc_disable)
		sc->sc_disable(sc);

	return (0);
}

int
bwi_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct bwi_softc *sc = ic->ic_if.if_softc;
	struct ieee80211_node *ni;
	int error;
	uint8_t chan;

	timeout_del(&sc->sc_amrr_ch);

	bwi_newstate_begin(sc, nstate);

	if (nstate == IEEE80211_S_INIT)
		goto back;

	chan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
	error = bwi_set_chan(sc, chan);
	if (error) {
		printf("%s: can't set channel to %u\n",
		    sc->sc_dev.dv_xname,
		    ieee80211_chan2ieee(ic, ic->ic_des_chan));
		return (error);
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		/* Nothing to do */
	} else if (nstate == IEEE80211_S_RUN) {
		struct bwi_mac *mac;

		ni = ic->ic_bss;

		bwi_set_bssid(sc, ic->ic_bss->ni_bssid);

		KASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
		mac = (struct bwi_mac *)sc->sc_cur_regwin;

		/* Initial TX power calibration */
		bwi_mac_calibrate_txpower(mac, BWI_TXPWR_INIT);
#ifdef notyet
		sc->sc_txpwrcb_type = BWI_TXPWR_FORCE;
#else
		sc->sc_txpwrcb_type = BWI_TXPWR_CALIB;
#endif
		if (ic->ic_opmode == IEEE80211_M_STA) {
			/* fake a join to init the tx rate */
			bwi_newassoc(ic, ni, 1);
		}

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			/* start automatic rate control timer */
			if (ic->ic_fixed_rate == -1)
				timeout_add_msec(&sc->sc_amrr_ch, 500);
		}
	} else
		bwi_set_bssid(sc, bwi_zero_addr);

back:
	error = sc->sc_newstate(ic, nstate, arg);

	if (nstate == IEEE80211_S_SCAN) {
		timeout_add_msec(&sc->sc_scan_ch, sc->sc_dwell_time);
	} else if (nstate == IEEE80211_S_RUN) {
		/* XXX 15 seconds */
		timeout_add_sec(&sc->sc_calib_ch, 1);
	}

	return (error);
}

int
bwi_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return (error);

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		bwi_init(ifp);

	return (0);
}

void
bwi_iter_func(void *arg, struct ieee80211_node *ni)
{
	struct bwi_softc *sc = arg;
	struct bwi_node *bn = (struct bwi_node *)ni;

	ieee80211_amrr_choose(&sc->sc_amrr, ni, &bn->amn);
}

void
bwi_amrr_timeout(void *arg)
{
	struct bwi_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_opmode == IEEE80211_M_STA)
		bwi_iter_func(sc, ic->ic_bss);
#ifndef IEEE80211_STA_ONLY
	else
		ieee80211_iterate_nodes(ic, bwi_iter_func, sc);
#endif

	timeout_add_msec(&sc->sc_amrr_ch, 500);
}

void
bwi_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	struct bwi_softc *sc = ic->ic_if.if_softc;
	int i;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	ieee80211_amrr_node_init(&sc->sc_amrr, &((struct bwi_node *)ni)->amn);

	/* set rate to some reasonable initial value */
	for (i = ni->ni_rates.rs_nrates - 1;
	    i > 0 && (ni->ni_rates.rs_rates[i] & IEEE80211_RATE_VAL) > 72;
	    i--);

	ni->ni_txrate = i;
}

struct ieee80211_node *
bwi_node_alloc(struct ieee80211com *ic)
{
	struct bwi_node *bn;

	bn = malloc(sizeof(*bn), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (bn == NULL)
		return (NULL);

	return ((struct ieee80211_node *)bn);
}

struct uvm_constraint_range bwi_constraint = { 0x0, (0x40000000 - 1) };
struct kmem_pa_mode bwi_pa_mode = {
	.kp_align = BWI_RING_ALIGN,
	.kp_constraint = &bwi_constraint,
	.kp_zero = 1
};

int
bwi_dma_alloc(struct bwi_softc *sc)
{
	int error, i, has_txstats;
	bus_size_t tx_ring_sz, rx_ring_sz, desc_sz = 0;
	uint32_t txrx_ctrl_step = 0;
	int s;

	has_txstats = 0;
	for (i = 0; i < sc->sc_nmac; ++i) {
		if (sc->sc_mac[i].mac_flags & BWI_MAC_F_HAS_TXSTATS) {
			has_txstats = 1;
			break;
		}
	}

	switch (sc->sc_bus_space) {
	case BWI_BUS_SPACE_30BIT:
		/* 
		 * 30bit devices must use bounce buffers but
		 * otherwise work like 32bit devices.
		 */
		sc->sc_newbuf = bwi_newbuf30;

		/* XXX implement txstats for 30bit? */
		has_txstats = 0;

		/* FALLTHROUGH */
	case BWI_BUS_SPACE_32BIT:
		desc_sz = sizeof(struct bwi_desc32);
		txrx_ctrl_step = 0x20;

		sc->sc_init_tx_ring = bwi_init_tx_ring32;
		sc->sc_free_tx_ring = bwi_free_tx_ring32;
		sc->sc_init_rx_ring = bwi_init_rx_ring32;
		sc->sc_free_rx_ring = bwi_free_rx_ring32;
		if (sc->sc_newbuf == NULL)
			sc->sc_newbuf = bwi_newbuf;
		sc->sc_setup_rxdesc = bwi_setup_rx_desc32;
		sc->sc_setup_txdesc = bwi_setup_tx_desc32;
		sc->sc_rxeof = bwi_rxeof32;
		sc->sc_start_tx = bwi_start_tx32;
		if (has_txstats) {
			sc->sc_init_txstats = bwi_init_txstats32;
			sc->sc_free_txstats = bwi_free_txstats32;
			sc->sc_txeof_status = bwi_txeof_status32;
		}
		break;

	default:
		panic("unsupported bus space type %d", sc->sc_bus_space);
	}

	KASSERT(desc_sz != 0);
	KASSERT(txrx_ctrl_step != 0);

	tx_ring_sz = roundup(desc_sz * BWI_TX_NDESC, BWI_RING_ALIGN);
	rx_ring_sz = roundup(desc_sz * BWI_RX_NDESC, BWI_RING_ALIGN);

	s = splvm();

#define TXRX_CTRL(idx)	(BWI_TXRX_CTRL_BASE + (idx) * txrx_ctrl_step)
	/*
	 * Create TX ring DMA stuffs
	 */
	for (i = 0; i < BWI_TX_NRING; ++i) {
		error = bwi_dma_ring_alloc(sc,
		    &sc->sc_tx_rdata[i], tx_ring_sz, TXRX_CTRL(i));
		if (error) {
			printf("%s: %dth TX ring DMA alloc failed\n",
			    sc->sc_dev.dv_xname, i);
			bwi_dma_free(sc);
			splx(s);
			return (error);
		}
	}

	/*
	 * Create RX ring DMA stuffs
	 */
	error = bwi_dma_ring_alloc(sc, &sc->sc_rx_rdata,
	    rx_ring_sz, TXRX_CTRL(0));
	if (error) {
		printf("%s: RX ring DMA alloc failed\n", sc->sc_dev.dv_xname);
		bwi_dma_free(sc);
		splx(s);
		return (error);
	}

	if (has_txstats) {
		error = bwi_dma_txstats_alloc(sc, TXRX_CTRL(3), desc_sz);
		if (error) {
			printf("%s: TX stats DMA alloc failed\n",
			    sc->sc_dev.dv_xname);
			bwi_dma_free(sc);
			splx(s);
			return (error);
		}
	}
#undef TXRX_CTRL

	if (sc->sc_bus_space == BWI_BUS_SPACE_30BIT)
		error = bwi_dma_mbuf_create30(sc);
	else
		error = bwi_dma_mbuf_create(sc);
	if (error)
		bwi_dma_free(sc);

	splx(s);

	return (error);
}

void
bwi_dma_free(struct bwi_softc *sc)
{
	struct bwi_ring_data *rd;
	int i;

	for (i = 0; i < BWI_TX_NRING; ++i) {
		rd = &sc->sc_tx_rdata[i];

		if (rd->rdata_desc != NULL) {
			bus_dmamap_unload(sc->sc_dmat, rd->rdata_dmap);
			km_free(rd->rdata_desc, rd->rdata_ring_sz,
			    &kv_intrsafe, &bwi_pa_mode);
			rd->rdata_desc = NULL;
		}
	}

	rd = &sc->sc_rx_rdata;

	if (rd->rdata_desc != NULL) {
		bus_dmamap_unload(sc->sc_dmat, rd->rdata_dmap);
		km_free(rd->rdata_desc, rd->rdata_ring_sz,
		    &kv_intrsafe, &bwi_pa_mode);
		rd->rdata_desc = NULL;
	}

	bwi_dma_txstats_free(sc);

	if (sc->sc_bus_space == BWI_BUS_SPACE_30BIT) {
		for (i = 0; i < BWI_TX_NRING; ++i) {
			if (sc->sc_bounce_tx_data[i] != NULL) {
				km_free(sc->sc_bounce_tx_data[i],
				    BWI_TX_NDESC * MCLBYTES,
				    &kv_intrsafe, &bwi_pa_mode);
				sc->sc_bounce_tx_data[i] = NULL;
			}
		}

		if (sc->sc_bounce_rx_data != NULL) {
			km_free(sc->sc_bounce_rx_data, BWI_RX_NDESC * MCLBYTES,
			    &kv_intrsafe, &bwi_pa_mode);
			sc->sc_bounce_rx_data = NULL;
		}
	}
}

int
bwi_dma_ring_alloc(struct bwi_softc *sc,
    struct bwi_ring_data *rd, bus_size_t size, uint32_t txrx_ctrl)
{
	int error;

	/* Allocate rings below 1GB so 30bit devices can access them.*/
	rd->rdata_desc = (caddr_t)km_alloc(size, &kv_intrsafe, &bwi_pa_mode,
	    &kd_nowait);
	if (rd->rdata_desc == NULL) {
		printf(": could not allocate ring DMA memory\n");
		return (ENOMEM);
	}

	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &rd->rdata_dmap);
	if (error) {
		printf(": cannot create ring DMA map (error %d)\n", error);
		km_free(rd->rdata_desc, size, &kv_intrsafe, &bwi_pa_mode);
		rd->rdata_desc = NULL;
		return (error);
	}

	error = bus_dmamap_load(sc->sc_dmat, rd->rdata_dmap, rd->rdata_desc,
	    size, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: can't load DMA mem\n", sc->sc_dev.dv_xname);
		bus_dmamap_destroy(sc->sc_dmat, rd->rdata_dmap);
		km_free(rd->rdata_desc, size, &kv_intrsafe, &bwi_pa_mode);
		rd->rdata_desc = NULL;
		return (error);
	}

	rd->rdata_ring_sz = size;
	rd->rdata_paddr = rd->rdata_dmap->dm_segs[0].ds_addr;
	rd->rdata_txrx_ctrl = txrx_ctrl;

	return (0);
}

int
bwi_dma_txstats_alloc(struct bwi_softc *sc, uint32_t ctrl_base,
    bus_size_t desc_sz)
{
	struct bwi_txstats_data *st;
	bus_size_t dma_size;
	int error, nsegs;

	st = malloc(sizeof(*st), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_txstats = st;

	/*
	 * Create TX stats descriptor DMA stuffs
	 */
	dma_size = roundup(desc_sz * BWI_TXSTATS_NDESC, BWI_RING_ALIGN);

	error = bus_dmamap_create(sc->sc_dmat, dma_size, 1, dma_size, 0,
	    BUS_DMA_NOWAIT, &st->stats_ring_dmap);
	if (error) {
		printf("%s: can't create txstats ring DMA mem\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_alloc(sc->sc_dmat, dma_size, BWI_RING_ALIGN, 0,
	     &st->stats_ring_seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error) {
		printf("%s: can't allocate txstats ring DMA mem\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_map(sc->sc_dmat, &st->stats_ring_seg, nsegs,
	    dma_size, (caddr_t *)&st->stats_ring, BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: can't map txstats ring DMA mem\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamap_load(sc->sc_dmat, st->stats_ring_dmap,
	    st->stats_ring, dma_size, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: can't load txstats ring DMA mem\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat, &st->stats_ring_seg, nsegs);
		return (error);
	}

	st->stats_ring_paddr = st->stats_ring_dmap->dm_segs[0].ds_addr;

	/*
	 * Create TX stats DMA stuffs
	 */
	dma_size = roundup(sizeof(struct bwi_txstats) * BWI_TXSTATS_NDESC,
	    BWI_ALIGN);

	error = bus_dmamap_create(sc->sc_dmat, dma_size, 1, dma_size, 0,
	    BUS_DMA_NOWAIT, &st->stats_dmap);
	if (error) {
		printf("%s: can't create txstats ring DMA mem\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}
	error = bus_dmamem_alloc(sc->sc_dmat, dma_size, BWI_ALIGN, 0,
	    &st->stats_seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error) {
		printf("%s: can't allocate txstats DMA mem\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_map(sc->sc_dmat, &st->stats_seg, nsegs,
	    dma_size, (caddr_t *)&st->stats, BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: can't map txstats DMA mem\n", sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamap_load(sc->sc_dmat, st->stats_dmap, st->stats,
	    dma_size, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: can't load txstats DMA mem\n", sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat, &st->stats_seg, nsegs);
		return (error);
	}

	st->stats_paddr = st->stats_dmap->dm_segs[0].ds_addr;
	st->stats_ctrl_base = ctrl_base;

	return (0);
}

void
bwi_dma_txstats_free(struct bwi_softc *sc)
{
	struct bwi_txstats_data *st;

	if (sc->sc_txstats == NULL)
		return;
	st = sc->sc_txstats;

	bus_dmamap_unload(sc->sc_dmat, st->stats_ring_dmap);
	bus_dmamem_free(sc->sc_dmat, &st->stats_ring_seg, 1);

	bus_dmamap_unload(sc->sc_dmat, st->stats_dmap);
	bus_dmamem_free(sc->sc_dmat, &st->stats_seg, 1);

	free(st, M_DEVBUF, sizeof *st);
}

int
bwi_dma_mbuf_create30(struct bwi_softc *sc)
{
	int i, j, k, error;

	for (i = 0; i < BWI_TX_NRING; ++i) {
		struct bwi_txbuf_data *tbd = &sc->sc_tx_bdata[i];

		sc->sc_bounce_tx_data[i] = (caddr_t)km_alloc(
		    BWI_TX_NDESC * MCLBYTES, &kv_intrsafe,
		    &bwi_pa_mode, &kd_waitok);
		if (sc->sc_bounce_tx_data[i] == NULL) {
			printf(": could not allocate TX mbuf bounce buffer\n");
			error = ENOMEM;
			break;
		}

		for (j = 0; j < BWI_TX_NDESC; ++j) {
			error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
			    1, MCLBYTES, 0, BUS_DMA_NOWAIT,
			    &tbd->tbd_buf[j].tb_dmap);
			if (error) {
				printf(": cannot create TX mbuf DMA map\n");
				for (k = 0; k < j; ++k) {
					bus_dmamap_destroy(sc->sc_dmat,
					    tbd->tbd_buf[k].tb_dmap);
				}
				break;
			}
		}
	}
	if (error) {
		bwi_dma_mbuf_destroy(sc, i, 0);
		for (j = 0; j < i; ++j)
			km_free(sc->sc_bounce_tx_data[j], BWI_TX_NDESC,
			    &kv_intrsafe, &bwi_pa_mode);
		return (error);
	}

	for (i = 0; i < BWI_TX_NRING; ++i) {
		struct bwi_txbuf_data *tbd = &sc->sc_tx_bdata[i];

		for (j = 0; j < BWI_TX_NDESC; ++j) {
			struct bwi_txbuf *tb = &tbd->tbd_buf[j];

			error = bus_dmamap_load(sc->sc_dmat, tb->tb_dmap,
			    sc->sc_bounce_tx_data[i] + (MCLBYTES * j),
			    MCLBYTES, NULL, BUS_DMA_NOWAIT);
			if (error) {
				printf(": cannot create TX mbuf DMA map\n");
				for (k = 0; k < j; ++k) {
					bus_dmamap_destroy(sc->sc_dmat,
					    tbd->tbd_buf[k].tb_dmap);
				}
				break;
			}
		}
	}
	if (error) {
		bwi_dma_mbuf_destroy(sc, BWI_TX_NRING, 0);
		for (i = 0; i < BWI_TX_NRING; ++i)
			km_free(sc->sc_bounce_tx_data[i], BWI_TX_NDESC,
			    &kv_intrsafe, &bwi_pa_mode);
		return (error);
	}

	sc->sc_bounce_rx_data = (caddr_t)km_alloc(BWI_RX_NDESC * MCLBYTES,
	    &kv_intrsafe, &bwi_pa_mode, &kd_waitok);
	if (sc->sc_bounce_rx_data == NULL) {
		printf(": could not allocate RX mbuf bounce buffer\n");
		bwi_dma_mbuf_destroy(sc, BWI_TX_NRING, 0);
		for (i = 0; i < BWI_TX_NRING; ++i)
			km_free(sc->sc_bounce_tx_data[i], BWI_TX_NDESC,
			    &kv_intrsafe, &bwi_pa_mode);
		return (ENOMEM);
	}

	for (i = 0; i < BWI_RX_NDESC; ++i) {
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &sc->sc_rx_bdata.rbd_buf[i].rb_dmap);
		if (error) {
			printf(": cannot create RX mbuf DMA map\n");
			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(sc->sc_dmat,
				    sc->sc_rx_bdata.rbd_buf[j].rb_dmap);
			}
			break;
		}
	}
	if (error) {
		bwi_dma_mbuf_destroy(sc, BWI_TX_NRING, 0);
		for (i = 0; i < BWI_TX_NRING; ++i)
			km_free(sc->sc_bounce_tx_data[i], BWI_TX_NDESC,
			    &kv_intrsafe, &bwi_pa_mode);
		km_free(sc->sc_bounce_rx_data, BWI_RX_NDESC * MCLBYTES,
		    &kv_intrsafe, &bwi_pa_mode);
		return (error);
	}

	for (i = 0; i < BWI_RX_NDESC; ++i) {
		error = bwi_newbuf30(sc, i, 1);
		if (error) {
			printf(": cannot create RX mbuf DMA map\n");
			break;
		}
	}
	if (error) {
		bwi_dma_mbuf_destroy(sc, BWI_TX_NRING, 1);
		for (i = 0; i < BWI_TX_NRING; ++i)
			km_free(sc->sc_bounce_tx_data[i], BWI_TX_NDESC,
			    &kv_intrsafe, &bwi_pa_mode);
		km_free(sc->sc_bounce_rx_data, BWI_RX_NDESC * MCLBYTES,
		    &kv_intrsafe, &bwi_pa_mode);
		return (error);
	}

	return (0);
}

int
bwi_dma_mbuf_create(struct bwi_softc *sc)
{
	struct bwi_rxbuf_data *rbd = &sc->sc_rx_bdata;
	int i, j, k, ntx, error;

	ntx = 0;

	/*
	 * Create TX mbuf DMA map
	 */
	for (i = 0; i < BWI_TX_NRING; ++i) {
		struct bwi_txbuf_data *tbd = &sc->sc_tx_bdata[i];

		for (j = 0; j < BWI_TX_NDESC; ++j) {
			error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
			    0, BUS_DMA_NOWAIT, &tbd->tbd_buf[j].tb_dmap);
			if (error) {
				printf(
				    "%s: can't create %dth tbd, %dth DMA map\n",
				    sc->sc_dev.dv_xname, i, j);
				ntx = i;
				for (k = 0; k < j; ++k) {
					bus_dmamap_destroy(sc->sc_dmat,
					    tbd->tbd_buf[k].tb_dmap);
				}
				goto fail;
			}
		}
	}
	ntx = BWI_TX_NRING;

	/*
	 * Create RX mbuf DMA map and a spare DMA map
	 */
	error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES, 0,
	    BUS_DMA_NOWAIT, &rbd->rbd_tmp_dmap);
	if (error) {
		printf("%s: can't create spare RX buf DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	for (j = 0; j < BWI_RX_NDESC; ++j) {
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES, 0,
		    BUS_DMA_NOWAIT, &rbd->rbd_buf[j].rb_dmap);
		if (error) {
			printf("%s: can't create %dth RX buf DMA map\n",
			    sc->sc_dev.dv_xname, j);

			for (k = 0; k < j; ++k) {
				bus_dmamap_destroy(sc->sc_dmat,
				    rbd->rbd_buf[k].rb_dmap);
			}
			bus_dmamap_destroy(sc->sc_dmat,
			    rbd->rbd_tmp_dmap);
			goto fail;
		}
	}

	return 0;
fail:
	bwi_dma_mbuf_destroy(sc, ntx, 0);

	return (error);
}

void
bwi_dma_mbuf_destroy(struct bwi_softc *sc, int ntx, int nrx)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int i, j;

	for (i = 0; i < ntx; ++i) {
		struct bwi_txbuf_data *tbd = &sc->sc_tx_bdata[i];

		for (j = 0; j < BWI_TX_NDESC; ++j) {
			struct bwi_txbuf *tb = &tbd->tbd_buf[j];

			if (tb->tb_mbuf != NULL) {
				bus_dmamap_unload(sc->sc_dmat,
				    tb->tb_dmap);
				m_freem(tb->tb_mbuf);
			}
			if (tb->tb_ni != NULL)
				ieee80211_release_node(ic, tb->tb_ni);
			bus_dmamap_destroy(sc->sc_dmat, tb->tb_dmap);
		}
	}

	if (nrx) {
		struct bwi_rxbuf_data *rbd = &sc->sc_rx_bdata;

		bus_dmamap_destroy(sc->sc_dmat, rbd->rbd_tmp_dmap);
		for (j = 0; j < BWI_RX_NDESC; ++j) {
			struct bwi_rxbuf *rb = &rbd->rbd_buf[j];

			if (rb->rb_mbuf != NULL) {
				bus_dmamap_unload(sc->sc_dmat,
						  rb->rb_dmap);
				m_freem(rb->rb_mbuf);
			}
			bus_dmamap_destroy(sc->sc_dmat, rb->rb_dmap);
		}
	}
}

void
bwi_enable_intrs(struct bwi_softc *sc, uint32_t enable_intrs)
{
	CSR_SETBITS_4(sc, BWI_MAC_INTR_MASK, enable_intrs);
}

void
bwi_disable_intrs(struct bwi_softc *sc, uint32_t disable_intrs)
{
	CSR_CLRBITS_4(sc, BWI_MAC_INTR_MASK, disable_intrs);
}

int
bwi_init_tx_ring32(struct bwi_softc *sc, int ring_idx)
{
	struct bwi_ring_data *rd;
	struct bwi_txbuf_data *tbd;
	uint32_t val, addr_hi, addr_lo;

	KASSERT(ring_idx < BWI_TX_NRING);
	rd = &sc->sc_tx_rdata[ring_idx];
	tbd = &sc->sc_tx_bdata[ring_idx];

	tbd->tbd_idx = 0;
	tbd->tbd_used = 0;

	bzero(rd->rdata_desc, sizeof(struct bwi_desc32) * BWI_TX_NDESC);
	bus_dmamap_sync(sc->sc_dmat, rd->rdata_dmap, 0,
	    rd->rdata_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	addr_lo = __SHIFTOUT(rd->rdata_paddr, BWI_TXRX32_RINGINFO_ADDR_MASK);
	addr_hi = __SHIFTOUT(rd->rdata_paddr, BWI_TXRX32_RINGINFO_FUNC_MASK);

	val = __SHIFTIN(addr_lo, BWI_TXRX32_RINGINFO_ADDR_MASK) |
	    __SHIFTIN(BWI_TXRX32_RINGINFO_FUNC_TXRX,
	    BWI_TXRX32_RINGINFO_FUNC_MASK);
	CSR_WRITE_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_RINGINFO, val);

	val = __SHIFTIN(addr_hi, BWI_TXRX32_CTRL_ADDRHI_MASK) |
	      BWI_TXRX32_CTRL_ENABLE;
	CSR_WRITE_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_CTRL, val);

	return (0);
}

void
bwi_init_rxdesc_ring32(struct bwi_softc *sc, uint32_t ctrl_base,
    bus_addr_t paddr, int hdr_size, int ndesc)
{
	uint32_t val, addr_hi, addr_lo;

	addr_lo = __SHIFTOUT(paddr, BWI_TXRX32_RINGINFO_ADDR_MASK);
	addr_hi = __SHIFTOUT(paddr, BWI_TXRX32_RINGINFO_FUNC_MASK);

	val = __SHIFTIN(addr_lo, BWI_TXRX32_RINGINFO_ADDR_MASK) |
	    __SHIFTIN(BWI_TXRX32_RINGINFO_FUNC_TXRX,
	      		BWI_TXRX32_RINGINFO_FUNC_MASK);
	CSR_WRITE_4(sc, ctrl_base + BWI_RX32_RINGINFO, val);

	val = __SHIFTIN(hdr_size, BWI_RX32_CTRL_HDRSZ_MASK) |
	    __SHIFTIN(addr_hi, BWI_TXRX32_CTRL_ADDRHI_MASK) |
	    BWI_TXRX32_CTRL_ENABLE;
	CSR_WRITE_4(sc, ctrl_base + BWI_RX32_CTRL, val);

	CSR_WRITE_4(sc, ctrl_base + BWI_RX32_INDEX,
	    (ndesc - 1) * sizeof(struct bwi_desc32));
}

int
bwi_init_rx_ring32(struct bwi_softc *sc)
{
	struct bwi_ring_data *rd = &sc->sc_rx_rdata;
	int i, error;

	sc->sc_rx_bdata.rbd_idx = 0;
	bzero(rd->rdata_desc, sizeof(struct bwi_desc32) * BWI_RX_NDESC);

	for (i = 0; i < BWI_RX_NDESC; ++i) {
		error = sc->sc_newbuf(sc, i, 1);
		if (error) {
			printf("%s: can't allocate %dth RX buffer\n",
			    sc->sc_dev.dv_xname, i);
			return (error);
		}
	}
	bus_dmamap_sync(sc->sc_dmat, rd->rdata_dmap, 0,
	    rd->rdata_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	bwi_init_rxdesc_ring32(sc, rd->rdata_txrx_ctrl, rd->rdata_paddr,
	    sizeof(struct bwi_rxbuf_hdr), BWI_RX_NDESC);
	return (0);
}

int
bwi_init_txstats32(struct bwi_softc *sc)
{
	struct bwi_txstats_data *st = sc->sc_txstats;
	bus_addr_t stats_paddr;
	int i;

	bzero(st->stats, BWI_TXSTATS_NDESC * sizeof(struct bwi_txstats));
	bus_dmamap_sync(sc->sc_dmat, st->stats_dmap, 0,
	    st->stats_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	st->stats_idx = 0;

	stats_paddr = st->stats_paddr;
	for (i = 0; i < BWI_TXSTATS_NDESC; ++i) {
		bwi_setup_desc32(sc, st->stats_ring, BWI_TXSTATS_NDESC, i,
				 stats_paddr, sizeof(struct bwi_txstats), 0);
		stats_paddr += sizeof(struct bwi_txstats);
	}
	bus_dmamap_sync(sc->sc_dmat, st->stats_ring_dmap, 0,
	    st->stats_ring_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	bwi_init_rxdesc_ring32(sc, st->stats_ctrl_base,
	    st->stats_ring_paddr, 0, BWI_TXSTATS_NDESC);

	return (0);
}

void
bwi_setup_rx_desc32(struct bwi_softc *sc, int buf_idx, bus_addr_t paddr,
    int buf_len)
{
	struct bwi_ring_data *rd = &sc->sc_rx_rdata;

	KASSERT(buf_idx < BWI_RX_NDESC);
	bwi_setup_desc32(sc, rd->rdata_desc, BWI_RX_NDESC, buf_idx,
	    paddr, buf_len, 0);
}

void
bwi_setup_tx_desc32(struct bwi_softc *sc, struct bwi_ring_data *rd,
    int buf_idx, bus_addr_t paddr, int buf_len)
{
	KASSERT(buf_idx < BWI_TX_NDESC);
	bwi_setup_desc32(sc, rd->rdata_desc, BWI_TX_NDESC, buf_idx,
	    paddr, buf_len, 1);
}

int
bwi_newbuf30(struct bwi_softc *sc, int buf_idx, int init)
{
	struct bwi_rxbuf_data *rbd = &sc->sc_rx_bdata;
	struct bwi_rxbuf *rb = &rbd->rbd_buf[buf_idx];
	struct mbuf *m;
	struct bwi_rxbuf_hdr *hdr;
	int error;

	KASSERT(buf_idx < BWI_RX_NDESC);

	/* Create host-side mbuf. */
	MGETHDR(m, init ? M_WAITOK : M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	MCLGET(m, init ? M_WAITOK : M_NOWAIT);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	if (init) {
		/* Load device-side RX DMA buffer. */
		error = bus_dmamap_load(sc->sc_dmat, rb->rb_dmap,
		    sc->sc_bounce_rx_data + (MCLBYTES * buf_idx),
		    MCLBYTES, NULL, BUS_DMA_WAITOK);
		if (error) {
			m_freem(m);
			return (error);
		}
	}

	rb->rb_mbuf = m;
	rb->rb_paddr = rb->rb_dmap->dm_segs[0].ds_addr;

	/*
	 * Clear RX buf header
	 */
	hdr = (struct bwi_rxbuf_hdr *)(sc->sc_bounce_rx_data +
	    (MCLBYTES * buf_idx));
	bzero(hdr, sizeof(*hdr));
	bus_dmamap_sync(sc->sc_dmat, rb->rb_dmap, 0,
	    rb->rb_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	/*
	 * Setup RX buf descriptor
	 */
	sc->sc_setup_rxdesc(sc, buf_idx, rb->rb_paddr,
	    m->m_len - sizeof(*hdr));

	return (0);
}

int
bwi_newbuf(struct bwi_softc *sc, int buf_idx, int init)
{
	struct bwi_rxbuf_data *rbd = &sc->sc_rx_bdata;
	struct bwi_rxbuf *rxbuf = &rbd->rbd_buf[buf_idx];
	struct bwi_rxbuf_hdr *hdr;
	bus_dmamap_t map;
	bus_addr_t paddr;
	struct mbuf *m;
	int error;

	KASSERT(buf_idx < BWI_RX_NDESC);

	MGETHDR(m, init ? M_WAITOK : M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	MCLGET(m, init ? M_WAITOK : M_DONTWAIT);
	if (m == NULL) {
		error = ENOBUFS;

		/*
		 * If the NIC is up and running, we need to:
		 * - Clear RX buffer's header.
		 * - Restore RX descriptor settings.
		 */
		if (init)
			return error;
		else
			goto back;
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	/*
	 * Try to load RX buf into temporary DMA map
	 */
	error = bus_dmamap_load_mbuf(sc->sc_dmat, rbd->rbd_tmp_dmap, m,
	    init ? BUS_DMA_WAITOK : BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);

		/*
		 * See the comment above
		 */
		if (init)
			return error;
		else
			goto back;
	}

	if (!init)
		bus_dmamap_unload(sc->sc_dmat, rxbuf->rb_dmap);
	rxbuf->rb_mbuf = m;

	/*
	 * Swap RX buf's DMA map with the loaded temporary one
	 */
	map = rxbuf->rb_dmap;
	rxbuf->rb_dmap = rbd->rbd_tmp_dmap;
	rbd->rbd_tmp_dmap = map;
	paddr = rxbuf->rb_dmap->dm_segs[0].ds_addr;
	rxbuf->rb_paddr = paddr;

back:
	/*
	 * Clear RX buf header
	 */
	hdr = mtod(rxbuf->rb_mbuf, struct bwi_rxbuf_hdr *);
	bzero(hdr, sizeof(*hdr));
	bus_dmamap_sync(sc->sc_dmat, rxbuf->rb_dmap, 0,
	    rxbuf->rb_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	/*
	 * Setup RX buf descriptor
	 */
	sc->sc_setup_rxdesc(sc, buf_idx, rxbuf->rb_paddr,
	    rxbuf->rb_mbuf->m_len - sizeof(*hdr));
	return error;
}

void
bwi_set_addr_filter(struct bwi_softc *sc, uint16_t addr_ofs,
    const uint8_t *addr)
{
	int i;

	CSR_WRITE_2(sc, BWI_ADDR_FILTER_CTRL,
	    BWI_ADDR_FILTER_CTRL_SET | addr_ofs);

	for (i = 0; i < (IEEE80211_ADDR_LEN / 2); ++i) {
		uint16_t addr_val;

		addr_val = (uint16_t)addr[i * 2] |
		    (((uint16_t)addr[(i * 2) + 1]) << 8);
		CSR_WRITE_2(sc, BWI_ADDR_FILTER_DATA, addr_val);
	}
}

int
bwi_set_chan(struct bwi_softc *sc, uint8_t chan)
{
	struct bwi_mac *mac;

	KASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	bwi_rf_set_chan(mac, chan, 0);

	return (0);
}

void
bwi_next_scan(void *xsc)
{
	struct bwi_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	s = splnet();

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);

	splx(s);
}

int
bwi_rxeof(struct bwi_softc *sc, int end_idx)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct bwi_ring_data *rd = &sc->sc_rx_rdata;
	struct bwi_rxbuf_data *rbd = &sc->sc_rx_bdata;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int idx, rx_data = 0;

	idx = rbd->rbd_idx;
	while (idx != end_idx) {
		struct bwi_rxbuf *rb = &rbd->rbd_buf[idx];
		struct bwi_rxbuf_hdr *hdr;
		struct ieee80211_frame *wh;
		struct ieee80211_rxinfo rxi;
		struct ieee80211_node *ni;
		struct mbuf *m;
		uint32_t plcp;
		uint16_t flags2;
		int buflen, wh_ofs, hdr_extra, rssi, type, rate;

		bus_dmamap_sync(sc->sc_dmat, rb->rb_dmap, 0,
		    rb->rb_dmap->dm_mapsize, BUS_DMASYNC_POSTREAD);

		if (sc->sc_bus_space == BWI_BUS_SPACE_30BIT) {
			/* Bounce for 30bit devices. */
			if (m_copyback(rb->rb_mbuf, 0, MCLBYTES,
			    sc->sc_bounce_rx_data + (MCLBYTES * idx),
			    M_NOWAIT) == ENOBUFS) {
				ifp->if_ierrors++;
				goto next;
			}
		}

		m = rb->rb_mbuf;

		if (sc->sc_newbuf(sc, idx, 0)) {
			ifp->if_ierrors++;
			goto next;
		}

		hdr = mtod(m, struct bwi_rxbuf_hdr *);
		flags2 = letoh16(hdr->rxh_flags2);

		hdr_extra = 0;
		if (flags2 & BWI_RXH_F2_TYPE2FRAME)
			hdr_extra = 2;
		wh_ofs = hdr_extra + 6;

		buflen = letoh16(hdr->rxh_buflen);
		if (buflen <= wh_ofs) {
			printf("%s: zero length data, hdr_extra %d\n",
			    sc->sc_dev.dv_xname, hdr_extra);
			ifp->if_ierrors++;
			m_freem(m);
			goto next;
		}

		bcopy((uint8_t *)(hdr + 1) + hdr_extra, &plcp, sizeof(plcp));
		rssi = bwi_calc_rssi(sc, hdr);

		m->m_len = m->m_pkthdr.len = buflen + sizeof(*hdr);
		m_adj(m, sizeof(*hdr) + wh_ofs);

		if (htole16(hdr->rxh_flags1) & BWI_RXH_F1_OFDM)
			rate = bwi_plcp2rate(plcp, IEEE80211_MODE_11G);
		else
			rate = bwi_plcp2rate(plcp, IEEE80211_MODE_11B);

#if NBPFILTER > 0
		/* RX radio tap */
		if (sc->sc_drvbpf != NULL) {
			struct mbuf mb;
			struct bwi_rx_radiotap_hdr *tap = &sc->sc_rxtap;

			tap->wr_tsf = hdr->rxh_tsf;
			tap->wr_flags = IEEE80211_RADIOTAP_F_FCS;
			tap->wr_rate = rate;
			tap->wr_chan_freq =
			    htole16(ic->ic_bss->ni_chan->ic_freq);
			tap->wr_chan_flags =
			    htole16(ic->ic_bss->ni_chan->ic_flags);
			tap->wr_antsignal = rssi;
			tap->wr_antnoise = BWI_NOISE_FLOOR;

			mb.m_data = (caddr_t)tap;
			mb.m_len = sc->sc_rxtap_len;
			mb.m_next = m;
			mb.m_nextpkt = NULL;
			mb.m_type = 0;
			mb.m_flags = 0;
			bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_IN);
		}
#endif

		m_adj(m, -IEEE80211_CRC_LEN);

		wh = mtod(m, struct ieee80211_frame *);
		ni = ieee80211_find_rxnode(ic, wh);
		type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

		memset(&rxi, 0, sizeof(rxi));
		rxi.rxi_rssi = hdr->rxh_rssi;
		rxi.rxi_tstamp = letoh16(hdr->rxh_tsf);
		ieee80211_inputm(ifp, m, ni, &rxi, &ml);

		ieee80211_release_node(ic, ni);

		if (type == IEEE80211_FC0_TYPE_DATA) {
			rx_data = 1;
			sc->sc_rx_rate = rate;
		}
next:
		idx = (idx + 1) % BWI_RX_NDESC;
	}
	if_input(ifp, &ml);

	rbd->rbd_idx = idx;
	bus_dmamap_sync(sc->sc_dmat, rd->rdata_dmap, 0,
	    rd->rdata_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	return (rx_data);
}

int
bwi_rxeof32(struct bwi_softc *sc)
{
	uint32_t val, rx_ctrl;
	int end_idx, rx_data;

	rx_ctrl = sc->sc_rx_rdata.rdata_txrx_ctrl;

	val = CSR_READ_4(sc, rx_ctrl + BWI_RX32_STATUS);
	end_idx = __SHIFTOUT(val, BWI_RX32_STATUS_INDEX_MASK) /
	    sizeof(struct bwi_desc32);

	rx_data = bwi_rxeof(sc, end_idx);

	CSR_WRITE_4(sc, rx_ctrl + BWI_RX32_INDEX,
	    end_idx * sizeof(struct bwi_desc32));

	return (rx_data);
}

void
bwi_reset_rx_ring32(struct bwi_softc *sc, uint32_t rx_ctrl)
{
	int i;

	CSR_WRITE_4(sc, rx_ctrl + BWI_RX32_CTRL, 0);

#define NRETRY 10
	for (i = 0; i < NRETRY; ++i) {
		uint32_t status;

		status = CSR_READ_4(sc, rx_ctrl + BWI_RX32_STATUS);
		if (__SHIFTOUT(status, BWI_RX32_STATUS_STATE_MASK) ==
		    BWI_RX32_STATUS_STATE_DISABLED)
			break;

		DELAY(1000);
	}
	if (i == NRETRY)
		printf("%s: reset rx ring timedout\n", sc->sc_dev.dv_xname);
#undef NRETRY

	CSR_WRITE_4(sc, rx_ctrl + BWI_RX32_RINGINFO, 0);
}

void
bwi_free_txstats32(struct bwi_softc *sc)
{
	bwi_reset_rx_ring32(sc, sc->sc_txstats->stats_ctrl_base);
}

void
bwi_free_rx_ring32(struct bwi_softc *sc)
{
	struct bwi_ring_data *rd = &sc->sc_rx_rdata;
	struct bwi_rxbuf_data *rbd = &sc->sc_rx_bdata;
	int i;

	bwi_reset_rx_ring32(sc, rd->rdata_txrx_ctrl);

	for (i = 0; i < BWI_RX_NDESC; ++i) {
		struct bwi_rxbuf *rb = &rbd->rbd_buf[i];

		if (rb->rb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, rb->rb_dmap);
			m_freem(rb->rb_mbuf);
			rb->rb_mbuf = NULL;
		}
	}
}

void
bwi_free_tx_ring32(struct bwi_softc *sc, int ring_idx)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct bwi_ring_data *rd;
	struct bwi_txbuf_data *tbd;
	uint32_t state, val;
	int i;

	KASSERT(ring_idx < BWI_TX_NRING);
	rd = &sc->sc_tx_rdata[ring_idx];
	tbd = &sc->sc_tx_bdata[ring_idx];

#define NRETRY 10
	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_STATUS);
		state = __SHIFTOUT(val, BWI_TX32_STATUS_STATE_MASK);
		if (state == BWI_TX32_STATUS_STATE_DISABLED ||
		    state == BWI_TX32_STATUS_STATE_IDLE ||
		    state == BWI_TX32_STATUS_STATE_STOPPED)
			break;

		DELAY(1000);
	}
	if (i == NRETRY) {
		printf("%s: wait for TX ring(%d) stable timed out\n",
		    sc->sc_dev.dv_xname, ring_idx);
	}

	CSR_WRITE_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_CTRL, 0);
	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_STATUS);
		state = __SHIFTOUT(val, BWI_TX32_STATUS_STATE_MASK);
		if (state == BWI_TX32_STATUS_STATE_DISABLED)
			break;

		DELAY(1000);
	}
	if (i == NRETRY)
		printf("%s: reset TX ring (%d) timed out\n",
		    sc->sc_dev.dv_xname, ring_idx);
#undef NRETRY

	DELAY(1000);

	CSR_WRITE_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_RINGINFO, 0);

	for (i = 0; i < BWI_TX_NDESC; ++i) {
		struct bwi_txbuf *tb = &tbd->tbd_buf[i];

		if (tb->tb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, tb->tb_dmap);
			m_freem(tb->tb_mbuf);
			tb->tb_mbuf = NULL;
		}
		if (tb->tb_ni != NULL) {
			ieee80211_release_node(ic, tb->tb_ni);
			tb->tb_ni = NULL;
		}
	}
}

uint8_t
bwi_plcp2rate(uint32_t plcp0, enum ieee80211_phymode phymode)
{
	uint32_t plcp = letoh32(plcp0) & IEEE80211_OFDM_PLCP_RATE_MASK;
	return (ieee80211_plcp2rate(plcp, phymode));
}

void
bwi_ofdm_plcp_header(uint32_t *plcp0, int pkt_len, uint8_t rate)
{
	uint32_t plcp;

	plcp = __SHIFTIN(ieee80211_rate2plcp(rate, IEEE80211_MODE_11G),
	    IEEE80211_OFDM_PLCP_RATE_MASK) |
	    __SHIFTIN(pkt_len, IEEE80211_OFDM_PLCP_LEN_MASK);
	*plcp0 = htole32(plcp);
}

void
bwi_ds_plcp_header(struct ieee80211_ds_plcp_hdr *plcp, int pkt_len,
    uint8_t rate)
{
	int len, service, pkt_bitlen;

	pkt_bitlen = pkt_len * NBBY;
	len = howmany(pkt_bitlen * 2, rate);

	service = IEEE80211_DS_PLCP_SERVICE_LOCKED;
	if (rate == (11 * 2)) {
		int pkt_bitlen1;

		/*
		 * PLCP service field needs to be adjusted,
		 * if TX rate is 11Mbytes/s
		 */
		pkt_bitlen1 = len * 11;
		if (pkt_bitlen1 - pkt_bitlen >= NBBY)
			service |= IEEE80211_DS_PLCP_SERVICE_LENEXT7;
	}

	plcp->i_signal = ieee80211_rate2plcp(rate, IEEE80211_MODE_11B);
	plcp->i_service = service;
	plcp->i_length = htole16(len);
	/* NOTE: do NOT touch i_crc */
}

void
bwi_plcp_header(void *plcp, int pkt_len, uint8_t rate)
{
	enum bwi_modtype modtype;

	/*
	 * Assume caller has zeroed 'plcp'
	 */

	modtype = bwi_rate2modtype(rate);
	if (modtype == IEEE80211_MODTYPE_OFDM)
		bwi_ofdm_plcp_header(plcp, pkt_len, rate);
	else if (modtype == IEEE80211_MODTYPE_DS)
		bwi_ds_plcp_header(plcp, pkt_len, rate);
	else
		panic("unsupported modulation type %u", modtype);
}

enum bwi_modtype
bwi_rate2modtype(uint8_t rate)
{
	rate &= IEEE80211_RATE_VAL;

	if (rate == 44)
		return IEEE80211_MODTYPE_PBCC;
	else if (rate == 22 || rate < 12)
		return IEEE80211_MODTYPE_DS;
	else
		return IEEE80211_MODTYPE_OFDM;
}

uint8_t
bwi_ack_rate(struct ieee80211_node *ni, uint8_t rate)
{
	const struct ieee80211_rateset *rs = &ni->ni_rates;
	uint8_t ack_rate = 0;
	enum bwi_modtype modtype;
	int i;

	rate &= IEEE80211_RATE_VAL;

	modtype = bwi_rate2modtype(rate);

	for (i = 0; i < rs->rs_nrates; ++i) {
		uint8_t rate1 = rs->rs_rates[i] & IEEE80211_RATE_VAL;
		
		if (rate1 > rate) {
			if (ack_rate != 0)
				return ack_rate;
			else
				break;
		}

		if ((rs->rs_rates[i] & IEEE80211_RATE_BASIC) &&
		    bwi_rate2modtype(rate1) == modtype)
			ack_rate = rate1;
	}

	switch (rate) {
	/* CCK */
	case 2:
	case 4:
	case 11:
	case 22:
		ack_rate = rate;
		break;
	/* PBCC */
	case 44:
		ack_rate = 22;
		break;

	/* OFDM */
	case 12:
	case 18:
		ack_rate = 12;
		break;
	case 24:
	case 36:
		ack_rate = 24;
		break;
	case 48:
	case 72:
	case 96:
	case 108:
		ack_rate = 48;
		break;
	default:
		panic("unsupported rate %d", rate);
	}
	return ack_rate;
}

#define IEEE80211_OFDM_TXTIME(kbps, frmlen)	\
	(IEEE80211_OFDM_PREAMBLE_TIME +		\
	 IEEE80211_OFDM_SIGNAL_TIME +		\
	(IEEE80211_OFDM_NSYMS((kbps), (frmlen)) * IEEE80211_OFDM_SYM_TIME))

#define IEEE80211_OFDM_SYM_TIME			4
#define IEEE80211_OFDM_PREAMBLE_TIME		16
#define IEEE80211_OFDM_SIGNAL_EXT_TIME		6
#define IEEE80211_OFDM_SIGNAL_TIME		4

#define IEEE80211_OFDM_PLCP_SERVICE_NBITS	16
#define IEEE80211_OFDM_TAIL_NBITS		6	

#define IEEE80211_OFDM_NBITS(frmlen)		\
	(IEEE80211_OFDM_PLCP_SERVICE_NBITS +	\
	 ((frmlen) * NBBY) +			\
	 IEEE80211_OFDM_TAIL_NBITS)

#define IEEE80211_OFDM_NBITS_PER_SYM(kbps)	\
	(((kbps) * IEEE80211_OFDM_SYM_TIME) / 1000)

#define IEEE80211_OFDM_NSYMS(kbps, frmlen)	\
	howmany(IEEE80211_OFDM_NBITS((frmlen)),	\
	IEEE80211_OFDM_NBITS_PER_SYM((kbps)))

#define IEEE80211_CCK_TXTIME(kbps, frmlen)	\
	(((IEEE80211_CCK_NBITS((frmlen)) * 1000) + (kbps) - 1) / (kbps))

#define IEEE80211_CCK_PREAMBLE_LEN		144
#define IEEE80211_CCK_PLCP_HDR_TIME		48
#define IEEE80211_CCK_SHPREAMBLE_LEN		72
#define IEEE80211_CCK_SHPLCP_HDR_TIME		24

#define IEEE80211_CCK_NBITS(frmlen)		((frmlen) * NBBY)

uint16_t
bwi_txtime(struct ieee80211com *ic, struct ieee80211_node *ni, uint len,
    uint8_t rs_rate, uint32_t flags)
{
	enum bwi_modtype modtype;
	uint16_t txtime;
	int rate;

	rs_rate &= IEEE80211_RATE_VAL;

	rate = rs_rate * 500;	/* ieee80211 rate -> kbps */

	modtype = bwi_rate2modtype(rs_rate);
	if (modtype == IEEE80211_MODTYPE_OFDM) {
		/*
		 * IEEE Std 802.11a-1999, page 37, equation (29)
		 * IEEE Std 802.11g-2003, page 44, equation (42)
		 */
		txtime = IEEE80211_OFDM_TXTIME(rate, len);
		if (ic->ic_curmode == IEEE80211_MODE_11G)
			txtime += IEEE80211_OFDM_SIGNAL_EXT_TIME;
	} else {
		/*
		 * IEEE Std 802.11b-1999, page 28, subclause 18.3.4
		 * IEEE Std 802.11g-2003, page 45, equation (43)
		 */
		if (modtype == IEEE80211_MODTYPE_PBCC)
			++len;
		txtime = IEEE80211_CCK_TXTIME(rate, len);

		/*
		 * Short preamble is not applicable for DS 1Mbits/s
		 */
		if (rs_rate != 2 && (flags & IEEE80211_F_SHPREAMBLE)) {
			txtime += IEEE80211_CCK_SHPREAMBLE_LEN +
				  IEEE80211_CCK_SHPLCP_HDR_TIME;
		} else {
			txtime += IEEE80211_CCK_PREAMBLE_LEN +
				  IEEE80211_CCK_PLCP_HDR_TIME;
		}
	}
	return txtime;
}

int
bwi_encap(struct bwi_softc *sc, int idx, struct mbuf *m,
    struct ieee80211_node *ni)
{
	DPRINTF(2, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	struct ieee80211com *ic = &sc->sc_ic;
	struct bwi_ring_data *rd = &sc->sc_tx_rdata[BWI_TX_DATA_RING];
	struct bwi_txbuf_data *tbd = &sc->sc_tx_bdata[BWI_TX_DATA_RING];
	struct bwi_txbuf *tb = &tbd->tbd_buf[idx];
	struct bwi_mac *mac;
	struct bwi_txbuf_hdr *hdr;
	struct ieee80211_frame *wh;
	uint8_t rate;
	uint32_t mac_ctrl;
	uint16_t phy_ctrl;
	bus_addr_t paddr;
	int pkt_len, error = 0;
#if 0
	const uint8_t *p;
	int i;
#endif

	KASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	wh = mtod(m, struct ieee80211_frame *);

	/* Get 802.11 frame len before prepending TX header */
	pkt_len = m->m_pkthdr.len + IEEE80211_CRC_LEN;

	/*
	 * Find TX rate
	 */
	bzero(tb->tb_rate_idx, sizeof(tb->tb_rate_idx));
	if (ni != NULL) {
		if (ic->ic_fixed_rate != -1) {
			rate = ic->ic_sup_rates[ic->ic_curmode].
			    rs_rates[ic->ic_fixed_rate];
		} else {
			/* AMRR rate control */
			rate = ni->ni_rates.rs_rates[ni->ni_txrate];
		}
	} else {
		/* Fixed at 1Mbytes/s for mgt frames */
		rate = (1 * 2);
	}

	rate &= IEEE80211_RATE_VAL;

	if (IEEE80211_IS_MULTICAST(wh->i_addr1))
		rate = (1 * 2);

	if (rate == 0) {
		printf("%s: invalid rate %u or fallback rate",
		    sc->sc_dev.dv_xname, rate);
		rate = (1 * 2); /* Force 1Mbytes/s */
	}
	sc->sc_tx_rate = rate;

#if NBPFILTER > 0
	/* TX radio tap */
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct bwi_tx_radiotap_hdr *tap = &sc->sc_txtap;

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

	/*
	 * Setup the embedded TX header
	 */
	M_PREPEND(m, sizeof(*hdr), M_DONTWAIT);
	if (m == NULL) {
		printf("%s: prepend TX header failed\n", sc->sc_dev.dv_xname);
		return (ENOBUFS);
	}
	hdr = mtod(m, struct bwi_txbuf_hdr *);

	bzero(hdr, sizeof(*hdr));

	bcopy(wh->i_fc, hdr->txh_fc, sizeof(hdr->txh_fc));
	bcopy(wh->i_addr1, hdr->txh_addr1, sizeof(hdr->txh_addr1));

	if (ni != NULL && !IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		uint16_t dur;
		uint8_t ack_rate;

		ack_rate = bwi_ack_rate(ni, rate);
		dur = bwi_txtime(ic, ni,
		    sizeof(struct ieee80211_frame_ack) + IEEE80211_CRC_LEN,
		    ack_rate, ic->ic_flags & IEEE80211_F_SHPREAMBLE);

		hdr->txh_fb_duration = htole16(dur);
	}

	hdr->txh_id = __SHIFTIN(BWI_TX_DATA_RING, BWI_TXH_ID_RING_MASK) |
	    __SHIFTIN(idx, BWI_TXH_ID_IDX_MASK);

	bwi_plcp_header(hdr->txh_plcp, pkt_len, rate);
	bwi_plcp_header(hdr->txh_fb_plcp, pkt_len, rate);

	phy_ctrl = __SHIFTIN(mac->mac_rf.rf_ant_mode,
	    BWI_TXH_PHY_C_ANTMODE_MASK);
	if (bwi_rate2modtype(rate) == IEEE80211_MODTYPE_OFDM)
		phy_ctrl |= BWI_TXH_PHY_C_OFDM;
	else if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) && rate != (2 * 1))
		phy_ctrl |= BWI_TXH_PHY_C_SHPREAMBLE;

	mac_ctrl = BWI_TXH_MAC_C_HWSEQ | BWI_TXH_MAC_C_FIRST_FRAG;
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1))
		mac_ctrl |= BWI_TXH_MAC_C_ACK;
	if (bwi_rate2modtype(rate) == IEEE80211_MODTYPE_OFDM)
		mac_ctrl |= BWI_TXH_MAC_C_FB_OFDM;

	hdr->txh_mac_ctrl = htole32(mac_ctrl);
	hdr->txh_phy_ctrl = htole16(phy_ctrl);

	/* Catch any further usage */
	hdr = NULL;
	wh = NULL;

	if (sc->sc_bus_space == BWI_BUS_SPACE_30BIT) {
		/* Bounce for 30bit devices. */
		m_copydata(m, 0, m->m_pkthdr.len,
		    sc->sc_bounce_tx_data[BWI_TX_DATA_RING] +
		    (MCLBYTES * idx));
	} else {
		/* DMA load */
		error = bus_dmamap_load_mbuf(sc->sc_dmat, tb->tb_dmap, m,
		    BUS_DMA_NOWAIT);
		if (error && error != EFBIG) {
			printf("%s: can't load TX buffer (1) %d\n",
			    sc->sc_dev.dv_xname, error);
			goto back;
		}

		if (error) {	/* error == EFBIG */
			if (m_defrag(m, M_DONTWAIT)) {
				printf("%s: can't defrag TX buffer\n",
				    sc->sc_dev.dv_xname);
				goto back;
			}
			error = bus_dmamap_load_mbuf(sc->sc_dmat, tb->tb_dmap,
			    m, BUS_DMA_NOWAIT);
			if (error) {
				printf("%s: can't load TX buffer (2) %d\n",
				    sc->sc_dev.dv_xname, error);
				goto back;
			}
		}
		error = 0;
	}

	bus_dmamap_sync(sc->sc_dmat, tb->tb_dmap, 0,
	    tb->tb_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	tb->tb_mbuf = m;
	tb->tb_ni = ni;

#if 0
	p = mtod(m, const uint8_t *);
	for (i = 0; i < m->m_pkthdr.len; ++i) {
		if (i != 0 && i % 8 == 0)
			printf("\n");
		printf("%02x ", p[i]);
	}
	printf("\n");

	DPRINTF(1, "%s: idx %d, pkt_len %d, buflen %d\n",
	    sc->sc_dev.dv_xname, idx, pkt_len, m->m_pkthdr.len);
#endif

	/* Setup TX descriptor */
	paddr = tb->tb_dmap->dm_segs[0].ds_addr;
	sc->sc_setup_txdesc(sc, rd, idx, paddr, m->m_pkthdr.len);
	bus_dmamap_sync(sc->sc_dmat, rd->rdata_dmap, 0,
	    rd->rdata_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	/* Kick start */
	sc->sc_start_tx(sc, rd->rdata_txrx_ctrl, idx);

back:
	if (error)
		m_freem(m);
	return (error);
}

void
bwi_start_tx32(struct bwi_softc *sc, uint32_t tx_ctrl, int idx)
{
	idx = (idx + 1) % BWI_TX_NDESC;
	CSR_WRITE_4(sc, tx_ctrl + BWI_TX32_INDEX,
	    idx * sizeof(struct bwi_desc32));
}

void
bwi_txeof_status32(struct bwi_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t val, ctrl_base;
	int end_idx;

	ctrl_base = sc->sc_txstats->stats_ctrl_base;

	val = CSR_READ_4(sc, ctrl_base + BWI_RX32_STATUS);
	end_idx = __SHIFTOUT(val, BWI_RX32_STATUS_INDEX_MASK) /
	    sizeof(struct bwi_desc32);

	bwi_txeof_status(sc, end_idx);

	CSR_WRITE_4(sc, ctrl_base + BWI_RX32_INDEX,
	    end_idx * sizeof(struct bwi_desc32));

	if (ifq_is_oactive(&ifp->if_snd) == 0)
		ifp->if_start(ifp);
}

void
_bwi_txeof(struct bwi_softc *sc, uint16_t tx_id)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct bwi_txbuf_data *tbd;
	struct bwi_txbuf *tb;
	int ring_idx, buf_idx;

	if (tx_id == 0) {
		printf("%s: zero tx id\n", sc->sc_dev.dv_xname);
		return;
	}

	ring_idx = __SHIFTOUT(tx_id, BWI_TXH_ID_RING_MASK);
	buf_idx = __SHIFTOUT(tx_id, BWI_TXH_ID_IDX_MASK);

	KASSERT(ring_idx == BWI_TX_DATA_RING);
	KASSERT(buf_idx < BWI_TX_NDESC);
#if 0
	DPRINTF(1, "%s: txeof idx %d\n", sc->sc_dev.dv_xname, buf_idx);
#endif
	tbd = &sc->sc_tx_bdata[ring_idx];
	KASSERT(tbd->tbd_used > 0);
	tbd->tbd_used--;

	tb = &tbd->tbd_buf[buf_idx];

	bus_dmamap_unload(sc->sc_dmat, tb->tb_dmap);
	m_freem(tb->tb_mbuf);
	tb->tb_mbuf = NULL;

	if (tb->tb_ni != NULL) {
		ieee80211_release_node(ic, tb->tb_ni);
		tb->tb_ni = NULL;
	}

	if (tbd->tbd_used == 0)
		sc->sc_tx_timer = 0;

	ifq_clr_oactive(&ifp->if_snd);
}

void
bwi_txeof_status(struct bwi_softc *sc, int end_idx)
{
	struct bwi_txstats_data *st = sc->sc_txstats;
	int idx;

	bus_dmamap_sync(sc->sc_dmat, st->stats_dmap, 0,
	    st->stats_dmap->dm_mapsize, BUS_DMASYNC_POSTREAD);

	idx = st->stats_idx;
	while (idx != end_idx) {
		_bwi_txeof(sc, letoh16(st->stats[idx].txs_id));
		idx = (idx + 1) % BWI_TXSTATS_NDESC;
	}
	st->stats_idx = idx;
}

void
bwi_txeof(struct bwi_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	for (;;) {
		uint32_t tx_status0, tx_status1;
		uint16_t tx_id, tx_info;

		tx_status0 = CSR_READ_4(sc, BWI_TXSTATUS_0);
		if (tx_status0 == 0)
			break;
		tx_status1 = CSR_READ_4(sc, BWI_TXSTATUS_1);

		tx_id = __SHIFTOUT(tx_status0, BWI_TXSTATUS_0_TXID_MASK);
		tx_info = BWI_TXSTATUS_0_INFO(tx_status0);

		if (tx_info & 0x30) /* XXX */
			continue;

		_bwi_txeof(sc, letoh16(tx_id));
	}

	if (ifq_is_oactive(&ifp->if_snd) == 0)
		ifp->if_start(ifp);
}

int
bwi_bbp_power_on(struct bwi_softc *sc, enum bwi_clock_mode clk_mode)
{
	bwi_power_on(sc, 1);

	return (bwi_set_clock_mode(sc, clk_mode));
}

void
bwi_bbp_power_off(struct bwi_softc *sc)
{
	bwi_set_clock_mode(sc, BWI_CLOCK_MODE_SLOW);
	bwi_power_off(sc, 1);
}

int
bwi_get_pwron_delay(struct bwi_softc *sc)
{
	struct bwi_regwin *com, *old;
	struct bwi_clock_freq freq;
	uint32_t val;
	int error;

	com = &sc->sc_com_regwin;
	KASSERT(BWI_REGWIN_EXIST(com));

	if ((sc->sc_cap & BWI_CAP_CLKMODE) == 0)
		return (0);

	error = bwi_regwin_switch(sc, com, &old);
	if (error)
		return (error);

	bwi_get_clock_freq(sc, &freq);

	val = CSR_READ_4(sc, BWI_PLL_ON_DELAY);
	sc->sc_pwron_delay = howmany((val + 2) * 1000000, freq.clkfreq_min);
	DPRINTF(1, "%s: power on delay %u\n",
	    sc->sc_dev.dv_xname, sc->sc_pwron_delay);

	return (bwi_regwin_switch(sc, old, NULL));
}

int
bwi_bus_attach(struct bwi_softc *sc)
{
	struct bwi_regwin *bus, *old;
	int error;

	bus = &sc->sc_bus_regwin;

	error = bwi_regwin_switch(sc, bus, &old);
	if (error)
		return (error);

	if (!bwi_regwin_is_enabled(sc, bus))
		bwi_regwin_enable(sc, bus, 0);

	/* Disable interrupts */
	CSR_WRITE_4(sc, BWI_INTRVEC, 0);

	return (bwi_regwin_switch(sc, old, NULL));
}

const char *
bwi_regwin_name(const struct bwi_regwin *rw)
{
	switch (rw->rw_type) {
	case BWI_REGWIN_T_COM:
		return ("COM");
	case BWI_REGWIN_T_BUSPCI:
		return ("PCI");
	case BWI_REGWIN_T_MAC:
		return ("MAC");
	case BWI_REGWIN_T_BUSPCIE:
		return ("PCIE");
	}
	panic("unknown regwin type 0x%04x", rw->rw_type);

	return (NULL);
}

uint32_t
bwi_regwin_disable_bits(struct bwi_softc *sc)
{
	uint32_t busrev;

	/* XXX cache this */
	busrev = __SHIFTOUT(CSR_READ_4(sc, BWI_ID_LO), BWI_ID_LO_BUSREV_MASK);
	DPRINTF(1, "%s: bus rev %u\n", sc->sc_dev.dv_xname, busrev);

	if (busrev == BWI_BUSREV_0)
		return (BWI_STATE_LO_DISABLE1);
	else if (busrev == BWI_BUSREV_1)
		return (BWI_STATE_LO_DISABLE2);
	else
		return ((BWI_STATE_LO_DISABLE1 | BWI_STATE_LO_DISABLE2));
}

int
bwi_regwin_is_enabled(struct bwi_softc *sc, struct bwi_regwin *rw)
{
	uint32_t val, disable_bits;

	disable_bits = bwi_regwin_disable_bits(sc);
	val = CSR_READ_4(sc, BWI_STATE_LO);

	if ((val & (BWI_STATE_LO_CLOCK |
	    BWI_STATE_LO_RESET |
	    disable_bits)) == BWI_STATE_LO_CLOCK) {
		DPRINTF(1, "%s: %s is enabled\n",
		    sc->sc_dev.dv_xname, bwi_regwin_name(rw));
		return (1);
	} else {
		DPRINTF(1, "%s: %s is disabled\n",
		    sc->sc_dev.dv_xname, bwi_regwin_name(rw));
		return (0);
	}
}

void
bwi_regwin_disable(struct bwi_softc *sc, struct bwi_regwin *rw, uint32_t flags)
{
	uint32_t state_lo, disable_bits;
	int i;

	state_lo = CSR_READ_4(sc, BWI_STATE_LO);

	/*
	 * If current regwin is in 'reset' state, it was already disabled.
	 */
	if (state_lo & BWI_STATE_LO_RESET) {
		DPRINTF(1, "%s: %s was already disabled\n",
		    sc->sc_dev.dv_xname, bwi_regwin_name(rw));
		return;
	}

	disable_bits = bwi_regwin_disable_bits(sc);

	/*
	 * Disable normal clock
	 */
	state_lo = BWI_STATE_LO_CLOCK | disable_bits;
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/*
	 * Wait until normal clock is disabled
	 */
#define NRETRY	1000
	for (i = 0; i < NRETRY; ++i) {
		state_lo = CSR_READ_4(sc, BWI_STATE_LO);
		if (state_lo & disable_bits)
			break;
		DELAY(10);
	}
	if (i == NRETRY) {
		printf("%s: %s disable clock timeout\n",
		    sc->sc_dev.dv_xname, bwi_regwin_name(rw));
	}

	for (i = 0; i < NRETRY; ++i) {
		uint32_t state_hi;

		state_hi = CSR_READ_4(sc, BWI_STATE_HI);
		if ((state_hi & BWI_STATE_HI_BUSY) == 0)
			break;
		DELAY(10);
	}
	if (i == NRETRY) {
		printf("%s: %s wait BUSY unset timeout\n",
		    sc->sc_dev.dv_xname, bwi_regwin_name(rw));
	}
#undef NRETRY

	/*
	 * Reset and disable regwin with gated clock
	 */
	state_lo = BWI_STATE_LO_RESET | disable_bits |
	    BWI_STATE_LO_CLOCK | BWI_STATE_LO_GATED_CLOCK |
	    __SHIFTIN(flags, BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1);

	/* Reset and disable regwin */
	state_lo = BWI_STATE_LO_RESET | disable_bits |
		   __SHIFTIN(flags, BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1);
}

void
bwi_regwin_enable(struct bwi_softc *sc, struct bwi_regwin *rw, uint32_t flags)
{
	uint32_t state_lo, state_hi, imstate;

	bwi_regwin_disable(sc, rw, flags);

	/* Reset regwin with gated clock */
	state_lo = BWI_STATE_LO_RESET |
	    BWI_STATE_LO_CLOCK |
	    BWI_STATE_LO_GATED_CLOCK |
	    __SHIFTIN(flags, BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1);

	state_hi = CSR_READ_4(sc, BWI_STATE_HI);
	if (state_hi & BWI_STATE_HI_SERROR)
		CSR_WRITE_4(sc, BWI_STATE_HI, 0);

	imstate = CSR_READ_4(sc, BWI_IMSTATE);
	if (imstate & (BWI_IMSTATE_INBAND_ERR | BWI_IMSTATE_TIMEOUT)) {
		imstate &= ~(BWI_IMSTATE_INBAND_ERR | BWI_IMSTATE_TIMEOUT);
		CSR_WRITE_4(sc, BWI_IMSTATE, imstate);
	}

	/* Enable regwin with gated clock */
	state_lo = BWI_STATE_LO_CLOCK |
	    BWI_STATE_LO_GATED_CLOCK |
	    __SHIFTIN(flags, BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1);

	/* Enable regwin with normal clock */
	state_lo = BWI_STATE_LO_CLOCK |
	    __SHIFTIN(flags, BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1);
}

void
bwi_set_bssid(struct bwi_softc *sc, const uint8_t *bssid)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct bwi_mac *mac;
	struct bwi_myaddr_bssid buf;
	const uint8_t *p;
	uint32_t val;
	int n, i;

	KASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	bwi_set_addr_filter(sc, BWI_ADDR_FILTER_BSSID, bssid);

	bcopy(ic->ic_myaddr, buf.myaddr, sizeof(buf.myaddr));
	bcopy(bssid, buf.bssid, sizeof(buf.bssid));

	n = sizeof(buf) / sizeof(val);
	p = (const uint8_t *)&buf;
	for (i = 0; i < n; ++i) {
		int j;

		val = 0;
		for (j = 0; j < sizeof(val); ++j)
			val |= ((uint32_t)(*p++)) << (j * 8);

		TMPLT_WRITE_4(mac, 0x20 + (i * sizeof(val)), val);
	}
}

void
bwi_updateslot(struct ieee80211com *ic)
{
	struct bwi_softc *sc = ic->ic_if.if_softc;
	struct bwi_mac *mac;
	struct ifnet *ifp = &ic->ic_if;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	DPRINTF(2, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	KASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	bwi_mac_updateslot(mac, (ic->ic_flags & IEEE80211_F_SHSLOT));
}

void
bwi_calibrate(void *xsc)
{
	struct bwi_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s;

	s = splnet();

	if (ic->ic_state == IEEE80211_S_RUN) {
		struct bwi_mac *mac;

		KASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
		mac = (struct bwi_mac *)sc->sc_cur_regwin;

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			bwi_mac_calibrate_txpower(mac, sc->sc_txpwrcb_type);
			sc->sc_txpwrcb_type = BWI_TXPWR_CALIB;
		}

		/* XXX 15 seconds */
		timeout_add_sec(&sc->sc_calib_ch, 15);
	}

	splx(s);
}

int
bwi_calc_rssi(struct bwi_softc *sc, const struct bwi_rxbuf_hdr *hdr)
{
	struct bwi_mac *mac;

	KASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	return (bwi_rf_calc_rssi(mac, hdr));
}
