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

#ifndef __USB_CORE_EHCI_PDRIVER_H
#define __USB_CORE_EHCI_PDRIVER_H

struct platform_device;
struct usb_hcd;

/**
 * struct usb_ehci_pdata - platform_data for generic ehci driver
 *
 * @caps_offset:	offset of the EHCI Capability Registers to the start of
 *			the io memory region provided to the driver.
 * @has_tt:		set to 1 if TT is integrated in root hub.
 * @port_power_on:	set to 1 if the controller needs a power up after
 *			initialization.
 * @port_power_off:	set to 1 if the controller needs to be powered down
 *			after initialization.
 * @no_io_watchdog:	set to 1 if the controller does not need the I/O
 *			watchdog to run.
 * @reset_on_resume:	set to 1 if the controller needs to be reset after
 * 			a suspend / resume cycle (but can't detect that itself).
 *
 * These are general configuration options for the EHCI controller. All of
 * these options are activating more or less workarounds for some hardware.
 */
struct usb_ehci_pdata {
	int		caps_offset;
	unsigned	has_tt:1;
	unsigned	has_synopsys_hc_bug:1;
	unsigned	big_endian_desc:1;
	unsigned	big_endian_mmio:1;
	unsigned	no_io_watchdog:1;
	unsigned	reset_on_resume:1;
	unsigned	dma_mask_64:1;
	unsigned	spurious_oc:1;

	/* Turn on all power and clocks */
	int (*power_on)(struct platform_device *pdev);
	/* Turn off all power and clocks */
	void (*power_off)(struct platform_device *pdev);
	/* Turn on only VBUS suspend power and hotplug detection,
	 * turn off everything else */
	void (*power_suspend)(struct platform_device *pdev);
	int (*pre_setup)(struct usb_hcd *hcd);
};

#endif /* __USB_CORE_EHCI_PDRIVER_H */
