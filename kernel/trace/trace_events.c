// SPDX-License-Identifier: GPL-2.0
/*
 * event tracer
 *
 * Copyright (C) 2008 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 *  - Added format output of fields of the trace point.
 *    This was based off of work by Tom Zanussi <tzanussi@gmail.com>.
 *
 */

#define pr_fmt(fmt) fmt

#include <linux/workqueue.h>
#include <linux/security.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/tracefs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/sort.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <trace/events/sched.h>
#include <trace/syscall.h>

#include <asm/setup.h>

#include "trace_output.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM "TRACE_SYSTEM"

DEFINE_MUTEX(event_mutex);

LIST_HEAD(ftrace_events);
static LIST_HEAD(ftrace_generic_fields);
static LIST_HEAD(ftrace_common_fields);
static bool eventdir_initialized;

static LIST_HEAD(module_strings);

struct module_string {
	struct list_head	next;
	struct module		*module;
	char			*str;
};

#define GFP_TRACE (GFP_KERNEL | __GFP_ZERO)

static struct kmem_cache *field_cachep;
static struct kmem_cache *file_cachep;

static inline int system_refcount(struct event_subsystem *system)
{
	return system->ref_count;
}

static int system_refcount_inc(struct event_subsystem *system)
{
	return system->ref_count++;
}

static int system_refcount_dec(struct event_subsystem *system)
{
	return --system->ref_count;
}

/* Double loops, do not use break, only goto's work */
#define do_for_each_event_file(tr, file)			\
	list_for_each_entry(tr, &ftrace_trace_arrays, list) {	\
		list_for_each_entry(file, &tr->events, list)

#define do_for_each_event_file_safe(tr, file)			\
	list_for_each_entry(tr, &ftrace_trace_arrays, list) {	\
		struct trace_event_file *___n;				\
		list_for_each_entry_safe(file, ___n, &tr->events, list)

#define while_for_each_event_file()		\
	}

static struct ftrace_event_field *
__find_event_field(struct list_head *head, const char *name)
{
	struct ftrace_event_field *field;

	list_for_each_entry(field, head, link) {
		if (!strcmp(field->name, name))
			return field;
	}

	return NULL;
}

struct ftrace_event_field *
trace_find_event_field(struct trace_event_call *call, char *name)
{
	struct ftrace_event_field *field;
	struct list_head *head;

	head = trace_get_fields(call);
	field = __find_event_field(head, name);
	if (field)
		return field;

	field = __find_event_field(&ftrace_generic_fields, name);
	if (field)
		return field;

	return __find_event_field(&ftrace_common_fields, name);
}

static int __trace_define_field(struct list_head *head, const char *type,
				const char *name, int offset, int size,
				int is_signed, int filter_type, int len,
				int need_test)
{
	struct ftrace_event_field *field;

	field = kmem_cache_alloc(field_cachep, GFP_TRACE);
	if (!field)
		return -ENOMEM;

	field->name = name;
	field->type = type;

	if (filter_type == FILTER_OTHER)
		field->filter_type = filter_assign_type(type);
	else
		field->filter_type = filter_type;

	field->offset = offset;
	field->size = size;
	field->is_signed = is_signed;
	field->needs_test = need_test;
	field->len = len;

	list_add(&field->link, head);

	return 0;
}

int trace_define_field(struct trace_event_call *call, const char *type,
		       const char *name, int offset, int size, int is_signed,
		       int filter_type)
{
	struct list_head *head;

	if (WARN_ON(!call->class))
		return 0;

	head = trace_get_fields(call);
	return __trace_define_field(head, type, name, offset, size,
				    is_signed, filter_type, 0, 0);
}
EXPORT_SYMBOL_GPL(trace_define_field);

static int trace_define_field_ext(struct trace_event_call *call, const char *type,
		       const char *name, int offset, int size, int is_signed,
		       int filter_type, int len, int need_test)
{
	struct list_head *head;

	if (WARN_ON(!call->class))
		return 0;

	head = trace_get_fields(call);
	return __trace_define_field(head, type, name, offset, size,
				    is_signed, filter_type, len, need_test);
}

#define __generic_field(type, item, filter_type)			\
	ret = __trace_define_field(&ftrace_generic_fields, #type,	\
				   #item, 0, 0, is_signed_type(type),	\
				   filter_type, 0, 0);			\
	if (ret)							\
		return ret;

#define __common_field(type, item)					\
	ret = __trace_define_field(&ftrace_common_fields, #type,	\
				   "common_" #item,			\
				   offsetof(typeof(ent), item),		\
				   sizeof(ent.item),			\
				   is_signed_type(type), FILTER_OTHER,	\
				   0, 0);				\
	if (ret)							\
		return ret;

static int trace_define_generic_fields(void)
{
	int ret;

	__generic_field(int, CPU, FILTER_CPU);
	__generic_field(int, cpu, FILTER_CPU);
	__generic_field(int, common_cpu, FILTER_CPU);
	__generic_field(char *, COMM, FILTER_COMM);
	__generic_field(char *, comm, FILTER_COMM);
	__generic_field(char *, stacktrace, FILTER_STACKTRACE);
	__generic_field(char *, STACKTRACE, FILTER_STACKTRACE);

	return ret;
}

static int trace_define_common_fields(void)
{
	int ret;
	struct trace_entry ent;

	__common_field(unsigned short, type);
	__common_field(unsigned char, flags);
	/* Holds both preempt_count and migrate_disable */
	__common_field(unsigned char, preempt_count);
	__common_field(int, pid);

	return ret;
}

static void trace_destroy_fields(struct trace_event_call *call)
{
	struct ftrace_event_field *field, *next;
	struct list_head *head;

	head = trace_get_fields(call);
	list_for_each_entry_safe(field, next, head, link) {
		list_del(&field->link);
		kmem_cache_free(field_cachep, field);
	}
}

/*
 * run-time version of trace_event_get_offsets_<call>() that returns the last
 * accessible offset of trace fields excluding __dynamic_array bytes
 */
int trace_event_get_offsets(struct trace_event_call *call)
{
	struct ftrace_event_field *tail;
	struct list_head *head;

	head = trace_get_fields(call);
	/*
	 * head->next points to the last field with the largest offset,
	 * since it was added last by trace_define_field()
	 */
	tail = list_first_entry(head, struct ftrace_event_field, link);
	return tail->offset + tail->size;
}


static struct trace_event_fields *find_event_field(const char *fmt,
						   struct trace_event_call *call)
{
	struct trace_event_fields *field = call->class->fields_array;
	const char *p = fmt;
	int len;

	if (!(len = str_has_prefix(fmt, "REC->")))
		return NULL;
	fmt += len;
	for (p = fmt; *p; p++) {
		if (!isalnum(*p) && *p != '_')
			break;
	}
	len = p - fmt;

	for (; field->type; field++) {
		if (strncmp(field->name, fmt, len) || field->name[len])
			continue;

		return field;
	}
	return NULL;
}

/*
 * Check if the referenced field is an array and return true,
 * as arrays are OK to dereference.
 */
static bool test_field(const char *fmt, struct trace_event_call *call)
{
	struct trace_event_fields *field;

	field = find_event_field(fmt, call);
	if (!field)
		return false;

	/* This is an array and is OK to dereference. */
	return strchr(field->type, '[') != NULL;
}

/* Look for a string within an argument */
static bool find_print_string(const char *arg, const char *str, const char *end)
{
	const char *r;

	r = strstr(arg, str);
	return r && r < end;
}

/* Return true if the argument pointer is safe */
static bool process_pointer(const char *fmt, int len, struct trace_event_call *call)
{
	const char *r, *e, *a;

	e = fmt + len;

	/* Find the REC-> in the argument */
	r = strstr(fmt, "REC->");
	if (r && r < e) {
		/*
		 * Addresses of events on the buffer, or an array on the buffer is
		 * OK to dereference. There's ways to fool this, but
		 * this is to catch common mistakes, not malicious code.
		 */
		a = strchr(fmt, '&');
		if ((a && (a < r)) || test_field(r, call))
			return true;
	} else if (find_print_string(fmt, "__get_dynamic_array(", e)) {
		return true;
	} else if (find_print_string(fmt, "__get_rel_dynamic_array(", e)) {
		return true;
	} else if (find_print_string(fmt, "__get_dynamic_array_len(", e)) {
		return true;
	} else if (find_print_string(fmt, "__get_rel_dynamic_array_len(", e)) {
		return true;
	} else if (find_print_string(fmt, "__get_sockaddr(", e)) {
		return true;
	} else if (find_print_string(fmt, "__get_rel_sockaddr(", e)) {
		return true;
	}
	return false;
}

/* Return true if the string is safe */
static bool process_string(const char *fmt, int len, struct trace_event_call *call)
{
	struct trace_event_fields *field;
	const char *r, *e, *s;

	e = fmt + len;

	/*
	 * There are several helper functions that return strings.
	 * If the argument contains a function, then assume its field is valid.
	 * It is considered that the argument has a function if it has:
	 *   alphanumeric or '_' before a parenthesis.
	 */
	s = fmt;
	do {
		r = strstr(s, "(");
		if (!r || r >= e)
			break;
		for (int i = 1; r - i >= s; i++) {
			char ch = *(r - i);
			if (isspace(ch))
				continue;
			if (isalnum(ch) || ch == '_')
				return true;
			/* Anything else, this isn't a function */
			break;
		}
		/* A function could be wrapped in parethesis, try the next one */
		s = r + 1;
	} while (s < e);

	/*
	 * Check for arrays. If the argument has: foo[REC->val]
	 * then it is very likely that foo is an array of strings
	 * that are safe to use.
	 */
	r = strstr(s, "[");
	if (r && r < e) {
		r = strstr(r, "REC->");
		if (r && r < e)
			return true;
	}

	/*
	 * If there's any strings in the argument consider this arg OK as it
	 * could be: REC->field ? "foo" : "bar" and we don't want to get into
	 * verifying that logic here.
	 */
	if (find_print_string(fmt, "\"", e))
		return true;

	/* Dereferenced strings are also valid like any other pointer */
	if (process_pointer(fmt, len, call))
		return true;

	/* Make sure the field is found */
	field = find_event_field(fmt, call);
	if (!field)
		return false;

	/* Test this field's string before printing the event */
	call->flags |= TRACE_EVENT_FL_TEST_STR;
	field->needs_test = 1;

	return true;
}

static void handle_dereference_arg(const char *arg_str, u64 string_flags, int len,
				   u64 *dereference_flags, int arg,
				   struct trace_event_call *call)
{
	if (string_flags & (1ULL << arg)) {
		if (process_string(arg_str, len, call))
			*dereference_flags &= ~(1ULL << arg);
	} else if (process_pointer(arg_str, len, call))
		*dereference_flags &= ~(1ULL << arg);
	else
		pr_warn("TRACE EVENT ERROR: Bad dereference argument: '%.*s'\n",
			len, arg_str);
}

/*
 * Examine the print fmt of the event looking for unsafe dereference
 * pointers using %p* that could be recorded in the trace event and
 * much later referenced after the pointer was freed. Dereferencing
 * pointers are OK, if it is dereferenced into the event itself.
 */
static void test_event_printk(struct trace_event_call *call)
{
	u64 dereference_flags = 0;
	u64 string_flags = 0;
	bool first = true;
	const char *fmt;
	int parens = 0;
	char in_quote = 0;
	int start_arg = 0;
	int arg = 0;
	int i, e;

	fmt = call->print_fmt;

	if (!fmt)
		return;

	for (i = 0; fmt[i]; i++) {
		switch (fmt[i]) {
		case '\\':
			i++;
			if (!fmt[i])
				return;
			continue;
		case '"':
		case '\'':
			/*
			 * The print fmt starts with a string that
			 * is processed first to find %p* usage,
			 * then after the first string, the print fmt
			 * contains arguments that are used to check
			 * if the dereferenced %p* usage is safe.
			 */
			if (first) {
				if (fmt[i] == '\'')
					continue;
				if (in_quote) {
					arg = 0;
					first = false;
					/*
					 * If there was no %p* uses
					 * the fmt is OK.
					 */
					if (!dereference_flags)
						return;
				}
			}
			if (in_quote) {
				if (in_quote == fmt[i])
					in_quote = 0;
			} else {
				in_quote = fmt[i];
			}
			continue;
		case '%':
			if (!first || !in_quote)
				continue;
			i++;
			if (!fmt[i])
				return;
			switch (fmt[i]) {
			case '%':
				continue;
			case 'p':
 do_pointer:
				/* Find dereferencing fields */
				switch (fmt[i + 1]) {
				case 'B': case 'R': case 'r':
				case 'b': case 'M': case 'm':
				case 'I': case 'i': case 'E':
				case 'U': case 'V': case 'N':
				case 'a': case 'd': case 'D':
				case 'g': case 't': case 'C':
				case 'O': case 'f':
					if (WARN_ONCE(arg == 63,
						      "Too many args for event: %s",
						      trace_event_name(call)))
						return;
					dereference_flags |= 1ULL << arg;
				}
				break;
			default:
			{
				bool star = false;
				int j;

				/* Increment arg if %*s exists. */
				for (j = 0; fmt[i + j]; j++) {
					if (isdigit(fmt[i + j]) ||
					    fmt[i + j] == '.')
						continue;
					if (fmt[i + j] == '*') {
						star = true;
						/* Handle %*pbl case */
						if (!j && fmt[i + 1] == 'p') {
							arg++;
							i++;
							goto do_pointer;
						}
						continue;
					}
					if ((fmt[i + j] == 's')) {
						if (star)
							arg++;
						if (WARN_ONCE(arg == 63,
							      "Too many args for event: %s",
							      trace_event_name(call)))
							return;
						dereference_flags |= 1ULL << arg;
						string_flags |= 1ULL << arg;
					}
					break;
				}
				break;
			} /* default */

			} /* switch */
			arg++;
			continue;
		case '(':
			if (in_quote)
				continue;
			parens++;
			continue;
		case ')':
			if (in_quote)
				continue;
			parens--;
			if (WARN_ONCE(parens < 0,
				      "Paren mismatch for event: %s\narg='%s'\n%*s",
				      trace_event_name(call),
				      fmt + start_arg,
				      (i - start_arg) + 5, "^"))
				return;
			continue;
		case ',':
			if (in_quote || parens)
				continue;
			e = i;
			i++;
			while (isspace(fmt[i]))
				i++;

			/*
			 * If start_arg is zero, then this is the start of the
			 * first argument. The processing of the argument happens
			 * when the end of the argument is found, as it needs to
			 * handle paranthesis and such.
			 */
			if (!start_arg) {
				start_arg = i;
				/* Balance out the i++ in the for loop */
				i--;
				continue;
			}

			if (dereference_flags & (1ULL << arg)) {
				handle_dereference_arg(fmt + start_arg, string_flags,
						       e - start_arg,
						       &dereference_flags, arg, call);
			}

			start_arg = i;
			arg++;
			/* Balance out the i++ in the for loop */
			i--;
		}
	}

	if (dereference_flags & (1ULL << arg)) {
		handle_dereference_arg(fmt + start_arg, string_flags,
				       i - start_arg,
				       &dereference_flags, arg, call);
	}

	/*
	 * If you triggered the below warning, the trace event reported
	 * uses an unsafe dereference pointer %p*. As the data stored
	 * at the trace event time may no longer exist when the trace
	 * event is printed, dereferencing to the original source is
	 * unsafe. The source of the dereference must be copied into the
	 * event itself, and the dereference must access the copy instead.
	 */
	if (WARN_ON_ONCE(dereference_flags)) {
		arg = 1;
		while (!(dereference_flags & 1)) {
			dereference_flags >>= 1;
			arg++;
		}
		pr_warn("event %s has unsafe dereference of argument %d\n",
			trace_event_name(call), arg);
		pr_warn("print_fmt: %s\n", fmt);
	}
}

int trace_event_raw_init(struct trace_event_call *call)
{
	int id;

	id = register_trace_event(&call->event);
	if (!id)
		return -ENODEV;

	test_event_printk(call);

	return 0;
}
EXPORT_SYMBOL_GPL(trace_event_raw_init);

bool trace_event_ignore_this_pid(struct trace_event_file *trace_file)
{
	struct trace_array *tr = trace_file->tr;
	struct trace_pid_list *no_pid_list;
	struct trace_pid_list *pid_list;

	pid_list = rcu_dereference_raw(tr->filtered_pids);
	no_pid_list = rcu_dereference_raw(tr->filtered_no_pids);

	if (!pid_list && !no_pid_list)
		return false;

	/*
	 * This is recorded at every sched_switch for this task.
	 * Thus, even if the task migrates the ignore value will be the same.
	 */
	return this_cpu_read(tr->array_buffer.data->ignore_pid) != 0;
}
EXPORT_SYMBOL_GPL(trace_event_ignore_this_pid);

void *trace_event_buffer_reserve(struct trace_event_buffer *fbuffer,
				 struct trace_event_file *trace_file,
				 unsigned long len)
{
	struct trace_event_call *event_call = trace_file->event_call;

	if ((trace_file->flags & EVENT_FILE_FL_PID_FILTER) &&
	    trace_event_ignore_this_pid(trace_file))
		return NULL;

	/*
	 * If CONFIG_PREEMPTION is enabled, then the tracepoint itself disables
	 * preemption (adding one to the preempt_count). Since we are
	 * interested in the preempt_count at the time the tracepoint was
	 * hit, we need to subtract one to offset the increment.
	 */
	fbuffer->trace_ctx = tracing_gen_ctx_dec();
	fbuffer->trace_file = trace_file;

	fbuffer->event =
		trace_event_buffer_lock_reserve(&fbuffer->buffer, trace_file,
						event_call->event.type, len,
						fbuffer->trace_ctx);
	if (!fbuffer->event)
		return NULL;

	fbuffer->regs = NULL;
	fbuffer->entry = ring_buffer_event_data(fbuffer->event);
	return fbuffer->entry;
}
EXPORT_SYMBOL_GPL(trace_event_buffer_reserve);

int trace_event_reg(struct trace_event_call *call,
		    enum trace_reg type, void *data)
{
	struct trace_event_file *file = data;

	WARN_ON(!(call->flags & TRACE_EVENT_FL_TRACEPOINT));
	switch (type) {
	case TRACE_REG_REGISTER:
		return tracepoint_probe_register(call->tp,
						 call->class->probe,
						 file);
	case TRACE_REG_UNREGISTER:
		tracepoint_probe_unregister(call->tp,
					    call->class->probe,
					    file);
		return 0;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return tracepoint_probe_register(call->tp,
						 call->class->perf_probe,
						 call);
	case TRACE_REG_PERF_UNREGISTER:
		tracepoint_probe_unregister(call->tp,
					    call->class->perf_probe,
					    call);
		return 0;
	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		return 0;
#endif
	}
	return 0;
}
EXPORT_SYMBOL_GPL(trace_event_reg);

void trace_event_enable_cmd_record(bool enable)
{
	struct trace_event_file *file;
	struct trace_array *tr;

	lockdep_assert_held(&event_mutex);

	do_for_each_event_file(tr, file) {

		if (!(file->flags & EVENT_FILE_FL_ENABLED))
			continue;

		if (enable) {
			tracing_start_cmdline_record();
			set_bit(EVENT_FILE_FL_RECORDED_CMD_BIT, &file->flags);
		} else {
			tracing_stop_cmdline_record();
			clear_bit(EVENT_FILE_FL_RECORDED_CMD_BIT, &file->flags);
		}
	} while_for_each_event_file();
}

void trace_event_enable_tgid_record(bool enable)
{
	struct trace_event_file *file;
	struct trace_array *tr;

	lockdep_assert_held(&event_mutex);

	do_for_each_event_file(tr, file) {
		if (!(file->flags & EVENT_FILE_FL_ENABLED))
			continue;

		if (enable) {
			tracing_start_tgid_record();
			set_bit(EVENT_FILE_FL_RECORDED_TGID_BIT, &file->flags);
		} else {
			tracing_stop_tgid_record();
			clear_bit(EVENT_FILE_FL_RECORDED_TGID_BIT,
				  &file->flags);
		}
	} while_for_each_event_file();
}

static int __ftrace_event_enable_disable(struct trace_event_file *file,
					 int enable, int soft_disable)
{
	struct trace_event_call *call = file->event_call;
	struct trace_array *tr = file->tr;
	bool soft_mode = atomic_read(&file->sm_ref) != 0;
	int ret = 0;
	int disable;

	switch (enable) {
	case 0:
		/*
		 * When soft_disable is set and enable is cleared, the sm_ref
		 * reference counter is decremented. If it reaches 0, we want
		 * to clear the SOFT_DISABLED flag but leave the event in the
		 * state that it was. That is, if the event was enabled and
		 * SOFT_DISABLED isn't set, then do nothing. But if SOFT_DISABLED
		 * is set we do not want the event to be enabled before we
		 * clear the bit.
		 *
		 * When soft_disable is not set but the soft_mode is,
		 * we do nothing. Do not disable the tracepoint, otherwise
		 * "soft enable"s (clearing the SOFT_DISABLED bit) wont work.
		 */
		if (soft_disable) {
			if (atomic_dec_return(&file->sm_ref) > 0)
				break;
			disable = file->flags & EVENT_FILE_FL_SOFT_DISABLED;
			soft_mode = false;
			/* Disable use of trace_buffered_event */
			trace_buffered_event_disable();
		} else
			disable = !soft_mode;

		if (disable && (file->flags & EVENT_FILE_FL_ENABLED)) {
			clear_bit(EVENT_FILE_FL_ENABLED_BIT, &file->flags);
			if (file->flags & EVENT_FILE_FL_RECORDED_CMD) {
				tracing_stop_cmdline_record();
				clear_bit(EVENT_FILE_FL_RECORDED_CMD_BIT, &file->flags);
			}

			if (file->flags & EVENT_FILE_FL_RECORDED_TGID) {
				tracing_stop_tgid_record();
				clear_bit(EVENT_FILE_FL_RECORDED_TGID_BIT, &file->flags);
			}

			ret = call->class->reg(call, TRACE_REG_UNREGISTER, file);

			WARN_ON_ONCE(ret);
		}
		/* If in soft mode, just set the SOFT_DISABLE_BIT, else clear it */
		if (soft_mode)
			set_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &file->flags);
		else
			clear_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &file->flags);
		break;
	case 1:
		/*
		 * When soft_disable is set and enable is set, we want to
		 * register the tracepoint for the event, but leave the event
		 * as is. That means, if the event was already enabled, we do
		 * nothing (but set soft_mode). If the event is disabled, we
		 * set SOFT_DISABLED before enabling the event tracepoint, so
		 * it still seems to be disabled.
		 */
		if (!soft_disable)
			clear_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &file->flags);
		else {
			if (atomic_inc_return(&file->sm_ref) > 1)
				break;
			soft_mode = true;
			/* Enable use of trace_buffered_event */
			trace_buffered_event_enable();
		}

		if (!(file->flags & EVENT_FILE_FL_ENABLED)) {
			bool cmd = false, tgid = false;

			/* Keep the event disabled, when going to soft mode. */
			if (soft_disable)
				set_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &file->flags);

			if (tr->trace_flags & TRACE_ITER_RECORD_CMD) {
				cmd = true;
				tracing_start_cmdline_record();
				set_bit(EVENT_FILE_FL_RECORDED_CMD_BIT, &file->flags);
			}

			if (tr->trace_flags & TRACE_ITER_RECORD_TGID) {
				tgid = true;
				tracing_start_tgid_record();
				set_bit(EVENT_FILE_FL_RECORDED_TGID_BIT, &file->flags);
			}

			ret = call->class->reg(call, TRACE_REG_REGISTER, file);
			if (ret) {
				if (cmd)
					tracing_stop_cmdline_record();
				if (tgid)
					tracing_stop_tgid_record();
				pr_info("event trace: Could not enable event "
					"%s\n", trace_event_name(call));
				break;
			}
			set_bit(EVENT_FILE_FL_ENABLED_BIT, &file->flags);

			/* WAS_ENABLED gets set but never cleared. */
			set_bit(EVENT_FILE_FL_WAS_ENABLED_BIT, &file->flags);
		}
		break;
	}

	return ret;
}

