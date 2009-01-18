/* tracepoint-sample.c
 *
 * Executes a tracepoint when /proc/tracepoint-example is opened.
 *
 * (C) Copyright 2007 Mathieu Desnoyers <mathieu.desnoyers@polymtl.ca>
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include "tp-samples-trace.h"

DEFINE_TRACE(subsys_event);
DEFINE_TRACE(subsys_eventb);

struct proc_dir_entry *pentry_example;

static int my_open(struct inode *inode, struct file *file)
{
	int i;

	trace_subsys_event(inode, file);
	for (i = 0; i < 10; i++)
		trace_subsys_eventb();
	return -EPERM;
}

static struct file_operations mark_ops = {
	.open = my_open,
};

static int __init example_init(void)
{
	printk(KERN_ALERT "example init\n");
	pentry_example = proc_create("tracepoint-example", 0444, NULL,
		&mark_ops);
	if (!pentry_example)
		return -EPERM;
	return 0;
}

static void __exit example_exit(void)
{
	printk(KERN_ALERT "example exit\n");
	remove_proc_entry("tracepoint-example", NULL);
}

module_init(example_init)
module_exit(example_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Tracepoint example");
