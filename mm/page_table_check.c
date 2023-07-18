// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2021, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */
#include <linux/kstrtox.h>
#include <linux/mm.h>
#include <linux/page_table_check.h>

#undef pr_fmt
#define pr_fmt(fmt)	"page_table_check: " fmt

struct page_table_check {
	atomic_t anon_map_count;
	atomic_t file_map_count;
};

static bool __page_table_check_enabled __initdata =
				IS_ENABLED(CONFIG_PAGE_TABLE_CHECK_ENFORCED);

DEFINE_STATIC_KEY_TRUE(page_table_check_disabled);
EXPORT_SYMBOL(page_table_check_disabled);

static int __init early_page_table_check_param(char *buf)
{
	return kstrtobool(buf, &__page_table_check_enabled);
}

early_param("page_table_check", early_page_table_check_param);

static bool __init need_page_table_check(void)
{
	return __page_table_check_enabled;
}

static void __init init_page_table_check(void)
{
	if (!__page_table_check_enabled)
		return;
	static_branch_disable(&page_table_check_disabled);
}

struct page_ext_operations page_table_check_ops = {
	.size = sizeof(struct page_table_check),
	.need = need_page_table_check,
	.init = init_page_table_check,
	.need_shared_flags = false,
};

static struct page_table_check *get_page_table_check(struct page_ext *page_ext)
{
	BUG_ON(!page_ext);
	return page_ext_data(page_ext, &page_table_check_ops);
}

/*
 * An entry is removed from the page table, decrement the counters for that page
 * verify that it is of correct type and counters do not become negative.
 */
static void page_table_check_clear(unsigned long pfn, unsigned long pgcnt)
{
	struct page_ext *page_ext;
	struct page *page;
	unsigned long i;
	bool anon;

	if (!pfn_valid(pfn))
		return;

	page = pfn_to_page(pfn);
	page_ext = page_ext_get(page);

	BUG_ON(PageSlab(page));
	anon = PageAnon(page);

	for (i = 0; i < pgcnt; i++) {
		struct page_table_check *ptc = get_page_table_check(page_ext);

		if (anon) {
			BUG_ON(atomic_read(&ptc->file_map_count));
			BUG_ON(atomic_dec_return(&ptc->anon_map_count) < 0);
		} else {
			BUG_ON(atomic_read(&ptc->anon_map_count));
			BUG_ON(atomic_dec_return(&ptc->file_map_count) < 0);
		}
		page_ext = page_ext_next(page_ext);
	}
	page_ext_put(page_ext);
}

/*
 * A new entry is added to the page table, increment the counters for that page
 * verify that it is of correct type and is not being mapped with a different
 * type to a different process.
 */
static void page_table_check_set(unsigned long pfn, unsigned long pgcnt,
				 bool rw)
{
	struct page_ext *page_ext;
	struct page *page;
	unsigned long i;
	bool anon;

	if (!pfn_valid(pfn))
		return;

	page = pfn_to_page(pfn);
	page_ext = page_ext_get(page);

	BUG_ON(PageSlab(page));
	anon = PageAnon(page);

	for (i = 0; i < pgcnt; i++) {
		struct page_table_check *ptc = get_page_table_check(page_ext);

		if (anon) {
			BUG_ON(atomic_read(&ptc->file_map_count));
			BUG_ON(atomic_inc_return(&ptc->anon_map_count) > 1 && rw);
		} else {
			BUG_ON(atomic_read(&ptc->anon_map_count));
			BUG_ON(atomic_inc_return(&ptc->file_map_count) < 0);
		}
		page_ext = page_ext_next(page_ext);
	}
	page_ext_put(page_ext);
}

/*
 * page is on free list, or is being allocated, verify that counters are zeroes
 * crash if they are not.
 */
void __page_table_check_zero(struct page *page, unsigned int order)
{
	struct page_ext *page_ext;
	unsigned long i;

	BUG_ON(PageSlab(page));

	page_ext = page_ext_get(page);
	BUG_ON(!page_ext);
	for (i = 0; i < (1ul << order); i++) {
		struct page_table_check *ptc = get_page_table_check(page_ext);

		BUG_ON(atomic_read(&ptc->anon_map_count));
		BUG_ON(atomic_read(&ptc->file_map_count));
		page_ext = page_ext_next(page_ext);
	}
	page_ext_put(page_ext);
}

void __page_table_check_pte_clear(struct mm_struct *mm, pte_t pte)
{
	if (&init_mm == mm)
		return;

	if (pte_user_accessible_page(pte)) {
		page_table_check_clear(pte_pfn(pte), PAGE_SIZE >> PAGE_SHIFT);
	}
}
EXPORT_SYMBOL(__page_table_check_pte_clear);

void __page_table_check_pmd_clear(struct mm_struct *mm, pmd_t pmd)
{
	if (&init_mm == mm)
		return;

	if (pmd_user_accessible_page(pmd)) {
		page_table_check_clear(pmd_pfn(pmd), PMD_SIZE >> PAGE_SHIFT);
	}
}
EXPORT_SYMBOL(__page_table_check_pmd_clear);

void __page_table_check_pud_clear(struct mm_struct *mm, pud_t pud)
{
	if (&init_mm == mm)
		return;

	if (pud_user_accessible_page(pud)) {
		page_table_check_clear(pud_pfn(pud), PUD_SIZE >> PAGE_SHIFT);
	}
}
EXPORT_SYMBOL(__page_table_check_pud_clear);

void __page_table_check_pte_set(struct mm_struct *mm, pte_t *ptep, pte_t pte)
{
	if (&init_mm == mm)
		return;

	__page_table_check_pte_clear(mm, ptep_get(ptep));
	if (pte_user_accessible_page(pte)) {
		page_table_check_set(pte_pfn(pte), PAGE_SIZE >> PAGE_SHIFT,
				     pte_write(pte));
	}
}
EXPORT_SYMBOL(__page_table_check_pte_set);

void __page_table_check_pmd_set(struct mm_struct *mm, pmd_t *pmdp, pmd_t pmd)
{
	if (&init_mm == mm)
		return;

	__page_table_check_pmd_clear(mm, *pmdp);
	if (pmd_user_accessible_page(pmd)) {
		page_table_check_set(pmd_pfn(pmd), PMD_SIZE >> PAGE_SHIFT,
				     pmd_write(pmd));
	}
}
EXPORT_SYMBOL(__page_table_check_pmd_set);

void __page_table_check_pud_set(struct mm_struct *mm, pud_t *pudp, pud_t pud)
{
	if (&init_mm == mm)
		return;

	__page_table_check_pud_clear(mm, *pudp);
	if (pud_user_accessible_page(pud)) {
		page_table_check_set(pud_pfn(pud), PUD_SIZE >> PAGE_SHIFT,
				     pud_write(pud));
	}
}
EXPORT_SYMBOL(__page_table_check_pud_set);

void __page_table_check_pte_clear_range(struct mm_struct *mm,
					unsigned long addr,
					pmd_t pmd)
{
	if (&init_mm == mm)
		return;

	if (!pmd_bad(pmd) && !pmd_leaf(pmd)) {
		pte_t *ptep = pte_offset_map(&pmd, addr);
		unsigned long i;

		if (WARN_ON(!ptep))
			return;
		for (i = 0; i < PTRS_PER_PTE; i++) {
			__page_table_check_pte_clear(mm, ptep_get(ptep));
			addr += PAGE_SIZE;
			ptep++;
		}
		pte_unmap(ptep - PTRS_PER_PTE);
	}
}
