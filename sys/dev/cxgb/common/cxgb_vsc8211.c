/**************************************************************************
SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2007, Chelsio Inc.
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

/* VSC8211 PHY specific registers. */
enum {
	VSC8211_SIGDET_CTRL   = 19,
	VSC8211_EXT_CTRL      = 23,
	VSC8211_PHY_CTRL      = 24,
	VSC8211_INTR_ENABLE   = 25,
	VSC8211_INTR_STATUS   = 26,
	VSC8211_LED_CTRL      = 27,
	VSC8211_AUX_CTRL_STAT = 28,
	VSC8211_EXT_PAGE_AXS  = 31,
};

enum {
	VSC_INTR_RX_ERR     = 1 << 0,
	VSC_INTR_MS_ERR     = 1 << 1,  /* master/slave resolution error */
	VSC_INTR_CABLE      = 1 << 2,  /* cable impairment */
	VSC_INTR_FALSE_CARR = 1 << 3,  /* false carrier */
	VSC_INTR_MEDIA_CHG  = 1 << 4,  /* AMS media change */
	VSC_INTR_RX_FIFO    = 1 << 5,  /* Rx FIFO over/underflow */
	VSC_INTR_TX_FIFO    = 1 << 6,  /* Tx FIFO over/underflow */
	VSC_INTR_DESCRAMBL  = 1 << 7,  /* descrambler lock-lost */
	VSC_INTR_SYMBOL_ERR = 1 << 8,  /* symbol error */
	VSC_INTR_NEG_DONE   = 1 << 10, /* autoneg done */
	VSC_INTR_NEG_ERR    = 1 << 11, /* autoneg error */
	VSC_INTR_DPLX_CHG   = 1 << 12, /* duplex change */
	VSC_INTR_LINK_CHG   = 1 << 13, /* link change */
	VSC_INTR_SPD_CHG    = 1 << 14, /* speed change */
	VSC_INTR_ENABLE     = 1 << 15, /* interrupt enable */
};

enum {
	VSC_CTRL_CLAUSE37_VIEW = 1 << 4,   /* Switch to Clause 37 view */
	VSC_CTRL_MEDIA_MODE_HI = 0xf000    /* High part of media mode select */
};

#define CFG_CHG_INTR_MASK (VSC_INTR_LINK_CHG | VSC_INTR_NEG_ERR | \
			   VSC_INTR_DPLX_CHG | VSC_INTR_SPD_CHG | \
	 		   VSC_INTR_NEG_DONE)
#define INTR_MASK (CFG_CHG_INTR_MASK | VSC_INTR_TX_FIFO | VSC_INTR_RX_FIFO | \
		   VSC_INTR_ENABLE)

/* PHY specific auxiliary control & status register fields */
#define S_ACSR_ACTIPHY_TMR    0
#define M_ACSR_ACTIPHY_TMR    0x3
#define V_ACSR_ACTIPHY_TMR(x) ((x) << S_ACSR_ACTIPHY_TMR)

#define S_ACSR_SPEED    3
#define M_ACSR_SPEED    0x3
#define G_ACSR_SPEED(x) (((x) >> S_ACSR_SPEED) & M_ACSR_SPEED)

#define S_ACSR_DUPLEX 5
#define F_ACSR_DUPLEX (1 << S_ACSR_DUPLEX)

#define S_ACSR_ACTIPHY 6
#define F_ACSR_ACTIPHY (1 << S_ACSR_ACTIPHY)

/*
 * Reset the PHY.  This PHY completes reset immediately so we never wait.
 */
static int vsc8211_reset(struct cphy *cphy, int wait)
{
	return t3_phy_reset(cphy, 0, 0);
}

static int vsc8211_intr_enable(struct cphy *cphy)
{
	return mdio_write(cphy, 0, VSC8211_INTR_ENABLE, INTR_MASK);
}

static int vsc8211_intr_disable(struct cphy *cphy)
{
	return mdio_write(cphy, 0, VSC8211_INTR_ENABLE, 0);
}

static int vsc8211_intr_clear(struct cphy *cphy)
{
	u32 val;

	/* Clear PHY interrupts by reading the register. */
	return mdio_read(cphy, 0, VSC8211_INTR_STATUS, &val);
}

static int vsc8211_autoneg_enable(struct cphy *cphy)
{
	return t3_mdio_change_bits(cphy, 0, MII_BMCR, BMCR_PDOWN | BMCR_ISOLATE,
				   BMCR_ANENABLE | BMCR_ANRESTART);
}