int trace_event_enable_disable(struct trace_event_file *file,
			       int enable, int soft_disable)
{
	return __ftrace_event_enable_disable(file, enable, soft_disable);
}

static int ftrace_event_enable_disable(struct trace_event_file *file,
				       int enable)
{
	return __ftrace_event_enable_disable(file, enable, 0);
}

#ifdef CONFIG_MODULES
struct event_mod_load {
	struct list_head	list;
	char			*module;
	char			*match;
	char			*system;
	char			*event;
};

static void free_event_mod(struct event_mod_load *event_mod)
{
	list_del(&event_mod->list);
	kfree(event_mod->module);
	kfree(event_mod->match);
	kfree(event_mod->system);
	kfree(event_mod->event);
	kfree(event_mod);
}

static void clear_mod_events(struct trace_array *tr)
{
	struct event_mod_load *event_mod, *n;

	list_for_each_entry_safe(event_mod, n, &tr->mod_events, list) {
		free_event_mod(event_mod);
	}
}

static int remove_cache_mod(struct trace_array *tr, const char *mod,
			    const char *match, const char *system, const char *event)
{
	struct event_mod_load *event_mod, *n;
	int ret = -EINVAL;

	list_for_each_entry_safe(event_mod, n, &tr->mod_events, list) {
		if (strcmp(event_mod->module, mod) != 0)
			continue;

		if (match && strcmp(event_mod->match, match) != 0)
			continue;

		if (system &&
		    (!event_mod->system || strcmp(event_mod->system, system) != 0))
			continue;

		if (event &&
		    (!event_mod->event || strcmp(event_mod->event, event) != 0))
			continue;

		free_event_mod(event_mod);
		ret = 0;
	}

	return ret;
}

static int cache_mod(struct trace_array *tr, const char *mod, int set,
		     const char *match, const char *system, const char *event)
{
	struct event_mod_load *event_mod;

	/* If the module exists, then this just failed to find an event */
	if (module_exists(mod))
		return -EINVAL;

	/* See if this is to remove a cached filter */
	if (!set)
		return remove_cache_mod(tr, mod, match, system, event);

	event_mod = kzalloc(sizeof(*event_mod), GFP_KERNEL);
	if (!event_mod)
		return -ENOMEM;

	INIT_LIST_HEAD(&event_mod->list);
	event_mod->module = kstrdup(mod, GFP_KERNEL);
	if (!event_mod->module)
		goto out_free;

	if (match) {
		event_mod->match = kstrdup(match, GFP_KERNEL);
		if (!event_mod->match)
			goto out_free;
	}

	if (system) {
		event_mod->system = kstrdup(system, GFP_KERNEL);
		if (!event_mod->system)
			goto out_free;
	}

	if (event) {
		event_mod->event = kstrdup(event, GFP_KERNEL);
		if (!event_mod->event)
			goto out_free;
	}

	list_add(&event_mod->list, &tr->mod_events);

	return 0;

 out_free:
	free_event_mod(event_mod);

	return -ENOMEM;
}
#else /* CONFIG_MODULES */
static inline void clear_mod_events(struct trace_array *tr) { }
static int cache_mod(struct trace_array *tr, const char *mod, int set,
		     const char *match, const char *system, const char *event)
{
	return -EINVAL;
}
#endif

static void ftrace_clear_events(struct trace_array *tr)
{
	struct trace_event_file *file;

	mutex_lock(&event_mutex);
	list_for_each_entry(file, &tr->events, list) {
		ftrace_event_enable_disable(file, 0);
	}
	clear_mod_events(tr);
	mutex_unlock(&event_mutex);
}

static void
event_filter_pid_sched_process_exit(void *data, struct task_struct *task)
{
	struct trace_pid_list *pid_list;
	struct trace_array *tr = data;

	pid_list = rcu_dereference_raw(tr->filtered_pids);
	trace_filter_add_remove_task(pid_list, NULL, task);

	pid_list = rcu_dereference_raw(tr->filtered_no_pids);
	trace_filter_add_remove_task(pid_list, NULL, task);
}

static void
event_filter_pid_sched_process_fork(void *data,
				    struct task_struct *self,
				    struct task_struct *task)
{
	struct trace_pid_list *pid_list;
	struct trace_array *tr = data;

	pid_list = rcu_dereference_sched(tr->filtered_pids);
	trace_filter_add_remove_task(pid_list, self, task);

	pid_list = rcu_dereference_sched(tr->filtered_no_pids);
	trace_filter_add_remove_task(pid_list, self, task);
}

void trace_event_follow_fork(struct trace_array *tr, bool enable)
{
	if (enable) {
		register_trace_prio_sched_process_fork(event_filter_pid_sched_process_fork,
						       tr, INT_MIN);
		register_trace_prio_sched_process_free(event_filter_pid_sched_process_exit,
						       tr, INT_MAX);
	} else {
		unregister_trace_sched_process_fork(event_filter_pid_sched_process_fork,
						    tr);
		unregister_trace_sched_process_free(event_filter_pid_sched_process_exit,
						    tr);
	}
}

static void
event_filter_pid_sched_switch_probe_pre(void *data, bool preempt,
					struct task_struct *prev,
					struct task_struct *next,
					unsigned int prev_state)
{
	struct trace_array *tr = data;
	struct trace_pid_list *no_pid_list;
	struct trace_pid_list *pid_list;
	bool ret;

	pid_list = rcu_dereference_sched(tr->filtered_pids);
	no_pid_list = rcu_dereference_sched(tr->filtered_no_pids);

	/*
	 * Sched switch is funny, as we only want to ignore it
	 * in the notrace case if both prev and next should be ignored.
	 */
	ret = trace_ignore_this_task(NULL, no_pid_list, prev) &&
		trace_ignore_this_task(NULL, no_pid_list, next);

	this_cpu_write(tr->array_buffer.data->ignore_pid, ret ||
		       (trace_ignore_this_task(pid_list, NULL, prev) &&
			trace_ignore_this_task(pid_list, NULL, next)));
}

static void
event_filter_pid_sched_switch_probe_post(void *data, bool preempt,
					 struct task_struct *prev,
					 struct task_struct *next,
					 unsigned int prev_state)
{
	struct trace_array *tr = data;
	struct trace_pid_list *no_pid_list;
	struct trace_pid_list *pid_list;

	pid_list = rcu_dereference_sched(tr->filtered_pids);
	no_pid_list = rcu_dereference_sched(tr->filtered_no_pids);

	this_cpu_write(tr->array_buffer.data->ignore_pid,
		       trace_ignore_this_task(pid_list, no_pid_list, next));
}

static void
event_filter_pid_sched_wakeup_probe_pre(void *data, struct task_struct *task)
{
	struct trace_array *tr = data;
	struct trace_pid_list *no_pid_list;
	struct trace_pid_list *pid_list;

	/* Nothing to do if we are already tracing */
	if (!this_cpu_read(tr->array_buffer.data->ignore_pid))
		return;

	pid_list = rcu_dereference_sched(tr->filtered_pids);
	no_pid_list = rcu_dereference_sched(tr->filtered_no_pids);

	this_cpu_write(tr->array_buffer.data->ignore_pid,
		       trace_ignore_this_task(pid_list, no_pid_list, task));
}

static void
event_filter_pid_sched_wakeup_probe_post(void *data, struct task_struct *task)
{
	struct trace_array *tr = data;
	struct trace_pid_list *no_pid_list;
	struct trace_pid_list *pid_list;

	/* Nothing to do if we are not tracing */
	if (this_cpu_read(tr->array_buffer.data->ignore_pid))
		return;

	pid_list = rcu_dereference_sched(tr->filtered_pids);
	no_pid_list = rcu_dereference_sched(tr->filtered_no_pids);

	/* Set tracing if current is enabled */
	this_cpu_write(tr->array_buffer.data->ignore_pid,
		       trace_ignore_this_task(pid_list, no_pid_list, current));
}

