// SPDX-License-Identifier: GPL-2.0

#include <linux/pagewalk.h>
#include <linux/ptdump.h>
#include <linux/kasan.h>

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
/*
 * This is an optimization for KASAN=y case. Since all kasan page tables
 * eventually point to the kasan_early_shadow_page we could call note_page()
 * right away without walking through lower level page tables. This saves
 * us dozens of seconds (minutes for 5-level config) while checking for
 * W+X mapping or reading kernel_page_tables debugfs file.
 */
static inline int note_kasan_page_table(struct mm_walk *walk,
					unsigned long addr)
{
	struct ptdump_state *st = walk->private;

	st->note_page(st, addr, 4, pte_val(kasan_early_shadow_pte[0]));

	walk->action = ACTION_CONTINUE;

	return 0;
}
#endif

static int ptdump_pgd_entry(pgd_t *pgd, unsigned long addr,
			    unsigned long next, struct mm_walk *walk)
{
	struct ptdump_state *st = walk->private;
	pgd_t val = READ_ONCE(*pgd);

#if CONFIG_PGTABLE_LEVELS > 4 && \
		(defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS))
	if (pgd_page(val) == virt_to_page(lm_alias(kasan_early_shadow_p4d)))
		return note_kasan_page_table(walk, addr);
#endif

	if (st->effective_prot)
		st->effective_prot(st, 0, pgd_val(val));

	if (pgd_leaf(val)) {
		st->note_page(st, addr, 0, pgd_val(val));
		walk->action = ACTION_CONTINUE;
	}

	return 0;
}

static int ptdump_p4d_entry(p4d_t *p4d, unsigned long addr,
			    unsigned long next, struct mm_walk *walk)
{
	struct ptdump_state *st = walk->private;
	p4d_t val = READ_ONCE(*p4d);

#if CONFIG_PGTABLE_LEVELS > 3 && \
		(defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS))
	if (p4d_page(val) == virt_to_page(lm_alias(kasan_early_shadow_pud)))
		return note_kasan_page_table(walk, addr);
#endif

	if (st->effective_prot)
		st->effective_prot(st, 1, p4d_val(val));

	if (p4d_leaf(val)) {
		st->note_page(st, addr, 1, p4d_val(val));
		walk->action = ACTION_CONTINUE;
	}

	return 0;
}

static int ptdump_pud_entry(pud_t *pud, unsigned long addr,
			    unsigned long next, struct mm_walk *walk)
{
	struct ptdump_state *st = walk->private;
	pud_t val = READ_ONCE(*pud);

#if CONFIG_PGTABLE_LEVELS > 2 && \
		(defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS))
	if (pud_page(val) == virt_to_page(lm_alias(kasan_early_shadow_pmd)))
		return note_kasan_page_table(walk, addr);
#endif

	if (st->effective_prot)
		st->effective_prot(st, 2, pud_val(val));

	if (pud_leaf(val)) {
		st->note_page(st, addr, 2, pud_val(val));
		walk->action = ACTION_CONTINUE;
	}

	return 0;
}

static int ptdump_pmd_entry(pmd_t *pmd, unsigned long addr,
			    unsigned long next, struct mm_walk *walk)
{
	struct ptdump_state *st = walk->private;
	pmd_t val = READ_ONCE(*pmd);

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
	if (pmd_page(val) == virt_to_page(lm_alias(kasan_early_shadow_pte)))
		return note_kasan_page_table(walk, addr);
#endif

	if (st->effective_prot)
		st->effective_prot(st, 3, pmd_val(val));
	if (pmd_leaf(val)) {
		st->note_page(st, addr, 3, pmd_val(val));
		walk->action = ACTION_CONTINUE;
	}

	return 0;
}

static int ptdump_pte_entry(pte_t *pte, unsigned long addr,
			    unsigned long next, struct mm_walk *walk)
{
	struct ptdump_state *st = walk->private;
	pte_t val = ptep_get(pte);

	if (st->effective_prot)
		st->effective_prot(st, 4, pte_val(val));

	st->note_page(st, addr, 4, pte_val(val));

	return 0;
}

static int ptdump_hole(unsigned long addr, unsigned long next,
		       int depth, struct mm_walk *walk)
{
	struct ptdump_state *st = walk->private;

	st->note_page(st, addr, depth, 0);

	return 0;
}

static const struct mm_walk_ops ptdump_ops = {
	.pgd_entry	= ptdump_pgd_entry,
	.p4d_entry	= ptdump_p4d_entry,
	.pud_entry	= ptdump_pud_entry,
	.pmd_entry	= ptdump_pmd_entry,
	.pte_entry	= ptdump_pte_entry,
	.pte_hole	= ptdump_hole,
};

void ptdump_walk_pgd(struct ptdump_state *st, struct mm_struct *mm, pgd_t *pgd)
{
	const struct ptdump_range *range = st->range;

	mmap_read_lock(mm);
	while (range->start != range->end) {
		walk_page_range_novma(mm, range->start, range->end,
				      &ptdump_ops, pgd, st);
		range++;
	}
	mmap_read_unlock(mm);

	/* Flush out the last page */
	st->note_page(st, 0, -1, 0);
}
