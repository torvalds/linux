/*	$KAME: rrenumd.c,v 1.20 2000/11/08 02:40:53 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/time.h>

#include <string.h>

#include <net/route.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>

#include <arpa/inet.h>

#ifdef IPSEC
#include <netipsec/ipsec.h>
#endif

#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>

#include "rrenumd.h"

#define LL_ALLROUTERS "ff02::2"
#define SL_ALLROUTERS "ff05::2"

#define RR_MCHLIM_DEFAULT 64

#ifndef IN6_IS_SCOPE_LINKLOCAL
#define IN6_IS_SCOPE_LINKLOCAL(a)	\
	((IN6_IS_ADDR_LINKLOCAL(a)) ||	\
	 (IN6_IS_ADDR_MC_LINKLOCAL(a)))
#endif /* IN6_IS_SCOPE_LINKLOCAL */

struct flags {
	u_long debug : 1;
	u_long fg : 1;
#ifdef IPSEC
#ifdef IPSEC_POLICY_IPSEC
	u_long policy : 1;
#else /* IPSEC_POLICY_IPSEC */
	u_long auth : 1;
	u_long encrypt : 1;
#endif /* IPSEC_POLICY_IPSEC */
#endif /*IPSEC*/
};

struct msghdr sndmhdr;
struct msghdr rcvmhdr;
struct sockaddr_in6 from;
struct sockaddr_in6 sin6_ll_allrouters;

int s4, s6;
int with_v4dest, with_v6dest;
struct in6_addr prefix; /* ADHOC */
int prefixlen = 64; /* ADHOC */

extern int parse(FILE **);

static void show_usage(void);
static void init_sin6(struct sockaddr_in6 *, const char *);
#if 0
static void join_multi(const char *);
#endif
static void init_globals(void);
static void config(FILE **);
#ifdef IPSEC_POLICY_IPSEC
static void sock6_open(struct flags *, char *);
static void sock4_open(struct flags *, char *);
#else
static void sock6_open(struct flags *);
static void sock4_open(struct flags *);
#endif
static void rrenum_output(struct payload_list *, struct dst_list *);
static void rrenum_snd_eachdst(struct payload_list *);
#if 0
static void rrenum_snd_fullsequence(void);
#endif
static void rrenum_input(int);
int main(int, char *[]);


/* Print usage. Don't call this after daemonized. */
static void
show_usage()
{
	fprintf(stderr, "usage: rrenumd [-c conf_file|-s] [-df"
#ifdef IPSEC
#ifdef IPSEC_POLICY_IPSEC
		"] [-P policy"
#else /* IPSEC_POLICY_IPSEC */
		"AE"
#endif /* IPSEC_POLICY_IPSEC */
#endif /* IPSEC */
		"]\n");
	exit(1);
}

static void
init_sin6(struct sockaddr_in6 *sin6, const char *addr_ascii)
{
	memset(sin6, 0, sizeof(*sin6));
	sin6->sin6_len = sizeof(*sin6);
	sin6->sin6_family = AF_INET6;
	if (inet_pton(AF_INET6, addr_ascii, &sin6->sin6_addr) != 1)
		; /* XXX do something */
}

#if 0  /* XXX: not necessary ?? */
static void
join_multi(const char *addrname)
{
	struct ipv6_mreq mreq;

	if (inet_pton(AF_INET6, addrname, &mreq.ipv6mr_multiaddr.s6_addr)
	    != 1) {
		syslog(LOG_ERR, "<%s> inet_pton failed(library bug?)",
		       __func__);
		exit(1);
	}
	/* ADHOC: currently join only one */
	{
		if ((mreq.ipv6mr_interface = if_nametoindex(ifname)) == 0) {
			syslog(LOG_ERR, "<%s> ifname %s should be invalid: %s",
			       __func__, ifname, strerror(errno));
			exit(1);
		}
		if (setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP,
			       &mreq,
			       sizeof(mreq)) < 0) {
			syslog(LOG_ERR, "<%s> IPV6_JOIN_GROUP on %s: %s",
			       __func__, ifname, strerror(errno));
			exit(1);
		}
	}
}
#endif

