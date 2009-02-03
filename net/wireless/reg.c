/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2008	Luis R. Rodriguez <lrodriguz@atheros.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * DOC: Wireless regulatory infrastructure
 *
 * The usual implementation is for a driver to read a device EEPROM to
 * determine which regulatory domain it should be operating under, then
 * looking up the allowable channels in a driver-local table and finally
 * registering those channels in the wiphy structure.
 *
 * Another set of compliance enforcement is for drivers to use their
 * own compliance limits which can be stored on the EEPROM. The host
 * driver or firmware may ensure these are used.
 *
 * In addition to all this we provide an extra layer of regulatory
 * conformance. For drivers which do not have any regulatory
 * information CRDA provides the complete regulatory solution.
 * For others it provides a community effort on further restrictions
 * to enhance compliance.
 *
 * Note: When number of rules --> infinity we will not be able to
 * index on alpha2 any more, instead we'll probably have to
 * rely on some SHA1 checksum of the regdomain for example.
 *
 */
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/random.h>
#include <linux/nl80211.h>
#include <linux/platform_device.h>
#include <net/wireless.h>
#include <net/cfg80211.h>
#include "core.h"
#include "reg.h"

/**
 * struct regulatory_request - receipt of last regulatory request
 *
 * @wiphy: this is set if this request's initiator is
 * 	%REGDOM_SET_BY_COUNTRY_IE or %REGDOM_SET_BY_DRIVER. This
 * 	can be used by the wireless core to deal with conflicts
 * 	and potentially inform users of which devices specifically
 * 	cased the conflicts.
 * @initiator: indicates who sent this request, could be any of
 * 	of those set in reg_set_by, %REGDOM_SET_BY_*
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
 */
struct regulatory_request {
	struct wiphy *wiphy;
	enum reg_set_by initiator;
	char alpha2[2];
	bool intersect;
	u32 country_ie_checksum;
	enum environment_cap country_ie_env;
};

/* Receipt of information from last regulatory request */
static struct regulatory_request *last_request;

/* To trigger userspace events */
static struct platform_device *reg_pdev;

/* Keep the ordering from large to small */
static u32 supported_bandwidths[] = {
	MHZ_TO_KHZ(40),
	MHZ_TO_KHZ(20),
};

/* Central wireless core regulatory domains, we only need two,
 * the current one and a world regulatory domain in case we have no
 * information to give us an alpha2 */
static const struct ieee80211_regdomain *cfg80211_regdomain;

/* We use this as a place for the rd structure built from the
 * last parsed country IE to rest until CRDA gets back to us with
 * what it thinks should apply for the same country */
static const struct ieee80211_regdomain *country_ie_regdomain;

/* We keep a static world regulatory domain in case of the absence of CRDA */
static const struct ieee80211_regdomain world_regdom = {
	.n_reg_rules = 1,
	.alpha2 =  "00",
	.reg_rules = {
		REG_RULE(2412-10, 2462+10, 40, 6, 20,
			NL80211_RRF_PASSIVE_SCAN |
			NL80211_RRF_NO_IBSS),
	}
};

static const struct ieee80211_regdomain *cfg80211_world_regdom =
	&world_regdom;

#ifdef CONFIG_WIRELESS_OLD_REGULATORY
static char *ieee80211_regdom = "US";
module_param(ieee80211_regdom, charp, 0444);
MODULE_PARM_DESC(ieee80211_regdom, "IEEE 802.11 regulatory domain code");

/* We assume 40 MHz bandwidth for the old regulatory work.
 * We make emphasis we are using the exact same frequencies
 * as before */

static const struct ieee80211_regdomain us_regdom = {
	.n_reg_rules = 6,
	.alpha2 =  "US",
	.reg_rules = {
		/* IEEE 802.11b/g, channels 1..11 */
		REG_RULE(2412-10, 2462+10, 40, 6, 27, 0),
		/* IEEE 802.11a, channel 36 */
		REG_RULE(5180-10, 5180+10, 40, 6, 23, 0),
		/* IEEE 802.11a, channel 40 */
		REG_RULE(5200-10, 5200+10, 40, 6, 23, 0),
		/* IEEE 802.11a, channel 44 */
		REG_RULE(5220-10, 5220+10, 40, 6, 23, 0),
		/* IEEE 802.11a, channels 48..64 */
		REG_RULE(5240-10, 5320+10, 40, 6, 23, 0),
		/* IEEE 802.11a, channels 149..165, outdoor */
		REG_RULE(5745-10, 5825+10, 40, 6, 30, 0),
	}
};

static const struct ieee80211_regdomain jp_regdom = {
	.n_reg_rules = 3,
	.alpha2 =  "JP",
	.reg_rules = {
		/* IEEE 802.11b/g, channels 1..14 */
		REG_RULE(2412-10, 2484+10, 40, 6, 20, 0),
		/* IEEE 802.11a, channels 34..48 */
		REG_RULE(5170-10, 5240+10, 40, 6, 20,
			NL80211_RRF_PASSIVE_SCAN),
		/* IEEE 802.11a, channels 52..64 */
		REG_RULE(5260-10, 5320+10, 40, 6, 20,
			NL80211_RRF_NO_IBSS |
			NL80211_RRF_DFS),
	}
};

static const struct ieee80211_regdomain eu_regdom = {
	.n_reg_rules = 6,
	/* This alpha2 is bogus, we leave it here just for stupid
	 * backward compatibility */
	.alpha2 =  "EU",
	.reg_rules = {
		/* IEEE 802.11b/g, channels 1..13 */
		REG_RULE(2412-10, 2472+10, 40, 6, 20, 0),
		/* IEEE 802.11a, channel 36 */
		REG_RULE(5180-10, 5180+10, 40, 6, 23,
			NL80211_RRF_PASSIVE_SCAN),
		/* IEEE 802.11a, channel 40 */
		REG_RULE(5200-10, 5200+10, 40, 6, 23,
			NL80211_RRF_PASSIVE_SCAN),
		/* IEEE 802.11a, channel 44 */
		REG_RULE(5220-10, 5220+10, 40, 6, 23,
			NL80211_RRF_PASSIVE_SCAN),
		/* IEEE 802.11a, channels 48..64 */
		REG_RULE(5240-10, 5320+10, 40, 6, 20,
			NL80211_RRF_NO_IBSS |
			NL80211_RRF_DFS),
		/* IEEE 802.11a, channels 100..140 */
		REG_RULE(5500-10, 5700+10, 40, 6, 30,
			NL80211_RRF_NO_IBSS |
			NL80211_RRF_DFS),
	}
};

