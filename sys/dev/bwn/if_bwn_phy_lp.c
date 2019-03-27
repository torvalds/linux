/*-
 * Copyright (c) 2009-2010 Weongyo Jeong <weongyo@freebsd.org>
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

#include "opt_bwn.h"
#include "opt_wlan.h"

/*
 * The Broadcom Wireless LAN controller driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/firmware.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_phy.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/bhnd_ids.h>

#include <dev/bhnd/cores/pmu/bhnd_pmu.h>

#include <dev/bwn/if_bwnreg.h>
#include <dev/bwn/if_bwnvar.h>

#include <dev/bwn/if_bwn_debug.h>
#include <dev/bwn/if_bwn_misc.h>
#include <dev/bwn/if_bwn_util.h>
#include <dev/bwn/if_bwn_phy_common.h>
#include <dev/bwn/if_bwn_phy_lp.h>

#include "bhnd_nvram_map.h"

static int	bwn_phy_lp_readsprom(struct bwn_mac *);
static void	bwn_phy_lp_bbinit(struct bwn_mac *);
static void	bwn_phy_lp_txpctl_init(struct bwn_mac *);
static void	bwn_phy_lp_calib(struct bwn_mac *);
static int	bwn_phy_lp_b2062_switch_channel(struct bwn_mac *, uint8_t);
static int	bwn_phy_lp_b2063_switch_channel(struct bwn_mac *, uint8_t);
static void	bwn_phy_lp_set_anafilter(struct bwn_mac *, uint8_t);
static void	bwn_phy_lp_set_gaintbl(struct bwn_mac *, uint32_t);
static void	bwn_phy_lp_digflt_save(struct bwn_mac *);
static void	bwn_phy_lp_get_txpctlmode(struct bwn_mac *);
static void	bwn_phy_lp_set_txpctlmode(struct bwn_mac *, uint8_t);
static void	bwn_phy_lp_bugfix(struct bwn_mac *);
static void	bwn_phy_lp_digflt_restore(struct bwn_mac *);
static void	bwn_phy_lp_tblinit(struct bwn_mac *);
static void	bwn_phy_lp_bbinit_r2(struct bwn_mac *);
static void	bwn_phy_lp_bbinit_r01(struct bwn_mac *);
static int	bwn_phy_lp_b2062_init(struct bwn_mac *);
static int	bwn_phy_lp_b2063_init(struct bwn_mac *);
static int	bwn_phy_lp_rxcal_r2(struct bwn_mac *);
static int	bwn_phy_lp_rccal_r12(struct bwn_mac *);
static void	bwn_phy_lp_set_rccap(struct bwn_mac *);
static uint32_t	bwn_phy_lp_roundup(uint32_t, uint32_t, uint8_t);
static void	bwn_phy_lp_b2062_reset_pllbias(struct bwn_mac *);
static void	bwn_phy_lp_b2062_vco_calib(struct bwn_mac *);
static void	bwn_tab_write_multi(struct bwn_mac *, uint32_t, int,
		    const void *);
static void	bwn_tab_read_multi(struct bwn_mac *, uint32_t, int, void *);
static struct bwn_txgain
		bwn_phy_lp_get_txgain(struct bwn_mac *);
static uint8_t	bwn_phy_lp_get_bbmult(struct bwn_mac *);
static void	bwn_phy_lp_set_txgain(struct bwn_mac *, struct bwn_txgain *);
static void	bwn_phy_lp_set_bbmult(struct bwn_mac *, uint8_t);
static void	bwn_phy_lp_set_trsw_over(struct bwn_mac *, uint8_t, uint8_t);
static void	bwn_phy_lp_set_rxgain(struct bwn_mac *, uint32_t);
static void	bwn_phy_lp_set_deaf(struct bwn_mac *, uint8_t);
static int	bwn_phy_lp_calc_rx_iq_comp(struct bwn_mac *, uint16_t);
static void	bwn_phy_lp_clear_deaf(struct bwn_mac *, uint8_t);
static void	bwn_phy_lp_tblinit_r01(struct bwn_mac *);
static void	bwn_phy_lp_tblinit_r2(struct bwn_mac *);
static void	bwn_phy_lp_tblinit_txgain(struct bwn_mac *);
static void	bwn_tab_write(struct bwn_mac *, uint32_t, uint32_t);
static void	bwn_phy_lp_b2062_tblinit(struct bwn_mac *);
static void	bwn_phy_lp_b2063_tblinit(struct bwn_mac *);
static int	bwn_phy_lp_loopback(struct bwn_mac *);
static void	bwn_phy_lp_set_rxgain_idx(struct bwn_mac *, uint16_t);
static void	bwn_phy_lp_ddfs_turnon(struct bwn_mac *, int, int, int, int,
		    int);
static uint8_t	bwn_phy_lp_rx_iq_est(struct bwn_mac *, uint16_t, uint8_t,
		    struct bwn_phy_lp_iq_est *);
static void	bwn_phy_lp_ddfs_turnoff(struct bwn_mac *);
static uint32_t	bwn_tab_read(struct bwn_mac *, uint32_t);
static void	bwn_phy_lp_set_txgain_dac(struct bwn_mac *, uint16_t);
static void	bwn_phy_lp_set_txgain_pa(struct bwn_mac *, uint16_t);
static void	bwn_phy_lp_set_txgain_override(struct bwn_mac *);
static uint16_t	bwn_phy_lp_get_pa_gain(struct bwn_mac *);
static uint8_t	bwn_nbits(int32_t);
static void	bwn_phy_lp_gaintbl_write_multi(struct bwn_mac *, int, int,
		    struct bwn_txgain_entry *);
static void	bwn_phy_lp_gaintbl_write(struct bwn_mac *, int,
		    struct bwn_txgain_entry);
static void	bwn_phy_lp_gaintbl_write_r2(struct bwn_mac *, int,
		    struct bwn_txgain_entry);
static void	bwn_phy_lp_gaintbl_write_r01(struct bwn_mac *, int,
		    struct bwn_txgain_entry);

static const uint8_t bwn_b2063_chantable_data[33][12] = {
	{ 0x6f, 0x3c, 0x3c, 0x4, 0x5, 0x5, 0x5, 0x5, 0x77, 0x80, 0x80, 0x70 },
	{ 0x6f, 0x2c, 0x2c, 0x4, 0x5, 0x5, 0x5, 0x5, 0x77, 0x80, 0x80, 0x70 },
	{ 0x6f, 0x1c, 0x1c, 0x4, 0x5, 0x5, 0x5, 0x5, 0x77, 0x80, 0x80, 0x70 },
	{ 0x6e, 0x1c, 0x1c, 0x4, 0x5, 0x5, 0x5, 0x5, 0x77, 0x80, 0x80, 0x70 },
	{ 0x6e, 0xc, 0xc, 0x4, 0x5, 0x5, 0x5, 0x5, 0x77, 0x80, 0x80, 0x70 },
	{ 0x6a, 0xc, 0xc, 0, 0x2, 0x5, 0xd, 0xd, 0x77, 0x80, 0x20, 0 },
	{ 0x6a, 0xc, 0xc, 0, 0x1, 0x5, 0xd, 0xc, 0x77, 0x80, 0x20, 0 },
	{ 0x6a, 0xc, 0xc, 0, 0x1, 0x4, 0xc, 0xc, 0x77, 0x80, 0x20, 0 },
	{ 0x69, 0xc, 0xc, 0, 0x1, 0x4, 0xc, 0xc, 0x77, 0x70, 0x20, 0 },
	{ 0x69, 0xc, 0xc, 0, 0x1, 0x4, 0xb, 0xc, 0x77, 0x70, 0x20, 0 },
	{ 0x69, 0xc, 0xc, 0, 0, 0x4, 0xb, 0xb, 0x77, 0x60, 0x20, 0 },
	{ 0x69, 0xc, 0xc, 0, 0, 0x3, 0xa, 0xb, 0x77, 0x60, 0x20, 0 },
	{ 0x69, 0xc, 0xc, 0, 0, 0x3, 0xa, 0xa, 0x77, 0x60, 0x20, 0 },
	{ 0x68, 0xc, 0xc, 0, 0, 0x2, 0x9, 0x9, 0x77, 0x60, 0x20, 0 },
	{ 0x68, 0xc, 0xc, 0, 0, 0x1, 0x8, 0x8, 0x77, 0x50, 0x10, 0 },
	{ 0x67, 0xc, 0xc, 0, 0, 0, 0x8, 0x8, 0x77, 0x50, 0x10, 0 },
	{ 0x64, 0xc, 0xc, 0, 0, 0, 0x2, 0x1, 0x77, 0x20, 0, 0 },
	{ 0x64, 0xc, 0xc, 0, 0, 0, 0x1, 0x1, 0x77, 0x20, 0, 0 },
	{ 0x63, 0xc, 0xc, 0, 0, 0, 0x1, 0, 0x77, 0x10, 0, 0 },
	{ 0x63, 0xc, 0xc, 0, 0, 0, 0, 0, 0x77, 0x10, 0, 0 },
	{ 0x62, 0xc, 0xc, 0, 0, 0, 0, 0, 0x77, 0x10, 0, 0 },
	{ 0x62, 0xc, 0xc, 0, 0, 0, 0, 0, 0x77, 0, 0, 0 },
	{ 0x61, 0xc, 0xc, 0, 0, 0, 0, 0, 0x77, 0, 0, 0 },
	{ 0x60, 0xc, 0xc, 0, 0, 0, 0, 0, 0x77, 0, 0, 0 },
	{ 0x6e, 0xc, 0xc, 0, 0x9, 0xe, 0xf, 0xf, 0x77, 0xc0, 0x50, 0 },
	{ 0x6e, 0xc, 0xc, 0, 0x9, 0xd, 0xf, 0xf, 0x77, 0xb0, 0x50, 0 },
	{ 0x6e, 0xc, 0xc, 0, 0x8, 0xc, 0xf, 0xf, 0x77, 0xb0, 0x50, 0 },
	{ 0x6d, 0xc, 0xc, 0, 0x8, 0xc, 0xf, 0xf, 0x77, 0xa0, 0x40, 0 },
	{ 0x6d, 0xc, 0xc, 0, 0x8, 0xb, 0xf, 0xf, 0x77, 0xa0, 0x40, 0 },
	{ 0x6d, 0xc, 0xc, 0, 0x8, 0xa, 0xf, 0xf, 0x77, 0xa0, 0x40, 0 },
	{ 0x6c, 0xc, 0xc, 0, 0x7, 0x9, 0xf, 0xf, 0x77, 0x90, 0x40, 0 },
	{ 0x6c, 0xc, 0xc, 0, 0x6, 0x8, 0xf, 0xf, 0x77, 0x90, 0x40, 0 },
	{ 0x6c, 0xc, 0xc, 0, 0x5, 0x8, 0xf, 0xf, 0x77, 0x90, 0x40, 0 }
};

static const struct bwn_b206x_chan bwn_b2063_chantable[] = {
	{ 1, 2412, bwn_b2063_chantable_data[0] },
	{ 2, 2417, bwn_b2063_chantable_data[0] },
	{ 3, 2422, bwn_b2063_chantable_data[0] },
	{ 4, 2427, bwn_b2063_chantable_data[1] },
	{ 5, 2432, bwn_b2063_chantable_data[1] },
	{ 6, 2437, bwn_b2063_chantable_data[1] },
	{ 7, 2442, bwn_b2063_chantable_data[1] },
	{ 8, 2447, bwn_b2063_chantable_data[1] },
	{ 9, 2452, bwn_b2063_chantable_data[2] },
	{ 10, 2457, bwn_b2063_chantable_data[2] },
	{ 11, 2462, bwn_b2063_chantable_data[3] },
	{ 12, 2467, bwn_b2063_chantable_data[3] },
	{ 13, 2472, bwn_b2063_chantable_data[3] },
	{ 14, 2484, bwn_b2063_chantable_data[4] },
	{ 34, 5170, bwn_b2063_chantable_data[5] },
	{ 36, 5180, bwn_b2063_chantable_data[6] },
	{ 38, 5190, bwn_b2063_chantable_data[7] },
	{ 40, 5200, bwn_b2063_chantable_data[8] },
	{ 42, 5210, bwn_b2063_chantable_data[9] },
	{ 44, 5220, bwn_b2063_chantable_data[10] },
	{ 46, 5230, bwn_b2063_chantable_data[11] },
	{ 48, 5240, bwn_b2063_chantable_data[12] },
	{ 52, 5260, bwn_b2063_chantable_data[13] },
	{ 56, 5280, bwn_b2063_chantable_data[14] },
	{ 60, 5300, bwn_b2063_chantable_data[14] },
	{ 64, 5320, bwn_b2063_chantable_data[15] },
	{ 100, 5500, bwn_b2063_chantable_data[16] },
	{ 104, 5520, bwn_b2063_chantable_data[17] },
	{ 108, 5540, bwn_b2063_chantable_data[18] },
	{ 112, 5560, bwn_b2063_chantable_data[19] },
	{ 116, 5580, bwn_b2063_chantable_data[20] },
	{ 120, 5600, bwn_b2063_chantable_data[21] },
	{ 124, 5620, bwn_b2063_chantable_data[21] },
	{ 128, 5640, bwn_b2063_chantable_data[22] },
	{ 132, 5660, bwn_b2063_chantable_data[22] },
	{ 136, 5680, bwn_b2063_chantable_data[22] },
	{ 140, 5700, bwn_b2063_chantable_data[23] },
	{ 149, 5745, bwn_b2063_chantable_data[23] },
	{ 153, 5765, bwn_b2063_chantable_data[23] },
	{ 157, 5785, bwn_b2063_chantable_data[23] },
	{ 161, 5805, bwn_b2063_chantable_data[23] },
	{ 165, 5825, bwn_b2063_chantable_data[23] },
	{ 184, 4920, bwn_b2063_chantable_data[24] },
	{ 188, 4940, bwn_b2063_chantable_data[25] },
	{ 192, 4960, bwn_b2063_chantable_data[26] },
	{ 196, 4980, bwn_b2063_chantable_data[27] },
	{ 200, 5000, bwn_b2063_chantable_data[28] },
	{ 204, 5020, bwn_b2063_chantable_data[29] },
	{ 208, 5040, bwn_b2063_chantable_data[30] },
	{ 212, 5060, bwn_b2063_chantable_data[31] },
	{ 216, 5080, bwn_b2063_chantable_data[32] }
};

static const uint8_t bwn_b2062_chantable_data[22][12] = {
	{ 0xff, 0xff, 0xb5, 0x1b, 0x24, 0x32, 0x32, 0x88, 0x88, 0, 0, 0 },
	{ 0, 0x22, 0x20, 0x84, 0x3c, 0x77, 0x35, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0x11, 0x10, 0x83, 0x3c, 0x77, 0x35, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0, 0, 0x83, 0x3c, 0x77, 0x35, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0x11, 0x20, 0x83, 0x3c, 0x77, 0x35, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0x11, 0x10, 0x84, 0x3c, 0x77, 0x35, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0x11, 0, 0x83, 0x3c, 0x77, 0x35, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0, 0, 0x63, 0x3c, 0x77, 0x35, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0, 0, 0x62, 0x3c, 0x77, 0x35, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0, 0, 0x30, 0x3c, 0x77, 0x37, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0, 0, 0x20, 0x3c, 0x77, 0x37, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0, 0, 0x10, 0x3c, 0x77, 0x37, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0, 0, 0, 0x3c, 0x77, 0x37, 0xff, 0x88, 0, 0, 0 },
	{ 0x55, 0x77, 0x90, 0xf7, 0x3c, 0x77, 0x35, 0xff, 0xff, 0, 0, 0 },
	{ 0x44, 0x77, 0x80, 0xe7, 0x3c, 0x77, 0x35, 0xff, 0xff, 0, 0, 0 },
	{ 0x44, 0x66, 0x80, 0xe7, 0x3c, 0x77, 0x35, 0xff, 0xff, 0, 0, 0 },
	{ 0x33, 0x66, 0x70, 0xc7, 0x3c, 0x77, 0x35, 0xff, 0xff, 0, 0, 0 },
	{ 0x22, 0x55, 0x60, 0xd7, 0x3c, 0x77, 0x35, 0xff, 0xff, 0, 0, 0 },
	{ 0x22, 0x55, 0x60, 0xc7, 0x3c, 0x77, 0x35, 0xff, 0xff, 0, 0, 0 },
	{ 0x22, 0x44, 0x50, 0xc7, 0x3c, 0x77, 0x35, 0xff, 0xff, 0, 0, 0 },
	{ 0x11, 0x44, 0x50, 0xa5, 0x3c, 0x77, 0x35, 0xff, 0x88, 0, 0, 0 },
	{ 0, 0x44, 0x40, 0xb6, 0x3c, 0x77, 0x35, 0xff, 0x88, 0, 0, 0 }
};

static const struct bwn_b206x_chan bwn_b2062_chantable[] = {
	{ 1, 2412, bwn_b2062_chantable_data[0] },
	{ 2, 2417, bwn_b2062_chantable_data[0] },
	{ 3, 2422, bwn_b2062_chantable_data[0] },
	{ 4, 2427, bwn_b2062_chantable_data[0] },
	{ 5, 2432, bwn_b2062_chantable_data[0] },
	{ 6, 2437, bwn_b2062_chantable_data[0] },
	{ 7, 2442, bwn_b2062_chantable_data[0] },
	{ 8, 2447, bwn_b2062_chantable_data[0] },
	{ 9, 2452, bwn_b2062_chantable_data[0] },
	{ 10, 2457, bwn_b2062_chantable_data[0] },
	{ 11, 2462, bwn_b2062_chantable_data[0] },
	{ 12, 2467, bwn_b2062_chantable_data[0] },
	{ 13, 2472, bwn_b2062_chantable_data[0] },
	{ 14, 2484, bwn_b2062_chantable_data[0] },
	{ 34, 5170, bwn_b2062_chantable_data[1] },
	{ 38, 5190, bwn_b2062_chantable_data[2] },
	{ 42, 5210, bwn_b2062_chantable_data[2] },
	{ 46, 5230, bwn_b2062_chantable_data[3] },
	{ 36, 5180, bwn_b2062_chantable_data[4] },
	{ 40, 5200, bwn_b2062_chantable_data[5] },
	{ 44, 5220, bwn_b2062_chantable_data[6] },
	{ 48, 5240, bwn_b2062_chantable_data[3] },
	{ 52, 5260, bwn_b2062_chantable_data[3] },
	{ 56, 5280, bwn_b2062_chantable_data[3] },
	{ 60, 5300, bwn_b2062_chantable_data[7] },
	{ 64, 5320, bwn_b2062_chantable_data[8] },
	{ 100, 5500, bwn_b2062_chantable_data[9] },
	{ 104, 5520, bwn_b2062_chantable_data[10] },
	{ 108, 5540, bwn_b2062_chantable_data[10] },
	{ 112, 5560, bwn_b2062_chantable_data[10] },
	{ 116, 5580, bwn_b2062_chantable_data[11] },
	{ 120, 5600, bwn_b2062_chantable_data[12] },
	{ 124, 5620, bwn_b2062_chantable_data[12] },
	{ 128, 5640, bwn_b2062_chantable_data[12] },
	{ 132, 5660, bwn_b2062_chantable_data[12] },
	{ 136, 5680, bwn_b2062_chantable_data[12] },
	{ 140, 5700, bwn_b2062_chantable_data[12] },
	{ 149, 5745, bwn_b2062_chantable_data[12] },
	{ 153, 5765, bwn_b2062_chantable_data[12] },
	{ 157, 5785, bwn_b2062_chantable_data[12] },
	{ 161, 5805, bwn_b2062_chantable_data[12] },
	{ 165, 5825, bwn_b2062_chantable_data[12] },
	{ 184, 4920, bwn_b2062_chantable_data[13] },
	{ 188, 4940, bwn_b2062_chantable_data[14] },
	{ 192, 4960, bwn_b2062_chantable_data[15] },
	{ 196, 4980, bwn_b2062_chantable_data[16] },
	{ 200, 5000, bwn_b2062_chantable_data[17] },
	{ 204, 5020, bwn_b2062_chantable_data[18] },
	{ 208, 5040, bwn_b2062_chantable_data[19] },
	{ 212, 5060, bwn_b2062_chantable_data[20] },
	{ 216, 5080, bwn_b2062_chantable_data[21] }
};

/* for LP PHY */
static const struct bwn_rxcompco bwn_rxcompco_5354[] = {
	{  1, -66, 15 }, {  2, -66, 15 }, {  3, -66, 15 }, {  4, -66, 15 },
	{  5, -66, 15 }, {  6, -66, 15 }, {  7, -66, 14 }, {  8, -66, 14 },
	{  9, -66, 14 }, { 10, -66, 14 }, { 11, -66, 14 }, { 12, -66, 13 },
	{ 13, -66, 13 }, { 14, -66, 13 },
};

/* for LP PHY */
static const struct bwn_rxcompco bwn_rxcompco_r12[] = {
	{   1, -64, 13 }, {   2, -64, 13 }, {   3, -64, 13 }, {   4, -64, 13 },
	{   5, -64, 12 }, {   6, -64, 12 }, {   7, -64, 12 }, {   8, -64, 12 },
	{   9, -64, 12 }, {  10, -64, 11 }, {  11, -64, 11 }, {  12, -64, 11 },
	{  13, -64, 11 }, {  14, -64, 10 }, {  34, -62, 24 }, {  38, -62, 24 },
	{  42, -62, 24 }, {  46, -62, 23 }, {  36, -62, 24 }, {  40, -62, 24 },
	{  44, -62, 23 }, {  48, -62, 23 }, {  52, -62, 23 }, {  56, -62, 22 },
	{  60, -62, 22 }, {  64, -62, 22 }, { 100, -62, 16 }, { 104, -62, 16 },
	{ 108, -62, 15 }, { 112, -62, 14 }, { 116, -62, 14 }, { 120, -62, 13 },
	{ 124, -62, 12 }, { 128, -62, 12 }, { 132, -62, 12 }, { 136, -62, 11 },
	{ 140, -62, 10 }, { 149, -61,  9 }, { 153, -61,  9 }, { 157, -61,  9 },
	{ 161, -61,  8 }, { 165, -61,  8 }, { 184, -62, 25 }, { 188, -62, 25 },
	{ 192, -62, 25 }, { 196, -62, 25 }, { 200, -62, 25 }, { 204, -62, 25 },
	{ 208, -62, 25 }, { 212, -62, 25 }, { 216, -62, 26 },
};

static const struct bwn_rxcompco bwn_rxcompco_r2 = { 0, -64, 0 };

static const uint8_t bwn_tab_sigsq_tbl[] = {
	0xde, 0xdc, 0xda, 0xd8, 0xd6, 0xd4, 0xd2, 0xcf, 0xcd,
	0xca, 0xc7, 0xc4, 0xc1, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe,
	0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0x00,
	0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe,
	0xbe, 0xbe, 0xbe, 0xbe, 0xc1, 0xc4, 0xc7, 0xca, 0xcd,
	0xcf, 0xd2, 0xd4, 0xd6, 0xd8, 0xda, 0xdc, 0xde,
};

static const uint8_t bwn_tab_pllfrac_tbl[] = {
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
};

static const uint16_t bwn_tabl_iqlocal_tbl[] = {
	0x0200, 0x0300, 0x0400, 0x0600, 0x0800, 0x0b00, 0x1000, 0x1001, 0x1002,
	0x1003, 0x1004, 0x1005, 0x1006, 0x1007, 0x1707, 0x2007, 0x2d07, 0x4007,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0200, 0x0300, 0x0400, 0x0600,
	0x0800, 0x0b00, 0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1005, 0x1006,
	0x1007, 0x1707, 0x2007, 0x2d07, 0x4007, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x4000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

void
bwn_phy_lp_init_pre(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_lp *plp = &phy->phy_lp;

	plp->plp_antenna = BWN_ANT_DEFAULT;
}

int
bwn_phy_lp_init(struct bwn_mac *mac)
{
	static const struct bwn_stxtable tables[] = {
		{ 2,  6, 0x3d, 3, 0x01 }, { 1, 12, 0x4c, 1, 0x01 },
		{ 1,  8, 0x50, 0, 0x7f }, { 0,  8, 0x44, 0, 0xff },
		{ 1,  0, 0x4a, 0, 0xff }, { 0,  4, 0x4d, 0, 0xff },
		{ 1,  4, 0x4e, 0, 0xff }, { 0, 12, 0x4f, 0, 0x0f },
		{ 1,  0, 0x4f, 4, 0x0f }, { 3,  0, 0x49, 0, 0x0f },
		{ 4,  3, 0x46, 4, 0x07 }, { 3, 15, 0x46, 0, 0x01 },
		{ 4,  0, 0x46, 1, 0x07 }, { 3,  8, 0x48, 4, 0x07 },
		{ 3, 11, 0x48, 0, 0x0f }, { 3,  4, 0x49, 4, 0x0f },
		{ 2, 15, 0x45, 0, 0x01 }, { 5, 13, 0x52, 4, 0x07 },
		{ 6,  0, 0x52, 7, 0x01 }, { 5,  3, 0x41, 5, 0x07 },
		{ 5,  6, 0x41, 0, 0x0f }, { 5, 10, 0x42, 5, 0x07 },
		{ 4, 15, 0x42, 0, 0x01 }, { 5,  0, 0x42, 1, 0x07 },
		{ 4, 11, 0x43, 4, 0x0f }, { 4,  7, 0x43, 0, 0x0f },
		{ 4,  6, 0x45, 1, 0x01 }, { 2,  7, 0x40, 4, 0x0f },
		{ 2, 11, 0x40, 0, 0x0f }
	};
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	struct bwn_softc *sc = mac->mac_sc;
	const struct bwn_stxtable *st;
	struct ieee80211com *ic = &sc->sc_ic;
	int i, error;
	uint16_t tmp;

	/* All LP-PHY devices have a PMU */
	if (sc->sc_pmu == NULL) {
		device_printf(sc->sc_dev, "no PMU; cannot configure PAREF "
		    "LDO\n");
		return (ENXIO);
	}

	if ((error = bwn_phy_lp_readsprom(mac)))
		return (error);

	bwn_phy_lp_bbinit(mac);

	/* initialize RF */
	BWN_PHY_SET(mac, BWN_PHY_4WIRECTL, 0x2);
	DELAY(1);
	BWN_PHY_MASK(mac, BWN_PHY_4WIRECTL, 0xfffd);
	DELAY(1);

	if (mac->mac_phy.rf_ver == 0x2062) {
		if ((error = bwn_phy_lp_b2062_init(mac)))
			return (error);
	} else {
		if ((error = bwn_phy_lp_b2063_init(mac)))
			return (error);

		/* synchronize stx table. */
		for (i = 0; i < N(tables); i++) {
			st = &tables[i];
			tmp = BWN_RF_READ(mac, st->st_rfaddr);
			tmp >>= st->st_rfshift;
			tmp <<= st->st_physhift;
			BWN_PHY_SETMASK(mac,
			    BWN_PHY_OFDM(0xf2 + st->st_phyoffset),
			    ~(st->st_mask << st->st_physhift), tmp);
		}

		BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0xf0), 0x5f80);
		BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0xf1), 0);
	}

	/* calibrate RC */
	if (mac->mac_phy.rev >= 2) {
		if ((error = bwn_phy_lp_rxcal_r2(mac)))
			return (error);
	} else if (!plp->plp_rccap) {
		if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
			if ((error = bwn_phy_lp_rccal_r12(mac)))
				return (error);
		}
	} else
		bwn_phy_lp_set_rccap(mac);

	error = bwn_phy_lp_switch_channel(mac, 7);
	if (error)
		device_printf(sc->sc_dev,
		    "failed to change channel 7 (%d)\n", error);
	bwn_phy_lp_txpctl_init(mac);
	bwn_phy_lp_calib(mac);
	return (0);
}

