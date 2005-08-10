/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic INET transport hashtables
 *
 * Authors:	Lotsa people, from code originally in tcp
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <net/inet_hashtables.h>

/*
 * Allocate and initialize a new local port bind bucket.
 * The bindhash mutex for snum's hash chain must be held here.
 */
struct inet_bind_bucket *inet_bind_bucket_create(kmem_cache_t *cachep,
						 struct inet_bind_hashbucket *head,
						 const unsigned short snum)
{
	struct inet_bind_bucket *tb = kmem_cache_alloc(cachep, SLAB_ATOMIC);

	if (tb != NULL) {
		tb->port      = snum;
		tb->fastreuse = 0;
		INIT_HLIST_HEAD(&tb->owners);
		hlist_add_head(&tb->node, &head->chain);
	}
	return tb;
}

EXPORT_SYMBOL(inet_bind_bucket_create);

/*
 * Caller must hold hashbucket lock for this tb with local BH disabled
 */
void inet_bind_bucket_destroy(kmem_cache_t *cachep, struct inet_bind_bucket *tb)
{
	if (hlist_empty(&tb->owners)) {
		__hlist_del(&tb->node);
		kmem_cache_free(cachep, tb);
	}
}

void inet_bind_hash(struct sock *sk, struct inet_bind_bucket *tb,
		    const unsigned short snum)
{
	struct inet_sock *inet = inet_sk(sk);
	inet->num	= snum;
	sk_add_bind_node(sk, &tb->owners);
	inet->bind_hash	= tb;
}

EXPORT_SYMBOL(inet_bind_hash);

/*
 * Get rid of any references to a local port held by the given sock.
 */
static void __inet_put_port(struct inet_hashinfo *hashinfo, struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);
	const int bhash = inet_bhashfn(inet->num, hashinfo->bhash_size);
	struct inet_bind_hashbucket *head = &hashinfo->bhash[bhash];
	struct inet_bind_bucket *tb;

	spin_lock(&head->lock);
	tb = inet->bind_hash;
	__sk_del_bind_node(sk);
	inet->bind_hash = NULL;
	inet->num = 0;
	inet_bind_bucket_destroy(hashinfo->bind_bucket_cachep, tb);
	spin_unlock(&head->lock);
}

void inet_put_port(struct inet_hashinfo *hashinfo, struct sock *sk)
{
	local_bh_disable();
	__inet_put_port(hashinfo, sk);
	local_bh_enable();
}

EXPORT_SYMBOL(inet_put_port);

/*
 * This lock without WQ_FLAG_EXCLUSIVE is good on UP and it can be very bad on SMP.
 * Look, when several writers sleep and reader wakes them up, all but one
 * immediately hit write lock and grab all the cpus. Exclusive sleep solves
 * this, _but_ remember, it adds useless work on UP machines (wake up each
 * exclusive lock release). It should be ifdefed really.
 */
void inet_listen_wlock(struct inet_hashinfo *hashinfo)
{
	write_lock(&hashinfo->lhash_lock);

	if (atomic_read(&hashinfo->lhash_users)) {
		DEFINE_WAIT(wait);

		for (;;) {
			prepare_to_wait_exclusive(&hashinfo->lhash_wait,
						  &wait, TASK_UNINTERRUPTIBLE);
			if (!atomic_read(&hashinfo->lhash_users))
				break;
			write_unlock_bh(&hashinfo->lhash_lock);
			schedule();
			write_lock_bh(&hashinfo->lhash_lock);
		}

		finish_wait(&hashinfo->lhash_wait, &wait);
	}
}

EXPORT_SYMBOL(inet_listen_wlock);