static const struct ieee80211_regdomain *static_regdom(char *alpha2)
{
	if (alpha2[0] == 'U' && alpha2[1] == 'S')
		return &us_regdom;
	if (alpha2[0] == 'J' && alpha2[1] == 'P')
		return &jp_regdom;
	if (alpha2[0] == 'E' && alpha2[1] == 'U')
		return &eu_regdom;
	/* Default, as per the old rules */
	return &us_regdom;
}

static bool is_old_static_regdom(const struct ieee80211_regdomain *rd)
{
	if (rd == &us_regdom || rd == &jp_regdom || rd == &eu_regdom)
		return true;
	return false;
}
#else
static inline bool is_old_static_regdom(const struct ieee80211_regdomain *rd)
{
	return false;
}
#endif

static void reset_regdomains(void)
{
	/* avoid freeing static information or freeing something twice */
	if (cfg80211_regdomain == cfg80211_world_regdom)
		cfg80211_regdomain = NULL;
	if (cfg80211_world_regdom == &world_regdom)
		cfg80211_world_regdom = NULL;
	if (cfg80211_regdomain == &world_regdom)
		cfg80211_regdomain = NULL;
	if (is_old_static_regdom(cfg80211_regdomain))
		cfg80211_regdomain = NULL;

	kfree(cfg80211_regdomain);
	kfree(cfg80211_world_regdom);

	cfg80211_world_regdom = &world_regdom;
	cfg80211_regdomain = NULL;
}

/* Dynamic world regulatory domain requested by the wireless
 * core upon initialization */
static void update_world_regdomain(const struct ieee80211_regdomain *rd)
{
	BUG_ON(!last_request);

	reset_regdomains();

	cfg80211_world_regdom = rd;
	cfg80211_regdomain = rd;
}

bool is_world_regdom(const char *alpha2)
{
	if (!alpha2)
		return false;
	if (alpha2[0] == '0' && alpha2[1] == '0')
		return true;
	return false;
}

static bool is_alpha2_set(const char *alpha2)
{
	if (!alpha2)
		return false;
	if (alpha2[0] != 0 && alpha2[1] != 0)
		return true;
	return false;
}

static bool is_alpha_upper(char letter)
{
	/* ASCII A - Z */
	if (letter >= 65 && letter <= 90)
		return true;
	return false;
}

static bool is_unknown_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;
	/* Special case where regulatory domain was built by driver
	 * but a specific alpha2 cannot be determined */
	if (alpha2[0] == '9' && alpha2[1] == '9')
		return true;
	return false;
}

static bool is_intersected_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;
	/* Special case where regulatory domain is the
	 * result of an intersection between two regulatory domain
	 * structures */
	if (alpha2[0] == '9' && alpha2[1] == '8')
		return true;
	return false;
}

static bool is_an_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;
	if (is_alpha_upper(alpha2[0]) && is_alpha_upper(alpha2[1]))
		return true;
	return false;
}

static bool alpha2_equal(const char *alpha2_x, const char *alpha2_y)
{
	if (!alpha2_x || !alpha2_y)
		return false;
	if (alpha2_x[0] == alpha2_y[0] &&
		alpha2_x[1] == alpha2_y[1])
		return true;
	return false;
}

static bool regdom_changed(const char *alpha2)
{
	if (!cfg80211_regdomain)
		return true;
	if (alpha2_equal(cfg80211_regdomain->alpha2, alpha2))
		return false;
	return true;
}

/**
 * country_ie_integrity_changes - tells us if the country IE has changed
 * @checksum: checksum of country IE of fields we are interested in
 *
 * If the country IE has not changed you can ignore it safely. This is
 * useful to determine if two devices are seeing two different country IEs
 * even on the same alpha2. Note that this will return false if no IE has
 * been set on the wireless core yet.
 */
static bool country_ie_integrity_changes(u32 checksum)
{
	/* If no IE has been set then the checksum doesn't change */
	if (unlikely(!last_request->country_ie_checksum))
		return false;
	if (unlikely(last_request->country_ie_checksum != checksum))
		return true;
	return false;
}

/* This lets us keep regulatory code which is updated on a regulatory
 * basis in userspace. */
static int call_crda(const char *alpha2)
{
	char country_env[9 + 2] = "COUNTRY=";
	char *envp[] = {
		country_env,
		NULL
	};

	if (!is_world_regdom((char *) alpha2))
		printk(KERN_INFO "cfg80211: Calling CRDA for country: %c%c\n",
			alpha2[0], alpha2[1]);
	else
		printk(KERN_INFO "cfg80211: Calling CRDA to update world "
			"regulatory domain\n");

	country_env[8] = alpha2[0];
	country_env[9] = alpha2[1];

	return kobject_uevent_env(&reg_pdev->dev.kobj, KOBJ_CHANGE, envp);
}

/* Used by nl80211 before kmalloc'ing our regulatory domain */
bool reg_is_valid_request(const char *alpha2)
{
	if (!last_request)
		return false;

	return alpha2_equal(last_request->alpha2, alpha2);
}

/* Sanity check on a regulatory rule */
static bool is_valid_reg_rule(const struct ieee80211_reg_rule *rule)
{
	const struct ieee80211_freq_range *freq_range = &rule->freq_range;
	u32 freq_diff;

	if (freq_range->start_freq_khz <= 0 || freq_range->end_freq_khz <= 0)
		return false;

	if (freq_range->start_freq_khz > freq_range->end_freq_khz)
		return false;

	freq_diff = freq_range->end_freq_khz - freq_range->start_freq_khz;

	if (freq_diff <= 0 || freq_range->max_bandwidth_khz > freq_diff)
		return false;

	return true;
}

static bool is_valid_rd(const struct ieee80211_regdomain *rd)
{
	const struct ieee80211_reg_rule *reg_rule = NULL;
	unsigned int i;

	if (!rd->n_reg_rules)
		return false;

	if (WARN_ON(rd->n_reg_rules > NL80211_MAX_SUPP_REG_RULES))
		return false;

	for (i = 0; i < rd->n_reg_rules; i++) {
		reg_rule = &rd->reg_rules[i];
		if (!is_valid_reg_rule(reg_rule))
			return false;
	}

	return true;
}

