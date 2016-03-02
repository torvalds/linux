/*
 * Driver include for the Inside Secure microread NFC Chip.
 *
 * Copyright (C) 2011 Tieto Poland
 * Copyright (C) 2012 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _MICROREAD_H
#define _MICROREAD_H

#include <linux/i2c.h>

#define MICROREAD_DRIVER_NAME	"microread"

/* board config platform data for microread */
struct microread_nfc_platform_data {
	unsigned int rst_gpio;
	unsigned int irq_gpio;
	unsigned int ioh_gpio;
};

#endif /* _MICROREAD_H */
