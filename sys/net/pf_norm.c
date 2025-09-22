/*	$OpenBSD: pf_norm.c,v 1.236 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright 2001 Niels Provos <provos@citi.umich.edu>
 * Copyright 2009 Henning Brauer <henning@openbsd.org>
 * Copyright 2011-2018 Alexander Bluhm <bluhm@openbsd.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/pool.h>
#include <sys/syslog.h>
#include <sys/mutex.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_fsm.h>
#include <netinet/udp.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>
#endif /* INET6 */

#include <net/pfvar.h>
#include <net/pfvar_priv.h>

struct pf_frent {
	TAILQ_ENTRY(pf_frent) fr_next;
	struct mbuf	*fe_m;
	u_int16_t	 fe_hdrlen;	/* ipv4 header length with ip options
					   ipv6, extension, fragment header */
	u_int16_t	 fe_extoff;	/* last extension header offset or 0 */
	u_int16_t	 fe_len;	/* fragment length */
	u_int16_t	 fe_off;	/* fragment offset */
	u_int16_t	 fe_mff;	/* more fragment flag */
};

RB_HEAD(pf_frag_tree, pf_fragment);
struct pf_frnode {
	struct pf_addr	fn_src;		/* ip source address */
	struct pf_addr	fn_dst;		/* ip destination address */
	sa_family_t	fn_af;		/* address family */
	u_int8_t	fn_proto;	/* protocol for fragments in fn_tree */
	u_int8_t	fn_direction;	/* pf packet direction */
	u_int32_t	fn_fragments;	/* number of entries in fn_tree */
	u_int32_t	fn_gen;		/* fr_gen of newest entry in fn_tree */

	RB_ENTRY(pf_frnode) fn_entry;
	struct pf_frag_tree fn_tree;	/* matching fragments, lookup by id */
};

struct pf_fragment {
	struct pf_frent	*fr_firstoff[PF_FRAG_ENTRY_POINTS];
					/* pointers to queue element */
	u_int8_t	fr_entries[PF_FRAG_ENTRY_POINTS];
					/* count entries between pointers */
	RB_ENTRY(pf_fragment) fr_entry;
	TAILQ_ENTRY(pf_fragment) frag_next;
	TAILQ_HEAD(pf_fragq, pf_frent) fr_queue;
	u_int32_t	fr_id;		/* fragment id for reassemble */
	int32_t		fr_timeout;
	u_int32_t	fr_gen;		/* generation number (per pf_frnode) */
	u_int16_t	fr_maxlen;	/* maximum length of single fragment */
	u_int16_t	fr_holes;	/* number of holes in the queue */
	struct pf_frnode *fr_node;	/* ip src/dst/proto/af for fragments */
};

struct pf_fragment_tag {
	u_int16_t	 ft_hdrlen;	/* header length of reassembled pkt */
	u_int16_t	 ft_extoff;	/* last extension header offset or 0 */
	u_int16_t	 ft_maxlen;	/* maximum fragment payload length */
};

TAILQ_HEAD(pf_fragqueue, pf_fragment)	pf_fragqueue;

static __inline int	 pf_frnode_compare(struct pf_frnode *,
			    struct pf_frnode *);
RB_HEAD(pf_frnode_tree, pf_frnode)	pf_frnode_tree;
RB_PROTOTYPE(pf_frnode_tree, pf_frnode, fn_entry, pf_frnode_compare);
RB_GENERATE(pf_frnode_tree, pf_frnode, fn_entry, pf_frnode_compare);

static __inline int	 pf_frag_compare(struct pf_fragment *,
			    struct pf_fragment *);
RB_PROTOTYPE(pf_frag_tree, pf_fragment, fr_entry, pf_frag_compare);
RB_GENERATE(pf_frag_tree, pf_fragment, fr_entry, pf_frag_compare);

/* Private prototypes */
void			 pf_flush_fragments(void);
void			 pf_free_fragment(struct pf_fragment *);
struct pf_fragment	*pf_find_fragment(struct pf_frnode *, u_int32_t);
struct pf_frent		*pf_create_fragment(u_short *);
int			 pf_frent_holes(struct pf_frent *);
static inline int	 pf_frent_index(struct pf_frent *);
int			 pf_frent_insert(struct pf_fragment *,
			    struct pf_frent *, struct pf_frent *);
void			 pf_frent_remove(struct pf_fragment *,
			    struct pf_frent *);
struct pf_frent		*pf_frent_previous(struct pf_fragment *,
			    struct pf_frent *);
struct pf_fragment	*pf_fillup_fragment(struct pf_frnode *, u_int32_t,
			    struct pf_frent *, u_short *);
struct mbuf		*pf_join_fragment(struct pf_fragment *);
int			 pf_reassemble(struct mbuf **, int, u_short *);
#ifdef INET6
int			 pf_reassemble6(struct mbuf **, struct ip6_frag *,
			    u_int16_t, u_int16_t, int, u_short *);
#endif /* INET6 */

/* Globals */
struct pool		 pf_frent_pl, pf_frag_pl, pf_frnode_pl;
struct pool		 pf_state_scrub_pl;

struct mutex		 pf_frag_mtx;

#define PF_FRAG_LOCK_INIT()	mtx_init(&pf_frag_mtx, IPL_SOFTNET)

void
pf_normalize_init(void)
{
	pool_init(&pf_frent_pl, sizeof(struct pf_frent), 0,
	    IPL_SOFTNET, 0, "pffrent", NULL);
	pool_init(&pf_frnode_pl, sizeof(struct pf_frnode), 0,
	    IPL_SOFTNET, 0, "pffrnode", NULL);
	pool_init(&pf_frag_pl, sizeof(struct pf_fragment), 0,
	    IPL_SOFTNET, 0, "pffrag", NULL);
	pool_init(&pf_state_scrub_pl, sizeof(struct pf_state_scrub), 0,
	    IPL_SOFTNET, 0, "pfstscr", NULL);

	pool_sethiwat(&pf_frag_pl, PFFRAG_FRAG_HIWAT);
	pool_sethardlimit(&pf_frent_pl, PFFRAG_FRENT_HIWAT);

	TAILQ_INIT(&pf_fragqueue);

	PF_FRAG_LOCK_INIT();
}

static __inline int
pf_frnode_compare(struct pf_frnode *a, struct pf_frnode *b)
{
	int	diff;

	if ((diff = a->fn_proto - b->fn_proto) != 0)
		return (diff);
	if ((diff = a->fn_af - b->fn_af) != 0)
		return (diff);
	if ((diff = pf_addr_compare(&a->fn_src, &b->fn_src, a->fn_af)) != 0)
		return (diff);
	if ((diff = pf_addr_compare(&a->fn_dst, &b->fn_dst, a->fn_af)) != 0)
		return (diff);

	return (0);
}

static __inline int
pf_frag_compare(struct pf_fragment *a, struct pf_fragment *b)
{
	int	diff;

	if ((diff = a->fr_id - b->fr_id) != 0)
		return (diff);

	return (0);
}

void
pf_purge_expired_fragments(void)
{
	struct pf_fragment	*frag;
	int32_t			 expire;

	PF_ASSERT_UNLOCKED();

	expire = getuptime() - pf_default_rule.timeout[PFTM_FRAG];

	PF_FRAG_LOCK();
	while ((frag = TAILQ_LAST(&pf_fragqueue, pf_fragqueue)) != NULL) {
		if (frag->fr_timeout > expire)
			break;
		DPFPRINTF(LOG_NOTICE, "expiring %d(%p)", frag->fr_id, frag);
		pf_free_fragment(frag);
	}
	PF_FRAG_UNLOCK();
}

/*
 * Try to flush old fragments to make space for new ones
 */
void
pf_flush_fragments(void)
{
	struct pf_fragment	*frag;
	u_int			 goal;

	goal = pf_status.fragments * 9 / 10;
	DPFPRINTF(LOG_NOTICE, "trying to free > %u frents",
	    pf_status.fragments - goal);
	while (goal < pf_status.fragments) {
		if ((frag = TAILQ_LAST(&pf_fragqueue, pf_fragqueue)) == NULL)
			break;
		pf_free_fragment(frag);
	}
}

/*
 * Remove a fragment from the fragment queue, free its fragment entries,
 * and free the fragment itself.
 */
