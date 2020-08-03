/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __NET_CFG80211_H
#define __NET_CFG80211_H
/*
 * 802.11 device and configuration interface
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014 Intel Mobile Communications GmbH
 * Copyright 2015-2017	Intel Deutschland GmbH
 * Copyright (C) 2018-2020 Intel Corporation
 */

#include <linux/netdevice.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/nl80211.h>
#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <linux/net.h>
#include <net/regulatory.h>

/**
 * DOC: Introduction
 *
 * cfg80211 is the configuration API for 802.11 devices in Linux. It bridges
 * userspace and drivers, and offers some utility functionality associated
 * with 802.11. cfg80211 must, directly or indirectly via mac80211, be used
 * by all modern wireless drivers in Linux, so that they offer a consistent
 * API through nl80211. For backward compatibility, cfg80211 also offers
 * wireless extensions to userspace, but hides them from drivers completely.
 *
 * Additionally, cfg80211 contains code to help enforce regulatory spectrum
 * use restrictions.
 */


/**
 * DOC: Device registration
 *
 * In order for a driver to use cfg80211, it must register the hardware device
 * with cfg80211. This happens through a number of hardware capability structs
 * described below.
 *
 * The fundamental structure for each device is the 'wiphy', of which each
 * instance describes a physical wireless device connected to the system. Each
 * such wiphy can have zero, one, or many virtual interfaces associated with
 * it, which need to be identified as such by pointing the network interface's
 * @ieee80211_ptr pointer to a &struct wireless_dev which further describes
 * the wireless part of the interface, normally this struct is embedded in the
 * network interface's private data area. Drivers can optionally allow creating
 * or destroying virtual interfaces on the fly, but without at least one or the
 * ability to create some the wireless device isn't useful.
 *
 * Each wiphy structure contains device capability information, and also has
 * a pointer to the various operations the driver offers. The definitions and
 * structures here describe these capabilities in detail.
 */

struct wiphy;

/*
 * wireless hardware capability structures
 */

/**
 * enum ieee80211_channel_flags - channel flags
 *
 * Channel flags set by the regulatory control code.
 *
 * @IEEE80211_CHAN_DISABLED: This channel is disabled.
 * @IEEE80211_CHAN_NO_IR: do not initiate radiation, this includes
 *	sending probe requests or beaconing.
 * @IEEE80211_CHAN_RADAR: Radar detection is required on this channel.
 * @IEEE80211_CHAN_NO_HT40PLUS: extension channel above this channel
 *	is not permitted.
 * @IEEE80211_CHAN_NO_HT40MINUS: extension channel below this channel
 *	is not permitted.
 * @IEEE80211_CHAN_NO_OFDM: OFDM is not allowed on this channel.
 * @IEEE80211_CHAN_NO_80MHZ: If the driver supports 80 MHz on the band,
 *	this flag indicates that an 80 MHz channel cannot use this
 *	channel as the control or any of the secondary channels.
 *	This may be due to the driver or due to regulatory bandwidth
 *	restrictions.
 * @IEEE80211_CHAN_NO_160MHZ: If the driver supports 160 MHz on the band,
 *	this flag indicates that an 160 MHz channel cannot use this
 *	channel as the control or any of the secondary channels.
 *	This may be due to the driver or due to regulatory bandwidth
 *	restrictions.
 * @IEEE80211_CHAN_INDOOR_ONLY: see %NL80211_FREQUENCY_ATTR_INDOOR_ONLY
 * @IEEE80211_CHAN_IR_CONCURRENT: see %NL80211_FREQUENCY_ATTR_IR_CONCURRENT
 * @IEEE80211_CHAN_NO_20MHZ: 20 MHz bandwidth is not permitted
 *	on this channel.
 * @IEEE80211_CHAN_NO_10MHZ: 10 MHz bandwidth is not permitted
 *	on this channel.
 * @IEEE80211_CHAN_NO_HE: HE operation is not permitted on this channel.
 *
 */
enum ieee80211_channel_flags {
	IEEE80211_CHAN_DISABLED		= 1<<0,
	IEEE80211_CHAN_NO_IR		= 1<<1,
	/* hole at 1<<2 */
	IEEE80211_CHAN_RADAR		= 1<<3,
	IEEE80211_CHAN_NO_HT40PLUS	= 1<<4,
	IEEE80211_CHAN_NO_HT40MINUS	= 1<<5,
	IEEE80211_CHAN_NO_OFDM		= 1<<6,
	IEEE80211_CHAN_NO_80MHZ		= 1<<7,
	IEEE80211_CHAN_NO_160MHZ	= 1<<8,
	IEEE80211_CHAN_INDOOR_ONLY	= 1<<9,
	IEEE80211_CHAN_IR_CONCURRENT	= 1<<10,
	IEEE80211_CHAN_NO_20MHZ		= 1<<11,
	IEEE80211_CHAN_NO_10MHZ		= 1<<12,
	IEEE80211_CHAN_NO_HE		= 1<<13,
};

#define IEEE80211_CHAN_NO_HT40 \
	(IEEE80211_CHAN_NO_HT40PLUS | IEEE80211_CHAN_NO_HT40MINUS)

#define IEEE80211_DFS_MIN_CAC_TIME_MS		60000
#define IEEE80211_DFS_MIN_NOP_TIME_MS		(30 * 60 * 1000)

/**
 * struct ieee80211_channel - channel definition
 *
 * This structure describes a single channel for use
 * with cfg80211.
 *
 * @center_freq: center frequency in MHz
 * @freq_offset: offset from @center_freq, in KHz
 * @hw_value: hardware-specific value for the channel
 * @flags: channel flags from &enum ieee80211_channel_flags.
 * @orig_flags: channel flags at registration time, used by regulatory
 *	code to support devices with additional restrictions
 * @band: band this channel belongs to.
 * @max_antenna_gain: maximum antenna gain in dBi
 * @max_power: maximum transmission power (in dBm)
 * @max_reg_power: maximum regulatory transmission power (in dBm)
 * @beacon_found: helper to regulatory code to indicate when a beacon
 *	has been found on this channel. Use regulatory_hint_found_beacon()
 *	to enable this, this is useful only on 5 GHz band.
 * @orig_mag: internal use
 * @orig_mpwr: internal use
 * @dfs_state: current state of this channel. Only relevant if radar is required
 *	on this channel.
 * @dfs_state_entered: timestamp (jiffies) when the dfs state was entered.
 * @dfs_cac_ms: DFS CAC time in milliseconds, this is valid for DFS channels.
 */
struct ieee80211_channel {
	enum nl80211_band band;
	u32 center_freq;
	u16 freq_offset;
	u16 hw_value;
	u32 flags;
	int max_antenna_gain;
	int max_power;
	int max_reg_power;
	bool beacon_found;
	u32 orig_flags;
	int orig_mag, orig_mpwr;
	enum nl80211_dfs_state dfs_state;
	unsigned long dfs_state_entered;
	unsigned int dfs_cac_ms;
};

/**
 * enum ieee80211_rate_flags - rate flags
 *
 * Hardware/specification flags for rates. These are structured
 * in a way that allows using the same bitrate structure for
 * different bands/PHY modes.
 *
 * @IEEE80211_RATE_SHORT_PREAMBLE: Hardware can send with short
 *	preamble on this bitrate; only relevant in 2.4GHz band and
 *	with CCK rates.
 * @IEEE80211_RATE_MANDATORY_A: This bitrate is a mandatory rate
 *	when used with 802.11a (on the 5 GHz band); filled by the
 *	core code when registering the wiphy.
 * @IEEE80211_RATE_MANDATORY_B: This bitrate is a mandatory rate
 *	when used with 802.11b (on the 2.4 GHz band); filled by the
 *	core code when registering the wiphy.
 * @IEEE80211_RATE_MANDATORY_G: This bitrate is a mandatory rate
 *	when used with 802.11g (on the 2.4 GHz band); filled by the
 *	core code when registering the wiphy.
 * @IEEE80211_RATE_ERP_G: This is an ERP rate in 802.11g mode.
 * @IEEE80211_RATE_SUPPORTS_5MHZ: Rate can be used in 5 MHz mode
 * @IEEE80211_RATE_SUPPORTS_10MHZ: Rate can be used in 10 MHz mode
 */
enum ieee80211_rate_flags {
	IEEE80211_RATE_SHORT_PREAMBLE	= 1<<0,
	IEEE80211_RATE_MANDATORY_A	= 1<<1,
	IEEE80211_RATE_MANDATORY_B	= 1<<2,
	IEEE80211_RATE_MANDATORY_G	= 1<<3,
	IEEE80211_RATE_ERP_G		= 1<<4,
	IEEE80211_RATE_SUPPORTS_5MHZ	= 1<<5,
	IEEE80211_RATE_SUPPORTS_10MHZ	= 1<<6,
};

/**
 * enum ieee80211_bss_type - BSS type filter
 *
 * @IEEE80211_BSS_TYPE_ESS: Infrastructure BSS
 * @IEEE80211_BSS_TYPE_PBSS: Personal BSS
 * @IEEE80211_BSS_TYPE_IBSS: Independent BSS
 * @IEEE80211_BSS_TYPE_MBSS: Mesh BSS
 * @IEEE80211_BSS_TYPE_ANY: Wildcard value for matching any BSS type
 */
enum ieee80211_bss_type {
	IEEE80211_BSS_TYPE_ESS,
	IEEE80211_BSS_TYPE_PBSS,
	IEEE80211_BSS_TYPE_IBSS,
	IEEE80211_BSS_TYPE_MBSS,
	IEEE80211_BSS_TYPE_ANY
};

/**
 * enum ieee80211_privacy - BSS privacy filter
 *
 * @IEEE80211_PRIVACY_ON: privacy bit set
 * @IEEE80211_PRIVACY_OFF: privacy bit clear
 * @IEEE80211_PRIVACY_ANY: Wildcard value for matching any privacy setting
 */
enum ieee80211_privacy {
	IEEE80211_PRIVACY_ON,
	IEEE80211_PRIVACY_OFF,
	IEEE80211_PRIVACY_ANY
};

#define IEEE80211_PRIVACY(x)	\
	((x) ? IEEE80211_PRIVACY_ON : IEEE80211_PRIVACY_OFF)

/**
 * struct ieee80211_rate - bitrate definition
 *
 * This structure describes a bitrate that an 802.11 PHY can
 * operate with. The two values @hw_value and @hw_value_short
 * are only for driver use when pointers to this structure are
 * passed around.
 *
 * @flags: rate-specific flags
 * @bitrate: bitrate in units of 100 Kbps
 * @hw_value: driver/hardware value for this rate
 * @hw_value_short: driver/hardware value for this rate when
 *	short preamble is used
 */
struct ieee80211_rate {
	u32 flags;
	u16 bitrate;
	u16 hw_value, hw_value_short;
};

/**
 * struct ieee80211_he_obss_pd - AP settings for spatial reuse
 *
 * @enable: is the feature enabled.
 * @min_offset: minimal tx power offset an associated station shall use
 * @max_offset: maximum tx power offset an associated station shall use
 */
struct ieee80211_he_obss_pd {
	bool enable;
	u8 min_offset;
	u8 max_offset;
};

/**
 * struct cfg80211_he_bss_color - AP settings for BSS coloring
 *
 * @color: the current color.
 * @disabled: is the feature disabled.
 * @partial: define the AID equation.
 */
struct cfg80211_he_bss_color {
	u8 color;
	bool disabled;
	bool partial;
};

/**
 * struct ieee80211_he_bss_color - AP settings for BSS coloring
 *
 * @color: the current color.
 * @disabled: is the feature disabled.
 * @partial: define the AID equation.
 */
struct ieee80211_he_bss_color {
	u8 color;
	bool disabled;
	bool partial;
};

/**
 * struct ieee80211_sta_ht_cap - STA's HT capabilities
 *
 * This structure describes most essential parameters needed
 * to describe 802.11n HT capabilities for an STA.
 *
 * @ht_supported: is HT supported by the STA
 * @cap: HT capabilities map as described in 802.11n spec
 * @ampdu_factor: Maximum A-MPDU length factor
 * @ampdu_density: Minimum A-MPDU spacing
 * @mcs: Supported MCS rates
 */
struct ieee80211_sta_ht_cap {
	u16 cap; /* use IEEE80211_HT_CAP_ */
	bool ht_supported;
	u8 ampdu_factor;
	u8 ampdu_density;
	struct ieee80211_mcs_info mcs;
};

/**
 * struct ieee80211_sta_vht_cap - STA's VHT capabilities
 *
 * This structure describes most essential parameters needed
 * to describe 802.11ac VHT capabilities for an STA.
 *
 * @vht_supported: is VHT supported by the STA
 * @cap: VHT capabilities map as described in 802.11ac spec
 * @vht_mcs: Supported VHT MCS rates
 */
struct ieee80211_sta_vht_cap {
	bool vht_supported;
	u32 cap; /* use IEEE80211_VHT_CAP_ */
	struct ieee80211_vht_mcs_info vht_mcs;
};

#define IEEE80211_HE_PPE_THRES_MAX_LEN		25

/**
 * struct ieee80211_sta_he_cap - STA's HE capabilities
 *
 * This structure describes most essential parameters needed
 * to describe 802.11ax HE capabilities for a STA.
 *
 * @has_he: true iff HE data is valid.
 * @he_cap_elem: Fixed portion of the HE capabilities element.
 * @he_mcs_nss_supp: The supported NSS/MCS combinations.
 * @ppe_thres: Holds the PPE Thresholds data.
 */
struct ieee80211_sta_he_cap {
	bool has_he;
	struct ieee80211_he_cap_elem he_cap_elem;
	struct ieee80211_he_mcs_nss_supp he_mcs_nss_supp;
	u8 ppe_thres[IEEE80211_HE_PPE_THRES_MAX_LEN];
};

/**
 * struct ieee80211_sband_iftype_data
 *
 * This structure encapsulates sband data that is relevant for the
 * interface types defined in @types_mask.  Each type in the
 * @types_mask must be unique across all instances of iftype_data.
 *
 * @types_mask: interface types mask
 * @he_cap: holds the HE capabilities
 * @he_6ghz_capa: HE 6 GHz capabilities, must be filled in for a
 *	6 GHz band channel (and 0 may be valid value).
 */
struct ieee80211_sband_iftype_data {
	u16 types_mask;
	struct ieee80211_sta_he_cap he_cap;
	struct ieee80211_he_6ghz_capa he_6ghz_capa;
};

/**
 * enum ieee80211_edmg_bw_config - allowed channel bandwidth configurations
 *
 * @IEEE80211_EDMG_BW_CONFIG_4: 2.16GHz
 * @IEEE80211_EDMG_BW_CONFIG_5: 2.16GHz and 4.32GHz
 * @IEEE80211_EDMG_BW_CONFIG_6: 2.16GHz, 4.32GHz and 6.48GHz
 * @IEEE80211_EDMG_BW_CONFIG_7: 2.16GHz, 4.32GHz, 6.48GHz and 8.64GHz
 * @IEEE80211_EDMG_BW_CONFIG_8: 2.16GHz and 2.16GHz + 2.16GHz
 * @IEEE80211_EDMG_BW_CONFIG_9: 2.16GHz, 4.32GHz and 2.16GHz + 2.16GHz
 * @IEEE80211_EDMG_BW_CONFIG_10: 2.16GHz, 4.32GHz, 6.48GHz and 2.16GHz+2.16GHz
 * @IEEE80211_EDMG_BW_CONFIG_11: 2.16GHz, 4.32GHz, 6.48GHz, 8.64GHz and
 *	2.16GHz+2.16GHz
 * @IEEE80211_EDMG_BW_CONFIG_12: 2.16GHz, 2.16GHz + 2.16GHz and
 *	4.32GHz + 4.32GHz
 * @IEEE80211_EDMG_BW_CONFIG_13: 2.16GHz, 4.32GHz, 2.16GHz + 2.16GHz and
 *	4.32GHz + 4.32GHz
 * @IEEE80211_EDMG_BW_CONFIG_14: 2.16GHz, 4.32GHz, 6.48GHz, 2.16GHz + 2.16GHz
 *	and 4.32GHz + 4.32GHz
 * @IEEE80211_EDMG_BW_CONFIG_15: 2.16GHz, 4.32GHz, 6.48GHz, 8.64GHz,
 *	2.16GHz + 2.16GHz and 4.32GHz + 4.32GHz
 */
enum ieee80211_edmg_bw_config {
	IEEE80211_EDMG_BW_CONFIG_4	= 4,
	IEEE80211_EDMG_BW_CONFIG_5	= 5,
	IEEE80211_EDMG_BW_CONFIG_6	= 6,
	IEEE80211_EDMG_BW_CONFIG_7	= 7,
	IEEE80211_EDMG_BW_CONFIG_8	= 8,
	IEEE80211_EDMG_BW_CONFIG_9	= 9,
	IEEE80211_EDMG_BW_CONFIG_10	= 10,
	IEEE80211_EDMG_BW_CONFIG_11	= 11,
	IEEE80211_EDMG_BW_CONFIG_12	= 12,
	IEEE80211_EDMG_BW_CONFIG_13	= 13,
	IEEE80211_EDMG_BW_CONFIG_14	= 14,
	IEEE80211_EDMG_BW_CONFIG_15	= 15,
};

/**
 * struct ieee80211_edmg - EDMG configuration
 *
 * This structure describes most essential parameters needed
 * to describe 802.11ay EDMG configuration
 *
 * @channels: bitmap that indicates the 2.16 GHz channel(s)
 *	that are allowed to be used for transmissions.
 *	Bit 0 indicates channel 1, bit 1 indicates channel 2, etc.
 *	Set to 0 indicate EDMG not supported.
 * @bw_config: Channel BW Configuration subfield encodes
 *	the allowed channel bandwidth configurations
 */
struct ieee80211_edmg {
	u8 channels;
	enum ieee80211_edmg_bw_config bw_config;
};

/**
 * struct ieee80211_supported_band - frequency band definition
 *
 * This structure describes a frequency band a wiphy
 * is able to operate in.
 *
 * @channels: Array of channels the hardware can operate in
 *	in this band.
 * @band: the band this structure represents
 * @n_channels: Number of channels in @channels
 * @bitrates: Array of bitrates the hardware can operate with
 *	in this band. Must be sorted to give a valid "supported
 *	rates" IE, i.e. CCK rates first, then OFDM.
 * @n_bitrates: Number of bitrates in @bitrates
 * @ht_cap: HT capabilities in this band
 * @vht_cap: VHT capabilities in this band
 * @edmg_cap: EDMG capabilities in this band
 * @n_iftype_data: number of iftype data entries
 * @iftype_data: interface type data entries.  Note that the bits in
 *	@types_mask inside this structure cannot overlap (i.e. only
 *	one occurrence of each type is allowed across all instances of
 *	iftype_data).
 */
struct ieee80211_supported_band {
	struct ieee80211_channel *channels;
	struct ieee80211_rate *bitrates;
	enum nl80211_band band;
	int n_channels;
	int n_bitrates;
	struct ieee80211_sta_ht_cap ht_cap;
	struct ieee80211_sta_vht_cap vht_cap;
	struct ieee80211_edmg edmg_cap;
	u16 n_iftype_data;
	const struct ieee80211_sband_iftype_data *iftype_data;
};

/**
 * ieee80211_get_sband_iftype_data - return sband data for a given iftype
 * @sband: the sband to search for the STA on
 * @iftype: enum nl80211_iftype
 *
 * Return: pointer to struct ieee80211_sband_iftype_data, or NULL is none found
 */
static inline const struct ieee80211_sband_iftype_data *
ieee80211_get_sband_iftype_data(const struct ieee80211_supported_band *sband,
				u8 iftype)
{
	int i;

	if (WARN_ON(iftype >= NL80211_IFTYPE_MAX))
		return NULL;

	for (i = 0; i < sband->n_iftype_data; i++)  {
		const struct ieee80211_sband_iftype_data *data =
			&sband->iftype_data[i];

		if (data->types_mask & BIT(iftype))
			return data;
	}

	return NULL;
}

/**
 * ieee80211_get_he_iftype_cap - return HE capabilities for an sband's iftype
 * @sband: the sband to search for the iftype on
 * @iftype: enum nl80211_iftype
 *
 * Return: pointer to the struct ieee80211_sta_he_cap, or NULL is none found
 */
static inline const struct ieee80211_sta_he_cap *
ieee80211_get_he_iftype_cap(const struct ieee80211_supported_band *sband,
			    u8 iftype)
{
	const struct ieee80211_sband_iftype_data *data =
		ieee80211_get_sband_iftype_data(sband, iftype);

	if (data && data->he_cap.has_he)
		return &data->he_cap;

	return NULL;
}

/**
 * ieee80211_get_he_sta_cap - return HE capabilities for an sband's STA
 * @sband: the sband to search for the STA on
 *
 * Return: pointer to the struct ieee80211_sta_he_cap, or NULL is none found
 */
static inline const struct ieee80211_sta_he_cap *
ieee80211_get_he_sta_cap(const struct ieee80211_supported_band *sband)
{
	return ieee80211_get_he_iftype_cap(sband, NL80211_IFTYPE_STATION);
}

/**
 * ieee80211_get_he_6ghz_capa - return HE 6 GHz capabilities
 * @sband: the sband to search for the STA on
 * @iftype: the iftype to search for
 *
 * Return: the 6GHz capabilities
 */
static inline __le16
ieee80211_get_he_6ghz_capa(const struct ieee80211_supported_band *sband,
			   enum nl80211_iftype iftype)
{
	const struct ieee80211_sband_iftype_data *data =
		ieee80211_get_sband_iftype_data(sband, iftype);

	if (WARN_ON(!data || !data->he_cap.has_he))
		return 0;

	return data->he_6ghz_capa.capa;
}

/**
 * wiphy_read_of_freq_limits - read frequency limits from device tree
 *
 * @wiphy: the wireless device to get extra limits for
 *
 * Some devices may have extra limitations specified in DT. This may be useful
 * for chipsets that normally support more bands but are limited due to board
 * design (e.g. by antennas or external power amplifier).
 *
 * This function reads info from DT and uses it to *modify* channels (disable
 * unavailable ones). It's usually a *bad* idea to use it in drivers with
 * shared channel data as DT limitations are device specific. You should make
 * sure to call it only if channels in wiphy are copied and can be modified
 * without affecting other devices.
 *
 * As this function access device node it has to be called after set_wiphy_dev.
 * It also modifies channels so they have to be set first.
 * If using this helper, call it before wiphy_register().
 */
#ifdef CONFIG_OF
void wiphy_read_of_freq_limits(struct wiphy *wiphy);
#else /* CONFIG_OF */
static inline void wiphy_read_of_freq_limits(struct wiphy *wiphy)
{
}
#endif /* !CONFIG_OF */


/*
 * Wireless hardware/device configuration structures and methods
 */

/**
 * DOC: Actions and configuration
 *
 * Each wireless device and each virtual interface offer a set of configuration
 * operations and other actions that are invoked by userspace. Each of these
 * actions is described in the operations structure, and the parameters these
 * operations use are described separately.
 *
 * Additionally, some operations are asynchronous and expect to get status
 * information via some functions that drivers need to call.
 *
 * Scanning and BSS list handling with its associated functionality is described
 * in a separate chapter.
 */

#define VHT_MUMIMO_GROUPS_DATA_LEN (WLAN_MEMBERSHIP_LEN +\
				    WLAN_USER_POSITION_LEN)

/**
 * struct vif_params - describes virtual interface parameters
 * @flags: monitor interface flags, unchanged if 0, otherwise
 *	%MONITOR_FLAG_CHANGED will be set
 * @use_4addr: use 4-address frames
 * @macaddr: address to use for this virtual interface.
 *	If this parameter is set to zero address the driver may
 *	determine the address as needed.
 *	This feature is only fully supported by drivers that enable the
 *	%NL80211_FEATURE_MAC_ON_CREATE flag.  Others may support creating
 **	only p2p devices with specified MAC.
 * @vht_mumimo_groups: MU-MIMO groupID, used for monitoring MU-MIMO packets
 *	belonging to that MU-MIMO groupID; %NULL if not changed
 * @vht_mumimo_follow_addr: MU-MIMO follow address, used for monitoring
 *	MU-MIMO packets going to the specified station; %NULL if not changed
 */
struct vif_params {
	u32 flags;
	int use_4addr;
	u8 macaddr[ETH_ALEN];
	const u8 *vht_mumimo_groups;
	const u8 *vht_mumimo_follow_addr;
};

/**
 * struct key_params - key information
 *
 * Information about a key
 *
 * @key: key material
 * @key_len: length of key material
 * @cipher: cipher suite selector
 * @seq: sequence counter (IV/PN) for TKIP and CCMP keys, only used
 *	with the get_key() callback, must be in little endian,
 *	length given by @seq_len.
 * @seq_len: length of @seq.
 * @vlan_id: vlan_id for VLAN group key (if nonzero)
 * @mode: key install mode (RX_TX, NO_TX or SET_TX)
 */
struct key_params {
	const u8 *key;
	const u8 *seq;
	int key_len;
	int seq_len;
	u16 vlan_id;
	u32 cipher;
	enum nl80211_key_mode mode;
};

/**
 * struct cfg80211_chan_def - channel definition
 * @chan: the (control) channel
 * @width: channel width
 * @center_freq1: center frequency of first segment
 * @center_freq2: center frequency of second segment
 *	(only with 80+80 MHz)
 * @edmg: define the EDMG channels configuration.
 *	If edmg is requested (i.e. the .channels member is non-zero),
 *	chan will define the primary channel and all other
 *	parameters are ignored.
 * @freq1_offset: offset from @center_freq1, in KHz
 */
struct cfg80211_chan_def {
	struct ieee80211_channel *chan;
	enum nl80211_chan_width width;
	u32 center_freq1;
	u32 center_freq2;
	struct ieee80211_edmg edmg;
	u16 freq1_offset;
};

/*
 * cfg80211_bitrate_mask - masks for bitrate control
 */
struct cfg80211_bitrate_mask {
	struct {
		u32 legacy;
		u8 ht_mcs[IEEE80211_HT_MCS_MASK_LEN];
		u16 vht_mcs[NL80211_VHT_NSS_MAX];
		enum nl80211_txrate_gi gi;
	} control[NUM_NL80211_BANDS];
};


/**
 * struct cfg80211_tid_cfg - TID specific configuration
 * @config_override: Flag to notify driver to reset TID configuration
 *	of the peer.
 * @tids: bitmap of TIDs to modify
 * @mask: bitmap of attributes indicating which parameter changed,
 *	similar to &nl80211_tid_config_supp.
 * @noack: noack configuration value for the TID
 * @retry_long: retry count value
 * @retry_short: retry count value
 * @ampdu: Enable/Disable MPDU aggregation
 * @rtscts: Enable/Disable RTS/CTS
 * @amsdu: Enable/Disable MSDU aggregation
 * @txrate_type: Tx bitrate mask type
 * @txrate_mask: Tx bitrate to be applied for the TID
 */
struct cfg80211_tid_cfg {
	bool config_override;
	u8 tids;
	u64 mask;
	enum nl80211_tid_config noack;
	u8 retry_long, retry_short;
	enum nl80211_tid_config ampdu;
	enum nl80211_tid_config rtscts;
	enum nl80211_tid_config amsdu;
	enum nl80211_tx_rate_setting txrate_type;
	struct cfg80211_bitrate_mask txrate_mask;
};

/**
 * struct cfg80211_tid_config - TID configuration
 * @peer: Station's MAC address
 * @n_tid_conf: Number of TID specific configurations to be applied
 * @tid_conf: Configuration change info
 */
struct cfg80211_tid_config {
	const u8 *peer;
	u32 n_tid_conf;
	struct cfg80211_tid_cfg tid_conf[];
};

/**
 * cfg80211_get_chandef_type - return old channel type from chandef
 * @chandef: the channel definition
 *
 * Return: The old channel type (NOHT, HT20, HT40+/-) from a given
 * chandef, which must have a bandwidth allowing this conversion.
 */
static inline enum nl80211_channel_type
cfg80211_get_chandef_type(const struct cfg80211_chan_def *chandef)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		return NL80211_CHAN_NO_HT;
	case NL80211_CHAN_WIDTH_20:
		return NL80211_CHAN_HT20;
	case NL80211_CHAN_WIDTH_40:
		if (chandef->center_freq1 > chandef->chan->center_freq)
			return NL80211_CHAN_HT40PLUS;
		return NL80211_CHAN_HT40MINUS;
	default:
		WARN_ON(1);
		return NL80211_CHAN_NO_HT;
	}
}

/**
 * cfg80211_chandef_create - create channel definition using channel type
 * @chandef: the channel definition struct to fill
 * @channel: the control channel
 * @chantype: the channel type
 *
 * Given a channel type, create a channel definition.
 */
void cfg80211_chandef_create(struct cfg80211_chan_def *chandef,
			     struct ieee80211_channel *channel,
			     enum nl80211_channel_type chantype);

/**
 * cfg80211_chandef_identical - check if two channel definitions are identical
 * @chandef1: first channel definition
 * @chandef2: second channel definition
 *
 * Return: %true if the channels defined by the channel definitions are
 * identical, %false otherwise.
 */
static inline bool
cfg80211_chandef_identical(const struct cfg80211_chan_def *chandef1,
			   const struct cfg80211_chan_def *chandef2)
{
	return (chandef1->chan == chandef2->chan &&
		chandef1->width == chandef2->width &&
		chandef1->center_freq1 == chandef2->center_freq1 &&
		chandef1->freq1_offset == chandef2->freq1_offset &&
		chandef1->center_freq2 == chandef2->center_freq2);
}

/**
 * cfg80211_chandef_is_edmg - check if chandef represents an EDMG channel
 *
 * @chandef: the channel definition
 *
 * Return: %true if EDMG defined, %false otherwise.
 */
static inline bool
cfg80211_chandef_is_edmg(const struct cfg80211_chan_def *chandef)
{
	return chandef->edmg.channels || chandef->edmg.bw_config;
}

/**
 * cfg80211_chandef_compatible - check if two channel definitions are compatible
 * @chandef1: first channel definition
 * @chandef2: second channel definition
 *
 * Return: %NULL if the given channel definitions are incompatible,
 * chandef1 or chandef2 otherwise.
 */
const struct cfg80211_chan_def *
cfg80211_chandef_compatible(const struct cfg80211_chan_def *chandef1,
			    const struct cfg80211_chan_def *chandef2);

/**
 * cfg80211_chandef_valid - check if a channel definition is valid
 * @chandef: the channel definition to check
 * Return: %true if the channel definition is valid. %false otherwise.
 */
bool cfg80211_chandef_valid(const struct cfg80211_chan_def *chandef);

/**
 * cfg80211_chandef_usable - check if secondary channels can be used
 * @wiphy: the wiphy to validate against
 * @chandef: the channel definition to check
 * @prohibited_flags: the regulatory channel flags that must not be set
 * Return: %true if secondary channels are usable. %false otherwise.
 */
bool cfg80211_chandef_usable(struct wiphy *wiphy,
			     const struct cfg80211_chan_def *chandef,
			     u32 prohibited_flags);

/**
 * cfg80211_chandef_dfs_required - checks if radar detection is required
 * @wiphy: the wiphy to validate against
 * @chandef: the channel definition to check
 * @iftype: the interface type as specified in &enum nl80211_iftype
 * Returns:
 *	1 if radar detection is required, 0 if it is not, < 0 on error
 */
int cfg80211_chandef_dfs_required(struct wiphy *wiphy,
				  const struct cfg80211_chan_def *chandef,
				  enum nl80211_iftype iftype);

/**
 * ieee80211_chandef_rate_flags - returns rate flags for a channel
 *
 * In some channel types, not all rates may be used - for example CCK
 * rates may not be used in 5/10 MHz channels.
 *
 * @chandef: channel definition for the channel
 *
 * Returns: rate flags which apply for this channel
 */
static inline enum ieee80211_rate_flags
ieee80211_chandef_rate_flags(struct cfg80211_chan_def *chandef)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_5:
		return IEEE80211_RATE_SUPPORTS_5MHZ;
	case NL80211_CHAN_WIDTH_10:
		return IEEE80211_RATE_SUPPORTS_10MHZ;
	default:
		break;
	}
	return 0;
}

/**
 * ieee80211_chandef_max_power - maximum transmission power for the chandef
 *
 * In some regulations, the transmit power may depend on the configured channel
 * bandwidth which may be defined as dBm/MHz. This function returns the actual
 * max_power for non-standard (20 MHz) channels.
 *
 * @chandef: channel definition for the channel
 *
 * Returns: maximum allowed transmission power in dBm for the chandef
 */
static inline int
ieee80211_chandef_max_power(struct cfg80211_chan_def *chandef)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_5:
		return min(chandef->chan->max_reg_power - 6,
			   chandef->chan->max_power);
	case NL80211_CHAN_WIDTH_10:
		return min(chandef->chan->max_reg_power - 3,
			   chandef->chan->max_power);
	default:
		break;
	}
	return chandef->chan->max_power;
}

/**
 * enum survey_info_flags - survey information flags
 *
 * @SURVEY_INFO_NOISE_DBM: noise (in dBm) was filled in
 * @SURVEY_INFO_IN_USE: channel is currently being used
 * @SURVEY_INFO_TIME: active time (in ms) was filled in
 * @SURVEY_INFO_TIME_BUSY: busy time was filled in
 * @SURVEY_INFO_TIME_EXT_BUSY: extension channel busy time was filled in
 * @SURVEY_INFO_TIME_RX: receive time was filled in
 * @SURVEY_INFO_TIME_TX: transmit time was filled in
 * @SURVEY_INFO_TIME_SCAN: scan time was filled in
 * @SURVEY_INFO_TIME_BSS_RX: local BSS receive time was filled in
 *
 * Used by the driver to indicate which info in &struct survey_info
 * it has filled in during the get_survey().
 */
enum survey_info_flags {
	SURVEY_INFO_NOISE_DBM		= BIT(0),
	SURVEY_INFO_IN_USE		= BIT(1),
	SURVEY_INFO_TIME		= BIT(2),
	SURVEY_INFO_TIME_BUSY		= BIT(3),
	SURVEY_INFO_TIME_EXT_BUSY	= BIT(4),
	SURVEY_INFO_TIME_RX		= BIT(5),
	SURVEY_INFO_TIME_TX		= BIT(6),
	SURVEY_INFO_TIME_SCAN		= BIT(7),
	SURVEY_INFO_TIME_BSS_RX		= BIT(8),
};

/**
 * struct survey_info - channel survey response
 *
 * @channel: the channel this survey record reports, may be %NULL for a single
 *	record to report global statistics
 * @filled: bitflag of flags from &enum survey_info_flags
 * @noise: channel noise in dBm. This and all following fields are
 *	optional
 * @time: amount of time in ms the radio was turn on (on the channel)
 * @time_busy: amount of time the primary channel was sensed busy
 * @time_ext_busy: amount of time the extension channel was sensed busy
 * @time_rx: amount of time the radio spent receiving data
 * @time_tx: amount of time the radio spent transmitting data
 * @time_scan: amount of time the radio spent for scanning
 * @time_bss_rx: amount of time the radio spent receiving data on a local BSS
 *
 * Used by dump_survey() to report back per-channel survey information.
 *
 * This structure can later be expanded with things like
 * channel duty cycle etc.
 */
struct survey_info {
	struct ieee80211_channel *channel;
	u64 time;
	u64 time_busy;
	u64 time_ext_busy;
	u64 time_rx;
	u64 time_tx;
	u64 time_scan;
	u64 time_bss_rx;
	u32 filled;
	s8 noise;
};

#define CFG80211_MAX_WEP_KEYS	4

/**
 * struct cfg80211_crypto_settings - Crypto settings
 * @wpa_versions: indicates which, if any, WPA versions are enabled
 *	(from enum nl80211_wpa_versions)
 * @cipher_group: group key cipher suite (or 0 if unset)
 * @n_ciphers_pairwise: number of AP supported unicast ciphers
 * @ciphers_pairwise: unicast key cipher suites
 * @n_akm_suites: number of AKM suites
 * @akm_suites: AKM suites
 * @control_port: Whether user space controls IEEE 802.1X port, i.e.,
 *	sets/clears %NL80211_STA_FLAG_AUTHORIZED. If true, the driver is
 *	required to assume that the port is unauthorized until authorized by
 *	user space. Otherwise, port is marked authorized by default.
 * @control_port_ethertype: the control port protocol that should be
 *	allowed through even on unauthorized ports
 * @control_port_no_encrypt: TRUE to prevent encryption of control port
 *	protocol frames.
 * @control_port_over_nl80211: TRUE if userspace expects to exchange control
 *	port frames over NL80211 instead of the network interface.
 * @control_port_no_preauth: disables pre-auth rx over the nl80211 control
 *	port for mac80211
 * @wep_keys: static WEP keys, if not NULL points to an array of
 *	CFG80211_MAX_WEP_KEYS WEP keys
 * @wep_tx_key: key index (0..3) of the default TX static WEP key
 * @psk: PSK (for devices supporting 4-way-handshake offload)
 * @sae_pwd: password for SAE authentication (for devices supporting SAE
 *	offload)
 * @sae_pwd_len: length of SAE password (for devices supporting SAE offload)
 */
struct cfg80211_crypto_settings {
	u32 wpa_versions;
	u32 cipher_group;
	int n_ciphers_pairwise;
	u32 ciphers_pairwise[NL80211_MAX_NR_CIPHER_SUITES];
	int n_akm_suites;
	u32 akm_suites[NL80211_MAX_NR_AKM_SUITES];
	bool control_port;
	__be16 control_port_ethertype;
	bool control_port_no_encrypt;
	bool control_port_over_nl80211;
	bool control_port_no_preauth;
	struct key_params *wep_keys;
	int wep_tx_key;
	const u8 *psk;
	const u8 *sae_pwd;
	u8 sae_pwd_len;
};

/**
 * struct cfg80211_beacon_data - beacon data
 * @head: head portion of beacon (before TIM IE)
 *	or %NULL if not changed
 * @tail: tail portion of beacon (after TIM IE)
 *	or %NULL if not changed
 * @head_len: length of @head
 * @tail_len: length of @tail
 * @beacon_ies: extra information element(s) to add into Beacon frames or %NULL
 * @beacon_ies_len: length of beacon_ies in octets
 * @proberesp_ies: extra information element(s) to add into Probe Response
 *	frames or %NULL
 * @proberesp_ies_len: length of proberesp_ies in octets
 * @assocresp_ies: extra information element(s) to add into (Re)Association
 *	Response frames or %NULL
 * @assocresp_ies_len: length of assocresp_ies in octets
 * @probe_resp_len: length of probe response template (@probe_resp)
 * @probe_resp: probe response template (AP mode only)
 * @ftm_responder: enable FTM responder functionality; -1 for no change
 *	(which also implies no change in LCI/civic location data)
 * @lci: Measurement Report element content, starting with Measurement Token
 *	(measurement type 8)
 * @civicloc: Measurement Report element content, starting with Measurement
 *	Token (measurement type 11)
 * @lci_len: LCI data length
 * @civicloc_len: Civic location data length
 */
