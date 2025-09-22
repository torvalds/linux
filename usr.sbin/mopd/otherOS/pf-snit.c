/*	$OpenBSD: pf-snit.c,v 1.8 2009/10/27 23:59:53 deraadt Exp $ */

/*
 * Copyright (c) 1993-96 Mats O Jansson.  All rights reserved.
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

#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <net/if.h>

#define	DEV_NIT	"/dev/nit"
#include <net/nit.h>
#include <net/nit_if.h>
#include <net/nit_pf.h>
#include <net/nit_buf.h>
#include <net/packetfilt.h>
#include <stropts.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <netdb.h>
#include <ctype.h>
#include <syslog.h>

#include "common/mopdef.h"

/*
 * Variables
 */

/* struct ifreq ifr; */
extern int errno;
extern int promisc;

/*
 * Return information to device.c how to open device.
 * In this case the driver can handle both Ethernet type II and
 * IEEE 802.3 frames (SNAP) in a single pfOpen.
 */

int
pfTrans(interface)
	char *interface;
{
	return TRANS_ETHER+TRANS_8023+TRANS_AND;
}

/*
 * Open and initialize packet filter.
 */

int
pfInit(interface, mode, protocol, trans)
	char *interface;
	u_short protocol;
	int trans, mode;
{
	int	 fd;
	int	 ioarg;
	char	 device[64];
	unsigned long if_flags;
	
	struct ifreq ifr;
	struct strioctl si;
	
	/* get clone */
	if ((fd = open(DEV_NIT, mode)) < 0) {
		syslog(LOG_ERR,"pfInit: open nit %m");
		return(-1);
	}
	
	/*
	 * set filter for protocol 
	 */
	
	if (setup_pf(fd, protocol, trans) < 0)
		return(-1);
	
	/*
	 * set options, bind to underlying interface
	 */
	
	strncpy(ifr.ifr_name, interface, sizeof(ifr.ifr_name));
	
	/* bind */
	si.ic_cmd = NIOCBIND;	/* bind to underlying interface */
	si.ic_timout = 10;
	si.ic_len = sizeof(ifr);
	si.ic_dp = (caddr_t)&ifr;
	if (ioctl(fd, I_STR, (caddr_t)&si) < 0) {
		syslog(LOG_ERR,"pfinit: I_STR %m");
		return(-1);
	}
	
	if (promisc) {
		if_flags = NI_PROMISC;
		si.ic_cmd = NIOCSFLAGS;
		si.ic_timout = 10;
		si.ic_len = sizeof(if_flags);
		si.ic_dp = (caddr_t)&if_flags;
		if (ioctl(fd, I_STR, (caddr_t)&si) < 0) {
			syslog(LOG_ERR,"pfInit: I_STR (promisc) %m");
			return(-1);
		}
	}
	
	/* set up messages */
	if (ioctl(fd, I_SRDOPT, (char *)RMSGD) < 0) { /* want messages */
		syslog(LOG_ERR,"pfInit: I_SRDOPT %m");
		return(-1);
	}

	/* flush read queue */
	if (ioctl(fd, I_FLUSH, (char *)FLUSHR) < 0) {
		syslog(LOG_ERR,"pfInit: I_FLUSH %m");
		return(-1);
	}
	
	return(fd);
}

/*
 * establish protocol filter
 */

