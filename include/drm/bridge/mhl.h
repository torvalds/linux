/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defines for Mobile High-Definition Link (MHL) interface
 *
 * Copyright (C) 2015, Samsung Electronics, Co., Ltd.
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * Based on MHL driver for Android devices.
 * Copyright (C) 2013-2014 Silicon Image, Inc.
 */

#ifndef __MHL_H__
#define __MHL_H__

#include <linux/types.h>

/* Device Capabilities Registers */
enum {
	MHL_DCAP_DEV_STATE,
	MHL_DCAP_MHL_VERSION,
	MHL_DCAP_CAT,
	MHL_DCAP_ADOPTER_ID_H,
	MHL_DCAP_ADOPTER_ID_L,
	MHL_DCAP_VID_LINK_MODE,
	MHL_DCAP_AUD_LINK_MODE,
	MHL_DCAP_VIDEO_TYPE,
	MHL_DCAP_LOG_DEV_MAP,
	MHL_DCAP_BANDWIDTH,
	MHL_DCAP_FEATURE_FLAG,
	MHL_DCAP_DEVICE_ID_H,
	MHL_DCAP_DEVICE_ID_L,
	MHL_DCAP_SCRATCHPAD_SIZE,
	MHL_DCAP_INT_STAT_SIZE,
	MHL_DCAP_RESERVED,
	MHL_DCAP_SIZE
};

#define MHL_DCAP_CAT_SINK			0x01
#define MHL_DCAP_CAT_SOURCE			0x02
#define MHL_DCAP_CAT_POWER			0x10
#define MHL_DCAP_CAT_PLIM(x)			((x) << 5)

#define MHL_DCAP_VID_LINK_RGB444		0x01
#define MHL_DCAP_VID_LINK_YCBCR444		0x02
#define MHL_DCAP_VID_LINK_YCBCR422		0x04
#define MHL_DCAP_VID_LINK_PPIXEL		0x08
#define MHL_DCAP_VID_LINK_ISLANDS		0x10
#define MHL_DCAP_VID_LINK_VGA			0x20
#define MHL_DCAP_VID_LINK_16BPP			0x40

#define MHL_DCAP_AUD_LINK_2CH			0x01
#define MHL_DCAP_AUD_LINK_8CH			0x02

#define MHL_DCAP_VT_GRAPHICS			0x00
#define MHL_DCAP_VT_PHOTO			0x02
#define MHL_DCAP_VT_CINEMA			0x04
#define MHL_DCAP_VT_GAMES			0x08
#define MHL_DCAP_SUPP_VT			0x80

#define MHL_DCAP_LD_DISPLAY			0x01
#define MHL_DCAP_LD_VIDEO			0x02
#define MHL_DCAP_LD_AUDIO			0x04
#define MHL_DCAP_LD_MEDIA			0x08
#define MHL_DCAP_LD_TUNER			0x10
#define MHL_DCAP_LD_RECORD			0x20
#define MHL_DCAP_LD_SPEAKER			0x40
#define MHL_DCAP_LD_GUI				0x80
#define MHL_DCAP_LD_ALL				0xFF

#define MHL_DCAP_FEATURE_RCP_SUPPORT		0x01
#define MHL_DCAP_FEATURE_RAP_SUPPORT		0x02
#define MHL_DCAP_FEATURE_SP_SUPPORT		0x04
#define MHL_DCAP_FEATURE_UCP_SEND_SUPPOR	0x08
#define MHL_DCAP_FEATURE_UCP_RECV_SUPPORT	0x10
#define MHL_DCAP_FEATURE_RBP_SUPPORT		0x40

/* Extended Device Capabilities Registers */
enum {
	MHL_XDC_ECBUS_SPEEDS,
	MHL_XDC_TMDS_SPEEDS,
	MHL_XDC_ECBUS_ROLES,
	MHL_XDC_LOG_DEV_MAPX,
	MHL_XDC_SIZE
};

#define MHL_XDC_ECBUS_S_075			0x01
#define MHL_XDC_ECBUS_S_8BIT			0x02
#define MHL_XDC_ECBUS_S_12BIT			0x04
#define MHL_XDC_ECBUS_D_150			0x10
#define MHL_XDC_ECBUS_D_8BIT			0x20

#define MHL_XDC_TMDS_000			0x00
#define MHL_XDC_TMDS_150			0x01
#define MHL_XDC_TMDS_300			0x02
#define MHL_XDC_TMDS_600			0x04

/* MHL_XDC_ECBUS_ROLES flags */
#define MHL_XDC_DEV_HOST			0x01
#define MHL_XDC_DEV_DEVICE			0x02
#define MHL_XDC_DEV_CHARGER			0x04
#define MHL_XDC_HID_HOST			0x08
#define MHL_XDC_HID_DEVICE			0x10

