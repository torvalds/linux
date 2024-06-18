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

static struct trace_iterator iter;
static struct ring_buffer_iter *buffer_iter[CONFIG_NR_CPUS];

static void ftrace_dump_buf(int skip_entries, long cpu_file)
{
	struct trace_array *tr;
	unsigned int old_userobj;
	int cnt = 0, cpu;

	tr = iter.tr;

	old_userobj = tr->trace_flags;

	/* don't look at user memory in panic mode */
	tr->trace_flags &= ~TRACE_ITER_SYM_USEROBJ;

	kdb_printf("Dumping ftrace buffer:\n");
	if (skip_entries)
		kdb_printf("(skipping %d entries)\n", skip_entries);

	trace_iterator_reset(&iter);
	iter.iter_flags |= TRACE_FILE_LAT_FMT;

	if (cpu_file == RING_BUFFER_ALL_CPUS) {
		for_each_tracing_cpu(cpu) {
			iter.buffer_iter[cpu] =
			ring_buffer_read_prepare(iter.array_buffer->buffer,
						 cpu, GFP_ATOMIC);
			ring_buffer_read_start(iter.buffer_iter[cpu]);
			tracing_iter_reset(&iter, cpu);
		}
	} else {
		iter.cpu_file = cpu_file;
		iter.buffer_iter[cpu_file] =
			ring_buffer_read_prepare(iter.array_buffer->buffer,
						 cpu_file, GFP_ATOMIC);
		ring_buffer_read_start(iter.buffer_iter[cpu_file]);
		tracing_iter_reset(&iter, cpu_file);
	}

	while (trace_find_next_entry_inc(&iter)) {
		if (!cnt)
			kdb_printf("---------------------------------\n");
		cnt++;

		if (!skip_entries) {
			print_trace_line(&iter);
			trace_printk_seq(&iter.seq);
		} else {
			skip_entries--;
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
	int skip_entries = 0;
	long cpu_file;
	char *cp;
	int cnt;
	int cpu;

	if (argc > 2)
		return KDB_ARGCOUNT;

	if (argc) {
		skip_entries = simple_strtol(argv[1], &cp, 0);
		if (*cp)
			skip_entries = 0;
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

	trace_init_global_iter(&iter);
	iter.buffer_iter = buffer_iter;

	for_each_tracing_cpu(cpu) {
		atomic_inc(&per_cpu_ptr(iter.array_buffer->data, cpu)->disabled);
	}

	/* A negative skip_entries means skip all but the last entries */
	if (skip_entries < 0) {
		if (cpu_file == RING_BUFFER_ALL_CPUS)
			cnt = trace_total_entries(NULL);
		else
			cnt = trace_total_entries_cpu(NULL, cpu_file);
		skip_entries = max(cnt + skip_entries, 0);
	}

	ftrace_dump_buf(skip_entries, cpu_file);

	for_each_tracing_cpu(cpu) {
		atomic_dec(&per_cpu_ptr(iter.array_buffer->data, cpu)->disabled);
	}

	kdb_trap_printk--;

	return 0;
}

static kdbtab_t ftdump_cmd = {
	.name = "ftdump",
	.func = kdb_ftdump,
	.usage = "[skip_#entries] [cpu]",
	.help = "Dump ftrace log; -skip dumps last #entries",
	.flags = KDB_ENABLE_ALWAYS_SAFE,
};

static __init int kdb_ftrace_register(void)
{
	kdb_register(&ftdump_cmd);
	return 0;
}

late_initcall(kdb_ftrace_register);
