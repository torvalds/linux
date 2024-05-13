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
 * Copyright (c) 2018 - 2023 Intel Corporation
 */

#ifndef LINUX_IEEE80211_H
#define LINUX_IEEE80211_H

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/bitfield.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

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

/* bits unique to S1G beacon */
#define IEEE80211_S1G_BCN_NEXT_TBTT	0x100

/* see 802.11ah-2016 9.9 NDP CMAC frames */
#define IEEE80211_S1G_1MHZ_NDP_BITS	25
#define IEEE80211_S1G_1MHZ_NDP_BYTES	4
#define IEEE80211_S1G_2MHZ_NDP_BITS	37
#define IEEE80211_S1G_2MHZ_NDP_BYTES	5

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

/* bits unique to S1G beacon */
#define IEEE80211_S1G_BCN_NEXT_TBTT    0x100

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


/* PV1 Layout 11ah 9.8.3.1 */
#define IEEE80211_PV1_FCTL_VERS		0x0003
#define IEEE80211_PV1_FCTL_FTYPE	0x001c
#define IEEE80211_PV1_FCTL_STYPE	0x00e0
#define IEEE80211_PV1_FCTL_TODS		0x0100
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

/* Maximal size of an A-MSDU that can be transported in a HT BA session */
#define IEEE80211_MAX_MPDU_LEN_HT_BA		4095

/* Maximal size of an A-MSDU */
#define IEEE80211_MAX_MPDU_LEN_HT_3839		3839
#define IEEE80211_MAX_MPDU_LEN_HT_7935		7935

#define IEEE80211_MAX_MPDU_LEN_VHT_3895		3895
#define IEEE80211_MAX_MPDU_LEN_VHT_7991		7991
#define IEEE80211_MAX_MPDU_LEN_VHT_11454	11454

#define IEEE80211_MAX_SSID_LEN		32

#define IEEE80211_MAX_MESH_ID_LEN	32

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

#define IEEE80211_HT_CTL_LEN		4

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
 */
static inline bool ieee80211_has_tods(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_TODS)) != 0;
}

/**
 * ieee80211_has_fromds - check if IEEE80211_FCTL_FROMDS is set
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_has_fromds(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FROMDS)) != 0;
}

/**
 * ieee80211_has_a4 - check if IEEE80211_FCTL_TODS and IEEE80211_FCTL_FROMDS are set
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_has_a4(__le16 fc)
{
	__le16 tmp = cpu_to_le16(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS);
	return (fc & tmp) == tmp;
}

/**
 * ieee80211_has_morefrags - check if IEEE80211_FCTL_MOREFRAGS is set
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_has_morefrags(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)) != 0;
}

/**
 * ieee80211_has_retry - check if IEEE80211_FCTL_RETRY is set
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_has_retry(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_RETRY)) != 0;
}

/**
 * ieee80211_has_pm - check if IEEE80211_FCTL_PM is set
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_has_pm(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_PM)) != 0;
}

/**
 * ieee80211_has_moredata - check if IEEE80211_FCTL_MOREDATA is set
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_has_moredata(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_MOREDATA)) != 0;
}

/**
 * ieee80211_has_protected - check if IEEE80211_FCTL_PROTECTED is set
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_has_protected(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_PROTECTED)) != 0;
}

/**
 * ieee80211_has_order - check if IEEE80211_FCTL_ORDER is set
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_has_order(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_ORDER)) != 0;
}

/**
 * ieee80211_is_mgmt - check if type is IEEE80211_FTYPE_MGMT
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_mgmt(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT);
}

/**
 * ieee80211_is_ctl - check if type is IEEE80211_FTYPE_CTL
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_ctl(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL);
}

/**
 * ieee80211_is_data - check if type is IEEE80211_FTYPE_DATA
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_data(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA);
}

/**
 * ieee80211_is_ext - check if type is IEEE80211_FTYPE_EXT
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_ext(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_EXT);
}


/**
 * ieee80211_is_data_qos - check if type is IEEE80211_FTYPE_DATA and IEEE80211_STYPE_QOS_DATA is set
 * @fc: frame control bytes in little-endian byteorder
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
 */
static inline bool ieee80211_is_assoc_req(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ASSOC_REQ);
}

/**
 * ieee80211_is_assoc_resp - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_ASSOC_RESP
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_assoc_resp(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ASSOC_RESP);
}

/**
 * ieee80211_is_reassoc_req - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_REASSOC_REQ
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_reassoc_req(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_REASSOC_REQ);
}

/**
 * ieee80211_is_reassoc_resp - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_REASSOC_RESP
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_reassoc_resp(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_REASSOC_RESP);
}

/**
 * ieee80211_is_probe_req - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_PROBE_REQ
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_probe_req(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ);
}

/**
 * ieee80211_is_probe_resp - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_PROBE_RESP
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_probe_resp(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_RESP);
}

/**
 * ieee80211_is_beacon - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_BEACON
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_beacon(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON);
}

/**
 * ieee80211_is_s1g_beacon - check if IEEE80211_FTYPE_EXT &&
 * IEEE80211_STYPE_S1G_BEACON
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_s1g_beacon(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE |
				 IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_EXT | IEEE80211_STYPE_S1G_BEACON);
}

/**
 * ieee80211_next_tbtt_present - check if IEEE80211_FTYPE_EXT &&
 * IEEE80211_STYPE_S1G_BEACON && IEEE80211_S1G_BCN_NEXT_TBTT
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_next_tbtt_present(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_EXT | IEEE80211_STYPE_S1G_BEACON) &&
	       fc & cpu_to_le16(IEEE80211_S1G_BCN_NEXT_TBTT);
}

/**
 * ieee80211_is_s1g_short_beacon - check if next tbtt present bit is set. Only
 * true for S1G beacons when they're short.
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_s1g_short_beacon(__le16 fc)
{
	return ieee80211_is_s1g_beacon(fc) && ieee80211_next_tbtt_present(fc);
}

/**
 * ieee80211_is_atim - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_ATIM
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_atim(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ATIM);
}

/**
 * ieee80211_is_disassoc - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_DISASSOC
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_disassoc(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DISASSOC);
}

/**
 * ieee80211_is_auth - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_AUTH
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_auth(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_AUTH);
}

/**
 * ieee80211_is_deauth - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_DEAUTH
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_deauth(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DEAUTH);
}

/**
 * ieee80211_is_action - check if IEEE80211_FTYPE_MGMT && IEEE80211_STYPE_ACTION
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_action(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION);
}

/**
 * ieee80211_is_back_req - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_BACK_REQ
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_back_req(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_BACK_REQ);
}

/**
 * ieee80211_is_back - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_BACK
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_back(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_BACK);
}

/**
 * ieee80211_is_pspoll - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_PSPOLL
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_pspoll(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_PSPOLL);
}

/**
 * ieee80211_is_rts - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_RTS
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_rts(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS);
}

/**
 * ieee80211_is_cts - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_CTS
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_cts(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS);
}

/**
 * ieee80211_is_ack - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_ACK
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_ack(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_ACK);
}

/**
 * ieee80211_is_cfend - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_CFEND
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_cfend(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CFEND);
}

/**
 * ieee80211_is_cfendack - check if IEEE80211_FTYPE_CTL && IEEE80211_STYPE_CFENDACK
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_cfendack(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CFENDACK);
}

/**
 * ieee80211_is_nullfunc - check if frame is a regular (non-QoS) nullfunc frame
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_nullfunc(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC);
}

/**
 * ieee80211_is_qos_nullfunc - check if frame is a QoS nullfunc frame
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_qos_nullfunc(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_NULLFUNC);
}

/**
 * ieee80211_is_trigger - check if frame is trigger frame
 * @fc: frame control field in little-endian byteorder
 */
static inline bool ieee80211_is_trigger(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_TRIGGER);
}

/**
 * ieee80211_is_any_nullfunc - check if frame is regular or QoS nullfunc frame
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_any_nullfunc(__le16 fc)
{
	return (ieee80211_is_nullfunc(fc) || ieee80211_is_qos_nullfunc(fc));
}

/**
 * ieee80211_is_first_frag - check if IEEE80211_SCTL_FRAG is not set
 * @seq_ctrl: frame sequence control bytes in little-endian byteorder
 */
static inline bool ieee80211_is_first_frag(__le16 seq_ctrl)
{
	return (seq_ctrl & cpu_to_le16(IEEE80211_SCTL_FRAG)) == 0;
}

/**
 * ieee80211_is_frag - check if a frame is a fragment
 * @hdr: 802.11 header of the frame
 */
static inline bool ieee80211_is_frag(struct ieee80211_hdr *hdr)
{
	return ieee80211_has_morefrags(hdr->frame_control) ||
	       hdr->seq_ctrl & cpu_to_le16(IEEE80211_SCTL_FRAG);
}

struct ieee80211s_hdr {
	u8 flags;
	u8 ttl;
	__le32 seqnum;
	u8 eaddr1[ETH_ALEN];
	u8 eaddr2[ETH_ALEN];
} __packed __aligned(2);

/* Mesh flags */
#define MESH_FLAGS_AE_A4 	0x1
#define MESH_FLAGS_AE_A5_A6	0x2
#define MESH_FLAGS_AE		0x3
#define MESH_FLAGS_PS_DEEP	0x4

/**
 * enum ieee80211_preq_flags - mesh PREQ element flags
 *
 * @IEEE80211_PREQ_PROACTIVE_PREP_FLAG: proactive PREP subfield
 */
enum ieee80211_preq_flags {
	IEEE80211_PREQ_PROACTIVE_PREP_FLAG	= 1<<2,
};

/**
 * enum ieee80211_preq_target_flags - mesh PREQ element per target flags
 *
 * @IEEE80211_PREQ_TO_FLAG: target only subfield
 * @IEEE80211_PREQ_USN_FLAG: unknown target HWMP sequence number subfield
 */
enum ieee80211_preq_target_flags {
	IEEE80211_PREQ_TO_FLAG	= 1<<0,
	IEEE80211_PREQ_USN_FLAG	= 1<<2,
};

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
 * struct ieee80211_mesh_chansw_params_ie - mesh channel switch parameters IE
 * @mesh_ttl: Time To Live
 * @mesh_flags: Flags
 * @mesh_reason: Reason Code
 * @mesh_pre_value: Precedence Value
 *
 * This structure represents the payload of the "Mesh Channel Switch
 * Parameters element" as described in IEEE Std 802.11-2020 section
 * 9.4.2.102.
 */