uint16_t
bwn_phy_lp_read(struct bwn_mac *mac, uint16_t reg)
{

	BWN_WRITE_2(mac, BWN_PHYCTL, reg);
	return (BWN_READ_2(mac, BWN_PHYDATA));
}

void
bwn_phy_lp_write(struct bwn_mac *mac, uint16_t reg, uint16_t value)
{

	BWN_WRITE_2(mac, BWN_PHYCTL, reg);
	BWN_WRITE_2(mac, BWN_PHYDATA, value);
}

void
bwn_phy_lp_maskset(struct bwn_mac *mac, uint16_t reg, uint16_t mask,
    uint16_t set)
{

	BWN_WRITE_2(mac, BWN_PHYCTL, reg);
	BWN_WRITE_2(mac, BWN_PHYDATA,
	    (BWN_READ_2(mac, BWN_PHYDATA) & mask) | set);
}

uint16_t
bwn_phy_lp_rf_read(struct bwn_mac *mac, uint16_t reg)
{

	KASSERT(reg != 1, ("unaccessible register %d", reg));
	if (mac->mac_phy.rev < 2 && reg != 0x4001)
		reg |= 0x100;
	if (mac->mac_phy.rev >= 2)
		reg |= 0x200;
	BWN_WRITE_2(mac, BWN_RFCTL, reg);
	return BWN_READ_2(mac, BWN_RFDATALO);
}

void
bwn_phy_lp_rf_write(struct bwn_mac *mac, uint16_t reg, uint16_t value)
{

	KASSERT(reg != 1, ("unaccessible register %d", reg));
	BWN_WRITE_2(mac, BWN_RFCTL, reg);
	BWN_WRITE_2(mac, BWN_RFDATALO, value);
}

void
bwn_phy_lp_rf_onoff(struct bwn_mac *mac, int on)
{

	if (on) {
		BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_0, 0xe0ff);
		BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2,
		    (mac->mac_phy.rev >= 2) ? 0xf7f7 : 0xffe7);
		return;
	}

	if (mac->mac_phy.rev >= 2) {
		BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0x83ff);
		BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x1f00);
		BWN_PHY_MASK(mac, BWN_PHY_AFE_DDFS, 0x80ff);
		BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0xdfff);
		BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_2, 0x0808);
		return;
	}

	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0xe0ff);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x1f00);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0xfcff);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_2, 0x0018);
}

int
bwn_phy_lp_switch_channel(struct bwn_mac *mac, uint32_t chan)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_lp *plp = &phy->phy_lp;
	int error;

	if (phy->rf_ver == 0x2063) {
		error = bwn_phy_lp_b2063_switch_channel(mac, chan);
		if (error)
			return (error);
	} else {
		error = bwn_phy_lp_b2062_switch_channel(mac, chan);
		if (error)
			return (error);
		bwn_phy_lp_set_anafilter(mac, chan);
		bwn_phy_lp_set_gaintbl(mac, ieee80211_ieee2mhz(chan, 0));
	}

	plp->plp_chan = chan;
	BWN_WRITE_2(mac, BWN_CHANNEL, chan);
	return (0);
}

uint32_t
bwn_phy_lp_get_default_chan(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;

	return (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan) ? 1 : 36);
}

void
bwn_phy_lp_set_antenna(struct bwn_mac *mac, int antenna)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_lp *plp = &phy->phy_lp;

	if (phy->rev >= 2 || antenna > BWN_ANTAUTO1)
		return;

	bwn_hf_write(mac, bwn_hf_read(mac) & ~BWN_HF_UCODE_ANTDIV_HELPER);
	BWN_PHY_SETMASK(mac, BWN_PHY_CRSGAIN_CTL, 0xfffd, antenna & 0x2);
	BWN_PHY_SETMASK(mac, BWN_PHY_CRSGAIN_CTL, 0xfffe, antenna & 0x1);
	bwn_hf_write(mac, bwn_hf_read(mac) | BWN_HF_UCODE_ANTDIV_HELPER);
	plp->plp_antenna = antenna;
}

void
bwn_phy_lp_task_60s(struct bwn_mac *mac)
{

	bwn_phy_lp_calib(mac);
}

static int
bwn_phy_lp_readsprom(struct bwn_mac *mac)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;

#define	BWN_PHY_LP_READVAR(_dev, _type, _name, _result)		\
do {									\
	int error;							\
									\
	error = bhnd_nvram_getvar_ ##_type((_dev), (_name), (_result));	\
	if (error) {							\
		device_printf((_dev), "NVRAM variable %s unreadable: "	\
		    "%d\n", (_name), error);				\
		return (error);						\
	}								\
} while(0)

	if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
		BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_TRI2G,
		    &plp->plp_txisoband_m);
		BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_BXA2G,
		    &plp->plp_bxarch);
		BWN_PHY_LP_READVAR(sc->sc_dev, int8, BHND_NVAR_RXPO2G,
		    &plp->plp_rxpwroffset);
		BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_RSSISMF2G,
		    &plp->plp_rssivf);
		BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_RSSISMC2G,
		    &plp->plp_rssivc);
		BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_RSSISAV2G,
		    &plp->plp_rssigs);

		return (0);
	}

	BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_TRI5GL,
	    &plp->plp_txisoband_l);
	BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_TRI5G,
	    &plp->plp_txisoband_m);
	BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_TRI5GH,
	    &plp->plp_txisoband_h);
	BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_BXA5G,
	    &plp->plp_bxarch);
	BWN_PHY_LP_READVAR(sc->sc_dev, int8, BHND_NVAR_RXPO5G,
	    &plp->plp_rxpwroffset);
	BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_RSSISMF5G,
	    &plp->plp_rssivf);
	BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_RSSISMC5G,
	    &plp->plp_rssivc);
	BWN_PHY_LP_READVAR(sc->sc_dev, uint8, BHND_NVAR_RSSISAV5G,
	    &plp->plp_rssigs);

#undef	BWN_PHY_LP_READVAR

	return (0);
}

static void
bwn_phy_lp_bbinit(struct bwn_mac *mac)
{

	bwn_phy_lp_tblinit(mac);
	if (mac->mac_phy.rev >= 2)
		bwn_phy_lp_bbinit_r2(mac);
	else
		bwn_phy_lp_bbinit_r01(mac);
}

static void
bwn_phy_lp_txpctl_init(struct bwn_mac *mac)
{
	struct bwn_txgain gain_2ghz = { 4, 12, 12, 0 };
	struct bwn_txgain gain_5ghz = { 7, 15, 14, 0 };
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;

	bwn_phy_lp_set_txgain(mac,
	    IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan) ? &gain_2ghz : &gain_5ghz);
	bwn_phy_lp_set_bbmult(mac, 150);
}

static void
bwn_phy_lp_calib(struct bwn_mac *mac)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	const struct bwn_rxcompco *rc = NULL;
	struct bwn_txgain ogain;
	int i, omode, oafeovr, orf, obbmult;
	uint8_t mode, fc = 0;

	if (plp->plp_chanfullcal != plp->plp_chan) {
		plp->plp_chanfullcal = plp->plp_chan;
		fc = 1;
	}

	bwn_mac_suspend(mac);

	/* BlueTooth Coexistance Override */
	BWN_WRITE_2(mac, BWN_BTCOEX_CTL, 0x3);
	BWN_WRITE_2(mac, BWN_BTCOEX_TXCTL, 0xff);

	if (mac->mac_phy.rev >= 2)
		bwn_phy_lp_digflt_save(mac);
	bwn_phy_lp_get_txpctlmode(mac);
	mode = plp->plp_txpctlmode;
	bwn_phy_lp_set_txpctlmode(mac, BWN_PHYLP_TXPCTL_OFF);
	if (mac->mac_phy.rev == 0 && mode != BWN_PHYLP_TXPCTL_OFF)
		bwn_phy_lp_bugfix(mac);
	if (mac->mac_phy.rev >= 2 && fc == 1) {
		bwn_phy_lp_get_txpctlmode(mac);
		omode = plp->plp_txpctlmode;
		oafeovr = BWN_PHY_READ(mac, BWN_PHY_AFE_CTL_OVR) & 0x40;
		if (oafeovr)
			ogain = bwn_phy_lp_get_txgain(mac);
		orf = BWN_PHY_READ(mac, BWN_PHY_RF_PWR_OVERRIDE) & 0xff;
		obbmult = bwn_phy_lp_get_bbmult(mac);
		bwn_phy_lp_set_txpctlmode(mac, BWN_PHYLP_TXPCTL_OFF);
		if (oafeovr)
			bwn_phy_lp_set_txgain(mac, &ogain);
		bwn_phy_lp_set_bbmult(mac, obbmult);
		bwn_phy_lp_set_txpctlmode(mac, omode);
		BWN_PHY_SETMASK(mac, BWN_PHY_RF_PWR_OVERRIDE, 0xff00, orf);
	}
	bwn_phy_lp_set_txpctlmode(mac, mode);
	if (mac->mac_phy.rev >= 2)
		bwn_phy_lp_digflt_restore(mac);

	/* do RX IQ Calculation; assumes that noise is true. */
	if (sc->sc_cid.chip_id == BHND_CHIPID_BCM5354) {
		for (i = 0; i < N(bwn_rxcompco_5354); i++) {
			if (bwn_rxcompco_5354[i].rc_chan == plp->plp_chan)
				rc = &bwn_rxcompco_5354[i];
		}
	} else if (mac->mac_phy.rev >= 2)
		rc = &bwn_rxcompco_r2;
	else {
		for (i = 0; i < N(bwn_rxcompco_r12); i++) {
			if (bwn_rxcompco_r12[i].rc_chan == plp->plp_chan)
				rc = &bwn_rxcompco_r12[i];
		}
	}
	if (rc == NULL)
		goto fail;

	BWN_PHY_SETMASK(mac, BWN_PHY_RX_COMP_COEFF_S, 0xff00, rc->rc_c1);
	BWN_PHY_SETMASK(mac, BWN_PHY_RX_COMP_COEFF_S, 0x00ff, rc->rc_c0 << 8);

	bwn_phy_lp_set_trsw_over(mac, 1 /* TX */, 0 /* RX */);

	if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
		BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x8);
		BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0xfff7, 0);
	} else {
		BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x20);
		BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0xffdf, 0);
	}

	bwn_phy_lp_set_rxgain(mac, 0x2d5d);
	BWN_PHY_MASK(mac, BWN_PHY_AFE_CTL_OVR, 0xfffe);
	BWN_PHY_MASK(mac, BWN_PHY_AFE_CTL_OVRVAL, 0xfffe);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x800);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0x800);
	bwn_phy_lp_set_deaf(mac, 0);
	/* XXX no checking return value? */
	(void)bwn_phy_lp_calc_rx_iq_comp(mac, 0xfff0);
	bwn_phy_lp_clear_deaf(mac, 0);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_0, 0xfffc);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_0, 0xfff7);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_0, 0xffdf);

	/* disable RX GAIN override. */
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_0, 0xfffe);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_0, 0xffef);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_0, 0xffbf);
	if (mac->mac_phy.rev >= 2) {
		BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2, 0xfeff);
		if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
			BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2, 0xfbff);
			BWN_PHY_MASK(mac, BWN_PHY_OFDM(0xe5), 0xfff7);
		}
	} else {
		BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2, 0xfdff);
	}

	BWN_PHY_MASK(mac, BWN_PHY_AFE_CTL_OVR, 0xfffe);
	BWN_PHY_MASK(mac, BWN_PHY_AFE_CTL_OVRVAL, 0xf7ff);
fail:
	bwn_mac_enable(mac);
}

void
bwn_phy_lp_switch_analog(struct bwn_mac *mac, int on)
{

	if (on) {
		BWN_PHY_MASK(mac, BWN_PHY_AFE_CTL_OVR, 0xfff8);
		return;
	}

	BWN_PHY_SET(mac, BWN_PHY_AFE_CTL_OVRVAL, 0x0007);
	BWN_PHY_SET(mac, BWN_PHY_AFE_CTL_OVR, 0x0007);
}

static int
bwn_phy_lp_b2063_switch_channel(struct bwn_mac *mac, uint8_t chan)
{
	static const struct bwn_b206x_chan *bc = NULL;
	struct bwn_softc *sc = mac->mac_sc;
	uint32_t count, freqref, freqvco, val[3], timeout, timeoutref,
	    tmp[6];
	uint16_t old, scale, tmp16;
	u_int freqxtal;
	int error, i, div;

	for (i = 0; i < N(bwn_b2063_chantable); i++) {
		if (bwn_b2063_chantable[i].bc_chan == chan) {
			bc = &bwn_b2063_chantable[i];
			break;
		}
	}
	if (bc == NULL)
		return (EINVAL);

	error = bhnd_get_clock_freq(sc->sc_dev, BHND_CLOCK_ALP, &freqxtal);
	if (error) {
		device_printf(sc->sc_dev, "failed to fetch clock frequency: %d",
		    error);
		return (error);
	}

	BWN_RF_WRITE(mac, BWN_B2063_LOGEN_VCOBUF1, bc->bc_data[0]);
	BWN_RF_WRITE(mac, BWN_B2063_LOGEN_MIXER2, bc->bc_data[1]);
	BWN_RF_WRITE(mac, BWN_B2063_LOGEN_BUF2, bc->bc_data[2]);
	BWN_RF_WRITE(mac, BWN_B2063_LOGEN_RCCR1, bc->bc_data[3]);
	BWN_RF_WRITE(mac, BWN_B2063_A_RX_1ST3, bc->bc_data[4]);
	BWN_RF_WRITE(mac, BWN_B2063_A_RX_2ND1, bc->bc_data[5]);
	BWN_RF_WRITE(mac, BWN_B2063_A_RX_2ND4, bc->bc_data[6]);
	BWN_RF_WRITE(mac, BWN_B2063_A_RX_2ND7, bc->bc_data[7]);
	BWN_RF_WRITE(mac, BWN_B2063_A_RX_PS6, bc->bc_data[8]);
	BWN_RF_WRITE(mac, BWN_B2063_TX_RF_CTL2, bc->bc_data[9]);
	BWN_RF_WRITE(mac, BWN_B2063_TX_RF_CTL5, bc->bc_data[10]);
	BWN_RF_WRITE(mac, BWN_B2063_PA_CTL11, bc->bc_data[11]);

	old = BWN_RF_READ(mac, BWN_B2063_COM15);
	BWN_RF_SET(mac, BWN_B2063_COM15, 0x1e);

	freqvco = bc->bc_freq << ((bc->bc_freq > 4000) ? 1 : 2);
	freqref = freqxtal * 3;
	div = (freqxtal <= 26000000 ? 1 : 2);
	timeout = ((((8 * freqxtal) / (div * 5000000)) + 1) >> 1) - 1;
	timeoutref = ((((8 * freqxtal) / (div * (timeout + 1))) +
		999999) / 1000000) + 1;

	BWN_RF_WRITE(mac, BWN_B2063_JTAG_VCO_CALIB3, 0x2);
	BWN_RF_SETMASK(mac, BWN_B2063_JTAG_VCO_CALIB6,
	    0xfff8, timeout >> 2);
	BWN_RF_SETMASK(mac, BWN_B2063_JTAG_VCO_CALIB7,
	    0xff9f,timeout << 5);
	BWN_RF_WRITE(mac, BWN_B2063_JTAG_VCO_CALIB5, timeoutref);

	val[0] = bwn_phy_lp_roundup(freqxtal, 1000000, 16);
	val[1] = bwn_phy_lp_roundup(freqxtal, 1000000 * div, 16);
	val[2] = bwn_phy_lp_roundup(freqvco, 3, 16);

	count = (bwn_phy_lp_roundup(val[2], val[1] + 16, 16) * (timeout + 1) *
	    (timeoutref + 1)) - 1;
	BWN_RF_SETMASK(mac, BWN_B2063_JTAG_VCO_CALIB7,
	    0xf0, count >> 8);
	BWN_RF_WRITE(mac, BWN_B2063_JTAG_VCO_CALIB8, count & 0xff);

	tmp[0] = ((val[2] * 62500) / freqref) << 4;
	tmp[1] = ((val[2] * 62500) % freqref) << 4;
	while (tmp[1] >= freqref) {
		tmp[0]++;
		tmp[1] -= freqref;
	}
	BWN_RF_SETMASK(mac, BWN_B2063_JTAG_SG1, 0xffe0, tmp[0] >> 4);
	BWN_RF_SETMASK(mac, BWN_B2063_JTAG_SG2, 0xfe0f, tmp[0] << 4);
	BWN_RF_SETMASK(mac, BWN_B2063_JTAG_SG2, 0xfff0, tmp[0] >> 16);
	BWN_RF_WRITE(mac, BWN_B2063_JTAG_SG3, (tmp[1] >> 8) & 0xff);
	BWN_RF_WRITE(mac, BWN_B2063_JTAG_SG4, tmp[1] & 0xff);

	BWN_RF_WRITE(mac, BWN_B2063_JTAG_LF1, 0xb9);
	BWN_RF_WRITE(mac, BWN_B2063_JTAG_LF2, 0x88);
	BWN_RF_WRITE(mac, BWN_B2063_JTAG_LF3, 0x28);
	BWN_RF_WRITE(mac, BWN_B2063_JTAG_LF4, 0x63);

	tmp[2] = ((41 * (val[2] - 3000)) /1200) + 27;
	tmp[3] = bwn_phy_lp_roundup(132000 * tmp[0], 8451, 16);

	if (howmany(tmp[3], tmp[2]) > 60) {
		scale = 1;
		tmp[4] = ((tmp[3] + tmp[2]) / (tmp[2] << 1)) - 8;
	} else {
		scale = 0;
		tmp[4] = ((tmp[3] + (tmp[2] >> 1)) / tmp[2]) - 8;
	}
	BWN_RF_SETMASK(mac, BWN_B2063_JTAG_CP2, 0xffc0, tmp[4]);
	BWN_RF_SETMASK(mac, BWN_B2063_JTAG_CP2, 0xffbf, scale << 6);

	tmp[5] = bwn_phy_lp_roundup(100 * val[0], val[2], 16) * (tmp[4] * 8) *
	    (scale + 1);
	if (tmp[5] > 150)
		tmp[5] = 0;

	BWN_RF_SETMASK(mac, BWN_B2063_JTAG_CP3, 0xffe0, tmp[5]);
	BWN_RF_SETMASK(mac, BWN_B2063_JTAG_CP3, 0xffdf, scale << 5);

	BWN_RF_SETMASK(mac, BWN_B2063_JTAG_XTAL_12, 0xfffb, 0x4);
	if (freqxtal > 26000000)
		BWN_RF_SET(mac, BWN_B2063_JTAG_XTAL_12, 0x2);
	else
		BWN_RF_MASK(mac, BWN_B2063_JTAG_XTAL_12, 0xfd);

	if (val[0] == 45)
		BWN_RF_SET(mac, BWN_B2063_JTAG_VCO1, 0x2);
	else
		BWN_RF_MASK(mac, BWN_B2063_JTAG_VCO1, 0xfd);

	BWN_RF_SET(mac, BWN_B2063_PLL_SP2, 0x3);
	DELAY(1);
	BWN_RF_MASK(mac, BWN_B2063_PLL_SP2, 0xfffc);

	/* VCO Calibration */
	BWN_RF_MASK(mac, BWN_B2063_PLL_SP1, ~0x40);
	tmp16 = BWN_RF_READ(mac, BWN_B2063_JTAG_CALNRST) & 0xf8;
	BWN_RF_WRITE(mac, BWN_B2063_JTAG_CALNRST, tmp16);
	DELAY(1);
	BWN_RF_WRITE(mac, BWN_B2063_JTAG_CALNRST, tmp16 | 0x4);
	DELAY(1);
	BWN_RF_WRITE(mac, BWN_B2063_JTAG_CALNRST, tmp16 | 0x6);
	DELAY(1);
	BWN_RF_WRITE(mac, BWN_B2063_JTAG_CALNRST, tmp16 | 0x7);
	DELAY(300);
	BWN_RF_SET(mac, BWN_B2063_PLL_SP1, 0x40);

	BWN_RF_WRITE(mac, BWN_B2063_COM15, old);
	return (0);
}

static int
bwn_phy_lp_b2062_switch_channel(struct bwn_mac *mac, uint8_t chan)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	const struct bwn_b206x_chan *bc = NULL;
	uint32_t tmp[9];
	u_int freqxtal;
	int error, i;

	for (i = 0; i < N(bwn_b2062_chantable); i++) {
		if (bwn_b2062_chantable[i].bc_chan == chan) {
			bc = &bwn_b2062_chantable[i];
			break;
		}
	}

	if (bc == NULL)
		return (EINVAL);

	error = bhnd_get_clock_freq(sc->sc_dev, BHND_CLOCK_ALP, &freqxtal);
	if (error) {
		device_printf(sc->sc_dev, "failed to fetch clock frequency: %d",
		    error);
		return (error);
	}

	BWN_RF_SET(mac, BWN_B2062_S_RFPLLCTL14, 0x04);
	BWN_RF_WRITE(mac, BWN_B2062_N_LGENATUNE0, bc->bc_data[0]);
	BWN_RF_WRITE(mac, BWN_B2062_N_LGENATUNE2, bc->bc_data[1]);
	BWN_RF_WRITE(mac, BWN_B2062_N_LGENATUNE3, bc->bc_data[2]);
	BWN_RF_WRITE(mac, BWN_B2062_N_TX_TUNE, bc->bc_data[3]);
	BWN_RF_WRITE(mac, BWN_B2062_S_LGENG_CTL1, bc->bc_data[4]);
	BWN_RF_WRITE(mac, BWN_B2062_N_LGENACTL5, bc->bc_data[5]);
	BWN_RF_WRITE(mac, BWN_B2062_N_LGENACTL6, bc->bc_data[6]);
	BWN_RF_WRITE(mac, BWN_B2062_N_TX_PGA, bc->bc_data[7]);
	BWN_RF_WRITE(mac, BWN_B2062_N_TX_PAD, bc->bc_data[8]);

	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL33, 0xcc);
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL34, 0x07);
	bwn_phy_lp_b2062_reset_pllbias(mac);
	tmp[0] = freqxtal / 1000;
	tmp[1] = plp->plp_div * 1000;
	tmp[2] = tmp[1] * ieee80211_ieee2mhz(chan, 0);
	if (ieee80211_ieee2mhz(chan, 0) < 4000)
		tmp[2] *= 2;
	tmp[3] = 48 * tmp[0];
	tmp[5] = tmp[2] / tmp[3];
	tmp[6] = tmp[2] % tmp[3];
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL26, tmp[5]);
	tmp[4] = tmp[6] * 0x100;
	tmp[5] = tmp[4] / tmp[3];
	tmp[6] = tmp[4] % tmp[3];
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL27, tmp[5]);
	tmp[4] = tmp[6] * 0x100;
	tmp[5] = tmp[4] / tmp[3];
	tmp[6] = tmp[4] % tmp[3];
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL28, tmp[5]);
	tmp[4] = tmp[6] * 0x100;
	tmp[5] = tmp[4] / tmp[3];
	tmp[6] = tmp[4] % tmp[3];
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL29,
	    tmp[5] + ((2 * tmp[6]) / tmp[3]));
	tmp[7] = BWN_RF_READ(mac, BWN_B2062_S_RFPLLCTL19);
	tmp[8] = ((2 * tmp[2] * (tmp[7] + 1)) + (3 * tmp[0])) / (6 * tmp[0]);
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL23, (tmp[8] >> 8) + 16);
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL24, tmp[8] & 0xff);

	bwn_phy_lp_b2062_vco_calib(mac);
	if (BWN_RF_READ(mac, BWN_B2062_S_RFPLLCTL3) & 0x10) {
		BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL33, 0xfc);
		BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL34, 0);
		bwn_phy_lp_b2062_reset_pllbias(mac);
		bwn_phy_lp_b2062_vco_calib(mac);
		if (BWN_RF_READ(mac, BWN_B2062_S_RFPLLCTL3) & 0x10) {
			BWN_RF_MASK(mac, BWN_B2062_S_RFPLLCTL14, ~0x04);
			return (EIO);
		}
	}
	BWN_RF_MASK(mac, BWN_B2062_S_RFPLLCTL14, ~0x04);
	return (0);
}

static void
bwn_phy_lp_set_anafilter(struct bwn_mac *mac, uint8_t channel)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	uint16_t tmp = (channel == 14);

	if (mac->mac_phy.rev < 2) {
		BWN_PHY_SETMASK(mac, BWN_PHY_LP_PHY_CTL, 0xfcff, tmp << 9);
		if ((mac->mac_phy.rev == 1) && (plp->plp_rccap))
			bwn_phy_lp_set_rccap(mac);
		return;
	}

	BWN_RF_WRITE(mac, BWN_B2063_TX_BB_SP3, 0x3f);
}

static void
bwn_phy_lp_set_gaintbl(struct bwn_mac *mac, uint32_t freq)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t iso, tmp[3];

	KASSERT(mac->mac_phy.rev < 2, ("%s:%d: fail", __func__, __LINE__));

	if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan))
		iso = plp->plp_txisoband_m;
	else if (freq <= 5320)
		iso = plp->plp_txisoband_l;
	else if (freq <= 5700)
		iso = plp->plp_txisoband_m;
	else
		iso = plp->plp_txisoband_h;

	tmp[0] = ((iso - 26) / 12) << 12;
	tmp[1] = tmp[0] + 0x1000;
	tmp[2] = tmp[0] + 0x2000;

	bwn_tab_write_multi(mac, BWN_TAB_2(13, 0), 3, tmp);
	bwn_tab_write_multi(mac, BWN_TAB_2(12, 0), 3, tmp);
}

