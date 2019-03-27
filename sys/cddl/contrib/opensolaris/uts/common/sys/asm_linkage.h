/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_ASM_LINKAGE_H
#define	_SYS_ASM_LINKAGE_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _ASM	/* The remainder of this file is only for assembly files */

/*
 * make annoying differences in assembler syntax go away
 */

#if defined(__i386__) || defined(__amd64__)

#define	ASM_ENTRY_ALIGN	16

#elif defined(__sparc64__)

/* GCC uses 32-byte function alignment for UltraSPARC CPUs. */
#define	ASM_ENTRY_ALIGN	32

#else

#error Unsupported architecture.

#endif

/*
 * ENTRY provides the standard procedure entry code and an easy way to
 * insert the calls to mcount for profiling. ENTRY_NP is identical, but
 * never calls mcount.
 */
#define	ENTRY(x) \
	.text; \
	.align	ASM_ENTRY_ALIGN; \
	.globl	x; \
	.type	x, @function; \
x:

/*
 * ALTENTRY provides for additional entry points.
 */
#define	ALTENTRY(x) \
	.globl x; \
	.type	x, @function; \
x:

/*
 * SET_SIZE trails a function and set the size for the ELF symbol table.
 */
#define	SET_SIZE(x) \
	.size	x, [.-x]

#endif /* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _IA32_SYS_ASM_LINKAGE_H */
