/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mctp-usb.h - MCTP USB transport binding: common definitions,
 * based on DMTF0283 specification:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0283_1.1.0.pdf
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

/*
 * MCTP-over-USB transport header. DSP0283 v1.0 has an 8-bit length field
 * (preceded by 8 reserved bits), v1.1 has a 13-bit length field (preceded by
 * 3 reserved bits). We use a be16 for our length to handle the larger v1.1
 * representation, and mask as appropriate.
 */
struct mctp_usb_hdr {
	__be16	id;
	__be16	len;
} __packed;

/* max transfer size for DSP0283 v1.0 */
#define MCTP_USB_1_0_XFER_SIZE	512
#define MCTP_USB_BTU		68
#define MCTP_USB_MTU_MIN	MCTP_USB_BTU
#define MCTP_USB_1_0_PKTLEN_MAX	U8_MAX
#define MCTP_USB_1_0_MTU_MAX	(MCTP_USB_1_0_PKTLEN_MAX - sizeof(struct mctp_usb_hdr))
#define MCTP_USB_1_1_PKTLEN_MAX	GENMASK(12, 0)
#define MCTP_USB_1_1_MTU_MAX	(MCTP_USB_1_1_PKTLEN_MAX - sizeof(struct mctp_usb_hdr))
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
	u16 ep_pktlen;
	bool span;
};

int mctp_usblib_rx_init(struct mctp_usblib_rx *rx, u16 ep_pktlen, bool span);
void mctp_usblib_rx_fini(struct mctp_usblib_rx *rx);

int mctp_usblib_rx_prepare(struct net_device *netdev,
			   struct mctp_usblib_rx *rx,
			   void **bufp, size_t *lenp, gfp_t gfp);

int mctp_usblib_rx_complete(struct net_device *netdev,
			    struct mctp_usblib_rx *rx, size_t len);

void mctp_usblib_rx_cancel(struct mctp_usblib_rx *rx);

/*
 * TX handle: created by mctp_usblib_tx_push() during the tx path, and
 * may persist across multiple packet transmits.
 */
struct mctp_usblib_tx_ctx;

struct mctp_usblib_tx_ops {
	/* Start a USB TX for @data. On returning success, the implementation
	 * must arrange for mctp_usblib_tx_send_complete() to be called at some
	 * later point (eg., on urb completion).
	 */
	int (*send)(struct mctp_usblib_tx_ctx *tx_ctx, void *data, size_t len);
};

struct mctp_usblib_tx {
	struct mctp_usblib_tx_ops ops;
	void *priv;
	bool span;
	/* protects access to cur_ctx */
	spinlock_t lock;
	/* context to which we are adding packets, cleared on send */
	struct mctp_usblib_tx_ctx *cur_ctx;
};

void mctp_usblib_tx_init(struct mctp_usblib_tx *tx,
			 const struct mctp_usblib_tx_ops *ops, void *priv,
			 bool span);
void mctp_usblib_tx_fini(struct mctp_usblib_tx *tx);

void *mctp_usblib_tx_ctx_priv(struct mctp_usblib_tx_ctx *tx_ctx);

int mctp_usblib_tx_push(struct net_device *dev,
			struct mctp_usblib_tx *tx,
			struct sk_buff *skb, bool more);

void mctp_usblib_tx_send_complete(struct mctp_usblib_tx_ctx *tx_ctx,
				  struct net_device *dev, bool ok);

void mctp_usblib_tx_cancel(struct mctp_usblib_tx *tx, struct net_device *dev,
			   enum skb_drop_reason reason);

#endif /*  __LINUX_USB_MCTP_USB_H */
