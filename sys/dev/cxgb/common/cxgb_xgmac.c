/**************************************************************************
SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2007-2009 Chelsio Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Chelsio Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

***************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <cxgb_include.h>

#undef msleep
#define msleep t3_os_sleep


static inline int macidx(const struct cmac *mac)
{
	return mac->offset / (XGMAC0_1_BASE_ADDR - XGMAC0_0_BASE_ADDR);
}

/*
 * Returns a reasonable A_XGM_RESET_CTRL value for the mac specified.
 */
static inline int xgm_reset_ctrl(const struct cmac *mac)
{
	adapter_t *adap = mac->adapter;
	int val = F_MAC_RESET_ | F_XGMAC_STOP_EN;

	if (is_10G(adap)) {
		int cfg = t3_read_reg(adap, A_XGM_PORT_CFG + mac->offset);

		val |= F_PCS_RESET_;
		if (G_PORTSPEED(cfg) != 3)	/* not running at 10G */
			val |= F_XG2G_RESET_;
	} else if (uses_xaui(adap))
		val |= F_PCS_RESET_ | F_XG2G_RESET_;
	else
		val |= F_RGMII_RESET_ | F_XG2G_RESET_;

	return (val);
}

static void xaui_serdes_reset(struct cmac *mac)
{
	static const unsigned int clear[] = {
		F_PWRDN0 | F_PWRDN1,    F_RESETPLL01,    F_RESET0 | F_RESET1,
	     	F_PWRDN2 | F_PWRDN3,    F_RESETPLL23,    F_RESET2 | F_RESET3
	};

	int i;
	adapter_t *adap = mac->adapter;
	u32 ctrl = A_XGM_SERDES_CTRL0 + mac->offset;

	t3_write_reg(adap, ctrl, adap->params.vpd.xauicfg[macidx(mac)] |
		     F_RESET3 | F_RESET2 | F_RESET1 | F_RESET0 |
		     F_PWRDN3 | F_PWRDN2 | F_PWRDN1 | F_PWRDN0 |
		     F_RESETPLL23 | F_RESETPLL01);
	(void)t3_read_reg(adap, ctrl);
	udelay(15);

	for (i = 0; i < ARRAY_SIZE(clear); i++) {
		t3_set_reg_field(adap, ctrl, clear[i], 0);
		udelay(15);
	}
}

/**
 *	t3b_pcs_reset - reset the PCS on T3B+ adapters
 *	@mac: the XGMAC handle
 *
 *	Reset the XGMAC PCS block on T3B+ adapters.
 */
void t3b_pcs_reset(struct cmac *mac)
{
	t3_set_reg_field(mac->adapter, A_XGM_RESET_CTRL + mac->offset,
			 F_PCS_RESET_, 0);

	/* No delay required */

	t3_set_reg_field(mac->adapter, A_XGM_RESET_CTRL + mac->offset, 0,
			 F_PCS_RESET_);
}

void t3c_pcs_force_los(struct cmac *mac)
{
	t3_set_reg_field(mac->adapter, A_XGM_SERDES_STAT0 + mac->offset,
	    F_LOWSIGFORCEEN0 | F_LOWSIGFORCEVALUE0,
	    F_LOWSIGFORCEEN0 | F_LOWSIGFORCEVALUE0);
	t3_set_reg_field(mac->adapter, A_XGM_SERDES_STAT1 + mac->offset,
	    F_LOWSIGFORCEEN1 | F_LOWSIGFORCEVALUE1,
	    F_LOWSIGFORCEEN1 | F_LOWSIGFORCEVALUE1);
	t3_set_reg_field(mac->adapter, A_XGM_SERDES_STAT2 + mac->offset,
	    F_LOWSIGFORCEEN2 | F_LOWSIGFORCEVALUE2,
	    F_LOWSIGFORCEEN2 | F_LOWSIGFORCEVALUE2);
	t3_set_reg_field(mac->adapter, A_XGM_SERDES_STAT3 + mac->offset,
	    F_LOWSIGFORCEEN3 | F_LOWSIGFORCEVALUE3,
	    F_LOWSIGFORCEEN3 | F_LOWSIGFORCEVALUE3);

	/* No delay required */

	t3_set_reg_field(mac->adapter, A_XGM_SERDES_STAT0 + mac->offset,
	    F_LOWSIGFORCEEN0, 0);
	t3_set_reg_field(mac->adapter, A_XGM_SERDES_STAT1 + mac->offset,
	    F_LOWSIGFORCEEN1, 0);
	t3_set_reg_field(mac->adapter, A_XGM_SERDES_STAT2 + mac->offset,
	    F_LOWSIGFORCEEN2, 0);
	t3_set_reg_field(mac->adapter, A_XGM_SERDES_STAT3 + mac->offset,
	    F_LOWSIGFORCEEN3, 0);
}

/**
 *	t3_mac_init - initialize a MAC
 *	@mac: the MAC to initialize
 *
 *	Initialize the given MAC.
 */
