/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_XILINX_LL_TEMAC_H
#define __LINUX_XILINX_LL_TEMAC_H

#include <linux/if_ether.h>
#include <linux/phy.h>

struct ll_temac_platform_data {
	bool txcsum;		/* Enable/disable TX checksum */
	bool rxcsum;		/* Enable/disable RX checksum */
	u8 mac_addr[ETH_ALEN];	/* MAC address (6 bytes) */
	/* Clock frequency for input to MDIO clock generator */
	u32 mdio_clk_freq;
	unsigned long long mdio_bus_id; /* Unique id for MDIO bus */
	int phy_addr;		/* Address of the PHY to connect to */
	phy_interface_t phy_interface; /* PHY interface mode */
	bool reg_little_endian;	/* Little endian TEMAC register access  */
	bool dma_little_endian;	/* Little endian DMA register access  */
};

#endif /* __LINUX_XILINX_LL_TEMAC_H */
