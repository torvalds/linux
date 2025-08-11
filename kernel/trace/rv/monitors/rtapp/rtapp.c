// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>

#define MODULE_NAME "rtapp"

#include "rtapp.h"

struct rv_monitor rv_rtapp;

struct rv_monitor rv_rtapp = {
	.name = "rtapp",
	.description = "Collection of monitors for detecting problems with real-time applications",
};

static int __init register_rtapp(void)
{
	return rv_register_monitor(&rv_rtapp, NULL);
}

static void __exit unregister_rtapp(void)
{
	rv_unregister_monitor(&rv_rtapp);
}

module_init(register_rtapp);
module_exit(unregister_rtapp);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nam Cao <namcao@linutronix.de>");
MODULE_DESCRIPTION("Collection of monitors for detecting problems with real-time applications");
