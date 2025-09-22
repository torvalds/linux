/*	$OpenBSD: vectors_88110.S,v 1.9 2014/06/09 15:00:57 miod Exp $	*/
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
	VECTOR(m88110_unknown_handler); VECTOR(m88110_unknown_handler); \
	VECTOR(m88110_unknown_handler); VECTOR(m88110_unknown_handler); \
	VECTOR(m88110_unknown_handler); VECTOR(m88110_unknown_handler); \
	VECTOR(m88110_unknown_handler); VECTOR(m88110_unknown_handler); \
	VECTOR(m88110_unknown_handler); VECTOR(m88110_unknown_handler); \
	VECTOR(m88110_unknown_handler); VECTOR(m88110_unknown_handler); \
	VECTOR(m88110_unknown_handler); VECTOR(m88110_unknown_handler); \
	VECTOR(m88110_unknown_handler); VECTOR(m88110_unknown_handler);

	.data
GLOBAL(m88110_vector_list)
	VECTOR(m88110_reset_handler)		/* 00 */
	VECTOR(m88110_interrupt_handler)	/* 01 */
	VECTOR(m88110_instruction_access_handler)	/* 02 */
	VECTOR(m88110_data_exception_handler)	/* 03 */
	VECTOR(m88110_misaligned_handler)	/* 04 */
	VECTOR(m88110_unimplemented_handler)	/* 05 */
	VECTOR(m88110_privilege_handler)	/* 06 */
	VECTOR(m88110_bounds_handler)		/* 07 */
	VECTOR(m88110_divide_handler)		/* 08 */
	VECTOR(m88110_overflow_handler)		/* 09 */
	VECTOR(m88110_error_handler)		/* 0a */
	VECTOR(m88110_nonmaskable)		/* 0b */
	VECTOR(m88110_data_read_miss)		/* 0c */
	VECTOR(m88110_data_write_miss)		/* 0d */
	VECTOR(m88110_inst_atc_miss)		/* 0e */
	VECTOR(m88110_unknown_handler)		/* 0f */
	UNKNOWN16				/* 1x */
	UNKNOWN16				/* 2x */
	UNKNOWN16				/* 3x */
	UNKNOWN16				/* 4x */
	UNKNOWN16				/* 5x */
	UNKNOWN16				/* 6x */
	VECTOR(m88110_unknown_handler)		/* 70 */
	VECTOR(m88110_unknown_handler)		/* 71 */
	VECTOR(m88110_fpu_handler)		/* 72 */
	VECTOR(m88110_unknown_handler)		/* 73 */
	VECTOR(m88110_unimplemented_handler)	/* 74 */
	VECTOR(m88110_unknown_handler)		/* 75 */
	VECTOR(m88110_unimplemented_handler)	/* 76 */
	VECTOR(m88110_unknown_handler)		/* 77 */
	VECTOR(m88110_unimplemented_handler)	/* 78 */
	VECTOR(m88110_unknown_handler)		/* 79 */
	VECTOR(m88110_unimplemented_handler)	/* 7a */
	VECTOR(m88110_unknown_handler)		/* 7b */
	VECTOR(m88110_unimplemented_handler)	/* 7c */
	VECTOR(m88110_unknown_handler)		/* 7d */
	VECTOR(m88110_unimplemented_handler)	/* 7e */
	VECTOR(m88110_unknown_handler)		/* 7f */
	VECTOR(m88110_sigsys)			/* 80 */
	VECTOR(m88110_sigsys)			/* 81 */
#ifdef DDB
	VECTOR(m88110_break)			/* 82 */
	VECTOR(m88110_trace)			/* 83 */
	VECTOR(m88110_entry)			/* 84 */
#endif
	.word	0
	VECTOR(m88110_data_exception_handler_bootstrap)