struct cfg80211_beacon_data {
	const u8 *head, *tail;
	const u8 *beacon_ies;
	const u8 *proberesp_ies;
	const u8 *assocresp_ies;
	const u8 *probe_resp;
	const u8 *lci;
	const u8 *civicloc;
	s8 ftm_responder;

	size_t head_len, tail_len;
	size_t beacon_ies_len;
	size_t proberesp_ies_len;
	size_t assocresp_ies_len;
	size_t probe_resp_len;
	size_t lci_len;
	size_t civicloc_len;
};

struct mac_address {
	u8 addr[ETH_ALEN];
};

/**
 * struct cfg80211_acl_data - Access control list data
 *
 * @acl_policy: ACL policy to be applied on the station's
 *	entry specified by mac_addr
 * @n_acl_entries: Number of MAC address entries passed
 * @mac_addrs: List of MAC addresses of stations to be used for ACL
 */
struct cfg80211_acl_data {
	enum nl80211_acl_policy acl_policy;
	int n_acl_entries;

	/* Keep it last */
	struct mac_address mac_addrs[];
};

/**
 * enum cfg80211_ap_settings_flags - AP settings flags
 *
 * Used by cfg80211_ap_settings
 *
 * @AP_SETTINGS_EXTERNAL_AUTH_SUPPORT: AP supports external authentication
 */
enum cfg80211_ap_settings_flags {
	AP_SETTINGS_EXTERNAL_AUTH_SUPPORT = BIT(0),
};

/**
 * struct cfg80211_ap_settings - AP configuration
 *
 * Used to configure an AP interface.
 *
 * @chandef: defines the channel to use
 * @beacon: beacon data
 * @beacon_interval: beacon interval
 * @dtim_period: DTIM period
 * @ssid: SSID to be used in the BSS (note: may be %NULL if not provided from
 *	user space)
 * @ssid_len: length of @ssid
 * @hidden_ssid: whether to hide the SSID in Beacon/Probe Response frames
 * @crypto: crypto settings
 * @privacy: the BSS uses privacy
 * @auth_type: Authentication type (algorithm)
 * @smps_mode: SMPS mode
 * @inactivity_timeout: time in seconds to determine station's inactivity.
 * @p2p_ctwindow: P2P CT Window
 * @p2p_opp_ps: P2P opportunistic PS
 * @acl: ACL configuration used by the drivers which has support for
 *	MAC address based access control
 * @pbss: If set, start as a PCP instead of AP. Relevant for DMG
 *	networks.
 * @beacon_rate: bitrate to be used for beacons
 * @ht_cap: HT capabilities (or %NULL if HT isn't enabled)
 * @vht_cap: VHT capabilities (or %NULL if VHT isn't enabled)
 * @he_cap: HE capabilities (or %NULL if HE isn't enabled)
 * @ht_required: stations must support HT
 * @vht_required: stations must support VHT
 * @twt_responder: Enable Target Wait Time
 * @he_required: stations must support HE
 * @flags: flags, as defined in enum cfg80211_ap_settings_flags
 * @he_obss_pd: OBSS Packet Detection settings
 * @he_bss_color: BSS Color settings
 * @he_oper: HE operation IE (or %NULL if HE isn't enabled)
 */
struct cfg80211_ap_settings {
	struct cfg80211_chan_def chandef;

	struct cfg80211_beacon_data beacon;

	int beacon_interval, dtim_period;
	const u8 *ssid;
	size_t ssid_len;
	enum nl80211_hidden_ssid hidden_ssid;
	struct cfg80211_crypto_settings crypto;
	bool privacy;
	enum nl80211_auth_type auth_type;
	enum nl80211_smps_mode smps_mode;
	int inactivity_timeout;
	u8 p2p_ctwindow;
	bool p2p_opp_ps;
	const struct cfg80211_acl_data *acl;
	bool pbss;
	struct cfg80211_bitrate_mask beacon_rate;

	const struct ieee80211_ht_cap *ht_cap;
	const struct ieee80211_vht_cap *vht_cap;
	const struct ieee80211_he_cap_elem *he_cap;
	const struct ieee80211_he_operation *he_oper;
	bool ht_required, vht_required, he_required;
	bool twt_responder;
	u32 flags;
	struct ieee80211_he_obss_pd he_obss_pd;
	struct cfg80211_he_bss_color he_bss_color;
};

/**
 * struct cfg80211_csa_settings - channel switch settings
 *
 * Used for channel switch
 *
 * @chandef: defines the channel to use after the switch
 * @beacon_csa: beacon data while performing the switch
 * @counter_offsets_beacon: offsets of the counters within the beacon (tail)
 * @counter_offsets_presp: offsets of the counters within the probe response
 * @n_counter_offsets_beacon: number of csa counters the beacon (tail)
 * @n_counter_offsets_presp: number of csa counters in the probe response
 * @beacon_after: beacon data to be used on the new channel
 * @radar_required: whether radar detection is required on the new channel
 * @block_tx: whether transmissions should be blocked while changing
 * @count: number of beacons until switch
 */
struct cfg80211_csa_settings {
	struct cfg80211_chan_def chandef;
	struct cfg80211_beacon_data beacon_csa;
	const u16 *counter_offsets_beacon;
	const u16 *counter_offsets_presp;
	unsigned int n_counter_offsets_beacon;
	unsigned int n_counter_offsets_presp;
	struct cfg80211_beacon_data beacon_after;
	bool radar_required;
	bool block_tx;
	u8 count;
};

#define CFG80211_MAX_NUM_DIFFERENT_CHANNELS 10

/**
 * struct iface_combination_params - input parameters for interface combinations
 *
 * Used to pass interface combination parameters
 *
 * @num_different_channels: the number of different channels we want
 *	to use for verification
 * @radar_detect: a bitmap where each bit corresponds to a channel
 *	width where radar detection is needed, as in the definition of
 *	&struct ieee80211_iface_combination.@radar_detect_widths
 * @iftype_num: array with the number of interfaces of each interface
 *	type.  The index is the interface type as specified in &enum
 *	nl80211_iftype.
 * @new_beacon_int: set this to the beacon interval of a new interface
 *	that's not operating yet, if such is to be checked as part of
 *	the verification
 */
struct iface_combination_params {
	int num_different_channels;
	u8 radar_detect;
	int iftype_num[NUM_NL80211_IFTYPES];
	u32 new_beacon_int;
};

/**
 * enum station_parameters_apply_mask - station parameter values to apply
 * @STATION_PARAM_APPLY_UAPSD: apply new uAPSD parameters (uapsd_queues, max_sp)
 * @STATION_PARAM_APPLY_CAPABILITY: apply new capability
 * @STATION_PARAM_APPLY_PLINK_STATE: apply new plink state
 *
 * Not all station parameters have in-band "no change" signalling,
 * for those that don't these flags will are used.
 */
enum station_parameters_apply_mask {
	STATION_PARAM_APPLY_UAPSD = BIT(0),
	STATION_PARAM_APPLY_CAPABILITY = BIT(1),
	STATION_PARAM_APPLY_PLINK_STATE = BIT(2),
	STATION_PARAM_APPLY_STA_TXPOWER = BIT(3),
};

/**
 * struct sta_txpwr - station txpower configuration
 *
 * Used to configure txpower for station.
 *
 * @power: tx power (in dBm) to be used for sending data traffic. If tx power
 *	is not provided, the default per-interface tx power setting will be
 *	overriding. Driver should be picking up the lowest tx power, either tx
 *	power per-interface or per-station.
 * @type: In particular if TPC %type is NL80211_TX_POWER_LIMITED then tx power
 *	will be less than or equal to specified from userspace, whereas if TPC
 *	%type is NL80211_TX_POWER_AUTOMATIC then it indicates default tx power.
 *	NL80211_TX_POWER_FIXED is not a valid configuration option for
 *	per peer TPC.
 */
struct sta_txpwr {
	s16 power;
	enum nl80211_tx_power_setting type;
};

/**
 * struct station_parameters - station parameters
 *
 * Used to change and create a new station.
 *
 * @vlan: vlan interface station should belong to
 * @supported_rates: supported rates in IEEE 802.11 format
 *	(or NULL for no change)
 * @supported_rates_len: number of supported rates
 * @sta_flags_mask: station flags that changed
 *	(bitmask of BIT(%NL80211_STA_FLAG_...))
 * @sta_flags_set: station flags values
 *	(bitmask of BIT(%NL80211_STA_FLAG_...))
 * @listen_interval: listen interval or -1 for no change
 * @aid: AID or zero for no change
 * @vlan_id: VLAN ID for station (if nonzero)
 * @peer_aid: mesh peer AID or zero for no change
 * @plink_action: plink action to take
 * @plink_state: set the peer link state for a station
 * @ht_capa: HT capabilities of station
 * @vht_capa: VHT capabilities of station
 * @uapsd_queues: bitmap of queues configured for uapsd. same format
 *	as the AC bitmap in the QoS info field
 * @max_sp: max Service Period. same format as the MAX_SP in the
 *	QoS info field (but already shifted down)
 * @sta_modify_mask: bitmap indicating which parameters changed
 *	(for those that don't have a natural "no change" value),
 *	see &enum station_parameters_apply_mask
 * @local_pm: local link-specific mesh power save mode (no change when set
 *	to unknown)
 * @capability: station capability
 * @ext_capab: extended capabilities of the station
 * @ext_capab_len: number of extended capabilities
 * @supported_channels: supported channels in IEEE 802.11 format
 * @supported_channels_len: number of supported channels
 * @supported_oper_classes: supported oper classes in IEEE 802.11 format
 * @supported_oper_classes_len: number of supported operating classes
 * @opmode_notif: operating mode field from Operating Mode Notification
 * @opmode_notif_used: information if operating mode field is used
 * @support_p2p_ps: information if station supports P2P PS mechanism
 * @he_capa: HE capabilities of station
 * @he_capa_len: the length of the HE capabilities
 * @airtime_weight: airtime scheduler weight for this station
 * @txpwr: transmit power for an associated station
 * @he_6ghz_capa: HE 6 GHz Band capabilities of station
 */
struct station_parameters {
	const u8 *supported_rates;
	struct net_device *vlan;
	u32 sta_flags_mask, sta_flags_set;
	u32 sta_modify_mask;
	int listen_interval;
	u16 aid;
	u16 vlan_id;
	u16 peer_aid;
	u8 supported_rates_len;
	u8 plink_action;
	u8 plink_state;
	const struct ieee80211_ht_cap *ht_capa;
	const struct ieee80211_vht_cap *vht_capa;
	u8 uapsd_queues;
	u8 max_sp;
	enum nl80211_mesh_power_mode local_pm;
	u16 capability;
	const u8 *ext_capab;
	u8 ext_capab_len;
	const u8 *supported_channels;
	u8 supported_channels_len;
	const u8 *supported_oper_classes;
	u8 supported_oper_classes_len;
	u8 opmode_notif;
	bool opmode_notif_used;
	int support_p2p_ps;
	const struct ieee80211_he_cap_elem *he_capa;
	u8 he_capa_len;
	u16 airtime_weight;
	struct sta_txpwr txpwr;
	const struct ieee80211_he_6ghz_capa *he_6ghz_capa;
};

/**
 * struct station_del_parameters - station deletion parameters
 *
 * Used to delete a station entry (or all stations).
 *
 * @mac: MAC address of the station to remove or NULL to remove all stations
 * @subtype: Management frame subtype to use for indicating removal
 *	(10 = Disassociation, 12 = Deauthentication)
 * @reason_code: Reason code for the Disassociation/Deauthentication frame
 */
struct station_del_parameters {
	const u8 *mac;
	u8 subtype;
	u16 reason_code;
};

/**
 * enum cfg80211_station_type - the type of station being modified
 * @CFG80211_STA_AP_CLIENT: client of an AP interface
 * @CFG80211_STA_AP_CLIENT_UNASSOC: client of an AP interface that is still
 *	unassociated (update properties for this type of client is permitted)
 * @CFG80211_STA_AP_MLME_CLIENT: client of an AP interface that has
 *	the AP MLME in the device
 * @CFG80211_STA_AP_STA: AP station on managed interface
 * @CFG80211_STA_IBSS: IBSS station
 * @CFG80211_STA_TDLS_PEER_SETUP: TDLS peer on managed interface (dummy entry
 *	while TDLS setup is in progress, it moves out of this state when
 *	being marked authorized; use this only if TDLS with external setup is
 *	supported/used)
 * @CFG80211_STA_TDLS_PEER_ACTIVE: TDLS peer on managed interface (active
 *	entry that is operating, has been marked authorized by userspace)
 * @CFG80211_STA_MESH_PEER_KERNEL: peer on mesh interface (kernel managed)
 * @CFG80211_STA_MESH_PEER_USER: peer on mesh interface (user managed)
 */
enum cfg80211_station_type {
	CFG80211_STA_AP_CLIENT,
	CFG80211_STA_AP_CLIENT_UNASSOC,
	CFG80211_STA_AP_MLME_CLIENT,
	CFG80211_STA_AP_STA,
	CFG80211_STA_IBSS,
	CFG80211_STA_TDLS_PEER_SETUP,
	CFG80211_STA_TDLS_PEER_ACTIVE,
	CFG80211_STA_MESH_PEER_KERNEL,
	CFG80211_STA_MESH_PEER_USER,
};

/**
 * cfg80211_check_station_change - validate parameter changes
 * @wiphy: the wiphy this operates on
 * @params: the new parameters for a station
 * @statype: the type of station being modified
 *
 * Utility function for the @change_station driver method. Call this function
 * with the appropriate station type looking up the station (and checking that
 * it exists). It will verify whether the station change is acceptable, and if
 * not will return an error code. Note that it may modify the parameters for
 * backward compatibility reasons, so don't use them before calling this.
 */
int cfg80211_check_station_change(struct wiphy *wiphy,
				  struct station_parameters *params,
				  enum cfg80211_station_type statype);

/**
 * enum station_info_rate_flags - bitrate info flags
 *
 * Used by the driver to indicate the specific rate transmission
 * type for 802.11n transmissions.
 *
 * @RATE_INFO_FLAGS_MCS: mcs field filled with HT MCS
 * @RATE_INFO_FLAGS_VHT_MCS: mcs field filled with VHT MCS
 * @RATE_INFO_FLAGS_SHORT_GI: 400ns guard interval
 * @RATE_INFO_FLAGS_DMG: 60GHz MCS
 * @RATE_INFO_FLAGS_HE_MCS: HE MCS information
 * @RATE_INFO_FLAGS_EDMG: 60GHz MCS in EDMG mode
 */
enum rate_info_flags {
	RATE_INFO_FLAGS_MCS			= BIT(0),
	RATE_INFO_FLAGS_VHT_MCS			= BIT(1),
	RATE_INFO_FLAGS_SHORT_GI		= BIT(2),
	RATE_INFO_FLAGS_DMG			= BIT(3),
	RATE_INFO_FLAGS_HE_MCS			= BIT(4),
	RATE_INFO_FLAGS_EDMG			= BIT(5),
};

/**
 * enum rate_info_bw - rate bandwidth information
 *
 * Used by the driver to indicate the rate bandwidth.
 *
 * @RATE_INFO_BW_5: 5 MHz bandwidth
 * @RATE_INFO_BW_10: 10 MHz bandwidth
 * @RATE_INFO_BW_20: 20 MHz bandwidth
 * @RATE_INFO_BW_40: 40 MHz bandwidth
 * @RATE_INFO_BW_80: 80 MHz bandwidth
 * @RATE_INFO_BW_160: 160 MHz bandwidth
 * @RATE_INFO_BW_HE_RU: bandwidth determined by HE RU allocation
 */
enum rate_info_bw {
	RATE_INFO_BW_20 = 0,
	RATE_INFO_BW_5,
	RATE_INFO_BW_10,
	RATE_INFO_BW_40,
	RATE_INFO_BW_80,
	RATE_INFO_BW_160,
	RATE_INFO_BW_HE_RU,
};

/**
 * struct rate_info - bitrate information
 *
 * Information about a receiving or transmitting bitrate
 *
 * @flags: bitflag of flags from &enum rate_info_flags
 * @mcs: mcs index if struct describes an HT/VHT/HE rate
 * @legacy: bitrate in 100kbit/s for 802.11abg
 * @nss: number of streams (VHT & HE only)
 * @bw: bandwidth (from &enum rate_info_bw)
 * @he_gi: HE guard interval (from &enum nl80211_he_gi)
 * @he_dcm: HE DCM value
 * @he_ru_alloc: HE RU allocation (from &enum nl80211_he_ru_alloc,
 *	only valid if bw is %RATE_INFO_BW_HE_RU)
 * @n_bonded_ch: In case of EDMG the number of bonded channels (1-4)
 */
struct rate_info {
	u8 flags;
	u8 mcs;
	u16 legacy;
	u8 nss;
	u8 bw;
	u8 he_gi;
	u8 he_dcm;
	u8 he_ru_alloc;
	u8 n_bonded_ch;
};

/**
 * enum station_info_rate_flags - bitrate info flags
 *
 * Used by the driver to indicate the specific rate transmission
 * type for 802.11n transmissions.
 *
 * @BSS_PARAM_FLAGS_CTS_PROT: whether CTS protection is enabled
 * @BSS_PARAM_FLAGS_SHORT_PREAMBLE: whether short preamble is enabled
 * @BSS_PARAM_FLAGS_SHORT_SLOT_TIME: whether short slot time is enabled
 */
enum bss_param_flags {
	BSS_PARAM_FLAGS_CTS_PROT	= 1<<0,
	BSS_PARAM_FLAGS_SHORT_PREAMBLE	= 1<<1,
	BSS_PARAM_FLAGS_SHORT_SLOT_TIME	= 1<<2,
};

/**
 * struct sta_bss_parameters - BSS parameters for the attached station
 *
 * Information about the currently associated BSS
 *
 * @flags: bitflag of flags from &enum bss_param_flags
 * @dtim_period: DTIM period for the BSS
 * @beacon_interval: beacon interval
 */
struct sta_bss_parameters {
	u8 flags;
	u8 dtim_period;
	u16 beacon_interval;
};

/**
 * struct cfg80211_txq_stats - TXQ statistics for this TID
 * @filled: bitmap of flags using the bits of &enum nl80211_txq_stats to
 *	indicate the relevant values in this struct are filled
 * @backlog_bytes: total number of bytes currently backlogged
 * @backlog_packets: total number of packets currently backlogged
 * @flows: number of new flows seen
 * @drops: total number of packets dropped
 * @ecn_marks: total number of packets marked with ECN CE
 * @overlimit: number of drops due to queue space overflow
 * @overmemory: number of drops due to memory limit overflow
 * @collisions: number of hash collisions
 * @tx_bytes: total number of bytes dequeued
 * @tx_packets: total number of packets dequeued
 * @max_flows: maximum number of flows supported
 */
struct cfg80211_txq_stats {
	u32 filled;
	u32 backlog_bytes;
	u32 backlog_packets;
	u32 flows;
	u32 drops;
	u32 ecn_marks;
	u32 overlimit;
	u32 overmemory;
	u32 collisions;
	u32 tx_bytes;
	u32 tx_packets;
	u32 max_flows;
};

/**
 * struct cfg80211_tid_stats - per-TID statistics
 * @filled: bitmap of flags using the bits of &enum nl80211_tid_stats to
 *	indicate the relevant values in this struct are filled
 * @rx_msdu: number of received MSDUs
 * @tx_msdu: number of (attempted) transmitted MSDUs
 * @tx_msdu_retries: number of retries (not counting the first) for
 *	transmitted MSDUs
 * @tx_msdu_failed: number of failed transmitted MSDUs
 * @txq_stats: TXQ statistics
 */
struct cfg80211_tid_stats {
	u32 filled;
	u64 rx_msdu;
	u64 tx_msdu;
	u64 tx_msdu_retries;
	u64 tx_msdu_failed;
	struct cfg80211_txq_stats txq_stats;
};

#define IEEE80211_MAX_CHAINS	4

/**
 * struct station_info - station information
 *
 * Station information filled by driver for get_station() and dump_station.
 *
 * @filled: bitflag of flags using the bits of &enum nl80211_sta_info to
 *	indicate the relevant values in this struct for them
 * @connected_time: time(in secs) since a station is last connected
 * @inactive_time: time since last station activity (tx/rx) in milliseconds
 * @assoc_at: bootime (ns) of the last association
 * @rx_bytes: bytes (size of MPDUs) received from this station
 * @tx_bytes: bytes (size of MPDUs) transmitted to this station
 * @llid: mesh local link id
 * @plid: mesh peer link id
 * @plink_state: mesh peer link state
 * @signal: The signal strength, type depends on the wiphy's signal_type.
 *	For CFG80211_SIGNAL_TYPE_MBM, value is expressed in _dBm_.
 * @signal_avg: Average signal strength, type depends on the wiphy's signal_type.
 *	For CFG80211_SIGNAL_TYPE_MBM, value is expressed in _dBm_.
 * @chains: bitmask for filled values in @chain_signal, @chain_signal_avg
 * @chain_signal: per-chain signal strength of last received packet in dBm
 * @chain_signal_avg: per-chain signal strength average in dBm
 * @txrate: current unicast bitrate from this station
 * @rxrate: current unicast bitrate to this station
 * @rx_packets: packets (MSDUs & MMPDUs) received from this station
 * @tx_packets: packets (MSDUs & MMPDUs) transmitted to this station
 * @tx_retries: cumulative retry counts (MPDUs)
 * @tx_failed: number of failed transmissions (MPDUs) (retries exceeded, no ACK)
 * @rx_dropped_misc:  Dropped for un-specified reason.
 * @bss_param: current BSS parameters
 * @generation: generation number for nl80211 dumps.
 *	This number should increase every time the list of stations
 *	changes, i.e. when a station is added or removed, so that
 *	userspace can tell whether it got a consistent snapshot.
 * @assoc_req_ies: IEs from (Re)Association Request.
 *	This is used only when in AP mode with drivers that do not use
 *	user space MLME/SME implementation. The information is provided for
 *	the cfg80211_new_sta() calls to notify user space of the IEs.
 * @assoc_req_ies_len: Length of assoc_req_ies buffer in octets.
 * @sta_flags: station flags mask & values
 * @beacon_loss_count: Number of times beacon loss event has triggered.
 * @t_offset: Time offset of the station relative to this host.
 * @local_pm: local mesh STA power save mode
 * @peer_pm: peer mesh STA power save mode
 * @nonpeer_pm: non-peer mesh STA power save mode
 * @expected_throughput: expected throughput in kbps (including 802.11 headers)
 *	towards this station.
 * @rx_beacon: number of beacons received from this peer
 * @rx_beacon_signal_avg: signal strength average (in dBm) for beacons received
 *	from this peer
 * @connected_to_gate: true if mesh STA has a path to mesh gate
 * @rx_duration: aggregate PPDU duration(usecs) for all the frames from a peer
 * @tx_duration: aggregate PPDU duration(usecs) for all the frames to a peer
 * @airtime_weight: current airtime scheduling weight
 * @pertid: per-TID statistics, see &struct cfg80211_tid_stats, using the last
 *	(IEEE80211_NUM_TIDS) index for MSDUs not encapsulated in QoS-MPDUs.
 *	Note that this doesn't use the @filled bit, but is used if non-NULL.
 * @ack_signal: signal strength (in dBm) of the last ACK frame.
 * @avg_ack_signal: average rssi value of ack packet for the no of msdu's has
 *	been sent.
 * @rx_mpdu_count: number of MPDUs received from this station
 * @fcs_err_count: number of packets (MPDUs) received from this station with
 *	an FCS error. This counter should be incremented only when TA of the
 *	received packet with an FCS error matches the peer MAC address.
 * @airtime_link_metric: mesh airtime link metric.
 */
struct station_info {
	u64 filled;
	u32 connected_time;
	u32 inactive_time;
	u64 assoc_at;
	u64 rx_bytes;
	u64 tx_bytes;
	u16 llid;
	u16 plid;
	u8 plink_state;
	s8 signal;
	s8 signal_avg;

	u8 chains;
	s8 chain_signal[IEEE80211_MAX_CHAINS];
	s8 chain_signal_avg[IEEE80211_MAX_CHAINS];

	struct rate_info txrate;
	struct rate_info rxrate;
	u32 rx_packets;
	u32 tx_packets;
	u32 tx_retries;
	u32 tx_failed;
	u32 rx_dropped_misc;
	struct sta_bss_parameters bss_param;
	struct nl80211_sta_flag_update sta_flags;

	int generation;

	const u8 *assoc_req_ies;
	size_t assoc_req_ies_len;

	u32 beacon_loss_count;
	s64 t_offset;
	enum nl80211_mesh_power_mode local_pm;
	enum nl80211_mesh_power_mode peer_pm;
	enum nl80211_mesh_power_mode nonpeer_pm;

	u32 expected_throughput;

	u64 tx_duration;
	u64 rx_duration;
	u64 rx_beacon;
	u8 rx_beacon_signal_avg;
	u8 connected_to_gate;

	struct cfg80211_tid_stats *pertid;
	s8 ack_signal;
	s8 avg_ack_signal;

	u16 airtime_weight;

	u32 rx_mpdu_count;
	u32 fcs_err_count;

	u32 airtime_link_metric;
};

#if IS_ENABLED(CONFIG_CFG80211)
/**
 * cfg80211_get_station - retrieve information about a given station
 * @dev: the device where the station is supposed to be connected to
 * @mac_addr: the mac address of the station of interest
 * @sinfo: pointer to the structure to fill with the information
 *
 * Returns 0 on success and sinfo is filled with the available information
 * otherwise returns a negative error code and the content of sinfo has to be
 * considered undefined.
 */
int cfg80211_get_station(struct net_device *dev, const u8 *mac_addr,
			 struct station_info *sinfo);
#else
static inline int cfg80211_get_station(struct net_device *dev,
				       const u8 *mac_addr,
				       struct station_info *sinfo)
{
	return -ENOENT;
}
#endif

/**
 * enum monitor_flags - monitor flags
 *
 * Monitor interface configuration flags. Note that these must be the bits
 * according to the nl80211 flags.
 *
 * @MONITOR_FLAG_CHANGED: set if the flags were changed
 * @MONITOR_FLAG_FCSFAIL: pass frames with bad FCS
 * @MONITOR_FLAG_PLCPFAIL: pass frames with bad PLCP
 * @MONITOR_FLAG_CONTROL: pass control frames
 * @MONITOR_FLAG_OTHER_BSS: disable BSSID filtering
 * @MONITOR_FLAG_COOK_FRAMES: report frames after processing
 * @MONITOR_FLAG_ACTIVE: active monitor, ACKs frames on its MAC address
 */
enum monitor_flags {
	MONITOR_FLAG_CHANGED		= 1<<__NL80211_MNTR_FLAG_INVALID,
	MONITOR_FLAG_FCSFAIL		= 1<<NL80211_MNTR_FLAG_FCSFAIL,
	MONITOR_FLAG_PLCPFAIL		= 1<<NL80211_MNTR_FLAG_PLCPFAIL,
	MONITOR_FLAG_CONTROL		= 1<<NL80211_MNTR_FLAG_CONTROL,
	MONITOR_FLAG_OTHER_BSS		= 1<<NL80211_MNTR_FLAG_OTHER_BSS,
	MONITOR_FLAG_COOK_FRAMES	= 1<<NL80211_MNTR_FLAG_COOK_FRAMES,
	MONITOR_FLAG_ACTIVE		= 1<<NL80211_MNTR_FLAG_ACTIVE,
};

/**
 * enum mpath_info_flags -  mesh path information flags
 *
 * Used by the driver to indicate which info in &struct mpath_info it has filled
 * in during get_station() or dump_station().
 *
 * @MPATH_INFO_FRAME_QLEN: @frame_qlen filled
 * @MPATH_INFO_SN: @sn filled
 * @MPATH_INFO_METRIC: @metric filled
 * @MPATH_INFO_EXPTIME: @exptime filled
 * @MPATH_INFO_DISCOVERY_TIMEOUT: @discovery_timeout filled
 * @MPATH_INFO_DISCOVERY_RETRIES: @discovery_retries filled
 * @MPATH_INFO_FLAGS: @flags filled
 * @MPATH_INFO_HOP_COUNT: @hop_count filled
 * @MPATH_INFO_PATH_CHANGE: @path_change_count filled
 */
enum mpath_info_flags {
	MPATH_INFO_FRAME_QLEN		= BIT(0),
	MPATH_INFO_SN			= BIT(1),
	MPATH_INFO_METRIC		= BIT(2),
	MPATH_INFO_EXPTIME		= BIT(3),
	MPATH_INFO_DISCOVERY_TIMEOUT	= BIT(4),
	MPATH_INFO_DISCOVERY_RETRIES	= BIT(5),
	MPATH_INFO_FLAGS		= BIT(6),
	MPATH_INFO_HOP_COUNT		= BIT(7),
	MPATH_INFO_PATH_CHANGE		= BIT(8),
};

/**
 * struct mpath_info - mesh path information
 *
 * Mesh path information filled by driver for get_mpath() and dump_mpath().
 *
 * @filled: bitfield of flags from &enum mpath_info_flags
 * @frame_qlen: number of queued frames for this destination
 * @sn: target sequence number
 * @metric: metric (cost) of this mesh path
 * @exptime: expiration time for the mesh path from now, in msecs
 * @flags: mesh path flags
 * @discovery_timeout: total mesh path discovery timeout, in msecs
 * @discovery_retries: mesh path discovery retries
 * @generation: generation number for nl80211 dumps.
 *	This number should increase every time the list of mesh paths
 *	changes, i.e. when a station is added or removed, so that
 *	userspace can tell whether it got a consistent snapshot.
 * @hop_count: hops to destination
 * @path_change_count: total number of path changes to destination
 */
struct mpath_info {
	u32 filled;
	u32 frame_qlen;
	u32 sn;
	u32 metric;
	u32 exptime;
	u32 discovery_timeout;
	u8 discovery_retries;
	u8 flags;
	u8 hop_count;
	u32 path_change_count;

	int generation;
};

/**
 * struct bss_parameters - BSS parameters
 *
 * Used to change BSS parameters (mainly for AP mode).
 *
 * @use_cts_prot: Whether to use CTS protection
 *	(0 = no, 1 = yes, -1 = do not change)
 * @use_short_preamble: Whether the use of short preambles is allowed
 *	(0 = no, 1 = yes, -1 = do not change)
 * @use_short_slot_time: Whether the use of short slot time is allowed
 *	(0 = no, 1 = yes, -1 = do not change)
 * @basic_rates: basic rates in IEEE 802.11 format
 *	(or NULL for no change)
 * @basic_rates_len: number of basic rates
 * @ap_isolate: do not forward packets between connected stations
 * @ht_opmode: HT Operation mode
 *	(u16 = opmode, -1 = do not change)
 * @p2p_ctwindow: P2P CT Window (-1 = no change)
 * @p2p_opp_ps: P2P opportunistic PS (-1 = no change)
 */
struct bss_parameters {
	int use_cts_prot;
	int use_short_preamble;
	int use_short_slot_time;
	const u8 *basic_rates;
	u8 basic_rates_len;
	int ap_isolate;
	int ht_opmode;
	s8 p2p_ctwindow, p2p_opp_ps;
};

/**
 * struct mesh_config - 802.11s mesh configuration
 *
 * These parameters can be changed while the mesh is active.
 *
 * @dot11MeshRetryTimeout: the initial retry timeout in millisecond units used
 *	by the Mesh Peering Open message
 * @dot11MeshConfirmTimeout: the initial retry timeout in millisecond units
 *	used by the Mesh Peering Open message
 * @dot11MeshHoldingTimeout: the confirm timeout in millisecond units used by
 *	the mesh peering management to close a mesh peering
 * @dot11MeshMaxPeerLinks: the maximum number of peer links allowed on this
 *	mesh interface
 * @dot11MeshMaxRetries: the maximum number of peer link open retries that can
 *	be sent to establish a new peer link instance in a mesh
 * @dot11MeshTTL: the value of TTL field set at a source mesh STA
 * @element_ttl: the value of TTL field set at a mesh STA for path selection
 *	elements
 * @auto_open_plinks: whether we should automatically open peer links when we
 *	detect compatible mesh peers
 * @dot11MeshNbrOffsetMaxNeighbor: the maximum number of neighbors to
 *	synchronize to for 11s default synchronization method
 * @dot11MeshHWMPmaxPREQretries: the number of action frames containing a PREQ
 *	that an originator mesh STA can send to a particular path target
 * @path_refresh_time: how frequently to refresh mesh paths in milliseconds
 * @min_discovery_timeout: the minimum length of time to wait until giving up on
 *	a path discovery in milliseconds
 * @dot11MeshHWMPactivePathTimeout: the time (in TUs) for which mesh STAs
 *	receiving a PREQ shall consider the forwarding information from the
 *	root to be valid. (TU = time unit)
 * @dot11MeshHWMPpreqMinInterval: the minimum interval of time (in TUs) during
 *	which a mesh STA can send only one action frame containing a PREQ
 *	element
 * @dot11MeshHWMPperrMinInterval: the minimum interval of time (in TUs) during
 *	which a mesh STA can send only one Action frame containing a PERR
 *	element
 * @dot11MeshHWMPnetDiameterTraversalTime: the interval of time (in TUs) that
 *	it takes for an HWMP information element to propagate across the mesh
 * @dot11MeshHWMPRootMode: the configuration of a mesh STA as root mesh STA
 * @dot11MeshHWMPRannInterval: the interval of time (in TUs) between root
 *	announcements are transmitted
 * @dot11MeshGateAnnouncementProtocol: whether to advertise that this mesh
 *	station has access to a broader network beyond the MBSS. (This is
 *	missnamed in draft 12.0: dot11MeshGateAnnouncementProtocol set to true
 *	only means that the station will announce others it's a mesh gate, but
 *	not necessarily using the gate announcement protocol. Still keeping the
 *	same nomenclature to be in sync with the spec)
 * @dot11MeshForwarding: whether the Mesh STA is forwarding or non-forwarding
 *	entity (default is TRUE - forwarding entity)
 * @rssi_threshold: the threshold for average signal strength of candidate
 *	station to establish a peer link
 * @ht_opmode: mesh HT protection mode
 *
 * @dot11MeshHWMPactivePathToRootTimeout: The time (in TUs) for which mesh STAs
 *	receiving a proactive PREQ shall consider the forwarding information to
 *	the root mesh STA to be valid.
 *
 * @dot11MeshHWMProotInterval: The interval of time (in TUs) between proactive
 *	PREQs are transmitted.
 * @dot11MeshHWMPconfirmationInterval: The minimum interval of time (in TUs)
 *	during which a mesh STA can send only one Action frame containing
 *	a PREQ element for root path confirmation.
 * @power_mode: The default mesh power save mode which will be the initial
 *	setting for new peer links.
 * @dot11MeshAwakeWindowDuration: The duration in TUs the STA will remain awake
 *	after transmitting its beacon.
 * @plink_timeout: If no tx activity is seen from a STA we've established
 *	peering with for longer than this time (in seconds), then remove it
 *	from the STA's list of peers.  Default is 30 minutes.
 * @dot11MeshConnectedToMeshGate: if set to true, advertise that this STA is
 *      connected to a mesh gate in mesh formation info.  If false, the
 *      value in mesh formation is determined by the presence of root paths
 *      in the mesh path table
 */
struct mesh_config {
	u16 dot11MeshRetryTimeout;
	u16 dot11MeshConfirmTimeout;
	u16 dot11MeshHoldingTimeout;
	u16 dot11MeshMaxPeerLinks;
	u8 dot11MeshMaxRetries;
	u8 dot11MeshTTL;
	u8 element_ttl;
	bool auto_open_plinks;
	u32 dot11MeshNbrOffsetMaxNeighbor;
	u8 dot11MeshHWMPmaxPREQretries;
	u32 path_refresh_time;
	u16 min_discovery_timeout;
	u32 dot11MeshHWMPactivePathTimeout;
	u16 dot11MeshHWMPpreqMinInterval;
	u16 dot11MeshHWMPperrMinInterval;
	u16 dot11MeshHWMPnetDiameterTraversalTime;
	u8 dot11MeshHWMPRootMode;
	bool dot11MeshConnectedToMeshGate;
	u16 dot11MeshHWMPRannInterval;
	bool dot11MeshGateAnnouncementProtocol;
	bool dot11MeshForwarding;
	s32 rssi_threshold;
	u16 ht_opmode;
	u32 dot11MeshHWMPactivePathToRootTimeout;
	u16 dot11MeshHWMProotInterval;
	u16 dot11MeshHWMPconfirmationInterval;
	enum nl80211_mesh_power_mode power_mode;
	u16 dot11MeshAwakeWindowDuration;
	u32 plink_timeout;
};

/**
 * struct mesh_setup - 802.11s mesh setup configuration
 * @chandef: defines the channel to use
 * @mesh_id: the mesh ID
 * @mesh_id_len: length of the mesh ID, at least 1 and at most 32 bytes
 * @sync_method: which synchronization method to use
 * @path_sel_proto: which path selection protocol to use
 * @path_metric: which metric to use
 * @auth_id: which authentication method this mesh is using
 * @ie: vendor information elements (optional)
 * @ie_len: length of vendor information elements
 * @is_authenticated: this mesh requires authentication
 * @is_secure: this mesh uses security
 * @user_mpm: userspace handles all MPM functions
 * @dtim_period: DTIM period to use
 * @beacon_interval: beacon interval to use
 * @mcast_rate: multicat rate for Mesh Node [6Mbps is the default for 802.11a]
 * @basic_rates: basic rates to use when creating the mesh
 * @beacon_rate: bitrate to be used for beacons
 * @userspace_handles_dfs: whether user space controls DFS operation, i.e.
 *	changes the channel when a radar is detected. This is required
 *	to operate on DFS channels.
 * @control_port_over_nl80211: TRUE if userspace expects to exchange control
 *	port frames over NL80211 instead of the network interface.
 *
 * These parameters are fixed when the mesh is created.
 */
struct mesh_setup {
	struct cfg80211_chan_def chandef;
	const u8 *mesh_id;
	u8 mesh_id_len;
	u8 sync_method;
	u8 path_sel_proto;
	u8 path_metric;
	u8 auth_id;
	const u8 *ie;
	u8 ie_len;
	bool is_authenticated;
	bool is_secure;
	bool user_mpm;
	u8 dtim_period;
	u16 beacon_interval;
	int mcast_rate[NUM_NL80211_BANDS];
	u32 basic_rates;
	struct cfg80211_bitrate_mask beacon_rate;
	bool userspace_handles_dfs;
	bool control_port_over_nl80211;
};

