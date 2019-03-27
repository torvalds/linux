/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1992 The University of Utah and the Center
 *	for Software Science (CSS).
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Center for Software Science of the University of Utah Computer
 * Science Department.  CSS requests users of this software to return
 * to css-dist@cs.utah.edu any improvements that they make and grant
 * CSS redistribution rights.
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
 *	from: @(#)bpf.c	8.1 (Berkeley) 6/4/93
 *
 * From: Utah Hdr: bpf.c 3.1 92/07/06
 * Author: Jeff Forys, University of Utah CSS
 */

#ifndef lint
#if 0
static const char sccsid[] = "@(#)bpf.c	8.1 (Berkeley) 6/4/93";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/bpf.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "defs.h"
#include "pathnames.h"

static int BpfFd = -1;
static unsigned BpfLen = 0;
static u_int8_t *BpfPkt = NULL;

/*
**  BpfOpen -- Open and initialize a BPF device.
**
**	Parameters:
**		None.
**
**	Returns:
**		File descriptor of opened BPF device (for select() etc).
**
**	Side Effects:
**		If an error is encountered, the program terminates here.
*/
int
BpfOpen(void)
{
	struct ifreq ifr;
	char bpfdev[32];
	int n = 0;

	/*
	 *  Open the first available BPF device.
	 */
	do {
		(void) sprintf(bpfdev, _PATH_BPF, n++);
		BpfFd = open(bpfdev, O_RDWR);
	} while (BpfFd < 0 && (errno == EBUSY || errno == EPERM));

	if (BpfFd < 0) {
		syslog(LOG_ERR, "bpf: no available devices: %m");
		Exit(0);
	}

	/*
	 *  Set interface name for bpf device, get data link layer
	 *  type and make sure it's type Ethernet.
	 */
	(void) strncpy(ifr.ifr_name, IntfName, sizeof(ifr.ifr_name));
	if (ioctl(BpfFd, BIOCSETIF, (caddr_t)&ifr) < 0) {
		syslog(LOG_ERR, "bpf: ioctl(BIOCSETIF,%s): %m", IntfName);
		Exit(0);
	}

	/*
	 *  Make sure we are dealing with an Ethernet device.
	 */
	if (ioctl(BpfFd, BIOCGDLT, (caddr_t)&n) < 0) {
		syslog(LOG_ERR, "bpf: ioctl(BIOCGDLT): %m");
		Exit(0);
	}
	if (n != DLT_EN10MB) {
		syslog(LOG_ERR,"bpf: %s: data-link type %d unsupported",
		       IntfName, n);
		Exit(0);
	}

	/*
	 *  On read(), return packets immediately (do not buffer them).
	 */
	n = 1;
	if (ioctl(BpfFd, BIOCIMMEDIATE, (caddr_t)&n) < 0) {
		syslog(LOG_ERR, "bpf: ioctl(BIOCIMMEDIATE): %m");
		Exit(0);
	}

	/*
	 *  Try to enable the chip/driver's multicast address filter to
	 *  grab our RMP address.  If this fails, try promiscuous mode.
	 *  If this fails, there's no way we are going to get any RMP
	 *  packets so just exit here.
	 */
#ifdef MSG_EOR
	ifr.ifr_addr.sa_len = RMP_ADDRLEN + 2;
#endif
	ifr.ifr_addr.sa_family = AF_UNSPEC;
	memmove((char *)&ifr.ifr_addr.sa_data[0], &RmpMcastAddr[0], RMP_ADDRLEN);
	if (ioctl(BpfFd, BIOCPROMISC, (caddr_t)0) < 0) {
		syslog(LOG_ERR, "bpf: can't set promiscuous mode: %m");
		Exit(0);
	}

	/*
	 *  Ask BPF how much buffer space it requires and allocate one.
	 */
	if (ioctl(BpfFd, BIOCGBLEN, (caddr_t)&BpfLen) < 0) {
		syslog(LOG_ERR, "bpf: ioctl(BIOCGBLEN): %m");
		Exit(0);
	}
	if (BpfPkt == NULL)
		BpfPkt = (u_int8_t *)malloc(BpfLen);

	if (BpfPkt == NULL) {
		syslog(LOG_ERR, "bpf: out of memory (%u bytes for bpfpkt)",
		       BpfLen);
		Exit(0);
	}

	/*
	 *  Write a little program to snarf RMP Boot packets and stuff
	 *  it down BPF's throat (i.e. set up the packet filter).
	 */
	{
#define	RMP	((struct rmp_packet *)0)
		static struct bpf_insn bpf_insn[] = {
		    { BPF_LD|BPF_B|BPF_ABS,  0, 0, (long)&RMP->hp_llc.dsap },
		    { BPF_JMP|BPF_JEQ|BPF_K, 0, 5, IEEE_DSAP_HP },
		    { BPF_LD|BPF_H|BPF_ABS,  0, 0, (long)&RMP->hp_llc.cntrl },
		    { BPF_JMP|BPF_JEQ|BPF_K, 0, 3, IEEE_CNTL_HP },
		    { BPF_LD|BPF_H|BPF_ABS,  0, 0, (long)&RMP->hp_llc.dxsap },
		    { BPF_JMP|BPF_JEQ|BPF_K, 0, 1, HPEXT_DXSAP },
		    { BPF_RET|BPF_K,         0, 0, RMP_MAX_PACKET },
		    { BPF_RET|BPF_K,         0, 0, 0x0 }
		};
#undef	RMP
		static struct bpf_program bpf_pgm = {
		    sizeof(bpf_insn)/sizeof(bpf_insn[0]), bpf_insn
		};

		if (ioctl(BpfFd, BIOCSETF, (caddr_t)&bpf_pgm) < 0) {
			syslog(LOG_ERR, "bpf: ioctl(BIOCSETF): %m");
			Exit(0);
		}
	}

	return(BpfFd);
}

/*
**  BPF GetIntfName -- Return the name of a network interface attached to
**		the system, or 0 if none can be found.  The interface
**		must be configured up; the lowest unit number is
**		preferred; loopback is ignored.
**
**	Parameters:
**		errmsg - if no network interface found, *errmsg explains why.
**
**	Returns:
**		A (static) pointer to interface name, or NULL on error.
**
**	Side Effects:
**		None.
*/
char *
BpfGetIntfName(char **errmsg)
{
	struct ifreq ibuf[8], *ifrp, *ifend, *mp;
	struct ifconf ifc;
	int fd;
	int minunit, n;
	char *cp;
	static char device[sizeof(ifrp->ifr_name)];
	static char errbuf[128] = "No Error!";

	if (errmsg != NULL)
		*errmsg = errbuf;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		(void) strcpy(errbuf, "bpf: socket: %m");
		return(NULL);
	}
	ifc.ifc_len = sizeof ibuf;
	ifc.ifc_buf = (caddr_t)ibuf;

	if (ioctl(fd, SIOCGIFCONF, (char *)&ifc) < 0 ||
	    ifc.ifc_len < sizeof(struct ifreq)) {
		(void) strcpy(errbuf, "bpf: ioctl(SIOCGIFCONF): %m");
		return(NULL);
	}
	ifrp = ibuf;
	ifend = (struct ifreq *)((char *)ibuf + ifc.ifc_len);

	mp = NULL;
	minunit = 666;
	for (; ifrp < ifend; ++ifrp) {
		if (ioctl(fd, SIOCGIFFLAGS, (char *)ifrp) < 0) {
			(void) strcpy(errbuf, "bpf: ioctl(SIOCGIFFLAGS): %m");
			return(NULL);
		}

		/*
		 *  If interface is down or this is the loopback interface,
		 *  ignore it.
		 */
		if ((ifrp->ifr_flags & IFF_UP) == 0 ||
#ifdef IFF_LOOPBACK
		    (ifrp->ifr_flags & IFF_LOOPBACK))
#else
		    (strcmp(ifrp->ifr_name, "lo0") == 0))
#endif
			continue;

		for (cp = ifrp->ifr_name; !isdigit(*cp); ++cp)
			;
		n = atoi(cp);
		if (n < minunit) {
			minunit = n;
			mp = ifrp;
		}
	}

	(void) close(fd);
	if (mp == NULL) {
		(void) strcpy(errbuf, "bpf: no interfaces found");
		return(NULL);
	}

	(void) strcpy(device, mp->ifr_name);
	return(device);
}