static int vsc8211_autoneg_restart(struct cphy *cphy)
{
	return t3_mdio_change_bits(cphy, 0, MII_BMCR, BMCR_PDOWN | BMCR_ISOLATE,
				   BMCR_ANRESTART);
}

static int vsc8211_get_link_status(struct cphy *cphy, int *link_state,
				     int *speed, int *duplex, int *fc)
{
	unsigned int bmcr, status, lpa, adv;
	int err, sp = -1, dplx = -1, pause = 0;

	err = mdio_read(cphy, 0, MII_BMCR, &bmcr);
	if (!err)
		err = mdio_read(cphy, 0, MII_BMSR, &status);
	if (err)
		return err;

	if (link_state) {
		/*
		 * BMSR_LSTATUS is latch-low, so if it is 0 we need to read it
		 * once more to get the current link state.
		 */
		if (!(status & BMSR_LSTATUS))
			err = mdio_read(cphy, 0, MII_BMSR, &status);
		if (err)
			return err;
		*link_state = status & BMSR_LSTATUS ? PHY_LINK_UP :
		    PHY_LINK_DOWN;
	}
	if (!(bmcr & BMCR_ANENABLE)) {
		dplx = (bmcr & BMCR_FULLDPLX) ? DUPLEX_FULL : DUPLEX_HALF;
		if (bmcr & BMCR_SPEED1000)
			sp = SPEED_1000;
		else if (bmcr & BMCR_SPEED100)
			sp = SPEED_100;
		else
			sp = SPEED_10;
	} else if (status & BMSR_ANEGCOMPLETE) {
		err = mdio_read(cphy, 0, VSC8211_AUX_CTRL_STAT, &status);
		if (err)
			return err;

		dplx = (status & F_ACSR_DUPLEX) ? DUPLEX_FULL : DUPLEX_HALF;
		sp = G_ACSR_SPEED(status);
		if (sp == 0)
			sp = SPEED_10;
		else if (sp == 1)
			sp = SPEED_100;
		else
			sp = SPEED_1000;

		if (fc && dplx == DUPLEX_FULL) {
			err = mdio_read(cphy, 0, MII_LPA, &lpa);
			if (!err)
				err = mdio_read(cphy, 0, MII_ADVERTISE, &adv);
			if (err)
				return err;

			if (lpa & adv & ADVERTISE_PAUSE_CAP)
				pause = PAUSE_RX | PAUSE_TX;
			else if ((lpa & ADVERTISE_PAUSE_CAP) &&
				 (lpa & ADVERTISE_PAUSE_ASYM) &&
				 (adv & ADVERTISE_PAUSE_ASYM))
				pause = PAUSE_TX;
			else if ((lpa & ADVERTISE_PAUSE_ASYM) &&
				 (adv & ADVERTISE_PAUSE_CAP))
				pause = PAUSE_RX;
		}
	}
	if (speed)
		*speed = sp;
	if (duplex)
		*duplex = dplx;
	if (fc)
		*fc = pause;
	return 0;
}

static int vsc8211_get_link_status_fiber(struct cphy *cphy, int *link_state,
					 int *speed, int *duplex, int *fc)
{
	unsigned int bmcr, status, lpa, adv;
	int err, sp = -1, dplx = -1, pause = 0;

	err = mdio_read(cphy, 0, MII_BMCR, &bmcr);
	if (!err)
		err = mdio_read(cphy, 0, MII_BMSR, &status);
	if (err)
		return err;

	if (link_state) {
		/*
		 * BMSR_LSTATUS is latch-low, so if it is 0 we need to read it
		 * once more to get the current link state.
		 */
		if (!(status & BMSR_LSTATUS))
			err = mdio_read(cphy, 0, MII_BMSR, &status);
		if (err)
			return err;
		*link_state = status & BMSR_LSTATUS ? PHY_LINK_UP :
		    PHY_LINK_DOWN;
	}
	if (!(bmcr & BMCR_ANENABLE)) {
		dplx = (bmcr & BMCR_FULLDPLX) ? DUPLEX_FULL : DUPLEX_HALF;
		if (bmcr & BMCR_SPEED1000)
			sp = SPEED_1000;
		else if (bmcr & BMCR_SPEED100)
			sp = SPEED_100;
		else
			sp = SPEED_10;
	} else if (status & BMSR_ANEGCOMPLETE) {
		err = mdio_read(cphy, 0, MII_LPA, &lpa);
		if (!err)
			err = mdio_read(cphy, 0, MII_ADVERTISE, &adv);
		if (err)
			return err;

		if (adv & lpa & ADVERTISE_1000XFULL) {
			dplx = DUPLEX_FULL;
			sp = SPEED_1000;
		} else if (adv & lpa & ADVERTISE_1000XHALF) {
			dplx = DUPLEX_HALF;
			sp = SPEED_1000;
		}

		if (fc && dplx == DUPLEX_FULL) {
			if (lpa & adv & ADVERTISE_1000XPAUSE)
				pause = PAUSE_RX | PAUSE_TX;
			else if ((lpa & ADVERTISE_1000XPAUSE) &&
				 (adv & lpa & ADVERTISE_1000XPSE_ASYM))
				pause = PAUSE_TX;
			else if ((lpa & ADVERTISE_1000XPSE_ASYM) &&
				 (adv & ADVERTISE_1000XPAUSE))
				pause = PAUSE_RX;
		}
	}
	if (speed)
		*speed = sp;
	if (duplex)
		*duplex = dplx;
	if (fc)
		*fc = pause;
	return 0;
}

