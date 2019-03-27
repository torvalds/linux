/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)defs.h	8.1 (Berkeley) 6/5/93
 *
 * $FreeBSD$
 */

#ifdef  sgi
#ident "$FreeBSD$"
#endif

/* Definitions for RIPv2 routing process.
 *
 * This code is based on the 4.4BSD `routed` daemon, with extensions to
 * support:
 *	RIPv2, including variable length subnet masks.
 *	Router Discovery
 *	aggregate routes in the kernel tables.
 *	aggregate advertised routes.
 *	maintain spare routes for faster selection of another gateway
 *		when the current gateway dies.
 *	timers on routes with second granularity so that selection
 *		of a new route does not wait 30-60 seconds.
 *	tolerance of static routes.
 *	tell the kernel hop counts
 *	do not advertise if ipforwarding=0
 *
 * The vestigial support for other protocols has been removed.  There
 * is no likelihood that IETF RIPv1 or RIPv2 will ever be used with
 * other protocols.  The result is far smaller, faster, cleaner, and
 * perhaps understandable.
 *
 * The accumulation of special flags and kludges added over the many
 * years have been simplified and integrated.
 */

#include <assert.h>
#include <stdio.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#ifdef sgi
#include <strings.h>
#include <bstring.h>
#endif
#include <stdarg.h>
#include <syslog.h>
#include <time.h>
#include <sys/cdefs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/queue.h>
#ifdef sgi
#define _USER_ROUTE_TREE
#include <net/radix.h>
#else
#include "radix.h"
#define UNUSED __attribute__((unused))
#define PATTRIB(f,l) __attribute__((format (printf,f,l)))
#endif
#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define RIPVERSION RIPv2
#include <protocols/routed.h>

#ifndef __RCSID
#define __RCSID(_s) static const char rcsid[] UNUSED = _s
#endif
#ifndef __COPYRIGHT
#define __COPYRIGHT(_s) static const char copyright[] UNUSED = _s
#endif

/* Type of an IP address.
 *	Some systems do not like to pass structures, so do not use in_addr.
 *	Some systems think a long has 64 bits, which would be a gross waste.
 * So define it here so it can be changed for the target system.
 * It should be defined somewhere netinet/in.h, but it is not.
 */
#ifdef sgi
#define naddr u_int32_t
#elif defined (__NetBSD__)
#define naddr u_int32_t
#define _HAVE_SA_LEN
#define _HAVE_SIN_LEN
#else
#define naddr u_long
#define _HAVE_SA_LEN
#define _HAVE_SIN_LEN
#endif

#define DAY (24*60*60)
#define NEVER DAY			/* a long time */
#define EPOCH NEVER			/* bias time by this to avoid <0 */

/* Scan the kernel regularly to see if any interfaces have appeared or been
 * turned off.  These must be less than STALE_TIME.
 */
#define	CHECK_BAD_INTERVAL	5	/* when an interface is known bad */
#define	CHECK_ACT_INTERVAL	30	/* when advertising */
#define	CHECK_QUIET_INTERVAL	300	/* when not */

#define LIM_SEC(s,l) ((s).tv_sec = MIN((s).tv_sec, (l)))

/* Metric used for fake default routes.  It ought to be 15, but when
 * processing advertised routes, previous versions of `routed` added
 * to the received metric and discarded the route if the total was 16
 * or larger.
 */
#define FAKE_METRIC (HOPCNT_INFINITY-2)


/* Router Discovery parameters */
#ifndef sgi
#define INADDR_ALLROUTERS_GROUP		0xe0000002  /* 224.0.0.2 */
#endif
#define	MaxMaxAdvertiseInterval		1800
#define	MinMaxAdvertiseInterval		4
#define	DefMaxAdvertiseInterval		600
#define MIN_PreferenceLevel		0x80000000

#define	MAX_INITIAL_ADVERT_INTERVAL	16
#define	MAX_INITIAL_ADVERTS		3

#define	MAX_SOLICITATION_DELAY		1
#define	SOLICITATION_INTERVAL		3
#define	MAX_SOLICITATIONS		3


/* Bloated packet size for systems that simply add authentication to
 * full-sized packets
 */
#define OVER_MAXPACKETSIZE (MAXPACKETSIZE+sizeof(struct netinfo)*2)
/* typical packet buffers */
union pkt_buf {
	char	packet[OVER_MAXPACKETSIZE*2];
	struct	rip rip;
};

