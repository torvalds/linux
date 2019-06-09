/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Ultra Wide Band
 * UWB Standard definitions
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * All these definitions are based on the ECMA-368 standard.
 *
 * Note all definitions are Little Endian in the wire, and we will
 * convert them to host order before operating on the bitfields (that
 * yes, we use extensively).
 */

#ifndef __LINUX__UWB_SPEC_H__
#define __LINUX__UWB_SPEC_H__

#include <linux/types.h>
#include <linux/bitmap.h>
#include <linux/if_ether.h>

#define i1480_FW 0x00000303
/* #define i1480_FW 0x00000302 */

/**
 * Number of Medium Access Slots in a superframe.
 *
 * UWB divides time in SuperFrames, each one divided in 256 pieces, or
 * Medium Access Slots. See MBOA MAC[5.4.5] for details. The MAS is the
 * basic bandwidth allocation unit in UWB.
 */
enum { UWB_NUM_MAS = 256 };

/**
 * Number of Zones in superframe.
 *
 * UWB divides the superframe into zones with numbering starting from BPST.
 * See MBOA MAC[16.8.6]
 */
enum { UWB_NUM_ZONES = 16 };

/*
 * Number of MAS in a zone.
 */
#define UWB_MAS_PER_ZONE (UWB_NUM_MAS / UWB_NUM_ZONES)

/*
 * Number of MAS required before a row can be considered available.
 */
#define UWB_USABLE_MAS_PER_ROW (UWB_NUM_ZONES - 1)

/*
 * Number of streams per DRP reservation between a pair of devices.
 *
 * [ECMA-368] section 16.8.6.
 */
enum { UWB_NUM_STREAMS = 8 };

/*
 * mMasLength
 *
 * The length of a MAS in microseconds.
 *
 * [ECMA-368] section 17.16.
 */
enum { UWB_MAS_LENGTH_US = 256 };

/*
 * mBeaconSlotLength
 *
 * The length of the beacon slot in microseconds.
 *
 * [ECMA-368] section 17.16
 */
enum { UWB_BEACON_SLOT_LENGTH_US = 85 };

/*
 * mMaxLostBeacons
 *
 * The number beacons missing in consecutive superframes before a
 * device can be considered as unreachable.
 *
 * [ECMA-368] section 17.16
 */
enum { UWB_MAX_LOST_BEACONS = 3 };

/*
 * mDRPBackOffWinMin
 *
 * The minimum number of superframes to wait before trying to reserve
 * extra MAS.
 *
 * [ECMA-368] section 17.16
 */
enum { UWB_DRP_BACKOFF_WIN_MIN = 2 };

/*
 * mDRPBackOffWinMax
 *
 * The maximum number of superframes to wait before trying to reserve
 * extra MAS.
 *
 * [ECMA-368] section 17.16
 */
enum { UWB_DRP_BACKOFF_WIN_MAX = 16 };

/*
 * Length of a superframe in microseconds.
 */
#define UWB_SUPERFRAME_LENGTH_US (UWB_MAS_LENGTH_US * UWB_NUM_MAS)

/**
 * UWB MAC address
 *
 * It is *imperative* that this struct is exactly 6 packed bytes (as
 * it is also used to define headers sent down and up the wire/radio).
 */
struct uwb_mac_addr {
	u8 data[ETH_ALEN];
} __attribute__((packed));


/**
 * UWB device address
 *
 * It is *imperative* that this struct is exactly 6 packed bytes (as
 * it is also used to define headers sent down and up the wire/radio).
 */
struct uwb_dev_addr {
	u8 data[2];
} __attribute__((packed));


/**
 * Types of UWB addresses
 *
 * Order matters (by size).
 */
enum uwb_addr_type {
	UWB_ADDR_DEV = 0,
	UWB_ADDR_MAC = 1,
};


/** Size of a char buffer for printing a MAC/device address */
enum { UWB_ADDR_STRSIZE = 32 };


