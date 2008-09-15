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

/* wiphy is set if this request's initiator is REGDOM_SET_BY_DRIVER */
struct regulatory_request {
	struct list_head list;
	struct wiphy *wiphy;
	int granted;
	enum reg_set_by initiator;
	char alpha2[2];
};

static LIST_HEAD(regulatory_requests);
DEFINE_MUTEX(cfg80211_reg_mutex);

/* To trigger userspace events */
static struct platform_device *reg_pdev;

/* Keep the ordering from large to small */
static u32 supported_bandwidths[] = {
	MHZ_TO_KHZ(40),
	MHZ_TO_KHZ(20),
};

static struct list_head regulatory_requests;

/* Central wireless core regulatory domains, we only need two,
 * the current one and a world regulatory domain in case we have no
 * information to give us an alpha2 */
static struct ieee80211_regdomain *cfg80211_regdomain;

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

static struct ieee80211_regdomain *cfg80211_world_regdom =
	(struct ieee80211_regdomain *) &world_regdom;

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

static bool is_old_static_regdom(struct ieee80211_regdomain *rd)
{
	if (rd == &us_regdom || rd == &jp_regdom || rd == &eu_regdom)
		return true;
	return false;
}

/* The old crap never deals with a world regulatory domain, it only
 * deals with the static regulatory domain passed and if possible
 * an updated "US" or "JP" regulatory domain. We do however store the
 * old static regulatory domain in cfg80211_world_regdom for convenience
 * of use here */
static void reset_regdomains_static(void)
{
	if (!is_old_static_regdom(cfg80211_regdomain))
		kfree(cfg80211_regdomain);
	/* This is setting the regdom to the old static regdom */
	cfg80211_regdomain =
		(struct ieee80211_regdomain *) cfg80211_world_regdom;
}
#else
static void reset_regdomains(void)
{
	if (cfg80211_world_regdom && cfg80211_world_regdom != &world_regdom) {
		if (cfg80211_world_regdom == cfg80211_regdomain) {
			kfree(cfg80211_regdomain);
		} else {
			kfree(cfg80211_world_regdom);
			kfree(cfg80211_regdomain);
		}
	} else if (cfg80211_regdomain && cfg80211_regdomain != &world_regdom)
		kfree(cfg80211_regdomain);

	cfg80211_world_regdom = (struct ieee80211_regdomain *) &world_regdom;
	cfg80211_regdomain = NULL;
}

/* Dynamic world regulatory domain requested by the wireless
 * core upon initialization */
static void update_world_regdomain(struct ieee80211_regdomain *rd)
{
	BUG_ON(list_empty(&regulatory_requests));

	reset_regdomains();

	cfg80211_world_regdom = rd;
	cfg80211_regdomain = rd;
}
#endif

bool is_world_regdom(char *alpha2)
{
	if (!alpha2)
		return false;
	if (alpha2[0] == '0' && alpha2[1] == '0')
		return true;
	return false;
}

static bool is_alpha2_set(char *alpha2)
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

static bool is_unknown_alpha2(char *alpha2)
{
	if (!alpha2)
		return false;
	/* Special case where regulatory domain was built by driver
	 * but a specific alpha2 cannot be determined */
	if (alpha2[0] == '9' && alpha2[1] == '9')
		return true;
	return false;
}

static bool is_an_alpha2(char *alpha2)
{
	if (!alpha2)
		return false;
	if (is_alpha_upper(alpha2[0]) && is_alpha_upper(alpha2[1]))
		return true;
	return false;
}

static bool alpha2_equal(char *alpha2_x, char *alpha2_y)
{
	if (!alpha2_x || !alpha2_y)
		return false;
	if (alpha2_x[0] == alpha2_y[0] &&
		alpha2_x[1] == alpha2_y[1])
		return true;
	return false;
}

static bool regdom_changed(char *alpha2)
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
#ifdef CONFIG_WIRELESS_OLD_REGULATORY
		return -EINVAL;
#else
		printk(KERN_INFO "cfg80211: Calling CRDA to update world "
			"regulatory domain\n");
#endif

	country_env[8] = alpha2[0];
	country_env[9] = alpha2[1];

	return kobject_uevent_env(&reg_pdev->dev.kobj, KOBJ_CHANGE, envp);
}

