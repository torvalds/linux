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
	UNKNOWN_TYPE,
	SDP_TYPE,
	DCP_TYPE,
	CDP_TYPE,
	ACA_TYPE,
};

/* USB charger state */
enum usb_charger_state {
	USB_CHARGER_DEFAULT,
	USB_CHARGER_PRESENT,
	USB_CHARGER_ABSENT,
};

#endif /* _UAPI__LINUX_USB_CHARGER_H */
