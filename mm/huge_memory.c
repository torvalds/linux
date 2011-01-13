/*
 *  Copyright (C) 2009  Red Hat, Inc.
 *
 *  This work is licensed under the terms of the GNU GPL, version 2. See
 *  the COPYING file in the top-level directory.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/mmu_notifier.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <asm/tlb.h>
#include <asm/pgalloc.h>
#include "internal.h"

unsigned long transparent_hugepage_flags __read_mostly =
	(1<<TRANSPARENT_HUGEPAGE_FLAG);

#ifdef CONFIG_SYSFS
static ssize_t double_flag_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf,
				enum transparent_hugepage_flag enabled,
				enum transparent_hugepage_flag req_madv)
{
	if (test_bit(enabled, &transparent_hugepage_flags)) {
		VM_BUG_ON(test_bit(req_madv, &transparent_hugepage_flags));
		return sprintf(buf, "[always] madvise never\n");
	} else if (test_bit(req_madv, &transparent_hugepage_flags))
		return sprintf(buf, "always [madvise] never\n");
	else
		return sprintf(buf, "always madvise [never]\n");
}
static ssize_t double_flag_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count,
				 enum transparent_hugepage_flag enabled,
				 enum transparent_hugepage_flag req_madv)
{
	if (!memcmp("always", buf,
		    min(sizeof("always")-1, count))) {
		set_bit(enabled, &transparent_hugepage_flags);
		clear_bit(req_madv, &transparent_hugepage_flags);
	} else if (!memcmp("madvise", buf,
			   min(sizeof("madvise")-1, count))) {
		clear_bit(enabled, &transparent_hugepage_flags);
		set_bit(req_madv, &transparent_hugepage_flags);
	} else if (!memcmp("never", buf,
			   min(sizeof("never")-1, count))) {
		clear_bit(enabled, &transparent_hugepage_flags);
		clear_bit(req_madv, &transparent_hugepage_flags);
	} else
		return -EINVAL;

	return count;
}

static ssize_t enabled_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	return double_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_FLAG,
				TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG);
}
static ssize_t enabled_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	return double_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_FLAG,
				 TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG);
}
static struct kobj_attribute enabled_attr =
	__ATTR(enabled, 0644, enabled_show, enabled_store);

static ssize_t single_flag_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf,
				enum transparent_hugepage_flag flag)
{
	if (test_bit(flag, &transparent_hugepage_flags))
		return sprintf(buf, "[yes] no\n");
	else
		return sprintf(buf, "yes [no]\n");
}
static ssize_t single_flag_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count,
				 enum transparent_hugepage_flag flag)
{
	if (!memcmp("yes", buf,
		    min(sizeof("yes")-1, count))) {
		set_bit(flag, &transparent_hugepage_flags);
	} else if (!memcmp("no", buf,
			   min(sizeof("no")-1, count))) {
		clear_bit(flag, &transparent_hugepage_flags);
	} else
		return -EINVAL;

	return count;
}

/*
 * Currently defrag only disables __GFP_NOWAIT for allocation. A blind
 * __GFP_REPEAT is too aggressive, it's never worth swapping tons of
 * memory just to allocate one more hugepage.
 */
static ssize_t defrag_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	return double_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_DEFRAG_FLAG,
				TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG);
}
static ssize_t defrag_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	return double_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_DEFRAG_FLAG,
				 TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG);
}
static struct kobj_attribute defrag_attr =
	__ATTR(defrag, 0644, defrag_show, defrag_store);

#ifdef CONFIG_DEBUG_VM
static ssize_t debug_cow_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return single_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_DEBUG_COW_FLAG);
}
static ssize_t debug_cow_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	return single_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_DEBUG_COW_FLAG);
}
static struct kobj_attribute debug_cow_attr =
	__ATTR(debug_cow, 0644, debug_cow_show, debug_cow_store);
#endif /* CONFIG_DEBUG_VM */

