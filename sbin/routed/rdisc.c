/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995
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
 * $FreeBSD$
 */

#include "defs.h"
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#ifdef __NetBSD__
__RCSID("$NetBSD$");
#elif defined(__FreeBSD__)
__RCSID("$FreeBSD$");
#else
__RCSID("$Revision: 2.27 $");
#ident "$Revision: 2.27 $"
#endif

/* router advertisement ICMP packet */
struct icmp_ad {
	u_int8_t    icmp_type;		/* type of message */
	u_int8_t    icmp_code;		/* type sub code */
	u_int16_t   icmp_cksum;		/* ones complement cksum of struct */
	u_int8_t    icmp_ad_num;	/* # of following router addresses */
	u_int8_t    icmp_ad_asize;	/* 2--words in each advertisement */
	u_int16_t   icmp_ad_life;	/* seconds of validity */
	struct icmp_ad_info {
	    n_long  icmp_ad_addr;
	    n_long  icmp_ad_pref;
	} icmp_ad_info[1];
};

/* router solicitation ICMP packet */
struct icmp_so {
	u_int8_t    icmp_type;		/* type of message */
	u_int8_t    icmp_code;		/* type sub code */
	u_int16_t   icmp_cksum;		/* ones complement cksum of struct */
	n_long	    icmp_so_rsvd;
};

union ad_u {
	struct icmp icmp;
	struct icmp_ad ad;
	struct icmp_so so;
};


int	rdisc_sock = -1;		/* router-discovery raw socket */
static const struct interface *rdisc_sock_mcast; /* current multicast interface */

struct timeval rdisc_timer;
int rdisc_ok;				/* using solicited route */


#define MAX_ADS 16			/* at least one per interface */
struct dr {				/* accumulated advertisements */
    struct interface *dr_ifp;
    naddr   dr_gate;			/* gateway */
    time_t  dr_ts;			/* when received */
    time_t  dr_life;			/* lifetime in host byte order */
    n_long  dr_recv_pref;		/* received but biased preference */
    n_long  dr_pref;			/* preference adjusted by metric */
};
static const struct dr *cur_drp;
static struct dr drs[MAX_ADS];

/* convert between signed, balanced around zero,
 * and unsigned zero-based preferences */
#define SIGN_PREF(p) ((p) ^ MIN_PreferenceLevel)
#define UNSIGN_PREF(p) SIGN_PREF(p)
/* adjust unsigned preference by interface metric,
 * without driving it to infinity */
#define PREF(p, ifp) ((int)(p) <= ((ifp)->int_metric+(ifp)->int_adj_outmetric)\
		      ? ((p) != 0 ? 1 : 0)				    \
		      : (p) - ((ifp)->int_metric+(ifp)->int_adj_outmetric))

static void rdisc_sort(void);


/* dump an ICMP Router Discovery Advertisement Message
 */
static void
trace_rdisc(const char	*act,
	    naddr	from,
	    naddr	to,
	    struct interface *ifp,
	    union ad_u	*p,
	    u_int	len)
{
	int i;
	n_long *wp, *lim;


	if (!TRACEPACKETS || ftrace == NULL)
		return;

	lastlog();

	if (p->icmp.icmp_type == ICMP_ROUTERADVERT) {
		(void)fprintf(ftrace, "%s Router Ad"
			      " from %s to %s via %s life=%d\n",
			      act, naddr_ntoa(from), naddr_ntoa(to),
			      ifp ? ifp->int_name : "?",
			      ntohs(p->ad.icmp_ad_life));
		if (!TRACECONTENTS)
			return;

		wp = &p->ad.icmp_ad_info[0].icmp_ad_addr;
		lim = &wp[(len - sizeof(p->ad)) / sizeof(*wp)];
		for (i = 0; i < p->ad.icmp_ad_num && wp <= lim; i++) {
			(void)fprintf(ftrace, "\t%s preference=%d",
				      naddr_ntoa(wp[0]), (int)ntohl(wp[1]));
			wp += p->ad.icmp_ad_asize;
		}
		(void)fputc('\n',ftrace);

	} else {
		trace_act("%s Router Solic. from %s to %s via %s value=%#x",
			  act, naddr_ntoa(from), naddr_ntoa(to),
			  ifp ? ifp->int_name : "?",
			  (int)ntohl(p->so.icmp_so_rsvd));
	}
}

