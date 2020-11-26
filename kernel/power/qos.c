// SPDX-License-Identifier: GPL-2.0-only
/*
 * Power Management Quality of Service (PM QoS) support base.
 *
 * Copyright (C) 2020 Intel Corporation
 *
 * Authors:
 *	Mark Gross <mgross@linux.intel.com>
 *	Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * Provided here is an interface for specifying PM QoS dependencies.  It allows
 * entities depending on QoS constraints to register their requests which are
 * aggregated as appropriate to produce effective constraints (target values)
 * that can be monitored by entities needing to respect them, either by polling
 * or through a built-in notification mechanism.
 *
 * In addition to the basic functionality, more specific interfaces for managing
 * global CPU latency QoS requests and frequency QoS requests are provided.
 */

/*#define DEBUG*/

#include <linux/pm_qos.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <linux/uaccess.h>
#include <linux/export.h>
#include <trace/events/power.h>

/*
 * locking rule: all changes to constraints or notifiers lists
 * or pm_qos_object list and pm_qos_objects need to happen with pm_qos_lock
 * held, taken with _irqsave.  One lock to rule them all
 */
static DEFINE_SPINLOCK(pm_qos_lock);

/**
 * pm_qos_read_value - Return the current effective constraint value.
 * @c: List of PM QoS constraint requests.
 */
s32 pm_qos_read_value(struct pm_qos_constraints *c)
{
	return READ_ONCE(c->target_value);
}

static int pm_qos_get_value(struct pm_qos_constraints *c)
{
	if (plist_head_empty(&c->list))
		return c->no_constraint_value;

	switch (c->type) {
	case PM_QOS_MIN:
		return plist_first(&c->list)->prio;

	case PM_QOS_MAX:
		return plist_last(&c->list)->prio;

	default:
		WARN(1, "Unknown PM QoS type in %s\n", __func__);
		return PM_QOS_DEFAULT_VALUE;
	}
}

static void pm_qos_set_value(struct pm_qos_constraints *c, s32 value)
{
	WRITE_ONCE(c->target_value, value);
}

/**
 * pm_qos_update_target - Update a list of PM QoS constraint requests.
 * @c: List of PM QoS requests.
 * @node: Target list entry.
 * @action: Action to carry out (add, update or remove).
 * @value: New request value for the target list entry.
 *
 * Update the given list of PM QoS constraint requests, @c, by carrying an
 * @action involving the @node list entry and @value on it.
 *
 * The recognized values of @action are PM_QOS_ADD_REQ (store @value in @node
 * and add it to the list), PM_QOS_UPDATE_REQ (remove @node from the list, store
 * @value in it and add it to the list again), and PM_QOS_REMOVE_REQ (remove
 * @node from the list, ignore @value).
 *
 * Return: 1 if the aggregate constraint value has changed, 0  otherwise.
 */
int pm_qos_update_target(struct pm_qos_constraints *c, struct plist_node *node,
			 enum pm_qos_req_action action, int value)
{
	int prev_value, curr_value, new_value;
	unsigned long flags;

	spin_lock_irqsave(&pm_qos_lock, flags);

	prev_value = pm_qos_get_value(c);
	if (value == PM_QOS_DEFAULT_VALUE)
		new_value = c->default_value;
	else
		new_value = value;

	switch (action) {
	case PM_QOS_REMOVE_REQ:
		plist_del(node, &c->list);
		break;
	case PM_QOS_UPDATE_REQ:
		/*
		 * To change the list, atomically remove, reinit with new value
		 * and add, then see if the aggregate has changed.
		 */
		plist_del(node, &c->list);
		fallthrough;
	case PM_QOS_ADD_REQ:
		plist_node_init(node, new_value);
		plist_add(node, &c->list);
		break;
	default:
		/* no action */
		;
	}

	curr_value = pm_qos_get_value(c);
	pm_qos_set_value(c, curr_value);

	spin_unlock_irqrestore(&pm_qos_lock, flags);

	trace_pm_qos_update_target(action, prev_value, curr_value);

	if (prev_value == curr_value)
		return 0;

