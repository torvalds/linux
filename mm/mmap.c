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
#include <linux/ksm.h>
#include <linux/memfd.h>

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
int mmap_rnd_bits_max __ro_after_init = CONFIG_ARCH_MMAP_RND_BITS_MAX;
int mmap_rnd_bits __read_mostly = CONFIG_ARCH_MMAP_RND_BITS;
#endif
#ifdef CONFIG_HAVE_ARCH_MMAP_RND_COMPAT_BITS
const int mmap_rnd_compat_bits_min = CONFIG_ARCH_MMAP_RND_COMPAT_BITS_MIN;
const int mmap_rnd_compat_bits_max = CONFIG_ARCH_MMAP_RND_COMPAT_BITS_MAX;
int mmap_rnd_compat_bits __read_mostly = CONFIG_ARCH_MMAP_RND_COMPAT_BITS;
#endif

static bool ignore_rlimit_data;
core_param(ignore_rlimit_data, ignore_rlimit_data, bool, 0644);

/* Update vma->vm_page_prot to reflect vma->vm_flags. */
void vma_set_page_prot(struct vm_area_struct *vma)
{
	vm_flags_t vm_flags = vma->vm_flags;
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

	return mlock_future_ok(current->mm, current->mm->def_flags, len)
		? 0 : -EAGAIN;
}

