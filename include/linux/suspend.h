#ifndef _LINUX_SWSUSP_H
#define _LINUX_SWSUSP_H

#if defined(CONFIG_X86) || defined(CONFIG_FRV) || defined(CONFIG_PPC32)
#include <asm/suspend.h>
#endif
#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/pm.h>

/* page backup entry */
typedef struct pbe {
	unsigned long address;		/* address of the copy */
	unsigned long orig_address;	/* original address of page */
	swp_entry_t swap_address;	

	struct pbe *next;	/* also used as scratch space at
				 * end of page (see link, diskpage)
				 */
} suspend_pagedir_t;

#define for_each_pbe(pbe, pblist) \
	for (pbe = pblist ; pbe ; pbe = pbe->next)

#define PBES_PER_PAGE      (PAGE_SIZE/sizeof(struct pbe))
#define PB_PAGE_SKIP       (PBES_PER_PAGE-1)

#define for_each_pb_page(pbe, pblist) \
	for (pbe = pblist ; pbe ; pbe = (pbe+PB_PAGE_SKIP)->next)


#define SWAP_FILENAME_MAXLENGTH	32


extern dev_t swsusp_resume_device;
   
/* mm/vmscan.c */
extern int shrink_mem(void);

/* mm/page_alloc.c */
extern void drain_local_pages(void);
extern void mark_free_pages(struct zone *zone);

#ifdef CONFIG_PM
/* kernel/power/swsusp.c */
extern int software_suspend(void);

extern int pm_prepare_console(void);
extern void pm_restore_console(void);

#else
static inline int software_suspend(void)
{
	printk("Warning: fake suspend called\n");
	return -EPERM;
}
#endif

#ifdef CONFIG_SUSPEND_SMP
extern void disable_nonboot_cpus(void);
extern void enable_nonboot_cpus(void);
#else
static inline void disable_nonboot_cpus(void) {}
static inline void enable_nonboot_cpus(void) {}
#endif

void save_processor_state(void);
void restore_processor_state(void);
struct saved_context;
void __save_processor_state(struct saved_context *ctxt);
void __restore_processor_state(struct saved_context *ctxt);
unsigned long get_safe_page(gfp_t gfp_mask);

/*
 * XXX: We try to keep some more pages free so that I/O operations succeed
 * without paging. Might this be more?
 */
#define PAGES_FOR_IO	512

#endif /* _LINUX_SWSUSP_H */
