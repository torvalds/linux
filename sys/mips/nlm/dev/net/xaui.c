/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
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
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/types.h>
#include <sys/systm.h>

#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/sys.h>
#include <mips/nlm/hal/nae.h>
#include <mips/nlm/hal/mdio.h>
#include <mips/nlm/hal/sgmii.h>
#include <mips/nlm/hal/xaui.h>

#include <mips/nlm/xlp.h>
void
nlm_xaui_pcs_init(uint64_t nae_base, int xaui_cplx_mask)
{
	int block, lane_ctrl, reg;
	int cplx_lane_enable;
	int lane_enable = 0;
	uint32_t regval;

	cplx_lane_enable = LM_XAUI |
	    (LM_XAUI << 4) |
	    (LM_XAUI << 8) |
	    (LM_XAUI << 12);

	if (xaui_cplx_mask == 0)
		return;

	/* write 0x2 to enable SGMII for all lane */
	block = 7;

	if (xaui_cplx_mask & 0x3) { /* Complexes 0, 1 */
		lane_enable = nlm_read_nae_reg(nae_base,
		    NAE_REG(block, LANE_CFG, LANE_CFG_CPLX_0_1));
		if (xaui_cplx_mask & 0x1) { /* Complex 0 */
			lane_enable &= ~(0xFFFF);
			lane_enable |= cplx_lane_enable;
		}
		if (xaui_cplx_mask & 0x2) { /* Complex 1 */
			lane_enable &= ~(0xFFFF<<16);
			lane_enable |= (cplx_lane_enable << 16);
		}
		nlm_write_nae_reg(nae_base,
		    NAE_REG(block, LANE_CFG, LANE_CFG_CPLX_0_1),
		    lane_enable);
	}
	lane_enable = 0;
	if (xaui_cplx_mask & 0xc) { /* Complexes 2, 3 */
		lane_enable = nlm_read_nae_reg(nae_base,
		    NAE_REG(block, LANE_CFG, LANE_CFG_CPLX_2_3));
		if (xaui_cplx_mask & 0x4) { /* Complex 2 */
			lane_enable &= ~(0xFFFF);
			lane_enable |= cplx_lane_enable;
		}
		if (xaui_cplx_mask & 0x8) { /* Complex 3 */
			lane_enable &= ~(0xFFFF<<16);
			lane_enable |= (cplx_lane_enable << 16);
		}
		nlm_write_nae_reg(nae_base,
		    NAE_REG(block, LANE_CFG, LANE_CFG_CPLX_2_3),
		    lane_enable);
	}

	/* Bring txpll out of reset */
	for (block = 0; block < 4; block++) {
		if ((xaui_cplx_mask & (1 << block)) == 0)
			continue;

		for (lane_ctrl = PHY_LANE_0_CTRL;
		    lane_ctrl <= PHY_LANE_3_CTRL; lane_ctrl++) {
			if (!nlm_is_xlp8xx_ax())
				xlp_nae_lane_reset_txpll(nae_base,
				    block, lane_ctrl, PHYMODE_XAUI);
			else
				xlp_ax_nae_lane_reset_txpll(nae_base, block,
				    lane_ctrl, PHYMODE_XAUI);
		}
	}

	/* Wait for Rx & TX clock stable */
	for (block = 0; block < 4; block++) {
		if ((xaui_cplx_mask & (1 << block)) == 0)
			continue;

		for (lane_ctrl = PHY_LANE_0_CTRL;
		    lane_ctrl <= PHY_LANE_3_CTRL; lane_ctrl++) {

			reg = NAE_REG(block, PHY, lane_ctrl - 4);
			/* Wait for TX clock to be set */
			do {
				regval = nlm_read_nae_reg(nae_base, reg);
			} while ((regval & LANE_TX_CLK) == 0);

			/* Wait for RX clock to be set */
			do {
				regval = nlm_read_nae_reg(nae_base, reg);
			} while ((regval & LANE_RX_CLK) == 0);

			/* Wait for XAUI Lane fault to be cleared */
			do {
				regval = nlm_read_nae_reg(nae_base, reg);
			} while ((regval & XAUI_LANE_FAULT) != 0);
		}
	}
}

void
nlm_nae_setup_rx_mode_xaui(uint64_t base, int nblock, int iface, int port_type,
    int broadcast_en, int multicast_en, int pause_en, int promisc_en)
{
	uint32_t val;

	val = ((broadcast_en & 0x1) << 10)  |
	    ((pause_en & 0x1) << 9)     |
	    ((multicast_en & 0x1) << 8) |
	    ((promisc_en & 0x1) << 7)   | /* unicast_enable - enables promisc mode */
	    1; /* MAC address is always valid */

	nlm_write_nae_reg(base, XAUI_MAC_FILTER_CFG(nblock), val);
}

