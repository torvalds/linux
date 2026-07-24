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

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>

struct mctp_usb_hdr {
	__be16	id;
	u8	rsvd;
	u8	len;
} __packed;

/* max transfer size for DSP0283 v1.0 */
#define MCTP_USB_1_0_XFER_SIZE	512
#define MCTP_USB_BTU		68
#define MCTP_USB_MTU_MIN	MCTP_USB_BTU
#define MCTP_USB_1_0_PKTLEN_MAX	U8_MAX
#define MCTP_USB_1_0_MTU_MAX	(MCTP_USB_1_0_PKTLEN_MAX - sizeof(struct mctp_usb_hdr))
#define MCTP_USB_DMTF_ID	0x1ab4

/* mctp-usblib */

/*
 * RX handle: drivers will typically create one on init, which persists for
 * the life of the driver. The same handle is used for progressive
 * prepare -> complete operations (for each incoming USB transfer), which
 * result in netif_rx()-ing the MCTP packets received
 */
struct mctp_usblib_rx {
	struct sk_buff *skb;
};

void mctp_usblib_rx_init(struct mctp_usblib_rx *rx);
void mctp_usblib_rx_fini(struct mctp_usblib_rx *rx);

int mctp_usblib_rx_prepare(struct net_device *netdev,
			   struct mctp_usblib_rx *rx,
			   void **bufp, size_t *lenp, gfp_t gfp);

int mctp_usblib_rx_complete(struct net_device *netdev,
			    struct mctp_usblib_rx *rx, size_t len);

void mctp_usblib_rx_cancel(struct mctp_usblib_rx *rx);

#endif /*  __LINUX_USB_MCTP_USB_H */
