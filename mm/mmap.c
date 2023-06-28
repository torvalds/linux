// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/mmap.c
 *
 * Written by obz.
 *
 * Address space accounting code	<alan@lxorguk.ukuu.org.uk>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/capability.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/hugetlb.h>
#include <linux/shmem_fs.h>
#include <linux/profile.h>
#include <linux/export.h>
#include <linux/mount.h>
#include <linux/mempolicy.h>
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <linux/mmdebug.h>
#include <linux/perf_event.h>
#include <linux/audit.h>
#include <linux/khugepaged.h>
#include <linux/uprobes.h>
#include <linux/notifier.h>
#include <linux/memory.h>
#include <linux/printk.h>
#include <linux/userfaultfd_k.h>
#include <linux/moduleparam.h>
#include <linux/pkeys.h>
#include <linux/oom.h>
#include <linux/sched/mm.h>

#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>

#define CREATE_TRACE_POINTS
#include <trace/events/mmap.h>

#include "internal.h"

#ifndef arch_mmap_check
#define arch_mmap_check(addr, len, flags)	(0)
#endif

#ifdef CONFIG_HAVE_ARCH_MMAP_RND_BITS
const int mmap_rnd_bits_min = CONFIG_ARCH_MMAP_RND_BITS_MIN;
const int mmap_rnd_bits_max = CONFIG_ARCH_MMAP_RND_BITS_MAX;
int mmap_rnd_bits __read_mostly = CONFIG_ARCH_MMAP_RND_BITS;
#endif
#ifdef CONFIG_HAVE_ARCH_MMAP_RND_COMPAT_BITS
const int mmap_rnd_compat_bits_min = CONFIG_ARCH_MMAP_RND_COMPAT_BITS_MIN;
const int mmap_rnd_compat_bits_max = CONFIG_ARCH_MMAP_RND_COMPAT_BITS_MAX;
int mmap_rnd_compat_bits __read_mostly = CONFIG_ARCH_MMAP_RND_COMPAT_BITS;
#endif

static bool ignore_rlimit_data;
core_param(ignore_rlimit_data, ignore_rlimit_data, bool, 0644);

static void unmap_region(struct mm_struct *mm, struct maple_tree *mt,
		struct vm_area_struct *vma, struct vm_area_struct *prev,
		struct vm_area_struct *next, unsigned long start,
		unsigned long end);

static pgprot_t vm_pgprot_modify(pgprot_t oldprot, unsigned long vm_flags)
{
	return pgprot_modify(oldprot, vm_get_page_prot(vm_flags));
}

/* Update vma->vm_page_prot to reflect vma->vm_flags. */
void vma_set_page_prot(struct vm_area_struct *vma)
{
	unsigned long vm_flags = vma->vm_flags;
	pgprot_t vm_page_prot;

	vm_page_prot = vm_pgprot_modify(vma->vm_page_prot, vm_flags);
	if (vma_wants_writenotify(vma, vm_page_prot)) {
		vm_flags &= ~VM_SHARED;
		vm_page_prot = vm_pgprot_modify(vm_page_prot, vm_flags);
	}
	/* remove_protection_ptes reads vma->vm_page_prot without mmap_lock */
	WRITE_ONCE(vma->vm_page_prot, vm_page_prot);
}

/*
 * Requires inode->i_mapping->i_mmap_rwsem
 */
static void __remove_shared_vm_struct(struct vm_area_struct *vma,
		struct file *file, struct address_space *mapping)
{
	if (vma->vm_flags & VM_SHARED)
		mapping_unmap_writable(mapping);

	flush_dcache_mmap_lock(mapping);
	vma_interval_tree_remove(vma, &mapping->i_mmap);
	flush_dcache_mmap_unlock(mapping);
}

/*
 * Unlink a file-based vm structure from its interval tree, to hide
 * vma from rmap and vmtruncate before freeing its page tables.
 */
void unlink_file_vma(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;

	if (file) {
		struct address_space *mapping = file->f_mapping;
		i_mmap_lock_write(mapping);
		__remove_shared_vm_struct(vma, file, mapping);
		i_mmap_unlock_write(mapping);
	}
}

/*
 * Close a vm structure and free it.
 */
static void remove_vma(struct vm_area_struct *vma)
{
	might_sleep();
	if (vma->vm_ops && vma->vm_ops->close)
		vma->vm_ops->close(vma);
	if (vma->vm_file)
		fput(vma->vm_file);
	mpol_put(vma_policy(vma));
	vm_area_free(vma);
}

/*
 * check_brk_limits() - Use platform specific check of range & verify mlock
 * limits.
 * @addr: The address to check
 * @len: The size of increase.
 *
 * Return: 0 on success.
 */
static int check_brk_limits(unsigned long addr, unsigned long len)
{
	unsigned long mapped_addr;

	mapped_addr = get_unmapped_area(NULL, addr, len, 0, MAP_FIXED);
	if (IS_ERR_VALUE(mapped_addr))
		return mapped_addr;

	return mlock_future_check(current->mm, current->mm->def_flags, len);
}
static int do_brk_munmap(struct ma_state *mas, struct vm_area_struct *vma,
			 unsigned long newbrk, unsigned long oldbrk,
			 struct list_head *uf);
static int do_brk_flags(struct ma_state *mas, struct vm_area_struct *brkvma,
		unsigned long addr, unsigned long request, unsigned long flags);
SYSCALL_DEFINE1(brk, unsigned long, brk)
{
	unsigned long newbrk, oldbrk, origbrk;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *brkvma, *next = NULL;
	unsigned long min_brk;
	bool populate;
	bool downgraded = false;
	LIST_HEAD(uf);
	MA_STATE(mas, &mm->mm_mt, 0, 0);

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	origbrk = mm->brk;

#ifdef CONFIG_COMPAT_BRK
	/*
	 * CONFIG_COMPAT_BRK can still be overridden by setting
	 * randomize_va_space to 2, which will still cause mm->start_brk
	 * to be arbitrarily shifted
	 */
	if (current->brk_randomized)
		min_brk = mm->start_brk;
	else
		min_brk = mm->end_data;
#else
	min_brk = mm->start_brk;
#endif
	if (brk < min_brk)
		goto out;

	/*
	 * Check against rlimit here. If this check is done later after the test
	 * of oldbrk with newbrk then it can escape the test and let the data
	 * segment grow beyond its set limit the in case where the limit is
	 * not page aligned -Ram Gupta
	 */
	if (check_data_rlimit(rlimit(RLIMIT_DATA), brk, mm->start_brk,
			      mm->end_data, mm->start_data))
		goto out;

	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(mm->brk);
	if (oldbrk == newbrk) {
		mm->brk = brk;
		goto success;
	}

	/*
	 * Always allow shrinking brk.
	 * do_brk_munmap() may downgrade mmap_lock to read.
	 */
	if (brk <= mm->brk) {
		int ret;

		/* Search one past newbrk */
		mas_set(&mas, newbrk);
		brkvma = mas_find(&mas, oldbrk);
		if (!brkvma || brkvma->vm_start >= oldbrk)
			goto out; /* mapping intersects with an existing non-brk vma. */
		/*
		 * mm->brk must be protected by write mmap_lock.
		 * do_brk_munmap() may downgrade the lock,  so update it
		 * before calling do_brk_munmap().
		 */
		mm->brk = brk;
		ret = do_brk_munmap(&mas, brkvma, newbrk, oldbrk, &uf);
		if (ret == 1)  {
			downgraded = true;
			goto success;
		} else if (!ret)
			goto success;

		mm->brk = origbrk;
		goto out;
	}

	if (check_brk_limits(oldbrk, newbrk - oldbrk))
		goto out;

	/*
	 * Only check if the next VMA is within the stack_guard_gap of the
	 * expansion area
	 */
	mas_set(&mas, oldbrk);
	next = mas_find(&mas, newbrk - 1 + PAGE_SIZE + stack_guard_gap);
	if (next && newbrk + PAGE_SIZE > vm_start_gap(next))
		goto out;

	brkvma = mas_prev(&mas, mm->start_brk);
	/* Ok, looks good - let it rip. */
	if (do_brk_flags(&mas, brkvma, oldbrk, newbrk - oldbrk, 0) < 0)
		goto out;

	mm->brk = brk;

success:
	populate = newbrk > oldbrk && (mm->def_flags & VM_LOCKED) != 0;
	if (downgraded)
		mmap_read_unlock(mm);
	else
		mmap_write_unlock(mm);
	userfaultfd_unmap_complete(mm, &uf);
	if (populate)
		mm_populate(oldbrk, newbrk - oldbrk);
	return brk;

out:
	mmap_write_unlock(mm);
	return origbrk;
}

#if defined(CONFIG_DEBUG_VM_MAPLE_TREE)
extern void mt_validate(struct maple_tree *mt);
extern void mt_dump(const struct maple_tree *mt);

/* Validate the maple tree */
static void validate_mm_mt(struct mm_struct *mm)
{
	struct maple_tree *mt = &mm->mm_mt;
	struct vm_area_struct *vma_mt;

	MA_STATE(mas, mt, 0, 0);

	mt_validate(&mm->mm_mt);
	mas_for_each(&mas, vma_mt, ULONG_MAX) {
		if ((vma_mt->vm_start != mas.index) ||
		    (vma_mt->vm_end - 1 != mas.last)) {
			pr_emerg("issue in %s\n", current->comm);
			dump_stack();
			dump_vma(vma_mt);
			pr_emerg("mt piv: %p %lu - %lu\n", vma_mt,
				 mas.index, mas.last);
			pr_emerg("mt vma: %p %lu - %lu\n", vma_mt,
				 vma_mt->vm_start, vma_mt->vm_end);

			mt_dump(mas.tree);
			if (vma_mt->vm_end != mas.last + 1) {
				pr_err("vma: %p vma_mt %lu-%lu\tmt %lu-%lu\n",
						mm, vma_mt->vm_start, vma_mt->vm_end,
						mas.index, mas.last);
				mt_dump(mas.tree);
			}
			VM_BUG_ON_MM(vma_mt->vm_end != mas.last + 1, mm);
			if (vma_mt->vm_start != mas.index) {
				pr_err("vma: %p vma_mt %p %lu - %lu doesn't match\n",
						mm, vma_mt, vma_mt->vm_start, vma_mt->vm_end);
				mt_dump(mas.tree);
			}
			VM_BUG_ON_MM(vma_mt->vm_start != mas.index, mm);
		}
	}
}

static void validate_mm(struct mm_struct *mm)
{
	int bug = 0;
	int i = 0;
	struct vm_area_struct *vma;
	MA_STATE(mas, &mm->mm_mt, 0, 0);

	validate_mm_mt(mm);

	mas_for_each(&mas, vma, ULONG_MAX) {
#ifdef CONFIG_DEBUG_VM_RB
		struct anon_vma *anon_vma = vma->anon_vma;
		struct anon_vma_chain *avc;

		if (anon_vma) {
			anon_vma_lock_read(anon_vma);
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				anon_vma_interval_tree_verify(avc);
			anon_vma_unlock_read(anon_vma);
		}
#endif
		i++;
	}
	if (i != mm->map_count) {
		pr_emerg("map_count %d mas_for_each %d\n", mm->map_count, i);
		bug = 1;
	}
	VM_BUG_ON_MM(bug, mm);
}

#else /* !CONFIG_DEBUG_VM_MAPLE_TREE */
#define validate_mm_mt(root) do { } while (0)
#define validate_mm(mm) do { } while (0)
#endif /* CONFIG_DEBUG_VM_MAPLE_TREE */

/*
 * vma has some anon_vma assigned, and is already inserted on that
 * anon_vma's interval trees.
 *
 * Before updating the vma's vm_start / vm_end / vm_pgoff fields, the
 * vma must be removed from the anon_vma's interval trees using
 * anon_vma_interval_tree_pre_update_vma().
 *
 * After the update, the vma will be reinserted using
 * anon_vma_interval_tree_post_update_vma().
 *
 * The entire update must be protected by exclusive mmap_lock and by
 * the root anon_vma's mutex.
 */
static inline void
anon_vma_interval_tree_pre_update_vma(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc;

	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
		anon_vma_interval_tree_remove(avc, &avc->anon_vma->rb_root);
}

static inline void
anon_vma_interval_tree_post_update_vma(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc;

	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
		anon_vma_interval_tree_insert(avc, &avc->anon_vma->rb_root);
}

static unsigned long count_vma_pages_range(struct mm_struct *mm,
		unsigned long addr, unsigned long end)
{
	VMA_ITERATOR(vmi, mm, addr);
	struct vm_area_struct *vma;
	unsigned long nr_pages = 0;

	for_each_vma_range(vmi, vma, end) {
		unsigned long vm_start = max(addr, vma->vm_start);
		unsigned long vm_end = min(end, vma->vm_end);

		nr_pages += PHYS_PFN(vm_end - vm_start);
	}

	return nr_pages;
}

static void __vma_link_file(struct vm_area_struct *vma,
			    struct address_space *mapping)
{
	if (vma->vm_flags & VM_SHARED)
		mapping_allow_writable(mapping);

	flush_dcache_mmap_lock(mapping);
	vma_interval_tree_insert(vma, &mapping->i_mmap);
	flush_dcache_mmap_unlock(mapping);
}

/*
 * vma_mas_store() - Store a VMA in the maple tree.
 * @vma: The vm_area_struct
 * @mas: The maple state
 *
 * Efficient way to store a VMA in the maple tree when the @mas has already
 * walked to the correct location.
 *
 * Note: the end address is inclusive in the maple tree.
 */
void vma_mas_store(struct vm_area_struct *vma, struct ma_state *mas)
{
	trace_vma_store(mas->tree, vma);
	mas_set_range(mas, vma->vm_start, vma->vm_end - 1);
	mas_store_prealloc(mas, vma);
}

/*
 * vma_mas_remove() - Remove a VMA from the maple tree.
 * @vma: The vm_area_struct
 * @mas: The maple state
 *
 * Efficient way to remove a VMA from the maple tree when the @mas has already
 * been established and points to the correct location.
 * Note: the end address is inclusive in the maple tree.
 */
void vma_mas_remove(struct vm_area_struct *vma, struct ma_state *mas)
{
	trace_vma_mas_szero(mas->tree, vma->vm_start, vma->vm_end - 1);
	mas->index = vma->vm_start;
	mas->last = vma->vm_end - 1;
	mas_store_prealloc(mas, NULL);
}

/*
 * vma_mas_szero() - Set a given range to zero.  Used when modifying a
 * vm_area_struct start or end.
 *
 * @mas: The maple tree ma_state
 * @start: The start address to zero
 * @end: The end address to zero.
 */
static inline void vma_mas_szero(struct ma_state *mas, unsigned long start,
				unsigned long end)
{
	trace_vma_mas_szero(mas->tree, start, end - 1);
	mas_set_range(mas, start, end - 1);
	mas_store_prealloc(mas, NULL);
}

