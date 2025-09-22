/*	$NetBSD: mrinfo.c,v 1.4 1995/12/10 11:00:51 mycroft Exp $	*/

/*
 * This tool requests configuration info from a multicast router
 * and prints the reply (if any).  Invoke it as:
 *
 *	mrinfo router-name-or-address
 *
 * Written Wed Mar 24 1993 by Van Jacobson (adapted from the
 * multicast mapper written by Pavel Curtis).
 *
 * The lawyers insist we include the following UC copyright notice.
 * The mapper from which this is derived contained a Xerox copyright
 * notice which follows the UC one.  Try not to get depressed noting
 * that the legal gibberish is larger than the program.
 *
 * Copyright (c) 1993 Regents of the University of California.
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
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
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
 * ---------------------------------
 * Copyright (c) 1992, 2001 Xerox Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.

 * Neither name of the Xerox, PARC, nor the names of its contributors may be used
 * to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE XEROX CORPORATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>
#include <netdb.h>
#include <sys/time.h>
#include "defs.h"
#include <arpa/inet.h>
#include <stdarg.h>
#include <poll.h>
#include <limits.h>
#include <err.h>

#define DEFAULT_TIMEOUT	4	/* How long to wait before retrying requests */
#define DEFAULT_RETRIES 3	/* How many times to ask each router */

u_int32_t	our_addr, target_addr = 0;	/* in NET order */
int     debug = 0;
int	nflag = 0;
int     retries = DEFAULT_RETRIES;
int     timeout = DEFAULT_TIMEOUT;
int	target_level = 0;
vifi_t  numvifs;		/* to keep loader happy */
				/* (see COPY_TABLES macro called in kern.c) */

char		*inet_name(u_int32_t addr);
void		ask(u_int32_t dst);
void		ask2(u_int32_t dst);
void		usage(void);

char *
inet_name(u_int32_t addr)
{
	struct hostent *e;
	struct in_addr in;

	if (addr == 0)
		return "local";

	if (nflag ||
	    (e = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET)) == NULL) {
		in.s_addr = addr;
		return (inet_ntoa(in));
	}
	return (e->h_name);
}

/*
 * Log errors and other messages to stderr, according to the severity of the
 * message and the current debug level.  For errors of severity LOG_ERR or
 * worse, terminate the program.
 */
void
logit(int severity, int syserr, char *format, ...)
{
	va_list ap;

	switch (debug) {
	case 0:
		if (severity > LOG_WARNING)
			return;
	case 1:
		if (severity > LOG_NOTICE)
			return;
	case 2:
		if (severity > LOG_INFO)
			return;
	default:
		if (severity == LOG_WARNING)
			fprintf(stderr, "warning - ");
		va_start(ap, format);
		vfprintf(stderr, format, ap);
		va_end(ap);
		if (syserr == 0)
			fputc('\n', stderr);
		else if (syserr < sys_nerr)
			fprintf(stderr, ": %s\n", sys_errlist[syserr]);
		else
			fprintf(stderr, ": errno %d\n", syserr);
	}

	if (severity <= LOG_ERR)
		exit(1);
}

/*
 * Send a neighbors-list request.
 */
void
ask(u_int32_t dst)
{
	send_igmp(our_addr, dst, IGMP_DVMRP, DVMRP_ASK_NEIGHBORS,
	    htonl(MROUTED_LEVEL), 0);
}

void
ask2(u_int32_t dst)
{
	send_igmp(our_addr, dst, IGMP_DVMRP, DVMRP_ASK_NEIGHBORS2,
	    htonl(MROUTED_LEVEL), 0);
}

/*
 * Process an incoming neighbor-list message.
 */
