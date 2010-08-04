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
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/random.h>
#include <linux/nl80211.h>
#include <linux/platform_device.h>
#include <net/cfg80211.h>
#include "core.h"
#include "reg.h"
#include "regdb.h"
#include "nl80211.h"

#ifdef CONFIG_CFG80211_REG_DEBUG
#define REG_DBG_PRINT(format, args...) \
	do { \
		printk(KERN_DEBUG format , ## args); \
	} while (0)
#else
#define REG_DBG_PRINT(args...)
#endif

/* Receipt of information from last regulatory request */
static struct regulatory_request *last_request;

/* To trigger userspace events */
static struct platform_device *reg_pdev;

/*
 * Central wireless core regulatory domains, we only need two,
 * the current one and a world regulatory domain in case we have no
 * information to give us an alpha2
 */
const struct ieee80211_regdomain *cfg80211_regdomain;

/*
 * We use this as a place for the rd structure built from the
 * last parsed country IE to rest until CRDA gets back to us with
 * what it thinks should apply for the same country
 */
static const struct ieee80211_regdomain *country_ie_regdomain;

/*
 * Protects static reg.c components:
 *     - cfg80211_world_regdom
 *     - cfg80211_regdom
 *     - country_ie_regdomain
 *     - last_request
 */
DEFINE_MUTEX(reg_mutex);
#define assert_reg_lock() WARN_ON(!mutex_is_locked(&reg_mutex))

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

static void reset_regdomains(void)
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
}

/*
 * Dynamic world regulatory domain requested by the wireless
 * core upon initialization
 */
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
				mutex_lock(&cfg80211_mutex);
				set_regdom(regdom);
				mutex_unlock(&cfg80211_mutex);
				break;
			}
		}

		kfree(request);
	}
	mutex_unlock(&reg_regdb_search_mutex);
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
#else
static inline void reg_regdb_query(const char *alpha2) {}
#endif /* CONFIG_CFG80211_INTERNAL_REGDB */

/*
 * This lets us keep regulatory code which is updated on a regulatory
 * basis in userspace.
 */
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

	/* query internal regulatory database (if it exists) */
	reg_regdb_query(alpha2);

	country_env[8] = alpha2[0];
	country_env[9] = alpha2[1];

	return kobject_uevent_env(&reg_pdev->dev.kobj, KOBJ_CHANGE, envp);
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
 * This is a work around for sanity checking ieee80211_channel_to_frequency()'s
 * work. ieee80211_channel_to_frequency() can for example currently provide a
 * 2 GHz channel when in fact a 5 GHz channel was desired. An example would be
 * an AP providing channel 8 on a country IE triplet when it sent this on the
 * 5 GHz band, that channel is designed to be channel 8 on 5 GHz, not a 2 GHz
 * channel.
 *
 * This can be removed once ieee80211_channel_to_frequency() takes in a band.
 */
static bool chan_in_band(int chan, enum ieee80211_band band)
{
	int center_freq = ieee80211_channel_to_frequency(chan);

	switch (band) {
	case IEEE80211_BAND_2GHZ:
		if (center_freq <= 2484)
			return true;
		return false;
	case IEEE80211_BAND_5GHZ:
		if (center_freq >= 5005)
			return true;
		return false;
	default:
		return false;
	}
}

/*
 * Some APs may send a country IE triplet for each channel they
 * support and while this is completely overkill and silly we still
 * need to support it. We avoid making a single rule for each channel
 * though and to help us with this we use this helper to find the
 * actual subband end channel. These type of country IE triplet
 * scenerios are handled then, all yielding two regulaotry rules from
 * parsing a country IE:
 *
 * [1]
 * [2]
 * [36]
 * [40]
 *
 * [1]
 * [2-4]
 * [5-12]
 * [36]
 * [40-44]
 *
 * [1-4]
 * [5-7]
 * [36-44]
 * [48-64]
 *
 * [36-36]
 * [40-40]
 * [44-44]
 * [48-48]
 * [52-52]
 * [56-56]
 * [60-60]
 * [64-64]
 * [100-100]
 * [104-104]
 * [108-108]
 * [112-112]
 * [116-116]
 * [120-120]
 * [124-124]
 * [128-128]
 * [132-132]
 * [136-136]
 * [140-140]
 *
 * Returns 0 if the IE has been found to be invalid in the middle
 * somewhere.
 */
