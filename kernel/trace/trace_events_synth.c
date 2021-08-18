// SPDX-License-Identifier: GPL-2.0
/*
 * trace_events_synth - synthetic trace events
 *
 * Copyright (C) 2015, 2020 Tom Zanussi <tom.zanussi@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/security.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/rculist.h>
#include <linux/tracefs.h>

/* for gfp flag names */
#include <linux/trace_events.h>
#include <trace/events/mmflags.h>

#include "trace_synth.h"

#undef ERRORS
#define ERRORS	\
	C(BAD_NAME,		"Illegal name"),		\
	C(INVALID_CMD,		"Command must be of the form: <name> field[;field] ..."),\
	C(INVALID_DYN_CMD,	"Command must be of the form: s or -:[synthetic/]<name> field[;field] ..."),\
	C(EVENT_EXISTS,		"Event already exists"),	\
	C(TOO_MANY_FIELDS,	"Too many fields"),		\
	C(INCOMPLETE_TYPE,	"Incomplete type"),		\
	C(INVALID_TYPE,		"Invalid type"),		\
	C(INVALID_FIELD,        "Invalid field"),		\
	C(INVALID_ARRAY_SPEC,	"Invalid array specification"),

#undef C
#define C(a, b)		SYNTH_ERR_##a

enum { ERRORS };

#undef C
#define C(a, b)		b

static const char *err_text[] = { ERRORS };

static char last_cmd[MAX_FILTER_STR_VAL];

static int errpos(const char *str)
{
	return err_pos(last_cmd, str);
}

static void last_cmd_set(const char *str)
{
	if (!str)
		return;

	strncpy(last_cmd, str, MAX_FILTER_STR_VAL - 1);
}

static void synth_err(u8 err_type, u8 err_pos)
{
	tracing_log_err(NULL, "synthetic_events", last_cmd, err_text,
			err_type, err_pos);
}

static int create_synth_event(const char *raw_command);
static int synth_event_show(struct seq_file *m, struct dyn_event *ev);
static int synth_event_release(struct dyn_event *ev);
static bool synth_event_is_busy(struct dyn_event *ev);
static bool synth_event_match(const char *system, const char *event,
			int argc, const char **argv, struct dyn_event *ev);

static struct dyn_event_operations synth_event_ops = {
	.create = create_synth_event,
	.show = synth_event_show,
	.is_busy = synth_event_is_busy,
	.free = synth_event_release,
	.match = synth_event_match,
};

static bool is_synth_event(struct dyn_event *ev)
{
	return ev->ops == &synth_event_ops;
}

static struct synth_event *to_synth_event(struct dyn_event *ev)
{
	return container_of(ev, struct synth_event, devent);
}

static bool synth_event_is_busy(struct dyn_event *ev)
{
	struct synth_event *event = to_synth_event(ev);

	return event->ref != 0;
}

static bool synth_event_match(const char *system, const char *event,
			int argc, const char **argv, struct dyn_event *ev)
{
	struct synth_event *sev = to_synth_event(ev);

	return strcmp(sev->name, event) == 0 &&
		(!system || strcmp(system, SYNTH_SYSTEM) == 0);
}

struct synth_trace_event {
	struct trace_entry	ent;
	u64			fields[];
};

static int synth_event_define_fields(struct trace_event_call *call)
{
	struct synth_trace_event trace;
	int offset = offsetof(typeof(trace), fields);
	struct synth_event *event = call->data;
	unsigned int i, size, n_u64;
	char *name, *type;
	bool is_signed;
	int ret = 0;

	for (i = 0, n_u64 = 0; i < event->n_fields; i++) {
		size = event->fields[i]->size;
		is_signed = event->fields[i]->is_signed;
		type = event->fields[i]->type;
		name = event->fields[i]->name;
		ret = trace_define_field(call, type, name, offset, size,
					 is_signed, FILTER_OTHER);
		if (ret)
			break;

		event->fields[i]->offset = n_u64;

		if (event->fields[i]->is_string && !event->fields[i]->is_dynamic) {
			offset += STR_VAR_LEN_MAX;
			n_u64 += STR_VAR_LEN_MAX / sizeof(u64);
		} else {
			offset += sizeof(u64);
			n_u64++;
		}
	}

	event->n_u64 = n_u64;

	return ret;
}

static bool synth_field_signed(char *type)
{
	if (str_has_prefix(type, "u"))
		return false;
	if (strcmp(type, "gfp_t") == 0)
		return false;

	return true;
}

static int synth_field_is_string(char *type)
{
	if (strstr(type, "char[") != NULL)
		return true;

	return false;
}

static int synth_field_string_size(char *type)
{
	char buf[4], *end, *start;
	unsigned int len;
	int size, err;

	start = strstr(type, "char[");
	if (start == NULL)
		return -EINVAL;
	start += sizeof("char[") - 1;

	end = strchr(type, ']');
	if (!end || end < start || type + strlen(type) > end + 1)
		return -EINVAL;

	len = end - start;
	if (len > 3)
		return -EINVAL;

	if (len == 0)
		return 0; /* variable-length string */

	strncpy(buf, start, len);
	buf[len] = '\0';

	err = kstrtouint(buf, 0, &size);
	if (err)
		return err;

	if (size > STR_VAR_LEN_MAX)
		return -EINVAL;

	return size;
}

static int synth_field_size(char *type)
{
	int size = 0;

	if (strcmp(type, "s64") == 0)
		size = sizeof(s64);
	else if (strcmp(type, "u64") == 0)
		size = sizeof(u64);
	else if (strcmp(type, "s32") == 0)
		size = sizeof(s32);
	else if (strcmp(type, "u32") == 0)
		size = sizeof(u32);
	else if (strcmp(type, "s16") == 0)
		size = sizeof(s16);
	else if (strcmp(type, "u16") == 0)
		size = sizeof(u16);
	else if (strcmp(type, "s8") == 0)
		size = sizeof(s8);
	else if (strcmp(type, "u8") == 0)
		size = sizeof(u8);
	else if (strcmp(type, "char") == 0)
		size = sizeof(char);
	else if (strcmp(type, "unsigned char") == 0)
		size = sizeof(unsigned char);
	else if (strcmp(type, "int") == 0)
		size = sizeof(int);
	else if (strcmp(type, "unsigned int") == 0)
		size = sizeof(unsigned int);
	else if (strcmp(type, "long") == 0)
		size = sizeof(long);
	else if (strcmp(type, "unsigned long") == 0)
		size = sizeof(unsigned long);
	else if (strcmp(type, "bool") == 0)
		size = sizeof(bool);
	else if (strcmp(type, "pid_t") == 0)
		size = sizeof(pid_t);
	else if (strcmp(type, "gfp_t") == 0)
		size = sizeof(gfp_t);
	else if (synth_field_is_string(type))
		size = synth_field_string_size(type);

	return size;
}

static const char *synth_field_fmt(char *type)
{
	const char *fmt = "%llu";

	if (strcmp(type, "s64") == 0)
		fmt = "%lld";
	else if (strcmp(type, "u64") == 0)
		fmt = "%llu";
	else if (strcmp(type, "s32") == 0)
		fmt = "%d";
	else if (strcmp(type, "u32") == 0)
		fmt = "%u";
	else if (strcmp(type, "s16") == 0)
		fmt = "%d";
	else if (strcmp(type, "u16") == 0)
		fmt = "%u";
	else if (strcmp(type, "s8") == 0)
		fmt = "%d";
	else if (strcmp(type, "u8") == 0)
		fmt = "%u";
	else if (strcmp(type, "char") == 0)
		fmt = "%d";
	else if (strcmp(type, "unsigned char") == 0)
		fmt = "%u";
	else if (strcmp(type, "int") == 0)
		fmt = "%d";
	else if (strcmp(type, "unsigned int") == 0)
		fmt = "%u";
	else if (strcmp(type, "long") == 0)
		fmt = "%ld";
	else if (strcmp(type, "unsigned long") == 0)
		fmt = "%lu";
	else if (strcmp(type, "bool") == 0)
		fmt = "%d";
	else if (strcmp(type, "pid_t") == 0)
		fmt = "%d";
	else if (strcmp(type, "gfp_t") == 0)
		fmt = "%x";
	else if (synth_field_is_string(type))
		fmt = "%.*s";

	return fmt;
}

