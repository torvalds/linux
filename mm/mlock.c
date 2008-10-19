/*
 *	linux/mm/mlock.c
 *
 *  (C) Copyright 1995 Linus Torvalds
 *  (C) Copyright 2002 Christoph Hellwig
 */

#include <linux/capability.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/pagemap.h>
#include <linux/mempolicy.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/rmap.h>
#include <linux/mmzone.h>
#include <linux/hugetlb.h>

#include "internal.h"

int can_do_mlock(void)
{
	if (capable(CAP_IPC_LOCK))
		return 1;
	if (current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur != 0)
		return 1;
	return 0;
}
EXPORT_SYMBOL(can_do_mlock);

#ifdef CONFIG_UNEVICTABLE_LRU
/*
 * Mlocked pages are marked with PageMlocked() flag for efficient testing
 * in vmscan and, possibly, the fault path; and to support semi-accurate
 * statistics.
 *
 * An mlocked page [PageMlocked(page)] is unevictable.  As such, it will
 * be placed on the LRU "unevictable" list, rather than the [in]active lists.
 * The unevictable list is an LRU sibling list to the [in]active lists.
 * PageUnevictable is set to indicate the unevictable state.
 *
 * When lazy mlocking via vmscan, it is important to ensure that the
 * vma's VM_LOCKED status is not concurrently being modified, otherwise we
 * may have mlocked a page that is being munlocked. So lazy mlock must take
 * the mmap_sem for read, and verify that the vma really is locked
 * (see mm/rmap.c).
 */

/*
 *  LRU accounting for clear_page_mlock()
 */
void __clear_page_mlock(struct page *page)
{
	VM_BUG_ON(!PageLocked(page));

	if (!page->mapping) {	/* truncated ? */
		return;
	}

	if (!isolate_lru_page(page)) {
		putback_lru_page(page);
	} else {
		/*
		 * Page not on the LRU yet.  Flush all pagevecs and retry.
		 */
		lru_add_drain_all();
		if (!isolate_lru_page(page))
			putback_lru_page(page);
	}
}

/*
 * Mark page as mlocked if not already.
 * If page on LRU, isolate and putback to move to unevictable list.
 */
void mlock_vma_page(struct page *page)
{
	BUG_ON(!PageLocked(page));

	if (!TestSetPageMlocked(page) && !isolate_lru_page(page))
		putback_lru_page(page);
}

/*
 * called from munlock()/munmap() path with page supposedly on the LRU.
 *
 * Note:  unlike mlock_vma_page(), we can't just clear the PageMlocked
 * [in try_to_munlock()] and then attempt to isolate the page.  We must
 * isolate the page to keep others from messing with its unevictable
 * and mlocked state while trying to munlock.  However, we pre-clear the
 * mlocked state anyway as we might lose the isolation race and we might
 * not get another chance to clear PageMlocked.  If we successfully
 * isolate the page and try_to_munlock() detects other VM_LOCKED vmas
 * mapping the page, it will restore the PageMlocked state, unless the page
 * is mapped in a non-linear vma.  So, we go ahead and SetPageMlocked(),
 * perhaps redundantly.
 * If we lose the isolation race, and the page is mapped by other VM_LOCKED
 * vmas, we'll detect this in vmscan--via try_to_munlock() or try_to_unmap()
 * either of which will restore the PageMlocked state by calling
 * mlock_vma_page() above, if it can grab the vma's mmap sem.
 */
static void munlock_vma_page(struct page *page)
{
	BUG_ON(!PageLocked(page));

	if (TestClearPageMlocked(page) && !isolate_lru_page(page)) {
		try_to_munlock(page);
		putback_lru_page(page);
	}
}

/*
 * mlock a range of pages in the vma.
 *
 * This takes care of making the pages present too.
 *
 * vma->vm_mm->mmap_sem must be held for write.
 */
