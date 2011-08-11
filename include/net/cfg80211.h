#ifndef __NET_CFG80211_H
#define __NET_CFG80211_H
/*
 * 802.11 device and configuration interface
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/netdevice.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/nl80211.h>
#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <net/regulatory.h>

/* remove once we remove the wext stuff */
#include <net/iw_handler.h>
#include <linux/wireless.h>


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

/*
 * wireless hardware capability structures
 */

/**
 * enum ieee80211_band - supported frequency bands
 *
 * The bands are assigned this way because the supported
 * bitrates differ in these bands.
 *
 * @IEEE80211_BAND_2GHZ: 2.4GHz ISM band
 * @IEEE80211_BAND_5GHZ: around 5GHz band (4.9-5.7)
 * @IEEE80211_NUM_BANDS: number of defined bands
 */
enum ieee80211_band {
	IEEE80211_BAND_2GHZ = NL80211_BAND_2GHZ,
	IEEE80211_BAND_5GHZ = NL80211_BAND_5GHZ,

	/* keep last */
	IEEE80211_NUM_BANDS
};

/**
 * enum ieee80211_channel_flags - channel flags
 *
 * Channel flags set by the regulatory control code.
 *
 * @IEEE80211_CHAN_DISABLED: This channel is disabled.
 * @IEEE80211_CHAN_PASSIVE_SCAN: Only passive scanning is permitted
 *	on this channel.
 * @IEEE80211_CHAN_NO_IBSS: IBSS is not allowed on this channel.
 * @IEEE80211_CHAN_RADAR: Radar detection is required on this channel.
 * @IEEE80211_CHAN_NO_HT40PLUS: extension channel above this channel
 * 	is not permitted.
 * @IEEE80211_CHAN_NO_HT40MINUS: extension channel below this channel
 * 	is not permitted.
 */
enum ieee80211_channel_flags {
	IEEE80211_CHAN_DISABLED		= 1<<0,
	IEEE80211_CHAN_PASSIVE_SCAN	= 1<<1,
	IEEE80211_CHAN_NO_IBSS		= 1<<2,
	IEEE80211_CHAN_RADAR		= 1<<3,
	IEEE80211_CHAN_NO_HT40PLUS	= 1<<4,
	IEEE80211_CHAN_NO_HT40MINUS	= 1<<5,
};

#define IEEE80211_CHAN_NO_HT40 \
	(IEEE80211_CHAN_NO_HT40PLUS | IEEE80211_CHAN_NO_HT40MINUS)

/**
 * struct ieee80211_channel - channel definition
 *
 * This structure describes a single channel for use
 * with cfg80211.
 *
 * @center_freq: center frequency in MHz
 * @hw_value: hardware-specific value for the channel
 * @flags: channel flags from &enum ieee80211_channel_flags.
 * @orig_flags: channel flags at registration time, used by regulatory
 *	code to support devices with additional restrictions
 * @band: band this channel belongs to.
 * @max_antenna_gain: maximum antenna gain in dBi
 * @max_power: maximum transmission power (in dBm)
 * @beacon_found: helper to regulatory code to indicate when a beacon
 *	has been found on this channel. Use regulatory_hint_found_beacon()
 *	to enable this, this is useful only on 5 GHz band.
 * @orig_mag: internal use
 * @orig_mpwr: internal use
 */
struct ieee80211_channel {
	enum ieee80211_band band;
	u16 center_freq;
	u16 hw_value;
	u32 flags;
	int max_antenna_gain;
	int max_power;
	bool beacon_found;
	u32 orig_flags;
	int orig_mag, orig_mpwr;
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
 */
enum ieee80211_rate_flags {
	IEEE80211_RATE_SHORT_PREAMBLE	= 1<<0,
	IEEE80211_RATE_MANDATORY_A	= 1<<1,
	IEEE80211_RATE_MANDATORY_B	= 1<<2,
	IEEE80211_RATE_MANDATORY_G	= 1<<3,
	IEEE80211_RATE_ERP_G		= 1<<4,
};

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
 */
struct ieee80211_supported_band {
	struct ieee80211_channel *channels;
	struct ieee80211_rate *bitrates;
	enum ieee80211_band band;
	int n_channels;
	int n_bitrates;
	struct ieee80211_sta_ht_cap ht_cap;
};

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

/**
 * struct vif_params - describes virtual interface parameters
 * @use_4addr: use 4-address frames
 */
struct vif_params {
       int use_4addr;
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
 */
struct key_params {
	u8 *key;
	u8 *seq;
	int key_len;
	int seq_len;
	u32 cipher;
};

/**
 * enum survey_info_flags - survey information flags
 *
 * @SURVEY_INFO_NOISE_DBM: noise (in dBm) was filled in
 * @SURVEY_INFO_IN_USE: channel is currently being used
 * @SURVEY_INFO_CHANNEL_TIME: channel active time (in ms) was filled in
 * @SURVEY_INFO_CHANNEL_TIME_BUSY: channel busy time was filled in
 * @SURVEY_INFO_CHANNEL_TIME_EXT_BUSY: extension channel busy time was filled in
 * @SURVEY_INFO_CHANNEL_TIME_RX: channel receive time was filled in
 * @SURVEY_INFO_CHANNEL_TIME_TX: channel transmit time was filled in
 *
 * Used by the driver to indicate which info in &struct survey_info
 * it has filled in during the get_survey().
 */
enum survey_info_flags {
	SURVEY_INFO_NOISE_DBM = 1<<0,
	SURVEY_INFO_IN_USE = 1<<1,
	SURVEY_INFO_CHANNEL_TIME = 1<<2,
	SURVEY_INFO_CHANNEL_TIME_BUSY = 1<<3,
	SURVEY_INFO_CHANNEL_TIME_EXT_BUSY = 1<<4,
	SURVEY_INFO_CHANNEL_TIME_RX = 1<<5,
	SURVEY_INFO_CHANNEL_TIME_TX = 1<<6,
};

/**
 * struct survey_info - channel survey response
 *
 * @channel: the channel this survey record reports, mandatory
 * @filled: bitflag of flags from &enum survey_info_flags
 * @noise: channel noise in dBm. This and all following fields are
 *     optional
 * @channel_time: amount of time in ms the radio spent on the channel
 * @channel_time_busy: amount of time the primary channel was sensed busy
 * @channel_time_ext_busy: amount of time the extension channel was sensed busy
 * @channel_time_rx: amount of time the radio spent receiving data
 * @channel_time_tx: amount of time the radio spent transmitting data
 *
 * Used by dump_survey() to report back per-channel survey information.
 *
 * This structure can later be expanded with things like
 * channel duty cycle etc.
 */
struct survey_info {
	struct ieee80211_channel *channel;
	u64 channel_time;
	u64 channel_time_busy;
	u64 channel_time_ext_busy;
	u64 channel_time_rx;
	u64 channel_time_tx;
	u32 filled;
	s8 noise;
};

/**
 * struct beacon_parameters - beacon parameters
 *
 * Used to configure the beacon for an interface.
 *
 * @head: head portion of beacon (before TIM IE)
 *     or %NULL if not changed
 * @tail: tail portion of beacon (after TIM IE)
 *     or %NULL if not changed
 * @interval: beacon interval or zero if not changed
 * @dtim_period: DTIM period or zero if not changed
 * @head_len: length of @head
 * @tail_len: length of @tail
 */
struct beacon_parameters {
	u8 *head, *tail;
	int interval, dtim_period;
	int head_len, tail_len;
};

/**
 * enum plink_action - actions to perform in mesh peers
 *
 * @PLINK_ACTION_INVALID: action 0 is reserved
 * @PLINK_ACTION_OPEN: start mesh peer link establishment
 * @PLINK_ACTION_BLOCK: block traffic from this mesh peer
 */
enum plink_actions {
	PLINK_ACTION_INVALID,
	PLINK_ACTION_OPEN,
	PLINK_ACTION_BLOCK,
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
 *	(bitmask of BIT(NL80211_STA_FLAG_...))
 * @sta_flags_set: station flags values
 *	(bitmask of BIT(NL80211_STA_FLAG_...))
 * @listen_interval: listen interval or -1 for no change
 * @aid: AID or zero for no change
 * @plink_action: plink action to take
 * @plink_state: set the peer link state for a station
 * @ht_capa: HT capabilities of station
 */
struct station_parameters {
	u8 *supported_rates;
	struct net_device *vlan;
	u32 sta_flags_mask, sta_flags_set;
	int listen_interval;
	u16 aid;
	u8 supported_rates_len;
	u8 plink_action;
	u8 plink_state;
	struct ieee80211_ht_cap *ht_capa;
};

/**
 * enum station_info_flags - station information flags
 *
 * Used by the driver to indicate which info in &struct station_info
 * it has filled in during get_station() or dump_station().
 *
 * @STATION_INFO_INACTIVE_TIME: @inactive_time filled
 * @STATION_INFO_RX_BYTES: @rx_bytes filled
 * @STATION_INFO_TX_BYTES: @tx_bytes filled
 * @STATION_INFO_LLID: @llid filled
 * @STATION_INFO_PLID: @plid filled
 * @STATION_INFO_PLINK_STATE: @plink_state filled
 * @STATION_INFO_SIGNAL: @signal filled
 * @STATION_INFO_TX_BITRATE: @txrate fields are filled
 *  (tx_bitrate, tx_bitrate_flags and tx_bitrate_mcs)
 * @STATION_INFO_RX_PACKETS: @rx_packets filled
 * @STATION_INFO_TX_PACKETS: @tx_packets filled
 * @STATION_INFO_TX_RETRIES: @tx_retries filled
 * @STATION_INFO_TX_FAILED: @tx_failed filled
 * @STATION_INFO_RX_DROP_MISC: @rx_dropped_misc filled
 * @STATION_INFO_SIGNAL_AVG: @signal_avg filled
 * @STATION_INFO_RX_BITRATE: @rxrate fields are filled
 * @STATION_INFO_BSS_PARAM: @bss_param filled
 * @STATION_INFO_CONNECTED_TIME: @connected_time filled
 * @STATION_INFO_ASSOC_REQ_IES: @assoc_req_ies filled
 */
enum station_info_flags {
	STATION_INFO_INACTIVE_TIME	= 1<<0,
	STATION_INFO_RX_BYTES		= 1<<1,
	STATION_INFO_TX_BYTES		= 1<<2,
	STATION_INFO_LLID		= 1<<3,
	STATION_INFO_PLID		= 1<<4,
	STATION_INFO_PLINK_STATE	= 1<<5,
	STATION_INFO_SIGNAL		= 1<<6,
	STATION_INFO_TX_BITRATE		= 1<<7,
	STATION_INFO_RX_PACKETS		= 1<<8,
	STATION_INFO_TX_PACKETS		= 1<<9,
	STATION_INFO_TX_RETRIES		= 1<<10,
	STATION_INFO_TX_FAILED		= 1<<11,
	STATION_INFO_RX_DROP_MISC	= 1<<12,
	STATION_INFO_SIGNAL_AVG		= 1<<13,
	STATION_INFO_RX_BITRATE		= 1<<14,
	STATION_INFO_BSS_PARAM          = 1<<15,
	STATION_INFO_CONNECTED_TIME	= 1<<16,
	STATION_INFO_ASSOC_REQ_IES	= 1<<17
};

/**
 * enum station_info_rate_flags - bitrate info flags
 *
 * Used by the driver to indicate the specific rate transmission
 * type for 802.11n transmissions.
 *
 * @RATE_INFO_FLAGS_MCS: @tx_bitrate_mcs filled
 * @RATE_INFO_FLAGS_40_MHZ_WIDTH: 40 Mhz width transmission
 * @RATE_INFO_FLAGS_SHORT_GI: 400ns guard interval
 */
enum rate_info_flags {
	RATE_INFO_FLAGS_MCS		= 1<<0,
	RATE_INFO_FLAGS_40_MHZ_WIDTH	= 1<<1,
	RATE_INFO_FLAGS_SHORT_GI	= 1<<2,
};

/**
 * struct rate_info - bitrate information
 *
 * Information about a receiving or transmitting bitrate
 *
 * @flags: bitflag of flags from &enum rate_info_flags
 * @mcs: mcs index if struct describes a 802.11n bitrate
 * @legacy: bitrate in 100kbit/s for 802.11abg
 */
struct rate_info {
	u8 flags;
	u8 mcs;
	u16 legacy;
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
 * struct station_info - station information
 *
 * Station information filled by driver for get_station() and dump_station.
 *
 * @filled: bitflag of flags from &enum station_info_flags
 * @connected_time: time(in secs) since a station is last connected
 * @inactive_time: time since last station activity (tx/rx) in milliseconds
 * @rx_bytes: bytes received from this station
 * @tx_bytes: bytes transmitted to this station
 * @llid: mesh local link id
 * @plid: mesh peer link id
 * @plink_state: mesh peer link state
 * @signal: signal strength of last received packet in dBm
 * @signal_avg: signal strength average in dBm
 * @txrate: current unicast bitrate from this station
 * @rxrate: current unicast bitrate to this station
 * @rx_packets: packets received from this station
 * @tx_packets: packets transmitted to this station
 * @tx_retries: cumulative retry counts
 * @tx_failed: number of failed transmissions (retries exceeded, no ACK)
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
 */
struct station_info {
	u32 filled;
	u32 connected_time;
	u32 inactive_time;
	u32 rx_bytes;
	u32 tx_bytes;
	u16 llid;
	u16 plid;
	u8 plink_state;
	s8 signal;
	s8 signal_avg;
	struct rate_info txrate;
	struct rate_info rxrate;
	u32 rx_packets;
	u32 tx_packets;
	u32 tx_retries;
	u32 tx_failed;
	u32 rx_dropped_misc;
	struct sta_bss_parameters bss_param;