/** UWB WiMedia protocol IDs. */
enum uwb_prid {
	UWB_PRID_WLP_RESERVED   = 0x0000,
	UWB_PRID_WLP		= 0x0001,
	UWB_PRID_WUSB_BOT	= 0x0010,
	UWB_PRID_WUSB		= 0x0010,
	UWB_PRID_WUSB_TOP	= 0x001F,
};


/** PHY Rate (MBOA MAC[7.8.12, Table 61]) */
enum uwb_phy_rate {
	UWB_PHY_RATE_53 = 0,
	UWB_PHY_RATE_80,
	UWB_PHY_RATE_106,
	UWB_PHY_RATE_160,
	UWB_PHY_RATE_200,
	UWB_PHY_RATE_320,
	UWB_PHY_RATE_400,
	UWB_PHY_RATE_480,
	UWB_PHY_RATE_INVALID
};


/**
 * Different ways to scan (MBOA MAC[6.2.2, Table 8], WUSB[Table 8-78])
 */
enum uwb_scan_type {
	UWB_SCAN_ONLY = 0,
	UWB_SCAN_OUTSIDE_BP,
	UWB_SCAN_WHILE_INACTIVE,
	UWB_SCAN_DISABLED,
	UWB_SCAN_ONLY_STARTTIME,
	UWB_SCAN_TOP
};


/** ACK Policy types (MBOA MAC[7.2.1.3]) */
enum uwb_ack_pol {
	UWB_ACK_NO = 0,
	UWB_ACK_INM = 1,
	UWB_ACK_B = 2,
	UWB_ACK_B_REQ = 3,
};


/** DRP reservation types ([ECMA-368 table 106) */
enum uwb_drp_type {
	UWB_DRP_TYPE_ALIEN_BP = 0,
	UWB_DRP_TYPE_HARD,
	UWB_DRP_TYPE_SOFT,
	UWB_DRP_TYPE_PRIVATE,
	UWB_DRP_TYPE_PCA,
};


/** DRP Reason Codes ([ECMA-368] table 107) */
enum uwb_drp_reason {
	UWB_DRP_REASON_ACCEPTED = 0,
	UWB_DRP_REASON_CONFLICT,
	UWB_DRP_REASON_PENDING,
	UWB_DRP_REASON_DENIED,
	UWB_DRP_REASON_MODIFIED,
};

/** Relinquish Request Reason Codes ([ECMA-368] table 113) */
enum uwb_relinquish_req_reason {
	UWB_RELINQUISH_REQ_REASON_NON_SPECIFIC = 0,
	UWB_RELINQUISH_REQ_REASON_OVER_ALLOCATION,
};

/**
 *  DRP Notification Reason Codes (WHCI 0.95 [3.1.4.9])
 */
enum uwb_drp_notif_reason {
	UWB_DRP_NOTIF_DRP_IE_RCVD = 0,
	UWB_DRP_NOTIF_CONFLICT,
	UWB_DRP_NOTIF_TERMINATE,
};


/** Allocation of MAS slots in a DRP request MBOA MAC[7.8.7] */
struct uwb_drp_alloc {
	__le16 zone_bm;
	__le16 mas_bm;
} __attribute__((packed));


/** General MAC Header format (ECMA-368[16.2]) */
struct uwb_mac_frame_hdr {
	__le16 Frame_Control;
	struct uwb_dev_addr DestAddr;
	struct uwb_dev_addr SrcAddr;
	__le16 Sequence_Control;
	__le16 Access_Information;
} __attribute__((packed));


/**
 * uwb_beacon_frame - a beacon frame including MAC headers
 *
 * [ECMA] section 16.3.
 */
struct uwb_beacon_frame {
	struct uwb_mac_frame_hdr hdr;
	struct uwb_mac_addr Device_Identifier;	/* may be a NULL EUI-48 */
	u8 Beacon_Slot_Number;
	u8 Device_Control;
	u8 IEData[];
} __attribute__((packed));


