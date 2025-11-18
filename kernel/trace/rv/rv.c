// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
 *
 * This is the online Runtime Verification (RV) interface.
 *
 * RV is a lightweight (yet rigorous) method that complements classical
 * exhaustive verification techniques (such as model checking and
 * theorem proving) with a more practical approach to complex systems.
 *
 * RV works by analyzing the trace of the system's actual execution,
 * comparing it against a formal specification of the system behavior.
 * RV can give precise information on the runtime behavior of the
 * monitored system while enabling the reaction for unexpected
 * events, avoiding, for example, the propagation of a failure on
 * safety-critical systems.
 *
 * The development of this interface roots in the development of the
 * paper:
 *
 * De Oliveira, Daniel Bristot; Cucinotta, Tommaso; De Oliveira, Romulo
 * Silva. Efficient formal verification for the Linux kernel. In:
 * International Conference on Software Engineering and Formal Methods.
 * Springer, Cham, 2019. p. 315-332.
 *
 * And:
 *
 * De Oliveira, Daniel Bristot, et al. Automata-based formal analysis
 * and verification of the real-time Linux kernel. PhD Thesis, 2020.
 *
 * == Runtime monitor interface ==
 *
 * A monitor is the central part of the runtime verification of a system.
 *
 * The monitor stands in between the formal specification of the desired
 * (or undesired) behavior, and the trace of the actual system.
 *
 * In Linux terms, the runtime verification monitors are encapsulated
 * inside the "RV monitor" abstraction. A RV monitor includes a reference
 * model of the system, a set of instances of the monitor (per-cpu monitor,
 * per-task monitor, and so on), and the helper functions that glue the
 * monitor to the system via trace. Generally, a monitor includes some form
 * of trace output as a reaction for event parsing and exceptions,
 * as depicted below:
 *
 * Linux  +----- RV Monitor ----------------------------------+ Formal
 *  Realm |                                                   |  Realm
 *  +-------------------+     +----------------+     +-----------------+
 *  |   Linux kernel    |     |     Monitor    |     |     Reference   |
 *  |     Tracing       |  -> |   Instance(s)  | <-  |       Model     |
 *  | (instrumentation) |     | (verification) |     | (specification) |
 *  +-------------------+     +----------------+     +-----------------+
 *         |                          |                       |
 *         |                          V                       |
 *         |                     +----------+                 |
 *         |                     | Reaction |                 |
 *         |                     +--+--+--+-+                 |
 *         |                        |  |  |                   |
 *         |                        |  |  +-> trace output ?  |
 *         +------------------------|--|----------------------+
 *                                  |  +----> panic ?
 *                                  +-------> <user-specified>
 *
 * This file implements the interface for loading RV monitors, and
 * to control the verification session.
 *
 * == Registering monitors ==
 *
 * The struct rv_monitor defines a set of callback functions to control
 * a verification session. For instance, when a given monitor is enabled,
 * the "enable" callback function is called to hook the instrumentation
 * functions to the kernel trace events. The "disable" function is called
 * when disabling the verification session.
 *
 * A RV monitor is registered via:
 *   int rv_register_monitor(struct rv_monitor *monitor);
 * And unregistered via:
 *   int rv_unregister_monitor(struct rv_monitor *monitor);
 *
 * == User interface ==
 *
 * The user interface resembles kernel tracing interface. It presents
 * these files:
 *
 *  "available_monitors"
 *    - List the available monitors, one per line.
 *
 *    For example:
 *      # cat available_monitors
 *      wip
 *      wwnr
 *
 *  "enabled_monitors"
 *    - Lists the enabled monitors, one per line;
 *    - Writing to it enables a given monitor;
 *    - Writing a monitor name with a '!' prefix disables it;
 *    - Truncating the file disables all enabled monitors.
 *
 *    For example:
 *      # cat enabled_monitors
 *      # echo wip > enabled_monitors
 *      # echo wwnr >> enabled_monitors
 *      # cat enabled_monitors
 *      wip
 *      wwnr
 *      # echo '!wip' >> enabled_monitors
 *      # cat enabled_monitors
 *      wwnr
 *      # echo > enabled_monitors
 *      # cat enabled_monitors
 *      #
 *
 *    Note that more than one monitor can be enabled concurrently.
 *
 *  "monitoring_on"
 *    - It is an on/off general switcher for monitoring. Note
 *    that it does not disable enabled monitors or detach events,
 *    but stops the per-entity monitors from monitoring the events
 *    received from the instrumentation. It resembles the "tracing_on"
 *    switcher.
 *
 *  "monitors/"
 *    Each monitor will have its own directory inside "monitors/". There
 *    the monitor specific files will be presented.
 *    The "monitors/" directory resembles the "events" directory on
 *    tracefs.
 *
 *    For example:
 *      # cd monitors/wip/
 *      # ls
 *      desc  enable
 *      # cat desc
 *      auto-generated wakeup in preemptive monitor.
 *      # cat enable
 *      0
 *
 *  For further information, see:
 *   Documentation/trace/rv/runtime-verification.rst
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>