static int vma_link(struct mm_struct *mm, struct vm_area_struct *vma)
{
	MA_STATE(mas, &mm->mm_mt, 0, 0);
	struct address_space *mapping = NULL;

	if (mas_preallocate(&mas, vma, GFP_KERNEL))
		return -ENOMEM;

	if (vma->vm_file) {
		mapping = vma->vm_file->f_mapping;
		i_mmap_lock_write(mapping);
	}

	vma_mas_store(vma, &mas);

	if (mapping) {
		__vma_link_file(vma, mapping);
		i_mmap_unlock_write(mapping);
	}

	mm->map_count++;
	validate_mm(mm);
	return 0;
}

/*
 * vma_expand - Expand an existing VMA
 *
 * @mas: The maple state
 * @vma: The vma to expand
 * @start: The start of the vma
 * @end: The exclusive end of the vma
 * @pgoff: The page offset of vma
 * @next: The current of next vma.
 *
 * Expand @vma to @start and @end.  Can expand off the start and end.  Will
 * expand over @next if it's different from @vma and @end == @next->vm_end.
 * Checking if the @vma can expand and merge with @next needs to be handled by
 * the caller.
 *
 * Returns: 0 on success
 */
inline int vma_expand(struct ma_state *mas, struct vm_area_struct *vma,
		      unsigned long start, unsigned long end, pgoff_t pgoff,
		      struct vm_area_struct *next)
{
	struct mm_struct *mm = vma->vm_mm;
	struct address_space *mapping = NULL;
	struct rb_root_cached *root = NULL;
	struct anon_vma *anon_vma = vma->anon_vma;
	struct file *file = vma->vm_file;
	bool remove_next = false;

	if (next && (vma != next) && (end == next->vm_end)) {
		remove_next = true;
		if (next->anon_vma && !vma->anon_vma) {
			int error;

			anon_vma = next->anon_vma;
			vma->anon_vma = anon_vma;
			error = anon_vma_clone(vma, next);
			if (error)
				return error;
		}
	}

	/* Not merging but overwriting any part of next is not handled. */
	VM_BUG_ON(next && !remove_next && next != vma && end > next->vm_start);
	/* Only handles expanding */
	VM_BUG_ON(vma->vm_start < start || vma->vm_end > end);

	if (mas_preallocate(mas, vma, GFP_KERNEL))
		goto nomem;

	vma_adjust_trans_huge(vma, start, end, 0);

	if (file) {
		mapping = file->f_mapping;
		root = &mapping->i_mmap;
		uprobe_munmap(vma, vma->vm_start, vma->vm_end);
		i_mmap_lock_write(mapping);
	}

	if (anon_vma) {
		anon_vma_lock_write(anon_vma);
		anon_vma_interval_tree_pre_update_vma(vma);
	}

	if (file) {
		flush_dcache_mmap_lock(mapping);
		vma_interval_tree_remove(vma, root);
	}

	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_pgoff = pgoff;
	/* Note: mas must be pointing to the expanding VMA */
	vma_mas_store(vma, mas);

	if (file) {
		vma_interval_tree_insert(vma, root);
		flush_dcache_mmap_unlock(mapping);
	}

	/* Expanding over the next vma */
	if (remove_next && file) {
		__remove_shared_vm_struct(next, file, mapping);
	}

	if (anon_vma) {
		anon_vma_interval_tree_post_update_vma(vma);
		anon_vma_unlock_write(anon_vma);
	}

	if (file) {
		i_mmap_unlock_write(mapping);
		uprobe_mmap(vma);
	}

	if (remove_next) {
		if (file) {
			uprobe_munmap(next, next->vm_start, next->vm_end);
			fput(file);
		}
		if (next->anon_vma)
			anon_vma_merge(vma, next);
		mm->map_count--;
		mpol_put(vma_policy(next));
		vm_area_free(next);
	}

	validate_mm(mm);
	return 0;

nomem:
	return -ENOMEM;
}

/*
 * We cannot adjust vm_start, vm_end, vm_pgoff fields of a vma that
 * is already present in an i_mmap tree without adjusting the tree.
 * The following helper function should be used when such adjustments
 * are necessary.  The "insert" vma (if any) is to be inserted
 * before we drop the necessary locks.
 */
int __vma_adjust(struct vm_area_struct *vma, unsigned long start,
	unsigned long end, pgoff_t pgoff, struct vm_area_struct *insert,
	struct vm_area_struct *expand)
{
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *next_next = NULL;	/* uninit var warning */
	struct vm_area_struct *next = find_vma(mm, vma->vm_end);
	struct vm_area_struct *orig_vma = vma;
	struct address_space *mapping = NULL;
	struct rb_root_cached *root = NULL;
	struct anon_vma *anon_vma = NULL;
	struct file *file = vma->vm_file;
	bool vma_changed = false;
	long adjust_next = 0;
	int remove_next = 0;
	MA_STATE(mas, &mm->mm_mt, 0, 0);
	struct vm_area_struct *exporter = NULL, *importer = NULL;

	if (next && !insert) {
		if (end >= next->vm_end) {
			/*
			 * vma expands, overlapping all the next, and
			 * perhaps the one after too (mprotect case 6).
			 * The only other cases that gets here are
			 * case 1, case 7 and case 8.
			 */
			if (next == expand) {
				/*
				 * The only case where we don't expand "vma"
				 * and we expand "next" instead is case 8.
				 */
				VM_WARN_ON(end != next->vm_end);
				/*
				 * remove_next == 3 means we're
				 * removing "vma" and that to do so we
				 * swapped "vma" and "next".
				 */
				remove_next = 3;
				VM_WARN_ON(file != next->vm_file);
				swap(vma, next);
			} else {
				VM_WARN_ON(expand != vma);
				/*
				 * case 1, 6, 7, remove_next == 2 is case 6,
				 * remove_next == 1 is case 1 or 7.
				 */
				remove_next = 1 + (end > next->vm_end);
				if (remove_next == 2)
					next_next = find_vma(mm, next->vm_end);

				VM_WARN_ON(remove_next == 2 &&
					   end != next_next->vm_end);
			}

			exporter = next;
			importer = vma;

			/*
			 * If next doesn't have anon_vma, import from vma after
			 * next, if the vma overlaps with it.
			 */
			if (remove_next == 2 && !next->anon_vma)
				exporter = next_next;

		} else if (end > next->vm_start) {
			/*
			 * vma expands, overlapping part of the next:
			 * mprotect case 5 shifting the boundary up.
			 */
			adjust_next = (end - next->vm_start);
			exporter = next;
			importer = vma;
			VM_WARN_ON(expand != importer);
		} else if (end < vma->vm_end) {
			/*
			 * vma shrinks, and !insert tells it's not
			 * split_vma inserting another: so it must be
			 * mprotect case 4 shifting the boundary down.
			 */
			adjust_next = -(vma->vm_end - end);
			exporter = vma;
			importer = next;
			VM_WARN_ON(expand != importer);
		}

		/*
		 * Easily overlooked: when mprotect shifts the boundary,
		 * make sure the expanding vma has anon_vma set if the
		 * shrinking vma had, to cover any anon pages imported.
		 */
		if (exporter && exporter->anon_vma && !importer->anon_vma) {
			int error;

			importer->anon_vma = exporter->anon_vma;
			error = anon_vma_clone(importer, exporter);
			if (error)
				return error;
		}
	}

	if (mas_preallocate(&mas, vma, GFP_KERNEL))
		return -ENOMEM;

	vma_adjust_trans_huge(orig_vma, start, end, adjust_next);
	if (file) {
		mapping = file->f_mapping;
		root = &mapping->i_mmap;
		uprobe_munmap(vma, vma->vm_start, vma->vm_end);

		if (adjust_next)
			uprobe_munmap(next, next->vm_start, next->vm_end);

		i_mmap_lock_write(mapping);
		if (insert && insert->vm_file) {
			/*
			 * Put into interval tree now, so instantiated pages
			 * are visible to arm/parisc __flush_dcache_page
			 * throughout; but we cannot insert into address
			 * space until vma start or end is updated.
			 */
			__vma_link_file(insert, insert->vm_file->f_mapping);
		}
	}

	anon_vma = vma->anon_vma;
	if (!anon_vma && adjust_next)
		anon_vma = next->anon_vma;
	if (anon_vma) {
		VM_WARN_ON(adjust_next && next->anon_vma &&
			   anon_vma != next->anon_vma);
		anon_vma_lock_write(anon_vma);
		anon_vma_interval_tree_pre_update_vma(vma);
		if (adjust_next)
			anon_vma_interval_tree_pre_update_vma(next);
	}

	if (file) {
		flush_dcache_mmap_lock(mapping);
		vma_interval_tree_remove(vma, root);
		if (adjust_next)
			vma_interval_tree_remove(next, root);
	}

	if (start != vma->vm_start) {
		if ((vma->vm_start < start) &&
		    (!insert || (insert->vm_end != start))) {
			vma_mas_szero(&mas, vma->vm_start, start);
			VM_WARN_ON(insert && insert->vm_start > vma->vm_start);
		} else {
			vma_changed = true;
		}
		vma->vm_start = start;
	}
	if (end != vma->vm_end) {
		if (vma->vm_end > end) {
			if (!insert || (insert->vm_start != end)) {
				vma_mas_szero(&mas, end, vma->vm_end);
				mas_reset(&mas);
				VM_WARN_ON(insert &&
					   insert->vm_end < vma->vm_end);
			}
		} else {
			vma_changed = true;
		}
		vma->vm_end = end;
	}

	if (vma_changed)
		vma_mas_store(vma, &mas);

	vma->vm_pgoff = pgoff;
	if (adjust_next) {
		next->vm_start += adjust_next;
		next->vm_pgoff += adjust_next >> PAGE_SHIFT;
		vma_mas_store(next, &mas);
	}

	if (file) {
		if (adjust_next)
			vma_interval_tree_insert(next, root);
		vma_interval_tree_insert(vma, root);
		flush_dcache_mmap_unlock(mapping);
	}

	if (remove_next && file) {
		__remove_shared_vm_struct(next, file, mapping);
		if (remove_next == 2)
			__remove_shared_vm_struct(next_next, file, mapping);
	} else if (insert) {
		/*
		 * split_vma has split insert from vma, and needs
		 * us to insert it before dropping the locks
		 * (it may either follow vma or precede it).
		 */
		mas_reset(&mas);
		vma_mas_store(insert, &mas);
		mm->map_count++;
	}

	if (anon_vma) {
		anon_vma_interval_tree_post_update_vma(vma);
		if (adjust_next)
			anon_vma_interval_tree_post_update_vma(next);
		anon_vma_unlock_write(anon_vma);
	}

	if (file) {
		i_mmap_unlock_write(mapping);
		uprobe_mmap(vma);

		if (adjust_next)
			uprobe_mmap(next);
	}

	if (remove_next) {
again:
		if (file) {
			uprobe_munmap(next, next->vm_start, next->vm_end);
			fput(file);
		}
		if (next->anon_vma)
			anon_vma_merge(vma, next);
		mm->map_count--;
		mpol_put(vma_policy(next));
		if (remove_next != 2)
			BUG_ON(vma->vm_end < next->vm_end);
		vm_area_free(next);

		/*
		 * In mprotect's case 6 (see comments on vma_merge),
		 * we must remove next_next too.
		 */
		if (remove_next == 2) {
			remove_next = 1;
			next = next_next;
			goto again;
		}
	}
	if (insert && file)
		uprobe_mmap(insert);

	mas_destroy(&mas);
	validate_mm(mm);

	return 0;
}

/*
 * If the vma has a ->close operation then the driver probably needs to release
 * per-vma resources, so we don't attempt to merge those.
 */
static inline int is_mergeable_vma(struct vm_area_struct *vma,
				struct file *file, unsigned long vm_flags,
				struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
				struct anon_vma_name *anon_name)
{
	/*
	 * VM_SOFTDIRTY should not prevent from VMA merging, if we
	 * match the flags but dirty bit -- the caller should mark
	 * merged VMA as dirty. If dirty bit won't be excluded from
	 * comparison, we increase pressure on the memory system forcing
	 * the kernel to generate new VMAs when old one could be
	 * extended instead.
	 */
	if ((vma->vm_flags ^ vm_flags) & ~VM_SOFTDIRTY)
		return 0;
	if (vma->vm_file != file)
		return 0;
	if (vma->vm_ops && vma->vm_ops->close)
		return 0;
	if (!is_mergeable_vm_userfaultfd_ctx(vma, vm_userfaultfd_ctx))
		return 0;
	if (!anon_vma_name_eq(anon_vma_name(vma), anon_name))
		return 0;
	return 1;
}

static inline int is_mergeable_anon_vma(struct anon_vma *anon_vma1,
					struct anon_vma *anon_vma2,
					struct vm_area_struct *vma)
{
	/*
	 * The list_is_singular() test is to avoid merging VMA cloned from
	 * parents. This can improve scalability caused by anon_vma lock.
	 */
	if ((!anon_vma1 || !anon_vma2) && (!vma ||
		list_is_singular(&vma->anon_vma_chain)))
		return 1;
	return anon_vma1 == anon_vma2;
}

/*
 * Return true if we can merge this (vm_flags,anon_vma,file,vm_pgoff)
 * in front of (at a lower virtual address and file offset than) the vma.
 *
 * We cannot merge two vmas if they have differently assigned (non-NULL)
 * anon_vmas, nor if same anon_vma is assigned but offsets incompatible.
 *
 * We don't check here for the merged mmap wrapping around the end of pagecache
 * indices (16TB on ia32) because do_mmap() does not permit mmap's which
 * wrap, nor mmaps which cover the final page at index -1UL.
 */
static int
can_vma_merge_before(struct vm_area_struct *vma, unsigned long vm_flags,
		     struct anon_vma *anon_vma, struct file *file,
		     pgoff_t vm_pgoff,
		     struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
		     struct anon_vma_name *anon_name)
{
	if (is_mergeable_vma(vma, file, vm_flags, vm_userfaultfd_ctx, anon_name) &&
	    is_mergeable_anon_vma(anon_vma, vma->anon_vma, vma)) {
		if (vma->vm_pgoff == vm_pgoff)
			return 1;
	}
	return 0;
}

/*
 * Return true if we can merge this (vm_flags,anon_vma,file,vm_pgoff)
 * beyond (at a higher virtual address and file offset than) the vma.
 *
 * We cannot merge two vmas if they have differently assigned (non-NULL)
 * anon_vmas, nor if same anon_vma is assigned but offsets incompatible.
 */
static int
can_vma_merge_after(struct vm_area_struct *vma, unsigned long vm_flags,
		    struct anon_vma *anon_vma, struct file *file,
		    pgoff_t vm_pgoff,
		    struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
		    struct anon_vma_name *anon_name)
{
	if (is_mergeable_vma(vma, file, vm_flags, vm_userfaultfd_ctx, anon_name) &&
	    is_mergeable_anon_vma(anon_vma, vma->anon_vma, vma)) {
		pgoff_t vm_pglen;
		vm_pglen = vma_pages(vma);
		if (vma->vm_pgoff + vm_pglen == vm_pgoff)
			return 1;
	}
	return 0;
}