/**
 * struct ocb_setup - 802.11p OCB mode setup configuration
 * @chandef: defines the channel to use
 *
 * These parameters are fixed when connecting to the network
 */
struct ocb_setup {
	struct cfg80211_chan_def chandef;
};

/**
 * struct ieee80211_txq_params - TX queue parameters
 * @ac: AC identifier
 * @txop: Maximum burst time in units of 32 usecs, 0 meaning disabled
 * @cwmin: Minimum contention window [a value of the form 2^n-1 in the range
 *	1..32767]
 * @cwmax: Maximum contention window [a value of the form 2^n-1 in the range
 *	1..32767]
 * @aifs: Arbitration interframe space [0..255]
 */
struct ieee80211_txq_params {
	enum nl80211_ac ac;
	u16 txop;
	u16 cwmin;
	u16 cwmax;
	u8 aifs;
};

/**
 * DOC: Scanning and BSS list handling
 *
 * The scanning process itself is fairly simple, but cfg80211 offers quite
 * a bit of helper functionality. To start a scan, the scan operation will
 * be invoked with a scan definition. This scan definition contains the
 * channels to scan, and the SSIDs to send probe requests for (including the
 * wildcard, if desired). A passive scan is indicated by having no SSIDs to
 * probe. Additionally, a scan request may contain extra information elements
 * that should be added to the probe request. The IEs are guaranteed to be
 * well-formed, and will not exceed the maximum length the driver advertised
 * in the wiphy structure.
 *
 * When scanning finds a BSS, cfg80211 needs to be notified of that, because
 * it is responsible for maintaining the BSS list; the driver should not
 * maintain a list itself. For this notification, various functions exist.
 *
 * Since drivers do not maintain a BSS list, there are also a number of
 * functions to search for a BSS and obtain information about it from the
 * BSS structure cfg80211 maintains. The BSS list is also made available
 * to userspace.
 */

/**
 * struct cfg80211_ssid - SSID description
 * @ssid: the SSID
 * @ssid_len: length of the ssid
 */
struct cfg80211_ssid {
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len;
};

/**
 * struct cfg80211_scan_info - information about completed scan
 * @scan_start_tsf: scan start time in terms of the TSF of the BSS that the
 *	wireless device that requested the scan is connected to. If this
 *	information is not available, this field is left zero.
 * @tsf_bssid: the BSSID according to which %scan_start_tsf is set.
 * @aborted: set to true if the scan was aborted for any reason,
 *	userspace will be notified of that
 */
struct cfg80211_scan_info {
	u64 scan_start_tsf;
	u8 tsf_bssid[ETH_ALEN] __aligned(2);
	bool aborted;
};

/**
 * struct cfg80211_scan_request - scan request description
 *
 * @ssids: SSIDs to scan for (active scan only)
 * @n_ssids: number of SSIDs
 * @channels: channels to scan on.
 * @n_channels: total number of channels to scan
 * @scan_width: channel width for scanning
 * @ie: optional information element(s) to add into Probe Request or %NULL
 * @ie_len: length of ie in octets
 * @duration: how long to listen on each channel, in TUs. If
 *	%duration_mandatory is not set, this is the maximum dwell time and
 *	the actual dwell time may be shorter.
 * @duration_mandatory: if set, the scan duration must be as specified by the
 *	%duration field.
 * @flags: bit field of flags controlling operation
 * @rates: bitmap of rates to advertise for each band
 * @wiphy: the wiphy this was for
 * @scan_start: time (in jiffies) when the scan started
 * @wdev: the wireless device to scan for
 * @info: (internal) information about completed scan
 * @notified: (internal) scan request was notified as done or aborted
 * @no_cck: used to send probe requests at non CCK rate in 2GHz band
 * @mac_addr: MAC address used with randomisation
 * @mac_addr_mask: MAC address mask used with randomisation, bits that
 *	are 0 in the mask should be randomised, bits that are 1 should
 *	be taken from the @mac_addr
 * @bssid: BSSID to scan for (most commonly, the wildcard BSSID)
 */
struct cfg80211_scan_request {
	struct cfg80211_ssid *ssids;
	int n_ssids;
	u32 n_channels;
	enum nl80211_bss_scan_width scan_width;
	const u8 *ie;
	size_t ie_len;
	u16 duration;
	bool duration_mandatory;
	u32 flags;

	u32 rates[NUM_NL80211_BANDS];

	struct wireless_dev *wdev;

	u8 mac_addr[ETH_ALEN] __aligned(2);
	u8 mac_addr_mask[ETH_ALEN] __aligned(2);
	u8 bssid[ETH_ALEN] __aligned(2);

	/* internal */
	struct wiphy *wiphy;
	unsigned long scan_start;
	struct cfg80211_scan_info info;
	bool notified;
	bool no_cck;

	/* keep last */
	struct ieee80211_channel *channels[];
};

static inline void get_random_mask_addr(u8 *buf, const u8 *addr, const u8 *mask)
{
	int i;

	get_random_bytes(buf, ETH_ALEN);
	for (i = 0; i < ETH_ALEN; i++) {
		buf[i] &= ~mask[i];
		buf[i] |= addr[i] & mask[i];
	}
}

/**
 * struct cfg80211_match_set - sets of attributes to match
 *
 * @ssid: SSID to be matched; may be zero-length in case of BSSID match
 *	or no match (RSSI only)
 * @bssid: BSSID to be matched; may be all-zero BSSID in case of SSID match
 *	or no match (RSSI only)
 * @rssi_thold: don't report scan results below this threshold (in s32 dBm)
 * @per_band_rssi_thold: Minimum rssi threshold for each band to be applied
 *	for filtering out scan results received. Drivers advertize this support
 *	of band specific rssi based filtering through the feature capability
 *	%NL80211_EXT_FEATURE_SCHED_SCAN_BAND_SPECIFIC_RSSI_THOLD. These band
 *	specific rssi thresholds take precedence over rssi_thold, if specified.
 *	If not specified for any band, it will be assigned with rssi_thold of
 *	corresponding matchset.
 */
struct cfg80211_match_set {
	struct cfg80211_ssid ssid;
	u8 bssid[ETH_ALEN];
	s32 rssi_thold;
	s32 per_band_rssi_thold[NUM_NL80211_BANDS];
};

/**
 * struct cfg80211_sched_scan_plan - scan plan for scheduled scan
 *
 * @interval: interval between scheduled scan iterations. In seconds.
 * @iterations: number of scan iterations in this scan plan. Zero means
 *	infinite loop.
 *	The last scan plan will always have this parameter set to zero,
 *	all other scan plans will have a finite number of iterations.
 */
struct cfg80211_sched_scan_plan {
	u32 interval;
	u32 iterations;
};

/**
 * struct cfg80211_bss_select_adjust - BSS selection with RSSI adjustment.
 *
 * @band: band of BSS which should match for RSSI level adjustment.
 * @delta: value of RSSI level adjustment.
 */
struct cfg80211_bss_select_adjust {
	enum nl80211_band band;
	s8 delta;
};

/**
 * struct cfg80211_sched_scan_request - scheduled scan request description
 *
 * @reqid: identifies this request.
 * @ssids: SSIDs to scan for (passed in the probe_reqs in active scans)
 * @n_ssids: number of SSIDs
 * @n_channels: total number of channels to scan
 * @scan_width: channel width for scanning
 * @ie: optional information element(s) to add into Probe Request or %NULL
 * @ie_len: length of ie in octets
 * @flags: bit field of flags controlling operation
 * @match_sets: sets of parameters to be matched for a scan result
 *	entry to be considered valid and to be passed to the host
 *	(others are filtered out).
 *	If ommited, all results are passed.
 * @n_match_sets: number of match sets
 * @report_results: indicates that results were reported for this request
 * @wiphy: the wiphy this was for
 * @dev: the interface
 * @scan_start: start time of the scheduled scan
 * @channels: channels to scan
 * @min_rssi_thold: for drivers only supporting a single threshold, this
 *	contains the minimum over all matchsets
 * @mac_addr: MAC address used with randomisation
 * @mac_addr_mask: MAC address mask used with randomisation, bits that
 *	are 0 in the mask should be randomised, bits that are 1 should
 *	be taken from the @mac_addr
 * @scan_plans: scan plans to be executed in this scheduled scan. Lowest
 *	index must be executed first.
 * @n_scan_plans: number of scan plans, at least 1.
 * @rcu_head: RCU callback used to free the struct
 * @owner_nlportid: netlink portid of owner (if this should is a request
 *	owned by a particular socket)
 * @nl_owner_dead: netlink owner socket was closed - this request be freed
 * @list: for keeping list of requests.
 * @delay: delay in seconds to use before starting the first scan
 *	cycle.  The driver may ignore this parameter and start
 *	immediately (or at any other time), if this feature is not
 *	supported.
 * @relative_rssi_set: Indicates whether @relative_rssi is set or not.
 * @relative_rssi: Relative RSSI threshold in dB to restrict scan result
 *	reporting in connected state to cases where a matching BSS is determined
 *	to have better or slightly worse RSSI than the current connected BSS.
 *	The relative RSSI threshold values are ignored in disconnected state.
 * @rssi_adjust: delta dB of RSSI preference to be given to the BSSs that belong
 *	to the specified band while deciding whether a better BSS is reported
 *	using @relative_rssi. If delta is a negative number, the BSSs that
 *	belong to the specified band will be penalized by delta dB in relative
 *	comparisions.
 */
struct cfg80211_sched_scan_request {
	u64 reqid;
	struct cfg80211_ssid *ssids;
	int n_ssids;
	u32 n_channels;
	enum nl80211_bss_scan_width scan_width;
	const u8 *ie;
	size_t ie_len;
	u32 flags;
	struct cfg80211_match_set *match_sets;
	int n_match_sets;
	s32 min_rssi_thold;
	u32 delay;
	struct cfg80211_sched_scan_plan *scan_plans;
	int n_scan_plans;

	u8 mac_addr[ETH_ALEN] __aligned(2);
	u8 mac_addr_mask[ETH_ALEN] __aligned(2);

	bool relative_rssi_set;
	s8 relative_rssi;
	struct cfg80211_bss_select_adjust rssi_adjust;

	/* internal */
	struct wiphy *wiphy;
	struct net_device *dev;
	unsigned long scan_start;
	bool report_results;
	struct rcu_head rcu_head;
	u32 owner_nlportid;
	bool nl_owner_dead;
	struct list_head list;

	/* keep last */
	struct ieee80211_channel *channels[];
};

/**
 * enum cfg80211_signal_type - signal type
 *
 * @CFG80211_SIGNAL_TYPE_NONE: no signal strength information available
 * @CFG80211_SIGNAL_TYPE_MBM: signal strength in mBm (100*dBm)
 * @CFG80211_SIGNAL_TYPE_UNSPEC: signal strength, increasing from 0 through 100
 */
enum cfg80211_signal_type {
	CFG80211_SIGNAL_TYPE_NONE,
	CFG80211_SIGNAL_TYPE_MBM,
	CFG80211_SIGNAL_TYPE_UNSPEC,
};

/**
 * struct cfg80211_inform_bss - BSS inform data
 * @chan: channel the frame was received on
 * @scan_width: scan width that was used
 * @signal: signal strength value, according to the wiphy's
 *	signal type
 * @boottime_ns: timestamp (CLOCK_BOOTTIME) when the information was
 *	received; should match the time when the frame was actually
 *	received by the device (not just by the host, in case it was
 *	buffered on the device) and be accurate to about 10ms.
 *	If the frame isn't buffered, just passing the return value of
 *	ktime_get_boottime_ns() is likely appropriate.
 * @parent_tsf: the time at the start of reception of the first octet of the
 *	timestamp field of the frame. The time is the TSF of the BSS specified
 *	by %parent_bssid.
 * @parent_bssid: the BSS according to which %parent_tsf is set. This is set to
 *	the BSS that requested the scan in which the beacon/probe was received.
 * @chains: bitmask for filled values in @chain_signal.
 * @chain_signal: per-chain signal strength of last received BSS in dBm.
 */
struct cfg80211_inform_bss {
	struct ieee80211_channel *chan;
	enum nl80211_bss_scan_width scan_width;
	s32 signal;
	u64 boottime_ns;
	u64 parent_tsf;
	u8 parent_bssid[ETH_ALEN] __aligned(2);
	u8 chains;
	s8 chain_signal[IEEE80211_MAX_CHAINS];
};

/**
 * struct cfg80211_bss_ies - BSS entry IE data
 * @tsf: TSF contained in the frame that carried these IEs
 * @rcu_head: internal use, for freeing
 * @len: length of the IEs
 * @from_beacon: these IEs are known to come from a beacon
 * @data: IE data
 */
struct cfg80211_bss_ies {
	u64 tsf;
	struct rcu_head rcu_head;
	int len;
	bool from_beacon;
	u8 data[];
};

/**
 * struct cfg80211_bss - BSS description
 *
 * This structure describes a BSS (which may also be a mesh network)
 * for use in scan results and similar.
 *
 * @channel: channel this BSS is on
 * @scan_width: width of the control channel
 * @bssid: BSSID of the BSS
 * @beacon_interval: the beacon interval as from the frame
 * @capability: the capability field in host byte order
 * @ies: the information elements (Note that there is no guarantee that these
 *	are well-formed!); this is a pointer to either the beacon_ies or
 *	proberesp_ies depending on whether Probe Response frame has been
 *	received. It is always non-%NULL.
 * @beacon_ies: the information elements from the last Beacon frame
 *	(implementation note: if @hidden_beacon_bss is set this struct doesn't
 *	own the beacon_ies, but they're just pointers to the ones from the
 *	@hidden_beacon_bss struct)
 * @proberesp_ies: the information elements from the last Probe Response frame
 * @hidden_beacon_bss: in case this BSS struct represents a probe response from
 *	a BSS that hides the SSID in its beacon, this points to the BSS struct
 *	that holds the beacon data. @beacon_ies is still valid, of course, and
 *	points to the same data as hidden_beacon_bss->beacon_ies in that case.
 * @transmitted_bss: pointer to the transmitted BSS, if this is a
 *	non-transmitted one (multi-BSSID support)
 * @nontrans_list: list of non-transmitted BSS, if this is a transmitted one
 *	(multi-BSSID support)
 * @signal: signal strength value (type depends on the wiphy's signal_type)
 * @chains: bitmask for filled values in @chain_signal.
 * @chain_signal: per-chain signal strength of last received BSS in dBm.
 * @bssid_index: index in the multiple BSS set
 * @max_bssid_indicator: max number of members in the BSS set
 * @priv: private area for driver use, has at least wiphy->bss_priv_size bytes
 */
struct cfg80211_bss {
	struct ieee80211_channel *channel;
	enum nl80211_bss_scan_width scan_width;

	const struct cfg80211_bss_ies __rcu *ies;
	const struct cfg80211_bss_ies __rcu *beacon_ies;
	const struct cfg80211_bss_ies __rcu *proberesp_ies;

	struct cfg80211_bss *hidden_beacon_bss;
	struct cfg80211_bss *transmitted_bss;
	struct list_head nontrans_list;

	s32 signal;

	u16 beacon_interval;
	u16 capability;

	u8 bssid[ETH_ALEN];
	u8 chains;
	s8 chain_signal[IEEE80211_MAX_CHAINS];

	u8 bssid_index;
	u8 max_bssid_indicator;

	u8 priv[] __aligned(sizeof(void *));
};

/**
 * ieee80211_bss_get_elem - find element with given ID
 * @bss: the bss to search
 * @id: the element ID
 *
 * Note that the return value is an RCU-protected pointer, so
 * rcu_read_lock() must be held when calling this function.
 * Return: %NULL if not found.
 */
const struct element *ieee80211_bss_get_elem(struct cfg80211_bss *bss, u8 id);

/**
 * ieee80211_bss_get_ie - find IE with given ID
 * @bss: the bss to search
 * @id: the element ID
 *
 * Note that the return value is an RCU-protected pointer, so
 * rcu_read_lock() must be held when calling this function.
 * Return: %NULL if not found.
 */
static inline const u8 *ieee80211_bss_get_ie(struct cfg80211_bss *bss, u8 id)
{
	return (void *)ieee80211_bss_get_elem(bss, id);
}


/**
 * struct cfg80211_auth_request - Authentication request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * authentication.
 *
 * @bss: The BSS to authenticate with, the callee must obtain a reference
 *	to it if it needs to keep it.
 * @auth_type: Authentication type (algorithm)
 * @ie: Extra IEs to add to Authentication frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @key_len: length of WEP key for shared key authentication
 * @key_idx: index of WEP key for shared key authentication
 * @key: WEP key for shared key authentication
 * @auth_data: Fields and elements in Authentication frames. This contains
 *	the authentication frame body (non-IE and IE data), excluding the
 *	Authentication algorithm number, i.e., starting at the Authentication
 *	transaction sequence number field.
 * @auth_data_len: Length of auth_data buffer in octets
 */
struct cfg80211_auth_request {
	struct cfg80211_bss *bss;
	const u8 *ie;
	size_t ie_len;
	enum nl80211_auth_type auth_type;
	const u8 *key;
	u8 key_len, key_idx;
	const u8 *auth_data;
	size_t auth_data_len;
};

/**
 * enum cfg80211_assoc_req_flags - Over-ride default behaviour in association.
 *
 * @ASSOC_REQ_DISABLE_HT:  Disable HT (802.11n)
 * @ASSOC_REQ_DISABLE_VHT:  Disable VHT
 * @ASSOC_REQ_USE_RRM: Declare RRM capability in this association
 * @CONNECT_REQ_EXTERNAL_AUTH_SUPPORT: User space indicates external
 *	authentication capability. Drivers can offload authentication to
 *	userspace if this flag is set. Only applicable for cfg80211_connect()
 *	request (connect callback).
 */
enum cfg80211_assoc_req_flags {
	ASSOC_REQ_DISABLE_HT			= BIT(0),
	ASSOC_REQ_DISABLE_VHT			= BIT(1),
	ASSOC_REQ_USE_RRM			= BIT(2),
	CONNECT_REQ_EXTERNAL_AUTH_SUPPORT	= BIT(3),
};

/**
 * struct cfg80211_assoc_request - (Re)Association request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * (re)association.
 * @bss: The BSS to associate with. If the call is successful the driver is
 *	given a reference that it must give back to cfg80211_send_rx_assoc()
 *	or to cfg80211_assoc_timeout(). To ensure proper refcounting, new
 *	association requests while already associating must be rejected.
 * @ie: Extra IEs to add to (Re)Association Request frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @use_mfp: Use management frame protection (IEEE 802.11w) in this association
 * @crypto: crypto settings
 * @prev_bssid: previous BSSID, if not %NULL use reassociate frame. This is used
 *	to indicate a request to reassociate within the ESS instead of a request
 *	do the initial association with the ESS. When included, this is set to
 *	the BSSID of the current association, i.e., to the value that is
 *	included in the Current AP address field of the Reassociation Request
 *	frame.
 * @flags:  See &enum cfg80211_assoc_req_flags
 * @ht_capa:  HT Capabilities over-rides.  Values set in ht_capa_mask
 *	will be used in ht_capa.  Un-supported values will be ignored.
 * @ht_capa_mask:  The bits of ht_capa which are to be used.
 * @vht_capa: VHT capability override
 * @vht_capa_mask: VHT capability mask indicating which fields to use
 * @fils_kek: FILS KEK for protecting (Re)Association Request/Response frame or
 *	%NULL if FILS is not used.
 * @fils_kek_len: Length of fils_kek in octets
 * @fils_nonces: FILS nonces (part of AAD) for protecting (Re)Association
 *	Request/Response frame or %NULL if FILS is not used. This field starts
 *	with 16 octets of STA Nonce followed by 16 octets of AP Nonce.
 */
struct cfg80211_assoc_request {
	struct cfg80211_bss *bss;
	const u8 *ie, *prev_bssid;
	size_t ie_len;
	struct cfg80211_crypto_settings crypto;
	bool use_mfp;
	u32 flags;
	struct ieee80211_ht_cap ht_capa;
	struct ieee80211_ht_cap ht_capa_mask;
	struct ieee80211_vht_cap vht_capa, vht_capa_mask;
	const u8 *fils_kek;
	size_t fils_kek_len;
	const u8 *fils_nonces;
};

/**
 * struct cfg80211_deauth_request - Deauthentication request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * deauthentication.
 *
 * @bssid: the BSSID of the BSS to deauthenticate from
 * @ie: Extra IEs to add to Deauthentication frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @reason_code: The reason code for the deauthentication
 * @local_state_change: if set, change local state only and
 *	do not set a deauth frame
 */
struct cfg80211_deauth_request {
	const u8 *bssid;
	const u8 *ie;
	size_t ie_len;
	u16 reason_code;
	bool local_state_change;
};

/**
 * struct cfg80211_disassoc_request - Disassociation request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * disassociation.
 *
 * @bss: the BSS to disassociate from
 * @ie: Extra IEs to add to Disassociation frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @reason_code: The reason code for the disassociation
 * @local_state_change: This is a request for a local state only, i.e., no
 *	Disassociation frame is to be transmitted.
 */
struct cfg80211_disassoc_request {
	struct cfg80211_bss *bss;
	const u8 *ie;
	size_t ie_len;
	u16 reason_code;
	bool local_state_change;
};

/**
 * struct cfg80211_ibss_params - IBSS parameters
 *
 * This structure defines the IBSS parameters for the join_ibss()
 * method.
 *
 * @ssid: The SSID, will always be non-null.
 * @ssid_len: The length of the SSID, will always be non-zero.
 * @bssid: Fixed BSSID requested, maybe be %NULL, if set do not
 *	search for IBSSs with a different BSSID.
 * @chandef: defines the channel to use if no other IBSS to join can be found
 * @channel_fixed: The channel should be fixed -- do not search for
 *	IBSSs to join on other channels.
 * @ie: information element(s) to include in the beacon
 * @ie_len: length of that
 * @beacon_interval: beacon interval to use
 * @privacy: this is a protected network, keys will be configured
 *	after joining
 * @control_port: whether user space controls IEEE 802.1X port, i.e.,
 *	sets/clears %NL80211_STA_FLAG_AUTHORIZED. If true, the driver is
 *	required to assume that the port is unauthorized until authorized by
 *	user space. Otherwise, port is marked authorized by default.
 * @control_port_over_nl80211: TRUE if userspace expects to exchange control
 *	port frames over NL80211 instead of the network interface.
 * @userspace_handles_dfs: whether user space controls DFS operation, i.e.
 *	changes the channel when a radar is detected. This is required
 *	to operate on DFS channels.
 * @basic_rates: bitmap of basic rates to use when creating the IBSS
 * @mcast_rate: per-band multicast rate index + 1 (0: disabled)
 * @ht_capa:  HT Capabilities over-rides.  Values set in ht_capa_mask
 *	will be used in ht_capa.  Un-supported values will be ignored.
 * @ht_capa_mask:  The bits of ht_capa which are to be used.
 * @wep_keys: static WEP keys, if not NULL points to an array of
 *	CFG80211_MAX_WEP_KEYS WEP keys
 * @wep_tx_key: key index (0..3) of the default TX static WEP key
 */
struct cfg80211_ibss_params {
	const u8 *ssid;
	const u8 *bssid;
	struct cfg80211_chan_def chandef;
	const u8 *ie;
	u8 ssid_len, ie_len;
	u16 beacon_interval;
	u32 basic_rates;
	bool channel_fixed;
	bool privacy;
	bool control_port;
	bool control_port_over_nl80211;
	bool userspace_handles_dfs;
	int mcast_rate[NUM_NL80211_BANDS];
	struct ieee80211_ht_cap ht_capa;
	struct ieee80211_ht_cap ht_capa_mask;
	struct key_params *wep_keys;
	int wep_tx_key;
};

/**
 * struct cfg80211_bss_selection - connection parameters for BSS selection.
 *
 * @behaviour: requested BSS selection behaviour.
 * @param: parameters for requestion behaviour.
 * @band_pref: preferred band for %NL80211_BSS_SELECT_ATTR_BAND_PREF.
 * @adjust: parameters for %NL80211_BSS_SELECT_ATTR_RSSI_ADJUST.
 */
struct cfg80211_bss_selection {
	enum nl80211_bss_select_attr behaviour;
	union {
		enum nl80211_band band_pref;
		struct cfg80211_bss_select_adjust adjust;
	} param;
};

/**
 * struct cfg80211_connect_params - Connection parameters
 *
 * This structure provides information needed to complete IEEE 802.11
 * authentication and association.
 *
 * @channel: The channel to use or %NULL if not specified (auto-select based
 *	on scan results)
 * @channel_hint: The channel of the recommended BSS for initial connection or
 *	%NULL if not specified
 * @bssid: The AP BSSID or %NULL if not specified (auto-select based on scan
 *	results)
 * @bssid_hint: The recommended AP BSSID for initial connection to the BSS or
 *	%NULL if not specified. Unlike the @bssid parameter, the driver is
 *	allowed to ignore this @bssid_hint if it has knowledge of a better BSS
 *	to use.
 * @ssid: SSID
 * @ssid_len: Length of ssid in octets
 * @auth_type: Authentication type (algorithm)
 * @ie: IEs for association request
 * @ie_len: Length of assoc_ie in octets
 * @privacy: indicates whether privacy-enabled APs should be used
 * @mfp: indicate whether management frame protection is used
 * @crypto: crypto settings
 * @key_len: length of WEP key for shared key authentication
 * @key_idx: index of WEP key for shared key authentication
 * @key: WEP key for shared key authentication
 * @flags:  See &enum cfg80211_assoc_req_flags
 * @bg_scan_period:  Background scan period in seconds
 *	or -1 to indicate that default value is to be used.
 * @ht_capa:  HT Capabilities over-rides.  Values set in ht_capa_mask
 *	will be used in ht_capa.  Un-supported values will be ignored.
 * @ht_capa_mask:  The bits of ht_capa which are to be used.
 * @vht_capa:  VHT Capability overrides
 * @vht_capa_mask: The bits of vht_capa which are to be used.
 * @pbss: if set, connect to a PCP instead of AP. Valid for DMG
 *	networks.
 * @bss_select: criteria to be used for BSS selection.
 * @prev_bssid: previous BSSID, if not %NULL use reassociate frame. This is used
 *	to indicate a request to reassociate within the ESS instead of a request
 *	do the initial association with the ESS. When included, this is set to
 *	the BSSID of the current association, i.e., to the value that is
 *	included in the Current AP address field of the Reassociation Request
 *	frame.
 * @fils_erp_username: EAP re-authentication protocol (ERP) username part of the
 *	NAI or %NULL if not specified. This is used to construct FILS wrapped
 *	data IE.
 * @fils_erp_username_len: Length of @fils_erp_username in octets.
 * @fils_erp_realm: EAP re-authentication protocol (ERP) realm part of NAI or
 *	%NULL if not specified. This specifies the domain name of ER server and
 *	is used to construct FILS wrapped data IE.
 * @fils_erp_realm_len: Length of @fils_erp_realm in octets.
 * @fils_erp_next_seq_num: The next sequence number to use in the FILS ERP
 *	messages. This is also used to construct FILS wrapped data IE.
 * @fils_erp_rrk: ERP re-authentication Root Key (rRK) used to derive additional
 *	keys in FILS or %NULL if not specified.
 * @fils_erp_rrk_len: Length of @fils_erp_rrk in octets.
 * @want_1x: indicates user-space supports and wants to use 802.1X driver
 *	offload of 4-way handshake.
 * @edmg: define the EDMG channels.
 *	This may specify multiple channels and bonding options for the driver
 *	to choose from, based on BSS configuration.
 */
struct cfg80211_connect_params {
	struct ieee80211_channel *channel;
	struct ieee80211_channel *channel_hint;
	const u8 *bssid;
	const u8 *bssid_hint;
	const u8 *ssid;
	size_t ssid_len;
	enum nl80211_auth_type auth_type;
	const u8 *ie;
	size_t ie_len;
	bool privacy;
	enum nl80211_mfp mfp;
	struct cfg80211_crypto_settings crypto;
	const u8 *key;
	u8 key_len, key_idx;
	u32 flags;
	int bg_scan_period;
	struct ieee80211_ht_cap ht_capa;
	struct ieee80211_ht_cap ht_capa_mask;
	struct ieee80211_vht_cap vht_capa;
	struct ieee80211_vht_cap vht_capa_mask;
	bool pbss;
	struct cfg80211_bss_selection bss_select;
	const u8 *prev_bssid;
	const u8 *fils_erp_username;
	size_t fils_erp_username_len;
	const u8 *fils_erp_realm;
	size_t fils_erp_realm_len;
	u16 fils_erp_next_seq_num;
	const u8 *fils_erp_rrk;
	size_t fils_erp_rrk_len;
	bool want_1x;
	struct ieee80211_edmg edmg;
};

/**
 * enum cfg80211_connect_params_changed - Connection parameters being updated
 *
 * This enum provides information of all connect parameters that
 * have to be updated as part of update_connect_params() call.
 *
 * @UPDATE_ASSOC_IES: Indicates whether association request IEs are updated
 * @UPDATE_FILS_ERP_INFO: Indicates that FILS connection parameters (realm,
 *	username, erp sequence number and rrk) are updated
 * @UPDATE_AUTH_TYPE: Indicates that authentication type is updated
 */
enum cfg80211_connect_params_changed {
	UPDATE_ASSOC_IES		= BIT(0),
	UPDATE_FILS_ERP_INFO		= BIT(1),
	UPDATE_AUTH_TYPE		= BIT(2),
};

/**
 * enum wiphy_params_flags - set_wiphy_params bitfield values
 * @WIPHY_PARAM_RETRY_SHORT: wiphy->retry_short has changed
 * @WIPHY_PARAM_RETRY_LONG: wiphy->retry_long has changed
 * @WIPHY_PARAM_FRAG_THRESHOLD: wiphy->frag_threshold has changed
 * @WIPHY_PARAM_RTS_THRESHOLD: wiphy->rts_threshold has changed
 * @WIPHY_PARAM_COVERAGE_CLASS: coverage class changed
 * @WIPHY_PARAM_DYN_ACK: dynack has been enabled
 * @WIPHY_PARAM_TXQ_LIMIT: TXQ packet limit has been changed
 * @WIPHY_PARAM_TXQ_MEMORY_LIMIT: TXQ memory limit has been changed
 * @WIPHY_PARAM_TXQ_QUANTUM: TXQ scheduler quantum
 */
enum wiphy_params_flags {
	WIPHY_PARAM_RETRY_SHORT		= 1 << 0,
	WIPHY_PARAM_RETRY_LONG		= 1 << 1,
	WIPHY_PARAM_FRAG_THRESHOLD	= 1 << 2,
	WIPHY_PARAM_RTS_THRESHOLD	= 1 << 3,
	WIPHY_PARAM_COVERAGE_CLASS	= 1 << 4,
	WIPHY_PARAM_DYN_ACK		= 1 << 5,
	WIPHY_PARAM_TXQ_LIMIT		= 1 << 6,
	WIPHY_PARAM_TXQ_MEMORY_LIMIT	= 1 << 7,
	WIPHY_PARAM_TXQ_QUANTUM		= 1 << 8,
};

#define IEEE80211_DEFAULT_AIRTIME_WEIGHT	256

/* The per TXQ device queue limit in airtime */
#define IEEE80211_DEFAULT_AQL_TXQ_LIMIT_L	5000
#define IEEE80211_DEFAULT_AQL_TXQ_LIMIT_H	12000

/* The per interface airtime threshold to switch to lower queue limit */
#define IEEE80211_AQL_THRESHOLD			24000

/**
 * struct cfg80211_pmksa - PMK Security Association
 *
 * This structure is passed to the set/del_pmksa() method for PMKSA
 * caching.
 *
 * @bssid: The AP's BSSID (may be %NULL).
 * @pmkid: The identifier to refer a PMKSA.
 * @pmk: The PMK for the PMKSA identified by @pmkid. This is used for key
 *	derivation by a FILS STA. Otherwise, %NULL.
 * @pmk_len: Length of the @pmk. The length of @pmk can differ depending on
 *	the hash algorithm used to generate this.
 * @ssid: SSID to specify the ESS within which a PMKSA is valid when using FILS
 *	cache identifier (may be %NULL).
 * @ssid_len: Length of the @ssid in octets.
 * @cache_id: 2-octet cache identifier advertized by a FILS AP identifying the
 *	scope of PMKSA. This is valid only if @ssid_len is non-zero (may be
 *	%NULL).
 * @pmk_lifetime: Maximum lifetime for PMKSA in seconds
 *	(dot11RSNAConfigPMKLifetime) or 0 if not specified.
 *	The configured PMKSA must not be used for PMKSA caching after
 *	expiration and any keys derived from this PMK become invalid on
 *	expiration, i.e., the current association must be dropped if the PMK
 *	used for it expires.
 * @pmk_reauth_threshold: Threshold time for reauthentication (percentage of
 *	PMK lifetime, dot11RSNAConfigPMKReauthThreshold) or 0 if not specified.
 *	Drivers are expected to trigger a full authentication instead of using
 *	this PMKSA for caching when reassociating to a new BSS after this
 *	threshold to generate a new PMK before the current one expires.
 */
struct cfg80211_pmksa {
	const u8 *bssid;
	const u8 *pmkid;
	const u8 *pmk;
	size_t pmk_len;
	const u8 *ssid;
	size_t ssid_len;
	const u8 *cache_id;
	u32 pmk_lifetime;
	u8 pmk_reauth_threshold;
};

/**
 * struct cfg80211_pkt_pattern - packet pattern
 * @mask: bitmask where to match pattern and where to ignore bytes,
 *	one bit per byte, in same format as nl80211
 * @pattern: bytes to match where bitmask is 1
 * @pattern_len: length of pattern (in bytes)
 * @pkt_offset: packet offset (in bytes)
 *
 * Internal note: @mask and @pattern are allocated in one chunk of
 * memory, free @mask only!
 */
struct cfg80211_pkt_pattern {
	const u8 *mask, *pattern;
	int pattern_len;
	int pkt_offset;
};

/**
 * struct cfg80211_wowlan_tcp - TCP connection parameters
 *
 * @sock: (internal) socket for source port allocation
 * @src: source IP address
 * @dst: destination IP address
 * @dst_mac: destination MAC address
 * @src_port: source port
 * @dst_port: destination port
 * @payload_len: data payload length
 * @payload: data payload buffer
 * @payload_seq: payload sequence stamping configuration
 * @data_interval: interval at which to send data packets
 * @wake_len: wakeup payload match length
 * @wake_data: wakeup payload match data
 * @wake_mask: wakeup payload match mask
 * @tokens_size: length of the tokens buffer
 * @payload_tok: payload token usage configuration
 */
struct cfg80211_wowlan_tcp {
	struct socket *sock;
	__be32 src, dst;
	u16 src_port, dst_port;
	u8 dst_mac[ETH_ALEN];
	int payload_len;
	const u8 *payload;
	struct nl80211_wowlan_tcp_data_seq payload_seq;
	u32 data_interval;
	u32 wake_len;
	const u8 *wake_data, *wake_mask;
	u32 tokens_size;
	/* must be last, variable member */
	struct nl80211_wowlan_tcp_data_token payload_tok;
};

/**
 * struct cfg80211_wowlan - Wake on Wireless-LAN support info
 *
 * This structure defines the enabled WoWLAN triggers for the device.
 * @any: wake up on any activity -- special trigger if device continues
 *	operating as normal during suspend
 * @disconnect: wake up if getting disconnected
 * @magic_pkt: wake up on receiving magic packet
 * @patterns: wake up on receiving packet matching a pattern
 * @n_patterns: number of patterns
 * @gtk_rekey_failure: wake up on GTK rekey failure
 * @eap_identity_req: wake up on EAP identity request packet
 * @four_way_handshake: wake up on 4-way handshake
 * @rfkill_release: wake up when rfkill is released
 * @tcp: TCP connection establishment/wakeup parameters, see nl80211.h.
 *	NULL if not configured.
 * @nd_config: configuration for the scan to be used for net detect wake.
 */
struct cfg80211_wowlan {
	bool any, disconnect, magic_pkt, gtk_rekey_failure,
	     eap_identity_req, four_way_handshake,
	     rfkill_release;
	struct cfg80211_pkt_pattern *patterns;
	struct cfg80211_wowlan_tcp *tcp;
	int n_patterns;
	struct cfg80211_sched_scan_request *nd_config;
};

/**
 * struct cfg80211_coalesce_rules - Coalesce rule parameters
 *
 * This structure defines coalesce rule for the device.
 * @delay: maximum coalescing delay in msecs.
 * @condition: condition for packet coalescence.
 *	see &enum nl80211_coalesce_condition.
 * @patterns: array of packet patterns
 * @n_patterns: number of patterns
 */
struct cfg80211_coalesce_rules {
	int delay;
	enum nl80211_coalesce_condition condition;
	struct cfg80211_pkt_pattern *patterns;
	int n_patterns;
};

/**
 * struct cfg80211_coalesce - Packet coalescing settings
 *
 * This structure defines coalescing settings.
 * @rules: array of coalesce rules
 * @n_rules: number of rules
 */
struct cfg80211_coalesce {
	struct cfg80211_coalesce_rules *rules;
	int n_rules;
};

/**
 * struct cfg80211_wowlan_nd_match - information about the match
 *
 * @ssid: SSID of the match that triggered the wake up
 * @n_channels: Number of channels where the match occurred.  This
 *	value may be zero if the driver can't report the channels.
 * @channels: center frequencies of the channels where a match
 *	occurred (in MHz)
 */
struct cfg80211_wowlan_nd_match {
	struct cfg80211_ssid ssid;
	int n_channels;
	u32 channels[];
};

/**
 * struct cfg80211_wowlan_nd_info - net detect wake up information
 *
 * @n_matches: Number of match information instances provided in
 *	@matches.  This value may be zero if the driver can't provide
 *	match information.
 * @matches: Array of pointers to matches containing information about
 *	the matches that triggered the wake up.
 */
struct cfg80211_wowlan_nd_info {
	int n_matches;
	struct cfg80211_wowlan_nd_match *matches[];
};

/**
 * struct cfg80211_wowlan_wakeup - wakeup report
 * @disconnect: woke up by getting disconnected
 * @magic_pkt: woke up by receiving magic packet
 * @gtk_rekey_failure: woke up by GTK rekey failure
 * @eap_identity_req: woke up by EAP identity request packet
 * @four_way_handshake: woke up by 4-way handshake
 * @rfkill_release: woke up by rfkill being released
 * @pattern_idx: pattern that caused wakeup, -1 if not due to pattern
 * @packet_present_len: copied wakeup packet data
 * @packet_len: original wakeup packet length
 * @packet: The packet causing the wakeup, if any.
 * @packet_80211:  For pattern match, magic packet and other data
 *	frame triggers an 802.3 frame should be reported, for
 *	disconnect due to deauth 802.11 frame. This indicates which
 *	it is.
 * @tcp_match: TCP wakeup packet received
 * @tcp_connlost: TCP connection lost or failed to establish
 * @tcp_nomoretokens: TCP data ran out of tokens
 * @net_detect: if not %NULL, woke up because of net detect
 */
