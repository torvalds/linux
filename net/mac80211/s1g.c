// SPDX-License-Identifier: GPL-2.0
/*
 * S1G handling
 * Copyright(c) 2020 Adapt-IP
 */
#include <linux/ieee80211.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"

void ieee80211_s1g_sta_rate_init(struct sta_info *sta)
{
	/* avoid indicating legacy bitrates for S1G STAs */
	sta->tx_stats.last_rate.flags |= IEEE80211_TX_RC_S1G_MCS;
	sta->rx_stats.last_rate =
			STA_STATS_FIELD(TYPE, STA_STATS_RATE_TYPE_S1G);
}
