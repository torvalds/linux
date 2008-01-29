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
};

/**
 * enum station_stats_flags - station statistics flags
 *
 * Used by the driver to indicate which info in &struct station_stats
 * it has filled in during get_station().
 *
 * @STATION_STAT_INACTIVE_TIME: @inactive_time filled
 * @STATION_STAT_RX_BYTES: @rx_bytes filled
 * @STATION_STAT_TX_BYTES: @tx_bytes filled
 */
enum station_stats_flags {
	STATION_STAT_INACTIVE_TIME	= 1<<0,
	STATION_STAT_RX_BYTES		= 1<<1,
	STATION_STAT_TX_BYTES		= 1<<2,
};

/**
 * struct station_stats - station statistics
 *
 * Station information filled by driver for get_station().
 *
 * @filled: bitflag of flags from &enum station_stats_flags
 * @inactive_time: time since last station activity (tx/rx) in milliseconds
 * @rx_bytes: bytes received from this station
 * @tx_bytes: bytes transmitted to this station
 */
struct station_stats {
	u32 filled;
	u32 inactive_time;
	u32 rx_bytes;
	u32 tx_bytes;
};

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
 * @add_virtual_intf: create a new virtual interface with the given name
 *
 * @del_virtual_intf: remove the virtual interface determined by ifindex.
 *
 * @change_virtual_intf: change type of virtual interface
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
 */
struct cfg80211_ops {
	int	(*add_virtual_intf)(struct wiphy *wiphy, char *name,
				    enum nl80211_iftype type);
	int	(*del_virtual_intf)(struct wiphy *wiphy, int ifindex);
	int	(*change_virtual_intf)(struct wiphy *wiphy, int ifindex,
				       enum nl80211_iftype type);

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
			       u8 *mac, struct station_stats *stats);
};

#endif /* __NET_CFG80211_H */
