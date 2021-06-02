// SPDX-License-Identifier: GPL-2.0
/*
 * Generic dynamic event control interface
 *
 * Copyright (C) 2018 Masami Hiramatsu <mhiramat@kernel.org>
 */

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/tracefs.h>

#include "trace.h"
#include "trace_dynevent.h"

static DEFINE_MUTEX(dyn_event_ops_mutex);
static LIST_HEAD(dyn_event_ops_list);

int dyn_event_register(struct dyn_event_operations *ops)
{
	if (!ops || !ops->create || !ops->show || !ops->is_busy ||
	    !ops->free || !ops->match)
		return -EINVAL;

	INIT_LIST_HEAD(&ops->list);
	mutex_lock(&dyn_event_ops_mutex);
	list_add_tail(&ops->list, &dyn_event_ops_list);
	mutex_unlock(&dyn_event_ops_mutex);
	return 0;
}

int dyn_event_release(const char *raw_command, struct dyn_event_operations *type)
{
	struct dyn_event *pos, *n;
	char *system = NULL, *event, *p;
	int argc, ret = -ENOENT;
	char **argv;

	argv = argv_split(GFP_KERNEL, raw_command, &argc);
	if (!argv)
		return -ENOMEM;

	if (argv[0][0] == '-') {
		if (argv[0][1] != ':') {
			ret = -EINVAL;
			goto out;
		}
		event = &argv[0][2];
	} else {
		event = strchr(argv[0], ':');
		if (!event) {
			ret = -EINVAL;
			goto out;
		}
		event++;
	}

	p = strchr(event, '/');
	if (p) {
		system = event;
		event = p + 1;
		*p = '\0';
	}
	if (event[0] == '\0') {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&event_mutex);
	for_each_dyn_event_safe(pos, n) {
		if (type && type != pos->ops)
			continue;
		if (!pos->ops->match(system, event,
				argc - 1, (const char **)argv + 1, pos))
			continue;

		ret = pos->ops->free(pos);
		if (ret)
			break;
	}
	mutex_unlock(&event_mutex);
out:
	argv_free(argv);
	return ret;
}

static int create_dyn_event(const char *raw_command)
{
	struct dyn_event_operations *ops;
	int ret = -ENODEV;

	if (raw_command[0] == '-' || raw_command[0] == '!')
		return dyn_event_release(raw_command, NULL);

	mutex_lock(&dyn_event_ops_mutex);
	list_for_each_entry(ops, &dyn_event_ops_list, list) {
		ret = ops->create(raw_command);
		if (!ret || ret != -ECANCELED)
			break;
	}
	mutex_unlock(&dyn_event_ops_mutex);
	if (ret == -ECANCELED)
		ret = -EINVAL;

	return ret;
}

/* Protected by event_mutex */
LIST_HEAD(dyn_event_list);

void *dyn_event_seq_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&event_mutex);
	return seq_list_start(&dyn_event_list, *pos);
}

void *dyn_event_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &dyn_event_list, pos);
}

void dyn_event_seq_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&event_mutex);
}

static int dyn_event_seq_show(struct seq_file *m, void *v)
{
	struct dyn_event *ev = v;

	if (ev && ev->ops)
		return ev->ops->show(m, ev);

	return 0;
}

static const struct seq_operations dyn_event_seq_op = {
	.start	= dyn_event_seq_start,
	.next	= dyn_event_seq_next,
	.stop	= dyn_event_seq_stop,
	.show	= dyn_event_seq_show
};

/*
 * dyn_events_release_all - Release all specific events
 * @type:	the dyn_event_operations * which filters releasing events
 *
 * This releases all events which ->ops matches @type. If @type is NULL,
 * all events are released.
 * Return -EBUSY if any of them are in use, and return other errors when
 * it failed to free the given event. Except for -EBUSY, event releasing
 * process will be aborted at that point and there may be some other
 * releasable events on the list.
 */
int dyn_events_release_all(struct dyn_event_operations *type)
{
	struct dyn_event *ev, *tmp;
	int ret = 0;

	mutex_lock(&event_mutex);
	for_each_dyn_event(ev) {
		if (type && ev->ops != type)
			continue;
		if (ev->ops->is_busy(ev)) {
			ret = -EBUSY;
			goto out;
		}
	}
	for_each_dyn_event_safe(ev, tmp) {
		if (type && ev->ops != type)
			continue;
		ret = ev->ops->free(ev);
		if (ret)
			break;
	}
out:
	mutex_unlock(&event_mutex);

	return ret;
}

