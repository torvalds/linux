/**************************************************************************
SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2009 Chelsio Inc.
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

enum {
	/* MDIO_DEV_PMA_PMD registers */
	AQ_LINK_STAT	= 0xe800,

	/* MDIO_DEV_XGXS registers */
	AQ_XAUI_RX_CFG	= 0xc400,
	AQ_XAUI_KX_CFG	= 0xc440,
	AQ_XAUI_TX_CFG	= 0xe400,

	/* MDIO_DEV_ANEG registers */
	AQ_100M_CTRL	= 0x0010,
	AQ_10G_CTRL	= 0x0020,
	AQ_1G_CTRL	= 0xc400,
	AQ_ANEG_STAT	= 0xc800,

	/* MDIO_DEV_VEND1 registers */
	AQ_FW_VERSION	= 0x0020,
	AQ_THERMAL_THR	= 0xc421,
	AQ_THERMAL1	= 0xc820,
	AQ_THERMAL2	= 0xc821,
	AQ_IFLAG_GLOBAL	= 0xfc00,
	AQ_IMASK_GLOBAL	= 0xff00,
};

#define AQBIT(x)	(1 << (0x##x))
#define ADV_1G_FULL	AQBIT(f)
#define ADV_1G_HALF	AQBIT(e)
#define ADV_10G_FULL	AQBIT(c)

#define AQ_WRITE_REGS(phy, regs) do { \
	int i; \
	for (i = 0; i < ARRAY_SIZE(regs); i++) { \
		(void) mdio_write(phy, regs[i].mmd, regs[i].reg, regs[i].val); \
	} \
} while (0)
#define AQ_READ_REGS(phy, regs) do { \
	unsigned i, v; \
	for (i = 0; i < ARRAY_SIZE(regs); i++) { \
		(void) mdio_read(phy, regs[i].mmd, regs[i].reg, &v); \
	} \
} while (0)

/*
 * Return value is temperature in celcius, 0xffff for error or don't know.
 */
static int
aq100x_temperature(struct cphy *phy)
{
	unsigned int v;

	if (mdio_read(phy, MDIO_DEV_VEND1, AQ_THERMAL2, &v) ||
	    v == 0xffff || (v & 1) != 1)
		return (0xffff);

	if (mdio_read(phy, MDIO_DEV_VEND1, AQ_THERMAL1, &v))
		return (0xffff);

	return ((int)((signed char)(v >> 8)));
}

static int
aq100x_set_defaults(struct cphy *phy)
{
	return mdio_write(phy, MDIO_DEV_VEND1, AQ_THERMAL_THR, 0x6c00);
}

static int
aq100x_reset(struct cphy *phy, int wait)
{
	int err;
	err = t3_phy_reset(phy, MDIO_DEV_PMA_PMD, wait);
	if (!err)
		err = aq100x_set_defaults(phy);
	return (err);
}

static int
aq100x_intr_enable(struct cphy *phy)
{
	struct {
		int mmd;
		int reg;
		int val;
	} imasks[] = {
		{MDIO_DEV_VEND1, 0xd400, AQBIT(e)},
		{MDIO_DEV_VEND1, 0xff01, AQBIT(2)},
		{MDIO_DEV_VEND1, AQ_IMASK_GLOBAL, AQBIT(0)}
	};

	AQ_WRITE_REGS(phy, imasks);

	return (0);
}

static int
aq100x_intr_disable(struct cphy *phy)
{
	struct {
		int mmd;
		int reg;
		int val;
	} imasks[] = {
		{MDIO_DEV_VEND1, 0xd400, 0},
		{MDIO_DEV_VEND1, 0xff01, 0},
		{MDIO_DEV_VEND1, AQ_IMASK_GLOBAL, 0}
	};

	AQ_WRITE_REGS(phy, imasks);

	return (0);
}

static int
aq100x_intr_clear(struct cphy *phy)
{
	struct {
		int mmd;
		int reg;
	} iclr[] = {
		{MDIO_DEV_VEND1, 0xcc00},
		{MDIO_DEV_VEND1, AQ_IMASK_GLOBAL} /* needed? */
	};

	AQ_READ_REGS(phy, iclr);

	return (0);
}