int t3_mac_init(struct cmac *mac)
{
	static struct addr_val_pair mac_reset_avp[] = {
		{ A_XGM_TX_CTRL, 0 },
		{ A_XGM_RX_CTRL, 0 },
		{ A_XGM_RX_CFG, F_DISPAUSEFRAMES | F_EN1536BFRAMES |
		                F_RMFCS | F_ENJUMBO | F_ENHASHMCAST },
		{ A_XGM_RX_HASH_LOW, 0 },
		{ A_XGM_RX_HASH_HIGH, 0 },
		{ A_XGM_RX_EXACT_MATCH_LOW_1, 0 },
		{ A_XGM_RX_EXACT_MATCH_LOW_2, 0 },
		{ A_XGM_RX_EXACT_MATCH_LOW_3, 0 },
		{ A_XGM_RX_EXACT_MATCH_LOW_4, 0 },
		{ A_XGM_RX_EXACT_MATCH_LOW_5, 0 },
		{ A_XGM_RX_EXACT_MATCH_LOW_6, 0 },
		{ A_XGM_RX_EXACT_MATCH_LOW_7, 0 },
		{ A_XGM_RX_EXACT_MATCH_LOW_8, 0 },
		{ A_XGM_STAT_CTRL, F_CLRSTATS }
	};
	u32 val;
	adapter_t *adap = mac->adapter;
	unsigned int oft = mac->offset;

	t3_write_reg(adap, A_XGM_RESET_CTRL + oft, F_MAC_RESET_);
	(void) t3_read_reg(adap, A_XGM_RESET_CTRL + oft);    /* flush */

	t3_write_regs(adap, mac_reset_avp, ARRAY_SIZE(mac_reset_avp), oft);
	t3_set_reg_field(adap, A_XGM_RXFIFO_CFG + oft,
			 F_RXSTRFRWRD | F_DISERRFRAMES,
			 uses_xaui(adap) ? 0 : F_RXSTRFRWRD);
	t3_set_reg_field(adap, A_XGM_TXFIFO_CFG + oft, 0, F_UNDERUNFIX);

	if (uses_xaui(adap)) {
		if (adap->params.rev == 0) {
			t3_set_reg_field(adap, A_XGM_SERDES_CTRL + oft, 0,
					 F_RXENABLE | F_TXENABLE);
			if (t3_wait_op_done(adap, A_XGM_SERDES_STATUS1 + oft,
					    F_CMULOCK, 1, 5, 2)) {
				CH_ERR(adap,
				       "MAC %d XAUI SERDES CMU lock failed\n",
				       macidx(mac));
				return -1;
			}
			t3_set_reg_field(adap, A_XGM_SERDES_CTRL + oft, 0,
					 F_SERDESRESET_);
		} else
			xaui_serdes_reset(mac);
	}


	if (mac->multiport) {
		t3_write_reg(adap, A_XGM_RX_MAX_PKT_SIZE + oft,
			     V_RXMAXPKTSIZE(MAX_FRAME_SIZE - 4));
		t3_set_reg_field(adap, A_XGM_TXFIFO_CFG + oft, 0,
				 F_DISPREAMBLE);
		t3_set_reg_field(adap, A_XGM_RX_CFG + oft, 0, F_COPYPREAMBLE |
				 F_ENNON802_3PREAMBLE);
		t3_set_reg_field(adap, A_XGM_TXFIFO_CFG + oft,
				 V_TXFIFOTHRESH(M_TXFIFOTHRESH),
				 V_TXFIFOTHRESH(64));
		t3_write_reg(adap, A_XGM_TX_CTRL + oft, F_TXEN);
		t3_write_reg(adap, A_XGM_RX_CTRL + oft, F_RXEN);
	}

	t3_set_reg_field(adap, A_XGM_RX_MAX_PKT_SIZE + oft,
			 V_RXMAXFRAMERSIZE(M_RXMAXFRAMERSIZE),
			 V_RXMAXFRAMERSIZE(MAX_FRAME_SIZE) | F_RXENFRAMER);

	val = xgm_reset_ctrl(mac);
	t3_write_reg(adap, A_XGM_RESET_CTRL + oft, val);
	(void) t3_read_reg(adap, A_XGM_RESET_CTRL + oft);  /* flush */
	if ((val & F_PCS_RESET_) && adap->params.rev) {
		msleep(1);
		t3b_pcs_reset(mac);
	}

	memset(&mac->stats, 0, sizeof(mac->stats));
	return 0;
}

