/*
 * kernel/power/wakeup_reason.c
 *
 * Logs the reasons which caused the kernel to resume from
 * the suspend mode.
 *
 * Copyright (C) 2020 Google, Inc.
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/wakeup_reason.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/slab.h>

/*
 * struct wakeup_irq_node - stores data and relationships for IRQs logged as
 * either base or nested wakeup reasons during suspend/resume flow.
 * @siblings - for membership on leaf or parent IRQ lists
 * @irq      - the IRQ number
 * @irq_name - the name associated with the IRQ, or a default if none
 */
struct wakeup_irq_node {
	struct list_head siblings;
	int irq;
	const char *irq_name;
};

enum wakeup_reason_flag {
	RESUME_NONE = 0,
	RESUME_IRQ,
	RESUME_ABORT,
	RESUME_ABNORMAL,
};

static DEFINE_SPINLOCK(wakeup_reason_lock);

static LIST_HEAD(leaf_irqs);   /* kept in ascending IRQ sorted order */
static LIST_HEAD(parent_irqs); /* unordered */

static struct kmem_cache *wakeup_irq_nodes_cache;

static const char *default_irq_name = "(unnamed)";

static struct kobject *kobj;

static bool capture_reasons;
static int wakeup_reason;
static char non_irq_wake_reason[MAX_SUSPEND_ABORT_LEN];

static ktime_t last_monotime; /* monotonic time before last suspend */
static ktime_t curr_monotime; /* monotonic time after last suspend */
static ktime_t last_stime; /* monotonic boottime offset before last suspend */
static ktime_t curr_stime; /* monotonic boottime offset after last suspend */

static void init_node(struct wakeup_irq_node *p, int irq)
{
	struct irq_desc *desc;

	INIT_LIST_HEAD(&p->siblings);

	p->irq = irq;
	desc = irq_to_desc(irq);
	if (desc && desc->action && desc->action->name)
		p->irq_name = desc->action->name;
	else
		p->irq_name = default_irq_name;
}

static struct wakeup_irq_node *create_node(int irq)
{
	struct wakeup_irq_node *result;

	result = kmem_cache_alloc(wakeup_irq_nodes_cache, GFP_ATOMIC);
	if (unlikely(!result))
		pr_warn("Failed to log wakeup IRQ %d\n", irq);
	else
		init_node(result, irq);

	return result;
}

static void delete_list(struct list_head *head)
{
	struct wakeup_irq_node *n;

	while (!list_empty(head)) {
		n = list_first_entry(head, struct wakeup_irq_node, siblings);
		list_del(&n->siblings);
		kmem_cache_free(wakeup_irq_nodes_cache, n);
	}
}

static bool add_sibling_node_sorted(struct list_head *head, int irq)
{
	struct wakeup_irq_node *n = NULL;
	struct list_head *predecessor = head;

	if (unlikely(WARN_ON(!head)))
		return NULL;

	if (!list_empty(head))
		list_for_each_entry(n, head, siblings) {
			if (n->irq < irq)
				predecessor = &n->siblings;
			else if (n->irq == irq)
				return true;
			else
				break;
		}

	n = create_node(irq);
	if (n) {
		list_add(&n->siblings, predecessor);
		return true;
	}

	return false;
}

static struct wakeup_irq_node *find_node_in_list(struct list_head *head,
						 int irq)
{
	struct wakeup_irq_node *n;

	if (unlikely(WARN_ON(!head)))
		return NULL;

	list_for_each_entry(n, head, siblings)
		if (n->irq == irq)
			return n;

	return NULL;
}

void log_irq_wakeup_reason(int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);
	if (wakeup_reason == RESUME_ABNORMAL || wakeup_reason == RESUME_ABORT) {
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return;
	}

	if (!capture_reasons) {
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return;
	}

	if (find_node_in_list(&parent_irqs, irq) == NULL)
		add_sibling_node_sorted(&leaf_irqs, irq);

	wakeup_reason = RESUME_IRQ;
	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}

void log_threaded_irq_wakeup_reason(int irq, int parent_irq)
{
	struct wakeup_irq_node *parent;
	unsigned long flags;

	/*
	 * Intentionally unsynchronized.  Calls that come in after we have
	 * resumed should have a fast exit path since there's no work to be
	 * done, any any coherence issue that could cause a wrong value here is
	 * both highly improbable - given the set/clear timing - and very low
	 * impact (parent IRQ gets logged instead of the specific child).
	 */
	if (!capture_reasons)
		return;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	if (wakeup_reason == RESUME_ABNORMAL || wakeup_reason == RESUME_ABORT) {
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return;
	}

	if (!capture_reasons || (find_node_in_list(&leaf_irqs, irq) != NULL)) {
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return;
	}

	parent = find_node_in_list(&parent_irqs, parent_irq);
	if (parent != NULL)
		add_sibling_node_sorted(&leaf_irqs, irq);
	else {
		parent = find_node_in_list(&leaf_irqs, parent_irq);
		if (parent != NULL) {
			list_del_init(&parent->siblings);
			list_add_tail(&parent->siblings, &parent_irqs);
			add_sibling_node_sorted(&leaf_irqs, irq);
		}
	}

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}
EXPORT_SYMBOL_GPL(log_threaded_irq_wakeup_reason);

static void __log_abort_or_abnormal_wake(bool abort, const char *fmt,
					 va_list args)
{
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	/* Suspend abort or abnormal wake reason has already been logged. */
	if (wakeup_reason != RESUME_NONE) {
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return;
	}

	if (abort)
		wakeup_reason = RESUME_ABORT;
	else
		wakeup_reason = RESUME_ABNORMAL;

	vsnprintf(non_irq_wake_reason, MAX_SUSPEND_ABORT_LEN, fmt, args);

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}

