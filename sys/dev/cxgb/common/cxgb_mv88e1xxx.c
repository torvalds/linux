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

/* Marvell PHY interrupt status bits. */
#define MV_INTR_JABBER          0x0001
#define MV_INTR_POLARITY_CHNG   0x0002
#define MV_INTR_ENG_DETECT_CHNG 0x0010
#define MV_INTR_DOWNSHIFT       0x0020
#define MV_INTR_MDI_XOVER_CHNG  0x0040
#define MV_INTR_FIFO_OVER_UNDER 0x0080
#define MV_INTR_FALSE_CARRIER   0x0100
#define MV_INTR_SYMBOL_ERROR    0x0200
#define MV_INTR_LINK_CHNG       0x0400
#define MV_INTR_AUTONEG_DONE    0x0800
#define MV_INTR_PAGE_RECV       0x1000
#define MV_INTR_DUPLEX_CHNG     0x2000
#define MV_INTR_SPEED_CHNG      0x4000
#define MV_INTR_AUTONEG_ERR     0x8000

/* Marvell PHY specific registers. */
#define MV88E1XXX_SPECIFIC_CNTRL          16
#define MV88E1XXX_SPECIFIC_STATUS         17
#define MV88E1XXX_INTR_ENABLE             18
#define MV88E1XXX_INTR_STATUS             19
#define MV88E1XXX_EXT_SPECIFIC_CNTRL      20
#define MV88E1XXX_RECV_ERR                21
#define MV88E1XXX_EXT_ADDR                22
#define MV88E1XXX_GLOBAL_STATUS           23
#define MV88E1XXX_LED_CNTRL               24
#define MV88E1XXX_LED_OVERRIDE            25
#define MV88E1XXX_EXT_SPECIFIC_CNTRL2     26
#define MV88E1XXX_EXT_SPECIFIC_STATUS     27
#define MV88E1XXX_VIRTUAL_CABLE_TESTER    28
#define MV88E1XXX_EXTENDED_ADDR           29
#define MV88E1XXX_EXTENDED_DATA           30

/* PHY specific control register fields */
#define S_PSCR_MDI_XOVER_MODE    5
#define M_PSCR_MDI_XOVER_MODE    0x3
#define V_PSCR_MDI_XOVER_MODE(x) ((x) << S_PSCR_MDI_XOVER_MODE)

/* Extended PHY specific control register fields */
#define S_DOWNSHIFT_ENABLE 8
#define V_DOWNSHIFT_ENABLE (1 << S_DOWNSHIFT_ENABLE)

#define S_DOWNSHIFT_CNT    9
#define M_DOWNSHIFT_CNT    0x7
#define V_DOWNSHIFT_CNT(x) ((x) << S_DOWNSHIFT_CNT)

/* PHY specific status register fields */
#define S_PSSR_JABBER 0
#define V_PSSR_JABBER (1 << S_PSSR_JABBER)

#define S_PSSR_POLARITY 1
#define V_PSSR_POLARITY (1 << S_PSSR_POLARITY)

#define S_PSSR_RX_PAUSE 2
#define V_PSSR_RX_PAUSE (1 << S_PSSR_RX_PAUSE)

#define S_PSSR_TX_PAUSE 3
#define V_PSSR_TX_PAUSE (1 << S_PSSR_TX_PAUSE)

#define S_PSSR_ENERGY_DETECT 4
#define V_PSSR_ENERGY_DETECT (1 << S_PSSR_ENERGY_DETECT)

#define S_PSSR_DOWNSHIFT_STATUS 5
#define V_PSSR_DOWNSHIFT_STATUS (1 << S_PSSR_DOWNSHIFT_STATUS)

#define S_PSSR_MDI 6
#define V_PSSR_MDI (1 << S_PSSR_MDI)

#define S_PSSR_CABLE_LEN    7
#define M_PSSR_CABLE_LEN    0x7
#define V_PSSR_CABLE_LEN(x) ((x) << S_PSSR_CABLE_LEN)
#define G_PSSR_CABLE_LEN(x) (((x) >> S_PSSR_CABLE_LEN) & M_PSSR_CABLE_LEN)

#define S_PSSR_LINK 10
#define V_PSSR_LINK (1 << S_PSSR_LINK)

#define S_PSSR_STATUS_RESOLVED 11
#define V_PSSR_STATUS_RESOLVED (1 << S_PSSR_STATUS_RESOLVED)

