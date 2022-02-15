// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/mm/mlock.c
 *
 *  (C) Copyright 1995 Linus Torvalds
 *  (C) Copyright 2002 Christoph Hellwig
 */

#include <linux/capability.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/sched/user.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/mempolicy.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/export.h>
#include <linux/rmap.h>
#include <linux/mmzone.h>
#include <linux/hugetlb.h>
#include <linux/memcontrol.h>
#include <linux/mm_inline.h>
#include <linux/secretmem.h>

#include "internal.h"

bool can_do_mlock(void)
{
	if (rlimit(RLIMIT_MEMLOCK) != 0)
		return true;
	if (capable(CAP_IPC_LOCK))
		return true;
	return false;
}
EXPORT_SYMBOL(can_do_mlock);

/*
 * Mlocked pages are marked with PageMlocked() flag for efficient testing
 * in vmscan and, possibly, the fault path; and to support semi-accurate
 * statistics.
 *
 * An mlocked page [PageMlocked(page)] is unevictable.  As such, it will
 * be placed on the LRU "unevictable" list, rather than the [in]active lists.
 * The unevictable list is an LRU sibling list to the [in]active lists.
 * PageUnevictable is set to indicate the unevictable state.
 */

/*
 *  LRU accounting for clear_page_mlock()
 */
void clear_page_mlock(struct page *page)
{
	int nr_pages;

	if (!TestClearPageMlocked(page))
		return;

	nr_pages = thp_nr_pages(page);
	mod_zone_page_state(page_zone(page), NR_MLOCK, -nr_pages);
	count_vm_events(UNEVICTABLE_PGCLEARED, nr_pages);
	/*
	 * The previous TestClearPageMlocked() corresponds to the smp_mb()
	 * in __pagevec_lru_add_fn().
	 *
	 * See __pagevec_lru_add_fn for more explanation.
	 */
	if (!isolate_lru_page(page)) {
		putback_lru_page(page);
	} else {
		/*
		 * We lost the race. the page already moved to evictable list.
		 */
		if (PageUnevictable(page))
			count_vm_events(UNEVICTABLE_PGSTRANDED, nr_pages);
	}
}

/**
 * mlock_page - mlock a page
 * @page: page to be mlocked, either a normal page or a THP head.
 */
void mlock_page(struct page *page)
{
	VM_BUG_ON_PAGE(PageTail(page), page);

	if (!TestSetPageMlocked(page)) {
		int nr_pages = thp_nr_pages(page);

		mod_zone_page_state(page_zone(page), NR_MLOCK, nr_pages);
		count_vm_events(UNEVICTABLE_PGMLOCKED, nr_pages);
		if (!isolate_lru_page(page))
			putback_lru_page(page);
	}
}

/**
 * munlock_page - munlock a page
 * @page: page to be munlocked, either a normal page or a THP head.
 */
void munlock_page(struct page *page)
{
	VM_BUG_ON_PAGE(PageTail(page), page);

	if (TestClearPageMlocked(page)) {
		int nr_pages = thp_nr_pages(page);

		mod_zone_page_state(page_zone(page), NR_MLOCK, -nr_pages);
		if (!isolate_lru_page(page)) {
			putback_lru_page(page);
			count_vm_events(UNEVICTABLE_PGMUNLOCKED, nr_pages);
		} else if (PageUnevictable(page)) {
			count_vm_events(UNEVICTABLE_PGSTRANDED, nr_pages);
		}
	}
}

/*
 * munlock_vma_pages_range() - munlock all pages in the vma range.'
 * @vma - vma containing range to be munlock()ed.
 * @start - start address in @vma of the range
 * @end - end of range in @vma.
 *
 *  For mremap(), munmap() and exit().
 *
 * Called with @vma VM_LOCKED.
 *
 * Returns with VM_LOCKED cleared.  Callers must be prepared to
 * deal with this.
 */
static void munlock_vma_pages_range(struct vm_area_struct *vma,
				    unsigned long start, unsigned long end)
{
	vma->vm_flags &= VM_LOCKED_CLEAR_MASK;

	/* Reimplementation to follow in later commit */
}

