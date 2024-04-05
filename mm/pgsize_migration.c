// SPDX-License-Identifier: GPL-2.0
/*
 * Page Size Migration
 *
 * This file contains the core logic of mitigations to ensure
 * app compatibility during the transition from 4kB to 16kB
 * page size in Android.
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 */

#include <linux/pgsize_migration.h>

#include <linux/init.h>
#include <linux/jump_label.h>
#include <linux/kobject.h>
#include <linux/kstrtox.h>
#include <linux/sysfs.h>

#ifdef CONFIG_64BIT
#if PAGE_SIZE == SZ_4K
DEFINE_STATIC_KEY_TRUE(pgsize_migration_enabled);

#define is_pgsize_migration_enabled() 	(static_branch_likely(&pgsize_migration_enabled))
#else /* PAGE_SIZE != SZ_4K */
DEFINE_STATIC_KEY_FALSE(pgsize_migration_enabled);

#define is_pgsize_migration_enabled() 	(static_branch_unlikely(&pgsize_migration_enabled))
#endif /* PAGE_SIZE == SZ_4K */

static ssize_t show_pgsize_migration_enabled(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	if (is_pgsize_migration_enabled())
		return sprintf(buf, "%d\n", 1);
	else
		return sprintf(buf, "%d\n", 0);
}

static ssize_t store_pgsize_migration_enabled(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf, size_t n)
{
	unsigned long val;

	/* Migration is only applicable to 4kB kernels */
	if (PAGE_SIZE != SZ_4K)
		return n;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val > 1)
		return -EINVAL;

	if (val == 1)
		static_branch_enable(&pgsize_migration_enabled);
	else if (val == 0)
		static_branch_disable(&pgsize_migration_enabled);

	return n;
}

static struct kobj_attribute pgsize_migration_enabled_attr = __ATTR(
	enabled,
	0644,
	show_pgsize_migration_enabled,
	store_pgsize_migration_enabled
);

static struct attribute *pgsize_migration_attrs[] = {
	&pgsize_migration_enabled_attr.attr,
	NULL
};

static struct attribute_group pgsize_migration_attr_group = {
	.name = "pgsize_migration",
	.attrs = pgsize_migration_attrs,
};

/**
 * What:          /sys/kernel/mm/pgsize_migration/enabled
 * Date:          April 2024
 * KernelVersion: v5.4+ (GKI kernels)
 * Contact:       Kalesh Singh <kaleshsingh@google.com>
 * Description:   /sys/kernel/mm/pgsize_migration/enabled
 *                allows for userspace to turn on or off page size
 *                migration mitigations necessary for app compatibility
 *                during Android's transition from 4kB to 16kB page size.
 *                Such mitigations include preserving /proc/<pid>/[s]maps
 *                output as if there was no segment extension by the
 *                dynamic loader; and preventing fault around in the padding
 *                sections of ELF LOAD segment mappings.
 * Users:         Bionic's dynamic linker
 */
static int __init init_pgsize_migration(void)
{
	if (sysfs_create_group(mm_kobj, &pgsize_migration_attr_group))
		pr_err("pgsize_migration: failed to create sysfs group\n");

	return 0;
};
late_initcall(init_pgsize_migration);

#if PAGE_SIZE == SZ_4K
void vma_set_pad_pages(struct vm_area_struct *vma,
		       unsigned long nr_pages)
{
	if (!is_pgsize_migration_enabled())
		return;

	vm_flags_set(vma, nr_pages << VM_PAD_SHIFT);
}

unsigned long vma_pad_pages(struct vm_area_struct *vma)
{
	if (!is_pgsize_migration_enabled())
		return 0;

	return vma->vm_flags >> VM_PAD_SHIFT;
}

static __always_inline bool str_has_suffix(const char *str, const char *suffix)
{
	size_t str_len = strlen(str);
	size_t suffix_len = strlen(suffix);

	if (str_len < suffix_len)
		return false;

	return !strncmp(str + str_len - suffix_len, suffix, suffix_len);
}

/*
 * Saves the number of padding pages for an ELF segment mapping
 * in vm_flags.
 *
 * The number of padding pages is deduced from the madvise DONTNEED range [start, end)
 * if the following conditions are met:
 *    1) The range is enclosed by a single VMA
 *    2) The range ends at the end address of the VMA
 *    3) The range starts at an address greater than the start address of the VMA
 *    4) The number of the pages in the range does not exceed VM_TOTAL_PAD_PAGES.
 *    5) The VMA is a regular file backed VMA (filemap_fault)
 *    6) The file backing the VMA is a shared library (*.so)
 */
void madvise_vma_pad_pages(struct vm_area_struct *vma,
			   unsigned long start, unsigned long end)
{
	unsigned long nr_pad_pages;

	if (!is_pgsize_migration_enabled())
		return;

	/* Only handle this for file backed VMAs */
	if (!vma->vm_file || !vma->vm_ops || vma->vm_ops->fault != filemap_fault)
		return;


	/* Limit this to only shared libraries (*.so) */
	if (!str_has_suffix(vma->vm_file->f_path.dentry->d_name.name, ".so"))
		return;

	/*
	 * If the madvise range is it at the end of the file save the number of
	 * pages in vm_flags (only need 4 bits are needed for 16kB aligned ELFs).
	 */
	if (start <= vma->vm_start || end != vma->vm_end)
		return;

	nr_pad_pages = (end - start) >> PAGE_SHIFT;

	if (!nr_pad_pages || nr_pad_pages > VM_TOTAL_PAD_PAGES)
		return;

	vma_set_pad_pages(vma, nr_pad_pages);
}
#endif /* PAGE_SIZE == SZ_4K */
#endif /* CONFIG_64BIT */
