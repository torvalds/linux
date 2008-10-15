#ifndef ASM_X86__TRAMPOLINE_H
#define ASM_X86__TRAMPOLINE_H

#ifndef __ASSEMBLY__

/*
 * Trampoline 80x86 program as an array.
 */
extern const unsigned char trampoline_data [];
extern const unsigned char trampoline_end  [];
extern unsigned char *trampoline_base;

extern unsigned long init_rsp;
extern unsigned long initial_code;

#define TRAMPOLINE_BASE 0x6000
extern unsigned long setup_trampoline(void);

#endif /* __ASSEMBLY__ */

#endif /* ASM_X86__TRAMPOLINE_H */
