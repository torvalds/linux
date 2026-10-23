// SPDX-License-Identifier: GPL-2.0
/*
 * Provide kernel BTF information for introspection and use by eBPF tools.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/btf.h>

/* See scripts/link-vmlinux.sh, gen_btf() func for details */
extern char __start_BTF[];
extern char __stop_BTF[];

static int btf_sysfs_mmap_check(void *data, size_t size, struct vm_area_struct *vma)
{
	unsigned long pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	size_t vm_size = vma->vm_end - vma->vm_start;

	if (!data || !PAGE_ALIGNED((unsigned long)data))
		return -EINVAL;

	if (vma->vm_pgoff)
		return -EINVAL;

	if (vma->vm_flags & (VM_WRITE | VM_EXEC | VM_MAYSHARE))
		return -EACCES;

	if ((vm_size >> PAGE_SHIFT) > pages)
		return -EINVAL;

	vm_flags_mod(vma, VM_DONTDUMP, VM_MAYEXEC | VM_MAYWRITE);
	return 0;
}

static int btf_sysfs_mmap_image(struct file *filp, struct kobject *kobj,
				const struct bin_attribute *attr,
				struct vm_area_struct *vma)
{
	void *data = READ_ONCE(attr->private);
	phys_addr_t addr;
	unsigned long pfn;
	size_t vm_size = vma->vm_end - vma->vm_start;
	int err;

	err = btf_sysfs_mmap_check(data, attr->size, vma);
	if (err)
		return err;

	/* Kernel image addresses can lie inside the vmalloc range on arm64. */
	addr = __pa_symbol(data);
	pfn = addr >> PAGE_SHIFT;
	if (pfn + (PAGE_ALIGN(attr->size) >> PAGE_SHIFT) < pfn)
		return -EINVAL;

	return remap_pfn_range(vma, vma->vm_start, pfn, vm_size, vma->vm_page_prot);
}

static struct bin_attribute bin_attr_btf_vmlinux __ro_after_init = {
	.attr = { .name = "vmlinux", .mode = 0444, },
	.read = sysfs_bin_attr_simple_read,
	.mmap = btf_sysfs_mmap_image,
};

struct kobject *btf_kobj;

struct btf_sysfs_entry {
	struct bin_attribute attr;
	char *module_name;
};

static void *btf_sysfs_lazy_data(const struct bin_attribute *attr)
{
	struct btf_sysfs_entry *entry = container_of(attr, struct btf_sysfs_entry, attr);
	void *data;

	data = smp_load_acquire(&attr->private);
	if (!data) {
		request_module("%s", entry->module_name);
		data = smp_load_acquire(&attr->private);
	}
	return data;
}

static ssize_t btf_sysfs_read_lazy(struct file *filp, struct kobject *kobj,
				   const struct bin_attribute *attr, char *buf,
				   loff_t off, size_t count)
{
	void *data = btf_sysfs_lazy_data(attr);

	if (!data)
		return -ENODEV;

	return memory_read_from_buffer(buf, count, &off, data, attr->size);
}

static int btf_sysfs_mmap_lazy(struct file *filp, struct kobject *kobj,
			       const struct bin_attribute *attr,
			       struct vm_area_struct *vma)
{
	void *data = btf_sysfs_lazy_data(attr);
	int err;

	if (!data)
		return -ENODEV;

	err = btf_sysfs_mmap_check(data, attr->size, vma);
	if (err)
		return err;

	return remap_vmalloc_range(vma, data, 0);
}

struct bin_attribute *sysfs_btf_add(const char *name, void *data, size_t data_size,
				    bool mmap, const char *lazy_module_name)
{
	struct btf_sysfs_entry *entry;
	struct bin_attribute *attr;
	int err;

	entry = kzalloc_obj(*entry);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	attr = &entry->attr;
	sysfs_bin_attr_init(attr);
	attr->attr.mode = 0444;
	attr->size = data_size;
	attr->private = data;
	attr->read = lazy_module_name ? btf_sysfs_read_lazy : sysfs_bin_attr_simple_read;
	if (mmap)
		attr->mmap = lazy_module_name ? btf_sysfs_mmap_lazy : btf_sysfs_mmap_image;
	attr->attr.name = kstrdup(name, GFP_KERNEL);
	if (!attr->attr.name) {
		err = -ENOMEM;
		goto err_free;
	}
	if (lazy_module_name) {
		entry->module_name = kstrdup(lazy_module_name, GFP_KERNEL);
		if (!entry->module_name) {
			err = -ENOMEM;
			goto err_free;
		}
	}

	err = sysfs_create_bin_file(btf_kobj, attr);
	if (err) {
		pr_warn("failed to register [%s] BTF in sysfs: %d\n", name, err);
		goto err_free;
	}

	return attr;

err_free:
	kfree(entry->module_name);
	kfree(attr->attr.name);
	kfree(entry);
	return ERR_PTR(err);
}

bool sysfs_btf_update(struct bin_attribute *attr, void *data)
{
	return attr && !cmpxchg(&attr->private, NULL, data);
}

void sysfs_btf_remove(struct bin_attribute *attr)
{
	struct btf_sysfs_entry *entry = container_of(attr, struct btf_sysfs_entry, attr);

	sysfs_remove_bin_file(btf_kobj, attr);
	kfree(entry->module_name);
	kfree(attr->attr.name);
	kfree(entry);
}

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
