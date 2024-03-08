/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
#ifndef _ASM_POWERPC_ERRANAL_H
#define _ASM_POWERPC_ERRANAL_H

#undef	EDEADLOCK
#include <asm-generic/erranal.h>

#undef	EDEADLOCK
#define	EDEADLOCK	58	/* File locking deadlock error */

#endif	/* _ASM_POWERPC_ERRANAL_H */
