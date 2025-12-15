// SPDX-License-Identifier: GPL-2.0
/*
 * trace_events_trigger - trace event triggers
 *
 * Copyright (C) 2013 Tom Zanussi <tom.zanussi@linux.intel.com>
 */

#include <linux/security.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/rculist.h>

#include "trace.h"

static LIST_HEAD(trigger_commands);
static DEFINE_MUTEX(trigger_cmd_mutex);

static struct task_struct *trigger_kthread;
static struct llist_head trigger_data_free_list;
static DEFINE_MUTEX(trigger_data_kthread_mutex);

/* Bulk garbage collection of event_trigger_data elements */
static int trigger_kthread_fn(void *ignore)
{
	struct event_trigger_data *data, *tmp;
	struct llist_node *llnodes;

	/* Once this task starts, it lives forever */
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (llist_empty(&trigger_data_free_list))
			schedule();

		__set_current_state(TASK_RUNNING);

		llnodes = llist_del_all(&trigger_data_free_list);

		/* make sure current triggers exit before free */
		tracepoint_synchronize_unregister();

		llist_for_each_entry_safe(data, tmp, llnodes, llist)
			kfree(data);
	}

	return 0;
}

void trigger_data_free(struct event_trigger_data *data)
{
	if (data->cmd_ops->set_filter)
		data->cmd_ops->set_filter(NULL, data, NULL);

	if (unlikely(!trigger_kthread)) {
		guard(mutex)(&trigger_data_kthread_mutex);
		/* Check again after taking mutex */
		if (!trigger_kthread) {
			struct task_struct *kthread;

			kthread = kthread_create(trigger_kthread_fn, NULL,
						 "trigger_data_free");
			if (!IS_ERR(kthread))
				WRITE_ONCE(trigger_kthread, kthread);
		}
	}

	if (!trigger_kthread) {
		/* Do it the slow way */
		tracepoint_synchronize_unregister();
		kfree(data);
		return;
	}

	llist_add(&data->llist, &trigger_data_free_list);
	wake_up_process(trigger_kthread);
}

static inline void data_ops_trigger(struct event_trigger_data *data,
				    struct trace_buffer *buffer,  void *rec,
				    struct ring_buffer_event *event)
{
	const struct event_command *cmd_ops = data->cmd_ops;

	if (data->flags & EVENT_TRIGGER_FL_COUNT) {
		if (!cmd_ops->count_func(data, buffer, rec, event))
			return;
	}

	cmd_ops->trigger(data, buffer, rec, event);
}

/**
 * event_triggers_call - Call triggers associated with a trace event
 * @file: The trace_event_file associated with the event
 * @buffer: The ring buffer that the event is being written to
 * @rec: The trace entry for the event, NULL for unconditional invocation
 * @event: The event meta data in the ring buffer
 *
 * For each trigger associated with an event, invoke the trigger
 * function registered with the associated trigger command.  If rec is
 * non-NULL, it means that the trigger requires further processing and
 * shouldn't be unconditionally invoked.  If rec is non-NULL and the
 * trigger has a filter associated with it, rec will checked against
 * the filter and if the record matches the trigger will be invoked.
 * If the trigger is a 'post_trigger', meaning it shouldn't be invoked
 * in any case until the current event is written, the trigger
 * function isn't invoked but the bit associated with the deferred
 * trigger is set in the return value.
 *
 * Returns an enum event_trigger_type value containing a set bit for
 * any trigger that should be deferred, ETT_NONE if nothing to defer.
 *
 * Called from tracepoint handlers (with rcu_read_lock_sched() held).
 *
 * Return: an enum event_trigger_type value containing a set bit for
 * any trigger that should be deferred, ETT_NONE if nothing to defer.
 */
enum event_trigger_type
event_triggers_call(struct trace_event_file *file,
		    struct trace_buffer *buffer, void *rec,
		    struct ring_buffer_event *event)
{
	struct event_trigger_data *data;
	enum event_trigger_type tt = ETT_NONE;
	struct event_filter *filter;

	if (list_empty(&file->triggers))
		return tt;

	list_for_each_entry_rcu(data, &file->triggers, list) {
		if (data->paused)
			continue;
		if (!rec) {
			data_ops_trigger(data, buffer, rec, event);
			continue;
		}
		filter = rcu_dereference_sched(data->filter);
		if (filter && !filter_match_preds(filter, rec))
			continue;
		if (event_command_post_trigger(data->cmd_ops)) {
			tt |= data->cmd_ops->trigger_type;
			continue;
		}
		data_ops_trigger(data, buffer, rec, event);
	}
	return tt;
}
EXPORT_SYMBOL_GPL(event_triggers_call);

bool __trace_trigger_soft_disabled(struct trace_event_file *file)
{
	unsigned long eflags = file->flags;

	if (eflags & EVENT_FILE_FL_TRIGGER_MODE)
		event_triggers_call(file, NULL, NULL, NULL);
	if (eflags & EVENT_FILE_FL_SOFT_DISABLED)
		return true;
	if (eflags & EVENT_FILE_FL_PID_FILTER)
		return trace_event_ignore_this_pid(file);
	return false;
}
EXPORT_SYMBOL_GPL(__trace_trigger_soft_disabled);

/**
 * event_triggers_post_call - Call 'post_triggers' for a trace event
 * @file: The trace_event_file associated with the event
 * @tt: enum event_trigger_type containing a set bit for each trigger to invoke
 *
 * For each trigger associated with an event, invoke the trigger
 * function registered with the associated trigger command, if the
 * corresponding bit is set in the tt enum passed into this function.
 * See @event_triggers_call for details on how those bits are set.
 *
 * Called from tracepoint handlers (with rcu_read_lock_sched() held).
 */
void
event_triggers_post_call(struct trace_event_file *file,
			 enum event_trigger_type tt)
{
	struct event_trigger_data *data;

	list_for_each_entry_rcu(data, &file->triggers, list) {
		if (data->paused)
			continue;
		if (data->cmd_ops->trigger_type & tt)
			data_ops_trigger(data, NULL, NULL, NULL);
	}
}
EXPORT_SYMBOL_GPL(event_triggers_post_call);

#define SHOW_AVAILABLE_TRIGGERS	(void *)(1UL)

static void *trigger_next(struct seq_file *m, void *t, loff_t *pos)
{
	struct trace_event_file *event_file = event_file_data(m->private);

	if (t == SHOW_AVAILABLE_TRIGGERS) {
		(*pos)++;
		return NULL;
	}
	return seq_list_next(t, &event_file->triggers, pos);
}

static bool check_user_trigger(struct trace_event_file *file)
{
	struct event_trigger_data *data;

	list_for_each_entry_rcu(data, &file->triggers, list,
				lockdep_is_held(&event_mutex)) {
		if (data->flags & EVENT_TRIGGER_FL_PROBE)
			continue;
		return true;
	}
	return false;
}