	int generation;

	const u8 *assoc_req_ies;
	size_t assoc_req_ies_len;

	/*
	 * Note: Add a new enum station_info_flags value for each new field and
	 * use it to check which fields are initialized.
	 */
};

/**
 * enum monitor_flags - monitor flags
 *
 * Monitor interface configuration flags. Note that these must be the bits
 * according to the nl80211 flags.
 *
 * @MONITOR_FLAG_FCSFAIL: pass frames with bad FCS
 * @MONITOR_FLAG_PLCPFAIL: pass frames with bad PLCP
 * @MONITOR_FLAG_CONTROL: pass control frames
 * @MONITOR_FLAG_OTHER_BSS: disable BSSID filtering
 * @MONITOR_FLAG_COOK_FRAMES: report frames after processing
 */
enum monitor_flags {
	MONITOR_FLAG_FCSFAIL		= 1<<NL80211_MNTR_FLAG_FCSFAIL,
	MONITOR_FLAG_PLCPFAIL		= 1<<NL80211_MNTR_FLAG_PLCPFAIL,
	MONITOR_FLAG_CONTROL		= 1<<NL80211_MNTR_FLAG_CONTROL,
	MONITOR_FLAG_OTHER_BSS		= 1<<NL80211_MNTR_FLAG_OTHER_BSS,
	MONITOR_FLAG_COOK_FRAMES	= 1<<NL80211_MNTR_FLAG_COOK_FRAMES,
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
 */
enum mpath_info_flags {
	MPATH_INFO_FRAME_QLEN		= BIT(0),
	MPATH_INFO_SN			= BIT(1),
	MPATH_INFO_METRIC		= BIT(2),
	MPATH_INFO_EXPTIME		= BIT(3),
	MPATH_INFO_DISCOVERY_TIMEOUT	= BIT(4),
	MPATH_INFO_DISCOVERY_RETRIES	= BIT(5),
	MPATH_INFO_FLAGS		= BIT(6),
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
 * 	(u16 = opmode, -1 = do not change)
 */
struct bss_parameters {
	int use_cts_prot;
	int use_short_preamble;
	int use_short_slot_time;
	u8 *basic_rates;
	u8 basic_rates_len;
	int ap_isolate;
	int ht_opmode;
};

/*
 * struct mesh_config - 802.11s mesh configuration
 *
 * These parameters can be changed while the mesh is active.
 */
struct mesh_config {
	/* Timeouts in ms */
	/* Mesh plink management parameters */
	u16 dot11MeshRetryTimeout;
	u16 dot11MeshConfirmTimeout;
	u16 dot11MeshHoldingTimeout;
	u16 dot11MeshMaxPeerLinks;
	u8  dot11MeshMaxRetries;
	u8  dot11MeshTTL;
	/* ttl used in path selection information elements */
	u8  element_ttl;
	bool auto_open_plinks;
	/* HWMP parameters */
	u8  dot11MeshHWMPmaxPREQretries;
	u32 path_refresh_time;
	u16 min_discovery_timeout;
	u32 dot11MeshHWMPactivePathTimeout;
	u16 dot11MeshHWMPpreqMinInterval;
	u16 dot11MeshHWMPnetDiameterTraversalTime;
	u8  dot11MeshHWMPRootMode;
};

/**
 * struct mesh_setup - 802.11s mesh setup configuration
 * @mesh_id: the mesh ID
 * @mesh_id_len: length of the mesh ID, at least 1 and at most 32 bytes
 * @path_sel_proto: which path selection protocol to use
 * @path_metric: which metric to use
 * @ie: vendor information elements (optional)
 * @ie_len: length of vendor information elements
 * @is_authenticated: this mesh requires authentication
 * @is_secure: this mesh uses security
 *
 * These parameters are fixed when the mesh is created.
 */
struct mesh_setup {
	const u8 *mesh_id;
	u8 mesh_id_len;
	u8  path_sel_proto;
	u8  path_metric;
	const u8 *ie;
	u8 ie_len;
	bool is_authenticated;
	bool is_secure;
};

/**
 * struct ieee80211_txq_params - TX queue parameters
 * @queue: TX queue identifier (NL80211_TXQ_Q_*)
 * @txop: Maximum burst time in units of 32 usecs, 0 meaning disabled
 * @cwmin: Minimum contention window [a value of the form 2^n-1 in the range
 *	1..32767]
 * @cwmax: Maximum contention window [a value of the form 2^n-1 in the range
 *	1..32767]
 * @aifs: Arbitration interframe space [0..255]
 */
struct ieee80211_txq_params {
	enum nl80211_txq_q queue;
	u16 txop;
	u16 cwmin;
	u16 cwmax;
	u8 aifs;
};

/* from net/wireless.h */
struct wiphy;

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
 * struct cfg80211_scan_request - scan request description
 *
 * @ssids: SSIDs to scan for (active scan only)
 * @n_ssids: number of SSIDs
 * @channels: channels to scan on.
 * @n_channels: total number of channels to scan
 * @ie: optional information element(s) to add into Probe Request or %NULL
 * @ie_len: length of ie in octets
 * @wiphy: the wiphy this was for
 * @dev: the interface
 * @aborted: (internal) scan request was notified as aborted
 */
struct cfg80211_scan_request {
	struct cfg80211_ssid *ssids;
	int n_ssids;
	u32 n_channels;
	const u8 *ie;
	size_t ie_len;

	/* internal */
	struct wiphy *wiphy;
	struct net_device *dev;
	bool aborted;

	/* keep last */
	struct ieee80211_channel *channels[0];
};

/**
 * struct cfg80211_sched_scan_request - scheduled scan request description
 *
 * @ssids: SSIDs to scan for (passed in the probe_reqs in active scans)
 * @n_ssids: number of SSIDs
 * @n_channels: total number of channels to scan
 * @interval: interval between each scheduled scan cycle
 * @ie: optional information element(s) to add into Probe Request or %NULL
 * @ie_len: length of ie in octets
 * @wiphy: the wiphy this was for
 * @dev: the interface
 * @channels: channels to scan
 */
struct cfg80211_sched_scan_request {
	struct cfg80211_ssid *ssids;
	int n_ssids;
	u32 n_channels;
	u32 interval;
	const u8 *ie;
	size_t ie_len;

	/* internal */
	struct wiphy *wiphy;
	struct net_device *dev;

