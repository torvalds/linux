/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IEEE 802.11 defines
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright (c) 2005, Devicescape Software, Inc.
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright (c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright (c) 2018 - 2026 Intel Corporation
 */

#ifndef LINUX_IEEE80211_H
#define LINUX_IEEE80211_H

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/bitfield.h>
#include <asm/byteorder.h>
#include <linux/unaligned.h>

/*
 * DS bit usage
 *
 * TA = transmitter address
 * RA = receiver address
 * DA = destination address
 * SA = source address
 *
 * ToDS    FromDS  A1(RA)  A2(TA)  A3      A4      Use
 * -----------------------------------------------------------------
 *  0       0       DA      SA      BSSID   -       IBSS/DLS
 *  0       1       DA      BSSID   SA      -       AP -> STA
 *  1       0       BSSID   SA      DA      -       AP <- STA
 *  1       1       RA      TA      DA      SA      unspecified (WDS)
 */

#define FCS_LEN 4

#define IEEE80211_FCTL_VERS		0x0003
#define IEEE80211_FCTL_FTYPE		0x000c
#define IEEE80211_FCTL_STYPE		0x00f0
#define IEEE80211_FCTL_TYPE		(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)
#define IEEE80211_FCTL_TODS		0x0100
#define IEEE80211_FCTL_FROMDS		0x0200
#define IEEE80211_FCTL_MOREFRAGS	0x0400
#define IEEE80211_FCTL_RETRY		0x0800
#define IEEE80211_FCTL_PM		0x1000
#define IEEE80211_FCTL_MOREDATA		0x2000
#define IEEE80211_FCTL_PROTECTED	0x4000
#define IEEE80211_FCTL_ORDER		0x8000
#define IEEE80211_FCTL_CTL_EXT		0x0f00

#define IEEE80211_SCTL_FRAG		0x000F
#define IEEE80211_SCTL_SEQ		0xFFF0

#define IEEE80211_FTYPE_MGMT		0x0000
#define IEEE80211_FTYPE_CTL		0x0004
#define IEEE80211_FTYPE_DATA		0x0008
#define IEEE80211_FTYPE_EXT		0x000c

/* management */
#define IEEE80211_STYPE_ASSOC_REQ	0x0000
#define IEEE80211_STYPE_ASSOC_RESP	0x0010
#define IEEE80211_STYPE_REASSOC_REQ	0x0020
#define IEEE80211_STYPE_REASSOC_RESP	0x0030
#define IEEE80211_STYPE_PROBE_REQ	0x0040
#define IEEE80211_STYPE_PROBE_RESP	0x0050
#define IEEE80211_STYPE_BEACON		0x0080
#define IEEE80211_STYPE_ATIM		0x0090
#define IEEE80211_STYPE_DISASSOC	0x00A0
#define IEEE80211_STYPE_AUTH		0x00B0
#define IEEE80211_STYPE_DEAUTH		0x00C0
#define IEEE80211_STYPE_ACTION		0x00D0

/* control */
#define IEEE80211_STYPE_TRIGGER		0x0020
#define IEEE80211_STYPE_CTL_EXT		0x0060
#define IEEE80211_STYPE_BACK_REQ	0x0080
#define IEEE80211_STYPE_BACK		0x0090
#define IEEE80211_STYPE_PSPOLL		0x00A0
#define IEEE80211_STYPE_RTS		0x00B0
#define IEEE80211_STYPE_CTS		0x00C0
#define IEEE80211_STYPE_ACK		0x00D0
#define IEEE80211_STYPE_CFEND		0x00E0
#define IEEE80211_STYPE_CFENDACK	0x00F0

/* data */
#define IEEE80211_STYPE_DATA			0x0000
#define IEEE80211_STYPE_DATA_CFACK		0x0010
#define IEEE80211_STYPE_DATA_CFPOLL		0x0020
#define IEEE80211_STYPE_DATA_CFACKPOLL		0x0030
#define IEEE80211_STYPE_NULLFUNC		0x0040
#define IEEE80211_STYPE_CFACK			0x0050
#define IEEE80211_STYPE_CFPOLL			0x0060
#define IEEE80211_STYPE_CFACKPOLL		0x0070
#define IEEE80211_STYPE_QOS_DATA		0x0080
#define IEEE80211_STYPE_QOS_DATA_CFACK		0x0090
#define IEEE80211_STYPE_QOS_DATA_CFPOLL		0x00A0
#define IEEE80211_STYPE_QOS_DATA_CFACKPOLL	0x00B0
#define IEEE80211_STYPE_QOS_NULLFUNC		0x00C0
#define IEEE80211_STYPE_QOS_CFACK		0x00D0
#define IEEE80211_STYPE_QOS_CFPOLL		0x00E0
#define IEEE80211_STYPE_QOS_CFACKPOLL		0x00F0

/* extension, added by 802.11ad */
#define IEEE80211_STYPE_DMG_BEACON		0x0000
#define IEEE80211_STYPE_S1G_BEACON		0x0010

#define IEEE80211_NDP_FTYPE_CTS			0
#define IEEE80211_NDP_FTYPE_CF_END		0
#define IEEE80211_NDP_FTYPE_PS_POLL		1
#define IEEE80211_NDP_FTYPE_ACK			2
#define IEEE80211_NDP_FTYPE_PS_POLL_ACK		3
#define IEEE80211_NDP_FTYPE_BA			4
#define IEEE80211_NDP_FTYPE_BF_REPORT_POLL	5
#define IEEE80211_NDP_FTYPE_PAGING		6
#define IEEE80211_NDP_FTYPE_PREQ		7

#define SM64(f, v)	((((u64)v) << f##_S) & f)

/* NDP CMAC frame fields */
#define IEEE80211_NDP_FTYPE                    0x0000000000000007
#define IEEE80211_NDP_FTYPE_S                  0x0000000000000000

/* 1M Probe Request 11ah 9.9.3.1.1 */
#define IEEE80211_NDP_1M_PREQ_ANO      0x0000000000000008
#define IEEE80211_NDP_1M_PREQ_ANO_S                     3
#define IEEE80211_NDP_1M_PREQ_CSSID    0x00000000000FFFF0
#define IEEE80211_NDP_1M_PREQ_CSSID_S                   4
#define IEEE80211_NDP_1M_PREQ_RTYPE    0x0000000000100000
#define IEEE80211_NDP_1M_PREQ_RTYPE_S                  20
#define IEEE80211_NDP_1M_PREQ_RSV      0x0000000001E00000
#define IEEE80211_NDP_1M_PREQ_RSV      0x0000000001E00000
/* 2M Probe Request 11ah 9.9.3.1.2 */
#define IEEE80211_NDP_2M_PREQ_ANO      0x0000000000000008
#define IEEE80211_NDP_2M_PREQ_ANO_S                     3
#define IEEE80211_NDP_2M_PREQ_CSSID    0x0000000FFFFFFFF0
#define IEEE80211_NDP_2M_PREQ_CSSID_S                   4
#define IEEE80211_NDP_2M_PREQ_RTYPE    0x0000001000000000
#define IEEE80211_NDP_2M_PREQ_RTYPE_S                  36

#define IEEE80211_ANO_NETTYPE_WILD              15

/* control extension - for IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTL_EXT */
#define IEEE80211_CTL_EXT_POLL		0x2000
#define IEEE80211_CTL_EXT_SPR		0x3000
#define IEEE80211_CTL_EXT_GRANT	0x4000
#define IEEE80211_CTL_EXT_DMG_CTS	0x5000
#define IEEE80211_CTL_EXT_DMG_DTS	0x6000
#define IEEE80211_CTL_EXT_SSW		0x8000
#define IEEE80211_CTL_EXT_SSW_FBACK	0x9000
#define IEEE80211_CTL_EXT_SSW_ACK	0xa000


#define IEEE80211_SN_MASK		((IEEE80211_SCTL_SEQ) >> 4)
#define IEEE80211_MAX_SN		IEEE80211_SN_MASK
#define IEEE80211_SN_MODULO		(IEEE80211_MAX_SN + 1)


/* PV1 Layout IEEE 802.11-2020 9.8.3.1 */
#define IEEE80211_PV1_FCTL_VERS		0x0003
#define IEEE80211_PV1_FCTL_FTYPE	0x001c
#define IEEE80211_PV1_FCTL_STYPE	0x00e0
#define IEEE80211_PV1_FCTL_FROMDS		0x0100
#define IEEE80211_PV1_FCTL_MOREFRAGS	0x0200
#define IEEE80211_PV1_FCTL_PM		0x0400
#define IEEE80211_PV1_FCTL_MOREDATA	0x0800
#define IEEE80211_PV1_FCTL_PROTECTED	0x1000
#define IEEE80211_PV1_FCTL_END_SP       0x2000
#define IEEE80211_PV1_FCTL_RELAYED      0x4000
#define IEEE80211_PV1_FCTL_ACK_POLICY   0x8000
#define IEEE80211_PV1_FCTL_CTL_EXT	0x0f00

static inline bool ieee80211_sn_less(u16 sn1, u16 sn2)
{
	return ((sn1 - sn2) & IEEE80211_SN_MASK) > (IEEE80211_SN_MODULO >> 1);
}

static inline bool ieee80211_sn_less_eq(u16 sn1, u16 sn2)
{
	return ((sn2 - sn1) & IEEE80211_SN_MASK) <= (IEEE80211_SN_MODULO >> 1);
}

static inline u16 ieee80211_sn_add(u16 sn1, u16 sn2)
{
	return (sn1 + sn2) & IEEE80211_SN_MASK;
}

static inline u16 ieee80211_sn_inc(u16 sn)
{
	return ieee80211_sn_add(sn, 1);
}

static inline u16 ieee80211_sn_sub(u16 sn1, u16 sn2)
{
	return (sn1 - sn2) & IEEE80211_SN_MASK;
}

#define IEEE80211_SEQ_TO_SN(seq)	(((seq) & IEEE80211_SCTL_SEQ) >> 4)
#define IEEE80211_SN_TO_SEQ(ssn)	(((ssn) << 4) & IEEE80211_SCTL_SEQ)

/* miscellaneous IEEE 802.11 constants */
#define IEEE80211_MAX_FRAG_THRESHOLD	2352
#define IEEE80211_MAX_RTS_THRESHOLD	2353
#define IEEE80211_MAX_AID		2007
#define IEEE80211_MAX_AID_S1G		8191
#define IEEE80211_MAX_TIM_LEN		251
#define IEEE80211_MAX_MESH_PEERINGS	63

/* Maximum size for the MA-UNITDATA primitive, 802.11 standard section
   6.2.1.1.2.

   802.11e clarifies the figure in section 7.1.2. The frame body is
   up to 2304 octets long (maximum MSDU size) plus any crypt overhead. */
#define IEEE80211_MAX_DATA_LEN		2304
/* 802.11ad extends maximum MSDU size for DMG (freq > 40Ghz) networks
 * to 7920 bytes, see 8.2.3 General frame format
 */
#define IEEE80211_MAX_DATA_LEN_DMG	7920
/* 30 byte 4 addr hdr, 2 byte QoS, 2304 byte MSDU, 12 byte crypt, 4 byte FCS */
#define IEEE80211_MAX_FRAME_LEN		2352

#define IEEE80211_MAX_SSID_LEN		32

#define IEEE80211_FIRST_TSPEC_TSID	8
#define IEEE80211_NUM_TIDS		16

/* number of user priorities 802.11 uses */
#define IEEE80211_NUM_UPS		8
/* number of ACs */
#define IEEE80211_NUM_ACS		4

#define IEEE80211_QOS_CTL_LEN		2
/* 1d tag mask */
#define IEEE80211_QOS_CTL_TAG1D_MASK		0x0007
/* TID mask */
#define IEEE80211_QOS_CTL_TID_MASK		0x000f
/* EOSP */
#define IEEE80211_QOS_CTL_EOSP			0x0010
/* ACK policy */
#define IEEE80211_QOS_CTL_ACK_POLICY_NORMAL	0x0000
#define IEEE80211_QOS_CTL_ACK_POLICY_NOACK	0x0020
#define IEEE80211_QOS_CTL_ACK_POLICY_NO_EXPL	0x0040
#define IEEE80211_QOS_CTL_ACK_POLICY_BLOCKACK	0x0060
#define IEEE80211_QOS_CTL_ACK_POLICY_MASK	0x0060
/* A-MSDU 802.11n */
#define IEEE80211_QOS_CTL_A_MSDU_PRESENT	0x0080
/* Mesh Control 802.11s */
#define IEEE80211_QOS_CTL_MESH_CONTROL_PRESENT  0x0100

/* Mesh Power Save Level */
#define IEEE80211_QOS_CTL_MESH_PS_LEVEL		0x0200
/* Mesh Receiver Service Period Initiated */
#define IEEE80211_QOS_CTL_RSPI			0x0400

/* U-APSD queue for WMM IEs sent by AP */
#define IEEE80211_WMM_IE_AP_QOSINFO_UAPSD	(1<<7)
#define IEEE80211_WMM_IE_AP_QOSINFO_PARAM_SET_CNT_MASK	0x0f

/* U-APSD queues for WMM IEs sent by STA */
#define IEEE80211_WMM_IE_STA_QOSINFO_AC_VO	(1<<0)
#define IEEE80211_WMM_IE_STA_QOSINFO_AC_VI	(1<<1)
#define IEEE80211_WMM_IE_STA_QOSINFO_AC_BK	(1<<2)
#define IEEE80211_WMM_IE_STA_QOSINFO_AC_BE	(1<<3)
#define IEEE80211_WMM_IE_STA_QOSINFO_AC_MASK	0x0f

/* U-APSD max SP length for WMM IEs sent by STA */
#define IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL	0x00
#define IEEE80211_WMM_IE_STA_QOSINFO_SP_2	0x01
#define IEEE80211_WMM_IE_STA_QOSINFO_SP_4	0x02
#define IEEE80211_WMM_IE_STA_QOSINFO_SP_6	0x03
#define IEEE80211_WMM_IE_STA_QOSINFO_SP_MASK	0x03
#define IEEE80211_WMM_IE_STA_QOSINFO_SP_SHIFT	5

/* trigger type within common_info of trigger frame */
#define IEEE80211_TRIGGER_TYPE_MASK		0xf
#define IEEE80211_TRIGGER_TYPE_BASIC		0x0
#define IEEE80211_TRIGGER_TYPE_BFRP		0x1
#define IEEE80211_TRIGGER_TYPE_MU_BAR		0x2
#define IEEE80211_TRIGGER_TYPE_MU_RTS		0x3
#define IEEE80211_TRIGGER_TYPE_BSRP		0x4
#define IEEE80211_TRIGGER_TYPE_GCR_MU_BAR	0x5
#define IEEE80211_TRIGGER_TYPE_BQRP		0x6
#define IEEE80211_TRIGGER_TYPE_NFRP		0x7

/* UL-bandwidth within common_info of trigger frame */
#define IEEE80211_TRIGGER_ULBW_MASK		0xc0000
#define IEEE80211_TRIGGER_ULBW_20MHZ		0x0
#define IEEE80211_TRIGGER_ULBW_40MHZ		0x1
#define IEEE80211_TRIGGER_ULBW_80MHZ		0x2
#define IEEE80211_TRIGGER_ULBW_160_80P80MHZ	0x3

struct ieee80211_hdr {
	__le16 frame_control;
	__le16 duration_id;
	struct_group(addrs,
		u8 addr1[ETH_ALEN];
		u8 addr2[ETH_ALEN];
		u8 addr3[ETH_ALEN];
	);
	__le16 seq_ctrl;
	u8 addr4[ETH_ALEN];
} __packed __aligned(2);

struct ieee80211_hdr_3addr {
	__le16 frame_control;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctrl;
} __packed __aligned(2);

struct ieee80211_qos_hdr {
	__le16 frame_control;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctrl;
	__le16 qos_ctrl;
} __packed __aligned(2);

struct ieee80211_qos_hdr_4addr {
	__le16 frame_control;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctrl;
	u8 addr4[ETH_ALEN];
	__le16 qos_ctrl;
} __packed __aligned(2);

struct ieee80211_trigger {
	__le16 frame_control;
	__le16 duration;
	u8 ra[ETH_ALEN];
	u8 ta[ETH_ALEN];
	__le64 common_info;
	u8 variable[];
} __packed __aligned(2);

/**
 * ieee80211_has_tods - check if IEEE80211_FCTL_TODS is set
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame has to-DS set
 */
static inline bool ieee80211_has_tods(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_TODS)) != 0;
}

