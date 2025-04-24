// SPDX-License-Identifier: GPL-2.0
/*
 * Wifi Band Exclusion Interface for WLAN
 * Copyright (C) 2023 Advanced Micro Devices
 * Copyright (C) 2025 Intel Corporation
 *
 */

#include <linux/acpi_amd_wbrf.h>
#include <linux/units.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"

void ieee80211_check_wbrf_support(struct ieee80211_local *local)
{
	struct wiphy *wiphy = local->hw.wiphy;
	struct device *dev;

	if (!wiphy)
		return;

	dev = wiphy->dev.parent;
	if (!dev)
		return;

	local->wbrf_supported = acpi_amd_wbrf_supported_producer(dev);
}

static void get_chan_freq_boundary(u32 center_freq, u32 bandwidth, u64 *start, u64 *end)
{
	bandwidth *= KHZ_PER_MHZ;
	center_freq *= KHZ_PER_MHZ;

	*start = center_freq - bandwidth / 2;
	*end = center_freq + bandwidth / 2;

	/* Frequency in Hz is expected */
	*start = *start * HZ_PER_KHZ;
	*end = *end * HZ_PER_KHZ;
}

static void get_ranges_from_chandef(struct cfg80211_chan_def *chandef,
				    struct wbrf_ranges_in_out *ranges_in)
{
	u64 start_freq1, end_freq1;
	u64 start_freq2, end_freq2;
	int bandwidth;

	bandwidth = cfg80211_chandef_get_width(chandef);

	get_chan_freq_boundary(chandef->center_freq1, bandwidth, &start_freq1, &end_freq1);

	ranges_in->band_list[0].start = start_freq1;
	ranges_in->band_list[0].end = end_freq1;
	ranges_in->num_of_ranges = 1;

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		get_chan_freq_boundary(chandef->center_freq2, bandwidth, &start_freq2, &end_freq2);

		ranges_in->band_list[1].start = start_freq2;
		ranges_in->band_list[1].end = end_freq2;
		ranges_in->num_of_ranges++;
	}
}

void ieee80211_add_wbrf(struct ieee80211_local *local, struct cfg80211_chan_def *chandef)
{
	struct wbrf_ranges_in_out ranges_in = {0};
	struct device *dev;

	if (!local->wbrf_supported)
		return;

	dev = local->hw.wiphy->dev.parent;

	get_ranges_from_chandef(chandef, &ranges_in);

	acpi_amd_wbrf_add_remove(dev, WBRF_RECORD_ADD, &ranges_in);
}

void ieee80211_remove_wbrf(struct ieee80211_local *local, struct cfg80211_chan_def *chandef)
{
	struct wbrf_ranges_in_out ranges_in = {0};
	struct device *dev;

	if (!local->wbrf_supported)
		return;

	dev = local->hw.wiphy->dev.parent;

	get_ranges_from_chandef(chandef, &ranges_in);

	acpi_amd_wbrf_add_remove(dev, WBRF_RECORD_REMOVE, &ranges_in);
}
