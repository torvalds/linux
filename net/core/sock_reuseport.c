/*
 * To speed up listener socket lookup, create an array to store all sockets
 * listening on the same port.  This allows a decision to be made after finding
 * the first socket.
 */

#include <net/sock_reuseport.h>
#include <linux/rcupdate.h>

#define INIT_SOCKS 128

static DEFINE_SPINLOCK(reuseport_lock);

static struct sock_reuseport *__reuseport_alloc(u16 max_socks)
{
	size_t size = sizeof(struct sock_reuseport) +
		      sizeof(struct sock *) * max_socks;
	struct sock_reuseport *reuse = kzalloc(size, GFP_ATOMIC);

	if (!reuse)
		return NULL;

	reuse->max_socks = max_socks;

	return reuse;
}

int reuseport_alloc(struct sock *sk)
{
	struct sock_reuseport *reuse;

	/* bh lock used since this function call may precede hlist lock in
	 * soft irq of receive path or setsockopt from process context
	 */
	spin_lock_bh(&reuseport_lock);
	WARN_ONCE(rcu_dereference_protected(sk->sk_reuseport_cb,
					    lockdep_is_held(&reuseport_lock)),
		  "multiple allocations for the same socket");
	reuse = __reuseport_alloc(INIT_SOCKS);
	if (!reuse) {
		spin_unlock_bh(&reuseport_lock);
		return -ENOMEM;
	}

	reuse->socks[0] = sk;
	reuse->num_socks = 1;
	rcu_assign_pointer(sk->sk_reuseport_cb, reuse);

	spin_unlock_bh(&reuseport_lock);

	return 0;
}
EXPORT_SYMBOL(reuseport_alloc);

static struct sock_reuseport *reuseport_grow(struct sock_reuseport *reuse)
{
	struct sock_reuseport *more_reuse;
	u32 more_socks_size, i;

	more_socks_size = reuse->max_socks * 2U;
	if (more_socks_size > U16_MAX)
		return NULL;

	more_reuse = __reuseport_alloc(more_socks_size);
	if (!more_reuse)
		return NULL;

	more_reuse->max_socks = more_socks_size;
	more_reuse->num_socks = reuse->num_socks;

	memcpy(more_reuse->socks, reuse->socks,
	       reuse->num_socks * sizeof(struct sock *));

	for (i = 0; i < reuse->num_socks; ++i)
		rcu_assign_pointer(reuse->socks[i]->sk_reuseport_cb,
				   more_reuse);

	kfree_rcu(reuse, rcu);
	return more_reuse;
}

/**
 *  reuseport_add_sock - Add a socket to the reuseport group of another.
 *  @sk:  New socket to add to the group.
 *  @sk2: Socket belonging to the existing reuseport group.
 *  May return ENOMEM and not add socket to group under memory pressure.
 */
int reuseport_add_sock(struct sock *sk, const struct sock *sk2)
{
	struct sock_reuseport *reuse;

	spin_lock_bh(&reuseport_lock);
	reuse = rcu_dereference_protected(sk2->sk_reuseport_cb,
					  lockdep_is_held(&reuseport_lock)),
	WARN_ONCE(rcu_dereference_protected(sk->sk_reuseport_cb,
					    lockdep_is_held(&reuseport_lock)),
		  "socket already in reuseport group");

	if (reuse->num_socks == reuse->max_socks) {
		reuse = reuseport_grow(reuse);
		if (!reuse) {
			spin_unlock_bh(&reuseport_lock);
			return -ENOMEM;
		}
	}

	reuse->socks[reuse->num_socks] = sk;
	/* paired with smp_rmb() in reuseport_select_sock() */
	smp_wmb();
	reuse->num_socks++;
	rcu_assign_pointer(sk->sk_reuseport_cb, reuse);

	spin_unlock_bh(&reuseport_lock);

	return 0;
}
EXPORT_SYMBOL(reuseport_add_sock);

void reuseport_detach_sock(struct sock *sk)
{
	struct sock_reuseport *reuse;
	int i;

	spin_lock_bh(&reuseport_lock);
	reuse = rcu_dereference_protected(sk->sk_reuseport_cb,
					  lockdep_is_held(&reuseport_lock));
	rcu_assign_pointer(sk->sk_reuseport_cb, NULL);

	for (i = 0; i < reuse->num_socks; i++) {
		if (reuse->socks[i] == sk) {
			reuse->socks[i] = reuse->socks[reuse->num_socks - 1];
			reuse->num_socks--;
			if (reuse->num_socks == 0)
				kfree_rcu(reuse, rcu);
			break;
		}
	}
	spin_unlock_bh(&reuseport_lock);
}
EXPORT_SYMBOL(reuseport_detach_sock);

/**
 *  reuseport_select_sock - Select a socket from an SO_REUSEPORT group.
 *  @sk: First socket in the group.
 *  @hash: Use this hash to select.
 *  Returns a socket that should receive the packet (or NULL on error).
 */
struct sock *reuseport_select_sock(struct sock *sk, u32 hash)
{
	struct sock_reuseport *reuse;
	struct sock *sk2 = NULL;
	u16 socks;

	rcu_read_lock();
	reuse = rcu_dereference(sk->sk_reuseport_cb);

	/* if memory allocation failed or add call is not yet complete */
	if (!reuse)
		goto out;

	socks = READ_ONCE(reuse->num_socks);
	if (likely(socks)) {
		/* paired with smp_wmb() in reuseport_add_sock() */
		smp_rmb();

		sk2 = reuse->socks[reciprocal_scale(hash, socks)];
	}

out:
	rcu_read_unlock();
	return sk2;
}
EXPORT_SYMBOL(reuseport_select_sock);