void
accept_neighbors(u_int32_t src, u_int32_t dst, u_char *p, int datalen,
    u_int32_t level)
{
	u_char *ep = p + datalen;

#define GET_ADDR(a) (a = ((u_int32_t)*p++ << 24), a += ((u_int32_t)*p++ << 16),\
		     a += ((u_int32_t)*p++ << 8), a += *p++)

	printf("%s (%s):\n", inet_fmt(src, s1), inet_name(src));
	while (p < ep) {
		u_char metric, thresh;
		u_int32_t laddr;
		int ncount;

		GET_ADDR(laddr);
		laddr = htonl(laddr);
		metric = *p++;
		thresh = *p++;
		ncount = *p++;
		while (--ncount >= 0) {
			u_int32_t neighbor;

			GET_ADDR(neighbor);
			neighbor = htonl(neighbor);
			printf("  %s -> ", inet_fmt(laddr, s1));
			printf("%s (%s) [%d/%d]\n", inet_fmt(neighbor, s1),
			    inet_name(neighbor), metric, thresh);
		}
	}
}

void
accept_neighbors2(u_int32_t src, u_int32_t dst, u_char *p, int datalen,
    u_int32_t level)
{
	u_char *ep = p + datalen;
	u_int broken_cisco = ((level & 0xffff) == 0x020a); /* 10.2 */
	/* well, only possibly_broken_cisco, but that's too long to type. */

	printf("%s (%s) [version %d.%d", inet_fmt(src, s1), inet_name(src),
	    level & 0xff, (level >> 8) & 0xff);
	if ((level >> 16) & NF_LEAF)
		printf (",leaf");
	if ((level >> 16) & NF_PRUNE)
		printf (",prune");
	if ((level >> 16) & NF_GENID)
		printf (",genid");
	if ((level >> 16) & NF_MTRACE)
		printf (",mtrace");
	printf ("]:\n");

	while (p < ep) {
		u_char metric, thresh, flags;
		u_int32_t laddr = *(u_int32_t*)p;
		int ncount;

		p += 4;
		metric = *p++;
		thresh = *p++;
		flags = *p++;
		ncount = *p++;
		if (broken_cisco && ncount == 0)	/* dumb Ciscos */
			ncount = 1;
		if (broken_cisco && ncount > 15)	/* dumb Ciscos */
			ncount = ncount & 0xf;
		while (--ncount >= 0 && p < ep) {
			u_int32_t neighbor = *(u_int32_t*)p;
			p += 4;
			printf("  %s -> ", inet_fmt(laddr, s1));
			printf("%s (%s) [%d/%d", inet_fmt(neighbor, s1),
			    inet_name(neighbor), metric, thresh);
			if (flags & DVMRP_NF_TUNNEL)
				printf("/tunnel");
			if (flags & DVMRP_NF_SRCRT)
				printf("/srcrt");
			if (flags & DVMRP_NF_PIM)
				printf("/pim");
			if (flags & DVMRP_NF_QUERIER)
				printf("/querier");
			if (flags & DVMRP_NF_DISABLED)
				printf("/disabled");
			if (flags & DVMRP_NF_DOWN)
				printf("/down");
			if (flags & DVMRP_NF_LEAF)
				printf("/leaf");
			printf("]\n");
		}
	}
}

