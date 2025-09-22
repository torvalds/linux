/*	$OpenBSD: pxe.c,v 1.8 2022/12/27 07:34:05 jca Exp $ */
/*	$NetBSD: pxe.c,v 1.5 2003/03/11 18:29:00 drochner Exp $	*/

/*
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2000 Alfred Perlstein <alfred@freebsd.org>
 * All rights reserved.
 * Copyright (c) 2000 Paul Saab <ps@freebsd.org>
 * All rights reserved.
 * Copyright (c) 2000 John Baldwin <jhb@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Support for the Intel Preboot Execution Environment (PXE).
 *
 * PXE provides a UDP implementation as well as a UNDI network device
 * driver.  UNDI is much more complicated to use than PXE UDP, so we
 * use PXE UDP as a cheap and easy way to get PXE support.
 */

#include <sys/param.h>
#include <sys/socket.h>

#ifdef _STANDALONE
#include <lib/libkern/libkern.h>
#else
#include <string.h>
#endif

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include <lib/libsa/stand.h>
#include <lib/libsa/net.h>
#include <lib/libsa/bootp.h>

#include <stand/boot/bootarg.h>
#include <machine/biosvar.h>

#include "pxeboot.h"
#include "pxe.h"
#include "pxe_netif.h"

void	(*pxe_call)(u_int16_t);

void	pxecall_bangpxe(u_int16_t);	/* pxe_call.S */
void	pxecall_pxenv(u_int16_t);	/* pxe_call.S */

char pxe_command_buf[256];

BOOTPLAYER bootplayer;

struct in_addr servip;			/* for tftp */	/* XXX init this */

extern char *bootmac;			/* To pass to kernel */

/* static struct btinfo_netif bi_netif; */

/*****************************************************************************
 * This section is a replacement for libsa/udp.c
 *****************************************************************************/

/* Caller must leave room for ethernet, ip, and udp headers in front!! */
ssize_t
pxesendudp(struct iodesc *d, void *pkt, size_t len)
{
	t_PXENV_UDP_WRITE *uw = (void *) pxe_command_buf;

	uw->status = 0;

	uw->ip = d->destip.s_addr;
	uw->gw = gateip.s_addr;
	uw->src_port = d->myport;
	uw->dst_port = d->destport;
	uw->buffer_size = len;
	uw->buffer.segment = VTOPSEG(pkt);
	uw->buffer.offset = VTOPOFF(pkt);

	pxe_call(PXENV_UDP_WRITE);

	if (uw->status != PXENV_STATUS_SUCCESS) {
		/* XXX This happens a lot; it shouldn't. */
		if (uw->status != PXENV_STATUS_FAILURE)
			printf("sendudp: PXENV_UDP_WRITE failed: 0x%x\n",
			    uw->status);
		return -1;
	}

	return len;
}

/*
 * Receive a UDP packet and validate it for us.
 * Caller leaves room for the headers (Ether, IP, UDP).
 */
ssize_t
pxereadudp(struct iodesc *d, void *pkt, size_t len, time_t tleft)
{
	t_PXENV_UDP_READ *ur = (void *) pxe_command_buf;
	struct udphdr *uh;
	struct ip *ip;

	uh = (struct udphdr *)pkt - 1;
	ip = (struct ip *)uh - 1;

	bzero(ur, sizeof(*ur));

	ur->dest_ip = d->myip.s_addr;
	ur->d_port = d->myport;
	ur->buffer_size = len;
	ur->buffer.segment = VTOPSEG(pkt);
	ur->buffer.offset = VTOPOFF(pkt);

	/* XXX Timeout unused. */

	pxe_call(PXENV_UDP_READ);

	if (ur->status != PXENV_STATUS_SUCCESS) {
		/* XXX This happens a lot; it shouldn't. */
		if (ur->status != PXENV_STATUS_FAILURE)
			printf("readudp: PXENV_UDP_READ_failed: 0x%0x\n",
			    ur->status);
		return -1;
	}

	ip->ip_src.s_addr = ur->src_ip;
	uh->uh_sport = ur->s_port;
	uh->uh_dport = d->myport;

	return ur->buffer_size;
}

/*
 * netif layer:
 *  open, close, shutdown: called from dev_net.c
 *  socktodesc: called by network protocol modules
 *
 * We only allow one open socket.
 */

static int pxe_inited;
static struct iodesc desc;

int
pxe_netif_open(void)
{
	t_PXENV_UDP_OPEN *uo = (void *) pxe_command_buf;

#ifdef NETIF_DEBUG
	printf("pxe_netif_open()\n");
#endif
	if (!pxe_inited) {
		if (pxe_init(0) != 0)
			return -1;
		pxe_inited = 1;
	}
	/* BI_ADD(&bi_netif, BTINFO_NETIF, sizeof(bi_netif)); */

	bzero(uo, sizeof(*uo));

	uo->src_ip = bootplayer.yip;

	pxe_call(PXENV_UDP_OPEN);

	if (uo->status != PXENV_STATUS_SUCCESS) {
		printf("\npxe_netif_open: PXENV_UDP_OPEN failed: 0x%x\n",
		    uo->status);
		return -1;
	}

	bcopy(bootplayer.CAddr, desc.myea, ETHER_ADDR_LEN);
	bootmac = bootplayer.CAddr;

	/*
	 * Since the PXE BIOS has already done DHCP, make sure we
	 * don't reuse any of its transaction IDs.
	 */
	desc.xid = bootplayer.ident;

	return 0;
}

