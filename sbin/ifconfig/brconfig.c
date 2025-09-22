/*	$OpenBSD: brconfig.c,v 1.33 2025/01/06 17:49:29 denis Exp $	*/

/*
 * Copyright (c) 1999, 2000 Jason L. Wright (jason@thought.net)
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
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SMALL

#include <stdio.h>
#include <sys/types.h>
#include <sys/stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <net/if_bridge.h>
#include <netdb.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <arpa/inet.h>

#include "ifconfig.h"

void bridge_ifsetflag(const char *, u_int32_t);
void bridge_ifclrflag(const char *, u_int32_t);

void bridge_list(char *);
void bridge_cfg(const char *);
void bridge_badrule(int, char **, int);
void bridge_showrule(struct ifbrlreq *);
int bridge_arprule(struct ifbrlreq *, int *, char ***);

#define	IFBAFBITS	"\020\1STATIC"
#define	IFBIFBITS	\
"\020\1LEARNING\2DISCOVER\3BLOCKNONIP\4STP\5EDGE\6AUTOEDGE\7PTP\10AUTOPTP\11SPAN\15LOCAL"

#define	PV2ID(pv, epri, eaddr)	do {					\
	epri	 = pv >> 48;						\
	eaddr[0] = pv >> 40;						\
	eaddr[1] = pv >> 32;						\
	eaddr[2] = pv >> 24;						\
	eaddr[3] = pv >> 16;						\
	eaddr[4] = pv >> 8;						\
	eaddr[5] = pv >> 0;						\
} while (0)

char *stpstates[] = {
	"disabled",
	"listening",
	"learning",
	"forwarding",
	"blocking",
	"discarding"
};
char *stpproto[] = {
	"stp",
	"(none)",
	"rstp",
};
char *stproles[] = {
	"disabled",
	"root",
	"designated",
	"alternate",
	"backup"
};


void
setdiscover(const char *val, int d)
{
	bridge_ifsetflag(val, IFBIF_DISCOVER);
}

void
unsetdiscover(const char *val, int d)
{
	bridge_ifclrflag(val, IFBIF_DISCOVER);
}

void
setblocknonip(const char *val, int d)
{
	bridge_ifsetflag(val, IFBIF_BLOCKNONIP);
}

void
unsetblocknonip(const char *val, int d)
{
	bridge_ifclrflag(val, IFBIF_BLOCKNONIP);
}

void
setlearn(const char *val, int d)
{
	bridge_ifsetflag(val, IFBIF_LEARNING);
}

void
unsetlearn(const char *val, int d)
{
	bridge_ifclrflag(val, IFBIF_LEARNING);
}

void
setstp(const char *val, int d)
{
	bridge_ifsetflag(val, IFBIF_STP);
}

void
unsetstp(const char *val, int d)
{
	bridge_ifclrflag(val, IFBIF_STP);
}

void
setedge(const char *val, int d)
{
	bridge_ifsetflag(val, IFBIF_BSTP_EDGE);
}

void
unsetedge(const char *val, int d)
{
	bridge_ifclrflag(val, IFBIF_BSTP_EDGE);
}

void
setautoedge(const char *val, int d)
{
	bridge_ifsetflag(val, IFBIF_BSTP_AUTOEDGE);
}

void
unsetautoedge(const char *val, int d)
{
	bridge_ifclrflag(val, IFBIF_BSTP_AUTOEDGE);
}

void
setptp(const char *val, int d)
{
	bridge_ifsetflag(val, IFBIF_BSTP_PTP);
}

void
unsetptp(const char *val, int d)
{
	bridge_ifclrflag(val, IFBIF_BSTP_PTP);
}

void
setautoptp(const char *val, int d)
{
	bridge_ifsetflag(val, IFBIF_BSTP_AUTOPTP);
}

void
unsetautoptp(const char *val, int d)
{
	bridge_ifclrflag(val, IFBIF_BSTP_AUTOPTP);
}

void
addlocal(const char *ifsname, int d)
{
	struct ifbreq breq;

	if (strncmp(ifsname, "vether", (sizeof("vether") - 1)) != 0)
		errx(1, "only vether can be local interface");

	/* Add local */
	strlcpy(breq.ifbr_name, ifname, sizeof(breq.ifbr_name));
	strlcpy(breq.ifbr_ifsname, ifsname, sizeof(breq.ifbr_ifsname));
	if (ioctl(sock, SIOCBRDGADDL, (caddr_t)&breq) == -1) {
		if (errno == EEXIST)
			return;
		else
			err(1, "%s: ioctl SIOCBRDGADDL %s", ifname, ifsname);
	}
}