/* prepare Router Discovery socket.
 */
static void
get_rdisc_sock(void)
{
	if (rdisc_sock < 0) {
		rdisc_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
		if (rdisc_sock < 0)
			BADERR(1,"rdisc_sock = socket()");
		fix_sock(rdisc_sock,"rdisc_sock");
		fix_select();
	}
}


/* Pick multicast group for router-discovery socket
 */
void
set_rdisc_mg(struct interface *ifp,
	     int on)			/* 0=turn it off */
{
	struct group_req gr;
	struct sockaddr_in *sin;

	assert(ifp != NULL);

	if (rdisc_sock < 0) {
		/* Create the raw socket so that we can hear at least
		 * broadcast router discovery packets.
		 */
		if ((ifp->int_state & IS_NO_RDISC) == IS_NO_RDISC
		    || !on)
			return;
		get_rdisc_sock();
	}

	if (!(ifp->int_if_flags & IFF_MULTICAST)) {
		ifp->int_state &= ~(IS_ALL_HOSTS | IS_ALL_ROUTERS);
		return;
	}

	memset(&gr, 0, sizeof(gr));
	gr.gr_interface = ifp->int_index;
	sin = (struct sockaddr_in *)&gr.gr_group;
	sin->sin_family = AF_INET;
#ifdef _HAVE_SIN_LEN
	sin->sin_len = sizeof(struct sockaddr_in);
#endif

	if (supplier
	    || (ifp->int_state & IS_NO_ADV_IN)
	    || !on) {
		/* stop listening to advertisements
		 */
		if (ifp->int_state & IS_ALL_HOSTS) {
			sin->sin_addr.s_addr = htonl(INADDR_ALLHOSTS_GROUP);
			if (setsockopt(rdisc_sock, IPPROTO_IP,
				       MCAST_LEAVE_GROUP,
				       &gr, sizeof(gr)) < 0)
				LOGERR("MCAST_LEAVE_GROUP ALLHOSTS");
			ifp->int_state &= ~IS_ALL_HOSTS;
		}

	} else if (!(ifp->int_state & IS_ALL_HOSTS)) {
		/* start listening to advertisements
		 */
		sin->sin_addr.s_addr = htonl(INADDR_ALLHOSTS_GROUP);
		if (setsockopt(rdisc_sock, IPPROTO_IP, MCAST_JOIN_GROUP,
			       &gr, sizeof(gr)) < 0) {
			LOGERR("MCAST_JOIN_GROUP ALLHOSTS");
		} else {
			ifp->int_state |= IS_ALL_HOSTS;
		}
	}

	if (!supplier
	    || (ifp->int_state & IS_NO_ADV_OUT)
	    || !on) {
		/* stop listening to solicitations
		 */
		if (ifp->int_state & IS_ALL_ROUTERS) {
			sin->sin_addr.s_addr = htonl(INADDR_ALLROUTERS_GROUP);
			if (setsockopt(rdisc_sock, IPPROTO_IP,
				       MCAST_LEAVE_GROUP,
				       &gr, sizeof(gr)) < 0)
				LOGERR("MCAST_LEAVE_GROUP ALLROUTERS");
			ifp->int_state &= ~IS_ALL_ROUTERS;
		}

	} else if (!(ifp->int_state & IS_ALL_ROUTERS)) {
		/* start hearing solicitations
		 */
		sin->sin_addr.s_addr = htonl(INADDR_ALLROUTERS_GROUP);
		if (setsockopt(rdisc_sock, IPPROTO_IP, MCAST_JOIN_GROUP,
			       &gr, sizeof(gr)) < 0) {
			LOGERR("MCAST_JOIN_GROUP ALLROUTERS");
		} else {
			ifp->int_state |= IS_ALL_ROUTERS;
		}
	}
}


/* start supplying routes
 */
