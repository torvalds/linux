/*	$OpenBSD: traceroute.c,v 1.170 2024/08/21 15:00:25 florian Exp $	*/
/*	$NetBSD: traceroute.c,v 1.10 1995/05/21 15:50:45 mycroft Exp $	*/

/*
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
 */

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Van Jacobson.
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
 */

/*
 * traceroute host  - trace the route ip packets follow going to "host".
 *
 * Attempt to trace the route an ip packet would follow to some
 * internet host.  We find out intermediate hops by launching probe
 * packets with a small ttl (time to live) then listening for an
 * icmp "time exceeded" reply from a gateway.  We start our probes
 * with a ttl of one and increase by one until we get an icmp "port
 * unreachable" (which means we got to "host") or hit a max (which
 * defaults to 64 hops & can be changed with the -m flag).  Three
 * probes (change with -q flag) are sent at each ttl setting and a
 * line is printed showing the ttl, address of the gateway and
 * round trip time of each probe.  If the probe answers come from
 * different gateways, the address of each responding system will
 * be printed.  If there is no response within a 5 sec. timeout
 * interval (changed with the -w flag), a "*" is printed for that
 * probe.
 *
 * Probe packets are UDP format.  We don't want the destination
 * host to process them so the destination port is set to an
 * unlikely value (if some clod on the destination is using that
 * value, it can be changed with the -p flag).
 *
 * A sample use might be:
 *
 *     [yak 71]% traceroute nis.nsf.net.
 *     traceroute to nis.nsf.net (35.1.1.48), 64 hops max, 56 byte packet
 *      1  helios.ee.lbl.gov (128.3.112.1)  19 ms  19 ms  0 ms
 *      2  lilac-dmc.Berkeley.EDU (128.32.216.1)  39 ms  39 ms  19 ms
 *      3  lilac-dmc.Berkeley.EDU (128.32.216.1)  39 ms  39 ms  19 ms
 *      4  ccngw-ner-cc.Berkeley.EDU (128.32.136.23)  39 ms  40 ms  39 ms
 *      5  ccn-nerif22.Berkeley.EDU (128.32.168.22)  39 ms  39 ms  39 ms
 *      6  128.32.197.4 (128.32.197.4)  40 ms  59 ms  59 ms
 *      7  131.119.2.5 (131.119.2.5)  59 ms  59 ms  59 ms
 *      8  129.140.70.13 (129.140.70.13)  99 ms  99 ms  80 ms
 *      9  129.140.71.6 (129.140.71.6)  139 ms  239 ms  319 ms
 *     10  129.140.81.7 (129.140.81.7)  220 ms  199 ms  199 ms
 *     11  nic.merit.edu (35.1.1.48)  239 ms  239 ms  239 ms
 *
 * Note that lines 2 & 3 are the same.  This is due to a buggy
 * kernel on the 2nd hop system -- lbl-csam.arpa -- that forwards
 * packets with a zero ttl.
 *
 * A more interesting example is:
 *
 *     [yak 72]% traceroute allspice.lcs.mit.edu.
 *     traceroute to allspice.lcs.mit.edu (18.26.0.115), 64 hops max
 *      1  helios.ee.lbl.gov (128.3.112.1)  0 ms  0 ms  0 ms
 *      2  lilac-dmc.Berkeley.EDU (128.32.216.1)  19 ms  19 ms  19 ms
 *      3  lilac-dmc.Berkeley.EDU (128.32.216.1)  39 ms  19 ms  19 ms
 *      4  ccngw-ner-cc.Berkeley.EDU (128.32.136.23)  19 ms  39 ms  39 ms
 *      5  ccn-nerif22.Berkeley.EDU (128.32.168.22)  20 ms  39 ms  39 ms
 *      6  128.32.197.4 (128.32.197.4)  59 ms  119 ms  39 ms
 *      7  131.119.2.5 (131.119.2.5)  59 ms  59 ms  39 ms
 *      8  129.140.70.13 (129.140.70.13)  80 ms  79 ms  99 ms
 *      9  129.140.71.6 (129.140.71.6)  139 ms  139 ms  159 ms
 *     10  129.140.81.7 (129.140.81.7)  199 ms  180 ms  300 ms
 *     11  129.140.72.17 (129.140.72.17)  300 ms  239 ms  239 ms
 *     12  * * *
 *     13  128.121.54.72 (128.121.54.72)  259 ms  499 ms  279 ms
 *     14  * * *
 *     15  * * *
 *     16  * * *
 *     17  * * *
 *     18  ALLSPICE.LCS.MIT.EDU (18.26.0.115)  339 ms  279 ms  279 ms
 *
 * (I start to see why I'm having so much trouble with mail to
 * MIT.)  Note that the gateways 12, 14, 15, 16 & 17 hops away
 * either don't send ICMP "time exceeded" messages or send them
 * with a ttl too small to reach us.  14 - 17 are running the
 * MIT C Gateway code that doesn't send "time exceeded"s.  God
 * only knows what's going on with 12.
 *
 * The silent gateway 12 in the above may be the result of a bug in
 * the 4.[23]BSD network code (and its derivatives):  4.x (x <= 3)
 * sends an unreachable message using whatever ttl remains in the
 * original datagram.  Since, for gateways, the remaining ttl is
 * zero, the icmp "time exceeded" is guaranteed to not make it back
 * to us.  The behavior of this bug is slightly more interesting
 * when it appears on the destination system:
 *
 *      1  helios.ee.lbl.gov (128.3.112.1)  0 ms  0 ms  0 ms
 *      2  lilac-dmc.Berkeley.EDU (128.32.216.1)  39 ms  19 ms  39 ms
 *      3  lilac-dmc.Berkeley.EDU (128.32.216.1)  19 ms  39 ms  19 ms
 *      4  ccngw-ner-cc.Berkeley.EDU (128.32.136.23)  39 ms  40 ms  19 ms
 *      5  ccn-nerif35.Berkeley.EDU (128.32.168.35)  39 ms  39 ms  39 ms
 *      6  csgw.Berkeley.EDU (128.32.133.254)  39 ms  59 ms  39 ms
 *      7  * * *
 *      8  * * *
 *      9  * * *
 *     10  * * *
 *     11  * * *
 *     12  * * *
 *     13  rip.Berkeley.EDU (128.32.131.22)  59 ms !  39 ms !  39 ms !
 *
 * Notice that there are 12 "gateways" (13 is the final
 * destination) and exactly the last half of them are "missing".
 * What's really happening is that rip (a Sun-3 running Sun OS3.5)
 * is using the ttl from our arriving datagram as the ttl in its
 * icmp reply.  So, the reply will time out on the return path
 * (with no notice sent to anyone since icmp's aren't sent for
 * icmp's) until we probe with a ttl that's at least twice the path
 * length.  I.e., rip is really only 7 hops away.  A reply that
 * returns with a ttl of 1 is a clue this problem exists.
 * Traceroute prints a "!" after the time if the ttl is <= 1.
 * Since vendors ship a lot of obsolete (DEC's Ultrix, Sun 3.x) or
 * non-standard (HPUX) software, expect to see this problem
 * frequently and/or take care picking the target host of your
 * probes.
 *
 * Other possible annotations after the time are !H, !N, !P (got a host,
 * network or protocol unreachable, respectively), !S or !F (source
 * route failed or fragmentation needed -- neither of these should
 * ever occur and the associated gateway is busted if you see one).  If
 * almost all the probes result in some kind of unreachable, traceroute
 * will give up and exit.
 *
 * Notes
 * -----
 * This program must be run by root or be setuid.  (I suggest that
 * you *don't* make it setuid -- casual use could result in a lot
 * of unnecessary traffic on our poor, congested nets.)
 *
 * This program requires a kernel mod that does not appear in any
 * system available from Berkeley:  A raw ip socket using proto
 * IPPROTO_RAW must interpret the data sent as an ip datagram (as
 * opposed to data to be wrapped in a ip datagram).  See the README
 * file that came with the source to this program for a description
 * of the mods I made to /sys/netinet/raw_ip.c.  Your mileage may
 * vary.  But, again, ANY 4.x (x < 4) BSD KERNEL WILL HAVE TO BE
 * MODIFIED TO RUN THIS PROGRAM.
 *
 * The udp port usage may appear bizarre (well, ok, it is bizarre).
 * The problem is that an icmp message only contains 8 bytes of
 * data from the original datagram.  8 bytes is the size of a udp
 * header so, if we want to associate replies with the original
 * datagram, the necessary information must be encoded into the
 * udp header (the ip id could be used but there's no way to
 * interlock with the kernel's assignment of ip id's and, anyway,
 * it would have taken a lot more kernel hacking to allow this
 * code to set the ip id).  So, to allow two or more users to
 * use traceroute simultaneously, we use this task's pid as the
 * source port (the high bit is set to move the port number out
 * of the "likely" range).  To keep track of which probe is being
 * replied to (so times and/or hop counts don't get confused by a
 * reply that was delayed in transit), we increment the destination
 * port number before each probe.
 *
 * Don't use this as a coding example.  I was trying to find a
 * routing problem and this code sort-of popped out after 48 hours
 * without sleep.  I was amazed it ever compiled, much less ran.
 *
 * I stole the idea for this program from Steve Deering.  Since
 * the first release, I've learned that had I attended the right
 * IETF working group meetings, I also could have stolen it from Guy
 * Almes or Matt Mathis.  I don't know (or care) who came up with
 * the idea first.  I envy the originators' perspicacity and I'm
 * glad they didn't keep the idea a secret.
 *
 * Tim Seaver, Ken Adelman and C. Philip Wood provided bug fixes and/or
 * enhancements to the original distribution.
 *
 * I've hacked up a round-trip-route version of this that works by
 * sending a loose-source-routed udp datagram through the destination
 * back to yourself.  Unfortunately, SO many gateways botch source
 * routing, the thing is almost worthless.  Maybe one day...
 *
 *  -- Van Jacobson (van@helios.ee.lbl.gov)
 *     Tue Dec 20 03:50:13 PST 1988
 */

