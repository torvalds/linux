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
 */
struct regulatory_request {
	struct wiphy *wiphy;
	enum reg_set_by initiator;
	char alpha2[2];
	bool intersect;
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

	if (freq_range->start_freq_khz == 0 || freq_range->end_freq_khz == 0)
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
 * Use this function to get the regulatory rule for a specific frequency.
 */
static int freq_reg_info(u32 center_freq, u32 *bandwidth,
			 const struct ieee80211_reg_rule **reg_rule)
{
	int i;
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
		max_bandwidth = freq_max_bandwidth(fr, center_freq);
		if (max_bandwidth && *bandwidth <= max_bandwidth) {
			*reg_rule = rr;
			*bandwidth = max_bandwidth;
			break;
		}
	}

	return !max_bandwidth;
}

static void handle_channel(struct ieee80211_channel *chan)
{
	int r;
	u32 flags = chan->orig_flags;
	u32 max_bandwidth = 0;
	const struct ieee80211_reg_rule *reg_rule = NULL;
	const struct ieee80211_power_rule *power_rule = NULL;

	r = freq_reg_info(MHZ_TO_KHZ(chan->center_freq),
		&max_bandwidth, &reg_rule);

	if (r) {
		flags |= IEEE80211_CHAN_DISABLED;
		chan->flags = flags;
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

static void handle_band(struct ieee80211_supported_band *sband)
{
	int i;

	for (i = 0; i < sband->n_channels; i++)
		handle_channel(&sband->channels[i]);
}

static void update_all_wiphy_regulatory(enum reg_set_by setby)
{
	struct cfg80211_registered_device *drv;

	list_for_each_entry(drv, &cfg80211_drv_list, list)
		wiphy_update_regulatory(&drv->wiphy, setby);
}

void wiphy_update_regulatory(struct wiphy *wiphy, enum reg_set_by setby)
{
	enum ieee80211_band band;
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (wiphy->bands[band])
			handle_band(wiphy->bands[band]);
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
			/* Two consecutive Country IE hints on the same wiphy */
			if (!alpha2_equal(cfg80211_regdomain->alpha2, alpha2))
				return 0;
			return -EALREADY;
		}
		/*
		 * Ignore Country IE hints for now, need to think about
		 * what we need to do to support multi-domain operation.
		 */
		return -EOPNOTSUPP;
	case REGDOM_SET_BY_DRIVER:
		if (last_request->initiator == REGDOM_SET_BY_DRIVER)
			return -EALREADY;
		return 0;
	case REGDOM_SET_BY_USER:
		if (last_request->initiator == REGDOM_SET_BY_COUNTRY_IE)
			return REG_INTERSECT;
		return 0;
	}

	return -EINVAL;
}

/* Caller must hold &cfg80211_drv_mutex */
int __regulatory_hint(struct wiphy *wiphy, enum reg_set_by set_by,
		      const char *alpha2)
{
	struct regulatory_request *request;
	bool intersect = false;
	int r = 0;

	r = ignore_request(wiphy, set_by, alpha2);

	if (r == REG_INTERSECT)
		intersect = true;
	else if (r)
		return r;

	switch (set_by) {
	case REGDOM_SET_BY_CORE:
	case REGDOM_SET_BY_COUNTRY_IE:
	case REGDOM_SET_BY_DRIVER:
	case REGDOM_SET_BY_USER:
		request = kzalloc(sizeof(struct regulatory_request),
				  GFP_KERNEL);
		if (!request)
			return -ENOMEM;

		request->alpha2[0] = alpha2[0];
		request->alpha2[1] = alpha2[1];
		request->initiator = set_by;
		request->wiphy = wiphy;
		request->intersect = intersect;

		kfree(last_request);
		last_request = request;
		r = call_crda(alpha2);
#ifndef CONFIG_WIRELESS_OLD_REGULATORY
		if (r)
			printk(KERN_ERR "cfg80211: Failed calling CRDA\n");
#endif
		break;
	default:
		r = -ENOTSUPP;
		break;
	}

	return r;
}

void regulatory_hint(struct wiphy *wiphy, const char *alpha2)
{
	BUG_ON(!alpha2);

	mutex_lock(&cfg80211_drv_mutex);
	__regulatory_hint(wiphy, REGDOM_SET_BY_DRIVER, alpha2);
	mutex_unlock(&cfg80211_drv_mutex);
}
EXPORT_SYMBOL(regulatory_hint);


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

	if (is_world_regdom(rd->alpha2))
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

/* Takes ownership of rd only if it doesn't fail */
static int __set_regdom(const struct ieee80211_regdomain *rd)
{
	const struct ieee80211_regdomain *intersected_rd = NULL;
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

	/* allow overriding the static definitions if CRDA is present */
	if (!is_old_static_regdom(cfg80211_regdomain) &&
	    !regdom_changed(rd->alpha2))
		return -EINVAL;

	/* Now lets set the regulatory domain, update all driver channels
	 * and finally inform them of what we have done, in case they want
	 * to review or adjust their own settings based on their own
	 * internal EEPROM data */

	if (WARN_ON(!reg_is_valid_request(rd->alpha2)))
		return -EINVAL;

	reset_regdomains();

	/* Country IE parsing coming soon */
	switch (last_request->initiator) {
	case REGDOM_SET_BY_CORE:
	case REGDOM_SET_BY_DRIVER:
	case REGDOM_SET_BY_USER:
		if (!is_valid_rd(rd)) {
			printk(KERN_ERR "cfg80211: Invalid "
				"regulatory domain detected:\n");
			print_regdomain_info(rd);
			return -EINVAL;
		}
		break;
	case REGDOM_SET_BY_COUNTRY_IE: /* Not yet */
		WARN_ON(1);
	default:
		return -EOPNOTSUPP;
	}

	if (unlikely(last_request->intersect)) {
		intersected_rd = regdom_intersect(rd, cfg80211_regdomain);
		if (!intersected_rd)
			return -EINVAL;
		kfree(rd);
		rd = intersected_rd;
	}

	/* Tada! */
	cfg80211_regdomain = rd;

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
	BUG_ON(rd != cfg80211_regdomain);

	/* update all wiphys now with the new established regulatory domain */
	update_all_wiphy_regulatory(last_request->initiator);

	print_regdomain(rd);

	return r;
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
					ieee80211_regdom);
#else
	cfg80211_regdomain = cfg80211_world_regdom;

	err = __regulatory_hint(NULL, REGDOM_SET_BY_CORE, "00");
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

	kfree(last_request);

	platform_device_unregister(reg_pdev);

	mutex_unlock(&cfg80211_drv_mutex);
}
