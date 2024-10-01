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
#include <linux/kernel.h>
#include <traceevent/event-parse.h>

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

/*
 * prev_state is of size long, which is 32 bits on 32 bit architectures.
 * As it needs to have the same bits for both 32 bit and 64 bit architectures
 * we can just assume that the flags we care about will all be within
 * the 32 bits.
 */
#define MAX_STATE_BITS 32

static const char *convert_sym(struct tep_print_flag_sym *sym)
{
	static char save_states[MAX_STATE_BITS + 1];

	memset(save_states, 0, sizeof(save_states));

	/* This is the flags for the prev_state_field, now make them into a string */
	for (; sym; sym = sym->next) {
		long bitmask = strtoul(sym->value, NULL, 0);
		int i;

		for (i = 0; !(bitmask & 1); i++)
			bitmask >>= 1;

		if (i >= MAX_STATE_BITS)
			continue;

		save_states[i] = sym->str[0];
	}

	return save_states;
}

static struct tep_print_arg_field *
find_arg_field(struct tep_format_field *prev_state_field, struct tep_print_arg *arg)
{
	struct tep_print_arg_field *field;

	if (!arg)
		return NULL;

	if (arg->type == TEP_PRINT_FIELD)
		return &arg->field;

	if (arg->type == TEP_PRINT_OP) {
		field = find_arg_field(prev_state_field, arg->op.left);
		if (field && field->field == prev_state_field)
			return field;
		field = find_arg_field(prev_state_field, arg->op.right);
		if (field && field->field == prev_state_field)
			return field;
	}
	return NULL;
}

static struct tep_print_flag_sym *
test_flags(struct tep_format_field *prev_state_field, struct tep_print_arg *arg)
{
	struct tep_print_arg_field *field;

	field = find_arg_field(prev_state_field, arg->flags.field);
	if (!field)
		return NULL;

	return arg->flags.flags;
}

static struct tep_print_flag_sym *
search_op(struct tep_format_field *prev_state_field, struct tep_print_arg *arg)
{
	struct tep_print_flag_sym *sym = NULL;

	if (!arg)
		return NULL;

	if (arg->type == TEP_PRINT_OP) {
		sym = search_op(prev_state_field, arg->op.left);
		if (sym)
			return sym;

		sym = search_op(prev_state_field, arg->op.right);
		if (sym)
			return sym;
	} else if (arg->type == TEP_PRINT_FLAGS) {
		sym = test_flags(prev_state_field, arg);
	}

	return sym;
}

const char *parse_task_states(struct tep_format_field *state_field)
{
	struct tep_print_flag_sym *sym;
	struct tep_print_arg *arg;
	struct tep_event *event;

	event = state_field->event;

	/*
	 * Look at the event format fields, and search for where
	 * the prev_state is parsed via the format flags.
	 */
	for (arg = event->print_fmt.args; arg; arg = arg->next) {
		/*
		 * Currently, the __print_flags() for the prev_state
		 * is embedded in operations, so they too must be
		 * searched.
		 */
		sym = search_op(state_field, arg);
		if (sym)
			return convert_sym(sym);
	}
	return NULL;
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
