/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2008-2011	Luis R. Rodriguez <mcgrof@qca.qualcomm.com>
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/random.h>
#include <linux/ctype.h>
#include <linux/nl80211.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include <net/cfg80211.h>
#include "core.h"
#include "reg.h"
#include "regdb.h"
#include "nl80211.h"

#ifdef CONFIG_CFG80211_REG_DEBUG
#define REG_DBG_PRINT(format, args...)			\
	printk(KERN_DEBUG pr_fmt(format), ##args)
#else
#define REG_DBG_PRINT(args...)
#endif

static struct regulatory_request core_request_world = {
	.initiator = NL80211_REGDOM_SET_BY_CORE,
	.alpha2[0] = '0',
	.alpha2[1] = '0',
	.intersect = false,
	.processed = true,
	.country_ie_env = ENVIRON_ANY,
};

/* Receipt of information from last regulatory request */
static struct regulatory_request *last_request = &core_request_world;

/* To trigger userspace events */
static struct platform_device *reg_pdev;

static struct device_type reg_device_type = {
	.uevent = reg_device_uevent,
};

/*
 * Central wireless core regulatory domains, we only need two,
 * the current one and a world regulatory domain in case we have no
 * information to give us an alpha2
 */
const struct ieee80211_regdomain *cfg80211_regdomain;

/*
 * Protects static reg.c components:
 *     - cfg80211_world_regdom
 *     - cfg80211_regdom
 *     - last_request
 */
static DEFINE_MUTEX(reg_mutex);

static inline void assert_reg_lock(void)
{
	lockdep_assert_held(&reg_mutex);
}

/* Used to queue up regulatory hints */
static LIST_HEAD(reg_requests_list);
static spinlock_t reg_requests_lock;

/* Used to queue up beacon hints for review */
static LIST_HEAD(reg_pending_beacons);
static spinlock_t reg_pending_beacons_lock;

/* Used to keep track of processed beacon hints */
static LIST_HEAD(reg_beacon_list);

struct reg_beacon {
	struct list_head list;
	struct ieee80211_channel chan;
};

static void reg_todo(struct work_struct *work);
static DECLARE_WORK(reg_work, reg_todo);

static void reg_timeout_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(reg_timeout, reg_timeout_work);

/* We keep a static world regulatory domain in case of the absence of CRDA */
static const struct ieee80211_regdomain world_regdom = {
	.n_reg_rules = 5,
	.alpha2 =  "00",
	.reg_rules = {
		/* IEEE 802.11b/g, channels 1..11 */
		REG_RULE(2412-10, 2462+10, 40, 6, 20, 0),
		/* IEEE 802.11b/g, channels 12..13. No HT40
		 * channel fits here. */
		REG_RULE(2467-10, 2472+10, 20, 6, 20,
			NL80211_RRF_PASSIVE_SCAN |
			NL80211_RRF_NO_IBSS),
		/* IEEE 802.11 channel 14 - Only JP enables
		 * this and for 802.11b only */
		REG_RULE(2484-10, 2484+10, 20, 6, 20,
			NL80211_RRF_PASSIVE_SCAN |
			NL80211_RRF_NO_IBSS |
			NL80211_RRF_NO_OFDM),
		/* IEEE 802.11a, channel 36..48 */
		REG_RULE(5180-10, 5240+10, 40, 6, 20,
                        NL80211_RRF_PASSIVE_SCAN |
                        NL80211_RRF_NO_IBSS),

		/* NB: 5260 MHz - 5700 MHz requies DFS */

		/* IEEE 802.11a, channel 149..165 */
		REG_RULE(5745-10, 5825+10, 40, 6, 20,
			NL80211_RRF_PASSIVE_SCAN |
			NL80211_RRF_NO_IBSS),
	}
};

static const struct ieee80211_regdomain *cfg80211_world_regdom =
	&world_regdom;

static char *ieee80211_regdom = "00";
static char user_alpha2[2];

module_param(ieee80211_regdom, charp, 0444);
MODULE_PARM_DESC(ieee80211_regdom, "IEEE 802.11 regulatory domain code");

static void reset_regdomains(bool full_reset)
{
	/* avoid freeing static information or freeing something twice */
	if (cfg80211_regdomain == cfg80211_world_regdom)
		cfg80211_regdomain = NULL;
	if (cfg80211_world_regdom == &world_regdom)
		cfg80211_world_regdom = NULL;
	if (cfg80211_regdomain == &world_regdom)
		cfg80211_regdomain = NULL;

	kfree(cfg80211_regdomain);
	kfree(cfg80211_world_regdom);

	cfg80211_world_regdom = &world_regdom;
	cfg80211_regdomain = NULL;

	if (!full_reset)
		return;

	if (last_request != &core_request_world)
		kfree(last_request);
	last_request = &core_request_world;
}

/*
 * Dynamic world regulatory domain requested by the wireless
 * core upon initialization
 */
static void update_world_regdomain(const struct ieee80211_regdomain *rd)
{
	BUG_ON(!last_request);

	reset_regdomains(false);

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

static bool is_unknown_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;
	/*
	 * Special case where regulatory domain was built by driver
	 * but a specific alpha2 cannot be determined
	 */
	if (alpha2[0] == '9' && alpha2[1] == '9')
		return true;
	return false;
}

static bool is_intersected_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;
	/*
	 * Special case where regulatory domain is the
	 * result of an intersection between two regulatory domain
	 * structures
	 */
	if (alpha2[0] == '9' && alpha2[1] == '8')
		return true;
	return false;
}

static bool is_an_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;
	if (isalpha(alpha2[0]) && isalpha(alpha2[1]))
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

static bool regdom_changes(const char *alpha2)
{
	assert_cfg80211_lock();

	if (!cfg80211_regdomain)
		return true;
	if (alpha2_equal(cfg80211_regdomain->alpha2, alpha2))
		return false;
	return true;
}

/*
 * The NL80211_REGDOM_SET_BY_USER regdom alpha2 is cached, this lets
 * you know if a valid regulatory hint with NL80211_REGDOM_SET_BY_USER
 * has ever been issued.
 */
static bool is_user_regdom_saved(void)
{
	if (user_alpha2[0] == '9' && user_alpha2[1] == '7')
		return false;

	/* This would indicate a mistake on the design */
	if (WARN((!is_world_regdom(user_alpha2) &&
		  !is_an_alpha2(user_alpha2)),
		 "Unexpected user alpha2: %c%c\n",
		 user_alpha2[0],
	         user_alpha2[1]))
		return false;

	return true;
}

static int reg_copy_regd(const struct ieee80211_regdomain **dst_regd,
			 const struct ieee80211_regdomain *src_regd)
{
	struct ieee80211_regdomain *regd;
	int size_of_regd = 0;
	unsigned int i;

	size_of_regd = sizeof(struct ieee80211_regdomain) +
	  ((src_regd->n_reg_rules + 1) * sizeof(struct ieee80211_reg_rule));

	regd = kzalloc(size_of_regd, GFP_KERNEL);
	if (!regd)
		return -ENOMEM;

	memcpy(regd, src_regd, sizeof(struct ieee80211_regdomain));

	for (i = 0; i < src_regd->n_reg_rules; i++)
		memcpy(&regd->reg_rules[i], &src_regd->reg_rules[i],
			sizeof(struct ieee80211_reg_rule));

	*dst_regd = regd;
	return 0;
}

#ifdef CONFIG_CFG80211_INTERNAL_REGDB
struct reg_regdb_search_request {
	char alpha2[2];
	struct list_head list;
};

static LIST_HEAD(reg_regdb_search_list);
static DEFINE_MUTEX(reg_regdb_search_mutex);

static void reg_regdb_search(struct work_struct *work)
{
	struct reg_regdb_search_request *request;
	const struct ieee80211_regdomain *curdom, *regdom;
	int i, r;
	bool set_reg = false;

	mutex_lock(&cfg80211_mutex);

	mutex_lock(&reg_regdb_search_mutex);
	while (!list_empty(&reg_regdb_search_list)) {
		request = list_first_entry(&reg_regdb_search_list,
					   struct reg_regdb_search_request,
					   list);
		list_del(&request->list);

		for (i=0; i<reg_regdb_size; i++) {
			curdom = reg_regdb[i];

			if (!memcmp(request->alpha2, curdom->alpha2, 2)) {
				r = reg_copy_regd(&regdom, curdom);
				if (r)
					break;
				set_reg = true;
				break;
			}
		}

		kfree(request);
	}
	mutex_unlock(&reg_regdb_search_mutex);

	if (set_reg)
		set_regdom(regdom);

	mutex_unlock(&cfg80211_mutex);
}

