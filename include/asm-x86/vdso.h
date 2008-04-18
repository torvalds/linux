#ifndef _ASM_X86_VDSO_H
#define _ASM_X86_VDSO_H	1

#ifdef CONFIG_X86_64
extern const char VDSO64_PRELINK[];

/*
 * Given a pointer to the vDSO image, find the pointer to VDSO64_name
 * as that symbol is defined in the vDSO sources or linker script.
 */
#define VDSO64_SYMBOL(base, name)					\
({									\
	extern const char VDSO64_##name[];				\
	(void *)(VDSO64_##name - VDSO64_PRELINK + (unsigned long)(base)); \
})
#endif

#if defined CONFIG_X86_32 || defined CONFIG_COMPAT
extern const char VDSO32_PRELINK[];

/*
 * Given a pointer to the vDSO image, find the pointer to VDSO32_name
 * as that symbol is defined in the vDSO sources or linker script.
 */
#define VDSO32_SYMBOL(base, name)					\
({									\
	extern const char VDSO32_##name[];				\
	(void *)(VDSO32_##name - VDSO32_PRELINK + (unsigned long)(base)); \
})
#endif

/*
 * These symbols are defined with the addresses in the vsyscall page.
 * See vsyscall-sigreturn.S.
 */
extern void __user __kernel_sigreturn;
extern void __user __kernel_rt_sigreturn;

#endif	/* asm-x86/vdso.h */
