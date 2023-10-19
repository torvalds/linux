/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PLATFORM_DATA_BCMGENET_H__
#define __LINUX_PLATFORM_DATA_BCMGENET_H__

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/phy.h>

struct bcmgenet_platform_data {
	bool		mdio_enabled;
	phy_interface_t	phy_interface;
	int		phy_address;
	int		phy_speed;
	int		phy_duplex;
	u8		mac_address[ETH_ALEN];
	int		genet_version;
};

#endif