/* This has the logic which determines when a new request
 * should be ignored. */
static int ignore_request(struct wiphy *wiphy, enum reg_set_by set_by,
	char *alpha2, struct ieee80211_regdomain *rd)
{
	struct regulatory_request *last_request = NULL;

	/* All initial requests are respected */
	if (list_empty(&regulatory_requests))
		return 0;

	last_request = list_first_entry(&regulatory_requests,
		struct regulatory_request, list);

	switch (set_by) {
	case REGDOM_SET_BY_INIT:
		return -EINVAL;
	case REGDOM_SET_BY_CORE:
		/* Always respect new wireless core hints, should only
		 * come in for updating the world regulatory domain at init
		 * anyway */
		return 0;
	case REGDOM_SET_BY_COUNTRY_IE:
		if (last_request->initiator == set_by) {
			if (last_request->wiphy != wiphy) {
				/* Two cards with two APs claiming different
				 * different Country IE alpha2s!
				 * You're special!! */
				if (!alpha2_equal(last_request->alpha2,
						cfg80211_regdomain->alpha2)) {
					/* XXX: Deal with conflict, consider
					 * building a new one out of the
					 * intersection */
					WARN_ON(1);
					return -EOPNOTSUPP;
				}
				return -EALREADY;
			}
			/* Two consecutive Country IE hints on the same wiphy */
			if (!alpha2_equal(cfg80211_regdomain->alpha2, alpha2))
				return 0;
			return -EALREADY;
		}
		if (WARN_ON(!is_alpha2_set(alpha2) || !is_an_alpha2(alpha2)),
				"Invalid Country IE regulatory hint passed "
				"to the wireless core\n")
			return -EINVAL;
		/* We ignore Country IE hints for now, as we haven't yet
		 * added the dot11MultiDomainCapabilityEnabled flag
		 * for wiphys */
		return 1;
	case REGDOM_SET_BY_DRIVER:
		BUG_ON(!wiphy);
		if (last_request->initiator == set_by) {
			/* Two separate drivers hinting different things,
			 * this is possible if you have two devices present
			 * on a system with different EEPROM regulatory
			 * readings. XXX: Do intersection, we support only
			 * the first regulatory hint for now */
			if (last_request->wiphy != wiphy)
				return -EALREADY;
			if (rd)
				return -EALREADY;
			/* Driver should not be trying to hint different
			 * regulatory domains! */
			BUG_ON(!alpha2_equal(alpha2,
					cfg80211_regdomain->alpha2));
			return -EALREADY;
		}
		if (last_request->initiator == REGDOM_SET_BY_CORE)
			return 0;
		/* XXX: Handle intersection, and add the
		 * dot11MultiDomainCapabilityEnabled flag to wiphy. For now
		 * we assume the driver has this set to false, following the
		 * 802.11d dot11MultiDomainCapabilityEnabled documentation */
		if (last_request->initiator == REGDOM_SET_BY_COUNTRY_IE)
			return 0;
		return 0;
	case REGDOM_SET_BY_USER:
		if (last_request->initiator == set_by ||
				last_request->initiator == REGDOM_SET_BY_CORE)
			return 0;
		/* Drivers can use their wiphy's reg_notifier()
		 * to override any information */
		if (last_request->initiator == REGDOM_SET_BY_DRIVER)
			return 0;
		/* XXX: Handle intersection */
		if (last_request->initiator == REGDOM_SET_BY_COUNTRY_IE)
			return -EOPNOTSUPP;
		return 0;
	default:
		return -EINVAL;
	}
}

static bool __reg_is_valid_request(char *alpha2,
	struct regulatory_request **request)
{
	struct regulatory_request *req;
	if (list_empty(&regulatory_requests))
		return false;
	list_for_each_entry(req, &regulatory_requests, list) {
		if (alpha2_equal(req->alpha2, alpha2)) {
			*request = req;
			return true;
		}
	}
	return false;
}

/* Used by nl80211 before kmalloc'ing our regulatory domain */
bool reg_is_valid_request(char *alpha2)
{
	struct regulatory_request *request = NULL;
	return  __reg_is_valid_request(alpha2, &request);
}