/**
 * ieee80211_has_fromds - check if IEEE80211_FCTL_FROMDS is set
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame has from-DS set
 */
static inline bool ieee80211_has_fromds(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FROMDS)) != 0;
}

/**
 * ieee80211_has_a4 - check if IEEE80211_FCTL_TODS and IEEE80211_FCTL_FROMDS are set
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not it's a 4-address frame (from-DS and to-DS set)
 */
static inline bool ieee80211_has_a4(__le16 fc)
{
	__le16 tmp = cpu_to_le16(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS);
	return (fc & tmp) == tmp;
}

/**
 * ieee80211_has_morefrags - check if IEEE80211_FCTL_MOREFRAGS is set
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame has more fragments (more frags bit set)
 */
static inline bool ieee80211_has_morefrags(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)) != 0;
}

/**
 * ieee80211_has_retry - check if IEEE80211_FCTL_RETRY is set
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the retry flag is set
 */
static inline bool ieee80211_has_retry(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_RETRY)) != 0;
}

/**
 * ieee80211_has_pm - check if IEEE80211_FCTL_PM is set
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the power management flag is set
 */
static inline bool ieee80211_has_pm(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_PM)) != 0;
}

/**
 * ieee80211_has_moredata - check if IEEE80211_FCTL_MOREDATA is set
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the more data flag is set
 */
static inline bool ieee80211_has_moredata(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_MOREDATA)) != 0;
}

/**
 * ieee80211_has_protected - check if IEEE80211_FCTL_PROTECTED is set
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the protected flag is set
 */
static inline bool ieee80211_has_protected(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_PROTECTED)) != 0;
}

/**
 * ieee80211_has_order - check if IEEE80211_FCTL_ORDER is set
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the order flag is set
 */
static inline bool ieee80211_has_order(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_ORDER)) != 0;
}

/**
 * ieee80211_is_mgmt - check if type is IEEE80211_FTYPE_MGMT
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame type is management
 */
static inline bool ieee80211_is_mgmt(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT);
}

/**
 * ieee80211_is_ctl - check if type is IEEE80211_FTYPE_CTL
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame type is control
 */
static inline bool ieee80211_is_ctl(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL);
}

/**
 * ieee80211_is_data - check if type is IEEE80211_FTYPE_DATA
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a data frame
 */
static inline bool ieee80211_is_data(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA);
}

/**
 * ieee80211_is_ext - check if type is IEEE80211_FTYPE_EXT
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame type is extended
 */
static inline bool ieee80211_is_ext(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_EXT);
}


/**
 * ieee80211_is_data_qos - check if type is IEEE80211_FTYPE_DATA and IEEE80211_STYPE_QOS_DATA is set
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a QoS data frame
 */
static inline bool ieee80211_is_data_qos(__le16 fc)
{
	/*
	 * mask with QOS_DATA rather than IEEE80211_FCTL_STYPE as we just need
	 * to check the one bit
	 */
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_STYPE_QOS_DATA)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA);
}

/**
 * ieee80211_is_data_present - check if type is IEEE80211_FTYPE_DATA and has data
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a QoS data frame that has data
 *	(i.e. is not null data)
 */
static inline bool ieee80211_is_data_present(__le16 fc)
{
	/*
	 * mask with 0x40 and test that that bit is clear to only return true
	 * for the data-containing substypes.
	 */
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | 0x40)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA);
}

/**
 * ieee80211_is_assoc_req - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_ASSOC_REQ
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is an association request
 */
static inline bool ieee80211_is_assoc_req(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ASSOC_REQ);
}

/**
 * ieee80211_is_assoc_resp - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_ASSOC_RESP
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is an association response
 */
static inline bool ieee80211_is_assoc_resp(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ASSOC_RESP);
}

/**
 * ieee80211_is_reassoc_req - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_REASSOC_REQ
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a reassociation request
 */
static inline bool ieee80211_is_reassoc_req(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_REASSOC_REQ);
}

/**
 * ieee80211_is_reassoc_resp - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_REASSOC_RESP
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a reassociation response
 */
static inline bool ieee80211_is_reassoc_resp(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_REASSOC_RESP);
}

/**
 * ieee80211_is_probe_req - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_PROBE_REQ
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a probe request
 */
static inline bool ieee80211_is_probe_req(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ);
}

/**
 * ieee80211_is_probe_resp - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_PROBE_RESP
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a probe response
 */
static inline bool ieee80211_is_probe_resp(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_RESP);
}

/**
 * ieee80211_is_beacon - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_BEACON
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a (regular, not S1G) beacon
 */
static inline bool ieee80211_is_beacon(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON);
}

/**
 * ieee80211_is_atim - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_ATIM
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is an ATIM frame
 */
static inline bool ieee80211_is_atim(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ATIM);
}

/**
 * ieee80211_is_disassoc - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_DISASSOC
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a disassociation frame
 */
static inline bool ieee80211_is_disassoc(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DISASSOC);
}

/**
 * ieee80211_is_auth - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_AUTH
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is an authentication frame
 */
static inline bool ieee80211_is_auth(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_AUTH);
}

/**
 * ieee80211_is_deauth - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_DEAUTH
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a deauthentication frame
 */
static inline bool ieee80211_is_deauth(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DEAUTH);
}

/**
 * ieee80211_is_action - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_ACTION
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is an action frame
 */
static inline bool ieee80211_is_action(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION);
}

/**
 * ieee80211_is_back_req - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_BACK_REQ
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a block-ACK request frame
 */
static inline bool ieee80211_is_back_req(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_BACK_REQ);
}

/**
 * ieee80211_is_back - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_BACK
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a block-ACK frame
 */
static inline bool ieee80211_is_back(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_BACK);
}

/**
 * ieee80211_is_pspoll - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_PSPOLL
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a PS-poll frame
 */
static inline bool ieee80211_is_pspoll(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_PSPOLL);
}

/**
 * ieee80211_is_rts - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_RTS
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is an RTS frame
 */
static inline bool ieee80211_is_rts(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS);
}

/**
 * ieee80211_is_cts - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_CTS
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a CTS frame
 */
static inline bool ieee80211_is_cts(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS);
}

/**
 * ieee80211_is_ack - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_ACK
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is an ACK frame
 */
static inline bool ieee80211_is_ack(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_ACK);
}

/**
 * ieee80211_is_cfend - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_CFEND
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a CF-end frame
 */
static inline bool ieee80211_is_cfend(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CFEND);
}

/**
 * ieee80211_is_cfendack - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_CFENDACK
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a CF-end-ack frame
 */
static inline bool ieee80211_is_cfendack(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CFENDACK);
}

/**
 * ieee80211_is_nullfunc - check if frame is a regular (non-QoS) nullfunc frame
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a nullfunc frame
 */
static inline bool ieee80211_is_nullfunc(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC);
}

/**
 * ieee80211_is_qos_nullfunc - check if frame is a QoS nullfunc frame
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a QoS nullfunc frame
 */
static inline bool ieee80211_is_qos_nullfunc(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_NULLFUNC);
}

/**
 * ieee80211_is_trigger - check if frame is trigger frame
 * @fc: frame control field in little-endian byteorder
 * Return: whether or not the frame is a trigger frame
 */
static inline bool ieee80211_is_trigger(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_TRIGGER);
}

/**
 * ieee80211_is_any_nullfunc - check if frame is regular or QoS nullfunc frame
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is a nullfunc or QoS nullfunc frame
 */
static inline bool ieee80211_is_any_nullfunc(__le16 fc)
{
	return (ieee80211_is_nullfunc(fc) || ieee80211_is_qos_nullfunc(fc));
}

/**
 * ieee80211_is_first_frag - check if IEEE80211_SCTL_FRAG is not set
 * @seq_ctrl: frame sequence control bytes in little-endian byteorder
 * Return: whether or not the frame is the first fragment (also true if
 *	it's not fragmented at all)
 */
static inline bool ieee80211_is_first_frag(__le16 seq_ctrl)
{
	return (seq_ctrl & cpu_to_le16(IEEE80211_SCTL_FRAG)) == 0;
}

/**
 * ieee80211_is_frag - check if a frame is a fragment
 * @hdr: 802.11 header of the frame
 * Return: whether or not the frame is a fragment
 */
static inline bool ieee80211_is_frag(struct ieee80211_hdr *hdr)
{
	return ieee80211_has_morefrags(hdr->frame_control) ||
	       hdr->seq_ctrl & cpu_to_le16(IEEE80211_SCTL_FRAG);
}

static inline u16 ieee80211_get_sn(struct ieee80211_hdr *hdr)
{
	return le16_get_bits(hdr->seq_ctrl, IEEE80211_SCTL_SEQ);
}

/**
 * struct ieee80211_quiet_ie - Quiet element
 * @count: Quiet Count
 * @period: Quiet Period
 * @duration: Quiet Duration
 * @offset: Quiet Offset
 *
 * This structure represents the payload of the "Quiet element" as
 * described in IEEE Std 802.11-2020 section 9.4.2.22.
 */
struct ieee80211_quiet_ie {
	u8 count;
	u8 period;
	__le16 duration;
	__le16 offset;
} __packed;

/**
 * struct ieee80211_msrment_ie - Measurement element
 * @token: Measurement Token
 * @mode: Measurement Report Mode
 * @type: Measurement Type
 * @request: Measurement Request or Measurement Report
 *
 * This structure represents the payload of both the "Measurement
 * Request element" and the "Measurement Report element" as described
 * in IEEE Std 802.11-2020 sections 9.4.2.20 and 9.4.2.21.
 */
struct ieee80211_msrment_ie {
	u8 token;
	u8 mode;
	u8 type;
	u8 request[];
} __packed;

/**
 * struct ieee80211_channel_sw_ie - Channel Switch Announcement element
 * @mode: Channel Switch Mode
 * @new_ch_num: New Channel Number
 * @count: Channel Switch Count
 *
 * This structure represents the payload of the "Channel Switch
 * Announcement element" as described in IEEE Std 802.11-2020 section
 * 9.4.2.18.
 */
struct ieee80211_channel_sw_ie {
	u8 mode;
	u8 new_ch_num;
	u8 count;
} __packed;

/**
 * struct ieee80211_ext_chansw_ie - Extended Channel Switch Announcement element
 * @mode: Channel Switch Mode
 * @new_operating_class: New Operating Class
 * @new_ch_num: New Channel Number
 * @count: Channel Switch Count
 *
 * This structure represents the "Extended Channel Switch Announcement
 * element" as described in IEEE Std 802.11-2020 section 9.4.2.52.
 */
struct ieee80211_ext_chansw_ie {
	u8 mode;
	u8 new_operating_class;
	u8 new_ch_num;
	u8 count;
} __packed;

/**
 * struct ieee80211_sec_chan_offs_ie - secondary channel offset IE
 * @sec_chan_offs: secondary channel offset, uses IEEE80211_HT_PARAM_CHA_SEC_*
 *	values here
 * This structure represents the "Secondary Channel Offset element"
 */
struct ieee80211_sec_chan_offs_ie {
	u8 sec_chan_offs;
} __packed;

/**
 * struct ieee80211_wide_bw_chansw_ie - wide bandwidth channel switch IE
 * @new_channel_width: New Channel Width
 * @new_center_freq_seg0: New Channel Center Frequency Segment 0
 * @new_center_freq_seg1: New Channel Center Frequency Segment 1
 *
 * This structure represents the payload of the "Wide Bandwidth
 * Channel Switch element" as described in IEEE Std 802.11-2020
 * section 9.4.2.160.
 */
struct ieee80211_wide_bw_chansw_ie {
	u8 new_channel_width;
	u8 new_center_freq_seg0, new_center_freq_seg1;
} __packed;

/**
 * struct ieee80211_tim_ie - Traffic Indication Map information element
 * @dtim_count: DTIM Count
 * @dtim_period: DTIM Period
 * @bitmap_ctrl: Bitmap Control
 * @required_octet: "Syntatic sugar" to force the struct size to the
 *                  minimum valid size when carried in a non-S1G PPDU
 * @virtual_map: Partial Virtual Bitmap
 *
 * This structure represents the payload of the "TIM element" as
 * described in IEEE Std 802.11-2020 section 9.4.2.5. Note that this
 * definition is only applicable when the element is carried in a
 * non-S1G PPDU. When the TIM is carried in an S1G PPDU, the Bitmap
 * Control and Partial Virtual Bitmap may not be present.
 */