static struct attribute *hugepage_attr[] = {
	&enabled_attr.attr,
	&defrag_attr.attr,
#ifdef CONFIG_DEBUG_VM
	&debug_cow_attr.attr,
#endif
	NULL,
};

static struct attribute_group hugepage_attr_group = {
	.attrs = hugepage_attr,
	.name = "transparent_hugepage",
};
#endif /* CONFIG_SYSFS */

static int __init hugepage_init(void)
{
#ifdef CONFIG_SYSFS
	int err;

	err = sysfs_create_group(mm_kobj, &hugepage_attr_group);
	if (err)
		printk(KERN_ERR "hugepage: register sysfs failed\n");
#endif
	return 0;
}
module_init(hugepage_init)

static int __init setup_transparent_hugepage(char *str)
{
	int ret = 0;
	if (!str)
		goto out;
	if (!strcmp(str, "always")) {
		set_bit(TRANSPARENT_HUGEPAGE_FLAG,
			&transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
			  &transparent_hugepage_flags);
		ret = 1;
	} else if (!strcmp(str, "madvise")) {
		clear_bit(TRANSPARENT_HUGEPAGE_FLAG,
			  &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
			&transparent_hugepage_flags);
		ret = 1;
	} else if (!strcmp(str, "never")) {
		clear_bit(TRANSPARENT_HUGEPAGE_FLAG,
			  &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
			  &transparent_hugepage_flags);
		ret = 1;
	}
out:
	if (!ret)
		printk(KERN_WARNING
		       "transparent_hugepage= cannot parse, ignored\n");
	return ret;
}
__setup("transparent_hugepage=", setup_transparent_hugepage);

static void prepare_pmd_huge_pte(pgtable_t pgtable,
				 struct mm_struct *mm)
{
	assert_spin_locked(&mm->page_table_lock);

	/* FIFO */
	if (!mm->pmd_huge_pte)
		INIT_LIST_HEAD(&pgtable->lru);
	else
		list_add(&pgtable->lru, &mm->pmd_huge_pte->lru);
	mm->pmd_huge_pte = pgtable;
}

static inline pmd_t maybe_pmd_mkwrite(pmd_t pmd, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pmd = pmd_mkwrite(pmd);
	return pmd;
}

static int __do_huge_pmd_anonymous_page(struct mm_struct *mm,
					struct vm_area_struct *vma,
					unsigned long haddr, pmd_t *pmd,
					struct page *page)
{
	int ret = 0;
	pgtable_t pgtable;

	VM_BUG_ON(!PageCompound(page));
	pgtable = pte_alloc_one(mm, haddr);
	if (unlikely(!pgtable)) {
		mem_cgroup_uncharge_page(page);
		put_page(page);
		return VM_FAULT_OOM;
	}

	clear_huge_page(page, haddr, HPAGE_PMD_NR);
	__SetPageUptodate(page);

	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_none(*pmd))) {
		spin_unlock(&mm->page_table_lock);
		mem_cgroup_uncharge_page(page);
		put_page(page);
		pte_free(mm, pgtable);
	} else {
		pmd_t entry;
		entry = mk_pmd(page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		entry = pmd_mkhuge(entry);
		/*
		 * The spinlocking to take the lru_lock inside
		 * page_add_new_anon_rmap() acts as a full memory
		 * barrier to be sure clear_huge_page writes become
		 * visible after the set_pmd_at() write.
		 */
		page_add_new_anon_rmap(page, vma, haddr);
		set_pmd_at(mm, haddr, pmd, entry);
		prepare_pmd_huge_pte(pgtable, mm);
		add_mm_counter(mm, MM_ANONPAGES, HPAGE_PMD_NR);
		spin_unlock(&mm->page_table_lock);
	}

	return ret;
}

static inline struct page *alloc_hugepage(int defrag)
{
	return alloc_pages(GFP_TRANSHUGE & ~(defrag ? 0 : __GFP_WAIT),
			   HPAGE_PMD_ORDER);
}

