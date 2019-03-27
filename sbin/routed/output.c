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
 * $FreeBSD$
 */

#include "defs.h"

#ifdef __NetBSD__
__RCSID("$NetBSD$");
#elif defined(__FreeBSD__)
__RCSID("$FreeBSD$");
#else
__RCSID("$Revision: 2.27 $");
#ident "$Revision: 2.27 $"
#endif


u_int update_seqno;


/* walk the tree of routes with this for output
 */
static struct {
	struct sockaddr_in to;
	naddr	to_mask;
	naddr	to_net;
	naddr	to_std_mask;
	naddr	to_std_net;
	struct interface *ifp;		/* usually output interface */
	struct auth *a;
	char	metric;			/* adjust metrics by interface */
	int	npackets;
	int	gen_limit;
	u_int	state;
#define	    WS_ST_FLASH	    0x001	/* send only changed routes */
#define	    WS_ST_RIP2_ALL  0x002	/* send full featured RIPv2 */
#define	    WS_ST_AG	    0x004	/* ok to aggregate subnets */
#define	    WS_ST_SUPER_AG  0x008	/* ok to aggregate networks */
#define	    WS_ST_QUERY	    0x010	/* responding to a query */
#define	    WS_ST_TO_ON_NET 0x020	/* sending onto one of our nets */
#define	    WS_ST_DEFAULT   0x040	/* faking a default */
} ws;

/* A buffer for what can be heard by both RIPv1 and RIPv2 listeners */
struct ws_buf v12buf;
static union pkt_buf ripv12_buf;

/* Another for only RIPv2 listeners */
static struct ws_buf v2buf;
static union pkt_buf rip_v2_buf;



void
bufinit(void)
{
	ripv12_buf.rip.rip_cmd = RIPCMD_RESPONSE;
	v12buf.buf = &ripv12_buf.rip;
	v12buf.base = &v12buf.buf->rip_nets[0];

	rip_v2_buf.rip.rip_cmd = RIPCMD_RESPONSE;
	rip_v2_buf.rip.rip_vers = RIPv2;
	v2buf.buf = &rip_v2_buf.rip;
	v2buf.base = &v2buf.buf->rip_nets[0];
}


/* Send the contents of the global buffer via the non-multicast socket
 */
int					/* <0 on failure */
output(enum output_type type,
       struct sockaddr_in *dst,		/* send to here */
       struct interface *ifp,
       struct rip *buf,
       int size)			/* this many bytes */
{
	struct sockaddr_in osin;
	int flags;
	const char *msg;
	int res;
	int soc;
	int serrno;

	assert(ifp != NULL);
	osin = *dst;
	if (osin.sin_port == 0)
		osin.sin_port = htons(RIP_PORT);
#ifdef _HAVE_SIN_LEN
	if (osin.sin_len == 0)
		osin.sin_len = sizeof(osin);
#endif

	soc = rip_sock;
	flags = 0;

	switch (type) {
	case OUT_QUERY:
		msg = "Answer Query";
		if (soc < 0)
			soc = ifp->int_rip_sock;
		break;
	case OUT_UNICAST:
		msg = "Send";
		if (soc < 0)
			soc = ifp->int_rip_sock;
		flags = MSG_DONTROUTE;
		break;
	case OUT_BROADCAST:
		if (ifp->int_if_flags & IFF_POINTOPOINT) {
			msg = "Send";
		} else {
			msg = "Send bcast";
		}
		flags = MSG_DONTROUTE;
		break;
	case OUT_MULTICAST:
		if ((ifp->int_if_flags & (IFF_POINTOPOINT|IFF_MULTICAST)) ==
		    IFF_POINTOPOINT) {
			msg = "Send pt-to-pt";
		} else if (ifp->int_state & IS_DUP) {
			trace_act("abort multicast output via %s"
				  " with duplicate address",
				  ifp->int_name);
			return 0;
		} else {
			msg = "Send mcast";
			if (rip_sock_mcast != ifp) {
				struct ip_mreqn mreqn;

				memset(&mreqn, 0, sizeof(struct ip_mreqn));
				mreqn.imr_ifindex = ifp->int_index;
				if (0 > setsockopt(rip_sock,
						   IPPROTO_IP,
						   IP_MULTICAST_IF,
						   &mreqn,
						   sizeof(mreqn))) {
					serrno = errno;
					LOGERR("setsockopt(rip_sock, "
					       "IP_MULTICAST_IF)");
					errno = serrno;
					ifp = NULL;
					return -1;
				}
				rip_sock_mcast = ifp;
			}
			osin.sin_addr.s_addr = htonl(INADDR_RIP_GROUP);
		}
		break;

	case NO_OUT_MULTICAST:
	case NO_OUT_RIPV2:
	default:
#ifdef DEBUG
		abort();
#endif
		return -1;
	}

	trace_rip(msg, "to", &osin, ifp, buf, size);

	res = sendto(soc, buf, size, flags,
		     (struct sockaddr *)&osin, sizeof(osin));
	if (res < 0
	    && (ifp == NULL || !(ifp->int_state & IS_BROKE))) {
		serrno = errno;
		msglog("%s sendto(%s%s%s.%d): %s", msg,
		       ifp != NULL ? ifp->int_name : "",
		       ifp != NULL ? ", " : "",
		       inet_ntoa(osin.sin_addr),
		       ntohs(osin.sin_port),
		       strerror(errno));
		errno = serrno;
	}

	return res;
}


