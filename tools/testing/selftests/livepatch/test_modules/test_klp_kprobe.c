// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Marcos Paulo de Souza <mpdesouza@suse.com>
// Copyright (C) 2024 Michael Vetter <mvetter@suse.com>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>

static bool has_post_handler = true;
module_param(has_post_handler, bool, 0444);

static void __kprobes post_handler(struct kprobe *p, struct pt_regs *regs,
				unsigned long flags)
{
}

static struct kprobe kp = {
	.symbol_name = "cmdline_proc_show",
};

static int __init kprobe_init(void)
{
	if (has_post_handler)
		kp.post_handler = post_handler;

	return register_kprobe(&kp);
}

static void __exit kprobe_exit(void)
{
	unregister_kprobe(&kp);
}

module_init(kprobe_init)
module_exit(kprobe_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Vetter <mvetter@suse.com>");
MODULE_DESCRIPTION("Livepatch test: kprobe function");
