/* $OpenBSD: pal.h,v 1.2 2000/11/08 21:27:20 ericj Exp $ */
/* $NetBSD: pal.h,v 1.1 1997/09/06 01:23:53 thorpej Exp $ */

/* 
 * Copyright (c) 1991,1990,1989,1994,1995,1996 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

/*
 * PAL "function" codes (used as arguments to call_pal instructions).
 *
 * Those marked with "P" are privileged, and those marked with "U"
 * are unprivileged.
 */

/* Common PAL function codes. */
#define	PAL_halt		0x0000			/* P */
#define	PAL_cflush		0x0001			/* P */
#define	PAL_draina		0x0002			/* P */
#define	PAL_cserve		0x0009			/* P */
#define	PAL_swppal		0x000a			/* P */
#define	PAL_ipir		0x000d			/* P */
#define	PAL_bpt			0x0080			/* U */
#define	PAL_bugchk		0x0081			/* U */
#define	PAL_imb			0x0086			/* U */
#define	PAL_rdunique		0x009e			/* U */
#define	PAL_wrunique		0x009f			/* U */
#define	PAL_gentrap		0x00aa			/* U */

/* VMS PAL function codes. */
#define	PAL_VMS_ldqp		0x0003			/* P */
#define	PAL_VMS_stqp		0x0004			/* P */
#define	PAL_VMS_mtpr_fen	0x000c			/* P */
#define	PAL_VMS_mtpr_ipir	0x000d			/* P */
#define	PAL_VMS_mfpr_ipl	0x000e			/* P */
#define	PAL_VMS_mtpr_ipl	0x000f			/* P */
#define	PAL_VMS_mfpr_mces	0x0010			/* P */
#define	PAL_VMS_mtpr_mces	0x0011			/* P */
#define	PAL_VMS_mfpr_prbr	0x0013			/* P */
#define	PAL_VMS_mtpr_prbr	0x0014			/* P */
#define	PAL_VMS_mfpr_ptbr	0x0015			/* P */
#define	PAL_VMS_mtpr_scbb	0x0017			/* P */
#define	PAL_VMS_mtpr_sirr	0x0018			/* P */
#define	PAL_VMS_mtpr_tbia	0x001b			/* P */
#define	PAL_VMS_mtpr_tbiap	0x001c			/* P */
#define	PAL_VMS_mtpr_tbis	0x001d			/* P */
#define	PAL_VMS_mfpr_usp	0x0022			/* P */
#define	PAL_VMS_mtpr_usp	0x0023			/* P */
#define	PAL_VMS_mfpr_vptb	0x0029			/* P */
#define	PAL_VMS_mfpr_whami	0x003f			/* P */
#define	PAL_VMS_rei		0x0092			/* U */

/* OSF/1 PAL function codes. */
#define	PAL_OSF1_rdmces		0x0010			/* P */
#define	PAL_OSF1_wrmces		0x0011			/* P */
#define	PAL_OSF1_wrfen		0x002b			/* P */
#define	PAL_OSF1_wrvptptr	0x002d			/* P */
#define	PAL_OSF1_swpctx		0x0030			/* P */
#define	PAL_OSF1_wrval		0x0031			/* P */
#define	PAL_OSF1_rdval		0x0032			/* P */
#define	PAL_OSF1_tbi		0x0033			/* P */
#define	PAL_OSF1_wrent		0x0034			/* P */
#define	PAL_OSF1_swpipl		0x0035			/* P */
#define	PAL_OSF1_rdps		0x0036			/* P */
#define	PAL_OSF1_wrkgp		0x0037			/* P */
#define	PAL_OSF1_wrusp		0x0038			/* P */
#define	PAL_OSF1_wrperfmon	0x0039			/* P */
#define	PAL_OSF1_rdusp		0x003a			/* P */
#define	PAL_OSF1_whami		0x003c			/* P */
#define	PAL_OSF1_retsys		0x003d			/* P */
#define	PAL_OSF1_rti		0x003f			/* P */
#define	PAL_OSF1_callsys	0x0083			/* U */
#define	PAL_OSF1_imb		0x0086			/* U */