#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <netinet/udp.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "traceroute.h"

int32_t	 sec_perturb;
int32_t	 usec_perturb;

u_char	 packet[512];
u_char	*outpacket;	/* last inbound (icmp) packet */

int	rcvsock;	/* receive (icmp) socket file descriptor */
int	sndsock;	/* send (udp) socket file descriptor */

int	rcvhlim;
struct in6_pktinfo *rcvpktinfo;

int	datalen;	/* How much data */

char	*hostname;

u_int16_t	srcport;

void	usage(void);

#define	TRACEROUTE_USER	"_traceroute"

void	sock_read(int, short, void *);
void	send_timer(int, short, void *);

struct tr_conf		*conf;	/* configuration defaults */
struct tr_result	*tr_results;
struct sockaddr_in	 from4, to4;
struct sockaddr_in6	 from6, to6;
struct sockaddr		*from, *to;
struct msghdr		 rcvmhdr;
struct event		 timer_ev;
int			 v6flag;
int			*waiting_ttls;
int			 last_tos = 0;

int
main(int argc, char *argv[])
{
	int	mib[4] = { CTL_NET, PF_INET, IPPROTO_IP, IPCTL_DEFTTL };
	char	hbuf[NI_MAXHOST];

	struct addrinfo		 hints, *res;
	struct ip		*ip = NULL;
	struct iovec		 rcviov[2];
	static u_char		*rcvcmsgbuf;
	struct passwd		*pw;
	struct event		 sock_ev;
	struct timeval		tv = {0, 0};

	long		 l;
	socklen_t	 len;
	size_t		 size;

	int		 ch;
	int		 on = 1;
	int		 error;
	int		 headerlen;	/* How long packet's header is */
	int		 i;
	int		 packetlen;
	int		 rcvcmsglen;
	int		 rcvsock4, rcvsock6;
	int		 sndsock4, sndsock6;
	u_int32_t	 tmprnd;
	int		 v4sock_errno, v6sock_errno;

	char		*dest;
	const char	*errstr;

	uid_t		 ouid, uid;
	gid_t		 gid;

	/* Cannot pledge due to special setsockopt()s below */
	if (unveil("/", "r") == -1)
		err(1, "unveil /");
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

	if ((conf = calloc(1, sizeof(*conf))) == NULL)
		err(1,NULL);

	conf->first_ttl = 1;
	conf->proto = IPPROTO_UDP;
	conf->max_ttl = IPDEFTTL;
	conf->nprobes = 3;
	conf->expected_responses = 2; /* icmp + DNS */

	/* start udp dest port # for probe packets */
	conf->port = 32768+666;

	memset(&rcvmhdr, 0, sizeof(rcvmhdr));
	memset(&rcviov, 0, sizeof(rcviov));

	rcvsock4 = rcvsock6 = sndsock4 = sndsock6 = -1;
	v4sock_errno = v6sock_errno = 0;

	conf->waittime = 3 * 1000;

	if ((rcvsock6 = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6)) == -1)
		v6sock_errno = errno;
	else if ((sndsock6 = socket(AF_INET6, SOCK_DGRAM, 0)) == -1)
		v6sock_errno = errno;

	if ((rcvsock4 = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) == -1)
		v4sock_errno = errno;
	else if ((sndsock4 = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) == -1)
		v4sock_errno = errno;

	/* revoke privs */
	ouid = getuid();
	if (ouid == 0 && (pw = getpwnam(TRACEROUTE_USER)) != NULL) {
		uid = pw->pw_uid;
		gid = pw->pw_gid;
	} else {
		uid = getuid();
		gid = getgid();
	}
	if (ouid && (setgroups(1, &gid) ||
	    setresgid(gid, gid, gid) ||
	    setresuid(uid, uid, uid)))
		err(1, "unable to revoke privs");

	if (strcmp("traceroute6", __progname) == 0) {
		v6flag = 1;
		if (v6sock_errno != 0)
			errc(5, v6sock_errno, rcvsock6 < 0 ? "socket(ICMPv6)" :
			    "socket(SOCK_DGRAM)");
		rcvsock = rcvsock6;
		sndsock = sndsock6;
		if (rcvsock4 >= 0)
			close(rcvsock4);
		if (sndsock4 >= 0)
			close(sndsock4);
	} else {
		if (v4sock_errno != 0)
			errc(5, v4sock_errno, rcvsock4 < 0 ? "icmp socket" :
			    "raw socket");
		rcvsock = rcvsock4;
		sndsock = sndsock4;
		if (rcvsock6 >= 0)
			close(rcvsock6);
		if (sndsock6 >= 0)
			close(sndsock6);
	}

	if (v6flag) {
		mib[1] = PF_INET6;
		mib[2] = IPPROTO_IPV6;
		mib[3] = IPV6CTL_DEFHLIM;
		/* specify to tell receiving interface */
		if (setsockopt(rcvsock, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on,
		    sizeof(on)) == -1)
			err(1, "setsockopt(IPV6_RECVPKTINFO)");

		/* specify to tell hoplimit field of received IP6 hdr */
		if (setsockopt(rcvsock, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, &on,
		    sizeof(on)) == -1)
			err(1, "setsockopt(IPV6_RECVHOPLIMIT)");
	}

	size = sizeof(i);
	if (sysctl(mib, sizeof(mib)/sizeof(mib[0]), &i, &size, NULL, 0) == -1)
		err(1, "sysctl");
	conf->max_ttl = i;

	while ((ch = getopt(argc, argv, v6flag ? "ADdf:Ilm:np:q:Ss:t:w:vV:" :
	    "ADdf:g:Ilm:nP:p:q:Ss:t:V:vw:x")) != -1)
		switch (ch) {
		case 'A':
			conf->Aflag = 1;
			conf->expected_responses++;
			break;
		case 'd':
			conf->dflag = 1;
			break;
		case 'D':
			conf->dump = 1;
			break;
		case 'f':
			conf->first_ttl = strtonum(optarg, 1, conf->max_ttl,
			    &errstr);
			if (errstr)
				errx(1, "min ttl must be 1 to %u.",
				    conf->max_ttl);
			break;
		case 'g':
			if (conf->lsrr >= MAX_LSRR)
				errx(1, "too many gateways; max %d", MAX_LSRR);
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_INET;

			if (getaddrinfo(optarg, NULL, &hints, &res) != 0)
				errx(1, "unknown host %s", optarg);

			conf->gateway[conf->lsrr] =
			    ((struct sockaddr_in *)res->ai_addr)->sin_addr;
			freeaddrinfo(res);

			if (++conf->lsrr == 1)
				conf->lsrrlen = 4;
			conf->lsrrlen += 4;
			break;
		case 'I':
			if (conf->protoset)
				errx(1, "protocol already set with -P");
			conf->protoset = 1;
			conf->proto = IPPROTO_ICMP;
			break;
		case 'l':
			conf->ttl_flag = 1;
			break;
		case 'm':
			conf->max_ttl = strtonum(optarg, conf->first_ttl,
			    MAXTTL, &errstr);
			if (errstr)
				errx(1, "max ttl must be %u to %u.",
				    conf->first_ttl, MAXTTL);
			break;
		case 'n':
			conf->nflag = 1;
			conf->expected_responses--;
			break;
		case 'p':
			conf->port = strtonum(optarg, 1, 65535, &errstr);
			if (errstr)
				errx(1, "port must be >0, <65536.");
			break;
		case 'P':
			if (conf->protoset)
				errx(1, "protocol already set with -I");
			conf->protoset = 1;
			conf->proto = strtonum(optarg, 1, IPPROTO_MAX - 1,
			    &errstr);
			if (errstr) {
				struct protoent *pent;

				pent = getprotobyname(optarg);
				if (pent)
					conf->proto = pent->p_proto;
				else
					errx(1, "proto must be >=1, or a "
					    "name.");
			}
			break;
		case 'q':
			conf->nprobes = strtonum(optarg, 1, 1024, &errstr);
			if (errstr)
				errx(1, "nprobes must be >0.");
			break;
		case 's':
			/*
			 * set the ip source address of the outbound
			 * probe (e.g., on a multi-homed host).
			 */
			conf->source = optarg;
			break;
		case 'S':
			conf->sump = 1;
			break;
		case 't':
			if (!map_tos(optarg, &conf->tos)) {
				if (strlen(optarg) > 1 && optarg[0] == '0' &&
				    optarg[1] == 'x') {
					char *ep;
					errno = 0;
					ep = NULL;
					l = strtol(optarg, &ep, 16);
					if (errno || !*optarg || *ep ||
					    l < 0 || l > 255)
						errx(1, "illegal tos value %s",
						    optarg);
					conf->tos = (int)l;
				} else {
					conf->tos = strtonum(optarg, 0, 255,
					    &errstr);
					if (errstr)
						errx(1, "illegal tos value %s",
						    optarg);
				}
			}
			conf->tflag = 1;
			last_tos = conf->tos;
			break;
		case 'v':
			conf->verbose = 1;
			break;
		case 'V':
			conf->rtableid = (unsigned int)strtonum(optarg, 0,
			    RT_TABLEID_MAX, &errstr);
			if (errstr)
				errx(1, "rtable value is %s: %s",
				    errstr, optarg);
			if (setsockopt(sndsock, SOL_SOCKET, SO_RTABLE,
			    &conf->rtableid, sizeof(conf->rtableid)) == -1)
				err(1, "setsockopt SO_RTABLE");
			if (setsockopt(rcvsock, SOL_SOCKET, SO_RTABLE,
			    &conf->rtableid, sizeof(conf->rtableid)) == -1)
				err(1, "setsockopt SO_RTABLE");
			break;
		case 'w':
			conf->waittime = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "wait must be >=1 sec.");
			conf->waittime *= 1000;
			break;
		case 'x':
			conf->xflag = 1;
			break;
		default:
			usage();
		}

	if (ouid == 0 && (setgroups(1, &gid) ||
	    setresgid(gid, gid, gid) ||
	    setresuid(uid, uid, uid)))
		err(1, "unable to revoke privs");

	argc -= optind;
	argv += optind;

	if (argc < 1 || argc > 2)
		usage();

	tr_results = calloc(sizeof(struct tr_result), conf->max_ttl *
	    conf->nprobes);
	if (tr_results == NULL)
		err(1, NULL);

	waiting_ttls = calloc(sizeof(int), conf->max_ttl);
	for (i = 0; i < conf->max_ttl; i++)
		waiting_ttls[i] = conf->nprobes * conf->expected_responses;

	setvbuf(stdout, NULL, _IOLBF, 0);

	conf->ident = (getpid() & 0xffff) | 0x8000;
	tmprnd = arc4random();
	sec_perturb = (tmprnd & 0x80000000) ? -(tmprnd & 0x7ff) :
	    (tmprnd & 0x7ff);
	usec_perturb = arc4random();

	memset(&to4, 0, sizeof(to4));
	memset(&to6, 0, sizeof(to6));

	dest = *argv;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = v6flag ? PF_INET6 : PF_INET;
	hints.ai_socktype = SOCK_RAW;
	hints.ai_protocol = 0;
	hints.ai_flags = AI_CANONNAME;
	if ((error = getaddrinfo(dest, NULL, &hints, &res)))
		errx(1, "%s", gai_strerror(error));

	switch (res->ai_family) {
	case AF_INET:
		to = (struct sockaddr *)&to4;
		from = (struct sockaddr *)&from4;
		break;
	case AF_INET6:
		to = (struct sockaddr *)&to6;
		from = (struct sockaddr *)&from6;
		break;
	default:
		errx(1, "unsupported AF: %d", res->ai_family);
		break;
	}

	memcpy(to, res->ai_addr, res->ai_addrlen);

	if (!hostname) {
		hostname = res->ai_canonname ? strdup(res->ai_canonname) : dest;
		if (!hostname)
			errx(1, "malloc");
	}

	if (res->ai_next) {
		if (getnameinfo(res->ai_addr, res->ai_addrlen, hbuf,
		    sizeof(hbuf), NULL, 0, NI_NUMERICHOST) != 0)
			strlcpy(hbuf, "?", sizeof(hbuf));
		warnx("Warning: %s has multiple "
		    "addresses; using %s", hostname, hbuf);
	}
	freeaddrinfo(res);

	if (*++argv) {
		datalen = strtonum(*argv, 0, INT_MAX, &errstr);
		if (errstr)
			errx(1, "datalen out of range");
	}

	switch (to->sa_family) {
	case AF_INET:
		switch (conf->proto) {
		case IPPROTO_UDP:
			headerlen = (sizeof(struct ip) + conf->lsrrlen +
			    sizeof(struct udphdr) + sizeof(struct packetdata));
			break;
		case IPPROTO_ICMP:
			headerlen = (sizeof(struct ip) + conf->lsrrlen +
			    sizeof(struct icmp) + sizeof(struct packetdata));
			break;
		default:
			headerlen = (sizeof(struct ip) + conf->lsrrlen +
			    sizeof(struct packetdata));
		}

		if (datalen < 0 || datalen > IP_MAXPACKET - headerlen)
			errx(1, "packet size must be 0 to %d.",
			    IP_MAXPACKET - headerlen);

		datalen += headerlen;

		if ((outpacket = calloc(1, datalen)) == NULL)
			err(1, "calloc");

		rcviov[0].iov_base = (caddr_t)packet;
		rcviov[0].iov_len = sizeof(packet);
		rcvmhdr.msg_name = (caddr_t)&from4;
		rcvmhdr.msg_namelen = sizeof(from4);
		rcvmhdr.msg_iov = rcviov;
		rcvmhdr.msg_iovlen = 1;
		rcvmhdr.msg_control = NULL;
		rcvmhdr.msg_controllen = 0;

		ip = (struct ip *)outpacket;
		if (conf->lsrr != 0) {
			u_char *p = (u_char *)(ip + 1);

			*p++ = IPOPT_NOP;
			*p++ = IPOPT_LSRR;
			*p++ = conf->lsrrlen - 1;
			*p++ = IPOPT_MINOFF;
			conf->gateway[conf->lsrr] = to4.sin_addr;
			for (i = 1; i <= conf->lsrr; i++) {
				memcpy(p, &conf->gateway[i],
				    sizeof(struct in_addr));
				p += sizeof(struct in_addr);
			}
			ip->ip_dst = conf->gateway[0];
		} else
			ip->ip_dst = to4.sin_addr;
		ip->ip_off = htons(0);
		ip->ip_hl = (sizeof(struct ip) + conf->lsrrlen) >> 2;
		ip->ip_p = conf->proto;
		ip->ip_v = IPVERSION;
		ip->ip_tos = conf->tos;

		if (setsockopt(sndsock, IPPROTO_IP, IP_HDRINCL,
		    &on, sizeof(on)) == -1)
			err(6, "IP_HDRINCL");

		if (conf->source) {
			memset(&from4, 0, sizeof(from4));
			from4.sin_family = AF_INET;
			if (inet_pton(AF_INET, conf->source, &from4.sin_addr)
			    != 1)
				errx(1, "unknown host %s", conf->source);
			ip->ip_src = from4.sin_addr;
			if (ouid != 0 &&
			    (ntohl(from4.sin_addr.s_addr) & 0xff000000U) ==
			    0x7f000000U && (ntohl(to4.sin_addr.s_addr) &
			    0xff000000U) != 0x7f000000U)
				errx(1, "source is on 127/8, destination is"
				    " not");
			if (ouid && bind(sndsock, (struct sockaddr *)&from4,
			    sizeof(from4)) == -1)
				err(1, "bind");
		}
		packetlen = datalen;
		break;
	case AF_INET6:
		/*
		 * packetlen is the size of the complete IP packet sent and
		 * reported in the first line of output.
		 * For IPv4 this is equal to datalen since we are constructing
		 * a raw packet.
		 * For IPv6 we need to always add the size of the IP6 header
		 * and for UDP packets the size of the UDP header since they
		 * are prepended to the packet by the kernel
		 */
		packetlen = sizeof(struct ip6_hdr);
		switch (conf->proto) {
		case IPPROTO_UDP:
			headerlen = sizeof(struct packetdata);
			packetlen += sizeof(struct udphdr);
			break;
		case IPPROTO_ICMP:
			headerlen = sizeof(struct icmp6_hdr) +
			    sizeof(struct packetdata);
			break;
		default:
			errx(1, "Unsupported proto: %hhu", conf->proto);
			break;
		}

		if (datalen < 0 || datalen > IP_MAXPACKET - headerlen)
			errx(1, "packet size must be 0 to %d.",
			    IP_MAXPACKET - headerlen);

		datalen += headerlen;
		packetlen += datalen;

		if ((outpacket = calloc(1, datalen)) == NULL)
			err(1, "calloc");

		/* initialize msghdr for receiving packets */
		rcviov[0].iov_base = (caddr_t)packet;
		rcviov[0].iov_len = sizeof(packet);
		rcvmhdr.msg_name = (caddr_t)&from6;
		rcvmhdr.msg_namelen = sizeof(from6);
		rcvmhdr.msg_iov = rcviov;
		rcvmhdr.msg_iovlen = 1;
		rcvcmsglen = CMSG_SPACE(sizeof(struct in6_pktinfo)) +
		    CMSG_SPACE(sizeof(int));

		if ((rcvcmsgbuf = malloc(rcvcmsglen)) == NULL)
			errx(1, "malloc");
		rcvmhdr.msg_control = (caddr_t) rcvcmsgbuf;
		rcvmhdr.msg_controllen = rcvcmsglen;

		/*
		 * Send UDP or ICMP
		 */
		if (conf->proto == IPPROTO_ICMP) {
			close(sndsock);
			sndsock = rcvsock;
		}

		/*
		 * Source selection
		 */
		memset(&from6, 0, sizeof(from6));
		if (conf->source) {
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_INET6;
			hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
			hints.ai_flags = AI_NUMERICHOST;
			if ((error = getaddrinfo(conf->source, "0", &hints,
			    &res)))
				errx(1, "%s: %s", conf->source,
				    gai_strerror(error));
			memcpy(&from6, res->ai_addr, res->ai_addrlen);
			freeaddrinfo(res);
		} else {
			struct sockaddr_in6 nxt;
			int dummy;

			nxt = to6;
			nxt.sin6_port = htons(DUMMY_PORT);
			if ((dummy = socket(AF_INET6, SOCK_DGRAM, 0)) == -1)
				err(1, "socket");
			if (conf->rtableid > 0 &&
			    setsockopt(dummy, SOL_SOCKET, SO_RTABLE,
			    &conf->rtableid, sizeof(conf->rtableid)) == -1)
				err(1, "setsockopt(SO_RTABLE)");
			if (connect(dummy, (struct sockaddr *)&nxt,
			    nxt.sin6_len) == -1)
				err(1, "connect");
			len = sizeof(from6);
			if (getsockname(dummy, (struct sockaddr *)&from6,
			    &len) == -1)
				err(1, "getsockname");
			close(dummy);
		}

		from6.sin6_port = htons(0);
		if (bind(sndsock, (struct sockaddr *)&from6, from6.sin6_len) == -1)
			err(1, "bind sndsock");

		if (conf->tflag) {
			if (setsockopt(sndsock, IPPROTO_IPV6, IPV6_TCLASS,
			    &conf->tos, sizeof(conf->tos)) == -1)
				err(6, "IPV6_TCLASS");
		}

		len = sizeof(from6);
		if (getsockname(sndsock, (struct sockaddr *)&from6, &len) == -1)
			err(1, "getsockname");
		srcport = ntohs(from6.sin6_port);
		break;
	default:
		errx(1, "unsupported AF: %d", to->sa_family);
		break;
	}

	if (conf->dflag) {
		(void) setsockopt(rcvsock, SOL_SOCKET, SO_DEBUG,
		    &on, sizeof(on));
		(void) setsockopt(sndsock, SOL_SOCKET, SO_DEBUG,
		    &on, sizeof(on));
	}

	if (setsockopt(sndsock, SOL_SOCKET, SO_SNDBUF,
	    &datalen, sizeof(datalen)) == -1)
		err(6, "SO_SNDBUF");

	if (conf->nflag && !conf->Aflag) {
		if (pledge("stdio inet", NULL) == -1)
			err(1, "pledge");
	} else {
		if (pledge("stdio inet dns", NULL) == -1)
			err(1, "pledge");
	}

	if (getnameinfo(to, to->sa_len, hbuf,
	    sizeof(hbuf), NULL, 0, NI_NUMERICHOST))
		strlcpy(hbuf, "(invalid)", sizeof(hbuf));
	fprintf(stderr, "%s to %s (%s)", __progname, hostname, hbuf);
	if (conf->source)
		fprintf(stderr, " from %s", conf->source);
	fprintf(stderr, ", %u hops max, %d byte packets\n", conf->max_ttl,
	    packetlen);
	(void) fflush(stderr);

	if (conf->first_ttl > 1)
		printf("Skipping %u intermediate hops\n", conf->first_ttl - 1);

	event_init();

	event_set(&sock_ev, rcvsock, EV_READ | EV_PERSIST, sock_read, NULL);
	event_add(&sock_ev, NULL);
	evtimer_set(&timer_ev, send_timer, &timer_ev);
	evtimer_add(&timer_ev, &tv);
	event_dispatch();
}

