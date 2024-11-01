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
#include <linux/suspend.h>
#include <net/sock.h>
#include <net/inet_sock.h>

#define NLMSG_MAX_SIZE 64

struct idletimer_tg {
	struct list_head entry;
	struct alarm alarm;
	struct timer_list timer;
	struct work_struct work;

	struct kobject *kobj;
	struct device_attribute attr;

	struct timespec64 delayed_timer_trigger;
	struct timespec64 last_modified_timer;
	struct timespec64 last_suspend_time;
	struct notifier_block pm_nb;

	int timeout;
	unsigned int refcnt;
	u8 timer_type;

	bool work_pending;
	bool send_nl_msg;
	bool active;
	uid_t uid;
	bool suspend_time_valid;
};

static LIST_HEAD(idletimer_tg_list);
static DEFINE_MUTEX(list_mutex);
static DEFINE_SPINLOCK(timestamp_lock);

static struct kobject *idletimer_tg_kobj;

static bool check_for_delayed_trigger(struct idletimer_tg *timer,
				      struct timespec64 *ts)
{
	bool state;
	struct timespec64 temp;
	spin_lock_bh(&timestamp_lock);
	timer->work_pending = false;
	if ((ts->tv_sec - timer->last_modified_timer.tv_sec) > timer->timeout ||
	    timer->delayed_timer_trigger.tv_sec != 0) {
		state = false;
		temp.tv_sec = timer->timeout;
		temp.tv_nsec = 0;
		if (timer->delayed_timer_trigger.tv_sec != 0) {
			temp = timespec64_add(timer->delayed_timer_trigger,
					      temp);
			ts->tv_sec = temp.tv_sec;
			ts->tv_nsec = temp.tv_nsec;
			timer->delayed_timer_trigger.tv_sec = 0;
			timer->work_pending = true;
			schedule_work(&timer->work);
		} else {
			temp = timespec64_add(timer->last_modified_timer, temp);
			ts->tv_sec = temp.tv_sec;
			ts->tv_nsec = temp.tv_nsec;
		}
	} else {
		state = timer->active;
	}
	spin_unlock_bh(&timestamp_lock);
	return state;
}

static void notify_netlink_uevent(const char *iface, struct idletimer_tg *timer)
{
	char iface_msg[NLMSG_MAX_SIZE];
	char state_msg[NLMSG_MAX_SIZE];
	char timestamp_msg[NLMSG_MAX_SIZE];
	char uid_msg[NLMSG_MAX_SIZE];
	char *envp[] = { iface_msg, state_msg, timestamp_msg, uid_msg, NULL };
	int res;
	struct timespec64 ts;
	u64 time_ns;
	bool state;

	res = snprintf(iface_msg, NLMSG_MAX_SIZE, "INTERFACE=%s",
		       iface);
	if (NLMSG_MAX_SIZE <= res) {
		pr_err("message too long (%d)\n", res);
		return;
	}

	ts = ktime_to_timespec64(ktime_get_boottime());
	state = check_for_delayed_trigger(timer, &ts);
	res = snprintf(state_msg, NLMSG_MAX_SIZE, "STATE=%s",
		       state ? "active" : "inactive");

	if (NLMSG_MAX_SIZE <= res) {
		pr_err("message too long (%d)\n", res);
		return;
	}

	if (state) {
		res = snprintf(uid_msg, NLMSG_MAX_SIZE, "UID=%u", timer->uid);
		if (NLMSG_MAX_SIZE <= res)
			pr_err("message too long (%d)\n", res);
	} else {
		res = snprintf(uid_msg, NLMSG_MAX_SIZE, "UID=");
		if (NLMSG_MAX_SIZE <= res)
			pr_err("message too long (%d)\n", res);
	}

	time_ns = timespec64_to_ns(&ts);
	res = snprintf(timestamp_msg, NLMSG_MAX_SIZE, "TIME_NS=%llu", time_ns);
	if (NLMSG_MAX_SIZE <= res) {
		timestamp_msg[0] = '\0';
		pr_err("message too long (%d)\n", res);
	}

	pr_debug("putting nlmsg: <%s> <%s> <%s> <%s>\n", iface_msg, state_msg,
		 timestamp_msg, uid_msg);
	kobject_uevent_env(idletimer_tg_kobj, KOBJ_CHANGE, envp);
	return;
}

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
	unsigned long now = jiffies;

	mutex_lock(&list_mutex);

	timer =	__idletimer_tg_find_by_label(attr->attr.name);
	if (timer) {
		if (timer->timer_type & XT_IDLETIMER_ALARM) {
			ktime_t expires_alarm = alarm_expires_remaining(&timer->alarm);
			ktimespec = ktime_to_timespec64(expires_alarm);
			time_diff = ktimespec.tv_sec;
		} else {
			expires = timer->timer.expires;
			time_diff = jiffies_to_msecs(expires - now) / 1000;
		}
	}

	mutex_unlock(&list_mutex);

	if (time_after(expires, now) || ktimespec.tv_sec > 0)
		return sysfs_emit(buf, "%ld\n", time_diff);

	if (timer->send_nl_msg)
		return sysfs_emit(buf, "0 %d\n",
				  jiffies_to_msecs(now - expires) / 1000);

	return sysfs_emit(buf, "0\n");
}