struct cfg80211_wowlan_wakeup {
	bool disconnect, magic_pkt, gtk_rekey_failure,
	     eap_identity_req, four_way_handshake,
	     rfkill_release, packet_80211,
	     tcp_match, tcp_connlost, tcp_nomoretokens;
	s32 pattern_idx;
	u32 packet_present_len, packet_len;
	const void *packet;
	struct cfg80211_wowlan_nd_info *net_detect;
};

/**
 * struct cfg80211_gtk_rekey_data - rekey data
 * @kek: key encryption key (@kek_len bytes)
 * @kck: key confirmation key (@kck_len bytes)
 * @replay_ctr: replay counter (NL80211_REPLAY_CTR_LEN bytes)
 * @kek_len: length of kek
 * @kck_len length of kck
 * @akm: akm (oui, id)
 */
struct cfg80211_gtk_rekey_data {
	const u8 *kek, *kck, *replay_ctr;
	u32 akm;
	u8 kek_len, kck_len;
};

/**
 * struct cfg80211_update_ft_ies_params - FT IE Information
 *
 * This structure provides information needed to update the fast transition IE
 *
 * @md: The Mobility Domain ID, 2 Octet value
 * @ie: Fast Transition IEs
 * @ie_len: Length of ft_ie in octets
 */
struct cfg80211_update_ft_ies_params {
	u16 md;
	const u8 *ie;
	size_t ie_len;
};

/**
 * struct cfg80211_mgmt_tx_params - mgmt tx parameters
 *
 * This structure provides information needed to transmit a mgmt frame
 *
 * @chan: channel to use
 * @offchan: indicates wether off channel operation is required
 * @wait: duration for ROC
 * @buf: buffer to transmit
 * @len: buffer length
 * @no_cck: don't use cck rates for this frame
 * @dont_wait_for_ack: tells the low level not to wait for an ack
 * @n_csa_offsets: length of csa_offsets array
 * @csa_offsets: array of all the csa offsets in the frame
 */
struct cfg80211_mgmt_tx_params {
	struct ieee80211_channel *chan;
	bool offchan;
	unsigned int wait;
	const u8 *buf;
	size_t len;
	bool no_cck;
	bool dont_wait_for_ack;
	int n_csa_offsets;
	const u16 *csa_offsets;
};

/**
 * struct cfg80211_dscp_exception - DSCP exception
 *
 * @dscp: DSCP value that does not adhere to the user priority range definition
 * @up: user priority value to which the corresponding DSCP value belongs
 */
struct cfg80211_dscp_exception {
	u8 dscp;
	u8 up;
};

/**
 * struct cfg80211_dscp_range - DSCP range definition for user priority
 *
 * @low: lowest DSCP value of this user priority range, inclusive
 * @high: highest DSCP value of this user priority range, inclusive
 */
struct cfg80211_dscp_range {
	u8 low;
	u8 high;
};

/* QoS Map Set element length defined in IEEE Std 802.11-2012, 8.4.2.97 */
#define IEEE80211_QOS_MAP_MAX_EX	21
#define IEEE80211_QOS_MAP_LEN_MIN	16
#define IEEE80211_QOS_MAP_LEN_MAX \
	(IEEE80211_QOS_MAP_LEN_MIN + 2 * IEEE80211_QOS_MAP_MAX_EX)

/**
 * struct cfg80211_qos_map - QoS Map Information
 *
 * This struct defines the Interworking QoS map setting for DSCP values
 *
 * @num_des: number of DSCP exceptions (0..21)
 * @dscp_exception: optionally up to maximum of 21 DSCP exceptions from
 *	the user priority DSCP range definition
 * @up: DSCP range definition for a particular user priority
 */
struct cfg80211_qos_map {
	u8 num_des;
	struct cfg80211_dscp_exception dscp_exception[IEEE80211_QOS_MAP_MAX_EX];
	struct cfg80211_dscp_range up[8];
};

/**
 * struct cfg80211_nan_conf - NAN configuration
 *
 * This struct defines NAN configuration parameters
 *
 * @master_pref: master preference (1 - 255)
 * @bands: operating bands, a bitmap of &enum nl80211_band values.
 *	For instance, for NL80211_BAND_2GHZ, bit 0 would be set
 *	(i.e. BIT(NL80211_BAND_2GHZ)).
 */
struct cfg80211_nan_conf {
	u8 master_pref;
	u8 bands;
};

/**
 * enum cfg80211_nan_conf_changes - indicates changed fields in NAN
 * configuration
 *
 * @CFG80211_NAN_CONF_CHANGED_PREF: master preference
 * @CFG80211_NAN_CONF_CHANGED_BANDS: operating bands
 */
enum cfg80211_nan_conf_changes {
	CFG80211_NAN_CONF_CHANGED_PREF = BIT(0),
	CFG80211_NAN_CONF_CHANGED_BANDS = BIT(1),
};

/**
 * struct cfg80211_nan_func_filter - a NAN function Rx / Tx filter
 *
 * @filter: the content of the filter
 * @len: the length of the filter
 */
struct cfg80211_nan_func_filter {
	const u8 *filter;
	u8 len;
};

/**
 * struct cfg80211_nan_func - a NAN function
 *
 * @type: &enum nl80211_nan_function_type
 * @service_id: the service ID of the function
 * @publish_type: &nl80211_nan_publish_type
 * @close_range: if true, the range should be limited. Threshold is
 *	implementation specific.
 * @publish_bcast: if true, the solicited publish should be broadcasted
 * @subscribe_active: if true, the subscribe is active
 * @followup_id: the instance ID for follow up
 * @followup_reqid: the requestor instance ID for follow up
 * @followup_dest: MAC address of the recipient of the follow up
 * @ttl: time to live counter in DW.
 * @serv_spec_info: Service Specific Info
 * @serv_spec_info_len: Service Specific Info length
 * @srf_include: if true, SRF is inclusive
 * @srf_bf: Bloom Filter
 * @srf_bf_len: Bloom Filter length
 * @srf_bf_idx: Bloom Filter index
 * @srf_macs: SRF MAC addresses
 * @srf_num_macs: number of MAC addresses in SRF
 * @rx_filters: rx filters that are matched with corresponding peer's tx_filter
 * @tx_filters: filters that should be transmitted in the SDF.
 * @num_rx_filters: length of &rx_filters.
 * @num_tx_filters: length of &tx_filters.
 * @instance_id: driver allocated id of the function.
 * @cookie: unique NAN function identifier.
 */
struct cfg80211_nan_func {
	enum nl80211_nan_function_type type;
	u8 service_id[NL80211_NAN_FUNC_SERVICE_ID_LEN];
	u8 publish_type;
	bool close_range;
	bool publish_bcast;
	bool subscribe_active;
	u8 followup_id;
	u8 followup_reqid;
	struct mac_address followup_dest;
	u32 ttl;
	const u8 *serv_spec_info;
	u8 serv_spec_info_len;
	bool srf_include;
	const u8 *srf_bf;
	u8 srf_bf_len;
	u8 srf_bf_idx;
	struct mac_address *srf_macs;
	int srf_num_macs;
	struct cfg80211_nan_func_filter *rx_filters;
	struct cfg80211_nan_func_filter *tx_filters;
	u8 num_tx_filters;
	u8 num_rx_filters;
	u8 instance_id;
	u64 cookie;
};

/**
 * struct cfg80211_pmk_conf - PMK configuration
 *
 * @aa: authenticator address
 * @pmk_len: PMK length in bytes.
 * @pmk: the PMK material
 * @pmk_r0_name: PMK-R0 Name. NULL if not applicable (i.e., the PMK
 *	is not PMK-R0). When pmk_r0_name is not NULL, the pmk field
 *	holds PMK-R0.
 */
struct cfg80211_pmk_conf {
	const u8 *aa;
	u8 pmk_len;
	const u8 *pmk;
	const u8 *pmk_r0_name;
};

/**
 * struct cfg80211_external_auth_params - Trigger External authentication.
 *
 * Commonly used across the external auth request and event interfaces.
 *
 * @action: action type / trigger for external authentication. Only significant
 *	for the authentication request event interface (driver to user space).
 * @bssid: BSSID of the peer with which the authentication has
 *	to happen. Used by both the authentication request event and
 *	authentication response command interface.
 * @ssid: SSID of the AP.  Used by both the authentication request event and
 *	authentication response command interface.
 * @key_mgmt_suite: AKM suite of the respective authentication. Used by the
 *	authentication request event interface.
 * @status: status code, %WLAN_STATUS_SUCCESS for successful authentication,
 *	use %WLAN_STATUS_UNSPECIFIED_FAILURE if user space cannot give you
 *	the real status code for failures. Used only for the authentication
 *	response command interface (user space to driver).
 * @pmkid: The identifier to refer a PMKSA.
 */
struct cfg80211_external_auth_params {
	enum nl80211_external_auth_action action;
	u8 bssid[ETH_ALEN] __aligned(2);
	struct cfg80211_ssid ssid;
	unsigned int key_mgmt_suite;
	u16 status;
	const u8 *pmkid;
};

/**
 * struct cfg80211_ftm_responder_stats - FTM responder statistics
 *
 * @filled: bitflag of flags using the bits of &enum nl80211_ftm_stats to
 *	indicate the relevant values in this struct for them
 * @success_num: number of FTM sessions in which all frames were successfully
 *	answered
 * @partial_num: number of FTM sessions in which part of frames were
 *	successfully answered
 * @failed_num: number of failed FTM sessions
 * @asap_num: number of ASAP FTM sessions
 * @non_asap_num: number of  non-ASAP FTM sessions
 * @total_duration_ms: total sessions durations - gives an indication
 *	of how much time the responder was busy
 * @unknown_triggers_num: number of unknown FTM triggers - triggers from
 *	initiators that didn't finish successfully the negotiation phase with
 *	the responder
 * @reschedule_requests_num: number of FTM reschedule requests - initiator asks
 *	for a new scheduling although it already has scheduled FTM slot
 * @out_of_window_triggers_num: total FTM triggers out of scheduled window
 */
struct cfg80211_ftm_responder_stats {
	u32 filled;
	u32 success_num;
	u32 partial_num;
	u32 failed_num;
	u32 asap_num;
	u32 non_asap_num;
	u64 total_duration_ms;
	u32 unknown_triggers_num;
	u32 reschedule_requests_num;
	u32 out_of_window_triggers_num;
};

/**
 * struct cfg80211_pmsr_ftm_result - FTM result
 * @failure_reason: if this measurement failed (PMSR status is
 *	%NL80211_PMSR_STATUS_FAILURE), this gives a more precise
 *	reason than just "failure"
 * @burst_index: if reporting partial results, this is the index
 *	in [0 .. num_bursts-1] of the burst that's being reported
 * @num_ftmr_attempts: number of FTM request frames transmitted
 * @num_ftmr_successes: number of FTM request frames acked
 * @busy_retry_time: if failure_reason is %NL80211_PMSR_FTM_FAILURE_PEER_BUSY,
 *	fill this to indicate in how many seconds a retry is deemed possible
 *	by the responder
 * @num_bursts_exp: actual number of bursts exponent negotiated
 * @burst_duration: actual burst duration negotiated
 * @ftms_per_burst: actual FTMs per burst negotiated
 * @lci_len: length of LCI information (if present)
 * @civicloc_len: length of civic location information (if present)
 * @lci: LCI data (may be %NULL)
 * @civicloc: civic location data (may be %NULL)
 * @rssi_avg: average RSSI over FTM action frames reported
 * @rssi_spread: spread of the RSSI over FTM action frames reported
 * @tx_rate: bitrate for transmitted FTM action frame response
 * @rx_rate: bitrate of received FTM action frame
 * @rtt_avg: average of RTTs measured (must have either this or @dist_avg)
 * @rtt_variance: variance of RTTs measured (note that standard deviation is
 *	the square root of the variance)
 * @rtt_spread: spread of the RTTs measured
 * @dist_avg: average of distances (mm) measured
 *	(must have either this or @rtt_avg)
 * @dist_variance: variance of distances measured (see also @rtt_variance)
 * @dist_spread: spread of distances measured (see also @rtt_spread)
 * @num_ftmr_attempts_valid: @num_ftmr_attempts is valid
 * @num_ftmr_successes_valid: @num_ftmr_successes is valid
 * @rssi_avg_valid: @rssi_avg is valid
 * @rssi_spread_valid: @rssi_spread is valid
 * @tx_rate_valid: @tx_rate is valid
 * @rx_rate_valid: @rx_rate is valid
 * @rtt_avg_valid: @rtt_avg is valid
 * @rtt_variance_valid: @rtt_variance is valid
 * @rtt_spread_valid: @rtt_spread is valid
 * @dist_avg_valid: @dist_avg is valid
 * @dist_variance_valid: @dist_variance is valid
 * @dist_spread_valid: @dist_spread is valid
 */
struct cfg80211_pmsr_ftm_result {
	const u8 *lci;
	const u8 *civicloc;
	unsigned int lci_len;
	unsigned int civicloc_len;
	enum nl80211_peer_measurement_ftm_failure_reasons failure_reason;
	u32 num_ftmr_attempts, num_ftmr_successes;
	s16 burst_index;
	u8 busy_retry_time;
	u8 num_bursts_exp;
	u8 burst_duration;
	u8 ftms_per_burst;
	s32 rssi_avg;
	s32 rssi_spread;
	struct rate_info tx_rate, rx_rate;
	s64 rtt_avg;
	s64 rtt_variance;
	s64 rtt_spread;
	s64 dist_avg;
	s64 dist_variance;
	s64 dist_spread;

	u16 num_ftmr_attempts_valid:1,
	    num_ftmr_successes_valid:1,
	    rssi_avg_valid:1,
	    rssi_spread_valid:1,
	    tx_rate_valid:1,
	    rx_rate_valid:1,
	    rtt_avg_valid:1,
	    rtt_variance_valid:1,
	    rtt_spread_valid:1,
	    dist_avg_valid:1,
	    dist_variance_valid:1,
	    dist_spread_valid:1;
};

/**
 * struct cfg80211_pmsr_result - peer measurement result
 * @addr: address of the peer
 * @host_time: host time (use ktime_get_boottime() adjust to the time when the
 *	measurement was made)
 * @ap_tsf: AP's TSF at measurement time
 * @status: status of the measurement
 * @final: if reporting partial results, mark this as the last one; if not
 *	reporting partial results always set this flag
 * @ap_tsf_valid: indicates the @ap_tsf value is valid
 * @type: type of the measurement reported, note that we only support reporting
 *	one type at a time, but you can report multiple results separately and
 *	they're all aggregated for userspace.
 */
struct cfg80211_pmsr_result {
	u64 host_time, ap_tsf;
	enum nl80211_peer_measurement_status status;

	u8 addr[ETH_ALEN];

	u8 final:1,
	   ap_tsf_valid:1;

	enum nl80211_peer_measurement_type type;

	union {
		struct cfg80211_pmsr_ftm_result ftm;
	};
};

/**
 * struct cfg80211_pmsr_ftm_request_peer - FTM request data
 * @requested: indicates FTM is requested
 * @preamble: frame preamble to use
 * @burst_period: burst period to use
 * @asap: indicates to use ASAP mode
 * @num_bursts_exp: number of bursts exponent
 * @burst_duration: burst duration
 * @ftms_per_burst: number of FTMs per burst
 * @ftmr_retries: number of retries for FTM request
 * @request_lci: request LCI information
 * @request_civicloc: request civic location information
 * @trigger_based: use trigger based ranging for the measurement
 *		 If neither @trigger_based nor @non_trigger_based is set,
 *		 EDCA based ranging will be used.
 * @non_trigger_based: use non trigger based ranging for the measurement
 *		 If neither @trigger_based nor @non_trigger_based is set,
 *		 EDCA based ranging will be used.
 *
 * See also nl80211 for the respective attribute documentation.
 */
struct cfg80211_pmsr_ftm_request_peer {
	enum nl80211_preamble preamble;
	u16 burst_period;
	u8 requested:1,
	   asap:1,
	   request_lci:1,
	   request_civicloc:1,
	   trigger_based:1,
	   non_trigger_based:1;
	u8 num_bursts_exp;
	u8 burst_duration;
	u8 ftms_per_burst;
	u8 ftmr_retries;
};

/**
 * struct cfg80211_pmsr_request_peer - peer data for a peer measurement request
 * @addr: MAC address
 * @chandef: channel to use
 * @report_ap_tsf: report the associated AP's TSF
 * @ftm: FTM data, see &struct cfg80211_pmsr_ftm_request_peer
 */
struct cfg80211_pmsr_request_peer {
	u8 addr[ETH_ALEN];
	struct cfg80211_chan_def chandef;
	u8 report_ap_tsf:1;
	struct cfg80211_pmsr_ftm_request_peer ftm;
};

/**
 * struct cfg80211_pmsr_request - peer measurement request
 * @cookie: cookie, set by cfg80211
 * @nl_portid: netlink portid - used by cfg80211
 * @drv_data: driver data for this request, if required for aborting,
 *	not otherwise freed or anything by cfg80211
 * @mac_addr: MAC address used for (randomised) request
 * @mac_addr_mask: MAC address mask used for randomisation, bits that
 *	are 0 in the mask should be randomised, bits that are 1 should
 *	be taken from the @mac_addr
 * @list: used by cfg80211 to hold on to the request
 * @timeout: timeout (in milliseconds) for the whole operation, if
 *	zero it means there's no timeout
 * @n_peers: number of peers to do measurements with
 * @peers: per-peer measurement request data
 */
struct cfg80211_pmsr_request {
	u64 cookie;
	void *drv_data;
	u32 n_peers;
	u32 nl_portid;

	u32 timeout;

	u8 mac_addr[ETH_ALEN] __aligned(2);
	u8 mac_addr_mask[ETH_ALEN] __aligned(2);

	struct list_head list;

	struct cfg80211_pmsr_request_peer peers[];
};

/**
 * struct cfg80211_update_owe_info - OWE Information
 *
 * This structure provides information needed for the drivers to offload OWE
 * (Opportunistic Wireless Encryption) processing to the user space.
 *
 * Commonly used across update_owe_info request and event interfaces.
 *
 * @peer: MAC address of the peer device for which the OWE processing
 *	has to be done.
 * @status: status code, %WLAN_STATUS_SUCCESS for successful OWE info
 *	processing, use %WLAN_STATUS_UNSPECIFIED_FAILURE if user space
 *	cannot give you the real status code for failures. Used only for
 *	OWE update request command interface (user space to driver).
 * @ie: IEs obtained from the peer or constructed by the user space. These are
 *	the IEs of the remote peer in the event from the host driver and
 *	the constructed IEs by the user space in the request interface.
 * @ie_len: Length of IEs in octets.
 */
struct cfg80211_update_owe_info {
	u8 peer[ETH_ALEN] __aligned(2);
	u16 status;
	const u8 *ie;
	size_t ie_len;
};

/**
 * struct mgmt_frame_regs - management frame registrations data
 * @global_stypes: bitmap of management frame subtypes registered
 *	for the entire device
 * @interface_stypes: bitmap of management frame subtypes registered
 *	for the given interface
 * @global_mcast_rx: mcast RX is needed globally for these subtypes
 * @interface_mcast_stypes: mcast RX is needed on this interface
 *	for these subtypes
 */
struct mgmt_frame_regs {
	u32 global_stypes, interface_stypes;
	u32 global_mcast_stypes, interface_mcast_stypes;
};

/**
 * struct cfg80211_ops - backend description for wireless configuration
 *
 * This struct is registered by fullmac card drivers and/or wireless stacks
 * in order to handle configuration requests on their interfaces.
 *
 * All callbacks except where otherwise noted should return 0
 * on success or a negative error code.
 *
 * All operations are currently invoked under rtnl for consistency with the
 * wireless extensions but this is subject to reevaluation as soon as this
 * code is used more widely and we have a first user without wext.
 *
 * @suspend: wiphy device needs to be suspended. The variable @wow will
 *	be %NULL or contain the enabled Wake-on-Wireless triggers that are
 *	configured for the device.
 * @resume: wiphy device needs to be resumed
 * @set_wakeup: Called when WoWLAN is enabled/disabled, use this callback
 *	to call device_set_wakeup_enable() to enable/disable wakeup from
 *	the device.
 *
 * @add_virtual_intf: create a new virtual interface with the given name,
 *	must set the struct wireless_dev's iftype. Beware: You must create
 *	the new netdev in the wiphy's network namespace! Returns the struct
 *	wireless_dev, or an ERR_PTR. For P2P device wdevs, the driver must
 *	also set the address member in the wdev.
 *
 * @del_virtual_intf: remove the virtual interface
 *
 * @change_virtual_intf: change type/configuration of virtual interface,
 *	keep the struct wireless_dev's iftype updated.
 *
 * @add_key: add a key with the given parameters. @mac_addr will be %NULL
 *	when adding a group key.
 *
 * @get_key: get information about the key with the given parameters.
 *	@mac_addr will be %NULL when requesting information for a group
 *	key. All pointers given to the @callback function need not be valid
 *	after it returns. This function should return an error if it is
 *	not possible to retrieve the key, -ENOENT if it doesn't exist.
 *
 * @del_key: remove a key given the @mac_addr (%NULL for a group key)
 *	and @key_index, return -ENOENT if the key doesn't exist.
 *
 * @set_default_key: set the default key on an interface
 *
 * @set_default_mgmt_key: set the default management frame key on an interface
 *
 * @set_default_beacon_key: set the default Beacon frame key on an interface
 *
 * @set_rekey_data: give the data necessary for GTK rekeying to the driver
 *
 * @start_ap: Start acting in AP mode defined by the parameters.
 * @change_beacon: Change the beacon parameters for an access point mode
 *	interface. This should reject the call when AP mode wasn't started.
 * @stop_ap: Stop being an AP, including stopping beaconing.
 *
 * @add_station: Add a new station.
 * @del_station: Remove a station
 * @change_station: Modify a given station. Note that flags changes are not much
 *	validated in cfg80211, in particular the auth/assoc/authorized flags
 *	might come to the driver in invalid combinations -- make sure to check
 *	them, also against the existing state! Drivers must call
 *	cfg80211_check_station_change() to validate the information.
 * @get_station: get station information for the station identified by @mac
 * @dump_station: dump station callback -- resume dump at index @idx
 *
 * @add_mpath: add a fixed mesh path
 * @del_mpath: delete a given mesh path
 * @change_mpath: change a given mesh path
 * @get_mpath: get a mesh path for the given parameters
 * @dump_mpath: dump mesh path callback -- resume dump at index @idx
 * @get_mpp: get a mesh proxy path for the given parameters
 * @dump_mpp: dump mesh proxy path callback -- resume dump at index @idx
 * @join_mesh: join the mesh network with the specified parameters
 *	(invoked with the wireless_dev mutex held)
 * @leave_mesh: leave the current mesh network
 *	(invoked with the wireless_dev mutex held)
 *
 * @get_mesh_config: Get the current mesh configuration
 *
 * @update_mesh_config: Update mesh parameters on a running mesh.
 *	The mask is a bitfield which tells us which parameters to
 *	set, and which to leave alone.
 *
 * @change_bss: Modify parameters for a given BSS.
 *
 * @set_txq_params: Set TX queue parameters
 *
 * @libertas_set_mesh_channel: Only for backward compatibility for libertas,
 *	as it doesn't implement join_mesh and needs to set the channel to
 *	join the mesh instead.
 *
 * @set_monitor_channel: Set the monitor mode channel for the device. If other
 *	interfaces are active this callback should reject the configuration.
 *	If no interfaces are active or the device is down, the channel should
 *	be stored for when a monitor interface becomes active.
 *
 * @scan: Request to do a scan. If returning zero, the scan request is given
 *	the driver, and will be valid until passed to cfg80211_scan_done().
 *	For scan results, call cfg80211_inform_bss(); you can call this outside
 *	the scan/scan_done bracket too.
 * @abort_scan: Tell the driver to abort an ongoing scan. The driver shall
 *	indicate the status of the scan through cfg80211_scan_done().
 *
 * @auth: Request to authenticate with the specified peer
 *	(invoked with the wireless_dev mutex held)
 * @assoc: Request to (re)associate with the specified peer
 *	(invoked with the wireless_dev mutex held)
 * @deauth: Request to deauthenticate from the specified peer
 *	(invoked with the wireless_dev mutex held)
 * @disassoc: Request to disassociate from the specified peer
 *	(invoked with the wireless_dev mutex held)
 *
 * @connect: Connect to the ESS with the specified parameters. When connected,
 *	call cfg80211_connect_result()/cfg80211_connect_bss() with status code
 *	%WLAN_STATUS_SUCCESS. If the connection fails for some reason, call
 *	cfg80211_connect_result()/cfg80211_connect_bss() with the status code
 *	from the AP or cfg80211_connect_timeout() if no frame with status code
 *	was received.
 *	The driver is allowed to roam to other BSSes within the ESS when the
 *	other BSS matches the connect parameters. When such roaming is initiated
 *	by the driver, the driver is expected to verify that the target matches
 *	the configured security parameters and to use Reassociation Request
 *	frame instead of Association Request frame.
 *	The connect function can also be used to request the driver to perform a
 *	specific roam when connected to an ESS. In that case, the prev_bssid
 *	parameter is set to the BSSID of the currently associated BSS as an
 *	indication of requesting reassociation.
 *	In both the driver-initiated and new connect() call initiated roaming
 *	cases, the result of roaming is indicated with a call to
 *	cfg80211_roamed(). (invoked with the wireless_dev mutex held)
 * @update_connect_params: Update the connect parameters while connected to a
 *	BSS. The updated parameters can be used by driver/firmware for
 *	subsequent BSS selection (roaming) decisions and to form the
 *	Authentication/(Re)Association Request frames. This call does not
 *	request an immediate disassociation or reassociation with the current
 *	BSS, i.e., this impacts only subsequent (re)associations. The bits in
 *	changed are defined in &enum cfg80211_connect_params_changed.
 *	(invoked with the wireless_dev mutex held)
 * @disconnect: Disconnect from the BSS/ESS or stop connection attempts if
 *      connection is in progress. Once done, call cfg80211_disconnected() in
 *      case connection was already established (invoked with the
 *      wireless_dev mutex held), otherwise call cfg80211_connect_timeout().
 *
 * @join_ibss: Join the specified IBSS (or create if necessary). Once done, call
 *	cfg80211_ibss_joined(), also call that function when changing BSSID due
 *	to a merge.
 *	(invoked with the wireless_dev mutex held)
 * @leave_ibss: Leave the IBSS.
 *	(invoked with the wireless_dev mutex held)
 *
 * @set_mcast_rate: Set the specified multicast rate (only if vif is in ADHOC or
 *	MESH mode)
 *
 * @set_wiphy_params: Notify that wiphy parameters have changed;
 *	@changed bitfield (see &enum wiphy_params_flags) describes which values
 *	have changed. The actual parameter values are available in
 *	struct wiphy. If returning an error, no value should be changed.
 *
 * @set_tx_power: set the transmit power according to the parameters,
 *	the power passed is in mBm, to get dBm use MBM_TO_DBM(). The
 *	wdev may be %NULL if power was set for the wiphy, and will
 *	always be %NULL unless the driver supports per-vif TX power
 *	(as advertised by the nl80211 feature flag.)
 * @get_tx_power: store the current TX power into the dbm variable;
 *	return 0 if successful
 *
 * @set_wds_peer: set the WDS peer for a WDS interface
 *
 * @rfkill_poll: polls the hw rfkill line, use cfg80211 reporting
 *	functions to adjust rfkill hw state
 *
 * @dump_survey: get site survey information.
 *
 * @remain_on_channel: Request the driver to remain awake on the specified
 *	channel for the specified duration to complete an off-channel
 *	operation (e.g., public action frame exchange). When the driver is
 *	ready on the requested channel, it must indicate this with an event
 *	notification by calling cfg80211_ready_on_channel().
 * @cancel_remain_on_channel: Cancel an on-going remain-on-channel operation.
 *	This allows the operation to be terminated prior to timeout based on
 *	the duration value.
 * @mgmt_tx: Transmit a management frame.
 * @mgmt_tx_cancel_wait: Cancel the wait time from transmitting a management
 *	frame on another channel
 *
 * @testmode_cmd: run a test mode command; @wdev may be %NULL
 * @testmode_dump: Implement a test mode dump. The cb->args[2] and up may be
 *	used by the function, but 0 and 1 must not be touched. Additionally,
 *	return error codes other than -ENOBUFS and -ENOENT will terminate the
 *	dump and return to userspace with an error, so be careful. If any data
 *	was passed in from userspace then the data/len arguments will be present
 *	and point to the data contained in %NL80211_ATTR_TESTDATA.
 *
 * @set_bitrate_mask: set the bitrate mask configuration
 *
 * @set_pmksa: Cache a PMKID for a BSSID. This is mostly useful for fullmac
 *	devices running firmwares capable of generating the (re) association
 *	RSN IE. It allows for faster roaming between WPA2 BSSIDs.
 * @del_pmksa: Delete a cached PMKID.
 * @flush_pmksa: Flush all cached PMKIDs.
 * @set_power_mgmt: Configure WLAN power management. A timeout value of -1
 *	allows the driver to adjust the dynamic ps timeout value.
 * @set_cqm_rssi_config: Configure connection quality monitor RSSI threshold.
 *	After configuration, the driver should (soon) send an event indicating
 *	the current level is above/below the configured threshold; this may
 *	need some care when the configuration is changed (without first being
 *	disabled.)
 * @set_cqm_rssi_range_config: Configure two RSSI thresholds in the
 *	connection quality monitor.  An event is to be sent only when the
 *	signal level is found to be outside the two values.  The driver should
 *	set %NL80211_EXT_FEATURE_CQM_RSSI_LIST if this method is implemented.
 *	If it is provided then there's no point providing @set_cqm_rssi_config.
 * @set_cqm_txe_config: Configure connection quality monitor TX error
 *	thresholds.
 * @sched_scan_start: Tell the driver to start a scheduled scan.
 * @sched_scan_stop: Tell the driver to stop an ongoing scheduled scan with
 *	given request id. This call must stop the scheduled scan and be ready
 *	for starting a new one before it returns, i.e. @sched_scan_start may be
 *	called immediately after that again and should not fail in that case.
 *	The driver should not call cfg80211_sched_scan_stopped() for a requested
 *	stop (when this method returns 0).
 *
 * @update_mgmt_frame_registrations: Notify the driver that management frame
 *	registrations were updated. The callback is allowed to sleep.
 *
 * @set_antenna: Set antenna configuration (tx_ant, rx_ant) on the device.
 *	Parameters are bitmaps of allowed antennas to use for TX/RX. Drivers may
 *	reject TX/RX mask combinations they cannot support by returning -EINVAL
 *	(also see nl80211.h @NL80211_ATTR_WIPHY_ANTENNA_TX).
 *
 * @get_antenna: Get current antenna configuration from device (tx_ant, rx_ant).
 *
 * @tdls_mgmt: Transmit a TDLS management frame.
 * @tdls_oper: Perform a high-level TDLS operation (e.g. TDLS link setup).
 *
 * @probe_client: probe an associated client, must return a cookie that it
 *	later passes to cfg80211_probe_status().
 *
 * @set_noack_map: Set the NoAck Map for the TIDs.
 *
 * @get_channel: Get the current operating channel for the virtual interface.
 *	For monitor interfaces, it should return %NULL unless there's a single
 *	current monitoring channel.
 *
 * @start_p2p_device: Start the given P2P device.
 * @stop_p2p_device: Stop the given P2P device.
 *
 * @set_mac_acl: Sets MAC address control list in AP and P2P GO mode.
 *	Parameters include ACL policy, an array of MAC address of stations
 *	and the number of MAC addresses. If there is already a list in driver
 *	this new list replaces the existing one. Driver has to clear its ACL
 *	when number of MAC addresses entries is passed as 0. Drivers which
 *	advertise the support for MAC based ACL have to implement this callback.
 *
 * @start_radar_detection: Start radar detection in the driver.
 *
 * @end_cac: End running CAC, probably because a related CAC
 *	was finished on another phy.
 *
 * @update_ft_ies: Provide updated Fast BSS Transition information to the
 *	driver. If the SME is in the driver/firmware, this information can be
 *	used in building Authentication and Reassociation Request frames.
 *
 * @crit_proto_start: Indicates a critical protocol needs more link reliability
 *	for a given duration (milliseconds). The protocol is provided so the
 *	driver can take the most appropriate actions.
 * @crit_proto_stop: Indicates critical protocol no longer needs increased link
 *	reliability. This operation can not fail.
 * @set_coalesce: Set coalesce parameters.
 *
 * @channel_switch: initiate channel-switch procedure (with CSA). Driver is
 *	responsible for veryfing if the switch is possible. Since this is
 *	inherently tricky driver may decide to disconnect an interface later
 *	with cfg80211_stop_iface(). This doesn't mean driver can accept
 *	everything. It should do it's best to verify requests and reject them
 *	as soon as possible.
 *
 * @set_qos_map: Set QoS mapping information to the driver
 *
 * @set_ap_chanwidth: Set the AP (including P2P GO) mode channel width for the
 *	given interface This is used e.g. for dynamic HT 20/40 MHz channel width
 *	changes during the lifetime of the BSS.
 *
 * @add_tx_ts: validate (if admitted_time is 0) or add a TX TS to the device
 *	with the given parameters; action frame exchange has been handled by
 *	userspace so this just has to modify the TX path to take the TS into
 *	account.
 *	If the admitted time is 0 just validate the parameters to make sure
 *	the session can be created at all; it is valid to just always return
 *	success for that but that may result in inefficient behaviour (handshake
 *	with the peer followed by immediate teardown when the addition is later
 *	rejected)
 * @del_tx_ts: remove an existing TX TS
 *
 * @join_ocb: join the OCB network with the specified parameters
 *	(invoked with the wireless_dev mutex held)
 * @leave_ocb: leave the current OCB network
 *	(invoked with the wireless_dev mutex held)
 *
 * @tdls_channel_switch: Start channel-switching with a TDLS peer. The driver
 *	is responsible for continually initiating channel-switching operations
 *	and returning to the base channel for communication with the AP.
 * @tdls_cancel_channel_switch: Stop channel-switching with a TDLS peer. Both
 *	peers must be on the base channel when the call completes.
 * @start_nan: Start the NAN interface.
 * @stop_nan: Stop the NAN interface.
 * @add_nan_func: Add a NAN function. Returns negative value on failure.
 *	On success @nan_func ownership is transferred to the driver and
 *	it may access it outside of the scope of this function. The driver
 *	should free the @nan_func when no longer needed by calling
 *	cfg80211_free_nan_func().
 *	On success the driver should assign an instance_id in the
 *	provided @nan_func.
 * @del_nan_func: Delete a NAN function.
 * @nan_change_conf: changes NAN configuration. The changed parameters must
 *	be specified in @changes (using &enum cfg80211_nan_conf_changes);
 *	All other parameters must be ignored.
 *
 * @set_multicast_to_unicast: configure multicast to unicast conversion for BSS
 *
 * @get_txq_stats: Get TXQ stats for interface or phy. If wdev is %NULL, this
 *      function should return phy stats, and interface stats otherwise.
 *
 * @set_pmk: configure the PMK to be used for offloaded 802.1X 4-Way handshake.
 *	If not deleted through @del_pmk the PMK remains valid until disconnect
 *	upon which the driver should clear it.
 *	(invoked with the wireless_dev mutex held)
 * @del_pmk: delete the previously configured PMK for the given authenticator.
 *	(invoked with the wireless_dev mutex held)
 *
 * @external_auth: indicates result of offloaded authentication processing from
 *     user space
 *
 * @tx_control_port: TX a control port frame (EAPoL).  The noencrypt parameter
 *	tells the driver that the frame should not be encrypted.
 *
 * @get_ftm_responder_stats: Retrieve FTM responder statistics, if available.
 *	Statistics should be cumulative, currently no way to reset is provided.
 * @start_pmsr: start peer measurement (e.g. FTM)
 * @abort_pmsr: abort peer measurement
 *
 * @update_owe_info: Provide updated OWE info to driver. Driver implementing SME
 *	but offloading OWE processing to the user space will get the updated
 *	DH IE through this interface.
 *
 * @probe_mesh_link: Probe direct Mesh peer's link quality by sending data frame
 *	and overrule HWMP path selection algorithm.
 * @set_tid_config: TID specific configuration, this can be peer or BSS specific
 *	This callback may sleep.
 * @reset_tid_config: Reset TID specific configuration for the peer, for the
 *	given TIDs. This callback may sleep.
 */
struct cfg80211_ops {
	int	(*suspend)(struct wiphy *wiphy, struct cfg80211_wowlan *wow);
	int	(*resume)(struct wiphy *wiphy);
	void	(*set_wakeup)(struct wiphy *wiphy, bool enabled);

	struct wireless_dev * (*add_virtual_intf)(struct wiphy *wiphy,
						  const char *name,
						  unsigned char name_assign_type,
						  enum nl80211_iftype type,
						  struct vif_params *params);
	int	(*del_virtual_intf)(struct wiphy *wiphy,
				    struct wireless_dev *wdev);
	int	(*change_virtual_intf)(struct wiphy *wiphy,
				       struct net_device *dev,
				       enum nl80211_iftype type,
				       struct vif_params *params);

	int	(*add_key)(struct wiphy *wiphy, struct net_device *netdev,
			   u8 key_index, bool pairwise, const u8 *mac_addr,
			   struct key_params *params);
	int	(*get_key)(struct wiphy *wiphy, struct net_device *netdev,
			   u8 key_index, bool pairwise, const u8 *mac_addr,
			   void *cookie,
			   void (*callback)(void *cookie, struct key_params*));
	int	(*del_key)(struct wiphy *wiphy, struct net_device *netdev,
			   u8 key_index, bool pairwise, const u8 *mac_addr);
	int	(*set_default_key)(struct wiphy *wiphy,
				   struct net_device *netdev,
				   u8 key_index, bool unicast, bool multicast);
	int	(*set_default_mgmt_key)(struct wiphy *wiphy,
					struct net_device *netdev,
					u8 key_index);
	int	(*set_default_beacon_key)(struct wiphy *wiphy,
					  struct net_device *netdev,
					  u8 key_index);

