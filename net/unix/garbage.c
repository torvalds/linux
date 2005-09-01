/*
 * NET3:	Garbage Collector For AF_UNIX sockets
 *
 * Garbage Collector:
 *	Copyright (C) Barak A. Pearlmutter.
 *	Released under the GPL version 2 or later.
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
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
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
 */
 
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/net.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/file.h>
#include <linux/proc_fs.h>

#include <net/sock.h>
#include <net/af_unix.h>
#include <net/scm.h>
#include <net/tcp_states.h>

/* Internal data structures and random procedures: */

#define GC_HEAD		((struct sock *)(-1))
#define GC_ORPHAN	((struct sock *)(-3))

static struct sock *gc_current = GC_HEAD; /* stack of objects to mark */

atomic_t unix_tot_inflight = ATOMIC_INIT(0);


static struct sock *unix_get_socket(struct file *filp)
{
	struct sock *u_sock = NULL;
	struct inode *inode = filp->f_dentry->d_inode;

	/*
	 *	Socket ?
	 */
	if (S_ISSOCK(inode->i_mode)) {
		struct socket * sock = SOCKET_I(inode);
		struct sock * s = sock->sk;

		/*
		 *	PF_UNIX ?
		 */
		if (s && sock->ops && sock->ops->family == PF_UNIX)
			u_sock = s;
	}
	return u_sock;
}

/*
 *	Keep the number of times in flight count for the file
 *	descriptor if it is for an AF_UNIX socket.
 */
 
void unix_inflight(struct file *fp)
{
	struct sock *s = unix_get_socket(fp);
	if(s) {
		atomic_inc(&unix_sk(s)->inflight);
		atomic_inc(&unix_tot_inflight);
	}
}

void unix_notinflight(struct file *fp)
{
	struct sock *s = unix_get_socket(fp);
	if(s) {
		atomic_dec(&unix_sk(s)->inflight);
		atomic_dec(&unix_tot_inflight);
	}
}


/*
 *	Garbage Collector Support Functions
 */

static inline struct sock *pop_stack(void)
{
	struct sock *p = gc_current;
	gc_current = unix_sk(p)->gc_tree;
	return p;
}

static inline int empty_stack(void)
{
	return gc_current == GC_HEAD;
}

static void maybe_unmark_and_push(struct sock *x)
{
	struct unix_sock *u = unix_sk(x);

	if (u->gc_tree != GC_ORPHAN)
		return;
	sock_hold(x);
	u->gc_tree = gc_current;
	gc_current = x;
}


/* The external entry point: unix_gc() */

void unix_gc(void)
{
	static DECLARE_MUTEX(unix_gc_sem);
	int i;
	struct sock *s;
	struct sk_buff_head hitlist;
	struct sk_buff *skb;

	/*
	 *	Avoid a recursive GC.
	 */

	if (down_trylock(&unix_gc_sem))
		return;

	read_lock(&unix_table_lock);

	forall_unix_sockets(i, s)
	{
		unix_sk(s)->gc_tree = GC_ORPHAN;
	}
	/*
	 *	Everything is now marked 
	 */

	/* Invariant to be maintained:
		- everything unmarked is either:
		-- (a) on the stack, or
		-- (b) has all of its children unmarked
		- everything on the stack is always unmarked
		- nothing is ever pushed onto the stack twice, because:
		-- nothing previously unmarked is ever pushed on the stack
	 */

	/*
	 *	Push root set
	 */

	forall_unix_sockets(i, s)
	{
		int open_count = 0;

		/*
		 *	If all instances of the descriptor are not
		 *	in flight we are in use.
		 *
		 *	Special case: when socket s is embrion, it may be
		 *	hashed but still not in queue of listening socket.
		 *	In this case (see unix_create1()) we set artificial
		 *	negative inflight counter to close race window.
		 *	It is trick of course and dirty one.
		 */
		if (s->sk_socket && s->sk_socket->file)
			open_count = file_count(s->sk_socket->file);
		if (open_count > atomic_read(&unix_sk(s)->inflight))
			maybe_unmark_and_push(s);
	}

	/*
	 *	Mark phase 
	 */

	while (!empty_stack())
	{
		struct sock *x = pop_stack();
		struct sock *sk;

		spin_lock(&x->sk_receive_queue.lock);
		skb = skb_peek(&x->sk_receive_queue);
		
		/*
		 *	Loop through all but first born 
		 */
		
		while (skb && skb != (struct sk_buff *)&x->sk_receive_queue) {
			/*
			 *	Do we have file descriptors ?
			 */
			if(UNIXCB(skb).fp)
			{
				/*
				 *	Process the descriptors of this socket
				 */
				int nfd=UNIXCB(skb).fp->count;
				struct file **fp = UNIXCB(skb).fp->fp;
				while(nfd--)
				{
					/*
					 *	Get the socket the fd matches if
					 *	it indeed does so
					 */
					if((sk=unix_get_socket(*fp++))!=NULL)
					{
						maybe_unmark_and_push(sk);
					}
				}
			}
			/* We have to scan not-yet-accepted ones too */
			if (x->sk_state == TCP_LISTEN)
				maybe_unmark_and_push(skb->sk);
			skb=skb->next;
		}
		spin_unlock(&x->sk_receive_queue.lock);
		sock_put(x);
	}

	skb_queue_head_init(&hitlist);

	forall_unix_sockets(i, s)
	{
		struct unix_sock *u = unix_sk(s);

		if (u->gc_tree == GC_ORPHAN) {
			struct sk_buff *nextsk;

			spin_lock(&s->sk_receive_queue.lock);
			skb = skb_peek(&s->sk_receive_queue);
			while (skb &&
			       skb != (struct sk_buff *)&s->sk_receive_queue) {
				nextsk = skb->next;
				/*
				 *	Do we have file descriptors ?
				 */
				if (UNIXCB(skb).fp) {
					__skb_unlink(skb,
						     &s->sk_receive_queue);
					__skb_queue_tail(&hitlist, skb);
				}
				skb = nextsk;
			}
			spin_unlock(&s->sk_receive_queue.lock);
		}
		u->gc_tree = GC_ORPHAN;
	}
	read_unlock(&unix_table_lock);

	/*
	 *	Here we are. Hitlist is filled. Die.
	 */

	__skb_queue_purge(&hitlist);
	up(&unix_gc_sem);
}