void
pxe_netif_close(int sock)
{
	t_PXENV_UDP_CLOSE *uc = (void *) pxe_command_buf;

#ifdef NETIF_DEBUG
	if (sock != 0)
		printf("pxe_netif_close: sock=%d\n", sock);
#endif

	uc->status = 0;

	pxe_call(PXENV_UDP_CLOSE);

	if (uc->status != PXENV_STATUS_SUCCESS)
		printf("pxe_netif_end: PXENV_UDP_CLOSE failed: 0x%x\n",
		    uc->status);
}

void
pxe_netif_shutdown(void)
{
#ifdef NETIF_DEBUG
	printf("pxe_netif_shutdown()\n");
#endif

	pxe_shutdown();
}

struct iodesc *
pxesocktodesc(int sock)
{

#ifdef NETIF_DEBUG
	if (sock != 0)
		return 0;
	else
#endif
		return &desc;
}

/*****************************************************************************
 * PXE initialization and support routines
 *****************************************************************************/

u_int16_t pxe_command_buf_seg;
u_int16_t pxe_command_buf_off;

extern u_int16_t bangpxe_off, bangpxe_seg;
extern u_int16_t pxenv_off, pxenv_seg;

/* static struct btinfo_netif bi_netif; */

void
pxeprobe(void)
{
	if (!pxe_inited) {
		if (pxe_init(1) == 0) {
			pxe_inited = 1;
		}
	}
}

int
pxe_init(int quiet)
{
	t_PXENV_GET_CACHED_INFO *gci = (void *) pxe_command_buf;
	pxenv_t *pxenv;
	pxe_t *pxe;
	char *cp;
	int i;
	u_int8_t cksum, *ucp;

	/*
	 * Checking for the presence of PXE is a machine-dependent
	 * operation.  On the IA-32, this can be done two ways:
	 *
	 *	Int 0x1a function 0x5650
	 *
	 *	Scan memory for the !PXE or PXENV+ signatures
	 *
	 * We do the latter, since the Int method returns a pointer
	 * to a deprecated structure (PXENV+).
	 */

	pxenv = NULL;
	pxe = NULL;

	for (cp = (char *)0xa0000; cp > (char *)0x10000; cp -= 2) {
		if (pxenv == NULL) {
			pxenv = (pxenv_t *)cp;
			if (memcmp(pxenv->Signature, S_SIZE("PXENV+")) != 0)
				pxenv = NULL;
			else {
				for (i = 0, ucp = (u_int8_t *)cp, cksum = 0;
				     i < pxenv->Length; i++)
					cksum += ucp[i];
				if (cksum != 0) {
					printf("\npxe_init: bad cksum (0x%x) "
					    "for PXENV+ at 0x%lx\n", cksum,
					    (u_long) cp);
					pxenv = NULL;
				}
			}
		}

		if (pxe == NULL) {
			pxe = (pxe_t *)cp;
			if (memcmp(pxe->Signature, S_SIZE("!PXE")) != 0)
				pxe = NULL;
			else {
				for (i = 0, ucp = (u_int8_t *)cp, cksum = 0;
				     i < pxe->StructLength; i++)
					cksum += ucp[i];
				if (cksum != 0) {
					printf("pxe_init: bad cksum (0x%x) "
					    "for !PXE at 0x%lx\n", cksum,
					    (u_long) cp);
					pxe = NULL;
				}
			}
		}

		if (pxe != NULL && pxenv != NULL)
			break;
	}

	if (pxe == NULL && pxenv == NULL) {
		if (!quiet) printf("pxe_init: No PXE BIOS found.\n");
		return 1;
	}

	if (pxenv == NULL) {
		/* assert(pxe != NULL); */

		printf(quiet ? " pxe!" : "PXE present\n");
	} else {				/* pxenv != NULL */
		int bang = 0;

		if (pxenv->Version >= 0x0201 && pxe != NULL) {
			/* 2.1 or greater -- don't use PXENV+ */
			bang = 1;
		}

		if (quiet) {
			printf(" pxe%c[%d.%d]",
			    (bang ? '!' : '+'),
			    (pxenv->Version >> 8) & 0xff,
			     pxenv->Version & 0xff);
		} else {
			printf("PXE BIOS Version %d.%d\n",
			    (pxenv->Version >> 8) & 0xff,
			     pxenv->Version & 0xff);
		}

		if (bang) {
			pxenv = NULL;
		}
	}

	if (pxenv == NULL) {
		pxe_call = pxecall_bangpxe;
		bangpxe_off = pxe->EntryPointSP.offset;
		bangpxe_seg = pxe->EntryPointSP.segment;
	} else {
		pxe_call = pxecall_pxenv;
		pxenv_off = pxenv->RMEntry.offset;
		pxenv_seg = pxenv->RMEntry.segment;
	}

	/*
	 * Pre-compute the segment/offset of the pxe_command_buf
	 * to make things nicer in the low-level calling glue.
	 */
	pxe_command_buf_seg = VTOPSEG(pxe_command_buf);
	pxe_command_buf_off = VTOPOFF(pxe_command_buf);

	/*
	 * Get the cached info from the server's Discovery reply packet.
	 */
	bzero(gci, sizeof(*gci));
	gci->PacketType = PXENV_PACKET_TYPE_CACHED_REPLY;
	pxe_call(PXENV_GET_CACHED_INFO);

	if (gci->Status != PXENV_STATUS_SUCCESS) {
		printf("\npxeinfo: PXENV_GET_CACHED_INFO failed: 0x%x\n",
		    gci->Status);
		return 1;
	}

	memcpy(&bootplayer,
	    SEGOFF2FLAT(gci->Buffer.segment, gci->Buffer.offset),
	    gci->BufferSize);

	bcopy(&bootplayer.yip, &myip.s_addr, sizeof(myip.s_addr));
	bcopy(&bootplayer.sip, &servip.s_addr, sizeof(servip.s_addr));

        /* Compute our "natural" netmask. */
	if (IN_CLASSA(myip.s_addr))
		netmask = IN_CLASSA_NET;
	else if (IN_CLASSB(myip.s_addr))
		netmask = IN_CLASSB_NET;
	else
		netmask = IN_CLASSC_NET;

	return 0;
}