struct ieee80211_mesh_chansw_params_ie {
	u8 mesh_ttl;
	u8 mesh_flags;
	__le16 mesh_reason;
	__le16 mesh_pre_value;
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
 * @virtual_map: Partial Virtual Bitmap
 *
 * This structure represents the payload of the "TIM element" as
 * described in IEEE Std 802.11-2020 section 9.4.2.5.
 */
struct ieee80211_tim_ie {
	u8 dtim_count;
	u8 dtim_period;
	u8 bitmap_ctrl;
	/* variable size: 1 - 251 bytes */
	u8 virtual_map[1];
} __packed;

/**
 * struct ieee80211_meshconf_ie - Mesh Configuration element
 * @meshconf_psel: Active Path Selection Protocol Identifier
 * @meshconf_pmetric: Active Path Selection Metric Identifier
 * @meshconf_congest: Congestion Control Mode Identifier
 * @meshconf_synch: Synchronization Method Identifier
 * @meshconf_auth: Authentication Protocol Identifier
 * @meshconf_form: Mesh Formation Info
 * @meshconf_cap: Mesh Capability (see &enum mesh_config_capab_flags)
 *
 * This structure represents the payload of the "Mesh Configuration
 * element" as described in IEEE Std 802.11-2020 section 9.4.2.97.
 */
struct ieee80211_meshconf_ie {
	u8 meshconf_psel;
	u8 meshconf_pmetric;
	u8 meshconf_congest;
	u8 meshconf_synch;
	u8 meshconf_auth;
	u8 meshconf_form;
	u8 meshconf_cap;
} __packed;

/**
 * enum mesh_config_capab_flags - Mesh Configuration IE capability field flags
 *
 * @IEEE80211_MESHCONF_CAPAB_ACCEPT_PLINKS: STA is willing to establish
 *	additional mesh peerings with other mesh STAs
 * @IEEE80211_MESHCONF_CAPAB_FORWARDING: the STA forwards MSDUs
 * @IEEE80211_MESHCONF_CAPAB_TBTT_ADJUSTING: TBTT adjustment procedure
 *	is ongoing
 * @IEEE80211_MESHCONF_CAPAB_POWER_SAVE_LEVEL: STA is in deep sleep mode or has
 *	neighbors in deep sleep mode
 *
 * Enumerates the "Mesh Capability" as described in IEEE Std
 * 802.11-2020 section 9.4.2.97.7.
 */
enum mesh_config_capab_flags {
	IEEE80211_MESHCONF_CAPAB_ACCEPT_PLINKS		= 0x01,
	IEEE80211_MESHCONF_CAPAB_FORWARDING		= 0x08,
	IEEE80211_MESHCONF_CAPAB_TBTT_ADJUSTING		= 0x20,
	IEEE80211_MESHCONF_CAPAB_POWER_SAVE_LEVEL	= 0x40,
};

#define IEEE80211_MESHCONF_FORM_CONNECTED_TO_GATE 0x1

/*
 * mesh channel switch parameters element's flag indicator
 *
 */
#define WLAN_EID_CHAN_SWITCH_PARAM_TX_RESTRICT BIT(0)
#define WLAN_EID_CHAN_SWITCH_PARAM_INITIATOR BIT(1)
#define WLAN_EID_CHAN_SWITCH_PARAM_REASON BIT(2)

/**
 * struct ieee80211_rann_ie - RANN (root announcement) element
 * @rann_flags: Flags
 * @rann_hopcount: Hop Count
 * @rann_ttl: Element TTL
 * @rann_addr: Root Mesh STA Address
 * @rann_seq: HWMP Sequence Number
 * @rann_interval: Interval
 * @rann_metric: Metric
 *
 * This structure represents the payload of the "RANN element" as
 * described in IEEE Std 802.11-2020 section 9.4.2.111.
 */
struct ieee80211_rann_ie {
	u8 rann_flags;
	u8 rann_hopcount;
	u8 rann_ttl;
	u8 rann_addr[ETH_ALEN];
	__le32 rann_seq;
	__le32 rann_interval;
	__le32 rann_metric;
} __packed;

enum ieee80211_rann_flags {
	RANN_FLAG_IS_GATE = 1 << 0,
};

enum ieee80211_ht_chanwidth_values {
	IEEE80211_HT_CHANWIDTH_20MHZ = 0,
	IEEE80211_HT_CHANWIDTH_ANY = 1,
};

/**
 * enum ieee80211_vht_opmode_bits - VHT operating mode field bits
 * @IEEE80211_OPMODE_NOTIF_CHANWIDTH_MASK: channel width mask
 * @IEEE80211_OPMODE_NOTIF_CHANWIDTH_20MHZ: 20 MHz channel width
 * @IEEE80211_OPMODE_NOTIF_CHANWIDTH_40MHZ: 40 MHz channel width
 * @IEEE80211_OPMODE_NOTIF_CHANWIDTH_80MHZ: 80 MHz channel width
 * @IEEE80211_OPMODE_NOTIF_CHANWIDTH_160MHZ: 160 MHz or 80+80 MHz channel width
 * @IEEE80211_OPMODE_NOTIF_BW_160_80P80: 160 / 80+80 MHz indicator flag
 * @IEEE80211_OPMODE_NOTIF_RX_NSS_MASK: number of spatial streams mask
 *	(the NSS value is the value of this field + 1)
 * @IEEE80211_OPMODE_NOTIF_RX_NSS_SHIFT: number of spatial streams shift
 * @IEEE80211_OPMODE_NOTIF_RX_NSS_TYPE_BF: indicates streams in SU-MIMO PPDU
 *	using a beamforming steering matrix
 */
enum ieee80211_vht_opmode_bits {
	IEEE80211_OPMODE_NOTIF_CHANWIDTH_MASK	= 0x03,
	IEEE80211_OPMODE_NOTIF_CHANWIDTH_20MHZ	= 0,
	IEEE80211_OPMODE_NOTIF_CHANWIDTH_40MHZ	= 1,
	IEEE80211_OPMODE_NOTIF_CHANWIDTH_80MHZ	= 2,
	IEEE80211_OPMODE_NOTIF_CHANWIDTH_160MHZ	= 3,
	IEEE80211_OPMODE_NOTIF_BW_160_80P80	= 0x04,
	IEEE80211_OPMODE_NOTIF_RX_NSS_MASK	= 0x70,
	IEEE80211_OPMODE_NOTIF_RX_NSS_SHIFT	= 4,
	IEEE80211_OPMODE_NOTIF_RX_NSS_TYPE_BF	= 0x80,
};

/**
 * enum ieee80211_s1g_chanwidth
 * These are defined in IEEE802.11-2016ah Table 10-20
 * as BSS Channel Width
 *
 * @IEEE80211_S1G_CHANWIDTH_1MHZ: 1MHz operating channel
 * @IEEE80211_S1G_CHANWIDTH_2MHZ: 2MHz operating channel
 * @IEEE80211_S1G_CHANWIDTH_4MHZ: 4MHz operating channel
 * @IEEE80211_S1G_CHANWIDTH_8MHZ: 8MHz operating channel
 * @IEEE80211_S1G_CHANWIDTH_16MHZ: 16MHz operating channel
 */
enum ieee80211_s1g_chanwidth {
	IEEE80211_S1G_CHANWIDTH_1MHZ = 0,
	IEEE80211_S1G_CHANWIDTH_2MHZ = 1,
	IEEE80211_S1G_CHANWIDTH_4MHZ = 3,
	IEEE80211_S1G_CHANWIDTH_8MHZ = 7,
	IEEE80211_S1G_CHANWIDTH_16MHZ = 15,
};

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

/**
 * struct ieee80211_s1g_bcn_compat_ie - S1G Beacon Compatibility element
 * @compat_info: Compatibility Information
 * @beacon_int: Beacon Interval
 * @tsf_completion: TSF Completion
 *
 * This structure represents the payload of the "S1G Beacon
 * Compatibility element" as described in IEEE Std 802.11-2020 section
 * 9.4.2.196.
 */
struct ieee80211_s1g_bcn_compat_ie {
	__le16 compat_info;
	__le16 beacon_int;
	__le32 tsf_completion;
} __packed;

/**
 * struct ieee80211_s1g_oper_ie - S1G Operation element
 * @ch_width: S1G Operation Information Channel Width
 * @oper_class: S1G Operation Information Operating Class
 * @primary_ch: S1G Operation Information Primary Channel Number
 * @oper_ch: S1G Operation Information  Channel Center Frequency
 * @basic_mcs_nss: Basic S1G-MCS and NSS Set
 *
 * This structure represents the payload of the "S1G Operation
 * element" as described in IEEE Std 802.11-2020 section 9.4.2.212.
 */
struct ieee80211_s1g_oper_ie {
	u8 ch_width;
	u8 oper_class;
	u8 primary_ch;
	u8 oper_ch;
	__le16 basic_mcs_nss;
} __packed;

/**
 * struct ieee80211_aid_response_ie - AID Response element
 * @aid: AID/Group AID
 * @switch_count: AID Switch Count
 * @response_int: AID Response Interval
 *
 * This structure represents the payload of the "AID Response element"
 * as described in IEEE Std 802.11-2020 section 9.4.2.194.
 */
struct ieee80211_aid_response_ie {
	__le16 aid;
	u8 switch_count;
	__le16 response_int;
} __packed;

struct ieee80211_s1g_cap {
	u8 capab_info[10];
	u8 supp_mcs_nss[5];
} __packed;

struct ieee80211_ext {
	__le16 frame_control;
	__le16 duration;
	union {
		struct {
			u8 sa[ETH_ALEN];
			__le32 timestamp;
			u8 change_seq;
			u8 variable[0];
		} __packed s1g_beacon;
		struct {
			u8 sa[ETH_ALEN];
			__le32 timestamp;
			u8 change_seq;
			u8 next_tbtt[3];
			u8 variable[0];
		} __packed s1g_short_beacon;
	} u;
} __packed __aligned(2);

#define IEEE80211_TWT_CONTROL_NDP			BIT(0)
#define IEEE80211_TWT_CONTROL_RESP_MODE			BIT(1)
#define IEEE80211_TWT_CONTROL_NEG_TYPE_BROADCAST	BIT(3)
#define IEEE80211_TWT_CONTROL_RX_DISABLED		BIT(4)
#define IEEE80211_TWT_CONTROL_WAKE_DUR_UNIT		BIT(5)

#define IEEE80211_TWT_REQTYPE_REQUEST			BIT(0)
#define IEEE80211_TWT_REQTYPE_SETUP_CMD			GENMASK(3, 1)
#define IEEE80211_TWT_REQTYPE_TRIGGER			BIT(4)
#define IEEE80211_TWT_REQTYPE_IMPLICIT			BIT(5)
#define IEEE80211_TWT_REQTYPE_FLOWTYPE			BIT(6)
#define IEEE80211_TWT_REQTYPE_FLOWID			GENMASK(9, 7)
#define IEEE80211_TWT_REQTYPE_WAKE_INT_EXP		GENMASK(14, 10)
#define IEEE80211_TWT_REQTYPE_PROTECTION		BIT(15)

enum ieee80211_twt_setup_cmd {
	TWT_SETUP_CMD_REQUEST,
	TWT_SETUP_CMD_SUGGEST,
	TWT_SETUP_CMD_DEMAND,
	TWT_SETUP_CMD_GROUPING,
	TWT_SETUP_CMD_ACCEPT,
	TWT_SETUP_CMD_ALTERNATE,
	TWT_SETUP_CMD_DICTATE,
	TWT_SETUP_CMD_REJECT,
};

struct ieee80211_twt_params {
	__le16 req_type;
	__le64 twt;
	u8 min_twt_dur;
	__le16 mantissa;
	u8 channel;
} __packed;

struct ieee80211_twt_setup {
	u8 dialog_token;
	u8 element_id;
	u8 length;
	u8 control;
	u8 params[];
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
					u8 variable[0];
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
			} u;
		} __packed action;
		DECLARE_FLEX_ARRAY(u8, body); /* Generic frame body */
	} u;
} __packed __aligned(2);

/* Supported rates membership selectors */
#define BSS_MEMBERSHIP_SELECTOR_HT_PHY	127
#define BSS_MEMBERSHIP_SELECTOR_VHT_PHY	126
#define BSS_MEMBERSHIP_SELECTOR_GLK	125
#define BSS_MEMBERSHIP_SELECTOR_EPS	124
#define BSS_MEMBERSHIP_SELECTOR_SAE_H2E 123
#define BSS_MEMBERSHIP_SELECTOR_HE_PHY	122
#define BSS_MEMBERSHIP_SELECTOR_EHT_PHY	121

/* mgmt header + 1 byte category code */
#define IEEE80211_MIN_ACTION_SIZE offsetof(struct ieee80211_mgmt, u.action.u)


/* Management MIC information element (IEEE 802.11w) */
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
			u8 variable[0];
		} __packed setup_req;
		struct {
			__le16 status_code;
			u8 dialog_token;
			__le16 capability;
			u8 variable[0];
		} __packed setup_resp;
		struct {
			__le16 status_code;
			u8 dialog_token;
			u8 variable[0];
		} __packed setup_cfm;
		struct {
			__le16 reason_code;
			u8 variable[0];
		} __packed teardown;
		struct {
			u8 dialog_token;
			u8 variable[0];
		} __packed discover_req;
		struct {
			u8 target_channel;
			u8 oper_class;
			u8 variable[0];
		} __packed chan_switch_req;
		struct {
			__le16 status_code;
			u8 variable[0];
		} __packed chan_switch_resp;
	} u;
} __packed;

/*
 * Peer-to-Peer IE attribute related definitions.
 */
/*
 * enum ieee80211_p2p_attr_id - identifies type of peer-to-peer attribute.
 */
enum ieee80211_p2p_attr_id {
	IEEE80211_P2P_ATTR_STATUS = 0,
	IEEE80211_P2P_ATTR_MINOR_REASON,
	IEEE80211_P2P_ATTR_CAPABILITY,
	IEEE80211_P2P_ATTR_DEVICE_ID,
	IEEE80211_P2P_ATTR_GO_INTENT,
	IEEE80211_P2P_ATTR_GO_CONFIG_TIMEOUT,
	IEEE80211_P2P_ATTR_LISTEN_CHANNEL,
	IEEE80211_P2P_ATTR_GROUP_BSSID,
	IEEE80211_P2P_ATTR_EXT_LISTEN_TIMING,
	IEEE80211_P2P_ATTR_INTENDED_IFACE_ADDR,
	IEEE80211_P2P_ATTR_MANAGABILITY,
	IEEE80211_P2P_ATTR_CHANNEL_LIST,
	IEEE80211_P2P_ATTR_ABSENCE_NOTICE,
	IEEE80211_P2P_ATTR_DEVICE_INFO,
	IEEE80211_P2P_ATTR_GROUP_INFO,
	IEEE80211_P2P_ATTR_GROUP_ID,
	IEEE80211_P2P_ATTR_INTERFACE,
	IEEE80211_P2P_ATTR_OPER_CHANNEL,
	IEEE80211_P2P_ATTR_INVITE_FLAGS,
	/* 19 - 220: Reserved */
	IEEE80211_P2P_ATTR_VENDOR_SPECIFIC = 221,

	IEEE80211_P2P_ATTR_MAX
};

/* Notice of Absence attribute - described in P2P spec 4.1.14 */
/* Typical max value used here */
#define IEEE80211_P2P_NOA_DESC_MAX	4

struct ieee80211_p2p_noa_desc {
	u8 count;
	__le32 duration;
	__le32 interval;
	__le32 start_time;
} __packed;

struct ieee80211_p2p_noa_attr {
	u8 index;
	u8 oppps_ctwindow;
	struct ieee80211_p2p_noa_desc desc[IEEE80211_P2P_NOA_DESC_MAX];
} __packed;

#define IEEE80211_P2P_OPPPS_ENABLE_BIT		BIT(7)
#define IEEE80211_P2P_OPPPS_CTWINDOW_MASK	0x7F

/**
 * struct ieee80211_bar - Block Ack Request frame format
 * @frame_control: Frame Control
 * @duration: Duration
 * @ra: RA
 * @ta: TA
 * @control: BAR Control
 * @start_seq_num: Starting Sequence Number (see Figure 9-37)
 *
 * This structure represents the "BlockAckReq frame format"
 * as described in IEEE Std 802.11-2020 section 9.3.1.7.
*/
struct ieee80211_bar {
	__le16 frame_control;
	__le16 duration;
	__u8 ra[ETH_ALEN];
	__u8 ta[ETH_ALEN];
	__le16 control;
	__le16 start_seq_num;
} __packed;

/* 802.11 BAR control masks */
#define IEEE80211_BAR_CTRL_ACK_POLICY_NORMAL	0x0000
#define IEEE80211_BAR_CTRL_MULTI_TID		0x0002
#define IEEE80211_BAR_CTRL_CBMTID_COMPRESSED_BA	0x0004
#define IEEE80211_BAR_CTRL_TID_INFO_MASK	0xf000
#define IEEE80211_BAR_CTRL_TID_INFO_SHIFT	12

#define IEEE80211_HT_MCS_MASK_LEN		10

/**
 * struct ieee80211_mcs_info - Supported MCS Set field
 * @rx_mask: RX mask
 * @rx_highest: highest supported RX rate. If set represents
 *	the highest supported RX data rate in units of 1 Mbps.
 *	If this field is 0 this value should not be used to
 *	consider the highest RX data rate supported.
 * @tx_params: TX parameters
 * @reserved: Reserved bits
 *
 * This structure represents the "Supported MCS Set field" as
 * described in IEEE Std 802.11-2020 section 9.4.2.55.4.
 */
struct ieee80211_mcs_info {
	u8 rx_mask[IEEE80211_HT_MCS_MASK_LEN];
	__le16 rx_highest;
	u8 tx_params;
	u8 reserved[3];
} __packed;

/* 802.11n HT capability MSC set */
#define IEEE80211_HT_MCS_RX_HIGHEST_MASK	0x3ff
#define IEEE80211_HT_MCS_TX_DEFINED		0x01
#define IEEE80211_HT_MCS_TX_RX_DIFF		0x02
/* value 0 == 1 stream etc */
#define IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK	0x0C
#define IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT	2
#define		IEEE80211_HT_MCS_TX_MAX_STREAMS	4
#define IEEE80211_HT_MCS_TX_UNEQUAL_MODULATION	0x10

/*
 * 802.11n D5.0 20.3.5 / 20.6 says:
 * - indices 0 to 7 and 32 are single spatial stream
 * - 8 to 31 are multiple spatial streams using equal modulation
 *   [8..15 for two streams, 16..23 for three and 24..31 for four]
 * - remainder are multiple spatial streams using unequal modulation
 */
#define IEEE80211_HT_MCS_UNEQUAL_MODULATION_START 33
#define IEEE80211_HT_MCS_UNEQUAL_MODULATION_START_BYTE \
	(IEEE80211_HT_MCS_UNEQUAL_MODULATION_START / 8)

