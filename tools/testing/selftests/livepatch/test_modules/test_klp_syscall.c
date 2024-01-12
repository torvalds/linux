// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017-2023 SUSE
 * Authors: Libor Pechacek <lpechacek@suse.cz>
 *          Nicolai Stange <nstange@suse.de>
 *          Marcos Paulo de Souza <mpdesouza@suse.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/livepatch.h>

#if defined(__x86_64__)
#define FN_PREFIX __x64_
#elif defined(__s390x__)
#define FN_PREFIX __s390x_
#elif defined(__aarch64__)
#define FN_PREFIX __arm64_
#else
/* powerpc does not select ARCH_HAS_SYSCALL_WRAPPER */
#define FN_PREFIX
#endif

/* Protects klp_pids */
static DEFINE_MUTEX(kpid_mutex);

static unsigned int npids, npids_pending;
static int klp_pids[NR_CPUS];
module_param_array(klp_pids, int, &npids_pending, 0);
MODULE_PARM_DESC(klp_pids, "Array of pids to be transitioned to livepatched state.");

static ssize_t npids_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	return sprintf(buf, "%u\n", npids_pending);
}

static struct kobj_attribute klp_attr = __ATTR_RO(npids);
static struct kobject *klp_kobj;

static asmlinkage long lp_sys_getpid(void)
{
	int i;

	mutex_lock(&kpid_mutex);
	if (npids_pending > 0) {
		for (i = 0; i < npids; i++) {
			if (current->pid == klp_pids[i]) {
				klp_pids[i] = 0;
				npids_pending--;
				break;
			}
		}
	}
	mutex_unlock(&kpid_mutex);

	return task_tgid_vnr(current);
}

static struct klp_func vmlinux_funcs[] = {
	{
		.old_name = __stringify(FN_PREFIX) "sys_getpid",
		.new_func = lp_sys_getpid,
	}, {}
};

static struct klp_object objs[] = {
	{
		/* name being NULL means vmlinux */
		.funcs = vmlinux_funcs,
	}, {}
};

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

static int livepatch_init(void)
{
	int ret;

	klp_kobj = kobject_create_and_add("test_klp_syscall", kernel_kobj);
	if (!klp_kobj)
		return -ENOMEM;

	ret = sysfs_create_file(klp_kobj, &klp_attr.attr);
	if (ret) {
		kobject_put(klp_kobj);
		return ret;
	}

	/*
	 * Save the number pids to transition to livepatched state before the
	 * number of pending pids is decremented.
	 */
	npids = npids_pending;

	return klp_enable_patch(&patch);
}

static void livepatch_exit(void)
{
	kobject_put(klp_kobj);
}

module_init(livepatch_init);
module_exit(livepatch_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
MODULE_AUTHOR("Libor Pechacek <lpechacek@suse.cz>");
MODULE_AUTHOR("Nicolai Stange <nstange@suse.de>");
MODULE_AUTHOR("Marcos Paulo de Souza <mpdesouza@suse.com>");
MODULE_DESCRIPTION("Livepatch test: syscall transition");