/* Returns value in KHz */
static u32 freq_max_bandwidth(const struct ieee80211_freq_range *freq_range,
	u32 freq)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(supported_bandwidths); i++) {
		u32 start_freq_khz = freq - supported_bandwidths[i]/2;
		u32 end_freq_khz = freq + supported_bandwidths[i]/2;
		if (start_freq_khz >= freq_range->start_freq_khz &&
			end_freq_khz <= freq_range->end_freq_khz)
			return supported_bandwidths[i];
	}
	return 0;
}

/**
 * freq_in_rule_band - tells us if a frequency is in a frequency band
 * @freq_range: frequency rule we want to query
 * @freq_khz: frequency we are inquiring about
 *
 * This lets us know if a specific frequency rule is or is not relevant to
 * a specific frequency's band. Bands are device specific and artificial
 * definitions (the "2.4 GHz band" and the "5 GHz band"), however it is
 * safe for now to assume that a frequency rule should not be part of a
 * frequency's band if the start freq or end freq are off by more than 2 GHz.
 * This resolution can be lowered and should be considered as we add
 * regulatory rule support for other "bands".
 **/
static bool freq_in_rule_band(const struct ieee80211_freq_range *freq_range,
	u32 freq_khz)
{
#define ONE_GHZ_IN_KHZ	1000000
	if (abs(freq_khz - freq_range->start_freq_khz) <= (2 * ONE_GHZ_IN_KHZ))
		return true;
	if (abs(freq_khz - freq_range->end_freq_khz) <= (2 * ONE_GHZ_IN_KHZ))
		return true;
	return false;
#undef ONE_GHZ_IN_KHZ
}

/* Converts a country IE to a regulatory domain. A regulatory domain
 * structure has a lot of information which the IE doesn't yet have,
 * so for the other values we use upper max values as we will intersect
 * with our userspace regulatory agent to get lower bounds. */
static struct ieee80211_regdomain *country_ie_2_rd(
				u8 *country_ie,
				u8 country_ie_len,
				u32 *checksum)
{
	struct ieee80211_regdomain *rd = NULL;
	unsigned int i = 0;
	char alpha2[2];
	u32 flags = 0;
	u32 num_rules = 0, size_of_regd = 0;
	u8 *triplets_start = NULL;
	u8 len_at_triplet = 0;
	/* the last channel we have registered in a subband (triplet) */
	int last_sub_max_channel = 0;

	*checksum = 0xDEADBEEF;

	/* Country IE requirements */
	BUG_ON(country_ie_len < IEEE80211_COUNTRY_IE_MIN_LEN ||
		country_ie_len & 0x01);

	alpha2[0] = country_ie[0];
	alpha2[1] = country_ie[1];

	/*
	 * Third octet can be:
	 *    'I' - Indoor
	 *    'O' - Outdoor
	 *
	 *  anything else we assume is no restrictions
	 */
	if (country_ie[2] == 'I')
		flags = NL80211_RRF_NO_OUTDOOR;
	else if (country_ie[2] == 'O')
		flags = NL80211_RRF_NO_INDOOR;

	country_ie += 3;
	country_ie_len -= 3;

	triplets_start = country_ie;
	len_at_triplet = country_ie_len;

	*checksum ^= ((flags ^ alpha2[0] ^ alpha2[1]) << 8);

	/* We need to build a reg rule for each triplet, but first we must
	 * calculate the number of reg rules we will need. We will need one
	 * for each channel subband */
	while (country_ie_len >= 3) {
		int end_channel = 0;
		struct ieee80211_country_ie_triplet *triplet =
			(struct ieee80211_country_ie_triplet *) country_ie;
		int cur_sub_max_channel = 0, cur_channel = 0;

		if (triplet->ext.reg_extension_id >=
				IEEE80211_COUNTRY_EXTENSION_ID) {
			country_ie += 3;
			country_ie_len -= 3;
			continue;
		}

		/* 2 GHz */
		if (triplet->chans.first_channel <= 14)
			end_channel = triplet->chans.first_channel +
				triplet->chans.num_channels;
		else
			/*
			 * 5 GHz -- For example in country IEs if the first
			 * channel given is 36 and the number of channels is 4
			 * then the individual channel numbers defined for the
			 * 5 GHz PHY by these parameters are: 36, 40, 44, and 48
			 * and not 36, 37, 38, 39.
			 *
			 * See: http://tinyurl.com/11d-clarification
			 */
			end_channel =  triplet->chans.first_channel +
				(4 * (triplet->chans.num_channels - 1));

		cur_channel = triplet->chans.first_channel;
		cur_sub_max_channel = end_channel;

		/* Basic sanity check */
		if (cur_sub_max_channel < cur_channel)
			return NULL;

		/* Do not allow overlapping channels. Also channels
		 * passed in each subband must be monotonically
		 * increasing */
		if (last_sub_max_channel) {
			if (cur_channel <= last_sub_max_channel)
				return NULL;
			if (cur_sub_max_channel <= last_sub_max_channel)
				return NULL;
		}

		/* When dot11RegulatoryClassesRequired is supported
		 * we can throw ext triplets as part of this soup,
		 * for now we don't care when those change as we
		 * don't support them */
		*checksum ^= ((cur_channel ^ cur_sub_max_channel) << 8) |
		  ((cur_sub_max_channel ^ cur_sub_max_channel) << 16) |
		  ((triplet->chans.max_power ^ cur_sub_max_channel) << 24);

		last_sub_max_channel = cur_sub_max_channel;

		country_ie += 3;
		country_ie_len -= 3;
		num_rules++;

		/* Note: this is not a IEEE requirement but
		 * simply a memory requirement */
		if (num_rules > NL80211_MAX_SUPP_REG_RULES)
			return NULL;
	}

	country_ie = triplets_start;
	country_ie_len = len_at_triplet;

	size_of_regd = sizeof(struct ieee80211_regdomain) +
		(num_rules * sizeof(struct ieee80211_reg_rule));

	rd = kzalloc(size_of_regd, GFP_KERNEL);
	if (!rd)
		return NULL;

	rd->n_reg_rules = num_rules;
	rd->alpha2[0] = alpha2[0];
	rd->alpha2[1] = alpha2[1];

	/* This time around we fill in the rd */
	while (country_ie_len >= 3) {
		int end_channel = 0;
		struct ieee80211_country_ie_triplet *triplet =
			(struct ieee80211_country_ie_triplet *) country_ie;
		struct ieee80211_reg_rule *reg_rule = NULL;
		struct ieee80211_freq_range *freq_range = NULL;
		struct ieee80211_power_rule *power_rule = NULL;

		/* Must parse if dot11RegulatoryClassesRequired is true,
		 * we don't support this yet */
		if (triplet->ext.reg_extension_id >=
				IEEE80211_COUNTRY_EXTENSION_ID) {
			country_ie += 3;
			country_ie_len -= 3;
			continue;
		}

		reg_rule = &rd->reg_rules[i];
		freq_range = &reg_rule->freq_range;
		power_rule = &reg_rule->power_rule;

		reg_rule->flags = flags;

		/* 2 GHz */
		if (triplet->chans.first_channel <= 14)
			end_channel = triplet->chans.first_channel +
				triplet->chans.num_channels;
		else
			end_channel =  triplet->chans.first_channel +
				(4 * (triplet->chans.num_channels - 1));

		/* The +10 is since the regulatory domain expects
		 * the actual band edge, not the center of freq for
		 * its start and end freqs, assuming 20 MHz bandwidth on
		 * the channels passed */
		freq_range->start_freq_khz =
			MHZ_TO_KHZ(ieee80211_channel_to_frequency(
				triplet->chans.first_channel) - 10);
		freq_range->end_freq_khz =
			MHZ_TO_KHZ(ieee80211_channel_to_frequency(
				end_channel) + 10);

		/* Large arbitrary values, we intersect later */
		/* Increment this if we ever support >= 40 MHz channels
		 * in IEEE 802.11 */
		freq_range->max_bandwidth_khz = MHZ_TO_KHZ(40);
		power_rule->max_antenna_gain = DBI_TO_MBI(100);
		power_rule->max_eirp = DBM_TO_MBM(100);

		country_ie += 3;
		country_ie_len -= 3;
		i++;

		BUG_ON(i > NL80211_MAX_SUPP_REG_RULES);
	}

	return rd;
}