static int __mlock_vma_pages_range(struct vm_area_struct *vma,
			unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long addr = start;
	struct page *pages[16]; /* 16 gives a reasonable batch */
	int write = !!(vma->vm_flags & VM_WRITE);
	int nr_pages = (end - start) / PAGE_SIZE;
	int ret;

	VM_BUG_ON(start & ~PAGE_MASK || end & ~PAGE_MASK);
	VM_BUG_ON(start < vma->vm_start || end > vma->vm_end);
	VM_BUG_ON(!rwsem_is_locked(&vma->vm_mm->mmap_sem));

	lru_add_drain_all();	/* push cached pages to LRU */

	while (nr_pages > 0) {
		int i;

		cond_resched();

		/*
		 * get_user_pages makes pages present if we are
		 * setting mlock. and this extra reference count will
		 * disable migration of this page.  However, page may
		 * still be truncated out from under us.
		 */
		ret = get_user_pages(current, mm, addr,
				min_t(int, nr_pages, ARRAY_SIZE(pages)),
				write, 0, pages, NULL);
		/*
		 * This can happen for, e.g., VM_NONLINEAR regions before
		 * a page has been allocated and mapped at a given offset,
		 * or for addresses that map beyond end of a file.
		 * We'll mlock the the pages if/when they get faulted in.
		 */
		if (ret < 0)
			break;
		if (ret == 0) {
			/*
			 * We know the vma is there, so the only time
			 * we cannot get a single page should be an
			 * error (ret < 0) case.
			 */
			WARN_ON(1);
			break;
		}

		lru_add_drain();	/* push cached pages to LRU */

		for (i = 0; i < ret; i++) {
			struct page *page = pages[i];

			lock_page(page);
			/*
			 * Because we lock page here and migration is blocked
			 * by the elevated reference, we need only check for
			 * page truncation (file-cache only).
			 */
			if (page->mapping)
				mlock_vma_page(page);
			unlock_page(page);
			put_page(page);		/* ref from get_user_pages() */

			/*
			 * here we assume that get_user_pages() has given us
			 * a list of virtually contiguous pages.
			 */
			addr += PAGE_SIZE;	/* for next get_user_pages() */
			nr_pages--;
		}
	}

	lru_add_drain_all();	/* to update stats */

	return 0;	/* count entire vma as locked_vm */
}

/*
 * private structure for munlock page table walk
 */
struct munlock_page_walk {
	struct vm_area_struct *vma;
	pmd_t                 *pmd; /* for migration_entry_wait() */
};

/*
 * munlock normal pages for present ptes
 */
static int __munlock_pte_handler(pte_t *ptep, unsigned long addr,
				   unsigned long end, struct mm_walk *walk)
{
	struct munlock_page_walk *mpw = walk->private;
	swp_entry_t entry;
	struct page *page;
	pte_t pte;

retry:
	pte = *ptep;
	/*
	 * If it's a swap pte, we might be racing with page migration.
	 */
	if (unlikely(!pte_present(pte))) {
		if (!is_swap_pte(pte))
			goto out;
		entry = pte_to_swp_entry(pte);
		if (is_migration_entry(entry)) {
			migration_entry_wait(mpw->vma->vm_mm, mpw->pmd, addr);
			goto retry;
		}
		goto out;
	}

	page = vm_normal_page(mpw->vma, addr, pte);
	if (!page)
		goto out;

	lock_page(page);
	if (!page->mapping) {
		unlock_page(page);
		goto retry;
	}
	munlock_vma_page(page);
	unlock_page(page);

out:
	return 0;
}

/*
 * Save pmd for pte handler for waiting on migration entries
 */
static int __munlock_pmd_handler(pmd_t *pmd, unsigned long addr,
				 unsigned long end, struct mm_walk *walk)
{
	struct munlock_page_walk *mpw = walk->private;

	mpw->pmd = pmd;
	return 0;
}


/*
 * munlock a range of pages in the vma using standard page table walk.
 *
 * vma->vm_mm->mmap_sem must be held for write.
 */
static void __munlock_vma_pages_range(struct vm_area_struct *vma,
			      unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	struct munlock_page_walk mpw = {
		.vma = vma,
	};
	struct mm_walk munlock_page_walk = {
		.pmd_entry = __munlock_pmd_handler,
		.pte_entry = __munlock_pte_handler,
		.private = &mpw,
		.mm = mm,
	};

	VM_BUG_ON(start & ~PAGE_MASK || end & ~PAGE_MASK);
	VM_BUG_ON(!rwsem_is_locked(&vma->vm_mm->mmap_sem));
	VM_BUG_ON(start < vma->vm_start);
	VM_BUG_ON(end > vma->vm_end);

	lru_add_drain_all();	/* push cached pages to LRU */
	walk_page_range(start, end, &munlock_page_walk);
	lru_add_drain_all();	/* to update stats */
}

#else /* CONFIG_UNEVICTABLE_LRU */

/*
 * Just make pages present if VM_LOCKED.  No-op if unlocking.
 */
static int __mlock_vma_pages_range(struct vm_area_struct *vma,
			unsigned long start, unsigned long end)
{
	if (vma->vm_flags & VM_LOCKED)
		make_pages_present(start, end);
	return 0;
}

/*
 * munlock a range of pages in the vma -- no-op.
 */
static void __munlock_vma_pages_range(struct vm_area_struct *vma,
			      unsigned long start, unsigned long end)
{
}
#endif /* CONFIG_UNEVICTABLE_LRU */

/*
 * mlock all pages in this vma range.  For mmap()/mremap()/...
 */