/* Sanity check on a regulatory rule */
static bool is_valid_reg_rule(struct ieee80211_reg_rule *rule)
{
	struct ieee80211_freq_range *freq_range = &rule->freq_range;
	u32 freq_diff;

	if (freq_range->start_freq_khz == 0 || freq_range->end_freq_khz == 0)
		return false;

	if (freq_range->start_freq_khz > freq_range->end_freq_khz)
		return false;

	freq_diff = freq_range->end_freq_khz - freq_range->start_freq_khz;

	if (freq_range->max_bandwidth_khz > freq_diff)
		return false;

	return true;
}

static bool is_valid_rd(struct ieee80211_regdomain *rd)
{
	struct ieee80211_reg_rule *reg_rule = NULL;
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

/* Caller must hold &cfg80211_drv_mutex */
int __regulatory_hint(struct wiphy *wiphy, enum reg_set_by set_by,
		      const char *alpha2, struct ieee80211_regdomain *rd)
{
	struct regulatory_request *request;
	char *rd_alpha2;
	int r = 0;

	r = ignore_request(wiphy, set_by, (char *) alpha2, rd);
	if (r)
		return r;

	if (rd)
		rd_alpha2 = rd->alpha2;
	else
		rd_alpha2 = (char *) alpha2;

	switch (set_by) {
	case REGDOM_SET_BY_CORE:
	case REGDOM_SET_BY_COUNTRY_IE:
	case REGDOM_SET_BY_DRIVER:
	case REGDOM_SET_BY_USER:
		request = kzalloc(sizeof(struct regulatory_request),
			GFP_KERNEL);
		if (!request)
			return -ENOMEM;

		request->alpha2[0] = rd_alpha2[0];
		request->alpha2[1] = rd_alpha2[1];
		request->initiator = set_by;
		request->wiphy = wiphy;

		list_add_tail(&request->list, &regulatory_requests);
		if (rd)
			break;
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

/* If rd is not NULL and if this call fails the caller must free it */
int regulatory_hint(struct wiphy *wiphy, const char *alpha2,
	struct ieee80211_regdomain *rd)
{
	int r;
	BUG_ON(!rd && !alpha2);

	mutex_lock(&cfg80211_drv_mutex);

	r = __regulatory_hint(wiphy, REGDOM_SET_BY_DRIVER, alpha2, rd);
	if (r || !rd)
		goto unlock_and_exit;

	/* If the driver passed a regulatory domain we skipped asking
	 * userspace for one so we can now go ahead and set it */
	r = set_regdom(rd);

unlock_and_exit:
	mutex_unlock(&cfg80211_drv_mutex);
	return r;
}
EXPORT_SYMBOL(regulatory_hint);


static void print_rd_rules(struct ieee80211_regdomain *rd)
{
	unsigned int i;
	struct ieee80211_reg_rule *reg_rule = NULL;
	struct ieee80211_freq_range *freq_range = NULL;
	struct ieee80211_power_rule *power_rule = NULL;

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

static void print_regdomain(struct ieee80211_regdomain *rd)
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

void print_regdomain_info(struct ieee80211_regdomain *rd)
{
	printk(KERN_INFO "cfg80211: Regulatory domain: %c%c\n",
		rd->alpha2[0], rd->alpha2[1]);
	print_rd_rules(rd);
}

static int __set_regdom(struct ieee80211_regdomain *rd)
{
	struct regulatory_request *request = NULL;

	/* Some basic sanity checks first */

#ifdef CONFIG_WIRELESS_OLD_REGULATORY
	/* We ignore the world regdom with the old static regdomains setup
	 * as there is no point to it with static regulatory definitions :(
	 * Don't worry this shit will be removed soon... */
	if (is_world_regdom(rd->alpha2))
		return -EINVAL;
#else
	if (is_world_regdom(rd->alpha2)) {
		if (WARN_ON(!__reg_is_valid_request(rd->alpha2, &request)))
			return -EINVAL;
		update_world_regdomain(rd);
		return 0;
	}
#endif

	if (!is_alpha2_set(rd->alpha2) && !is_an_alpha2(rd->alpha2) &&
			!is_unknown_alpha2(rd->alpha2))
		return -EINVAL;

	if (list_empty(&regulatory_requests))
		return -EINVAL;

#ifdef CONFIG_WIRELESS_OLD_REGULATORY
	/* Static "US" and "JP" will be overridden, but just once */
	if (!is_old_static_regdom(cfg80211_regdomain) &&
			!regdom_changed(rd->alpha2))
		return -EINVAL;
#else
	if (!regdom_changed(rd->alpha2))
		return -EINVAL;
#endif

	/* Now lets set the regulatory domain, update all driver channels
	 * and finally inform them of what we have done, in case they want
	 * to review or adjust their own settings based on their own
	 * internal EEPROM data */

	if (WARN_ON(!__reg_is_valid_request(rd->alpha2, &request)))
		return -EINVAL;

#ifdef CONFIG_WIRELESS_OLD_REGULATORY
	reset_regdomains_static();
#else
	reset_regdomains();
#endif

	/* Country IE parsing coming soon */
	switch (request->initiator) {
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

	/* Tada! */
	cfg80211_regdomain = rd;
	request->granted = 1;

	return 0;
}


/* Use this call to set the current regulatory domain. Conflicts with
 * multiple drivers can be ironed out later. Caller must've already
 * kmalloc'd the rd structure. If this calls fails you should kfree()
 * the passed rd. Caller must hold cfg80211_drv_mutex */
int set_regdom(struct ieee80211_regdomain *rd)
{
	struct regulatory_request *this_request = NULL, *prev_request = NULL;
	int r;

	if (!list_empty(&regulatory_requests))
		prev_request = list_first_entry(&regulatory_requests,
			struct regulatory_request, list);

	/* Note that this doesn't update the wiphys, this is done below */
	r = __set_regdom(rd);
	if (r)
		return r;

	BUG_ON((!__reg_is_valid_request(rd->alpha2, &this_request)));

	/* The initial standard core update of the world regulatory domain, no
	 * need to keep that request info around if it didn't fail. */
	if (is_world_regdom(rd->alpha2) &&
			this_request->initiator == REGDOM_SET_BY_CORE &&
			this_request->granted) {
		list_del(&this_request->list);
		kfree(this_request);
		this_request = NULL;
	}

	/* Remove old requests, we only leave behind the last one */
	if (prev_request) {
		list_del(&prev_request->list);
		kfree(prev_request);
		prev_request = NULL;
	}

	/* This would make this whole thing pointless */
	BUG_ON(rd != cfg80211_regdomain);

	/* update all wiphys now with the new established regulatory domain */
	update_all_wiphy_regulatory(this_request->initiator);

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
	cfg80211_regdomain =
		(struct ieee80211_regdomain *) static_regdom(ieee80211_regdom);
	/* Used during reset_regdomains_static() */
	cfg80211_world_regdom = cfg80211_regdomain;

	printk(KERN_INFO "cfg80211: Using old static regulatory domain:\n");
	print_regdomain_info(cfg80211_regdomain);
	/* The old code still requests for a new regdomain and if
	 * you have CRDA you get it updated, otherwise you get
	 * stuck with the static values. We ignore "EU" code as
	 * that is not a valid ISO / IEC 3166 alpha2 */
	if (ieee80211_regdom[0] != 'E' && ieee80211_regdom[1] != 'U')
		err = __regulatory_hint(NULL, REGDOM_SET_BY_CORE,
					ieee80211_regdom, NULL);
#else
	cfg80211_regdomain =
		(struct ieee80211_regdomain *) cfg80211_world_regdom;

	err = __regulatory_hint(NULL, REGDOM_SET_BY_CORE, "00", NULL);
	if (err)
		printk(KERN_ERR "cfg80211: calling CRDA failed - "
		       "unable to update world regulatory domain, "
		       "using static definition\n");
#endif

	return 0;
}

void regulatory_exit(void)
{
	struct regulatory_request *req, *req_tmp;

	mutex_lock(&cfg80211_drv_mutex);

#ifdef CONFIG_WIRELESS_OLD_REGULATORY
	reset_regdomains_static();
#else
	reset_regdomains();
#endif

	list_for_each_entry_safe(req, req_tmp, &regulatory_requests, list) {
		list_del(&req->list);
		kfree(req);
	}
	platform_device_unregister(reg_pdev);

	mutex_unlock(&cfg80211_drv_mutex);
}