/*
 * mlock_fixup  - handle mlock[all]/munlock[all] requests.
 *
 * Filters out "special" vmas -- VM_LOCKED never gets set for these, and
 * munlock is a no-op.  However, for some special vmas, we go ahead and
 * populate the ptes.
 *
 * For vmas that pass the filters, merge/split as appropriate.
 */
static int mlock_fixup(struct vm_area_struct *vma, struct vm_area_struct **prev,
	unsigned long start, unsigned long end, vm_flags_t newflags)
{
	struct mm_struct *mm = vma->vm_mm;
	pgoff_t pgoff;
	int nr_pages;
	int ret = 0;
	int lock = !!(newflags & VM_LOCKED);
	vm_flags_t old_flags = vma->vm_flags;

	if (newflags == vma->vm_flags || (vma->vm_flags & VM_SPECIAL) ||
	    is_vm_hugetlb_page(vma) || vma == get_gate_vma(current->mm) ||
	    vma_is_dax(vma) || vma_is_secretmem(vma))
		/* don't set VM_LOCKED or VM_LOCKONFAULT and don't count */
		goto out;

	pgoff = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
	*prev = vma_merge(mm, *prev, start, end, newflags, vma->anon_vma,
			  vma->vm_file, pgoff, vma_policy(vma),
			  vma->vm_userfaultfd_ctx, vma_anon_name(vma));
	if (*prev) {
		vma = *prev;
		goto success;
	}

	if (start != vma->vm_start) {
		ret = split_vma(mm, vma, start, 1);
		if (ret)
			goto out;
	}

	if (end != vma->vm_end) {
		ret = split_vma(mm, vma, end, 0);
		if (ret)
			goto out;
	}

success:
	/*
	 * Keep track of amount of locked VM.
	 */
	nr_pages = (end - start) >> PAGE_SHIFT;
	if (!lock)
		nr_pages = -nr_pages;
	else if (old_flags & VM_LOCKED)
		nr_pages = 0;
	mm->locked_vm += nr_pages;

	/*
	 * vm_flags is protected by the mmap_lock held in write mode.
	 * It's okay if try_to_unmap_one unmaps a page just after we
	 * set VM_LOCKED, populate_vma_page_range will bring it back.
	 */

	if (lock)
		vma->vm_flags = newflags;
	else
		munlock_vma_pages_range(vma, start, end);

out:
	*prev = vma;
	return ret;
}

static int apply_vma_lock_flags(unsigned long start, size_t len,
				vm_flags_t flags)
{
	unsigned long nstart, end, tmp;
	struct vm_area_struct *vma, *prev;
	int error;

	VM_BUG_ON(offset_in_page(start));
	VM_BUG_ON(len != PAGE_ALIGN(len));
	end = start + len;
	if (end < start)
		return -EINVAL;
	if (end == start)
		return 0;
	vma = find_vma(current->mm, start);
	if (!vma || vma->vm_start > start)
		return -ENOMEM;

	prev = vma->vm_prev;
	if (start > vma->vm_start)
		prev = vma;

	for (nstart = start ; ; ) {
		vm_flags_t newflags = vma->vm_flags & VM_LOCKED_CLEAR_MASK;

		newflags |= flags;

		/* Here we know that  vma->vm_start <= nstart < vma->vm_end. */
		tmp = vma->vm_end;
		if (tmp > end)
			tmp = end;
		error = mlock_fixup(vma, &prev, nstart, tmp, newflags);
		if (error)
			break;
		nstart = tmp;
		if (nstart < prev->vm_end)
			nstart = prev->vm_end;
		if (nstart >= end)
			break;

		vma = prev->vm_next;
		if (!vma || vma->vm_start != nstart) {
			error = -ENOMEM;
			break;
		}
	}
	return error;
}

/*
 * Go through vma areas and sum size of mlocked
 * vma pages, as return value.
 * Note deferred memory locking case(mlock2(,,MLOCK_ONFAULT)
 * is also counted.
 * Return value: previously mlocked page counts
 */