static void *trigger_start(struct seq_file *m, loff_t *pos)
{
	struct trace_event_file *event_file;

	/* ->stop() is called even if ->start() fails */
	mutex_lock(&event_mutex);
	event_file = event_file_file(m->private);
	if (unlikely(!event_file))
		return ERR_PTR(-ENODEV);

	if (list_empty(&event_file->triggers) || !check_user_trigger(event_file))
		return *pos == 0 ? SHOW_AVAILABLE_TRIGGERS : NULL;

	return seq_list_start(&event_file->triggers, *pos);
}

static void trigger_stop(struct seq_file *m, void *t)
{
	mutex_unlock(&event_mutex);
}

static int trigger_show(struct seq_file *m, void *v)
{
	struct event_trigger_data *data;
	struct event_command *p;

	if (v == SHOW_AVAILABLE_TRIGGERS) {
		seq_puts(m, "# Available triggers:\n");
		seq_putc(m, '#');
		mutex_lock(&trigger_cmd_mutex);
		list_for_each_entry_reverse(p, &trigger_commands, list)
			seq_printf(m, " %s", p->name);
		seq_putc(m, '\n');
		mutex_unlock(&trigger_cmd_mutex);
		return 0;
	}

	data = list_entry(v, struct event_trigger_data, list);
	data->cmd_ops->print(m, data);

	return 0;
}

static const struct seq_operations event_triggers_seq_ops = {
	.start = trigger_start,
	.next = trigger_next,
	.stop = trigger_stop,
	.show = trigger_show,
};

static int event_trigger_regex_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = security_locked_down(LOCKDOWN_TRACEFS);
	if (ret)
		return ret;

	guard(mutex)(&event_mutex);

	if (unlikely(!event_file_file(file)))
		return -ENODEV;

	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC)) {
		struct trace_event_file *event_file;
		struct event_command *p;

		event_file = event_file_data(file);

		list_for_each_entry(p, &trigger_commands, list) {
			if (p->unreg_all)
				p->unreg_all(event_file);
		}
	}

	if (file->f_mode & FMODE_READ) {
		ret = seq_open(file, &event_triggers_seq_ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = file;
		}
	}

	return ret;
}

int trigger_process_regex(struct trace_event_file *file, char *buff)
{
	char *command, *next;
	struct event_command *p;

	next = buff = strim(buff);

	command = strsep(&next, ": \t");
	if (next) {
		next = skip_spaces(next);
		if (!*next)
			next = NULL;
	}
	command = (command[0] != '!') ? command : command + 1;

	guard(mutex)(&trigger_cmd_mutex);

	list_for_each_entry(p, &trigger_commands, list) {
		if (strcmp(p->name, command) == 0)
			return p->parse(p, file, buff, command, next);
	}

	return -EINVAL;
}

static ssize_t event_trigger_regex_write(struct file *file,
					 const char __user *ubuf,
					 size_t cnt, loff_t *ppos)
{
	struct trace_event_file *event_file;
	ssize_t ret;
	char *buf __free(kfree) = NULL;

	if (!cnt)
		return 0;

	if (cnt >= PAGE_SIZE)
		return -EINVAL;

	buf = memdup_user_nul(ubuf, cnt);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	guard(mutex)(&event_mutex);

	event_file = event_file_file(file);
	if (unlikely(!event_file))
		return -ENODEV;

	ret = trigger_process_regex(event_file, buf);
	if (ret < 0)
		return ret;

	*ppos += cnt;
	return cnt;
}

static int event_trigger_regex_release(struct inode *inode, struct file *file)
{
	if (file->f_mode & FMODE_READ)
		seq_release(inode, file);

	return 0;
}

static ssize_t
event_trigger_write(struct file *filp, const char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	return event_trigger_regex_write(filp, ubuf, cnt, ppos);
}

static int
event_trigger_open(struct inode *inode, struct file *filp)
{
	/* Checks for tracefs lockdown */
	return event_trigger_regex_open(inode, filp);
}

static int
event_trigger_release(struct inode *inode, struct file *file)
{
	return event_trigger_regex_release(inode, file);
}

const struct file_operations event_trigger_fops = {
	.open = event_trigger_open,
	.read = seq_read,
	.write = event_trigger_write,
	.llseek = tracing_lseek,
	.release = event_trigger_release,
};

/*
 * Currently we only register event commands from __init, so mark this
 * __init too.
 */
__init int register_event_command(struct event_command *cmd)
{
	struct event_command *p;

	guard(mutex)(&trigger_cmd_mutex);

	list_for_each_entry(p, &trigger_commands, list) {
		if (strcmp(cmd->name, p->name) == 0)
			return -EBUSY;
	}
	list_add(&cmd->list, &trigger_commands);

	return 0;
}

/*
 * Currently we only unregister event commands from __init, so mark
 * this __init too.
 */
__init int unregister_event_command(struct event_command *cmd)
{
	struct event_command *p, *n;

	guard(mutex)(&trigger_cmd_mutex);

	list_for_each_entry_safe(p, n, &trigger_commands, list) {
		if (strcmp(cmd->name, p->name) == 0) {
			list_del_init(&p->list);
			return 0;
		}
	}

	return -ENODEV;
}

/**
 * event_trigger_count - Optional count function for event triggers
 * @data: Trigger-specific data
 * @buffer: The ring buffer that the event is being written to
 * @rec: The trace entry for the event, NULL for unconditional invocation
 * @event: The event meta data in the ring buffer
 *
 * For triggers that can take a count parameter that doesn't do anything
 * special, they can use this function to assign to their .count_func
 * field.
 *
 * This simply does a count down of the @data->count field.
 *
 * If the @data->count is greater than zero, it will decrement it.
 *
 * Returns false if @data->count is zero, otherwise true.
 */
bool event_trigger_count(struct event_trigger_data *data,
			 struct trace_buffer *buffer,  void *rec,
			 struct ring_buffer_event *event)
{
	if (!data->count)
		return false;

	if (data->count != -1)
		(data->count)--;

	return true;
}

/**
 * event_trigger_print - Generic event_command @print implementation
 * @name: The name of the event trigger
 * @m: The seq_file being printed to
 * @data: Trigger-specific data
 * @filter_str: filter_str to print, if present
 *
 * Common implementation for event triggers to print themselves.
 *
 * Usually wrapped by a function that simply sets the @name of the
 * trigger command and then invokes this.
 *
 * Return: 0 on success, errno otherwise
 */
static int
event_trigger_print(const char *name, struct seq_file *m,
		    void *data, char *filter_str)
{
	long count = (long)data;

	seq_puts(m, name);

	if (count == -1)
		seq_puts(m, ":unlimited");
	else
		seq_printf(m, ":count=%ld", count);

	if (filter_str)
		seq_printf(m, " if %s\n", filter_str);
	else
		seq_putc(m, '\n');

	return 0;
}

/**
 * event_trigger_init - Generic event_command @init implementation
 * @data: Trigger-specific data
 *
 * Common implementation of event trigger initialization.
 *
 * Usually used directly as the @init method in event trigger
 * implementations.
 *
 * Return: 0 on success, errno otherwise
 */
