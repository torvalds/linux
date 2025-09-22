/*	$OpenBSD: conf.c,v 1.1 2023/03/11 20:56:01 miod Exp $	*/
/*	$NetBSD: conf.c,v 1.3 1995/11/23 02:39:31 cgd Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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

#include "libsa.h"

#include <lib/libsa/ufs.h>
#include <lib/libsa/ufs2.h>
#include <lib/libsa/cd9660.h>
#include <dev/cons.h>

const char version[] = "2.0";
#ifdef DEBUG
int debug = 0;
#endif

struct fs_ops file_system[] = {
	{ ufs_open,	ufs_close,	ufs_read,	ufs_write,
	  ufs_seek,	ufs_stat,	ufs_readdir,	ufs_fchmod },
	{ ufs2_open,	ufs2_close,	ufs2_read,	ufs2_write,
	  ufs2_seek,	ufs2_stat,	ufs2_readdir,	ufs2_fchmod },
	{ cd9660_open,	cd9660_close,	cd9660_read,	cd9660_write,
	  cd9660_seek,	cd9660_stat,	cd9660_readdir }
};
int	nfsys = sizeof(file_system) / sizeof(file_system[0]);

struct devsw devsw[] = {
	{ "disk",
	  diskstrategy,	diskopen,	diskclose,	diskioctl }
};
int	ndevs = sizeof(devsw) / sizeof(devsw[0]);

struct consdev constab[] = {
	{ prom_cnprobe,	prom_cninit,	prom_cngetc,	prom_cnputc },
	{ NULL }
};
struct consdev *cn_tab;
