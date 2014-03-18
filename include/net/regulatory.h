#ifndef __NET_REGULATORY_H
#define __NET_REGULATORY_H
/*
 * regulatory support structures
 *
 * Copyright 2008-2009	Luis R. Rodriguez <mcgrof@qca.qualcomm.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/rcupdate.h>

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
 * @rcu_head: RCU head struct used to free the request
 * @wiphy_idx: this is set if this request's initiator is
 *	%REGDOM_SET_BY_COUNTRY_IE or %REGDOM_SET_BY_DRIVER. This
 *	can be used by the wireless core to deal with conflicts
 *	and potentially inform users of which devices specifically
 *	cased the conflicts.
 * @initiator: indicates who sent this request, could be any of
 *	of those set in nl80211_reg_initiator (%NL80211_REGDOM_SET_BY_*)
 * @alpha2: the ISO / IEC 3166 alpha2 country code of the requested
 *	regulatory domain. We have a few special codes:
 *	00 - World regulatory domain
 *	99 - built by driver but a specific alpha2 cannot be determined
 *	98 - result of an intersection between two regulatory domains
 *	97 - regulatory domain has not yet been configured
 * @dfs_region: If CRDA responded with a regulatory domain that requires
 *	DFS master operation on a known DFS region (NL80211_DFS_*),
 *	dfs_region represents that region. Drivers can use this and the
 *	@alpha2 to adjust their device's DFS parameters as required.
 * @user_reg_hint_type: if the @initiator was of type
 *	%NL80211_REGDOM_SET_BY_USER, this classifies the type
 *	of hint passed. This could be any of the %NL80211_USER_REG_HINT_*
 *	types.
 * @intersect: indicates whether the wireless core should intersect
 *	the requested regulatory domain with the presently set regulatory
 *	domain.
 * @processed: indicates whether or not this requests has already been
 *	processed. When the last request is processed it means that the
 *	currently regulatory domain set on cfg80211 is updated from
 *	CRDA and can be used by other regulatory requests. When a
 *	the last request is not yet processed we must yield until it
 *	is processed before processing any new requests.
 * @country_ie_checksum: checksum of the last processed and accepted
 *	country IE
 * @country_ie_env: lets us know if the AP is telling us we are outdoor,
 *	indoor, or if it doesn't matter
 * @list: used to insert into the reg_requests_list linked list
 */
struct regulatory_request {
	struct rcu_head rcu_head;
	int wiphy_idx;
	enum nl80211_reg_initiator initiator;
	enum nl80211_user_reg_hint_type user_reg_hint_type;
	char alpha2[2];
	enum nl80211_dfs_regions dfs_region;
	bool intersect;
	bool processed;
	enum environment_cap country_ie_env;
	struct list_head list;
};

/**
 * enum ieee80211_regulatory_flags - device regulatory flags
 *
 * @REGULATORY_CUSTOM_REG: tells us the driver for this device
 *	has its own custom regulatory domain and cannot identify the
 *	ISO / IEC 3166 alpha2 it belongs to. When this is enabled
 *	we will disregard the first regulatory hint (when the
 *	initiator is %REGDOM_SET_BY_CORE). Drivers that use
 *	wiphy_apply_custom_regulatory() should have this flag set
 *	or the regulatory core will set it for the wiphy.
 *	If you use regulatory_hint() *after* using
 *	wiphy_apply_custom_regulatory() the wireless core will
 *	clear the REGULATORY_CUSTOM_REG for your wiphy as it would be
 *	implied that the device somehow gained knowledge of its region.
 * @REGULATORY_STRICT_REG: tells us that the wiphy for this device
 *	has regulatory domain that it wishes to be considered as the
 *	superset for regulatory rules. After this device gets its regulatory
 *	domain programmed further regulatory hints shall only be considered
 *	for this device to enhance regulatory compliance, forcing the
 *	device to only possibly use subsets of the original regulatory
 *	rules. For example if channel 13 and 14 are disabled by this
 *	device's regulatory domain no user specified regulatory hint which
 *	has these channels enabled would enable them for this wiphy,
 *	the device's original regulatory domain will be trusted as the
 *	base. You can program the superset of regulatory rules for this
 *	wiphy with regulatory_hint() for cards programmed with an
 *	ISO3166-alpha2 country code. wiphys that use regulatory_hint()
 *	will have their wiphy->regd programmed once the regulatory
 *	domain is set, and all other regulatory hints will be ignored
 *	until their own regulatory domain gets programmed.
 * @REGULATORY_DISABLE_BEACON_HINTS: enable this if your driver needs to
 *	ensure that passive scan flags and beaconing flags may not be lifted by
 *	cfg80211 due to regulatory beacon hints. For more information on beacon
 *	hints read the documenation for regulatory_hint_found_beacon()
 * @REGULATORY_COUNTRY_IE_FOLLOW_POWER:  for devices that have a preference
 *	that even though they may have programmed their own custom power
 *	setting prior to wiphy registration, they want to ensure their channel
 *	power settings are updated for this connection with the power settings
 *	derived from the regulatory domain. The regulatory domain used will be
 *	based on the ISO3166-alpha2 from country IE provided through
 *	regulatory_hint_country_ie()
 * @REGULATORY_COUNTRY_IE_IGNORE: for devices that have a preference to ignore
 * 	all country IE information processed by the regulatory core. This will
 * 	override %REGULATORY_COUNTRY_IE_FOLLOW_POWER as all country IEs will
 * 	be ignored.
 */
enum ieee80211_regulatory_flags {
	REGULATORY_CUSTOM_REG			= BIT(0),
	REGULATORY_STRICT_REG			= BIT(1),
	REGULATORY_DISABLE_BEACON_HINTS		= BIT(2),
	REGULATORY_COUNTRY_IE_FOLLOW_POWER	= BIT(3),
	REGULATORY_COUNTRY_IE_IGNORE		= BIT(4),
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
	struct rcu_head rcu_head;
	u32 n_reg_rules;
	char alpha2[2];
	enum nl80211_dfs_regions dfs_region;
	struct ieee80211_reg_rule reg_rules[];
};

#define MHZ_TO_KHZ(freq) ((freq) * 1000)
#define KHZ_TO_MHZ(freq) ((freq) / 1000)
#define DBI_TO_MBI(gain) ((gain) * 100)
#define MBI_TO_DBI(gain) ((gain) / 100)
#define DBM_TO_MBM(gain) ((gain) * 100)
#define MBM_TO_DBM(gain) ((gain) / 100)

#define REG_RULE(start, end, bw, gain, eirp, reg_flags) \
{							\
	.freq_range.start_freq_khz = MHZ_TO_KHZ(start),	\
	.freq_range.end_freq_khz = MHZ_TO_KHZ(end),	\
	.freq_range.max_bandwidth_khz = MHZ_TO_KHZ(bw),	\
	.power_rule.max_antenna_gain = DBI_TO_MBI(gain),\
	.power_rule.max_eirp = DBM_TO_MBM(eirp),	\
	.flags = reg_flags,				\
}

#endif
