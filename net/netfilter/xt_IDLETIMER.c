// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/net/netfilter/xt_IDLETIMER.c
 *
 * Netfilter module to trigger a timer when packet matches.
 * After timer expires a kevent will be sent.
 *
 * Copyright (C) 2004, 2010 Nokia Corporation
 * Written by Timo Teras <ext-timo.teras@nokia.com>
 *
 * Converted to x_tables and reworked for upstream inclusion
 * by Luciano Coelho <luciano.coelho@nokia.com>
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/timer.h>
#include <linux/alarmtimer.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_IDLETIMER.h>
#include <linux/kdev_t.h>
#include <linux/kobject.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>

struct idletimer_tg {
	struct list_head entry;
	struct alarm alarm;
	struct timer_list timer;
	struct work_struct work;

	struct kobject *kobj;
	struct device_attribute attr;

	unsigned int refcnt;
	u8 timer_type;
};

static LIST_HEAD(idletimer_tg_list);
static DEFINE_MUTEX(list_mutex);

static struct kobject *idletimer_tg_kobj;

static
struct idletimer_tg *__idletimer_tg_find_by_label(const char *label)
{
	struct idletimer_tg *entry;

	list_for_each_entry(entry, &idletimer_tg_list, entry) {
		if (!strcmp(label, entry->attr.attr.name))
			return entry;
	}

	return NULL;
}

static ssize_t idletimer_tg_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct idletimer_tg *timer;
	unsigned long expires = 0;
	struct timespec64 ktimespec = {};
	long time_diff = 0;

	mutex_lock(&list_mutex);

	timer =	__idletimer_tg_find_by_label(attr->attr.name);
	if (timer) {
		if (timer->timer_type & XT_IDLETIMER_ALARM) {
			ktime_t expires_alarm = alarm_expires_remaining(&timer->alarm);
			ktimespec = ktime_to_timespec64(expires_alarm);
			time_diff = ktimespec.tv_sec;
		} else {
			expires = timer->timer.expires;
			time_diff = jiffies_to_msecs(expires - jiffies) / 1000;
		}
	}

	mutex_unlock(&list_mutex);

	if (time_after(expires, jiffies) || ktimespec.tv_sec > 0)
		return sysfs_emit(buf, "%ld\n", time_diff);

	return sysfs_emit(buf, "0\n");
}

static void idletimer_tg_work(struct work_struct *work)
{
	struct idletimer_tg *timer = container_of(work, struct idletimer_tg,
						  work);

	sysfs_notify(idletimer_tg_kobj, NULL, timer->attr.attr.name);
}

static void idletimer_tg_expired(struct timer_list *t)
{
	struct idletimer_tg *timer = from_timer(timer, t, timer);

	pr_debug("timer %s expired\n", timer->attr.attr.name);

	schedule_work(&timer->work);
}

static void idletimer_tg_alarmproc(struct alarm *alarm, ktime_t now)
{
	struct idletimer_tg *timer = alarm->data;

	pr_debug("alarm %s expired\n", timer->attr.attr.name);
	schedule_work(&timer->work);
}

static int idletimer_check_sysfs_name(const char *name, unsigned int size)
{
	int ret;

	ret = xt_check_proc_name(name, size);
	if (ret < 0)
		return ret;

	if (!strcmp(name, "power") ||
	    !strcmp(name, "subsystem") ||
	    !strcmp(name, "uevent"))
		return -EINVAL;

	return 0;
}

static int idletimer_tg_create(struct idletimer_tg_info *info)
{
	int ret;

	info->timer = kzalloc(sizeof(*info->timer), GFP_KERNEL);
	if (!info->timer) {
		ret = -ENOMEM;
		goto out;
	}

	ret = idletimer_check_sysfs_name(info->label, sizeof(info->label));
	if (ret < 0)
		goto out_free_timer;

	sysfs_attr_init(&info->timer->attr.attr);
	info->timer->attr.attr.name = kstrdup(info->label, GFP_KERNEL);
	if (!info->timer->attr.attr.name) {
		ret = -ENOMEM;
		goto out_free_timer;
	}
	info->timer->attr.attr.mode = 0444;
	info->timer->attr.show = idletimer_tg_show;

	ret = sysfs_create_file(idletimer_tg_kobj, &info->timer->attr.attr);
	if (ret < 0) {
		pr_debug("couldn't add file to sysfs");
		goto out_free_attr;
	}

	list_add(&info->timer->entry, &idletimer_tg_list);

	timer_setup(&info->timer->timer, idletimer_tg_expired, 0);
	info->timer->refcnt = 1;

	INIT_WORK(&info->timer->work, idletimer_tg_work);

	mod_timer(&info->timer->timer,
		  msecs_to_jiffies(info->timeout * 1000) + jiffies);

	return 0;

out_free_attr:
	kfree(info->timer->attr.attr.name);
out_free_timer:
	kfree(info->timer);
out:
	return ret;
}