static void
bwn_phy_lp_digflt_save(struct bwn_mac *mac)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	int i;
	static const uint16_t addr[] = {
		BWN_PHY_OFDM(0xc1), BWN_PHY_OFDM(0xc2),
		BWN_PHY_OFDM(0xc3), BWN_PHY_OFDM(0xc4),
		BWN_PHY_OFDM(0xc5), BWN_PHY_OFDM(0xc6),
		BWN_PHY_OFDM(0xc7), BWN_PHY_OFDM(0xc8),
		BWN_PHY_OFDM(0xcf),
	};
	static const uint16_t val[] = {
		0xde5e, 0xe832, 0xe331, 0x4d26,
		0x0026, 0x1420, 0x0020, 0xfe08,
		0x0008,
	};

	for (i = 0; i < N(addr); i++) {
		plp->plp_digfilt[i] = BWN_PHY_READ(mac, addr[i]);
		BWN_PHY_WRITE(mac, addr[i], val[i]);
	}
}

static void
bwn_phy_lp_get_txpctlmode(struct bwn_mac *mac)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	struct bwn_softc *sc = mac->mac_sc;
	uint16_t ctl;

	ctl = BWN_PHY_READ(mac, BWN_PHY_TX_PWR_CTL_CMD);
	switch (ctl & BWN_PHY_TX_PWR_CTL_CMD_MODE) {
	case BWN_PHY_TX_PWR_CTL_CMD_MODE_OFF:
		plp->plp_txpctlmode = BWN_PHYLP_TXPCTL_OFF;
		break;
	case BWN_PHY_TX_PWR_CTL_CMD_MODE_SW:
		plp->plp_txpctlmode = BWN_PHYLP_TXPCTL_ON_SW;
		break;
	case BWN_PHY_TX_PWR_CTL_CMD_MODE_HW:
		plp->plp_txpctlmode = BWN_PHYLP_TXPCTL_ON_HW;
		break;
	default:
		plp->plp_txpctlmode = BWN_PHYLP_TXPCTL_UNKNOWN;
		device_printf(sc->sc_dev, "unknown command mode\n");
		break;
	}
}

static void
bwn_phy_lp_set_txpctlmode(struct bwn_mac *mac, uint8_t mode)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	uint16_t ctl;
	uint8_t old;

	bwn_phy_lp_get_txpctlmode(mac);
	old = plp->plp_txpctlmode;
	if (old == mode)
		return;
	plp->plp_txpctlmode = mode;

	if (old != BWN_PHYLP_TXPCTL_ON_HW && mode == BWN_PHYLP_TXPCTL_ON_HW) {
		BWN_PHY_SETMASK(mac, BWN_PHY_TX_PWR_CTL_CMD, 0xff80,
		    plp->plp_tssiidx);
		BWN_PHY_SETMASK(mac, BWN_PHY_TX_PWR_CTL_NNUM,
		    0x8fff, ((uint16_t)plp->plp_tssinpt << 16));

		/* disable TX GAIN override */
		if (mac->mac_phy.rev < 2)
			BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2, 0xfeff);
		else {
			BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2, 0xff7f);
			BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2, 0xbfff);
		}
		BWN_PHY_MASK(mac, BWN_PHY_AFE_CTL_OVR, 0xffbf);

		plp->plp_txpwridx = -1;
	}
	if (mac->mac_phy.rev >= 2) {
		if (mode == BWN_PHYLP_TXPCTL_ON_HW)
			BWN_PHY_SET(mac, BWN_PHY_OFDM(0xd0), 0x2);
		else
			BWN_PHY_MASK(mac, BWN_PHY_OFDM(0xd0), 0xfffd);
	}

	/* writes TX Power Control mode */
	switch (plp->plp_txpctlmode) {
	case BWN_PHYLP_TXPCTL_OFF:
		ctl = BWN_PHY_TX_PWR_CTL_CMD_MODE_OFF;
		break;
	case BWN_PHYLP_TXPCTL_ON_HW:
		ctl = BWN_PHY_TX_PWR_CTL_CMD_MODE_HW;
		break;
	case BWN_PHYLP_TXPCTL_ON_SW:
		ctl = BWN_PHY_TX_PWR_CTL_CMD_MODE_SW;
		break;
	default:
		ctl = 0;
		KASSERT(0 == 1, ("%s:%d: fail", __func__, __LINE__));
	}
	BWN_PHY_SETMASK(mac, BWN_PHY_TX_PWR_CTL_CMD,
	    (uint16_t)~BWN_PHY_TX_PWR_CTL_CMD_MODE, ctl);
}

static void
bwn_phy_lp_bugfix(struct bwn_mac *mac)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	struct bwn_softc *sc = mac->mac_sc;
	const unsigned int size = 256;
	struct bwn_txgain tg;
	uint32_t rxcomp, txgain, coeff, rfpwr, *tabs;
	uint16_t tssinpt, tssiidx, value[2];
	uint8_t mode;
	int8_t txpwridx;

	tabs = (uint32_t *)malloc(sizeof(uint32_t) * size, M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (tabs == NULL) {
		device_printf(sc->sc_dev, "failed to allocate buffer.\n");
		return;
	}

	bwn_phy_lp_get_txpctlmode(mac);
	mode = plp->plp_txpctlmode;
	txpwridx = plp->plp_txpwridx;
	tssinpt = plp->plp_tssinpt;
	tssiidx = plp->plp_tssiidx;

	bwn_tab_read_multi(mac,
	    (mac->mac_phy.rev < 2) ? BWN_TAB_4(10, 0x140) :
	    BWN_TAB_4(7, 0x140), size, tabs);

	bwn_phy_lp_tblinit(mac);
	bwn_phy_lp_bbinit(mac);
	bwn_phy_lp_txpctl_init(mac);
	bwn_phy_lp_rf_onoff(mac, 1);
	bwn_phy_lp_set_txpctlmode(mac, BWN_PHYLP_TXPCTL_OFF);

	bwn_tab_write_multi(mac,
	    (mac->mac_phy.rev < 2) ? BWN_TAB_4(10, 0x140) :
	    BWN_TAB_4(7, 0x140), size, tabs);

	BWN_WRITE_2(mac, BWN_CHANNEL, plp->plp_chan);
	plp->plp_tssinpt = tssinpt;
	plp->plp_tssiidx = tssiidx;
	bwn_phy_lp_set_anafilter(mac, plp->plp_chan);
	if (txpwridx != -1) {
		/* set TX power by index */
		plp->plp_txpwridx = txpwridx;
		bwn_phy_lp_get_txpctlmode(mac);
		if (plp->plp_txpctlmode != BWN_PHYLP_TXPCTL_OFF)
			bwn_phy_lp_set_txpctlmode(mac, BWN_PHYLP_TXPCTL_ON_SW);
		if (mac->mac_phy.rev >= 2) {
			rxcomp = bwn_tab_read(mac,
			    BWN_TAB_4(7, txpwridx + 320));
			txgain = bwn_tab_read(mac,
			    BWN_TAB_4(7, txpwridx + 192));
			tg.tg_pad = (txgain >> 16) & 0xff;
			tg.tg_gm = txgain & 0xff;
			tg.tg_pga = (txgain >> 8) & 0xff;
			tg.tg_dac = (rxcomp >> 28) & 0xff;
			bwn_phy_lp_set_txgain(mac, &tg);
		} else {
			rxcomp = bwn_tab_read(mac,
			    BWN_TAB_4(10, txpwridx + 320));
			txgain = bwn_tab_read(mac,
			    BWN_TAB_4(10, txpwridx + 192));
			BWN_PHY_SETMASK(mac, BWN_PHY_TX_GAIN_CTL_OVERRIDE_VAL,
			    0xf800, (txgain >> 4) & 0x7fff);
			bwn_phy_lp_set_txgain_dac(mac, txgain & 0x7);
			bwn_phy_lp_set_txgain_pa(mac, (txgain >> 24) & 0x7f);
		}
		bwn_phy_lp_set_bbmult(mac, (rxcomp >> 20) & 0xff);

		/* set TX IQCC */
		value[0] = (rxcomp >> 10) & 0x3ff;
		value[1] = rxcomp & 0x3ff;
		bwn_tab_write_multi(mac, BWN_TAB_2(0, 80), 2, value);

		coeff = bwn_tab_read(mac,
		    (mac->mac_phy.rev >= 2) ? BWN_TAB_4(7, txpwridx + 448) :
		    BWN_TAB_4(10, txpwridx + 448));
		bwn_tab_write(mac, BWN_TAB_2(0, 85), coeff & 0xffff);
		if (mac->mac_phy.rev >= 2) {
			rfpwr = bwn_tab_read(mac,
			    BWN_TAB_4(7, txpwridx + 576));
			BWN_PHY_SETMASK(mac, BWN_PHY_RF_PWR_OVERRIDE, 0xff00,
			    rfpwr & 0xffff);
		}
		bwn_phy_lp_set_txgain_override(mac);
	}
	if (plp->plp_rccap)
		bwn_phy_lp_set_rccap(mac);
	bwn_phy_lp_set_antenna(mac, plp->plp_antenna);
	bwn_phy_lp_set_txpctlmode(mac, mode);
	free(tabs, M_DEVBUF);
}

static void
bwn_phy_lp_digflt_restore(struct bwn_mac *mac)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	int i;
	static const uint16_t addr[] = {
		BWN_PHY_OFDM(0xc1), BWN_PHY_OFDM(0xc2),
		BWN_PHY_OFDM(0xc3), BWN_PHY_OFDM(0xc4),
		BWN_PHY_OFDM(0xc5), BWN_PHY_OFDM(0xc6),
		BWN_PHY_OFDM(0xc7), BWN_PHY_OFDM(0xc8),
		BWN_PHY_OFDM(0xcf),
	};

	for (i = 0; i < N(addr); i++)
		BWN_PHY_WRITE(mac, addr[i], plp->plp_digfilt[i]);
}

static void
bwn_phy_lp_tblinit(struct bwn_mac *mac)
{
	uint32_t freq = ieee80211_ieee2mhz(bwn_phy_lp_get_default_chan(mac), 0);

	if (mac->mac_phy.rev < 2) {
		bwn_phy_lp_tblinit_r01(mac);
		bwn_phy_lp_tblinit_txgain(mac);
		bwn_phy_lp_set_gaintbl(mac, freq);
		return;
	}

	bwn_phy_lp_tblinit_r2(mac);
	bwn_phy_lp_tblinit_txgain(mac);
}

struct bwn_wpair {
	uint16_t		reg;
	uint16_t		value;
};

struct bwn_smpair {
	uint16_t		offset;
	uint16_t		mask;
	uint16_t		set;
};

static void
bwn_phy_lp_bbinit_r2(struct bwn_mac *mac)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	static const struct bwn_wpair v1[] = {
		{ BWN_PHY_AFE_DAC_CTL, 0x50 },
		{ BWN_PHY_AFE_CTL, 0x8800 },
		{ BWN_PHY_AFE_CTL_OVR, 0 },
		{ BWN_PHY_AFE_CTL_OVRVAL, 0 },
		{ BWN_PHY_RF_OVERRIDE_0, 0 },
		{ BWN_PHY_RF_OVERRIDE_2, 0 },
		{ BWN_PHY_OFDM(0xf9), 0 },
		{ BWN_PHY_TR_LOOKUP_1, 0 }
	};
	static const struct bwn_smpair v2[] = {
		{ BWN_PHY_OFDMSYNCTHRESH0, 0xff00, 0xb4 },
		{ BWN_PHY_DCOFFSETTRANSIENT, 0xf8ff, 0x200 },
		{ BWN_PHY_DCOFFSETTRANSIENT, 0xff00, 0x7f },
		{ BWN_PHY_GAINDIRECTMISMATCH, 0xff0f, 0x40 },
		{ BWN_PHY_PREAMBLECONFIRMTO, 0xff00, 0x2 }
	};
	static const struct bwn_smpair v3[] = {
		{ BWN_PHY_OFDM(0xfe), 0xffe0, 0x1f },
		{ BWN_PHY_OFDM(0xff), 0xffe0, 0xc },
		{ BWN_PHY_OFDM(0x100), 0xff00, 0x19 },
		{ BWN_PHY_OFDM(0xff), 0x03ff, 0x3c00 },
		{ BWN_PHY_OFDM(0xfe), 0xfc1f, 0x3e0 },
		{ BWN_PHY_OFDM(0xff), 0xffe0, 0xc },
		{ BWN_PHY_OFDM(0x100), 0x00ff, 0x1900 },
		{ BWN_PHY_CLIPCTRTHRESH, 0x83ff, 0x5800 },
		{ BWN_PHY_CLIPCTRTHRESH, 0xffe0, 0x12 },
		{ BWN_PHY_GAINMISMATCH, 0x0fff, 0x9000 },

	};
	int i;

	for (i = 0; i < N(v1); i++)
		BWN_PHY_WRITE(mac, v1[i].reg, v1[i].value);
	BWN_PHY_SET(mac, BWN_PHY_ADC_COMPENSATION_CTL, 0x10);
	for (i = 0; i < N(v2); i++)
		BWN_PHY_SETMASK(mac, v2[i].offset, v2[i].mask, v2[i].set);

	BWN_PHY_MASK(mac, BWN_PHY_CRSGAIN_CTL, ~0x4000);
	BWN_PHY_MASK(mac, BWN_PHY_CRSGAIN_CTL, ~0x2000);
	BWN_PHY_SET(mac, BWN_PHY_OFDM(0x10a), 0x1);
	if (sc->sc_board_info.board_rev >= 0x18) {
		bwn_tab_write(mac, BWN_TAB_4(17, 65), 0xec);
		BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x10a), 0xff01, 0x14);
	} else {
		BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x10a), 0xff01, 0x10);
	}
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0xdf), 0xff00, 0xf4);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0xdf), 0x00ff, 0xf100);
	BWN_PHY_WRITE(mac, BWN_PHY_CLIPTHRESH, 0x48);
	BWN_PHY_SETMASK(mac, BWN_PHY_HIGAINDB, 0xff00, 0x46);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0xe4), 0xff00, 0x10);
	BWN_PHY_SETMASK(mac, BWN_PHY_PWR_THRESH1, 0xfff0, 0x9);
	BWN_PHY_MASK(mac, BWN_PHY_GAINDIRECTMISMATCH, ~0xf);
	BWN_PHY_SETMASK(mac, BWN_PHY_VERYLOWGAINDB, 0x00ff, 0x5500);
	BWN_PHY_SETMASK(mac, BWN_PHY_CLIPCTRTHRESH, 0xfc1f, 0xa0);
	BWN_PHY_SETMASK(mac, BWN_PHY_GAINDIRECTMISMATCH, 0xe0ff, 0x300);
	BWN_PHY_SETMASK(mac, BWN_PHY_HIGAINDB, 0x00ff, 0x2a00);
	if (sc->sc_cid.chip_id == BHND_CHIPID_BCM4325 &&
	    sc->sc_cid.chip_pkg == 0) {
		BWN_PHY_SETMASK(mac, BWN_PHY_LOWGAINDB, 0x00ff, 0x2100);
		BWN_PHY_SETMASK(mac, BWN_PHY_VERYLOWGAINDB, 0xff00, 0xa);
	} else {
		BWN_PHY_SETMASK(mac, BWN_PHY_LOWGAINDB, 0x00ff, 0x1e00);
		BWN_PHY_SETMASK(mac, BWN_PHY_VERYLOWGAINDB, 0xff00, 0xd);
	}
	for (i = 0; i < N(v3); i++)
		BWN_PHY_SETMASK(mac, v3[i].offset, v3[i].mask, v3[i].set);
	if (sc->sc_cid.chip_id == BHND_CHIPID_BCM4325 &&
	    sc->sc_cid.chip_pkg == 0) {
		bwn_tab_write(mac, BWN_TAB_2(0x08, 0x14), 0);
		bwn_tab_write(mac, BWN_TAB_2(0x08, 0x12), 0x40);
	}

	if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
		BWN_PHY_SET(mac, BWN_PHY_CRSGAIN_CTL, 0x40);
		BWN_PHY_SETMASK(mac, BWN_PHY_CRSGAIN_CTL, 0xf0ff, 0xb00);
		BWN_PHY_SETMASK(mac, BWN_PHY_SYNCPEAKCNT, 0xfff8, 0x6);
		BWN_PHY_SETMASK(mac, BWN_PHY_MINPWR_LEVEL, 0x00ff, 0x9d00);
		BWN_PHY_SETMASK(mac, BWN_PHY_MINPWR_LEVEL, 0xff00, 0xa1);
		BWN_PHY_MASK(mac, BWN_PHY_IDLEAFTERPKTRXTO, 0x00ff);
	} else
		BWN_PHY_MASK(mac, BWN_PHY_CRSGAIN_CTL, ~0x40);

	BWN_PHY_SETMASK(mac, BWN_PHY_CRS_ED_THRESH, 0xff00, 0xb3);
	BWN_PHY_SETMASK(mac, BWN_PHY_CRS_ED_THRESH, 0x00ff, 0xad00);
	BWN_PHY_SETMASK(mac, BWN_PHY_INPUT_PWRDB, 0xff00, plp->plp_rxpwroffset);
	BWN_PHY_SET(mac, BWN_PHY_RESET_CTL, 0x44);
	BWN_PHY_WRITE(mac, BWN_PHY_RESET_CTL, 0x80);
	BWN_PHY_WRITE(mac, BWN_PHY_AFE_RSSI_CTL_0, 0xa954);
	BWN_PHY_WRITE(mac, BWN_PHY_AFE_RSSI_CTL_1,
	    0x2000 | ((uint16_t)plp->plp_rssigs << 10) |
	    ((uint16_t)plp->plp_rssivc << 4) | plp->plp_rssivf);

	if (sc->sc_cid.chip_id == BHND_CHIPID_BCM4325 &&
	    sc->sc_cid.chip_pkg == 0) {
		BWN_PHY_SET(mac, BWN_PHY_AFE_ADC_CTL_0, 0x1c);
		BWN_PHY_SETMASK(mac, BWN_PHY_AFE_CTL, 0x00ff, 0x8800);
		BWN_PHY_SETMASK(mac, BWN_PHY_AFE_ADC_CTL_1, 0xfc3c, 0x0400);
	}

	bwn_phy_lp_digflt_save(mac);
}

static void
bwn_phy_lp_bbinit_r01(struct bwn_mac *mac)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	static const struct bwn_smpair v1[] = {
		{ BWN_PHY_CLIPCTRTHRESH, 0xffe0, 0x0005 },
		{ BWN_PHY_CLIPCTRTHRESH, 0xfc1f, 0x0180 },
		{ BWN_PHY_CLIPCTRTHRESH, 0x83ff, 0x3c00 },
		{ BWN_PHY_GAINDIRECTMISMATCH, 0xfff0, 0x0005 },
		{ BWN_PHY_GAIN_MISMATCH_LIMIT, 0xffc0, 0x001a },
		{ BWN_PHY_CRS_ED_THRESH, 0xff00, 0x00b3 },
		{ BWN_PHY_CRS_ED_THRESH, 0x00ff, 0xad00 }
	};
	static const struct bwn_smpair v2[] = {
		{ BWN_PHY_TR_LOOKUP_1, 0xffc0, 0x000a },
		{ BWN_PHY_TR_LOOKUP_1, 0x3f00, 0x0900 },
		{ BWN_PHY_TR_LOOKUP_2, 0xffc0, 0x000a },
		{ BWN_PHY_TR_LOOKUP_2, 0xc0ff, 0x0b00 },
		{ BWN_PHY_TR_LOOKUP_3, 0xffc0, 0x000a },
		{ BWN_PHY_TR_LOOKUP_3, 0xc0ff, 0x0400 },
		{ BWN_PHY_TR_LOOKUP_4, 0xffc0, 0x000a },
		{ BWN_PHY_TR_LOOKUP_4, 0xc0ff, 0x0b00 },
		{ BWN_PHY_TR_LOOKUP_5, 0xffc0, 0x000a },
		{ BWN_PHY_TR_LOOKUP_5, 0xc0ff, 0x0900 },
		{ BWN_PHY_TR_LOOKUP_6, 0xffc0, 0x000a },
		{ BWN_PHY_TR_LOOKUP_6, 0xc0ff, 0x0b00 },
		{ BWN_PHY_TR_LOOKUP_7, 0xffc0, 0x000a },
		{ BWN_PHY_TR_LOOKUP_7, 0xc0ff, 0x0900 },
		{ BWN_PHY_TR_LOOKUP_8, 0xffc0, 0x000a },
		{ BWN_PHY_TR_LOOKUP_8, 0xc0ff, 0x0b00 }
	};
	static const struct bwn_smpair v3[] = {
		{ BWN_PHY_TR_LOOKUP_1, 0xffc0, 0x0001 },
		{ BWN_PHY_TR_LOOKUP_1, 0xc0ff, 0x0400 },
		{ BWN_PHY_TR_LOOKUP_2, 0xffc0, 0x0001 },
		{ BWN_PHY_TR_LOOKUP_2, 0xc0ff, 0x0500 },
		{ BWN_PHY_TR_LOOKUP_3, 0xffc0, 0x0002 },
		{ BWN_PHY_TR_LOOKUP_3, 0xc0ff, 0x0800 },
		{ BWN_PHY_TR_LOOKUP_4, 0xffc0, 0x0002 },
		{ BWN_PHY_TR_LOOKUP_4, 0xc0ff, 0x0a00 }
	};
	static const struct bwn_smpair v4[] = {
		{ BWN_PHY_TR_LOOKUP_1, 0xffc0, 0x0004 },
		{ BWN_PHY_TR_LOOKUP_1, 0xc0ff, 0x0800 },
		{ BWN_PHY_TR_LOOKUP_2, 0xffc0, 0x0004 },
		{ BWN_PHY_TR_LOOKUP_2, 0xc0ff, 0x0c00 },
		{ BWN_PHY_TR_LOOKUP_3, 0xffc0, 0x0002 },
		{ BWN_PHY_TR_LOOKUP_3, 0xc0ff, 0x0100 },
		{ BWN_PHY_TR_LOOKUP_4, 0xffc0, 0x0002 },
		{ BWN_PHY_TR_LOOKUP_4, 0xc0ff, 0x0300 }
	};
	static const struct bwn_smpair v5[] = {
		{ BWN_PHY_TR_LOOKUP_1, 0xffc0, 0x000a },
		{ BWN_PHY_TR_LOOKUP_1, 0xc0ff, 0x0900 },
		{ BWN_PHY_TR_LOOKUP_2, 0xffc0, 0x000a },
		{ BWN_PHY_TR_LOOKUP_2, 0xc0ff, 0x0b00 },
		{ BWN_PHY_TR_LOOKUP_3, 0xffc0, 0x0006 },
		{ BWN_PHY_TR_LOOKUP_3, 0xc0ff, 0x0500 },
		{ BWN_PHY_TR_LOOKUP_4, 0xffc0, 0x0006 },
		{ BWN_PHY_TR_LOOKUP_4, 0xc0ff, 0x0700 }
	};
	int error, i;
	uint16_t tmp, tmp2;

	BWN_PHY_MASK(mac, BWN_PHY_AFE_DAC_CTL, 0xf7ff);
	BWN_PHY_WRITE(mac, BWN_PHY_AFE_CTL, 0);
	BWN_PHY_WRITE(mac, BWN_PHY_AFE_CTL_OVR, 0);
	BWN_PHY_WRITE(mac, BWN_PHY_RF_OVERRIDE_0, 0);
	BWN_PHY_WRITE(mac, BWN_PHY_RF_OVERRIDE_2, 0);
	BWN_PHY_SET(mac, BWN_PHY_AFE_DAC_CTL, 0x0004);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDMSYNCTHRESH0, 0xff00, 0x0078);
	BWN_PHY_SETMASK(mac, BWN_PHY_CLIPCTRTHRESH, 0x83ff, 0x5800);
	BWN_PHY_WRITE(mac, BWN_PHY_ADC_COMPENSATION_CTL, 0x0016);
	BWN_PHY_SETMASK(mac, BWN_PHY_AFE_ADC_CTL_0, 0xfff8, 0x0004);
	BWN_PHY_SETMASK(mac, BWN_PHY_VERYLOWGAINDB, 0x00ff, 0x5400);
	BWN_PHY_SETMASK(mac, BWN_PHY_HIGAINDB, 0x00ff, 0x2400);
	BWN_PHY_SETMASK(mac, BWN_PHY_LOWGAINDB, 0x00ff, 0x2100);
	BWN_PHY_SETMASK(mac, BWN_PHY_VERYLOWGAINDB, 0xff00, 0x0006);
	BWN_PHY_MASK(mac, BWN_PHY_RX_RADIO_CTL, 0xfffe);
	for (i = 0; i < N(v1); i++)
		BWN_PHY_SETMASK(mac, v1[i].offset, v1[i].mask, v1[i].set);
	BWN_PHY_SETMASK(mac, BWN_PHY_INPUT_PWRDB,
	    0xff00, plp->plp_rxpwroffset);
	if ((sc->sc_board_info.board_flags & BHND_BFL_FEM) &&
	    ((IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan)) ||
	   (sc->sc_board_info.board_flags & BHND_BFL_PAREF))) {
		error = bhnd_pmu_set_voltage_raw(sc->sc_pmu,
		    BHND_REGULATOR_PAREF_LDO, 0x28);
		if (error)
			device_printf(sc->sc_dev, "failed to set PAREF LDO "
			    "voltage: %d\n", error);

		error = bhnd_pmu_enable_regulator(sc->sc_pmu,
		    BHND_REGULATOR_PAREF_LDO);
		if (error)
			device_printf(sc->sc_dev, "failed to enable PAREF LDO "
			    "regulator: %d\n", error);

		if (mac->mac_phy.rev == 0)
			BWN_PHY_SETMASK(mac, BWN_PHY_LP_RF_SIGNAL_LUT,
			    0xffcf, 0x0010);
		bwn_tab_write(mac, BWN_TAB_2(11, 7), 60);
	} else {
		error = bhnd_pmu_disable_regulator(sc->sc_pmu,
		    BHND_REGULATOR_PAREF_LDO);
		if (error)
			device_printf(sc->sc_dev, "failed to disable PAREF LDO "
			    "regulator: %d\n", error);

		BWN_PHY_SETMASK(mac, BWN_PHY_LP_RF_SIGNAL_LUT, 0xffcf, 0x0020);
		bwn_tab_write(mac, BWN_TAB_2(11, 7), 100);
	}
	tmp = plp->plp_rssivf | plp->plp_rssivc << 4 | 0xa000;
	BWN_PHY_WRITE(mac, BWN_PHY_AFE_RSSI_CTL_0, tmp);
	if (sc->sc_board_info.board_flags & BHND_BFL_RSSIINV)
		BWN_PHY_SETMASK(mac, BWN_PHY_AFE_RSSI_CTL_1, 0xf000, 0x0aaa);
	else
		BWN_PHY_SETMASK(mac, BWN_PHY_AFE_RSSI_CTL_1, 0xf000, 0x02aa);
	bwn_tab_write(mac, BWN_TAB_2(11, 1), 24);
	BWN_PHY_SETMASK(mac, BWN_PHY_RX_RADIO_CTL,
	    0xfff9, (plp->plp_bxarch << 1));
	if (mac->mac_phy.rev == 1 &&
	    (sc->sc_board_info.board_flags & BHND_BFL_FEM_BT)) {
		for (i = 0; i < N(v2); i++)
			BWN_PHY_SETMASK(mac, v2[i].offset, v2[i].mask,
			    v2[i].set);
	} else if (IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan) ||
	    (sc->sc_board_info.board_type == 0x048a) ||
	    ((mac->mac_phy.rev == 0) &&
	     (sc->sc_board_info.board_flags & BHND_BFL_FEM))) {
		for (i = 0; i < N(v3); i++)
			BWN_PHY_SETMASK(mac, v3[i].offset, v3[i].mask,
			    v3[i].set);
	} else if (mac->mac_phy.rev == 1 ||
		  (sc->sc_board_info.board_flags & BHND_BFL_FEM)) {
		for (i = 0; i < N(v4); i++)
			BWN_PHY_SETMASK(mac, v4[i].offset, v4[i].mask,
			    v4[i].set);
	} else {
		for (i = 0; i < N(v5); i++)
			BWN_PHY_SETMASK(mac, v5[i].offset, v5[i].mask,
			    v5[i].set);
	}
	if (mac->mac_phy.rev == 1 &&
	    (sc->sc_board_info.board_flags & BHND_BFL_PAREF)) {
		BWN_PHY_COPY(mac, BWN_PHY_TR_LOOKUP_5, BWN_PHY_TR_LOOKUP_1);
		BWN_PHY_COPY(mac, BWN_PHY_TR_LOOKUP_6, BWN_PHY_TR_LOOKUP_2);
		BWN_PHY_COPY(mac, BWN_PHY_TR_LOOKUP_7, BWN_PHY_TR_LOOKUP_3);
		BWN_PHY_COPY(mac, BWN_PHY_TR_LOOKUP_8, BWN_PHY_TR_LOOKUP_4);
	}
	if ((sc->sc_board_info.board_flags & BHND_BFL_FEM_BT) &&
	    (sc->sc_cid.chip_id == BHND_CHIPID_BCM5354) &&
	    (sc->sc_cid.chip_pkg == BHND_PKGID_BCM4712SMALL)) {
		BWN_PHY_SET(mac, BWN_PHY_CRSGAIN_CTL, 0x0006);
		BWN_PHY_WRITE(mac, BWN_PHY_GPIO_SELECT, 0x0005);
		BWN_PHY_WRITE(mac, BWN_PHY_GPIO_OUTEN, 0xffff);
		bwn_hf_write(mac, bwn_hf_read(mac) | BWN_HF_PR45960W);
	}
	if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
		BWN_PHY_SET(mac, BWN_PHY_LP_PHY_CTL, 0x8000);
		BWN_PHY_SET(mac, BWN_PHY_CRSGAIN_CTL, 0x0040);
		BWN_PHY_SETMASK(mac, BWN_PHY_MINPWR_LEVEL, 0x00ff, 0xa400);
		BWN_PHY_SETMASK(mac, BWN_PHY_CRSGAIN_CTL, 0xf0ff, 0x0b00);
		BWN_PHY_SETMASK(mac, BWN_PHY_SYNCPEAKCNT, 0xfff8, 0x0007);
		BWN_PHY_SETMASK(mac, BWN_PHY_DSSS_CONFIRM_CNT, 0xfff8, 0x0003);
		BWN_PHY_SETMASK(mac, BWN_PHY_DSSS_CONFIRM_CNT, 0xffc7, 0x0020);
		BWN_PHY_MASK(mac, BWN_PHY_IDLEAFTERPKTRXTO, 0x00ff);
	} else {
		BWN_PHY_MASK(mac, BWN_PHY_LP_PHY_CTL, 0x7fff);
		BWN_PHY_MASK(mac, BWN_PHY_CRSGAIN_CTL, 0xffbf);
	}
	if (mac->mac_phy.rev == 1) {
		tmp = BWN_PHY_READ(mac, BWN_PHY_CLIPCTRTHRESH);
		tmp2 = (tmp & 0x03e0) >> 5;
		tmp2 |= tmp2 << 5;
		BWN_PHY_WRITE(mac, BWN_PHY_4C3, tmp2);
		tmp = BWN_PHY_READ(mac, BWN_PHY_GAINDIRECTMISMATCH);
		tmp2 = (tmp & 0x1f00) >> 8;
		tmp2 |= tmp2 << 5;
		BWN_PHY_WRITE(mac, BWN_PHY_4C4, tmp2);
		tmp = BWN_PHY_READ(mac, BWN_PHY_VERYLOWGAINDB);
		tmp2 = tmp & 0x00ff;
		tmp2 |= tmp << 8;
		BWN_PHY_WRITE(mac, BWN_PHY_4C5, tmp2);
	}
}

