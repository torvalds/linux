// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NET3:	Garbage Collector For AF_UNIX sockets
 *
 * Garbage Collector:
 *	Copyright (C) Barak A. Pearlmutter.
 *
 * Chopped about by Alan Cox 22/3/96 to make it fit the AF_UNIX socket problem.
 * If it doesn't work blame me, it worked when Barak sent it.
 *
 * Assumptions:
 *
 *  - object w/ a bit
 *  - free list
 *
 * Current optimizations:
 *
 *  - explicit stack instead of recursion
 *  - tail recurse on first born instead of immediate push/pop
 *  - we gather the stuff that should not be killed into tree
 *    and stack is just a path from root to the current pointer.
 *
 *  Future optimizations:
 *
 *  - don't just push entire root set; process in place
 *
 *  Fixes:
 *	Alan Cox	07 Sept	1997	Vmalloc internal stack as needed.
 *					Cope with changing max_files.
 *	Al Viro		11 Oct 1998
 *		Graph may have cycles. That is, we can send the descriptor
 *		of foo to bar and vice versa. Current code chokes on that.
 *		Fix: move SCM_RIGHTS ones into the separate list and then
 *		skb_free() them all instead of doing explicit fput's.
 *		Another problem: since fput() may block somebody may
 *		create a new unix_socket when we are in the middle of sweep
 *		phase. Fix: revert the logic wrt MARKED. Mark everything
 *		upon the beginning and unmark non-junk ones.
 *
 *		[12 Oct 1998] AAARGH! New code purges all SCM_RIGHTS
 *		sent to connect()'ed but still not accept()'ed sockets.
 *		Fixed. Old code had slightly different problem here:
 *		extra fput() in situation when we passed the descriptor via
 *		such socket and closed it (descriptor). That would happen on
 *		each unix_gc() until the accept(). Since the struct file in
 *		question would go to the free list and might be reused...
 *		That might be the reason of random oopses on filp_close()
 *		in unrelated processes.
 *
 *	AV		28 Feb 1999
 *		Kill the explicit allocation of stack. Now we keep the tree
 *		with root in dummy + pointer (gc_current) to one of the nodes.
 *		Stack is represented as path from gc_current to dummy. Unmark
 *		now means "add to tree". Push == "make it a son of gc_current".
 *		Pop == "move gc_current to parent". We keep only pointers to
 *		parents (->gc_tree).
 *	AV		1 Mar 1999
 *		Damn. Added missing check for ->dead in listen queues scanning.
 *
 *	Miklos Szeredi 25 Jun 2007
 *		Reimplement with a cycle collecting algorithm. This should
 *		solve several problems with the previous code, like being racy
 *		wrt receive and holding up unrelated socket operations.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/net.h>
#include <linux/fs.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#include <net/sock.h>
#include <net/af_unix.h>
#include <net/scm.h>
#include <net/tcp_states.h>

#include "scm.h"

/* Internal data structures and random procedures: */

static LIST_HEAD(gc_candidates);
static DECLARE_WAIT_QUEUE_HEAD(unix_gc_wait);

static void scan_inflight(struct sock *x, void (*func)(struct unix_sock *),
			  struct sk_buff_head *hitlist)
{
	struct sk_buff *skb;
	struct sk_buff *next;

	spin_lock(&x->sk_receive_queue.lock);
	skb_queue_walk_safe(&x->sk_receive_queue, skb, next) {
		/* Do we have file descriptors ? */
		if (UNIXCB(skb).fp) {
			bool hit = false;
			/* Process the descriptors of this socket */
			int nfd = UNIXCB(skb).fp->count;
			struct file **fp = UNIXCB(skb).fp->fp;

			while (nfd--) {
				/* Get the socket the fd matches if it indeed does so */
				struct sock *sk = unix_get_socket(*fp++);

				if (sk) {
					struct unix_sock *u = unix_sk(sk);

					/* Ignore non-candidates, they could
					 * have been added to the queues after
					 * starting the garbage collection
					 */
					if (test_bit(UNIX_GC_CANDIDATE, &u->gc_flags)) {
						hit = true;

						func(u);
					}
				}
			}
			if (hit && hitlist != NULL) {
				__skb_unlink(skb, &x->sk_receive_queue);
				__skb_queue_tail(hitlist, skb);
			}
		}
	}
	spin_unlock(&x->sk_receive_queue.lock);
}