/* Find the first key for a packet to send.
 * Try for a key that is eligible and has not expired, but settle for
 * the last key if they have all expired.
 * If no key is ready yet, give up.
 */
struct auth *
find_auth(struct interface *ifp)
{
	struct auth *ap, *res;
	int i;


	if (ifp == NULL)
		return 0;

	res = NULL;
	ap = ifp->int_auth;
	for (i = 0; i < MAX_AUTH_KEYS; i++, ap++) {
		/* stop looking after the last key */
		if (ap->type == RIP_AUTH_NONE)
			break;

		/* ignore keys that are not ready yet */
		if ((u_long)ap->start > (u_long)clk.tv_sec)
			continue;

		if ((u_long)ap->end < (u_long)clk.tv_sec) {
			/* note best expired password as a fall-back */
			if (res == NULL || (u_long)ap->end > (u_long)res->end)
				res = ap;
			continue;
		}

		/* note key with the best future */
		if (res == NULL || (u_long)res->end < (u_long)ap->end)
			res = ap;
	}
	return res;
}


void
clr_ws_buf(struct ws_buf *wb,
	   struct auth *ap)
{
	struct netauth *na;

	wb->lim = wb->base + NETS_LEN;
	wb->n = wb->base;
	memset(wb->n, 0, NETS_LEN*sizeof(*wb->n));

	/* (start to) install authentication if appropriate
	 */
	if (ap == NULL)
		return;

	na = (struct netauth*)wb->n;
	if (ap->type == RIP_AUTH_PW) {
		na->a_family = RIP_AF_AUTH;
		na->a_type = RIP_AUTH_PW;
		memcpy(na->au.au_pw, ap->key, sizeof(na->au.au_pw));
		wb->n++;

	} else if (ap->type ==  RIP_AUTH_MD5) {
		na->a_family = RIP_AF_AUTH;
		na->a_type = RIP_AUTH_MD5;
		na->au.a_md5.md5_keyid = ap->keyid;
		na->au.a_md5.md5_auth_len = RIP_AUTH_MD5_KEY_LEN;
		na->au.a_md5.md5_seqno = htonl(clk.tv_sec);
		wb->n++;
		wb->lim--;		/* make room for trailer */
	}
}


void
end_md5_auth(struct ws_buf *wb,
	     struct auth *ap)
{
	struct netauth *na, *na2;
	MD5_CTX md5_ctx;
	int len;