#define GNAME_LEN   64			/* assumed=64 in parms.c */
/* bigger than IFNAMSIZ, with room for "external()" or "remote()" */
#define IF_NAME_LEN (GNAME_LEN+15)

/* No more routes than this, to protect ourself in case something goes
 * whacko and starts broadcasting zillions of bogus routes.
 */
#define MAX_ROUTES  (128*1024)
extern int total_routes;

/* Main, daemon routing table structure
 */
struct rt_entry {
	struct	radix_node rt_nodes[2];	/* radix tree glue */
	u_int	rt_state;
#	    define RS_IF	0x001	/* for network interface */
#	    define RS_NET_INT	0x002	/* authority route */
#	    define RS_NET_SYN	0x004	/* fake net route for subnet */
#	    define RS_NO_NET_SYN (RS_LOCAL | RS_LOCAL | RS_IF)
#	    define RS_SUBNET	0x008	/* subnet route from any source */
#	    define RS_LOCAL	0x010	/* loopback for pt-to-pt */
#	    define RS_MHOME	0x020	/* from -m */
#	    define RS_STATIC	0x040	/* from the kernel */
#	    define RS_RDISC     0x080	/* from router discovery */
	struct sockaddr_in rt_dst_sock;
	naddr   rt_mask;
	struct rt_spare {
	    struct interface *rts_ifp;
	    naddr   rts_gate;		/* forward packets here */
	    naddr   rts_router;		/* on the authority of this router */
	    char    rts_metric;
	    u_short rts_tag;
	    time_t  rts_time;		/* timer to junk stale routes */
	    u_int   rts_de_ag;		/* de-aggregation level */
#define NUM_SPARES 4
	} rt_spares[NUM_SPARES];
	u_int	rt_seqno;		/* when last changed */
	char	rt_poison_metric;	/* to notice maximum recently */
	time_t	rt_poison_time;		/*	advertised metric */
};
#define rt_dst	    rt_dst_sock.sin_addr.s_addr
#define rt_ifp	    rt_spares[0].rts_ifp
#define rt_gate	    rt_spares[0].rts_gate
#define rt_router   rt_spares[0].rts_router
#define rt_metric   rt_spares[0].rts_metric
#define rt_tag	    rt_spares[0].rts_tag
#define rt_time	    rt_spares[0].rts_time
#define rt_de_ag    rt_spares[0].rts_de_ag

#define HOST_MASK	0xffffffff
#define RT_ISHOST(rt)	((rt)->rt_mask == HOST_MASK)

/* age all routes that
 *	are not from -g, -m, or static routes from the kernel
 *	not unbroken interface routes
 *		but not broken interfaces
 *	nor non-passive, remote interfaces that are not aliases
 *		(i.e. remote & metric=0)
 */
#define AGE_RT(rt_state,ifp) (0 == ((rt_state) & (RS_MHOME | RS_STATIC	    \
						  | RS_NET_SYN | RS_RDISC)) \
			      && (!((rt_state) & RS_IF)			    \
				  || (ifp) == 0				    \
				  || (((ifp)->int_state & IS_REMOTE)	    \
				      && !((ifp)->int_state & IS_PASSIVE))))

/* true if A is better than B
 * Better if
 *	- A is not a poisoned route
 *	- and A is not stale
 *	- and A has a shorter path
 *		- or is the router speaking for itself
 *		- or the current route is equal but stale
 *		- or it is a host route advertised by a system for itself
 */
#define BETTER_LINK(rt,A,B) ((A)->rts_metric < HOPCNT_INFINITY		\
			     && now_stale <= (A)->rts_time		\
			     && ((A)->rts_metric < (B)->rts_metric	\
				 || ((A)->rts_gate == (A)->rts_router	\
				     && (B)->rts_gate != (B)->rts_router) \
				 || ((A)->rts_metric == (B)->rts_metric	\
				     && now_stale > (B)->rts_time)	\
				 || (RT_ISHOST(rt)			\
				     && (rt)->rt_dst == (A)->rts_router	\
				     && (A)->rts_metric == (B)->rts_metric)))


/* An "interface" is similar to a kernel ifnet structure, except it also
 * handles "logical" or "IS_REMOTE" interfaces (remote gateways).
 */