void
bridge_ifsetflag(const char *ifsname, u_int32_t flag)
{
	struct ifbreq req;

	strlcpy(req.ifbr_name, ifname, sizeof(req.ifbr_name));
	strlcpy(req.ifbr_ifsname, ifsname, sizeof(req.ifbr_ifsname));
	if (ioctl(sock, SIOCBRDGGIFFLGS, (caddr_t)&req) == -1)
		err(1, "%s: ioctl SIOCBRDGGIFFLGS %s", ifname, ifsname);

	req.ifbr_ifsflags |= flag & ~IFBIF_RO_MASK;

	if (ioctl(sock, SIOCBRDGSIFFLGS, (caddr_t)&req) == -1)
		err(1, "%s: ioctl SIOCBRDGSIFFLGS %s", ifname, ifsname);
}

void
bridge_ifclrflag(const char *ifsname, u_int32_t flag)
{
	struct ifbreq req;

	strlcpy(req.ifbr_name, ifname, sizeof(req.ifbr_name));
	strlcpy(req.ifbr_ifsname, ifsname, sizeof(req.ifbr_ifsname));

	if (ioctl(sock, SIOCBRDGGIFFLGS, (caddr_t)&req) == -1)
		err(1, "%s: ioctl SIOCBRDGGIFFLGS %s", ifname, ifsname);

	req.ifbr_ifsflags &= ~(flag | IFBIF_RO_MASK);

	if (ioctl(sock, SIOCBRDGSIFFLGS, (caddr_t)&req) == -1)
		err(1, "%s: ioctl SIOCBRDGSIFFLGS %s", ifname, ifsname);
}

void
bridge_flushall(const char *val, int p)
{
	struct ifbreq req;

	strlcpy(req.ifbr_name, ifname, sizeof(req.ifbr_name));
	req.ifbr_ifsflags = IFBF_FLUSHALL;
	if (ioctl(sock, SIOCBRDGFLUSH, &req) == -1)
		err(1, "%s", ifname);
}

void
bridge_flush(const char *val, int p)
{
	struct ifbreq req;

	strlcpy(req.ifbr_name, ifname, sizeof(req.ifbr_name));
	req.ifbr_ifsflags = IFBF_FLUSHDYN;
	if (ioctl(sock, SIOCBRDGFLUSH, &req) == -1)
		err(1, "%s", ifname);
}

void
bridge_cfg(const char *delim)
{
	struct ifbropreq ifbp;
	u_int16_t pri;
	u_int8_t ht, fd, ma, hc, proto;
	u_int8_t lladdr[ETHER_ADDR_LEN];
	u_int16_t bprio;

	strlcpy(ifbp.ifbop_name, ifname, sizeof(ifbp.ifbop_name));
	if (ioctl(sock, SIOCBRDGGPARAM, (caddr_t)&ifbp) == -1) {
		if (errno == ENOTTY)
			return;
		err(1, "%s SIOCBRDGGPARAM", ifname);
	}

	printf("%s", delim);
	pri = ifbp.ifbop_priority;
	ht = ifbp.ifbop_hellotime;
	fd = ifbp.ifbop_fwddelay;
	ma = ifbp.ifbop_maxage;
	hc = ifbp.ifbop_holdcount;
	proto = ifbp.ifbop_protocol;

	printf("priority %u hellotime %u fwddelay %u maxage %u "
	    "holdcnt %u proto %s\n", pri, ht, fd, ma, hc, stpproto[proto]);

	if (aflag)
		return;

	PV2ID(ifbp.ifbop_desg_bridge, bprio, lladdr);
	printf("\tdesignated: id %s priority %u\n",
	    ether_ntoa((struct ether_addr *)lladdr), bprio);

	if (ifbp.ifbop_root_bridge == ifbp.ifbop_desg_bridge)
		return;

	PV2ID(ifbp.ifbop_root_bridge, bprio, lladdr);
	printf("\troot: id %s priority %u ifcost %u port %u\n",
	    ether_ntoa((struct ether_addr *)lladdr), bprio,
	    ifbp.ifbop_root_path_cost, ifbp.ifbop_root_port & 0xfff);
}

void
bridge_list(char *delim)
{
	struct ifbreq *reqp;
	struct ifbifconf bifc;
	int i, len = 8192;
	char buf[sizeof(reqp->ifbr_ifsname) + 1], *inbuf = NULL, *inb;

	while (1) {
		bifc.ifbic_len = len;
		inb = realloc(inbuf, len);
		if (inb == NULL)
			err(1, "malloc");
		bifc.ifbic_buf = inbuf = inb;
		strlcpy(bifc.ifbic_name, ifname, sizeof(bifc.ifbic_name));
		if (ioctl(sock, SIOCBRDGIFS, &bifc) == -1) {
			if (errno == ENOTTY)
				return;
			err(1, "%s SIOCBRDGIFS", ifname);
		}
		if (bifc.ifbic_len + sizeof(*reqp) < len)
			break;
		len *= 2;
	}
	for (i = 0; i < bifc.ifbic_len / sizeof(*reqp); i++) {
		reqp = bifc.ifbic_req + i;
		strlcpy(buf, reqp->ifbr_ifsname, sizeof(buf));
		printf("%s%s ", delim, buf);
		printb("flags", reqp->ifbr_ifsflags, IFBIFBITS);
		printf("\n");
		if (reqp->ifbr_ifsflags & IFBIF_SPAN)
			continue;
		printf("\t\t");
		printf("port %u ifpriority %u ifcost %u",
		    reqp->ifbr_portno, reqp->ifbr_priority,
		    reqp->ifbr_path_cost);
		if (reqp->ifbr_protected) {
			int v;

			v = ffs(reqp->ifbr_protected);
			printf(" protected %u", v);
			while (++v < 32) {
				if ((1 << (v - 1)) & reqp->ifbr_protected)
					printf(",%u", v);
			}
		}
		if (reqp->ifbr_ifsflags & IFBIF_STP)
			printf(" %s role %s",
			    stpstates[reqp->ifbr_state],
			    stproles[reqp->ifbr_role]);
		printf("\n");
		bridge_rules(buf, 1);
	}
	free(bifc.ifbic_buf);
}