void
set_supplier(void)
{
	struct interface *ifp;
	struct dr *drp;

	if (supplier_set)
		return;

	trace_act("start supplying routes");

	/* Forget discovered routes.
	 */
	for (drp = drs; drp < &drs[MAX_ADS]; drp++) {
		drp->dr_recv_pref = 0;
		drp->dr_life = 0;
	}
	rdisc_age(0);

	supplier_set = 1;
	supplier = 1;

	/* Do not start advertising until we have heard some RIP routes */
	LIM_SEC(rdisc_timer, now.tv_sec+MIN_WAITTIME);

	/* Switch router discovery multicast groups from soliciting
	 * to advertising.
	 */
	LIST_FOREACH(ifp, &ifnet, int_list) {
		if (ifp->int_state & IS_BROKE)
			continue;
		ifp->int_rdisc_cnt = 0;
		ifp->int_rdisc_timer.tv_usec = rdisc_timer.tv_usec;
		ifp->int_rdisc_timer.tv_sec = now.tv_sec+MIN_WAITTIME;
		set_rdisc_mg(ifp, 1);
	}

	/* get rid of any redirects */
	del_redirects(0,0);
}


/* age discovered routes and find the best one
 */
void
rdisc_age(naddr bad_gate)
{
	time_t sec;
	struct dr *drp;


	/* If only advertising, then do only that. */
	if (supplier) {
		/* If switching from client to server, get rid of old
		 * default routes.
		 */
		if (cur_drp != NULL)
			rdisc_sort();
		rdisc_adv();
		return;
	}

	/* If we are being told about a bad router,
	 * then age the discovered default route, and if there is
	 * no alternative, solicit a replacement.
	 */
	if (bad_gate != 0) {
		/* Look for the bad discovered default route.
		 * Age it and note its interface.
		 */
		for (drp = drs; drp < &drs[MAX_ADS]; drp++) {
			if (drp->dr_ts == 0)
				continue;

			/* When we find the bad router, then age the route
			 * to at most SUPPLY_INTERVAL.
			 * This is contrary to RFC 1256, but defends against
			 * black holes.
			 */
			if (drp->dr_gate == bad_gate) {
				sec = (now.tv_sec - drp->dr_life
				       + SUPPLY_INTERVAL);
				if (drp->dr_ts > sec) {
					trace_act("age 0.0.0.0 --> %s via %s",
						  naddr_ntoa(drp->dr_gate),
						  drp->dr_ifp->int_name);
					drp->dr_ts = sec;
				}
				break;
			}
		}
	}

	rdisc_sol();
	rdisc_sort();

	/* Delete old redirected routes to keep the kernel table small,
	 * and to prevent black holes.  Check that the kernel table
	 * matches the daemon table (i.e. has the default route).
	 * But only if RIP is not running and we are not dealing with
	 * a bad gateway, since otherwise age() will be called.
	 */
	if (rip_sock < 0 && bad_gate == 0)
		age(0);
}


/* Zap all routes discovered via an interface that has gone bad
 *	This should only be called when !(ifp->int_state & IS_ALIAS)
 */
void
if_bad_rdisc(struct interface *ifp)
{
	struct dr *drp;

	for (drp = drs; drp < &drs[MAX_ADS]; drp++) {
		if (drp->dr_ifp != ifp)
			continue;
		drp->dr_recv_pref = 0;
		drp->dr_ts = 0;
		drp->dr_life = 0;
	}

	/* make a note to re-solicit, turn RIP on or off, etc. */
	rdisc_timer.tv_sec = 0;
}


/* mark an interface ok for router discovering.
 */
void
if_ok_rdisc(struct interface *ifp)
{
	set_rdisc_mg(ifp, 1);

	ifp->int_rdisc_cnt = 0;
	ifp->int_rdisc_timer.tv_sec = now.tv_sec + (supplier
						    ? MIN_WAITTIME
						    : MAX_SOLICITATION_DELAY);
	if (timercmp(&rdisc_timer, &ifp->int_rdisc_timer, >))
		rdisc_timer = ifp->int_rdisc_timer;
}


/* get rid of a dead discovered router
 */