static void print_synth_event_num_val(struct trace_seq *s,
				      char *print_fmt, char *name,
				      int size, u64 val, char *space)
{
	switch (size) {
	case 1:
		trace_seq_printf(s, print_fmt, name, (u8)val, space);
		break;

	case 2:
		trace_seq_printf(s, print_fmt, name, (u16)val, space);
		break;

	case 4:
		trace_seq_printf(s, print_fmt, name, (u32)val, space);
		break;

	default:
		trace_seq_printf(s, print_fmt, name, val, space);
		break;
	}
}

static enum print_line_t print_synth_event(struct trace_iterator *iter,
					   int flags,
					   struct trace_event *event)
{
	struct trace_array *tr = iter->tr;
	struct trace_seq *s = &iter->seq;
	struct synth_trace_event *entry;
	struct synth_event *se;
	unsigned int i, n_u64;
	char print_fmt[32];
	const char *fmt;

	entry = (struct synth_trace_event *)iter->ent;
	se = container_of(event, struct synth_event, call.event);

	trace_seq_printf(s, "%s: ", se->name);

	for (i = 0, n_u64 = 0; i < se->n_fields; i++) {
		if (trace_seq_has_overflowed(s))
			goto end;

		fmt = synth_field_fmt(se->fields[i]->type);

		/* parameter types */
		if (tr && tr->trace_flags & TRACE_ITER_VERBOSE)
			trace_seq_printf(s, "%s ", fmt);

		snprintf(print_fmt, sizeof(print_fmt), "%%s=%s%%s", fmt);

		/* parameter values */
		if (se->fields[i]->is_string) {
			if (se->fields[i]->is_dynamic) {
				u32 offset, data_offset;
				char *str_field;

				offset = (u32)entry->fields[n_u64];
				data_offset = offset & 0xffff;

				str_field = (char *)entry + data_offset;

				trace_seq_printf(s, print_fmt, se->fields[i]->name,
						 STR_VAR_LEN_MAX,
						 str_field,
						 i == se->n_fields - 1 ? "" : " ");
				n_u64++;
			} else {
				trace_seq_printf(s, print_fmt, se->fields[i]->name,
						 STR_VAR_LEN_MAX,
						 (char *)&entry->fields[n_u64],
						 i == se->n_fields - 1 ? "" : " ");
				n_u64 += STR_VAR_LEN_MAX / sizeof(u64);
			}
		} else {
			struct trace_print_flags __flags[] = {
			    __def_gfpflag_names, {-1, NULL} };
			char *space = (i == se->n_fields - 1 ? "" : " ");

			print_synth_event_num_val(s, print_fmt,
						  se->fields[i]->name,
						  se->fields[i]->size,
						  entry->fields[n_u64],
						  space);

			if (strcmp(se->fields[i]->type, "gfp_t") == 0) {
				trace_seq_puts(s, " (");
				trace_print_flags_seq(s, "|",
						      entry->fields[n_u64],
						      __flags);
				trace_seq_putc(s, ')');
			}
			n_u64++;
		}
	}
end:
	trace_seq_putc(s, '\n');

	return trace_handle_return(s);
}

static struct trace_event_functions synth_event_funcs = {
	.trace		= print_synth_event
};

static unsigned int trace_string(struct synth_trace_event *entry,
				 struct synth_event *event,
				 char *str_val,
				 bool is_dynamic,
				 unsigned int data_size,
				 unsigned int *n_u64)
{
	unsigned int len = 0;
	char *str_field;

	if (is_dynamic) {
		u32 data_offset;

		data_offset = offsetof(typeof(*entry), fields);
		data_offset += event->n_u64 * sizeof(u64);
		data_offset += data_size;

		str_field = (char *)entry + data_offset;

		len = strlen(str_val) + 1;
		strscpy(str_field, str_val, len);

		data_offset |= len << 16;
		*(u32 *)&entry->fields[*n_u64] = data_offset;

		(*n_u64)++;
	} else {
		str_field = (char *)&entry->fields[*n_u64];

		strscpy(str_field, str_val, STR_VAR_LEN_MAX);
		(*n_u64) += STR_VAR_LEN_MAX / sizeof(u64);
	}

	return len;
}

static notrace void trace_event_raw_event_synth(void *__data,
						u64 *var_ref_vals,
						unsigned int *var_ref_idx)
{
	unsigned int i, n_u64, val_idx, len, data_size = 0;
	struct trace_event_file *trace_file = __data;
	struct synth_trace_event *entry;
	struct trace_event_buffer fbuffer;
	struct trace_buffer *buffer;
	struct synth_event *event;
	int fields_size = 0;

	event = trace_file->event_call->data;

	if (trace_trigger_soft_disabled(trace_file))
		return;

	fields_size = event->n_u64 * sizeof(u64);

	for (i = 0; i < event->n_dynamic_fields; i++) {
		unsigned int field_pos = event->dynamic_fields[i]->field_pos;
		char *str_val;

		val_idx = var_ref_idx[field_pos];
		str_val = (char *)(long)var_ref_vals[val_idx];

		len = strlen(str_val) + 1;

		fields_size += len;
	}

	/*
	 * Avoid ring buffer recursion detection, as this event
	 * is being performed within another event.
	 */
	buffer = trace_file->tr->array_buffer.buffer;
	ring_buffer_nest_start(buffer);

	entry = trace_event_buffer_reserve(&fbuffer, trace_file,
					   sizeof(*entry) + fields_size);
	if (!entry)
		goto out;

	for (i = 0, n_u64 = 0; i < event->n_fields; i++) {
		val_idx = var_ref_idx[i];
		if (event->fields[i]->is_string) {
			char *str_val = (char *)(long)var_ref_vals[val_idx];

			len = trace_string(entry, event, str_val,
					   event->fields[i]->is_dynamic,
					   data_size, &n_u64);
			data_size += len; /* only dynamic string increments */
		} else {
			struct synth_field *field = event->fields[i];
			u64 val = var_ref_vals[val_idx];

			switch (field->size) {
			case 1:
				*(u8 *)&entry->fields[n_u64] = (u8)val;
				break;

			case 2:
				*(u16 *)&entry->fields[n_u64] = (u16)val;
				break;

			case 4:
				*(u32 *)&entry->fields[n_u64] = (u32)val;
				break;

			default:
				entry->fields[n_u64] = val;
				break;
			}
			n_u64++;
		}
	}

	trace_event_buffer_commit(&fbuffer);
out:
	ring_buffer_nest_end(buffer);
}

static void free_synth_event_print_fmt(struct trace_event_call *call)
{
	if (call) {
		kfree(call->print_fmt);
		call->print_fmt = NULL;
	}
}

static int __set_synth_event_print_fmt(struct synth_event *event,
				       char *buf, int len)
{
	const char *fmt;
	int pos = 0;
	int i;

	/* When len=0, we just calculate the needed length */
#define LEN_OR_ZERO (len ? len - pos : 0)

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"");
	for (i = 0; i < event->n_fields; i++) {
		fmt = synth_field_fmt(event->fields[i]->type);
		pos += snprintf(buf + pos, LEN_OR_ZERO, "%s=%s%s",
				event->fields[i]->name, fmt,
				i == event->n_fields - 1 ? "" : ", ");
	}
	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"");

	for (i = 0; i < event->n_fields; i++) {
		if (event->fields[i]->is_string &&
		    event->fields[i]->is_dynamic)
			pos += snprintf(buf + pos, LEN_OR_ZERO,
				", __get_str(%s)", event->fields[i]->name);
		else
			pos += snprintf(buf + pos, LEN_OR_ZERO,
					", REC->%s", event->fields[i]->name);
	}

