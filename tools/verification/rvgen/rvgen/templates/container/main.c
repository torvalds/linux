// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>

#define MODULE_NAME "%%MODEL_NAME%%"

#include "%%MODEL_NAME%%.h"

struct rv_monitor rv_%%MODEL_NAME%%;

struct rv_monitor rv_%%MODEL_NAME%% = {
	.name = "%%MODEL_NAME%%",
	.description = "%%DESCRIPTION%%",
	.enable = NULL,
	.disable = NULL,
	.reset = NULL,
	.enabled = 0,
};

static int __init register_%%MODEL_NAME%%(void)
{
	rv_register_monitor(&rv_%%MODEL_NAME%%, NULL);
	return 0;
}

static void __exit unregister_%%MODEL_NAME%%(void)
{
	rv_unregister_monitor(&rv_%%MODEL_NAME%%);
}

module_init(register_%%MODEL_NAME%%);
module_exit(unregister_%%MODEL_NAME%%);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dot2k: auto-generated");
MODULE_DESCRIPTION("%%MODEL_NAME%%: %%DESCRIPTION%%");