static void
del_rdisc(struct dr *drp)
{
	struct interface *ifp;
	naddr gate;
	int i;


	del_redirects(gate = drp->dr_gate, 0);
	drp->dr_ts = 0;
	drp->dr_life = 0;


	/* Count the other discovered routes on the interface.
	 */
	i = 0;
	ifp = drp->dr_ifp;
	for (drp = drs; drp < &drs[MAX_ADS]; drp++) {
		if (drp->dr_ts != 0
		    && drp->dr_ifp == ifp)
			i++;
	}

	/* If that was the last good discovered router on the interface,
	 * then solicit a new one.
	 * This is contrary to RFC 1256, but defends against black holes.
	 */
	if (i != 0) {
		trace_act("discovered router %s via %s"
			  " is bad--have %d remaining",
			  naddr_ntoa(gate), ifp->int_name, i);
	} else if (ifp->int_rdisc_cnt >= MAX_SOLICITATIONS) {
		trace_act("last discovered router %s via %s"
			  " is bad--re-solicit",
			  naddr_ntoa(gate), ifp->int_name);
		ifp->int_rdisc_cnt = 0;
		ifp->int_rdisc_timer.tv_sec = 0;
		rdisc_sol();
	} else {
		trace_act("last discovered router %s via %s"
			  " is bad--wait to solicit",
			  naddr_ntoa(gate), ifp->int_name);
	}
}


/* Find the best discovered route,
 * and discard stale routers.
 */
static void
rdisc_sort(void)
{
	struct dr *drp, *new_drp;
	struct rt_entry *rt;
	struct rt_spare new;
	struct interface *ifp;
	u_int new_st = 0;
	n_long new_pref = 0;


	/* Find the best discovered route.
	 */
	new_drp = NULL;
	for (drp = drs; drp < &drs[MAX_ADS]; drp++) {
		if (drp->dr_ts == 0)
			continue;
		ifp = drp->dr_ifp;

		/* Get rid of expired discovered routers.
		 */
		if (drp->dr_ts + drp->dr_life <= now.tv_sec) {
			del_rdisc(drp);
			continue;
		}

		LIM_SEC(rdisc_timer, drp->dr_ts+drp->dr_life+1);

		/* Update preference with possibly changed interface
		 * metric.
		 */
		drp->dr_pref = PREF(drp->dr_recv_pref, ifp);

		/* Prefer the current route to prevent thrashing.
		 * Prefer shorter lifetimes to speed the detection of
		 * bad routers.
		 * Avoid sick interfaces.
		 */
		if (new_drp == NULL
		    || (!((new_st ^ drp->dr_ifp->int_state) & IS_SICK)
			&& (new_pref < drp->dr_pref
			    || (new_pref == drp->dr_pref
				&& (drp == cur_drp
				    || (new_drp != cur_drp
					&& new_drp->dr_life > drp->dr_life)))))
		    || ((new_st & IS_SICK)
			&& !(drp->dr_ifp->int_state & IS_SICK))) {
			    new_drp = drp;
			    new_st = drp->dr_ifp->int_state;
			    new_pref = drp->dr_pref;
		}
	}

	/* switch to a better default route
	 */
	if (new_drp != cur_drp) {
		rt = rtget(RIP_DEFAULT, 0);

		/* Stop using discovered routes if they are all bad
		 */
		if (new_drp == NULL) {
			trace_act("turn off Router Discovery client");
			rdisc_ok = 0;

			if (rt != NULL
			    && (rt->rt_state & RS_RDISC)) {
				new = rt->rt_spares[0];
				new.rts_metric = HOPCNT_INFINITY;
				new.rts_time = now.tv_sec - GARBAGE_TIME;
				rtchange(rt, rt->rt_state & ~RS_RDISC,
					 &new, 0);
				rtswitch(rt, 0);
			}

		} else {
			if (cur_drp == NULL) {
				trace_act("turn on Router Discovery client"
					  " using %s via %s",
					  naddr_ntoa(new_drp->dr_gate),
					  new_drp->dr_ifp->int_name);
				rdisc_ok = 1;

			} else {
				trace_act("switch Router Discovery from"
					  " %s via %s to %s via %s",
					  naddr_ntoa(cur_drp->dr_gate),
					  cur_drp->dr_ifp->int_name,
					  naddr_ntoa(new_drp->dr_gate),
					  new_drp->dr_ifp->int_name);
			}

			memset(&new, 0, sizeof(new));
			new.rts_ifp = new_drp->dr_ifp;
			new.rts_gate = new_drp->dr_gate;
			new.rts_router = new_drp->dr_gate;
			new.rts_metric = HOPCNT_INFINITY-1;
			new.rts_time = now.tv_sec;
			if (rt != NULL) {
				rtchange(rt, rt->rt_state | RS_RDISC, &new, 0);
			} else {
				rtadd(RIP_DEFAULT, 0, RS_RDISC, &new);
			}
		}

		cur_drp = new_drp;
	}

	/* turn RIP on or off */
	if (!rdisc_ok || rip_interfaces > 1) {
		rip_on(0);
	} else {
		rip_off();
	}
}