static int max_subband_chan(enum ieee80211_band band,
			    int orig_cur_chan,
			    int orig_end_channel,
			    s8 orig_max_power,
			    u8 **country_ie,
			    u8 *country_ie_len)
{
	u8 *triplets_start = *country_ie;
	u8 len_at_triplet = *country_ie_len;
	int end_subband_chan = orig_end_channel;

	/*
	 * We'll deal with padding for the caller unless
	 * its not immediate and we don't process any channels
	 */
	if (*country_ie_len == 1) {
		*country_ie += 1;
		*country_ie_len -= 1;
		return orig_end_channel;
	}

	/* Move to the next triplet and then start search */
	*country_ie += 3;
	*country_ie_len -= 3;

	if (!chan_in_band(orig_cur_chan, band))
		return 0;

	while (*country_ie_len >= 3) {
		int end_channel = 0;
		struct ieee80211_country_ie_triplet *triplet =
			(struct ieee80211_country_ie_triplet *) *country_ie;
		int cur_channel = 0, next_expected_chan;

		/* means last triplet is completely unrelated to this one */
		if (triplet->ext.reg_extension_id >=
				IEEE80211_COUNTRY_EXTENSION_ID) {
			*country_ie -= 3;
			*country_ie_len += 3;
			break;
		}

		if (triplet->chans.first_channel == 0) {
			*country_ie += 1;
			*country_ie_len -= 1;
			if (*country_ie_len != 0)
				return 0;
			break;
		}

		if (triplet->chans.num_channels == 0)
			return 0;

		/* Monitonically increasing channel order */
		if (triplet->chans.first_channel <= end_subband_chan)
			return 0;

		if (!chan_in_band(triplet->chans.first_channel, band))
			return 0;

		/* 2 GHz */
		if (triplet->chans.first_channel <= 14) {
			end_channel = triplet->chans.first_channel +
				triplet->chans.num_channels - 1;
		}
		else {
			end_channel =  triplet->chans.first_channel +
				(4 * (triplet->chans.num_channels - 1));
		}

		if (!chan_in_band(end_channel, band))
			return 0;

		if (orig_max_power != triplet->chans.max_power) {
			*country_ie -= 3;
			*country_ie_len += 3;
			break;
		}

		cur_channel = triplet->chans.first_channel;

		/* The key is finding the right next expected channel */
		if (band == IEEE80211_BAND_2GHZ)
			next_expected_chan = end_subband_chan + 1;
		 else
			next_expected_chan = end_subband_chan + 4;

		if (cur_channel != next_expected_chan) {
			*country_ie -= 3;
			*country_ie_len += 3;
			break;
		}

		end_subband_chan = end_channel;

		/* Move to the next one */
		*country_ie += 3;
		*country_ie_len -= 3;

		/*
		 * Padding needs to be dealt with if we processed
		 * some channels.
		 */
		if (*country_ie_len == 1) {
			*country_ie += 1;
			*country_ie_len -= 1;
			break;
		}

		/* If seen, the IE is invalid */
		if (*country_ie_len == 2)
			return 0;
	}

	if (end_subband_chan == orig_end_channel) {
		*country_ie = triplets_start;
		*country_ie_len = len_at_triplet;
		return orig_end_channel;
	}

	return end_subband_chan;
}

/*
 * Converts a country IE to a regulatory domain. A regulatory domain
 * structure has a lot of information which the IE doesn't yet have,
 * so for the other values we use upper max values as we will intersect
 * with our userspace regulatory agent to get lower bounds.
 */