SYSCALL_DEFINE1(brk, unsigned long, brk)
{
	unsigned long newbrk, oldbrk, origbrk;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *brkvma, *next = NULL;
	unsigned long min_brk;
	bool populate = false;
	LIST_HEAD(uf);
	struct vma_iterator vmi;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	origbrk = mm->brk;

	min_brk = mm->start_brk;
#ifdef CONFIG_COMPAT_BRK
	/*
	 * CONFIG_COMPAT_BRK can still be overridden by setting
	 * randomize_va_space to 2, which will still cause mm->start_brk
	 * to be arbitrarily shifted
	 */
	if (!current->brk_randomized)
		min_brk = mm->end_data;
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

	/* Always allow shrinking brk. */
	if (brk <= mm->brk) {
		/* Search one past newbrk */
		vma_iter_init(&vmi, mm, newbrk);
		brkvma = vma_find(&vmi, oldbrk);
		if (!brkvma || brkvma->vm_start >= oldbrk)
			goto out; /* mapping intersects with an existing non-brk vma. */
		/*
		 * mm->brk must be protected by write mmap_lock.
		 * do_vmi_align_munmap() will drop the lock on success,  so
		 * update it before calling do_vma_munmap().
		 */
		mm->brk = brk;
		if (do_vmi_align_munmap(&vmi, brkvma, mm, newbrk, oldbrk, &uf,
					/* unlock = */ true))
			goto out;

		goto success_unlocked;
	}

	if (check_brk_limits(oldbrk, newbrk - oldbrk))
		goto out;

	/*
	 * Only check if the next VMA is within the stack_guard_gap of the
	 * expansion area
	 */
	vma_iter_init(&vmi, mm, oldbrk);
	next = vma_find(&vmi, newbrk + PAGE_SIZE + stack_guard_gap);
	if (next && newbrk + PAGE_SIZE > vm_start_gap(next))
		goto out;

	brkvma = vma_prev_limit(&vmi, mm->start_brk);
	/* Ok, looks good - let it rip. */
	if (do_brk_flags(&vmi, brkvma, oldbrk, newbrk - oldbrk, 0) < 0)
		goto out;

	mm->brk = brk;
	if (mm->def_flags & VM_LOCKED)
		populate = true;

success:
	mmap_write_unlock(mm);
success_unlocked:
	userfaultfd_unmap_complete(mm, &uf);
	if (populate)
		mm_populate(oldbrk, newbrk - oldbrk);
	return brk;

out:
	mm->brk = origbrk;
	mmap_write_unlock(mm);
	return origbrk;
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

bool mlock_future_ok(const struct mm_struct *mm, vm_flags_t vm_flags,
			unsigned long bytes)
{
	unsigned long locked_pages, limit_pages;

	if (!(vm_flags & VM_LOCKED) || capable(CAP_IPC_LOCK))
		return true;

	locked_pages = bytes >> PAGE_SHIFT;
	locked_pages += mm->locked_vm;

	limit_pages = rlimit(RLIMIT_MEMLOCK);
	limit_pages >>= PAGE_SHIFT;

	return locked_pages <= limit_pages;
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
	if (file->f_op->fop_flags & FOP_UNSIGNED_OFFSET)
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

/**
 * do_mmap() - Perform a userland memory mapping into the current process
 * address space of length @len with protection bits @prot, mmap flags @flags
 * (from which VMA flags will be inferred), and any additional VMA flags to
 * apply @vm_flags. If this is a file-backed mapping then the file is specified
 * in @file and page offset into the file via @pgoff.
 *
 * This function does not perform security checks on the file and assumes, if
 * @uf is non-NULL, the caller has provided a list head to track unmap events
 * for userfaultfd @uf.
 *
 * It also simply indicates whether memory population is required by setting
 * @populate, which must be non-NULL, expecting the caller to actually perform
 * this task itself if appropriate.
 *
 * This function will invoke architecture-specific (and if provided and
 * relevant, file system-specific) logic to determine the most appropriate
 * unmapped area in which to place the mapping if not MAP_FIXED.
 *
 * Callers which require userland mmap() behaviour should invoke vm_mmap(),
 * which is also exported for module use.
 *
 * Those which require this behaviour less security checks, userfaultfd and
 * populate behaviour, and who handle the mmap write lock themselves, should
 * call this function.
 *
 * Note that the returned address may reside within a merged VMA if an
 * appropriate merge were to take place, so it doesn't necessarily specify the
 * start of a VMA, rather only the start of a valid mapped range of length
 * @len bytes, rounded down to the nearest page size.
 *
 * The caller must write-lock current->mm->mmap_lock.
 *
 * @file: An optional struct file pointer describing the file which is to be
 * mapped, if a file-backed mapping.
 * @addr: If non-zero, hints at (or if @flags has MAP_FIXED set, specifies) the
 * address at which to perform this mapping. See mmap (2) for details. Must be
 * page-aligned.
 * @len: The length of the mapping. Will be page-aligned and must be at least 1
 * page in size.
 * @prot: Protection bits describing access required to the mapping. See mmap
 * (2) for details.
 * @flags: Flags specifying how the mapping should be performed, see mmap (2)
 * for details.
 * @vm_flags: VMA flags which should be set by default, or 0 otherwise.
 * @pgoff: Page offset into the @file if file-backed, should be 0 otherwise.
 * @populate: A pointer to a value which will be set to 0 if no population of
 * the range is required, or the number of bytes to populate if it is. Must be
 * non-NULL. See mmap (2) for details as to under what circumstances population
 * of the range occurs.
 * @uf: An optional pointer to a list head to track userfaultfd unmap events
 * should unmapping events arise. If provided, it is up to the caller to manage
 * this.
 *
 * Returns: Either an error, or the address at which the requested mapping has
 * been performed.
 */
unsigned long do_mmap(struct file *file, unsigned long addr,
			unsigned long len, unsigned long prot,
			unsigned long flags, vm_flags_t vm_flags,
			unsigned long pgoff, unsigned long *populate,
			struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	int pkey = 0;

	*populate = 0;

	mmap_assert_write_locked(mm);

	if (!len)
		return -EINVAL;

	/*
	 * Does the application expect PROT_READ to imply PROT_EXEC?
	 *
	 * (the exception is when the underlying filesystem is noexec
	 *  mounted, in which case we don't add PROT_EXEC.)
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

	/*
	 * addr is returned from get_unmapped_area,
	 * There are two cases:
	 * 1> MAP_FIXED == false
	 *	unallocated memory, no need to check sealing.
	 * 1> MAP_FIXED == true
	 *	sealing is checked inside mmap_region when
	 *	do_vmi_munmap is called.
	 */

	if (prot == PROT_EXEC) {
		pkey = execute_only_pkey(mm);
		if (pkey < 0)
			pkey = 0;
	}

	/* Do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 */
	vm_flags |= calc_vm_prot_bits(prot, pkey) | calc_vm_flag_bits(file, flags) |
			mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

	/* Obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */
	addr = __get_unmapped_area(file, addr, len, pgoff, flags, vm_flags);
	if (IS_ERR_VALUE(addr))
		return addr;

	if (flags & MAP_FIXED_NOREPLACE) {
		if (find_vma_intersection(mm, addr, addr + len))
			return -EEXIST;
	}

	if (flags & MAP_LOCKED)
		if (!can_do_mlock())
			return -EPERM;

	if (!mlock_future_ok(mm, vm_flags, len))
		return -EAGAIN;

	if (file) {
		struct inode *inode = file_inode(file);
		unsigned long flags_mask;
		int err;

		if (!file_mmap_ok(file, inode, pgoff, len))
			return -EOVERFLOW;

		flags_mask = LEGACY_MAP_MASK;
		if (file->f_op->fop_flags & FOP_MMAP_SYNC)
			flags_mask |= MAP_SYNC;

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

			if (!can_mmap_file(file))
				return -ENODEV;
			if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))
				return -EINVAL;
			break;

		default:
			return -EINVAL;
		}

		/*
		 * Check to see if we are violating any seals and update VMA
		 * flags if necessary to avoid future seal violations.
		 */
		err = memfd_check_seals_mmap(file, &vm_flags);
		if (err)
			return (unsigned long)err;
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
		case MAP_DROPPABLE:
			if (VM_DROPPABLE == VM_NONE)
				return -ENOTSUPP;
			/*
			 * A locked or stack area makes no sense to be droppable.
			 *
			 * Also, since droppable pages can just go away at any time
			 * it makes no sense to copy them on fork or dump them.
			 *
			 * And don't attempt to combine with hugetlb for now.
			 */
			if (flags & (MAP_LOCKED | MAP_HUGETLB))
			        return -EINVAL;
			if (vm_flags & (VM_GROWSDOWN | VM_GROWSUP))
			        return -EINVAL;

			vm_flags |= VM_DROPPABLE;

			/*
			 * If the pages can be dropped, then it doesn't make
			 * sense to reserve them.
			 */
			vm_flags |= VM_NORESERVE;

			/*
			 * Likewise, they're volatile enough that they
			 * shouldn't survive forks or coredumps.
			 */
			vm_flags |= VM_WIPEONFORK | VM_DONTDUMP;
			fallthrough;
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
 * Determine if the allocation needs to ensure that there is no
 * existing mapping within it's guard gaps, for use as start_gap.
 */
static inline unsigned long stack_guard_placement(vm_flags_t vm_flags)
{
	if (vm_flags & VM_SHADOW_STACK)
		return PAGE_SIZE;

	return 0;
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
			  unsigned long flags, vm_flags_t vm_flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct vm_unmapped_area_info info = {};
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

	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = mmap_end;
	info.start_gap = stack_guard_placement(vm_flags);
	if (filp && is_file_hugepages(filp))
		info.align_mask = huge_page_mask_align(filp);
	return vm_unmapped_area(&info);
}

#ifndef HAVE_ARCH_UNMAPPED_AREA
unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		       unsigned long len, unsigned long pgoff,
		       unsigned long flags, vm_flags_t vm_flags)
{
	return generic_get_unmapped_area(filp, addr, len, pgoff, flags,
					 vm_flags);
}
#endif