static void unregister_pid_events(struct trace_array *tr)
{
	unregister_trace_sched_switch(event_filter_pid_sched_switch_probe_pre, tr);
	unregister_trace_sched_switch(event_filter_pid_sched_switch_probe_post, tr);

	unregister_trace_sched_wakeup(event_filter_pid_sched_wakeup_probe_pre, tr);
	unregister_trace_sched_wakeup(event_filter_pid_sched_wakeup_probe_post, tr);

	unregister_trace_sched_wakeup_new(event_filter_pid_sched_wakeup_probe_pre, tr);
	unregister_trace_sched_wakeup_new(event_filter_pid_sched_wakeup_probe_post, tr);

	unregister_trace_sched_waking(event_filter_pid_sched_wakeup_probe_pre, tr);
	unregister_trace_sched_waking(event_filter_pid_sched_wakeup_probe_post, tr);
}

static void __ftrace_clear_event_pids(struct trace_array *tr, int type)
{
	struct trace_pid_list *pid_list;
	struct trace_pid_list *no_pid_list;
	struct trace_event_file *file;
	int cpu;

	pid_list = rcu_dereference_protected(tr->filtered_pids,
					     lockdep_is_held(&event_mutex));
	no_pid_list = rcu_dereference_protected(tr->filtered_no_pids,
					     lockdep_is_held(&event_mutex));

	/* Make sure there's something to do */
	if (!pid_type_enabled(type, pid_list, no_pid_list))
		return;

	if (!still_need_pid_events(type, pid_list, no_pid_list)) {
		unregister_pid_events(tr);

		list_for_each_entry(file, &tr->events, list) {
			clear_bit(EVENT_FILE_FL_PID_FILTER_BIT, &file->flags);
		}

		for_each_possible_cpu(cpu)
			per_cpu_ptr(tr->array_buffer.data, cpu)->ignore_pid = false;
	}

	if (type & TRACE_PIDS)
		rcu_assign_pointer(tr->filtered_pids, NULL);

	if (type & TRACE_NO_PIDS)
		rcu_assign_pointer(tr->filtered_no_pids, NULL);

	/* Wait till all users are no longer using pid filtering */
	tracepoint_synchronize_unregister();

	if ((type & TRACE_PIDS) && pid_list)
		trace_pid_list_free(pid_list);

	if ((type & TRACE_NO_PIDS) && no_pid_list)
		trace_pid_list_free(no_pid_list);
}

static void ftrace_clear_event_pids(struct trace_array *tr, int type)
{
	mutex_lock(&event_mutex);
	__ftrace_clear_event_pids(tr, type);
	mutex_unlock(&event_mutex);
}

static void __put_system(struct event_subsystem *system)
{
	struct event_filter *filter = system->filter;

	WARN_ON_ONCE(system_refcount(system) == 0);
	if (system_refcount_dec(system))
		return;

	list_del(&system->list);

	if (filter) {
		kfree(filter->filter_string);
		kfree(filter);
	}
	kfree_const(system->name);
	kfree(system);
}

static void __get_system(struct event_subsystem *system)
{
	WARN_ON_ONCE(system_refcount(system) == 0);
	system_refcount_inc(system);
}

static void __get_system_dir(struct trace_subsystem_dir *dir)
{
	WARN_ON_ONCE(dir->ref_count == 0);
	dir->ref_count++;
	__get_system(dir->subsystem);
}

static void __put_system_dir(struct trace_subsystem_dir *dir)
{
	WARN_ON_ONCE(dir->ref_count == 0);
	/* If the subsystem is about to be freed, the dir must be too */
	WARN_ON_ONCE(system_refcount(dir->subsystem) == 1 && dir->ref_count != 1);

	__put_system(dir->subsystem);
	if (!--dir->ref_count)
		kfree(dir);
}

static void put_system(struct trace_subsystem_dir *dir)
{
	mutex_lock(&event_mutex);
	__put_system_dir(dir);
	mutex_unlock(&event_mutex);
}

static void remove_subsystem(struct trace_subsystem_dir *dir)
{
	if (!dir)
		return;

	if (!--dir->nr_events) {
		eventfs_remove_dir(dir->ei);
		list_del(&dir->list);
		__put_system_dir(dir);
	}
}

void event_file_get(struct trace_event_file *file)
{
	refcount_inc(&file->ref);
}

void event_file_put(struct trace_event_file *file)
{
	if (WARN_ON_ONCE(!refcount_read(&file->ref))) {
		if (file->flags & EVENT_FILE_FL_FREED)
			kmem_cache_free(file_cachep, file);
		return;
	}

	if (refcount_dec_and_test(&file->ref)) {
		/* Count should only go to zero when it is freed */
		if (WARN_ON_ONCE(!(file->flags & EVENT_FILE_FL_FREED)))
			return;
		kmem_cache_free(file_cachep, file);
	}
}

static void remove_event_file_dir(struct trace_event_file *file)
{
	eventfs_remove_dir(file->ei);
	list_del(&file->list);
	remove_subsystem(file->system);
	free_event_filter(file->filter);
	file->flags |= EVENT_FILE_FL_FREED;
	event_file_put(file);
}

/*
 * __ftrace_set_clr_event(NULL, NULL, NULL, set) will set/unset all events.
 */
static int
__ftrace_set_clr_event_nolock(struct trace_array *tr, const char *match,
			      const char *sub, const char *event, int set,
			      const char *mod)
{
	struct trace_event_file *file;
	struct trace_event_call *call;
	char *module __free(kfree) = NULL;
	const char *name;
	int ret = -EINVAL;
	int eret = 0;

	if (mod) {
		char *p;

		module = kstrdup(mod, GFP_KERNEL);
		if (!module)
			return -ENOMEM;

		/* Replace all '-' with '_' as that's what modules do */
		for (p = strchr(module, '-'); p; p = strchr(p + 1, '-'))
			*p = '_';
	}

	list_for_each_entry(file, &tr->events, list) {

		call = file->event_call;

		/* If a module is specified, skip events that are not that module */
		if (module && (!call->module || strcmp(module_name(call->module), module)))
			continue;

		name = trace_event_name(call);

		if (!name || !call->class || !call->class->reg)
			continue;

		if (call->flags & TRACE_EVENT_FL_IGNORE_ENABLE)
			continue;

		if (match &&
		    strcmp(match, name) != 0 &&
		    strcmp(match, call->class->system) != 0)
			continue;

		if (sub && strcmp(sub, call->class->system) != 0)
			continue;

		if (event && strcmp(event, name) != 0)
			continue;

		ret = ftrace_event_enable_disable(file, set);

		/*
		 * Save the first error and return that. Some events
		 * may still have been enabled, but let the user
		 * know that something went wrong.
		 */
		if (ret && !eret)
			eret = ret;

		ret = eret;
	}

	/*
	 * If this is a module setting and nothing was found,
	 * check if the module was loaded. If it wasn't cache it.
	 */
	if (module && ret == -EINVAL && !eret)
		ret = cache_mod(tr, module, set, match, sub, event);

	return ret;
}

static int __ftrace_set_clr_event(struct trace_array *tr, const char *match,
				  const char *sub, const char *event, int set,
				  const char *mod)
{
	int ret;

	mutex_lock(&event_mutex);
	ret = __ftrace_set_clr_event_nolock(tr, match, sub, event, set, mod);
	mutex_unlock(&event_mutex);

	return ret;
}

int ftrace_set_clr_event(struct trace_array *tr, char *buf, int set)
{
	char *event = NULL, *sub = NULL, *match, *mod;
	int ret;

	if (!tr)
		return -ENOENT;

	/* Modules events can be appened with :mod:<module> */
	mod = strstr(buf, ":mod:");
	if (mod) {
		*mod = '\0';
		/* move to the module name */
		mod += 5;
	}

	/*
	 * The buf format can be <subsystem>:<event-name>
	 *  *:<event-name> means any event by that name.
	 *  :<event-name> is the same.
	 *
	 *  <subsystem>:* means all events in that subsystem
	 *  <subsystem>: means the same.
	 *
	 *  <name> (no ':') means all events in a subsystem with
	 *  the name <name> or any event that matches <name>
	 */

	match = strsep(&buf, ":");
	if (buf) {
		sub = match;
		event = buf;
		match = NULL;

		if (!strlen(sub) || strcmp(sub, "*") == 0)
			sub = NULL;
		if (!strlen(event) || strcmp(event, "*") == 0)
			event = NULL;
	} else if (mod) {
		/* Allow wildcard for no length or star */
		if (!strlen(match) || strcmp(match, "*") == 0)
			match = NULL;
	}

	ret = __ftrace_set_clr_event(tr, match, sub, event, set, mod);

	/* Put back the colon to allow this to be called again */
	if (buf)
		*(buf - 1) = ':';

	return ret;
}

/**
 * trace_set_clr_event - enable or disable an event
 * @system: system name to match (NULL for any system)
 * @event: event name to match (NULL for all events, within system)
 * @set: 1 to enable, 0 to disable
 *
 * This is a way for other parts of the kernel to enable or disable
 * event recording.
 *
 * Returns 0 on success, -EINVAL if the parameters do not match any
 * registered events.
 */
int trace_set_clr_event(const char *system, const char *event, int set)
{
	struct trace_array *tr = top_trace_array();

	if (!tr)
		return -ENODEV;

	return __ftrace_set_clr_event(tr, NULL, system, event, set, NULL);
}
EXPORT_SYMBOL_GPL(trace_set_clr_event);

/**
 * trace_array_set_clr_event - enable or disable an event for a trace array.
 * @tr: concerned trace array.
 * @system: system name to match (NULL for any system)
 * @event: event name to match (NULL for all events, within system)
 * @enable: true to enable, false to disable
 *
 * This is a way for other parts of the kernel to enable or disable
 * event recording.
 *
 * Returns 0 on success, -EINVAL if the parameters do not match any
 * registered events.
 */
int trace_array_set_clr_event(struct trace_array *tr, const char *system,
		const char *event, bool enable)
{
	int set;

	if (!tr)
		return -ENOENT;

	set = (enable == true) ? 1 : 0;
	return __ftrace_set_clr_event(tr, NULL, system, event, set, NULL);
}
EXPORT_SYMBOL_GPL(trace_array_set_clr_event);

/* 128 should be much more than enough */
#define EVENT_BUF_SIZE		127

static ssize_t
ftrace_event_write(struct file *file, const char __user *ubuf,
		   size_t cnt, loff_t *ppos)
{
	struct trace_parser parser;
	struct seq_file *m = file->private_data;
	struct trace_array *tr = m->private;
	ssize_t read, ret;

	if (!cnt)
		return 0;

	ret = tracing_update_buffers(tr);
	if (ret < 0)
		return ret;

	if (trace_parser_get_init(&parser, EVENT_BUF_SIZE + 1))
		return -ENOMEM;

	read = trace_get_user(&parser, ubuf, cnt, ppos);

	if (read >= 0 && trace_parser_loaded((&parser))) {
		int set = 1;

		if (*parser.buffer == '!')
			set = 0;

		ret = ftrace_set_clr_event(tr, parser.buffer + !set, set);
		if (ret)
			goto out_put;
	}

	ret = read;

 out_put:
	trace_parser_put(&parser);

	return ret;
}

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct trace_event_file *file = v;
	struct trace_event_call *call;
	struct trace_array *tr = m->private;

	(*pos)++;

	list_for_each_entry_continue(file, &tr->events, list) {
		call = file->event_call;
		/*
		 * The ftrace subsystem is for showing formats only.
		 * They can not be enabled or disabled via the event files.
		 */
		if (call->class && call->class->reg &&
		    !(call->flags & TRACE_EVENT_FL_IGNORE_ENABLE))
			return file;
	}

	return NULL;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct trace_event_file *file;
	struct trace_array *tr = m->private;
	loff_t l;

	mutex_lock(&event_mutex);

	file = list_entry(&tr->events, struct trace_event_file, list);
	for (l = 0; l <= *pos; ) {
		file = t_next(m, file, &l);
		if (!file)
			break;
	}
	return file;
}

enum set_event_iter_type {
	SET_EVENT_FILE,
	SET_EVENT_MOD,
};

struct set_event_iter {
	enum set_event_iter_type	type;
	union {
		struct trace_event_file	*file;
		struct event_mod_load	*event_mod;
	};
};

static void *
s_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct set_event_iter *iter = v;
	struct trace_event_file *file;
	struct trace_array *tr = m->private;

	(*pos)++;

	if (iter->type == SET_EVENT_FILE) {
		file = iter->file;
		list_for_each_entry_continue(file, &tr->events, list) {
			if (file->flags & EVENT_FILE_FL_ENABLED) {
				iter->file = file;
				return iter;
			}
		}
#ifdef CONFIG_MODULES
		iter->type = SET_EVENT_MOD;
		iter->event_mod = list_entry(&tr->mod_events, struct event_mod_load, list);
#endif
	}

#ifdef CONFIG_MODULES
	list_for_each_entry_continue(iter->event_mod, &tr->mod_events, list)
		return iter;
#endif

	/*
	 * The iter is allocated in s_start() and passed via the 'v'
	 * parameter. To stop the iterator, NULL must be returned. But
	 * the return value is what the 'v' parameter in s_stop() receives
	 * and frees. Free iter here as it will no longer be used.
	 */
	kfree(iter);
	return NULL;
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	struct trace_array *tr = m->private;
	struct set_event_iter *iter;
	loff_t l;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	mutex_lock(&event_mutex);
	if (!iter)
		return NULL;

	iter->type = SET_EVENT_FILE;
	iter->file = list_entry(&tr->events, struct trace_event_file, list);

	for (l = 0; l <= *pos; ) {
		iter = s_next(m, iter, &l);
		if (!iter)
			break;
	}
	return iter;
}

static int t_show(struct seq_file *m, void *v)
{
	struct trace_event_file *file = v;
	struct trace_event_call *call = file->event_call;

	if (strcmp(call->class->system, TRACE_SYSTEM) != 0)
		seq_printf(m, "%s:", call->class->system);
	seq_printf(m, "%s\n", trace_event_name(call));

	return 0;
}

static void t_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&event_mutex);
}

#ifdef CONFIG_MODULES
static int s_show(struct seq_file *m, void *v)
{
	struct set_event_iter *iter = v;
	const char *system;
	const char *event;

	if (iter->type == SET_EVENT_FILE)
		return t_show(m, iter->file);

	/* When match is set, system and event are not */
	if (iter->event_mod->match) {
		seq_printf(m, "%s:mod:%s\n", iter->event_mod->match,
			   iter->event_mod->module);
		return 0;
	}

	system = iter->event_mod->system ? : "*";
	event = iter->event_mod->event ? : "*";

	seq_printf(m, "%s:%s:mod:%s\n", system, event, iter->event_mod->module);

	return 0;
}
#else /* CONFIG_MODULES */
static int s_show(struct seq_file *m, void *v)
{
	struct set_event_iter *iter = v;

	return t_show(m, iter->file);
}
#endif

static void s_stop(struct seq_file *m, void *v)
{
	kfree(v);
	t_stop(m, NULL);
}

static void *
__next(struct seq_file *m, void *v, loff_t *pos, int type)
{
	struct trace_array *tr = m->private;
	struct trace_pid_list *pid_list;

	if (type == TRACE_PIDS)
		pid_list = rcu_dereference_sched(tr->filtered_pids);
	else
		pid_list = rcu_dereference_sched(tr->filtered_no_pids);

	return trace_pid_next(pid_list, v, pos);
}

static void *
p_next(struct seq_file *m, void *v, loff_t *pos)
{
	return __next(m, v, pos, TRACE_PIDS);
}

static void *
np_next(struct seq_file *m, void *v, loff_t *pos)
{
	return __next(m, v, pos, TRACE_NO_PIDS);
}

static void *__start(struct seq_file *m, loff_t *pos, int type)
	__acquires(RCU)
{
	struct trace_pid_list *pid_list;
	struct trace_array *tr = m->private;

	/*
	 * Grab the mutex, to keep calls to p_next() having the same
	 * tr->filtered_pids as p_start() has.
	 * If we just passed the tr->filtered_pids around, then RCU would
	 * have been enough, but doing that makes things more complex.
	 */
	mutex_lock(&event_mutex);
	rcu_read_lock_sched();

	if (type == TRACE_PIDS)
		pid_list = rcu_dereference_sched(tr->filtered_pids);
	else
		pid_list = rcu_dereference_sched(tr->filtered_no_pids);

	if (!pid_list)
		return NULL;

	return trace_pid_start(pid_list, pos);
}