/*
 * Given a mapping request (addr,end,vm_flags,file,pgoff,anon_name),
 * figure out whether that can be merged with its predecessor or its
 * successor.  Or both (it neatly fills a hole).
 *
 * In most cases - when called for mmap, brk or mremap - [addr,end) is
 * certain not to be mapped by the time vma_merge is called; but when
 * called for mprotect, it is certain to be already mapped (either at
 * an offset within prev, or at the start of next), and the flags of
 * this area are about to be changed to vm_flags - and the no-change
 * case has already been eliminated.
 *
 * The following mprotect cases have to be considered, where AAAA is
 * the area passed down from mprotect_fixup, never extending beyond one
 * vma, PPPPPP is the prev vma specified, and NNNNNN the next vma after:
 *
 *     AAAA             AAAA                   AAAA
 *    PPPPPPNNNNNN    PPPPPPNNNNNN       PPPPPPNNNNNN
 *    cannot merge    might become       might become
 *                    PPNNNNNNNNNN       PPPPPPPPPPNN
 *    mmap, brk or    case 4 below       case 5 below
 *    mremap move:
 *                        AAAA               AAAA
 *                    PPPP    NNNN       PPPPNNNNXXXX
 *                    might become       might become
 *                    PPPPPPPPPPPP 1 or  PPPPPPPPPPPP 6 or
 *                    PPPPPPPPNNNN 2 or  PPPPPPPPXXXX 7 or
 *                    PPPPNNNNNNNN 3     PPPPXXXXXXXX 8
 *
 * It is important for case 8 that the vma NNNN overlapping the
 * region AAAA is never going to extended over XXXX. Instead XXXX must
 * be extended in region AAAA and NNNN must be removed. This way in
 * all cases where vma_merge succeeds, the moment vma_adjust drops the
 * rmap_locks, the properties of the merged vma will be already
 * correct for the whole merged range. Some of those properties like
 * vm_page_prot/vm_flags may be accessed by rmap_walks and they must
 * be correct for the whole merged range immediately after the
 * rmap_locks are released. Otherwise if XXXX would be removed and
 * NNNN would be extended over the XXXX range, remove_migration_ptes
 * or other rmap walkers (if working on addresses beyond the "end"
 * parameter) may establish ptes with the wrong permissions of NNNN
 * instead of the right permissions of XXXX.
 */
struct vm_area_struct *vma_merge(struct mm_struct *mm,
			struct vm_area_struct *prev, unsigned long addr,
			unsigned long end, unsigned long vm_flags,
			struct anon_vma *anon_vma, struct file *file,
			pgoff_t pgoff, struct mempolicy *policy,
			struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
			struct anon_vma_name *anon_name)
{
	pgoff_t pglen = (end - addr) >> PAGE_SHIFT;
	struct vm_area_struct *mid, *next, *res;
	int err = -1;
	bool merge_prev = false;
	bool merge_next = false;

	/*
	 * We later require that vma->vm_flags == vm_flags,
	 * so this tests vma->vm_flags & VM_SPECIAL, too.
	 */
	if (vm_flags & VM_SPECIAL)
		return NULL;

	next = find_vma(mm, prev ? prev->vm_end : 0);
	mid = next;
	if (next && next->vm_end == end)		/* cases 6, 7, 8 */
		next = find_vma(mm, next->vm_end);

	/* verify some invariant that must be enforced by the caller */
	VM_WARN_ON(prev && addr <= prev->vm_start);
	VM_WARN_ON(mid && end > mid->vm_end);
	VM_WARN_ON(addr >= end);

	/* Can we merge the predecessor? */
	if (prev && prev->vm_end == addr &&
			mpol_equal(vma_policy(prev), policy) &&
			can_vma_merge_after(prev, vm_flags,
					    anon_vma, file, pgoff,
					    vm_userfaultfd_ctx, anon_name)) {
		merge_prev = true;
	}
	/* Can we merge the successor? */
	if (next && end == next->vm_start &&
			mpol_equal(policy, vma_policy(next)) &&
			can_vma_merge_before(next, vm_flags,
					     anon_vma, file, pgoff+pglen,
					     vm_userfaultfd_ctx, anon_name)) {
		merge_next = true;
	}
	/* Can we merge both the predecessor and the successor? */
	if (merge_prev && merge_next &&
			is_mergeable_anon_vma(prev->anon_vma,
				next->anon_vma, NULL)) {	 /* cases 1, 6 */
		err = __vma_adjust(prev, prev->vm_start,
					next->vm_end, prev->vm_pgoff, NULL,
					prev);
		res = prev;
	} else if (merge_prev) {			/* cases 2, 5, 7 */
		err = __vma_adjust(prev, prev->vm_start,
					end, prev->vm_pgoff, NULL, prev);
		res = prev;
	} else if (merge_next) {
		if (prev && addr < prev->vm_end)	/* case 4 */
			err = __vma_adjust(prev, prev->vm_start,
					addr, prev->vm_pgoff, NULL, next);
		else					/* cases 3, 8 */
			err = __vma_adjust(mid, addr, next->vm_end,
					next->vm_pgoff - pglen, NULL, next);
		res = next;
	}

	/*
	 * Cannot merge with predecessor or successor or error in __vma_adjust?
	 */
	if (err)
		return NULL;
	khugepaged_enter_vma(res, vm_flags);
	return res;
}

/*
 * Rough compatibility check to quickly see if it's even worth looking
 * at sharing an anon_vma.
 *
 * They need to have the same vm_file, and the flags can only differ
 * in things that mprotect may change.
 *
 * NOTE! The fact that we share an anon_vma doesn't _have_ to mean that
 * we can merge the two vma's. For example, we refuse to merge a vma if
 * there is a vm_ops->close() function, because that indicates that the
 * driver is doing some kind of reference counting. But that doesn't
 * really matter for the anon_vma sharing case.
 */
static int anon_vma_compatible(struct vm_area_struct *a, struct vm_area_struct *b)
{
	return a->vm_end == b->vm_start &&
		mpol_equal(vma_policy(a), vma_policy(b)) &&
		a->vm_file == b->vm_file &&
		!((a->vm_flags ^ b->vm_flags) & ~(VM_ACCESS_FLAGS | VM_SOFTDIRTY)) &&
		b->vm_pgoff == a->vm_pgoff + ((b->vm_start - a->vm_start) >> PAGE_SHIFT);
}

/*
 * Do some basic sanity checking to see if we can re-use the anon_vma
 * from 'old'. The 'a'/'b' vma's are in VM order - one of them will be
 * the same as 'old', the other will be the new one that is trying
 * to share the anon_vma.
 *
 * NOTE! This runs with mmap_lock held for reading, so it is possible that
 * the anon_vma of 'old' is concurrently in the process of being set up
 * by another page fault trying to merge _that_. But that's ok: if it
 * is being set up, that automatically means that it will be a singleton
 * acceptable for merging, so we can do all of this optimistically. But
 * we do that READ_ONCE() to make sure that we never re-load the pointer.
 *
 * IOW: that the "list_is_singular()" test on the anon_vma_chain only
 * matters for the 'stable anon_vma' case (ie the thing we want to avoid
 * is to return an anon_vma that is "complex" due to having gone through
 * a fork).
 *
 * We also make sure that the two vma's are compatible (adjacent,
 * and with the same memory policies). That's all stable, even with just
 * a read lock on the mmap_lock.
 */
static struct anon_vma *reusable_anon_vma(struct vm_area_struct *old, struct vm_area_struct *a, struct vm_area_struct *b)
{
	if (anon_vma_compatible(a, b)) {
		struct anon_vma *anon_vma = READ_ONCE(old->anon_vma);

		if (anon_vma && list_is_singular(&old->anon_vma_chain))
			return anon_vma;
	}
	return NULL;
}

/*
 * find_mergeable_anon_vma is used by anon_vma_prepare, to check
 * neighbouring vmas for a suitable anon_vma, before it goes off
 * to allocate a new anon_vma.  It checks because a repetitive
 * sequence of mprotects and faults may otherwise lead to distinct
 * anon_vmas being allocated, preventing vma merge in subsequent
 * mprotect.
 */
struct anon_vma *find_mergeable_anon_vma(struct vm_area_struct *vma)
{
	MA_STATE(mas, &vma->vm_mm->mm_mt, vma->vm_end, vma->vm_end);
	struct anon_vma *anon_vma = NULL;
	struct vm_area_struct *prev, *next;

	/* Try next first. */
	next = mas_walk(&mas);
	if (next) {
		anon_vma = reusable_anon_vma(next, vma, next);
		if (anon_vma)
			return anon_vma;
	}

	prev = mas_prev(&mas, 0);
	VM_BUG_ON_VMA(prev != vma, vma);
	prev = mas_prev(&mas, 0);
	/* Try prev next. */
	if (prev)
		anon_vma = reusable_anon_vma(prev, prev, vma);

	/*
	 * We might reach here with anon_vma == NULL if we can't find
	 * any reusable anon_vma.
	 * There's no absolute need to look only at touching neighbours:
	 * we could search further afield for "compatible" anon_vmas.
	 * But it would probably just be a waste of time searching,
	 * or lead to too many vmas hanging off the same anon_vma.
	 * We're trying to allow mprotect remerging later on,
	 * not trying to minimize memory used for anon_vmas.
	 */
	return anon_vma;
}

/*
 * If a hint addr is less than mmap_min_addr change hint to be as
 * low as possible but still greater than mmap_min_addr
 */
static inline unsigned long round_hint_to_min(unsigned long hint)
{
	hint &= PAGE_MASK;
	if (((void *)hint != NULL) &&
	    (hint < mmap_min_addr))
		return PAGE_ALIGN(mmap_min_addr);
	return hint;
}

int mlock_future_check(struct mm_struct *mm, unsigned long flags,
		       unsigned long len)
{
	unsigned long locked, lock_limit;

	/*  mlock MCL_FUTURE? */
	if (flags & VM_LOCKED) {
		locked = len >> PAGE_SHIFT;
		locked += mm->locked_vm;
		lock_limit = rlimit(RLIMIT_MEMLOCK);
		lock_limit >>= PAGE_SHIFT;
		if (locked > lock_limit && !capable(CAP_IPC_LOCK))
			return -EAGAIN;
	}
	return 0;
}

static inline u64 file_mmap_size_max(struct file *file, struct inode *inode)
{
	if (S_ISREG(inode->i_mode))
		return MAX_LFS_FILESIZE;

	if (S_ISBLK(inode->i_mode))
		return MAX_LFS_FILESIZE;

	if (S_ISSOCK(inode->i_mode))
		return MAX_LFS_FILESIZE;

	/* Special "we do even unsigned file positions" case */
	if (file->f_mode & FMODE_UNSIGNED_OFFSET)
		return 0;

	/* Yes, random drivers might want more. But I'm tired of buggy drivers */
	return ULONG_MAX;
}

static inline bool file_mmap_ok(struct file *file, struct inode *inode,
				unsigned long pgoff, unsigned long len)
{
	u64 maxsize = file_mmap_size_max(file, inode);

	if (maxsize && len > maxsize)
		return false;
	maxsize -= len;
	if (pgoff > maxsize >> PAGE_SHIFT)
		return false;
	return true;
}

/*
 * The caller must write-lock current->mm->mmap_lock.
 */
unsigned long do_mmap(struct file *file, unsigned long addr,
			unsigned long len, unsigned long prot,
			unsigned long flags, unsigned long pgoff,
			unsigned long *populate, struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	vm_flags_t vm_flags;
	int pkey = 0;

	validate_mm(mm);
	*populate = 0;

	if (!len)
		return -EINVAL;

	/*
	 * Does the application expect PROT_READ to imply PROT_EXEC?
	 *
	 * (the exception is when the underlying filesystem is noexec
	 *  mounted, in which case we dont add PROT_EXEC.)
	 */
	if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
		if (!(file && path_noexec(&file->f_path)))
			prot |= PROT_EXEC;

	/* force arch specific MAP_FIXED handling in get_unmapped_area */
	if (flags & MAP_FIXED_NOREPLACE)
		flags |= MAP_FIXED;

	if (!(flags & MAP_FIXED))
		addr = round_hint_to_min(addr);

	/* Careful about overflows.. */
	len = PAGE_ALIGN(len);
	if (!len)
		return -ENOMEM;

	/* offset overflow? */
	if ((pgoff + (len >> PAGE_SHIFT)) < pgoff)
		return -EOVERFLOW;

	/* Too many mappings? */
	if (mm->map_count > sysctl_max_map_count)
		return -ENOMEM;

	/* Obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */
	addr = get_unmapped_area(file, addr, len, pgoff, flags);
	if (IS_ERR_VALUE(addr))
		return addr;

	if (flags & MAP_FIXED_NOREPLACE) {
		if (find_vma_intersection(mm, addr, addr + len))
			return -EEXIST;
	}

	if (prot == PROT_EXEC) {
		pkey = execute_only_pkey(mm);
		if (pkey < 0)
			pkey = 0;
	}

	/* Do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 */
	vm_flags = calc_vm_prot_bits(prot, pkey) | calc_vm_flag_bits(flags) |
			mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

	if (flags & MAP_LOCKED)
		if (!can_do_mlock())
			return -EPERM;

	if (mlock_future_check(mm, vm_flags, len))
		return -EAGAIN;

	if (file) {
		struct inode *inode = file_inode(file);
		unsigned long flags_mask;

		if (!file_mmap_ok(file, inode, pgoff, len))
			return -EOVERFLOW;

		flags_mask = LEGACY_MAP_MASK | file->f_op->mmap_supported_flags;

		switch (flags & MAP_TYPE) {
		case MAP_SHARED:
			/*
			 * Force use of MAP_SHARED_VALIDATE with non-legacy
			 * flags. E.g. MAP_SYNC is dangerous to use with
			 * MAP_SHARED as you don't know which consistency model
			 * you will get. We silently ignore unsupported flags
			 * with MAP_SHARED to preserve backward compatibility.
			 */
			flags &= LEGACY_MAP_MASK;
			fallthrough;
		case MAP_SHARED_VALIDATE:
			if (flags & ~flags_mask)
				return -EOPNOTSUPP;
			if (prot & PROT_WRITE) {
				if (!(file->f_mode & FMODE_WRITE))
					return -EACCES;
				if (IS_SWAPFILE(file->f_mapping->host))
					return -ETXTBSY;
			}

			/*
			 * Make sure we don't allow writing to an append-only
			 * file..
			 */
			if (IS_APPEND(inode) && (file->f_mode & FMODE_WRITE))
				return -EACCES;

			vm_flags |= VM_SHARED | VM_MAYSHARE;
			if (!(file->f_mode & FMODE_WRITE))
				vm_flags &= ~(VM_MAYWRITE | VM_SHARED);
			fallthrough;
		case MAP_PRIVATE:
			if (!(file->f_mode & FMODE_READ))
				return -EACCES;
			if (path_noexec(&file->f_path)) {
				if (vm_flags & VM_EXEC)
					return -EPERM;
				vm_flags &= ~VM_MAYEXEC;
			}

			if (!file->f_op->mmap)
				return -ENODEV;
			if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))
				return -EINVAL;
			break;

		default:
			return -EINVAL;
		}
	} else {
		switch (flags & MAP_TYPE) {
		case MAP_SHARED:
			if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))
				return -EINVAL;
			/*
			 * Ignore pgoff.
			 */
			pgoff = 0;
			vm_flags |= VM_SHARED | VM_MAYSHARE;
			break;
		case MAP_PRIVATE:
			/*
			 * Set pgoff according to addr for anon_vma.
			 */
			pgoff = addr >> PAGE_SHIFT;
			break;
		default:
			return -EINVAL;
		}
	}

	/*
	 * Set 'VM_NORESERVE' if we should not account for the
	 * memory use of this mapping.
	 */
	if (flags & MAP_NORESERVE) {
		/* We honor MAP_NORESERVE if allowed to overcommit */
		if (sysctl_overcommit_memory != OVERCOMMIT_NEVER)
			vm_flags |= VM_NORESERVE;

		/* hugetlb applies strict overcommit unless MAP_NORESERVE */
		if (file && is_file_hugepages(file))
			vm_flags |= VM_NORESERVE;
	}

	addr = mmap_region(file, addr, len, vm_flags, pgoff, uf);
	if (!IS_ERR_VALUE(addr) &&
	    ((vm_flags & VM_LOCKED) ||
	     (flags & (MAP_POPULATE | MAP_NONBLOCK)) == MAP_POPULATE))
		*populate = len;
	return addr;
}

