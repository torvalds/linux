#ifndef _I386_PGTABLE_2LEVEL_DEFS_H
#define _I386_PGTABLE_2LEVEL_DEFS_H

/*
 * traditional i386 two-level paging structure:
 */

#define PGDIR_SHIFT	22
#define PTRS_PER_PGD	1024

/*
 * the i386 is two-level, so we don't really have any
 * PMD directory physically.
 */

#define PTRS_PER_PTE	1024

#endif /* _I386_PGTABLE_2LEVEL_DEFS_H */
