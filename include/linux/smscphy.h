/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SMSCPHY_H__
#define __LINUX_SMSCPHY_H__

#define MII_LAN83C185_ISF 29 /* Interrupt Source Flags */
#define MII_LAN83C185_IM  30 /* Interrupt Mask */
#define MII_LAN83C185_CTRL_STATUS 17 /* Mode/Status Register */
#define MII_LAN83C185_SPECIAL_MODES 18 /* Special Modes Register */

#define MII_LAN83C185_ISF_INT1 (1<<1) /* Auto-Negotiation Page Received */
#define MII_LAN83C185_ISF_INT2 (1<<2) /* Parallel Detection Fault */
#define MII_LAN83C185_ISF_INT3 (1<<3) /* Auto-Negotiation LP Ack */
#define MII_LAN83C185_ISF_INT4 (1<<4) /* Link Down */
#define MII_LAN83C185_ISF_INT5 (1<<5) /* Remote Fault Detected */
#define MII_LAN83C185_ISF_INT6 (1<<6) /* Auto-Negotiation complete */
#define MII_LAN83C185_ISF_INT7 (1<<7) /* ENERGYON */

#define MII_LAN83C185_ISF_INT_ALL (0x0e)

#define MII_LAN83C185_ISF_INT_PHYLIB_EVENTS \
	(MII_LAN83C185_ISF_INT6 | MII_LAN83C185_ISF_INT4 | \
	 MII_LAN83C185_ISF_INT7)

#define MII_LAN83C185_EDPWRDOWN (1 << 13) /* EDPWRDOWN */
#define MII_LAN83C185_ENERGYON  (1 << 1)  /* ENERGYON */

#define MII_LAN83C185_MODE_MASK      0xE0
#define MII_LAN83C185_MODE_POWERDOWN 0xC0 /* Power Down mode */
#define MII_LAN83C185_MODE_ALL       0xE0 /* All capable mode */

int smsc_phy_config_intr(struct phy_device *phydev);
irqreturn_t smsc_phy_handle_interrupt(struct phy_device *phydev);
int smsc_phy_config_init(struct phy_device *phydev);
int lan87xx_read_status(struct phy_device *phydev);
int smsc_phy_get_tunable(struct phy_device *phydev,
			 struct ethtool_tunable *tuna, void *data);
int smsc_phy_set_tunable(struct phy_device *phydev,
			 struct ethtool_tunable *tuna, const void *data);
int smsc_phy_probe(struct phy_device *phydev);

#define MII_LAN874X_PHY_MMD_WOL_WUCSR		0x8010
#define MII_LAN874X_PHY_MMD_WOL_WUF_CFGA	0x8011
#define MII_LAN874X_PHY_MMD_WOL_WUF_CFGB	0x8012
#define MII_LAN874X_PHY_MMD_WOL_WUF_MASK0	0x8021
#define MII_LAN874X_PHY_MMD_WOL_WUF_MASK1	0x8022
#define MII_LAN874X_PHY_MMD_WOL_WUF_MASK2	0x8023
#define MII_LAN874X_PHY_MMD_WOL_WUF_MASK3	0x8024
#define MII_LAN874X_PHY_MMD_WOL_WUF_MASK4	0x8025
#define MII_LAN874X_PHY_MMD_WOL_WUF_MASK5	0x8026
#define MII_LAN874X_PHY_MMD_WOL_WUF_MASK6	0x8027
#define MII_LAN874X_PHY_MMD_WOL_WUF_MASK7	0x8028
#define MII_LAN874X_PHY_MMD_WOL_RX_ADDRA	0x8061
#define MII_LAN874X_PHY_MMD_WOL_RX_ADDRB	0x8062
#define MII_LAN874X_PHY_MMD_WOL_RX_ADDRC	0x8063
#define MII_LAN874X_PHY_MMD_MCFGR		0x8064

#define MII_LAN874X_PHY_PME1_SET		(2 << 13)
#define MII_LAN874X_PHY_PME2_SET		(2 << 11)
#define MII_LAN874X_PHY_PME_SELF_CLEAR		BIT(9)
#define MII_LAN874X_PHY_WOL_PFDA_FR		BIT(7)
#define MII_LAN874X_PHY_WOL_WUFR		BIT(6)
#define MII_LAN874X_PHY_WOL_MPR			BIT(5)
#define MII_LAN874X_PHY_WOL_BCAST_FR		BIT(4)
#define MII_LAN874X_PHY_WOL_PFDAEN		BIT(3)
#define MII_LAN874X_PHY_WOL_WUEN		BIT(2)
#define MII_LAN874X_PHY_WOL_MPEN		BIT(1)
#define MII_LAN874X_PHY_WOL_BCSTEN		BIT(0)

#define MII_LAN874X_PHY_WOL_FILTER_EN		BIT(15)
#define MII_LAN874X_PHY_WOL_FILTER_MCASTTEN	BIT(9)
#define MII_LAN874X_PHY_WOL_FILTER_BCSTEN	BIT(8)

#define MII_LAN874X_PHY_PME_SELF_CLEAR_DELAY	0x1000 /* 81 milliseconds */

#endif /* __LINUX_SMSCPHY_H__ */
