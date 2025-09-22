/*	$OpenBSD: vectors_88100.S,v 1.7 2014/06/09 15:00:57 miod Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1991, 1992 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <machine/asm.h>

#define	UNKNOWN16	\
	VECTOR(unknown_handler); VECTOR(unknown_handler); \
	VECTOR(unknown_handler); VECTOR(unknown_handler); \
	VECTOR(unknown_handler); VECTOR(unknown_handler); \
	VECTOR(unknown_handler); VECTOR(unknown_handler); \
	VECTOR(unknown_handler); VECTOR(unknown_handler); \
	VECTOR(unknown_handler); VECTOR(unknown_handler); \
	VECTOR(unknown_handler); VECTOR(unknown_handler); \
	VECTOR(unknown_handler); VECTOR(unknown_handler);

	.data
GLOBAL(vector_list)
	VECTOR(reset_handler)			/* 00 */
	VECTOR(interrupt_handler)		/* 01 */
	VECTOR(instruction_access_handler)	/* 02 */
	VECTOR(data_exception_handler)		/* 03 */
	VECTOR(misaligned_handler)		/* 04 */
	VECTOR(unimplemented_handler)		/* 05 */
	VECTOR(privilege_handler)		/* 06 */
	VECTOR(bounds_handler)			/* 07 */
	VECTOR(divide_handler)			/* 08 */
	VECTOR(overflow_handler)		/* 09 */
	VECTOR(error_handler)			/* 0a */
	VECTOR(unknown_handler)			/* 0b */
	VECTOR(unknown_handler)			/* 0c */
	VECTOR(unknown_handler)			/* 0d */
	VECTOR(unknown_handler)			/* 0e */
	VECTOR(unknown_handler)			/* 0f */
	UNKNOWN16				/* 1x */
	UNKNOWN16				/* 2x */
	UNKNOWN16				/* 3x */
	UNKNOWN16				/* 4x */
	UNKNOWN16				/* 5x */
	UNKNOWN16				/* 6x */
	VECTOR(unknown_handler)			/* 70 */
	VECTOR(unknown_handler)			/* 71 */
	VECTOR(fp_precise_handler)		/* 72 */
	VECTOR(fp_imprecise_handler)		/* 73 */
	VECTOR(unimplemented_handler)		/* 74 */
	VECTOR(unknown_handler)			/* 75 */
	VECTOR(unimplemented_handler)		/* 76 */
	VECTOR(unknown_handler)			/* 77 */
	VECTOR(unimplemented_handler)		/* 78 */
	VECTOR(unknown_handler)			/* 79 */
	VECTOR(unimplemented_handler)		/* 7a */
	VECTOR(unknown_handler)			/* 7b */
	VECTOR(unimplemented_handler)		/* 7c */
	VECTOR(unknown_handler)			/* 7d */
	VECTOR(unimplemented_handler)		/* 7e */
	VECTOR(unknown_handler)			/* 7f */
	VECTOR(sigsys)				/* 80 */
	VECTOR(sigsys)				/* 81 */
#ifdef DDB
	VECTOR(break)				/* 82 */
	VECTOR(trace)				/* 83 */
	VECTOR(entry)				/* 84 */
#endif
	.word	0
	VECTOR(data_exception_handler_bootstrap)