unsigned long ksys_mmap_pgoff(unsigned long addr, unsigned long len,
			      unsigned long prot, unsigned long flags,
			      unsigned long fd, unsigned long pgoff)
{
	struct file *file = NULL;
	unsigned long retval;

	if (!(flags & MAP_ANONYMOUS)) {
		audit_mmap_fd(fd, flags);
		file = fget(fd);
		if (!file)
			return -EBADF;
		if (is_file_hugepages(file)) {
			len = ALIGN(len, huge_page_size(hstate_file(file)));
		} else if (unlikely(flags & MAP_HUGETLB)) {
			retval = -EINVAL;
			goto out_fput;
		}
	} else if (flags & MAP_HUGETLB) {
		struct hstate *hs;

		hs = hstate_sizelog((flags >> MAP_HUGE_SHIFT) & MAP_HUGE_MASK);
		if (!hs)
			return -EINVAL;

		len = ALIGN(len, huge_page_size(hs));
		/*
		 * VM_NORESERVE is used because the reservations will be
		 * taken when vm_ops->mmap() is called
		 */
		file = hugetlb_file_setup(HUGETLB_ANON_FILE, len,
				VM_NORESERVE,
				HUGETLB_ANONHUGE_INODE,
				(flags >> MAP_HUGE_SHIFT) & MAP_HUGE_MASK);
		if (IS_ERR(file))
			return PTR_ERR(file);
	}

	retval = vm_mmap_pgoff(file, addr, len, prot, flags, pgoff);
out_fput:
	if (file)
		fput(file);
	return retval;
}

SYSCALL_DEFINE6(mmap_pgoff, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, pgoff)
{
	return ksys_mmap_pgoff(addr, len, prot, flags, fd, pgoff);
}

#ifdef __ARCH_WANT_SYS_OLD_MMAP
struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

SYSCALL_DEFINE1(old_mmap, struct mmap_arg_struct __user *, arg)
{
	struct mmap_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	if (offset_in_page(a.offset))
		return -EINVAL;

	return ksys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd,
			       a.offset >> PAGE_SHIFT);
}
#endif /* __ARCH_WANT_SYS_OLD_MMAP */

/*
 * Some shared mappings will want the pages marked read-only
 * to track write events. If so, we'll downgrade vm_page_prot
 * to the private version (using protection_map[] without the
 * VM_SHARED bit).
 */
int vma_wants_writenotify(struct vm_area_struct *vma, pgprot_t vm_page_prot)
{
	vm_flags_t vm_flags = vma->vm_flags;
	const struct vm_operations_struct *vm_ops = vma->vm_ops;

	/* If it was private or non-writable, the write bit is already clear */
	if ((vm_flags & (VM_WRITE|VM_SHARED)) != ((VM_WRITE|VM_SHARED)))
		return 0;

	/* The backer wishes to know when pages are first written to? */
	if (vm_ops && (vm_ops->page_mkwrite || vm_ops->pfn_mkwrite))
		return 1;

	/* The open routine did something to the protections that pgprot_modify
	 * won't preserve? */
	if (pgprot_val(vm_page_prot) !=
	    pgprot_val(vm_pgprot_modify(vm_page_prot, vm_flags)))
		return 0;

	/*
	 * Do we need to track softdirty? hugetlb does not support softdirty
	 * tracking yet.
	 */
	if (vma_soft_dirty_enabled(vma) && !is_vm_hugetlb_page(vma))
		return 1;

	/* Do we need write faults for uffd-wp tracking? */
	if (userfaultfd_wp(vma))
		return 1;

	/* Specialty mapping? */
	if (vm_flags & VM_PFNMAP)
		return 0;

	/* Can the mapping track the dirty pages? */
	return vma->vm_file && vma->vm_file->f_mapping &&
		mapping_can_writeback(vma->vm_file->f_mapping);
}

/*
 * We account for memory if it's a private writeable mapping,
 * not hugepages and VM_NORESERVE wasn't set.
 */
static inline int accountable_mapping(struct file *file, vm_flags_t vm_flags)
{
	/*
	 * hugetlb has its own accounting separate from the core VM
	 * VM_HUGETLB may not be set yet so we cannot check for that flag.
	 */
	if (file && is_file_hugepages(file))
		return 0;

	return (vm_flags & (VM_NORESERVE | VM_SHARED | VM_WRITE)) == VM_WRITE;
}

/**
 * unmapped_area() - Find an area between the low_limit and the high_limit with
 * the correct alignment and offset, all from @info. Note: current->mm is used
 * for the search.
 *
 * @info: The unmapped area information including the range (low_limit -
 * hight_limit), the alignment offset and mask.
 *
 * Return: A memory address or -ENOMEM.
 */
static unsigned long unmapped_area(struct vm_unmapped_area_info *info)
{
	unsigned long length, gap, low_limit;
	struct vm_area_struct *tmp;

	MA_STATE(mas, &current->mm->mm_mt, 0, 0);

	/* Adjust search length to account for worst case alignment overhead */
	length = info->length + info->align_mask;
	if (length < info->length)
		return -ENOMEM;

	low_limit = info->low_limit;
retry:
	if (mas_empty_area(&mas, low_limit, info->high_limit - 1, length))
		return -ENOMEM;

	gap = mas.index;
	gap += (info->align_offset - gap) & info->align_mask;
	tmp = mas_next(&mas, ULONG_MAX);
	if (tmp && (tmp->vm_flags & VM_GROWSDOWN)) { /* Avoid prev check if possible */
		if (vm_start_gap(tmp) < gap + length - 1) {
			low_limit = tmp->vm_end;
			mas_reset(&mas);
			goto retry;
		}
	} else {
		tmp = mas_prev(&mas, 0);
		if (tmp && vm_end_gap(tmp) > gap) {
			low_limit = vm_end_gap(tmp);
			mas_reset(&mas);
			goto retry;
		}
	}

	return gap;
}

/**
 * unmapped_area_topdown() - Find an area between the low_limit and the
 * high_limit with * the correct alignment and offset at the highest available
 * address, all from @info. Note: current->mm is used for the search.
 *
 * @info: The unmapped area information including the range (low_limit -
 * hight_limit), the alignment offset and mask.
 *
 * Return: A memory address or -ENOMEM.
 */
static unsigned long unmapped_area_topdown(struct vm_unmapped_area_info *info)
{
	unsigned long length, gap, high_limit, gap_end;
	struct vm_area_struct *tmp;

	MA_STATE(mas, &current->mm->mm_mt, 0, 0);
	/* Adjust search length to account for worst case alignment overhead */
	length = info->length + info->align_mask;
	if (length < info->length)
		return -ENOMEM;

	high_limit = info->high_limit;
retry:
	if (mas_empty_area_rev(&mas, info->low_limit, high_limit - 1,
				length))
		return -ENOMEM;

	gap = mas.last + 1 - info->length;
	gap -= (gap - info->align_offset) & info->align_mask;
	gap_end = mas.last;
	tmp = mas_next(&mas, ULONG_MAX);
	if (tmp && (tmp->vm_flags & VM_GROWSDOWN)) { /* Avoid prev check if possible */
		if (vm_start_gap(tmp) <= gap_end) {
			high_limit = vm_start_gap(tmp);
			mas_reset(&mas);
			goto retry;
		}
	} else {
		tmp = mas_prev(&mas, 0);
		if (tmp && vm_end_gap(tmp) > gap) {
			high_limit = tmp->vm_start;
			mas_reset(&mas);
			goto retry;
		}
	}

	return gap;
}

/*
 * Search for an unmapped address range.
 *
 * We are looking for a range that:
 * - does not intersect with any VMA;
 * - is contained within the [low_limit, high_limit) interval;
 * - is at least the desired size.
 * - satisfies (begin_addr & align_mask) == (align_offset & align_mask)
 */
unsigned long vm_unmapped_area(struct vm_unmapped_area_info *info)
{
	unsigned long addr;

	if (info->flags & VM_UNMAPPED_AREA_TOPDOWN)
		addr = unmapped_area_topdown(info);
	else
		addr = unmapped_area(info);

	trace_vm_unmapped_area(addr, info);
	return addr;
}

/* Get an address range which is currently unmapped.
 * For shmat() with addr=0.
 *
 * Ugly calling convention alert:
 * Return value with the low bits set means error value,
 * ie
 *	if (ret & ~PAGE_MASK)
 *		error = ret;
 *
 * This function "knows" that -ENOMEM has the bits set.
 */
unsigned long
generic_get_unmapped_area(struct file *filp, unsigned long addr,
			  unsigned long len, unsigned long pgoff,
			  unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct vm_unmapped_area_info info;
	const unsigned long mmap_end = arch_get_mmap_end(addr, len, flags);

	if (len > mmap_end - mmap_min_addr)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		return addr;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma_prev(mm, addr, &prev);
		if (mmap_end - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vm_start_gap(vma)) &&
		    (!prev || addr >= vm_end_gap(prev)))
			return addr;
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = mmap_end;
	info.align_mask = 0;
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}

#ifndef HAVE_ARCH_UNMAPPED_AREA
unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		       unsigned long len, unsigned long pgoff,
		       unsigned long flags)
{
	return generic_get_unmapped_area(filp, addr, len, pgoff, flags);
}
#endif

/*
 * This mmap-allocator allocates new areas top-down from below the
 * stack's low limit (the base):
 */
unsigned long
generic_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
				  unsigned long len, unsigned long pgoff,
				  unsigned long flags)
{
	struct vm_area_struct *vma, *prev;
	struct mm_struct *mm = current->mm;
	struct vm_unmapped_area_info info;
	const unsigned long mmap_end = arch_get_mmap_end(addr, len, flags);

	/* requested length too big for entire address space */
	if (len > mmap_end - mmap_min_addr)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		return addr;

	/* requesting a specific address */
	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma_prev(mm, addr, &prev);
		if (mmap_end - len >= addr && addr >= mmap_min_addr &&
				(!vma || addr + len <= vm_start_gap(vma)) &&
				(!prev || addr >= vm_end_gap(prev)))
			return addr;
	}

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = max(PAGE_SIZE, mmap_min_addr);
	info.high_limit = arch_get_mmap_base(addr, mm->mmap_base);
	info.align_mask = 0;
	info.align_offset = 0;
	addr = vm_unmapped_area(&info);

	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	if (offset_in_page(addr)) {
		VM_BUG_ON(addr != -ENOMEM);
		info.flags = 0;
		info.low_limit = TASK_UNMAPPED_BASE;
		info.high_limit = mmap_end;
		addr = vm_unmapped_area(&info);
	}

	return addr;
}

#ifndef HAVE_ARCH_UNMAPPED_AREA_TOPDOWN
unsigned long
arch_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
			       unsigned long len, unsigned long pgoff,
			       unsigned long flags)
{
	return generic_get_unmapped_area_topdown(filp, addr, len, pgoff, flags);
}
#endif

unsigned long
get_unmapped_area(struct file *file, unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	unsigned long (*get_area)(struct file *, unsigned long,
				  unsigned long, unsigned long, unsigned long);

	unsigned long error = arch_mmap_check(addr, len, flags);
	if (error)
		return error;

	/* Careful about overflows.. */
	if (len > TASK_SIZE)
		return -ENOMEM;

	get_area = current->mm->get_unmapped_area;
	if (file) {
		if (file->f_op->get_unmapped_area)
			get_area = file->f_op->get_unmapped_area;
	} else if (flags & MAP_SHARED) {
		/*
		 * mmap_region() will call shmem_zero_setup() to create a file,
		 * so use shmem's get_unmapped_area in case it can be huge.
		 * do_mmap() will clear pgoff, so match alignment.
		 */
		pgoff = 0;
		get_area = shmem_get_unmapped_area;
	}

	addr = get_area(file, addr, len, pgoff, flags);
	if (IS_ERR_VALUE(addr))
		return addr;

	if (addr > TASK_SIZE - len)
		return -ENOMEM;
	if (offset_in_page(addr))
		return -EINVAL;

	error = security_mmap_addr(addr);
	return error ? error : addr;
}

EXPORT_SYMBOL(get_unmapped_area);

/**
 * find_vma_intersection() - Look up the first VMA which intersects the interval
 * @mm: The process address space.
 * @start_addr: The inclusive start user address.
 * @end_addr: The exclusive end user address.
 *
 * Returns: The first VMA within the provided range, %NULL otherwise.  Assumes
 * start_addr < end_addr.
 */
struct vm_area_struct *find_vma_intersection(struct mm_struct *mm,
					     unsigned long start_addr,
					     unsigned long end_addr)
{
	unsigned long index = start_addr;

	mmap_assert_locked(mm);
	return mt_find(&mm->mm_mt, &index, end_addr - 1);
}
EXPORT_SYMBOL(find_vma_intersection);

/**
 * find_vma() - Find the VMA for a given address, or the next VMA.
 * @mm: The mm_struct to check
 * @addr: The address
 *
 * Returns: The VMA associated with addr, or the next VMA.
 * May return %NULL in the case of no VMA at addr or above.
 */
struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
	unsigned long index = addr;

	mmap_assert_locked(mm);
	return mt_find(&mm->mm_mt, &index, ULONG_MAX);
}
EXPORT_SYMBOL(find_vma);

/**
 * find_vma_prev() - Find the VMA for a given address, or the next vma and
 * set %pprev to the previous VMA, if any.
 * @mm: The mm_struct to check
 * @addr: The address
 * @pprev: The pointer to set to the previous VMA
 *
 * Note that RCU lock is missing here since the external mmap_lock() is used
 * instead.
 *
 * Returns: The VMA associated with @addr, or the next vma.
 * May return %NULL in the case of no vma at addr or above.
 */
struct vm_area_struct *
find_vma_prev(struct mm_struct *mm, unsigned long addr,
			struct vm_area_struct **pprev)
{
	struct vm_area_struct *vma;
	MA_STATE(mas, &mm->mm_mt, addr, addr);

	vma = mas_walk(&mas);
	*pprev = mas_prev(&mas, 0);
	if (!vma)
		vma = mas_next(&mas, ULONG_MAX);
	return vma;
}