	na = (struct netauth*)wb->base;
	na2 = (struct netauth*)wb->n;
	len = (char *)na2-(char *)wb->buf;
	na2->a_family = RIP_AF_AUTH;
	na2->a_type = htons(1);
	na->au.a_md5.md5_pkt_len = htons(len);
	MD5Init(&md5_ctx);
	MD5Update(&md5_ctx, (u_char *)wb->buf, len + RIP_AUTH_MD5_HASH_XTRA);
	MD5Update(&md5_ctx, ap->key, RIP_AUTH_MD5_KEY_LEN);
	MD5Final(na2->au.au_pw, &md5_ctx);
	wb->n++;
}


/* Send the buffer
 */
static void
supply_write(struct ws_buf *wb)
{
	/* Output multicast only if legal.
	 * If we would multicast and it would be illegal, then discard the
	 * packet.
	 */
	switch (wb->type) {
	case NO_OUT_MULTICAST:
		trace_pkt("skip multicast to %s because impossible",
			  naddr_ntoa(ws.to.sin_addr.s_addr));
		break;
	case NO_OUT_RIPV2:
		break;
	default:
		if (ws.a != NULL && ws.a->type == RIP_AUTH_MD5)
			end_md5_auth(wb,ws.a);
		if (output(wb->type, &ws.to, ws.ifp, wb->buf,
			   ((char *)wb->n - (char*)wb->buf)) < 0
		    && ws.ifp != NULL)
			if_sick(ws.ifp);
		ws.npackets++;
		break;
	}

	clr_ws_buf(wb,ws.a);
}


/* put an entry into the packet
 */
static void
supply_out(struct ag_info *ag)
{
	int i;
	naddr mask, v1_mask, dst_h, ddst_h = 0;
	struct ws_buf *wb;


	/* Skip this route if doing a flash update and it and the routes
	 * it aggregates have not changed recently.
	 */
	if (ag->ag_seqno < update_seqno
	    && (ws.state & WS_ST_FLASH))
		return;

	dst_h = ag->ag_dst_h;
	mask = ag->ag_mask;
	v1_mask = ripv1_mask_host(htonl(dst_h),
				  (ws.state & WS_ST_TO_ON_NET) ? ws.ifp : 0);
	i = 0;

	/* If we are sending RIPv2 packets that cannot (or must not) be
	 * heard by RIPv1 listeners, do not worry about sub- or supernets.
	 * Subnets (from other networks) can only be sent via multicast.
	 * A pair of subnet routes might have been promoted so that they
	 * are legal to send by RIPv1.
	 * If RIPv1 is off, use the multicast buffer.
	 */
	if ((ws.state & WS_ST_RIP2_ALL)
	    || ((ag->ag_state & AGS_RIPV2) && v1_mask != mask)) {
		/* use the RIPv2-only buffer */
		wb = &v2buf;

	} else {
		/* use the RIPv1-or-RIPv2 buffer */
		wb = &v12buf;

		/* Convert supernet route into corresponding set of network
		 * routes for RIPv1, but leave non-contiguous netmasks
		 * to ag_check().
		 */
		if (v1_mask > mask
		    && mask + (mask & -mask) == 0) {
			ddst_h = v1_mask & -v1_mask;
			i = (v1_mask & ~mask)/ddst_h;

			if (i > ws.gen_limit) {
				/* Punt if we would have to generate an
				 * unreasonable number of routes.
				 */
				if (TRACECONTENTS)
					trace_misc("sending %s-->%s as 1"
						   " instead of %d routes",
						   addrname(htonl(dst_h), mask,
							1),
						   naddr_ntoa(ws.to.sin_addr
							.s_addr),
						   i+1);
				i = 0;

			} else {
				mask = v1_mask;
				ws.gen_limit -= i;
			}
		}
	}

	do {
		wb->n->n_family = RIP_AF_INET;
		wb->n->n_dst = htonl(dst_h);
		/* If the route is from router-discovery or we are
		 * shutting down, admit only a bad metric.
		 */
		wb->n->n_metric = ((stopint || ag->ag_metric < 1)
				   ? HOPCNT_INFINITY
				   : ag->ag_metric);
		wb->n->n_metric = htonl(wb->n->n_metric);
		/* Any non-zero bits in the supposedly unused RIPv1 fields
		 * cause the old `routed` to ignore the route.
		 * That means the mask and so forth cannot be sent
		 * in the hybrid RIPv1/RIPv2 mode.
		 */
		if (ws.state & WS_ST_RIP2_ALL) {
			if (ag->ag_nhop != 0
			    && ((ws.state & WS_ST_QUERY)
				|| (ag->ag_nhop != ws.ifp->int_addr
				    && on_net(ag->ag_nhop,
					      ws.ifp->int_net,
					      ws.ifp->int_mask))))
				wb->n->n_nhop = ag->ag_nhop;
			wb->n->n_mask = htonl(mask);
			wb->n->n_tag = ag->ag_tag;
		}
		dst_h += ddst_h;

		if (++wb->n >= wb->lim)
			supply_write(wb);
	} while (i-- != 0);
}


