/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_POWERPC_ERRNO_H
#define _ASM_POWERPC_ERRNO_H

#include <asm-generic/errno.h>

#undef	EDEADLOCK
#define	EDEADLOCK	58	/* File locking deadlock error */

#endif	/* _ASM_POWERPC_ERRNO_H */