static DECLARE_WORK(reg_regdb_work, reg_regdb_search);

static void reg_regdb_query(const char *alpha2)
{
	struct reg_regdb_search_request *request;

	if (!alpha2)
		return;

	request = kzalloc(sizeof(struct reg_regdb_search_request), GFP_KERNEL);
	if (!request)
		return;

	memcpy(request->alpha2, alpha2, 2);

	mutex_lock(&reg_regdb_search_mutex);
	list_add_tail(&request->list, &reg_regdb_search_list);
	mutex_unlock(&reg_regdb_search_mutex);

	schedule_work(&reg_regdb_work);
}

/* Feel free to add any other sanity checks here */
static void reg_regdb_size_check(void)
{
	/* We should ideally BUILD_BUG_ON() but then random builds would fail */
	WARN_ONCE(!reg_regdb_size, "db.txt is empty, you should update it...");
}
#else
static inline void reg_regdb_size_check(void) {}
static inline void reg_regdb_query(const char *alpha2) {}
#endif /* CONFIG_CFG80211_INTERNAL_REGDB */

/*
 * This lets us keep regulatory code which is updated on a regulatory
 * basis in userspace. Country information is filled in by
 * reg_device_uevent
 */
static int call_crda(const char *alpha2)
{
	if (!is_world_regdom((char *) alpha2))
		pr_info("Calling CRDA for country: %c%c\n",
			alpha2[0], alpha2[1]);
	else
		pr_info("Calling CRDA to update world regulatory domain\n");

	/* query internal regulatory database (if it exists) */
	reg_regdb_query(alpha2);

	return kobject_uevent(&reg_pdev->dev.kobj, KOBJ_CHANGE);
}