/* MHL_XDC_LOG_DEV_MAPX flags */
#define MHL_XDC_LD_PHONE			0x01

/* Device Status Registers */
enum {
	MHL_DST_CONNECTED_RDY,
	MHL_DST_LINK_MODE,
	MHL_DST_VERSION,
	MHL_DST_SIZE
};

/* Offset of DEVSTAT registers */
#define MHL_DST_OFFSET				0x30
#define MHL_DST_REG(name) (MHL_DST_OFFSET + MHL_DST_##name)

#define MHL_DST_CONN_DCAP_RDY			0x01
#define MHL_DST_CONN_XDEVCAPP_SUPP		0x02
#define MHL_DST_CONN_POW_STAT			0x04
#define MHL_DST_CONN_PLIM_STAT_MASK		0x38

#define MHL_DST_LM_CLK_MODE_MASK		0x07
#define MHL_DST_LM_CLK_MODE_PACKED_PIXEL	0x02
#define MHL_DST_LM_CLK_MODE_NORMAL		0x03
#define MHL_DST_LM_PATH_EN_MASK			0x08
#define MHL_DST_LM_PATH_ENABLED			0x08
#define MHL_DST_LM_PATH_DISABLED		0x00
#define MHL_DST_LM_MUTED_MASK			0x10

/* Extended Device Status Registers */
enum {
	MHL_XDS_CURR_ECBUS_MODE,
	MHL_XDS_AVLINK_MODE_STATUS,
	MHL_XDS_AVLINK_MODE_CONTROL,
	MHL_XDS_MULTI_SINK_STATUS,
	MHL_XDS_SIZE
};

/* Offset of XDEVSTAT registers */
#define MHL_XDS_OFFSET				0x90
#define MHL_XDS_REG(name) (MHL_XDS_OFFSET + MHL_XDS_##name)

/* MHL_XDS_REG_CURR_ECBUS_MODE flags */
#define MHL_XDS_SLOT_MODE_8BIT			0x00
#define MHL_XDS_SLOT_MODE_6BIT			0x01
#define MHL_XDS_ECBUS_S				0x04
#define MHL_XDS_ECBUS_D				0x08

#define MHL_XDS_LINK_CLOCK_75MHZ		0x00
#define MHL_XDS_LINK_CLOCK_150MHZ		0x10
#define MHL_XDS_LINK_CLOCK_300MHZ		0x20
#define MHL_XDS_LINK_CLOCK_600MHZ		0x30

#define MHL_XDS_LINK_STATUS_NO_SIGNAL		0x00
#define MHL_XDS_LINK_STATUS_CRU_LOCKED		0x01
#define MHL_XDS_LINK_STATUS_TMDS_NORMAL		0x02
#define MHL_XDS_LINK_STATUS_TMDS_RESERVED	0x03

#define MHL_XDS_LINK_RATE_1_5_GBPS		0x00
#define MHL_XDS_LINK_RATE_3_0_GBPS		0x01
#define MHL_XDS_LINK_RATE_6_0_GBPS		0x02
#define MHL_XDS_ATT_CAPABLE			0x08

#define MHL_XDS_SINK_STATUS_1_HPD_LOW		0x00
#define MHL_XDS_SINK_STATUS_1_HPD_HIGH		0x01
#define MHL_XDS_SINK_STATUS_2_HPD_LOW		0x00
#define MHL_XDS_SINK_STATUS_2_HPD_HIGH		0x04
#define MHL_XDS_SINK_STATUS_3_HPD_LOW		0x00
#define MHL_XDS_SINK_STATUS_3_HPD_HIGH		0x10
#define MHL_XDS_SINK_STATUS_4_HPD_LOW		0x00
#define MHL_XDS_SINK_STATUS_4_HPD_HIGH		0x40

/* Interrupt Registers */
enum {
	MHL_INT_RCHANGE,
	MHL_INT_DCHANGE,
	MHL_INT_SIZE
};

/* Offset of DEVSTAT registers */
#define MHL_INT_OFFSET				0x20
#define MHL_INT_REG(name) (MHL_INT_OFFSET + MHL_INT_##name)

#define	MHL_INT_RC_DCAP_CHG			0x01
#define MHL_INT_RC_DSCR_CHG			0x02
#define MHL_INT_RC_REQ_WRT			0x04
#define MHL_INT_RC_GRT_WRT			0x08
#define MHL_INT_RC_3D_REQ			0x10
#define MHL_INT_RC_FEAT_REQ			0x20
#define MHL_INT_RC_FEAT_COMPLETE		0x40

