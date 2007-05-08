/*
 * IEEE 802.11 driver (80211.o) -- hostapd interface
 * Copyright 2002-2004, Instant802 Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IEEE80211_COMMON_H
#define IEEE80211_COMMON_H

#include <linux/types.h>

/*
 * This is common header information with user space. It is used on all
 * frames sent to wlan#ap interface.
 */

#define IEEE80211_FI_VERSION 0x80211001

struct ieee80211_frame_info {
	__be32 version;
	__be32 length;
	__be64 mactime;
	__be64 hosttime;
	__be32 phytype;
	__be32 channel;
	__be32 datarate;
	__be32 antenna;
	__be32 priority;
	__be32 ssi_type;
	__be32 ssi_signal;
	__be32 ssi_noise;
	__be32 preamble;
	__be32 encoding;

	/* Note: this structure is otherwise identical to capture format used
	 * in linux-wlan-ng, but this additional field is used to provide meta
	 * data about the frame to hostapd. This was the easiest method for
	 * providing this information, but this might change in the future. */
	__be32 msg_type;
} __attribute__ ((packed));


enum ieee80211_msg_type {
	ieee80211_msg_normal = 0,
	ieee80211_msg_tx_callback_ack = 1,
	ieee80211_msg_tx_callback_fail = 2,
	ieee80211_msg_passive_scan = 3,
	ieee80211_msg_wep_frame_unknown_key = 4,
	ieee80211_msg_michael_mic_failure = 5,
	/* hole at 6, was monitor but never sent to userspace */
	ieee80211_msg_sta_not_assoc = 7,
	ieee80211_msg_set_aid_for_sta = 8 /* used by Intersil MVC driver */,
	ieee80211_msg_key_threshold_notification = 9,
	ieee80211_msg_radar = 11,
};

struct ieee80211_msg_set_aid_for_sta {
	char	sta_address[ETH_ALEN];
	u16	aid;
};

struct ieee80211_msg_key_notification {
	int tx_rx_count;
	char ifname[IFNAMSIZ];
	u8 addr[ETH_ALEN]; /* ff:ff:ff:ff:ff:ff for broadcast keys */
};


enum ieee80211_phytype {
	ieee80211_phytype_fhss_dot11_97  = 1,
	ieee80211_phytype_dsss_dot11_97  = 2,
	ieee80211_phytype_irbaseband     = 3,
	ieee80211_phytype_dsss_dot11_b   = 4,
	ieee80211_phytype_pbcc_dot11_b   = 5,
	ieee80211_phytype_ofdm_dot11_g   = 6,
	ieee80211_phytype_pbcc_dot11_g   = 7,
	ieee80211_phytype_ofdm_dot11_a   = 8,
	ieee80211_phytype_dsss_dot11_turbog = 255,
	ieee80211_phytype_dsss_dot11_turbo = 256,
};

enum ieee80211_ssi_type {
	ieee80211_ssi_none = 0,
	ieee80211_ssi_norm = 1, /* normalized, 0-1000 */
	ieee80211_ssi_dbm = 2,
	ieee80211_ssi_raw = 3, /* raw SSI */
};

struct ieee80211_radar_info {
		int channel;
		int radar;
		int radar_type;
};

#endif /* IEEE80211_COMMON_H */
