#ifndef _I386_PGTABLE_3LEVEL_DEFS_H
#define _I386_PGTABLE_3LEVEL_DEFS_H

/*
 * PGDIR_SHIFT determines what a top-level page table entry can map
 */
#define PGDIR_SHIFT	30
#define PTRS_PER_PGD	4

/*
 * PMD_SHIFT determines the size of the area a middle-level
 * page table can map
 */
#define PMD_SHIFT	21
#define PTRS_PER_PMD	512

/*
 * entries per page directory level
 */
#define PTRS_PER_PTE	512

#endif /* _I386_PGTABLE_3LEVEL_DEFS_H */
