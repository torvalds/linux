// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/pid_namespace.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/loadavg.h>
#include <linux/sched/stat.h>
#include <linux/seq_file.h>
#include <linux/seqlock.h>
#include <linux/time.h>

static int loadavg_proc_show(struct seq_file *m, void *v)
{
	unsigned long avnrun[3];

	get_avenrun(avnrun, FIXED_1/200, 0);

	seq_printf(m, "%lu.%02lu %lu.%02lu %lu.%02lu %ld/%d %d\n",
		LOAD_INT(avnrun[0]), LOAD_FRAC(avnrun[0]),
		LOAD_INT(avnrun[1]), LOAD_FRAC(avnrun[1]),
		LOAD_INT(avnrun[2]), LOAD_FRAC(avnrun[2]),
		nr_running(), nr_threads,
		idr_get_cursor(&task_active_pid_ns(current)->idr) - 1);
	return 0;
}

static int __init proc_loadavg_init(void)
{
	proc_create_single("loadavg", 0, NULL, loadavg_proc_show);
	return 0;
}
fs_initcall(proc_loadavg_init);
