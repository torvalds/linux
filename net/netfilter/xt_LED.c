// SPDX-License-Identifier: GPL-2.0-only
/*
 * xt_LED.c - netfilter target to make LEDs blink upon packet matches
 *
 * Copyright (C) 2008 Adam Nielsen <a.nielsen@shikadi.net>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter/x_tables.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/mutex.h>

#include <linux/netfilter/xt_LED.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adam Nielsen <a.nielsen@shikadi.net>");
MODULE_DESCRIPTION("Xtables: trigger LED devices on packet match");
MODULE_ALIAS("ipt_LED");
MODULE_ALIAS("ip6t_LED");

static LIST_HEAD(xt_led_triggers);
static DEFINE_MUTEX(xt_led_mutex);

/*
 * This is declared in here (the kernel module) only, to avoid having these
 * dependencies in userspace code.  This is what xt_led_info.internal_data
 * points to.
 */
struct xt_led_info_internal {
	struct list_head list;
	int refcnt;
	char *trigger_id;
	struct led_trigger netfilter_led_trigger;
	struct timer_list timer;
};

#define XT_LED_BLINK_DELAY 50 /* ms */

static unsigned int
led_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_led_info *ledinfo = par->targinfo;
	struct xt_led_info_internal *ledinternal = ledinfo->internal_data;

	/*
	 * If "always blink" is enabled, and there's still some time until the
	 * LED will switch off, briefly switch it off now.
	 */
	if ((ledinfo->delay > 0) && ledinfo->always_blink &&
	    timer_pending(&ledinternal->timer))
		led_trigger_blink_oneshot(&ledinternal->netfilter_led_trigger,
					  XT_LED_BLINK_DELAY, XT_LED_BLINK_DELAY, 1);
	else
		led_trigger_event(&ledinternal->netfilter_led_trigger, LED_FULL);

	/* If there's a positive delay, start/update the timer */
	if (ledinfo->delay > 0) {
		mod_timer(&ledinternal->timer,
			  jiffies + msecs_to_jiffies(ledinfo->delay));

	/* Otherwise if there was no delay given, blink as fast as possible */
	} else if (ledinfo->delay == 0) {
		led_trigger_event(&ledinternal->netfilter_led_trigger, LED_OFF);
	}

	/* else the delay is negative, which means switch on and stay on */

	return XT_CONTINUE;
}

static void led_timeout_callback(struct timer_list *t)
{
	struct xt_led_info_internal *ledinternal = timer_container_of(ledinternal,
								      t,
								      timer);

	led_trigger_event(&ledinternal->netfilter_led_trigger, LED_OFF);
}

static struct xt_led_info_internal *led_trigger_lookup(const char *name)
{
	struct xt_led_info_internal *ledinternal;

	list_for_each_entry(ledinternal, &xt_led_triggers, list) {
		if (!strcmp(name, ledinternal->netfilter_led_trigger.name)) {
			return ledinternal;
		}
	}
	return NULL;
}

static int led_tg_check(const struct xt_tgchk_param *par)
{
	struct xt_led_info *ledinfo = par->targinfo;
	struct xt_led_info_internal *ledinternal;
	int err;

	/* Bail out if empty string or not a string at all. */
	if (ledinfo->id[0] == '\0' ||
	    !memchr(ledinfo->id, '\0', sizeof(ledinfo->id)))
		return -EINVAL;

	mutex_lock(&xt_led_mutex);

	ledinternal = led_trigger_lookup(ledinfo->id);
	if (ledinternal) {
		ledinternal->refcnt++;
		goto out;
	}

	err = -ENOMEM;
	ledinternal = kzalloc(sizeof(struct xt_led_info_internal), GFP_KERNEL);
	if (!ledinternal)
		goto exit_mutex_only;

	ledinternal->trigger_id = kstrdup(ledinfo->id, GFP_KERNEL);
	if (!ledinternal->trigger_id)
		goto exit_internal_alloc;

	ledinternal->refcnt = 1;
	ledinternal->netfilter_led_trigger.name = ledinternal->trigger_id;

	err = led_trigger_register(&ledinternal->netfilter_led_trigger);
	if (err) {
		pr_info_ratelimited("Trigger name is already in use.\n");
		goto exit_alloc;
	}

	/* Since the letinternal timer can be shared between multiple targets,
	 * always set it up, even if the current target does not need it
	 */
	timer_setup(&ledinternal->timer, led_timeout_callback, 0);

	list_add_tail(&ledinternal->list, &xt_led_triggers);

out:
	mutex_unlock(&xt_led_mutex);

	ledinfo->internal_data = ledinternal;

	return 0;

exit_alloc:
	kfree(ledinternal->trigger_id);

exit_internal_alloc:
	kfree(ledinternal);

exit_mutex_only:
	mutex_unlock(&xt_led_mutex);

	return err;
}

static void led_tg_destroy(const struct xt_tgdtor_param *par)
{
	const struct xt_led_info *ledinfo = par->targinfo;
	struct xt_led_info_internal *ledinternal = ledinfo->internal_data;

	mutex_lock(&xt_led_mutex);

	if (--ledinternal->refcnt) {
		mutex_unlock(&xt_led_mutex);
		return;
	}

	list_del(&ledinternal->list);

	timer_shutdown_sync(&ledinternal->timer);

	led_trigger_unregister(&ledinternal->netfilter_led_trigger);

	mutex_unlock(&xt_led_mutex);

	kfree(ledinternal->trigger_id);
	kfree(ledinternal);
}

static struct xt_target led_tg_reg[] __read_mostly = {
	{
		.name		= "LED",
		.revision	= 0,
		.family		= NFPROTO_IPV4,
		.target		= led_tg,
		.targetsize	= sizeof(struct xt_led_info),
		.usersize	= offsetof(struct xt_led_info, internal_data),
		.checkentry	= led_tg_check,
		.destroy	= led_tg_destroy,
		.me		= THIS_MODULE,
	},
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	{
		.name		= "LED",
		.revision	= 0,
		.family		= NFPROTO_IPV6,
		.target		= led_tg,
		.targetsize	= sizeof(struct xt_led_info),
		.usersize	= offsetof(struct xt_led_info, internal_data),
		.checkentry	= led_tg_check,
		.destroy	= led_tg_destroy,
		.me		= THIS_MODULE,
	},
#endif
};

static int __init led_tg_init(void)
{
	return xt_register_targets(led_tg_reg, ARRAY_SIZE(led_tg_reg));
}

static void __exit led_tg_exit(void)
{
	xt_unregister_targets(led_tg_reg, ARRAY_SIZE(led_tg_reg));
}

module_init(led_tg_init);
module_exit(led_tg_exit);
