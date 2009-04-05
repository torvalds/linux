#ifndef __NET_CFG80211_H
#define __NET_CFG80211_H

#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/nl80211.h>
#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <net/genetlink.h>
/* remove once we remove the wext stuff */
#include <net/iw_handler.h>

/*
 * 802.11 configuration in-kernel interface
 *
 * Copyright 2006, 2007	Johannes Berg <johannes@sipsolutions.net>
 */

/**
 * struct vif_params - describes virtual interface parameters
 * @mesh_id: mesh ID to use
 * @mesh_id_len: length of the mesh ID
 */
struct vif_params {
       u8 *mesh_id;
       int mesh_id_len;
};

/* Radiotap header iteration
 *   implemented in net/wireless/radiotap.c
 *   docs in Documentation/networking/radiotap-headers.txt
 */
/**
 * struct ieee80211_radiotap_iterator - tracks walk thru present radiotap args
 * @rtheader: pointer to the radiotap header we are walking through
 * @max_length: length of radiotap header in cpu byte ordering
 * @this_arg_index: IEEE80211_RADIOTAP_... index of current arg
 * @this_arg: pointer to current radiotap arg
 * @arg_index: internal next argument index
 * @arg: internal next argument pointer
 * @next_bitmap: internal pointer to next present u32
 * @bitmap_shifter: internal shifter for curr u32 bitmap, b0 set == arg present
 */

struct ieee80211_radiotap_iterator {
	struct ieee80211_radiotap_header *rtheader;
	int max_length;
	int this_arg_index;
	u8 *this_arg;

	int arg_index;
	u8 *arg;
	__le32 *next_bitmap;
	u32 bitmap_shifter;
};

extern int ieee80211_radiotap_iterator_init(
   struct ieee80211_radiotap_iterator *iterator,
   struct ieee80211_radiotap_header *radiotap_header,
   int max_length);

extern int ieee80211_radiotap_iterator_next(
   struct ieee80211_radiotap_iterator *iterator);


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
 */