	int	(*start_ap)(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_ap_settings *settings);
	int	(*change_beacon)(struct wiphy *wiphy, struct net_device *dev,
				 struct cfg80211_beacon_data *info);
	int	(*stop_ap)(struct wiphy *wiphy, struct net_device *dev);


	int	(*add_station)(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *mac,
			       struct station_parameters *params);
	int	(*del_station)(struct wiphy *wiphy, struct net_device *dev,
			       struct station_del_parameters *params);
	int	(*change_station)(struct wiphy *wiphy, struct net_device *dev,
				  const u8 *mac,
				  struct station_parameters *params);
	int	(*get_station)(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *mac, struct station_info *sinfo);
	int	(*dump_station)(struct wiphy *wiphy, struct net_device *dev,
				int idx, u8 *mac, struct station_info *sinfo);

	int	(*add_mpath)(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *dst, const u8 *next_hop);
	int	(*del_mpath)(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *dst);
	int	(*change_mpath)(struct wiphy *wiphy, struct net_device *dev,
				  const u8 *dst, const u8 *next_hop);
	int	(*get_mpath)(struct wiphy *wiphy, struct net_device *dev,
			     u8 *dst, u8 *next_hop, struct mpath_info *pinfo);
	int	(*dump_mpath)(struct wiphy *wiphy, struct net_device *dev,
			      int idx, u8 *dst, u8 *next_hop,
			      struct mpath_info *pinfo);
	int	(*get_mpp)(struct wiphy *wiphy, struct net_device *dev,
			   u8 *dst, u8 *mpp, struct mpath_info *pinfo);
	int	(*dump_mpp)(struct wiphy *wiphy, struct net_device *dev,
			    int idx, u8 *dst, u8 *mpp,
			    struct mpath_info *pinfo);
	int	(*get_mesh_config)(struct wiphy *wiphy,
				struct net_device *dev,
				struct mesh_config *conf);
	int	(*update_mesh_config)(struct wiphy *wiphy,
				      struct net_device *dev, u32 mask,
				      const struct mesh_config *nconf);
	int	(*join_mesh)(struct wiphy *wiphy, struct net_device *dev,
			     const struct mesh_config *conf,
			     const struct mesh_setup *setup);
	int	(*leave_mesh)(struct wiphy *wiphy, struct net_device *dev);

	int	(*join_ocb)(struct wiphy *wiphy, struct net_device *dev,
			    struct ocb_setup *setup);
	int	(*leave_ocb)(struct wiphy *wiphy, struct net_device *dev);

	int	(*change_bss)(struct wiphy *wiphy, struct net_device *dev,
			      struct bss_parameters *params);

	int	(*set_txq_params)(struct wiphy *wiphy, struct net_device *dev,
				  struct ieee80211_txq_params *params);

	int	(*libertas_set_mesh_channel)(struct wiphy *wiphy,
					     struct net_device *dev,
					     struct ieee80211_channel *chan);

	int	(*set_monitor_channel)(struct wiphy *wiphy,
				       struct cfg80211_chan_def *chandef);

	int	(*scan)(struct wiphy *wiphy,
			struct cfg80211_scan_request *request);
	void	(*abort_scan)(struct wiphy *wiphy, struct wireless_dev *wdev);

	int	(*auth)(struct wiphy *wiphy, struct net_device *dev,
			struct cfg80211_auth_request *req);
	int	(*assoc)(struct wiphy *wiphy, struct net_device *dev,
			 struct cfg80211_assoc_request *req);
	int	(*deauth)(struct wiphy *wiphy, struct net_device *dev,
			  struct cfg80211_deauth_request *req);
	int	(*disassoc)(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_disassoc_request *req);

	int	(*connect)(struct wiphy *wiphy, struct net_device *dev,
			   struct cfg80211_connect_params *sme);
	int	(*update_connect_params)(struct wiphy *wiphy,
					 struct net_device *dev,
					 struct cfg80211_connect_params *sme,
					 u32 changed);
	int	(*disconnect)(struct wiphy *wiphy, struct net_device *dev,
			      u16 reason_code);

	int	(*join_ibss)(struct wiphy *wiphy, struct net_device *dev,
			     struct cfg80211_ibss_params *params);
	int	(*leave_ibss)(struct wiphy *wiphy, struct net_device *dev);

	int	(*set_mcast_rate)(struct wiphy *wiphy, struct net_device *dev,
				  int rate[NUM_NL80211_BANDS]);

	int	(*set_wiphy_params)(struct wiphy *wiphy, u32 changed);

	int	(*set_tx_power)(struct wiphy *wiphy, struct wireless_dev *wdev,
				enum nl80211_tx_power_setting type, int mbm);
	int	(*get_tx_power)(struct wiphy *wiphy, struct wireless_dev *wdev,
				int *dbm);

	int	(*set_wds_peer)(struct wiphy *wiphy, struct net_device *dev,
				const u8 *addr);

	void	(*rfkill_poll)(struct wiphy *wiphy);

#ifdef CONFIG_NL80211_TESTMODE
	int	(*testmode_cmd)(struct wiphy *wiphy, struct wireless_dev *wdev,
				void *data, int len);
	int	(*testmode_dump)(struct wiphy *wiphy, struct sk_buff *skb,
				 struct netlink_callback *cb,
				 void *data, int len);
#endif

	int	(*set_bitrate_mask)(struct wiphy *wiphy,
				    struct net_device *dev,
				    const u8 *peer,
				    const struct cfg80211_bitrate_mask *mask);

	int	(*dump_survey)(struct wiphy *wiphy, struct net_device *netdev,
			int idx, struct survey_info *info);

	int	(*set_pmksa)(struct wiphy *wiphy, struct net_device *netdev,
			     struct cfg80211_pmksa *pmksa);
	int	(*del_pmksa)(struct wiphy *wiphy, struct net_device *netdev,
			     struct cfg80211_pmksa *pmksa);
	int	(*flush_pmksa)(struct wiphy *wiphy, struct net_device *netdev);

	int	(*remain_on_channel)(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     struct ieee80211_channel *chan,
				     unsigned int duration,
				     u64 *cookie);
	int	(*cancel_remain_on_channel)(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    u64 cookie);

	int	(*mgmt_tx)(struct wiphy *wiphy, struct wireless_dev *wdev,
			   struct cfg80211_mgmt_tx_params *params,
			   u64 *cookie);
	int	(*mgmt_tx_cancel_wait)(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       u64 cookie);

	int	(*set_power_mgmt)(struct wiphy *wiphy, struct net_device *dev,
				  bool enabled, int timeout);

	int	(*set_cqm_rssi_config)(struct wiphy *wiphy,
				       struct net_device *dev,
				       s32 rssi_thold, u32 rssi_hyst);

	int	(*set_cqm_rssi_range_config)(struct wiphy *wiphy,
					     struct net_device *dev,
					     s32 rssi_low, s32 rssi_high);

	int	(*set_cqm_txe_config)(struct wiphy *wiphy,
				      struct net_device *dev,
				      u32 rate, u32 pkts, u32 intvl);

	void	(*update_mgmt_frame_registrations)(struct wiphy *wiphy,
						   struct wireless_dev *wdev,
						   struct mgmt_frame_regs *upd);

	int	(*set_antenna)(struct wiphy *wiphy, u32 tx_ant, u32 rx_ant);
	int	(*get_antenna)(struct wiphy *wiphy, u32 *tx_ant, u32 *rx_ant);

	int	(*sched_scan_start)(struct wiphy *wiphy,
				struct net_device *dev,
				struct cfg80211_sched_scan_request *request);
	int	(*sched_scan_stop)(struct wiphy *wiphy, struct net_device *dev,
				   u64 reqid);

	int	(*set_rekey_data)(struct wiphy *wiphy, struct net_device *dev,
				  struct cfg80211_gtk_rekey_data *data);

	int	(*tdls_mgmt)(struct wiphy *wiphy, struct net_device *dev,
			     const u8 *peer, u8 action_code,  u8 dialog_token,
			     u16 status_code, u32 peer_capability,
			     bool initiator, const u8 *buf, size_t len);
	int	(*tdls_oper)(struct wiphy *wiphy, struct net_device *dev,
			     const u8 *peer, enum nl80211_tdls_operation oper);

	int	(*probe_client)(struct wiphy *wiphy, struct net_device *dev,
				const u8 *peer, u64 *cookie);

	int	(*set_noack_map)(struct wiphy *wiphy,
				  struct net_device *dev,
				  u16 noack_map);

	int	(*get_channel)(struct wiphy *wiphy,
			       struct wireless_dev *wdev,
			       struct cfg80211_chan_def *chandef);

	int	(*start_p2p_device)(struct wiphy *wiphy,
				    struct wireless_dev *wdev);
	void	(*stop_p2p_device)(struct wiphy *wiphy,
				   struct wireless_dev *wdev);

	int	(*set_mac_acl)(struct wiphy *wiphy, struct net_device *dev,
			       const struct cfg80211_acl_data *params);

	int	(*start_radar_detection)(struct wiphy *wiphy,
					 struct net_device *dev,
					 struct cfg80211_chan_def *chandef,
					 u32 cac_time_ms);
	void	(*end_cac)(struct wiphy *wiphy,
				struct net_device *dev);
	int	(*update_ft_ies)(struct wiphy *wiphy, struct net_device *dev,
				 struct cfg80211_update_ft_ies_params *ftie);
	int	(*crit_proto_start)(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    enum nl80211_crit_proto_id protocol,
				    u16 duration);
	void	(*crit_proto_stop)(struct wiphy *wiphy,
				   struct wireless_dev *wdev);
	int	(*set_coalesce)(struct wiphy *wiphy,
				struct cfg80211_coalesce *coalesce);

	int	(*channel_switch)(struct wiphy *wiphy,
				  struct net_device *dev,
				  struct cfg80211_csa_settings *params);

	int     (*set_qos_map)(struct wiphy *wiphy,
			       struct net_device *dev,
			       struct cfg80211_qos_map *qos_map);

	int	(*set_ap_chanwidth)(struct wiphy *wiphy, struct net_device *dev,
				    struct cfg80211_chan_def *chandef);

	int	(*add_tx_ts)(struct wiphy *wiphy, struct net_device *dev,
			     u8 tsid, const u8 *peer, u8 user_prio,
			     u16 admitted_time);
	int	(*del_tx_ts)(struct wiphy *wiphy, struct net_device *dev,
			     u8 tsid, const u8 *peer);

	int	(*tdls_channel_switch)(struct wiphy *wiphy,
				       struct net_device *dev,
				       const u8 *addr, u8 oper_class,
				       struct cfg80211_chan_def *chandef);
	void	(*tdls_cancel_channel_switch)(struct wiphy *wiphy,
					      struct net_device *dev,
					      const u8 *addr);
	int	(*start_nan)(struct wiphy *wiphy, struct wireless_dev *wdev,
			     struct cfg80211_nan_conf *conf);
	void	(*stop_nan)(struct wiphy *wiphy, struct wireless_dev *wdev);
	int	(*add_nan_func)(struct wiphy *wiphy, struct wireless_dev *wdev,
				struct cfg80211_nan_func *nan_func);
	void	(*del_nan_func)(struct wiphy *wiphy, struct wireless_dev *wdev,
			       u64 cookie);
	int	(*nan_change_conf)(struct wiphy *wiphy,
				   struct wireless_dev *wdev,
				   struct cfg80211_nan_conf *conf,
				   u32 changes);

	int	(*set_multicast_to_unicast)(struct wiphy *wiphy,
					    struct net_device *dev,
					    const bool enabled);

	int	(*get_txq_stats)(struct wiphy *wiphy,
				 struct wireless_dev *wdev,
				 struct cfg80211_txq_stats *txqstats);

	int	(*set_pmk)(struct wiphy *wiphy, struct net_device *dev,
			   const struct cfg80211_pmk_conf *conf);
	int	(*del_pmk)(struct wiphy *wiphy, struct net_device *dev,
			   const u8 *aa);
	int     (*external_auth)(struct wiphy *wiphy, struct net_device *dev,
				 struct cfg80211_external_auth_params *params);

	int	(*tx_control_port)(struct wiphy *wiphy,
				   struct net_device *dev,
				   const u8 *buf, size_t len,
				   const u8 *dest, const __be16 proto,
				   const bool noencrypt,
				   u64 *cookie);

	int	(*get_ftm_responder_stats)(struct wiphy *wiphy,
				struct net_device *dev,
				struct cfg80211_ftm_responder_stats *ftm_stats);

	int	(*start_pmsr)(struct wiphy *wiphy, struct wireless_dev *wdev,
			      struct cfg80211_pmsr_request *request);
	void	(*abort_pmsr)(struct wiphy *wiphy, struct wireless_dev *wdev,
			      struct cfg80211_pmsr_request *request);
	int	(*update_owe_info)(struct wiphy *wiphy, struct net_device *dev,
				   struct cfg80211_update_owe_info *owe_info);
	int	(*probe_mesh_link)(struct wiphy *wiphy, struct net_device *dev,
				   const u8 *buf, size_t len);
	int     (*set_tid_config)(struct wiphy *wiphy, struct net_device *dev,
				  struct cfg80211_tid_config *tid_conf);
	int	(*reset_tid_config)(struct wiphy *wiphy, struct net_device *dev,
				    const u8 *peer, u8 tids);
};

/*
 * wireless hardware and networking interfaces structures
 * and registration/helper functions
 */

/**
 * enum wiphy_flags - wiphy capability flags
 *
 * @WIPHY_FLAG_NETNS_OK: if not set, do not allow changing the netns of this
 *	wiphy at all
 * @WIPHY_FLAG_PS_ON_BY_DEFAULT: if set to true, powersave will be enabled
 *	by default -- this flag will be set depending on the kernel's default
 *	on wiphy_new(), but can be changed by the driver if it has a good
 *	reason to override the default
 * @WIPHY_FLAG_4ADDR_AP: supports 4addr mode even on AP (with a single station
 *	on a VLAN interface). This flag also serves an extra purpose of
 *	supporting 4ADDR AP mode on devices which do not support AP/VLAN iftype.
 * @WIPHY_FLAG_4ADDR_STATION: supports 4addr mode even as a station
 * @WIPHY_FLAG_CONTROL_PORT_PROTOCOL: This device supports setting the
 *	control port protocol ethertype. The device also honours the
 *	control_port_no_encrypt flag.
 * @WIPHY_FLAG_IBSS_RSN: The device supports IBSS RSN.
 * @WIPHY_FLAG_MESH_AUTH: The device supports mesh authentication by routing
 *	auth frames to userspace. See @NL80211_MESH_SETUP_USERSPACE_AUTH.
 * @WIPHY_FLAG_SUPPORTS_FW_ROAM: The device supports roaming feature in the
 *	firmware.
 * @WIPHY_FLAG_AP_UAPSD: The device supports uapsd on AP.
 * @WIPHY_FLAG_SUPPORTS_TDLS: The device supports TDLS (802.11z) operation.
 * @WIPHY_FLAG_TDLS_EXTERNAL_SETUP: The device does not handle TDLS (802.11z)
 *	link setup/discovery operations internally. Setup, discovery and
 *	teardown packets should be sent through the @NL80211_CMD_TDLS_MGMT
 *	command. When this flag is not set, @NL80211_CMD_TDLS_OPER should be
 *	used for asking the driver/firmware to perform a TDLS operation.
 * @WIPHY_FLAG_HAVE_AP_SME: device integrates AP SME
 * @WIPHY_FLAG_REPORTS_OBSS: the device will report beacons from other BSSes
 *	when there are virtual interfaces in AP mode by calling
 *	cfg80211_report_obss_beacon().
 * @WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD: When operating as an AP, the device
 *	responds to probe-requests in hardware.
 * @WIPHY_FLAG_OFFCHAN_TX: Device supports direct off-channel TX.
 * @WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL: Device supports remain-on-channel call.
 * @WIPHY_FLAG_SUPPORTS_5_10_MHZ: Device supports 5 MHz and 10 MHz channels.
 * @WIPHY_FLAG_HAS_CHANNEL_SWITCH: Device supports channel switch in
 *	beaconing mode (AP, IBSS, Mesh, ...).
 * @WIPHY_FLAG_HAS_STATIC_WEP: The device supports static WEP key installation
 *	before connection.
 * @WIPHY_FLAG_SUPPORTS_EXT_KEK_KCK: The device supports bigger kek and kck keys
 */
enum wiphy_flags {
	WIPHY_FLAG_SUPPORTS_EXT_KEK_KCK		= BIT(0),
	/* use hole at 1 */
	/* use hole at 2 */
	WIPHY_FLAG_NETNS_OK			= BIT(3),
	WIPHY_FLAG_PS_ON_BY_DEFAULT		= BIT(4),
	WIPHY_FLAG_4ADDR_AP			= BIT(5),
	WIPHY_FLAG_4ADDR_STATION		= BIT(6),
	WIPHY_FLAG_CONTROL_PORT_PROTOCOL	= BIT(7),
	WIPHY_FLAG_IBSS_RSN			= BIT(8),
	WIPHY_FLAG_MESH_AUTH			= BIT(10),
	/* use hole at 11 */
	/* use hole at 12 */
	WIPHY_FLAG_SUPPORTS_FW_ROAM		= BIT(13),
	WIPHY_FLAG_AP_UAPSD			= BIT(14),
	WIPHY_FLAG_SUPPORTS_TDLS		= BIT(15),
	WIPHY_FLAG_TDLS_EXTERNAL_SETUP		= BIT(16),
	WIPHY_FLAG_HAVE_AP_SME			= BIT(17),
	WIPHY_FLAG_REPORTS_OBSS			= BIT(18),
	WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD	= BIT(19),
	WIPHY_FLAG_OFFCHAN_TX			= BIT(20),
	WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL	= BIT(21),
	WIPHY_FLAG_SUPPORTS_5_10_MHZ		= BIT(22),
	WIPHY_FLAG_HAS_CHANNEL_SWITCH		= BIT(23),
	WIPHY_FLAG_HAS_STATIC_WEP		= BIT(24),
};

/**
 * struct ieee80211_iface_limit - limit on certain interface types
 * @max: maximum number of interfaces of these types
 * @types: interface types (bits)
 */
struct ieee80211_iface_limit {
	u16 max;
	u16 types;
};

/**
 * struct ieee80211_iface_combination - possible interface combination
 *
 * With this structure the driver can describe which interface
 * combinations it supports concurrently.
 *
 * Examples:
 *
 * 1. Allow #STA <= 1, #AP <= 1, matching BI, channels = 1, 2 total:
 *
 *    .. code-block:: c
 *
 *	struct ieee80211_iface_limit limits1[] = {
 *		{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION), },
 *		{ .max = 1, .types = BIT(NL80211_IFTYPE_AP}, },
 *	};
 *	struct ieee80211_iface_combination combination1 = {
 *		.limits = limits1,
 *		.n_limits = ARRAY_SIZE(limits1),
 *		.max_interfaces = 2,
 *		.beacon_int_infra_match = true,
 *	};
 *
 *
 * 2. Allow #{AP, P2P-GO} <= 8, channels = 1, 8 total:
 *
 *    .. code-block:: c
 *
 *	struct ieee80211_iface_limit limits2[] = {
 *		{ .max = 8, .types = BIT(NL80211_IFTYPE_AP) |
 *				     BIT(NL80211_IFTYPE_P2P_GO), },
 *	};
 *	struct ieee80211_iface_combination combination2 = {
 *		.limits = limits2,
 *		.n_limits = ARRAY_SIZE(limits2),
 *		.max_interfaces = 8,
 *		.num_different_channels = 1,
 *	};
 *
 *
 * 3. Allow #STA <= 1, #{P2P-client,P2P-GO} <= 3 on two channels, 4 total.
 *
 *    This allows for an infrastructure connection and three P2P connections.
 *
 *    .. code-block:: c
 *
 *	struct ieee80211_iface_limit limits3[] = {
 *		{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION), },
 *		{ .max = 3, .types = BIT(NL80211_IFTYPE_P2P_GO) |
 *				     BIT(NL80211_IFTYPE_P2P_CLIENT), },
 *	};
 *	struct ieee80211_iface_combination combination3 = {
 *		.limits = limits3,
 *		.n_limits = ARRAY_SIZE(limits3),
 *		.max_interfaces = 4,
 *		.num_different_channels = 2,
 *	};
 *
 */
struct ieee80211_iface_combination {
	/**
	 * @limits:
	 * limits for the given interface types
	 */
	const struct ieee80211_iface_limit *limits;

	/**
	 * @num_different_channels:
	 * can use up to this many different channels
	 */
	u32 num_different_channels;

	/**
	 * @max_interfaces:
	 * maximum number of interfaces in total allowed in this group
	 */
	u16 max_interfaces;

	/**
	 * @n_limits:
	 * number of limitations
	 */
	u8 n_limits;

	/**
	 * @beacon_int_infra_match:
	 * In this combination, the beacon intervals between infrastructure
	 * and AP types must match. This is required only in special cases.
	 */
	bool beacon_int_infra_match;

	/**
	 * @radar_detect_widths:
	 * bitmap of channel widths supported for radar detection
	 */
	u8 radar_detect_widths;

	/**
	 * @radar_detect_regions:
	 * bitmap of regions supported for radar detection
	 */
	u8 radar_detect_regions;

	/**
	 * @beacon_int_min_gcd:
	 * This interface combination supports different beacon intervals.
	 *
	 * = 0
	 *   all beacon intervals for different interface must be same.
	 * > 0
	 *   any beacon interval for the interface part of this combination AND
	 *   GCD of all beacon intervals from beaconing interfaces of this
	 *   combination must be greater or equal to this value.
	 */
	u32 beacon_int_min_gcd;
};

struct ieee80211_txrx_stypes {
	u16 tx, rx;
};

/**
 * enum wiphy_wowlan_support_flags - WoWLAN support flags
 * @WIPHY_WOWLAN_ANY: supports wakeup for the special "any"
 *	trigger that keeps the device operating as-is and
 *	wakes up the host on any activity, for example a
 *	received packet that passed filtering; note that the
 *	packet should be preserved in that case
 * @WIPHY_WOWLAN_MAGIC_PKT: supports wakeup on magic packet
 *	(see nl80211.h)
 * @WIPHY_WOWLAN_DISCONNECT: supports wakeup on disconnect
 * @WIPHY_WOWLAN_SUPPORTS_GTK_REKEY: supports GTK rekeying while asleep
 * @WIPHY_WOWLAN_GTK_REKEY_FAILURE: supports wakeup on GTK rekey failure
 * @WIPHY_WOWLAN_EAP_IDENTITY_REQ: supports wakeup on EAP identity request
 * @WIPHY_WOWLAN_4WAY_HANDSHAKE: supports wakeup on 4-way handshake failure
 * @WIPHY_WOWLAN_RFKILL_RELEASE: supports wakeup on RF-kill release
 * @WIPHY_WOWLAN_NET_DETECT: supports wakeup on network detection
 */
enum wiphy_wowlan_support_flags {
	WIPHY_WOWLAN_ANY		= BIT(0),
	WIPHY_WOWLAN_MAGIC_PKT		= BIT(1),
	WIPHY_WOWLAN_DISCONNECT		= BIT(2),
	WIPHY_WOWLAN_SUPPORTS_GTK_REKEY	= BIT(3),
	WIPHY_WOWLAN_GTK_REKEY_FAILURE	= BIT(4),
	WIPHY_WOWLAN_EAP_IDENTITY_REQ	= BIT(5),
	WIPHY_WOWLAN_4WAY_HANDSHAKE	= BIT(6),
	WIPHY_WOWLAN_RFKILL_RELEASE	= BIT(7),
	WIPHY_WOWLAN_NET_DETECT		= BIT(8),
};

struct wiphy_wowlan_tcp_support {
	const struct nl80211_wowlan_tcp_data_token_feature *tok;
	u32 data_payload_max;
	u32 data_interval_max;
	u32 wake_payload_max;
	bool seq;
};

/**
 * struct wiphy_wowlan_support - WoWLAN support data
 * @flags: see &enum wiphy_wowlan_support_flags
 * @n_patterns: number of supported wakeup patterns
 *	(see nl80211.h for the pattern definition)
 * @pattern_max_len: maximum length of each pattern
 * @pattern_min_len: minimum length of each pattern
 * @max_pkt_offset: maximum Rx packet offset
 * @max_nd_match_sets: maximum number of matchsets for net-detect,
 *	similar, but not necessarily identical, to max_match_sets for
 *	scheduled scans.
 *	See &struct cfg80211_sched_scan_request.@match_sets for more
 *	details.
 * @tcp: TCP wakeup support information
 */
struct wiphy_wowlan_support {
	u32 flags;
	int n_patterns;
	int pattern_max_len;
	int pattern_min_len;
	int max_pkt_offset;
	int max_nd_match_sets;
	const struct wiphy_wowlan_tcp_support *tcp;
};

/**
 * struct wiphy_coalesce_support - coalesce support data
 * @n_rules: maximum number of coalesce rules
 * @max_delay: maximum supported coalescing delay in msecs
 * @n_patterns: number of supported patterns in a rule
 *	(see nl80211.h for the pattern definition)
 * @pattern_max_len: maximum length of each pattern
 * @pattern_min_len: minimum length of each pattern
 * @max_pkt_offset: maximum Rx packet offset
 */
struct wiphy_coalesce_support {
	int n_rules;
	int max_delay;
	int n_patterns;
	int pattern_max_len;
	int pattern_min_len;
	int max_pkt_offset;
};

/**
 * enum wiphy_vendor_command_flags - validation flags for vendor commands
 * @WIPHY_VENDOR_CMD_NEED_WDEV: vendor command requires wdev
 * @WIPHY_VENDOR_CMD_NEED_NETDEV: vendor command requires netdev
 * @WIPHY_VENDOR_CMD_NEED_RUNNING: interface/wdev must be up & running
 *	(must be combined with %_WDEV or %_NETDEV)
 */
enum wiphy_vendor_command_flags {
	WIPHY_VENDOR_CMD_NEED_WDEV = BIT(0),
	WIPHY_VENDOR_CMD_NEED_NETDEV = BIT(1),
	WIPHY_VENDOR_CMD_NEED_RUNNING = BIT(2),
};

/**
 * enum wiphy_opmode_flag - Station's ht/vht operation mode information flags
 *
 * @STA_OPMODE_MAX_BW_CHANGED: Max Bandwidth changed
 * @STA_OPMODE_SMPS_MODE_CHANGED: SMPS mode changed
 * @STA_OPMODE_N_SS_CHANGED: max N_SS (number of spatial streams) changed
 *
 */
enum wiphy_opmode_flag {
	STA_OPMODE_MAX_BW_CHANGED	= BIT(0),
	STA_OPMODE_SMPS_MODE_CHANGED	= BIT(1),
	STA_OPMODE_N_SS_CHANGED		= BIT(2),
};

/**
 * struct sta_opmode_info - Station's ht/vht operation mode information
 * @changed: contains value from &enum wiphy_opmode_flag
 * @smps_mode: New SMPS mode value from &enum nl80211_smps_mode of a station
 * @bw: new max bandwidth value from &enum nl80211_chan_width of a station
 * @rx_nss: new rx_nss value of a station
 */

struct sta_opmode_info {
	u32 changed;
	enum nl80211_smps_mode smps_mode;
	enum nl80211_chan_width bw;
	u8 rx_nss;
};

#define VENDOR_CMD_RAW_DATA ((const struct nla_policy *)(long)(-ENODATA))

/**
 * struct wiphy_vendor_command - vendor command definition
 * @info: vendor command identifying information, as used in nl80211
 * @flags: flags, see &enum wiphy_vendor_command_flags
 * @doit: callback for the operation, note that wdev is %NULL if the
 *	flags didn't ask for a wdev and non-%NULL otherwise; the data
 *	pointer may be %NULL if userspace provided no data at all
 * @dumpit: dump callback, for transferring bigger/multiple items. The
 *	@storage points to cb->args[5], ie. is preserved over the multiple
 *	dumpit calls.
 * @policy: policy pointer for attributes within %NL80211_ATTR_VENDOR_DATA.
 *	Set this to %VENDOR_CMD_RAW_DATA if no policy can be given and the
 *	attribute is just raw data (e.g. a firmware command).
 * @maxattr: highest attribute number in policy
 * It's recommended to not have the same sub command with both @doit and
 * @dumpit, so that userspace can assume certain ones are get and others
 * are used with dump requests.
 */
struct wiphy_vendor_command {
	struct nl80211_vendor_cmd_info info;
	u32 flags;
	int (*doit)(struct wiphy *wiphy, struct wireless_dev *wdev,
		    const void *data, int data_len);
	int (*dumpit)(struct wiphy *wiphy, struct wireless_dev *wdev,
		      struct sk_buff *skb, const void *data, int data_len,
		      unsigned long *storage);
	const struct nla_policy *policy;
	unsigned int maxattr;
};

/**
 * struct wiphy_iftype_ext_capab - extended capabilities per interface type
 * @iftype: interface type
 * @extended_capabilities: extended capabilities supported by the driver,
 *	additional capabilities might be supported by userspace; these are the
 *	802.11 extended capabilities ("Extended Capabilities element") and are
 *	in the same format as in the information element. See IEEE Std
 *	802.11-2012 8.4.2.29 for the defined fields.
 * @extended_capabilities_mask: mask of the valid values
 * @extended_capabilities_len: length of the extended capabilities
 */
struct wiphy_iftype_ext_capab {
	enum nl80211_iftype iftype;
	const u8 *extended_capabilities;
	const u8 *extended_capabilities_mask;
	u8 extended_capabilities_len;
};

/**
 * struct cfg80211_pmsr_capabilities - cfg80211 peer measurement capabilities
 * @max_peers: maximum number of peers in a single measurement
 * @report_ap_tsf: can report assoc AP's TSF for radio resource measurement
 * @randomize_mac_addr: can randomize MAC address for measurement
 * @ftm.supported: FTM measurement is supported
 * @ftm.asap: ASAP-mode is supported
 * @ftm.non_asap: non-ASAP-mode is supported
 * @ftm.request_lci: can request LCI data
 * @ftm.request_civicloc: can request civic location data
 * @ftm.preambles: bitmap of preambles supported (&enum nl80211_preamble)
 * @ftm.bandwidths: bitmap of bandwidths supported (&enum nl80211_chan_width)
 * @ftm.max_bursts_exponent: maximum burst exponent supported
 *	(set to -1 if not limited; note that setting this will necessarily
 *	forbid using the value 15 to let the responder pick)
 * @ftm.max_ftms_per_burst: maximum FTMs per burst supported (set to 0 if
 *	not limited)
 * @ftm.trigger_based: trigger based ranging measurement is supported
 * @ftm.non_trigger_based: non trigger based ranging measurement is supported
 */
struct cfg80211_pmsr_capabilities {
	unsigned int max_peers;
	u8 report_ap_tsf:1,
	   randomize_mac_addr:1;

	struct {
		u32 preambles;
		u32 bandwidths;
		s8 max_bursts_exponent;
		u8 max_ftms_per_burst;
		u8 supported:1,
		   asap:1,
		   non_asap:1,
		   request_lci:1,
		   request_civicloc:1,
		   trigger_based:1,
		   non_trigger_based:1;
	} ftm;
};

/**
 * struct wiphy_iftype_akm_suites - This structure encapsulates supported akm
 * suites for interface types defined in @iftypes_mask. Each type in the
 * @iftypes_mask must be unique across all instances of iftype_akm_suites.
 *
 * @iftypes_mask: bitmask of interfaces types
 * @akm_suites: points to an array of supported akm suites
 * @n_akm_suites: number of supported AKM suites
 */
struct wiphy_iftype_akm_suites {
	u16 iftypes_mask;
	const u32 *akm_suites;
	int n_akm_suites;
};

/**
 * struct wiphy - wireless hardware description
 * @reg_notifier: the driver's regulatory notification callback,
 *	note that if your driver uses wiphy_apply_custom_regulatory()
 *	the reg_notifier's request can be passed as NULL
 * @regd: the driver's regulatory domain, if one was requested via
 *	the regulatory_hint() API. This can be used by the driver
 *	on the reg_notifier() if it chooses to ignore future
 *	regulatory domain changes caused by other drivers.
 * @signal_type: signal type reported in &struct cfg80211_bss.
 * @cipher_suites: supported cipher suites
 * @n_cipher_suites: number of supported cipher suites
 * @akm_suites: supported AKM suites. These are the default AKMs supported if
 *	the supported AKMs not advertized for a specific interface type in
 *	iftype_akm_suites.
 * @n_akm_suites: number of supported AKM suites
 * @iftype_akm_suites: array of supported akm suites info per interface type.
 *	Note that the bits in @iftypes_mask inside this structure cannot
 *	overlap (i.e. only one occurrence of each type is allowed across all
 *	instances of iftype_akm_suites).
 * @num_iftype_akm_suites: number of interface types for which supported akm
 *	suites are specified separately.
 * @retry_short: Retry limit for short frames (dot11ShortRetryLimit)
 * @retry_long: Retry limit for long frames (dot11LongRetryLimit)
 * @frag_threshold: Fragmentation threshold (dot11FragmentationThreshold);
 *	-1 = fragmentation disabled, only odd values >= 256 used
 * @rts_threshold: RTS threshold (dot11RTSThreshold); -1 = RTS/CTS disabled
 * @_net: the network namespace this wiphy currently lives in
 * @perm_addr: permanent MAC address of this device
 * @addr_mask: If the device supports multiple MAC addresses by masking,
 *	set this to a mask with variable bits set to 1, e.g. if the last
 *	four bits are variable then set it to 00-00-00-00-00-0f. The actual
 *	variable bits shall be determined by the interfaces added, with
 *	interfaces not matching the mask being rejected to be brought up.
 * @n_addresses: number of addresses in @addresses.
 * @addresses: If the device has more than one address, set this pointer
 *	to a list of addresses (6 bytes each). The first one will be used
 *	by default for perm_addr. In this case, the mask should be set to
 *	all-zeroes. In this case it is assumed that the device can handle
 *	the same number of arbitrary MAC addresses.
 * @registered: protects ->resume and ->suspend sysfs callbacks against
 *	unregister hardware
 * @debugfsdir: debugfs directory used for this wiphy (ieee80211/<wiphyname>).
 *	It will be renamed automatically on wiphy renames
 * @dev: (virtual) struct device for this wiphy. The item in
 *	/sys/class/ieee80211/ points to this. You need use set_wiphy_dev()
 *	(see below).
 * @wext: wireless extension handlers
 * @priv: driver private data (sized according to wiphy_new() parameter)
 * @interface_modes: bitmask of interfaces types valid for this wiphy,
 *	must be set by driver
 * @iface_combinations: Valid interface combinations array, should not
 *	list single interface types.
 * @n_iface_combinations: number of entries in @iface_combinations array.
 * @software_iftypes: bitmask of software interface types, these are not
 *	subject to any restrictions since they are purely managed in SW.
 * @flags: wiphy flags, see &enum wiphy_flags
 * @regulatory_flags: wiphy regulatory flags, see
 *	&enum ieee80211_regulatory_flags
 * @features: features advertised to nl80211, see &enum nl80211_feature_flags.
 * @ext_features: extended features advertised to nl80211, see
 *	&enum nl80211_ext_feature_index.
 * @bss_priv_size: each BSS struct has private data allocated with it,
 *	this variable determines its size
 * @max_scan_ssids: maximum number of SSIDs the device can scan for in
 *	any given scan
 * @max_sched_scan_reqs: maximum number of scheduled scan requests that
 *	the device can run concurrently.
 * @max_sched_scan_ssids: maximum number of SSIDs the device can scan
 *	for in any given scheduled scan
 * @max_match_sets: maximum number of match sets the device can handle
 *	when performing a scheduled scan, 0 if filtering is not
 *	supported.
 * @max_scan_ie_len: maximum length of user-controlled IEs device can
 *	add to probe request frames transmitted during a scan, must not
 *	include fixed IEs like supported rates
 * @max_sched_scan_ie_len: same as max_scan_ie_len, but for scheduled
 *	scans
 * @max_sched_scan_plans: maximum number of scan plans (scan interval and number
 *	of iterations) for scheduled scan supported by the device.
 * @max_sched_scan_plan_interval: maximum interval (in seconds) for a
 *	single scan plan supported by the device.
 * @max_sched_scan_plan_iterations: maximum number of iterations for a single
 *	scan plan supported by the device.
 * @coverage_class: current coverage class
 * @fw_version: firmware version for ethtool reporting
 * @hw_version: hardware version for ethtool reporting
 * @max_num_pmkids: maximum number of PMKIDs supported by device
 * @privid: a pointer that drivers can use to identify if an arbitrary
 *	wiphy is theirs, e.g. in global notifiers
 * @bands: information about bands/channels supported by this device
 *
 * @mgmt_stypes: bitmasks of frame subtypes that can be subscribed to or
 *	transmitted through nl80211, points to an array indexed by interface
 *	type
 *
 * @available_antennas_tx: bitmap of antennas which are available to be
 *	configured as TX antennas. Antenna configuration commands will be
 *	rejected unless this or @available_antennas_rx is set.
 *
 * @available_antennas_rx: bitmap of antennas which are available to be
 *	configured as RX antennas. Antenna configuration commands will be
 *	rejected unless this or @available_antennas_tx is set.
 *
 * @probe_resp_offload:
 *	 Bitmap of supported protocols for probe response offloading.
 *	 See &enum nl80211_probe_resp_offload_support_attr. Only valid
 *	 when the wiphy flag @WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD is set.
 *
 * @max_remain_on_channel_duration: Maximum time a remain-on-channel operation
 *	may request, if implemented.
 *
 * @wowlan: WoWLAN support information
 * @wowlan_config: current WoWLAN configuration; this should usually not be
 *	used since access to it is necessarily racy, use the parameter passed
 *	to the suspend() operation instead.
 *
 * @ap_sme_capa: AP SME capabilities, flags from &enum nl80211_ap_sme_features.
 * @ht_capa_mod_mask:  Specify what ht_cap values can be over-ridden.
 *	If null, then none can be over-ridden.
 * @vht_capa_mod_mask:  Specify what VHT capabilities can be over-ridden.
 *	If null, then none can be over-ridden.
 *
 * @wdev_list: the list of associated (virtual) interfaces; this list must
 *	not be modified by the driver, but can be read with RTNL/RCU protection.
 *
 * @max_acl_mac_addrs: Maximum number of MAC addresses that the device
 *	supports for ACL.
 *
 * @extended_capabilities: extended capabilities supported by the driver,
 *	additional capabilities might be supported by userspace; these are
 *	the 802.11 extended capabilities ("Extended Capabilities element")
 *	and are in the same format as in the information element. See
 *	802.11-2012 8.4.2.29 for the defined fields. These are the default
 *	extended capabilities to be used if the capabilities are not specified
 *	for a specific interface type in iftype_ext_capab.
 * @extended_capabilities_mask: mask of the valid values
 * @extended_capabilities_len: length of the extended capabilities
 * @iftype_ext_capab: array of extended capabilities per interface type
 * @num_iftype_ext_capab: number of interface types for which extended
 *	capabilities are specified separately.
 * @coalesce: packet coalescing support information
 *
 * @vendor_commands: array of vendor commands supported by the hardware
 * @n_vendor_commands: number of vendor commands
 * @vendor_events: array of vendor events supported by the hardware
 * @n_vendor_events: number of vendor events
 *
 * @max_ap_assoc_sta: maximum number of associated stations supported in AP mode
 *	(including P2P GO) or 0 to indicate no such limit is advertised. The
 *	driver is allowed to advertise a theoretical limit that it can reach in
 *	some cases, but may not always reach.
 *
 * @max_num_csa_counters: Number of supported csa_counters in beacons
 *	and probe responses.  This value should be set if the driver
 *	wishes to limit the number of csa counters. Default (0) means
 *	infinite.
 * @bss_select_support: bitmask indicating the BSS selection criteria supported
 *	by the driver in the .connect() callback. The bit position maps to the
 *	attribute indices defined in &enum nl80211_bss_select_attr.
 *
 * @nan_supported_bands: bands supported by the device in NAN mode, a
 *	bitmap of &enum nl80211_band values.  For instance, for
 *	NL80211_BAND_2GHZ, bit 0 would be set
 *	(i.e. BIT(NL80211_BAND_2GHZ)).
 *
 * @txq_limit: configuration of internal TX queue frame limit
 * @txq_memory_limit: configuration internal TX queue memory limit
 * @txq_quantum: configuration of internal TX queue scheduler quantum
 *
 * @tx_queue_len: allow setting transmit queue len for drivers not using
 *	wake_tx_queue
 *
 * @support_mbssid: can HW support association with nontransmitted AP
 * @support_only_he_mbssid: don't parse MBSSID elements if it is not
 *	HE AP, in order to avoid compatibility issues.
 *	@support_mbssid must be set for this to have any effect.
 *
 * @pmsr_capa: peer measurement capabilities
 *
 * @tid_config_support: describes the per-TID config support that the
 *	device has
 * @tid_config_support.vif: bitmap of attributes (configurations)
 *	supported by the driver for each vif
 * @tid_config_support.peer: bitmap of attributes (configurations)
 *	supported by the driver for each peer
 * @tid_config_support.max_retry: maximum supported retry count for
 *	long/short retry configuration
 *
 * @max_data_retry_count: maximum supported per TID retry count for
 *	configuration through the %NL80211_TID_CONFIG_ATTR_RETRY_SHORT and
 *	%NL80211_TID_CONFIG_ATTR_RETRY_LONG attributes
 */
