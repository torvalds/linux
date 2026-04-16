// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <linux/kallsyms.h>

#define MODULE_NAME "deadline"

#include "deadline.h"

struct rv_monitor rv_deadline = {
	.name = "deadline",
	.description = "container for several deadline scheduler specifications.",
	.enable = NULL,
	.disable = NULL,
	.reset = NULL,
	.enabled = 0,
};

/* Used by other monitors */
struct sched_class *rv_ext_sched_class;

static int __init register_deadline(void)
{
	if (IS_ENABLED(CONFIG_SCHED_CLASS_EXT)) {
		rv_ext_sched_class = (void *)kallsyms_lookup_name("ext_sched_class");
		if (!rv_ext_sched_class)
			pr_warn("rv: Missing ext_sched_class, monitors may not work.\n");
	}
	return rv_register_monitor(&rv_deadline, NULL);
}

static void __exit unregister_deadline(void)
{
	rv_unregister_monitor(&rv_deadline);
}

module_init(register_deadline);
module_exit(unregister_deadline);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("deadline: container for several deadline scheduler specifications.");
