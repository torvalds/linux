/*
 * include/net/dsa.h - Driver for Distributed Switch Architecture switch chips
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_NET_DSA_H
#define __LINUX_NET_DSA_H

#define DSA_MAX_PORTS	12

struct dsa_platform_data {
	/*
	 * Reference to a Linux network interface that connects
	 * to the switch chip.
	 */
	struct device	*netdev;

	/*
	 * How to access the switch configuration registers, and
	 * the names of the switch ports (use "cpu" to designate
	 * the switch port that the cpu is connected to).
	 */
	struct device	*mii_bus;
	int		sw_addr;
	char		*port_names[DSA_MAX_PORTS];
};


#endif
