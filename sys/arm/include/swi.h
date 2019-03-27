/*	$NetBSD: swi.h,v 1.1 2002/01/13 15:03:06 bjh21 Exp $	*/
/* $FreeBSD$ */

/*-
 * This file is in the Public Domain.
 * Ben Harris, 2002.
 */

#ifndef _MACHINE_SWI_H_
#define _MACHINE_SWI_H_

#define SWI_OS_MASK	0xf00000
#define SWI_OS_RISCOS	0x000000
#define SWI_OS_RISCIX	0x800000
#define SWI_OS_LINUX	0x900000
#define SWI_OS_NETBSD	0xa00000
#define SWI_OS_ARM	0xf00000

#define SWI_IMB		0xf00000
#define SWI_IMBrange	0xf00001

#endif /* !_MACHINE_SWI_H_ */

