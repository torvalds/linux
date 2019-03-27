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

static struct rt_spare *rts_better(struct rt_entry *);
static struct rt_spare rts_empty = {0,0,0,HOPCNT_INFINITY,0,0,0};
static void  set_need_flash(void);
#ifdef _HAVE_SIN_LEN
static void masktrim(struct sockaddr_in *ap);
#else
static void masktrim(struct sockaddr_in_new *ap);
#endif
static void rtbad(struct rt_entry *);


struct radix_node_head *rhead;		/* root of the radix tree */

int	need_flash = 1;			/* flash update needed
					 * start =1 to suppress the 1st
					 */

struct timeval age_timer;		/* next check of old routes */
struct timeval need_kern = {		/* need to update kernel table */
	EPOCH+MIN_WAITTIME-1, 0
};

int	stopint;

int	total_routes;

/* zap any old routes through this gateway */
static naddr age_bad_gate;


/* It is desirable to "aggregate" routes, to combine differing routes of
 * the same metric and next hop into a common route with a smaller netmask
 * or to suppress redundant routes, routes that add no information to
 * routes with smaller netmasks.
 *
 * A route is redundant if and only if any and all routes with smaller
 * but matching netmasks and nets are the same.  Since routes are
 * kept sorted in the radix tree, redundant routes always come second.
 *
 * There are two kinds of aggregations.  First, two routes of the same bit
 * mask and differing only in the least significant bit of the network
 * number can be combined into a single route with a coarser mask.
 *
 * Second, a route can be suppressed in favor of another route with a more
 * coarse mask provided no incompatible routes with intermediate masks
 * are present.  The second kind of aggregation involves suppressing routes.
 * A route must not be suppressed if an incompatible route exists with
 * an intermediate mask, since the suppressed route would be covered
 * by the intermediate.
 *
 * This code relies on the radix tree walk encountering routes
 * sorted first by address, with the smallest address first.
 */

static struct ag_info ag_slots[NUM_AG_SLOTS], *ag_avail, *ag_corsest, *ag_finest;

/* #define DEBUG_AG */
#ifdef DEBUG_AG
#define CHECK_AG() {int acnt = 0; struct ag_info *cag;		\
	for (cag = ag_avail; cag != NULL; cag = cag->ag_fine)	\
		acnt++;						\
	for (cag = ag_corsest; cag != NULL; cag = cag->ag_fine)	\
		acnt++;						\
	if (acnt != NUM_AG_SLOTS) {				\
		(void)fflush(stderr);				\
		abort();					\
	}							\
}
#else
#define CHECK_AG()
#endif


/* Output the contents of an aggregation table slot.
 *	This function must always be immediately followed with the deletion
 *	of the target slot.
 */
static void
ag_out(struct ag_info *ag,
	 void (*out)(struct ag_info *))
{
	struct ag_info *ag_cors;
	naddr bit;


	/* Forget it if this route should not be output for split-horizon. */
	if (ag->ag_state & AGS_SPLIT_HZ)
		return;

	/* If we output both the even and odd twins, then the immediate parent,
	 * if it is present, is redundant, unless the parent manages to
	 * aggregate into something coarser.
	 * On successive calls, this code detects the even and odd twins,
	 * and marks the parent.
	 *
	 * Note that the order in which the radix tree code emits routes
	 * ensures that the twins are seen before the parent is emitted.
	 */
	ag_cors = ag->ag_cors;
	if (ag_cors != NULL
	    && ag_cors->ag_mask == ag->ag_mask<<1
	    && ag_cors->ag_dst_h == (ag->ag_dst_h & ag_cors->ag_mask)) {
		ag_cors->ag_state |= ((ag_cors->ag_dst_h == ag->ag_dst_h)
				      ? AGS_REDUN0
				      : AGS_REDUN1);
	}

	/* Skip it if this route is itself redundant.
	 *
	 * It is ok to change the contents of the slot here, since it is
	 * always deleted next.
	 */
	if (ag->ag_state & AGS_REDUN0) {
		if (ag->ag_state & AGS_REDUN1)
			return;		/* quit if fully redundant */
		/* make it finer if it is half-redundant */
		bit = (-ag->ag_mask) >> 1;
		ag->ag_dst_h |= bit;
		ag->ag_mask |= bit;

	} else if (ag->ag_state & AGS_REDUN1) {
		/* make it finer if it is half-redundant */
		bit = (-ag->ag_mask) >> 1;
		ag->ag_mask |= bit;
	}
	out(ag);
}


static void
ag_del(struct ag_info *ag)
{
	CHECK_AG();

	if (ag->ag_cors == NULL)
		ag_corsest = ag->ag_fine;
	else
		ag->ag_cors->ag_fine = ag->ag_fine;

	if (ag->ag_fine == NULL)
		ag_finest = ag->ag_cors;
	else
		ag->ag_fine->ag_cors = ag->ag_cors;

	ag->ag_fine = ag_avail;
	ag_avail = ag;

	CHECK_AG();
}


/* Flush routes waiting for aggregation.
 *	This must not suppress a route unless it is known that among all
 *	routes with coarser masks that match it, the one with the longest
 *	mask is appropriate.  This is ensured by scanning the routes
 *	in lexical order, and with the most restrictive mask first
 *	among routes to the same destination.
 */
void
ag_flush(naddr lim_dst_h,		/* flush routes to here */
	 naddr lim_mask,		/* matching this mask */
	 void (*out)(struct ag_info *))
{
	struct ag_info *ag, *ag_cors;
	naddr dst_h;


	for (ag = ag_finest;
	     ag != NULL && ag->ag_mask >= lim_mask;
	     ag = ag_cors) {
		ag_cors = ag->ag_cors;

		/* work on only the specified routes */
		dst_h = ag->ag_dst_h;
		if ((dst_h & lim_mask) != lim_dst_h)
			continue;

		if (!(ag->ag_state & AGS_SUPPRESS))
			ag_out(ag, out);

		else for ( ; ; ag_cors = ag_cors->ag_cors) {
			/* Look for a route that can suppress the
			 * current route */
			if (ag_cors == NULL) {
				/* failed, so output it and look for
				 * another route to work on
				 */
				ag_out(ag, out);
				break;
			}

			if ((dst_h & ag_cors->ag_mask) == ag_cors->ag_dst_h) {
				/* We found a route with a coarser mask that
				 * aggregates the current target.
				 *
				 * If it has a different next hop, it
				 * cannot replace the target, so output
				 * the target.
				 */
				if (ag->ag_gate != ag_cors->ag_gate
				    && !(ag->ag_state & AGS_FINE_GATE)
				    && !(ag_cors->ag_state & AGS_CORS_GATE)) {
					ag_out(ag, out);
					break;
				}

				/* If the coarse route has a good enough
				 * metric, it suppresses the target.
				 * If the suppressed target was redundant,
				 * then mark the suppressor redundant.
				 */
				if (ag_cors->ag_pref <= ag->ag_pref) {
				    if (AG_IS_REDUN(ag->ag_state)
					&& ag_cors->ag_mask==ag->ag_mask<<1) {
					if (ag_cors->ag_dst_h == dst_h)
					    ag_cors->ag_state |= AGS_REDUN0;
					else
					    ag_cors->ag_state |= AGS_REDUN1;
				    }
				    if (ag->ag_tag != ag_cors->ag_tag)
					    ag_cors->ag_tag = 0;
				    if (ag->ag_nhop != ag_cors->ag_nhop)
					    ag_cors->ag_nhop = 0;
				    break;
				}
			}
		}

		/* That route has either been output or suppressed */
		ag_cors = ag->ag_cors;
		ag_del(ag);
	}

	CHECK_AG();
}


/* Try to aggregate a route with previous routes.
 */
