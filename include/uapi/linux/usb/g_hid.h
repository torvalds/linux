/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */

#ifndef __UAPI_LINUX_USB_G_HID_H
#define __UAPI_LINUX_USB_G_HID_H

#include <linux/types.h>

/* Maximum HID report length for High-Speed USB (i.e. USB 2.0) */
#define MAX_REPORT_LENGTH 64

/**
 * struct usb_hidg_report - response to GET_REPORT
 * @report_id: report ID that this is a response for
 * @userspace_req:
 *    !0 this report is used for any pending GET_REPORT request
 *       but wait on userspace to issue a new report on future requests
 *    0  this report is to be used for any future GET_REPORT requests
 * @length: length of the report response
 * @data: report response
 * @padding: padding for 32/64 bit compatibility
 *
 * Structure used by GADGET_HID_WRITE_GET_REPORT ioctl on /dev/hidg*.
 */
struct usb_hidg_report {
	__u8 report_id;
	__u8 userspace_req;
	__u16 length;
	__u8 data[MAX_REPORT_LENGTH];
	__u8 padding[4];
};

/* The 'g' code is used by gadgetfs and hid gadget ioctl requests.
 * Don't add any colliding codes to either driver, and keep
 * them in unique ranges.
 */

#define GADGET_HID_READ_GET_REPORT_ID   _IOR('g', 0x41, __u8)
#define GADGET_HID_WRITE_GET_REPORT     _IOW('g', 0x42, struct usb_hidg_report)

#endif /* __UAPI_LINUX_USB_G_HID_H */
