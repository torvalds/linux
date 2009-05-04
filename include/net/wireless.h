#ifndef __NET_WIRELESS_H
#define __NET_WIRELESS_H

/*
 * 802.11 device management
 *
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/netdevice.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>

/**
 * enum ieee80211_band - supported frequency bands
 *
 * The bands are assigned this way because the supported
 * bitrates differ in these bands.
 *
 * @IEEE80211_BAND_2GHZ: 2.4GHz ISM band
 * @IEEE80211_BAND_5GHZ: around 5GHz band (4.9-5.7)
 */
enum ieee80211_band {
	IEEE80211_BAND_2GHZ,
	IEEE80211_BAND_5GHZ,

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
 * @IEEE80211_CHAN_NO_FAT_ABOVE: extension channel above this channel
 * 	is not permitted.
 * @IEEE80211_CHAN_NO_FAT_BELOW: extension channel below this channel
 * 	is not permitted.
 */
enum ieee80211_channel_flags {
	IEEE80211_CHAN_DISABLED		= 1<<0,
	IEEE80211_CHAN_PASSIVE_SCAN	= 1<<1,
	IEEE80211_CHAN_NO_IBSS		= 1<<2,
	IEEE80211_CHAN_RADAR		= 1<<3,
	IEEE80211_CHAN_NO_FAT_ABOVE	= 1<<4,
	IEEE80211_CHAN_NO_FAT_BELOW	= 1<<5,
};

/**
 * struct ieee80211_channel - channel definition
 *
 * This structure describes a single channel for use
 * with cfg80211.
 *
 * @center_freq: center frequency in MHz
 * @max_bandwidth: maximum allowed bandwidth for this channel, in MHz
 * @hw_value: hardware-specific value for the channel
 * @flags: channel flags from &enum ieee80211_channel_flags.
 * @orig_flags: channel flags at registration time, used by regulatory
 *	code to support devices with additional restrictions
 * @band: band this channel belongs to.
 * @max_antenna_gain: maximum antenna gain in dBi
 * @max_power: maximum transmission power (in dBm)
 * @beacon_found: helper to regulatory code to indicate when a beacon
 *	has been found on this channel. Use regulatory_hint_found_beacon()
 *	to enable this, this is is useful only on 5 GHz band.
 * @orig_mag: internal use
 * @orig_mpwr: internal use
 */
struct ieee80211_channel {
	enum ieee80211_band band;
	u16 center_freq;
	u8 max_bandwidth;
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
 */
struct ieee80211_supported_band {
	struct ieee80211_channel *channels;
	struct ieee80211_rate *bitrates;
	enum ieee80211_band band;
	int n_channels;
	int n_bitrates;
	struct ieee80211_sta_ht_cap ht_cap;
};

/**
 * struct wiphy - wireless hardware description
 * @idx: the wiphy index assigned to this item
 * @class_dev: the class device representing /sys/class/ieee80211/<wiphy-name>
 * @custom_regulatory: tells us the driver for this device
 * 	has its own custom regulatory domain and cannot identify the
 * 	ISO / IEC 3166 alpha2 it belongs to. When this is enabled
 * 	we will disregard the first regulatory hint (when the
 * 	initiator is %REGDOM_SET_BY_CORE).
 * @strict_regulatory: tells us the driver for this device will ignore
 * 	regulatory domain settings until it gets its own regulatory domain
 * 	via its regulatory_hint(). After its gets its own regulatory domain
 * 	it will only allow further regulatory domain settings to further
 * 	enhance compliance. For example if channel 13 and 14 are disabled
 * 	by this regulatory domain no user regulatory domain can enable these
 * 	channels at a later time. This can be used for devices which do not
 * 	have calibration information gauranteed for frequencies or settings
 * 	outside of its regulatory domain.
 * @reg_notifier: the driver's regulatory notification callback
 * @regd: the driver's regulatory domain, if one was requested via
 * 	the regulatory_hint() API. This can be used by the driver
 *	on the reg_notifier() if it chooses to ignore future
 *	regulatory domain changes caused by other drivers.
 * @signal_type: signal type reported in &struct cfg80211_bss.
 */
struct wiphy {
	/* assign these fields before you register the wiphy */

	/* permanent MAC address */
	u8 perm_addr[ETH_ALEN];

	/* Supported interface modes, OR together BIT(NL80211_IFTYPE_...) */
	u16 interface_modes;