static struct ieee80211_regdomain *country_ie_2_rd(
				enum ieee80211_band band,
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

	/*
	 * We need to build a reg rule for each triplet, but first we must
	 * calculate the number of reg rules we will need. We will need one
	 * for each channel subband
	 */
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

		/*
		 * APs can add padding to make length divisible
		 * by two, required by the spec.
		 */
		if (triplet->chans.first_channel == 0) {
			country_ie++;
			country_ie_len--;
			/* This is expected to be at the very end only */
			if (country_ie_len != 0)
				return NULL;
			break;
		}

		if (triplet->chans.num_channels == 0)
			return NULL;

		if (!chan_in_band(triplet->chans.first_channel, band))
			return NULL;

		/* 2 GHz */
		if (band == IEEE80211_BAND_2GHZ)
			end_channel = triplet->chans.first_channel +
				triplet->chans.num_channels - 1;
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

		/*
		 * Enhancement for APs that send a triplet for every channel
		 * or for whatever reason sends triplets with multiple channels
		 * separated when in fact they should be together.
		 */
		end_channel = max_subband_chan(band,
					       cur_channel,
					       end_channel,
					       triplet->chans.max_power,
					       &country_ie,
					       &country_ie_len);
		if (!end_channel)
			return NULL;

		if (!chan_in_band(end_channel, band))
			return NULL;

		cur_sub_max_channel = end_channel;

		/* Basic sanity check */
		if (cur_sub_max_channel < cur_channel)
			return NULL;

		/*
		 * Do not allow overlapping channels. Also channels
		 * passed in each subband must be monotonically
		 * increasing
		 */
		if (last_sub_max_channel) {
			if (cur_channel <= last_sub_max_channel)
				return NULL;
			if (cur_sub_max_channel <= last_sub_max_channel)
				return NULL;
		}

		/*
		 * When dot11RegulatoryClassesRequired is supported
		 * we can throw ext triplets as part of this soup,
		 * for now we don't care when those change as we
		 * don't support them
		 */
		*checksum ^= ((cur_channel ^ cur_sub_max_channel) << 8) |
		  ((cur_sub_max_channel ^ cur_sub_max_channel) << 16) |
		  ((triplet->chans.max_power ^ cur_sub_max_channel) << 24);

		last_sub_max_channel = cur_sub_max_channel;

		num_rules++;

		if (country_ie_len >= 3) {
			country_ie += 3;
			country_ie_len -= 3;
		}

		/*
		 * Note: this is not a IEEE requirement but
		 * simply a memory requirement
		 */
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

		/*
		 * Must parse if dot11RegulatoryClassesRequired is true,
		 * we don't support this yet
		 */
		if (triplet->ext.reg_extension_id >=
				IEEE80211_COUNTRY_EXTENSION_ID) {
			country_ie += 3;
			country_ie_len -= 3;
			continue;
		}

		if (triplet->chans.first_channel == 0) {
			country_ie++;
			country_ie_len--;
			break;
		}

		reg_rule = &rd->reg_rules[i];
		freq_range = &reg_rule->freq_range;
		power_rule = &reg_rule->power_rule;

		reg_rule->flags = flags;

		/* 2 GHz */
		if (band == IEEE80211_BAND_2GHZ)
			end_channel = triplet->chans.first_channel +
				triplet->chans.num_channels -1;
		else
			end_channel =  triplet->chans.first_channel +
				(4 * (triplet->chans.num_channels - 1));

		end_channel = max_subband_chan(band,
					       triplet->chans.first_channel,
					       end_channel,
					       triplet->chans.max_power,
					       &country_ie,
					       &country_ie_len);

		/*
		 * The +10 is since the regulatory domain expects
		 * the actual band edge, not the center of freq for
		 * its start and end freqs, assuming 20 MHz bandwidth on
		 * the channels passed
		 */
		freq_range->start_freq_khz =
			MHZ_TO_KHZ(ieee80211_channel_to_frequency(
				triplet->chans.first_channel) - 10);
		freq_range->end_freq_khz =
			MHZ_TO_KHZ(ieee80211_channel_to_frequency(
				end_channel) + 10);

		/*
		 * These are large arbitrary values we use to intersect later.
		 * Increment this if we ever support >= 40 MHz channels
		 * in IEEE 802.11
		 */
		freq_range->max_bandwidth_khz = MHZ_TO_KHZ(40);
		power_rule->max_antenna_gain = DBI_TO_MBI(100);
		power_rule->max_eirp = DBM_TO_MBM(triplet->chans.max_power);

		i++;

		if (country_ie_len >= 3) {
			country_ie += 3;
			country_ie_len -= 3;
		}

		BUG_ON(i > NL80211_MAX_SUPP_REG_RULES);
	}

	return rd;
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
	if (last_request->initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE &&
	    last_request->initiator != NL80211_REGDOM_SET_BY_USER &&
	    wiphy->regd)
		regd = wiphy->regd;

	if (!regd)
		return -EINVAL;

	for (i = 0; i < regd->n_reg_rules; i++) {
		const struct ieee80211_reg_rule *rr;
		const struct ieee80211_freq_range *fr = NULL;
		const struct ieee80211_power_rule *pr = NULL;

		rr = &regd->reg_rules[i];
		fr = &rr->freq_range;
		pr = &rr->power_rule;

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
EXPORT_SYMBOL(freq_reg_info);

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

/*
 * Note that right now we assume the desired channel bandwidth
 * is always 20 MHz for each individual channel (HT40 uses 20 MHz
 * per channel, the primary and the extension channel). To support
 * smaller custom bandwidths such as 5 MHz or 10 MHz we'll need a
 * new ieee80211_channel.target_bw and re run the regulatory check
 * on the wiphy with the target_bw specified. Then we can simply use
 * that below for the desired_bw_khz below.
 */
static void handle_channel(struct wiphy *wiphy, enum ieee80211_band band,
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
		 * This means no regulatory rule was found in the country IE
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
		    last_request->initiator ==
		    NL80211_REGDOM_SET_BY_COUNTRY_IE) {
			REG_DBG_PRINT("cfg80211: Leaving channel %d MHz "
				"intact on %s - no rule found in band on "
				"Country IE\n",
			chan->center_freq, wiphy_name(wiphy));
		} else {
		/*
		 * In this case we know the country IE has at least one reg rule
		 * for the band so we respect its band definitions
		 */
			if (last_request->initiator ==
			    NL80211_REGDOM_SET_BY_COUNTRY_IE)
				REG_DBG_PRINT("cfg80211: Disabling "
					"channel %d MHz on %s due to "
					"Country IE\n",
					chan->center_freq, wiphy_name(wiphy));
			flags |= IEEE80211_CHAN_DISABLED;
			chan->flags = flags;
		}
		return;
	}

	power_rule = &reg_rule->power_rule;
	freq_range = &reg_rule->freq_range;

	if (freq_range->max_bandwidth_khz < MHZ_TO_KHZ(40))
		bw_flags = IEEE80211_CHAN_NO_HT40;

	if (last_request->initiator == NL80211_REGDOM_SET_BY_DRIVER &&
	    request_wiphy && request_wiphy == wiphy &&
	    request_wiphy->flags & WIPHY_FLAG_STRICT_REGULATORY) {
		/*
		 * This gaurantees the driver's requested regulatory domain
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

	chan->flags = flags | bw_flags | map_regdom_flags(reg_rule->flags);
	chan->max_antenna_gain = min(chan->orig_mag,
		(int) MBI_TO_DBI(power_rule->max_antenna_gain));
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

static bool ignore_reg_update(struct wiphy *wiphy,
			      enum nl80211_reg_initiator initiator)
{
	if (!last_request)
		return true;
	if (initiator == NL80211_REGDOM_SET_BY_CORE &&
	    wiphy->flags & WIPHY_FLAG_CUSTOM_REGULATORY)
		return true;
	/*
	 * wiphy->regd will be set once the device has its own
	 * desired regulatory domain set
	 */
	if (wiphy->flags & WIPHY_FLAG_STRICT_REGULATORY && !wiphy->regd &&
	    !is_world_regdom(last_request->alpha2))
		return true;
	return false;
}