struct bwn_b2062_freq {
	uint16_t		freq;
	uint8_t			value[6];
};

static int
bwn_phy_lp_b2062_init(struct bwn_mac *mac)
{
#define	CALC_CTL7(freq, div)						\
	(((800000000 * (div) + (freq)) / (2 * (freq)) - 8) & 0xff)
#define	CALC_CTL18(freq, div)						\
	((((100 * (freq) + 16000000 * (div)) / (32000000 * (div))) - 1) & 0xff)
#define	CALC_CTL19(freq, div)						\
	((((2 * (freq) + 1000000 * (div)) / (2000000 * (div))) - 1) & 0xff)
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	static const struct bwn_b2062_freq freqdata_tab[] = {
		{ 12000, { 6, 6, 6, 6, 10, 6 } },
		{ 13000, { 4, 4, 4, 4, 11, 7 } },
		{ 14400, { 3, 3, 3, 3, 12, 7 } },
		{ 16200, { 3, 3, 3, 3, 13, 8 } },
		{ 18000, { 2, 2, 2, 2, 14, 8 } },
		{ 19200, { 1, 1, 1, 1, 14, 9 } }
	};
	static const struct bwn_wpair v1[] = {
		{ BWN_B2062_N_TXCTL3, 0 },
		{ BWN_B2062_N_TXCTL4, 0 },
		{ BWN_B2062_N_TXCTL5, 0 },
		{ BWN_B2062_N_TXCTL6, 0 },
		{ BWN_B2062_N_PDNCTL0, 0x40 },
		{ BWN_B2062_N_PDNCTL0, 0 },
		{ BWN_B2062_N_CALIB_TS, 0x10 },
		{ BWN_B2062_N_CALIB_TS, 0 }
	};
	const struct bwn_b2062_freq *f = NULL;
	uint32_t ref;
	u_int xtalfreq;
	unsigned int i;
	int error;

	error = bhnd_get_clock_freq(sc->sc_dev, BHND_CLOCK_ALP, &xtalfreq);
	if (error) {
		device_printf(sc->sc_dev, "failed to fetch clock frequency: %d",
		    error);
		return (error);
	}

	bwn_phy_lp_b2062_tblinit(mac);

	for (i = 0; i < N(v1); i++)
		BWN_RF_WRITE(mac, v1[i].reg, v1[i].value);
	if (mac->mac_phy.rev > 0)
		BWN_RF_WRITE(mac, BWN_B2062_S_BG_CTL1,
		    (BWN_RF_READ(mac, BWN_B2062_N_COM2) >> 1) | 0x80);
	if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan))
		BWN_RF_SET(mac, BWN_B2062_N_TSSI_CTL0, 0x1);
	else
		BWN_RF_MASK(mac, BWN_B2062_N_TSSI_CTL0, ~0x1);

	if (xtalfreq <= 30000000) {
		plp->plp_div = 1;
		BWN_RF_MASK(mac, BWN_B2062_S_RFPLLCTL1, 0xfffb);
	} else {
		plp->plp_div = 2;
		BWN_RF_SET(mac, BWN_B2062_S_RFPLLCTL1, 0x4);
	}

	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL7,
	    CALC_CTL7(xtalfreq, plp->plp_div));
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL18,
	    CALC_CTL18(xtalfreq, plp->plp_div));
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL19,
	    CALC_CTL19(xtalfreq, plp->plp_div));

	ref = (1000 * plp->plp_div + 2 * xtalfreq) / (2000 * plp->plp_div);
	ref &= 0xffff;
	for (i = 0; i < N(freqdata_tab); i++) {
		if (ref < freqdata_tab[i].freq) {
			f = &freqdata_tab[i];
			break;
		}
	}
	if (f == NULL)
		f = &freqdata_tab[N(freqdata_tab) - 1];
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL8,
	    ((uint16_t)(f->value[1]) << 4) | f->value[0]);
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL9,
	    ((uint16_t)(f->value[3]) << 4) | f->value[2]);
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL10, f->value[4]);
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL11, f->value[5]);

	return (0);
#undef CALC_CTL7
#undef CALC_CTL18
#undef CALC_CTL19
}

static int
bwn_phy_lp_b2063_init(struct bwn_mac *mac)
{

	bwn_phy_lp_b2063_tblinit(mac);
	BWN_RF_WRITE(mac, BWN_B2063_LOGEN_SP5, 0);
	BWN_RF_SET(mac, BWN_B2063_COM8, 0x38);
	BWN_RF_WRITE(mac, BWN_B2063_REG_SP1, 0x56);
	BWN_RF_MASK(mac, BWN_B2063_RX_BB_CTL2, ~0x2);
	BWN_RF_WRITE(mac, BWN_B2063_PA_SP7, 0);
	BWN_RF_WRITE(mac, BWN_B2063_TX_RF_SP6, 0x20);
	BWN_RF_WRITE(mac, BWN_B2063_TX_RF_SP9, 0x40);
	if (mac->mac_phy.rev == 2) {
		BWN_RF_WRITE(mac, BWN_B2063_PA_SP3, 0xa0);
		BWN_RF_WRITE(mac, BWN_B2063_PA_SP4, 0xa0);
		BWN_RF_WRITE(mac, BWN_B2063_PA_SP2, 0x18);
	} else {
		BWN_RF_WRITE(mac, BWN_B2063_PA_SP3, 0x20);
		BWN_RF_WRITE(mac, BWN_B2063_PA_SP2, 0x20);
	}

	return (0);
}

static int
bwn_phy_lp_rxcal_r2(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	static const struct bwn_wpair v1[] = {
		{ BWN_B2063_RX_BB_SP8, 0x0 },
		{ BWN_B2063_RC_CALIB_CTL1, 0x7e },
		{ BWN_B2063_RC_CALIB_CTL1, 0x7c },
		{ BWN_B2063_RC_CALIB_CTL2, 0x15 },
		{ BWN_B2063_RC_CALIB_CTL3, 0x70 },
		{ BWN_B2063_RC_CALIB_CTL4, 0x52 },
		{ BWN_B2063_RC_CALIB_CTL5, 0x1 },
		{ BWN_B2063_RC_CALIB_CTL1, 0x7d }
	};
	static const struct bwn_wpair v2[] = {
		{ BWN_B2063_TX_BB_SP3, 0x0 },
		{ BWN_B2063_RC_CALIB_CTL1, 0x7e },
		{ BWN_B2063_RC_CALIB_CTL1, 0x7c },
		{ BWN_B2063_RC_CALIB_CTL2, 0x55 },
		{ BWN_B2063_RC_CALIB_CTL3, 0x76 }
	};
	u_int freqxtal;
	int error, i;
	uint8_t tmp;

	error = bhnd_get_clock_freq(sc->sc_dev, BHND_CLOCK_ALP, &freqxtal);
	if (error) {
		device_printf(sc->sc_dev, "failed to fetch clock frequency: %d",
		    error);
		return (error);
	}

	tmp = BWN_RF_READ(mac, BWN_B2063_RX_BB_SP8) & 0xff;

	for (i = 0; i < 2; i++)
		BWN_RF_WRITE(mac, v1[i].reg, v1[i].value);
	BWN_RF_MASK(mac, BWN_B2063_PLL_SP1, 0xf7);
	for (i = 2; i < N(v1); i++)
		BWN_RF_WRITE(mac, v1[i].reg, v1[i].value);
	for (i = 0; i < 10000; i++) {
		if (BWN_RF_READ(mac, BWN_B2063_RC_CALIB_CTL6) & 0x2)
			break;
		DELAY(1000);
	}

	if (!(BWN_RF_READ(mac, BWN_B2063_RC_CALIB_CTL6) & 0x2))
		BWN_RF_WRITE(mac, BWN_B2063_RX_BB_SP8, tmp);

	tmp = BWN_RF_READ(mac, BWN_B2063_TX_BB_SP3) & 0xff;

	for (i = 0; i < N(v2); i++)
		BWN_RF_WRITE(mac, v2[i].reg, v2[i].value);
	if (freqxtal == 24000000) {
		BWN_RF_WRITE(mac, BWN_B2063_RC_CALIB_CTL4, 0xfc);
		BWN_RF_WRITE(mac, BWN_B2063_RC_CALIB_CTL5, 0x0);
	} else {
		BWN_RF_WRITE(mac, BWN_B2063_RC_CALIB_CTL4, 0x13);
		BWN_RF_WRITE(mac, BWN_B2063_RC_CALIB_CTL5, 0x1);
	}
	BWN_RF_WRITE(mac, BWN_B2063_PA_SP7, 0x7d);
	for (i = 0; i < 10000; i++) {
		if (BWN_RF_READ(mac, BWN_B2063_RC_CALIB_CTL6) & 0x2)
			break;
		DELAY(1000);
	}
	if (!(BWN_RF_READ(mac, BWN_B2063_RC_CALIB_CTL6) & 0x2))
		BWN_RF_WRITE(mac, BWN_B2063_TX_BB_SP3, tmp);
	BWN_RF_WRITE(mac, BWN_B2063_RC_CALIB_CTL1, 0x7e);

	return (0);
}

static int
bwn_phy_lp_rccal_r12(struct bwn_mac *mac)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy_lp_iq_est ie;
	struct bwn_txgain tx_gains;
	static const uint32_t pwrtbl[21] = {
		0x10000, 0x10557, 0x10e2d, 0x113e0, 0x10f22, 0x0ff64,
		0x0eda2, 0x0e5d4, 0x0efd1, 0x0fbe8, 0x0b7b8, 0x04b35,
		0x01a5e, 0x00a0b, 0x00444, 0x001fd, 0x000ff, 0x00088,
		0x0004c, 0x0002c, 0x0001a,
	};
	uint32_t npwr, ipwr, sqpwr, tmp;
	int loopback, i, j, sum, error;
	uint16_t save[7];
	uint8_t txo, bbmult, txpctlmode;

	error = bwn_phy_lp_switch_channel(mac, 7);
	if (error)
		device_printf(sc->sc_dev,
		    "failed to change channel to 7 (%d)\n", error);
	txo = (BWN_PHY_READ(mac, BWN_PHY_AFE_CTL_OVR) & 0x40) ? 1 : 0;
	bbmult = bwn_phy_lp_get_bbmult(mac);
	if (txo)
		tx_gains = bwn_phy_lp_get_txgain(mac);

	save[0] = BWN_PHY_READ(mac, BWN_PHY_RF_OVERRIDE_0);
	save[1] = BWN_PHY_READ(mac, BWN_PHY_RF_OVERRIDE_VAL_0);
	save[2] = BWN_PHY_READ(mac, BWN_PHY_AFE_CTL_OVR);
	save[3] = BWN_PHY_READ(mac, BWN_PHY_AFE_CTL_OVRVAL);
	save[4] = BWN_PHY_READ(mac, BWN_PHY_RF_OVERRIDE_2);
	save[5] = BWN_PHY_READ(mac, BWN_PHY_RF_OVERRIDE_2_VAL);
	save[6] = BWN_PHY_READ(mac, BWN_PHY_LP_PHY_CTL);

	bwn_phy_lp_get_txpctlmode(mac);
	txpctlmode = plp->plp_txpctlmode;
	bwn_phy_lp_set_txpctlmode(mac, BWN_PHYLP_TXPCTL_OFF);

	/* disable CRS */
	bwn_phy_lp_set_deaf(mac, 1);
	bwn_phy_lp_set_trsw_over(mac, 0, 1);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0xfffb);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x4);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0xfff7);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x8);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0x10);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x10);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0xffdf);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x20);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0xffbf);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x40);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0x7);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0x38);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0xff3f);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0x100);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0xfdff);
	BWN_PHY_WRITE(mac, BWN_PHY_PS_CTL_OVERRIDE_VAL0, 0);
	BWN_PHY_WRITE(mac, BWN_PHY_PS_CTL_OVERRIDE_VAL1, 1);
	BWN_PHY_WRITE(mac, BWN_PHY_PS_CTL_OVERRIDE_VAL2, 0x20);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0xfbff);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0xf7ff);
	BWN_PHY_WRITE(mac, BWN_PHY_TX_GAIN_CTL_OVERRIDE_VAL, 0);
	BWN_PHY_WRITE(mac, BWN_PHY_RX_GAIN_CTL_OVERRIDE_VAL, 0x45af);
	BWN_PHY_WRITE(mac, BWN_PHY_RF_OVERRIDE_2, 0x3ff);

	loopback = bwn_phy_lp_loopback(mac);
	if (loopback == -1)
		goto done;
	bwn_phy_lp_set_rxgain_idx(mac, loopback);
	BWN_PHY_SETMASK(mac, BWN_PHY_LP_PHY_CTL, 0xffbf, 0x40);
	BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0xfff8, 0x1);
	BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0xffc7, 0x8);
	BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL, 0xff3f, 0xc0);

	tmp = 0;
	memset(&ie, 0, sizeof(ie));
	for (i = 128; i <= 159; i++) {
		BWN_RF_WRITE(mac, BWN_B2062_N_RXBB_CALIB2, i);
		sum = 0;
		for (j = 5; j <= 25; j++) {
			bwn_phy_lp_ddfs_turnon(mac, 1, 1, j, j, 0);
			if (!(bwn_phy_lp_rx_iq_est(mac, 1000, 32, &ie)))
				goto done;
			sqpwr = ie.ie_ipwr + ie.ie_qpwr;
			ipwr = ((pwrtbl[j - 5] >> 3) + 1) >> 1;
			npwr = bwn_phy_lp_roundup(sqpwr, (j == 5) ? sqpwr : 0,
			    12);
			sum += ((ipwr - npwr) * (ipwr - npwr));
			if ((i == 128) || (sum < tmp)) {
				plp->plp_rccap = i;
				tmp = sum;
			}
		}
	}
	bwn_phy_lp_ddfs_turnoff(mac);
done:
	/* restore CRS */
	bwn_phy_lp_clear_deaf(mac, 1);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_0, 0xff80);
	BWN_PHY_MASK(mac, BWN_PHY_RF_OVERRIDE_2, 0xfc00);

	BWN_PHY_WRITE(mac, BWN_PHY_RF_OVERRIDE_VAL_0, save[1]);
	BWN_PHY_WRITE(mac, BWN_PHY_RF_OVERRIDE_0, save[0]);
	BWN_PHY_WRITE(mac, BWN_PHY_AFE_CTL_OVRVAL, save[3]);
	BWN_PHY_WRITE(mac, BWN_PHY_AFE_CTL_OVR, save[2]);
	BWN_PHY_WRITE(mac, BWN_PHY_RF_OVERRIDE_2_VAL, save[5]);
	BWN_PHY_WRITE(mac, BWN_PHY_RF_OVERRIDE_2, save[4]);
	BWN_PHY_WRITE(mac, BWN_PHY_LP_PHY_CTL, save[6]);

	bwn_phy_lp_set_bbmult(mac, bbmult);
	if (txo)
		bwn_phy_lp_set_txgain(mac, &tx_gains);
	bwn_phy_lp_set_txpctlmode(mac, txpctlmode);
	if (plp->plp_rccap)
		bwn_phy_lp_set_rccap(mac);

	return (0);
}

static void
bwn_phy_lp_set_rccap(struct bwn_mac *mac)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	uint8_t rc_cap = (plp->plp_rccap & 0x1f) >> 1;

	if (mac->mac_phy.rev == 1)
		rc_cap = MIN(rc_cap + 5, 15);

	BWN_RF_WRITE(mac, BWN_B2062_N_RXBB_CALIB2,
	    MAX(plp->plp_rccap - 4, 0x80));
	BWN_RF_WRITE(mac, BWN_B2062_N_TXCTL_A, rc_cap | 0x80);
	BWN_RF_WRITE(mac, BWN_B2062_S_RXG_CNT16,
	    ((plp->plp_rccap & 0x1f) >> 2) | 0x80);
}

static uint32_t
bwn_phy_lp_roundup(uint32_t value, uint32_t div, uint8_t pre)
{
	uint32_t i, q, r;

	if (div == 0)
		return (0);

	for (i = 0, q = value / div, r = value % div; i < pre; i++) {
		q <<= 1;
		if (r << 1 >= div) {
			q++;
			r = (r << 1) - div;
		}
	}
	if (r << 1 >= div)
		q++;
	return (q);
}

static void
bwn_phy_lp_b2062_reset_pllbias(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;

	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL2, 0xff);
	DELAY(20);
	if (sc->sc_cid.chip_id == BHND_CHIPID_BCM5354) {
		BWN_RF_WRITE(mac, BWN_B2062_N_COM1, 4);
		BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL2, 4);
	} else {
		BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL2, 0);
	}
	DELAY(5);
}

static void
bwn_phy_lp_b2062_vco_calib(struct bwn_mac *mac)
{

	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL21, 0x42);
	BWN_RF_WRITE(mac, BWN_B2062_S_RFPLLCTL21, 0x62);
	DELAY(200);
}

static void
bwn_phy_lp_b2062_tblinit(struct bwn_mac *mac)
{
#define	FLAG_A	0x01
#define	FLAG_G	0x02
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	static const struct bwn_b206x_rfinit_entry bwn_b2062_init_tab[] = {
		{ BWN_B2062_N_COM4, 0x1, 0x0, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_PDNCTL1, 0x0, 0xca, FLAG_G, },
		{ BWN_B2062_N_PDNCTL3, 0x0, 0x0, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_PDNCTL4, 0x15, 0x2a, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_LGENC, 0xDB, 0xff, FLAG_A, },
		{ BWN_B2062_N_LGENATUNE0, 0xdd, 0x0, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_LGENATUNE2, 0xdd, 0x0, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_LGENATUNE3, 0x77, 0xB5, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_LGENACTL3, 0x0, 0xff, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_LGENACTL7, 0x33, 0x33, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_RXA_CTL1, 0x0, 0x0, FLAG_G, },
		{ BWN_B2062_N_RXBB_CTL0, 0x82, 0x80, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_RXBB_GAIN1, 0x4, 0x4, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_RXBB_GAIN2, 0x0, 0x0, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_TXCTL4, 0x3, 0x3, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_TXCTL5, 0x2, 0x2, FLAG_A | FLAG_G, },
		{ BWN_B2062_N_TX_TUNE, 0x88, 0x1b, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_COM4, 0x1, 0x0, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_PDS_CTL0, 0xff, 0xff, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_LGENG_CTL0, 0xf8, 0xd8, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_LGENG_CTL1, 0x3c, 0x24, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_LGENG_CTL8, 0x88, 0x80, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_LGENG_CTL10, 0x88, 0x80, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL0, 0x98, 0x98, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL1, 0x10, 0x10, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL5, 0x43, 0x43, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL6, 0x47, 0x47, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL7, 0xc, 0xc, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL8, 0x11, 0x11, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL9, 0x11, 0x11, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL10, 0xe, 0xe, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL11, 0x8, 0x8, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL12, 0x33, 0x33, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL13, 0xa, 0xa, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL14, 0x6, 0x6, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL18, 0x3e, 0x3e, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL19, 0x13, 0x13, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL21, 0x62, 0x62, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL22, 0x7, 0x7, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL23, 0x16, 0x16, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL24, 0x5c, 0x5c, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL25, 0x95, 0x95, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL30, 0xa0, 0xa0, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL31, 0x4, 0x4, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL33, 0xcc, 0xcc, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RFPLLCTL34, 0x7, 0x7, FLAG_A | FLAG_G, },
		{ BWN_B2062_S_RXG_CNT8, 0xf, 0xf, FLAG_A, },
	};
	const struct bwn_b206x_rfinit_entry *br;
	unsigned int i;

	for (i = 0; i < N(bwn_b2062_init_tab); i++) {
		br = &bwn_b2062_init_tab[i];
		if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
			if (br->br_flags & FLAG_G)
				BWN_RF_WRITE(mac, br->br_offset, br->br_valueg);
		} else {
			if (br->br_flags & FLAG_A)
				BWN_RF_WRITE(mac, br->br_offset, br->br_valuea);
		}
	}
#undef FLAG_A
#undef FLAG_B
}