/* Helper for regdom_intersect(), this does the real
 * mathematical intersection fun */
static int reg_rules_intersect(
	const struct ieee80211_reg_rule *rule1,
	const struct ieee80211_reg_rule *rule2,
	struct ieee80211_reg_rule *intersected_rule)
{
	const struct ieee80211_freq_range *freq_range1, *freq_range2;
	struct ieee80211_freq_range *freq_range;
	const struct ieee80211_power_rule *power_rule1, *power_rule2;
	struct ieee80211_power_rule *power_rule;
	u32 freq_diff;

	freq_range1 = &rule1->freq_range;
	freq_range2 = &rule2->freq_range;
	freq_range = &intersected_rule->freq_range;

	power_rule1 = &rule1->power_rule;
	power_rule2 = &rule2->power_rule;
	power_rule = &intersected_rule->power_rule;

	freq_range->start_freq_khz = max(freq_range1->start_freq_khz,
		freq_range2->start_freq_khz);
	freq_range->end_freq_khz = min(freq_range1->end_freq_khz,
		freq_range2->end_freq_khz);
	freq_range->max_bandwidth_khz = min(freq_range1->max_bandwidth_khz,
		freq_range2->max_bandwidth_khz);

	freq_diff = freq_range->end_freq_khz - freq_range->start_freq_khz;
	if (freq_range->max_bandwidth_khz > freq_diff)
		freq_range->max_bandwidth_khz = freq_diff;

	power_rule->max_eirp = min(power_rule1->max_eirp,
		power_rule2->max_eirp);
	power_rule->max_antenna_gain = min(power_rule1->max_antenna_gain,
		power_rule2->max_antenna_gain);

	intersected_rule->flags = (rule1->flags | rule2->flags);

	if (!is_valid_reg_rule(intersected_rule))
		return -EINVAL;

	return 0;
}

/**
 * regdom_intersect - do the intersection between two regulatory domains
 * @rd1: first regulatory domain
 * @rd2: second regulatory domain
 *
 * Use this function to get the intersection between two regulatory domains.
 * Once completed we will mark the alpha2 for the rd as intersected, "98",
 * as no one single alpha2 can represent this regulatory domain.
 *
 * Returns a pointer to the regulatory domain structure which will hold the
 * resulting intersection of rules between rd1 and rd2. We will
 * kzalloc() this structure for you.
 */
static struct ieee80211_regdomain *regdom_intersect(
	const struct ieee80211_regdomain *rd1,
	const struct ieee80211_regdomain *rd2)
{
	int r, size_of_regd;
	unsigned int x, y;
	unsigned int num_rules = 0, rule_idx = 0;
	const struct ieee80211_reg_rule *rule1, *rule2;
	struct ieee80211_reg_rule *intersected_rule;
	struct ieee80211_regdomain *rd;
	/* This is just a dummy holder to help us count */
	struct ieee80211_reg_rule irule;

	/* Uses the stack temporarily for counter arithmetic */
	intersected_rule = &irule;

	memset(intersected_rule, 0, sizeof(struct ieee80211_reg_rule));

	if (!rd1 || !rd2)
		return NULL;

	/* First we get a count of the rules we'll need, then we actually
	 * build them. This is to so we can malloc() and free() a
	 * regdomain once. The reason we use reg_rules_intersect() here
	 * is it will return -EINVAL if the rule computed makes no sense.
	 * All rules that do check out OK are valid. */

	for (x = 0; x < rd1->n_reg_rules; x++) {
		rule1 = &rd1->reg_rules[x];
		for (y = 0; y < rd2->n_reg_rules; y++) {
			rule2 = &rd2->reg_rules[y];
			if (!reg_rules_intersect(rule1, rule2,
					intersected_rule))
				num_rules++;
			memset(intersected_rule, 0,
					sizeof(struct ieee80211_reg_rule));
		}
	}

	if (!num_rules)
		return NULL;

	size_of_regd = sizeof(struct ieee80211_regdomain) +
		((num_rules + 1) * sizeof(struct ieee80211_reg_rule));

	rd = kzalloc(size_of_regd, GFP_KERNEL);
	if (!rd)
		return NULL;

	for (x = 0; x < rd1->n_reg_rules; x++) {
		rule1 = &rd1->reg_rules[x];
		for (y = 0; y < rd2->n_reg_rules; y++) {
			rule2 = &rd2->reg_rules[y];
			/* This time around instead of using the stack lets
			 * write to the target rule directly saving ourselves
			 * a memcpy() */
			intersected_rule = &rd->reg_rules[rule_idx];
			r = reg_rules_intersect(rule1, rule2,
				intersected_rule);
			/* No need to memset here the intersected rule here as
			 * we're not using the stack anymore */
			if (r)
				continue;
			rule_idx++;
		}
	}

	if (rule_idx != num_rules) {
		kfree(rd);
		return NULL;
	}

	rd->n_reg_rules = num_rules;
	rd->alpha2[0] = '9';
	rd->alpha2[1] = '8';

	return rd;
}