/** Information Element codes (MBOA MAC[T54]) */
enum uwb_ie {
	UWB_PCA_AVAILABILITY = 2,
	UWB_IE_DRP_AVAILABILITY = 8,
	UWB_IE_DRP = 9,
	UWB_BP_SWITCH_IE = 11,
	UWB_MAC_CAPABILITIES_IE = 12,
	UWB_PHY_CAPABILITIES_IE = 13,
	UWB_APP_SPEC_PROBE_IE = 15,
	UWB_IDENTIFICATION_IE = 19,
	UWB_MASTER_KEY_ID_IE = 20,
	UWB_RELINQUISH_REQUEST_IE = 21,
	UWB_IE_WLP = 250, /* WiMedia Logical Link Control Protocol WLP 0.99 */
	UWB_APP_SPEC_IE = 255,
};


/**
 * Header common to all Information Elements (IEs)
 */
struct uwb_ie_hdr {
	u8 element_id;	/* enum uwb_ie */
	u8 length;
} __attribute__((packed));


/** Dynamic Reservation Protocol IE (MBOA MAC[7.8.6]) */
struct uwb_ie_drp {
	struct uwb_ie_hdr	hdr;
	__le16                  drp_control;
	struct uwb_dev_addr	dev_addr;
	struct uwb_drp_alloc	allocs[];
} __attribute__((packed));

static inline int uwb_ie_drp_type(struct uwb_ie_drp *ie)
{
	return (le16_to_cpu(ie->drp_control) >> 0) & 0x7;
}

static inline int uwb_ie_drp_stream_index(struct uwb_ie_drp *ie)
{
	return (le16_to_cpu(ie->drp_control) >> 3) & 0x7;
}

static inline int uwb_ie_drp_reason_code(struct uwb_ie_drp *ie)
{
	return (le16_to_cpu(ie->drp_control) >> 6) & 0x7;
}

static inline int uwb_ie_drp_status(struct uwb_ie_drp *ie)
{
	return (le16_to_cpu(ie->drp_control) >> 9) & 0x1;
}

static inline int uwb_ie_drp_owner(struct uwb_ie_drp *ie)
{
	return (le16_to_cpu(ie->drp_control) >> 10) & 0x1;
}

static inline int uwb_ie_drp_tiebreaker(struct uwb_ie_drp *ie)
{
	return (le16_to_cpu(ie->drp_control) >> 11) & 0x1;
}

static inline int uwb_ie_drp_unsafe(struct uwb_ie_drp *ie)
{
	return (le16_to_cpu(ie->drp_control) >> 12) & 0x1;
}

static inline void uwb_ie_drp_set_type(struct uwb_ie_drp *ie, enum uwb_drp_type type)
{
	u16 drp_control = le16_to_cpu(ie->drp_control);
	drp_control = (drp_control & ~(0x7 << 0)) | (type << 0);
	ie->drp_control = cpu_to_le16(drp_control);
}

static inline void uwb_ie_drp_set_stream_index(struct uwb_ie_drp *ie, int stream_index)
{
	u16 drp_control = le16_to_cpu(ie->drp_control);
	drp_control = (drp_control & ~(0x7 << 3)) | (stream_index << 3);
	ie->drp_control = cpu_to_le16(drp_control);
}

static inline void uwb_ie_drp_set_reason_code(struct uwb_ie_drp *ie,
				       enum uwb_drp_reason reason_code)
{
	u16 drp_control = le16_to_cpu(ie->drp_control);
	drp_control = (ie->drp_control & ~(0x7 << 6)) | (reason_code << 6);
	ie->drp_control = cpu_to_le16(drp_control);
}

static inline void uwb_ie_drp_set_status(struct uwb_ie_drp *ie, int status)
{
	u16 drp_control = le16_to_cpu(ie->drp_control);
	drp_control = (drp_control & ~(0x1 << 9)) | (status << 9);
	ie->drp_control = cpu_to_le16(drp_control);
}

