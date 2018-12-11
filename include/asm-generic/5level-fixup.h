/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _5LEVEL_FIXUP_H
#define _5LEVEL_FIXUP_H

#define __ARCH_HAS_5LEVEL_HACK
#define __PAGETABLE_P4D_FOLDED 1

#define P4D_SHIFT			PGDIR_SHIFT
#define P4D_SIZE			PGDIR_SIZE
#define P4D_MASK			PGDIR_MASK
#define MAX_PTRS_PER_P4D		1
#define PTRS_PER_P4D			1

#define p4d_t				pgd_t

#define pud_alloc(mm, p4d, address) \
	((unlikely(pgd_none(*(p4d))) && __pud_alloc(mm, p4d, address)) ? \
		NULL : pud_offset(p4d, address))

#define p4d_alloc(mm, pgd, address)	(pgd)
#define p4d_offset(pgd, start)		(pgd)
#define p4d_none(p4d)			0
#define p4d_bad(p4d)			0
#define p4d_present(p4d)		1
#define p4d_ERROR(p4d)			do { } while (0)
#define p4d_clear(p4d)			pgd_clear(p4d)
#define p4d_val(p4d)			pgd_val(p4d)
#define p4d_populate(mm, p4d, pud)	pgd_populate(mm, p4d, pud)
#define p4d_page(p4d)			pgd_page(p4d)
#define p4d_page_vaddr(p4d)		pgd_page_vaddr(p4d)

#define __p4d(x)			__pgd(x)
#define set_p4d(p4dp, p4d)		set_pgd(p4dp, p4d)

#undef p4d_free_tlb
#define p4d_free_tlb(tlb, x, addr)	do { } while (0)
#define p4d_free(mm, x)			do { } while (0)
#define __p4d_free_tlb(tlb, x, addr)	do { } while (0)

#undef  p4d_addr_end
#define p4d_addr_end(addr, end)		(end)

#endif
