/*
 * Copyright 2006, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* just for IFNAMSIZ */
#include <linux/if.h>
#include <linux/slab.h>
#include "led.h"

void ieee80211_led_rx(struct ieee80211_local *local)
{
	if (unlikely(!local->rx_led))
		return;
	if (local->rx_led_counter++ % 2 == 0)
		led_trigger_event(local->rx_led, LED_OFF);
	else
		led_trigger_event(local->rx_led, LED_FULL);
}

/* q is 1 if a packet was enqueued, 0 if it has been transmitted */
void ieee80211_led_tx(struct ieee80211_local *local, int q)
{
	if (unlikely(!local->tx_led))
		return;
	/* not sure how this is supposed to work ... */
	local->tx_led_counter += 2*q-1;
	if (local->tx_led_counter % 2 == 0)
		led_trigger_event(local->tx_led, LED_OFF);
	else
		led_trigger_event(local->tx_led, LED_FULL);
}

void ieee80211_led_assoc(struct ieee80211_local *local, bool associated)
{
	if (unlikely(!local->assoc_led))
		return;
	if (associated)
		led_trigger_event(local->assoc_led, LED_FULL);
	else
		led_trigger_event(local->assoc_led, LED_OFF);
}

void ieee80211_led_radio(struct ieee80211_local *local, bool enabled)
{
	if (unlikely(!local->radio_led))
		return;
	if (enabled)
		led_trigger_event(local->radio_led, LED_FULL);
	else
		led_trigger_event(local->radio_led, LED_OFF);
}

void ieee80211_led_names(struct ieee80211_local *local)
{
	snprintf(local->rx_led_name, sizeof(local->rx_led_name),
		 "%srx", wiphy_name(local->hw.wiphy));
	snprintf(local->tx_led_name, sizeof(local->tx_led_name),
		 "%stx", wiphy_name(local->hw.wiphy));
	snprintf(local->assoc_led_name, sizeof(local->assoc_led_name),
		 "%sassoc", wiphy_name(local->hw.wiphy));
	snprintf(local->radio_led_name, sizeof(local->radio_led_name),
		 "%sradio", wiphy_name(local->hw.wiphy));
}

void ieee80211_led_init(struct ieee80211_local *local)
{
	local->rx_led = kzalloc(sizeof(struct led_trigger), GFP_KERNEL);
	if (local->rx_led) {
		local->rx_led->name = local->rx_led_name;
		if (led_trigger_register(local->rx_led)) {
			kfree(local->rx_led);
			local->rx_led = NULL;
		}
	}

	local->tx_led = kzalloc(sizeof(struct led_trigger), GFP_KERNEL);
	if (local->tx_led) {
		local->tx_led->name = local->tx_led_name;
		if (led_trigger_register(local->tx_led)) {
			kfree(local->tx_led);
			local->tx_led = NULL;
		}
	}

	local->assoc_led = kzalloc(sizeof(struct led_trigger), GFP_KERNEL);
	if (local->assoc_led) {
		local->assoc_led->name = local->assoc_led_name;
		if (led_trigger_register(local->assoc_led)) {
			kfree(local->assoc_led);
			local->assoc_led = NULL;
		}
	}

	local->radio_led = kzalloc(sizeof(struct led_trigger), GFP_KERNEL);
	if (local->radio_led) {
		local->radio_led->name = local->radio_led_name;
		if (led_trigger_register(local->radio_led)) {
			kfree(local->radio_led);
			local->radio_led = NULL;
		}
	}

	if (local->tpt_led_trigger) {
		if (led_trigger_register(&local->tpt_led_trigger->trig)) {
			kfree(local->tpt_led_trigger);
			local->tpt_led_trigger = NULL;
		}
	}
}

void ieee80211_led_exit(struct ieee80211_local *local)
{
	if (local->radio_led) {
		led_trigger_unregister(local->radio_led);
		kfree(local->radio_led);
	}
	if (local->assoc_led) {
		led_trigger_unregister(local->assoc_led);
		kfree(local->assoc_led);
	}
	if (local->tx_led) {
		led_trigger_unregister(local->tx_led);
		kfree(local->tx_led);
	}
	if (local->rx_led) {
		led_trigger_unregister(local->rx_led);
		kfree(local->rx_led);
	}

	if (local->tpt_led_trigger) {
		led_trigger_unregister(&local->tpt_led_trigger->trig);
		kfree(local->tpt_led_trigger);
	}
}

char *__ieee80211_get_radio_led_name(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	return local->radio_led_name;
}
EXPORT_SYMBOL(__ieee80211_get_radio_led_name);

char *__ieee80211_get_assoc_led_name(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	return local->assoc_led_name;
}
EXPORT_SYMBOL(__ieee80211_get_assoc_led_name);

char *__ieee80211_get_tx_led_name(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	return local->tx_led_name;
}
EXPORT_SYMBOL(__ieee80211_get_tx_led_name);

