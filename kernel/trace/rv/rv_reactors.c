// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
 *
 * Runtime reactor interface.
 *
 * A runtime monitor can cause a reaction to the detection of an
 * exception on the model's execution. By default, the monitors have
 * tracing reactions, printing the monitor output via tracepoints.
 * But other reactions can be added (on-demand) via this interface.
 *
 * == Registering reactors ==
 *
 * The struct rv_reactor defines a callback function to be executed
 * in case of a model exception happens. The callback function
 * receives a message to be (optionally) printed before executing
 * the reaction.
 *
 * A RV reactor is registered via:
 *   int rv_register_reactor(struct rv_reactor *reactor)
 * And unregistered via:
 *   int rv_unregister_reactor(struct rv_reactor *reactor)
 *
 * These functions are exported to modules, enabling reactors to be
 * dynamically loaded.
 *
 * == User interface ==
 *
 * The user interface resembles the kernel tracing interface and
 * presents these files:
 *
 *  "available_reactors"
 *    - List the available reactors, one per line.
 *
 *    For example:
 *      # cat available_reactors
 *      nop
 *      panic
 *      printk
 *
 *  "reacting_on"
 *    - It is an on/off general switch for reactors, disabling
 *    all reactions.
 *
 *  "monitors/MONITOR/reactors"
 *    - List available reactors, with the select reaction for the given
 *    MONITOR inside []. The default one is the nop (no operation)
 *    reactor.
 *    - Writing the name of an reactor enables it to the given
 *    MONITOR.
 *
 *    For example:
 *      # cat monitors/wip/reactors
 *      [nop]
 *      panic
 *      printk
 *      # echo panic > monitors/wip/reactors
 *      # cat monitors/wip/reactors
 *      nop
 *      [panic]
 *      printk
 */

#include <linux/slab.h>

#include "rv.h"

/*
 * Interface for the reactor register.
 */
static LIST_HEAD(rv_reactors_list);

static struct rv_reactor *get_reactor_rdef_by_name(char *name)
{
	struct rv_reactor *r;

	list_for_each_entry(r, &rv_reactors_list, list) {
		if (strcmp(name, r->name) == 0)
			return r;
	}
	return NULL;
}

/*
 * Available reactors seq functions.
 */
static int reactors_show(struct seq_file *m, void *p)
{
	struct rv_reactor *reactor = container_of(p, struct rv_reactor, list);

	seq_printf(m, "%s\n", reactor->name);
	return 0;
}

static void reactors_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&rv_interface_lock);
}

static void *reactors_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&rv_interface_lock);
	return seq_list_start(&rv_reactors_list, *pos);
}

static void *reactors_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &rv_reactors_list, pos);
}

/*
 * available_reactors seq definition.
 */
static const struct seq_operations available_reactors_seq_ops = {
	.start	= reactors_start,
	.next	= reactors_next,
	.stop	= reactors_stop,
	.show	= reactors_show
};

/*
 * available_reactors interface.
 */
static int available_reactors_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &available_reactors_seq_ops);
};