struct key_params {
	u8 *key;
	u8 *seq;
	int key_len;
	int seq_len;
	u32 cipher;
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
 * enum station_flags - station flags
 *
 * Station capability flags. Note that these must be the bits
 * according to the nl80211 flags.
 *
 * @STATION_FLAG_CHANGED: station flags were changed
 * @STATION_FLAG_AUTHORIZED: station is authorized to send frames (802.1X)
 * @STATION_FLAG_SHORT_PREAMBLE: station is capable of receiving frames
 *	with short preambles
 * @STATION_FLAG_WME: station is WME/QoS capable
 * @STATION_FLAG_MFP: station uses management frame protection
 */
enum station_flags {
	STATION_FLAG_CHANGED		= 1<<0,
	STATION_FLAG_AUTHORIZED		= 1<<NL80211_STA_FLAG_AUTHORIZED,
	STATION_FLAG_SHORT_PREAMBLE	= 1<<NL80211_STA_FLAG_SHORT_PREAMBLE,
	STATION_FLAG_WME		= 1<<NL80211_STA_FLAG_WME,
	STATION_FLAG_MFP		= 1<<NL80211_STA_FLAG_MFP,
};

/**
 * enum plink_action - actions to perform in mesh peers
 *
 * @PLINK_ACTION_INVALID: action 0 is reserved
 * @PLINK_ACTION_OPEN: start mesh peer link establishment
 * @PLINK_ACTION_BLOCL: block traffic from this mesh peer
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
 * @station_flags: station flags (see &enum station_flags)
 * @listen_interval: listen interval or -1 for no change
 * @aid: AID or zero for no change
 */
struct station_parameters {
	u8 *supported_rates;
	struct net_device *vlan;
	u32 station_flags;
	int listen_interval;
	u16 aid;
	u8 supported_rates_len;
	u8 plink_action;
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
 * @STATION_INFO_TX_BITRATE: @tx_bitrate fields are filled
 *  (tx_bitrate, tx_bitrate_flags and tx_bitrate_mcs)
 * @STATION_INFO_RX_PACKETS: @rx_packets filled
 * @STATION_INFO_TX_PACKETS: @tx_packets filled
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
 * struct station_info - station information
 *
 * Station information filled by driver for get_station() and dump_station.
 *
 * @filled: bitflag of flags from &enum station_info_flags
 * @inactive_time: time since last station activity (tx/rx) in milliseconds
 * @rx_bytes: bytes received from this station
 * @tx_bytes: bytes transmitted to this station
 * @llid: mesh local link id
 * @plid: mesh peer link id
 * @plink_state: mesh peer link state
 * @signal: signal strength of last received packet in dBm
 * @txrate: current unicast bitrate to this station
 * @rx_packets: packets received from this station
 * @tx_packets: packets transmitted to this station
 */
struct station_info {
	u32 filled;
	u32 inactive_time;
	u32 rx_bytes;
	u32 tx_bytes;
	u16 llid;
	u16 plid;
	u8 plink_state;
	s8 signal;
	struct rate_info txrate;
	u32 rx_packets;
	u32 tx_packets;
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
 * MPATH_INFO_FRAME_QLEN: @frame_qlen filled
 * MPATH_INFO_DSN: @dsn filled
 * MPATH_INFO_METRIC: @metric filled
 * MPATH_INFO_EXPTIME: @exptime filled
 * MPATH_INFO_DISCOVERY_TIMEOUT: @discovery_timeout filled
 * MPATH_INFO_DISCOVERY_RETRIES: @discovery_retries filled
 * MPATH_INFO_FLAGS: @flags filled
 */
enum mpath_info_flags {
	MPATH_INFO_FRAME_QLEN		= BIT(0),
	MPATH_INFO_DSN			= BIT(1),
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
 * @dsn: destination sequence number
 * @metric: metric (cost) of this mesh path
 * @exptime: expiration time for the mesh path from now, in msecs
 * @flags: mesh path flags
 * @discovery_timeout: total mesh path discovery timeout, in msecs
 * @discovery_retries: mesh path discovery retries
 */
struct mpath_info {
	u32 filled;
	u32 frame_qlen;
	u32 dsn;
	u32 metric;
	u32 exptime;
	u32 discovery_timeout;
	u8 discovery_retries;
	u8 flags;
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
 */
struct bss_parameters {
	int use_cts_prot;
	int use_short_preamble;
	int use_short_slot_time;
	u8 *basic_rates;
	u8 basic_rates_len;
};

/**
 * enum environment_cap - Environment parsed from country IE
 * @ENVIRON_ANY: indicates country IE applies to both indoor and
 *	outdoor operation.
 * @ENVIRON_INDOOR: indicates country IE applies only to indoor operation
 * @ENVIRON_OUTDOOR: indicates country IE applies only to outdoor operation
 */
enum environment_cap {
	ENVIRON_ANY,
	ENVIRON_INDOOR,
	ENVIRON_OUTDOOR,
};

/**
 * struct regulatory_request - used to keep track of regulatory requests
 *
 * @wiphy_idx: this is set if this request's initiator is
 * 	%REGDOM_SET_BY_COUNTRY_IE or %REGDOM_SET_BY_DRIVER. This
 * 	can be used by the wireless core to deal with conflicts
 * 	and potentially inform users of which devices specifically
 * 	cased the conflicts.
 * @initiator: indicates who sent this request, could be any of
 * 	of those set in nl80211_reg_initiator (%NL80211_REGDOM_SET_BY_*)
 * @alpha2: the ISO / IEC 3166 alpha2 country code of the requested
 * 	regulatory domain. We have a few special codes:
 * 	00 - World regulatory domain
 * 	99 - built by driver but a specific alpha2 cannot be determined
 * 	98 - result of an intersection between two regulatory domains
 * @intersect: indicates whether the wireless core should intersect
 * 	the requested regulatory domain with the presently set regulatory
 * 	domain.
 * @country_ie_checksum: checksum of the last processed and accepted
 * 	country IE
 * @country_ie_env: lets us know if the AP is telling us we are outdoor,
 * 	indoor, or if it doesn't matter
 * @list: used to insert into the reg_requests_list linked list
 */
struct regulatory_request {
	int wiphy_idx;
	enum nl80211_reg_initiator initiator;
	char alpha2[2];
	bool intersect;
	u32 country_ie_checksum;
	enum environment_cap country_ie_env;
	struct list_head list;
};

struct ieee80211_freq_range {
	u32 start_freq_khz;
	u32 end_freq_khz;
	u32 max_bandwidth_khz;
};

struct ieee80211_power_rule {
	u32 max_antenna_gain;
	u32 max_eirp;
};

struct ieee80211_reg_rule {
	struct ieee80211_freq_range freq_range;
	struct ieee80211_power_rule power_rule;
	u32 flags;
};

struct ieee80211_regdomain {
	u32 n_reg_rules;
	char alpha2[2];
	struct ieee80211_reg_rule reg_rules[];
};

#define MHZ_TO_KHZ(freq) ((freq) * 1000)
#define KHZ_TO_MHZ(freq) ((freq) / 1000)
#define DBI_TO_MBI(gain) ((gain) * 100)
#define MBI_TO_DBI(gain) ((gain) / 100)
#define DBM_TO_MBM(gain) ((gain) * 100)
#define MBM_TO_DBM(gain) ((gain) / 100)

#define REG_RULE(start, end, bw, gain, eirp, reg_flags) { \
	.freq_range.start_freq_khz = MHZ_TO_KHZ(start), \
	.freq_range.end_freq_khz = MHZ_TO_KHZ(end), \
	.freq_range.max_bandwidth_khz = MHZ_TO_KHZ(bw), \
	.power_rule.max_antenna_gain = DBI_TO_MBI(gain), \
	.power_rule.max_eirp = DBM_TO_MBM(eirp), \
	.flags = reg_flags, \
	}

struct mesh_config {
	/* Timeouts in ms */
	/* Mesh plink management parameters */
	u16 dot11MeshRetryTimeout;
	u16 dot11MeshConfirmTimeout;
	u16 dot11MeshHoldingTimeout;
	u16 dot11MeshMaxPeerLinks;
	u8  dot11MeshMaxRetries;
	u8  dot11MeshTTL;
	bool auto_open_plinks;
	/* HWMP parameters */
	u8  dot11MeshHWMPmaxPREQretries;
	u32 path_refresh_time;
	u16 min_discovery_timeout;
	u32 dot11MeshHWMPactivePathTimeout;
	u16 dot11MeshHWMPpreqMinInterval;
	u16 dot11MeshHWMPnetDiameterTraversalTime;
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

/* from net/ieee80211.h */
struct ieee80211_channel;

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
 * @n_channels: number of channels for each band
 * @ie: optional information element(s) to add into Probe Request or %NULL
 * @ie_len: length of ie in octets
 * @wiphy: the wiphy this was for
 * @ifidx: the interface index
 */
struct cfg80211_scan_request {
	struct cfg80211_ssid *ssids;
	int n_ssids;
	struct ieee80211_channel **channels;
	u32 n_channels;
	u8 *ie;
	size_t ie_len;