static void
init_globals()
{
	static struct iovec rcviov;
	static u_char rprdata[4500]; /* maximal MTU of connected links */
	static u_char *rcvcmsgbuf = NULL;
	static u_char *sndcmsgbuf = NULL;
	int sndcmsglen, rcvcmsglen;

	/* init ll_allrouters */
	init_sin6(&sin6_ll_allrouters, LL_ALLROUTERS);

	/* initialize msghdr for receiving packets */
	rcviov.iov_base = (caddr_t)rprdata;
	rcviov.iov_len = sizeof(rprdata);
	rcvmhdr.msg_namelen = sizeof(struct sockaddr_in6);
	rcvmhdr.msg_iov = &rcviov;
	rcvmhdr.msg_iovlen = 1;
	rcvcmsglen = CMSG_SPACE(sizeof(struct in6_pktinfo)) +
		CMSG_SPACE(sizeof(int));
	if (rcvcmsgbuf == NULL &&
	    (rcvcmsgbuf = (u_char *)malloc(rcvcmsglen)) == NULL) {
		syslog(LOG_ERR, "<%s>: malloc failed", __func__);
		exit(1);
	}
	rcvmhdr.msg_control = (caddr_t)rcvcmsgbuf;
	rcvmhdr.msg_controllen = rcvcmsglen;

	/* initialize msghdr for sending packets */
	sndmhdr.msg_namelen = sizeof(struct sockaddr_in6);
	sndmhdr.msg_iovlen = 1;
	sndcmsglen = CMSG_SPACE(sizeof(struct in6_pktinfo)) +
		CMSG_SPACE(sizeof(int));
	if (sndcmsgbuf == NULL &&
	    (sndcmsgbuf = (u_char *)malloc(sndcmsglen)) == NULL) {
		syslog(LOG_ERR, "<%s>: malloc failed", __func__);
		exit(1);
	}
	sndmhdr.msg_control = (caddr_t)sndcmsgbuf;
	sndmhdr.msg_controllen = sndcmsglen;
}

static void
config(FILE **fpp)
{
	struct payload_list *pl;
	struct iovec *iov;
	struct icmp6_router_renum *irr;
	struct rr_pco_match *rpm;

	if (parse(fpp) < 0) {
		syslog(LOG_ERR, "<%s> parse failed", __func__);
		exit(1);
	}

	/* initialize fields not configured by parser */
	for (pl = pl_head; pl; pl = pl->pl_next) {
		iov = (struct iovec *)&pl->pl_sndiov;
		irr = (struct icmp6_router_renum *)&pl->pl_irr;
		rpm = (struct rr_pco_match *)&pl->pl_rpm;

		irr->rr_type = ICMP6_ROUTER_RENUMBERING;
		irr->rr_code = 0;
		/*
		 * now we don't support multiple PCOs in a rr message.
		 * so segment number is not supported.
		 */
		/* TODO: rr flags config in parser */
		irr->rr_flags |= ICMP6_RR_FLAGS_SPECSITE;
		/* TODO: max delay config in parser */

		/*
		 * means only 1 use_prefix is contained as router-renum-05.txt.
		 * now we don't support multiple PCOs in a rr message,
		 * nor multiple use_prefix in one PCO.
		 */
		rpm->rpm_len = 4*1 +3;
		rpm->rpm_ordinal = 0;
		iov->iov_base = (caddr_t)irr;
		iov->iov_len =  sizeof(struct icmp6_router_renum)
			+ sizeof(struct rr_pco_match)
			+ sizeof(struct rr_pco_use);
	}
}

static void
sock6_open(struct flags *flags
#ifdef IPSEC_POLICY_IPSEC
	   , char *policy
#endif /* IPSEC_POLICY_IPSEC */
	   )
{
	struct icmp6_filter filt;
	int on;
#ifdef IPSEC
#ifndef IPSEC_POLICY_IPSEC
	int optval;
#endif
#endif

	if (with_v6dest == 0)
		return;
	if (with_v6dest &&
	    (s6 = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6)) < 0) {
		syslog(LOG_ERR, "<%s> socket(v6): %s", __func__,
		       strerror(errno));
		exit(1);
	}

	/*
	 * join all routers multicast addresses.
	 */