void
pf_free_fragment(struct pf_fragment *frag)
{
	struct pf_frent		*frent;
	struct pf_frnode	*frnode;

	frnode = frag->fr_node;
	RB_REMOVE(pf_frag_tree, &frnode->fn_tree, frag);
	KASSERT(frnode->fn_fragments >= 1);
	frnode->fn_fragments--;
	if (frnode->fn_fragments == 0) {
		KASSERT(RB_EMPTY(&frnode->fn_tree));
		RB_REMOVE(pf_frnode_tree, &pf_frnode_tree, frnode);
		pool_put(&pf_frnode_pl, frnode);
	}
	TAILQ_REMOVE(&pf_fragqueue, frag, frag_next);

	/* Free all fragment entries */
	while ((frent = TAILQ_FIRST(&frag->fr_queue)) != NULL) {
		TAILQ_REMOVE(&frag->fr_queue, frent, fr_next);
		pf_status.ncounters[NCNT_FRAG_REMOVALS]++;
		m_freem(frent->fe_m);
		pool_put(&pf_frent_pl, frent);
		pf_status.fragments--;
	}
	pool_put(&pf_frag_pl, frag);
}

struct pf_fragment *
pf_find_fragment(struct pf_frnode *key, u_int32_t id)
{
	struct pf_fragment	*frag, idkey;
	struct pf_frnode	*frnode;
	u_int32_t		 stale;

	frnode = RB_FIND(pf_frnode_tree, &pf_frnode_tree, key);
	pf_status.ncounters[NCNT_FRAG_SEARCH]++;
	if (frnode == NULL)
		return (NULL);
	KASSERT(frnode->fn_fragments >= 1);
	idkey.fr_id = id;
	frag = RB_FIND(pf_frag_tree, &frnode->fn_tree, &idkey);
	if (frag == NULL)
		return (NULL);
	/*
	 * Limit the number of fragments we accept for each (proto,src,dst,af)
	 * combination (aka pf_frnode), so we can deal better with a high rate
	 * of fragments.  Problem analysis is in RFC 4963.
	 * Store the current generation for each pf_frnode in fn_gen and on
	 * lookup discard 'stale' fragments (pf_fragment, based on the fr_gen
	 * member).  Instead of adding another button interpret the pf fragment
	 * timeout in multiples of 200 fragments.  This way the default of 60s
	 * means: pf_fragment objects older than 60*200 = 12,000 generations
	 * are considered stale.
	 */
	stale = pf_default_rule.timeout[PFTM_FRAG] * PF_FRAG_STALE;
	if ((frnode->fn_gen - frag->fr_gen) >= stale) {
		DPFPRINTF(LOG_NOTICE, "stale fragment %d(%p), gen %u, num %u",
		    frag->fr_id, frag, frag->fr_gen, frnode->fn_fragments);
		pf_free_fragment(frag);
		return (NULL);
	}
	TAILQ_REMOVE(&pf_fragqueue, frag, frag_next);
	TAILQ_INSERT_HEAD(&pf_fragqueue, frag, frag_next);

	return (frag);
}

struct pf_frent *
pf_create_fragment(u_short *reason)
{
	struct pf_frent	*frent;

	frent = pool_get(&pf_frent_pl, PR_NOWAIT);
	if (frent == NULL) {
		pf_flush_fragments();
		frent = pool_get(&pf_frent_pl, PR_NOWAIT);
		if (frent == NULL) {
			REASON_SET(reason, PFRES_MEMORY);
			return (NULL);
		}
	}
	pf_status.fragments++;

	return (frent);
}

/*
 * Calculate the additional holes that were created in the fragment
 * queue by inserting this fragment.  A fragment in the middle
 * creates one more hole by splitting.  For each connected side,
 * it loses one hole.
 * Fragment entry must be in the queue when calling this function.
 */
int
pf_frent_holes(struct pf_frent *frent)
{
	struct pf_frent *prev = TAILQ_PREV(frent, pf_fragq, fr_next);
	struct pf_frent *next = TAILQ_NEXT(frent, fr_next);
	int holes = 1;

	if (prev == NULL) {
		if (frent->fe_off == 0)
			holes--;
	} else {
		KASSERT(frent->fe_off != 0);
		if (frent->fe_off == prev->fe_off + prev->fe_len)
			holes--;
	}
	if (next == NULL) {
		if (!frent->fe_mff)
			holes--;
	} else {
		KASSERT(frent->fe_mff);
		if (next->fe_off == frent->fe_off + frent->fe_len)
			holes--;
	}
	return holes;
}

static inline int
pf_frent_index(struct pf_frent *frent)
{
	/*
	 * We have an array of 16 entry points to the queue.  A full size
	 * 65535 octet IP packet can have 8192 fragments.  So the queue
	 * traversal length is at most 512 and at most 16 entry points are
	 * checked.  We need 128 additional bytes on a 64 bit architecture.
	 */
	CTASSERT(((u_int16_t)0xffff &~ 7) / (0x10000 / PF_FRAG_ENTRY_POINTS) ==
	    16 - 1);
	CTASSERT(((u_int16_t)0xffff >> 3) / PF_FRAG_ENTRY_POINTS == 512 - 1);

	return frent->fe_off / (0x10000 / PF_FRAG_ENTRY_POINTS);
}

int
pf_frent_insert(struct pf_fragment *frag, struct pf_frent *frent,
    struct pf_frent *prev)
{
	CTASSERT(PF_FRAG_ENTRY_LIMIT <= 0xff);
	int index;

	/*
	 * A packet has at most 65536 octets.  With 16 entry points, each one
	 * spawns 4096 octets.  We limit these to 64 fragments each, which
	 * means on average every fragment must have at least 64 octets.
	 */
	index = pf_frent_index(frent);
	if (frag->fr_entries[index] >= PF_FRAG_ENTRY_LIMIT)
		return ENOBUFS;
	frag->fr_entries[index]++;

	if (prev == NULL) {
		TAILQ_INSERT_HEAD(&frag->fr_queue, frent, fr_next);
	} else {
		KASSERT(prev->fe_off + prev->fe_len <= frent->fe_off);
		TAILQ_INSERT_AFTER(&frag->fr_queue, prev, frent, fr_next);
	}
	pf_status.ncounters[NCNT_FRAG_INSERT]++;

	if (frag->fr_firstoff[index] == NULL) {
		KASSERT(prev == NULL || pf_frent_index(prev) < index);
		frag->fr_firstoff[index] = frent;
	} else {
		if (frent->fe_off < frag->fr_firstoff[index]->fe_off) {
			KASSERT(prev == NULL || pf_frent_index(prev) < index);
			frag->fr_firstoff[index] = frent;
		} else {
			KASSERT(prev != NULL);
			KASSERT(pf_frent_index(prev) == index);
		}
	}

	frag->fr_holes += pf_frent_holes(frent);

	return 0;
}

void
pf_frent_remove(struct pf_fragment *frag, struct pf_frent *frent)
{
#ifdef DIAGNOSTIC
	struct pf_frent *prev = TAILQ_PREV(frent, pf_fragq, fr_next);
#endif
	struct pf_frent *next = TAILQ_NEXT(frent, fr_next);
	int index;

	frag->fr_holes -= pf_frent_holes(frent);

	index = pf_frent_index(frent);
	KASSERT(frag->fr_firstoff[index] != NULL);
	if (frag->fr_firstoff[index]->fe_off == frent->fe_off) {
		if (next == NULL) {
			frag->fr_firstoff[index] = NULL;
		} else {
			KASSERT(frent->fe_off + frent->fe_len <= next->fe_off);
			if (pf_frent_index(next) == index) {
				frag->fr_firstoff[index] = next;
			} else {
				frag->fr_firstoff[index] = NULL;
			}
		}
	} else {
		KASSERT(frag->fr_firstoff[index]->fe_off < frent->fe_off);
		KASSERT(prev != NULL);
		KASSERT(prev->fe_off + prev->fe_len <= frent->fe_off);
		KASSERT(pf_frent_index(prev) == index);
	}

	TAILQ_REMOVE(&frag->fr_queue, frent, fr_next);
	pf_status.ncounters[NCNT_FRAG_REMOVALS]++;

	KASSERT(frag->fr_entries[index] > 0);
	frag->fr_entries[index]--;
}