#define S_PSSR_PAGE_RECEIVED 12
#define V_PSSR_PAGE_RECEIVED (1 << S_PSSR_PAGE_RECEIVED)

#define S_PSSR_DUPLEX 13
#define V_PSSR_DUPLEX (1 << S_PSSR_DUPLEX)

#define S_PSSR_SPEED    14
#define M_PSSR_SPEED    0x3
#define V_PSSR_SPEED(x) ((x) << S_PSSR_SPEED)
#define G_PSSR_SPEED(x) (((x) >> S_PSSR_SPEED) & M_PSSR_SPEED)

/* MV88E1XXX MDI crossover register values */
#define CROSSOVER_MDI   0
#define CROSSOVER_MDIX  1
#define CROSSOVER_AUTO  3

#define INTR_ENABLE_MASK (MV_INTR_SPEED_CHNG | MV_INTR_DUPLEX_CHNG | \
	MV_INTR_AUTONEG_DONE | MV_INTR_LINK_CHNG | MV_INTR_FIFO_OVER_UNDER | \
	MV_INTR_ENG_DETECT_CHNG)

/*
 * Reset the PHY.  If 'wait' is set wait until the reset completes.
 */
static int mv88e1xxx_reset(struct cphy *cphy, int wait)
{
	return t3_phy_reset(cphy, 0, wait);
}

static int mv88e1xxx_intr_enable(struct cphy *cphy)
{
	return mdio_write(cphy, 0, MV88E1XXX_INTR_ENABLE, INTR_ENABLE_MASK);
}

static int mv88e1xxx_intr_disable(struct cphy *cphy)
{
	return mdio_write(cphy, 0, MV88E1XXX_INTR_ENABLE, 0);
}

static int mv88e1xxx_intr_clear(struct cphy *cphy)
{
	u32 val;

	/* Clear PHY interrupts by reading the register. */
	return mdio_read(cphy, 0, MV88E1XXX_INTR_STATUS, &val);
}

static int mv88e1xxx_crossover_set(struct cphy *cphy, int crossover)
{
	return t3_mdio_change_bits(cphy, 0, MV88E1XXX_SPECIFIC_CNTRL,
				   V_PSCR_MDI_XOVER_MODE(M_PSCR_MDI_XOVER_MODE),
				   V_PSCR_MDI_XOVER_MODE(crossover));
}

static int mv88e1xxx_autoneg_enable(struct cphy *cphy)
{
	mv88e1xxx_crossover_set(cphy, CROSSOVER_AUTO);

	/* restart autoneg for change to take effect */
	return t3_mdio_change_bits(cphy, 0, MII_BMCR, BMCR_PDOWN | BMCR_ISOLATE,
			 	   BMCR_ANENABLE | BMCR_ANRESTART);
}

static int mv88e1xxx_autoneg_restart(struct cphy *cphy)
{
	return t3_mdio_change_bits(cphy, 0, MII_BMCR, BMCR_PDOWN | BMCR_ISOLATE,
			 	   BMCR_ANRESTART);
}

static int mv88e1xxx_set_loopback(struct cphy *cphy, int mmd, int dir, int on)
{
	return t3_mdio_change_bits(cphy, 0, MII_BMCR, BMCR_LOOPBACK,
			 	   on ? BMCR_LOOPBACK : 0);
}

static int mv88e1xxx_get_link_status(struct cphy *cphy, int *link_state,
				     int *speed, int *duplex, int *fc)
{
	u32 status;
	int sp = -1, dplx = -1, pause = 0;

	mdio_read(cphy, 0, MV88E1XXX_SPECIFIC_STATUS, &status);
	if ((status & V_PSSR_STATUS_RESOLVED) != 0) {
		if (status & V_PSSR_RX_PAUSE)
			pause |= PAUSE_RX;
		if (status & V_PSSR_TX_PAUSE)
			pause |= PAUSE_TX;
		dplx = (status & V_PSSR_DUPLEX) ? DUPLEX_FULL : DUPLEX_HALF;
		sp = G_PSSR_SPEED(status);
		if (sp == 0)
			sp = SPEED_10;
		else if (sp == 1)
			sp = SPEED_100;
		else
			sp = SPEED_1000;
	}
	if (link_state)
		*link_state = status & V_PSSR_LINK ? PHY_LINK_UP :
		    PHY_LINK_DOWN;
	if (speed)
		*speed = sp;
	if (duplex)
		*duplex = dplx;
	if (fc)
		*fc = pause;
	return 0;
}