static void *p_start(struct seq_file *m, loff_t *pos)
	__acquires(RCU)
{
	return __start(m, pos, TRACE_PIDS);
}

static void *np_start(struct seq_file *m, loff_t *pos)
	__acquires(RCU)
{
	return __start(m, pos, TRACE_NO_PIDS);
}

static void p_stop(struct seq_file *m, void *p)
	__releases(RCU)
{
	rcu_read_unlock_sched();
	mutex_unlock(&event_mutex);
}

static ssize_t
event_enable_read(struct file *filp, char __user *ubuf, size_t cnt,
		  loff_t *ppos)
{
	struct trace_event_file *file;
	unsigned long flags;
	char buf[4] = "0";

	mutex_lock(&event_mutex);
	file = event_file_file(filp);
	if (likely(file))
		flags = file->flags;
	mutex_unlock(&event_mutex);

	if (!file)
		return -ENODEV;

	if (flags & EVENT_FILE_FL_ENABLED &&
	    !(flags & EVENT_FILE_FL_SOFT_DISABLED))
		strcpy(buf, "1");

	if (atomic_read(&file->sm_ref) != 0)
		strcat(buf, "*");

	strcat(buf, "\n");

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, strlen(buf));
}

static ssize_t
event_enable_write(struct file *filp, const char __user *ubuf, size_t cnt,
		   loff_t *ppos)
{
	struct trace_event_file *file;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	guard(mutex)(&event_mutex);

	switch (val) {
	case 0:
	case 1:
		file = event_file_file(filp);
		if (!file)
			return -ENODEV;
		ret = tracing_update_buffers(file->tr);
		if (ret < 0)
			return ret;
		ret = ftrace_event_enable_disable(file, val);
		if (ret < 0)
			return ret;
		break;

	default:
		return -EINVAL;
	}

	*ppos += cnt;

	return cnt;
}

/*
 * Returns:
 *   0 : no events exist?
 *   1 : all events are disabled
 *   2 : all events are enabled
 *   3 : some events are enabled and some are enabled
 */
int trace_events_enabled(struct trace_array *tr, const char *system)
{
	struct trace_event_call *call;
	struct trace_event_file *file;
	int set = 0;

	guard(mutex)(&event_mutex);

	list_for_each_entry(file, &tr->events, list) {
		call = file->event_call;
		if ((call->flags & TRACE_EVENT_FL_IGNORE_ENABLE) ||
		    !trace_event_name(call) || !call->class || !call->class->reg)
			continue;

		if (system && strcmp(call->class->system, system) != 0)
			continue;

		/*
		 * We need to find out if all the events are set
		 * or if all events or cleared, or if we have
		 * a mixture.
		 */
		set |= (1 << !!(file->flags & EVENT_FILE_FL_ENABLED));

		/*
		 * If we have a mixture, no need to look further.
		 */
		if (set == 3)
			break;
	}

	return set;
}

static ssize_t
system_enable_read(struct file *filp, char __user *ubuf, size_t cnt,
		   loff_t *ppos)
{
	const char set_to_char[4] = { '?', '0', '1', 'X' };
	struct trace_subsystem_dir *dir = filp->private_data;
	struct event_subsystem *system = dir->subsystem;
	struct trace_array *tr = dir->tr;
	char buf[2];
	int set;
	int ret;

	set = trace_events_enabled(tr, system ? system->name : NULL);

	buf[0] = set_to_char[set];
	buf[1] = '\n';

	ret = simple_read_from_buffer(ubuf, cnt, ppos, buf, 2);

	return ret;
}

static ssize_t
system_enable_write(struct file *filp, const char __user *ubuf, size_t cnt,
		    loff_t *ppos)
{
	struct trace_subsystem_dir *dir = filp->private_data;
	struct event_subsystem *system = dir->subsystem;
	const char *name = NULL;
	unsigned long val;
	ssize_t ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	ret = tracing_update_buffers(dir->tr);
	if (ret < 0)
		return ret;

	if (val != 0 && val != 1)
		return -EINVAL;

	/*
	 * Opening of "enable" adds a ref count to system,
	 * so the name is safe to use.
	 */
	if (system)
		name = system->name;

	ret = __ftrace_set_clr_event(dir->tr, NULL, name, NULL, val, NULL);
	if (ret)
		goto out;

	ret = cnt;

out:
	*ppos += cnt;

	return ret;
}

enum {
	FORMAT_HEADER		= 1,
	FORMAT_FIELD_SEPERATOR	= 2,
	FORMAT_PRINTFMT		= 3,
};

static void *f_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct trace_event_file *file = event_file_data(m->private);
	struct trace_event_call *call = file->event_call;
	struct list_head *common_head = &ftrace_common_fields;
	struct list_head *head = trace_get_fields(call);
	struct list_head *node = v;

	(*pos)++;

	switch ((unsigned long)v) {
	case FORMAT_HEADER:
		node = common_head;
		break;

	case FORMAT_FIELD_SEPERATOR:
		node = head;
		break;

	case FORMAT_PRINTFMT:
		/* all done */
		return NULL;
	}

	node = node->prev;
	if (node == common_head)
		return (void *)FORMAT_FIELD_SEPERATOR;
	else if (node == head)
		return (void *)FORMAT_PRINTFMT;
	else
		return node;
}

static int f_show(struct seq_file *m, void *v)
{
	struct trace_event_file *file = event_file_data(m->private);
	struct trace_event_call *call = file->event_call;
	struct ftrace_event_field *field;
	const char *array_descriptor;

	switch ((unsigned long)v) {
	case FORMAT_HEADER:
		seq_printf(m, "name: %s\n", trace_event_name(call));
		seq_printf(m, "ID: %d\n", call->event.type);
		seq_puts(m, "format:\n");
		return 0;

	case FORMAT_FIELD_SEPERATOR:
		seq_putc(m, '\n');
		return 0;

	case FORMAT_PRINTFMT:
		seq_printf(m, "\nprint fmt: %s\n",
			   call->print_fmt);
		return 0;
	}

	field = list_entry(v, struct ftrace_event_field, link);
	/*
	 * Smartly shows the array type(except dynamic array).
	 * Normal:
	 *	field:TYPE VAR
	 * If TYPE := TYPE[LEN], it is shown:
	 *	field:TYPE VAR[LEN]
	 */
	array_descriptor = strchr(field->type, '[');

	if (str_has_prefix(field->type, "__data_loc"))
		array_descriptor = NULL;

	if (!array_descriptor)
		seq_printf(m, "\tfield:%s %s;\toffset:%u;\tsize:%u;\tsigned:%d;\n",
			   field->type, field->name, field->offset,
			   field->size, !!field->is_signed);
	else if (field->len)
		seq_printf(m, "\tfield:%.*s %s[%d];\toffset:%u;\tsize:%u;\tsigned:%d;\n",
			   (int)(array_descriptor - field->type),
			   field->type, field->name,
			   field->len, field->offset,
			   field->size, !!field->is_signed);
	else
		seq_printf(m, "\tfield:%.*s %s[];\toffset:%u;\tsize:%u;\tsigned:%d;\n",
				(int)(array_descriptor - field->type),
				field->type, field->name,
				field->offset, field->size, !!field->is_signed);

	return 0;
}

static void *f_start(struct seq_file *m, loff_t *pos)
{
	struct trace_event_file *file;
	void *p = (void *)FORMAT_HEADER;
	loff_t l = 0;

	/* ->stop() is called even if ->start() fails */
	mutex_lock(&event_mutex);
	file = event_file_file(m->private);
	if (!file)
		return ERR_PTR(-ENODEV);

	while (l < *pos && p)
		p = f_next(m, p, &l);

	return p;
}

static void f_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&event_mutex);
}

static const struct seq_operations trace_format_seq_ops = {
	.start		= f_start,
	.next		= f_next,
	.stop		= f_stop,
	.show		= f_show,
};

static int trace_format_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	/* Do we want to hide event format files on tracefs lockdown? */

	ret = seq_open(file, &trace_format_seq_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = file;

	return 0;
}

#ifdef CONFIG_PERF_EVENTS
static ssize_t
event_id_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int id = (long)event_file_data(filp);
	char buf[32];
	int len;

	if (unlikely(!id))
		return -ENODEV;

	len = sprintf(buf, "%d\n", id);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}
#endif

static ssize_t
event_filter_read(struct file *filp, char __user *ubuf, size_t cnt,
		  loff_t *ppos)
{
	struct trace_event_file *file;
	struct trace_seq *s;
	int r = -ENODEV;

	if (*ppos)
		return 0;

	s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	mutex_lock(&event_mutex);
	file = event_file_file(filp);
	if (file)
		print_event_filter(file, s);
	mutex_unlock(&event_mutex);

	if (file)
		r = simple_read_from_buffer(ubuf, cnt, ppos,
					    s->buffer, trace_seq_used(s));

	kfree(s);

	return r;
}

static ssize_t
event_filter_write(struct file *filp, const char __user *ubuf, size_t cnt,
		   loff_t *ppos)
{
	struct trace_event_file *file;
	char *buf;
	int err = -ENODEV;

	if (cnt >= PAGE_SIZE)
		return -EINVAL;

	buf = memdup_user_nul(ubuf, cnt);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	mutex_lock(&event_mutex);
	file = event_file_file(filp);
	if (file) {
		if (file->flags & EVENT_FILE_FL_FREED)
			err = -ENODEV;
		else
			err = apply_event_filter(file, buf);
	}
	mutex_unlock(&event_mutex);

	kfree(buf);
	if (err < 0)
		return err;

	*ppos += cnt;

	return cnt;
}

static LIST_HEAD(event_subsystems);

static int subsystem_open(struct inode *inode, struct file *filp)
{
	struct trace_subsystem_dir *dir = NULL, *iter_dir;
	struct trace_array *tr = NULL, *iter_tr;
	struct event_subsystem *system = NULL;
	int ret;

	if (tracing_is_disabled())
		return -ENODEV;

	/* Make sure the system still exists */
	mutex_lock(&event_mutex);
	mutex_lock(&trace_types_lock);
	list_for_each_entry(iter_tr, &ftrace_trace_arrays, list) {
		list_for_each_entry(iter_dir, &iter_tr->systems, list) {
			if (iter_dir == inode->i_private) {
				/* Don't open systems with no events */
				tr = iter_tr;
				dir = iter_dir;
				if (dir->nr_events) {
					__get_system_dir(dir);
					system = dir->subsystem;
				}
				goto exit_loop;
			}
		}
	}
 exit_loop:
	mutex_unlock(&trace_types_lock);
	mutex_unlock(&event_mutex);

	if (!system)
		return -ENODEV;

	/* Still need to increment the ref count of the system */
	if (trace_array_get(tr) < 0) {
		put_system(dir);
		return -ENODEV;
	}

	ret = tracing_open_generic(inode, filp);
	if (ret < 0) {
		trace_array_put(tr);
		put_system(dir);
	}

	return ret;
}

static int system_tr_open(struct inode *inode, struct file *filp)
{
	struct trace_subsystem_dir *dir;
	struct trace_array *tr = inode->i_private;
	int ret;

	/* Make a temporary dir that has no system but points to tr */
	dir = kzalloc(sizeof(*dir), GFP_KERNEL);
	if (!dir)
		return -ENOMEM;

	ret = tracing_open_generic_tr(inode, filp);
	if (ret < 0) {
		kfree(dir);
		return ret;
	}
	dir->tr = tr;
	filp->private_data = dir;

	return 0;
}

static int subsystem_release(struct inode *inode, struct file *file)
{
	struct trace_subsystem_dir *dir = file->private_data;

	trace_array_put(dir->tr);

	/*
	 * If dir->subsystem is NULL, then this is a temporary
	 * descriptor that was made for a trace_array to enable
	 * all subsystems.
	 */
	if (dir->subsystem)
		put_system(dir);
	else
		kfree(dir);

	return 0;
}

static ssize_t
subsystem_filter_read(struct file *filp, char __user *ubuf, size_t cnt,
		      loff_t *ppos)
{
	struct trace_subsystem_dir *dir = filp->private_data;
	struct event_subsystem *system = dir->subsystem;
	struct trace_seq *s;
	int r;

	if (*ppos)
		return 0;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	print_subsystem_event_filter(system, s);
	r = simple_read_from_buffer(ubuf, cnt, ppos,
				    s->buffer, trace_seq_used(s));

	kfree(s);

	return r;
}

static ssize_t
subsystem_filter_write(struct file *filp, const char __user *ubuf, size_t cnt,
		       loff_t *ppos)
{
	struct trace_subsystem_dir *dir = filp->private_data;
	char *buf;
	int err;

	if (cnt >= PAGE_SIZE)
		return -EINVAL;

	buf = memdup_user_nul(ubuf, cnt);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	err = apply_subsystem_event_filter(dir, buf);
	kfree(buf);
	if (err < 0)
		return err;

	*ppos += cnt;

	return cnt;
}

static ssize_t
show_header_page_file(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	struct trace_seq *s;
	int r;

	if (*ppos)
		return 0;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	ring_buffer_print_page_header(tr->array_buffer.buffer, s);
	r = simple_read_from_buffer(ubuf, cnt, ppos,
				    s->buffer, trace_seq_used(s));

	kfree(s);

	return r;
}

static ssize_t
show_header_event_file(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct trace_seq *s;
	int r;

	if (*ppos)
		return 0;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	ring_buffer_print_entry_header(s);
	r = simple_read_from_buffer(ubuf, cnt, ppos,
				    s->buffer, trace_seq_used(s));

	kfree(s);

	return r;
}

static void ignore_task_cpu(void *data)
{
	struct trace_array *tr = data;
	struct trace_pid_list *pid_list;
	struct trace_pid_list *no_pid_list;

	/*
	 * This function is called by on_each_cpu() while the
	 * event_mutex is held.
	 */
	pid_list = rcu_dereference_protected(tr->filtered_pids,
					     mutex_is_locked(&event_mutex));
	no_pid_list = rcu_dereference_protected(tr->filtered_no_pids,
					     mutex_is_locked(&event_mutex));

	this_cpu_write(tr->array_buffer.data->ignore_pid,
		       trace_ignore_this_task(pid_list, no_pid_list, current));
}

static void register_pid_events(struct trace_array *tr)
{
	/*
	 * Register a probe that is called before all other probes
	 * to set ignore_pid if next or prev do not match.
	 * Register a probe this is called after all other probes
	 * to only keep ignore_pid set if next pid matches.
	 */
	register_trace_prio_sched_switch(event_filter_pid_sched_switch_probe_pre,
					 tr, INT_MAX);
	register_trace_prio_sched_switch(event_filter_pid_sched_switch_probe_post,
					 tr, 0);

	register_trace_prio_sched_wakeup(event_filter_pid_sched_wakeup_probe_pre,
					 tr, INT_MAX);
	register_trace_prio_sched_wakeup(event_filter_pid_sched_wakeup_probe_post,
					 tr, 0);

	register_trace_prio_sched_wakeup_new(event_filter_pid_sched_wakeup_probe_pre,
					     tr, INT_MAX);
	register_trace_prio_sched_wakeup_new(event_filter_pid_sched_wakeup_probe_post,
					     tr, 0);

	register_trace_prio_sched_waking(event_filter_pid_sched_wakeup_probe_pre,
					 tr, INT_MAX);
	register_trace_prio_sched_waking(event_filter_pid_sched_wakeup_probe_post,
					 tr, 0);
}