void log_suspend_abort_reason(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	__log_abort_or_abnormal_wake(true, fmt, args);
	va_end(args);
}
EXPORT_SYMBOL_GPL(log_suspend_abort_reason);

void log_abnormal_wakeup_reason(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	__log_abort_or_abnormal_wake(false, fmt, args);
	va_end(args);
}
EXPORT_SYMBOL_GPL(log_abnormal_wakeup_reason);

void clear_wakeup_reasons(void)
{
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	delete_list(&leaf_irqs);
	delete_list(&parent_irqs);
	wakeup_reason = RESUME_NONE;
	capture_reasons = true;

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}

static void print_wakeup_sources(void)
{
	struct wakeup_irq_node *n;
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	capture_reasons = false;

	if (wakeup_reason == RESUME_ABORT) {
		pr_info("Abort: %s\n", non_irq_wake_reason);
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return;
	}

	if (wakeup_reason == RESUME_IRQ && !list_empty(&leaf_irqs))
		list_for_each_entry(n, &leaf_irqs, siblings)
			pr_info("Resume caused by IRQ %d, %s\n", n->irq,
				n->irq_name);
	else if (wakeup_reason == RESUME_ABNORMAL)
		pr_info("Resume caused by %s\n", non_irq_wake_reason);
	else
		pr_info("Resume cause unknown\n");

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}

static ssize_t last_resume_reason_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	ssize_t buf_offset = 0;
	struct wakeup_irq_node *n;
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	if (wakeup_reason == RESUME_ABORT) {
		buf_offset = scnprintf(buf, PAGE_SIZE, "Abort: %s",
				       non_irq_wake_reason);
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return buf_offset;
	}

	if (wakeup_reason == RESUME_IRQ && !list_empty(&leaf_irqs))
		list_for_each_entry(n, &leaf_irqs, siblings)
			buf_offset += scnprintf(buf + buf_offset,
						PAGE_SIZE - buf_offset,
						"%d %s\n", n->irq, n->irq_name);
	else if (wakeup_reason == RESUME_ABNORMAL)
		buf_offset = scnprintf(buf, PAGE_SIZE, "-1 %s",
				       non_irq_wake_reason);

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);

	return buf_offset;
}

static ssize_t last_suspend_time_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	struct timespec64 sleep_time;
	struct timespec64 total_time;
	struct timespec64 suspend_resume_time;

	/*
	 * total_time is calculated from monotonic bootoffsets because
	 * unlike CLOCK_MONOTONIC it include the time spent in suspend state.
	 */
	total_time = ktime_to_timespec64(ktime_sub(curr_stime, last_stime));

	/*
	 * suspend_resume_time is calculated as monotonic (CLOCK_MONOTONIC)
	 * time interval before entering suspend and post suspend.
	 */
	suspend_resume_time =
		ktime_to_timespec64(ktime_sub(curr_monotime, last_monotime));

	/* sleep_time = total_time - suspend_resume_time */
	sleep_time = timespec64_sub(total_time, suspend_resume_time);

	/* Export suspend_resume_time and sleep_time in pair here. */
	return sprintf(buf, "%llu.%09lu %llu.%09lu\n",
		       (unsigned long long)suspend_resume_time.tv_sec,
		       suspend_resume_time.tv_nsec,
		       (unsigned long long)sleep_time.tv_sec,
		       sleep_time.tv_nsec);
}

static struct kobj_attribute resume_reason = __ATTR_RO(last_resume_reason);
static struct kobj_attribute suspend_time = __ATTR_RO(last_suspend_time);

static struct attribute *attrs[] = {
	&resume_reason.attr,
	&suspend_time.attr,
	NULL,
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};

/* Detects a suspend and clears all the previous wake up reasons*/
static int wakeup_reason_pm_event(struct notifier_block *notifier,
		unsigned long pm_event, void *unused)
{
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		/* monotonic time since boot */
		last_monotime = ktime_get();
		/* monotonic time since boot including the time spent in suspend */
		last_stime = ktime_get_boottime();
		clear_wakeup_reasons();
		break;
	case PM_POST_SUSPEND:
		/* monotonic time since boot */
		curr_monotime = ktime_get();
		/* monotonic time since boot including the time spent in suspend */
		curr_stime = ktime_get_boottime();
		print_wakeup_sources();
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block wakeup_reason_pm_notifier_block = {
	.notifier_call = wakeup_reason_pm_event,
};

static int __init wakeup_reason_init(void)
{
	if (register_pm_notifier(&wakeup_reason_pm_notifier_block)) {
		pr_warn("[%s] failed to register PM notifier\n", __func__);
		goto fail;
	}

	kobj = kobject_create_and_add("wakeup_reasons", kernel_kobj);
	if (!kobj) {
		pr_warn("[%s] failed to create a sysfs kobject\n", __func__);
		goto fail_unregister_pm_notifier;
	}

	if (sysfs_create_group(kobj, &attr_group)) {
		pr_warn("[%s] failed to create a sysfs group\n", __func__);
		goto fail_kobject_put;
	}

	wakeup_irq_nodes_cache =
		kmem_cache_create("wakeup_irq_node_cache",
				  sizeof(struct wakeup_irq_node), 0, 0, NULL);
	if (!wakeup_irq_nodes_cache)
		goto fail_remove_group;

	return 0;

fail_remove_group:
	sysfs_remove_group(kobj, &attr_group);
fail_kobject_put:
	kobject_put(kobj);
fail_unregister_pm_notifier:
	unregister_pm_notifier(&wakeup_reason_pm_notifier_block);
fail:
	return 1;
}

late_initcall(wakeup_reason_init);