/*
 * Verify that the stack growth is acceptable and
 * update accounting. This is shared with both the
 * grow-up and grow-down cases.
 */
static int acct_stack_growth(struct vm_area_struct *vma,
			     unsigned long size, unsigned long grow)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long new_start;

	/* address space limit tests */
	if (!may_expand_vm(mm, vma->vm_flags, grow))
		return -ENOMEM;

	/* Stack limit test */
	if (size > rlimit(RLIMIT_STACK))
		return -ENOMEM;

	/* mlock limit tests */
	if (mlock_future_check(mm, vma->vm_flags, grow << PAGE_SHIFT))
		return -ENOMEM;

	/* Check to ensure the stack will not grow into a hugetlb-only region */
	new_start = (vma->vm_flags & VM_GROWSUP) ? vma->vm_start :
			vma->vm_end - size;
	if (is_hugepage_only_range(vma->vm_mm, new_start, size))
		return -EFAULT;

	/*
	 * Overcommit..  This must be the final test, as it will
	 * update security statistics.
	 */
	if (security_vm_enough_memory_mm(mm, grow))
		return -ENOMEM;

	return 0;
}

#if defined(CONFIG_STACK_GROWSUP) || defined(CONFIG_IA64)
/*
 * PA-RISC uses this for its stack; IA64 for its Register Backing Store.
 * vma is the last one with address > vma->vm_end.  Have to extend vma.
 */
int expand_upwards(struct vm_area_struct *vma, unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *next;
	unsigned long gap_addr;
	int error = 0;
	MA_STATE(mas, &mm->mm_mt, 0, 0);

	if (!(vma->vm_flags & VM_GROWSUP))
		return -EFAULT;

	/* Guard against exceeding limits of the address space. */
	address &= PAGE_MASK;
	if (address >= (TASK_SIZE & PAGE_MASK))
		return -ENOMEM;
	address += PAGE_SIZE;

	/* Enforce stack_guard_gap */
	gap_addr = address + stack_guard_gap;

	/* Guard against overflow */
	if (gap_addr < address || gap_addr > TASK_SIZE)
		gap_addr = TASK_SIZE;

	next = find_vma_intersection(mm, vma->vm_end, gap_addr);
	if (next && vma_is_accessible(next)) {
		if (!(next->vm_flags & VM_GROWSUP))
			return -ENOMEM;
		/* Check that both stack segments have the same anon_vma? */
	}

	if (mas_preallocate(&mas, vma, GFP_KERNEL))
		return -ENOMEM;

	/* We must make sure the anon_vma is allocated. */
	if (unlikely(anon_vma_prepare(vma))) {
		mas_destroy(&mas);
		return -ENOMEM;
	}

	/*
	 * vma->vm_start/vm_end cannot change under us because the caller
	 * is required to hold the mmap_lock in read mode.  We need the
	 * anon_vma lock to serialize against concurrent expand_stacks.
	 */
	anon_vma_lock_write(vma->anon_vma);

	/* Somebody else might have raced and expanded it already */
	if (address > vma->vm_end) {
		unsigned long size, grow;

		size = address - vma->vm_start;
		grow = (address - vma->vm_end) >> PAGE_SHIFT;

		error = -ENOMEM;
		if (vma->vm_pgoff + (size >> PAGE_SHIFT) >= vma->vm_pgoff) {
			error = acct_stack_growth(vma, size, grow);
			if (!error) {
				/*
				 * We only hold a shared mmap_lock lock here, so
				 * we need to protect against concurrent vma
				 * expansions.  anon_vma_lock_write() doesn't
				 * help here, as we don't guarantee that all
				 * growable vmas in a mm share the same root
				 * anon vma.  So, we reuse mm->page_table_lock
				 * to guard against concurrent vma expansions.
				 */
				spin_lock(&mm->page_table_lock);
				if (vma->vm_flags & VM_LOCKED)
					mm->locked_vm += grow;
				vm_stat_account(mm, vma->vm_flags, grow);
				anon_vma_interval_tree_pre_update_vma(vma);
				vma->vm_end = address;
				/* Overwrite old entry in mtree. */
				vma_mas_store(vma, &mas);
				anon_vma_interval_tree_post_update_vma(vma);
				spin_unlock(&mm->page_table_lock);

				perf_event_mmap(vma);
			}
		}
	}
	anon_vma_unlock_write(vma->anon_vma);
	khugepaged_enter_vma(vma, vma->vm_flags);
	mas_destroy(&mas);
	return error;
}
#endif /* CONFIG_STACK_GROWSUP || CONFIG_IA64 */

/*
 * vma is the first one with address < vma->vm_start.  Have to extend vma.
 */
int expand_downwards(struct vm_area_struct *vma, unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	MA_STATE(mas, &mm->mm_mt, vma->vm_start, vma->vm_start);
	struct vm_area_struct *prev;
	int error = 0;

	address &= PAGE_MASK;
	if (address < mmap_min_addr)
		return -EPERM;

	/* Enforce stack_guard_gap */
	prev = mas_prev(&mas, 0);
	/* Check that both stack segments have the same anon_vma? */
	if (prev && !(prev->vm_flags & VM_GROWSDOWN) &&
			vma_is_accessible(prev)) {
		if (address - prev->vm_end < stack_guard_gap)
			return -ENOMEM;
	}

	if (mas_preallocate(&mas, vma, GFP_KERNEL))
		return -ENOMEM;

	/* We must make sure the anon_vma is allocated. */
	if (unlikely(anon_vma_prepare(vma))) {
		mas_destroy(&mas);
		return -ENOMEM;
	}

	/*
	 * vma->vm_start/vm_end cannot change under us because the caller
	 * is required to hold the mmap_lock in read mode.  We need the
	 * anon_vma lock to serialize against concurrent expand_stacks.
	 */
	anon_vma_lock_write(vma->anon_vma);

	/* Somebody else might have raced and expanded it already */
	if (address < vma->vm_start) {
		unsigned long size, grow;

		size = vma->vm_end - address;
		grow = (vma->vm_start - address) >> PAGE_SHIFT;

		error = -ENOMEM;
		if (grow <= vma->vm_pgoff) {
			error = acct_stack_growth(vma, size, grow);
			if (!error) {
				/*
				 * We only hold a shared mmap_lock lock here, so
				 * we need to protect against concurrent vma
				 * expansions.  anon_vma_lock_write() doesn't
				 * help here, as we don't guarantee that all
				 * growable vmas in a mm share the same root
				 * anon vma.  So, we reuse mm->page_table_lock
				 * to guard against concurrent vma expansions.
				 */
				spin_lock(&mm->page_table_lock);
				if (vma->vm_flags & VM_LOCKED)
					mm->locked_vm += grow;
				vm_stat_account(mm, vma->vm_flags, grow);
				anon_vma_interval_tree_pre_update_vma(vma);
				vma->vm_start = address;
				vma->vm_pgoff -= grow;
				/* Overwrite old entry in mtree. */
				vma_mas_store(vma, &mas);
				anon_vma_interval_tree_post_update_vma(vma);
				spin_unlock(&mm->page_table_lock);

				perf_event_mmap(vma);
			}
		}
	}
	anon_vma_unlock_write(vma->anon_vma);
	khugepaged_enter_vma(vma, vma->vm_flags);
	mas_destroy(&mas);
	return error;
}

/* enforced gap between the expanding stack and other mappings. */
unsigned long stack_guard_gap = 256UL<<PAGE_SHIFT;

static int __init cmdline_parse_stack_guard_gap(char *p)
{
	unsigned long val;
	char *endptr;

	val = simple_strtoul(p, &endptr, 10);
	if (!*endptr)
		stack_guard_gap = val << PAGE_SHIFT;

	return 1;
}
__setup("stack_guard_gap=", cmdline_parse_stack_guard_gap);

#ifdef CONFIG_STACK_GROWSUP
int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	return expand_upwards(vma, address);
}

struct vm_area_struct *
find_extend_vma(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma, *prev;

	addr &= PAGE_MASK;
	vma = find_vma_prev(mm, addr, &prev);
	if (vma && (vma->vm_start <= addr))
		return vma;
	if (!prev || expand_stack(prev, addr))
		return NULL;
	if (prev->vm_flags & VM_LOCKED)
		populate_vma_page_range(prev, addr, prev->vm_end, NULL);
	return prev;
}
#else
int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	return expand_downwards(vma, address);
}

struct vm_area_struct *
find_extend_vma(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma;
	unsigned long start;

	addr &= PAGE_MASK;
	vma = find_vma(mm, addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		return NULL;
	start = vma->vm_start;
	if (expand_stack(vma, addr))
		return NULL;
	if (vma->vm_flags & VM_LOCKED)
		populate_vma_page_range(vma, addr, start, NULL);
	return vma;
}
#endif

EXPORT_SYMBOL_GPL(find_extend_vma);

/*
 * Ok - we have the memory areas we should free on a maple tree so release them,
 * and do the vma updates.
 *
 * Called with the mm semaphore held.
 */
static inline void remove_mt(struct mm_struct *mm, struct ma_state *mas)
{
	unsigned long nr_accounted = 0;
	struct vm_area_struct *vma;

	/* Update high watermark before we lower total_vm */
	update_hiwater_vm(mm);
	mas_for_each(mas, vma, ULONG_MAX) {
		long nrpages = vma_pages(vma);

		if (vma->vm_flags & VM_ACCOUNT)
			nr_accounted += nrpages;
		vm_stat_account(mm, vma->vm_flags, -nrpages);
		remove_vma(vma);
	}
	vm_unacct_memory(nr_accounted);
	validate_mm(mm);
}

/*
 * Get rid of page table information in the indicated region.
 *
 * Called with the mm semaphore held.
 */
static void unmap_region(struct mm_struct *mm, struct maple_tree *mt,
		struct vm_area_struct *vma, struct vm_area_struct *prev,
		struct vm_area_struct *next,
		unsigned long start, unsigned long end)
{
	struct mmu_gather tlb;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm);
	update_hiwater_rss(mm);
	unmap_vmas(&tlb, mt, vma, start, end);
	free_pgtables(&tlb, mt, vma, prev ? prev->vm_end : FIRST_USER_ADDRESS,
				 next ? next->vm_start : USER_PGTABLES_CEILING);
	tlb_finish_mmu(&tlb);
}

/*
 * __split_vma() bypasses sysctl_max_map_count checking.  We use this where it
 * has already been checked or doesn't make sense to fail.
 */
int __split_vma(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long addr, int new_below)
{
	struct vm_area_struct *new;
	int err;
	validate_mm_mt(mm);

	if (vma->vm_ops && vma->vm_ops->may_split) {
		err = vma->vm_ops->may_split(vma, addr);
		if (err)
			return err;
	}

	new = vm_area_dup(vma);
	if (!new)
		return -ENOMEM;

	if (new_below)
		new->vm_end = addr;
	else {
		new->vm_start = addr;
		new->vm_pgoff += ((addr - vma->vm_start) >> PAGE_SHIFT);
	}

	err = vma_dup_policy(vma, new);
	if (err)
		goto out_free_vma;

	err = anon_vma_clone(new, vma);
	if (err)
		goto out_free_mpol;

	if (new->vm_file)
		get_file(new->vm_file);

	if (new->vm_ops && new->vm_ops->open)
		new->vm_ops->open(new);

	if (new_below)
		err = vma_adjust(vma, addr, vma->vm_end, vma->vm_pgoff +
			((addr - new->vm_start) >> PAGE_SHIFT), new);
	else
		err = vma_adjust(vma, vma->vm_start, addr, vma->vm_pgoff, new);

	/* Success. */
	if (!err)
		return 0;

	/* Avoid vm accounting in close() operation */
	new->vm_start = new->vm_end;
	new->vm_pgoff = 0;
	/* Clean everything up if vma_adjust failed. */
	if (new->vm_ops && new->vm_ops->close)
		new->vm_ops->close(new);
	if (new->vm_file)
		fput(new->vm_file);
	unlink_anon_vmas(new);
 out_free_mpol:
	mpol_put(vma_policy(new));
 out_free_vma:
	vm_area_free(new);
	validate_mm_mt(mm);
	return err;
}

/*
 * Split a vma into two pieces at address 'addr', a new vma is allocated
 * either for the first part or the tail.
 */
int split_vma(struct mm_struct *mm, struct vm_area_struct *vma,
	      unsigned long addr, int new_below)
{
	if (mm->map_count >= sysctl_max_map_count)
		return -ENOMEM;

	return __split_vma(mm, vma, addr, new_below);
}

/*
 * do_mas_align_munmap() - munmap the aligned region from @start to @end.
 * @mas: The maple_state, ideally set up to alter the correct tree location.
 * @vma: The starting vm_area_struct
 * @mm: The mm_struct
 * @start: The aligned start address to munmap.
 * @end: The aligned end address to munmap.
 * @uf: The userfaultfd list_head
 * @downgrade: Set to true to attempt a write downgrade of the mmap_sem
 *
 * If @downgrade is true, check return code for potential release of the lock.
 */