int do_huge_pmd_anonymous_page(struct mm_struct *mm, struct vm_area_struct *vma,
			       unsigned long address, pmd_t *pmd,
			       unsigned int flags)
{
	struct page *page;
	unsigned long haddr = address & HPAGE_PMD_MASK;
	pte_t *pte;

	if (haddr >= vma->vm_start && haddr + HPAGE_PMD_SIZE <= vma->vm_end) {
		if (unlikely(anon_vma_prepare(vma)))
			return VM_FAULT_OOM;
		page = alloc_hugepage(transparent_hugepage_defrag(vma));
		if (unlikely(!page))
			goto out;
		if (unlikely(mem_cgroup_newpage_charge(page, mm, GFP_KERNEL))) {
			put_page(page);
			goto out;
		}

		return __do_huge_pmd_anonymous_page(mm, vma, haddr, pmd, page);
	}
out:
	/*
	 * Use __pte_alloc instead of pte_alloc_map, because we can't
	 * run pte_offset_map on the pmd, if an huge pmd could
	 * materialize from under us from a different thread.
	 */
	if (unlikely(__pte_alloc(mm, vma, pmd, address)))
		return VM_FAULT_OOM;
	/* if an huge pmd materialized from under us just retry later */
	if (unlikely(pmd_trans_huge(*pmd)))
		return 0;
	/*
	 * A regular pmd is established and it can't morph into a huge pmd
	 * from under us anymore at this point because we hold the mmap_sem
	 * read mode and khugepaged takes it in write mode. So now it's
	 * safe to run pte_offset_map().
	 */
	pte = pte_offset_map(pmd, address);
	return handle_pte_fault(mm, vma, address, pte, pmd, flags);
}

int copy_huge_pmd(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
		  struct vm_area_struct *vma)
{
	struct page *src_page;
	pmd_t pmd;
	pgtable_t pgtable;
	int ret;

	ret = -ENOMEM;
	pgtable = pte_alloc_one(dst_mm, addr);
	if (unlikely(!pgtable))
		goto out;

	spin_lock(&dst_mm->page_table_lock);
	spin_lock_nested(&src_mm->page_table_lock, SINGLE_DEPTH_NESTING);

	ret = -EAGAIN;
	pmd = *src_pmd;
	if (unlikely(!pmd_trans_huge(pmd))) {
		pte_free(dst_mm, pgtable);
		goto out_unlock;
	}
	if (unlikely(pmd_trans_splitting(pmd))) {
		/* split huge page running from under us */
		spin_unlock(&src_mm->page_table_lock);
		spin_unlock(&dst_mm->page_table_lock);
		pte_free(dst_mm, pgtable);

		wait_split_huge_page(vma->anon_vma, src_pmd); /* src_vma */
		goto out;
	}
	src_page = pmd_page(pmd);
	VM_BUG_ON(!PageHead(src_page));
	get_page(src_page);
	page_dup_rmap(src_page);
	add_mm_counter(dst_mm, MM_ANONPAGES, HPAGE_PMD_NR);

	pmdp_set_wrprotect(src_mm, addr, src_pmd);
	pmd = pmd_mkold(pmd_wrprotect(pmd));
	set_pmd_at(dst_mm, addr, dst_pmd, pmd);
	prepare_pmd_huge_pte(pgtable, dst_mm);

	ret = 0;
out_unlock:
	spin_unlock(&src_mm->page_table_lock);
	spin_unlock(&dst_mm->page_table_lock);
out:
	return ret;
}

/* no "address" argument so destroys page coloring of some arch */
pgtable_t get_pmd_huge_pte(struct mm_struct *mm)
{
	pgtable_t pgtable;

	assert_spin_locked(&mm->page_table_lock);

	/* FIFO */
	pgtable = mm->pmd_huge_pte;
	if (list_empty(&pgtable->lru))
		mm->pmd_huge_pte = NULL;
	else {
		mm->pmd_huge_pte = list_entry(pgtable->lru.next,
					      struct page, lru);
		list_del(&pgtable->lru);
	}
	return pgtable;
}