static int t3_mac_reset(struct cmac *mac, int portspeed)
{
	u32 val, store_mps;
	adapter_t *adap = mac->adapter;
	unsigned int oft = mac->offset;
	int idx = macidx(mac);
	unsigned int store;

	/* Stop egress traffic to xgm*/
	store_mps = t3_read_reg(adap, A_MPS_CFG);
	if (!idx)
		t3_set_reg_field(adap, A_MPS_CFG, F_PORT0ACTIVE, 0);
	else
		t3_set_reg_field(adap, A_MPS_CFG, F_PORT1ACTIVE, 0);

	/* This will reduce the number of TXTOGGLES */
	/* Clear: to stop the NIC traffic */
	t3_set_reg_field(adap, A_MPS_CFG, F_ENFORCEPKT, 0);
	/* Ensure TX drains */
	t3_set_reg_field(adap, A_XGM_TX_CFG + oft, F_TXPAUSEEN, 0);

	/* PCS in reset */
	t3_write_reg(adap, A_XGM_RESET_CTRL + oft, F_MAC_RESET_);
	(void) t3_read_reg(adap, A_XGM_RESET_CTRL + oft);    /* flush */

	/* Store A_TP_TX_DROP_CFG_CH0 */
	t3_write_reg(adap, A_TP_PIO_ADDR, A_TP_TX_DROP_CFG_CH0 + idx);
	store = t3_read_reg(adap, A_TP_PIO_DATA);

	msleep(10);

	/* Change DROP_CFG to 0xc0000011 */
	t3_write_reg(adap, A_TP_PIO_ADDR, A_TP_TX_DROP_CFG_CH0 + idx);
	t3_write_reg(adap, A_TP_PIO_DATA, 0xc0000011);

	/* Check for xgm Rx fifo empty */
	/* Increased loop count to 1000 from 5 cover 1G and 100Mbps case */
	if (t3_wait_op_done(adap, A_XGM_RX_MAX_PKT_SIZE_ERR_CNT + oft,
			    0x80000000, 1, 1000, 2) && portspeed < 0) {
		CH_ERR(adap, "MAC %d Rx fifo drain failed\n", idx);
		return -1;
	}

	if (portspeed >= 0) {
		u32 intr = t3_read_reg(adap, A_XGM_INT_ENABLE + oft);

		/*
		 * safespeedchange: wipes out pretty much all XGMAC registers.
		 */

		t3_set_reg_field(adap, A_XGM_PORT_CFG + oft,
		    V_PORTSPEED(M_PORTSPEED) | F_SAFESPEEDCHANGE,
		    portspeed | F_SAFESPEEDCHANGE);
		(void) t3_read_reg(adap, A_XGM_PORT_CFG + oft);
		t3_set_reg_field(adap, A_XGM_PORT_CFG + oft,
		    F_SAFESPEEDCHANGE, 0);
		(void) t3_read_reg(adap, A_XGM_PORT_CFG + oft);
		t3_mac_init(mac);
		
		t3_write_reg(adap, A_XGM_INT_ENABLE + oft, intr);
	} else {

		t3_write_reg(adap, A_XGM_RESET_CTRL + oft, 0); /*MAC in reset*/
		(void) t3_read_reg(adap, A_XGM_RESET_CTRL + oft);    /* flush */

		val = xgm_reset_ctrl(mac);
		t3_write_reg(adap, A_XGM_RESET_CTRL + oft, val);
		(void) t3_read_reg(adap, A_XGM_RESET_CTRL + oft);  /* flush */
		if ((val & F_PCS_RESET_) && adap->params.rev) {
			msleep(1);
			t3b_pcs_reset(mac);
		}
		t3_write_reg(adap, A_XGM_RX_CFG + oft,
			 F_DISPAUSEFRAMES | F_EN1536BFRAMES |
					F_RMFCS | F_ENJUMBO | F_ENHASHMCAST );
	}

	/* Restore the DROP_CFG */
	t3_write_reg(adap, A_TP_PIO_ADDR, A_TP_TX_DROP_CFG_CH0 + idx);
	t3_write_reg(adap, A_TP_PIO_DATA, store);

	/* Resume egress traffic to xgm */
	t3_set_reg_field(adap, A_MPS_CFG, F_PORT1ACTIVE | F_PORT0ACTIVE,
			 store_mps);

	/*  Set: re-enable NIC traffic */
	t3_set_reg_field(adap, A_MPS_CFG, F_ENFORCEPKT, F_ENFORCEPKT);

	return 0;
}

/*
 * Set the exact match register 'idx' to recognize the given Ethernet address.
 */
static void set_addr_filter(struct cmac *mac, int idx, const u8 *addr)
{
	u32 addr_lo, addr_hi;
	unsigned int oft = mac->offset + idx * 8;

	addr_lo = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	addr_hi = (addr[5] << 8) | addr[4];

	t3_write_reg(mac->adapter, A_XGM_RX_EXACT_MATCH_LOW_1 + oft, addr_lo);
	t3_write_reg(mac->adapter, A_XGM_RX_EXACT_MATCH_HIGH_1 + oft, addr_hi);
}

/**
 *	t3_mac_set_address - set one of the station's unicast MAC addresses
 *	@mac: the MAC handle
 *	@idx: index of the exact address match filter to use
 *	@addr: the Ethernet address
 *
 *	Set one of the station's unicast MAC addresses.
 */
