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
#include <linux/pagewalk.h>
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

struct mlock_pvec {
	local_lock_t lock;
	struct pagevec vec;
};

static DEFINE_PER_CPU(struct mlock_pvec, mlock_pvec) = {
	.lock = INIT_LOCAL_LOCK(lock),
};

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

static struct lruvec *__mlock_page(struct page *page, struct lruvec *lruvec)
{
	/* There is nothing more we can do while it's off LRU */
	if (!TestClearPageLRU(page))
		return lruvec;

	lruvec = folio_lruvec_relock_irq(page_folio(page), lruvec);

	if (unlikely(page_evictable(page))) {
		/*
		 * This is a little surprising, but quite possible:
		 * PageMlocked must have got cleared already by another CPU.
		 * Could this page be on the Unevictable LRU?  I'm not sure,
		 * but move it now if so.
		 */
		if (PageUnevictable(page)) {
			del_page_from_lru_list(page, lruvec);
			ClearPageUnevictable(page);
			add_page_to_lru_list(page, lruvec);
			__count_vm_events(UNEVICTABLE_PGRESCUED,
					  thp_nr_pages(page));
		}
		goto out;
	}

	if (PageUnevictable(page)) {
		if (PageMlocked(page))
			page->mlock_count++;
		goto out;
	}

	del_page_from_lru_list(page, lruvec);
	ClearPageActive(page);
	SetPageUnevictable(page);
	page->mlock_count = !!PageMlocked(page);
	add_page_to_lru_list(page, lruvec);
	__count_vm_events(UNEVICTABLE_PGCULLED, thp_nr_pages(page));
out:
	SetPageLRU(page);
	return lruvec;
}

static struct lruvec *__mlock_new_page(struct page *page, struct lruvec *lruvec)
{
	VM_BUG_ON_PAGE(PageLRU(page), page);

	lruvec = folio_lruvec_relock_irq(page_folio(page), lruvec);

	/* As above, this is a little surprising, but possible */
	if (unlikely(page_evictable(page)))
		goto out;

	SetPageUnevictable(page);
	page->mlock_count = !!PageMlocked(page);
	__count_vm_events(UNEVICTABLE_PGCULLED, thp_nr_pages(page));
out:
	add_page_to_lru_list(page, lruvec);
	SetPageLRU(page);
	return lruvec;
}

static struct lruvec *__munlock_page(struct page *page, struct lruvec *lruvec)
{
	int nr_pages = thp_nr_pages(page);
	bool isolated = false;

	if (!TestClearPageLRU(page))
		goto munlock;

	isolated = true;
	lruvec = folio_lruvec_relock_irq(page_folio(page), lruvec);

	if (PageUnevictable(page)) {
		/* Then mlock_count is maintained, but might undercount */
		if (page->mlock_count)
			page->mlock_count--;
		if (page->mlock_count)
			goto out;
	}
	/* else assume that was the last mlock: reclaim will fix it if not */

munlock:
	if (TestClearPageMlocked(page)) {
		__mod_zone_page_state(page_zone(page), NR_MLOCK, -nr_pages);
		if (isolated || !PageUnevictable(page))
			__count_vm_events(UNEVICTABLE_PGMUNLOCKED, nr_pages);
		else
			__count_vm_events(UNEVICTABLE_PGSTRANDED, nr_pages);
	}

	/* page_evictable() has to be checked *after* clearing Mlocked */
	if (isolated && PageUnevictable(page) && page_evictable(page)) {
		del_page_from_lru_list(page, lruvec);
		ClearPageUnevictable(page);
		add_page_to_lru_list(page, lruvec);
		__count_vm_events(UNEVICTABLE_PGRESCUED, nr_pages);
	}
out:
	if (isolated)
		SetPageLRU(page);
	return lruvec;
}

/*
 * Flags held in the low bits of a struct page pointer on the mlock_pvec.
 */
#define LRU_PAGE 0x1
#define NEW_PAGE 0x2
static inline struct page *mlock_lru(struct page *page)
{
	return (struct page *)((unsigned long)page + LRU_PAGE);
}

static inline struct page *mlock_new(struct page *page)
{
	return (struct page *)((unsigned long)page + NEW_PAGE);
}

/*
 * mlock_pagevec() is derived from pagevec_lru_move_fn():
 * perhaps that can make use of such page pointer flags in future,
 * but for now just keep it for mlock.  We could use three separate
 * pagevecs instead, but one feels better (munlocking a full pagevec
 * does not need to drain mlocking pagevecs first).
 */
static void mlock_pagevec(struct pagevec *pvec)
{
	struct lruvec *lruvec = NULL;
	unsigned long mlock;
	struct page *page;
	int i;

	for (i = 0; i < pagevec_count(pvec); i++) {
		page = pvec->pages[i];
		mlock = (unsigned long)page & (LRU_PAGE | NEW_PAGE);
		page = (struct page *)((unsigned long)page - mlock);
		pvec->pages[i] = page;

		if (mlock & LRU_PAGE)
			lruvec = __mlock_page(page, lruvec);
		else if (mlock & NEW_PAGE)
			lruvec = __mlock_new_page(page, lruvec);
		else
			lruvec = __munlock_page(page, lruvec);
	}

	if (lruvec)
		unlock_page_lruvec_irq(lruvec);
	release_pages(pvec->pages, pvec->nr);
	pagevec_reinit(pvec);
}

