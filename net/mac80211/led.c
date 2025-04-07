// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2006, Johannes Berg <johannes@sipsolutions.net>
 */

/* just for IFNAMSIZ */
#include <linux/if.h>
#include <linux/slab.h>
#include <linux/export.h>
#include "led.h"

void ieee80211_led_assoc(struct ieee80211_local *local, bool associated)
{
	if (!atomic_read(&local->assoc_led_active))
		return;
	if (associated)
		led_trigger_event(&local->assoc_led, LED_FULL);
	else
		led_trigger_event(&local->assoc_led, LED_OFF);
}

void ieee80211_led_radio(struct ieee80211_local *local, bool enabled)
{
	if (!atomic_read(&local->radio_led_active))
		return;
	if (enabled)
		led_trigger_event(&local->radio_led, LED_FULL);
	else
		led_trigger_event(&local->radio_led, LED_OFF);
}

void ieee80211_alloc_led_names(struct ieee80211_local *local)
{
	local->rx_led.name = kasprintf(GFP_KERNEL, "%srx",
				       wiphy_name(local->hw.wiphy));
	local->tx_led.name = kasprintf(GFP_KERNEL, "%stx",
				       wiphy_name(local->hw.wiphy));
	local->assoc_led.name = kasprintf(GFP_KERNEL, "%sassoc",
					  wiphy_name(local->hw.wiphy));
	local->radio_led.name = kasprintf(GFP_KERNEL, "%sradio",
					  wiphy_name(local->hw.wiphy));
}

void ieee80211_free_led_names(struct ieee80211_local *local)
{
	kfree(local->rx_led.name);
	kfree(local->tx_led.name);
	kfree(local->assoc_led.name);
	kfree(local->radio_led.name);
}

static int ieee80211_tx_led_activate(struct led_classdev *led_cdev)
{
	struct ieee80211_local *local = container_of(led_cdev->trigger,
						     struct ieee80211_local,
						     tx_led);

	atomic_inc(&local->tx_led_active);

	return 0;
}

static void ieee80211_tx_led_deactivate(struct led_classdev *led_cdev)
{
	struct ieee80211_local *local = container_of(led_cdev->trigger,
						     struct ieee80211_local,
						     tx_led);

	atomic_dec(&local->tx_led_active);
}

static int ieee80211_rx_led_activate(struct led_classdev *led_cdev)
{
	struct ieee80211_local *local = container_of(led_cdev->trigger,
						     struct ieee80211_local,
						     rx_led);

	atomic_inc(&local->rx_led_active);

	return 0;
}

static void ieee80211_rx_led_deactivate(struct led_classdev *led_cdev)
{
	struct ieee80211_local *local = container_of(led_cdev->trigger,
						     struct ieee80211_local,
						     rx_led);

	atomic_dec(&local->rx_led_active);
}

static int ieee80211_assoc_led_activate(struct led_classdev *led_cdev)
{
	struct ieee80211_local *local = container_of(led_cdev->trigger,
						     struct ieee80211_local,
						     assoc_led);

	atomic_inc(&local->assoc_led_active);

	return 0;
}

static void ieee80211_assoc_led_deactivate(struct led_classdev *led_cdev)
{
	struct ieee80211_local *local = container_of(led_cdev->trigger,
						     struct ieee80211_local,
						     assoc_led);

	atomic_dec(&local->assoc_led_active);
}

static int ieee80211_radio_led_activate(struct led_classdev *led_cdev)
{
	struct ieee80211_local *local = container_of(led_cdev->trigger,
						     struct ieee80211_local,
						     radio_led);

	atomic_inc(&local->radio_led_active);

	return 0;
}

static void ieee80211_radio_led_deactivate(struct led_classdev *led_cdev)
{
	struct ieee80211_local *local = container_of(led_cdev->trigger,
						     struct ieee80211_local,
						     radio_led);

	atomic_dec(&local->radio_led_active);
}

static int ieee80211_tpt_led_activate(struct led_classdev *led_cdev)
{
	struct ieee80211_local *local = container_of(led_cdev->trigger,
						     struct ieee80211_local,
						     tpt_led);

	atomic_inc(&local->tpt_led_active);

	return 0;
}

static void ieee80211_tpt_led_deactivate(struct led_classdev *led_cdev)
{
	struct ieee80211_local *local = container_of(led_cdev->trigger,
						     struct ieee80211_local,
						     tpt_led);

	atomic_dec(&local->tpt_led_active);
}