#if 0 /* XXX: not necessary ?? */
	join_multi(LL_ALLROUTERS);
	join_multi(SL_ALLROUTERS);
#endif

	/* set icmpv6 filter */
	ICMP6_FILTER_SETBLOCKALL(&filt);
	ICMP6_FILTER_SETPASS(ICMP6_ROUTER_RENUMBERING, &filt);
	if (setsockopt(s6, IPPROTO_ICMPV6, ICMP6_FILTER, &filt,
		       sizeof(filt)) < 0) {
		syslog(LOG_ERR, "<%s> IICMP6_FILTER: %s",
		       __func__, strerror(errno));
		exit(1);
	}

	/* specify to tell receiving interface */
	on = 1;
	if (setsockopt(s6, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on,
		       sizeof(on)) < 0) {
		syslog(LOG_ERR, "<%s> IPV6_RECVPKTINFO: %s",
		       __func__, strerror(errno));
		exit(1);
	}

#ifdef IPSEC
#ifdef IPSEC_POLICY_IPSEC
	if (flags->policy) {
		char *buf;
		buf = ipsec_set_policy(policy, strlen(policy));
		if (buf == NULL)
			errx(1, "%s", ipsec_strerror());
		/* XXX should handle in/out bound policy. */
		if (setsockopt(s6, IPPROTO_IPV6, IPV6_IPSEC_POLICY,
				buf, ipsec_get_policylen(buf)) < 0)
			err(1, "setsockopt(IPV6_IPSEC_POLICY)");
		free(buf);
	}
#else /* IPSEC_POLICY_IPSEC */
	if (flags->auth) {
		optval = IPSEC_LEVEL_REQUIRE;
		if (setsockopt(s6, IPPROTO_IPV6, IPV6_AUTH_TRANS_LEVEL,
			       &optval, sizeof(optval)) == -1) {
			syslog(LOG_ERR, "<%s> IPV6_AUTH_TRANS_LEVEL: %s",
			       __func__, strerror(errno));
			exit(1);
		}
	}
	if (flags->encrypt) {
		optval = IPSEC_LEVEL_REQUIRE;
		if (setsockopt(s6, IPPROTO_IPV6, IPV6_ESP_TRANS_LEVEL,
				&optval, sizeof(optval)) == -1) {
			syslog(LOG_ERR, "<%s> IPV6_ESP_TRANS_LEVEL: %s",
			       __func__, strerror(errno));
			exit(1);
		}
	}
#endif /* IPSEC_POLICY_IPSEC */
#endif /* IPSEC */

	return;
}

static void
sock4_open(struct flags *flags
#ifdef IPSEC_POLICY_IPSEC
	   , char *policy
#endif /* IPSEC_POLICY_IPSEC */
	   )
{
#ifdef IPSEC
#ifndef IPSEC_POLICY_IPSEC
	int optval;
#endif
#endif

	if (with_v4dest == 0)
		return;
	if ((s4 = socket(AF_INET, SOCK_RAW, IPPROTO_ICMPV6)) < 0) {
		syslog(LOG_ERR, "<%s> socket(v4): %s", __func__,
		       strerror(errno));
		exit(1);
	}

#if 0 /* XXX: not necessary ?? */
	/*
	 * join all routers multicast addresses.
	 */
	some_join_function();
#endif

#ifdef IPSEC
#ifdef IPSEC_POLICY_IPSEC
	if (flags->policy) {
		char *buf;
		buf = ipsec_set_policy(policy, strlen(policy));
		if (buf == NULL)
			errx(1, "%s", ipsec_strerror());
		/* XXX should handle in/out bound policy. */
		if (setsockopt(s4, IPPROTO_IP, IP_IPSEC_POLICY,
				buf, ipsec_get_policylen(buf)) < 0)
			err(1, "setsockopt(IP_IPSEC_POLICY)");
		free(buf);
	}
#else /* IPSEC_POLICY_IPSEC */
	if (flags->auth) {
		optval = IPSEC_LEVEL_REQUIRE;
		if (setsockopt(s4, IPPROTO_IP, IP_AUTH_TRANS_LEVEL,
			       &optval, sizeof(optval)) == -1) {
			syslog(LOG_ERR, "<%s> IP_AUTH_TRANS_LEVEL: %s",
			       __func__, strerror(errno));
			exit(1);
		}
	}
	if (flags->encrypt) {
		optval = IPSEC_LEVEL_REQUIRE;
		if (setsockopt(s4, IPPROTO_IP, IP_ESP_TRANS_LEVEL,
				&optval, sizeof(optval)) == -1) {
			syslog(LOG_ERR, "<%s> IP_ESP_TRANS_LEVEL: %s",
			       __func__, strerror(errno));
			exit(1);
		}
	}
#endif /* IPSEC_POLICY_IPSEC */
#endif /* IPSEC */

	return;
}

