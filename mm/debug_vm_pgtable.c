// SPDX-License-Identifier: GPL-2.0-only
/*
 * This kernel test validates architecture page table helpers and
 * accessors and helps in verifying their continued compliance with
 * expected generic MM semantics.
 *
 * Copyright (C) 2019 ARM Ltd.
 *
 * Author: Anshuman Khandual <anshuman.khandual@arm.com>
 */
#define pr_fmt(fmt) "debug_vm_pgtable: %s: " fmt, __func__

#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/kernel.h>
#include <linux/kconfig.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/pfn_t.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/start_kernel.h>
#include <linux/sched/mm.h>
#include <asm/pgalloc.h>

#define VMFLAGS	(VM_READ|VM_WRITE|VM_EXEC)

/*
 * On s390 platform, the lower 4 bits are used to identify given page table
 * entry type. But these bits might affect the ability to clear entries with
 * pxx_clear() because of how dynamic page table folding works on s390. So
 * while loading up the entries do not change the lower 4 bits. It does not
 * have affect any other platform.
 */
#define S390_MASK_BITS	4
#define RANDOM_ORVALUE	GENMASK(BITS_PER_LONG - 1, S390_MASK_BITS)
#define RANDOM_NZVALUE	GENMASK(7, 0)

static void __init pte_basic_tests(unsigned long pfn, pgprot_t prot)
{
	pte_t pte = pfn_pte(pfn, prot);

	WARN_ON(!pte_same(pte, pte));
	WARN_ON(!pte_young(pte_mkyoung(pte_mkold(pte))));
	WARN_ON(!pte_dirty(pte_mkdirty(pte_mkclean(pte))));
	WARN_ON(!pte_write(pte_mkwrite(pte_wrprotect(pte))));
	WARN_ON(pte_young(pte_mkold(pte_mkyoung(pte))));
	WARN_ON(pte_dirty(pte_mkclean(pte_mkdirty(pte))));
	WARN_ON(pte_write(pte_wrprotect(pte_mkwrite(pte))));
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void __init pmd_basic_tests(unsigned long pfn, pgprot_t prot)
{
	pmd_t pmd = pfn_pmd(pfn, prot);

	if (!has_transparent_hugepage())
		return;

	WARN_ON(!pmd_same(pmd, pmd));
	WARN_ON(!pmd_young(pmd_mkyoung(pmd_mkold(pmd))));
	WARN_ON(!pmd_dirty(pmd_mkdirty(pmd_mkclean(pmd))));
	WARN_ON(!pmd_write(pmd_mkwrite(pmd_wrprotect(pmd))));
	WARN_ON(pmd_young(pmd_mkold(pmd_mkyoung(pmd))));
	WARN_ON(pmd_dirty(pmd_mkclean(pmd_mkdirty(pmd))));
	WARN_ON(pmd_write(pmd_wrprotect(pmd_mkwrite(pmd))));
	/*
	 * A huge page does not point to next level page table
	 * entry. Hence this must qualify as pmd_bad().
	 */
	WARN_ON(!pmd_bad(pmd_mkhuge(pmd)));
}

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
static void __init pud_basic_tests(unsigned long pfn, pgprot_t prot)
{
	pud_t pud = pfn_pud(pfn, prot);

	if (!has_transparent_hugepage())
		return;

	WARN_ON(!pud_same(pud, pud));
	WARN_ON(!pud_young(pud_mkyoung(pud_mkold(pud))));
	WARN_ON(!pud_write(pud_mkwrite(pud_wrprotect(pud))));
	WARN_ON(pud_write(pud_wrprotect(pud_mkwrite(pud))));
	WARN_ON(pud_young(pud_mkold(pud_mkyoung(pud))));

	if (mm_pmd_folded(mm))
		return;

	/*
	 * A huge page does not point to next level page table
	 * entry. Hence this must qualify as pud_bad().
	 */
	WARN_ON(!pud_bad(pud_mkhuge(pud)));
}
#else  /* !CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */
static void __init pud_basic_tests(unsigned long pfn, pgprot_t prot) { }
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */
#else  /* !CONFIG_TRANSPARENT_HUGEPAGE */
static void __init pmd_basic_tests(unsigned long pfn, pgprot_t prot) { }
static void __init pud_basic_tests(unsigned long pfn, pgprot_t prot) { }
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static void __init p4d_basic_tests(unsigned long pfn, pgprot_t prot)
{
	p4d_t p4d;

	memset(&p4d, RANDOM_NZVALUE, sizeof(p4d_t));
	WARN_ON(!p4d_same(p4d, p4d));
}

static void __init pgd_basic_tests(unsigned long pfn, pgprot_t prot)
{
	pgd_t pgd;

	memset(&pgd, RANDOM_NZVALUE, sizeof(pgd_t));
	WARN_ON(!pgd_same(pgd, pgd));
}

#ifndef __PAGETABLE_PUD_FOLDED
static void __init pud_clear_tests(struct mm_struct *mm, pud_t *pudp)
{
	pud_t pud = READ_ONCE(*pudp);

	if (mm_pmd_folded(mm))
		return;

	pud = __pud(pud_val(pud) | RANDOM_ORVALUE);
	WRITE_ONCE(*pudp, pud);
	pud_clear(pudp);
	pud = READ_ONCE(*pudp);
	WARN_ON(!pud_none(pud));
}

static void __init pud_populate_tests(struct mm_struct *mm, pud_t *pudp,
				      pmd_t *pmdp)
{
	pud_t pud;

	if (mm_pmd_folded(mm))
		return;
	/*
	 * This entry points to next level page table page.
	 * Hence this must not qualify as pud_bad().
	 */
	pmd_clear(pmdp);
	pud_clear(pudp);
	pud_populate(mm, pudp, pmdp);
	pud = READ_ONCE(*pudp);
	WARN_ON(pud_bad(pud));
}
#else  /* !__PAGETABLE_PUD_FOLDED */
static void __init pud_clear_tests(struct mm_struct *mm, pud_t *pudp) { }
static void __init pud_populate_tests(struct mm_struct *mm, pud_t *pudp,
				      pmd_t *pmdp)
{
}
#endif /* PAGETABLE_PUD_FOLDED */

#ifndef __PAGETABLE_P4D_FOLDED
static void __init p4d_clear_tests(struct mm_struct *mm, p4d_t *p4dp)
{
	p4d_t p4d = READ_ONCE(*p4dp);

	if (mm_pud_folded(mm))
		return;

	p4d = __p4d(p4d_val(p4d) | RANDOM_ORVALUE);
	WRITE_ONCE(*p4dp, p4d);
	p4d_clear(p4dp);
	p4d = READ_ONCE(*p4dp);
	WARN_ON(!p4d_none(p4d));
}

static void __init p4d_populate_tests(struct mm_struct *mm, p4d_t *p4dp,
				      pud_t *pudp)
{
	p4d_t p4d;

	if (mm_pud_folded(mm))
		return;

	/*
	 * This entry points to next level page table page.
	 * Hence this must not qualify as p4d_bad().
	 */
	pud_clear(pudp);
	p4d_clear(p4dp);
	p4d_populate(mm, p4dp, pudp);
	p4d = READ_ONCE(*p4dp);
	WARN_ON(p4d_bad(p4d));
}

static void __init pgd_clear_tests(struct mm_struct *mm, pgd_t *pgdp)
{
	pgd_t pgd = READ_ONCE(*pgdp);

	if (mm_p4d_folded(mm))
		return;

	pgd = __pgd(pgd_val(pgd) | RANDOM_ORVALUE);
	WRITE_ONCE(*pgdp, pgd);
	pgd_clear(pgdp);
	pgd = READ_ONCE(*pgdp);
	WARN_ON(!pgd_none(pgd));
}

static void __init pgd_populate_tests(struct mm_struct *mm, pgd_t *pgdp,
				      p4d_t *p4dp)
{
	pgd_t pgd;

	if (mm_p4d_folded(mm))
		return;

	/*
	 * This entry points to next level page table page.
	 * Hence this must not qualify as pgd_bad().
	 */
	p4d_clear(p4dp);
	pgd_clear(pgdp);
	pgd_populate(mm, pgdp, p4dp);
	pgd = READ_ONCE(*pgdp);
	WARN_ON(pgd_bad(pgd));
}
#else  /* !__PAGETABLE_P4D_FOLDED */
static void __init p4d_clear_tests(struct mm_struct *mm, p4d_t *p4dp) { }
static void __init pgd_clear_tests(struct mm_struct *mm, pgd_t *pgdp) { }
static void __init p4d_populate_tests(struct mm_struct *mm, p4d_t *p4dp,
				      pud_t *pudp)
{
}
static void __init pgd_populate_tests(struct mm_struct *mm, pgd_t *pgdp,
				      p4d_t *p4dp)
{
}
#endif /* PAGETABLE_P4D_FOLDED */

static void __init pte_clear_tests(struct mm_struct *mm, pte_t *ptep,
				   unsigned long vaddr)
{
	pte_t pte = ptep_get(ptep);

	pte = __pte(pte_val(pte) | RANDOM_ORVALUE);
	set_pte_at(mm, vaddr, ptep, pte);
	barrier();
	pte_clear(mm, vaddr, ptep);
	pte = ptep_get(ptep);
	WARN_ON(!pte_none(pte));
}

static void __init pmd_clear_tests(struct mm_struct *mm, pmd_t *pmdp)
{
	pmd_t pmd = READ_ONCE(*pmdp);

	pmd = __pmd(pmd_val(pmd) | RANDOM_ORVALUE);
	WRITE_ONCE(*pmdp, pmd);
	pmd_clear(pmdp);
	pmd = READ_ONCE(*pmdp);
	WARN_ON(!pmd_none(pmd));
}

static void __init pmd_populate_tests(struct mm_struct *mm, pmd_t *pmdp,
				      pgtable_t pgtable)
{
	pmd_t pmd;

	/*
	 * This entry points to next level page table page.
	 * Hence this must not qualify as pmd_bad().
	 */
	pmd_clear(pmdp);
	pmd_populate(mm, pmdp, pgtable);
	pmd = READ_ONCE(*pmdp);
	WARN_ON(pmd_bad(pmd));
}

static unsigned long __init get_random_vaddr(void)
{
	unsigned long random_vaddr, random_pages, total_user_pages;

	total_user_pages = (TASK_SIZE - FIRST_USER_ADDRESS) / PAGE_SIZE;

	random_pages = get_random_long() % total_user_pages;
	random_vaddr = FIRST_USER_ADDRESS + random_pages * PAGE_SIZE;

	return random_vaddr;
}

static int __init debug_vm_pgtable(void)
{
	struct mm_struct *mm;
	pgd_t *pgdp;
	p4d_t *p4dp, *saved_p4dp;
	pud_t *pudp, *saved_pudp;
	pmd_t *pmdp, *saved_pmdp, pmd;
	pte_t *ptep;
	pgtable_t saved_ptep;
	pgprot_t prot;
	phys_addr_t paddr;
	unsigned long vaddr, pte_aligned, pmd_aligned;
	unsigned long pud_aligned, p4d_aligned, pgd_aligned;
	spinlock_t *uninitialized_var(ptl);

	pr_info("Validating architecture page table helpers\n");
	prot = vm_get_page_prot(VMFLAGS);
	vaddr = get_random_vaddr();
	mm = mm_alloc();
	if (!mm) {
		pr_err("mm_struct allocation failed\n");
		return 1;
	}

	/*
	 * PFN for mapping at PTE level is determined from a standard kernel
	 * text symbol. But pfns for higher page table levels are derived by
	 * masking lower bits of this real pfn. These derived pfns might not
	 * exist on the platform but that does not really matter as pfn_pxx()
	 * helpers will still create appropriate entries for the test. This
	 * helps avoid large memory block allocations to be used for mapping
	 * at higher page table levels.
	 */
	paddr = __pa_symbol(&start_kernel);

	pte_aligned = (paddr & PAGE_MASK) >> PAGE_SHIFT;
	pmd_aligned = (paddr & PMD_MASK) >> PAGE_SHIFT;
	pud_aligned = (paddr & PUD_MASK) >> PAGE_SHIFT;
	p4d_aligned = (paddr & P4D_MASK) >> PAGE_SHIFT;
	pgd_aligned = (paddr & PGDIR_MASK) >> PAGE_SHIFT;
	WARN_ON(!pfn_valid(pte_aligned));

	pgdp = pgd_offset(mm, vaddr);
	p4dp = p4d_alloc(mm, pgdp, vaddr);
	pudp = pud_alloc(mm, p4dp, vaddr);
	pmdp = pmd_alloc(mm, pudp, vaddr);
	ptep = pte_alloc_map_lock(mm, pmdp, vaddr, &ptl);

	/*
	 * Save all the page table page addresses as the page table
	 * entries will be used for testing with random or garbage
	 * values. These saved addresses will be used for freeing
	 * page table pages.
	 */
	pmd = READ_ONCE(*pmdp);
	saved_p4dp = p4d_offset(pgdp, 0UL);
	saved_pudp = pud_offset(p4dp, 0UL);
	saved_pmdp = pmd_offset(pudp, 0UL);
	saved_ptep = pmd_pgtable(pmd);

	pte_basic_tests(pte_aligned, prot);
	pmd_basic_tests(pmd_aligned, prot);
	pud_basic_tests(pud_aligned, prot);
	p4d_basic_tests(p4d_aligned, prot);
	pgd_basic_tests(pgd_aligned, prot);

	pte_clear_tests(mm, ptep, vaddr);
	pmd_clear_tests(mm, pmdp);
	pud_clear_tests(mm, pudp);
	p4d_clear_tests(mm, p4dp);
	pgd_clear_tests(mm, pgdp);

	pte_unmap_unlock(ptep, ptl);

	pmd_populate_tests(mm, pmdp, saved_ptep);
	pud_populate_tests(mm, pudp, saved_pmdp);
	p4d_populate_tests(mm, p4dp, saved_pudp);
	pgd_populate_tests(mm, pgdp, saved_p4dp);

	p4d_free(mm, saved_p4dp);
	pud_free(mm, saved_pudp);
	pmd_free(mm, saved_pmdp);
	pte_free(mm, saved_ptep);

	mm_dec_nr_puds(mm);
	mm_dec_nr_pmds(mm);
	mm_dec_nr_ptes(mm);
	mmdrop(mm);
	return 0;
}
late_initcall(debug_vm_pgtable);
