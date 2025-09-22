/*	$OpenBSD: conf.c,v 1.6 2023/06/18 13:13:00 aoyama Exp $	*/
/*	$NetBSD: conf.c,v 1.3 2013/01/16 15:46:20 tsutsui Exp $	*/

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
#include <sys/socket.h>

#include <dev/cons.h>

#include <net/if.h>
#include <netinet/in.h>

#include <luna88k/stand/boot/samachdep.h>
#include <lib/libsa/nfs.h>
#include <lib/libsa/ufs.h>
#include "dev_net.h"

#define xxstrategy	\
	(int (*)(void *, int, daddr_t, size_t, void *, size_t *))nullsys
#define xxopen		(int (*)(struct open_file *, ...))nodev
#define xxclose		(int (*)(struct open_file *))nullsys

/*
 * Device configuration
 */
#ifndef SUPPORT_ETHERNET
#define	netstrategy	xxstrategy
#define	netopen		xxopen
#define	netclose	xxclose
#else
#define	netstrategy	net_strategy
#define	netopen		net_open
#define	netclose	net_close
#endif
#define	netioctl	noioctl

#ifndef SUPPORT_DISK
#define	sdstrategy	xxstrategy
#define	sdopen		xxopen
#define	sdclose		xxclose
#endif
#define	sdioctl		noioctl

/*
 * Note: "le" isn't a major offset.
 */
struct devsw devsw[] = {
	{ "sd",	sdstrategy,	sdopen,	sdclose,	sdioctl },
	{ "le",	netstrategy,	netopen, netclose,	netioctl },
};
int	ndevs = sizeof(devsw) / sizeof(devsw[0]);

#ifdef SUPPORT_ETHERNET
extern struct netif_driver le_netif_driver;
struct netif_driver *netif_drivers[] = {
	&le_netif_driver,
};
int	n_netif_drivers = sizeof(netif_drivers) / sizeof(netif_drivers[0]);
#endif

/*
 * Filesystem configuration
 */
#define	FS_OPS(fs) { \
	__CONCAT(fs,_open), \
	__CONCAT(fs,_close), \
	__CONCAT(fs,_read), \
	__CONCAT(fs,_write), \
	__CONCAT(fs,_seek), \
	__CONCAT(fs,_stat), \
	__CONCAT(fs,_readdir), \
	__CONCAT(fs,_fchmod) }
#ifdef SUPPORT_DISK
struct fs_ops file_system_disk[] = {
	FS_OPS(ufs),
};
int	nfsys_disk = sizeof(file_system_disk) / sizeof(file_system_disk[0]);
#endif
#ifdef SUPPORT_ETHERNET
#define	nfs_fchmod	NULL
struct fs_ops file_system_nfs[] = { FS_OPS(nfs) };
#endif

#define MAX_NFSYS	5
struct fs_ops file_system[MAX_NFSYS];
int	nfsys = 1;		/* we always know which one we want */

/*
 * Console configuration
 */

struct	consdev constab[] = {
	{ bmccnprobe,	bmccninit,	bmccngetc,	bmccnputc },
	{ siocnprobe,	siocninit,	siocngetc,	siocnputc },
	{ 0 },
};

struct consdev *cn_tab;