/*
 * Enable/disable auto MDI/MDI-X in forced link speed mode.
 */
static int vsc8211_set_automdi(struct cphy *phy, int enable)
{
	int err;

	if ((err = mdio_write(phy, 0, VSC8211_EXT_PAGE_AXS, 0x52b5)) != 0 ||
	    (err = mdio_write(phy, 0, 18, 0x12)) != 0 ||
	    (err = mdio_write(phy, 0, 17, enable ? 0x2803 : 0x3003)) != 0 ||
	    (err = mdio_write(phy, 0, 16, 0x87fa)) != 0 ||
	    (err = mdio_write(phy, 0, VSC8211_EXT_PAGE_AXS, 0)) != 0)
		return err;
	return 0;
}

static int vsc8211_set_speed_duplex(struct cphy *phy, int speed, int duplex)
{
	int err;

	err = t3_set_phy_speed_duplex(phy, speed, duplex);
	if (!err)
		err = vsc8211_set_automdi(phy, 1);
	return err;
}

static int vsc8211_power_down(struct cphy *cphy, int enable)
{
	return t3_mdio_change_bits(cphy, 0, MII_BMCR, BMCR_PDOWN,
				   enable ? BMCR_PDOWN : 0);
}

static int vsc8211_intr_handler(struct cphy *cphy)
{
	unsigned int cause;
	int err, cphy_cause = 0;

	err = mdio_read(cphy, 0, VSC8211_INTR_STATUS, &cause);
	if (err)
		return err;

	cause &= INTR_MASK;
	if (cause & CFG_CHG_INTR_MASK)
		cphy_cause |= cphy_cause_link_change;
	if (cause & (VSC_INTR_RX_FIFO | VSC_INTR_TX_FIFO))
		cphy_cause |= cphy_cause_fifo_error;
	return cphy_cause;
}

#ifdef C99_NOT_SUPPORTED
static struct cphy_ops vsc8211_ops = {
	vsc8211_reset,
	vsc8211_intr_enable,
	vsc8211_intr_disable,
	vsc8211_intr_clear,
	vsc8211_intr_handler,
	vsc8211_autoneg_enable,
	vsc8211_autoneg_restart,
	t3_phy_advertise,
	NULL,
	vsc8211_set_speed_duplex,
	vsc8211_get_link_status,
	vsc8211_power_down,
};

static struct cphy_ops vsc8211_fiber_ops = {
	vsc8211_reset,
	vsc8211_intr_enable,
	vsc8211_intr_disable,
	vsc8211_intr_clear,
	vsc8211_intr_handler,
	vsc8211_autoneg_enable,
	vsc8211_autoneg_restart,
	t3_phy_advertise_fiber,
	NULL,
	t3_set_phy_speed_duplex,
	vsc8211_get_link_status_fiber,
	vsc8211_power_down,
};
#else
static struct cphy_ops vsc8211_ops = {
	.reset             = vsc8211_reset,
	.intr_enable       = vsc8211_intr_enable,
	.intr_disable      = vsc8211_intr_disable,
	.intr_clear        = vsc8211_intr_clear,
	.intr_handler      = vsc8211_intr_handler,
	.autoneg_enable    = vsc8211_autoneg_enable,
	.autoneg_restart   = vsc8211_autoneg_restart,
	.advertise         = t3_phy_advertise,
	.set_speed_duplex  = vsc8211_set_speed_duplex,
	.get_link_status   = vsc8211_get_link_status,
	.power_down        = vsc8211_power_down,
};