void
bridge_add(const char *ifn, int d)
{
	struct ifbreq req;

	strlcpy(req.ifbr_name, ifname, sizeof(req.ifbr_name));
	strlcpy(req.ifbr_ifsname, ifn, sizeof(req.ifbr_ifsname));
	if (ioctl(sock, SIOCBRDGADD, &req) == -1) {
		if (errno == EEXIST)
			return;
		err(1, "%s: %s", ifname, ifn);
	}
}

void
bridge_delete(const char *ifn, int d)
{
	struct ifbreq req;

	strlcpy(req.ifbr_name, ifname, sizeof(req.ifbr_name));
	strlcpy(req.ifbr_ifsname, ifn, sizeof(req.ifbr_ifsname));
	if (ioctl(sock, SIOCBRDGDEL, &req) == -1)
		err(1, "%s: %s", ifname, ifn);
}

void
bridge_addspan(const char *ifn, int d)
{
	struct ifbreq req;

	strlcpy(req.ifbr_name, ifname, sizeof(req.ifbr_name));
	strlcpy(req.ifbr_ifsname, ifn, sizeof(req.ifbr_ifsname));
	if (ioctl(sock, SIOCBRDGADDS, &req) == -1) {
		if (errno == EEXIST)
			return;
		err(1, "%s: %s", ifname, ifn);
	}
}

void
bridge_delspan(const char *ifn, int d)
{
	struct ifbreq req;

	strlcpy(req.ifbr_name, ifname, sizeof(req.ifbr_name));
	strlcpy(req.ifbr_ifsname, ifn, sizeof(req.ifbr_ifsname));
	if (ioctl(sock, SIOCBRDGDELS, &req) == -1)
		err(1, "%s: %s", ifname, ifn);
}

void
bridge_timeout(const char *arg, int d)
{
	struct ifbrparam bp;
	const char *errstr;

	bp.ifbrp_ctime = strtonum(arg, 0, UINT32_MAX, &errstr);
	if (errstr)
		err(1, "timeout %s is: %s", arg, errstr);

	strlcpy(bp.ifbrp_name, ifname, sizeof(bp.ifbrp_name));
	if (ioctl(sock, SIOCBRDGSTO, (caddr_t)&bp) == -1)
		err(1, "%s", ifname);
}

void
bridge_maxage(const char *arg, int d)
{
	struct ifbrparam bp;
	const char *errstr;

	bp.ifbrp_maxage = strtonum(arg, 0, UINT8_MAX, &errstr);
	if (errstr)
		errx(1, "maxage %s is: %s", arg, errstr);

	strlcpy(bp.ifbrp_name, ifname, sizeof(bp.ifbrp_name));
	if (ioctl(sock, SIOCBRDGSMA, (caddr_t)&bp) == -1)
		err(1, "%s", ifname);
}

void
bridge_priority(const char *arg, int d)
{
	struct ifbrparam bp;
	const char *errstr;

	bp.ifbrp_prio  = strtonum(arg, 0, UINT16_MAX, &errstr);
	if (errstr)
		errx(1, "spanpriority %s is: %s", arg, errstr);

	strlcpy(bp.ifbrp_name, ifname, sizeof(bp.ifbrp_name));
	if (ioctl(sock, SIOCBRDGSPRI, (caddr_t)&bp) == -1)
		err(1, "%s", ifname);
}

void
bridge_protect(const char *ifsname, const char *val)
{
	struct ifbreq breq;
	unsigned long v;
	char *optlist, *str;
	const char *errstr;

	strlcpy(breq.ifbr_name, ifname, sizeof(breq.ifbr_name));
	strlcpy(breq.ifbr_ifsname, ifsname, sizeof(breq.ifbr_ifsname));
	breq.ifbr_protected = 0;

	/* We muck with the string, so copy it. */
	optlist = strdup(val);
	if (optlist == NULL)
		err(1, "strdup");

	str = strtok(optlist, ",");
	while (str != NULL) {
		v = strtonum(str, 1, 31, &errstr);
		if (errstr)
			err(1, "protected domain %s is: %s", str, errstr);
		breq.ifbr_protected |= (1 << (v - 1));
		str = strtok(NULL, ",");
	}

	if (ioctl(sock, SIOCBRDGSIFPROT, (caddr_t)&breq) == -1)
		err(1, "%s: %s", ifname, val);

	free(optlist);
}