#define MHL_INT_DC_EDID_CHG			0x02

enum {
	MHL_ACK = 0x33, /* Command or Data byte acknowledge */
	MHL_NACK = 0x34, /* Command or Data byte not acknowledge */
	MHL_ABORT = 0x35, /* Transaction abort */
	MHL_WRITE_STAT = 0xe0, /* Write one status register */
	MHL_SET_INT = 0x60, /* Write one interrupt register */
	MHL_READ_DEVCAP_REG = 0x61, /* Read one register */
	MHL_GET_STATE = 0x62, /* Read CBUS revision level from follower */
	MHL_GET_VENDOR_ID = 0x63, /* Read vendor ID value from follower */
	MHL_SET_HPD = 0x64, /* Set Hot Plug Detect in follower */
	MHL_CLR_HPD = 0x65, /* Clear Hot Plug Detect in follower */
	MHL_SET_CAP_ID = 0x66, /* Set Capture ID for downstream device */
	MHL_GET_CAP_ID = 0x67, /* Get Capture ID from downstream device */
	MHL_MSC_MSG = 0x68, /* VS command to send RCP sub-commands */
	MHL_GET_SC1_ERRORCODE = 0x69, /* Get Vendor-Specific error code */
	MHL_GET_DDC_ERRORCODE = 0x6A, /* Get DDC channel command error code */
	MHL_GET_MSC_ERRORCODE = 0x6B, /* Get MSC command error code */
	MHL_WRITE_BURST = 0x6C, /* Write 1-16 bytes to responder's scratchpad */
	MHL_GET_SC3_ERRORCODE = 0x6D, /* Get channel 3 command error code */
	MHL_WRITE_XSTAT = 0x70, /* Write one extended status register */
	MHL_READ_XDEVCAP_REG = 0x71, /* Read one extended devcap register */
	/* let the rest of these float, they are software specific */
	MHL_READ_EDID_BLOCK,
	MHL_SEND_3D_REQ_OR_FEAT_REQ,
	MHL_READ_DEVCAP,
	MHL_READ_XDEVCAP
};

/* MSC message types */
enum {
	MHL_MSC_MSG_RCP = 0x10, /* RCP sub-command */
	MHL_MSC_MSG_RCPK = 0x11, /* RCP Acknowledge sub-command */
	MHL_MSC_MSG_RCPE = 0x12, /* RCP Error sub-command */
	MHL_MSC_MSG_RAP = 0x20, /* Mode Change Warning sub-command */
	MHL_MSC_MSG_RAPK = 0x21, /* MCW Acknowledge sub-command */
	MHL_MSC_MSG_RBP = 0x22, /* Remote Button Protocol sub-command */
	MHL_MSC_MSG_RBPK = 0x23, /* RBP Acknowledge sub-command */
	MHL_MSC_MSG_RBPE = 0x24, /* RBP Error sub-command */
	MHL_MSC_MSG_UCP = 0x30, /* UCP sub-command */
	MHL_MSC_MSG_UCPK = 0x31, /* UCP Acknowledge sub-command */
	MHL_MSC_MSG_UCPE = 0x32, /* UCP Error sub-command */
	MHL_MSC_MSG_RUSB = 0x40, /* Request USB host role */
	MHL_MSC_MSG_RUSBK = 0x41, /* Acknowledge request for USB host role */
	MHL_MSC_MSG_RHID = 0x42, /* Request HID host role */
	MHL_MSC_MSG_RHIDK = 0x43, /* Acknowledge request for HID host role */
	MHL_MSC_MSG_ATT = 0x50, /* Request attention sub-command */
	MHL_MSC_MSG_ATTK = 0x51, /* ATT Acknowledge sub-command */
	MHL_MSC_MSG_BIST_TRIGGER = 0x60,
	MHL_MSC_MSG_BIST_REQUEST_STAT = 0x61,
	MHL_MSC_MSG_BIST_READY = 0x62,
	MHL_MSC_MSG_BIST_STOP = 0x63,
};

/* RAP action codes */
#define MHL_RAP_POLL		0x00	/* Just do an ack */
#define MHL_RAP_CONTENT_ON	0x10	/* Turn content stream ON */
#define MHL_RAP_CONTENT_OFF	0x11	/* Turn content stream OFF */
#define MHL_RAP_CBUS_MODE_DOWN	0x20
#define MHL_RAP_CBUS_MODE_UP	0x21

/* RAPK status codes */
#define MHL_RAPK_NO_ERR		0x00	/* RAP action recognized & supported */
#define MHL_RAPK_UNRECOGNIZED	0x01	/* Unknown RAP action code received */
#define MHL_RAPK_UNSUPPORTED	0x02	/* Rcvd RAP action code not supported */
#define MHL_RAPK_BUSY		0x03	/* Responder too busy to respond */

