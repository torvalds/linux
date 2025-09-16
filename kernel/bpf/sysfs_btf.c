// SPDX-License-Identifier: GPL-2.0
/*
 * Provide kernel BTF information for introspection and use by eBPF tools.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/init.h>
#include <linux/sysfs.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/btf.h>

/* See scripts/link-vmlinux.sh, gen_btf() func for details */
extern char __start_BTF[];
extern char __stop_BTF[];

static int btf_sysfs_vmlinux_mmap(struct file *filp, struct kobject *kobj,
				  const struct bin_attribute *attr,
				  struct vm_area_struct *vma)
{
	unsigned long pages = PAGE_ALIGN(attr->size) >> PAGE_SHIFT;
	size_t vm_size = vma->vm_end - vma->vm_start;
	phys_addr_t addr = __pa_symbol(__start_BTF);
	unsigned long pfn = addr >> PAGE_SHIFT;

	if (attr->private != __start_BTF || !PAGE_ALIGNED(addr))
		return -EINVAL;

	if (vma->vm_pgoff)
		return -EINVAL;

	if (vma->vm_flags & (VM_WRITE | VM_EXEC | VM_MAYSHARE))
		return -EACCES;

	if (pfn + pages < pfn)
		return -EINVAL;

	if ((vm_size >> PAGE_SHIFT) > pages)
		return -EINVAL;

	vm_flags_mod(vma, VM_DONTDUMP, VM_MAYEXEC | VM_MAYWRITE);
	return remap_pfn_range(vma, vma->vm_start, pfn, vm_size, vma->vm_page_prot);
}

static struct bin_attribute bin_attr_btf_vmlinux __ro_after_init = {
	.attr = { .name = "vmlinux", .mode = 0444, },
	.read = sysfs_bin_attr_simple_read,
	.mmap = btf_sysfs_vmlinux_mmap,
};

struct kobject *btf_kobj;

static int __init btf_vmlinux_init(void)
{
	bin_attr_btf_vmlinux.private = __start_BTF;
	bin_attr_btf_vmlinux.size = __stop_BTF - __start_BTF;

	if (bin_attr_btf_vmlinux.size == 0)
		return 0;

	btf_kobj = kobject_create_and_add("btf", kernel_kobj);
	if (!btf_kobj)
		return -ENOMEM;

	return sysfs_create_bin_file(btf_kobj, &bin_attr_btf_vmlinux);
}

subsys_initcall(btf_vmlinux_init);