void
pxeinfo(void)
{
	u_int8_t *p;
#ifdef PXE_DEBUG
	t_PXENV_UNDI_GET_NIC_TYPE *gnt = (void *) pxe_command_buf;
#endif

	printf(" mac %s", ether_sprintf(bootplayer.CAddr));
	p = (u_int8_t *)&myip.s_addr;
	printf(", ip %d.%d.%d.%d", p[0], p[1], p[2], p[3]);
	p = (u_int8_t *)&servip.s_addr;
	printf(", server %d.%d.%d.%d", p[0], p[1], p[2], p[3]);

#ifdef PXE_DEBUG
	/*
	 * Get network interface information.
	 */
	bzero(gnt, sizeof(*gnt));
	pxe_call(PXENV_UNDI_GET_NIC_TYPE);

	if (gnt->Status != PXENV_STATUS_SUCCESS) {
		printf("\npxeinfo: PXENV_UNDI_GET_NIC_TYPE failed: 0x%x\n",
		    gnt->Status);
		return;
	}

	switch (gnt->NicType) {
	case PCI_NIC:
	case CardBus_NIC:
		/* strncpy(bi_netif.ifname, "pxe", sizeof(bi_netif.ifname)); */
		/* bi_netif.bus = BI_BUS_PCI; */
		/* bi_netif.addr.tag = gnt->info.pci.BusDevFunc; */

		printf("\nPXE: Using %s device at bus %d device %d function %d\n",
		    gnt->NicType == PCI_NIC ? "PCI" : "CardBus",
		    (gnt->info.pci.BusDevFunc >> 8) & 0xff,
		    (gnt->info.pci.BusDevFunc >> 3) & 0x1f,
		    gnt->info.pci.BusDevFunc & 0x7);
		break;

	case PnP_NIC:
		/* XXX Make bootinfo work with this. */
		printf("\nPXE: Using PnP device at 0x%x\n",
		    gnt->info.pnp.CardSelNum);
	}
#endif
}

void
pxe_shutdown(void)
{
	int try;
	t_PXENV_UNLOAD_STACK *unload = (void *) pxe_command_buf;
	t_PXENV_UNDI_SHUTDOWN *shutdown = (void *) pxe_command_buf;
#ifdef PXE_DEBUG
	t_PXENV_UDP_CLOSE *close = (void *) pxe_command_buf;
#endif

	if (pxe_call == NULL)
		return;

	/* Close any open UDP connections.  Ignore return value. */
	pxe_call(PXENV_UDP_CLOSE);
#ifdef PXE_DEBUG
	printf("pxe_shutdown: PXENV_UDP_CLOSE returned 0x%x\n", close->status);
#endif

	/* Sometimes PXENV_UNDI_SHUTDOWN doesn't work at first */
	for (try = 3; try > 0; try--) {
		pxe_call(PXENV_UNDI_SHUTDOWN);

		if (shutdown->Status == PXENV_STATUS_SUCCESS)
			break;

		printf("pxe_shutdown: PXENV_UNDI_SHUTDOWN failed: 0x%x\n",
		    shutdown->Status);

		if (try != 1)
			sleep(1);
	}

	/* Have multiple attempts at PXENV_UNLOAD_STACK, too */
	for (try = 3; try > 0; try--) {
		pxe_call(PXENV_UNLOAD_STACK);

		if (unload->Status == PXENV_STATUS_SUCCESS)
			break;

		printf("pxe_shutdown: PXENV_UNLOAD_STACK failed: 0x%x\n",
		    unload->Status);

		if (try != 1)
			sleep(1);
	}
}