static void
bwn_phy_lp_b2063_tblinit(struct bwn_mac *mac)
{
#define	FLAG_A	0x01
#define	FLAG_G	0x02
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	static const struct bwn_b206x_rfinit_entry bwn_b2063_init_tab[] = {
		{ BWN_B2063_COM1, 0x0, 0x0, FLAG_G, },
		{ BWN_B2063_COM10, 0x1, 0x0, FLAG_A, },
		{ BWN_B2063_COM16, 0x0, 0x0, FLAG_G, },
		{ BWN_B2063_COM17, 0x0, 0x0, FLAG_G, },
		{ BWN_B2063_COM18, 0x0, 0x0, FLAG_G, },
		{ BWN_B2063_COM19, 0x0, 0x0, FLAG_G, },
		{ BWN_B2063_COM20, 0x0, 0x0, FLAG_G, },
		{ BWN_B2063_COM21, 0x0, 0x0, FLAG_G, },
		{ BWN_B2063_COM22, 0x0, 0x0, FLAG_G, },
		{ BWN_B2063_COM23, 0x0, 0x0, FLAG_G, },
		{ BWN_B2063_COM24, 0x0, 0x0, FLAG_G, },
		{ BWN_B2063_LOGEN_SP1, 0xe8, 0xd4, FLAG_A | FLAG_G, },
		{ BWN_B2063_LOGEN_SP2, 0xa7, 0x53, FLAG_A | FLAG_G, },
		{ BWN_B2063_LOGEN_SP4, 0xf0, 0xf, FLAG_A | FLAG_G, },
		{ BWN_B2063_G_RX_SP1, 0x1f, 0x5e, FLAG_G, },
		{ BWN_B2063_G_RX_SP2, 0x7f, 0x7e, FLAG_G, },
		{ BWN_B2063_G_RX_SP3, 0x30, 0xf0, FLAG_G, },
		{ BWN_B2063_G_RX_SP7, 0x7f, 0x7f, FLAG_A | FLAG_G, },
		{ BWN_B2063_G_RX_SP10, 0xc, 0xc, FLAG_A | FLAG_G, },
		{ BWN_B2063_A_RX_SP1, 0x3c, 0x3f, FLAG_A, },
		{ BWN_B2063_A_RX_SP2, 0xfc, 0xfe, FLAG_A, },
		{ BWN_B2063_A_RX_SP7, 0x8, 0x8, FLAG_A | FLAG_G, },
		{ BWN_B2063_RX_BB_SP4, 0x60, 0x60, FLAG_A | FLAG_G, },
		{ BWN_B2063_RX_BB_SP8, 0x30, 0x30, FLAG_A | FLAG_G, },
		{ BWN_B2063_TX_RF_SP3, 0xc, 0xb, FLAG_A | FLAG_G, },
		{ BWN_B2063_TX_RF_SP4, 0x10, 0xf, FLAG_A | FLAG_G, },
		{ BWN_B2063_PA_SP1, 0x3d, 0xfd, FLAG_A | FLAG_G, },
		{ BWN_B2063_TX_BB_SP1, 0x2, 0x2, FLAG_A | FLAG_G, },
		{ BWN_B2063_BANDGAP_CTL1, 0x56, 0x56, FLAG_A | FLAG_G, },
		{ BWN_B2063_JTAG_VCO2, 0xF7, 0xF7, FLAG_A | FLAG_G, },
		{ BWN_B2063_G_RX_MIX3, 0x71, 0x71, FLAG_A | FLAG_G, },
		{ BWN_B2063_G_RX_MIX4, 0x71, 0x71, FLAG_A | FLAG_G, },
		{ BWN_B2063_A_RX_1ST2, 0xf0, 0x30, FLAG_A, },
		{ BWN_B2063_A_RX_PS6, 0x77, 0x77, FLAG_A | FLAG_G, },
		{ BWN_B2063_A_RX_MIX4, 0x3, 0x3, FLAG_A | FLAG_G, },
		{ BWN_B2063_A_RX_MIX5, 0xf, 0xf, FLAG_A | FLAG_G, },
		{ BWN_B2063_A_RX_MIX6, 0xf, 0xf, FLAG_A | FLAG_G, },
		{ BWN_B2063_RX_TIA_CTL1, 0x77, 0x77, FLAG_A | FLAG_G, },
		{ BWN_B2063_RX_TIA_CTL3, 0x77, 0x77, FLAG_A | FLAG_G, },
		{ BWN_B2063_RX_BB_CTL2, 0x4, 0x4, FLAG_A | FLAG_G, },
		{ BWN_B2063_PA_CTL1, 0x0, 0x4, FLAG_A, },
		{ BWN_B2063_VREG_CTL1, 0x3, 0x3, FLAG_A | FLAG_G, },
	};
	const struct bwn_b206x_rfinit_entry *br;
	unsigned int i;

	for (i = 0; i < N(bwn_b2063_init_tab); i++) {
		br = &bwn_b2063_init_tab[i];
		if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
			if (br->br_flags & FLAG_G)
				BWN_RF_WRITE(mac, br->br_offset, br->br_valueg);
		} else {
			if (br->br_flags & FLAG_A)
				BWN_RF_WRITE(mac, br->br_offset, br->br_valuea);
		}
	}
#undef FLAG_A
#undef FLAG_B
}

static void
bwn_tab_read_multi(struct bwn_mac *mac, uint32_t typenoffset,
    int count, void *_data)
{
	unsigned int i;
	uint32_t offset, type;
	uint8_t *data = _data;

	type = BWN_TAB_GETTYPE(typenoffset);
	offset = BWN_TAB_GETOFFSET(typenoffset);
	KASSERT(offset <= 0xffff, ("%s:%d: fail", __func__, __LINE__));

	BWN_PHY_WRITE(mac, BWN_PHY_TABLE_ADDR, offset);

	for (i = 0; i < count; i++) {
		switch (type) {
		case BWN_TAB_8BIT:
			*data = BWN_PHY_READ(mac, BWN_PHY_TABLEDATALO) & 0xff;
			data++;
			break;
		case BWN_TAB_16BIT:
			*((uint16_t *)data) = BWN_PHY_READ(mac,
			    BWN_PHY_TABLEDATALO);
			data += 2;
			break;
		case BWN_TAB_32BIT:
			*((uint32_t *)data) = BWN_PHY_READ(mac,
			    BWN_PHY_TABLEDATAHI);
			*((uint32_t *)data) <<= 16;
			*((uint32_t *)data) |= BWN_PHY_READ(mac,
			    BWN_PHY_TABLEDATALO);
			data += 4;
			break;
		default:
			KASSERT(0 == 1, ("%s:%d: fail", __func__, __LINE__));
		}
	}
}

static void
bwn_tab_write_multi(struct bwn_mac *mac, uint32_t typenoffset,
    int count, const void *_data)
{
	uint32_t offset, type, value;
	const uint8_t *data = _data;
	unsigned int i;

	type = BWN_TAB_GETTYPE(typenoffset);
	offset = BWN_TAB_GETOFFSET(typenoffset);
	KASSERT(offset <= 0xffff, ("%s:%d: fail", __func__, __LINE__));

	BWN_PHY_WRITE(mac, BWN_PHY_TABLE_ADDR, offset);

	for (i = 0; i < count; i++) {
		switch (type) {
		case BWN_TAB_8BIT:
			value = *data;
			data++;
			KASSERT(!(value & ~0xff),
			    ("%s:%d: fail", __func__, __LINE__));
			BWN_PHY_WRITE(mac, BWN_PHY_TABLEDATALO, value);
			break;
		case BWN_TAB_16BIT:
			value = *((const uint16_t *)data);
			data += 2;
			KASSERT(!(value & ~0xffff),
			    ("%s:%d: fail", __func__, __LINE__));
			BWN_PHY_WRITE(mac, BWN_PHY_TABLEDATALO, value);
			break;
		case BWN_TAB_32BIT:
			value = *((const uint32_t *)data);
			data += 4;
			BWN_PHY_WRITE(mac, BWN_PHY_TABLEDATAHI, value >> 16);
			BWN_PHY_WRITE(mac, BWN_PHY_TABLEDATALO, value);
			break;
		default:
			KASSERT(0 == 1, ("%s:%d: fail", __func__, __LINE__));
		}
	}
}

static struct bwn_txgain
bwn_phy_lp_get_txgain(struct bwn_mac *mac)
{
	struct bwn_txgain tg;
	uint16_t tmp;

	tg.tg_dac = (BWN_PHY_READ(mac, BWN_PHY_AFE_DAC_CTL) & 0x380) >> 7;
	if (mac->mac_phy.rev < 2) {
		tmp = BWN_PHY_READ(mac,
		    BWN_PHY_TX_GAIN_CTL_OVERRIDE_VAL) & 0x7ff;
		tg.tg_gm = tmp & 0x0007;
		tg.tg_pga = (tmp & 0x0078) >> 3;
		tg.tg_pad = (tmp & 0x780) >> 7;
		return (tg);
	}

	tmp = BWN_PHY_READ(mac, BWN_PHY_TX_GAIN_CTL_OVERRIDE_VAL);
	tg.tg_pad = BWN_PHY_READ(mac, BWN_PHY_OFDM(0xfb)) & 0xff;
	tg.tg_gm = tmp & 0xff;
	tg.tg_pga = (tmp >> 8) & 0xff;
	return (tg);
}

static uint8_t
bwn_phy_lp_get_bbmult(struct bwn_mac *mac)
{

	return (bwn_tab_read(mac, BWN_TAB_2(0, 87)) & 0xff00) >> 8;
}

static void
bwn_phy_lp_set_txgain(struct bwn_mac *mac, struct bwn_txgain *tg)
{
	uint16_t pa;

	if (mac->mac_phy.rev < 2) {
		BWN_PHY_SETMASK(mac, BWN_PHY_TX_GAIN_CTL_OVERRIDE_VAL, 0xf800,
		    (tg->tg_pad << 7) | (tg->tg_pga << 3) | tg->tg_gm);
		bwn_phy_lp_set_txgain_dac(mac, tg->tg_dac);
		bwn_phy_lp_set_txgain_override(mac);
		return;
	}

	pa = bwn_phy_lp_get_pa_gain(mac);
	BWN_PHY_WRITE(mac, BWN_PHY_TX_GAIN_CTL_OVERRIDE_VAL,
	    (tg->tg_pga << 8) | tg->tg_gm);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0xfb), 0x8000,
	    tg->tg_pad | (pa << 6));
	BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0xfc), (tg->tg_pga << 8) | tg->tg_gm);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0xfd), 0x8000,
	    tg->tg_pad | (pa << 8));
	bwn_phy_lp_set_txgain_dac(mac, tg->tg_dac);
	bwn_phy_lp_set_txgain_override(mac);
}

static void
bwn_phy_lp_set_bbmult(struct bwn_mac *mac, uint8_t bbmult)
{

	bwn_tab_write(mac, BWN_TAB_2(0, 87), (uint16_t)bbmult << 8);
}

static void
bwn_phy_lp_set_trsw_over(struct bwn_mac *mac, uint8_t tx, uint8_t rx)
{
	uint16_t trsw = (tx << 1) | rx;

	BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0xfffc, trsw);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x3);
}

static void
bwn_phy_lp_set_rxgain(struct bwn_mac *mac, uint32_t gain)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t ext_lna, high_gain, lna, low_gain, trsw, tmp;

	if (mac->mac_phy.rev < 2) {
		trsw = gain & 0x1;
		lna = (gain & 0xfffc) | ((gain & 0xc) >> 2);
		ext_lna = (gain & 2) >> 1;

		BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0xfffe, trsw);
		BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL,
		    0xfbff, ext_lna << 10);
		BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL,
		    0xf7ff, ext_lna << 11);
		BWN_PHY_WRITE(mac, BWN_PHY_RX_GAIN_CTL_OVERRIDE_VAL, lna);
	} else {
		low_gain = gain & 0xffff;
		high_gain = (gain >> 16) & 0xf;
		ext_lna = (gain >> 21) & 0x1;
		trsw = ~(gain >> 20) & 0x1;

		BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0xfffe, trsw);
		BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL,
		    0xfdff, ext_lna << 9);
		BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL,
		    0xfbff, ext_lna << 10);
		BWN_PHY_WRITE(mac, BWN_PHY_RX_GAIN_CTL_OVERRIDE_VAL, low_gain);
		BWN_PHY_SETMASK(mac, BWN_PHY_AFE_DDFS, 0xfff0, high_gain);
		if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
			tmp = (gain >> 2) & 0x3;
			BWN_PHY_SETMASK(mac, BWN_PHY_RF_OVERRIDE_2_VAL,
			    0xe7ff, tmp<<11);
			BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0xe6), 0xffe7,
			    tmp << 3);
		}
	}

	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x1);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x10);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x40);
	if (mac->mac_phy.rev >= 2) {
		BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_2, 0x100);
		if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
			BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_2, 0x400);
			BWN_PHY_SET(mac, BWN_PHY_OFDM(0xe5), 0x8);
		}
		return;
	}
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_2, 0x200);
}

static void
bwn_phy_lp_set_deaf(struct bwn_mac *mac, uint8_t user)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;

	if (user)
		plp->plp_crsusr_off = 1;
	else
		plp->plp_crssys_off = 1;

	BWN_PHY_SETMASK(mac, BWN_PHY_CRSGAIN_CTL, 0xff1f, 0x80);
}

static void
bwn_phy_lp_clear_deaf(struct bwn_mac *mac, uint8_t user)
{
	struct bwn_phy_lp *plp = &mac->mac_phy.phy_lp;
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;

	if (user)
		plp->plp_crsusr_off = 0;
	else
		plp->plp_crssys_off = 0;

	if (plp->plp_crsusr_off || plp->plp_crssys_off)
		return;

	if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan))
		BWN_PHY_SETMASK(mac, BWN_PHY_CRSGAIN_CTL, 0xff1f, 0x60);
	else
		BWN_PHY_SETMASK(mac, BWN_PHY_CRSGAIN_CTL, 0xff1f, 0x20);
}

static int
bwn_phy_lp_calc_rx_iq_comp(struct bwn_mac *mac, uint16_t sample)
{
#define	CALC_COEFF(_v, _x, _y, _z)	do {				\
	int _t;								\
	_t = _x - 20;							\
	if (_t >= 0) {							\
		_v = ((_y << (30 - _x)) + (_z >> (1 + _t))) / (_z >> _t); \
	} else {							\
		_v = ((_y << (30 - _x)) + (_z << (-1 - _t))) / (_z << -_t); \
	}								\
} while (0)
#define	CALC_COEFF2(_v, _x, _y, _z)	do {				\
	int _t;								\
	_t = _x - 11;							\
	if (_t >= 0)							\
		_v = (_y << (31 - _x)) / (_z >> _t);			\
	else								\
		_v = (_y << (31 - _x)) / (_z << -_t);			\
} while (0)
	struct bwn_phy_lp_iq_est ie;
	uint16_t v0, v1;
	int tmp[2], ret;

	v1 = BWN_PHY_READ(mac, BWN_PHY_RX_COMP_COEFF_S);
	v0 = v1 >> 8;
	v1 |= 0xff;

	BWN_PHY_SETMASK(mac, BWN_PHY_RX_COMP_COEFF_S, 0xff00, 0x00c0);
	BWN_PHY_MASK(mac, BWN_PHY_RX_COMP_COEFF_S, 0x00ff);

	ret = bwn_phy_lp_rx_iq_est(mac, sample, 32, &ie);
	if (ret == 0)
		goto done;

	if (ie.ie_ipwr + ie.ie_qpwr < 2) {
		ret = 0;
		goto done;
	}

	CALC_COEFF(tmp[0], bwn_nbits(ie.ie_iqprod), ie.ie_iqprod, ie.ie_ipwr);
	CALC_COEFF2(tmp[1], bwn_nbits(ie.ie_qpwr), ie.ie_qpwr, ie.ie_ipwr);

	tmp[1] = -bwn_sqrt(mac, tmp[1] - (tmp[0] * tmp[0]));
	v0 = tmp[0] >> 3;
	v1 = tmp[1] >> 4;
done:
	BWN_PHY_SETMASK(mac, BWN_PHY_RX_COMP_COEFF_S, 0xff00, v1);
	BWN_PHY_SETMASK(mac, BWN_PHY_RX_COMP_COEFF_S, 0x00ff, v0 << 8);
	return ret;
#undef CALC_COEFF
#undef CALC_COEFF2
}

static void
bwn_phy_lp_tblinit_r01(struct bwn_mac *mac)
{
	static const uint16_t noisescale[] = {
		0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4,
		0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa400, 0xa4a4, 0xa4a4,
		0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4,
		0xa4a4, 0xa4a4, 0x00a4, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x4c00, 0x2d36, 0x0000, 0x0000, 0x4c00, 0x2d36,
	};
	static const uint16_t crsgainnft[] = {
		0x0366, 0x036a, 0x036f, 0x0364, 0x0367, 0x036d, 0x0374, 0x037f,
		0x036f, 0x037b, 0x038a, 0x0378, 0x0367, 0x036d, 0x0375, 0x0381,
		0x0374, 0x0381, 0x0392, 0x03a9, 0x03c4, 0x03e1, 0x0001, 0x001f,
		0x0040, 0x005e, 0x007f, 0x009e, 0x00bd, 0x00dd, 0x00fd, 0x011d,
		0x013d,
	};
	static const uint16_t filterctl[] = {
		0xa0fc, 0x10fc, 0x10db, 0x20b7, 0xff93, 0x10bf, 0x109b, 0x2077,
		0xff53, 0x0127,
	};
	static const uint32_t psctl[] = {
		0x00010000, 0x000000a0, 0x00040000, 0x00000048, 0x08080101,
		0x00000080, 0x08080101, 0x00000040, 0x08080101, 0x000000c0,
		0x08a81501, 0x000000c0, 0x0fe8fd01, 0x000000c0, 0x08300105,
		0x000000c0, 0x08080201, 0x000000c0, 0x08280205, 0x000000c0,
		0xe80802fe, 0x000000c7, 0x28080206, 0x000000c0, 0x08080202,
		0x000000c0, 0x0ba87602, 0x000000c0, 0x1068013d, 0x000000c0,
		0x10280105, 0x000000c0, 0x08880102, 0x000000c0, 0x08280106,
		0x000000c0, 0xe80801fd, 0x000000c7, 0xa8080115, 0x000000c0,
	};
	static const uint16_t ofdmcckgain_r0[] = {
		0x0001, 0x0001, 0x0001, 0x0001, 0x1001, 0x2001, 0x3001, 0x4001,
		0x5001, 0x6001, 0x7001, 0x7011, 0x7021, 0x2035, 0x2045, 0x2055,
		0x2065, 0x2075, 0x006d, 0x007d, 0x014d, 0x015d, 0x115d, 0x035d,
		0x135d, 0x055d, 0x155d, 0x0d5d, 0x1d5d, 0x2d5d, 0x555d, 0x655d,
		0x755d,
	};
	static const uint16_t ofdmcckgain_r1[] = {
		0x5000, 0x6000, 0x7000, 0x0001, 0x1001, 0x2001, 0x3001, 0x4001,
		0x5001, 0x6001, 0x7001, 0x7011, 0x7021, 0x2035, 0x2045, 0x2055,
		0x2065, 0x2075, 0x006d, 0x007d, 0x014d, 0x015d, 0x115d, 0x035d,
		0x135d, 0x055d, 0x155d, 0x0d5d, 0x1d5d, 0x2d5d, 0x555d, 0x655d,
		0x755d,
	};
	static const uint16_t gaindelta[] = {
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000,
	};
	static const uint32_t txpwrctl[] = {
		0x00000050, 0x0000004f, 0x0000004e, 0x0000004d, 0x0000004c,
		0x0000004b, 0x0000004a, 0x00000049, 0x00000048, 0x00000047,
		0x00000046, 0x00000045, 0x00000044, 0x00000043, 0x00000042,
		0x00000041, 0x00000040, 0x0000003f, 0x0000003e, 0x0000003d,
		0x0000003c, 0x0000003b, 0x0000003a, 0x00000039, 0x00000038,
		0x00000037, 0x00000036, 0x00000035, 0x00000034, 0x00000033,
		0x00000032, 0x00000031, 0x00000030, 0x0000002f, 0x0000002e,
		0x0000002d, 0x0000002c, 0x0000002b, 0x0000002a, 0x00000029,
		0x00000028, 0x00000027, 0x00000026, 0x00000025, 0x00000024,
		0x00000023, 0x00000022, 0x00000021, 0x00000020, 0x0000001f,
		0x0000001e, 0x0000001d, 0x0000001c, 0x0000001b, 0x0000001a,
		0x00000019, 0x00000018, 0x00000017, 0x00000016, 0x00000015,
		0x00000014, 0x00000013, 0x00000012, 0x00000011, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x000075a0, 0x000075a0, 0x000075a1,
		0x000075a1, 0x000075a2, 0x000075a2, 0x000075a3, 0x000075a3,
		0x000074b0, 0x000074b0, 0x000074b1, 0x000074b1, 0x000074b2,
		0x000074b2, 0x000074b3, 0x000074b3, 0x00006d20, 0x00006d20,
		0x00006d21, 0x00006d21, 0x00006d22, 0x00006d22, 0x00006d23,
		0x00006d23, 0x00004660, 0x00004660, 0x00004661, 0x00004661,
		0x00004662, 0x00004662, 0x00004663, 0x00004663, 0x00003e60,
		0x00003e60, 0x00003e61, 0x00003e61, 0x00003e62, 0x00003e62,
		0x00003e63, 0x00003e63, 0x00003660, 0x00003660, 0x00003661,
		0x00003661, 0x00003662, 0x00003662, 0x00003663, 0x00003663,
		0x00002e60, 0x00002e60, 0x00002e61, 0x00002e61, 0x00002e62,
		0x00002e62, 0x00002e63, 0x00002e63, 0x00002660, 0x00002660,
		0x00002661, 0x00002661, 0x00002662, 0x00002662, 0x00002663,
		0x00002663, 0x000025e0, 0x000025e0, 0x000025e1, 0x000025e1,
		0x000025e2, 0x000025e2, 0x000025e3, 0x000025e3, 0x00001de0,
		0x00001de0, 0x00001de1, 0x00001de1, 0x00001de2, 0x00001de2,
		0x00001de3, 0x00001de3, 0x00001d60, 0x00001d60, 0x00001d61,
		0x00001d61, 0x00001d62, 0x00001d62, 0x00001d63, 0x00001d63,
		0x00001560, 0x00001560, 0x00001561, 0x00001561, 0x00001562,
		0x00001562, 0x00001563, 0x00001563, 0x00000d60, 0x00000d60,
		0x00000d61, 0x00000d61, 0x00000d62, 0x00000d62, 0x00000d63,
		0x00000d63, 0x00000ce0, 0x00000ce0, 0x00000ce1, 0x00000ce1,
		0x00000ce2, 0x00000ce2, 0x00000ce3, 0x00000ce3, 0x00000e10,
		0x00000e10, 0x00000e11, 0x00000e11, 0x00000e12, 0x00000e12,
		0x00000e13, 0x00000e13, 0x00000bf0, 0x00000bf0, 0x00000bf1,
		0x00000bf1, 0x00000bf2, 0x00000bf2, 0x00000bf3, 0x00000bf3,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
		0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000,
		0x04000000, 0x04200000, 0x04000000, 0x000000ff, 0x000002fc,
		0x0000fa08, 0x00000305, 0x00000206, 0x00000304, 0x0000fb04,
		0x0000fcff, 0x000005fb, 0x0000fd01, 0x00000401, 0x00000006,
		0x0000ff03, 0x000007fc, 0x0000fc08, 0x00000203, 0x0000fffb,
		0x00000600, 0x0000fa01, 0x0000fc03, 0x0000fe06, 0x0000fe00,
		0x00000102, 0x000007fd, 0x000004fb, 0x000006ff, 0x000004fd,
		0x0000fdfa, 0x000007fb, 0x0000fdfa, 0x0000fa06, 0x00000500,
		0x0000f902, 0x000007fa, 0x0000fafa, 0x00000500, 0x000007fa,
		0x00000700, 0x00000305, 0x000004ff, 0x00000801, 0x00000503,
		0x000005f9, 0x00000404, 0x0000fb08, 0x000005fd, 0x00000501,
		0x00000405, 0x0000fb03, 0x000007fc, 0x00000403, 0x00000303,
		0x00000402, 0x0000faff, 0x0000fe05, 0x000005fd, 0x0000fe01,
		0x000007fa, 0x00000202, 0x00000504, 0x00000102, 0x000008fe,
		0x0000fa04, 0x0000fafc, 0x0000fe08, 0x000000f9, 0x000002fa,
		0x000003fe, 0x00000304, 0x000004f9, 0x00000100, 0x0000fd06,
		0x000008fc, 0x00000701, 0x00000504, 0x0000fdfe, 0x0000fdfc,
		0x000003fe, 0x00000704, 0x000002fc, 0x000004f9, 0x0000fdfd,
		0x0000fa07, 0x00000205, 0x000003fd, 0x000005fb, 0x000004f9,
		0x00000804, 0x0000fc06, 0x0000fcf9, 0x00000100, 0x0000fe05,
		0x00000408, 0x0000fb02, 0x00000304, 0x000006fe, 0x000004fa,
		0x00000305, 0x000008fc, 0x00000102, 0x000001fd, 0x000004fc,
		0x0000fe03, 0x00000701, 0x000001fb, 0x000001f9, 0x00000206,
		0x000006fd, 0x00000508, 0x00000700, 0x00000304, 0x000005fe,
		0x000005ff, 0x0000fa04, 0x00000303, 0x0000fefb, 0x000007f9,
		0x0000fefc, 0x000004fd, 0x000005fc, 0x0000fffd, 0x0000fc08,
		0x0000fbf9, 0x0000fd07, 0x000008fb, 0x0000fe02, 0x000006fb,
		0x00000702,
	};

	KASSERT(mac->mac_phy.rev < 2, ("%s:%d: fail", __func__, __LINE__));

	bwn_tab_write_multi(mac, BWN_TAB_1(2, 0), N(bwn_tab_sigsq_tbl),
	    bwn_tab_sigsq_tbl);
	bwn_tab_write_multi(mac, BWN_TAB_2(1, 0), N(noisescale), noisescale);
	bwn_tab_write_multi(mac, BWN_TAB_2(14, 0), N(crsgainnft), crsgainnft);
	bwn_tab_write_multi(mac, BWN_TAB_2(8, 0), N(filterctl), filterctl);
	bwn_tab_write_multi(mac, BWN_TAB_4(9, 0), N(psctl), psctl);
	bwn_tab_write_multi(mac, BWN_TAB_1(6, 0), N(bwn_tab_pllfrac_tbl),
	    bwn_tab_pllfrac_tbl);
	bwn_tab_write_multi(mac, BWN_TAB_2(0, 0), N(bwn_tabl_iqlocal_tbl),
	    bwn_tabl_iqlocal_tbl);
	if (mac->mac_phy.rev == 0) {
		bwn_tab_write_multi(mac, BWN_TAB_2(13, 0), N(ofdmcckgain_r0),
		    ofdmcckgain_r0);
		bwn_tab_write_multi(mac, BWN_TAB_2(12, 0), N(ofdmcckgain_r0),
		    ofdmcckgain_r0);
	} else {
		bwn_tab_write_multi(mac, BWN_TAB_2(13, 0), N(ofdmcckgain_r1),
		    ofdmcckgain_r1);
		bwn_tab_write_multi(mac, BWN_TAB_2(12, 0), N(ofdmcckgain_r1),
		    ofdmcckgain_r1);
	}
	bwn_tab_write_multi(mac, BWN_TAB_2(15, 0), N(gaindelta), gaindelta);
	bwn_tab_write_multi(mac, BWN_TAB_4(10, 0), N(txpwrctl), txpwrctl);
}