static void idletimer_tg_work(struct work_struct *work)
{
	struct idletimer_tg *timer = container_of(work, struct idletimer_tg,
						  work);

	sysfs_notify(idletimer_tg_kobj, NULL, timer->attr.attr.name);

	if (timer->send_nl_msg)
		notify_netlink_uevent(timer->attr.attr.name, timer);
}

static void idletimer_tg_expired(struct timer_list *t)
{
	struct idletimer_tg *timer = from_timer(timer, t, timer);

	pr_debug("timer %s expired\n", timer->attr.attr.name);

	spin_lock_bh(&timestamp_lock);
	timer->active = false;
	timer->work_pending = true;
	schedule_work(&timer->work);
	spin_unlock_bh(&timestamp_lock);
}

static int idletimer_resume(struct notifier_block *notifier,
			    unsigned long pm_event, void *unused)
{
	struct timespec64 ts;
	unsigned long time_diff, now = jiffies;
	struct idletimer_tg *timer = container_of(notifier,
						  struct idletimer_tg, pm_nb);
	if (!timer)
		return NOTIFY_DONE;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		timer->last_suspend_time =
			ktime_to_timespec64(ktime_get_boottime());
		timer->suspend_time_valid = true;
		break;
	case PM_POST_SUSPEND:
		if (!timer->suspend_time_valid)
			break;
		timer->suspend_time_valid = false;

		spin_lock_bh(&timestamp_lock);
		if (!timer->active) {
			spin_unlock_bh(&timestamp_lock);
			break;
		}
		/* since jiffies are not updated when suspended now represents
		 * the time it would have suspended */
		if (time_after(timer->timer.expires, now)) {
			ts = ktime_to_timespec64(ktime_get_boottime());
			ts = timespec64_sub(ts, timer->last_suspend_time);
			time_diff = timespec64_to_jiffies(&ts);
			if (timer->timer.expires > (time_diff + now)) {
				mod_timer_pending(&timer->timer,
						  (timer->timer.expires - time_diff));
			} else {
				del_timer(&timer->timer);
				timer->timer.expires = 0;
				timer->active = false;
				timer->work_pending = true;
				schedule_work(&timer->work);
			}
		}
		spin_unlock_bh(&timestamp_lock);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static enum alarmtimer_restart idletimer_tg_alarmproc(struct alarm *alarm,
							  ktime_t now)
{
	struct idletimer_tg *timer = alarm->data;

	pr_debug("alarm %s expired\n", timer->attr.attr.name);
	schedule_work(&timer->work);
	return ALARMTIMER_NORESTART;
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
		pr_debug("couldn't add file to sysfs\n");
		goto out_free_attr;
	}

	list_add(&info->timer->entry, &idletimer_tg_list);
	pr_debug("timer type value is 0.\n");
	info->timer->timer_type = 0;
	info->timer->refcnt = 1;
	info->timer->send_nl_msg = false;
	info->timer->active = true;
	info->timer->timeout = info->timeout;

	info->timer->delayed_timer_trigger.tv_sec = 0;
	info->timer->delayed_timer_trigger.tv_nsec = 0;
	info->timer->work_pending = false;
	info->timer->uid = 0;
	info->timer->last_modified_timer =
		ktime_to_timespec64(ktime_get_boottime());

	info->timer->pm_nb.notifier_call = idletimer_resume;
	ret = register_pm_notifier(&info->timer->pm_nb);
	if (ret)
		printk(KERN_WARNING "[%s] Failed to register pm notifier %d\n",
		       __func__, ret);

	INIT_WORK(&info->timer->work, idletimer_tg_work);

	timer_setup(&info->timer->timer, idletimer_tg_expired, 0);
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
		pr_debug("couldn't add file to sysfs\n");
		goto out_free_attr;
	}

	/*  notify userspace  */
	kobject_uevent(idletimer_tg_kobj,KOBJ_ADD);

	list_add(&info->timer->entry, &idletimer_tg_list);
	pr_debug("timer type value is %u\n", info->timer_type);
	info->timer->timer_type = info->timer_type;
	info->timer->refcnt = 1;
	info->timer->send_nl_msg = (info->send_nl_msg != 0);
	info->timer->active = true;
	info->timer->timeout = info->timeout;

	info->timer->delayed_timer_trigger.tv_sec = 0;
	info->timer->delayed_timer_trigger.tv_nsec = 0;
	info->timer->work_pending = false;
	info->timer->uid = 0;
	info->timer->last_modified_timer =
		ktime_to_timespec64(ktime_get_boottime());

	info->timer->pm_nb.notifier_call = idletimer_resume;
	ret = register_pm_notifier(&info->timer->pm_nb);
	if (ret)
		printk(KERN_WARNING "[%s] Failed to register pm notifier %d\n",
		       __func__, ret);

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

