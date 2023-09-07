// SPDX-License-Identifier: GPL-2.0
/*
 * trace_events_inject - trace event injection
 *
 * Copyright (C) 2019 Cong Wang <cwang@twitter.com>
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/rculist.h>

#include "trace.h"

static int
trace_inject_entry(struct trace_event_file *file, void *rec, int len)
{
	struct trace_event_buffer fbuffer;
	int written = 0;
	void *entry;

	rcu_read_lock_sched();
	entry = trace_event_buffer_reserve(&fbuffer, file, len);
	if (entry) {
		memcpy(entry, rec, len);
		written = len;
		trace_event_buffer_commit(&fbuffer);
	}
	rcu_read_unlock_sched();

	return written;
}

static int
parse_field(char *str, struct trace_event_call *call,
	    struct ftrace_event_field **pf, u64 *pv)
{
	struct ftrace_event_field *field;
	char *field_name;
	int s, i = 0;
	int len;
	u64 val;

	if (!str[i])
		return 0;
	/* First find the field to associate to */
	while (isspace(str[i]))
		i++;
	s = i;
	while (isalnum(str[i]) || str[i] == '_')
		i++;
	len = i - s;
	if (!len)
		return -EINVAL;

	field_name = kmemdup_nul(str + s, len, GFP_KERNEL);
	if (!field_name)
		return -ENOMEM;
	field = trace_find_event_field(call, field_name);
	kfree(field_name);
	if (!field)
		return -ENOENT;

	*pf = field;
	while (isspace(str[i]))
		i++;
	if (str[i] != '=')
		return -EINVAL;
	i++;
	while (isspace(str[i]))
		i++;
	s = i;
	if (isdigit(str[i]) || str[i] == '-') {
		char *num, c;
		int ret;

		/* Make sure the field is not a string */
		if (is_string_field(field))
			return -EINVAL;

		if (str[i] == '-')
			i++;

		/* We allow 0xDEADBEEF */
		while (isalnum(str[i]))
			i++;
		num = str + s;
		c = str[i];
		if (c != '\0' && !isspace(c))
			return -EINVAL;
		str[i] = '\0';
		/* Make sure it is a value */
		if (field->is_signed)
			ret = kstrtoll(num, 0, &val);
		else
			ret = kstrtoull(num, 0, &val);
		str[i] = c;
		if (ret)
			return ret;

		*pv = val;
		return i;
	} else if (str[i] == '\'' || str[i] == '"') {
		char q = str[i];

		/* Make sure the field is OK for strings */
		if (!is_string_field(field))
			return -EINVAL;

		for (i++; str[i]; i++) {
			if (str[i] == '\\' && str[i + 1]) {
				i++;
				continue;
			}
			if (str[i] == q)
				break;
		}
		if (!str[i])
			return -EINVAL;

		/* Skip quotes */
		s++;
		len = i - s;
		if (len >= MAX_FILTER_STR_VAL)
			return -EINVAL;

		*pv = (unsigned long)(str + s);
		str[i] = 0;
		/* go past the last quote */
		i++;
		return i;
	}

	return -EINVAL;
}

static int trace_get_entry_size(struct trace_event_call *call)
{
	struct ftrace_event_field *field;
	struct list_head *head;
	int size = 0;

	head = trace_get_fields(call);
	list_for_each_entry(field, head, link) {
		if (field->size + field->offset > size)
			size = field->size + field->offset;
	}

	return size;
}

static void *trace_alloc_entry(struct trace_event_call *call, int *size)
{
	int entry_size = trace_get_entry_size(call);
	struct ftrace_event_field *field;
	struct list_head *head;
	void *entry = NULL;

	/* We need an extra '\0' at the end. */
	entry = kzalloc(entry_size + 1, GFP_KERNEL);
	if (!entry)
		return NULL;

	head = trace_get_fields(call);
	list_for_each_entry(field, head, link) {
		if (!is_string_field(field))
			continue;
		if (field->filter_type == FILTER_STATIC_STRING)
			continue;
		if (field->filter_type == FILTER_DYN_STRING) {
			u32 *str_item;
			int str_loc = entry_size & 0xffff;

			str_item = (u32 *)(entry + field->offset);
			*str_item = str_loc; /* string length is 0. */
		} else {
			char **paddr;

			paddr = (char **)(entry + field->offset);
			*paddr = "";
		}
	}

	*size = entry_size + 1;
	return entry;
}