void mlock_page_drain_local(void)
{
	struct pagevec *pvec;

	local_lock(&mlock_pvec.lock);
	pvec = this_cpu_ptr(&mlock_pvec.vec);
	if (pagevec_count(pvec))
		mlock_pagevec(pvec);
	local_unlock(&mlock_pvec.lock);
}

void mlock_page_drain_remote(int cpu)
{
	struct pagevec *pvec;

	WARN_ON_ONCE(cpu_online(cpu));
	pvec = &per_cpu(mlock_pvec.vec, cpu);
	if (pagevec_count(pvec))
		mlock_pagevec(pvec);
}

bool need_mlock_page_drain(int cpu)
{
	return pagevec_count(&per_cpu(mlock_pvec.vec, cpu));
}

/**
 * mlock_folio - mlock a folio already on (or temporarily off) LRU
 * @folio: folio to be mlocked.
 */
void mlock_folio(struct folio *folio)
{
	struct pagevec *pvec;

	local_lock(&mlock_pvec.lock);
	pvec = this_cpu_ptr(&mlock_pvec.vec);

	if (!folio_test_set_mlocked(folio)) {
		int nr_pages = folio_nr_pages(folio);

		zone_stat_mod_folio(folio, NR_MLOCK, nr_pages);
		__count_vm_events(UNEVICTABLE_PGMLOCKED, nr_pages);
	}

	folio_get(folio);
	if (!pagevec_add(pvec, mlock_lru(&folio->page)) ||
	    folio_test_large(folio) || lru_cache_disabled())
		mlock_pagevec(pvec);
	local_unlock(&mlock_pvec.lock);
}

/**
 * mlock_new_page - mlock a newly allocated page not yet on LRU
 * @page: page to be mlocked, either a normal page or a THP head.
 */
void mlock_new_page(struct page *page)
{
	struct pagevec *pvec;
	int nr_pages = thp_nr_pages(page);

	local_lock(&mlock_pvec.lock);
	pvec = this_cpu_ptr(&mlock_pvec.vec);
	SetPageMlocked(page);
	mod_zone_page_state(page_zone(page), NR_MLOCK, nr_pages);
	__count_vm_events(UNEVICTABLE_PGMLOCKED, nr_pages);

	get_page(page);
	if (!pagevec_add(pvec, mlock_new(page)) ||
	    PageHead(page) || lru_cache_disabled())
		mlock_pagevec(pvec);
	local_unlock(&mlock_pvec.lock);
}

/**
 * munlock_page - munlock a page
 * @page: page to be munlocked, either a normal page or a THP head.
 */
void munlock_page(struct page *page)
{
	struct pagevec *pvec;

	local_lock(&mlock_pvec.lock);
	pvec = this_cpu_ptr(&mlock_pvec.vec);
	/*
	 * TestClearPageMlocked(page) must be left to __munlock_page(),
	 * which will check whether the page is multiply mlocked.
	 */

	get_page(page);
	if (!pagevec_add(pvec, page) ||
	    PageHead(page) || lru_cache_disabled())
		mlock_pagevec(pvec);
	local_unlock(&mlock_pvec.lock);
}

static int mlock_pte_range(pmd_t *pmd, unsigned long addr,
			   unsigned long end, struct mm_walk *walk)

{
	struct vm_area_struct *vma = walk->vma;
	spinlock_t *ptl;
	pte_t *start_pte, *pte;
	struct page *page;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		if (!pmd_present(*pmd))
			goto out;
		if (is_huge_zero_pmd(*pmd))
			goto out;
		page = pmd_page(*pmd);
		if (vma->vm_flags & VM_LOCKED)
			mlock_folio(page_folio(page));
		else
			munlock_page(page);
		goto out;
	}

	start_pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (pte = start_pte; addr != end; pte++, addr += PAGE_SIZE) {
		if (!pte_present(*pte))
			continue;
		page = vm_normal_page(vma, addr, *pte);
		if (!page || is_zone_device_page(page))
			continue;
		if (PageTransCompound(page))
			continue;
		if (vma->vm_flags & VM_LOCKED)
			mlock_folio(page_folio(page));
		else
			munlock_page(page);
	}
	pte_unmap(start_pte);
out:
	spin_unlock(ptl);
	cond_resched();
	return 0;
}

/*
 * mlock_vma_pages_range() - mlock any pages already in the range,
 *                           or munlock all pages in the range.
 * @vma - vma containing range to be mlock()ed or munlock()ed
 * @start - start address in @vma of the range
 * @end - end of range in @vma
 * @newflags - the new set of flags for @vma.
 *
 * Called for mlock(), mlock2() and mlockall(), to set @vma VM_LOCKED;
 * called for munlock() and munlockall(), to clear VM_LOCKED from @vma.
 */
