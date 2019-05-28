// SPDX-License-Identifier: GPL-2.0
/*
 * trace_events_trigger - trace event triggers
 *
 * Copyright (C) 2013 Tom Zanussi <tom.zanussi@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/rculist.h>

#include "trace.h"

static LIST_HEAD(trigger_commands);
static DEFINE_MUTEX(trigger_cmd_mutex);

void trigger_data_free(struct event_trigger_data *data)
{
	if (data->cmd_ops->set_filter)
		data->cmd_ops->set_filter(NULL, data, NULL);

	/* make sure current triggers exit before free */
	tracepoint_synchronize_unregister();

	kfree(data);
}

/**
 * event_triggers_call - Call triggers associated with a trace event
 * @file: The trace_event_file associated with the event
 * @rec: The trace entry for the event, NULL for unconditional invocation
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
event_triggers_call(struct trace_event_file *file, void *rec,
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
			data->ops->func(data, rec, event);
			continue;
		}
		filter = rcu_dereference_sched(data->filter);
		if (filter && !filter_match_preds(filter, rec))
			continue;
		if (event_command_post_trigger(data->cmd_ops)) {
			tt |= data->cmd_ops->trigger_type;
			continue;
		}
		data->ops->func(data, rec, event);
	}
	return tt;
}
EXPORT_SYMBOL_GPL(event_triggers_call);

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
			data->ops->func(data, NULL, NULL);
	}
}
EXPORT_SYMBOL_GPL(event_triggers_post_call);

#define SHOW_AVAILABLE_TRIGGERS	(void *)(1UL)

static void *trigger_next(struct seq_file *m, void *t, loff_t *pos)
{
	struct trace_event_file *event_file = event_file_data(m->private);

	if (t == SHOW_AVAILABLE_TRIGGERS)
		return NULL;

	return seq_list_next(t, &event_file->triggers, pos);
}

static void *trigger_start(struct seq_file *m, loff_t *pos)
{
	struct trace_event_file *event_file;

	/* ->stop() is called even if ->start() fails */
	mutex_lock(&event_mutex);
	event_file = event_file_data(m->private);
	if (unlikely(!event_file))
		return ERR_PTR(-ENODEV);

	if (list_empty(&event_file->triggers))
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
	data->ops->print(m, data->ops, data);

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
	int ret = 0;

	mutex_lock(&event_mutex);

	if (unlikely(!event_file_data(file))) {
		mutex_unlock(&event_mutex);
		return -ENODEV;
	}

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

	mutex_unlock(&event_mutex);

	return ret;
}

static int trigger_process_regex(struct trace_event_file *file, char *buff)
{
	char *command, *next = buff;
	struct event_command *p;
	int ret = -EINVAL;

	command = strsep(&next, ": \t");
	command = (command[0] != '!') ? command : command + 1;

	mutex_lock(&trigger_cmd_mutex);
	list_for_each_entry(p, &trigger_commands, list) {
		if (strcmp(p->name, command) == 0) {
			ret = p->func(p, file, buff, command, next);
			goto out_unlock;
		}
	}
 out_unlock:
	mutex_unlock(&trigger_cmd_mutex);

	return ret;
}

static ssize_t event_trigger_regex_write(struct file *file,
					 const char __user *ubuf,
					 size_t cnt, loff_t *ppos)
{
	struct trace_event_file *event_file;
	ssize_t ret;
	char *buf;

	if (!cnt)
		return 0;

	if (cnt >= PAGE_SIZE)
		return -EINVAL;

	buf = memdup_user_nul(ubuf, cnt);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	strim(buf);

	mutex_lock(&event_mutex);
	event_file = event_file_data(file);
	if (unlikely(!event_file)) {
		mutex_unlock(&event_mutex);
		kfree(buf);
		return -ENODEV;
	}
	ret = trigger_process_regex(event_file, buf);
	mutex_unlock(&event_mutex);

	kfree(buf);
	if (ret < 0)
		goto out;

	*ppos += cnt;
	ret = cnt;
 out:
	return ret;
}