static void
bwn_phy_lp_tblinit_r2(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	int i;
	static const uint16_t noisescale[] = {
		0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4,
		0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4,
		0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4,
		0x00a4, 0x00a4, 0x0000, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4,
		0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4,
		0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4,
		0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4
	};
	static const uint32_t filterctl[] = {
		0x000141fc, 0x000021fc, 0x000021b7, 0x0000416f, 0x0001ff27,
		0x0000217f, 0x00002137, 0x000040ef, 0x0001fea7, 0x0000024f
	};
	static const uint32_t psctl[] = {
		0x00e38e08, 0x00e08e38, 0x00000000, 0x00000000, 0x00000000,
		0x00002080, 0x00006180, 0x00003002, 0x00000040, 0x00002042,
		0x00180047, 0x00080043, 0x00000041, 0x000020c1, 0x00046006,
		0x00042002, 0x00040000, 0x00002003, 0x00180006, 0x00080002
	};
	static const uint32_t gainidx[] = {
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x10000001, 0x00000000,
		0x20000082, 0x00000000, 0x40000104, 0x00000000, 0x60004207,
		0x00000001, 0x7000838a, 0x00000001, 0xd021050d, 0x00000001,
		0xe041c683, 0x00000001, 0x50828805, 0x00000000, 0x80e34288,
		0x00000000, 0xb144040b, 0x00000000, 0xe1a6058e, 0x00000000,
		0x12064711, 0x00000001, 0xb0a18612, 0x00000010, 0xe1024794,
		0x00000010, 0x11630915, 0x00000011, 0x31c3ca1b, 0x00000011,
		0xc1848a9c, 0x00000018, 0xf1e50da0, 0x00000018, 0x22468e21,
		0x00000019, 0x4286d023, 0x00000019, 0xa347d0a4, 0x00000019,
		0xb36811a6, 0x00000019, 0xf3e89227, 0x00000019, 0x0408d329,
		0x0000001a, 0x244953aa, 0x0000001a, 0x346994ab, 0x0000001a,
		0x54aa152c, 0x0000001a, 0x64ca55ad, 0x0000001a, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x10000001, 0x00000000, 0x20000082,
		0x00000000, 0x40000104, 0x00000000, 0x60004207, 0x00000001,
		0x7000838a, 0x00000001, 0xd021050d, 0x00000001, 0xe041c683,
		0x00000001, 0x50828805, 0x00000000, 0x80e34288, 0x00000000,
		0xb144040b, 0x00000000, 0xe1a6058e, 0x00000000, 0x12064711,
		0x00000001, 0xb0a18612, 0x00000010, 0xe1024794, 0x00000010,
		0x11630915, 0x00000011, 0x31c3ca1b, 0x00000011, 0xc1848a9c,
		0x00000018, 0xf1e50da0, 0x00000018, 0x22468e21, 0x00000019,
		0x4286d023, 0x00000019, 0xa347d0a4, 0x00000019, 0xb36811a6,
		0x00000019, 0xf3e89227, 0x00000019, 0x0408d329, 0x0000001a,
		0x244953aa, 0x0000001a, 0x346994ab, 0x0000001a, 0x54aa152c,
		0x0000001a, 0x64ca55ad, 0x0000001a
	};
	static const uint16_t auxgainidx[] = {
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0001, 0x0002, 0x0004, 0x0016, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002,
		0x0004, 0x0016
	};
	static const uint16_t swctl[] = {
		0x0128, 0x0128, 0x0009, 0x0009, 0x0028, 0x0028, 0x0028, 0x0028,
		0x0128, 0x0128, 0x0009, 0x0009, 0x0028, 0x0028, 0x0028, 0x0028,
		0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
		0x0018, 0x0018, 0x0018, 0x0018, 0x0018, 0x0018, 0x0018, 0x0018,
		0x0128, 0x0128, 0x0009, 0x0009, 0x0028, 0x0028, 0x0028, 0x0028,
		0x0128, 0x0128, 0x0009, 0x0009, 0x0028, 0x0028, 0x0028, 0x0028,
		0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
		0x0018, 0x0018, 0x0018, 0x0018, 0x0018, 0x0018, 0x0018, 0x0018
	};
	static const uint8_t hf[] = {
		0x4b, 0x36, 0x24, 0x18, 0x49, 0x34, 0x23, 0x17, 0x48,
		0x33, 0x23, 0x17, 0x48, 0x33, 0x23, 0x17
	};
	static const uint32_t gainval[] = {
		0x00000008, 0x0000000e, 0x00000014, 0x0000001a, 0x000000fb,
		0x00000004, 0x00000008, 0x0000000d, 0x00000001, 0x00000004,
		0x00000007, 0x0000000a, 0x0000000d, 0x00000010, 0x00000012,
		0x00000015, 0x00000000, 0x00000006, 0x0000000c, 0x00000000,
		0x00000000, 0x00000000, 0x00000012, 0x00000000, 0x00000000,
		0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x0000001e, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000003,
		0x00000006, 0x00000009, 0x0000000c, 0x0000000f, 0x00000012,
		0x00000015, 0x00000018, 0x0000001b, 0x0000001e, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,
		0x000000f1, 0x00000000, 0x00000000
	};
	static const uint16_t gain[] = {
		0x0000, 0x0400, 0x0800, 0x0802, 0x0804, 0x0806, 0x0807, 0x0808,
		0x080a, 0x080b, 0x080c, 0x080e, 0x080f, 0x0810, 0x0812, 0x0813,
		0x0814, 0x0816, 0x0817, 0x081a, 0x081b, 0x081f, 0x0820, 0x0824,
		0x0830, 0x0834, 0x0837, 0x083b, 0x083f, 0x0840, 0x0844, 0x0857,
		0x085b, 0x085f, 0x08d7, 0x08db, 0x08df, 0x0957, 0x095b, 0x095f,
		0x0b57, 0x0b5b, 0x0b5f, 0x0f5f, 0x135f, 0x175f, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
	};
	static const uint32_t papdeps[] = {
		0x00000000, 0x00013ffc, 0x0001dff3, 0x0001bff0, 0x00023fe9,
		0x00021fdf, 0x00028fdf, 0x00033fd2, 0x00039fcb, 0x00043fc7,
		0x0004efc2, 0x00055fb5, 0x0005cfb0, 0x00063fa8, 0x00068fa3,
		0x00071f98, 0x0007ef92, 0x00084f8b, 0x0008df82, 0x00097f77,
		0x0009df69, 0x000a3f62, 0x000adf57, 0x000b6f4c, 0x000bff41,
		0x000c9f39, 0x000cff30, 0x000dbf27, 0x000e4f1e, 0x000edf16,
		0x000f7f13, 0x00102f11, 0x00110f10, 0x0011df11, 0x0012ef15,
		0x00143f1c, 0x00158f27, 0x00172f35, 0x00193f47, 0x001baf5f,
		0x001e6f7e, 0x0021cfa4, 0x0025bfd2, 0x002a2008, 0x002fb047,
		0x00360090, 0x003d40e0, 0x0045c135, 0x004fb189, 0x005ae1d7,
		0x0067221d, 0x0075025a, 0x007ff291, 0x007ff2bf, 0x007ff2e3,
		0x007ff2ff, 0x007ff315, 0x007ff329, 0x007ff33f, 0x007ff356,
		0x007ff36e, 0x007ff39c, 0x007ff441, 0x007ff506
	};
	static const uint32_t papdmult[] = {
		0x001111e0, 0x00652051, 0x00606055, 0x005b005a, 0x00555060,
		0x00511065, 0x004c806b, 0x0047d072, 0x00444078, 0x00400080,
		0x003ca087, 0x0039408f, 0x0035e098, 0x0032e0a1, 0x003030aa,
		0x002d80b4, 0x002ae0bf, 0x002880ca, 0x002640d6, 0x002410e3,
		0x002220f0, 0x002020ff, 0x001e510e, 0x001ca11e, 0x001b012f,
		0x00199140, 0x00182153, 0x0016c168, 0x0015817d, 0x00145193,
		0x001321ab, 0x001211c5, 0x001111e0, 0x001021fc, 0x000f321a,
		0x000e523a, 0x000d925c, 0x000cd27f, 0x000c12a5, 0x000b62cd,
		0x000ac2f8, 0x000a2325, 0x00099355, 0x00091387, 0x000883bd,
		0x000813f5, 0x0007a432, 0x00073471, 0x0006c4b5, 0x000664fc,
		0x00061547, 0x0005b598, 0x000565ec, 0x00051646, 0x0004d6a5,
		0x0004870a, 0x00044775, 0x000407e6, 0x0003d85e, 0x000398dd,
		0x00036963, 0x000339f2, 0x00030a89, 0x0002db28
	};
	static const uint32_t gainidx_a0[] = {
		0x001111e0, 0x00652051, 0x00606055, 0x005b005a, 0x00555060,
		0x00511065, 0x004c806b, 0x0047d072, 0x00444078, 0x00400080,
		0x003ca087, 0x0039408f, 0x0035e098, 0x0032e0a1, 0x003030aa,
		0x002d80b4, 0x002ae0bf, 0x002880ca, 0x002640d6, 0x002410e3,
		0x002220f0, 0x002020ff, 0x001e510e, 0x001ca11e, 0x001b012f,
		0x00199140, 0x00182153, 0x0016c168, 0x0015817d, 0x00145193,
		0x001321ab, 0x001211c5, 0x001111e0, 0x001021fc, 0x000f321a,
		0x000e523a, 0x000d925c, 0x000cd27f, 0x000c12a5, 0x000b62cd,
		0x000ac2f8, 0x000a2325, 0x00099355, 0x00091387, 0x000883bd,
		0x000813f5, 0x0007a432, 0x00073471, 0x0006c4b5, 0x000664fc,
		0x00061547, 0x0005b598, 0x000565ec, 0x00051646, 0x0004d6a5,
		0x0004870a, 0x00044775, 0x000407e6, 0x0003d85e, 0x000398dd,
		0x00036963, 0x000339f2, 0x00030a89, 0x0002db28
	};
	static const uint16_t auxgainidx_a0[] = {
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0002, 0x0014, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0002, 0x0014
	};
	static const uint32_t gainval_a0[] = {
		0x00000008, 0x0000000e, 0x00000014, 0x0000001a, 0x000000fb,
		0x00000004, 0x00000008, 0x0000000d, 0x00000001, 0x00000004,
		0x00000007, 0x0000000a, 0x0000000d, 0x00000010, 0x00000012,
		0x00000015, 0x00000000, 0x00000006, 0x0000000c, 0x00000000,
		0x00000000, 0x00000000, 0x00000012, 0x00000000, 0x00000000,
		0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x0000001e, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000003,
		0x00000006, 0x00000009, 0x0000000c, 0x0000000f, 0x00000012,
		0x00000015, 0x00000018, 0x0000001b, 0x0000001e, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000f,
		0x000000f7, 0x00000000, 0x00000000
	};
	static const uint16_t gain_a0[] = {
		0x0000, 0x0002, 0x0004, 0x0006, 0x0007, 0x0008, 0x000a, 0x000b,
		0x000c, 0x000e, 0x000f, 0x0010, 0x0012, 0x0013, 0x0014, 0x0016,
		0x0017, 0x001a, 0x001b, 0x001f, 0x0020, 0x0024, 0x0030, 0x0034,
		0x0037, 0x003b, 0x003f, 0x0040, 0x0044, 0x0057, 0x005b, 0x005f,
		0x00d7, 0x00db, 0x00df, 0x0157, 0x015b, 0x015f, 0x0357, 0x035b,
		0x035f, 0x075f, 0x0b5f, 0x0f5f, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
	};

	KASSERT(mac->mac_phy.rev < 2, ("%s:%d: fail", __func__, __LINE__));

	for (i = 0; i < 704; i++)
		bwn_tab_write(mac, BWN_TAB_4(7, i), 0);

	bwn_tab_write_multi(mac, BWN_TAB_1(2, 0), N(bwn_tab_sigsq_tbl),
	    bwn_tab_sigsq_tbl);
	bwn_tab_write_multi(mac, BWN_TAB_2(1, 0), N(noisescale), noisescale);
	bwn_tab_write_multi(mac, BWN_TAB_4(11, 0), N(filterctl), filterctl);
	bwn_tab_write_multi(mac, BWN_TAB_4(12, 0), N(psctl), psctl);
	bwn_tab_write_multi(mac, BWN_TAB_4(13, 0), N(gainidx), gainidx);
	bwn_tab_write_multi(mac, BWN_TAB_2(14, 0), N(auxgainidx), auxgainidx);
	bwn_tab_write_multi(mac, BWN_TAB_2(15, 0), N(swctl), swctl);
	bwn_tab_write_multi(mac, BWN_TAB_1(16, 0), N(hf), hf);
	bwn_tab_write_multi(mac, BWN_TAB_4(17, 0), N(gainval), gainval);
	bwn_tab_write_multi(mac, BWN_TAB_2(18, 0), N(gain), gain);
	bwn_tab_write_multi(mac, BWN_TAB_1(6, 0), N(bwn_tab_pllfrac_tbl),
	    bwn_tab_pllfrac_tbl);
	bwn_tab_write_multi(mac, BWN_TAB_2(0, 0), N(bwn_tabl_iqlocal_tbl),
	    bwn_tabl_iqlocal_tbl);
	bwn_tab_write_multi(mac, BWN_TAB_4(9, 0), N(papdeps), papdeps);
	bwn_tab_write_multi(mac, BWN_TAB_4(10, 0), N(papdmult), papdmult);

	if (sc->sc_cid.chip_id == BHND_CHIPID_BCM4325 &&
	    sc->sc_cid.chip_pkg == 0) {
		bwn_tab_write_multi(mac, BWN_TAB_4(13, 0), N(gainidx_a0),
		    gainidx_a0);
		bwn_tab_write_multi(mac, BWN_TAB_2(14, 0), N(auxgainidx_a0),
		    auxgainidx_a0);
		bwn_tab_write_multi(mac, BWN_TAB_4(17, 0), N(gainval_a0),
		    gainval_a0);
		bwn_tab_write_multi(mac, BWN_TAB_2(18, 0), N(gain_a0), gain_a0);
	}
}