static const struct file_operations available_reactors_ops = {
	.open    = available_reactors_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

/*
 * Monitor's reactor file.
 */
static int monitor_reactor_show(struct seq_file *m, void *p)
{
	struct rv_monitor *mon = m->private;
	struct rv_reactor *reactor = container_of(p, struct rv_reactor, list);

	if (mon->reactor == reactor)
		seq_printf(m, "[%s]\n", reactor->name);
	else
		seq_printf(m, "%s\n", reactor->name);
	return 0;
}

/*
 * available_reactors seq definition.
 */
static const struct seq_operations monitor_reactors_seq_ops = {
	.start	= reactors_start,
	.next	= reactors_next,
	.stop	= reactors_stop,
	.show	= monitor_reactor_show
};

static void monitor_swap_reactors_single(struct rv_monitor *mon,
					 struct rv_reactor *reactor,
					 bool nested)
{
	bool monitor_enabled;

	/* nothing to do */
	if (mon->reactor == reactor)
		return;

	monitor_enabled = mon->enabled;
	if (monitor_enabled)
		rv_disable_monitor(mon);

	mon->reactor = reactor;
	mon->react = reactor->react;

	/* enable only once if iterating through a container */
	if (monitor_enabled && !nested)
		rv_enable_monitor(mon);
}

static void monitor_swap_reactors(struct rv_monitor *mon, struct rv_reactor *reactor)
{
	struct rv_monitor *p = mon;

	if (rv_is_container_monitor(mon))
		list_for_each_entry_continue(p, &rv_monitors_list, list) {
			if (p->parent != mon)
				break;
			monitor_swap_reactors_single(p, reactor, true);
		}
	/*
	 * This call enables and disables the monitor if they were active.
	 * In case of a container, we already disabled all and will enable all.
	 * All nested monitors are enabled also if they were off, we may refine
	 * this logic in the future.
	 */
	monitor_swap_reactors_single(mon, reactor, false);
}

static ssize_t
monitor_reactors_write(struct file *file, const char __user *user_buf,
		      size_t count, loff_t *ppos)
{
	char buff[MAX_RV_REACTOR_NAME_SIZE + 2];
	struct rv_monitor *mon;
	struct rv_reactor *reactor;
	struct seq_file *seq_f;
	int retval = -EINVAL;
	char *ptr;
	int len;

	if (count < 1 || count > MAX_RV_REACTOR_NAME_SIZE + 1)
		return -EINVAL;

	memset(buff, 0, sizeof(buff));

	retval = simple_write_to_buffer(buff, sizeof(buff) - 1, ppos, user_buf, count);
	if (retval < 0)
		return -EFAULT;

	ptr = strim(buff);

	len = strlen(ptr);
	if (!len)
		return count;

	/*
	 * See monitor_reactors_open()
	 */
	seq_f = file->private_data;
	mon = seq_f->private;

	mutex_lock(&rv_interface_lock);

	retval = -EINVAL;

	list_for_each_entry(reactor, &rv_reactors_list, list) {
		if (strcmp(ptr, reactor->name) != 0)
			continue;

		monitor_swap_reactors(mon, reactor);

		retval = count;
		break;
	}

	mutex_unlock(&rv_interface_lock);

	return retval;
}

/*
 * available_reactors interface.
 */
static int monitor_reactors_open(struct inode *inode, struct file *file)
{
	struct rv_monitor *mon = inode->i_private;
	struct seq_file *seq_f;
	int ret;

	ret = seq_open(file, &monitor_reactors_seq_ops);
	if (ret < 0)
		return ret;

	/*
	 * seq_open stores the seq_file on the file->private data.
	 */
	seq_f = file->private_data;

	/*
	 * Copy the create file "private" data to the seq_file private data.
	 */
	seq_f->private = mon;

	return 0;
};

static const struct file_operations monitor_reactors_ops = {
	.open    = monitor_reactors_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
	.write = monitor_reactors_write
};

static int __rv_register_reactor(struct rv_reactor *reactor)
{
	struct rv_reactor *r;

	list_for_each_entry(r, &rv_reactors_list, list) {
		if (strcmp(reactor->name, r->name) == 0) {
			pr_info("Reactor %s is already registered\n", reactor->name);
			return -EINVAL;
		}
	}

	list_add_tail(&reactor->list, &rv_reactors_list);

	return 0;
}

/**
 * rv_register_reactor - register a rv reactor.
 * @reactor:	The rv_reactor to be registered.
 *
 * Returns 0 if successful, error otherwise.
 */
int rv_register_reactor(struct rv_reactor *reactor)
{
	int retval = 0;

	if (strlen(reactor->name) >= MAX_RV_REACTOR_NAME_SIZE) {
		pr_info("Reactor %s has a name longer than %d\n",
			reactor->name, MAX_RV_MONITOR_NAME_SIZE);
		return -EINVAL;
	}

	mutex_lock(&rv_interface_lock);
	retval = __rv_register_reactor(reactor);
	mutex_unlock(&rv_interface_lock);
	return retval;
}

/**
 * rv_unregister_reactor - unregister a rv reactor.
 * @reactor:	The rv_reactor to be unregistered.
 *
 * Returns 0 if successful, error otherwise.
 */
int rv_unregister_reactor(struct rv_reactor *reactor)
{
	mutex_lock(&rv_interface_lock);
	list_del(&reactor->list);
	mutex_unlock(&rv_interface_lock);
	return 0;
}

/*
 * reacting_on interface.
 */
static bool __read_mostly reacting_on;

/**
 * rv_reacting_on - checks if reacting is on
 *
 * Returns 1 if on, 0 otherwise.
 */
bool rv_reacting_on(void)
{
	/* Ensures that concurrent monitors read consistent reacting_on */
	smp_rmb();
	return READ_ONCE(reacting_on);
}

static ssize_t reacting_on_read_data(struct file *filp,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	char *buff;

	buff = rv_reacting_on() ? "1\n" : "0\n";

	return simple_read_from_buffer(user_buf, count, ppos, buff, strlen(buff)+1);
}

static void turn_reacting_off(void)
{
	WRITE_ONCE(reacting_on, false);
	/* Ensures that concurrent monitors read consistent reacting_on */
	smp_wmb();
}

static void turn_reacting_on(void)
{
	WRITE_ONCE(reacting_on, true);
	/* Ensures that concurrent monitors read consistent reacting_on */
	smp_wmb();
}

static ssize_t reacting_on_write_data(struct file *filp, const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	int retval;
	bool val;

	retval = kstrtobool_from_user(user_buf, count, &val);
	if (retval)
		return retval;

	mutex_lock(&rv_interface_lock);

	if (val)
		turn_reacting_on();
	else
		turn_reacting_off();

	/*
	 * Wait for the execution of all events to finish
	 * before returning to user-space.
	 */
	tracepoint_synchronize_unregister();

	mutex_unlock(&rv_interface_lock);

	return count;
}

static const struct file_operations reacting_on_fops = {
	.open   = simple_open,
	.write  = reacting_on_write_data,
	.read   = reacting_on_read_data,
};

/**
 * reactor_populate_monitor - creates per monitor reactors file
 * @mon:	The monitor.
 *
 * Returns 0 if successful, error otherwise.
 */
int reactor_populate_monitor(struct rv_monitor *mon)
{
	struct dentry *tmp;

	tmp = rv_create_file("reactors", RV_MODE_WRITE, mon->root_d, mon, &monitor_reactors_ops);
	if (!tmp)
		return -ENOMEM;

	/*
	 * Configure as the rv_nop reactor.
	 */
	mon->reactor = get_reactor_rdef_by_name("nop");

	return 0;
}

/*
 * Nop reactor register
 */
__printf(1, 2) static void rv_nop_reaction(const char *msg, ...)
{
}

static struct rv_reactor rv_nop = {
	.name = "nop",
	.description = "no-operation reactor: do nothing.",
	.react = rv_nop_reaction
};

int init_rv_reactors(struct dentry *root_dir)
{
	struct dentry *available, *reacting;
	int retval;

	available = rv_create_file("available_reactors", RV_MODE_READ, root_dir, NULL,
				   &available_reactors_ops);
	if (!available)
		goto out_err;

	reacting = rv_create_file("reacting_on", RV_MODE_WRITE, root_dir, NULL, &reacting_on_fops);
	if (!reacting)
		goto rm_available;

	retval = __rv_register_reactor(&rv_nop);
	if (retval)
		goto rm_reacting;

	turn_reacting_on();

	return 0;

rm_reacting:
	rv_remove(reacting);
rm_available:
	rv_remove(available);
out_err:
	return -ENOMEM;
}