#undef LEN_OR_ZERO

	/* return the length of print_fmt */
	return pos;
}

static int set_synth_event_print_fmt(struct trace_event_call *call)
{
	struct synth_event *event = call->data;
	char *print_fmt;
	int len;

	/* First: called with 0 length to calculate the needed length */
	len = __set_synth_event_print_fmt(event, NULL, 0);

	print_fmt = kmalloc(len + 1, GFP_KERNEL);
	if (!print_fmt)
		return -ENOMEM;

	/* Second: actually write the @print_fmt */
	__set_synth_event_print_fmt(event, print_fmt, len + 1);
	call->print_fmt = print_fmt;

	return 0;
}

static void free_synth_field(struct synth_field *field)
{
	kfree(field->type);
	kfree(field->name);
	kfree(field);
}

static int check_field_version(const char *prefix, const char *field_type,
			       const char *field_name)
{
	/*
	 * For backward compatibility, the old synthetic event command
	 * format did not require semicolons, and in order to not
	 * break user space, that old format must still work. If a new
	 * feature is added, then the format that uses the new feature
	 * will be required to have semicolons, as nothing that uses
	 * the old format would be using the new, yet to be created,
	 * feature. When a new feature is added, this will detect it,
	 * and return a number greater than 1, and require the format
	 * to use semicolons.
	 */
	return 1;
}

static struct synth_field *parse_synth_field(int argc, char **argv,
					     int *consumed, int *field_version)
{
	const char *prefix = NULL, *field_type = argv[0], *field_name, *array;
	struct synth_field *field;
	int len, ret = -ENOMEM;
	struct seq_buf s;
	ssize_t size;

	if (!strcmp(field_type, "unsigned")) {
		if (argc < 3) {
			synth_err(SYNTH_ERR_INCOMPLETE_TYPE, errpos(field_type));
			return ERR_PTR(-EINVAL);
		}
		prefix = "unsigned ";
		field_type = argv[1];
		field_name = argv[2];
		*consumed += 3;
	} else {
		field_name = argv[1];
		*consumed += 2;
	}

	if (!field_name) {
		synth_err(SYNTH_ERR_INVALID_FIELD, errpos(field_type));
		return ERR_PTR(-EINVAL);
	}

	*field_version = check_field_version(prefix, field_type, field_name);

	field = kzalloc(sizeof(*field), GFP_KERNEL);
	if (!field)
		return ERR_PTR(-ENOMEM);

	len = strlen(field_name);
	array = strchr(field_name, '[');
	if (array)
		len -= strlen(array);

	field->name = kmemdup_nul(field_name, len, GFP_KERNEL);
	if (!field->name)
		goto free;

	if (!is_good_name(field->name)) {
		synth_err(SYNTH_ERR_BAD_NAME, errpos(field_name));
		ret = -EINVAL;
		goto free;
	}

	len = strlen(field_type) + 1;

	if (array)
		len += strlen(array);

	if (prefix)
		len += strlen(prefix);

	field->type = kzalloc(len, GFP_KERNEL);
	if (!field->type)
		goto free;

	seq_buf_init(&s, field->type, len);
	if (prefix)
		seq_buf_puts(&s, prefix);
	seq_buf_puts(&s, field_type);
	if (array)
		seq_buf_puts(&s, array);
	if (WARN_ON_ONCE(!seq_buf_buffer_left(&s)))
		goto free;

	s.buffer[s.len] = '\0';

	size = synth_field_size(field->type);
	if (size < 0) {
		if (array)
			synth_err(SYNTH_ERR_INVALID_ARRAY_SPEC, errpos(field_name));
		else
			synth_err(SYNTH_ERR_INVALID_TYPE, errpos(field_type));
		ret = -EINVAL;
		goto free;
	} else if (size == 0) {
		if (synth_field_is_string(field->type)) {
			char *type;

			len = sizeof("__data_loc ") + strlen(field->type) + 1;
			type = kzalloc(len, GFP_KERNEL);
			if (!type)
				goto free;

			seq_buf_init(&s, type, len);
			seq_buf_puts(&s, "__data_loc ");
			seq_buf_puts(&s, field->type);

			if (WARN_ON_ONCE(!seq_buf_buffer_left(&s)))
				goto free;
			s.buffer[s.len] = '\0';

			kfree(field->type);
			field->type = type;

			field->is_dynamic = true;
			size = sizeof(u64);
		} else {
			synth_err(SYNTH_ERR_INVALID_TYPE, errpos(field_type));
			ret = -EINVAL;
			goto free;
		}
	}
	field->size = size;

	if (synth_field_is_string(field->type))
		field->is_string = true;

	field->is_signed = synth_field_signed(field->type);
 out:
	return field;
 free:
	free_synth_field(field);
	field = ERR_PTR(ret);
	goto out;
}

static void free_synth_tracepoint(struct tracepoint *tp)
{
	if (!tp)
		return;

	kfree(tp->name);
	kfree(tp);
}

static struct tracepoint *alloc_synth_tracepoint(char *name)
{
	struct tracepoint *tp;

	tp = kzalloc(sizeof(*tp), GFP_KERNEL);
	if (!tp)
		return ERR_PTR(-ENOMEM);

	tp->name = kstrdup(name, GFP_KERNEL);
	if (!tp->name) {
		kfree(tp);
		return ERR_PTR(-ENOMEM);
	}

	return tp;
}

struct synth_event *find_synth_event(const char *name)
{
	struct dyn_event *pos;
	struct synth_event *event;

	for_each_dyn_event(pos) {
		if (!is_synth_event(pos))
			continue;
		event = to_synth_event(pos);
		if (strcmp(event->name, name) == 0)
			return event;
	}

	return NULL;
}

static struct trace_event_fields synth_event_fields_array[] = {
	{ .type = TRACE_FUNCTION_TYPE,
	  .define_fields = synth_event_define_fields },
	{}
};

static int register_synth_event(struct synth_event *event)
{
	struct trace_event_call *call = &event->call;
	int ret = 0;

	event->call.class = &event->class;
	event->class.system = kstrdup(SYNTH_SYSTEM, GFP_KERNEL);
	if (!event->class.system) {
		ret = -ENOMEM;
		goto out;
	}

	event->tp = alloc_synth_tracepoint(event->name);
	if (IS_ERR(event->tp)) {
		ret = PTR_ERR(event->tp);
		event->tp = NULL;
		goto out;
	}

	INIT_LIST_HEAD(&call->class->fields);
	call->event.funcs = &synth_event_funcs;
	call->class->fields_array = synth_event_fields_array;

	ret = register_trace_event(&call->event);
	if (!ret) {
		ret = -ENODEV;
		goto out;
	}
	call->flags = TRACE_EVENT_FL_TRACEPOINT;
	call->class->reg = trace_event_reg;
	call->class->probe = trace_event_raw_event_synth;
	call->data = event;
	call->tp = event->tp;

	ret = trace_add_event_call(call);
	if (ret) {
		pr_warn("Failed to register synthetic event: %s\n",
			trace_event_name(call));
		goto err;
	}

	ret = set_synth_event_print_fmt(call);
	if (ret < 0) {
		trace_remove_event_call(call);
		goto err;
	}
 out:
	return ret;
 err:
	unregister_trace_event(&call->event);
	goto out;
}