static int do_huge_pmd_wp_page_fallback(struct mm_struct *mm,
					struct vm_area_struct *vma,
					unsigned long address,
					pmd_t *pmd, pmd_t orig_pmd,
					struct page *page,
					unsigned long haddr)
{
	pgtable_t pgtable;
	pmd_t _pmd;
	int ret = 0, i;
	struct page **pages;

	pages = kmalloc(sizeof(struct page *) * HPAGE_PMD_NR,
			GFP_KERNEL);
	if (unlikely(!pages)) {
		ret |= VM_FAULT_OOM;
		goto out;
	}

	for (i = 0; i < HPAGE_PMD_NR; i++) {
		pages[i] = alloc_page_vma(GFP_HIGHUSER_MOVABLE,
					  vma, address);
		if (unlikely(!pages[i] ||
			     mem_cgroup_newpage_charge(pages[i], mm,
						       GFP_KERNEL))) {
			if (pages[i])
				put_page(pages[i]);
			mem_cgroup_uncharge_start();
			while (--i >= 0) {
				mem_cgroup_uncharge_page(pages[i]);
				put_page(pages[i]);
			}
			mem_cgroup_uncharge_end();
			kfree(pages);
			ret |= VM_FAULT_OOM;
			goto out;
		}
	}

	for (i = 0; i < HPAGE_PMD_NR; i++) {
		copy_user_highpage(pages[i], page + i,
				   haddr + PAGE_SHIFT*i, vma);
		__SetPageUptodate(pages[i]);
		cond_resched();
	}

	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_same(*pmd, orig_pmd)))
		goto out_free_pages;
	VM_BUG_ON(!PageHead(page));

	pmdp_clear_flush_notify(vma, haddr, pmd);
	/* leave pmd empty until pte is filled */

	pgtable = get_pmd_huge_pte(mm);
	pmd_populate(mm, &_pmd, pgtable);

	for (i = 0; i < HPAGE_PMD_NR; i++, haddr += PAGE_SIZE) {
		pte_t *pte, entry;
		entry = mk_pte(pages[i], vma->vm_page_prot);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		page_add_new_anon_rmap(pages[i], vma, haddr);
		pte = pte_offset_map(&_pmd, haddr);
		VM_BUG_ON(!pte_none(*pte));
		set_pte_at(mm, haddr, pte, entry);
		pte_unmap(pte);
	}
	kfree(pages);

	mm->nr_ptes++;
	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(mm, pmd, pgtable);
	page_remove_rmap(page);
	spin_unlock(&mm->page_table_lock);

	ret |= VM_FAULT_WRITE;
	put_page(page);

out:
	return ret;

out_free_pages:
	spin_unlock(&mm->page_table_lock);
	mem_cgroup_uncharge_start();
	for (i = 0; i < HPAGE_PMD_NR; i++) {
		mem_cgroup_uncharge_page(pages[i]);
		put_page(pages[i]);
	}
	mem_cgroup_uncharge_end();
	kfree(pages);
	goto out;
}