static inline void uwb_ie_drp_set_owner(struct uwb_ie_drp *ie, int owner)
{
	u16 drp_control = le16_to_cpu(ie->drp_control);
	drp_control = (drp_control & ~(0x1 << 10)) | (owner << 10);
	ie->drp_control = cpu_to_le16(drp_control);
}

static inline void uwb_ie_drp_set_tiebreaker(struct uwb_ie_drp *ie, int tiebreaker)
{
	u16 drp_control = le16_to_cpu(ie->drp_control);
	drp_control = (drp_control & ~(0x1 << 11)) | (tiebreaker << 11);
	ie->drp_control = cpu_to_le16(drp_control);
}

static inline void uwb_ie_drp_set_unsafe(struct uwb_ie_drp *ie, int unsafe)
{
	u16 drp_control = le16_to_cpu(ie->drp_control);
	drp_control = (drp_control & ~(0x1 << 12)) | (unsafe << 12);
	ie->drp_control = cpu_to_le16(drp_control);
}

/** Dynamic Reservation Protocol IE (MBOA MAC[7.8.7]) */
struct uwb_ie_drp_avail {
	struct uwb_ie_hdr	hdr;
	DECLARE_BITMAP(bmp, UWB_NUM_MAS);
} __attribute__((packed));

/* Relinqish Request IE ([ECMA-368] section 16.8.19). */
struct uwb_relinquish_request_ie {
        struct uwb_ie_hdr       hdr;
        __le16                  relinquish_req_control;
        struct uwb_dev_addr     dev_addr;
        struct uwb_drp_alloc    allocs[];
} __attribute__((packed));

static inline int uwb_ie_relinquish_req_reason_code(struct uwb_relinquish_request_ie *ie)
{
	return (le16_to_cpu(ie->relinquish_req_control) >> 0) & 0xf;
}

static inline void uwb_ie_relinquish_req_set_reason_code(struct uwb_relinquish_request_ie *ie,
							 int reason_code)
{
	u16 ctrl = le16_to_cpu(ie->relinquish_req_control);
	ctrl = (ctrl & ~(0xf << 0)) | (reason_code << 0);
	ie->relinquish_req_control = cpu_to_le16(ctrl);
}

/**
 * The Vendor ID is set to an OUI that indicates the vendor of the device.
 * ECMA-368 [16.8.10]
 */
struct uwb_vendor_id {
	u8 data[3];
} __attribute__((packed));

/**
 * The device type ID
 * FIXME: clarify what this means
 * ECMA-368 [16.8.10]
 */
struct uwb_device_type_id {
	u8 data[3];
} __attribute__((packed));


/**
 * UWB device information types
 * ECMA-368 [16.8.10]
 */
enum uwb_dev_info_type {
	UWB_DEV_INFO_VENDOR_ID = 0,
	UWB_DEV_INFO_VENDOR_TYPE,
	UWB_DEV_INFO_NAME,
};

/**
 * UWB device information found in Identification IE
 * ECMA-368 [16.8.10]
 */
struct uwb_dev_info {
	u8 type;	/* enum uwb_dev_info_type */
	u8 length;
	u8 data[];
} __attribute__((packed));

/**
 * UWB Identification IE
 * ECMA-368 [16.8.10]
 */
struct uwb_identification_ie {
	struct uwb_ie_hdr hdr;
	struct uwb_dev_info info[];
} __attribute__((packed));

/*
 * UWB Radio Controller
 *
 * These definitions are common to the Radio Control layers as
 * exported by the WUSB1.0 HWA and WHCI interfaces.
 */

/** Radio Control Command Block (WUSB1.0[Table 8-65] and WHCI 0.95) */
struct uwb_rccb {
	u8 bCommandType;		/* enum hwa_cet */
	__le16 wCommand;		/* Command code */
	u8 bCommandContext;		/* Context ID */
} __attribute__((packed));


