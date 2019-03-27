/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 *	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
 *	  All Rights Reserved
 *
 */

/*
 * Copyright 2014 Garrett D'Amore <garrett@damore.org>
 *
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_PROCESSOR_H
#define	_SYS_PROCESSOR_H

#include <sys/types.h>
#include <sys/procset.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Definitions for p_online, processor_info & lgrp system calls.
 */

/*
 * Type for an lgrpid
 */
typedef uint16_t lgrpid_t;

/*
 * Type for processor name (CPU number).
 */
typedef	int	processorid_t;
typedef int	chipid_t;

/*
 * Flags and return values for p_online(2), and pi_state for processor_info(2).
 * These flags are *not* for in-kernel examination of CPU states.
 * See <sys/cpuvar.h> for appropriate informational functions.
 */
#define	P_OFFLINE	0x0001	/* processor is offline, as quiet as possible */
#define	P_ONLINE	0x0002	/* processor is online */
#define	P_STATUS	0x0003	/* value passed to p_online to request status */
#define	P_FAULTED	0x0004	/* processor is offline, in faulted state */
#define	P_POWEROFF	0x0005	/* processor is powered off */
#define	P_NOINTR	0x0006	/* processor is online, but no I/O interrupts */
#define	P_SPARE		0x0007	/* processor is offline, can be reactivated */
#define	P_BAD		P_FAULTED	/* unused but defined by USL */
#define	P_FORCED 	0x10000000	/* force processor offline */

/*
 * String names for processor states defined above.
 */
#define	PS_OFFLINE	"off-line"
#define	PS_ONLINE	"on-line"
#define	PS_FAULTED	"faulted"
#define	PS_POWEROFF	"powered-off"
#define	PS_NOINTR	"no-intr"
#define	PS_SPARE	"spare"

/*
 * Structure filled in by processor_info(2). This structure
 * SHOULD NOT BE MODIFIED. Changes to the structure would
 * negate ABI compatibility.
 *
 * The string fields are guaranteed to contain a NULL.
 *
 * The pi_fputypes field contains a (possibly empty) comma-separated
 * list of floating point identifier strings.
 */
#define	PI_TYPELEN	16	/* max size of CPU type string */
#define	PI_FPUTYPE	32	/* max size of FPU types string */

typedef struct {
	int	pi_state;  			/* processor state, see above */
	char	pi_processor_type[PI_TYPELEN];	/* ASCII CPU type */
	char	pi_fputypes[PI_FPUTYPE];	/* ASCII FPU types */
	int	pi_clock;			/* CPU clock freq in MHz */
} processor_info_t;

/*
 * Binding values for processor_bind(2)
 */
#define	PBIND_NONE	-1	/* LWP/thread is not bound */
#define	PBIND_QUERY	-2	/* don't set, just return the binding */
#define	PBIND_HARD	-3	/* prevents offlining CPU (default) */
#define	PBIND_SOFT	-4	/* allows offlining CPU */
#define	PBIND_QUERY_TYPE	-5	/* Return binding type */

/*
 * User-level system call interface prototypes
 */
#ifndef _KERNEL

extern int	p_online(processorid_t processorid, int flag);
extern int	processor_info(processorid_t processorid,
		    processor_info_t *infop);
extern int	processor_bind(idtype_t idtype, id_t id,
		    processorid_t processorid, processorid_t *obind);
extern processorid_t getcpuid(void);
extern lgrpid_t gethomelgroup(void);

#else   /* _KERNEL */

/*
 * Internal interface prototypes
 */
extern int	p_online_internal(processorid_t, int, int *);
extern int	p_online_internal_locked(processorid_t, int, int *);

#endif /* !_KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_PROCESSOR_H */