	if (c->notifiers)
		blocking_notifier_call_chain(c->notifiers, curr_value, NULL);

	return 1;
}

/**
 * pm_qos_flags_remove_req - Remove device PM QoS flags request.
 * @pqf: Device PM QoS flags set to remove the request from.
 * @req: Request to remove from the set.
 */
static void pm_qos_flags_remove_req(struct pm_qos_flags *pqf,
				    struct pm_qos_flags_request *req)
{
	s32 val = 0;

	list_del(&req->node);
	list_for_each_entry(req, &pqf->list, node)
		val |= req->flags;

	pqf->effective_flags = val;
}

/**
 * pm_qos_update_flags - Update a set of PM QoS flags.
 * @pqf: Set of PM QoS flags to update.
 * @req: Request to add to the set, to modify, or to remove from the set.
 * @action: Action to take on the set.
 * @val: Value of the request to add or modify.
 *
 * Return: 1 if the aggregate constraint value has changed, 0 otherwise.
 */
bool pm_qos_update_flags(struct pm_qos_flags *pqf,
			 struct pm_qos_flags_request *req,
			 enum pm_qos_req_action action, s32 val)
{
	unsigned long irqflags;
	s32 prev_value, curr_value;

	spin_lock_irqsave(&pm_qos_lock, irqflags);

	prev_value = list_empty(&pqf->list) ? 0 : pqf->effective_flags;

	switch (action) {
	case PM_QOS_REMOVE_REQ:
		pm_qos_flags_remove_req(pqf, req);
		break;
	case PM_QOS_UPDATE_REQ:
		pm_qos_flags_remove_req(pqf, req);
		fallthrough;
	case PM_QOS_ADD_REQ:
		req->flags = val;
		INIT_LIST_HEAD(&req->node);
		list_add_tail(&req->node, &pqf->list);
		pqf->effective_flags |= val;
		break;
	default:
		/* no action */
		;
	}

	curr_value = list_empty(&pqf->list) ? 0 : pqf->effective_flags;

	spin_unlock_irqrestore(&pm_qos_lock, irqflags);

	trace_pm_qos_update_flags(action, prev_value, curr_value);

	return prev_value != curr_value;
}

#ifdef CONFIG_CPU_IDLE
/* Definitions related to the CPU latency QoS. */

static struct pm_qos_constraints cpu_latency_constraints = {
	.list = PLIST_HEAD_INIT(cpu_latency_constraints.list),
	.target_value = PM_QOS_CPU_LATENCY_DEFAULT_VALUE,
	.default_value = PM_QOS_CPU_LATENCY_DEFAULT_VALUE,
	.no_constraint_value = PM_QOS_CPU_LATENCY_DEFAULT_VALUE,
	.type = PM_QOS_MIN,
};

/**
 * cpu_latency_qos_limit - Return current system-wide CPU latency QoS limit.
 */
s32 cpu_latency_qos_limit(void)
{
	return pm_qos_read_value(&cpu_latency_constraints);
}

/**
 * cpu_latency_qos_request_active - Check the given PM QoS request.
 * @req: PM QoS request to check.
 *
 * Return: 'true' if @req has been added to the CPU latency QoS list, 'false'
 * otherwise.
 */
bool cpu_latency_qos_request_active(struct pm_qos_request *req)
{
	return req->qos == &cpu_latency_constraints;
}
EXPORT_SYMBOL_GPL(cpu_latency_qos_request_active);

static void cpu_latency_qos_apply(struct pm_qos_request *req,
				  enum pm_qos_req_action action, s32 value)
{
	int ret = pm_qos_update_target(req->qos, &req->node, action, value);
	if (ret > 0)
		wake_up_all_idle_cpus();
}

/**
 * cpu_latency_qos_add_request - Add new CPU latency QoS request.
 * @req: Pointer to a preallocated handle.
 * @value: Requested constraint value.
 *
 * Use @value to initialize the request handle pointed to by @req, insert it as
 * a new entry to the CPU latency QoS list and recompute the effective QoS
 * constraint for that list.
 *
 * Callers need to save the handle for later use in updates and removal of the
 * QoS request represented by it.
 */
