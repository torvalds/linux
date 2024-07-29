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
static int do_brk_flags(struct vma_iterator *vmi, struct vm_area_struct *brkvma,
		unsigned long addr, unsigned long request, unsigned long flags);
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

	/* Always allow shrinking brk. */
	if (brk <= mm->brk) {
		/* Search one past newbrk */
		vma_iter_init(&vmi, mm, newbrk);
		brkvma = vma_find(&vmi, oldbrk);
		if (!brkvma || brkvma->vm_start >= oldbrk)
			goto out; /* mapping intersects with an existing non-brk vma. */
		/*
		 * mm->brk must be protected by write mmap_lock.
		 * do_vma_munmap() will drop the lock on success,  so update it
		 * before calling do_vma_munmap().
		 */
		mm->brk = brk;
		if (do_vma_munmap(&vmi, brkvma, newbrk, oldbrk, &uf, true))
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

bool mlock_future_ok(struct mm_struct *mm, unsigned long flags,
			unsigned long bytes)
{
	unsigned long locked_pages, limit_pages;

	if (!(flags & VM_LOCKED) || capable(CAP_IPC_LOCK))
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
			unsigned long flags, vm_flags_t vm_flags,
			unsigned long pgoff, unsigned long *populate,
			struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	int pkey = 0;

	*populate = 0;

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
	vm_flags |= calc_vm_prot_bits(prot, pkey) | calc_vm_flag_bits(flags) |
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
 * We account for memory if it's a private writeable mapping,
 * not hugepages and VM_NORESERVE wasn't set.
 */
static inline bool accountable_mapping(struct file *file, vm_flags_t vm_flags)
{
	/*
	 * hugetlb has its own accounting separate from the core VM
	 * VM_HUGETLB may not be set yet so we cannot check for that flag.
	 */
	if (file && is_file_hugepages(file))
		return false;

	return (vm_flags & (VM_NORESERVE | VM_SHARED | VM_WRITE)) == VM_WRITE;
}

/**
 * unmapped_area() - Find an area between the low_limit and the high_limit with
 * the correct alignment and offset, all from @info. Note: current->mm is used
 * for the search.
 *
 * @info: The unmapped area information including the range [low_limit -
 * high_limit), the alignment offset and mask.
 *
 * Return: A memory address or -ENOMEM.
 */
static unsigned long unmapped_area(struct vm_unmapped_area_info *info)
{
	unsigned long length, gap;
	unsigned long low_limit, high_limit;
	struct vm_area_struct *tmp;
	VMA_ITERATOR(vmi, current->mm, 0);

	/* Adjust search length to account for worst case alignment overhead */
	length = info->length + info->align_mask + info->start_gap;
	if (length < info->length)
		return -ENOMEM;

	low_limit = info->low_limit;
	if (low_limit < mmap_min_addr)
		low_limit = mmap_min_addr;
	high_limit = info->high_limit;
retry:
	if (vma_iter_area_lowest(&vmi, low_limit, high_limit, length))
		return -ENOMEM;

	/*
	 * Adjust for the gap first so it doesn't interfere with the
	 * later alignment. The first step is the minimum needed to
	 * fulill the start gap, the next steps is the minimum to align
	 * that. It is the minimum needed to fulill both.
	 */
	gap = vma_iter_addr(&vmi) + info->start_gap;
	gap += (info->align_offset - gap) & info->align_mask;
	tmp = vma_next(&vmi);
	if (tmp && (tmp->vm_flags & VM_STARTGAP_FLAGS)) { /* Avoid prev check if possible */
		if (vm_start_gap(tmp) < gap + length - 1) {
			low_limit = tmp->vm_end;
			vma_iter_reset(&vmi);
			goto retry;
		}
	} else {
		tmp = vma_prev(&vmi);
		if (tmp && vm_end_gap(tmp) > gap) {
			low_limit = vm_end_gap(tmp);
			vma_iter_reset(&vmi);
			goto retry;
		}
	}

	return gap;
}

/**
 * unmapped_area_topdown() - Find an area between the low_limit and the
 * high_limit with the correct alignment and offset at the highest available
 * address, all from @info. Note: current->mm is used for the search.
 *
 * @info: The unmapped area information including the range [low_limit -
 * high_limit), the alignment offset and mask.
 *
 * Return: A memory address or -ENOMEM.
 */
static unsigned long unmapped_area_topdown(struct vm_unmapped_area_info *info)
{
	unsigned long length, gap, gap_end;
	unsigned long low_limit, high_limit;
	struct vm_area_struct *tmp;
	VMA_ITERATOR(vmi, current->mm, 0);

	/* Adjust search length to account for worst case alignment overhead */
	length = info->length + info->align_mask + info->start_gap;
	if (length < info->length)
		return -ENOMEM;

	low_limit = info->low_limit;
	if (low_limit < mmap_min_addr)
		low_limit = mmap_min_addr;
	high_limit = info->high_limit;
retry:
	if (vma_iter_area_highest(&vmi, low_limit, high_limit, length))
		return -ENOMEM;

	gap = vma_iter_end(&vmi) - info->length;
	gap -= (gap - info->align_offset) & info->align_mask;
	gap_end = vma_iter_end(&vmi);
	tmp = vma_next(&vmi);
	if (tmp && (tmp->vm_flags & VM_STARTGAP_FLAGS)) { /* Avoid prev check if possible */
		if (vm_start_gap(tmp) < gap_end) {
			high_limit = vm_start_gap(tmp);
			vma_iter_reset(&vmi);
			goto retry;
		}
	} else {
		tmp = vma_prev(&vmi);
		if (tmp && vm_end_gap(tmp) > gap) {
			high_limit = tmp->vm_start;
			vma_iter_reset(&vmi);
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

#ifndef HAVE_ARCH_UNMAPPED_AREA_VMFLAGS
unsigned long
arch_get_unmapped_area_vmflags(struct file *filp, unsigned long addr, unsigned long len,
			       unsigned long pgoff, unsigned long flags, vm_flags_t vm_flags)
{
	return arch_get_unmapped_area(filp, addr, len, pgoff, flags);
}

unsigned long
arch_get_unmapped_area_topdown_vmflags(struct file *filp, unsigned long addr,
				       unsigned long len, unsigned long pgoff,
				       unsigned long flags, vm_flags_t vm_flags)
{
	return arch_get_unmapped_area_topdown(filp, addr, len, pgoff, flags);
}
#endif

unsigned long mm_get_unmapped_area_vmflags(struct mm_struct *mm, struct file *filp,
					   unsigned long addr, unsigned long len,
					   unsigned long pgoff, unsigned long flags,
					   vm_flags_t vm_flags)
{
	if (test_bit(MMF_TOPDOWN, &mm->flags))
		return arch_get_unmapped_area_topdown_vmflags(filp, addr, len, pgoff,
							      flags, vm_flags);
	return arch_get_unmapped_area_vmflags(filp, addr, len, pgoff, flags, vm_flags);
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
	} else if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
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
	if (test_bit(MMF_TOPDOWN, &mm->flags))
		return arch_get_unmapped_area_topdown(file, addr, len, pgoff, flags);
	return arch_get_unmapped_area(file, addr, len, pgoff, flags);
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
	if (!mlock_future_ok(mm, vma->vm_flags, grow << PAGE_SHIFT))
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

#if defined(CONFIG_STACK_GROWSUP)
/*
 * PA-RISC uses this for its stack.
 * vma is the last one with address > vma->vm_end.  Have to extend vma.
 */
static int expand_upwards(struct vm_area_struct *vma, unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *next;
	unsigned long gap_addr;
	int error = 0;
	VMA_ITERATOR(vmi, mm, vma->vm_start);

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

	if (next)
		vma_iter_prev_range_limit(&vmi, address);

	vma_iter_config(&vmi, vma->vm_start, address);
	if (vma_iter_prealloc(&vmi, vma))
		return -ENOMEM;

	/* We must make sure the anon_vma is allocated. */
	if (unlikely(anon_vma_prepare(vma))) {
		vma_iter_free(&vmi);
		return -ENOMEM;
	}

	/* Lock the VMA before expanding to prevent concurrent page faults */
	vma_start_write(vma);
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
				vma_iter_store(&vmi, vma);
				anon_vma_interval_tree_post_update_vma(vma);
				spin_unlock(&mm->page_table_lock);

				perf_event_mmap(vma);
			}
		}
	}
	anon_vma_unlock_write(vma->anon_vma);
	vma_iter_free(&vmi);
	validate_mm(mm);
	return error;
}
#endif /* CONFIG_STACK_GROWSUP */

/*
 * vma is the first one with address < vma->vm_start.  Have to extend vma.
 * mmap_lock held for writing.
 */
int expand_downwards(struct vm_area_struct *vma, unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *prev;
	int error = 0;
	VMA_ITERATOR(vmi, mm, vma->vm_start);

	if (!(vma->vm_flags & VM_GROWSDOWN))
		return -EFAULT;

	address &= PAGE_MASK;
	if (address < mmap_min_addr || address < FIRST_USER_ADDRESS)
		return -EPERM;

	/* Enforce stack_guard_gap */
	prev = vma_prev(&vmi);
	/* Check that both stack segments have the same anon_vma? */
	if (prev) {
		if (!(prev->vm_flags & VM_GROWSDOWN) &&
		    vma_is_accessible(prev) &&
		    (address - prev->vm_end < stack_guard_gap))
			return -ENOMEM;
	}

	if (prev)
		vma_iter_next_range_limit(&vmi, vma->vm_start);

	vma_iter_config(&vmi, address, vma->vm_end);
	if (vma_iter_prealloc(&vmi, vma))
		return -ENOMEM;

	/* We must make sure the anon_vma is allocated. */
	if (unlikely(anon_vma_prepare(vma))) {
		vma_iter_free(&vmi);
		return -ENOMEM;
	}

	/* Lock the VMA before expanding to prevent concurrent page faults */
	vma_start_write(vma);
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
				vma_iter_store(&vmi, vma);
				anon_vma_interval_tree_post_update_vma(vma);
				spin_unlock(&mm->page_table_lock);

				perf_event_mmap(vma);
			}
		}
	}
	anon_vma_unlock_write(vma->anon_vma);
	vma_iter_free(&vmi);
	validate_mm(mm);
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
	bool writable_file_mapping = false;
	pgoff_t vm_pgoff;
	int error;
	VMA_ITERATOR(vmi, mm, addr);

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
	error = do_vmi_munmap(&vmi, mm, addr, len, uf, false);
	if (error == -EPERM)
		return error;
	else if (error)
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

	next = vma_next(&vmi);
	prev = vma_prev(&vmi);
	if (vm_flags & VM_SPECIAL) {
		if (prev)
			vma_iter_next_range(&vmi);
		goto cannot_expand;
	}

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
	} else if (prev) {
		vma_iter_next_range(&vmi);
	}

	/* Actually expand, if possible */
	if (vma &&
	    !vma_expand(&vmi, vma, merge_start, merge_end, vm_pgoff, next)) {
		khugepaged_enter_vma(vma, vm_flags);
		goto expanded;
	}

	if (vma == prev)
		vma_iter_set(&vmi, addr);
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

	vma_iter_config(&vmi, addr, end);
	vma_set_range(vma, addr, end, pgoff);
	vm_flags_init(vma, vm_flags);
	vma->vm_page_prot = vm_get_page_prot(vm_flags);

	if (file) {
		vma->vm_file = get_file(file);
		error = call_mmap(file, vma);
		if (error)
			goto unmap_and_free_vma;

		if (vma_is_shared_maywrite(vma)) {
			error = mapping_map_writable(file->f_mapping);
			if (error)
				goto close_and_free_vma;

			writable_file_mapping = true;
		}

		/*
		 * Expansion is handled above, merging is handled below.
		 * Drivers should not alter the address of the VMA.
		 */
		error = -EINVAL;
		if (WARN_ON((addr != vma->vm_start)))
			goto close_and_free_vma;

		vma_iter_config(&vmi, addr, end);
		/*
		 * If vm_flags changed after call_mmap(), we should try merge
		 * vma again as we may succeed this time.
		 */
		if (unlikely(vm_flags != vma->vm_flags && prev)) {
			merge = vma_merge_new_vma(&vmi, prev, vma,
						  vma->vm_start, vma->vm_end,
						  vma->vm_pgoff);
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

	if (map_deny_write_exec(vma, vma->vm_flags)) {
		error = -EACCES;
		goto close_and_free_vma;
	}

	/* Allow architectures to sanity-check the vm_flags */
	error = -EINVAL;
	if (!arch_validate_flags(vma->vm_flags))
		goto close_and_free_vma;

	error = -ENOMEM;
	if (vma_iter_prealloc(&vmi, vma))
		goto close_and_free_vma;

	/* Lock the VMA since it is modified after insertion into VMA tree */
	vma_start_write(vma);
	vma_iter_store(&vmi, vma);
	mm->map_count++;
	vma_link_file(vma);

	/*
	 * vma_merge() calls khugepaged_enter_vma() either, the below
	 * call covers the non-merge case.
	 */
	khugepaged_enter_vma(vma, vma->vm_flags);

	/* Once vma denies write, undo our temporary denial count */
unmap_writable:
	if (writable_file_mapping)
		mapping_unmap_writable(file->f_mapping);
	file = vma->vm_file;
	ksm_add_vma(vma);
expanded:
	perf_event_mmap(vma);

	vm_stat_account(mm, vm_flags, len >> PAGE_SHIFT);
	if (vm_flags & VM_LOCKED) {
		if ((vm_flags & VM_SPECIAL) || vma_is_dax(vma) ||
					is_vm_hugetlb_page(vma) ||
					vma == get_gate_vma(current->mm))
			vm_flags_clear(vma, VM_LOCKED_MASK);
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
	vm_flags_set(vma, VM_SOFTDIRTY);

	vma_set_page_prot(vma);

	validate_mm(mm);
	return addr;

close_and_free_vma:
	if (file && vma->vm_ops && vma->vm_ops->close)
		vma->vm_ops->close(vma);

	if (file || vma->vm_file) {
unmap_and_free_vma:
		fput(vma->vm_file);
		vma->vm_file = NULL;

		vma_iter_set(&vmi, vma->vm_end);
		/* Undo any partial mapping done by a device driver. */
		unmap_region(mm, &vmi.mas, vma, prev, next, vma->vm_start,
			     vma->vm_end, vma->vm_end, true);
	}
	if (writable_file_mapping)
		mapping_unmap_writable(file->f_mapping);
free_vma:
	vm_area_free(vma);
unacct_error:
	if (charged)
		vm_unacct_memory(charged);
	validate_mm(mm);
	return error;
}

static int __vm_munmap(unsigned long start, size_t len, bool unlock)
{
	int ret;
	struct mm_struct *mm = current->mm;
	LIST_HEAD(uf);
	VMA_ITERATOR(vmi, mm, start);

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = do_vmi_munmap(&vmi, mm, start, len, &uf, unlock);
	if (ret || !unlock)
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
			prot, flags, 0, pgoff, &populate, NULL);
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
 * do_vma_munmap() - Unmap a full or partial vma.
 * @vmi: The vma iterator pointing at the vma
 * @vma: The first vma to be munmapped
 * @start: the start of the address to unmap
 * @end: The end of the address to unmap
 * @uf: The userfaultfd list_head
 * @unlock: Drop the lock on success
 *
 * unmaps a VMA mapping when the vma iterator is already in position.
 * Does not handle alignment.
 *
 * Return: 0 on success drops the lock of so directed, error on failure and will
 * still hold the lock.
 */
int do_vma_munmap(struct vma_iterator *vmi, struct vm_area_struct *vma,
		unsigned long start, unsigned long end, struct list_head *uf,
		bool unlock)
{
	struct mm_struct *mm = vma->vm_mm;

	/*
	 * Check if memory is sealed before arch_unmap.
	 * Prevent unmapping a sealed VMA.
	 * can_modify_mm assumes we have acquired the lock on MM.
	 */
	if (unlikely(!can_modify_mm(mm, start, end)))
		return -EPERM;

	arch_unmap(mm, start, end);
	return do_vmi_align_munmap(vmi, vma, mm, start, end, uf, unlock);
}

/*
 * do_brk_flags() - Increase the brk vma if the flags match.
 * @vmi: The vma iterator
 * @addr: The start address
 * @len: The length of the increase
 * @vma: The vma,
 * @flags: The VMA Flags
 *
 * Extend the brk VMA from addr to addr + len.  If the VMA is NULL or the flags
 * do not match then create a new anonymous VMA.  Eventually we may be able to
 * do some brk-specific accounting here.
 */
static int do_brk_flags(struct vma_iterator *vmi, struct vm_area_struct *vma,
		unsigned long addr, unsigned long len, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vma_prepare vp;

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
		vma_iter_config(vmi, vma->vm_start, addr + len);
		if (vma_iter_prealloc(vmi, vma))
			goto unacct_fail;

		vma_start_write(vma);

		init_vma_prep(&vp, vma);
		vma_prepare(&vp);
		vma_adjust_trans_huge(vma, vma->vm_start, addr + len, 0);
		vma->vm_end = addr + len;
		vm_flags_set(vma, VM_SOFTDIRTY);
		vma_iter_store(vmi, vma);

		vma_complete(&vp, vmi, mm);
		khugepaged_enter_vma(vma, flags);
		goto out;
	}

	if (vma)
		vma_iter_next_range(vmi);
	/* create a vma struct for an anonymous mapping */
	vma = vm_area_alloc(mm);
	if (!vma)
		goto unacct_fail;

	vma_set_anonymous(vma);
	vma_set_range(vma, addr, addr + len, addr >> PAGE_SHIFT);
	vm_flags_init(vma, flags);
	vma->vm_page_prot = vm_get_page_prot(flags);
	vma_start_write(vma);
	if (vma_iter_store_gfp(vmi, vma, GFP_KERNEL))
		goto mas_store_fail;

	mm->map_count++;
	validate_mm(mm);
	ksm_add_vma(vma);
out:
	perf_event_mmap(vma);
	mm->total_vm += len >> PAGE_SHIFT;
	mm->data_vm += len >> PAGE_SHIFT;
	if (flags & VM_LOCKED)
		mm->locked_vm += (len >> PAGE_SHIFT);
	vm_flags_set(vma, VM_SOFTDIRTY);
	return 0;

mas_store_fail:
	vm_area_free(vma);
unacct_fail:
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
	VMA_ITERATOR(vmi, mm, addr);

	len = PAGE_ALIGN(request);
	if (len < request)
		return -ENOMEM;
	if (!len)
		return 0;

	/* Until we need other flags, refuse anything except VM_EXEC. */
	if ((flags & (~VM_EXEC)) != 0)
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
	ret = do_brk_flags(&vmi, vma, addr, len, flags);
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

	lru_add_drain();
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
	set_bit(MMF_OOM_SKIP, &mm->flags);
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
		remove_vma(vma, true);
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
		if (vma->vm_flags & VM_ACCOUNT)
			vm_unacct_memory(charged);
		return -ENOMEM;
	}

	return 0;
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
 * Relocate a VMA downwards by shift bytes. There cannot be any VMAs between
 * this VMA and its relocated range, which will now reside at [vma->vm_start -
 * shift, vma->vm_end - shift).
 *
 * This function is almost certainly NOT what you want for anything other than
 * early executable temporary stack relocation.
 */
int relocate_vma_down(struct vm_area_struct *vma, unsigned long shift)
{
	/*
	 * The process proceeds as follows:
	 *
	 * 1) Use shift to calculate the new vma endpoints.
	 * 2) Extend vma to cover both the old and new ranges.  This ensures the
	 *    arguments passed to subsequent functions are consistent.
	 * 3) Move vma's page tables to the new range.
	 * 4) Free up any cleared pgd range.
	 * 5) Shrink the vma to cover only the new range.
	 */

	struct mm_struct *mm = vma->vm_mm;
	unsigned long old_start = vma->vm_start;
	unsigned long old_end = vma->vm_end;
	unsigned long length = old_end - old_start;
	unsigned long new_start = old_start - shift;
	unsigned long new_end = old_end - shift;
	VMA_ITERATOR(vmi, mm, new_start);
	struct vm_area_struct *next;
	struct mmu_gather tlb;

	BUG_ON(new_start > new_end);

	/*
	 * ensure there are no vmas between where we want to go
	 * and where we are
	 */
	if (vma != vma_next(&vmi))
		return -EFAULT;

	vma_iter_prev_range(&vmi);
	/*
	 * cover the whole range: [new_start, old_end)
	 */
	if (vma_expand(&vmi, vma, new_start, old_end, vma->vm_pgoff, NULL))
		return -ENOMEM;

	/*
	 * move the page tables downwards, on failure we rely on
	 * process cleanup to remove whatever mess we made.
	 */
	if (length != move_page_tables(vma, old_start,
				       vma, new_start, length, false, true))
		return -ENOMEM;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm);
	next = vma_next(&vmi);
	if (new_end > old_start) {
		/*
		 * when the old and new regions overlap clear from new_end.
		 */
		free_pgd_range(&tlb, new_end, old_end, new_end,
			next ? next->vm_start : USER_PGTABLES_CEILING);
	} else {
		/*
		 * otherwise, clean from old_start; this is done to not touch
		 * the address space in [new_end, old_start) some architectures
		 * have constraints on va-space that make this illegal (IA64) -
		 * for the others its just a little faster.
		 */
		free_pgd_range(&tlb, old_start, old_end, new_end,
			next ? next->vm_start : USER_PGTABLES_CEILING);
	}
	tlb_finish_mmu(&tlb);

	vma_prev(&vmi);
	/* Shrink the vma to just the new range */
	return vma_shrink(&vmi, vma, new_start, new_end, vma->vm_pgoff);
}
