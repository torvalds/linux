// SPDX-License-Identifier: GPL-2.0+
/*
 * g_hid.h -- Header file for USB HID gadget driver
 *
 * Copyright (C) 2010 Fabien Chouteau <fabien.chouteau@barco.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __LINUX_USB_G_HID_H
#define __LINUX_USB_G_HID_H

struct hidg_func_descriptor {
	unsigned char		subclass;
	unsigned char		protocol;
	unsigned short		report_length;
	unsigned short		report_desc_length;
	unsigned char		report_desc[];
};

#endif /* __LINUX_USB_G_HID_H */