int event_trigger_init(struct event_trigger_data *data)
{
	data->ref++;
	return 0;
}

/**
 * event_trigger_free - Generic event_command @free implementation
 * @data: Trigger-specific data
 *
 * Common implementation of event trigger de-initialization.
 *
 * Usually used directly as the @free method in event trigger
 * implementations.
 */
static void
event_trigger_free(struct event_trigger_data *data)
{
	if (WARN_ON_ONCE(data->ref <= 0))
		return;

	data->ref--;
	if (!data->ref)
		trigger_data_free(data);
}

int trace_event_trigger_enable_disable(struct trace_event_file *file,
				       int trigger_enable)
{
	int ret = 0;

	if (trigger_enable) {
		if (atomic_inc_return(&file->tm_ref) > 1)
			return ret;
		set_bit(EVENT_FILE_FL_TRIGGER_MODE_BIT, &file->flags);
		ret = trace_event_enable_disable(file, 1, 1);
	} else {
		if (atomic_dec_return(&file->tm_ref) > 0)
			return ret;
		clear_bit(EVENT_FILE_FL_TRIGGER_MODE_BIT, &file->flags);
		ret = trace_event_enable_disable(file, 0, 1);
	}

	return ret;
}

/**
 * clear_event_triggers - Clear all triggers associated with a trace array
 * @tr: The trace array to clear
 *
 * For each trigger, the triggering event has its tm_ref decremented
 * via trace_event_trigger_enable_disable(), and any associated event
 * (in the case of enable/disable_event triggers) will have its sm_ref
 * decremented via free()->trace_event_enable_disable().  That
 * combination effectively reverses the soft-mode/trigger state added
 * by trigger registration.
 *
 * Must be called with event_mutex held.
 */
void
clear_event_triggers(struct trace_array *tr)
{
	struct trace_event_file *file;

	list_for_each_entry(file, &tr->events, list) {
		struct event_trigger_data *data, *n;
		list_for_each_entry_safe(data, n, &file->triggers, list) {
			trace_event_trigger_enable_disable(file, 0);
			list_del_rcu(&data->list);
			if (data->cmd_ops->free)
				data->cmd_ops->free(data);
		}
	}
}

/**
 * update_cond_flag - Set or reset the TRIGGER_COND bit
 * @file: The trace_event_file associated with the event
 *
 * If an event has triggers and any of those triggers has a filter or
 * a post_trigger, trigger invocation needs to be deferred until after
 * the current event has logged its data, and the event should have
 * its TRIGGER_COND bit set, otherwise the TRIGGER_COND bit should be
 * cleared.
 */
void update_cond_flag(struct trace_event_file *file)
{
	struct event_trigger_data *data;
	bool set_cond = false;

	lockdep_assert_held(&event_mutex);

	list_for_each_entry(data, &file->triggers, list) {
		if (data->filter || event_command_post_trigger(data->cmd_ops) ||
		    event_command_needs_rec(data->cmd_ops)) {
			set_cond = true;
			break;
		}
	}

	if (set_cond)
		set_bit(EVENT_FILE_FL_TRIGGER_COND_BIT, &file->flags);
	else
		clear_bit(EVENT_FILE_FL_TRIGGER_COND_BIT, &file->flags);
}

/**
 * register_trigger - Generic event_command @reg implementation
 * @glob: The raw string used to register the trigger
 * @data: Trigger-specific data to associate with the trigger
 * @file: The trace_event_file associated with the event
 *
 * Common implementation for event trigger registration.
 *
 * Usually used directly as the @reg method in event command
 * implementations.
 *
 * Return: 0 on success, errno otherwise
 */
static int register_trigger(char *glob,
			    struct event_trigger_data *data,
			    struct trace_event_file *file)
{
	struct event_trigger_data *test;
	int ret = 0;

	lockdep_assert_held(&event_mutex);

	list_for_each_entry(test, &file->triggers, list) {
		if (test->cmd_ops->trigger_type == data->cmd_ops->trigger_type)
			return -EEXIST;
	}

	if (data->cmd_ops->init) {
		ret = data->cmd_ops->init(data);
		if (ret < 0)
			return ret;
	}

	list_add_rcu(&data->list, &file->triggers);

	update_cond_flag(file);
	ret = trace_event_trigger_enable_disable(file, 1);
	if (ret < 0) {
		list_del_rcu(&data->list);
		update_cond_flag(file);
	}
	return ret;
}

/*
 * True if the trigger was found and unregistered, else false.
 */
static bool try_unregister_trigger(char *glob,
				   struct event_trigger_data *test,
				   struct trace_event_file *file)
{
	struct event_trigger_data *data = NULL, *iter;

	lockdep_assert_held(&event_mutex);

	list_for_each_entry(iter, &file->triggers, list) {
		if (iter->cmd_ops->trigger_type == test->cmd_ops->trigger_type) {
			data = iter;
			list_del_rcu(&data->list);
			trace_event_trigger_enable_disable(file, 0);
			update_cond_flag(file);
			break;
		}
	}

	if (data) {
		if (data->cmd_ops->free)
			data->cmd_ops->free(data);

		return true;
	}

	return false;
}

/**
 * unregister_trigger - Generic event_command @unreg implementation
 * @glob: The raw string used to register the trigger
 * @test: Trigger-specific data used to find the trigger to remove
 * @file: The trace_event_file associated with the event
 *
 * Common implementation for event trigger unregistration.
 *
 * Usually used directly as the @unreg method in event command
 * implementations.
 */
static void unregister_trigger(char *glob,
			       struct event_trigger_data *test,
			       struct trace_event_file *file)
{
	try_unregister_trigger(glob, test, file);
}

