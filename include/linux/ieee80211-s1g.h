/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IEEE 802.11 S1G definitions
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright (c) 2005, Devicescape Software, Inc.
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright (c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright (c) 2018 - 2025 Intel Corporation
 */

#ifndef LINUX_IEEE80211_S1G_H
#define LINUX_IEEE80211_S1G_H

#include <linux/types.h>
#include <linux/if_ether.h>

/* bits unique to S1G beacon frame control */
#define IEEE80211_S1G_BCN_NEXT_TBTT	0x100
#define IEEE80211_S1G_BCN_CSSID		0x200
#define IEEE80211_S1G_BCN_ANO		0x400

/* see 802.11ah-2016 9.9 NDP CMAC frames */
#define IEEE80211_S1G_1MHZ_NDP_BITS	25
#define IEEE80211_S1G_1MHZ_NDP_BYTES	4
#define IEEE80211_S1G_2MHZ_NDP_BITS	37
#define IEEE80211_S1G_2MHZ_NDP_BYTES	5

/**
 * ieee80211_is_s1g_beacon - check if IEEE80211_FTYPE_EXT &&
 * IEEE80211_STYPE_S1G_BEACON
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame is an S1G beacon
 */
static inline bool ieee80211_is_s1g_beacon(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE |
				 IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_EXT | IEEE80211_STYPE_S1G_BEACON);
}

/**
 * ieee80211_s1g_has_next_tbtt - check if IEEE80211_S1G_BCN_NEXT_TBTT
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame contains the variable-length
 *	next TBTT field
 */
static inline bool ieee80211_s1g_has_next_tbtt(__le16 fc)
{
	return ieee80211_is_s1g_beacon(fc) &&
		(fc & cpu_to_le16(IEEE80211_S1G_BCN_NEXT_TBTT));
}

/**
 * ieee80211_s1g_has_ano - check if IEEE80211_S1G_BCN_ANO
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame contains the variable-length
 *	ANO field
 */
static inline bool ieee80211_s1g_has_ano(__le16 fc)
{
	return ieee80211_is_s1g_beacon(fc) &&
		(fc & cpu_to_le16(IEEE80211_S1G_BCN_ANO));
}

/**
 * ieee80211_s1g_has_cssid - check if IEEE80211_S1G_BCN_CSSID
 * @fc: frame control bytes in little-endian byteorder
 * Return: whether or not the frame contains the variable-length
 *	compressed SSID field
 */
static inline bool ieee80211_s1g_has_cssid(__le16 fc)
{
	return ieee80211_is_s1g_beacon(fc) &&
		(fc & cpu_to_le16(IEEE80211_S1G_BCN_CSSID));
}

/**
 * enum ieee80211_s1g_chanwidth - S1G channel widths
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

/**
 * enum ieee80211_s1g_pri_chanwidth - S1G primary channel widths
 *	described in IEEE80211-2024 Table 10-39.
 *
 * @IEEE80211_S1G_PRI_CHANWIDTH_2MHZ: 2MHz primary channel
 * @IEEE80211_S1G_PRI_CHANWIDTH_1MHZ: 1MHz primary channel
 */
enum ieee80211_s1g_pri_chanwidth {
	IEEE80211_S1G_PRI_CHANWIDTH_2MHZ = 0,
	IEEE80211_S1G_PRI_CHANWIDTH_1MHZ = 1,
};

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

/**
 * ieee80211_s1g_optional_len - determine length of optional S1G beacon fields
 * @fc: frame control bytes in little-endian byteorder
 * Return: total length in bytes of the optional fixed-length fields
 *
 * S1G beacons may contain up to three optional fixed-length fields that
 * precede the variable-length elements. Whether these fields are present
 * is indicated by flags in the frame control field.
 *
 * From IEEE 802.11-2024 section 9.3.4.3:
 *  - Next TBTT field may be 0 or 3 bytes
 *  - Short SSID field may be 0 or 4 bytes
 *  - Access Network Options (ANO) field may be 0 or 1 byte
 */
