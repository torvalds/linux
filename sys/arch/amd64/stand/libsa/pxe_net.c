/*	$OpenBSD: pxe_net.c,v 1.5 2020/12/09 18:10:18 krw Exp $	*/
/*	$NetBSD: dev_net.c,v 1.4 2003/03/12 13:15:08 drochner Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Gordon W. Ross.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This module implements a "raw device" interface suitable for
 * use by the stand-alone I/O library NFS and TFTP code.  This interface
 * does not support any "block" access, and exists only for the
 * purpose of initializing the network interface and getting boot
 * parameters.
 *
 * At open time, this does:
 *
 * find interface       - netif_open()
 * BOOTP for IP address - bootp()
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>

#include <lib/libkern/libkern.h>

#include <lib/libsa/stand.h>
#include <lib/libsa/net.h>
#include "pxe_netif.h"
#include "pxe_net.h"

static int netdev_sock = -1;
static int netdev_opens;

int net_getparams(int);

/*
 * Called by devopen after it sets f->f_dev to our devsw entry.
 * This opens the low-level device and sets f->f_devdata.
 * This is declared with variable arguments...
 */
int
net_open(struct open_file *f, ...)
{
	int error = 0;

#ifdef	NETIF_DEBUG
	if (debug)
		printf("net_open()\n");
#endif

	/* On first open, do netif open, mount, etc. */
	if (netdev_opens == 0) {
		/* Find network interface. */
		if (netdev_sock < 0) {
			netdev_sock = pxe_netif_open();
			if (netdev_sock < 0) {
				printf("net_open: netif_open() failed\n");
				return ENXIO;
			}
#ifdef	NETIF_DEBUG
			if (debug)
				printf("net_open: netif_open() succeeded\n");
#endif
		}
#ifdef notyet
		if (rootip.s_addr == 0) {
			/* Get root IP address, and path, etc. */
			error = net_getparams(netdev_sock);
			if (error) {
				/* getparams makes its own noise */
				pxe_netif_close(netdev_sock);
				netdev_sock = -1;
				return error;
			}
		}
#endif
	}
	netdev_opens++;
	f->f_devdata = &netdev_sock;
	return error;
}

int
net_close(struct open_file *f)
{
#ifdef	NETIF_DEBUG
	if (debug)
		printf("net_close: opens=%d\n", netdev_opens);
#endif

	/* On last close, do netif close, etc. */
	f->f_devdata = NULL;
	/* Extra close call? */
	if (netdev_opens <= 0)
		return 0;
	netdev_opens--;
	/* Not last close? */
	if (netdev_opens > 0)
		return 0;
	rootip.s_addr = 0;
	if (netdev_sock >= 0) {
#ifdef	NETIF_DEBUG
		if (debug)
			printf("net_close: calling netif_close()\n");
#endif
		pxe_netif_close(netdev_sock);
		netdev_sock = -1;
	}
	return 0;
}

int
net_ioctl(struct open_file *f, u_long cmd, void *data)
{
	return EIO;
}

int
net_strategy(void *devdata, int rw, daddr_t blk, size_t size, void *buf,
    size_t *rsize)
{
	return EIO;
}


/*
 * Get info for network boot: our IP address, our hostname,
 * server IP address, and our root path on the server.
 */
extern int bootp(int);

int
net_getparams(int sock)
{
	/*
	 * Try to get boot info using BOOTP.  If we succeed, then
	 * the server IP address, gateway, and root path will all
	 * be initialized.  If any remain uninitialized, we will
	 * use RARP and RPC/bootparam (the Sun way) to get them.
	 */
	bootp(sock);
	if (myip.s_addr != 0)
		return 0;
	if (debug)
		printf("net_getparams: BOOTP failed\n");

	return EIO;
}