/* supply one route from the table
 */
/* ARGSUSED */
static int
walk_supply(struct radix_node *rn,
	    struct walkarg *argp UNUSED)
{
#define RT ((struct rt_entry *)rn)
	u_short ags;
	char metric, pref;
	naddr dst, nhop;
	struct rt_spare *rts;
	int i;


	/* Do not advertise external remote interfaces or passive interfaces.
	 */
	if ((RT->rt_state & RS_IF)
	    && RT->rt_ifp != 0
	    && (RT->rt_ifp->int_state & IS_PASSIVE)
	    && !(RT->rt_state & RS_MHOME))
		return 0;

	/* If being quiet about our ability to forward, then
	 * do not say anything unless responding to a query,
	 * except about our main interface.
	 */
	if (!supplier && !(ws.state & WS_ST_QUERY)
	    && !(RT->rt_state & RS_MHOME))
		return 0;

	dst = RT->rt_dst;

	/* do not collide with the fake default route */
	if (dst == RIP_DEFAULT
	    && (ws.state & WS_ST_DEFAULT))
		return 0;

	if (RT->rt_state & RS_NET_SYN) {
		if (RT->rt_state & RS_NET_INT) {
			/* Do not send manual synthetic network routes
			 * into the subnet.
			 */
			if (on_net(ws.to.sin_addr.s_addr,
				   ntohl(dst), RT->rt_mask))
				return 0;

		} else {
			/* Do not send automatic synthetic network routes
			 * if they are not needed because no RIPv1 listeners
			 * can hear them.
			 */
			if (ws.state & WS_ST_RIP2_ALL)
				return 0;

			/* Do not send automatic synthetic network routes to
			 * the real subnet.
			 */
			if (on_net(ws.to.sin_addr.s_addr,
				   ntohl(dst), RT->rt_mask))
				return 0;
		}
		nhop = 0;

	} else {
		/* Advertise the next hop if this is not a route for one
		 * of our interfaces and the next hop is on the same
		 * network as the target.
		 * The final determination is made by supply_out().
		 */
		if (!(RT->rt_state & RS_IF)
		    && RT->rt_gate != myaddr
		    && RT->rt_gate != loopaddr)
			nhop = RT->rt_gate;
		else
			nhop = 0;
	}

	metric = RT->rt_metric;
	ags = 0;

