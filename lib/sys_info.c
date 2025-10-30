// SPDX-License-Identifier: GPL-2.0-only
#include <linux/bitops.h>
#include <linux/console.h>
#include <linux/log2.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/nmi.h>
#include <linux/sched/debug.h>
#include <linux/string.h>
#include <linux/sysctl.h>

#include <linux/sys_info.h>

/*
 * When 'si_names' gets updated,  please make sure the 'sys_info_avail'
 * below is updated accordingly.
 */
static const char * const si_names[] = {
	[ilog2(SYS_INFO_TASKS)]			= "tasks",
	[ilog2(SYS_INFO_MEM)]			= "mem",
	[ilog2(SYS_INFO_TIMERS)]		= "timers",
	[ilog2(SYS_INFO_LOCKS)]			= "locks",
	[ilog2(SYS_INFO_FTRACE)]		= "ftrace",
	[ilog2(SYS_INFO_PANIC_CONSOLE_REPLAY)]	= "",
	[ilog2(SYS_INFO_ALL_BT)]		= "all_bt",
	[ilog2(SYS_INFO_BLOCKED_TASKS)]		= "blocked_tasks",
};

/* Expecting string like "xxx_sys_info=tasks,mem,timers,locks,ftrace,..." */
unsigned long sys_info_parse_param(char *str)
{
	unsigned long si_bits = 0;
	char *s, *name;
	int i;

	s = str;
	while ((name = strsep(&s, ",")) && *name) {
		i = match_string(si_names, ARRAY_SIZE(si_names), name);
		if (i >= 0)
			__set_bit(i, &si_bits);
	}

	return si_bits;
}

#ifdef CONFIG_SYSCTL

static const char sys_info_avail[] __maybe_unused = "tasks,mem,timers,locks,ftrace,all_bt,blocked_tasks";

int sysctl_sys_info_handler(const struct ctl_table *ro_table, int write,
					  void *buffer, size_t *lenp,
					  loff_t *ppos)
{
	char names[sizeof(sys_info_avail)];
	struct ctl_table table;
	unsigned long *si_bits_global;
	unsigned long si_bits;

	si_bits_global = ro_table->data;

	if (write) {
		int ret;

		table = *ro_table;
		table.data = names;
		table.maxlen = sizeof(names);
		ret = proc_dostring(&table, write, buffer, lenp, ppos);
		if (ret)
			return ret;

		si_bits = sys_info_parse_param(names);
		/* The access to the global value is not synchronized. */
		WRITE_ONCE(*si_bits_global, si_bits);
		return 0;
	} else {
		/* for 'read' operation */
		char *delim = "";
		int i, len = 0;

		/* The access to the global value is not synchronized. */
		si_bits = READ_ONCE(*si_bits_global);

		names[0] = '\0';
		for_each_set_bit(i, &si_bits, ARRAY_SIZE(si_names)) {
			if (*si_names[i]) {
				len += scnprintf(names + len, sizeof(names) - len,
						 "%s%s", delim, si_names[i]);
				delim = ",";
			}
		}

		table = *ro_table;
		table.data = names;
		table.maxlen = sizeof(names);
		return proc_dostring(&table, write, buffer, lenp, ppos);
	}
}
#endif

void sys_info(unsigned long si_mask)
{
	if (si_mask & SYS_INFO_TASKS)
		show_state();

	if (si_mask & SYS_INFO_MEM)
		show_mem();

	if (si_mask & SYS_INFO_TIMERS)
		sysrq_timer_list_show();

	if (si_mask & SYS_INFO_LOCKS)
		debug_show_all_locks();

	if (si_mask & SYS_INFO_FTRACE)
		ftrace_dump(DUMP_ALL);

	if (si_mask & SYS_INFO_ALL_BT)
		trigger_all_cpu_backtrace();

	if (si_mask & SYS_INFO_BLOCKED_TASKS)
		show_state_filter(TASK_UNINTERRUPTIBLE);
}