/* Used by nl80211 before kmalloc'ing our regulatory domain */
bool reg_is_valid_request(const char *alpha2)
{
	assert_cfg80211_lock();

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

	if (freq_range->end_freq_khz <= freq_range->start_freq_khz ||
			freq_range->max_bandwidth_khz > freq_diff)
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

static bool reg_does_bw_fit(const struct ieee80211_freq_range *freq_range,
			    u32 center_freq_khz,
			    u32 bw_khz)
{
	u32 start_freq_khz, end_freq_khz;

	start_freq_khz = center_freq_khz - (bw_khz/2);
	end_freq_khz = center_freq_khz + (bw_khz/2);

	if (start_freq_khz >= freq_range->start_freq_khz &&
	    end_freq_khz <= freq_range->end_freq_khz)
		return true;

	return false;
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

/*
 * Helper for regdom_intersect(), this does the real
 * mathematical intersection fun
 */
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

	/*
	 * First we get a count of the rules we'll need, then we actually
	 * build them. This is to so we can malloc() and free() a
	 * regdomain once. The reason we use reg_rules_intersect() here
	 * is it will return -EINVAL if the rule computed makes no sense.
	 * All rules that do check out OK are valid.
	 */

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
			/*
			 * This time around instead of using the stack lets
			 * write to the target rule directly saving ourselves
			 * a memcpy()
			 */
			intersected_rule = &rd->reg_rules[rule_idx];
			r = reg_rules_intersect(rule1, rule2,
				intersected_rule);
			/*
			 * No need to memset here the intersected rule here as
			 * we're not using the stack anymore
			 */
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

/*
 * XXX: add support for the rest of enum nl80211_reg_rule_flags, we may
 * want to just have the channel structure use these
 */
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

static int freq_reg_info_regd(struct wiphy *wiphy,
			      u32 center_freq,
			      u32 desired_bw_khz,
			      const struct ieee80211_reg_rule **reg_rule,
			      const struct ieee80211_regdomain *custom_regd)
{
	int i;
	bool band_rule_found = false;
	const struct ieee80211_regdomain *regd;
	bool bw_fits = false;

	if (!desired_bw_khz)
		desired_bw_khz = MHZ_TO_KHZ(20);

	regd = custom_regd ? custom_regd : cfg80211_regdomain;

	/*
	 * Follow the driver's regulatory domain, if present, unless a country
	 * IE has been processed or a user wants to help complaince further
	 */
	if (!custom_regd &&
	    last_request->initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE &&
	    last_request->initiator != NL80211_REGDOM_SET_BY_USER &&
	    wiphy->regd)
		regd = wiphy->regd;

	if (!regd)
		return -EINVAL;

	for (i = 0; i < regd->n_reg_rules; i++) {
		const struct ieee80211_reg_rule *rr;
		const struct ieee80211_freq_range *fr = NULL;

		rr = &regd->reg_rules[i];
		fr = &rr->freq_range;

		/*
		 * We only need to know if one frequency rule was
		 * was in center_freq's band, that's enough, so lets
		 * not overwrite it once found
		 */
		if (!band_rule_found)
			band_rule_found = freq_in_rule_band(fr, center_freq);

		bw_fits = reg_does_bw_fit(fr,
					  center_freq,
					  desired_bw_khz);

		if (band_rule_found && bw_fits) {
			*reg_rule = rr;
			return 0;
		}
	}

	if (!band_rule_found)
		return -ERANGE;

	return -EINVAL;
}

int freq_reg_info(struct wiphy *wiphy,
		  u32 center_freq,
		  u32 desired_bw_khz,
		  const struct ieee80211_reg_rule **reg_rule)
{
	assert_cfg80211_lock();
	return freq_reg_info_regd(wiphy,
				  center_freq,
				  desired_bw_khz,
				  reg_rule,
				  NULL);
}
EXPORT_SYMBOL(freq_reg_info);

#ifdef CONFIG_CFG80211_REG_DEBUG
static const char *reg_initiator_name(enum nl80211_reg_initiator initiator)
{
	switch (initiator) {
	case NL80211_REGDOM_SET_BY_CORE:
		return "Set by core";
	case NL80211_REGDOM_SET_BY_USER:
		return "Set by user";
	case NL80211_REGDOM_SET_BY_DRIVER:
		return "Set by driver";
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		return "Set by country IE";
	default:
		WARN_ON(1);
		return "Set by bug";
	}
}

static void chan_reg_rule_print_dbg(struct ieee80211_channel *chan,
				    u32 desired_bw_khz,
				    const struct ieee80211_reg_rule *reg_rule)
{
	const struct ieee80211_power_rule *power_rule;
	const struct ieee80211_freq_range *freq_range;
	char max_antenna_gain[32];

	power_rule = &reg_rule->power_rule;
	freq_range = &reg_rule->freq_range;

	if (!power_rule->max_antenna_gain)
		snprintf(max_antenna_gain, 32, "N/A");
	else
		snprintf(max_antenna_gain, 32, "%d", power_rule->max_antenna_gain);

	REG_DBG_PRINT("Updating information on frequency %d MHz "
		      "for a %d MHz width channel with regulatory rule:\n",
		      chan->center_freq,
		      KHZ_TO_MHZ(desired_bw_khz));

	REG_DBG_PRINT("%d KHz - %d KHz @ %d KHz), (%s mBi, %d mBm)\n",
		      freq_range->start_freq_khz,
		      freq_range->end_freq_khz,
		      freq_range->max_bandwidth_khz,
		      max_antenna_gain,
		      power_rule->max_eirp);
}
#else
static void chan_reg_rule_print_dbg(struct ieee80211_channel *chan,
				    u32 desired_bw_khz,
				    const struct ieee80211_reg_rule *reg_rule)
{
	return;
}
#endif

/*
 * Note that right now we assume the desired channel bandwidth
 * is always 20 MHz for each individual channel (HT40 uses 20 MHz
 * per channel, the primary and the extension channel). To support
 * smaller custom bandwidths such as 5 MHz or 10 MHz we'll need a
 * new ieee80211_channel.target_bw and re run the regulatory check
 * on the wiphy with the target_bw specified. Then we can simply use
 * that below for the desired_bw_khz below.
 */
static void handle_channel(struct wiphy *wiphy,
			   enum nl80211_reg_initiator initiator,
			   enum ieee80211_band band,
			   unsigned int chan_idx)
{
	int r;
	u32 flags, bw_flags = 0;
	u32 desired_bw_khz = MHZ_TO_KHZ(20);
	const struct ieee80211_reg_rule *reg_rule = NULL;
	const struct ieee80211_power_rule *power_rule = NULL;
	const struct ieee80211_freq_range *freq_range = NULL;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	struct wiphy *request_wiphy = NULL;

	assert_cfg80211_lock();

	request_wiphy = wiphy_idx_to_wiphy(last_request->wiphy_idx);

	sband = wiphy->bands[band];
	BUG_ON(chan_idx >= sband->n_channels);
	chan = &sband->channels[chan_idx];

	flags = chan->orig_flags;

	r = freq_reg_info(wiphy,
			  MHZ_TO_KHZ(chan->center_freq),
			  desired_bw_khz,
			  &reg_rule);

	if (r) {
		/*
		 * We will disable all channels that do not match our
		 * received regulatory rule unless the hint is coming
		 * from a Country IE and the Country IE had no information
		 * about a band. The IEEE 802.11 spec allows for an AP
		 * to send only a subset of the regulatory rules allowed,
		 * so an AP in the US that only supports 2.4 GHz may only send
		 * a country IE with information for the 2.4 GHz band
		 * while 5 GHz is still supported.
		 */
		if (initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE &&
		    r == -ERANGE)
			return;

		REG_DBG_PRINT("Disabling freq %d MHz\n", chan->center_freq);
		chan->flags = IEEE80211_CHAN_DISABLED;
		return;
	}

	chan_reg_rule_print_dbg(chan, desired_bw_khz, reg_rule);

	power_rule = &reg_rule->power_rule;
	freq_range = &reg_rule->freq_range;

	if (freq_range->max_bandwidth_khz < MHZ_TO_KHZ(40))
		bw_flags = IEEE80211_CHAN_NO_HT40;

	if (last_request->initiator == NL80211_REGDOM_SET_BY_DRIVER &&
	    request_wiphy && request_wiphy == wiphy &&
	    request_wiphy->flags & WIPHY_FLAG_STRICT_REGULATORY) {
		/*
		 * This guarantees the driver's requested regulatory domain
		 * will always be used as a base for further regulatory
		 * settings
		 */
		chan->flags = chan->orig_flags =
			map_regdom_flags(reg_rule->flags) | bw_flags;
		chan->max_antenna_gain = chan->orig_mag =
			(int) MBI_TO_DBI(power_rule->max_antenna_gain);
		chan->max_power = chan->orig_mpwr =
			(int) MBM_TO_DBM(power_rule->max_eirp);
		return;
	}

	chan->beacon_found = false;
	chan->flags = flags | bw_flags | map_regdom_flags(reg_rule->flags);
	chan->max_antenna_gain = min(chan->orig_mag,
		(int) MBI_TO_DBI(power_rule->max_antenna_gain));
	chan->max_reg_power = (int) MBM_TO_DBM(power_rule->max_eirp);
	if (chan->orig_mpwr) {
		/*
		 * Devices that have their own custom regulatory domain
		 * but also use WIPHY_FLAG_STRICT_REGULATORY will follow the
		 * passed country IE power settings.
		 */
		if (initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE &&
		    wiphy->flags & WIPHY_FLAG_CUSTOM_REGULATORY &&
		    wiphy->flags & WIPHY_FLAG_STRICT_REGULATORY)
			chan->max_power = chan->max_reg_power;
		else
			chan->max_power = min(chan->orig_mpwr,
					      chan->max_reg_power);
	} else
		chan->max_power = chan->max_reg_power;
}

static void handle_band(struct wiphy *wiphy,
			enum ieee80211_band band,
			enum nl80211_reg_initiator initiator)
{
	unsigned int i;
	struct ieee80211_supported_band *sband;

	BUG_ON(!wiphy->bands[band]);
	sband = wiphy->bands[band];

	for (i = 0; i < sband->n_channels; i++)
		handle_channel(wiphy, initiator, band, i);
}

static bool ignore_reg_update(struct wiphy *wiphy,
			      enum nl80211_reg_initiator initiator)
{
	if (!last_request) {
		REG_DBG_PRINT("Ignoring regulatory request %s since "
			      "last_request is not set\n",
			      reg_initiator_name(initiator));
		return true;
	}

	if (initiator == NL80211_REGDOM_SET_BY_CORE &&
	    wiphy->flags & WIPHY_FLAG_CUSTOM_REGULATORY) {
		REG_DBG_PRINT("Ignoring regulatory request %s "
			      "since the driver uses its own custom "
			      "regulatory domain\n",
			      reg_initiator_name(initiator));
		return true;
	}

	/*
	 * wiphy->regd will be set once the device has its own
	 * desired regulatory domain set
	 */
	if (wiphy->flags & WIPHY_FLAG_STRICT_REGULATORY && !wiphy->regd &&
	    initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE &&
	    !is_world_regdom(last_request->alpha2)) {
		REG_DBG_PRINT("Ignoring regulatory request %s "
			      "since the driver requires its own regulatory "
			      "domain to be set first\n",
			      reg_initiator_name(initiator));
		return true;
	}

	return false;
}

static void handle_reg_beacon(struct wiphy *wiphy,
			      unsigned int chan_idx,
			      struct reg_beacon *reg_beacon)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	bool channel_changed = false;
	struct ieee80211_channel chan_before;

	assert_cfg80211_lock();

	sband = wiphy->bands[reg_beacon->chan.band];
	chan = &sband->channels[chan_idx];

	if (likely(chan->center_freq != reg_beacon->chan.center_freq))
		return;

	if (chan->beacon_found)
		return;

	chan->beacon_found = true;

	if (wiphy->flags & WIPHY_FLAG_DISABLE_BEACON_HINTS)
		return;

	chan_before.center_freq = chan->center_freq;
	chan_before.flags = chan->flags;

	if (chan->flags & IEEE80211_CHAN_PASSIVE_SCAN) {
		chan->flags &= ~IEEE80211_CHAN_PASSIVE_SCAN;
		channel_changed = true;
	}

	if (chan->flags & IEEE80211_CHAN_NO_IBSS) {
		chan->flags &= ~IEEE80211_CHAN_NO_IBSS;
		channel_changed = true;
	}

	if (channel_changed)
		nl80211_send_beacon_hint_event(wiphy, &chan_before, chan);
}

/*
 * Called when a scan on a wiphy finds a beacon on
 * new channel
 */
static void wiphy_update_new_beacon(struct wiphy *wiphy,
				    struct reg_beacon *reg_beacon)
{
	unsigned int i;
	struct ieee80211_supported_band *sband;

	assert_cfg80211_lock();

	if (!wiphy->bands[reg_beacon->chan.band])
		return;

	sband = wiphy->bands[reg_beacon->chan.band];

	for (i = 0; i < sband->n_channels; i++)
		handle_reg_beacon(wiphy, i, reg_beacon);
}

/*
 * Called upon reg changes or a new wiphy is added
 */
static void wiphy_update_beacon_reg(struct wiphy *wiphy)
{
	unsigned int i;
	struct ieee80211_supported_band *sband;
	struct reg_beacon *reg_beacon;

	assert_cfg80211_lock();

	if (list_empty(&reg_beacon_list))
		return;

	list_for_each_entry(reg_beacon, &reg_beacon_list, list) {
		if (!wiphy->bands[reg_beacon->chan.band])
			continue;
		sband = wiphy->bands[reg_beacon->chan.band];
		for (i = 0; i < sband->n_channels; i++)
			handle_reg_beacon(wiphy, i, reg_beacon);
	}
}

static bool reg_is_world_roaming(struct wiphy *wiphy)
{
	if (is_world_regdom(cfg80211_regdomain->alpha2) ||
	    (wiphy->regd && is_world_regdom(wiphy->regd->alpha2)))
		return true;
	if (last_request &&
	    last_request->initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE &&
	    wiphy->flags & WIPHY_FLAG_CUSTOM_REGULATORY)
		return true;
	return false;
}

/* Reap the advantages of previously found beacons */
static void reg_process_beacons(struct wiphy *wiphy)
{
	/*
	 * Means we are just firing up cfg80211, so no beacons would
	 * have been processed yet.
	 */
	if (!last_request)
		return;
	if (!reg_is_world_roaming(wiphy))
		return;
	wiphy_update_beacon_reg(wiphy);
}

static bool is_ht40_not_allowed(struct ieee80211_channel *chan)
{
	if (!chan)
		return true;
	if (chan->flags & IEEE80211_CHAN_DISABLED)
		return true;
	/* This would happen when regulatory rules disallow HT40 completely */
	if (IEEE80211_CHAN_NO_HT40 == (chan->flags & (IEEE80211_CHAN_NO_HT40)))
		return true;
	return false;
}

static void reg_process_ht_flags_channel(struct wiphy *wiphy,
					 enum ieee80211_band band,
					 unsigned int chan_idx)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *channel;
	struct ieee80211_channel *channel_before = NULL, *channel_after = NULL;
	unsigned int i;

	assert_cfg80211_lock();

	sband = wiphy->bands[band];
	BUG_ON(chan_idx >= sband->n_channels);
	channel = &sband->channels[chan_idx];

	if (is_ht40_not_allowed(channel)) {
		channel->flags |= IEEE80211_CHAN_NO_HT40;
		return;
	}

	/*
	 * We need to ensure the extension channels exist to
	 * be able to use HT40- or HT40+, this finds them (or not)
	 */
	for (i = 0; i < sband->n_channels; i++) {
		struct ieee80211_channel *c = &sband->channels[i];
		if (c->center_freq == (channel->center_freq - 20))
			channel_before = c;
		if (c->center_freq == (channel->center_freq + 20))
			channel_after = c;
	}

	/*
	 * Please note that this assumes target bandwidth is 20 MHz,
	 * if that ever changes we also need to change the below logic
	 * to include that as well.
	 */
	if (is_ht40_not_allowed(channel_before))
		channel->flags |= IEEE80211_CHAN_NO_HT40MINUS;
	else
		channel->flags &= ~IEEE80211_CHAN_NO_HT40MINUS;

	if (is_ht40_not_allowed(channel_after))
		channel->flags |= IEEE80211_CHAN_NO_HT40PLUS;
	else
		channel->flags &= ~IEEE80211_CHAN_NO_HT40PLUS;
}

