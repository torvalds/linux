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
__RCSID("$Revision: 2.26 $");
#ident "$Revision: 2.26 $"
#endif

static void input(struct sockaddr_in *, struct interface *, struct interface *,
		  struct rip *, int);
static void input_route(naddr, naddr, struct rt_spare *, struct netinfo *);
static int ck_passwd(struct interface *, struct rip *, void *,
		     naddr, struct msg_limit *);


/* process RIP input
 */
void
read_rip(int sock,
	 struct interface *sifp)
{
	struct sockaddr_in from;
	struct interface *aifp;
	socklen_t fromlen;
	int cc;
#ifdef USE_PASSIFNAME
	static struct msg_limit  bad_name;
	struct {
		char	ifname[IFNAMSIZ];
		union pkt_buf pbuf;
	} inbuf;
#else
	struct {
		union pkt_buf pbuf;
	} inbuf;
#endif


	for (;;) {
		fromlen = sizeof(from);
		cc = recvfrom(sock, &inbuf, sizeof(inbuf), 0,
			      (struct sockaddr*)&from, &fromlen);
		if (cc <= 0) {
			if (cc < 0 && errno != EWOULDBLOCK)
				LOGERR("recvfrom(rip)");
			break;
		}
		if (fromlen != sizeof(struct sockaddr_in))
			logbad(1,"impossible recvfrom(rip) fromlen=%d",
			       (int)fromlen);

		/* aifp is the "authenticated" interface via which the packet
		 *	arrived.  In fact, it is only the interface on which
		 *	the packet should have arrived based on is source
		 *	address.
		 * sifp is interface associated with the socket through which
		 *	the packet was received.
		 */
#ifdef USE_PASSIFNAME
		if ((cc -= sizeof(inbuf.ifname)) < 0)
			logbad(0,"missing USE_PASSIFNAME; only %d bytes",
			       cc+sizeof(inbuf.ifname));

		/* check the remote interfaces first */
		LIST_FOREACH(aifp, &remote_if, remote_list) {
			if (aifp->int_addr == from.sin_addr.s_addr)
				break;
		}
		if (aifp == NULL) {
			aifp = ifwithname(inbuf.ifname, 0);
			if (aifp == NULL) {
				msglim(&bad_name, from.sin_addr.s_addr,
				       "impossible interface name %.*s",
				       IFNAMSIZ, inbuf.ifname);
			} else if (((aifp->int_if_flags & IFF_POINTOPOINT)
				    && aifp->int_dstaddr!=from.sin_addr.s_addr)
				   || (!(aifp->int_if_flags & IFF_POINTOPOINT)
				       && !on_net(from.sin_addr.s_addr,
						  aifp->int_net,
						  aifp->int_mask))) {
				/* If it came via the wrong interface, do not
				 * trust it.
				 */
				aifp = NULL;
			}
		}
#else
		aifp = iflookup(from.sin_addr.s_addr);
#endif
		if (sifp == NULL)
			sifp = aifp;

		input(&from, sifp, aifp, &inbuf.pbuf.rip, cc);
	}
}


/* Process a RIP packet
 */