static int
aq100x_vendor_intr(struct cphy *phy, int *rc)
{
	int err;
	unsigned int cause, v;

	err = mdio_read(phy, MDIO_DEV_VEND1, 0xfc01, &cause);
	if (err)
		return (err);

	if (cause & AQBIT(2)) {
		err = mdio_read(phy, MDIO_DEV_VEND1, 0xcc00, &v);
		if (err)
			return (err);

		if (v & AQBIT(e)) {
			CH_WARN(phy->adapter, "PHY%d: temperature is now %dC\n",
			    phy->addr, aq100x_temperature(phy));

			t3_set_reg_field(phy->adapter, A_T3DBG_GPIO_EN,
			    phy->addr ? F_GPIO10_OUT_VAL : F_GPIO6_OUT_VAL, 0);

			*rc |= cphy_cause_alarm;
		}

		cause &= ~4;
	}

	if (cause)
		CH_WARN(phy->adapter, "PHY%d: unhandled vendor interrupt"
		    " (0x%x)\n", phy->addr, cause);

	return (0);

}

static int
aq100x_intr_handler(struct cphy *phy)
{
	int err, rc = 0;
	unsigned int cause;

	err = mdio_read(phy, MDIO_DEV_VEND1, AQ_IFLAG_GLOBAL, &cause);
	if (err)
		return (err);

	if (cause & AQBIT(0)) {
		err = aq100x_vendor_intr(phy, &rc);
		if (err)
			return (err);
		cause &= ~AQBIT(0);
	}

	if (cause)
		CH_WARN(phy->adapter, "PHY%d: unhandled interrupt (0x%x)\n",
		    phy->addr, cause);

	return (rc);
}

static int
aq100x_power_down(struct cphy *phy, int off)
{
	int err, wait = 500;
	unsigned int v;

	err = t3_mdio_change_bits(phy, MDIO_DEV_PMA_PMD, MII_BMCR, BMCR_PDOWN,
	    off ? BMCR_PDOWN : 0);
	if (err || off)
		return (err);

	msleep(300);
	do {
		err = mdio_read(phy, MDIO_DEV_PMA_PMD, MII_BMCR, &v);
		if (err)
			return (err);
		v &= BMCR_RESET;
		if (v)
			msleep(10);
	} while (v && --wait);
	if (v) {
		CH_WARN(phy->adapter, "PHY%d: power-up timed out (0x%x).\n",
		    phy->addr, v);
		return (ETIMEDOUT);
	}

	return (0);
}

static int
aq100x_autoneg_enable(struct cphy *phy)
{
	int err;

	err = aq100x_power_down(phy, 0);
	if (!err)
		err = t3_mdio_change_bits(phy, MDIO_DEV_ANEG, MII_BMCR,
		    BMCR_RESET, BMCR_ANENABLE | BMCR_ANRESTART);

	return (err);
}

static int
aq100x_autoneg_restart(struct cphy *phy)
{
	return aq100x_autoneg_enable(phy);
}

static int
aq100x_advertise(struct cphy *phy, unsigned int advertise_map)
{
	unsigned int adv;
	int err;

	/* 10G advertisement */
	adv = 0;
	if (advertise_map & ADVERTISED_10000baseT_Full)
		adv |= ADV_10G_FULL;
	err = t3_mdio_change_bits(phy, MDIO_DEV_ANEG, AQ_10G_CTRL,
				  ADV_10G_FULL, adv);
	if (err)
		return (err);

	/* 1G advertisement */
	adv = 0;
	if (advertise_map & ADVERTISED_1000baseT_Full)
		adv |= ADV_1G_FULL;
	if (advertise_map & ADVERTISED_1000baseT_Half)
		adv |= ADV_1G_HALF;
	err = t3_mdio_change_bits(phy, MDIO_DEV_ANEG, AQ_1G_CTRL,
				  ADV_1G_FULL | ADV_1G_HALF, adv);
	if (err)
		return (err);

	/* 100M, pause advertisement */
	adv = 0;
	if (advertise_map & ADVERTISED_100baseT_Half)
		adv |= ADVERTISE_100HALF;
	if (advertise_map & ADVERTISED_100baseT_Full)
		adv |= ADVERTISE_100FULL;
	if (advertise_map & ADVERTISED_Pause)
		adv |= ADVERTISE_PAUSE_CAP;
	if (advertise_map & ADVERTISED_Asym_Pause)
		adv |= ADVERTISE_PAUSE_ASYM;
	err = t3_mdio_change_bits(phy, MDIO_DEV_ANEG, AQ_100M_CTRL, 0xfe0, adv);

	return (err);
}