void
bridge_unprotect(const char *ifsname, int d)
{
	struct ifbreq breq;

	strlcpy(breq.ifbr_name, ifname, sizeof(breq.ifbr_name));
	strlcpy(breq.ifbr_ifsname, ifsname, sizeof(breq.ifbr_ifsname));

	breq.ifbr_protected = 0;

	if (ioctl(sock, SIOCBRDGSIFPROT, (caddr_t)&breq) == -1)
		err(1, "%s: %d", ifname, 0);
}

void
bridge_proto(const char *arg, int d)
{
	struct ifbrparam bp;
	int i, proto = -1;

	for (i = 0; i <= BSTP_PROTO_MAX; i++)
		if (strcmp(arg, stpproto[i]) == 0) {
			proto = i;
			break;
		}
	if (proto == -1)
		errx(1, "invalid arg for proto: %s", arg);

	strlcpy(bp.ifbrp_name, ifname, sizeof(bp.ifbrp_name));
	bp.ifbrp_prio = proto;
	if (ioctl(sock, SIOCBRDGSPROTO, (caddr_t)&bp) == -1)
		err(1, "%s", ifname);
}

void
bridge_fwddelay(const char *arg, int d)
{
	struct ifbrparam bp;
	const char *errstr;

	bp.ifbrp_fwddelay = strtonum(arg, 0, UINT8_MAX, &errstr);
	if (errstr)
		errx(1, "fwddelay %s is: %s", arg, errstr);

	strlcpy(bp.ifbrp_name, ifname, sizeof(bp.ifbrp_name));

	if (ioctl(sock, SIOCBRDGSFD, (caddr_t)&bp) == -1)
		err(1, "%s", ifname);
}

void
bridge_hellotime(const char *arg, int d)
{
	struct ifbrparam bp;
	const char *errstr;

	bp.ifbrp_hellotime = strtonum(arg, 0, UINT8_MAX, &errstr);
	if (errstr)
		errx(1, "hellotime %s is: %s", arg, errstr);

	strlcpy(bp.ifbrp_name, ifname, sizeof(bp.ifbrp_name));

	if (ioctl(sock, SIOCBRDGSHT, (caddr_t)&bp) == -1)
		err(1, "%s", ifname);
}

void
bridge_maxaddr(const char *arg, int d)
{
	struct ifbrparam bp;
	const char *errstr;

	bp.ifbrp_csize = strtonum(arg, 0, UINT32_MAX, &errstr);
	if (errstr)
		errx(1, "maxaddr %s is: %s", arg, errstr);

	strlcpy(bp.ifbrp_name, ifname, sizeof(bp.ifbrp_name));
	if (ioctl(sock, SIOCBRDGSCACHE, (caddr_t)&bp) == -1)
		err(1, "%s", ifname);
}

void
bridge_deladdr(const char *addr, int d)
{
	struct ifbareq ifba;
	struct ether_addr *ea;

	strlcpy(ifba.ifba_name, ifname, sizeof(ifba.ifba_name));
	ea = ether_aton(addr);
	if (ea == NULL)
		err(1, "Invalid address: %s", addr);

	bcopy(ea, &ifba.ifba_dst, sizeof(struct ether_addr));

	if (ioctl(sock, SIOCBRDGDADDR, &ifba) == -1)
		err(1, "%s: %s", ifname, addr);
}

void
bridge_ifprio(const char *ifsname, const char *val)
{
	struct ifbreq breq;
	const char *errstr;

	breq.ifbr_priority = strtonum(val, 0, UINT8_MAX, &errstr);
	if (errstr)
		errx(1, "ifpriority %s is: %s", val, errstr);

	strlcpy(breq.ifbr_name, ifname, sizeof(breq.ifbr_name));
	strlcpy(breq.ifbr_ifsname, ifsname, sizeof(breq.ifbr_ifsname));

	if (ioctl(sock, SIOCBRDGSIFPRIO, (caddr_t)&breq) == -1)
		err(1, "%s: %s", ifname, val);
}

void
bridge_ifcost(const char *ifsname, const char *val)
{
	struct ifbreq breq;
	const char *errstr;

	breq.ifbr_path_cost = strtonum(val, 0, UINT32_MAX, &errstr);
	if (errstr)
		errx(1, "ifcost %s is: %s", val, errstr);

	strlcpy(breq.ifbr_name, ifname, sizeof(breq.ifbr_name));
	strlcpy(breq.ifbr_ifsname, ifsname, sizeof(breq.ifbr_ifsname));

	if (ioctl(sock, SIOCBRDGSIFCOST, (caddr_t)&breq) == -1)
		err(1, "%s: %s", ifname, val);
}