static int
do_mas_align_munmap(struct ma_state *mas, struct vm_area_struct *vma,
		    struct mm_struct *mm, unsigned long start,
		    unsigned long end, struct list_head *uf, bool downgrade)
{
	struct vm_area_struct *prev, *next = NULL;
	struct maple_tree mt_detach;
	int count = 0;
	int error = -ENOMEM;
	unsigned long locked_vm = 0;
	MA_STATE(mas_detach, &mt_detach, 0, 0);
	mt_init_flags(&mt_detach, mas->tree->ma_flags & MT_FLAGS_LOCK_MASK);
	mt_set_external_lock(&mt_detach, &mm->mmap_lock);

	if (mas_preallocate(mas, vma, GFP_KERNEL))
		return -ENOMEM;

	mas->last = end - 1;
	/*
	 * If we need to split any vma, do it now to save pain later.
	 *
	 * Note: mremap's move_vma VM_ACCOUNT handling assumes a partially
	 * unmapped vm_area_struct will remain in use: so lower split_vma
	 * places tmp vma above, and higher split_vma places tmp vma below.
	 */

	/* Does it split the first one? */
	if (start > vma->vm_start) {

		/*
		 * Make sure that map_count on return from munmap() will
		 * not exceed its limit; but let map_count go just above
		 * its limit temporarily, to help free resources as expected.
		 */
		if (end < vma->vm_end && mm->map_count >= sysctl_max_map_count)
			goto map_count_exceeded;

		/*
		 * mas_pause() is not needed since mas->index needs to be set
		 * differently than vma->vm_end anyways.
		 */
		error = __split_vma(mm, vma, start, 0);
		if (error)
			goto start_split_failed;

		mas_set(mas, start);
		vma = mas_walk(mas);
	}

	prev = mas_prev(mas, 0);
	if (unlikely((!prev)))
		mas_set(mas, start);

	/*
	 * Detach a range of VMAs from the mm. Using next as a temp variable as
	 * it is always overwritten.
	 */
	mas_for_each(mas, next, end - 1) {
		/* Does it split the end? */
		if (next->vm_end > end) {
			struct vm_area_struct *split;

			error = __split_vma(mm, next, end, 1);
			if (error)
				goto end_split_failed;

			mas_set(mas, end);
			split = mas_prev(mas, 0);
			mas_set_range(&mas_detach, split->vm_start, split->vm_end - 1);
			error = mas_store_gfp(&mas_detach, split, GFP_KERNEL);
			if (error)
				goto munmap_gather_failed;
			if (next->vm_flags & VM_LOCKED)
				locked_vm += vma_pages(split);

			count++;
			if (vma == next)
				vma = split;
			break;
		}
		mas_set_range(&mas_detach, next->vm_start, next->vm_end - 1);
		error = mas_store_gfp(&mas_detach, next, GFP_KERNEL);
		if (error)
			goto munmap_gather_failed;
		if (next->vm_flags & VM_LOCKED)
			locked_vm += vma_pages(next);

		count++;
#ifdef CONFIG_DEBUG_VM_MAPLE_TREE
		BUG_ON(next->vm_start < start);
		BUG_ON(next->vm_start > end);
#endif
	}

	if (!next)
		next = mas_next(mas, ULONG_MAX);

	if (unlikely(uf)) {
		/*
		 * If userfaultfd_unmap_prep returns an error the vmas
		 * will remain split, but userland will get a
		 * highly unexpected error anyway. This is no
		 * different than the case where the first of the two
		 * __split_vma fails, but we don't undo the first
		 * split, despite we could. This is unlikely enough
		 * failure that it's not worth optimizing it for.
		 */
		error = userfaultfd_unmap_prep(mm, start, end, uf);

		if (error)
			goto userfaultfd_error;
	}

	/* Point of no return */
	mas_set_range(mas, start, end - 1);
#if defined(CONFIG_DEBUG_VM_MAPLE_TREE)
	/* Make sure no VMAs are about to be lost. */
	{
		MA_STATE(test, &mt_detach, start, end - 1);
		struct vm_area_struct *vma_mas, *vma_test;
		int test_count = 0;

		rcu_read_lock();
		vma_test = mas_find(&test, end - 1);
		mas_for_each(mas, vma_mas, end - 1) {
			BUG_ON(vma_mas != vma_test);
			test_count++;
			vma_test = mas_next(&test, end - 1);
		}
		rcu_read_unlock();
		BUG_ON(count != test_count);
		mas_set_range(mas, start, end - 1);
	}
#endif
	/* Point of no return */
	mas_store_prealloc(mas, NULL);

	mm->locked_vm -= locked_vm;
	mm->map_count -= count;
	/*
	 * Do not downgrade mmap_lock if we are next to VM_GROWSDOWN or
	 * VM_GROWSUP VMA. Such VMAs can change their size under
	 * down_read(mmap_lock) and collide with the VMA we are about to unmap.
	 */
	if (downgrade) {
		if (next && (next->vm_flags & VM_GROWSDOWN))
			downgrade = false;
		else if (prev && (prev->vm_flags & VM_GROWSUP))
			downgrade = false;
		else
			mmap_write_downgrade(mm);
	}

	unmap_region(mm, &mt_detach, vma, prev, next, start, end);
	/* Statistics and freeing VMAs */
	mas_set(&mas_detach, start);
	remove_mt(mm, &mas_detach);
	__mt_destroy(&mt_detach);


	validate_mm(mm);
	return downgrade ? 1 : 0;

userfaultfd_error:
munmap_gather_failed:
end_split_failed:
	__mt_destroy(&mt_detach);
start_split_failed:
map_count_exceeded:
	mas_destroy(mas);
	return error;
}

/*
 * do_mas_munmap() - munmap a given range.
 * @mas: The maple state
 * @mm: The mm_struct
 * @start: The start address to munmap
 * @len: The length of the range to munmap
 * @uf: The userfaultfd list_head
 * @downgrade: set to true if the user wants to attempt to write_downgrade the
 * mmap_sem
 *
 * This function takes a @mas that is either pointing to the previous VMA or set
 * to MA_START and sets it up to remove the mapping(s).  The @len will be
 * aligned and any arch_unmap work will be preformed.
 *
 * Returns: -EINVAL on failure, 1 on success and unlock, 0 otherwise.
 */
int do_mas_munmap(struct ma_state *mas, struct mm_struct *mm,
		  unsigned long start, size_t len, struct list_head *uf,
		  bool downgrade)
{
	unsigned long end;
	struct vm_area_struct *vma;

	if ((offset_in_page(start)) || start > TASK_SIZE || len > TASK_SIZE-start)
		return -EINVAL;

	end = start + PAGE_ALIGN(len);
	if (end == start)
		return -EINVAL;

	 /* arch_unmap() might do unmaps itself.  */
	arch_unmap(mm, start, end);

	/* Find the first overlapping VMA */
	vma = mas_find(mas, end - 1);
	if (!vma)
		return 0;

	return do_mas_align_munmap(mas, vma, mm, start, end, uf, downgrade);
}

/* do_munmap() - Wrapper function for non-maple tree aware do_munmap() calls.
 * @mm: The mm_struct
 * @start: The start address to munmap
 * @len: The length to be munmapped.
 * @uf: The userfaultfd list_head
 */
int do_munmap(struct mm_struct *mm, unsigned long start, size_t len,
	      struct list_head *uf)
{
	MA_STATE(mas, &mm->mm_mt, start, start);

	return do_mas_munmap(&mas, mm, start, len, uf, false);
}

unsigned long mmap_region(struct file *file, unsigned long addr,
		unsigned long len, vm_flags_t vm_flags, unsigned long pgoff,
		struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	struct vm_area_struct *next, *prev, *merge;
	pgoff_t pglen = len >> PAGE_SHIFT;
	unsigned long charged = 0;
	unsigned long end = addr + len;
	unsigned long merge_start = addr, merge_end = end;
	pgoff_t vm_pgoff;
	int error;
	MA_STATE(mas, &mm->mm_mt, addr, end - 1);

	/* Check against address space limit. */
	if (!may_expand_vm(mm, vm_flags, len >> PAGE_SHIFT)) {
		unsigned long nr_pages;

		/*
		 * MAP_FIXED may remove pages of mappings that intersects with
		 * requested mapping. Account for the pages it would unmap.
		 */
		nr_pages = count_vma_pages_range(mm, addr, end);

		if (!may_expand_vm(mm, vm_flags,
					(len >> PAGE_SHIFT) - nr_pages))
			return -ENOMEM;
	}

	/* Unmap any existing mapping in the area */
	if (do_mas_munmap(&mas, mm, addr, len, uf, false))
		return -ENOMEM;

	/*
	 * Private writable mapping: check memory availability
	 */
	if (accountable_mapping(file, vm_flags)) {
		charged = len >> PAGE_SHIFT;
		if (security_vm_enough_memory_mm(mm, charged))
			return -ENOMEM;
		vm_flags |= VM_ACCOUNT;
	}

	next = mas_next(&mas, ULONG_MAX);
	prev = mas_prev(&mas, 0);
	if (vm_flags & VM_SPECIAL)
		goto cannot_expand;

	/* Attempt to expand an old mapping */
	/* Check next */
	if (next && next->vm_start == end && !vma_policy(next) &&
	    can_vma_merge_before(next, vm_flags, NULL, file, pgoff+pglen,
				 NULL_VM_UFFD_CTX, NULL)) {
		merge_end = next->vm_end;
		vma = next;
		vm_pgoff = next->vm_pgoff - pglen;
	}

	/* Check prev */
	if (prev && prev->vm_end == addr && !vma_policy(prev) &&
	    (vma ? can_vma_merge_after(prev, vm_flags, vma->anon_vma, file,
				       pgoff, vma->vm_userfaultfd_ctx, NULL) :
		   can_vma_merge_after(prev, vm_flags, NULL, file, pgoff,
				       NULL_VM_UFFD_CTX, NULL))) {
		merge_start = prev->vm_start;
		vma = prev;
		vm_pgoff = prev->vm_pgoff;
	}


	/* Actually expand, if possible */
	if (vma &&
	    !vma_expand(&mas, vma, merge_start, merge_end, vm_pgoff, next)) {
		khugepaged_enter_vma(vma, vm_flags);
		goto expanded;
	}

	mas.index = addr;
	mas.last = end - 1;
cannot_expand:
	/*
	 * Determine the object being mapped and call the appropriate
	 * specific mapper. the address has already been validated, but
	 * not unmapped, but the maps are removed from the list.
	 */
	vma = vm_area_alloc(mm);
	if (!vma) {
		error = -ENOMEM;
		goto unacct_error;
	}

	vma->vm_start = addr;
	vma->vm_end = end;
	vma->vm_flags = vm_flags;
	vma->vm_page_prot = vm_get_page_prot(vm_flags);
	vma->vm_pgoff = pgoff;

	if (file) {
		if (vm_flags & VM_SHARED) {
			error = mapping_map_writable(file->f_mapping);
			if (error)
				goto free_vma;
		}

		vma->vm_file = get_file(file);
		error = call_mmap(file, vma);
		if (error)
			goto unmap_and_free_vma;

		/*
		 * Expansion is handled above, merging is handled below.
		 * Drivers should not alter the address of the VMA.
		 */
		if (WARN_ON((addr != vma->vm_start))) {
			error = -EINVAL;
			goto close_and_free_vma;
		}
		mas_reset(&mas);

		/*
		 * If vm_flags changed after call_mmap(), we should try merge
		 * vma again as we may succeed this time.
		 */
		if (unlikely(vm_flags != vma->vm_flags && prev)) {
			merge = vma_merge(mm, prev, vma->vm_start, vma->vm_end, vma->vm_flags,
				NULL, vma->vm_file, vma->vm_pgoff, NULL, NULL_VM_UFFD_CTX, NULL);
			if (merge) {
				/*
				 * ->mmap() can change vma->vm_file and fput
				 * the original file. So fput the vma->vm_file
				 * here or we would add an extra fput for file
				 * and cause general protection fault
				 * ultimately.
				 */
				fput(vma->vm_file);
				vm_area_free(vma);
				vma = merge;
				/* Update vm_flags to pick up the change. */
				vm_flags = vma->vm_flags;
				goto unmap_writable;
			}
		}

		vm_flags = vma->vm_flags;
	} else if (vm_flags & VM_SHARED) {
		error = shmem_zero_setup(vma);
		if (error)
			goto free_vma;
	} else {
		vma_set_anonymous(vma);
	}

	/* Allow architectures to sanity-check the vm_flags */
	if (!arch_validate_flags(vma->vm_flags)) {
		error = -EINVAL;
		if (file)
			goto close_and_free_vma;
		else if (vma->vm_file)
			goto unmap_and_free_vma;
		else
			goto free_vma;
	}

	if (mas_preallocate(&mas, vma, GFP_KERNEL)) {
		error = -ENOMEM;
		if (file)
			goto close_and_free_vma;
		else if (vma->vm_file)
			goto unmap_and_free_vma;
		else
			goto free_vma;
	}

	if (vma->vm_file)
		i_mmap_lock_write(vma->vm_file->f_mapping);

	vma_mas_store(vma, &mas);
	mm->map_count++;
	if (vma->vm_file) {
		if (vma->vm_flags & VM_SHARED)
			mapping_allow_writable(vma->vm_file->f_mapping);

		flush_dcache_mmap_lock(vma->vm_file->f_mapping);
		vma_interval_tree_insert(vma, &vma->vm_file->f_mapping->i_mmap);
		flush_dcache_mmap_unlock(vma->vm_file->f_mapping);
		i_mmap_unlock_write(vma->vm_file->f_mapping);
	}

	/*
	 * vma_merge() calls khugepaged_enter_vma() either, the below
	 * call covers the non-merge case.
	 */
	khugepaged_enter_vma(vma, vma->vm_flags);

	/* Once vma denies write, undo our temporary denial count */
unmap_writable:
	if (file && vm_flags & VM_SHARED)
		mapping_unmap_writable(file->f_mapping);
	file = vma->vm_file;
expanded:
	perf_event_mmap(vma);

	vm_stat_account(mm, vm_flags, len >> PAGE_SHIFT);
	if (vm_flags & VM_LOCKED) {
		if ((vm_flags & VM_SPECIAL) || vma_is_dax(vma) ||
					is_vm_hugetlb_page(vma) ||
					vma == get_gate_vma(current->mm))
			vma->vm_flags &= VM_LOCKED_CLEAR_MASK;
		else
			mm->locked_vm += (len >> PAGE_SHIFT);
	}

	if (file)
		uprobe_mmap(vma);

	/*
	 * New (or expanded) vma always get soft dirty status.
	 * Otherwise user-space soft-dirty page tracker won't
	 * be able to distinguish situation when vma area unmapped,
	 * then new mapped in-place (which must be aimed as
	 * a completely new data area).
	 */
	vma->vm_flags |= VM_SOFTDIRTY;

	vma_set_page_prot(vma);

	validate_mm(mm);
	return addr;

close_and_free_vma:
	if (vma->vm_ops && vma->vm_ops->close)
		vma->vm_ops->close(vma);
unmap_and_free_vma:
	fput(vma->vm_file);
	vma->vm_file = NULL;

	/* Undo any partial mapping done by a device driver. */
	unmap_region(mm, mas.tree, vma, prev, next, vma->vm_start, vma->vm_end);
	if (file && (vm_flags & VM_SHARED))
		mapping_unmap_writable(file->f_mapping);
free_vma:
	vm_area_free(vma);
unacct_error:
	if (charged)
		vm_unacct_memory(charged);
	validate_mm(mm);
	return error;
}

static int __vm_munmap(unsigned long start, size_t len, bool downgrade)
{
	int ret;
	struct mm_struct *mm = current->mm;
	LIST_HEAD(uf);
	MA_STATE(mas, &mm->mm_mt, start, start);

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = do_mas_munmap(&mas, mm, start, len, &uf, downgrade);
	/*
	 * Returning 1 indicates mmap_lock is downgraded.
	 * But 1 is not legal return value of vm_munmap() and munmap(), reset
	 * it to 0 before return.
	 */
	if (ret == 1) {
		mmap_read_unlock(mm);
		ret = 0;
	} else
		mmap_write_unlock(mm);

	userfaultfd_unmap_complete(mm, &uf);
	return ret;
}

int vm_munmap(unsigned long start, size_t len)
{
	return __vm_munmap(start, len, false);
}
EXPORT_SYMBOL(vm_munmap);

SYSCALL_DEFINE2(munmap, unsigned long, addr, size_t, len)
{
	addr = untagged_addr(addr);
	return __vm_munmap(addr, len, true);
}


/*
 * Emulation of deprecated remap_file_pages() syscall.
 */