struct ieee80211_tim_ie {
	u8 dtim_count;
	u8 dtim_period;
	u8 bitmap_ctrl;
	union {
		u8 required_octet;
		DECLARE_FLEX_ARRAY(u8, virtual_map);
	};
} __packed;

#define WLAN_SA_QUERY_TR_ID_LEN 2
#define WLAN_MEMBERSHIP_LEN 8
#define WLAN_USER_POSITION_LEN 16

/**
 * struct ieee80211_tpc_report_ie - TPC Report element
 * @tx_power: Transmit Power
 * @link_margin: Link Margin
 *
 * This structure represents the payload of the "TPC Report element" as
 * described in IEEE Std 802.11-2020 section 9.4.2.16.
 */
struct ieee80211_tpc_report_ie {
	u8 tx_power;
	u8 link_margin;
} __packed;

#define IEEE80211_ADDBA_EXT_FRAG_LEVEL_MASK	GENMASK(2, 1)
#define IEEE80211_ADDBA_EXT_FRAG_LEVEL_SHIFT	1
#define IEEE80211_ADDBA_EXT_NO_FRAG		BIT(0)
#define IEEE80211_ADDBA_EXT_BUF_SIZE_MASK	GENMASK(7, 5)
#define IEEE80211_ADDBA_EXT_BUF_SIZE_SHIFT	10

struct ieee80211_addba_ext_ie {
	u8 data;
} __packed;

struct ieee80211_ext {
	__le16 frame_control;
	__le16 duration;
	union {
		struct {
			u8 sa[ETH_ALEN];
			__le32 timestamp;
			u8 change_seq;
			u8 variable[];
		} __packed s1g_beacon;
	} u;
} __packed __aligned(2);

/**
 * struct ieee80211_bss_load_elem - BSS Load elemen
 *
 * Defined in section 9.4.2.26 in IEEE 802.11-REVme D4.1
 *
 * @sta_count: total number of STAs currently associated with the AP.
 * @channel_util: Percentage of time that the access point sensed the channel
 *	was busy. This value is in range [0, 255], the highest value means
 *	100% busy.
 * @avail_admission_capa: remaining amount of medium time used for admission
 *	control.
 */
struct ieee80211_bss_load_elem {
	__le16 sta_count;
	u8 channel_util;
	__le16 avail_admission_capa;
} __packed;

struct ieee80211_mgmt {
	__le16 frame_control;
	__le16 duration;
	u8 da[ETH_ALEN];
	u8 sa[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	__le16 seq_ctrl;
	union {
		struct {
			__le16 auth_alg;
			__le16 auth_transaction;
			__le16 status_code;
			/* possibly followed by Challenge text */
			u8 variable[];
		} __packed auth;
		struct {
			__le16 reason_code;
		} __packed deauth;
		struct {
			__le16 capab_info;
			__le16 listen_interval;
			/* followed by SSID and Supported rates */
			u8 variable[];
		} __packed assoc_req;
		struct {
			__le16 capab_info;
			__le16 status_code;
			__le16 aid;
			/* followed by Supported rates */
			u8 variable[];
		} __packed assoc_resp, reassoc_resp;
		struct {
			__le16 capab_info;
			__le16 status_code;
			u8 variable[];
		} __packed s1g_assoc_resp, s1g_reassoc_resp;
		struct {
			__le16 capab_info;
			__le16 listen_interval;
			u8 current_ap[ETH_ALEN];
			/* followed by SSID and Supported rates */
			u8 variable[];
		} __packed reassoc_req;
		struct {
			__le16 reason_code;
		} __packed disassoc;
		struct {
			__le64 timestamp;
			__le16 beacon_int;
			__le16 capab_info;
			/* followed by some of SSID, Supported rates,
			 * FH Params, DS Params, CF Params, IBSS Params, TIM */
			u8 variable[];
		} __packed beacon;
		struct {
			/* only variable items: SSID, Supported rates */
			DECLARE_FLEX_ARRAY(u8, variable);
		} __packed probe_req;
		struct {
			__le64 timestamp;
			__le16 beacon_int;
			__le16 capab_info;
			/* followed by some of SSID, Supported rates,
			 * FH Params, DS Params, CF Params, IBSS Params */
			u8 variable[];
		} __packed probe_resp;
		struct {
			u8 category;
			union {
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 status_code;
					u8 variable[];
				} __packed wme_action;
				struct{
					u8 action_code;
					u8 variable[];
				} __packed chan_switch;
				struct{
					u8 action_code;
					struct ieee80211_ext_chansw_ie data;
					u8 variable[];
				} __packed ext_chan_switch;
				struct{
					u8 action_code;
					u8 dialog_token;
					u8 element_id;
					u8 length;
					struct ieee80211_msrment_ie msr_elem;
				} __packed measurement;
				struct{
					u8 action_code;
					u8 dialog_token;
					__le16 capab;
					__le16 timeout;
					__le16 start_seq_num;
					/* followed by BA Extension */
					u8 variable[];
				} __packed addba_req;
				struct{
					u8 action_code;
					u8 dialog_token;
					__le16 status;
					__le16 capab;
					__le16 timeout;
					/* followed by BA Extension */
					u8 variable[];
				} __packed addba_resp;
				struct{
					u8 action_code;
					__le16 params;
					__le16 reason_code;
				} __packed delba;
				struct {
					u8 action_code;
					u8 variable[];
				} __packed self_prot;
				struct{
					u8 action_code;
					u8 variable[];
				} __packed mesh_action;
				struct {
					u8 action;
					u8 trans_id[WLAN_SA_QUERY_TR_ID_LEN];
				} __packed sa_query;
				struct {
					u8 action;
					u8 smps_control;
				} __packed ht_smps;
				struct {
					u8 action_code;
					u8 chanwidth;
				} __packed ht_notify_cw;
				struct {
					u8 action_code;
					u8 dialog_token;
					__le16 capability;
					u8 variable[];
				} __packed tdls_discover_resp;
				struct {
					u8 action_code;
					u8 operating_mode;
				} __packed vht_opmode_notif;
				struct {
					u8 action_code;
					u8 membership[WLAN_MEMBERSHIP_LEN];
					u8 position[WLAN_USER_POSITION_LEN];
				} __packed vht_group_notif;
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 tpc_elem_id;
					u8 tpc_elem_length;
					struct ieee80211_tpc_report_ie tpc;
				} __packed tpc_report;
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 follow_up;
					u8 tod[6];
					u8 toa[6];
					__le16 tod_error;
					__le16 toa_error;
					u8 variable[];
				} __packed ftm;
				struct {
					u8 action_code;
					u8 variable[];
				} __packed s1g;
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 follow_up;
					u32 tod;
					u32 toa;
					u8 max_tod_error;
					u8 max_toa_error;
				} __packed wnm_timing_msr;
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 variable[];
				} __packed ttlm_req;
				struct {
					u8 action_code;
					u8 dialog_token;
					__le16 status_code;
					u8 variable[];
				} __packed ttlm_res;
				struct {
					u8 action_code;
				} __packed ttlm_tear_down;
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 variable[];
				} __packed ml_reconf_req;
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 count;
					u8 variable[];
				} __packed ml_reconf_resp;
				struct {
					u8 action_code;
					u8 variable[];
				} __packed epcs;
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 control;
					u8 variable[];
				} __packed eml_omn;
			} u;
		} __packed action;
		DECLARE_FLEX_ARRAY(u8, body); /* Generic frame body */
	} u;
} __packed __aligned(2);

/* Supported rates membership selectors */
#define BSS_MEMBERSHIP_SELECTOR_HT_PHY	127
#define BSS_MEMBERSHIP_SELECTOR_VHT_PHY	126
#define BSS_MEMBERSHIP_SELECTOR_GLK	125
#define BSS_MEMBERSHIP_SELECTOR_EPD	124
#define BSS_MEMBERSHIP_SELECTOR_SAE_H2E 123
#define BSS_MEMBERSHIP_SELECTOR_HE_PHY	122
#define BSS_MEMBERSHIP_SELECTOR_EHT_PHY	121
#define BSS_MEMBERSHIP_SELECTOR_UHR_PHY	120

#define BSS_MEMBERSHIP_SELECTOR_MIN	BSS_MEMBERSHIP_SELECTOR_UHR_PHY

/* mgmt header + 1 byte category code */
#define IEEE80211_MIN_ACTION_SIZE offsetof(struct ieee80211_mgmt, u.action.u)


/* Management MIC information element (IEEE 802.11w) for CMAC */
struct ieee80211_mmie {
	u8 element_id;
	u8 length;
	__le16 key_id;
	u8 sequence_number[6];
	u8 mic[8];
} __packed;

/* Management MIC information element (IEEE 802.11w) for GMAC and CMAC-256 */
struct ieee80211_mmie_16 {
	u8 element_id;
	u8 length;
	__le16 key_id;
	u8 sequence_number[6];
	u8 mic[16];
} __packed;

/* Management MIC information element (IEEE 802.11w) for all variants */
struct ieee80211_mmie_var {
	u8 element_id;
	u8 length;
	__le16 key_id;
	u8 sequence_number[6];
	u8 mic[]; /* 8 or 16 bytes */
} __packed;

struct ieee80211_vendor_ie {
	u8 element_id;
	u8 len;
	u8 oui[3];
	u8 oui_type;
} __packed;

struct ieee80211_wmm_ac_param {
	u8 aci_aifsn; /* AIFSN, ACM, ACI */
	u8 cw; /* ECWmin, ECWmax (CW = 2^ECW - 1) */
	__le16 txop_limit;
} __packed;

struct ieee80211_wmm_param_ie {
	u8 element_id; /* Element ID: 221 (0xdd); */
	u8 len; /* Length: 24 */
	/* required fields for WMM version 1 */
	u8 oui[3]; /* 00:50:f2 */
	u8 oui_type; /* 2 */
	u8 oui_subtype; /* 1 */
	u8 version; /* 1 for WMM version 1.0 */
	u8 qos_info; /* AP/STA specific QoS info */
	u8 reserved; /* 0 */
	/* AC_BE, AC_BK, AC_VI, AC_VO */
	struct ieee80211_wmm_ac_param ac[4];
} __packed;

/* Control frames */
struct ieee80211_rts {
	__le16 frame_control;
	__le16 duration;
	u8 ra[ETH_ALEN];
	u8 ta[ETH_ALEN];
} __packed __aligned(2);

struct ieee80211_cts {
	__le16 frame_control;
	__le16 duration;
	u8 ra[ETH_ALEN];
} __packed __aligned(2);

struct ieee80211_pspoll {
	__le16 frame_control;
	__le16 aid;
	u8 bssid[ETH_ALEN];
	u8 ta[ETH_ALEN];
} __packed __aligned(2);

/* TDLS */

/* Channel switch timing */
struct ieee80211_ch_switch_timing {
	__le16 switch_time;
	__le16 switch_timeout;
} __packed;

/* Link-id information element */
struct ieee80211_tdls_lnkie {
	u8 ie_type; /* Link Identifier IE */
	u8 ie_len;
	u8 bssid[ETH_ALEN];
	u8 init_sta[ETH_ALEN];
	u8 resp_sta[ETH_ALEN];
} __packed;

struct ieee80211_tdls_data {
	u8 da[ETH_ALEN];
	u8 sa[ETH_ALEN];
	__be16 ether_type;
	u8 payload_type;
	u8 category;
	u8 action_code;
	union {
		struct {
			u8 dialog_token;
			__le16 capability;
			u8 variable[];
		} __packed setup_req;
		struct {
			__le16 status_code;
			u8 dialog_token;
			__le16 capability;
			u8 variable[];
		} __packed setup_resp;
		struct {
			__le16 status_code;
			u8 dialog_token;
			u8 variable[];
		} __packed setup_cfm;
		struct {
			__le16 reason_code;
			u8 variable[];
		} __packed teardown;
		struct {
			u8 dialog_token;
			u8 variable[];
		} __packed discover_req;
		struct {
			u8 target_channel;
			u8 oper_class;
			u8 variable[];
		} __packed chan_switch_req;
		struct {
			__le16 status_code;
			u8 variable[];
		} __packed chan_switch_resp;
	} u;
} __packed;

/* Authentication algorithms */
#define WLAN_AUTH_OPEN 0
#define WLAN_AUTH_SHARED_KEY 1
#define WLAN_AUTH_FT 2
#define WLAN_AUTH_SAE 3
#define WLAN_AUTH_FILS_SK 4
#define WLAN_AUTH_FILS_SK_PFS 5
#define WLAN_AUTH_FILS_PK 6
#define WLAN_AUTH_EPPKE 9
#define WLAN_AUTH_LEAP 128

#define WLAN_AUTH_CHALLENGE_LEN 128

#define WLAN_CAPABILITY_ESS		(1<<0)
#define WLAN_CAPABILITY_IBSS		(1<<1)

/*
 * A mesh STA sets the ESS and IBSS capability bits to zero.
 * however, this holds true for p2p probe responses (in the p2p_find
 * phase) as well.
 */
#define WLAN_CAPABILITY_IS_STA_BSS(cap)	\
	(!((cap) & (WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_IBSS)))

#define WLAN_CAPABILITY_CF_POLLABLE	(1<<2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST	(1<<3)
#define WLAN_CAPABILITY_PRIVACY		(1<<4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE	(1<<5)
#define WLAN_CAPABILITY_PBCC		(1<<6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY	(1<<7)

/* 802.11h */
#define WLAN_CAPABILITY_SPECTRUM_MGMT	(1<<8)
#define WLAN_CAPABILITY_QOS		(1<<9)
#define WLAN_CAPABILITY_SHORT_SLOT_TIME	(1<<10)
#define WLAN_CAPABILITY_APSD		(1<<11)
#define WLAN_CAPABILITY_RADIO_MEASURE	(1<<12)
#define WLAN_CAPABILITY_DSSS_OFDM	(1<<13)
#define WLAN_CAPABILITY_DEL_BACK	(1<<14)
#define WLAN_CAPABILITY_IMM_BACK	(1<<15)