struct pf_frent *
pf_frent_previous(struct pf_fragment *frag, struct pf_frent *frent)
{
	struct pf_frent *prev, *next;
	int index;

	/*
	 * If there are no fragments after frag, take the final one.  Assume
	 * that the global queue is not empty.
	 */
	prev = TAILQ_LAST(&frag->fr_queue, pf_fragq);
	KASSERT(prev != NULL);
	if (prev->fe_off <= frent->fe_off)
		return prev;
	/*
	 * We want to find a fragment entry that is before frag, but still
	 * close to it.  Find the first fragment entry that is in the same
	 * entry point or in the first entry point after that.  As we have
	 * already checked that there are entries behind frag, this will
	 * succeed.
	 */
	for (index = pf_frent_index(frent); index < PF_FRAG_ENTRY_POINTS;
	    index++) {
		prev = frag->fr_firstoff[index];
		if (prev != NULL)
			break;
	}
	KASSERT(prev != NULL);
	/*
	 * In prev we may have a fragment from the same entry point that is
	 * before frent, or one that is just one position behind frent.
	 * In the latter case, we go back one step and have the predecessor.
	 * There may be none if the new fragment will be the first one.
	 */
	if (prev->fe_off > frent->fe_off) {
		prev = TAILQ_PREV(prev, pf_fragq, fr_next);
		if (prev == NULL)
			return NULL;
		KASSERT(prev->fe_off <= frent->fe_off);
		return prev;
	}
	/*
	 * In prev is the first fragment of the entry point.  The offset
	 * of frag is behind it.  Find the closest previous fragment.
	 */
	for (next = TAILQ_NEXT(prev, fr_next); next != NULL;
	    next = TAILQ_NEXT(next, fr_next)) {
		if (next->fe_off > frent->fe_off)
			break;
		prev = next;
	}
	return prev;
}

struct pf_fragment *
pf_fillup_fragment(struct pf_frnode *key, u_int32_t id,
    struct pf_frent *frent, u_short *reason)
{
	struct pf_frent		*after, *next, *prev;
	struct pf_fragment	*frag;
	struct pf_frnode	*frnode;
	u_int16_t		 total;

	/* No empty fragments */
	if (frent->fe_len == 0) {
		DPFPRINTF(LOG_NOTICE, "bad fragment: len 0");
		goto bad_fragment;
	}

	/* All fragments are 8 byte aligned */
	if (frent->fe_mff && (frent->fe_len & 0x7)) {
		DPFPRINTF(LOG_NOTICE, "bad fragment: mff and len %d",
		    frent->fe_len);
		goto bad_fragment;
	}

	/* Respect maximum length, IP_MAXPACKET == IPV6_MAXPACKET */
	if (frent->fe_off + frent->fe_len > IP_MAXPACKET) {
		DPFPRINTF(LOG_NOTICE, "bad fragment: max packet %d",
		    frent->fe_off + frent->fe_len);
		goto bad_fragment;
	}

	DPFPRINTF(LOG_INFO, key->fn_af == AF_INET ?
	    "reass frag %d @ %d-%d" : "reass frag %#08x @ %d-%d",
	    id, frent->fe_off, frent->fe_off + frent->fe_len);

	/* Fully buffer all of the fragments in this fragment queue */
	frag = pf_find_fragment(key, id);

	/* Create a new reassembly queue for this packet */
	if (frag == NULL) {
		frag = pool_get(&pf_frag_pl, PR_NOWAIT);
		if (frag == NULL) {
			pf_flush_fragments();
			frag = pool_get(&pf_frag_pl, PR_NOWAIT);
			if (frag == NULL) {
				REASON_SET(reason, PFRES_MEMORY);
				goto drop_fragment;
			}
		}
		frnode = RB_FIND(pf_frnode_tree, &pf_frnode_tree, key);
		if (frnode == NULL) {
			frnode = pool_get(&pf_frnode_pl, PR_NOWAIT);
			if (frnode == NULL) {
				pf_flush_fragments();
				frnode = pool_get(&pf_frnode_pl, PR_NOWAIT);
				if (frnode == NULL) {
					REASON_SET(reason, PFRES_MEMORY);
					pool_put(&pf_frag_pl, frag);
					goto drop_fragment;
				}
			}
			*frnode = *key;
			RB_INIT(&frnode->fn_tree);
			frnode->fn_fragments = 0;
			frnode->fn_gen = 0;
		}
		memset(frag->fr_firstoff, 0, sizeof(frag->fr_firstoff));
		memset(frag->fr_entries, 0, sizeof(frag->fr_entries));
		TAILQ_INIT(&frag->fr_queue);
		frag->fr_id = id;
		frag->fr_timeout = getuptime();
		frag->fr_gen = frnode->fn_gen++;
		frag->fr_maxlen = frent->fe_len;
		frag->fr_holes = 1;
		frag->fr_node = frnode;
		/* RB_INSERT cannot fail as pf_find_fragment() found nothing */
		RB_INSERT(pf_frag_tree, &frnode->fn_tree, frag);
		frnode->fn_fragments++;
		if (frnode->fn_fragments == 1)
			RB_INSERT(pf_frnode_tree, &pf_frnode_tree, frnode);
		TAILQ_INSERT_HEAD(&pf_fragqueue, frag, frag_next);

		/* We do not have a previous fragment, cannot fail. */
		pf_frent_insert(frag, frent, NULL);

		return (frag);
	}

	KASSERT(!TAILQ_EMPTY(&frag->fr_queue));
	KASSERT(frag->fr_node);

	/* Remember maximum fragment len for refragmentation */
	if (frent->fe_len > frag->fr_maxlen)
		frag->fr_maxlen = frent->fe_len;

	/* Maximum data we have seen already */
	total = TAILQ_LAST(&frag->fr_queue, pf_fragq)->fe_off +
	    TAILQ_LAST(&frag->fr_queue, pf_fragq)->fe_len;

	/* Non terminal fragments must have more fragments flag */
	if (frent->fe_off + frent->fe_len < total && !frent->fe_mff)
		goto free_ipv6_fragment;

	/* Check if we saw the last fragment already */
	if (!TAILQ_LAST(&frag->fr_queue, pf_fragq)->fe_mff) {
		if (frent->fe_off + frent->fe_len > total ||
		    (frent->fe_off + frent->fe_len == total && frent->fe_mff))
			goto free_ipv6_fragment;
	} else {
		if (frent->fe_off + frent->fe_len == total && !frent->fe_mff)
			goto free_ipv6_fragment;
	}

	/* Find neighbors for newly inserted fragment */
	prev = pf_frent_previous(frag, frent);
	if (prev == NULL) {
		after = TAILQ_FIRST(&frag->fr_queue);
		KASSERT(after != NULL);
	} else {
		after = TAILQ_NEXT(prev, fr_next);
	}

	if (prev != NULL && prev->fe_off + prev->fe_len > frent->fe_off) {
		u_int16_t	precut;

#ifdef INET6
		if (frag->fr_node->fn_af == AF_INET6)
			goto free_ipv6_fragment;
#endif /* INET6 */

		precut = prev->fe_off + prev->fe_len - frent->fe_off;
		if (precut >= frent->fe_len) {
			DPFPRINTF(LOG_NOTICE, "new frag overlapped");
			goto drop_fragment;
		}
		DPFPRINTF(LOG_NOTICE, "frag head overlap %d", precut);
		m_adj(frent->fe_m, precut);
		frent->fe_off += precut;
		frent->fe_len -= precut;
	}

	for (; after != NULL && frent->fe_off + frent->fe_len > after->fe_off;
	    after = next) {
		u_int16_t	aftercut;

#ifdef INET6
		if (frag->fr_node->fn_af == AF_INET6)
			goto free_ipv6_fragment;
#endif /* INET6 */

		aftercut = frent->fe_off + frent->fe_len - after->fe_off;
		if (aftercut < after->fe_len) {
			DPFPRINTF(LOG_NOTICE, "frag tail overlap %d", aftercut);
			m_adj(after->fe_m, aftercut);
			/* Fragment may switch queue as fe_off changes */
			pf_frent_remove(frag, after);
			after->fe_off += aftercut;
			after->fe_len -= aftercut;
			/* Insert into correct queue */
			if (pf_frent_insert(frag, after, prev)) {
				DPFPRINTF(LOG_WARNING,
				    "fragment requeue limit exceeded");
				m_freem(after->fe_m);
				pool_put(&pf_frent_pl, after);
				pf_status.fragments--;
				/* There is not way to recover */
				goto free_fragment;
			}
			break;
		}

		/* This fragment is completely overlapped, lose it */
		DPFPRINTF(LOG_NOTICE, "old frag overlapped");
		next = TAILQ_NEXT(after, fr_next);
		pf_frent_remove(frag, after);
		m_freem(after->fe_m);
		pool_put(&pf_frent_pl, after);
		pf_status.fragments--;
	}

