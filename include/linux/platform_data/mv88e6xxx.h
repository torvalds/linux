/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DSA_MV88E6XXX_H
#define __DSA_MV88E6XXX_H

#include <linux/platform_data/dsa.h>

struct dsa_mv88e6xxx_pdata {
	/* Must be first, such that dsa_register_switch() can access this
	 * without gory pointer manipulations
	 */
	struct dsa_chip_data cd;
	const char *compatible;
	unsigned int enabled_ports;
	struct net_device *netdev;
	u32 eeprom_len;
	int irq;
};

#endif