struct wiphy {
	/* assign these fields before you register the wiphy */

	u8 perm_addr[ETH_ALEN];
	u8 addr_mask[ETH_ALEN];

	struct mac_address *addresses;

	const struct ieee80211_txrx_stypes *mgmt_stypes;

	const struct ieee80211_iface_combination *iface_combinations;
	int n_iface_combinations;
	u16 software_iftypes;

	u16 n_addresses;

	/* Supported interface modes, OR together BIT(NL80211_IFTYPE_...) */
	u16 interface_modes;

	u16 max_acl_mac_addrs;

	u32 flags, regulatory_flags, features;
	u8 ext_features[DIV_ROUND_UP(NUM_NL80211_EXT_FEATURES, 8)];

	u32 ap_sme_capa;

	enum cfg80211_signal_type signal_type;

	int bss_priv_size;
	u8 max_scan_ssids;
	u8 max_sched_scan_reqs;
	u8 max_sched_scan_ssids;
	u8 max_match_sets;
	u16 max_scan_ie_len;
	u16 max_sched_scan_ie_len;
	u32 max_sched_scan_plans;
	u32 max_sched_scan_plan_interval;
	u32 max_sched_scan_plan_iterations;

	int n_cipher_suites;
	const u32 *cipher_suites;

	int n_akm_suites;
	const u32 *akm_suites;

	const struct wiphy_iftype_akm_suites *iftype_akm_suites;
	unsigned int num_iftype_akm_suites;

	u8 retry_short;
	u8 retry_long;
	u32 frag_threshold;
	u32 rts_threshold;
	u8 coverage_class;

	char fw_version[ETHTOOL_FWVERS_LEN];
	u32 hw_version;

#ifdef CONFIG_PM
	const struct wiphy_wowlan_support *wowlan;
	struct cfg80211_wowlan *wowlan_config;
#endif

	u16 max_remain_on_channel_duration;

	u8 max_num_pmkids;

	u32 available_antennas_tx;
	u32 available_antennas_rx;

	u32 probe_resp_offload;

	const u8 *extended_capabilities, *extended_capabilities_mask;
	u8 extended_capabilities_len;

	const struct wiphy_iftype_ext_capab *iftype_ext_capab;
	unsigned int num_iftype_ext_capab;

	const void *privid;

	struct ieee80211_supported_band *bands[NUM_NL80211_BANDS];

	void (*reg_notifier)(struct wiphy *wiphy,
			     struct regulatory_request *request);

	/* fields below are read-only, assigned by cfg80211 */

	const struct ieee80211_regdomain __rcu *regd;

	struct device dev;

	bool registered;

	struct dentry *debugfsdir;

	const struct ieee80211_ht_cap *ht_capa_mod_mask;
	const struct ieee80211_vht_cap *vht_capa_mod_mask;

	struct list_head wdev_list;

	possible_net_t _net;

#ifdef CONFIG_CFG80211_WEXT
	const struct iw_handler_def *wext;
#endif

	const struct wiphy_coalesce_support *coalesce;

	const struct wiphy_vendor_command *vendor_commands;
	const struct nl80211_vendor_cmd_info *vendor_events;
	int n_vendor_commands, n_vendor_events;

	u16 max_ap_assoc_sta;

	u8 max_num_csa_counters;

	u32 bss_select_support;

	u8 nan_supported_bands;

	u32 txq_limit;
	u32 txq_memory_limit;
	u32 txq_quantum;

	unsigned long tx_queue_len;

	u8 support_mbssid:1,
	   support_only_he_mbssid:1;

	const struct cfg80211_pmsr_capabilities *pmsr_capa;

	struct {
		u64 peer, vif;
		u8 max_retry;
	} tid_config_support;

	u8 max_data_retry_count;

	char priv[] __aligned(NETDEV_ALIGN);
};

static inline struct net *wiphy_net(struct wiphy *wiphy)
{
	return read_pnet(&wiphy->_net);
}

static inline void wiphy_net_set(struct wiphy *wiphy, struct net *net)
{
	write_pnet(&wiphy->_net, net);
}

/**
 * wiphy_priv - return priv from wiphy
 *
 * @wiphy: the wiphy whose priv pointer to return
 * Return: The priv of @wiphy.
 */
static inline void *wiphy_priv(struct wiphy *wiphy)
{
	BUG_ON(!wiphy);
	return &wiphy->priv;
}

/**
 * priv_to_wiphy - return the wiphy containing the priv
 *
 * @priv: a pointer previously returned by wiphy_priv
 * Return: The wiphy of @priv.
 */
static inline struct wiphy *priv_to_wiphy(void *priv)
{
	BUG_ON(!priv);
	return container_of(priv, struct wiphy, priv);
}

/**
 * set_wiphy_dev - set device pointer for wiphy
 *
 * @wiphy: The wiphy whose device to bind
 * @dev: The device to parent it to
 */
static inline void set_wiphy_dev(struct wiphy *wiphy, struct device *dev)
{
	wiphy->dev.parent = dev;
}

/**
 * wiphy_dev - get wiphy dev pointer
 *
 * @wiphy: The wiphy whose device struct to look up
 * Return: The dev of @wiphy.
 */
static inline struct device *wiphy_dev(struct wiphy *wiphy)
{
	return wiphy->dev.parent;
}

/**
 * wiphy_name - get wiphy name
 *
 * @wiphy: The wiphy whose name to return
 * Return: The name of @wiphy.
 */
static inline const char *wiphy_name(const struct wiphy *wiphy)
{
	return dev_name(&wiphy->dev);
}

/**
 * wiphy_new_nm - create a new wiphy for use with cfg80211
 *
 * @ops: The configuration operations for this device
 * @sizeof_priv: The size of the private area to allocate
 * @requested_name: Request a particular name.
 *	NULL is valid value, and means use the default phy%d naming.
 *
 * Create a new wiphy and associate the given operations with it.
 * @sizeof_priv bytes are allocated for private use.
 *
 * Return: A pointer to the new wiphy. This pointer must be
 * assigned to each netdev's ieee80211_ptr for proper operation.
 */
struct wiphy *wiphy_new_nm(const struct cfg80211_ops *ops, int sizeof_priv,
			   const char *requested_name);

/**
 * wiphy_new - create a new wiphy for use with cfg80211
 *
 * @ops: The configuration operations for this device
 * @sizeof_priv: The size of the private area to allocate
 *
 * Create a new wiphy and associate the given operations with it.
 * @sizeof_priv bytes are allocated for private use.
 *
 * Return: A pointer to the new wiphy. This pointer must be
 * assigned to each netdev's ieee80211_ptr for proper operation.
 */
static inline struct wiphy *wiphy_new(const struct cfg80211_ops *ops,
				      int sizeof_priv)
{
	return wiphy_new_nm(ops, sizeof_priv, NULL);
}

/**
 * wiphy_register - register a wiphy with cfg80211
 *
 * @wiphy: The wiphy to register.
 *
 * Return: A non-negative wiphy index or a negative error code.
 */
int wiphy_register(struct wiphy *wiphy);

/**
 * wiphy_unregister - deregister a wiphy from cfg80211
 *
 * @wiphy: The wiphy to unregister.
 *
 * After this call, no more requests can be made with this priv
 * pointer, but the call may sleep to wait for an outstanding
 * request that is being handled.
 */
void wiphy_unregister(struct wiphy *wiphy);

/**
 * wiphy_free - free wiphy
 *
 * @wiphy: The wiphy to free
 */
void wiphy_free(struct wiphy *wiphy);

/* internal structs */
struct cfg80211_conn;
struct cfg80211_internal_bss;
struct cfg80211_cached_keys;
struct cfg80211_cqm_config;

/**
 * struct wireless_dev - wireless device state
 *
 * For netdevs, this structure must be allocated by the driver
 * that uses the ieee80211_ptr field in struct net_device (this
 * is intentional so it can be allocated along with the netdev.)
 * It need not be registered then as netdev registration will
 * be intercepted by cfg80211 to see the new wireless device.
 *
 * For non-netdev uses, it must also be allocated by the driver
 * in response to the cfg80211 callbacks that require it, as
 * there's no netdev registration in that case it may not be
 * allocated outside of callback operations that return it.
 *
 * @wiphy: pointer to hardware description
 * @iftype: interface type
 * @list: (private) Used to collect the interfaces
 * @netdev: (private) Used to reference back to the netdev, may be %NULL
 * @identifier: (private) Identifier used in nl80211 to identify this
 *	wireless device if it has no netdev
 * @current_bss: (private) Used by the internal configuration code
 * @chandef: (private) Used by the internal configuration code to track
 *	the user-set channel definition.
 * @preset_chandef: (private) Used by the internal configuration code to
 *	track the channel to be used for AP later
 * @bssid: (private) Used by the internal configuration code
 * @ssid: (private) Used by the internal configuration code
 * @ssid_len: (private) Used by the internal configuration code
 * @mesh_id_len: (private) Used by the internal configuration code
 * @mesh_id_up_len: (private) Used by the internal configuration code
 * @wext: (private) Used by the internal wireless extensions compat code
 * @wext.ibss: (private) IBSS data part of wext handling
 * @wext.connect: (private) connection handling data
 * @wext.keys: (private) (WEP) key data
 * @wext.ie: (private) extra elements for association
 * @wext.ie_len: (private) length of extra elements
 * @wext.bssid: (private) selected network BSSID
 * @wext.ssid: (private) selected network SSID
 * @wext.default_key: (private) selected default key index
 * @wext.default_mgmt_key: (private) selected default management key index
 * @wext.prev_bssid: (private) previous BSSID for reassociation
 * @wext.prev_bssid_valid: (private) previous BSSID validity
 * @use_4addr: indicates 4addr mode is used on this interface, must be
 *	set by driver (if supported) on add_interface BEFORE registering the
 *	netdev and may otherwise be used by driver read-only, will be update
 *	by cfg80211 on change_interface
 * @mgmt_registrations: list of registrations for management frames
 * @mgmt_registrations_lock: lock for the list
 * @mgmt_registrations_need_update: mgmt registrations were updated,
 *	need to propagate the update to the driver
 * @mtx: mutex used to lock data in this struct, may be used by drivers
 *	and some API functions require it held
 * @beacon_interval: beacon interval used on this device for transmitting
 *	beacons, 0 when not valid
 * @address: The address for this device, valid only if @netdev is %NULL
 * @is_running: true if this is a non-netdev device that has been started, e.g.
 *	the P2P Device.
 * @cac_started: true if DFS channel availability check has been started
 * @cac_start_time: timestamp (jiffies) when the dfs state was entered.
 * @cac_time_ms: CAC time in ms
 * @ps: powersave mode is enabled
 * @ps_timeout: dynamic powersave timeout
 * @ap_unexpected_nlportid: (private) netlink port ID of application
 *	registered for unexpected class 3 frames (AP mode)
 * @conn: (private) cfg80211 software SME connection state machine data
 * @connect_keys: (private) keys to set after connection is established
 * @conn_bss_type: connecting/connected BSS type
 * @conn_owner_nlportid: (private) connection owner socket port ID
 * @disconnect_wk: (private) auto-disconnect work
 * @disconnect_bssid: (private) the BSSID to use for auto-disconnect
 * @ibss_fixed: (private) IBSS is using fixed BSSID
 * @ibss_dfs_possible: (private) IBSS may change to a DFS channel
 * @event_list: (private) list for internal event processing
 * @event_lock: (private) lock for event list
 * @owner_nlportid: (private) owner socket port ID
 * @nl_owner_dead: (private) owner socket went away
 * @cqm_config: (private) nl80211 RSSI monitor state
 * @pmsr_list: (private) peer measurement requests
 * @pmsr_lock: (private) peer measurements requests/results lock
 * @pmsr_free_wk: (private) peer measurements cleanup work
 * @unprot_beacon_reported: (private) timestamp of last
 *	unprotected beacon report
 */
struct wireless_dev {
	struct wiphy *wiphy;
	enum nl80211_iftype iftype;

	/* the remainder of this struct should be private to cfg80211 */
	struct list_head list;
	struct net_device *netdev;

	u32 identifier;

	struct list_head mgmt_registrations;
	spinlock_t mgmt_registrations_lock;
	u8 mgmt_registrations_need_update:1;

	struct mutex mtx;

	bool use_4addr, is_running;

	u8 address[ETH_ALEN] __aligned(sizeof(u16));

	/* currently used for IBSS and SME - might be rearranged later */
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len, mesh_id_len, mesh_id_up_len;
	struct cfg80211_conn *conn;
	struct cfg80211_cached_keys *connect_keys;
	enum ieee80211_bss_type conn_bss_type;
	u32 conn_owner_nlportid;

	struct work_struct disconnect_wk;
	u8 disconnect_bssid[ETH_ALEN];

	struct list_head event_list;
	spinlock_t event_lock;

	struct cfg80211_internal_bss *current_bss; /* associated / joined */
	struct cfg80211_chan_def preset_chandef;
	struct cfg80211_chan_def chandef;

	bool ibss_fixed;
	bool ibss_dfs_possible;

	bool ps;
	int ps_timeout;

	int beacon_interval;

	u32 ap_unexpected_nlportid;

	u32 owner_nlportid;
	bool nl_owner_dead;

	bool cac_started;
	unsigned long cac_start_time;
	unsigned int cac_time_ms;

#ifdef CONFIG_CFG80211_WEXT
	/* wext data */
	struct {
		struct cfg80211_ibss_params ibss;
		struct cfg80211_connect_params connect;
		struct cfg80211_cached_keys *keys;
		const u8 *ie;
		size_t ie_len;
		u8 bssid[ETH_ALEN];
		u8 prev_bssid[ETH_ALEN];
		u8 ssid[IEEE80211_MAX_SSID_LEN];
		s8 default_key, default_mgmt_key;
		bool prev_bssid_valid;
	} wext;
#endif

	struct cfg80211_cqm_config *cqm_config;

	struct list_head pmsr_list;
	spinlock_t pmsr_lock;
	struct work_struct pmsr_free_wk;

	unsigned long unprot_beacon_reported;
};

static inline u8 *wdev_address(struct wireless_dev *wdev)
{
	if (wdev->netdev)
		return wdev->netdev->dev_addr;
	return wdev->address;
}

static inline bool wdev_running(struct wireless_dev *wdev)
{
	if (wdev->netdev)
		return netif_running(wdev->netdev);
	return wdev->is_running;
}

/**
 * wdev_priv - return wiphy priv from wireless_dev
 *
 * @wdev: The wireless device whose wiphy's priv pointer to return
 * Return: The wiphy priv of @wdev.
 */
static inline void *wdev_priv(struct wireless_dev *wdev)
{
	BUG_ON(!wdev);
	return wiphy_priv(wdev->wiphy);
}

/**
 * DOC: Utility functions
 *
 * cfg80211 offers a number of utility functions that can be useful.
 */

/**
 * ieee80211_channel_equal - compare two struct ieee80211_channel
 *
 * @a: 1st struct ieee80211_channel
 * @b: 2nd struct ieee80211_channel
 * Return: true if center frequency of @a == @b
 */
static inline bool
ieee80211_channel_equal(struct ieee80211_channel *a,
			struct ieee80211_channel *b)
{
	return (a->center_freq == b->center_freq &&
		a->freq_offset == b->freq_offset);
}

/**
 * ieee80211_channel_to_khz - convert ieee80211_channel to frequency in KHz
 * @chan: struct ieee80211_channel to convert
 * Return: The corresponding frequency (in KHz)
 */
static inline u32
ieee80211_channel_to_khz(const struct ieee80211_channel *chan)
{
	return MHZ_TO_KHZ(chan->center_freq) + chan->freq_offset;
}

/**
 * ieee80211_channel_to_freq_khz - convert channel number to frequency
 * @chan: channel number
 * @band: band, necessary due to channel number overlap
 * Return: The corresponding frequency (in KHz), or 0 if the conversion failed.
 */
u32 ieee80211_channel_to_freq_khz(int chan, enum nl80211_band band);

/**
 * ieee80211_channel_to_frequency - convert channel number to frequency
 * @chan: channel number
 * @band: band, necessary due to channel number overlap
 * Return: The corresponding frequency (in MHz), or 0 if the conversion failed.
 */
static inline int
ieee80211_channel_to_frequency(int chan, enum nl80211_band band)
{
	return KHZ_TO_MHZ(ieee80211_channel_to_freq_khz(chan, band));
}

/**
 * ieee80211_freq_khz_to_channel - convert frequency to channel number
 * @freq: center frequency in KHz
 * Return: The corresponding channel, or 0 if the conversion failed.
 */
int ieee80211_freq_khz_to_channel(u32 freq);

/**
 * ieee80211_frequency_to_channel - convert frequency to channel number
 * @freq: center frequency in MHz
 * Return: The corresponding channel, or 0 if the conversion failed.
 */
static inline int
ieee80211_frequency_to_channel(int freq)
{
	return ieee80211_freq_khz_to_channel(MHZ_TO_KHZ(freq));
}

/**
 * ieee80211_get_channel_khz - get channel struct from wiphy for specified
 * frequency
 * @wiphy: the struct wiphy to get the channel for
 * @freq: the center frequency (in KHz) of the channel
 * Return: The channel struct from @wiphy at @freq.
 */
struct ieee80211_channel *
ieee80211_get_channel_khz(struct wiphy *wiphy, u32 freq);

/**
 * ieee80211_get_channel - get channel struct from wiphy for specified frequency
 *
 * @wiphy: the struct wiphy to get the channel for
 * @freq: the center frequency (in MHz) of the channel
 * Return: The channel struct from @wiphy at @freq.
 */
static inline struct ieee80211_channel *
ieee80211_get_channel(struct wiphy *wiphy, int freq)
{
	return ieee80211_get_channel_khz(wiphy, MHZ_TO_KHZ(freq));
}

/**
 * cfg80211_channel_is_psc - Check if the channel is a 6 GHz PSC
 * @chan: control channel to check
 *
 * The Preferred Scanning Channels (PSC) are defined in
 * Draft IEEE P802.11ax/D5.0, 26.17.2.3.3
 */
static inline bool cfg80211_channel_is_psc(struct ieee80211_channel *chan)
{
	if (chan->band != NL80211_BAND_6GHZ)
		return false;

	return ieee80211_frequency_to_channel(chan->center_freq) % 16 == 5;
}

/**
 * ieee80211_get_response_rate - get basic rate for a given rate
 *
 * @sband: the band to look for rates in
 * @basic_rates: bitmap of basic rates
 * @bitrate: the bitrate for which to find the basic rate
 *
 * Return: The basic rate corresponding to a given bitrate, that
 * is the next lower bitrate contained in the basic rate map,
 * which is, for this function, given as a bitmap of indices of
 * rates in the band's bitrate table.
 */
struct ieee80211_rate *
ieee80211_get_response_rate(struct ieee80211_supported_band *sband,
			    u32 basic_rates, int bitrate);

/**
 * ieee80211_mandatory_rates - get mandatory rates for a given band
 * @sband: the band to look for rates in
 * @scan_width: width of the control channel
 *
 * This function returns a bitmap of the mandatory rates for the given
 * band, bits are set according to the rate position in the bitrates array.
 */
u32 ieee80211_mandatory_rates(struct ieee80211_supported_band *sband,
			      enum nl80211_bss_scan_width scan_width);

/*
 * Radiotap parsing functions -- for controlled injection support
 *
 * Implemented in net/wireless/radiotap.c
 * Documentation in Documentation/networking/radiotap-headers.rst
 */

struct radiotap_align_size {
	uint8_t align:4, size:4;
};

struct ieee80211_radiotap_namespace {
	const struct radiotap_align_size *align_size;
	int n_bits;
	uint32_t oui;
	uint8_t subns;
};

struct ieee80211_radiotap_vendor_namespaces {
	const struct ieee80211_radiotap_namespace *ns;
	int n_ns;
};

/**
 * struct ieee80211_radiotap_iterator - tracks walk thru present radiotap args
 * @this_arg_index: index of current arg, valid after each successful call
 *	to ieee80211_radiotap_iterator_next()
 * @this_arg: pointer to current radiotap arg; it is valid after each
 *	call to ieee80211_radiotap_iterator_next() but also after
 *	ieee80211_radiotap_iterator_init() where it will point to
 *	the beginning of the actual data portion
 * @this_arg_size: length of the current arg, for convenience
 * @current_namespace: pointer to the current namespace definition
 *	(or internally %NULL if the current namespace is unknown)
 * @is_radiotap_ns: indicates whether the current namespace is the default
 *	radiotap namespace or not
 *
 * @_rtheader: pointer to the radiotap header we are walking through
 * @_max_length: length of radiotap header in cpu byte ordering
 * @_arg_index: next argument index
 * @_arg: next argument pointer
 * @_next_bitmap: internal pointer to next present u32
 * @_bitmap_shifter: internal shifter for curr u32 bitmap, b0 set == arg present
 * @_vns: vendor namespace definitions
 * @_next_ns_data: beginning of the next namespace's data
 * @_reset_on_ext: internal; reset the arg index to 0 when going to the
 *	next bitmap word
 *
 * Describes the radiotap parser state. Fields prefixed with an underscore
 * must not be used by users of the parser, only by the parser internally.
 */

struct ieee80211_radiotap_iterator {
	struct ieee80211_radiotap_header *_rtheader;
	const struct ieee80211_radiotap_vendor_namespaces *_vns;
	const struct ieee80211_radiotap_namespace *current_namespace;

	unsigned char *_arg, *_next_ns_data;
	__le32 *_next_bitmap;

	unsigned char *this_arg;
	int this_arg_index;
	int this_arg_size;

	int is_radiotap_ns;

	int _max_length;
	int _arg_index;
	uint32_t _bitmap_shifter;
	int _reset_on_ext;
};

int
ieee80211_radiotap_iterator_init(struct ieee80211_radiotap_iterator *iterator,
				 struct ieee80211_radiotap_header *radiotap_header,
				 int max_length,
				 const struct ieee80211_radiotap_vendor_namespaces *vns);

int
ieee80211_radiotap_iterator_next(struct ieee80211_radiotap_iterator *iterator);


extern const unsigned char rfc1042_header[6];
extern const unsigned char bridge_tunnel_header[6];

/**
 * ieee80211_get_hdrlen_from_skb - get header length from data
 *
 * @skb: the frame
 *
 * Given an skb with a raw 802.11 header at the data pointer this function
 * returns the 802.11 header length.
 *
 * Return: The 802.11 header length in bytes (not including encryption
 * headers). Or 0 if the data in the sk_buff is too short to contain a valid
 * 802.11 header.
 */
unsigned int ieee80211_get_hdrlen_from_skb(const struct sk_buff *skb);

/**
 * ieee80211_hdrlen - get header length in bytes from frame control
 * @fc: frame control field in little-endian format
 * Return: The header length in bytes.
 */
unsigned int __attribute_const__ ieee80211_hdrlen(__le16 fc);

/**
 * ieee80211_get_mesh_hdrlen - get mesh extension header length
 * @meshhdr: the mesh extension header, only the flags field
 *	(first byte) will be accessed
 * Return: The length of the extension header, which is always at
 * least 6 bytes and at most 18 if address 5 and 6 are present.
 */
unsigned int ieee80211_get_mesh_hdrlen(struct ieee80211s_hdr *meshhdr);

/**
 * DOC: Data path helpers
 *
 * In addition to generic utilities, cfg80211 also offers
 * functions that help implement the data path for devices
 * that do not do the 802.11/802.3 conversion on the device.
 */

/**
 * ieee80211_data_to_8023_exthdr - convert an 802.11 data frame to 802.3
 * @skb: the 802.11 data frame
 * @ehdr: pointer to a &struct ethhdr that will get the header, instead
 *	of it being pushed into the SKB
 * @addr: the device MAC address
 * @iftype: the virtual interface type
 * @data_offset: offset of payload after the 802.11 header
 * Return: 0 on success. Non-zero on error.
 */
int ieee80211_data_to_8023_exthdr(struct sk_buff *skb, struct ethhdr *ehdr,
				  const u8 *addr, enum nl80211_iftype iftype,
				  u8 data_offset);

/**
 * ieee80211_data_to_8023 - convert an 802.11 data frame to 802.3
 * @skb: the 802.11 data frame
 * @addr: the device MAC address
 * @iftype: the virtual interface type
 * Return: 0 on success. Non-zero on error.
 */
static inline int ieee80211_data_to_8023(struct sk_buff *skb, const u8 *addr,
					 enum nl80211_iftype iftype)
{
	return ieee80211_data_to_8023_exthdr(skb, NULL, addr, iftype, 0);
}

/**
 * ieee80211_amsdu_to_8023s - decode an IEEE 802.11n A-MSDU frame
 *
 * Decode an IEEE 802.11 A-MSDU and convert it to a list of 802.3 frames.
 * The @list will be empty if the decode fails. The @skb must be fully
 * header-less before being passed in here; it is freed in this function.
 *
 * @skb: The input A-MSDU frame without any headers.
 * @list: The output list of 802.3 frames. It must be allocated and
 *	initialized by by the caller.
 * @addr: The device MAC address.
 * @iftype: The device interface type.
 * @extra_headroom: The hardware extra headroom for SKBs in the @list.
 * @check_da: DA to check in the inner ethernet header, or NULL
 * @check_sa: SA to check in the inner ethernet header, or NULL
 */
void ieee80211_amsdu_to_8023s(struct sk_buff *skb, struct sk_buff_head *list,
			      const u8 *addr, enum nl80211_iftype iftype,
			      const unsigned int extra_headroom,
			      const u8 *check_da, const u8 *check_sa);

/**
 * cfg80211_classify8021d - determine the 802.1p/1d tag for a data frame
 * @skb: the data frame
 * @qos_map: Interworking QoS mapping or %NULL if not in use
 * Return: The 802.1p/1d tag.
 */
unsigned int cfg80211_classify8021d(struct sk_buff *skb,
				    struct cfg80211_qos_map *qos_map);

/**
 * cfg80211_find_elem_match - match information element and byte array in data
 *
 * @eid: element ID
 * @ies: data consisting of IEs
 * @len: length of data
 * @match: byte array to match
 * @match_len: number of bytes in the match array
 * @match_offset: offset in the IE data where the byte array should match.
 *	Note the difference to cfg80211_find_ie_match() which considers
 *	the offset to start from the element ID byte, but here we take
 *	the data portion instead.
 *
 * Return: %NULL if the element ID could not be found or if
 * the element is invalid (claims to be longer than the given
 * data) or if the byte array doesn't match; otherwise return the
 * requested element struct.
 *
 * Note: There are no checks on the element length other than
 * having to fit into the given data and being large enough for the
 * byte array to match.
 */
const struct element *
cfg80211_find_elem_match(u8 eid, const u8 *ies, unsigned int len,
			 const u8 *match, unsigned int match_len,
			 unsigned int match_offset);

/**
 * cfg80211_find_ie_match - match information element and byte array in data
 *
 * @eid: element ID
 * @ies: data consisting of IEs
 * @len: length of data
 * @match: byte array to match
 * @match_len: number of bytes in the match array
 * @match_offset: offset in the IE where the byte array should match.
 *	If match_len is zero, this must also be set to zero.
 *	Otherwise this must be set to 2 or more, because the first
 *	byte is the element id, which is already compared to eid, and
 *	the second byte is the IE length.
 *
 * Return: %NULL if the element ID could not be found or if
 * the element is invalid (claims to be longer than the given
 * data) or if the byte array doesn't match, or a pointer to the first
 * byte of the requested element, that is the byte containing the
 * element ID.
 *
 * Note: There are no checks on the element length other than
 * having to fit into the given data and being large enough for the
 * byte array to match.
 */
static inline const u8 *
cfg80211_find_ie_match(u8 eid, const u8 *ies, unsigned int len,
		       const u8 *match, unsigned int match_len,
		       unsigned int match_offset)
{
	/* match_offset can't be smaller than 2, unless match_len is
	 * zero, in which case match_offset must be zero as well.
	 */
	if (WARN_ON((match_len && match_offset < 2) ||
		    (!match_len && match_offset)))
		return NULL;

	return (void *)cfg80211_find_elem_match(eid, ies, len,
						match, match_len,
						match_offset ?
							match_offset - 2 : 0);
}

/**
 * cfg80211_find_elem - find information element in data
 *
 * @eid: element ID
 * @ies: data consisting of IEs
 * @len: length of data
 *
 * Return: %NULL if the element ID could not be found or if
 * the element is invalid (claims to be longer than the given
 * data) or if the byte array doesn't match; otherwise return the
 * requested element struct.
 *
 * Note: There are no checks on the element length other than
 * having to fit into the given data.
 */
static inline const struct element *
cfg80211_find_elem(u8 eid, const u8 *ies, int len)
{
	return cfg80211_find_elem_match(eid, ies, len, NULL, 0, 0);
}

/**
 * cfg80211_find_ie - find information element in data
 *
 * @eid: element ID
 * @ies: data consisting of IEs
 * @len: length of data
 *
 * Return: %NULL if the element ID could not be found or if
 * the element is invalid (claims to be longer than the given
 * data), or a pointer to the first byte of the requested
 * element, that is the byte containing the element ID.
 *
 * Note: There are no checks on the element length other than
 * having to fit into the given data.
 */
static inline const u8 *cfg80211_find_ie(u8 eid, const u8 *ies, int len)
{
	return cfg80211_find_ie_match(eid, ies, len, NULL, 0, 0);
}

/**
 * cfg80211_find_ext_elem - find information element with EID Extension in data
 *
 * @ext_eid: element ID Extension
 * @ies: data consisting of IEs
 * @len: length of data
 *
 * Return: %NULL if the etended element could not be found or if
 * the element is invalid (claims to be longer than the given
 * data) or if the byte array doesn't match; otherwise return the
 * requested element struct.
 *
 * Note: There are no checks on the element length other than
 * having to fit into the given data.
 */
static inline const struct element *
cfg80211_find_ext_elem(u8 ext_eid, const u8 *ies, int len)
{
	return cfg80211_find_elem_match(WLAN_EID_EXTENSION, ies, len,
					&ext_eid, 1, 0);
}

/**
 * cfg80211_find_ext_ie - find information element with EID Extension in data
 *
 * @ext_eid: element ID Extension
 * @ies: data consisting of IEs
 * @len: length of data
 *
 * Return: %NULL if the extended element ID could not be found or if
 * the element is invalid (claims to be longer than the given
 * data), or a pointer to the first byte of the requested
 * element, that is the byte containing the element ID.
 *
 * Note: There are no checks on the element length other than
 * having to fit into the given data.
 */
static inline const u8 *cfg80211_find_ext_ie(u8 ext_eid, const u8 *ies, int len)
{
	return cfg80211_find_ie_match(WLAN_EID_EXTENSION, ies, len,
				      &ext_eid, 1, 2);
}

/**
 * cfg80211_find_vendor_elem - find vendor specific information element in data
 *
 * @oui: vendor OUI
 * @oui_type: vendor-specific OUI type (must be < 0xff), negative means any
 * @ies: data consisting of IEs
 * @len: length of data
 *
 * Return: %NULL if the vendor specific element ID could not be found or if the
 * element is invalid (claims to be longer than the given data); otherwise
 * return the element structure for the requested element.
 *
 * Note: There are no checks on the element length other than having to fit into
 * the given data.
 */
const struct element *cfg80211_find_vendor_elem(unsigned int oui, int oui_type,
						const u8 *ies,
						unsigned int len);

/**
 * cfg80211_find_vendor_ie - find vendor specific information element in data
 *
 * @oui: vendor OUI
 * @oui_type: vendor-specific OUI type (must be < 0xff), negative means any
 * @ies: data consisting of IEs
 * @len: length of data
 *
 * Return: %NULL if the vendor specific element ID could not be found or if the
 * element is invalid (claims to be longer than the given data), or a pointer to
 * the first byte of the requested element, that is the byte containing the
 * element ID.
 *
 * Note: There are no checks on the element length other than having to fit into
 * the given data.
 */
static inline const u8 *
cfg80211_find_vendor_ie(unsigned int oui, int oui_type,
			const u8 *ies, unsigned int len)
{
	return (void *)cfg80211_find_vendor_elem(oui, oui_type, ies, len);
}

/**
 * cfg80211_send_layer2_update - send layer 2 update frame
 *
 * @dev: network device
 * @addr: STA MAC address
 *
 * Wireless drivers can use this function to update forwarding tables in bridge
 * devices upon STA association.
 */
void cfg80211_send_layer2_update(struct net_device *dev, const u8 *addr);

/**
 * DOC: Regulatory enforcement infrastructure
 *
 * TODO
 */

/**
 * regulatory_hint - driver hint to the wireless core a regulatory domain
 * @wiphy: the wireless device giving the hint (used only for reporting
 *	conflicts)
 * @alpha2: the ISO/IEC 3166 alpha2 the driver claims its regulatory domain
 *	should be in. If @rd is set this should be NULL. Note that if you
 *	set this to NULL you should still set rd->alpha2 to some accepted
 *	alpha2.
 *
 * Wireless drivers can use this function to hint to the wireless core
 * what it believes should be the current regulatory domain by
 * giving it an ISO/IEC 3166 alpha2 country code it knows its regulatory
 * domain should be in or by providing a completely build regulatory domain.
 * If the driver provides an ISO/IEC 3166 alpha2 userspace will be queried
 * for a regulatory domain structure for the respective country.
 *
 * The wiphy must have been registered to cfg80211 prior to this call.
 * For cfg80211 drivers this means you must first use wiphy_register(),
 * for mac80211 drivers you must first use ieee80211_register_hw().
 *
 * Drivers should check the return value, its possible you can get
 * an -ENOMEM.
 *
 * Return: 0 on success. -ENOMEM.
 */
int regulatory_hint(struct wiphy *wiphy, const char *alpha2);

/**
 * regulatory_set_wiphy_regd - set regdom info for self managed drivers
 * @wiphy: the wireless device we want to process the regulatory domain on
 * @rd: the regulatory domain informatoin to use for this wiphy
 *
 * Set the regulatory domain information for self-managed wiphys, only they
 * may use this function. See %REGULATORY_WIPHY_SELF_MANAGED for more
 * information.
 *
 * Return: 0 on success. -EINVAL, -EPERM
 */
int regulatory_set_wiphy_regd(struct wiphy *wiphy,
			      struct ieee80211_regdomain *rd);

/**
 * regulatory_set_wiphy_regd_sync_rtnl - set regdom for self-managed drivers
 * @wiphy: the wireless device we want to process the regulatory domain on
 * @rd: the regulatory domain information to use for this wiphy
 *
 * This functions requires the RTNL to be held and applies the new regdomain
 * synchronously to this wiphy. For more details see
 * regulatory_set_wiphy_regd().
 *
 * Return: 0 on success. -EINVAL, -EPERM
 */
int regulatory_set_wiphy_regd_sync_rtnl(struct wiphy *wiphy,
					struct ieee80211_regdomain *rd);

/**
 * wiphy_apply_custom_regulatory - apply a custom driver regulatory domain
 * @wiphy: the wireless device we want to process the regulatory domain on
 * @regd: the custom regulatory domain to use for this wiphy
 *
 * Drivers can sometimes have custom regulatory domains which do not apply
 * to a specific country. Drivers can use this to apply such custom regulatory
 * domains. This routine must be called prior to wiphy registration. The
 * custom regulatory domain will be trusted completely and as such previous
 * default channel settings will be disregarded. If no rule is found for a
 * channel on the regulatory domain the channel will be disabled.
 * Drivers using this for a wiphy should also set the wiphy flag
 * REGULATORY_CUSTOM_REG or cfg80211 will set it for the wiphy
 * that called this helper.
 */
void wiphy_apply_custom_regulatory(struct wiphy *wiphy,
				   const struct ieee80211_regdomain *regd);

/**
 * freq_reg_info - get regulatory information for the given frequency
 * @wiphy: the wiphy for which we want to process this rule for
 * @center_freq: Frequency in KHz for which we want regulatory information for
 *
 * Use this function to get the regulatory rule for a specific frequency on
 * a given wireless device. If the device has a specific regulatory domain
 * it wants to follow we respect that unless a country IE has been received
 * and processed already.
 *
 * Return: A valid pointer, or, when an error occurs, for example if no rule
 * can be found, the return value is encoded using ERR_PTR(). Use IS_ERR() to
 * check and PTR_ERR() to obtain the numeric return value. The numeric return
 * value will be -ERANGE if we determine the given center_freq does not even
 * have a regulatory rule for a frequency range in the center_freq's band.
 * See freq_in_rule_band() for our current definition of a band -- this is
 * purely subjective and right now it's 802.11 specific.
 */
