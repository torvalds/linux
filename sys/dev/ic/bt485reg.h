/* $OpenBSD: bt485reg.h,v 1.2 2001/04/21 20:03:55 aaron Exp $ */
/* $NetBSD: bt485reg.h,v 1.1 1998/04/15 20:16:30 drochner Exp $ */

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
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
 * Register definitions for the Brooktree Bt485A 170MHz Monolithic
 * CMOS True-Color RAMDAC.
 */


/*
 * Directly-addressed registers.
 */
#define	BT485_REG_PCRAM_WRADDR	0x00
#define	BT485_REG_PALETTE	0x01
#define	BT485_REG_PIXMASK	0x02
#define	BT485_REG_PCRAM_RDADDR	0x03
#define	BT485_REG_COC_WRADDR	0x04
#define	BT485_REG_COCDATA	0x05
#define	BT485_REG_COMMAND_0	0x06
#define	BT485_REG_COC_RDADDR	0x07
#define	BT485_REG_COMMAND_1	0x08
#define	BT485_REG_COMMAND_2	0x09
#define	BT485_REG_STATUS	0x0a
#define	BT485_REG_EXTENDED	BT485_REG_STATUS
#define	BT485_REG_CURSOR_RAM	0x0b
#define	BT485_REG_CURSOR_X_LOW	0x0c
#define	BT485_REG_CURSOR_X_HIGH	0x0d
#define	BT485_REG_CURSOR_Y_LOW	0x0e
#define	BT485_REG_CURSOR_Y_HIGH	0x0f

#define	BT485_REG_MAX		0x0f

#define	BT485_IREG_STATUS	0x00
#define	BT485_IREG_COMMAND_3	0x01
#define	BT485_IREG_COMMAND_4	0x02
#define	BT485_IREG_RSA		0x20
#define	BT485_IREG_GSA		0x21
#define	BT485_IREG_BSA		0x22
