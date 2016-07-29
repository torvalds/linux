#ifndef _LINUX_PKEYS_H
#define _LINUX_PKEYS_H

#include <linux/mm_types.h>
#include <asm/mmu_context.h>

#define PKEY_DISABLE_ACCESS	0x1
#define PKEY_DISABLE_WRITE	0x2
#define PKEY_ACCESS_MASK	(PKEY_DISABLE_ACCESS |\
				 PKEY_DISABLE_WRITE)

#ifdef CONFIG_ARCH_HAS_PKEYS
#include <asm/pkeys.h>
#else /* ! CONFIG_ARCH_HAS_PKEYS */
#define arch_max_pkey() (1)
#define execute_only_pkey(mm) (0)
#define arch_override_mprotect_pkey(vma, prot, pkey) (0)
#define PKEY_DEDICATED_EXECUTE_ONLY 0
#endif /* ! CONFIG_ARCH_HAS_PKEYS */

#endif /* _LINUX_PKEYS_H */