static void mlock_vma_pages_range(struct vm_area_struct *vma,
	unsigned long start, unsigned long end, vm_flags_t newflags)
{
	static const struct mm_walk_ops mlock_walk_ops = {
		.pmd_entry = mlock_pte_range,
	};

	/*
	 * There is a slight chance that concurrent page migration,
	 * or page reclaim finding a page of this now-VM_LOCKED vma,
	 * will call mlock_vma_page() and raise page's mlock_count:
	 * double counting, leaving the page unevictable indefinitely.
	 * Communicate this danger to mlock_vma_page() with VM_IO,
	 * which is a VM_SPECIAL flag not allowed on VM_LOCKED vmas.
	 * mmap_lock is held in write mode here, so this weird
	 * combination should not be visible to other mmap_lock users;
	 * but WRITE_ONCE so rmap walkers must see VM_IO if VM_LOCKED.
	 */
	if (newflags & VM_LOCKED)
		newflags |= VM_IO;
	WRITE_ONCE(vma->vm_flags, newflags);

	lru_add_drain();
	walk_page_range(vma->vm_mm, start, end, &mlock_walk_ops, NULL);
	lru_add_drain();

	if (newflags & VM_IO) {
		newflags &= ~VM_IO;
		WRITE_ONCE(vma->vm_flags, newflags);
	}
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
	vm_flags_t oldflags = vma->vm_flags;

	if (newflags == oldflags || (oldflags & VM_SPECIAL) ||
	    is_vm_hugetlb_page(vma) || vma == get_gate_vma(current->mm) ||
	    vma_is_dax(vma) || vma_is_secretmem(vma))
		/* don't set VM_LOCKED or VM_LOCKONFAULT and don't count */
		goto out;

	pgoff = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
	*prev = vma_merge(mm, *prev, start, end, newflags, vma->anon_vma,
			  vma->vm_file, pgoff, vma_policy(vma),
			  vma->vm_userfaultfd_ctx, anon_vma_name(vma));
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
	if (!(newflags & VM_LOCKED))
		nr_pages = -nr_pages;
	else if (oldflags & VM_LOCKED)
		nr_pages = 0;
	mm->locked_vm += nr_pages;

	/*
	 * vm_flags is protected by the mmap_lock held in write mode.
	 * It's okay if try_to_unmap_one unmaps a page just after we
	 * set VM_LOCKED, populate_vma_page_range will bring it back.
	 */

	if ((newflags & VM_LOCKED) && (oldflags & VM_LOCKED)) {
		/* No work to do, and mlocking twice would be wrong */
		vma->vm_flags = newflags;
	} else {
		mlock_vma_pages_range(vma, start, end, newflags);
	}
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
	MA_STATE(mas, &current->mm->mm_mt, start, start);

	VM_BUG_ON(offset_in_page(start));
	VM_BUG_ON(len != PAGE_ALIGN(len));
	end = start + len;
	if (end < start)
		return -EINVAL;
	if (end == start)
		return 0;
	vma = mas_walk(&mas);
	if (!vma)
		return -ENOMEM;

	if (start > vma->vm_start)
		prev = vma;
	else
		prev = mas_prev(&mas, 0);

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

		vma = find_vma(prev->vm_mm, prev->vm_end);
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
	unsigned long end;
	VMA_ITERATOR(vmi, mm, start);

	/* Don't overflow past ULONG_MAX */
	if (unlikely(ULONG_MAX - len < start))
		end = ULONG_MAX;
	else
		end = start + len;

	for_each_vma_range(vmi, vma, end) {
		if (vma->vm_flags & VM_LOCKED) {
			if (start > vma->vm_start)
				count -= (start - vma->vm_start);
			if (end < vma->vm_end) {
				count += end - vma->vm_start;
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
	MA_STATE(mas, &current->mm->mm_mt, 0, 0);
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

	mas_for_each(&mas, vma, ULONG_MAX) {
		vm_flags_t newflags;

		newflags = vma->vm_flags & VM_LOCKED_CLEAR_MASK;
		newflags |= to_add;

		/* Ignore errors */
		mlock_fixup(vma, &prev, vma->vm_start, vma->vm_end, newflags);
		mas_pause(&mas);
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
	if (lock_limit != RLIM_INFINITY)
		lock_limit >>= PAGE_SHIFT;
	spin_lock(&shmlock_user_lock);
	memlock = inc_rlimit_ucounts(ucounts, UCOUNT_RLIMIT_MEMLOCK, locked);

	if ((memlock == LONG_MAX || memlock > lock_limit) && !capable(CAP_IPC_LOCK)) {
		dec_rlimit_ucounts(ucounts, UCOUNT_RLIMIT_MEMLOCK, locked);
		goto out;
	}
	if (!get_ucounts(ucounts)) {
		dec_rlimit_ucounts(ucounts, UCOUNT_RLIMIT_MEMLOCK, locked);
		allowed = 0;
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