#define INJECT_STRING "STATIC STRING CAN NOT BE INJECTED"

/* Caller is responsible to free the *pentry. */
static int parse_entry(char *str, struct trace_event_call *call, void **pentry)
{
	struct ftrace_event_field *field;
	unsigned long irq_flags;
	void *entry = NULL;
	int entry_size;
	u64 val = 0;
	int len;

	entry = trace_alloc_entry(call, &entry_size);
	*pentry = entry;
	if (!entry)
		return -ENOMEM;

	local_save_flags(irq_flags);
	tracing_generic_entry_update(entry, call->event.type, irq_flags,
				     preempt_count());

	while ((len = parse_field(str, call, &field, &val)) > 0) {
		if (is_function_field(field))
			return -EINVAL;

		if (is_string_field(field)) {
			char *addr = (char *)(unsigned long) val;

			if (field->filter_type == FILTER_STATIC_STRING) {
				strlcpy(entry + field->offset, addr, field->size);
			} else if (field->filter_type == FILTER_DYN_STRING) {
				int str_len = strlen(addr) + 1;
				int str_loc = entry_size & 0xffff;
				u32 *str_item;

				entry_size += str_len;
				*pentry = krealloc(entry, entry_size, GFP_KERNEL);
				if (!*pentry) {
					kfree(entry);
					return -ENOMEM;
				}
				entry = *pentry;

				strlcpy(entry + (entry_size - str_len), addr, str_len);
				str_item = (u32 *)(entry + field->offset);
				*str_item = (str_len << 16) | str_loc;
			} else {
				char **paddr;

				paddr = (char **)(entry + field->offset);
				*paddr = INJECT_STRING;
			}
		} else {
			switch (field->size) {
			case 1: {
				u8 tmp = (u8) val;

				memcpy(entry + field->offset, &tmp, 1);
				break;
			}
			case 2: {
				u16 tmp = (u16) val;

				memcpy(entry + field->offset, &tmp, 2);
				break;
			}
			case 4: {
				u32 tmp = (u32) val;

				memcpy(entry + field->offset, &tmp, 4);
				break;
			}
			case 8:
				memcpy(entry + field->offset, &val, 8);
				break;
			default:
				return -EINVAL;
			}
		}

		str += len;
	}

	if (len < 0)
		return len;

	return entry_size;
}

static ssize_t
event_inject_write(struct file *filp, const char __user *ubuf, size_t cnt,
		   loff_t *ppos)
{
	struct trace_event_call *call;
	struct trace_event_file *file;
	int err = -ENODEV, size;
	void *entry = NULL;
	char *buf;

	if (cnt >= PAGE_SIZE)
		return -EINVAL;

	buf = memdup_user_nul(ubuf, cnt);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	strim(buf);

	mutex_lock(&event_mutex);
	file = event_file_data(filp);
	if (file) {
		call = file->event_call;
		size = parse_entry(buf, call, &entry);
		if (size < 0)
			err = size;
		else
			err = trace_inject_entry(file, entry, size);
	}
	mutex_unlock(&event_mutex);

	kfree(entry);
	kfree(buf);

	if (err < 0)
		return err;

	*ppos += err;
	return cnt;
}

static ssize_t
event_inject_read(struct file *file, char __user *buf, size_t size,
		  loff_t *ppos)
{
	return -EPERM;
}

const struct file_operations event_inject_fops = {
	.open = tracing_open_file_tr,
	.read = event_inject_read,
	.write = event_inject_write,
	.release = tracing_release_file_tr,
};