struct interface {
	LIST_ENTRY(interface)		int_list;
	LIST_ENTRY(interface)		remote_list;
	struct interface *int_ahash, **int_ahash_prev;
	struct interface *int_bhash, **int_bhash_prev;
	struct interface *int_nhash, **int_nhash_prev;
	char	int_name[IF_NAME_LEN+1];
	u_short	int_index;
	naddr	int_addr;		/* address on this host (net order) */
	naddr	int_brdaddr;		/* broadcast address (n) */
	naddr	int_dstaddr;		/* other end of pt-to-pt link (n) */
	naddr	int_net;		/* working network # (host order)*/
	naddr	int_mask;		/* working net mask (host order) */
	naddr	int_ripv1_mask;		/* for inferring a mask (n) */
	naddr	int_std_addr;		/* class A/B/C address (n) */
	naddr	int_std_net;		/* class A/B/C network (h) */
	naddr	int_std_mask;		/* class A/B/C netmask (h) */
	int	int_rip_sock;		/* for queries */
	int	int_if_flags;		/* some bits copied from kernel */
	u_int	int_state;
	time_t	int_act_time;		/* last thought healthy */
	time_t	int_query_time;
	u_short	int_transitions;	/* times gone up-down */
	char	int_metric;
	u_char	int_d_metric;		/* for faked default route */
	u_char	int_adj_inmetric;	/* adjust advertised metrics */
	u_char	int_adj_outmetric;	/*    instead of interface metric */
	struct int_data {
		u_int	ipackets;	/* previous network stats */
		u_int	ierrors;
		u_int	opackets;
		u_int	oerrors;
#ifdef sgi
		u_int	odrops;
#endif
		time_t	ts;		/* timestamp on network stats */
	} int_data;
#	define MAX_AUTH_KEYS 5
	struct auth {			/* authentication info */
	    u_int16_t type;
	    u_char  key[RIP_AUTH_PW_LEN];
	    u_char  keyid;
	    time_t  start, end;
	} int_auth[MAX_AUTH_KEYS];
	/* router discovery parameters */
	int	int_rdisc_pref;		/* signed preference to advertise */
	int	int_rdisc_int;		/* MaxAdvertiseInterval */
	int	int_rdisc_cnt;
	struct timeval int_rdisc_timer;
};

/* bits in int_state */
#define IS_ALIAS	    0x0000001	/* interface alias */
#define IS_SUBNET	    0x0000002	/* interface on subnetted network */
#define	IS_REMOTE	    0x0000004	/* interface is not on this machine */
#define	IS_PASSIVE	    0x0000008	/* remote and does not do RIP */
#define IS_EXTERNAL	    0x0000010	/* handled by EGP or something */
#define IS_CHECKED	    0x0000020	/* still exists */
#define IS_ALL_HOSTS	    0x0000040	/* in INADDR_ALLHOSTS_GROUP */
#define IS_ALL_ROUTERS	    0x0000080	/* in INADDR_ALLROUTERS_GROUP */
#define IS_DISTRUST	    0x0000100	/* ignore untrusted routers */
#define IS_REDIRECT_OK	    0x0000200	/* accept ICMP redirects */
#define IS_BROKE	    0x0000400	/* seems to be broken */
#define IS_SICK		    0x0000800	/* seems to be broken */
#define IS_DUP		    0x0001000	/* has a duplicate address */
#define IS_NEED_NET_SYN	    0x0002000	/* need RS_NET_SYN route */
#define IS_NO_AG	    0x0004000	/* do not aggregate subnets */
#define IS_NO_SUPER_AG	    0x0008000	/* do not aggregate networks */
#define IS_NO_RIPV1_IN	    0x0010000	/* no RIPv1 input at all */
#define IS_NO_RIPV2_IN	    0x0020000	/* no RIPv2 input at all */
#define IS_NO_RIP_IN	(IS_NO_RIPV1_IN | IS_NO_RIPV2_IN)
#define IS_RIP_IN_OFF(s) (((s) & IS_NO_RIP_IN) == IS_NO_RIP_IN)
#define IS_NO_RIPV1_OUT	    0x0040000	/* no RIPv1 output at all */
#define IS_NO_RIPV2_OUT	    0x0080000	/* no RIPv2 output at all */
#define IS_NO_RIP_OUT	(IS_NO_RIPV1_OUT | IS_NO_RIPV2_OUT)
#define IS_NO_RIP	(IS_NO_RIP_OUT | IS_NO_RIP_IN)
#define IS_RIP_OUT_OFF(s) (((s) & IS_NO_RIP_OUT) == IS_NO_RIP_OUT)
#define IS_RIP_OFF(s)	(((s) & IS_NO_RIP) == IS_NO_RIP)
#define	IS_NO_RIP_MCAST	    0x0100000	/* broadcast RIPv2 */
#define IS_NO_ADV_IN	    0x0200000	/* do not listen to advertisements */
#define IS_NO_SOL_OUT	    0x0400000	/* send no solicitations */
#define IS_SOL_OUT	    0x0800000	/* send solicitations */
#define GROUP_IS_SOL_OUT (IS_SOL_OUT | IS_NO_SOL_OUT)
#define IS_NO_ADV_OUT	    0x1000000	/* do not advertise rdisc */
#define IS_ADV_OUT	    0x2000000	/* advertise rdisc */
#define GROUP_IS_ADV_OUT (IS_NO_ADV_OUT | IS_ADV_OUT)
#define IS_BCAST_RDISC	    0x4000000	/* broadcast instead of multicast */
#define IS_NO_RDISC	(IS_NO_ADV_IN | IS_NO_SOL_OUT | IS_NO_ADV_OUT)
#define IS_PM_RDISC	    0x8000000	/* poor-man's router discovery */