/**
 * struct ieee80211_ht_cap - HT capabilities element
 * @cap_info: HT Capability Information
 * @ampdu_params_info: A-MPDU Parameters
 * @mcs: Supported MCS Set
 * @extended_ht_cap_info: HT Extended Capabilities
 * @tx_BF_cap_info: Transmit Beamforming Capabilities
 * @antenna_selection_info: ASEL Capability
 *
 * This structure represents the payload of the "HT Capabilities
 * element" as described in IEEE Std 802.11-2020 section 9.4.2.55.
 */
struct ieee80211_ht_cap {
	__le16 cap_info;
	u8 ampdu_params_info;

	/* 16 bytes MCS information */
	struct ieee80211_mcs_info mcs;

	__le16 extended_ht_cap_info;
	__le32 tx_BF_cap_info;
	u8 antenna_selection_info;
} __packed;

/* 802.11n HT capabilities masks (for cap_info) */
#define IEEE80211_HT_CAP_LDPC_CODING		0x0001
#define IEEE80211_HT_CAP_SUP_WIDTH_20_40	0x0002
#define IEEE80211_HT_CAP_SM_PS			0x000C
#define		IEEE80211_HT_CAP_SM_PS_SHIFT	2
#define IEEE80211_HT_CAP_GRN_FLD		0x0010
#define IEEE80211_HT_CAP_SGI_20			0x0020
#define IEEE80211_HT_CAP_SGI_40			0x0040
#define IEEE80211_HT_CAP_TX_STBC		0x0080
#define IEEE80211_HT_CAP_RX_STBC		0x0300
#define		IEEE80211_HT_CAP_RX_STBC_SHIFT	8
#define IEEE80211_HT_CAP_DELAY_BA		0x0400
#define IEEE80211_HT_CAP_MAX_AMSDU		0x0800
#define IEEE80211_HT_CAP_DSSSCCK40		0x1000
#define IEEE80211_HT_CAP_RESERVED		0x2000
#define IEEE80211_HT_CAP_40MHZ_INTOLERANT	0x4000
#define IEEE80211_HT_CAP_LSIG_TXOP_PROT		0x8000

/* 802.11n HT extended capabilities masks (for extended_ht_cap_info) */
#define IEEE80211_HT_EXT_CAP_PCO		0x0001
#define IEEE80211_HT_EXT_CAP_PCO_TIME		0x0006
#define		IEEE80211_HT_EXT_CAP_PCO_TIME_SHIFT	1
#define IEEE80211_HT_EXT_CAP_MCS_FB		0x0300
#define		IEEE80211_HT_EXT_CAP_MCS_FB_SHIFT	8
#define IEEE80211_HT_EXT_CAP_HTC_SUP		0x0400
#define IEEE80211_HT_EXT_CAP_RD_RESPONDER	0x0800

/* 802.11n HT capability AMPDU settings (for ampdu_params_info) */
#define IEEE80211_HT_AMPDU_PARM_FACTOR		0x03
#define IEEE80211_HT_AMPDU_PARM_DENSITY		0x1C
#define		IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT	2

/*
 * Maximum length of AMPDU that the STA can receive in high-throughput (HT).
 * Length = 2 ^ (13 + max_ampdu_length_exp) - 1 (octets)
 */
enum ieee80211_max_ampdu_length_exp {
	IEEE80211_HT_MAX_AMPDU_8K = 0,
	IEEE80211_HT_MAX_AMPDU_16K = 1,
	IEEE80211_HT_MAX_AMPDU_32K = 2,
	IEEE80211_HT_MAX_AMPDU_64K = 3
};

/*
 * Maximum length of AMPDU that the STA can receive in VHT.
 * Length = 2 ^ (13 + max_ampdu_length_exp) - 1 (octets)
 */
enum ieee80211_vht_max_ampdu_length_exp {
	IEEE80211_VHT_MAX_AMPDU_8K = 0,
	IEEE80211_VHT_MAX_AMPDU_16K = 1,
	IEEE80211_VHT_MAX_AMPDU_32K = 2,
	IEEE80211_VHT_MAX_AMPDU_64K = 3,
	IEEE80211_VHT_MAX_AMPDU_128K = 4,
	IEEE80211_VHT_MAX_AMPDU_256K = 5,
	IEEE80211_VHT_MAX_AMPDU_512K = 6,
	IEEE80211_VHT_MAX_AMPDU_1024K = 7
};

#define IEEE80211_HT_MAX_AMPDU_FACTOR 13

/* Minimum MPDU start spacing */
enum ieee80211_min_mpdu_spacing {
	IEEE80211_HT_MPDU_DENSITY_NONE = 0,	/* No restriction */
	IEEE80211_HT_MPDU_DENSITY_0_25 = 1,	/* 1/4 usec */
	IEEE80211_HT_MPDU_DENSITY_0_5 = 2,	/* 1/2 usec */
	IEEE80211_HT_MPDU_DENSITY_1 = 3,	/* 1 usec */
	IEEE80211_HT_MPDU_DENSITY_2 = 4,	/* 2 usec */
	IEEE80211_HT_MPDU_DENSITY_4 = 5,	/* 4 usec */
	IEEE80211_HT_MPDU_DENSITY_8 = 6,	/* 8 usec */
	IEEE80211_HT_MPDU_DENSITY_16 = 7	/* 16 usec */
};

/**
 * struct ieee80211_ht_operation - HT operation IE
 * @primary_chan: Primary Channel
 * @ht_param: HT Operation Information parameters
 * @operation_mode: HT Operation Information operation mode
 * @stbc_param: HT Operation Information STBC params
 * @basic_set: Basic HT-MCS Set
 *
 * This structure represents the payload of the "HT Operation
 * element" as described in IEEE Std 802.11-2020 section 9.4.2.56.
 */
struct ieee80211_ht_operation {
	u8 primary_chan;
	u8 ht_param;
	__le16 operation_mode;
	__le16 stbc_param;
	u8 basic_set[16];
} __packed;

/* for ht_param */
#define IEEE80211_HT_PARAM_CHA_SEC_OFFSET		0x03
#define		IEEE80211_HT_PARAM_CHA_SEC_NONE		0x00
#define		IEEE80211_HT_PARAM_CHA_SEC_ABOVE	0x01
#define		IEEE80211_HT_PARAM_CHA_SEC_BELOW	0x03
#define IEEE80211_HT_PARAM_CHAN_WIDTH_ANY		0x04
#define IEEE80211_HT_PARAM_RIFS_MODE			0x08

/* for operation_mode */
#define IEEE80211_HT_OP_MODE_PROTECTION			0x0003
#define		IEEE80211_HT_OP_MODE_PROTECTION_NONE		0
#define		IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER	1
#define		IEEE80211_HT_OP_MODE_PROTECTION_20MHZ		2
#define		IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED	3
#define IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT		0x0004
#define IEEE80211_HT_OP_MODE_NON_HT_STA_PRSNT		0x0010
#define IEEE80211_HT_OP_MODE_CCFS2_SHIFT		5
#define IEEE80211_HT_OP_MODE_CCFS2_MASK			0x1fe0

/* for stbc_param */
#define IEEE80211_HT_STBC_PARAM_DUAL_BEACON		0x0040
#define IEEE80211_HT_STBC_PARAM_DUAL_CTS_PROT		0x0080
#define IEEE80211_HT_STBC_PARAM_STBC_BEACON		0x0100
#define IEEE80211_HT_STBC_PARAM_LSIG_TXOP_FULLPROT	0x0200
#define IEEE80211_HT_STBC_PARAM_PCO_ACTIVE		0x0400
#define IEEE80211_HT_STBC_PARAM_PCO_PHASE		0x0800


/* block-ack parameters */
#define IEEE80211_ADDBA_PARAM_AMSDU_MASK 0x0001
#define IEEE80211_ADDBA_PARAM_POLICY_MASK 0x0002
#define IEEE80211_ADDBA_PARAM_TID_MASK 0x003C
#define IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK 0xFFC0
#define IEEE80211_DELBA_PARAM_TID_MASK 0xF000
#define IEEE80211_DELBA_PARAM_INITIATOR_MASK 0x0800

/*
 * A-MPDU buffer sizes
 * According to HT size varies from 8 to 64 frames
 * HE adds the ability to have up to 256 frames.
 * EHT adds the ability to have up to 1K frames.
 */
#define IEEE80211_MIN_AMPDU_BUF		0x8
#define IEEE80211_MAX_AMPDU_BUF_HT	0x40
#define IEEE80211_MAX_AMPDU_BUF_HE	0x100
#define IEEE80211_MAX_AMPDU_BUF_EHT	0x400


/* Spatial Multiplexing Power Save Modes (for capability) */
#define WLAN_HT_CAP_SM_PS_STATIC	0
#define WLAN_HT_CAP_SM_PS_DYNAMIC	1
#define WLAN_HT_CAP_SM_PS_INVALID	2
#define WLAN_HT_CAP_SM_PS_DISABLED	3

/* for SM power control field lower two bits */
#define WLAN_HT_SMPS_CONTROL_DISABLED	0
#define WLAN_HT_SMPS_CONTROL_STATIC	1
#define WLAN_HT_SMPS_CONTROL_DYNAMIC	3

/**
 * struct ieee80211_vht_mcs_info - VHT MCS information
 * @rx_mcs_map: RX MCS map 2 bits for each stream, total 8 streams
 * @rx_highest: Indicates highest long GI VHT PPDU data rate
 *	STA can receive. Rate expressed in units of 1 Mbps.
 *	If this field is 0 this value should not be used to
 *	consider the highest RX data rate supported.
 *	The top 3 bits of this field indicate the Maximum NSTS,total
 *	(a beamformee capability.)
 * @tx_mcs_map: TX MCS map 2 bits for each stream, total 8 streams
 * @tx_highest: Indicates highest long GI VHT PPDU data rate
 *	STA can transmit. Rate expressed in units of 1 Mbps.
 *	If this field is 0 this value should not be used to
 *	consider the highest TX data rate supported.
 *	The top 2 bits of this field are reserved, the
 *	3rd bit from the top indiciates VHT Extended NSS BW
 *	Capability.
 */
struct ieee80211_vht_mcs_info {
	__le16 rx_mcs_map;
	__le16 rx_highest;
	__le16 tx_mcs_map;
	__le16 tx_highest;
} __packed;

/* for rx_highest */
#define IEEE80211_VHT_MAX_NSTS_TOTAL_SHIFT	13
#define IEEE80211_VHT_MAX_NSTS_TOTAL_MASK	(7 << IEEE80211_VHT_MAX_NSTS_TOTAL_SHIFT)

/* for tx_highest */
#define IEEE80211_VHT_EXT_NSS_BW_CAPABLE	(1 << 13)

/**
 * enum ieee80211_vht_mcs_support - VHT MCS support definitions
 * @IEEE80211_VHT_MCS_SUPPORT_0_7: MCSes 0-7 are supported for the
 *	number of streams
 * @IEEE80211_VHT_MCS_SUPPORT_0_8: MCSes 0-8 are supported
 * @IEEE80211_VHT_MCS_SUPPORT_0_9: MCSes 0-9 are supported
 * @IEEE80211_VHT_MCS_NOT_SUPPORTED: This number of streams isn't supported
 *
 * These definitions are used in each 2-bit subfield of the @rx_mcs_map
 * and @tx_mcs_map fields of &struct ieee80211_vht_mcs_info, which are
 * both split into 8 subfields by number of streams. These values indicate
 * which MCSes are supported for the number of streams the value appears
 * for.
 */
enum ieee80211_vht_mcs_support {
	IEEE80211_VHT_MCS_SUPPORT_0_7	= 0,
	IEEE80211_VHT_MCS_SUPPORT_0_8	= 1,
	IEEE80211_VHT_MCS_SUPPORT_0_9	= 2,
	IEEE80211_VHT_MCS_NOT_SUPPORTED	= 3,
};

/**
 * struct ieee80211_vht_cap - VHT capabilities
 *
 * This structure is the "VHT capabilities element" as
 * described in 802.11ac D3.0 8.4.2.160
 * @vht_cap_info: VHT capability info
 * @supp_mcs: VHT MCS supported rates
 */
struct ieee80211_vht_cap {
	__le32 vht_cap_info;
	struct ieee80211_vht_mcs_info supp_mcs;
} __packed;

/**
 * enum ieee80211_vht_chanwidth - VHT channel width
 * @IEEE80211_VHT_CHANWIDTH_USE_HT: use the HT operation IE to
 *	determine the channel width (20 or 40 MHz)
 * @IEEE80211_VHT_CHANWIDTH_80MHZ: 80 MHz bandwidth
 * @IEEE80211_VHT_CHANWIDTH_160MHZ: 160 MHz bandwidth
 * @IEEE80211_VHT_CHANWIDTH_80P80MHZ: 80+80 MHz bandwidth
 */
enum ieee80211_vht_chanwidth {
	IEEE80211_VHT_CHANWIDTH_USE_HT		= 0,
	IEEE80211_VHT_CHANWIDTH_80MHZ		= 1,
	IEEE80211_VHT_CHANWIDTH_160MHZ		= 2,
	IEEE80211_VHT_CHANWIDTH_80P80MHZ	= 3,
};

/**
 * struct ieee80211_vht_operation - VHT operation IE
 *
 * This structure is the "VHT operation element" as
 * described in 802.11ac D3.0 8.4.2.161
 * @chan_width: Operating channel width
 * @center_freq_seg0_idx: center freq segment 0 index
 * @center_freq_seg1_idx: center freq segment 1 index
 * @basic_mcs_set: VHT Basic MCS rate set
 */
struct ieee80211_vht_operation {
	u8 chan_width;
	u8 center_freq_seg0_idx;
	u8 center_freq_seg1_idx;
	__le16 basic_mcs_set;
} __packed;

/**
 * struct ieee80211_he_cap_elem - HE capabilities element
 * @mac_cap_info: HE MAC Capabilities Information
 * @phy_cap_info: HE PHY Capabilities Information
 *
 * This structure represents the fixed fields of the payload of the
 * "HE capabilities element" as described in IEEE Std 802.11ax-2021
 * sections 9.4.2.248.2 and 9.4.2.248.3.
 */
struct ieee80211_he_cap_elem {
	u8 mac_cap_info[6];
	u8 phy_cap_info[11];
} __packed;

#define IEEE80211_TX_RX_MCS_NSS_DESC_MAX_LEN	5

/**
 * enum ieee80211_he_mcs_support - HE MCS support definitions
 * @IEEE80211_HE_MCS_SUPPORT_0_7: MCSes 0-7 are supported for the
 *	number of streams
 * @IEEE80211_HE_MCS_SUPPORT_0_9: MCSes 0-9 are supported
 * @IEEE80211_HE_MCS_SUPPORT_0_11: MCSes 0-11 are supported
 * @IEEE80211_HE_MCS_NOT_SUPPORTED: This number of streams isn't supported
 *
 * These definitions are used in each 2-bit subfield of the rx_mcs_*
 * and tx_mcs_* fields of &struct ieee80211_he_mcs_nss_supp, which are
 * both split into 8 subfields by number of streams. These values indicate
 * which MCSes are supported for the number of streams the value appears
 * for.
 */