/* DMG (60gHz) 802.11ad */
/* type - bits 0..1 */
#define WLAN_CAPABILITY_DMG_TYPE_MASK		(3<<0)
#define WLAN_CAPABILITY_DMG_TYPE_IBSS		(1<<0) /* Tx by: STA */
#define WLAN_CAPABILITY_DMG_TYPE_PBSS		(2<<0) /* Tx by: PCP */
#define WLAN_CAPABILITY_DMG_TYPE_AP		(3<<0) /* Tx by: AP */

#define WLAN_CAPABILITY_DMG_CBAP_ONLY		(1<<2)
#define WLAN_CAPABILITY_DMG_CBAP_SOURCE		(1<<3)
#define WLAN_CAPABILITY_DMG_PRIVACY		(1<<4)
#define WLAN_CAPABILITY_DMG_ECPAC		(1<<5)

#define WLAN_CAPABILITY_DMG_SPECTRUM_MGMT	(1<<8)
#define WLAN_CAPABILITY_DMG_RADIO_MEASURE	(1<<12)

/* measurement */
#define IEEE80211_SPCT_MSR_RPRT_MODE_LATE	(1<<0)
#define IEEE80211_SPCT_MSR_RPRT_MODE_INCAPABLE	(1<<1)
#define IEEE80211_SPCT_MSR_RPRT_MODE_REFUSED	(1<<2)

#define IEEE80211_SPCT_MSR_RPRT_TYPE_BASIC	0
#define IEEE80211_SPCT_MSR_RPRT_TYPE_CCA	1
#define IEEE80211_SPCT_MSR_RPRT_TYPE_RPI	2
#define IEEE80211_SPCT_MSR_RPRT_TYPE_LCI	8
#define IEEE80211_SPCT_MSR_RPRT_TYPE_CIVIC	11

/* 802.11g ERP information element */
#define WLAN_ERP_NON_ERP_PRESENT (1<<0)
#define WLAN_ERP_USE_PROTECTION (1<<1)
#define WLAN_ERP_BARKER_PREAMBLE (1<<2)

/* WLAN_ERP_BARKER_PREAMBLE values */
enum {
	WLAN_ERP_PREAMBLE_SHORT = 0,
	WLAN_ERP_PREAMBLE_LONG = 1,
};

/* Band ID, 802.11ad #8.4.1.45 */
enum {
	IEEE80211_BANDID_TV_WS = 0, /* TV white spaces */
	IEEE80211_BANDID_SUB1  = 1, /* Sub-1 GHz (excluding TV white spaces) */
	IEEE80211_BANDID_2G    = 2, /* 2.4 GHz */
	IEEE80211_BANDID_3G    = 3, /* 3.6 GHz */
	IEEE80211_BANDID_5G    = 4, /* 4.9 and 5 GHz */
	IEEE80211_BANDID_60G   = 5, /* 60 GHz */
};

/* Status codes */
enum ieee80211_statuscode {
	WLAN_STATUS_SUCCESS = 0,
	WLAN_STATUS_UNSPECIFIED_FAILURE = 1,
	WLAN_STATUS_CAPS_UNSUPPORTED = 10,
	WLAN_STATUS_REASSOC_NO_ASSOC = 11,
	WLAN_STATUS_ASSOC_DENIED_UNSPEC = 12,
	WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG = 13,
	WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION = 14,
	WLAN_STATUS_CHALLENGE_FAIL = 15,
	WLAN_STATUS_AUTH_TIMEOUT = 16,
	WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA = 17,
	WLAN_STATUS_ASSOC_DENIED_RATES = 18,
	/* 802.11b */
	WLAN_STATUS_ASSOC_DENIED_NOSHORTPREAMBLE = 19,
	WLAN_STATUS_ASSOC_DENIED_NOPBCC = 20,
	WLAN_STATUS_ASSOC_DENIED_NOAGILITY = 21,
	/* 802.11h */
	WLAN_STATUS_ASSOC_DENIED_NOSPECTRUM = 22,
	WLAN_STATUS_ASSOC_REJECTED_BAD_POWER = 23,
	WLAN_STATUS_ASSOC_REJECTED_BAD_SUPP_CHAN = 24,
	/* 802.11g */
	WLAN_STATUS_ASSOC_DENIED_NOSHORTTIME = 25,
	WLAN_STATUS_ASSOC_DENIED_NODSSSOFDM = 26,
	/* 802.11w */
	WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY = 30,
	WLAN_STATUS_ROBUST_MGMT_FRAME_POLICY_VIOLATION = 31,
	/* 802.11i */
	WLAN_STATUS_INVALID_IE = 40,
	WLAN_STATUS_INVALID_GROUP_CIPHER = 41,
	WLAN_STATUS_INVALID_PAIRWISE_CIPHER = 42,
	WLAN_STATUS_INVALID_AKMP = 43,
	WLAN_STATUS_UNSUPP_RSN_VERSION = 44,
	WLAN_STATUS_INVALID_RSN_IE_CAP = 45,
	WLAN_STATUS_CIPHER_SUITE_REJECTED = 46,
	/* 802.11e */
	WLAN_STATUS_UNSPECIFIED_QOS = 32,
	WLAN_STATUS_ASSOC_DENIED_NOBANDWIDTH = 33,
	WLAN_STATUS_ASSOC_DENIED_LOWACK = 34,
	WLAN_STATUS_ASSOC_DENIED_UNSUPP_QOS = 35,
	WLAN_STATUS_REQUEST_DECLINED = 37,
	WLAN_STATUS_INVALID_QOS_PARAM = 38,
	WLAN_STATUS_CHANGE_TSPEC = 39,
	WLAN_STATUS_WAIT_TS_DELAY = 47,
	WLAN_STATUS_NO_DIRECT_LINK = 48,
	WLAN_STATUS_STA_NOT_PRESENT = 49,
	WLAN_STATUS_STA_NOT_QSTA = 50,
	/* 802.11s */
	WLAN_STATUS_ANTI_CLOG_REQUIRED = 76,
	WLAN_STATUS_FCG_NOT_SUPP = 78,
	WLAN_STATUS_STA_NO_TBTT = 78,
	/* 802.11ad */
	WLAN_STATUS_REJECTED_WITH_SUGGESTED_CHANGES = 39,
	WLAN_STATUS_REJECTED_FOR_DELAY_PERIOD = 47,
	WLAN_STATUS_REJECT_WITH_SCHEDULE = 83,
	WLAN_STATUS_PENDING_ADMITTING_FST_SESSION = 86,
	WLAN_STATUS_PERFORMING_FST_NOW = 87,
	WLAN_STATUS_PENDING_GAP_IN_BA_WINDOW = 88,
	WLAN_STATUS_REJECT_U_PID_SETTING = 89,
	WLAN_STATUS_REJECT_DSE_BAND = 96,
	WLAN_STATUS_DENIED_WITH_SUGGESTED_BAND_AND_CHANNEL = 99,
	WLAN_STATUS_DENIED_DUE_TO_SPECTRUM_MANAGEMENT = 103,
	/* 802.11ai */
	WLAN_STATUS_FILS_AUTHENTICATION_FAILURE = 112,
	WLAN_STATUS_UNKNOWN_AUTHENTICATION_SERVER = 113,
	WLAN_STATUS_SAE_HASH_TO_ELEMENT = 126,
	WLAN_STATUS_SAE_PK = 127,
	WLAN_STATUS_DENIED_TID_TO_LINK_MAPPING = 133,
	WLAN_STATUS_PREF_TID_TO_LINK_MAPPING_SUGGESTED = 134,
};


/* Reason codes */
enum ieee80211_reasoncode {
	WLAN_REASON_UNSPECIFIED = 1,
	WLAN_REASON_PREV_AUTH_NOT_VALID = 2,
	WLAN_REASON_DEAUTH_LEAVING = 3,
	WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY = 4,
	WLAN_REASON_DISASSOC_AP_BUSY = 5,
	WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA = 6,
	WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA = 7,
	WLAN_REASON_DISASSOC_STA_HAS_LEFT = 8,
	WLAN_REASON_STA_REQ_ASSOC_WITHOUT_AUTH = 9,
	/* 802.11h */
	WLAN_REASON_DISASSOC_BAD_POWER = 10,
	WLAN_REASON_DISASSOC_BAD_SUPP_CHAN = 11,
	/* 802.11i */
	WLAN_REASON_INVALID_IE = 13,
	WLAN_REASON_MIC_FAILURE = 14,
	WLAN_REASON_4WAY_HANDSHAKE_TIMEOUT = 15,
	WLAN_REASON_GROUP_KEY_HANDSHAKE_TIMEOUT = 16,
	WLAN_REASON_IE_DIFFERENT = 17,
	WLAN_REASON_INVALID_GROUP_CIPHER = 18,
	WLAN_REASON_INVALID_PAIRWISE_CIPHER = 19,
	WLAN_REASON_INVALID_AKMP = 20,
	WLAN_REASON_UNSUPP_RSN_VERSION = 21,
	WLAN_REASON_INVALID_RSN_IE_CAP = 22,
	WLAN_REASON_IEEE8021X_FAILED = 23,
	WLAN_REASON_CIPHER_SUITE_REJECTED = 24,
	/* TDLS (802.11z) */
	WLAN_REASON_TDLS_TEARDOWN_UNREACHABLE = 25,
	WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED = 26,
	/* 802.11e */
	WLAN_REASON_DISASSOC_UNSPECIFIED_QOS = 32,
	WLAN_REASON_DISASSOC_QAP_NO_BANDWIDTH = 33,
	WLAN_REASON_DISASSOC_LOW_ACK = 34,
	WLAN_REASON_DISASSOC_QAP_EXCEED_TXOP = 35,
	WLAN_REASON_QSTA_LEAVE_QBSS = 36,
	WLAN_REASON_QSTA_NOT_USE = 37,
	WLAN_REASON_QSTA_REQUIRE_SETUP = 38,
	WLAN_REASON_QSTA_TIMEOUT = 39,
	WLAN_REASON_QSTA_CIPHER_NOT_SUPP = 45,
	/* 802.11s */
	WLAN_REASON_MESH_PEER_CANCELED = 52,
	WLAN_REASON_MESH_MAX_PEERS = 53,
	WLAN_REASON_MESH_CONFIG = 54,
	WLAN_REASON_MESH_CLOSE = 55,
	WLAN_REASON_MESH_MAX_RETRIES = 56,
	WLAN_REASON_MESH_CONFIRM_TIMEOUT = 57,
	WLAN_REASON_MESH_INVALID_GTK = 58,
	WLAN_REASON_MESH_INCONSISTENT_PARAM = 59,
	WLAN_REASON_MESH_INVALID_SECURITY = 60,
	WLAN_REASON_MESH_PATH_ERROR = 61,
	WLAN_REASON_MESH_PATH_NOFORWARD = 62,
	WLAN_REASON_MESH_PATH_DEST_UNREACHABLE = 63,
	WLAN_REASON_MAC_EXISTS_IN_MBSS = 64,
	WLAN_REASON_MESH_CHAN_REGULATORY = 65,
	WLAN_REASON_MESH_CHAN = 66,
};


