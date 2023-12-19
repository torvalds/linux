// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains helper code to handle channel
 * settings and keeping track of what is possible at
 * any point in time.
 *
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright 2018-2023	Intel Corporation
 */

#include <linux/export.h>
#include <linux/bitfield.h>
#include <net/cfg80211.h>
#include "core.h"
#include "rdev-ops.h"

static bool cfg80211_valid_60g_freq(u32 freq)
{
	return freq >= 58320 && freq <= 70200;
}

void cfg80211_chandef_create(struct cfg80211_chan_def *chandef,
			     struct ieee80211_channel *chan,
			     enum nl80211_channel_type chan_type)
{
	if (WARN_ON(!chan))
		return;

	chandef->chan = chan;
	chandef->freq1_offset = chan->freq_offset;
	chandef->center_freq2 = 0;
	chandef->edmg.bw_config = 0;
	chandef->edmg.channels = 0;

	switch (chan_type) {
	case NL80211_CHAN_NO_HT:
		chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
		chandef->center_freq1 = chan->center_freq;
		break;
	case NL80211_CHAN_HT20:
		chandef->width = NL80211_CHAN_WIDTH_20;
		chandef->center_freq1 = chan->center_freq;
		break;
	case NL80211_CHAN_HT40PLUS:
		chandef->width = NL80211_CHAN_WIDTH_40;
		chandef->center_freq1 = chan->center_freq + 10;
		break;
	case NL80211_CHAN_HT40MINUS:
		chandef->width = NL80211_CHAN_WIDTH_40;
		chandef->center_freq1 = chan->center_freq - 10;
		break;
	default:
		WARN_ON(1);
	}
}
EXPORT_SYMBOL(cfg80211_chandef_create);

static bool cfg80211_edmg_chandef_valid(const struct cfg80211_chan_def *chandef)
{
	int max_contiguous = 0;
	int num_of_enabled = 0;
	int contiguous = 0;
	int i;

	if (!chandef->edmg.channels || !chandef->edmg.bw_config)
		return false;

	if (!cfg80211_valid_60g_freq(chandef->chan->center_freq))
		return false;

	for (i = 0; i < 6; i++) {
		if (chandef->edmg.channels & BIT(i)) {
			contiguous++;
			num_of_enabled++;
		} else {
			contiguous = 0;
		}

		max_contiguous = max(contiguous, max_contiguous);
	}
	/* basic verification of edmg configuration according to
	 * IEEE P802.11ay/D4.0 section 9.4.2.251
	 */
	/* check bw_config against contiguous edmg channels */
	switch (chandef->edmg.bw_config) {
	case IEEE80211_EDMG_BW_CONFIG_4:
	case IEEE80211_EDMG_BW_CONFIG_8:
	case IEEE80211_EDMG_BW_CONFIG_12:
		if (max_contiguous < 1)
			return false;
		break;
	case IEEE80211_EDMG_BW_CONFIG_5:
	case IEEE80211_EDMG_BW_CONFIG_9:
	case IEEE80211_EDMG_BW_CONFIG_13:
		if (max_contiguous < 2)
			return false;
		break;
	case IEEE80211_EDMG_BW_CONFIG_6:
	case IEEE80211_EDMG_BW_CONFIG_10:
	case IEEE80211_EDMG_BW_CONFIG_14:
		if (max_contiguous < 3)
			return false;
		break;
	case IEEE80211_EDMG_BW_CONFIG_7:
	case IEEE80211_EDMG_BW_CONFIG_11:
	case IEEE80211_EDMG_BW_CONFIG_15:
		if (max_contiguous < 4)
			return false;
		break;

	default:
		return false;
	}

	/* check bw_config against aggregated (non contiguous) edmg channels */
	switch (chandef->edmg.bw_config) {
	case IEEE80211_EDMG_BW_CONFIG_4:
	case IEEE80211_EDMG_BW_CONFIG_5:
	case IEEE80211_EDMG_BW_CONFIG_6:
	case IEEE80211_EDMG_BW_CONFIG_7:
		break;
	case IEEE80211_EDMG_BW_CONFIG_8:
	case IEEE80211_EDMG_BW_CONFIG_9:
	case IEEE80211_EDMG_BW_CONFIG_10:
	case IEEE80211_EDMG_BW_CONFIG_11:
		if (num_of_enabled < 2)
			return false;
		break;
	case IEEE80211_EDMG_BW_CONFIG_12:
	case IEEE80211_EDMG_BW_CONFIG_13:
	case IEEE80211_EDMG_BW_CONFIG_14:
	case IEEE80211_EDMG_BW_CONFIG_15:
		if (num_of_enabled < 4 || max_contiguous < 2)
			return false;
		break;
	default:
		return false;
	}

	return true;
}

int nl80211_chan_width_to_mhz(enum nl80211_chan_width chan_width)
{
	int mhz;

	switch (chan_width) {
	case NL80211_CHAN_WIDTH_1:
		mhz = 1;
		break;
	case NL80211_CHAN_WIDTH_2:
		mhz = 2;
		break;
	case NL80211_CHAN_WIDTH_4:
		mhz = 4;
		break;
	case NL80211_CHAN_WIDTH_8:
		mhz = 8;
		break;
	case NL80211_CHAN_WIDTH_16:
		mhz = 16;
		break;
	case NL80211_CHAN_WIDTH_5:
		mhz = 5;
		break;
	case NL80211_CHAN_WIDTH_10:
		mhz = 10;
		break;
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_20_NOHT:
		mhz = 20;
		break;
	case NL80211_CHAN_WIDTH_40:
		mhz = 40;
		break;
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_80:
		mhz = 80;
		break;
	case NL80211_CHAN_WIDTH_160:
		mhz = 160;
		break;
	case NL80211_CHAN_WIDTH_320:
		mhz = 320;
		break;
	default:
		WARN_ON_ONCE(1);
		return -1;
	}
	return mhz;
}
EXPORT_SYMBOL(nl80211_chan_width_to_mhz);

