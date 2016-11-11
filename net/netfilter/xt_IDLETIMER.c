/*
 * linux/net/netfilter/xt_IDLETIMER.c
 *
 * Netfilter module to trigger a timer when packet matches.
 * After timer expires a kevent will be sent.
 *
 * Copyright (C) 2004, 2010 Nokia Corporation
 *
 * Written by Timo Teras <ext-timo.teras@nokia.com>
 *
 * Converted to x_tables and reworked for upstream inclusion
 * by Luciano Coelho <luciano.coelho@nokia.com>
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_IDLETIMER.h>
#include <linux/kdev_t.h>
#include <linux/kobject.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/math64.h>
#include <linux/suspend.h>
#include <linux/notifier.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/inet_sock.h>

struct idletimer_tg {
	struct list_head entry;
	struct timer_list timer;
	struct work_struct work;

	struct kobject *kobj;
	struct device_attribute attr;

	struct timespec delayed_timer_trigger;
	struct timespec last_modified_timer;
	struct timespec last_suspend_time;
	struct notifier_block pm_nb;

	int timeout;
	unsigned int refcnt;
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
		struct timespec *ts)
{
	bool state;
	struct timespec temp;
	spin_lock_bh(&timestamp_lock);
	timer->work_pending = false;
	if ((ts->tv_sec - timer->last_modified_timer.tv_sec) > timer->timeout ||
			timer->delayed_timer_trigger.tv_sec != 0) {
		state = false;
		temp.tv_sec = timer->timeout;
		temp.tv_nsec = 0;
		if (timer->delayed_timer_trigger.tv_sec != 0) {
			temp = timespec_add(timer->delayed_timer_trigger, temp);
			ts->tv_sec = temp.tv_sec;
			ts->tv_nsec = temp.tv_nsec;
			timer->delayed_timer_trigger.tv_sec = 0;
			timer->work_pending = true;
			schedule_work(&timer->work);
		} else {
			temp = timespec_add(timer->last_modified_timer, temp);
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
	struct timespec ts;
	uint64_t time_ns;
	bool state;

	res = snprintf(iface_msg, NLMSG_MAX_SIZE, "INTERFACE=%s",
		       iface);
	if (NLMSG_MAX_SIZE <= res) {
		pr_err("message too long (%d)", res);
		return;
	}

	get_monotonic_boottime(&ts);
	state = check_for_delayed_trigger(timer, &ts);
	res = snprintf(state_msg, NLMSG_MAX_SIZE, "STATE=%s",
			state ? "active" : "inactive");

	if (NLMSG_MAX_SIZE <= res) {
		pr_err("message too long (%d)", res);
		return;
	}

	if (state) {
		res = snprintf(uid_msg, NLMSG_MAX_SIZE, "UID=%u", timer->uid);
		if (NLMSG_MAX_SIZE <= res)
			pr_err("message too long (%d)", res);
	} else {
		res = snprintf(uid_msg, NLMSG_MAX_SIZE, "UID=");
		if (NLMSG_MAX_SIZE <= res)
			pr_err("message too long (%d)", res);
	}

	time_ns = timespec_to_ns(&ts);
	res = snprintf(timestamp_msg, NLMSG_MAX_SIZE, "TIME_NS=%llu", time_ns);
	if (NLMSG_MAX_SIZE <= res) {
		timestamp_msg[0] = '\0';
		pr_err("message too long (%d)", res);
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

	BUG_ON(!label);

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
	unsigned long now = jiffies;

	mutex_lock(&list_mutex);

	timer =	__idletimer_tg_find_by_label(attr->attr.name);
	if (timer)
		expires = timer->timer.expires;

	mutex_unlock(&list_mutex);

	if (time_after(expires, now))
		return sprintf(buf, "%u\n",
			       jiffies_to_msecs(expires - now) / 1000);

	if (timer->send_nl_msg)
		return sprintf(buf, "0 %d\n",
			jiffies_to_msecs(now - expires) / 1000);
	else
		return sprintf(buf, "0\n");
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
	struct timespec ts;
	unsigned long time_diff, now = jiffies;
	struct idletimer_tg *timer = container_of(notifier,
			struct idletimer_tg, pm_nb);
	if (!timer)
		return NOTIFY_DONE;
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		get_monotonic_boottime(&timer->last_suspend_time);
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
			get_monotonic_boottime(&ts);
			ts = timespec_sub(ts, timer->last_suspend_time);
			time_diff = timespec_to_jiffies(&ts);
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
	info->timer->send_nl_msg = (info->send_nl_msg == 0) ? false : true;
	info->timer->active = true;
	info->timer->timeout = info->timeout;

	info->timer->delayed_timer_trigger.tv_sec = 0;
	info->timer->delayed_timer_trigger.tv_nsec = 0;
	info->timer->work_pending = false;
	info->timer->uid = 0;
	get_monotonic_boottime(&info->timer->last_modified_timer);

	info->timer->pm_nb.notifier_call = idletimer_resume;
	ret = register_pm_notifier(&info->timer->pm_nb);
	if (ret)
		printk(KERN_WARNING "[%s] Failed to register pm notifier %d\n",
				__func__, ret);

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

static void reset_timer(const struct idletimer_tg_info *info,
			struct sk_buff *skb)
{
	unsigned long now = jiffies;
	struct idletimer_tg *timer = info->timer;
	bool timer_prev;

	spin_lock_bh(&timestamp_lock);
	timer_prev = timer->active;
	timer->active = true;
	/* timer_prev is used to guard overflow problem in time_before*/
	if (!timer_prev || time_before(timer->timer.expires, now)) {
		pr_debug("Starting Checkentry timer (Expired, Jiffies): %lu, %lu\n",
				timer->timer.expires, now);

		/* Stores the uid resposible for waking up the radio */
		if (skb && (skb->sk)) {
			timer->uid = from_kuid_munged(current_user_ns(),
					sock_i_uid(skb_to_full_sk(skb)));
		}

		/* checks if there is a pending inactive notification*/
		if (timer->work_pending)
			timer->delayed_timer_trigger = timer->last_modified_timer;
		else {
			timer->work_pending = true;
			schedule_work(&timer->work);
		}
	}

	get_monotonic_boottime(&timer->last_modified_timer);
	mod_timer(&timer->timer,
			msecs_to_jiffies(info->timeout * 1000) + now);
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

	BUG_ON(!info->timer);

	info->timer->active = true;

	if (time_before(info->timer->timer.expires, now)) {
		schedule_work(&info->timer->work);
		pr_debug("Starting timer %s (Expired, Jiffies): %lu, %lu\n",
			 info->label, info->timer->timer.expires, now);
	}

	/* TODO: Avoid modifying timers on each packet */
	reset_timer(info, skb);
	return XT_CONTINUE;
}

static int idletimer_tg_checkentry(const struct xt_tgchk_param *par)
{
	struct idletimer_tg_info *info = par->targinfo;
	int ret;

	pr_debug("checkentry targinfo %s\n", info->label);

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

	mutex_lock(&list_mutex);

	info->timer = __idletimer_tg_find_by_label(info->label);
	if (info->timer) {
		info->timer->refcnt++;
		reset_timer(info, NULL);
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

static struct xt_target idletimer_tg __read_mostly = {
	.name		= "IDLETIMER",
	.revision	= 1,
	.family		= NFPROTO_UNSPEC,
	.target		= idletimer_tg_target,
	.targetsize     = sizeof(struct idletimer_tg_info),
	.usersize	= offsetof(struct idletimer_tg_info, timer),
	.checkentry	= idletimer_tg_checkentry,
	.destroy        = idletimer_tg_destroy,
	.me		= THIS_MODULE,
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

	err =  xt_register_target(&idletimer_tg);
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
	xt_unregister_target(&idletimer_tg);

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
