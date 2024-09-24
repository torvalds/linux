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
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>

typedef void (*show_pad_maps_fn)	(struct seq_file *m, struct vm_area_struct *vma);
typedef int  (*show_pad_smaps_fn)	(struct seq_file *m, void *v);

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

	/*
	 * Usually to modify vm_flags we need to take exclusive mmap_lock but here
	 * only have the lock in read mode, to avoid all DONTNEED/DONTNEED_LOCKED
	 * calls needing the write lock.
	 *
	 * A race to the flags update can only happen with another MADV_DONTNEED on
	 * the same process and same range (VMA).
	 *
	 * In practice, this specific scenario is not possible because the action that
	 * could cause it is usually performed at most once per VMA and only by the
	 * dynamic linker.
	 *
	 * Forego protection for this case, to avoid penalties in the common cases.
	 */
	__vm_flags_mod(vma, 0, VM_PAD_MASK);
	__vm_flags_mod(vma, nr_pages << VM_PAD_SHIFT, 0);
}

unsigned long vma_pad_pages(struct vm_area_struct *vma)
{
	if (!is_pgsize_migration_enabled())
		return 0;

	return (vma->vm_flags & VM_PAD_MASK) >> VM_PAD_SHIFT;
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
 * The dynamic linker, or interpreter, operates within the process context
 * of the binary that necessitated dynamic linking.
 *
 * Consequently, process context identifiers; like PID, comm, ...; cannot
 * be used to differentiate whether the execution context belongs to the
 * dynamic linker or not.
 *
 * linker_ctx() deduces whether execution is currently in the dynamic linker's
 * context by correlating the current userspace instruction pointer with the
 * VMAs of the current task.
 *
 * Returns true if in linker context, otherwise false.
 *
 * Caller must hold mmap lock in read mode.
 */
static inline bool linker_ctx(void)
{
	struct pt_regs *regs = task_pt_regs(current);
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct file *file;

	if (!regs)
		return false;

	vma = find_vma(mm, instruction_pointer(regs));

	/* Current execution context, the VMA must be present */
	BUG_ON(!vma);

	file = vma->vm_file;
	if (!file)
		return false;

	if ((vma->vm_flags & VM_EXEC)) {
		char buf[64];
		const int bufsize = sizeof(buf);
		char *path;

		memset(buf, 0, bufsize);
		path = d_path(&file->f_path, buf, bufsize);

		/*
		 * Depending on interpreter requested, valid paths could be any of:
		 *   1. /system/bin/bootstrap/linker64
		 *   2. /system/bin/linker64
		 *   3. /apex/com.android.runtime/bin/linker64
		 *
		 * Check the base name (linker64).
		 */
		if (!strcmp(kbasename(path), "linker64"))
			return true;
	}

	return false;
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
 *    5) The VMA is a file backed VMA.
 *    6) The file backing the VMA is a shared library (*.so)
 *    7) The madvise was requested by bionic's dynamic linker.
 */
void madvise_vma_pad_pages(struct vm_area_struct *vma,
			   unsigned long start, unsigned long end)
{
	unsigned long nr_pad_pages;

	if (!is_pgsize_migration_enabled())
		return;

	/*
	 * If the madvise range is it at the end of the file save the number of
	 * pages in vm_flags (only need 4 bits are needed for up to 64kB aligned ELFs).
	 */
	if (start <= vma->vm_start || end != vma->vm_end)
		return;

	nr_pad_pages = (end - start) >> PAGE_SHIFT;

	if (!nr_pad_pages || nr_pad_pages > VM_TOTAL_PAD_PAGES)
		return;

	/* Only handle this for file backed VMAs */
	if (!vma->vm_file)
		return;

	/* Limit this to only shared libraries (*.so) */
	if (!str_has_suffix(vma->vm_file->f_path.dentry->d_name.name, ".so"))
		return;

	/* Only bionic's dynamic linker needs to hint padding pages. */
	if (!linker_ctx())
		return;

	vma_set_pad_pages(vma, nr_pad_pages);
}

static const char *pad_vma_name(struct vm_area_struct *vma)
{
	return "[page size compat]";
}

static const struct vm_operations_struct pad_vma_ops = {
	.name = pad_vma_name,
};

/*
 * Returns a new VMA representing the padding in @vma, if no padding
 * in @vma returns NULL.
 */
struct vm_area_struct *get_pad_vma(struct vm_area_struct *vma)
{
	struct vm_area_struct *pad;

	if (!is_pgsize_migration_enabled() || !(vma->vm_flags & VM_PAD_MASK))
		return NULL;

	pad = kzalloc(sizeof(struct vm_area_struct), GFP_KERNEL);

	memcpy(pad, vma, sizeof(struct vm_area_struct));

	/* Remove file */
	pad->vm_file = NULL;

