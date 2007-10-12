/*
 * Copyright 2006, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/leds.h>
#include "ieee80211_i.h"

#ifdef CONFIG_MAC80211_LEDS
extern void ieee80211_led_rx(struct ieee80211_local *local);
extern void ieee80211_led_tx(struct ieee80211_local *local, int q);
extern void ieee80211_led_assoc(struct ieee80211_local *local,
				bool associated);
extern void ieee80211_led_init(struct ieee80211_local *local);
extern void ieee80211_led_exit(struct ieee80211_local *local);
#else
static inline void ieee80211_led_rx(struct ieee80211_local *local)
{
}
static inline void ieee80211_led_tx(struct ieee80211_local *local, int q)
{
}
static inline void ieee80211_led_assoc(struct ieee80211_local *local,
				       bool associated)
{
}
static inline void ieee80211_led_init(struct ieee80211_local *local)
{
}
static inline void ieee80211_led_exit(struct ieee80211_local *local)
{
}
#endif