static int cfg80211_chandef_get_width(const struct cfg80211_chan_def *c)
{
	return nl80211_chan_width_to_mhz(c->width);
}

bool cfg80211_chandef_valid(const struct cfg80211_chan_def *chandef)
{
	u32 control_freq, oper_freq;
	int oper_width, control_width;

	if (!chandef->chan)
		return false;

	if (chandef->freq1_offset >= 1000)
		return false;

	control_freq = chandef->chan->center_freq;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_20_NOHT:
		if (ieee80211_chandef_to_khz(chandef) !=
		    ieee80211_channel_to_khz(chandef->chan))
			return false;
		if (chandef->center_freq2)
			return false;
		break;
	case NL80211_CHAN_WIDTH_1:
	case NL80211_CHAN_WIDTH_2:
	case NL80211_CHAN_WIDTH_4:
	case NL80211_CHAN_WIDTH_8:
	case NL80211_CHAN_WIDTH_16:
		if (chandef->chan->band != NL80211_BAND_S1GHZ)
			return false;

		control_freq = ieee80211_channel_to_khz(chandef->chan);
		oper_freq = ieee80211_chandef_to_khz(chandef);
		control_width = nl80211_chan_width_to_mhz(
					ieee80211_s1g_channel_width(
								chandef->chan));
		oper_width = cfg80211_chandef_get_width(chandef);

		if (oper_width < 0 || control_width < 0)
			return false;
		if (chandef->center_freq2)
			return false;

		if (control_freq + MHZ_TO_KHZ(control_width) / 2 >
		    oper_freq + MHZ_TO_KHZ(oper_width) / 2)
			return false;

		if (control_freq - MHZ_TO_KHZ(control_width) / 2 <
		    oper_freq - MHZ_TO_KHZ(oper_width) / 2)
			return false;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		if (!chandef->center_freq2)
			return false;
		/* adjacent is not allowed -- that's a 160 MHz channel */
		if (chandef->center_freq1 - chandef->center_freq2 == 80 ||
		    chandef->center_freq2 - chandef->center_freq1 == 80)
			return false;
		break;
	default:
		if (chandef->center_freq2)
			return false;
		break;
	}

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_1:
	case NL80211_CHAN_WIDTH_2:
	case NL80211_CHAN_WIDTH_4:
	case NL80211_CHAN_WIDTH_8:
	case NL80211_CHAN_WIDTH_16:
		/* all checked above */
		break;
	case NL80211_CHAN_WIDTH_320:
		if (chandef->center_freq1 == control_freq + 150 ||
		    chandef->center_freq1 == control_freq + 130 ||
		    chandef->center_freq1 == control_freq + 110 ||
		    chandef->center_freq1 == control_freq + 90 ||
		    chandef->center_freq1 == control_freq - 90 ||
		    chandef->center_freq1 == control_freq - 110 ||
		    chandef->center_freq1 == control_freq - 130 ||
		    chandef->center_freq1 == control_freq - 150)
			break;
		fallthrough;
	case NL80211_CHAN_WIDTH_160:
		if (chandef->center_freq1 == control_freq + 70 ||
		    chandef->center_freq1 == control_freq + 50 ||
		    chandef->center_freq1 == control_freq - 50 ||
		    chandef->center_freq1 == control_freq - 70)
			break;
		fallthrough;
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_80:
		if (chandef->center_freq1 == control_freq + 30 ||
		    chandef->center_freq1 == control_freq - 30)
			break;
		fallthrough;
	case NL80211_CHAN_WIDTH_40:
		if (chandef->center_freq1 == control_freq + 10 ||
		    chandef->center_freq1 == control_freq - 10)
			break;
		fallthrough;
	default:
		return false;
	}

	/* channel 14 is only for IEEE 802.11b */
	if (chandef->center_freq1 == 2484 &&
	    chandef->width != NL80211_CHAN_WIDTH_20_NOHT)
		return false;

	if (cfg80211_chandef_is_edmg(chandef) &&
	    !cfg80211_edmg_chandef_valid(chandef))
		return false;

	return true;
}
EXPORT_SYMBOL(cfg80211_chandef_valid);

static void chandef_primary_freqs(const struct cfg80211_chan_def *c,
				  u32 *pri40, u32 *pri80, u32 *pri160)
{
	int tmp;

	switch (c->width) {
	case NL80211_CHAN_WIDTH_40:
		*pri40 = c->center_freq1;
		*pri80 = 0;
		*pri160 = 0;
		break;
	case NL80211_CHAN_WIDTH_80:
	case NL80211_CHAN_WIDTH_80P80:
		*pri160 = 0;
		*pri80 = c->center_freq1;
		/* n_P20 */
		tmp = (30 + c->chan->center_freq - c->center_freq1)/20;
		/* n_P40 */
		tmp /= 2;
		/* freq_P40 */
		*pri40 = c->center_freq1 - 20 + 40 * tmp;
		break;
	case NL80211_CHAN_WIDTH_160:
		*pri160 = c->center_freq1;
		/* n_P20 */
		tmp = (70 + c->chan->center_freq - c->center_freq1)/20;
		/* n_P40 */
		tmp /= 2;
		/* freq_P40 */
		*pri40 = c->center_freq1 - 60 + 40 * tmp;
		/* n_P80 */
		tmp /= 2;
		*pri80 = c->center_freq1 - 40 + 80 * tmp;
		break;
	case NL80211_CHAN_WIDTH_320:
		/* n_P20 */
		tmp = (150 + c->chan->center_freq - c->center_freq1) / 20;
		/* n_P40 */
		tmp /= 2;
		/* freq_P40 */
		*pri40 = c->center_freq1 - 140 + 40 * tmp;
		/* n_P80 */
		tmp /= 2;
		*pri80 = c->center_freq1 - 120 + 80 * tmp;
		/* n_P160 */
		tmp /= 2;
		*pri160 = c->center_freq1 - 80 + 160 * tmp;
		break;
	default:
		WARN_ON_ONCE(1);
	}
}