static void
bwn_phy_lp_tblinit_txgain(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	static struct bwn_txgain_entry txgain_r2[] = {
		{ 255, 255, 203, 0, 152 }, { 255, 255, 203, 0, 147 },
		{ 255, 255, 203, 0, 143 }, { 255, 255, 203, 0, 139 },
		{ 255, 255, 203, 0, 135 }, { 255, 255, 203, 0, 131 },
		{ 255, 255, 203, 0, 128 }, { 255, 255, 203, 0, 124 },
		{ 255, 255, 203, 0, 121 }, { 255, 255, 203, 0, 117 },
		{ 255, 255, 203, 0, 114 }, { 255, 255, 203, 0, 111 },
		{ 255, 255, 203, 0, 107 }, { 255, 255, 203, 0, 104 },
		{ 255, 255, 203, 0, 101 }, { 255, 255, 203, 0, 99 },
		{ 255, 255, 203, 0, 96 }, { 255, 255, 203, 0, 93 },
		{ 255, 255, 203, 0, 90 }, { 255, 255, 203, 0, 88 },
		{ 255, 255, 203, 0, 85 }, { 255, 255, 203, 0, 83 },
		{ 255, 255, 203, 0, 81 }, { 255, 255, 203, 0, 78 },
		{ 255, 255, 203, 0, 76 }, { 255, 255, 203, 0, 74 },
		{ 255, 255, 203, 0, 72 }, { 255, 255, 203, 0, 70 },
		{ 255, 255, 203, 0, 68 }, { 255, 255, 203, 0, 66 },
		{ 255, 255, 203, 0, 64 }, { 255, 255, 197, 0, 64 },
		{ 255, 255, 192, 0, 64 }, { 255, 255, 186, 0, 64 },
		{ 255, 255, 181, 0, 64 }, { 255, 255, 176, 0, 64 },
		{ 255, 255, 171, 0, 64 }, { 255, 255, 166, 0, 64 },
		{ 255, 255, 161, 0, 64 }, { 255, 255, 157, 0, 64 },
		{ 255, 255, 152, 0, 64 }, { 255, 255, 148, 0, 64 },
		{ 255, 255, 144, 0, 64 }, { 255, 255, 140, 0, 64 },
		{ 255, 255, 136, 0, 64 }, { 255, 255, 132, 0, 64 },
		{ 255, 255, 128, 0, 64 }, { 255, 255, 124, 0, 64 },
		{ 255, 255, 121, 0, 64 }, { 255, 255, 117, 0, 64 },
		{ 255, 255, 114, 0, 64 }, { 255, 255, 111, 0, 64 },
		{ 255, 255, 108, 0, 64 }, { 255, 255, 105, 0, 64 },
		{ 255, 255, 102, 0, 64 }, { 255, 255, 99, 0, 64 },
		{ 255, 255, 96, 0, 64 }, { 255, 255, 93, 0, 64 },
		{ 255, 255, 91, 0, 64 }, { 255, 255, 88, 0, 64 },
		{ 255, 255, 86, 0, 64 }, { 255, 255, 83, 0, 64 },
		{ 255, 255, 81, 0, 64 }, { 255, 255, 79, 0, 64 },
		{ 255, 255, 76, 0, 64 }, { 255, 255, 74, 0, 64 },
		{ 255, 255, 72, 0, 64 }, { 255, 255, 70, 0, 64 },
		{ 255, 255, 68, 0, 64 }, { 255, 255, 66, 0, 64 },
		{ 255, 255, 64, 0, 64 }, { 255, 248, 64, 0, 64 },
		{ 255, 248, 62, 0, 64 }, { 255, 241, 62, 0, 64 },
		{ 255, 241, 60, 0, 64 }, { 255, 234, 60, 0, 64 },
		{ 255, 234, 59, 0, 64 }, { 255, 227, 59, 0, 64 },
		{ 255, 227, 57, 0, 64 }, { 255, 221, 57, 0, 64 },
		{ 255, 221, 55, 0, 64 }, { 255, 215, 55, 0, 64 },
		{ 255, 215, 54, 0, 64 }, { 255, 208, 54, 0, 64 },
		{ 255, 208, 52, 0, 64 }, { 255, 203, 52, 0, 64 },
		{ 255, 203, 51, 0, 64 }, { 255, 197, 51, 0, 64 },
		{ 255, 197, 49, 0, 64 }, { 255, 191, 49, 0, 64 },
		{ 255, 191, 48, 0, 64 }, { 255, 186, 48, 0, 64 },
		{ 255, 186, 47, 0, 64 }, { 255, 181, 47, 0, 64 },
		{ 255, 181, 45, 0, 64 }, { 255, 175, 45, 0, 64 },
		{ 255, 175, 44, 0, 64 }, { 255, 170, 44, 0, 64 },
		{ 255, 170, 43, 0, 64 }, { 255, 166, 43, 0, 64 },
		{ 255, 166, 42, 0, 64 }, { 255, 161, 42, 0, 64 },
		{ 255, 161, 40, 0, 64 }, { 255, 156, 40, 0, 64 },
		{ 255, 156, 39, 0, 64 }, { 255, 152, 39, 0, 64 },
		{ 255, 152, 38, 0, 64 }, { 255, 148, 38, 0, 64 },
		{ 255, 148, 37, 0, 64 }, { 255, 143, 37, 0, 64 },
		{ 255, 143, 36, 0, 64 }, { 255, 139, 36, 0, 64 },
		{ 255, 139, 35, 0, 64 }, { 255, 135, 35, 0, 64 },
		{ 255, 135, 34, 0, 64 }, { 255, 132, 34, 0, 64 },
		{ 255, 132, 33, 0, 64 }, { 255, 128, 33, 0, 64 },
		{ 255, 128, 32, 0, 64 }, { 255, 124, 32, 0, 64 },
		{ 255, 124, 31, 0, 64 }, { 255, 121, 31, 0, 64 },
		{ 255, 121, 30, 0, 64 }, { 255, 117, 30, 0, 64 },
		{ 255, 117, 29, 0, 64 }, { 255, 114, 29, 0, 64 },
		{ 255, 114, 29, 0, 64 }, { 255, 111, 29, 0, 64 },
	};
	static struct bwn_txgain_entry txgain_2ghz_r2[] = {
		{ 7, 99, 255, 0, 64 }, { 7, 96, 255, 0, 64 },
		{ 7, 93, 255, 0, 64 }, { 7, 90, 255, 0, 64 },
		{ 7, 88, 255, 0, 64 }, { 7, 85, 255, 0, 64 },
		{ 7, 83, 255, 0, 64 }, { 7, 81, 255, 0, 64 },
		{ 7, 78, 255, 0, 64 }, { 7, 76, 255, 0, 64 },
		{ 7, 74, 255, 0, 64 }, { 7, 72, 255, 0, 64 },
		{ 7, 70, 255, 0, 64 }, { 7, 68, 255, 0, 64 },
		{ 7, 66, 255, 0, 64 }, { 7, 64, 255, 0, 64 },
		{ 7, 64, 255, 0, 64 }, { 7, 62, 255, 0, 64 },
		{ 7, 62, 248, 0, 64 }, { 7, 60, 248, 0, 64 },
		{ 7, 60, 241, 0, 64 }, { 7, 59, 241, 0, 64 },
		{ 7, 59, 234, 0, 64 }, { 7, 57, 234, 0, 64 },
		{ 7, 57, 227, 0, 64 }, { 7, 55, 227, 0, 64 },
		{ 7, 55, 221, 0, 64 }, { 7, 54, 221, 0, 64 },
		{ 7, 54, 215, 0, 64 }, { 7, 52, 215, 0, 64 },
		{ 7, 52, 208, 0, 64 }, { 7, 51, 208, 0, 64 },
		{ 7, 51, 203, 0, 64 }, { 7, 49, 203, 0, 64 },
		{ 7, 49, 197, 0, 64 }, { 7, 48, 197, 0, 64 },
		{ 7, 48, 191, 0, 64 }, { 7, 47, 191, 0, 64 },
		{ 7, 47, 186, 0, 64 }, { 7, 45, 186, 0, 64 },
		{ 7, 45, 181, 0, 64 }, { 7, 44, 181, 0, 64 },
		{ 7, 44, 175, 0, 64 }, { 7, 43, 175, 0, 64 },
		{ 7, 43, 170, 0, 64 }, { 7, 42, 170, 0, 64 },
		{ 7, 42, 166, 0, 64 }, { 7, 40, 166, 0, 64 },
		{ 7, 40, 161, 0, 64 }, { 7, 39, 161, 0, 64 },
		{ 7, 39, 156, 0, 64 }, { 7, 38, 156, 0, 64 },
		{ 7, 38, 152, 0, 64 }, { 7, 37, 152, 0, 64 },
		{ 7, 37, 148, 0, 64 }, { 7, 36, 148, 0, 64 },
		{ 7, 36, 143, 0, 64 }, { 7, 35, 143, 0, 64 },
		{ 7, 35, 139, 0, 64 }, { 7, 34, 139, 0, 64 },
		{ 7, 34, 135, 0, 64 }, { 7, 33, 135, 0, 64 },
		{ 7, 33, 132, 0, 64 }, { 7, 32, 132, 0, 64 },
		{ 7, 32, 128, 0, 64 }, { 7, 31, 128, 0, 64 },
		{ 7, 31, 124, 0, 64 }, { 7, 30, 124, 0, 64 },
		{ 7, 30, 121, 0, 64 }, { 7, 29, 121, 0, 64 },
		{ 7, 29, 117, 0, 64 }, { 7, 29, 117, 0, 64 },
		{ 7, 29, 114, 0, 64 }, { 7, 28, 114, 0, 64 },
		{ 7, 28, 111, 0, 64 }, { 7, 27, 111, 0, 64 },
		{ 7, 27, 108, 0, 64 }, { 7, 26, 108, 0, 64 },
		{ 7, 26, 104, 0, 64 }, { 7, 25, 104, 0, 64 },
		{ 7, 25, 102, 0, 64 }, { 7, 25, 102, 0, 64 },
		{ 7, 25, 99, 0, 64 }, { 7, 24, 99, 0, 64 },
		{ 7, 24, 96, 0, 64 }, { 7, 23, 96, 0, 64 },
		{ 7, 23, 93, 0, 64 }, { 7, 23, 93, 0, 64 },
		{ 7, 23, 90, 0, 64 }, { 7, 22, 90, 0, 64 },
		{ 7, 22, 88, 0, 64 }, { 7, 21, 88, 0, 64 },
		{ 7, 21, 85, 0, 64 }, { 7, 21, 85, 0, 64 },
		{ 7, 21, 83, 0, 64 }, { 7, 20, 83, 0, 64 },
		{ 7, 20, 81, 0, 64 }, { 7, 20, 81, 0, 64 },
		{ 7, 20, 78, 0, 64 }, { 7, 19, 78, 0, 64 },
		{ 7, 19, 76, 0, 64 }, { 7, 19, 76, 0, 64 },
		{ 7, 19, 74, 0, 64 }, { 7, 18, 74, 0, 64 },
		{ 7, 18, 72, 0, 64 }, { 7, 18, 72, 0, 64 },
		{ 7, 18, 70, 0, 64 }, { 7, 17, 70, 0, 64 },
		{ 7, 17, 68, 0, 64 }, { 7, 17, 68, 0, 64 },
		{ 7, 17, 66, 0, 64 }, { 7, 16, 66, 0, 64 },
		{ 7, 16, 64, 0, 64 }, { 7, 16, 64, 0, 64 },
		{ 7, 16, 62, 0, 64 }, { 7, 15, 62, 0, 64 },
		{ 7, 15, 60, 0, 64 }, { 7, 15, 60, 0, 64 },
		{ 7, 15, 59, 0, 64 }, { 7, 14, 59, 0, 64 },
		{ 7, 14, 57, 0, 64 }, { 7, 14, 57, 0, 64 },
		{ 7, 14, 55, 0, 64 }, { 7, 14, 55, 0, 64 },
		{ 7, 14, 54, 0, 64 }, { 7, 13, 54, 0, 64 },
		{ 7, 13, 52, 0, 64 }, { 7, 13, 52, 0, 64 },
	};
	static struct bwn_txgain_entry txgain_5ghz_r2[] = {
		{ 255, 255, 255, 0, 152 }, { 255, 255, 255, 0, 147 },
		{ 255, 255, 255, 0, 143 }, { 255, 255, 255, 0, 139 },
		{ 255, 255, 255, 0, 135 }, { 255, 255, 255, 0, 131 },
		{ 255, 255, 255, 0, 128 }, { 255, 255, 255, 0, 124 },
		{ 255, 255, 255, 0, 121 }, { 255, 255, 255, 0, 117 },
		{ 255, 255, 255, 0, 114 }, { 255, 255, 255, 0, 111 },
		{ 255, 255, 255, 0, 107 }, { 255, 255, 255, 0, 104 },
		{ 255, 255, 255, 0, 101 }, { 255, 255, 255, 0, 99 },
		{ 255, 255, 255, 0, 96 }, { 255, 255, 255, 0, 93 },
		{ 255, 255, 255, 0, 90 }, { 255, 255, 255, 0, 88 },
		{ 255, 255, 255, 0, 85 }, { 255, 255, 255, 0, 83 },
		{ 255, 255, 255, 0, 81 }, { 255, 255, 255, 0, 78 },
		{ 255, 255, 255, 0, 76 }, { 255, 255, 255, 0, 74 },
		{ 255, 255, 255, 0, 72 }, { 255, 255, 255, 0, 70 },
		{ 255, 255, 255, 0, 68 }, { 255, 255, 255, 0, 66 },
		{ 255, 255, 255, 0, 64 }, { 255, 255, 248, 0, 64 },
		{ 255, 255, 241, 0, 64 }, { 255, 255, 234, 0, 64 },
		{ 255, 255, 227, 0, 64 }, { 255, 255, 221, 0, 64 },
		{ 255, 255, 215, 0, 64 }, { 255, 255, 208, 0, 64 },
		{ 255, 255, 203, 0, 64 }, { 255, 255, 197, 0, 64 },
		{ 255, 255, 191, 0, 64 }, { 255, 255, 186, 0, 64 },
		{ 255, 255, 181, 0, 64 }, { 255, 255, 175, 0, 64 },
		{ 255, 255, 170, 0, 64 }, { 255, 255, 166, 0, 64 },
		{ 255, 255, 161, 0, 64 }, { 255, 255, 156, 0, 64 },
		{ 255, 255, 152, 0, 64 }, { 255, 255, 148, 0, 64 },
		{ 255, 255, 143, 0, 64 }, { 255, 255, 139, 0, 64 },
		{ 255, 255, 135, 0, 64 }, { 255, 255, 132, 0, 64 },
		{ 255, 255, 128, 0, 64 }, { 255, 255, 124, 0, 64 },
		{ 255, 255, 121, 0, 64 }, { 255, 255, 117, 0, 64 },
		{ 255, 255, 114, 0, 64 }, { 255, 255, 111, 0, 64 },
		{ 255, 255, 108, 0, 64 }, { 255, 255, 104, 0, 64 },
		{ 255, 255, 102, 0, 64 }, { 255, 255, 99, 0, 64 },
		{ 255, 255, 96, 0, 64 }, { 255, 255, 93, 0, 64 },
		{ 255, 255, 90, 0, 64 }, { 255, 255, 88, 0, 64 },
		{ 255, 255, 85, 0, 64 }, { 255, 255, 83, 0, 64 },
		{ 255, 255, 81, 0, 64 }, { 255, 255, 78, 0, 64 },
		{ 255, 255, 76, 0, 64 }, { 255, 255, 74, 0, 64 },
		{ 255, 255, 72, 0, 64 }, { 255, 255, 70, 0, 64 },
		{ 255, 255, 68, 0, 64 }, { 255, 255, 66, 0, 64 },
		{ 255, 255, 64, 0, 64 }, { 255, 255, 64, 0, 64 },
		{ 255, 255, 62, 0, 64 }, { 255, 248, 62, 0, 64 },
		{ 255, 248, 60, 0, 64 }, { 255, 241, 60, 0, 64 },
		{ 255, 241, 59, 0, 64 }, { 255, 234, 59, 0, 64 },
		{ 255, 234, 57, 0, 64 }, { 255, 227, 57, 0, 64 },
		{ 255, 227, 55, 0, 64 }, { 255, 221, 55, 0, 64 },
		{ 255, 221, 54, 0, 64 }, { 255, 215, 54, 0, 64 },
		{ 255, 215, 52, 0, 64 }, { 255, 208, 52, 0, 64 },
		{ 255, 208, 51, 0, 64 }, { 255, 203, 51, 0, 64 },
		{ 255, 203, 49, 0, 64 }, { 255, 197, 49, 0, 64 },
		{ 255, 197, 48, 0, 64 }, { 255, 191, 48, 0, 64 },
		{ 255, 191, 47, 0, 64 }, { 255, 186, 47, 0, 64 },
		{ 255, 186, 45, 0, 64 }, { 255, 181, 45, 0, 64 },
		{ 255, 181, 44, 0, 64 }, { 255, 175, 44, 0, 64 },
		{ 255, 175, 43, 0, 64 }, { 255, 170, 43, 0, 64 },
		{ 255, 170, 42, 0, 64 }, { 255, 166, 42, 0, 64 },
		{ 255, 166, 40, 0, 64 }, { 255, 161, 40, 0, 64 },
		{ 255, 161, 39, 0, 64 }, { 255, 156, 39, 0, 64 },
		{ 255, 156, 38, 0, 64 }, { 255, 152, 38, 0, 64 },
		{ 255, 152, 37, 0, 64 }, { 255, 148, 37, 0, 64 },
		{ 255, 148, 36, 0, 64 }, { 255, 143, 36, 0, 64 },
		{ 255, 143, 35, 0, 64 }, { 255, 139, 35, 0, 64 },
		{ 255, 139, 34, 0, 64 }, { 255, 135, 34, 0, 64 },
		{ 255, 135, 33, 0, 64 }, { 255, 132, 33, 0, 64 },
		{ 255, 132, 32, 0, 64 }, { 255, 128, 32, 0, 64 }
	};
	static struct bwn_txgain_entry txgain_r0[] = {
		{ 7, 15, 14, 0, 152 }, { 7, 15, 14, 0, 147 },
		{ 7, 15, 14, 0, 143 }, { 7, 15, 14, 0, 139 },
		{ 7, 15, 14, 0, 135 }, { 7, 15, 14, 0, 131 },
		{ 7, 15, 14, 0, 128 }, { 7, 15, 14, 0, 124 },
		{ 7, 15, 14, 0, 121 }, { 7, 15, 14, 0, 117 },
		{ 7, 15, 14, 0, 114 }, { 7, 15, 14, 0, 111 },
		{ 7, 15, 14, 0, 107 }, { 7, 15, 14, 0, 104 },
		{ 7, 15, 14, 0, 101 }, { 7, 15, 14, 0, 99 },
		{ 7, 15, 14, 0, 96 }, { 7, 15, 14, 0, 93 },
		{ 7, 15, 14, 0, 90 }, { 7, 15, 14, 0, 88 },
		{ 7, 15, 14, 0, 85 }, { 7, 15, 14, 0, 83 },
		{ 7, 15, 14, 0, 81 }, { 7, 15, 14, 0, 78 },
		{ 7, 15, 14, 0, 76 }, { 7, 15, 14, 0, 74 },
		{ 7, 15, 14, 0, 72 }, { 7, 15, 14, 0, 70 },
		{ 7, 15, 14, 0, 68 }, { 7, 15, 14, 0, 66 },
		{ 7, 15, 14, 0, 64 }, { 7, 15, 14, 0, 62 },
		{ 7, 15, 14, 0, 60 }, { 7, 15, 14, 0, 59 },
		{ 7, 15, 14, 0, 57 }, { 7, 15, 13, 0, 72 },
		{ 7, 15, 13, 0, 70 }, { 7, 15, 13, 0, 68 },
		{ 7, 15, 13, 0, 66 }, { 7, 15, 13, 0, 64 },
		{ 7, 15, 13, 0, 62 }, { 7, 15, 13, 0, 60 },
		{ 7, 15, 13, 0, 59 }, { 7, 15, 13, 0, 57 },
		{ 7, 15, 12, 0, 71 }, { 7, 15, 12, 0, 69 },
		{ 7, 15, 12, 0, 67 }, { 7, 15, 12, 0, 65 },
		{ 7, 15, 12, 0, 63 }, { 7, 15, 12, 0, 62 },
		{ 7, 15, 12, 0, 60 }, { 7, 15, 12, 0, 58 },
		{ 7, 15, 12, 0, 57 }, { 7, 15, 11, 0, 70 },
		{ 7, 15, 11, 0, 68 }, { 7, 15, 11, 0, 66 },
		{ 7, 15, 11, 0, 65 }, { 7, 15, 11, 0, 63 },
		{ 7, 15, 11, 0, 61 }, { 7, 15, 11, 0, 59 },
		{ 7, 15, 11, 0, 58 }, { 7, 15, 10, 0, 71 },
		{ 7, 15, 10, 0, 69 }, { 7, 15, 10, 0, 67 },
		{ 7, 15, 10, 0, 65 }, { 7, 15, 10, 0, 63 },
		{ 7, 15, 10, 0, 61 }, { 7, 15, 10, 0, 60 },
		{ 7, 15, 10, 0, 58 }, { 7, 15, 10, 0, 56 },
		{ 7, 15, 9, 0, 70 }, { 7, 15, 9, 0, 68 },
		{ 7, 15, 9, 0, 66 }, { 7, 15, 9, 0, 64 },
		{ 7, 15, 9, 0, 62 }, { 7, 15, 9, 0, 60 },
		{ 7, 15, 9, 0, 59 }, { 7, 14, 9, 0, 72 },
		{ 7, 14, 9, 0, 70 }, { 7, 14, 9, 0, 68 },
		{ 7, 14, 9, 0, 66 }, { 7, 14, 9, 0, 64 },
		{ 7, 14, 9, 0, 62 }, { 7, 14, 9, 0, 60 },
		{ 7, 14, 9, 0, 59 }, { 7, 13, 9, 0, 72 },
		{ 7, 13, 9, 0, 70 }, { 7, 13, 9, 0, 68 },
		{ 7, 13, 9, 0, 66 }, { 7, 13, 9, 0, 64 },
		{ 7, 13, 9, 0, 63 }, { 7, 13, 9, 0, 61 },
		{ 7, 13, 9, 0, 59 }, { 7, 13, 9, 0, 57 },
		{ 7, 13, 8, 0, 72 }, { 7, 13, 8, 0, 70 },
		{ 7, 13, 8, 0, 68 }, { 7, 13, 8, 0, 66 },
		{ 7, 13, 8, 0, 64 }, { 7, 13, 8, 0, 62 },
		{ 7, 13, 8, 0, 60 }, { 7, 13, 8, 0, 59 },
		{ 7, 12, 8, 0, 72 }, { 7, 12, 8, 0, 70 },
		{ 7, 12, 8, 0, 68 }, { 7, 12, 8, 0, 66 },
		{ 7, 12, 8, 0, 64 }, { 7, 12, 8, 0, 62 },
		{ 7, 12, 8, 0, 61 }, { 7, 12, 8, 0, 59 },
		{ 7, 12, 7, 0, 73 }, { 7, 12, 7, 0, 71 },
		{ 7, 12, 7, 0, 69 }, { 7, 12, 7, 0, 67 },
		{ 7, 12, 7, 0, 65 }, { 7, 12, 7, 0, 63 },
		{ 7, 12, 7, 0, 61 }, { 7, 12, 7, 0, 59 },
		{ 7, 11, 7, 0, 72 }, { 7, 11, 7, 0, 70 },
		{ 7, 11, 7, 0, 68 }, { 7, 11, 7, 0, 66 },
		{ 7, 11, 7, 0, 65 }, { 7, 11, 7, 0, 63 },
		{ 7, 11, 7, 0, 61 }, { 7, 11, 7, 0, 59 },
		{ 7, 11, 6, 0, 73 }, { 7, 11, 6, 0, 71 }
	};
	static struct bwn_txgain_entry txgain_2ghz_r0[] = {
		{ 4, 15, 9, 0, 64 }, { 4, 15, 9, 0, 62 },
		{ 4, 15, 9, 0, 60 }, { 4, 15, 9, 0, 59 },
		{ 4, 14, 9, 0, 72 }, { 4, 14, 9, 0, 70 },
		{ 4, 14, 9, 0, 68 }, { 4, 14, 9, 0, 66 },
		{ 4, 14, 9, 0, 64 }, { 4, 14, 9, 0, 62 },
		{ 4, 14, 9, 0, 60 }, { 4, 14, 9, 0, 59 },
		{ 4, 13, 9, 0, 72 }, { 4, 13, 9, 0, 70 },
		{ 4, 13, 9, 0, 68 }, { 4, 13, 9, 0, 66 },
		{ 4, 13, 9, 0, 64 }, { 4, 13, 9, 0, 63 },
		{ 4, 13, 9, 0, 61 }, { 4, 13, 9, 0, 59 },
		{ 4, 13, 9, 0, 57 }, { 4, 13, 8, 0, 72 },
		{ 4, 13, 8, 0, 70 }, { 4, 13, 8, 0, 68 },
		{ 4, 13, 8, 0, 66 }, { 4, 13, 8, 0, 64 },
		{ 4, 13, 8, 0, 62 }, { 4, 13, 8, 0, 60 },
		{ 4, 13, 8, 0, 59 }, { 4, 12, 8, 0, 72 },
		{ 4, 12, 8, 0, 70 }, { 4, 12, 8, 0, 68 },
		{ 4, 12, 8, 0, 66 }, { 4, 12, 8, 0, 64 },
		{ 4, 12, 8, 0, 62 }, { 4, 12, 8, 0, 61 },
		{ 4, 12, 8, 0, 59 }, { 4, 12, 7, 0, 73 },
		{ 4, 12, 7, 0, 71 }, { 4, 12, 7, 0, 69 },
		{ 4, 12, 7, 0, 67 }, { 4, 12, 7, 0, 65 },
		{ 4, 12, 7, 0, 63 }, { 4, 12, 7, 0, 61 },
		{ 4, 12, 7, 0, 59 }, { 4, 11, 7, 0, 72 },
		{ 4, 11, 7, 0, 70 }, { 4, 11, 7, 0, 68 },
		{ 4, 11, 7, 0, 66 }, { 4, 11, 7, 0, 65 },
		{ 4, 11, 7, 0, 63 }, { 4, 11, 7, 0, 61 },
		{ 4, 11, 7, 0, 59 }, { 4, 11, 6, 0, 73 },
		{ 4, 11, 6, 0, 71 }, { 4, 11, 6, 0, 69 },
		{ 4, 11, 6, 0, 67 }, { 4, 11, 6, 0, 65 },
		{ 4, 11, 6, 0, 63 }, { 4, 11, 6, 0, 61 },
		{ 4, 11, 6, 0, 60 }, { 4, 10, 6, 0, 72 },
		{ 4, 10, 6, 0, 70 }, { 4, 10, 6, 0, 68 },
		{ 4, 10, 6, 0, 66 }, { 4, 10, 6, 0, 64 },
		{ 4, 10, 6, 0, 62 }, { 4, 10, 6, 0, 60 },
		{ 4, 10, 6, 0, 59 }, { 4, 10, 5, 0, 72 },
		{ 4, 10, 5, 0, 70 }, { 4, 10, 5, 0, 68 },
		{ 4, 10, 5, 0, 66 }, { 4, 10, 5, 0, 64 },
		{ 4, 10, 5, 0, 62 }, { 4, 10, 5, 0, 60 },
		{ 4, 10, 5, 0, 59 }, { 4, 9, 5, 0, 70 },
		{ 4, 9, 5, 0, 68 }, { 4, 9, 5, 0, 66 },
		{ 4, 9, 5, 0, 64 }, { 4, 9, 5, 0, 63 },
		{ 4, 9, 5, 0, 61 }, { 4, 9, 5, 0, 59 },
		{ 4, 9, 4, 0, 71 }, { 4, 9, 4, 0, 69 },
		{ 4, 9, 4, 0, 67 }, { 4, 9, 4, 0, 65 },
		{ 4, 9, 4, 0, 63 }, { 4, 9, 4, 0, 62 },
		{ 4, 9, 4, 0, 60 }, { 4, 9, 4, 0, 58 },
		{ 4, 8, 4, 0, 70 }, { 4, 8, 4, 0, 68 },
		{ 4, 8, 4, 0, 66 }, { 4, 8, 4, 0, 65 },
		{ 4, 8, 4, 0, 63 }, { 4, 8, 4, 0, 61 },
		{ 4, 8, 4, 0, 59 }, { 4, 7, 4, 0, 68 },
		{ 4, 7, 4, 0, 66 }, { 4, 7, 4, 0, 64 },
		{ 4, 7, 4, 0, 62 }, { 4, 7, 4, 0, 61 },
		{ 4, 7, 4, 0, 59 }, { 4, 7, 3, 0, 67 },
		{ 4, 7, 3, 0, 65 }, { 4, 7, 3, 0, 63 },
		{ 4, 7, 3, 0, 62 }, { 4, 7, 3, 0, 60 },
		{ 4, 6, 3, 0, 65 }, { 4, 6, 3, 0, 63 },
		{ 4, 6, 3, 0, 61 }, { 4, 6, 3, 0, 60 },
		{ 4, 6, 3, 0, 58 }, { 4, 5, 3, 0, 68 },
		{ 4, 5, 3, 0, 66 }, { 4, 5, 3, 0, 64 },
		{ 4, 5, 3, 0, 62 }, { 4, 5, 3, 0, 60 },
		{ 4, 5, 3, 0, 59 }, { 4, 5, 3, 0, 57 },
		{ 4, 4, 2, 0, 83 }, { 4, 4, 2, 0, 81 },
		{ 4, 4, 2, 0, 78 }, { 4, 4, 2, 0, 76 },
		{ 4, 4, 2, 0, 74 }, { 4, 4, 2, 0, 72 }
	};
	static struct bwn_txgain_entry txgain_5ghz_r0[] = {
		{ 7, 15, 15, 0, 99 }, { 7, 15, 15, 0, 96 },
		{ 7, 15, 15, 0, 93 }, { 7, 15, 15, 0, 90 },
		{ 7, 15, 15, 0, 88 }, { 7, 15, 15, 0, 85 },
		{ 7, 15, 15, 0, 83 }, { 7, 15, 15, 0, 81 },
		{ 7, 15, 15, 0, 78 }, { 7, 15, 15, 0, 76 },
		{ 7, 15, 15, 0, 74 }, { 7, 15, 15, 0, 72 },
		{ 7, 15, 15, 0, 70 }, { 7, 15, 15, 0, 68 },
		{ 7, 15, 15, 0, 66 }, { 7, 15, 15, 0, 64 },
		{ 7, 15, 15, 0, 62 }, { 7, 15, 15, 0, 60 },
		{ 7, 15, 15, 0, 59 }, { 7, 15, 15, 0, 57 },
		{ 7, 15, 15, 0, 55 }, { 7, 15, 14, 0, 72 },
		{ 7, 15, 14, 0, 70 }, { 7, 15, 14, 0, 68 },
		{ 7, 15, 14, 0, 66 }, { 7, 15, 14, 0, 64 },
		{ 7, 15, 14, 0, 62 }, { 7, 15, 14, 0, 60 },
		{ 7, 15, 14, 0, 58 }, { 7, 15, 14, 0, 56 },
		{ 7, 15, 14, 0, 55 }, { 7, 15, 13, 0, 71 },
		{ 7, 15, 13, 0, 69 }, { 7, 15, 13, 0, 67 },
		{ 7, 15, 13, 0, 65 }, { 7, 15, 13, 0, 63 },
		{ 7, 15, 13, 0, 62 }, { 7, 15, 13, 0, 60 },
		{ 7, 15, 13, 0, 58 }, { 7, 15, 13, 0, 56 },
		{ 7, 15, 12, 0, 72 }, { 7, 15, 12, 0, 70 },
		{ 7, 15, 12, 0, 68 }, { 7, 15, 12, 0, 66 },
		{ 7, 15, 12, 0, 64 }, { 7, 15, 12, 0, 62 },
		{ 7, 15, 12, 0, 60 }, { 7, 15, 12, 0, 59 },
		{ 7, 15, 12, 0, 57 }, { 7, 15, 11, 0, 73 },
		{ 7, 15, 11, 0, 71 }, { 7, 15, 11, 0, 69 },
		{ 7, 15, 11, 0, 67 }, { 7, 15, 11, 0, 65 },
		{ 7, 15, 11, 0, 63 }, { 7, 15, 11, 0, 61 },
		{ 7, 15, 11, 0, 60 }, { 7, 15, 11, 0, 58 },
		{ 7, 15, 10, 0, 71 }, { 7, 15, 10, 0, 69 },
		{ 7, 15, 10, 0, 67 }, { 7, 15, 10, 0, 65 },
		{ 7, 15, 10, 0, 63 }, { 7, 15, 10, 0, 61 },
		{ 7, 15, 10, 0, 60 }, { 7, 15, 10, 0, 58 },
		{ 7, 15, 9, 0, 70 }, { 7, 15, 9, 0, 68 },
		{ 7, 15, 9, 0, 66 }, { 7, 15, 9, 0, 64 },
		{ 7, 15, 9, 0, 62 }, { 7, 15, 9, 0, 61 },
		{ 7, 15, 9, 0, 59 }, { 7, 15, 9, 0, 57 },
		{ 7, 15, 9, 0, 56 }, { 7, 14, 9, 0, 68 },
		{ 7, 14, 9, 0, 66 }, { 7, 14, 9, 0, 65 },
		{ 7, 14, 9, 0, 63 }, { 7, 14, 9, 0, 61 },
		{ 7, 14, 9, 0, 59 }, { 7, 14, 9, 0, 58 },
		{ 7, 13, 9, 0, 70 }, { 7, 13, 9, 0, 68 },
		{ 7, 13, 9, 0, 66 }, { 7, 13, 9, 0, 64 },
		{ 7, 13, 9, 0, 63 }, { 7, 13, 9, 0, 61 },
		{ 7, 13, 9, 0, 59 }, { 7, 13, 9, 0, 57 },
		{ 7, 13, 8, 0, 70 }, { 7, 13, 8, 0, 68 },
		{ 7, 13, 8, 0, 66 }, { 7, 13, 8, 0, 64 },
		{ 7, 13, 8, 0, 62 }, { 7, 13, 8, 0, 60 },
		{ 7, 13, 8, 0, 59 }, { 7, 13, 8, 0, 57 },
		{ 7, 12, 8, 0, 70 }, { 7, 12, 8, 0, 68 },
		{ 7, 12, 8, 0, 66 }, { 7, 12, 8, 0, 64 },
		{ 7, 12, 8, 0, 62 }, { 7, 12, 8, 0, 61 },
		{ 7, 12, 8, 0, 59 }, { 7, 12, 8, 0, 57 },
		{ 7, 12, 7, 0, 70 }, { 7, 12, 7, 0, 68 },
		{ 7, 12, 7, 0, 66 }, { 7, 12, 7, 0, 64 },
		{ 7, 12, 7, 0, 62 }, { 7, 12, 7, 0, 61 },
		{ 7, 12, 7, 0, 59 }, { 7, 12, 7, 0, 57 },
		{ 7, 11, 7, 0, 70 }, { 7, 11, 7, 0, 68 },
		{ 7, 11, 7, 0, 66 }, { 7, 11, 7, 0, 64 },
		{ 7, 11, 7, 0, 62 }, { 7, 11, 7, 0, 61 },
		{ 7, 11, 7, 0, 59 }, { 7, 11, 7, 0, 57 },
		{ 7, 11, 6, 0, 69 }, { 7, 11, 6, 0, 67 },
		{ 7, 11, 6, 0, 65 }, { 7, 11, 6, 0, 63 },
		{ 7, 11, 6, 0, 62 }, { 7, 11, 6, 0, 60 }
	};
	static struct bwn_txgain_entry txgain_r1[] = {
		{ 7, 15, 14, 0, 152 }, { 7, 15, 14, 0, 147 },
		{ 7, 15, 14, 0, 143 }, { 7, 15, 14, 0, 139 },
		{ 7, 15, 14, 0, 135 }, { 7, 15, 14, 0, 131 },
		{ 7, 15, 14, 0, 128 }, { 7, 15, 14, 0, 124 },
		{ 7, 15, 14, 0, 121 }, { 7, 15, 14, 0, 117 },
		{ 7, 15, 14, 0, 114 }, { 7, 15, 14, 0, 111 },
		{ 7, 15, 14, 0, 107 }, { 7, 15, 14, 0, 104 },
		{ 7, 15, 14, 0, 101 }, { 7, 15, 14, 0, 99 },
		{ 7, 15, 14, 0, 96 }, { 7, 15, 14, 0, 93 },
		{ 7, 15, 14, 0, 90 }, { 7, 15, 14, 0, 88 },
		{ 7, 15, 14, 0, 85 }, { 7, 15, 14, 0, 83 },
		{ 7, 15, 14, 0, 81 }, { 7, 15, 14, 0, 78 },
		{ 7, 15, 14, 0, 76 }, { 7, 15, 14, 0, 74 },
		{ 7, 15, 14, 0, 72 }, { 7, 15, 14, 0, 70 },
		{ 7, 15, 14, 0, 68 }, { 7, 15, 14, 0, 66 },
		{ 7, 15, 14, 0, 64 }, { 7, 15, 14, 0, 62 },
		{ 7, 15, 14, 0, 60 }, { 7, 15, 14, 0, 59 },
		{ 7, 15, 14, 0, 57 }, { 7, 15, 13, 0, 72 },
		{ 7, 15, 13, 0, 70 }, { 7, 15, 14, 0, 68 },
		{ 7, 15, 14, 0, 66 }, { 7, 15, 14, 0, 64 },
		{ 7, 15, 14, 0, 62 }, { 7, 15, 14, 0, 60 },
		{ 7, 15, 14, 0, 59 }, { 7, 15, 14, 0, 57 },
		{ 7, 15, 13, 0, 72 }, { 7, 15, 13, 0, 70 },
		{ 7, 15, 13, 0, 68 }, { 7, 15, 13, 0, 66 },
		{ 7, 15, 13, 0, 64 }, { 7, 15, 13, 0, 62 },
		{ 7, 15, 13, 0, 60 }, { 7, 15, 13, 0, 59 },
		{ 7, 15, 13, 0, 57 }, { 7, 15, 12, 0, 71 },
		{ 7, 15, 12, 0, 69 }, { 7, 15, 12, 0, 67 },
		{ 7, 15, 12, 0, 65 }, { 7, 15, 12, 0, 63 },
		{ 7, 15, 12, 0, 62 }, { 7, 15, 12, 0, 60 },
		{ 7, 15, 12, 0, 58 }, { 7, 15, 12, 0, 57 },
		{ 7, 15, 11, 0, 70 }, { 7, 15, 11, 0, 68 },
		{ 7, 15, 11, 0, 66 }, { 7, 15, 11, 0, 65 },
		{ 7, 15, 11, 0, 63 }, { 7, 15, 11, 0, 61 },
		{ 7, 15, 11, 0, 59 }, { 7, 15, 11, 0, 58 },
		{ 7, 15, 10, 0, 71 }, { 7, 15, 10, 0, 69 },
		{ 7, 15, 10, 0, 67 }, { 7, 15, 10, 0, 65 },
		{ 7, 15, 10, 0, 63 }, { 7, 15, 10, 0, 61 },
		{ 7, 15, 10, 0, 60 }, { 7, 15, 10, 0, 58 },
		{ 7, 15, 10, 0, 56 }, { 7, 15, 9, 0, 70 },
		{ 7, 15, 9, 0, 68 }, { 7, 15, 9, 0, 66 },
		{ 7, 15, 9, 0, 64 }, { 7, 15, 9, 0, 62 },
		{ 7, 15, 9, 0, 60 }, { 7, 15, 9, 0, 59 },
		{ 7, 14, 9, 0, 72 }, { 7, 14, 9, 0, 70 },
		{ 7, 14, 9, 0, 68 }, { 7, 14, 9, 0, 66 },
		{ 7, 14, 9, 0, 64 }, { 7, 14, 9, 0, 62 },
		{ 7, 14, 9, 0, 60 }, { 7, 14, 9, 0, 59 },
		{ 7, 13, 9, 0, 72 }, { 7, 13, 9, 0, 70 },
		{ 7, 13, 9, 0, 68 }, { 7, 13, 9, 0, 66 },
		{ 7, 13, 9, 0, 64 }, { 7, 13, 9, 0, 63 },
		{ 7, 13, 9, 0, 61 }, { 7, 13, 9, 0, 59 },
		{ 7, 13, 9, 0, 57 }, { 7, 13, 8, 0, 72 },
		{ 7, 13, 8, 0, 70 }, { 7, 13, 8, 0, 68 },
		{ 7, 13, 8, 0, 66 }, { 7, 13, 8, 0, 64 },
		{ 7, 13, 8, 0, 62 }, { 7, 13, 8, 0, 60 },
		{ 7, 13, 8, 0, 59 }, { 7, 12, 8, 0, 72 },
		{ 7, 12, 8, 0, 70 }, { 7, 12, 8, 0, 68 },
		{ 7, 12, 8, 0, 66 }, { 7, 12, 8, 0, 64 },
		{ 7, 12, 8, 0, 62 }, { 7, 12, 8, 0, 61 },
		{ 7, 12, 8, 0, 59 }, { 7, 12, 7, 0, 73 },
		{ 7, 12, 7, 0, 71 }, { 7, 12, 7, 0, 69 },
		{ 7, 12, 7, 0, 67 }, { 7, 12, 7, 0, 65 },
		{ 7, 12, 7, 0, 63 }, { 7, 12, 7, 0, 61 },
		{ 7, 12, 7, 0, 59 }, { 7, 11, 7, 0, 72 },
		{ 7, 11, 7, 0, 70 }, { 7, 11, 7, 0, 68 },
		{ 7, 11, 7, 0, 66 }, { 7, 11, 7, 0, 65 },
		{ 7, 11, 7, 0, 63 }, { 7, 11, 7, 0, 61 },
		{ 7, 11, 7, 0, 59 }, { 7, 11, 6, 0, 73 },
		{ 7, 11, 6, 0, 71 }
	};
	static struct bwn_txgain_entry txgain_2ghz_r1[] = {
		{ 4, 15, 15, 0, 90 }, { 4, 15, 15, 0, 88 },
		{ 4, 15, 15, 0, 85 }, { 4, 15, 15, 0, 83 },
		{ 4, 15, 15, 0, 81 }, { 4, 15, 15, 0, 78 },
		{ 4, 15, 15, 0, 76 }, { 4, 15, 15, 0, 74 },
		{ 4, 15, 15, 0, 72 }, { 4, 15, 15, 0, 70 },
		{ 4, 15, 15, 0, 68 }, { 4, 15, 15, 0, 66 },
		{ 4, 15, 15, 0, 64 }, { 4, 15, 15, 0, 62 },
		{ 4, 15, 15, 0, 60 }, { 4, 15, 15, 0, 59 },
		{ 4, 15, 14, 0, 72 }, { 4, 15, 14, 0, 70 },
		{ 4, 15, 14, 0, 68 }, { 4, 15, 14, 0, 66 },
		{ 4, 15, 14, 0, 64 }, { 4, 15, 14, 0, 62 },
		{ 4, 15, 14, 0, 60 }, { 4, 15, 14, 0, 59 },
		{ 4, 15, 13, 0, 72 }, { 4, 15, 13, 0, 70 },
		{ 4, 15, 13, 0, 68 }, { 4, 15, 13, 0, 66 },
		{ 4, 15, 13, 0, 64 }, { 4, 15, 13, 0, 62 },
		{ 4, 15, 13, 0, 60 }, { 4, 15, 13, 0, 59 },
		{ 4, 15, 12, 0, 72 }, { 4, 15, 12, 0, 70 },
		{ 4, 15, 12, 0, 68 }, { 4, 15, 12, 0, 66 },
		{ 4, 15, 12, 0, 64 }, { 4, 15, 12, 0, 62 },
		{ 4, 15, 12, 0, 60 }, { 4, 15, 12, 0, 59 },
		{ 4, 15, 11, 0, 72 }, { 4, 15, 11, 0, 70 },
		{ 4, 15, 11, 0, 68 }, { 4, 15, 11, 0, 66 },
		{ 4, 15, 11, 0, 64 }, { 4, 15, 11, 0, 62 },
		{ 4, 15, 11, 0, 60 }, { 4, 15, 11, 0, 59 },
		{ 4, 15, 10, 0, 72 }, { 4, 15, 10, 0, 70 },
		{ 4, 15, 10, 0, 68 }, { 4, 15, 10, 0, 66 },
		{ 4, 15, 10, 0, 64 }, { 4, 15, 10, 0, 62 },
		{ 4, 15, 10, 0, 60 }, { 4, 15, 10, 0, 59 },
		{ 4, 15, 9, 0, 72 }, { 4, 15, 9, 0, 70 },
		{ 4, 15, 9, 0, 68 }, { 4, 15, 9, 0, 66 },
		{ 4, 15, 9, 0, 64 }, { 4, 15, 9, 0, 62 },
		{ 4, 15, 9, 0, 60 }, { 4, 15, 9, 0, 59 },
		{ 4, 14, 9, 0, 72 }, { 4, 14, 9, 0, 70 },
		{ 4, 14, 9, 0, 68 }, { 4, 14, 9, 0, 66 },
		{ 4, 14, 9, 0, 64 }, { 4, 14, 9, 0, 62 },
		{ 4, 14, 9, 0, 60 }, { 4, 14, 9, 0, 59 },
		{ 4, 13, 9, 0, 72 }, { 4, 13, 9, 0, 70 },
		{ 4, 13, 9, 0, 68 }, { 4, 13, 9, 0, 66 },
		{ 4, 13, 9, 0, 64 }, { 4, 13, 9, 0, 63 },
		{ 4, 13, 9, 0, 61 }, { 4, 13, 9, 0, 59 },
		{ 4, 13, 9, 0, 57 }, { 4, 13, 8, 0, 72 },
		{ 4, 13, 8, 0, 70 }, { 4, 13, 8, 0, 68 },
		{ 4, 13, 8, 0, 66 }, { 4, 13, 8, 0, 64 },
		{ 4, 13, 8, 0, 62 }, { 4, 13, 8, 0, 60 },
		{ 4, 13, 8, 0, 59 }, { 4, 12, 8, 0, 72 },
		{ 4, 12, 8, 0, 70 }, { 4, 12, 8, 0, 68 },
		{ 4, 12, 8, 0, 66 }, { 4, 12, 8, 0, 64 },
		{ 4, 12, 8, 0, 62 }, { 4, 12, 8, 0, 61 },
		{ 4, 12, 8, 0, 59 }, { 4, 12, 7, 0, 73 },
		{ 4, 12, 7, 0, 71 }, { 4, 12, 7, 0, 69 },
		{ 4, 12, 7, 0, 67 }, { 4, 12, 7, 0, 65 },
		{ 4, 12, 7, 0, 63 }, { 4, 12, 7, 0, 61 },
		{ 4, 12, 7, 0, 59 }, { 4, 11, 7, 0, 72 },
		{ 4, 11, 7, 0, 70 }, { 4, 11, 7, 0, 68 },
		{ 4, 11, 7, 0, 66 }, { 4, 11, 7, 0, 65 },
		{ 4, 11, 7, 0, 63 }, { 4, 11, 7, 0, 61 },
		{ 4, 11, 7, 0, 59 }, { 4, 11, 6, 0, 73 },
		{ 4, 11, 6, 0, 71 }, { 4, 11, 6, 0, 69 },
		{ 4, 11, 6, 0, 67 }, { 4, 11, 6, 0, 65 },
		{ 4, 11, 6, 0, 63 }, { 4, 11, 6, 0, 61 },
		{ 4, 11, 6, 0, 60 }, { 4, 10, 6, 0, 72 },
		{ 4, 10, 6, 0, 70 }, { 4, 10, 6, 0, 68 },
		{ 4, 10, 6, 0, 66 }, { 4, 10, 6, 0, 64 },
		{ 4, 10, 6, 0, 62 }, { 4, 10, 6, 0, 60 }
	};
	static struct bwn_txgain_entry txgain_5ghz_r1[] = {
		{ 7, 15, 15, 0, 99 }, { 7, 15, 15, 0, 96 },
		{ 7, 15, 15, 0, 93 }, { 7, 15, 15, 0, 90 },
		{ 7, 15, 15, 0, 88 }, { 7, 15, 15, 0, 85 },
		{ 7, 15, 15, 0, 83 }, { 7, 15, 15, 0, 81 },
		{ 7, 15, 15, 0, 78 }, { 7, 15, 15, 0, 76 },
		{ 7, 15, 15, 0, 74 }, { 7, 15, 15, 0, 72 },
		{ 7, 15, 15, 0, 70 }, { 7, 15, 15, 0, 68 },
		{ 7, 15, 15, 0, 66 }, { 7, 15, 15, 0, 64 },
		{ 7, 15, 15, 0, 62 }, { 7, 15, 15, 0, 60 },
		{ 7, 15, 15, 0, 59 }, { 7, 15, 15, 0, 57 },
		{ 7, 15, 15, 0, 55 }, { 7, 15, 14, 0, 72 },
		{ 7, 15, 14, 0, 70 }, { 7, 15, 14, 0, 68 },
		{ 7, 15, 14, 0, 66 }, { 7, 15, 14, 0, 64 },
		{ 7, 15, 14, 0, 62 }, { 7, 15, 14, 0, 60 },
		{ 7, 15, 14, 0, 58 }, { 7, 15, 14, 0, 56 },
		{ 7, 15, 14, 0, 55 }, { 7, 15, 13, 0, 71 },
		{ 7, 15, 13, 0, 69 }, { 7, 15, 13, 0, 67 },
		{ 7, 15, 13, 0, 65 }, { 7, 15, 13, 0, 63 },
		{ 7, 15, 13, 0, 62 }, { 7, 15, 13, 0, 60 },
		{ 7, 15, 13, 0, 58 }, { 7, 15, 13, 0, 56 },
		{ 7, 15, 12, 0, 72 }, { 7, 15, 12, 0, 70 },
		{ 7, 15, 12, 0, 68 }, { 7, 15, 12, 0, 66 },
		{ 7, 15, 12, 0, 64 }, { 7, 15, 12, 0, 62 },
		{ 7, 15, 12, 0, 60 }, { 7, 15, 12, 0, 59 },
		{ 7, 15, 12, 0, 57 }, { 7, 15, 11, 0, 73 },
		{ 7, 15, 11, 0, 71 }, { 7, 15, 11, 0, 69 },
		{ 7, 15, 11, 0, 67 }, { 7, 15, 11, 0, 65 },
		{ 7, 15, 11, 0, 63 }, { 7, 15, 11, 0, 61 },
		{ 7, 15, 11, 0, 60 }, { 7, 15, 11, 0, 58 },
		{ 7, 15, 10, 0, 71 }, { 7, 15, 10, 0, 69 },
		{ 7, 15, 10, 0, 67 }, { 7, 15, 10, 0, 65 },
		{ 7, 15, 10, 0, 63 }, { 7, 15, 10, 0, 61 },
		{ 7, 15, 10, 0, 60 }, { 7, 15, 10, 0, 58 },
		{ 7, 15, 9, 0, 70 }, { 7, 15, 9, 0, 68 },
		{ 7, 15, 9, 0, 66 }, { 7, 15, 9, 0, 64 },
		{ 7, 15, 9, 0, 62 }, { 7, 15, 9, 0, 61 },
		{ 7, 15, 9, 0, 59 }, { 7, 15, 9, 0, 57 },
		{ 7, 15, 9, 0, 56 }, { 7, 14, 9, 0, 68 },
		{ 7, 14, 9, 0, 66 }, { 7, 14, 9, 0, 65 },
		{ 7, 14, 9, 0, 63 }, { 7, 14, 9, 0, 61 },
		{ 7, 14, 9, 0, 59 }, { 7, 14, 9, 0, 58 },
		{ 7, 13, 9, 0, 70 }, { 7, 13, 9, 0, 68 },
		{ 7, 13, 9, 0, 66 }, { 7, 13, 9, 0, 64 },
		{ 7, 13, 9, 0, 63 }, { 7, 13, 9, 0, 61 },
		{ 7, 13, 9, 0, 59 }, { 7, 13, 9, 0, 57 },
		{ 7, 13, 8, 0, 70 }, { 7, 13, 8, 0, 68 },
		{ 7, 13, 8, 0, 66 }, { 7, 13, 8, 0, 64 },
		{ 7, 13, 8, 0, 62 }, { 7, 13, 8, 0, 60 },
		{ 7, 13, 8, 0, 59 }, { 7, 13, 8, 0, 57 },
		{ 7, 12, 8, 0, 70 }, { 7, 12, 8, 0, 68 },
		{ 7, 12, 8, 0, 66 }, { 7, 12, 8, 0, 64 },
		{ 7, 12, 8, 0, 62 }, { 7, 12, 8, 0, 61 },
		{ 7, 12, 8, 0, 59 }, { 7, 12, 8, 0, 57 },
		{ 7, 12, 7, 0, 70 }, { 7, 12, 7, 0, 68 },
		{ 7, 12, 7, 0, 66 }, { 7, 12, 7, 0, 64 },
		{ 7, 12, 7, 0, 62 }, { 7, 12, 7, 0, 61 },
		{ 7, 12, 7, 0, 59 }, { 7, 12, 7, 0, 57 },
		{ 7, 11, 7, 0, 70 }, { 7, 11, 7, 0, 68 },
		{ 7, 11, 7, 0, 66 }, { 7, 11, 7, 0, 64 },
		{ 7, 11, 7, 0, 62 }, { 7, 11, 7, 0, 61 },
		{ 7, 11, 7, 0, 59 }, { 7, 11, 7, 0, 57 },
		{ 7, 11, 6, 0, 69 }, { 7, 11, 6, 0, 67 },
		{ 7, 11, 6, 0, 65 }, { 7, 11, 6, 0, 63 },
		{ 7, 11, 6, 0, 62 }, { 7, 11, 6, 0, 60 }
	};

	if (mac->mac_phy.rev != 0 && mac->mac_phy.rev != 1) {
		if (sc->sc_board_info.board_flags & BHND_BFL_NOPA)
			bwn_phy_lp_gaintbl_write_multi(mac, 0, 128, txgain_r2);
		else if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan))
			bwn_phy_lp_gaintbl_write_multi(mac, 0, 128,
			    txgain_2ghz_r2);
		else
			bwn_phy_lp_gaintbl_write_multi(mac, 0, 128,
			    txgain_5ghz_r2);
		return;
	}

	if (mac->mac_phy.rev == 0) {
		if ((sc->sc_board_info.board_flags & BHND_BFL_NOPA) ||
		    (sc->sc_board_info.board_flags & BHND_BFL_HGPA))
			bwn_phy_lp_gaintbl_write_multi(mac, 0, 128, txgain_r0);
		else if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan))
			bwn_phy_lp_gaintbl_write_multi(mac, 0, 128,
			    txgain_2ghz_r0);
		else
			bwn_phy_lp_gaintbl_write_multi(mac, 0, 128,
			    txgain_5ghz_r0);
		return;
	}

	if ((sc->sc_board_info.board_flags & BHND_BFL_NOPA) ||
	    (sc->sc_board_info.board_flags & BHND_BFL_HGPA))
		bwn_phy_lp_gaintbl_write_multi(mac, 0, 128, txgain_r1);
	else if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan))
		bwn_phy_lp_gaintbl_write_multi(mac, 0, 128, txgain_2ghz_r1);
	else
		bwn_phy_lp_gaintbl_write_multi(mac, 0, 128, txgain_5ghz_r1);
}

