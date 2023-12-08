// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009, Steven Rostedt <srostedt@redhat.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "debug.h"
#include "trace-event.h"

#include <linux/ctype.h>

static int get_common_field(struct scripting_context *context,
			    int *offset, int *size, const char *type)
{
	struct tep_handle *pevent = context->pevent;
	struct tep_event *event;
	struct tep_format_field *field;

	if (!*size) {

		event = tep_get_first_event(pevent);
		if (!event)
			return 0;

		field = tep_find_common_field(event, type);
		if (!field)
			return 0;
		*offset = field->offset;
		*size = field->size;
	}

	return tep_read_number(pevent, context->event_data + *offset, *size);
}

int common_lock_depth(struct scripting_context *context)
{
	static int offset;
	static int size;
	int ret;

	ret = get_common_field(context, &size, &offset,
			       "common_lock_depth");
	if (ret < 0)
		return -1;

	return ret;
}

int common_flags(struct scripting_context *context)
{
	static int offset;
	static int size;
	int ret;

	ret = get_common_field(context, &size, &offset,
			       "common_flags");
	if (ret < 0)
		return -1;

	return ret;
}

int common_pc(struct scripting_context *context)
{
	static int offset;
	static int size;
	int ret;

	ret = get_common_field(context, &size, &offset,
			       "common_preempt_count");
	if (ret < 0)
		return -1;

	return ret;
}

unsigned long long
raw_field_value(struct tep_event *event, const char *name, void *data)
{
	struct tep_format_field *field;
	unsigned long long val;

	field = tep_find_any_field(event, name);
	if (!field)
		return 0ULL;

	tep_read_number_field(field, data, &val);

	return val;
}

unsigned long long read_size(struct tep_event *event, void *ptr, int size)
{
	return tep_read_number(event->tep, ptr, size);
}

void event_format__fprintf(struct tep_event *event,
			   int cpu, void *data, int size, FILE *fp)
{
	struct tep_record record;
	struct trace_seq s;

	memset(&record, 0, sizeof(record));
	record.cpu = cpu;
	record.size = size;
	record.data = data;

	trace_seq_init(&s);
	tep_print_event(event->tep, &s, &record, "%s", TEP_PRINT_INFO);
	trace_seq_do_fprintf(&s, fp);
	trace_seq_destroy(&s);
}

void event_format__print(struct tep_event *event,
			 int cpu, void *data, int size)
{
	return event_format__fprintf(event, cpu, data, size, stdout);
}

void parse_ftrace_printk(struct tep_handle *pevent,
			 char *file, unsigned int size __maybe_unused)
{
	unsigned long long addr;
	char *printk;
	char *line;
	char *next = NULL;
	char *addr_str;
	char *fmt = NULL;

	line = strtok_r(file, "\n", &next);
	while (line) {
		addr_str = strtok_r(line, ":", &fmt);
		if (!addr_str) {
			pr_warning("printk format with empty entry");
			break;
		}
		addr = strtoull(addr_str, NULL, 16);
		/* fmt still has a space, skip it */
		printk = strdup(fmt+1);
		line = strtok_r(NULL, "\n", &next);
		tep_register_print_string(pevent, printk, addr);
		free(printk);
	}
}

void parse_saved_cmdline(struct tep_handle *pevent,
			 char *file, unsigned int size __maybe_unused)
{
	char comm[17]; /* Max comm length in the kernel is 16. */
	char *line;
	char *next = NULL;
	int pid;

	line = strtok_r(file, "\n", &next);
	while (line) {
		if (sscanf(line, "%d %16s", &pid, comm) == 2)
			tep_register_comm(pevent, comm, pid);
		line = strtok_r(NULL, "\n", &next);
	}
}

int parse_ftrace_file(struct tep_handle *pevent, char *buf, unsigned long size)
{
	return tep_parse_event(pevent, buf, size, "ftrace");
}

int parse_event_file(struct tep_handle *pevent,
		     char *buf, unsigned long size, char *sys)
{
	return tep_parse_event(pevent, buf, size, sys);
}

struct flag {
	const char *name;
	unsigned long long value;
};

static const struct flag flags[] = {
	{ "HI_SOFTIRQ", 0 },
	{ "TIMER_SOFTIRQ", 1 },
	{ "NET_TX_SOFTIRQ", 2 },
	{ "NET_RX_SOFTIRQ", 3 },
	{ "BLOCK_SOFTIRQ", 4 },
	{ "IRQ_POLL_SOFTIRQ", 5 },
	{ "TASKLET_SOFTIRQ", 6 },
	{ "SCHED_SOFTIRQ", 7 },
	{ "HRTIMER_SOFTIRQ", 8 },
	{ "RCU_SOFTIRQ", 9 },

	{ "HRTIMER_NORESTART", 0 },
	{ "HRTIMER_RESTART", 1 },
};

unsigned long long eval_flag(const char *flag)
{
	int i;

	/*
	 * Some flags in the format files do not get converted.
	 * If the flag is not numeric, see if it is something that
	 * we already know about.
	 */
	if (isdigit(flag[0]))
		return strtoull(flag, NULL, 0);

	for (i = 0; i < (int)(ARRAY_SIZE(flags)); i++)
		if (strcmp(flags[i].name, flag) == 0)
			return flags[i].value;

	return 0;
}