/*
 * This mmap-allocator allocates new areas top-down from below the
 * stack's low limit (the base):
 */
unsigned long
generic_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
				  unsigned long len, unsigned long pgoff,
				  unsigned long flags, vm_flags_t vm_flags)
{
	struct vm_area_struct *vma, *prev;
	struct mm_struct *mm = current->mm;
	struct vm_unmapped_area_info info = {};
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
	info.low_limit = PAGE_SIZE;
	info.high_limit = arch_get_mmap_base(addr, mm->mmap_base);
	info.start_gap = stack_guard_placement(vm_flags);
	if (filp && is_file_hugepages(filp))
		info.align_mask = huge_page_mask_align(filp);
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
			       unsigned long flags, vm_flags_t vm_flags)
{
	return generic_get_unmapped_area_topdown(filp, addr, len, pgoff, flags,
						 vm_flags);
}
#endif

unsigned long mm_get_unmapped_area_vmflags(struct mm_struct *mm, struct file *filp,
					   unsigned long addr, unsigned long len,
					   unsigned long pgoff, unsigned long flags,
					   vm_flags_t vm_flags)
{
	if (mm_flags_test(MMF_TOPDOWN, mm))
		return arch_get_unmapped_area_topdown(filp, addr, len, pgoff,
						      flags, vm_flags);
	return arch_get_unmapped_area(filp, addr, len, pgoff, flags, vm_flags);
}