void cpu_latency_qos_add_request(struct pm_qos_request *req, s32 value)
{
	if (!req)
		return;

	if (cpu_latency_qos_request_active(req)) {
		WARN(1, KERN_ERR "%s called for already added request\n", __func__);
		return;
	}

	trace_pm_qos_add_request(value);

	req->qos = &cpu_latency_constraints;
	cpu_latency_qos_apply(req, PM_QOS_ADD_REQ, value);
}
EXPORT_SYMBOL_GPL(cpu_latency_qos_add_request);

/**
 * cpu_latency_qos_update_request - Modify existing CPU latency QoS request.
 * @req : QoS request to update.
 * @new_value: New requested constraint value.
 *
 * Use @new_value to update the QoS request represented by @req in the CPU
 * latency QoS list along with updating the effective constraint value for that
 * list.
 */
void cpu_latency_qos_update_request(struct pm_qos_request *req, s32 new_value)
{
	if (!req)
		return;

	if (!cpu_latency_qos_request_active(req)) {
		WARN(1, KERN_ERR "%s called for unknown object\n", __func__);
		return;
	}

	trace_pm_qos_update_request(new_value);

	if (new_value == req->node.prio)
		return;

	cpu_latency_qos_apply(req, PM_QOS_UPDATE_REQ, new_value);
}
EXPORT_SYMBOL_GPL(cpu_latency_qos_update_request);

/**
 * cpu_latency_qos_remove_request - Remove existing CPU latency QoS request.
 * @req: QoS request to remove.
 *
 * Remove the CPU latency QoS request represented by @req from the CPU latency
 * QoS list along with updating the effective constraint value for that list.
 */
void cpu_latency_qos_remove_request(struct pm_qos_request *req)
{
	if (!req)
		return;

	if (!cpu_latency_qos_request_active(req)) {
		WARN(1, KERN_ERR "%s called for unknown object\n", __func__);
		return;
	}

	trace_pm_qos_remove_request(PM_QOS_DEFAULT_VALUE);

	cpu_latency_qos_apply(req, PM_QOS_REMOVE_REQ, PM_QOS_DEFAULT_VALUE);
	memset(req, 0, sizeof(*req));
}
EXPORT_SYMBOL_GPL(cpu_latency_qos_remove_request);

/* User space interface to the CPU latency QoS via misc device. */

static int cpu_latency_qos_open(struct inode *inode, struct file *filp)
{
	struct pm_qos_request *req;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	cpu_latency_qos_add_request(req, PM_QOS_DEFAULT_VALUE);
	filp->private_data = req;

	return 0;
}

static int cpu_latency_qos_release(struct inode *inode, struct file *filp)
{
	struct pm_qos_request *req = filp->private_data;

	filp->private_data = NULL;

	cpu_latency_qos_remove_request(req);
	kfree(req);

	return 0;
}

static ssize_t cpu_latency_qos_read(struct file *filp, char __user *buf,
				    size_t count, loff_t *f_pos)
{
	struct pm_qos_request *req = filp->private_data;
	unsigned long flags;
	s32 value;

	if (!req || !cpu_latency_qos_request_active(req))
		return -EINVAL;

	spin_lock_irqsave(&pm_qos_lock, flags);
	value = pm_qos_get_value(&cpu_latency_constraints);
	spin_unlock_irqrestore(&pm_qos_lock, flags);

	return simple_read_from_buffer(buf, count, f_pos, &value, sizeof(s32));
}

static ssize_t cpu_latency_qos_write(struct file *filp, const char __user *buf,
				     size_t count, loff_t *f_pos)
{
	s32 value;

	if (count == sizeof(s32)) {
		if (copy_from_user(&value, buf, sizeof(s32)))
			return -EFAULT;
	} else {
		int ret;

		ret = kstrtos32_from_user(buf, count, 16, &value);
		if (ret)
			return ret;
	}

	cpu_latency_qos_update_request(filp->private_data, value);

	return count;
}