int do_huge_pmd_wp_page(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pmd_t *pmd, pmd_t orig_pmd)
{
	int ret = 0;
	struct page *page, *new_page;
	unsigned long haddr;

	VM_BUG_ON(!vma->anon_vma);
	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_same(*pmd, orig_pmd)))
		goto out_unlock;

	page = pmd_page(orig_pmd);
	VM_BUG_ON(!PageCompound(page) || !PageHead(page));
	haddr = address & HPAGE_PMD_MASK;
	if (page_mapcount(page) == 1) {
		pmd_t entry;
		entry = pmd_mkyoung(orig_pmd);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		if (pmdp_set_access_flags(vma, haddr, pmd, entry,  1))
			update_mmu_cache(vma, address, entry);
		ret |= VM_FAULT_WRITE;
		goto out_unlock;
	}
	get_page(page);
	spin_unlock(&mm->page_table_lock);

	if (transparent_hugepage_enabled(vma) &&
	    !transparent_hugepage_debug_cow())
		new_page = alloc_hugepage(transparent_hugepage_defrag(vma));
	else
		new_page = NULL;

	if (unlikely(!new_page)) {
		ret = do_huge_pmd_wp_page_fallback(mm, vma, address,
						   pmd, orig_pmd, page, haddr);
		put_page(page);
		goto out;
	}

	if (unlikely(mem_cgroup_newpage_charge(new_page, mm, GFP_KERNEL))) {
		put_page(new_page);
		put_page(page);
		ret |= VM_FAULT_OOM;
		goto out;
	}

	copy_user_huge_page(new_page, page, haddr, vma, HPAGE_PMD_NR);
	__SetPageUptodate(new_page);

	spin_lock(&mm->page_table_lock);
	put_page(page);
	if (unlikely(!pmd_same(*pmd, orig_pmd))) {
		mem_cgroup_uncharge_page(new_page);
		put_page(new_page);
	} else {
		pmd_t entry;
		VM_BUG_ON(!PageHead(page));
		entry = mk_pmd(new_page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		entry = pmd_mkhuge(entry);
		pmdp_clear_flush_notify(vma, haddr, pmd);
		page_add_new_anon_rmap(new_page, vma, haddr);
		set_pmd_at(mm, haddr, pmd, entry);
		update_mmu_cache(vma, address, entry);
		page_remove_rmap(page);
		put_page(page);
		ret |= VM_FAULT_WRITE;
	}
out_unlock:
	spin_unlock(&mm->page_table_lock);
out:
	return ret;
}

struct page *follow_trans_huge_pmd(struct mm_struct *mm,
				   unsigned long addr,
				   pmd_t *pmd,
				   unsigned int flags)
{
	struct page *page = NULL;

	assert_spin_locked(&mm->page_table_lock);

	if (flags & FOLL_WRITE && !pmd_write(*pmd))
		goto out;

	page = pmd_page(*pmd);
	VM_BUG_ON(!PageHead(page));
	if (flags & FOLL_TOUCH) {
		pmd_t _pmd;
		/*
		 * We should set the dirty bit only for FOLL_WRITE but
		 * for now the dirty bit in the pmd is meaningless.
		 * And if the dirty bit will become meaningful and
		 * we'll only set it with FOLL_WRITE, an atomic
		 * set_bit will be required on the pmd to set the
		 * young bit, instead of the current set_pmd_at.
		 */
		_pmd = pmd_mkyoung(pmd_mkdirty(*pmd));
		set_pmd_at(mm, addr & HPAGE_PMD_MASK, pmd, _pmd);
	}
	page += (addr & ~HPAGE_PMD_MASK) >> PAGE_SHIFT;
	VM_BUG_ON(!PageCompound(page));
	if (flags & FOLL_GET)
		get_page(page);

out:
	return page;
}

int zap_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		 pmd_t *pmd)
{
	int ret = 0;

	spin_lock(&tlb->mm->page_table_lock);
	if (likely(pmd_trans_huge(*pmd))) {
		if (unlikely(pmd_trans_splitting(*pmd))) {
			spin_unlock(&tlb->mm->page_table_lock);
			wait_split_huge_page(vma->anon_vma,
					     pmd);
		} else {
			struct page *page;
			pgtable_t pgtable;
			pgtable = get_pmd_huge_pte(tlb->mm);
			page = pmd_page(*pmd);
			pmd_clear(pmd);
			page_remove_rmap(page);
			VM_BUG_ON(page_mapcount(page) < 0);
			add_mm_counter(tlb->mm, MM_ANONPAGES, -HPAGE_PMD_NR);
			VM_BUG_ON(!PageHead(page));
			spin_unlock(&tlb->mm->page_table_lock);
			tlb_remove_page(tlb, page);
			pte_free(tlb->mm, pgtable);
			ret = 1;
		}
	} else
		spin_unlock(&tlb->mm->page_table_lock);

	return ret;
}