static void
bwn_tab_write(struct bwn_mac *mac, uint32_t typeoffset, uint32_t value)
{
	uint32_t offset, type;

	type = BWN_TAB_GETTYPE(typeoffset);
	offset = BWN_TAB_GETOFFSET(typeoffset);
	KASSERT(offset <= 0xffff, ("%s:%d: fail", __func__, __LINE__));

	switch (type) {
	case BWN_TAB_8BIT:
		KASSERT(!(value & ~0xff), ("%s:%d: fail", __func__, __LINE__));
		BWN_PHY_WRITE(mac, BWN_PHY_TABLE_ADDR, offset);
		BWN_PHY_WRITE(mac, BWN_PHY_TABLEDATALO, value);
		break;
	case BWN_TAB_16BIT:
		KASSERT(!(value & ~0xffff),
		    ("%s:%d: fail", __func__, __LINE__));
		BWN_PHY_WRITE(mac, BWN_PHY_TABLE_ADDR, offset);
		BWN_PHY_WRITE(mac, BWN_PHY_TABLEDATALO, value);
		break;
	case BWN_TAB_32BIT:
		BWN_PHY_WRITE(mac, BWN_PHY_TABLE_ADDR, offset);
		BWN_PHY_WRITE(mac, BWN_PHY_TABLEDATAHI, value >> 16);
		BWN_PHY_WRITE(mac, BWN_PHY_TABLEDATALO, value);
		break;
	default:
		KASSERT(0 == 1, ("%s:%d: fail", __func__, __LINE__));
	}
}

static int
bwn_phy_lp_loopback(struct bwn_mac *mac)
{
	struct bwn_phy_lp_iq_est ie;
	int i, index = -1;
	uint32_t tmp;

	memset(&ie, 0, sizeof(ie));

	bwn_phy_lp_set_trsw_over(mac, 1, 1);
	BWN_PHY_SET(mac, BWN_PHY_AFE_CTL_OVR, 1);
	BWN_PHY_MASK(mac, BWN_PHY_AFE_CTL_OVRVAL, 0xfffe);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x800);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0x800);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x8);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0x8);
	BWN_RF_WRITE(mac, BWN_B2062_N_TXCTL_A, 0x80);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_0, 0x80);
	BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_VAL_0, 0x80);
	for (i = 0; i < 32; i++) {
		bwn_phy_lp_set_rxgain_idx(mac, i);
		bwn_phy_lp_ddfs_turnon(mac, 1, 1, 5, 5, 0);
		if (!(bwn_phy_lp_rx_iq_est(mac, 1000, 32, &ie)))
			continue;
		tmp = (ie.ie_ipwr + ie.ie_qpwr) / 1000;
		if ((tmp > 4000) && (tmp < 10000)) {
			index = i;
			break;
		}
	}
	bwn_phy_lp_ddfs_turnoff(mac);
	return (index);
}

static void
bwn_phy_lp_set_rxgain_idx(struct bwn_mac *mac, uint16_t idx)
{

	bwn_phy_lp_set_rxgain(mac, bwn_tab_read(mac, BWN_TAB_2(12, idx)));
}

static void
bwn_phy_lp_ddfs_turnon(struct bwn_mac *mac, int i_on, int q_on,
    int incr1, int incr2, int scale_idx)
{

	bwn_phy_lp_ddfs_turnoff(mac);
	BWN_PHY_MASK(mac, BWN_PHY_AFE_DDFS_POINTER_INIT, 0xff80);
	BWN_PHY_MASK(mac, BWN_PHY_AFE_DDFS_POINTER_INIT, 0x80ff);
	BWN_PHY_SETMASK(mac, BWN_PHY_AFE_DDFS_INCR_INIT, 0xff80, incr1);
	BWN_PHY_SETMASK(mac, BWN_PHY_AFE_DDFS_INCR_INIT, 0x80ff, incr2 << 8);
	BWN_PHY_SETMASK(mac, BWN_PHY_AFE_DDFS, 0xfff7, i_on << 3);
	BWN_PHY_SETMASK(mac, BWN_PHY_AFE_DDFS, 0xffef, q_on << 4);
	BWN_PHY_SETMASK(mac, BWN_PHY_AFE_DDFS, 0xff9f, scale_idx << 5);
	BWN_PHY_MASK(mac, BWN_PHY_AFE_DDFS, 0xfffb);
	BWN_PHY_SET(mac, BWN_PHY_AFE_DDFS, 0x2);
	BWN_PHY_SET(mac, BWN_PHY_LP_PHY_CTL, 0x20);
}

static uint8_t
bwn_phy_lp_rx_iq_est(struct bwn_mac *mac, uint16_t sample, uint8_t time,
    struct bwn_phy_lp_iq_est *ie)
{
	int i;

	BWN_PHY_MASK(mac, BWN_PHY_CRSGAIN_CTL, 0xfff7);
	BWN_PHY_WRITE(mac, BWN_PHY_IQ_NUM_SMPLS_ADDR, sample);
	BWN_PHY_SETMASK(mac, BWN_PHY_IQ_ENABLE_WAIT_TIME_ADDR, 0xff00, time);
	BWN_PHY_MASK(mac, BWN_PHY_IQ_ENABLE_WAIT_TIME_ADDR, 0xfeff);
	BWN_PHY_SET(mac, BWN_PHY_IQ_ENABLE_WAIT_TIME_ADDR, 0x200);

	for (i = 0; i < 500; i++) {
		if (!(BWN_PHY_READ(mac,
		    BWN_PHY_IQ_ENABLE_WAIT_TIME_ADDR) & 0x200))
			break;
		DELAY(1000);
	}
	if ((BWN_PHY_READ(mac, BWN_PHY_IQ_ENABLE_WAIT_TIME_ADDR) & 0x200)) {
		BWN_PHY_SET(mac, BWN_PHY_CRSGAIN_CTL, 0x8);
		return 0;
	}

	ie->ie_iqprod = BWN_PHY_READ(mac, BWN_PHY_IQ_ACC_HI_ADDR);
	ie->ie_iqprod <<= 16;
	ie->ie_iqprod |= BWN_PHY_READ(mac, BWN_PHY_IQ_ACC_LO_ADDR);
	ie->ie_ipwr = BWN_PHY_READ(mac, BWN_PHY_IQ_I_PWR_ACC_HI_ADDR);
	ie->ie_ipwr <<= 16;
	ie->ie_ipwr |= BWN_PHY_READ(mac, BWN_PHY_IQ_I_PWR_ACC_LO_ADDR);
	ie->ie_qpwr = BWN_PHY_READ(mac, BWN_PHY_IQ_Q_PWR_ACC_HI_ADDR);
	ie->ie_qpwr <<= 16;
	ie->ie_qpwr |= BWN_PHY_READ(mac, BWN_PHY_IQ_Q_PWR_ACC_LO_ADDR);

	BWN_PHY_SET(mac, BWN_PHY_CRSGAIN_CTL, 0x8);
	return 1;
}

static uint32_t
bwn_tab_read(struct bwn_mac *mac, uint32_t typeoffset)
{
	uint32_t offset, type, value;

	type = BWN_TAB_GETTYPE(typeoffset);
	offset = BWN_TAB_GETOFFSET(typeoffset);
	KASSERT(offset <= 0xffff, ("%s:%d: fail", __func__, __LINE__));

	switch (type) {
	case BWN_TAB_8BIT:
		BWN_PHY_WRITE(mac, BWN_PHY_TABLE_ADDR, offset);
		value = BWN_PHY_READ(mac, BWN_PHY_TABLEDATALO) & 0xff;
		break;
	case BWN_TAB_16BIT:
		BWN_PHY_WRITE(mac, BWN_PHY_TABLE_ADDR, offset);
		value = BWN_PHY_READ(mac, BWN_PHY_TABLEDATALO);
		break;
	case BWN_TAB_32BIT:
		BWN_PHY_WRITE(mac, BWN_PHY_TABLE_ADDR, offset);
		value = BWN_PHY_READ(mac, BWN_PHY_TABLEDATAHI);
		value <<= 16;
		value |= BWN_PHY_READ(mac, BWN_PHY_TABLEDATALO);
		break;
	default:
		KASSERT(0 == 1, ("%s:%d: fail", __func__, __LINE__));
		value = 0;
	}

	return (value);
}

static void
bwn_phy_lp_ddfs_turnoff(struct bwn_mac *mac)
{

	BWN_PHY_MASK(mac, BWN_PHY_AFE_DDFS, 0xfffd);
	BWN_PHY_MASK(mac, BWN_PHY_LP_PHY_CTL, 0xffdf);
}

static void
bwn_phy_lp_set_txgain_dac(struct bwn_mac *mac, uint16_t dac)
{
	uint16_t ctl;

	ctl = BWN_PHY_READ(mac, BWN_PHY_AFE_DAC_CTL) & 0xc7f;
	ctl |= dac << 7;
	BWN_PHY_SETMASK(mac, BWN_PHY_AFE_DAC_CTL, 0xf000, ctl);
}

static void
bwn_phy_lp_set_txgain_pa(struct bwn_mac *mac, uint16_t gain)
{

	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0xfb), 0xe03f, gain << 6);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0xfd), 0x80ff, gain << 8);
}

static void
bwn_phy_lp_set_txgain_override(struct bwn_mac *mac)
{

	if (mac->mac_phy.rev < 2)
		BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_2, 0x100);
	else {
		BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_2, 0x80);
		BWN_PHY_SET(mac, BWN_PHY_RF_OVERRIDE_2, 0x4000);
	}
	BWN_PHY_SET(mac, BWN_PHY_AFE_CTL_OVR, 0x40);
}

static uint16_t
bwn_phy_lp_get_pa_gain(struct bwn_mac *mac)
{

	return BWN_PHY_READ(mac, BWN_PHY_OFDM(0xfb)) & 0x7f;
}

static uint8_t
bwn_nbits(int32_t val)
{
	uint32_t tmp;
	uint8_t nbits = 0;

	for (tmp = abs(val); tmp != 0; tmp >>= 1)
		nbits++;
	return (nbits);
}

static void
bwn_phy_lp_gaintbl_write_multi(struct bwn_mac *mac, int offset, int count,
    struct bwn_txgain_entry *table)
{
	int i;

	for (i = offset; i < count; i++)
		bwn_phy_lp_gaintbl_write(mac, i, table[i]);
}

static void
bwn_phy_lp_gaintbl_write(struct bwn_mac *mac, int offset,
    struct bwn_txgain_entry data)
{

	if (mac->mac_phy.rev >= 2)
		bwn_phy_lp_gaintbl_write_r2(mac, offset, data);
	else
		bwn_phy_lp_gaintbl_write_r01(mac, offset, data);
}

static void
bwn_phy_lp_gaintbl_write_r2(struct bwn_mac *mac, int offset,
    struct bwn_txgain_entry te)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	KASSERT(mac->mac_phy.rev >= 2, ("%s:%d: fail", __func__, __LINE__));

	tmp = (te.te_pad << 16) | (te.te_pga << 8) | te.te_gm;
	if (mac->mac_phy.rev >= 3) {
		tmp |= ((IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan)) ?
		    (0x10 << 24) : (0x70 << 24));
	} else {
		tmp |= ((IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan)) ?
		    (0x14 << 24) : (0x7f << 24));
	}
	bwn_tab_write(mac, BWN_TAB_4(7, 0xc0 + offset), tmp);
	bwn_tab_write(mac, BWN_TAB_4(7, 0x140 + offset),
	    te.te_bbmult << 20 | te.te_dac << 28);
}

static void
bwn_phy_lp_gaintbl_write_r01(struct bwn_mac *mac, int offset,
    struct bwn_txgain_entry te)
{

	KASSERT(mac->mac_phy.rev < 2, ("%s:%d: fail", __func__, __LINE__));

	bwn_tab_write(mac, BWN_TAB_4(10, 0xc0 + offset),
	    (te.te_pad << 11) | (te.te_pga << 7) | (te.te_gm  << 4) |
	    te.te_dac);
	bwn_tab_write(mac, BWN_TAB_4(10, 0x140 + offset), te.te_bbmult << 20);
}