static void
input(struct sockaddr_in *from,		/* received from this IP address */
      struct interface *sifp,		/* interface of incoming socket */
      struct interface *aifp,		/* "authenticated" interface */
      struct rip *rip,
      int cc)
{
#	define FROM_NADDR from->sin_addr.s_addr
	static struct msg_limit use_auth, bad_len, bad_mask;
	static struct msg_limit unk_router, bad_router, bad_nhop;

	struct rt_entry *rt;
	struct rt_spare new;
	struct netinfo *n, *lim;
	struct interface *ifp1;
	naddr gate, mask, v1_mask, dst, ddst_h = 0;
	struct auth *ap;
	struct tgate *tg = NULL;
	struct tgate_net *tn;
	int i, j;

	/* Notice when we hear from a remote gateway
	 */
	if (aifp != NULL
	    && (aifp->int_state & IS_REMOTE))
		aifp->int_act_time = now.tv_sec;

	trace_rip("Recv", "from", from, sifp, rip, cc);

	if (sifp == NULL) {
		trace_pkt("    discard a request from an indirect router"
		    " (possibly an attack)");
		return;
	}

	if (rip->rip_vers == 0) {
		msglim(&bad_router, FROM_NADDR,
		       "RIP version 0, cmd %d, packet received from %s",
		       rip->rip_cmd, naddr_ntoa(FROM_NADDR));
		return;
	} else if (rip->rip_vers > RIPv2) {
		rip->rip_vers = RIPv2;
	}
	if (cc > (int)OVER_MAXPACKETSIZE) {
		msglim(&bad_router, FROM_NADDR,
		       "packet at least %d bytes too long received from %s",
		       cc-MAXPACKETSIZE, naddr_ntoa(FROM_NADDR));
		return;
	}

	n = rip->rip_nets;
	lim = (struct netinfo *)((char*)rip + cc);

	/* Notice authentication.
	 * As required by section 4.2 in RFC 1723, discard authenticated
	 * RIPv2 messages, but only if configured for that silliness.
	 *
	 * RIPv2 authentication is lame.  Why authenticate queries?
	 * Why should a RIPv2 implementation with authentication disabled
	 * not be able to listen to RIPv2 packets with authentication, while
	 * RIPv1 systems will listen?  Crazy!
	 */
	if (!auth_ok
	    && rip->rip_vers == RIPv2
	    && n < lim && n->n_family == RIP_AF_AUTH) {
		msglim(&use_auth, FROM_NADDR,
		       "RIPv2 message with authentication from %s discarded",
		       naddr_ntoa(FROM_NADDR));
		return;
	}

	switch (rip->rip_cmd) {
	case RIPCMD_REQUEST:
		/* For mere requests, be a little sloppy about the source
		 */
		if (aifp == NULL)
			aifp = sifp;

		/* Are we talking to ourself or a remote gateway?
		 */
		ifp1 = ifwithaddr(FROM_NADDR, 0, 1);
		if (ifp1) {
			if (ifp1->int_state & IS_REMOTE) {
				/* remote gateway */
				aifp = ifp1;
				if (check_remote(aifp)) {
					aifp->int_act_time = now.tv_sec;
					(void)if_ok(aifp, "remote ");
				}
			} else if (from->sin_port == htons(RIP_PORT)) {
				trace_pkt("    discard our own RIP request");
				return;
			}
		}

		/* did the request come from a router?
		 */
		if (from->sin_port == htons(RIP_PORT)) {
			/* yes, ignore the request if RIP is off so that
			 * the router does not depend on us.
			 */
			if (rip_sock < 0
			    || (aifp != NULL
				&& IS_RIP_OUT_OFF(aifp->int_state))) {
				trace_pkt("    discard request while RIP off");
				return;
			}
		}

		/* According to RFC 1723, we should ignore unauthenticated
		 * queries.  That is too silly to bother with.  Sheesh!
		 * Are forwarding tables supposed to be secret, when
		 * a bad guy can infer them with test traffic?  When RIP
		 * is still the most common router-discovery protocol
		 * and so hosts need to send queries that will be answered?
		 * What about `rtquery`?
		 * Maybe on firewalls you'd care, but not enough to
		 * give up the diagnostic facilities of remote probing.
		 */

		if (n >= lim) {
			msglim(&bad_len, FROM_NADDR, "empty request from %s",
			       naddr_ntoa(FROM_NADDR));
			return;
		}
		if (cc%sizeof(*n) != sizeof(struct rip)%sizeof(*n)) {
			msglim(&bad_len, FROM_NADDR,
			       "request of bad length (%d) from %s",
			       cc, naddr_ntoa(FROM_NADDR));
		}

		if (rip->rip_vers == RIPv2
		    && (aifp == NULL || (aifp->int_state & IS_NO_RIPV1_OUT))) {
			v12buf.buf->rip_vers = RIPv2;
			/* If we have a secret but it is a cleartext secret,
			 * do not disclose our secret unless the other guy
			 * already knows it.
			 */
			ap = find_auth(aifp);
			if (ap != NULL && ap->type == RIP_AUTH_PW
			    && n->n_family == RIP_AF_AUTH
			    && !ck_passwd(aifp,rip,lim,FROM_NADDR,&use_auth))
				ap = NULL;
		} else {
			v12buf.buf->rip_vers = RIPv1;
			ap = NULL;
		}
		clr_ws_buf(&v12buf, ap);

		do {
			n->n_metric = ntohl(n->n_metric);

			/* A single entry with family RIP_AF_UNSPEC and
			 * metric HOPCNT_INFINITY means "all routes".
			 * We respond to routers only if we are acting
			 * as a supplier, or to anyone other than a router
			 * (i.e. a query).
			 */
			if (n->n_family == RIP_AF_UNSPEC
			    && n->n_metric == HOPCNT_INFINITY) {
				/* Answer a query from a utility program
				 * with all we know.
				 */
				if (aifp == NULL) {
					trace_pkt("ignore remote query");
					return;
				}
				if (from->sin_port != htons(RIP_PORT)) {
					/*
					 * insecure: query from non-router node
					 *   > 1: allow from distant node
					 *   > 0: allow from neighbor node
					 *  == 0: deny
					 */
					if ((aifp != NULL && insecure > 0) ||
					    (aifp == NULL && insecure > 1))
						supply(from, aifp, OUT_QUERY, 0,
						       rip->rip_vers,
						       ap != NULL);
					else
						trace_pkt("Warning: "
						    "possible attack detected");
					return;
				}

				/* A router trying to prime its tables.
				 * Filter the answer in the about same way
				 * broadcasts are filtered.
				 *
				 * Only answer a router if we are a supplier
				 * to keep an unwary host that is just starting
				 * from picking us as a router.
				 */
				if (aifp == NULL) {
					trace_pkt("ignore distant router");
					return;
				}
				if (!supplier
				    || IS_RIP_OFF(aifp->int_state)) {
					trace_pkt("ignore; not supplying");
					return;
				}

				/* Do not answer a RIPv1 router if
				 * we are sending RIPv2.  But do offer
				 * poor man's router discovery.
				 */
				if ((aifp->int_state & IS_NO_RIPV1_OUT)
				    && rip->rip_vers == RIPv1) {
					if (!(aifp->int_state & IS_PM_RDISC)) {
					    trace_pkt("ignore; sending RIPv2");
					    return;
					}

					v12buf.n->n_family = RIP_AF_INET;
					v12buf.n->n_dst = RIP_DEFAULT;
					i = aifp->int_d_metric;
					if (NULL != (rt = rtget(RIP_DEFAULT, 0))) {
					    j = (rt->rt_metric
						 +aifp->int_metric
						 +aifp->int_adj_outmetric
						 +1);
					    if (i > j)
						i = j;
					}
					v12buf.n->n_metric = htonl(i);
					v12buf.n++;
					break;
				}

				/* Respond with RIPv1 instead of RIPv2 if
				 * that is what we are broadcasting on the
				 * interface to keep the remote router from
				 * getting the wrong initial idea of the
				 * routes we send.
				 */
				supply(from, aifp, OUT_UNICAST, 0,
				       (aifp->int_state & IS_NO_RIPV1_OUT)
				       ? RIPv2 : RIPv1,
				       ap != NULL);
				return;
			}

			/* Ignore authentication */
			if (n->n_family == RIP_AF_AUTH)
				continue;

			if (n->n_family != RIP_AF_INET) {
				msglim(&bad_router, FROM_NADDR,
				       "request from %s for unsupported"
				       " (af %d) %s",
				       naddr_ntoa(FROM_NADDR),
				       ntohs(n->n_family),
				       naddr_ntoa(n->n_dst));
				return;
			}

			/* We are being asked about a specific destination.
			 */
			dst = n->n_dst;
			if (!check_dst(dst)) {
				msglim(&bad_router, FROM_NADDR,
				       "bad queried destination %s from %s",
				       naddr_ntoa(dst),
				       naddr_ntoa(FROM_NADDR));
				return;
			}

			/* decide what mask was intended */
			if (rip->rip_vers == RIPv1
			    || 0 == (mask = ntohl(n->n_mask))
			    || 0 != (ntohl(dst) & ~mask))
				mask = ripv1_mask_host(dst, aifp);

			/* try to find the answer */
			rt = rtget(dst, mask);
			if (!rt && dst != RIP_DEFAULT)
				rt = rtfind(n->n_dst);

			if (v12buf.buf->rip_vers != RIPv1)
				v12buf.n->n_mask = mask;
			if (rt == NULL) {
				/* we do not have the answer */
				v12buf.n->n_metric = HOPCNT_INFINITY;
			} else {
				/* we have the answer, so compute the
				 * right metric and next hop.
				 */
				v12buf.n->n_family = RIP_AF_INET;
				v12buf.n->n_dst = dst;
				j = rt->rt_metric+1;
				if (!aifp)
					++j;
				else
					j += (aifp->int_metric
					      + aifp->int_adj_outmetric);
				if (j < HOPCNT_INFINITY)
					v12buf.n->n_metric = j;
				else
					v12buf.n->n_metric = HOPCNT_INFINITY;
				if (v12buf.buf->rip_vers != RIPv1) {
					v12buf.n->n_tag = rt->rt_tag;
					v12buf.n->n_mask = mask;
					if (aifp != NULL
					    && on_net(rt->rt_gate,
						      aifp->int_net,
						      aifp->int_mask)
					    && rt->rt_gate != aifp->int_addr)
					    v12buf.n->n_nhop = rt->rt_gate;
				}
			}
			v12buf.n->n_metric = htonl(v12buf.n->n_metric);

			/* Stop paying attention if we fill the output buffer.
			 */
			if (++v12buf.n >= v12buf.lim)
				break;
		} while (++n < lim);

		/* Send the answer about specific routes.
		 */
		if (ap != NULL && ap->type == RIP_AUTH_MD5)
			end_md5_auth(&v12buf, ap);

		if (from->sin_port != htons(RIP_PORT)) {
			/* query */
			(void)output(OUT_QUERY, from, aifp,
				     v12buf.buf,
				     ((char *)v12buf.n - (char*)v12buf.buf));
		} else if (supplier) {
			(void)output(OUT_UNICAST, from, aifp,
				     v12buf.buf,
				     ((char *)v12buf.n - (char*)v12buf.buf));
		} else {
			/* Only answer a router if we are a supplier
			 * to keep an unwary host that is just starting
			 * from picking us an a router.
			 */
			;
		}
		return;

	case RIPCMD_TRACEON:
	case RIPCMD_TRACEOFF:
		/* Notice that trace messages are turned off for all possible
		 * abuse if _PATH_TRACE is undefined in pathnames.h.
		 * Notice also that because of the way the trace file is
		 * handled in trace.c, no abuse is plausible even if
		 * _PATH_TRACE_ is defined.
		 *
		 * First verify message came from a privileged port. */
		if (ntohs(from->sin_port) > IPPORT_RESERVED) {
			msglog("trace command from untrusted port on %s",
			       naddr_ntoa(FROM_NADDR));
			return;
		}
		if (aifp == NULL) {
			msglog("trace command from unknown router %s",
			       naddr_ntoa(FROM_NADDR));
			return;
		}
		if (rip->rip_cmd == RIPCMD_TRACEON) {
			rip->rip_tracefile[cc-4] = '\0';
			set_tracefile((char*)rip->rip_tracefile,
				      "trace command: %s\n", 0);
		} else {
			trace_off("tracing turned off by %s",
				  naddr_ntoa(FROM_NADDR));
		}
		return;

	case RIPCMD_RESPONSE:
		if (cc%sizeof(*n) != sizeof(struct rip)%sizeof(*n)) {
			msglim(&bad_len, FROM_NADDR,
			       "response of bad length (%d) from %s",
			       cc, naddr_ntoa(FROM_NADDR));
		}

		/* verify message came from a router */
		if (from->sin_port != ntohs(RIP_PORT)) {
			msglim(&bad_router, FROM_NADDR,
			       "    discard RIP response from unknown port"
			       " %d on %s",
			       ntohs(from->sin_port), naddr_ntoa(FROM_NADDR));
			return;
		}

		if (rip_sock < 0) {
			trace_pkt("    discard response while RIP off");
			return;
		}

		/* Are we talking to ourself or a remote gateway?
		 */
		ifp1 = ifwithaddr(FROM_NADDR, 0, 1);
		if (ifp1) {
			if (ifp1->int_state & IS_REMOTE) {
				/* remote gateway */
				aifp = ifp1;
				if (check_remote(aifp)) {
					aifp->int_act_time = now.tv_sec;
					(void)if_ok(aifp, "remote ");
				}
			} else {
				trace_pkt("    discard our own RIP response");
				return;
			}
		}

		/* Accept routing packets from routers directly connected
		 * via broadcast or point-to-point networks, and from
		 * those listed in /etc/gateways.
		 */
		if (aifp == NULL) {
			msglim(&unk_router, FROM_NADDR,
			       "   discard response from %s"
			       " via unexpected interface",
			       naddr_ntoa(FROM_NADDR));
			return;
		}
		if (IS_RIP_IN_OFF(aifp->int_state)) {
			trace_pkt("    discard RIPv%d response"
				  " via disabled interface %s",
				  rip->rip_vers, aifp->int_name);
			return;
		}

		if (n >= lim) {
			msglim(&bad_len, FROM_NADDR, "empty response from %s",
			       naddr_ntoa(FROM_NADDR));
			return;
		}

		if (((aifp->int_state & IS_NO_RIPV1_IN)
		     && rip->rip_vers == RIPv1)
		    || ((aifp->int_state & IS_NO_RIPV2_IN)
			&& rip->rip_vers != RIPv1)) {
			trace_pkt("    discard RIPv%d response",
				  rip->rip_vers);
			return;
		}

		/* Ignore routes via dead interface.
		 */
		if (aifp->int_state & IS_BROKE) {
			trace_pkt("discard response via broken interface %s",
				  aifp->int_name);
			return;
		}

		/* If the interface cares, ignore bad routers.
		 * Trace but do not log this problem, because where it
		 * happens, it happens frequently.
		 */
		if (aifp->int_state & IS_DISTRUST) {
			tg = tgates;
			while (tg->tgate_addr != FROM_NADDR) {
				tg = tg->tgate_next;
				if (tg == NULL) {
					trace_pkt("    discard RIP response"
						  " from untrusted router %s",
						  naddr_ntoa(FROM_NADDR));
					return;
				}
			}
		}

		/* Authenticate the packet if we have a secret.
		 * If we do not have any secrets, ignore the error in
		 * RFC 1723 and accept it regardless.
		 */
		if (aifp->int_auth[0].type != RIP_AUTH_NONE
		    && rip->rip_vers != RIPv1
		    && !ck_passwd(aifp,rip,lim,FROM_NADDR,&use_auth))
			return;

		do {
			if (n->n_family == RIP_AF_AUTH)
				continue;

			n->n_metric = ntohl(n->n_metric);
			dst = n->n_dst;
			if (n->n_family != RIP_AF_INET
			    && (n->n_family != RIP_AF_UNSPEC
				|| dst != RIP_DEFAULT)) {
				msglim(&bad_router, FROM_NADDR,
				       "route from %s to unsupported"
				       " address family=%d destination=%s",
				       naddr_ntoa(FROM_NADDR),
				       n->n_family,
				       naddr_ntoa(dst));
				continue;
			}
			if (!check_dst(dst)) {
				msglim(&bad_router, FROM_NADDR,
				       "bad destination %s from %s",
				       naddr_ntoa(dst),
				       naddr_ntoa(FROM_NADDR));
				return;
			}
			if (n->n_metric == 0
			    || n->n_metric > HOPCNT_INFINITY) {
				msglim(&bad_router, FROM_NADDR,
				       "bad metric %d from %s"
				       " for destination %s",
				       n->n_metric,
				       naddr_ntoa(FROM_NADDR),
				       naddr_ntoa(dst));
				return;
			}

			/* Notice the next-hop.
			 */
			gate = FROM_NADDR;
			if (n->n_nhop != 0) {
				if (rip->rip_vers == RIPv1) {
					n->n_nhop = 0;
				} else {
				    /* Use it only if it is valid. */
				    if (on_net(n->n_nhop,
					       aifp->int_net, aifp->int_mask)
					&& check_dst(n->n_nhop)) {
					    gate = n->n_nhop;
				    } else {
					    msglim(&bad_nhop, FROM_NADDR,
						   "router %s to %s"
						   " has bad next hop %s",
						   naddr_ntoa(FROM_NADDR),
						   naddr_ntoa(dst),
						   naddr_ntoa(n->n_nhop));
					    n->n_nhop = 0;
				    }
				}
			}

			if (rip->rip_vers == RIPv1
			    || 0 == (mask = ntohl(n->n_mask))) {
				mask = ripv1_mask_host(dst,aifp);
			} else if ((ntohl(dst) & ~mask) != 0) {
				msglim(&bad_mask, FROM_NADDR,
				       "router %s sent bad netmask"
				       " %#lx with %s",
				       naddr_ntoa(FROM_NADDR),
				       (u_long)mask,
				       naddr_ntoa(dst));
				continue;
			}
			if (rip->rip_vers == RIPv1)
				n->n_tag = 0;

			/* Adjust metric according to incoming interface..
			 */
			n->n_metric += (aifp->int_metric
					+ aifp->int_adj_inmetric);
			if (n->n_metric > HOPCNT_INFINITY)
				n->n_metric = HOPCNT_INFINITY;

			/* Should we trust this route from this router? */
			if (tg && (tn = tg->tgate_nets)->mask != 0) {
				for (i = 0; i < MAX_TGATE_NETS; i++, tn++) {
					if (on_net(dst, tn->net, tn->mask)
					    && tn->mask <= mask)
					    break;
				}
				if (i >= MAX_TGATE_NETS || tn->mask == 0) {
					trace_pkt("   ignored unauthorized %s",
						  addrname(dst,mask,0));
					continue;
				}
			}

			/* Recognize and ignore a default route we faked
			 * which is being sent back to us by a machine with
			 * broken split-horizon.
			 * Be a little more paranoid than that, and reject
			 * default routes with the same metric we advertised.
			 */
			if (aifp->int_d_metric != 0
			    && dst == RIP_DEFAULT
			    && (int)n->n_metric >= aifp->int_d_metric)
				continue;

			/* We can receive aggregated RIPv2 routes that must
			 * be broken down before they are transmitted by
			 * RIPv1 via an interface on a subnet.
			 * We might also receive the same routes aggregated
			 * via other RIPv2 interfaces.
			 * This could cause duplicate routes to be sent on
			 * the RIPv1 interfaces.  "Longest matching variable
			 * length netmasks" lets RIPv2 listeners understand,
			 * but breaking down the aggregated routes for RIPv1
			 * listeners can produce duplicate routes.
			 *
			 * Breaking down aggregated routes here bloats
			 * the daemon table, but does not hurt the kernel
			 * table, since routes are always aggregated for
			 * the kernel.
			 *
			 * Notice that this does not break down network
			 * routes corresponding to subnets.  This is part
			 * of the defense against RS_NET_SYN.
			 */
			if (have_ripv1_out
			    && (((rt = rtget(dst,mask)) == NULL
				 || !(rt->rt_state & RS_NET_SYN)))
			    && (v1_mask = ripv1_mask_net(dst,0)) > mask) {
				ddst_h = v1_mask & -v1_mask;
				i = (v1_mask & ~mask)/ddst_h;
				if (i >= 511) {
					/* Punt if we would have to generate
					 * an unreasonable number of routes.
					 */
					if (TRACECONTENTS)
					    trace_misc("accept %s-->%s as 1"
						       " instead of %d routes",
						       addrname(dst,mask,0),
						       naddr_ntoa(FROM_NADDR),
						       i+1);
					i = 0;
				} else {
					mask = v1_mask;
				}
			} else {
				i = 0;
			}

			new.rts_gate = gate;
			new.rts_router = FROM_NADDR;
			new.rts_metric = n->n_metric;
			new.rts_tag = n->n_tag;
			new.rts_time = now.tv_sec;
			new.rts_ifp = aifp;
			new.rts_de_ag = i;
			j = 0;
			for (;;) {
				input_route(dst, mask, &new, n);
				if (++j > i)
					break;
				dst = htonl(ntohl(dst) + ddst_h);
			}
		} while (++n < lim);
		break;
	}
#undef FROM_NADDR
}


