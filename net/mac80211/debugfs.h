/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MAC80211_DEFS_H
#define __MAC80211_DEFS_H

#include "ieee80211_i.h"

#ifdef CONFIG_MAC80211_DEFS
void defs_hw_add(struct ieee80211_local *local);
int __printf(4, 5) mac80211_format_buffer(char __user *userbuf, size_t count,
					  loff_t *ppos, char *fmt, ...);
#else
static inline void defs_hw_add(struct ieee80211_local *local)
{
}
#endif

#endif /* __MAC80211_DEFS_H */
