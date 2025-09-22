/*	$OpenBSD: dev_net.c,v 1.7 2023/01/16 07:29:35 deraadt Exp $	*/
/*	$NetBSD: dev_net.c,v 1.4 1997/04/06 08:41:24 cgd Exp $	*/

/*
 * Copyright (c) 1995 Gordon W. Ross
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This module implements a "raw device" interface suitable for
 * use by the stand-alone I/O library NFS code.  This interface
 * does not support any "block" access, and exists only for the
 * purpose of initializing the network interface, getting boot
 * parameters, and performing the NFS mount.
 *
 * At open time, this does:
 *
 * find interface      - netif_open()
 * RARP for IP address - rarp_getipaddress()
 * RPC/bootparams      - callrpc(d, RPC_BOOTPARAMS, ...)
 * RPC/mountd          - nfs_mount(sock, ip, path)
 *
 * the root file handle from mountd is saved in a global
 * for use by the NFS open code (NFS/lookup).
 */

#include <stdarg.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <lib/libsa/stand.h>
#include <lib/libsa/net.h>
#include <lib/libsa/netif.h>
#include <lib/libsa/bootparam.h>
#include "dev_net.h"

extern int debug;
extern int nfs_root_node[];	/* XXX - get from nfs_mount() */

/*
 * Various globals needed by the network code:
 */

#if 0
/* for arp.c, rarp.c */
u_char bcea[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
#endif

/*
 * Local things...
 */
static int netdev_sock = -1;
static int netdev_opens;

int net_getparams(int);
int nfs_mount(int, struct in_addr, char *);

/*
 * Called by devopen after it sets f->f_dev to our devsw entry.
 * This opens the low-level device and sets f->f_devdata.
 * This is declared with variable arguments...
 */
int
net_open(struct open_file *f, ...)
{
	va_list ap;
	char *devname;		/* Device part of file name (or NULL). */
	int error = 0;

	va_start(ap, f);
	devname = va_arg(ap, char *);
	va_end(ap);

#ifdef	NETIF_DEBUG
	if (debug)
		printf("net_open: %s\n", devname);
#endif

	/* On first open, do netif open, mount, etc. */
	if (netdev_opens == 0) {
		/* Find network interface. */
		if (netdev_sock < 0) {
			netdev_sock = netif_open(devname);
			if (netdev_sock < 0) {
				printf("net_open: netif_open() failed\n");
				return (ENXIO);
			}
			if (debug)
				printf("net_open: netif_open() succeeded\n");
		}
		if (rootip.s_addr == 0) {
			/* Get root IP address, and path, etc. */
			error = net_getparams(netdev_sock);
			if (error) {
				/* getparams makes its own noise */
				goto fail;
			}
			/* Get the NFS file handle (mountd). */
			error = nfs_mount(netdev_sock, rootip, rootpath);
			if (error) {
				printf("net_open: NFS mount error=%d\n", error);
				rootip.s_addr = 0;
			fail:
				netif_close(netdev_sock);
				netdev_sock = -1;
				return (error);
			}
			if (debug)
				printf("net_open: NFS mount succeeded\n");
		}
	}
	netdev_opens++;
	f->f_devdata = nfs_root_node;
	return (error);
}

int
net_close(f)
	struct open_file *f;
{

#ifdef	NETIF_DEBUG
	if (debug)
		printf("net_close: opens=%d\n", netdev_opens);
#endif

	/* On last close, do netif close, etc. */
	f->f_devdata = NULL;
	/* Extra close call? */
	if (netdev_opens <= 0)
		return (0);
	netdev_opens--;
	/* Not last close? */
	if (netdev_opens > 0)
		return(0);
	rootip.s_addr = 0;
	if (netdev_sock >= 0) {
		if (debug)
			printf("net_close: calling netif_close()\n");
		netif_close(netdev_sock);
		netdev_sock = -1;
	}
	return (0);
}

int
net_ioctl()
{
	return EIO;
}

int
net_strategy()
{
	return EIO;
}

int
net_getparams(sock)
	int sock;
{
	/*
	 * Get info for NFS boot: our IP address, our hostname,
	 * server IP address, and our root path on the server.
	 * There are two ways to do this:  The old, Sun way,
	 * and the more modern, BOOTP way. (RFC951, RFC1048)
	 */

#ifdef	SUN_BOOTPARAMS
	/* Get our IP address.  (rarp.c) */
	if (rarp_getipaddress(sock)) {
		printf("net_open: RARP failed\n");
		return (EIO);
	}
#else	/* BOOTPARAMS */
	/*
	 * Get boot info using BOOTP. (RFC951, RFC1048)
	 * This also gets the server IP address, gateway,
	 * root path, etc.
	 */
	bootp(sock);
	if (myip.s_addr == 0) {
		printf("net_open: BOOTP failed\n");
		return (EIO);
	}
#endif	/* BOOTPARAMS */

	printf("boot: client addr: %s\n", inet_ntoa(myip));

#ifdef	SUN_BOOTPARAMS
	/* Get our hostname, server IP address, gateway. */
	if (bp_whoami(sock)) {
		printf("net_open: bootparam/whoami RPC failed\n");
		return (EIO);
	}
#endif	/* BOOTPARAMS */

	printf("boot: client name: %s\n", hostname);
	if (gateip.s_addr) {
		printf("boot: subnet mask: %s\n", intoa(netmask));
		printf("boot: net gateway: %s\n", inet_ntoa(gateip));
	}

#ifdef	SUN_BOOTPARAMS
	/* Get the root pathname. */
	if (bp_getfile(sock, "root", &rootip, rootpath)) {
		printf("net_open: bootparam/getfile RPC failed\n");
		return (EIO);
	}
#endif	/* BOOTPARAMS */

	printf("boot: server addr: %s\n", inet_ntoa(rootip));
	printf("boot: server path: %s\n", rootpath);

	return (0);
}
