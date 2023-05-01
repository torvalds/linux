/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2019 NXP */

#ifndef _FSL_ENETC_MDIO_H_
#define _FSL_ENETC_MDIO_H_

#include <linux/phy.h>

/* PCS registers */
#define ENETC_PCS_LINK_TIMER1			0x12
#define ENETC_PCS_LINK_TIMER1_VAL		0x06a0
#define ENETC_PCS_LINK_TIMER2			0x13
#define ENETC_PCS_LINK_TIMER2_VAL		0x0003
#define ENETC_PCS_IF_MODE			0x14
#define ENETC_PCS_IF_MODE_SGMII_EN		BIT(0)
#define ENETC_PCS_IF_MODE_USE_SGMII_AN		BIT(1)
#define ENETC_PCS_IF_MODE_SGMII_SPEED(x)	(((x) << 2) & GENMASK(3, 2))
#define ENETC_PCS_IF_MODE_DUPLEX_HALF		BIT(3)

/* Not a mistake, the SerDes PLL needs to be set at 3.125 GHz by Reset
 * Configuration Word (RCW, outside Linux control) for 2.5G SGMII mode. The PCS
 * still thinks it's at gigabit.
 */
enum enetc_pcs_speed {
	ENETC_PCS_SPEED_10	= 0,
	ENETC_PCS_SPEED_100	= 1,
	ENETC_PCS_SPEED_1000	= 2,
	ENETC_PCS_SPEED_2500	= 2,
};

struct enetc_hw;

struct enetc_mdio_priv {
	struct enetc_hw *hw;
	int mdio_base;
};

#if IS_REACHABLE(CONFIG_FSL_ENETC_MDIO)

int enetc_mdio_read_c22(struct mii_bus *bus, int phy_id, int regnum);
int enetc_mdio_write_c22(struct mii_bus *bus, int phy_id, int regnum,
			 u16 value);
int enetc_mdio_read_c45(struct mii_bus *bus, int phy_id, int devad, int regnum);
int enetc_mdio_write_c45(struct mii_bus *bus, int phy_id, int devad, int regnum,
			 u16 value);
struct enetc_hw *enetc_hw_alloc(struct device *dev, void __iomem *port_regs);

#else

static inline int enetc_mdio_read_c22(struct mii_bus *bus, int phy_id,
				      int regnum)
{ return -EINVAL; }
static inline int enetc_mdio_write_c22(struct mii_bus *bus, int phy_id,
				       int regnum, u16 value)
{ return -EINVAL; }
static inline int enetc_mdio_read_c45(struct mii_bus *bus, int phy_id,
				      int devad, int regnum)
{ return -EINVAL; }
static inline int enetc_mdio_write_c45(struct mii_bus *bus, int phy_id,
				       int devad, int regnum, u16 value)
{ return -EINVAL; }
struct enetc_hw *enetc_hw_alloc(struct device *dev, void __iomem *port_regs)
{ return ERR_PTR(-EINVAL); }

#endif

#endif