/* handle a single address in an advertisement
 */
static void
parse_ad(naddr from,
	 naddr gate,
	 n_long pref,			/* signed and in network order */
	 u_short life,			/* in host byte order */
	 struct interface *ifp)
{
	static struct msg_limit bad_gate;
	struct dr *drp, *new_drp;


	if (gate == RIP_DEFAULT
	    || !check_dst(gate)) {
		msglim(&bad_gate, from,"router %s advertising bad gateway %s",
		       naddr_ntoa(from),
		       naddr_ntoa(gate));
		return;
	}

	/* ignore pointers to ourself and routes via unreachable networks
	 */
	if (ifwithaddr(gate, 1, 0) != NULL) {
		trace_pkt("    discard Router Discovery Ad pointing at us");
		return;
	}
	if (!on_net(gate, ifp->int_net, ifp->int_mask)) {
		trace_pkt("    discard Router Discovery Ad"
			  " toward unreachable net");
		return;
	}

	/* Convert preference to an unsigned value
	 * and later bias it by the metric of the interface.
	 */
	pref = UNSIGN_PREF(ntohl(pref));

	if (pref == 0 || life < MinMaxAdvertiseInterval) {
		pref = 0;
		life = 0;
	}

	for (new_drp = NULL, drp = drs; drp < &drs[MAX_ADS]; drp++) {
		/* accept new info for a familiar entry
		 */
		if (drp->dr_gate == gate) {
			new_drp = drp;
			break;
		}

		if (life == 0)
			continue;	/* do not worry about dead ads */

		if (drp->dr_ts == 0) {
			new_drp = drp;	/* use unused entry */

		} else if (new_drp == NULL) {
			/* look for an entry worse than the new one to
			 * reuse.
			 */
			if ((!(ifp->int_state & IS_SICK)
			     && (drp->dr_ifp->int_state & IS_SICK))
			    || (pref > drp->dr_pref
				&& !((ifp->int_state ^ drp->dr_ifp->int_state)
				     & IS_SICK)))
				new_drp = drp;

		} else if (new_drp->dr_ts != 0) {
			/* look for the least valuable entry to reuse
			 */
			if ((!(new_drp->dr_ifp->int_state & IS_SICK)
			     && (drp->dr_ifp->int_state & IS_SICK))
			    || (new_drp->dr_pref > drp->dr_pref
				&& !((new_drp->dr_ifp->int_state
				      ^ drp->dr_ifp->int_state)
				     & IS_SICK)))
				new_drp = drp;
		}
	}

	/* forget it if all of the current entries are better */
	if (new_drp == NULL)
		return;

	new_drp->dr_ifp = ifp;
	new_drp->dr_gate = gate;
	new_drp->dr_ts = now.tv_sec;
	new_drp->dr_life = life;
	new_drp->dr_recv_pref = pref;
	/* bias functional preference by metric of the interface */
	new_drp->dr_pref = PREF(pref,ifp);

	/* after hearing a good advertisement, stop asking
	 */
	if (!(ifp->int_state & IS_SICK))
		ifp->int_rdisc_cnt = MAX_SOLICITATIONS;
}


/* Compute the IP checksum
 *	This assumes the packet is less than 32K long.
 */
static u_short
in_cksum(u_short *p,
	 u_int len)
{
	u_int sum = 0;
	int nwords = len >> 1;

	while (nwords-- != 0)
		sum += *p++;

	if (len & 1)
		sum += *(u_char *)p;

	/* end-around-carry */
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	return (~sum);
}