	if (RT->rt_state & RS_MHOME) {
		/* retain host route of multi-homed servers */
		;

	} else if (RT_ISHOST(RT)) {
		/* We should always suppress (into existing network routes)
		 * the host routes for the local end of our point-to-point
		 * links.
		 * If we are suppressing host routes in general, then do so.
		 * Avoid advertising host routes onto their own network,
		 * where they should be handled by proxy-ARP.
		 */
		if ((RT->rt_state & RS_LOCAL)
		    || ridhosts
		    || on_net(dst, ws.to_net, ws.to_mask))
			ags |= AGS_SUPPRESS;

		/* Aggregate stray host routes into network routes if allowed.
		 * We cannot aggregate host routes into small network routes
		 * without confusing RIPv1 listeners into thinking the
		 * network routes are host routes.
		 */
		if ((ws.state & WS_ST_AG) && (ws.state & WS_ST_RIP2_ALL))
			ags |= AGS_AGGREGATE;

	} else {
		/* Always suppress network routes into other, existing
		 * network routes
		 */
		ags |= AGS_SUPPRESS;

		/* Generate supernets if allowed.
		 * If we can be heard by RIPv1 systems, we will
		 * later convert back to ordinary nets.
		 * This unifies dealing with received supernets.
		 */
		if ((ws.state & WS_ST_AG)
		    && ((RT->rt_state & RS_SUBNET)
			|| (ws.state & WS_ST_SUPER_AG)))
			ags |= AGS_AGGREGATE;
	}

	/* Do not send RIPv1 advertisements of subnets to other
	 * networks. If possible, multicast them by RIPv2.
	 */
	if ((RT->rt_state & RS_SUBNET)
	    && !(ws.state & WS_ST_RIP2_ALL)
	    && !on_net(dst, ws.to_std_net, ws.to_std_mask))
		ags |= AGS_RIPV2 | AGS_AGGREGATE;


	/* Do not send a route back to where it came from, except in
	 * response to a query.  This is "split-horizon".  That means not
	 * advertising back to the same network	and so via the same interface.
	 *
	 * We want to suppress routes that might have been fragmented
	 * from this route by a RIPv1 router and sent back to us, and so we
	 * cannot forget this route here.  Let the split-horizon route
	 * suppress the fragmented routes and then itself be forgotten.
	 *
	 * Include the routes for both ends of point-to-point interfaces
	 * among those suppressed by split-horizon, since the other side
	 * should knows them as well as we do.
	 *
	 * Notice spare routes with the same metric that we are about to
	 * advertise, to split the horizon on redundant, inactive paths.
	 *
	 * Do not suppress advertisements of interface-related addresses on
	 * non-point-to-point interfaces.  This ensures that we have something
	 * to say every 30 seconds to help detect broken Ethernets or
	 * other interfaces where one packet every 30 seconds costs nothing.
	 */
	if (ws.ifp != NULL
	    && !(ws.state & WS_ST_QUERY)
	    && (ws.state & WS_ST_TO_ON_NET)
	    && (!(RT->rt_state & RS_IF)
		|| ws.ifp->int_if_flags & IFF_POINTOPOINT)) {
		for (rts = RT->rt_spares, i = NUM_SPARES; i != 0; i--, rts++) {
			if (rts->rts_metric > metric
			    || rts->rts_ifp != ws.ifp)
				continue;

			/* If we do not mark the route with AGS_SPLIT_HZ here,
			 * it will be poisoned-reverse, or advertised back
			 * toward its source with an infinite metric.
			 * If we have recently advertised the route with a
			 * better metric than we now have, then we should
			 * poison-reverse the route before suppressing it for
			 * split-horizon.
			 *
			 * In almost all cases, if there is no spare for the
			 * route then it is either old and dead or a brand
			 * new route. If it is brand new, there is no need
			 * for poison-reverse. If it is old and dead, it
			 * is already poisoned.
			 */
			if (RT->rt_poison_time < now_expire
			    || RT->rt_poison_metric >= metric
			    || RT->rt_spares[1].rts_gate == 0) {
				ags |= AGS_SPLIT_HZ;
				ags &= ~AGS_SUPPRESS;
			}
			metric = HOPCNT_INFINITY;
			break;
		}
	}

	/* Keep track of the best metric with which the
	 * route has been advertised recently.
	 */
	if (RT->rt_poison_metric >= metric
	    || RT->rt_poison_time < now_expire) {
		RT->rt_poison_time = now.tv_sec;
		RT->rt_poison_metric = metric;
	}

