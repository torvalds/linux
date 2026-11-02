// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026, Oracle and/or its affiliates. */
/*
 * Provide kernel BTF inline function information for use by BPF tools.
 */
#include <linux/btf.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#if IS_BUILTIN(CONFIG_DEBUG_INFO_BTF_INLINE)
extern char __start_BTF_inline[];
extern char __stop_BTF_inline[];
#endif

static int __init btf_vmlinux_inline_init(void)
{
#if IS_BUILTIN(CONFIG_DEBUG_INFO_BTF_INLINE)
	size_t data_size = __stop_BTF_inline - __start_BTF_inline;

	if (data_size)
		sysfs_btf_add("vmlinux.inline", __start_BTF_inline, data_size, true, NULL);
#endif
	return 0;
}
subsys_initcall(btf_vmlinux_inline_init);

MODULE_DESCRIPTION("BTF inline information for vmlinux");
MODULE_LICENSE("GPL");