	/* keep last */
	struct ieee80211_channel *channels[0];
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
 * struct cfg80211_bss - BSS description
 *
 * This structure describes a BSS (which may also be a mesh network)
 * for use in scan results and similar.
 *
 * @channel: channel this BSS is on
 * @bssid: BSSID of the BSS
 * @tsf: timestamp of last received update
 * @beacon_interval: the beacon interval as from the frame
 * @capability: the capability field in host byte order
 * @information_elements: the information elements (Note that there
 *	is no guarantee that these are well-formed!); this is a pointer to
 *	either the beacon_ies or proberesp_ies depending on whether Probe
 *	Response frame has been received
 * @len_information_elements: total length of the information elements
 * @beacon_ies: the information elements from the last Beacon frame
 * @len_beacon_ies: total length of the beacon_ies
 * @proberesp_ies: the information elements from the last Probe Response frame
 * @len_proberesp_ies: total length of the proberesp_ies
 * @signal: signal strength value (type depends on the wiphy's signal_type)
 * @free_priv: function pointer to free private data
 * @priv: private area for driver use, has at least wiphy->bss_priv_size bytes
 */
struct cfg80211_bss {
	struct ieee80211_channel *channel;

	u8 bssid[ETH_ALEN];
	u64 tsf;
	u16 beacon_interval;
	u16 capability;
	u8 *information_elements;
	size_t len_information_elements;
	u8 *beacon_ies;
	size_t len_beacon_ies;
	u8 *proberesp_ies;
	size_t len_proberesp_ies;

	s32 signal;

	void (*free_priv)(struct cfg80211_bss *bss);
	u8 priv[0] __attribute__((__aligned__(sizeof(void *))));
};

/**
 * ieee80211_bss_get_ie - find IE with given ID
 * @bss: the bss to search
 * @ie: the IE ID
 * Returns %NULL if not found.
 */
const u8 *ieee80211_bss_get_ie(struct cfg80211_bss *bss, u8 ie);


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
};

/**
 * struct cfg80211_auth_request - Authentication request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * authentication.
 *
 * @bss: The BSS to authenticate with.
 * @auth_type: Authentication type (algorithm)
 * @ie: Extra IEs to add to Authentication frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @key_len: length of WEP key for shared key authentication
 * @key_idx: index of WEP key for shared key authentication
 * @key: WEP key for shared key authentication
 * @local_state_change: This is a request for a local state only, i.e., no
 *	Authentication frame is to be transmitted and authentication state is
 *	to be changed without having to wait for a response from the peer STA
 *	(AP).
 */
struct cfg80211_auth_request {
	struct cfg80211_bss *bss;
	const u8 *ie;
	size_t ie_len;
	enum nl80211_auth_type auth_type;
	const u8 *key;
	u8 key_len, key_idx;
	bool local_state_change;
};

/**
 * struct cfg80211_assoc_request - (Re)Association request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * (re)association.
 * @bss: The BSS to associate with.
 * @ie: Extra IEs to add to (Re)Association Request frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @use_mfp: Use management frame protection (IEEE 802.11w) in this association
 * @crypto: crypto settings
 * @prev_bssid: previous BSSID, if not %NULL use reassociate frame
 */
struct cfg80211_assoc_request {
	struct cfg80211_bss *bss;
	const u8 *ie, *prev_bssid;
	size_t ie_len;
	struct cfg80211_crypto_settings crypto;
	bool use_mfp;
};

/**
 * struct cfg80211_deauth_request - Deauthentication request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * deauthentication.
 *
 * @bss: the BSS to deauthenticate from
 * @ie: Extra IEs to add to Deauthentication frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @reason_code: The reason code for the deauthentication
 * @local_state_change: This is a request for a local state only, i.e., no
 *	Deauthentication frame is to be transmitted.
 */
struct cfg80211_deauth_request {
	struct cfg80211_bss *bss;
	const u8 *ie;
	size_t ie_len;
	u16 reason_code;
	bool local_state_change;
};

/**
 * struct cfg80211_disassoc_request - Disassociation request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * disassocation.
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
 * @channel: The channel to use if no IBSS can be found to join.
 * @channel_fixed: The channel should be fixed -- do not search for
 *	IBSSs to join on other channels.
 * @ie: information element(s) to include in the beacon
 * @ie_len: length of that
 * @beacon_interval: beacon interval to use
 * @privacy: this is a protected network, keys will be configured
 *	after joining
 * @basic_rates: bitmap of basic rates to use when creating the IBSS
 * @mcast_rate: per-band multicast rate index + 1 (0: disabled)
 */
struct cfg80211_ibss_params {
	u8 *ssid;
	u8 *bssid;
	struct ieee80211_channel *channel;
	u8 *ie;
	u8 ssid_len, ie_len;
	u16 beacon_interval;
	u32 basic_rates;
	bool channel_fixed;
	bool privacy;
	int mcast_rate[IEEE80211_NUM_BANDS];
};

/**
 * struct cfg80211_connect_params - Connection parameters
 *
 * This structure provides information needed to complete IEEE 802.11
 * authentication and association.
 *
 * @channel: The channel to use or %NULL if not specified (auto-select based
 *	on scan results)
 * @bssid: The AP BSSID or %NULL if not specified (auto-select based on scan
 *	results)
 * @ssid: SSID
 * @ssid_len: Length of ssid in octets
 * @auth_type: Authentication type (algorithm)
 * @ie: IEs for association request
 * @ie_len: Length of assoc_ie in octets
 * @privacy: indicates whether privacy-enabled APs should be used
 * @crypto: crypto settings
 * @key_len: length of WEP key for shared key authentication
 * @key_idx: index of WEP key for shared key authentication
 * @key: WEP key for shared key authentication
 */
struct cfg80211_connect_params {
	struct ieee80211_channel *channel;
	u8 *bssid;
	u8 *ssid;
	size_t ssid_len;
	enum nl80211_auth_type auth_type;
	u8 *ie;
	size_t ie_len;
	bool privacy;
	struct cfg80211_crypto_settings crypto;
	const u8 *key;
	u8 key_len, key_idx;
};

/**
 * enum wiphy_params_flags - set_wiphy_params bitfield values
 * @WIPHY_PARAM_RETRY_SHORT: wiphy->retry_short has changed
 * @WIPHY_PARAM_RETRY_LONG: wiphy->retry_long has changed
 * @WIPHY_PARAM_FRAG_THRESHOLD: wiphy->frag_threshold has changed
 * @WIPHY_PARAM_RTS_THRESHOLD: wiphy->rts_threshold has changed
 * @WIPHY_PARAM_COVERAGE_CLASS: coverage class changed
 */
enum wiphy_params_flags {
	WIPHY_PARAM_RETRY_SHORT		= 1 << 0,
	WIPHY_PARAM_RETRY_LONG		= 1 << 1,
	WIPHY_PARAM_FRAG_THRESHOLD	= 1 << 2,
	WIPHY_PARAM_RTS_THRESHOLD	= 1 << 3,
	WIPHY_PARAM_COVERAGE_CLASS	= 1 << 4,
};

/*
 * cfg80211_bitrate_mask - masks for bitrate control
 */
struct cfg80211_bitrate_mask {
	struct {
		u32 legacy;
		/* TODO: add support for masking MCS rates; e.g.: */
		/* u8 mcs[IEEE80211_HT_MCS_MASK_LEN]; */
	} control[IEEE80211_NUM_BANDS];
};
/**
 * struct cfg80211_pmksa - PMK Security Association
 *
 * This structure is passed to the set/del_pmksa() method for PMKSA
 * caching.
 *
 * @bssid: The AP's BSSID.
 * @pmkid: The PMK material itself.
 */
struct cfg80211_pmksa {
	u8 *bssid;
	u8 *pmkid;
};

/**
 * struct cfg80211_wowlan_trig_pkt_pattern - packet pattern
 * @mask: bitmask where to match pattern and where to ignore bytes,
 *	one bit per byte, in same format as nl80211
 * @pattern: bytes to match where bitmask is 1
 * @pattern_len: length of pattern (in bytes)
 *
 * Internal note: @mask and @pattern are allocated in one chunk of
 * memory, free @mask only!
 */
struct cfg80211_wowlan_trig_pkt_pattern {
	u8 *mask, *pattern;
	int pattern_len;
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
 */
struct cfg80211_wowlan {
	bool any, disconnect, magic_pkt;
	struct cfg80211_wowlan_trig_pkt_pattern *patterns;
	int n_patterns;
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
 *
 * @add_virtual_intf: create a new virtual interface with the given name,
 *	must set the struct wireless_dev's iftype. Beware: You must create
 *	the new netdev in the wiphy's network namespace! Returns the netdev,
 *	or an ERR_PTR.
 *
 * @del_virtual_intf: remove the virtual interface determined by ifindex.
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
 * @add_beacon: Add a beacon with given parameters, @head, @interval
 *	and @dtim_period will be valid, @tail is optional.
 * @set_beacon: Change the beacon parameters for an access point mode
 *	interface. This should reject the call when no beacon has been
 *	configured.
 * @del_beacon: Remove beacon configuration and stop sending the beacon.
 *
 * @add_station: Add a new station.
 * @del_station: Remove a station; @mac may be NULL to remove all stations.
 * @change_station: Modify a given station.
 * @get_station: get station information for the station identified by @mac
 * @dump_station: dump station callback -- resume dump at index @idx
 *
 * @add_mpath: add a fixed mesh path
 * @del_mpath: delete a given mesh path
 * @change_mpath: change a given mesh path
 * @get_mpath: get a mesh path for the given parameters
 * @dump_mpath: dump mesh path callback -- resume dump at index @idx
 * @join_mesh: join the mesh network with the specified parameters
 * @leave_mesh: leave the current mesh network
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
 * @set_channel: Set channel for a given wireless interface. Some devices
 *	may support multi-channel operation (by channel hopping) so cfg80211
 *	doesn't verify much. Note, however, that the passed netdev may be
 *	%NULL as well if the user requested changing the channel for the
 *	device itself, or for a monitor interface.
 *
 * @scan: Request to do a scan. If returning zero, the scan request is given
 *	the driver, and will be valid until passed to cfg80211_scan_done().
 *	For scan results, call cfg80211_inform_bss(); you can call this outside
 *	the scan/scan_done bracket too.
 *
 * @auth: Request to authenticate with the specified peer
 * @assoc: Request to (re)associate with the specified peer
 * @deauth: Request to deauthenticate from the specified peer
 * @disassoc: Request to disassociate from the specified peer
 *
 * @connect: Connect to the ESS with the specified parameters. When connected,
 *	call cfg80211_connect_result() with status code %WLAN_STATUS_SUCCESS.
 *	If the connection fails for some reason, call cfg80211_connect_result()
 *	with the status from the AP.
 * @disconnect: Disconnect from the BSS/ESS.
 *
 * @join_ibss: Join the specified IBSS (or create if necessary). Once done, call
 *	cfg80211_ibss_joined(), also call that function when changing BSSID due
 *	to a merge.
 * @leave_ibss: Leave the IBSS.
 *
 * @set_wiphy_params: Notify that wiphy parameters have changed;
 *	@changed bitfield (see &enum wiphy_params_flags) describes which values
 *	have changed. The actual parameter values are available in
 *	struct wiphy. If returning an error, no value should be changed.
 *
 * @set_tx_power: set the transmit power according to the parameters
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
 * @testmode_cmd: run a test mode command
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
 * @sched_scan_start: Tell the driver to start a scheduled scan.
 * @sched_scan_stop: Tell the driver to stop an ongoing scheduled
 *	scan.  The driver_initiated flag specifies whether the driver
 *	itself has informed that the scan has stopped.
 *
 * @mgmt_frame_register: Notify driver that a management frame type was
 *	registered. Note that this callback may not sleep, and cannot run
 *	concurrently with itself.
 *
 * @set_antenna: Set antenna configuration (tx_ant, rx_ant) on the device.
 *	Parameters are bitmaps of allowed antennas to use for TX/RX. Drivers may
 *	reject TX/RX mask combinations they cannot support by returning -EINVAL
 *	(also see nl80211.h @NL80211_ATTR_WIPHY_ANTENNA_TX).
 *
 * @get_antenna: Get current antenna configuration from device (tx_ant, rx_ant).
 *
 * @set_ringparam: Set tx and rx ring sizes.
 *
 * @get_ringparam: Get tx and rx ring current and maximum sizes.
 */
struct cfg80211_ops {
	int	(*suspend)(struct wiphy *wiphy, struct cfg80211_wowlan *wow);
	int	(*resume)(struct wiphy *wiphy);

