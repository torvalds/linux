#ifndef __MMU_H
#define __MMU_H

#include <linux/config.h>
#include <asm/page.h>
#include <asm/const.h>

/*
 * For the 8k pagesize kernel, use only 10 hw context bits to optimize some
 * shifts in the fast tlbmiss handlers, instead of all 13 bits (specifically
 * for vpte offset calculation). For other pagesizes, this optimization in
 * the tlbhandlers can not be done; but still, all 13 bits can not be used
 * because the tlb handlers use "andcc" instruction which sign extends 13
 * bit arguments.
 */
#if PAGE_SHIFT == 13
#define CTX_NR_BITS		10
#else
#define CTX_NR_BITS		12
#endif

#define TAG_CONTEXT_BITS	((_AC(1,UL) << CTX_NR_BITS) - _AC(1,UL))

/* UltraSPARC-III+ and later have a feature whereby you can
 * select what page size the various Data-TLB instances in the
 * chip.  In order to gracefully support this, we put the version
 * field in a spot outside of the areas of the context register
 * where this parameter is specified.
 */
#define CTX_VERSION_SHIFT	22
#define CTX_VERSION_MASK	((~0UL) << CTX_VERSION_SHIFT)

#define CTX_PGSZ_8KB		_AC(0x0,UL)
#define CTX_PGSZ_64KB		_AC(0x1,UL)
#define CTX_PGSZ_512KB		_AC(0x2,UL)
#define CTX_PGSZ_4MB		_AC(0x3,UL)
#define CTX_PGSZ_BITS		_AC(0x7,UL)
#define CTX_PGSZ0_NUC_SHIFT	61
#define CTX_PGSZ1_NUC_SHIFT	58
#define CTX_PGSZ0_SHIFT		16
#define CTX_PGSZ1_SHIFT		19
#define CTX_PGSZ_MASK		((CTX_PGSZ_BITS << CTX_PGSZ0_SHIFT) | \
				 (CTX_PGSZ_BITS << CTX_PGSZ1_SHIFT))

#if defined(CONFIG_SPARC64_PAGE_SIZE_8KB)
#define CTX_PGSZ_BASE	CTX_PGSZ_8KB
#elif defined(CONFIG_SPARC64_PAGE_SIZE_64KB)
#define CTX_PGSZ_BASE	CTX_PGSZ_64KB
#elif defined(CONFIG_SPARC64_PAGE_SIZE_512KB)
#define CTX_PGSZ_BASE	CTX_PGSZ_512KB
#elif defined(CONFIG_SPARC64_PAGE_SIZE_4MB)
#define CTX_PGSZ_BASE	CTX_PGSZ_4MB
#else
#error No page size specified in kernel configuration
#endif

#if defined(CONFIG_HUGETLB_PAGE_SIZE_4MB)
#define CTX_PGSZ_HUGE		CTX_PGSZ_4MB
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512K)
#define CTX_PGSZ_HUGE		CTX_PGSZ_512KB
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
#define CTX_PGSZ_HUGE		CTX_PGSZ_64KB
#endif

#define CTX_PGSZ_KERN	CTX_PGSZ_4MB

/* Thus, when running on UltraSPARC-III+ and later, we use the following
 * PRIMARY_CONTEXT register values for the kernel context.
 */
#define CTX_CHEETAH_PLUS_NUC \
	((CTX_PGSZ_KERN << CTX_PGSZ0_NUC_SHIFT) | \
	 (CTX_PGSZ_BASE << CTX_PGSZ1_NUC_SHIFT))

#define CTX_CHEETAH_PLUS_CTX0 \
	((CTX_PGSZ_KERN << CTX_PGSZ0_SHIFT) | \
	 (CTX_PGSZ_BASE << CTX_PGSZ1_SHIFT))

/* If you want "the TLB context number" use CTX_NR_MASK.  If you
 * want "the bits I program into the context registers" use
 * CTX_HW_MASK.
 */
#define CTX_NR_MASK		TAG_CONTEXT_BITS
#define CTX_HW_MASK		(CTX_NR_MASK | CTX_PGSZ_MASK)

#define CTX_FIRST_VERSION	((_AC(1,UL) << CTX_VERSION_SHIFT) + _AC(1,UL))
#define CTX_VALID(__ctx)	\
	 (!(((__ctx.sparc64_ctx_val) ^ tlb_context_cache) & CTX_VERSION_MASK))
#define CTX_HWBITS(__ctx)	((__ctx.sparc64_ctx_val) & CTX_HW_MASK)
#define CTX_NRBITS(__ctx)	((__ctx.sparc64_ctx_val) & CTX_NR_MASK)

#ifndef __ASSEMBLY__

typedef struct {
	unsigned long	sparc64_ctx_val;
} mm_context_t;

#endif /* !__ASSEMBLY__ */

#endif /* __MMU_H */
