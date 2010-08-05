/*
 * kdb helper for dumping the ftrace buffer
 *
 * Copyright (C) 2010 Jason Wessel <jason.wessel@windriver.com>
 *
 * ftrace_dump_buf based on ftrace_dump:
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 *
 */
#include <linux/init.h>
#include <linux/kgdb.h>
#include <linux/kdb.h>
#include <linux/ftrace.h>

#include "../debug/kdb/kdb_private.h"
#include "trace.h"
#include "trace_output.h"

static void ftrace_dump_buf(int skip_lines)
{
	/* use static because iter can be a bit big for the stack */
	static struct trace_iterator iter;
	unsigned int old_userobj;
	int cnt = 0, cpu;

	trace_init_global_iter(&iter);

	for_each_tracing_cpu(cpu) {
		atomic_inc(&iter.tr->data[cpu]->disabled);
	}

	old_userobj = trace_flags;

	/* don't look at user memory in panic mode */
	trace_flags &= ~TRACE_ITER_SYM_USEROBJ;

	kdb_printf("Dumping ftrace buffer:\n");

	/* reset all but tr, trace, and overruns */
	memset(&iter.seq, 0,
		   sizeof(struct trace_iterator) -
		   offsetof(struct trace_iterator, seq));
	iter.iter_flags |= TRACE_FILE_LAT_FMT;
	iter.pos = -1;

	for_each_tracing_cpu(cpu) {
		iter.buffer_iter[cpu] =
			ring_buffer_read_prepare(iter.tr->buffer, cpu);
		ring_buffer_read_start(iter.buffer_iter[cpu]);
		tracing_iter_reset(&iter, cpu);
	}

	if (!trace_empty(&iter))
		trace_find_next_entry_inc(&iter);
	while (!trace_empty(&iter)) {
		if (!cnt)
			kdb_printf("---------------------------------\n");
		cnt++;

		if (trace_find_next_entry_inc(&iter) != NULL && !skip_lines)
			print_trace_line(&iter);
		if (!skip_lines)
			trace_printk_seq(&iter.seq);
		else
			skip_lines--;
		if (KDB_FLAG(CMD_INTERRUPT))
			goto out;
	}

	if (!cnt)
		kdb_printf("   (ftrace buffer empty)\n");
	else
		kdb_printf("---------------------------------\n");

out:
	trace_flags = old_userobj;

	for_each_tracing_cpu(cpu) {
		atomic_dec(&iter.tr->data[cpu]->disabled);
	}

	for_each_tracing_cpu(cpu)
		if (iter.buffer_iter[cpu])
			ring_buffer_read_finish(iter.buffer_iter[cpu]);
}

/*
 * kdb_ftdump - Dump the ftrace log buffer
 */
static int kdb_ftdump(int argc, const char **argv)
{
	int skip_lines = 0;
	char *cp;

	if (argc > 1)
		return KDB_ARGCOUNT;

	if (argc) {
		skip_lines = simple_strtol(argv[1], &cp, 0);
		if (*cp)
			skip_lines = 0;
	}

	kdb_trap_printk++;
	ftrace_dump_buf(skip_lines);
	kdb_trap_printk--;

	return 0;
}

static __init int kdb_ftrace_register(void)
{
	kdb_register_repeat("ftdump", kdb_ftdump, "", "Dump ftrace log",
			    0, KDB_REPEAT_NONE);
	return 0;
}

late_initcall(kdb_ftrace_register);
