/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * WFA NAN definitions
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

#ifndef LINUX_IEEE80211_NAN_H
#define LINUX_IEEE80211_NAN_H

/* NAN operation mode, as defined in Wi-Fi Aware (TM) specification Table 81 */
#define NAN_OP_MODE_PHY_MODE_VHT	0x01
#define NAN_OP_MODE_PHY_MODE_HE		0x10
#define NAN_OP_MODE_PHY_MODE_MASK	0x11
#define NAN_OP_MODE_80P80MHZ		0x02
#define NAN_OP_MODE_160MHZ		0x04
#define NAN_OP_MODE_PNDL_SUPPRTED	0x08

#define NAN_DEV_CAPA_NUM_TX_ANT_POS	0
#define NAN_DEV_CAPA_NUM_TX_ANT_MASK	0x0f
#define NAN_DEV_CAPA_NUM_RX_ANT_POS	4
#define NAN_DEV_CAPA_NUM_RX_ANT_MASK	0xf0

/* NAN Device capabilities, as defined in Wi-Fi Aware (TM) specification
 * Table 79
 */
#define NAN_DEV_CAPA_DFS_OWNER			0x01
#define NAN_DEV_CAPA_EXT_KEY_ID_SUPPORTED	0x02
#define NAN_DEV_CAPA_SIM_NDP_RX_SUPPORTED	0x04
#define NAN_DEV_CAPA_NDPE_SUPPORTED		0x08
#define NAN_DEV_CAPA_S3_SUPPORTED		0x10

/* NAN attributes, as defined in Wi-Fi Aware (TM) specification 4.0 Table 42 */
#define NAN_ATTR_MASTER_INDICATION		0x00
#define NAN_ATTR_CLUSTER_INFO			0x01

struct ieee80211_nan_attr {
	u8 attr;
	__le16 length;
	u8 data[];
} __packed;

struct ieee80211_nan_master_indication {
	u8 master_pref;
	u8 random_factor;
} __packed;

struct ieee80211_nan_anchor_master_info {
	union {
		__le64 master_rank;
		struct {
			u8 master_addr[ETH_ALEN];
			u8 random_factor;
			u8 master_pref;
		} __packed;
	} __packed;
	u8 hop_count;
	__le32 ambtt;
} __packed;

#define for_each_nan_attr(_attr, _data, _datalen)			\
	for (_attr = (const struct ieee80211_nan_attr *)(_data);	\
	     (const u8 *)(_data) + (_datalen) - (const u8 *)_attr >=	\
		(int)sizeof(*_attr) &&					\
	     (const u8 *)(_data) + (_datalen) - (const u8 *)_attr >=	\
		(int)sizeof(*_attr) + le16_to_cpu(_attr->length);	\
	     _attr = (const struct ieee80211_nan_attr *)		\
		(_attr->data + le16_to_cpu(_attr->length)))

#endif /* LINUX_IEEE80211_NAN_H */
