#ifndef _M68K_PAGE_H
#define _M68K_PAGE_H


#ifdef __KERNEL__

/* PAGE_SHIFT determines the page size */
#ifndef CONFIG_SUN3
#define PAGE_SHIFT	(12)
#else
#define PAGE_SHIFT	(13)
#endif
#ifdef __ASSEMBLY__
#define PAGE_SIZE	(1 << PAGE_SHIFT)
#else
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#endif
#define PAGE_MASK	(~(PAGE_SIZE-1))

#include <asm/setup.h>

#if PAGE_SHIFT < 13
#define THREAD_SIZE (8192)
#else
#define THREAD_SIZE PAGE_SIZE
#endif

#ifndef __ASSEMBLY__

#include <linux/compiler.h>

#include <asm/module.h>

#define get_user_page(vaddr)		__get_free_page(GFP_KERNEL)
#define free_user_page(page, addr)	free_page(addr)

/*
 * We don't need to check for alignment etc.
 */
#ifdef CPU_M68040_OR_M68060_ONLY
static inline void copy_page(void *to, void *from)
{
  unsigned long tmp;

  __asm__ __volatile__("1:\t"
		       ".chip 68040\n\t"
		       "move16 %1@+,%0@+\n\t"
		       "move16 %1@+,%0@+\n\t"
		       ".chip 68k\n\t"
		       "dbra  %2,1b\n\t"
		       : "=a" (to), "=a" (from), "=d" (tmp)
		       : "0" (to), "1" (from) , "2" (PAGE_SIZE / 32 - 1)
		       );
}

static inline void clear_page(void *page)
{
	unsigned long tmp;
	unsigned long *sp = page;

	*sp++ = 0;
	*sp++ = 0;
	*sp++ = 0;
	*sp++ = 0;

	__asm__ __volatile__("1:\t"
			     ".chip 68040\n\t"
			     "move16 %2@+,%0@+\n\t"
			     ".chip 68k\n\t"
			     "subqw  #8,%2\n\t"
			     "subqw  #8,%2\n\t"
			     "dbra   %1,1b\n\t"
			     : "=a" (sp), "=d" (tmp)
			     : "a" (page), "0" (sp),
			       "1" ((PAGE_SIZE - 16) / 16 - 1));
}

#else
#define clear_page(page)	memset((page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((to), (from), PAGE_SIZE)
#endif

#define clear_user_page(addr, vaddr, page)	\
	do {	clear_page(addr);		\
		flush_dcache_page(page);	\
	} while (0)
#define copy_user_page(to, from, vaddr, page)	\
	do {	copy_page(to, from);		\
		flush_dcache_page(page);	\
	} while (0)

/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd[16]; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((&x)->pmd[0])
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)	((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#endif /* !__ASSEMBLY__ */

#include <asm/page_offset.h>

#define PAGE_OFFSET		(PAGE_OFFSET_RAW)

#ifndef __ASSEMBLY__

extern unsigned long m68k_memoffset;

#ifndef CONFIG_SUN3

#define WANT_PAGE_VIRTUAL

static inline unsigned long ___pa(void *vaddr)
{
	unsigned long paddr;
	asm (
		"1:	addl #0,%0\n"
		m68k_fixup(%c2, 1b+2)
		: "=r" (paddr)
		: "0" (vaddr), "i" (m68k_fixup_memoffset));
	return paddr;
}
#define __pa(vaddr)	___pa((void *)(vaddr))
static inline void *__va(unsigned long paddr)
{
	void *vaddr;
	asm (
		"1:	subl #0,%0\n"
		m68k_fixup(%c2, 1b+2)
		: "=r" (vaddr)
		: "0" (paddr), "i" (m68k_fixup_memoffset));
	return vaddr;
}

#else	/* !CONFIG_SUN3 */
/* This #define is a horrible hack to suppress lots of warnings. --m */
#define __pa(x) ___pa((unsigned long)(x))
static inline unsigned long ___pa(unsigned long x)
{
     if(x == 0)
	  return 0;
     if(x >= PAGE_OFFSET)
        return (x-PAGE_OFFSET);
     else
        return (x+0x2000000);
}

static inline void *__va(unsigned long x)
{
     if(x == 0)
	  return (void *)0;

     if(x < 0x2000000)
        return (void *)(x+PAGE_OFFSET);
     else
        return (void *)(x-0x2000000);
}
#endif	/* CONFIG_SUN3 */

/*
 * NOTE: virtual isn't really correct, actually it should be the offset into the
 * memory node, but we have no highmem, so that works for now.
 * TODO: implement (fast) pfn<->pgdat_idx conversion functions, this makes lots
 * of the shifts unnecessary.
 */
#define virt_to_pfn(kaddr)	(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_virt(pfn)	__va((pfn) << PAGE_SHIFT)

extern int m68k_virt_to_node_shift;

#ifdef CONFIG_SINGLE_MEMORY_CHUNK
#define __virt_to_node(addr)	(&pg_data_map[0])
#else
extern struct pglist_data *pg_data_table[];

static inline __attribute_const__ int __virt_to_node_shift(void)
{
	int shift;

	asm (
		"1:	moveq	#0,%0\n"
		m68k_fixup(%c1, 1b)
		: "=d" (shift)
		: "i" (m68k_fixup_vnode_shift));
	return shift;
}

#define __virt_to_node(addr)	(pg_data_table[(unsigned long)(addr) >> __virt_to_node_shift()])
#endif

#define virt_to_page(addr) ({						\
	pfn_to_page(virt_to_pfn(addr));					\
})
#define page_to_virt(page) ({						\
	pfn_to_virt(page_to_pfn(page));					\
})

#define pfn_to_page(pfn) ({						\
	unsigned long __pfn = (pfn);					\
	struct pglist_data *pgdat;					\
	pgdat = __virt_to_node((unsigned long)pfn_to_virt(__pfn));	\
	pgdat->node_mem_map + (__pfn - pgdat->node_start_pfn);		\
})
#define page_to_pfn(_page) ({						\
	struct page *__p = (_page);					\
	struct pglist_data *pgdat;					\
	pgdat = &pg_data_map[page_to_nid(__p)];				\
	((__p) - pgdat->node_mem_map) + pgdat->node_start_pfn;		\
})

#define virt_addr_valid(kaddr)	((void *)(kaddr) >= (void *)PAGE_OFFSET && (void *)(kaddr) < high_memory)
#define pfn_valid(pfn)		virt_addr_valid(pfn_to_virt(pfn))

#endif /* __ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/page.h>

#endif /* __KERNEL__ */

#endif /* _M68K_PAGE_H */