unsigned long
__get_unmapped_area(struct file *file, unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags, vm_flags_t vm_flags)
{
	unsigned long (*get_area)(struct file *, unsigned long,
				  unsigned long, unsigned long, unsigned long)
				  = NULL;

	unsigned long error = arch_mmap_check(addr, len, flags);
	if (error)
		return error;

	/* Careful about overflows.. */
	if (len > TASK_SIZE)
		return -ENOMEM;

	if (file) {
		if (file->f_op->get_unmapped_area)
			get_area = file->f_op->get_unmapped_area;
	} else if (flags & MAP_SHARED) {
		/*
		 * mmap_region() will call shmem_zero_setup() to create a file,
		 * so use shmem's get_unmapped_area in case it can be huge.
		 */
		get_area = shmem_get_unmapped_area;
	}

	/* Always treat pgoff as zero for anonymous memory. */
	if (!file)
		pgoff = 0;

	if (get_area) {
		addr = get_area(file, addr, len, pgoff, flags);
	} else if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) && !file
		   && !addr /* no hint */
		   && IS_ALIGNED(len, PMD_SIZE)) {
		/* Ensures that larger anonymous mappings are THP aligned. */
		addr = thp_get_unmapped_area_vmflags(file, addr, len,
						     pgoff, flags, vm_flags);
	} else {
		addr = mm_get_unmapped_area_vmflags(current->mm, file, addr, len,
						    pgoff, flags, vm_flags);
	}
	if (IS_ERR_VALUE(addr))
		return addr;

	if (addr > TASK_SIZE - len)
		return -ENOMEM;
	if (offset_in_page(addr))
		return -EINVAL;

	error = security_mmap_addr(addr);
	return error ? error : addr;
}

unsigned long
mm_get_unmapped_area(struct mm_struct *mm, struct file *file,
		     unsigned long addr, unsigned long len,
		     unsigned long pgoff, unsigned long flags)
{
	return mm_get_unmapped_area_vmflags(mm, file, addr, len,
					    pgoff, flags, 0);
}
EXPORT_SYMBOL(mm_get_unmapped_area);

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
	VMA_ITERATOR(vmi, mm, addr);

	vma = vma_iter_load(&vmi);
	*pprev = vma_prev(&vmi);
	if (!vma)
		vma = vma_next(&vmi);
	return vma;
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
int expand_stack_locked(struct vm_area_struct *vma, unsigned long address)
{
	return expand_upwards(vma, address);
}

struct vm_area_struct *find_extend_vma_locked(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma, *prev;

	addr &= PAGE_MASK;
	vma = find_vma_prev(mm, addr, &prev);
	if (vma && (vma->vm_start <= addr))
		return vma;
	if (!prev)
		return NULL;
	if (expand_stack_locked(prev, addr))
		return NULL;
	if (prev->vm_flags & VM_LOCKED)
		populate_vma_page_range(prev, addr, prev->vm_end, NULL);
	return prev;
}
#else
int expand_stack_locked(struct vm_area_struct *vma, unsigned long address)
{
	return expand_downwards(vma, address);
}