SYSCALL_DEFINE5(remap_file_pages, unsigned long, start, unsigned long, size,
		unsigned long, prot, unsigned long, pgoff, unsigned long, flags)
{

	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long populate = 0;
	unsigned long ret = -EINVAL;
	struct file *file;

	pr_warn_once("%s (%d) uses deprecated remap_file_pages() syscall. See Documentation/mm/remap_file_pages.rst.\n",
		     current->comm, current->pid);

	if (prot)
		return ret;
	start = start & PAGE_MASK;
	size = size & PAGE_MASK;

	if (start + size <= start)
		return ret;

	/* Does pgoff wrap? */
	if (pgoff + (size >> PAGE_SHIFT) < pgoff)
		return ret;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	vma = vma_lookup(mm, start);

	if (!vma || !(vma->vm_flags & VM_SHARED))
		goto out;

	if (start + size > vma->vm_end) {
		VMA_ITERATOR(vmi, mm, vma->vm_end);
		struct vm_area_struct *next, *prev = vma;

		for_each_vma_range(vmi, next, start + size) {
			/* hole between vmas ? */
			if (next->vm_start != prev->vm_end)
				goto out;

			if (next->vm_file != vma->vm_file)
				goto out;

			if (next->vm_flags != vma->vm_flags)
				goto out;

			if (start + size <= next->vm_end)
				break;

			prev = next;
		}

		if (!next)
			goto out;
	}

	prot |= vma->vm_flags & VM_READ ? PROT_READ : 0;
	prot |= vma->vm_flags & VM_WRITE ? PROT_WRITE : 0;
	prot |= vma->vm_flags & VM_EXEC ? PROT_EXEC : 0;

	flags &= MAP_NONBLOCK;
	flags |= MAP_SHARED | MAP_FIXED | MAP_POPULATE;
	if (vma->vm_flags & VM_LOCKED)
		flags |= MAP_LOCKED;

	file = get_file(vma->vm_file);
	ret = do_mmap(vma->vm_file, start, size,
			prot, flags, pgoff, &populate, NULL);
	fput(file);
out:
	mmap_write_unlock(mm);
	if (populate)
		mm_populate(ret, populate);
	if (!IS_ERR_VALUE(ret))
		ret = 0;
	return ret;
}

/*
 * brk_munmap() - Unmap a parital vma.
 * @mas: The maple tree state.
 * @vma: The vma to be modified
 * @newbrk: the start of the address to unmap
 * @oldbrk: The end of the address to unmap
 * @uf: The userfaultfd list_head
 *
 * Returns: 1 on success.
 * unmaps a partial VMA mapping.  Does not handle alignment, downgrades lock if
 * possible.
 */
static int do_brk_munmap(struct ma_state *mas, struct vm_area_struct *vma,
			 unsigned long newbrk, unsigned long oldbrk,
			 struct list_head *uf)
{
	struct mm_struct *mm = vma->vm_mm;
	int ret;

	arch_unmap(mm, newbrk, oldbrk);
	ret = do_mas_align_munmap(mas, vma, mm, newbrk, oldbrk, uf, true);
	validate_mm_mt(mm);
	return ret;
}

/*
 * do_brk_flags() - Increase the brk vma if the flags match.
 * @mas: The maple tree state.
 * @addr: The start address
 * @len: The length of the increase
 * @vma: The vma,
 * @flags: The VMA Flags
 *
 * Extend the brk VMA from addr to addr + len.  If the VMA is NULL or the flags
 * do not match then create a new anonymous VMA.  Eventually we may be able to
 * do some brk-specific accounting here.
 */
static int do_brk_flags(struct ma_state *mas, struct vm_area_struct *vma,
		unsigned long addr, unsigned long len, unsigned long flags)
{
	struct mm_struct *mm = current->mm;

	validate_mm_mt(mm);
	/*
	 * Check against address space limits by the changed size
	 * Note: This happens *after* clearing old mappings in some code paths.
	 */
	flags |= VM_DATA_DEFAULT_FLAGS | VM_ACCOUNT | mm->def_flags;
	if (!may_expand_vm(mm, flags, len >> PAGE_SHIFT))
		return -ENOMEM;

	if (mm->map_count > sysctl_max_map_count)
		return -ENOMEM;

	if (security_vm_enough_memory_mm(mm, len >> PAGE_SHIFT))
		return -ENOMEM;

	/*
	 * Expand the existing vma if possible; Note that singular lists do not
	 * occur after forking, so the expand will only happen on new VMAs.
	 */
	if (vma && vma->vm_end == addr && !vma_policy(vma) &&
	    can_vma_merge_after(vma, flags, NULL, NULL,
				addr >> PAGE_SHIFT, NULL_VM_UFFD_CTX, NULL)) {
		mas_set_range(mas, vma->vm_start, addr + len - 1);
		if (mas_preallocate(mas, vma, GFP_KERNEL))
			return -ENOMEM;

		vma_adjust_trans_huge(vma, vma->vm_start, addr + len, 0);
		if (vma->anon_vma) {
			anon_vma_lock_write(vma->anon_vma);
			anon_vma_interval_tree_pre_update_vma(vma);
		}
		vma->vm_end = addr + len;
		vma->vm_flags |= VM_SOFTDIRTY;
		mas_store_prealloc(mas, vma);

		if (vma->anon_vma) {
			anon_vma_interval_tree_post_update_vma(vma);
			anon_vma_unlock_write(vma->anon_vma);
		}
		khugepaged_enter_vma(vma, flags);
		goto out;
	}

	/* create a vma struct for an anonymous mapping */
	vma = vm_area_alloc(mm);
	if (!vma)
		goto vma_alloc_fail;

	vma_set_anonymous(vma);
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_pgoff = addr >> PAGE_SHIFT;
	vma->vm_flags = flags;
	vma->vm_page_prot = vm_get_page_prot(flags);
	mas_set_range(mas, vma->vm_start, addr + len - 1);
	if (mas_store_gfp(mas, vma, GFP_KERNEL))
		goto mas_store_fail;

	mm->map_count++;
out:
	perf_event_mmap(vma);
	mm->total_vm += len >> PAGE_SHIFT;
	mm->data_vm += len >> PAGE_SHIFT;
	if (flags & VM_LOCKED)
		mm->locked_vm += (len >> PAGE_SHIFT);
	vma->vm_flags |= VM_SOFTDIRTY;
	validate_mm(mm);
	return 0;

mas_store_fail:
	vm_area_free(vma);
vma_alloc_fail:
	vm_unacct_memory(len >> PAGE_SHIFT);
	return -ENOMEM;
}

int vm_brk_flags(unsigned long addr, unsigned long request, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	unsigned long len;
	int ret;
	bool populate;
	LIST_HEAD(uf);
	MA_STATE(mas, &mm->mm_mt, addr, addr);

	len = PAGE_ALIGN(request);
	if (len < request)
		return -ENOMEM;
	if (!len)
		return 0;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	/* Until we need other flags, refuse anything except VM_EXEC. */
	if ((flags & (~VM_EXEC)) != 0)
		return -EINVAL;

	ret = check_brk_limits(addr, len);
	if (ret)
		goto limits_failed;

	ret = do_mas_munmap(&mas, mm, addr, len, &uf, 0);
	if (ret)
		goto munmap_failed;

	vma = mas_prev(&mas, 0);
	ret = do_brk_flags(&mas, vma, addr, len, flags);
	populate = ((mm->def_flags & VM_LOCKED) != 0);
	mmap_write_unlock(mm);
	userfaultfd_unmap_complete(mm, &uf);
	if (populate && !ret)
		mm_populate(addr, len);
	return ret;

munmap_failed:
limits_failed:
	mmap_write_unlock(mm);
	return ret;
}
EXPORT_SYMBOL(vm_brk_flags);

int vm_brk(unsigned long addr, unsigned long len)
{
	return vm_brk_flags(addr, len, 0);
}
EXPORT_SYMBOL(vm_brk);

/* Release all mmaps. */
void exit_mmap(struct mm_struct *mm)
{
	struct mmu_gather tlb;
	struct vm_area_struct *vma;
	unsigned long nr_accounted = 0;
	MA_STATE(mas, &mm->mm_mt, 0, 0);
	int count = 0;

	/* mm's last user has gone, and its about to be pulled down */
	mmu_notifier_release(mm);

	mmap_read_lock(mm);
	arch_exit_mmap(mm);

	vma = mas_find(&mas, ULONG_MAX);
	if (!vma) {
		/* Can happen if dup_mmap() received an OOM */
		mmap_read_unlock(mm);
		return;
	}

	lru_add_drain();
	flush_cache_mm(mm);
	tlb_gather_mmu_fullmm(&tlb, mm);
	/* update_hiwater_rss(mm) here? but nobody should be looking */
	/* Use ULONG_MAX here to ensure all VMAs in the mm are unmapped */
	unmap_vmas(&tlb, &mm->mm_mt, vma, 0, ULONG_MAX);
	mmap_read_unlock(mm);

	/*
	 * Set MMF_OOM_SKIP to hide this task from the oom killer/reaper
	 * because the memory has been already freed.
	 */
	set_bit(MMF_OOM_SKIP, &mm->flags);
	mmap_write_lock(mm);
	mt_clear_in_rcu(&mm->mm_mt);
	free_pgtables(&tlb, &mm->mm_mt, vma, FIRST_USER_ADDRESS,
		      USER_PGTABLES_CEILING);
	tlb_finish_mmu(&tlb);

	/*
	 * Walk the list again, actually closing and freeing it, with preemption
	 * enabled, without holding any MM locks besides the unreachable
	 * mmap_write_lock.
	 */
	do {
		if (vma->vm_flags & VM_ACCOUNT)
			nr_accounted += vma_pages(vma);
		remove_vma(vma);
		count++;
		cond_resched();
	} while ((vma = mas_find(&mas, ULONG_MAX)) != NULL);

	BUG_ON(count != mm->map_count);

	trace_exit_mmap(mm);
	__mt_destroy(&mm->mm_mt);
	mmap_write_unlock(mm);
	vm_unacct_memory(nr_accounted);
}

/* Insert vm structure into process list sorted by address
 * and into the inode's i_mmap tree.  If vm_file is non-NULL
 * then i_mmap_rwsem is taken here.
 */
int insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vma)
{
	unsigned long charged = vma_pages(vma);


	if (find_vma_intersection(mm, vma->vm_start, vma->vm_end))
		return -ENOMEM;

	if ((vma->vm_flags & VM_ACCOUNT) &&
	     security_vm_enough_memory_mm(mm, charged))
		return -ENOMEM;

	/*
	 * The vm_pgoff of a purely anonymous vma should be irrelevant
	 * until its first write fault, when page's anon_vma and index
	 * are set.  But now set the vm_pgoff it will almost certainly
	 * end up with (unless mremap moves it elsewhere before that
	 * first wfault), so /proc/pid/maps tells a consistent story.
	 *
	 * By setting it to reflect the virtual start address of the
	 * vma, merges and splits can happen in a seamless way, just
	 * using the existing file pgoff checks and manipulations.
	 * Similarly in do_mmap and in do_brk_flags.
	 */
	if (vma_is_anonymous(vma)) {
		BUG_ON(vma->anon_vma);
		vma->vm_pgoff = vma->vm_start >> PAGE_SHIFT;
	}

	if (vma_link(mm, vma)) {
		vm_unacct_memory(charged);
		return -ENOMEM;
	}

	return 0;
}

/*
 * Copy the vma structure to a new location in the same mm,
 * prior to moving page table entries, to effect an mremap move.
 */
struct vm_area_struct *copy_vma(struct vm_area_struct **vmap,
	unsigned long addr, unsigned long len, pgoff_t pgoff,
	bool *need_rmap_locks)
{
	struct vm_area_struct *vma = *vmap;
	unsigned long vma_start = vma->vm_start;
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *new_vma, *prev;
	bool faulted_in_anon_vma = true;

	validate_mm_mt(mm);
	/*
	 * If anonymous vma has not yet been faulted, update new pgoff
	 * to match new location, to increase its chance of merging.
	 */
	if (unlikely(vma_is_anonymous(vma) && !vma->anon_vma)) {
		pgoff = addr >> PAGE_SHIFT;
		faulted_in_anon_vma = false;
	}

	new_vma = find_vma_prev(mm, addr, &prev);
	if (new_vma && new_vma->vm_start < addr + len)
		return NULL;	/* should never get here */

	new_vma = vma_merge(mm, prev, addr, addr + len, vma->vm_flags,
			    vma->anon_vma, vma->vm_file, pgoff, vma_policy(vma),
			    vma->vm_userfaultfd_ctx, anon_vma_name(vma));
	if (new_vma) {
		/*
		 * Source vma may have been merged into new_vma
		 */
		if (unlikely(vma_start >= new_vma->vm_start &&
			     vma_start < new_vma->vm_end)) {
			/*
			 * The only way we can get a vma_merge with
			 * self during an mremap is if the vma hasn't
			 * been faulted in yet and we were allowed to
			 * reset the dst vma->vm_pgoff to the
			 * destination address of the mremap to allow
			 * the merge to happen. mremap must change the
			 * vm_pgoff linearity between src and dst vmas
			 * (in turn preventing a vma_merge) to be
			 * safe. It is only safe to keep the vm_pgoff
			 * linear if there are no pages mapped yet.
			 */
			VM_BUG_ON_VMA(faulted_in_anon_vma, new_vma);
			*vmap = vma = new_vma;
		}
		*need_rmap_locks = (new_vma->vm_pgoff <= vma->vm_pgoff);
	} else {
		new_vma = vm_area_dup(vma);
		if (!new_vma)
			goto out;
		new_vma->vm_start = addr;
		new_vma->vm_end = addr + len;
		new_vma->vm_pgoff = pgoff;
		if (vma_dup_policy(vma, new_vma))
			goto out_free_vma;
		if (anon_vma_clone(new_vma, vma))
			goto out_free_mempol;
		if (new_vma->vm_file)
			get_file(new_vma->vm_file);
		if (new_vma->vm_ops && new_vma->vm_ops->open)
			new_vma->vm_ops->open(new_vma);
		if (vma_link(mm, new_vma))
			goto out_vma_link;
		*need_rmap_locks = false;
	}
	validate_mm_mt(mm);
	return new_vma;

out_vma_link:
	if (new_vma->vm_ops && new_vma->vm_ops->close)
		new_vma->vm_ops->close(new_vma);

	if (new_vma->vm_file)
		fput(new_vma->vm_file);

	unlink_anon_vmas(new_vma);
out_free_mempol:
	mpol_put(vma_policy(new_vma));
out_free_vma:
	vm_area_free(new_vma);
out:
	validate_mm_mt(mm);
	return NULL;
}

/*
 * Return true if the calling process may expand its vm space by the passed
 * number of pages
 */
bool may_expand_vm(struct mm_struct *mm, vm_flags_t flags, unsigned long npages)
{
	if (mm->total_vm + npages > rlimit(RLIMIT_AS) >> PAGE_SHIFT)
		return false;

	if (is_data_mapping(flags) &&
	    mm->data_vm + npages > rlimit(RLIMIT_DATA) >> PAGE_SHIFT) {
		/* Workaround for Valgrind */
		if (rlimit(RLIMIT_DATA) == 0 &&
		    mm->data_vm + npages <= rlimit_max(RLIMIT_DATA) >> PAGE_SHIFT)
			return true;

		pr_warn_once("%s (%d): VmData %lu exceed data ulimit %lu. Update limits%s.\n",
			     current->comm, current->pid,
			     (mm->data_vm + npages) << PAGE_SHIFT,
			     rlimit(RLIMIT_DATA),
			     ignore_rlimit_data ? "" : " or use boot option ignore_rlimit_data");

		if (!ignore_rlimit_data)
			return false;
	}

	return true;
}

