// SPDX-License-Identifier: GPL-2.0-only
#include <linux/sched/debug.h>
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/nmi.h>

#include <linux/sys_info.h>

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

	if (si_mask & SYS_INFO_ALL_CPU_BT)
		trigger_all_cpu_backtrace();

	if (si_mask & SYS_INFO_BLOCKED_TASKS)
		show_state_filter(TASK_UNINTERRUPTIBLE);
}