/** Radio Control Event Block (WUSB[table 8-66], WHCI 0.95) */
struct uwb_rceb {
	u8 bEventType;			/* enum hwa_cet */
	__le16 wEvent;			/* Event code */
	u8 bEventContext;		/* Context ID */
} __attribute__((packed));


enum {
	UWB_RC_CET_GENERAL = 0,		/* General Command/Event type */
	UWB_RC_CET_EX_TYPE_1 = 1,	/* Extended Type 1 Command/Event type */
};

/* Commands to the radio controller */
enum uwb_rc_cmd {
	UWB_RC_CMD_CHANNEL_CHANGE = 16,
	UWB_RC_CMD_DEV_ADDR_MGMT = 17,	/* Device Address Management */
	UWB_RC_CMD_GET_IE = 18,		/* GET Information Elements */
	UWB_RC_CMD_RESET = 19,
	UWB_RC_CMD_SCAN = 20,		/* Scan management  */
	UWB_RC_CMD_SET_BEACON_FILTER = 21,
	UWB_RC_CMD_SET_DRP_IE = 22,	/* Dynamic Reservation Protocol IEs */
	UWB_RC_CMD_SET_IE = 23,		/* Information Element management */
	UWB_RC_CMD_SET_NOTIFICATION_FILTER = 24,
	UWB_RC_CMD_SET_TX_POWER = 25,
	UWB_RC_CMD_SLEEP = 26,
	UWB_RC_CMD_START_BEACON = 27,
	UWB_RC_CMD_STOP_BEACON = 28,
	UWB_RC_CMD_BP_MERGE = 29,
	UWB_RC_CMD_SEND_COMMAND_FRAME = 30,
	UWB_RC_CMD_SET_ASIE_NOTIF = 31,
};

/* Notifications from the radio controller */
enum uwb_rc_evt {
	UWB_RC_EVT_IE_RCV = 0,
	UWB_RC_EVT_BEACON = 1,
	UWB_RC_EVT_BEACON_SIZE = 2,
	UWB_RC_EVT_BPOIE_CHANGE = 3,
	UWB_RC_EVT_BP_SLOT_CHANGE = 4,
	UWB_RC_EVT_BP_SWITCH_IE_RCV = 5,
	UWB_RC_EVT_DEV_ADDR_CONFLICT = 6,
	UWB_RC_EVT_DRP_AVAIL = 7,
	UWB_RC_EVT_DRP = 8,
	UWB_RC_EVT_BP_SWITCH_STATUS = 9,
	UWB_RC_EVT_CMD_FRAME_RCV = 10,
	UWB_RC_EVT_CHANNEL_CHANGE_IE_RCV = 11,
	/* Events (command responses) use the same code as the command */
	UWB_RC_EVT_UNKNOWN_CMD_RCV = 65535,
};

enum uwb_rc_extended_type_1_cmd {
	UWB_RC_SET_DAA_ENERGY_MASK = 32,
	UWB_RC_SET_NOTIFICATION_FILTER_EX = 33,
};

enum uwb_rc_extended_type_1_evt {
	UWB_RC_DAA_ENERGY_DETECTED = 0,
};

/* Radio Control Result Code. [WHCI] table 3-3. */
enum {
	UWB_RC_RES_SUCCESS = 0,
	UWB_RC_RES_FAIL,
	UWB_RC_RES_FAIL_HARDWARE,
	UWB_RC_RES_FAIL_NO_SLOTS,
	UWB_RC_RES_FAIL_BEACON_TOO_LARGE,
	UWB_RC_RES_FAIL_INVALID_PARAMETER,
	UWB_RC_RES_FAIL_UNSUPPORTED_PWR_LEVEL,
	UWB_RC_RES_FAIL_INVALID_IE_DATA,
	UWB_RC_RES_FAIL_BEACON_SIZE_EXCEEDED,
	UWB_RC_RES_FAIL_CANCELLED,
	UWB_RC_RES_FAIL_INVALID_STATE,
	UWB_RC_RES_FAIL_INVALID_SIZE,
	UWB_RC_RES_FAIL_ACK_NOT_RECEIVED,
	UWB_RC_RES_FAIL_NO_MORE_ASIE_NOTIF,
	UWB_RC_RES_FAIL_TIME_OUT = 255,
};