void
ag_check(naddr	dst,
	 naddr	mask,
	 naddr	gate,
	 naddr	nhop,
	 char	metric,
	 char	pref,
	 u_int	new_seqno,
	 u_short tag,
	 u_short state,
	 void (*out)(struct ag_info *))	/* output using this */
{
	struct ag_info *ag, *nag, *ag_cors;
	naddr xaddr;
	int x;

	dst = ntohl(dst);

	/* Punt non-contiguous subnet masks.
	 *
	 * (X & -X) contains a single bit if and only if X is a power of 2.
	 * (X + (X & -X)) == 0 if and only if X is a power of 2.
	 */
	if ((mask & -mask) + mask != 0) {
		struct ag_info nc_ag;

		nc_ag.ag_dst_h = dst;
		nc_ag.ag_mask = mask;
		nc_ag.ag_gate = gate;
		nc_ag.ag_nhop = nhop;
		nc_ag.ag_metric = metric;
		nc_ag.ag_pref = pref;
		nc_ag.ag_tag = tag;
		nc_ag.ag_state = state;
		nc_ag.ag_seqno = new_seqno;
		out(&nc_ag);
		return;
	}

	/* Search for the right slot in the aggregation table.
	 */
	ag_cors = NULL;
	ag = ag_corsest;
	while (ag != NULL) {
		if (ag->ag_mask >= mask)
			break;

		/* Suppress old routes (i.e. combine with compatible routes
		 * with coarser masks) as we look for the right slot in the
		 * aggregation table for the new route.
		 * A route to an address less than the current destination
		 * will not be affected by the current route or any route
		 * seen hereafter.  That means it is safe to suppress it.
		 * This check keeps poor routes (e.g. with large hop counts)
		 * from preventing suppression of finer routes.
		 */
		if (ag_cors != NULL
		    && ag->ag_dst_h < dst
		    && (ag->ag_state & AGS_SUPPRESS)
		    && ag_cors->ag_pref <= ag->ag_pref
		    && (ag->ag_dst_h & ag_cors->ag_mask) == ag_cors->ag_dst_h
		    && (ag_cors->ag_gate == ag->ag_gate
			|| (ag->ag_state & AGS_FINE_GATE)
			|| (ag_cors->ag_state & AGS_CORS_GATE))) {
			/*  If the suppressed target was redundant,
			 * then mark the suppressor redundant.
			 */
			if (AG_IS_REDUN(ag->ag_state)
			    && ag_cors->ag_mask == ag->ag_mask<<1) {
				if (ag_cors->ag_dst_h == dst)
					ag_cors->ag_state |= AGS_REDUN0;
				else
					ag_cors->ag_state |= AGS_REDUN1;
			}
			if (ag->ag_tag != ag_cors->ag_tag)
				ag_cors->ag_tag = 0;
			if (ag->ag_nhop != ag_cors->ag_nhop)
				ag_cors->ag_nhop = 0;
			ag_del(ag);
			CHECK_AG();
		} else {
			ag_cors = ag;
		}
		ag = ag_cors->ag_fine;
	}

	/* If we find the even/odd twin of the new route, and if the
	 * masks and so forth are equal, we can aggregate them.
	 * We can probably promote one of the pair.
	 *
	 * Since the routes are encountered in lexical order,
	 * the new route must be odd.  However, the second or later
	 * times around this loop, it could be the even twin promoted
	 * from the even/odd pair of twins of the finer route.
	 */
	while (ag != NULL
	       && ag->ag_mask == mask
	       && ((ag->ag_dst_h ^ dst) & (mask<<1)) == 0) {

		/* Here we know the target route and the route in the current
		 * slot have the same netmasks and differ by at most the
		 * last bit.  They are either for the same destination, or
		 * for an even/odd pair of destinations.
		 */
		if (ag->ag_dst_h == dst) {
			/* We have two routes to the same destination.
			 * Routes are encountered in lexical order, so a
			 * route is never promoted until the parent route is
			 * already present.  So we know that the new route is
			 * a promoted (or aggregated) pair and the route
			 * already in the slot is the explicit route.
			 *
			 * Prefer the best route if their metrics differ,
			 * or the aggregated one if not, following a sort
			 * of longest-match rule.
			 */
			if (pref <= ag->ag_pref) {
				ag->ag_gate = gate;
				ag->ag_nhop = nhop;
				ag->ag_tag = tag;
				ag->ag_metric = metric;
				ag->ag_pref = pref;
				if (ag->ag_seqno < new_seqno)
					ag->ag_seqno = new_seqno;
				x = ag->ag_state;
				ag->ag_state = state;
				state = x;
			}

			/* Some bits are set if they are set on either route,
			 * except when the route is for an interface.
			 */
			if (!(ag->ag_state & AGS_IF))
				ag->ag_state |= (state & (AGS_AGGREGATE_EITHER
							| AGS_REDUN0
							| AGS_REDUN1));
			return;
		}

		/* If one of the routes can be promoted and the other can
		 * be suppressed, it may be possible to combine them or
		 * worthwhile to promote one.
		 *
		 * Any route that can be promoted is always
		 * marked to be eligible to be suppressed.
		 */
		if (!((state & AGS_AGGREGATE)
		      && (ag->ag_state & AGS_SUPPRESS))
		    && !((ag->ag_state & AGS_AGGREGATE)
			 && (state & AGS_SUPPRESS)))
			break;

		/* A pair of even/odd twin routes can be combined
		 * if either is redundant, or if they are via the
		 * same gateway and have the same metric.
		 */
		if (AG_IS_REDUN(ag->ag_state)
		    || AG_IS_REDUN(state)
		    || (ag->ag_gate == gate
			&& ag->ag_pref == pref
			&& (state & ag->ag_state & AGS_AGGREGATE) != 0)) {

			/* We have both the even and odd pairs.
			 * Since the routes are encountered in order,
			 * the route in the slot must be the even twin.
			 *
			 * Combine and promote (aggregate) the pair of routes.
			 */
			if (new_seqno < ag->ag_seqno)
				new_seqno = ag->ag_seqno;
			if (!AG_IS_REDUN(state))
				state &= ~AGS_REDUN1;
			if (AG_IS_REDUN(ag->ag_state))
				state |= AGS_REDUN0;
			else
				state &= ~AGS_REDUN0;
			state |= (ag->ag_state & AGS_AGGREGATE_EITHER);
			if (ag->ag_tag != tag)
				tag = 0;
			if (ag->ag_nhop != nhop)
				nhop = 0;

			/* Get rid of the even twin that was already
			 * in the slot.
			 */
			ag_del(ag);

		} else if (ag->ag_pref >= pref
			   && (ag->ag_state & AGS_AGGREGATE)) {
			/* If we cannot combine the pair, maybe the route
			 * with the worse metric can be promoted.
			 *
			 * Promote the old, even twin, by giving its slot
			 * in the table to the new, odd twin.
			 */
			ag->ag_dst_h = dst;

			xaddr = ag->ag_gate;
			ag->ag_gate = gate;
			gate = xaddr;

			xaddr = ag->ag_nhop;
			ag->ag_nhop = nhop;
			nhop = xaddr;

			x = ag->ag_tag;
			ag->ag_tag = tag;
			tag = x;

			/* The promoted route is even-redundant only if the
			 * even twin was fully redundant.  It is not
			 * odd-redundant because the odd-twin will still be
			 * in the table.
			 */
			x = ag->ag_state;
			if (!AG_IS_REDUN(x))
				x &= ~AGS_REDUN0;
			x &= ~AGS_REDUN1;
			ag->ag_state = state;
			state = x;

			x = ag->ag_metric;
			ag->ag_metric = metric;
			metric = x;

			x = ag->ag_pref;
			ag->ag_pref = pref;
			pref = x;

			/* take the newest sequence number */
			if (new_seqno <= ag->ag_seqno)
				new_seqno = ag->ag_seqno;
			else
				ag->ag_seqno = new_seqno;

		} else {
			if (!(state & AGS_AGGREGATE))
				break;	/* cannot promote either twin */

			/* Promote the new, odd twin by shaving its
			 * mask and address.
			 * The promoted route is odd-redundant only if the
			 * odd twin was fully redundant.  It is not
			 * even-redundant because the even twin is still in
			 * the table.
			 */
			if (!AG_IS_REDUN(state))
				state &= ~AGS_REDUN1;
			state &= ~AGS_REDUN0;
			if (new_seqno < ag->ag_seqno)
				new_seqno = ag->ag_seqno;
			else
				ag->ag_seqno = new_seqno;
		}

		mask <<= 1;
		dst &= mask;

		if (ag_cors == NULL) {
			ag = ag_corsest;
			break;
		}
		ag = ag_cors;
		ag_cors = ag->ag_cors;
	}

	/* When we can no longer promote and combine routes,
	 * flush the old route in the target slot.  Also flush
	 * any finer routes that we know will never be aggregated by
	 * the new route.
	 *
	 * In case we moved toward coarser masks,
	 * get back where we belong
	 */
	if (ag != NULL
	    && ag->ag_mask < mask) {
		ag_cors = ag;
		ag = ag->ag_fine;
	}

	/* Empty the target slot
	 */
	if (ag != NULL && ag->ag_mask == mask) {
		ag_flush(ag->ag_dst_h, ag->ag_mask, out);
		ag = (ag_cors == NULL) ? ag_corsest : ag_cors->ag_fine;
	}

#ifdef DEBUG_AG
	(void)fflush(stderr);
	if (ag == NULL && ag_cors != ag_finest)
		abort();
	if (ag_cors == NULL && ag != ag_corsest)
		abort();
	if (ag != NULL && ag->ag_cors != ag_cors)
		abort();
	if (ag_cors != NULL && ag_cors->ag_fine != ag)
		abort();
	CHECK_AG();
#endif

	/* Save the new route on the end of the table.
	 */
	nag = ag_avail;
	ag_avail = nag->ag_fine;

	nag->ag_dst_h = dst;
	nag->ag_mask = mask;
	nag->ag_gate = gate;
	nag->ag_nhop = nhop;
	nag->ag_metric = metric;
	nag->ag_pref = pref;
	nag->ag_tag = tag;
	nag->ag_state = state;
	nag->ag_seqno = new_seqno;

	nag->ag_fine = ag;
	if (ag != NULL)
		ag->ag_cors = nag;
	else
		ag_finest = nag;
	nag->ag_cors = ag_cors;
	if (ag_cors == NULL)
		ag_corsest = nag;
	else
		ag_cors->ag_fine = nag;
	CHECK_AG();
}

