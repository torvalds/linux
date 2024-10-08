/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __QCA8337_H__
#define __QCA8337_H__

#define BITS(_s, _n)	(((1UL << (_n)) - 1) << (_s))

#define QCA8337_PHY_ID		0x004dd036
#define ATH8030_PHY_ID		0x004dd076
#define ATH8031_PHY_ID		0x004dd074
#define ATH8035_PHY_ID		0x004dd072
#define QCA8337_ID_QCA8337	0x13
#define QCA8337_NUM_PORTS					7
/* Make sure that port0 is the cpu port */
#define QCA8337_CPU_PORT					0
/* size of the vlan table */
#define QCA8337_MAX_VLANS					128
#define QCA8337_NUM_PHYS		5

#define ADVERTISE_MULTI_PORT_PREFER	0x0400

#define QCA8337_AT803X_INTR_ENABLE			0x12
#define QCA8337_AT803X_INTR_STATUS			0x13
#define QCA8337_AT803X_SMART_SPEED			0x14
#define QCA8337_AT803X_LED_CONTROL			0x18
#define QCA8337_AT803X_WOL_ENABLE			0x01
#define QCA8337_AT803X_DEVICE_ADDR			0x03
#define QCA8337_AT803X_LOC_MAC_ADDR_0_15_OFFSET		0x804C
#define QCA8337_AT803X_LOC_MAC_ADDR_16_31_OFFSET	0x804B
#define QCA8337_AT803X_LOC_MAC_ADDR_32_47_OFFSET	0x804A
#define QCA8337_AT803X_MMD_ACCESS_CONTROL		0x0D
#define QCA8337_AT803X_MMD_ACCESS_CONTROL_DATA		0x0E
#define QCA8337_AT803X_FUNC_DATA			0x4003
#define QCA8337_AT803X_INER				0x0012
#define QCA8337_AT803X_INER_INIT			0xec00
#define QCA8337_AT803X_INSR				0x0013
#define QCA8337_AT803X_DEBUG_ADDR			0x1D
#define QCA8337_AT803X_DEBUG_DATA			0x1E
#define QCA8337_AT803X_DEBUG_SYSTEM_MODE_CTRL		0x05
#define QCA8337_AT803X_DEBUG_RGMII_TX_CLK_DLY		BIT(8)

/* MASK_CTRL */
#define QCA8337_REG_MASK_CTRL		0x0000
#define	QCA8337_CTRL_REVISION		BITS(0, 8)
#define	QCA8337_CTRL_REVISION_S		0
#define	QCA8337_CTRL_VERSION		BITS(8, 8)
#define	QCA8337_CTRL_VERSION_S		8
#define	QCA8337_CTRL_RESET			BIT(31)

/* PORT0/1_PAD_CTRL */
#define QCA8337_REG_PAD0_CTRL			0x004
#define QCA8337_REG_PAD5_CTRL			0x008
#define QCA8337_REG_PAD6_CTRL			0x00c
#define   QCA8337_PAD_MAC_MII_RXCLK_SEL		BIT(0)
#define   QCA8337_PAD_MAC_MII_TXCLK_SEL		BIT(1)
#define   QCA8337_PAD_MAC_MII_EN			BIT(2)
#define   QCA8337_PAD_MAC_GMII_RXCLK_SEL		BIT(4)
#define   QCA8337_PAD_MAC_GMII_TXCLK_SEL		BIT(5)
#define   QCA8337_PAD_MAC_GMII_EN		BIT(6)
#define   QCA8337_PAD_SGMII_EN			BIT(7)
#define   QCA8337_PAD_PHY_MII_RXCLK_SEL		BIT(8)
#define   QCA8337_PAD_PHY_MII_TXCLK_SEL		BIT(9)
#define   QCA8337_PAD_PHY_MII_EN			BIT(10)
#define   QCA8337_PAD_PHY_GMII_PIPE_RXCLK_SEL	BIT(11)
#define   QCA8337_PAD_PHY_GMII_RXCLK_SEL		BIT(12)
#define   QCA8337_PAD_PHY_GMII_TXCLK_SEL		BIT(13)
#define   QCA8337_PAD_PHY_GMII_EN		BIT(14)
#define   QCA8337_PAD_PHYX_GMII_EN		BIT(16)
#define   QCA8337_PAD_PHYX_RGMII_EN		BIT(17)
#define   QCA8337_PAD_PHYX_MII_EN		BIT(18)
#define   QCA8337_PAD_RGMII_RXCLK_DELAY_SEL	BITS(20, 2)
#define   QCA8337_PAD_RGMII_RXCLK_DELAY_SEL_S	20
#define   QCA8337_PAD_RGMII_TXCLK_DELAY_SEL	BITS(22, 2)
#define   QCA8337_PAD_RGMII_TXCLK_DELAY_SEL_S	22
#define   QCA8337_PAD_RGMII_RXCLK_DELAY_EN	BIT(24)
#define   QCA8337_PAD_RGMII_TXCLK_DELAY_EN	BIT(25)
#define   QCA8337_PAD_RGMII_EN			BIT(26)