static inline size_t
ieee80211_s1g_optional_len(__le16 fc)
{
	size_t len = 0;

	if (ieee80211_s1g_has_next_tbtt(fc))
		len += 3;

	if (ieee80211_s1g_has_cssid(fc))
		len += 4;

	if (ieee80211_s1g_has_ano(fc))
		len += 1;

	return len;
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

#define S1G_OPER_CH_WIDTH_PRIMARY	BIT(0)
#define S1G_OPER_CH_WIDTH_OPER		GENMASK(4, 1)
#define S1G_OPER_CH_PRIMARY_LOCATION	BIT(5)

#define S1G_2M_PRIMARY_LOCATION_LOWER	0
#define S1G_2M_PRIMARY_LOCATION_UPPER	1

#define LISTEN_INT_USF	GENMASK(15, 14)
#define LISTEN_INT_UI	GENMASK(13, 0)

#define IEEE80211_MAX_USF	FIELD_MAX(LISTEN_INT_USF)
#define IEEE80211_MAX_UI	FIELD_MAX(LISTEN_INT_UI)

/* S1G encoding types */
#define IEEE80211_S1G_TIM_ENC_MODE_BLOCK	0
#define IEEE80211_S1G_TIM_ENC_MODE_SINGLE	1
#define IEEE80211_S1G_TIM_ENC_MODE_OLB		2

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

/**
 * ieee80211_is_s1g_short_beacon - check if frame is an S1G short beacon
 * @fc: frame control bytes in little-endian byteorder
 * @variable: pointer to the beacon frame elements
 * @variable_len: length of the frame elements
 * Return: whether or not the frame is an S1G short beacon. As per
 *	IEEE80211-2024 11.1.3.10.1, The S1G beacon compatibility element shall
 *	always be present as the first element in beacon frames generated at a
 *	TBTT (Target Beacon Transmission Time), so any frame not containing
 *	this element must have been generated at a TSBTT (Target Short Beacon
 *	Transmission Time) that is not a TBTT. Additionally, short beacons are
 *	prohibited from containing the S1G beacon compatibility element as per
 *	IEEE80211-2024 9.3.4.3 Table 9-76, so if we have an S1G beacon with
 *	either no elements or the first element is not the beacon compatibility
 *	element, we have a short beacon.
 */
static inline bool ieee80211_is_s1g_short_beacon(__le16 fc, const u8 *variable,
						 size_t variable_len)
{
	if (!ieee80211_is_s1g_beacon(fc))
		return false;

	/*
	 * If the frame does not contain at least 1 element (this is perfectly
	 * valid in a short beacon) and is an S1G beacon, we have a short
	 * beacon.
	 */
	if (variable_len < 2)
		return true;

	return variable[0] != WLAN_EID_S1G_BCN_COMPAT;
}

struct s1g_tim_aid {
	u16 aid;
	u8 target_blk; /* Target block index */
	u8 target_subblk; /* Target subblock index */
	u8 target_subblk_bit; /* Target subblock bit */
};

struct s1g_tim_enc_block {
	u8 enc_mode;
	bool inverse;
	const u8 *ptr;
	u8 len;

	/*
	 * For an OLB encoded block that spans multiple blocks, this
	 * is the offset into the span described by that encoded block.
	 */
	u8 olb_blk_offset;
};

/*
 * Helper routines to quickly extract the length of an encoded block. Validation
 * is also performed to ensure the length extracted lies within the TIM.
 */

static inline int ieee80211_s1g_len_bitmap(const u8 *ptr, const u8 *end)
{
	u8 blkmap;
	u8 n_subblks;

	if (ptr >= end)
		return -EINVAL;

	blkmap = *ptr;
	n_subblks = hweight8(blkmap);

	if (ptr + 1 + n_subblks > end)
		return -EINVAL;

	return 1 + n_subblks;
}

static inline int ieee80211_s1g_len_single(const u8 *ptr, const u8 *end)
{
	return (ptr + 1 > end) ? -EINVAL : 1;
}

static inline int ieee80211_s1g_len_olb(const u8 *ptr, const u8 *end)
{
	if (ptr >= end)
		return -EINVAL;

	return (ptr + 1 + *ptr > end) ? -EINVAL : 1 + *ptr;
}

/*
 * Enumerate all encoded blocks until we find the encoded block that describes
 * our target AID. OLB is a special case as a single encoded block can describe
 * multiple blocks as a single encoded block.
 */
static inline int ieee80211_s1g_find_target_block(struct s1g_tim_enc_block *enc,
						  const struct s1g_tim_aid *aid,
						  const u8 *ptr, const u8 *end)
{
	/* need at least block-control octet */
	while (ptr + 1 <= end) {
		u8 ctrl = *ptr++;
		u8 mode = ctrl & 0x03;
		bool contains, inverse = ctrl & BIT(2);
		u8 span, blk_off = ctrl >> 3;
		int len;

		switch (mode) {
		case IEEE80211_S1G_TIM_ENC_MODE_BLOCK:
			len = ieee80211_s1g_len_bitmap(ptr, end);
			contains = blk_off == aid->target_blk;
			break;
		case IEEE80211_S1G_TIM_ENC_MODE_SINGLE:
			len = ieee80211_s1g_len_single(ptr, end);
			contains = blk_off == aid->target_blk;
			break;
		case IEEE80211_S1G_TIM_ENC_MODE_OLB:
			len = ieee80211_s1g_len_olb(ptr, end);
			/*
			 * An OLB encoded block can describe more then one
			 * block, meaning an encoded OLB block can span more
			 * then a single block.
			 */
			if (len > 0) {
				/* Minus one for the length octet */
				span = DIV_ROUND_UP(len - 1, 8);
				/*
				 * Check if our target block lies within the
				 * block span described by this encoded block.
				 */
				contains = (aid->target_blk >= blk_off) &&
					   (aid->target_blk < blk_off + span);
			}
			break;
		default:
			return -EOPNOTSUPP;
		}

		if (len < 0)
			return len;

		if (contains) {
			enc->enc_mode = mode;
			enc->inverse = inverse;
			enc->ptr = ptr;
			enc->len = (u8)len;
			enc->olb_blk_offset = blk_off;
			return 0;
		}

		ptr += len;
	}

	return -ENOENT;
}

static inline bool ieee80211_s1g_parse_bitmap(struct s1g_tim_enc_block *enc,
					      struct s1g_tim_aid *aid)
{
	const u8 *ptr = enc->ptr;
	u8 blkmap = *ptr++;

	/*
	 * If our block bitmap does not contain a set bit that corresponds
	 * to our AID, it could mean a variety of things depending on if
	 * the encoding mode is inverted or not.
	 *
	 * 1. If inverted, it means the entire subblock is present and hence
	 *    our AID has been set.
	 * 2. If not inverted, it means our subblock is not present and hence
	 *    it is all zero meaning our AID is not set.
	 */
	if (!(blkmap & BIT(aid->target_subblk)))
		return enc->inverse;

	/*
	 * Increment ptr by the number of set subblocks that appear before our
	 * target subblock. If our target subblock is 0, do nothing as ptr
	 * already points to our target subblock.
	 */
	if (aid->target_subblk)
		ptr += hweight8(blkmap & GENMASK(aid->target_subblk - 1, 0));

	return !!(*ptr & BIT(aid->target_subblk_bit)) ^ enc->inverse;
}

static inline bool ieee80211_s1g_parse_single(struct s1g_tim_enc_block *enc,
					      struct s1g_tim_aid *aid)
{
	/*
	 * Single AID mode describes, as the name suggests, a single AID
	 * within the block described by the encoded block. The octet
	 * contains the 6 LSBs of the AID described in the block. The other
	 * 2 bits are reserved. When inversed, every single AID described
	 * by the current block have buffered traffic except for the AID
	 * described in the single AID octet.
	 */
	return ((*enc->ptr & 0x3f) == (aid->aid & 0x3f)) ^ enc->inverse;
}

static inline bool ieee80211_s1g_parse_olb(struct s1g_tim_enc_block *enc,
					   struct s1g_tim_aid *aid)
{
	const u8 *ptr = enc->ptr;
	u8 blk_len = *ptr++;
	/*
	 * Given an OLB encoded block that describes multiple blocks,
	 * calculate the offset into the span. Then calculate the
	 * subblock location normally.
	 */
	u16 span_offset = aid->target_blk - enc->olb_blk_offset;
	u16 subblk_idx = span_offset * 8 + aid->target_subblk;

	if (subblk_idx >= blk_len)
		return enc->inverse;

	return !!(ptr[subblk_idx] & BIT(aid->target_subblk_bit)) ^ enc->inverse;
}

/*
 * An S1G PVB has 3 non optional encoding types, each that can be inverted.
 * An S1G PVB is constructed with zero or more encoded block subfields. Each
 * encoded block represents a single "block" of AIDs (64), and each encoded
 * block can contain one of the 3 encoding types alongside a single bit for
 * whether the bits should be inverted.
 *
 * As the standard makes no guarantee about the ordering of encoded blocks,
 * we must parse every encoded block in the worst case scenario given an
 * AID that lies within the last block.
 */
static inline bool ieee80211_s1g_check_tim(const struct ieee80211_tim_ie *tim,
					   u8 tim_len, u16 aid)
{
	int err;
	struct s1g_tim_aid target_aid;
	struct s1g_tim_enc_block enc_blk;

	if (tim_len < 3)
		return false;

	target_aid.aid = aid;
	target_aid.target_blk = (aid >> 6) & 0x1f;
	target_aid.target_subblk = (aid >> 3) & 0x7;
	target_aid.target_subblk_bit = aid & 0x7;

	/*
	 * Find our AIDs target encoded block and fill &enc_blk with the
	 * encoded blocks information. If no entry is found or an error
	 * occurs return false.
	 */
	err = ieee80211_s1g_find_target_block(&enc_blk, &target_aid,
					      tim->virtual_map,
					      (const u8 *)tim + tim_len + 2);
	if (err)
		return false;

	switch (enc_blk.enc_mode) {
	case IEEE80211_S1G_TIM_ENC_MODE_BLOCK:
		return ieee80211_s1g_parse_bitmap(&enc_blk, &target_aid);
	case IEEE80211_S1G_TIM_ENC_MODE_SINGLE:
		return ieee80211_s1g_parse_single(&enc_blk, &target_aid);
	case IEEE80211_S1G_TIM_ENC_MODE_OLB:
		return ieee80211_s1g_parse_olb(&enc_blk, &target_aid);
	default:
		return false;
	}
}

#endif /* LINUX_IEEE80211_S1G_H */