enum ieee80211_he_mcs_support {
	IEEE80211_HE_MCS_SUPPORT_0_7	= 0,
	IEEE80211_HE_MCS_SUPPORT_0_9	= 1,
	IEEE80211_HE_MCS_SUPPORT_0_11	= 2,
	IEEE80211_HE_MCS_NOT_SUPPORTED	= 3,
};

/**
 * struct ieee80211_he_mcs_nss_supp - HE Tx/Rx HE MCS NSS Support Field
 *
 * This structure holds the data required for the Tx/Rx HE MCS NSS Support Field
 * described in P802.11ax_D2.0 section 9.4.2.237.4
 *
 * @rx_mcs_80: Rx MCS map 2 bits for each stream, total 8 streams, for channel
 *     widths less than 80MHz.
 * @tx_mcs_80: Tx MCS map 2 bits for each stream, total 8 streams, for channel
 *     widths less than 80MHz.
 * @rx_mcs_160: Rx MCS map 2 bits for each stream, total 8 streams, for channel
 *     width 160MHz.
 * @tx_mcs_160: Tx MCS map 2 bits for each stream, total 8 streams, for channel
 *     width 160MHz.
 * @rx_mcs_80p80: Rx MCS map 2 bits for each stream, total 8 streams, for
 *     channel width 80p80MHz.
 * @tx_mcs_80p80: Tx MCS map 2 bits for each stream, total 8 streams, for
 *     channel width 80p80MHz.
 */
struct ieee80211_he_mcs_nss_supp {
	__le16 rx_mcs_80;
	__le16 tx_mcs_80;
	__le16 rx_mcs_160;
	__le16 tx_mcs_160;
	__le16 rx_mcs_80p80;
	__le16 tx_mcs_80p80;
} __packed;

/**
 * struct ieee80211_he_operation - HE Operation element
 * @he_oper_params: HE Operation Parameters + BSS Color Information
 * @he_mcs_nss_set: Basic HE-MCS And NSS Set
 * @optional: Optional fields VHT Operation Information, Max Co-Hosted
 *            BSSID Indicator, and 6 GHz Operation Information
 *
 * This structure represents the payload of the "HE Operation
 * element" as described in IEEE Std 802.11ax-2021 section 9.4.2.249.
 */
struct ieee80211_he_operation {
	__le32 he_oper_params;
	__le16 he_mcs_nss_set;
	u8 optional[];
} __packed;

/**
 * struct ieee80211_he_spr - Spatial Reuse Parameter Set element
 * @he_sr_control: SR Control
 * @optional: Optional fields Non-SRG OBSS PD Max Offset, SRG OBSS PD
 *            Min Offset, SRG OBSS PD Max Offset, SRG BSS Color
 *            Bitmap, and SRG Partial BSSID Bitmap
 *
 * This structure represents the payload of the "Spatial Reuse
 * Parameter Set element" as described in IEEE Std 802.11ax-2021
 * section 9.4.2.252.
 */
struct ieee80211_he_spr {
	u8 he_sr_control;
	u8 optional[];
} __packed;

/**
 * struct ieee80211_he_mu_edca_param_ac_rec - MU AC Parameter Record field
 * @aifsn: ACI/AIFSN
 * @ecw_min_max: ECWmin/ECWmax
 * @mu_edca_timer: MU EDCA Timer
 *
 * This structure represents the "MU AC Parameter Record" as described
 * in IEEE Std 802.11ax-2021 section 9.4.2.251, Figure 9-788p.
 */
struct ieee80211_he_mu_edca_param_ac_rec {
	u8 aifsn;
	u8 ecw_min_max;
	u8 mu_edca_timer;
} __packed;

/**
 * struct ieee80211_mu_edca_param_set - MU EDCA Parameter Set element
 * @mu_qos_info: QoS Info
 * @ac_be: MU AC_BE Parameter Record
 * @ac_bk: MU AC_BK Parameter Record
 * @ac_vi: MU AC_VI Parameter Record
 * @ac_vo: MU AC_VO Parameter Record
 *
 * This structure represents the payload of the "MU EDCA Parameter Set
 * element" as described in IEEE Std 802.11ax-2021 section 9.4.2.251.
 */
struct ieee80211_mu_edca_param_set {
	u8 mu_qos_info;
	struct ieee80211_he_mu_edca_param_ac_rec ac_be;
	struct ieee80211_he_mu_edca_param_ac_rec ac_bk;
	struct ieee80211_he_mu_edca_param_ac_rec ac_vi;
	struct ieee80211_he_mu_edca_param_ac_rec ac_vo;
} __packed;

#define IEEE80211_EHT_MCS_NSS_RX 0x0f
#define IEEE80211_EHT_MCS_NSS_TX 0xf0

/**
 * struct ieee80211_eht_mcs_nss_supp_20mhz_only - EHT 20MHz only station max
 * supported NSS for per MCS.
 *
 * For each field below, bits 0 - 3 indicate the maximal number of spatial
 * streams for Rx, and bits 4 - 7 indicate the maximal number of spatial streams
 * for Tx.
 *
 * @rx_tx_mcs7_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 0 - 7.
 * @rx_tx_mcs9_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 8 - 9.
 * @rx_tx_mcs11_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 10 - 11.
 * @rx_tx_mcs13_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 12 - 13.
 * @rx_tx_max_nss: array of the previous fields for easier loop access
 */
struct ieee80211_eht_mcs_nss_supp_20mhz_only {
	union {
		struct {
			u8 rx_tx_mcs7_max_nss;
			u8 rx_tx_mcs9_max_nss;
			u8 rx_tx_mcs11_max_nss;
			u8 rx_tx_mcs13_max_nss;
		};
		u8 rx_tx_max_nss[4];
	};
};

/**
 * struct ieee80211_eht_mcs_nss_supp_bw - EHT max supported NSS per MCS (except
 * 20MHz only stations).
 *
 * For each field below, bits 0 - 3 indicate the maximal number of spatial
 * streams for Rx, and bits 4 - 7 indicate the maximal number of spatial streams
 * for Tx.
 *
 * @rx_tx_mcs9_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 0 - 9.
 * @rx_tx_mcs11_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 10 - 11.
 * @rx_tx_mcs13_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 12 - 13.
 * @rx_tx_max_nss: array of the previous fields for easier loop access
 */
struct ieee80211_eht_mcs_nss_supp_bw {
	union {
		struct {
			u8 rx_tx_mcs9_max_nss;
			u8 rx_tx_mcs11_max_nss;
			u8 rx_tx_mcs13_max_nss;
		};
		u8 rx_tx_max_nss[3];
	};
};

/**
 * struct ieee80211_eht_cap_elem_fixed - EHT capabilities fixed data
 *
 * This structure is the "EHT Capabilities element" fixed fields as
 * described in P802.11be_D2.0 section 9.4.2.313.
 *
 * @mac_cap_info: MAC capabilities, see IEEE80211_EHT_MAC_CAP*
 * @phy_cap_info: PHY capabilities, see IEEE80211_EHT_PHY_CAP*
 */
struct ieee80211_eht_cap_elem_fixed {
	u8 mac_cap_info[2];
	u8 phy_cap_info[9];
} __packed;

/**
 * struct ieee80211_eht_cap_elem - EHT capabilities element
 * @fixed: fixed parts, see &ieee80211_eht_cap_elem_fixed
 * @optional: optional parts
 */
struct ieee80211_eht_cap_elem {
	struct ieee80211_eht_cap_elem_fixed fixed;

	/*
	 * Followed by:
	 * Supported EHT-MCS And NSS Set field: 4, 3, 6 or 9 octets.
	 * EHT PPE Thresholds field: variable length.
	 */
	u8 optional[];
} __packed;

#define IEEE80211_EHT_OPER_INFO_PRESENT	                        0x01
#define IEEE80211_EHT_OPER_DISABLED_SUBCHANNEL_BITMAP_PRESENT	0x02
#define IEEE80211_EHT_OPER_EHT_DEF_PE_DURATION	                0x04
#define IEEE80211_EHT_OPER_GROUP_ADDRESSED_BU_IND_LIMIT         0x08
#define IEEE80211_EHT_OPER_GROUP_ADDRESSED_BU_IND_EXP_MASK      0x30

/**
 * struct ieee80211_eht_operation - eht operation element
 *
 * This structure is the "EHT Operation Element" fields as
 * described in P802.11be_D2.0 section 9.4.2.311
 *
 * @params: EHT operation element parameters. See &IEEE80211_EHT_OPER_*
 * @basic_mcs_nss: indicates the EHT-MCSs for each number of spatial streams in
 *     EHT PPDUs that are supported by all EHT STAs in the BSS in transmit and
 *     receive.
 * @optional: optional parts
 */
struct ieee80211_eht_operation {
	u8 params;
	struct ieee80211_eht_mcs_nss_supp_20mhz_only basic_mcs_nss;
	u8 optional[];
} __packed;

/**
 * struct ieee80211_eht_operation_info - eht operation information
 *
 * @control: EHT operation information control.
 * @ccfs0: defines a channel center frequency for a 20, 40, 80, 160, or 320 MHz
 *     EHT BSS.
 * @ccfs1: defines a channel center frequency for a 160 or 320 MHz EHT BSS.
 * @optional: optional parts
 */
struct ieee80211_eht_operation_info {
	u8 control;
	u8 ccfs0;
	u8 ccfs1;
	u8 optional[];
} __packed;

/* 802.11ac VHT Capabilities */
#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895			0x00000000
#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991			0x00000001
#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454			0x00000002
#define IEEE80211_VHT_CAP_MAX_MPDU_MASK				0x00000003
#define IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ		0x00000004
#define IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ	0x00000008
#define IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK			0x0000000C
#define IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_SHIFT			2
#define IEEE80211_VHT_CAP_RXLDPC				0x00000010
#define IEEE80211_VHT_CAP_SHORT_GI_80				0x00000020
#define IEEE80211_VHT_CAP_SHORT_GI_160				0x00000040
#define IEEE80211_VHT_CAP_TXSTBC				0x00000080
#define IEEE80211_VHT_CAP_RXSTBC_1				0x00000100
#define IEEE80211_VHT_CAP_RXSTBC_2				0x00000200
#define IEEE80211_VHT_CAP_RXSTBC_3				0x00000300
#define IEEE80211_VHT_CAP_RXSTBC_4				0x00000400
#define IEEE80211_VHT_CAP_RXSTBC_MASK				0x00000700
#define IEEE80211_VHT_CAP_RXSTBC_SHIFT				8
#define IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE			0x00000800
#define IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE			0x00001000
#define IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT                  13
#define IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK			\
		(7 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT)
#define IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT		16
#define IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK		\
		(7 << IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT)
#define IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE			0x00080000
#define IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE			0x00100000
#define IEEE80211_VHT_CAP_VHT_TXOP_PS				0x00200000
#define IEEE80211_VHT_CAP_HTC_VHT				0x00400000
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT	23
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK	\
		(7 << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT)
#define IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_UNSOL_MFB	0x08000000
#define IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB	0x0c000000
#define IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN			0x10000000
#define IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN			0x20000000
#define IEEE80211_VHT_CAP_EXT_NSS_BW_SHIFT			30
#define IEEE80211_VHT_CAP_EXT_NSS_BW_MASK			0xc0000000

/**
 * ieee80211_get_vht_max_nss - return max NSS for a given bandwidth/MCS
 * @cap: VHT capabilities of the peer
 * @bw: bandwidth to use
 * @mcs: MCS index to use
 * @ext_nss_bw_capable: indicates whether or not the local transmitter
 *	(rate scaling algorithm) can deal with the new logic
 *	(dot11VHTExtendedNSSBWCapable)
 * @max_vht_nss: current maximum NSS as advertised by the STA in
 *	operating mode notification, can be 0 in which case the
 *	capability data will be used to derive this (from MCS support)
 *
 * Due to the VHT Extended NSS Bandwidth Support, the maximum NSS can
 * vary for a given BW/MCS. This function parses the data.
 *
 * Note: This function is exported by cfg80211.
 */
int ieee80211_get_vht_max_nss(struct ieee80211_vht_cap *cap,
			      enum ieee80211_vht_chanwidth bw,
			      int mcs, bool ext_nss_bw_capable,
			      unsigned int max_vht_nss);

/**
 * enum ieee80211_ap_reg_power - regulatory power for a Access Point
 *
 * @IEEE80211_REG_UNSET_AP: Access Point has no regulatory power mode
 * @IEEE80211_REG_LPI_AP: Indoor Access Point
 * @IEEE80211_REG_SP_AP: Standard power Access Point
 * @IEEE80211_REG_VLP_AP: Very low power Access Point
 * @IEEE80211_REG_AP_POWER_AFTER_LAST: internal
 * @IEEE80211_REG_AP_POWER_MAX: maximum value
 */
enum ieee80211_ap_reg_power {
	IEEE80211_REG_UNSET_AP,
	IEEE80211_REG_LPI_AP,
	IEEE80211_REG_SP_AP,
	IEEE80211_REG_VLP_AP,
	IEEE80211_REG_AP_POWER_AFTER_LAST,
	IEEE80211_REG_AP_POWER_MAX =
		IEEE80211_REG_AP_POWER_AFTER_LAST - 1,
};

/**
 * enum ieee80211_client_reg_power - regulatory power for a client
 *
 * @IEEE80211_REG_UNSET_CLIENT: Client has no regulatory power mode
 * @IEEE80211_REG_DEFAULT_CLIENT: Default Client
 * @IEEE80211_REG_SUBORDINATE_CLIENT: Subordinate Client
 * @IEEE80211_REG_CLIENT_POWER_AFTER_LAST: internal
 * @IEEE80211_REG_CLIENT_POWER_MAX: maximum value
 */
enum ieee80211_client_reg_power {
	IEEE80211_REG_UNSET_CLIENT,
	IEEE80211_REG_DEFAULT_CLIENT,
	IEEE80211_REG_SUBORDINATE_CLIENT,
	IEEE80211_REG_CLIENT_POWER_AFTER_LAST,
	IEEE80211_REG_CLIENT_POWER_MAX =
		IEEE80211_REG_CLIENT_POWER_AFTER_LAST - 1,
};

/* 802.11ax HE MAC capabilities */
#define IEEE80211_HE_MAC_CAP0_HTC_HE				0x01
#define IEEE80211_HE_MAC_CAP0_TWT_REQ				0x02
#define IEEE80211_HE_MAC_CAP0_TWT_RES				0x04
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_NOT_SUPP		0x00
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_1		0x08
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_2		0x10
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_3		0x18
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_MASK			0x18
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_1		0x00
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_2		0x20
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_4		0x40
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_8		0x60
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_16		0x80
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_32		0xa0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_64		0xc0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_UNLIMITED	0xe0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_MASK		0xe0