	/* Add vm_ops->name */
	pad->vm_ops = &pad_vma_ops;

	/* Adjust the start to begin at the start of the padding section */
	pad->vm_start = VMA_PAD_START(pad);

	/*
	 * The below modifications to vm_flags don't need mmap write lock,
	 * since, pad does not belong to the VMA tree.
	 */
	/* Make the pad vma PROT_NONE */
	__vm_flags_mod(pad, 0, VM_READ|VM_WRITE|VM_EXEC);
	/* Remove padding bits */
	__vm_flags_mod(pad, 0, VM_PAD_MASK);

	return pad;
}

/*
 * Returns a new VMA exclusing the padding from @vma; if no padding in
 * @vma returns @vma.
 */
struct vm_area_struct *get_data_vma(struct vm_area_struct *vma)
{
	struct vm_area_struct *data;

	if (!is_pgsize_migration_enabled() || !(vma->vm_flags & VM_PAD_MASK))
		return vma;

	data = kzalloc(sizeof(struct vm_area_struct), GFP_KERNEL);

	memcpy(data, vma, sizeof(struct vm_area_struct));

	/* Adjust the end to the start of the padding section */
	data->vm_end = VMA_PAD_START(data);

	return data;
}

/*
 * Calls the show_pad_vma_fn on the @pad VMA, and frees the copies of @vma
 * and @pad.
 */
void show_map_pad_vma(struct vm_area_struct *vma, struct vm_area_struct *pad,
		      struct seq_file *m, void *func, bool smaps)
{
	if (!pad)
		return;

	/*
	 * This cannot happen. If @pad vma was allocated the corresponding
	 * @vma should have the VM_PAD_MASK bit(s) set.
	 */
	BUG_ON(!(vma->vm_flags & VM_PAD_MASK));

	/*
	 * This cannot happen. @pad is a section of the original VMA.
	 * Therefore @vma cannot be null if @pad is not null.
	 */
	BUG_ON(!vma);

	if (smaps)
		((show_pad_smaps_fn)func)(m, pad);
	else
		((show_pad_maps_fn)func)(m, pad);

	kfree(pad);
	kfree(vma);
}

/*
 * When splitting a padding VMA there are a couple of cases to handle.
 *
 * Given:
 *
 *     | DDDDPPPP |
 *
 * where:
 *     - D represents 1 page of data;
 *     - P represents 1 page of padding;
 *     - | represents the boundaries (start/end) of the VMA
 *
 *
 * 1) Split exactly at the padding boundary
 *
 *     | DDDDPPPP | --> | DDDD | PPPP |
 *
 *     - Remove padding flags from the first VMA.
 *     - The second VMA is all padding
 *
 * 2) Split within the padding area
 *
 *     | DDDDPPPP | --> | DDDDPP | PP |
 *
 *     - Subtract the length of the second VMA from the first VMA's padding.
 *     - The second VMA is all padding, adjust its padding length (flags)
 *
 * 3) Split within the data area
 *
 *     | DDDDPPPP | --> | DD | DDPPPP |
 *
 *     - Remove padding flags from the first VMA.
 *     - The second VMA is has the same padding as from before the split.
 */
void split_pad_vma(struct vm_area_struct *vma, struct vm_area_struct *new,
		   unsigned long addr, int new_below)
{
	unsigned long nr_pad_pages = vma_pad_pages(vma);
	unsigned long nr_vma2_pages;
	struct vm_area_struct *first;
	struct vm_area_struct *second;

	if (!nr_pad_pages)
		return;

	if (new_below) {
		first = new;
		second = vma;
	} else {
		first = vma;
		second = new;
	}

	nr_vma2_pages = vma_pages(second);

	if (nr_vma2_pages >= nr_pad_pages) { 			/* Case 1 & 3 */
		vma_set_pad_pages(first, 0);
		vma_set_pad_pages(second, nr_pad_pages);
	} else {						/* Case 2 */
		vma_set_pad_pages(first, nr_pad_pages - nr_vma2_pages);
		vma_set_pad_pages(second, nr_vma2_pages);
	}
}

/*
 * Merging of padding VMAs is uncommon, as padding is only allowed
 * from the linker context.
 *
 * To simplify the semantics, adjacent VMAs with padding are not
 * allowed to merge.
 */
bool is_mergable_pad_vma(struct vm_area_struct *vma,
			 unsigned long vm_flags)
{
	/* Padding VMAs cannot be merged with other padding or real VMAs */
	return !((vma->vm_flags | vm_flags) & VM_PAD_MASK);
}

unsigned long vma_data_pages(struct vm_area_struct *vma)
{
	return vma_pages(vma) - vma_pad_pages(vma);
}

#endif /* PAGE_SIZE == SZ_4K */
#endif /* CONFIG_64BIT */
