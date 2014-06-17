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
void ieee80211_led_rx(struct ieee80211_local *local);
void ieee80211_led_tx(struct ieee80211_local *local);
void ieee80211_led_assoc(struct ieee80211_local *local,
			 bool associated);
void ieee80211_led_radio(struct ieee80211_local *local,
			 bool enabled);
void ieee80211_led_names(struct ieee80211_local *local);
void ieee80211_led_init(struct ieee80211_local *local);
void ieee80211_led_exit(struct ieee80211_local *local);
void ieee80211_mod_tpt_led_trig(struct ieee80211_local *local,
				unsigned int types_on, unsigned int types_off);
#else
static inline void ieee80211_led_rx(struct ieee80211_local *local)
{
}
static inline void ieee80211_led_tx(struct ieee80211_local *local)
{
}
static inline void ieee80211_led_assoc(struct ieee80211_local *local,
				       bool associated)
{
}
static inline void ieee80211_led_radio(struct ieee80211_local *local,
				       bool enabled)
{
}
static inline void ieee80211_led_names(struct ieee80211_local *local)
{
}
static inline void ieee80211_led_init(struct ieee80211_local *local)
{
}
static inline void ieee80211_led_exit(struct ieee80211_local *local)
{
}
static inline void ieee80211_mod_tpt_led_trig(struct ieee80211_local *local,
					      unsigned int types_on,
					      unsigned int types_off)
{
}
#endif

static inline void
ieee80211_tpt_led_trig_tx(struct ieee80211_local *local, __le16 fc, int bytes)
{
#ifdef CONFIG_MAC80211_LEDS
	if (local->tpt_led_trigger && ieee80211_is_data(fc))
		local->tpt_led_trigger->tx_bytes += bytes;
#endif
}

static inline void
ieee80211_tpt_led_trig_rx(struct ieee80211_local *local, __le16 fc, int bytes)
{
#ifdef CONFIG_MAC80211_LEDS
	if (local->tpt_led_trigger && ieee80211_is_data(fc))
		local->tpt_led_trigger->rx_bytes += bytes;
#endif
}
