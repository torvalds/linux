/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PLATFORM_DATA_ETH_EP93XX
#define _LINUX_PLATFORM_DATA_ETH_EP93XX

struct ep93xx_eth_data {
	unsigned char	dev_addr[6];
	unsigned char	phy_id;
};

#endif
