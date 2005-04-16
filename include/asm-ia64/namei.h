#ifndef _ASM_IA64_NAMEI_H
#define _ASM_IA64_NAMEI_H

/*
 * Modified 1998, 1999, 2001
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 */

#include <asm/ptrace.h>
#include <asm/system.h>

#define EMUL_PREFIX_LINUX_IA32 "/emul/ia32-linux/"

static inline char *
__emul_prefix (void)
{
	switch (current->personality) {
	      case PER_LINUX32:
		return EMUL_PREFIX_LINUX_IA32;
	      default:
		return NULL;
	}
}

#endif /* _ASM_IA64_NAMEI_H */