/*
 * Event trigger parsing helper functions.
 *
 * These functions help make it easier to write an event trigger
 * parsing function i.e. the struct event_command.parse() callback
 * function responsible for parsing and registering a trigger command
 * written to the 'trigger' file.
 *
 * A trigger command (or just 'trigger' for short) takes the form:
 *   [trigger] [if filter]
 *
 * The struct event_command.parse() callback (and other struct
 * event_command functions) refer to several components of a trigger
 * command.  Those same components are referenced by the event trigger
 * parsing helper functions defined below.  These components are:
 *
 *   cmd               - the trigger command name
 *   glob              - the trigger command name optionally prefaced with '!'
 *   param_and_filter  - text following cmd and ':'
 *   param             - text following cmd and ':' and stripped of filter
 *   filter            - the optional filter text following (and including) 'if'
 *
 * To illustrate the use of these components, here are some concrete
 * examples. For the following triggers:
 *
 *   echo 'traceon:5 if pid == 0' > trigger
 *     - 'traceon' is both cmd and glob
 *     - '5 if pid == 0' is the param_and_filter
 *     - '5' is the param
 *     - 'if pid == 0' is the filter
 *
 *   echo 'enable_event:sys:event:n' > trigger
 *     - 'enable_event' is both cmd and glob
 *     - 'sys:event:n' is the param_and_filter
 *     - 'sys:event:n' is the param
 *     - there is no filter
 *
 *   echo 'hist:keys=pid if prio > 50' > trigger
 *     - 'hist' is both cmd and glob
 *     - 'keys=pid if prio > 50' is the param_and_filter
 *     - 'keys=pid' is the param
 *     - 'if prio > 50' is the filter
 *
 *   echo '!enable_event:sys:event:n' > trigger
 *     - 'enable_event' the cmd
 *     - '!enable_event' is the glob
 *     - 'sys:event:n' is the param_and_filter
 *     - 'sys:event:n' is the param
 *     - there is no filter
 *
 *   echo 'traceoff' > trigger
 *     - 'traceoff' is both cmd and glob
 *     - there is no param_and_filter
 *     - there is no param
 *     - there is no filter
 *
 * There are a few different categories of event trigger covered by
 * these helpers:
 *
 *  - triggers that don't require a parameter e.g. traceon
 *  - triggers that do require a parameter e.g. enable_event and hist
 *  - triggers that though they may not require a param may support an
 *    optional 'n' param (n = number of times the trigger should fire)
 *    e.g.: traceon:5 or enable_event:sys:event:n
 *  - triggers that do not support an 'n' param e.g. hist
 *
 * These functions can be used or ignored as necessary - it all
 * depends on the complexity of the trigger, and the granularity of
 * the functions supported reflects the fact that some implementations
 * may need to customize certain aspects of their implementations and
 * won't need certain functions.  For instance, the hist trigger
 * implementation doesn't use event_trigger_separate_filter() because
 * it has special requirements for handling the filter.
 */

/**
 * event_trigger_check_remove - check whether an event trigger specifies remove
 * @glob: The trigger command string, with optional remove(!) operator
 *
 * The event trigger callback implementations pass in 'glob' as a
 * parameter.  This is the command name either with or without a
 * remove(!)  operator.  This function simply parses the glob and
 * determines whether the command corresponds to a trigger removal or
 * a trigger addition.
 *
 * Return: true if this is a remove command, false otherwise
 */
bool event_trigger_check_remove(const char *glob)
{
	return (glob && glob[0] == '!') ? true : false;
}

/**
 * event_trigger_empty_param - check whether the param is empty
 * @param: The trigger param string
 *
 * The event trigger callback implementations pass in 'param' as a
 * parameter.  This corresponds to the string following the command
 * name minus the command name.  This function can be called by a
 * callback implementation for any command that requires a param; a
 * callback that doesn't require a param can ignore it.
 *
 * Return: true if this is an empty param, false otherwise
 */
bool event_trigger_empty_param(const char *param)
{
	return !param;
}

/**
 * event_trigger_separate_filter - separate an event trigger from a filter
 * @param_and_filter: String containing trigger and possibly filter
 * @param: outparam, will be filled with a pointer to the trigger
 * @filter: outparam, will be filled with a pointer to the filter
 * @param_required: Specifies whether or not the param string is required
 *
 * Given a param string of the form '[trigger] [if filter]', this
 * function separates the filter from the trigger and returns the
 * trigger in @param and the filter in @filter.  Either the @param
 * or the @filter may be set to NULL by this function - if not set to
 * NULL, they will contain strings corresponding to the trigger and
 * filter.
 *
 * There are two cases that need to be handled with respect to the
 * passed-in param: either the param is required, or it is not
 * required.  If @param_required is set, and there's no param, it will
 * return -EINVAL.  If @param_required is not set and there's a param
 * that starts with a number, that corresponds to the case of a
 * trigger with :n (n = number of times the trigger should fire) and
 * the parsing continues normally; otherwise the function just returns
 * and assumes param just contains a filter and there's nothing else
 * to do.
 *
 * Return: 0 on success, errno otherwise
 */
int event_trigger_separate_filter(char *param_and_filter, char **param,
				  char **filter, bool param_required)
{
	int ret = 0;

	*param = *filter = NULL;

	if (!param_and_filter) {
		if (param_required)
			ret = -EINVAL;
		return ret;
	}

	/*
	 * Here we check for an optional param. The only legal
	 * optional param is :n, and if that's the case, continue
	 * below. Otherwise we assume what's left is a filter and
	 * return it as the filter string for the caller to deal with.
	 */
	if (!param_required && param_and_filter && !isdigit(param_and_filter[0])) {
		*filter = param_and_filter;
		return ret;
	}

	/*
	 * Separate the param from the filter (param [if filter]).
	 * Here we have either an optional :n param or a required
	 * param and an optional filter.
	 */
	*param = strsep(&param_and_filter, " \t");

	/*
	 * Here we have a filter, though it may be empty.
	 */
	if (param_and_filter) {
		*filter = skip_spaces(param_and_filter);
		if (!**filter)
			*filter = NULL;
	}
	return ret;
}

/**
 * trigger_data_alloc - allocate and init event_trigger_data for a trigger
 * @cmd_ops: The event_command operations for the trigger
 * @cmd: The cmd string
 * @param: The param string
 * @private_data: User data to associate with the event trigger
 *
 * Allocate an event_trigger_data instance and initialize it.  The
 * @cmd_ops defines how the trigger will operate. If @param is set,
 * and @cmd_ops->trigger_ops->count_func is non NULL, then the
 * data->count is set to @param and before the trigger is executed, the
 * @cmd_ops->trigger_ops->count_func() is called. If that function returns
 * false, the @cmd_ops->trigger_ops->trigger() function will not be called.
 * @private_data can also be passed in and associated with the
 * event_trigger_data.
 *
 * Use trigger_data_free() to free an event_trigger_data object.
 *
 * Return: The trigger_data object success, NULL otherwise
 */
struct event_trigger_data *trigger_data_alloc(struct event_command *cmd_ops,
					      char *cmd,
					      char *param,
					      void *private_data)
{
	struct event_trigger_data *trigger_data;

	trigger_data = kzalloc(sizeof(*trigger_data), GFP_KERNEL);
	if (!trigger_data)
		return NULL;

	trigger_data->count = -1;
	trigger_data->cmd_ops = cmd_ops;
	trigger_data->private_data = private_data;
	if (param && cmd_ops->count_func)
		trigger_data->flags |= EVENT_TRIGGER_FL_COUNT;

	INIT_LIST_HEAD(&trigger_data->list);
	INIT_LIST_HEAD(&trigger_data->named_list);
	RCU_INIT_POINTER(trigger_data->filter, NULL);

	return trigger_data;
}

/**
 * event_trigger_parse_num - parse and return the number param for a trigger
 * @param: The param string
 * @trigger_data: The trigger_data for the trigger
 *
 * Parse the :n (n = number of times the trigger should fire) param
 * and set the count variable in the trigger_data to the parsed count.
 *
 * Return: 0 on success, errno otherwise
 */
