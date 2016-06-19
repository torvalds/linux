/*
 * TI DaVinci EMAC platform support
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef _LINUX_DAVINCI_EMAC_H
#define _LINUX_DAVINCI_EMAC_H

#include <linux/if_ether.h>
#include <linux/nvmem-consumer.h>

struct mdio_platform_data {
	unsigned long		bus_freq;
};

struct emac_platform_data {
	char mac_addr[ETH_ALEN];
	u32 ctrl_reg_offset;
	u32 ctrl_mod_reg_offset;
	u32 ctrl_ram_offset;
	u32 hw_ram_addr;
	u32 ctrl_ram_size;

	/*
	 * phy_id can be one of the following:
	 *   - NULL		: use the first phy on the bus,
	 *   - ""		: force to 100/full, no mdio control
	 *   - "<bus>:<addr>"	: use the specified bus and phy
	 */
	const char *phy_id;

	u8 rmii_en;
	u8 version;
	bool no_bd_ram;
	void (*interrupt_enable) (void);
	void (*interrupt_disable) (void);
};

enum {
	EMAC_VERSION_1,	/* DM644x */
	EMAC_VERSION_2,	/* DM646x */
};

void davinci_get_mac_addr(struct nvmem_device *nvmem, void *context);
#endif