const struct cfg80211_chan_def *
cfg80211_chandef_compatible(const struct cfg80211_chan_def *c1,
			    const struct cfg80211_chan_def *c2)
{
	u32 c1_pri40, c1_pri80, c2_pri40, c2_pri80, c1_pri160, c2_pri160;

	/* If they are identical, return */
	if (cfg80211_chandef_identical(c1, c2))
		return c1;

	/* otherwise, must have same control channel */
	if (c1->chan != c2->chan)
		return NULL;

	/*
	 * If they have the same width, but aren't identical,
	 * then they can't be compatible.
	 */
	if (c1->width == c2->width)
		return NULL;

	/*
	 * can't be compatible if one of them is 5 or 10 MHz,
	 * but they don't have the same width.
	 */
	if (c1->width == NL80211_CHAN_WIDTH_5 ||
	    c1->width == NL80211_CHAN_WIDTH_10 ||
	    c2->width == NL80211_CHAN_WIDTH_5 ||
	    c2->width == NL80211_CHAN_WIDTH_10)
		return NULL;

	if (c1->width == NL80211_CHAN_WIDTH_20_NOHT ||
	    c1->width == NL80211_CHAN_WIDTH_20)
		return c2;

	if (c2->width == NL80211_CHAN_WIDTH_20_NOHT ||
	    c2->width == NL80211_CHAN_WIDTH_20)
		return c1;

	chandef_primary_freqs(c1, &c1_pri40, &c1_pri80, &c1_pri160);
	chandef_primary_freqs(c2, &c2_pri40, &c2_pri80, &c2_pri160);

	if (c1_pri40 != c2_pri40)
		return NULL;

	if (c1->width == NL80211_CHAN_WIDTH_40)
		return c2;

	if (c2->width == NL80211_CHAN_WIDTH_40)
		return c1;

	if (c1_pri80 != c2_pri80)
		return NULL;

	if (c1->width == NL80211_CHAN_WIDTH_80 &&
	    c2->width > NL80211_CHAN_WIDTH_80)
		return c2;

	if (c2->width == NL80211_CHAN_WIDTH_80 &&
	    c1->width > NL80211_CHAN_WIDTH_80)
		return c1;

	WARN_ON(!c1_pri160 && !c2_pri160);
	if (c1_pri160 && c2_pri160 && c1_pri160 != c2_pri160)
		return NULL;

	if (c1->width > c2->width)
		return c1;
	return c2;
}
EXPORT_SYMBOL(cfg80211_chandef_compatible);

static void cfg80211_set_chans_dfs_state(struct wiphy *wiphy, u32 center_freq,
					 u32 bandwidth,
					 enum nl80211_dfs_state dfs_state)
{
	struct ieee80211_channel *c;
	u32 freq;

	for (freq = center_freq - bandwidth/2 + 10;
	     freq <= center_freq + bandwidth/2 - 10;
	     freq += 20) {
		c = ieee80211_get_channel(wiphy, freq);
		if (!c || !(c->flags & IEEE80211_CHAN_RADAR))
			continue;

		c->dfs_state = dfs_state;
		c->dfs_state_entered = jiffies;
	}
}

void cfg80211_set_dfs_state(struct wiphy *wiphy,
			    const struct cfg80211_chan_def *chandef,
			    enum nl80211_dfs_state dfs_state)
{
	int width;

	if (WARN_ON(!cfg80211_chandef_valid(chandef)))
		return;

	width = cfg80211_chandef_get_width(chandef);
	if (width < 0)
		return;

	cfg80211_set_chans_dfs_state(wiphy, chandef->center_freq1,
				     width, dfs_state);

	if (!chandef->center_freq2)
		return;
	cfg80211_set_chans_dfs_state(wiphy, chandef->center_freq2,
				     width, dfs_state);
}

static u32 cfg80211_get_start_freq(u32 center_freq,
				   u32 bandwidth)
{
	u32 start_freq;

	bandwidth = MHZ_TO_KHZ(bandwidth);
	if (bandwidth <= MHZ_TO_KHZ(20))
		start_freq = center_freq;
	else
		start_freq = center_freq - bandwidth / 2 + MHZ_TO_KHZ(10);

	return start_freq;
}

static u32 cfg80211_get_end_freq(u32 center_freq,
				 u32 bandwidth)
{
	u32 end_freq;

	bandwidth = MHZ_TO_KHZ(bandwidth);
	if (bandwidth <= MHZ_TO_KHZ(20))
		end_freq = center_freq;
	else
		end_freq = center_freq + bandwidth / 2 - MHZ_TO_KHZ(10);

	return end_freq;
}

static int cfg80211_get_chans_dfs_required(struct wiphy *wiphy,
					    u32 center_freq,
					    u32 bandwidth)
{
	struct ieee80211_channel *c;
	u32 freq, start_freq, end_freq;

	start_freq = cfg80211_get_start_freq(center_freq, bandwidth);
	end_freq = cfg80211_get_end_freq(center_freq, bandwidth);

	for (freq = start_freq; freq <= end_freq; freq += MHZ_TO_KHZ(20)) {
		c = ieee80211_get_channel_khz(wiphy, freq);
		if (!c)
			return -EINVAL;

		if (c->flags & IEEE80211_CHAN_RADAR)
			return 1;
	}
	return 0;
}


int cfg80211_chandef_dfs_required(struct wiphy *wiphy,
				  const struct cfg80211_chan_def *chandef,
				  enum nl80211_iftype iftype)
{
	int width;
	int ret;

	if (WARN_ON(!cfg80211_chandef_valid(chandef)))
		return -EINVAL;

	switch (iftype) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_MESH_POINT:
		width = cfg80211_chandef_get_width(chandef);
		if (width < 0)
			return -EINVAL;

		ret = cfg80211_get_chans_dfs_required(wiphy,
					ieee80211_chandef_to_khz(chandef),
					width);
		if (ret < 0)
			return ret;
		else if (ret > 0)
			return BIT(chandef->width);

		if (!chandef->center_freq2)
			return 0;

