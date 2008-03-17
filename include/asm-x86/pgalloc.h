#ifndef _ASM_X86_PGALLOC_H
#define _ASM_X86_PGALLOC_H

#include <linux/threads.h>
#include <linux/mm.h>		/* for struct page */
#include <linux/pagemap.h>

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define paravirt_alloc_pt(mm, pfn) do { } while (0)
#define paravirt_alloc_pd(mm, pfn) do { } while (0)
#define paravirt_alloc_pd_clone(pfn, clonepfn, start, count) do { } while (0)
#define paravirt_release_pt(pfn) do { } while (0)
#define paravirt_release_pd(pfn) do { } while (0)
#endif

/*
 * Allocate and free page tables.
 */
extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

extern pte_t *pte_alloc_one_kernel(struct mm_struct *, unsigned long);
extern pgtable_t pte_alloc_one(struct mm_struct *, unsigned long);

#ifdef CONFIG_X86_32
# include "pgalloc_32.h"
#else
# include "pgalloc_64.h"
#endif

#endif	/* _ASM_X86_PGALLOC_H */