void
usage(void)
{
	if (v6flag) {
		fprintf(stderr, "usage: %s "
		    "[-ADdIlnSv] [-f first_hop] [-m max_hop] [-p port]\n"
		    "\t[-q nqueries] [-s sourceaddr] [-t toskeyword] [-V rtable] "
		    "[-w waittime]\n\thost [datalen]\n", __progname);
	} else {
		fprintf(stderr,
		    "usage: %s [-ADdIlnSvx] [-f first_ttl] [-g gateway_addr] "
		    "[-m max_ttl]\n"
		    "\t[-P proto] [-p port] [-q nqueries] [-s sourceaddr]\n"
		    "\t[-t toskeyword] "
		    "[-V rtable] [-w waittime] host [datalen]\n",
		    __progname);
	}
	exit(1);
}

void
sock_read(int fd, short events, void *arg)
{
	struct ip	*ip;
	struct timeval	 t2, tv = {0, 0};
	int		 pkg_ok, cc, recv_seq, recv_seq_row;
	char		 hbuf[NI_MAXHOST];

	cc = recvmsg(rcvsock, &rcvmhdr, 0);

	if (cc == 0)
		return;

	evtimer_add(&timer_ev, &tv);

	gettime(&t2);

	pkg_ok = packet_ok(conf, to->sa_family, &rcvmhdr, cc, &recv_seq);

	/* Skip wrong packet */
	if (pkg_ok == 0)
		goto out;

	/* skip corrupt sequence number */
	if (recv_seq < 0 || recv_seq >= conf->max_ttl * conf->nprobes)
		goto out;

	recv_seq_row = recv_seq / conf->nprobes;

	/* skipping dup */
	if (tr_results[recv_seq].dup++)
		goto out;

	switch (to->sa_family) {
	case AF_INET:
		ip = (struct ip *)packet;

		print(conf, from, cc - (ip->ip_hl << 2), inet_ntop(AF_INET,
		    &ip->ip_dst, hbuf, sizeof(hbuf)), &tr_results[recv_seq]);
		break;
	case AF_INET6:
		print(conf, from, cc, rcvpktinfo ? inet_ntop(AF_INET6,
		    &rcvpktinfo->ipi6_addr, hbuf, sizeof(hbuf)) : "?",
		    &tr_results[recv_seq]);
		break;
	default:
		errx(1, "unsupported AF: %d", to->sa_family);
	}

	tr_results[recv_seq].t2 = t2;
	tr_results[recv_seq].resp_ttl = v6flag ? rcvhlim : ip->ip_ttl;

	waiting_ttls[recv_seq_row]--;

	if (pkg_ok == -2) {
		if ((v6flag && rcvhlim <= 1) ||
		    (!v6flag && ip->ip_ttl <=1))
			snprintf(tr_results[recv_seq].icmp_code,
			    sizeof(tr_results[recv_seq].icmp_code), "%s", " !");
		tr_results[recv_seq].got_there++;
	} else {
		if (to->sa_family == AF_INET && conf->tflag)
			check_tos(ip, &last_tos, &tr_results[recv_seq]);
		if (pkg_ok != -1) {
			icmp_code(to->sa_family, pkg_ok - 1,
			    &tr_results[recv_seq].got_there,
			    &tr_results[recv_seq].unreachable,
			    &tr_results[recv_seq]);
		}
	}

	if (cc && ((recv_seq + 1) % conf->nprobes) == 0 &&
	    (conf->xflag || conf->verbose))
		print_exthdr(packet, cc, &tr_results[recv_seq]);
 out:
	catchup_result_rows(tr_results, conf);
}

void
send_timer(int fd, short events, void *arg)
{
	static int	 seq;
	struct timeval	 tv = {0, 30000}, t1;
	struct event	*ev = arg;
	int		 ttl;

	evtimer_add(ev, &tv);

	ttl = conf->first_ttl + seq / conf->nprobes;
	if (ttl <= conf->max_ttl) {
		gettime(&t1);
		tr_results[seq].seq = seq;
		tr_results[seq].row = seq / conf->nprobes;
		tr_results[seq].ttl = ttl;
		tr_results[seq].t1 = t1;
		send_probe(conf, seq, ttl, to);
		seq++;
	}

	catchup_result_rows(tr_results, conf);

}