	struct net_device * (*add_virtual_intf)(struct wiphy *wiphy,
						char *name,
						enum nl80211_iftype type,
						u32 *flags,
						struct vif_params *params);
	int	(*del_virtual_intf)(struct wiphy *wiphy, struct net_device *dev);
	int	(*change_virtual_intf)(struct wiphy *wiphy,
				       struct net_device *dev,
				       enum nl80211_iftype type, u32 *flags,
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

	int	(*add_beacon)(struct wiphy *wiphy, struct net_device *dev,
			      struct beacon_parameters *info);
	int	(*set_beacon)(struct wiphy *wiphy, struct net_device *dev,
			      struct beacon_parameters *info);
	int	(*del_beacon)(struct wiphy *wiphy, struct net_device *dev);


	int	(*add_station)(struct wiphy *wiphy, struct net_device *dev,
			       u8 *mac, struct station_parameters *params);
	int	(*del_station)(struct wiphy *wiphy, struct net_device *dev,
			       u8 *mac);
	int	(*change_station)(struct wiphy *wiphy, struct net_device *dev,
				  u8 *mac, struct station_parameters *params);
	int	(*get_station)(struct wiphy *wiphy, struct net_device *dev,
			       u8 *mac, struct station_info *sinfo);
	int	(*dump_station)(struct wiphy *wiphy, struct net_device *dev,
			       int idx, u8 *mac, struct station_info *sinfo);

	int	(*add_mpath)(struct wiphy *wiphy, struct net_device *dev,
			       u8 *dst, u8 *next_hop);
	int	(*del_mpath)(struct wiphy *wiphy, struct net_device *dev,
			       u8 *dst);
	int	(*change_mpath)(struct wiphy *wiphy, struct net_device *dev,
				  u8 *dst, u8 *next_hop);
	int	(*get_mpath)(struct wiphy *wiphy, struct net_device *dev,
			       u8 *dst, u8 *next_hop,
			       struct mpath_info *pinfo);
	int	(*dump_mpath)(struct wiphy *wiphy, struct net_device *dev,
			       int idx, u8 *dst, u8 *next_hop,
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

	int	(*change_bss)(struct wiphy *wiphy, struct net_device *dev,
			      struct bss_parameters *params);

	int	(*set_txq_params)(struct wiphy *wiphy,
				  struct ieee80211_txq_params *params);

	int	(*set_channel)(struct wiphy *wiphy, struct net_device *dev,
			       struct ieee80211_channel *chan,
			       enum nl80211_channel_type channel_type);

	int	(*scan)(struct wiphy *wiphy, struct net_device *dev,
			struct cfg80211_scan_request *request);

	int	(*auth)(struct wiphy *wiphy, struct net_device *dev,
			struct cfg80211_auth_request *req);
	int	(*assoc)(struct wiphy *wiphy, struct net_device *dev,
			 struct cfg80211_assoc_request *req);
	int	(*deauth)(struct wiphy *wiphy, struct net_device *dev,
			  struct cfg80211_deauth_request *req,
			  void *cookie);
	int	(*disassoc)(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_disassoc_request *req,
			    void *cookie);

	int	(*connect)(struct wiphy *wiphy, struct net_device *dev,
			   struct cfg80211_connect_params *sme);
	int	(*disconnect)(struct wiphy *wiphy, struct net_device *dev,
			      u16 reason_code);

	int	(*join_ibss)(struct wiphy *wiphy, struct net_device *dev,
			     struct cfg80211_ibss_params *params);
	int	(*leave_ibss)(struct wiphy *wiphy, struct net_device *dev);

	int	(*set_wiphy_params)(struct wiphy *wiphy, u32 changed);

	int	(*set_tx_power)(struct wiphy *wiphy,
				enum nl80211_tx_power_setting type, int mbm);
	int	(*get_tx_power)(struct wiphy *wiphy, int *dbm);

	int	(*set_wds_peer)(struct wiphy *wiphy, struct net_device *dev,
				const u8 *addr);

	void	(*rfkill_poll)(struct wiphy *wiphy);

#ifdef CONFIG_NL80211_TESTMODE
	int	(*testmode_cmd)(struct wiphy *wiphy, void *data, int len);
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
				     struct net_device *dev,
				     struct ieee80211_channel *chan,
				     enum nl80211_channel_type channel_type,
				     unsigned int duration,
				     u64 *cookie);
	int	(*cancel_remain_on_channel)(struct wiphy *wiphy,
					    struct net_device *dev,
					    u64 cookie);

	int	(*mgmt_tx)(struct wiphy *wiphy, struct net_device *dev,
			  struct ieee80211_channel *chan, bool offchan,
			  enum nl80211_channel_type channel_type,
			  bool channel_type_valid, unsigned int wait,
			  const u8 *buf, size_t len, u64 *cookie);
	int	(*mgmt_tx_cancel_wait)(struct wiphy *wiphy,
				       struct net_device *dev,
				       u64 cookie);

	int	(*set_power_mgmt)(struct wiphy *wiphy, struct net_device *dev,
				  bool enabled, int timeout);

	int	(*set_cqm_rssi_config)(struct wiphy *wiphy,
				       struct net_device *dev,
				       s32 rssi_thold, u32 rssi_hyst);

	void	(*mgmt_frame_register)(struct wiphy *wiphy,
				       struct net_device *dev,
				       u16 frame_type, bool reg);

	int	(*set_antenna)(struct wiphy *wiphy, u32 tx_ant, u32 rx_ant);
	int	(*get_antenna)(struct wiphy *wiphy, u32 *tx_ant, u32 *rx_ant);

	int	(*set_ringparam)(struct wiphy *wiphy, u32 tx, u32 rx);
	void	(*get_ringparam)(struct wiphy *wiphy,
				 u32 *tx, u32 *tx_max, u32 *rx, u32 *rx_max);

	int	(*sched_scan_start)(struct wiphy *wiphy,
				struct net_device *dev,
				struct cfg80211_sched_scan_request *request);
	int	(*sched_scan_stop)(struct wiphy *wiphy, struct net_device *dev);
};

/*
 * wireless hardware and networking interfaces structures
 * and registration/helper functions
 */

/**
 * enum wiphy_flags - wiphy capability flags
 *
 * @WIPHY_FLAG_CUSTOM_REGULATORY:  tells us the driver for this device
 * 	has its own custom regulatory domain and cannot identify the
 * 	ISO / IEC 3166 alpha2 it belongs to. When this is enabled
 * 	we will disregard the first regulatory hint (when the
 * 	initiator is %REGDOM_SET_BY_CORE).
 * @WIPHY_FLAG_STRICT_REGULATORY: tells us the driver for this device will
 *	ignore regulatory domain settings until it gets its own regulatory
 *	domain via its regulatory_hint() unless the regulatory hint is
 *	from a country IE. After its gets its own regulatory domain it will
 *	only allow further regulatory domain settings to further enhance
 *	compliance. For example if channel 13 and 14 are disabled by this
 *	regulatory domain no user regulatory domain can enable these channels
 *	at a later time. This can be used for devices which do not have
 *	calibration information guaranteed for frequencies or settings
 *	outside of its regulatory domain.
 * @WIPHY_FLAG_DISABLE_BEACON_HINTS: enable this if your driver needs to ensure
 *	that passive scan flags and beaconing flags may not be lifted by
 *	cfg80211 due to regulatory beacon hints. For more information on beacon
 *	hints read the documenation for regulatory_hint_found_beacon()
 * @WIPHY_FLAG_NETNS_OK: if not set, do not allow changing the netns of this
 *	wiphy at all
 * @WIPHY_FLAG_ENFORCE_COMBINATIONS: Set this flag to enforce interface
 *	combinations for this device. This flag is used for backward
 *	compatibility only until all drivers advertise combinations and
 *	they will always be enforced.
 * @WIPHY_FLAG_PS_ON_BY_DEFAULT: if set to true, powersave will be enabled
 *	by default -- this flag will be set depending on the kernel's default
 *	on wiphy_new(), but can be changed by the driver if it has a good
 *	reason to override the default
 * @WIPHY_FLAG_4ADDR_AP: supports 4addr mode even on AP (with a single station
 *	on a VLAN interface)
 * @WIPHY_FLAG_4ADDR_STATION: supports 4addr mode even as a station
 * @WIPHY_FLAG_CONTROL_PORT_PROTOCOL: This device supports setting the
 *	control port protocol ethertype. The device also honours the
 *	control_port_no_encrypt flag.
 * @WIPHY_FLAG_IBSS_RSN: The device supports IBSS RSN.
 * @WIPHY_FLAG_MESH_AUTH: The device supports mesh authentication by routing
 *	auth frames to userspace. See @NL80211_MESH_SETUP_USERSPACE_AUTH.
 * @WIPHY_FLAG_SUPPORTS_SCHED_SCAN: The device supports scheduled scans.
 */
enum wiphy_flags {
	WIPHY_FLAG_CUSTOM_REGULATORY		= BIT(0),
	WIPHY_FLAG_STRICT_REGULATORY		= BIT(1),
	WIPHY_FLAG_DISABLE_BEACON_HINTS		= BIT(2),
	WIPHY_FLAG_NETNS_OK			= BIT(3),
	WIPHY_FLAG_PS_ON_BY_DEFAULT		= BIT(4),
	WIPHY_FLAG_4ADDR_AP			= BIT(5),
	WIPHY_FLAG_4ADDR_STATION		= BIT(6),
	WIPHY_FLAG_CONTROL_PORT_PROTOCOL	= BIT(7),
	WIPHY_FLAG_IBSS_RSN			= BIT(8),
	WIPHY_FLAG_MESH_AUTH			= BIT(10),
	WIPHY_FLAG_SUPPORTS_SCHED_SCAN		= BIT(11),
	WIPHY_FLAG_ENFORCE_COMBINATIONS		= BIT(12),
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
 * @limits: limits for the given interface types
 * @n_limits: number of limitations
 * @num_different_channels: can use up to this many different channels
 * @max_interfaces: maximum number of interfaces in total allowed in this
 *	group
 * @beacon_int_infra_match: In this combination, the beacon intervals
 *	between infrastructure and AP types must match. This is required
 *	only in special cases.
 *
 * These examples can be expressed as follows:
 *
 * Allow #STA <= 1, #AP <= 1, matching BI, channels = 1, 2 total:
 *
 *  struct ieee80211_iface_limit limits1[] = {
 *	{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION), },
 *	{ .max = 1, .types = BIT(NL80211_IFTYPE_AP}, },
 *  };
 *  struct ieee80211_iface_combination combination1 = {
 *	.limits = limits1,
 *	.n_limits = ARRAY_SIZE(limits1),
 *	.max_interfaces = 2,
 *	.beacon_int_infra_match = true,
 *  };
 *
 *
 * Allow #{AP, P2P-GO} <= 8, channels = 1, 8 total:
 *
 *  struct ieee80211_iface_limit limits2[] = {
 *	{ .max = 8, .types = BIT(NL80211_IFTYPE_AP) |
 *			     BIT(NL80211_IFTYPE_P2P_GO), },
 *  };
 *  struct ieee80211_iface_combination combination2 = {
 *	.limits = limits2,
 *	.n_limits = ARRAY_SIZE(limits2),
 *	.max_interfaces = 8,
 *	.num_different_channels = 1,
 *  };
 *
 *
 * Allow #STA <= 1, #{P2P-client,P2P-GO} <= 3 on two channels, 4 total.
 * This allows for an infrastructure connection and three P2P connections.
 *
 *  struct ieee80211_iface_limit limits3[] = {
 *	{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION), },
 *	{ .max = 3, .types = BIT(NL80211_IFTYPE_P2P_GO) |
 *			     BIT(NL80211_IFTYPE_P2P_CLIENT), },
 *  };
 *  struct ieee80211_iface_combination combination3 = {
 *	.limits = limits3,
 *	.n_limits = ARRAY_SIZE(limits3),
 *	.max_interfaces = 4,
 *	.num_different_channels = 2,
 *  };
 */
struct ieee80211_iface_combination {
	const struct ieee80211_iface_limit *limits;
	u32 num_different_channels;
	u16 max_interfaces;
	u8 n_limits;
	bool beacon_int_infra_match;
};

struct mac_address {
	u8 addr[ETH_ALEN];
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
 */
enum wiphy_wowlan_support_flags {
	WIPHY_WOWLAN_ANY	= BIT(0),
	WIPHY_WOWLAN_MAGIC_PKT	= BIT(1),
	WIPHY_WOWLAN_DISCONNECT	= BIT(2),
};

/**
 * struct wiphy_wowlan_support - WoWLAN support data
 * @flags: see &enum wiphy_wowlan_support_flags
 * @n_patterns: number of supported wakeup patterns
 *	(see nl80211.h for the pattern definition)
 * @pattern_max_len: maximum length of each pattern
 * @pattern_min_len: minimum length of each pattern
 */
struct wiphy_wowlan_support {
	u32 flags;
	int n_patterns;
	int pattern_max_len;
	int pattern_min_len;
};

/**
 * struct wiphy - wireless hardware description
 * @reg_notifier: the driver's regulatory notification callback,
 *	note that if your driver uses wiphy_apply_custom_regulatory()
 *	the reg_notifier's request can be passed as NULL
 * @regd: the driver's regulatory domain, if one was requested via
 * 	the regulatory_hint() API. This can be used by the driver
 *	on the reg_notifier() if it chooses to ignore future
 *	regulatory domain changes caused by other drivers.
 * @signal_type: signal type reported in &struct cfg80211_bss.
 * @cipher_suites: supported cipher suites
 * @n_cipher_suites: number of supported cipher suites
 * @retry_short: Retry limit for short frames (dot11ShortRetryLimit)
 * @retry_long: Retry limit for long frames (dot11LongRetryLimit)
 * @frag_threshold: Fragmentation threshold (dot11FragmentationThreshold);
 *	-1 = fragmentation disabled, only odd values >= 256 used
 * @rts_threshold: RTS threshold (dot11RTSThreshold); -1 = RTS/CTS disabled
 * @_net: the network namespace this wiphy currently lives in
 * @perm_addr: permanent MAC address of this device
 * @addr_mask: If the device supports multiple MAC addresses by masking,
 *	set this to a mask with variable bits set to 1, e.g. if the last
 *	four bits are variable then set it to 00:...:00:0f. The actual
 *	variable bits shall be determined by the interfaces added, with
 *	interfaces not matching the mask being rejected to be brought up.
 * @n_addresses: number of addresses in @addresses.
 * @addresses: If the device has more than one address, set this pointer
 *	to a list of addresses (6 bytes each). The first one will be used
 *	by default for perm_addr. In this case, the mask should be set to
 *	all-zeroes. In this case it is assumed that the device can handle
 *	the same number of arbitrary MAC addresses.
 * @debugfsdir: debugfs directory used for this wiphy, will be renamed
 *	automatically on wiphy renames
 * @dev: (virtual) struct device for this wiphy
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
 * @bss_priv_size: each BSS struct has private data allocated with it,
 *	this variable determines its size
 * @max_scan_ssids: maximum number of SSIDs the device can scan for in
 *	any given scan
 * @max_scan_ie_len: maximum length of user-controlled IEs device can
 *	add to probe request frames transmitted during a scan, must not
 *	include fixed IEs like supported rates
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
 * @max_remain_on_channel_duration: Maximum time a remain-on-channel operation
 *	may request, if implemented.
 *
 * @wowlan: WoWLAN support information
 */
struct wiphy {
	/* assign these fields before you register the wiphy */