#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_UNLIMITED		0x00
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_128			0x01
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_256			0x02
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_512			0x03
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_MASK		0x03
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_0US		0x00
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_8US		0x04
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US		0x08
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_MASK		0x0c
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_1		0x00
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_2		0x10
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_3		0x20
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_4		0x30
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_5		0x40
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_6		0x50
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_7		0x60
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8		0x70
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_MASK		0x70

/* Link adaptation is split between byte HE_MAC_CAP1 and
 * HE_MAC_CAP2. It should be set only if IEEE80211_HE_MAC_CAP0_HTC_HE
 * in which case the following values apply:
 * 0 = No feedback.
 * 1 = reserved.
 * 2 = Unsolicited feedback.
 * 3 = both
 */
#define IEEE80211_HE_MAC_CAP1_LINK_ADAPTATION			0x80

#define IEEE80211_HE_MAC_CAP2_LINK_ADAPTATION			0x01
#define IEEE80211_HE_MAC_CAP2_ALL_ACK				0x02
#define IEEE80211_HE_MAC_CAP2_TRS				0x04
#define IEEE80211_HE_MAC_CAP2_BSR				0x08
#define IEEE80211_HE_MAC_CAP2_BCAST_TWT				0x10
#define IEEE80211_HE_MAC_CAP2_32BIT_BA_BITMAP			0x20
#define IEEE80211_HE_MAC_CAP2_MU_CASCADING			0x40
#define IEEE80211_HE_MAC_CAP2_ACK_EN				0x80

#define IEEE80211_HE_MAC_CAP3_OMI_CONTROL			0x02
#define IEEE80211_HE_MAC_CAP3_OFDMA_RA				0x04

/* The maximum length of an A-MDPU is defined by the combination of the Maximum
 * A-MDPU Length Exponent field in the HT capabilities, VHT capabilities and the
 * same field in the HE capabilities.
 */
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_0		0x00
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_1		0x08
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_2		0x10
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3		0x18
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK		0x18
#define IEEE80211_HE_MAC_CAP3_AMSDU_FRAG			0x20
#define IEEE80211_HE_MAC_CAP3_FLEX_TWT_SCHED			0x40
#define IEEE80211_HE_MAC_CAP3_RX_CTRL_FRAME_TO_MULTIBSS		0x80

#define IEEE80211_HE_MAC_CAP4_BSRP_BQRP_A_MPDU_AGG		0x01
#define IEEE80211_HE_MAC_CAP4_QTP				0x02
#define IEEE80211_HE_MAC_CAP4_BQR				0x04
#define IEEE80211_HE_MAC_CAP4_PSR_RESP				0x08
#define IEEE80211_HE_MAC_CAP4_NDP_FB_REP			0x10
#define IEEE80211_HE_MAC_CAP4_OPS				0x20
#define IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU			0x40
/* Multi TID agg TX is split between byte #4 and #5
 * The value is a combination of B39,B40,B41
 */
#define IEEE80211_HE_MAC_CAP4_MULTI_TID_AGG_TX_QOS_B39		0x80

#define IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B40		0x01
#define IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B41		0x02
#define IEEE80211_HE_MAC_CAP5_SUBCHAN_SELECTIVE_TRANSMISSION	0x04
#define IEEE80211_HE_MAC_CAP5_UL_2x996_TONE_RU			0x08
#define IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX		0x10
#define IEEE80211_HE_MAC_CAP5_HE_DYNAMIC_SM_PS			0x20
#define IEEE80211_HE_MAC_CAP5_PUNCTURED_SOUNDING		0x40
#define IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX		0x80

#define IEEE80211_HE_VHT_MAX_AMPDU_FACTOR	20
#define IEEE80211_HE_HT_MAX_AMPDU_FACTOR	16
#define IEEE80211_HE_6GHZ_MAX_AMPDU_FACTOR	13

/* 802.11ax HE PHY capabilities */
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G		0x02
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G	0x04
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G		0x08
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G	0x10
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK_ALL		0x1e

#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G	0x20
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G	0x40
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK			0xfe

#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_20MHZ	0x01
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_40MHZ	0x02
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_160MHZ_ONLY_SECOND_20MHZ	0x04
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_160MHZ_ONLY_SECOND_40MHZ	0x08
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK			0x0f
#define IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A				0x10
#define IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD			0x20
#define IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US		0x40
/* Midamble RX/TX Max NSTS is split between byte #2 and byte #3 */
#define IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS			0x80

#define IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS			0x01
#define IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US			0x02
#define IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ			0x04
#define IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ			0x08
#define IEEE80211_HE_PHY_CAP2_DOPPLER_TX				0x10
#define IEEE80211_HE_PHY_CAP2_DOPPLER_RX				0x20

/* Note that the meaning of UL MU below is different between an AP and a non-AP
 * sta, where in the AP case it indicates support for Rx and in the non-AP sta
 * case it indicates support for Tx.
 */
#define IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO			0x40
#define IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO			0x80

#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_NO_DCM			0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK			0x01
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK			0x02
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM			0x03
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK			0x03
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1				0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_2				0x04
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_NO_DCM			0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK			0x08
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK			0x10
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM			0x18
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_MASK			0x18
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1				0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_2				0x20
#define IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU		0x40
#define IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER				0x80

#define IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE				0x01
#define IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER				0x02

/* Minimal allowed value of Max STS under 80MHz is 3 */
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4		0x0c
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_5		0x10
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_6		0x14
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_7		0x18
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_8		0x1c
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_MASK	0x1c

/* Minimal allowed value of Max STS above 80MHz is 3 */
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4		0x60
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_5		0x80
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_6		0xa0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_7		0xc0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_8		0xe0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_MASK	0xe0

#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_1	0x00
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_2	0x01
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_3	0x02
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_4	0x03
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_5	0x04
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_6	0x05
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_7	0x06
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_8	0x07
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK	0x07

#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_1	0x00
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_2	0x08
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_3	0x10
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_4	0x18
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_5	0x20
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_6	0x28
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_7	0x30
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_8	0x38
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK	0x38

#define IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK				0x40
#define IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK				0x80

#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU			0x01
#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU			0x02
#define IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB			0x04
#define IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB		0x08
#define IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB				0x10
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE			0x20
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO		0x40
#define IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT			0x80

#define IEEE80211_HE_PHY_CAP7_PSR_BASED_SR				0x01
#define IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP			0x02
#define IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI		0x04
#define IEEE80211_HE_PHY_CAP7_MAX_NC_1					0x08
#define IEEE80211_HE_PHY_CAP7_MAX_NC_2					0x10
#define IEEE80211_HE_PHY_CAP7_MAX_NC_3					0x18
#define IEEE80211_HE_PHY_CAP7_MAX_NC_4					0x20
#define IEEE80211_HE_PHY_CAP7_MAX_NC_5					0x28
#define IEEE80211_HE_PHY_CAP7_MAX_NC_6					0x30
#define IEEE80211_HE_PHY_CAP7_MAX_NC_7					0x38
#define IEEE80211_HE_PHY_CAP7_MAX_NC_MASK				0x38
#define IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ			0x40
#define IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ			0x80

#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI		0x01
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G		0x02
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU			0x04
#define IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU			0x08
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI		0x10
#define IEEE80211_HE_PHY_CAP8_MIDAMBLE_RX_TX_2X_AND_1XLTF		0x20
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242				0x00
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484				0x40
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996				0x80
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_2x996				0xc0
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_MASK				0xc0

#define IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM		0x01
#define IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK		0x02
#define IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU		0x04
#define IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU		0x08
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB	0x10
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB	0x20
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_0US			0x0
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_8US			0x1
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US			0x2
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_RESERVED		0x3
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_POS			6
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK			0xc0

#define IEEE80211_HE_PHY_CAP10_HE_MU_M1RU_MAX_LTF			0x01

/* 802.11ax HE TX/RX MCS NSS Support  */
#define IEEE80211_TX_RX_MCS_NSS_SUPP_HIGHEST_MCS_POS			(3)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_TX_BITMAP_POS			(6)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_RX_BITMAP_POS			(11)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_TX_BITMAP_MASK			0x07c0
#define IEEE80211_TX_RX_MCS_NSS_SUPP_RX_BITMAP_MASK			0xf800

/* TX/RX HE MCS Support field Highest MCS subfield encoding */
enum ieee80211_he_highest_mcs_supported_subfield_enc {
	HIGHEST_MCS_SUPPORTED_MCS7 = 0,
	HIGHEST_MCS_SUPPORTED_MCS8,
	HIGHEST_MCS_SUPPORTED_MCS9,
	HIGHEST_MCS_SUPPORTED_MCS10,
	HIGHEST_MCS_SUPPORTED_MCS11,
};

/* Calculate 802.11ax HE capabilities IE Tx/Rx HE MCS NSS Support Field size */
static inline u8
ieee80211_he_mcs_nss_size(const struct ieee80211_he_cap_elem *he_cap)
{
	u8 count = 4;

	if (he_cap->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G)
		count += 4;

	if (he_cap->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
		count += 4;

	return count;
}

/* 802.11ax HE PPE Thresholds */
#define IEEE80211_PPE_THRES_NSS_SUPPORT_2NSS			(1)
#define IEEE80211_PPE_THRES_NSS_POS				(0)
#define IEEE80211_PPE_THRES_NSS_MASK				(7)
#define IEEE80211_PPE_THRES_RU_INDEX_BITMASK_2x966_AND_966_RU	\
	(BIT(5) | BIT(6))
#define IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK		0x78
#define IEEE80211_PPE_THRES_RU_INDEX_BITMASK_POS		(3)
#define IEEE80211_PPE_THRES_INFO_PPET_SIZE			(3)
#define IEEE80211_HE_PPE_THRES_INFO_HEADER_SIZE			(7)

/*
 * Calculate 802.11ax HE capabilities IE PPE field size
 * Input: Header byte of ppe_thres (first byte), and HE capa IE's PHY cap u8*
 */
static inline u8
ieee80211_he_ppe_size(u8 ppe_thres_hdr, const u8 *phy_cap_info)
{
	u8 n;

	if ((phy_cap_info[6] &
	     IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) == 0)
		return 0;

	n = hweight8(ppe_thres_hdr &
		     IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK);
	n *= (1 + ((ppe_thres_hdr & IEEE80211_PPE_THRES_NSS_MASK) >>
		   IEEE80211_PPE_THRES_NSS_POS));

	/*
	 * Each pair is 6 bits, and we need to add the 7 "header" bits to the
	 * total size.
	 */
	n = (n * IEEE80211_PPE_THRES_INFO_PPET_SIZE * 2) + 7;
	n = DIV_ROUND_UP(n, 8);

	return n;
}

static inline bool ieee80211_he_capa_size_ok(const u8 *data, u8 len)
{
	const struct ieee80211_he_cap_elem *he_cap_ie_elem = (const void *)data;
	u8 needed = sizeof(*he_cap_ie_elem);

	if (len < needed)
		return false;

	needed += ieee80211_he_mcs_nss_size(he_cap_ie_elem);
	if (len < needed)
		return false;

	if (he_cap_ie_elem->phy_cap_info[6] &
			IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
		if (len < needed + 1)
			return false;
		needed += ieee80211_he_ppe_size(data[needed],
						he_cap_ie_elem->phy_cap_info);
	}

	return len >= needed;
}

/* HE Operation defines */
#define IEEE80211_HE_OPERATION_DFLT_PE_DURATION_MASK		0x00000007
#define IEEE80211_HE_OPERATION_TWT_REQUIRED			0x00000008
#define IEEE80211_HE_OPERATION_RTS_THRESHOLD_MASK		0x00003ff0
#define IEEE80211_HE_OPERATION_RTS_THRESHOLD_OFFSET		4
#define IEEE80211_HE_OPERATION_VHT_OPER_INFO			0x00004000
#define IEEE80211_HE_OPERATION_CO_HOSTED_BSS			0x00008000
#define IEEE80211_HE_OPERATION_ER_SU_DISABLE			0x00010000
#define IEEE80211_HE_OPERATION_6GHZ_OP_INFO			0x00020000
#define IEEE80211_HE_OPERATION_BSS_COLOR_MASK			0x3f000000
#define IEEE80211_HE_OPERATION_BSS_COLOR_OFFSET			24
#define IEEE80211_HE_OPERATION_PARTIAL_BSS_COLOR		0x40000000
#define IEEE80211_HE_OPERATION_BSS_COLOR_DISABLED		0x80000000

#define IEEE80211_6GHZ_CTRL_REG_LPI_AP	0
#define IEEE80211_6GHZ_CTRL_REG_SP_AP	1

/**
 * struct ieee80211_he_6ghz_oper - HE 6 GHz operation Information field
 * @primary: primary channel
 * @control: control flags
 * @ccfs0: channel center frequency segment 0
 * @ccfs1: channel center frequency segment 1
 * @minrate: minimum rate (in 1 Mbps units)
 */
struct ieee80211_he_6ghz_oper {
	u8 primary;
#define IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH	0x3
#define		IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_20MHZ	0
#define		IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_40MHZ	1
#define		IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_80MHZ	2
#define		IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_160MHZ	3
#define IEEE80211_HE_6GHZ_OPER_CTRL_DUP_BEACON	0x4
#define IEEE80211_HE_6GHZ_OPER_CTRL_REG_INFO	0x38
	u8 control;
	u8 ccfs0;
	u8 ccfs1;
	u8 minrate;
} __packed;

/*
 * In "9.4.2.161 Transmit Power Envelope element" of "IEEE Std 802.11ax-2021",
 * it show four types in "Table 9-275a-Maximum Transmit Power Interpretation
 * subfield encoding", and two category for each type in "Table E-12-Regulatory
 * Info subfield encoding in the United States".
 * So it it totally max 8 Transmit Power Envelope element.
 */
#define IEEE80211_TPE_MAX_IE_COUNT	8
/*
 * In "Table 9-277—Meaning of Maximum Transmit Power Count subfield"
 * of "IEEE Std 802.11ax™‐2021", the max power level is 8.
 */
#define IEEE80211_MAX_NUM_PWR_LEVEL	8

#define IEEE80211_TPE_MAX_POWER_COUNT	8

/* transmit power interpretation type of transmit power envelope element */
enum ieee80211_tx_power_intrpt_type {
	IEEE80211_TPE_LOCAL_EIRP,
	IEEE80211_TPE_LOCAL_EIRP_PSD,
	IEEE80211_TPE_REG_CLIENT_EIRP,
	IEEE80211_TPE_REG_CLIENT_EIRP_PSD,
};

/**
 * struct ieee80211_tx_pwr_env - Transmit Power Envelope
 * @tx_power_info: Transmit Power Information field
 * @tx_power: Maximum Transmit Power field
 *
 * This structure represents the payload of the "Transmit Power
 * Envelope element" as described in IEEE Std 802.11ax-2021 section
 * 9.4.2.161
 */
struct ieee80211_tx_pwr_env {
	u8 tx_power_info;
	s8 tx_power[IEEE80211_TPE_MAX_POWER_COUNT];
} __packed;

#define IEEE80211_TX_PWR_ENV_INFO_COUNT 0x7
#define IEEE80211_TX_PWR_ENV_INFO_INTERPRET 0x38
#define IEEE80211_TX_PWR_ENV_INFO_CATEGORY 0xC0

/*
 * ieee80211_he_oper_size - calculate 802.11ax HE Operations IE size
 * @he_oper_ie: byte data of the He Operations IE, stating from the byte
 *	after the ext ID byte. It is assumed that he_oper_ie has at least
 *	sizeof(struct ieee80211_he_operation) bytes, the caller must have
 *	validated this.
 * @return the actual size of the IE data (not including header), or 0 on error
 */
static inline u8
ieee80211_he_oper_size(const u8 *he_oper_ie)
{
	const struct ieee80211_he_operation *he_oper = (const void *)he_oper_ie;
	u8 oper_len = sizeof(struct ieee80211_he_operation);
	u32 he_oper_params;

	/* Make sure the input is not NULL */
	if (!he_oper_ie)
		return 0;

	/* Calc required length */
	he_oper_params = le32_to_cpu(he_oper->he_oper_params);
	if (he_oper_params & IEEE80211_HE_OPERATION_VHT_OPER_INFO)
		oper_len += 3;
	if (he_oper_params & IEEE80211_HE_OPERATION_CO_HOSTED_BSS)
		oper_len++;
	if (he_oper_params & IEEE80211_HE_OPERATION_6GHZ_OP_INFO)
		oper_len += sizeof(struct ieee80211_he_6ghz_oper);

	/* Add the first byte (extension ID) to the total length */
	oper_len++;

	return oper_len;
}

/**
 * ieee80211_he_6ghz_oper - obtain 6 GHz operation field
 * @he_oper: HE operation element (must be pre-validated for size)
 *	but may be %NULL
 *
 * Return: a pointer to the 6 GHz operation field, or %NULL
 */
static inline const struct ieee80211_he_6ghz_oper *
ieee80211_he_6ghz_oper(const struct ieee80211_he_operation *he_oper)
{
	const u8 *ret = (const void *)&he_oper->optional;
	u32 he_oper_params;

	if (!he_oper)
		return NULL;

	he_oper_params = le32_to_cpu(he_oper->he_oper_params);

	if (!(he_oper_params & IEEE80211_HE_OPERATION_6GHZ_OP_INFO))
		return NULL;
	if (he_oper_params & IEEE80211_HE_OPERATION_VHT_OPER_INFO)
		ret += 3;
	if (he_oper_params & IEEE80211_HE_OPERATION_CO_HOSTED_BSS)
		ret++;

	return (const void *)ret;
}

/* HE Spatial Reuse defines */
#define IEEE80211_HE_SPR_PSR_DISALLOWED				BIT(0)
#define IEEE80211_HE_SPR_NON_SRG_OBSS_PD_SR_DISALLOWED		BIT(1)
#define IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT			BIT(2)
#define IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT		BIT(3)
#define IEEE80211_HE_SPR_HESIGA_SR_VAL15_ALLOWED		BIT(4)

/*
 * ieee80211_he_spr_size - calculate 802.11ax HE Spatial Reuse IE size
 * @he_spr_ie: byte data of the He Spatial Reuse IE, stating from the byte
 *	after the ext ID byte. It is assumed that he_spr_ie has at least
 *	sizeof(struct ieee80211_he_spr) bytes, the caller must have validated
 *	this
 * @return the actual size of the IE data (not including header), or 0 on error
 */
static inline u8
ieee80211_he_spr_size(const u8 *he_spr_ie)
{
	const struct ieee80211_he_spr *he_spr = (const void *)he_spr_ie;
	u8 spr_len = sizeof(struct ieee80211_he_spr);
	u8 he_spr_params;

	/* Make sure the input is not NULL */
	if (!he_spr_ie)
		return 0;

	/* Calc required length */
	he_spr_params = he_spr->he_sr_control;
	if (he_spr_params & IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT)
		spr_len++;
	if (he_spr_params & IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT)
		spr_len += 18;

	/* Add the first byte (extension ID) to the total length */
	spr_len++;

	return spr_len;
}

/* S1G Capabilities Information field */
#define IEEE80211_S1G_CAPABILITY_LEN	15

#define S1G_CAP0_S1G_LONG	BIT(0)
#define S1G_CAP0_SGI_1MHZ	BIT(1)
#define S1G_CAP0_SGI_2MHZ	BIT(2)
#define S1G_CAP0_SGI_4MHZ	BIT(3)
#define S1G_CAP0_SGI_8MHZ	BIT(4)
#define S1G_CAP0_SGI_16MHZ	BIT(5)
#define S1G_CAP0_SUPP_CH_WIDTH	GENMASK(7, 6)

#define S1G_SUPP_CH_WIDTH_2	0
#define S1G_SUPP_CH_WIDTH_4	1
#define S1G_SUPP_CH_WIDTH_8	2
#define S1G_SUPP_CH_WIDTH_16	3
#define S1G_SUPP_CH_WIDTH_MAX(cap) ((1 << FIELD_GET(S1G_CAP0_SUPP_CH_WIDTH, \
						    cap[0])) << 1)

