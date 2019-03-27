/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1989, 1993
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
 *	@(#)radix.h	8.2 (Berkeley) 10/31/94
 * $FreeBSD$
 */

#ifndef _RADIX_H_
#define	_RADIX_H_

#ifdef _KERNEL
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/_rmlock.h>
#endif

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_RTABLE);
#endif

/*
 * Radix search tree node layout.
 */

struct radix_node {
	struct	radix_mask *rn_mklist;	/* list of masks contained in subtree */
	struct	radix_node *rn_parent;	/* parent */
	short	rn_bit;			/* bit offset; -1-index(netmask) */
	char	rn_bmask;		/* node: mask for bit test*/
	u_char	rn_flags;		/* enumerated next */
#define RNF_NORMAL	1		/* leaf contains normal route */
#define RNF_ROOT	2		/* leaf is root leaf for tree */
#define RNF_ACTIVE	4		/* This node is alive (for rtfree) */
	union {
		struct {			/* leaf only data: */
			caddr_t	rn_Key;		/* object of search */
			caddr_t	rn_Mask;	/* netmask, if present */
			struct	radix_node *rn_Dupedkey;
		} rn_leaf;
		struct {			/* node only data: */
			int	rn_Off;		/* where to start compare */
			struct	radix_node *rn_L;/* progeny */
			struct	radix_node *rn_R;/* progeny */
		} rn_node;
	}		rn_u;
#ifdef RN_DEBUG
	int rn_info;
	struct radix_node *rn_twin;
	struct radix_node *rn_ybro;
#endif
};

#define	rn_dupedkey	rn_u.rn_leaf.rn_Dupedkey
#define	rn_key		rn_u.rn_leaf.rn_Key
#define	rn_mask		rn_u.rn_leaf.rn_Mask
#define	rn_offset	rn_u.rn_node.rn_Off
#define	rn_left		rn_u.rn_node.rn_L
#define	rn_right	rn_u.rn_node.rn_R

/*
 * Annotations to tree concerning potential routes applying to subtrees.
 */

struct radix_mask {
	short	rm_bit;			/* bit offset; -1-index(netmask) */
	char	rm_unused;		/* cf. rn_bmask */
	u_char	rm_flags;		/* cf. rn_flags */
	struct	radix_mask *rm_mklist;	/* more masks to try */
	union	{
		caddr_t	rmu_mask;		/* the mask */
		struct	radix_node *rmu_leaf;	/* for normal routes */
	}	rm_rmu;
	int	rm_refs;		/* # of references to this struct */
};

#define	rm_mask rm_rmu.rmu_mask
#define	rm_leaf rm_rmu.rmu_leaf		/* extra field would make 32 bytes */

struct radix_head;

typedef int walktree_f_t(struct radix_node *, void *);
typedef struct radix_node *rn_matchaddr_f_t(void *v,
    struct radix_head *head);
typedef struct radix_node *rn_addaddr_f_t(void *v, void *mask,
    struct radix_head *head, struct radix_node nodes[]);
typedef struct radix_node *rn_deladdr_f_t(void *v, void *mask,
    struct radix_head *head);
typedef struct radix_node *rn_lookup_f_t(void *v, void *mask,
    struct radix_head *head);
typedef int rn_walktree_t(struct radix_head *head, walktree_f_t *f,
    void *w);
typedef int rn_walktree_from_t(struct radix_head *head,
    void *a, void *m, walktree_f_t *f, void *w);
typedef void rn_close_t(struct radix_node *rn, struct radix_head *head);

struct radix_mask_head;

struct radix_head {
	struct	radix_node *rnh_treetop;
	struct	radix_mask_head *rnh_masks;	/* Storage for our masks */
};

struct radix_node_head {
	struct radix_head rh;
	rn_matchaddr_f_t	*rnh_matchaddr;	/* longest match for sockaddr */
	rn_addaddr_f_t	*rnh_addaddr;	/* add based on sockaddr*/
	rn_deladdr_f_t	*rnh_deladdr;	/* remove based on sockaddr */
	rn_lookup_f_t	*rnh_lookup;	/* exact match for sockaddr */
	rn_walktree_t	*rnh_walktree;	/* traverse tree */
	rn_walktree_from_t	*rnh_walktree_from; /* traverse tree below a */
	rn_close_t	*rnh_close;	/*do something when the last ref drops*/
	struct	radix_node rnh_nodes[3];	/* empty tree for common case */
#ifdef _KERNEL
	struct	rmlock rnh_lock;		/* locks entire radix tree */
#endif
};

struct radix_mask_head {
	struct radix_head head;
	struct radix_node mask_nodes[3];
};

void rn_inithead_internal(struct radix_head *rh, struct radix_node *base_nodes,
    int off);

#ifndef _KERNEL
#define R_Malloc(p, t, n) (p = (t) malloc((unsigned int)(n)))
#define R_Zalloc(p, t, n) (p = (t) calloc(1,(unsigned int)(n)))
#define R_Free(p) free((char *)p);
#else
#define R_Malloc(p, t, n) (p = (t) malloc((unsigned long)(n), M_RTABLE, M_NOWAIT))
#define R_Zalloc(p, t, n) (p = (t) malloc((unsigned long)(n), M_RTABLE, M_NOWAIT | M_ZERO))
#define R_Free(p) free((caddr_t)p, M_RTABLE);

#define	RADIX_NODE_HEAD_RLOCK_TRACKER	struct rm_priotracker _rnh_tracker
#define	RADIX_NODE_HEAD_LOCK_INIT(rnh)	\
    rm_init(&(rnh)->rnh_lock, "radix node head")
#define	RADIX_NODE_HEAD_LOCK(rnh)	rm_wlock(&(rnh)->rnh_lock)
#define	RADIX_NODE_HEAD_UNLOCK(rnh)	rm_wunlock(&(rnh)->rnh_lock)
#define	RADIX_NODE_HEAD_RLOCK(rnh)	rm_rlock(&(rnh)->rnh_lock,\
    &_rnh_tracker)
#define	RADIX_NODE_HEAD_RUNLOCK(rnh)	rm_runlock(&(rnh)->rnh_lock,\
    &_rnh_tracker)
#define	RADIX_NODE_HEAD_DESTROY(rnh)	rm_destroy(&(rnh)->rnh_lock)
#define	RADIX_NODE_HEAD_LOCK_ASSERT(rnh) rm_assert(&(rnh)->rnh_lock, RA_LOCKED)
#define	RADIX_NODE_HEAD_WLOCK_ASSERT(rnh) rm_assert(&(rnh)->rnh_lock, RA_WLOCKED)
#endif /* _KERNEL */

int	 rn_inithead(void **, int);
int	 rn_detachhead(void **);
int	 rn_refines(void *, void *);
struct radix_node *rn_addroute(void *, void *, struct radix_head *,
    struct radix_node[2]);
struct radix_node *rn_delete(void *, void *, struct radix_head *);
struct radix_node *rn_lookup (void *v_arg, void *m_arg,
    struct radix_head *head);
struct radix_node *rn_match(void *, struct radix_head *);
int rn_walktree_from(struct radix_head *h, void *a, void *m,
    walktree_f_t *f, void *w);
int rn_walktree(struct radix_head *, walktree_f_t *, void *);

#endif /* _RADIX_H_ */
