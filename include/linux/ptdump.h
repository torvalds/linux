/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_PTDUMP_H
#define _LINUX_PTDUMP_H

#include <linux/mm_types.h>

struct ptdump_range {
	unsigned long start;
	unsigned long end;
};

struct ptdump_state {
	void (*note_page_pte)(struct ptdump_state *st, unsigned long addr, pte_t pte);
	void (*note_page_pmd)(struct ptdump_state *st, unsigned long addr, pmd_t pmd);
	void (*note_page_pud)(struct ptdump_state *st, unsigned long addr, pud_t pud);
	void (*note_page_p4d)(struct ptdump_state *st, unsigned long addr, p4d_t p4d);
	void (*note_page_pgd)(struct ptdump_state *st, unsigned long addr, pgd_t pgd);
	void (*note_page_flush)(struct ptdump_state *st);
	void (*effective_prot_pte)(struct ptdump_state *st, pte_t pte);
	void (*effective_prot_pmd)(struct ptdump_state *st, pmd_t pmd);
	void (*effective_prot_pud)(struct ptdump_state *st, pud_t pud);
	void (*effective_prot_p4d)(struct ptdump_state *st, p4d_t p4d);
	void (*effective_prot_pgd)(struct ptdump_state *st, pgd_t pgd);
	const struct ptdump_range *range;
};

bool ptdump_walk_pgd_level_core(struct seq_file *m,
				struct mm_struct *mm, pgd_t *pgd,
				bool checkwx, bool dmesg);
void ptdump_walk_pgd(struct ptdump_state *st, struct mm_struct *mm, pgd_t *pgd);
bool ptdump_check_wx(void);

static inline void debug_checkwx(void)
{
	if (IS_ENABLED(CONFIG_DEBUG_WX))
		ptdump_check_wx();
}

#endif /* _LINUX_PTDUMP_H */