	/* Adjust the outgoing metric by the cost of the link.
	 * Avoid aggregation when a route is counting to infinity.
	 */
	pref = RT->rt_poison_metric + ws.metric;
	metric += ws.metric;

	/* Do not advertise stable routes that will be ignored,
	 * unless we are answering a query.
	 * If the route recently was advertised with a metric that
	 * would have been less than infinity through this interface,
	 * we need to continue to advertise it in order to poison it.
	 */
	if (metric >= HOPCNT_INFINITY) {
		if (!(ws.state & WS_ST_QUERY)
		    && (pref >= HOPCNT_INFINITY
			|| RT->rt_poison_time < now_garbage))
			return 0;

		metric = HOPCNT_INFINITY;
	}

	ag_check(dst, RT->rt_mask, 0, nhop, metric, pref,
		 RT->rt_seqno, RT->rt_tag, ags, supply_out);
	return 0;
#undef RT
}


/* Supply dst with the contents of the routing tables.
 * If this won't fit in one packet, chop it up into several.
 */
void
supply(struct sockaddr_in *dst,
       struct interface *ifp,		/* output interface */
       enum output_type type,
       int flash,			/* 1=flash update */
       int vers,			/* RIP version */
       int passwd_ok)			/* OK to include cleartext password */
{
	struct rt_entry *rt;
	int def_metric;

	ws.state = 0;
	ws.gen_limit = 1024;

	ws.to = *dst;
	ws.to_std_mask = std_mask(ws.to.sin_addr.s_addr);
	ws.to_std_net = ntohl(ws.to.sin_addr.s_addr) & ws.to_std_mask;

	if (ifp != NULL) {
		ws.to_mask = ifp->int_mask;
		ws.to_net = ifp->int_net;
		if (on_net(ws.to.sin_addr.s_addr, ws.to_net, ws.to_mask))
			ws.state |= WS_ST_TO_ON_NET;

	} else {
		ws.to_mask = ripv1_mask_net(ws.to.sin_addr.s_addr, 0);
		ws.to_net = ntohl(ws.to.sin_addr.s_addr) & ws.to_mask;
		rt = rtfind(dst->sin_addr.s_addr);
		if (rt)
			ifp = rt->rt_ifp;
	}

	ws.npackets = 0;
	if (flash)
		ws.state |= WS_ST_FLASH;

	if ((ws.ifp = ifp) == NULL) {
		ws.metric = 1;
	} else {
		/* Adjust the advertised metric by the outgoing interface
		 * metric.
		 */
		ws.metric = ifp->int_metric + 1 + ifp->int_adj_outmetric;
	}

	ripv12_buf.rip.rip_vers = vers;

	switch (type) {
	case OUT_MULTICAST:
		if (ifp != NULL && ifp->int_if_flags & IFF_MULTICAST)
			v2buf.type = OUT_MULTICAST;
		else
			v2buf.type = NO_OUT_MULTICAST;
		v12buf.type = OUT_BROADCAST;
		break;

	case OUT_QUERY:
		ws.state |= WS_ST_QUERY;
		/* FALLTHROUGH */
	case OUT_BROADCAST:
	case OUT_UNICAST:
		v2buf.type = (vers == RIPv2) ? type : NO_OUT_RIPV2;
		v12buf.type = type;
		break;

	case NO_OUT_MULTICAST:
	case NO_OUT_RIPV2:
		break;			/* no output */
	}

	if (vers == RIPv2) {
		/* full RIPv2 only if cannot be heard by RIPv1 listeners */
		if (type != OUT_BROADCAST)
			ws.state |= WS_ST_RIP2_ALL;
		if ((ws.state & WS_ST_QUERY)
		    || !(ws.state & WS_ST_TO_ON_NET)) {
			ws.state |= (WS_ST_AG | WS_ST_SUPER_AG);
		} else if (ifp == NULL || !(ifp->int_state & IS_NO_AG)) {
			ws.state |= WS_ST_AG;
			if (type != OUT_BROADCAST
			    && (ifp == NULL
				|| !(ifp->int_state & IS_NO_SUPER_AG)))
				ws.state |= WS_ST_SUPER_AG;
		}
	}

	ws.a = (vers == RIPv2) ? find_auth(ifp) : 0;
	if (!passwd_ok && ws.a != NULL && ws.a->type == RIP_AUTH_PW)
		ws.a = NULL;
	clr_ws_buf(&v12buf,ws.a);
	clr_ws_buf(&v2buf,ws.a);

	/*  Fake a default route if asked and if there is not already
	 * a better, real default route.
	 */
	if (supplier && ifp && (def_metric = ifp->int_d_metric) != 0) {
		if ((rt = rtget(RIP_DEFAULT, 0)) == NULL 
		    || rt->rt_metric+ws.metric >= def_metric) {
			ws.state |= WS_ST_DEFAULT;
			ag_check(0, 0, 0, 0, def_metric, def_metric,
				 0, 0, 0, supply_out);
		} else {
			def_metric = rt->rt_metric+ws.metric;
		}

		/* If both RIPv2 and the poor-man's router discovery
		 * kludge are on, arrange to advertise an extra
		 * default route via RIPv1.
		 */
		if ((ws.state & WS_ST_RIP2_ALL)
		    && (ifp->int_state & IS_PM_RDISC)) {
			ripv12_buf.rip.rip_vers = RIPv1;
			v12buf.n->n_family = RIP_AF_INET;
			v12buf.n->n_dst = htonl(RIP_DEFAULT);
			v12buf.n->n_metric = htonl(def_metric);
			v12buf.n++;
		}
	}

	(void)rn_walktree(rhead, walk_supply, 0);
	ag_flush(0,0,supply_out);

	/* Flush the packet buffers, provided they are not empty and
	 * do not contain only the password.
	 */
	if (v12buf.n != v12buf.base
	    && (v12buf.n > v12buf.base+1
		|| v12buf.base->n_family != RIP_AF_AUTH))
		supply_write(&v12buf);
	if (v2buf.n != v2buf.base
	    && (v2buf.n > v2buf.base+1
		|| v2buf.base->n_family != RIP_AF_AUTH))
		supply_write(&v2buf);

	/* If we sent nothing and this is an answer to a query, send
	 * an empty buffer.
	 */
	if (ws.npackets == 0
	    && (ws.state & WS_ST_QUERY))
		supply_write(&v12buf);
}