		ret = cfg80211_get_chans_dfs_required(wiphy,
					MHZ_TO_KHZ(chandef->center_freq2),
					width);
		if (ret < 0)
			return ret;
		else if (ret > 0)
			return BIT(chandef->width);

		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_OCB:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_DEVICE:
	case NL80211_IFTYPE_NAN:
		break;
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_UNSPECIFIED:
	case NUM_NL80211_IFTYPES:
		WARN_ON(1);
	}

	return 0;
}
EXPORT_SYMBOL(cfg80211_chandef_dfs_required);

static int cfg80211_get_chans_dfs_usable(struct wiphy *wiphy,
					 u32 center_freq,
					 u32 bandwidth)
{
	struct ieee80211_channel *c;
	u32 freq, start_freq, end_freq;
	int count = 0;

	start_freq = cfg80211_get_start_freq(center_freq, bandwidth);
	end_freq = cfg80211_get_end_freq(center_freq, bandwidth);

	/*
	 * Check entire range of channels for the bandwidth.
	 * Check all channels are DFS channels (DFS_USABLE or
	 * DFS_AVAILABLE). Return number of usable channels
	 * (require CAC). Allow DFS and non-DFS channel mix.
	 */
	for (freq = start_freq; freq <= end_freq; freq += MHZ_TO_KHZ(20)) {
		c = ieee80211_get_channel_khz(wiphy, freq);
		if (!c)
			return -EINVAL;

		if (c->flags & IEEE80211_CHAN_DISABLED)
			return -EINVAL;

		if (c->flags & IEEE80211_CHAN_RADAR) {
			if (c->dfs_state == NL80211_DFS_UNAVAILABLE)
				return -EINVAL;

			if (c->dfs_state == NL80211_DFS_USABLE)
				count++;
		}
	}

	return count;
}

bool cfg80211_chandef_dfs_usable(struct wiphy *wiphy,
				 const struct cfg80211_chan_def *chandef)
{
	int width;
	int r1, r2 = 0;

	if (WARN_ON(!cfg80211_chandef_valid(chandef)))
		return false;

	width = cfg80211_chandef_get_width(chandef);
	if (width < 0)
		return false;

	r1 = cfg80211_get_chans_dfs_usable(wiphy,
					   MHZ_TO_KHZ(chandef->center_freq1),
					   width);

	if (r1 < 0)
		return false;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_80P80:
		WARN_ON(!chandef->center_freq2);
		r2 = cfg80211_get_chans_dfs_usable(wiphy,
					MHZ_TO_KHZ(chandef->center_freq2),
					width);
		if (r2 < 0)
			return false;
		break;
	default:
		WARN_ON(chandef->center_freq2);
		break;
	}

	return (r1 + r2 > 0);
}
EXPORT_SYMBOL(cfg80211_chandef_dfs_usable);

/*
 * Checks if center frequency of chan falls with in the bandwidth
 * range of chandef.
 */
bool cfg80211_is_sub_chan(struct cfg80211_chan_def *chandef,
			  struct ieee80211_channel *chan,
			  bool primary_only)
{
	int width;
	u32 freq;

	if (!chandef->chan)
		return false;

	if (chandef->chan->center_freq == chan->center_freq)
		return true;

	if (primary_only)
		return false;

	width = cfg80211_chandef_get_width(chandef);
	if (width <= 20)
		return false;

	for (freq = chandef->center_freq1 - width / 2 + 10;
	     freq <= chandef->center_freq1 + width / 2 - 10; freq += 20) {
		if (chan->center_freq == freq)
			return true;
	}

	if (!chandef->center_freq2)
		return false;

	for (freq = chandef->center_freq2 - width / 2 + 10;
	     freq <= chandef->center_freq2 + width / 2 - 10; freq += 20) {
		if (chan->center_freq == freq)
			return true;
	}

	return false;
}

bool cfg80211_beaconing_iface_active(struct wireless_dev *wdev)
{
	unsigned int link;

	lockdep_assert_wiphy(wdev->wiphy);

	switch (wdev->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		for_each_valid_link(wdev, link) {
			if (wdev->links[link].ap.beacon_interval)
				return true;
		}
		break;
	case NL80211_IFTYPE_ADHOC:
		if (wdev->u.ibss.ssid_len)
			return true;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		if (wdev->u.mesh.id_len)
			return true;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_OCB:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_DEVICE:
	/* Can NAN type be considered as beaconing interface? */
	case NL80211_IFTYPE_NAN:
		break;
	case NL80211_IFTYPE_UNSPECIFIED:
	case NL80211_IFTYPE_WDS:
	case NUM_NL80211_IFTYPES:
		WARN_ON(1);
	}

	return false;
}

bool cfg80211_wdev_on_sub_chan(struct wireless_dev *wdev,
			       struct ieee80211_channel *chan,
			       bool primary_only)
{
	unsigned int link;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		for_each_valid_link(wdev, link) {
			if (cfg80211_is_sub_chan(&wdev->links[link].ap.chandef,
						 chan, primary_only))
				return true;
		}
		break;
	case NL80211_IFTYPE_ADHOC:
		return cfg80211_is_sub_chan(&wdev->u.ibss.chandef, chan,
					    primary_only);
	case NL80211_IFTYPE_MESH_POINT:
		return cfg80211_is_sub_chan(&wdev->u.mesh.chandef, chan,
					    primary_only);
	default:
		break;
	}

	return false;
}

static bool cfg80211_is_wiphy_oper_chan(struct wiphy *wiphy,
					struct ieee80211_channel *chan)
{
	struct wireless_dev *wdev;

	lockdep_assert_wiphy(wiphy);

	list_for_each_entry(wdev, &wiphy->wdev_list, list) {
		if (!cfg80211_beaconing_iface_active(wdev))
			continue;

		if (cfg80211_wdev_on_sub_chan(wdev, chan, false))
			return true;
	}

	return false;
}

static bool
cfg80211_offchan_chain_is_active(struct cfg80211_registered_device *rdev,
				 struct ieee80211_channel *channel)
{
	if (!rdev->background_radar_wdev)
		return false;

	if (!cfg80211_chandef_valid(&rdev->background_radar_chandef))
		return false;