static int unregister_synth_event(struct synth_event *event)
{
	struct trace_event_call *call = &event->call;
	int ret;

	ret = trace_remove_event_call(call);

	return ret;
}

static void free_synth_event(struct synth_event *event)
{
	unsigned int i;

	if (!event)
		return;

	for (i = 0; i < event->n_fields; i++)
		free_synth_field(event->fields[i]);

	kfree(event->fields);
	kfree(event->dynamic_fields);
	kfree(event->name);
	kfree(event->class.system);
	free_synth_tracepoint(event->tp);
	free_synth_event_print_fmt(&event->call);
	kfree(event);
}

static struct synth_event *alloc_synth_event(const char *name, int n_fields,
					     struct synth_field **fields)
{
	unsigned int i, j, n_dynamic_fields = 0;
	struct synth_event *event;

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event) {
		event = ERR_PTR(-ENOMEM);
		goto out;
	}

	event->name = kstrdup(name, GFP_KERNEL);
	if (!event->name) {
		kfree(event);
		event = ERR_PTR(-ENOMEM);
		goto out;
	}

	event->fields = kcalloc(n_fields, sizeof(*event->fields), GFP_KERNEL);
	if (!event->fields) {
		free_synth_event(event);
		event = ERR_PTR(-ENOMEM);
		goto out;
	}

	for (i = 0; i < n_fields; i++)
		if (fields[i]->is_dynamic)
			n_dynamic_fields++;

	if (n_dynamic_fields) {
		event->dynamic_fields = kcalloc(n_dynamic_fields,
						sizeof(*event->dynamic_fields),
						GFP_KERNEL);
		if (!event->dynamic_fields) {
			free_synth_event(event);
			event = ERR_PTR(-ENOMEM);
			goto out;
		}
	}

	dyn_event_init(&event->devent, &synth_event_ops);

	for (i = 0, j = 0; i < n_fields; i++) {
		fields[i]->field_pos = i;
		event->fields[i] = fields[i];

		if (fields[i]->is_dynamic)
			event->dynamic_fields[j++] = fields[i];
	}
	event->n_dynamic_fields = j;
	event->n_fields = n_fields;
 out:
	return event;
}

static int synth_event_check_arg_fn(void *data)
{
	struct dynevent_arg_pair *arg_pair = data;
	int size;

	size = synth_field_size((char *)arg_pair->lhs);
	if (size == 0) {
		if (strstr((char *)arg_pair->lhs, "["))
			return 0;
	}

	return size ? 0 : -EINVAL;
}

/**
 * synth_event_add_field - Add a new field to a synthetic event cmd
 * @cmd: A pointer to the dynevent_cmd struct representing the new event
 * @type: The type of the new field to add
 * @name: The name of the new field to add
 *
 * Add a new field to a synthetic event cmd object.  Field ordering is in
 * the same order the fields are added.
 *
 * See synth_field_size() for available types. If field_name contains
 * [n] the field is considered to be an array.
 *
 * Return: 0 if successful, error otherwise.
 */
