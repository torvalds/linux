// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Joe Lawrence <joe.lawrence@redhat.com>
 */

/*
 * livepatch-callbacks-mod.c - (un)patching callbacks demo support module
 *
 *
 * Purpose
 * -------
 *
 * Simple module to demonstrate livepatch (un)patching callbacks.
 *
 *
 * Usage
 * -----
 *
 * This module is not intended to be standalone.  See the "Usage"
 * section of livepatch-callbacks-demo.c.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

static int livepatch_callbacks_mod_init(void)
{
	pr_info("%s\n", __func__);
	return 0;
}

static void livepatch_callbacks_mod_exit(void)
{
	pr_info("%s\n", __func__);
}

module_init(livepatch_callbacks_mod_init);
module_exit(livepatch_callbacks_mod_exit);
MODULE_DESCRIPTION("Live patching demo for (un)patching callbacks, support module");
MODULE_LICENSE("GPL");