	/* internal */
	struct wiphy *wiphy;
	int ifidx;
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
 * @bssid: BSSID of the BSS
 * @tsf: timestamp of last received update
 * @beacon_interval: the beacon interval as from the frame
 * @capability: the capability field in host byte order
 * @information_elements: the information elements (Note that there
 *	is no guarantee that these are well-formed!)
 * @len_information_elements: total length of the information elements
 * @signal: signal strength value (type depends on the wiphy's signal_type)
 * @hold: BSS should not expire
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

	s32 signal;

	void (*free_priv)(struct cfg80211_bss *bss);
	u8 priv[0] __attribute__((__aligned__(sizeof(void *))));
};

/**
 * struct cfg80211_auth_request - Authentication request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * authentication.
 * NOTE: This structure will likely change when more code from mac80211 is
 * moved into cfg80211 so that non-mac80211 drivers can benefit from it, too.
 * Before using this in a driver that does not use mac80211, it would be better
 * to check the status of that work and better yet, volunteer to work on it.
 *
 * @chan: The channel to use or %NULL if not specified (auto-select based on
 *	scan results)
 * @peer_addr: The address of the peer STA (AP BSSID in infrastructure case);
 *	this field is required to be present; if the driver wants to help with
 *	BSS selection, it should use (yet to be added) MLME event to allow user
 *	space SME to be notified of roaming candidate, so that the SME can then
 *	use the authentication request with the recommended BSSID and whatever
 *	other data may be needed for authentication/association
 * @ssid: SSID or %NULL if not yet available
 * @ssid_len: Length of ssid in octets
 * @auth_type: Authentication type (algorithm)
 * @ie: Extra IEs to add to Authentication frame or %NULL
 * @ie_len: Length of ie buffer in octets
 */
struct cfg80211_auth_request {
	struct ieee80211_channel *chan;
	u8 *peer_addr;
	const u8 *ssid;
	size_t ssid_len;
	enum nl80211_auth_type auth_type;
	const u8 *ie;
	size_t ie_len;
};

/**
 * struct cfg80211_assoc_request - (Re)Association request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * (re)association.
 * NOTE: This structure will likely change when more code from mac80211 is
 * moved into cfg80211 so that non-mac80211 drivers can benefit from it, too.
 * Before using this in a driver that does not use mac80211, it would be better
 * to check the status of that work and better yet, volunteer to work on it.
 *
 * @chan: The channel to use or %NULL if not specified (auto-select based on
 *	scan results)
 * @peer_addr: The address of the peer STA (AP BSSID); this field is required
 *	to be present and the STA must be in State 2 (authenticated) with the
 *	peer STA
 * @ssid: SSID
 * @ssid_len: Length of ssid in octets
 * @ie: Extra IEs to add to (Re)Association Request frame or %NULL
 * @ie_len: Length of ie buffer in octets
 */
struct cfg80211_assoc_request {
	struct ieee80211_channel *chan;
	u8 *peer_addr;
	const u8 *ssid;
	size_t ssid_len;
	const u8 *ie;
	size_t ie_len;
};

/**
 * struct cfg80211_deauth_request - Deauthentication request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * deauthentication.
 *
 * @peer_addr: The address of the peer STA (AP BSSID); this field is required
 *	to be present and the STA must be authenticated with the peer STA
 * @ie: Extra IEs to add to Deauthentication frame or %NULL
 * @ie_len: Length of ie buffer in octets
 */
struct cfg80211_deauth_request {
	u8 *peer_addr;
	u16 reason_code;
	const u8 *ie;
	size_t ie_len;
};

/**
 * struct cfg80211_disassoc_request - Disassociation request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * disassocation.
 *
 * @peer_addr: The address of the peer STA (AP BSSID); this field is required
 *	to be present and the STA must be associated with the peer STA
 * @ie: Extra IEs to add to Disassociation frame or %NULL
 * @ie_len: Length of ie buffer in octets
 */
struct cfg80211_disassoc_request {
	u8 *peer_addr;
	u16 reason_code;
	const u8 *ie;
	size_t ie_len;
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
 * @suspend: wiphy device needs to be suspended
 * @resume: wiphy device needs to be resumed
 *
 * @add_virtual_intf: create a new virtual interface with the given name,
 *	must set the struct wireless_dev's iftype.
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
 *	after it returns.
 *
 * @del_key: remove a key given the @mac_addr (%NULL for a group key)
 *	and @key_index
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
 *
 * @del_station: Remove a station; @mac may be NULL to remove all stations.
 *
 * @change_station: Modify a given station.
 *
 * @get_mesh_params: Put the current mesh parameters into *params
 *
 * @set_mesh_params: Set mesh parameters.
 *	The mask is a bitfield which tells us which parameters to
 *	set, and which to leave alone.
 *
 * @set_mesh_cfg: set mesh parameters (by now, just mesh id)
 *
 * @change_bss: Modify parameters for a given BSS.
 *
 * @set_txq_params: Set TX queue parameters
 *
 * @set_channel: Set channel
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
 */
struct cfg80211_ops {
	int	(*suspend)(struct wiphy *wiphy);
	int	(*resume)(struct wiphy *wiphy);

