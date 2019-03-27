/*	$KAME: mld6.c,v 1.15 2003/04/02 11:29:54 suz Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <signal.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>

#include  <arpa/inet.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

/* portability with older KAME headers */
#ifndef MLD_LISTENER_QUERY
#define MLD_LISTENER_QUERY	MLD6_LISTENER_QUERY
#define MLD_LISTENER_REPORT	MLD6_LISTENER_REPORT
#define MLD_LISTENER_DONE	MLD6_LISTENER_DONE
#define MLD_MTRACE_RESP		MLD6_MTRACE_RESP
#define MLD_MTRACE		MLD6_MTRACE
#define mld_hdr		mld6_hdr
#define mld_type	mld6_type
#define mld_code	mld6_code
#define mld_cksum	mld6_cksum
#define mld_maxdelay	mld6_maxdelay
#define mld_reserved	mld6_reserved
#define mld_addr	mld6_addr
#endif
#ifndef IP6OPT_ROUTER_ALERT
#define IP6OPT_ROUTER_ALERT	IP6OPT_RTALERT
#endif

struct msghdr m;
struct sockaddr_in6 dst;
struct mld_hdr mldh;
struct in6_addr maddr = IN6ADDR_ANY_INIT, any = IN6ADDR_ANY_INIT;
struct ipv6_mreq mreq;
u_short ifindex;
int s;

#define QUERY_RESPONSE_INTERVAL 10000

void make_msg(int index, struct in6_addr *addr, u_int type, struct in6_addr *qaddr);
void usage(void);
void dump(int);
void quit(int);

int
main(int argc, char *argv[])
{
	int i;
	struct icmp6_filter filt;
	u_int hlim = 1;
	fd_set fdset;
	struct itimerval itimer;
	u_int type;
	int ch;
	struct in6_addr *qaddr = &maddr;

	type = MLD_LISTENER_QUERY;
	while ((ch = getopt(argc, argv, "dgr")) != -1) {
		switch (ch) {
		case 'd':
			if (type != MLD_LISTENER_QUERY) {
				printf("Can not specifiy -d with -r\n");
				return 1;
			}
			type = MLD_LISTENER_DONE;
			break;
		case 'g':
			qaddr = &any;
			break;
		case 'r':
			if (type != MLD_LISTENER_QUERY) {
				printf("Can not specifiy -r with -d\n");
				return 1;
			}
			type = MLD_LISTENER_REPORT;
			break;
		default:
			usage();
			/*NOTREACHED*/
		}
	}

	argv += optind;
	argc -= optind;
	
	if (argc != 1 && argc != 2)
		usage();

	ifindex = (u_short)if_nametoindex(argv[0]);
	if (ifindex == 0)
		usage();
	if (argc == 2 && inet_pton(AF_INET6, argv[1], &maddr) != 1)
		usage();
	if (type != MLD_LISTENER_QUERY && qaddr != &maddr) {
		printf("Can not specifiy -g with -d or -r\n");
		return 1;
	}

	if ((s = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6)) < 0)
		err(1, "socket");

	if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hlim,
		       sizeof(hlim)) == -1)
		err(1, "setsockopt(IPV6_MULTICAST_HOPS)");

	if (IN6_IS_ADDR_UNSPECIFIED(&maddr)) {
		if (inet_pton(AF_INET6, "ff02::1", &maddr) != 1)
			errx(1, "inet_pton failed");
	}

	mreq.ipv6mr_multiaddr = maddr;
	mreq.ipv6mr_interface = ifindex;
	if (setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq,
		       sizeof(mreq)) == -1)
		err(1, "setsockopt(IPV6_JOIN_GROUP)");

	ICMP6_FILTER_SETBLOCKALL(&filt);
	ICMP6_FILTER_SETPASS(ICMP6_MEMBERSHIP_QUERY, &filt);
	ICMP6_FILTER_SETPASS(ICMP6_MEMBERSHIP_REPORT, &filt);
	ICMP6_FILTER_SETPASS(ICMP6_MEMBERSHIP_REDUCTION, &filt);
	if (setsockopt(s, IPPROTO_ICMPV6, ICMP6_FILTER, &filt,
			sizeof(filt)) < 0)
		err(1, "setsockopt(ICMP6_FILTER)");

	make_msg(ifindex, &maddr, type, qaddr);

	if (sendmsg(s, &m, 0) < 0)
		err(1, "sendmsg");

	itimer.it_value.tv_sec =  QUERY_RESPONSE_INTERVAL / 1000;
	itimer.it_interval.tv_sec = 0;
	itimer.it_interval.tv_usec = 0;
	itimer.it_value.tv_usec = 0;

	(void)signal(SIGALRM, quit);
	(void)setitimer(ITIMER_REAL, &itimer, NULL);

	FD_ZERO(&fdset);
	if (s >= FD_SETSIZE)
		errx(1, "descriptor too big");
	for (;;) {
		FD_SET(s, &fdset);
		if ((i = select(s + 1, &fdset, NULL, NULL, NULL)) < 0)
			perror("select");
		if (i == 0)
			continue;
		else
			dump(s);
	}
}

