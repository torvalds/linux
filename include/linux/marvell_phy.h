/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MARVELL_PHY_H
#define _MARVELL_PHY_H

/* Mask used for ID comparisons */
#define MARVELL_PHY_ID_MASK		0xfffffff0

/* Known PHY IDs */
#define MARVELL_PHY_ID_88E1101		0x01410c60
#define MARVELL_PHY_ID_88E3082		0x01410c80
#define MARVELL_PHY_ID_88E1112		0x01410c90
#define MARVELL_PHY_ID_88E1111		0x01410cc0
#define MARVELL_PHY_ID_88E1118		0x01410e10
#define MARVELL_PHY_ID_88E1121R		0x01410cb0
#define MARVELL_PHY_ID_88E1145		0x01410cd0
#define MARVELL_PHY_ID_88E1149R		0x01410e50
#define MARVELL_PHY_ID_88E1240		0x01410e30
#define MARVELL_PHY_ID_88E1318S		0x01410e90
#define MARVELL_PHY_ID_88E1340S		0x01410dc0
#define MARVELL_PHY_ID_88E1116R		0x01410e40
#define MARVELL_PHY_ID_88E1510		0x01410dd0
#define MARVELL_PHY_ID_88E1540		0x01410eb0
#define MARVELL_PHY_ID_88E1545		0x01410ea0
#define MARVELL_PHY_ID_88E1548P		0x01410ec0
#define MARVELL_PHY_ID_88E3016		0x01410e60
#define MARVELL_PHY_ID_88X3310		0x002b09a0
#define MARVELL_PHY_ID_88E2110		0x002b09b0
#define MARVELL_PHY_ID_88X2222		0x01410f10
#define MARVELL_PHY_ID_88Q2110		0x002b0980
#define MARVELL_PHY_ID_88Q2220		0x002b0b20

/* Marvel 88E1111 in Finisar SFP module with modified PHY ID */
#define MARVELL_PHY_ID_88E1111_FINISAR	0x01ff0cc0

/* ID from 88E6020, assumed to be the same for the whole 6250 family */
#define MARVELL_PHY_ID_88E6250_FAMILY	0x01410db0
/* These Ethernet switch families contain embedded PHYs, but they do
 * not have a model ID. So the switch driver traps reads to the ID2
 * register and returns the switch family ID
 */
#define MARVELL_PHY_ID_88E6341_FAMILY	0x01410f41
#define MARVELL_PHY_ID_88E6390_FAMILY	0x01410f90
#define MARVELL_PHY_ID_88E6393_FAMILY	0x002b0b9b

#define MARVELL_PHY_FAMILY_ID(id)	((id) >> 4)

/* struct phy_device dev_flags definitions */
#define MARVELL_PHY_M1145_FLAGS_RESISTANCE	0x00000001
#define MARVELL_PHY_M1118_DNS323_LEDS		0x00000002
#define MARVELL_PHY_LED0_LINK_LED1_ACTIVE	0x00000004

#endif /* _MARVELL_PHY_H */