int t3_mac_set_address(struct cmac *mac, unsigned int idx, u8 addr[6])
{
	if (mac->multiport)
		idx = mac->ext_port + idx * mac->adapter->params.nports;
	if (idx >= mac->nucast)
		return -EINVAL;
	set_addr_filter(mac, idx, addr);
	if (mac->multiport && idx < mac->adapter->params.nports)
		t3_vsc7323_set_addr(mac->adapter, addr, idx);
	return 0;
}

/**
 *	t3_mac_set_num_ucast - set the number of unicast addresses needed
 *	@mac: the MAC handle
 *	@n: number of unicast addresses needed
 *
 *	Specify the number of exact address filters that should be reserved for
 *	unicast addresses.  Caller should reload the unicast and multicast
 *	addresses after calling this.
 *
 *	Generally, this is 1 with the first one used for the station address,
 *	and the rest are available for multicast addresses.
 */
int t3_mac_set_num_ucast(struct cmac *mac, unsigned char n)
{
	if (n > EXACT_ADDR_FILTERS)
		return -EINVAL;
	mac->nucast = n;
	return 0;
}

void t3_mac_disable_exact_filters(struct cmac *mac)
{
	unsigned int i, reg = mac->offset + A_XGM_RX_EXACT_MATCH_LOW_1;

	for (i = 0; i < EXACT_ADDR_FILTERS; i++, reg += 8) {
		u32 v = t3_read_reg(mac->adapter, reg);
		t3_write_reg(mac->adapter, reg, v);
	}
	t3_read_reg(mac->adapter, A_XGM_RX_EXACT_MATCH_LOW_1); /* flush */
}

void t3_mac_enable_exact_filters(struct cmac *mac)
{
	unsigned int i, reg = mac->offset + A_XGM_RX_EXACT_MATCH_HIGH_1;

	for (i = 0; i < EXACT_ADDR_FILTERS; i++, reg += 8) {
		u32 v = t3_read_reg(mac->adapter, reg);
		t3_write_reg(mac->adapter, reg, v);
	}
	t3_read_reg(mac->adapter, A_XGM_RX_EXACT_MATCH_LOW_1); /* flush */
}

/* Calculate the RX hash filter index of an Ethernet address */
static int hash_hw_addr(const u8 *addr)
{
	int hash = 0, octet, bit, i = 0, c;

	for (octet = 0; octet < 6; ++octet)
		for (c = addr[octet], bit = 0; bit < 8; c >>= 1, ++bit) {
			hash ^= (c & 1) << i;
			if (++i == 6)
				i = 0;
		}
	return hash;
}

/**
 *	t3_mac_set_rx_mode - set the Rx mode and address filters
 *	@mac: the MAC to configure
 *	@rm: structure containing the Rx mode and MAC addresses needed
 *
 *	Configures the MAC Rx mode (promiscuity, etc) and exact and hash
 *	address filters.
 */
int t3_mac_set_rx_mode(struct cmac *mac, struct t3_rx_mode *rm)
{
	u32 hash_lo, hash_hi;
	adapter_t *adap = mac->adapter;
	unsigned int oft = mac->offset;

	if (promisc_rx_mode(rm))
		mac->promisc_map |= 1 << mac->ext_port;
	else
		mac->promisc_map &= ~(1 << mac->ext_port);
	t3_set_reg_field(adap, A_XGM_RX_CFG + oft, F_COPYALLFRAMES,
			 mac->promisc_map ? F_COPYALLFRAMES : 0);

	if (allmulti_rx_mode(rm) || mac->multiport)
		hash_lo = hash_hi = 0xffffffff;
	else {
		u8 *addr;
		int exact_addr_idx = mac->nucast;

		hash_lo = hash_hi = 0;
		while ((addr = t3_get_next_mcaddr(rm)))
			if (exact_addr_idx < EXACT_ADDR_FILTERS)
				set_addr_filter(mac, exact_addr_idx++, addr);
			else {
				int hash = hash_hw_addr(addr);

				if (hash < 32)
					hash_lo |= (1 << hash);
				else
					hash_hi |= (1 << (hash - 32));
			}
	}

	t3_write_reg(adap, A_XGM_RX_HASH_LOW + oft, hash_lo);
	t3_write_reg(adap, A_XGM_RX_HASH_HIGH + oft, hash_hi);
	return 0;
}

static int rx_fifo_hwm(int mtu)
{
	int hwm;

	hwm = max(MAC_RXFIFO_SIZE - 3 * mtu, (MAC_RXFIFO_SIZE * 38) / 100);
	return min(hwm, MAC_RXFIFO_SIZE - 8192);
}

/**
 *	t3_mac_set_mtu - set the MAC MTU
 *	@mac: the MAC to configure
 *	@mtu: the MTU
 *
 *	Sets the MAC MTU and adjusts the FIFO PAUSE watermarks accordingly.
 */
