/*
 * Wireless USB Wire Adapter constants and structures.
 *
 * Copyright (C) 2005-2006 Intel Corporation.
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: docs
 * FIXME: organize properly, group logically
 *
 * All the event structures are defined in uwb/spec.h, as they are
 * common to the WHCI and WUSB radio control interfaces.
 *
 * References:
 *   [WUSB] Wireless Universal Serial Bus Specification, revision 1.0, ch8
 */
#ifndef __LINUX_USB_WUSB_WA_H
#define __LINUX_USB_WUSB_WA_H

/**
 * Radio Command Request for the Radio Control Interface
 *
 * Radio Control Interface command and event codes are the same as
 * WHCI, and listed in include/linux/uwb.h:UWB_RC_{CMD,EVT}_*
 */
enum {
	WA_EXEC_RC_CMD = 40,	/* Radio Control command Request */
};

/* Wireless Adapter Requests ([WUSB] table 8-51) */
enum {
	WUSB_REQ_ADD_MMC_IE     = 20,
	WUSB_REQ_REMOVE_MMC_IE  = 21,
	WUSB_REQ_SET_NUM_DNTS   = 22,
	WUSB_REQ_SET_CLUSTER_ID = 23,
	WUSB_REQ_SET_DEV_INFO   = 24,
	WUSB_REQ_GET_TIME       = 25,
	WUSB_REQ_SET_STREAM_IDX = 26,
	WUSB_REQ_SET_WUSB_MAS   = 27,
	WUSB_REQ_CHAN_STOP      = 28,
};


/* Wireless Adapter WUSB Channel Time types ([WUSB] table 8-52) */
enum {
	WUSB_TIME_ADJ   = 0,
	WUSB_TIME_BPST  = 1,
	WUSB_TIME_WUSB  = 2,
};

enum {
	WA_ENABLE = 0x01,
	WA_RESET = 0x02,
	RPIPE_PAUSE = 0x1,
};

/* Responses from Get Status request ([WUSB] section 8.3.1.6) */
enum {
	WA_STATUS_ENABLED = 0x01,
	WA_STATUS_RESETTING = 0x02
};

enum rpipe_crs {
	RPIPE_CRS_CTL = 0x01,
	RPIPE_CRS_ISO = 0x02,
	RPIPE_CRS_BULK = 0x04,
	RPIPE_CRS_INTR = 0x08
};

/**
 * RPipe descriptor ([WUSB] section 8.5.2.11)
 *
 * FIXME: explain rpipes
 */
struct usb_rpipe_descriptor {
	u8	bLength;
	u8	bDescriptorType;
	__le16  wRPipeIndex;
	__le16	wRequests;
	__le16	wBlocks;		/* rw if 0 */
	__le16	wMaxPacketSize;		/* rw */
	union {
		u8	dwa_bHSHubAddress;		/* rw: DWA. */
		u8	hwa_bMaxBurst;			/* rw: HWA. */
	};
	union {
		u8	dwa_bHSHubPort;		/*  rw: DWA. */
		u8	hwa_bDeviceInfoIndex;	/*  rw: HWA. */
	};
	u8	bSpeed;			/* rw: xfer rate 'enum uwb_phy_rate' */
	union {
		u8 dwa_bDeviceAddress;	/* rw: DWA Target device address. */
		u8 hwa_reserved;		/* rw: HWA. */
	};
	u8	bEndpointAddress;	/* rw: Target EP address */
	u8	bDataSequence;		/* ro: Current Data sequence */
	__le32	dwCurrentWindow;	/* ro */
	u8	bMaxDataSequence;	/* ro?: max supported seq */
	u8	bInterval;		/* rw:  */
	u8	bOverTheAirInterval;	/* rw:  */
	u8	bmAttribute;		/* ro?  */
	u8	bmCharacteristics;	/* ro? enum rpipe_attr, supported xsactions */
	u8	bmRetryOptions;		/* rw? */
	__le16	wNumTransactionErrors;	/* rw */
} __attribute__ ((packed));

/**
 * Wire Adapter Notification types ([WUSB] sections 8.4.5 & 8.5.4)
 *
 * These are the notifications coming on the notification endpoint of
 * an HWA and a DWA.
 */
enum wa_notif_type {
	DWA_NOTIF_RWAKE = 0x91,
	DWA_NOTIF_PORTSTATUS = 0x92,
	WA_NOTIF_TRANSFER = 0x93,
	HWA_NOTIF_BPST_ADJ = 0x94,
	HWA_NOTIF_DN = 0x95,
};

/**
 * Wire Adapter notification header
 *
 * Notifications coming from a wire adapter use a common header
 * defined in [WUSB] sections 8.4.5 & 8.5.4.
 */
struct wa_notif_hdr {
	u8 bLength;
	u8 bNotifyType;			/* enum wa_notif_type */
} __attribute__((packed));

