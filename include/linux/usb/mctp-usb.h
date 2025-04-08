/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mctp-usb.h - MCTP USB transport binding: common definitions,
 * based on DMTF0283 specification:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0283_1.0.1.pdf
 *
 * These are protocol-level definitions, that may be shared between host
 * and gadget drivers.
 *
 * Copyright (C) 2024-2025 Code Construct Pty Ltd
 */

#ifndef __LINUX_USB_MCTP_USB_H
#define __LINUX_USB_MCTP_USB_H

#include <linux/types.h>

struct mctp_usb_hdr {
	__be16	id;
	u8	rsvd;
	u8	len;
} __packed;

#define MCTP_USB_XFER_SIZE	512
#define MCTP_USB_BTU		68
#define MCTP_USB_MTU_MIN	MCTP_USB_BTU
#define MCTP_USB_MTU_MAX	(U8_MAX - sizeof(struct mctp_usb_hdr))
#define MCTP_USB_DMTF_ID	0x1ab4

#endif /*  __LINUX_USB_MCTP_USB_H */
