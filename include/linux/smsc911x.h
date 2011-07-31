/***************************************************************************
 *
 * Copyright (C) 2004-2008 SMSC
 * Copyright (C) 2005-2008 ARM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 ***************************************************************************/
#ifndef __LINUX_SMSC911X_H__
#define __LINUX_SMSC911X_H__

#include <linux/phy.h>

/* platform_device configuration data, should be assigned to
 * the platform_device's dev.platform_data */
struct smsc911x_platform_config {
	unsigned int irq_polarity;
	unsigned int irq_type;
	unsigned int flags;
	phy_interface_t phy_interface;
	unsigned char mac[6];
};

/* Constants for platform_device irq polarity configuration */
#define SMSC911X_IRQ_POLARITY_ACTIVE_LOW	0
#define SMSC911X_IRQ_POLARITY_ACTIVE_HIGH	1

/* Constants for platform_device irq type configuration */
#define SMSC911X_IRQ_TYPE_OPEN_DRAIN		0
#define SMSC911X_IRQ_TYPE_PUSH_PULL		1

/* Constants for flags */
#define SMSC911X_USE_16BIT 			(BIT(0))
#define SMSC911X_USE_32BIT 			(BIT(1))
#define SMSC911X_FORCE_INTERNAL_PHY		(BIT(2))
#define SMSC911X_FORCE_EXTERNAL_PHY 		(BIT(3))
#define SMSC911X_SAVE_MAC_ADDRESS		(BIT(4))

/*
 * SMSC911X_SWAP_FIFO:
 * Enables software byte swap for fifo data. Should only be used as a
 * "last resort" in the case of big endian mode on boards with incorrectly
 * routed data bus to older devices such as LAN9118. Newer devices such as
 * LAN9221 can handle this in hardware, there are registers to control
 * this swapping but the driver doesn't currently use them.
 */
#define SMSC911X_SWAP_FIFO			(BIT(5))

#endif /* __LINUX_SMSC911X_H__ */