/**
 * HWA DN Received notification [(WUSB] section 8.5.4.2)
 *
 * The DNData is specified in WUSB1.0[7.6]. For each device
 * notification we received, we just need to dispatch it.
 *
 * @dndata:  this is really an array of notifications, but all start
 *           with the same header.
 */
struct hwa_notif_dn {
	struct wa_notif_hdr hdr;
	u8 bSourceDeviceAddr;		/* from errata 2005/07 */
	u8 bmAttributes;
	struct wusb_dn_hdr dndata[];
} __attribute__((packed));

/* [WUSB] section 8.3.3 */
enum wa_xfer_type {
	WA_XFER_TYPE_CTL = 0x80,
	WA_XFER_TYPE_BI = 0x81,		/* bulk/interrupt */
	WA_XFER_TYPE_ISO = 0x82,
	WA_XFER_RESULT = 0x83,
	WA_XFER_ABORT = 0x84,
};

/* [WUSB] section 8.3.3 */
struct wa_xfer_hdr {
	u8 bLength;			/* 0x18 */
	u8 bRequestType;		/* 0x80 WA_REQUEST_TYPE_CTL */
	__le16 wRPipe;			/* RPipe index */
	__le32 dwTransferID;		/* Host-assigned ID */
	__le32 dwTransferLength;	/* Length of data to xfer */
	u8 bTransferSegment;
} __attribute__((packed));

struct wa_xfer_ctl {
	struct wa_xfer_hdr hdr;
	u8 bmAttribute;
	__le16 wReserved;
	struct usb_ctrlrequest baSetupData;
} __attribute__((packed));

struct wa_xfer_bi {
	struct wa_xfer_hdr hdr;
	u8 bReserved;
	__le16 wReserved;
} __attribute__((packed));

struct wa_xfer_hwaiso {
	struct wa_xfer_hdr hdr;
	u8 bReserved;
	__le16 wPresentationTime;
	__le32 dwNumOfPackets;
	/* FIXME: u8 pktdata[]? */
} __attribute__((packed));

/* [WUSB] section 8.3.3.5 */
struct wa_xfer_abort {
	u8 bLength;
	u8 bRequestType;
	__le16 wRPipe;			/* RPipe index */
	__le32 dwTransferID;		/* Host-assigned ID */
} __attribute__((packed));

/**
 * WA Transfer Complete notification ([WUSB] section 8.3.3.3)
 *
 */
struct wa_notif_xfer {
	struct wa_notif_hdr hdr;
	u8 bEndpoint;
	u8 Reserved;
} __attribute__((packed));

/** Transfer result basic codes [WUSB] table 8-15 */
enum {
	WA_XFER_STATUS_SUCCESS,
	WA_XFER_STATUS_HALTED,
	WA_XFER_STATUS_DATA_BUFFER_ERROR,
	WA_XFER_STATUS_BABBLE,
	WA_XFER_RESERVED,
	WA_XFER_STATUS_NOT_FOUND,
	WA_XFER_STATUS_INSUFFICIENT_RESOURCE,
	WA_XFER_STATUS_TRANSACTION_ERROR,
	WA_XFER_STATUS_ABORTED,
	WA_XFER_STATUS_RPIPE_NOT_READY,
	WA_XFER_INVALID_FORMAT,
	WA_XFER_UNEXPECTED_SEGMENT_NUMBER,
	WA_XFER_STATUS_RPIPE_TYPE_MISMATCH,
};

/** [WUSB] section 8.3.3.4 */
struct wa_xfer_result {
	struct wa_notif_hdr hdr;
	__le32 dwTransferID;
	__le32 dwTransferLength;
	u8     bTransferSegment;
	u8     bTransferStatus;
	__le32 dwNumOfPackets;
} __attribute__((packed));

/**
 * Wire Adapter Class Descriptor ([WUSB] section 8.5.2.7).
 *
 * NOTE: u16 fields are read Little Endian from the hardware.
 *
 * @bNumPorts is the original max number of devices that the host can
 *            connect; we might chop this so the stack can handle
 *            it. In case you need to access it, use wusbhc->ports_max
 *            if it is a Wireless USB WA.
 */
struct usb_wa_descriptor {
	u8	bLength;
	u8	bDescriptorType;
	u16	bcdWAVersion;
	u8	bNumPorts;		/* don't use!! */
	u8	bmAttributes;		/* Reserved == 0 */
	u16	wNumRPipes;
	u16	wRPipeMaxBlock;
	u8	bRPipeBlockSize;
	u8	bPwrOn2PwrGood;
	u8	bNumMMCIEs;
	u8	DeviceRemovable;	/* FIXME: in DWA this is up to 16 bytes */
} __attribute__((packed));

/**
 * HWA Device Information Buffer (WUSB1.0[T8.54])
 */
struct hwa_dev_info {
	u8	bmDeviceAvailability[32];       /* FIXME: ignored for now */
	u8	bDeviceAddress;
	__le16	wPHYRates;
	u8	bmDeviceAttribute;
} __attribute__((packed));

#endif /* #ifndef __LINUX_USB_WUSB_WA_H */
