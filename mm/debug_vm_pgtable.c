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
#define pr_fmt(fmt) "debug_vm_pgtable: [%-25s]: " fmt, __func__

#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/kernel.h>
#include <linux/kconfig.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/pfn_t.h>
#include <linux/printk.h>
#include <linux/pgtable.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/start_kernel.h>
#include <linux/sched/mm.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

/*
 * Please refer Documentation/mm/arch_pgtable_helpers.rst for the semantics
 * expectations that are being validated here. All future changes in here
 * or the documentation need to be in sync.
 *
 * On s390 platform, the lower 4 bits are used to identify given page table
 * entry type. But these bits might affect the ability to clear entries with
 * pxx_clear() because of how dynamic page table folding works on s390. So
 * while loading up the entries do not change the lower 4 bits. It does not
 * have affect any other platform. Also avoid the 62nd bit on ppc64 that is
 * used to mark a pte entry.
 */
#define S390_SKIP_MASK		GENMASK(3, 0)
#if __BITS_PER_LONG == 64
#define PPC64_SKIP_MASK		GENMASK(62, 62)
#else
#define PPC64_SKIP_MASK		0x0
#endif
#define ARCH_SKIP_MASK (S390_SKIP_MASK | PPC64_SKIP_MASK)
#define RANDOM_ORVALUE (GENMASK(BITS_PER_LONG - 1, 0) & ~ARCH_SKIP_MASK)
#define RANDOM_NZVALUE	GENMASK(7, 0)

struct pgtable_debug_args {
	struct mm_struct	*mm;
	struct vm_area_struct	*vma;

	pgd_t			*pgdp;
	p4d_t			*p4dp;
	pud_t			*pudp;
	pmd_t			*pmdp;
	pte_t			*ptep;

	p4d_t			*start_p4dp;
	pud_t			*start_pudp;
	pmd_t			*start_pmdp;
	pgtable_t		start_ptep;

	unsigned long		vaddr;
	pgprot_t		page_prot;
	pgprot_t		page_prot_none;

	bool			is_contiguous_page;
	unsigned long		pud_pfn;
	unsigned long		pmd_pfn;
	unsigned long		pte_pfn;

	unsigned long		fixed_alignment;
	unsigned long		fixed_pgd_pfn;
	unsigned long		fixed_p4d_pfn;
	unsigned long		fixed_pud_pfn;
	unsigned long		fixed_pmd_pfn;
	unsigned long		fixed_pte_pfn;
};

static void __init pte_basic_tests(struct pgtable_debug_args *args, int idx)
{
	pgprot_t prot = vm_get_page_prot(idx);
	pte_t pte = pfn_pte(args->fixed_pte_pfn, prot);
	unsigned long val = idx, *ptr = &val;

	pr_debug("Validating PTE basic (%pGv)\n", ptr);

	/*
	 * This test needs to be executed after the given page table entry
	 * is created with pfn_pte() to make sure that vm_get_page_prot(idx)
	 * does not have the dirty bit enabled from the beginning. This is
	 * important for platforms like arm64 where (!PTE_RDONLY) indicate
	 * dirty bit being set.
	 */
	WARN_ON(pte_dirty(pte_wrprotect(pte)));

	WARN_ON(!pte_same(pte, pte));
	WARN_ON(!pte_young(pte_mkyoung(pte_mkold(pte))));
	WARN_ON(!pte_dirty(pte_mkdirty(pte_mkclean(pte))));
	WARN_ON(!pte_write(pte_mkwrite(pte_wrprotect(pte), args->vma)));
	WARN_ON(pte_young(pte_mkold(pte_mkyoung(pte))));
	WARN_ON(pte_dirty(pte_mkclean(pte_mkdirty(pte))));
	WARN_ON(pte_write(pte_wrprotect(pte_mkwrite(pte, args->vma))));
	WARN_ON(pte_dirty(pte_wrprotect(pte_mkclean(pte))));
	WARN_ON(!pte_dirty(pte_wrprotect(pte_mkdirty(pte))));
}