static ssize_t
event_pid_write(struct file *filp, const char __user *ubuf,
		size_t cnt, loff_t *ppos, int type)
{
	struct seq_file *m = filp->private_data;
	struct trace_array *tr = m->private;
	struct trace_pid_list *filtered_pids = NULL;
	struct trace_pid_list *other_pids = NULL;
	struct trace_pid_list *pid_list;
	struct trace_event_file *file;
	ssize_t ret;

	if (!cnt)
		return 0;

	ret = tracing_update_buffers(tr);
	if (ret < 0)
		return ret;

	guard(mutex)(&event_mutex);

	if (type == TRACE_PIDS) {
		filtered_pids = rcu_dereference_protected(tr->filtered_pids,
							  lockdep_is_held(&event_mutex));
		other_pids = rcu_dereference_protected(tr->filtered_no_pids,
							  lockdep_is_held(&event_mutex));
	} else {
		filtered_pids = rcu_dereference_protected(tr->filtered_no_pids,
							  lockdep_is_held(&event_mutex));
		other_pids = rcu_dereference_protected(tr->filtered_pids,
							  lockdep_is_held(&event_mutex));
	}

	ret = trace_pid_write(filtered_pids, &pid_list, ubuf, cnt);
	if (ret < 0)
		return ret;

	if (type == TRACE_PIDS)
		rcu_assign_pointer(tr->filtered_pids, pid_list);
	else
		rcu_assign_pointer(tr->filtered_no_pids, pid_list);

	list_for_each_entry(file, &tr->events, list) {
		set_bit(EVENT_FILE_FL_PID_FILTER_BIT, &file->flags);
	}

	if (filtered_pids) {
		tracepoint_synchronize_unregister();
		trace_pid_list_free(filtered_pids);
	} else if (pid_list && !other_pids) {
		register_pid_events(tr);
	}

	/*
	 * Ignoring of pids is done at task switch. But we have to
	 * check for those tasks that are currently running.
	 * Always do this in case a pid was appended or removed.
	 */
	on_each_cpu(ignore_task_cpu, tr, 1);

	*ppos += ret;

	return ret;
}

static ssize_t
ftrace_event_pid_write(struct file *filp, const char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	return event_pid_write(filp, ubuf, cnt, ppos, TRACE_PIDS);
}

static ssize_t
ftrace_event_npid_write(struct file *filp, const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	return event_pid_write(filp, ubuf, cnt, ppos, TRACE_NO_PIDS);
}

static int ftrace_event_avail_open(struct inode *inode, struct file *file);
static int ftrace_event_set_open(struct inode *inode, struct file *file);
static int ftrace_event_set_pid_open(struct inode *inode, struct file *file);
static int ftrace_event_set_npid_open(struct inode *inode, struct file *file);
static int ftrace_event_release(struct inode *inode, struct file *file);

static const struct seq_operations show_event_seq_ops = {
	.start = t_start,
	.next = t_next,
	.show = t_show,
	.stop = t_stop,
};

static const struct seq_operations show_set_event_seq_ops = {
	.start = s_start,
	.next = s_next,
	.show = s_show,
	.stop = s_stop,
};

static const struct seq_operations show_set_pid_seq_ops = {
	.start = p_start,
	.next = p_next,
	.show = trace_pid_show,
	.stop = p_stop,
};

static const struct seq_operations show_set_no_pid_seq_ops = {
	.start = np_start,
	.next = np_next,
	.show = trace_pid_show,
	.stop = p_stop,
};

static const struct file_operations ftrace_avail_fops = {
	.open = ftrace_event_avail_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static const struct file_operations ftrace_set_event_fops = {
	.open = ftrace_event_set_open,
	.read = seq_read,
	.write = ftrace_event_write,
	.llseek = seq_lseek,
	.release = ftrace_event_release,
};

static const struct file_operations ftrace_set_event_pid_fops = {
	.open = ftrace_event_set_pid_open,
	.read = seq_read,
	.write = ftrace_event_pid_write,
	.llseek = seq_lseek,
	.release = ftrace_event_release,
};

static const struct file_operations ftrace_set_event_notrace_pid_fops = {
	.open = ftrace_event_set_npid_open,
	.read = seq_read,
	.write = ftrace_event_npid_write,
	.llseek = seq_lseek,
	.release = ftrace_event_release,
};

static const struct file_operations ftrace_enable_fops = {
	.open = tracing_open_file_tr,
	.read = event_enable_read,
	.write = event_enable_write,
	.release = tracing_release_file_tr,
	.llseek = default_llseek,
};

static const struct file_operations ftrace_event_format_fops = {
	.open = trace_format_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

#ifdef CONFIG_PERF_EVENTS
static const struct file_operations ftrace_event_id_fops = {
	.read = event_id_read,
	.llseek = default_llseek,
};
#endif

static const struct file_operations ftrace_event_filter_fops = {
	.open = tracing_open_file_tr,
	.read = event_filter_read,
	.write = event_filter_write,
	.release = tracing_release_file_tr,
	.llseek = default_llseek,
};

static const struct file_operations ftrace_subsystem_filter_fops = {
	.open = subsystem_open,
	.read = subsystem_filter_read,
	.write = subsystem_filter_write,
	.llseek = default_llseek,
	.release = subsystem_release,
};

static const struct file_operations ftrace_system_enable_fops = {
	.open = subsystem_open,
	.read = system_enable_read,
	.write = system_enable_write,
	.llseek = default_llseek,
	.release = subsystem_release,
};

static const struct file_operations ftrace_tr_enable_fops = {
	.open = system_tr_open,
	.read = system_enable_read,
	.write = system_enable_write,
	.llseek = default_llseek,
	.release = subsystem_release,
};

static const struct file_operations ftrace_show_header_page_fops = {
	.open = tracing_open_generic_tr,
	.read = show_header_page_file,
	.llseek = default_llseek,
	.release = tracing_release_generic_tr,
};

static const struct file_operations ftrace_show_header_event_fops = {
	.open = tracing_open_generic_tr,
	.read = show_header_event_file,
	.llseek = default_llseek,
	.release = tracing_release_generic_tr,
};

static int
ftrace_event_open(struct inode *inode, struct file *file,
		  const struct seq_operations *seq_ops)
{
	struct seq_file *m;
	int ret;

	ret = security_locked_down(LOCKDOWN_TRACEFS);
	if (ret)
		return ret;

	ret = seq_open(file, seq_ops);
	if (ret < 0)
		return ret;
	m = file->private_data;
	/* copy tr over to seq ops */
	m->private = inode->i_private;

	return ret;
}

static int ftrace_event_release(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;

	trace_array_put(tr);

	return seq_release(inode, file);
}

static int
ftrace_event_avail_open(struct inode *inode, struct file *file)
{
	const struct seq_operations *seq_ops = &show_event_seq_ops;

	/* Checks for tracefs lockdown */
	return ftrace_event_open(inode, file, seq_ops);
}

static int
ftrace_event_set_open(struct inode *inode, struct file *file)
{
	const struct seq_operations *seq_ops = &show_set_event_seq_ops;
	struct trace_array *tr = inode->i_private;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC))
		ftrace_clear_events(tr);

	ret = ftrace_event_open(inode, file, seq_ops);
	if (ret < 0)
		trace_array_put(tr);
	return ret;
}

static int
ftrace_event_set_pid_open(struct inode *inode, struct file *file)
{
	const struct seq_operations *seq_ops = &show_set_pid_seq_ops;
	struct trace_array *tr = inode->i_private;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC))
		ftrace_clear_event_pids(tr, TRACE_PIDS);

	ret = ftrace_event_open(inode, file, seq_ops);
	if (ret < 0)
		trace_array_put(tr);
	return ret;
}

static int
ftrace_event_set_npid_open(struct inode *inode, struct file *file)
{
	const struct seq_operations *seq_ops = &show_set_no_pid_seq_ops;
	struct trace_array *tr = inode->i_private;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC))
		ftrace_clear_event_pids(tr, TRACE_NO_PIDS);

	ret = ftrace_event_open(inode, file, seq_ops);
	if (ret < 0)
		trace_array_put(tr);
	return ret;
}

static struct event_subsystem *
create_new_subsystem(const char *name)
{
	struct event_subsystem *system;

	/* need to create new entry */
	system = kmalloc(sizeof(*system), GFP_KERNEL);
	if (!system)
		return NULL;

	system->ref_count = 1;

	/* Only allocate if dynamic (kprobes and modules) */
	system->name = kstrdup_const(name, GFP_KERNEL);
	if (!system->name)
		goto out_free;

	system->filter = kzalloc(sizeof(struct event_filter), GFP_KERNEL);
	if (!system->filter)
		goto out_free;

	list_add(&system->list, &event_subsystems);

	return system;

 out_free:
	kfree_const(system->name);
	kfree(system);
	return NULL;
}

static int system_callback(const char *name, umode_t *mode, void **data,
		    const struct file_operations **fops)
{
	if (strcmp(name, "filter") == 0)
		*fops = &ftrace_subsystem_filter_fops;

	else if (strcmp(name, "enable") == 0)
		*fops = &ftrace_system_enable_fops;

	else
		return 0;

	*mode = TRACE_MODE_WRITE;
	return 1;
}

static struct eventfs_inode *
event_subsystem_dir(struct trace_array *tr, const char *name,
		    struct trace_event_file *file, struct eventfs_inode *parent)
{
	struct event_subsystem *system, *iter;
	struct trace_subsystem_dir *dir;
	struct eventfs_inode *ei;
	int nr_entries;
	static struct eventfs_entry system_entries[] = {
		{
			.name		= "filter",
			.callback	= system_callback,
		},
		{
			.name		= "enable",
			.callback	= system_callback,
		}
	};

	/* First see if we did not already create this dir */
	list_for_each_entry(dir, &tr->systems, list) {
		system = dir->subsystem;
		if (strcmp(system->name, name) == 0) {
			dir->nr_events++;
			file->system = dir;
			return dir->ei;
		}
	}

	/* Now see if the system itself exists. */
	system = NULL;
	list_for_each_entry(iter, &event_subsystems, list) {
		if (strcmp(iter->name, name) == 0) {
			system = iter;
			break;
		}
	}

	dir = kmalloc(sizeof(*dir), GFP_KERNEL);
	if (!dir)
		goto out_fail;

	if (!system) {
		system = create_new_subsystem(name);
		if (!system)
			goto out_free;
	} else
		__get_system(system);

	/* ftrace only has directories no files */
	if (strcmp(name, "ftrace") == 0)
		nr_entries = 0;
	else
		nr_entries = ARRAY_SIZE(system_entries);

	ei = eventfs_create_dir(name, parent, system_entries, nr_entries, dir);
	if (IS_ERR(ei)) {
		pr_warn("Failed to create system directory %s\n", name);
		__put_system(system);
		goto out_free;
	}

	dir->ei = ei;
	dir->tr = tr;
	dir->ref_count = 1;
	dir->nr_events = 1;
	dir->subsystem = system;
	file->system = dir;

	list_add(&dir->list, &tr->systems);

	return dir->ei;

 out_free:
	kfree(dir);
 out_fail:
	/* Only print this message if failed on memory allocation */
	if (!dir || !system)
		pr_warn("No memory to create event subsystem %s\n", name);
	return NULL;
}

static int
event_define_fields(struct trace_event_call *call)
{
	struct list_head *head;
	int ret = 0;

	/*
	 * Other events may have the same class. Only update
	 * the fields if they are not already defined.
	 */
	head = trace_get_fields(call);
	if (list_empty(head)) {
		struct trace_event_fields *field = call->class->fields_array;
		unsigned int offset = sizeof(struct trace_entry);

		for (; field->type; field++) {
			if (field->type == TRACE_FUNCTION_TYPE) {
				field->define_fields(call);
				break;
			}

			offset = ALIGN(offset, field->align);
			ret = trace_define_field_ext(call, field->type, field->name,
						 offset, field->size,
						 field->is_signed, field->filter_type,
						 field->len, field->needs_test);
			if (WARN_ON_ONCE(ret)) {
				pr_err("error code is %d\n", ret);
				break;
			}

			offset += field->size;
		}
	}

	return ret;
}

static int event_callback(const char *name, umode_t *mode, void **data,
			  const struct file_operations **fops)
{
	struct trace_event_file *file = *data;
	struct trace_event_call *call = file->event_call;

	if (strcmp(name, "format") == 0) {
		*mode = TRACE_MODE_READ;
		*fops = &ftrace_event_format_fops;
		return 1;
	}

	/*
	 * Only event directories that can be enabled should have
	 * triggers or filters, with the exception of the "print"
	 * event that can have a "trigger" file.
	 */
	if (!(call->flags & TRACE_EVENT_FL_IGNORE_ENABLE)) {
		if (call->class->reg && strcmp(name, "enable") == 0) {
			*mode = TRACE_MODE_WRITE;
			*fops = &ftrace_enable_fops;
			return 1;
		}

		if (strcmp(name, "filter") == 0) {
			*mode = TRACE_MODE_WRITE;
			*fops = &ftrace_event_filter_fops;
			return 1;
		}
	}

	if (!(call->flags & TRACE_EVENT_FL_IGNORE_ENABLE) ||
	    strcmp(trace_event_name(call), "print") == 0) {
		if (strcmp(name, "trigger") == 0) {
			*mode = TRACE_MODE_WRITE;
			*fops = &event_trigger_fops;
			return 1;
		}
	}

#ifdef CONFIG_PERF_EVENTS
	if (call->event.type && call->class->reg &&
	    strcmp(name, "id") == 0) {
		*mode = TRACE_MODE_READ;
		*data = (void *)(long)call->event.type;
		*fops = &ftrace_event_id_fops;
		return 1;
	}
#endif

#ifdef CONFIG_HIST_TRIGGERS
	if (strcmp(name, "hist") == 0) {
		*mode = TRACE_MODE_READ;
		*fops = &event_hist_fops;
		return 1;
	}
#endif
#ifdef CONFIG_HIST_TRIGGERS_DEBUG
	if (strcmp(name, "hist_debug") == 0) {
		*mode = TRACE_MODE_READ;
		*fops = &event_hist_debug_fops;
		return 1;
	}
#endif
#ifdef CONFIG_TRACE_EVENT_INJECT
	if (call->event.type && call->class->reg &&
	    strcmp(name, "inject") == 0) {
		*mode = 0200;
		*fops = &event_inject_fops;
		return 1;
	}
#endif
	return 0;
}

/* The file is incremented on creation and freeing the enable file decrements it */
static void event_release(const char *name, void *data)
{
	struct trace_event_file *file = data;

	event_file_put(file);
}

static int
event_create_dir(struct eventfs_inode *parent, struct trace_event_file *file)
{
	struct trace_event_call *call = file->event_call;
	struct trace_array *tr = file->tr;
	struct eventfs_inode *e_events;
	struct eventfs_inode *ei;
	const char *name;
	int nr_entries;
	int ret;
	static struct eventfs_entry event_entries[] = {
		{
			.name		= "enable",
			.callback	= event_callback,
			.release	= event_release,
		},
		{
			.name		= "filter",
			.callback	= event_callback,
		},
		{
			.name		= "trigger",
			.callback	= event_callback,
		},
		{
			.name		= "format",
			.callback	= event_callback,
		},
#ifdef CONFIG_PERF_EVENTS
		{
			.name		= "id",
			.callback	= event_callback,
		},
#endif
#ifdef CONFIG_HIST_TRIGGERS
		{
			.name		= "hist",
			.callback	= event_callback,
		},
#endif
#ifdef CONFIG_HIST_TRIGGERS_DEBUG
		{
			.name		= "hist_debug",
			.callback	= event_callback,
		},
#endif
#ifdef CONFIG_TRACE_EVENT_INJECT
		{
			.name		= "inject",
			.callback	= event_callback,
		},
#endif
	};

	/*
	 * If the trace point header did not define TRACE_SYSTEM
	 * then the system would be called "TRACE_SYSTEM". This should
	 * never happen.
	 */
	if (WARN_ON_ONCE(strcmp(call->class->system, TRACE_SYSTEM) == 0))
		return -ENODEV;

	e_events = event_subsystem_dir(tr, call->class->system, file, parent);
	if (!e_events)
		return -ENOMEM;

	nr_entries = ARRAY_SIZE(event_entries);

	name = trace_event_name(call);
	ei = eventfs_create_dir(name, e_events, event_entries, nr_entries, file);
	if (IS_ERR(ei)) {
		pr_warn("Could not create tracefs '%s' directory\n", name);
		return -1;
	}

	file->ei = ei;

	ret = event_define_fields(call);
	if (ret < 0) {
		pr_warn("Could not initialize trace point events/%s\n", name);
		return ret;
	}

	/* Gets decremented on freeing of the "enable" file */
	event_file_get(file);

	return 0;
}

