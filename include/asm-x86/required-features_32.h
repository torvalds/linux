#ifndef _ASM_REQUIRED_FEATURES_H
#define _ASM_REQUIRED_FEATURES_H 1

/* Define minimum CPUID feature set for kernel These bits are checked
   really early to actually display a visible error message before the
   kernel dies.  Make sure to assign features to the proper mask!

   Some requirements that are not in CPUID yet are also in the
   CONFIG_X86_MINIMUM_CPU_FAMILY which is checked too.

   The real information is in arch/i386/Kconfig.cpu, this just converts
   the CONFIGs into a bitmask */

#ifndef CONFIG_MATH_EMULATION
# define NEED_FPU	(1<<(X86_FEATURE_FPU & 31))
#else
# define NEED_FPU	0
#endif

#ifdef CONFIG_X86_PAE
# define NEED_PAE	(1<<(X86_FEATURE_PAE & 31))
#else
# define NEED_PAE	0
#endif

#ifdef CONFIG_X86_CMOV
# define NEED_CMOV	(1<<(X86_FEATURE_CMOV & 31))
#else
# define NEED_CMOV	0
#endif

#ifdef CONFIG_X86_PAE
# define NEED_CX8	(1<<(X86_FEATURE_CX8 & 31))
#else
# define NEED_CX8	0
#endif

#define REQUIRED_MASK0	(NEED_FPU|NEED_PAE|NEED_CMOV|NEED_CX8)

#ifdef CONFIG_X86_USE_3DNOW
# define NEED_3DNOW	(1<<(X86_FEATURE_3DNOW & 31))
#else
# define NEED_3DNOW	0
#endif

#define REQUIRED_MASK1	(NEED_3DNOW)

#define REQUIRED_MASK2	0
#define REQUIRED_MASK3	0
#define REQUIRED_MASK4	0
#define REQUIRED_MASK5	0
#define REQUIRED_MASK6	0
#define REQUIRED_MASK7	0

#endif
