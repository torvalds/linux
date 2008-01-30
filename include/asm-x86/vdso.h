#ifndef _ASM_X86_VDSO_H
#define _ASM_X86_VDSO_H	1

extern const char VDSO64_PRELINK[];

/*
 * Given a pointer to the vDSO image, find the pointer to VDSO64_name
 * as that symbol is defined in the vDSO sources or linker script.
 */
#define VDSO64_SYMBOL(base, name) ({		\
	extern const char VDSO64_##name[];	\
	(void *) (VDSO64_##name - VDSO64_PRELINK + (unsigned long) (base)); })

#endif	/* asm-x86/vdso.h */