static const char *
rtm_type_name(u_char type)
{
	static const char * const rtm_types[] = {
		"RTM_ADD",
		"RTM_DELETE",
		"RTM_CHANGE",
		"RTM_GET",
		"RTM_LOSING",
		"RTM_REDIRECT",
		"RTM_MISS",
		"RTM_LOCK",
		"RTM_OLDADD",
		"RTM_OLDDEL",
		"RTM_RESOLVE",
		"RTM_NEWADDR",
		"RTM_DELADDR",
#ifdef RTM_OIFINFO
		"RTM_OIFINFO",
#endif
		"RTM_IFINFO",
		"RTM_NEWMADDR",
		"RTM_DELMADDR"
	};
#define NEW_RTM_PAT "RTM type %#x"
	static char name0[sizeof(NEW_RTM_PAT)+2];


	if (type > sizeof(rtm_types)/sizeof(rtm_types[0])
	    || type == 0) {
		snprintf(name0, sizeof(name0), NEW_RTM_PAT, type);
		return name0;
	} else {
		return rtm_types[type-1];
	}
#undef NEW_RTM_PAT
}


/* Trim a mask in a sockaddr
 *	Produce a length of 0 for an address of 0.
 *	Otherwise produce the index of the first zero byte.
 */
void
#ifdef _HAVE_SIN_LEN
masktrim(struct sockaddr_in *ap)
#else
masktrim(struct sockaddr_in_new *ap)
#endif
{
	char *cp;

	if (ap->sin_addr.s_addr == 0) {
		ap->sin_len = 0;
		return;
	}
	cp = (char *)(&ap->sin_addr.s_addr+1);
	while (*--cp == 0)
		continue;
	ap->sin_len = cp - (char*)ap + 1;
}


/* Tell the kernel to add, delete or change a route
 */
static void
rtioctl(int action,			/* RTM_DELETE, etc */
	naddr dst,
	naddr gate,
	naddr mask,
	int metric,
	int flags)
{
	struct {
		struct rt_msghdr w_rtm;
		struct sockaddr_in w_dst;
		struct sockaddr_in w_gate;
#ifdef _HAVE_SA_LEN
		struct sockaddr_in w_mask;
#else
		struct sockaddr_in_new w_mask;
#endif
	} w;
	long cc;
#   define PAT " %-10s %s metric=%d flags=%#x"
#   define ARGS rtm_type_name(action), rtname(dst,mask,gate), metric, flags

again:
	memset(&w, 0, sizeof(w));
	w.w_rtm.rtm_msglen = sizeof(w);
	w.w_rtm.rtm_version = RTM_VERSION;
	w.w_rtm.rtm_type = action;
	w.w_rtm.rtm_flags = flags;
	w.w_rtm.rtm_seq = ++rt_sock_seqno;
	w.w_rtm.rtm_addrs = RTA_DST|RTA_GATEWAY;
	if (metric != 0 || action == RTM_CHANGE) {
		w.w_rtm.rtm_rmx.rmx_hopcount = metric;
		w.w_rtm.rtm_inits |= RTV_HOPCOUNT;
	}
	w.w_dst.sin_family = AF_INET;
	w.w_dst.sin_addr.s_addr = dst;
	w.w_gate.sin_family = AF_INET;
	w.w_gate.sin_addr.s_addr = gate;
#ifdef _HAVE_SA_LEN
	w.w_dst.sin_len = sizeof(w.w_dst);
	w.w_gate.sin_len = sizeof(w.w_gate);
#endif
	if (mask == HOST_MASK) {
		w.w_rtm.rtm_flags |= RTF_HOST;
		w.w_rtm.rtm_msglen -= sizeof(w.w_mask);
	} else {
		w.w_rtm.rtm_addrs |= RTA_NETMASK;
		w.w_mask.sin_addr.s_addr = htonl(mask);
#ifdef _HAVE_SA_LEN
		masktrim(&w.w_mask);
		if (w.w_mask.sin_len == 0)
			w.w_mask.sin_len = sizeof(long);
		w.w_rtm.rtm_msglen -= (sizeof(w.w_mask) - w.w_mask.sin_len);
#endif
	}

#ifndef NO_INSTALL
	cc = write(rt_sock, &w, w.w_rtm.rtm_msglen);
	if (cc < 0) {
		if (errno == ESRCH
		    && (action == RTM_CHANGE || action == RTM_DELETE)) {
			trace_act("route disappeared before" PAT, ARGS);
			if (action == RTM_CHANGE) {
				action = RTM_ADD;
				goto again;
			}
			return;
		}
		msglog("write(rt_sock)" PAT ": %s", ARGS, strerror(errno));
		return;
	} else if (cc != w.w_rtm.rtm_msglen) {
		msglog("write(rt_sock) wrote %ld instead of %d for" PAT,
		       cc, w.w_rtm.rtm_msglen, ARGS);
		return;
	}
#endif
	if (TRACEKERNEL)
		trace_misc("write kernel" PAT, ARGS);
#undef PAT
#undef ARGS
}


#define KHASH_SIZE 71			/* should be prime */
#define KHASH(a,m) khash_bins[((a) ^ (m)) % KHASH_SIZE]
static struct khash {
	struct khash *k_next;
	naddr	k_dst;
	naddr	k_mask;
	naddr	k_gate;
	short	k_metric;
	u_short	k_state;
#define	    KS_NEW	0x001
#define	    KS_DELETE	0x002		/* need to delete the route */
#define	    KS_ADD	0x004		/* add to the kernel */
#define	    KS_CHANGE	0x008		/* tell kernel to change the route */
#define	    KS_DEL_ADD	0x010		/* delete & add to change the kernel */
#define	    KS_STATIC	0x020		/* Static flag in kernel */
#define	    KS_GATEWAY	0x040		/* G flag in kernel */
#define	    KS_DYNAMIC	0x080		/* result of redirect */
#define	    KS_DELETED	0x100		/* already deleted from kernel */
#define	    KS_CHECK	0x200
	time_t	k_keep;
#define	    K_KEEP_LIM	30
	time_t	k_redirect_time;	/* when redirected route 1st seen */
} *khash_bins[KHASH_SIZE];


static struct khash*
kern_find(naddr dst, naddr mask, struct khash ***ppk)
{
	struct khash *k, **pk;

	for (pk = &KHASH(dst,mask); (k = *pk) != NULL; pk = &k->k_next) {
		if (k->k_dst == dst && k->k_mask == mask)
			break;
	}
	if (ppk != NULL)
		*ppk = pk;
	return k;
}


static struct khash*
kern_add(naddr dst, naddr mask)
{
	struct khash *k, **pk;

	k = kern_find(dst, mask, &pk);
	if (k != NULL)
		return k;

	k = (struct khash *)rtmalloc(sizeof(*k), "kern_add");

	memset(k, 0, sizeof(*k));
	k->k_dst = dst;
	k->k_mask = mask;
	k->k_state = KS_NEW;
	k->k_keep = now.tv_sec;
	*pk = k;

	return k;
}