	/* permanent MAC address(es) */
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

	u32 flags;

	enum cfg80211_signal_type signal_type;

	int bss_priv_size;
	u8 max_scan_ssids;
	u16 max_scan_ie_len;

	int n_cipher_suites;
	const u32 *cipher_suites;

	u8 retry_short;
	u8 retry_long;
	u32 frag_threshold;
	u32 rts_threshold;
	u8 coverage_class;

	char fw_version[ETHTOOL_BUSINFO_LEN];
	u32 hw_version;

	struct wiphy_wowlan_support wowlan;

	u16 max_remain_on_channel_duration;

	u8 max_num_pmkids;

	u32 available_antennas_tx;
	u32 available_antennas_rx;

	/* If multiple wiphys are registered and you're handed e.g.
	 * a regular netdev with assigned ieee80211_ptr, you won't
	 * know whether it points to a wiphy your driver has registered
	 * or not. Assign this to something global to your driver to
	 * help determine whether you own this wiphy or not. */
	const void *privid;

	struct ieee80211_supported_band *bands[IEEE80211_NUM_BANDS];

	/* Lets us get back the wiphy on the callback */
	int (*reg_notifier)(struct wiphy *wiphy,
			    struct regulatory_request *request);

	/* fields below are read-only, assigned by cfg80211 */

	const struct ieee80211_regdomain *regd;

	/* the item in /sys/class/ieee80211/ points to this,
	 * you need use set_wiphy_dev() (see below) */
	struct device dev;

	/* dir in debugfs: ieee80211/<wiphyname> */
	struct dentry *debugfsdir;

#ifdef CONFIG_NET_NS
	/* the network namespace this phy lives in currently */
	struct net *_net;
#endif

#ifdef CONFIG_CFG80211_WEXT
	const struct iw_handler_def *wext;
#endif