#ifdef CONFIG_RV_MON_EVENTS
#define CREATE_TRACE_POINTS
#include <rv_trace.h>
#endif

#include "rv.h"

DEFINE_MUTEX(rv_interface_lock);

static struct rv_interface rv_root;

struct dentry *get_monitors_root(void)
{
	return rv_root.monitors_dir;
}

/*
 * Interface for the monitor register.
 */
LIST_HEAD(rv_monitors_list);

static int task_monitor_count;
static bool task_monitor_slots[CONFIG_RV_PER_TASK_MONITORS];

int rv_get_task_monitor_slot(void)
{
	int i;

	lockdep_assert_held(&rv_interface_lock);

	if (task_monitor_count == CONFIG_RV_PER_TASK_MONITORS)
		return -EBUSY;

	task_monitor_count++;

	for (i = 0; i < CONFIG_RV_PER_TASK_MONITORS; i++) {
		if (task_monitor_slots[i] == false) {
			task_monitor_slots[i] = true;
			return i;
		}
	}

	WARN_ONCE(1, "RV task_monitor_count and slots are out of sync\n");

	return -EINVAL;
}

void rv_put_task_monitor_slot(int slot)
{
	lockdep_assert_held(&rv_interface_lock);

	if (slot < 0 || slot >= CONFIG_RV_PER_TASK_MONITORS) {
		WARN_ONCE(1, "RV releasing an invalid slot!: %d\n", slot);
		return;
	}

	WARN_ONCE(!task_monitor_slots[slot], "RV releasing unused task_monitor_slots: %d\n",
		  slot);

	task_monitor_count--;
	task_monitor_slots[slot] = false;
}

/*
 * Monitors with a parent are nested,
 * Monitors without a parent could be standalone or containers.
 */
bool rv_is_nested_monitor(struct rv_monitor *mon)
{
	return mon->parent != NULL;
}

/*
 * We set our list to have nested monitors listed after their parent
 * if a monitor has a child element its a container.
 * Containers can be also identified based on their function pointers:
 * as they are not real monitors they do not need function definitions
 * for enable()/disable(). Use this condition to find empty containers.
 * Keep both conditions in case we have some non-compliant containers.
 */
bool rv_is_container_monitor(struct rv_monitor *mon)
{
	struct rv_monitor *next;

	if (list_is_last(&mon->list, &rv_monitors_list))
		return false;

	next = list_next_entry(mon, list);

	return next->parent == mon || !mon->enable;
}

/*
 * This section collects the monitor/ files and folders.
 */
static ssize_t monitor_enable_read_data(struct file *filp, char __user *user_buf, size_t count,
					loff_t *ppos)
{
	struct rv_monitor *mon = filp->private_data;
	const char *buff;

	buff = mon->enabled ? "1\n" : "0\n";

	return simple_read_from_buffer(user_buf, count, ppos, buff, strlen(buff)+1);
}

/*
 * __rv_disable_monitor - disabled an enabled monitor
 */
static int __rv_disable_monitor(struct rv_monitor *mon, bool sync)
{
	lockdep_assert_held(&rv_interface_lock);

	if (mon->enabled) {
		mon->enabled = 0;
		if (mon->disable)
			mon->disable();

		/*
		 * Wait for the execution of all events to finish.
		 * Otherwise, the data used by the monitor could
		 * be inconsistent. i.e., if the monitor is re-enabled.
		 */
		if (sync)
			tracepoint_synchronize_unregister();
		return 1;
	}
	return 0;
}

static void rv_disable_single(struct rv_monitor *mon)
{
	__rv_disable_monitor(mon, true);
}

static int rv_enable_single(struct rv_monitor *mon)
{
	int retval;

	lockdep_assert_held(&rv_interface_lock);

	if (mon->enabled)
		return 0;

	retval = mon->enable();

	if (!retval)
		mon->enabled = 1;

	return retval;
}

static void rv_disable_container(struct rv_monitor *mon)
{
	struct rv_monitor *p = mon;
	int enabled = 0;

	list_for_each_entry_continue(p, &rv_monitors_list, list) {
		if (p->parent != mon)
			break;
		enabled += __rv_disable_monitor(p, false);
	}
	if (enabled)
		tracepoint_synchronize_unregister();
	mon->enabled = 0;
}

