/*	$OpenBSD: reboot.h,v 1.21 2025/09/16 12:18:10 hshoexer Exp $	*/
/*	$NetBSD: reboot.h,v 1.9 1996/04/22 01:23:25 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)reboot.h	8.2 (Berkeley) 7/10/94
 */

#ifndef _SYS_REBOOT_H_
#define	_SYS_REBOOT_H_

/*
 * Arguments to reboot system call.  These are passed to the boot program,
 * and then on to init.
 */
#define	RB_AUTOBOOT	0	/* flags for system auto-booting itself */

#define	RB_ASKNAME	0x00001	/* ask for file name to reboot from */
#define	RB_SINGLE	0x00002	/* reboot to single user only */
#define	RB_NOSYNC	0x00004	/* dont sync before reboot */
#define	RB_HALT		0x00008	/* don't reboot, just halt */
#define	RB_INITNAME	0x00010	/* name given for /etc/init (unused) */
#define	RB_DFLTROOT	0x00020	/* use compiled-in rootdev */
#define	RB_KDB		0x00040	/* give control to kernel debugger */
#define	RB_RDONLY	0x00080	/* mount root fs read-only */
#define	RB_DUMP		0x00100	/* dump kernel memory before reboot */
#define	RB_MINIROOT	0x00200	/* mini-root present in memory at boot time */
#define	RB_CONFIG	0x00400	/* change configured devices */
#define	RB_TIMEBAD	0x00800	/* don't call resettodr() in boot() */
#define	RB_POWERDOWN	0x01000	/* attempt to power down machine */
#define	RB_SERCONS	0x02000	/* use serial console if available */
#define	RB_USERREQ	0x04000	/* boot() called at user request (e.g. ddb) */
#define	RB_RESET	0x08000	/* just reset, no cleanup  */
#define	RB_GOODRANDOM	0x10000	/* excellent random seed loaded */
#define	RB_UNHIBERNATE	0x20000	/* unhibernate */
#define RB_COCOVM	0x40000	/* booting confidential VM (e.g. SEV enabled) */

/*
 * Constants for converting boot-style device number to type,
 * adaptor (uba, mba, etc), unit number and partition number.
 * Type (== major device number) is in the low byte
 * for backward compatibility.  Except for that of the "magic
 * number", each mask applies to the shifted value.
 * Format:
 *	 (4) (4) (4) (4)  (8)     (8)
 *	--------------------------------
 *	|MA | AD| CT| UN| PART  | TYPE |
 *	--------------------------------
 */
#define	B_ADAPTORSHIFT		24
#define	B_ADAPTORMASK		0x0f
#define	B_ADAPTOR(val)		(((val) >> B_ADAPTORSHIFT) & B_ADAPTORMASK)
#define B_CONTROLLERSHIFT	20
#define B_CONTROLLERMASK	0xf
#define	B_CONTROLLER(val)	(((val)>>B_CONTROLLERSHIFT) & B_CONTROLLERMASK)
#define B_UNITSHIFT		16
#define B_UNITMASK		0xf
#define	B_UNIT(val)		(((val) >> B_UNITSHIFT) & B_UNITMASK)
#define B_PARTITIONSHIFT	8
#define B_PARTITIONMASK		0xff
#define	B_PARTITION(val)	(((val) >> B_PARTITIONSHIFT) & B_PARTITIONMASK)
#define	B_TYPESHIFT		0
#define	B_TYPEMASK		0xff
#define	B_TYPE(val)		(((val) >> B_TYPESHIFT) & B_TYPEMASK)

#define	B_MAGICMASK	0xf0000000
#define	B_DEVMAGIC	0xa0000000

#define MAKEBOOTDEV(type, adaptor, controller, unit, partition) \
	(((type) << B_TYPESHIFT) | ((adaptor) << B_ADAPTORSHIFT) | \
	((controller) << B_CONTROLLERSHIFT) | ((unit) << B_UNITSHIFT) | \
	((partition) << B_PARTITIONSHIFT) | B_DEVMAGIC)

#if	defined(_KERNEL) && !defined(_STANDALONE) && !defined(_LOCORE)

__BEGIN_DECLS
__dead void	reboot(int);
__dead void	boot(int);
__END_DECLS

#endif /* _KERNEL */

#endif /* !_SYS_REBOOT_H_ */