/* Information Element IDs */
enum ieee80211_eid {
	WLAN_EID_SSID = 0,
	WLAN_EID_SUPP_RATES = 1,
	WLAN_EID_FH_PARAMS = 2, /* reserved now */
	WLAN_EID_DS_PARAMS = 3,
	WLAN_EID_CF_PARAMS = 4,
	WLAN_EID_TIM = 5,
	WLAN_EID_IBSS_PARAMS = 6,
	WLAN_EID_COUNTRY = 7,
	/* 8, 9 reserved */
	WLAN_EID_REQUEST = 10,
	WLAN_EID_QBSS_LOAD = 11,
	WLAN_EID_EDCA_PARAM_SET = 12,
	WLAN_EID_TSPEC = 13,
	WLAN_EID_TCLAS = 14,
	WLAN_EID_SCHEDULE = 15,
	WLAN_EID_CHALLENGE = 16,
	/* 17-31 reserved for challenge text extension */
	WLAN_EID_PWR_CONSTRAINT = 32,
	WLAN_EID_PWR_CAPABILITY = 33,
	WLAN_EID_TPC_REQUEST = 34,
	WLAN_EID_TPC_REPORT = 35,
	WLAN_EID_SUPPORTED_CHANNELS = 36,
	WLAN_EID_CHANNEL_SWITCH = 37,
	WLAN_EID_MEASURE_REQUEST = 38,
	WLAN_EID_MEASURE_REPORT = 39,
	WLAN_EID_QUIET = 40,
	WLAN_EID_IBSS_DFS = 41,
	WLAN_EID_ERP_INFO = 42,
	WLAN_EID_TS_DELAY = 43,
	WLAN_EID_TCLAS_PROCESSING = 44,
	WLAN_EID_HT_CAPABILITY = 45,
	WLAN_EID_QOS_CAPA = 46,
	/* 47 reserved for Broadcom */
	WLAN_EID_RSN = 48,
	WLAN_EID_802_15_COEX = 49,
	WLAN_EID_EXT_SUPP_RATES = 50,
	WLAN_EID_AP_CHAN_REPORT = 51,
	WLAN_EID_NEIGHBOR_REPORT = 52,
	WLAN_EID_RCPI = 53,
	WLAN_EID_MOBILITY_DOMAIN = 54,
	WLAN_EID_FAST_BSS_TRANSITION = 55,
	WLAN_EID_TIMEOUT_INTERVAL = 56,
	WLAN_EID_RIC_DATA = 57,
	WLAN_EID_DSE_REGISTERED_LOCATION = 58,
	WLAN_EID_SUPPORTED_REGULATORY_CLASSES = 59,
	WLAN_EID_EXT_CHANSWITCH_ANN = 60,
	WLAN_EID_HT_OPERATION = 61,
	WLAN_EID_SECONDARY_CHANNEL_OFFSET = 62,
	WLAN_EID_BSS_AVG_ACCESS_DELAY = 63,
	WLAN_EID_ANTENNA_INFO = 64,
	WLAN_EID_RSNI = 65,
	WLAN_EID_MEASUREMENT_PILOT_TX_INFO = 66,
	WLAN_EID_BSS_AVAILABLE_CAPACITY = 67,
	WLAN_EID_BSS_AC_ACCESS_DELAY = 68,
	WLAN_EID_TIME_ADVERTISEMENT = 69,
	WLAN_EID_RRM_ENABLED_CAPABILITIES = 70,
	WLAN_EID_MULTIPLE_BSSID = 71,
	WLAN_EID_BSS_COEX_2040 = 72,
	WLAN_EID_BSS_INTOLERANT_CHL_REPORT = 73,
	WLAN_EID_OVERLAP_BSS_SCAN_PARAM = 74,
	WLAN_EID_RIC_DESCRIPTOR = 75,
	WLAN_EID_MMIE = 76,
	WLAN_EID_ASSOC_COMEBACK_TIME = 77,
	WLAN_EID_EVENT_REQUEST = 78,
	WLAN_EID_EVENT_REPORT = 79,
	WLAN_EID_DIAGNOSTIC_REQUEST = 80,
	WLAN_EID_DIAGNOSTIC_REPORT = 81,
	WLAN_EID_LOCATION_PARAMS = 82,
	WLAN_EID_NON_TX_BSSID_CAP =  83,
	WLAN_EID_SSID_LIST = 84,
	WLAN_EID_MULTI_BSSID_IDX = 85,
	WLAN_EID_FMS_DESCRIPTOR = 86,
	WLAN_EID_FMS_REQUEST = 87,
	WLAN_EID_FMS_RESPONSE = 88,
	WLAN_EID_QOS_TRAFFIC_CAPA = 89,
	WLAN_EID_BSS_MAX_IDLE_PERIOD = 90,
	WLAN_EID_TSF_REQUEST = 91,
	WLAN_EID_TSF_RESPOSNE = 92,
	WLAN_EID_WNM_SLEEP_MODE = 93,
	WLAN_EID_TIM_BCAST_REQ = 94,
	WLAN_EID_TIM_BCAST_RESP = 95,
	WLAN_EID_COLL_IF_REPORT = 96,
	WLAN_EID_CHANNEL_USAGE = 97,
	WLAN_EID_TIME_ZONE = 98,
	WLAN_EID_DMS_REQUEST = 99,
	WLAN_EID_DMS_RESPONSE = 100,
	WLAN_EID_LINK_ID = 101,
	WLAN_EID_WAKEUP_SCHEDUL = 102,
	/* 103 reserved */
	WLAN_EID_CHAN_SWITCH_TIMING = 104,
	WLAN_EID_PTI_CONTROL = 105,
	WLAN_EID_PU_BUFFER_STATUS = 106,
	WLAN_EID_INTERWORKING = 107,
	WLAN_EID_ADVERTISEMENT_PROTOCOL = 108,
	WLAN_EID_EXPEDITED_BW_REQ = 109,
	WLAN_EID_QOS_MAP_SET = 110,
	WLAN_EID_ROAMING_CONSORTIUM = 111,
	WLAN_EID_EMERGENCY_ALERT = 112,
	WLAN_EID_MESH_CONFIG = 113,
	WLAN_EID_MESH_ID = 114,
	WLAN_EID_LINK_METRIC_REPORT = 115,
	WLAN_EID_CONGESTION_NOTIFICATION = 116,
	WLAN_EID_PEER_MGMT = 117,
	WLAN_EID_CHAN_SWITCH_PARAM = 118,
	WLAN_EID_MESH_AWAKE_WINDOW = 119,
	WLAN_EID_BEACON_TIMING = 120,
	WLAN_EID_MCCAOP_SETUP_REQ = 121,
	WLAN_EID_MCCAOP_SETUP_RESP = 122,
	WLAN_EID_MCCAOP_ADVERT = 123,
	WLAN_EID_MCCAOP_TEARDOWN = 124,
	WLAN_EID_GANN = 125,
	WLAN_EID_RANN = 126,
	WLAN_EID_EXT_CAPABILITY = 127,
	/* 128, 129 reserved for Agere */
	WLAN_EID_PREQ = 130,
	WLAN_EID_PREP = 131,
	WLAN_EID_PERR = 132,
	/* 133-136 reserved for Cisco */
	WLAN_EID_PXU = 137,
	WLAN_EID_PXUC = 138,
	WLAN_EID_AUTH_MESH_PEER_EXCH = 139,
	WLAN_EID_MIC = 140,
	WLAN_EID_DESTINATION_URI = 141,
	WLAN_EID_UAPSD_COEX = 142,
	WLAN_EID_WAKEUP_SCHEDULE = 143,
	WLAN_EID_EXT_SCHEDULE = 144,
	WLAN_EID_STA_AVAILABILITY = 145,
	WLAN_EID_DMG_TSPEC = 146,
	WLAN_EID_DMG_AT = 147,
	WLAN_EID_DMG_CAP = 148,
	/* 149 reserved for Cisco */
	WLAN_EID_CISCO_VENDOR_SPECIFIC = 150,
	WLAN_EID_DMG_OPERATION = 151,
	WLAN_EID_DMG_BSS_PARAM_CHANGE = 152,
	WLAN_EID_DMG_BEAM_REFINEMENT = 153,
	WLAN_EID_CHANNEL_MEASURE_FEEDBACK = 154,
	/* 155-156 reserved for Cisco */
	WLAN_EID_AWAKE_WINDOW = 157,
	WLAN_EID_MULTI_BAND = 158,
	WLAN_EID_ADDBA_EXT = 159,
	WLAN_EID_NEXT_PCP_LIST = 160,
	WLAN_EID_PCP_HANDOVER = 161,
	WLAN_EID_DMG_LINK_MARGIN = 162,
	WLAN_EID_SWITCHING_STREAM = 163,
	WLAN_EID_SESSION_TRANSITION = 164,
	WLAN_EID_DYN_TONE_PAIRING_REPORT = 165,
	WLAN_EID_CLUSTER_REPORT = 166,
	WLAN_EID_RELAY_CAP = 167,
	WLAN_EID_RELAY_XFER_PARAM_SET = 168,
	WLAN_EID_BEAM_LINK_MAINT = 169,
	WLAN_EID_MULTIPLE_MAC_ADDR = 170,
	WLAN_EID_U_PID = 171,
	WLAN_EID_DMG_LINK_ADAPT_ACK = 172,
	/* 173 reserved for Symbol */
	WLAN_EID_MCCAOP_ADV_OVERVIEW = 174,
	WLAN_EID_QUIET_PERIOD_REQ = 175,
	/* 176 reserved for Symbol */
	WLAN_EID_QUIET_PERIOD_RESP = 177,
	/* 178-179 reserved for Symbol */
	/* 180 reserved for ISO/IEC 20011 */
	WLAN_EID_EPAC_POLICY = 182,
	WLAN_EID_CLISTER_TIME_OFF = 183,
	WLAN_EID_INTER_AC_PRIO = 184,
	WLAN_EID_SCS_DESCRIPTOR = 185,
	WLAN_EID_QLOAD_REPORT = 186,
	WLAN_EID_HCCA_TXOP_UPDATE_COUNT = 187,
	WLAN_EID_HL_STREAM_ID = 188,
	WLAN_EID_GCR_GROUP_ADDR = 189,
	WLAN_EID_ANTENNA_SECTOR_ID_PATTERN = 190,
	WLAN_EID_VHT_CAPABILITY = 191,
	WLAN_EID_VHT_OPERATION = 192,
	WLAN_EID_EXTENDED_BSS_LOAD = 193,
	WLAN_EID_WIDE_BW_CHANNEL_SWITCH = 194,
	WLAN_EID_TX_POWER_ENVELOPE = 195,
	WLAN_EID_CHANNEL_SWITCH_WRAPPER = 196,
	WLAN_EID_AID = 197,
	WLAN_EID_QUIET_CHANNEL = 198,
	WLAN_EID_OPMODE_NOTIF = 199,

	WLAN_EID_REDUCED_NEIGHBOR_REPORT = 201,

	WLAN_EID_AID_REQUEST = 210,
	WLAN_EID_AID_RESPONSE = 211,
	WLAN_EID_S1G_BCN_COMPAT = 213,
	WLAN_EID_S1G_SHORT_BCN_INTERVAL = 214,
	WLAN_EID_S1G_TWT = 216,
	WLAN_EID_S1G_CAPABILITIES = 217,
	WLAN_EID_VENDOR_SPECIFIC = 221,
	WLAN_EID_QOS_PARAMETER = 222,
	WLAN_EID_S1G_OPERATION = 232,
	WLAN_EID_CAG_NUMBER = 237,
	WLAN_EID_AP_CSN = 239,
	WLAN_EID_FILS_INDICATION = 240,
	WLAN_EID_DILS = 241,
	WLAN_EID_FRAGMENT = 242,
	WLAN_EID_RSNX = 244,
	WLAN_EID_EXTENSION = 255
};

/* Element ID Extensions for Element ID 255 */
enum ieee80211_eid_ext {
	WLAN_EID_EXT_ASSOC_DELAY_INFO = 1,
	WLAN_EID_EXT_FILS_REQ_PARAMS = 2,
	WLAN_EID_EXT_FILS_KEY_CONFIRM = 3,
	WLAN_EID_EXT_FILS_SESSION = 4,
	WLAN_EID_EXT_FILS_HLP_CONTAINER = 5,
	WLAN_EID_EXT_FILS_IP_ADDR_ASSIGN = 6,
	WLAN_EID_EXT_KEY_DELIVERY = 7,
	WLAN_EID_EXT_FILS_WRAPPED_DATA = 8,
	WLAN_EID_EXT_FILS_PUBLIC_KEY = 12,
	WLAN_EID_EXT_FILS_NONCE = 13,
	WLAN_EID_EXT_FUTURE_CHAN_GUIDANCE = 14,
	WLAN_EID_EXT_DH_PARAMETER = 32,
	WLAN_EID_EXT_HE_CAPABILITY = 35,
	WLAN_EID_EXT_HE_OPERATION = 36,
	WLAN_EID_EXT_UORA = 37,
	WLAN_EID_EXT_HE_MU_EDCA = 38,
	WLAN_EID_EXT_HE_SPR = 39,
	WLAN_EID_EXT_NDP_FEEDBACK_REPORT_PARAMSET = 41,
	WLAN_EID_EXT_BSS_COLOR_CHG_ANN = 42,
	WLAN_EID_EXT_QUIET_TIME_PERIOD_SETUP = 43,
	WLAN_EID_EXT_ESS_REPORT = 45,
	WLAN_EID_EXT_OPS = 46,
	WLAN_EID_EXT_HE_BSS_LOAD = 47,
	WLAN_EID_EXT_MAX_CHANNEL_SWITCH_TIME = 52,
	WLAN_EID_EXT_MULTIPLE_BSSID_CONFIGURATION = 55,
	WLAN_EID_EXT_NON_INHERITANCE = 56,
	WLAN_EID_EXT_KNOWN_BSSID = 57,
	WLAN_EID_EXT_SHORT_SSID_LIST = 58,
	WLAN_EID_EXT_HE_6GHZ_CAPA = 59,
	WLAN_EID_EXT_UL_MU_POWER_CAPA = 60,
	WLAN_EID_EXT_EHT_OPERATION = 106,
	WLAN_EID_EXT_EHT_MULTI_LINK = 107,
	WLAN_EID_EXT_EHT_CAPABILITY = 108,
	WLAN_EID_EXT_TID_TO_LINK_MAPPING = 109,
	WLAN_EID_EXT_BANDWIDTH_INDICATION = 135,
	WLAN_EID_EXT_KNOWN_STA_IDENTIFCATION = 136,
	WLAN_EID_EXT_NON_AP_STA_REG_CON = 137,
	WLAN_EID_EXT_UHR_OPER = 151,
	WLAN_EID_EXT_UHR_CAPA = 152,
	WLAN_EID_EXT_MACP = 153,
	WLAN_EID_EXT_SMD = 154,
	WLAN_EID_EXT_BSS_SMD_TRANS_PARAMS = 155,
	WLAN_EID_EXT_CHAN_USAGE = 156,
	WLAN_EID_EXT_UHR_MODE_CHG = 157,
	WLAN_EID_EXT_UHR_PARAM_UPD = 158,
	WLAN_EID_EXT_TXPI = 159,
};

/* Action category code */
enum ieee80211_category {
	WLAN_CATEGORY_SPECTRUM_MGMT = 0,
	WLAN_CATEGORY_QOS = 1,
	WLAN_CATEGORY_DLS = 2,
	WLAN_CATEGORY_BACK = 3,
	WLAN_CATEGORY_PUBLIC = 4,
	WLAN_CATEGORY_RADIO_MEASUREMENT = 5,
	WLAN_CATEGORY_FAST_BBS_TRANSITION = 6,
	WLAN_CATEGORY_HT = 7,
	WLAN_CATEGORY_SA_QUERY = 8,
	WLAN_CATEGORY_PROTECTED_DUAL_OF_ACTION = 9,
	WLAN_CATEGORY_WNM = 10,
	WLAN_CATEGORY_WNM_UNPROTECTED = 11,
	WLAN_CATEGORY_TDLS = 12,
	WLAN_CATEGORY_MESH_ACTION = 13,
	WLAN_CATEGORY_MULTIHOP_ACTION = 14,
	WLAN_CATEGORY_SELF_PROTECTED = 15,
	WLAN_CATEGORY_DMG = 16,
	WLAN_CATEGORY_WMM = 17,
	WLAN_CATEGORY_FST = 18,
	WLAN_CATEGORY_UNPROT_DMG = 20,
	WLAN_CATEGORY_VHT = 21,
	WLAN_CATEGORY_S1G = 22,
	WLAN_CATEGORY_PROTECTED_EHT = 37,
	WLAN_CATEGORY_VENDOR_SPECIFIC_PROTECTED = 126,
	WLAN_CATEGORY_VENDOR_SPECIFIC = 127,
};

/* SPECTRUM_MGMT action code */
enum ieee80211_spectrum_mgmt_actioncode {
	WLAN_ACTION_SPCT_MSR_REQ = 0,
	WLAN_ACTION_SPCT_MSR_RPRT = 1,
	WLAN_ACTION_SPCT_TPC_REQ = 2,
	WLAN_ACTION_SPCT_TPC_RPRT = 3,
	WLAN_ACTION_SPCT_CHL_SWITCH = 4,
};

/* Self Protected Action codes */
enum ieee80211_self_protected_actioncode {
	WLAN_SP_RESERVED = 0,
	WLAN_SP_MESH_PEERING_OPEN = 1,
	WLAN_SP_MESH_PEERING_CONFIRM = 2,
	WLAN_SP_MESH_PEERING_CLOSE = 3,
	WLAN_SP_MGK_INFORM = 4,
	WLAN_SP_MGK_ACK = 5,
};

