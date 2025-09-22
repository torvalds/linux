/* $OpenBSD: tc_3000_300.h,v 1.5 2002/05/02 22:56:06 miod Exp $ */
/* $NetBSD: tc_3000_300.h,v 1.4 1998/10/22 01:03:09 briggs Exp $ */

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

/*
 * TurboChannel-specific functions and structures for 3000_500.
 */

/*
 * TURBOchannel Interface Registers.
 */
#define	TC_3000_300_IR			KV(0x00000001e0000000)	/* Dense */
#define	TC_3000_300_CSR			KV(0x00000001e0000008)	/* Dense */
#define	TC_3000_300_MCR			KV(0x00000001e0000010)	/* Dense */
#define	TC_3000_300_LED			KV(0x00000001e0000018)	/* Dense */

/* Interrupt bits. */
#define	TC_3000_300_IR_CXTURBO		0x00000004	/* TC CXTURBO */
#define	TC_3000_300_IR_TCDS		0x00000008	/* TC Dual SCSI */
#define	TC_3000_300_IR_IOASIC		0x00000010	/* TC IOASIC */
#define	TC_3000_300_IR_BCTAGPARITY	0x08000000	/* BC tag par. err. */
#define	TC_3000_300_IR_TCOVERRUN	0x10000000	/* TC overrun */
#define	TC_3000_300_IR_TCTIMEOUT	0x20000000	/* TC timeout on I/O */
#define	TC_3000_300_IR_BCACHEPARITY	0x40000000	/* Bcache par. err. */
#define	TC_3000_300_IR_MEMPARITY	0x80000000	/* Memory par. err. */

/* Device number "cookies." */
#define	TC_3000_300_DEV_OPT0	0
#define	TC_3000_300_DEV_OPT1	1
#define	TC_3000_300_DEV_TCDS	2
#define	TC_3000_300_DEV_IOASIC	3
#define	TC_3000_300_DEV_CXTURBO	4

#define TC_3000_300_DEV_BOGUS	-1

#define	TC_3000_300_NCOOKIES	5

extern int	tc_3000_300_fb_cnattach(u_int64_t);
