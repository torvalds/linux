#ifndef _ASM_REQUIRED_FEATURES_H
#define _ASM_REQUIRED_FEATURES_H 1

/* Define minimum CPUID feature set for kernel These bits are checked
   really early to actually display a visible error message before the
   kernel dies.  Only add word 0 bits here

   Some requirements that are not in CPUID yet are also in the
   CONFIG_X86_MINIMUM_CPU mode which is checked too.

   The real information is in arch/i386/Kconfig.cpu, this just converts
   the CONFIGs into a bitmask */

#ifdef CONFIG_X86_PAE
#define NEED_PAE	(1<<X86_FEATURE_PAE)
#else
#define NEED_PAE	0
#endif

#ifdef CONFIG_X86_CMOV
#define NEED_CMOV	(1<<X86_FEATURE_CMOV)
#else
#define NEED_CMOV	0
#endif

#ifdef CONFIG_X86_CMPXCHG64
#define NEED_CMPXCHG64  (1<<X86_FEATURE_CX8)
#else
#define NEED_CMPXCHG64  0
#endif

#define REQUIRED_MASK1	(NEED_PAE|NEED_CMOV|NEED_CMPXCHG64)

#endif