static void
rrenum_output(struct payload_list *pl, struct dst_list *dl)
{
	int i, msglen = 0;
	struct cmsghdr *cm;
	struct in6_pktinfo *pi;
	struct sockaddr_in6 *sin6 = NULL;

	sndmhdr.msg_name = (caddr_t)dl->dl_dst;
	if (dl->dl_dst->sa_family == AF_INET6)
		sin6 = (struct sockaddr_in6 *)dl->dl_dst;

	if (sin6 != NULL &&
	    IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr)) {
		int hoplimit = RR_MCHLIM_DEFAULT;

		cm = CMSG_FIRSTHDR(&sndmhdr);
		/* specify the outgoing interface */
		cm->cmsg_level = IPPROTO_IPV6;
		cm->cmsg_type = IPV6_PKTINFO;
		cm->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
		pi = (struct in6_pktinfo *)CMSG_DATA(cm);
		memset(&pi->ipi6_addr, 0, sizeof(pi->ipi6_addr));	/*XXX*/
		pi->ipi6_ifindex = sin6->sin6_scope_id;
		msglen += CMSG_LEN(sizeof(struct in6_pktinfo));

		/* specify the hop limit of the packet if dest is link local */
		/* not defined by router-renum-05.txt, but maybe its OK */
		cm = CMSG_NXTHDR(&sndmhdr, cm);
		cm->cmsg_level = IPPROTO_IPV6;
		cm->cmsg_type = IPV6_HOPLIMIT;
		cm->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(cm), &hoplimit, sizeof(int));
		msglen += CMSG_LEN(sizeof(int));
	}
	sndmhdr.msg_controllen = msglen;
	if (sndmhdr.msg_controllen == 0)
		sndmhdr.msg_control = 0;

	sndmhdr.msg_iov = &pl->pl_sndiov;
	i = sendmsg(dl->dl_dst->sa_family == AF_INET ? s4 : s6, &sndmhdr, 0);

	if (i < 0 || i != sndmhdr.msg_iov->iov_len)
		syslog(LOG_ERR, "<%s> sendmsg: %s", __func__,
		       strerror(errno));
}

static void
rrenum_snd_eachdst(struct payload_list *pl)
{
	struct dst_list *dl;

	for (dl = dl_head; dl; dl = dl->dl_next) {
		rrenum_output(pl, dl);
	}
}

#if 0
static void
rrenum_snd_fullsequence()
{
	struct payload_list *pl;

	for (pl = pl_head; pl; pl = pl->pl_next) {
		rrenum_snd_eachdst(pl);
	}
}
#endif

static void
rrenum_input(int s)
{
	int i;
	struct icmp6_router_renum *rr;

	/* get message */
	if ((i = recvmsg(s, &rcvmhdr, 0)) < 0) {
		syslog(LOG_ERR, "<%s> recvmsg: %s", __func__,
		       strerror(errno));
		return;
	}
	if (s == s4)
		i -= sizeof(struct ip);
	if (i < sizeof(struct icmp6_router_renum)) {
		syslog(LOG_ERR, "<%s> packet size(%d) is too short",
		       __func__, i);
		return;
	}
	if (s == s4) {
		struct ip *ip = (struct ip *)rcvmhdr.msg_iov->iov_base;

		rr = (struct icmp6_router_renum *)(ip + 1);
	} else /* s == s6 */
		rr = (struct icmp6_router_renum *)rcvmhdr.msg_iov->iov_base;

	switch(rr->rr_code) {
	case ICMP6_ROUTER_RENUMBERING_COMMAND:
		/* COMMAND will be processed by rtadvd */
		break;
	case ICMP6_ROUTER_RENUMBERING_RESULT:
		/* TODO: receiving result message */
		break;
	default:
		syslog(LOG_ERR,	"<%s> received unknown code %d",
		       __func__, rr->rr_code);
		break;
	}
}