/* If a kernel route has a non-zero metric, check that it is still in the
 *	daemon table, and not deleted by interfaces coming and going.
 */
static void
kern_check_static(struct khash *k,
		  struct interface *ifp)
{
	struct rt_entry *rt;
	struct rt_spare new;

	if (k->k_metric == 0)
		return;

	memset(&new, 0, sizeof(new));
	new.rts_ifp = ifp;
	new.rts_gate = k->k_gate;
	new.rts_router = (ifp != NULL) ? ifp->int_addr : loopaddr;
	new.rts_metric = k->k_metric;
	new.rts_time = now.tv_sec;

	rt = rtget(k->k_dst, k->k_mask);
	if (rt != NULL) {
		if (!(rt->rt_state & RS_STATIC))
			rtchange(rt, rt->rt_state | RS_STATIC, &new, 0);
	} else {
		rtadd(k->k_dst, k->k_mask, RS_STATIC, &new);
	}
}


/* operate on a kernel entry
 */
static void
kern_ioctl(struct khash *k,
	   int action,			/* RTM_DELETE, etc */
	   int flags)

{
	switch (action) {
	case RTM_DELETE:
		k->k_state &= ~KS_DYNAMIC;
		if (k->k_state & KS_DELETED)
			return;
		k->k_state |= KS_DELETED;
		break;
	case RTM_ADD:
		k->k_state &= ~KS_DELETED;
		break;
	case RTM_CHANGE:
		if (k->k_state & KS_DELETED) {
			action = RTM_ADD;
			k->k_state &= ~KS_DELETED;
		}
		break;
	}

	rtioctl(action, k->k_dst, k->k_gate, k->k_mask, k->k_metric, flags);
}


/* add a route the kernel told us
 */
static void
rtm_add(struct rt_msghdr *rtm,
	struct rt_addrinfo *info,
	time_t keep)
{
	struct khash *k;
	struct interface *ifp;
	naddr mask;


	if (rtm->rtm_flags & RTF_HOST) {
		mask = HOST_MASK;
	} else if (INFO_MASK(info) != 0) {
		mask = ntohl(S_ADDR(INFO_MASK(info)));
	} else {
		msglog("ignore %s without mask", rtm_type_name(rtm->rtm_type));
		return;
	}

	k = kern_add(S_ADDR(INFO_DST(info)), mask);
	if (k->k_state & KS_NEW)
		k->k_keep = now.tv_sec+keep;
	if (INFO_GATE(info) == 0) {
		trace_act("note %s without gateway",
			  rtm_type_name(rtm->rtm_type));
		k->k_metric = HOPCNT_INFINITY;
	} else if (INFO_GATE(info)->sa_family != AF_INET) {
		trace_act("note %s with gateway AF=%d",
			  rtm_type_name(rtm->rtm_type),
			  INFO_GATE(info)->sa_family);
		k->k_metric = HOPCNT_INFINITY;
	} else {
		k->k_gate = S_ADDR(INFO_GATE(info));
		k->k_metric = rtm->rtm_rmx.rmx_hopcount;
		if (k->k_metric < 0)
			k->k_metric = 0;
		else if (k->k_metric > HOPCNT_INFINITY-1)
			k->k_metric = HOPCNT_INFINITY-1;
	}
	k->k_state &= ~(KS_DELETE | KS_ADD | KS_CHANGE | KS_DEL_ADD
			| KS_DELETED | KS_GATEWAY | KS_STATIC
			| KS_NEW | KS_CHECK);
	if (rtm->rtm_flags & RTF_GATEWAY)
		k->k_state |= KS_GATEWAY;
	if (rtm->rtm_flags & RTF_STATIC)
		k->k_state |= KS_STATIC;

	if (0 != (rtm->rtm_flags & (RTF_DYNAMIC | RTF_MODIFIED))) {
		if (INFO_AUTHOR(info) != 0
		    && INFO_AUTHOR(info)->sa_family == AF_INET)
			ifp = iflookup(S_ADDR(INFO_AUTHOR(info)));
		else
			ifp = NULL;
		if (supplier
		    && (ifp == NULL || !(ifp->int_state & IS_REDIRECT_OK))) {
			/* Routers are not supposed to listen to redirects,
			 * so delete it if it came via an unknown interface
			 * or the interface does not have special permission.
			 */
			k->k_state &= ~KS_DYNAMIC;
			k->k_state |= KS_DELETE;
			LIM_SEC(need_kern, 0);
			trace_act("mark for deletion redirected %s --> %s"
				  " via %s",
				  addrname(k->k_dst, k->k_mask, 0),
				  naddr_ntoa(k->k_gate),
				  ifp ? ifp->int_name : "unknown interface");
		} else {
			k->k_state |= KS_DYNAMIC;
			k->k_redirect_time = now.tv_sec;
			trace_act("accept redirected %s --> %s via %s",
				  addrname(k->k_dst, k->k_mask, 0),
				  naddr_ntoa(k->k_gate),
				  ifp ? ifp->int_name : "unknown interface");
		}
		return;
	}

	/* If it is not a static route, quit until the next comparison
	 * between the kernel and daemon tables, when it will be deleted.
	 */
	if (!(k->k_state & KS_STATIC)) {
		k->k_state |= KS_DELETE;
		LIM_SEC(need_kern, k->k_keep);
		return;
	}

	/* Put static routes with real metrics into the daemon table so
	 * they can be advertised.
	 *
	 * Find the interface toward the gateway.
	 */
	ifp = iflookup(k->k_gate);
	if (ifp == NULL)
		msglog("static route %s --> %s impossibly lacks ifp",
		       addrname(S_ADDR(INFO_DST(info)), mask, 0),
		       naddr_ntoa(k->k_gate));

	kern_check_static(k, ifp);
}


/* deal with packet loss
 */
static void
rtm_lose(struct rt_msghdr *rtm,
	 struct rt_addrinfo *info)
{
	if (INFO_GATE(info) == 0
	    || INFO_GATE(info)->sa_family != AF_INET) {
		trace_act("ignore %s without gateway",
			  rtm_type_name(rtm->rtm_type));
		return;
	}

	if (rdisc_ok)
		rdisc_age(S_ADDR(INFO_GATE(info)));
	age(S_ADDR(INFO_GATE(info)));
}


/* Make the gateway slot of an info structure point to something
 * useful.  If it is not already useful, but it specifies an interface,
 * then fill in the sockaddr_in provided and point it there.
 */
static int
get_info_gate(struct sockaddr **sap,
	      struct sockaddr_in *rsin)
{
	struct sockaddr_dl *sdl = (struct sockaddr_dl *)*sap;
	struct interface *ifp;

	if (sdl == NULL)
		return 0;
	if ((sdl)->sdl_family == AF_INET)
		return 1;
	if ((sdl)->sdl_family != AF_LINK)
		return 0;

	ifp = ifwithindex(sdl->sdl_index, 1);
	if (ifp == NULL)
		return 0;

	rsin->sin_addr.s_addr = ifp->int_addr;
#ifdef _HAVE_SA_LEN
	rsin->sin_len = sizeof(*rsin);
#endif
	rsin->sin_family = AF_INET;
	*sap = (struct sockaddr*)rsin;

	return 1;
}


/* Clean the kernel table by copying it to the daemon image.
 * Eventually the daemon will delete any extra routes.
 */