/* Send a router discovery advertisement or solicitation ICMP packet.
 */
static void
send_rdisc(union ad_u *p,
	   int p_size,
	   struct interface *ifp,
	   naddr dst,			/* 0 or unicast destination */
	   int	type)			/* 0=unicast, 1=bcast, 2=mcast */
{
	struct sockaddr_in rsin;
	int flags;
	const char *msg;


	memset(&rsin, 0, sizeof(rsin));
	rsin.sin_addr.s_addr = dst;
	rsin.sin_family = AF_INET;
#ifdef _HAVE_SIN_LEN
	rsin.sin_len = sizeof(rsin);
#endif
	flags = MSG_DONTROUTE;

	switch (type) {
	case 0:				/* unicast */
	default:
		msg = "Send";
		break;

	case 1:				/* broadcast */
		if (ifp->int_if_flags & IFF_POINTOPOINT) {
			msg = "Send pt-to-pt";
			rsin.sin_addr.s_addr = ifp->int_dstaddr;
		} else {
			msg = "Send broadcast";
			rsin.sin_addr.s_addr = ifp->int_brdaddr;
		}
		break;

	case 2:				/* multicast */
		msg = "Send multicast";
		if (ifp->int_state & IS_DUP) {
			trace_act("abort multicast output via %s"
				  " with duplicate address",
				  ifp->int_name);
			return;
		}
		if (rdisc_sock_mcast != ifp) {
			/* select the right interface. */
			struct ip_mreqn mreqn;

			memset(&mreqn, 0, sizeof(struct ip_mreqn));
			mreqn.imr_ifindex = ifp->int_index;
			if (0 > setsockopt(rdisc_sock,
					   IPPROTO_IP, IP_MULTICAST_IF,
					   &mreqn,
					   sizeof(mreqn))) {
				LOGERR("setsockopt(rdisc_sock,"
				       "IP_MULTICAST_IF)");
				rdisc_sock_mcast = NULL;
				return;
			}
			rdisc_sock_mcast = ifp;
		}
		flags = 0;
		break;
	}

	if (rdisc_sock < 0)
		get_rdisc_sock();

	trace_rdisc(msg, (ifp ? ifp->int_addr : 0), rsin.sin_addr.s_addr, ifp,
		    p, p_size);

	if (0 > sendto(rdisc_sock, p, p_size, flags,
		       (struct sockaddr *)&rsin, sizeof(rsin))) {
		if (ifp == NULL || !(ifp->int_state & IS_BROKE))
			msglog("sendto(%s%s%s): %s",
			       ifp != NULL ? ifp->int_name : "",
			       ifp != NULL ? ", " : "",
			       inet_ntoa(rsin.sin_addr),
			       strerror(errno));
		if (ifp != NULL)
			if_sick(ifp);
	}
}


/* Send an advertisement
 */
static void
send_adv(struct interface *ifp,
	 naddr	dst,			/* 0 or unicast destination */
	 int	type)			/* 0=unicast, 1=bcast, 2=mcast */
{
	union ad_u u;
	n_long pref;


	memset(&u, 0, sizeof(u.ad));

	u.ad.icmp_type = ICMP_ROUTERADVERT;
	u.ad.icmp_ad_num = 1;
	u.ad.icmp_ad_asize = sizeof(u.ad.icmp_ad_info[0])/4;

	u.ad.icmp_ad_life = stopint ? 0 : htons(ifp->int_rdisc_int*3);

	/* Convert the configured preference to an unsigned value,
	 * bias it by the interface metric, and then send it as a
	 * signed, network byte order value.
	 */
	pref = UNSIGN_PREF(ifp->int_rdisc_pref);
	u.ad.icmp_ad_info[0].icmp_ad_pref = htonl(SIGN_PREF(PREF(pref, ifp)));

	u.ad.icmp_ad_info[0].icmp_ad_addr = ifp->int_addr;

	u.ad.icmp_cksum = in_cksum((u_short*)&u.ad, sizeof(u.ad));

	send_rdisc(&u, sizeof(u.ad), ifp, dst, type);
}


