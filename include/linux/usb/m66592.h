// SPDX-License-Identifier: GPL-2.0
/*
 * M66592 driver platform data
 *
 * Copyright (C) 2009  Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __LINUX_USB_M66592_H
#define __LINUX_USB_M66592_H

#define M66592_PLATDATA_XTAL_12MHZ	0x01
#define M66592_PLATDATA_XTAL_24MHZ	0x02
#define M66592_PLATDATA_XTAL_48MHZ	0x03

struct m66592_platdata {
	/* one = on chip controller, zero = external controller */
	unsigned	on_chip:1;

	/* one = big endian, zero = little endian */
	unsigned	endian:1;

	/* (external controller only) M66592_PLATDATA_XTAL_nnMHZ */
	unsigned	xtal:2;

	/* (external controller only) one = 3.3V, zero = 1.5V */
	unsigned	vif:1;

	/* (external controller only) set one = WR0_N shorted to WR1_N */
	unsigned	wr0_shorted_to_wr1:1;
};

#endif /* __LINUX_USB_M66592_H */