static struct cphy_ops vsc8211_fiber_ops = {
	.reset             = vsc8211_reset,
	.intr_enable       = vsc8211_intr_enable,
	.intr_disable      = vsc8211_intr_disable,
	.intr_clear        = vsc8211_intr_clear,
	.intr_handler      = vsc8211_intr_handler,
	.autoneg_enable    = vsc8211_autoneg_enable,
	.autoneg_restart   = vsc8211_autoneg_restart,
	.advertise         = t3_phy_advertise_fiber,
	.set_speed_duplex  = t3_set_phy_speed_duplex,
	.get_link_status   = vsc8211_get_link_status_fiber,
	.power_down        = vsc8211_power_down,
};
#endif

#define VSC8211_PHY_CTRL 24

#define S_VSC8211_TXFIFODEPTH    7
#define M_VSC8211_TXFIFODEPTH    0x7
#define V_VSC8211_TXFIFODEPTH(x) ((x) << S_VSC8211_TXFIFODEPTH)
#define G_VSC8211_TXFIFODEPTH(x) (((x) >> S_VSC8211_TXFIFODEPTH) & M_VSC8211_TXFIFODEPTH)

#define S_VSC8211_RXFIFODEPTH    4
#define M_VSC8211_RXFIFODEPTH    0x7
#define V_VSC8211_RXFIFODEPTH(x) ((x) << S_VSC8211_RXFIFODEPTH)
#define G_VSC8211_RXFIFODEPTH(x) (((x) >> S_VSC8211_RXFIFODEPTH) & M_VSC8211_RXFIFODEPTH)

int t3_vsc8211_fifo_depth(adapter_t *adap, unsigned int mtu, int port)
{
	/* TX FIFO Depth set bits 9:7 to 100 (IEEE mode) */
	unsigned int val = 4;
	unsigned int currentregval;
	unsigned int regval;
	int err;

	/* Retrieve the port info structure from adater_t */
	struct port_info *portinfo = adap2pinfo(adap, port);

	/* What phy is this */
	struct cphy *phy = &portinfo->phy;

	/* Read the current value of the PHY control Register */
	err = mdio_read(phy, 0, VSC8211_PHY_CTRL, &currentregval);

	if (err)
		return err;

	/* IEEE mode supports up to 1518 bytes */
	/* mtu does not contain the header + FCS (18 bytes) */
	if (mtu > 1500)
		/* 
		 * If using a packet size > 1500  set TX FIFO Depth bits 
		 * 9:7 to 011 (Jumbo packet mode) 
		 */
		val = 3;

	regval = V_VSC8211_TXFIFODEPTH(val) | V_VSC8211_RXFIFODEPTH(val) | 
		(currentregval & ~V_VSC8211_TXFIFODEPTH(M_VSC8211_TXFIFODEPTH) &
		~V_VSC8211_RXFIFODEPTH(M_VSC8211_RXFIFODEPTH));

	return  mdio_write(phy, 0, VSC8211_PHY_CTRL, regval);
}

int t3_vsc8211_phy_prep(pinfo_t *pinfo, int phy_addr,
			const struct mdio_ops *mdio_ops)
{
	struct cphy *phy = &pinfo->phy;
	int err;
	unsigned int val;

	cphy_init(&pinfo->phy, pinfo->adapter, pinfo, phy_addr, &vsc8211_ops, mdio_ops,
		  SUPPORTED_10baseT_Full | SUPPORTED_100baseT_Full |
		  SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg | SUPPORTED_MII |
		  SUPPORTED_TP | SUPPORTED_IRQ, "10/100/1000BASE-T");
	msleep(20);       /* PHY needs ~10ms to start responding to MDIO */

	err = mdio_read(phy, 0, VSC8211_EXT_CTRL, &val);
	if (err)
		return err;
	if (val & VSC_CTRL_MEDIA_MODE_HI) {
		/* copper interface, just need to configure the LEDs */
		return mdio_write(phy, 0, VSC8211_LED_CTRL, 0x100);
	}

	phy->caps = SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg |
		    SUPPORTED_MII | SUPPORTED_FIBRE | SUPPORTED_IRQ;
	phy->desc = "1000BASE-X";
	phy->ops = &vsc8211_fiber_ops;

	if ((err = mdio_write(phy, 0, VSC8211_EXT_PAGE_AXS, 1)) != 0 ||
	    (err = mdio_write(phy, 0, VSC8211_SIGDET_CTRL, 1)) != 0 ||
	    (err = mdio_write(phy, 0, VSC8211_EXT_PAGE_AXS, 0)) != 0 ||
	    (err = mdio_write(phy, 0, VSC8211_EXT_CTRL,
			      val | VSC_CTRL_CLAUSE37_VIEW)) != 0 ||
	    (err = vsc8211_reset(phy, 0)) != 0)
		return err;

	udelay(5); /* delay after reset before next SMI */
	return 0;
}