#define iff_up(f) ((f) & IFF_UP)

LIST_HEAD(ifhead, interface);

/* Information for aggregating routes */
#define NUM_AG_SLOTS	32
struct ag_info {
	struct ag_info *ag_fine;	/* slot with finer netmask */
	struct ag_info *ag_cors;	/* more coarse netmask */
	naddr	ag_dst_h;		/* destination in host byte order */
	naddr	ag_mask;
	naddr	ag_gate;
	naddr	ag_nhop;
	char	ag_metric;		/* metric to be advertised */
	char	ag_pref;		/* aggregate based on this */
	u_int	ag_seqno;
	u_short	ag_tag;
	u_short	ag_state;
#define	    AGS_SUPPRESS    0x001	/* combine with coarser mask */
#define	    AGS_AGGREGATE   0x002	/* synthesize combined routes */
#define	    AGS_REDUN0	    0x004	/* redundant, finer routes output */
#define	    AGS_REDUN1	    0x008
#define	    AG_IS_REDUN(state) (((state) & (AGS_REDUN0 | AGS_REDUN1)) \
				== (AGS_REDUN0 | AGS_REDUN1))
#define	    AGS_GATEWAY	    0x010	/* tell kernel RTF_GATEWAY */
#define	    AGS_IF	    0x020	/* for an interface */
#define	    AGS_RIPV2	    0x040	/* send only as RIPv2 */
#define	    AGS_FINE_GATE   0x080	/* ignore differing ag_gate when this
					 * has the finer netmask */
#define	    AGS_CORS_GATE   0x100	/* ignore differing gate when this
					 * has the coarser netmasks */
#define	    AGS_SPLIT_HZ    0x200	/* suppress for split horizon */

	/* some bits are set if they are set on either route */
#define	    AGS_AGGREGATE_EITHER (AGS_RIPV2 | AGS_GATEWAY |   \
				  AGS_SUPPRESS | AGS_CORS_GATE)
};


/* parameters for interfaces */
struct parm {
	struct parm *parm_next;
	char	parm_name[IF_NAME_LEN+1];
	naddr	parm_net;
	naddr	parm_mask;

	u_char	parm_d_metric;
	u_char	parm_adj_inmetric;
	char	parm_adj_outmetric;
	u_int	parm_int_state;
	int	parm_rdisc_pref;	/* signed IRDP preference */
	int	parm_rdisc_int;		/* IRDP advertising interval */
	struct auth parm_auth[MAX_AUTH_KEYS];
};

/* authority for internal networks */
extern struct intnet {
	struct intnet *intnet_next;
	naddr	intnet_addr;		/* network byte order */
	naddr	intnet_mask;
	char	intnet_metric;
} *intnets;

/* defined RIPv1 netmasks */
extern struct r1net {
	struct r1net *r1net_next;
	naddr	r1net_net;		/* host order */
	naddr	r1net_match;
	naddr	r1net_mask;
} *r1nets;

/* trusted routers */
extern struct tgate {
	struct tgate *tgate_next;
	naddr	tgate_addr;
#define	    MAX_TGATE_NETS 32
	struct tgate_net {
	    naddr   net;		/* host order */
	    naddr   mask;
	} tgate_nets[MAX_TGATE_NETS];
} *tgates;

enum output_type {OUT_QUERY, OUT_UNICAST, OUT_BROADCAST, OUT_MULTICAST,
	NO_OUT_MULTICAST, NO_OUT_RIPV2};

