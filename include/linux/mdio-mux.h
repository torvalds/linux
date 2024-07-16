/*
 * MDIO bus multiplexer framwork.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011, 2012 Cavium, Inc.
 */
#ifndef __LINUX_MDIO_MUX_H
#define __LINUX_MDIO_MUX_H
#include <linux/device.h>
#include <linux/phy.h>

/* mdio_mux_init() - Initialize a MDIO mux
 * @dev		The device owning the MDIO mux
 * @mux_node	The device node of the MDIO mux
 * @switch_fn	The function called for switching target MDIO child
 * mux_handle	A pointer to a (void *) used internaly by mdio-mux
 * @data	Private data used by switch_fn()
 * @mux_bus	An optional parent bus (Other case are to use parent_bus property)
 */
int mdio_mux_init(struct device *dev,
		  struct device_node *mux_node,
		  int (*switch_fn) (int cur, int desired, void *data),
		  void **mux_handle,
		  void *data,
		  struct mii_bus *mux_bus);

void mdio_mux_uninit(void *mux_handle);

#endif /* __LINUX_MDIO_MUX_H */
