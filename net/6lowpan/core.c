/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 * (C) 2015 Pengutronix, Alexander Aring <aar@pengutronix.de>
 */

#include <net/6lowpan.h>

void lowpan_netdev_setup(struct net_device *dev, enum lowpan_lltypes lltype)
{
	lowpan_priv(dev)->lltype = lltype;
}
EXPORT_SYMBOL(lowpan_netdev_setup);