void
flush_kern(void)
{
	static char *sysctl_buf;
	static size_t sysctl_buf_size = 0;
	size_t needed;
	int mib[6];
	char *next, *lim;
	struct rt_msghdr *rtm;
	struct sockaddr_in gate_sin;
	struct rt_addrinfo info;
	int i;
	struct khash *k;


	for (i = 0; i < KHASH_SIZE; i++) {
		for (k = khash_bins[i]; k != NULL; k = k->k_next) {
			k->k_state |= KS_CHECK;
		}
	}

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;		/* protocol */
	mib[3] = 0;		/* wildcard address family */
	mib[4] = NET_RT_DUMP;
	mib[5] = 0;		/* no flags */
	for (;;) {
		if ((needed = sysctl_buf_size) != 0) {
			if (sysctl(mib, 6, sysctl_buf,&needed, 0, 0) >= 0)
				break;
			if (errno != ENOMEM && errno != EFAULT)
				BADERR(1,"flush_kern: sysctl(RT_DUMP)");
			free(sysctl_buf);
			needed = 0;
		}
		if (sysctl(mib, 6, 0, &needed, 0, 0) < 0)
			BADERR(1,"flush_kern: sysctl(RT_DUMP) estimate");
		/* Kludge around the habit of some systems, such as
		 * BSD/OS 3.1, to not admit how many routes are in the
		 * kernel, or at least to be quite wrong.
		 */
		needed += 50*(sizeof(*rtm)+5*sizeof(struct sockaddr));
		sysctl_buf = rtmalloc(sysctl_buf_size = needed,
				      "flush_kern sysctl(RT_DUMP)");
	}

	lim = sysctl_buf + needed;
	for (next = sysctl_buf; next < lim; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)next;
		if (rtm->rtm_msglen == 0) {
			msglog("zero length kernel route at "
			       " %#lx in buffer %#lx before %#lx",
			       (u_long)rtm, (u_long)sysctl_buf, (u_long)lim);
			break;
		}

		rt_xaddrs(&info,
			  (struct sockaddr *)(rtm+1),
			  (struct sockaddr *)(next + rtm->rtm_msglen),
			  rtm->rtm_addrs);

		if (INFO_DST(&info) == 0
		    || INFO_DST(&info)->sa_family != AF_INET)
			continue;

#if defined (RTF_LLINFO)		
		/* ignore ARP table entries on systems with a merged route
		 * and ARP table.
		 */
		if (rtm->rtm_flags & RTF_LLINFO)
			continue;
#endif
#if defined(RTF_WASCLONED) && defined(__FreeBSD__)
		/* ignore cloned routes
		 */
		if (rtm->rtm_flags & RTF_WASCLONED)
			continue;
#endif

		/* ignore multicast addresses
		 */
		if (IN_MULTICAST(ntohl(S_ADDR(INFO_DST(&info)))))
			continue;

		if (!get_info_gate(&INFO_GATE(&info), &gate_sin))
			continue;

		/* Note static routes and interface routes, and also
		 * preload the image of the kernel table so that
		 * we can later clean it, as well as avoid making
		 * unneeded changes.  Keep the old kernel routes for a
		 * few seconds to allow a RIP or router-discovery
		 * response to be heard.
		 */
		rtm_add(rtm,&info,MIN_WAITTIME);
	}

	for (i = 0; i < KHASH_SIZE; i++) {
		for (k = khash_bins[i]; k != NULL; k = k->k_next) {
			if (k->k_state & KS_CHECK) {
				msglog("%s --> %s disappeared from kernel",
				       addrname(k->k_dst, k->k_mask, 0),
				       naddr_ntoa(k->k_gate));
				del_static(k->k_dst, k->k_mask, k->k_gate, 1);
			}
		}
	}
}


/* Listen to announcements from the kernel
 */
void
read_rt(void)
{
	long cc;
	struct interface *ifp;
	struct sockaddr_in gate_sin;
	naddr mask, gate;
	union {
		struct {
			struct rt_msghdr rtm;
			struct sockaddr addrs[RTAX_MAX];
		} r;
		struct if_msghdr ifm;
	} m;
	char str[100], *strp;
	struct rt_addrinfo info;


	for (;;) {
		cc = read(rt_sock, &m, sizeof(m));
		if (cc <= 0) {
			if (cc < 0 && errno != EWOULDBLOCK)
				LOGERR("read(rt_sock)");
			return;
		}

		if (m.r.rtm.rtm_version != RTM_VERSION) {
			msglog("bogus routing message version %d",
			       m.r.rtm.rtm_version);
			continue;
		}

		/* Ignore our own results.
		 */
		if (m.r.rtm.rtm_type <= RTM_CHANGE
		    && m.r.rtm.rtm_pid == mypid) {
			static int complained = 0;
			if (!complained) {
				msglog("receiving our own change messages");
				complained = 1;
			}
			continue;
		}

		if (m.r.rtm.rtm_type == RTM_IFINFO
		    || m.r.rtm.rtm_type == RTM_NEWADDR
		    || m.r.rtm.rtm_type == RTM_DELADDR) {
			ifp = ifwithindex(m.ifm.ifm_index,
					  m.r.rtm.rtm_type != RTM_DELADDR);
			if (ifp == NULL)
				trace_act("note %s with flags %#x"
					  " for unknown interface index #%d",
					  rtm_type_name(m.r.rtm.rtm_type),
					  m.ifm.ifm_flags,
					  m.ifm.ifm_index);
			else
				trace_act("note %s with flags %#x for %s",
					  rtm_type_name(m.r.rtm.rtm_type),
					  m.ifm.ifm_flags,
					  ifp->int_name);

			/* After being informed of a change to an interface,
			 * check them all now if the check would otherwise
			 * be a long time from now, if the interface is
			 * not known, or if the interface has been turned
			 * off or on.
			 */
			if (ifinit_timer.tv_sec-now.tv_sec>=CHECK_BAD_INTERVAL
			    || ifp == NULL
			    || ((ifp->int_if_flags ^ m.ifm.ifm_flags)
				& IFF_UP) != 0)
				ifinit_timer.tv_sec = now.tv_sec;
			continue;
		}
#ifdef RTM_OIFINFO
		if (m.r.rtm.rtm_type == RTM_OIFINFO)
			continue;	/* ignore compat message */
#endif

		strlcpy(str, rtm_type_name(m.r.rtm.rtm_type), sizeof(str));
		strp = &str[strlen(str)];
		if (m.r.rtm.rtm_type <= RTM_CHANGE)
			strp += sprintf(strp," from pid %d",m.r.rtm.rtm_pid);

		/*
		 * Only messages that use the struct rt_msghdr format are
		 * allowed beyond this point.
		 */
		if (m.r.rtm.rtm_type > RTM_RESOLVE) {
			trace_act("ignore %s", str);
			continue;
		}
		
		rt_xaddrs(&info, m.r.addrs, &m.r.addrs[RTAX_MAX],
			  m.r.rtm.rtm_addrs);

		if (INFO_DST(&info) == 0) {
			trace_act("ignore %s without dst", str);
			continue;
		}

		if (INFO_DST(&info)->sa_family != AF_INET) {
			trace_act("ignore %s for AF %d", str,
				  INFO_DST(&info)->sa_family);
			continue;
		}

		mask = ((INFO_MASK(&info) != 0)
			? ntohl(S_ADDR(INFO_MASK(&info)))
			: (m.r.rtm.rtm_flags & RTF_HOST)
			? HOST_MASK
			: std_mask(S_ADDR(INFO_DST(&info))));

		strp += sprintf(strp, ": %s",
				addrname(S_ADDR(INFO_DST(&info)), mask, 0));

		if (IN_MULTICAST(ntohl(S_ADDR(INFO_DST(&info))))) {
			trace_act("ignore multicast %s", str);
			continue;
		}

#if defined(RTF_LLINFO) 
		if (m.r.rtm.rtm_flags & RTF_LLINFO) {
			trace_act("ignore ARP %s", str);
			continue;
		}
#endif
		
#if defined(RTF_WASCLONED) && defined(__FreeBSD__)
		if (m.r.rtm.rtm_flags & RTF_WASCLONED) {
			trace_act("ignore cloned %s", str);
			continue;
		}
#endif

		if (get_info_gate(&INFO_GATE(&info), &gate_sin)) {
			gate = S_ADDR(INFO_GATE(&info));
			strp += sprintf(strp, " --> %s", naddr_ntoa(gate));
		} else {
			gate = 0;
		}

		if (INFO_AUTHOR(&info) != 0)
			strp += sprintf(strp, " by authority of %s",
					saddr_ntoa(INFO_AUTHOR(&info)));

		switch (m.r.rtm.rtm_type) {
		case RTM_ADD:
		case RTM_CHANGE:
		case RTM_REDIRECT:
			if (m.r.rtm.rtm_errno != 0) {
				trace_act("ignore %s with \"%s\" error",
					  str, strerror(m.r.rtm.rtm_errno));
			} else {
				trace_act("%s", str);
				rtm_add(&m.r.rtm,&info,0);
			}
			break;

		case RTM_DELETE:
			if (m.r.rtm.rtm_errno != 0
			    && m.r.rtm.rtm_errno != ESRCH) {
				trace_act("ignore %s with \"%s\" error",
					  str, strerror(m.r.rtm.rtm_errno));
			} else {
				trace_act("%s", str);
				del_static(S_ADDR(INFO_DST(&info)), mask,
					   gate, 1);
			}
			break;

		case RTM_LOSING:
			trace_act("%s", str);
			rtm_lose(&m.r.rtm,&info);
			break;

		default:
			trace_act("ignore %s", str);
			break;
		}
	}
}


