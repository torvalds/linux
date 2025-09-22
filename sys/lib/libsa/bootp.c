/*	$OpenBSD: bootp.c,v 1.15 2014/11/19 19:58:40 miod Exp $	*/
/*	$NetBSD: bootp.c,v 1.10 1996/10/13 02:28:59 christos Exp $	*/

/*
 * Copyright (c) 1992 Regents of the University of California.
 * All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
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
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * @(#) Header: bootp.c,v 1.4 93/09/11 03:13:51 leres Exp  (LBL)
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "stand.h"
#include "net.h"
#include "netif.h"
#include "bootp.h"

static u_int32_t	nmask, smask;

static time_t	bot;

static	char vm_rfc1048[4] = VM_RFC1048;
static	char vm_cmu[4] = VM_CMU;

/* Local forwards */
static	ssize_t bootpsend(struct iodesc *, void *, size_t);
static	ssize_t bootprecv(struct iodesc *, void *, size_t, time_t);
static	void vend_cmu(const u_char *);
static	void vend_rfc1048(const u_char *, u_int);

/* Fetch required bootp information */
void
bootp(int sock)
{
	struct iodesc *d;
	struct bootp *bp;
	struct {
		struct packet_header header;
		struct bootp wbootp;
	} wbuf;
	struct {
		struct packet_header header;
		struct bootp rbootp;
	} rbuf;

#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootp: socket=%d\n", sock);
#endif
	if (!bot)
		bot = getsecs();

	if (!(d = socktodesc(sock))) {
		printf("bootp: bad socket. %d\n", sock);
		return;
	}
#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootp: d=%x\n", (u_int)d);
#endif

	bp = &wbuf.wbootp;
	bzero(bp, sizeof(*bp));

	bp->bp_op = BOOTREQUEST;
	bp->bp_htype = HTYPE_ETHERNET;	/* 10Mb Ethernet (48 bits) */
	bp->bp_hlen = 6;
	bp->bp_xid = htonl(d->xid);
	MACPY(d->myea, bp->bp_chaddr);
	bzero(bp->bp_file, sizeof(bp->bp_file));
	bcopy(vm_rfc1048, bp->bp_vend, sizeof(vm_rfc1048));

	d->myip = myip;
	d->myport = htons(IPPORT_BOOTPC);
	d->destip.s_addr = INADDR_BROADCAST;
	d->destport = htons(IPPORT_BOOTPS);

	(void)sendrecv(d,
	    bootpsend, bp, sizeof(*bp),
	    bootprecv, &rbuf.rbootp, sizeof(rbuf.rbootp));

	/* Bump xid so next request will be unique. */
	++d->xid;
}

/* Transmit a bootp request */
static ssize_t
bootpsend(struct iodesc *d, void *pkt, size_t len)
{
	struct bootp *bp;

#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootpsend: d=%x called.\n", (u_int)d);
#endif

	bp = pkt;
	bp->bp_secs = htons((u_short)(getsecs() - bot));

#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootpsend: calling sendudp\n");
#endif

	return (sendudp(d, pkt, len));
}

/* Returns 0 if this is the packet we're waiting for else -1 (and errno == 0) */
static ssize_t
bootprecv(struct iodesc *d, void *pkt, size_t len, time_t tleft)
{
	ssize_t n;
	struct bootp *bp;

#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootprecv: called\n");
#endif

	n = readudp(d, pkt, len, tleft);
	if (n < 0 || (size_t)n < sizeof(struct bootp))
		goto bad;

	bp = (struct bootp *)pkt;

#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootprecv: checked.  bp = 0x%x, n = %d\n",
		    (unsigned)bp, n);
#endif
	if (bp->bp_xid != htonl(d->xid)) {
#ifdef BOOTP_DEBUG
		if (debug) {
			printf("bootprecv: expected xid 0x%lx, got 0x%lx\n",
			    d->xid, ntohl(bp->bp_xid));
		}
#endif
		goto bad;
	}

#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootprecv: got one!\n");
#endif

	/* Pick up our ip address (and natural netmask) */
	myip = d->myip = bp->bp_yiaddr;
#ifdef BOOTP_DEBUG
	if (debug)
		printf("our ip address is %s\n", inet_ntoa(d->myip));
