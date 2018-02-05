/*
 * Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef _DT_BINDINGS_DRAM_ROCKCHIP_RK322X_H
#define _DT_BINDINGS_DRAM_ROCKCHIP_RK322X_H

#define DDR3_DS_34ohm		(1 << 1)
#define DDR3_DS_40ohm		(0x0)

#define LP2_DS_34ohm		(0x1)
#define LP2_DS_40ohm		(0x2)
#define LP2_DS_48ohm		(0x3)
#define LP2_DS_60ohm		(0x4)
#define LP2_DS_68_6ohm		(0x5)/* optional */
#define LP2_DS_80ohm		(0x6)
#define LP2_DS_120ohm		(0x7)/* optional */

#define LP3_DS_34ohm		(0x1)
#define LP3_DS_40ohm		(0x2)
#define LP3_DS_48ohm		(0x3)
#define LP3_DS_60ohm		(0x4)
#define LP3_DS_80ohm		(0x6)
#define LP3_DS_34D_40U		(0x9)
#define LP3_DS_40D_48U		(0xa)
#define LP3_DS_34D_48U		(0xb)

#define DDR3_ODT_DIS		(0)
#define DDR3_ODT_40ohm		((1 << 2) | (1 << 6))
#define DDR3_ODT_60ohm		(1 << 2)
#define DDR3_ODT_120ohm		(1 << 6)

#define LP3_ODT_DIS		(0)
#define LP3_ODT_60ohm		(1)
#define LP3_ODT_120ohm		(2)
#define LP3_ODT_240ohm		(3)

#define PHY_DDR3_RON_RTT_DISABLE	(0)
#define PHY_DDR3_RON_RTT_451ohm		(1)
#define PHY_DDR3_RON_RTT_225ohm		(2)
#define PHY_DDR3_RON_RTT_150ohm		(3)
#define PHY_DDR3_RON_RTT_112ohm		(4)
#define PHY_DDR3_RON_RTT_90ohm		(5)
#define PHY_DDR3_RON_RTT_75ohm		(6)
#define PHY_DDR3_RON_RTT_64ohm		(7)
#define PHY_DDR3_RON_RTT_56ohm		(16)
#define PHY_DDR3_RON_RTT_50ohm		(17)
#define PHY_DDR3_RON_RTT_45ohm		(18)
#define PHY_DDR3_RON_RTT_41ohm		(19)
#define PHY_DDR3_RON_RTT_37ohm		(20)
#define PHY_DDR3_RON_RTT_34ohm		(21)
#define PHY_DDR3_RON_RTT_33ohm		(22)
#define PHY_DDR3_RON_RTT_30ohm		(23)
#define PHY_DDR3_RON_RTT_28ohm		(24)
#define PHY_DDR3_RON_RTT_26ohm		(25)
#define PHY_DDR3_RON_RTT_25ohm		(26)
#define PHY_DDR3_RON_RTT_23ohm		(27)
#define PHY_DDR3_RON_RTT_22ohm		(28)
#define PHY_DDR3_RON_RTT_21ohm		(29)
#define PHY_DDR3_RON_RTT_20ohm		(30)
#define PHY_DDR3_RON_RTT_19ohm		(31)

#define PHY_LP23_RON_RTT_DISABLE	(0)
#define PHY_LP23_RON_RTT_480ohm		(1)
#define PHY_LP23_RON_RTT_240ohm		(2)
#define PHY_LP23_RON_RTT_160ohm		(3)
#define PHY_LP23_RON_RTT_120ohm		(4)
#define PHY_LP23_RON_RTT_96ohm		(5)
#define PHY_LP23_RON_RTT_80ohm		(6)
#define PHY_LP23_RON_RTT_68ohm		(7)
#define PHY_LP23_RON_RTT_60ohm		(16)
#define PHY_LP23_RON_RTT_53ohm		(17)
#define PHY_LP23_RON_RTT_48ohm		(18)
#define PHY_LP23_RON_RTT_43ohm		(19)
#define PHY_LP23_RON_RTT_40ohm		(20)
#define PHY_LP23_RON_RTT_37ohm		(21)
#define PHY_LP23_RON_RTT_34ohm		(22)
#define PHY_LP23_RON_RTT_32ohm		(23)
#define PHY_LP23_RON_RTT_30ohm		(24)
#define PHY_LP23_RON_RTT_28ohm		(25)
#define PHY_LP23_RON_RTT_26ohm		(26)
#define PHY_LP23_RON_RTT_25ohm		(27)
#define PHY_LP23_RON_RTT_24ohm		(28)
#define PHY_LP23_RON_RTT_22ohm		(29)
#define PHY_LP23_RON_RTT_21ohm		(30)
#define PHY_LP23_RON_RTT_20ohm		(31)

#endif /* _DT_BINDINGS_DRAM_ROCKCHIP_RK322X_H */
