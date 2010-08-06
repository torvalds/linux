/*
 * This file holds the definitions of quirks found in USB devices.
 * Only quirks that affect the whole device, not an interface,
 * belong here.
 */

#ifndef __LINUX_USB_QUIRKS_H
#define __LINUX_USB_QUIRKS_H

/* string descriptors must not be fetched using a 255-byte read */
#define USB_QUIRK_STRING_FETCH_255	0x00000001

/* device can't resume correctly so reset it instead */
#define USB_QUIRK_RESET_RESUME		0x00000002

/* device can't handle Set-Interface requests */
#define USB_QUIRK_NO_SET_INTF		0x00000004

/* device can't handle its Configuration or Interface strings */
#define USB_QUIRK_CONFIG_INTF_STRINGS	0x00000008

/*device will morph if reset, don't use reset for handling errors */
#define USB_QUIRK_RESET_MORPHS		0x00000010

/* device has more interface descriptions than the bNumInterfaces count,
   and can't handle talking to these interfaces */
#define USB_QUIRK_HONOR_BNUMINTERFACES	0x00000020

#endif /* __LINUX_USB_QUIRKS_H */
