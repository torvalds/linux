#ifndef _SPARC_CACHEFLUSH_H
#define _SPARC_CACHEFLUSH_H

#include <linux/mm.h>		/* Common for other includes */
// #include <linux/kernel.h> from pgalloc.h
// #include <linux/sched.h>  from pgalloc.h

// #include <asm/page.h>
#include <asm/btfixup.h>

/*
 * Fine grained cache flushing.
 */
#ifdef CONFIG_SMP

BTFIXUPDEF_CALL(void, local_flush_cache_all, void)
BTFIXUPDEF_CALL(void, local_flush_cache_mm, struct mm_struct *)
BTFIXUPDEF_CALL(void, local_flush_cache_range, struct vm_area_struct *, unsigned long, unsigned long)
BTFIXUPDEF_CALL(void, local_flush_cache_page, struct vm_area_struct *, unsigned long)

#define local_flush_cache_all() BTFIXUP_CALL(local_flush_cache_all)()
#define local_flush_cache_mm(mm) BTFIXUP_CALL(local_flush_cache_mm)(mm)
#define local_flush_cache_range(vma,start,end) BTFIXUP_CALL(local_flush_cache_range)(vma,start,end)
#define local_flush_cache_page(vma,addr) BTFIXUP_CALL(local_flush_cache_page)(vma,addr)

BTFIXUPDEF_CALL(void, local_flush_page_to_ram, unsigned long)
BTFIXUPDEF_CALL(void, local_flush_sig_insns, struct mm_struct *, unsigned long)

#define local_flush_page_to_ram(addr) BTFIXUP_CALL(local_flush_page_to_ram)(addr)
#define local_flush_sig_insns(mm,insn_addr) BTFIXUP_CALL(local_flush_sig_insns)(mm,insn_addr)

extern void smp_flush_cache_all(void);
extern void smp_flush_cache_mm(struct mm_struct *mm);
extern void smp_flush_cache_range(struct vm_area_struct *vma,
				  unsigned long start,
				  unsigned long end);
extern void smp_flush_cache_page(struct vm_area_struct *vma, unsigned long page);

extern void smp_flush_page_to_ram(unsigned long page);
extern void smp_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr);

#endif /* CONFIG_SMP */

BTFIXUPDEF_CALL(void, flush_cache_all, void)
BTFIXUPDEF_CALL(void, flush_cache_mm, struct mm_struct *)
BTFIXUPDEF_CALL(void, flush_cache_range, struct vm_area_struct *, unsigned long, unsigned long)
BTFIXUPDEF_CALL(void, flush_cache_page, struct vm_area_struct *, unsigned long)

#define flush_cache_all() BTFIXUP_CALL(flush_cache_all)()
#define flush_cache_mm(mm) BTFIXUP_CALL(flush_cache_mm)(mm)
#define flush_cache_dup_mm(mm) BTFIXUP_CALL(flush_cache_mm)(mm)
#define flush_cache_range(vma,start,end) BTFIXUP_CALL(flush_cache_range)(vma,start,end)
#define flush_cache_page(vma,addr,pfn) BTFIXUP_CALL(flush_cache_page)(vma,addr)
#define flush_icache_range(start, end)		do { } while (0)
#define flush_icache_page(vma, pg)		do { } while (0)

#define flush_icache_user_range(vma,pg,adr,len)	do { } while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	do {							\
		flush_cache_page(vma, vaddr, page_to_pfn(page));\
		memcpy(dst, src, len);				\
	} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	do {							\
		flush_cache_page(vma, vaddr, page_to_pfn(page));\
		memcpy(dst, src, len);				\
	} while (0)

BTFIXUPDEF_CALL(void, __flush_page_to_ram, unsigned long)
BTFIXUPDEF_CALL(void, flush_sig_insns, struct mm_struct *, unsigned long)

#define __flush_page_to_ram(addr) BTFIXUP_CALL(__flush_page_to_ram)(addr)
#define flush_sig_insns(mm,insn_addr) BTFIXUP_CALL(flush_sig_insns)(mm,insn_addr)

extern void sparc_flush_page_to_ram(struct page *page);

#define flush_dcache_page(page)			sparc_flush_page_to_ram(page)
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vunmap(start, end)		flush_cache_all()

#endif /* _SPARC_CACHEFLUSH_H */