/* Advertise for Router Discovery
 */
void
rdisc_adv(void)
{
	struct interface *ifp;

	if (!supplier)
		return;

	rdisc_timer.tv_sec = now.tv_sec + NEVER;

	LIST_FOREACH(ifp, &ifnet, int_list) {
		if (0 != (ifp->int_state & (IS_NO_ADV_OUT | IS_BROKE)))
			continue;

		if (!timercmp(&ifp->int_rdisc_timer, &now, >)
		    || stopint) {
			send_adv(ifp, htonl(INADDR_ALLHOSTS_GROUP),
				 (ifp->int_state&IS_BCAST_RDISC) ? 1 : 2);
			ifp->int_rdisc_cnt++;

			intvl_random(&ifp->int_rdisc_timer,
				     (ifp->int_rdisc_int*3)/4,
				     ifp->int_rdisc_int);
			if (ifp->int_rdisc_cnt < MAX_INITIAL_ADVERTS
			    && (ifp->int_rdisc_timer.tv_sec
				> MAX_INITIAL_ADVERT_INTERVAL)) {
				ifp->int_rdisc_timer.tv_sec
				= MAX_INITIAL_ADVERT_INTERVAL;
			}
			timevaladd(&ifp->int_rdisc_timer, &now);
		}

		if (timercmp(&rdisc_timer, &ifp->int_rdisc_timer, >))
			rdisc_timer = ifp->int_rdisc_timer;
	}
}


/* Solicit for Router Discovery
 */
void
rdisc_sol(void)
{
	struct interface *ifp;
	union ad_u u;


	if (supplier)
		return;

	rdisc_timer.tv_sec = now.tv_sec + NEVER;

	LIST_FOREACH(ifp, &ifnet, int_list) {
		if (0 != (ifp->int_state & (IS_NO_SOL_OUT | IS_BROKE))
		    || ifp->int_rdisc_cnt >= MAX_SOLICITATIONS)
			continue;

		if (!timercmp(&ifp->int_rdisc_timer, &now, >)) {
			memset(&u, 0, sizeof(u.so));
			u.so.icmp_type = ICMP_ROUTERSOLICIT;
			u.so.icmp_cksum = in_cksum((u_short*)&u.so,
						   sizeof(u.so));
			send_rdisc(&u, sizeof(u.so), ifp,
				   htonl(INADDR_ALLROUTERS_GROUP),
				   ((ifp->int_state&IS_BCAST_RDISC) ? 1 : 2));

			if (++ifp->int_rdisc_cnt >= MAX_SOLICITATIONS)
				continue;

			ifp->int_rdisc_timer.tv_sec = SOLICITATION_INTERVAL;
			ifp->int_rdisc_timer.tv_usec = 0;
			timevaladd(&ifp->int_rdisc_timer, &now);
		}

		if (timercmp(&rdisc_timer, &ifp->int_rdisc_timer, >))
			rdisc_timer = ifp->int_rdisc_timer;
	}
}


/* check the IP header of a possible Router Discovery ICMP packet */
static struct interface *		/* 0 if bad */
ck_icmp(const char *act,
	naddr	from,
	struct interface *ifp,
	naddr	to,
	union ad_u *p,
	u_int	len)
{
	const char *type;


	if (p->icmp.icmp_type == ICMP_ROUTERADVERT) {
		type = "advertisement";
	} else if (p->icmp.icmp_type == ICMP_ROUTERSOLICIT) {
		type = "solicitation";
	} else {
		return 0;
	}

	if (p->icmp.icmp_code != 0) {
		trace_pkt("unrecognized ICMP Router %s code=%d from %s to %s",
			  type, p->icmp.icmp_code,
			  naddr_ntoa(from), naddr_ntoa(to));
		return 0;
	}

	trace_rdisc(act, from, to, ifp, p, len);

	if (ifp == NULL)
		trace_pkt("unknown interface for router-discovery %s"
			  " from %s to %s",
			  type, naddr_ntoa(from), naddr_ntoa(to));

	return ifp;
}


/* read packets from the router discovery socket
 */