/* PORT_STATUS */
#define QCA8337_REG_PORT_STATUS(_i)		(0x07c + (_i) * 4)
#define   QCA8337_PORT_STATUS_SPEED	    BITS(0, 2)
#define   QCA8337_PORT_STATUS_SPEED_S	    0
#define   QCA8337_PORT_STATUS_TXMAC	    BIT(2)
#define   QCA8337_PORT_STATUS_RXMAC	    BIT(3)
#define   QCA8337_PORT_STATUS_TXFLOW	    BIT(4)
#define   QCA8337_PORT_STATUS_RXFLOW	    BIT(5)
#define   QCA8337_PORT_STATUS_DUPLEX	    BIT(6)
#define   QCA8337_PORT_STATUS_LINK_UP	    BIT(8)
#define   QCA8337_PORT_STATUS_LINK_AUTO	    BIT(9)
#define   QCA8337_PORT_STATUS_LINK_PAUSE     BIT(10)

/* GLOBAL_FW_CTRL0 */
#define QCA8337_REG_GLOBAL_FW_CTRL0			0x620
#define QCA8337_GLOBAL_FW_CTRL0_CPU_PORT_EN		BIT(10)

/* GLOBAL_FW_CTRL1 */
#define QCA8337_REG_GLOBAL_FW_CTRL1			0x624
#define QCA8337_IGMP_JN_L_DP_SH		24
#define QCA8337_BROAD_DP_SHIFT              16
#define QCA8337_MULTI_FLOOD_DP_SH        8
#define QCA8337_UNI_FLOOD_DP_SHIFT          0
#define QCA8337_IGMP_JOIN_LEAVE_DPALL       (0x7f << QCA8337_IGMP_JN_L_DP_SH)
#define QCA8337_BROAD_DPALL                 (0x7f << QCA8337_BROAD_DP_SHIFT)
#define QCA8337_MULTI_FLOOD_DPALL           (0x7f << QCA8337_MULTI_FLOOD_DP_SH)
#define QCA8337_UNI_FLOOD_DPALL             (0x7f << QCA8337_UNI_FLOOD_DP_SHIFT)

/* PWS_REG (POWER_ON_STRIP) */
#define QCA8337_REG_POWER_ON_STRIP		0x010
#define QCA8337_REG_POS_VAL			0x261320
#define   QCA8337_PWS_POWER_ON_SEL	BIT(31)
#define   QCA8337_PWS_LED_OPEN_EN	BIT(24)
#define   QCA8337_PWS_SERDES_AEN	BIT(7)

/* MAC_PWR_SEL*/
#define QCA8337_MAC_PWR_SEL		0x0e4
#define QCA8337_MAC_PWR_SEL_VAL		0xaa545

