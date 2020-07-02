/*
 * Copyright (C) 2015 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
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

enum tlb_flush_reason {
	TLB_FLUSH_ON_TASK_SWITCH,
	TLB_REMOTE_SHOOTDOWN,
	TLB_LOCAL_SHOOTDOWN,
	TLB_LOCAL_MM_SHOOTDOWN,
	NR_TLB_FLUSH_REASONS,
};

static int tlb_flush_handler(struct trace_seq *s, struct tep_record *record,
			     struct tep_event *event, void *context)
{
	unsigned long long val;

	trace_seq_printf(s, "pages=");

	tep_print_num_field(s, "%ld", event, "pages", record, 1);

	if (tep_get_field_val(s, event, "reason", record, &val, 1) < 0)
		return -1;

	trace_seq_puts(s, " reason=");

	switch (val) {
	case TLB_FLUSH_ON_TASK_SWITCH:
		trace_seq_puts(s, "flush on task switch");
		break;
	case TLB_REMOTE_SHOOTDOWN:
		trace_seq_puts(s, "remote shootdown");
		break;
	case TLB_LOCAL_SHOOTDOWN:
		trace_seq_puts(s, "local shootdown");
		break;
	case TLB_LOCAL_MM_SHOOTDOWN:
		trace_seq_puts(s, "local mm shootdown");
		break;
	}

	trace_seq_printf(s, " (%lld)", val);

	return 0;
}

int TEP_PLUGIN_LOADER(struct tep_handle *tep)
{
	tep_register_event_handler(tep, -1, "tlb", "tlb_flush",
				   tlb_flush_handler, NULL);

	return 0;
}

void TEP_PLUGIN_UNLOADER(struct tep_handle *tep)
{
	tep_unregister_event_handler(tep, -1,
				     "tlb", "tlb_flush",
				     tlb_flush_handler, NULL);
}