static void remove_event_from_tracers(struct trace_event_call *call)
{
	struct trace_event_file *file;
	struct trace_array *tr;

	do_for_each_event_file_safe(tr, file) {
		if (file->event_call != call)
			continue;

		remove_event_file_dir(file);
		/*
		 * The do_for_each_event_file_safe() is
		 * a double loop. After finding the call for this
		 * trace_array, we use break to jump to the next
		 * trace_array.
		 */
		break;
	} while_for_each_event_file();
}

static void event_remove(struct trace_event_call *call)
{
	struct trace_array *tr;
	struct trace_event_file *file;

	do_for_each_event_file(tr, file) {
		if (file->event_call != call)
			continue;

		if (file->flags & EVENT_FILE_FL_WAS_ENABLED)
			tr->clear_trace = true;

		ftrace_event_enable_disable(file, 0);
		/*
		 * The do_for_each_event_file() is
		 * a double loop. After finding the call for this
		 * trace_array, we use break to jump to the next
		 * trace_array.
		 */
		break;
	} while_for_each_event_file();

	if (call->event.funcs)
		__unregister_trace_event(&call->event);
	remove_event_from_tracers(call);
	list_del(&call->list);
}

static int event_init(struct trace_event_call *call)
{
	int ret = 0;
	const char *name;

	name = trace_event_name(call);
	if (WARN_ON(!name))
		return -EINVAL;

	if (call->class->raw_init) {
		ret = call->class->raw_init(call);
		if (ret < 0 && ret != -ENOSYS)
			pr_warn("Could not initialize trace events/%s\n", name);
	}

	return ret;
}

static int
__register_event(struct trace_event_call *call, struct module *mod)
{
	int ret;

	ret = event_init(call);
	if (ret < 0)
		return ret;

	down_write(&trace_event_sem);
	list_add(&call->list, &ftrace_events);
	up_write(&trace_event_sem);

	if (call->flags & TRACE_EVENT_FL_DYNAMIC)
		atomic_set(&call->refcnt, 0);
	else
		call->module = mod;

	return 0;
}

static char *eval_replace(char *ptr, struct trace_eval_map *map, int len)
{
	int rlen;
	int elen;

	/* Find the length of the eval value as a string */
	elen = snprintf(ptr, 0, "%ld", map->eval_value);
	/* Make sure there's enough room to replace the string with the value */
	if (len < elen)
		return NULL;

	snprintf(ptr, elen + 1, "%ld", map->eval_value);

	/* Get the rest of the string of ptr */
	rlen = strlen(ptr + len);
	memmove(ptr + elen, ptr + len, rlen);
	/* Make sure we end the new string */
	ptr[elen + rlen] = 0;

	return ptr + elen;
}

static void update_event_printk(struct trace_event_call *call,
				struct trace_eval_map *map)
{
	char *ptr;
	int quote = 0;
	int len = strlen(map->eval_string);

	for (ptr = call->print_fmt; *ptr; ptr++) {
		if (*ptr == '\\') {
			ptr++;
			/* paranoid */
			if (!*ptr)
				break;
			continue;
		}
		if (*ptr == '"') {
			quote ^= 1;
			continue;
		}
		if (quote)
			continue;
		if (isdigit(*ptr)) {
			/* skip numbers */
			do {
				ptr++;
				/* Check for alpha chars like ULL */
			} while (isalnum(*ptr));
			if (!*ptr)
				break;
			/*
			 * A number must have some kind of delimiter after
			 * it, and we can ignore that too.
			 */
			continue;
		}
		if (isalpha(*ptr) || *ptr == '_') {
			if (strncmp(map->eval_string, ptr, len) == 0 &&
			    !isalnum(ptr[len]) && ptr[len] != '_') {
				ptr = eval_replace(ptr, map, len);
				/* enum/sizeof string smaller than value */
				if (WARN_ON_ONCE(!ptr))
					return;
				/*
				 * No need to decrement here, as eval_replace()
				 * returns the pointer to the character passed
				 * the eval, and two evals can not be placed
				 * back to back without something in between.
				 * We can skip that something in between.
				 */
				continue;
			}
		skip_more:
			do {
				ptr++;
			} while (isalnum(*ptr) || *ptr == '_');
			if (!*ptr)
				break;
			/*
			 * If what comes after this variable is a '.' or
			 * '->' then we can continue to ignore that string.
			 */
			if (*ptr == '.' || (ptr[0] == '-' && ptr[1] == '>')) {
				ptr += *ptr == '.' ? 1 : 2;
				if (!*ptr)
					break;
				goto skip_more;
			}
			/*
			 * Once again, we can skip the delimiter that came
			 * after the string.
			 */
			continue;
		}
	}
}

static void add_str_to_module(struct module *module, char *str)
{
	struct module_string *modstr;

	modstr = kmalloc(sizeof(*modstr), GFP_KERNEL);

	/*
	 * If we failed to allocate memory here, then we'll just
	 * let the str memory leak when the module is removed.
	 * If this fails to allocate, there's worse problems than
	 * a leaked string on module removal.
	 */
	if (WARN_ON_ONCE(!modstr))
		return;

	modstr->module = module;
	modstr->str = str;

	list_add(&modstr->next, &module_strings);
}

#define ATTRIBUTE_STR "__attribute__("
#define ATTRIBUTE_STR_LEN (sizeof(ATTRIBUTE_STR) - 1)

/* Remove all __attribute__() from @type. Return allocated string or @type. */
static char *sanitize_field_type(const char *type)
{
	char *attr, *tmp, *next, *ret = (char *)type;
	int depth;

	next = (char *)type;
	while ((attr = strstr(next, ATTRIBUTE_STR))) {
		/* Retry if "__attribute__(" is a part of another word. */
		if (attr != next && !isspace(attr[-1])) {
			next = attr + ATTRIBUTE_STR_LEN;
			continue;
		}

		if (ret == type) {
			ret = kstrdup(type, GFP_KERNEL);
			if (WARN_ON_ONCE(!ret))
				return NULL;
			attr = ret + (attr - type);
		}

		/* the ATTRIBUTE_STR already has the first '(' */
		depth = 1;
		next = attr + ATTRIBUTE_STR_LEN;
		do {
			tmp = strpbrk(next, "()");
			/* There is unbalanced parentheses */
			if (WARN_ON_ONCE(!tmp)) {
				kfree(ret);
				return (char *)type;
			}

			if (*tmp == '(')
				depth++;
			else
				depth--;
			next = tmp + 1;
		} while (depth > 0);
		next = skip_spaces(next);
		strcpy(attr, next);
		next = attr;
	}
	return ret;
}

static char *find_replacable_eval(const char *type, const char *eval_string,
				  int len)
{
	char *ptr;

	if (!eval_string)
		return NULL;

	ptr = strchr(type, '[');
	if (!ptr)
		return NULL;
	ptr++;

	if (!isalpha(*ptr) && *ptr != '_')
		return NULL;

	if (strncmp(eval_string, ptr, len) != 0)
		return NULL;

	return ptr;
}

static void update_event_fields(struct trace_event_call *call,
				struct trace_eval_map *map)
{
	struct ftrace_event_field *field;
	const char *eval_string = NULL;
	struct list_head *head;
	int len = 0;
	char *ptr;
	char *str;

	/* Dynamic events should never have field maps */
	if (call->flags & TRACE_EVENT_FL_DYNAMIC)
		return;

	if (map) {
		eval_string = map->eval_string;
		len = strlen(map->eval_string);
	}

	head = trace_get_fields(call);
	list_for_each_entry(field, head, link) {
		str = sanitize_field_type(field->type);
		if (!str)
			return;

		ptr = find_replacable_eval(str, eval_string, len);
		if (ptr) {
			if (str == field->type) {
				str = kstrdup(field->type, GFP_KERNEL);
				if (WARN_ON_ONCE(!str))
					return;
				ptr = str + (ptr - field->type);
			}

			ptr = eval_replace(ptr, map, len);
			/* enum/sizeof string smaller than value */
			if (WARN_ON_ONCE(!ptr)) {
				kfree(str);
				continue;
			}
		}

		if (str == field->type)
			continue;
		/*
		 * If the event is part of a module, then we need to free the string
		 * when the module is removed. Otherwise, it will stay allocated
		 * until a reboot.
		 */
		if (call->module)
			add_str_to_module(call->module, str);

		field->type = str;
		if (field->filter_type == FILTER_OTHER)
			field->filter_type = filter_assign_type(field->type);
	}
}

/* Update all events for replacing eval and sanitizing */
void trace_event_update_all(struct trace_eval_map **map, int len)
{
	struct trace_event_call *call, *p;
	const char *last_system = NULL;
	bool first = false;
	bool updated;
	int last_i;
	int i;

	down_write(&trace_event_sem);
	list_for_each_entry_safe(call, p, &ftrace_events, list) {
		/* events are usually grouped together with systems */
		if (!last_system || call->class->system != last_system) {
			first = true;
			last_i = 0;
			last_system = call->class->system;
		}

		updated = false;
		/*
		 * Since calls are grouped by systems, the likelihood that the
		 * next call in the iteration belongs to the same system as the
		 * previous call is high. As an optimization, we skip searching
		 * for a map[] that matches the call's system if the last call
		 * was from the same system. That's what last_i is for. If the
		 * call has the same system as the previous call, then last_i
		 * will be the index of the first map[] that has a matching
		 * system.
		 */
		for (i = last_i; i < len; i++) {
			if (call->class->system == map[i]->system) {
				/* Save the first system if need be */
				if (first) {
					last_i = i;
					first = false;
				}
				update_event_printk(call, map[i]);
				update_event_fields(call, map[i]);
				updated = true;
			}
		}
		/* If not updated yet, update field for sanitizing. */
		if (!updated)
			update_event_fields(call, NULL);
		cond_resched();
	}
	up_write(&trace_event_sem);
}

static bool event_in_systems(struct trace_event_call *call,
			     const char *systems)
{
	const char *system;
	const char *p;

	if (!systems)
		return true;

	system = call->class->system;
	p = strstr(systems, system);
	if (!p)
		return false;

	if (p != systems && !isspace(*(p - 1)) && *(p - 1) != ',')
		return false;

	p += strlen(system);
	return !*p || isspace(*p) || *p == ',';
}

#ifdef CONFIG_HIST_TRIGGERS
/*
 * Wake up waiter on the hist_poll_wq from irq_work because the hist trigger
 * may happen in any context.
 */
static void hist_poll_event_irq_work(struct irq_work *work)
{
	wake_up_all(&hist_poll_wq);
}

DEFINE_IRQ_WORK(hist_poll_work, hist_poll_event_irq_work);
DECLARE_WAIT_QUEUE_HEAD(hist_poll_wq);
#endif

static struct trace_event_file *
trace_create_new_event(struct trace_event_call *call,
		       struct trace_array *tr)
{
	struct trace_pid_list *no_pid_list;
	struct trace_pid_list *pid_list;
	struct trace_event_file *file;
	unsigned int first;

	if (!event_in_systems(call, tr->system_names))
		return NULL;

	file = kmem_cache_alloc(file_cachep, GFP_TRACE);
	if (!file)
		return ERR_PTR(-ENOMEM);

	pid_list = rcu_dereference_protected(tr->filtered_pids,
					     lockdep_is_held(&event_mutex));
	no_pid_list = rcu_dereference_protected(tr->filtered_no_pids,
					     lockdep_is_held(&event_mutex));

	if (!trace_pid_list_first(pid_list, &first) ||
	    !trace_pid_list_first(no_pid_list, &first))
		file->flags |= EVENT_FILE_FL_PID_FILTER;

	file->event_call = call;
	file->tr = tr;
	atomic_set(&file->sm_ref, 0);
	atomic_set(&file->tm_ref, 0);
	INIT_LIST_HEAD(&file->triggers);
	list_add(&file->list, &tr->events);
	refcount_set(&file->ref, 1);

	return file;
}

#define MAX_BOOT_TRIGGERS 32

static struct boot_triggers {
	const char		*event;
	char			*trigger;
} bootup_triggers[MAX_BOOT_TRIGGERS];

static char bootup_trigger_buf[COMMAND_LINE_SIZE];
static int nr_boot_triggers;

static __init int setup_trace_triggers(char *str)
{
	char *trigger;
	char *buf;
	int i;

	strscpy(bootup_trigger_buf, str, COMMAND_LINE_SIZE);
	trace_set_ring_buffer_expanded(NULL);
	disable_tracing_selftest("running event triggers");

	buf = bootup_trigger_buf;
	for (i = 0; i < MAX_BOOT_TRIGGERS; i++) {
		trigger = strsep(&buf, ",");
		if (!trigger)
			break;
		bootup_triggers[i].event = strsep(&trigger, ".");
		bootup_triggers[i].trigger = trigger;
		if (!bootup_triggers[i].trigger)
			break;
	}

	nr_boot_triggers = i;
	return 1;
}
__setup("trace_trigger=", setup_trace_triggers);

/* Add an event to a trace directory */
static int
__trace_add_new_event(struct trace_event_call *call, struct trace_array *tr)
{
	struct trace_event_file *file;

	file = trace_create_new_event(call, tr);
	/*
	 * trace_create_new_event() returns ERR_PTR(-ENOMEM) if failed
	 * allocation, or NULL if the event is not part of the tr->system_names.
	 * When the event is not part of the tr->system_names, return zero, not
	 * an error.
	 */
	if (!file)
		return 0;

	if (IS_ERR(file))
		return PTR_ERR(file);

	if (eventdir_initialized)
		return event_create_dir(tr->event_dir, file);
	else
		return event_define_fields(call);
}

static void trace_early_triggers(struct trace_event_file *file, const char *name)
{
	int ret;
	int i;

	for (i = 0; i < nr_boot_triggers; i++) {
		if (strcmp(name, bootup_triggers[i].event))
			continue;
		mutex_lock(&event_mutex);
		ret = trigger_process_regex(file, bootup_triggers[i].trigger);
		mutex_unlock(&event_mutex);
		if (ret)
			pr_err("Failed to register trigger '%s' on event %s\n",
			       bootup_triggers[i].trigger,
			       bootup_triggers[i].event);
	}
}

/*
 * Just create a descriptor for early init. A descriptor is required
 * for enabling events at boot. We want to enable events before
 * the filesystem is initialized.
 */
static int
__trace_early_add_new_event(struct trace_event_call *call,
			    struct trace_array *tr)
{
	struct trace_event_file *file;
	int ret;

	file = trace_create_new_event(call, tr);
	/*
	 * trace_create_new_event() returns ERR_PTR(-ENOMEM) if failed
	 * allocation, or NULL if the event is not part of the tr->system_names.
	 * When the event is not part of the tr->system_names, return zero, not
	 * an error.
	 */
	if (!file)
		return 0;

	if (IS_ERR(file))
		return PTR_ERR(file);

	ret = event_define_fields(call);
	if (ret)
		return ret;

	trace_early_triggers(file, trace_event_name(call));

	return 0;
}

struct ftrace_module_file_ops;
static void __add_event_to_tracers(struct trace_event_call *call);

/* Add an additional event_call dynamically */
int trace_add_event_call(struct trace_event_call *call)
{
	int ret;
	lockdep_assert_held(&event_mutex);

	guard(mutex)(&trace_types_lock);

	ret = __register_event(call, NULL);
	if (ret < 0)
		return ret;

	__add_event_to_tracers(call);
	return ret;
}
EXPORT_SYMBOL_GPL(trace_add_event_call);

/*
 * Must be called under locking of trace_types_lock, event_mutex and
 * trace_event_sem.
 */
static void __trace_remove_event_call(struct trace_event_call *call)
{
	event_remove(call);
	trace_destroy_fields(call);
}

