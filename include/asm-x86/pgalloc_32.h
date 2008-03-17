#ifndef _I386_PGALLOC_H
#define _I386_PGALLOC_H

#ifdef CONFIG_X86_PAE
extern void pud_populate(struct mm_struct *mm, pud_t *pudp, pmd_t *pmd);

#endif	/* CONFIG_X86_PAE */

#endif /* _I386_PGALLOC_H */
