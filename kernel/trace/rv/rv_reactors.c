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

static struct rv_reactor_def *get_reactor_rdef_by_name(char *name)
{
	struct rv_reactor_def *r;

	list_for_each_entry(r, &rv_reactors_list, list) {
		if (strcmp(name, r->reactor->name) == 0)
			return r;
	}
	return NULL;
}

/*
 * Available reactors seq functions.
 */
static int reactors_show(struct seq_file *m, void *p)
{
	struct rv_reactor_def *rea_def = p;

	seq_printf(m, "%s\n", rea_def->reactor->name);
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
	struct rv_monitor_def *mdef = m->private;
	struct rv_reactor_def *rdef = p;

	if (mdef->rdef == rdef)
		seq_printf(m, "[%s]\n", rdef->reactor->name);
	else
		seq_printf(m, "%s\n", rdef->reactor->name);
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

static void monitor_swap_reactors(struct rv_monitor_def *mdef, struct rv_reactor_def *rdef,
				    bool reacting)
{
	bool monitor_enabled;

	/* nothing to do */
	if (mdef->rdef == rdef)
		return;

	monitor_enabled = mdef->monitor->enabled;
	if (monitor_enabled)
		rv_disable_monitor(mdef);

	/* swap reactor's usage */
	mdef->rdef->counter--;
	rdef->counter++;

	mdef->rdef = rdef;
	mdef->reacting = reacting;
	mdef->monitor->react = rdef->reactor->react;

	if (monitor_enabled)
		rv_enable_monitor(mdef);
}

static ssize_t
monitor_reactors_write(struct file *file, const char __user *user_buf,
		      size_t count, loff_t *ppos)
{
	char buff[MAX_RV_REACTOR_NAME_SIZE + 2];
	struct rv_monitor_def *mdef;
	struct rv_reactor_def *rdef;
	struct seq_file *seq_f;
	int retval = -EINVAL;
	bool enable;
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
	mdef = seq_f->private;

	mutex_lock(&rv_interface_lock);

	retval = -EINVAL;

	list_for_each_entry(rdef, &rv_reactors_list, list) {
		if (strcmp(ptr, rdef->reactor->name) != 0)
			continue;

		if (rdef == get_reactor_rdef_by_name("nop"))
			enable = false;
		else
			enable = true;

		monitor_swap_reactors(mdef, rdef, enable);

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
	struct rv_monitor_def *mdef = inode->i_private;
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
	seq_f->private = mdef;

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
	struct rv_reactor_def *r;

	list_for_each_entry(r, &rv_reactors_list, list) {
		if (strcmp(reactor->name, r->reactor->name) == 0) {
			pr_info("Reactor %s is already registered\n", reactor->name);
			return -EINVAL;
		}
	}

	r = kzalloc(sizeof(struct rv_reactor_def), GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	r->reactor = reactor;
	r->counter = 0;

	list_add_tail(&r->list, &rv_reactors_list);

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
	struct rv_reactor_def *ptr, *next;
	int ret = 0;

	mutex_lock(&rv_interface_lock);

	list_for_each_entry_safe(ptr, next, &rv_reactors_list, list) {
		if (strcmp(reactor->name, ptr->reactor->name) == 0) {

			if (!ptr->counter) {
				list_del(&ptr->list);
			} else {
				printk(KERN_WARNING
				       "rv: the rv_reactor %s is in use by %d monitor(s)\n",
				       ptr->reactor->name, ptr->counter);
				printk(KERN_WARNING "rv: the rv_reactor %s cannot be removed\n",
				       ptr->reactor->name);
				ret = -EBUSY;
				break;
			}
		}
	}

	mutex_unlock(&rv_interface_lock);
	return ret;
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
 * @mdef:	monitor's definition.
 *
 * Returns 0 if successful, error otherwise.
 */
int reactor_populate_monitor(struct rv_monitor_def *mdef)
{
	struct dentry *tmp;

	tmp = rv_create_file("reactors", RV_MODE_WRITE, mdef->root_d, mdef, &monitor_reactors_ops);
	if (!tmp)
		return -ENOMEM;

	/*
	 * Configure as the rv_nop reactor.
	 */
	mdef->rdef = get_reactor_rdef_by_name("nop");
	mdef->rdef->counter++;
	mdef->reacting = false;

	return 0;
}

/**
 * reactor_cleanup_monitor - cleanup a monitor reference
 * @mdef:       monitor's definition.
 */
void reactor_cleanup_monitor(struct rv_monitor_def *mdef)
{
	lockdep_assert_held(&rv_interface_lock);
	mdef->rdef->counter--;
	WARN_ON_ONCE(mdef->rdef->counter < 0);
}

/*
 * Nop reactor register
 */
static void rv_nop_reaction(char *msg)
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