pmd_t *page_check_address_pmd(struct page *page,
			      struct mm_struct *mm,
			      unsigned long address,
			      enum page_check_address_pmd_flag flag)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd, *ret = NULL;

	if (address & ~HPAGE_PMD_MASK)
		goto out;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out;

	pud = pud_offset(pgd, address);
	if (!pud_present(*pud))
		goto out;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd))
		goto out;
	if (pmd_page(*pmd) != page)
		goto out;
	VM_BUG_ON(flag == PAGE_CHECK_ADDRESS_PMD_NOTSPLITTING_FLAG &&
		  pmd_trans_splitting(*pmd));
	if (pmd_trans_huge(*pmd)) {
		VM_BUG_ON(flag == PAGE_CHECK_ADDRESS_PMD_SPLITTING_FLAG &&
			  !pmd_trans_splitting(*pmd));
		ret = pmd;
	}
out:
	return ret;
}

static int __split_huge_page_splitting(struct page *page,
				       struct vm_area_struct *vma,
				       unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	pmd_t *pmd;
	int ret = 0;

	spin_lock(&mm->page_table_lock);
	pmd = page_check_address_pmd(page, mm, address,
				     PAGE_CHECK_ADDRESS_PMD_NOTSPLITTING_FLAG);
	if (pmd) {
		/*
		 * We can't temporarily set the pmd to null in order
		 * to split it, the pmd must remain marked huge at all
		 * times or the VM won't take the pmd_trans_huge paths
		 * and it won't wait on the anon_vma->root->lock to
		 * serialize against split_huge_page*.
		 */
		pmdp_splitting_flush_notify(vma, address, pmd);
		ret = 1;
	}
	spin_unlock(&mm->page_table_lock);

	return ret;
}

static void __split_huge_page_refcount(struct page *page)
{
	int i;
	unsigned long head_index = page->index;
	struct zone *zone = page_zone(page);

	/* prevent PageLRU to go away from under us, and freeze lru stats */
	spin_lock_irq(&zone->lru_lock);
	compound_lock(page);

	for (i = 1; i < HPAGE_PMD_NR; i++) {
		struct page *page_tail = page + i;

		/* tail_page->_count cannot change */
		atomic_sub(atomic_read(&page_tail->_count), &page->_count);
		BUG_ON(page_count(page) <= 0);
		atomic_add(page_mapcount(page) + 1, &page_tail->_count);
		BUG_ON(atomic_read(&page_tail->_count) <= 0);

		/* after clearing PageTail the gup refcount can be released */
		smp_mb();

		page_tail->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
		page_tail->flags |= (page->flags &
				     ((1L << PG_referenced) |
				      (1L << PG_swapbacked) |
				      (1L << PG_mlocked) |
				      (1L << PG_uptodate)));
		page_tail->flags |= (1L << PG_dirty);

		/*
		 * 1) clear PageTail before overwriting first_page
		 * 2) clear PageTail before clearing PageHead for VM_BUG_ON
		 */
		smp_wmb();

		/*
		 * __split_huge_page_splitting() already set the
		 * splitting bit in all pmd that could map this
		 * hugepage, that will ensure no CPU can alter the
		 * mapcount on the head page. The mapcount is only
		 * accounted in the head page and it has to be
		 * transferred to all tail pages in the below code. So
		 * for this code to be safe, the split the mapcount
		 * can't change. But that doesn't mean userland can't
		 * keep changing and reading the page contents while
		 * we transfer the mapcount, so the pmd splitting
		 * status is achieved setting a reserved bit in the
		 * pmd, not by clearing the present bit.
		*/
		BUG_ON(page_mapcount(page_tail));
		page_tail->_mapcount = page->_mapcount;

		BUG_ON(page_tail->mapping);
		page_tail->mapping = page->mapping;

		page_tail->index = ++head_index;

		BUG_ON(!PageAnon(page_tail));
		BUG_ON(!PageUptodate(page_tail));
		BUG_ON(!PageDirty(page_tail));
		BUG_ON(!PageSwapBacked(page_tail));

		lru_add_page_tail(zone, page, page_tail);
	}

	ClearPageCompound(page);
	compound_unlock(page);
	spin_unlock_irq(&zone->lru_lock);

	for (i = 1; i < HPAGE_PMD_NR; i++) {
		struct page *page_tail = page + i;
		BUG_ON(page_count(page_tail) <= 0);
		/*
		 * Tail pages may be freed if there wasn't any mapping
		 * like if add_to_swap() is running on a lru page that
		 * had its mapping zapped. And freeing these pages
		 * requires taking the lru_lock so we do the put_page
		 * of the tail pages after the split is complete.
		 */
		put_page(page_tail);
	}

	/*
	 * Only the head page (now become a regular page) is required
	 * to be pinned by the caller.
	 */
	BUG_ON(page_count(page) <= 0);
}