const struct ieee80211_reg_rule *freq_reg_info(struct wiphy *wiphy,
					       u32 center_freq);

/**
 * reg_initiator_name - map regulatory request initiator enum to name
 * @initiator: the regulatory request initiator
 *
 * You can use this to map the regulatory request initiator enum to a
 * proper string representation.
 */
const char *reg_initiator_name(enum nl80211_reg_initiator initiator);

/**
 * regulatory_pre_cac_allowed - check if pre-CAC allowed in the current regdom
 * @wiphy: wiphy for which pre-CAC capability is checked.
 *
 * Pre-CAC is allowed only in some regdomains (notable ETSI).
 */
bool regulatory_pre_cac_allowed(struct wiphy *wiphy);

/**
 * DOC: Internal regulatory db functions
 *
 */

/**
 * reg_query_regdb_wmm -  Query internal regulatory db for wmm rule
 * Regulatory self-managed driver can use it to proactively
 *
 * @alpha2: the ISO/IEC 3166 alpha2 wmm rule to be queried.
 * @freq: the freqency(in MHz) to be queried.
 * @rule: pointer to store the wmm rule from the regulatory db.
 *
 * Self-managed wireless drivers can use this function to  query
 * the internal regulatory database to check whether the given
 * ISO/IEC 3166 alpha2 country and freq have wmm rule limitations.
 *
 * Drivers should check the return value, its possible you can get
 * an -ENODATA.
 *
 * Return: 0 on success. -ENODATA.
 */
int reg_query_regdb_wmm(char *alpha2, int freq,
			struct ieee80211_reg_rule *rule);

/*
 * callbacks for asynchronous cfg80211 methods, notification
 * functions and BSS handling helpers
 */

/**
 * cfg80211_scan_done - notify that scan finished
 *
 * @request: the corresponding scan request
 * @info: information about the completed scan
 */
void cfg80211_scan_done(struct cfg80211_scan_request *request,
			struct cfg80211_scan_info *info);

/**
 * cfg80211_sched_scan_results - notify that new scan results are available
 *
 * @wiphy: the wiphy which got scheduled scan results
 * @reqid: identifier for the related scheduled scan request
 */
void cfg80211_sched_scan_results(struct wiphy *wiphy, u64 reqid);

/**
 * cfg80211_sched_scan_stopped - notify that the scheduled scan has stopped
 *
 * @wiphy: the wiphy on which the scheduled scan stopped
 * @reqid: identifier for the related scheduled scan request
 *
 * The driver can call this function to inform cfg80211 that the
 * scheduled scan had to be stopped, for whatever reason.  The driver
 * is then called back via the sched_scan_stop operation when done.
 */
void cfg80211_sched_scan_stopped(struct wiphy *wiphy, u64 reqid);

/**
 * cfg80211_sched_scan_stopped_rtnl - notify that the scheduled scan has stopped
 *
 * @wiphy: the wiphy on which the scheduled scan stopped
 * @reqid: identifier for the related scheduled scan request
 *
 * The driver can call this function to inform cfg80211 that the
 * scheduled scan had to be stopped, for whatever reason.  The driver
 * is then called back via the sched_scan_stop operation when done.
 * This function should be called with rtnl locked.
 */
void cfg80211_sched_scan_stopped_rtnl(struct wiphy *wiphy, u64 reqid);

/**
 * cfg80211_inform_bss_frame_data - inform cfg80211 of a received BSS frame
 * @wiphy: the wiphy reporting the BSS
 * @data: the BSS metadata
 * @mgmt: the management frame (probe response or beacon)
 * @len: length of the management frame
 * @gfp: context flags
 *
 * This informs cfg80211 that BSS information was found and
 * the BSS should be updated/added.
 *
 * Return: A referenced struct, must be released with cfg80211_put_bss()!
 * Or %NULL on error.
 */
struct cfg80211_bss * __must_check
cfg80211_inform_bss_frame_data(struct wiphy *wiphy,
			       struct cfg80211_inform_bss *data,
			       struct ieee80211_mgmt *mgmt, size_t len,
			       gfp_t gfp);

static inline struct cfg80211_bss * __must_check
cfg80211_inform_bss_width_frame(struct wiphy *wiphy,
				struct ieee80211_channel *rx_channel,
				enum nl80211_bss_scan_width scan_width,
				struct ieee80211_mgmt *mgmt, size_t len,
				s32 signal, gfp_t gfp)
{
	struct cfg80211_inform_bss data = {
		.chan = rx_channel,
		.scan_width = scan_width,
		.signal = signal,
	};

	return cfg80211_inform_bss_frame_data(wiphy, &data, mgmt, len, gfp);
}

static inline struct cfg80211_bss * __must_check
cfg80211_inform_bss_frame(struct wiphy *wiphy,
			  struct ieee80211_channel *rx_channel,
			  struct ieee80211_mgmt *mgmt, size_t len,
			  s32 signal, gfp_t gfp)
{
	struct cfg80211_inform_bss data = {
		.chan = rx_channel,
		.scan_width = NL80211_BSS_CHAN_WIDTH_20,
		.signal = signal,
	};

	return cfg80211_inform_bss_frame_data(wiphy, &data, mgmt, len, gfp);
}

/**
 * cfg80211_gen_new_bssid - generate a nontransmitted BSSID for multi-BSSID
 * @bssid: transmitter BSSID
 * @max_bssid: max BSSID indicator, taken from Multiple BSSID element
 * @mbssid_index: BSSID index, taken from Multiple BSSID index element
 * @new_bssid: calculated nontransmitted BSSID
 */
static inline void cfg80211_gen_new_bssid(const u8 *bssid, u8 max_bssid,
					  u8 mbssid_index, u8 *new_bssid)
{
	u64 bssid_u64 = ether_addr_to_u64(bssid);
	u64 mask = GENMASK_ULL(max_bssid - 1, 0);
	u64 new_bssid_u64;

	new_bssid_u64 = bssid_u64 & ~mask;

	new_bssid_u64 |= ((bssid_u64 & mask) + mbssid_index) & mask;

	u64_to_ether_addr(new_bssid_u64, new_bssid);
}

/**
 * cfg80211_is_element_inherited - returns if element ID should be inherited
 * @element: element to check
 * @non_inherit_element: non inheritance element
 */
bool cfg80211_is_element_inherited(const struct element *element,
				   const struct element *non_inherit_element);

/**
 * cfg80211_merge_profile - merges a MBSSID profile if it is split between IEs
 * @ie: ies
 * @ielen: length of IEs
 * @mbssid_elem: current MBSSID element
 * @sub_elem: current MBSSID subelement (profile)
 * @merged_ie: location of the merged profile
 * @max_copy_len: max merged profile length
 */
size_t cfg80211_merge_profile(const u8 *ie, size_t ielen,
			      const struct element *mbssid_elem,
			      const struct element *sub_elem,
			      u8 *merged_ie, size_t max_copy_len);

/**
 * enum cfg80211_bss_frame_type - frame type that the BSS data came from
 * @CFG80211_BSS_FTYPE_UNKNOWN: driver doesn't know whether the data is
 *	from a beacon or probe response
 * @CFG80211_BSS_FTYPE_BEACON: data comes from a beacon
 * @CFG80211_BSS_FTYPE_PRESP: data comes from a probe response
 */
enum cfg80211_bss_frame_type {
	CFG80211_BSS_FTYPE_UNKNOWN,
	CFG80211_BSS_FTYPE_BEACON,
	CFG80211_BSS_FTYPE_PRESP,
};

/**
 * cfg80211_inform_bss_data - inform cfg80211 of a new BSS
 *
 * @wiphy: the wiphy reporting the BSS
 * @data: the BSS metadata
 * @ftype: frame type (if known)
 * @bssid: the BSSID of the BSS
 * @tsf: the TSF sent by the peer in the beacon/probe response (or 0)
 * @capability: the capability field sent by the peer
 * @beacon_interval: the beacon interval announced by the peer
 * @ie: additional IEs sent by the peer
 * @ielen: length of the additional IEs
 * @gfp: context flags
 *
 * This informs cfg80211 that BSS information was found and
 * the BSS should be updated/added.
 *
 * Return: A referenced struct, must be released with cfg80211_put_bss()!
 * Or %NULL on error.
 */
struct cfg80211_bss * __must_check
cfg80211_inform_bss_data(struct wiphy *wiphy,
			 struct cfg80211_inform_bss *data,
			 enum cfg80211_bss_frame_type ftype,
			 const u8 *bssid, u64 tsf, u16 capability,
			 u16 beacon_interval, const u8 *ie, size_t ielen,
			 gfp_t gfp);

static inline struct cfg80211_bss * __must_check
cfg80211_inform_bss_width(struct wiphy *wiphy,
			  struct ieee80211_channel *rx_channel,
			  enum nl80211_bss_scan_width scan_width,
			  enum cfg80211_bss_frame_type ftype,
			  const u8 *bssid, u64 tsf, u16 capability,
			  u16 beacon_interval, const u8 *ie, size_t ielen,
			  s32 signal, gfp_t gfp)
{
	struct cfg80211_inform_bss data = {
		.chan = rx_channel,
		.scan_width = scan_width,
		.signal = signal,
	};

	return cfg80211_inform_bss_data(wiphy, &data, ftype, bssid, tsf,
					capability, beacon_interval, ie, ielen,
					gfp);
}

static inline struct cfg80211_bss * __must_check
cfg80211_inform_bss(struct wiphy *wiphy,
		    struct ieee80211_channel *rx_channel,
		    enum cfg80211_bss_frame_type ftype,
		    const u8 *bssid, u64 tsf, u16 capability,
		    u16 beacon_interval, const u8 *ie, size_t ielen,
		    s32 signal, gfp_t gfp)
{
	struct cfg80211_inform_bss data = {
		.chan = rx_channel,
		.scan_width = NL80211_BSS_CHAN_WIDTH_20,
		.signal = signal,
	};

	return cfg80211_inform_bss_data(wiphy, &data, ftype, bssid, tsf,
					capability, beacon_interval, ie, ielen,
					gfp);
}

/**
 * cfg80211_get_bss - get a BSS reference
 * @wiphy: the wiphy this BSS struct belongs to
 * @channel: the channel to search on (or %NULL)
 * @bssid: the desired BSSID (or %NULL)
 * @ssid: the desired SSID (or %NULL)
 * @ssid_len: length of the SSID (or 0)
 * @bss_type: type of BSS, see &enum ieee80211_bss_type
 * @privacy: privacy filter, see &enum ieee80211_privacy
 */
struct cfg80211_bss *cfg80211_get_bss(struct wiphy *wiphy,
				      struct ieee80211_channel *channel,
				      const u8 *bssid,
				      const u8 *ssid, size_t ssid_len,
				      enum ieee80211_bss_type bss_type,
				      enum ieee80211_privacy privacy);
static inline struct cfg80211_bss *
cfg80211_get_ibss(struct wiphy *wiphy,
		  struct ieee80211_channel *channel,
		  const u8 *ssid, size_t ssid_len)
{
	return cfg80211_get_bss(wiphy, channel, NULL, ssid, ssid_len,
				IEEE80211_BSS_TYPE_IBSS,
				IEEE80211_PRIVACY_ANY);
}

/**
 * cfg80211_ref_bss - reference BSS struct
 * @wiphy: the wiphy this BSS struct belongs to
 * @bss: the BSS struct to reference
 *
 * Increments the refcount of the given BSS struct.
 */
void cfg80211_ref_bss(struct wiphy *wiphy, struct cfg80211_bss *bss);

/**
 * cfg80211_put_bss - unref BSS struct
 * @wiphy: the wiphy this BSS struct belongs to
 * @bss: the BSS struct
 *
 * Decrements the refcount of the given BSS struct.
 */
void cfg80211_put_bss(struct wiphy *wiphy, struct cfg80211_bss *bss);

/**
 * cfg80211_unlink_bss - unlink BSS from internal data structures
 * @wiphy: the wiphy
 * @bss: the bss to remove
 *
 * This function removes the given BSS from the internal data structures
 * thereby making it no longer show up in scan results etc. Use this
 * function when you detect a BSS is gone. Normally BSSes will also time
 * out, so it is not necessary to use this function at all.
 */
void cfg80211_unlink_bss(struct wiphy *wiphy, struct cfg80211_bss *bss);

/**
 * cfg80211_bss_iter - iterate all BSS entries
 *
 * This function iterates over the BSS entries associated with the given wiphy
 * and calls the callback for the iterated BSS. The iterator function is not
 * allowed to call functions that might modify the internal state of the BSS DB.
 *
 * @wiphy: the wiphy
 * @chandef: if given, the iterator function will be called only if the channel
 *     of the currently iterated BSS is a subset of the given channel.
 * @iter: the iterator function to call
 * @iter_data: an argument to the iterator function
 */
void cfg80211_bss_iter(struct wiphy *wiphy,
		       struct cfg80211_chan_def *chandef,
		       void (*iter)(struct wiphy *wiphy,
				    struct cfg80211_bss *bss,
				    void *data),
		       void *iter_data);

static inline enum nl80211_bss_scan_width
cfg80211_chandef_to_scan_width(const struct cfg80211_chan_def *chandef)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_5:
		return NL80211_BSS_CHAN_WIDTH_5;
	case NL80211_CHAN_WIDTH_10:
		return NL80211_BSS_CHAN_WIDTH_10;
	default:
		return NL80211_BSS_CHAN_WIDTH_20;
	}
}

/**
 * cfg80211_rx_mlme_mgmt - notification of processed MLME management frame
 * @dev: network device
 * @buf: authentication frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever an authentication, disassociation or
 * deauthentication frame has been received and processed in station mode.
 * After being asked to authenticate via cfg80211_ops::auth() the driver must
 * call either this function or cfg80211_auth_timeout().
 * After being asked to associate via cfg80211_ops::assoc() the driver must
 * call either this function or cfg80211_auth_timeout().
 * While connected, the driver must calls this for received and processed
 * disassociation and deauthentication frames. If the frame couldn't be used
 * because it was unprotected, the driver must call the function
 * cfg80211_rx_unprot_mlme_mgmt() instead.
 *
 * This function may sleep. The caller must hold the corresponding wdev's mutex.
 */
void cfg80211_rx_mlme_mgmt(struct net_device *dev, const u8 *buf, size_t len);

/**
 * cfg80211_auth_timeout - notification of timed out authentication
 * @dev: network device
 * @addr: The MAC address of the device with which the authentication timed out
 *
 * This function may sleep. The caller must hold the corresponding wdev's
 * mutex.
 */
void cfg80211_auth_timeout(struct net_device *dev, const u8 *addr);

/**
 * cfg80211_rx_assoc_resp - notification of processed association response
 * @dev: network device
 * @bss: the BSS that association was requested with, ownership of the pointer
 *	moves to cfg80211 in this call
 * @buf: (Re)Association Response frame (header + body)
 * @len: length of the frame data
 * @uapsd_queues: bitmap of queues configured for uapsd. Same format
 *	as the AC bitmap in the QoS info field
 * @req_ies: information elements from the (Re)Association Request frame
 * @req_ies_len: length of req_ies data
 *
 * After being asked to associate via cfg80211_ops::assoc() the driver must
 * call either this function or cfg80211_auth_timeout().
 *
 * This function may sleep. The caller must hold the corresponding wdev's mutex.
 */
void cfg80211_rx_assoc_resp(struct net_device *dev,
			    struct cfg80211_bss *bss,
			    const u8 *buf, size_t len,
			    int uapsd_queues,
			    const u8 *req_ies, size_t req_ies_len);

/**
 * cfg80211_assoc_timeout - notification of timed out association
 * @dev: network device
 * @bss: The BSS entry with which association timed out.
 *
 * This function may sleep. The caller must hold the corresponding wdev's mutex.
 */
void cfg80211_assoc_timeout(struct net_device *dev, struct cfg80211_bss *bss);

/**
 * cfg80211_abandon_assoc - notify cfg80211 of abandoned association attempt
 * @dev: network device
 * @bss: The BSS entry with which association was abandoned.
 *
 * Call this whenever - for reasons reported through other API, like deauth RX,
 * an association attempt was abandoned.
 * This function may sleep. The caller must hold the corresponding wdev's mutex.
 */
void cfg80211_abandon_assoc(struct net_device *dev, struct cfg80211_bss *bss);

/**
 * cfg80211_tx_mlme_mgmt - notification of transmitted deauth/disassoc frame
 * @dev: network device
 * @buf: 802.11 frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever deauthentication has been processed in
 * station mode. This includes both received deauthentication frames and
 * locally generated ones. This function may sleep. The caller must hold the
 * corresponding wdev's mutex.
 */
void cfg80211_tx_mlme_mgmt(struct net_device *dev, const u8 *buf, size_t len);

/**
 * cfg80211_rx_unprot_mlme_mgmt - notification of unprotected mlme mgmt frame
 * @dev: network device
 * @buf: received management frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever a received deauthentication or dissassoc
 * frame has been dropped in station mode because of MFP being used but the
 * frame was not protected. This is also used to notify reception of a Beacon
 * frame that was dropped because it did not include a valid MME MIC while
 * beacon protection was enabled (BIGTK configured in station mode).
 *
 * This function may sleep.
 */
void cfg80211_rx_unprot_mlme_mgmt(struct net_device *dev,
				  const u8 *buf, size_t len);

/**
 * cfg80211_michael_mic_failure - notification of Michael MIC failure (TKIP)
 * @dev: network device
 * @addr: The source MAC address of the frame
 * @key_type: The key type that the received frame used
 * @key_id: Key identifier (0..3). Can be -1 if missing.
 * @tsc: The TSC value of the frame that generated the MIC failure (6 octets)
 * @gfp: allocation flags
 *
 * This function is called whenever the local MAC detects a MIC failure in a
 * received frame. This matches with MLME-MICHAELMICFAILURE.indication()
 * primitive.
 */
void cfg80211_michael_mic_failure(struct net_device *dev, const u8 *addr,
				  enum nl80211_key_type key_type, int key_id,
				  const u8 *tsc, gfp_t gfp);

/**
 * cfg80211_ibss_joined - notify cfg80211 that device joined an IBSS
 *
 * @dev: network device
 * @bssid: the BSSID of the IBSS joined
 * @channel: the channel of the IBSS joined
 * @gfp: allocation flags
 *
 * This function notifies cfg80211 that the device joined an IBSS or
 * switched to a different BSSID. Before this function can be called,
 * either a beacon has to have been received from the IBSS, or one of
 * the cfg80211_inform_bss{,_frame} functions must have been called
 * with the locally generated beacon -- this guarantees that there is
 * always a scan result for this IBSS. cfg80211 will handle the rest.
 */
void cfg80211_ibss_joined(struct net_device *dev, const u8 *bssid,
			  struct ieee80211_channel *channel, gfp_t gfp);

/**
 * cfg80211_notify_new_candidate - notify cfg80211 of a new mesh peer candidate
 *
 * @dev: network device
 * @macaddr: the MAC address of the new candidate
 * @ie: information elements advertised by the peer candidate
 * @ie_len: length of the information elements buffer
 * @gfp: allocation flags
 *
 * This function notifies cfg80211 that the mesh peer candidate has been
 * detected, most likely via a beacon or, less likely, via a probe response.
 * cfg80211 then sends a notification to userspace.
 */
void cfg80211_notify_new_peer_candidate(struct net_device *dev,
		const u8 *macaddr, const u8 *ie, u8 ie_len,
		int sig_dbm, gfp_t gfp);

/**
 * DOC: RFkill integration
 *
 * RFkill integration in cfg80211 is almost invisible to drivers,
 * as cfg80211 automatically registers an rfkill instance for each
 * wireless device it knows about. Soft kill is also translated
 * into disconnecting and turning all interfaces off, drivers are
 * expected to turn off the device when all interfaces are down.
 *
 * However, devices may have a hard RFkill line, in which case they
 * also need to interact with the rfkill subsystem, via cfg80211.
 * They can do this with a few helper functions documented here.
 */

/**
 * wiphy_rfkill_set_hw_state - notify cfg80211 about hw block state
 * @wiphy: the wiphy
 * @blocked: block status
 */
void wiphy_rfkill_set_hw_state(struct wiphy *wiphy, bool blocked);

/**
 * wiphy_rfkill_start_polling - start polling rfkill
 * @wiphy: the wiphy
 */
void wiphy_rfkill_start_polling(struct wiphy *wiphy);

/**
 * wiphy_rfkill_stop_polling - stop polling rfkill
 * @wiphy: the wiphy
 */
void wiphy_rfkill_stop_polling(struct wiphy *wiphy);

/**
 * DOC: Vendor commands
 *
 * Occasionally, there are special protocol or firmware features that
 * can't be implemented very openly. For this and similar cases, the
 * vendor command functionality allows implementing the features with
 * (typically closed-source) userspace and firmware, using nl80211 as
 * the configuration mechanism.
 *
 * A driver supporting vendor commands must register them as an array
 * in struct wiphy, with handlers for each one, each command has an
 * OUI and sub command ID to identify it.
 *
 * Note that this feature should not be (ab)used to implement protocol
 * features that could openly be shared across drivers. In particular,
 * it must never be required to use vendor commands to implement any
 * "normal" functionality that higher-level userspace like connection
 * managers etc. need.
 */

struct sk_buff *__cfg80211_alloc_reply_skb(struct wiphy *wiphy,
					   enum nl80211_commands cmd,
					   enum nl80211_attrs attr,
					   int approxlen);

struct sk_buff *__cfg80211_alloc_event_skb(struct wiphy *wiphy,
					   struct wireless_dev *wdev,
					   enum nl80211_commands cmd,
					   enum nl80211_attrs attr,
					   unsigned int portid,
					   int vendor_event_idx,
					   int approxlen, gfp_t gfp);

void __cfg80211_send_event_skb(struct sk_buff *skb, gfp_t gfp);

/**
 * cfg80211_vendor_cmd_alloc_reply_skb - allocate vendor command reply
 * @wiphy: the wiphy
 * @approxlen: an upper bound of the length of the data that will
 *	be put into the skb
 *
 * This function allocates and pre-fills an skb for a reply to
 * a vendor command. Since it is intended for a reply, calling
 * it outside of a vendor command's doit() operation is invalid.
 *
 * The returned skb is pre-filled with some identifying data in
 * a way that any data that is put into the skb (with skb_put(),
 * nla_put() or similar) will end up being within the
 * %NL80211_ATTR_VENDOR_DATA attribute, so all that needs to be done
 * with the skb is adding data for the corresponding userspace tool
 * which can then read that data out of the vendor data attribute.
 * You must not modify the skb in any other way.
 *
 * When done, call cfg80211_vendor_cmd_reply() with the skb and return
 * its error code as the result of the doit() operation.
 *
 * Return: An allocated and pre-filled skb. %NULL if any errors happen.
 */
static inline struct sk_buff *
cfg80211_vendor_cmd_alloc_reply_skb(struct wiphy *wiphy, int approxlen)
{
	return __cfg80211_alloc_reply_skb(wiphy, NL80211_CMD_VENDOR,
					  NL80211_ATTR_VENDOR_DATA, approxlen);
}

/**
 * cfg80211_vendor_cmd_reply - send the reply skb
 * @skb: The skb, must have been allocated with
 *	cfg80211_vendor_cmd_alloc_reply_skb()
 *
 * Since calling this function will usually be the last thing
 * before returning from the vendor command doit() you should
 * return the error code.  Note that this function consumes the
 * skb regardless of the return value.
 *
 * Return: An error code or 0 on success.
 */
int cfg80211_vendor_cmd_reply(struct sk_buff *skb);

/**
 * cfg80211_vendor_cmd_get_sender
 * @wiphy: the wiphy
 *
 * Return the current netlink port ID in a vendor command handler.
 * Valid to call only there.
 */
unsigned int cfg80211_vendor_cmd_get_sender(struct wiphy *wiphy);

/**
 * cfg80211_vendor_event_alloc - allocate vendor-specific event skb
 * @wiphy: the wiphy
 * @wdev: the wireless device
 * @event_idx: index of the vendor event in the wiphy's vendor_events
 * @approxlen: an upper bound of the length of the data that will
 *	be put into the skb
 * @gfp: allocation flags
 *
 * This function allocates and pre-fills an skb for an event on the
 * vendor-specific multicast group.
 *
 * If wdev != NULL, both the ifindex and identifier of the specified
 * wireless device are added to the event message before the vendor data
 * attribute.
 *
 * When done filling the skb, call cfg80211_vendor_event() with the
 * skb to send the event.
 *
 * Return: An allocated and pre-filled skb. %NULL if any errors happen.
 */
static inline struct sk_buff *
cfg80211_vendor_event_alloc(struct wiphy *wiphy, struct wireless_dev *wdev,
			     int approxlen, int event_idx, gfp_t gfp)
{
	return __cfg80211_alloc_event_skb(wiphy, wdev, NL80211_CMD_VENDOR,
					  NL80211_ATTR_VENDOR_DATA,
					  0, event_idx, approxlen, gfp);
}

/**
 * cfg80211_vendor_event_alloc_ucast - alloc unicast vendor-specific event skb
 * @wiphy: the wiphy
 * @wdev: the wireless device
 * @event_idx: index of the vendor event in the wiphy's vendor_events
 * @portid: port ID of the receiver
 * @approxlen: an upper bound of the length of the data that will
 *	be put into the skb
 * @gfp: allocation flags
 *
 * This function allocates and pre-fills an skb for an event to send to
 * a specific (userland) socket. This socket would previously have been
 * obtained by cfg80211_vendor_cmd_get_sender(), and the caller MUST take
 * care to register a netlink notifier to see when the socket closes.
 *
 * If wdev != NULL, both the ifindex and identifier of the specified
 * wireless device are added to the event message before the vendor data
 * attribute.
 *
 * When done filling the skb, call cfg80211_vendor_event() with the
 * skb to send the event.
 *
 * Return: An allocated and pre-filled skb. %NULL if any errors happen.
 */
static inline struct sk_buff *
cfg80211_vendor_event_alloc_ucast(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  unsigned int portid, int approxlen,
				  int event_idx, gfp_t gfp)
{
	return __cfg80211_alloc_event_skb(wiphy, wdev, NL80211_CMD_VENDOR,
					  NL80211_ATTR_VENDOR_DATA,
					  portid, event_idx, approxlen, gfp);
}

/**
 * cfg80211_vendor_event - send the event
 * @skb: The skb, must have been allocated with cfg80211_vendor_event_alloc()
 * @gfp: allocation flags
 *
 * This function sends the given @skb, which must have been allocated
 * by cfg80211_vendor_event_alloc(), as an event. It always consumes it.
 */
static inline void cfg80211_vendor_event(struct sk_buff *skb, gfp_t gfp)
{
	__cfg80211_send_event_skb(skb, gfp);
}

#ifdef CONFIG_NL80211_TESTMODE
/**
 * DOC: Test mode
 *
 * Test mode is a set of utility functions to allow drivers to
 * interact with driver-specific tools to aid, for instance,
 * factory programming.
 *
 * This chapter describes how drivers interact with it, for more
 * information see the nl80211 book's chapter on it.
 */

/**
 * cfg80211_testmode_alloc_reply_skb - allocate testmode reply
 * @wiphy: the wiphy
 * @approxlen: an upper bound of the length of the data that will
 *	be put into the skb
 *
 * This function allocates and pre-fills an skb for a reply to
 * the testmode command. Since it is intended for a reply, calling
 * it outside of the @testmode_cmd operation is invalid.
 *
 * The returned skb is pre-filled with the wiphy index and set up in
 * a way that any data that is put into the skb (with skb_put(),
 * nla_put() or similar) will end up being within the
 * %NL80211_ATTR_TESTDATA attribute, so all that needs to be done
 * with the skb is adding data for the corresponding userspace tool
 * which can then read that data out of the testdata attribute. You
 * must not modify the skb in any other way.
 *
 * When done, call cfg80211_testmode_reply() with the skb and return
 * its error code as the result of the @testmode_cmd operation.
 *
 * Return: An allocated and pre-filled skb. %NULL if any errors happen.
 */
static inline struct sk_buff *
cfg80211_testmode_alloc_reply_skb(struct wiphy *wiphy, int approxlen)
{
	return __cfg80211_alloc_reply_skb(wiphy, NL80211_CMD_TESTMODE,
					  NL80211_ATTR_TESTDATA, approxlen);
}

/**
 * cfg80211_testmode_reply - send the reply skb
 * @skb: The skb, must have been allocated with
 *	cfg80211_testmode_alloc_reply_skb()
 *
 * Since calling this function will usually be the last thing
 * before returning from the @testmode_cmd you should return
 * the error code.  Note that this function consumes the skb
 * regardless of the return value.
 *
 * Return: An error code or 0 on success.
 */
static inline int cfg80211_testmode_reply(struct sk_buff *skb)
{
	return cfg80211_vendor_cmd_reply(skb);
}

/**
 * cfg80211_testmode_alloc_event_skb - allocate testmode event
 * @wiphy: the wiphy
 * @approxlen: an upper bound of the length of the data that will
 *	be put into the skb
 * @gfp: allocation flags
 *
 * This function allocates and pre-fills an skb for an event on the
 * testmode multicast group.
 *
 * The returned skb is set up in the same way as with
 * cfg80211_testmode_alloc_reply_skb() but prepared for an event. As
 * there, you should simply add data to it that will then end up in the
 * %NL80211_ATTR_TESTDATA attribute. Again, you must not modify the skb
 * in any other way.
 *
 * When done filling the skb, call cfg80211_testmode_event() with the
 * skb to send the event.
 *
 * Return: An allocated and pre-filled skb. %NULL if any errors happen.
 */
static inline struct sk_buff *
cfg80211_testmode_alloc_event_skb(struct wiphy *wiphy, int approxlen, gfp_t gfp)
{
	return __cfg80211_alloc_event_skb(wiphy, NULL, NL80211_CMD_TESTMODE,
					  NL80211_ATTR_TESTDATA, 0, -1,
					  approxlen, gfp);
}

/**
 * cfg80211_testmode_event - send the event
 * @skb: The skb, must have been allocated with
 *	cfg80211_testmode_alloc_event_skb()
 * @gfp: allocation flags
 *
 * This function sends the given @skb, which must have been allocated
 * by cfg80211_testmode_alloc_event_skb(), as an event. It always
 * consumes it.
 */
static inline void cfg80211_testmode_event(struct sk_buff *skb, gfp_t gfp)
{
	__cfg80211_send_event_skb(skb, gfp);
}

#define CFG80211_TESTMODE_CMD(cmd)	.testmode_cmd = (cmd),
#define CFG80211_TESTMODE_DUMP(cmd)	.testmode_dump = (cmd),
#else
#define CFG80211_TESTMODE_CMD(cmd)
#define CFG80211_TESTMODE_DUMP(cmd)
#endif

/**
 * struct cfg80211_fils_resp_params - FILS connection response params
 * @kek: KEK derived from a successful FILS connection (may be %NULL)
 * @kek_len: Length of @fils_kek in octets
 * @update_erp_next_seq_num: Boolean value to specify whether the value in
 *	@erp_next_seq_num is valid.
 * @erp_next_seq_num: The next sequence number to use in ERP message in
 *	FILS Authentication. This value should be specified irrespective of the
 *	status for a FILS connection.
 * @pmk: A new PMK if derived from a successful FILS connection (may be %NULL).
 * @pmk_len: Length of @pmk in octets
 * @pmkid: A new PMKID if derived from a successful FILS connection or the PMKID
 *	used for this FILS connection (may be %NULL).
 */
struct cfg80211_fils_resp_params {
	const u8 *kek;
	size_t kek_len;
	bool update_erp_next_seq_num;
	u16 erp_next_seq_num;
	const u8 *pmk;
	size_t pmk_len;
	const u8 *pmkid;
};

/**
 * struct cfg80211_connect_resp_params - Connection response params
 * @status: Status code, %WLAN_STATUS_SUCCESS for successful connection, use
 *	%WLAN_STATUS_UNSPECIFIED_FAILURE if your device cannot give you
 *	the real status code for failures. If this call is used to report a
 *	failure due to a timeout (e.g., not receiving an Authentication frame
 *	from the AP) instead of an explicit rejection by the AP, -1 is used to
 *	indicate that this is a failure, but without a status code.
 *	@timeout_reason is used to report the reason for the timeout in that
 *	case.
 * @bssid: The BSSID of the AP (may be %NULL)
 * @bss: Entry of bss to which STA got connected to, can be obtained through
 *	cfg80211_get_bss() (may be %NULL). But it is recommended to store the
 *	bss from the connect_request and hold a reference to it and return
 *	through this param to avoid a warning if the bss is expired during the
 *	connection, esp. for those drivers implementing connect op.
 *	Only one parameter among @bssid and @bss needs to be specified.
 * @req_ie: Association request IEs (may be %NULL)
 * @req_ie_len: Association request IEs length
 * @resp_ie: Association response IEs (may be %NULL)
 * @resp_ie_len: Association response IEs length
 * @fils: FILS connection response parameters.
 * @timeout_reason: Reason for connection timeout. This is used when the
 *	connection fails due to a timeout instead of an explicit rejection from
 *	the AP. %NL80211_TIMEOUT_UNSPECIFIED is used when the timeout reason is
 *	not known. This value is used only if @status < 0 to indicate that the
 *	failure is due to a timeout and not due to explicit rejection by the AP.
 *	This value is ignored in other cases (@status >= 0).
 */
struct cfg80211_connect_resp_params {
	int status;
	const u8 *bssid;
	struct cfg80211_bss *bss;
	const u8 *req_ie;
	size_t req_ie_len;
	const u8 *resp_ie;
	size_t resp_ie_len;
	struct cfg80211_fils_resp_params fils;
	enum nl80211_timeout_reason timeout_reason;
};

/**
 * cfg80211_connect_done - notify cfg80211 of connection result
 *
 * @dev: network device
 * @params: connection response parameters
 * @gfp: allocation flags
 *
 * It should be called by the underlying driver once execution of the connection
 * request from connect() has been completed. This is similar to
 * cfg80211_connect_bss(), but takes a structure pointer for connection response
 * parameters. Only one of the functions among cfg80211_connect_bss(),
 * cfg80211_connect_result(), cfg80211_connect_timeout(),
 * and cfg80211_connect_done() should be called.
 */
void cfg80211_connect_done(struct net_device *dev,
			   struct cfg80211_connect_resp_params *params,
			   gfp_t gfp);

/**
 * cfg80211_connect_bss - notify cfg80211 of connection result
 *
 * @dev: network device
 * @bssid: the BSSID of the AP
 * @bss: Entry of bss to which STA got connected to, can be obtained through
 *	cfg80211_get_bss() (may be %NULL). But it is recommended to store the
 *	bss from the connect_request and hold a reference to it and return
 *	through this param to avoid a warning if the bss is expired during the
 *	connection, esp. for those drivers implementing connect op.
 *	Only one parameter among @bssid and @bss needs to be specified.
 * @req_ie: association request IEs (maybe be %NULL)
 * @req_ie_len: association request IEs length
 * @resp_ie: association response IEs (may be %NULL)
 * @resp_ie_len: assoc response IEs length
 * @status: status code, %WLAN_STATUS_SUCCESS for successful connection, use
 *	%WLAN_STATUS_UNSPECIFIED_FAILURE if your device cannot give you
 *	the real status code for failures. If this call is used to report a
 *	failure due to a timeout (e.g., not receiving an Authentication frame
 *	from the AP) instead of an explicit rejection by the AP, -1 is used to
 *	indicate that this is a failure, but without a status code.
 *	@timeout_reason is used to report the reason for the timeout in that
 *	case.
 * @gfp: allocation flags
 * @timeout_reason: reason for connection timeout. This is used when the
 *	connection fails due to a timeout instead of an explicit rejection from
 *	the AP. %NL80211_TIMEOUT_UNSPECIFIED is used when the timeout reason is
 *	not known. This value is used only if @status < 0 to indicate that the
 *	failure is due to a timeout and not due to explicit rejection by the AP.
 *	This value is ignored in other cases (@status >= 0).
 *
 * It should be called by the underlying driver once execution of the connection
 * request from connect() has been completed. This is similar to
 * cfg80211_connect_result(), but with the option of identifying the exact bss
 * entry for the connection. Only one of the functions among
 * cfg80211_connect_bss(), cfg80211_connect_result(),
 * cfg80211_connect_timeout(), and cfg80211_connect_done() should be called.
 */
static inline void
cfg80211_connect_bss(struct net_device *dev, const u8 *bssid,
		     struct cfg80211_bss *bss, const u8 *req_ie,
		     size_t req_ie_len, const u8 *resp_ie,
		     size_t resp_ie_len, int status, gfp_t gfp,
		     enum nl80211_timeout_reason timeout_reason)
{
	struct cfg80211_connect_resp_params params;

	memset(&params, 0, sizeof(params));
	params.status = status;
	params.bssid = bssid;
	params.bss = bss;
	params.req_ie = req_ie;
	params.req_ie_len = req_ie_len;
	params.resp_ie = resp_ie;
	params.resp_ie_len = resp_ie_len;
	params.timeout_reason = timeout_reason;

	cfg80211_connect_done(dev, &params, gfp);
}

/**
 * cfg80211_connect_result - notify cfg80211 of connection result
 *
 * @dev: network device
 * @bssid: the BSSID of the AP
 * @req_ie: association request IEs (maybe be %NULL)
 * @req_ie_len: association request IEs length
 * @resp_ie: association response IEs (may be %NULL)
 * @resp_ie_len: assoc response IEs length
 * @status: status code, %WLAN_STATUS_SUCCESS for successful connection, use
 *	%WLAN_STATUS_UNSPECIFIED_FAILURE if your device cannot give you
 *	the real status code for failures.
 * @gfp: allocation flags
 *
 * It should be called by the underlying driver once execution of the connection
 * request from connect() has been completed. This is similar to
 * cfg80211_connect_bss() which allows the exact bss entry to be specified. Only
 * one of the functions among cfg80211_connect_bss(), cfg80211_connect_result(),
 * cfg80211_connect_timeout(), and cfg80211_connect_done() should be called.
 */
static inline void
cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
			const u8 *req_ie, size_t req_ie_len,
			const u8 *resp_ie, size_t resp_ie_len,
			u16 status, gfp_t gfp)
{
	cfg80211_connect_bss(dev, bssid, NULL, req_ie, req_ie_len, resp_ie,
			     resp_ie_len, status, gfp,
			     NL80211_TIMEOUT_UNSPECIFIED);
}