	return cfg80211_is_sub_chan(&rdev->background_radar_chandef, channel,
				    false);
}

bool cfg80211_any_wiphy_oper_chan(struct wiphy *wiphy,
				  struct ieee80211_channel *chan)
{
	struct cfg80211_registered_device *rdev;

	ASSERT_RTNL();

	if (!(chan->flags & IEEE80211_CHAN_RADAR))
		return false;

	for_each_rdev(rdev) {
		bool found;

		if (!reg_dfs_domain_same(wiphy, &rdev->wiphy))
			continue;

		wiphy_lock(&rdev->wiphy);
		found = cfg80211_is_wiphy_oper_chan(&rdev->wiphy, chan) ||
			cfg80211_offchan_chain_is_active(rdev, chan);
		wiphy_unlock(&rdev->wiphy);

		if (found)
			return true;
	}

	return false;
}

static bool cfg80211_get_chans_dfs_available(struct wiphy *wiphy,
					     u32 center_freq,
					     u32 bandwidth)
{
	struct ieee80211_channel *c;
	u32 freq, start_freq, end_freq;
	bool dfs_offload;

	dfs_offload = wiphy_ext_feature_isset(wiphy,
					      NL80211_EXT_FEATURE_DFS_OFFLOAD);

	start_freq = cfg80211_get_start_freq(center_freq, bandwidth);
	end_freq = cfg80211_get_end_freq(center_freq, bandwidth);

	/*
	 * Check entire range of channels for the bandwidth.
	 * If any channel in between is disabled or has not
	 * had gone through CAC return false
	 */
	for (freq = start_freq; freq <= end_freq; freq += MHZ_TO_KHZ(20)) {
		c = ieee80211_get_channel_khz(wiphy, freq);
		if (!c)
			return false;

		if (c->flags & IEEE80211_CHAN_DISABLED)
			return false;

		if ((c->flags & IEEE80211_CHAN_RADAR) &&
		    (c->dfs_state != NL80211_DFS_AVAILABLE) &&
		    !(c->dfs_state == NL80211_DFS_USABLE && dfs_offload))
			return false;
	}

	return true;
}

static bool cfg80211_chandef_dfs_available(struct wiphy *wiphy,
				const struct cfg80211_chan_def *chandef)
{
	int width;
	int r;

	if (WARN_ON(!cfg80211_chandef_valid(chandef)))
		return false;

	width = cfg80211_chandef_get_width(chandef);
	if (width < 0)
		return false;

	r = cfg80211_get_chans_dfs_available(wiphy,
					     MHZ_TO_KHZ(chandef->center_freq1),
					     width);

	/* If any of channels unavailable for cf1 just return */
	if (!r)
		return r;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_80P80:
		WARN_ON(!chandef->center_freq2);
		r = cfg80211_get_chans_dfs_available(wiphy,
					MHZ_TO_KHZ(chandef->center_freq2),
					width);
		break;
	default:
		WARN_ON(chandef->center_freq2);
		break;
	}

	return r;
}

static unsigned int cfg80211_get_chans_dfs_cac_time(struct wiphy *wiphy,
						    u32 center_freq,
						    u32 bandwidth)
{
	struct ieee80211_channel *c;
	u32 start_freq, end_freq, freq;
	unsigned int dfs_cac_ms = 0;

	start_freq = cfg80211_get_start_freq(center_freq, bandwidth);
	end_freq = cfg80211_get_end_freq(center_freq, bandwidth);

	for (freq = start_freq; freq <= end_freq; freq += MHZ_TO_KHZ(20)) {
		c = ieee80211_get_channel_khz(wiphy, freq);
		if (!c)
			return 0;

		if (c->flags & IEEE80211_CHAN_DISABLED)
			return 0;

		if (!(c->flags & IEEE80211_CHAN_RADAR))
			continue;

		if (c->dfs_cac_ms > dfs_cac_ms)
			dfs_cac_ms = c->dfs_cac_ms;
	}

	return dfs_cac_ms;
}

unsigned int
cfg80211_chandef_dfs_cac_time(struct wiphy *wiphy,
			      const struct cfg80211_chan_def *chandef)
{
	int width;
	unsigned int t1 = 0, t2 = 0;

	if (WARN_ON(!cfg80211_chandef_valid(chandef)))
		return 0;

	width = cfg80211_chandef_get_width(chandef);
	if (width < 0)
		return 0;

	t1 = cfg80211_get_chans_dfs_cac_time(wiphy,
					     MHZ_TO_KHZ(chandef->center_freq1),
					     width);

	if (!chandef->center_freq2)
		return t1;

	t2 = cfg80211_get_chans_dfs_cac_time(wiphy,
					     MHZ_TO_KHZ(chandef->center_freq2),
					     width);

	return max(t1, t2);
}
EXPORT_SYMBOL(cfg80211_chandef_dfs_cac_time);

static bool cfg80211_secondary_chans_ok(struct wiphy *wiphy,
					u32 center_freq, u32 bandwidth,
					u32 prohibited_flags)
{
	struct ieee80211_channel *c;
	u32 freq, start_freq, end_freq;

	start_freq = cfg80211_get_start_freq(center_freq, bandwidth);
	end_freq = cfg80211_get_end_freq(center_freq, bandwidth);

	for (freq = start_freq; freq <= end_freq; freq += MHZ_TO_KHZ(20)) {
		c = ieee80211_get_channel_khz(wiphy, freq);
		if (!c || c->flags & prohibited_flags)
			return false;
	}

	return true;
}

/* check if the operating channels are valid and supported */
static bool cfg80211_edmg_usable(struct wiphy *wiphy, u8 edmg_channels,
				 enum ieee80211_edmg_bw_config edmg_bw_config,
				 int primary_channel,
				 struct ieee80211_edmg *edmg_cap)
{
	struct ieee80211_channel *chan;
	int i, freq;
	int channels_counter = 0;

	if (!edmg_channels && !edmg_bw_config)
		return true;

