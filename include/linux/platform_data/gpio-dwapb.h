/*
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef GPIO_DW_APB_H
#define GPIO_DW_APB_H

struct dwapb_port_property {
	struct fwnode_handle *fwnode;
	unsigned int	idx;
	unsigned int	ngpio;
	unsigned int	gpio_base;
	int		irq[32];
	bool		has_irq;
	bool		irq_shared;
};

struct dwapb_platform_data {
	struct dwapb_port_property *properties;
	unsigned int nports;
};

#endif