void
nlm_nae_setup_mac_addr_xaui(uint64_t base, int nblock, int iface,
    int port_type, unsigned char *mac_addr)
{
	nlm_write_nae_reg(base,
	    XAUI_MAC_ADDR0_LO(nblock),
	    (mac_addr[5] << 24) |
	    (mac_addr[4] << 16) |
	    (mac_addr[3] << 8)  |
	    mac_addr[2]);

	nlm_write_nae_reg(base,
	    XAUI_MAC_ADDR0_HI(nblock),
	    (mac_addr[1] << 24) |
	    (mac_addr[0] << 16));

	nlm_write_nae_reg(base,
	    XAUI_MAC_ADDR_MASK0_LO(nblock),
	    0xffffffff);
	nlm_write_nae_reg(base,
	    XAUI_MAC_ADDR_MASK0_HI(nblock),
	    0xffffffff);

	nlm_nae_setup_rx_mode_xaui(base, nblock, iface,
	    XAUIC,
	    1, /* broadcast enabled */
	    1, /* multicast enabled */
	    0, /* do not accept pause frames */
	    0 /* promisc mode disabled */
	    );
}

void
nlm_config_xaui_mtu(uint64_t nae_base, int nblock,
    int max_tx_frame_sz, int max_rx_frame_sz)
{
	uint32_t tx_words = max_tx_frame_sz >> 2; /* max_tx_frame_sz / 4 */

	/* write max frame length */
	nlm_write_nae_reg(nae_base,
	    XAUI_MAX_FRAME_LEN(nblock),
	    ((tx_words & 0x3ff) << 16) | (max_rx_frame_sz & 0xffff));
}

void
nlm_config_xaui(uint64_t nae_base, int nblock,
    int max_tx_frame_sz, int max_rx_frame_sz, int vlan_pri_en)
{
	uint32_t val;

	val = nlm_read_nae_reg(nae_base, XAUI_NETIOR_XGMAC_CTRL1(nblock));
	val &= ~(0x1 << 11);	/* clear soft reset */
	nlm_write_nae_reg(nae_base, XAUI_NETIOR_XGMAC_CTRL1(nblock), val);

	val = nlm_read_nae_reg(nae_base, XAUI_NETIOR_XGMAC_CTRL1(nblock));
	val &= ~(0x3 << 11);	/* clear soft reset and hard reset */
	nlm_write_nae_reg(nae_base, XAUI_NETIOR_XGMAC_CTRL1(nblock), val);
	nlm_write_nae_reg(nae_base, XAUI_CONFIG0(nblock), 0xffffffff);
	nlm_write_nae_reg(nae_base, XAUI_CONFIG0(nblock), 0);

	/* Enable tx/rx frame */
	val = 0x000010A8;
	val |= XAUI_CONFIG_LENCHK;
	val |= XAUI_CONFIG_GENFCS;
	val |= XAUI_CONFIG_PAD_64;
	nlm_write_nae_reg(nae_base, XAUI_CONFIG1(nblock), val);

	/* write max frame length */
	nlm_config_xaui_mtu(nae_base, nblock, max_tx_frame_sz,
	    max_rx_frame_sz);

	/* set stats counter */
	val = nlm_read_nae_reg(nae_base, XAUI_NETIOR_XGMAC_CTRL1(nblock));
	val |= (0x1 << NETIOR_XGMAC_VLAN_DC_POS);
	val |= (0x1 << NETIOR_XGMAC_STATS_EN_POS);
	if (vlan_pri_en) {
		val |= (0x1 << NETIOR_XGMAC_TX_PFC_EN_POS);
		val |= (0x1 << NETIOR_XGMAC_RX_PFC_EN_POS);
		val |= (0x1 << NETIOR_XGMAC_TX_PAUSE_POS);
	} else {
		val &= ~(0x1 << NETIOR_XGMAC_TX_PFC_EN_POS);
		val |= (0x1 << NETIOR_XGMAC_TX_PAUSE_POS);
	}
	nlm_write_nae_reg(nae_base, XAUI_NETIOR_XGMAC_CTRL1(nblock), val);
	/* configure on / off timer */
	if (vlan_pri_en)
		val = 0xF1230000; /* PFC mode, offtimer = 0xf123, ontimer = 0 */
	else
		val = 0x0000F123; /* link level FC mode, offtimer = 0xf123 */
	nlm_write_nae_reg(nae_base, XAUI_NETIOR_XGMAC_CTRL2(nblock), val);

	/* set xaui tx threshold */
	val = nlm_read_nae_reg(nae_base, XAUI_NETIOR_XGMAC_CTRL3(nblock));
	val &= ~(0x1f << 10);
	val |= ~(15 << 10);
	nlm_write_nae_reg(nae_base, XAUI_NETIOR_XGMAC_CTRL3(nblock), val);
}