#define S1G_CAP1_RX_LDPC	BIT(0)
#define S1G_CAP1_TX_STBC	BIT(1)
#define S1G_CAP1_RX_STBC	BIT(2)
#define S1G_CAP1_SU_BFER	BIT(3)
#define S1G_CAP1_SU_BFEE	BIT(4)
#define S1G_CAP1_BFEE_STS	GENMASK(7, 5)

#define S1G_CAP2_SOUNDING_DIMENSIONS	GENMASK(2, 0)
#define S1G_CAP2_MU_BFER		BIT(3)
#define S1G_CAP2_MU_BFEE		BIT(4)
#define S1G_CAP2_PLUS_HTC_VHT		BIT(5)
#define S1G_CAP2_TRAVELING_PILOT	GENMASK(7, 6)

#define S1G_CAP3_RD_RESPONDER		BIT(0)
#define S1G_CAP3_HT_DELAYED_BA		BIT(1)
#define S1G_CAP3_MAX_MPDU_LEN		BIT(2)
#define S1G_CAP3_MAX_AMPDU_LEN_EXP	GENMASK(4, 3)
#define S1G_CAP3_MIN_MPDU_START		GENMASK(7, 5)

#define S1G_CAP4_UPLINK_SYNC	BIT(0)
#define S1G_CAP4_DYNAMIC_AID	BIT(1)
#define S1G_CAP4_BAT		BIT(2)
#define S1G_CAP4_TIME_ADE	BIT(3)
#define S1G_CAP4_NON_TIM	BIT(4)
#define S1G_CAP4_GROUP_AID	BIT(5)
#define S1G_CAP4_STA_TYPE	GENMASK(7, 6)

#define S1G_CAP5_CENT_AUTH_CONTROL	BIT(0)
#define S1G_CAP5_DIST_AUTH_CONTROL	BIT(1)
#define S1G_CAP5_AMSDU			BIT(2)
#define S1G_CAP5_AMPDU			BIT(3)
#define S1G_CAP5_ASYMMETRIC_BA		BIT(4)
#define S1G_CAP5_FLOW_CONTROL		BIT(5)
#define S1G_CAP5_SECTORIZED_BEAM	GENMASK(7, 6)

#define S1G_CAP6_OBSS_MITIGATION	BIT(0)
#define S1G_CAP6_FRAGMENT_BA		BIT(1)
#define S1G_CAP6_NDP_PS_POLL		BIT(2)
#define S1G_CAP6_RAW_OPERATION		BIT(3)
#define S1G_CAP6_PAGE_SLICING		BIT(4)
#define S1G_CAP6_TXOP_SHARING_IMP_ACK	BIT(5)
#define S1G_CAP6_VHT_LINK_ADAPT		GENMASK(7, 6)

#define S1G_CAP7_TACK_AS_PS_POLL		BIT(0)
#define S1G_CAP7_DUP_1MHZ			BIT(1)
#define S1G_CAP7_MCS_NEGOTIATION		BIT(2)
#define S1G_CAP7_1MHZ_CTL_RESPONSE_PREAMBLE	BIT(3)
#define S1G_CAP7_NDP_BFING_REPORT_POLL		BIT(4)
#define S1G_CAP7_UNSOLICITED_DYN_AID		BIT(5)
#define S1G_CAP7_SECTOR_TRAINING_OPERATION	BIT(6)
#define S1G_CAP7_TEMP_PS_MODE_SWITCH		BIT(7)

#define S1G_CAP8_TWT_GROUPING	BIT(0)
#define S1G_CAP8_BDT		BIT(1)
#define S1G_CAP8_COLOR		GENMASK(4, 2)
#define S1G_CAP8_TWT_REQUEST	BIT(5)
#define S1G_CAP8_TWT_RESPOND	BIT(6)
#define S1G_CAP8_PV1_FRAME	BIT(7)

#define S1G_CAP9_LINK_ADAPT_PER_CONTROL_RESPONSE BIT(0)

#define S1G_OPER_CH_WIDTH_PRIMARY_1MHZ	BIT(0)
#define S1G_OPER_CH_WIDTH_OPER		GENMASK(4, 1)

/* EHT MAC capabilities as defined in P802.11be_D2.0 section 9.4.2.313.2 */
#define IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS			0x01
#define IEEE80211_EHT_MAC_CAP0_OM_CONTROL			0x02
#define IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1		0x04
#define IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE2		0x08
#define IEEE80211_EHT_MAC_CAP0_RESTRICTED_TWT			0x10
#define IEEE80211_EHT_MAC_CAP0_SCS_TRAFFIC_DESC			0x20
#define IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_MASK		0xc0
#define	IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_3895	        0
#define	IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_7991	        1
#define	IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_11454	        2

#define IEEE80211_EHT_MAC_CAP1_MAX_AMPDU_LEN_MASK		0x01

/* EHT PHY capabilities as defined in P802.11be_D2.0 section 9.4.2.313.3 */
#define IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ			0x02
#define IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ		0x04
#define IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI		0x08
#define IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO		0x10
#define IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER			0x20
#define IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE			0x40

/* EHT beamformee number of spatial streams <= 80MHz is split */
#define IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK		0x80
#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK		0x03

#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK	0x1c
#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_320MHZ_MASK	0xe0

#define IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_80MHZ_MASK		0x07
#define IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK		0x38

/* EHT number of sounding dimensions for 320MHz is split */
#define IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_320MHZ_MASK		0xc0
#define IEEE80211_EHT_PHY_CAP3_SOUNDING_DIM_320MHZ_MASK		0x01
#define IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK		0x02
#define IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK		0x04
#define IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK		0x08
#define IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK		0x10
#define IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK			0x20
#define IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK		0x40
#define IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK			0x80

#define IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO		0x01
#define IEEE80211_EHT_PHY_CAP4_PSR_SR_SUPP			0x02
#define IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP		0x04
#define IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI	0x08
#define IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK			0xf0

#define IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK		0x01
#define IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP		0x02
#define IEEE80211_EHT_PHY_CAP5_RX_LESS_242_TONE_RU_SUPP		0x04
#define IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT		0x08
#define IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK	0x30
#define   IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_0US	0
#define   IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_8US	1
#define   IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_16US	2
#define   IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_20US	3

/* Maximum number of supported EHT LTF is split */
#define IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK	0xc0
#define IEEE80211_EHT_PHY_CAP5_SUPP_EXTRA_EHT_LTF		0x40
#define IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK	0x07

#define IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK			0x78
#define IEEE80211_EHT_PHY_CAP6_EHT_DUP_6GHZ_SUPP		0x80

#define IEEE80211_EHT_PHY_CAP7_20MHZ_STA_RX_NDP_WIDER_BW	0x01
#define IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ	0x02
#define IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ	0x04
#define IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_320MHZ	0x08
#define IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ		0x10
#define IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ		0x20
#define IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_320MHZ		0x40
#define IEEE80211_EHT_PHY_CAP7_TB_SOUNDING_FDBK_RATE_LIMIT	0x80

#define IEEE80211_EHT_PHY_CAP8_RX_1024QAM_WIDER_BW_DL_OFDMA	0x01
#define IEEE80211_EHT_PHY_CAP8_RX_4096QAM_WIDER_BW_DL_OFDMA	0x02

/*
 * EHT operation channel width as defined in P802.11be_D2.0 section 9.4.2.311
 */
#define IEEE80211_EHT_OPER_CHAN_WIDTH		0x7
#define IEEE80211_EHT_OPER_CHAN_WIDTH_20MHZ	0
#define IEEE80211_EHT_OPER_CHAN_WIDTH_40MHZ	1
#define IEEE80211_EHT_OPER_CHAN_WIDTH_80MHZ	2
#define IEEE80211_EHT_OPER_CHAN_WIDTH_160MHZ	3
#define IEEE80211_EHT_OPER_CHAN_WIDTH_320MHZ	4

/* Calculate 802.11be EHT capabilities IE Tx/Rx EHT MCS NSS Support Field size */
static inline u8
ieee80211_eht_mcs_nss_size(const struct ieee80211_he_cap_elem *he_cap,
			   const struct ieee80211_eht_cap_elem_fixed *eht_cap,
			   bool from_ap)
{
	u8 count = 0;

	/* on 2.4 GHz, if it supports 40 MHz, the result is 3 */
	if (he_cap->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G)
		return 3;

	/* on 2.4 GHz, these three bits are reserved, so should be 0 */
	if (he_cap->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G)
		count += 3;

	if (he_cap->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G)
		count += 3;

	if (eht_cap->phy_cap_info[0] & IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ)
		count += 3;

	if (count)
		return count;

	return from_ap ? 3 : 4;
}

/* 802.11be EHT PPE Thresholds */
#define IEEE80211_EHT_PPE_THRES_NSS_POS			0
#define IEEE80211_EHT_PPE_THRES_NSS_MASK		0xf
#define IEEE80211_EHT_PPE_THRES_RU_INDEX_BITMASK_MASK	0x1f0
#define IEEE80211_EHT_PPE_THRES_INFO_PPET_SIZE		3
#define IEEE80211_EHT_PPE_THRES_INFO_HEADER_SIZE	9

/*
 * Calculate 802.11be EHT capabilities IE EHT field size
 */
