/*
 * Define generic no-op hooks for arch_dup_mmap and arch_exit_mmap, to
 * be included in asm-FOO/mmu_context.h for any arch FOO which doesn't
 * need to hook these.
 */
#ifndef _ASM_GENERIC_MM_HOOKS_H
#define _ASM_GENERIC_MM_HOOKS_H

static inline void arch_dup_mmap(struct mm_struct *oldmm,
				 struct mm_struct *mm)
{
}

static inline void arch_exit_mmap(struct mm_struct *mm)
{
}

#endif	/* _ASM_GENERIC_MM_HOOKS_H */
