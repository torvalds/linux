#ifndef _ASM_X86_DISABLED_FEATURES_H
#define _ASM_X86_DISABLED_FEATURES_H

/* These features, although they might be available in a CPU
 * will not be used because the compile options to support
 * them are not present.
 *
 * This code allows them to be checked and disabled at
 * compile time without an explicit #ifdef.  Use
 * cpu_feature_enabled().
 */

#ifdef CONFIG_X86_INTEL_MPX
# define DISABLE_MPX	0
#else
# define DISABLE_MPX	(1<<(X86_FEATURE_MPX & 31))
#endif

#ifdef CONFIG_X86_SMAP
# define DISABLE_SMAP	0
#else
# define DISABLE_SMAP	(1<<(X86_FEATURE_SMAP & 31))
#endif

#ifdef CONFIG_X86_INTEL_UMIP
# define DISABLE_UMIP	0
#else
# define DISABLE_UMIP	(1<<(X86_FEATURE_UMIP & 31))
#endif

#ifdef CONFIG_X86_64
# define DISABLE_VME		(1<<(X86_FEATURE_VME & 31))
# define DISABLE_K6_MTRR	(1<<(X86_FEATURE_K6_MTRR & 31))
# define DISABLE_CYRIX_ARR	(1<<(X86_FEATURE_CYRIX_ARR & 31))
# define DISABLE_CENTAUR_MCR	(1<<(X86_FEATURE_CENTAUR_MCR & 31))
# define DISABLE_PCID		0
#else
# define DISABLE_VME		0
# define DISABLE_K6_MTRR	0
# define DISABLE_CYRIX_ARR	0
# define DISABLE_CENTAUR_MCR	0
# define DISABLE_PCID		(1<<(X86_FEATURE_PCID & 31))
#endif /* CONFIG_X86_64 */

#ifdef CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
# define DISABLE_PKU		0
# define DISABLE_OSPKE		0
#else
# define DISABLE_PKU		(1<<(X86_FEATURE_PKU & 31))
# define DISABLE_OSPKE		(1<<(X86_FEATURE_OSPKE & 31))
#endif /* CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS */

#ifdef CONFIG_X86_5LEVEL
# define DISABLE_LA57	0
#else
# define DISABLE_LA57	(1<<(X86_FEATURE_LA57 & 31))
#endif

#ifdef CONFIG_PAGE_TABLE_ISOLATION
# define DISABLE_PTI		0
#else
# define DISABLE_PTI		(1 << (X86_FEATURE_PTI & 31))
#endif

/*
 * Make sure to add features to the correct mask
 */
#define DISABLED_MASK0	(DISABLE_VME)
#define DISABLED_MASK1	0
#define DISABLED_MASK2	0
#define DISABLED_MASK3	(DISABLE_CYRIX_ARR|DISABLE_CENTAUR_MCR|DISABLE_K6_MTRR)
#define DISABLED_MASK4	(DISABLE_PCID)
#define DISABLED_MASK5	0
#define DISABLED_MASK6	0
#define DISABLED_MASK7	(DISABLE_PTI)
#define DISABLED_MASK8	0
#define DISABLED_MASK9	(DISABLE_MPX|DISABLE_SMAP)
#define DISABLED_MASK10	0
#define DISABLED_MASK11	0
#define DISABLED_MASK12	0
#define DISABLED_MASK13	0
#define DISABLED_MASK14	0
#define DISABLED_MASK15	0
#define DISABLED_MASK16	(DISABLE_PKU|DISABLE_OSPKE|DISABLE_LA57|DISABLE_UMIP)
#define DISABLED_MASK17	0
#define DISABLED_MASK18	0
#define DISABLED_MASK_CHECK BUILD_BUG_ON_ZERO(NCAPINTS != 19)

#endif /* _ASM_X86_DISABLED_FEATURES_H */
