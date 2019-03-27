/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	Regents of the University of California.  All rights reserved.
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
 *	@(#)netstat.h	8.2 (Berkeley) 1/4/94
 * $FreeBSD$
 */

#include <sys/cdefs.h>

#define	satosin(sa)	((struct sockaddr_in *)(sa))
#define	satosin6(sa)	((struct sockaddr_in6 *)(sa))
#define	sin6tosa(sin6)	((struct sockaddr *)(sin6))

extern int	Aflag;	/* show addresses of protocol control block */
extern int	aflag;	/* show all sockets (including servers) */
extern int	bflag;	/* show i/f total bytes in/out */
extern int	dflag;	/* show i/f dropped packets */
extern int	gflag;	/* show group (multicast) routing or stats */
extern int	hflag;	/* show counters in human readable format */
extern int	iflag;	/* show interfaces */
extern int	Lflag;	/* show size of listen queues */
extern int	mflag;	/* show memory stats */
extern int	noutputs;	/* how much outputs before we exit */
extern int	numeric_addr;	/* show addresses numerically */
extern int	numeric_port;	/* show ports numerically */
extern int	Pflag;	/* show TCP log ID */
extern int	rflag;	/* show routing tables (or routing stats) */
extern int	Rflag;	/* show flowid / RSS information */
extern int	sflag;	/* show protocol statistics */
extern int	Tflag;  /* show TCP control block info */
extern int	Wflag;	/* wide display */
extern int	xflag;	/* extended display, includes all socket buffer info */
extern int	zflag;	/* zero stats */

extern int	interval; /* repeat interval for i/f stats */

extern char	*interface; /* desired i/f for stats, or NULL for all i/fs */
extern int	unit;	/* unit number for above */

extern int	live;	/* true if we are examining a live system */

typedef	int kreadfn_t(u_long, void *, size_t);
int	fetch_stats(const char *, u_long, void *, size_t, kreadfn_t);
int	fetch_stats_ro(const char *, u_long, void *, size_t, kreadfn_t);

int	kread(u_long addr, void *buf, size_t size);
uint64_t kread_counter(u_long addr);
int	kread_counters(u_long addr, void *buf, size_t size);
void	kset_dpcpu(u_int);
const char *plural(uintmax_t);
const char *plurales(uintmax_t);
const char *pluralies(uintmax_t);

struct sockaddr;
struct socket;
struct xsocket;
int	sotoxsocket(struct socket *, struct xsocket *);
void	protopr(u_long, const char *, int, int);
void	tcp_stats(u_long, const char *, int, int);
void	udp_stats(u_long, const char *, int, int);
#ifdef SCTP
void	sctp_protopr(u_long, const char *, int, int);
void	sctp_stats(u_long, const char *, int, int);
#endif
void	arp_stats(u_long, const char *, int, int);
void	ip_stats(u_long, const char *, int, int);
void	icmp_stats(u_long, const char *, int, int);
void	igmp_stats(u_long, const char *, int, int);
void	pim_stats(u_long, const char *, int, int);
void	carp_stats(u_long, const char *, int, int);
void	pfsync_stats(u_long, const char *, int, int);
#ifdef IPSEC
void	ipsec_stats(u_long, const char *, int, int);
void	esp_stats(u_long, const char *, int, int);
void	ah_stats(u_long, const char *, int, int);
void	ipcomp_stats(u_long, const char *, int, int);
#endif

#ifdef INET
struct in_addr;

char	*inetname(struct in_addr *);
#endif

#ifdef INET6
struct in6_addr;

char	*inet6name(struct in6_addr *);
void	ip6_stats(u_long, const char *, int, int);
void	ip6_ifstats(char *);
void	icmp6_stats(u_long, const char *, int, int);
void	icmp6_ifstats(char *);
void	pim6_stats(u_long, const char *, int, int);
void	rip6_stats(u_long, const char *, int, int);
void	mroute6pr(void);
void	mrt6_stats(void);

struct sockaddr_in6;
struct in6_addr;
void in6_fillscopeid(struct sockaddr_in6 *);
void	inet6print(const char *, struct in6_addr *, int, const char *, int);
#endif /*INET6*/

#ifdef IPSEC
void	pfkey_stats(u_long, const char *, int, int);
#endif

void	mbpr(void *, u_long);

void	netisr_stats(void);

void	hostpr(u_long, u_long);
void	impstats(u_long, u_long);

void	intpr(void (*)(char *), int);

void	pr_family(int);
void	rt_stats(void);

char	*routename(struct sockaddr *, int);
const char *netname(struct sockaddr *, struct sockaddr *);
void	routepr(int, int);

#ifdef NETGRAPH
void	netgraphprotopr(u_long, const char *, int, int);
#endif

void	unixpr(u_long, u_long, u_long, u_long, u_long, bool *);

void	mroutepr(void);
void	mrt_stats(void);
void	bpf_stats(char *);
