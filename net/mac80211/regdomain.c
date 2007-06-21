/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * This regulatory domain control implementation is known to be incomplete
 * and confusing. mac80211 regulatory domain control will be significantly
 * reworked in the not-too-distant future.
 *
 * For now, drivers wishing to control which channels are and aren't available
 * are advised as follows:
 *  - set the IEEE80211_HW_DEFAULT_REG_DOMAIN_CONFIGURED flag
 *  - continue to include *ALL* possible channels in the modes registered
 *    through ieee80211_register_hwmode()
 *  - for each allowable ieee80211_channel structure registered in the above
 *    call, set the flag member to some meaningful value such as
 *    IEEE80211_CHAN_W_SCAN | IEEE80211_CHAN_W_ACTIVE_SCAN |
 *    IEEE80211_CHAN_W_IBSS.
 *  - leave flag as 0 for non-allowable channels
 *
 * The usual implementation is for a driver to read a device EEPROM to
 * determine which regulatory domain it should be operating under, then
 * looking up the allowable channels in a driver-local table, then performing
 * the above.
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"

static int ieee80211_regdom = 0x10; /* FCC */
module_param(ieee80211_regdom, int, 0444);
MODULE_PARM_DESC(ieee80211_regdom, "IEEE 802.11 regulatory domain; 64=MKK");

/*
 * If firmware is upgraded by the vendor, additional channels can be used based
 * on the new Japanese regulatory rules. This is indicated by setting
 * ieee80211_japan_5ghz module parameter to one when loading the 80211 kernel
 * module.
 */
static int ieee80211_japan_5ghz /* = 0 */;
module_param(ieee80211_japan_5ghz, int, 0444);
MODULE_PARM_DESC(ieee80211_japan_5ghz, "Vendor-updated firmware for 5 GHz");


struct ieee80211_channel_range {
	short start_freq;
	short end_freq;
	unsigned char power_level;
	unsigned char antenna_max;
};

static const struct ieee80211_channel_range ieee80211_fcc_channels[] = {
	{ 2412, 2462, 27, 6 } /* IEEE 802.11b/g, channels 1..11 */,
	{ 5180, 5240, 17, 6 } /* IEEE 802.11a, channels 36..48 */,
	{ 5260, 5320, 23, 6 } /* IEEE 802.11a, channels 52..64 */,
	{ 5745, 5825, 30, 6 } /* IEEE 802.11a, channels 149..165, outdoor */,
	{ 0 }
};

static const struct ieee80211_channel_range ieee80211_mkk_channels[] = {
	{ 2412, 2472, 20, 6 } /* IEEE 802.11b/g, channels 1..13 */,
	{ 5170, 5240, 20, 6 } /* IEEE 802.11a, channels 34..48 */,
	{ 5260, 5320, 20, 6 } /* IEEE 802.11a, channels 52..64 */,
	{ 0 }
};


static const struct ieee80211_channel_range *channel_range =
	ieee80211_fcc_channels;


static void ieee80211_unmask_channel(int mode, struct ieee80211_channel *chan)
{
	int i;

	chan->flag = 0;

	if (ieee80211_regdom == 64 &&
	    (mode == MODE_ATHEROS_TURBO || mode == MODE_ATHEROS_TURBOG)) {
		/* Do not allow Turbo modes in Japan. */
		return;
	}

	for (i = 0; channel_range[i].start_freq; i++) {
		const struct ieee80211_channel_range *r = &channel_range[i];
		if (r->start_freq <= chan->freq && r->end_freq >= chan->freq) {
			if (ieee80211_regdom == 64 && !ieee80211_japan_5ghz &&
			    chan->freq >= 5260 && chan->freq <= 5320) {
				/*
				 * Skip new channels in Japan since the
				 * firmware was not marked having been upgraded
				 * by the vendor.
				 */
				continue;
			}

			if (ieee80211_regdom == 0x10 &&
			    (chan->freq == 5190 || chan->freq == 5210 ||
			     chan->freq == 5230)) {
				    /* Skip MKK channels when in FCC domain. */
				    continue;
			}

			chan->flag |= IEEE80211_CHAN_W_SCAN |
				IEEE80211_CHAN_W_ACTIVE_SCAN |
				IEEE80211_CHAN_W_IBSS;
			chan->power_level = r->power_level;
			chan->antenna_max = r->antenna_max;

			if (ieee80211_regdom == 64 &&
			    (chan->freq == 5170 || chan->freq == 5190 ||
			     chan->freq == 5210 || chan->freq == 5230)) {
				/*
				 * New regulatory rules in Japan have backwards
				 * compatibility with old channels in 5.15-5.25
				 * GHz band, but the station is not allowed to
				 * use active scan on these old channels.
				 */
				chan->flag &= ~IEEE80211_CHAN_W_ACTIVE_SCAN;
			}

			if (ieee80211_regdom == 64 &&
			    (chan->freq == 5260 || chan->freq == 5280 ||
			     chan->freq == 5300 || chan->freq == 5320)) {
				/*
				 * IBSS is not allowed on 5.25-5.35 GHz band
				 * due to radar detection requirements.
				 */
				chan->flag &= ~IEEE80211_CHAN_W_IBSS;
			}

			break;
		}
	}
}


void ieee80211_set_default_regdomain(struct ieee80211_hw_mode *mode)
{
	int c;
	for (c = 0; c < mode->num_channels; c++)
		ieee80211_unmask_channel(mode->mode, &mode->channels[c]);
}


void ieee80211_regdomain_init(void)
{
	if (ieee80211_regdom == 0x40)
		channel_range = ieee80211_mkk_channels;
}