#endif
	if (IN_CLASSA(d->myip.s_addr))
		nmask = IN_CLASSA_NET;
	else if (IN_CLASSB(d->myip.s_addr))
		nmask = IN_CLASSB_NET;
	else
		nmask = IN_CLASSC_NET;
#ifdef BOOTP_DEBUG
	if (debug)
		printf("'native netmask' is %s\n", intoa(nmask));
#endif

	/* Pick up root or swap server address and file spec. */
	if (bp->bp_siaddr.s_addr != 0)
		rootip = bp->bp_siaddr;
	if (bp->bp_file[0] != '\0') {
		strncpy(bootfile, (char *)bp->bp_file, sizeof(bootfile));
		bootfile[sizeof(bootfile) - 1] = '\0';
	}

	/* Suck out vendor info */
	if (bcmp(vm_cmu, bp->bp_vend, sizeof(vm_cmu)) == 0)
		vend_cmu(bp->bp_vend);
	else if (bcmp(vm_rfc1048, bp->bp_vend, sizeof(vm_rfc1048)) == 0)
		vend_rfc1048(bp->bp_vend, sizeof(bp->bp_vend));
	else
		printf("bootprecv: unknown vendor 0x%lx\n", (long)bp->bp_vend);

	/* Check subnet mask against net mask; toss if bogus */
	if ((nmask & smask) != nmask) {
#ifdef BOOTP_DEBUG
		if (debug)
			printf("subnet mask (%s) bad\n", intoa(smask));
#endif
		smask = 0;
	}

	/* Get subnet (or natural net) mask */
	netmask = nmask;
	if (smask)
		netmask = smask;
#ifdef BOOTP_DEBUG
	if (debug)
		printf("mask: %s\n", intoa(netmask));
#endif

	/* We need a gateway if root or swap is on a different net */
	if (!SAMENET(d->myip, rootip, netmask)) {
#ifdef BOOTP_DEBUG
		if (debug)
			printf("need gateway for root ip\n");
#endif
	}

	if (!SAMENET(d->myip, swapip, netmask)) {
#ifdef BOOTP_DEBUG
		if (debug)
			printf("need gateway for swap ip\n");
#endif
	}

	/* Toss gateway if on a different net */
	if (!SAMENET(d->myip, gateip, netmask)) {
#ifdef BOOTP_DEBUG
		if (debug)
			printf("gateway ip (%s) bad\n", inet_ntoa(gateip));
#endif
		gateip.s_addr = 0;
	}

	return (n);

bad:
	errno = 0;
	return (-1);
}

static void
vend_cmu(const u_char *cp)
{
	const struct cmu_vend *vp;

#ifdef BOOTP_DEBUG
	if (debug)
		printf("vend_cmu bootp info.\n");
#endif
	vp = (const struct cmu_vend *)cp;

	if (vp->v_smask.s_addr != 0)
		smask = vp->v_smask.s_addr;
	if (vp->v_dgate.s_addr != 0)
		gateip = vp->v_dgate;
}

static void
vend_rfc1048(const u_char *cp, u_int len)
{
	const u_char *ep;
	int size;
	u_char tag;

#ifdef BOOTP_DEBUG
	if (debug)
		printf("vend_rfc1048 bootp info. len=%d\n", len);
#endif
	ep = cp + len;

	/* Step over magic cookie */
	cp += sizeof(int);

	while (cp < ep) {
		tag = *cp++;
		size = *cp++;
		if (tag == TAG_END)
			break;

		if (tag == TAG_SUBNET_MASK)
			bcopy(cp, &smask, sizeof(smask));
		if (tag == TAG_GATEWAY)
			bcopy(cp, &gateip.s_addr, sizeof(gateip.s_addr));
		if (tag == TAG_SWAPSERVER)
			bcopy(cp, &swapip.s_addr, sizeof(swapip.s_addr));
		if (tag == TAG_DOMAIN_SERVER)
			bcopy(cp, &nameip.s_addr, sizeof(nameip.s_addr));
		if (tag == TAG_ROOTPATH) {
			strncpy(rootpath, (char *)cp, sizeof(rootpath));
			rootpath[size] = '\0';
		}
		if (tag == TAG_HOSTNAME) {
			strncpy(hostname, (char *)cp, sizeof(hostname));
			hostname[size] = '\0';
		}
		if (tag == TAG_DOMAINNAME) {
			strncpy(domainname, (char *)cp, sizeof(domainname));
			domainname[size] = '\0';
		}
		cp += size;
	}
}
