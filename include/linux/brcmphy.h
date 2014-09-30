#ifndef _LINUX_BRCMPHY_H
#define _LINUX_BRCMPHY_H

#define PHY_ID_BCM50610			0x0143bd60
#define PHY_ID_BCM50610M		0x0143bd70
#define PHY_ID_BCM5241			0x0143bc30
#define PHY_ID_BCMAC131			0x0143bc70
#define PHY_ID_BCM5481			0x0143bca0
#define PHY_ID_BCM5482			0x0143bcb0
#define PHY_ID_BCM5411			0x00206070
#define PHY_ID_BCM5421			0x002060e0
#define PHY_ID_BCM5464			0x002060b0
#define PHY_ID_BCM5461			0x002060c0
#define PHY_ID_BCM57780			0x03625d90

#define PHY_ID_BCM7366			0x600d8490
#define PHY_ID_BCM7439			0x600d8480
#define PHY_ID_BCM7445			0x600d8510

#define PHY_BCM_OUI_MASK		0xfffffc00
#define PHY_BCM_OUI_1			0x00206000
#define PHY_BCM_OUI_2			0x0143bc00
#define PHY_BCM_OUI_3			0x03625c00
#define PHY_BCM_OUI_4			0x600d0000
#define PHY_BCM_OUI_5			0x03625e00


#define PHY_BCM_FLAGS_MODE_COPPER	0x00000001
#define PHY_BCM_FLAGS_MODE_1000BX	0x00000002
#define PHY_BCM_FLAGS_INTF_SGMII	0x00000010
#define PHY_BCM_FLAGS_INTF_XAUI		0x00000020
#define PHY_BRCM_WIRESPEED_ENABLE	0x00000100
#define PHY_BRCM_AUTO_PWRDWN_ENABLE	0x00000200
#define PHY_BRCM_RX_REFCLK_UNUSED	0x00000400
#define PHY_BRCM_STD_IBND_DISABLE	0x00000800
#define PHY_BRCM_EXT_IBND_RX_ENABLE	0x00001000
#define PHY_BRCM_EXT_IBND_TX_ENABLE	0x00002000
#define PHY_BRCM_CLEAR_RGMII_MODE	0x00004000
#define PHY_BRCM_DIS_TXCRXC_NOENRGY	0x00008000
/* Broadcom BCM7xxx specific workarounds */
#define PHY_BRCM_100MBPS_WAR		0x00010000
#define PHY_BCM_FLAGS_VALID		0x80000000

/* Broadcom BCM54XX register definitions, common to most Broadcom PHYs */
#define MII_BCM54XX_ECR		0x10	/* BCM54xx extended control register */
#define MII_BCM54XX_ECR_IM	0x1000	/* Interrupt mask */
#define MII_BCM54XX_ECR_IF	0x0800	/* Interrupt force */

#define MII_BCM54XX_ESR		0x11	/* BCM54xx extended status register */
#define MII_BCM54XX_ESR_IS	0x1000	/* Interrupt status */

#define MII_BCM54XX_EXP_DATA	0x15	/* Expansion register data */
#define MII_BCM54XX_EXP_SEL	0x17	/* Expansion register select */
#define MII_BCM54XX_EXP_SEL_SSD	0x0e00	/* Secondary SerDes select */
#define MII_BCM54XX_EXP_SEL_ER	0x0f00	/* Expansion register select */

#define MII_BCM54XX_AUX_CTL	0x18	/* Auxiliary control register */
#define MII_BCM54XX_ISR		0x1a	/* BCM54xx interrupt status register */
#define MII_BCM54XX_IMR		0x1b	/* BCM54xx interrupt mask register */
#define MII_BCM54XX_INT_CRCERR	0x0001	/* CRC error */
#define MII_BCM54XX_INT_LINK	0x0002	/* Link status changed */
#define MII_BCM54XX_INT_SPEED	0x0004	/* Link speed change */
#define MII_BCM54XX_INT_DUPLEX	0x0008	/* Duplex mode changed */
#define MII_BCM54XX_INT_LRS	0x0010	/* Local receiver status changed */
#define MII_BCM54XX_INT_RRS	0x0020	/* Remote receiver status changed */
#define MII_BCM54XX_INT_SSERR	0x0040	/* Scrambler synchronization error */
#define MII_BCM54XX_INT_UHCD	0x0080	/* Unsupported HCD negotiated */
#define MII_BCM54XX_INT_NHCD	0x0100	/* No HCD */
#define MII_BCM54XX_INT_NHCDL	0x0200	/* No HCD link */
#define MII_BCM54XX_INT_ANPR	0x0400	/* Auto-negotiation page received */
#define MII_BCM54XX_INT_LC	0x0800	/* All counters below 128 */
#define MII_BCM54XX_INT_HC	0x1000	/* Counter above 32768 */
#define MII_BCM54XX_INT_MDIX	0x2000	/* MDIX status change */
#define MII_BCM54XX_INT_PSERR	0x4000	/* Pair swap error */

#define MII_BCM54XX_SHD		0x1c	/* 0x1c shadow registers */
#define MII_BCM54XX_SHD_WRITE	0x8000
#define MII_BCM54XX_SHD_VAL(x)	((x & 0x1f) << 10)
#define MII_BCM54XX_SHD_DATA(x)	((x & 0x3ff) << 0)

/*
 * AUXILIARY CONTROL SHADOW ACCESS REGISTERS.  (PHY REG 0x18)
 */
#define MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL	0x0000
#define MII_BCM54XX_AUXCTL_ACTL_TX_6DB		0x0400
#define MII_BCM54XX_AUXCTL_ACTL_SMDSP_ENA	0x0800

#define MII_BCM54XX_AUXCTL_MISC_WREN	0x8000
#define MII_BCM54XX_AUXCTL_MISC_FORCE_AMDIX	0x0200
#define MII_BCM54XX_AUXCTL_MISC_RDSEL_MISC	0x7000
#define MII_BCM54XX_AUXCTL_SHDWSEL_MISC	0x0007

#define MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL	0x0000

#endif /* _LINUX_BRCMPHY_H */