/* SGMII_CTRL */
#define QCA8337_SGMII_CTRL_REG			0x0e0
#define QCA8337_SGMII_CTRL_VAL			0xc74164de
#define   QCA8337_SGMII_CTRL_MODE_CTRL		BITS(22, 2)
#define   QCA8337_SGMII_CTRL_MODE_CTRL_S		22
#define QCA8337_SGMII_EN_LCKDT                   BIT(0)
#define QCA8337_SGMII_EN_PLL                     BIT(1)
#define QCA8337_SGMII_EN_RX                      BIT(2)
#define QCA8337_SGMII_EN_TX                      BIT(3)
#define QCA8337_SGMII_EN_SD                      BIT(4)
#define QCA8337_SGMII_BW_HIGH                    BIT(6)
#define QCA8337_SGMII_SEL_CLK125M                BIT(7)
#define QCA8337_SGMII_TXDR_CTRL_600mV            BIT(10)
#define QCA8337_SGMII_CDR_BW_8                   BIT(13)
#define QCA8337_SGMII_DIS_AUTO_LPI_25M           BIT(16)
#define QCA8337_SGMII_MODE_CTRL_SGMII_PHY        BIT(22)
#define QCA8337_SGMII_PAUSE_SG_TX_EN_25M         BIT(24)
#define QCA8337_SGMII_ASYM_PAUSE_25M             BIT(25)
#define QCA8337_SGMII_PAUSE_25M                  BIT(26)
#define QCA8337_SGMII_HALF_DUPLEX_25M            BIT(30)
#define QCA8337_SGMII_FULL_DUPLEX_25M            BIT(31)

/* PORT_LOOKUP_CTRL */
#define QCA8337_REG_PORT_LOOKUP(_i)		(0x660 + (_i) * 0xc)
#define   QCA8337_PORT_LOOKUP_MEMBER		BITS(0, 7)
#define   QCA8337_PORT_LOOKUP_IN_MODE		BITS(8, 2)
#define   QCA8337_PORT_LOOKUP_IN_MODE_S		8
#define   QCA8337_PORT_LOOKUP_STATE		BITS(16, 3)
#define   QCA8337_PORT_LOOKUP_STATE_S		16
#define   QCA8337_PORT_LOOKUP_LEARN		BIT(20)

/* PORT_VLAN_CTRL0 */
#define QCA8337_REG_PORT_VLAN0(_i)		(0x420 + (_i) * 0x8)
#define   QCA8337_PORT_VLAN0_DEF_SVID		BITS(0, 12)
#define   QCA8337_PORT_VLAN0_DEF_SVID_S		0
#define   QCA8337_PORT_VLAN0_DEF_CVID		BITS(16, 12)
#define   QCA8337_PORT_VLAN0_DEF_CVID_S		16

/* PORT_VLAN_CTRL1 */
#define QCA8337_REG_PORT_VLAN1(_i)		(0x424 + (_i) * 0x8)
#define   QCA8337_PORT_VLAN1_PORT_VLAN_PROP	BIT(6)
#define   QCA8337_PORT_VLAN1_OUT_MODE		BITS(12, 2)
#define   QCA8337_PORT_VLAN1_OUT_MODE_S		12
#define   QCA8337_PORT_VLAN1_OUT_MODE_UNMOD	0
#define   QCA8337_PORT_VLAN1_OUT_MODE_UNTAG	1
#define   QCA8337_PORT_VLAN1_OUT_MODE_TAG	2
#define   QCA8337_PORT_VLAN1_OUT_MODE_UNTOUCH	3

/* MODULE_EN */
#define QCA8337_REG_MODULE_EN				0x030
#define   QCA8337_MODULE_EN_MIB				BIT(0)

/* MIB */
#define QCA8337_REG_MIB					0x034
#define   QCA8337_MIB_FLUSH				BIT(24)
#define   QCA8337_MIB_CPU_KEEP				BIT(20)
#define   QCA8337_MIB_BUSY				BIT(17)

/* PORT_HEADER_CTRL */
#define QCA8337_REG_PORT_HEADER(_i)		(0x09c + (_i) * 4)
#define QCA8337_PORT_HDR_CTRL_RX_S			2
#define QCA8337_PORT_HDR_CTRL_TX_S			0
#define QCA8337_PORT_HDR_CTRL_ALL			2

/* EEE_CTRL */
#define QCA8337_REG_EEE_CTRL			0x100
#define QCA8337_EEE_CTRL_DISABLE			0x0 /*EEE disable*/

/* VTU_FUNC_REG0  */
#define QCA8337_REG_VTU_FUNC0			0x0610
#define   QCA8337_VTU_FUNC0_EG_MODE		BITS(4, 14)
#define   QCA8337_VTU_FUNC0_EG_MODE_S(_i)	(4 + (_i) * 2)
#define   QCA8337_VTU_FUNC0_EG_MODE_KEEP		0
#define   QCA8337_VTU_FUNC0_EG_MODE_UNTAG	1
#define   QCA8337_VTU_FUNC0_EG_MODE_TAG		2
#define   QCA8337_VTU_FUNC0_EG_MODE_NOT		3
#define   QCA8337_VTU_FUNC0_IVL			BIT(19)
#define   QCA8337_VTU_FUNC0_VALID		BIT(20)

