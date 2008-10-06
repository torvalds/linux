#ifndef __NET_CFG80211_H
#define __NET_CFG80211_H

#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/nl80211.h>
#include <net/genetlink.h>

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
 */
enum station_flags {
	STATION_FLAG_CHANGED		= 1<<0,
	STATION_FLAG_AUTHORIZED		= 1<<NL80211_STA_FLAG_AUTHORIZED,
	STATION_FLAG_SHORT_PREAMBLE	= 1<<NL80211_STA_FLAG_SHORT_PREAMBLE,
	STATION_FLAG_WME		= 1<<NL80211_STA_FLAG_WME,
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
 */
enum station_info_flags {
	STATION_INFO_INACTIVE_TIME	= 1<<0,
	STATION_INFO_RX_BYTES		= 1<<1,
	STATION_INFO_TX_BYTES		= 1<<2,
	STATION_INFO_LLID		= 1<<3,
	STATION_INFO_PLID		= 1<<4,
	STATION_INFO_PLINK_STATE	= 1<<5,
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
 */
struct station_info {
	u32 filled;
	u32 inactive_time;
	u32 rx_bytes;
	u32 tx_bytes;
	u16 llid;
	u16 plid;
	u8 plink_state;
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
 */
struct bss_parameters {
	int use_cts_prot;
	int use_short_preamble;
	int use_short_slot_time;
};

/**
 * enum reg_set_by - Indicates who is trying to set the regulatory domain
 * @REGDOM_SET_BY_INIT: regulatory domain was set by initialization. We will be
 * 	using a static world regulatory domain by default.
 * @REGDOM_SET_BY_CORE: Core queried CRDA for a dynamic world regulatory domain.
 * @REGDOM_SET_BY_USER: User asked the wireless core to set the
 * 	regulatory domain.
 * @REGDOM_SET_BY_DRIVER: a wireless drivers has hinted to the wireless core
 *	it thinks its knows the regulatory domain we should be in.
 * @REGDOM_SET_BY_COUNTRY_IE: the wireless core has received an 802.11 country
 *	information element with regulatory information it thinks we
 *	should consider.
 */
enum reg_set_by {
	REGDOM_SET_BY_INIT,
	REGDOM_SET_BY_CORE,
	REGDOM_SET_BY_USER,
	REGDOM_SET_BY_DRIVER,
	REGDOM_SET_BY_COUNTRY_IE,
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

#define MHZ_TO_KHZ(freq) (freq * 1000)
#define KHZ_TO_MHZ(freq) (freq / 1000)
#define DBI_TO_MBI(gain) (gain * 100)
#define MBI_TO_DBI(gain) (gain / 100)
#define DBM_TO_MBM(gain) (gain * 100)
#define MBM_TO_DBM(gain) (gain / 100)

#define REG_RULE(start, end, bw, gain, eirp, reg_flags) { \
	.freq_range.start_freq_khz = (start) * 1000, \
	.freq_range.end_freq_khz = (end) * 1000, \
	.freq_range.max_bandwidth_khz = (bw) * 1000, \
	.power_rule.max_antenna_gain = (gain) * 100, \
	.power_rule.max_eirp = (eirp) * 100, \
	.flags = reg_flags, \
	}

/* from net/wireless.h */
struct wiphy;

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
 * @set_mesh_cfg: set mesh parameters (by now, just mesh id)
 *
 * @change_bss: Modify parameters for a given BSS.
 */
struct cfg80211_ops {
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

	int	(*change_bss)(struct wiphy *wiphy, struct net_device *dev,
			      struct bss_parameters *params);
};

#endif /* __NET_CFG80211_H */