static int dyn_event_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = tracing_check_open_get_tr(NULL);
	if (ret)
		return ret;

	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC)) {
		ret = dyn_events_release_all(NULL);
		if (ret < 0)
			return ret;
	}

	return seq_open(file, &dyn_event_seq_op);
}

static ssize_t dyn_event_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *ppos)
{
	return trace_parse_run_command(file, buffer, count, ppos,
				       create_dyn_event);
}

static const struct file_operations dynamic_events_ops = {
	.owner          = THIS_MODULE,
	.open           = dyn_event_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
	.write		= dyn_event_write,
};

/* Make a tracefs interface for controlling dynamic events */
static __init int init_dynamic_event(void)
{
	struct dentry *entry;
	int ret;

	ret = tracing_init_dentry();
	if (ret)
		return 0;

	entry = tracefs_create_file("dynamic_events", 0644, NULL,
				    NULL, &dynamic_events_ops);

	/* Event list interface */
	if (!entry)
		pr_warn("Could not create tracefs 'dynamic_events' entry\n");

	return 0;
}
fs_initcall(init_dynamic_event);

/**
 * dynevent_arg_add - Add an arg to a dynevent_cmd
 * @cmd: A pointer to the dynevent_cmd struct representing the new event cmd
 * @arg: The argument to append to the current cmd
 * @check_arg: An (optional) pointer to a function checking arg sanity
 *
 * Append an argument to a dynevent_cmd.  The argument string will be
 * appended to the current cmd string, followed by a separator, if
 * applicable.  Before the argument is added, the @check_arg function,
 * if present, will be used to check the sanity of the current arg
 * string.
 *
 * The cmd string and separator should be set using the
 * dynevent_arg_init() before any arguments are added using this
 * function.
 *
 * Return: 0 if successful, error otherwise.
 */
int dynevent_arg_add(struct dynevent_cmd *cmd,
		     struct dynevent_arg *arg,
		     dynevent_check_arg_fn_t check_arg)
{
	int ret = 0;

	if (check_arg) {
		ret = check_arg(arg);
		if (ret)
			return ret;
	}

	ret = seq_buf_printf(&cmd->seq, " %s%c", arg->str, arg->separator);
	if (ret) {
		pr_err("String is too long: %s%c\n", arg->str, arg->separator);
		return -E2BIG;
	}

	return ret;
}

/**
 * dynevent_arg_pair_add - Add an arg pair to a dynevent_cmd
 * @cmd: A pointer to the dynevent_cmd struct representing the new event cmd
 * @arg_pair: The argument pair to append to the current cmd
 * @check_arg: An (optional) pointer to a function checking arg sanity
 *
 * Append an argument pair to a dynevent_cmd.  An argument pair
 * consists of a left-hand-side argument and a right-hand-side
 * argument separated by an operator, which can be whitespace, all
 * followed by a separator, if applicable.  This can be used to add
 * arguments of the form 'type variable_name;' or 'x+y'.
 *
 * The lhs argument string will be appended to the current cmd string,
 * followed by an operator, if applicable, followed by the rhs string,
 * followed finally by a separator, if applicable.  Before the
 * argument is added, the @check_arg function, if present, will be
 * used to check the sanity of the current arg strings.
 *
 * The cmd strings, operator, and separator should be set using the
 * dynevent_arg_pair_init() before any arguments are added using this
 * function.
 *
 * Return: 0 if successful, error otherwise.
 */
int dynevent_arg_pair_add(struct dynevent_cmd *cmd,
			  struct dynevent_arg_pair *arg_pair,
			  dynevent_check_arg_fn_t check_arg)
{
	int ret = 0;

	if (check_arg) {
		ret = check_arg(arg_pair);
		if (ret)
			return ret;
	}

	ret = seq_buf_printf(&cmd->seq, " %s%c%s%c", arg_pair->lhs,
			     arg_pair->operator, arg_pair->rhs,
			     arg_pair->separator);
	if (ret) {
		pr_err("field string is too long: %s%c%s%c\n", arg_pair->lhs,
		       arg_pair->operator, arg_pair->rhs,
		       arg_pair->separator);
		return -E2BIG;
	}

	return ret;
}

