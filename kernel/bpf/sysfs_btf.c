// SPDX-License-Identifier: GPL-2.0
/*
 * Provide kernel BTF information for introspection and use by eBPF tools.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/init.h>
#include <linux/sysfs.h>

/* See scripts/link-vmlinux.sh, gen_btf() func for details */
extern char __weak _binary__btf_vmlinux_bin_start[];
extern char __weak _binary__btf_vmlinux_bin_end[];

static ssize_t
btf_vmlinux_read(struct file *file, struct kobject *kobj,
		 struct bin_attribute *bin_attr,
		 char *buf, loff_t off, size_t len)
{
	memcpy(buf, _binary__btf_vmlinux_bin_start + off, len);
	return len;
}

static struct bin_attribute bin_attr_btf_vmlinux __ro_after_init = {
	.attr = { .name = "vmlinux", .mode = 0444, },
	.read = btf_vmlinux_read,
};

static struct kobject *btf_kobj;

static int __init btf_vmlinux_init(void)
{
	if (!_binary__btf_vmlinux_bin_start)
		return 0;

	btf_kobj = kobject_create_and_add("btf", kernel_kobj);
	if (!btf_kobj)
		return -ENOMEM;

	bin_attr_btf_vmlinux.size = _binary__btf_vmlinux_bin_end -
				    _binary__btf_vmlinux_bin_start;

	return sysfs_create_bin_file(btf_kobj, &bin_attr_btf_vmlinux);
}

subsys_initcall(btf_vmlinux_init);