/* Process a single input route.
 */
static void
input_route(naddr dst,			/* network order */
	    naddr mask,
	    struct rt_spare *new,
	    struct netinfo *n)
{
	int i;
	struct rt_entry *rt;
	struct rt_spare *rts, *rts0;
	struct interface *ifp1;


	/* See if the other guy is telling us to send our packets to him.
	 * Sometimes network routes arrive over a point-to-point link for
	 * the network containing the address(es) of the link.
	 *
	 * If our interface is broken, switch to using the other guy.
	 */
	ifp1 = ifwithaddr(dst, 1, 1);
	if (ifp1 != NULL
	    && (!(ifp1->int_state & IS_BROKE)
		|| (ifp1->int_state & IS_PASSIVE)))
		return;

	/* Look for the route in our table.
	 */
	rt = rtget(dst, mask);

	/* Consider adding the route if we do not already have it.
	 */
	if (rt == NULL) {
		/* Ignore unknown routes being poisoned.
		 */
		if (new->rts_metric == HOPCNT_INFINITY)
			return;

		/* Ignore the route if it points to us */
		if (n->n_nhop != 0
		    && ifwithaddr(n->n_nhop, 1, 0) != NULL)
			return;

		/* If something has not gone crazy and tried to fill
		 * our memory, accept the new route.
		 */
		if (total_routes < MAX_ROUTES)
			rtadd(dst, mask, 0, new);
		return;
	}