static const struct file_operations cpu_latency_qos_fops = {
	.write = cpu_latency_qos_write,
	.read = cpu_latency_qos_read,
	.open = cpu_latency_qos_open,
	.release = cpu_latency_qos_release,
	.llseek = noop_llseek,
};

static struct miscdevice cpu_latency_qos_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "cpu_dma_latency",
	.fops = &cpu_latency_qos_fops,
};

static int __init cpu_latency_qos_init(void)
{
	int ret;

	ret = misc_register(&cpu_latency_qos_miscdev);
	if (ret < 0)
		pr_err("%s: %s setup failed\n", __func__,
		       cpu_latency_qos_miscdev.name);

	return ret;
}
late_initcall(cpu_latency_qos_init);
#endif /* CONFIG_CPU_IDLE */

/* Definitions related to the frequency QoS below. */

/**
 * freq_constraints_init - Initialize frequency QoS constraints.
 * @qos: Frequency QoS constraints to initialize.
 */
void freq_constraints_init(struct freq_constraints *qos)
{
	struct pm_qos_constraints *c;

	c = &qos->min_freq;
	plist_head_init(&c->list);
	c->target_value = FREQ_QOS_MIN_DEFAULT_VALUE;
	c->default_value = FREQ_QOS_MIN_DEFAULT_VALUE;
	c->no_constraint_value = FREQ_QOS_MIN_DEFAULT_VALUE;
	c->type = PM_QOS_MAX;
	c->notifiers = &qos->min_freq_notifiers;
	BLOCKING_INIT_NOTIFIER_HEAD(c->notifiers);

	c = &qos->max_freq;
	plist_head_init(&c->list);
	c->target_value = FREQ_QOS_MAX_DEFAULT_VALUE;
	c->default_value = FREQ_QOS_MAX_DEFAULT_VALUE;
	c->no_constraint_value = FREQ_QOS_MAX_DEFAULT_VALUE;
	c->type = PM_QOS_MIN;
	c->notifiers = &qos->max_freq_notifiers;
	BLOCKING_INIT_NOTIFIER_HEAD(c->notifiers);
}

/**
 * freq_qos_read_value - Get frequency QoS constraint for a given list.
 * @qos: Constraints to evaluate.
 * @type: QoS request type.
 */
s32 freq_qos_read_value(struct freq_constraints *qos,
			enum freq_qos_req_type type)
{
	s32 ret;

	switch (type) {
	case FREQ_QOS_MIN:
		ret = IS_ERR_OR_NULL(qos) ?
			FREQ_QOS_MIN_DEFAULT_VALUE :
			pm_qos_read_value(&qos->min_freq);
		break;
	case FREQ_QOS_MAX:
		ret = IS_ERR_OR_NULL(qos) ?
			FREQ_QOS_MAX_DEFAULT_VALUE :
			pm_qos_read_value(&qos->max_freq);
		break;
	default:
		WARN_ON(1);
		ret = 0;
	}

	return ret;
}

/**
 * freq_qos_apply - Add/modify/remove frequency QoS request.
 * @req: Constraint request to apply.
 * @action: Action to perform (add/update/remove).
 * @value: Value to assign to the QoS request.
 *
 * This is only meant to be called from inside pm_qos, not drivers.
 */
int freq_qos_apply(struct freq_qos_request *req,
			  enum pm_qos_req_action action, s32 value)
{
	int ret;

