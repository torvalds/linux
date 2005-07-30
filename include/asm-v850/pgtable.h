#ifndef __V850_PGTABLE_H__
#define __V850_PGTABLE_H__

#include <asm-generic/4level-fixup.h>

#include <linux/config.h>
#include <asm/page.h>


#define pgd_present(pgd)	(1) /* pages are always present on NO_MM */
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_clear(pgdp)		((void)0)

#define	pmd_offset(a, b)	((void *)0)

#define kern_addr_valid(addr)	(1)


#define __swp_type(x)		(0)
#define __swp_offset(x)		(0)
#define __swp_entry(typ,off)	((swp_entry_t) { ((typ) | ((off) << 7)) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

static inline int pte_file (pte_t pte) { return 0; }


/* These mean nothing to !CONFIG_MMU.  */
#define PAGE_NONE		__pgprot(0)
#define PAGE_SHARED		__pgprot(0)
#define PAGE_COPY		__pgprot(0)
#define PAGE_READONLY		__pgprot(0)
#define PAGE_KERNEL		__pgprot(0)


/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc.  When CONFIG_MMU is not defined, this
 * should never actually be used, so just define it to something that's
 * will hopefully cause a bus error if it is.
 */
#define ZERO_PAGE(vaddr)	((void *)0x87654321)


/* Some bogus code in procfs uses these; whatever.  */
#define VMALLOC_START	0
#define VMALLOC_END	(~0)


extern void paging_init (void);
#define swapper_pg_dir ((pgd_t *) 0)

#define pgtable_cache_init()   ((void)0)


extern unsigned int kobjsize(const void *objp);


#endif /* __V850_PGTABLE_H__ */