void
bridge_noifcost(const char *ifsname, int d)
{
	struct ifbreq breq;

	strlcpy(breq.ifbr_name, ifname, sizeof(breq.ifbr_name));
	strlcpy(breq.ifbr_ifsname, ifsname, sizeof(breq.ifbr_ifsname));

	breq.ifbr_path_cost = 0;

	if (ioctl(sock, SIOCBRDGSIFCOST, (caddr_t)&breq) == -1)
		err(1, "%s", ifname);
}

void
bridge_addaddr(const char *ifsname, const char *addr)
{
	struct ifbareq ifba;
	struct ether_addr *ea;

	strlcpy(ifba.ifba_name, ifname, sizeof(ifba.ifba_name));
	strlcpy(ifba.ifba_ifsname, ifsname, sizeof(ifba.ifba_ifsname));

	ea = ether_aton(addr);
	if (ea == NULL)
		errx(1, "Invalid address: %s", addr);

	bcopy(ea, &ifba.ifba_dst, sizeof(struct ether_addr));
	ifba.ifba_flags = IFBAF_STATIC;

	if (ioctl(sock, SIOCBRDGSADDR, &ifba) == -1)
		err(1, "%s: %s", ifname, addr);
}

void
bridge_addendpoint(const char *endpoint, const char *addr)
{
	struct ifbareq ifba;
	struct ether_addr *ea;
	struct addrinfo *res;
	int ecode;

	/* should we handle ports? */
	ecode = getaddrinfo(endpoint, NULL, NULL, &res);
	if (ecode != 0) {
                errx(1, "%s endpoint %s: %s", ifname, endpoint,
                    gai_strerror(ecode));
	}
	if (res->ai_addrlen > sizeof(ifba.ifba_dstsa))
		errx(1, "%s: addrlen > dstsa", __func__);

	ea = ether_aton(addr);
	if (ea == NULL) {
		errx(1, "%s endpoint %s %s: invalid Ethernet address",
		    ifname, endpoint, addr);
	}

	memset(&ifba, 0, sizeof(ifba));
	strlcpy(ifba.ifba_name, ifname, sizeof(ifba.ifba_name));
	strlcpy(ifba.ifba_ifsname, ifname, sizeof(ifba.ifba_ifsname));
	memcpy(&ifba.ifba_dst, ea, sizeof(struct ether_addr));
	memcpy(&ifba.ifba_dstsa, res->ai_addr, res->ai_addrlen);
	ifba.ifba_flags = IFBAF_STATIC;

	freeaddrinfo(res);

	if (ioctl(sock, SIOCBRDGSADDR, &ifba) == -1)
		err(1, "%s endpoint %s %s", ifname, endpoint, addr);
}

void
bridge_delendpoint(const char *addr, int d)
{
	struct ifbareq ifba;
	struct ether_addr *ea;
	int ecode;

	ea = ether_aton(addr);
	if (ea == NULL) {
		errx(1, "%s -endpoint %s: invalid Ethernet address",
		    ifname, addr);
	}

	memset(&ifba, 0, sizeof(ifba));
	strlcpy(ifba.ifba_name, ifname, sizeof(ifba.ifba_name));
	strlcpy(ifba.ifba_ifsname, ifname, sizeof(ifba.ifba_ifsname));
	memcpy(&ifba.ifba_dst, ea, sizeof(struct ether_addr));
	ifba.ifba_flags = IFBAF_STATIC;

	if (ioctl(sock, SIOCBRDGDADDR, &ifba) == -1)
		err(1, "%s -endpoint %s", ifname, addr);
}

void
bridge_addrs(const char *delim, int d)
{
	char dstaddr[NI_MAXHOST];
	char dstport[NI_MAXSERV];
	const int niflag = NI_NUMERICHOST|NI_DGRAM;
	struct ifbaconf ifbac;
	struct ifbareq *ifba;
	char *inbuf = NULL, buf[sizeof(ifba->ifba_ifsname) + 1], *inb;
	struct sockaddr *sa;
	int i, len = 8192;

	/* ifconfig will call us with the argv of the command */
	if (strcmp(delim, "addr") == 0)
		delim = "";

	while (1) {
		ifbac.ifbac_len = len;
		inb = realloc(inbuf, len);
		if (inb == NULL)
			err(1, "malloc");
		ifbac.ifbac_buf = inbuf = inb;
		strlcpy(ifbac.ifbac_name, ifname, sizeof(ifbac.ifbac_name));
		if (ioctl(sock, SIOCBRDGRTS, &ifbac) == -1) {
			if (errno == ENETDOWN)
				return;
			err(1, "%s", ifname);
		}
		if (ifbac.ifbac_len + sizeof(*ifba) < len)
			break;
		len *= 2;
	}

	for (i = 0; i < ifbac.ifbac_len / sizeof(*ifba); i++) {
		ifba = ifbac.ifbac_req + i;
		strlcpy(buf, ifba->ifba_ifsname, sizeof(buf));
		printf("%s%s %s %u ", delim, ether_ntoa(&ifba->ifba_dst),
		    buf, ifba->ifba_age);
		sa = (struct sockaddr *)&ifba->ifba_dstsa;
		printb("flags", ifba->ifba_flags, IFBAFBITS);
		if (sa->sa_family != AF_UNSPEC &&
		    getnameinfo(sa, sa->sa_len,
		    dstaddr, sizeof(dstaddr),
		    dstport, sizeof(dstport), niflag) == 0)
			printf(" tunnel %s:%s", dstaddr, dstport);
		printf("\n");
	}
	free(inbuf);
}

