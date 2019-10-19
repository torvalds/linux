/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MARVELL_PHY_H
#define _MARVELL_PHY_H

/* Mask used for ID comparisons */
#define MARVELL_PHY_ID_MASK		0xfffffff0

/* Known PHY IDs */
#define MARVELL_PHY_ID_88E1101		0x01410c60
#define MARVELL_PHY_ID_88E1112		0x01410c90
#define MARVELL_PHY_ID_88E1111		0x01410cc0
#define MARVELL_PHY_ID_88E1118		0x01410e10
#define MARVELL_PHY_ID_88E1121R		0x01410cb0
#define MARVELL_PHY_ID_88E1145		0x01410cd0
#define MARVELL_PHY_ID_88E1149R		0x01410e50
#define MARVELL_PHY_ID_88E1240		0x01410e30
#define MARVELL_PHY_ID_88E1318S		0x01410e90
#define MARVELL_PHY_ID_88E1116R		0x01410e40
#define MARVELL_PHY_ID_88E1510		0x01410dd0
#define MARVELL_PHY_ID_88E1540		0x01410eb0
#define MARVELL_PHY_ID_88E1545		0x01410ea0
#define MARVELL_PHY_ID_88E3016		0x01410e60
#define MARVELL_PHY_ID_88X3310		0x002b09a0
#define MARVELL_PHY_ID_88E2110		0x002b09b0

/* The MV88e6390 Ethernet switch contains embedded PHYs. These PHYs do
 * not have a model ID. So the switch driver traps reads to the ID2
 * register and returns the switch family ID
 */
#define MARVELL_PHY_ID_88E6390		0x01410f90

#define MARVELL_PHY_FAMILY_ID(id)	((id) >> 4)

/* struct phy_device dev_flags definitions */
#define MARVELL_PHY_M1145_FLAGS_RESISTANCE	0x00000001
#define MARVELL_PHY_M1118_DNS323_LEDS		0x00000002
#define MARVELL_PHY_LED0_LINK_LED1_ACTIVE	0x00000004

#endif /* _MARVELL_PHY_H */