void
read_d(void)
{
	static struct msg_limit bad_asize, bad_len;
#ifdef USE_PASSIFNAME
	static struct msg_limit  bad_name;
#endif
	struct sockaddr_in from;
	int n, fromlen, cc, hlen;
	struct {
#ifdef USE_PASSIFNAME
		char	ifname[IFNAMSIZ];
#endif
		union {
			struct ip ip;
			u_char	b[512];
		} pkt;
	} buf;
	union ad_u *p;
	n_long *wp;
	struct interface *ifp;


	for (;;) {
		fromlen = sizeof(from);
		cc = recvfrom(rdisc_sock, &buf, sizeof(buf), 0,
			      (struct sockaddr*)&from,
			      &fromlen);
		if (cc <= 0) {
			if (cc < 0 && errno != EWOULDBLOCK)
				LOGERR("recvfrom(rdisc_sock)");
			break;
		}
		if (fromlen != sizeof(struct sockaddr_in))
			logbad(1,"impossible recvfrom(rdisc_sock) fromlen=%d",
			       fromlen);
#ifdef USE_PASSIFNAME
		if ((cc -= sizeof(buf.ifname)) < 0)
			logbad(0,"missing USE_PASSIFNAME; only %d bytes",
			       cc+sizeof(buf.ifname));
#endif

		hlen = buf.pkt.ip.ip_hl << 2;
		if (cc < hlen + ICMP_MINLEN)
			continue;
		p = (union ad_u *)&buf.pkt.b[hlen];
		cc -= hlen;

#ifdef USE_PASSIFNAME
		ifp = ifwithname(buf.ifname, 0);
		if (ifp == NULL)
			msglim(&bad_name, from.sin_addr.s_addr,
			       "impossible rdisc if_ name %.*s",
			       IFNAMSIZ, buf.ifname);
#else
		/* If we could tell the interface on which a packet from
		 * address 0 arrived, we could deal with such solicitations.
		 */
		ifp = ((from.sin_addr.s_addr == 0)
		       ? 0 : iflookup(from.sin_addr.s_addr));
#endif
		ifp = ck_icmp("Recv", from.sin_addr.s_addr, ifp,
			      buf.pkt.ip.ip_dst.s_addr, p, cc);
		if (ifp == NULL)
			continue;
		if (ifwithaddr(from.sin_addr.s_addr, 0, 0)) {
			trace_pkt("    "
				  "discard our own Router Discovery message");
			continue;
		}

		switch (p->icmp.icmp_type) {
		case ICMP_ROUTERADVERT:
			if (p->ad.icmp_ad_asize*4
			    < (int)sizeof(p->ad.icmp_ad_info[0])) {
				msglim(&bad_asize, from.sin_addr.s_addr,
				       "intolerable rdisc address size=%d",
				       p->ad.icmp_ad_asize);
				continue;
			}
			if (p->ad.icmp_ad_num == 0) {
				trace_pkt("    empty?");
				continue;
			}
			if (cc != (int)(sizeof(p->ad)
					- sizeof(p->ad.icmp_ad_info)
					+ (p->ad.icmp_ad_num
					   * sizeof(p->ad.icmp_ad_info[0])))) {
				msglim(&bad_len, from.sin_addr.s_addr,
				       "rdisc length %d does not match ad_num"
				       " %d", cc, p->ad.icmp_ad_num);
				continue;
			}
			if (supplier)
				continue;
			if (ifp->int_state & IS_NO_ADV_IN)
				continue;

			wp = &p->ad.icmp_ad_info[0].icmp_ad_addr;
			for (n = 0; n < p->ad.icmp_ad_num; n++) {
				parse_ad(from.sin_addr.s_addr,
					 wp[0], wp[1],
					 ntohs(p->ad.icmp_ad_life),
					 ifp);
				wp += p->ad.icmp_ad_asize;
			}
			break;


		case ICMP_ROUTERSOLICIT:
			if (!supplier)
				continue;
			if (ifp->int_state & IS_NO_ADV_OUT)
				continue;
			if (stopint)
				continue;

			/* XXX
			 * We should handle messages from address 0.
			 */

			/* Respond with a point-to-point advertisement */
			send_adv(ifp, from.sin_addr.s_addr, 0);
			break;
		}
	}

	rdisc_sort();
}