/* after aggregating, note routes that belong in the kernel
 */
static void
kern_out(struct ag_info *ag)
{
	struct khash *k;


	/* Do not install bad routes if they are not already present.
	 * This includes routes that had RS_NET_SYN for interfaces that
	 * recently died.
	 */
	if (ag->ag_metric == HOPCNT_INFINITY) {
		k = kern_find(htonl(ag->ag_dst_h), ag->ag_mask, 0);
		if (k == NULL)
			return;
	} else {
		k = kern_add(htonl(ag->ag_dst_h), ag->ag_mask);
	}

	if (k->k_state & KS_NEW) {
		/* will need to add new entry to the kernel table */
		k->k_state = KS_ADD;
		if (ag->ag_state & AGS_GATEWAY)
			k->k_state |= KS_GATEWAY;
		k->k_gate = ag->ag_gate;
		k->k_metric = ag->ag_metric;
		return;
	}

	if (k->k_state & KS_STATIC)
		return;

	/* modify existing kernel entry if necessary */
	if (k->k_gate != ag->ag_gate
	    || k->k_metric != ag->ag_metric) {
		/* Must delete bad interface routes etc. to change them. */
		if (k->k_metric == HOPCNT_INFINITY)
			k->k_state |= KS_DEL_ADD;
		k->k_gate = ag->ag_gate;
		k->k_metric = ag->ag_metric;
		k->k_state |= KS_CHANGE;
	}

	/* If the daemon thinks the route should exist, forget
	 * about any redirections.
	 * If the daemon thinks the route should exist, eventually
	 * override manual intervention by the operator.
	 */
	if ((k->k_state & (KS_DYNAMIC | KS_DELETED)) != 0) {
		k->k_state &= ~KS_DYNAMIC;
		k->k_state |= (KS_ADD | KS_DEL_ADD);
	}

	if ((k->k_state & KS_GATEWAY)
	    && !(ag->ag_state & AGS_GATEWAY)) {
		k->k_state &= ~KS_GATEWAY;
		k->k_state |= (KS_ADD | KS_DEL_ADD);
	} else if (!(k->k_state & KS_GATEWAY)
		   && (ag->ag_state & AGS_GATEWAY)) {
		k->k_state |= KS_GATEWAY;
		k->k_state |= (KS_ADD | KS_DEL_ADD);
	}

	/* Deleting-and-adding is necessary to change aspects of a route.
	 * Just delete instead of deleting and then adding a bad route.
	 * Otherwise, we want to keep the route in the kernel.
	 */
	if (k->k_metric == HOPCNT_INFINITY
	    && (k->k_state & KS_DEL_ADD))
		k->k_state |= KS_DELETE;
	else
		k->k_state &= ~KS_DELETE;
#undef RT
}


/* ARGSUSED */
static int
walk_kern(struct radix_node *rn,
	  struct walkarg *argp UNUSED)
{
#define RT ((struct rt_entry *)rn)
	char metric, pref;
	u_int ags = 0;


	/* Do not install synthetic routes */
	if (RT->rt_state & RS_NET_SYN)
		return 0;

	if (!(RT->rt_state & RS_IF)) {
		/* This is an ordinary route, not for an interface.
		 */

		/* aggregate, ordinary good routes without regard to
		 * their metric
		 */
		pref = 1;
		ags |= (AGS_GATEWAY | AGS_SUPPRESS | AGS_AGGREGATE);

		/* Do not install host routes directly to hosts, to avoid
		 * interfering with ARP entries in the kernel table.
		 */
		if (RT_ISHOST(RT)
		    && ntohl(RT->rt_dst) == RT->rt_gate)
			return 0;

	} else {
		/* This is an interface route.
		 * Do not install routes for "external" remote interfaces.
		 */
		if (RT->rt_ifp != 0 && (RT->rt_ifp->int_state & IS_EXTERNAL))
			return 0;

		/* Interfaces should override received routes.
		 */
		pref = 0;
		ags |= (AGS_IF | AGS_CORS_GATE);

		/* If it is not an interface, or an alias for an interface,
		 * it must be a "gateway."
		 *
		 * If it is a "remote" interface, it is also a "gateway" to
		 * the kernel if is not an alias.
		 */
		if (RT->rt_ifp == 0
		    || (RT->rt_ifp->int_state & IS_REMOTE))
			ags |= (AGS_GATEWAY | AGS_SUPPRESS | AGS_AGGREGATE);
	}

	/* If RIP is off and IRDP is on, let the route to the discovered
	 * route suppress any RIP routes.  Eventually the RIP routes
	 * will time-out and be deleted.  This reaches the steady-state
	 * quicker.
	 */
	if ((RT->rt_state & RS_RDISC) && rip_sock < 0)
		ags |= AGS_CORS_GATE;

	metric = RT->rt_metric;
	if (metric == HOPCNT_INFINITY) {
		/* if the route is dead, so try hard to aggregate. */
		pref = HOPCNT_INFINITY;
		ags |= (AGS_FINE_GATE | AGS_SUPPRESS);
		ags &= ~(AGS_IF | AGS_CORS_GATE);
	}

	ag_check(RT->rt_dst, RT->rt_mask, RT->rt_gate, 0,
		 metric,pref, 0, 0, ags, kern_out);
	return 0;
#undef RT
}


/* Update the kernel table to match the daemon table.
 */
static void
fix_kern(void)
{
	int i;
	struct khash *k, **pk;


	need_kern = age_timer;

	/* Walk daemon table, updating the copy of the kernel table.
	 */
	(void)rn_walktree(rhead, walk_kern, 0);
	ag_flush(0,0,kern_out);

	for (i = 0; i < KHASH_SIZE; i++) {
		for (pk = &khash_bins[i]; (k = *pk) != NULL; ) {
			/* Do not touch static routes */
			if (k->k_state & KS_STATIC) {
				kern_check_static(k,0);
				pk = &k->k_next;
				continue;
			}

			/* check hold on routes deleted by the operator */
			if (k->k_keep > now.tv_sec) {
				/* ensure we check when the hold is over */
				LIM_SEC(need_kern, k->k_keep);
				/* mark for the next cycle */
				k->k_state |= KS_DELETE;
				pk = &k->k_next;
				continue;
			}

			if ((k->k_state & KS_DELETE)
			    && !(k->k_state & KS_DYNAMIC)) {
				kern_ioctl(k, RTM_DELETE, 0);
				*pk = k->k_next;
				free(k);
				continue;
			}

			if (k->k_state & KS_DEL_ADD)
				kern_ioctl(k, RTM_DELETE, 0);

			if (k->k_state & KS_ADD) {
				kern_ioctl(k, RTM_ADD,
					   ((0 != (k->k_state & (KS_GATEWAY
							| KS_DYNAMIC)))
					    ? RTF_GATEWAY : 0));
			} else if (k->k_state & KS_CHANGE) {
				kern_ioctl(k,  RTM_CHANGE,
					   ((0 != (k->k_state & (KS_GATEWAY
							| KS_DYNAMIC)))
					    ? RTF_GATEWAY : 0));
			}
			k->k_state &= ~(KS_ADD|KS_CHANGE|KS_DEL_ADD);

			/* Mark this route to be deleted in the next cycle.
			 * This deletes routes that disappear from the
			 * daemon table, since the normal aging code
			 * will clear the bit for routes that have not
			 * disappeared from the daemon table.
			 */
			k->k_state |= KS_DELETE;
			pk = &k->k_next;
		}
	}
}


/* Delete a static route in the image of the kernel table.
 */