static void update_all_wiphy_regulatory(enum nl80211_reg_initiator initiator)
{
	struct cfg80211_registered_device *rdev;

	list_for_each_entry(rdev, &cfg80211_rdev_list, list)
		wiphy_update_regulatory(&rdev->wiphy, initiator);
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

void wiphy_update_regulatory(struct wiphy *wiphy,
			     enum nl80211_reg_initiator initiator)
{
	enum ieee80211_band band;

	if (ignore_reg_update(wiphy, initiator))
		goto out;
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (wiphy->bands[band])
			handle_band(wiphy, band);
	}
out:
	reg_process_beacons(wiphy);
	reg_process_ht_flags(wiphy);
	if (wiphy->reg_notifier)
		wiphy->reg_notifier(wiphy, last_request);
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
		chan->flags = IEEE80211_CHAN_DISABLED;
		return;
	}

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
		return REG_INTERSECT;
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
		if (r == -EALREADY)
			nl80211_send_reg_change_event(last_request);
		return r;
	}

	return call_crda(last_request->alpha2);
}

/* This processes *all* regulatory hints */
static void reg_process_hint(struct regulatory_request *reg_request)
{
	int r = 0;
	struct wiphy *wiphy = NULL;

	BUG_ON(!reg_request->alpha2);

	mutex_lock(&cfg80211_mutex);
	mutex_lock(&reg_mutex);

	if (wiphy_idx_valid(reg_request->wiphy_idx))
		wiphy = wiphy_idx_to_wiphy(reg_request->wiphy_idx);

	if (reg_request->initiator == NL80211_REGDOM_SET_BY_DRIVER &&
	    !wiphy) {
		kfree(reg_request);
		goto out;
	}

	r = __regulatory_hint(wiphy, reg_request);
	/* This is required so that the orig_* parameters are saved */
	if (r == -EALREADY && wiphy &&
	    wiphy->flags & WIPHY_FLAG_STRICT_REGULATORY)
		wiphy_update_regulatory(wiphy, reg_request->initiator);
out:
	mutex_unlock(&reg_mutex);
	mutex_unlock(&cfg80211_mutex);
}

