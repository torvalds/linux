#ifndef _4LEVEL_FIXUP_H
#define _4LEVEL_FIXUP_H

#define __ARCH_HAS_4LEVEL_HACK
#define __PAGETABLE_PUD_FOLDED

#define PUD_SHIFT			PGDIR_SHIFT
#define PUD_SIZE			PGDIR_SIZE
#define PUD_MASK			PGDIR_MASK
#define PTRS_PER_PUD			1

#define pud_t				pgd_t

#define pmd_alloc(mm, pud, address) \
	((unlikely(pgd_none(*(pud))) && __pmd_alloc(mm, pud, address))? \
 		NULL: pmd_offset(pud, address))

#define pud_alloc(mm, pgd, address)	(pgd)
#define pud_offset(pgd, start)		(pgd)
#define pud_none(pud)			0
#define pud_bad(pud)			0
#define pud_present(pud)		1
#define pud_ERROR(pud)			do { } while (0)
#define pud_clear(pud)			pgd_clear(pud)
#define pud_val(pud)			pgd_val(pud)
#define pud_populate(mm, pud, pmd)	pgd_populate(mm, pud, pmd)
#define pud_page(pud)			pgd_page(pud)
#define pud_page_vaddr(pud)		pgd_page_vaddr(pud)

#undef pud_free_tlb
#define pud_free_tlb(tlb, x, addr)	do { } while (0)
#define pud_free(mm, x)			do { } while (0)
#define __pud_free_tlb(tlb, x, addr)	do { } while (0)

#undef  pud_addr_end
#define pud_addr_end(addr, end)		(end)

#endif