static int idletimer_tg_create_v1(struct idletimer_tg_info_v1 *info)
{
	int ret;

	info->timer = kmalloc(sizeof(*info->timer), GFP_KERNEL);
	if (!info->timer) {
		ret = -ENOMEM;
		goto out;
	}

	ret = idletimer_check_sysfs_name(info->label, sizeof(info->label));
	if (ret < 0)
		goto out_free_timer;

	sysfs_attr_init(&info->timer->attr.attr);
	info->timer->attr.attr.name = kstrdup(info->label, GFP_KERNEL);
	if (!info->timer->attr.attr.name) {
		ret = -ENOMEM;
		goto out_free_timer;
	}
	info->timer->attr.attr.mode = 0444;
	info->timer->attr.show = idletimer_tg_show;

	ret = sysfs_create_file(idletimer_tg_kobj, &info->timer->attr.attr);
	if (ret < 0) {
		pr_debug("couldn't add file to sysfs");
		goto out_free_attr;
	}

	/*  notify userspace  */
	kobject_uevent(idletimer_tg_kobj,KOBJ_ADD);

	list_add(&info->timer->entry, &idletimer_tg_list);
	pr_debug("timer type value is %u", info->timer_type);
	info->timer->timer_type = info->timer_type;
	info->timer->refcnt = 1;

	INIT_WORK(&info->timer->work, idletimer_tg_work);

	if (info->timer->timer_type & XT_IDLETIMER_ALARM) {
		ktime_t tout;
		alarm_init(&info->timer->alarm, ALARM_BOOTTIME,
			   idletimer_tg_alarmproc);
		info->timer->alarm.data = info->timer;
		tout = ktime_set(info->timeout, 0);
		alarm_start_relative(&info->timer->alarm, tout);
	} else {
		timer_setup(&info->timer->timer, idletimer_tg_expired, 0);
		mod_timer(&info->timer->timer,
				msecs_to_jiffies(info->timeout * 1000) + jiffies);
	}

	return 0;

out_free_attr:
	kfree(info->timer->attr.attr.name);
out_free_timer:
	kfree(info->timer);
out:
	return ret;
}

/*
 * The actual xt_tables plugin.
 */
static unsigned int idletimer_tg_target(struct sk_buff *skb,
					 const struct xt_action_param *par)
{
	const struct idletimer_tg_info *info = par->targinfo;

	pr_debug("resetting timer %s, timeout period %u\n",
		 info->label, info->timeout);

	mod_timer(&info->timer->timer,
		  msecs_to_jiffies(info->timeout * 1000) + jiffies);

	return XT_CONTINUE;
}

/*
 * The actual xt_tables plugin.
 */
static unsigned int idletimer_tg_target_v1(struct sk_buff *skb,
					 const struct xt_action_param *par)
{
	const struct idletimer_tg_info_v1 *info = par->targinfo;

	pr_debug("resetting timer %s, timeout period %u\n",
		 info->label, info->timeout);

	if (info->timer->timer_type & XT_IDLETIMER_ALARM) {
		ktime_t tout = ktime_set(info->timeout, 0);
		alarm_start_relative(&info->timer->alarm, tout);
	} else {
		mod_timer(&info->timer->timer,
				msecs_to_jiffies(info->timeout * 1000) + jiffies);
	}

	return XT_CONTINUE;
}

static int idletimer_tg_helper(struct idletimer_tg_info *info)
{
	if (info->timeout == 0) {
		pr_debug("timeout value is zero\n");
		return -EINVAL;
	}
	if (info->timeout >= INT_MAX / 1000) {
		pr_debug("timeout value is too big\n");
		return -EINVAL;
	}
	if (info->label[0] == '\0' ||
	    strnlen(info->label,
		    MAX_IDLETIMER_LABEL_SIZE) == MAX_IDLETIMER_LABEL_SIZE) {
		pr_debug("label is empty or not nul-terminated\n");
		return -EINVAL;
	}
	return 0;
}


