/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLATFORM_DATA_ETH_IXP4XX
#define __PLATFORM_DATA_ETH_IXP4XX

#include <linux/types.h>

#define IXP4XX_ETH_NPEA		0x00
#define IXP4XX_ETH_NPEB		0x10
#define IXP4XX_ETH_NPEC		0x20

/* Information about built-in Ethernet MAC interfaces */
struct eth_plat_info {
	u8 phy;		/* MII PHY ID, 0 - 31 */
	u8 rxq;		/* configurable, currently 0 - 31 only */
	u8 txreadyq;
	u8 hwaddr[6];
};

#endif