/*
**  BpfRead -- Read packets from a BPF device and fill in `rconn'.
**
**	Parameters:
**		rconn - filled in with next packet.
**		doread - is True if we can issue a read() syscall.
**
**	Returns:
**		True if `rconn' contains a new packet, False otherwise.
**
**	Side Effects:
**		None.
*/
int
BpfRead(RMPCONN *rconn, int doread)
{
	int datlen, caplen, hdrlen;
	static u_int8_t *bp = NULL, *ep = NULL;
	int cc;

	/*
	 *  The read() may block, or it may return one or more packets.
	 *  We let the caller decide whether or not we can issue a read().
	 */
	if (doread) {
		if ((cc = read(BpfFd, (char *)BpfPkt, (int)BpfLen)) < 0) {
			syslog(LOG_ERR, "bpf: read: %m");
			return(0);
		} else {
			bp = BpfPkt;
			ep = BpfPkt + cc;
		}
	}

#define bhp ((struct bpf_hdr *)bp)
	/*
	 *  If there is a new packet in the buffer, stuff it into `rconn'
	 *  and return a success indication.
	 */
	if (bp < ep) {
		datlen = bhp->bh_datalen;
		caplen = bhp->bh_caplen;
		hdrlen = bhp->bh_hdrlen;

		if (caplen != datlen)
			syslog(LOG_ERR,
			       "bpf: short packet dropped (%d of %d bytes)",
			       caplen, datlen);
		else if (caplen > sizeof(struct rmp_packet))
			syslog(LOG_ERR, "bpf: large packet dropped (%d bytes)",
			       caplen);
		else {
			rconn->rmplen = caplen;
			memmove((char *)&rconn->tstamp, (char *)&bhp->bh_tstamp,
			      sizeof(struct timeval));
			memmove((char *)&rconn->rmp, (char *)bp + hdrlen, caplen);
		}
		bp += BPF_WORDALIGN(caplen + hdrlen);
		return(1);
	}
#undef bhp

	return(0);
}