	/* If part of the queue gets too long, there is not way to recover. */
	if (pf_frent_insert(frag, frent, prev)) {
		DPFPRINTF(LOG_WARNING, "fragment queue limit exceeded");
		goto free_fragment;
	}

	return (frag);

free_ipv6_fragment:
	if (frag->fr_node->fn_af == AF_INET)
		goto bad_fragment;
	/*
	 * RFC 5722, Errata 3089:  When reassembling an IPv6 datagram, if one
	 * or more its constituent fragments is determined to be an overlapping
	 * fragment, the entire datagram (and any constituent fragments) MUST
	 * be silently discarded.
	 */
	DPFPRINTF(LOG_NOTICE, "flush overlapping fragments");
free_fragment:
	pf_free_fragment(frag);
bad_fragment:
	REASON_SET(reason, PFRES_FRAG);
drop_fragment:
	pool_put(&pf_frent_pl, frent);
	pf_status.fragments--;
	return (NULL);
}

struct mbuf *
pf_join_fragment(struct pf_fragment *frag)
{
	struct mbuf		*m, *m2;
	struct pf_frent		*frent;

	frent = TAILQ_FIRST(&frag->fr_queue);
	TAILQ_REMOVE(&frag->fr_queue, frent, fr_next);
	pf_status.ncounters[NCNT_FRAG_REMOVALS]++;

	m = frent->fe_m;
	/* Strip off any trailing bytes */
	if ((frent->fe_hdrlen + frent->fe_len) < m->m_pkthdr.len)
		m_adj(m, (frent->fe_hdrlen + frent->fe_len) - m->m_pkthdr.len);
	/* Magic from ip_input */
	m2 = m->m_next;
	m->m_next = NULL;
	m_cat(m, m2);
	pool_put(&pf_frent_pl, frent);
	pf_status.fragments--;

	while ((frent = TAILQ_FIRST(&frag->fr_queue)) != NULL) {
		TAILQ_REMOVE(&frag->fr_queue, frent, fr_next);
		pf_status.ncounters[NCNT_FRAG_REMOVALS]++;
		m2 = frent->fe_m;
		/* Strip off ip header */
		m_adj(m2, frent->fe_hdrlen);
		/* Strip off any trailing bytes */
		if (frent->fe_len < m2->m_pkthdr.len)
			m_adj(m2, frent->fe_len - m2->m_pkthdr.len);
		pool_put(&pf_frent_pl, frent);
		pf_status.fragments--;
		m_removehdr(m2);
		m_cat(m, m2);
	}

	/* Remove from fragment queue */
	pf_free_fragment(frag);

	return (m);
}

int
pf_reassemble(struct mbuf **m0, int dir, u_short *reason)
{
	struct mbuf		*m = *m0;
	struct ip		*ip = mtod(m, struct ip *);
	struct pf_frent		*frent;
	struct pf_fragment	*frag;
	struct pf_frnode	 key;
	u_int16_t		 total, hdrlen;

	/* Get an entry for the fragment queue */
	if ((frent = pf_create_fragment(reason)) == NULL)
		return (PF_DROP);

	frent->fe_m = m;
	frent->fe_hdrlen = ip->ip_hl << 2;
	frent->fe_extoff = 0;
	frent->fe_len = ntohs(ip->ip_len) - (ip->ip_hl << 2);
	frent->fe_off = (ntohs(ip->ip_off) & IP_OFFMASK) << 3;
	frent->fe_mff = ntohs(ip->ip_off) & IP_MF;

	key.fn_src.v4 = ip->ip_src;
	key.fn_dst.v4 = ip->ip_dst;
	key.fn_af = AF_INET;
	key.fn_proto = ip->ip_p;
	key.fn_direction = dir;

	if ((frag = pf_fillup_fragment(&key, ip->ip_id, frent, reason))
	    == NULL)
		return (PF_DROP);

	/* The mbuf is part of the fragment entry, no direct free or access */
	m = *m0 = NULL;

	if (frag->fr_holes) {
		DPFPRINTF(LOG_DEBUG, "frag %d, holes %d",
		    frag->fr_id, frag->fr_holes);
		return (PF_PASS);  /* drop because *m0 is NULL, no error */
	}

	/* We have all the data */
	frent = TAILQ_FIRST(&frag->fr_queue);
	KASSERT(frent != NULL);
	total = TAILQ_LAST(&frag->fr_queue, pf_fragq)->fe_off +
	    TAILQ_LAST(&frag->fr_queue, pf_fragq)->fe_len;
	hdrlen = frent->fe_hdrlen;
	m = *m0 = pf_join_fragment(frag);
	frag = NULL;
	m_calchdrlen(m);

	ip = mtod(m, struct ip *);
	ip->ip_len = htons(hdrlen + total);
	ip->ip_off &= ~(IP_MF|IP_OFFMASK);

	if (hdrlen + total > IP_MAXPACKET) {
		DPFPRINTF(LOG_NOTICE, "drop: too big: %d", total);
		ip->ip_len = 0;
		REASON_SET(reason, PFRES_SHORT);
		/* PF_DROP requires a valid mbuf *m0 in pf_test() */
		return (PF_DROP);
	}

	DPFPRINTF(LOG_INFO, "complete: %p(%d)", m, ntohs(ip->ip_len));
	return (PF_PASS);
}

#ifdef INET6
int
pf_reassemble6(struct mbuf **m0, struct ip6_frag *fraghdr,
    u_int16_t hdrlen, u_int16_t extoff, int dir, u_short *reason)
{
	struct mbuf		*m = *m0;
	struct ip6_hdr		*ip6 = mtod(m, struct ip6_hdr *);
	struct m_tag		*mtag;
	struct pf_fragment_tag	*ftag;
	struct pf_frent		*frent;
	struct pf_fragment	*frag;
	struct pf_frnode	 key;
	int			 off;
	u_int16_t		 total, maxlen;
	u_int8_t		 proto;

	/* Get an entry for the fragment queue */
	if ((frent = pf_create_fragment(reason)) == NULL)
		return (PF_DROP);

	frent->fe_m = m;
	frent->fe_hdrlen = hdrlen;
	frent->fe_extoff = extoff;
	frent->fe_len = sizeof(struct ip6_hdr) + ntohs(ip6->ip6_plen) - hdrlen;
	frent->fe_off = ntohs(fraghdr->ip6f_offlg & IP6F_OFF_MASK);
	frent->fe_mff = fraghdr->ip6f_offlg & IP6F_MORE_FRAG;

	key.fn_src.v6 = ip6->ip6_src;
	key.fn_dst.v6 = ip6->ip6_dst;
	key.fn_af = AF_INET6;
	/* Only the first fragment's protocol is relevant */
	key.fn_proto = 0;
	key.fn_direction = dir;

	if ((frag = pf_fillup_fragment(&key, fraghdr->ip6f_ident, frent,
	    reason)) == NULL)
		return (PF_DROP);

	/* The mbuf is part of the fragment entry, no direct free or access */
	m = *m0 = NULL;

	if (frag->fr_holes) {
		DPFPRINTF(LOG_DEBUG, "frag %#08x, holes %d",
		    frag->fr_id, frag->fr_holes);
		return (PF_PASS);  /* drop because *m0 is NULL, no error */
	}

	/* We have all the data */
	frent = TAILQ_FIRST(&frag->fr_queue);
	KASSERT(frent != NULL);
	extoff = frent->fe_extoff;
	maxlen = frag->fr_maxlen;
	total = TAILQ_LAST(&frag->fr_queue, pf_fragq)->fe_off +
	    TAILQ_LAST(&frag->fr_queue, pf_fragq)->fe_len;
	hdrlen = frent->fe_hdrlen - sizeof(struct ip6_frag);
	m = *m0 = pf_join_fragment(frag);
	frag = NULL;

	/* Take protocol from first fragment header */
	if ((m = m_getptr(m, hdrlen + offsetof(struct ip6_frag, ip6f_nxt),
	    &off)) == NULL)
		panic("%s: short frag mbuf chain", __func__);
	proto = *(mtod(m, caddr_t) + off);
	m = *m0;

	/* Delete frag6 header */
	if (frag6_deletefraghdr(m, hdrlen) != 0)
		goto fail;

	m_calchdrlen(m);

	if ((mtag = m_tag_get(PACKET_TAG_PF_REASSEMBLED, sizeof(struct
	    pf_fragment_tag), M_NOWAIT)) == NULL)
		goto fail;
	ftag = (struct pf_fragment_tag *)(mtag + 1);
	ftag->ft_hdrlen = hdrlen;
	ftag->ft_extoff = extoff;
	ftag->ft_maxlen = maxlen;
	m_tag_prepend(m, mtag);

	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_plen = htons(hdrlen - sizeof(struct ip6_hdr) + total);
	if (extoff) {
		/* Write protocol into next field of last extension header */
		if ((m = m_getptr(m, extoff + offsetof(struct ip6_ext,
		    ip6e_nxt), &off)) == NULL)
			panic("%s: short ext mbuf chain", __func__);
		*(mtod(m, caddr_t) + off) = proto;
		m = *m0;
	} else
		ip6->ip6_nxt = proto;

	if (hdrlen - sizeof(struct ip6_hdr) + total > IPV6_MAXPACKET) {
		DPFPRINTF(LOG_NOTICE, "drop: too big: %d", total);
		ip6->ip6_plen = 0;
		REASON_SET(reason, PFRES_SHORT);
		/* PF_DROP requires a valid mbuf *m0 in pf_test6() */
		return (PF_DROP);
	}

	DPFPRINTF(LOG_INFO, "complete: %p(%d)", m, ntohs(ip6->ip6_plen));
	return (PF_PASS);

fail:
	REASON_SET(reason, PFRES_MEMORY);
	/* PF_DROP requires a valid mbuf *m0 in pf_test6(), will free later */
	return (PF_DROP);
}

