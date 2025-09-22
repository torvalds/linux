/*	$OpenBSD: tcreg.h,v 1.3 2002/05/02 22:56:06 miod Exp $	*/
/*	$NetBSD: tcreg.h,v 1.1 1995/12/20 00:48:36 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
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

#ifndef __DEV_TC_TCREG_H__
#define __DEV_TC_TCREG_H__

/*
 * TurboChannel bus and register definitions.
 */

#define	TC_ROM_LLEN		8
#define	TC_ROM_SLEN		4
#define	TC_ROM_TEST_SIZE	16

#define	TC_SLOT_ROM		0x000003e0
#define	TC_SLOT_PROTOROM	0x003c03e0

typedef struct tc_padchar {
	u_int8_t	v;
	u_int8_t	pad[3];
} tc_padchar_t;

struct tc_rommap {
	tc_padchar_t	tcr_width;
	tc_padchar_t	tcr_stride;
	tc_padchar_t	tcr_rsize;
	tc_padchar_t	tcr_ssize;
	u_int8_t	tcr_test[TC_ROM_TEST_SIZE];
	tc_padchar_t	tcr_rev[TC_ROM_LLEN];
	tc_padchar_t	tcr_vendname[TC_ROM_LLEN];
	tc_padchar_t	tcr_modname[TC_ROM_LLEN];
	tc_padchar_t	tcr_firmtype[TC_ROM_SLEN];
};

#endif /* __DEV_TC_TCREG_H__ */