static void reg_process_ht_flags_band(struct wiphy *wiphy,
				      enum ieee80211_band band)
{
	unsigned int i;
	struct ieee80211_supported_band *sband;

	BUG_ON(!wiphy->bands[band]);
	sband = wiphy->bands[band];

	for (i = 0; i < sband->n_channels; i++)
		reg_process_ht_flags_channel(wiphy, band, i);
}

static void reg_process_ht_flags(struct wiphy *wiphy)
{
	enum ieee80211_band band;

	if (!wiphy)
		return;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (wiphy->bands[band])
			reg_process_ht_flags_band(wiphy, band);
	}

}

static void wiphy_update_regulatory(struct wiphy *wiphy,
				    enum nl80211_reg_initiator initiator)
{
	enum ieee80211_band band;

	assert_reg_lock();

	if (ignore_reg_update(wiphy, initiator))
		return;

	last_request->dfs_region = cfg80211_regdomain->dfs_region;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (wiphy->bands[band])
			handle_band(wiphy, band, initiator);
	}

	reg_process_beacons(wiphy);
	reg_process_ht_flags(wiphy);
	if (wiphy->reg_notifier)
		wiphy->reg_notifier(wiphy, last_request);
}

void regulatory_update(struct wiphy *wiphy,
		       enum nl80211_reg_initiator setby)
{
	mutex_lock(&reg_mutex);
	wiphy_update_regulatory(wiphy, setby);
	mutex_unlock(&reg_mutex);
}

static void update_all_wiphy_regulatory(enum nl80211_reg_initiator initiator)
{
	struct cfg80211_registered_device *rdev;
	struct wiphy *wiphy;

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		wiphy = &rdev->wiphy;
		wiphy_update_regulatory(wiphy, initiator);
		/*
		 * Regulatory updates set by CORE are ignored for custom
		 * regulatory cards. Let us notify the changes to the driver,
		 * as some drivers used this to restore its orig_* reg domain.
		 */
		if (initiator == NL80211_REGDOM_SET_BY_CORE &&
		    wiphy->flags & WIPHY_FLAG_CUSTOM_REGULATORY &&
		    wiphy->reg_notifier)
			wiphy->reg_notifier(wiphy, last_request);
	}
}

static void handle_channel_custom(struct wiphy *wiphy,
				  enum ieee80211_band band,
				  unsigned int chan_idx,
				  const struct ieee80211_regdomain *regd)
{
	int r;
	u32 desired_bw_khz = MHZ_TO_KHZ(20);
	u32 bw_flags = 0;
	const struct ieee80211_reg_rule *reg_rule = NULL;
	const struct ieee80211_power_rule *power_rule = NULL;
	const struct ieee80211_freq_range *freq_range = NULL;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;

	assert_reg_lock();

	sband = wiphy->bands[band];
	BUG_ON(chan_idx >= sband->n_channels);
	chan = &sband->channels[chan_idx];

	r = freq_reg_info_regd(wiphy,
			       MHZ_TO_KHZ(chan->center_freq),
			       desired_bw_khz,
			       &reg_rule,
			       regd);

	if (r) {
		REG_DBG_PRINT("Disabling freq %d MHz as custom "
			      "regd has no rule that fits a %d MHz "
			      "wide channel\n",
			      chan->center_freq,
			      KHZ_TO_MHZ(desired_bw_khz));
		chan->flags = IEEE80211_CHAN_DISABLED;
		return;
	}

	chan_reg_rule_print_dbg(chan, desired_bw_khz, reg_rule);

	power_rule = &reg_rule->power_rule;
	freq_range = &reg_rule->freq_range;

	if (freq_range->max_bandwidth_khz < MHZ_TO_KHZ(40))
		bw_flags = IEEE80211_CHAN_NO_HT40;

	chan->flags |= map_regdom_flags(reg_rule->flags) | bw_flags;
	chan->max_antenna_gain = (int) MBI_TO_DBI(power_rule->max_antenna_gain);
	chan->max_power = (int) MBM_TO_DBM(power_rule->max_eirp);
}

static void handle_band_custom(struct wiphy *wiphy, enum ieee80211_band band,
			       const struct ieee80211_regdomain *regd)
{
	unsigned int i;
	struct ieee80211_supported_band *sband;

	BUG_ON(!wiphy->bands[band]);
	sband = wiphy->bands[band];

	for (i = 0; i < sband->n_channels; i++)
		handle_channel_custom(wiphy, band, i, regd);
}

/* Used by drivers prior to wiphy registration */
void wiphy_apply_custom_regulatory(struct wiphy *wiphy,
				   const struct ieee80211_regdomain *regd)
{
	enum ieee80211_band band;
	unsigned int bands_set = 0;

	mutex_lock(&reg_mutex);
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (!wiphy->bands[band])
			continue;
		handle_band_custom(wiphy, band, regd);
		bands_set++;
	}
	mutex_unlock(&reg_mutex);

	/*
	 * no point in calling this if it won't have any effect
	 * on your device's supportd bands.
	 */
	WARN_ON(!bands_set);
}
EXPORT_SYMBOL(wiphy_apply_custom_regulatory);

/*
 * Return value which can be used by ignore_request() to indicate
 * it has been determined we should intersect two regulatory domains
 */
#define REG_INTERSECT	1

/* This has the logic which determines when a new request
 * should be ignored. */
static int ignore_request(struct wiphy *wiphy,
			  struct regulatory_request *pending_request)
{
	struct wiphy *last_wiphy = NULL;

	assert_cfg80211_lock();

	/* All initial requests are respected */
	if (!last_request)
		return 0;