void
bridge_holdcnt(const char *value, int d)
{
	struct ifbrparam bp;
	const char *errstr;

	bp.ifbrp_txhc = strtonum(value, 0, UINT8_MAX, &errstr);
	if (errstr)
		err(1, "holdcnt %s is: %s", value, errstr);

	strlcpy(bp.ifbrp_name, ifname, sizeof(bp.ifbrp_name));
	if (ioctl(sock, SIOCBRDGSTXHC, (caddr_t)&bp) == -1)
		err(1, "%s", ifname);
}

/*
 * Check to make sure interface is really a bridge interface.
 */
int
is_bridge()
{
	struct ifbaconf ifbac;

	ifbac.ifbac_len = 0;
	strlcpy(ifbac.ifbac_name, ifname, sizeof(ifbac.ifbac_name));
	if (ioctl(sock, SIOCBRDGRTS, (caddr_t)&ifbac) == -1) {
		if (errno == ENETDOWN)
			return (1);
		return (0);
	}
	return (1);
}

/* no tpmr(4) specific ioctls, name is enough if ifconfig.c:printif() passed */
int
is_tpmr(void)
{
	return (strncmp(ifname, "tpmr", sizeof("tpmr") - 1) == 0);
}

void
bridge_status(void)
{
	struct ifbrparam bp1, bp2;

	if (is_tpmr()) {
		bridge_list("\t");
		return;
	}

	if (!is_bridge())
		return;

	bridge_cfg("\t");
	bridge_list("\t");

	if (aflag && !ifaliases)
		return;

	strlcpy(bp1.ifbrp_name, ifname, sizeof(bp1.ifbrp_name));
	if (ioctl(sock, SIOCBRDGGCACHE, (caddr_t)&bp1) == -1)
		return;

	strlcpy(bp2.ifbrp_name, ifname, sizeof(bp2.ifbrp_name));
	if (ioctl(sock, SIOCBRDGGTO, (caddr_t)&bp2) == -1)
		return;

	printf("\tAddresses (max cache: %u, timeout: %u):\n",
	    bp1.ifbrp_csize, bp2.ifbrp_ctime);

	bridge_addrs("\t\t", 0);
}

void
bridge_flushrule(const char *ifsname, int d)
{
	struct ifbrlreq req;

	strlcpy(req.ifbr_name, ifname, sizeof(req.ifbr_name));
	strlcpy(req.ifbr_ifsname, ifsname, sizeof(req.ifbr_ifsname));
	if (ioctl(sock, SIOCBRDGFRL, &req) == -1)
		err(1, "%s: %s", ifname, ifsname);
}

void
bridge_rules(const char *ifsname, int usetab)
{
	char *inbuf = NULL, *inb;
	struct ifbrlconf ifc;
	struct ifbrlreq *ifrp;
	int len = 8192, i;

	while (1) {
		ifc.ifbrl_len = len;
		inb = realloc(inbuf, len);
		if (inb == NULL)
			err(1, "malloc");
		ifc.ifbrl_buf = inbuf = inb;
		strlcpy(ifc.ifbrl_name, ifname, sizeof(ifc.ifbrl_name));
		strlcpy(ifc.ifbrl_ifsname, ifsname, sizeof(ifc.ifbrl_ifsname));
		if (ioctl(sock, SIOCBRDGGRL, &ifc) == -1)
			err(1, "ioctl(SIOCBRDGGRL)");
		if (ifc.ifbrl_len + sizeof(*ifrp) < len)
			break;
		len *= 2;
	}
	ifrp = ifc.ifbrl_req;
	for (i = 0; i < ifc.ifbrl_len; i += sizeof(*ifrp)) {
		ifrp = (struct ifbrlreq *)((caddr_t)ifc.ifbrl_req + i);

		if (usetab)
			printf("\t");

		bridge_showrule(ifrp);
	}
}