static void __init pte_advanced_tests(struct pgtable_debug_args *args)
{
	struct page *page;
	pte_t pte;

	/*
	 * Architectures optimize set_pte_at by avoiding TLB flush.
	 * This requires set_pte_at to be not used to update an
	 * existing pte entry. Clear pte before we do set_pte_at
	 *
	 * flush_dcache_page() is called after set_pte_at() to clear
	 * PG_arch_1 for the page on ARM64. The page flag isn't cleared
	 * when it's released and page allocation check will fail when
	 * the page is allocated again. For architectures other than ARM64,
	 * the unexpected overhead of cache flushing is acceptable.
	 */
	page = (args->pte_pfn != ULONG_MAX) ? pfn_to_page(args->pte_pfn) : NULL;
	if (!page)
		return;

	pr_debug("Validating PTE advanced\n");
	if (WARN_ON(!args->ptep))
		return;

	pte = pfn_pte(args->pte_pfn, args->page_prot);
	set_pte_at(args->mm, args->vaddr, args->ptep, pte);
	flush_dcache_page(page);
	ptep_set_wrprotect(args->mm, args->vaddr, args->ptep);
	pte = ptep_get(args->ptep);
	WARN_ON(pte_write(pte));
	ptep_get_and_clear(args->mm, args->vaddr, args->ptep);
	pte = ptep_get(args->ptep);
	WARN_ON(!pte_none(pte));

	pte = pfn_pte(args->pte_pfn, args->page_prot);
	pte = pte_wrprotect(pte);
	pte = pte_mkclean(pte);
	set_pte_at(args->mm, args->vaddr, args->ptep, pte);
	flush_dcache_page(page);
	pte = pte_mkwrite(pte, args->vma);
	pte = pte_mkdirty(pte);
	ptep_set_access_flags(args->vma, args->vaddr, args->ptep, pte, 1);
	pte = ptep_get(args->ptep);
	WARN_ON(!(pte_write(pte) && pte_dirty(pte)));
	ptep_get_and_clear_full(args->mm, args->vaddr, args->ptep, 1);
	pte = ptep_get(args->ptep);
	WARN_ON(!pte_none(pte));

	pte = pfn_pte(args->pte_pfn, args->page_prot);
	pte = pte_mkyoung(pte);
	set_pte_at(args->mm, args->vaddr, args->ptep, pte);
	flush_dcache_page(page);
	ptep_test_and_clear_young(args->vma, args->vaddr, args->ptep);
	pte = ptep_get(args->ptep);
	WARN_ON(pte_young(pte));

	ptep_get_and_clear_full(args->mm, args->vaddr, args->ptep, 1);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void __init pmd_basic_tests(struct pgtable_debug_args *args, int idx)
{
	pgprot_t prot = vm_get_page_prot(idx);
	unsigned long val = idx, *ptr = &val;
	pmd_t pmd;

	if (!has_transparent_hugepage())
		return;

	pr_debug("Validating PMD basic (%pGv)\n", ptr);
	pmd = pfn_pmd(args->fixed_pmd_pfn, prot);

	/*
	 * This test needs to be executed after the given page table entry
	 * is created with pfn_pmd() to make sure that vm_get_page_prot(idx)
	 * does not have the dirty bit enabled from the beginning. This is
	 * important for platforms like arm64 where (!PTE_RDONLY) indicate
	 * dirty bit being set.
	 */
	WARN_ON(pmd_dirty(pmd_wrprotect(pmd)));


	WARN_ON(!pmd_same(pmd, pmd));
	WARN_ON(!pmd_young(pmd_mkyoung(pmd_mkold(pmd))));
	WARN_ON(!pmd_dirty(pmd_mkdirty(pmd_mkclean(pmd))));
	WARN_ON(!pmd_write(pmd_mkwrite(pmd_wrprotect(pmd), args->vma)));
	WARN_ON(pmd_young(pmd_mkold(pmd_mkyoung(pmd))));
	WARN_ON(pmd_dirty(pmd_mkclean(pmd_mkdirty(pmd))));
	WARN_ON(pmd_write(pmd_wrprotect(pmd_mkwrite(pmd, args->vma))));
	WARN_ON(pmd_dirty(pmd_wrprotect(pmd_mkclean(pmd))));
	WARN_ON(!pmd_dirty(pmd_wrprotect(pmd_mkdirty(pmd))));
	/*
	 * A huge page does not point to next level page table
	 * entry. Hence this must qualify as pmd_bad().
	 */
	WARN_ON(!pmd_bad(pmd_mkhuge(pmd)));
}

static void __init pmd_advanced_tests(struct pgtable_debug_args *args)
{
	struct page *page;
	pmd_t pmd;
	unsigned long vaddr = args->vaddr;

	if (!has_transparent_hugepage())
		return;

	page = (args->pmd_pfn != ULONG_MAX) ? pfn_to_page(args->pmd_pfn) : NULL;
	if (!page)
		return;

	/*
	 * flush_dcache_page() is called after set_pmd_at() to clear
	 * PG_arch_1 for the page on ARM64. The page flag isn't cleared
	 * when it's released and page allocation check will fail when
	 * the page is allocated again. For architectures other than ARM64,
	 * the unexpected overhead of cache flushing is acceptable.
	 */
	pr_debug("Validating PMD advanced\n");
	/* Align the address wrt HPAGE_PMD_SIZE */
	vaddr &= HPAGE_PMD_MASK;

	pgtable_trans_huge_deposit(args->mm, args->pmdp, args->start_ptep);

	pmd = pfn_pmd(args->pmd_pfn, args->page_prot);
	set_pmd_at(args->mm, vaddr, args->pmdp, pmd);
	flush_dcache_page(page);
	pmdp_set_wrprotect(args->mm, vaddr, args->pmdp);
	pmd = READ_ONCE(*args->pmdp);
	WARN_ON(pmd_write(pmd));
	pmdp_huge_get_and_clear(args->mm, vaddr, args->pmdp);
	pmd = READ_ONCE(*args->pmdp);
	WARN_ON(!pmd_none(pmd));

	pmd = pfn_pmd(args->pmd_pfn, args->page_prot);
	pmd = pmd_wrprotect(pmd);
	pmd = pmd_mkclean(pmd);
	set_pmd_at(args->mm, vaddr, args->pmdp, pmd);
	flush_dcache_page(page);
	pmd = pmd_mkwrite(pmd, args->vma);
	pmd = pmd_mkdirty(pmd);
	pmdp_set_access_flags(args->vma, vaddr, args->pmdp, pmd, 1);
	pmd = READ_ONCE(*args->pmdp);
	WARN_ON(!(pmd_write(pmd) && pmd_dirty(pmd)));
	pmdp_huge_get_and_clear_full(args->vma, vaddr, args->pmdp, 1);
	pmd = READ_ONCE(*args->pmdp);
	WARN_ON(!pmd_none(pmd));

	pmd = pmd_mkhuge(pfn_pmd(args->pmd_pfn, args->page_prot));
	pmd = pmd_mkyoung(pmd);
	set_pmd_at(args->mm, vaddr, args->pmdp, pmd);
	flush_dcache_page(page);
	pmdp_test_and_clear_young(args->vma, vaddr, args->pmdp);
	pmd = READ_ONCE(*args->pmdp);
	WARN_ON(pmd_young(pmd));

	/*  Clear the pte entries  */
	pmdp_huge_get_and_clear(args->mm, vaddr, args->pmdp);
	pgtable_trans_huge_withdraw(args->mm, args->pmdp);
}

static void __init pmd_leaf_tests(struct pgtable_debug_args *args)
{
	pmd_t pmd;

	if (!has_transparent_hugepage())
		return;

	pr_debug("Validating PMD leaf\n");
	pmd = pfn_pmd(args->fixed_pmd_pfn, args->page_prot);

	/*
	 * PMD based THP is a leaf entry.
	 */
	pmd = pmd_mkhuge(pmd);
	WARN_ON(!pmd_leaf(pmd));
}

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
static void __init pud_basic_tests(struct pgtable_debug_args *args, int idx)
{
	pgprot_t prot = vm_get_page_prot(idx);
	unsigned long val = idx, *ptr = &val;
	pud_t pud;

	if (!has_transparent_pud_hugepage())
		return;

	pr_debug("Validating PUD basic (%pGv)\n", ptr);
	pud = pfn_pud(args->fixed_pud_pfn, prot);

	/*
	 * This test needs to be executed after the given page table entry
	 * is created with pfn_pud() to make sure that vm_get_page_prot(idx)
	 * does not have the dirty bit enabled from the beginning. This is
	 * important for platforms like arm64 where (!PTE_RDONLY) indicate
	 * dirty bit being set.
	 */
	WARN_ON(pud_dirty(pud_wrprotect(pud)));

	WARN_ON(!pud_same(pud, pud));
	WARN_ON(!pud_young(pud_mkyoung(pud_mkold(pud))));
	WARN_ON(!pud_dirty(pud_mkdirty(pud_mkclean(pud))));
	WARN_ON(pud_dirty(pud_mkclean(pud_mkdirty(pud))));
	WARN_ON(!pud_write(pud_mkwrite(pud_wrprotect(pud))));
	WARN_ON(pud_write(pud_wrprotect(pud_mkwrite(pud))));
	WARN_ON(pud_young(pud_mkold(pud_mkyoung(pud))));
	WARN_ON(pud_dirty(pud_wrprotect(pud_mkclean(pud))));
	WARN_ON(!pud_dirty(pud_wrprotect(pud_mkdirty(pud))));

	if (mm_pmd_folded(args->mm))
		return;

	/*
	 * A huge page does not point to next level page table
	 * entry. Hence this must qualify as pud_bad().
	 */
	WARN_ON(!pud_bad(pud_mkhuge(pud)));
}

static void __init pud_advanced_tests(struct pgtable_debug_args *args)
{
	struct page *page;
	unsigned long vaddr = args->vaddr;
	pud_t pud;

	if (!has_transparent_pud_hugepage())
		return;

	page = (args->pud_pfn != ULONG_MAX) ? pfn_to_page(args->pud_pfn) : NULL;
	if (!page)
		return;

	/*
	 * flush_dcache_page() is called after set_pud_at() to clear
	 * PG_arch_1 for the page on ARM64. The page flag isn't cleared
	 * when it's released and page allocation check will fail when
	 * the page is allocated again. For architectures other than ARM64,
	 * the unexpected overhead of cache flushing is acceptable.
	 */
	pr_debug("Validating PUD advanced\n");
	/* Align the address wrt HPAGE_PUD_SIZE */
	vaddr &= HPAGE_PUD_MASK;

	pud = pfn_pud(args->pud_pfn, args->page_prot);
	set_pud_at(args->mm, vaddr, args->pudp, pud);
	flush_dcache_page(page);
	pudp_set_wrprotect(args->mm, vaddr, args->pudp);
	pud = READ_ONCE(*args->pudp);
	WARN_ON(pud_write(pud));

#ifndef __PAGETABLE_PMD_FOLDED
	pudp_huge_get_and_clear(args->mm, vaddr, args->pudp);
	pud = READ_ONCE(*args->pudp);
	WARN_ON(!pud_none(pud));
#endif /* __PAGETABLE_PMD_FOLDED */
	pud = pfn_pud(args->pud_pfn, args->page_prot);
	pud = pud_wrprotect(pud);
	pud = pud_mkclean(pud);
	set_pud_at(args->mm, vaddr, args->pudp, pud);
	flush_dcache_page(page);
	pud = pud_mkwrite(pud);
	pud = pud_mkdirty(pud);
	pudp_set_access_flags(args->vma, vaddr, args->pudp, pud, 1);
	pud = READ_ONCE(*args->pudp);
	WARN_ON(!(pud_write(pud) && pud_dirty(pud)));

#ifndef __PAGETABLE_PMD_FOLDED
	pudp_huge_get_and_clear_full(args->vma, vaddr, args->pudp, 1);
	pud = READ_ONCE(*args->pudp);
	WARN_ON(!pud_none(pud));
#endif /* __PAGETABLE_PMD_FOLDED */

	pud = pfn_pud(args->pud_pfn, args->page_prot);
	pud = pud_mkyoung(pud);
	set_pud_at(args->mm, vaddr, args->pudp, pud);
	flush_dcache_page(page);
	pudp_test_and_clear_young(args->vma, vaddr, args->pudp);
	pud = READ_ONCE(*args->pudp);
	WARN_ON(pud_young(pud));

	pudp_huge_get_and_clear(args->mm, vaddr, args->pudp);
}

static void __init pud_leaf_tests(struct pgtable_debug_args *args)
{
	pud_t pud;

	if (!has_transparent_pud_hugepage())
		return;

	pr_debug("Validating PUD leaf\n");
	pud = pfn_pud(args->fixed_pud_pfn, args->page_prot);
	/*
	 * PUD based THP is a leaf entry.
	 */
	pud = pud_mkhuge(pud);
	WARN_ON(!pud_leaf(pud));
}
#else  /* !CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */
static void __init pud_basic_tests(struct pgtable_debug_args *args, int idx) { }
static void __init pud_advanced_tests(struct pgtable_debug_args *args) { }
static void __init pud_leaf_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */
#else  /* !CONFIG_TRANSPARENT_HUGEPAGE */
static void __init pmd_basic_tests(struct pgtable_debug_args *args, int idx) { }
static void __init pud_basic_tests(struct pgtable_debug_args *args, int idx) { }
static void __init pmd_advanced_tests(struct pgtable_debug_args *args) { }
static void __init pud_advanced_tests(struct pgtable_debug_args *args) { }
static void __init pmd_leaf_tests(struct pgtable_debug_args *args) { }
static void __init pud_leaf_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP
static void __init pmd_huge_tests(struct pgtable_debug_args *args)
{
	pmd_t pmd;

	if (!arch_vmap_pmd_supported(args->page_prot) ||
	    args->fixed_alignment < PMD_SIZE)
		return;

	pr_debug("Validating PMD huge\n");
	/*
	 * X86 defined pmd_set_huge() verifies that the given
	 * PMD is not a populated non-leaf entry.
	 */
	WRITE_ONCE(*args->pmdp, __pmd(0));
	WARN_ON(!pmd_set_huge(args->pmdp, __pfn_to_phys(args->fixed_pmd_pfn), args->page_prot));
	WARN_ON(!pmd_clear_huge(args->pmdp));
	pmd = READ_ONCE(*args->pmdp);
	WARN_ON(!pmd_none(pmd));
}

static void __init pud_huge_tests(struct pgtable_debug_args *args)
{
	pud_t pud;

	if (!arch_vmap_pud_supported(args->page_prot) ||
	    args->fixed_alignment < PUD_SIZE)
		return;

	pr_debug("Validating PUD huge\n");
	/*
	 * X86 defined pud_set_huge() verifies that the given
	 * PUD is not a populated non-leaf entry.
	 */
	WRITE_ONCE(*args->pudp, __pud(0));
	WARN_ON(!pud_set_huge(args->pudp, __pfn_to_phys(args->fixed_pud_pfn), args->page_prot));
	WARN_ON(!pud_clear_huge(args->pudp));
	pud = READ_ONCE(*args->pudp);
	WARN_ON(!pud_none(pud));
}
#else /* !CONFIG_HAVE_ARCH_HUGE_VMAP */
static void __init pmd_huge_tests(struct pgtable_debug_args *args) { }
static void __init pud_huge_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_HAVE_ARCH_HUGE_VMAP */

static void __init p4d_basic_tests(struct pgtable_debug_args *args)
{
	p4d_t p4d;

	pr_debug("Validating P4D basic\n");
	memset(&p4d, RANDOM_NZVALUE, sizeof(p4d_t));
	WARN_ON(!p4d_same(p4d, p4d));
}

static void __init pgd_basic_tests(struct pgtable_debug_args *args)
{
	pgd_t pgd;

	pr_debug("Validating PGD basic\n");
	memset(&pgd, RANDOM_NZVALUE, sizeof(pgd_t));
	WARN_ON(!pgd_same(pgd, pgd));
}

#ifndef __PAGETABLE_PUD_FOLDED
static void __init pud_clear_tests(struct pgtable_debug_args *args)
{
	pud_t pud = READ_ONCE(*args->pudp);

	if (mm_pmd_folded(args->mm))
		return;

	pr_debug("Validating PUD clear\n");
	pud = __pud(pud_val(pud) | RANDOM_ORVALUE);
	WRITE_ONCE(*args->pudp, pud);
	pud_clear(args->pudp);
	pud = READ_ONCE(*args->pudp);
	WARN_ON(!pud_none(pud));
}

static void __init pud_populate_tests(struct pgtable_debug_args *args)
{
	pud_t pud;

	if (mm_pmd_folded(args->mm))
		return;

	pr_debug("Validating PUD populate\n");
	/*
	 * This entry points to next level page table page.
	 * Hence this must not qualify as pud_bad().
	 */
	pud_populate(args->mm, args->pudp, args->start_pmdp);
	pud = READ_ONCE(*args->pudp);
	WARN_ON(pud_bad(pud));
}
#else  /* !__PAGETABLE_PUD_FOLDED */
static void __init pud_clear_tests(struct pgtable_debug_args *args) { }
static void __init pud_populate_tests(struct pgtable_debug_args *args) { }
#endif /* PAGETABLE_PUD_FOLDED */

#ifndef __PAGETABLE_P4D_FOLDED
static void __init p4d_clear_tests(struct pgtable_debug_args *args)
{
	p4d_t p4d = READ_ONCE(*args->p4dp);

	if (mm_pud_folded(args->mm))
		return;

	pr_debug("Validating P4D clear\n");
	p4d = __p4d(p4d_val(p4d) | RANDOM_ORVALUE);
	WRITE_ONCE(*args->p4dp, p4d);
	p4d_clear(args->p4dp);
	p4d = READ_ONCE(*args->p4dp);
	WARN_ON(!p4d_none(p4d));
}

static void __init p4d_populate_tests(struct pgtable_debug_args *args)
{
	p4d_t p4d;

	if (mm_pud_folded(args->mm))
		return;

	pr_debug("Validating P4D populate\n");
	/*
	 * This entry points to next level page table page.
	 * Hence this must not qualify as p4d_bad().
	 */
	pud_clear(args->pudp);
	p4d_clear(args->p4dp);
	p4d_populate(args->mm, args->p4dp, args->start_pudp);
	p4d = READ_ONCE(*args->p4dp);
	WARN_ON(p4d_bad(p4d));
}

static void __init pgd_clear_tests(struct pgtable_debug_args *args)
{
	pgd_t pgd = READ_ONCE(*(args->pgdp));

	if (mm_p4d_folded(args->mm))
		return;

	pr_debug("Validating PGD clear\n");
	pgd = __pgd(pgd_val(pgd) | RANDOM_ORVALUE);
	WRITE_ONCE(*args->pgdp, pgd);
	pgd_clear(args->pgdp);
	pgd = READ_ONCE(*args->pgdp);
	WARN_ON(!pgd_none(pgd));
}

static void __init pgd_populate_tests(struct pgtable_debug_args *args)
{
	pgd_t pgd;

	if (mm_p4d_folded(args->mm))
		return;

	pr_debug("Validating PGD populate\n");
	/*
	 * This entry points to next level page table page.
	 * Hence this must not qualify as pgd_bad().
	 */
	p4d_clear(args->p4dp);
	pgd_clear(args->pgdp);
	pgd_populate(args->mm, args->pgdp, args->start_p4dp);
	pgd = READ_ONCE(*args->pgdp);
	WARN_ON(pgd_bad(pgd));
}
#else  /* !__PAGETABLE_P4D_FOLDED */
static void __init p4d_clear_tests(struct pgtable_debug_args *args) { }
static void __init pgd_clear_tests(struct pgtable_debug_args *args) { }
static void __init p4d_populate_tests(struct pgtable_debug_args *args) { }
static void __init pgd_populate_tests(struct pgtable_debug_args *args) { }
#endif /* PAGETABLE_P4D_FOLDED */

static void __init pte_clear_tests(struct pgtable_debug_args *args)
{
	struct page *page;
	pte_t pte = pfn_pte(args->pte_pfn, args->page_prot);

	page = (args->pte_pfn != ULONG_MAX) ? pfn_to_page(args->pte_pfn) : NULL;
	if (!page)
		return;

	/*
	 * flush_dcache_page() is called after set_pte_at() to clear
	 * PG_arch_1 for the page on ARM64. The page flag isn't cleared
	 * when it's released and page allocation check will fail when
	 * the page is allocated again. For architectures other than ARM64,
	 * the unexpected overhead of cache flushing is acceptable.
	 */
	pr_debug("Validating PTE clear\n");
	if (WARN_ON(!args->ptep))
		return;

#ifndef CONFIG_RISCV
	pte = __pte(pte_val(pte) | RANDOM_ORVALUE);
#endif
	set_pte_at(args->mm, args->vaddr, args->ptep, pte);
	flush_dcache_page(page);
	barrier();
	ptep_clear(args->mm, args->vaddr, args->ptep);
	pte = ptep_get(args->ptep);
	WARN_ON(!pte_none(pte));
}

static void __init pmd_clear_tests(struct pgtable_debug_args *args)
{
	pmd_t pmd = READ_ONCE(*args->pmdp);

	pr_debug("Validating PMD clear\n");
	pmd = __pmd(pmd_val(pmd) | RANDOM_ORVALUE);
	WRITE_ONCE(*args->pmdp, pmd);
	pmd_clear(args->pmdp);
	pmd = READ_ONCE(*args->pmdp);
	WARN_ON(!pmd_none(pmd));
}

static void __init pmd_populate_tests(struct pgtable_debug_args *args)
{
	pmd_t pmd;

	pr_debug("Validating PMD populate\n");
	/*
	 * This entry points to next level page table page.
	 * Hence this must not qualify as pmd_bad().
	 */
	pmd_populate(args->mm, args->pmdp, args->start_ptep);
	pmd = READ_ONCE(*args->pmdp);
	WARN_ON(pmd_bad(pmd));
}

static void __init pte_special_tests(struct pgtable_debug_args *args)
{
	pte_t pte = pfn_pte(args->fixed_pte_pfn, args->page_prot);

	if (!IS_ENABLED(CONFIG_ARCH_HAS_PTE_SPECIAL))
		return;

	pr_debug("Validating PTE special\n");
	WARN_ON(!pte_special(pte_mkspecial(pte)));
}

static void __init pte_protnone_tests(struct pgtable_debug_args *args)
{
	pte_t pte = pfn_pte(args->fixed_pte_pfn, args->page_prot_none);

	if (!IS_ENABLED(CONFIG_NUMA_BALANCING))
		return;

	pr_debug("Validating PTE protnone\n");
	WARN_ON(!pte_protnone(pte));
	WARN_ON(!pte_present(pte));
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void __init pmd_protnone_tests(struct pgtable_debug_args *args)
{
	pmd_t pmd;

	if (!IS_ENABLED(CONFIG_NUMA_BALANCING))
		return;

	if (!has_transparent_hugepage())
		return;

	pr_debug("Validating PMD protnone\n");
	pmd = pmd_mkhuge(pfn_pmd(args->fixed_pmd_pfn, args->page_prot_none));
	WARN_ON(!pmd_protnone(pmd));
	WARN_ON(!pmd_present(pmd));
}
#else  /* !CONFIG_TRANSPARENT_HUGEPAGE */
static void __init pmd_protnone_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#ifdef CONFIG_ARCH_HAS_PTE_DEVMAP
static void __init pte_devmap_tests(struct pgtable_debug_args *args)
{
	pte_t pte = pfn_pte(args->fixed_pte_pfn, args->page_prot);

	pr_debug("Validating PTE devmap\n");
	WARN_ON(!pte_devmap(pte_mkdevmap(pte)));
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void __init pmd_devmap_tests(struct pgtable_debug_args *args)
{
	pmd_t pmd;

	if (!has_transparent_hugepage())
		return;

	pr_debug("Validating PMD devmap\n");
	pmd = pfn_pmd(args->fixed_pmd_pfn, args->page_prot);
	WARN_ON(!pmd_devmap(pmd_mkdevmap(pmd)));
}

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
static void __init pud_devmap_tests(struct pgtable_debug_args *args)
{
	pud_t pud;

	if (!has_transparent_pud_hugepage())
		return;

	pr_debug("Validating PUD devmap\n");
	pud = pfn_pud(args->fixed_pud_pfn, args->page_prot);
	WARN_ON(!pud_devmap(pud_mkdevmap(pud)));
}
#else  /* !CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */
static void __init pud_devmap_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */
#else  /* CONFIG_TRANSPARENT_HUGEPAGE */
static void __init pmd_devmap_tests(struct pgtable_debug_args *args) { }
static void __init pud_devmap_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
#else
static void __init pte_devmap_tests(struct pgtable_debug_args *args) { }
static void __init pmd_devmap_tests(struct pgtable_debug_args *args) { }
static void __init pud_devmap_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_ARCH_HAS_PTE_DEVMAP */

static void __init pte_soft_dirty_tests(struct pgtable_debug_args *args)
{
	pte_t pte = pfn_pte(args->fixed_pte_pfn, args->page_prot);

	if (!IS_ENABLED(CONFIG_MEM_SOFT_DIRTY))
		return;

	pr_debug("Validating PTE soft dirty\n");
	WARN_ON(!pte_soft_dirty(pte_mksoft_dirty(pte)));
	WARN_ON(pte_soft_dirty(pte_clear_soft_dirty(pte)));
}

static void __init pte_swap_soft_dirty_tests(struct pgtable_debug_args *args)
{
	pte_t pte = pfn_pte(args->fixed_pte_pfn, args->page_prot);

	if (!IS_ENABLED(CONFIG_MEM_SOFT_DIRTY))
		return;

	pr_debug("Validating PTE swap soft dirty\n");
	WARN_ON(!pte_swp_soft_dirty(pte_swp_mksoft_dirty(pte)));
	WARN_ON(pte_swp_soft_dirty(pte_swp_clear_soft_dirty(pte)));
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void __init pmd_soft_dirty_tests(struct pgtable_debug_args *args)
{
	pmd_t pmd;

	if (!IS_ENABLED(CONFIG_MEM_SOFT_DIRTY))
		return;

	if (!has_transparent_hugepage())
		return;

	pr_debug("Validating PMD soft dirty\n");
	pmd = pfn_pmd(args->fixed_pmd_pfn, args->page_prot);
	WARN_ON(!pmd_soft_dirty(pmd_mksoft_dirty(pmd)));
	WARN_ON(pmd_soft_dirty(pmd_clear_soft_dirty(pmd)));
}

static void __init pmd_swap_soft_dirty_tests(struct pgtable_debug_args *args)
{
	pmd_t pmd;

	if (!IS_ENABLED(CONFIG_MEM_SOFT_DIRTY) ||
		!IS_ENABLED(CONFIG_ARCH_ENABLE_THP_MIGRATION))
		return;

	if (!has_transparent_hugepage())
		return;

	pr_debug("Validating PMD swap soft dirty\n");
	pmd = pfn_pmd(args->fixed_pmd_pfn, args->page_prot);
	WARN_ON(!pmd_swp_soft_dirty(pmd_swp_mksoft_dirty(pmd)));
	WARN_ON(pmd_swp_soft_dirty(pmd_swp_clear_soft_dirty(pmd)));
}
#else  /* !CONFIG_TRANSPARENT_HUGEPAGE */
static void __init pmd_soft_dirty_tests(struct pgtable_debug_args *args) { }
static void __init pmd_swap_soft_dirty_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static void __init pte_swap_exclusive_tests(struct pgtable_debug_args *args)
{
	unsigned long max_swap_offset;
	swp_entry_t entry, entry2;
	pte_t pte;

	pr_debug("Validating PTE swap exclusive\n");

	/* See generic_max_swapfile_size(): probe the maximum offset */
	max_swap_offset = swp_offset(pte_to_swp_entry(swp_entry_to_pte(swp_entry(0, ~0UL))));

	/* Create a swp entry with all possible bits set */
	entry = swp_entry((1 << MAX_SWAPFILES_SHIFT) - 1, max_swap_offset);

	pte = swp_entry_to_pte(entry);
	WARN_ON(pte_swp_exclusive(pte));
	WARN_ON(!is_swap_pte(pte));
	entry2 = pte_to_swp_entry(pte);
	WARN_ON(memcmp(&entry, &entry2, sizeof(entry)));

	pte = pte_swp_mkexclusive(pte);
	WARN_ON(!pte_swp_exclusive(pte));
	WARN_ON(!is_swap_pte(pte));
	WARN_ON(pte_swp_soft_dirty(pte));
	entry2 = pte_to_swp_entry(pte);
	WARN_ON(memcmp(&entry, &entry2, sizeof(entry)));

	pte = pte_swp_clear_exclusive(pte);
	WARN_ON(pte_swp_exclusive(pte));
	WARN_ON(!is_swap_pte(pte));
	entry2 = pte_to_swp_entry(pte);
	WARN_ON(memcmp(&entry, &entry2, sizeof(entry)));
}

static void __init pte_swap_tests(struct pgtable_debug_args *args)
{
	swp_entry_t swp;
	pte_t pte;

	pr_debug("Validating PTE swap\n");
	pte = pfn_pte(args->fixed_pte_pfn, args->page_prot);
	swp = __pte_to_swp_entry(pte);
	pte = __swp_entry_to_pte(swp);
	WARN_ON(args->fixed_pte_pfn != pte_pfn(pte));
}

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
static void __init pmd_swap_tests(struct pgtable_debug_args *args)
{
	swp_entry_t swp;
	pmd_t pmd;

	if (!has_transparent_hugepage())
		return;

	pr_debug("Validating PMD swap\n");
	pmd = pfn_pmd(args->fixed_pmd_pfn, args->page_prot);
	swp = __pmd_to_swp_entry(pmd);
	pmd = __swp_entry_to_pmd(swp);
	WARN_ON(args->fixed_pmd_pfn != pmd_pfn(pmd));
}
#else  /* !CONFIG_ARCH_ENABLE_THP_MIGRATION */
static void __init pmd_swap_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_ARCH_ENABLE_THP_MIGRATION */

static void __init swap_migration_tests(struct pgtable_debug_args *args)
{
	struct page *page;
	swp_entry_t swp;

	if (!IS_ENABLED(CONFIG_MIGRATION))
		return;

	/*
	 * swap_migration_tests() requires a dedicated page as it needs to
	 * be locked before creating a migration entry from it. Locking the
	 * page that actually maps kernel text ('start_kernel') can be real
	 * problematic. Lets use the allocated page explicitly for this
	 * purpose.
	 */
	page = (args->pte_pfn != ULONG_MAX) ? pfn_to_page(args->pte_pfn) : NULL;
	if (!page)
		return;

	pr_debug("Validating swap migration\n");

	/*
	 * make_[readable|writable]_migration_entry() expects given page to
	 * be locked, otherwise it stumbles upon a BUG_ON().
	 */
	__SetPageLocked(page);
	swp = make_writable_migration_entry(page_to_pfn(page));
	WARN_ON(!is_migration_entry(swp));
	WARN_ON(!is_writable_migration_entry(swp));

	swp = make_readable_migration_entry(swp_offset(swp));
	WARN_ON(!is_migration_entry(swp));
	WARN_ON(is_writable_migration_entry(swp));

	swp = make_readable_migration_entry(page_to_pfn(page));
	WARN_ON(!is_migration_entry(swp));
	WARN_ON(is_writable_migration_entry(swp));
	__ClearPageLocked(page);
}

#ifdef CONFIG_HUGETLB_PAGE
static void __init hugetlb_basic_tests(struct pgtable_debug_args *args)
{
	struct page *page;
	pte_t pte;

	pr_debug("Validating HugeTLB basic\n");
	/*
	 * Accessing the page associated with the pfn is safe here,
	 * as it was previously derived from a real kernel symbol.
	 */
	page = pfn_to_page(args->fixed_pmd_pfn);
	pte = mk_huge_pte(page, args->page_prot);

	WARN_ON(!huge_pte_dirty(huge_pte_mkdirty(pte)));
	WARN_ON(!huge_pte_write(huge_pte_mkwrite(huge_pte_wrprotect(pte))));
	WARN_ON(huge_pte_write(huge_pte_wrprotect(huge_pte_mkwrite(pte))));

#ifdef CONFIG_ARCH_WANT_GENERAL_HUGETLB
	pte = pfn_pte(args->fixed_pmd_pfn, args->page_prot);

	WARN_ON(!pte_huge(arch_make_huge_pte(pte, PMD_SHIFT, VM_ACCESS_FLAGS)));
#endif /* CONFIG_ARCH_WANT_GENERAL_HUGETLB */
}
#else  /* !CONFIG_HUGETLB_PAGE */
static void __init hugetlb_basic_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_HUGETLB_PAGE */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void __init pmd_thp_tests(struct pgtable_debug_args *args)
{
	pmd_t pmd;

	if (!has_transparent_hugepage())
		return;

	pr_debug("Validating PMD based THP\n");
	/*
	 * pmd_trans_huge() and pmd_present() must return positive after
	 * MMU invalidation with pmd_mkinvalid(). This behavior is an
	 * optimization for transparent huge page. pmd_trans_huge() must
	 * be true if pmd_page() returns a valid THP to avoid taking the
	 * pmd_lock when others walk over non transhuge pmds (i.e. there
	 * are no THP allocated). Especially when splitting a THP and
	 * removing the present bit from the pmd, pmd_trans_huge() still
	 * needs to return true. pmd_present() should be true whenever
	 * pmd_trans_huge() returns true.
	 */
	pmd = pfn_pmd(args->fixed_pmd_pfn, args->page_prot);
	WARN_ON(!pmd_trans_huge(pmd_mkhuge(pmd)));

#ifndef __HAVE_ARCH_PMDP_INVALIDATE
	WARN_ON(!pmd_trans_huge(pmd_mkinvalid(pmd_mkhuge(pmd))));
	WARN_ON(!pmd_present(pmd_mkinvalid(pmd_mkhuge(pmd))));
#endif /* __HAVE_ARCH_PMDP_INVALIDATE */
}

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
static void __init pud_thp_tests(struct pgtable_debug_args *args)
{
	pud_t pud;

	if (!has_transparent_pud_hugepage())
		return;

	pr_debug("Validating PUD based THP\n");
	pud = pfn_pud(args->fixed_pud_pfn, args->page_prot);
	WARN_ON(!pud_trans_huge(pud_mkhuge(pud)));

	/*
	 * pud_mkinvalid() has been dropped for now. Enable back
	 * these tests when it comes back with a modified pud_present().
	 *
	 * WARN_ON(!pud_trans_huge(pud_mkinvalid(pud_mkhuge(pud))));
	 * WARN_ON(!pud_present(pud_mkinvalid(pud_mkhuge(pud))));
	 */
}
#else  /* !CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */
static void __init pud_thp_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */
#else  /* !CONFIG_TRANSPARENT_HUGEPAGE */
static void __init pmd_thp_tests(struct pgtable_debug_args *args) { }
static void __init pud_thp_tests(struct pgtable_debug_args *args) { }
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static unsigned long __init get_random_vaddr(void)
{
	unsigned long random_vaddr, random_pages, total_user_pages;

	total_user_pages = (TASK_SIZE - FIRST_USER_ADDRESS) / PAGE_SIZE;

	random_pages = get_random_long() % total_user_pages;
	random_vaddr = FIRST_USER_ADDRESS + random_pages * PAGE_SIZE;

	return random_vaddr;
}

static void __init destroy_args(struct pgtable_debug_args *args)
{
	struct page *page = NULL;

	/* Free (huge) page */
	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
	    has_transparent_pud_hugepage() &&
	    args->pud_pfn != ULONG_MAX) {
		if (args->is_contiguous_page) {
			free_contig_range(args->pud_pfn,
					  (1 << (HPAGE_PUD_SHIFT - PAGE_SHIFT)));
		} else {
			page = pfn_to_page(args->pud_pfn);
			__free_pages(page, HPAGE_PUD_SHIFT - PAGE_SHIFT);
		}

		args->pud_pfn = ULONG_MAX;
		args->pmd_pfn = ULONG_MAX;
		args->pte_pfn = ULONG_MAX;
	}

	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
	    has_transparent_hugepage() &&
	    args->pmd_pfn != ULONG_MAX) {
		if (args->is_contiguous_page) {
			free_contig_range(args->pmd_pfn, (1 << HPAGE_PMD_ORDER));
		} else {
			page = pfn_to_page(args->pmd_pfn);
			__free_pages(page, HPAGE_PMD_ORDER);
		}

		args->pmd_pfn = ULONG_MAX;
		args->pte_pfn = ULONG_MAX;
	}

	if (args->pte_pfn != ULONG_MAX) {
		page = pfn_to_page(args->pte_pfn);
		__free_page(page);

		args->pte_pfn = ULONG_MAX;
	}

	/* Free page table entries */
	if (args->start_ptep) {
		pte_free(args->mm, args->start_ptep);
		mm_dec_nr_ptes(args->mm);
	}

	if (args->start_pmdp) {
		pmd_free(args->mm, args->start_pmdp);
		mm_dec_nr_pmds(args->mm);
	}

	if (args->start_pudp) {
		pud_free(args->mm, args->start_pudp);
		mm_dec_nr_puds(args->mm);
	}

	if (args->start_p4dp)
		p4d_free(args->mm, args->start_p4dp);

	/* Free vma and mm struct */
	if (args->vma)
		vm_area_free(args->vma);

	if (args->mm)
		mmdrop(args->mm);
}

static struct page * __init
debug_vm_pgtable_alloc_huge_page(struct pgtable_debug_args *args, int order)
{
	struct page *page = NULL;

#ifdef CONFIG_CONTIG_ALLOC
	if (order > MAX_ORDER) {
		page = alloc_contig_pages((1 << order), GFP_KERNEL,
					  first_online_node, NULL);
		if (page) {
			args->is_contiguous_page = true;
			return page;
		}
	}
#endif

	if (order <= MAX_ORDER)
		page = alloc_pages(GFP_KERNEL, order);

	return page;
}

/*
 * Check if a physical memory range described by <pstart, pend> contains
 * an area that is of size psize, and aligned to psize.
 *
 * Don't use address 0, an all-zeroes physical address might mask bugs, and
 * it's not used on x86.
 */
static void  __init phys_align_check(phys_addr_t pstart,
				     phys_addr_t pend, unsigned long psize,
				     phys_addr_t *physp, unsigned long *alignp)
{
	phys_addr_t aligned_start, aligned_end;

	if (pstart == 0)
		pstart = PAGE_SIZE;

	aligned_start = ALIGN(pstart, psize);
	aligned_end = aligned_start + psize;

	if (aligned_end > aligned_start && aligned_end <= pend) {
		*alignp = psize;
		*physp = aligned_start;
	}
}

static void __init init_fixed_pfns(struct pgtable_debug_args *args)
{
	u64 idx;
	phys_addr_t phys, pstart, pend;

	/*
	 * Initialize the fixed pfns. To do this, try to find a
	 * valid physical range, preferably aligned to PUD_SIZE,
	 * but settling for aligned to PMD_SIZE as a fallback. If
	 * neither of those is found, use the physical address of
	 * the start_kernel symbol.
	 *
	 * The memory doesn't need to be allocated, it just needs to exist
	 * as usable memory. It won't be touched.
	 *
	 * The alignment is recorded, and can be checked to see if we
	 * can run the tests that require an actual valid physical
	 * address range on some architectures ({pmd,pud}_huge_test
	 * on x86).
	 */

	phys = __pa_symbol(&start_kernel);
	args->fixed_alignment = PAGE_SIZE;

	for_each_mem_range(idx, &pstart, &pend) {
		/* First check for a PUD-aligned area */
		phys_align_check(pstart, pend, PUD_SIZE, &phys,
				 &args->fixed_alignment);

		/* If a PUD-aligned area is found, we're done */
		if (args->fixed_alignment == PUD_SIZE)
			break;

		/*
		 * If no PMD-aligned area found yet, check for one,
		 * but continue the loop to look for a PUD-aligned area.
		 */
		if (args->fixed_alignment < PMD_SIZE)
			phys_align_check(pstart, pend, PMD_SIZE, &phys,
					 &args->fixed_alignment);
	}

	args->fixed_pgd_pfn = __phys_to_pfn(phys & PGDIR_MASK);
	args->fixed_p4d_pfn = __phys_to_pfn(phys & P4D_MASK);
	args->fixed_pud_pfn = __phys_to_pfn(phys & PUD_MASK);
	args->fixed_pmd_pfn = __phys_to_pfn(phys & PMD_MASK);
	args->fixed_pte_pfn = __phys_to_pfn(phys & PAGE_MASK);
	WARN_ON(!pfn_valid(args->fixed_pte_pfn));
}


static int __init init_args(struct pgtable_debug_args *args)
{
	struct page *page = NULL;
	int ret = 0;

	/*
	 * Initialize the debugging data.
	 *
	 * vm_get_page_prot(VM_NONE) or vm_get_page_prot(VM_SHARED|VM_NONE)
	 * will help create page table entries with PROT_NONE permission as
	 * required for pxx_protnone_tests().
	 */
	memset(args, 0, sizeof(*args));
	args->vaddr              = get_random_vaddr();
	args->page_prot          = vm_get_page_prot(VM_ACCESS_FLAGS);
	args->page_prot_none     = vm_get_page_prot(VM_NONE);
	args->is_contiguous_page = false;
	args->pud_pfn            = ULONG_MAX;
	args->pmd_pfn            = ULONG_MAX;
	args->pte_pfn            = ULONG_MAX;
	args->fixed_pgd_pfn      = ULONG_MAX;
	args->fixed_p4d_pfn      = ULONG_MAX;
	args->fixed_pud_pfn      = ULONG_MAX;
	args->fixed_pmd_pfn      = ULONG_MAX;
	args->fixed_pte_pfn      = ULONG_MAX;

	/* Allocate mm and vma */
	args->mm = mm_alloc();
	if (!args->mm) {
		pr_err("Failed to allocate mm struct\n");
		ret = -ENOMEM;
		goto error;
	}

	args->vma = vm_area_alloc(args->mm);
	if (!args->vma) {
		pr_err("Failed to allocate vma\n");
		ret = -ENOMEM;
		goto error;
	}

	/*
	 * Allocate page table entries. They will be modified in the tests.
	 * Lets save the page table entries so that they can be released
	 * when the tests are completed.
	 */
	args->pgdp = pgd_offset(args->mm, args->vaddr);
	args->p4dp = p4d_alloc(args->mm, args->pgdp, args->vaddr);
	if (!args->p4dp) {
		pr_err("Failed to allocate p4d entries\n");
		ret = -ENOMEM;
		goto error;
	}
	args->start_p4dp = p4d_offset(args->pgdp, 0UL);
	WARN_ON(!args->start_p4dp);

	args->pudp = pud_alloc(args->mm, args->p4dp, args->vaddr);
	if (!args->pudp) {
		pr_err("Failed to allocate pud entries\n");
		ret = -ENOMEM;
		goto error;
	}
	args->start_pudp = pud_offset(args->p4dp, 0UL);
	WARN_ON(!args->start_pudp);

	args->pmdp = pmd_alloc(args->mm, args->pudp, args->vaddr);
	if (!args->pmdp) {
		pr_err("Failed to allocate pmd entries\n");
		ret = -ENOMEM;
		goto error;
	}
	args->start_pmdp = pmd_offset(args->pudp, 0UL);
	WARN_ON(!args->start_pmdp);

	if (pte_alloc(args->mm, args->pmdp)) {
		pr_err("Failed to allocate pte entries\n");
		ret = -ENOMEM;
		goto error;
	}
	args->start_ptep = pmd_pgtable(READ_ONCE(*args->pmdp));
	WARN_ON(!args->start_ptep);

	init_fixed_pfns(args);

	/*
	 * Allocate (huge) pages because some of the tests need to access
	 * the data in the pages. The corresponding tests will be skipped
	 * if we fail to allocate (huge) pages.
	 */
	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
	    has_transparent_pud_hugepage()) {
		page = debug_vm_pgtable_alloc_huge_page(args,
				HPAGE_PUD_SHIFT - PAGE_SHIFT);
		if (page) {
			args->pud_pfn = page_to_pfn(page);
			args->pmd_pfn = args->pud_pfn;
			args->pte_pfn = args->pud_pfn;
			return 0;
		}
	}

	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
	    has_transparent_hugepage()) {
		page = debug_vm_pgtable_alloc_huge_page(args, HPAGE_PMD_ORDER);
		if (page) {
			args->pmd_pfn = page_to_pfn(page);
			args->pte_pfn = args->pmd_pfn;
			return 0;
		}
	}

	page = alloc_page(GFP_KERNEL);
	if (page)
		args->pte_pfn = page_to_pfn(page);

	return 0;

error:
	destroy_args(args);
	return ret;
}

