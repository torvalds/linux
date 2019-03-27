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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_FASTTRAP_ISA_H
#define	_FASTTRAP_ISA_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This is our reserved trap instruction: ta 0x38
 */
#define	FASTTRAP_INSTR			0x91d02038

#define	FASTTRAP_SUNWDTRACE_SIZE	128

typedef uint32_t	fasttrap_instr_t;

typedef struct fasttrap_machtp {
	fasttrap_instr_t	ftmt_instr;	/* original instruction */
	uintptr_t		ftmt_dest;	/* destination of DCTI */
	uint8_t			ftmt_type;	/* emulation type */
	uint8_t			ftmt_flags;	/* emulation flags */
	uint8_t			ftmt_cc;	/* which cc to look at */
	uint8_t			ftmt_code;	/* branch condition */
} fasttrap_machtp_t;

#define	ftt_instr	ftt_mtp.ftmt_instr
#define	ftt_dest	ftt_mtp.ftmt_dest
#define	ftt_type	ftt_mtp.ftmt_type
#define	ftt_flags	ftt_mtp.ftmt_flags
#define	ftt_cc		ftt_mtp.ftmt_cc
#define	ftt_code	ftt_mtp.ftmt_code

#define	FASTTRAP_T_COMMON	0x00	/* common case -- no emulation */
#define	FASTTRAP_T_CCR		0x01	/* integer condition code branch */
#define	FASTTRAP_T_FCC		0x02	/* floating-point branch */
#define	FASTTRAP_T_REG		0x03	/* register predicated branch */
#define	FASTTRAP_T_ALWAYS	0x04	/* branch always */
#define	FASTTRAP_T_CALL		0x05	/* call instruction */
#define	FASTTRAP_T_JMPL		0x06	/* jmpl instruction */
#define	FASTTRAP_T_RDPC		0x07	/* rdpc instruction */
#define	FASTTRAP_T_RETURN	0x08	/* return instruction */

/*
 * For performance rather than correctness.
 */
#define	FASTTRAP_T_SAVE		0x10	/* save instruction (func entry only) */
#define	FASTTRAP_T_RESTORE	0x11	/* restore instruction */
#define	FASTTRAP_T_OR		0x12	/* mov instruction */
#define	FASTTRAP_T_SETHI	0x13	/* sethi instruction (includes nop) */

#define	FASTTRAP_F_ANNUL	0x01	/* branch is annulled */
#define	FASTTRAP_F_RETMAYBE	0x02	/* not definitely a return site */

#define	FASTTRAP_AFRAMES		3
#define	FASTTRAP_RETURN_AFRAMES		4
#define	FASTTRAP_ENTRY_AFRAMES		3
#define	FASTTRAP_OFFSET_AFRAMES		3


#ifdef	__cplusplus
}
#endif

#endif	/* _FASTTRAP_ISA_H */
