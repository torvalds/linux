#ifndef _LINUX_SWSUSP_H
#define _LINUX_SWSUSP_H

#if defined(CONFIG_X86) || defined(CONFIG_FRV) || defined(CONFIG_PPC32)
#include <asm/suspend.h>
#endif
#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/mm.h>

/* struct pbe is used for creating lists of pages that should be restored
 * atomically during the resume from disk, because the page frames they have
 * occupied before the suspend are in use.
 */
struct pbe {
	void *address;		/* address of the copy */
	void *orig_address;	/* original address of a page */
	struct pbe *next;
};

/* mm/page_alloc.c */
extern void drain_local_pages(void);
extern void mark_free_pages(struct zone *zone);

#if defined(CONFIG_PM) && defined(CONFIG_VT) && defined(CONFIG_VT_CONSOLE)
extern int pm_prepare_console(void);
extern void pm_restore_console(void);
#else
static inline int pm_prepare_console(void) { return 0; }
static inline void pm_restore_console(void) {}
#endif

#if defined(CONFIG_PM) && defined(CONFIG_SOFTWARE_SUSPEND)
/* kernel/power/snapshot.c */
extern void __init register_nosave_region(unsigned long, unsigned long);
extern int swsusp_page_is_forbidden(struct page *);
extern void swsusp_set_page_free(struct page *);
extern void swsusp_unset_page_free(struct page *);
extern unsigned long get_safe_page(gfp_t gfp_mask);
#else
static inline void register_nosave_region(unsigned long b, unsigned long e) {}
static inline int swsusp_page_is_forbidden(struct page *p) { return 0; }
static inline void swsusp_set_page_free(struct page *p) {}
static inline void swsusp_unset_page_free(struct page *p) {}
#endif /* defined(CONFIG_PM) && defined(CONFIG_SOFTWARE_SUSPEND) */

void save_processor_state(void);
void restore_processor_state(void);
struct saved_context;
void __save_processor_state(struct saved_context *ctxt);
void __restore_processor_state(struct saved_context *ctxt);

/*
 * XXX: We try to keep some more pages free so that I/O operations succeed
 * without paging. Might this be more?
 */
#define PAGES_FOR_IO	1024

#endif /* _LINUX_SWSUSP_H */