int
pf_refragment6(struct mbuf **m0, struct m_tag *mtag, struct sockaddr_in6 *dst,
    struct ifnet *ifp, struct rtentry *rt)
{
	struct mbuf		*m = *m0;
	struct mbuf_list	 ml;
	struct pf_fragment_tag	*ftag = (struct pf_fragment_tag *)(mtag + 1);
	u_int32_t		 mtu;
	u_int16_t		 hdrlen, extoff, maxlen;
	u_int8_t		 proto;
	int			 error;

	hdrlen = ftag->ft_hdrlen;
	extoff = ftag->ft_extoff;
	maxlen = ftag->ft_maxlen;
	m_tag_delete(m, mtag);
	mtag = NULL;
	ftag = NULL;

	/* Checksum must be calculated for the whole packet */
	in6_proto_cksum_out(m, NULL);

	if (extoff) {
		int off;

		/* Use protocol from next field of last extension header */
		if ((m = m_getptr(m, extoff + offsetof(struct ip6_ext,
		    ip6e_nxt), &off)) == NULL)
			panic("%s: short ext mbuf chain", __func__);
		proto = *(mtod(m, caddr_t) + off);
		*(mtod(m, caddr_t) + off) = IPPROTO_FRAGMENT;
		m = *m0;
	} else {
		struct ip6_hdr *hdr;

		hdr = mtod(m, struct ip6_hdr *);
		proto = hdr->ip6_nxt;
		hdr->ip6_nxt = IPPROTO_FRAGMENT;
	}

	/*
	 * Maxlen may be less than 8 iff there was only a single
	 * fragment.  As it was fragmented before, add a fragment
	 * header also for a single fragment.  If total or maxlen
	 * is less than 8, ip6_fragment() will return EMSGSIZE and
	 * we drop the packet.
	 */
	mtu = hdrlen + sizeof(struct ip6_frag) + maxlen;
	error = ip6_fragment(m, &ml, hdrlen, proto, mtu);
	*m0 = NULL;	/* ip6_fragment() has consumed original packet. */
	if (error) {
		DPFPRINTF(LOG_NOTICE, "refragment error %d", error);
		return (PF_DROP);
	}

	while ((m = ml_dequeue(&ml)) != NULL) {
		m->m_pkthdr.pf.flags |= PF_TAG_REFRAGMENTED;
		if (ifp == NULL) {
			int flags = 0;

			switch (atomic_load_int(&ip6_forwarding)) {
			case 2:
				SET(flags, IPV6_FORWARDING_IPSEC);
				/* FALLTHROUGH */
			case 1:
				SET(flags, IPV6_FORWARDING);
				break;
			default:
				ip6stat_inc(ip6s_cantforward);
				return (PF_DROP);
			}
			ip6_forward(m, NULL, flags);
		} else if ((u_long)m->m_pkthdr.len <= ifp->if_mtu) {
			ifp->if_output(ifp, m, sin6tosa(dst), rt);
		} else {
			icmp6_error(m, ICMP6_PACKET_TOO_BIG, 0, ifp->if_mtu);
		}
	}

	return (PF_PASS);
}
#endif /* INET6 */

int
pf_normalize_ip(struct pf_pdesc *pd, u_short *reason)
{
	struct ip	*h = mtod(pd->m, struct ip *);
	u_int16_t	 fragoff = (ntohs(h->ip_off) & IP_OFFMASK) << 3;
	u_int16_t	 mff = (ntohs(h->ip_off) & IP_MF);

	if (!fragoff && !mff)
		goto no_fragment;

	/* Clear IP_DF if we're in no-df mode */
	if (pf_status.reass & PF_REASS_NODF && h->ip_off & htons(IP_DF))
		h->ip_off &= htons(~IP_DF);

	/* We're dealing with a fragment now. Don't allow fragments
	 * with IP_DF to enter the cache. If the flag was cleared by
	 * no-df above, fine. Otherwise drop it.
	 */
	if (h->ip_off & htons(IP_DF)) {
		DPFPRINTF(LOG_NOTICE, "bad fragment: IP_DF");
		REASON_SET(reason, PFRES_FRAG);
		return (PF_DROP);
	}

	if (!pf_status.reass)
		return (PF_PASS);	/* no reassembly */

	/* Returns PF_DROP or m is NULL or completely reassembled mbuf */
	PF_FRAG_LOCK();
	if (pf_reassemble(&pd->m, pd->dir, reason) != PF_PASS) {
		PF_FRAG_UNLOCK();
		return (PF_DROP);
	}
	PF_FRAG_UNLOCK();
	if (pd->m == NULL)
		return (PF_PASS);  /* packet has been reassembled, no error */

	h = mtod(pd->m, struct ip *);

no_fragment:
	/* At this point, only IP_DF is allowed in ip_off */
	if (h->ip_off & ~htons(IP_DF))
		h->ip_off &= htons(IP_DF);

	return (PF_PASS);
}

#ifdef INET6
int
pf_normalize_ip6(struct pf_pdesc *pd, u_short *reason)
{
	struct ip6_frag		 frag;

	if (pd->fragoff == 0)
		goto no_fragment;

	if (!pf_pull_hdr(pd->m, pd->fragoff, &frag, sizeof(frag), reason,
	    AF_INET6))
		return (PF_DROP);

	if (!pf_status.reass)
		return (PF_PASS);	/* no reassembly */

	/* Returns PF_DROP or m is NULL or completely reassembled mbuf */
	PF_FRAG_LOCK();
	if (pf_reassemble6(&pd->m, &frag, pd->fragoff + sizeof(frag),
	    pd->extoff, pd->dir, reason) != PF_PASS) {
		PF_FRAG_UNLOCK();
		return (PF_DROP);
	}
	PF_FRAG_UNLOCK();
	if (pd->m == NULL)
		return (PF_PASS);  /* packet has been reassembled, no error */

no_fragment:
	return (PF_PASS);
}
#endif /* INET6 */

struct pf_state_scrub *
pf_state_scrub_get(void)
{
	return (pool_get(&pf_state_scrub_pl, PR_NOWAIT | PR_ZERO));
}

void
pf_state_scrub_put(struct pf_state_scrub *scrub)
{
	pool_put(&pf_state_scrub_pl, scrub);
}

int
pf_normalize_tcp_alloc(struct pf_state_peer *src)
{
	src->scrub = pf_state_scrub_get();
	if (src->scrub == NULL)
		return (ENOMEM);

	return (0);
}

