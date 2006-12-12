/* 
 * linux/include/asm-arm26/namei.h
 *
 * Routines to handle famous /usr/gnemul
 * Derived from the Sparc version of this file
 *
 * Included from linux/fs/namei.c
 */

#ifndef __ASMARM_NAMEI_H
#define __ASMARM_NAMEI_H

#define ARM_BSD_EMUL "usr/gnemul/bsd/"

static inline char *__emul_prefix(void)
{
	switch (current->personality) {
	case PER_BSD:
		return ARM_BSD_EMUL;
	default:
		return NULL;
	}
}

#endif /* __ASMARM_NAMEI_H */