void ieee80211_led_init(struct ieee80211_local *local)
{
	atomic_set(&local->rx_led_active, 0);
	local->rx_led.activate = ieee80211_rx_led_activate;
	local->rx_led.deactivate = ieee80211_rx_led_deactivate;
	if (local->rx_led.name && led_trigger_register(&local->rx_led)) {
		kfree(local->rx_led.name);
		local->rx_led.name = NULL;
	}

	atomic_set(&local->tx_led_active, 0);
	local->tx_led.activate = ieee80211_tx_led_activate;
	local->tx_led.deactivate = ieee80211_tx_led_deactivate;
	if (local->tx_led.name && led_trigger_register(&local->tx_led)) {
		kfree(local->tx_led.name);
		local->tx_led.name = NULL;
	}

	atomic_set(&local->assoc_led_active, 0);
	local->assoc_led.activate = ieee80211_assoc_led_activate;
	local->assoc_led.deactivate = ieee80211_assoc_led_deactivate;
	if (local->assoc_led.name && led_trigger_register(&local->assoc_led)) {
		kfree(local->assoc_led.name);
		local->assoc_led.name = NULL;
	}

	atomic_set(&local->radio_led_active, 0);
	local->radio_led.activate = ieee80211_radio_led_activate;
	local->radio_led.deactivate = ieee80211_radio_led_deactivate;
	if (local->radio_led.name && led_trigger_register(&local->radio_led)) {
		kfree(local->radio_led.name);
		local->radio_led.name = NULL;
	}

	atomic_set(&local->tpt_led_active, 0);
	if (local->tpt_led_trigger) {
		local->tpt_led.activate = ieee80211_tpt_led_activate;
		local->tpt_led.deactivate = ieee80211_tpt_led_deactivate;
		if (led_trigger_register(&local->tpt_led)) {
			kfree(local->tpt_led_trigger);
			local->tpt_led_trigger = NULL;
		}
	}
}

void ieee80211_led_exit(struct ieee80211_local *local)
{
	if (local->radio_led.name)
		led_trigger_unregister(&local->radio_led);
	if (local->assoc_led.name)
		led_trigger_unregister(&local->assoc_led);
	if (local->tx_led.name)
		led_trigger_unregister(&local->tx_led);
	if (local->rx_led.name)
		led_trigger_unregister(&local->rx_led);

	if (local->tpt_led_trigger) {
		led_trigger_unregister(&local->tpt_led);
		kfree(local->tpt_led_trigger);
	}
}

const char *__ieee80211_get_radio_led_name(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	return local->radio_led.name;
}
EXPORT_SYMBOL(__ieee80211_get_radio_led_name);

const char *__ieee80211_get_assoc_led_name(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	return local->assoc_led.name;
}
EXPORT_SYMBOL(__ieee80211_get_assoc_led_name);

const char *__ieee80211_get_tx_led_name(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	return local->tx_led.name;
}
EXPORT_SYMBOL(__ieee80211_get_tx_led_name);

const char *__ieee80211_get_rx_led_name(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	return local->rx_led.name;
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

static void tpt_trig_timer(struct timer_list *t)
{
	struct tpt_led_trigger *tpt_trig = from_timer(tpt_trig, t, timer);
	struct ieee80211_local *local = tpt_trig->local;
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

	led_trigger_blink(&local->tpt_led, on, off);
}

const char *
__ieee80211_create_tpt_led_trigger(struct ieee80211_hw *hw,
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

	local->tpt_led.name = tpt_trig->name;

	tpt_trig->blink_table = blink_table;
	tpt_trig->blink_table_len = blink_table_len;
	tpt_trig->want = flags;
	tpt_trig->local = local;

	timer_setup(&tpt_trig->timer, tpt_trig_timer, 0);

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

	tpt_trig_timer(&tpt_trig->timer);
	mod_timer(&tpt_trig->timer, round_jiffies(jiffies + HZ));
}

static void ieee80211_stop_tpt_led_trig(struct ieee80211_local *local)
{
	struct tpt_led_trigger *tpt_trig = local->tpt_led_trigger;

	if (!tpt_trig->running)
		return;

	tpt_trig->running = false;
	timer_delete_sync(&tpt_trig->timer);

	led_trigger_event(&local->tpt_led, LED_OFF);
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