void vm_stat_account(struct mm_struct *mm, vm_flags_t flags, long npages)
{
	WRITE_ONCE(mm->total_vm, READ_ONCE(mm->total_vm)+npages);

	if (is_exec_mapping(flags))
		mm->exec_vm += npages;
	else if (is_stack_mapping(flags))
		mm->stack_vm += npages;
	else if (is_data_mapping(flags))
		mm->data_vm += npages;
}

static vm_fault_t special_mapping_fault(struct vm_fault *vmf);

/*
 * Having a close hook prevents vma merging regardless of flags.
 */
static void special_mapping_close(struct vm_area_struct *vma)
{
}

static const char *special_mapping_name(struct vm_area_struct *vma)
{
	return ((struct vm_special_mapping *)vma->vm_private_data)->name;
}

static int special_mapping_mremap(struct vm_area_struct *new_vma)
{
	struct vm_special_mapping *sm = new_vma->vm_private_data;

	if (WARN_ON_ONCE(current->mm != new_vma->vm_mm))
		return -EFAULT;

	if (sm->mremap)
		return sm->mremap(sm, new_vma);

	return 0;
}

static int special_mapping_split(struct vm_area_struct *vma, unsigned long addr)
{
	/*
	 * Forbid splitting special mappings - kernel has expectations over
	 * the number of pages in mapping. Together with VM_DONTEXPAND
	 * the size of vma should stay the same over the special mapping's
	 * lifetime.
	 */
	return -EINVAL;
}

static const struct vm_operations_struct special_mapping_vmops = {
	.close = special_mapping_close,
	.fault = special_mapping_fault,
	.mremap = special_mapping_mremap,
	.name = special_mapping_name,
	/* vDSO code relies that VVAR can't be accessed remotely */
	.access = NULL,
	.may_split = special_mapping_split,
};

static const struct vm_operations_struct legacy_special_mapping_vmops = {
	.close = special_mapping_close,
	.fault = special_mapping_fault,
};

static vm_fault_t special_mapping_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	pgoff_t pgoff;
	struct page **pages;

	if (vma->vm_ops == &legacy_special_mapping_vmops) {
		pages = vma->vm_private_data;
	} else {
		struct vm_special_mapping *sm = vma->vm_private_data;

		if (sm->fault)
			return sm->fault(sm, vmf->vma, vmf);

		pages = sm->pages;
	}

	for (pgoff = vmf->pgoff; pgoff && *pages; ++pages)
		pgoff--;

	if (*pages) {
		struct page *page = *pages;
		get_page(page);
		vmf->page = page;
		return 0;
	}

	return VM_FAULT_SIGBUS;
}

static struct vm_area_struct *__install_special_mapping(
	struct mm_struct *mm,
	unsigned long addr, unsigned long len,
	unsigned long vm_flags, void *priv,
	const struct vm_operations_struct *ops)
{
	int ret;
	struct vm_area_struct *vma;

	validate_mm_mt(mm);
	vma = vm_area_alloc(mm);
	if (unlikely(vma == NULL))
		return ERR_PTR(-ENOMEM);

	vma->vm_start = addr;
	vma->vm_end = addr + len;

	vma->vm_flags = vm_flags | mm->def_flags | VM_DONTEXPAND | VM_SOFTDIRTY;
	vma->vm_flags &= VM_LOCKED_CLEAR_MASK;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	vma->vm_ops = ops;
	vma->vm_private_data = priv;

	ret = insert_vm_struct(mm, vma);
	if (ret)
		goto out;

	vm_stat_account(mm, vma->vm_flags, len >> PAGE_SHIFT);

	perf_event_mmap(vma);

	validate_mm_mt(mm);
	return vma;

out:
	vm_area_free(vma);
	validate_mm_mt(mm);
	return ERR_PTR(ret);
}

bool vma_is_special_mapping(const struct vm_area_struct *vma,
	const struct vm_special_mapping *sm)
{
	return vma->vm_private_data == sm &&
		(vma->vm_ops == &special_mapping_vmops ||
		 vma->vm_ops == &legacy_special_mapping_vmops);
}

/*
 * Called with mm->mmap_lock held for writing.
 * Insert a new vma covering the given region, with the given flags.
 * Its pages are supplied by the given array of struct page *.
 * The array can be shorter than len >> PAGE_SHIFT if it's null-terminated.
 * The region past the last page supplied will always produce SIGBUS.
 * The array pointer and the pages it points to are assumed to stay alive
 * for as long as this mapping might exist.
 */
struct vm_area_struct *_install_special_mapping(
	struct mm_struct *mm,
	unsigned long addr, unsigned long len,
	unsigned long vm_flags, const struct vm_special_mapping *spec)
{
	return __install_special_mapping(mm, addr, len, vm_flags, (void *)spec,
					&special_mapping_vmops);
}

int install_special_mapping(struct mm_struct *mm,
			    unsigned long addr, unsigned long len,
			    unsigned long vm_flags, struct page **pages)
{
	struct vm_area_struct *vma = __install_special_mapping(
		mm, addr, len, vm_flags, (void *)pages,
		&legacy_special_mapping_vmops);

	return PTR_ERR_OR_ZERO(vma);
}

static DEFINE_MUTEX(mm_all_locks_mutex);

static void vm_lock_anon_vma(struct mm_struct *mm, struct anon_vma *anon_vma)
{
	if (!test_bit(0, (unsigned long *) &anon_vma->root->rb_root.rb_root.rb_node)) {
		/*
		 * The LSB of head.next can't change from under us
		 * because we hold the mm_all_locks_mutex.
		 */
		down_write_nest_lock(&anon_vma->root->rwsem, &mm->mmap_lock);
		/*
		 * We can safely modify head.next after taking the
		 * anon_vma->root->rwsem. If some other vma in this mm shares
		 * the same anon_vma we won't take it again.
		 *
		 * No need of atomic instructions here, head.next
		 * can't change from under us thanks to the
		 * anon_vma->root->rwsem.
		 */
		if (__test_and_set_bit(0, (unsigned long *)
				       &anon_vma->root->rb_root.rb_root.rb_node))
			BUG();
	}
}

static void vm_lock_mapping(struct mm_struct *mm, struct address_space *mapping)
{
	if (!test_bit(AS_MM_ALL_LOCKS, &mapping->flags)) {
		/*
		 * AS_MM_ALL_LOCKS can't change from under us because
		 * we hold the mm_all_locks_mutex.
		 *
		 * Operations on ->flags have to be atomic because
		 * even if AS_MM_ALL_LOCKS is stable thanks to the
		 * mm_all_locks_mutex, there may be other cpus
		 * changing other bitflags in parallel to us.
		 */
		if (test_and_set_bit(AS_MM_ALL_LOCKS, &mapping->flags))
			BUG();
		down_write_nest_lock(&mapping->i_mmap_rwsem, &mm->mmap_lock);
	}
}

/*
 * This operation locks against the VM for all pte/vma/mm related
 * operations that could ever happen on a certain mm. This includes
 * vmtruncate, try_to_unmap, and all page faults.
 *
 * The caller must take the mmap_lock in write mode before calling
 * mm_take_all_locks(). The caller isn't allowed to release the
 * mmap_lock until mm_drop_all_locks() returns.
 *
 * mmap_lock in write mode is required in order to block all operations
 * that could modify pagetables and free pages without need of
 * altering the vma layout. It's also needed in write mode to avoid new
 * anon_vmas to be associated with existing vmas.
 *
 * A single task can't take more than one mm_take_all_locks() in a row
 * or it would deadlock.
 *
 * The LSB in anon_vma->rb_root.rb_node and the AS_MM_ALL_LOCKS bitflag in
 * mapping->flags avoid to take the same lock twice, if more than one
 * vma in this mm is backed by the same anon_vma or address_space.
 *
 * We take locks in following order, accordingly to comment at beginning
 * of mm/rmap.c:
 *   - all hugetlbfs_i_mmap_rwsem_key locks (aka mapping->i_mmap_rwsem for
 *     hugetlb mapping);
 *   - all i_mmap_rwsem locks;
 *   - all anon_vma->rwseml
 *
 * We can take all locks within these types randomly because the VM code
 * doesn't nest them and we protected from parallel mm_take_all_locks() by
 * mm_all_locks_mutex.
 *
 * mm_take_all_locks() and mm_drop_all_locks are expensive operations
 * that may have to take thousand of locks.
 *
 * mm_take_all_locks() can fail if it's interrupted by signals.
 */
int mm_take_all_locks(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	struct anon_vma_chain *avc;
	MA_STATE(mas, &mm->mm_mt, 0, 0);

	mmap_assert_write_locked(mm);

	mutex_lock(&mm_all_locks_mutex);

	mas_for_each(&mas, vma, ULONG_MAX) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->vm_file && vma->vm_file->f_mapping &&
				is_vm_hugetlb_page(vma))
			vm_lock_mapping(mm, vma->vm_file->f_mapping);
	}

	mas_set(&mas, 0);
	mas_for_each(&mas, vma, ULONG_MAX) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->vm_file && vma->vm_file->f_mapping &&
				!is_vm_hugetlb_page(vma))
			vm_lock_mapping(mm, vma->vm_file->f_mapping);
	}

	mas_set(&mas, 0);
	mas_for_each(&mas, vma, ULONG_MAX) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->anon_vma)
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				vm_lock_anon_vma(mm, avc->anon_vma);
	}

	return 0;

out_unlock:
	mm_drop_all_locks(mm);
	return -EINTR;
}

static void vm_unlock_anon_vma(struct anon_vma *anon_vma)
{
	if (test_bit(0, (unsigned long *) &anon_vma->root->rb_root.rb_root.rb_node)) {
		/*
		 * The LSB of head.next can't change to 0 from under
		 * us because we hold the mm_all_locks_mutex.
		 *
		 * We must however clear the bitflag before unlocking
		 * the vma so the users using the anon_vma->rb_root will
		 * never see our bitflag.
		 *
		 * No need of atomic instructions here, head.next
		 * can't change from under us until we release the
		 * anon_vma->root->rwsem.
		 */
		if (!__test_and_clear_bit(0, (unsigned long *)
					  &anon_vma->root->rb_root.rb_root.rb_node))
			BUG();
		anon_vma_unlock_write(anon_vma);
	}
}

static void vm_unlock_mapping(struct address_space *mapping)
{
	if (test_bit(AS_MM_ALL_LOCKS, &mapping->flags)) {
		/*
		 * AS_MM_ALL_LOCKS can't change to 0 from under us
		 * because we hold the mm_all_locks_mutex.
		 */
		i_mmap_unlock_write(mapping);
		if (!test_and_clear_bit(AS_MM_ALL_LOCKS,
					&mapping->flags))
			BUG();
	}
}

/*
 * The mmap_lock cannot be released by the caller until
 * mm_drop_all_locks() returns.
 */
void mm_drop_all_locks(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	struct anon_vma_chain *avc;
	MA_STATE(mas, &mm->mm_mt, 0, 0);

	mmap_assert_write_locked(mm);
	BUG_ON(!mutex_is_locked(&mm_all_locks_mutex));

	mas_for_each(&mas, vma, ULONG_MAX) {
		if (vma->anon_vma)
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				vm_unlock_anon_vma(avc->anon_vma);
		if (vma->vm_file && vma->vm_file->f_mapping)
			vm_unlock_mapping(vma->vm_file->f_mapping);
	}

	mutex_unlock(&mm_all_locks_mutex);
}

/*
 * initialise the percpu counter for VM
 */
void __init mmap_init(void)
{
	int ret;

	ret = percpu_counter_init(&vm_committed_as, 0, GFP_KERNEL);
	VM_BUG_ON(ret);
}

/*
 * Initialise sysctl_user_reserve_kbytes.
 *
 * This is intended to prevent a user from starting a single memory hogging
 * process, such that they cannot recover (kill the hog) in OVERCOMMIT_NEVER
 * mode.
 *
 * The default value is min(3% of free memory, 128MB)
 * 128MB is enough to recover with sshd/login, bash, and top/kill.
 */
static int init_user_reserve(void)
{
	unsigned long free_kbytes;

	free_kbytes = global_zone_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

	sysctl_user_reserve_kbytes = min(free_kbytes / 32, 1UL << 17);
	return 0;
}
subsys_initcall(init_user_reserve);

/*
 * Initialise sysctl_admin_reserve_kbytes.
 *
 * The purpose of sysctl_admin_reserve_kbytes is to allow the sys admin
 * to log in and kill a memory hogging process.
 *
 * Systems with more than 256MB will reserve 8MB, enough to recover
 * with sshd, bash, and top in OVERCOMMIT_GUESS. Smaller systems will
 * only reserve 3% of free pages by default.
 */
static int init_admin_reserve(void)
{
	unsigned long free_kbytes;

	free_kbytes = global_zone_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

	sysctl_admin_reserve_kbytes = min(free_kbytes / 32, 1UL << 13);
	return 0;
}
subsys_initcall(init_admin_reserve);

/*
 * Reinititalise user and admin reserves if memory is added or removed.
 *
 * The default user reserve max is 128MB, and the default max for the
 * admin reserve is 8MB. These are usually, but not always, enough to
 * enable recovery from a memory hogging process using login/sshd, a shell,
 * and tools like top. It may make sense to increase or even disable the
 * reserve depending on the existence of swap or variations in the recovery
 * tools. So, the admin may have changed them.
 *
 * If memory is added and the reserves have been eliminated or increased above
 * the default max, then we'll trust the admin.
 *
 * If memory is removed and there isn't enough free memory, then we
 * need to reset the reserves.
 *
 * Otherwise keep the reserve set by the admin.
 */
static int reserve_mem_notifier(struct notifier_block *nb,
			     unsigned long action, void *data)
{
	unsigned long tmp, free_kbytes;

	switch (action) {
	case MEM_ONLINE:
		/* Default max is 128MB. Leave alone if modified by operator. */
		tmp = sysctl_user_reserve_kbytes;
		if (0 < tmp && tmp < (1UL << 17))
			init_user_reserve();

		/* Default max is 8MB.  Leave alone if modified by operator. */
		tmp = sysctl_admin_reserve_kbytes;
		if (0 < tmp && tmp < (1UL << 13))
			init_admin_reserve();

		break;
	case MEM_OFFLINE:
		free_kbytes = global_zone_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

		if (sysctl_user_reserve_kbytes > free_kbytes) {
			init_user_reserve();
			pr_info("vm.user_reserve_kbytes reset to %lu\n",
				sysctl_user_reserve_kbytes);
		}

		if (sysctl_admin_reserve_kbytes > free_kbytes) {
			init_admin_reserve();
			pr_info("vm.admin_reserve_kbytes reset to %lu\n",
				sysctl_admin_reserve_kbytes);
		}
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block reserve_mem_nb = {
	.notifier_call = reserve_mem_notifier,
};

static int __meminit init_reserve_notifier(void)
{
	if (register_hotmemory_notifier(&reserve_mem_nb))
		pr_err("Failed registering memory add/remove notifier for admin reserve\n");

	return 0;
}
subsys_initcall(init_reserve_notifier);
