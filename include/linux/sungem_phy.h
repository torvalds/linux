/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUNGEM_PHY_H__
#define __SUNGEM_PHY_H__

struct mii_phy;

/* Operations supported by any kind of PHY */
struct mii_phy_ops
{
	int		(*init)(struct mii_phy *phy);
	int		(*suspend)(struct mii_phy *phy);
	int		(*setup_aneg)(struct mii_phy *phy, u32 advertise);
	int		(*setup_forced)(struct mii_phy *phy, int speed, int fd);
	int		(*poll_link)(struct mii_phy *phy);
	int		(*read_link)(struct mii_phy *phy);
	int		(*enable_fiber)(struct mii_phy *phy, int autoneg);
};

/* Structure used to statically define an mii/gii based PHY */
struct mii_phy_def
{
	u32				phy_id;		/* Concatenated ID1 << 16 | ID2 */
	u32				phy_id_mask;	/* Significant bits */
	u32				features;	/* Ethtool SUPPORTED_* defines */
	int				magic_aneg;	/* Autoneg does all speed test for us */
	const char*			name;
	const struct mii_phy_ops*	ops;
};

enum {
	BCM54XX_COPPER,
	BCM54XX_FIBER,
	BCM54XX_GBIC,
	BCM54XX_SGMII,
	BCM54XX_UNKNOWN,
};

/* An instance of a PHY, partially borrowed from mii_if_info */
struct mii_phy
{
	struct mii_phy_def*	def;
	u32			advertising;
	int			mii_id;

	/* 1: autoneg enabled, 0: disabled */
	int			autoneg;

	/* forced speed & duplex (no autoneg)
	 * partner speed & duplex & pause (autoneg)
	 */
	int			speed;
	int			duplex;
	int			pause;

	/* Provided by host chip */
	struct net_device	*dev;
	int (*mdio_read) (struct net_device *dev, int mii_id, int reg);
	void (*mdio_write) (struct net_device *dev, int mii_id, int reg, int val);
	void			*platform_data;
};

/* Pass in a struct mii_phy with dev, mdio_read and mdio_write
 * filled, the remaining fields will be filled on return
 */
extern int sungem_phy_probe(struct mii_phy *phy, int mii_id);


/* MII definitions missing from mii.h */

#define BMCR_SPD2	0x0040		/* Gigabit enable (bcm54xx)	*/
#define LPA_PAUSE	0x0400

/* More PHY registers (model specific) */

/* MII BCM5201 MULTIPHY interrupt register */
#define MII_BCM5201_INTERRUPT			0x1A
#define MII_BCM5201_INTERRUPT_INTENABLE		0x4000

#define MII_BCM5201_AUXMODE2			0x1B
#define MII_BCM5201_AUXMODE2_LOWPOWER		0x0008

#define MII_BCM5201_MULTIPHY                    0x1E

/* MII BCM5201 MULTIPHY register bits */
#define MII_BCM5201_MULTIPHY_SERIALMODE         0x0002
#define MII_BCM5201_MULTIPHY_SUPERISOLATE       0x0008

/* MII BCM5221 Additional registers */
#define MII_BCM5221_TEST			0x1f
#define MII_BCM5221_TEST_ENABLE_SHADOWS		0x0080
#define MII_BCM5221_SHDOW_AUX_STAT2		0x1b
#define MII_BCM5221_SHDOW_AUX_STAT2_APD		0x0020
#define MII_BCM5221_SHDOW_AUX_MODE4		0x1a
#define MII_BCM5221_SHDOW_AUX_MODE4_IDDQMODE	0x0001
#define MII_BCM5221_SHDOW_AUX_MODE4_CLKLOPWR	0x0004

/* MII BCM5241 Additional registers */
#define MII_BCM5241_SHDOW_AUX_MODE4_STANDBYPWR	0x0008

/* MII BCM5400 1000-BASET Control register */
#define MII_BCM5400_GB_CONTROL			0x09
#define MII_BCM5400_GB_CONTROL_FULLDUPLEXCAP	0x0200

/* MII BCM5400 AUXCONTROL register */
#define MII_BCM5400_AUXCONTROL                  0x18
#define MII_BCM5400_AUXCONTROL_PWR10BASET       0x0004

/* MII BCM5400 AUXSTATUS register */
#define MII_BCM5400_AUXSTATUS                   0x19
#define MII_BCM5400_AUXSTATUS_LINKMODE_MASK     0x0700
#define MII_BCM5400_AUXSTATUS_LINKMODE_SHIFT    8

/* 1000BT control (Marvell & BCM54xx at least) */
#define MII_1000BASETCONTROL			0x09
#define MII_1000BASETCONTROL_FULLDUPLEXCAP	0x0200
#define MII_1000BASETCONTROL_HALFDUPLEXCAP	0x0100

/* Marvell 88E1011 PHY control */
#define MII_M1011_PHY_SPEC_CONTROL		0x10
#define MII_M1011_PHY_SPEC_CONTROL_MANUAL_MDIX	0x20
#define MII_M1011_PHY_SPEC_CONTROL_AUTO_MDIX	0x40

/* Marvell 88E1011 PHY status */
#define MII_M1011_PHY_SPEC_STATUS		0x11
#define MII_M1011_PHY_SPEC_STATUS_1000		0x8000
#define MII_M1011_PHY_SPEC_STATUS_100		0x4000
#define MII_M1011_PHY_SPEC_STATUS_SPD_MASK	0xc000
#define MII_M1011_PHY_SPEC_STATUS_FULLDUPLEX	0x2000
#define MII_M1011_PHY_SPEC_STATUS_RESOLVED	0x0800
#define MII_M1011_PHY_SPEC_STATUS_TX_PAUSE	0x0008
#define MII_M1011_PHY_SPEC_STATUS_RX_PAUSE	0x0004

#endif /* __SUNGEM_PHY_H__ */