static inline u8
ieee80211_eht_ppe_size(u16 ppe_thres_hdr, const u8 *phy_cap_info)
{
	u32 n;

	if (!(phy_cap_info[5] &
	      IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT))
		return 0;

	n = hweight16(ppe_thres_hdr &
		      IEEE80211_EHT_PPE_THRES_RU_INDEX_BITMASK_MASK);
	n *= 1 + u16_get_bits(ppe_thres_hdr, IEEE80211_EHT_PPE_THRES_NSS_MASK);

	/*
	 * Each pair is 6 bits, and we need to add the 9 "header" bits to the
	 * total size.
	 */
	n = n * IEEE80211_EHT_PPE_THRES_INFO_PPET_SIZE * 2 +
	    IEEE80211_EHT_PPE_THRES_INFO_HEADER_SIZE;
	return DIV_ROUND_UP(n, 8);
}

static inline bool
ieee80211_eht_capa_size_ok(const u8 *he_capa, const u8 *data, u8 len,
			   bool from_ap)
{
	const struct ieee80211_eht_cap_elem_fixed *elem = (const void *)data;
	u8 needed = sizeof(struct ieee80211_eht_cap_elem_fixed);

	if (len < needed || !he_capa)
		return false;

	needed += ieee80211_eht_mcs_nss_size((const void *)he_capa,
					     (const void *)data,
					     from_ap);
	if (len < needed)
		return false;

	if (elem->phy_cap_info[5] &
			IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT) {
		u16 ppe_thres_hdr;

		if (len < needed + sizeof(ppe_thres_hdr))
			return false;

		ppe_thres_hdr = get_unaligned_le16(data + needed);
		needed += ieee80211_eht_ppe_size(ppe_thres_hdr,
						 elem->phy_cap_info);
	}

	return len >= needed;
}

static inline bool
ieee80211_eht_oper_size_ok(const u8 *data, u8 len)
{
	const struct ieee80211_eht_operation *elem = (const void *)data;
	u8 needed = sizeof(*elem);

	if (len < needed)
		return false;

	if (elem->params & IEEE80211_EHT_OPER_INFO_PRESENT) {
		needed += 3;

		if (elem->params &
		    IEEE80211_EHT_OPER_DISABLED_SUBCHANNEL_BITMAP_PRESENT)
			needed += 2;
	}

	return len >= needed;
}

#define LISTEN_INT_USF	GENMASK(15, 14)
#define LISTEN_INT_UI	GENMASK(13, 0)

#define IEEE80211_MAX_USF	FIELD_MAX(LISTEN_INT_USF)
#define IEEE80211_MAX_UI	FIELD_MAX(LISTEN_INT_UI)

/* Authentication algorithms */
#define WLAN_AUTH_OPEN 0
#define WLAN_AUTH_SHARED_KEY 1
#define WLAN_AUTH_FT 2
#define WLAN_AUTH_SAE 3
#define WLAN_AUTH_FILS_SK 4
#define WLAN_AUTH_FILS_SK_PFS 5
#define WLAN_AUTH_FILS_PK 6
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
	WLAN_STATUS_FILS_AUTHENTICATION_FAILURE = 108,
	WLAN_STATUS_UNKNOWN_AUTHENTICATION_SERVER = 109,
	WLAN_STATUS_SAE_HASH_TO_ELEMENT = 126,
	WLAN_STATUS_SAE_PK = 127,
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

/* HT action codes */
enum ieee80211_ht_actioncode {
	WLAN_HT_ACTION_NOTIFY_CHANWIDTH = 0,
	WLAN_HT_ACTION_SMPS = 1,
	WLAN_HT_ACTION_PSMP = 2,
	WLAN_HT_ACTION_PCO_PHASE = 3,
	WLAN_HT_ACTION_CSI = 4,
	WLAN_HT_ACTION_NONCOMPRESSED_BF = 5,
	WLAN_HT_ACTION_COMPRESSED_BF = 6,
	WLAN_HT_ACTION_ASEL_IDX_FEEDBACK = 7,
};

/* VHT action codes */
enum ieee80211_vht_actioncode {
	WLAN_VHT_ACTION_COMPRESSED_BF = 0,
	WLAN_VHT_ACTION_GROUPID_MGMT = 1,
	WLAN_VHT_ACTION_OPMODE_NOTIF = 2,
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

/* Mesh action codes */
enum ieee80211_mesh_actioncode {
	WLAN_MESH_ACTION_LINK_METRIC_REPORT,
	WLAN_MESH_ACTION_HWMP_PATH_SELECTION,
	WLAN_MESH_ACTION_GATE_ANNOUNCEMENT,
	WLAN_MESH_ACTION_CONGESTION_CONTROL_NOTIFICATION,
	WLAN_MESH_ACTION_MCCA_SETUP_REQUEST,
	WLAN_MESH_ACTION_MCCA_SETUP_REPLY,
	WLAN_MESH_ACTION_MCCA_ADVERTISEMENT_REQUEST,
	WLAN_MESH_ACTION_MCCA_ADVERTISEMENT,
	WLAN_MESH_ACTION_MCCA_TEARDOWN,
	WLAN_MESH_ACTION_TBTT_ADJUSTMENT_REQUEST,
	WLAN_MESH_ACTION_TBTT_ADJUSTMENT_RESPONSE,
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

enum ieee80211_s1g_actioncode {
	WLAN_S1G_AID_SWITCH_REQUEST,
	WLAN_S1G_AID_SWITCH_RESPONSE,
	WLAN_S1G_SYNC_CONTROL,
	WLAN_S1G_STA_INFO_ANNOUNCE,
	WLAN_S1G_EDCA_PARAM_SET,
	WLAN_S1G_EL_OPERATION,
	WLAN_S1G_TWT_SETUP,
	WLAN_S1G_TWT_TEARDOWN,
	WLAN_S1G_SECT_GROUP_ID_LIST,
	WLAN_S1G_SECT_ID_FEEDBACK,
	WLAN_S1G_TWT_INFORMATION = 11,
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

/* TDLS specific payload type in the LLC/SNAP header */
#define WLAN_TDLS_SNAP_RFTYPE	0x2

/* BSS Coex IE information field bits */
#define WLAN_BSS_COEX_INFORMATION_REQUEST	BIT(0)

/**
 * enum ieee80211_mesh_sync_method - mesh synchronization method identifier
 *
 * @IEEE80211_SYNC_METHOD_NEIGHBOR_OFFSET: the default synchronization method
 * @IEEE80211_SYNC_METHOD_VENDOR: a vendor specific synchronization method
 *	that will be specified in a vendor specific information element
 */
enum ieee80211_mesh_sync_method {
	IEEE80211_SYNC_METHOD_NEIGHBOR_OFFSET = 1,
	IEEE80211_SYNC_METHOD_VENDOR = 255,
};

/**
 * enum ieee80211_mesh_path_protocol - mesh path selection protocol identifier
 *
 * @IEEE80211_PATH_PROTOCOL_HWMP: the default path selection protocol
 * @IEEE80211_PATH_PROTOCOL_VENDOR: a vendor specific protocol that will
 *	be specified in a vendor specific information element
 */
enum ieee80211_mesh_path_protocol {
	IEEE80211_PATH_PROTOCOL_HWMP = 1,
	IEEE80211_PATH_PROTOCOL_VENDOR = 255,
};

/**
 * enum ieee80211_mesh_path_metric - mesh path selection metric identifier
 *
 * @IEEE80211_PATH_METRIC_AIRTIME: the default path selection metric
 * @IEEE80211_PATH_METRIC_VENDOR: a vendor specific metric that will be
 *	specified in a vendor specific information element
 */
enum ieee80211_mesh_path_metric {
	IEEE80211_PATH_METRIC_AIRTIME = 1,
	IEEE80211_PATH_METRIC_VENDOR = 255,
};

/**
 * enum ieee80211_root_mode_identifier - root mesh STA mode identifier
 *
 * These attribute are used by dot11MeshHWMPRootMode to set root mesh STA mode
 *
 * @IEEE80211_ROOTMODE_NO_ROOT: the mesh STA is not a root mesh STA (default)
 * @IEEE80211_ROOTMODE_ROOT: the mesh STA is a root mesh STA if greater than
 *	this value
 * @IEEE80211_PROACTIVE_PREQ_NO_PREP: the mesh STA is a root mesh STA supports
 *	the proactive PREQ with proactive PREP subfield set to 0
 * @IEEE80211_PROACTIVE_PREQ_WITH_PREP: the mesh STA is a root mesh STA
 *	supports the proactive PREQ with proactive PREP subfield set to 1
 * @IEEE80211_PROACTIVE_RANN: the mesh STA is a root mesh STA supports
 *	the proactive RANN
 */
enum ieee80211_root_mode_identifier {
	IEEE80211_ROOTMODE_NO_ROOT = 0,
	IEEE80211_ROOTMODE_ROOT = 1,
	IEEE80211_PROACTIVE_PREQ_NO_PREP = 2,
	IEEE80211_PROACTIVE_PREQ_WITH_PREP = 3,
	IEEE80211_PROACTIVE_RANN = 4,
};

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
 * struct ieee80211_bss_max_idle_period_ie
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

/* BACK action code */
enum ieee80211_back_actioncode {
	WLAN_ACTION_ADDBA_REQ = 0,
	WLAN_ACTION_ADDBA_RESP = 1,
	WLAN_ACTION_DELBA = 2,
};

/* BACK (block-ack) parties */
enum ieee80211_back_parties {
	WLAN_BACK_RECIPIENT = 0,
	WLAN_BACK_INITIATOR = 1,
};

/* SA Query action */
enum ieee80211_sa_query_action {
	WLAN_ACTION_SA_QUERY_REQUEST = 0,
	WLAN_ACTION_SA_QUERY_RESPONSE = 1,
};

/**
 * struct ieee80211_bssid_index
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
 * struct ieee80211_multiple_bssid_configuration
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

struct ieee80211_he_6ghz_capa {
	/* uses IEEE80211_HE_6GHZ_CAP_* below */
	__le16 capa;
} __packed;

/* HE 6 GHz band capabilities */
/* uses enum ieee80211_min_mpdu_spacing values */
#define IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START	0x0007
/* uses enum ieee80211_vht_max_ampdu_length_exp values */
#define IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP	0x0038
/* uses IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_* values */
#define IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN	0x00c0
/* WLAN_HT_CAP_SM_PS_* values */
#define IEEE80211_HE_6GHZ_CAP_SM_PS		0x0600
#define IEEE80211_HE_6GHZ_CAP_RD_RESPONDER	0x0800
#define IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS	0x1000
#define IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS	0x2000

/**
 * ieee80211_get_qos_ctl - get pointer to qos control bytes
 * @hdr: the frame
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
 */
static inline u8 ieee80211_get_tid(struct ieee80211_hdr *hdr)
{
	u8 *qc = ieee80211_get_qos_ctl(hdr);

	return qc[0] & IEEE80211_QOS_CTL_TID_MASK;
}

/**
 * ieee80211_get_SA - get pointer to SA
 * @hdr: the frame
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
		action != WLAN_PUB_ACTION_FILS_DISCOVERY;
}

/**
 * _ieee80211_is_group_privacy_action - check if frame is a group addressed
 * privacy action frame
 * @hdr: the frame
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
 * privacy action frame
 * @skb: the skb containing the frame, length will be checked
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
 */
static inline unsigned long ieee80211_tu_to_usec(unsigned long tu)
{
	return 1024 * tu;
}

/**
 * ieee80211_check_tim - check if AID bit is set in TIM
 * @tim: the TIM IE
 * @tim_len: length of the TIM IE
 * @aid: the AID to look for
 */
static inline bool ieee80211_check_tim(const struct ieee80211_tim_ie *tim,
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
 * ieee80211_get_tdls_action - get tdls packet action (or -1, if not tdls packet)
 * @skb: the skb containing the frame, length will not be checked
 * @hdr_size: the size of the ieee80211_hdr that starts at skb->data
 *
 * This function assumes the frame is a data frame, and that the network header
 * is in the correct place.
 */
static inline int ieee80211_get_tdls_action(struct sk_buff *skb, u32 hdr_size)
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
} __packed;

/* multi-link device */
#define IEEE80211_MLD_MAX_NUM_LINKS	15

#define IEEE80211_ML_CONTROL_TYPE			0x0007
#define IEEE80211_ML_CONTROL_TYPE_BASIC			0
#define IEEE80211_ML_CONTROL_TYPE_PREQ			1
#define IEEE80211_ML_CONTROL_TYPE_RECONF		2
#define IEEE80211_ML_CONTROL_TYPE_TDLS			3
#define IEEE80211_ML_CONTROL_TYPE_PRIO_ACCESS		4
#define IEEE80211_ML_CONTROL_PRESENCE_MASK		0xfff0

struct ieee80211_multi_link_elem {
	__le16 control;
	u8 variable[];
} __packed;

#define IEEE80211_MLC_BASIC_PRES_LINK_ID		0x0010
#define IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT	0x0020
#define IEEE80211_MLC_BASIC_PRES_MED_SYNC_DELAY		0x0040
#define IEEE80211_MLC_BASIC_PRES_EML_CAPA		0x0080
#define IEEE80211_MLC_BASIC_PRES_MLD_CAPA_OP		0x0100
#define IEEE80211_MLC_BASIC_PRES_MLD_ID			0x0200

#define IEEE80211_MED_SYNC_DELAY_DURATION		0x00ff
#define IEEE80211_MED_SYNC_DELAY_SYNC_OFDM_ED_THRESH	0x0f00
#define IEEE80211_MED_SYNC_DELAY_SYNC_MAX_NUM_TXOPS	0xf000

/*
 * Described in P802.11be_D3.0
 * dot11MSDTimerDuration should default to 5484 (i.e. 171.375)
 * dot11MSDOFDMEDthreshold defaults to -72 (i.e. 0)
 * dot11MSDTXOPMAX defaults to 1
 */
#define IEEE80211_MED_SYNC_DELAY_DEFAULT		0x10ac

#define IEEE80211_EML_CAP_EMLSR_SUPP			0x0001
#define IEEE80211_EML_CAP_EMLSR_PADDING_DELAY		0x000e
#define  IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_0US		0
#define  IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_32US		1
#define  IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_64US		2
#define  IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_128US		3
#define  IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_256US		4
#define IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY	0x0070
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_0US		0
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_16US		1
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_32US		2
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_64US		3
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_128US		4
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_256US		5
#define IEEE80211_EML_CAP_EMLMR_SUPPORT			0x0080
#define IEEE80211_EML_CAP_EMLMR_DELAY			0x0700
#define  IEEE80211_EML_CAP_EMLMR_DELAY_0US			0
#define  IEEE80211_EML_CAP_EMLMR_DELAY_32US			1
#define  IEEE80211_EML_CAP_EMLMR_DELAY_64US			2
#define  IEEE80211_EML_CAP_EMLMR_DELAY_128US			3
#define  IEEE80211_EML_CAP_EMLMR_DELAY_256US			4
#define IEEE80211_EML_CAP_TRANSITION_TIMEOUT		0x7800
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_0			0
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_128US		1
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_256US		2
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_512US		3
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_1TU		4
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_2TU		5
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_4TU		6
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_8TU		7
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_16TU		8
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_32TU		9
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_64TU		10
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_128TU		11

#define IEEE80211_MLD_CAP_OP_MAX_SIMUL_LINKS		0x000f
#define IEEE80211_MLD_CAP_OP_SRS_SUPPORT		0x0010
#define IEEE80211_MLD_CAP_OP_TID_TO_LINK_MAP_NEG_SUPP	0x0060
#define IEEE80211_MLD_CAP_OP_FREQ_SEP_TYPE_IND		0x0f80
#define IEEE80211_MLD_CAP_OP_AAR_SUPPORT		0x1000

struct ieee80211_mle_basic_common_info {
	u8 len;
	u8 mld_mac_addr[ETH_ALEN];
	u8 variable[];
} __packed;

#define IEEE80211_MLC_PREQ_PRES_MLD_ID			0x0010

struct ieee80211_mle_preq_common_info {
	u8 len;
	u8 variable[];
} __packed;

#define IEEE80211_MLC_RECONF_PRES_MLD_MAC_ADDR		0x0010

/* no fixed fields in RECONF */

struct ieee80211_mle_tdls_common_info {
	u8 len;
	u8 ap_mld_mac_addr[ETH_ALEN];
} __packed;

#define IEEE80211_MLC_PRIO_ACCESS_PRES_AP_MLD_MAC_ADDR	0x0010

/* no fixed fields in PRIO_ACCESS */

/**
 * ieee80211_mle_common_size - check multi-link element common size
 * @data: multi-link element, must already be checked for size using
 *	ieee80211_mle_size_ok()
 */
static inline u8 ieee80211_mle_common_size(const u8 *data)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control = le16_to_cpu(mle->control);
	u8 common = 0;