static void scan_children(struct sock *x, void (*func)(struct unix_sock *),
			  struct sk_buff_head *hitlist)
{
	if (x->sk_state != TCP_LISTEN) {
		scan_inflight(x, func, hitlist);
	} else {
		struct sk_buff *skb;
		struct sk_buff *next;
		struct unix_sock *u;
		LIST_HEAD(embryos);

		/* For a listening socket collect the queued embryos
		 * and perform a scan on them as well.
		 */
		spin_lock(&x->sk_receive_queue.lock);
		skb_queue_walk_safe(&x->sk_receive_queue, skb, next) {
			u = unix_sk(skb->sk);

			/* An embryo cannot be in-flight, so it's safe
			 * to use the list link.
			 */
			BUG_ON(!list_empty(&u->link));
			list_add_tail(&u->link, &embryos);
		}
		spin_unlock(&x->sk_receive_queue.lock);

		while (!list_empty(&embryos)) {
			u = list_entry(embryos.next, struct unix_sock, link);
			scan_inflight(&u->sk, func, hitlist);
			list_del_init(&u->link);
		}
	}
}

static void dec_inflight(struct unix_sock *usk)
{
	atomic_long_dec(&usk->inflight);
}

static void inc_inflight(struct unix_sock *usk)
{
	atomic_long_inc(&usk->inflight);
}

static void inc_inflight_move_tail(struct unix_sock *u)
{
	atomic_long_inc(&u->inflight);
	/* If this still might be part of a cycle, move it to the end
	 * of the list, so that it's checked even if it was already
	 * passed over
	 */
	if (test_bit(UNIX_GC_MAYBE_CYCLE, &u->gc_flags))
		list_move_tail(&u->link, &gc_candidates);
}

static bool gc_in_progress;
#define UNIX_INFLIGHT_TRIGGER_GC 16000

void wait_for_unix_gc(void)
{
	/* If number of inflight sockets is insane,
	 * force a garbage collect right now.
	 * Paired with the WRITE_ONCE() in unix_inflight(),
	 * unix_notinflight() and gc_in_progress().
	 */
	if (READ_ONCE(unix_tot_inflight) > UNIX_INFLIGHT_TRIGGER_GC &&
	    !READ_ONCE(gc_in_progress))
		unix_gc();
	wait_event(unix_gc_wait, gc_in_progress == false);
}