static int idletimer_tg_checkentry(const struct xt_tgchk_param *par)
{
	struct idletimer_tg_info *info = par->targinfo;
	int ret;

	pr_debug("checkentry targinfo%s\n", info->label);

	ret = idletimer_tg_helper(info);
	if(ret < 0)
	{
		pr_debug("checkentry helper return invalid\n");
		return -EINVAL;
	}
	mutex_lock(&list_mutex);

	info->timer = __idletimer_tg_find_by_label(info->label);
	if (info->timer) {
		info->timer->refcnt++;
		mod_timer(&info->timer->timer,
			  msecs_to_jiffies(info->timeout * 1000) + jiffies);

		pr_debug("increased refcnt of timer %s to %u\n",
			 info->label, info->timer->refcnt);
	} else {
		ret = idletimer_tg_create(info);
		if (ret < 0) {
			pr_debug("failed to create timer\n");
			mutex_unlock(&list_mutex);
			return ret;
		}
	}

	mutex_unlock(&list_mutex);
	return 0;
}

static int idletimer_tg_checkentry_v1(const struct xt_tgchk_param *par)
{
	struct idletimer_tg_info_v1 *info = par->targinfo;
	int ret;

	pr_debug("checkentry targinfo%s\n", info->label);

	if (info->send_nl_msg)
		return -EOPNOTSUPP;

	ret = idletimer_tg_helper((struct idletimer_tg_info *)info);
	if(ret < 0)
	{
		pr_debug("checkentry helper return invalid\n");
		return -EINVAL;
	}

	if (info->timer_type > XT_IDLETIMER_ALARM) {
		pr_debug("invalid value for timer type\n");
		return -EINVAL;
	}

	mutex_lock(&list_mutex);

	info->timer = __idletimer_tg_find_by_label(info->label);
	if (info->timer) {
		if (info->timer->timer_type != info->timer_type) {
			pr_debug("Adding/Replacing rule with same label and different timer type is not allowed\n");
			mutex_unlock(&list_mutex);
			return -EINVAL;
		}

		info->timer->refcnt++;
		if (info->timer_type & XT_IDLETIMER_ALARM) {
			/* calculate remaining expiry time */
			ktime_t tout = alarm_expires_remaining(&info->timer->alarm);
			struct timespec64 ktimespec = ktime_to_timespec64(tout);

			if (ktimespec.tv_sec > 0) {
				pr_debug("time_expiry_remaining %lld\n",
					 ktimespec.tv_sec);
				alarm_start_relative(&info->timer->alarm, tout);
			}
		} else {
				mod_timer(&info->timer->timer,
					msecs_to_jiffies(info->timeout * 1000) + jiffies);
		}
		pr_debug("increased refcnt of timer %s to %u\n",
			 info->label, info->timer->refcnt);
	} else {
		ret = idletimer_tg_create_v1(info);
		if (ret < 0) {
			pr_debug("failed to create timer\n");
			mutex_unlock(&list_mutex);
			return ret;
		}
	}

	mutex_unlock(&list_mutex);
	return 0;
}

static void idletimer_tg_destroy(const struct xt_tgdtor_param *par)
{
	const struct idletimer_tg_info *info = par->targinfo;

	pr_debug("destroy targinfo %s\n", info->label);

	mutex_lock(&list_mutex);

	if (--info->timer->refcnt > 0) {
		pr_debug("decreased refcnt of timer %s to %u\n",
			 info->label, info->timer->refcnt);
		mutex_unlock(&list_mutex);
		return;
	}

	pr_debug("deleting timer %s\n", info->label);

	list_del(&info->timer->entry);
	mutex_unlock(&list_mutex);

	timer_shutdown_sync(&info->timer->timer);
	cancel_work_sync(&info->timer->work);
	sysfs_remove_file(idletimer_tg_kobj, &info->timer->attr.attr);
	kfree(info->timer->attr.attr.name);
	kfree(info->timer);
}

