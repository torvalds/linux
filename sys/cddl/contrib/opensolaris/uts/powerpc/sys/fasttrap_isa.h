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
/* Portions Copyright 2013 Justin Hibbits */
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

#define	FASTTRAP_SUNWDTRACE_SIZE	64
#define FASTTRAP_INSTR			0x0FFFDDDD

typedef	uint32_t	fasttrap_instr_t;

typedef struct fasttrap_machtp_t {
	fasttrap_instr_t	ftmt_instr;	/* original instruction */
	uintptr_t		ftmt_dest;	/* branch target */
	uint8_t			ftmt_type;	/* emulation type */
	uint8_t			ftmt_flags;	/* emulation flags */
	uint8_t			ftmt_bo;	/* BO field */
	uint8_t			ftmt_bi;	/* BI field (CR bit) */
} fasttrap_machtp_t;

#define	ftt_instr	ftt_mtp.ftmt_instr
#define	ftt_dest	ftt_mtp.ftmt_dest
#define	ftt_type	ftt_mtp.ftmt_type
#define	ftt_flags	ftt_mtp.ftmt_flags
#define	ftt_bo		ftt_mtp.ftmt_bo
#define	ftt_bi		ftt_mtp.ftmt_bi

#define FASTTRAP_T_COMMON	0x00
#define FASTTRAP_T_B		0x01
#define FASTTRAP_T_BC		0x02
#define FASTTRAP_T_BLR		0x03
#define FASTTRAP_T_BCTR		0x04
#define FASTTRAP_T_NOP		0x05

#define	FASTTRAP_AFRAMES		3
#define	FASTTRAP_RETURN_AFRAMES		4
#define	FASTTRAP_ENTRY_AFRAMES		3
#define	FASTTRAP_OFFSET_AFRAMES		3

#ifdef	__cplusplus
}
#endif

#endif	/* _FASTTRAP_ISA_H */