static int rv_enable_container(struct rv_monitor *mon)
{
	struct rv_monitor *p = mon;
	int retval = 0;

	list_for_each_entry_continue(p, &rv_monitors_list, list) {
		if (retval || p->parent != mon)
			break;
		retval = rv_enable_single(p);
	}
	if (retval)
		rv_disable_container(mon);
	else
		mon->enabled = 1;
	return retval;
}

/**
 * rv_disable_monitor - disable a given runtime monitor
 * @mon: Pointer to the monitor definition structure.
 *
 * Returns 0 on success.
 */
int rv_disable_monitor(struct rv_monitor *mon)
{
	if (rv_is_container_monitor(mon))
		rv_disable_container(mon);
	else
		rv_disable_single(mon);

	return 0;
}

/**
 * rv_enable_monitor - enable a given runtime monitor
 * @mon: Pointer to the monitor definition structure.
 *
 * Returns 0 on success, error otherwise.
 */
int rv_enable_monitor(struct rv_monitor *mon)
{
	int retval;

	if (rv_is_container_monitor(mon))
		retval = rv_enable_container(mon);
	else
		retval = rv_enable_single(mon);

	return retval;
}

/*
 * interface for enabling/disabling a monitor.
 */
static ssize_t monitor_enable_write_data(struct file *filp, const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct rv_monitor *mon = filp->private_data;
	int retval;
	bool val;

	retval = kstrtobool_from_user(user_buf, count, &val);
	if (retval)
		return retval;

	mutex_lock(&rv_interface_lock);

	if (val)
		retval = rv_enable_monitor(mon);
	else
		retval = rv_disable_monitor(mon);

	mutex_unlock(&rv_interface_lock);

	return retval ? : count;
}

static const struct file_operations interface_enable_fops = {
	.open   = simple_open,
	.write  = monitor_enable_write_data,
	.read   = monitor_enable_read_data,
};

/*
 * Interface to read monitors description.
 */
static ssize_t monitor_desc_read_data(struct file *filp, char __user *user_buf, size_t count,
				      loff_t *ppos)
{
	struct rv_monitor *mon = filp->private_data;
	char buff[256];

	memset(buff, 0, sizeof(buff));

	snprintf(buff, sizeof(buff), "%s\n", mon->description);

	return simple_read_from_buffer(user_buf, count, ppos, buff, strlen(buff) + 1);
}

static const struct file_operations interface_desc_fops = {
	.open   = simple_open,
	.read	= monitor_desc_read_data,
};

/*
 * During the registration of a monitor, this function creates
 * the monitor dir, where the specific options of the monitor
 * are exposed.
 */
static int create_monitor_dir(struct rv_monitor *mon, struct rv_monitor *parent)
{
	struct dentry *root = parent ? parent->root_d : get_monitors_root();
	const char *name = mon->name;
	struct dentry *tmp;
	int retval;

	mon->root_d = rv_create_dir(name, root);
	if (!mon->root_d)
		return -ENOMEM;

	tmp = rv_create_file("enable", RV_MODE_WRITE, mon->root_d, mon, &interface_enable_fops);
	if (!tmp) {
		retval = -ENOMEM;
		goto out_remove_root;
	}

	tmp = rv_create_file("desc", RV_MODE_READ, mon->root_d, mon, &interface_desc_fops);
	if (!tmp) {
		retval = -ENOMEM;
		goto out_remove_root;
	}

	retval = reactor_populate_monitor(mon);
	if (retval)
		goto out_remove_root;

	return 0;

out_remove_root:
	rv_remove(mon->root_d);
	return retval;
}

/*
 * Available/Enable monitor shared seq functions.
 */
static int monitors_show(struct seq_file *m, void *p)
{
	struct rv_monitor *mon = container_of(p, struct rv_monitor, list);

	if (mon->parent)
		seq_printf(m, "%s:%s\n", mon->parent->name, mon->name);
	else
		seq_printf(m, "%s\n", mon->name);
	return 0;
}

/*
 * Used by the seq file operations at the end of a read
 * operation.
 */
static void monitors_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&rv_interface_lock);
}

/*
 * Available monitor seq functions.
 */
static void *available_monitors_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&rv_interface_lock);
	return seq_list_start(&rv_monitors_list, *pos);
}

static void *available_monitors_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &rv_monitors_list, pos);
}

/*
 * Enable monitor seq functions.
 */