/**
 * dynevent_str_add - Add a string to a dynevent_cmd
 * @cmd: A pointer to the dynevent_cmd struct representing the new event cmd
 * @str: The string to append to the current cmd
 *
 * Append a string to a dynevent_cmd.  The string will be appended to
 * the current cmd string as-is, with nothing prepended or appended.
 *
 * Return: 0 if successful, error otherwise.
 */
int dynevent_str_add(struct dynevent_cmd *cmd, const char *str)
{
	int ret = 0;

	ret = seq_buf_puts(&cmd->seq, str);
	if (ret) {
		pr_err("String is too long: %s\n", str);
		return -E2BIG;
	}

	return ret;
}

/**
 * dynevent_cmd_init - Initialize a dynevent_cmd object
 * @cmd: A pointer to the dynevent_cmd struct representing the cmd
 * @buf: A pointer to the buffer to generate the command into
 * @maxlen: The length of the buffer the command will be generated into
 * @type: The type of the cmd, checked against further operations
 * @run_command: The type-specific function that will actually run the command
 *
 * Initialize a dynevent_cmd.  A dynevent_cmd is used to build up and
 * run dynamic event creation commands, such as commands for creating
 * synthetic and kprobe events.  Before calling any of the functions
 * used to build the command, a dynevent_cmd object should be
 * instantiated and initialized using this function.
 *
 * The initialization sets things up by saving a pointer to the
 * user-supplied buffer and its length via the @buf and @maxlen
 * params, and by saving the cmd-specific @type and @run_command
 * params which are used to check subsequent dynevent_cmd operations
 * and actually run the command when complete.
 */
void dynevent_cmd_init(struct dynevent_cmd *cmd, char *buf, int maxlen,
		       enum dynevent_type type,
		       dynevent_create_fn_t run_command)
{
	memset(cmd, '\0', sizeof(*cmd));

	seq_buf_init(&cmd->seq, buf, maxlen);
	cmd->type = type;
	cmd->run_command = run_command;
}

/**
 * dynevent_arg_init - Initialize a dynevent_arg object
 * @arg: A pointer to the dynevent_arg struct representing the arg
 * @separator: An (optional) separator, appended after adding the arg
 *
 * Initialize a dynevent_arg object.  A dynevent_arg represents an
 * object used to append single arguments to the current command
 * string.  After the arg string is successfully appended to the
 * command string, the optional @separator is appended.  If no
 * separator was specified when initializing the arg, a space will be
 * appended.
 */
void dynevent_arg_init(struct dynevent_arg *arg,
		       char separator)
{
	memset(arg, '\0', sizeof(*arg));

	if (!separator)
		separator = ' ';
	arg->separator = separator;
}

/**
 * dynevent_arg_pair_init - Initialize a dynevent_arg_pair object
 * @arg_pair: A pointer to the dynevent_arg_pair struct representing the arg
 * @operator: An (optional) operator, appended after adding the first arg
 * @separator: An (optional) separator, appended after adding the second arg
 *
 * Initialize a dynevent_arg_pair object.  A dynevent_arg_pair
 * represents an object used to append argument pairs such as 'type
 * variable_name;' or 'x+y' to the current command string.  An
 * argument pair consists of a left-hand-side argument and a
 * right-hand-side argument separated by an operator, which can be
 * whitespace, all followed by a separator, if applicable.  After the
 * first arg string is successfully appended to the command string,
 * the optional @operator is appended, followed by the second arg and
 * optional @separator.  If no separator was specified when
 * initializing the arg, a space will be appended.
 */
void dynevent_arg_pair_init(struct dynevent_arg_pair *arg_pair,
			    char operator, char separator)
{
	memset(arg_pair, '\0', sizeof(*arg_pair));

	if (!operator)
		operator = ' ';
	arg_pair->operator = operator;

	if (!separator)
		separator = ' ';
	arg_pair->separator = separator;
}

/**
 * dynevent_create - Create the dynamic event contained in dynevent_cmd
 * @cmd: The dynevent_cmd object containing the dynamic event creation command
 *
 * Once a dynevent_cmd object has been successfully built up via the
 * dynevent_cmd_init(), dynevent_arg_add() and dynevent_arg_pair_add()
 * functions, this function runs the final command to actually create
 * the event.
 *
 * Return: 0 if the event was successfully created, error otherwise.
 */
int dynevent_create(struct dynevent_cmd *cmd)
{
	return cmd->run_command(cmd);
}
EXPORT_SYMBOL_GPL(dynevent_create);
