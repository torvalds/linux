// SPDX-License-Identifier: GPL-2.0-only
#include <linux/sched/debug.h>
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/sysctl.h>
#include <linux/nmi.h>

#include <linux/sys_info.h>

struct sys_info_name {
	unsigned long bit;
	const char *name;
};

/*
 * When 'si_names' gets updated,  please make sure the 'sys_info_avail'
 * below is updated accordingly.
 */
static const struct sys_info_name  si_names[] = {
	{ SYS_INFO_TASKS,		"tasks" },
	{ SYS_INFO_MEM,			"mem" },
	{ SYS_INFO_TIMERS,		"timers" },
	{ SYS_INFO_LOCKS,		"locks" },
	{ SYS_INFO_FTRACE,		"ftrace" },
	{ SYS_INFO_ALL_CPU_BT,		"all_bt" },
	{ SYS_INFO_BLOCKED_TASKS,	"blocked_tasks" },
};

/* Expecting string like "xxx_sys_info=tasks,mem,timers,locks,ftrace,..." */
unsigned long sys_info_parse_param(char *str)
{
	unsigned long si_bits = 0;
	char *s, *name;
	int i;

	s = str;
	while ((name = strsep(&s, ",")) && *name) {
		for (i = 0; i < ARRAY_SIZE(si_names); i++) {
			if (!strcmp(name, si_names[i].name)) {
				si_bits |= si_names[i].bit;
				break;
			}
		}
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

	si_bits_global = ro_table->data;

	if (write) {
		unsigned long si_bits;
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

		names[0] = '\0';
		for (i = 0; i < ARRAY_SIZE(si_names); i++) {
			if (*si_bits_global & si_names[i].bit) {
				len += scnprintf(names + len, sizeof(names) - len,
					"%s%s", delim, si_names[i].name);
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

	if (si_mask & SYS_INFO_ALL_CPU_BT)
		trigger_all_cpu_backtrace();

	if (si_mask & SYS_INFO_BLOCKED_TASKS)
		show_state_filter(TASK_UNINTERRUPTIBLE);
}
