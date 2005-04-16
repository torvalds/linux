#ifndef _ALPHA_TLBFLUSH_H
#define _ALPHA_TLBFLUSH_H

#include <linux/config.h>
#include <linux/mm.h>
#include <asm/compiler.h>

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __MMU_EXTERN_INLINE
#endif

extern void __load_new_mm_context(struct mm_struct *);


/* Use a few helper functions to hide the ugly broken ASN
   numbers on early Alphas (ev4 and ev45).  */

__EXTERN_INLINE void
ev4_flush_tlb_current(struct mm_struct *mm)
{
	__load_new_mm_context(mm);
	tbiap();
}

__EXTERN_INLINE void
ev5_flush_tlb_current(struct mm_struct *mm)
{
	__load_new_mm_context(mm);
}

/* Flush just one page in the current TLB set.  We need to be very
   careful about the icache here, there is no way to invalidate a
   specific icache page.  */

__EXTERN_INLINE void
ev4_flush_tlb_current_page(struct mm_struct * mm,
			   struct vm_area_struct *vma,
			   unsigned long addr)
{
	int tbi_flag = 2;
	if (vma->vm_flags & VM_EXEC) {
		__load_new_mm_context(mm);
		tbi_flag = 3;
	}
	tbi(tbi_flag, addr);
}

__EXTERN_INLINE void
ev5_flush_tlb_current_page(struct mm_struct * mm,
			   struct vm_area_struct *vma,
			   unsigned long addr)
{
	if (vma->vm_flags & VM_EXEC)
		__load_new_mm_context(mm);
	else
		tbi(2, addr);
}


#ifdef CONFIG_ALPHA_GENERIC
# define flush_tlb_current		alpha_mv.mv_flush_tlb_current
# define flush_tlb_current_page		alpha_mv.mv_flush_tlb_current_page
#else
# ifdef CONFIG_ALPHA_EV4
#  define flush_tlb_current		ev4_flush_tlb_current
#  define flush_tlb_current_page	ev4_flush_tlb_current_page
# else
#  define flush_tlb_current		ev5_flush_tlb_current
#  define flush_tlb_current_page	ev5_flush_tlb_current_page
# endif
#endif

#ifdef __MMU_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __MMU_EXTERN_INLINE
#endif

/* Flush current user mapping.  */
static inline void
flush_tlb(void)
{
	flush_tlb_current(current->active_mm);
}

/* Flush someone else's user mapping.  */
static inline void
flush_tlb_other(struct mm_struct *mm)
{
	unsigned long *mmc = &mm->context[smp_processor_id()];
	/* Check it's not zero first to avoid cacheline ping pong
	   when possible.  */
	if (*mmc) *mmc = 0;
}

/* Flush a specified range of user mapping page tables from TLB.
   Although Alpha uses VPTE caches, this can be a nop, as Alpha does
   not have finegrained tlb flushing, so it will flush VPTE stuff
   during next flush_tlb_range.  */

static inline void
flush_tlb_pgtables(struct mm_struct *mm, unsigned long start,
		   unsigned long end)
{
}

#ifndef CONFIG_SMP
/* Flush everything (kernel mapping may also have changed
   due to vmalloc/vfree).  */
static inline void flush_tlb_all(void)
{
	tbia();
}

/* Flush a specified user mapping.  */
static inline void
flush_tlb_mm(struct mm_struct *mm)
{
	if (mm == current->active_mm)
		flush_tlb_current(mm);
	else
		flush_tlb_other(mm);
}

/* Page-granular tlb flush.  */
static inline void
flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm == current->active_mm)
		flush_tlb_current_page(mm, vma, addr);
	else
		flush_tlb_other(mm);
}

/* Flush a specified range of user mapping.  On the Alpha we flush
   the whole user tlb.  */
static inline void
flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		unsigned long end)
{
	flush_tlb_mm(vma->vm_mm);
}

#else /* CONFIG_SMP */

extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *);
extern void flush_tlb_page(struct vm_area_struct *, unsigned long);
extern void flush_tlb_range(struct vm_area_struct *, unsigned long,
			    unsigned long);

#endif /* CONFIG_SMP */

#define flush_tlb_kernel_range(start, end) flush_tlb_all()

#endif /* _ALPHA_TLBFLUSH_H */