/* send all of the routing table or just do a flash update
 */
void
rip_bcast(int flash)
{
#ifdef _HAVE_SIN_LEN
	static struct sockaddr_in dst = {sizeof(dst), AF_INET, 0, {0}, {0}};
#else
	static struct sockaddr_in dst = {AF_INET};
#endif
	struct interface *ifp;
	enum output_type type;
	int vers;
	struct timeval rtime;


	need_flash = 0;
	intvl_random(&rtime, MIN_WAITTIME, MAX_WAITTIME);
	no_flash = rtime;
	timevaladd(&no_flash, &now);

	if (rip_sock < 0)
		return;

	trace_act("send %s and inhibit dynamic updates for %.3f sec",
		  flash ? "dynamic update" : "all routes",
		  rtime.tv_sec + ((float)rtime.tv_usec)/1000000.0);

	LIST_FOREACH(ifp, &ifnet, int_list) {
		/* Skip interfaces not doing RIP.
		 * Do try broken interfaces to see if they have healed.
		 */
		if (IS_RIP_OUT_OFF(ifp->int_state))
			continue;

		/* skip turned off interfaces */
		if (!iff_up(ifp->int_if_flags))
			continue;

		vers = (ifp->int_state & IS_NO_RIPV1_OUT) ? RIPv2 : RIPv1;

		if (ifp->int_if_flags & IFF_BROADCAST) {
			/* ordinary, hardware interface */
			dst.sin_addr.s_addr = ifp->int_brdaddr;

			if (vers == RIPv2
			    && !(ifp->int_state  & IS_NO_RIP_MCAST)) {
				type = OUT_MULTICAST;
			} else {
				type = OUT_BROADCAST;
			}

		} else if (ifp->int_if_flags & IFF_POINTOPOINT) {
			/* point-to-point hardware interface */
			dst.sin_addr.s_addr = ifp->int_dstaddr;
			if (vers == RIPv2 &&
			    ifp->int_if_flags & IFF_MULTICAST &&
			    !(ifp->int_state  & IS_NO_RIP_MCAST)) {
				type = OUT_MULTICAST;
			} else {
				type = OUT_UNICAST;
			}

		} else if (ifp->int_state & IS_REMOTE) {
			/* remote interface */
			dst.sin_addr.s_addr = ifp->int_addr;
			type = OUT_UNICAST;

		} else {
			/* ATM, HIPPI, etc. */
			continue;
		}

		supply(&dst, ifp, type, flash, vers, 1);
	}

	update_seqno++;			/* all routes are up to date */
}