static unsigned long count_mm_mlocked_page_nr(struct mm_struct *mm,
		unsigned long start, size_t len)
{
	struct vm_area_struct *vma;
	unsigned long count = 0;

	if (mm == NULL)
		mm = current->mm;

	vma = find_vma(mm, start);
	if (vma == NULL)
		return 0;

	for (; vma ; vma = vma->vm_next) {
		if (start >= vma->vm_end)
			continue;
		if (start + len <=  vma->vm_start)
			break;
		if (vma->vm_flags & VM_LOCKED) {
			if (start > vma->vm_start)
				count -= (start - vma->vm_start);
			if (start + len < vma->vm_end) {
				count += start + len - vma->vm_start;
				break;
			}
			count += vma->vm_end - vma->vm_start;
		}
	}

	return count >> PAGE_SHIFT;
}

/*
 * convert get_user_pages() return value to posix mlock() error
 */
static int __mlock_posix_error_return(long retval)
{
	if (retval == -EFAULT)
		retval = -ENOMEM;
	else if (retval == -ENOMEM)
		retval = -EAGAIN;
	return retval;
}

static __must_check int do_mlock(unsigned long start, size_t len, vm_flags_t flags)
{
	unsigned long locked;
	unsigned long lock_limit;
	int error = -ENOMEM;

	start = untagged_addr(start);

	if (!can_do_mlock())
		return -EPERM;

	len = PAGE_ALIGN(len + (offset_in_page(start)));
	start &= PAGE_MASK;

	lock_limit = rlimit(RLIMIT_MEMLOCK);
	lock_limit >>= PAGE_SHIFT;
	locked = len >> PAGE_SHIFT;

	if (mmap_write_lock_killable(current->mm))
		return -EINTR;

	locked += current->mm->locked_vm;
	if ((locked > lock_limit) && (!capable(CAP_IPC_LOCK))) {
		/*
		 * It is possible that the regions requested intersect with
		 * previously mlocked areas, that part area in "mm->locked_vm"
		 * should not be counted to new mlock increment count. So check
		 * and adjust locked count if necessary.
		 */
		locked -= count_mm_mlocked_page_nr(current->mm,
				start, len);
	}

	/* check against resource limits */
	if ((locked <= lock_limit) || capable(CAP_IPC_LOCK))
		error = apply_vma_lock_flags(start, len, flags);

	mmap_write_unlock(current->mm);
	if (error)
		return error;

	error = __mm_populate(start, len, 0);
	if (error)
		return __mlock_posix_error_return(error);
	return 0;
}

SYSCALL_DEFINE2(mlock, unsigned long, start, size_t, len)
{
	return do_mlock(start, len, VM_LOCKED);
}

SYSCALL_DEFINE3(mlock2, unsigned long, start, size_t, len, int, flags)
{
	vm_flags_t vm_flags = VM_LOCKED;

	if (flags & ~MLOCK_ONFAULT)
		return -EINVAL;

	if (flags & MLOCK_ONFAULT)
		vm_flags |= VM_LOCKONFAULT;

	return do_mlock(start, len, vm_flags);
}

SYSCALL_DEFINE2(munlock, unsigned long, start, size_t, len)
{
	int ret;

	start = untagged_addr(start);

	len = PAGE_ALIGN(len + (offset_in_page(start)));
	start &= PAGE_MASK;

	if (mmap_write_lock_killable(current->mm))
		return -EINTR;
	ret = apply_vma_lock_flags(start, len, 0);
	mmap_write_unlock(current->mm);

	return ret;
}

/*
 * Take the MCL_* flags passed into mlockall (or 0 if called from munlockall)
 * and translate into the appropriate modifications to mm->def_flags and/or the
 * flags for all current VMAs.
 *
 * There are a couple of subtleties with this.  If mlockall() is called multiple
 * times with different flags, the values do not necessarily stack.  If mlockall
 * is called once including the MCL_FUTURE flag and then a second time without
 * it, VM_LOCKED and VM_LOCKONFAULT will be cleared from mm->def_flags.
 */