int t3_mac_set_mtu(struct cmac *mac, unsigned int mtu)
{
	int hwm, lwm;
	int ipg;
	unsigned int thres, v, reg;
	adapter_t *adap = mac->adapter;
	unsigned port_type = adap->params.vpd.port_type[macidx(mac)];
	unsigned int orig_mtu=mtu;

	/*
	 * MAX_FRAME_SIZE inludes header + FCS, mtu doesn't.  The HW max
	 * packet size register includes header, but not FCS.
	 */
	mtu += 14;
	if (mac->multiport)
		mtu += 8;                             /* for preamble */
	if (mtu > MAX_FRAME_SIZE - 4)
		return -EINVAL;
	if (mac->multiport)
		return t3_vsc7323_set_mtu(adap, mtu - 4, mac->ext_port);

	/* Modify the TX and RX fifo depth only if the card has a vsc8211 phy */
	if (port_type == 2) {
		int err = t3_vsc8211_fifo_depth(adap,orig_mtu,macidx(mac));

		if (err)
			return err;
	}

	if (adap->params.rev >= T3_REV_B2 &&
	    (t3_read_reg(adap, A_XGM_RX_CTRL + mac->offset) & F_RXEN)) {
		t3_mac_disable_exact_filters(mac);
		v = t3_read_reg(adap, A_XGM_RX_CFG + mac->offset);
		t3_set_reg_field(adap, A_XGM_RX_CFG + mac->offset,
				 F_ENHASHMCAST | F_COPYALLFRAMES, F_DISBCAST);

		reg = adap->params.rev == T3_REV_B2 ?
			A_XGM_RX_MAX_PKT_SIZE_ERR_CNT : A_XGM_RXFIFO_CFG;

		/* drain RX FIFO */
		if (t3_wait_op_done(adap, reg + mac->offset,
				    F_RXFIFO_EMPTY, 1, 20, 5)) {
			t3_write_reg(adap, A_XGM_RX_CFG + mac->offset, v);
			t3_mac_enable_exact_filters(mac);
			return -EIO;
		}
		t3_set_reg_field(adap, A_XGM_RX_MAX_PKT_SIZE + mac->offset,
				 V_RXMAXPKTSIZE(M_RXMAXPKTSIZE),
				 V_RXMAXPKTSIZE(mtu));
		t3_write_reg(adap, A_XGM_RX_CFG + mac->offset, v);
		t3_mac_enable_exact_filters(mac);
	} else
		t3_set_reg_field(adap, A_XGM_RX_MAX_PKT_SIZE + mac->offset,
				 V_RXMAXPKTSIZE(M_RXMAXPKTSIZE),
				 V_RXMAXPKTSIZE(mtu));
	/*
	 * Adjust the PAUSE frame watermarks.  We always set the LWM, and the
	 * HWM only if flow-control is enabled.
	 */
	hwm = rx_fifo_hwm(mtu);
	lwm = min(3 * (int) mtu, MAC_RXFIFO_SIZE /4);
	v = t3_read_reg(adap, A_XGM_RXFIFO_CFG + mac->offset);
	v &= ~V_RXFIFOPAUSELWM(M_RXFIFOPAUSELWM);
	v |= V_RXFIFOPAUSELWM(lwm / 8);
	if (G_RXFIFOPAUSEHWM(v))
		v = (v & ~V_RXFIFOPAUSEHWM(M_RXFIFOPAUSEHWM)) |
		    V_RXFIFOPAUSEHWM(hwm / 8);

	t3_write_reg(adap, A_XGM_RXFIFO_CFG + mac->offset, v);

	/* Adjust the TX FIFO threshold based on the MTU */
	thres = (adap->params.vpd.cclk * 1000) / 15625;
	thres = (thres * mtu) / 1000;
	if (is_10G(adap))
		thres /= 10;
	thres = mtu > thres ? (mtu - thres + 7) / 8 : 0;
	thres = max(thres, 8U);                          /* need at least 8 */
	ipg = (port_type == 9 || adap->params.rev != T3_REV_C) ? 1 : 0;
	t3_set_reg_field(adap, A_XGM_TXFIFO_CFG + mac->offset,
			 V_TXFIFOTHRESH(M_TXFIFOTHRESH) | V_TXIPG(M_TXIPG),
			 V_TXFIFOTHRESH(thres) | V_TXIPG(ipg));
	return 0;
}

/**
 *	t3_mac_set_speed_duplex_fc - set MAC speed, duplex and flow control
 *	@mac: the MAC to configure
 *	@speed: the desired speed (10/100/1000/10000)
 *	@duplex: the desired duplex
 *	@fc: desired Tx/Rx PAUSE configuration
 *
 *	Set the MAC speed, duplex (actually only full-duplex is supported), and
 *	flow control.  If a parameter value is negative the corresponding
 *	MAC setting is left at its current value.
 */