/*
**  BpfWrite -- Write packet to BPF device.
**
**	Parameters:
**		rconn - packet to send.
**
**	Returns:
**		True if write succeeded, False otherwise.
**
**	Side Effects:
**		None.
*/
int
BpfWrite(RMPCONN *rconn)
{
	if (write(BpfFd, (char *)&rconn->rmp, rconn->rmplen) < 0) {
		syslog(LOG_ERR, "write: %s: %m", EnetStr(rconn));
		return(0);
	}

	return(1);
}

/*
**  BpfClose -- Close a BPF device.
**
**	Parameters:
**		None.
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		None.
*/
void
BpfClose(void)
{
	struct ifreq ifr;

	if (BpfPkt != NULL) {
		free((char *)BpfPkt);
		BpfPkt = NULL;
	}

	if (BpfFd == -1)
		return;

#ifdef MSG_EOR
	ifr.ifr_addr.sa_len = RMP_ADDRLEN + 2;
#endif
	ifr.ifr_addr.sa_family = AF_UNSPEC;
	memmove((char *)&ifr.ifr_addr.sa_data[0], &RmpMcastAddr[0], RMP_ADDRLEN);
	if (ioctl(BpfFd, SIOCDELMULTI, (caddr_t)&ifr) < 0)
		(void) ioctl(BpfFd, BIOCPROMISC, (caddr_t)0);

	(void) close(BpfFd);
	BpfFd = -1;
}