/* Processes regulatory hints, this is all the NL80211_REGDOM_SET_BY_* */
static void reg_process_pending_hints(void)
	{
	struct regulatory_request *reg_request;

	spin_lock(&reg_requests_lock);
	while (!list_empty(&reg_requests_list)) {
		reg_request = list_first_entry(&reg_requests_list,
					       struct regulatory_request,
					       list);
		list_del_init(&reg_request->list);

		spin_unlock(&reg_requests_lock);
		reg_process_hint(reg_request);
		spin_lock(&reg_requests_lock);
	}
	spin_unlock(&reg_requests_lock);
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

static DECLARE_WORK(reg_work, reg_todo);

static void queue_regulatory_request(struct regulatory_request *request)
{
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

	kfree(last_request);
	last_request = NULL;

	request = kzalloc(sizeof(struct regulatory_request),
			  GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->alpha2[0] = alpha2[0];
	request->alpha2[1] = alpha2[1];
	request->initiator = NL80211_REGDOM_SET_BY_CORE;

	/*
	 * This ensures last_request is populated once modules
	 * come swinging in and calling regulatory hints and
	 * wiphy_apply_custom_regulatory().
	 */
	reg_process_hint(request);

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

/* Caller must hold reg_mutex */
static bool reg_same_country_ie_hint(struct wiphy *wiphy,
			u32 country_ie_checksum)
{
	struct wiphy *request_wiphy;

	assert_reg_lock();

	if (unlikely(last_request->initiator !=
	    NL80211_REGDOM_SET_BY_COUNTRY_IE))
		return false;

	request_wiphy = wiphy_idx_to_wiphy(last_request->wiphy_idx);

	if (!request_wiphy)
		return false;

	if (likely(request_wiphy != wiphy))
		return !country_ie_integrity_changes(country_ie_checksum);
	/*
	 * We should not have let these through at this point, they
	 * should have been picked up earlier by the first alpha2 check
	 * on the device
	 */
	if (WARN_ON(!country_ie_integrity_changes(country_ie_checksum)))
		return true;
	return false;
}

/*
 * We hold wdev_lock() here so we cannot hold cfg80211_mutex() and
 * therefore cannot iterate over the rdev list here.
 */
void regulatory_hint_11d(struct wiphy *wiphy,
			 enum ieee80211_band band,
			 u8 *country_ie,
			 u8 country_ie_len)
{
	struct ieee80211_regdomain *rd = NULL;
	char alpha2[2];
	u32 checksum = 0;
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

	/*
	 * Pending country IE processing, this can happen after we
	 * call CRDA and wait for a response if a beacon was received before
	 * we were able to process the last regulatory_hint_11d() call
	 */
	if (country_ie_regdomain)
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

	rd = country_ie_2_rd(band, country_ie, country_ie_len, &checksum);
	if (!rd) {
		REG_DBG_PRINT("cfg80211: Ignoring bogus country IE\n");
		goto out;
	}

	/*
	 * This will not happen right now but we leave it here for the
	 * the future when we want to add suspend/resume support and having
	 * the user move to another country after doing so, or having the user
	 * move to another AP. Right now we just trust the first AP.
	 *
	 * If we hit this before we add this support we want to be informed of
	 * it as it would indicate a mistake in the current design
	 */
	if (WARN_ON(reg_same_country_ie_hint(wiphy, checksum)))
		goto free_rd_out;

	request = kzalloc(sizeof(struct regulatory_request), GFP_KERNEL);
	if (!request)
		goto free_rd_out;

	/*
	 * We keep this around for when CRDA comes back with a response so
	 * we can intersect with that
	 */
	country_ie_regdomain = rd;

	request->wiphy_idx = get_wiphy_idx(wiphy);
	request->alpha2[0] = rd->alpha2[0];
	request->alpha2[1] = rd->alpha2[1];
	request->initiator = NL80211_REGDOM_SET_BY_COUNTRY_IE;
	request->country_ie_checksum = checksum;
	request->country_ie_env = env;

	mutex_unlock(&reg_mutex);

	queue_regulatory_request(request);

	return;

free_rd_out:
	kfree(rd);
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
			REG_DBG_PRINT("cfg80211: Restoring regulatory settings "
			       "including user preference\n");
			user_alpha2[0] = '9';
			user_alpha2[1] = '7';

			/*
			 * If we're ignoring user settings, we still need to
			 * check the module parameter to ensure we put things
			 * back as they were for a full restore.
			 */
			if (!is_world_regdom(ieee80211_regdom)) {
				REG_DBG_PRINT("cfg80211: Keeping preference on "
				       "module parameter ieee80211_regdom: %c%c\n",
				       ieee80211_regdom[0],
				       ieee80211_regdom[1]);
				alpha2[0] = ieee80211_regdom[0];
				alpha2[1] = ieee80211_regdom[1];
			}
		} else {
			REG_DBG_PRINT("cfg80211: Restoring regulatory settings "
			       "while preserving user preference for: %c%c\n",
			       user_alpha2[0],
			       user_alpha2[1]);
			alpha2[0] = user_alpha2[0];
			alpha2[1] = user_alpha2[1];
		}
	} else if (!is_world_regdom(ieee80211_regdom)) {
		REG_DBG_PRINT("cfg80211: Keeping preference on "
		       "module parameter ieee80211_regdom: %c%c\n",
		       ieee80211_regdom[0],
		       ieee80211_regdom[1]);
		alpha2[0] = ieee80211_regdom[0];
		alpha2[1] = ieee80211_regdom[1];
	} else
		REG_DBG_PRINT("cfg80211: Restoring regulatory settings\n");
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
	struct reg_beacon *reg_beacon, *btmp;

	mutex_lock(&cfg80211_mutex);
	mutex_lock(&reg_mutex);

	reset_regdomains();
	restore_alpha2(alpha2, reset_user);

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

	mutex_unlock(&reg_mutex);
	mutex_unlock(&cfg80211_mutex);

	regulatory_hint_core(cfg80211_regdomain->alpha2);

	/*
	 * This restores the ieee80211_regdom module parameter
	 * preference or the last user requested regulatory
	 * settings, user regulatory settings takes precedence.
	 */
	if (is_an_alpha2(alpha2))
		regulatory_hint_user(user_alpha2);
}


void regulatory_hint_disconnect(void)
{
	REG_DBG_PRINT("cfg80211: All devices are disconnected, going to "
		      "restore regulatory settings\n");
	restore_regulatory_settings(false);
}

static bool freq_is_chan_12_13_14(u16 freq)
{
	if (freq == ieee80211_channel_to_frequency(12) ||
	    freq == ieee80211_channel_to_frequency(13) ||
	    freq == ieee80211_channel_to_frequency(14))
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

	REG_DBG_PRINT("cfg80211: Found new beacon on "
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

	printk(KERN_INFO "    (start_freq - end_freq @ bandwidth), "
		"(max_antenna_gain, max_eirp)\n");

	for (i = 0; i < rd->n_reg_rules; i++) {
		reg_rule = &rd->reg_rules[i];
		freq_range = &reg_rule->freq_range;
		power_rule = &reg_rule->power_rule;

		/*
		 * There may not be documentation for max antenna gain
		 * in certain regions
		 */
		if (power_rule->max_antenna_gain)
			printk(KERN_INFO "    (%d KHz - %d KHz @ %d KHz), "
				"(%d mBi, %d mBm)\n",
				freq_range->start_freq_khz,
				freq_range->end_freq_khz,
				freq_range->max_bandwidth_khz,
				power_rule->max_antenna_gain,
				power_rule->max_eirp);
		else
			printk(KERN_INFO "    (%d KHz - %d KHz @ %d KHz), "
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

		if (last_request->initiator ==
		    NL80211_REGDOM_SET_BY_COUNTRY_IE) {
			struct cfg80211_registered_device *rdev;
			rdev = cfg80211_rdev_by_wiphy_idx(
				last_request->wiphy_idx);
			if (rdev) {
				printk(KERN_INFO "cfg80211: Current regulatory "
					"domain updated by AP to: %c%c\n",
					rdev->country_ie_alpha2[0],
					rdev->country_ie_alpha2[1]);
			} else
				printk(KERN_INFO "cfg80211: Current regulatory "
					"domain intersected:\n");
		} else
			printk(KERN_INFO "cfg80211: Current regulatory "
				"domain intersected:\n");
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
		printk(KERN_ERR "cfg80211: Invalid "
			"regulatory domain detected:\n");
		print_regdomain_info(rd);
		return -EINVAL;
	}

	request_wiphy = wiphy_idx_to_wiphy(last_request->wiphy_idx);

	if (!last_request->intersect) {
		int r;

		if (last_request->initiator != NL80211_REGDOM_SET_BY_DRIVER) {
			reset_regdomains();
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

		reset_regdomains();
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

		reset_regdomains();
		cfg80211_regdomain = intersected_rd;

		return 0;
	}

	/*
	 * Country IE requests are handled a bit differently, we intersect
	 * the country IE rd with what CRDA believes that country should have
	 */

	/*
	 * Userspace could have sent two replies with only
	 * one kernel request. By the second reply we would have
	 * already processed and consumed the country_ie_regdomain.
	 */
	if (!country_ie_regdomain)
		return -EALREADY;
	BUG_ON(rd == country_ie_regdomain);

	/*
	 * Intersect what CRDA returned and our what we
	 * had built from the Country IE received
	 */

	intersected_rd = regdom_intersect(rd, country_ie_regdomain);

	reg_country_ie_process_debug(rd,
				     country_ie_regdomain,
				     intersected_rd);

	kfree(country_ie_regdomain);
	country_ie_regdomain = NULL;

	if (!intersected_rd)
		return -EINVAL;

	rdev = wiphy_to_dev(request_wiphy);

	rdev->country_ie_alpha2[0] = rd->alpha2[0];
	rdev->country_ie_alpha2[1] = rd->alpha2[1];
	rdev->env = last_request->country_ie_env;

	BUG_ON(intersected_rd == rd);

	kfree(rd);
	rd = NULL;

	reset_regdomains();
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

	mutex_unlock(&reg_mutex);

	return r;
}

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

int regulatory_init(void)
{
	int err = 0;

	reg_pdev = platform_device_register_simple("regulatory", 0, NULL, 0);
	if (IS_ERR(reg_pdev))
		return PTR_ERR(reg_pdev);

	spin_lock_init(&reg_requests_lock);
	spin_lock_init(&reg_pending_beacons_lock);

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
		printk(KERN_ERR "cfg80211: kobject_uevent_env() was unable "
			"to call CRDA during init");
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

void regulatory_exit(void)
{
	struct regulatory_request *reg_request, *tmp;
	struct reg_beacon *reg_beacon, *btmp;

	cancel_work_sync(&reg_work);

	mutex_lock(&cfg80211_mutex);
	mutex_lock(&reg_mutex);

	reset_regdomains();

	kfree(country_ie_regdomain);
	country_ie_regdomain = NULL;

	kfree(last_request);

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
