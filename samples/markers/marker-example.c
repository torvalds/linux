/* marker-example.c
 *
 * Executes a marker when /proc/marker-example is opened.
 *
 * (C) Copyright 2007 Mathieu Desnoyers <mathieu.desnoyers@polymtl.ca>
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#include <linux/module.h>
#include <linux/marker.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>

struct proc_dir_entry *pentry_example;

static int my_open(struct inode *inode, struct file *file)
{
	int i;

	trace_mark(subsystem_event, "%d %s", 123, "example string");
	for (i = 0; i < 10; i++)
		trace_mark(subsystem_eventb, MARK_NOARGS);
	return -EPERM;
}

static struct file_operations mark_ops = {
	.open = my_open,
};

static int example_init(void)
{
	printk(KERN_ALERT "example init\n");
	pentry_example = create_proc_entry("marker-example", 0444, NULL);
	if (pentry_example)
		pentry_example->proc_fops = &mark_ops;
	else
		return -EPERM;
	return 0;
}

static void example_exit(void)
{
	printk(KERN_ALERT "example exit\n");
	remove_proc_entry("marker-example", NULL);
}

module_init(example_init)
module_exit(example_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Marker example");