int event_trigger_parse_num(char *param,
			    struct event_trigger_data *trigger_data)
{
	char *number;
	int ret = 0;

	if (param) {
		number = strsep(&param, ":");

		if (!strlen(number))
			return -EINVAL;

		/*
		 * We use the callback data field (which is a pointer)
		 * as our counter.
		 */
		ret = kstrtoul(number, 0, &trigger_data->count);
	}

	return ret;
}

/**
 * event_trigger_set_filter - set an event trigger's filter
 * @cmd_ops: The event_command operations for the trigger
 * @file: The event file for the trigger's event
 * @param: The string containing the filter
 * @trigger_data: The trigger_data for the trigger
 *
 * Set the filter for the trigger.  If the filter is NULL, just return
 * without error.
 *
 * Return: 0 on success, errno otherwise
 */
int event_trigger_set_filter(struct event_command *cmd_ops,
			     struct trace_event_file *file,
			     char *param,
			     struct event_trigger_data *trigger_data)
{
	if (param && cmd_ops->set_filter)
		return cmd_ops->set_filter(param, trigger_data, file);

	return 0;
}

/**
 * event_trigger_reset_filter - reset an event trigger's filter
 * @cmd_ops: The event_command operations for the trigger
 * @trigger_data: The trigger_data for the trigger
 *
 * Reset the filter for the trigger to no filter.
 */
void event_trigger_reset_filter(struct event_command *cmd_ops,
				struct event_trigger_data *trigger_data)
{
	if (cmd_ops->set_filter)
		cmd_ops->set_filter(NULL, trigger_data, NULL);
}

/**
 * event_trigger_register - register an event trigger
 * @cmd_ops: The event_command operations for the trigger
 * @file: The event file for the trigger's event
 * @glob: The trigger command string, with optional remove(!) operator
 * @trigger_data: The trigger_data for the trigger
 *
 * Register an event trigger.  The @cmd_ops are used to call the
 * cmd_ops->reg() function which actually does the registration.
 *
 * Return: 0 on success, errno otherwise
 */
int event_trigger_register(struct event_command *cmd_ops,
			   struct trace_event_file *file,
			   char *glob,
			   struct event_trigger_data *trigger_data)
{
	return cmd_ops->reg(glob, trigger_data, file);
}

/**
 * event_trigger_unregister - unregister an event trigger
 * @cmd_ops: The event_command operations for the trigger
 * @file: The event file for the trigger's event
 * @glob: The trigger command string, with optional remove(!) operator
 * @trigger_data: The trigger_data for the trigger
 *
 * Unregister an event trigger.  The @cmd_ops are used to call the
 * cmd_ops->unreg() function which actually does the unregistration.
 */
void event_trigger_unregister(struct event_command *cmd_ops,
			      struct trace_event_file *file,
			      char *glob,
			      struct event_trigger_data *trigger_data)
{
	cmd_ops->unreg(glob, trigger_data, file);
}

/*
 * End event trigger parsing helper functions.
 */

/**
 * event_trigger_parse - Generic event_command @parse implementation
 * @cmd_ops: The command ops, used for trigger registration
 * @file: The trace_event_file associated with the event
 * @glob: The raw string used to register the trigger
 * @cmd: The cmd portion of the string used to register the trigger
 * @param_and_filter: The param and filter portion of the string used to register the trigger
 *
 * Common implementation for event command parsing and trigger
 * instantiation.
 *
 * Usually used directly as the @parse method in event command
 * implementations.
 *
 * Return: 0 on success, errno otherwise
 */
static int
event_trigger_parse(struct event_command *cmd_ops,
		    struct trace_event_file *file,
		    char *glob, char *cmd, char *param_and_filter)
{
	struct event_trigger_data *trigger_data;
	char *param, *filter;
	bool remove;
	int ret;

	remove = event_trigger_check_remove(glob);

	ret = event_trigger_separate_filter(param_and_filter, &param, &filter, false);
	if (ret)
		return ret;

	ret = -ENOMEM;
	trigger_data = trigger_data_alloc(cmd_ops, cmd, param, file);
	if (!trigger_data)
		return ret;

	if (remove) {
		event_trigger_unregister(cmd_ops, file, glob+1, trigger_data);
		trigger_data_free(trigger_data);
		return 0;
	}

	ret = event_trigger_parse_num(param, trigger_data);
	if (ret)
		goto out_free;

	ret = event_trigger_set_filter(cmd_ops, file, filter, trigger_data);
	if (ret < 0)
		goto out_free;

	/* Up the trigger_data count to make sure reg doesn't free it on failure */
	event_trigger_init(trigger_data);

	ret = event_trigger_register(cmd_ops, file, glob, trigger_data);
	if (ret)
		goto out_free;

	/* Down the counter of trigger_data or free it if not used anymore */
	event_trigger_free(trigger_data);
	return ret;

 out_free:
	event_trigger_reset_filter(cmd_ops, trigger_data);
	trigger_data_free(trigger_data);
	return ret;
}

/**
 * set_trigger_filter - Generic event_command @set_filter implementation
 * @filter_str: The filter string for the trigger, NULL to remove filter
 * @trigger_data: Trigger-specific data
 * @file: The trace_event_file associated with the event
 *
 * Common implementation for event command filter parsing and filter
 * instantiation.
 *
 * Usually used directly as the @set_filter method in event command
 * implementations.
 *
 * Also used to remove a filter (if filter_str = NULL).
 *
 * Return: 0 on success, errno otherwise
 */
int set_trigger_filter(char *filter_str,
		       struct event_trigger_data *trigger_data,
		       struct trace_event_file *file)
{
	struct event_trigger_data *data = trigger_data;
	struct event_filter *filter = NULL, *tmp;
	int ret = -EINVAL;
	char *s;

	if (!filter_str) /* clear the current filter */
		goto assign;

	s = strsep(&filter_str, " \t");

	if (!strlen(s) || strcmp(s, "if") != 0)
		return ret;

	if (!filter_str)
		return ret;

	/* The filter is for the 'trigger' event, not the triggered event */
	ret = create_event_filter(file->tr, file->event_call,
				  filter_str, true, &filter);

	/* Only enabled set_str for error handling */
	if (filter) {
		kfree(filter->filter_string);
		filter->filter_string = NULL;
	}

	/*
	 * If create_event_filter() fails, filter still needs to be freed.
	 * Which the calling code will do with data->filter.
	 */
 assign:
	tmp = rcu_access_pointer(data->filter);

	rcu_assign_pointer(data->filter, filter);

	if (tmp) {
		/*
		 * Make sure the call is done with the filter.
		 * It is possible that a filter could fail at boot up,
		 * and then this path will be called. Avoid the synchronization
		 * in that case.
		 */
		if (system_state != SYSTEM_BOOTING)
			tracepoint_synchronize_unregister();
		free_event_filter(tmp);
	}

	kfree(data->filter_str);
	data->filter_str = NULL;

