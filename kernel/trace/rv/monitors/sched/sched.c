// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>

#define MODULE_NAME "sched"

#include "sched.h"

struct rv_monitor rv_sched;

struct rv_monitor rv_sched = {
	.name = "sched",
	.description = "container for several scheduler monitor specifications.",
	.enable = NULL,
	.disable = NULL,
	.reset = NULL,
	.enabled = 0,
};

static int __init register_sched(void)
{
	return rv_register_monitor(&rv_sched, NULL);
}

static void __exit unregister_sched(void)
{
	rv_unregister_monitor(&rv_sched);
}

module_init(register_sched);
module_exit(unregister_sched);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("sched: container for several scheduler monitor specifications.");