static int event_trigger_regex_release(struct inode *inode, struct file *file)
{
	mutex_lock(&event_mutex);

	if (file->f_mode & FMODE_READ)
		seq_release(inode, file);

	mutex_unlock(&event_mutex);

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
	int ret = 0;

	mutex_lock(&trigger_cmd_mutex);
	list_for_each_entry(p, &trigger_commands, list) {
		if (strcmp(cmd->name, p->name) == 0) {
			ret = -EBUSY;
			goto out_unlock;
		}
	}
	list_add(&cmd->list, &trigger_commands);
 out_unlock:
	mutex_unlock(&trigger_cmd_mutex);

	return ret;
}

/*
 * Currently we only unregister event commands from __init, so mark
 * this __init too.
 */
__init int unregister_event_command(struct event_command *cmd)
{
	struct event_command *p, *n;
	int ret = -ENODEV;

	mutex_lock(&trigger_cmd_mutex);
	list_for_each_entry_safe(p, n, &trigger_commands, list) {
		if (strcmp(cmd->name, p->name) == 0) {
			ret = 0;
			list_del_init(&p->list);
			goto out_unlock;
		}
	}
 out_unlock:
	mutex_unlock(&trigger_cmd_mutex);

	return ret;
}

/**
 * event_trigger_print - Generic event_trigger_ops @print implementation
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
 * event_trigger_init - Generic event_trigger_ops @init implementation
 * @ops: The trigger ops associated with the trigger
 * @data: Trigger-specific data
 *
 * Common implementation of event trigger initialization.
 *
 * Usually used directly as the @init method in event trigger
 * implementations.
 *
 * Return: 0 on success, errno otherwise
 */
int event_trigger_init(struct event_trigger_ops *ops,
		       struct event_trigger_data *data)
{
	data->ref++;
	return 0;
}

/**
 * event_trigger_free - Generic event_trigger_ops @free implementation
 * @ops: The trigger ops associated with the trigger
 * @data: Trigger-specific data
 *
 * Common implementation of event trigger de-initialization.
 *
 * Usually used directly as the @free method in event trigger
 * implementations.
 */