static void reset_timer(struct idletimer_tg * const info_timer,
			const __u32 info_timeout,
			struct sk_buff *skb)
{
	unsigned long now = jiffies;
	bool timer_prev;

	spin_lock_bh(&timestamp_lock);
	timer_prev = info_timer->active;
	info_timer->active = true;
	/* timer_prev is used to guard overflow problem in time_before*/
	if (!timer_prev || time_before(info_timer->timer.expires, now)) {
		pr_debug("Starting Checkentry timer (Expired, Jiffies): %lu, %lu\n",
			 info_timer->timer.expires, now);

		/* Stores the uid resposible for waking up the radio */
		if (skb && (skb->sk)) {
			info_timer->uid = from_kuid_munged(current_user_ns(),
							   sock_i_uid(skb_to_full_sk(skb)));
		}

		/* checks if there is a pending inactive notification*/
		if (info_timer->work_pending)
			info_timer->delayed_timer_trigger = info_timer->last_modified_timer;
		else {
			info_timer->work_pending = true;
			schedule_work(&info_timer->work);
		}
	}

	info_timer->last_modified_timer = ktime_to_timespec64(ktime_get_boottime());
	mod_timer(&info_timer->timer, msecs_to_jiffies(info_timeout * 1000) + now);
	spin_unlock_bh(&timestamp_lock);
}

/*
 * The actual xt_tables plugin.
 */
static unsigned int idletimer_tg_target(struct sk_buff *skb,
					 const struct xt_action_param *par)
{
	const struct idletimer_tg_info *info = par->targinfo;
	unsigned long now = jiffies;

	pr_debug("resetting timer %s, timeout period %u\n",
		 info->label, info->timeout);

	info->timer->active = true;

	if (time_before(info->timer->timer.expires, now)) {
		schedule_work(&info->timer->work);
		pr_debug("Starting timer %s (Expired, Jiffies): %lu, %lu\n",
			 info->label, info->timer->timer.expires, now);
	}

	/* TODO: Avoid modifying timers on each packet */
	reset_timer(info->timer, info->timeout, skb);

	return XT_CONTINUE;
}

/*
 * The actual xt_tables plugin.
 */