/* Unprotected WNM action codes */
enum ieee80211_unprotected_wnm_actioncode {
	WLAN_UNPROTECTED_WNM_ACTION_TIM = 0,
	WLAN_UNPROTECTED_WNM_ACTION_TIMING_MEASUREMENT_RESPONSE = 1,
};

/* Security key length */
enum ieee80211_key_len {
	WLAN_KEY_LEN_WEP40 = 5,
	WLAN_KEY_LEN_WEP104 = 13,
	WLAN_KEY_LEN_CCMP = 16,
	WLAN_KEY_LEN_CCMP_256 = 32,
	WLAN_KEY_LEN_TKIP = 32,
	WLAN_KEY_LEN_AES_CMAC = 16,
	WLAN_KEY_LEN_SMS4 = 32,
	WLAN_KEY_LEN_GCMP = 16,
	WLAN_KEY_LEN_GCMP_256 = 32,
	WLAN_KEY_LEN_BIP_CMAC_256 = 32,
	WLAN_KEY_LEN_BIP_GMAC_128 = 16,
	WLAN_KEY_LEN_BIP_GMAC_256 = 32,
};

/* Radio measurement action codes as defined in IEEE 802.11-2024 - Table 9-470 */
enum ieee80211_radio_measurement_actioncode {
	WLAN_RM_ACTION_RADIO_MEASUREMENT_REQUEST = 0,
	WLAN_RM_ACTION_RADIO_MEASUREMENT_REPORT  = 1,
	WLAN_RM_ACTION_LINK_MEASUREMENT_REQUEST  = 2,
	WLAN_RM_ACTION_LINK_MEASUREMENT_REPORT   = 3,
	WLAN_RM_ACTION_NEIGHBOR_REPORT_REQUEST   = 4,
	WLAN_RM_ACTION_NEIGHBOR_REPORT_RESPONSE  = 5,
};

#define IEEE80211_WEP_IV_LEN		4
#define IEEE80211_WEP_ICV_LEN		4
#define IEEE80211_CCMP_HDR_LEN		8
#define IEEE80211_CCMP_MIC_LEN		8
#define IEEE80211_CCMP_PN_LEN		6
#define IEEE80211_CCMP_256_HDR_LEN	8
#define IEEE80211_CCMP_256_MIC_LEN	16
#define IEEE80211_CCMP_256_PN_LEN	6
#define IEEE80211_TKIP_IV_LEN		8
#define IEEE80211_TKIP_ICV_LEN		4
#define IEEE80211_CMAC_PN_LEN		6
#define IEEE80211_GMAC_PN_LEN		6
#define IEEE80211_GCMP_HDR_LEN		8
#define IEEE80211_GCMP_MIC_LEN		16
#define IEEE80211_GCMP_PN_LEN		6
#define IEEE80211_CMAC_128_MIC_LEN	8
#define IEEE80211_CMAC_256_MIC_LEN	16
#define IEEE80211_GMAC_MIC_LEN		16

#define FILS_NONCE_LEN			16
#define FILS_MAX_KEK_LEN		64

#define FILS_ERP_MAX_USERNAME_LEN	16
#define FILS_ERP_MAX_REALM_LEN		253
#define FILS_ERP_MAX_RRK_LEN		64

#define PMK_MAX_LEN			64
#define SAE_PASSWORD_MAX_LEN		128

/* Public action codes (IEEE Std 802.11-2016, 9.6.8.1, Table 9-307) */
enum ieee80211_pub_actioncode {
	WLAN_PUB_ACTION_20_40_BSS_COEX = 0,
	WLAN_PUB_ACTION_DSE_ENABLEMENT = 1,
	WLAN_PUB_ACTION_DSE_DEENABLEMENT = 2,
	WLAN_PUB_ACTION_DSE_REG_LOC_ANN = 3,
	WLAN_PUB_ACTION_EXT_CHANSW_ANN = 4,
	WLAN_PUB_ACTION_DSE_MSMT_REQ = 5,
	WLAN_PUB_ACTION_DSE_MSMT_RESP = 6,
	WLAN_PUB_ACTION_MSMT_PILOT = 7,
	WLAN_PUB_ACTION_DSE_PC = 8,
	WLAN_PUB_ACTION_VENDOR_SPECIFIC = 9,
	WLAN_PUB_ACTION_GAS_INITIAL_REQ = 10,
	WLAN_PUB_ACTION_GAS_INITIAL_RESP = 11,
	WLAN_PUB_ACTION_GAS_COMEBACK_REQ = 12,
	WLAN_PUB_ACTION_GAS_COMEBACK_RESP = 13,
	WLAN_PUB_ACTION_TDLS_DISCOVER_RES = 14,
	WLAN_PUB_ACTION_LOC_TRACK_NOTI = 15,
	WLAN_PUB_ACTION_QAB_REQUEST_FRAME = 16,
	WLAN_PUB_ACTION_QAB_RESPONSE_FRAME = 17,
	WLAN_PUB_ACTION_QMF_POLICY = 18,
	WLAN_PUB_ACTION_QMF_POLICY_CHANGE = 19,
	WLAN_PUB_ACTION_QLOAD_REQUEST = 20,
	WLAN_PUB_ACTION_QLOAD_REPORT = 21,
	WLAN_PUB_ACTION_HCCA_TXOP_ADVERT = 22,
	WLAN_PUB_ACTION_HCCA_TXOP_RESPONSE = 23,
	WLAN_PUB_ACTION_PUBLIC_KEY = 24,
	WLAN_PUB_ACTION_CHANNEL_AVAIL_QUERY = 25,
	WLAN_PUB_ACTION_CHANNEL_SCHEDULE_MGMT = 26,
	WLAN_PUB_ACTION_CONTACT_VERI_SIGNAL = 27,
	WLAN_PUB_ACTION_GDD_ENABLEMENT_REQ = 28,
	WLAN_PUB_ACTION_GDD_ENABLEMENT_RESP = 29,
	WLAN_PUB_ACTION_NETWORK_CHANNEL_CONTROL = 30,
	WLAN_PUB_ACTION_WHITE_SPACE_MAP_ANN = 31,
	WLAN_PUB_ACTION_FTM_REQUEST = 32,
	WLAN_PUB_ACTION_FTM_RESPONSE = 33,
	WLAN_PUB_ACTION_FILS_DISCOVERY = 34,
};

/* TDLS action codes */
enum ieee80211_tdls_actioncode {
	WLAN_TDLS_SETUP_REQUEST = 0,
	WLAN_TDLS_SETUP_RESPONSE = 1,
	WLAN_TDLS_SETUP_CONFIRM = 2,
	WLAN_TDLS_TEARDOWN = 3,
	WLAN_TDLS_PEER_TRAFFIC_INDICATION = 4,
	WLAN_TDLS_CHANNEL_SWITCH_REQUEST = 5,
	WLAN_TDLS_CHANNEL_SWITCH_RESPONSE = 6,
	WLAN_TDLS_PEER_PSM_REQUEST = 7,
	WLAN_TDLS_PEER_PSM_RESPONSE = 8,
	WLAN_TDLS_PEER_TRAFFIC_RESPONSE = 9,
	WLAN_TDLS_DISCOVERY_REQUEST = 10,
};

/* Extended Channel Switching capability to be set in the 1st byte of
 * the @WLAN_EID_EXT_CAPABILITY information element
 */
#define WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING	BIT(2)

/* Multiple BSSID capability is set in the 6th bit of 3rd byte of the
 * @WLAN_EID_EXT_CAPABILITY information element
 */
#define WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT	BIT(6)

/* Timing Measurement protocol for time sync is set in the 7th bit of 3rd byte
 * of the @WLAN_EID_EXT_CAPABILITY information element
 */
#define WLAN_EXT_CAPA3_TIMING_MEASUREMENT_SUPPORT	BIT(7)

/* TDLS capabilities in the 4th byte of @WLAN_EID_EXT_CAPABILITY */
#define WLAN_EXT_CAPA4_TDLS_BUFFER_STA		BIT(4)
#define WLAN_EXT_CAPA4_TDLS_PEER_PSM		BIT(5)
#define WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH		BIT(6)

/* Interworking capabilities are set in 7th bit of 4th byte of the
 * @WLAN_EID_EXT_CAPABILITY information element
 */
#define WLAN_EXT_CAPA4_INTERWORKING_ENABLED	BIT(7)

/*
 * TDLS capabililites to be enabled in the 5th byte of the
 * @WLAN_EID_EXT_CAPABILITY information element
 */
#define WLAN_EXT_CAPA5_TDLS_ENABLED	BIT(5)
#define WLAN_EXT_CAPA5_TDLS_PROHIBITED	BIT(6)
#define WLAN_EXT_CAPA5_TDLS_CH_SW_PROHIBITED	BIT(7)

#define WLAN_EXT_CAPA8_TDLS_WIDE_BW_ENABLED	BIT(5)
#define WLAN_EXT_CAPA8_OPMODE_NOTIF	BIT(6)

/* Defines the maximal number of MSDUs in an A-MSDU. */
#define WLAN_EXT_CAPA8_MAX_MSDU_IN_AMSDU_LSB	BIT(7)
#define WLAN_EXT_CAPA9_MAX_MSDU_IN_AMSDU_MSB	BIT(0)

/*
 * Fine Timing Measurement Initiator - bit 71 of @WLAN_EID_EXT_CAPABILITY
 * information element
 */
#define WLAN_EXT_CAPA9_FTM_INITIATOR	BIT(7)

/* Defines support for TWT Requester and TWT Responder */
#define WLAN_EXT_CAPA10_TWT_REQUESTER_SUPPORT	BIT(5)
#define WLAN_EXT_CAPA10_TWT_RESPONDER_SUPPORT	BIT(6)

/*
 * When set, indicates that the AP is able to tolerate 26-tone RU UL
 * OFDMA transmissions using HE TB PPDU from OBSS (not falsely classify the
 * 26-tone RU UL OFDMA transmissions as radar pulses).
 */
#define WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT BIT(7)

/* Defines support for enhanced multi-bssid advertisement*/
#define WLAN_EXT_CAPA11_EMA_SUPPORT	BIT(3)

/* Enable Beacon Protection */
#define WLAN_EXT_CAPA11_BCN_PROTECT	BIT(4)

/* TDLS specific payload type in the LLC/SNAP header */
#define WLAN_TDLS_SNAP_RFTYPE	0x2

/* BSS Coex IE information field bits */
#define WLAN_BSS_COEX_INFORMATION_REQUEST	BIT(0)

/*
 * IEEE 802.11-2007 7.3.2.9 Country information element
 *
 * Minimum length is 8 octets, ie len must be evenly
 * divisible by 2
 */

/* Although the spec says 8 I'm seeing 6 in practice */
#define IEEE80211_COUNTRY_IE_MIN_LEN	6

/* The Country String field of the element shall be 3 octets in length */
#define IEEE80211_COUNTRY_STRING_LEN	3

/*
 * For regulatory extension stuff see IEEE 802.11-2007
 * Annex I (page 1141) and Annex J (page 1147). Also
 * review 7.3.2.9.
 *
 * When dot11RegulatoryClassesRequired is true and the
 * first_channel/reg_extension_id is >= 201 then the IE
 * compromises of the 'ext' struct represented below:
 *
 *  - Regulatory extension ID - when generating IE this just needs
 *    to be monotonically increasing for each triplet passed in
 *    the IE
 *  - Regulatory class - index into set of rules
 *  - Coverage class - index into air propagation time (Table 7-27),
 *    in microseconds, you can compute the air propagation time from
 *    the index by multiplying by 3, so index 10 yields a propagation
 *    of 10 us. Valid values are 0-31, values 32-255 are not defined
 *    yet. A value of 0 inicates air propagation of <= 1 us.
 *
 *  See also Table I.2 for Emission limit sets and table
 *  I.3 for Behavior limit sets. Table J.1 indicates how to map
 *  a reg_class to an emission limit set and behavior limit set.
 */
#define IEEE80211_COUNTRY_EXTENSION_ID 201

/*
 *  Channels numbers in the IE must be monotonically increasing
 *  if dot11RegulatoryClassesRequired is not true.
 *
 *  If dot11RegulatoryClassesRequired is true consecutive
 *  subband triplets following a regulatory triplet shall
 *  have monotonically increasing first_channel number fields.
 *
 *  Channel numbers shall not overlap.
 *
 *  Note that max_power is signed.
 */
struct ieee80211_country_ie_triplet {
	union {
		struct {
			u8 first_channel;
			u8 num_channels;
			s8 max_power;
		} __packed chans;
		struct {
			u8 reg_extension_id;
			u8 reg_class;
			u8 coverage_class;
		} __packed ext;
	};
} __packed;

enum ieee80211_timeout_interval_type {
	WLAN_TIMEOUT_REASSOC_DEADLINE = 1 /* 802.11r */,
	WLAN_TIMEOUT_KEY_LIFETIME = 2 /* 802.11r */,
	WLAN_TIMEOUT_ASSOC_COMEBACK = 3 /* 802.11w */,
};

/**
 * struct ieee80211_timeout_interval_ie - Timeout Interval element
 * @type: type, see &enum ieee80211_timeout_interval_type
 * @value: timeout interval value
 */
struct ieee80211_timeout_interval_ie {
	u8 type;
	__le32 value;
} __packed;

/**
 * enum ieee80211_idle_options - BSS idle options
 * @WLAN_IDLE_OPTIONS_PROTECTED_KEEP_ALIVE: the station should send an RSN
 *	protected frame to the AP to reset the idle timer at the AP for
 *	the station.
 */
enum ieee80211_idle_options {
	WLAN_IDLE_OPTIONS_PROTECTED_KEEP_ALIVE = BIT(0),
};

/**
 * struct ieee80211_bss_max_idle_period_ie - BSS max idle period element struct
 *
 * This structure refers to "BSS Max idle period element"
 *
 * @max_idle_period: indicates the time period during which a station can
 *	refrain from transmitting frames to its associated AP without being
 *	disassociated. In units of 1000 TUs.
 * @idle_options: indicates the options associated with the BSS idle capability
 *	as specified in &enum ieee80211_idle_options.
 */
struct ieee80211_bss_max_idle_period_ie {
	__le16 max_idle_period;
	u8 idle_options;
} __packed;

/* SA Query action */
enum ieee80211_sa_query_action {
	WLAN_ACTION_SA_QUERY_REQUEST = 0,
	WLAN_ACTION_SA_QUERY_RESPONSE = 1,
};