/* VTU_FUNC_REG1 */
#define QCA8337_REG_VTU_FUNC1			0x0614
#define   QCA8337_VTU_FUNC1_OP			BITS(0, 3)
#define   QCA8337_VTU_FUNC1_OP_NOOP		0
#define   QCA8337_VTU_FUNC1_OP_FLUSH		1
#define   QCA8337_VTU_FUNC1_OP_LOAD		2
#define   QCA8337_VTU_FUNC1_OP_PURGE		3
#define   QCA8337_VTU_FUNC1_OP_REMOVE_PORT	4
#define   QCA8337_VTU_FUNC1_OP_GET_NEXT		5
#define   QCA8337_VTU_FUNC1_OP_GET_ONE		6
#define   QCA8337_VTU_FUNC1_FULL			BIT(4)
#define   QCA8337_VTU_FUNC1_PORT			BIT(8, 4)
#define   QCA8337_VTU_FUNC1_PORT_S		8
#define   QCA8337_VTU_FUNC1_VID			BIT(16, 12)
#define   QCA8337_VTU_FUNC1_VID_S		16
#define   QCA8337_VTU_FUNC1_BUSY			BIT(31)

#define	QCA8337_REG_ATU_FUNC			0x60c
#define QCA8337_ATU_FUNC_BUSY			BIT(31)
#define   QCA8337_ATU_FUNC_OP_GET_NEXT		0x6
#define QCA8337_REG_ATU_DATA0			0x600
#define QCA8337_REG_ATU_DATA1			0x604
#define QCA8337_REG_ATU_DATA2			0x608

#define QCA8337_GLOBAL_INT1	0x0024
#define QCA8337_GLOBAL_INT1_MASK 0x002c

/* port speed */
enum {
	QCA8337_PORT_SPEED_10M = 0,
	QCA8337_PORT_SPEED_100M = 1,
	QCA8337_PORT_SPEED_1000M = 2,
	QCA8337_PORT_SPEED_ERR = 3,
};

/* ingress 802.1q mode */
enum {
	QCA8337_IN_PORT_ONLY = 0,
	QCA8337_IN_PORT_FALLBACK = 1,
	QCA8337_IN_VLAN_ONLY = 2,
	QCA8337_IN_SECURE = 3
};

/* egress 802.1q mode */
enum {
	QCA8337_OUT_KEEP = 0,
	QCA8337_OUT_STRIP_VLAN = 1,
	QCA8337_OUT_ADD_VLAN = 2
};

/* port forwarding state */
enum {
	QCA8337_PORT_STATE_DISABLED = 0,
	QCA8337_PORT_STATE_BLOCK = 1,
	QCA8337_PORT_STATE_LISTEN = 2,
	QCA8337_PORT_STATE_LEARN = 3,
	QCA8337_PORT_STATE_FORWARD = 4
};

struct qca8337_priv;

struct qca8337_switch_ops {
	int (*hw_init)(struct qca8337_priv *priv);
	void (*reset_switch)(struct qca8337_priv *priv);

	/* Switch internal register read/write function */
	u32 (*read)(struct qca8337_priv *priv, u32 reg);
	void (*write)(struct qca8337_priv *priv, u32 reg, u32 val);
};

struct port_link_info {
	bool link;
	int speed;
	int duplex;
	int aneg;
	int rx_flow;
	int tx_flow;
};

struct qca8337_priv {
	struct device *dev;
	struct phy_device *phy;
	u8 chip_ver;
	u8 chip_rev;
	u8 cpu_port;
	u8 ports;
	u16 vlans;
	u8 num_phy;
	u32 old_port_status;
	char buf[2048];

	struct qca8337_switch_ops *ops;
	struct regmap *regmap;
};

struct qca8337_mib_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

void qca8337_check(void);

u32 qca8337_read(struct qca8337_priv *priv, u32 reg);
void qca8337_write(struct qca8337_priv *priv, u32 reg, u32 val);
#endif /*__QCA8337_H__*/