int mlock_vma_pages_range(struct vm_area_struct *vma,
			unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	int nr_pages = (end - start) / PAGE_SIZE;
	BUG_ON(!(vma->vm_flags & VM_LOCKED));

	/*
	 * filter unlockable vmas
	 */
	if (vma->vm_flags & (VM_IO | VM_PFNMAP))
		goto no_mlock;

	if (!((vma->vm_flags & (VM_DONTEXPAND | VM_RESERVED)) ||
			is_vm_hugetlb_page(vma) ||
			vma == get_gate_vma(current))) {
		downgrade_write(&mm->mmap_sem);
		nr_pages = __mlock_vma_pages_range(vma, start, end);

		up_read(&mm->mmap_sem);
		/* vma can change or disappear */
		down_write(&mm->mmap_sem);
		vma = find_vma(mm, start);
		/* non-NULL vma must contain @start, but need to check @end */
		if (!vma ||  end > vma->vm_end)
			return -EAGAIN;
		return nr_pages;
	}

	/*
	 * User mapped kernel pages or huge pages:
	 * make these pages present to populate the ptes, but
	 * fall thru' to reset VM_LOCKED--no need to unlock, and
	 * return nr_pages so these don't get counted against task's
	 * locked limit.  huge pages are already counted against
	 * locked vm limit.
	 */
	make_pages_present(start, end);

no_mlock:
	vma->vm_flags &= ~VM_LOCKED;	/* and don't come back! */
	return nr_pages;		/* pages NOT mlocked */
}


/*
 * munlock all pages in vma.   For munmap() and exit().
 */
void munlock_vma_pages_all(struct vm_area_struct *vma)
{
	vma->vm_flags &= ~VM_LOCKED;
	__munlock_vma_pages_range(vma, vma->vm_start, vma->vm_end);
}

/*
 * mlock_fixup  - handle mlock[all]/munlock[all] requests.
 *
 * Filters out "special" vmas -- VM_LOCKED never gets set for these, and
 * munlock is a no-op.  However, for some special vmas, we go ahead and
 * populate the ptes via make_pages_present().
 *
 * For vmas that pass the filters, merge/split as appropriate.
 */
