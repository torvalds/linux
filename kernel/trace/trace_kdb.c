// SPDX-License-Identifier: GPL-2.0
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

#include "trace.h"
#include "trace_output.h"

static void ftrace_dump_buf(int skip_lines, long cpu_file)
{
	/* use static because iter can be a bit big for the stack */
	static struct trace_iterator iter;
	static struct ring_buffer_iter *buffer_iter[CONFIG_NR_CPUS];
	struct trace_array *tr;
	unsigned int old_userobj;
	int cnt = 0, cpu;

	trace_init_global_iter(&iter);
	iter.buffer_iter = buffer_iter;
	tr = iter.tr;

	for_each_tracing_cpu(cpu) {
		atomic_inc(&per_cpu_ptr(iter.trace_buffer->data, cpu)->disabled);
	}

	old_userobj = tr->trace_flags;

	/* don't look at user memory in panic mode */
	tr->trace_flags &= ~TRACE_ITER_SYM_USEROBJ;

	kdb_printf("Dumping ftrace buffer:\n");

	/* reset all but tr, trace, and overruns */
	memset(&iter.seq, 0,
		   sizeof(struct trace_iterator) -
		   offsetof(struct trace_iterator, seq));
	iter.iter_flags |= TRACE_FILE_LAT_FMT;
	iter.pos = -1;

	if (cpu_file == RING_BUFFER_ALL_CPUS) {
		for_each_tracing_cpu(cpu) {
			iter.buffer_iter[cpu] =
			ring_buffer_read_prepare(iter.trace_buffer->buffer,
						 cpu, GFP_ATOMIC);
			ring_buffer_read_start(iter.buffer_iter[cpu]);
			tracing_iter_reset(&iter, cpu);
		}
	} else {
		iter.cpu_file = cpu_file;
		iter.buffer_iter[cpu_file] =
			ring_buffer_read_prepare(iter.trace_buffer->buffer,
						 cpu_file, GFP_ATOMIC);
		ring_buffer_read_start(iter.buffer_iter[cpu_file]);
		tracing_iter_reset(&iter, cpu_file);
	}

	while (trace_find_next_entry_inc(&iter)) {
		if (!cnt)
			kdb_printf("---------------------------------\n");
		cnt++;

		if (!skip_lines) {
			print_trace_line(&iter);
			trace_printk_seq(&iter.seq);
		} else {
			skip_lines--;
		}

		if (KDB_FLAG(CMD_INTERRUPT))
			goto out;
	}

	if (!cnt)
		kdb_printf("   (ftrace buffer empty)\n");
	else
		kdb_printf("---------------------------------\n");

out:
	tr->trace_flags = old_userobj;

	for_each_tracing_cpu(cpu) {
		atomic_dec(&per_cpu_ptr(iter.trace_buffer->data, cpu)->disabled);
	}

	for_each_tracing_cpu(cpu) {
		if (iter.buffer_iter[cpu]) {
			ring_buffer_read_finish(iter.buffer_iter[cpu]);
			iter.buffer_iter[cpu] = NULL;
		}
	}
}

/*
 * kdb_ftdump - Dump the ftrace log buffer
 */
static int kdb_ftdump(int argc, const char **argv)
{
	int skip_lines = 0;
	long cpu_file;
	char *cp;

	if (argc > 2)
		return KDB_ARGCOUNT;

	if (argc) {
		skip_lines = simple_strtol(argv[1], &cp, 0);
		if (*cp)
			skip_lines = 0;
	}

	if (argc == 2) {
		cpu_file = simple_strtol(argv[2], &cp, 0);
		if (*cp || cpu_file >= NR_CPUS || cpu_file < 0 ||
		    !cpu_online(cpu_file))
			return KDB_BADINT;
	} else {
		cpu_file = RING_BUFFER_ALL_CPUS;
	}

	kdb_trap_printk++;
	ftrace_dump_buf(skip_lines, cpu_file);
	kdb_trap_printk--;

	return 0;
}

static __init int kdb_ftrace_register(void)
{
	kdb_register_flags("ftdump", kdb_ftdump, "[skip_#lines] [cpu]",
			    "Dump ftrace log", 0, KDB_ENABLE_ALWAYS_SAFE);
	return 0;
}

late_initcall(kdb_ftrace_register);
