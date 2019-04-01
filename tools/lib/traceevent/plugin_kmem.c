/*
 * Copyright (C) 2009 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
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
#include "trace-seq.h"

static int call_site_handler(struct trace_seq *s, struct tep_record *record,
			     struct tep_event *event, void *context)
{
	struct tep_format_field *field;
	unsigned long long val, addr;
	void *data = record->data;
	const char *func;

	field = tep_find_field(event, "call_site");
	if (!field)
		return 1;

	if (tep_read_number_field(field, data, &val))
		return 1;

	func = tep_find_function(event->pevent, val);
	if (!func)
		return 1;

	addr = tep_find_function_address(event->pevent, val);

	trace_seq_printf(s, "(%s+0x%x) ", func, (int)(val - addr));
	return 1;
}

int TEP_PLUGIN_LOADER(struct tep_handle *tep)
{
	tep_register_event_handler(tep, -1, "kmem", "kfree",
				   call_site_handler, NULL);

	tep_register_event_handler(tep, -1, "kmem", "kmalloc",
				   call_site_handler, NULL);

	tep_register_event_handler(tep, -1, "kmem", "kmalloc_node",
				   call_site_handler, NULL);

	tep_register_event_handler(tep, -1, "kmem", "kmem_cache_alloc",
				   call_site_handler, NULL);

	tep_register_event_handler(tep, -1, "kmem",
				   "kmem_cache_alloc_node",
				   call_site_handler, NULL);

	tep_register_event_handler(tep, -1, "kmem", "kmem_cache_free",
				   call_site_handler, NULL);
	return 0;
}

void TEP_PLUGIN_UNLOADER(struct tep_handle *tep)
{
	tep_unregister_event_handler(tep, -1, "kmem", "kfree",
				     call_site_handler, NULL);

	tep_unregister_event_handler(tep, -1, "kmem", "kmalloc",
				     call_site_handler, NULL);

	tep_unregister_event_handler(tep, -1, "kmem", "kmalloc_node",
				     call_site_handler, NULL);

	tep_unregister_event_handler(tep, -1, "kmem", "kmem_cache_alloc",
				     call_site_handler, NULL);

	tep_unregister_event_handler(tep, -1, "kmem",
				     "kmem_cache_alloc_node",
				     call_site_handler, NULL);

	tep_unregister_event_handler(tep, -1, "kmem", "kmem_cache_free",
				     call_site_handler, NULL);
}
