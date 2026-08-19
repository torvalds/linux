// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>

#define MODULE_NAME "test_container"

#include "test_container.h"

struct rv_monitor rv_test_container = {
	.name = "test_container",
	.description = "Test container for grouping monitors",
	.enable = NULL,
	.disable = NULL,
	.reset = NULL,
	.enabled = 0,
};

static int __init register_test_container(void)
{
	return rv_register_monitor(&rv_test_container, NULL);
}

static void __exit unregister_test_container(void)
{
	rv_unregister_monitor(&rv_test_container);
}

module_init(register_test_container);
module_exit(unregister_test_container);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rvgen: auto-generated");
MODULE_DESCRIPTION("test_container: Test container for grouping monitors");