static int probe_remove_event_call(struct trace_event_call *call)
{
	struct trace_array *tr;
	struct trace_event_file *file;

#ifdef CONFIG_PERF_EVENTS
	if (call->perf_refcount)
		return -EBUSY;
#endif
	do_for_each_event_file(tr, file) {
		if (file->event_call != call)
			continue;
		/*
		 * We can't rely on ftrace_event_enable_disable(enable => 0)
		 * we are going to do, soft mode can suppress
		 * TRACE_REG_UNREGISTER.
		 */
		if (file->flags & EVENT_FILE_FL_ENABLED)
			goto busy;

		if (file->flags & EVENT_FILE_FL_WAS_ENABLED)
			tr->clear_trace = true;
		/*
		 * The do_for_each_event_file_safe() is
		 * a double loop. After finding the call for this
		 * trace_array, we use break to jump to the next
		 * trace_array.
		 */
		break;
	} while_for_each_event_file();

	__trace_remove_event_call(call);

	return 0;
 busy:
	/* No need to clear the trace now */
	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		tr->clear_trace = false;
	}
	return -EBUSY;
}

/* Remove an event_call */
int trace_remove_event_call(struct trace_event_call *call)
{
	int ret;

	lockdep_assert_held(&event_mutex);

	mutex_lock(&trace_types_lock);
	down_write(&trace_event_sem);
	ret = probe_remove_event_call(call);
	up_write(&trace_event_sem);
	mutex_unlock(&trace_types_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(trace_remove_event_call);

#define for_each_event(event, start, end)			\
	for (event = start;					\
	     (unsigned long)event < (unsigned long)end;		\
	     event++)

#ifdef CONFIG_MODULES
static void update_mod_cache(struct trace_array *tr, struct module *mod)
{
	struct event_mod_load *event_mod, *n;

	list_for_each_entry_safe(event_mod, n, &tr->mod_events, list) {
		if (strcmp(event_mod->module, mod->name) != 0)
			continue;

		__ftrace_set_clr_event_nolock(tr, event_mod->match,
					      event_mod->system,
					      event_mod->event, 1, mod->name);
		free_event_mod(event_mod);
	}
}

static void update_cache_events(struct module *mod)
{
	struct trace_array *tr;

	list_for_each_entry(tr, &ftrace_trace_arrays, list)
		update_mod_cache(tr, mod);
}

static void trace_module_add_events(struct module *mod)
{
	struct trace_event_call **call, **start, **end;

	if (!mod->num_trace_events)
		return;

	/* Don't add infrastructure for mods without tracepoints */
	if (trace_module_has_bad_taint(mod)) {
		pr_err("%s: module has bad taint, not creating trace events\n",
		       mod->name);
		return;
	}

	start = mod->trace_events;
	end = mod->trace_events + mod->num_trace_events;

	for_each_event(call, start, end) {
		__register_event(*call, mod);
		__add_event_to_tracers(*call);
	}

	update_cache_events(mod);
}

static void trace_module_remove_events(struct module *mod)
{
	struct trace_event_call *call, *p;
	struct module_string *modstr, *m;

	down_write(&trace_event_sem);
	list_for_each_entry_safe(call, p, &ftrace_events, list) {
		if ((call->flags & TRACE_EVENT_FL_DYNAMIC) || !call->module)
			continue;
		if (call->module == mod)
			__trace_remove_event_call(call);
	}
	/* Check for any strings allocated for this module */
	list_for_each_entry_safe(modstr, m, &module_strings, next) {
		if (modstr->module != mod)
			continue;
		list_del(&modstr->next);
		kfree(modstr->str);
		kfree(modstr);
	}
	up_write(&trace_event_sem);

	/*
	 * It is safest to reset the ring buffer if the module being unloaded
	 * registered any events that were used. The only worry is if
	 * a new module gets loaded, and takes on the same id as the events
	 * of this module. When printing out the buffer, traced events left
	 * over from this module may be passed to the new module events and
	 * unexpected results may occur.
	 */
	tracing_reset_all_online_cpus_unlocked();
}

static int trace_module_notify(struct notifier_block *self,
			       unsigned long val, void *data)
{
	struct module *mod = data;

	mutex_lock(&event_mutex);
	mutex_lock(&trace_types_lock);
	switch (val) {
	case MODULE_STATE_COMING:
		trace_module_add_events(mod);
		break;
	case MODULE_STATE_GOING:
		trace_module_remove_events(mod);
		break;
	}
	mutex_unlock(&trace_types_lock);
	mutex_unlock(&event_mutex);

	return NOTIFY_OK;
}

static struct notifier_block trace_module_nb = {
	.notifier_call = trace_module_notify,
	.priority = 1, /* higher than trace.c module notify */
};
#endif /* CONFIG_MODULES */

/* Create a new event directory structure for a trace directory. */
static void
__trace_add_event_dirs(struct trace_array *tr)
{
	struct trace_event_call *call;
	int ret;

	lockdep_assert_held(&trace_event_sem);

	list_for_each_entry(call, &ftrace_events, list) {
		ret = __trace_add_new_event(call, tr);
		if (ret < 0)
			pr_warn("Could not create directory for event %s\n",
				trace_event_name(call));
	}
}

/* Returns any file that matches the system and event */
struct trace_event_file *
__find_event_file(struct trace_array *tr, const char *system, const char *event)
{
	struct trace_event_file *file;
	struct trace_event_call *call;
	const char *name;

	list_for_each_entry(file, &tr->events, list) {

		call = file->event_call;
		name = trace_event_name(call);

		if (!name || !call->class)
			continue;

		if (strcmp(event, name) == 0 &&
		    strcmp(system, call->class->system) == 0)
			return file;
	}
	return NULL;
}

/* Returns valid trace event files that match system and event */
struct trace_event_file *
find_event_file(struct trace_array *tr, const char *system, const char *event)
{
	struct trace_event_file *file;

	file = __find_event_file(tr, system, event);
	if (!file || !file->event_call->class->reg ||
	    file->event_call->flags & TRACE_EVENT_FL_IGNORE_ENABLE)
		return NULL;

	return file;
}

/**
 * trace_get_event_file - Find and return a trace event file
 * @instance: The name of the trace instance containing the event
 * @system: The name of the system containing the event
 * @event: The name of the event
 *
 * Return a trace event file given the trace instance name, trace
 * system, and trace event name.  If the instance name is NULL, it
 * refers to the top-level trace array.
 *
 * This function will look it up and return it if found, after calling
 * trace_array_get() to prevent the instance from going away, and
 * increment the event's module refcount to prevent it from being
 * removed.
 *
 * To release the file, call trace_put_event_file(), which will call
 * trace_array_put() and decrement the event's module refcount.
 *
 * Return: The trace event on success, ERR_PTR otherwise.
 */
struct trace_event_file *trace_get_event_file(const char *instance,
					      const char *system,
					      const char *event)
{
	struct trace_array *tr = top_trace_array();
	struct trace_event_file *file = NULL;
	int ret = -EINVAL;

	if (instance) {
		tr = trace_array_find_get(instance);
		if (!tr)
			return ERR_PTR(-ENOENT);
	} else {
		ret = trace_array_get(tr);
		if (ret)
			return ERR_PTR(ret);
	}

	guard(mutex)(&event_mutex);

	file = find_event_file(tr, system, event);
	if (!file) {
		trace_array_put(tr);
		return ERR_PTR(-EINVAL);
	}

	/* Don't let event modules unload while in use */
	ret = trace_event_try_get_ref(file->event_call);
	if (!ret) {
		trace_array_put(tr);
		return ERR_PTR(-EBUSY);
	}

	return file;
}
EXPORT_SYMBOL_GPL(trace_get_event_file);

/**
 * trace_put_event_file - Release a file from trace_get_event_file()
 * @file: The trace event file
 *
 * If a file was retrieved using trace_get_event_file(), this should
 * be called when it's no longer needed.  It will cancel the previous
 * trace_array_get() called by that function, and decrement the
 * event's module refcount.
 */
void trace_put_event_file(struct trace_event_file *file)
{
	mutex_lock(&event_mutex);
	trace_event_put_ref(file->event_call);
	mutex_unlock(&event_mutex);

	trace_array_put(file->tr);
}
EXPORT_SYMBOL_GPL(trace_put_event_file);

#ifdef CONFIG_DYNAMIC_FTRACE

/* Avoid typos */
#define ENABLE_EVENT_STR	"enable_event"
#define DISABLE_EVENT_STR	"disable_event"

struct event_probe_data {
	struct trace_event_file	*file;
	unsigned long			count;
	int				ref;
	bool				enable;
};

static void update_event_probe(struct event_probe_data *data)
{
	if (data->enable)
		clear_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &data->file->flags);
	else
		set_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &data->file->flags);
}

static void
event_enable_probe(unsigned long ip, unsigned long parent_ip,
		   struct trace_array *tr, struct ftrace_probe_ops *ops,
		   void *data)
{
	struct ftrace_func_mapper *mapper = data;
	struct event_probe_data *edata;
	void **pdata;

	pdata = ftrace_func_mapper_find_ip(mapper, ip);
	if (!pdata || !*pdata)
		return;

	edata = *pdata;
	update_event_probe(edata);
}

static void
event_enable_count_probe(unsigned long ip, unsigned long parent_ip,
			 struct trace_array *tr, struct ftrace_probe_ops *ops,
			 void *data)
{
	struct ftrace_func_mapper *mapper = data;
	struct event_probe_data *edata;
	void **pdata;

	pdata = ftrace_func_mapper_find_ip(mapper, ip);
	if (!pdata || !*pdata)
		return;

	edata = *pdata;

	if (!edata->count)
		return;

	/* Skip if the event is in a state we want to switch to */
	if (edata->enable == !(edata->file->flags & EVENT_FILE_FL_SOFT_DISABLED))
		return;

	if (edata->count != -1)
		(edata->count)--;

	update_event_probe(edata);
}

static int
event_enable_print(struct seq_file *m, unsigned long ip,
		   struct ftrace_probe_ops *ops, void *data)
{
	struct ftrace_func_mapper *mapper = data;
	struct event_probe_data *edata;
	void **pdata;

	pdata = ftrace_func_mapper_find_ip(mapper, ip);

	if (WARN_ON_ONCE(!pdata || !*pdata))
		return 0;

	edata = *pdata;

	seq_printf(m, "%ps:", (void *)ip);

	seq_printf(m, "%s:%s:%s",
		   edata->enable ? ENABLE_EVENT_STR : DISABLE_EVENT_STR,
		   edata->file->event_call->class->system,
		   trace_event_name(edata->file->event_call));

	if (edata->count == -1)
		seq_puts(m, ":unlimited\n");
	else
		seq_printf(m, ":count=%ld\n", edata->count);

	return 0;
}

static int
event_enable_init(struct ftrace_probe_ops *ops, struct trace_array *tr,
		  unsigned long ip, void *init_data, void **data)
{
	struct ftrace_func_mapper *mapper = *data;
	struct event_probe_data *edata = init_data;
	int ret;

	if (!mapper) {
		mapper = allocate_ftrace_func_mapper();
		if (!mapper)
			return -ENODEV;
		*data = mapper;
	}

	ret = ftrace_func_mapper_add_ip(mapper, ip, edata);
	if (ret < 0)
		return ret;

	edata->ref++;

	return 0;
}

static int free_probe_data(void *data)
{
	struct event_probe_data *edata = data;

	edata->ref--;
	if (!edata->ref) {
		/* Remove soft mode */
		__ftrace_event_enable_disable(edata->file, 0, 1);
		trace_event_put_ref(edata->file->event_call);
		kfree(edata);
	}
	return 0;
}

static void
event_enable_free(struct ftrace_probe_ops *ops, struct trace_array *tr,
		  unsigned long ip, void *data)
{
	struct ftrace_func_mapper *mapper = data;
	struct event_probe_data *edata;

	if (!ip) {
		if (!mapper)
			return;
		free_ftrace_func_mapper(mapper, free_probe_data);
		return;
	}

	edata = ftrace_func_mapper_remove_ip(mapper, ip);

	if (WARN_ON_ONCE(!edata))
		return;

	if (WARN_ON_ONCE(edata->ref <= 0))
		return;

	free_probe_data(edata);
}

static struct ftrace_probe_ops event_enable_probe_ops = {
	.func			= event_enable_probe,
	.print			= event_enable_print,
	.init			= event_enable_init,
	.free			= event_enable_free,
};

static struct ftrace_probe_ops event_enable_count_probe_ops = {
	.func			= event_enable_count_probe,
	.print			= event_enable_print,
	.init			= event_enable_init,
	.free			= event_enable_free,
};

static struct ftrace_probe_ops event_disable_probe_ops = {
	.func			= event_enable_probe,
	.print			= event_enable_print,
	.init			= event_enable_init,
	.free			= event_enable_free,
};

static struct ftrace_probe_ops event_disable_count_probe_ops = {
	.func			= event_enable_count_probe,
	.print			= event_enable_print,
	.init			= event_enable_init,
	.free			= event_enable_free,
};

static int
event_enable_func(struct trace_array *tr, struct ftrace_hash *hash,
		  char *glob, char *cmd, char *param, int enabled)
{
	struct trace_event_file *file;
	struct ftrace_probe_ops *ops;
	struct event_probe_data *data;
	unsigned long count = -1;
	const char *system;
	const char *event;
	char *number;
	bool enable;
	int ret;

	if (!tr)
		return -ENODEV;

	/* hash funcs only work with set_ftrace_filter */
	if (!enabled || !param)
		return -EINVAL;

	system = strsep(&param, ":");
	if (!param)
		return -EINVAL;

	event = strsep(&param, ":");

	guard(mutex)(&event_mutex);

	file = find_event_file(tr, system, event);
	if (!file)
		return -EINVAL;

	enable = strcmp(cmd, ENABLE_EVENT_STR) == 0;

	if (enable)
		ops = param ? &event_enable_count_probe_ops : &event_enable_probe_ops;
	else
		ops = param ? &event_disable_count_probe_ops : &event_disable_probe_ops;

	if (glob[0] == '!')
		return unregister_ftrace_function_probe_func(glob+1, tr, ops);

	if (param) {
		number = strsep(&param, ":");

		if (!strlen(number))
			return -EINVAL;

		/*
		 * We use the callback data field (which is a pointer)
		 * as our counter.
		 */
		ret = kstrtoul(number, 0, &count);
		if (ret)
			return ret;
	}

	/* Don't let event modules unload while probe registered */
	ret = trace_event_try_get_ref(file->event_call);
	if (!ret)
		return -EBUSY;

	ret = __ftrace_event_enable_disable(file, 1, 1);
	if (ret < 0)
		goto out_put;

	ret = -ENOMEM;
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		goto out_put;

	data->enable = enable;
	data->count = count;
	data->file = file;

	ret = register_ftrace_function_probe(glob, tr, ops, data);
	/*
	 * The above returns on success the # of functions enabled,
	 * but if it didn't find any functions it returns zero.
	 * Consider no functions a failure too.
	 */

	/* Just return zero, not the number of enabled functions */
	if (ret > 0)
		return 0;

	kfree(data);

	if (!ret)
		ret = -ENOENT;

	__ftrace_event_enable_disable(file, 0, 1);
 out_put:
	trace_event_put_ref(file->event_call);
	return ret;
}

static struct ftrace_func_command event_enable_cmd = {
	.name			= ENABLE_EVENT_STR,
	.func			= event_enable_func,
};

static struct ftrace_func_command event_disable_cmd = {
	.name			= DISABLE_EVENT_STR,
	.func			= event_enable_func,
};

static __init int register_event_cmds(void)
{
	int ret;

	ret = register_ftrace_command(&event_enable_cmd);
	if (WARN_ON(ret < 0))
		return ret;
	ret = register_ftrace_command(&event_disable_cmd);
	if (WARN_ON(ret < 0))
		unregister_ftrace_command(&event_enable_cmd);
	return ret;
}
#else
static inline int register_event_cmds(void) { return 0; }
#endif /* CONFIG_DYNAMIC_FTRACE */

/*
 * The top level array and trace arrays created by boot-time tracing
 * have already had its trace_event_file descriptors created in order
 * to allow for early events to be recorded.
 * This function is called after the tracefs has been initialized,
 * and we now have to create the files associated to the events.
 */