	char priv[0] __attribute__((__aligned__(NETDEV_ALIGN)));
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
 */
static inline struct device *wiphy_dev(struct wiphy *wiphy)
{
	return wiphy->dev.parent;
}

/**
 * wiphy_name - get wiphy name
 *
 * @wiphy: The wiphy whose name to return
 */
static inline const char *wiphy_name(const struct wiphy *wiphy)
{
	return dev_name(&wiphy->dev);
}

/**
 * wiphy_new - create a new wiphy for use with cfg80211
 *
 * @ops: The configuration operations for this device
 * @sizeof_priv: The size of the private area to allocate
 *
 * Create a new wiphy and associate the given operations with it.
 * @sizeof_priv bytes are allocated for private use.
 *
 * The returned pointer must be assigned to each netdev's
 * ieee80211_ptr for proper operation.
 */
struct wiphy *wiphy_new(const struct cfg80211_ops *ops, int sizeof_priv);

/**
 * wiphy_register - register a wiphy with cfg80211
 *
 * @wiphy: The wiphy to register.
 *
 * Returns a non-negative wiphy index or a negative error code.
 */
extern int wiphy_register(struct wiphy *wiphy);

/**
 * wiphy_unregister - deregister a wiphy from cfg80211
 *
 * @wiphy: The wiphy to unregister.
 *
 * After this call, no more requests can be made with this priv
 * pointer, but the call may sleep to wait for an outstanding
 * request that is being handled.
 */
extern void wiphy_unregister(struct wiphy *wiphy);

/**
 * wiphy_free - free wiphy
 *
 * @wiphy: The wiphy to free
 */
extern void wiphy_free(struct wiphy *wiphy);

/* internal structs */
struct cfg80211_conn;
struct cfg80211_internal_bss;
struct cfg80211_cached_keys;

#define MAX_AUTH_BSSES		4

/**
 * struct wireless_dev - wireless per-netdev state
 *
 * This structure must be allocated by the driver/stack
 * that uses the ieee80211_ptr field in struct net_device
 * (this is intentional so it can be allocated along with
 * the netdev.)
 *
 * @wiphy: pointer to hardware description
 * @iftype: interface type
 * @list: (private) Used to collect the interfaces
 * @netdev: (private) Used to reference back to the netdev
 * @current_bss: (private) Used by the internal configuration code
 * @channel: (private) Used by the internal configuration code to track
 *	user-set AP, monitor and WDS channels for wireless extensions
 * @bssid: (private) Used by the internal configuration code
 * @ssid: (private) Used by the internal configuration code
 * @ssid_len: (private) Used by the internal configuration code
 * @mesh_id_len: (private) Used by the internal configuration code
 * @mesh_id_up_len: (private) Used by the internal configuration code
 * @wext: (private) Used by the internal wireless extensions compat code
 * @use_4addr: indicates 4addr mode is used on this interface, must be
 *	set by driver (if supported) on add_interface BEFORE registering the
 *	netdev and may otherwise be used by driver read-only, will be update
 *	by cfg80211 on change_interface
 * @mgmt_registrations: list of registrations for management frames
 * @mgmt_registrations_lock: lock for the list
 * @mtx: mutex used to lock data in this struct
 * @cleanup_work: work struct used for cleanup that can't be done directly
 * @beacon_interval: beacon interval used on this device for transmitting
 *	beacons, 0 when not valid
 */
struct wireless_dev {
	struct wiphy *wiphy;
	enum nl80211_iftype iftype;

	/* the remainder of this struct should be private to cfg80211 */
	struct list_head list;
	struct net_device *netdev;

	struct list_head mgmt_registrations;
	spinlock_t mgmt_registrations_lock;

	struct mutex mtx;

	struct work_struct cleanup_work;

	bool use_4addr;

	/* currently used for IBSS and SME - might be rearranged later */
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len, mesh_id_len, mesh_id_up_len;
	enum {
		CFG80211_SME_IDLE,
		CFG80211_SME_CONNECTING,
		CFG80211_SME_CONNECTED,
	} sme_state;
	struct cfg80211_conn *conn;
	struct cfg80211_cached_keys *connect_keys;

	struct list_head event_list;
	spinlock_t event_lock;

	struct cfg80211_internal_bss *authtry_bsses[MAX_AUTH_BSSES];
	struct cfg80211_internal_bss *auth_bsses[MAX_AUTH_BSSES];
	struct cfg80211_internal_bss *current_bss; /* associated / joined */
	struct ieee80211_channel *channel;

	bool ps;
	int ps_timeout;

