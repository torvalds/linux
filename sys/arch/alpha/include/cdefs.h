/*	$OpenBSD: cdefs.h,v 1.11 2013/03/28 17:30:45 martynas Exp $	*/

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

#ifndef _MACHINE_CDEFS_H_
#define	_MACHINE_CDEFS_H_

#define	__strong_alias(alias,sym)				\
	__asm__(".global " __STRING(alias) " ; "		\
	    __STRING(alias) " = " __STRING(sym))
#define	__weak_alias(alias,sym)					\
	__asm__(".weak " __STRING(alias) " ; "			\
	    __STRING(alias) " = " __STRING(sym))
#define	__warn_references(sym,msg)				\
	__asm__(".section .gnu.warning." __STRING(sym)		\
	    " ; .ascii \"" msg "\" ; .text")

#endif /* !_MACHINE_CDEFS_H_ */