void
usage()
{
	fprintf(stderr,
	    "Usage: mrinfo [-d[debug_level]] [-n] [-t timeout] [-r retries] [router]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int tries, trynew, curaddr, udp, ch;
	struct hostent *hp, bogus;
	struct sockaddr_in addr;
	socklen_t addrlen;
	struct timeval et;
	char *host;
	uid_t uid;
	const char *errstr;

	if (geteuid() != 0) {
		fprintf(stderr, "mrinfo: must be root\n");
		exit(1);
	}

	init_igmp();

	uid = getuid();
	if (setresuid(uid, uid, uid) == -1)
		err(1, "setresuid");

	setvbuf(stderr, NULL, _IOLBF, 0);

	while ((ch = getopt(argc, argv, "d::nr:t:")) != -1) {
		switch (ch) {
		case 'd':
			if (!optarg)
				debug = DEFAULT_DEBUG;
			else {
				debug = strtonum(optarg, 0, 3, &errstr);
				if (errstr) {
					warnx("debug level %s", errstr);
					debug = DEFAULT_DEBUG;
				}
			}
			break;
		case 'n':
			++nflag;
			break;
		case 'r':
			retries = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr) {
				warnx("retries %s", errstr);
				usage();
			}
			break;
		case 't':
			timeout = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr) {
				warnx("timeout %s", errstr);
				usage();
			}
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
			
	if (argc > 1)
		usage();
	if (argc == 1)
		host = argv[0];
	else
		host = "127.0.0.1";

	if ((target_addr = inet_addr(host)) != -1) {
		hp = &bogus;
		hp->h_length = sizeof(target_addr);
		if (!(hp->h_addr_list = calloc(2, sizeof(char *))))
			err(1, "can't allocate memory");
		if (!(hp->h_addr_list[0] = malloc(hp->h_length)))
			err(1, "can't allocate memory");
		memcpy(hp->h_addr_list[0], &target_addr, hp->h_length);
		hp->h_addr_list[1] = 0;
	} else
		hp = gethostbyname(host);

	if (hp == NULL || hp->h_length != sizeof(target_addr)) {
		fprintf(stderr, "mrinfo: %s: no such host\n", argv[0]);
		exit(1);
	}
	if (debug)
		fprintf(stderr, "Debug level %u\n", debug);

	/* Check all addresses; mrouters often have unreachable interfaces */
	for (curaddr = 0; hp->h_addr_list[curaddr] != NULL; curaddr++) {
		memcpy(&target_addr, hp->h_addr_list[curaddr], hp->h_length);
		/* Find a good local address for us. */
		addrlen = sizeof(addr);
		memset(&addr, 0, sizeof addr);
		addr.sin_family = AF_INET;
		addr.sin_len = sizeof addr;
		addr.sin_addr.s_addr = target_addr;
		addr.sin_port = htons(2000);	/* any port over 1024 will
						 * do... */
		if ((udp = socket(AF_INET, SOCK_DGRAM, 0)) == -1 ||
		    connect(udp, (struct sockaddr *) & addr, sizeof(addr)) == -1 ||
		    getsockname(udp, (struct sockaddr *) & addr, &addrlen) == -1) {
			perror("Determining local address");
			exit(1);
		}
		close(udp);
		our_addr = addr.sin_addr.s_addr;

		tries = 0;
		trynew = 1;
		/*
		 * New strategy: send 'ask2' for two timeouts, then fall back
		 * to 'ask', since it's not very likely that we are going to
		 * find someone who only responds to 'ask' these days
		 */
		ask2(target_addr);

		gettimeofday(&et, 0);
		et.tv_sec += timeout;

		/* Main receive loop */
		for (;;) {
			int count, recvlen, ipdatalen, iphdrlen, igmpdatalen;
			u_int32_t src, dst, group;
			struct timeval tv, now;
			socklen_t dummy = 0;
			struct igmp *igmp;
			struct ip *ip;
			struct pollfd pfd[1];

			pfd[0].fd = igmp_socket;
			pfd[0].events = POLLIN;

			gettimeofday(&now, 0);
			tv.tv_sec = et.tv_sec - now.tv_sec;
			tv.tv_usec = et.tv_usec - now.tv_usec;

			if (tv.tv_usec < 0) {
				tv.tv_usec += 1000000L;
				--tv.tv_sec;
			}
			if (tv.tv_sec < 0)
				timerclear(&tv);

			count = poll(pfd, 1, tv.tv_sec * 1000);

			if (count == -1) {
				if (errno != EINTR)
					perror("select");
				continue;
			} else if (count == 0) {
				logit(LOG_DEBUG, 0,
				    "Timed out receiving neighbor lists");
				if (++tries > retries)
					break;
				/* If we've tried ASK_NEIGHBORS2 twice with
				 * no response, fall back to ASK_NEIGHBORS
				 */
				if (tries == 2 && target_level == 0)
					trynew = 0;
				if (target_level == 0 && trynew == 0)
					ask(target_addr);
				else
					ask2(target_addr);
				gettimeofday(&et, 0);
				et.tv_sec += timeout;
				continue;
			}
			recvlen = recvfrom(igmp_socket, recv_buf, RECV_BUF_SIZE,
			    0, NULL, &dummy);
			if (recvlen <= 0) {
				if (recvlen && errno != EINTR)
					perror("recvfrom");
				continue;
			}

			if (recvlen < sizeof(struct ip)) {
				logit(LOG_WARNING, 0,
				    "packet too short (%u bytes) for IP header",
				    recvlen);
				continue;
			}
			ip = (struct ip *) recv_buf;
			if (ip->ip_p == 0)
				continue;	/* Request to install cache entry */
			src = ip->ip_src.s_addr;
			dst = ip->ip_dst.s_addr;
			iphdrlen = ip->ip_hl << 2;
			ipdatalen = ntohs(ip->ip_len) - iphdrlen;
			if (iphdrlen + ipdatalen != recvlen) {
				logit(LOG_WARNING, 0,
				    "packet shorter (%u bytes) than "
				    "hdr+data length (%u+%u)",
				    recvlen, iphdrlen, ipdatalen);
				continue;
			}
			igmp = (struct igmp *) (recv_buf + iphdrlen);
			group = igmp->igmp_group.s_addr;
			igmpdatalen = ipdatalen - IGMP_MINLEN;
			if (igmpdatalen < 0) {
				logit(LOG_WARNING, 0,
				    "IP data field too short (%u bytes) "
				    "for IGMP, from %s",
				    ipdatalen, inet_fmt(src, s1));
				continue;
			}
			if (igmp->igmp_type != IGMP_DVMRP)
				continue;

			switch (igmp->igmp_code) {
			case DVMRP_NEIGHBORS:
			case DVMRP_NEIGHBORS2:
				if (src != target_addr) {
					fprintf(stderr, "mrinfo: got reply from %s",
					    inet_fmt(src, s1));
					fprintf(stderr, " instead of %s\n",
					    inet_fmt(target_addr, s1));
					/*continue;*/
				}
				break;
			default:
				continue;	/* ignore all other DVMRP messages */
			}

			switch (igmp->igmp_code) {
			case DVMRP_NEIGHBORS:
				if (group) {
					/* knows about DVMRP_NEIGHBORS2 msg */
					if (target_level == 0) {
						target_level = ntohl(group);
						ask2(target_addr);
					}
				} else {
					accept_neighbors(src, dst, (u_char *)(igmp + 1),
					    igmpdatalen, ntohl(group));
					exit(0);
				}
				break;
			case DVMRP_NEIGHBORS2:
				accept_neighbors2(src, dst, (u_char *)(igmp + 1),
				    igmpdatalen, ntohl(group));
				exit(0);
			}
		}
	}
	exit(1);
}

/* dummies */
void
accept_probe(u_int32_t src, u_int32_t dst, char *p, int datalen,
    u_int32_t level)
{
}

void
accept_group_report(u_int32_t src, u_int32_t dst, u_int32_t group, int r_type)
{
}

void
accept_neighbor_request2(u_int32_t src, u_int32_t dst)
{
}

void
accept_report(u_int32_t src, u_int32_t dst, char *p, int datalen,
    u_int32_t level)
{
}

void
accept_neighbor_request(u_int32_t src, u_int32_t dst)
{
}

void
accept_prune(u_int32_t src, u_int32_t dst, char *p, int datalen)
{
}

void
accept_graft(u_int32_t src, u_int32_t dst, char *p, int datalen)
{
}

void
accept_g_ack(u_int32_t src, u_int32_t dst, char *p, int datalen)
{
}

void
add_table_entry(u_int32_t origin, u_int32_t mcastgrp)
{
}

void
check_vif_state(void)
{
}

void
accept_leave_message(u_int32_t src, u_int32_t dst, u_int32_t group)
{
}

void
accept_mtrace(u_int32_t src, u_int32_t dst, u_int32_t group, char *data,
    u_int no, int datalen)
{
}

void
accept_membership_query(u_int32_t src, u_int32_t dst, u_int32_t group, int tmo)
{
}

void
accept_info_request(u_int32_t src, u_int32_t dst, u_char *p, int datalen)
{
}

void
accept_info_reply(u_int32_t src, u_int32_t dst, u_char *p, int datalen)
{
}
