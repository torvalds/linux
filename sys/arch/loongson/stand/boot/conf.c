/*	$OpenBSD: conf.c,v 1.17 2022/08/10 12:20:05 miod Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1990, 1993
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
 *	@(#)conf.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>

#include <dev/cons.h>

#include "libsa.h"
#include <lib/libsa/ufs.h>
#include <lib/libsa/ufs2.h>
#include <lib/libsa/cd9660.h>

const char version[] = "1.4";
#if 0	/* network code not compiled in */
int	debug = 0;
#endif

/*
 * Device configuration
 */
struct devsw devsw[] = {
	/* initrd */
	{ "rd",		rd_iostrategy, rd_ioopen, rd_ioclose, noioctl },
	/* ATA storage device */
	{ "wd",		pmon_iostrategy, pmon_ioopen, pmon_ioclose, noioctl }
};
int ndevs = nitems(devsw);

/*
 * Filesystem configuration
 */
struct fs_ops file_system[] = {
	/* initrd ``filesystem'' */
	{	rdfs_open,	rdfs_close,	rdfs_read,	rdfs_write,
		rdfs_seek,	rdfs_stat,	rdfs_readdir	},
	/* ufs filesystem */
	{	ufs_open,	ufs_close,	ufs_read,	ufs_write,
		ufs_seek,	ufs_stat,	ufs_readdir,	ufs_fchmod },
	{	ufs2_open,	ufs2_close,	ufs2_read,	ufs2_write,
		ufs2_seek,	ufs2_stat,	ufs2_readdir,	ufs2_fchmod },
	/* cd9660 filesystem - in case a cd image is dd'ed on non USB media */
	{	cd9660_open,	cd9660_close,	cd9660_read,	cd9660_write,
		cd9660_seek,	cd9660_stat,	cd9660_readdir	}
};
int nfsys = nitems(file_system);

/*
 * Console configuration
 */
struct consdev constab[] = {
	{ pmon_cnprobe, pmon_cninit, pmon_cngetc, pmon_cnputc },
	{ NULL }
};
struct consdev *cn_tab;