int t3_mac_set_speed_duplex_fc(struct cmac *mac, int speed, int duplex, int fc)
{
	u32 val;
	adapter_t *adap = mac->adapter;
	unsigned int oft = mac->offset;
	unsigned int pause_bits;

	if (duplex >= 0 && duplex != DUPLEX_FULL)
		return -EINVAL;

	pause_bits = MAC_RXFIFO_SIZE * 4 * 8;
	t3_write_reg(adap, A_XGM_TX_PAUSE_QUANTA + mac->offset,
		     pause_bits / 512);
	t3_write_reg(adap, A_XGM_PAUSE_TIMER + mac->offset,
		     (pause_bits >> (adap->params.rev == T3_REV_C ? 10 : 7)));

	if (mac->multiport) {
		u32 rx_max_pkt_size =
		    G_RXMAXPKTSIZE(t3_read_reg(adap,
					       A_XGM_RX_MAX_PKT_SIZE + oft));
		val = t3_read_reg(adap, A_XGM_RXFIFO_CFG + oft);
		val &= ~V_RXFIFOPAUSEHWM(M_RXFIFOPAUSEHWM);
		val |= V_RXFIFOPAUSEHWM(rx_fifo_hwm(rx_max_pkt_size) / 8);
		t3_write_reg(adap, A_XGM_RXFIFO_CFG + oft, val);
		t3_set_reg_field(adap, A_XGM_TX_CFG + oft, F_TXPAUSEEN,
			  		F_TXPAUSEEN);

		return t3_vsc7323_set_speed_fc(adap, speed, fc, mac->ext_port);
	}
	if (speed >= 0) {
		if (speed == SPEED_10)
			val = V_PORTSPEED(0);
		else if (speed == SPEED_100)
			val = V_PORTSPEED(1);
		else if (speed == SPEED_1000)
			val = V_PORTSPEED(2);
		else if (speed == SPEED_10000)
			val = V_PORTSPEED(3);
		else
			return -EINVAL;

		if (!uses_xaui(adap)) /* T302 */
			t3_set_reg_field(adap, A_XGM_PORT_CFG + oft,
			    V_PORTSPEED(M_PORTSPEED), val);
		else {
			u32 old = t3_read_reg(adap, A_XGM_PORT_CFG + oft);

			if ((old & V_PORTSPEED(M_PORTSPEED)) != val) {
				t3_mac_reset(mac, val);
				mac->was_reset = 1;
			}
		}
	}

	val = t3_read_reg(adap, A_XGM_RXFIFO_CFG + oft);
	val &= ~V_RXFIFOPAUSEHWM(M_RXFIFOPAUSEHWM);
	if (fc & PAUSE_TX) {
		u32 rx_max_pkt_size =
		    G_RXMAXPKTSIZE(t3_read_reg(adap,
					       A_XGM_RX_MAX_PKT_SIZE + oft));
		val |= V_RXFIFOPAUSEHWM(rx_fifo_hwm(rx_max_pkt_size) / 8);
	}
	t3_write_reg(adap, A_XGM_RXFIFO_CFG + oft, val);

	t3_set_reg_field(adap, A_XGM_TX_CFG + oft, F_TXPAUSEEN,
			(fc & PAUSE_RX) ? F_TXPAUSEEN : 0);
	return 0;
}

/**
 *	t3_mac_enable - enable the MAC in the given directions
 *	@mac: the MAC to configure
 *	@which: bitmap indicating which directions to enable
 *
 *	Enables the MAC for operation in the given directions.
 *	%MAC_DIRECTION_TX enables the Tx direction, and %MAC_DIRECTION_RX
 *	enables the Rx one.
 */
int t3_mac_enable(struct cmac *mac, int which)
{
	int idx = macidx(mac);
	adapter_t *adap = mac->adapter;
	unsigned int oft = mac->offset;
	struct mac_stats *s = &mac->stats;

	if (mac->multiport)
		return t3_vsc7323_enable(adap, mac->ext_port, which);

	if (which & MAC_DIRECTION_TX) {
		t3_write_reg(adap, A_TP_PIO_ADDR, A_TP_TX_DROP_CFG_CH0 + idx);
		t3_write_reg(adap, A_TP_PIO_DATA,
			     adap->params.rev == T3_REV_C ?
			     0xc4ffff01 : 0xc0ede401);
		t3_write_reg(adap, A_TP_PIO_ADDR, A_TP_TX_DROP_MODE);
		t3_set_reg_field(adap, A_TP_PIO_DATA, 1 << idx,
				 adap->params.rev == T3_REV_C ?
				 0 : 1 << idx);

		t3_write_reg(adap, A_XGM_TX_CTRL + oft, F_TXEN);

		t3_write_reg(adap, A_TP_PIO_ADDR, A_TP_TX_DROP_CNT_CH0 + idx);
		mac->tx_mcnt = s->tx_frames;
		mac->tx_tcnt = (G_TXDROPCNTCH0RCVD(t3_read_reg(adap,
							       A_TP_PIO_DATA)));
		mac->tx_xcnt = (G_TXSPI4SOPCNT(t3_read_reg(adap,
						A_XGM_TX_SPI4_SOP_EOP_CNT +
						oft)));
		mac->rx_mcnt = s->rx_frames;
		mac->rx_pause = s->rx_pause;
		mac->rx_xcnt = (G_TXSPI4SOPCNT(t3_read_reg(adap,
						A_XGM_RX_SPI4_SOP_EOP_CNT +
						oft)));
		mac->rx_ocnt = s->rx_fifo_ovfl;
		mac->txen = F_TXEN;
		mac->toggle_cnt = 0;
	}
	if (which & MAC_DIRECTION_RX)
		t3_write_reg(adap, A_XGM_RX_CTRL + oft, F_RXEN);
	return 0;
}

