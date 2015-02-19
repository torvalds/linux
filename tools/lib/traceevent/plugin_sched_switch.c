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

static void write_state(struct trace_seq *s, int val)
{
	const char states[] = "SDTtZXxW";
	int found = 0;
	int i;

	for (i = 0; i < (sizeof(states) - 1); i++) {
		if (!(val & (1 << i)))
			continue;

		if (found)
			trace_seq_putc(s, '|');

		found = 1;
		trace_seq_putc(s, states[i]);
	}

	if (!found)
		trace_seq_putc(s, 'R');
}

static void write_and_save_comm(struct format_field *field,
				struct pevent_record *record,
				struct trace_seq *s, int pid)
{
	const char *comm;
	int len;

	comm = (char *)(record->data + field->offset);
	len = s->len;
	trace_seq_printf(s, "%.*s",
			 field->size, comm);

	/* make sure the comm has a \0 at the end. */
	trace_seq_terminate(s);
	comm = &s->buffer[len];

	/* Help out the comm to ids. This will handle dups */
	pevent_register_comm(field->event->pevent, comm, pid);
}

static int sched_wakeup_handler(struct trace_seq *s,
				struct pevent_record *record,
				struct event_format *event, void *context)
{
	struct format_field *field;
	unsigned long long val;

	if (pevent_get_field_val(s, event, "pid", record, &val, 1))
		return trace_seq_putc(s, '!');

	field = pevent_find_any_field(event, "comm");
	if (field) {
		write_and_save_comm(field, record, s, val);
		trace_seq_putc(s, ':');
	}
	trace_seq_printf(s, "%lld", val);

	if (pevent_get_field_val(s, event, "prio", record, &val, 0) == 0)
		trace_seq_printf(s, " [%lld]", val);

	if (pevent_get_field_val(s, event, "success", record, &val, 1) == 0)
		trace_seq_printf(s, " success=%lld", val);

	if (pevent_get_field_val(s, event, "target_cpu", record, &val, 0) == 0)
		trace_seq_printf(s, " CPU:%03llu", val);

	return 0;
}

static int sched_switch_handler(struct trace_seq *s,
				struct pevent_record *record,
				struct event_format *event, void *context)
{
	struct format_field *field;
	unsigned long long val;

	if (pevent_get_field_val(s, event, "prev_pid", record, &val, 1))
		return trace_seq_putc(s, '!');

	field = pevent_find_any_field(event, "prev_comm");
	if (field) {
		write_and_save_comm(field, record, s, val);
		trace_seq_putc(s, ':');
	}
	trace_seq_printf(s, "%lld ", val);

	if (pevent_get_field_val(s, event, "prev_prio", record, &val, 0) == 0)
		trace_seq_printf(s, "[%lld] ", val);

	if (pevent_get_field_val(s,  event, "prev_state", record, &val, 0) == 0)
		write_state(s, val);

	trace_seq_puts(s, " ==> ");

	if (pevent_get_field_val(s, event, "next_pid", record, &val, 1))
		return trace_seq_putc(s, '!');

	field = pevent_find_any_field(event, "next_comm");
	if (field) {
		write_and_save_comm(field, record, s, val);
		trace_seq_putc(s, ':');
	}
	trace_seq_printf(s, "%lld", val);

	if (pevent_get_field_val(s, event, "next_prio", record, &val, 0) == 0)
		trace_seq_printf(s, " [%lld]", val);

	return 0;
}

int PEVENT_PLUGIN_LOADER(struct pevent *pevent)
{
	pevent_register_event_handler(pevent, -1, "sched", "sched_switch",
				      sched_switch_handler, NULL);

	pevent_register_event_handler(pevent, -1, "sched", "sched_wakeup",
				      sched_wakeup_handler, NULL);

	pevent_register_event_handler(pevent, -1, "sched", "sched_wakeup_new",
				      sched_wakeup_handler, NULL);
	return 0;
}

void PEVENT_PLUGIN_UNLOADER(struct pevent *pevent)
{
	pevent_unregister_event_handler(pevent, -1, "sched", "sched_switch",
					sched_switch_handler, NULL);

	pevent_unregister_event_handler(pevent, -1, "sched", "sched_wakeup",
					sched_wakeup_handler, NULL);

	pevent_unregister_event_handler(pevent, -1, "sched", "sched_wakeup_new",
					sched_wakeup_handler, NULL);
}
