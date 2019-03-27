/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Scooter Morris at Genentech Inc.
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
 *	@(#)lockf.h	8.1 (Berkeley) 6/11/93
 * $FreeBSD$
 */

#ifndef _SYS_LOCKF_H_
#define	_SYS_LOCKF_H_

#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_sx.h>

struct flock;
struct vop_advlock_args;
struct vop_advlockasync_args;

/*
 * The lockf_entry structure is a kernel structure which contains the
 * information associated with a byte range lock.  The lockf_entry
 * structures are linked into the inode structure. Locks are sorted by
 * the starting byte of the lock for efficiency.
 *
 * Active and pending locks on a vnode are organised into a
 * graph. Each pending lock has an out-going edge to each active lock
 * that blocks it.
 *
 * Locks:
 * (i)		locked by the vnode interlock
 * (s)		locked by state->ls_lock
 * (S)		locked by lf_lock_states_lock
 * (c)		const until freeing
 */
struct lockf_edge {
	LIST_ENTRY(lockf_edge) le_outlink; /* (s) link from's out-edge list */
	LIST_ENTRY(lockf_edge) le_inlink; /* (s) link to's in-edge list */
	struct lockf_entry *le_from;	/* (c) out-going from here */
	struct lockf_entry *le_to;	/* (s) in-coming to here */
};
LIST_HEAD(lockf_edge_list, lockf_edge);

struct lockf_entry {
	short	lf_flags;	    /* (c) Semantics: F_POSIX, F_FLOCK, F_WAIT */
	short	lf_type;	    /* (s) Lock type: F_RDLCK, F_WRLCK */
	off_t	lf_start;	    /* (s) Byte # of the start of the lock */
	off_t	lf_end;		    /* (s) Byte # of the end of the lock (OFF_MAX=EOF) */
	struct	lock_owner *lf_owner; /* (c) Owner of the lock */
	struct	vnode *lf_vnode;    /* (c) File being locked (only valid for active lock) */
	struct	inode *lf_inode;    /* (c) Back pointer to the inode */
	struct	task *lf_async_task;/* (c) Async lock callback */
	LIST_ENTRY(lockf_entry) lf_link;  /* (s) Linkage for lock lists */
	struct lockf_edge_list lf_outedges; /* (s) list of out-edges */
	struct lockf_edge_list lf_inedges; /* (s) list of out-edges */
	int	lf_refs;	    /* (s) ref count */
};
LIST_HEAD(lockf_entry_list, lockf_entry);

/*
 * Extra lf_flags bits used by the implementation
 */
#define	F_INTR		0x8000	/* lock was interrupted by lf_purgelocks */

/*
 * Filesystem private node structures should include space for a
 * pointer to a struct lockf_state. This pointer is used by the lock
 * manager to track the locking state for a file.
 *
 * The ls_active list contains the set of active locks on the file. It
 * is strictly ordered by the lock's lf_start value. Each active lock
 * will have in-coming edges to any pending lock which it blocks.
 *
 * Lock requests which are blocked by some other active lock are
 * listed in ls_pending with newer requests first in the list. Lock
 * requests in this list will have out-going edges to each active lock
 * that blocks then. They will also have out-going edges to each
 * pending lock that is older in the queue - this helps to ensure
 * fairness when several processes are contenting to lock the same
 * record.

 * The value of ls_threads is the number of threads currently using
 * the state structure (typically either setting/clearing locks or
 * sleeping waiting to do so). This is used to defer freeing the
 * structure while some thread is still using it.
 */
struct lockf {
	LIST_ENTRY(lockf) ls_link;	/* (S) all active lockf states */
	struct	sx	ls_lock;
	struct	lockf_entry_list ls_active; /* (s) Active locks */
	struct	lockf_entry_list ls_pending; /* (s) Pending locks */
	int		ls_threads;	/* (i) Thread count */
};
LIST_HEAD(lockf_list, lockf);

typedef int lf_iterator(struct vnode *, struct flock *, void *);

int	 lf_advlock(struct vop_advlock_args *, struct lockf **, u_quad_t);
int	 lf_advlockasync(struct vop_advlockasync_args *, struct lockf **, u_quad_t);
void	 lf_purgelocks(struct vnode *vp, struct lockf **statep);
int	 lf_iteratelocks_sysid(int sysid, lf_iterator *, void *);
int	 lf_iteratelocks_vnode(struct vnode *vp, lf_iterator *, void *);
int	 lf_countlocks(int sysid);
void	 lf_clearremotesys(int sysid);

#endif /* !_SYS_LOCKF_H_ */