static unsigned int idletimer_tg_target_v1(struct sk_buff *skb,
					 const struct xt_action_param *par)
{
	const struct idletimer_tg_info_v1 *info = par->targinfo;
	unsigned long now = jiffies;

	pr_debug("resetting timer %s, timeout period %u\n",
		 info->label, info->timeout);

	if (info->timer->timer_type & XT_IDLETIMER_ALARM) {
		ktime_t tout = ktime_set(info->timeout, 0);
		alarm_start_relative(&info->timer->alarm, tout);
	} else {
		info->timer->active = true;

		if (time_before(info->timer->timer.expires, now)) {
			schedule_work(&info->timer->work);
			pr_debug("Starting timer %s (Expired, Jiffies): %lu, %lu\n",
				 info->label, info->timer->timer.expires, now);
		}

		/* TODO: Avoid modifying timers on each packet */
		reset_timer(info->timer, info->timeout, skb);
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
		reset_timer(info->timer, info->timeout, NULL);
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

	if (info->send_nl_msg > 1) {
		pr_debug("invalid value for send_nl_msg\n");
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
			reset_timer(info->timer, info->timeout, NULL);
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

	if (--info->timer->refcnt == 0) {
		pr_debug("deleting timer %s\n", info->label);

		list_del(&info->timer->entry);
		del_timer_sync(&info->timer->timer);
		sysfs_remove_file(idletimer_tg_kobj, &info->timer->attr.attr);
		unregister_pm_notifier(&info->timer->pm_nb);
		cancel_work_sync(&info->timer->work);
		kfree(info->timer->attr.attr.name);
		kfree(info->timer);
	} else {
		pr_debug("decreased refcnt of timer %s to %u\n",
			 info->label, info->timer->refcnt);
	}

	mutex_unlock(&list_mutex);
}

static void idletimer_tg_destroy_v1(const struct xt_tgdtor_param *par)
{
	const struct idletimer_tg_info_v1 *info = par->targinfo;

	pr_debug("destroy targinfo %s\n", info->label);

	mutex_lock(&list_mutex);

	if (--info->timer->refcnt == 0) {
		pr_debug("deleting timer %s\n", info->label);

		list_del(&info->timer->entry);
		if (info->timer->timer_type & XT_IDLETIMER_ALARM) {
			alarm_cancel(&info->timer->alarm);
		} else {
			del_timer_sync(&info->timer->timer);
		}
		sysfs_remove_file(idletimer_tg_kobj, &info->timer->attr.attr);
		unregister_pm_notifier(&info->timer->pm_nb);
		cancel_work_sync(&info->timer->work);
		kfree(info->timer->attr.attr.name);
		kfree(info->timer);
	} else {
		pr_debug("decreased refcnt of timer %s to %u\n",
			 info->label, info->timer->refcnt);
	}

	mutex_unlock(&list_mutex);
}


static struct xt_target idletimer_tg[] __read_mostly = {
	{
	.name		= "IDLETIMER",
	.family		= NFPROTO_UNSPEC,
	.target		= idletimer_tg_target,
	.targetsize     = sizeof(struct idletimer_tg_info),
	.usersize	= offsetof(struct idletimer_tg_info, timer),
	.checkentry	= idletimer_tg_checkentry,
	.destroy        = idletimer_tg_destroy,
	.me		= THIS_MODULE,
	},
	{
	.name		= "IDLETIMER",
	.family		= NFPROTO_UNSPEC,
	.revision	= 1,
	.target		= idletimer_tg_target_v1,
	.targetsize     = sizeof(struct idletimer_tg_info_v1),
	.usersize	= offsetof(struct idletimer_tg_info_v1, timer),
	.checkentry	= idletimer_tg_checkentry_v1,
	.destroy        = idletimer_tg_destroy_v1,
	.me		= THIS_MODULE,
	},


};

static struct class *idletimer_tg_class;

static struct device *idletimer_tg_device;

static int __init idletimer_tg_init(void)
{
	int err;

	idletimer_tg_class = class_create(THIS_MODULE, "xt_idletimer");
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
MODULE_ALIAS("arpt_IDLETIMER");
