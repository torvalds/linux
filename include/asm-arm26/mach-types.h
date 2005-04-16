/*
 * Unlike ARM32 this is NOT automatically generated. DONT delete it
 * Instead, consider FIXME-ing it so its auto-detected.
 */

#ifndef __ASM_ARM_MACH_TYPE_H
#define __ASM_ARM_MACH_TYPE_H

#include <linux/config.h>

#ifndef __ASSEMBLY__
extern unsigned int __machine_arch_type;
#endif

#define MACH_TYPE_ARCHIMEDES           10
#define MACH_TYPE_A5K                  11

#ifdef CONFIG_ARCH_ARC
# define machine_arch_type		MACH_TYPE_ARCHIMEDES
# define machine_is_archimedes()	(machine_arch_type == MACH_TYPE_ARCHIMEDES)
#else
# define machine_is_archimedes()	(0)
#endif

#ifdef CONFIG_ARCH_A5K
# define machine_arch_type		MACH_TYPE_A5K
# define machine_is_a5k()		(machine_arch_type == MACH_TYPE_A5K)
#else
# define machine_is_a5k()	(0)
#endif

#ifndef machine_arch_type
#error Unknown machine type
#define machine_arch_type       __machine_arch_type
#endif

#endif