static int __init debug_vm_pgtable(void)
{
	struct pgtable_debug_args args;
	spinlock_t *ptl = NULL;
	int idx, ret;

	pr_info("Validating architecture page table helpers\n");
	ret = init_args(&args);
	if (ret)
		return ret;

	/*
	 * Iterate over each possible vm_flags to make sure that all
	 * the basic page table transformation validations just hold
	 * true irrespective of the starting protection value for a
	 * given page table entry.
	 *
	 * Protection based vm_flags combinations are always linear
	 * and increasing i.e starting from VM_NONE and going up to
	 * (VM_SHARED | READ | WRITE | EXEC).
	 */
#define VM_FLAGS_START	(VM_NONE)
#define VM_FLAGS_END	(VM_SHARED | VM_EXEC | VM_WRITE | VM_READ)

	for (idx = VM_FLAGS_START; idx <= VM_FLAGS_END; idx++) {
		pte_basic_tests(&args, idx);
		pmd_basic_tests(&args, idx);
		pud_basic_tests(&args, idx);
	}

	/*
	 * Both P4D and PGD level tests are very basic which do not
	 * involve creating page table entries from the protection
	 * value and the given pfn. Hence just keep them out from
	 * the above iteration for now to save some test execution
	 * time.
	 */
	p4d_basic_tests(&args);
	pgd_basic_tests(&args);

	pmd_leaf_tests(&args);
	pud_leaf_tests(&args);

	pte_special_tests(&args);
	pte_protnone_tests(&args);
	pmd_protnone_tests(&args);

	pte_devmap_tests(&args);
	pmd_devmap_tests(&args);
	pud_devmap_tests(&args);

	pte_soft_dirty_tests(&args);
	pmd_soft_dirty_tests(&args);
	pte_swap_soft_dirty_tests(&args);
	pmd_swap_soft_dirty_tests(&args);

	pte_swap_exclusive_tests(&args);

	pte_swap_tests(&args);
	pmd_swap_tests(&args);

	swap_migration_tests(&args);

	pmd_thp_tests(&args);
	pud_thp_tests(&args);

	hugetlb_basic_tests(&args);

	/*
	 * Page table modifying tests. They need to hold
	 * proper page table lock.
	 */

	args.ptep = pte_offset_map_lock(args.mm, args.pmdp, args.vaddr, &ptl);
	pte_clear_tests(&args);
	pte_advanced_tests(&args);
	if (args.ptep)
		pte_unmap_unlock(args.ptep, ptl);

	ptl = pmd_lock(args.mm, args.pmdp);
	pmd_clear_tests(&args);
	pmd_advanced_tests(&args);
	pmd_huge_tests(&args);
	pmd_populate_tests(&args);
	spin_unlock(ptl);

	ptl = pud_lock(args.mm, args.pudp);
	pud_clear_tests(&args);
	pud_advanced_tests(&args);
	pud_huge_tests(&args);
	pud_populate_tests(&args);
	spin_unlock(ptl);

	spin_lock(&(args.mm->page_table_lock));
	p4d_clear_tests(&args);
	pgd_clear_tests(&args);
	p4d_populate_tests(&args);
	pgd_populate_tests(&args);
	spin_unlock(&(args.mm->page_table_lock));

	destroy_args(&args);
	return 0;
}
late_initcall(debug_vm_pgtable);