	switch (pending_request->initiator) {
	case NL80211_REGDOM_SET_BY_CORE:
		return 0;
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:

		last_wiphy = wiphy_idx_to_wiphy(last_request->wiphy_idx);

		if (unlikely(!is_an_alpha2(pending_request->alpha2)))
			return -EINVAL;
		if (last_request->initiator ==
		    NL80211_REGDOM_SET_BY_COUNTRY_IE) {
			if (last_wiphy != wiphy) {
				/*
				 * Two cards with two APs claiming different
				 * Country IE alpha2s. We could
				 * intersect them, but that seems unlikely
				 * to be correct. Reject second one for now.
				 */
				if (regdom_changes(pending_request->alpha2))
					return -EOPNOTSUPP;
				return -EALREADY;
			}
			/*
			 * Two consecutive Country IE hints on the same wiphy.
			 * This should be picked up early by the driver/stack
			 */
			if (WARN_ON(regdom_changes(pending_request->alpha2)))
				return 0;
			return -EALREADY;
		}
		return 0;
	case NL80211_REGDOM_SET_BY_DRIVER:
		if (last_request->initiator == NL80211_REGDOM_SET_BY_CORE) {
			if (regdom_changes(pending_request->alpha2))
				return 0;
			return -EALREADY;
		}

		/*
		 * This would happen if you unplug and plug your card
		 * back in or if you add a new device for which the previously
		 * loaded card also agrees on the regulatory domain.
		 */
		if (last_request->initiator == NL80211_REGDOM_SET_BY_DRIVER &&
		    !regdom_changes(pending_request->alpha2))
			return -EALREADY;

		return REG_INTERSECT;
	case NL80211_REGDOM_SET_BY_USER:
		if (last_request->initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE)
			return REG_INTERSECT;
		/*
		 * If the user knows better the user should set the regdom
		 * to their country before the IE is picked up
		 */
		if (last_request->initiator == NL80211_REGDOM_SET_BY_USER &&
			  last_request->intersect)
			return -EOPNOTSUPP;
		/*
		 * Process user requests only after previous user/driver/core
		 * requests have been processed
		 */
		if (last_request->initiator == NL80211_REGDOM_SET_BY_CORE ||
		    last_request->initiator == NL80211_REGDOM_SET_BY_DRIVER ||
		    last_request->initiator == NL80211_REGDOM_SET_BY_USER) {
			if (regdom_changes(last_request->alpha2))
				return -EAGAIN;
		}

		if (!regdom_changes(pending_request->alpha2))
			return -EALREADY;

		return 0;
	}

	return -EINVAL;
}

static void reg_set_request_processed(void)
{
	bool need_more_processing = false;

	last_request->processed = true;

	spin_lock(&reg_requests_lock);
	if (!list_empty(&reg_requests_list))
		need_more_processing = true;
	spin_unlock(&reg_requests_lock);

	if (last_request->initiator == NL80211_REGDOM_SET_BY_USER)
		cancel_delayed_work(&reg_timeout);

	if (need_more_processing)
		schedule_work(&reg_work);
}

/**
 * __regulatory_hint - hint to the wireless core a regulatory domain
 * @wiphy: if the hint comes from country information from an AP, this
 *	is required to be set to the wiphy that received the information
 * @pending_request: the regulatory request currently being processed
 *
 * The Wireless subsystem can use this function to hint to the wireless core
 * what it believes should be the current regulatory domain.
 *
 * Returns zero if all went fine, %-EALREADY if a regulatory domain had
 * already been set or other standard error codes.
 *
 * Caller must hold &cfg80211_mutex and &reg_mutex
 */
static int __regulatory_hint(struct wiphy *wiphy,
			     struct regulatory_request *pending_request)
{
	bool intersect = false;
	int r = 0;

	assert_cfg80211_lock();

	r = ignore_request(wiphy, pending_request);

	if (r == REG_INTERSECT) {
		if (pending_request->initiator ==
		    NL80211_REGDOM_SET_BY_DRIVER) {
			r = reg_copy_regd(&wiphy->regd, cfg80211_regdomain);
			if (r) {
				kfree(pending_request);
				return r;
			}
		}
		intersect = true;
	} else if (r) {
		/*
		 * If the regulatory domain being requested by the
		 * driver has already been set just copy it to the
		 * wiphy
		 */
		if (r == -EALREADY &&
		    pending_request->initiator ==
		    NL80211_REGDOM_SET_BY_DRIVER) {
			r = reg_copy_regd(&wiphy->regd, cfg80211_regdomain);
			if (r) {
				kfree(pending_request);
				return r;
			}
			r = -EALREADY;
			goto new_request;
		}
		kfree(pending_request);
		return r;
	}

new_request:
	if (last_request != &core_request_world)
		kfree(last_request);

	last_request = pending_request;
	last_request->intersect = intersect;

	pending_request = NULL;

	if (last_request->initiator == NL80211_REGDOM_SET_BY_USER) {
		user_alpha2[0] = last_request->alpha2[0];
		user_alpha2[1] = last_request->alpha2[1];
	}

	/* When r == REG_INTERSECT we do need to call CRDA */
	if (r < 0) {
		/*
		 * Since CRDA will not be called in this case as we already
		 * have applied the requested regulatory domain before we just
		 * inform userspace we have processed the request
		 */
		if (r == -EALREADY) {
			nl80211_send_reg_change_event(last_request);
			reg_set_request_processed();
		}
		return r;
	}

	return call_crda(last_request->alpha2);
}

/* This processes *all* regulatory hints */
static void reg_process_hint(struct regulatory_request *reg_request,
			     enum nl80211_reg_initiator reg_initiator)
{
	int r = 0;
	struct wiphy *wiphy = NULL;

	BUG_ON(!reg_request->alpha2);

	if (wiphy_idx_valid(reg_request->wiphy_idx))
		wiphy = wiphy_idx_to_wiphy(reg_request->wiphy_idx);

	if (reg_initiator == NL80211_REGDOM_SET_BY_DRIVER &&
	    !wiphy) {
		kfree(reg_request);
		return;
	}

	r = __regulatory_hint(wiphy, reg_request);
	/* This is required so that the orig_* parameters are saved */
	if (r == -EALREADY && wiphy &&
	    wiphy->flags & WIPHY_FLAG_STRICT_REGULATORY) {
		wiphy_update_regulatory(wiphy, reg_initiator);
		return;
	}

	/*
	 * We only time out user hints, given that they should be the only
	 * source of bogus requests.
	 */
	if (r != -EALREADY &&
	    reg_initiator == NL80211_REGDOM_SET_BY_USER)
		schedule_delayed_work(&reg_timeout, msecs_to_jiffies(3142));
}

/*
 * Processes regulatory hints, this is all the NL80211_REGDOM_SET_BY_*
 * Regulatory hints come on a first come first serve basis and we
 * must process each one atomically.
 */
static void reg_process_pending_hints(void)
{
	struct regulatory_request *reg_request;

	mutex_lock(&cfg80211_mutex);
	mutex_lock(&reg_mutex);

	/* When last_request->processed becomes true this will be rescheduled */
	if (last_request && !last_request->processed) {
		REG_DBG_PRINT("Pending regulatory request, waiting "
			      "for it to be processed...\n");
		goto out;
	}

	spin_lock(&reg_requests_lock);

	if (list_empty(&reg_requests_list)) {
		spin_unlock(&reg_requests_lock);
		goto out;
	}

	reg_request = list_first_entry(&reg_requests_list,
				       struct regulatory_request,
				       list);
	list_del_init(&reg_request->list);

	spin_unlock(&reg_requests_lock);

	reg_process_hint(reg_request, reg_request->initiator);

out:
	mutex_unlock(&reg_mutex);
	mutex_unlock(&cfg80211_mutex);
}

/* Processes beacon hints -- this has nothing to do with country IEs */
static void reg_process_pending_beacon_hints(void)
{
	struct cfg80211_registered_device *rdev;
	struct reg_beacon *pending_beacon, *tmp;

	/*
	 * No need to hold the reg_mutex here as we just touch wiphys
	 * and do not read or access regulatory variables.
	 */
	mutex_lock(&cfg80211_mutex);

	/* This goes through the _pending_ beacon list */
	spin_lock_bh(&reg_pending_beacons_lock);

	if (list_empty(&reg_pending_beacons)) {
		spin_unlock_bh(&reg_pending_beacons_lock);
		goto out;
	}

	list_for_each_entry_safe(pending_beacon, tmp,
				 &reg_pending_beacons, list) {

		list_del_init(&pending_beacon->list);

		/* Applies the beacon hint to current wiphys */
		list_for_each_entry(rdev, &cfg80211_rdev_list, list)
			wiphy_update_new_beacon(&rdev->wiphy, pending_beacon);

		/* Remembers the beacon hint for new wiphys or reg changes */
		list_add_tail(&pending_beacon->list, &reg_beacon_list);
	}

	spin_unlock_bh(&reg_pending_beacons_lock);
out:
	mutex_unlock(&cfg80211_mutex);
}

