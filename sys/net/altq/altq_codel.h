/*
 * CoDel - The Controlled-Delay Active Queue Management algorithm
 *
 *  Copyright (C) 2013 Ermal Luçi <eri@FreeBSD.org>
 *  Copyright (C) 2011-2012 Kathleen Nichols <nichols@pollere.com>
 *  Copyright (C) 2011-2012 Van Jacobson <van@pollere.net>
 *  Copyright (C) 2012 Michael D. Taht <dave.taht@bufferbloat.net>
 *  Copyright (C) 2012 Eric Dumazet <edumazet@google.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _ALTQ_ALTQ_CODEL_H_
#define	_ALTQ_ALTQ_CODEL_H_

struct codel_stats {
	u_int32_t	maxpacket;
	struct pktcntr	drop_cnt;
	u_int		marked_packets;
};

struct codel_ifstats {
	u_int			qlength;
	u_int			qlimit;
	struct codel_stats	stats;
	struct pktcntr	cl_xmitcnt;	/* transmitted packet counter */
	struct pktcntr	cl_dropcnt;	/* dropped packet counter */
};

/*
 * CBQ_STATS_VERSION is defined in altq.h to work around issues stemming
 * from mixing of public-API and internal bits in each scheduler-specific
 * header.
 */

#ifdef _KERNEL
#include <net/altq/altq_classq.h>

/**
 * struct codel_params - contains codel parameters
 *  <at> target:	target queue size (in time units)
 *  <at> interval:	width of moving time window
 *  <at> ecn:	is Explicit Congestion Notification enabled
 */
struct codel_params {
	u_int64_t	target;
	u_int64_t	interval;
	int		ecn;
};

/**
 * struct codel_vars - contains codel variables
 *  <at> count:		how many drops we've done since the last time we
 *			entered dropping state
 *  <at> lastcount:	count at entry to dropping state
 *  <at> dropping:	set to true if in dropping state
 *  <at> rec_inv_sqrt:	reciprocal value of sqrt(count) >> 1
 *  <at> first_above_time:	when we went (or will go) continuously above
 *				target for interval
 *  <at> drop_next:	time to drop next packet, or when we dropped last
 *  <at> ldelay:	sojourn time of last dequeued packet
 */
struct codel_vars {
	u_int32_t	count;
	u_int32_t	lastcount;
	int		dropping;
	u_int16_t	rec_inv_sqrt;
	u_int64_t	first_above_time;
	u_int64_t	drop_next;
	u_int64_t	ldelay;
};
        
struct codel {
	int			last_pps;
	struct codel_params	params;
	struct codel_vars	vars;
	struct codel_stats	stats;
	struct timeval		last_log;
	u_int32_t		drop_overlimit;
};

/*
 * codel interface state
 */
struct codel_if {
	struct codel_if		*cif_next;	/* interface state list */
	struct ifaltq		*cif_ifq;	/* backpointer to ifaltq */
	u_int			cif_bandwidth;	/* link bandwidth in bps */

	class_queue_t	*cl_q;		/* class queue structure */
	struct codel	codel;

	/* statistics */
	struct codel_ifstats cl_stats;
};

struct codel	*codel_alloc(int, int, int);
void		 codel_destroy(struct codel *);
int		 codel_addq(struct codel *, class_queue_t *, struct mbuf *);
struct mbuf	*codel_getq(struct codel *, class_queue_t *);
void		 codel_getstats(struct codel *, struct codel_stats *);

#endif /* _KERNEL */

#endif /* _ALTQ_ALTQ_CODEL_H_ */