/* common output buffers */
extern struct ws_buf {
	struct rip	*buf;
	struct netinfo	*n;
	struct netinfo	*base;
	struct netinfo	*lim;
	enum output_type type;
} v12buf;

extern pid_t	mypid;
extern naddr	myaddr;			/* main address of this system */

extern int	stopint;		/* !=0 to stop */

extern int	rip_sock;		/* RIP socket */
extern const struct interface *rip_sock_mcast; /* current multicast interface */
extern int	rt_sock;		/* routing socket */
extern int	rt_sock_seqno;
extern int	rdisc_sock;		/* router-discovery raw socket */

extern int	supplier;		/* process should supply updates */
extern int	supplier_set;		/* -s or -q requested */
extern int	ridhosts;		/* 1=reduce host routes */
extern int	mhome;			/* 1=want multi-homed host route */
extern int	advertise_mhome;	/* 1=must continue advertising it */
extern int	auth_ok;		/* 1=ignore auth if we do not care */
extern int	insecure;		/* Reply to special queries or not */

extern struct timeval clk;		/* system clock's idea of time */
extern struct timeval epoch;		/* system clock when started */
extern struct timeval now;		/* current idea of time */
extern time_t	now_stale;
extern time_t	now_expire;
extern time_t	now_garbage;

extern struct timeval age_timer;	/* next check of old routes */
extern struct timeval no_flash;		/* inhibit flash update until then */
extern struct timeval rdisc_timer;	/* next advert. or solicitation */
extern int rdisc_ok;			/* using solicited route */

extern struct timeval ifinit_timer;	/* time to check interfaces */

extern naddr	loopaddr;		/* our address on loopback */
extern int	tot_interfaces;		/* # of remote and local interfaces */
extern int	rip_interfaces;		/* # of interfaces doing RIP */
extern struct ifhead ifnet;		/* all interfaces */
extern struct ifhead remote_if;		/* remote interfaces */
extern int	have_ripv1_out;		/* have a RIPv1 interface */
extern int	need_flash;		/* flash update needed */
extern struct timeval need_kern;	/* need to update kernel table */
extern u_int	update_seqno;		/* a route has changed */

extern int	tracelevel, new_tracelevel;
#define MAX_TRACELEVEL 4
#define TRACEKERNEL (tracelevel >= 4)	/* log kernel changes */
#define	TRACECONTENTS (tracelevel >= 3)	/* display packet contents */
#define TRACEPACKETS (tracelevel >= 2)	/* note packets */
#define	TRACEACTIONS (tracelevel != 0)
extern FILE	*ftrace;		/* output trace file */
extern char inittracename[PATH_MAX];

extern struct radix_node_head *rhead;


#ifdef sgi
/* Fix conflicts */
#define	dup2(x,y)		BSDdup2(x,y)
#endif /* sgi */

void fix_sock(int, const char *);
void fix_select(void);
void rip_off(void);
void rip_on(struct interface *);

void bufinit(void);
int  output(enum output_type, struct sockaddr_in *,
		   struct interface *, struct rip *, int);
void clr_ws_buf(struct ws_buf *, struct auth *);
void rip_query(void);
void rip_bcast(int);
void supply(struct sockaddr_in *, struct interface *,
		   enum output_type, int, int, int);

void	msglog(const char *, ...) PATTRIB(1,2);
struct msg_limit {
    time_t	reuse;
    struct msg_sub {
	naddr	addr;
	time_t	until;
#   define MSG_SUBJECT_N 8
    } subs[MSG_SUBJECT_N];
};
void	msglim(struct msg_limit *, naddr,
		       const char *, ...) PATTRIB(3,4);
#define	LOGERR(msg) msglog(msg ": %s", strerror(errno))
void	logbad(int, const char *, ...) PATTRIB(2,3);
#define	BADERR(dump,msg) logbad(dump,msg ": %s", strerror(errno))
#ifdef DEBUG
#define	DBGERR(dump,msg) BADERR(dump,msg)
#else
#define	DBGERR(dump,msg) LOGERR(msg)
#endif
char	*naddr_ntoa(naddr);
const char *saddr_ntoa(struct sockaddr *);

void	*rtmalloc(size_t, const char *);
void	timevaladd(struct timeval *, struct timeval *);
void	intvl_random(struct timeval *, u_long, u_long);
int	getnet(char *, naddr *, naddr *);
int	gethost(char *, naddr *);
void	gwkludge(void);
const char *parse_parms(char *, int);
const char *check_parms(struct parm *);
void	get_parms(struct interface *);