	if (filter_str) {
		data->filter_str = kstrdup(filter_str, GFP_KERNEL);
		if (!data->filter_str) {
			free_event_filter(rcu_access_pointer(data->filter));
			data->filter = NULL;
			ret = -ENOMEM;
		}
	}
	return ret;
}

static LIST_HEAD(named_triggers);

/**
 * find_named_trigger - Find the common named trigger associated with @name
 * @name: The name of the set of named triggers to find the common data for
 *
 * Named triggers are sets of triggers that share a common set of
 * trigger data.  The first named trigger registered with a given name
 * owns the common trigger data that the others subsequently
 * registered with the same name will reference.  This function
 * returns the common trigger data associated with that first
 * registered instance.
 *
 * Return: the common trigger data for the given named trigger on
 * success, NULL otherwise.
 */
struct event_trigger_data *find_named_trigger(const char *name)
{
	struct event_trigger_data *data;

	if (!name)
		return NULL;

	list_for_each_entry(data, &named_triggers, named_list) {
		if (data->named_data)
			continue;
		if (strcmp(data->name, name) == 0)
			return data;
	}

	return NULL;
}

/**
 * is_named_trigger - determine if a given trigger is a named trigger
 * @test: The trigger data to test
 *
 * Return: true if 'test' is a named trigger, false otherwise.
 */
bool is_named_trigger(struct event_trigger_data *test)
{
	struct event_trigger_data *data;

	list_for_each_entry(data, &named_triggers, named_list) {
		if (test == data)
			return true;
	}

	return false;
}

/**
 * save_named_trigger - save the trigger in the named trigger list
 * @name: The name of the named trigger set
 * @data: The trigger data to save
 *
 * Return: 0 if successful, negative error otherwise.
 */
int save_named_trigger(const char *name, struct event_trigger_data *data)
{
	data->name = kstrdup(name, GFP_KERNEL);
	if (!data->name)
		return -ENOMEM;

	list_add(&data->named_list, &named_triggers);

	return 0;
}

/**
 * del_named_trigger - delete a trigger from the named trigger list
 * @data: The trigger data to delete
 */
void del_named_trigger(struct event_trigger_data *data)
{
	kfree(data->name);
	data->name = NULL;

	list_del(&data->named_list);
}

static void __pause_named_trigger(struct event_trigger_data *data, bool pause)
{
	struct event_trigger_data *test;

	list_for_each_entry(test, &named_triggers, named_list) {
		if (strcmp(test->name, data->name) == 0) {
			if (pause) {
				test->paused_tmp = test->paused;
				test->paused = true;
			} else {
				test->paused = test->paused_tmp;
			}
		}
	}
}

/**
 * pause_named_trigger - Pause all named triggers with the same name
 * @data: The trigger data of a named trigger to pause
 *
 * Pauses a named trigger along with all other triggers having the
 * same name.  Because named triggers share a common set of data,
 * pausing only one is meaningless, so pausing one named trigger needs
 * to pause all triggers with the same name.
 */
void pause_named_trigger(struct event_trigger_data *data)
{
	__pause_named_trigger(data, true);
}

/**
 * unpause_named_trigger - Un-pause all named triggers with the same name
 * @data: The trigger data of a named trigger to unpause
 *
 * Un-pauses a named trigger along with all other triggers having the
 * same name.  Because named triggers share a common set of data,
 * unpausing only one is meaningless, so unpausing one named trigger
 * needs to unpause all triggers with the same name.
 */
void unpause_named_trigger(struct event_trigger_data *data)
{
	__pause_named_trigger(data, false);
}

/**
 * set_named_trigger_data - Associate common named trigger data
 * @data: The trigger data to associate
 * @named_data: The common named trigger to be associated
 *
 * Named triggers are sets of triggers that share a common set of
 * trigger data.  The first named trigger registered with a given name
 * owns the common trigger data that the others subsequently
 * registered with the same name will reference.  This function
 * associates the common trigger data from the first trigger with the
 * given trigger.
 */
void set_named_trigger_data(struct event_trigger_data *data,
			    struct event_trigger_data *named_data)
{
	data->named_data = named_data;
}

struct event_trigger_data *
get_named_trigger_data(struct event_trigger_data *data)
{
	return data->named_data;
}

static void
traceon_trigger(struct event_trigger_data *data,
		struct trace_buffer *buffer, void *rec,
		struct ring_buffer_event *event)
{
	struct trace_event_file *file = data->private_data;

	if (file) {
		if (tracer_tracing_is_on(file->tr))
			return;

		tracer_tracing_on(file->tr);
		return;
	}

	if (tracing_is_on())
		return;

	tracing_on();
}

static bool
traceon_count_func(struct event_trigger_data *data,
		   struct trace_buffer *buffer, void *rec,
		   struct ring_buffer_event *event)
{
	struct trace_event_file *file = data->private_data;

	if (file) {
		if (tracer_tracing_is_on(file->tr))
			return false;
	} else {
		if (tracing_is_on())
			return false;
	}

	if (!data->count)
		return false;

	if (data->count != -1)
		(data->count)--;

	return true;
}

static void
traceoff_trigger(struct event_trigger_data *data,
		 struct trace_buffer *buffer, void *rec,
		 struct ring_buffer_event *event)
{
	struct trace_event_file *file = data->private_data;

	if (file) {
		if (!tracer_tracing_is_on(file->tr))
			return;

		tracer_tracing_off(file->tr);
		return;
	}

	if (!tracing_is_on())
		return;

	tracing_off();
}

static bool
traceoff_count_func(struct event_trigger_data *data,
		    struct trace_buffer *buffer, void *rec,
		    struct ring_buffer_event *event)
{
	struct trace_event_file *file = data->private_data;

	if (file) {
		if (!tracer_tracing_is_on(file->tr))
			return false;
	} else {
		if (!tracing_is_on())
			return false;
	}

	if (!data->count)
		return false;

	if (data->count != -1)
		(data->count)--;

	return true;
}

static int
traceon_trigger_print(struct seq_file *m, struct event_trigger_data *data)
{
	return event_trigger_print("traceon", m, (void *)data->count,
				   data->filter_str);
}

static int
traceoff_trigger_print(struct seq_file *m, struct event_trigger_data *data)
{
	return event_trigger_print("traceoff", m, (void *)data->count,
				   data->filter_str);
}

