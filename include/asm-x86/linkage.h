#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#ifdef CONFIG_X86_64
#define __ALIGN .p2align 4,,15
#define __ALIGN_STR ".p2align 4,,15"
#endif

#ifdef CONFIG_X86_32
#define asmlinkage CPP_ASMLINKAGE __attribute__((regparm(0)))
#define prevent_tail_call(ret) __asm__ ("" : "=r" (ret) : "0" (ret))
/*
 * For 32-bit UML - mark functions implemented in assembly that use
 * regparm input parameters:
 */
#define asmregparm __attribute__((regparm(3)))
#endif

#ifdef CONFIG_X86_ALIGNMENT_16
#define __ALIGN .align 16,0x90
#define __ALIGN_STR ".align 16,0x90"
#endif

#endif