static int mlock_fixup(struct vm_area_struct *vma, struct vm_area_struct **prev,
	unsigned long start, unsigned long end, unsigned int newflags)
{
	struct mm_struct *mm = vma->vm_mm;
	pgoff_t pgoff;
	int nr_pages;
	int ret = 0;
	int lock = newflags & VM_LOCKED;

	if (newflags == vma->vm_flags ||
			(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		goto out;	/* don't set VM_LOCKED,  don't count */

	if ((vma->vm_flags & (VM_DONTEXPAND | VM_RESERVED)) ||
			is_vm_hugetlb_page(vma) ||
			vma == get_gate_vma(current)) {
		if (lock)
			make_pages_present(start, end);
		goto out;	/* don't set VM_LOCKED,  don't count */
	}

	pgoff = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
	*prev = vma_merge(mm, *prev, start, end, newflags, vma->anon_vma,
			  vma->vm_file, pgoff, vma_policy(vma));
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
	mm->locked_vm += nr_pages;

	/*
	 * vm_flags is protected by the mmap_sem held in write mode.
	 * It's okay if try_to_unmap_one unmaps a page just after we
	 * set VM_LOCKED, __mlock_vma_pages_range will bring it back.
	 */
	vma->vm_flags = newflags;

	if (lock) {
		/*
		 * mmap_sem is currently held for write.  Downgrade the write
		 * lock to a read lock so that other faults, mmap scans, ...
		 * while we fault in all pages.
		 */
		downgrade_write(&mm->mmap_sem);

		ret = __mlock_vma_pages_range(vma, start, end);
		if (ret > 0) {
			mm->locked_vm -= ret;
			ret = 0;
		}
		/*
		 * Need to reacquire mmap sem in write mode, as our callers
		 * expect this.  We have no support for atomically upgrading
		 * a sem to write, so we need to check for ranges while sem
		 * is unlocked.
		 */
		up_read(&mm->mmap_sem);
		/* vma can change or disappear */
		down_write(&mm->mmap_sem);
		*prev = find_vma(mm, start);
		/* non-NULL *prev must contain @start, but need to check @end */
		if (!(*prev) || end > (*prev)->vm_end)
			ret = -EAGAIN;
	} else {
		/*
		 * TODO:  for unlocking, pages will already be resident, so
		 * we don't need to wait for allocations/reclaim/pagein, ...
		 * However, unlocking a very large region can still take a
		 * while.  Should we downgrade the semaphore for both lock
		 * AND unlock ?
		 */
		__munlock_vma_pages_range(vma, start, end);
	}

out:
	*prev = vma;
	return ret;
}

static int do_mlock(unsigned long start, size_t len, int on)
{
	unsigned long nstart, end, tmp;
	struct vm_area_struct * vma, * prev;
	int error;

	len = PAGE_ALIGN(len);
	end = start + len;
	if (end < start)
		return -EINVAL;
	if (end == start)
		return 0;
	vma = find_vma_prev(current->mm, start, &prev);
	if (!vma || vma->vm_start > start)
		return -ENOMEM;

	if (start > vma->vm_start)
		prev = vma;

	for (nstart = start ; ; ) {
		unsigned int newflags;

		/* Here we know that  vma->vm_start <= nstart < vma->vm_end. */

		newflags = vma->vm_flags | VM_LOCKED;
		if (!on)
			newflags &= ~VM_LOCKED;

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

asmlinkage long sys_mlock(unsigned long start, size_t len)
{
	unsigned long locked;
	unsigned long lock_limit;
	int error = -ENOMEM;

	if (!can_do_mlock())
		return -EPERM;

	down_write(&current->mm->mmap_sem);
	len = PAGE_ALIGN(len + (start & ~PAGE_MASK));
	start &= PAGE_MASK;

	locked = len >> PAGE_SHIFT;
	locked += current->mm->locked_vm;

	lock_limit = current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur;
	lock_limit >>= PAGE_SHIFT;

	/* check against resource limits */
	if ((locked <= lock_limit) || capable(CAP_IPC_LOCK))
		error = do_mlock(start, len, 1);
	up_write(&current->mm->mmap_sem);
	return error;
}

asmlinkage long sys_munlock(unsigned long start, size_t len)
{
	int ret;

	down_write(&current->mm->mmap_sem);
	len = PAGE_ALIGN(len + (start & ~PAGE_MASK));
	start &= PAGE_MASK;
	ret = do_mlock(start, len, 0);
	up_write(&current->mm->mmap_sem);
	return ret;
}

static int do_mlockall(int flags)
{
	struct vm_area_struct * vma, * prev = NULL;
	unsigned int def_flags = 0;

	if (flags & MCL_FUTURE)
		def_flags = VM_LOCKED;
	current->mm->def_flags = def_flags;
	if (flags == MCL_FUTURE)
		goto out;

	for (vma = current->mm->mmap; vma ; vma = prev->vm_next) {
		unsigned int newflags;

		newflags = vma->vm_flags | VM_LOCKED;
		if (!(flags & MCL_CURRENT))
			newflags &= ~VM_LOCKED;

		/* Ignore errors */
		mlock_fixup(vma, &prev, vma->vm_start, vma->vm_end, newflags);
	}
out:
	return 0;
}

asmlinkage long sys_mlockall(int flags)
{
	unsigned long lock_limit;
	int ret = -EINVAL;

	if (!flags || (flags & ~(MCL_CURRENT | MCL_FUTURE)))
		goto out;

	ret = -EPERM;
	if (!can_do_mlock())
		goto out;

	down_write(&current->mm->mmap_sem);

	lock_limit = current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur;
	lock_limit >>= PAGE_SHIFT;

	ret = -ENOMEM;
	if (!(flags & MCL_CURRENT) || (current->mm->total_vm <= lock_limit) ||
	    capable(CAP_IPC_LOCK))
		ret = do_mlockall(flags);
	up_write(&current->mm->mmap_sem);
out:
	return ret;
}

asmlinkage long sys_munlockall(void)
{
	int ret;

	down_write(&current->mm->mmap_sem);
	ret = do_mlockall(0);
	up_write(&current->mm->mmap_sem);
	return ret;
}

/*
 * Objects with different lifetime than processes (SHM_LOCK and SHM_HUGETLB
 * shm segments) get accounted against the user_struct instead.
 */
static DEFINE_SPINLOCK(shmlock_user_lock);

int user_shm_lock(size_t size, struct user_struct *user)
{
	unsigned long lock_limit, locked;
	int allowed = 0;

	locked = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	lock_limit = current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur;
	if (lock_limit == RLIM_INFINITY)
		allowed = 1;
	lock_limit >>= PAGE_SHIFT;
	spin_lock(&shmlock_user_lock);
	if (!allowed &&
	    locked + user->locked_shm > lock_limit && !capable(CAP_IPC_LOCK))
		goto out;
	get_uid(user);
	user->locked_shm += locked;
	allowed = 1;
out:
	spin_unlock(&shmlock_user_lock);
	return allowed;
}

void user_shm_unlock(size_t size, struct user_struct *user)
{
	spin_lock(&shmlock_user_lock);
	user->locked_shm -= (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	spin_unlock(&shmlock_user_lock);
	free_uid(user);
}