void
bridge_showrule(struct ifbrlreq *r)
{
	if (r->ifbr_action == BRL_ACTION_BLOCK)
		printf("block ");
	else if (r->ifbr_action == BRL_ACTION_PASS)
		printf("pass ");
	else
		printf("[neither block nor pass?]\n");

	if ((r->ifbr_flags & (BRL_FLAG_IN | BRL_FLAG_OUT)) ==
	    (BRL_FLAG_IN | BRL_FLAG_OUT))
		printf("in/out ");
	else if (r->ifbr_flags & BRL_FLAG_IN)
		printf("in ");
	else if (r->ifbr_flags & BRL_FLAG_OUT)
		printf("out ");
	else
		printf("[neither in nor out?]\n");

	printf("on %s", r->ifbr_ifsname);

	if (r->ifbr_flags & BRL_FLAG_SRCVALID)
		printf(" src %s", ether_ntoa(&r->ifbr_src));
	if (r->ifbr_flags & BRL_FLAG_DSTVALID)
		printf(" dst %s", ether_ntoa(&r->ifbr_dst));
	if (r->ifbr_tagname[0])
		printf(" tag %s", r->ifbr_tagname);

	if (r->ifbr_arpf.brla_flags & BRLA_ARP)
		printf(" arp");
	if (r->ifbr_arpf.brla_flags & BRLA_RARP)
		printf(" rarp");
	if (r->ifbr_arpf.brla_op == ARPOP_REQUEST ||
	    r->ifbr_arpf.brla_op == ARPOP_REVREQUEST)
		printf(" request");
	if (r->ifbr_arpf.brla_op == ARPOP_REPLY ||
	    r->ifbr_arpf.brla_op == ARPOP_REVREPLY)
		printf(" reply");
	if (r->ifbr_arpf.brla_flags & BRLA_SHA)
		printf(" sha %s", ether_ntoa(&r->ifbr_arpf.brla_sha));
	if (r->ifbr_arpf.brla_flags & BRLA_THA)
		printf(" tha %s", ether_ntoa(&r->ifbr_arpf.brla_tha));
	if (r->ifbr_arpf.brla_flags & BRLA_SPA)
		printf(" spa %s", inet_ntoa(r->ifbr_arpf.brla_spa));
	if (r->ifbr_arpf.brla_flags & BRLA_TPA)
		printf(" tpa %s", inet_ntoa(r->ifbr_arpf.brla_tpa));

	printf("\n");
}

/*
 * Parse a rule definition and send it upwards.
 *
 * Syntax:
 *	{block|pass} {in|out|in/out} on {ifs} [src {mac}] [dst {mac}]
 */
int
bridge_rule(int targc, char **targv, int ln)
{
	char **argv = targv;
	int argc = targc;
	struct ifbrlreq rule;
	struct ether_addr *ea, *dea;

	if (argc == 0) {
		warnx("invalid rule");
		return (1);
	}
	bzero(&rule, sizeof(rule));
	strlcpy(rule.ifbr_name, ifname, sizeof(rule.ifbr_name));

	if (strcmp(argv[0], "block") == 0)
		rule.ifbr_action = BRL_ACTION_BLOCK;
	else if (strcmp(argv[0], "pass") == 0)
		rule.ifbr_action = BRL_ACTION_PASS;
	else
		goto bad_rule;
	argc--;	argv++;

	if (argc == 0) {
		bridge_badrule(targc, targv, ln);
		return (1);
	}
	if (strcmp(argv[0], "in") == 0)
		rule.ifbr_flags |= BRL_FLAG_IN;
	else if (strcmp(argv[0], "out") == 0)
		rule.ifbr_flags |= BRL_FLAG_OUT;
	else if (strcmp(argv[0], "in/out") == 0)
		rule.ifbr_flags |= BRL_FLAG_IN | BRL_FLAG_OUT;
	else if (strcmp(argv[0], "on") == 0) {
		rule.ifbr_flags |= BRL_FLAG_IN | BRL_FLAG_OUT;
		argc++; argv--;
	} else
		goto bad_rule;
	argc--; argv++;

	if (argc == 0 || strcmp(argv[0], "on"))
		goto bad_rule;
	argc--; argv++;

	if (argc == 0)
		goto bad_rule;
	strlcpy(rule.ifbr_ifsname, argv[0], sizeof(rule.ifbr_ifsname));
	argc--; argv++;

	while (argc) {
		dea = NULL;
		if (strcmp(argv[0], "dst") == 0) {
			if (rule.ifbr_flags & BRL_FLAG_DSTVALID)
				goto bad_rule;
			rule.ifbr_flags |= BRL_FLAG_DSTVALID;
			dea = &rule.ifbr_dst;
			argc--; argv++;
		} else if (strcmp(argv[0], "src") == 0) {
			if (rule.ifbr_flags & BRL_FLAG_SRCVALID)
				goto bad_rule;
			rule.ifbr_flags |= BRL_FLAG_SRCVALID;
			dea = &rule.ifbr_src;
			argc--; argv++;
		} else if (strcmp(argv[0], "tag") == 0) {
			if (argc < 2) {
				warnx("missing tag name");
				goto bad_rule;
			}
			if (rule.ifbr_tagname[0]) {
				warnx("tag already defined");
				goto bad_rule;
			}
			argc--; argv++;
			if (strlcpy(rule.ifbr_tagname, argv[0],
			    PF_TAG_NAME_SIZE) > PF_TAG_NAME_SIZE) {
				warnx("tag name '%s' too long", argv[0]);
				goto bad_rule;
			}
			argc--; argv++;
		} else if (strcmp(argv[0], "arp") == 0) {
			rule.ifbr_arpf.brla_flags |= BRLA_ARP;
			argc--; argv++;
			if (bridge_arprule(&rule, &argc, &argv) == -1)
				goto bad_rule;
		} else if (strcmp(argv[0], "rarp") == 0) {
			rule.ifbr_arpf.brla_flags |= BRLA_RARP;
			argc--; argv++;
			if (bridge_arprule(&rule, &argc, &argv) == -1)
				goto bad_rule;
		} else
			goto bad_rule;

		if (dea != NULL) {
			if (argc == 0)
				goto bad_rule;
			ea = ether_aton(argv[0]);
			if (ea == NULL) {
				warnx("invalid address: %s", argv[0]);
				return (1);
			}
			bcopy(ea, dea, sizeof(*dea));
			argc--; argv++;
		}
	}

	if (ioctl(sock, SIOCBRDGARL, &rule) == -1) {
		warn("%s", ifname);
		return (1);
	}
	return (0);

bad_rule:
	bridge_badrule(targc, targv, ln);
	return (1);
}