/**
 * struct ieee80211_bssid_index - multiple BSSID index element structure
 *
 * This structure refers to "Multiple BSSID-index element"
 *
 * @bssid_index: BSSID index
 * @dtim_period: optional, overrides transmitted BSS dtim period
 * @dtim_count: optional, overrides transmitted BSS dtim count
 */
struct ieee80211_bssid_index {
	u8 bssid_index;
	u8 dtim_period;
	u8 dtim_count;
};

/**
 * struct ieee80211_multiple_bssid_configuration - multiple BSSID configuration
 *	element structure
 *
 * This structure refers to "Multiple BSSID Configuration element"
 *
 * @bssid_count: total number of active BSSIDs in the set
 * @profile_periodicity: the least number of beacon frames need to be received
 *	in order to discover all the nontransmitted BSSIDs in the set.
 */
struct ieee80211_multiple_bssid_configuration {
	u8 bssid_count;
	u8 profile_periodicity;
};

#define SUITE(oui, id)	(((oui) << 8) | (id))

/* cipher suite selectors */
#define WLAN_CIPHER_SUITE_USE_GROUP	SUITE(0x000FAC, 0)
#define WLAN_CIPHER_SUITE_WEP40		SUITE(0x000FAC, 1)
#define WLAN_CIPHER_SUITE_TKIP		SUITE(0x000FAC, 2)
/* reserved: 				SUITE(0x000FAC, 3) */
#define WLAN_CIPHER_SUITE_CCMP		SUITE(0x000FAC, 4)
#define WLAN_CIPHER_SUITE_WEP104	SUITE(0x000FAC, 5)
#define WLAN_CIPHER_SUITE_AES_CMAC	SUITE(0x000FAC, 6)
#define WLAN_CIPHER_SUITE_GCMP		SUITE(0x000FAC, 8)
#define WLAN_CIPHER_SUITE_GCMP_256	SUITE(0x000FAC, 9)
#define WLAN_CIPHER_SUITE_CCMP_256	SUITE(0x000FAC, 10)
#define WLAN_CIPHER_SUITE_BIP_GMAC_128	SUITE(0x000FAC, 11)
#define WLAN_CIPHER_SUITE_BIP_GMAC_256	SUITE(0x000FAC, 12)
#define WLAN_CIPHER_SUITE_BIP_CMAC_256	SUITE(0x000FAC, 13)

#define WLAN_CIPHER_SUITE_SMS4		SUITE(0x001472, 1)

/* AKM suite selectors */
#define WLAN_AKM_SUITE_8021X			SUITE(0x000FAC, 1)
#define WLAN_AKM_SUITE_PSK			SUITE(0x000FAC, 2)
#define WLAN_AKM_SUITE_FT_8021X			SUITE(0x000FAC, 3)
#define WLAN_AKM_SUITE_FT_PSK			SUITE(0x000FAC, 4)
#define WLAN_AKM_SUITE_8021X_SHA256		SUITE(0x000FAC, 5)
#define WLAN_AKM_SUITE_PSK_SHA256		SUITE(0x000FAC, 6)
#define WLAN_AKM_SUITE_TDLS			SUITE(0x000FAC, 7)
#define WLAN_AKM_SUITE_SAE			SUITE(0x000FAC, 8)
#define WLAN_AKM_SUITE_FT_OVER_SAE		SUITE(0x000FAC, 9)
#define WLAN_AKM_SUITE_AP_PEER_KEY		SUITE(0x000FAC, 10)
#define WLAN_AKM_SUITE_8021X_SUITE_B		SUITE(0x000FAC, 11)
#define WLAN_AKM_SUITE_8021X_SUITE_B_192	SUITE(0x000FAC, 12)
#define WLAN_AKM_SUITE_FT_8021X_SHA384		SUITE(0x000FAC, 13)
#define WLAN_AKM_SUITE_FILS_SHA256		SUITE(0x000FAC, 14)
#define WLAN_AKM_SUITE_FILS_SHA384		SUITE(0x000FAC, 15)
#define WLAN_AKM_SUITE_FT_FILS_SHA256		SUITE(0x000FAC, 16)
#define WLAN_AKM_SUITE_FT_FILS_SHA384		SUITE(0x000FAC, 17)
#define WLAN_AKM_SUITE_OWE			SUITE(0x000FAC, 18)
#define WLAN_AKM_SUITE_FT_PSK_SHA384		SUITE(0x000FAC, 19)
#define WLAN_AKM_SUITE_PSK_SHA384		SUITE(0x000FAC, 20)

#define WLAN_AKM_SUITE_WFA_DPP			SUITE(WLAN_OUI_WFA, 2)

#define WLAN_MAX_KEY_LEN		32

#define WLAN_PMK_NAME_LEN		16
#define WLAN_PMKID_LEN			16
#define WLAN_PMK_LEN_EAP_LEAP		16
#define WLAN_PMK_LEN			32
#define WLAN_PMK_LEN_SUITE_B_192	48

#define WLAN_OUI_WFA			0x506f9a
#define WLAN_OUI_TYPE_WFA_P2P		9
#define WLAN_OUI_TYPE_WFA_DPP		0x1A
#define WLAN_OUI_MICROSOFT		0x0050f2
#define WLAN_OUI_TYPE_MICROSOFT_WPA	1
#define WLAN_OUI_TYPE_MICROSOFT_WMM	2
#define WLAN_OUI_TYPE_MICROSOFT_WPS	4
#define WLAN_OUI_TYPE_MICROSOFT_TPC	8

/*
 * WMM/802.11e Tspec Element
 */
#define IEEE80211_WMM_IE_TSPEC_TID_MASK		0x0F
#define IEEE80211_WMM_IE_TSPEC_TID_SHIFT	1

enum ieee80211_tspec_status_code {
	IEEE80211_TSPEC_STATUS_ADMISS_ACCEPTED = 0,
	IEEE80211_TSPEC_STATUS_ADDTS_INVAL_PARAMS = 0x1,
};

struct ieee80211_tspec_ie {
	u8 element_id;
	u8 len;
	u8 oui[3];
	u8 oui_type;
	u8 oui_subtype;
	u8 version;
	__le16 tsinfo;
	u8 tsinfo_resvd;
	__le16 nominal_msdu;
	__le16 max_msdu;
	__le32 min_service_int;
	__le32 max_service_int;
	__le32 inactivity_int;
	__le32 suspension_int;
	__le32 service_start_time;
	__le32 min_data_rate;
	__le32 mean_data_rate;
	__le32 peak_data_rate;
	__le32 max_burst_size;
	__le32 delay_bound;
	__le32 min_phy_rate;
	__le16 sba;
	__le16 medium_time;
} __packed;

/**
 * ieee80211_get_qos_ctl - get pointer to qos control bytes
 * @hdr: the frame
 * Return: a pointer to the QoS control field in the frame header
 *
 * The qos ctrl bytes come after the frame_control, duration, seq_num
 * and 3 or 4 addresses of length ETH_ALEN. Checks frame_control to choose
 * between struct ieee80211_qos_hdr_4addr and struct ieee80211_qos_hdr.
 */
static inline u8 *ieee80211_get_qos_ctl(struct ieee80211_hdr *hdr)
{
	union {
		struct ieee80211_qos_hdr	addr3;
		struct ieee80211_qos_hdr_4addr	addr4;
	} *qos;

	qos = (void *)hdr;
	if (ieee80211_has_a4(qos->addr3.frame_control))
		return (u8 *)&qos->addr4.qos_ctrl;
	else
		return (u8 *)&qos->addr3.qos_ctrl;
}

/**
 * ieee80211_get_tid - get qos TID
 * @hdr: the frame
 * Return: the TID from the QoS control field
 */
static inline u8 ieee80211_get_tid(struct ieee80211_hdr *hdr)
{
	u8 *qc = ieee80211_get_qos_ctl(hdr);

	return qc[0] & IEEE80211_QOS_CTL_TID_MASK;
}

/**
 * ieee80211_get_SA - get pointer to SA
 * @hdr: the frame
 * Return: a pointer to the source address (SA)
 *
 * Given an 802.11 frame, this function returns the offset
 * to the source address (SA). It does not verify that the
 * header is long enough to contain the address, and the
 * header must be long enough to contain the frame control
 * field.
 */
static inline u8 *ieee80211_get_SA(struct ieee80211_hdr *hdr)
{
	if (ieee80211_has_a4(hdr->frame_control))
		return hdr->addr4;
	if (ieee80211_has_fromds(hdr->frame_control))
		return hdr->addr3;
	return hdr->addr2;
}

/**
 * ieee80211_get_DA - get pointer to DA
 * @hdr: the frame
 * Return: a pointer to the destination address (DA)
 *
 * Given an 802.11 frame, this function returns the offset
 * to the destination address (DA). It does not verify that
 * the header is long enough to contain the address, and the
 * header must be long enough to contain the frame control
 * field.
 */
static inline u8 *ieee80211_get_DA(struct ieee80211_hdr *hdr)
{
	if (ieee80211_has_tods(hdr->frame_control))
		return hdr->addr3;
	else
		return hdr->addr1;
}

/**
 * ieee80211_is_bufferable_mmpdu - check if frame is bufferable MMPDU
 * @skb: the skb to check, starting with the 802.11 header
 * Return: whether or not the MMPDU is bufferable
 */
static inline bool ieee80211_is_bufferable_mmpdu(struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	__le16 fc = mgmt->frame_control;

	/*
	 * IEEE 802.11 REVme D2.0 definition of bufferable MMPDU;
	 * note that this ignores the IBSS special case.
	 */
	if (!ieee80211_is_mgmt(fc))
		return false;

	if (ieee80211_is_disassoc(fc) || ieee80211_is_deauth(fc))
		return true;

	if (!ieee80211_is_action(fc))
		return false;

	if (skb->len < offsetofend(typeof(*mgmt), u.action.u.ftm.action_code))
		return true;

	/* action frame - additionally check for non-bufferable FTM */

	if (mgmt->u.action.category != WLAN_CATEGORY_PUBLIC &&
	    mgmt->u.action.category != WLAN_CATEGORY_PROTECTED_DUAL_OF_ACTION)
		return true;

	if (mgmt->u.action.u.ftm.action_code == WLAN_PUB_ACTION_FTM_REQUEST ||
	    mgmt->u.action.u.ftm.action_code == WLAN_PUB_ACTION_FTM_RESPONSE)
		return false;

	return true;
}

/**
 * _ieee80211_is_robust_mgmt_frame - check if frame is a robust management frame
 * @hdr: the frame (buffer must include at least the first octet of payload)
 * Return: whether or not the frame is a robust management frame
 */
static inline bool _ieee80211_is_robust_mgmt_frame(struct ieee80211_hdr *hdr)
{
	if (ieee80211_is_disassoc(hdr->frame_control) ||
	    ieee80211_is_deauth(hdr->frame_control))
		return true;

	if (ieee80211_is_action(hdr->frame_control)) {
		u8 *category;

		/*
		 * Action frames, excluding Public Action frames, are Robust
		 * Management Frames. However, if we are looking at a Protected
		 * frame, skip the check since the data may be encrypted and
		 * the frame has already been found to be a Robust Management
		 * Frame (by the other end).
		 */
		if (ieee80211_has_protected(hdr->frame_control))
			return true;
		category = ((u8 *) hdr) + 24;
		return *category != WLAN_CATEGORY_PUBLIC &&
			*category != WLAN_CATEGORY_HT &&
			*category != WLAN_CATEGORY_WNM_UNPROTECTED &&
			*category != WLAN_CATEGORY_SELF_PROTECTED &&
			*category != WLAN_CATEGORY_UNPROT_DMG &&
			*category != WLAN_CATEGORY_VHT &&
			*category != WLAN_CATEGORY_S1G &&
			*category != WLAN_CATEGORY_VENDOR_SPECIFIC;
	}

	return false;
}

/**
 * ieee80211_is_robust_mgmt_frame - check if skb contains a robust mgmt frame
 * @skb: the skb containing the frame, length will be checked
 * Return: whether or not the frame is a robust management frame
 */
static inline bool ieee80211_is_robust_mgmt_frame(struct sk_buff *skb)
{
	if (skb->len < IEEE80211_MIN_ACTION_SIZE)
		return false;
	return _ieee80211_is_robust_mgmt_frame((void *)skb->data);
}

/**
 * ieee80211_is_public_action - check if frame is a public action frame
 * @hdr: the frame
 * @len: length of the frame
 * Return: whether or not the frame is a public action frame
 */
static inline bool ieee80211_is_public_action(struct ieee80211_hdr *hdr,
					      size_t len)
{
	struct ieee80211_mgmt *mgmt = (void *)hdr;

	if (len < IEEE80211_MIN_ACTION_SIZE)
		return false;
	if (!ieee80211_is_action(hdr->frame_control))
		return false;
	return mgmt->u.action.category == WLAN_CATEGORY_PUBLIC;
}

/**
 * ieee80211_is_protected_dual_of_public_action - check if skb contains a
 * protected dual of public action management frame
 * @skb: the skb containing the frame, length will be checked
 *
 * Return: true if the skb contains a protected dual of public action
 * management frame, false otherwise.
 */
static inline bool
ieee80211_is_protected_dual_of_public_action(struct sk_buff *skb)
{
	u8 action;

	if (!ieee80211_is_public_action((void *)skb->data, skb->len) ||
	    skb->len < IEEE80211_MIN_ACTION_SIZE + 1)
		return false;

	action = *(u8 *)(skb->data + IEEE80211_MIN_ACTION_SIZE);

	return action != WLAN_PUB_ACTION_20_40_BSS_COEX &&
		action != WLAN_PUB_ACTION_DSE_REG_LOC_ANN &&
		action != WLAN_PUB_ACTION_MSMT_PILOT &&
		action != WLAN_PUB_ACTION_TDLS_DISCOVER_RES &&
		action != WLAN_PUB_ACTION_LOC_TRACK_NOTI &&
		action != WLAN_PUB_ACTION_FTM_REQUEST &&
		action != WLAN_PUB_ACTION_FTM_RESPONSE &&
		action != WLAN_PUB_ACTION_FILS_DISCOVERY &&
		action != WLAN_PUB_ACTION_VENDOR_SPECIFIC;
}

/**
 * _ieee80211_is_group_privacy_action - check if frame is a group addressed
 *	privacy action frame
 * @hdr: the frame
 * Return: whether or not the frame is a group addressed privacy action frame
 */