/* XXX: add support for the rest of enum nl80211_reg_rule_flags, we may
 * want to just have the channel structure use these */
static u32 map_regdom_flags(u32 rd_flags)
{
	u32 channel_flags = 0;
	if (rd_flags & NL80211_RRF_PASSIVE_SCAN)
		channel_flags |= IEEE80211_CHAN_PASSIVE_SCAN;
	if (rd_flags & NL80211_RRF_NO_IBSS)
		channel_flags |= IEEE80211_CHAN_NO_IBSS;
	if (rd_flags & NL80211_RRF_DFS)
		channel_flags |= IEEE80211_CHAN_RADAR;
	return channel_flags;
}

/**
 * freq_reg_info - get regulatory information for the given frequency
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
static int freq_reg_info(u32 center_freq, u32 *bandwidth,
			 const struct ieee80211_reg_rule **reg_rule)
{
	int i;
	bool band_rule_found = false;
	u32 max_bandwidth = 0;

	if (!cfg80211_regdomain)
		return -EINVAL;

	for (i = 0; i < cfg80211_regdomain->n_reg_rules; i++) {
		const struct ieee80211_reg_rule *rr;
		const struct ieee80211_freq_range *fr = NULL;
		const struct ieee80211_power_rule *pr = NULL;

		rr = &cfg80211_regdomain->reg_rules[i];
		fr = &rr->freq_range;
		pr = &rr->power_rule;

		/* We only need to know if one frequency rule was
		 * was in center_freq's band, that's enough, so lets
		 * not overwrite it once found */
		if (!band_rule_found)
			band_rule_found = freq_in_rule_band(fr, center_freq);

		max_bandwidth = freq_max_bandwidth(fr, center_freq);

		if (max_bandwidth && *bandwidth <= max_bandwidth) {
			*reg_rule = rr;
			*bandwidth = max_bandwidth;
			break;
		}
	}

	if (!band_rule_found)
		return -ERANGE;

	return !max_bandwidth;
}

static void handle_channel(struct wiphy *wiphy, enum ieee80211_band band,
			   unsigned int chan_idx)
{
	int r;
	u32 flags;
	u32 max_bandwidth = 0;
	const struct ieee80211_reg_rule *reg_rule = NULL;
	const struct ieee80211_power_rule *power_rule = NULL;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;

	sband = wiphy->bands[band];
	BUG_ON(chan_idx >= sband->n_channels);
	chan = &sband->channels[chan_idx];

	flags = chan->orig_flags;

	r = freq_reg_info(MHZ_TO_KHZ(chan->center_freq),
		&max_bandwidth, &reg_rule);

	if (r) {
		/* This means no regulatory rule was found in the country IE
		 * with a frequency range on the center_freq's band, since
		 * IEEE-802.11 allows for a country IE to have a subset of the
		 * regulatory information provided in a country we ignore
		 * disabling the channel unless at least one reg rule was
		 * found on the center_freq's band. For details see this
		 * clarification:
		 *
		 * http://tinyurl.com/11d-clarification
		 */
		if (r == -ERANGE &&
		    last_request->initiator == REGDOM_SET_BY_COUNTRY_IE) {
#ifdef CONFIG_CFG80211_REG_DEBUG
			printk(KERN_DEBUG "cfg80211: Leaving channel %d MHz "
				"intact on %s - no rule found in band on "
				"Country IE\n",
				chan->center_freq, wiphy_name(wiphy));
#endif
		} else {
		/* In this case we know the country IE has at least one reg rule
		 * for the band so we respect its band definitions */
#ifdef CONFIG_CFG80211_REG_DEBUG
			if (last_request->initiator == REGDOM_SET_BY_COUNTRY_IE)
				printk(KERN_DEBUG "cfg80211: Disabling "
					"channel %d MHz on %s due to "
					"Country IE\n",
					chan->center_freq, wiphy_name(wiphy));
#endif
			flags |= IEEE80211_CHAN_DISABLED;
			chan->flags = flags;
		}
		return;
	}

	power_rule = &reg_rule->power_rule;

	chan->flags = flags | map_regdom_flags(reg_rule->flags);
	chan->max_antenna_gain = min(chan->orig_mag,
		(int) MBI_TO_DBI(power_rule->max_antenna_gain));
	chan->max_bandwidth = KHZ_TO_MHZ(max_bandwidth);
	if (chan->orig_mpwr)
		chan->max_power = min(chan->orig_mpwr,
			(int) MBM_TO_DBM(power_rule->max_eirp));
	else
		chan->max_power = (int) MBM_TO_DBM(power_rule->max_eirp);
}

static void handle_band(struct wiphy *wiphy, enum ieee80211_band band)
{
	unsigned int i;
	struct ieee80211_supported_band *sband;

	BUG_ON(!wiphy->bands[band]);
	sband = wiphy->bands[band];

	for (i = 0; i < sband->n_channels; i++)
		handle_channel(wiphy, band, i);
}

static bool ignore_reg_update(struct wiphy *wiphy, enum reg_set_by setby)
{
	if (!last_request)
		return true;
	if (setby == REGDOM_SET_BY_CORE &&
		  wiphy->fw_handles_regulatory)
		return true;
	return false;
}