int
setup_pf(s, prot, trans)
	int s, trans;
	u_short prot;
{
	int ioarg;
	u_short offset;

	struct packetfilt pf;
	u_short *fwp = pf.Pf_Filter;
	struct strioctl si;

#define	s_offset(structp, element) (&(((structp)0)->element))

	bzero(&pf, sizeof(pf));
 	pf.Pf_Priority = 128;

	offset = ((int)s_offset(struct ether_header *, ether_type))/sizeof(u_short);
	*fwp++ = ENF_PUSHWORD + offset;		/* Check Ethernet type II    */
	*fwp++ = ENF_PUSHLIT | ENF_EQ;		/* protocol prot             */
	*fwp++ = htons(prot);
	*fwp++ = ENF_PUSHWORD + offset + 4;	/* Check 802.3 protocol prot */
	*fwp++ = ENF_PUSHLIT | ENF_EQ;
	*fwp++ = htons(prot);
	*fwp++ = ENF_PUSHWORD + offset + 1;	/* Check for SSAP and DSAP   */
	*fwp++ = ENF_PUSHLIT | ENF_EQ;
	*fwp++ = htons(0xaaaa);
	*fwp++ = ENF_AND;
	*fwp++ = ENF_OR;
	pf.Pf_FilterLen = 11;

	si.ic_cmd = NIOCSETF;
	si.ic_timout = 10;
	si.ic_len = sizeof(pf);
	si.ic_dp = (char *)&pf;
	if (ioctl(s, I_PUSH, "pf") < 0) {
		syslog(LOG_ERR,"setup_pf: I_PUSH %m");
		return(-1);
	}
	if (ioctl(s, I_STR, (char *)&si) < 0) {
		syslog(LOG_ERR,"setup_pf: I_STR %m");
		return(-1);
	}
	
	return(0);
}

/*
 * Get the interface ethernet address
 */

int
pfEthAddr(fd, addr)
int fd;
u_char *addr;
{
	struct ifreq ifr;
	struct sockaddr *sa;
	
	if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
		syslog(LOG_ERR,"pfEthAddr: SIOCGIFADDR %m");
		return(-1);
	}
	sa = (struct sockaddr *)ifr.ifr_data;
	bcopy((char *)sa->sa_data, (char *)addr, 6);
	
	return(0);
}

/*
 * Add a Multicast address to the interface
 */

int
pfAddMulti(s, interface, addr)
	int s;
	char *interface, *addr;
{
	struct ifreq ifr;
	int fd;
	
	strncpy(ifr.ifr_name, interface, sizeof (ifr.ifr_name) -1);
	ifr.ifr_name[sizeof(ifr.ifr_name)] = 0;
	
	ifr.ifr_addr.sa_family = AF_UNSPEC;
	bcopy(addr, ifr.ifr_addr.sa_data, 6);
	
	/*
	 * open a socket, temporarily, to use for SIOC* ioctls
	 */

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR,"pfAddMulti: socket() %m");
		return(-1);
	}
	if (ioctl(fd, SIOCADDMULTI, (caddr_t)&ifr) < 0) {
		syslog(LOG_ERR,"pfAddMulti: SIOCADDMULTI %m");
		close(fd);
		return(-1);
	}
	close(fd);
	
	return(0);
}

/*
 * delete a multicast address from the interface
 */

int
pfDelMulti(s, interface, addr)
int s;
char *interface, *addr;
{
	struct ifreq ifr;
	int fd;
	
	strncpy(ifr.ifr_name, interface, sizeof (ifr.ifr_name) -1);
	ifr.ifr_name[sizeof(ifr.ifr_name)] = 0;
	
	ifr.ifr_addr.sa_family = AF_UNSPEC;
	bcopy(addr, ifr.ifr_addr.sa_data, 6);
	
	/*
	 * open a socket, temporarily, to use for SIOC* ioctls
	 *
	 */
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR,"pfDelMulti: socket() %m");
		return(-1);
	}
	
	if (ioctl(fd, SIOCDELMULTI, (caddr_t)&ifr) < 0) {
		syslog(LOG_ERR,"pfDelMulti: SIOCDELMULTI %m");
		close(fd);
		return(-1);
	}
	close(fd);
	
	return(0);
}

/*
 * read a packet
 */

int
pfRead(fd, buf, len)
int fd, len;
u_char *buf;
{
	return(read(fd, buf, len));
}

/*
 * write a packet
 */

int
pfWrite(fd, buf, len, trans)
	int fd, len, trans;
	u_char *buf;
{
	
	struct sockaddr sa;
	struct strbuf pbuf, dbuf;
	
	sa.sa_family = AF_UNSPEC;
	bcopy(buf, sa.sa_data, sizeof(sa.sa_data));

	switch (trans) {
	default:
		pbuf.len = sizeof(struct sockaddr);
		pbuf.buf = (char *) &sa;
		dbuf.len = len-14;
		dbuf.buf = (char *)buf+14;
		break;
	}

	if (putmsg(fd, &pbuf, &dbuf, 0) == 0)
	  return(len);
	
	return(-1);
}
