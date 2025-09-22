/*	$OpenBSD: pfvar_priv.h,v 1.38 2024/09/07 22:41:55 aisha Exp $	*/

/*
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002 - 2013 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2016 Alexander Bluhm <bluhm@openbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _NET_PFVAR_PRIV_H_
#define _NET_PFVAR_PRIV_H_

#ifdef _KERNEL

#include <sys/rwlock.h>
#include <sys/mutex.h>
#include <sys/percpu.h>

/*
 * Locks used to protect struct members in this file:
 *	L	pf_inp_mtx		link pf to inp mutex
 */

struct pfsync_deferral;

/*
 * pf state items - links from pf_state_key to pf_states
 */

struct pf_state_item {
	TAILQ_ENTRY(pf_state_item)
				 si_entry;
	struct pf_state		*si_st;
};

TAILQ_HEAD(pf_statelisthead, pf_state_item);

/*
 * pf state keys - look up states by address
 */

struct pf_state_key {
	struct pf_addr	 addr[2];
	u_int16_t	 port[2];
	u_int16_t	 rdomain;
	u_int16_t	 hash;
	sa_family_t	 af;
	u_int8_t	 proto;

	RBT_ENTRY(pf_state_key)	 sk_entry;
	struct pf_statelisthead	 sk_states;
	struct pf_state_key	*sk_reverse;
	struct inpcb		*sk_inp;	/* [L] */
	pf_refcnt_t		 sk_refcnt;
	u_int8_t		 sk_removed;
};

RBT_HEAD(pf_state_tree, pf_state_key);
RBT_PROTOTYPE(pf_state_tree, pf_state_key, sk_entry, pf_state_compare_key);

#define PF_REVERSED_KEY(key, family)				\
	((key[PF_SK_WIRE]->af != key[PF_SK_STACK]->af) &&	\
	 (key[PF_SK_WIRE]->af != (family)))

/*
 * pf state
 *
 * Protection/ownership of pf_state members:
 *	I	immutable after pf_state_insert()
 *	M	pf_state mtx
 *	P	PF_STATE_LOCK
 *	S	pfsync
 *	L	pf_state_list
 *	g	pf_purge gc
 */

struct pf_state {
	u_int64_t		 id;		/* [I] */
	u_int32_t		 creatorid;	/* [I] */
	u_int8_t		 direction;	/* [I] */
	u_int8_t		 pad[3];

	TAILQ_ENTRY(pf_state)	 sync_list;	/* [S] */
	struct pfsync_deferral	*sync_defer;	/* [S] */
	TAILQ_ENTRY(pf_state)	 entry_list;	/* [L] */
	SLIST_ENTRY(pf_state)	 gc_list;	/* [g] */
	RBT_ENTRY(pf_state)	 entry_id;	/* [P] */
	struct pf_state_peer	 src;
	struct pf_state_peer	 dst;
	struct pf_rule_slist	 match_rules;	/* [I] */
	union pf_rule_ptr	 rule;		/* [I] */
	union pf_rule_ptr	 anchor;	/* [I] */
	union pf_rule_ptr	 natrule;	/* [I] */
	struct pf_addr		 rt_addr;	/* [I] */
	struct pf_sn_head	 src_nodes;	/* [I] */
	struct pf_state_key	*key[2];	/* [I] stack and wire */
	struct pfi_kif		*kif;		/* [I] */
	struct mutex		 mtx;
	pf_refcnt_t		 refcnt;
	u_int64_t		 packets[2];
	u_int64_t		 bytes[2];
	int32_t			 creation;	/* [I] */
	int32_t			 expire;
	int32_t			 pfsync_time;	/* [S] */
	int			 rtableid[2];	/* [I] stack and wire */
	u_int16_t		 qid;		/* [I] */
	u_int16_t		 pqid;		/* [I] */
	u_int16_t		 tag;		/* [I] */
	u_int16_t		 state_flags;	/* [M] */
	u_int8_t		 log;		/* [I] */
	u_int8_t		 timeout;
	u_int8_t		 sync_state;	/* [S] PFSYNC_S_x */
	u_int8_t		 sync_updates;	/* [S] */
	u_int8_t		 min_ttl;	/* [I] */
	u_int8_t		 set_tos;	/* [I] */
	u_int8_t		 set_prio[2];	/* [I] */
	u_int16_t		 max_mss;	/* [I] */
	u_int16_t		 if_index_in;	/* [I] */
	u_int16_t		 if_index_out;	/* [I] */
	u_int16_t		 delay;		/* [I] */
	u_int8_t		 rt;		/* [I] */
};

RBT_HEAD(pf_state_tree_id, pf_state);
RBT_PROTOTYPE(pf_state_tree_id, pf_state, entry_id, pf_state_compare_id);
extern struct pf_state_tree_id tree_id;

