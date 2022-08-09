/*
 * Platform data for OMAP1 USB
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive for
 * more details.
 */
#ifndef __LINUX_USB_OMAP1_H
#define __LINUX_USB_OMAP1_H

#include <linux/platform_device.h>

struct omap_usb_config {
	/* Configure drivers according to the connectors on your board:
	 *  - "A" connector (rectagular)
	 *	... for host/OHCI use, set "register_host".
	 *  - "B" connector (squarish) or "Mini-B"
	 *	... for device/gadget use, set "register_dev".
	 *  - "Mini-AB" connector (very similar to Mini-B)
	 *	... for OTG use as device OR host, initialize "otg"
	 */
	unsigned	register_host:1;
	unsigned	register_dev:1;
	u8		otg;	/* port number, 1-based:  usb1 == 2 */

	const char	*extcon;	/* extcon device for OTG */

	u8		hmc_mode;

	/* implicitly true if otg:  host supports remote wakeup? */
	u8		rwc;

	/* signaling pins used to talk to transceiver on usbN:
	 *  0 == usbN unused
	 *  2 == usb0-only, using internal transceiver
	 *  3 == 3 wire bidirectional
	 *  4 == 4 wire bidirectional
	 *  6 == 6 wire unidirectional (or TLL)
	 */
	u8		pins[3];

	struct platform_device *udc_device;
	struct platform_device *ohci_device;
	struct platform_device *otg_device;

	u32 (*usb0_init)(unsigned nwires, unsigned is_device);
	u32 (*usb1_init)(unsigned nwires);
	u32 (*usb2_init)(unsigned nwires, unsigned alt_pingroup);

	int (*ocpi_enable)(void);

	void (*lb_reset)(void);
};

#endif /* __LINUX_USB_OMAP1_H */
