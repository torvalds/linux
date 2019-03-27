/*-
 * Copyright (c) 2015-2016
 * 	Alexander V. Chernikov <melifaro@FreeBSD.org>
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

#ifndef _NET_ROUTE_VAR_H_
#define _NET_ROUTE_VAR_H_

struct rib_head {
	struct radix_head	head;
	rn_matchaddr_f_t	*rnh_matchaddr;	/* longest match for sockaddr */
	rn_addaddr_f_t		*rnh_addaddr;	/* add based on sockaddr*/
	rn_deladdr_f_t		*rnh_deladdr;	/* remove based on sockaddr */
	rn_lookup_f_t		*rnh_lookup;	/* exact match for sockaddr */
	rn_walktree_t		*rnh_walktree;	/* traverse tree */
	rn_walktree_from_t	*rnh_walktree_from; /* traverse tree below a */
	rn_close_t		*rnh_close;	/*do something when the last ref drops*/
	rt_gen_t		rnh_gen;	/* generation counter */
	int			rnh_multipath;	/* multipath capable ? */
	struct radix_node	rnh_nodes[3];	/* empty tree for common case */
	struct rmlock		rib_lock;	/* config/data path lock */
	struct radix_mask_head	rmhead;		/* masks radix head */
};

#define	RIB_RLOCK_TRACKER	struct rm_priotracker _rib_tracker
#define	RIB_LOCK_INIT(rh)	rm_init(&(rh)->rib_lock, "rib head lock")
#define	RIB_LOCK_DESTROY(rh)	rm_destroy(&(rh)->rib_lock)
#define	RIB_RLOCK(rh)		rm_rlock(&(rh)->rib_lock, &_rib_tracker)
#define	RIB_RUNLOCK(rh)		rm_runlock(&(rh)->rib_lock, &_rib_tracker)
#define	RIB_WLOCK(rh)		rm_wlock(&(rh)->rib_lock)
#define	RIB_WUNLOCK(rh)		rm_wunlock(&(rh)->rib_lock)
#define	RIB_LOCK_ASSERT(rh)	rm_assert(&(rh)->rib_lock, RA_LOCKED)
#define	RIB_WLOCK_ASSERT(rh)	rm_assert(&(rh)->rib_lock, RA_WLOCKED)

struct rib_head *rt_tables_get_rnh(int fib, int family);

/* rte<>nhop translation */
static inline uint16_t
fib_rte_to_nh_flags(int rt_flags)
{
	uint16_t res;

	res = (rt_flags & RTF_REJECT) ? NHF_REJECT : 0;
	res |= (rt_flags & RTF_BLACKHOLE) ? NHF_BLACKHOLE : 0;
	res |= (rt_flags & (RTF_DYNAMIC|RTF_MODIFIED)) ? NHF_REDIRECT : 0;
	res |= (rt_flags & RTF_BROADCAST) ? NHF_BROADCAST : 0;
	res |= (rt_flags & RTF_GATEWAY) ? NHF_GATEWAY : 0;

	return (res);
}


#endif