	bool custom_regulatory;
	bool strict_regulatory;

	enum cfg80211_signal_type signal_type;

	int bss_priv_size;
	u8 max_scan_ssids;

	/* If multiple wiphys are registered and you're handed e.g.
	 * a regular netdev with assigned ieee80211_ptr, you won't
	 * know whether it points to a wiphy your driver has registered
	 * or not. Assign this to something global to your driver to
	 * help determine whether you own this wiphy or not. */
	void *privid;

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

	char priv[0] __attribute__((__aligned__(NETDEV_ALIGN)));
};

/** struct wireless_dev - wireless per-netdev state
 *
 * This structure must be allocated by the driver/stack
 * that uses the ieee80211_ptr field in struct net_device
 * (this is intentional so it can be allocated along with
 * the netdev.)
 *
 * @wiphy: pointer to hardware description
 * @iftype: interface type
 */
struct wireless_dev {
	struct wiphy *wiphy;
	enum nl80211_iftype iftype;

	/* private to the generic wireless code */
	struct list_head list;
	struct net_device *netdev;
};

/**
 * wiphy_priv - return priv from wiphy
 */
static inline void *wiphy_priv(struct wiphy *wiphy)
{
	BUG_ON(!wiphy);
	return &wiphy->priv;
}

/**
 * set_wiphy_dev - set device pointer for wiphy
 */
static inline void set_wiphy_dev(struct wiphy *wiphy, struct device *dev)
{
	wiphy->dev.parent = dev;
}

/**
 * wiphy_dev - get wiphy dev pointer
 */
static inline struct device *wiphy_dev(struct wiphy *wiphy)
{
	return wiphy->dev.parent;
}

/**
 * wiphy_name - get wiphy name
 */
static inline const char *wiphy_name(struct wiphy *wiphy)
{
	return dev_name(&wiphy->dev);
}

/**
 * wdev_priv - return wiphy priv from wireless_dev
 */
static inline void *wdev_priv(struct wireless_dev *wdev)
{
	BUG_ON(!wdev);
	return wiphy_priv(wdev->wiphy);
}

/**
 * wiphy_new - create a new wiphy for use with cfg80211
 *
 * create a new wiphy and associate the given operations with it.
 * @sizeof_priv bytes are allocated for private use.
 *
 * the returned pointer must be assigned to each netdev's
 * ieee80211_ptr for proper operation.
 */
struct wiphy *wiphy_new(struct cfg80211_ops *ops, int sizeof_priv);

/**
 * wiphy_register - register a wiphy with cfg80211
 *
 * register the given wiphy
 *
 * Returns a non-negative wiphy index or a negative error code.
 */
extern int wiphy_register(struct wiphy *wiphy);

/**
 * wiphy_unregister - deregister a wiphy from cfg80211
 *
 * unregister a device with the given priv pointer.
 * After this call, no more requests can be made with this priv
 * pointer, but the call may sleep to wait for an outstanding
 * request that is being handled.
 */
extern void wiphy_unregister(struct wiphy *wiphy);

/**
 * wiphy_free - free wiphy
 */
extern void wiphy_free(struct wiphy *wiphy);

/**
 * ieee80211_channel_to_frequency - convert channel number to frequency
 */
extern int ieee80211_channel_to_frequency(int chan);

/**
 * ieee80211_frequency_to_channel - convert frequency to channel number
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
 * regulatory_hint_11d - hints a country IE as a regulatory domain
 * @wiphy: the wireless device giving the hint (used only for reporting
 *	conflicts)
 * @country_ie: pointer to the country IE
 * @country_ie_len: length of the country IE
 *
 * We will intersect the rd with the what CRDA tells us should apply
 * for the alpha2 this country IE belongs to, this prevents APs from
 * sending us incorrect or outdated information against a country.
 */
extern void regulatory_hint_11d(struct wiphy *wiphy,
				u8 *country_ie,
				u8 country_ie_len);
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
 * @bandwidth: the bandwidth requirement you have in KHz, if you do not have one
 * 	you can set this to 0. If this frequency is allowed we then set
 * 	this value to the maximum allowed bandwidth.
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
extern int freq_reg_info(struct wiphy *wiphy, u32 center_freq, u32 *bandwidth,
			 const struct ieee80211_reg_rule **reg_rule);

#endif /* __NET_WIRELESS_H */