static void update_all_wiphy_regulatory(enum reg_set_by setby)
{
	struct cfg80211_registered_device *drv;

	list_for_each_entry(drv, &cfg80211_drv_list, list)
		if (!ignore_reg_update(&drv->wiphy, setby))
			wiphy_update_regulatory(&drv->wiphy, setby);
}

void wiphy_update_regulatory(struct wiphy *wiphy, enum reg_set_by setby)
{
	enum ieee80211_band band;
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (wiphy->bands[band])
			handle_band(wiphy, band);
		if (wiphy->reg_notifier)
			wiphy->reg_notifier(wiphy, setby);
	}
}

/* Return value which can be used by ignore_request() to indicate
 * it has been determined we should intersect two regulatory domains */
#define REG_INTERSECT	1

/* This has the logic which determines when a new request
 * should be ignored. */
static int ignore_request(struct wiphy *wiphy, enum reg_set_by set_by,
			  const char *alpha2)
{
	/* All initial requests are respected */
	if (!last_request)
		return 0;

	switch (set_by) {
	case REGDOM_SET_BY_INIT:
		return -EINVAL;
	case REGDOM_SET_BY_CORE:
		/*
		 * Always respect new wireless core hints, should only happen
		 * when updating the world regulatory domain at init.
		 */
		return 0;
	case REGDOM_SET_BY_COUNTRY_IE:
		if (unlikely(!is_an_alpha2(alpha2)))
			return -EINVAL;
		if (last_request->initiator == REGDOM_SET_BY_COUNTRY_IE) {
			if (last_request->wiphy != wiphy) {
				/*
				 * Two cards with two APs claiming different
				 * different Country IE alpha2s. We could
				 * intersect them, but that seems unlikely
				 * to be correct. Reject second one for now.
				 */
				if (!alpha2_equal(alpha2,
						  cfg80211_regdomain->alpha2))
					return -EOPNOTSUPP;
				return -EALREADY;
			}
			/* Two consecutive Country IE hints on the same wiphy.
			 * This should be picked up early by the driver/stack */
			if (WARN_ON(!alpha2_equal(cfg80211_regdomain->alpha2,
				  alpha2)))
				return 0;
			return -EALREADY;
		}
		return REG_INTERSECT;
	case REGDOM_SET_BY_DRIVER:
		if (last_request->initiator == REGDOM_SET_BY_DRIVER)
			return -EALREADY;
		return 0;
	case REGDOM_SET_BY_USER:
		if (last_request->initiator == REGDOM_SET_BY_COUNTRY_IE)
			return REG_INTERSECT;
		/* If the user knows better the user should set the regdom
		 * to their country before the IE is picked up */
		if (last_request->initiator == REGDOM_SET_BY_USER &&
			  last_request->intersect)
			return -EOPNOTSUPP;
		return 0;
	}

	return -EINVAL;
}

/* Caller must hold &cfg80211_drv_mutex */
int __regulatory_hint(struct wiphy *wiphy, enum reg_set_by set_by,
			const char *alpha2,
			u32 country_ie_checksum,
			enum environment_cap env)
{
	struct regulatory_request *request;
	bool intersect = false;
	int r = 0;

	r = ignore_request(wiphy, set_by, alpha2);

	if (r == REG_INTERSECT)
		intersect = true;
	else if (r)
		return r;

