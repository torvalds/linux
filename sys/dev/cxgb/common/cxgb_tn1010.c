/**************************************************************************
SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2008, Chelsio Inc.
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

/* TN1010 PHY specific registers. */
enum {
	TN1010_VEND1_STAT = 1,
};

/* IEEE auto-negotiation 10GBASE-T registers */
enum {
	ANEG_ADVER    = 16,
	ANEG_LPA      = 19,
	ANEG_10G_CTRL = 32,
	ANEG_10G_STAT = 33
};

#define ADVERTISE_ENPAGE      (1 << 12)
#define ADVERTISE_10000FULL   (1 << 12)
#define ADVERTISE_LOOP_TIMING (1 << 0)

/* vendor specific status register fields */
#define F_XS_LANE_ALIGN_STAT (1 << 0)
#define F_PCS_BLK_LOCK       (1 << 1)
#define F_PMD_SIGNAL_OK      (1 << 2)
#define F_LINK_STAT          (1 << 3)
#define F_ANEG_SPEED_1G      (1 << 4)
#define F_ANEG_MASTER        (1 << 5)

#define S_ANEG_STAT    6
#define M_ANEG_STAT    0x3
#define G_ANEG_STAT(x) (((x) >> S_ANEG_STAT) & M_ANEG_STAT)

enum {                        /* autonegotiation status */
	ANEG_IN_PROGR = 0,
	ANEG_COMPLETE = 1,
	ANEG_FAILED   = 3
};

/*
 * Reset the PHY.  May take up to 500ms to complete.
 */
static int tn1010_reset(struct cphy *phy, int wait)
{
	int err = t3_phy_reset(phy, MDIO_DEV_PMA_PMD, wait);
	msleep(500);
	return err;
}

static int tn1010_power_down(struct cphy *phy, int enable)
{
	return t3_mdio_change_bits(phy, MDIO_DEV_PMA_PMD, MII_BMCR,
				   BMCR_PDOWN, enable ? BMCR_PDOWN : 0);
}

static int tn1010_autoneg_enable(struct cphy *phy)
{
	int err;

	err = tn1010_power_down(phy, 0);
	if (!err)
		err = t3_mdio_change_bits(phy, MDIO_DEV_ANEG, MII_BMCR, 0,
					  BMCR_ANENABLE | BMCR_ANRESTART);
	return err;
}

static int tn1010_autoneg_restart(struct cphy *phy)
{
	int err;

	err = tn1010_power_down(phy, 0);
	if (!err)
		err = t3_mdio_change_bits(phy, MDIO_DEV_ANEG, MII_BMCR, 0,
					  BMCR_ANRESTART);
	return err;
}

static int tn1010_advertise(struct cphy *phy, unsigned int advert)
{
	int err, val;

	if (!(advert & ADVERTISED_1000baseT_Full))
		return -EINVAL;               /* PHY can't disable 1000BASE-T */

	val = ADVERTISE_CSMA | ADVERTISE_ENPAGE | ADVERTISE_NPAGE;
	if (advert & ADVERTISED_Pause)
		val |= ADVERTISE_PAUSE_CAP;
	if (advert & ADVERTISED_Asym_Pause)
		val |= ADVERTISE_PAUSE_ASYM;
	err = mdio_write(phy, MDIO_DEV_ANEG, ANEG_ADVER, val);
	if (err)
		return err;

	val = (advert & ADVERTISED_10000baseT_Full) ? ADVERTISE_10000FULL : 0;
	return mdio_write(phy, MDIO_DEV_ANEG, ANEG_10G_CTRL, val |
			  ADVERTISE_LOOP_TIMING);
}

static int tn1010_get_link_status(struct cphy *phy, int *link_state,
				  int *speed, int *duplex, int *fc)
{
	unsigned int status, lpa, adv;
	int err, sp = -1, pause = 0;

	err = mdio_read(phy, MDIO_DEV_VEND1, TN1010_VEND1_STAT, &status);
	if (err)
		return err;

	if (link_state)
		*link_state = status & F_LINK_STAT ? PHY_LINK_UP :
		    PHY_LINK_DOWN;

	if (G_ANEG_STAT(status) == ANEG_COMPLETE) {
		sp = (status & F_ANEG_SPEED_1G) ? SPEED_1000 : SPEED_10000;

		if (fc) {
			err = mdio_read(phy, MDIO_DEV_ANEG, ANEG_LPA, &lpa);
			if (!err)
				err = mdio_read(phy, MDIO_DEV_ANEG, ANEG_ADVER,
						&adv);
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
		*duplex = DUPLEX_FULL;
	if (fc)
		*fc = pause;
	return 0;
}

static int tn1010_set_speed_duplex(struct cphy *phy, int speed, int duplex)
{
	return -EINVAL;    /* require autoneg */
}

#ifdef C99_NOT_SUPPORTED
static struct cphy_ops tn1010_ops = {
	tn1010_reset,
	t3_phy_lasi_intr_enable,
	t3_phy_lasi_intr_disable,
	t3_phy_lasi_intr_clear,
	t3_phy_lasi_intr_handler,
	tn1010_autoneg_enable,
	tn1010_autoneg_restart,
	tn1010_advertise,
	NULL,
	tn1010_set_speed_duplex,
	tn1010_get_link_status,
	tn1010_power_down,
};
#else
static struct cphy_ops tn1010_ops = {
	.reset             = tn1010_reset,
	.intr_enable       = t3_phy_lasi_intr_enable,
	.intr_disable      = t3_phy_lasi_intr_disable,
	.intr_clear        = t3_phy_lasi_intr_clear,
	.intr_handler      = t3_phy_lasi_intr_handler,
	.autoneg_enable    = tn1010_autoneg_enable,
	.autoneg_restart   = tn1010_autoneg_restart,
	.advertise         = tn1010_advertise,
	.set_speed_duplex  = tn1010_set_speed_duplex,
	.get_link_status   = tn1010_get_link_status,
	.power_down        = tn1010_power_down,
};
#endif

int t3_tn1010_phy_prep(pinfo_t *pinfo, int phy_addr,
		       const struct mdio_ops *mdio_ops)
{
	cphy_init(&pinfo->phy, pinfo->adapter, pinfo, phy_addr, &tn1010_ops, mdio_ops,
		  SUPPORTED_1000baseT_Full | SUPPORTED_10000baseT_Full |
		  SUPPORTED_Autoneg | SUPPORTED_AUI | SUPPORTED_TP,
		  "1000/10GBASE-T");
	msleep(500);    /* PHY needs up to 500ms to start responding to MDIO */
	return 0;
}