/* Confirm event. [WHCI] section 3.1.3.1 etc. */
struct uwb_rc_evt_confirm {
	struct uwb_rceb rceb;
	u8 bResultCode;
} __attribute__((packed));

/* Device Address Management event. [WHCI] section 3.1.3.2. */
struct uwb_rc_evt_dev_addr_mgmt {
	struct uwb_rceb rceb;
	u8 baAddr[ETH_ALEN];
	u8 bResultCode;
} __attribute__((packed));


/* Get IE Event. [WHCI] section 3.1.3.3. */
struct uwb_rc_evt_get_ie {
	struct uwb_rceb rceb;
	__le16 wIELength;
	u8 IEData[];
} __attribute__((packed));

/* Set DRP IE Event. [WHCI] section 3.1.3.7. */
struct uwb_rc_evt_set_drp_ie {
	struct uwb_rceb rceb;
	__le16 wRemainingSpace;
	u8 bResultCode;
} __attribute__((packed));

/* Set IE Event. [WHCI] section 3.1.3.8. */
struct uwb_rc_evt_set_ie {
	struct uwb_rceb rceb;
	__le16 RemainingSpace;
	u8 bResultCode;
} __attribute__((packed));

/* Scan command. [WHCI] 3.1.3.5. */
struct uwb_rc_cmd_scan {
	struct uwb_rccb rccb;
	u8 bChannelNumber;
	u8 bScanState;
	__le16 wStartTime;
} __attribute__((packed));

/* Set DRP IE command. [WHCI] section 3.1.3.7. */
struct uwb_rc_cmd_set_drp_ie {
	struct uwb_rccb rccb;
	__le16 wIELength;
	struct uwb_ie_drp IEData[];
} __attribute__((packed));

/* Set IE command. [WHCI] section 3.1.3.8. */
struct uwb_rc_cmd_set_ie {
	struct uwb_rccb rccb;
	__le16 wIELength;
	u8 IEData[];
} __attribute__((packed));

/* Set DAA Energy Mask event. [WHCI 0.96] section 3.1.3.17. */
struct uwb_rc_evt_set_daa_energy_mask {
	struct uwb_rceb rceb;
	__le16 wLength;
	u8 result;
} __attribute__((packed));

/* Set Notification Filter Extended event. [WHCI 0.96] section 3.1.3.18. */
struct uwb_rc_evt_set_notification_filter_ex {
	struct uwb_rceb rceb;
	__le16 wLength;
	u8 result;
} __attribute__((packed));

/* IE Received notification. [WHCI] section 3.1.4.1. */
struct uwb_rc_evt_ie_rcv {
	struct uwb_rceb rceb;
	struct uwb_dev_addr SrcAddr;
	__le16 wIELength;
	u8 IEData[];
} __attribute__((packed));

/* Type of the received beacon. [WHCI] section 3.1.4.2. */
enum uwb_rc_beacon_type {
	UWB_RC_BEACON_TYPE_SCAN = 0,
	UWB_RC_BEACON_TYPE_NEIGHBOR,
	UWB_RC_BEACON_TYPE_OL_ALIEN,
	UWB_RC_BEACON_TYPE_NOL_ALIEN,
};

/* Beacon received notification. [WHCI] 3.1.4.2. */
struct uwb_rc_evt_beacon {
	struct uwb_rceb rceb;
	u8	bChannelNumber;
	u8	bBeaconType;
	__le16	wBPSTOffset;
	u8	bLQI;
	u8	bRSSI;
	__le16	wBeaconInfoLength;
	u8	BeaconInfo[];
} __attribute__((packed));


/* Beacon Size Change notification. [WHCI] section 3.1.4.3 */
struct uwb_rc_evt_beacon_size {
	struct uwb_rceb rceb;
	__le16 wNewBeaconSize;
} __attribute__((packed));