/*
 * states are linked into a global list to support the following
 * functionality:
 *
 * - garbage collection
 * - pfsync bulk send operations
 * - bulk state fetches via the DIOCGETSTATES ioctl
 * - bulk state clearing via the DIOCCLRSTATES ioctl
 * 
 * states are inserted into the global pf_state_list once it has also
 * been successfully added to the various trees that make up the state
 * table. states are only removed from the pf_state_list by the garbage
 * collection process.
 *
 * the pf_state_list head and tail pointers (ie, the pfs_list TAILQ_HEAD
 * structure) and the pointers between the entries on the pf_state_list
 * are locked separately. at a high level, this allows for insertion
 * of new states into the pf_state_list while other contexts (eg, the
 * ioctls) are traversing the state items in the list. for garbage
 * collection to remove items from the pf_state_list, it has to exclude
 * both modifications to the list head and tail pointers, and traversal
 * of the links between the states.
 *
 * the head and tail pointers are protected by a mutex. the pointers
 * between states are protected by an rwlock.
 *
 * because insertions are only made to the end of the list, if we get
 * a snapshot of the head and tail of the list and prevent modifications
 * to the links between states, we can safely traverse between the
 * head and tail entries. subsequent insertions can add entries after
 * our view of the tail, but we don't look past our view.
 *
 * if both locks must be taken, the rwlock protecting the links between
 * states is taken before the mutex protecting the head and tail
 * pointer.
 *
 * insertion into the list follows this pattern:
 *
 *	// serialise list head/tail modifications
 *	mtx_enter(&pf_state_list.pfs_mtx);
 *	TAILQ_INSERT_TAIL(&pf_state_list.pfs_list, state, entry_list);
 *	mtx_leave(&pf_state_list.pfs_mtx);
 *
 * traversal of the list:
 *
 *	// lock against the gc removing an item from the list
 *	rw_enter_read(&pf_state_list.pfs_rwl);
 *
 *	// get a snapshot view of the ends of the list
 *	mtx_enter(&pf_state_list.pfs_mtx);
 *	head = TAILQ_FIRST(&pf_state_list.pfs_list);
 *	tail = TAILQ_LAST(&pf_state_list.pfs_list, pf_state_queue);
 *	mtx_leave(&pf_state_list.pfs_mtx);
 *
 *	state = NULL;
 *	next = head;
 *
 *	while (state != tail) {
 *		state = next;
 *		next = TAILQ_NEXT(state, entry_list);
 *
 *		// look at the state
 *	}
 *
 *	rw_exit_read(&pf_state_list.pfs_rwl);
 *
 * removing an item from the list:
 * 
 *	// wait for iterators (readers) to get out
 *	rw_enter_write(&pf_state_list.pfs_rwl);
 *
 *	// serialise list head/tail modifications
 *	mtx_enter(&pf_state_list.pfs_mtx);
 *	TAILQ_REMOVE(&pf_state_list.pfs_list, state, entry_list);
 *	mtx_leave(&pf_state_list.pfs_mtx);
 *
 *	rw_exit_write(&pf_state_list.pfs_rwl);
 *
 * the lock ordering for pf_state_list locks and the rest of the pf
 * locks are:
 *
 * 1. KERNEL_LOCK
 * 2. NET_LOCK
 * 3. pf_state_list.pfs_rwl
 * 4. PF_LOCK
 * 5. PF_STATE_LOCK
 * 6. pf_state_list.pfs_mtx
 */

struct pf_state_list {
	/* the list of states in the system */
	struct pf_state_queue		pfs_list;

	/* serialise pfs_list head/tail access */
	struct mutex			pfs_mtx;

	/* serialise access to pointers between pfs_list entries */
	struct rwlock			pfs_rwl;
};

#define PF_STATE_LIST_INITIALIZER(_pfs) {				\
	.pfs_list	= TAILQ_HEAD_INITIALIZER(_pfs.pfs_list),	\
	.pfs_mtx	= MUTEX_INITIALIZER(IPL_SOFTNET),		\
	.pfs_rwl	= RWLOCK_INITIALIZER("pfstates"),		\
}

extern struct rwlock pf_lock;

struct pf_pdesc {
	struct {
		int	 done;
		uid_t	 uid;
		gid_t	 gid;
		pid_t	 pid;
	}		 lookup;
	u_int64_t	 tot_len;	/* Make Mickey money */

	struct pf_addr	 nsaddr;	/* src address after NAT */
	struct pf_addr	 ndaddr;	/* dst address after NAT */

	struct pfi_kif	*kif;		/* incoming interface */
	struct mbuf	*m;		/* mbuf containing the packet */
	struct pf_addr	*src;		/* src address */
	struct pf_addr	*dst;		/* dst address */
	u_int16_t	*pcksum;	/* proto cksum */
	u_int16_t	*sport;
	u_int16_t	*dport;
	u_int16_t	 osport;
	u_int16_t	 odport;
	u_int16_t	 hash;
	u_int16_t	 nsport;	/* src port after NAT */
	u_int16_t	 ndport;	/* dst port after NAT */

	u_int32_t	 off;		/* protocol header offset */
	u_int32_t	 hdrlen;	/* protocol header length */
	u_int32_t	 p_len;		/* length of protocol payload */
	u_int32_t	 extoff;	/* extension header offset */
	u_int32_t	 fragoff;	/* fragment header offset */
	u_int32_t	 jumbolen;	/* length from v6 jumbo header */
	u_int32_t	 badopts;	/* v4 options or v6 routing headers */
#define PF_OPT_OTHER		0x0001
#define PF_OPT_JUMBO		0x0002
#define PF_OPT_ROUTER_ALERT	0x0004

