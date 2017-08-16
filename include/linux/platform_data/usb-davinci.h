/*
 * USB related definitions
 *
 * Copyright (C) 2009 MontaVista Software, Inc. <source@mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ASM_ARCH_USB_H
#define __ASM_ARCH_USB_H

struct	da8xx_ohci_root_hub;

typedef void (*da8xx_ocic_handler_t)(struct da8xx_ohci_root_hub *hub,
				     unsigned port);

/* Passed as the platform data to the OHCI driver */
struct	da8xx_ohci_root_hub {
	/* Switch the port power on/off */
	int	(*set_power)(unsigned port, int on);
	/* Read the port power status */
	int	(*get_power)(unsigned port);
	/* Read the port over-current indicator */
	int	(*get_oci)(unsigned port);
	/* Over-current indicator change notification (pass NULL to disable) */
	int	(*ocic_notify)(da8xx_ocic_handler_t handler);

	/* Time from power on to power good (in 2 ms units) */
	u8	potpgt;
};

void davinci_setup_usb(unsigned mA, unsigned potpgt_ms);

#endif	/* ifndef __ASM_ARCH_USB_H */