void
del_static(naddr dst,
	   naddr mask,
	   naddr gate,
	   int gone)
{
	struct khash *k;
	struct rt_entry *rt;

	/* Just mark it in the table to be deleted next time the kernel
	 * table is updated.
	 * If it has already been deleted, mark it as such, and set its
	 * keep-timer so that it will not be deleted again for a while.
	 * This lets the operator delete a route added by the daemon
	 * and add a replacement.
	 */
	k = kern_find(dst, mask, 0);
	if (k != NULL && (gate == 0 || k->k_gate == gate)) {
		k->k_state &= ~(KS_STATIC | KS_DYNAMIC | KS_CHECK);
		k->k_state |= KS_DELETE;
		if (gone) {
			k->k_state |= KS_DELETED;
			k->k_keep = now.tv_sec + K_KEEP_LIM;
		}
	}

	rt = rtget(dst, mask);
	if (rt != NULL && (rt->rt_state & RS_STATIC))
		rtbad(rt);
}


/* Delete all routes generated from ICMP Redirects that use a given gateway,
 * as well as old redirected routes.
 */
void
del_redirects(naddr bad_gate,
	      time_t old)
{
	int i;
	struct khash *k;


	for (i = 0; i < KHASH_SIZE; i++) {
		for (k = khash_bins[i]; k != NULL; k = k->k_next) {
			if (!(k->k_state & KS_DYNAMIC)
			    || (k->k_state & KS_STATIC))
				continue;

			if (k->k_gate != bad_gate
			    && k->k_redirect_time > old
			    && !supplier)
				continue;

			k->k_state |= KS_DELETE;
			k->k_state &= ~KS_DYNAMIC;
			need_kern.tv_sec = now.tv_sec;
			trace_act("mark redirected %s --> %s for deletion",
				  addrname(k->k_dst, k->k_mask, 0),
				  naddr_ntoa(k->k_gate));
		}
	}
}


/* Start the daemon tables.
 */
extern int max_keylen;

void
rtinit(void)
{
	int i;
	struct ag_info *ag;

	/* Initialize the radix trees */
	max_keylen = sizeof(struct sockaddr_in);
	rn_init();
	rn_inithead(&rhead, 32);

	/* mark all of the slots in the table free */
	ag_avail = ag_slots;
	for (ag = ag_slots, i = 1; i < NUM_AG_SLOTS; i++) {
		ag->ag_fine = ag+1;
		ag++;
	}
}


#ifdef _HAVE_SIN_LEN
static struct sockaddr_in dst_sock = {sizeof(dst_sock), AF_INET, 0, {0}, {0}};
static struct sockaddr_in mask_sock = {sizeof(mask_sock), AF_INET, 0, {0}, {0}};
#else
static struct sockaddr_in_new dst_sock = {_SIN_ADDR_SIZE, AF_INET};
static struct sockaddr_in_new mask_sock = {_SIN_ADDR_SIZE, AF_INET};
#endif


static void
set_need_flash(void)
{
	if (!need_flash) {
		need_flash = 1;
		/* Do not send the flash update immediately.  Wait a little
		 * while to hear from other routers.
		 */
		no_flash.tv_sec = now.tv_sec + MIN_WAITTIME;
	}
}


/* Get a particular routing table entry
 */
struct rt_entry *
rtget(naddr dst, naddr mask)
{
	struct rt_entry *rt;

	dst_sock.sin_addr.s_addr = dst;
	mask_sock.sin_addr.s_addr = htonl(mask);
	masktrim(&mask_sock);
	rt = (struct rt_entry *)rhead->rnh_lookup(&dst_sock,&mask_sock,rhead);
	if (!rt
	    || rt->rt_dst != dst
	    || rt->rt_mask != mask)
		return 0;

	return rt;
}


/* Find a route to dst as the kernel would.
 */
struct rt_entry *
rtfind(naddr dst)
{
	dst_sock.sin_addr.s_addr = dst;
	return (struct rt_entry *)rhead->rnh_matchaddr(&dst_sock, rhead);
}


/* add a route to the table
 */
void
rtadd(naddr	dst,
      naddr	mask,
      u_int	state,			/* rt_state for the entry */
      struct	rt_spare *new)
{
	struct rt_entry *rt;
	naddr smask;
	int i;
	struct rt_spare *rts;

	rt = (struct rt_entry *)rtmalloc(sizeof (*rt), "rtadd");
	memset(rt, 0, sizeof(*rt));
	for (rts = rt->rt_spares, i = NUM_SPARES; i != 0; i--, rts++)
		rts->rts_metric = HOPCNT_INFINITY;

	rt->rt_nodes->rn_key = (caddr_t)&rt->rt_dst_sock;
	rt->rt_dst = dst;
	rt->rt_dst_sock.sin_family = AF_INET;
#ifdef _HAVE_SIN_LEN
	rt->rt_dst_sock.sin_len = dst_sock.sin_len;
#endif
	if (mask != HOST_MASK) {
		smask = std_mask(dst);
		if ((smask & ~mask) == 0 && mask > smask)
			state |= RS_SUBNET;
	}
	mask_sock.sin_addr.s_addr = htonl(mask);
	masktrim(&mask_sock);
	rt->rt_mask = mask;
	rt->rt_state = state;
	rt->rt_spares[0] = *new;
	rt->rt_time = now.tv_sec;
	rt->rt_poison_metric = HOPCNT_INFINITY;
	rt->rt_seqno = update_seqno;

	if (++total_routes == MAX_ROUTES)
		msglog("have maximum (%d) routes", total_routes);
	if (TRACEACTIONS)
		trace_add_del("Add", rt);

	need_kern.tv_sec = now.tv_sec;
	set_need_flash();

	if (0 == rhead->rnh_addaddr(&rt->rt_dst_sock, &mask_sock,
				    rhead, rt->rt_nodes)) {
		msglog("rnh_addaddr() failed for %s mask=%#lx",
		       naddr_ntoa(dst), (u_long)mask);
		free(rt);
	}
}


/* notice a changed route
 */
void
rtchange(struct rt_entry *rt,
	 u_int	state,			/* new state bits */
	 struct rt_spare *new,
	 char	*label)
{
	if (rt->rt_metric != new->rts_metric) {
		/* Fix the kernel immediately if it seems the route
		 * has gone bad, since there may be a working route that
		 * aggregates this route.
		 */
		if (new->rts_metric == HOPCNT_INFINITY) {
			need_kern.tv_sec = now.tv_sec;
			if (new->rts_time >= now.tv_sec - EXPIRE_TIME)
				new->rts_time = now.tv_sec - EXPIRE_TIME;
		}
		rt->rt_seqno = update_seqno;
		set_need_flash();
	}

	if (rt->rt_gate != new->rts_gate) {
		need_kern.tv_sec = now.tv_sec;
		rt->rt_seqno = update_seqno;
		set_need_flash();
	}

	state |= (rt->rt_state & RS_SUBNET);

	/* Keep various things from deciding ageless routes are stale.
	 */
	if (!AGE_RT(state, new->rts_ifp))
		new->rts_time = now.tv_sec;

	if (TRACEACTIONS)
		trace_change(rt, state, new,
			     label ? label : "Chg   ");

	rt->rt_state = state;
	rt->rt_spares[0] = *new;
}


/* check for a better route among the spares
 */
static struct rt_spare *
rts_better(struct rt_entry *rt)
{
	struct rt_spare *rts, *rts1;
	int i;

	/* find the best alternative among the spares */
	rts = rt->rt_spares+1;
	for (i = NUM_SPARES, rts1 = rts+1; i > 2; i--, rts1++) {
		if (BETTER_LINK(rt,rts1,rts))
			rts = rts1;
	}

	return rts;
}


/* switch to a backup route
 */
void
rtswitch(struct rt_entry *rt,
	 struct rt_spare *rts)
{
	struct rt_spare swap;
	char label[10];


	/* Do not change permanent routes */
	if (0 != (rt->rt_state & (RS_MHOME | RS_STATIC | RS_RDISC
				  | RS_NET_SYN | RS_IF)))
		return;

	/* find the best alternative among the spares */
	if (rts == NULL)
		rts = rts_better(rt);

	/* Do not bother if it is not worthwhile.
	 */
	if (!BETTER_LINK(rt, rts, rt->rt_spares))
		return;

	swap = rt->rt_spares[0];
	(void)sprintf(label, "Use #%d", (int)(rts - rt->rt_spares));
	rtchange(rt, rt->rt_state & ~(RS_NET_SYN | RS_RDISC), rts, label);
	if (swap.rts_metric == HOPCNT_INFINITY) {
		*rts = rts_empty;
	} else {
		*rts = swap;
	}
}