	u_int16_t	 rdomain;	/* original routing domain */
	u_int16_t	 virtual_proto;
#define PF_VPROTO_FRAGMENT	256
	sa_family_t	 af;
	sa_family_t	 naf;
	u_int8_t	 proto;
	u_int8_t	 tos;
	u_int8_t	 ttl;
	u_int8_t	 dir;		/* direction */
	u_int8_t	 sidx;		/* key index for source */
	u_int8_t	 didx;		/* key index for destination */
	u_int8_t	 destchg;	/* flag set when destination changed */
	u_int8_t	 pflog;		/* flags for packet logging */
	union {
		struct tcphdr			tcp;
		struct udphdr			udp;
		struct icmp			icmp;
#ifdef INET6
		struct icmp6_hdr		icmp6;
		struct mld_hdr			mld;
		struct nd_neighbor_solicit	nd_ns;
#endif /* INET6 */
	} hdr;
};

struct pf_anchor_stackframe {
	struct pf_ruleset	*sf_rs;
	struct pf_rule		*sf_anchor;
	union {
		struct pf_rule			*u_r;
		struct pf_anchor_stackframe	*u_stack_top;
	} u;
	struct pf_anchor	*sf_child;
	int			 sf_jump_target;
};
#define sf_r		u.u_r
#define sf_stack_top	u.u_stack_top
enum {
	PF_NEXT_RULE,
	PF_NEXT_CHILD
};

extern struct cpumem *pf_anchor_stack;

enum pf_trans_type {
	PF_TRANS_NONE,
	PF_TRANS_GETRULE,
	PF_TRANS_MAX
};

struct pf_trans {
	LIST_ENTRY(pf_trans)	pft_entry;
	uint32_t		pft_unit;		/* process id */
	uint64_t		pft_ticket;
	enum pf_trans_type	pft_type;
	union {
		struct {
			u_int32_t		 gr_version;
			struct pf_anchor	*gr_anchor;
			struct pf_rule		*gr_rule;
		} u_getrule;
	} u;
};

#define pftgr_version	u.u_getrule.gr_version
#define pftgr_anchor	u.u_getrule.gr_anchor
#define pftgr_rule	u.u_getrule.gr_rule

extern struct timeout	pf_purge_states_to;
extern struct task	pf_purge_task;
extern struct timeout	pf_purge_to;

struct pf_state		*pf_state_ref(struct pf_state *);
void			 pf_state_unref(struct pf_state *);

extern struct rwlock	pf_lock;
extern struct rwlock	pf_state_lock;
extern struct mutex	pf_frag_mtx;
extern struct mutex	pf_inp_mtx;

#define PF_LOCK()		do {			\
		rw_enter_write(&pf_lock);		\
	} while (0)

#define PF_UNLOCK()		do {			\
		PF_ASSERT_LOCKED();			\
		rw_exit_write(&pf_lock);		\
	} while (0)

#define PF_ASSERT_LOCKED()	do {			\
		if (rw_status(&pf_lock) != RW_WRITE)	\
			splassert_fail(RW_WRITE,	\
			    rw_status(&pf_lock),__func__);\
	} while (0)

#define PF_ASSERT_UNLOCKED()	do {			\
		if (rw_status(&pf_lock) == RW_WRITE)	\
			splassert_fail(0, rw_status(&pf_lock), __func__);\
	} while (0)

#define PF_STATE_ENTER_READ()	do {			\
		rw_enter_read(&pf_state_lock);		\
	} while (0)

#define PF_STATE_EXIT_READ()	do {			\
		rw_exit_read(&pf_state_lock);		\
	} while (0)

#define PF_STATE_ENTER_WRITE()	do {			\
		rw_enter_write(&pf_state_lock);		\
	} while (0)

#define PF_STATE_EXIT_WRITE()	do {			\
		PF_STATE_ASSERT_LOCKED();		\
		rw_exit_write(&pf_state_lock);		\
	} while (0)

#define PF_STATE_ASSERT_LOCKED()	do {		\
		if (rw_status(&pf_state_lock) != RW_WRITE)\
			splassert_fail(RW_WRITE,	\
			    rw_status(&pf_state_lock), __func__);\
	} while (0)

#define PF_FRAG_LOCK()		mtx_enter(&pf_frag_mtx)
#define PF_FRAG_UNLOCK()	mtx_leave(&pf_frag_mtx)

/* for copies to/from network byte order */
void			pf_state_peer_hton(const struct pf_state_peer *,
			    struct pfsync_state_peer *);
void			pf_state_peer_ntoh(const struct pfsync_state_peer *,
			    struct pf_state_peer *);
u_int16_t		pf_pkt_hash(sa_family_t, uint8_t,
			    const struct pf_addr *, const struct pf_addr *,
			    uint16_t, uint16_t);

#endif /* _KERNEL */

#endif /* _NET_PFVAR_PRIV_H_ */