static void *enabled_monitors_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct rv_monitor *mon = container_of(p, struct rv_monitor, list);

	(*pos)++;

	list_for_each_entry_continue(mon, &rv_monitors_list, list) {
		if (mon->enabled)
			return &mon->list;
	}

	return NULL;
}

static void *enabled_monitors_start(struct seq_file *m, loff_t *pos)
{
	struct list_head *head;
	loff_t l;

	mutex_lock(&rv_interface_lock);

	if (list_empty(&rv_monitors_list))
		return NULL;

	head = &rv_monitors_list;

	for (l = 0; l <= *pos; ) {
		head = enabled_monitors_next(m, head, &l);
		if (!head)
			break;
	}

	return head;
}

/*
 * available/enabled monitors seq definition.
 */
static const struct seq_operations available_monitors_seq_ops = {
	.start	= available_monitors_start,
	.next	= available_monitors_next,
	.stop	= monitors_stop,
	.show	= monitors_show
};

static const struct seq_operations enabled_monitors_seq_ops = {
	.start  = enabled_monitors_start,
	.next   = enabled_monitors_next,
	.stop   = monitors_stop,
	.show   = monitors_show
};

/*
 * available_monitors interface.
 */
static int available_monitors_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &available_monitors_seq_ops);
};

static const struct file_operations available_monitors_ops = {
	.open    = available_monitors_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

/*
 * enabled_monitors interface.
 */
static void disable_all_monitors(void)
{
	struct rv_monitor *mon;
	int enabled = 0;

	mutex_lock(&rv_interface_lock);

	list_for_each_entry(mon, &rv_monitors_list, list)
		enabled += __rv_disable_monitor(mon, false);

	if (enabled) {
		/*
		 * Wait for the execution of all events to finish.
		 * Otherwise, the data used by the monitor could
		 * be inconsistent. i.e., if the monitor is re-enabled.
		 */
		tracepoint_synchronize_unregister();
	}

	mutex_unlock(&rv_interface_lock);
}

static int enabled_monitors_open(struct inode *inode, struct file *file)
{
	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC))
		disable_all_monitors();

	return seq_open(file, &enabled_monitors_seq_ops);
};

static ssize_t enabled_monitors_write(struct file *filp, const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	char buff[MAX_RV_MONITOR_NAME_SIZE + 2];
	struct rv_monitor *mon;
	int retval = -EINVAL;
	bool enable = true;
	char *ptr, *tmp;
	int len;

	if (count < 1 || count > MAX_RV_MONITOR_NAME_SIZE + 1)
		return -EINVAL;

	memset(buff, 0, sizeof(buff));

	retval = simple_write_to_buffer(buff, sizeof(buff) - 1, ppos, user_buf, count);
	if (retval < 0)
		return -EFAULT;

	ptr = strim(buff);

	if (ptr[0] == '!') {
		enable = false;
		ptr++;
	}

	len = strlen(ptr);
	if (!len)
		return count;

	mutex_lock(&rv_interface_lock);

	retval = -EINVAL;

	/* we support 1 nesting level, trim the parent */
	tmp = strstr(ptr, ":");
	if (tmp)
		ptr = tmp+1;

	list_for_each_entry(mon, &rv_monitors_list, list) {
		if (strcmp(ptr, mon->name) != 0)
			continue;

		/*
		 * Monitor found!
		 */
		if (enable)
			retval = rv_enable_monitor(mon);
		else
			retval = rv_disable_monitor(mon);

		if (!retval)
			retval = count;

		break;
	}

	mutex_unlock(&rv_interface_lock);
	return retval;
}