int
bridge_arprule(struct ifbrlreq *rule, int *argc, char ***argv)
{
	while (*argc) {
		struct ether_addr	*ea, *dea = NULL;
		struct in_addr		 ia, *dia = NULL;

		if (strcmp((*argv)[0], "request") == 0) {
			if (rule->ifbr_arpf.brla_flags & BRLA_ARP)
				rule->ifbr_arpf.brla_op = ARPOP_REQUEST;
			else if (rule->ifbr_arpf.brla_flags & BRLA_RARP)
				rule->ifbr_arpf.brla_op = ARPOP_REVREQUEST;
			else
				errx(1, "bridge_arprule: arp/rarp undefined");
		} else if (strcmp((*argv)[0], "reply") == 0) {
			if (rule->ifbr_arpf.brla_flags & BRLA_ARP)
				rule->ifbr_arpf.brla_op = ARPOP_REPLY;
			else if (rule->ifbr_arpf.brla_flags & BRLA_RARP)
				rule->ifbr_arpf.brla_op = ARPOP_REVREPLY;
			else
				errx(1, "bridge_arprule: arp/rarp undefined");
		} else if (strcmp((*argv)[0], "sha") == 0) {
			rule->ifbr_arpf.brla_flags |= BRLA_SHA;
			dea = &rule->ifbr_arpf.brla_sha;
		} else if (strcmp((*argv)[0], "tha") == 0) {
			rule->ifbr_arpf.brla_flags |= BRLA_THA;
			dea = &rule->ifbr_arpf.brla_tha;
		} else if (strcmp((*argv)[0], "spa") == 0) {
			rule->ifbr_arpf.brla_flags |= BRLA_SPA;
			dia = &rule->ifbr_arpf.brla_spa;
		} else if (strcmp((*argv)[0], "tpa") == 0) {
			rule->ifbr_arpf.brla_flags |= BRLA_TPA;
			dia = &rule->ifbr_arpf.brla_tpa;
		} else
			return (0);

		(*argc)--; (*argv)++;
		if (dea != NULL) {
			if (*argc == 0)
				return (-1);
			ea = ether_aton((*argv)[0]);
			if (ea == NULL) {
				warnx("invalid address: %s", (*argv)[0]);
				return (-1);
			}
			bcopy(ea, dea, sizeof(*dea));
			(*argc)--; (*argv)++;
		}
		if (dia != NULL) {
			if (*argc == 0)
				return (-1);
			ia.s_addr = inet_addr((*argv)[0]);
			if (ia.s_addr == INADDR_NONE) {
				warnx("invalid address: %s", (*argv)[0]);
				return (-1);
			}
			bcopy(&ia, dia, sizeof(*dia));
			(*argc)--; (*argv)++;
		}
	}
	return (0);
}


#define MAXRULEWORDS 32

void
bridge_rulefile(const char *fname, int d)
{
	FILE *f;
	char *str, *argv[MAXRULEWORDS], buf[1024];
	int ln = 0, argc = 0;

	f = fopen(fname, "r");
	if (f == NULL)
		err(1, "%s", fname);

	while (fgets(buf, sizeof(buf), f) != NULL) {
		ln++;
		if (buf[0] == '#' || buf[0] == '\n')
			continue;

		argc = 0;
		str = strtok(buf, "\n\t\r ");
		while (str != NULL && argc < MAXRULEWORDS) {
			argv[argc++] = str;
			str = strtok(NULL, "\n\t\r ");
		}

		/* Rule is too long if there's more. */
		if (str != NULL) {
			warnx("invalid rule: %d: %s ...", ln, buf);
			continue;
		}

		bridge_rule(argc, argv, ln);
	}
	fclose(f);
}

void
bridge_badrule(int argc, char *argv[], int ln)
{
	extern const char *__progname;
	int i;

	fprintf(stderr, "%s: invalid rule: ", __progname);
	if (ln != -1)
		fprintf(stderr, "%d: ", ln);
	for (i = 0; i < argc; i++)
		fprintf(stderr, "%s ", argv[i]);
	fprintf(stderr, "\n");
}

#endif