	/* We already know about the route.  Consider this update.
	 *
	 * If (rt->rt_state & RS_NET_SYN), then this route
	 * is the same as a network route we have inferred
	 * for subnets we know, in order to tell RIPv1 routers
	 * about the subnets.
	 *
	 * It is impossible to tell if the route is coming
	 * from a distant RIPv2 router with the standard
	 * netmask because that router knows about the entire
	 * network, or if it is a round-about echo of a
	 * synthetic, RIPv1 network route of our own.
	 * The worst is that both kinds of routes might be
	 * received, and the bad one might have the smaller
	 * metric.  Partly solve this problem by never
	 * aggregating into such a route.  Also keep it
	 * around as long as the interface exists.
	 */

	rts0 = rt->rt_spares;
	for (rts = rts0, i = NUM_SPARES; i != 0; i--, rts++) {
		if (rts->rts_router == new->rts_router)
			break;
		/* Note the worst slot to reuse,
		 * other than the current slot.
		 */
		if (rts0 == rt->rt_spares
		    || BETTER_LINK(rt, rts0, rts))
			rts0 = rts;
	}
	if (i != 0) {
		/* Found a route from the router already in the table.
		 */

		/* If the new route is a route broken down from an
		 * aggregated route, and if the previous route is either
		 * not a broken down route or was broken down from a finer
		 * netmask, and if the previous route is current,
		 * then forget this one.
		 */
		if (new->rts_de_ag > rts->rts_de_ag
		    && now_stale <= rts->rts_time)
			return;

		/* Keep poisoned routes around only long enough to pass
		 * the poison on.  Use a new timestamp for good routes.
		 */
		if (rts->rts_metric == HOPCNT_INFINITY
		    && new->rts_metric == HOPCNT_INFINITY)
			new->rts_time = rts->rts_time;

		/* If this is an update for the router we currently prefer,
		 * then note it.
		 */
		if (i == NUM_SPARES) {
			rtchange(rt, rt->rt_state, new, 0);
			/* If the route got worse, check for something better.
			 */
			if (new->rts_metric > rts->rts_metric)
				rtswitch(rt, 0);
			return;
		}

		/* This is an update for a spare route.
		 * Finished if the route is unchanged.
		 */
		if (rts->rts_gate == new->rts_gate
		    && rts->rts_metric == new->rts_metric
		    && rts->rts_tag == new->rts_tag) {
			trace_upslot(rt, rts, new);
			*rts = *new;
			return;
		}
		/* Forget it if it has gone bad.
		 */
		if (new->rts_metric == HOPCNT_INFINITY) {
			rts_delete(rt, rts);
			return;
		}

	} else {
		/* The update is for a route we know about,
		 * but not from a familiar router.
		 *
		 * Ignore the route if it points to us.
		 */
		if (n->n_nhop != 0
		    && NULL != ifwithaddr(n->n_nhop, 1, 0))
			return;

		/* the loop above set rts0=worst spare */
		rts = rts0;

		/* Save the route as a spare only if it has
		 * a better metric than our worst spare.
		 * This also ignores poisoned routes (those
		 * received with metric HOPCNT_INFINITY).
		 */
		if (new->rts_metric >= rts->rts_metric)
			return;
	}