static struct event_command trigger_traceon_cmd = {
	.name			= "traceon",
	.trigger_type		= ETT_TRACE_ONOFF,
	.parse			= event_trigger_parse,
	.reg			= register_trigger,
	.unreg			= unregister_trigger,
	.set_filter		= set_trigger_filter,
	.trigger		= traceon_trigger,
	.count_func		= traceon_count_func,
	.print			= traceon_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_command trigger_traceoff_cmd = {
	.name			= "traceoff",
	.trigger_type		= ETT_TRACE_ONOFF,
	.flags			= EVENT_CMD_FL_POST_TRIGGER,
	.parse			= event_trigger_parse,
	.reg			= register_trigger,
	.unreg			= unregister_trigger,
	.set_filter		= set_trigger_filter,
	.trigger		= traceoff_trigger,
	.count_func		= traceoff_count_func,
	.print			= traceoff_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

#ifdef CONFIG_TRACER_SNAPSHOT
static void
snapshot_trigger(struct event_trigger_data *data,
		 struct trace_buffer *buffer, void *rec,
		 struct ring_buffer_event *event)
{
	struct trace_event_file *file = data->private_data;

	if (file)
		tracing_snapshot_instance(file->tr);
	else
		tracing_snapshot();
}

static int
register_snapshot_trigger(char *glob,
			  struct event_trigger_data *data,
			  struct trace_event_file *file)
{
	int ret = tracing_arm_snapshot(file->tr);

	if (ret < 0)
		return ret;

	ret = register_trigger(glob, data, file);
	if (ret < 0)
		tracing_disarm_snapshot(file->tr);
	return ret;
}

static void unregister_snapshot_trigger(char *glob,
					struct event_trigger_data *data,
					struct trace_event_file *file)
{
	if (try_unregister_trigger(glob, data, file))
		tracing_disarm_snapshot(file->tr);
}

static int
snapshot_trigger_print(struct seq_file *m, struct event_trigger_data *data)
{
	return event_trigger_print("snapshot", m, (void *)data->count,
				   data->filter_str);
}

static struct event_command trigger_snapshot_cmd = {
	.name			= "snapshot",
	.trigger_type		= ETT_SNAPSHOT,
	.parse			= event_trigger_parse,
	.reg			= register_snapshot_trigger,
	.unreg			= unregister_snapshot_trigger,
	.set_filter		= set_trigger_filter,
	.trigger		= snapshot_trigger,
	.count_func		= event_trigger_count,
	.print			= snapshot_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static __init int register_trigger_snapshot_cmd(void)
{
	int ret;

	ret = register_event_command(&trigger_snapshot_cmd);
	WARN_ON(ret < 0);

	return ret;
}
#else
static __init int register_trigger_snapshot_cmd(void) { return 0; }
#endif /* CONFIG_TRACER_SNAPSHOT */

#ifdef CONFIG_STACKTRACE
#ifdef CONFIG_UNWINDER_ORC
/* Skip 2:
 *   event_triggers_post_call()
 *   trace_event_raw_event_xxx()
 */
# define STACK_SKIP 2
#else
/*
 * Skip 4:
 *   stacktrace_trigger()
 *   event_triggers_post_call()
 *   trace_event_buffer_commit()
 *   trace_event_raw_event_xxx()
 */
#define STACK_SKIP 4
#endif

static void
stacktrace_trigger(struct event_trigger_data *data,
		   struct trace_buffer *buffer,  void *rec,
		   struct ring_buffer_event *event)
{
	struct trace_event_file *file = data->private_data;

	if (file)
		__trace_stack(file->tr, tracing_gen_ctx_dec(), STACK_SKIP);
	else
		trace_dump_stack(STACK_SKIP);
}

static int
stacktrace_trigger_print(struct seq_file *m, struct event_trigger_data *data)
{
	return event_trigger_print("stacktrace", m, (void *)data->count,
				   data->filter_str);
}

static struct event_command trigger_stacktrace_cmd = {
	.name			= "stacktrace",
	.trigger_type		= ETT_STACKTRACE,
	.flags			= EVENT_CMD_FL_POST_TRIGGER,
	.parse			= event_trigger_parse,
	.reg			= register_trigger,
	.unreg			= unregister_trigger,
	.set_filter		= set_trigger_filter,
	.trigger		= stacktrace_trigger,
	.count_func		= event_trigger_count,
	.print			= stacktrace_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static __init int register_trigger_stacktrace_cmd(void)
{
	int ret;

	ret = register_event_command(&trigger_stacktrace_cmd);
	WARN_ON(ret < 0);

	return ret;
}
#else
static __init int register_trigger_stacktrace_cmd(void) { return 0; }
#endif /* CONFIG_STACKTRACE */

static __init void unregister_trigger_traceon_traceoff_cmds(void)
{
	unregister_event_command(&trigger_traceon_cmd);
	unregister_event_command(&trigger_traceoff_cmd);
}

static void
event_enable_trigger(struct event_trigger_data *data,
		     struct trace_buffer *buffer,  void *rec,
		     struct ring_buffer_event *event)
{
	struct enable_trigger_data *enable_data = data->private_data;

	if (enable_data->enable)
		clear_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &enable_data->file->flags);
	else
		set_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &enable_data->file->flags);
}

static bool
event_enable_count_func(struct event_trigger_data *data,
			struct trace_buffer *buffer,  void *rec,
			struct ring_buffer_event *event)
{
	struct enable_trigger_data *enable_data = data->private_data;

	if (!data->count)
		return false;

	/* Skip if the event is in a state we want to switch to */
	if (enable_data->enable == !(enable_data->file->flags & EVENT_FILE_FL_SOFT_DISABLED))
		return false;

	if (data->count != -1)
		(data->count)--;

	return true;
}

int event_enable_trigger_print(struct seq_file *m,
			       struct event_trigger_data *data)
{
	struct enable_trigger_data *enable_data = data->private_data;

	seq_printf(m, "%s:%s:%s",
		   enable_data->hist ?
		   (enable_data->enable ? ENABLE_HIST_STR : DISABLE_HIST_STR) :
		   (enable_data->enable ? ENABLE_EVENT_STR : DISABLE_EVENT_STR),
		   enable_data->file->event_call->class->system,
		   trace_event_name(enable_data->file->event_call));

	if (data->count == -1)
		seq_puts(m, ":unlimited");
	else
		seq_printf(m, ":count=%ld", data->count);

	if (data->filter_str)
		seq_printf(m, " if %s\n", data->filter_str);
	else
		seq_putc(m, '\n');

	return 0;
}

void event_enable_trigger_free(struct event_trigger_data *data)
{
	struct enable_trigger_data *enable_data = data->private_data;

	if (WARN_ON_ONCE(data->ref <= 0))
		return;

	data->ref--;
	if (!data->ref) {
		/* Remove the SOFT_MODE flag */
		trace_event_enable_disable(enable_data->file, 0, 1);
		trace_event_put_ref(enable_data->file->event_call);
		trigger_data_free(data);
		kfree(enable_data);
	}
}

int event_enable_trigger_parse(struct event_command *cmd_ops,
			       struct trace_event_file *file,
			       char *glob, char *cmd, char *param_and_filter)
{
	struct trace_event_file *event_enable_file;
	struct enable_trigger_data *enable_data;
	struct event_trigger_data *trigger_data;
	struct trace_array *tr = file->tr;
	char *param, *filter;
	bool enable, remove;
	const char *system;
	const char *event;
	bool hist = false;
	int ret;

	remove = event_trigger_check_remove(glob);

	if (event_trigger_empty_param(param_and_filter))
		return -EINVAL;

	ret = event_trigger_separate_filter(param_and_filter, &param, &filter, true);
	if (ret)
		return ret;

	system = strsep(&param, ":");
	if (!param)
		return -EINVAL;

	event = strsep(&param, ":");

	ret = -EINVAL;
	event_enable_file = find_event_file(tr, system, event);
	if (!event_enable_file)
		return ret;

#ifdef CONFIG_HIST_TRIGGERS
	hist = ((strcmp(cmd, ENABLE_HIST_STR) == 0) ||
		(strcmp(cmd, DISABLE_HIST_STR) == 0));

	enable = ((strcmp(cmd, ENABLE_EVENT_STR) == 0) ||
		  (strcmp(cmd, ENABLE_HIST_STR) == 0));
#else
	enable = strcmp(cmd, ENABLE_EVENT_STR) == 0;
#endif
	ret = -ENOMEM;

	enable_data = kzalloc(sizeof(*enable_data), GFP_KERNEL);
	if (!enable_data)
		return ret;

	enable_data->hist = hist;
	enable_data->enable = enable;
	enable_data->file = event_enable_file;

	trigger_data = trigger_data_alloc(cmd_ops, cmd, param, enable_data);
	if (!trigger_data) {
		kfree(enable_data);
		return ret;
	}

	if (remove) {
		event_trigger_unregister(cmd_ops, file, glob+1, trigger_data);
		kfree(trigger_data);
		kfree(enable_data);
		ret = 0;
		return ret;
	}

	/* Up the trigger_data count to make sure nothing frees it on failure */
	event_trigger_init(trigger_data);

	ret = event_trigger_parse_num(param, trigger_data);
	if (ret)
		goto out_free;

	ret = event_trigger_set_filter(cmd_ops, file, filter, trigger_data);
	if (ret < 0)
		goto out_free;

	/* Don't let event modules unload while probe registered */
	ret = trace_event_try_get_ref(event_enable_file->event_call);
	if (!ret) {
		ret = -EBUSY;
		goto out_free;
	}

	ret = trace_event_enable_disable(event_enable_file, 1, 1);
	if (ret < 0)
		goto out_put;

	ret = event_trigger_register(cmd_ops, file, glob, trigger_data);
	if (ret)
		goto out_disable;

	event_trigger_free(trigger_data);
	return ret;
 out_disable:
	trace_event_enable_disable(event_enable_file, 0, 1);
 out_put:
	trace_event_put_ref(event_enable_file->event_call);
 out_free:
	event_trigger_reset_filter(cmd_ops, trigger_data);
	event_trigger_free(trigger_data);
	kfree(enable_data);

	return ret;
}

int event_enable_register_trigger(char *glob,
				  struct event_trigger_data *data,
				  struct trace_event_file *file)
{
	struct enable_trigger_data *enable_data = data->private_data;
	struct enable_trigger_data *test_enable_data;
	struct event_trigger_data *test;
	int ret = 0;

	lockdep_assert_held(&event_mutex);

	list_for_each_entry(test, &file->triggers, list) {
		test_enable_data = test->private_data;
		if (test_enable_data &&
		    (test->cmd_ops->trigger_type ==
		     data->cmd_ops->trigger_type) &&
		    (test_enable_data->file == enable_data->file)) {
			return -EEXIST;
		}
	}

	if (data->cmd_ops->init) {
		ret = data->cmd_ops->init(data);
		if (ret < 0)
			return ret;
	}

	list_add_rcu(&data->list, &file->triggers);

	update_cond_flag(file);
	ret = trace_event_trigger_enable_disable(file, 1);
	if (ret < 0) {
		list_del_rcu(&data->list);
		update_cond_flag(file);
	}
	return ret;
}

void event_enable_unregister_trigger(char *glob,
				     struct event_trigger_data *test,
				     struct trace_event_file *file)
{
	struct enable_trigger_data *test_enable_data = test->private_data;
	struct event_trigger_data *data = NULL, *iter;
	struct enable_trigger_data *enable_data;

	lockdep_assert_held(&event_mutex);

	list_for_each_entry(iter, &file->triggers, list) {
		enable_data = iter->private_data;
		if (enable_data &&
		    (iter->cmd_ops->trigger_type ==
		     test->cmd_ops->trigger_type) &&
		    (enable_data->file == test_enable_data->file)) {
			data = iter;
			list_del_rcu(&data->list);
			trace_event_trigger_enable_disable(file, 0);
			update_cond_flag(file);
			break;
		}
	}

	if (data && data->cmd_ops->free)
		data->cmd_ops->free(data);
}

static struct event_command trigger_enable_cmd = {
	.name			= ENABLE_EVENT_STR,
	.trigger_type		= ETT_EVENT_ENABLE,
	.parse			= event_enable_trigger_parse,
	.reg			= event_enable_register_trigger,
	.unreg			= event_enable_unregister_trigger,
	.set_filter		= set_trigger_filter,
	.trigger		= event_enable_trigger,
	.count_func		= event_enable_count_func,
	.print			= event_enable_trigger_print,
	.init			= event_trigger_init,
	.free			= event_enable_trigger_free,
};

static struct event_command trigger_disable_cmd = {
	.name			= DISABLE_EVENT_STR,
	.trigger_type		= ETT_EVENT_ENABLE,
	.parse			= event_enable_trigger_parse,
	.reg			= event_enable_register_trigger,
	.unreg			= event_enable_unregister_trigger,
	.set_filter		= set_trigger_filter,
	.trigger		= event_enable_trigger,
	.count_func		= event_enable_count_func,
	.print			= event_enable_trigger_print,
	.init			= event_trigger_init,
	.free			= event_enable_trigger_free,
};

static __init void unregister_trigger_enable_disable_cmds(void)
{
	unregister_event_command(&trigger_enable_cmd);
	unregister_event_command(&trigger_disable_cmd);
}

static __init int register_trigger_enable_disable_cmds(void)
{
	int ret;

	ret = register_event_command(&trigger_enable_cmd);
	if (WARN_ON(ret < 0))
		return ret;
	ret = register_event_command(&trigger_disable_cmd);
	if (WARN_ON(ret < 0))
		unregister_trigger_enable_disable_cmds();

	return ret;
}

static __init int register_trigger_traceon_traceoff_cmds(void)
{
	int ret;

	ret = register_event_command(&trigger_traceon_cmd);
	if (WARN_ON(ret < 0))
		return ret;
	ret = register_event_command(&trigger_traceoff_cmd);
	if (WARN_ON(ret < 0))
		unregister_trigger_traceon_traceoff_cmds();

	return ret;
}

__init int register_trigger_cmds(void)
{
	register_trigger_traceon_traceoff_cmds();
	register_trigger_snapshot_cmd();
	register_trigger_stacktrace_cmd();
	register_trigger_enable_disable_cmds();
	register_trigger_hist_enable_disable_cmds();
	register_trigger_hist_cmd();

	return 0;
}
