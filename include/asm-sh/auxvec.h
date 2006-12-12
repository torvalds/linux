#ifndef __ASM_SH_AUXVEC_H
#define __ASM_SH_AUXVEC_H

/*
 * Architecture-neutral AT_ values in 0-17, leave some room
 * for more of them.
 */

#ifdef CONFIG_VSYSCALL
/*
 * Only define this in the vsyscall case, the entry point to
 * the vsyscall page gets placed here. The kernel will attempt
 * to build a gate VMA we don't care about otherwise..
 */
#define AT_SYSINFO_EHDR		33
#endif

#endif /* __ASM_SH_AUXVEC_H */