static int mv88e1xxx_set_speed_duplex(struct cphy *phy, int speed, int duplex)
{
	int err = t3_set_phy_speed_duplex(phy, speed, duplex);

	/* PHY needs reset for new settings to take effect */
	if (!err)
		err = mv88e1xxx_reset(phy, 0);
	return err;
}

static int mv88e1xxx_downshift_set(struct cphy *cphy, int downshift_enable)
{
	/*
	 * Set the downshift counter to 2 so we try to establish Gb link
	 * twice before downshifting.
	 */
	return t3_mdio_change_bits(cphy, 0, MV88E1XXX_EXT_SPECIFIC_CNTRL,
		V_DOWNSHIFT_ENABLE | V_DOWNSHIFT_CNT(M_DOWNSHIFT_CNT),
		downshift_enable ? V_DOWNSHIFT_ENABLE | V_DOWNSHIFT_CNT(2) : 0);
}

static int mv88e1xxx_power_down(struct cphy *cphy, int enable)
{
	return t3_mdio_change_bits(cphy, 0, MII_BMCR, BMCR_PDOWN,
				   enable ? BMCR_PDOWN : 0);
}

static int mv88e1xxx_intr_handler(struct cphy *cphy)
{
	const u32 link_change_intrs = MV_INTR_LINK_CHNG |
		MV_INTR_AUTONEG_DONE | MV_INTR_DUPLEX_CHNG |
		MV_INTR_SPEED_CHNG | MV_INTR_DOWNSHIFT;

	u32 cause;
	int cphy_cause = 0;

	mdio_read(cphy, 0, MV88E1XXX_INTR_STATUS, &cause);
	cause &= INTR_ENABLE_MASK;
	if (cause & link_change_intrs)
		cphy_cause |= cphy_cause_link_change;
	if (cause & MV_INTR_FIFO_OVER_UNDER)
		cphy_cause |= cphy_cause_fifo_error;
	return cphy_cause;
}

#ifdef C99_NOT_SUPPORTED
static struct cphy_ops mv88e1xxx_ops = {
	mv88e1xxx_reset,
	mv88e1xxx_intr_enable,
	mv88e1xxx_intr_disable,
	mv88e1xxx_intr_clear,
	mv88e1xxx_intr_handler,
	mv88e1xxx_autoneg_enable,
	mv88e1xxx_autoneg_restart,
	t3_phy_advertise,
	mv88e1xxx_set_loopback,
	mv88e1xxx_set_speed_duplex,
	mv88e1xxx_get_link_status,
	mv88e1xxx_power_down,
};
#else
static struct cphy_ops mv88e1xxx_ops = {
	.reset             = mv88e1xxx_reset,
	.intr_enable       = mv88e1xxx_intr_enable,
	.intr_disable      = mv88e1xxx_intr_disable,
	.intr_clear        = mv88e1xxx_intr_clear,
	.intr_handler      = mv88e1xxx_intr_handler,
	.autoneg_enable    = mv88e1xxx_autoneg_enable,
	.autoneg_restart   = mv88e1xxx_autoneg_restart,
	.advertise         = t3_phy_advertise,
	.set_loopback      = mv88e1xxx_set_loopback,
	.set_speed_duplex  = mv88e1xxx_set_speed_duplex,
	.get_link_status   = mv88e1xxx_get_link_status,
	.power_down        = mv88e1xxx_power_down,
};
#endif

int t3_mv88e1xxx_phy_prep(pinfo_t *pinfo, int phy_addr,
			  const struct mdio_ops *mdio_ops)
{
	struct cphy *phy = &pinfo->phy;
	int err;

	cphy_init(phy, pinfo->adapter, pinfo, phy_addr, &mv88e1xxx_ops, mdio_ops,
		  SUPPORTED_10baseT_Full | SUPPORTED_100baseT_Full |
		  SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg | SUPPORTED_MII |
		  SUPPORTED_TP | SUPPORTED_IRQ, "10/100/1000BASE-T");

	/* Configure copper PHY transmitter as class A to reduce EMI. */
	err = mdio_write(phy, 0, MV88E1XXX_EXTENDED_ADDR, 0xb);
	if (!err)
		err = mdio_write(phy, 0, MV88E1XXX_EXTENDED_DATA, 0x8004);

	if (!err)
		err = mv88e1xxx_downshift_set(phy, 1);   /* Enable downshift */
	return err;
}