/* BPOIE Change notification. [WHCI] section 3.1.4.4. */
struct uwb_rc_evt_bpoie_change {
	struct uwb_rceb rceb;
	__le16 wBPOIELength;
	u8 BPOIE[];
} __attribute__((packed));


/* Beacon Slot Change notification. [WHCI] section 3.1.4.5. */
struct uwb_rc_evt_bp_slot_change {
	struct uwb_rceb rceb;
	u8 slot_info;
} __attribute__((packed));

static inline int uwb_rc_evt_bp_slot_change_slot_num(
	const struct uwb_rc_evt_bp_slot_change *evt)
{
	return evt->slot_info & 0x7f;
}

static inline int uwb_rc_evt_bp_slot_change_no_slot(
	const struct uwb_rc_evt_bp_slot_change *evt)
{
	return (evt->slot_info & 0x80) >> 7;
}

/* BP Switch IE Received notification. [WHCI] section 3.1.4.6. */
struct uwb_rc_evt_bp_switch_ie_rcv {
	struct uwb_rceb rceb;
	struct uwb_dev_addr wSrcAddr;
	__le16 wIELength;
	u8 IEData[];
} __attribute__((packed));

/* DevAddr Conflict notification. [WHCI] section 3.1.4.7. */
struct uwb_rc_evt_dev_addr_conflict {
	struct uwb_rceb rceb;
} __attribute__((packed));

/* DRP notification. [WHCI] section 3.1.4.9. */
struct uwb_rc_evt_drp {
	struct uwb_rceb           rceb;
	struct uwb_dev_addr       src_addr;
	u8                        reason;
	u8                        beacon_slot_number;
	__le16                    ie_length;
	u8                        ie_data[];
} __attribute__((packed));

static inline enum uwb_drp_notif_reason uwb_rc_evt_drp_reason(struct uwb_rc_evt_drp *evt)
{
	return evt->reason & 0x0f;
}


/* DRP Availability Change notification. [WHCI] section 3.1.4.8. */
struct uwb_rc_evt_drp_avail {
	struct uwb_rceb rceb;
	DECLARE_BITMAP(bmp, UWB_NUM_MAS);
} __attribute__((packed));

/* BP switch status notification. [WHCI] section 3.1.4.10. */
struct uwb_rc_evt_bp_switch_status {
	struct uwb_rceb rceb;
	u8 status;
	u8 slot_offset;
	__le16 bpst_offset;
	u8 move_countdown;
} __attribute__((packed));

/* Command Frame Received notification. [WHCI] section 3.1.4.11. */
struct uwb_rc_evt_cmd_frame_rcv {
	struct uwb_rceb rceb;
	__le16 receive_time;
	struct uwb_dev_addr wSrcAddr;
	struct uwb_dev_addr wDstAddr;
	__le16 control;
	__le16 reserved;
	__le16 dataLength;
	u8 data[];
} __attribute__((packed));

/* Channel Change IE Received notification. [WHCI] section 3.1.4.12. */
struct uwb_rc_evt_channel_change_ie_rcv {
	struct uwb_rceb rceb;
	struct uwb_dev_addr wSrcAddr;
	__le16 wIELength;
	u8 IEData[];
} __attribute__((packed));

/* DAA Energy Detected notification. [WHCI 0.96] section 3.1.4.14. */
struct uwb_rc_evt_daa_energy_detected {
	struct uwb_rceb rceb;
	__le16 wLength;
	u8 bandID;
	u8 reserved;
	u8 toneBmp[16];
} __attribute__((packed));


/**
 * Radio Control Interface Class Descriptor
 *
 *  WUSB 1.0 [8.6.1.2]
 */
struct uwb_rc_control_intf_class_desc {
	u8 bLength;
	u8 bDescriptorType;
	__le16 bcdRCIVersion;
} __attribute__((packed));

#endif /* #ifndef __LINUX__UWB_SPEC_H__ */