static const struct file_operations enabled_monitors_ops = {
	.open		= enabled_monitors_open,
	.read		= seq_read,
	.write		= enabled_monitors_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/*
 * Monitoring on global switcher!
 */
static bool __read_mostly monitoring_on;

/**
 * rv_monitoring_on - checks if monitoring is on
 *
 * Returns 1 if on, 0 otherwise.
 */
bool rv_monitoring_on(void)
{
	return READ_ONCE(monitoring_on);
}

/*
 * monitoring_on general switcher.
 */
static ssize_t monitoring_on_read_data(struct file *filp, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	const char *buff;

	buff = rv_monitoring_on() ? "1\n" : "0\n";

	return simple_read_from_buffer(user_buf, count, ppos, buff, strlen(buff) + 1);
}

static void turn_monitoring_off(void)
{
	WRITE_ONCE(monitoring_on, false);
}

static void reset_all_monitors(void)
{
	struct rv_monitor *mon;

	list_for_each_entry(mon, &rv_monitors_list, list) {
		if (mon->enabled && mon->reset)
			mon->reset();
	}
}

static void turn_monitoring_on(void)
{
	WRITE_ONCE(monitoring_on, true);
}

static void turn_monitoring_on_with_reset(void)
{
	lockdep_assert_held(&rv_interface_lock);

	if (rv_monitoring_on())
		return;

	/*
	 * Monitors might be out of sync with the system if events were not
	 * processed because of !rv_monitoring_on().
	 *
	 * Reset all monitors, forcing a re-sync.
	 */
	reset_all_monitors();
	turn_monitoring_on();
}

static ssize_t monitoring_on_write_data(struct file *filp, const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	int retval;
	bool val;

	retval = kstrtobool_from_user(user_buf, count, &val);
	if (retval)
		return retval;

	mutex_lock(&rv_interface_lock);

	if (val)
		turn_monitoring_on_with_reset();
	else
		turn_monitoring_off();

	/*
	 * Wait for the execution of all events to finish
	 * before returning to user-space.
	 */
	tracepoint_synchronize_unregister();

	mutex_unlock(&rv_interface_lock);

	return count;
}

static const struct file_operations monitoring_on_fops = {
	.open   = simple_open,
	.write  = monitoring_on_write_data,
	.read   = monitoring_on_read_data,
};

static void destroy_monitor_dir(struct rv_monitor *mon)
{
	rv_remove(mon->root_d);
}

/**
 * rv_register_monitor - register a rv monitor.
 * @monitor:    The rv_monitor to be registered.
 * @parent:     The parent of the monitor to be registered, NULL if not nested.
 *
 * Returns 0 if successful, error otherwise.
 */
int rv_register_monitor(struct rv_monitor *monitor, struct rv_monitor *parent)
{
	struct rv_monitor *r;
	int retval = 0;

	if (strlen(monitor->name) >= MAX_RV_MONITOR_NAME_SIZE) {
		pr_info("Monitor %s has a name longer than %d\n", monitor->name,
			MAX_RV_MONITOR_NAME_SIZE);
		return -EINVAL;
	}

	mutex_lock(&rv_interface_lock);

	list_for_each_entry(r, &rv_monitors_list, list) {
		if (strcmp(monitor->name, r->name) == 0) {
			pr_info("Monitor %s is already registered\n", monitor->name);
			retval = -EEXIST;
			goto out_unlock;
		}
	}

	if (parent && rv_is_nested_monitor(parent)) {
		pr_info("Parent monitor %s is already nested, cannot nest further\n",
			parent->name);
		retval = -EINVAL;
		goto out_unlock;
	}

	monitor->parent = parent;

	retval = create_monitor_dir(monitor, parent);
	if (retval)
		goto out_unlock;

	/* keep children close to the parent for easier visualisation */
	if (parent)
		list_add(&monitor->list, &parent->list);
	else
		list_add_tail(&monitor->list, &rv_monitors_list);

out_unlock:
	mutex_unlock(&rv_interface_lock);
	return retval;
}

/**
 * rv_unregister_monitor - unregister a rv monitor.
 * @monitor:    The rv_monitor to be unregistered.
 *
 * Returns 0 if successful, error otherwise.
 */
int rv_unregister_monitor(struct rv_monitor *monitor)
{
	mutex_lock(&rv_interface_lock);

	rv_disable_monitor(monitor);
	list_del(&monitor->list);
	destroy_monitor_dir(monitor);

	mutex_unlock(&rv_interface_lock);
	return 0;
}

int __init rv_init_interface(void)
{
	struct dentry *tmp;
	int retval;

	rv_root.root_dir = rv_create_dir("rv", NULL);
	if (!rv_root.root_dir)
		goto out_err;

	rv_root.monitors_dir = rv_create_dir("monitors", rv_root.root_dir);
	if (!rv_root.monitors_dir)
		goto out_err;

	tmp = rv_create_file("available_monitors", RV_MODE_READ, rv_root.root_dir, NULL,
			     &available_monitors_ops);
	if (!tmp)
		goto out_err;

	tmp = rv_create_file("enabled_monitors", RV_MODE_WRITE, rv_root.root_dir, NULL,
			     &enabled_monitors_ops);
	if (!tmp)
		goto out_err;

	tmp = rv_create_file("monitoring_on", RV_MODE_WRITE, rv_root.root_dir, NULL,
			     &monitoring_on_fops);
	if (!tmp)
		goto out_err;
	retval = init_rv_reactors(rv_root.root_dir);
	if (retval)
		goto out_err;

	turn_monitoring_on();

	return 0;

out_err:
	rv_remove(rv_root.root_dir);
	printk(KERN_ERR "RV: Error while creating the RV interface\n");
	return 1;
}