static void idletimer_tg_destroy_v1(const struct xt_tgdtor_param *par)
{
	const struct idletimer_tg_info_v1 *info = par->targinfo;

	pr_debug("destroy targinfo %s\n", info->label);

	mutex_lock(&list_mutex);

	if (--info->timer->refcnt > 0) {
		pr_debug("decreased refcnt of timer %s to %u\n",
			 info->label, info->timer->refcnt);
		mutex_unlock(&list_mutex);
		return;
	}

	pr_debug("deleting timer %s\n", info->label);

	list_del(&info->timer->entry);
	mutex_unlock(&list_mutex);

	if (info->timer->timer_type & XT_IDLETIMER_ALARM) {
		alarm_cancel(&info->timer->alarm);
	} else {
		timer_shutdown_sync(&info->timer->timer);
	}
	cancel_work_sync(&info->timer->work);
	sysfs_remove_file(idletimer_tg_kobj, &info->timer->attr.attr);
	kfree(info->timer->attr.attr.name);
	kfree(info->timer);
}


static struct xt_target idletimer_tg[] __read_mostly = {
	{
		.name		= "IDLETIMER",
		.family		= NFPROTO_IPV4,
		.target		= idletimer_tg_target,
		.targetsize     = sizeof(struct idletimer_tg_info),
		.usersize	= offsetof(struct idletimer_tg_info, timer),
		.checkentry	= idletimer_tg_checkentry,
		.destroy        = idletimer_tg_destroy,
		.me		= THIS_MODULE,
	},
	{
		.name		= "IDLETIMER",
		.family		= NFPROTO_IPV4,
		.revision	= 1,
		.target		= idletimer_tg_target_v1,
		.targetsize     = sizeof(struct idletimer_tg_info_v1),
		.usersize	= offsetof(struct idletimer_tg_info_v1, timer),
		.checkentry	= idletimer_tg_checkentry_v1,
		.destroy        = idletimer_tg_destroy_v1,
		.me		= THIS_MODULE,
	},
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	{
		.name		= "IDLETIMER",
		.family		= NFPROTO_IPV6,
		.target		= idletimer_tg_target,
		.targetsize     = sizeof(struct idletimer_tg_info),
		.usersize	= offsetof(struct idletimer_tg_info, timer),
		.checkentry	= idletimer_tg_checkentry,
		.destroy        = idletimer_tg_destroy,
		.me		= THIS_MODULE,
	},
	{
		.name		= "IDLETIMER",
		.family		= NFPROTO_IPV6,
		.revision	= 1,
		.target		= idletimer_tg_target_v1,
		.targetsize     = sizeof(struct idletimer_tg_info_v1),
		.usersize	= offsetof(struct idletimer_tg_info_v1, timer),
		.checkentry	= idletimer_tg_checkentry_v1,
		.destroy        = idletimer_tg_destroy_v1,
		.me		= THIS_MODULE,
	},
#endif
};

static struct class *idletimer_tg_class;

static struct device *idletimer_tg_device;

static int __init idletimer_tg_init(void)
{
	int err;

	idletimer_tg_class = class_create("xt_idletimer");
	err = PTR_ERR(idletimer_tg_class);
	if (IS_ERR(idletimer_tg_class)) {
		pr_debug("couldn't register device class\n");
		goto out;
	}

	idletimer_tg_device = device_create(idletimer_tg_class, NULL,
					    MKDEV(0, 0), NULL, "timers");
	err = PTR_ERR(idletimer_tg_device);
	if (IS_ERR(idletimer_tg_device)) {
		pr_debug("couldn't register system device\n");
		goto out_class;
	}

	idletimer_tg_kobj = &idletimer_tg_device->kobj;

	err = xt_register_targets(idletimer_tg, ARRAY_SIZE(idletimer_tg));

	if (err < 0) {
		pr_debug("couldn't register xt target\n");
		goto out_dev;
	}

	return 0;
out_dev:
	device_destroy(idletimer_tg_class, MKDEV(0, 0));
out_class:
	class_destroy(idletimer_tg_class);
out:
	return err;
}

static void __exit idletimer_tg_exit(void)
{
	xt_unregister_targets(idletimer_tg, ARRAY_SIZE(idletimer_tg));

	device_destroy(idletimer_tg_class, MKDEV(0, 0));
	class_destroy(idletimer_tg_class);
}

module_init(idletimer_tg_init);
module_exit(idletimer_tg_exit);

MODULE_AUTHOR("Timo Teras <ext-timo.teras@nokia.com>");
MODULE_AUTHOR("Luciano Coelho <luciano.coelho@nokia.com>");
MODULE_DESCRIPTION("Xtables: idle time monitor");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("ipt_IDLETIMER");
MODULE_ALIAS("ip6t_IDLETIMER");
