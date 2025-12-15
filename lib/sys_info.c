// SPDX-License-Identifier: GPL-2.0-only
#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/console.h>
#include <linux/log2.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/nmi.h>
#include <linux/sched/debug.h>
#include <linux/string.h>
#include <linux/sysctl.h>

#include <linux/sys_info.h>

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

/*
 * Default kernel sys_info mask.
 * If a kernel module calls sys_info() with "parameter == 0", then
 * this mask will be used.
 */
static unsigned long kernel_si_mask;

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

static int sys_info_write_handler(const struct ctl_table *table,
				  void *buffer, size_t *lenp, loff_t *ppos,
				  unsigned long *si_bits_global)
{
	unsigned long si_bits;
	int ret;

	ret = proc_dostring(table, 1, buffer, lenp, ppos);
	if (ret)
		return ret;

	si_bits = sys_info_parse_param(table->data);

	/* The access to the global value is not synchronized. */
	WRITE_ONCE(*si_bits_global, si_bits);

	return 0;
}

static int sys_info_read_handler(const struct ctl_table *table,
				 void *buffer, size_t *lenp, loff_t *ppos,
				 unsigned long *si_bits_global)
{
	unsigned long si_bits;
	unsigned int len = 0;
	char *delim = "";
	unsigned int i;

	/* The access to the global value is not synchronized. */
	si_bits = READ_ONCE(*si_bits_global);

	for_each_set_bit(i, &si_bits, ARRAY_SIZE(si_names)) {
		if (*si_names[i]) {
			len += scnprintf(table->data + len, table->maxlen - len,
					 "%s%s", delim, si_names[i]);
			delim = ",";
		}
	}

	return proc_dostring(table, 0, buffer, lenp, ppos);
}

int sysctl_sys_info_handler(const struct ctl_table *ro_table, int write,
					  void *buffer, size_t *lenp,
					  loff_t *ppos)
{
	struct ctl_table table;
	unsigned int i;
	size_t maxlen;

	maxlen = 0;
	for (i = 0; i < ARRAY_SIZE(si_names); i++)
		maxlen += strlen(si_names[i]) + 1;

	char *names __free(kfree) = kzalloc(maxlen, GFP_KERNEL);
	if (!names)
		return -ENOMEM;

	table = *ro_table;
	table.data = names;
	table.maxlen = maxlen;

	if (write)
		return sys_info_write_handler(&table, buffer, lenp, ppos, ro_table->data);
	else
		return sys_info_read_handler(&table, buffer, lenp, ppos, ro_table->data);
}

static const struct ctl_table sys_info_sysctls[] = {
	{
		.procname	= "kernel_sys_info",
		.data		= &kernel_si_mask,
		.maxlen         = sizeof(kernel_si_mask),
		.mode		= 0644,
		.proc_handler	= sysctl_sys_info_handler,
	},
};

static int __init sys_info_sysctl_init(void)
{
	register_sysctl_init("kernel", sys_info_sysctls);
	return 0;
}
subsys_initcall(sys_info_sysctl_init);
#endif

static void __sys_info(unsigned long si_mask)
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

void sys_info(unsigned long si_mask)
{
	__sys_info(si_mask ? : kernel_si_mask);
}
