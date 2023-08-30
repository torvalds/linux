/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _LINUX_DROPREASON_H
#define _LINUX_DROPREASON_H
#include <net/dropreason-core.h>

/**
 * enum skb_drop_reason_subsys - subsystem tag for (extended) drop reasons
 */
enum skb_drop_reason_subsys {
	/** @SKB_DROP_REASON_SUBSYS_CORE: core drop reasons defined above */
	SKB_DROP_REASON_SUBSYS_CORE,

	/**
	 * @SKB_DROP_REASON_SUBSYS_MAC80211_UNUSABLE: mac80211 drop reasons
	 * for unusable frames, see net/mac80211/drop.h
	 */
	SKB_DROP_REASON_SUBSYS_MAC80211_UNUSABLE,

	/**
	 * @SKB_DROP_REASON_SUBSYS_MAC80211_MONITOR: mac80211 drop reasons
	 * for frames still going to monitor, see net/mac80211/drop.h
	 */
	SKB_DROP_REASON_SUBSYS_MAC80211_MONITOR,

	/** @SKB_DROP_REASON_SUBSYS_NUM: number of subsystems defined */
	SKB_DROP_REASON_SUBSYS_NUM
};

struct drop_reason_list {
	const char * const *reasons;
	size_t n_reasons;
};

/* Note: due to dynamic registrations, access must be under RCU */
extern const struct drop_reason_list __rcu *
drop_reasons_by_subsys[SKB_DROP_REASON_SUBSYS_NUM];

void drop_reasons_register_subsys(enum skb_drop_reason_subsys subsys,
				  const struct drop_reason_list *list);
void drop_reasons_unregister_subsys(enum skb_drop_reason_subsys subsys);

#endif