int synth_event_add_field(struct dynevent_cmd *cmd, const char *type,
			  const char *name)
{
	struct dynevent_arg_pair arg_pair;
	int ret;

	if (cmd->type != DYNEVENT_TYPE_SYNTH)
		return -EINVAL;

	if (!type || !name)
		return -EINVAL;

	dynevent_arg_pair_init(&arg_pair, 0, ';');

	arg_pair.lhs = type;
	arg_pair.rhs = name;

	ret = dynevent_arg_pair_add(cmd, &arg_pair, synth_event_check_arg_fn);
	if (ret)
		return ret;

	if (++cmd->n_fields > SYNTH_FIELDS_MAX)
		ret = -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(synth_event_add_field);

/**
 * synth_event_add_field_str - Add a new field to a synthetic event cmd
 * @cmd: A pointer to the dynevent_cmd struct representing the new event
 * @type_name: The type and name of the new field to add, as a single string
 *
 * Add a new field to a synthetic event cmd object, as a single
 * string.  The @type_name string is expected to be of the form 'type
 * name', which will be appended by ';'.  No sanity checking is done -
 * what's passed in is assumed to already be well-formed.  Field
 * ordering is in the same order the fields are added.
 *
 * See synth_field_size() for available types. If field_name contains
 * [n] the field is considered to be an array.
 *
 * Return: 0 if successful, error otherwise.
 */
int synth_event_add_field_str(struct dynevent_cmd *cmd, const char *type_name)
{
	struct dynevent_arg arg;
	int ret;

	if (cmd->type != DYNEVENT_TYPE_SYNTH)
		return -EINVAL;

	if (!type_name)
		return -EINVAL;

	dynevent_arg_init(&arg, ';');

	arg.str = type_name;

	ret = dynevent_arg_add(cmd, &arg, NULL);
	if (ret)
		return ret;

	if (++cmd->n_fields > SYNTH_FIELDS_MAX)
		ret = -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(synth_event_add_field_str);

/**
 * synth_event_add_fields - Add multiple fields to a synthetic event cmd
 * @cmd: A pointer to the dynevent_cmd struct representing the new event
 * @fields: An array of type/name field descriptions
 * @n_fields: The number of field descriptions contained in the fields array
 *
 * Add a new set of fields to a synthetic event cmd object.  The event
 * fields that will be defined for the event should be passed in as an
 * array of struct synth_field_desc, and the number of elements in the
 * array passed in as n_fields.  Field ordering will retain the
 * ordering given in the fields array.
 *
 * See synth_field_size() for available types. If field_name contains
 * [n] the field is considered to be an array.
 *
 * Return: 0 if successful, error otherwise.
 */
int synth_event_add_fields(struct dynevent_cmd *cmd,
			   struct synth_field_desc *fields,
			   unsigned int n_fields)
{
	unsigned int i;
	int ret = 0;

	for (i = 0; i < n_fields; i++) {
		if (fields[i].type == NULL || fields[i].name == NULL) {
			ret = -EINVAL;
			break;
		}

		ret = synth_event_add_field(cmd, fields[i].type, fields[i].name);
		if (ret)
			break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(synth_event_add_fields);

/**
 * __synth_event_gen_cmd_start - Start a synthetic event command from arg list
 * @cmd: A pointer to the dynevent_cmd struct representing the new event
 * @name: The name of the synthetic event
 * @mod: The module creating the event, NULL if not created from a module
 * @args: Variable number of arg (pairs), one pair for each field
 *
 * NOTE: Users normally won't want to call this function directly, but
 * rather use the synth_event_gen_cmd_start() wrapper, which
 * automatically adds a NULL to the end of the arg list.  If this
 * function is used directly, make sure the last arg in the variable
 * arg list is NULL.
 *
 * Generate a synthetic event command to be executed by
 * synth_event_gen_cmd_end().  This function can be used to generate
 * the complete command or only the first part of it; in the latter
 * case, synth_event_add_field(), synth_event_add_field_str(), or
 * synth_event_add_fields() can be used to add more fields following
 * this.
 *
 * There should be an even number variable args, each pair consisting
 * of a type followed by a field name.
 *
 * See synth_field_size() for available types. If field_name contains
 * [n] the field is considered to be an array.
 *
 * Return: 0 if successful, error otherwise.
 */
int __synth_event_gen_cmd_start(struct dynevent_cmd *cmd, const char *name,
				struct module *mod, ...)
{
	struct dynevent_arg arg;
	va_list args;
	int ret;

	cmd->event_name = name;
	cmd->private_data = mod;

	if (cmd->type != DYNEVENT_TYPE_SYNTH)
		return -EINVAL;

	dynevent_arg_init(&arg, 0);
	arg.str = name;
	ret = dynevent_arg_add(cmd, &arg, NULL);
	if (ret)
		return ret;

	va_start(args, mod);
	for (;;) {
		const char *type, *name;

		type = va_arg(args, const char *);
		if (!type)
			break;
		name = va_arg(args, const char *);
		if (!name)
			break;

		if (++cmd->n_fields > SYNTH_FIELDS_MAX) {
			ret = -EINVAL;
			break;
		}

		ret = synth_event_add_field(cmd, type, name);
		if (ret)
			break;
	}
	va_end(args);

	return ret;
}
EXPORT_SYMBOL_GPL(__synth_event_gen_cmd_start);

/**
 * synth_event_gen_cmd_array_start - Start synthetic event command from an array
 * @cmd: A pointer to the dynevent_cmd struct representing the new event
 * @name: The name of the synthetic event
 * @fields: An array of type/name field descriptions
 * @n_fields: The number of field descriptions contained in the fields array
 *
 * Generate a synthetic event command to be executed by
 * synth_event_gen_cmd_end().  This function can be used to generate
 * the complete command or only the first part of it; in the latter
 * case, synth_event_add_field(), synth_event_add_field_str(), or
 * synth_event_add_fields() can be used to add more fields following
 * this.
 *
 * The event fields that will be defined for the event should be
 * passed in as an array of struct synth_field_desc, and the number of
 * elements in the array passed in as n_fields.  Field ordering will
 * retain the ordering given in the fields array.
 *
 * See synth_field_size() for available types. If field_name contains
 * [n] the field is considered to be an array.
 *
 * Return: 0 if successful, error otherwise.
 */
int synth_event_gen_cmd_array_start(struct dynevent_cmd *cmd, const char *name,
				    struct module *mod,
				    struct synth_field_desc *fields,
				    unsigned int n_fields)
{
	struct dynevent_arg arg;
	unsigned int i;
	int ret = 0;

	cmd->event_name = name;
	cmd->private_data = mod;

	if (cmd->type != DYNEVENT_TYPE_SYNTH)
		return -EINVAL;

	if (n_fields > SYNTH_FIELDS_MAX)
		return -EINVAL;

	dynevent_arg_init(&arg, 0);
	arg.str = name;
	ret = dynevent_arg_add(cmd, &arg, NULL);
	if (ret)
		return ret;

	for (i = 0; i < n_fields; i++) {
		if (fields[i].type == NULL || fields[i].name == NULL)
			return -EINVAL;

		ret = synth_event_add_field(cmd, fields[i].type, fields[i].name);
		if (ret)
			break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(synth_event_gen_cmd_array_start);

static int __create_synth_event(const char *name, const char *raw_fields)
{
	char **argv, *field_str, *tmp_fields, *saved_fields = NULL;
	struct synth_field *field, *fields[SYNTH_FIELDS_MAX];
	int consumed, cmd_version = 1, n_fields_this_loop;
	int i, argc, n_fields = 0, ret = 0;
	struct synth_event *event = NULL;

	/*
	 * Argument syntax:
	 *  - Add synthetic event: <event_name> field[;field] ...
	 *  - Remove synthetic event: !<event_name> field[;field] ...
	 *      where 'field' = type field_name
	 */

	if (name[0] == '\0') {
		synth_err(SYNTH_ERR_INVALID_CMD, 0);
		return -EINVAL;
	}

	if (!is_good_name(name)) {
		synth_err(SYNTH_ERR_BAD_NAME, errpos(name));
		return -EINVAL;
	}

	mutex_lock(&event_mutex);

	event = find_synth_event(name);
	if (event) {
		synth_err(SYNTH_ERR_EVENT_EXISTS, errpos(name));
		ret = -EEXIST;
		goto err;
	}

	tmp_fields = saved_fields = kstrdup(raw_fields, GFP_KERNEL);
	if (!tmp_fields) {
		ret = -ENOMEM;
		goto err;
	}

	while ((field_str = strsep(&tmp_fields, ";")) != NULL) {
		argv = argv_split(GFP_KERNEL, field_str, &argc);
		if (!argv) {
			ret = -ENOMEM;
			goto err;
		}

		if (!argc) {
			argv_free(argv);
			continue;
		}

		n_fields_this_loop = 0;
		consumed = 0;
		while (argc > consumed) {
			int field_version;

			field = parse_synth_field(argc - consumed,
						  argv + consumed, &consumed,
						  &field_version);
			if (IS_ERR(field)) {
				argv_free(argv);
				ret = PTR_ERR(field);
				goto err;
			}

			/*
			 * Track the highest version of any field we
			 * found in the command.
			 */
			if (field_version > cmd_version)
				cmd_version = field_version;

			/*
			 * Now sort out what is and isn't valid for
			 * each supported version.
			 *
			 * If we see more than 1 field per loop, it
			 * means we have multiple fields between
			 * semicolons, and that's something we no
			 * longer support in a version 2 or greater
			 * command.
			 */
			if (cmd_version > 1 && n_fields_this_loop >= 1) {
				synth_err(SYNTH_ERR_INVALID_CMD, errpos(field_str));
				ret = -EINVAL;
				goto err;
			}

			fields[n_fields++] = field;
			if (n_fields == SYNTH_FIELDS_MAX) {
				synth_err(SYNTH_ERR_TOO_MANY_FIELDS, 0);
				ret = -EINVAL;
				goto err;
			}

			n_fields_this_loop++;
		}

		if (consumed < argc) {
			synth_err(SYNTH_ERR_INVALID_CMD, 0);
			ret = -EINVAL;
			goto err;
		}

		argv_free(argv);
	}

	if (n_fields == 0) {
		synth_err(SYNTH_ERR_INVALID_CMD, 0);
		ret = -EINVAL;
		goto err;
	}

	event = alloc_synth_event(name, n_fields, fields);
	if (IS_ERR(event)) {
		ret = PTR_ERR(event);
		event = NULL;
		goto err;
	}
	ret = register_synth_event(event);
	if (!ret)
		dyn_event_add(&event->devent, &event->call);
	else
		free_synth_event(event);
 out:
	mutex_unlock(&event_mutex);

	kfree(saved_fields);

	return ret;
 err:
	for (i = 0; i < n_fields; i++)
		free_synth_field(fields[i]);

	goto out;
}

/**
 * synth_event_create - Create a new synthetic event
 * @name: The name of the new synthetic event
 * @fields: An array of type/name field descriptions
 * @n_fields: The number of field descriptions contained in the fields array
 * @mod: The module creating the event, NULL if not created from a module
 *
 * Create a new synthetic event with the given name under the
 * trace/events/synthetic/ directory.  The event fields that will be
 * defined for the event should be passed in as an array of struct
 * synth_field_desc, and the number elements in the array passed in as
 * n_fields. Field ordering will retain the ordering given in the
 * fields array.
 *
 * If the new synthetic event is being created from a module, the mod
 * param must be non-NULL.  This will ensure that the trace buffer
 * won't contain unreadable events.
 *
 * The new synth event should be deleted using synth_event_delete()
 * function.  The new synthetic event can be generated from modules or
 * other kernel code using trace_synth_event() and related functions.
 *
 * Return: 0 if successful, error otherwise.
 */
int synth_event_create(const char *name, struct synth_field_desc *fields,
		       unsigned int n_fields, struct module *mod)
{
	struct dynevent_cmd cmd;
	char *buf;
	int ret;

	buf = kzalloc(MAX_DYNEVENT_CMD_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	synth_event_cmd_init(&cmd, buf, MAX_DYNEVENT_CMD_LEN);

	ret = synth_event_gen_cmd_array_start(&cmd, name, mod,
					      fields, n_fields);
	if (ret)
		goto out;

	ret = synth_event_gen_cmd_end(&cmd);
 out:
	kfree(buf);

	return ret;
}
EXPORT_SYMBOL_GPL(synth_event_create);

static int destroy_synth_event(struct synth_event *se)
{
	int ret;

	if (se->ref)
		return -EBUSY;

	if (trace_event_dyn_busy(&se->call))
		return -EBUSY;

	ret = unregister_synth_event(se);
	if (!ret) {
		dyn_event_remove(&se->devent);
		free_synth_event(se);
	}

	return ret;
}

/**
 * synth_event_delete - Delete a synthetic event
 * @event_name: The name of the new synthetic event
 *
 * Delete a synthetic event that was created with synth_event_create().
 *
 * Return: 0 if successful, error otherwise.
 */
int synth_event_delete(const char *event_name)
{
	struct synth_event *se = NULL;
	struct module *mod = NULL;
	int ret = -ENOENT;

	mutex_lock(&event_mutex);
	se = find_synth_event(event_name);
	if (se) {
		mod = se->mod;
		ret = destroy_synth_event(se);
	}
	mutex_unlock(&event_mutex);

	if (mod) {
		mutex_lock(&trace_types_lock);
		/*
		 * It is safest to reset the ring buffer if the module
		 * being unloaded registered any events that were
		 * used. The only worry is if a new module gets
		 * loaded, and takes on the same id as the events of
		 * this module. When printing out the buffer, traced
		 * events left over from this module may be passed to
		 * the new module events and unexpected results may
		 * occur.
		 */
		tracing_reset_all_online_cpus();
		mutex_unlock(&trace_types_lock);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(synth_event_delete);

static int check_command(const char *raw_command)
{
	char **argv = NULL, *cmd, *saved_cmd, *name_and_field;
	int argc, ret = 0;

	cmd = saved_cmd = kstrdup(raw_command, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	name_and_field = strsep(&cmd, ";");
	if (!name_and_field) {
		ret = -EINVAL;
		goto free;
	}

	if (name_and_field[0] == '!')
		goto free;

	argv = argv_split(GFP_KERNEL, name_and_field, &argc);
	if (!argv) {
		ret = -ENOMEM;
		goto free;
	}
	argv_free(argv);

	if (argc < 3)
		ret = -EINVAL;
free:
	kfree(saved_cmd);

	return ret;
}

static int create_or_delete_synth_event(const char *raw_command)
{
	char *name = NULL, *fields, *p;
	int ret = 0;

	raw_command = skip_spaces(raw_command);
	if (raw_command[0] == '\0')
		return ret;

	last_cmd_set(raw_command);

	ret = check_command(raw_command);
	if (ret) {
		synth_err(SYNTH_ERR_INVALID_CMD, 0);
		return ret;
	}

	p = strpbrk(raw_command, " \t");
	if (!p && raw_command[0] != '!') {
		synth_err(SYNTH_ERR_INVALID_CMD, 0);
		ret = -EINVAL;
		goto free;
	}

	name = kmemdup_nul(raw_command, p ? p - raw_command : strlen(raw_command), GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	if (name[0] == '!') {
		ret = synth_event_delete(name + 1);
		goto free;
	}

	fields = skip_spaces(p);

	ret = __create_synth_event(name, fields);
free:
	kfree(name);

	return ret;
}

static int synth_event_run_command(struct dynevent_cmd *cmd)
{
	struct synth_event *se;
	int ret;

	ret = create_or_delete_synth_event(cmd->seq.buffer);
	if (ret)
		return ret;

	se = find_synth_event(cmd->event_name);
	if (WARN_ON(!se))
		return -ENOENT;

	se->mod = cmd->private_data;

	return ret;
}

/**
 * synth_event_cmd_init - Initialize a synthetic event command object
 * @cmd: A pointer to the dynevent_cmd struct representing the new event
 * @buf: A pointer to the buffer used to build the command
 * @maxlen: The length of the buffer passed in @buf
 *
 * Initialize a synthetic event command object.  Use this before
 * calling any of the other dyenvent_cmd functions.
 */
void synth_event_cmd_init(struct dynevent_cmd *cmd, char *buf, int maxlen)
{
	dynevent_cmd_init(cmd, buf, maxlen, DYNEVENT_TYPE_SYNTH,
			  synth_event_run_command);
}
EXPORT_SYMBOL_GPL(synth_event_cmd_init);

static inline int
__synth_event_trace_init(struct trace_event_file *file,
			 struct synth_event_trace_state *trace_state)
{
	int ret = 0;

	memset(trace_state, '\0', sizeof(*trace_state));

	/*
	 * Normal event tracing doesn't get called at all unless the
	 * ENABLED bit is set (which attaches the probe thus allowing
	 * this code to be called, etc).  Because this is called
	 * directly by the user, we don't have that but we still need
	 * to honor not logging when disabled.  For the iterated
	 * trace case, we save the enabled state upon start and just
	 * ignore the following data calls.
	 */
	if (!(file->flags & EVENT_FILE_FL_ENABLED) ||
	    trace_trigger_soft_disabled(file)) {
		trace_state->disabled = true;
		ret = -ENOENT;
		goto out;
	}

	trace_state->event = file->event_call->data;
out:
	return ret;
}

static inline int
__synth_event_trace_start(struct trace_event_file *file,
			  struct synth_event_trace_state *trace_state,
			  int dynamic_fields_size)
{
	int entry_size, fields_size = 0;
	int ret = 0;

	fields_size = trace_state->event->n_u64 * sizeof(u64);
	fields_size += dynamic_fields_size;

	/*
	 * Avoid ring buffer recursion detection, as this event
	 * is being performed within another event.
	 */
	trace_state->buffer = file->tr->array_buffer.buffer;
	ring_buffer_nest_start(trace_state->buffer);

	entry_size = sizeof(*trace_state->entry) + fields_size;
	trace_state->entry = trace_event_buffer_reserve(&trace_state->fbuffer,
							file,
							entry_size);
	if (!trace_state->entry) {
		ring_buffer_nest_end(trace_state->buffer);
		ret = -EINVAL;
	}

	return ret;
}

static inline void
__synth_event_trace_end(struct synth_event_trace_state *trace_state)
{
	trace_event_buffer_commit(&trace_state->fbuffer);

	ring_buffer_nest_end(trace_state->buffer);
}

/**
 * synth_event_trace - Trace a synthetic event
 * @file: The trace_event_file representing the synthetic event
 * @n_vals: The number of values in vals
 * @args: Variable number of args containing the event values
 *
 * Trace a synthetic event using the values passed in the variable
 * argument list.
 *
 * The argument list should be a list 'n_vals' u64 values.  The number
 * of vals must match the number of field in the synthetic event, and
 * must be in the same order as the synthetic event fields.
 *
 * All vals should be cast to u64, and string vals are just pointers
 * to strings, cast to u64.  Strings will be copied into space
 * reserved in the event for the string, using these pointers.
 *
 * Return: 0 on success, err otherwise.
 */
int synth_event_trace(struct trace_event_file *file, unsigned int n_vals, ...)
{
	unsigned int i, n_u64, len, data_size = 0;
	struct synth_event_trace_state state;
	va_list args;
	int ret;

	ret = __synth_event_trace_init(file, &state);
	if (ret) {
		if (ret == -ENOENT)
			ret = 0; /* just disabled, not really an error */
		return ret;
	}

	if (state.event->n_dynamic_fields) {
		va_start(args, n_vals);

		for (i = 0; i < state.event->n_fields; i++) {
			u64 val = va_arg(args, u64);

			if (state.event->fields[i]->is_string &&
			    state.event->fields[i]->is_dynamic) {
				char *str_val = (char *)(long)val;

				data_size += strlen(str_val) + 1;
			}
		}

		va_end(args);
	}

	ret = __synth_event_trace_start(file, &state, data_size);
	if (ret)
		return ret;

	if (n_vals != state.event->n_fields) {
		ret = -EINVAL;
		goto out;
	}

	data_size = 0;

	va_start(args, n_vals);
	for (i = 0, n_u64 = 0; i < state.event->n_fields; i++) {
		u64 val;

		val = va_arg(args, u64);

		if (state.event->fields[i]->is_string) {
			char *str_val = (char *)(long)val;

			len = trace_string(state.entry, state.event, str_val,
					   state.event->fields[i]->is_dynamic,
					   data_size, &n_u64);
			data_size += len; /* only dynamic string increments */
		} else {
			struct synth_field *field = state.event->fields[i];

			switch (field->size) {
			case 1:
				*(u8 *)&state.entry->fields[n_u64] = (u8)val;
				break;

			case 2:
				*(u16 *)&state.entry->fields[n_u64] = (u16)val;
				break;

			case 4:
				*(u32 *)&state.entry->fields[n_u64] = (u32)val;
				break;

			default:
				state.entry->fields[n_u64] = val;
				break;
			}
			n_u64++;
		}
	}
	va_end(args);
out:
	__synth_event_trace_end(&state);

	return ret;
}
EXPORT_SYMBOL_GPL(synth_event_trace);

/**
 * synth_event_trace_array - Trace a synthetic event from an array
 * @file: The trace_event_file representing the synthetic event
 * @vals: Array of values
 * @n_vals: The number of values in vals
 *
 * Trace a synthetic event using the values passed in as 'vals'.
 *
 * The 'vals' array is just an array of 'n_vals' u64.  The number of
 * vals must match the number of field in the synthetic event, and
 * must be in the same order as the synthetic event fields.
 *
 * All vals should be cast to u64, and string vals are just pointers
 * to strings, cast to u64.  Strings will be copied into space
 * reserved in the event for the string, using these pointers.
 *
 * Return: 0 on success, err otherwise.
 */
int synth_event_trace_array(struct trace_event_file *file, u64 *vals,
			    unsigned int n_vals)
{
	unsigned int i, n_u64, field_pos, len, data_size = 0;
	struct synth_event_trace_state state;
	char *str_val;
	int ret;

	ret = __synth_event_trace_init(file, &state);
	if (ret) {
		if (ret == -ENOENT)
			ret = 0; /* just disabled, not really an error */
		return ret;
	}

	if (state.event->n_dynamic_fields) {
		for (i = 0; i < state.event->n_dynamic_fields; i++) {
			field_pos = state.event->dynamic_fields[i]->field_pos;
			str_val = (char *)(long)vals[field_pos];
			len = strlen(str_val) + 1;
			data_size += len;
		}
	}

	ret = __synth_event_trace_start(file, &state, data_size);
	if (ret)
		return ret;

	if (n_vals != state.event->n_fields) {
		ret = -EINVAL;
		goto out;
	}

	data_size = 0;

	for (i = 0, n_u64 = 0; i < state.event->n_fields; i++) {
		if (state.event->fields[i]->is_string) {
			char *str_val = (char *)(long)vals[i];

			len = trace_string(state.entry, state.event, str_val,
					   state.event->fields[i]->is_dynamic,
					   data_size, &n_u64);
			data_size += len; /* only dynamic string increments */
		} else {
			struct synth_field *field = state.event->fields[i];
			u64 val = vals[i];

			switch (field->size) {
			case 1:
				*(u8 *)&state.entry->fields[n_u64] = (u8)val;
				break;

			case 2:
				*(u16 *)&state.entry->fields[n_u64] = (u16)val;
				break;

			case 4:
				*(u32 *)&state.entry->fields[n_u64] = (u32)val;
				break;

			default:
				state.entry->fields[n_u64] = val;
				break;
			}
			n_u64++;
		}
	}
out:
	__synth_event_trace_end(&state);

	return ret;
}
EXPORT_SYMBOL_GPL(synth_event_trace_array);

/**
 * synth_event_trace_start - Start piecewise synthetic event trace
 * @file: The trace_event_file representing the synthetic event
 * @trace_state: A pointer to object tracking the piecewise trace state
 *
 * Start the trace of a synthetic event field-by-field rather than all
 * at once.
 *
 * This function 'opens' an event trace, which means space is reserved
 * for the event in the trace buffer, after which the event's
 * individual field values can be set through either
 * synth_event_add_next_val() or synth_event_add_val().
 *
 * A pointer to a trace_state object is passed in, which will keep
 * track of the current event trace state until the event trace is
 * closed (and the event finally traced) using
 * synth_event_trace_end().
 *
 * Note that synth_event_trace_end() must be called after all values
 * have been added for each event trace, regardless of whether adding
 * all field values succeeded or not.
 *
 * Note also that for a given event trace, all fields must be added
 * using either synth_event_add_next_val() or synth_event_add_val()
 * but not both together or interleaved.
 *
 * Return: 0 on success, err otherwise.
 */
int synth_event_trace_start(struct trace_event_file *file,
			    struct synth_event_trace_state *trace_state)
{
	int ret;

	if (!trace_state)
		return -EINVAL;

	ret = __synth_event_trace_init(file, trace_state);
	if (ret) {
		if (ret == -ENOENT)
			ret = 0; /* just disabled, not really an error */
		return ret;
	}

	if (trace_state->event->n_dynamic_fields)
		return -ENOTSUPP;

	ret = __synth_event_trace_start(file, trace_state, 0);

	return ret;
}
EXPORT_SYMBOL_GPL(synth_event_trace_start);

static int __synth_event_add_val(const char *field_name, u64 val,
				 struct synth_event_trace_state *trace_state)
{
	struct synth_field *field = NULL;
	struct synth_trace_event *entry;
	struct synth_event *event;
	int i, ret = 0;

	if (!trace_state) {
		ret = -EINVAL;
		goto out;
	}

	/* can't mix add_next_synth_val() with add_synth_val() */
	if (field_name) {
		if (trace_state->add_next) {
			ret = -EINVAL;
			goto out;
		}
		trace_state->add_name = true;
	} else {
		if (trace_state->add_name) {
			ret = -EINVAL;
			goto out;
		}
		trace_state->add_next = true;
	}

	if (trace_state->disabled)
		goto out;

	event = trace_state->event;
	if (trace_state->add_name) {
		for (i = 0; i < event->n_fields; i++) {
			field = event->fields[i];
			if (strcmp(field->name, field_name) == 0)
				break;
		}
		if (!field) {
			ret = -EINVAL;
			goto out;
		}
	} else {
		if (trace_state->cur_field >= event->n_fields) {
			ret = -EINVAL;
			goto out;
		}
		field = event->fields[trace_state->cur_field++];
	}

	entry = trace_state->entry;
	if (field->is_string) {
		char *str_val = (char *)(long)val;
		char *str_field;

		if (field->is_dynamic) { /* add_val can't do dynamic strings */
			ret = -EINVAL;
			goto out;
		}

		if (!str_val) {
			ret = -EINVAL;
			goto out;
		}

		str_field = (char *)&entry->fields[field->offset];
		strscpy(str_field, str_val, STR_VAR_LEN_MAX);
	} else {
		switch (field->size) {
		case 1:
			*(u8 *)&trace_state->entry->fields[field->offset] = (u8)val;
			break;

		case 2:
			*(u16 *)&trace_state->entry->fields[field->offset] = (u16)val;
			break;

		case 4:
			*(u32 *)&trace_state->entry->fields[field->offset] = (u32)val;
			break;

		default:
			trace_state->entry->fields[field->offset] = val;
			break;
		}
	}
 out:
	return ret;
}

/**
 * synth_event_add_next_val - Add the next field's value to an open synth trace
 * @val: The value to set the next field to
 * @trace_state: A pointer to object tracking the piecewise trace state
 *
 * Set the value of the next field in an event that's been opened by
 * synth_event_trace_start().
 *
 * The val param should be the value cast to u64.  If the value points
 * to a string, the val param should be a char * cast to u64.
 *
 * This function assumes all the fields in an event are to be set one
 * after another - successive calls to this function are made, one for
 * each field, in the order of the fields in the event, until all
 * fields have been set.  If you'd rather set each field individually
 * without regard to ordering, synth_event_add_val() can be used
 * instead.
 *
 * Note however that synth_event_add_next_val() and
 * synth_event_add_val() can't be intermixed for a given event trace -
 * one or the other but not both can be used at the same time.
 *
 * Note also that synth_event_trace_end() must be called after all
 * values have been added for each event trace, regardless of whether
 * adding all field values succeeded or not.
 *
 * Return: 0 on success, err otherwise.
 */
int synth_event_add_next_val(u64 val,
			     struct synth_event_trace_state *trace_state)
{
	return __synth_event_add_val(NULL, val, trace_state);
}
EXPORT_SYMBOL_GPL(synth_event_add_next_val);

/**
 * synth_event_add_val - Add a named field's value to an open synth trace
 * @field_name: The name of the synthetic event field value to set
 * @val: The value to set the next field to
 * @trace_state: A pointer to object tracking the piecewise trace state
 *
 * Set the value of the named field in an event that's been opened by
 * synth_event_trace_start().
 *
 * The val param should be the value cast to u64.  If the value points
 * to a string, the val param should be a char * cast to u64.
 *
 * This function looks up the field name, and if found, sets the field
 * to the specified value.  This lookup makes this function more
 * expensive than synth_event_add_next_val(), so use that or the
 * none-piecewise synth_event_trace() instead if efficiency is more
 * important.
 *
 * Note however that synth_event_add_next_val() and
 * synth_event_add_val() can't be intermixed for a given event trace -
 * one or the other but not both can be used at the same time.
 *
 * Note also that synth_event_trace_end() must be called after all
 * values have been added for each event trace, regardless of whether
 * adding all field values succeeded or not.
 *
 * Return: 0 on success, err otherwise.
 */
int synth_event_add_val(const char *field_name, u64 val,
			struct synth_event_trace_state *trace_state)
{
	return __synth_event_add_val(field_name, val, trace_state);
}
EXPORT_SYMBOL_GPL(synth_event_add_val);

/**
 * synth_event_trace_end - End piecewise synthetic event trace
 * @trace_state: A pointer to object tracking the piecewise trace state
 *
 * End the trace of a synthetic event opened by
 * synth_event_trace__start().
 *
 * This function 'closes' an event trace, which basically means that
 * it commits the reserved event and cleans up other loose ends.
 *
 * A pointer to a trace_state object is passed in, which will keep
 * track of the current event trace state opened with
 * synth_event_trace_start().
 *
 * Note that this function must be called after all values have been
 * added for each event trace, regardless of whether adding all field
 * values succeeded or not.
 *
 * Return: 0 on success, err otherwise.
 */
int synth_event_trace_end(struct synth_event_trace_state *trace_state)
{
	if (!trace_state)
		return -EINVAL;

	__synth_event_trace_end(trace_state);

	return 0;
}
EXPORT_SYMBOL_GPL(synth_event_trace_end);

static int create_synth_event(const char *raw_command)
{
	char *fields, *p;
	const char *name;
	int len, ret = 0;

	raw_command = skip_spaces(raw_command);
	if (raw_command[0] == '\0')
		return ret;

	last_cmd_set(raw_command);

	p = strpbrk(raw_command, " \t");
	if (!p) {
		synth_err(SYNTH_ERR_INVALID_CMD, 0);
		return -EINVAL;
	}

	fields = skip_spaces(p);

	name = raw_command;

	if (name[0] != 's' || name[1] != ':')
		return -ECANCELED;
	name += 2;

	/* This interface accepts group name prefix */
	if (strchr(name, '/')) {
		len = str_has_prefix(name, SYNTH_SYSTEM "/");
		if (len == 0) {
			synth_err(SYNTH_ERR_INVALID_DYN_CMD, 0);
			return -EINVAL;
		}
		name += len;
	}

	len = name - raw_command;

	ret = check_command(raw_command + len);
	if (ret) {
		synth_err(SYNTH_ERR_INVALID_CMD, 0);
		return ret;
	}

	name = kmemdup_nul(raw_command + len, p - raw_command - len, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	ret = __create_synth_event(name, fields);

	kfree(name);

	return ret;
}

static int synth_event_release(struct dyn_event *ev)
{
	struct synth_event *event = to_synth_event(ev);
	int ret;

	if (event->ref)
		return -EBUSY;

	if (trace_event_dyn_busy(&event->call))
		return -EBUSY;

	ret = unregister_synth_event(event);
	if (ret)
		return ret;

	dyn_event_remove(ev);
	free_synth_event(event);
	return 0;
}

static int __synth_event_show(struct seq_file *m, struct synth_event *event)
{
	struct synth_field *field;
	unsigned int i;
	char *type, *t;

	seq_printf(m, "%s\t", event->name);

	for (i = 0; i < event->n_fields; i++) {
		field = event->fields[i];

		type = field->type;
		t = strstr(type, "__data_loc");
		if (t) { /* __data_loc belongs in format but not event desc */
			t += sizeof("__data_loc");
			type = t;
		}

		/* parameter values */
		seq_printf(m, "%s %s%s", type, field->name,
			   i == event->n_fields - 1 ? "" : "; ");
	}

	seq_putc(m, '\n');

	return 0;
}

static int synth_event_show(struct seq_file *m, struct dyn_event *ev)
{
	struct synth_event *event = to_synth_event(ev);

	seq_printf(m, "s:%s/", event->class.system);

	return __synth_event_show(m, event);
}

static int synth_events_seq_show(struct seq_file *m, void *v)
{
	struct dyn_event *ev = v;

	if (!is_synth_event(ev))
		return 0;

	return __synth_event_show(m, to_synth_event(ev));
}

static const struct seq_operations synth_events_seq_op = {
	.start	= dyn_event_seq_start,
	.next	= dyn_event_seq_next,
	.stop	= dyn_event_seq_stop,
	.show	= synth_events_seq_show,
};

static int synth_events_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = security_locked_down(LOCKDOWN_TRACEFS);
	if (ret)
		return ret;

	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC)) {
		ret = dyn_events_release_all(&synth_event_ops);
		if (ret < 0)
			return ret;
	}

	return seq_open(file, &synth_events_seq_op);
}

static ssize_t synth_events_write(struct file *file,
				  const char __user *buffer,
				  size_t count, loff_t *ppos)
{
	return trace_parse_run_command(file, buffer, count, ppos,
				       create_or_delete_synth_event);
}

static const struct file_operations synth_events_fops = {
	.open           = synth_events_open,
	.write		= synth_events_write,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
};

/*
 * Register dynevent at core_initcall. This allows kernel to setup kprobe
 * events in postcore_initcall without tracefs.
 */
static __init int trace_events_synth_init_early(void)
{
	int err = 0;

	err = dyn_event_register(&synth_event_ops);
	if (err)
		pr_warn("Could not register synth_event_ops\n");

	return err;
}
core_initcall(trace_events_synth_init_early);

static __init int trace_events_synth_init(void)
{
	struct dentry *entry = NULL;
	int err = 0;
	err = tracing_init_dentry();
	if (err)
		goto err;

	entry = tracefs_create_file("synthetic_events", TRACE_MODE_WRITE,
				    NULL, NULL, &synth_events_fops);
	if (!entry) {
		err = -ENODEV;
		goto err;
	}

	return err;
 err:
	pr_warn("Could not create tracefs 'synthetic_events' entry\n");

	return err;
}

fs_initcall(trace_events_synth_init);
