/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 * from: Utah $Hdr: fdioctl.h 1.1 90/07/09$
 *
 *	@(#)vnioctl.h	8.1 (Berkeley) 6/10/93
 *
 * From: src/sys/sys/vnioctl.h,v 1.4
 *
 * $FreeBSD$
 */

#ifndef _SYS_MDIOCTL_H_
#define _SYS_MDIOCTL_H_

enum md_types {MD_MALLOC, MD_PRELOAD, MD_VNODE, MD_SWAP, MD_NULL};

/*
 * Ioctl definitions for memory disk pseudo-device.
 */

#define MDNPAD		96
struct md_ioctl {
	unsigned	md_version;	/* Structure layout version */
	unsigned	md_unit;	/* unit number */
	enum md_types	md_type ;	/* type of disk */
	char		*md_file;	/* pathname of file to mount */
	off_t		md_mediasize;	/* size of disk in bytes */
	unsigned	md_sectorsize;	/* sectorsize */
	unsigned	md_options;	/* options */
	u_int64_t	md_base;	/* base address */
	int		md_fwheads;	/* firmware heads */
	int		md_fwsectors;	/* firmware sectors */
	char		*md_label;	/* label of the device */
	int		md_pad[MDNPAD];	/* storage for MDIOCLIST */
};

#define MD_NAME		"md"
#define MDCTL_NAME	"mdctl"
#define MDIOVERSION	0

/*
 * Before you can use a unit, it must be configured with MDIOCSET.
 * The configuration persists across opens and closes of the device;
 * an MDIOCCLR must be used to reset a configuration.  An attempt to
 * MDIOCSET an already active unit will return EBUSY.
 */

#define MDIOCATTACH	_IOWR('m', 0, struct md_ioctl)	/* attach disk */
#define MDIOCDETACH	_IOWR('m', 1, struct md_ioctl)	/* detach disk */
#define MDIOCQUERY	_IOWR('m', 2, struct md_ioctl)	/* query status */
#define MDIOCLIST	_IOWR('m', 3, struct md_ioctl)	/* query status */
#define MDIOCRESIZE	_IOWR('m', 4, struct md_ioctl)	/* resize disk */

#define MD_CLUSTER	0x01	/* Don't cluster */
#define MD_RESERVE	0x02	/* Pre-reserve swap */
#define MD_AUTOUNIT	0x04	/* Assign next free unit */
#define MD_READONLY	0x08	/* Readonly mode */
#define MD_COMPRESS	0x10	/* Compression mode */
#define MD_FORCE	0x20	/* Don't try to prevent foot-shooting */
#define MD_ASYNC	0x40	/* Asynchronous mode */
#define MD_VERIFY	0x80	/* Open file with O_VERIFY (vnode only) */
#define	MD_CACHE	0x100	/* Cache vnode data */

#endif	/* _SYS_MDIOCTL_H_*/
