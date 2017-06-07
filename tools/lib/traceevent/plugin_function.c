/*
 * Copyright (C) 2009, 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License (not later!)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not,  see <http://www.gnu.org/licenses>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event-parse.h"
#include "event-utils.h"

static struct func_stack {
	int size;
	char **stack;
} *fstack;

static int cpus = -1;

#define STK_BLK 10

struct pevent_plugin_option plugin_options[] =
{
	{
		.name = "parent",
		.plugin_alias = "ftrace",
		.description =
		"Print parent of functions for function events",
	},
	{
		.name = "indent",
		.plugin_alias = "ftrace",
		.description =
		"Try to show function call indents, based on parents",
		.set = 1,
	},
	{
		.name = NULL,
	}
};

static struct pevent_plugin_option *ftrace_parent = &plugin_options[0];
static struct pevent_plugin_option *ftrace_indent = &plugin_options[1];

static void add_child(struct func_stack *stack, const char *child, int pos)
{
	int i;

	if (!child)
		return;

	if (pos < stack->size)
		free(stack->stack[pos]);
	else {
		char **ptr;

		ptr = realloc(stack->stack, sizeof(char *) *
			      (stack->size + STK_BLK));
		if (!ptr) {
			warning("could not allocate plugin memory\n");
			return;
		}

		stack->stack = ptr;

		for (i = stack->size; i < stack->size + STK_BLK; i++)
			stack->stack[i] = NULL;
		stack->size += STK_BLK;
	}

	stack->stack[pos] = strdup(child);
}

static int add_and_get_index(const char *parent, const char *child, int cpu)
{
	int i;

	if (cpu < 0)
		return 0;

	if (cpu > cpus) {
		struct func_stack *ptr;

		ptr = realloc(fstack, sizeof(*fstack) * (cpu + 1));
		if (!ptr) {
			warning("could not allocate plugin memory\n");
			return 0;
		}

		fstack = ptr;

		/* Account for holes in the cpu count */
		for (i = cpus + 1; i <= cpu; i++)
			memset(&fstack[i], 0, sizeof(fstack[i]));
		cpus = cpu;
	}

	for (i = 0; i < fstack[cpu].size && fstack[cpu].stack[i]; i++) {
		if (strcmp(parent, fstack[cpu].stack[i]) == 0) {
			add_child(&fstack[cpu], child, i+1);
			return i;
		}
	}

	/* Not found */
	add_child(&fstack[cpu], parent, 0);
	add_child(&fstack[cpu], child, 1);
	return 0;
}

static int function_handler(struct trace_seq *s, struct pevent_record *record,
			    struct event_format *event, void *context)
{
	struct pevent *pevent = event->pevent;
	unsigned long long function;
	unsigned long long pfunction;
	const char *func;
	const char *parent;
	int index = 0;

	if (pevent_get_field_val(s, event, "ip", record, &function, 1))
		return trace_seq_putc(s, '!');

	func = pevent_find_function(pevent, function);

	if (pevent_get_field_val(s, event, "parent_ip", record, &pfunction, 1))
		return trace_seq_putc(s, '!');

	parent = pevent_find_function(pevent, pfunction);

	if (parent && ftrace_indent->set)
		index = add_and_get_index(parent, func, record->cpu);

	trace_seq_printf(s, "%*s", index*3, "");

	if (func)
		trace_seq_printf(s, "%s", func);
	else
		trace_seq_printf(s, "0x%llx", function);

	if (ftrace_parent->set) {
		trace_seq_printf(s, " <-- ");
		if (parent)
			trace_seq_printf(s, "%s", parent);
		else
			trace_seq_printf(s, "0x%llx", pfunction);
	}

	return 0;
}

int PEVENT_PLUGIN_LOADER(struct pevent *pevent)
{
	pevent_register_event_handler(pevent, -1, "ftrace", "function",
				      function_handler, NULL);

	traceevent_plugin_add_options("ftrace", plugin_options);

	return 0;
}

void PEVENT_PLUGIN_UNLOADER(struct pevent *pevent)
{
	int i, x;

	pevent_unregister_event_handler(pevent, -1, "ftrace", "function",
					function_handler, NULL);

	for (i = 0; i <= cpus; i++) {
		for (x = 0; x < fstack[i].size && fstack[i].stack[x]; x++)
			free(fstack[i].stack[x]);
		free(fstack[i].stack);
	}

	traceevent_plugin_remove_options(plugin_options);

	free(fstack);
	fstack = NULL;
	cpus = -1;
}
