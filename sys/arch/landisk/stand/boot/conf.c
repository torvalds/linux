/*	$OpenBSD: conf.c,v 1.18 2022/09/02 10:15:35 miod Exp $	*/

/*
 * Copyright (c) 2006 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <libsa.h>
#include <lib/libsa/ufs.h>
#include <lib/libsa/ufs2.h>
#include <dev/cons.h>

const char version[] = "1.11";
#ifdef DEBUG
int	debug = 1;
#endif

struct fs_ops file_system[] = {
	{ ufs_open,    ufs_close,    ufs_read,    ufs_write,    ufs_seek,
	  ufs_stat,    ufs_readdir,  ufs_fchmod },
	{ ufs2_open,   ufs2_close,   ufs2_read,   ufs2_write,   ufs2_seek,
	  ufs2_stat,   ufs2_readdir, ufs2_fchmod }
};
int nfsys = nitems(file_system);

struct devsw	devsw[] = {
	{ "dk", blkdevstrategy, blkdevopen, blkdevclose, noioctl }
};
int ndevs = nitems(devsw);

struct consdev constab[] = {
	{ scif_cnprobe, scif_cninit, scif_cngetc, scif_cnputc },
	{ NULL }
};
struct consdev *cn_tab;