int
pf_normalize_tcp(struct pf_pdesc *pd)
{
	struct tcphdr	*th = &pd->hdr.tcp;
	u_short		 reason;
	u_int8_t	 flags;
	u_int		 rewrite = 0;

	flags = th->th_flags;
	if (flags & TH_SYN) {
		/* Illegal packet */
		if (flags & TH_RST)
			goto tcp_drop;

		if (flags & TH_FIN)	/* XXX why clear instead of drop? */
			flags &= ~TH_FIN;
	} else {
		/* Illegal packet */
		if (!(flags & (TH_ACK|TH_RST)))
			goto tcp_drop;
	}

	if (!(flags & TH_ACK)) {
		/* These flags are only valid if ACK is set */
		if (flags & (TH_FIN|TH_PUSH|TH_URG))
			goto tcp_drop;
	}

	/* If flags changed, or reserved data set, then adjust */
	if (flags != th->th_flags || th->th_x2 != 0) {
		/* hack: set 4-bit th_x2 = 0 */
		u_int8_t *th_off = (u_int8_t*)(&th->th_ack+1);
		pf_patch_8(pd, th_off, th->th_off << 4, PF_HI);

		pf_patch_8(pd, &th->th_flags, flags, PF_LO);
		rewrite = 1;
	}

	/* Remove urgent pointer, if TH_URG is not set */
	if (!(flags & TH_URG) && th->th_urp) {
		pf_patch_16(pd, &th->th_urp, 0);
		rewrite = 1;
	}

	/* copy back packet headers if we sanitized */
	if (rewrite) {
		m_copyback(pd->m, pd->off, sizeof(*th), th, M_NOWAIT);
	}

	return (PF_PASS);

tcp_drop:
	REASON_SET(&reason, PFRES_NORM);
	return (PF_DROP);
}

int
pf_normalize_tcp_init(struct pf_pdesc *pd, struct pf_state_peer *src)
{
	struct tcphdr	*th = &pd->hdr.tcp;
	u_int32_t	 tsval, tsecr;
	int		 olen;
	u_int8_t	 opts[MAX_TCPOPTLEN], *opt;


	KASSERT(src->scrub == NULL);

	if (pf_normalize_tcp_alloc(src) != 0)
		return (1);

	switch (pd->af) {
	case AF_INET: {
		struct ip *h = mtod(pd->m, struct ip *);
		src->scrub->pfss_ttl = h->ip_ttl;
		break;
	}
#ifdef INET6
	case AF_INET6: {
		struct ip6_hdr *h = mtod(pd->m, struct ip6_hdr *);
		src->scrub->pfss_ttl = h->ip6_hlim;
		break;
	}
#endif /* INET6 */
	default:
		unhandled_af(pd->af);
	}

	/*
	 * All normalizations below are only begun if we see the start of
	 * the connections.  They must all set an enabled bit in pfss_flags
	 */
	if ((th->th_flags & TH_SYN) == 0)
		return (0);

	olen = (th->th_off << 2) - sizeof(*th);
	if (olen < TCPOLEN_TIMESTAMP || !pf_pull_hdr(pd->m,
	    pd->off + sizeof(*th), opts, olen, NULL, pd->af))
		return (0);

	opt = opts;
	while ((opt = pf_find_tcpopt(opt, opts, olen,
		    TCPOPT_TIMESTAMP, TCPOLEN_TIMESTAMP)) != NULL) {

		src->scrub->pfss_flags |= PFSS_TIMESTAMP;
		src->scrub->pfss_ts_mod = arc4random();
		/* note PFSS_PAWS not set yet */
		memcpy(&tsval, &opt[2], sizeof(u_int32_t));
		memcpy(&tsecr, &opt[6], sizeof(u_int32_t));
		src->scrub->pfss_tsval0 = ntohl(tsval);
		src->scrub->pfss_tsval = ntohl(tsval);
		src->scrub->pfss_tsecr = ntohl(tsecr);
		getmicrouptime(&src->scrub->pfss_last);

		opt += opt[1];
	}

	return (0);
}

void
pf_normalize_tcp_cleanup(struct pf_state *state)
{
	if (state->src.scrub)
		pool_put(&pf_state_scrub_pl, state->src.scrub);
	if (state->dst.scrub)
		pool_put(&pf_state_scrub_pl, state->dst.scrub);

	/* Someday... flush the TCP segment reassembly descriptors. */
}

int
pf_normalize_tcp_stateful(struct pf_pdesc *pd, u_short *reason,
    struct pf_state *state, struct pf_state_peer *src,
    struct pf_state_peer *dst, int *writeback)
{
	struct tcphdr	*th = &pd->hdr.tcp;
	struct timeval	 uptime;
	u_int		 tsval_from_last;
	u_int32_t	 tsval, tsecr;
	int		 copyback = 0;
	int		 got_ts = 0;
	int		 olen;
	u_int8_t	 opts[MAX_TCPOPTLEN], *opt;

	KASSERT(src->scrub || dst->scrub);

	/*
	 * Enforce the minimum TTL seen for this connection.  Negate a common
	 * technique to evade an intrusion detection system and confuse
	 * firewall state code.
	 */
	switch (pd->af) {
	case AF_INET:
		if (src->scrub) {
			struct ip *h = mtod(pd->m, struct ip *);
			if (h->ip_ttl > src->scrub->pfss_ttl)
				src->scrub->pfss_ttl = h->ip_ttl;
			h->ip_ttl = src->scrub->pfss_ttl;
		}
		break;
#ifdef INET6
	case AF_INET6:
		if (src->scrub) {
			struct ip6_hdr *h = mtod(pd->m, struct ip6_hdr *);
			if (h->ip6_hlim > src->scrub->pfss_ttl)
				src->scrub->pfss_ttl = h->ip6_hlim;
			h->ip6_hlim = src->scrub->pfss_ttl;
		}
		break;
#endif /* INET6 */
	default:
		unhandled_af(pd->af);
	}

	olen = (th->th_off << 2) - sizeof(*th);

	if (olen >= TCPOLEN_TIMESTAMP &&
	    ((src->scrub && (src->scrub->pfss_flags & PFSS_TIMESTAMP)) ||
	    (dst->scrub && (dst->scrub->pfss_flags & PFSS_TIMESTAMP))) &&
	    pf_pull_hdr(pd->m, pd->off + sizeof(*th), opts, olen, NULL,
	    pd->af)) {

		/* Modulate the timestamps.  Can be used for NAT detection, OS
		 * uptime determination or reboot detection.
		 */
		opt = opts;
		while ((opt = pf_find_tcpopt(opt, opts, olen,
			    TCPOPT_TIMESTAMP, TCPOLEN_TIMESTAMP)) != NULL) {

			u_int8_t *ts = opt + 2;
			u_int8_t *tsr = opt + 6;

			if (got_ts) {
				/* Huh?  Multiple timestamps!? */
				if (pf_status.debug >= LOG_NOTICE) {
					log(LOG_NOTICE,
					    "pf: %s: multiple TS??", __func__);
					pf_print_state(state);
					addlog("\n");
				}
				REASON_SET(reason, PFRES_TS);
				return (PF_DROP);
			}

			memcpy(&tsval, ts, sizeof(u_int32_t));
			memcpy(&tsecr, tsr, sizeof(u_int32_t));

			/* modulate TS */
			if (tsval && src->scrub &&
			    (src->scrub->pfss_flags & PFSS_TIMESTAMP)) {
				/* tsval used further on */
				tsval = ntohl(tsval);
				pf_patch_32_unaligned(pd,
				    ts, htonl(tsval + src->scrub->pfss_ts_mod),
				    PF_ALGNMNT(ts - opts));
				copyback = 1;
			}

			/* modulate TS reply if any (!0) */
			if (tsecr && dst->scrub &&
			    (dst->scrub->pfss_flags & PFSS_TIMESTAMP)) {
				/* tsecr used further on */
				tsecr = ntohl(tsecr) - dst->scrub->pfss_ts_mod;
				pf_patch_32_unaligned(pd,
				    tsr, htonl(tsecr), PF_ALGNMNT(tsr - opts));
				copyback = 1;
			}

			got_ts = 1;
			opt += opt[1];
		}

		if (copyback) {
			/* Copyback the options, caller copies back header */
			*writeback = 1;
			m_copyback(pd->m, pd->off + sizeof(*th), olen, opts, M_NOWAIT);
		}
	}


