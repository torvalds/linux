/*
 * Host AP (software wireless LAN access point) user space daemon for
 * Host AP kernel driver
 * Copyright 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2002-2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef HOSTAPD_IOCTL_H
#define HOSTAPD_IOCTL_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif /* __KERNEL__ */

#define PRISM2_IOCTL_PRISM2_PARAM (SIOCIWFIRSTPRIV + 0)
#define PRISM2_IOCTL_GET_PRISM2_PARAM (SIOCIWFIRSTPRIV + 1)
#define PRISM2_IOCTL_HOSTAPD (SIOCIWFIRSTPRIV + 3)

/* PRISM2_IOCTL_PRISM2_PARAM ioctl() subtypes:
 * This table is no longer added to, the whole sub-ioctl
 * mess shall be deleted completely. */
enum {
	PRISM2_PARAM_IEEE_802_1X = 23,

	/* Instant802 additions */
	PRISM2_PARAM_CTS_PROTECT_ERP_FRAMES = 1001,
	PRISM2_PARAM_PREAMBLE = 1003,
	PRISM2_PARAM_SHORT_SLOT_TIME = 1006,
	PRISM2_PARAM_NEXT_MODE = 1008,
	PRISM2_PARAM_RADIO_ENABLED = 1010,
	PRISM2_PARAM_ANTENNA_MODE = 1013,
	PRISM2_PARAM_STAT_TIME = 1016,
	PRISM2_PARAM_STA_ANTENNA_SEL = 1017,
	PRISM2_PARAM_TX_POWER_REDUCTION = 1022,
	PRISM2_PARAM_KEY_TX_RX_THRESHOLD = 1024,
	PRISM2_PARAM_DEFAULT_WEP_ONLY = 1026,
	PRISM2_PARAM_WIFI_WME_NOACK_TEST = 1033,
	PRISM2_PARAM_SCAN_FLAGS = 1035,
	PRISM2_PARAM_HW_MODES = 1036,
	PRISM2_PARAM_CREATE_IBSS = 1037,
	PRISM2_PARAM_WMM_ENABLED = 1038,
	PRISM2_PARAM_MIXED_CELL = 1039,
	PRISM2_PARAM_RADAR_DETECT = 1043,
	PRISM2_PARAM_SPECTRUM_MGMT = 1044,
};

enum {
	IEEE80211_KEY_MGMT_NONE = 0,
	IEEE80211_KEY_MGMT_IEEE8021X = 1,
	IEEE80211_KEY_MGMT_WPA_PSK = 2,
	IEEE80211_KEY_MGMT_WPA_EAP = 3,
};


/* Data structures used for get_hw_features ioctl */
struct hostapd_ioctl_hw_modes_hdr {
	int mode;
	int num_channels;
	int num_rates;
};

struct ieee80211_channel_data {
	short chan; /* channel number (IEEE 802.11) */
	short freq; /* frequency in MHz */
	int flag; /* flag for hostapd use (IEEE80211_CHAN_*) */
};

struct ieee80211_rate_data {
	int rate; /* rate in 100 kbps */
	int flags; /* IEEE80211_RATE_ flags */
};


/* ADD_IF, REMOVE_IF, and UPDATE_IF 'type' argument */
enum {
	HOSTAP_IF_WDS = 1, HOSTAP_IF_VLAN = 2, HOSTAP_IF_BSS = 3,
	HOSTAP_IF_STA = 4
};

struct hostapd_if_wds {
	u8 remote_addr[ETH_ALEN];
};

struct hostapd_if_vlan {
	u8 id;
};

struct hostapd_if_bss {
	u8 bssid[ETH_ALEN];
};

struct hostapd_if_sta {
};

#endif /* HOSTAPD_IOCTL_H */
