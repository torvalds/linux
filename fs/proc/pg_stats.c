
/*
 * fs/proc/pg_stats.c
 *
 * This module creates a /proc/pg_stats entry which displays page table operation
 * statistics for each process. The statistics include allocation, free, and set counts
 * for each level of the page table hierarchy: PGD, PUD, PMD, and PTE.
 *
 * Copyright (C) 2025 Your Name
 * Licensed under GPL v2
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched/signal.h>

/* Show function for the seq_file: prints out stats for each process */
static int pg_stats_show(struct seq_file *m, void *v)
{
	struct task_struct *task;

	rcu_read_lock();
	for_each_process(task) {
		seq_printf(m, "%d: [%llu,%llu,%llu], [%llu,%llu,%llu], [%llu,%llu,%llu], [%llu,%llu,%llu]\n",
			task->pid,
			task->pgd_alloc_count, task->pgd_free_count, task->pgd_set_count,
			task->pud_alloc_count, task->pud_free_count, task->pud_set_count,
			task->pmd_alloc_count, task->pmd_free_count, task->pmd_set_count,
			task->pte_alloc_count, task->pte_free_count, task->pte_set_count);
	}
	rcu_read_unlock();
	return 0;
}

/* Open function for the proc file using the seq_file interface */
static int pg_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, pg_stats_show, NULL);
}

#ifdef CONFIG_PROC_FS
/* Use the newer proc_ops interface if available */
static const struct proc_ops pg_stats_proc_ops = {
	.proc_open    = pg_stats_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};
#endif

/* Module initialization: create the /proc/pg_stats entry */
static int __init pg_stats_init(void)
{
	struct proc_dir_entry *entry;

#ifdef CONFIG_PROC_FS
	entry = proc_create("pg_stats", 0, NULL, &pg_stats_proc_ops);
	if (!entry) {
		pr_err("Failed to create /proc/pg_stats\n");
		return -ENOMEM;
	}
#endif
	pr_info("/proc/pg_stats created\n");
	return 0;
}

/* Module exit: remove the proc entry */
static void __exit pg_stats_exit(void)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry("pg_stats", NULL);
#endif
	pr_info("/proc/pg_stats removed\n");
}

module_init(pg_stats_init);
module_exit(pg_stats_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Module to show page table operation statistics");