	int beacon_interval;

#ifdef CONFIG_CFG80211_WEXT
	/* wext data */
	struct {
		struct cfg80211_ibss_params ibss;
		struct cfg80211_connect_params connect;
		struct cfg80211_cached_keys *keys;
		u8 *ie;
		size_t ie_len;
		u8 bssid[ETH_ALEN], prev_bssid[ETH_ALEN];
		u8 ssid[IEEE80211_MAX_SSID_LEN];
		s8 default_key, default_mgmt_key;
		bool prev_bssid_valid;
	} wext;
#endif
};

/**
 * wdev_priv - return wiphy priv from wireless_dev
 *
 * @wdev: The wireless device whose wiphy's priv pointer to return
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
 * ieee80211_channel_to_frequency - convert channel number to frequency
 * @chan: channel number
 * @band: band, necessary due to channel number overlap
 */
extern int ieee80211_channel_to_frequency(int chan, enum ieee80211_band band);

/**
 * ieee80211_frequency_to_channel - convert frequency to channel number
 * @freq: center frequency
 */
extern int ieee80211_frequency_to_channel(int freq);

/*
 * Name indirection necessary because the ieee80211 code also has
 * a function named "ieee80211_get_channel", so if you include
 * cfg80211's header file you get cfg80211's version, if you try
 * to include both header files you'll (rightfully!) get a symbol
 * clash.
 */
extern struct ieee80211_channel *__ieee80211_get_channel(struct wiphy *wiphy,
							 int freq);
/**
 * ieee80211_get_channel - get channel struct from wiphy for specified frequency
 * @wiphy: the struct wiphy to get the channel for
 * @freq: the center frequency of the channel
 */
static inline struct ieee80211_channel *
ieee80211_get_channel(struct wiphy *wiphy, int freq)
{
	return __ieee80211_get_channel(wiphy, freq);
}

/**
 * ieee80211_get_response_rate - get basic rate for a given rate
 *
 * @sband: the band to look for rates in
 * @basic_rates: bitmap of basic rates
 * @bitrate: the bitrate for which to find the basic rate
 *
 * This function returns the basic rate corresponding to a given
 * bitrate, that is the next lower bitrate contained in the basic
 * rate map, which is, for this function, given as a bitmap of
 * indices of rates in the band's bitrate table.
 */
struct ieee80211_rate *
ieee80211_get_response_rate(struct ieee80211_supported_band *sband,
			    u32 basic_rates, int bitrate);

/*
 * Radiotap parsing functions -- for controlled injection support
 *
 * Implemented in net/wireless/radiotap.c
 * Documentation in Documentation/networking/radiotap-headers.txt
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

extern int ieee80211_radiotap_iterator_init(
	struct ieee80211_radiotap_iterator *iterator,
	struct ieee80211_radiotap_header *radiotap_header,
	int max_length, const struct ieee80211_radiotap_vendor_namespaces *vns);

extern int ieee80211_radiotap_iterator_next(
	struct ieee80211_radiotap_iterator *iterator);


extern const unsigned char rfc1042_header[6];
extern const unsigned char bridge_tunnel_header[6];

/**
 * ieee80211_get_hdrlen_from_skb - get header length from data
 *
 * Given an skb with a raw 802.11 header at the data pointer this function
 * returns the 802.11 header length in bytes (not including encryption
 * headers). If the data in the sk_buff is too short to contain a valid 802.11
 * header the function returns 0.
 *
 * @skb: the frame
 */
unsigned int ieee80211_get_hdrlen_from_skb(const struct sk_buff *skb);

/**
 * ieee80211_hdrlen - get header length in bytes from frame control
 * @fc: frame control field in little-endian format
 */
unsigned int __attribute_const__ ieee80211_hdrlen(__le16 fc);

/**
 * DOC: Data path helpers
 *
 * In addition to generic utilities, cfg80211 also offers
 * functions that help implement the data path for devices
 * that do not do the 802.11/802.3 conversion on the device.
 */

/**
 * ieee80211_data_to_8023 - convert an 802.11 data frame to 802.3
 * @skb: the 802.11 data frame
 * @addr: the device MAC address
 * @iftype: the virtual interface type
 */
int ieee80211_data_to_8023(struct sk_buff *skb, const u8 *addr,
			   enum nl80211_iftype iftype);

/**
 * ieee80211_data_from_8023 - convert an 802.3 frame to 802.11
 * @skb: the 802.3 frame
 * @addr: the device MAC address
 * @iftype: the virtual interface type
 * @bssid: the network bssid (used only for iftype STATION and ADHOC)
 * @qos: build 802.11 QoS data frame
 */
int ieee80211_data_from_8023(struct sk_buff *skb, const u8 *addr,
			     enum nl80211_iftype iftype, u8 *bssid, bool qos);

/**
 * ieee80211_amsdu_to_8023s - decode an IEEE 802.11n A-MSDU frame
 *
 * Decode an IEEE 802.11n A-MSDU frame and convert it to a list of
 * 802.3 frames. The @list will be empty if the decode fails. The
 * @skb is consumed after the function returns.
 *
 * @skb: The input IEEE 802.11n A-MSDU frame.
 * @list: The output list of 802.3 frames. It must be allocated and
 *	initialized by by the caller.
 * @addr: The device MAC address.
 * @iftype: The device interface type.
 * @extra_headroom: The hardware extra headroom for SKBs in the @list.
 * @has_80211_header: Set it true if SKB is with IEEE 802.11 header.
 */
void ieee80211_amsdu_to_8023s(struct sk_buff *skb, struct sk_buff_head *list,
			      const u8 *addr, enum nl80211_iftype iftype,
			      const unsigned int extra_headroom,
			      bool has_80211_header);

/**
 * cfg80211_classify8021d - determine the 802.1p/1d tag for a data frame
 * @skb: the data frame
 */
unsigned int cfg80211_classify8021d(struct sk_buff *skb);

/**
 * cfg80211_find_ie - find information element in data
 *
 * @eid: element ID
 * @ies: data consisting of IEs
 * @len: length of data
 *
 * This function will return %NULL if the element ID could
 * not be found or if the element is invalid (claims to be
 * longer than the given data), or a pointer to the first byte
 * of the requested element, that is the byte containing the
 * element ID. There are no checks on the element length
 * other than having to fit into the given data.
 */
const u8 *cfg80211_find_ie(u8 eid, const u8 *ies, int len);

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
 * 	should be in. If @rd is set this should be NULL. Note that if you
 * 	set this to NULL you should still set rd->alpha2 to some accepted
 * 	alpha2.
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
 */
extern int regulatory_hint(struct wiphy *wiphy, const char *alpha2);

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
 */
extern void wiphy_apply_custom_regulatory(
	struct wiphy *wiphy,
	const struct ieee80211_regdomain *regd);

/**
 * freq_reg_info - get regulatory information for the given frequency
 * @wiphy: the wiphy for which we want to process this rule for
 * @center_freq: Frequency in KHz for which we want regulatory information for
 * @desired_bw_khz: the desired max bandwidth you want to use per
 *	channel. Note that this is still 20 MHz if you want to use HT40
 *	as HT40 makes use of two channels for its 40 MHz width bandwidth.
 *	If set to 0 we'll assume you want the standard 20 MHz.
 * @reg_rule: the regulatory rule which we have for this frequency
 *
 * Use this function to get the regulatory rule for a specific frequency on
 * a given wireless device. If the device has a specific regulatory domain
 * it wants to follow we respect that unless a country IE has been received
 * and processed already.
 *
 * Returns 0 if it was able to find a valid regulatory rule which does
 * apply to the given center_freq otherwise it returns non-zero. It will
 * also return -ERANGE if we determine the given center_freq does not even have
 * a regulatory rule for a frequency range in the center_freq's band. See
 * freq_in_rule_band() for our current definition of a band -- this is purely
 * subjective and right now its 802.11 specific.
 */
extern int freq_reg_info(struct wiphy *wiphy,
			 u32 center_freq,
			 u32 desired_bw_khz,
			 const struct ieee80211_reg_rule **reg_rule);

/*
 * Temporary wext handlers & helper functions
 *
 * In the future cfg80211 will simply assign the entire wext handler
 * structure to netdevs it manages, but we're not there yet.
 */
int cfg80211_wext_giwname(struct net_device *dev,
			  struct iw_request_info *info,
			  char *name, char *extra);
int cfg80211_wext_siwmode(struct net_device *dev, struct iw_request_info *info,
			  u32 *mode, char *extra);
int cfg80211_wext_giwmode(struct net_device *dev, struct iw_request_info *info,
			  u32 *mode, char *extra);
int cfg80211_wext_siwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
int cfg80211_wext_giwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *data, char *extra);
int cfg80211_wext_siwmlme(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *data, char *extra);
int cfg80211_wext_giwrange(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *extra);
int cfg80211_wext_siwgenie(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *extra);
int cfg80211_wext_siwauth(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *data, char *extra);
int cfg80211_wext_giwauth(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *data, char *extra);

int cfg80211_wext_siwfreq(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_freq *freq, char *extra);
int cfg80211_wext_giwfreq(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_freq *freq, char *extra);
int cfg80211_wext_siwessid(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *ssid);
int cfg80211_wext_giwessid(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *ssid);
int cfg80211_wext_siwrate(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *rate, char *extra);
int cfg80211_wext_giwrate(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *rate, char *extra);

int cfg80211_wext_siwrts(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *rts, char *extra);
int cfg80211_wext_giwrts(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *rts, char *extra);
int cfg80211_wext_siwfrag(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *frag, char *extra);
int cfg80211_wext_giwfrag(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *frag, char *extra);
int cfg80211_wext_siwretry(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *retry, char *extra);
int cfg80211_wext_giwretry(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *retry, char *extra);
int cfg80211_wext_siwencodeext(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_point *erq, char *extra);
int cfg80211_wext_siwencode(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_point *erq, char *keybuf);
int cfg80211_wext_giwencode(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_point *erq, char *keybuf);
int cfg80211_wext_siwtxpower(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *data, char *keybuf);
int cfg80211_wext_giwtxpower(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *data, char *keybuf);
struct iw_statistics *cfg80211_wireless_stats(struct net_device *dev);

int cfg80211_wext_siwpower(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *wrq, char *extra);
int cfg80211_wext_giwpower(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *wrq, char *extra);

int cfg80211_wext_siwap(struct net_device *dev,
			struct iw_request_info *info,
			struct sockaddr *ap_addr, char *extra);
int cfg80211_wext_giwap(struct net_device *dev,
			struct iw_request_info *info,
			struct sockaddr *ap_addr, char *extra);

int cfg80211_wext_siwpmksa(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *extra);

/*
 * callbacks for asynchronous cfg80211 methods, notification
 * functions and BSS handling helpers
 */

/**
 * cfg80211_scan_done - notify that scan finished
 *
 * @request: the corresponding scan request
 * @aborted: set to true if the scan was aborted for any reason,
 *	userspace will be notified of that
 */
void cfg80211_scan_done(struct cfg80211_scan_request *request, bool aborted);

/**
 * cfg80211_sched_scan_results - notify that new scan results are available
 *
 * @wiphy: the wiphy which got scheduled scan results
 */
void cfg80211_sched_scan_results(struct wiphy *wiphy);

/**
 * cfg80211_sched_scan_stopped - notify that the scheduled scan has stopped
 *
 * @wiphy: the wiphy on which the scheduled scan stopped
 *
 * The driver can call this function to inform cfg80211 that the
 * scheduled scan had to be stopped, for whatever reason.  The driver
 * is then called back via the sched_scan_stop operation when done.
 */
void cfg80211_sched_scan_stopped(struct wiphy *wiphy);

/**
 * cfg80211_inform_bss_frame - inform cfg80211 of a received BSS frame
 *
 * @wiphy: the wiphy reporting the BSS
 * @channel: The channel the frame was received on
 * @mgmt: the management frame (probe response or beacon)
 * @len: length of the management frame
 * @signal: the signal strength, type depends on the wiphy's signal_type
 * @gfp: context flags
 *
 * This informs cfg80211 that BSS information was found and
 * the BSS should be updated/added.
 */
struct cfg80211_bss*
cfg80211_inform_bss_frame(struct wiphy *wiphy,
			  struct ieee80211_channel *channel,
			  struct ieee80211_mgmt *mgmt, size_t len,
			  s32 signal, gfp_t gfp);

/**
 * cfg80211_inform_bss - inform cfg80211 of a new BSS
 *
 * @wiphy: the wiphy reporting the BSS
 * @channel: The channel the frame was received on
 * @bssid: the BSSID of the BSS
 * @timestamp: the TSF timestamp sent by the peer
 * @capability: the capability field sent by the peer
 * @beacon_interval: the beacon interval announced by the peer
 * @ie: additional IEs sent by the peer
 * @ielen: length of the additional IEs
 * @signal: the signal strength, type depends on the wiphy's signal_type
 * @gfp: context flags
 *
 * This informs cfg80211 that BSS information was found and
 * the BSS should be updated/added.
 */
struct cfg80211_bss*
cfg80211_inform_bss(struct wiphy *wiphy,
		    struct ieee80211_channel *channel,
		    const u8 *bssid,
		    u64 timestamp, u16 capability, u16 beacon_interval,
		    const u8 *ie, size_t ielen,
		    s32 signal, gfp_t gfp);

struct cfg80211_bss *cfg80211_get_bss(struct wiphy *wiphy,
				      struct ieee80211_channel *channel,
				      const u8 *bssid,
				      const u8 *ssid, size_t ssid_len,
				      u16 capa_mask, u16 capa_val);
static inline struct cfg80211_bss *
cfg80211_get_ibss(struct wiphy *wiphy,
		  struct ieee80211_channel *channel,
		  const u8 *ssid, size_t ssid_len)
{
	return cfg80211_get_bss(wiphy, channel, NULL, ssid, ssid_len,
				WLAN_CAPABILITY_IBSS, WLAN_CAPABILITY_IBSS);
}

struct cfg80211_bss *cfg80211_get_mesh(struct wiphy *wiphy,
				       struct ieee80211_channel *channel,
				       const u8 *meshid, size_t meshidlen,
				       const u8 *meshcfg);
void cfg80211_put_bss(struct cfg80211_bss *bss);

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
 * cfg80211_send_rx_auth - notification of processed authentication
 * @dev: network device
 * @buf: authentication frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever an authentication has been processed in
 * station mode. The driver is required to call either this function or
 * cfg80211_send_auth_timeout() to indicate the result of cfg80211_ops::auth()
 * call. This function may sleep.
 */
void cfg80211_send_rx_auth(struct net_device *dev, const u8 *buf, size_t len);

/**
 * cfg80211_send_auth_timeout - notification of timed out authentication
 * @dev: network device
 * @addr: The MAC address of the device with which the authentication timed out
 *
 * This function may sleep.
 */
void cfg80211_send_auth_timeout(struct net_device *dev, const u8 *addr);

/**
 * __cfg80211_auth_canceled - notify cfg80211 that authentication was canceled
 * @dev: network device
 * @addr: The MAC address of the device with which the authentication timed out
 *
 * When a pending authentication had no action yet, the driver may decide
 * to not send a deauth frame, but in that case must calls this function
 * to tell cfg80211 about this decision. It is only valid to call this
 * function within the deauth() callback.
 */
void __cfg80211_auth_canceled(struct net_device *dev, const u8 *addr);

/**
 * cfg80211_send_rx_assoc - notification of processed association
 * @dev: network device
 * @buf: (re)association response frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever a (re)association response has been
 * processed in station mode. The driver is required to call either this
 * function or cfg80211_send_assoc_timeout() to indicate the result of
 * cfg80211_ops::assoc() call. This function may sleep.
 */
void cfg80211_send_rx_assoc(struct net_device *dev, const u8 *buf, size_t len);

/**
 * cfg80211_send_assoc_timeout - notification of timed out association
 * @dev: network device
 * @addr: The MAC address of the device with which the association timed out
 *
 * This function may sleep.
 */
void cfg80211_send_assoc_timeout(struct net_device *dev, const u8 *addr);

/**
 * cfg80211_send_deauth - notification of processed deauthentication
 * @dev: network device
 * @buf: deauthentication frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever deauthentication has been processed in
 * station mode. This includes both received deauthentication frames and
 * locally generated ones. This function may sleep.
 */
void cfg80211_send_deauth(struct net_device *dev, const u8 *buf, size_t len);

/**
 * __cfg80211_send_deauth - notification of processed deauthentication
 * @dev: network device
 * @buf: deauthentication frame (header + body)
 * @len: length of the frame data
 *
 * Like cfg80211_send_deauth(), but doesn't take the wdev lock.
 */
void __cfg80211_send_deauth(struct net_device *dev, const u8 *buf, size_t len);

/**
 * cfg80211_send_disassoc - notification of processed disassociation
 * @dev: network device
 * @buf: disassociation response frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever disassociation has been processed in
 * station mode. This includes both received disassociation frames and locally
 * generated ones. This function may sleep.
 */
void cfg80211_send_disassoc(struct net_device *dev, const u8 *buf, size_t len);

/**
 * __cfg80211_send_disassoc - notification of processed disassociation
 * @dev: network device
 * @buf: disassociation response frame (header + body)
 * @len: length of the frame data
 *
 * Like cfg80211_send_disassoc(), but doesn't take the wdev lock.
 */
void __cfg80211_send_disassoc(struct net_device *dev, const u8 *buf,
	size_t len);

/**
 * cfg80211_send_unprot_deauth - notification of unprotected deauthentication
 * @dev: network device
 * @buf: deauthentication frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever a received Deauthentication frame has been
 * dropped in station mode because of MFP being used but the Deauthentication
 * frame was not protected. This function may sleep.
 */
void cfg80211_send_unprot_deauth(struct net_device *dev, const u8 *buf,
				 size_t len);

/**
 * cfg80211_send_unprot_disassoc - notification of unprotected disassociation
 * @dev: network device
 * @buf: disassociation frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever a received Disassociation frame has been
 * dropped in station mode because of MFP being used but the Disassociation
 * frame was not protected. This function may sleep.
 */
void cfg80211_send_unprot_disassoc(struct net_device *dev, const u8 *buf,
				   size_t len);

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
 * @gfp: allocation flags
 *
 * This function notifies cfg80211 that the device joined an IBSS or
 * switched to a different BSSID. Before this function can be called,
 * either a beacon has to have been received from the IBSS, or one of
 * the cfg80211_inform_bss{,_frame} functions must have been called
 * with the locally generated beacon -- this guarantees that there is
 * always a scan result for this IBSS. cfg80211 will handle the rest.
 */
void cfg80211_ibss_joined(struct net_device *dev, const u8 *bssid, gfp_t gfp);

/**
 * cfg80211_notify_new_candidate - notify cfg80211 of a new mesh peer candidate
 *
 * @dev: network device
 * @macaddr: the MAC address of the new candidate
 * @ie: information elements advertised by the peer candidate
 * @ie_len: lenght of the information elements buffer
 * @gfp: allocation flags
 *
 * This function notifies cfg80211 that the mesh peer candidate has been
 * detected, most likely via a beacon or, less likely, via a probe response.
 * cfg80211 then sends a notification to userspace.
 */
void cfg80211_notify_new_peer_candidate(struct net_device *dev,
		const u8 *macaddr, const u8 *ie, u8 ie_len, gfp_t gfp);

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
 * The returned skb (or %NULL if any errors happen) is pre-filled
 * with the wiphy index and set up in a way that any data that is
 * put into the skb (with skb_put(), nla_put() or similar) will end
 * up being within the %NL80211_ATTR_TESTDATA attribute, so all that
 * needs to be done with the skb is adding data for the corresponding
 * userspace tool which can then read that data out of the testdata
 * attribute. You must not modify the skb in any other way.
 *
 * When done, call cfg80211_testmode_reply() with the skb and return
 * its error code as the result of the @testmode_cmd operation.
 */
struct sk_buff *cfg80211_testmode_alloc_reply_skb(struct wiphy *wiphy,
						  int approxlen);

/**
 * cfg80211_testmode_reply - send the reply skb
 * @skb: The skb, must have been allocated with
 *	cfg80211_testmode_alloc_reply_skb()
 *
 * Returns an error code or 0 on success, since calling this
 * function will usually be the last thing before returning
 * from the @testmode_cmd you should return the error code.
 * Note that this function consumes the skb regardless of the
 * return value.
 */
int cfg80211_testmode_reply(struct sk_buff *skb);

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
 * The returned skb (or %NULL if any errors happen) is set up in the
 * same way as with cfg80211_testmode_alloc_reply_skb() but prepared
 * for an event. As there, you should simply add data to it that will
 * then end up in the %NL80211_ATTR_TESTDATA attribute. Again, you must
 * not modify the skb in any other way.
 *
 * When done filling the skb, call cfg80211_testmode_event() with the
 * skb to send the event.
 */
struct sk_buff *cfg80211_testmode_alloc_event_skb(struct wiphy *wiphy,
						  int approxlen, gfp_t gfp);

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
void cfg80211_testmode_event(struct sk_buff *skb, gfp_t gfp);

#define CFG80211_TESTMODE_CMD(cmd)	.testmode_cmd = (cmd),
#else
#define CFG80211_TESTMODE_CMD(cmd)
#endif

/**
 * cfg80211_connect_result - notify cfg80211 of connection result
 *
 * @dev: network device
 * @bssid: the BSSID of the AP
 * @req_ie: association request IEs (maybe be %NULL)
 * @req_ie_len: association request IEs length
 * @resp_ie: association response IEs (may be %NULL)
 * @resp_ie_len: assoc response IEs length
 * @status: status code, 0 for successful connection, use
 *	%WLAN_STATUS_UNSPECIFIED_FAILURE if your device cannot give you
 *	the real status code for failures.
 * @gfp: allocation flags
 *
 * It should be called by the underlying driver whenever connect() has
 * succeeded.
 */
void cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
			     const u8 *req_ie, size_t req_ie_len,
			     const u8 *resp_ie, size_t resp_ie_len,
			     u16 status, gfp_t gfp);

/**
 * cfg80211_roamed - notify cfg80211 of roaming
 *
 * @dev: network device
 * @channel: the channel of the new AP
 * @bssid: the BSSID of the new AP
 * @req_ie: association request IEs (maybe be %NULL)
 * @req_ie_len: association request IEs length
 * @resp_ie: association response IEs (may be %NULL)
 * @resp_ie_len: assoc response IEs length
 * @gfp: allocation flags
 *
 * It should be called by the underlying driver whenever it roamed
 * from one AP to another while connected.
 */
void cfg80211_roamed(struct net_device *dev,
		     struct ieee80211_channel *channel,
		     const u8 *bssid,
		     const u8 *req_ie, size_t req_ie_len,
		     const u8 *resp_ie, size_t resp_ie_len, gfp_t gfp);

/**
 * cfg80211_disconnected - notify cfg80211 that connection was dropped
 *
 * @dev: network device
 * @ie: information elements of the deauth/disassoc frame (may be %NULL)
 * @ie_len: length of IEs
 * @reason: reason code for the disconnection, set it to 0 if unknown
 * @gfp: allocation flags
 *
 * After it calls this function, the driver should enter an idle state
 * and not try to connect to any AP any more.
 */
void cfg80211_disconnected(struct net_device *dev, u16 reason,
			   u8 *ie, size_t ie_len, gfp_t gfp);

/**
 * cfg80211_ready_on_channel - notification of remain_on_channel start
 * @dev: network device
 * @cookie: the request cookie
 * @chan: The current channel (from remain_on_channel request)
 * @channel_type: Channel type
 * @duration: Duration in milliseconds that the driver intents to remain on the
 *	channel
 * @gfp: allocation flags
 */
void cfg80211_ready_on_channel(struct net_device *dev, u64 cookie,
			       struct ieee80211_channel *chan,
			       enum nl80211_channel_type channel_type,
			       unsigned int duration, gfp_t gfp);

/**
 * cfg80211_remain_on_channel_expired - remain_on_channel duration expired
 * @dev: network device
 * @cookie: the request cookie
 * @chan: The current channel (from remain_on_channel request)
 * @channel_type: Channel type
 * @gfp: allocation flags
 */
void cfg80211_remain_on_channel_expired(struct net_device *dev,
					u64 cookie,
					struct ieee80211_channel *chan,
					enum nl80211_channel_type channel_type,
					gfp_t gfp);


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
 * cfg80211_del_sta - notify userspace about deletion of a station
 *
 * @dev: the netdev
 * @mac_addr: the station's address
 * @gfp: allocation flags
 */
void cfg80211_del_sta(struct net_device *dev, const u8 *mac_addr, gfp_t gfp);

/**
 * cfg80211_rx_mgmt - notification of received, unprocessed management frame
 * @dev: network device
 * @freq: Frequency on which the frame was received in MHz
 * @buf: Management frame (header + body)
 * @len: length of the frame data
 * @gfp: context flags
 *
 * Returns %true if a user space application has registered for this frame.
 * For action frames, that makes it responsible for rejecting unrecognized
 * action frames; %false otherwise, in which case for action frames the
 * driver is responsible for rejecting the frame.
 *
 * This function is called whenever an Action frame is received for a station
 * mode interface, but is not processed in kernel.
 */
bool cfg80211_rx_mgmt(struct net_device *dev, int freq, const u8 *buf,
		      size_t len, gfp_t gfp);

/**
 * cfg80211_mgmt_tx_status - notification of TX status for management frame
 * @dev: network device
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
void cfg80211_mgmt_tx_status(struct net_device *dev, u64 cookie,
			     const u8 *buf, size_t len, bool ack, gfp_t gfp);


/**
 * cfg80211_cqm_rssi_notify - connection quality monitoring rssi event
 * @dev: network device
 * @rssi_event: the triggered RSSI event
 * @gfp: context flags
 *
 * This function is called when a configured connection quality monitoring
 * rssi threshold reached event occurs.
 */
void cfg80211_cqm_rssi_notify(struct net_device *dev,
			      enum nl80211_cqm_rssi_threshold_event rssi_event,
			      gfp_t gfp);

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

#endif /* __NET_CFG80211_H */