static int
aq100x_set_loopback(struct cphy *phy, int mmd, int dir, int enable)
{
	return t3_mdio_change_bits(phy, MDIO_DEV_PMA_PMD, MII_BMCR,
				   BMCR_LOOPBACK, enable ? BMCR_LOOPBACK : 0);
}

static int
aq100x_set_speed_duplex(struct cphy *phy, int speed, int duplex)
{
	int err, set;

	if (speed == SPEED_100)
		set = BMCR_SPEED100;
	else if (speed == SPEED_1000)
		set = BMCR_SPEED1000;
	else if (speed == SPEED_10000)
		set = BMCR_SPEED1000 | BMCR_SPEED100;
	else
		return (EINVAL);

	if (duplex != DUPLEX_FULL)
		return (EINVAL);

	err = t3_mdio_change_bits(phy, MDIO_DEV_ANEG, MII_BMCR,
	    BMCR_RESET | BMCR_ANENABLE | BMCR_ANRESTART, 0);
	if (err)
		return (err);

	err = t3_mdio_change_bits(phy, MDIO_DEV_PMA_PMD, MII_BMCR,
	    BMCR_SPEED1000 | BMCR_SPEED100, set);
	if (err)
		return (err);

	return (0);
}

static int
aq100x_get_link_status(struct cphy *phy, int *link_state, int *speed, int *duplex,
		       int *fc)
{
	int err;
	unsigned int v, link = 0;

	err = mdio_read(phy, MDIO_DEV_PMA_PMD, AQ_LINK_STAT, &v);
	if (err)
		return (err);
	if (v == 0xffff || !(v & 1))
		goto done;

	err = mdio_read(phy, MDIO_DEV_ANEG, MII_BMCR, &v);
	if (err)
		return (err);
	if (v & 0x8000)
		goto done;
	if (v & BMCR_ANENABLE) {

		err = mdio_read(phy, MDIO_DEV_ANEG, 1, &v);
		if (err)
			return (err);
		if ((v & 0x20) == 0)
			goto done;

		err = mdio_read(phy, MDIO_DEV_ANEG, AQ_ANEG_STAT, &v);
		if (err)
			return (err);

		if (speed) {
			switch (v & 0x6) {
			case 0x6: *speed = SPEED_10000;
				break;
			case 0x4: *speed = SPEED_1000;
				break;
			case 0x2: *speed = SPEED_100;
				break;
			case 0x0: *speed = SPEED_10;
				break;
			}
		}

		if (duplex)
			*duplex = v & 1 ? DUPLEX_FULL : DUPLEX_HALF;

		if (fc) {
			unsigned int lpa, adv;
			err = mdio_read(phy, MDIO_DEV_ANEG, 0x13, &lpa);
			if (!err)
				err = mdio_read(phy, MDIO_DEV_ANEG,
				    AQ_100M_CTRL, &adv);
			if (err)
				return err;

			if (lpa & adv & ADVERTISE_PAUSE_CAP)
				*fc = PAUSE_RX | PAUSE_TX;
			else if (lpa & ADVERTISE_PAUSE_CAP &&
			    lpa & ADVERTISE_PAUSE_ASYM &&
			    adv & ADVERTISE_PAUSE_ASYM)
				*fc = PAUSE_TX;
			else if (lpa & ADVERTISE_PAUSE_ASYM &&
			    adv & ADVERTISE_PAUSE_CAP)
				*fc = PAUSE_RX;
			else
				*fc = 0;
		}

	} else {
		err = mdio_read(phy, MDIO_DEV_PMA_PMD, MII_BMCR, &v);
		if (err)
			return (err);

		v &= BMCR_SPEED1000 | BMCR_SPEED100;
		if (speed) {
			if (v == (BMCR_SPEED1000 | BMCR_SPEED100))
				*speed = SPEED_10000;
			else if (v == BMCR_SPEED1000)
				*speed = SPEED_1000;
			else if (v == BMCR_SPEED100)
				*speed = SPEED_100;
			else
				*speed = SPEED_10;
		}

		if (duplex)
			*duplex = DUPLEX_FULL;
	}

	link = 1;
done:
	if (link_state)
		*link_state = link ? PHY_LINK_UP : PHY_LINK_DOWN;
	return (0);
}

