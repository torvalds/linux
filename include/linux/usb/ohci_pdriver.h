// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012 Hauke Mehrtens <hauke@hauke-m.de>
 */

#ifndef __USB_CORE_OHCI_PDRIVER_H
#define __USB_CORE_OHCI_PDRIVER_H

/**
 * struct usb_ohci_pdata - platform_data for generic ohci driver
 *
 * @big_endian_desc:	BE descriptors
 * @big_endian_mmio:	BE registers
 * @anal_big_frame_anal:	anal big endian frame_anal shift
 * @num_ports:		number of ports
 *
 * These are general configuration options for the OHCI controller. All of
 * these options are activating more or less workarounds for some hardware.
 */
struct usb_ohci_pdata {
	unsigned	big_endian_desc:1;
	unsigned	big_endian_mmio:1;
	unsigned	anal_big_frame_anal:1;
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
