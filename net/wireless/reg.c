/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * This regulatory domain control implementation is highly incomplete, it
 * only exists for the purpose of not regressing mac80211.
 *
 * For now, drivers can restrict the set of allowed channels by either
 * not registering those channels or setting the IEEE80211_CHAN_DISABLED
 * flag; that flag will only be *set* by this code, never *cleared.
 *
 * The usual implementation is for a driver to read a device EEPROM to
 * determine which regulatory domain it should be operating under, then
 * looking up the allowable channels in a driver-local table and finally
 * registering those channels in the wiphy structure.
 *
 * Alternatively, drivers that trust the regulatory domain control here
 * will register a complete set of capabilities and the control code
 * will restrict the set by setting the IEEE80211_CHAN_* flags.
 */
#include <linux/kernel.h>
#include <net/wireless.h>
#include "core.h"

static char *ieee80211_regdom = "US";
module_param(ieee80211_regdom, charp, 0444);
MODULE_PARM_DESC(ieee80211_regdom, "IEEE 802.11 regulatory domain code");

struct ieee80211_channel_range {
	short start_freq;
	short end_freq;
	int max_power;
	int max_antenna_gain;
	u32 flags;
};

struct ieee80211_regdomain {
	const char *code;
	const struct ieee80211_channel_range *ranges;
	int n_ranges;
};

#define RANGE_PWR(_start, _end, _pwr, _ag, _flags)	\
	{ _start, _end, _pwr, _ag, _flags }


/*
 * Ideally, in the future, these definitions will be loaded from a
 * userspace table via some daemon.
 */
static const struct ieee80211_channel_range ieee80211_US_channels[] = {
	/* IEEE 802.11b/g, channels 1..11 */
	RANGE_PWR(2412, 2462, 27, 6, 0),
	/* IEEE 802.11a, channel 36*/
	RANGE_PWR(5180, 5180, 23, 6, 0),
	/* IEEE 802.11a, channel 40*/
	RANGE_PWR(5200, 5200, 23, 6, 0),
	/* IEEE 802.11a, channel 44*/
	RANGE_PWR(5220, 5220, 23, 6, 0),
	/* IEEE 802.11a, channels 48..64 */
	RANGE_PWR(5240, 5320, 23, 6, 0),
	/* IEEE 802.11a, channels 149..165, outdoor */
	RANGE_PWR(5745, 5825, 30, 6, 0),
};

static const struct ieee80211_channel_range ieee80211_JP_channels[] = {
	/* IEEE 802.11b/g, channels 1..14 */
	RANGE_PWR(2412, 2484, 20, 6, 0),
	/* IEEE 802.11a, channels 34..48 */
	RANGE_PWR(5170, 5240, 20, 6, IEEE80211_CHAN_PASSIVE_SCAN),
	/* IEEE 802.11a, channels 52..64 */
	RANGE_PWR(5260, 5320, 20, 6, IEEE80211_CHAN_NO_IBSS |
				     IEEE80211_CHAN_RADAR),
};

#define REGDOM(_code)							\
	{								\
		.code = __stringify(_code),				\
		.ranges = ieee80211_ ##_code## _channels,		\
		.n_ranges = ARRAY_SIZE(ieee80211_ ##_code## _channels),	\
	}

static const struct ieee80211_regdomain ieee80211_regdoms[] = {
	REGDOM(US),
	REGDOM(JP),
};


static const struct ieee80211_regdomain *get_regdom(void)
{
	static const struct ieee80211_channel_range
	ieee80211_world_channels[] = {
		/* IEEE 802.11b/g, channels 1..11 */
		RANGE_PWR(2412, 2462, 27, 6, 0),
	};
	static const struct ieee80211_regdomain regdom_world = REGDOM(world);
	int i;

	for (i = 0; i < ARRAY_SIZE(ieee80211_regdoms); i++)
		if (strcmp(ieee80211_regdom, ieee80211_regdoms[i].code) == 0)
			return &ieee80211_regdoms[i];

	return &regdom_world;
}


static void handle_channel(struct ieee80211_channel *chan,
			   const struct ieee80211_regdomain *rd)
{
	int i;
	u32 flags = chan->orig_flags;
	const struct ieee80211_channel_range *rg = NULL;

	for (i = 0; i < rd->n_ranges; i++) {
		if (rd->ranges[i].start_freq <= chan->center_freq &&
		    chan->center_freq <= rd->ranges[i].end_freq) {
			rg = &rd->ranges[i];
			break;
		}
	}

	if (!rg) {
		/* not found */
		flags |= IEEE80211_CHAN_DISABLED;
		chan->flags = flags;
		return;
	}

	chan->flags = flags;
	chan->max_antenna_gain = min(chan->orig_mag,
					 rg->max_antenna_gain);
	if (chan->orig_mpwr)
		chan->max_power = min(chan->orig_mpwr, rg->max_power);
	else
		chan->max_power = rg->max_power;
}

static void handle_band(struct ieee80211_supported_band *sband,
			const struct ieee80211_regdomain *rd)
{
	int i;

	for (i = 0; i < sband->n_channels; i++)
		handle_channel(&sband->channels[i], rd);
}

void wiphy_update_regulatory(struct wiphy *wiphy)
{
	enum ieee80211_band band;
	const struct ieee80211_regdomain *rd = get_regdom();

	for (band = 0; band < IEEE80211_NUM_BANDS; band++)
		if (wiphy->bands[band])
			handle_band(wiphy->bands[band], rd);
}