	request = kzalloc(sizeof(struct regulatory_request),
			  GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->alpha2[0] = alpha2[0];
	request->alpha2[1] = alpha2[1];
	request->initiator = set_by;
	request->wiphy = wiphy;
	request->intersect = intersect;
	request->country_ie_checksum = country_ie_checksum;
	request->country_ie_env = env;

	kfree(last_request);
	last_request = request;
	/*
	 * Note: When CONFIG_WIRELESS_OLD_REGULATORY is enabled
	 * AND if CRDA is NOT present nothing will happen, if someone
	 * wants to bother with 11d with OLD_REG you can add a timer.
	 * If after x amount of time nothing happens you can call:
	 *
	 * return set_regdom(country_ie_regdomain);
	 *
	 * to intersect with the static rd
	 */
	return call_crda(alpha2);
}

void regulatory_hint(struct wiphy *wiphy, const char *alpha2)
{
	BUG_ON(!alpha2);

	mutex_lock(&cfg80211_drv_mutex);
	__regulatory_hint(wiphy, REGDOM_SET_BY_DRIVER, alpha2, 0, ENVIRON_ANY);
	mutex_unlock(&cfg80211_drv_mutex);
}
EXPORT_SYMBOL(regulatory_hint);

static bool reg_same_country_ie_hint(struct wiphy *wiphy,
			u32 country_ie_checksum)
{
	if (!last_request->wiphy)
		return false;
	if (likely(last_request->wiphy != wiphy))
		return !country_ie_integrity_changes(country_ie_checksum);
	/* We should not have let these through at this point, they
	 * should have been picked up earlier by the first alpha2 check
	 * on the device */
	if (WARN_ON(!country_ie_integrity_changes(country_ie_checksum)))
		return true;
	return false;
}

void regulatory_hint_11d(struct wiphy *wiphy,
			u8 *country_ie,
			u8 country_ie_len)
{
	struct ieee80211_regdomain *rd = NULL;
	char alpha2[2];
	u32 checksum = 0;
	enum environment_cap env = ENVIRON_ANY;

	if (!last_request)
		return;

	mutex_lock(&cfg80211_drv_mutex);

	/* IE len must be evenly divisible by 2 */
	if (country_ie_len & 0x01)
		goto out;

	if (country_ie_len < IEEE80211_COUNTRY_IE_MIN_LEN)
		goto out;

	/* Pending country IE processing, this can happen after we
	 * call CRDA and wait for a response if a beacon was received before
	 * we were able to process the last regulatory_hint_11d() call */
	if (country_ie_regdomain)
		goto out;

	alpha2[0] = country_ie[0];
	alpha2[1] = country_ie[1];

	if (country_ie[2] == 'I')
		env = ENVIRON_INDOOR;
	else if (country_ie[2] == 'O')
		env = ENVIRON_OUTDOOR;

	/* We will run this for *every* beacon processed for the BSSID, so
	 * we optimize an early check to exit out early if we don't have to
	 * do anything */
	if (likely(last_request->wiphy)) {
		struct cfg80211_registered_device *drv_last_ie;

		drv_last_ie = wiphy_to_dev(last_request->wiphy);

		/* Lets keep this simple -- we trust the first AP
		 * after we intersect with CRDA */
		if (likely(last_request->wiphy == wiphy)) {
			/* Ignore IEs coming in on this wiphy with
			 * the same alpha2 and environment cap */
			if (likely(alpha2_equal(drv_last_ie->country_ie_alpha2,
				  alpha2) &&
				  env == drv_last_ie->env)) {
				goto out;
			}
			/* the wiphy moved on to another BSSID or the AP
			 * was reconfigured. XXX: We need to deal with the
			 * case where the user suspends and goes to goes
			 * to another country, and then gets IEs from an
			 * AP with different settings */
			goto out;
		} else {
			/* Ignore IEs coming in on two separate wiphys with
			 * the same alpha2 and environment cap */
			if (likely(alpha2_equal(drv_last_ie->country_ie_alpha2,
				  alpha2) &&
				  env == drv_last_ie->env)) {
				goto out;
			}
			/* We could potentially intersect though */
			goto out;
		}
	}

	rd = country_ie_2_rd(country_ie, country_ie_len, &checksum);
	if (!rd)
		goto out;

	/* This will not happen right now but we leave it here for the
	 * the future when we want to add suspend/resume support and having
	 * the user move to another country after doing so, or having the user
	 * move to another AP. Right now we just trust the first AP. This is why
	 * this is marked as likley(). If we hit this before we add this support
	 * we want to be informed of it as it would indicate a mistake in the
	 * current design  */
	if (likely(WARN_ON(reg_same_country_ie_hint(wiphy, checksum))))
		goto out;

	/* We keep this around for when CRDA comes back with a response so
	 * we can intersect with that */
	country_ie_regdomain = rd;

	__regulatory_hint(wiphy, REGDOM_SET_BY_COUNTRY_IE,
		country_ie_regdomain->alpha2, checksum, env);

out:
	mutex_unlock(&cfg80211_drv_mutex);
}
EXPORT_SYMBOL(regulatory_hint_11d);

static void print_rd_rules(const struct ieee80211_regdomain *rd)
{
	unsigned int i;
	const struct ieee80211_reg_rule *reg_rule = NULL;
	const struct ieee80211_freq_range *freq_range = NULL;
	const struct ieee80211_power_rule *power_rule = NULL;

	printk(KERN_INFO "\t(start_freq - end_freq @ bandwidth), "
		"(max_antenna_gain, max_eirp)\n");

	for (i = 0; i < rd->n_reg_rules; i++) {
		reg_rule = &rd->reg_rules[i];
		freq_range = &reg_rule->freq_range;
		power_rule = &reg_rule->power_rule;

		/* There may not be documentation for max antenna gain
		 * in certain regions */
		if (power_rule->max_antenna_gain)
			printk(KERN_INFO "\t(%d KHz - %d KHz @ %d KHz), "
				"(%d mBi, %d mBm)\n",
				freq_range->start_freq_khz,
				freq_range->end_freq_khz,
				freq_range->max_bandwidth_khz,
				power_rule->max_antenna_gain,
				power_rule->max_eirp);
		else
			printk(KERN_INFO "\t(%d KHz - %d KHz @ %d KHz), "
				"(N/A, %d mBm)\n",
				freq_range->start_freq_khz,
				freq_range->end_freq_khz,
				freq_range->max_bandwidth_khz,
				power_rule->max_eirp);
	}
}

static void print_regdomain(const struct ieee80211_regdomain *rd)
{

	if (is_intersected_alpha2(rd->alpha2)) {
		struct wiphy *wiphy = NULL;
		struct cfg80211_registered_device *drv;

		if (last_request->initiator == REGDOM_SET_BY_COUNTRY_IE) {
			if (last_request->wiphy) {
				wiphy = last_request->wiphy;
				drv = wiphy_to_dev(wiphy);
				printk(KERN_INFO "cfg80211: Current regulatory "
					"domain updated by AP to: %c%c\n",
					drv->country_ie_alpha2[0],
					drv->country_ie_alpha2[1]);
			} else
				printk(KERN_INFO "cfg80211: Current regulatory "
					"domain intersected: \n");
		} else
				printk(KERN_INFO "cfg80211: Current regulatory "
					"intersected: \n");
	} else if (is_world_regdom(rd->alpha2))
		printk(KERN_INFO "cfg80211: World regulatory "
			"domain updated:\n");
	else {
		if (is_unknown_alpha2(rd->alpha2))
			printk(KERN_INFO "cfg80211: Regulatory domain "
				"changed to driver built-in settings "
				"(unknown country)\n");
		else
			printk(KERN_INFO "cfg80211: Regulatory domain "
				"changed to country: %c%c\n",
				rd->alpha2[0], rd->alpha2[1]);
	}
	print_rd_rules(rd);
}

static void print_regdomain_info(const struct ieee80211_regdomain *rd)
{
	printk(KERN_INFO "cfg80211: Regulatory domain: %c%c\n",
		rd->alpha2[0], rd->alpha2[1]);
	print_rd_rules(rd);
}

#ifdef CONFIG_CFG80211_REG_DEBUG
static void reg_country_ie_process_debug(
	const struct ieee80211_regdomain *rd,
	const struct ieee80211_regdomain *country_ie_regdomain,
	const struct ieee80211_regdomain *intersected_rd)
{
	printk(KERN_DEBUG "cfg80211: Received country IE:\n");
	print_regdomain_info(country_ie_regdomain);
	printk(KERN_DEBUG "cfg80211: CRDA thinks this should applied:\n");
	print_regdomain_info(rd);
	if (intersected_rd) {
		printk(KERN_DEBUG "cfg80211: We intersect both of these "
			"and get:\n");
		print_regdomain_info(intersected_rd);
		return;
	}
	printk(KERN_DEBUG "cfg80211: Intersection between both failed\n");
}
#else
static inline void reg_country_ie_process_debug(
	const struct ieee80211_regdomain *rd,
	const struct ieee80211_regdomain *country_ie_regdomain,
	const struct ieee80211_regdomain *intersected_rd)
{
}
#endif

/* Takes ownership of rd only if it doesn't fail */
static int __set_regdom(const struct ieee80211_regdomain *rd)
{
	const struct ieee80211_regdomain *intersected_rd = NULL;
	struct cfg80211_registered_device *drv = NULL;
	struct wiphy *wiphy = NULL;
	/* Some basic sanity checks first */

	if (is_world_regdom(rd->alpha2)) {
		if (WARN_ON(!reg_is_valid_request(rd->alpha2)))
			return -EINVAL;
		update_world_regdomain(rd);
		return 0;
	}

	if (!is_alpha2_set(rd->alpha2) && !is_an_alpha2(rd->alpha2) &&
			!is_unknown_alpha2(rd->alpha2))
		return -EINVAL;

	if (!last_request)
		return -EINVAL;

	/* Lets only bother proceeding on the same alpha2 if the current
	 * rd is non static (it means CRDA was present and was used last)
	 * and the pending request came in from a country IE */
	if (last_request->initiator != REGDOM_SET_BY_COUNTRY_IE) {
		/* If someone else asked us to change the rd lets only bother
		 * checking if the alpha2 changes if CRDA was already called */
		if (!is_old_static_regdom(cfg80211_regdomain) &&
		    !regdom_changed(rd->alpha2))
			return -EINVAL;
	}

	wiphy = last_request->wiphy;

	/* Now lets set the regulatory domain, update all driver channels
	 * and finally inform them of what we have done, in case they want
	 * to review or adjust their own settings based on their own
	 * internal EEPROM data */

	if (WARN_ON(!reg_is_valid_request(rd->alpha2)))
		return -EINVAL;

	if (!is_valid_rd(rd)) {
		printk(KERN_ERR "cfg80211: Invalid "
			"regulatory domain detected:\n");
		print_regdomain_info(rd);
		return -EINVAL;
	}

	if (!last_request->intersect) {
		reset_regdomains();
		cfg80211_regdomain = rd;
		return 0;
	}

	/* Intersection requires a bit more work */

	if (last_request->initiator != REGDOM_SET_BY_COUNTRY_IE) {

		intersected_rd = regdom_intersect(rd, cfg80211_regdomain);
		if (!intersected_rd)
			return -EINVAL;

		/* We can trash what CRDA provided now */
		kfree(rd);
		rd = NULL;

		reset_regdomains();
		cfg80211_regdomain = intersected_rd;

		return 0;
	}

	/*
	 * Country IE requests are handled a bit differently, we intersect
	 * the country IE rd with what CRDA believes that country should have
	 */

	BUG_ON(!country_ie_regdomain);

	if (rd != country_ie_regdomain) {
		/* Intersect what CRDA returned and our what we
		 * had built from the Country IE received */

		intersected_rd = regdom_intersect(rd, country_ie_regdomain);

		reg_country_ie_process_debug(rd, country_ie_regdomain,
			intersected_rd);

		kfree(country_ie_regdomain);
		country_ie_regdomain = NULL;
	} else {
		/* This would happen when CRDA was not present and
		 * OLD_REGULATORY was enabled. We intersect our Country
		 * IE rd and what was set on cfg80211 originally */
		intersected_rd = regdom_intersect(rd, cfg80211_regdomain);
	}

	if (!intersected_rd)
		return -EINVAL;

	drv = wiphy_to_dev(wiphy);

	drv->country_ie_alpha2[0] = rd->alpha2[0];
	drv->country_ie_alpha2[1] = rd->alpha2[1];
	drv->env = last_request->country_ie_env;

	BUG_ON(intersected_rd == rd);

	kfree(rd);
	rd = NULL;

	reset_regdomains();
	cfg80211_regdomain = intersected_rd;

	return 0;
}


/* Use this call to set the current regulatory domain. Conflicts with
 * multiple drivers can be ironed out later. Caller must've already
 * kmalloc'd the rd structure. Caller must hold cfg80211_drv_mutex */
int set_regdom(const struct ieee80211_regdomain *rd)
{
	int r;

	/* Note that this doesn't update the wiphys, this is done below */
	r = __set_regdom(rd);
	if (r) {
		kfree(rd);
		return r;
	}

	/* This would make this whole thing pointless */
	if (!last_request->intersect)
		BUG_ON(rd != cfg80211_regdomain);

	/* update all wiphys now with the new established regulatory domain */
	update_all_wiphy_regulatory(last_request->initiator);

	print_regdomain(cfg80211_regdomain);

	return r;
}

/* Caller must hold cfg80211_drv_mutex */
void reg_device_remove(struct wiphy *wiphy)
{
	if (!last_request || !last_request->wiphy)
		return;
	if (last_request->wiphy != wiphy)
		return;
	last_request->wiphy = NULL;
	last_request->country_ie_env = ENVIRON_ANY;
}

int regulatory_init(void)
{
	int err;

	reg_pdev = platform_device_register_simple("regulatory", 0, NULL, 0);
	if (IS_ERR(reg_pdev))
		return PTR_ERR(reg_pdev);

#ifdef CONFIG_WIRELESS_OLD_REGULATORY
	cfg80211_regdomain = static_regdom(ieee80211_regdom);

	printk(KERN_INFO "cfg80211: Using static regulatory domain info\n");
	print_regdomain_info(cfg80211_regdomain);
	/* The old code still requests for a new regdomain and if
	 * you have CRDA you get it updated, otherwise you get
	 * stuck with the static values. We ignore "EU" code as
	 * that is not a valid ISO / IEC 3166 alpha2 */
	if (ieee80211_regdom[0] != 'E' || ieee80211_regdom[1] != 'U')
		err = __regulatory_hint(NULL, REGDOM_SET_BY_CORE,
					ieee80211_regdom, 0, ENVIRON_ANY);
#else
	cfg80211_regdomain = cfg80211_world_regdom;

	err = __regulatory_hint(NULL, REGDOM_SET_BY_CORE, "00", 0, ENVIRON_ANY);
	if (err)
		printk(KERN_ERR "cfg80211: calling CRDA failed - "
		       "unable to update world regulatory domain, "
		       "using static definition\n");
#endif

	return 0;
}

void regulatory_exit(void)
{
	mutex_lock(&cfg80211_drv_mutex);

	reset_regdomains();

	kfree(country_ie_regdomain);
	country_ie_regdomain = NULL;

	kfree(last_request);

	platform_device_unregister(reg_pdev);

	mutex_unlock(&cfg80211_drv_mutex);
}
