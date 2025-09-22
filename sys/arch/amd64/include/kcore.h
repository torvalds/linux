/*	$OpenBSD: kcore.h,v 1.5 2018/04/03 07:13:37 mlarkin Exp $	*/
/*	$NetBSD: kcore.h,v 1.1 2003/04/26 18:39:43 fvdl Exp $	*/

/*
 * Copyright (c) 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
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

/*
 * Modified for NetBSD/i386 by Jason R. Thorpe, Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
 *
 * And once again modified for x86-64 by Frank van der Linden of
 * Wasabi Systems, Inc.
 */

#ifndef _MACHINE_KCORE_H_
#define _MACHINE_KCORE_H_

typedef struct cpu_kcore_hdr {
	u_int64_t	ptdpaddr;		/* PA of PML4 */
	u_int64_t	nmemsegs;		/* Number of RAM segments */
} cpu_kcore_hdr_t;

#endif /* _MACHINE_KCORE_H_ */
