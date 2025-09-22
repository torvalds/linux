/*	$OpenBSD: vndioctl.h,v 1.12 2023/05/14 18:34:02 krw Exp $	*/
/*	$NetBSD: vndioctl.h,v 1.5 1995/01/25 04:46:30 cgd Exp $	*/

/*
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
 */

#ifndef _SYS_VNDIOCTL_H_
#define _SYS_VNDIOCTL_H_

#define VNDNLEN	1024		/* PATH_MAX */

/*
 * Ioctl definitions for file (vnode) disk pseudo-device.
 */
struct vnd_ioctl {
	char		*vnd_file;	/* pathname of file to mount */
	size_t		 vnd_secsize;	/* sector size in bytes */
	size_t		 vnd_nsectors;	/* number of sectors in a track */
	size_t		 vnd_ntracks;	/* number of tracks per cylinder */
	off_t		 vnd_size;	/* (returned) size of disk */
	u_char		*vnd_key;
	int		 vnd_keylen;
	uint16_t	 vnd_type;	/* DTYPE being emulated */
};

/*
 * A simple structure used by userland to query about a specific vnd.
 */
struct vnd_user {
	char	vnu_file[VNDNLEN];	/* vnd file */
	int	vnu_unit;		/* vnd unit */
	dev_t	vnu_dev;		/* vnd device */
	ino_t	vnu_ino;		/* vnd inode */
};

/*
 * Before you can use a unit, it must be configured with VNDIOCSET.
 * The configuration persists across opens and closes of the device;
 * an VNDIOCCLR must be used to reset a configuration.  An attempt to
 * VNDIOCSET an already active unit will return EBUSY.
 */
#define VNDIOCSET	_IOWR('F', 0, struct vnd_ioctl)	/* enable disk */
#define VNDIOCCLR	_IOW('F', 1, struct vnd_ioctl)	/* disable disk */
#define VNDIOCGET	_IOWR('F', 3, struct vnd_user)	/* get disk info */

#endif /* !_SYS_VNDIOCTL_H_ */
