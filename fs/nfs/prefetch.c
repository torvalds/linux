#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/in6.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/sysctl.h>

#include "internal.h"
#include "iostat.h"
#include "fscache.h"


static int global_var;
static int min_val = 0;
static int max_val = 5;

static struct ctl_table prefetch_child_table[] = {
{
	//.ctl_name = CTL_UNNUMBERED,
	.procname = "prefetch",
	.maxlen = sizeof(int),
	.mode = 0644,
	.data = &global_var,
	.proc_handler = &proc_dointvec_minmax,
	.extra1 = &min_val,
	.extra2 = &max_val,
},
{}
};

static struct ctl_table prefetch_parent_table[] = {
{
	//.ctl_name = CTL_KERN,
	.procname = "kernel",
	.mode = 0555,
	.child = prefetch_child_table,
},
{}
};

int prefetch_register_sysctl(void)
{
	/* register the above sysctl */
	if (!register_sysctl_table(prefetch_parent_table)) {
		printk(KERN_ALERT "Error: Failed to register sample_parent_table\n");
		return -EFAULT;
	}
	return 0;
}