	switch (u16_get_bits(control, IEEE80211_ML_CONTROL_TYPE)) {
	case IEEE80211_ML_CONTROL_TYPE_BASIC:
	case IEEE80211_ML_CONTROL_TYPE_PREQ:
	case IEEE80211_ML_CONTROL_TYPE_TDLS:
	case IEEE80211_ML_CONTROL_TYPE_RECONF:
		/*
		 * The length is the first octet pointed by mle->variable so no
		 * need to add anything
		 */
		break;
	case IEEE80211_ML_CONTROL_TYPE_PRIO_ACCESS:
		if (control & IEEE80211_MLC_PRIO_ACCESS_PRES_AP_MLD_MAC_ADDR)
			common += ETH_ALEN;
		return common;
	default:
		WARN_ON(1);
		return 0;
	}

	return sizeof(*mle) + common + mle->variable[0];
}

/**
 * ieee80211_mle_get_bss_param_ch_cnt - returns the BSS parameter change count
 * @mle: the basic multi link element
 *
 * The element is assumed to be of the correct type (BASIC) and big enough,
 * this must be checked using ieee80211_mle_type_ok().
 *
 * If the BSS parameter change count value can't be found (the presence bit
 * for it is clear), 0 will be returned.
 */
static inline u8
ieee80211_mle_get_bss_param_ch_cnt(const struct ieee80211_multi_link_elem *mle)
{
	u16 control = le16_to_cpu(mle->control);
	const u8 *common = mle->variable;

	/* common points now at the beginning of ieee80211_mle_basic_common_info */
	common += sizeof(struct ieee80211_mle_basic_common_info);

	if (!(control & IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT))
		return 0;

	if (control & IEEE80211_MLC_BASIC_PRES_LINK_ID)
		common += 1;

	return *common;
}

/**
 * ieee80211_mle_get_eml_med_sync_delay - returns the medium sync delay
 * @data: pointer to the multi link EHT IE
 *
 * The element is assumed to be of the correct type (BASIC) and big enough,
 * this must be checked using ieee80211_mle_type_ok().
 *
 * If the medium synchronization is not present, then the default value is
 * returned.
 */
static inline u16 ieee80211_mle_get_eml_med_sync_delay(const u8 *data)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control = le16_to_cpu(mle->control);
	const u8 *common = mle->variable;

	/* common points now at the beginning of ieee80211_mle_basic_common_info */
	common += sizeof(struct ieee80211_mle_basic_common_info);

	if (!(control & IEEE80211_MLC_BASIC_PRES_MED_SYNC_DELAY))
		return IEEE80211_MED_SYNC_DELAY_DEFAULT;

	if (control & IEEE80211_MLC_BASIC_PRES_LINK_ID)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT)
		common += 1;

	return get_unaligned_le16(common);
}

/**
 * ieee80211_mle_get_eml_cap - returns the EML capability
 * @data: pointer to the multi link EHT IE
 *
 * The element is assumed to be of the correct type (BASIC) and big enough,
 * this must be checked using ieee80211_mle_type_ok().
 *
 * If the EML capability is not present, 0 will be returned.
 */
static inline u16 ieee80211_mle_get_eml_cap(const u8 *data)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control = le16_to_cpu(mle->control);
	const u8 *common = mle->variable;

	/* common points now at the beginning of ieee80211_mle_basic_common_info */
	common += sizeof(struct ieee80211_mle_basic_common_info);

	if (!(control & IEEE80211_MLC_BASIC_PRES_EML_CAPA))
		return 0;

	if (control & IEEE80211_MLC_BASIC_PRES_LINK_ID)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_MED_SYNC_DELAY)
		common += 2;

	return get_unaligned_le16(common);
}

/**
 * ieee80211_mle_size_ok - validate multi-link element size
 * @data: pointer to the element data
 * @len: length of the containing element
 */
static inline bool ieee80211_mle_size_ok(const u8 *data, size_t len)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u8 fixed = sizeof(*mle);
	u8 common = 0;
	bool check_common_len = false;
	u16 control;

	if (len < fixed)
		return false;

	control = le16_to_cpu(mle->control);

	switch (u16_get_bits(control, IEEE80211_ML_CONTROL_TYPE)) {
	case IEEE80211_ML_CONTROL_TYPE_BASIC:
		common += sizeof(struct ieee80211_mle_basic_common_info);
		check_common_len = true;
		if (control & IEEE80211_MLC_BASIC_PRES_LINK_ID)
			common += 1;
		if (control & IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT)
			common += 1;
		if (control & IEEE80211_MLC_BASIC_PRES_MED_SYNC_DELAY)
			common += 2;
		if (control & IEEE80211_MLC_BASIC_PRES_EML_CAPA)
			common += 2;
		if (control & IEEE80211_MLC_BASIC_PRES_MLD_CAPA_OP)
			common += 2;
		if (control & IEEE80211_MLC_BASIC_PRES_MLD_ID)
			common += 1;
		break;
	case IEEE80211_ML_CONTROL_TYPE_PREQ:
		common += sizeof(struct ieee80211_mle_preq_common_info);
		if (control & IEEE80211_MLC_PREQ_PRES_MLD_ID)
			common += 1;
		check_common_len = true;
		break;
	case IEEE80211_ML_CONTROL_TYPE_RECONF:
		if (control & IEEE80211_MLC_RECONF_PRES_MLD_MAC_ADDR)
			common += ETH_ALEN;
		break;
	case IEEE80211_ML_CONTROL_TYPE_TDLS:
		common += sizeof(struct ieee80211_mle_tdls_common_info);
		check_common_len = true;
		break;
	case IEEE80211_ML_CONTROL_TYPE_PRIO_ACCESS:
		if (control & IEEE80211_MLC_PRIO_ACCESS_PRES_AP_MLD_MAC_ADDR)
			common += ETH_ALEN;
		break;
	default:
		/* we don't know this type */
		return true;
	}

	if (len < fixed + common)
		return false;

	if (!check_common_len)
		return true;

	/* if present, common length is the first octet there */
	return mle->variable[0] >= common;
}

/**
 * ieee80211_mle_type_ok - validate multi-link element type and size
 * @data: pointer to the element data
 * @type: expected type of the element
 * @len: length of the containing element
 */
static inline bool ieee80211_mle_type_ok(const u8 *data, u8 type, size_t len)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control;

	if (!ieee80211_mle_size_ok(data, len))
		return false;

	control = le16_to_cpu(mle->control);

	if (u16_get_bits(control, IEEE80211_ML_CONTROL_TYPE) == type)
		return true;

	return false;
}

enum ieee80211_mle_subelems {
	IEEE80211_MLE_SUBELEM_PER_STA_PROFILE		= 0,
	IEEE80211_MLE_SUBELEM_FRAGMENT		        = 254,
};

#define IEEE80211_MLE_STA_CONTROL_LINK_ID			0x000f
#define IEEE80211_MLE_STA_CONTROL_COMPLETE_PROFILE		0x0010
#define IEEE80211_MLE_STA_CONTROL_STA_MAC_ADDR_PRESENT		0x0020
#define IEEE80211_MLE_STA_CONTROL_BEACON_INT_PRESENT		0x0040
#define IEEE80211_MLE_STA_CONTROL_TSF_OFFS_PRESENT		0x0080
#define IEEE80211_MLE_STA_CONTROL_DTIM_INFO_PRESENT		0x0100
#define IEEE80211_MLE_STA_CONTROL_NSTR_LINK_PAIR_PRESENT	0x0200
#define IEEE80211_MLE_STA_CONTROL_NSTR_BITMAP_SIZE		0x0400
#define IEEE80211_MLE_STA_CONTROL_BSS_PARAM_CHANGE_CNT_PRESENT	0x0800

struct ieee80211_mle_per_sta_profile {
	__le16 control;
	u8 sta_info_len;
	u8 variable[];
} __packed;

/**
 * ieee80211_mle_basic_sta_prof_size_ok - validate basic multi-link element sta
 *	profile size
 * @data: pointer to the sub element data
 * @len: length of the containing sub element
 */
static inline bool ieee80211_mle_basic_sta_prof_size_ok(const u8 *data,
							size_t len)
{
	const struct ieee80211_mle_per_sta_profile *prof = (const void *)data;
	u16 control;
	u8 fixed = sizeof(*prof);
	u8 info_len = 1;

	if (len < fixed)
		return false;

	control = le16_to_cpu(prof->control);

	if (control & IEEE80211_MLE_STA_CONTROL_STA_MAC_ADDR_PRESENT)
		info_len += 6;
	if (control & IEEE80211_MLE_STA_CONTROL_BEACON_INT_PRESENT)
		info_len += 2;
	if (control & IEEE80211_MLE_STA_CONTROL_TSF_OFFS_PRESENT)
		info_len += 8;
	if (control & IEEE80211_MLE_STA_CONTROL_DTIM_INFO_PRESENT)
		info_len += 2;
	if (control & IEEE80211_MLE_STA_CONTROL_COMPLETE_PROFILE &&
	    control & IEEE80211_MLE_STA_CONTROL_NSTR_LINK_PAIR_PRESENT) {
		if (control & IEEE80211_MLE_STA_CONTROL_NSTR_BITMAP_SIZE)
			info_len += 2;
		else
			info_len += 1;
	}
	if (control & IEEE80211_MLE_STA_CONTROL_BSS_PARAM_CHANGE_CNT_PRESENT)
		info_len += 1;

	return prof->sta_info_len >= info_len &&
	       fixed + prof->sta_info_len <= len;
}

/**
 * ieee80211_mle_basic_sta_prof_bss_param_ch_cnt - get per-STA profile BSS
 *	parameter change count
 * @prof: the per-STA profile, having been checked with
 *	ieee80211_mle_basic_sta_prof_size_ok() for the correct length
 *
 * Return: The BSS parameter change count value if present, 0 otherwise.
 */
static inline u8
ieee80211_mle_basic_sta_prof_bss_param_ch_cnt(const struct ieee80211_mle_per_sta_profile *prof)
{
	u16 control = le16_to_cpu(prof->control);
	const u8 *pos = prof->variable;

	if (!(control & IEEE80211_MLE_STA_CONTROL_BSS_PARAM_CHANGE_CNT_PRESENT))
		return 0;

	if (control & IEEE80211_MLE_STA_CONTROL_STA_MAC_ADDR_PRESENT)
		pos += 6;
	if (control & IEEE80211_MLE_STA_CONTROL_BEACON_INT_PRESENT)
		pos += 2;
	if (control & IEEE80211_MLE_STA_CONTROL_TSF_OFFS_PRESENT)
		pos += 8;
	if (control & IEEE80211_MLE_STA_CONTROL_DTIM_INFO_PRESENT)
		pos += 2;
	if (control & IEEE80211_MLE_STA_CONTROL_COMPLETE_PROFILE &&
	    control & IEEE80211_MLE_STA_CONTROL_NSTR_LINK_PAIR_PRESENT) {
		if (control & IEEE80211_MLE_STA_CONTROL_NSTR_BITMAP_SIZE)
			pos += 2;
		else
			pos += 1;
	}

	return *pos;
}

#define IEEE80211_MLE_STA_RECONF_CONTROL_LINK_ID			0x000f
#define IEEE80211_MLE_STA_RECONF_CONTROL_COMPLETE_PROFILE		0x0010
#define IEEE80211_MLE_STA_RECONF_CONTROL_STA_MAC_ADDR_PRESENT		0x0020
#define IEEE80211_MLE_STA_RECONF_CONTROL_AP_REM_TIMER_PRESENT		0x0040
#define IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_UPDATE_TYPE		0x0780
#define IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_PARAMS_PRESENT	0x0800

/**
 * ieee80211_mle_reconf_sta_prof_size_ok - validate reconfiguration multi-link
 *	element sta profile size.
 * @data: pointer to the sub element data
 * @len: length of the containing sub element
 */
static inline bool ieee80211_mle_reconf_sta_prof_size_ok(const u8 *data,
							 size_t len)
{
	const struct ieee80211_mle_per_sta_profile *prof = (const void *)data;
	u16 control;
	u8 fixed = sizeof(*prof);
	u8 info_len = 1;

	if (len < fixed)
		return false;

	control = le16_to_cpu(prof->control);

	if (control & IEEE80211_MLE_STA_RECONF_CONTROL_STA_MAC_ADDR_PRESENT)
		info_len += ETH_ALEN;
	if (control & IEEE80211_MLE_STA_RECONF_CONTROL_AP_REM_TIMER_PRESENT)
		info_len += 2;
	if (control & IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_PARAMS_PRESENT)
		info_len += 2;

	return prof->sta_info_len >= info_len &&
	       fixed + prof->sta_info_len - 1 <= len;
}

#define for_each_mle_subelement(_elem, _data, _len)			\
	if (ieee80211_mle_size_ok(_data, _len))				\
		for_each_element(_elem,					\
				 _data + ieee80211_mle_common_size(_data),\
				 _len - ieee80211_mle_common_size(_data))

#endif /* LINUX_IEEE80211_H */