static int __split_huge_page_map(struct page *page,
				 struct vm_area_struct *vma,
				 unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	pmd_t *pmd, _pmd;
	int ret = 0, i;
	pgtable_t pgtable;
	unsigned long haddr;

	spin_lock(&mm->page_table_lock);
	pmd = page_check_address_pmd(page, mm, address,
				     PAGE_CHECK_ADDRESS_PMD_SPLITTING_FLAG);
	if (pmd) {
		pgtable = get_pmd_huge_pte(mm);
		pmd_populate(mm, &_pmd, pgtable);

		for (i = 0, haddr = address; i < HPAGE_PMD_NR;
		     i++, haddr += PAGE_SIZE) {
			pte_t *pte, entry;
			BUG_ON(PageCompound(page+i));
			entry = mk_pte(page + i, vma->vm_page_prot);
			entry = maybe_mkwrite(pte_mkdirty(entry), vma);
			if (!pmd_write(*pmd))
				entry = pte_wrprotect(entry);
			else
				BUG_ON(page_mapcount(page) != 1);
			if (!pmd_young(*pmd))
				entry = pte_mkold(entry);
			pte = pte_offset_map(&_pmd, haddr);
			BUG_ON(!pte_none(*pte));
			set_pte_at(mm, haddr, pte, entry);
			pte_unmap(pte);
		}

		mm->nr_ptes++;
		smp_wmb(); /* make pte visible before pmd */
		/*
		 * Up to this point the pmd is present and huge and
		 * userland has the whole access to the hugepage
		 * during the split (which happens in place). If we
		 * overwrite the pmd with the not-huge version
		 * pointing to the pte here (which of course we could
		 * if all CPUs were bug free), userland could trigger
		 * a small page size TLB miss on the small sized TLB
		 * while the hugepage TLB entry is still established
		 * in the huge TLB. Some CPU doesn't like that. See
		 * http://support.amd.com/us/Processor_TechDocs/41322.pdf,
		 * Erratum 383 on page 93. Intel should be safe but is
		 * also warns that it's only safe if the permission
		 * and cache attributes of the two entries loaded in
		 * the two TLB is identical (which should be the case
		 * here). But it is generally safer to never allow
		 * small and huge TLB entries for the same virtual
		 * address to be loaded simultaneously. So instead of
		 * doing "pmd_populate(); flush_tlb_range();" we first
		 * mark the current pmd notpresent (atomically because
		 * here the pmd_trans_huge and pmd_trans_splitting
		 * must remain set at all times on the pmd until the
		 * split is complete for this pmd), then we flush the
		 * SMP TLB and finally we write the non-huge version
		 * of the pmd entry with pmd_populate.
		 */
		set_pmd_at(mm, address, pmd, pmd_mknotpresent(*pmd));
		flush_tlb_range(vma, address, address + HPAGE_PMD_SIZE);
		pmd_populate(mm, pmd, pgtable);
		ret = 1;
	}
	spin_unlock(&mm->page_table_lock);

	return ret;
}

