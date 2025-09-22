/*	$OpenBSD: conf.c,v 1.4 2024/06/17 09:12:45 kettenis Exp $	*/

/*
 * Copyright (c) 1996 Michael Shalayeff
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/disklabel.h>
#include <lib/libsa/stand.h>
#include <lib/libsa/tftp.h>
#include <lib/libsa/ufs.h>
#include <lib/libsa/ufs2.h>
#include <dev/cons.h>

#include <dev/biovar.h>
#include <dev/softraidvar.h>

#include <efi.h>

#include "disk.h"
#include "efiboot.h"
#include "efidev.h"
#include "efipxe.h"
#include "softraid_riscv64.h"

const char version[] = "1.7";
int	debug = 0;

struct fs_ops file_system[] = {
	{ mtftp_open,  mtftp_close,  mtftp_read,  mtftp_write,  mtftp_seek,
	  mtftp_stat,  mtftp_readdir   },
	{ efitftp_open,tftp_close,   tftp_read,   tftp_write,   tftp_seek,
	  tftp_stat,   tftp_readdir   },
	{ ufs_open,    ufs_close,    ufs_read,    ufs_write,    ufs_seek,
	  ufs_stat,    ufs_readdir,  ufs_fchmod },
	{ ufs2_open,   ufs2_close,   ufs2_read,   ufs2_write,   ufs2_seek,
	  ufs2_stat,   ufs2_readdir, ufs2_fchmod },
};
int nfsys = nitems(file_system);

struct devsw	devsw[] = {
	{ "tftp", tftpstrategy, tftpopen, tftpclose, tftpioctl },
	{ "sd", efistrategy, efiopen, eficlose, efiioctl },
	{ "sr", srstrategy, sropen, srclose, srioctl },
};
int ndevs = nitems(devsw);

struct consdev constab[] = {
	{ efi_cons_probe, efi_cons_init, efi_cons_getc, efi_cons_putc },
	{ efi_fb_probe, efi_fb_init, efi_cons_getc, efi_cons_putc },
	{ NULL }
};
struct consdev *cn_tab;

struct netif_driver *netif_drivers[] = {
	&efinet_driver,
};
int n_netif_drivers = nitems(netif_drivers);