/* The external entry point: unix_gc() */
void unix_gc(void)
{
	struct sk_buff *next_skb, *skb;
	struct unix_sock *u;
	struct unix_sock *next;
	struct sk_buff_head hitlist;
	struct list_head cursor;
	LIST_HEAD(not_cycle_list);

	spin_lock(&unix_gc_lock);

	/* Avoid a recursive GC. */
	if (gc_in_progress)
		goto out;

	/* Paired with READ_ONCE() in wait_for_unix_gc(). */
	WRITE_ONCE(gc_in_progress, true);

	/* First, select candidates for garbage collection.  Only
	 * in-flight sockets are considered, and from those only ones
	 * which don't have any external reference.
	 *
	 * Holding unix_gc_lock will protect these candidates from
	 * being detached, and hence from gaining an external
	 * reference.  Since there are no possible receivers, all
	 * buffers currently on the candidates' queues stay there
	 * during the garbage collection.
	 *
	 * We also know that no new candidate can be added onto the
	 * receive queues.  Other, non candidate sockets _can_ be
	 * added to queue, so we must make sure only to touch
	 * candidates.
	 */
	list_for_each_entry_safe(u, next, &gc_inflight_list, link) {
		long total_refs;
		long inflight_refs;

		total_refs = file_count(u->sk.sk_socket->file);
		inflight_refs = atomic_long_read(&u->inflight);

		BUG_ON(inflight_refs < 1);
		BUG_ON(total_refs < inflight_refs);
		if (total_refs == inflight_refs) {
			list_move_tail(&u->link, &gc_candidates);
			__set_bit(UNIX_GC_CANDIDATE, &u->gc_flags);
			__set_bit(UNIX_GC_MAYBE_CYCLE, &u->gc_flags);
		}
	}

	/* Now remove all internal in-flight reference to children of
	 * the candidates.
	 */
	list_for_each_entry(u, &gc_candidates, link)
		scan_children(&u->sk, dec_inflight, NULL);

	/* Restore the references for children of all candidates,
	 * which have remaining references.  Do this recursively, so
	 * only those remain, which form cyclic references.
	 *
	 * Use a "cursor" link, to make the list traversal safe, even
	 * though elements might be moved about.
	 */
	list_add(&cursor, &gc_candidates);
	while (cursor.next != &gc_candidates) {
		u = list_entry(cursor.next, struct unix_sock, link);

		/* Move cursor to after the current position. */
		list_move(&cursor, &u->link);

		if (atomic_long_read(&u->inflight) > 0) {
			list_move_tail(&u->link, &not_cycle_list);
			__clear_bit(UNIX_GC_MAYBE_CYCLE, &u->gc_flags);
			scan_children(&u->sk, inc_inflight_move_tail, NULL);
		}
	}
	list_del(&cursor);

	/* Now gc_candidates contains only garbage.  Restore original
	 * inflight counters for these as well, and remove the skbuffs
	 * which are creating the cycle(s).
	 */
	skb_queue_head_init(&hitlist);
	list_for_each_entry(u, &gc_candidates, link)
		scan_children(&u->sk, inc_inflight, &hitlist);

	/* not_cycle_list contains those sockets which do not make up a
	 * cycle.  Restore these to the inflight list.
	 */
	while (!list_empty(&not_cycle_list)) {
		u = list_entry(not_cycle_list.next, struct unix_sock, link);
		__clear_bit(UNIX_GC_CANDIDATE, &u->gc_flags);
		list_move_tail(&u->link, &gc_inflight_list);
	}

	spin_unlock(&unix_gc_lock);

	/* We need io_uring to clean its registered files, ignore all io_uring
	 * originated skbs. It's fine as io_uring doesn't keep references to
	 * other io_uring instances and so killing all other files in the cycle
	 * will put all io_uring references forcing it to go through normal
	 * release.path eventually putting registered files.
	 */
	skb_queue_walk_safe(&hitlist, skb, next_skb) {
		if (skb->destructor == io_uring_destruct_scm) {
			__skb_unlink(skb, &hitlist);
			skb_queue_tail(&skb->sk->sk_receive_queue, skb);
		}
	}

	/* Here we are. Hitlist is filled. Die. */
	__skb_queue_purge(&hitlist);

#if IS_ENABLED(CONFIG_AF_UNIX_OOB)
	while (!list_empty(&gc_candidates)) {
		u = list_entry(gc_candidates.next, struct unix_sock, link);
		if (u->oob_skb) {
			struct sk_buff *skb = u->oob_skb;

			u->oob_skb = NULL;
			kfree_skb(skb);
		}
	}
#endif

	spin_lock(&unix_gc_lock);

	/* There could be io_uring registered files, just push them back to
	 * the inflight list
	 */
	list_for_each_entry_safe(u, next, &gc_candidates, link)
		list_move_tail(&u->link, &gc_inflight_list);

	/* All candidates should have been detached by now. */
	BUG_ON(!list_empty(&gc_candidates));

	/* Paired with READ_ONCE() in wait_for_unix_gc(). */
	WRITE_ONCE(gc_in_progress, false);

	wake_up(&unix_gc_wait);

 out:
	spin_unlock(&unix_gc_lock);
}