void
make_msg(int index, struct in6_addr *addr, u_int type, struct in6_addr *qaddr)
{
	static struct iovec iov[2];
	static u_char *cmsgbuf;
	int cmsglen, hbhlen = 0;
#ifdef USE_RFC2292BIS
	void *hbhbuf = NULL, *optp = NULL;
	int currentlen;
#else
	u_int8_t raopt[IP6OPT_RTALERT_LEN];
#endif 
	struct in6_pktinfo *pi;
	struct cmsghdr *cmsgp;
	u_short rtalert_code = htons(IP6OPT_RTALERT_MLD);
	struct ifaddrs *ifa, *ifap;
	struct in6_addr src;

	dst.sin6_len = sizeof(dst);
	dst.sin6_family = AF_INET6;
	dst.sin6_addr = *addr;
	m.msg_name = (caddr_t)&dst;
	m.msg_namelen = dst.sin6_len;
	iov[0].iov_base = (caddr_t)&mldh;
	iov[0].iov_len = sizeof(mldh);
	m.msg_iov = iov;
	m.msg_iovlen = 1;

	bzero(&mldh, sizeof(mldh));
	mldh.mld_type = type & 0xff;
	mldh.mld_maxdelay = htons(QUERY_RESPONSE_INTERVAL);
	mldh.mld_addr = *qaddr;

	/* MLD packet should be advertised from linklocal address */
	getifaddrs(&ifa);
	for (ifap = ifa; ifap; ifap = ifap->ifa_next) {
		if (index != if_nametoindex(ifap->ifa_name))
			continue;

		if (ifap->ifa_addr->sa_family != AF_INET6)
			continue;
		if (!IN6_IS_ADDR_LINKLOCAL(&((struct sockaddr_in6 *)
					    ifap->ifa_addr)->sin6_addr))
			continue;
		break;
	}
	if (ifap == NULL)
		errx(1, "no linkocal address is available");
	memcpy(&src, &((struct sockaddr_in6 *)ifap->ifa_addr)->sin6_addr,
	       sizeof(src));
	freeifaddrs(ifa);
#ifdef __KAME__
	/* remove embedded ifindex */
	src.s6_addr[2] = src.s6_addr[3] = 0;
#endif

#ifdef USE_RFC2292BIS
	if ((hbhlen = inet6_opt_init(NULL, 0)) == -1)
		errx(1, "inet6_opt_init(0) failed");
	if ((hbhlen = inet6_opt_append(NULL, 0, hbhlen, IP6OPT_ROUTER_ALERT, 2,
				       2, NULL)) == -1)
		errx(1, "inet6_opt_append(0) failed");
	if ((hbhlen = inet6_opt_finish(NULL, 0, hbhlen)) == -1)
		errx(1, "inet6_opt_finish(0) failed");
	cmsglen = CMSG_SPACE(sizeof(struct in6_pktinfo)) + CMSG_SPACE(hbhlen);
#else
	hbhlen = sizeof(raopt); 
	cmsglen = CMSG_SPACE(sizeof(struct in6_pktinfo)) +
	    inet6_option_space(hbhlen);
#endif 

	if ((cmsgbuf = malloc(cmsglen)) == NULL)
		errx(1, "can't allocate enough memory for cmsg");
	cmsgp = (struct cmsghdr *)cmsgbuf;
	m.msg_control = (caddr_t)cmsgbuf;
	m.msg_controllen = cmsglen;
	/* specify the outgoing interface */
	cmsgp->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
	cmsgp->cmsg_level = IPPROTO_IPV6;
	cmsgp->cmsg_type = IPV6_PKTINFO;
	pi = (struct in6_pktinfo *)CMSG_DATA(cmsgp);
	pi->ipi6_ifindex = index;
	memcpy(&pi->ipi6_addr, &src, sizeof(pi->ipi6_addr));
	/* specifiy to insert router alert option in a hop-by-hop opt hdr. */
	cmsgp = CMSG_NXTHDR(&m, cmsgp);
#ifdef USE_RFC2292BIS
	cmsgp->cmsg_len = CMSG_LEN(hbhlen);
	cmsgp->cmsg_level = IPPROTO_IPV6;
	cmsgp->cmsg_type = IPV6_HOPOPTS;
	hbhbuf = CMSG_DATA(cmsgp);
	if ((currentlen = inet6_opt_init(hbhbuf, hbhlen)) == -1)
		errx(1, "inet6_opt_init(len = %d) failed", hbhlen);
	if ((currentlen = inet6_opt_append(hbhbuf, hbhlen, currentlen,
					   IP6OPT_ROUTER_ALERT, 2,
					   2, &optp)) == -1)
		errx(1, "inet6_opt_append(currentlen = %d, hbhlen = %d) failed",
		     currentlen, hbhlen);
	(void)inet6_opt_set_val(optp, 0, &rtalert_code, sizeof(rtalert_code));
	if ((currentlen = inet6_opt_finish(hbhbuf, hbhlen, currentlen)) == -1)
		errx(1, "inet6_opt_finish(buf) failed");
#else  /* old advanced API */
	if (inet6_option_init((void *)cmsgp, &cmsgp, IPV6_HOPOPTS))
		errx(1, "inet6_option_init failed\n");
	raopt[0] = IP6OPT_ROUTER_ALERT;
	raopt[1] = IP6OPT_RTALERT_LEN - 2;
	memcpy(&raopt[2], (caddr_t)&rtalert_code, sizeof(u_short));
	if (inet6_option_append(cmsgp, raopt, 4, 0))
		errx(1, "inet6_option_append failed\n");
#endif 
}