	if ((!edmg_channels && edmg_bw_config) ||
	    (edmg_channels && !edmg_bw_config))
		return false;

	if (!(edmg_channels & BIT(primary_channel - 1)))
		return false;

	/* 60GHz channels 1..6 */
	for (i = 0; i < 6; i++) {
		if (!(edmg_channels & BIT(i)))
			continue;

		if (!(edmg_cap->channels & BIT(i)))
			return false;

		channels_counter++;

		freq = ieee80211_channel_to_frequency(i + 1,
						      NL80211_BAND_60GHZ);
		chan = ieee80211_get_channel(wiphy, freq);
		if (!chan || chan->flags & IEEE80211_CHAN_DISABLED)
			return false;
	}

	/* IEEE802.11 allows max 4 channels */
	if (channels_counter > 4)
		return false;

	/* check bw_config is a subset of what driver supports
	 * (see IEEE P802.11ay/D4.0 section 9.4.2.251, Table 13)
	 */
	if ((edmg_bw_config % 4) > (edmg_cap->bw_config % 4))
		return false;

	if (edmg_bw_config > edmg_cap->bw_config)
		return false;

	return true;
}

bool cfg80211_chandef_usable(struct wiphy *wiphy,
			     const struct cfg80211_chan_def *chandef,
			     u32 prohibited_flags)
{
	struct ieee80211_sta_ht_cap *ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap;
	struct ieee80211_edmg *edmg_cap;
	u32 width, control_freq, cap;
	bool ext_nss_cap, support_80_80 = false, support_320 = false;
	const struct ieee80211_sband_iftype_data *iftd;
	struct ieee80211_supported_band *sband;
	int i;

	if (WARN_ON(!cfg80211_chandef_valid(chandef)))
		return false;

	ht_cap = &wiphy->bands[chandef->chan->band]->ht_cap;
	vht_cap = &wiphy->bands[chandef->chan->band]->vht_cap;
	edmg_cap = &wiphy->bands[chandef->chan->band]->edmg_cap;
	ext_nss_cap = __le16_to_cpu(vht_cap->vht_mcs.tx_highest) &
			IEEE80211_VHT_EXT_NSS_BW_CAPABLE;

	if (edmg_cap->channels &&
	    !cfg80211_edmg_usable(wiphy,
				  chandef->edmg.channels,
				  chandef->edmg.bw_config,
				  chandef->chan->hw_value,
				  edmg_cap))
		return false;

	control_freq = chandef->chan->center_freq;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_1:
		width = 1;
		break;
	case NL80211_CHAN_WIDTH_2:
		width = 2;
		break;
	case NL80211_CHAN_WIDTH_4:
		width = 4;
		break;
	case NL80211_CHAN_WIDTH_8:
		width = 8;
		break;
	case NL80211_CHAN_WIDTH_16:
		width = 16;
		break;
	case NL80211_CHAN_WIDTH_5:
		width = 5;
		break;
	case NL80211_CHAN_WIDTH_10:
		prohibited_flags |= IEEE80211_CHAN_NO_10MHZ;
		width = 10;
		break;
	case NL80211_CHAN_WIDTH_20:
		if (!ht_cap->ht_supported &&
		    chandef->chan->band != NL80211_BAND_6GHZ)
			return false;
		fallthrough;
	case NL80211_CHAN_WIDTH_20_NOHT:
		prohibited_flags |= IEEE80211_CHAN_NO_20MHZ;
		width = 20;
		break;
	case NL80211_CHAN_WIDTH_40:
		width = 40;
		if (chandef->chan->band == NL80211_BAND_6GHZ)
			break;
		if (!ht_cap->ht_supported)
			return false;
		if (!(ht_cap->cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) ||
		    ht_cap->cap & IEEE80211_HT_CAP_40MHZ_INTOLERANT)
			return false;
		if (chandef->center_freq1 < control_freq &&
		    chandef->chan->flags & IEEE80211_CHAN_NO_HT40MINUS)
			return false;
		if (chandef->center_freq1 > control_freq &&
		    chandef->chan->flags & IEEE80211_CHAN_NO_HT40PLUS)
			return false;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		cap = vht_cap->cap;
		support_80_80 =
			(cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ) ||
			(cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ &&
			 cap & IEEE80211_VHT_CAP_EXT_NSS_BW_MASK) ||
			(ext_nss_cap &&
			 u32_get_bits(cap, IEEE80211_VHT_CAP_EXT_NSS_BW_MASK) > 1);
		if (chandef->chan->band != NL80211_BAND_6GHZ && !support_80_80)
			return false;
		fallthrough;
	case NL80211_CHAN_WIDTH_80:
		prohibited_flags |= IEEE80211_CHAN_NO_80MHZ;
		width = 80;
		if (chandef->chan->band == NL80211_BAND_6GHZ)
			break;
		if (!vht_cap->vht_supported)
			return false;
		break;
	case NL80211_CHAN_WIDTH_160:
		prohibited_flags |= IEEE80211_CHAN_NO_160MHZ;
		width = 160;
		if (chandef->chan->band == NL80211_BAND_6GHZ)
			break;
		if (!vht_cap->vht_supported)
			return false;
		cap = vht_cap->cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;
		if (cap != IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ &&
		    cap != IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ &&
		    !(ext_nss_cap &&
		      (vht_cap->cap & IEEE80211_VHT_CAP_EXT_NSS_BW_MASK)))
			return false;
		break;
	case NL80211_CHAN_WIDTH_320:
		prohibited_flags |= IEEE80211_CHAN_NO_320MHZ;
		width = 320;

		if (chandef->chan->band != NL80211_BAND_6GHZ)
			return false;

		sband = wiphy->bands[NL80211_BAND_6GHZ];
		if (!sband)
			return false;

		for_each_sband_iftype_data(sband, i, iftd) {
			if (!iftd->eht_cap.has_eht)
				continue;

			if (iftd->eht_cap.eht_cap_elem.phy_cap_info[0] &
			    IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ) {
				support_320 = true;
				break;
			}
		}

		if (!support_320)
			return false;
		break;
	default:
		WARN_ON_ONCE(1);
		return false;
	}

	/*
	 * TODO: What if there are only certain 80/160/80+80 MHz channels
	 *	 allowed by the driver, or only certain combinations?
	 *	 For 40 MHz the driver can set the NO_HT40 flags, but for
	 *	 80/160 MHz and in particular 80+80 MHz this isn't really
	 *	 feasible and we only have NO_80MHZ/NO_160MHZ so far but
	 *	 no way to cover 80+80 MHz or more complex restrictions.
	 *	 Note that such restrictions also need to be advertised to
	 *	 userspace, for example for P2P channel selection.
	 */

	if (width > 20)
		prohibited_flags |= IEEE80211_CHAN_NO_OFDM;

	/* 5 and 10 MHz are only defined for the OFDM PHY */
	if (width < 20)
		prohibited_flags |= IEEE80211_CHAN_NO_OFDM;


	if (!cfg80211_secondary_chans_ok(wiphy,
					 ieee80211_chandef_to_khz(chandef),
					 width, prohibited_flags))
		return false;

	if (!chandef->center_freq2)
		return true;
	return cfg80211_secondary_chans_ok(wiphy,
					   MHZ_TO_KHZ(chandef->center_freq2),
					   width, prohibited_flags);
}
EXPORT_SYMBOL(cfg80211_chandef_usable);

