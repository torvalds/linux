/*
 * Copyright (C) 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
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

#define MINORBITS	20
#define MINORMASK	((1U << MINORBITS) - 1)

#define MAJOR(dev)	((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)	((unsigned int) ((dev) & MINORMASK))

static unsigned long long
process_jbd2_dev_to_name(struct trace_seq *s, unsigned long long *args)
{
	unsigned int dev = args[0];

	trace_seq_printf(s, "%d:%d", MAJOR(dev), MINOR(dev));
	return 0;
}

static unsigned long long
process_jiffies_to_msecs(struct trace_seq *s, unsigned long long *args)
{
	unsigned long long jiffies = args[0];

	trace_seq_printf(s, "%lld", jiffies);
	return jiffies;
}

int PEVENT_PLUGIN_LOADER(struct pevent *pevent)
{
	pevent_register_print_function(pevent,
				       process_jbd2_dev_to_name,
				       PEVENT_FUNC_ARG_STRING,
				       "jbd2_dev_to_name",
				       PEVENT_FUNC_ARG_INT,
				       PEVENT_FUNC_ARG_VOID);

	pevent_register_print_function(pevent,
				       process_jiffies_to_msecs,
				       PEVENT_FUNC_ARG_LONG,
				       "jiffies_to_msecs",
				       PEVENT_FUNC_ARG_LONG,
				       PEVENT_FUNC_ARG_VOID);
	return 0;
}

void PEVENT_PLUGIN_UNLOADER(struct pevent *pevent)
{
	pevent_unregister_print_function(pevent, process_jbd2_dev_to_name,
					 "jbd2_dev_to_name");

	pevent_unregister_print_function(pevent, process_jiffies_to_msecs,
					 "jiffies_to_msecs");
}
