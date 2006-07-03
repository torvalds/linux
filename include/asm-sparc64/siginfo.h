#ifndef _SPARC64_SIGINFO_H
#define _SPARC64_SIGINFO_H

#define SI_PAD_SIZE32	((SI_MAX_SIZE/sizeof(int)) - 3)

#define __ARCH_SI_PREAMBLE_SIZE	(4 * sizeof(int))
#define __ARCH_SI_TRAPNO
#define __ARCH_SI_BAND_T int

#include <asm-generic/siginfo.h>

#ifdef __KERNEL__

#include <linux/compat.h>

#ifdef CONFIG_COMPAT

struct compat_siginfo;

#endif /* CONFIG_COMPAT */

#endif /* __KERNEL__ */

#define SI_NOINFO	32767		/* no information in siginfo_t */

/*
 * SIGEMT si_codes
 */
#define EMT_TAGOVF	(__SI_FAULT|1)	/* tag overflow */
#define NSIGEMT		1

#endif
