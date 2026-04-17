// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 Intel Corporation */
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include "../../../drivers/dax/bus.h"

static bool hmem_test;

static void hmem_test_work(struct work_struct *work)
{
}

static void hmem_test_release(struct device *dev)
{
	struct hmem_platform_device *hpdev =
		container_of(dev, typeof(*hpdev), pdev.dev);

	memset(hpdev, 0, sizeof(*hpdev));
}

static struct hmem_platform_device hmem_test_device = {
	.pdev = {
		.name = "hmem_platform",
		.id = 1,
		.dev = {
			.release = hmem_test_release,
		},
	},
	.work = __WORK_INITIALIZER(hmem_test_device.work, hmem_test_work),
};

int hmem_test_init(void)
{
	if (!hmem_test)
		return 0;

	return platform_device_register(&hmem_test_device.pdev);
}

void hmem_test_exit(void)
{
	if (hmem_test)
		platform_device_unregister(&hmem_test_device.pdev);
}

module_param(hmem_test, bool, 0444);
MODULE_PARM_DESC(hmem_test, "Enable/disable the dax_hmem test platform device");