	int	(*add_virtual_intf)(struct wiphy *wiphy, char *name,
				    enum nl80211_iftype type, u32 *flags,
				    struct vif_params *params);
	int	(*del_virtual_intf)(struct wiphy *wiphy, int ifindex);
	int	(*change_virtual_intf)(struct wiphy *wiphy, int ifindex,
				       enum nl80211_iftype type, u32 *flags,
				       struct vif_params *params);

	int	(*add_key)(struct wiphy *wiphy, struct net_device *netdev,
			   u8 key_index, u8 *mac_addr,
			   struct key_params *params);
	int	(*get_key)(struct wiphy *wiphy, struct net_device *netdev,
			   u8 key_index, u8 *mac_addr, void *cookie,
			   void (*callback)(void *cookie, struct key_params*));
	int	(*del_key)(struct wiphy *wiphy, struct net_device *netdev,
			   u8 key_index, u8 *mac_addr);
	int	(*set_default_key)(struct wiphy *wiphy,
				   struct net_device *netdev,
				   u8 key_index);
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
	int	(*get_mesh_params)(struct wiphy *wiphy,
				struct net_device *dev,
				struct mesh_config *conf);
	int	(*set_mesh_params)(struct wiphy *wiphy,
				struct net_device *dev,
				const struct mesh_config *nconf, u32 mask);
	int	(*change_bss)(struct wiphy *wiphy, struct net_device *dev,
			      struct bss_parameters *params);

