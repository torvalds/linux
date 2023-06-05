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

#endif /* __LINUX_SMSCPHY_H__ */