static void reg_todo(struct work_struct *work)
{
	reg_process_pending_hints();
	reg_process_pending_beacon_hints();
}

static void queue_regulatory_request(struct regulatory_request *request)
{
	if (isalpha(request->alpha2[0]))
		request->alpha2[0] = toupper(request->alpha2[0]);
	if (isalpha(request->alpha2[1]))
		request->alpha2[1] = toupper(request->alpha2[1]);

	spin_lock(&reg_requests_lock);
	list_add_tail(&request->list, &reg_requests_list);
	spin_unlock(&reg_requests_lock);

	schedule_work(&reg_work);
}

/*
 * Core regulatory hint -- happens during cfg80211_init()
 * and when we restore regulatory settings.
 */
static int regulatory_hint_core(const char *alpha2)
{
	struct regulatory_request *request;

	request = kzalloc(sizeof(struct regulatory_request),
			  GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->alpha2[0] = alpha2[0];
	request->alpha2[1] = alpha2[1];
	request->initiator = NL80211_REGDOM_SET_BY_CORE;

	queue_regulatory_request(request);

	return 0;
}

/* User hints */
int regulatory_hint_user(const char *alpha2)
{
	struct regulatory_request *request;

	BUG_ON(!alpha2);

	request = kzalloc(sizeof(struct regulatory_request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->wiphy_idx = WIPHY_IDX_STALE;
	request->alpha2[0] = alpha2[0];
	request->alpha2[1] = alpha2[1];
	request->initiator = NL80211_REGDOM_SET_BY_USER;

	queue_regulatory_request(request);

	return 0;
}

/* Driver hints */
int regulatory_hint(struct wiphy *wiphy, const char *alpha2)
{
	struct regulatory_request *request;

	BUG_ON(!alpha2);
	BUG_ON(!wiphy);

	request = kzalloc(sizeof(struct regulatory_request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->wiphy_idx = get_wiphy_idx(wiphy);

	/* Must have registered wiphy first */
	BUG_ON(!wiphy_idx_valid(request->wiphy_idx));

	request->alpha2[0] = alpha2[0];
	request->alpha2[1] = alpha2[1];
	request->initiator = NL80211_REGDOM_SET_BY_DRIVER;

	queue_regulatory_request(request);

	return 0;
}
EXPORT_SYMBOL(regulatory_hint);

/*
 * We hold wdev_lock() here so we cannot hold cfg80211_mutex() and
 * therefore cannot iterate over the rdev list here.
 */
void regulatory_hint_11d(struct wiphy *wiphy,
			 enum ieee80211_band band,
			 u8 *country_ie,
			 u8 country_ie_len)
{
	char alpha2[2];
	enum environment_cap env = ENVIRON_ANY;
	struct regulatory_request *request;

	mutex_lock(&reg_mutex);

	if (unlikely(!last_request))
		goto out;

	/* IE len must be evenly divisible by 2 */
	if (country_ie_len & 0x01)
		goto out;

	if (country_ie_len < IEEE80211_COUNTRY_IE_MIN_LEN)
		goto out;

	alpha2[0] = country_ie[0];
	alpha2[1] = country_ie[1];

	if (country_ie[2] == 'I')
		env = ENVIRON_INDOOR;
	else if (country_ie[2] == 'O')
		env = ENVIRON_OUTDOOR;

	/*
	 * We will run this only upon a successful connection on cfg80211.
	 * We leave conflict resolution to the workqueue, where can hold
	 * cfg80211_mutex.
	 */
	if (likely(last_request->initiator ==
	    NL80211_REGDOM_SET_BY_COUNTRY_IE &&
	    wiphy_idx_valid(last_request->wiphy_idx)))
		goto out;

	request = kzalloc(sizeof(struct regulatory_request), GFP_KERNEL);
	if (!request)
		goto out;

	request->wiphy_idx = get_wiphy_idx(wiphy);
	request->alpha2[0] = alpha2[0];
	request->alpha2[1] = alpha2[1];
	request->initiator = NL80211_REGDOM_SET_BY_COUNTRY_IE;
	request->country_ie_env = env;

	mutex_unlock(&reg_mutex);

	queue_regulatory_request(request);

	return;

out:
	mutex_unlock(&reg_mutex);
}

static void restore_alpha2(char *alpha2, bool reset_user)
{
	/* indicates there is no alpha2 to consider for restoration */
	alpha2[0] = '9';
	alpha2[1] = '7';

	/* The user setting has precedence over the module parameter */
	if (is_user_regdom_saved()) {
		/* Unless we're asked to ignore it and reset it */
		if (reset_user) {
			REG_DBG_PRINT("Restoring regulatory settings "
			       "including user preference\n");
			user_alpha2[0] = '9';
			user_alpha2[1] = '7';

			/*
			 * If we're ignoring user settings, we still need to
			 * check the module parameter to ensure we put things
			 * back as they were for a full restore.
			 */
			if (!is_world_regdom(ieee80211_regdom)) {
				REG_DBG_PRINT("Keeping preference on "
				       "module parameter ieee80211_regdom: %c%c\n",
				       ieee80211_regdom[0],
				       ieee80211_regdom[1]);
				alpha2[0] = ieee80211_regdom[0];
				alpha2[1] = ieee80211_regdom[1];
			}
		} else {
			REG_DBG_PRINT("Restoring regulatory settings "
			       "while preserving user preference for: %c%c\n",
			       user_alpha2[0],
			       user_alpha2[1]);
			alpha2[0] = user_alpha2[0];
			alpha2[1] = user_alpha2[1];
		}
	} else if (!is_world_regdom(ieee80211_regdom)) {
		REG_DBG_PRINT("Keeping preference on "
		       "module parameter ieee80211_regdom: %c%c\n",
		       ieee80211_regdom[0],
		       ieee80211_regdom[1]);
		alpha2[0] = ieee80211_regdom[0];
		alpha2[1] = ieee80211_regdom[1];
	} else
		REG_DBG_PRINT("Restoring regulatory settings\n");
}

static void restore_custom_reg_settings(struct wiphy *wiphy)
{
	struct ieee80211_supported_band *sband;
	enum ieee80211_band band;
	struct ieee80211_channel *chan;
	int i;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		sband = wiphy->bands[band];
		if (!sband)
			continue;
		for (i = 0; i < sband->n_channels; i++) {
			chan = &sband->channels[i];
			chan->flags = chan->orig_flags;
			chan->max_antenna_gain = chan->orig_mag;
			chan->max_power = chan->orig_mpwr;
		}
	}
}

/*
 * Restoring regulatory settings involves ingoring any
 * possibly stale country IE information and user regulatory
 * settings if so desired, this includes any beacon hints
 * learned as we could have traveled outside to another country
 * after disconnection. To restore regulatory settings we do
 * exactly what we did at bootup:
 *
 *   - send a core regulatory hint
 *   - send a user regulatory hint if applicable
 *
 * Device drivers that send a regulatory hint for a specific country
 * keep their own regulatory domain on wiphy->regd so that does does
 * not need to be remembered.
 */
static void restore_regulatory_settings(bool reset_user)
{
	char alpha2[2];
	char world_alpha2[2];
	struct reg_beacon *reg_beacon, *btmp;
	struct regulatory_request *reg_request, *tmp;
	LIST_HEAD(tmp_reg_req_list);
	struct cfg80211_registered_device *rdev;

	mutex_lock(&cfg80211_mutex);
	mutex_lock(&reg_mutex);

	reset_regdomains(true);
	restore_alpha2(alpha2, reset_user);

	/*
	 * If there's any pending requests we simply
	 * stash them to a temporary pending queue and
	 * add then after we've restored regulatory
	 * settings.
	 */
	spin_lock(&reg_requests_lock);
	if (!list_empty(&reg_requests_list)) {
		list_for_each_entry_safe(reg_request, tmp,
					 &reg_requests_list, list) {
			if (reg_request->initiator !=
			    NL80211_REGDOM_SET_BY_USER)
				continue;
			list_del(&reg_request->list);
			list_add_tail(&reg_request->list, &tmp_reg_req_list);
		}
	}
	spin_unlock(&reg_requests_lock);

	/* Clear beacon hints */
	spin_lock_bh(&reg_pending_beacons_lock);
	if (!list_empty(&reg_pending_beacons)) {
		list_for_each_entry_safe(reg_beacon, btmp,
					 &reg_pending_beacons, list) {
			list_del(&reg_beacon->list);
			kfree(reg_beacon);
		}
	}
	spin_unlock_bh(&reg_pending_beacons_lock);

	if (!list_empty(&reg_beacon_list)) {
		list_for_each_entry_safe(reg_beacon, btmp,
					 &reg_beacon_list, list) {
			list_del(&reg_beacon->list);
			kfree(reg_beacon);
		}
	}

	/* First restore to the basic regulatory settings */
	cfg80211_regdomain = cfg80211_world_regdom;
	world_alpha2[0] = cfg80211_regdomain->alpha2[0];
	world_alpha2[1] = cfg80211_regdomain->alpha2[1];

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		if (rdev->wiphy.flags & WIPHY_FLAG_CUSTOM_REGULATORY)
			restore_custom_reg_settings(&rdev->wiphy);
	}

	mutex_unlock(&reg_mutex);
	mutex_unlock(&cfg80211_mutex);

	regulatory_hint_core(world_alpha2);

	/*
	 * This restores the ieee80211_regdom module parameter
	 * preference or the last user requested regulatory
	 * settings, user regulatory settings takes precedence.
	 */
	if (is_an_alpha2(alpha2))
		regulatory_hint_user(user_alpha2);

	if (list_empty(&tmp_reg_req_list))
		return;

	mutex_lock(&cfg80211_mutex);
	mutex_lock(&reg_mutex);

	spin_lock(&reg_requests_lock);
	list_for_each_entry_safe(reg_request, tmp, &tmp_reg_req_list, list) {
		REG_DBG_PRINT("Adding request for country %c%c back "
			      "into the queue\n",
			      reg_request->alpha2[0],
			      reg_request->alpha2[1]);
		list_del(&reg_request->list);
		list_add_tail(&reg_request->list, &reg_requests_list);
	}
	spin_unlock(&reg_requests_lock);

	mutex_unlock(&reg_mutex);
	mutex_unlock(&cfg80211_mutex);

	REG_DBG_PRINT("Kicking the queue\n");

	schedule_work(&reg_work);
}

void regulatory_hint_disconnect(void)
{
	REG_DBG_PRINT("All devices are disconnected, going to "
		      "restore regulatory settings\n");
	restore_regulatory_settings(false);
}

static bool freq_is_chan_12_13_14(u16 freq)
{
	if (freq == ieee80211_channel_to_frequency(12, IEEE80211_BAND_2GHZ) ||
	    freq == ieee80211_channel_to_frequency(13, IEEE80211_BAND_2GHZ) ||
	    freq == ieee80211_channel_to_frequency(14, IEEE80211_BAND_2GHZ))
		return true;
	return false;
}

int regulatory_hint_found_beacon(struct wiphy *wiphy,
				 struct ieee80211_channel *beacon_chan,
				 gfp_t gfp)
{
	struct reg_beacon *reg_beacon;

	if (likely((beacon_chan->beacon_found ||
	    (beacon_chan->flags & IEEE80211_CHAN_RADAR) ||
	    (beacon_chan->band == IEEE80211_BAND_2GHZ &&
	     !freq_is_chan_12_13_14(beacon_chan->center_freq)))))
		return 0;

	reg_beacon = kzalloc(sizeof(struct reg_beacon), gfp);
	if (!reg_beacon)
		return -ENOMEM;

	REG_DBG_PRINT("Found new beacon on "
		      "frequency: %d MHz (Ch %d) on %s\n",
		      beacon_chan->center_freq,
		      ieee80211_frequency_to_channel(beacon_chan->center_freq),
		      wiphy_name(wiphy));

	memcpy(&reg_beacon->chan, beacon_chan,
		sizeof(struct ieee80211_channel));


	/*
	 * Since we can be called from BH or and non-BH context
	 * we must use spin_lock_bh()
	 */
	spin_lock_bh(&reg_pending_beacons_lock);
	list_add_tail(&reg_beacon->list, &reg_pending_beacons);
	spin_unlock_bh(&reg_pending_beacons_lock);

	schedule_work(&reg_work);

	return 0;
}

static void print_rd_rules(const struct ieee80211_regdomain *rd)
{
	unsigned int i;
	const struct ieee80211_reg_rule *reg_rule = NULL;
	const struct ieee80211_freq_range *freq_range = NULL;
	const struct ieee80211_power_rule *power_rule = NULL;

	pr_info("  (start_freq - end_freq @ bandwidth), (max_antenna_gain, max_eirp)\n");

	for (i = 0; i < rd->n_reg_rules; i++) {
		reg_rule = &rd->reg_rules[i];
		freq_range = &reg_rule->freq_range;
		power_rule = &reg_rule->power_rule;

		/*
		 * There may not be documentation for max antenna gain
		 * in certain regions
		 */
		if (power_rule->max_antenna_gain)
			pr_info("  (%d KHz - %d KHz @ %d KHz), (%d mBi, %d mBm)\n",
				freq_range->start_freq_khz,
				freq_range->end_freq_khz,
				freq_range->max_bandwidth_khz,
				power_rule->max_antenna_gain,
				power_rule->max_eirp);
		else
			pr_info("  (%d KHz - %d KHz @ %d KHz), (N/A, %d mBm)\n",
				freq_range->start_freq_khz,
				freq_range->end_freq_khz,
				freq_range->max_bandwidth_khz,
				power_rule->max_eirp);
	}
}

bool reg_supported_dfs_region(u8 dfs_region)
{
	switch (dfs_region) {
	case NL80211_DFS_UNSET:
	case NL80211_DFS_FCC:
	case NL80211_DFS_ETSI:
	case NL80211_DFS_JP:
		return true;
	default:
		REG_DBG_PRINT("Ignoring uknown DFS master region: %d\n",
			      dfs_region);
		return false;
	}
}

static void print_dfs_region(u8 dfs_region)
{
	if (!dfs_region)
		return;

	switch (dfs_region) {
	case NL80211_DFS_FCC:
		pr_info(" DFS Master region FCC");
		break;
	case NL80211_DFS_ETSI:
		pr_info(" DFS Master region ETSI");
		break;
	case NL80211_DFS_JP:
		pr_info(" DFS Master region JP");
		break;
	default:
		pr_info(" DFS Master region Uknown");
		break;
	}
}

static void print_regdomain(const struct ieee80211_regdomain *rd)
{

	if (is_intersected_alpha2(rd->alpha2)) {

		if (last_request->initiator ==
		    NL80211_REGDOM_SET_BY_COUNTRY_IE) {
			struct cfg80211_registered_device *rdev;
			rdev = cfg80211_rdev_by_wiphy_idx(
				last_request->wiphy_idx);
			if (rdev) {
				pr_info("Current regulatory domain updated by AP to: %c%c\n",
					rdev->country_ie_alpha2[0],
					rdev->country_ie_alpha2[1]);
			} else
				pr_info("Current regulatory domain intersected:\n");
		} else
			pr_info("Current regulatory domain intersected:\n");
	} else if (is_world_regdom(rd->alpha2))
		pr_info("World regulatory domain updated:\n");
	else {
		if (is_unknown_alpha2(rd->alpha2))
			pr_info("Regulatory domain changed to driver built-in settings (unknown country)\n");
		else
			pr_info("Regulatory domain changed to country: %c%c\n",
				rd->alpha2[0], rd->alpha2[1]);
	}
	print_dfs_region(rd->dfs_region);
	print_rd_rules(rd);
}

static void print_regdomain_info(const struct ieee80211_regdomain *rd)
{
	pr_info("Regulatory domain: %c%c\n", rd->alpha2[0], rd->alpha2[1]);
	print_rd_rules(rd);
}

/* Takes ownership of rd only if it doesn't fail */
static int __set_regdom(const struct ieee80211_regdomain *rd)
{
	const struct ieee80211_regdomain *intersected_rd = NULL;
	struct cfg80211_registered_device *rdev = NULL;
	struct wiphy *request_wiphy;
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

	/*
	 * Lets only bother proceeding on the same alpha2 if the current
	 * rd is non static (it means CRDA was present and was used last)
	 * and the pending request came in from a country IE
	 */
	if (last_request->initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE) {
		/*
		 * If someone else asked us to change the rd lets only bother
		 * checking if the alpha2 changes if CRDA was already called
		 */
		if (!regdom_changes(rd->alpha2))
			return -EINVAL;
	}

	/*
	 * Now lets set the regulatory domain, update all driver channels
	 * and finally inform them of what we have done, in case they want
	 * to review or adjust their own settings based on their own
	 * internal EEPROM data
	 */

	if (WARN_ON(!reg_is_valid_request(rd->alpha2)))
		return -EINVAL;

	if (!is_valid_rd(rd)) {
		pr_err("Invalid regulatory domain detected:\n");
		print_regdomain_info(rd);
		return -EINVAL;
	}

	request_wiphy = wiphy_idx_to_wiphy(last_request->wiphy_idx);
	if (!request_wiphy &&
	    (last_request->initiator == NL80211_REGDOM_SET_BY_DRIVER ||
	     last_request->initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE)) {
		schedule_delayed_work(&reg_timeout, 0);
		return -ENODEV;
	}

	if (!last_request->intersect) {
		int r;

		if (last_request->initiator != NL80211_REGDOM_SET_BY_DRIVER) {
			reset_regdomains(false);
			cfg80211_regdomain = rd;
			return 0;
		}

		/*
		 * For a driver hint, lets copy the regulatory domain the
		 * driver wanted to the wiphy to deal with conflicts
		 */

		/*
		 * Userspace could have sent two replies with only
		 * one kernel request.
		 */
		if (request_wiphy->regd)
			return -EALREADY;

		r = reg_copy_regd(&request_wiphy->regd, rd);
		if (r)
			return r;

		reset_regdomains(false);
		cfg80211_regdomain = rd;
		return 0;
	}

	/* Intersection requires a bit more work */

	if (last_request->initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE) {

		intersected_rd = regdom_intersect(rd, cfg80211_regdomain);
		if (!intersected_rd)
			return -EINVAL;

		/*
		 * We can trash what CRDA provided now.
		 * However if a driver requested this specific regulatory
		 * domain we keep it for its private use
		 */
		if (last_request->initiator == NL80211_REGDOM_SET_BY_DRIVER)
			request_wiphy->regd = rd;
		else
			kfree(rd);

		rd = NULL;

		reset_regdomains(false);
		cfg80211_regdomain = intersected_rd;

		return 0;
	}

	if (!intersected_rd)
		return -EINVAL;

	rdev = wiphy_to_dev(request_wiphy);

	rdev->country_ie_alpha2[0] = rd->alpha2[0];
	rdev->country_ie_alpha2[1] = rd->alpha2[1];
	rdev->env = last_request->country_ie_env;

	BUG_ON(intersected_rd == rd);

	kfree(rd);
	rd = NULL;

	reset_regdomains(false);
	cfg80211_regdomain = intersected_rd;

	return 0;
}


/*
 * Use this call to set the current regulatory domain. Conflicts with
 * multiple drivers can be ironed out later. Caller must've already
 * kmalloc'd the rd structure. Caller must hold cfg80211_mutex
 */
int set_regdom(const struct ieee80211_regdomain *rd)
{
	int r;

	assert_cfg80211_lock();

	mutex_lock(&reg_mutex);

	/* Note that this doesn't update the wiphys, this is done below */
	r = __set_regdom(rd);
	if (r) {
		kfree(rd);
		mutex_unlock(&reg_mutex);
		return r;
	}

	/* This would make this whole thing pointless */
	if (!last_request->intersect)
		BUG_ON(rd != cfg80211_regdomain);

	/* update all wiphys now with the new established regulatory domain */
	update_all_wiphy_regulatory(last_request->initiator);

	print_regdomain(cfg80211_regdomain);

	nl80211_send_reg_change_event(last_request);

	reg_set_request_processed();

	mutex_unlock(&reg_mutex);

	return r;
}

#ifdef CONFIG_HOTPLUG
int reg_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	if (last_request && !last_request->processed) {
		if (add_uevent_var(env, "COUNTRY=%c%c",
				   last_request->alpha2[0],
				   last_request->alpha2[1]))
			return -ENOMEM;
	}

	return 0;
}
#else
int reg_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return -ENODEV;
}
#endif /* CONFIG_HOTPLUG */

/* Caller must hold cfg80211_mutex */
void reg_device_remove(struct wiphy *wiphy)
{
	struct wiphy *request_wiphy = NULL;

	assert_cfg80211_lock();

	mutex_lock(&reg_mutex);

	kfree(wiphy->regd);

	if (last_request)
		request_wiphy = wiphy_idx_to_wiphy(last_request->wiphy_idx);

	if (!request_wiphy || request_wiphy != wiphy)
		goto out;

	last_request->wiphy_idx = WIPHY_IDX_STALE;
	last_request->country_ie_env = ENVIRON_ANY;
out:
	mutex_unlock(&reg_mutex);
}

static void reg_timeout_work(struct work_struct *work)
{
	REG_DBG_PRINT("Timeout while waiting for CRDA to reply, "
		      "restoring regulatory settings\n");
	restore_regulatory_settings(true);
}

int __init regulatory_init(void)
{
	int err = 0;

	reg_pdev = platform_device_register_simple("regulatory", 0, NULL, 0);
	if (IS_ERR(reg_pdev))
		return PTR_ERR(reg_pdev);

	reg_pdev->dev.type = &reg_device_type;

	spin_lock_init(&reg_requests_lock);
	spin_lock_init(&reg_pending_beacons_lock);

	reg_regdb_size_check();

	cfg80211_regdomain = cfg80211_world_regdom;

	user_alpha2[0] = '9';
	user_alpha2[1] = '7';

	/* We always try to get an update for the static regdomain */
	err = regulatory_hint_core(cfg80211_regdomain->alpha2);
	if (err) {
		if (err == -ENOMEM)
			return err;
		/*
		 * N.B. kobject_uevent_env() can fail mainly for when we're out
		 * memory which is handled and propagated appropriately above
		 * but it can also fail during a netlink_broadcast() or during
		 * early boot for call_usermodehelper(). For now treat these
		 * errors as non-fatal.
		 */
		pr_err("kobject_uevent_env() was unable to call CRDA during init\n");
#ifdef CONFIG_CFG80211_REG_DEBUG
		/* We want to find out exactly why when debugging */
		WARN_ON(err);
#endif
	}

	/*
	 * Finally, if the user set the module parameter treat it
	 * as a user hint.
	 */
	if (!is_world_regdom(ieee80211_regdom))
		regulatory_hint_user(ieee80211_regdom);

	return 0;
}

void /* __init_or_exit */ regulatory_exit(void)
{
	struct regulatory_request *reg_request, *tmp;
	struct reg_beacon *reg_beacon, *btmp;

	cancel_work_sync(&reg_work);
	cancel_delayed_work_sync(&reg_timeout);

	mutex_lock(&cfg80211_mutex);
	mutex_lock(&reg_mutex);

	reset_regdomains(true);

	dev_set_uevent_suppress(&reg_pdev->dev, true);

	platform_device_unregister(reg_pdev);

	spin_lock_bh(&reg_pending_beacons_lock);
	if (!list_empty(&reg_pending_beacons)) {
		list_for_each_entry_safe(reg_beacon, btmp,
					 &reg_pending_beacons, list) {
			list_del(&reg_beacon->list);
			kfree(reg_beacon);
		}
	}
	spin_unlock_bh(&reg_pending_beacons_lock);

	if (!list_empty(&reg_beacon_list)) {
		list_for_each_entry_safe(reg_beacon, btmp,
					 &reg_beacon_list, list) {
			list_del(&reg_beacon->list);
			kfree(reg_beacon);
		}
	}

	spin_lock(&reg_requests_lock);
	if (!list_empty(&reg_requests_list)) {
		list_for_each_entry_safe(reg_request, tmp,
					 &reg_requests_list, list) {
			list_del(&reg_request->list);
			kfree(reg_request);
		}
	}
	spin_unlock(&reg_requests_lock);

	mutex_unlock(&reg_mutex);
	mutex_unlock(&cfg80211_mutex);
}
