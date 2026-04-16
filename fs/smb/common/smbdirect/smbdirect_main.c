// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#include "smbdirect_internal.h"
#include <linux/module.h>

struct smbdirect_module_state smbdirect_globals = {
	.mutex = __MUTEX_INITIALIZER(smbdirect_globals.mutex),
};

static __init int smbdirect_module_init(void)
{
	int ret = -ENOMEM;

	pr_notice("subsystem loading...\n");
	mutex_lock(&smbdirect_globals.mutex);

	smbdirect_globals.workqueues.accept = alloc_workqueue("smbdirect-accept",
							      WQ_SYSFS |
							      WQ_PERCPU |
							      WQ_POWER_EFFICIENT,
							      0);
	if (smbdirect_globals.workqueues.accept == NULL)
		goto alloc_accept_wq_failed;

	smbdirect_globals.workqueues.connect = alloc_workqueue("smbdirect-connect",
							       WQ_SYSFS |
							       WQ_PERCPU |
							       WQ_POWER_EFFICIENT,
							       0);
	if (smbdirect_globals.workqueues.connect == NULL)
		goto alloc_connect_wq_failed;

	smbdirect_globals.workqueues.idle = alloc_workqueue("smbdirect-idle",
							    WQ_SYSFS |
							    WQ_PERCPU |
							    WQ_POWER_EFFICIENT,
							    0);
	if (smbdirect_globals.workqueues.idle == NULL)
		goto alloc_idle_wq_failed;

	smbdirect_globals.workqueues.refill = alloc_workqueue("smbdirect-refill",
							      WQ_HIGHPRI |
							      WQ_SYSFS |
							      WQ_PERCPU |
							      WQ_POWER_EFFICIENT,
							      0);
	if (smbdirect_globals.workqueues.refill == NULL)
		goto alloc_refill_wq_failed;

	smbdirect_globals.workqueues.immediate = alloc_workqueue("smbdirect-immediate",
								 WQ_HIGHPRI |
								 WQ_SYSFS |
								 WQ_PERCPU |
								 WQ_POWER_EFFICIENT,
								 0);
	if (smbdirect_globals.workqueues.immediate == NULL)
		goto alloc_immediate_wq_failed;

	smbdirect_globals.workqueues.cleanup = alloc_workqueue("smbdirect-cleanup",
							       WQ_MEM_RECLAIM |
							       WQ_HIGHPRI |
							       WQ_SYSFS |
							       WQ_PERCPU |
							       WQ_POWER_EFFICIENT,
							       0);
	if (smbdirect_globals.workqueues.cleanup == NULL)
		goto alloc_cleanup_wq_failed;

	ret = smbdirect_devices_init();
	if (ret)
		goto devices_init_failed;

	mutex_unlock(&smbdirect_globals.mutex);
	pr_notice("subsystem loaded\n");
	return 0;

devices_init_failed:
	destroy_workqueue(smbdirect_globals.workqueues.cleanup);
alloc_cleanup_wq_failed:
	destroy_workqueue(smbdirect_globals.workqueues.immediate);
alloc_immediate_wq_failed:
	destroy_workqueue(smbdirect_globals.workqueues.refill);
alloc_refill_wq_failed:
	destroy_workqueue(smbdirect_globals.workqueues.idle);
alloc_idle_wq_failed:
	destroy_workqueue(smbdirect_globals.workqueues.connect);
alloc_connect_wq_failed:
	destroy_workqueue(smbdirect_globals.workqueues.accept);
alloc_accept_wq_failed:
	mutex_unlock(&smbdirect_globals.mutex);
	pr_crit("failed to loaded: %d (%1pe)\n",
		ret, SMBDIRECT_DEBUG_ERR_PTR(ret));
	return ret;
}

static __exit void smbdirect_module_exit(void)
{
	pr_notice("subsystem unloading...\n");
	mutex_lock(&smbdirect_globals.mutex);

	smbdirect_devices_exit();

	destroy_workqueue(smbdirect_globals.workqueues.accept);
	destroy_workqueue(smbdirect_globals.workqueues.connect);
	destroy_workqueue(smbdirect_globals.workqueues.idle);
	destroy_workqueue(smbdirect_globals.workqueues.refill);
	destroy_workqueue(smbdirect_globals.workqueues.immediate);
	destroy_workqueue(smbdirect_globals.workqueues.cleanup);

	mutex_unlock(&smbdirect_globals.mutex);
	pr_notice("subsystem unloaded\n");
}

module_init(smbdirect_module_init);
module_exit(smbdirect_module_exit);

MODULE_DESCRIPTION("smbdirect subsystem");
MODULE_LICENSE("GPL");
