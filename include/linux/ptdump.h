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
			  int level, unsigned long val);
	void (*effective_prot)(struct ptdump_state *st, int level, u64 val);
	const struct ptdump_range *range;
};

void ptdump_walk_pgd(struct ptdump_state *st, struct mm_struct *mm, pgd_t *pgd);

#endif /* _LINUX_PTDUMP_H */