	switch(req->type) {
	case FREQ_QOS_MIN:
		ret = pm_qos_update_target(&req->qos->min_freq, &req->pnode,
					   action, value);
		break;
	case FREQ_QOS_MAX:
		ret = pm_qos_update_target(&req->qos->max_freq, &req->pnode,
					   action, value);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/**
 * freq_qos_add_request - Insert new frequency QoS request into a given list.
 * @qos: Constraints to update.
 * @req: Preallocated request object.
 * @type: Request type.
 * @value: Request value.
 *
 * Insert a new entry into the @qos list of requests, recompute the effective
 * QoS constraint value for that list and initialize the @req object.  The
 * caller needs to save that object for later use in updates and removal.
 *
 * Return 1 if the effective constraint value has changed, 0 if the effective
 * constraint value has not changed, or a negative error code on failures.
 */
int freq_qos_add_request(struct freq_constraints *qos,
			 struct freq_qos_request *req,
			 enum freq_qos_req_type type, s32 value)
{
	int ret;

	if (IS_ERR_OR_NULL(qos) || !req)
		return -EINVAL;

	if (WARN(freq_qos_request_active(req),
		 "%s() called for active request\n", __func__))
		return -EINVAL;

	req->qos = qos;
	req->type = type;
	ret = freq_qos_apply(req, PM_QOS_ADD_REQ, value);
	if (ret < 0) {
		req->qos = NULL;
		req->type = 0;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(freq_qos_add_request);

/**
 * freq_qos_update_request - Modify existing frequency QoS request.
 * @req: Request to modify.
 * @new_value: New request value.
 *
 * Update an existing frequency QoS request along with the effective constraint
 * value for the list of requests it belongs to.
 *
 * Return 1 if the effective constraint value has changed, 0 if the effective
 * constraint value has not changed, or a negative error code on failures.
 */
int freq_qos_update_request(struct freq_qos_request *req, s32 new_value)
{
	if (!req)
		return -EINVAL;

	if (WARN(!freq_qos_request_active(req),
		 "%s() called for unknown object\n", __func__))
		return -EINVAL;

	if (req->pnode.prio == new_value)
		return 0;

	return freq_qos_apply(req, PM_QOS_UPDATE_REQ, new_value);
}
EXPORT_SYMBOL_GPL(freq_qos_update_request);

/**
 * freq_qos_remove_request - Remove frequency QoS request from its list.
 * @req: Request to remove.
 *
 * Remove the given frequency QoS request from the list of constraints it
 * belongs to and recompute the effective constraint value for that list.
 *
 * Return 1 if the effective constraint value has changed, 0 if the effective
 * constraint value has not changed, or a negative error code on failures.
 */
int freq_qos_remove_request(struct freq_qos_request *req)
{
	int ret;

	if (!req)
		return -EINVAL;

	if (WARN(!freq_qos_request_active(req),
		 "%s() called for unknown object\n", __func__))
		return -EINVAL;

	ret = freq_qos_apply(req, PM_QOS_REMOVE_REQ, PM_QOS_DEFAULT_VALUE);
	req->qos = NULL;
	req->type = 0;

	return ret;
}
EXPORT_SYMBOL_GPL(freq_qos_remove_request);

/**
 * freq_qos_add_notifier - Add frequency QoS change notifier.
 * @qos: List of requests to add the notifier to.
 * @type: Request type.
 * @notifier: Notifier block to add.
 */
int freq_qos_add_notifier(struct freq_constraints *qos,
			  enum freq_qos_req_type type,
			  struct notifier_block *notifier)
{
	int ret;

	if (IS_ERR_OR_NULL(qos) || !notifier)
		return -EINVAL;

	switch (type) {
	case FREQ_QOS_MIN:
		ret = blocking_notifier_chain_register(qos->min_freq.notifiers,
						       notifier);
		break;
	case FREQ_QOS_MAX:
		ret = blocking_notifier_chain_register(qos->max_freq.notifiers,
						       notifier);
		break;
	default:
		WARN_ON(1);
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(freq_qos_add_notifier);

/**
 * freq_qos_remove_notifier - Remove frequency QoS change notifier.
 * @qos: List of requests to remove the notifier from.
 * @type: Request type.
 * @notifier: Notifier block to remove.
 */
int freq_qos_remove_notifier(struct freq_constraints *qos,
			     enum freq_qos_req_type type,
			     struct notifier_block *notifier)
{
	int ret;

	if (IS_ERR_OR_NULL(qos) || !notifier)
		return -EINVAL;

	switch (type) {
	case FREQ_QOS_MIN:
		ret = blocking_notifier_chain_unregister(qos->min_freq.notifiers,
							 notifier);
		break;
	case FREQ_QOS_MAX:
		ret = blocking_notifier_chain_unregister(qos->max_freq.notifiers,
							 notifier);
		break;
	default:
		WARN_ON(1);
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(freq_qos_remove_notifier);