void	lastlog(void);
void	trace_close(int);
void	set_tracefile(const char *, const char *, int);
void	tracelevel_msg(const char *, int);
void	trace_off(const char*, ...) PATTRIB(1,2);
void	set_tracelevel(void);
void	trace_flush(void);
void	trace_misc(const char *, ...) PATTRIB(1,2);
void	trace_act(const char *, ...) PATTRIB(1,2);
void	trace_pkt(const char *, ...) PATTRIB(1,2);
void	trace_add_del(const char *, struct rt_entry *);
void	trace_change(struct rt_entry *, u_int, struct rt_spare *,
			     const char *);
void	trace_if(const char *, struct interface *);
void	trace_upslot(struct rt_entry *, struct rt_spare *,
			     struct rt_spare *);
void	trace_rip(const char*, const char*, struct sockaddr_in *,
			  struct interface *, struct rip *, int);
char	*addrname(naddr, naddr, int);
char	*rtname(naddr, naddr, naddr);

void	rdisc_age(naddr);
void	set_rdisc_mg(struct interface *, int);
void	set_supplier(void);
void	if_bad_rdisc(struct interface *);
void	if_ok_rdisc(struct interface *);
void	read_rip(int, struct interface *);
void	read_rt(void);
void	read_d(void);
void	rdisc_adv(void);
void	rdisc_sol(void);

void	sigtrace_on(int);
void	sigtrace_off(int);

void	flush_kern(void);
void	age(naddr);

void	ag_flush(naddr, naddr, void (*)(struct ag_info *));
void	ag_check(naddr, naddr, naddr, naddr, char, char, u_int,
			 u_short, u_short, void (*)(struct ag_info *));
void	del_static(naddr, naddr, naddr, int);
void	del_redirects(naddr, time_t);
struct rt_entry *rtget(naddr, naddr);
struct rt_entry *rtfind(naddr);
void	rtinit(void);
void	rtadd(naddr, naddr, u_int, struct rt_spare *);
void	rtchange(struct rt_entry *, u_int, struct rt_spare *, char *);
void	rtdelete(struct rt_entry *);
void	rts_delete(struct rt_entry *, struct rt_spare *);
void	rtbad_sub(struct rt_entry *);
void	rtswitch(struct rt_entry *, struct rt_spare *);

#define S_ADDR(x)	(((struct sockaddr_in *)(x))->sin_addr.s_addr)
#define INFO_DST(I)	((I)->rti_info[RTAX_DST])
#define INFO_GATE(I)	((I)->rti_info[RTAX_GATEWAY])
#define INFO_MASK(I)	((I)->rti_info[RTAX_NETMASK])
#define INFO_IFA(I)	((I)->rti_info[RTAX_IFA])
#define INFO_AUTHOR(I)	((I)->rti_info[RTAX_AUTHOR])
#define INFO_BRD(I)	((I)->rti_info[RTAX_BRD])
void rt_xaddrs(struct rt_addrinfo *, struct sockaddr *, struct sockaddr *,
	       int);

naddr	std_mask(naddr);
naddr	ripv1_mask_net(naddr, struct interface *);
naddr	ripv1_mask_host(naddr,struct interface *);
#define		on_net(a,net,mask) (((ntohl(a) ^ (net)) & (mask)) == 0)
int	check_dst(naddr);
struct interface *check_dup(naddr, naddr, naddr, int);
int	check_remote(struct interface *);
void	ifinit(void);
int	walk_bad(struct radix_node *, struct walkarg *);
int	if_ok(struct interface *, const char *);
void	if_sick(struct interface *);
void	if_link(struct interface *);
struct interface *ifwithaddr(naddr addr, int bcast, int remote);
struct interface *ifwithindex(u_short, int);
struct interface *iflookup(naddr);

struct auth *find_auth(struct interface *);
void end_md5_auth(struct ws_buf *, struct auth *);

#if defined(__FreeBSD__) || defined(__NetBSD__)
#include <md5.h>
#else
#define MD5_DIGEST_LEN 16
typedef struct {
	u_int32_t state[4];		/* state (ABCD) */
	u_int32_t count[2];		/* # of bits, modulo 2^64 (LSB 1st) */
	unsigned char buffer[64];	/* input buffer */
} MD5_CTX;
void MD5Init(MD5_CTX*);
void MD5Update(MD5_CTX*, u_char*, u_int);
void MD5Final(u_char[MD5_DIGEST_LEN], MD5_CTX*);
#endif
