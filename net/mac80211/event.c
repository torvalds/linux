/*
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * mac80211 - events
 */
#include <net/cfg80211.h>
#include "ieee80211_i.h"

/*
 * Indicate a failed Michael MIC to userspace. If the caller knows the TSC of
 * the frame that generated the MIC failure (i.e., if it was provided by the
 * driver or is still in the frame), it should provide that information.
 */
void mac80211_ev_michael_mic_failure(struct ieee80211_sub_if_data *sdata, int keyidx,
				     struct ieee80211_hdr *hdr, const u8 *tsc,
				     gfp_t gfp)
{
	cfg80211_michael_mic_failure(sdata->dev, hdr->addr2,
				     (hdr->addr1[0] & 0x01) ?
				     NL80211_KEYTYPE_GROUP :
				     NL80211_KEYTYPE_PAIRWISE,
				     keyidx, tsc, gfp);
}