/* Ask for routes
 * Do it only once to an interface, and not even after the interface
 * was broken and recovered.
 */
void
rip_query(void)
{
#ifdef _HAVE_SIN_LEN
	static struct sockaddr_in dst = {sizeof(dst), AF_INET, 0, {0}, {0}};
#else
	static struct sockaddr_in dst = {AF_INET};
#endif
	struct interface *ifp;
	struct rip buf;
	enum output_type type;


	if (rip_sock < 0)
		return;

	memset(&buf, 0, sizeof(buf));

	LIST_FOREACH(ifp, &ifnet, int_list) {
		/* Skip interfaces those already queried.
		 * Do not ask via interfaces through which we don't
		 * accept input.  Do not ask via interfaces that cannot
		 * send RIP packets.
		 * Do try broken interfaces to see if they have healed.
		 */
		if (IS_RIP_IN_OFF(ifp->int_state)
		    || ifp->int_query_time != NEVER)
			continue;

		/* skip turned off interfaces */
		if (!iff_up(ifp->int_if_flags))
			continue;

		buf.rip_vers = (ifp->int_state&IS_NO_RIPV1_OUT) ? RIPv2:RIPv1;
		buf.rip_cmd = RIPCMD_REQUEST;
		buf.rip_nets[0].n_family = RIP_AF_UNSPEC;
		buf.rip_nets[0].n_metric = htonl(HOPCNT_INFINITY);

		/* Send a RIPv1 query only if allowed and if we will
		 * listen to RIPv1 routers.
		 */
		if ((ifp->int_state & IS_NO_RIPV1_OUT)
		    || (ifp->int_state & IS_NO_RIPV1_IN)) {
			buf.rip_vers = RIPv2;
		} else {
			buf.rip_vers = RIPv1;
		}

		if (ifp->int_if_flags & IFF_BROADCAST) {
			/* ordinary, hardware interface */
			dst.sin_addr.s_addr = ifp->int_brdaddr;

			/* Broadcast RIPv1 queries and RIPv2 queries
			 * when the hardware cannot multicast.
			 */
			if (buf.rip_vers == RIPv2
			    && (ifp->int_if_flags & IFF_MULTICAST)
			    && !(ifp->int_state  & IS_NO_RIP_MCAST)) {
				type = OUT_MULTICAST;
			} else {
				type = OUT_BROADCAST;
			}

		} else if (ifp->int_if_flags & IFF_POINTOPOINT) {
			/* point-to-point hardware interface */
			dst.sin_addr.s_addr = ifp->int_dstaddr;
			type = OUT_UNICAST;

		} else if (ifp->int_state & IS_REMOTE) {
			/* remote interface */
			dst.sin_addr.s_addr = ifp->int_addr;
			type = OUT_UNICAST;

		} else {
			/* ATM, HIPPI, etc. */
			continue;
		}

		ifp->int_query_time = now.tv_sec+SUPPLY_INTERVAL;
		if (output(type, &dst, ifp, &buf, sizeof(buf)) < 0)
			if_sick(ifp);
	}
}