static bool cfg80211_ir_permissive_check_wdev(enum nl80211_iftype iftype,
					      struct wireless_dev *wdev,
					      struct ieee80211_channel *chan)
{
	struct ieee80211_channel *other_chan = NULL;
	unsigned int link_id;
	int r1, r2;

	for_each_valid_link(wdev, link_id) {
		if (wdev->iftype == NL80211_IFTYPE_STATION &&
		    wdev->links[link_id].client.current_bss)
			other_chan = wdev->links[link_id].client.current_bss->pub.channel;

		/*
		 * If a GO already operates on the same GO_CONCURRENT channel,
		 * this one (maybe the same one) can beacon as well. We allow
		 * the operation even if the station we relied on with
		 * GO_CONCURRENT is disconnected now. But then we must make sure
		 * we're not outdoor on an indoor-only channel.
		 */
		if (iftype == NL80211_IFTYPE_P2P_GO &&
		    wdev->iftype == NL80211_IFTYPE_P2P_GO &&
		    wdev->links[link_id].ap.beacon_interval &&
		    !(chan->flags & IEEE80211_CHAN_INDOOR_ONLY))
			other_chan = wdev->links[link_id].ap.chandef.chan;

		if (!other_chan)
			continue;

		if (chan == other_chan)
			return true;

		if (chan->band != NL80211_BAND_5GHZ &&
		    chan->band != NL80211_BAND_6GHZ)
			continue;

		r1 = cfg80211_get_unii(chan->center_freq);
		r2 = cfg80211_get_unii(other_chan->center_freq);

		if (r1 != -EINVAL && r1 == r2) {
			/*
			 * At some locations channels 149-165 are considered a
			 * bundle, but at other locations, e.g., Indonesia,
			 * channels 149-161 are considered a bundle while
			 * channel 165 is left out and considered to be in a
			 * different bundle. Thus, in case that there is a
			 * station interface connected to an AP on channel 165,
			 * it is assumed that channels 149-161 are allowed for
			 * GO operations. However, having a station interface
			 * connected to an AP on channels 149-161, does not
			 * allow GO operation on channel 165.
			 */
			if (chan->center_freq == 5825 &&
			    other_chan->center_freq != 5825)
				continue;
			return true;
		}
	}

	return false;
}

/*
 * Check if the channel can be used under permissive conditions mandated by
 * some regulatory bodies, i.e., the channel is marked with
 * IEEE80211_CHAN_IR_CONCURRENT and there is an additional station interface
 * associated to an AP on the same channel or on the same UNII band
 * (assuming that the AP is an authorized master).
 * In addition allow operation on a channel on which indoor operation is
 * allowed, iff we are currently operating in an indoor environment.
 */
static bool cfg80211_ir_permissive_chan(struct wiphy *wiphy,
					enum nl80211_iftype iftype,
					struct ieee80211_channel *chan)
{
	struct wireless_dev *wdev;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	lockdep_assert_held(&rdev->wiphy.mtx);

	if (!IS_ENABLED(CONFIG_CFG80211_REG_RELAX_NO_IR) ||
	    !(wiphy->regulatory_flags & REGULATORY_ENABLE_RELAX_NO_IR))
		return false;

	/* only valid for GO and TDLS off-channel (station/p2p-CL) */
	if (iftype != NL80211_IFTYPE_P2P_GO &&
	    iftype != NL80211_IFTYPE_STATION &&
	    iftype != NL80211_IFTYPE_P2P_CLIENT)
		return false;

	if (regulatory_indoor_allowed() &&
	    (chan->flags & IEEE80211_CHAN_INDOOR_ONLY))
		return true;

	if (!(chan->flags & IEEE80211_CHAN_IR_CONCURRENT))
		return false;

	/*
	 * Generally, it is possible to rely on another device/driver to allow
	 * the IR concurrent relaxation, however, since the device can further
	 * enforce the relaxation (by doing a similar verifications as this),
	 * and thus fail the GO instantiation, consider only the interfaces of
	 * the current registered device.
	 */
	list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list) {
		bool ret;

		ret = cfg80211_ir_permissive_check_wdev(iftype, wdev, chan);
		if (ret)
			return ret;
	}

	return false;
}

static bool _cfg80211_reg_can_beacon(struct wiphy *wiphy,
				     struct cfg80211_chan_def *chandef,
				     enum nl80211_iftype iftype,
				     bool check_no_ir)
{
	bool res;
	u32 prohibited_flags = IEEE80211_CHAN_DISABLED |
			       IEEE80211_CHAN_RADAR;

	trace_cfg80211_reg_can_beacon(wiphy, chandef, iftype, check_no_ir);

	if (check_no_ir)
		prohibited_flags |= IEEE80211_CHAN_NO_IR;

	if (cfg80211_chandef_dfs_required(wiphy, chandef, iftype) > 0 &&
	    cfg80211_chandef_dfs_available(wiphy, chandef)) {
		/* We can skip IEEE80211_CHAN_NO_IR if chandef dfs available */
		prohibited_flags = IEEE80211_CHAN_DISABLED;
	}

	res = cfg80211_chandef_usable(wiphy, chandef, prohibited_flags);

	trace_cfg80211_return_bool(res);
	return res;
}