	/*
	 * Must invalidate PAWS checks on connections idle for too long.
	 * The fastest allowed timestamp clock is 1ms.  That turns out to
	 * be about 24 days before it wraps.  XXX Right now our lowerbound
	 * TS echo check only works for the first 12 days of a connection
	 * when the TS has exhausted half its 32bit space
	 */
#define TS_MAX_IDLE	(24*24*60*60)
#define TS_MAX_CONN	(12*24*60*60)	/* XXX remove when better tsecr check */

	getmicrouptime(&uptime);
	if (src->scrub && (src->scrub->pfss_flags & PFSS_PAWS) &&
	    (uptime.tv_sec - src->scrub->pfss_last.tv_sec > TS_MAX_IDLE ||
	    getuptime() - state->creation > TS_MAX_CONN))  {
		if (pf_status.debug >= LOG_NOTICE) {
			log(LOG_NOTICE, "pf: src idled out of PAWS ");
			pf_print_state(state);
			addlog("\n");
		}
		src->scrub->pfss_flags =
		    (src->scrub->pfss_flags & ~PFSS_PAWS) | PFSS_PAWS_IDLED;
	}
	if (dst->scrub && (dst->scrub->pfss_flags & PFSS_PAWS) &&
	    uptime.tv_sec - dst->scrub->pfss_last.tv_sec > TS_MAX_IDLE) {
		if (pf_status.debug >= LOG_NOTICE) {
			log(LOG_NOTICE, "pf: dst idled out of PAWS ");
			pf_print_state(state);
			addlog("\n");
		}
		dst->scrub->pfss_flags =
		    (dst->scrub->pfss_flags & ~PFSS_PAWS) | PFSS_PAWS_IDLED;
	}

	if (got_ts && src->scrub && dst->scrub &&
	    (src->scrub->pfss_flags & PFSS_PAWS) &&
	    (dst->scrub->pfss_flags & PFSS_PAWS)) {
		/* Validate that the timestamps are "in-window".
		 * RFC1323 describes TCP Timestamp options that allow
		 * measurement of RTT (round trip time) and PAWS
		 * (protection against wrapped sequence numbers).  PAWS
		 * gives us a set of rules for rejecting packets on
		 * long fat pipes (packets that were somehow delayed
		 * in transit longer than the time it took to send the
		 * full TCP sequence space of 4Gb).  We can use these
		 * rules and infer a few others that will let us treat
		 * the 32bit timestamp and the 32bit echoed timestamp
		 * as sequence numbers to prevent a blind attacker from
		 * inserting packets into a connection.
		 *
		 * RFC1323 tells us:
		 *  - The timestamp on this packet must be greater than
		 *    or equal to the last value echoed by the other
		 *    endpoint.  The RFC says those will be discarded
		 *    since it is a dup that has already been acked.
		 *    This gives us a lowerbound on the timestamp.
		 *        timestamp >= other last echoed timestamp
		 *  - The timestamp will be less than or equal to
		 *    the last timestamp plus the time between the
		 *    last packet and now.  The RFC defines the max
		 *    clock rate as 1ms.  We will allow clocks to be
		 *    up to 10% fast and will allow a total difference
		 *    or 30 seconds due to a route change.  And this
		 *    gives us an upperbound on the timestamp.
		 *        timestamp <= last timestamp + max ticks
		 *    We have to be careful here.  Windows will send an
		 *    initial timestamp of zero and then initialize it
		 *    to a random value after the 3whs; presumably to
		 *    avoid a DoS by having to call an expensive RNG
		 *    during a SYN flood.  Proof MS has at least one
		 *    good security geek.
		 *
		 *  - The TCP timestamp option must also echo the other
		 *    endpoints timestamp.  The timestamp echoed is the
		 *    one carried on the earliest unacknowledged segment
		 *    on the left edge of the sequence window.  The RFC
		 *    states that the host will reject any echoed
		 *    timestamps that were larger than any ever sent.
		 *    This gives us an upperbound on the TS echo.
		 *        tescr <= largest_tsval
		 *  - The lowerbound on the TS echo is a little more
		 *    tricky to determine.  The other endpoint's echoed
		 *    values will not decrease.  But there may be
		 *    network conditions that re-order packets and
		 *    cause our view of them to decrease.  For now the
		 *    only lowerbound we can safely determine is that
		 *    the TS echo will never be less than the original
		 *    TS.  XXX There is probably a better lowerbound.
		 *    Remove TS_MAX_CONN with better lowerbound check.
		 *        tescr >= other original TS
		 *
		 * It is also important to note that the fastest
		 * timestamp clock of 1ms will wrap its 32bit space in
		 * 24 days.  So we just disable TS checking after 24
		 * days of idle time.  We actually must use a 12d
		 * connection limit until we can come up with a better
		 * lowerbound to the TS echo check.
		 */
		struct timeval	delta_ts;
		int		ts_fudge;

		/*
		 * PFTM_TS_DIFF is how many seconds of leeway to allow
		 * a host's timestamp.  This can happen if the previous
		 * packet got delayed in transit for much longer than
		 * this packet.
		 */
		if ((ts_fudge = state->rule.ptr->timeout[PFTM_TS_DIFF]) == 0)
			ts_fudge = pf_default_rule.timeout[PFTM_TS_DIFF];

		/* Calculate max ticks since the last timestamp */
#define TS_MAXFREQ	1100		/* RFC max TS freq of 1Khz + 10% skew */
#define TS_MICROSECS	1000000		/* microseconds per second */
		timersub(&uptime, &src->scrub->pfss_last, &delta_ts);
		tsval_from_last = (delta_ts.tv_sec + ts_fudge) * TS_MAXFREQ;
		tsval_from_last += delta_ts.tv_usec / (TS_MICROSECS/TS_MAXFREQ);

		if ((src->state >= TCPS_ESTABLISHED &&
		    dst->state >= TCPS_ESTABLISHED) &&
		    (SEQ_LT(tsval, dst->scrub->pfss_tsecr) ||
		    SEQ_GT(tsval, src->scrub->pfss_tsval + tsval_from_last) ||
		    (tsecr && (SEQ_GT(tsecr, dst->scrub->pfss_tsval) ||
		    SEQ_LT(tsecr, dst->scrub->pfss_tsval0))))) {
			/* Bad RFC1323 implementation or an insertion attack.
			 *
			 * - Solaris 2.6 and 2.7 are known to send another ACK
			 *   after the FIN,FIN|ACK,ACK closing that carries
			 *   an old timestamp.
			 */

			DPFPRINTF(LOG_NOTICE, "Timestamp failed %c%c%c%c",
			    SEQ_LT(tsval, dst->scrub->pfss_tsecr) ? '0' : ' ',
			    SEQ_GT(tsval, src->scrub->pfss_tsval +
			    tsval_from_last) ? '1' : ' ',
			    SEQ_GT(tsecr, dst->scrub->pfss_tsval) ? '2' : ' ',
			    SEQ_LT(tsecr, dst->scrub->pfss_tsval0)? '3' : ' ');
			DPFPRINTF(LOG_NOTICE, " tsval: %u  tsecr: %u  "
			    "+ticks: %u  idle: %llu.%06lus", tsval, tsecr,
			    tsval_from_last, (long long)delta_ts.tv_sec,
			    delta_ts.tv_usec);
			DPFPRINTF(LOG_NOTICE, " src->tsval: %u  tsecr: %u",
			    src->scrub->pfss_tsval, src->scrub->pfss_tsecr);
			DPFPRINTF(LOG_NOTICE, " dst->tsval: %u  tsecr: %u  "
			    "tsval0: %u", dst->scrub->pfss_tsval,
			    dst->scrub->pfss_tsecr, dst->scrub->pfss_tsval0);
			if (pf_status.debug >= LOG_NOTICE) {
				log(LOG_NOTICE, "pf: ");
				pf_print_state(state);
				pf_print_flags(th->th_flags);
				addlog("\n");
			}
			REASON_SET(reason, PFRES_TS);
			return (PF_DROP);
		}
		/* XXX I'd really like to require tsecr but it's optional */
	} else if (!got_ts && (th->th_flags & TH_RST) == 0 &&
	    ((src->state == TCPS_ESTABLISHED && dst->state == TCPS_ESTABLISHED)
	    || pd->p_len > 0 || (th->th_flags & TH_SYN)) &&
	    src->scrub && dst->scrub &&
	    (src->scrub->pfss_flags & PFSS_PAWS) &&
	    (dst->scrub->pfss_flags & PFSS_PAWS)) {
		/* Didn't send a timestamp.  Timestamps aren't really useful
		 * when:
		 *  - connection opening or closing (often not even sent).
		 *    but we must not let an attacker to put a FIN on a
		 *    data packet to sneak it through our ESTABLISHED check.
		 *  - on a TCP reset.  RFC suggests not even looking at TS.
		 *  - on an empty ACK.  The TS will not be echoed so it will
		 *    probably not help keep the RTT calculation in sync and
		 *    there isn't as much danger when the sequence numbers
		 *    got wrapped.  So some stacks don't include TS on empty
		 *    ACKs :-(
		 *
		 * To minimize the disruption to mostly RFC1323 conformant
		 * stacks, we will only require timestamps on data packets.
		 *
		 * And what do ya know, we cannot require timestamps on data
		 * packets.  There appear to be devices that do legitimate
		 * TCP connection hijacking.  There are HTTP devices that allow
		 * a 3whs (with timestamps) and then buffer the HTTP request.
		 * If the intermediate device has the HTTP response cache, it
		 * will spoof the response but not bother timestamping its
		 * packets.  So we can look for the presence of a timestamp in
		 * the first data packet and if there, require it in all future
		 * packets.
		 */

		if (pd->p_len > 0 && (src->scrub->pfss_flags & PFSS_DATA_TS)) {
			/*
			 * Hey!  Someone tried to sneak a packet in.  Or the
			 * stack changed its RFC1323 behavior?!?!
			 */
			if (pf_status.debug >= LOG_NOTICE) {
				log(LOG_NOTICE,
				    "pf: did not receive expected RFC1323 "
				    "timestamp");
				pf_print_state(state);
				pf_print_flags(th->th_flags);
				addlog("\n");
			}
			REASON_SET(reason, PFRES_TS);
			return (PF_DROP);
		}
	}

