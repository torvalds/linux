/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)reboot.h	8.3 (Berkeley) 12/13/94
 * $FreeBSD$
 */

#ifndef _SYS_REBOOT_H_
#define	_SYS_REBOOT_H_

/*
 * Arguments to reboot system call.  These are passed to
 * the boot program and on to init.
 */
#define	RB_AUTOBOOT	0	/* flags for system auto-booting itself */

#define	RB_ASKNAME	0x001	/* force prompt of device of root filesystem */
#define	RB_SINGLE	0x002	/* reboot to single user only */
#define	RB_NOSYNC	0x004	/* dont sync before reboot */
#define	RB_HALT		0x008	/* don't reboot, just halt */
#define	RB_INITNAME	0x010	/* Unused placeholder to specify init path */
#define	RB_DFLTROOT	0x020	/* use compiled-in rootdev */
#define	RB_KDB		0x040	/* give control to kernel debugger */
#define	RB_RDONLY	0x080	/* mount root fs read-only */
#define	RB_DUMP		0x100	/* dump kernel memory before reboot */
#define	RB_MINIROOT	0x200	/* Unused placeholder */
#define	RB_VERBOSE	0x800	/* print all potentially useful info */
#define	RB_SERIAL	0x1000	/* use serial port as console */
#define	RB_CDROM	0x2000	/* use cdrom as root */
#define	RB_POWEROFF	0x4000	/* turn the power off if possible */
#define	RB_GDB		0x8000	/* use GDB remote debugger instead of DDB */
#define	RB_MUTE		0x10000	/* start up with the console muted */
#define	RB_SELFTEST	0x20000	/* unused placeholder */
#define	RB_RESERVED1	0x40000	/* reserved for internal use of boot blocks */
#define	RB_RESERVED2	0x80000	/* reserved for internal use of boot blocks */
#define	RB_PAUSE	0x100000 /* pause after each output line during probe */
#define	RB_REROOT	0x200000 /* unmount the rootfs and mount it again */
#define	RB_POWERCYCLE	0x400000 /* Power cycle if possible */
#define	RB_PROBE	0x10000000	/* Probe multiple consoles */
#define	RB_MULTIPLE	0x20000000	/* use multiple consoles */

#define	RB_BOOTINFO	0x80000000	/* have `struct bootinfo *' arg */

#endif