static void __trace_early_add_event_dirs(struct trace_array *tr)
{
	struct trace_event_file *file;
	int ret;


	list_for_each_entry(file, &tr->events, list) {
		ret = event_create_dir(tr->event_dir, file);
		if (ret < 0)
			pr_warn("Could not create directory for event %s\n",
				trace_event_name(file->event_call));
	}
}

/*
 * For early boot up, the top trace array and the trace arrays created
 * by boot-time tracing require to have a list of events that can be
 * enabled. This must be done before the filesystem is set up in order
 * to allow events to be traced early.
 */
void __trace_early_add_events(struct trace_array *tr)
{
	struct trace_event_call *call;
	int ret;

	list_for_each_entry(call, &ftrace_events, list) {
		/* Early boot up should not have any modules loaded */
		if (!(call->flags & TRACE_EVENT_FL_DYNAMIC) &&
		    WARN_ON_ONCE(call->module))
			continue;

		ret = __trace_early_add_new_event(call, tr);
		if (ret < 0)
			pr_warn("Could not create early event %s\n",
				trace_event_name(call));
	}
}

/* Remove the event directory structure for a trace directory. */
static void
__trace_remove_event_dirs(struct trace_array *tr)
{
	struct trace_event_file *file, *next;

	list_for_each_entry_safe(file, next, &tr->events, list)
		remove_event_file_dir(file);
}

static void __add_event_to_tracers(struct trace_event_call *call)
{
	struct trace_array *tr;

	list_for_each_entry(tr, &ftrace_trace_arrays, list)
		__trace_add_new_event(call, tr);
}

extern struct trace_event_call *__start_ftrace_events[];
extern struct trace_event_call *__stop_ftrace_events[];

static char bootup_event_buf[COMMAND_LINE_SIZE] __initdata;

static __init int setup_trace_event(char *str)
{
	strscpy(bootup_event_buf, str, COMMAND_LINE_SIZE);
	trace_set_ring_buffer_expanded(NULL);
	disable_tracing_selftest("running event tracing");

	return 1;
}
__setup("trace_event=", setup_trace_event);

static int events_callback(const char *name, umode_t *mode, void **data,
			   const struct file_operations **fops)
{
	if (strcmp(name, "enable") == 0) {
		*mode = TRACE_MODE_WRITE;
		*fops = &ftrace_tr_enable_fops;
		return 1;
	}

	if (strcmp(name, "header_page") == 0) {
		*mode = TRACE_MODE_READ;
		*fops = &ftrace_show_header_page_fops;

	} else if (strcmp(name, "header_event") == 0) {
		*mode = TRACE_MODE_READ;
		*fops = &ftrace_show_header_event_fops;
	} else
		return 0;

	return 1;
}

/* Expects to have event_mutex held when called */
static int
create_event_toplevel_files(struct dentry *parent, struct trace_array *tr)
{
	struct eventfs_inode *e_events;
	struct dentry *entry;
	int nr_entries;
	static struct eventfs_entry events_entries[] = {
		{
			.name		= "enable",
			.callback	= events_callback,
		},
		{
			.name		= "header_page",
			.callback	= events_callback,
		},
		{
			.name		= "header_event",
			.callback	= events_callback,
		},
	};

	entry = trace_create_file("set_event", TRACE_MODE_WRITE, parent,
				  tr, &ftrace_set_event_fops);
	if (!entry)
		return -ENOMEM;

	nr_entries = ARRAY_SIZE(events_entries);

	e_events = eventfs_create_events_dir("events", parent, events_entries,
					     nr_entries, tr);
	if (IS_ERR(e_events)) {
		pr_warn("Could not create tracefs 'events' directory\n");
		return -ENOMEM;
	}

	/* There are not as crucial, just warn if they are not created */

	trace_create_file("set_event_pid", TRACE_MODE_WRITE, parent,
			  tr, &ftrace_set_event_pid_fops);

	trace_create_file("set_event_notrace_pid",
			  TRACE_MODE_WRITE, parent, tr,
			  &ftrace_set_event_notrace_pid_fops);

	tr->event_dir = e_events;

	return 0;
}

/**
 * event_trace_add_tracer - add a instance of a trace_array to events
 * @parent: The parent dentry to place the files/directories for events in
 * @tr: The trace array associated with these events
 *
 * When a new instance is created, it needs to set up its events
 * directory, as well as other files associated with events. It also
 * creates the event hierarchy in the @parent/events directory.
 *
 * Returns 0 on success.
 *
 * Must be called with event_mutex held.
 */
int event_trace_add_tracer(struct dentry *parent, struct trace_array *tr)
{
	int ret;

	lockdep_assert_held(&event_mutex);

	ret = create_event_toplevel_files(parent, tr);
	if (ret)
		goto out;

	down_write(&trace_event_sem);
	/* If tr already has the event list, it is initialized in early boot. */
	if (unlikely(!list_empty(&tr->events)))
		__trace_early_add_event_dirs(tr);
	else
		__trace_add_event_dirs(tr);
	up_write(&trace_event_sem);

 out:
	return ret;
}

/*
 * The top trace array already had its file descriptors created.
 * Now the files themselves need to be created.
 */
static __init int
early_event_add_tracer(struct dentry *parent, struct trace_array *tr)
{
	int ret;

	guard(mutex)(&event_mutex);

	ret = create_event_toplevel_files(parent, tr);
	if (ret)
		return ret;

	down_write(&trace_event_sem);
	__trace_early_add_event_dirs(tr);
	up_write(&trace_event_sem);

	return 0;
}

/* Must be called with event_mutex held */
int event_trace_del_tracer(struct trace_array *tr)
{
	lockdep_assert_held(&event_mutex);

	/* Disable any event triggers and associated soft-disabled events */
	clear_event_triggers(tr);

	/* Clear the pid list */
	__ftrace_clear_event_pids(tr, TRACE_PIDS | TRACE_NO_PIDS);

	/* Disable any running events */
	__ftrace_set_clr_event_nolock(tr, NULL, NULL, NULL, 0, NULL);

	/* Make sure no more events are being executed */
	tracepoint_synchronize_unregister();

	down_write(&trace_event_sem);
	__trace_remove_event_dirs(tr);
	eventfs_remove_events_dir(tr->event_dir);
	up_write(&trace_event_sem);

	tr->event_dir = NULL;

	return 0;
}

static __init int event_trace_memsetup(void)
{
	field_cachep = KMEM_CACHE(ftrace_event_field, SLAB_PANIC);
	file_cachep = KMEM_CACHE(trace_event_file, SLAB_PANIC);
	return 0;
}

__init void
early_enable_events(struct trace_array *tr, char *buf, bool disable_first)
{
	char *token;
	int ret;

	while (true) {
		token = strsep(&buf, ",");

		if (!token)
			break;

		if (*token) {
			/* Restarting syscalls requires that we stop them first */
			if (disable_first)
				ftrace_set_clr_event(tr, token, 0);

			ret = ftrace_set_clr_event(tr, token, 1);
			if (ret)
				pr_warn("Failed to enable trace event: %s\n", token);
		}

		/* Put back the comma to allow this to be called again */
		if (buf)
			*(buf - 1) = ',';
	}
}

static __init int event_trace_enable(void)
{
	struct trace_array *tr = top_trace_array();
	struct trace_event_call **iter, *call;
	int ret;

	if (!tr)
		return -ENODEV;

	for_each_event(iter, __start_ftrace_events, __stop_ftrace_events) {

		call = *iter;
		ret = event_init(call);
		if (!ret)
			list_add(&call->list, &ftrace_events);
	}

	register_trigger_cmds();

	/*
	 * We need the top trace array to have a working set of trace
	 * points at early init, before the debug files and directories
	 * are created. Create the file entries now, and attach them
	 * to the actual file dentries later.
	 */
	__trace_early_add_events(tr);

	early_enable_events(tr, bootup_event_buf, false);

	trace_printk_start_comm();

	register_event_cmds();


	return 0;
}

/*
 * event_trace_enable() is called from trace_event_init() first to
 * initialize events and perhaps start any events that are on the
 * command line. Unfortunately, there are some events that will not
 * start this early, like the system call tracepoints that need
 * to set the %SYSCALL_WORK_SYSCALL_TRACEPOINT flag of pid 1. But
 * event_trace_enable() is called before pid 1 starts, and this flag
 * is never set, making the syscall tracepoint never get reached, but
 * the event is enabled regardless (and not doing anything).
 */
static __init int event_trace_enable_again(void)
{
	struct trace_array *tr;

	tr = top_trace_array();
	if (!tr)
		return -ENODEV;

	early_enable_events(tr, bootup_event_buf, true);

	return 0;
}

early_initcall(event_trace_enable_again);

/* Init fields which doesn't related to the tracefs */
static __init int event_trace_init_fields(void)
{
	if (trace_define_generic_fields())
		pr_warn("tracing: Failed to allocated generic fields");

	if (trace_define_common_fields())
		pr_warn("tracing: Failed to allocate common fields");

	return 0;
}

__init int event_trace_init(void)
{
	struct trace_array *tr;
	int ret;

	tr = top_trace_array();
	if (!tr)
		return -ENODEV;

	trace_create_file("available_events", TRACE_MODE_READ,
			  NULL, tr, &ftrace_avail_fops);

	ret = early_event_add_tracer(NULL, tr);
	if (ret)
		return ret;

#ifdef CONFIG_MODULES
	ret = register_module_notifier(&trace_module_nb);
	if (ret)
		pr_warn("Failed to register trace events module notifier\n");
#endif

	eventdir_initialized = true;

	return 0;
}

void __init trace_event_init(void)
{
	event_trace_memsetup();
	init_ftrace_syscalls();
	event_trace_enable();
	event_trace_init_fields();
}

#ifdef CONFIG_EVENT_TRACE_STARTUP_TEST

static DEFINE_SPINLOCK(test_spinlock);
static DEFINE_SPINLOCK(test_spinlock_irq);
static DEFINE_MUTEX(test_mutex);

static __init void test_work(struct work_struct *dummy)
{
	spin_lock(&test_spinlock);
	spin_lock_irq(&test_spinlock_irq);
	udelay(1);
	spin_unlock_irq(&test_spinlock_irq);
	spin_unlock(&test_spinlock);

	mutex_lock(&test_mutex);
	msleep(1);
	mutex_unlock(&test_mutex);
}

static __init int event_test_thread(void *unused)
{
	void *test_malloc;

	test_malloc = kmalloc(1234, GFP_KERNEL);
	if (!test_malloc)
		pr_info("failed to kmalloc\n");

	schedule_on_each_cpu(test_work);

	kfree(test_malloc);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);

	return 0;
}

/*
 * Do various things that may trigger events.
 */
static __init void event_test_stuff(void)
{
	struct task_struct *test_thread;

	test_thread = kthread_run(event_test_thread, NULL, "test-events");
	msleep(1);
	kthread_stop(test_thread);
}

/*
 * For every trace event defined, we will test each trace point separately,
 * and then by groups, and finally all trace points.
 */
static __init void event_trace_self_tests(void)
{
	struct trace_subsystem_dir *dir;
	struct trace_event_file *file;
	struct trace_event_call *call;
	struct event_subsystem *system;
	struct trace_array *tr;
	int ret;

	tr = top_trace_array();
	if (!tr)
		return;

	pr_info("Running tests on trace events:\n");

	list_for_each_entry(file, &tr->events, list) {

		call = file->event_call;

		/* Only test those that have a probe */
		if (!call->class || !call->class->probe)
			continue;

/*
 * Testing syscall events here is pretty useless, but
 * we still do it if configured. But this is time consuming.
 * What we really need is a user thread to perform the
 * syscalls as we test.
 */
#ifndef CONFIG_EVENT_TRACE_TEST_SYSCALLS
		if (call->class->system &&
		    strcmp(call->class->system, "syscalls") == 0)
			continue;
#endif

		pr_info("Testing event %s: ", trace_event_name(call));

		/*
		 * If an event is already enabled, someone is using
		 * it and the self test should not be on.
		 */
		if (file->flags & EVENT_FILE_FL_ENABLED) {
			pr_warn("Enabled event during self test!\n");
			WARN_ON_ONCE(1);
			continue;
		}

		ftrace_event_enable_disable(file, 1);
		event_test_stuff();
		ftrace_event_enable_disable(file, 0);

		pr_cont("OK\n");
	}

	/* Now test at the sub system level */

	pr_info("Running tests on trace event systems:\n");

	list_for_each_entry(dir, &tr->systems, list) {

		system = dir->subsystem;

		/* the ftrace system is special, skip it */
		if (strcmp(system->name, "ftrace") == 0)
			continue;

		pr_info("Testing event system %s: ", system->name);

		ret = __ftrace_set_clr_event(tr, NULL, system->name, NULL, 1, NULL);
		if (WARN_ON_ONCE(ret)) {
			pr_warn("error enabling system %s\n",
				system->name);
			continue;
		}

		event_test_stuff();

		ret = __ftrace_set_clr_event(tr, NULL, system->name, NULL, 0, NULL);
		if (WARN_ON_ONCE(ret)) {
			pr_warn("error disabling system %s\n",
				system->name);
			continue;
		}

		pr_cont("OK\n");
	}

	/* Test with all events enabled */

	pr_info("Running tests on all trace events:\n");
	pr_info("Testing all events: ");

	ret = __ftrace_set_clr_event(tr, NULL, NULL, NULL, 1, NULL);
	if (WARN_ON_ONCE(ret)) {
		pr_warn("error enabling all events\n");
		return;
	}

	event_test_stuff();

	/* reset sysname */
	ret = __ftrace_set_clr_event(tr, NULL, NULL, NULL, 0, NULL);
	if (WARN_ON_ONCE(ret)) {
		pr_warn("error disabling all events\n");
		return;
	}

	pr_cont("OK\n");
}

#ifdef CONFIG_FUNCTION_TRACER

static DEFINE_PER_CPU(atomic_t, ftrace_test_event_disable);

static struct trace_event_file event_trace_file __initdata;

static void __init
function_test_events_call(unsigned long ip, unsigned long parent_ip,
			  struct ftrace_ops *op, struct ftrace_regs *regs)
{
	struct trace_buffer *buffer;
	struct ring_buffer_event *event;
	struct ftrace_entry *entry;
	unsigned int trace_ctx;
	long disabled;
	int cpu;

	trace_ctx = tracing_gen_ctx();
	preempt_disable_notrace();
	cpu = raw_smp_processor_id();
	disabled = atomic_inc_return(&per_cpu(ftrace_test_event_disable, cpu));

	if (disabled != 1)
		goto out;

	event = trace_event_buffer_lock_reserve(&buffer, &event_trace_file,
						TRACE_FN, sizeof(*entry),
						trace_ctx);
	if (!event)
		goto out;
	entry	= ring_buffer_event_data(event);
	entry->ip			= ip;
	entry->parent_ip		= parent_ip;

	event_trigger_unlock_commit(&event_trace_file, buffer, event,
				    entry, trace_ctx);
 out:
	atomic_dec(&per_cpu(ftrace_test_event_disable, cpu));
	preempt_enable_notrace();
}

static struct ftrace_ops trace_ops __initdata  =
{
	.func = function_test_events_call,
};

static __init void event_trace_self_test_with_function(void)
{
	int ret;

	event_trace_file.tr = top_trace_array();
	if (WARN_ON(!event_trace_file.tr))
		return;

	ret = register_ftrace_function(&trace_ops);
	if (WARN_ON(ret < 0)) {
		pr_info("Failed to enable function tracer for event tests\n");
		return;
	}
	pr_info("Running tests again, along with the function tracer\n");
	event_trace_self_tests();
	unregister_ftrace_function(&trace_ops);
}
#else
static __init void event_trace_self_test_with_function(void)
{
}
#endif

static __init int event_trace_self_tests_init(void)
{
	if (!tracing_selftest_disabled) {
		event_trace_self_tests();
		event_trace_self_test_with_function();
	}

	return 0;
}

late_initcall(event_trace_self_tests_init);

#endif