/**
 *	t3_mac_disable - disable the MAC in the given directions
 *	@mac: the MAC to configure
 *	@which: bitmap indicating which directions to disable
 *
 *	Disables the MAC in the given directions.
 *	%MAC_DIRECTION_TX disables the Tx direction, and %MAC_DIRECTION_RX
 *	disables the Rx one.
 */
int t3_mac_disable(struct cmac *mac, int which)
{
	adapter_t *adap = mac->adapter;

	if (mac->multiport)
		return t3_vsc7323_disable(adap, mac->ext_port, which);

	if (which & MAC_DIRECTION_TX) {
		t3_write_reg(adap, A_XGM_TX_CTRL + mac->offset, 0);
		mac->txen = 0;
	}
	if (which & MAC_DIRECTION_RX) {
		int val = xgm_reset_ctrl(mac);

		t3_set_reg_field(mac->adapter, A_XGM_RESET_CTRL + mac->offset,
				 F_PCS_RESET_, 0);
		msleep(100);
		t3_write_reg(adap, A_XGM_RX_CTRL + mac->offset, 0);
		t3_write_reg(mac->adapter, A_XGM_RESET_CTRL + mac->offset, val);
	}
	return 0;
}

int t3b2_mac_watchdog_task(struct cmac *mac)
{
	int status;
	unsigned int tx_tcnt, tx_xcnt;
	adapter_t *adap = mac->adapter;
	struct mac_stats *s = &mac->stats;
	u64 tx_mcnt = s->tx_frames;

	if (mac->multiport)
		tx_mcnt = t3_read_reg(adap, A_XGM_STAT_TX_FRAME_LOW);

	status = 0;
	tx_xcnt = 1; /* By default tx_xcnt is making progress*/
	tx_tcnt = mac->tx_tcnt; /* If tx_mcnt is progressing ignore tx_tcnt*/
	if (tx_mcnt == mac->tx_mcnt && mac->rx_pause == s->rx_pause) {
		u32 cfg, active, enforcepkt;

		tx_xcnt = (G_TXSPI4SOPCNT(t3_read_reg(adap,
						      A_XGM_TX_SPI4_SOP_EOP_CNT +
						      mac->offset)));
		cfg = t3_read_reg(adap, A_MPS_CFG);
		active = macidx(mac) ? cfg & F_PORT1ACTIVE : cfg & F_PORT0ACTIVE;
		enforcepkt = cfg & F_ENFORCEPKT;	
		if (active && enforcepkt && (tx_xcnt == 0)) {
			t3_write_reg(adap, A_TP_PIO_ADDR,
			     	A_TP_TX_DROP_CNT_CH0 + macidx(mac));
			tx_tcnt = (G_TXDROPCNTCH0RCVD(t3_read_reg(adap,
			      	A_TP_PIO_DATA)));
		} else
			goto out;

	} else {
		mac->toggle_cnt = 0;
		goto out;
	}

	if ((tx_tcnt != mac->tx_tcnt) && (mac->tx_xcnt == 0)) {
		if (mac->toggle_cnt > 4) {
			status = 2;
			goto out;
		} else {
			status = 1;
			goto out;
		}
	} else {
		mac->toggle_cnt = 0;
		goto out;
	}

out:
	mac->tx_tcnt = tx_tcnt;
	mac->tx_xcnt = tx_xcnt;
	mac->tx_mcnt = s->tx_frames;
	mac->rx_pause = s->rx_pause;
	if (status == 1) {
		t3_write_reg(adap, A_XGM_TX_CTRL + mac->offset, 0);
		t3_read_reg(adap, A_XGM_TX_CTRL + mac->offset);  /* flush */
		t3_write_reg(adap, A_XGM_TX_CTRL + mac->offset, mac->txen);
		t3_read_reg(adap, A_XGM_TX_CTRL + mac->offset);  /* flush */
		mac->toggle_cnt++;
	} else if (status == 2) {
		t3_mac_reset(mac, -1);
		mac->toggle_cnt = 0;
	}
	return status;
}