/* Bit masks for RCP messages */
#define MHL_RCP_KEY_RELEASED_MASK	0x80
#define MHL_RCP_KEY_ID_MASK		0x7F

/*
 * Error status codes for RCPE messages
 */
/* No error. (Not allowed in RCPE messages) */
#define MHL_RCPE_STATUS_NO_ERROR		0x00
/* Unsupported/unrecognized key code */
#define MHL_RCPE_STATUS_INEFFECTIVE_KEY_CODE	0x01
/* Responder busy. Initiator may retry message */
#define MHL_RCPE_STATUS_BUSY			0x02

/*
 * Error status codes for RBPE messages
 */
/* No error. (Not allowed in RBPE messages) */
#define MHL_RBPE_STATUS_NO_ERROR		0x00
/* Unsupported/unrecognized button code */
#define MHL_RBPE_STATUS_INEFFECTIVE_BUTTON_CODE	0x01
/* Responder busy. Initiator may retry message */
#define MHL_RBPE_STATUS_BUSY			0x02

/*
 * Error status codes for UCPE messages
 */
/* No error. (Not allowed in UCPE messages) */
#define MHL_UCPE_STATUS_NO_ERROR		0x00
/* Unsupported/unrecognized key code */
#define MHL_UCPE_STATUS_INEFFECTIVE_KEY_CODE	0x01

enum mhl_burst_id {
	MHL_BURST_ID_3D_VIC = 0x10,
	MHL_BURST_ID_3D_DTD = 0x11,
	MHL_BURST_ID_HEV_VIC = 0x20,
	MHL_BURST_ID_HEV_DTDA = 0x21,
	MHL_BURST_ID_HEV_DTDB = 0x22,
	MHL_BURST_ID_VC_ASSIGN = 0x38,
	MHL_BURST_ID_VC_CONFIRM = 0x39,
	MHL_BURST_ID_AUD_DELAY = 0x40,
	MHL_BURST_ID_ADT_BURSTID = 0x41,
	MHL_BURST_ID_BIST_SETUP = 0x51,
	MHL_BURST_ID_BIST_RETURN_STAT = 0x52,
	MHL_BURST_ID_EMSC_SUPPORT = 0x61,
	MHL_BURST_ID_HID_PAYLOAD = 0x62,
	MHL_BURST_ID_BLK_RCV_BUFFER_INFO = 0x63,
	MHL_BURST_ID_BITS_PER_PIXEL_FMT = 0x64,
};

struct mhl_burst_blk_rcv_buffer_info {
	__be16 id;
	__le16 size;
} __packed;

struct mhl3_burst_header {
	__be16 id;
	u8 checksum;
	u8 total_entries;
	u8 sequence_index;
} __packed;

struct mhl_burst_bits_per_pixel_fmt {
	struct mhl3_burst_header hdr;
	u8 num_entries;
	struct {
		u8 stream_id;
		u8 pixel_format;
	} __packed desc[0];
} __packed;

struct mhl_burst_emsc_support {
	struct mhl3_burst_header hdr;
	u8 num_entries;
	__be16 burst_id[0];
} __packed;

struct mhl_burst_audio_descr {
	struct mhl3_burst_header hdr;
	u8 flags;
	u8 short_desc[9];
} __packed;

/*
 * MHL3 infoframe related definitions
 */

#define MHL3_IEEE_OUI		0x7ca61d
#define MHL3_INFOFRAME_SIZE	15

enum mhl3_video_format {
	MHL3_VIDEO_FORMAT_NONE,
	MHL3_VIDEO_FORMAT_3D,
	MHL3_VIDEO_FORMAT_MULTI_VIEW,
	MHL3_VIDEO_FORMAT_DUAL_3D
};

enum mhl3_3d_format_type {
	MHL3_3D_FORMAT_TYPE_FS, /* frame sequential */
	MHL3_3D_FORMAT_TYPE_TB, /* top-bottom */
	MHL3_3D_FORMAT_TYPE_LR, /* left-right */
	MHL3_3D_FORMAT_TYPE_FS_TB, /* frame sequential, top-bottom */
	MHL3_3D_FORMAT_TYPE_FS_LR, /* frame sequential, left-right */
	MHL3_3D_FORMAT_TYPE_TB_LR /* top-bottom, left-right */
};

struct mhl3_infoframe {
	unsigned char version;
	enum mhl3_video_format video_format;
	enum mhl3_3d_format_type format_type;
	bool sep_audio;
	int hev_format;
	int av_delay;
};

#endif /* __MHL_H__ */
