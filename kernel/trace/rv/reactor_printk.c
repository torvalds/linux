// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
 *
 * Printk RV reactor:
 *   Prints the exception msg to the kernel message log.
 */
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>

__printf(1, 2) static void rv_printk_reaction(const char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	vprintk_deferred(msg, args);
	va_end(args);
}

static struct rv_reactor rv_printk = {
	.name = "printk",
	.description = "prints the exception msg to the kernel message log.",
	.react = rv_printk_reaction
};

static int __init register_react_printk(void)
{
	rv_register_reactor(&rv_printk);
	return 0;
}

static void __exit unregister_react_printk(void)
{
	rv_unregister_reactor(&rv_printk);
}

module_init(register_react_printk);
module_exit(unregister_react_printk);

MODULE_AUTHOR("Daniel Bristot de Oliveira");
MODULE_DESCRIPTION("printk rv reactor: printk if an exception is hit.");