/**
 *	t3_mac_update_stats - accumulate MAC statistics
 *	@mac: the MAC handle
 *
 *	This function is called periodically to accumulate the current values
 *	of the RMON counters into the port statistics.  Since the packet
 *	counters are only 32 bits they can overflow in ~286 secs at 10G, so the
 *	function should be called more frequently than that.  The byte counters
 *	are 45-bit wide, they would overflow in ~7.8 hours.
 */
const struct mac_stats *t3_mac_update_stats(struct cmac *mac)
{
#define RMON_READ(mac, addr) t3_read_reg(mac->adapter, addr + mac->offset)
#define RMON_UPDATE(mac, name, reg) \
	(mac)->stats.name += (u64)RMON_READ(mac, A_XGM_STAT_##reg)
#define RMON_UPDATE64(mac, name, reg_lo, reg_hi) \
	(mac)->stats.name += RMON_READ(mac, A_XGM_STAT_##reg_lo) + \
			     ((u64)RMON_READ(mac, A_XGM_STAT_##reg_hi) << 32)

	u32 v, lo;

	if (mac->multiport)
		return t3_vsc7323_update_stats(mac);

	RMON_UPDATE64(mac, rx_octets, RX_BYTES_LOW, RX_BYTES_HIGH);
	RMON_UPDATE64(mac, rx_frames, RX_FRAMES_LOW, RX_FRAMES_HIGH);
	RMON_UPDATE(mac, rx_mcast_frames, RX_MCAST_FRAMES);
	RMON_UPDATE(mac, rx_bcast_frames, RX_BCAST_FRAMES);
	RMON_UPDATE(mac, rx_fcs_errs, RX_CRC_ERR_FRAMES);
	RMON_UPDATE(mac, rx_pause, RX_PAUSE_FRAMES);
	RMON_UPDATE(mac, rx_jabber, RX_JABBER_FRAMES);
	RMON_UPDATE(mac, rx_short, RX_SHORT_FRAMES);
	RMON_UPDATE(mac, rx_symbol_errs, RX_SYM_CODE_ERR_FRAMES);

	RMON_UPDATE(mac, rx_too_long, RX_OVERSIZE_FRAMES);

	v = RMON_READ(mac, A_XGM_RX_MAX_PKT_SIZE_ERR_CNT);
	if (mac->adapter->params.rev == T3_REV_B2)
		v &= 0x7fffffff;
	mac->stats.rx_too_long += v;

	RMON_UPDATE(mac, rx_frames_64,        RX_64B_FRAMES);
	RMON_UPDATE(mac, rx_frames_65_127,    RX_65_127B_FRAMES);
	RMON_UPDATE(mac, rx_frames_128_255,   RX_128_255B_FRAMES);
	RMON_UPDATE(mac, rx_frames_256_511,   RX_256_511B_FRAMES);
	RMON_UPDATE(mac, rx_frames_512_1023,  RX_512_1023B_FRAMES);
	RMON_UPDATE(mac, rx_frames_1024_1518, RX_1024_1518B_FRAMES);
	RMON_UPDATE(mac, rx_frames_1519_max,  RX_1519_MAXB_FRAMES);

	RMON_UPDATE64(mac, tx_octets, TX_BYTE_LOW, TX_BYTE_HIGH);
	RMON_UPDATE64(mac, tx_frames, TX_FRAME_LOW, TX_FRAME_HIGH);
	RMON_UPDATE(mac, tx_mcast_frames, TX_MCAST);
	RMON_UPDATE(mac, tx_bcast_frames, TX_BCAST);
	RMON_UPDATE(mac, tx_pause, TX_PAUSE);
	/* This counts error frames in general (bad FCS, underrun, etc). */
	RMON_UPDATE(mac, tx_underrun, TX_ERR_FRAMES);

	RMON_UPDATE(mac, tx_frames_64,        TX_64B_FRAMES);
	RMON_UPDATE(mac, tx_frames_65_127,    TX_65_127B_FRAMES);
	RMON_UPDATE(mac, tx_frames_128_255,   TX_128_255B_FRAMES);
	RMON_UPDATE(mac, tx_frames_256_511,   TX_256_511B_FRAMES);
	RMON_UPDATE(mac, tx_frames_512_1023,  TX_512_1023B_FRAMES);
	RMON_UPDATE(mac, tx_frames_1024_1518, TX_1024_1518B_FRAMES);
	RMON_UPDATE(mac, tx_frames_1519_max,  TX_1519_MAXB_FRAMES);

	/* The next stat isn't clear-on-read. */
	t3_write_reg(mac->adapter, A_TP_MIB_INDEX, mac->offset ? 51 : 50);
	v = t3_read_reg(mac->adapter, A_TP_MIB_RDATA);
	lo = (u32)mac->stats.rx_cong_drops;
	mac->stats.rx_cong_drops += (u64)(v - lo);

	return &mac->stats;
}
