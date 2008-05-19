/*
 * Wireless USB - Cable Based Association
 *
 * Copyright (C) 2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 */
#ifndef __LINUX_USB_ASSOCIATION_H
#define __LINUX_USB_ASSOCIATION_H


/*
 * Association attributes
 *
 * Association Models Supplement to WUSB 1.0 T[3-1]
 *
 * Each field in the structures has it's ID, it's length and then the
 * value. This is the actual definition of the field's ID and its
 * length.
 */
struct wusb_am_attr {
	__u8 id;
	__u8 len;
};

/* Different fields defined by the spec */
#define WUSB_AR_AssociationTypeId	{ .id = 0x0000, .len =  2 }
#define WUSB_AR_AssociationSubTypeId	{ .id = 0x0001, .len =  2 }
#define WUSB_AR_Length			{ .id = 0x0002, .len =  4 }
#define WUSB_AR_AssociationStatus	{ .id = 0x0004, .len =  4 }
#define WUSB_AR_LangID			{ .id = 0x0008, .len =  2 }
#define WUSB_AR_DeviceFriendlyName	{ .id = 0x000b, .len = 64 } /* max */
#define WUSB_AR_HostFriendlyName	{ .id = 0x000c, .len = 64 } /* max */
#define WUSB_AR_CHID			{ .id = 0x1000, .len = 16 }
#define WUSB_AR_CDID			{ .id = 0x1001, .len = 16 }
#define WUSB_AR_ConnectionContext	{ .id = 0x1002, .len = 48 }
#define WUSB_AR_BandGroups		{ .id = 0x1004, .len =  2 }

/* CBAF Control Requests (AMS1.0[T4-1] */
enum {
	CBAF_REQ_GET_ASSOCIATION_INFORMATION = 0x01,
	CBAF_REQ_GET_ASSOCIATION_REQUEST,
	CBAF_REQ_SET_ASSOCIATION_RESPONSE
};

/*
 * CBAF USB-interface defitions
 *
 * No altsettings, one optional interrupt endpoint.
 */
enum {
	CBAF_IFACECLASS    = 0xef,
	CBAF_IFACESUBCLASS = 0x03,
	CBAF_IFACEPROTOCOL = 0x01,
};

/* Association Information (AMS1.0[T4-3]) */
struct wusb_cbaf_assoc_info {
	__le16 Length;
	__u8 NumAssociationRequests;
	__le16 Flags;
	__u8 AssociationRequestsArray[];
} __attribute__((packed));

/* Association Request (AMS1.0[T4-4]) */
struct wusb_cbaf_assoc_request {
	__u8 AssociationDataIndex;
	__u8 Reserved;
	__le16 AssociationTypeId;
	__le16 AssociationSubTypeId;
	__le32 AssociationTypeInfoSize;
} __attribute__((packed));

enum {
	AR_TYPE_WUSB                    = 0x0001,
	AR_TYPE_WUSB_RETRIEVE_HOST_INFO = 0x0000,
	AR_TYPE_WUSB_ASSOCIATE          = 0x0001,
};

/* Association Attribute header (AMS1.0[3.8]) */
struct wusb_cbaf_attr_hdr {
	__le16 id;
	__le16 len;
} __attribute__((packed));

/* Host Info (AMS1.0[T4-7]) (yeah, more headers and fields...) */
struct wusb_cbaf_host_info {
	struct wusb_cbaf_attr_hdr AssociationTypeId_hdr;
	__le16 AssociationTypeId;
	struct wusb_cbaf_attr_hdr AssociationSubTypeId_hdr;
	__le16 AssociationSubTypeId;
	struct wusb_cbaf_attr_hdr CHID_hdr;
	struct wusb_ckhdid CHID;
	struct wusb_cbaf_attr_hdr LangID_hdr;
	__le16 LangID;
	struct wusb_cbaf_attr_hdr HostFriendlyName_hdr;
	__u8 HostFriendlyName[];
} __attribute__((packed));

/* Device Info (AMS1.0[T4-8])
 *
 * I still don't get this tag'n'header stuff for each goddamn
 * field...
 */
struct wusb_cbaf_device_info {
	struct wusb_cbaf_attr_hdr Length_hdr;
	__le32 Length;
	struct wusb_cbaf_attr_hdr CDID_hdr;
	struct wusb_ckhdid CDID;
	struct wusb_cbaf_attr_hdr BandGroups_hdr;
	__le16 BandGroups;
	struct wusb_cbaf_attr_hdr LangID_hdr;
	__le16 LangID;
	struct wusb_cbaf_attr_hdr DeviceFriendlyName_hdr;
	__u8 DeviceFriendlyName[];
} __attribute__((packed));

/* Connection Context; CC_DATA - Success case (AMS1.0[T4-9]) */
struct wusb_cbaf_cc_data {
	struct wusb_cbaf_attr_hdr AssociationTypeId_hdr;
	__le16 AssociationTypeId;
	struct wusb_cbaf_attr_hdr AssociationSubTypeId_hdr;
	__le16 AssociationSubTypeId;
	struct wusb_cbaf_attr_hdr Length_hdr;
	__le32 Length;
	struct wusb_cbaf_attr_hdr ConnectionContext_hdr;
	struct wusb_ckhdid CHID;
	struct wusb_ckhdid CDID;
	struct wusb_ckhdid CK;
	struct wusb_cbaf_attr_hdr BandGroups_hdr;
	__le16 BandGroups;
} __attribute__((packed));

/* CC_DATA - Failure case (AMS1.0[T4-10]) */
struct wusb_cbaf_cc_data_fail {
	struct wusb_cbaf_attr_hdr AssociationTypeId_hdr;
	__le16 AssociationTypeId;
	struct wusb_cbaf_attr_hdr AssociationSubTypeId_hdr;
	__le16 AssociationSubTypeId;
	struct wusb_cbaf_attr_hdr Length_hdr;
	__le16 Length;
	struct wusb_cbaf_attr_hdr AssociationStatus_hdr;
	__u32 AssociationStatus;
} __attribute__((packed));

#endif	/* __LINUX_USB_ASSOCIATION_H */