	trace_upslot(rt, rts, new);
	*rts = *new;

	/* try to switch to a better route */
	rtswitch(rt, rts);
}


static int				/* 0 if bad */
ck_passwd(struct interface *aifp,
	  struct rip *rip,
	  void *lim,
	  naddr from,
	  struct msg_limit *use_authp)
{
#	define NA (rip->rip_auths)
	struct netauth *na2;
	struct auth *ap;
	MD5_CTX md5_ctx;
	u_char hash[RIP_AUTH_PW_LEN];
	int i, len;

	assert(aifp != NULL);
	if ((void *)NA >= lim || NA->a_family != RIP_AF_AUTH) {
		msglim(use_authp, from, "missing password from %s",
		       naddr_ntoa(from));
		return 0;
	}

	/* accept any current (+/- 24 hours) password
	 */
	for (ap = aifp->int_auth, i = 0; i < MAX_AUTH_KEYS; i++, ap++) {
		if (ap->type != NA->a_type
		    || (u_long)ap->start > (u_long)clk.tv_sec+DAY
		    || (u_long)ap->end+DAY < (u_long)clk.tv_sec)
			continue;

		if (NA->a_type == RIP_AUTH_PW) {
			if (!memcmp(NA->au.au_pw, ap->key, RIP_AUTH_PW_LEN))
				return 1;

		} else {
			/* accept MD5 secret with the right key ID
			 */
			if (NA->au.a_md5.md5_keyid != ap->keyid)
				continue;

			len = ntohs(NA->au.a_md5.md5_pkt_len);
			if ((len-sizeof(*rip)) % sizeof(*NA) != 0
			    || len != (char *)lim-(char*)rip-(int)sizeof(*NA)) {
				msglim(use_authp, from,
				       "wrong MD5 RIPv2 packet length of %d"
				       " instead of %d from %s",
				       len, (int)((char *)lim-(char *)rip
						  -sizeof(*NA)),
				       naddr_ntoa(from));
				return 0;
			}
			na2 = (struct netauth *)((char *)rip+len);

			/* Given a good hash value, these are not security
			 * problems so be generous and accept the routes,
			 * after complaining.
			 */
			if (TRACEPACKETS) {
				if (NA->au.a_md5.md5_auth_len
				    != RIP_AUTH_MD5_HASH_LEN)
					msglim(use_authp, from,
					       "unknown MD5 RIPv2 auth len %#x"
					       " instead of %#x from %s",
					       NA->au.a_md5.md5_auth_len,
					       (unsigned)RIP_AUTH_MD5_HASH_LEN,
					       naddr_ntoa(from));
				if (na2->a_family != RIP_AF_AUTH)
					msglim(use_authp, from,
					       "unknown MD5 RIPv2 family %#x"
					       " instead of %#x from %s",
					       na2->a_family, RIP_AF_AUTH,
					       naddr_ntoa(from));
				if (na2->a_type != ntohs(1))
					msglim(use_authp, from,
					       "MD5 RIPv2 hash has %#x"
					       " instead of %#x from %s",
					       na2->a_type, ntohs(1),
					       naddr_ntoa(from));
			}

			MD5Init(&md5_ctx);
			MD5Update(&md5_ctx, (u_char *)rip,
				  len + RIP_AUTH_MD5_HASH_XTRA);
			MD5Update(&md5_ctx, ap->key, RIP_AUTH_MD5_KEY_LEN);
			MD5Final(hash, &md5_ctx);
			if (!memcmp(hash, na2->au.au_pw, sizeof(hash)))
				return 1;
		}
	}

	msglim(use_authp, from, "bad password from %s",
	       naddr_ntoa(from));
	return 0;
#undef NA
}
