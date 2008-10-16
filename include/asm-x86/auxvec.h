#ifndef ASM_X86__AUXVEC_H
#define ASM_X86__AUXVEC_H
/*
 * Architecture-neutral AT_ values in 0-17, leave some room
 * for more of them, start the x86-specific ones at 32.
 */
#ifdef __i386__
#define AT_SYSINFO		32
#endif
#define AT_SYSINFO_EHDR		33

#endif /* ASM_X86__AUXVEC_H */