static struct cphy_ops aq100x_ops = {
	.reset             = aq100x_reset,
	.intr_enable       = aq100x_intr_enable,
	.intr_disable      = aq100x_intr_disable,
	.intr_clear        = aq100x_intr_clear,
	.intr_handler      = aq100x_intr_handler,
	.autoneg_enable    = aq100x_autoneg_enable,
	.autoneg_restart   = aq100x_autoneg_restart,
	.advertise         = aq100x_advertise,
	.set_loopback      = aq100x_set_loopback,
	.set_speed_duplex  = aq100x_set_speed_duplex,
	.get_link_status   = aq100x_get_link_status,
	.power_down        = aq100x_power_down,
};

int
t3_aq100x_phy_prep(pinfo_t *pinfo, int phy_addr,
		       const struct mdio_ops *mdio_ops)
{
	struct cphy *phy = &pinfo->phy;
	unsigned int v, v2, gpio, wait;
	int err;
	adapter_t *adapter = pinfo->adapter;

	cphy_init(&pinfo->phy, adapter, pinfo, phy_addr, &aq100x_ops, mdio_ops,
		  SUPPORTED_1000baseT_Full | SUPPORTED_10000baseT_Full |
		  SUPPORTED_TP | SUPPORTED_Autoneg | SUPPORTED_AUI |
		  SUPPORTED_MISC_IRQ, "1000/10GBASE-T");

	/*
	 * Hard reset the PHY.
	 */
	gpio = phy_addr ? F_GPIO10_OUT_VAL : F_GPIO6_OUT_VAL;
	t3_set_reg_field(adapter, A_T3DBG_GPIO_EN, gpio, 0);
	msleep(1);
	t3_set_reg_field(adapter, A_T3DBG_GPIO_EN, gpio, gpio);

	/*
	 * Give it enough time to load the firmware and get ready for mdio.
	 */
	msleep(1000);
	wait = 500; /* in 10ms increments */
	do {
		err = mdio_read(phy, MDIO_DEV_PMA_PMD, MII_BMCR, &v);
		if (err || v == 0xffff) {

			/* Allow prep_adapter to succeed when ffff is read */

			CH_WARN(adapter, "PHY%d: reset failed (0x%x, 0x%x).\n",
				phy_addr, err, v);
			goto done;
		}

		v &= BMCR_RESET;
		if (v)
			msleep(10);
	} while (v && --wait);
	if (v) {
		CH_WARN(adapter, "PHY%d: reset timed out (0x%x).\n",
			phy_addr, v);

		goto done; /* let prep_adapter succeed */
	}

	/* Firmware version check. */
	(void) mdio_read(phy, MDIO_DEV_VEND1, AQ_FW_VERSION, &v);
	if (v < 0x115)
		CH_WARN(adapter, "PHY%d: unknown firmware %d.%d\n", phy_addr,
		    v >> 8, v & 0xff);

	/* The PHY should start in really-low-power mode. */
	(void) mdio_read(phy, MDIO_DEV_PMA_PMD, MII_BMCR, &v);
	if ((v & BMCR_PDOWN) == 0)
		CH_WARN(adapter, "PHY%d does not start in low power mode.\n",
			phy_addr);

	/*
	 * Verify XAUI and 1000-X settings, but let prep succeed no matter what.
	 */
	v = v2 = 0;
	(void) mdio_read(phy, MDIO_DEV_XGXS, AQ_XAUI_RX_CFG, &v);
	(void) mdio_read(phy, MDIO_DEV_XGXS, AQ_XAUI_TX_CFG, &v2);
	if (v != 0x1b || v2 != 0x1b)
		CH_WARN(adapter, "PHY%d: incorrect XAUI settings "
		    "(0x%x, 0x%x).\n", phy_addr, v, v2);
	v = 0;
	(void) mdio_read(phy, MDIO_DEV_XGXS, AQ_XAUI_KX_CFG, &v);
	if ((v & 0xf) != 0xf)
		CH_WARN(adapter, "PHY%d: incorrect 1000-X settings "
		    "(0x%x).\n", phy_addr, v);

	(void) aq100x_set_defaults(phy);
done:
	return (err);
}