/* must be called with anon_vma->root->lock hold */
static void __split_huge_page(struct page *page,
			      struct anon_vma *anon_vma)
{
	int mapcount, mapcount2;
	struct anon_vma_chain *avc;

	BUG_ON(!PageHead(page));
	BUG_ON(PageTail(page));

	mapcount = 0;
	list_for_each_entry(avc, &anon_vma->head, same_anon_vma) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long addr = vma_address(page, vma);
		BUG_ON(is_vma_temporary_stack(vma));
		if (addr == -EFAULT)
			continue;
		mapcount += __split_huge_page_splitting(page, vma, addr);
	}
	/*
	 * It is critical that new vmas are added to the tail of the
	 * anon_vma list. This guarantes that if copy_huge_pmd() runs
	 * and establishes a child pmd before
	 * __split_huge_page_splitting() freezes the parent pmd (so if
	 * we fail to prevent copy_huge_pmd() from running until the
	 * whole __split_huge_page() is complete), we will still see
	 * the newly established pmd of the child later during the
	 * walk, to be able to set it as pmd_trans_splitting too.
	 */
	if (mapcount != page_mapcount(page))
		printk(KERN_ERR "mapcount %d page_mapcount %d\n",
		       mapcount, page_mapcount(page));
	BUG_ON(mapcount != page_mapcount(page));

	__split_huge_page_refcount(page);

	mapcount2 = 0;
	list_for_each_entry(avc, &anon_vma->head, same_anon_vma) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long addr = vma_address(page, vma);
		BUG_ON(is_vma_temporary_stack(vma));
		if (addr == -EFAULT)
			continue;
		mapcount2 += __split_huge_page_map(page, vma, addr);
	}
	if (mapcount != mapcount2)
		printk(KERN_ERR "mapcount %d mapcount2 %d page_mapcount %d\n",
		       mapcount, mapcount2, page_mapcount(page));
	BUG_ON(mapcount != mapcount2);
}

int split_huge_page(struct page *page)
{
	struct anon_vma *anon_vma;
	int ret = 1;

	BUG_ON(!PageAnon(page));
	anon_vma = page_lock_anon_vma(page);
	if (!anon_vma)
		goto out;
	ret = 0;
	if (!PageCompound(page))
		goto out_unlock;

	BUG_ON(!PageSwapBacked(page));
	__split_huge_page(page, anon_vma);

	BUG_ON(PageCompound(page));
out_unlock:
	page_unlock_anon_vma(anon_vma);
out:
	return ret;
}

int hugepage_madvise(unsigned long *vm_flags)
{
	/*
	 * Be somewhat over-protective like KSM for now!
	 */
	if (*vm_flags & (VM_HUGEPAGE | VM_SHARED  | VM_MAYSHARE   |
			 VM_PFNMAP   | VM_IO      | VM_DONTEXPAND |
			 VM_RESERVED | VM_HUGETLB | VM_INSERTPAGE |
			 VM_MIXEDMAP | VM_SAO))
		return -EINVAL;

	*vm_flags |= VM_HUGEPAGE;

	return 0;
}

void __split_huge_page_pmd(struct mm_struct *mm, pmd_t *pmd)
{
	struct page *page;

	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_trans_huge(*pmd))) {
		spin_unlock(&mm->page_table_lock);
		return;
	}
	page = pmd_page(*pmd);
	VM_BUG_ON(!page_count(page));
	get_page(page);
	spin_unlock(&mm->page_table_lock);

	split_huge_page(page);

	put_page(page);
	BUG_ON(pmd_trans_huge(*pmd));
}
