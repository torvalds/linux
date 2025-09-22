/*	$OpenBSD: conf.c,v 1.79 2024/06/04 21:48:20 krw Exp $	*/

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
#include <netinet/in.h>
#include <libsa.h>
#include <lib/libsa/ufs.h>
#include <lib/libsa/ufs2.h>
#ifdef notdef
#include <lib/libsa/cd9660.h>
#include <lib/libsa/fat.h>
#include <lib/libsa/nfs.h>
#include <lib/libsa/tftp.h>
#include <lib/libsa/netif.h>
#endif
#include <biosdev.h>
#include <dev/cons.h>
#include "debug.h"

const char version[] = "3.67";
int	debug = 1;


void (*sa_cleanup)(void) = NULL;


void (*i386_probe1[])(void) = {
	ps2probe, gateA20on, debug_init, cninit,
	apmprobe, pciprobe, /* smpprobe, */ memprobe
};
void (*i386_probe2[])(void) = {
 	diskprobe
};

struct i386_boot_probes probe_list[] = {
	{ "probing", i386_probe1, nitems(i386_probe1) },
	{ "disk",    i386_probe2, nitems(i386_probe2) }
};
int nibprobes = nitems(probe_list);


struct fs_ops file_system[] = {
	{ ufs_open,    ufs_close,    ufs_read,    ufs_write,    ufs_seek,
	  ufs_stat,    ufs_readdir,  ufs_fchmod },
#ifndef FDBOOT
	{ ufs2_open,   ufs2_close,   ufs2_read,   ufs2_write,   ufs2_seek,
	  ufs2_stat,   ufs2_readdir, ufs2_fchmod },
#endif
#ifdef notdef
	{ fat_open,    fat_close,    fat_read,    fat_write,    fat_seek,
	  fat_stat,    fat_readdir    },
	{ nfs_open,    nfs_close,    nfs_read,    nfs_write,    nfs_seek,
	  nfs_stat,    nfs_readdir    },
	{ cd9660_open, cd9660_close, cd9660_read, cd9660_write, cd9660_seek,
	  cd9660_stat, cd9660_readdir },
#endif
};
int nfsys = nitems(file_system);

struct devsw	devsw[] = {
	{ "BIOS", biosstrategy, biosopen, biosclose, biosioctl },
#if 0
	{ "TFTP", tftpstrategy, tftpopen, tftpclose, tftpioctl },
#endif
};
int ndevs = nitems(devsw);

#ifdef notdef
struct netif_driver	*netif_drivers[] = {
	NULL
};
int n_netif_drivers = nitems(netif_drivers);
#endif

struct consdev constab[] = {
	{ pc_probe, pc_init, pc_getc, pc_putc },
	{ com_probe, com_init, com_getc, com_putc },
	{ NULL }
};
struct consdev *cn_tab = constab;
