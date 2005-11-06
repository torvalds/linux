#ifndef _ASM_POWERPC_ERRNO_H
#define _ASM_POWERPC_ERRNO_H

#include <asm-generic/errno.h>

#undef	EDEADLOCK
#define	EDEADLOCK	58	/* File locking deadlock error */

#define _LAST_ERRNO	516

#endif	/* _ASM_POWERPC_ERRNO_H */
