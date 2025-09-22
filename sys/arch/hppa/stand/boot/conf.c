/*	$OpenBSD: conf.c,v 1.37 2020/12/09 18:10:18 krw Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <libsa.h>
#include <lib/libsa/ufs.h>
#include <lib/libsa/ufs2.h>
#include <lib/libsa/cd9660.h>
#include <dev/cons.h>

const char version[] = "1.11";
int	debug = 0;

struct fs_ops file_system[] = {
	{ ufs_open,    ufs_close,    ufs_read,    ufs_write,    ufs_seek,
	  ufs_stat,    ufs_readdir,  ufs_fchmod },
	{ ufs2_open,   ufs2_close,   ufs2_read,   ufs2_write,   ufs2_seek,
	  ufs2_stat,   ufs2_readdir, ufs2_fchmod },
	{ cd9660_open, cd9660_close, cd9660_read, cd9660_write, cd9660_seek,
	  cd9660_stat, cd9660_readdir },
	{ lif_open,    lif_close,    lif_read,    lif_write,    lif_seek,
	  lif_stat,    lif_readdir    },
};
int nfsys = nitems(file_system);

struct devsw devsw[] = {
	{ "dk",	iodcstrategy, dkopen, dkclose, noioctl },
	{ "ct",	iodcstrategy, ctopen, ctclose, noioctl },
	{ "lf", iodcstrategy, lfopen, lfclose, noioctl }
};
int	ndevs = nitems(devsw);

struct consdev	constab[] = {
	{ ite_probe, ite_init, ite_getc, ite_putc },
	{ NULL }
};
struct consdev *cn_tab;