/**
 * cfg80211_connect_timeout - notify cfg80211 of connection timeout
 *
 * @dev: network device
 * @bssid: the BSSID of the AP
 * @req_ie: association request IEs (maybe be %NULL)
 * @req_ie_len: association request IEs length
 * @gfp: allocation flags
 * @timeout_reason: reason for connection timeout.
 *
 * It should be called by the underlying driver whenever connect() has failed
 * in a sequence where no explicit authentication/association rejection was
 * received from the AP. This could happen, e.g., due to not being able to send
 * out the Authentication or Association Request frame or timing out while
 * waiting for the response. Only one of the functions among
 * cfg80211_connect_bss(), cfg80211_connect_result(),
 * cfg80211_connect_timeout(), and cfg80211_connect_done() should be called.
 */
static inline void
cfg80211_connect_timeout(struct net_device *dev, const u8 *bssid,
			 const u8 *req_ie, size_t req_ie_len, gfp_t gfp,
			 enum nl80211_timeout_reason timeout_reason)
{
	cfg80211_connect_bss(dev, bssid, NULL, req_ie, req_ie_len, NULL, 0, -1,
			     gfp, timeout_reason);
}

/**
 * struct cfg80211_roam_info - driver initiated roaming information
 *
 * @channel: the channel of the new AP
 * @bss: entry of bss to which STA got roamed (may be %NULL if %bssid is set)
 * @bssid: the BSSID of the new AP (may be %NULL if %bss is set)
 * @req_ie: association request IEs (maybe be %NULL)
 * @req_ie_len: association request IEs length
 * @resp_ie: association response IEs (may be %NULL)
 * @resp_ie_len: assoc response IEs length
 * @fils: FILS related roaming information.
 */
struct cfg80211_roam_info {
	struct ieee80211_channel *channel;
	struct cfg80211_bss *bss;
	const u8 *bssid;
	const u8 *req_ie;
	size_t req_ie_len;
	const u8 *resp_ie;
	size_t resp_ie_len;
	struct cfg80211_fils_resp_params fils;
};

/**
 * cfg80211_roamed - notify cfg80211 of roaming
 *
 * @dev: network device
 * @info: information about the new BSS. struct &cfg80211_roam_info.
 * @gfp: allocation flags
 *
 * This function may be called with the driver passing either the BSSID of the
 * new AP or passing the bss entry to avoid a race in timeout of the bss entry.
 * It should be called by the underlying driver whenever it roamed from one AP
 * to another while connected. Drivers which have roaming implemented in
 * firmware should pass the bss entry to avoid a race in bss entry timeout where
 * the bss entry of the new AP is seen in the driver, but gets timed out by the
 * time it is accessed in __cfg80211_roamed() due to delay in scheduling
 * rdev->event_work. In case of any failures, the reference is released
 * either in cfg80211_roamed() or in __cfg80211_romed(), Otherwise, it will be
 * released while disconnecting from the current bss.
 */
void cfg80211_roamed(struct net_device *dev, struct cfg80211_roam_info *info,
		     gfp_t gfp);

/**
 * cfg80211_port_authorized - notify cfg80211 of successful security association
 *
 * @dev: network device
 * @bssid: the BSSID of the AP
 * @gfp: allocation flags
 *
 * This function should be called by a driver that supports 4 way handshake
 * offload after a security association was successfully established (i.e.,
 * the 4 way handshake was completed successfully). The call to this function
 * should be preceded with a call to cfg80211_connect_result(),
 * cfg80211_connect_done(), cfg80211_connect_bss() or cfg80211_roamed() to
 * indicate the 802.11 association.
 */
void cfg80211_port_authorized(struct net_device *dev, const u8 *bssid,
			      gfp_t gfp);

/**
 * cfg80211_disconnected - notify cfg80211 that connection was dropped
 *
 * @dev: network device
 * @ie: information elements of the deauth/disassoc frame (may be %NULL)
 * @ie_len: length of IEs
 * @reason: reason code for the disconnection, set it to 0 if unknown
 * @locally_generated: disconnection was requested locally
 * @gfp: allocation flags
 *
 * After it calls this function, the driver should enter an idle state
 * and not try to connect to any AP any more.
 */
void cfg80211_disconnected(struct net_device *dev, u16 reason,
			   const u8 *ie, size_t ie_len,
			   bool locally_generated, gfp_t gfp);

/**
 * cfg80211_ready_on_channel - notification of remain_on_channel start
 * @wdev: wireless device
 * @cookie: the request cookie
 * @chan: The current channel (from remain_on_channel request)
 * @duration: Duration in milliseconds that the driver intents to remain on the
 *	channel
 * @gfp: allocation flags
 */
void cfg80211_ready_on_channel(struct wireless_dev *wdev, u64 cookie,
			       struct ieee80211_channel *chan,
			       unsigned int duration, gfp_t gfp);

/**
 * cfg80211_remain_on_channel_expired - remain_on_channel duration expired
 * @wdev: wireless device
 * @cookie: the request cookie
 * @chan: The current channel (from remain_on_channel request)
 * @gfp: allocation flags
 */
void cfg80211_remain_on_channel_expired(struct wireless_dev *wdev, u64 cookie,
					struct ieee80211_channel *chan,
					gfp_t gfp);

/**
 * cfg80211_tx_mgmt_expired - tx_mgmt duration expired
 * @wdev: wireless device
 * @cookie: the requested cookie
 * @chan: The current channel (from tx_mgmt request)
 * @gfp: allocation flags
 */
void cfg80211_tx_mgmt_expired(struct wireless_dev *wdev, u64 cookie,
			      struct ieee80211_channel *chan, gfp_t gfp);

/**
 * cfg80211_sinfo_alloc_tid_stats - allocate per-tid statistics.
 *
 * @sinfo: the station information
 * @gfp: allocation flags
 */
int cfg80211_sinfo_alloc_tid_stats(struct station_info *sinfo, gfp_t gfp);

/**
 * cfg80211_sinfo_release_content - release contents of station info
 * @sinfo: the station information
 *
 * Releases any potentially allocated sub-information of the station
 * information, but not the struct itself (since it's typically on
 * the stack.)
 */
static inline void cfg80211_sinfo_release_content(struct station_info *sinfo)
{
	kfree(sinfo->pertid);
}

/**
 * cfg80211_new_sta - notify userspace about station
 *
 * @dev: the netdev
 * @mac_addr: the station's address
 * @sinfo: the station information
 * @gfp: allocation flags
 */
void cfg80211_new_sta(struct net_device *dev, const u8 *mac_addr,
		      struct station_info *sinfo, gfp_t gfp);

/**
 * cfg80211_del_sta_sinfo - notify userspace about deletion of a station
 * @dev: the netdev
 * @mac_addr: the station's address
 * @sinfo: the station information/statistics
 * @gfp: allocation flags
 */
void cfg80211_del_sta_sinfo(struct net_device *dev, const u8 *mac_addr,
			    struct station_info *sinfo, gfp_t gfp);

/**
 * cfg80211_del_sta - notify userspace about deletion of a station
 *
 * @dev: the netdev
 * @mac_addr: the station's address
 * @gfp: allocation flags
 */
static inline void cfg80211_del_sta(struct net_device *dev,
				    const u8 *mac_addr, gfp_t gfp)
{
	cfg80211_del_sta_sinfo(dev, mac_addr, NULL, gfp);
}

/**
 * cfg80211_conn_failed - connection request failed notification
 *
 * @dev: the netdev
 * @mac_addr: the station's address
 * @reason: the reason for connection failure
 * @gfp: allocation flags
 *
 * Whenever a station tries to connect to an AP and if the station
 * could not connect to the AP as the AP has rejected the connection
 * for some reasons, this function is called.
 *
 * The reason for connection failure can be any of the value from
 * nl80211_connect_failed_reason enum
 */
void cfg80211_conn_failed(struct net_device *dev, const u8 *mac_addr,
			  enum nl80211_connect_failed_reason reason,
			  gfp_t gfp);

/**
 * cfg80211_rx_mgmt_khz - notification of received, unprocessed management frame
 * @wdev: wireless device receiving the frame
 * @freq: Frequency on which the frame was received in KHz
 * @sig_dbm: signal strength in dBm, or 0 if unknown
 * @buf: Management frame (header + body)
 * @len: length of the frame data
 * @flags: flags, as defined in enum nl80211_rxmgmt_flags
 *
 * This function is called whenever an Action frame is received for a station
 * mode interface, but is not processed in kernel.
 *
 * Return: %true if a user space application has registered for this frame.
 * For action frames, that makes it responsible for rejecting unrecognized
 * action frames; %false otherwise, in which case for action frames the
 * driver is responsible for rejecting the frame.
 */
bool cfg80211_rx_mgmt_khz(struct wireless_dev *wdev, int freq, int sig_dbm,
			  const u8 *buf, size_t len, u32 flags);

/**
 * cfg80211_rx_mgmt - notification of received, unprocessed management frame
 * @wdev: wireless device receiving the frame
 * @freq: Frequency on which the frame was received in MHz
 * @sig_dbm: signal strength in dBm, or 0 if unknown
 * @buf: Management frame (header + body)
 * @len: length of the frame data
 * @flags: flags, as defined in enum nl80211_rxmgmt_flags
 *
 * This function is called whenever an Action frame is received for a station
 * mode interface, but is not processed in kernel.
 *
 * Return: %true if a user space application has registered for this frame.
 * For action frames, that makes it responsible for rejecting unrecognized
 * action frames; %false otherwise, in which case for action frames the
 * driver is responsible for rejecting the frame.
 */
static inline bool cfg80211_rx_mgmt(struct wireless_dev *wdev, int freq,
				    int sig_dbm, const u8 *buf, size_t len,
				    u32 flags)
{
	return cfg80211_rx_mgmt_khz(wdev, MHZ_TO_KHZ(freq), sig_dbm, buf, len,
				    flags);
}

/**
 * cfg80211_mgmt_tx_status - notification of TX status for management frame
 * @wdev: wireless device receiving the frame
 * @cookie: Cookie returned by cfg80211_ops::mgmt_tx()
 * @buf: Management frame (header + body)
 * @len: length of the frame data
 * @ack: Whether frame was acknowledged
 * @gfp: context flags
 *
 * This function is called whenever a management frame was requested to be
 * transmitted with cfg80211_ops::mgmt_tx() to report the TX status of the
 * transmission attempt.
 */
void cfg80211_mgmt_tx_status(struct wireless_dev *wdev, u64 cookie,
			     const u8 *buf, size_t len, bool ack, gfp_t gfp);

/**
 * cfg80211_control_port_tx_status - notification of TX status for control
 *                                   port frames
 * @wdev: wireless device receiving the frame
 * @cookie: Cookie returned by cfg80211_ops::tx_control_port()
 * @buf: Data frame (header + body)
 * @len: length of the frame data
 * @ack: Whether frame was acknowledged
 * @gfp: context flags
 *
 * This function is called whenever a control port frame was requested to be
 * transmitted with cfg80211_ops::tx_control_port() to report the TX status of
 * the transmission attempt.
 */
void cfg80211_control_port_tx_status(struct wireless_dev *wdev, u64 cookie,
				     const u8 *buf, size_t len, bool ack,
				     gfp_t gfp);

/**
 * cfg80211_rx_control_port - notification about a received control port frame
 * @dev: The device the frame matched to
 * @skb: The skbuf with the control port frame.  It is assumed that the skbuf
 *	is 802.3 formatted (with 802.3 header).  The skb can be non-linear.
 *	This function does not take ownership of the skb, so the caller is
 *	responsible for any cleanup.  The caller must also ensure that
 *	skb->protocol is set appropriately.
 * @unencrypted: Whether the frame was received unencrypted
 *
 * This function is used to inform userspace about a received control port
 * frame.  It should only be used if userspace indicated it wants to receive
 * control port frames over nl80211.
 *
 * The frame is the data portion of the 802.3 or 802.11 data frame with all
 * network layer headers removed (e.g. the raw EAPoL frame).
 *
 * Return: %true if the frame was passed to userspace
 */
bool cfg80211_rx_control_port(struct net_device *dev,
			      struct sk_buff *skb, bool unencrypted);

/**
 * cfg80211_cqm_rssi_notify - connection quality monitoring rssi event
 * @dev: network device
 * @rssi_event: the triggered RSSI event
 * @rssi_level: new RSSI level value or 0 if not available
 * @gfp: context flags
 *
 * This function is called when a configured connection quality monitoring
 * rssi threshold reached event occurs.
 */
void cfg80211_cqm_rssi_notify(struct net_device *dev,
			      enum nl80211_cqm_rssi_threshold_event rssi_event,
			      s32 rssi_level, gfp_t gfp);

/**
 * cfg80211_cqm_pktloss_notify - notify userspace about packetloss to peer
 * @dev: network device
 * @peer: peer's MAC address
 * @num_packets: how many packets were lost -- should be a fixed threshold
 *	but probably no less than maybe 50, or maybe a throughput dependent
 *	threshold (to account for temporary interference)
 * @gfp: context flags
 */
void cfg80211_cqm_pktloss_notify(struct net_device *dev,
				 const u8 *peer, u32 num_packets, gfp_t gfp);

/**
 * cfg80211_cqm_txe_notify - TX error rate event
 * @dev: network device
 * @peer: peer's MAC address
 * @num_packets: how many packets were lost
 * @rate: % of packets which failed transmission
 * @intvl: interval (in s) over which the TX failure threshold was breached.
 * @gfp: context flags
 *
 * Notify userspace when configured % TX failures over number of packets in a
 * given interval is exceeded.
 */
void cfg80211_cqm_txe_notify(struct net_device *dev, const u8 *peer,
			     u32 num_packets, u32 rate, u32 intvl, gfp_t gfp);

/**
 * cfg80211_cqm_beacon_loss_notify - beacon loss event
 * @dev: network device
 * @gfp: context flags
 *
 * Notify userspace about beacon loss from the connected AP.
 */
void cfg80211_cqm_beacon_loss_notify(struct net_device *dev, gfp_t gfp);

/**
 * cfg80211_radar_event - radar detection event
 * @wiphy: the wiphy
 * @chandef: chandef for the current channel
 * @gfp: context flags
 *
 * This function is called when a radar is detected on the current chanenl.
 */
void cfg80211_radar_event(struct wiphy *wiphy,
			  struct cfg80211_chan_def *chandef, gfp_t gfp);

/**
 * cfg80211_sta_opmode_change_notify - STA's ht/vht operation mode change event
 * @dev: network device
 * @mac: MAC address of a station which opmode got modified
 * @sta_opmode: station's current opmode value
 * @gfp: context flags
 *
 * Driver should call this function when station's opmode modified via action
 * frame.
 */
void cfg80211_sta_opmode_change_notify(struct net_device *dev, const u8 *mac,
				       struct sta_opmode_info *sta_opmode,
				       gfp_t gfp);

/**
 * cfg80211_cac_event - Channel availability check (CAC) event
 * @netdev: network device
 * @chandef: chandef for the current channel
 * @event: type of event
 * @gfp: context flags
 *
 * This function is called when a Channel availability check (CAC) is finished
 * or aborted. This must be called to notify the completion of a CAC process,
 * also by full-MAC drivers.
 */
void cfg80211_cac_event(struct net_device *netdev,
			const struct cfg80211_chan_def *chandef,
			enum nl80211_radar_event event, gfp_t gfp);


/**
 * cfg80211_gtk_rekey_notify - notify userspace about driver rekeying
 * @dev: network device
 * @bssid: BSSID of AP (to avoid races)
 * @replay_ctr: new replay counter
 * @gfp: allocation flags
 */
void cfg80211_gtk_rekey_notify(struct net_device *dev, const u8 *bssid,
			       const u8 *replay_ctr, gfp_t gfp);

/**
 * cfg80211_pmksa_candidate_notify - notify about PMKSA caching candidate
 * @dev: network device
 * @index: candidate index (the smaller the index, the higher the priority)
 * @bssid: BSSID of AP
 * @preauth: Whether AP advertises support for RSN pre-authentication
 * @gfp: allocation flags
 */
void cfg80211_pmksa_candidate_notify(struct net_device *dev, int index,
				     const u8 *bssid, bool preauth, gfp_t gfp);

/**
 * cfg80211_rx_spurious_frame - inform userspace about a spurious frame
 * @dev: The device the frame matched to
 * @addr: the transmitter address
 * @gfp: context flags
 *
 * This function is used in AP mode (only!) to inform userspace that
 * a spurious class 3 frame was received, to be able to deauth the
 * sender.
 * Return: %true if the frame was passed to userspace (or this failed
 * for a reason other than not having a subscription.)
 */
bool cfg80211_rx_spurious_frame(struct net_device *dev,
				const u8 *addr, gfp_t gfp);

/**
 * cfg80211_rx_unexpected_4addr_frame - inform about unexpected WDS frame
 * @dev: The device the frame matched to
 * @addr: the transmitter address
 * @gfp: context flags
 *
 * This function is used in AP mode (only!) to inform userspace that
 * an associated station sent a 4addr frame but that wasn't expected.
 * It is allowed and desirable to send this event only once for each
 * station to avoid event flooding.
 * Return: %true if the frame was passed to userspace (or this failed
 * for a reason other than not having a subscription.)
 */
bool cfg80211_rx_unexpected_4addr_frame(struct net_device *dev,
					const u8 *addr, gfp_t gfp);

/**
 * cfg80211_probe_status - notify userspace about probe status
 * @dev: the device the probe was sent on
 * @addr: the address of the peer
 * @cookie: the cookie filled in @probe_client previously
 * @acked: indicates whether probe was acked or not
 * @ack_signal: signal strength (in dBm) of the ACK frame.
 * @is_valid_ack_signal: indicates the ack_signal is valid or not.
 * @gfp: allocation flags
 */
void cfg80211_probe_status(struct net_device *dev, const u8 *addr,
			   u64 cookie, bool acked, s32 ack_signal,
			   bool is_valid_ack_signal, gfp_t gfp);

/**
 * cfg80211_report_obss_beacon_khz - report beacon from other APs
 * @wiphy: The wiphy that received the beacon
 * @frame: the frame
 * @len: length of the frame
 * @freq: frequency the frame was received on in KHz
 * @sig_dbm: signal strength in dBm, or 0 if unknown
 *
 * Use this function to report to userspace when a beacon was
 * received. It is not useful to call this when there is no
 * netdev that is in AP/GO mode.
 */
void cfg80211_report_obss_beacon_khz(struct wiphy *wiphy, const u8 *frame,
				     size_t len, int freq, int sig_dbm);

/**
 * cfg80211_report_obss_beacon - report beacon from other APs
 * @wiphy: The wiphy that received the beacon
 * @frame: the frame
 * @len: length of the frame
 * @freq: frequency the frame was received on
 * @sig_dbm: signal strength in dBm, or 0 if unknown
 *
 * Use this function to report to userspace when a beacon was
 * received. It is not useful to call this when there is no
 * netdev that is in AP/GO mode.
 */
static inline void cfg80211_report_obss_beacon(struct wiphy *wiphy,
					       const u8 *frame, size_t len,
					       int freq, int sig_dbm)
{
	cfg80211_report_obss_beacon_khz(wiphy, frame, len, MHZ_TO_KHZ(freq),
					sig_dbm);
}

/**
 * cfg80211_reg_can_beacon - check if beaconing is allowed
 * @wiphy: the wiphy
 * @chandef: the channel definition
 * @iftype: interface type
 *
 * Return: %true if there is no secondary channel or the secondary channel(s)
 * can be used for beaconing (i.e. is not a radar channel etc.)
 */
bool cfg80211_reg_can_beacon(struct wiphy *wiphy,
			     struct cfg80211_chan_def *chandef,
			     enum nl80211_iftype iftype);

/**
 * cfg80211_reg_can_beacon_relax - check if beaconing is allowed with relaxation
 * @wiphy: the wiphy
 * @chandef: the channel definition
 * @iftype: interface type
 *
 * Return: %true if there is no secondary channel or the secondary channel(s)
 * can be used for beaconing (i.e. is not a radar channel etc.). This version
 * also checks if IR-relaxation conditions apply, to allow beaconing under
 * more permissive conditions.
 *
 * Requires the RTNL to be held.
 */
bool cfg80211_reg_can_beacon_relax(struct wiphy *wiphy,
				   struct cfg80211_chan_def *chandef,
				   enum nl80211_iftype iftype);

/*
 * cfg80211_ch_switch_notify - update wdev channel and notify userspace
 * @dev: the device which switched channels
 * @chandef: the new channel definition
 *
 * Caller must acquire wdev_lock, therefore must only be called from sleepable
 * driver context!
 */
void cfg80211_ch_switch_notify(struct net_device *dev,
			       struct cfg80211_chan_def *chandef);

/*
 * cfg80211_ch_switch_started_notify - notify channel switch start
 * @dev: the device on which the channel switch started
 * @chandef: the future channel definition
 * @count: the number of TBTTs until the channel switch happens
 *
 * Inform the userspace about the channel switch that has just
 * started, so that it can take appropriate actions (eg. starting
 * channel switch on other vifs), if necessary.
 */
void cfg80211_ch_switch_started_notify(struct net_device *dev,
				       struct cfg80211_chan_def *chandef,
				       u8 count);

/**
 * ieee80211_operating_class_to_band - convert operating class to band
 *
 * @operating_class: the operating class to convert
 * @band: band pointer to fill
 *
 * Returns %true if the conversion was successful, %false otherwise.
 */
bool ieee80211_operating_class_to_band(u8 operating_class,
				       enum nl80211_band *band);

/**
 * ieee80211_chandef_to_operating_class - convert chandef to operation class
 *
 * @chandef: the chandef to convert
 * @op_class: a pointer to the resulting operating class
 *
 * Returns %true if the conversion was successful, %false otherwise.
 */
bool ieee80211_chandef_to_operating_class(struct cfg80211_chan_def *chandef,
					  u8 *op_class);

/**
 * ieee80211_chandef_to_khz - convert chandef to frequency in KHz
 *
 * @chandef: the chandef to convert
 *
 * Returns the center frequency of chandef (1st segment) in KHz.
 */
static inline u32
ieee80211_chandef_to_khz(const struct cfg80211_chan_def *chandef)
{
	return MHZ_TO_KHZ(chandef->center_freq1) + chandef->freq1_offset;
}

/*
 * cfg80211_tdls_oper_request - request userspace to perform TDLS operation
 * @dev: the device on which the operation is requested
 * @peer: the MAC address of the peer device
 * @oper: the requested TDLS operation (NL80211_TDLS_SETUP or
 *	NL80211_TDLS_TEARDOWN)
 * @reason_code: the reason code for teardown request
 * @gfp: allocation flags
 *
 * This function is used to request userspace to perform TDLS operation that
 * requires knowledge of keys, i.e., link setup or teardown when the AP
 * connection uses encryption. This is optional mechanism for the driver to use
 * if it can automatically determine when a TDLS link could be useful (e.g.,
 * based on traffic and signal strength for a peer).
 */
void cfg80211_tdls_oper_request(struct net_device *dev, const u8 *peer,
				enum nl80211_tdls_operation oper,
				u16 reason_code, gfp_t gfp);

/*
 * cfg80211_calculate_bitrate - calculate actual bitrate (in 100Kbps units)
 * @rate: given rate_info to calculate bitrate from
 *
 * return 0 if MCS index >= 32
 */
u32 cfg80211_calculate_bitrate(struct rate_info *rate);

/**
 * cfg80211_unregister_wdev - remove the given wdev
 * @wdev: struct wireless_dev to remove
 *
 * Call this function only for wdevs that have no netdev assigned,
 * e.g. P2P Devices. It removes the device from the list so that
 * it can no longer be used. It is necessary to call this function
 * even when cfg80211 requests the removal of the interface by
 * calling the del_virtual_intf() callback. The function must also
 * be called when the driver wishes to unregister the wdev, e.g.
 * when the device is unbound from the driver.
 *
 * Requires the RTNL to be held.
 */
void cfg80211_unregister_wdev(struct wireless_dev *wdev);

/**
 * struct cfg80211_ft_event - FT Information Elements
 * @ies: FT IEs
 * @ies_len: length of the FT IE in bytes
 * @target_ap: target AP's MAC address
 * @ric_ies: RIC IE
 * @ric_ies_len: length of the RIC IE in bytes
 */
struct cfg80211_ft_event_params {
	const u8 *ies;
	size_t ies_len;
	const u8 *target_ap;
	const u8 *ric_ies;
	size_t ric_ies_len;
};

/**
 * cfg80211_ft_event - notify userspace about FT IE and RIC IE
 * @netdev: network device
 * @ft_event: IE information
 */
void cfg80211_ft_event(struct net_device *netdev,
		       struct cfg80211_ft_event_params *ft_event);

/**
 * cfg80211_get_p2p_attr - find and copy a P2P attribute from IE buffer
 * @ies: the input IE buffer
 * @len: the input length
 * @attr: the attribute ID to find
 * @buf: output buffer, can be %NULL if the data isn't needed, e.g.
 *	if the function is only called to get the needed buffer size
 * @bufsize: size of the output buffer
 *
 * The function finds a given P2P attribute in the (vendor) IEs and
 * copies its contents to the given buffer.
 *
 * Return: A negative error code (-%EILSEQ or -%ENOENT) if the data is
 * malformed or the attribute can't be found (respectively), or the
 * length of the found attribute (which can be zero).
 */
int cfg80211_get_p2p_attr(const u8 *ies, unsigned int len,
			  enum ieee80211_p2p_attr_id attr,
			  u8 *buf, unsigned int bufsize);

/**
 * ieee80211_ie_split_ric - split an IE buffer according to ordering (with RIC)
 * @ies: the IE buffer
 * @ielen: the length of the IE buffer
 * @ids: an array with element IDs that are allowed before
 *	the split. A WLAN_EID_EXTENSION value means that the next
 *	EID in the list is a sub-element of the EXTENSION IE.
 * @n_ids: the size of the element ID array
 * @after_ric: array IE types that come after the RIC element
 * @n_after_ric: size of the @after_ric array
 * @offset: offset where to start splitting in the buffer
 *
 * This function splits an IE buffer by updating the @offset
 * variable to point to the location where the buffer should be
 * split.
 *
 * It assumes that the given IE buffer is well-formed, this
 * has to be guaranteed by the caller!
 *
 * It also assumes that the IEs in the buffer are ordered
 * correctly, if not the result of using this function will not
 * be ordered correctly either, i.e. it does no reordering.
 *
 * The function returns the offset where the next part of the
 * buffer starts, which may be @ielen if the entire (remainder)
 * of the buffer should be used.
 */
size_t ieee80211_ie_split_ric(const u8 *ies, size_t ielen,
			      const u8 *ids, int n_ids,
			      const u8 *after_ric, int n_after_ric,
			      size_t offset);

/**
 * ieee80211_ie_split - split an IE buffer according to ordering
 * @ies: the IE buffer
 * @ielen: the length of the IE buffer
 * @ids: an array with element IDs that are allowed before
 *	the split. A WLAN_EID_EXTENSION value means that the next
 *	EID in the list is a sub-element of the EXTENSION IE.
 * @n_ids: the size of the element ID array
 * @offset: offset where to start splitting in the buffer
 *
 * This function splits an IE buffer by updating the @offset
 * variable to point to the location where the buffer should be
 * split.
 *
 * It assumes that the given IE buffer is well-formed, this
 * has to be guaranteed by the caller!
 *
 * It also assumes that the IEs in the buffer are ordered
 * correctly, if not the result of using this function will not
 * be ordered correctly either, i.e. it does no reordering.
 *
 * The function returns the offset where the next part of the
 * buffer starts, which may be @ielen if the entire (remainder)
 * of the buffer should be used.
 */
static inline size_t ieee80211_ie_split(const u8 *ies, size_t ielen,
					const u8 *ids, int n_ids, size_t offset)
{
	return ieee80211_ie_split_ric(ies, ielen, ids, n_ids, NULL, 0, offset);
}

/**
 * cfg80211_report_wowlan_wakeup - report wakeup from WoWLAN
 * @wdev: the wireless device reporting the wakeup
 * @wakeup: the wakeup report
 * @gfp: allocation flags
 *
 * This function reports that the given device woke up. If it
 * caused the wakeup, report the reason(s), otherwise you may
 * pass %NULL as the @wakeup parameter to advertise that something
 * else caused the wakeup.
 */
void cfg80211_report_wowlan_wakeup(struct wireless_dev *wdev,
				   struct cfg80211_wowlan_wakeup *wakeup,
				   gfp_t gfp);

/**
 * cfg80211_crit_proto_stopped() - indicate critical protocol stopped by driver.
 *
 * @wdev: the wireless device for which critical protocol is stopped.
 * @gfp: allocation flags
 *
 * This function can be called by the driver to indicate it has reverted
 * operation back to normal. One reason could be that the duration given
 * by .crit_proto_start() has expired.
 */
void cfg80211_crit_proto_stopped(struct wireless_dev *wdev, gfp_t gfp);

/**
 * ieee80211_get_num_supported_channels - get number of channels device has
 * @wiphy: the wiphy
 *
 * Return: the number of channels supported by the device.
 */
unsigned int ieee80211_get_num_supported_channels(struct wiphy *wiphy);

/**
 * cfg80211_check_combinations - check interface combinations
 *
 * @wiphy: the wiphy
 * @params: the interface combinations parameter
 *
 * This function can be called by the driver to check whether a
 * combination of interfaces and their types are allowed according to
 * the interface combinations.
 */
int cfg80211_check_combinations(struct wiphy *wiphy,
				struct iface_combination_params *params);

/**
 * cfg80211_iter_combinations - iterate over matching combinations
 *
 * @wiphy: the wiphy
 * @params: the interface combinations parameter
 * @iter: function to call for each matching combination
 * @data: pointer to pass to iter function
 *
 * This function can be called by the driver to check what possible
 * combinations it fits in at a given moment, e.g. for channel switching
 * purposes.
 */
int cfg80211_iter_combinations(struct wiphy *wiphy,
			       struct iface_combination_params *params,
			       void (*iter)(const struct ieee80211_iface_combination *c,
					    void *data),
			       void *data);

/*
 * cfg80211_stop_iface - trigger interface disconnection
 *
 * @wiphy: the wiphy
 * @wdev: wireless device
 * @gfp: context flags
 *
 * Trigger interface to be stopped as if AP was stopped, IBSS/mesh left, STA
 * disconnected.
 *
 * Note: This doesn't need any locks and is asynchronous.
 */
void cfg80211_stop_iface(struct wiphy *wiphy, struct wireless_dev *wdev,
			 gfp_t gfp);

/**
 * cfg80211_shutdown_all_interfaces - shut down all interfaces for a wiphy
 * @wiphy: the wiphy to shut down
 *
 * This function shuts down all interfaces belonging to this wiphy by
 * calling dev_close() (and treating non-netdev interfaces as needed).
 * It shouldn't really be used unless there are some fatal device errors
 * that really can't be recovered in any other way.
 *
 * Callers must hold the RTNL and be able to deal with callbacks into
 * the driver while the function is running.
 */
void cfg80211_shutdown_all_interfaces(struct wiphy *wiphy);

/**
 * wiphy_ext_feature_set - set the extended feature flag
 *
 * @wiphy: the wiphy to modify.
 * @ftidx: extended feature bit index.
 *
 * The extended features are flagged in multiple bytes (see
 * &struct wiphy.@ext_features)
 */
static inline void wiphy_ext_feature_set(struct wiphy *wiphy,
					 enum nl80211_ext_feature_index ftidx)
{
	u8 *ft_byte;

	ft_byte = &wiphy->ext_features[ftidx / 8];
	*ft_byte |= BIT(ftidx % 8);
}

/**
 * wiphy_ext_feature_isset - check the extended feature flag
 *
 * @wiphy: the wiphy to modify.
 * @ftidx: extended feature bit index.
 *
 * The extended features are flagged in multiple bytes (see
 * &struct wiphy.@ext_features)
 */
static inline bool
wiphy_ext_feature_isset(struct wiphy *wiphy,
			enum nl80211_ext_feature_index ftidx)
{
	u8 ft_byte;

	ft_byte = wiphy->ext_features[ftidx / 8];
	return (ft_byte & BIT(ftidx % 8)) != 0;
}

/**
 * cfg80211_free_nan_func - free NAN function
 * @f: NAN function that should be freed
 *
 * Frees all the NAN function and all it's allocated members.
 */
void cfg80211_free_nan_func(struct cfg80211_nan_func *f);

/**
 * struct cfg80211_nan_match_params - NAN match parameters
 * @type: the type of the function that triggered a match. If it is
 *	 %NL80211_NAN_FUNC_SUBSCRIBE it means that we replied to a subscriber.
 *	 If it is %NL80211_NAN_FUNC_PUBLISH, it means that we got a discovery
 *	 result.
 *	 If it is %NL80211_NAN_FUNC_FOLLOW_UP, we received a follow up.
 * @inst_id: the local instance id
 * @peer_inst_id: the instance id of the peer's function
 * @addr: the MAC address of the peer
 * @info_len: the length of the &info
 * @info: the Service Specific Info from the peer (if any)
 * @cookie: unique identifier of the corresponding function
 */
struct cfg80211_nan_match_params {
	enum nl80211_nan_function_type type;
	u8 inst_id;
	u8 peer_inst_id;
	const u8 *addr;
	u8 info_len;
	const u8 *info;
	u64 cookie;
};

/**
 * cfg80211_nan_match - report a match for a NAN function.
 * @wdev: the wireless device reporting the match
 * @match: match notification parameters
 * @gfp: allocation flags
 *
 * This function reports that the a NAN function had a match. This
 * can be a subscribe that had a match or a solicited publish that
 * was sent. It can also be a follow up that was received.
 */
void cfg80211_nan_match(struct wireless_dev *wdev,
			struct cfg80211_nan_match_params *match, gfp_t gfp);

/**
 * cfg80211_nan_func_terminated - notify about NAN function termination.
 *
 * @wdev: the wireless device reporting the match
 * @inst_id: the local instance id
 * @reason: termination reason (one of the NL80211_NAN_FUNC_TERM_REASON_*)
 * @cookie: unique NAN function identifier
 * @gfp: allocation flags
 *
 * This function reports that the a NAN function is terminated.
 */
void cfg80211_nan_func_terminated(struct wireless_dev *wdev,
				  u8 inst_id,
				  enum nl80211_nan_func_term_reason reason,
				  u64 cookie, gfp_t gfp);

/* ethtool helper */
void cfg80211_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info);

/**
 * cfg80211_external_auth_request - userspace request for authentication
 * @netdev: network device
 * @params: External authentication parameters
 * @gfp: allocation flags
 * Returns: 0 on success, < 0 on error
 */
int cfg80211_external_auth_request(struct net_device *netdev,
				   struct cfg80211_external_auth_params *params,
				   gfp_t gfp);

/**
 * cfg80211_pmsr_report - report peer measurement result data
 * @wdev: the wireless device reporting the measurement
 * @req: the original measurement request
 * @result: the result data
 * @gfp: allocation flags
 */
void cfg80211_pmsr_report(struct wireless_dev *wdev,
			  struct cfg80211_pmsr_request *req,
			  struct cfg80211_pmsr_result *result,
			  gfp_t gfp);

/**
 * cfg80211_pmsr_complete - report peer measurement completed
 * @wdev: the wireless device reporting the measurement
 * @req: the original measurement request
 * @gfp: allocation flags
 *
 * Report that the entire measurement completed, after this
 * the request pointer will no longer be valid.
 */
void cfg80211_pmsr_complete(struct wireless_dev *wdev,
			    struct cfg80211_pmsr_request *req,
			    gfp_t gfp);

/**
 * cfg80211_iftype_allowed - check whether the interface can be allowed
 * @wiphy: the wiphy
 * @iftype: interface type
 * @is_4addr: use_4addr flag, must be '0' when check_swif is '1'
 * @check_swif: check iftype against software interfaces
 *
 * Check whether the interface is allowed to operate; additionally, this API
 * can be used to check iftype against the software interfaces when
 * check_swif is '1'.
 */
bool cfg80211_iftype_allowed(struct wiphy *wiphy, enum nl80211_iftype iftype,
			     bool is_4addr, u8 check_swif);


/* Logging, debugging and troubleshooting/diagnostic helpers. */

/* wiphy_printk helpers, similar to dev_printk */

#define wiphy_printk(level, wiphy, format, args...)		\
	dev_printk(level, &(wiphy)->dev, format, ##args)
#define wiphy_emerg(wiphy, format, args...)			\
	dev_emerg(&(wiphy)->dev, format, ##args)
#define wiphy_alert(wiphy, format, args...)			\
	dev_alert(&(wiphy)->dev, format, ##args)
#define wiphy_crit(wiphy, format, args...)			\
	dev_crit(&(wiphy)->dev, format, ##args)
#define wiphy_err(wiphy, format, args...)			\
	dev_err(&(wiphy)->dev, format, ##args)
#define wiphy_warn(wiphy, format, args...)			\
	dev_warn(&(wiphy)->dev, format, ##args)
#define wiphy_notice(wiphy, format, args...)			\
	dev_notice(&(wiphy)->dev, format, ##args)
#define wiphy_info(wiphy, format, args...)			\
	dev_info(&(wiphy)->dev, format, ##args)

#define wiphy_err_ratelimited(wiphy, format, args...)		\
	dev_err_ratelimited(&(wiphy)->dev, format, ##args)
#define wiphy_warn_ratelimited(wiphy, format, args...)		\
	dev_warn_ratelimited(&(wiphy)->dev, format, ##args)

#define wiphy_debug(wiphy, format, args...)			\
	wiphy_printk(KERN_DEBUG, wiphy, format, ##args)

#define wiphy_dbg(wiphy, format, args...)			\
	dev_dbg(&(wiphy)->dev, format, ##args)

#if defined(VERBOSE_DEBUG)
#define wiphy_vdbg	wiphy_dbg
#else
#define wiphy_vdbg(wiphy, format, args...)				\
({									\
	if (0)								\
		wiphy_printk(KERN_DEBUG, wiphy, format, ##args);	\
	0;								\
})
#endif

/*
 * wiphy_WARN() acts like wiphy_printk(), but with the key difference
 * of using a WARN/WARN_ON to get the message out, including the
 * file/line information and a backtrace.
 */
#define wiphy_WARN(wiphy, format, args...)			\
	WARN(1, "wiphy: %s\n" format, wiphy_name(wiphy), ##args);

/**
 * cfg80211_update_owe_info_event - Notify the peer's OWE info to user space
 * @netdev: network device
 * @owe_info: peer's owe info
 * @gfp: allocation flags
 */
void cfg80211_update_owe_info_event(struct net_device *netdev,
				    struct cfg80211_update_owe_info *owe_info,
				    gfp_t gfp);

#endif /* __NET_CFG80211_H */
