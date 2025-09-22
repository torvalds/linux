/*	$OpenBSD: bpf.c,v 1.27 2019/06/28 13:32:50 deraadt Exp $	*/
/*	$NetBSD: bpf.c,v 1.5.2.1 1995/11/14 08:45:42 thorpej Exp $	*/

/*
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

#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/bpf.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>
#include <ifaddrs.h>
#include "defs.h"

static int BpfFd = -1;
static unsigned int BpfLen = 0;
static u_int8_t *BpfPkt = NULL;

/*
**  BpfOpen -- Open and initialize a BPF device.
**
**	Parameters:
**		None.
**
**	Returns:
**		File descriptor of opened BPF device
**
**	Side Effects:
**		If an error is encountered, the program terminates here.
*/
int
BpfOpen(void)
{
	struct ifreq ifr;
	int n;

	if ((BpfFd = open("/dev/bpf", O_RDWR)) == -1) {
		syslog(LOG_ERR, "bpf: can't open device: %m");
		DoExit();
	}

	/*
	 *  Set interface name for bpf device, get data link layer
	 *  type and make sure it's type Ethernet.
	 */
	(void) strncpy(ifr.ifr_name, IntfName, sizeof(ifr.ifr_name));
	if (ioctl(BpfFd, BIOCSETIF, (caddr_t)&ifr) == -1) {
		syslog(LOG_ERR, "bpf: ioctl(BIOCSETIF,%s): %m", IntfName);
		DoExit();
	}

	/*
	 *  Make sure we are dealing with an Ethernet device.
	 */
	if (ioctl(BpfFd, BIOCGDLT, (caddr_t)&n) == -1) {
		syslog(LOG_ERR, "bpf: ioctl(BIOCGDLT): %m");
		DoExit();
	}
	if (n != DLT_EN10MB) {
		syslog(LOG_ERR,"bpf: %s: data-link type %d unsupported",
		    IntfName, n);
		DoExit();
	}

	/*
	 *  On read(), return packets immediately (do not buffer them).
	 */
	n = 1;
	if (ioctl(BpfFd, BIOCIMMEDIATE, (caddr_t)&n) == -1) {
		syslog(LOG_ERR, "bpf: ioctl(BIOCIMMEDIATE): %m");
		DoExit();
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
	bcopy(&RmpMcastAddr[0], (char *)&ifr.ifr_addr.sa_data[0], RMP_ADDRLEN);
	if (ioctl(BpfFd, SIOCADDMULTI, (caddr_t)&ifr) == -1) {
		syslog(LOG_WARNING,
		    "bpf: can't add mcast addr (%m), setting promiscuous mode");

		if (ioctl(BpfFd, BIOCPROMISC, (caddr_t)0) == -1) {
			syslog(LOG_ERR, "bpf: can't set promiscuous mode: %m");
			DoExit();
		}
	}

	/*
	 *  Ask BPF how much buffer space it requires and allocate one.
	 */
	if (ioctl(BpfFd, BIOCGBLEN, (caddr_t)&BpfLen) == -1) {
		syslog(LOG_ERR, "bpf: ioctl(BIOCGBLEN): %m");
		DoExit();
	}
	if (BpfPkt == NULL)
		BpfPkt = malloc(BpfLen);

	if (BpfPkt == NULL) {
		syslog(LOG_ERR, "bpf: out of memory (%u bytes for bpfpkt)",
		    BpfLen);
		DoExit();
	}

	/*
	 *  Write a little program to snarf RMP Boot packets and stuff
	 *  it down BPF's throat (i.e. set up the packet filter).
	 */
	{
#define	RMP(field)	offsetof(struct rmp_packet, field)
		static struct bpf_insn bpf_insn[] = {
		    /* make sure it is a 802.3 packet */
		    { BPF_LD|BPF_H|BPF_ABS,  0, 0, RMP(hp_hdr.len) },
		    { BPF_JMP|BPF_JGE|BPF_K, 7, 0, 0x600 },

		    { BPF_LD|BPF_B|BPF_ABS,  0, 0, RMP(hp_llc.dsap) },
		    { BPF_JMP|BPF_JEQ|BPF_K, 0, 5, IEEE_DSAP_HP },
		    { BPF_LD|BPF_H|BPF_ABS,  0, 0, RMP(hp_llc.cntrl) },
		    { BPF_JMP|BPF_JEQ|BPF_K, 0, 3, IEEE_CNTL_HP },
		    { BPF_LD|BPF_H|BPF_ABS,  0, 0, RMP(hp_llc.dxsap) },
		    { BPF_JMP|BPF_JEQ|BPF_K, 0, 1, HPEXT_DXSAP },
		    { BPF_RET|BPF_K,         0, 0, RMP_MAX_PACKET },
		    { BPF_RET|BPF_K,         0, 0, 0x0 }
		};

		static struct bpf_insn bpf_wf_insn[] = {
		    /* make sure it is a 802.3 packet */
		    { BPF_LD|BPF_H|BPF_ABS,  0, 0, RMP(hp_hdr.len) },
		    { BPF_JMP|BPF_JGE|BPF_K, 12, 0, 0x600 },

		    /* check the SNAP header */
		    { BPF_LD|BPF_B|BPF_ABS,  0, 0, RMP(hp_llc.dsap) },
		    { BPF_JMP|BPF_JEQ|BPF_K, 0, 10, IEEE_DSAP_HP },
		    { BPF_LD|BPF_H|BPF_ABS,  0, 0, RMP(hp_llc.cntrl) },
		    { BPF_JMP|BPF_JEQ|BPF_K, 0, 8, IEEE_CNTL_HP },

		    { BPF_LD|BPF_H|BPF_ABS,  0, 0, RMP(hp_llc.sxsap) },
		    { BPF_JMP|BPF_JEQ|BPF_K, 0, 6, HPEXT_DXSAP },
		    { BPF_LD|BPF_H|BPF_ABS,  0, 0, RMP(hp_llc.dxsap) },
		    { BPF_JMP|BPF_JEQ|BPF_K, 0, 4, HPEXT_SXSAP },

		    /* check return type code */
		    { BPF_LD|BPF_B|BPF_ABS,  0, 0,
		        RMP(rmp_proto.rmp_raw.rmp_type) },
		    { BPF_JMP|BPF_JEQ|BPF_K, 1, 0, RMP_BOOT_REPL },
		    { BPF_JMP|BPF_JEQ|BPF_K, 0, 1, RMP_READ_REPL },

		    { BPF_RET|BPF_K,         0, 0, RMP_MAX_PACKET },
		    { BPF_RET|BPF_K,         0, 0, 0x0 }
		};
#undef	RMP
		static struct bpf_program bpf_pgm = {
			sizeof(bpf_insn)/sizeof(bpf_insn[0]), bpf_insn
		};

		static struct bpf_program bpf_w_pgm = {
			sizeof(bpf_wf_insn)/sizeof(bpf_wf_insn[0]), bpf_wf_insn
		};

		if (ioctl(BpfFd, BIOCSETF, (caddr_t)&bpf_pgm) == -1) {
			syslog(LOG_ERR, "bpf: ioctl(BIOCSETF): %m");
			DoExit();
		}

		if (ioctl(BpfFd, BIOCSETWF, (caddr_t)&bpf_w_pgm) == -1) {
			syslog(LOG_ERR, "bpf: ioctl(BIOCSETWF): %m");
			DoExit();
		}

		if (ioctl(BpfFd, BIOCLOCK) == -1) {
			syslog(LOG_ERR, "bpf: ioctl(BIOCLOCK): %m");
			DoExit();
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
	int minunit = 999, n;
	char *cp;
	const char *errstr;
	static char device[IFNAMSIZ];
	static char errbuf[128] = "No Error!";
	struct ifaddrs *ifap, *ifa, *mp = NULL;

	if (errmsg != NULL)
		*errmsg = errbuf;

	if (getifaddrs(&ifap) != 0) {
		(void) strlcpy(errbuf, "bpf: getifaddrs: %m", sizeof(errbuf));
		return(NULL);
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		/*
		 *  If interface is down or this is the loopback interface,
		 *  ignore it.
		 */
		if ((ifa->ifa_flags & IFF_UP) == 0 ||
#ifdef IFF_LOOPBACK
		    (ifa->ifa_flags & IFF_LOOPBACK))
#else
		    (strcmp(ifa->ifa_name, "lo0") == 0))
#endif
			continue;

		for (cp = ifa->ifa_name; !isdigit((unsigned char)*cp); ++cp)
			;
		n = strtonum(cp, 0, INT_MAX, &errstr);
		if (errstr == NULL && n < minunit) {
			minunit = n;
			mp = ifa;
		}
	}

	if (mp == 0) {
		(void) strlcpy(errbuf, "bpf: no interfaces found",
		    sizeof(errbuf));
		freeifaddrs(ifap);
		return(NULL);
	}

	(void) strlcpy(device, mp->ifa_name, sizeof device);
	freeifaddrs(ifap);
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
		if ((cc = read(BpfFd, (char *)BpfPkt, (int)BpfLen)) == -1) {
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
			rconn->tstamp.tv_sec = bhp->bh_tstamp.tv_sec;
			rconn->tstamp.tv_usec = bhp->bh_tstamp.tv_usec;
			bcopy((char *)bp + hdrlen, (char *)&rconn->rmp, caplen);
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
	if (write(BpfFd, (char *)&rconn->rmp, rconn->rmplen) == -1) {
		syslog(LOG_ERR, "write: %s: %m", EnetStr(rconn));
		return(0);
	}

	return(1);
}