static int apply_mlockall_flags(int flags)
{
	struct vm_area_struct *vma, *prev = NULL;
	vm_flags_t to_add = 0;

	current->mm->def_flags &= VM_LOCKED_CLEAR_MASK;
	if (flags & MCL_FUTURE) {
		current->mm->def_flags |= VM_LOCKED;

		if (flags & MCL_ONFAULT)
			current->mm->def_flags |= VM_LOCKONFAULT;

		if (!(flags & MCL_CURRENT))
			goto out;
	}

	if (flags & MCL_CURRENT) {
		to_add |= VM_LOCKED;
		if (flags & MCL_ONFAULT)
			to_add |= VM_LOCKONFAULT;
	}

	for (vma = current->mm->mmap; vma ; vma = prev->vm_next) {
		vm_flags_t newflags;

		newflags = vma->vm_flags & VM_LOCKED_CLEAR_MASK;
		newflags |= to_add;

		/* Ignore errors */
		mlock_fixup(vma, &prev, vma->vm_start, vma->vm_end, newflags);
		cond_resched();
	}
out:
	return 0;
}

SYSCALL_DEFINE1(mlockall, int, flags)
{
	unsigned long lock_limit;
	int ret;

	if (!flags || (flags & ~(MCL_CURRENT | MCL_FUTURE | MCL_ONFAULT)) ||
	    flags == MCL_ONFAULT)
		return -EINVAL;

	if (!can_do_mlock())
		return -EPERM;

	lock_limit = rlimit(RLIMIT_MEMLOCK);
	lock_limit >>= PAGE_SHIFT;

	if (mmap_write_lock_killable(current->mm))
		return -EINTR;

	ret = -ENOMEM;
	if (!(flags & MCL_CURRENT) || (current->mm->total_vm <= lock_limit) ||
	    capable(CAP_IPC_LOCK))
		ret = apply_mlockall_flags(flags);
	mmap_write_unlock(current->mm);
	if (!ret && (flags & MCL_CURRENT))
		mm_populate(0, TASK_SIZE);

	return ret;
}

SYSCALL_DEFINE0(munlockall)
{
	int ret;

	if (mmap_write_lock_killable(current->mm))
		return -EINTR;
	ret = apply_mlockall_flags(0);
	mmap_write_unlock(current->mm);
	return ret;
}

/*
 * Objects with different lifetime than processes (SHM_LOCK and SHM_HUGETLB
 * shm segments) get accounted against the user_struct instead.
 */
static DEFINE_SPINLOCK(shmlock_user_lock);

int user_shm_lock(size_t size, struct ucounts *ucounts)
{
	unsigned long lock_limit, locked;
	long memlock;
	int allowed = 0;

	locked = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	lock_limit = rlimit(RLIMIT_MEMLOCK);
	if (lock_limit == RLIM_INFINITY)
		allowed = 1;
	lock_limit >>= PAGE_SHIFT;
	spin_lock(&shmlock_user_lock);
	memlock = inc_rlimit_ucounts(ucounts, UCOUNT_RLIMIT_MEMLOCK, locked);

	if (!allowed && (memlock == LONG_MAX || memlock > lock_limit) && !capable(CAP_IPC_LOCK)) {
		dec_rlimit_ucounts(ucounts, UCOUNT_RLIMIT_MEMLOCK, locked);
		goto out;
	}
	if (!get_ucounts(ucounts)) {
		dec_rlimit_ucounts(ucounts, UCOUNT_RLIMIT_MEMLOCK, locked);
		goto out;
	}
	allowed = 1;
out:
	spin_unlock(&shmlock_user_lock);
	return allowed;
}

void user_shm_unlock(size_t size, struct ucounts *ucounts)
{
	spin_lock(&shmlock_user_lock);
	dec_rlimit_ucounts(ucounts, UCOUNT_RLIMIT_MEMLOCK, (size + PAGE_SIZE - 1) >> PAGE_SHIFT);
	spin_unlock(&shmlock_user_lock);
	put_ucounts(ucounts);
}
