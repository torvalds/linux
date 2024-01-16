/*
 * This file defines the USB charger type and state that are needed for
 * USB device APIs.
 */

#ifndef _UAPI__LINUX_USB_CHARGER_H
#define _UAPI__LINUX_USB_CHARGER_H

/*
 * USB charger type:
 * SDP (Standard Downstream Port)
 * DCP (Dedicated Charging Port)
 * CDP (Charging Downstream Port)
 * ACA (Accessory Charger Adapters)
 */
enum usb_charger_type {
	UNKNOWN_TYPE = 0,
	SDP_TYPE = 1,
	DCP_TYPE = 2,
	CDP_TYPE = 3,
	ACA_TYPE = 4,
};

/* USB charger state */
enum usb_charger_state {
	USB_CHARGER_DEFAULT = 0,
	USB_CHARGER_PRESENT = 1,
	USB_CHARGER_ABSENT = 2,
};

#endif /* _UAPI__LINUX_USB_CHARGER_H */
