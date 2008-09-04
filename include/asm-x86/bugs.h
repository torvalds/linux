#ifndef ASM_X86__BUGS_H
#define ASM_X86__BUGS_H

extern void check_bugs(void);

#ifdef CONFIG_CPU_SUP_INTEL_32
int ppro_with_ram_bug(void);
#else
static inline int ppro_with_ram_bug(void) { return 0; }
#endif

#endif /* ASM_X86__BUGS_H */