static inline bool _ieee80211_is_group_privacy_action(struct ieee80211_hdr *hdr)
{
	struct ieee80211_mgmt *mgmt = (void *)hdr;

	if (!ieee80211_is_action(hdr->frame_control) ||
	    !is_multicast_ether_addr(hdr->addr1))
		return false;

	return mgmt->u.action.category == WLAN_CATEGORY_MESH_ACTION ||
	       mgmt->u.action.category == WLAN_CATEGORY_MULTIHOP_ACTION;
}

/**
 * ieee80211_is_group_privacy_action - check if frame is a group addressed
 *	privacy action frame
 * @skb: the skb containing the frame, length will be checked
 * Return: whether or not the frame is a group addressed privacy action frame
 */
static inline bool ieee80211_is_group_privacy_action(struct sk_buff *skb)
{
	if (skb->len < IEEE80211_MIN_ACTION_SIZE)
		return false;
	return _ieee80211_is_group_privacy_action((void *)skb->data);
}

/**
 * ieee80211_tu_to_usec - convert time units (TU) to microseconds
 * @tu: the TUs
 * Return: the time value converted to microseconds
 */
static inline unsigned long ieee80211_tu_to_usec(unsigned long tu)
{
	return 1024 * tu;
}

static inline bool __ieee80211_check_tim(const struct ieee80211_tim_ie *tim,
					 u8 tim_len, u16 aid)
{
	u8 mask;
	u8 index, indexn1, indexn2;

	if (unlikely(!tim || tim_len < sizeof(*tim)))
		return false;

	aid &= 0x3fff;
	index = aid / 8;
	mask  = 1 << (aid & 7);

	indexn1 = tim->bitmap_ctrl & 0xfe;
	indexn2 = tim_len + indexn1 - 4;

	if (index < indexn1 || index > indexn2)
		return false;

	index -= indexn1;

	return !!(tim->virtual_map[index] & mask);
}

/**
 * ieee80211_get_tdls_action - get TDLS action code
 * @skb: the skb containing the frame, length will not be checked
 * Return: the TDLS action code, or -1 if it's not an encapsulated TDLS action
 *	frame
 *
 * This function assumes the frame is a data frame, and that the network header
 * is in the correct place.
 */
static inline int ieee80211_get_tdls_action(struct sk_buff *skb)
{
	if (!skb_is_nonlinear(skb) &&
	    skb->len > (skb_network_offset(skb) + 2)) {
		/* Point to where the indication of TDLS should start */
		const u8 *tdls_data = skb_network_header(skb) - 2;

		if (get_unaligned_be16(tdls_data) == ETH_P_TDLS &&
		    tdls_data[2] == WLAN_TDLS_SNAP_RFTYPE &&
		    tdls_data[3] == WLAN_CATEGORY_TDLS)
			return tdls_data[4];
	}

	return -1;
}

/* convert time units */
#define TU_TO_JIFFIES(x)	(usecs_to_jiffies((x) * 1024))
#define TU_TO_EXP_TIME(x)	(jiffies + TU_TO_JIFFIES(x))

/* convert frequencies */
#define MHZ_TO_KHZ(freq) ((freq) * 1000)
#define KHZ_TO_MHZ(freq) ((freq) / 1000)
#define PR_KHZ(f) KHZ_TO_MHZ(f), f % 1000
#define KHZ_F "%d.%03d"

/* convert powers */
#define DBI_TO_MBI(gain) ((gain) * 100)
#define MBI_TO_DBI(gain) ((gain) / 100)
#define DBM_TO_MBM(gain) ((gain) * 100)
#define MBM_TO_DBM(gain) ((gain) / 100)

/**
 * ieee80211_action_contains_tpc - checks if the frame contains TPC element
 * @skb: the skb containing the frame, length will be checked
 * Return: %true if the frame contains a TPC element, %false otherwise
 *
 * This function checks if it's either TPC report action frame or Link
 * Measurement report action frame as defined in IEEE Std. 802.11-2012 8.5.2.5
 * and 8.5.7.5 accordingly.
 */
static inline bool ieee80211_action_contains_tpc(struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;

	if (!ieee80211_is_action(mgmt->frame_control))
		return false;

	if (skb->len < IEEE80211_MIN_ACTION_SIZE +
		       sizeof(mgmt->u.action.u.tpc_report))
		return false;

	/*
	 * TPC report - check that:
	 * category = 0 (Spectrum Management) or 5 (Radio Measurement)
	 * spectrum management action = 3 (TPC/Link Measurement report)
	 * TPC report EID = 35
	 * TPC report element length = 2
	 *
	 * The spectrum management's tpc_report struct is used here both for
	 * parsing tpc_report and radio measurement's link measurement report
	 * frame, since the relevant part is identical in both frames.
	 */
	if (mgmt->u.action.category != WLAN_CATEGORY_SPECTRUM_MGMT &&
	    mgmt->u.action.category != WLAN_CATEGORY_RADIO_MEASUREMENT)
		return false;

	/* both spectrum mgmt and link measurement have same action code */
	if (mgmt->u.action.u.tpc_report.action_code !=
	    WLAN_ACTION_SPCT_TPC_RPRT)
		return false;

	if (mgmt->u.action.u.tpc_report.tpc_elem_id != WLAN_EID_TPC_REPORT ||
	    mgmt->u.action.u.tpc_report.tpc_elem_length !=
	    sizeof(struct ieee80211_tpc_report_ie))
		return false;

	return true;
}

/**
 * ieee80211_is_timing_measurement - check if frame is timing measurement response
 * @skb: the SKB to check
 * Return: whether or not the frame is a valid timing measurement response
 */
static inline bool ieee80211_is_timing_measurement(struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;

	if (skb->len < IEEE80211_MIN_ACTION_SIZE)
		return false;

	if (!ieee80211_is_action(mgmt->frame_control))
		return false;

	if (mgmt->u.action.category == WLAN_CATEGORY_WNM_UNPROTECTED &&
	    mgmt->u.action.u.wnm_timing_msr.action_code ==
		WLAN_UNPROTECTED_WNM_ACTION_TIMING_MEASUREMENT_RESPONSE &&
	    skb->len >= offsetofend(typeof(*mgmt), u.action.u.wnm_timing_msr))
		return true;

	return false;
}

/**
 * ieee80211_is_ftm - check if frame is FTM response
 * @skb: the SKB to check
 * Return: whether or not the frame is a valid FTM response action frame
 */
static inline bool ieee80211_is_ftm(struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;

	if (!ieee80211_is_public_action((void *)mgmt, skb->len))
		return false;

	if (mgmt->u.action.u.ftm.action_code ==
		WLAN_PUB_ACTION_FTM_RESPONSE &&
	    skb->len >= offsetofend(typeof(*mgmt), u.action.u.ftm))
		return true;

	return false;
}

struct element {
	u8 id;
	u8 datalen;
	u8 data[];
} __packed;

/* element iteration helpers */
#define for_each_element(_elem, _data, _datalen)			\
	for (_elem = (const struct element *)(_data);			\
	     (const u8 *)(_data) + (_datalen) - (const u8 *)_elem >=	\
		(int)sizeof(*_elem) &&					\
	     (const u8 *)(_data) + (_datalen) - (const u8 *)_elem >=	\
		(int)sizeof(*_elem) + _elem->datalen;			\
	     _elem = (const struct element *)(_elem->data + _elem->datalen))

#define for_each_element_id(element, _id, data, datalen)		\
	for_each_element(element, data, datalen)			\
		if (element->id == (_id))

#define for_each_element_extid(element, extid, _data, _datalen)		\
	for_each_element(element, _data, _datalen)			\
		if (element->id == WLAN_EID_EXTENSION &&		\
		    element->datalen > 0 &&				\
		    element->data[0] == (extid))

#define for_each_subelement(sub, element)				\
	for_each_element(sub, (element)->data, (element)->datalen)

#define for_each_subelement_id(sub, id, element)			\
	for_each_element_id(sub, id, (element)->data, (element)->datalen)

#define for_each_subelement_extid(sub, extid, element)			\
	for_each_element_extid(sub, extid, (element)->data, (element)->datalen)

/**
 * for_each_element_completed - determine if element parsing consumed all data
 * @element: element pointer after for_each_element() or friends
 * @data: same data pointer as passed to for_each_element() or friends
 * @datalen: same data length as passed to for_each_element() or friends
 * Return: %true if all elements were iterated, %false otherwise; see notes
 *
 * This function returns %true if all the data was parsed or considered
 * while walking the elements. Only use this if your for_each_element()
 * loop cannot be broken out of, otherwise it always returns %false.
 *
 * If some data was malformed, this returns %false since the last parsed
 * element will not fill the whole remaining data.
 */
static inline bool for_each_element_completed(const struct element *element,
					      const void *data, size_t datalen)
{
	return (const u8 *)element == (const u8 *)data + datalen;
}

/*
 * RSNX Capabilities:
 * bits 0-3: Field length (n-1)
 */
#define WLAN_RSNX_CAPA_PROTECTED_TWT BIT(4)
#define WLAN_RSNX_CAPA_SAE_H2E BIT(5)

/* EBPCC = Enhanced BSS Parameter Change Count */
#define IEEE80211_ENH_CRIT_UPD_EBPCC		0x0F
#define IEEE80211_ENH_CRIT_UPD_TYPE		0x70
#define IEEE80211_ENH_CRIT_UPD_TYPE_NO_UHR	0
#define IEEE80211_ENH_CRIT_UPD_TYPE_UHR		1
#define IEEE80211_ENH_CRIT_UPD_ALL		0x80

/**
 * struct ieee80211_enh_crit_upd - enhanced critical update (UHR)
 * @v: value of the enhanced critical update data,
 *	see %IEEE80211_ENH_CRIT_UPD_* to parse the bits
 */
struct ieee80211_enh_crit_upd {
	u8 v;
} __packed;

/*
 * reduced neighbor report, based on Draft P802.11ax_D6.1,
 * section 9.4.2.170 and accepted contributions.
 */
#define IEEE80211_AP_INFO_TBTT_HDR_TYPE				0x03
#define IEEE80211_AP_INFO_TBTT_HDR_FILTERED			0x04
#define IEEE80211_AP_INFO_TBTT_HDR_COLOC			0x08
#define IEEE80211_AP_INFO_TBTT_HDR_COUNT			0xF0
#define IEEE80211_TBTT_INFO_TYPE_TBTT				0
#define IEEE80211_TBTT_INFO_TYPE_MLD				1

#define IEEE80211_RNR_TBTT_PARAMS_OCT_RECOMMENDED		0x01
#define IEEE80211_RNR_TBTT_PARAMS_SAME_SSID			0x02
#define IEEE80211_RNR_TBTT_PARAMS_MULTI_BSSID			0x04
#define IEEE80211_RNR_TBTT_PARAMS_TRANSMITTED_BSSID		0x08
#define IEEE80211_RNR_TBTT_PARAMS_COLOC_ESS			0x10
#define IEEE80211_RNR_TBTT_PARAMS_PROBE_ACTIVE			0x20
#define IEEE80211_RNR_TBTT_PARAMS_COLOC_AP			0x40
#define IEEE80211_RNR_TBTT_PARAMS_SAME_SMD			0x80

#define IEEE80211_RNR_TBTT_PARAMS_PSD_NO_LIMIT			127
#define IEEE80211_RNR_TBTT_PARAMS_PSD_RESERVED			-128

struct ieee80211_neighbor_ap_info {
	u8 tbtt_info_hdr;
	u8 tbtt_info_len;
	u8 op_class;
	u8 channel;
} __packed;

enum ieee80211_range_params_max_total_ltf {
	IEEE80211_RANGE_PARAMS_MAX_TOTAL_LTF_4 = 0,
	IEEE80211_RANGE_PARAMS_MAX_TOTAL_LTF_8,
	IEEE80211_RANGE_PARAMS_MAX_TOTAL_LTF_16,
	IEEE80211_RANGE_PARAMS_MAX_TOTAL_LTF_UNSPECIFIED,
};

/*
 * reduced neighbor report, based on Draft P802.11be_D3.0,
 * section 9.4.2.170.2.
 */
struct ieee80211_rnr_mld_params {
	u8 mld_id;
	__le16 params;
} __packed;

#define IEEE80211_RNR_MLD_PARAMS_LINK_ID			0x000F
#define IEEE80211_RNR_MLD_PARAMS_BSS_CHANGE_COUNT		0x0FF0
#define IEEE80211_RNR_MLD_PARAMS_UPDATES_INCLUDED		0x1000
#define IEEE80211_RNR_MLD_PARAMS_DISABLED_LINK			0x2000

/* Format of the TBTT information element if it has 7, 8 or 9 bytes */
struct ieee80211_tbtt_info_7_8_9 {
	u8 tbtt_offset;
	u8 bssid[ETH_ALEN];

	/* The following element is optional, structure may not grow */
	u8 bss_params;
	s8 psd_20;
} __packed;

/* Format of the TBTT information element if it has >= 11 bytes */
struct ieee80211_tbtt_info_ge_11 {
	u8 tbtt_offset;
	u8 bssid[ETH_ALEN];
	__le32 short_ssid;

	/* The following elements are optional, structure may grow */
	u8 bss_params;
	s8 psd_20;
	struct ieee80211_rnr_mld_params mld_params;
	struct ieee80211_enh_crit_upd enh_crit_upd;
} __packed;

#include "ieee80211-ht.h"
#include "ieee80211-vht.h"
#include "ieee80211-he.h"
#include "ieee80211-eht.h"
#include "ieee80211-uhr.h"
#include "ieee80211-mesh.h"
#include "ieee80211-s1g.h"
#include "ieee80211-p2p.h"
#include "ieee80211-nan.h"

/**
 * ieee80211_check_tim - check if AID bit is set in TIM
 * @tim: the TIM IE
 * @tim_len: length of the TIM IE
 * @aid: the AID to look for
 * @s1g: whether the TIM is from an S1G PPDU
 * Return: whether or not traffic is indicated in the TIM for the given AID
 */
static inline bool ieee80211_check_tim(const struct ieee80211_tim_ie *tim,
				       u8 tim_len, u16 aid, bool s1g)
{
	return s1g ? ieee80211_s1g_check_tim(tim, tim_len, aid) :
		     __ieee80211_check_tim(tim, tim_len, aid);
}

#endif /* LINUX_IEEE80211_H */
