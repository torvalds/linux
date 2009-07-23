/*
 * R8A66597 driver platform data
 *
 * Copyright (C) 2009  Renesas Solutions Corp.
 *
 * Author : Yoshihiro Shimoda <shimoda.yoshihiro@renesas.com>
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

#ifndef __LINUX_USB_R8A66597_H
#define __LINUX_USB_R8A66597_H

#define R8A66597_PLATDATA_XTAL_12MHZ	0x01
#define R8A66597_PLATDATA_XTAL_24MHZ	0x02
#define R8A66597_PLATDATA_XTAL_48MHZ	0x03

struct r8a66597_platdata {
	/* This ops can controll port power instead of DVSTCTR register. */
	void (*port_power)(int port, int power);

	/* (external controller only) set R8A66597_PLATDATA_XTAL_nnMHZ */
	unsigned	xtal:2;

	/* set one = 3.3V, set zero = 1.5V */
	unsigned	vif:1;

	/* set one = big endian, set zero = little endian */
	unsigned	endian:1;
};
#endif

