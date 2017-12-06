// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __USB_CORE_OHCI_PDRIVER_H
#define __USB_CORE_OHCI_PDRIVER_H

/**
 * struct usb_ohci_pdata - platform_data for generic ohci driver
 *
 * @big_endian_desc:	BE descriptors
 * @big_endian_mmio:	BE registers
 * @no_big_frame_no:	no big endian frame_no shift
 * @num_ports:		number of ports
 *
 * These are general configuration options for the OHCI controller. All of
 * these options are activating more or less workarounds for some hardware.
 */
struct usb_ohci_pdata {
	unsigned	big_endian_desc:1;
	unsigned	big_endian_mmio:1;
	unsigned	no_big_frame_no:1;
	unsigned int	num_ports;

	/* Turn on all power and clocks */
	int (*power_on)(struct platform_device *pdev);
	/* Turn off all power and clocks */
	void (*power_off)(struct platform_device *pdev);
	/* Turn on only VBUS suspend power and hotplug detection,
	 * turn off everything else */
	void (*power_suspend)(struct platform_device *pdev);
};

#endif /* __USB_CORE_OHCI_PDRIVER_H */
