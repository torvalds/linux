#ifndef _LINUX_PKEYS_H
#define _LINUX_PKEYS_H

#include <linux/mm_types.h>
#include <asm/mmu_context.h>

#ifdef CONFIG_ARCH_HAS_PKEYS
#include <asm/pkeys.h>
#else /* ! CONFIG_ARCH_HAS_PKEYS */
#define arch_max_pkey() (1)
#endif /* ! CONFIG_ARCH_HAS_PKEYS */

/*
 * This is called from mprotect_pkey().
 *
 * Returns true if the protection keys is valid.
 */
static inline bool validate_pkey(int pkey)
{
	if (pkey < 0)
		return false;
	return (pkey < arch_max_pkey());
}

#endif /* _LINUX_PKEYS_H */
