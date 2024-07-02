/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_PTDUMP_H
#define _LINUX_PTDUMP_H

#include <linux/mm_types.h>

struct ptdump_range {
	unsigned long start;
	unsigned long end;
};

struct ptdump_state {
	/* level is 0:PGD to 4:PTE, or -1 if unknown */
	void (*note_page)(struct ptdump_state *st, unsigned long addr,
			  int level, u64 val);
	void (*effective_prot)(struct ptdump_state *st, int level, u64 val);
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
