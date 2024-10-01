// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (C) 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event-parse.h"
#include "trace-seq.h"

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

int TEP_PLUGIN_LOADER(struct tep_handle *tep)
{
	tep_register_print_function(tep,
				    process_jbd2_dev_to_name,
				    TEP_FUNC_ARG_STRING,
				    "jbd2_dev_to_name",
				    TEP_FUNC_ARG_INT,
				    TEP_FUNC_ARG_VOID);

	tep_register_print_function(tep,
				    process_jiffies_to_msecs,
				    TEP_FUNC_ARG_LONG,
				    "jiffies_to_msecs",
				    TEP_FUNC_ARG_LONG,
				    TEP_FUNC_ARG_VOID);
	return 0;
}

void TEP_PLUGIN_UNLOADER(struct tep_handle *tep)
{
	tep_unregister_print_function(tep, process_jbd2_dev_to_name,
				      "jbd2_dev_to_name");

	tep_unregister_print_function(tep, process_jiffies_to_msecs,
				      "jiffies_to_msecs");
}
