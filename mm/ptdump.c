// SPDX-License-Identifier: GPL-2.0

#include <linux/pagewalk.h>
#include <linux/debugfs.h>
#include <linux/ptdump.h>
#include <linux/kasan.h>
#include "internal.h"

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

	st->note_page_pte(st, addr, kasan_early_shadow_pte[0]);

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

	if (st->effective_prot_pgd)
		st->effective_prot_pgd(st, val);

	if (pgd_leaf(val)) {
		st->note_page_pgd(st, addr, val);
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

	if (st->effective_prot_p4d)
		st->effective_prot_p4d(st, val);

	if (p4d_leaf(val)) {
		st->note_page_p4d(st, addr, val);
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

	if (st->effective_prot_pud)
		st->effective_prot_pud(st, val);

	if (pud_leaf(val)) {
		st->note_page_pud(st, addr, val);
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

	if (st->effective_prot_pmd)
		st->effective_prot_pmd(st, val);
	if (pmd_leaf(val)) {
		st->note_page_pmd(st, addr, val);
		walk->action = ACTION_CONTINUE;
	}

	return 0;
}

static int ptdump_pte_entry(pte_t *pte, unsigned long addr,
			    unsigned long next, struct mm_walk *walk)
{
	struct ptdump_state *st = walk->private;
	pte_t val = ptep_get_lockless(pte);

	if (st->effective_prot_pte)
		st->effective_prot_pte(st, val);

	st->note_page_pte(st, addr, val);

	return 0;
}

static int ptdump_hole(unsigned long addr, unsigned long next,
		       int depth, struct mm_walk *walk)
{
	struct ptdump_state *st = walk->private;
	pte_t pte_zero = {0};
	pmd_t pmd_zero = {0};
	pud_t pud_zero = {0};
	p4d_t p4d_zero = {0};
	pgd_t pgd_zero = {0};

	switch (depth) {
	case 4:
		st->note_page_pte(st, addr, pte_zero);
		break;
	case 3:
		st->note_page_pmd(st, addr, pmd_zero);
		break;
	case 2:
		st->note_page_pud(st, addr, pud_zero);
		break;
	case 1:
		st->note_page_p4d(st, addr, p4d_zero);
		break;
	case 0:
		st->note_page_pgd(st, addr, pgd_zero);
		break;
	default:
		break;
	}
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

	get_online_mems();
	mmap_write_lock(mm);
	while (range->start != range->end) {
		walk_page_range_debug(mm, range->start, range->end,
				      &ptdump_ops, pgd, st);
		range++;
	}
	mmap_write_unlock(mm);
	put_online_mems();

	/* Flush out the last page */
	st->note_page_flush(st);
}

static int check_wx_show(struct seq_file *m, void *v)
{
	if (ptdump_check_wx())
		seq_puts(m, "SUCCESS\n");
	else
		seq_puts(m, "FAILED\n");

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(check_wx);

static int ptdump_debugfs_init(void)
{
	debugfs_create_file("check_wx_pages", 0400, NULL, NULL, &check_wx_fops);

	return 0;
}

device_initcall(ptdump_debugfs_init);