bool cfg80211_reg_can_beacon(struct wiphy *wiphy,
			     struct cfg80211_chan_def *chandef,
			     enum nl80211_iftype iftype)
{
	return _cfg80211_reg_can_beacon(wiphy, chandef, iftype, true);
}
EXPORT_SYMBOL(cfg80211_reg_can_beacon);

bool cfg80211_reg_can_beacon_relax(struct wiphy *wiphy,
				   struct cfg80211_chan_def *chandef,
				   enum nl80211_iftype iftype)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	bool check_no_ir;

	lockdep_assert_held(&rdev->wiphy.mtx);

	/*
	 * Under certain conditions suggested by some regulatory bodies a
	 * GO/STA can IR on channels marked with IEEE80211_NO_IR. Set this flag
	 * only if such relaxations are not enabled and the conditions are not
	 * met.
	 */
	check_no_ir = !cfg80211_ir_permissive_chan(wiphy, iftype,
						   chandef->chan);

	return _cfg80211_reg_can_beacon(wiphy, chandef, iftype, check_no_ir);
}
EXPORT_SYMBOL(cfg80211_reg_can_beacon_relax);

int cfg80211_set_monitor_channel(struct cfg80211_registered_device *rdev,
				 struct cfg80211_chan_def *chandef)
{
	if (!rdev->ops->set_monitor_channel)
		return -EOPNOTSUPP;
	if (!cfg80211_has_monitors_only(rdev))
		return -EBUSY;

	return rdev_set_monitor_channel(rdev, chandef);
}

bool cfg80211_any_usable_channels(struct wiphy *wiphy,
				  unsigned long sband_mask,
				  u32 prohibited_flags)
{
	int idx;

	prohibited_flags |= IEEE80211_CHAN_DISABLED;

	for_each_set_bit(idx, &sband_mask, NUM_NL80211_BANDS) {
		struct ieee80211_supported_band *sband = wiphy->bands[idx];
		int chanidx;

		if (!sband)
			continue;

		for (chanidx = 0; chanidx < sband->n_channels; chanidx++) {
			struct ieee80211_channel *chan;

			chan = &sband->channels[chanidx];

			if (chan->flags & prohibited_flags)
				continue;

			return true;
		}
	}

	return false;
}
EXPORT_SYMBOL(cfg80211_any_usable_channels);

struct cfg80211_chan_def *wdev_chandef(struct wireless_dev *wdev,
				       unsigned int link_id)
{
	lockdep_assert_wiphy(wdev->wiphy);

	WARN_ON(wdev->valid_links && !(wdev->valid_links & BIT(link_id)));
	WARN_ON(!wdev->valid_links && link_id > 0);

	switch (wdev->iftype) {
	case NL80211_IFTYPE_MESH_POINT:
		return &wdev->u.mesh.chandef;
	case NL80211_IFTYPE_ADHOC:
		return &wdev->u.ibss.chandef;
	case NL80211_IFTYPE_OCB:
		return &wdev->u.ocb.chandef;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		return &wdev->links[link_id].ap.chandef;
	default:
		return NULL;
	}
}
EXPORT_SYMBOL(wdev_chandef);

struct cfg80211_per_bw_puncturing_values {
	u8 len;
	const u16 *valid_values;
};

static const u16 puncturing_values_80mhz[] = {
	0x8, 0x4, 0x2, 0x1
};

static const u16 puncturing_values_160mhz[] = {
	 0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1, 0xc0, 0x30, 0xc, 0x3
};

static const u16 puncturing_values_320mhz[] = {
	0xc000, 0x3000, 0xc00, 0x300, 0xc0, 0x30, 0xc, 0x3, 0xf000, 0xf00,
	0xf0, 0xf, 0xfc00, 0xf300, 0xf0c0, 0xf030, 0xf00c, 0xf003, 0xc00f,
	0x300f, 0xc0f, 0x30f, 0xcf, 0x3f
};

#define CFG80211_PER_BW_VALID_PUNCTURING_VALUES(_bw) \
	{ \
		.len = ARRAY_SIZE(puncturing_values_ ## _bw ## mhz), \
		.valid_values = puncturing_values_ ## _bw ## mhz \
	}

static const struct cfg80211_per_bw_puncturing_values per_bw_puncturing[] = {
	CFG80211_PER_BW_VALID_PUNCTURING_VALUES(80),
	CFG80211_PER_BW_VALID_PUNCTURING_VALUES(160),
	CFG80211_PER_BW_VALID_PUNCTURING_VALUES(320)
};

bool cfg80211_valid_disable_subchannel_bitmap(u16 *bitmap,
					      const struct cfg80211_chan_def *chandef)
{
	u32 idx, i, start_freq;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_80:
		idx = 0;
		start_freq = chandef->center_freq1 - 40;
		break;
	case NL80211_CHAN_WIDTH_160:
		idx = 1;
		start_freq = chandef->center_freq1 - 80;
		break;
	case NL80211_CHAN_WIDTH_320:
		idx = 2;
		start_freq = chandef->center_freq1 - 160;
		break;
	default:
		*bitmap = 0;
		break;
	}

	if (!*bitmap)
		return true;

	/* check if primary channel is punctured */
	if (*bitmap & (u16)BIT((chandef->chan->center_freq - start_freq) / 20))
		return false;

	for (i = 0; i < per_bw_puncturing[idx].len; i++)
		if (per_bw_puncturing[idx].valid_values[i] == *bitmap)
			return true;

	return false;
}
EXPORT_SYMBOL(cfg80211_valid_disable_subchannel_bitmap);
