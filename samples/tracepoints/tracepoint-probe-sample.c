/*
 * tracepoint-probe-sample.c
 *
 * sample tracepoint probes.
 */

#include <linux/module.h>
#include <linux/file.h>
#include <linux/dcache.h>
#include "tp-samples-trace.h"

/*
 * Here the caller only guarantees locking for struct file and struct inode.
 * Locking must therefore be done in the probe to use the dentry.
 */
static void probe_subsys_event(void *ignore,
			       struct inode *inode, struct file *file)
{
	path_get(&file->f_path);
	dget(file->f_path.dentry);
	printk(KERN_INFO "Event is encountered with filename %s\n",
		file->f_path.dentry->d_name.name);
	dput(file->f_path.dentry);
	path_put(&file->f_path);
}

static void probe_subsys_eventb(void *ignore)
{
	printk(KERN_INFO "Event B is encountered\n");
}

static int __init tp_sample_trace_init(void)
{
	int ret;

	ret = register_trace_subsys_event(probe_subsys_event, NULL);
	WARN_ON(ret);
	ret = register_trace_subsys_eventb(probe_subsys_eventb, NULL);
	WARN_ON(ret);

	return 0;
}

module_init(tp_sample_trace_init);

static void __exit tp_sample_trace_exit(void)
{
	unregister_trace_subsys_eventb(probe_subsys_eventb, NULL);
	unregister_trace_subsys_event(probe_subsys_event, NULL);
	tracepoint_synchronize_unregister();
}

module_exit(tp_sample_trace_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Tracepoint Probes Samples");
