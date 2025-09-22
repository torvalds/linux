/*	$OpenBSD: netif_of.c,v 1.9 2023/06/03 21:37:53 krw Exp $	*/
/*	$NetBSD: netif_of.c,v 1.1 2000/08/20 14:58:39 mrg Exp $	*/

/*
 * Copyright (C) 1995 Wolfgang Solfrank.
 * Copyright (C) 1995 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Open Firmware does most of the job for interfacing to the hardware,
 * so it is easiest to just replace the netif module with
 * this adaptation to the PROM network interface.
 *
 * Note: this is based in part on sys/arch/sparc/stand/netif_sun.c
 */

#include <sys/param.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <lib/libsa/stand.h>
#include <lib/libsa/net.h>
#include <lib/libsa/netif.h>

#include "ofdev.h"
#include "openfirm.h"

static struct netif netif_of;

struct iodesc sockets[SOPEN_MAX];

struct iodesc *
socktodesc(int sock)
{
	if (sock != 0)
		return NULL;
	return sockets;
}

int
netif_open(void *machdep_hint)
{
	struct of_dev *op = machdep_hint;
	struct iodesc *io;

	DNPRINTF(BOOT_D_OFNET, "netif_open...");

	/* find a free socket */
	io = sockets;
	if (io->io_netif) {
		DNPRINTF(BOOT_D_OFNET, "device busy\n");
		errno = ENFILE;
		return -1;
	}
	bzero(io, sizeof *io);

	netif_of.nif_devdata = op;
	io->io_netif = &netif_of;

	/* Put our ethernet address in io->myea */
	OF_getprop(OF_instance_to_package(op->handle),
		   "mac-address", io->myea, sizeof io->myea);

	DNPRINTF(BOOT_D_OFNET, "OK\n");
	return 0;
}

int
netif_close(int fd)
{
	struct iodesc *io;
	struct netif *ni;

	DNPRINTF(BOOT_D_OFNET, "netif_close(%x)...", fd);

	if (fd != 0) {
		DNPRINTF(BOOT_D_OFNET, "EBADF\n");
		errno = EBADF;
		return -1;
	}

	io = &sockets[fd];
	ni = io->io_netif;
	if (ni != NULL) {
		ni->nif_devdata = NULL;
		io->io_netif = NULL;
	}

	DNPRINTF(BOOT_D_OFNET, "OK\n");
	return 0;
}

/*
 * Send a packet.  The ether header is already there.
 * Return the length sent (or -1 on error).
 */
ssize_t
netif_put(struct iodesc *desc, void *pkt, size_t len)
{
	struct of_dev *op;
	ssize_t rv;
	size_t sendlen;

	op = desc->io_netif->nif_devdata;

	DNPRINTF(BOOT_D_OFNET, "netif_put: desc=0x%x pkt=0x%x len=%d\n",
	    desc, pkt, len);
	DNPRINTF(BOOT_D_OFNET, "dst: %s, src: %s, type: 0x%x\n",
	    ether_sprintf(((struct ether_header *)pkt)->ether_dhost),
	    ether_sprintf(((struct ether_header *)pkt)->ether_shost),
	    ((struct ether_header *)pkt)->ether_type & 0xFFFF);

	sendlen = len;
	if (sendlen < 60) {
		sendlen = 60;
		DNPRINTF(BOOT_D_OFNET, "netif_put: length padded to %d\n",
		    sendlen);
	}

	rv = OF_write(op->handle, pkt, sendlen);

	DNPRINTF(BOOT_D_OFNET, "netif_put: xmit returned %d\n", rv);

	return rv;
}

/*
 * Receive a packet, including the ether header.
 * Return the total length received (or -1 on error).
 */
ssize_t
netif_get(struct iodesc *desc, void *pkt, size_t maxlen, time_t timo)
{
	struct of_dev *op;
	int tick0, tmo_ms;
	int len;

	op = desc->io_netif->nif_devdata;

	DNPRINTF(BOOT_D_OFNET, "netif_get: pkt=0x%x, maxlen=%d, tmo=%d\n",
	    pkt, maxlen, timo);

	tmo_ms = timo * 1000;
	tick0 = OF_milliseconds();

	do {
		len = OF_read(op->handle, pkt, maxlen);
	} while ((len == -2 || len == 0) &&
		 (OF_milliseconds() - tick0 < tmo_ms));

	DNPRINTF(BOOT_D_OFNET, "netif_get: received len=%d\n", len);

	if (len < 12)
		return -1;

	DNPRINTF(BOOT_D_OFNET, "dst: %s, src: %s, type: 0x%x\n",
	    ether_sprintf(((struct ether_header *)pkt)->ether_dhost),
	    ether_sprintf(((struct ether_header *)pkt)->ether_shost),
	    ((struct ether_header *)pkt)->ether_type & 0xFFFF);

	return len;
}

/*
 * Shouldn't really be here, but is used solely for networking, so...
 */
time_t
getsecs(void)
{
	return OF_milliseconds() / 1000;
}