int
main(int argc, char *argv[])
{
	FILE *fp = stdin;
	fd_set fdset;
	struct timeval timeout;
	int ch, i, maxfd = 0, send_counter = 0;
	struct flags flags;
	struct payload_list *pl;
#ifdef IPSEC_POLICY_IPSEC
	char *policy = NULL;
#endif

	memset(&flags, 0, sizeof(flags));
	openlog("rrenumd", LOG_PID, LOG_DAEMON);

	/* get options */
	while ((ch = getopt(argc, argv, "c:sdf"
#ifdef IPSEC
#ifdef IPSEC_POLICY_IPSEC
			    "P:"
#else /* IPSEC_POLICY_IPSEC */
			    "AE"
#endif /* IPSEC_POLICY_IPSEC */
#endif /* IPSEC */
			    )) != -1){
		switch (ch) {
		case 'c':
			if((fp = fopen(optarg, "r")) == NULL) {
				syslog(LOG_ERR,
				       "<%s> config file %s open failed",
				       __func__, optarg);
				exit(1);
			}
			break;
		case 's':
			fp = stdin;
			break;
		case 'd':
			flags.debug = 1;
			break;
		case 'f':
			flags.fg = 1;
			break;
#ifdef IPSEC
#ifdef IPSEC_POLICY_IPSEC
		case 'P':
			flags.policy = 1;
			policy = strdup(optarg);
			break;
#else /* IPSEC_POLICY_IPSEC */
		case 'A':
			flags.auth = 1;
			break;
		case 'E':
			flags.encrypt = 1;
			break;
#endif /* IPSEC_POLICY_IPSEC */
#endif /*IPSEC*/
		default:
			show_usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* set log level */
	if (flags.debug == 0)
		(void)setlogmask(LOG_UPTO(LOG_ERR));
	if (flags.debug == 1)
		(void)setlogmask(LOG_UPTO(LOG_INFO));

	/* init global variables */
	init_globals();

	config(&fp);

	sock6_open(&flags
#ifdef IPSEC_POLICY_IPSEC
		   , policy
#endif /* IPSEC_POLICY_IPSEC */
		   );
	sock4_open(&flags
#ifdef IPSEC_POLICY_IPSEC
		   , policy
#endif /* IPSEC_POLICY_IPSEC */
		   );

	if (!flags.fg)
		daemon(0, 0);

	FD_ZERO(&fdset);
	if (with_v6dest) {
		FD_SET(s6, &fdset);
		if (s6 > maxfd)
			maxfd = s6;
	}
	if (with_v4dest) {
		FD_SET(s4, &fdset);
		if (s4 > maxfd)
			maxfd = s4;
	}

	/* ADHOC: timeout each 30seconds */
	memset(&timeout, 0, sizeof(timeout));

	/* init temporary payload_list and send_counter*/
	pl = pl_head;
	send_counter = retry + 1;
	while (1) {
		struct fd_set select_fd = fdset; /* reinitialize */

		if ((i = select(maxfd + 1, &select_fd, NULL, NULL,
				&timeout)) < 0){
			syslog(LOG_ERR, "<%s> select: %s",
			       __func__, strerror(errno));
			continue;
		}
		if (i == 0) {	/* timeout */
			if (pl == NULL)
				exit(0);
			rrenum_snd_eachdst(pl);
			send_counter--;
			timeout.tv_sec = 30;
			if (send_counter == 0) {
				timeout.tv_sec = 0;
				pl = pl->pl_next;
				send_counter = retry + 1;
			}
		}
		if (FD_ISSET(s4, &select_fd))
			rrenum_input(s4);
		if (FD_ISSET(s6, &select_fd))
			rrenum_input(s6);
	}
}
