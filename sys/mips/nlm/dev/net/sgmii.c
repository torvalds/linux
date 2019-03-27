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

void
nlm_configure_sgmii_interface(uint64_t nae_base, int block, int port,
    int mtu, int loopback)
{
	uint32_t data1, data2;

	/* Apply a soft reset */
	data1 = (0x1 << 31); /* soft reset */
	if (loopback)
		data1 |= (0x01 << 8);
	data1 |= (0x01 << 2); /* Rx enable */
	data1 |= 0x01; /* Tx enable */
	nlm_write_nae_reg(nae_base, NAE_REG(block, port, MAC_CONF1), data1);

	data2 = (0x7 << 12) | /* pre-amble length=7 */
	    (0x2 << 8) | /* byteMode */
	    0x1;         /* fullDuplex */
	nlm_write_nae_reg(nae_base, NAE_REG(block, port, MAC_CONF2), data2);

	/* Remove a soft reset */
	data1 &= ~(0x01 << 31);
	nlm_write_nae_reg(nae_base, NAE_REG(block, port, MAC_CONF1), data1);

	/* setup sgmii max frame length */
	nlm_write_nae_reg(nae_base, SGMII_MAX_FRAME(block, port), mtu);
}

void
nlm_sgmii_pcs_init(uint64_t nae_base, uint32_t cplx_mask)
{
	xlp_nae_config_lane_gmac(nae_base, cplx_mask);
}

void
nlm_nae_setup_mac(uint64_t nae_base, int nblock, int iface, int reset,
    int rx_en, int tx_en, int speed, int duplex)
{
	uint32_t mac_cfg1, mac_cfg2, netwk_inf;

	mac_cfg1 = nlm_read_nae_reg(nae_base,
	    SGMII_MAC_CONF1(nblock,iface));
	mac_cfg2 = nlm_read_nae_reg(nae_base,
	    SGMII_MAC_CONF2(nblock,iface));
	netwk_inf = nlm_read_nae_reg(nae_base,
	    SGMII_NET_IFACE_CTRL(nblock, iface));

	mac_cfg1 &= ~(0x1 << 31); /* remove reset */
	mac_cfg1 &= ~(0x1 << 2); /* remove rx */
	mac_cfg1 &= ~(0x1); /* remove tx */
	mac_cfg2 &= ~(0x3 << 8); /* remove interface mode bits */
	mac_cfg2 &= ~(0x1); /* remove duplex */
	netwk_inf &= ~(0x1 << 2); /* remove tx */
	netwk_inf &= ~(0x3); /* remove speed */

	switch (speed) {
	case NLM_SGMII_SPEED_10:
		netwk_inf |= 0x0; /* 2.5 Mhz clock for 10 Mbps */
		mac_cfg2  |= (0x1 << 8); /* enable 10/100 Mbps */
		break;
	case NLM_SGMII_SPEED_100:
		netwk_inf |= 0x1; /* 25 Mhz clock for 100 Mbps */
		mac_cfg2  |= (0x1 << 8); /* enable 10/100 Mbps */
		break;
	default: /* make it as 1G */
		netwk_inf |= 0x2; /* 125 Mhz clock for 1G */
		mac_cfg2  |= (0x2 << 8); /* enable 1G */
		break;
	}

	if (reset)
		mac_cfg1 |= (0x1 << 31); /* set reset */

	if (rx_en)
		mac_cfg1 |= (0x1 << 2); /* set rx */

        nlm_write_nae_reg(nae_base,
	    SGMII_NET_IFACE_CTRL(nblock, iface),
	    netwk_inf);

	if (tx_en) {
		mac_cfg1 |= 0x1; /* set tx */
		netwk_inf |= (0x1 << 2); /* set tx */
	}

	switch (duplex) {
	case NLM_SGMII_DUPLEX_HALF:
		/* duplexity is already set to half duplex */
		break;
	default:
		mac_cfg2 |= 0x1; /* set full duplex */
	}

	nlm_write_nae_reg(nae_base, SGMII_MAC_CONF1(nblock, iface), mac_cfg1);
	nlm_write_nae_reg(nae_base, SGMII_MAC_CONF2(nblock, iface), mac_cfg2);
	nlm_write_nae_reg(nae_base, SGMII_NET_IFACE_CTRL(nblock, iface),
	    netwk_inf);
}

void
nlm_nae_setup_rx_mode_sgmii(uint64_t base, int nblock, int iface, int port_type,
    int broadcast_en, int multicast_en, int pause_en, int promisc_en)
{
	uint32_t val;

	/* bit[17] of vlan_typefilter - allows packet matching in MAC.
	 * When DA filtering is disabled, this bit and bit[16] should
	 * be zero.
	 * bit[16] of vlan_typefilter - Allows hash matching to be used
	 * for DA filtering. When DA filtering is disabled, this bit and
	 * bit[17] should be zero.
	 * Both bits have to be set only if you want to turn on both
	 * features / modes.
	 */
	if (promisc_en == 1) {
		val = nlm_read_nae_reg(base,
		    SGMII_NETIOR_VLANTYPE_FILTER(nblock, iface));
		val &= (~(0x3 << 16));
		nlm_write_nae_reg(base,
		    SGMII_NETIOR_VLANTYPE_FILTER(nblock, iface), val);
	} else {
		val = nlm_read_nae_reg(base,
		    SGMII_NETIOR_VLANTYPE_FILTER(nblock, iface));
		val |= (0x1 << 17);
		nlm_write_nae_reg(base,
		    SGMII_NETIOR_VLANTYPE_FILTER(nblock, iface), val);
	}

	val = ((broadcast_en & 0x1) << 10)  |
	    ((pause_en & 0x1) << 9)     |
	    ((multicast_en & 0x1) << 8) |
	    ((promisc_en & 0x1) << 7)   | /* unicast_enable - enables promisc mode */
	    1; /* MAC address is always valid */

	nlm_write_nae_reg(base, SGMII_MAC_FILTER_CONFIG(nblock, iface), val);

}

void
nlm_nae_setup_mac_addr_sgmii(uint64_t base, int nblock, int iface,
    int port_type, uint8_t *mac_addr)
{
	nlm_write_nae_reg(base,
	    SGMII_MAC_ADDR0_LO(nblock, iface),
	    (mac_addr[5] << 24) |
	    (mac_addr[4] << 16) |
	    (mac_addr[3] << 8)  |
	    mac_addr[2]);

	nlm_write_nae_reg(base,
	    SGMII_MAC_ADDR0_HI(nblock, iface),
	    (mac_addr[1] << 24) |
	    (mac_addr[0] << 16));

	nlm_write_nae_reg(base,
	    SGMII_MAC_ADDR_MASK0_LO(nblock, iface),
	    0xffffffff);
	nlm_write_nae_reg(base,
	    SGMII_MAC_ADDR_MASK0_HI(nblock, iface),
	    0xffffffff);

	nlm_nae_setup_rx_mode_sgmii(base, nblock, iface,
	    SGMIIC,
	    1, /* broadcast enabled */
	    1, /* multicast enabled */
	    0, /* do not accept pause frames */
	    0 /* promisc mode disabled */
	    );
}