char *__ieee80211_get_rx_led_name(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	return local->rx_led_name;
}
EXPORT_SYMBOL(__ieee80211_get_rx_led_name);

static unsigned long tpt_trig_traffic(struct ieee80211_local *local,
				      struct tpt_led_trigger *tpt_trig)
{
	unsigned long traffic, delta;

	traffic = tpt_trig->tx_bytes + tpt_trig->rx_bytes;

	delta = traffic - tpt_trig->prev_traffic;
	tpt_trig->prev_traffic = traffic;
	return DIV_ROUND_UP(delta, 1024 / 8);
}

static void tpt_trig_timer(unsigned long data)
{
	struct ieee80211_local *local = (void *)data;
	struct tpt_led_trigger *tpt_trig = local->tpt_led_trigger;
	struct led_classdev *led_cdev;
	unsigned long on, off, tpt;
	int i;

	if (!tpt_trig->running)
		return;

	mod_timer(&tpt_trig->timer, round_jiffies(jiffies + HZ));

	tpt = tpt_trig_traffic(local, tpt_trig);

	/* default to just solid on */
	on = 1;
	off = 0;

	for (i = tpt_trig->blink_table_len - 1; i >= 0; i--) {
		if (tpt_trig->blink_table[i].throughput < 0 ||
		    tpt > tpt_trig->blink_table[i].throughput) {
			off = tpt_trig->blink_table[i].blink_time / 2;
			on = tpt_trig->blink_table[i].blink_time - off;
			break;
		}
	}

	read_lock(&tpt_trig->trig.leddev_list_lock);
	list_for_each_entry(led_cdev, &tpt_trig->trig.led_cdevs, trig_list)
		led_blink_set(led_cdev, &on, &off);
	read_unlock(&tpt_trig->trig.leddev_list_lock);
}

char *__ieee80211_create_tpt_led_trigger(struct ieee80211_hw *hw,
				unsigned int flags,
				const struct ieee80211_tpt_blink *blink_table,
				unsigned int blink_table_len)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct tpt_led_trigger *tpt_trig;

	if (WARN_ON(local->tpt_led_trigger))
		return NULL;

	tpt_trig = kzalloc(sizeof(struct tpt_led_trigger), GFP_KERNEL);
	if (!tpt_trig)
		return NULL;

	snprintf(tpt_trig->name, sizeof(tpt_trig->name),
		 "%stpt", wiphy_name(local->hw.wiphy));

	tpt_trig->trig.name = tpt_trig->name;

	tpt_trig->blink_table = blink_table;
	tpt_trig->blink_table_len = blink_table_len;
	tpt_trig->want = flags;

	setup_timer(&tpt_trig->timer, tpt_trig_timer, (unsigned long)local);

	local->tpt_led_trigger = tpt_trig;

	return tpt_trig->name;
}
EXPORT_SYMBOL(__ieee80211_create_tpt_led_trigger);

static void ieee80211_start_tpt_led_trig(struct ieee80211_local *local)
{
	struct tpt_led_trigger *tpt_trig = local->tpt_led_trigger;

	if (tpt_trig->running)
		return;

	/* reset traffic */
	tpt_trig_traffic(local, tpt_trig);
	tpt_trig->running = true;

	tpt_trig_timer((unsigned long)local);
	mod_timer(&tpt_trig->timer, round_jiffies(jiffies + HZ));
}

static void ieee80211_stop_tpt_led_trig(struct ieee80211_local *local)
{
	struct tpt_led_trigger *tpt_trig = local->tpt_led_trigger;
	struct led_classdev *led_cdev;

	if (!tpt_trig->running)
		return;

	tpt_trig->running = false;
	del_timer_sync(&tpt_trig->timer);

	read_lock(&tpt_trig->trig.leddev_list_lock);
	list_for_each_entry(led_cdev, &tpt_trig->trig.led_cdevs, trig_list)
		led_brightness_set(led_cdev, LED_OFF);
	read_unlock(&tpt_trig->trig.leddev_list_lock);
}

void ieee80211_mod_tpt_led_trig(struct ieee80211_local *local,
				unsigned int types_on, unsigned int types_off)
{
	struct tpt_led_trigger *tpt_trig = local->tpt_led_trigger;
	bool allowed;

	WARN_ON(types_on & types_off);

	if (!tpt_trig)
		return;

	tpt_trig->active &= ~types_off;
	tpt_trig->active |= types_on;

	/*
	 * Regardless of wanted state, we shouldn't blink when
	 * the radio is disabled -- this can happen due to some
	 * code ordering issues with __ieee80211_recalc_idle()
	 * being called before the radio is started.
	 */
	allowed = tpt_trig->active & IEEE80211_TPT_LEDTRIG_FL_RADIO;

	if (!allowed || !(tpt_trig->active & tpt_trig->want))
		ieee80211_stop_tpt_led_trig(local);
	else
		ieee80211_start_tpt_led_trig(local);
}
