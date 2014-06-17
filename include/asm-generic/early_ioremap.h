#ifndef _ASM_EARLY_IOREMAP_H_
#define _ASM_EARLY_IOREMAP_H_

#include <linux/types.h>

/*
 * early_ioremap() and early_iounmap() are for temporary early boot-time
 * mappings, before the real ioremap() is functional.
 */
extern void __iomem *early_ioremap(resource_size_t phys_addr,
				   unsigned long size);
extern void *early_memremap(resource_size_t phys_addr,
			    unsigned long size);
extern void early_iounmap(void __iomem *addr, unsigned long size);
extern void early_memunmap(void *addr, unsigned long size);

/*
 * Weak function called by early_ioremap_reset(). It does nothing, but
 * architectures may provide their own version to do any needed cleanups.
 */
extern void early_ioremap_shutdown(void);

#if defined(CONFIG_GENERIC_EARLY_IOREMAP) && defined(CONFIG_MMU)
/* Arch-specific initialization */
extern void early_ioremap_init(void);

/* Generic initialization called by architecture code */
extern void early_ioremap_setup(void);

/*
 * Called as last step in paging_init() so library can act
 * accordingly for subsequent map/unmap requests.
 */
extern void early_ioremap_reset(void);

#else
static inline void early_ioremap_init(void) { }
static inline void early_ioremap_setup(void) { }
static inline void early_ioremap_reset(void) { }
#endif

#endif /* _ASM_EARLY_IOREMAP_H_ */