struct vm_area_struct *find_extend_vma_locked(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma;
	unsigned long start;

	addr &= PAGE_MASK;
	vma = find_vma(mm, addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	start = vma->vm_start;
	if (expand_stack_locked(vma, addr))
		return NULL;
	if (vma->vm_flags & VM_LOCKED)
		populate_vma_page_range(vma, addr, start, NULL);
	return vma;
}
#endif

#if defined(CONFIG_STACK_GROWSUP)

#define vma_expand_up(vma,addr) expand_upwards(vma, addr)
#define vma_expand_down(vma, addr) (-EFAULT)

#else

#define vma_expand_up(vma,addr) (-EFAULT)
#define vma_expand_down(vma, addr) expand_downwards(vma, addr)

#endif

/*
 * expand_stack(): legacy interface for page faulting. Don't use unless
 * you have to.
 *
 * This is called with the mm locked for reading, drops the lock, takes
 * the lock for writing, tries to look up a vma again, expands it if
 * necessary, and downgrades the lock to reading again.
 *
 * If no vma is found or it can't be expanded, it returns NULL and has
 * dropped the lock.
 */
struct vm_area_struct *expand_stack(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma, *prev;

	mmap_read_unlock(mm);
	if (mmap_write_lock_killable(mm))
		return NULL;

	vma = find_vma_prev(mm, addr, &prev);
	if (vma && vma->vm_start <= addr)
		goto success;

	if (prev && !vma_expand_up(prev, addr)) {
		vma = prev;
		goto success;
	}

	if (vma && !vma_expand_down(vma, addr))
		goto success;

	mmap_write_unlock(mm);
	return NULL;

success:
	mmap_write_downgrade(mm);
	return vma;
}

/* do_munmap() - Wrapper function for non-maple tree aware do_munmap() calls.
 * @mm: The mm_struct
 * @start: The start address to munmap
 * @len: The length to be munmapped.
 * @uf: The userfaultfd list_head
 *
 * Return: 0 on success, error otherwise.
 */
int do_munmap(struct mm_struct *mm, unsigned long start, size_t len,
	      struct list_head *uf)
{
	VMA_ITERATOR(vmi, mm, start);

	return do_vmi_munmap(&vmi, mm, start, len, uf, false);
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
	vm_flags_t vm_flags;

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

	if (mmap_read_lock_killable(mm))
		return -EINTR;

	/*
	 * Look up VMA under read lock first so we can perform the security
	 * without holding locks (which can be problematic). We reacquire a
	 * write lock later and check nothing changed underneath us.
	 */
	vma = vma_lookup(mm, start);

	if (!vma || !(vma->vm_flags & VM_SHARED)) {
		mmap_read_unlock(mm);
		return -EINVAL;
	}

	prot |= vma->vm_flags & VM_READ ? PROT_READ : 0;
	prot |= vma->vm_flags & VM_WRITE ? PROT_WRITE : 0;
	prot |= vma->vm_flags & VM_EXEC ? PROT_EXEC : 0;

	flags &= MAP_NONBLOCK;
	flags |= MAP_SHARED | MAP_FIXED | MAP_POPULATE;
	if (vma->vm_flags & VM_LOCKED)
		flags |= MAP_LOCKED;

	/* Save vm_flags used to calculate prot and flags, and recheck later. */
	vm_flags = vma->vm_flags;
	file = get_file(vma->vm_file);

	mmap_read_unlock(mm);

	/* Call outside mmap_lock to be consistent with other callers. */
	ret = security_mmap_file(file, prot, flags);
	if (ret) {
		fput(file);
		return ret;
	}

	ret = -EINVAL;

	/* OK security check passed, take write lock + let it rip. */
	if (mmap_write_lock_killable(mm)) {
		fput(file);
		return -EINTR;
	}

	vma = vma_lookup(mm, start);

	if (!vma)
		goto out;

	/* Make sure things didn't change under us. */
	if (vma->vm_flags != vm_flags)
		goto out;
	if (vma->vm_file != file)
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

	ret = do_mmap(vma->vm_file, start, size,
			prot, flags, 0, pgoff, &populate, NULL);
out:
	mmap_write_unlock(mm);
	fput(file);
	if (populate)
		mm_populate(ret, populate);
	if (!IS_ERR_VALUE(ret))
		ret = 0;
	return ret;
}

int vm_brk_flags(unsigned long addr, unsigned long request, vm_flags_t vm_flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	unsigned long len;
	int ret;
	bool populate;
	LIST_HEAD(uf);
	VMA_ITERATOR(vmi, mm, addr);

	len = PAGE_ALIGN(request);
	if (len < request)
		return -ENOMEM;
	if (!len)
		return 0;

	/* Until we need other flags, refuse anything except VM_EXEC. */
	if ((vm_flags & (~VM_EXEC)) != 0)
		return -EINVAL;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = check_brk_limits(addr, len);
	if (ret)
		goto limits_failed;

	ret = do_vmi_munmap(&vmi, mm, addr, len, &uf, 0);
	if (ret)
		goto munmap_failed;

	vma = vma_prev(&vmi);
	ret = do_brk_flags(&vmi, vma, addr, len, vm_flags);
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

/* Release all mmaps. */
void exit_mmap(struct mm_struct *mm)
{
	struct mmu_gather tlb;
	struct vm_area_struct *vma;
	unsigned long nr_accounted = 0;
	VMA_ITERATOR(vmi, mm, 0);
	int count = 0;

	/* mm's last user has gone, and its about to be pulled down */
	mmu_notifier_release(mm);

	mmap_read_lock(mm);
	arch_exit_mmap(mm);

	vma = vma_next(&vmi);
	if (!vma || unlikely(xa_is_zero(vma))) {
		/* Can happen if dup_mmap() received an OOM */
		mmap_read_unlock(mm);
		mmap_write_lock(mm);
		goto destroy;
	}

	flush_cache_mm(mm);
	tlb_gather_mmu_fullmm(&tlb, mm);
	/* update_hiwater_rss(mm) here? but nobody should be looking */
	/* Use ULONG_MAX here to ensure all VMAs in the mm are unmapped */
	unmap_vmas(&tlb, &vmi.mas, vma, 0, ULONG_MAX, ULONG_MAX, false);
	mmap_read_unlock(mm);

	/*
	 * Set MMF_OOM_SKIP to hide this task from the oom killer/reaper
	 * because the memory has been already freed.
	 */
	mm_flags_set(MMF_OOM_SKIP, mm);
	mmap_write_lock(mm);
	mt_clear_in_rcu(&mm->mm_mt);
	vma_iter_set(&vmi, vma->vm_end);
	free_pgtables(&tlb, &vmi.mas, vma, FIRST_USER_ADDRESS,
		      USER_PGTABLES_CEILING, true);
	tlb_finish_mmu(&tlb);

	/*
	 * Walk the list again, actually closing and freeing it, with preemption
	 * enabled, without holding any MM locks besides the unreachable
	 * mmap_write_lock.
	 */
	vma_iter_set(&vmi, vma->vm_end);
	do {
		if (vma->vm_flags & VM_ACCOUNT)
			nr_accounted += vma_pages(vma);
		vma_mark_detached(vma);
		remove_vma(vma);
		count++;
		cond_resched();
		vma = vma_next(&vmi);
	} while (vma && likely(!xa_is_zero(vma)));

	BUG_ON(count != mm->map_count);

	trace_exit_mmap(mm);
destroy:
	__mt_destroy(&mm->mm_mt);
	mmap_write_unlock(mm);
	vm_unacct_memory(nr_accounted);
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
 * Close hook, called for unmap() and on the old vma for mremap().
 *
 * Having a close hook prevents vma merging regardless of flags.
 */
static void special_mapping_close(struct vm_area_struct *vma)
{
	const struct vm_special_mapping *sm = vma->vm_private_data;

	if (sm->close)
		sm->close(sm, vma);
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

static vm_fault_t special_mapping_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	pgoff_t pgoff;
	struct page **pages;
	struct vm_special_mapping *sm = vma->vm_private_data;

	if (sm->fault)
		return sm->fault(sm, vmf->vma, vmf);

	pages = sm->pages;

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
	vm_flags_t vm_flags, void *priv,
	const struct vm_operations_struct *ops)
{
	int ret;
	struct vm_area_struct *vma;

	vma = vm_area_alloc(mm);
	if (unlikely(vma == NULL))
		return ERR_PTR(-ENOMEM);

	vma_set_range(vma, addr, addr + len, 0);
	vm_flags_init(vma, (vm_flags | mm->def_flags |
		      VM_DONTEXPAND | VM_SOFTDIRTY) & ~VM_LOCKED_MASK);
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	vma->vm_ops = ops;
	vma->vm_private_data = priv;

	ret = insert_vm_struct(mm, vma);
	if (ret)
		goto out;

	vm_stat_account(mm, vma->vm_flags, len >> PAGE_SHIFT);

	perf_event_mmap(vma);

	return vma;

out:
	vm_area_free(vma);
	return ERR_PTR(ret);
}

bool vma_is_special_mapping(const struct vm_area_struct *vma,
	const struct vm_special_mapping *sm)
{
	return vma->vm_private_data == sm &&
		vma->vm_ops == &special_mapping_vmops;
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
	vm_flags_t vm_flags, const struct vm_special_mapping *spec)
{
	return __install_special_mapping(mm, addr, len, vm_flags, (void *)spec,
					&special_mapping_vmops);
}

#ifdef CONFIG_SYSCTL
#if defined(HAVE_ARCH_PICK_MMAP_LAYOUT) || \
		defined(CONFIG_ARCH_WANT_DEFAULT_TOPDOWN_MMAP_LAYOUT)
int sysctl_legacy_va_layout;
#endif

static const struct ctl_table mmap_table[] = {
		{
				.procname       = "max_map_count",
				.data           = &sysctl_max_map_count,
				.maxlen         = sizeof(sysctl_max_map_count),
				.mode           = 0644,
				.proc_handler   = proc_dointvec_minmax,
				.extra1         = SYSCTL_ZERO,
		},
#if defined(HAVE_ARCH_PICK_MMAP_LAYOUT) || \
		defined(CONFIG_ARCH_WANT_DEFAULT_TOPDOWN_MMAP_LAYOUT)
		{
				.procname       = "legacy_va_layout",
				.data           = &sysctl_legacy_va_layout,
				.maxlen         = sizeof(sysctl_legacy_va_layout),
				.mode           = 0644,
				.proc_handler   = proc_dointvec_minmax,
				.extra1         = SYSCTL_ZERO,
		},
#endif
#ifdef CONFIG_HAVE_ARCH_MMAP_RND_BITS
		{
				.procname       = "mmap_rnd_bits",
				.data           = &mmap_rnd_bits,
				.maxlen         = sizeof(mmap_rnd_bits),
				.mode           = 0600,
				.proc_handler   = proc_dointvec_minmax,
				.extra1         = (void *)&mmap_rnd_bits_min,
				.extra2         = (void *)&mmap_rnd_bits_max,
		},
#endif
#ifdef CONFIG_HAVE_ARCH_MMAP_RND_COMPAT_BITS
		{
				.procname       = "mmap_rnd_compat_bits",
				.data           = &mmap_rnd_compat_bits,
				.maxlen         = sizeof(mmap_rnd_compat_bits),
				.mode           = 0600,
				.proc_handler   = proc_dointvec_minmax,
				.extra1         = (void *)&mmap_rnd_compat_bits_min,
				.extra2         = (void *)&mmap_rnd_compat_bits_max,
		},
#endif
};
#endif /* CONFIG_SYSCTL */

/*
 * initialise the percpu counter for VM, initialise VMA state.
 */
void __init mmap_init(void)
{
	int ret;

	ret = percpu_counter_init(&vm_committed_as, 0, GFP_KERNEL);
	VM_BUG_ON(ret);
#ifdef CONFIG_SYSCTL
	register_sysctl_init("vm", mmap_table);
#endif
	vma_state_init();
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

	free_kbytes = K(global_zone_page_state(NR_FREE_PAGES));

	sysctl_user_reserve_kbytes = min(free_kbytes / 32, SZ_128K);
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

	free_kbytes = K(global_zone_page_state(NR_FREE_PAGES));

	sysctl_admin_reserve_kbytes = min(free_kbytes / 32, SZ_8K);
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
		if (tmp > 0 && tmp < SZ_128K)
			init_user_reserve();

		/* Default max is 8MB.  Leave alone if modified by operator. */
		tmp = sysctl_admin_reserve_kbytes;
		if (tmp > 0 && tmp < SZ_8K)
			init_admin_reserve();

		break;
	case MEM_OFFLINE:
		free_kbytes = K(global_zone_page_state(NR_FREE_PAGES));

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

static int __meminit init_reserve_notifier(void)
{
	if (hotplug_memory_notifier(reserve_mem_notifier, DEFAULT_CALLBACK_PRI))
		pr_err("Failed registering memory add/remove notifier for admin reserve\n");

	return 0;
}
subsys_initcall(init_reserve_notifier);

/*
 * Obtain a read lock on mm->mmap_lock, if the specified address is below the
 * start of the VMA, the intent is to perform a write, and it is a
 * downward-growing stack, then attempt to expand the stack to contain it.
 *
 * This function is intended only for obtaining an argument page from an ELF
 * image, and is almost certainly NOT what you want to use for any other
 * purpose.
 *
 * IMPORTANT - VMA fields are accessed without an mmap lock being held, so the
 * VMA referenced must not be linked in any user-visible tree, i.e. it must be a
 * new VMA being mapped.
 *
 * The function assumes that addr is either contained within the VMA or below
 * it, and makes no attempt to validate this value beyond that.
 *
 * Returns true if the read lock was obtained and a stack was perhaps expanded,
 * false if the stack expansion failed.
 *
 * On stack expansion the function temporarily acquires an mmap write lock
 * before downgrading it.
 */
bool mmap_read_lock_maybe_expand(struct mm_struct *mm,
				 struct vm_area_struct *new_vma,
				 unsigned long addr, bool write)
{
	if (!write || addr >= new_vma->vm_start) {
		mmap_read_lock(mm);
		return true;
	}

	if (!(new_vma->vm_flags & VM_GROWSDOWN))
		return false;

	mmap_write_lock(mm);
	if (expand_downwards(new_vma, addr)) {
		mmap_write_unlock(mm);
		return false;
	}

	mmap_write_downgrade(mm);
	return true;
}

__latent_entropy int dup_mmap(struct mm_struct *mm, struct mm_struct *oldmm)
{
	struct vm_area_struct *mpnt, *tmp;
	int retval;
	unsigned long charge = 0;
	LIST_HEAD(uf);
	VMA_ITERATOR(vmi, mm, 0);

	if (mmap_write_lock_killable(oldmm))
		return -EINTR;
	flush_cache_dup_mm(oldmm);
	uprobe_dup_mmap(oldmm, mm);
	/*
	 * Not linked in yet - no deadlock potential:
	 */
	mmap_write_lock_nested(mm, SINGLE_DEPTH_NESTING);

	/* No ordering required: file already has been exposed. */
	dup_mm_exe_file(mm, oldmm);

	mm->total_vm = oldmm->total_vm;
	mm->data_vm = oldmm->data_vm;
	mm->exec_vm = oldmm->exec_vm;
	mm->stack_vm = oldmm->stack_vm;

	/* Use __mt_dup() to efficiently build an identical maple tree. */
	retval = __mt_dup(&oldmm->mm_mt, &mm->mm_mt, GFP_KERNEL);
	if (unlikely(retval))
		goto out;

	mt_clear_in_rcu(vmi.mas.tree);
	for_each_vma(vmi, mpnt) {
		struct file *file;

		vma_start_write(mpnt);
		if (mpnt->vm_flags & VM_DONTCOPY) {
			retval = vma_iter_clear_gfp(&vmi, mpnt->vm_start,
						    mpnt->vm_end, GFP_KERNEL);
			if (retval)
				goto loop_out;

			vm_stat_account(mm, mpnt->vm_flags, -vma_pages(mpnt));
			continue;
		}
		charge = 0;
		/*
		 * Don't duplicate many vmas if we've been oom-killed (for
		 * example)
		 */
		if (fatal_signal_pending(current)) {
			retval = -EINTR;
			goto loop_out;
		}
		if (mpnt->vm_flags & VM_ACCOUNT) {
			unsigned long len = vma_pages(mpnt);

			if (security_vm_enough_memory_mm(oldmm, len)) /* sic */
				goto fail_nomem;
			charge = len;
		}

		tmp = vm_area_dup(mpnt);
		if (!tmp)
			goto fail_nomem;
		retval = vma_dup_policy(mpnt, tmp);
		if (retval)
			goto fail_nomem_policy;
		tmp->vm_mm = mm;
		retval = dup_userfaultfd(tmp, &uf);
		if (retval)
			goto fail_nomem_anon_vma_fork;
		if (tmp->vm_flags & VM_WIPEONFORK) {
			/*
			 * VM_WIPEONFORK gets a clean slate in the child.
			 * Don't prepare anon_vma until fault since we don't
			 * copy page for current vma.
			 */
			tmp->anon_vma = NULL;
		} else if (anon_vma_fork(tmp, mpnt))
			goto fail_nomem_anon_vma_fork;
		vm_flags_clear(tmp, VM_LOCKED_MASK);
		/*
		 * Copy/update hugetlb private vma information.
		 */
		if (is_vm_hugetlb_page(tmp))
			hugetlb_dup_vma_private(tmp);

		/*
		 * Link the vma into the MT. After using __mt_dup(), memory
		 * allocation is not necessary here, so it cannot fail.
		 */
		vma_iter_bulk_store(&vmi, tmp);

		mm->map_count++;

		if (tmp->vm_ops && tmp->vm_ops->open)
			tmp->vm_ops->open(tmp);

		file = tmp->vm_file;
		if (file) {
			struct address_space *mapping = file->f_mapping;

			get_file(file);
			i_mmap_lock_write(mapping);
			if (vma_is_shared_maywrite(tmp))
				mapping_allow_writable(mapping);
			flush_dcache_mmap_lock(mapping);
			/* insert tmp into the share list, just after mpnt */
			vma_interval_tree_insert_after(tmp, mpnt,
					&mapping->i_mmap);
			flush_dcache_mmap_unlock(mapping);
			i_mmap_unlock_write(mapping);
		}

		if (!(tmp->vm_flags & VM_WIPEONFORK))
			retval = copy_page_range(tmp, mpnt);

		if (retval) {
			mpnt = vma_next(&vmi);
			goto loop_out;
		}
	}
	/* a new mm has just been created */
	retval = arch_dup_mmap(oldmm, mm);
loop_out:
	vma_iter_free(&vmi);
	if (!retval) {
		mt_set_in_rcu(vmi.mas.tree);
		ksm_fork(mm, oldmm);
		khugepaged_fork(mm, oldmm);
	} else {

		/*
		 * The entire maple tree has already been duplicated. If the
		 * mmap duplication fails, mark the failure point with
		 * XA_ZERO_ENTRY. In exit_mmap(), if this marker is encountered,
		 * stop releasing VMAs that have not been duplicated after this
		 * point.
		 */
		if (mpnt) {
			mas_set_range(&vmi.mas, mpnt->vm_start, mpnt->vm_end - 1);
			mas_store(&vmi.mas, XA_ZERO_ENTRY);
			/* Avoid OOM iterating a broken tree */
			mm_flags_set(MMF_OOM_SKIP, mm);
		}
		/*
		 * The mm_struct is going to exit, but the locks will be dropped
		 * first.  Set the mm_struct as unstable is advisable as it is
		 * not fully initialised.
		 */
		mm_flags_set(MMF_UNSTABLE, mm);
	}
out:
	mmap_write_unlock(mm);
	flush_tlb_mm(oldmm);
	mmap_write_unlock(oldmm);
	if (!retval)
		dup_userfaultfd_complete(&uf);
	else
		dup_userfaultfd_fail(&uf);
	return retval;

fail_nomem_anon_vma_fork:
	mpol_put(vma_policy(tmp));
fail_nomem_policy:
	vm_area_free(tmp);
fail_nomem:
	retval = -ENOMEM;
	vm_unacct_memory(charge);
	goto loop_out;
}
