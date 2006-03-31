#ifndef _CRIS_PAGE_H
#define _CRIS_PAGE_H

#include <linux/config.h>
#include <asm/arch/page.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	13
#ifndef __ASSEMBLY__
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#else
#define PAGE_SIZE	(1 << PAGE_SHIFT)
#endif
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

#define clear_page(page)        memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from)      memcpy((void *)(to), (void *)(from), PAGE_SIZE)

#define clear_user_page(page, vaddr, pg)    clear_page(page)
#define copy_user_page(to, from, vaddr, pg) copy_page(to, from)

#define alloc_zeroed_user_highpage(vma, vaddr) alloc_page_vma(GFP_HIGHUSER | __GFP_ZERO, vma, vaddr)
#define __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE

/*
 * These are used to make use of C type-checking..
 */
#ifndef __ASSEMBLY__
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
#endif

#define pte_val(x)	((x).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

/* On CRIS the PFN numbers doesn't start at 0 so we have to compensate */
/* for that before indexing into the page table starting at mem_map    */
#define ARCH_PFN_OFFSET		(PAGE_OFFSET >> PAGE_SHIFT)
#define pfn_valid(pfn)		(((pfn) - (PAGE_OFFSET >> PAGE_SHIFT)) < max_mapnr)

/* to index into the page map. our pages all start at physical addr PAGE_OFFSET so
 * we can let the map start there. notice that we subtract PAGE_OFFSET because
 * we start our mem_map there - in other ports they map mem_map physically and
 * use __pa instead. in our system both the physical and virtual address of DRAM
 * is too high to let mem_map start at 0, so we do it this way instead (similar
 * to arm and m68k I think)
 */ 

#define virt_to_page(kaddr)    (mem_map + (((unsigned long)(kaddr) - PAGE_OFFSET) >> PAGE_SHIFT))
#define VALID_PAGE(page)       (((page) - mem_map) < max_mapnr)
#define virt_addr_valid(kaddr)	pfn_valid((unsigned)(kaddr) >> PAGE_SHIFT)

/* convert a page (based on mem_map and forward) to a physical address
 * do this by figuring out the virtual address and then use __pa
 */

#define page_to_phys(page)     __pa((((page) - mem_map) << PAGE_SHIFT) + PAGE_OFFSET)

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#ifndef __ASSEMBLY__

#endif /* __ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#endif /* __KERNEL__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#endif /* _CRIS_PAGE_H */