void
rtdelete(struct rt_entry *rt)
{
	struct khash *k;


	if (TRACEACTIONS)
		trace_add_del("Del", rt);

	k = kern_find(rt->rt_dst, rt->rt_mask, 0);
	if (k != NULL) {
		k->k_state |= KS_DELETE;
		need_kern.tv_sec = now.tv_sec;
	}

	dst_sock.sin_addr.s_addr = rt->rt_dst;
	mask_sock.sin_addr.s_addr = htonl(rt->rt_mask);
	masktrim(&mask_sock);
	if (rt != (struct rt_entry *)rhead->rnh_deladdr(&dst_sock, &mask_sock,
							rhead)) {
		msglog("rnh_deladdr() failed");
	} else {
		free(rt);
		total_routes--;
	}
}


void
rts_delete(struct rt_entry *rt,
	   struct rt_spare *rts)
{
	trace_upslot(rt, rts, &rts_empty);
	*rts = rts_empty;
}


/* Get rid of a bad route, and try to switch to a replacement.
 */
static void
rtbad(struct rt_entry *rt)
{
	struct rt_spare new;

	/* Poison the route */
	new = rt->rt_spares[0];
	new.rts_metric = HOPCNT_INFINITY;
	rtchange(rt, rt->rt_state & ~(RS_IF | RS_LOCAL | RS_STATIC), &new, 0);
	rtswitch(rt, 0);
}


/* Junk a RS_NET_SYN or RS_LOCAL route,
 *	unless it is needed by another interface.
 */
void
rtbad_sub(struct rt_entry *rt)
{
	struct interface *ifp, *ifp1;
	struct intnet *intnetp;
	u_int state;


	ifp1 = NULL;
	state = 0;

	if (rt->rt_state & RS_LOCAL) {
		/* Is this the route through loopback for the interface?
		 * If so, see if it is used by any other interfaces, such
		 * as a point-to-point interface with the same local address.
		 */
		LIST_FOREACH(ifp, &ifnet, int_list) {
			/* Retain it if another interface needs it.
			 */
			if (ifp->int_addr == rt->rt_ifp->int_addr) {
				state |= RS_LOCAL;
				ifp1 = ifp;
				break;
			}
		}

	}

	if (!(state & RS_LOCAL)) {
		/* Retain RIPv1 logical network route if there is another
		 * interface that justifies it.
		 */
		if (rt->rt_state & RS_NET_SYN) {
			LIST_FOREACH(ifp, &ifnet, int_list) {
				if ((ifp->int_state & IS_NEED_NET_SYN)
				    && rt->rt_mask == ifp->int_std_mask
				    && rt->rt_dst == ifp->int_std_addr) {
					state |= RS_NET_SYN;
					ifp1 = ifp;
					break;
				}
			}
		}

		/* or if there is an authority route that needs it. */
		for (intnetp = intnets;
		     intnetp != NULL;
		     intnetp = intnetp->intnet_next) {
			if (intnetp->intnet_addr == rt->rt_dst
			    && intnetp->intnet_mask == rt->rt_mask) {
				state |= (RS_NET_SYN | RS_NET_INT);
				break;
			}
		}
	}

	if (ifp1 != NULL || (state & RS_NET_SYN)) {
		struct rt_spare new = rt->rt_spares[0];
		new.rts_ifp = ifp1;
		rtchange(rt, ((rt->rt_state & ~(RS_NET_SYN|RS_LOCAL)) | state),
			 &new, 0);
	} else {
		rtbad(rt);
	}
}


/* Called while walking the table looking for sick interfaces
 * or after a time change.
 */
/* ARGSUSED */
int
walk_bad(struct radix_node *rn,
	 struct walkarg *argp UNUSED)
{
#define RT ((struct rt_entry *)rn)
	struct rt_spare *rts;
	int i;


	/* fix any spare routes through the interface
	 */
	rts = RT->rt_spares;
	for (i = NUM_SPARES; i != 1; i--) {
		rts++;
		if (rts->rts_metric < HOPCNT_INFINITY
		    && (rts->rts_ifp == NULL
			|| (rts->rts_ifp->int_state & IS_BROKE)))
			rts_delete(RT, rts);
	}

	/* Deal with the main route
	 */
	/* finished if it has been handled before or if its interface is ok
	 */
	if (RT->rt_ifp == 0 || !(RT->rt_ifp->int_state & IS_BROKE))
		return 0;

	/* Bad routes for other than interfaces are easy.
	 */
	if (0 == (RT->rt_state & (RS_IF | RS_NET_SYN | RS_LOCAL))) {
		rtbad(RT);
		return 0;
	}

	rtbad_sub(RT);
	return 0;
#undef RT
}


/* Check the age of an individual route.
 */
/* ARGSUSED */
static int
walk_age(struct radix_node *rn,
	   struct walkarg *argp UNUSED)
{
#define RT ((struct rt_entry *)rn)
	struct interface *ifp;
	struct rt_spare *rts;
	int i;


	/* age all of the spare routes, including the primary route
	 * currently in use
	 */
	rts = RT->rt_spares;
	for (i = NUM_SPARES; i != 0; i--, rts++) {

		ifp = rts->rts_ifp;
		if (i == NUM_SPARES) {
			if (!AGE_RT(RT->rt_state, ifp)) {
				/* Keep various things from deciding ageless
				 * routes are stale
				 */
				rts->rts_time = now.tv_sec;
				continue;
			}

			/* forget RIP routes after RIP has been turned off.
			 */
			if (rip_sock < 0) {
				rtdelete(RT);
				return 0;
			}
		}

		/* age failing routes
		 */
		if (age_bad_gate == rts->rts_gate
		    && rts->rts_time >= now_stale) {
			rts->rts_time -= SUPPLY_INTERVAL;
		}

		/* trash the spare routes when they go bad */
		if (rts->rts_metric < HOPCNT_INFINITY
		    && now_garbage > rts->rts_time
		    && i != NUM_SPARES)
			rts_delete(RT, rts);
	}


	/* finished if the active route is still fresh */
	if (now_stale <= RT->rt_time)
		return 0;

	/* try to switch to an alternative */
	rtswitch(RT, 0);

	/* Delete a dead route after it has been publicly mourned. */
	if (now_garbage > RT->rt_time) {
		rtdelete(RT);
		return 0;
	}

	/* Start poisoning a bad route before deleting it. */
	if (now.tv_sec - RT->rt_time > EXPIRE_TIME) {
		struct rt_spare new = RT->rt_spares[0];
		new.rts_metric = HOPCNT_INFINITY;
		rtchange(RT, RT->rt_state, &new, 0);
	}
	return 0;
}


/* Watch for dead routes and interfaces.
 */
void
age(naddr bad_gate)
{
	struct interface *ifp;
	int need_query = 0;

	/* If not listening to RIP, there is no need to age the routes in
	 * the table.
	 */
	age_timer.tv_sec = (now.tv_sec
			    + ((rip_sock < 0) ? NEVER : SUPPLY_INTERVAL));

	/* Check for dead IS_REMOTE interfaces by timing their
	 * transmissions.
	 */
	LIST_FOREACH(ifp, &ifnet, int_list) {
		if (!(ifp->int_state & IS_REMOTE))
			continue;

		/* ignore unreachable remote interfaces */
		if (!check_remote(ifp))
			continue;

		/* Restore remote interface that has become reachable
		 */
		if (ifp->int_state & IS_BROKE)
			if_ok(ifp, "remote ");

		if (ifp->int_act_time != NEVER
		    && now.tv_sec - ifp->int_act_time > EXPIRE_TIME) {
			msglog("remote interface %s to %s timed out after"
			       " %ld:%ld",
			       ifp->int_name,
			       naddr_ntoa(ifp->int_dstaddr),
			       (long)(now.tv_sec - ifp->int_act_time)/60,
			       (long)(now.tv_sec - ifp->int_act_time)%60);
			if_sick(ifp);
		}

		/* If we have not heard from the other router
		 * recently, ask it.
		 */
		if (now.tv_sec >= ifp->int_query_time) {
			ifp->int_query_time = NEVER;
			need_query = 1;
		}
	}

	/* Age routes. */
	age_bad_gate = bad_gate;
	(void)rn_walktree(rhead, walk_age, 0);

	/* delete old redirected routes to keep the kernel table small
	 * and prevent blackholes
	 */
	del_redirects(bad_gate, now.tv_sec-STALE_TIME);

	/* Update the kernel routing table. */
	fix_kern();

	/* poke reticent remote gateways */
	if (need_query)
		rip_query();
}