void
dump(int s)
{
	int i;
	struct mld_hdr *mld;
	u_char buf[1024];
	struct sockaddr_in6 from;
	int from_len = sizeof(from);
	char ntop_buf[256];

	if ((i = recvfrom(s, buf, sizeof(buf), 0,
			  (struct sockaddr *)&from,
			  &from_len)) < 0)
		return;

	if (i < sizeof(struct mld_hdr)) {
		printf("too short!\n");
		return;
	}

	mld = (struct mld_hdr *)buf;

	printf("from %s, ", inet_ntop(AF_INET6, &from.sin6_addr,
				      ntop_buf, sizeof(ntop_buf)));

	switch (mld->mld_type) {
	case ICMP6_MEMBERSHIP_QUERY:
		printf("type=Multicast Listener Query, ");
		break;
	case ICMP6_MEMBERSHIP_REPORT:
		printf("type=Multicast Listener Report, ");
		break;
	case ICMP6_MEMBERSHIP_REDUCTION:
		printf("type=Multicast Listener Done, ");
		break;
	}
	printf("addr=%s\n", inet_ntop(AF_INET6, &mld->mld_addr,
				    ntop_buf, sizeof(ntop_buf)));
	
	fflush(stdout);
}

void
quit(int signum __unused)
{
	mreq.ipv6mr_multiaddr = maddr;
	mreq.ipv6mr_interface = ifindex;
	if (setsockopt(s, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq,
		       sizeof(mreq)) == -1)
		err(1, "setsockopt(IPV6_LEAVE_GROUP)");

	exit(0);
}

void
usage(void)
{
	(void)fprintf(stderr, "usage: mld6query [-dgr] ifname [addr]\n");
	exit(1);
}