static void
event_trigger_free(struct event_trigger_ops *ops,
		   struct event_trigger_data *data)
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
			if (data->ops->free)
				data->ops->free(data->ops, data);
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

	list_for_each_entry_rcu(data, &file->triggers, list) {
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
 * @ops: The trigger ops associated with the trigger
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
static int register_trigger(char *glob, struct event_trigger_ops *ops,
			    struct event_trigger_data *data,
			    struct trace_event_file *file)
{
	struct event_trigger_data *test;
	int ret = 0;

	list_for_each_entry_rcu(test, &file->triggers, list) {
		if (test->cmd_ops->trigger_type == data->cmd_ops->trigger_type) {
			ret = -EEXIST;
			goto out;
		}
	}

	if (data->ops->init) {
		ret = data->ops->init(data->ops, data);
		if (ret < 0)
			goto out;
	}

	list_add_rcu(&data->list, &file->triggers);
	ret++;

	update_cond_flag(file);
	if (trace_event_trigger_enable_disable(file, 1) < 0) {
		list_del_rcu(&data->list);
		update_cond_flag(file);
		ret--;
	}
out:
	return ret;
}

/**
 * unregister_trigger - Generic event_command @unreg implementation
 * @glob: The raw string used to register the trigger
 * @ops: The trigger ops associated with the trigger
 * @test: Trigger-specific data used to find the trigger to remove
 * @file: The trace_event_file associated with the event
 *
 * Common implementation for event trigger unregistration.
 *
 * Usually used directly as the @unreg method in event command
 * implementations.
 */
static void unregister_trigger(char *glob, struct event_trigger_ops *ops,
			       struct event_trigger_data *test,
			       struct trace_event_file *file)
{
	struct event_trigger_data *data;
	bool unregistered = false;

	list_for_each_entry_rcu(data, &file->triggers, list) {
		if (data->cmd_ops->trigger_type == test->cmd_ops->trigger_type) {
			unregistered = true;
			list_del_rcu(&data->list);
			trace_event_trigger_enable_disable(file, 0);
			update_cond_flag(file);
			break;
		}
	}

	if (unregistered && data->ops->free)
		data->ops->free(data->ops, data);
}

/**
 * event_trigger_callback - Generic event_command @func implementation
 * @cmd_ops: The command ops, used for trigger registration
 * @file: The trace_event_file associated with the event
 * @glob: The raw string used to register the trigger
 * @cmd: The cmd portion of the string used to register the trigger
 * @param: The params portion of the string used to register the trigger
 *
 * Common implementation for event command parsing and trigger
 * instantiation.
 *
 * Usually used directly as the @func method in event command
 * implementations.
 *
 * Return: 0 on success, errno otherwise
 */
static int
event_trigger_callback(struct event_command *cmd_ops,
		       struct trace_event_file *file,
		       char *glob, char *cmd, char *param)
{
	struct event_trigger_data *trigger_data;
	struct event_trigger_ops *trigger_ops;
	char *trigger = NULL;
	char *number;
	int ret;

	/* separate the trigger from the filter (t:n [if filter]) */
	if (param && isdigit(param[0]))
		trigger = strsep(&param, " \t");

	trigger_ops = cmd_ops->get_trigger_ops(cmd, trigger);

	ret = -ENOMEM;
	trigger_data = kzalloc(sizeof(*trigger_data), GFP_KERNEL);
	if (!trigger_data)
		goto out;

	trigger_data->count = -1;
	trigger_data->ops = trigger_ops;
	trigger_data->cmd_ops = cmd_ops;
	trigger_data->private_data = file;
	INIT_LIST_HEAD(&trigger_data->list);
	INIT_LIST_HEAD(&trigger_data->named_list);

	if (glob[0] == '!') {
		cmd_ops->unreg(glob+1, trigger_ops, trigger_data, file);
		kfree(trigger_data);
		ret = 0;
		goto out;
	}

	if (trigger) {
		number = strsep(&trigger, ":");

		ret = -EINVAL;
		if (!strlen(number))
			goto out_free;

		/*
		 * We use the callback data field (which is a pointer)
		 * as our counter.
		 */
		ret = kstrtoul(number, 0, &trigger_data->count);
		if (ret)
			goto out_free;
	}

	if (!param) /* if param is non-empty, it's supposed to be a filter */
		goto out_reg;

	if (!cmd_ops->set_filter)
		goto out_reg;

	ret = cmd_ops->set_filter(param, trigger_data, file);
	if (ret < 0)
		goto out_free;

 out_reg:
	/* Up the trigger_data count to make sure reg doesn't free it on failure */
	event_trigger_init(trigger_ops, trigger_data);
	ret = cmd_ops->reg(glob, trigger_ops, trigger_data, file);
	/*
	 * The above returns on success the # of functions enabled,
	 * but if it didn't find any functions it returns zero.
	 * Consider no functions a failure too.
	 */
	if (!ret) {
		cmd_ops->unreg(glob, trigger_ops, trigger_data, file);
		ret = -ENOENT;
	} else if (ret > 0)
		ret = 0;

	/* Down the counter of trigger_data or free it if not used anymore */
	event_trigger_free(trigger_ops, trigger_data);
 out:
	return ret;

 out_free:
	if (cmd_ops->set_filter)
		cmd_ops->set_filter(NULL, trigger_data, NULL);
	kfree(trigger_data);
	goto out;
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
		goto out;

	if (!filter_str)
		goto out;

	/* The filter is for the 'trigger' event, not the triggered event */
	ret = create_event_filter(file->tr, file->event_call,
				  filter_str, false, &filter);
	/*
	 * If create_event_filter() fails, filter still needs to be freed.
	 * Which the calling code will do with data->filter.
	 */
 assign:
	tmp = rcu_access_pointer(data->filter);

	rcu_assign_pointer(data->filter, filter);

	if (tmp) {
		/* Make sure the call is done with the filter */
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
 out:
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
 * @data: The trigger data of a named trigger to unpause
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
traceon_trigger(struct event_trigger_data *data, void *rec,
		struct ring_buffer_event *event)
{
	if (tracing_is_on())
		return;

	tracing_on();
}

static void
traceon_count_trigger(struct event_trigger_data *data, void *rec,
		      struct ring_buffer_event *event)
{
	if (tracing_is_on())
		return;

	if (!data->count)
		return;

	if (data->count != -1)
		(data->count)--;

	tracing_on();
}

static void
traceoff_trigger(struct event_trigger_data *data, void *rec,
		 struct ring_buffer_event *event)
{
	if (!tracing_is_on())
		return;

	tracing_off();
}

static void
traceoff_count_trigger(struct event_trigger_data *data, void *rec,
		       struct ring_buffer_event *event)
{
	if (!tracing_is_on())
		return;

	if (!data->count)
		return;

	if (data->count != -1)
		(data->count)--;

	tracing_off();
}

static int
traceon_trigger_print(struct seq_file *m, struct event_trigger_ops *ops,
		      struct event_trigger_data *data)
{
	return event_trigger_print("traceon", m, (void *)data->count,
				   data->filter_str);
}

static int
traceoff_trigger_print(struct seq_file *m, struct event_trigger_ops *ops,
		       struct event_trigger_data *data)
{
	return event_trigger_print("traceoff", m, (void *)data->count,
				   data->filter_str);
}

static struct event_trigger_ops traceon_trigger_ops = {
	.func			= traceon_trigger,
	.print			= traceon_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops traceon_count_trigger_ops = {
	.func			= traceon_count_trigger,
	.print			= traceon_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops traceoff_trigger_ops = {
	.func			= traceoff_trigger,
	.print			= traceoff_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops traceoff_count_trigger_ops = {
	.func			= traceoff_count_trigger,
	.print			= traceoff_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops *
onoff_get_trigger_ops(char *cmd, char *param)
{
	struct event_trigger_ops *ops;

	/* we register both traceon and traceoff to this callback */
	if (strcmp(cmd, "traceon") == 0)
		ops = param ? &traceon_count_trigger_ops :
			&traceon_trigger_ops;
	else
		ops = param ? &traceoff_count_trigger_ops :
			&traceoff_trigger_ops;

	return ops;
}

static struct event_command trigger_traceon_cmd = {
	.name			= "traceon",
	.trigger_type		= ETT_TRACE_ONOFF,
	.func			= event_trigger_callback,
	.reg			= register_trigger,
	.unreg			= unregister_trigger,
	.get_trigger_ops	= onoff_get_trigger_ops,
	.set_filter		= set_trigger_filter,
};

static struct event_command trigger_traceoff_cmd = {
	.name			= "traceoff",
	.trigger_type		= ETT_TRACE_ONOFF,
	.flags			= EVENT_CMD_FL_POST_TRIGGER,
	.func			= event_trigger_callback,
	.reg			= register_trigger,
	.unreg			= unregister_trigger,
	.get_trigger_ops	= onoff_get_trigger_ops,
	.set_filter		= set_trigger_filter,
};

#ifdef CONFIG_TRACER_SNAPSHOT
static void
snapshot_trigger(struct event_trigger_data *data, void *rec,
		 struct ring_buffer_event *event)
{
	struct trace_event_file *file = data->private_data;

	if (file)
		tracing_snapshot_instance(file->tr);
	else
		tracing_snapshot();
}

static void
snapshot_count_trigger(struct event_trigger_data *data, void *rec,
		       struct ring_buffer_event *event)
{
	if (!data->count)
		return;

	if (data->count != -1)
		(data->count)--;

	snapshot_trigger(data, rec, event);
}

static int
register_snapshot_trigger(char *glob, struct event_trigger_ops *ops,
			  struct event_trigger_data *data,
			  struct trace_event_file *file)
{
	int ret = register_trigger(glob, ops, data, file);

	if (ret > 0 && tracing_alloc_snapshot_instance(file->tr) != 0) {
		unregister_trigger(glob, ops, data, file);
		ret = 0;
	}

	return ret;
}

static int
snapshot_trigger_print(struct seq_file *m, struct event_trigger_ops *ops,
		       struct event_trigger_data *data)
{
	return event_trigger_print("snapshot", m, (void *)data->count,
				   data->filter_str);
}

static struct event_trigger_ops snapshot_trigger_ops = {
	.func			= snapshot_trigger,
	.print			= snapshot_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops snapshot_count_trigger_ops = {
	.func			= snapshot_count_trigger,
	.print			= snapshot_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops *
snapshot_get_trigger_ops(char *cmd, char *param)
{
	return param ? &snapshot_count_trigger_ops : &snapshot_trigger_ops;
}

static struct event_command trigger_snapshot_cmd = {
	.name			= "snapshot",
	.trigger_type		= ETT_SNAPSHOT,
	.func			= event_trigger_callback,
	.reg			= register_snapshot_trigger,
	.unreg			= unregister_trigger,
	.get_trigger_ops	= snapshot_get_trigger_ops,
	.set_filter		= set_trigger_filter,
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
stacktrace_trigger(struct event_trigger_data *data, void *rec,
		   struct ring_buffer_event *event)
{
	trace_dump_stack(STACK_SKIP);
}

static void
stacktrace_count_trigger(struct event_trigger_data *data, void *rec,
			 struct ring_buffer_event *event)
{
	if (!data->count)
		return;

	if (data->count != -1)
		(data->count)--;

	stacktrace_trigger(data, rec, event);
}

static int
stacktrace_trigger_print(struct seq_file *m, struct event_trigger_ops *ops,
			 struct event_trigger_data *data)
{
	return event_trigger_print("stacktrace", m, (void *)data->count,
				   data->filter_str);
}

static struct event_trigger_ops stacktrace_trigger_ops = {
	.func			= stacktrace_trigger,
	.print			= stacktrace_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops stacktrace_count_trigger_ops = {
	.func			= stacktrace_count_trigger,
	.print			= stacktrace_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops *
stacktrace_get_trigger_ops(char *cmd, char *param)
{
	return param ? &stacktrace_count_trigger_ops : &stacktrace_trigger_ops;
}

static struct event_command trigger_stacktrace_cmd = {
	.name			= "stacktrace",
	.trigger_type		= ETT_STACKTRACE,
	.flags			= EVENT_CMD_FL_POST_TRIGGER,
	.func			= event_trigger_callback,
	.reg			= register_trigger,
	.unreg			= unregister_trigger,
	.get_trigger_ops	= stacktrace_get_trigger_ops,
	.set_filter		= set_trigger_filter,
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
event_enable_trigger(struct event_trigger_data *data, void *rec,
		     struct ring_buffer_event *event)
{
	struct enable_trigger_data *enable_data = data->private_data;

	if (enable_data->enable)
		clear_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &enable_data->file->flags);
	else
		set_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &enable_data->file->flags);
}

static void
event_enable_count_trigger(struct event_trigger_data *data, void *rec,
			   struct ring_buffer_event *event)
{
	struct enable_trigger_data *enable_data = data->private_data;

	if (!data->count)
		return;

	/* Skip if the event is in a state we want to switch to */
	if (enable_data->enable == !(enable_data->file->flags & EVENT_FILE_FL_SOFT_DISABLED))
		return;

	if (data->count != -1)
		(data->count)--;

	event_enable_trigger(data, rec, event);
}

int event_enable_trigger_print(struct seq_file *m,
			       struct event_trigger_ops *ops,
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

void event_enable_trigger_free(struct event_trigger_ops *ops,
			       struct event_trigger_data *data)
{
	struct enable_trigger_data *enable_data = data->private_data;

	if (WARN_ON_ONCE(data->ref <= 0))
		return;

	data->ref--;
	if (!data->ref) {
		/* Remove the SOFT_MODE flag */
		trace_event_enable_disable(enable_data->file, 0, 1);
		module_put(enable_data->file->event_call->mod);
		trigger_data_free(data);
		kfree(enable_data);
	}
}

static struct event_trigger_ops event_enable_trigger_ops = {
	.func			= event_enable_trigger,
	.print			= event_enable_trigger_print,
	.init			= event_trigger_init,
	.free			= event_enable_trigger_free,
};

static struct event_trigger_ops event_enable_count_trigger_ops = {
	.func			= event_enable_count_trigger,
	.print			= event_enable_trigger_print,
	.init			= event_trigger_init,
	.free			= event_enable_trigger_free,
};

static struct event_trigger_ops event_disable_trigger_ops = {
	.func			= event_enable_trigger,
	.print			= event_enable_trigger_print,
	.init			= event_trigger_init,
	.free			= event_enable_trigger_free,
};

static struct event_trigger_ops event_disable_count_trigger_ops = {
	.func			= event_enable_count_trigger,
	.print			= event_enable_trigger_print,
	.init			= event_trigger_init,
	.free			= event_enable_trigger_free,
};

int event_enable_trigger_func(struct event_command *cmd_ops,
			      struct trace_event_file *file,
			      char *glob, char *cmd, char *param)
{
	struct trace_event_file *event_enable_file;
	struct enable_trigger_data *enable_data;
	struct event_trigger_data *trigger_data;
	struct event_trigger_ops *trigger_ops;
	struct trace_array *tr = file->tr;
	const char *system;
	const char *event;
	bool hist = false;
	char *trigger;
	char *number;
	bool enable;
	int ret;

	if (!param)
		return -EINVAL;

	/* separate the trigger from the filter (s:e:n [if filter]) */
	trigger = strsep(&param, " \t");
	if (!trigger)
		return -EINVAL;

	system = strsep(&trigger, ":");
	if (!trigger)
		return -EINVAL;

	event = strsep(&trigger, ":");

	ret = -EINVAL;
	event_enable_file = find_event_file(tr, system, event);
	if (!event_enable_file)
		goto out;

#ifdef CONFIG_HIST_TRIGGERS
	hist = ((strcmp(cmd, ENABLE_HIST_STR) == 0) ||
		(strcmp(cmd, DISABLE_HIST_STR) == 0));

	enable = ((strcmp(cmd, ENABLE_EVENT_STR) == 0) ||
		  (strcmp(cmd, ENABLE_HIST_STR) == 0));
#else
	enable = strcmp(cmd, ENABLE_EVENT_STR) == 0;
#endif
	trigger_ops = cmd_ops->get_trigger_ops(cmd, trigger);

	ret = -ENOMEM;
	trigger_data = kzalloc(sizeof(*trigger_data), GFP_KERNEL);
	if (!trigger_data)
		goto out;

	enable_data = kzalloc(sizeof(*enable_data), GFP_KERNEL);
	if (!enable_data) {
		kfree(trigger_data);
		goto out;
	}

	trigger_data->count = -1;
	trigger_data->ops = trigger_ops;
	trigger_data->cmd_ops = cmd_ops;
	INIT_LIST_HEAD(&trigger_data->list);
	RCU_INIT_POINTER(trigger_data->filter, NULL);

	enable_data->hist = hist;
	enable_data->enable = enable;
	enable_data->file = event_enable_file;
	trigger_data->private_data = enable_data;

	if (glob[0] == '!') {
		cmd_ops->unreg(glob+1, trigger_ops, trigger_data, file);
		kfree(trigger_data);
		kfree(enable_data);
		ret = 0;
		goto out;
	}

	/* Up the trigger_data count to make sure nothing frees it on failure */
	event_trigger_init(trigger_ops, trigger_data);

	if (trigger) {
		number = strsep(&trigger, ":");

		ret = -EINVAL;
		if (!strlen(number))
			goto out_free;

		/*
		 * We use the callback data field (which is a pointer)
		 * as our counter.
		 */
		ret = kstrtoul(number, 0, &trigger_data->count);
		if (ret)
			goto out_free;
	}

	if (!param) /* if param is non-empty, it's supposed to be a filter */
		goto out_reg;

	if (!cmd_ops->set_filter)
		goto out_reg;

	ret = cmd_ops->set_filter(param, trigger_data, file);
	if (ret < 0)
		goto out_free;

 out_reg:
	/* Don't let event modules unload while probe registered */
	ret = try_module_get(event_enable_file->event_call->mod);
	if (!ret) {
		ret = -EBUSY;
		goto out_free;
	}

	ret = trace_event_enable_disable(event_enable_file, 1, 1);
	if (ret < 0)
		goto out_put;
	ret = cmd_ops->reg(glob, trigger_ops, trigger_data, file);
	/*
	 * The above returns on success the # of functions enabled,
	 * but if it didn't find any functions it returns zero.
	 * Consider no functions a failure too.
	 */
	if (!ret) {
		ret = -ENOENT;
		goto out_disable;
	} else if (ret < 0)
		goto out_disable;
	/* Just return zero, not the number of enabled functions */
	ret = 0;
	event_trigger_free(trigger_ops, trigger_data);
 out:
	return ret;

 out_disable:
	trace_event_enable_disable(event_enable_file, 0, 1);
 out_put:
	module_put(event_enable_file->event_call->mod);
 out_free:
	if (cmd_ops->set_filter)
		cmd_ops->set_filter(NULL, trigger_data, NULL);
	event_trigger_free(trigger_ops, trigger_data);
	kfree(enable_data);
	goto out;
}

int event_enable_register_trigger(char *glob,
				  struct event_trigger_ops *ops,
				  struct event_trigger_data *data,
				  struct trace_event_file *file)
{
	struct enable_trigger_data *enable_data = data->private_data;
	struct enable_trigger_data *test_enable_data;
	struct event_trigger_data *test;
	int ret = 0;

	list_for_each_entry_rcu(test, &file->triggers, list) {
		test_enable_data = test->private_data;
		if (test_enable_data &&
		    (test->cmd_ops->trigger_type ==
		     data->cmd_ops->trigger_type) &&
		    (test_enable_data->file == enable_data->file)) {
			ret = -EEXIST;
			goto out;
		}
	}

	if (data->ops->init) {
		ret = data->ops->init(data->ops, data);
		if (ret < 0)
			goto out;
	}

	list_add_rcu(&data->list, &file->triggers);
	ret++;

	update_cond_flag(file);
	if (trace_event_trigger_enable_disable(file, 1) < 0) {
		list_del_rcu(&data->list);
		update_cond_flag(file);
		ret--;
	}
out:
	return ret;
}

void event_enable_unregister_trigger(char *glob,
				     struct event_trigger_ops *ops,
				     struct event_trigger_data *test,
				     struct trace_event_file *file)
{
	struct enable_trigger_data *test_enable_data = test->private_data;
	struct enable_trigger_data *enable_data;
	struct event_trigger_data *data;
	bool unregistered = false;

	list_for_each_entry_rcu(data, &file->triggers, list) {
		enable_data = data->private_data;
		if (enable_data &&
		    (data->cmd_ops->trigger_type ==
		     test->cmd_ops->trigger_type) &&
		    (enable_data->file == test_enable_data->file)) {
			unregistered = true;
			list_del_rcu(&data->list);
			trace_event_trigger_enable_disable(file, 0);
			update_cond_flag(file);
			break;
		}
	}

	if (unregistered && data->ops->free)
		data->ops->free(data->ops, data);
}

static struct event_trigger_ops *
event_enable_get_trigger_ops(char *cmd, char *param)
{
	struct event_trigger_ops *ops;
	bool enable;

#ifdef CONFIG_HIST_TRIGGERS
	enable = ((strcmp(cmd, ENABLE_EVENT_STR) == 0) ||
		  (strcmp(cmd, ENABLE_HIST_STR) == 0));
#else
	enable = strcmp(cmd, ENABLE_EVENT_STR) == 0;
#endif
	if (enable)
		ops = param ? &event_enable_count_trigger_ops :
			&event_enable_trigger_ops;
	else
		ops = param ? &event_disable_count_trigger_ops :
			&event_disable_trigger_ops;

	return ops;
}

static struct event_command trigger_enable_cmd = {
	.name			= ENABLE_EVENT_STR,
	.trigger_type		= ETT_EVENT_ENABLE,
	.func			= event_enable_trigger_func,
	.reg			= event_enable_register_trigger,
	.unreg			= event_enable_unregister_trigger,
	.get_trigger_ops	= event_enable_get_trigger_ops,
	.set_filter		= set_trigger_filter,
};

static struct event_command trigger_disable_cmd = {
	.name			= DISABLE_EVENT_STR,
	.trigger_type		= ETT_EVENT_ENABLE,
	.func			= event_enable_trigger_func,
	.reg			= event_enable_register_trigger,
	.unreg			= event_enable_unregister_trigger,
	.get_trigger_ops	= event_enable_get_trigger_ops,
	.set_filter		= set_trigger_filter,
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