	/*
	 * We will note if a host sends his data packets with or without
	 * timestamps.  And require all data packets to contain a timestamp
	 * if the first does.  PAWS implicitly requires that all data packets be
	 * timestamped.  But I think there are middle-man devices that hijack
	 * TCP streams immediately after the 3whs and don't timestamp their
	 * packets (seen in a WWW accelerator or cache).
	 */
	if (pd->p_len > 0 && src->scrub && (src->scrub->pfss_flags &
	    (PFSS_TIMESTAMP|PFSS_DATA_TS|PFSS_DATA_NOTS)) == PFSS_TIMESTAMP) {
		if (got_ts)
			src->scrub->pfss_flags |= PFSS_DATA_TS;
		else {
			src->scrub->pfss_flags |= PFSS_DATA_NOTS;
			if (pf_status.debug >= LOG_NOTICE && dst->scrub &&
			    (dst->scrub->pfss_flags & PFSS_TIMESTAMP)) {
				/* Don't warn if other host rejected RFC1323 */
				log(LOG_NOTICE,
				    "pf: broken RFC1323 stack did not "
				    "timestamp data packet. Disabled PAWS "
				    "security.");
				pf_print_state(state);
				pf_print_flags(th->th_flags);
				addlog("\n");
			}
		}
	}

	/*
	 * Update PAWS values
	 */
	if (got_ts && src->scrub && PFSS_TIMESTAMP == (src->scrub->pfss_flags &
	    (PFSS_PAWS_IDLED|PFSS_TIMESTAMP))) {
		getmicrouptime(&src->scrub->pfss_last);
		if (SEQ_GEQ(tsval, src->scrub->pfss_tsval) ||
		    (src->scrub->pfss_flags & PFSS_PAWS) == 0)
			src->scrub->pfss_tsval = tsval;

		if (tsecr) {
			if (SEQ_GEQ(tsecr, src->scrub->pfss_tsecr) ||
			    (src->scrub->pfss_flags & PFSS_PAWS) == 0)
				src->scrub->pfss_tsecr = tsecr;

			if ((src->scrub->pfss_flags & PFSS_PAWS) == 0 &&
			    (SEQ_LT(tsval, src->scrub->pfss_tsval0) ||
			    src->scrub->pfss_tsval0 == 0)) {
				/* tsval0 MUST be the lowest timestamp */
				src->scrub->pfss_tsval0 = tsval;
			}

			/* Only fully initialized after a TS gets echoed */
			if ((src->scrub->pfss_flags & PFSS_PAWS) == 0)
				src->scrub->pfss_flags |= PFSS_PAWS;
		}
	}

	/* I have a dream....  TCP segment reassembly.... */
	return (0);
}

int
pf_normalize_mss(struct pf_pdesc *pd, u_int16_t maxmss)
{
	int		 olen, optsoff;
	u_int8_t	 opts[MAX_TCPOPTLEN], *opt;

	olen = (pd->hdr.tcp.th_off << 2) - sizeof(struct tcphdr);
	optsoff = pd->off + sizeof(struct tcphdr);
	if (olen < TCPOLEN_MAXSEG ||
	    !pf_pull_hdr(pd->m, optsoff, opts, olen, NULL, pd->af))
		return (0);

	opt = opts;
	while ((opt = pf_find_tcpopt(opt, opts, olen,
		    TCPOPT_MAXSEG, TCPOLEN_MAXSEG)) != NULL) {
		u_int16_t	mss;
		u_int8_t       *mssp = opt + 2;
		memcpy(&mss, mssp, sizeof(mss));
		if (ntohs(mss) > maxmss) {
			size_t mssoffopts = mssp - opts;
			pf_patch_16_unaligned(pd, &mss,
			    htons(maxmss), PF_ALGNMNT(mssoffopts));
			m_copyback(pd->m, optsoff + mssoffopts,
			    sizeof(mss), &mss, M_NOWAIT);
			m_copyback(pd->m, pd->off,
			    sizeof(struct tcphdr), &pd->hdr.tcp, M_NOWAIT);
		}

		opt += opt[1];
	}

	return (0);
}

void
pf_scrub(struct mbuf *m, u_int16_t flags, sa_family_t af, u_int8_t min_ttl,
    u_int8_t tos)
{
	struct ip		*h = mtod(m, struct ip *);
#ifdef INET6
	struct ip6_hdr		*h6 = mtod(m, struct ip6_hdr *);
#endif	/* INET6 */
	u_int16_t		 old;

	/* Clear IP_DF if no-df was requested */
	if (flags & PFSTATE_NODF && af == AF_INET && h->ip_off & htons(IP_DF)) {
		old = h->ip_off;
		h->ip_off &= htons(~IP_DF);
		pf_cksum_fixup(&h->ip_sum, old, h->ip_off, 0);
	}

	/* Enforce a minimum ttl, may cause endless packet loops */
	if (min_ttl && af == AF_INET && h->ip_ttl < min_ttl) {
		old = h->ip_ttl;
		h->ip_ttl = min_ttl;
		pf_cksum_fixup(&h->ip_sum, old, h->ip_ttl, 0);
	}
#ifdef INET6
	if (min_ttl && af == AF_INET6 && h6->ip6_hlim < min_ttl)
		h6->ip6_hlim = min_ttl;
#endif	/* INET6 */

	/* Enforce tos */
	if (flags & PFSTATE_SETTOS) {
		if (af == AF_INET) {
			/*
			 * ip_tos is 8 bit field at offset 1. Use 16 bit value
			 * at offset 0.
			 */
			old = *(u_int16_t *)h;
			h->ip_tos = tos | (h->ip_tos & IPTOS_ECN_MASK);
			pf_cksum_fixup(&h->ip_sum, old, *(u_int16_t *)h, 0);
		}
#ifdef INET6
		if (af == AF_INET6) {
			/* drugs are unable to explain such idiocy */
			h6->ip6_flow &= ~htonl(0x0fc00000);
			h6->ip6_flow |= htonl(((u_int32_t)tos) << 20);
		}
#endif	/* INET6 */
	}

	/* random-id, but not for fragments */
	if (flags & PFSTATE_RANDOMID && af == AF_INET &&
	    !(h->ip_off & ~htons(IP_DF))) {
		old = h->ip_id;
		h->ip_id = htons(ip_randomid());
		pf_cksum_fixup(&h->ip_sum, old, h->ip_id, 0);
	}
}
