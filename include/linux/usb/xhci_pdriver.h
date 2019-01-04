/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 */

#ifndef __USB_CORE_XHCI_PDRIVER_H
#define __USB_CORE_XHCI_PDRIVER_H

/**
 * struct usb_xhci_pdata - platform_data for generic xhci platform driver
 *
 * @usb3_lpm_capable:	determines if this xhci platform supports USB3
 *			LPM capability
 * @xhci_slow_suspend:	set if this xhci platform need an extraordinary
 *			delay to wait for xHC enter the Halted state
 *			after the Run/Stop (R/S) bit is cleared to '0'.
 * @xhci_trb_ent: set if this xhci platform need to enable the Evaluate
 *		  Next TRB(ENT) flag in the TRB data structure to force
 *		  xHC to pre-fetch the next TRB of a TD.
 * @usb3_disable_autosuspend: determines if this xhci platform supports
 *			USB3 autosuspend capability
 * @usb3_warm_reset_on_resume:	determines if it need warm reset on resume.
 *
 */
struct usb_xhci_pdata {
	unsigned	usb3_lpm_capable:1;
	unsigned	xhci_slow_suspend:1;
	unsigned	xhci_trb_ent:1;
	unsigned	usb3_disable_autosuspend:1;
	unsigned	usb3_warm_reset_on_resume:1;
};

#endif /* __USB_CORE_XHCI_PDRIVER_H */