	int	(*set_txq_params)(struct wiphy *wiphy,
				  struct ieee80211_txq_params *params);

	int	(*set_channel)(struct wiphy *wiphy,
			       struct ieee80211_channel *chan,
			       enum nl80211_channel_type channel_type);

	int	(*scan)(struct wiphy *wiphy, struct net_device *dev,
			struct cfg80211_scan_request *request);

	int	(*auth)(struct wiphy *wiphy, struct net_device *dev,
			struct cfg80211_auth_request *req);
	int	(*assoc)(struct wiphy *wiphy, struct net_device *dev,
			 struct cfg80211_assoc_request *req);
	int	(*deauth)(struct wiphy *wiphy, struct net_device *dev,
			  struct cfg80211_deauth_request *req);
	int	(*disassoc)(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_disassoc_request *req);
};

/* temporary wext handlers */
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
int cfg80211_wext_giwrange(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *extra);

/**
 * cfg80211_scan_done - notify that scan finished
 *
 * @request: the corresponding scan request
 * @aborted: set to true if the scan was aborted for any reason,
 *	userspace will be notified of that
 */
void cfg80211_scan_done(struct cfg80211_scan_request *request, bool aborted);

/**
 * cfg80211_inform_bss - inform cfg80211 of a new BSS
 *
 * @wiphy: the wiphy reporting the BSS
 * @bss: the found BSS
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
 * station mode.
 */
void cfg80211_send_rx_auth(struct net_device *dev, const u8 *buf, size_t len);

/**
 * cfg80211_send_rx_assoc - notification of processed association
 * @dev: network device
 * @buf: (re)association response frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever a (re)association response has been
 * processed in station mode.
 */
void cfg80211_send_rx_assoc(struct net_device *dev, const u8 *buf, size_t len);

/**
 * cfg80211_send_rx_deauth - notification of processed deauthentication
 * @dev: network device
 * @buf: deauthentication frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever deauthentication has been processed in
 * station mode.
 */
void cfg80211_send_rx_deauth(struct net_device *dev, const u8 *buf,
			     size_t len);

/**
 * cfg80211_send_rx_disassoc - notification of processed disassociation
 * @dev: network device
 * @buf: disassociation response frame (header + body)
 * @len: length of the frame data
 *
 * This function is called whenever disassociation has been processed in
 * station mode.
 */
void cfg80211_send_rx_disassoc(struct net_device *dev, const u8 *buf,
			       size_t len);

/**
 * cfg80211_hold_bss - exclude bss from expiration
 * @bss: bss which should not expire
 *
 * In a case when the BSS is not updated but it shouldn't expire this
 * function can be used to mark the BSS to be excluded from expiration.
 */
void cfg80211_hold_bss(struct cfg80211_bss *bss);

/**
 * cfg80211_unhold_bss - remove expiration exception from the BSS
 * @bss: bss which can expire again
 *
 * This function marks the BSS to be expirable again.
 */
void cfg80211_unhold_bss(struct cfg80211_bss *bss);

#endif /* __NET_CFG80211_H */
