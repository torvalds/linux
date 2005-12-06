/*
 * NETLINK      Kernel-user communication protocol.
 *
 * 		Authors:	Alan Cox <alan@redhat.com>
 * 				Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 * 
 * Tue Jun 26 14:36:48 MEST 2001 Herbert "herp" Rosmanith
 *                               added netlink_proto_exit
 * Tue Jan 22 18:32:44 BRST 2002 Arnaldo C. de Melo <acme@conectiva.com.br>
 * 				 use nlk_sk, as sk->protinfo is on a diet 8)
 * Fri Jul 22 19:51:12 MEST 2005 Harald Welte <laforge@gnumonks.org>
 * 				 - inc module use count of module that owns
 * 				   the kernel socket in case userspace opens
 * 				   socket of same protocol
 * 				 - remove all module support, since netlink is
 * 				   mandatory if CONFIG_NET=y these days
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/smp_lock.h>
#include <linux/notifier.h>
#include <linux/security.h>
#include <linux/jhash.h>
#include <linux/jiffies.h>
#include <linux/random.h>
#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/audit.h>

#include <net/sock.h>
#include <net/scm.h>
#include <net/netlink.h>

#define Nprintk(a...)
#define NLGRPSZ(x)	(ALIGN(x, sizeof(unsigned long) * 8) / 8)

struct netlink_sock {
	/* struct sock has to be the first member of netlink_sock */
	struct sock		sk;
	u32			pid;
	u32			dst_pid;
	u32			dst_group;
	u32			flags;
	u32			subscriptions;
	u32			ngroups;
	unsigned long		*groups;
	unsigned long		state;
	wait_queue_head_t	wait;
	struct netlink_callback	*cb;
	spinlock_t		cb_lock;
	void			(*data_ready)(struct sock *sk, int bytes);
	struct module		*module;
};

#define NETLINK_KERNEL_SOCKET	0x1
#define NETLINK_RECV_PKTINFO	0x2

static inline struct netlink_sock *nlk_sk(struct sock *sk)
{
	return (struct netlink_sock *)sk;
}

struct nl_pid_hash {
	struct hlist_head *table;
	unsigned long rehash_time;

	unsigned int mask;
	unsigned int shift;

	unsigned int entries;
	unsigned int max_shift;

	u32 rnd;
};

struct netlink_table {
	struct nl_pid_hash hash;
	struct hlist_head mc_list;
	unsigned int nl_nonroot;
	unsigned int groups;
	struct module *module;
	int registered;
};

static struct netlink_table *nl_table;

static DECLARE_WAIT_QUEUE_HEAD(nl_table_wait);

static int netlink_dump(struct sock *sk);
static void netlink_destroy_callback(struct netlink_callback *cb);

static DEFINE_RWLOCK(nl_table_lock);
static atomic_t nl_table_users = ATOMIC_INIT(0);

static struct notifier_block *netlink_chain;

static u32 netlink_group_mask(u32 group)
{
	return group ? 1 << (group - 1) : 0;
}

static struct hlist_head *nl_pid_hashfn(struct nl_pid_hash *hash, u32 pid)
{
	return &hash->table[jhash_1word(pid, hash->rnd) & hash->mask];
}

static void netlink_sock_destruct(struct sock *sk)
{
	skb_queue_purge(&sk->sk_receive_queue);

	if (!sock_flag(sk, SOCK_DEAD)) {
		printk("Freeing alive netlink socket %p\n", sk);
		return;
	}
	BUG_TRAP(!atomic_read(&sk->sk_rmem_alloc));
	BUG_TRAP(!atomic_read(&sk->sk_wmem_alloc));
	BUG_TRAP(!nlk_sk(sk)->cb);
	BUG_TRAP(!nlk_sk(sk)->groups);
}

/* This lock without WQ_FLAG_EXCLUSIVE is good on UP and it is _very_ bad on SMP.
 * Look, when several writers sleep and reader wakes them up, all but one
 * immediately hit write lock and grab all the cpus. Exclusive sleep solves
 * this, _but_ remember, it adds useless work on UP machines.
 */

static void netlink_table_grab(void)
{
	write_lock_bh(&nl_table_lock);

	if (atomic_read(&nl_table_users)) {
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue_exclusive(&nl_table_wait, &wait);
		for(;;) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			if (atomic_read(&nl_table_users) == 0)
				break;
			write_unlock_bh(&nl_table_lock);
			schedule();
			write_lock_bh(&nl_table_lock);
		}

		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&nl_table_wait, &wait);
	}
}

static __inline__ void netlink_table_ungrab(void)
{
	write_unlock_bh(&nl_table_lock);
	wake_up(&nl_table_wait);
}

static __inline__ void
netlink_lock_table(void)
{
	/* read_lock() synchronizes us to netlink_table_grab */

	read_lock(&nl_table_lock);
	atomic_inc(&nl_table_users);
	read_unlock(&nl_table_lock);
}

static __inline__ void
netlink_unlock_table(void)
{
	if (atomic_dec_and_test(&nl_table_users))
		wake_up(&nl_table_wait);
}

static __inline__ struct sock *netlink_lookup(int protocol, u32 pid)
{
	struct nl_pid_hash *hash = &nl_table[protocol].hash;
	struct hlist_head *head;
	struct sock *sk;
	struct hlist_node *node;

	read_lock(&nl_table_lock);
	head = nl_pid_hashfn(hash, pid);
	sk_for_each(sk, node, head) {
		if (nlk_sk(sk)->pid == pid) {
			sock_hold(sk);
			goto found;
		}
	}
	sk = NULL;
found:
	read_unlock(&nl_table_lock);
	return sk;
}

static inline struct hlist_head *nl_pid_hash_alloc(size_t size)
{
	if (size <= PAGE_SIZE)
		return kmalloc(size, GFP_ATOMIC);
	else
		return (struct hlist_head *)
			__get_free_pages(GFP_ATOMIC, get_order(size));
}

static inline void nl_pid_hash_free(struct hlist_head *table, size_t size)
{
	if (size <= PAGE_SIZE)
		kfree(table);
	else
		free_pages((unsigned long)table, get_order(size));
}

static int nl_pid_hash_rehash(struct nl_pid_hash *hash, int grow)
{
	unsigned int omask, mask, shift;
	size_t osize, size;
	struct hlist_head *otable, *table;
	int i;

	omask = mask = hash->mask;
	osize = size = (mask + 1) * sizeof(*table);
	shift = hash->shift;

	if (grow) {
		if (++shift > hash->max_shift)
			return 0;
		mask = mask * 2 + 1;
		size *= 2;
	}

	table = nl_pid_hash_alloc(size);
	if (!table)
		return 0;

	memset(table, 0, size);
	otable = hash->table;
	hash->table = table;
	hash->mask = mask;
	hash->shift = shift;
	get_random_bytes(&hash->rnd, sizeof(hash->rnd));

	for (i = 0; i <= omask; i++) {
		struct sock *sk;
		struct hlist_node *node, *tmp;

		sk_for_each_safe(sk, node, tmp, &otable[i])
			__sk_add_node(sk, nl_pid_hashfn(hash, nlk_sk(sk)->pid));
	}

	nl_pid_hash_free(otable, osize);
	hash->rehash_time = jiffies + 10 * 60 * HZ;
	return 1;
}

static inline int nl_pid_hash_dilute(struct nl_pid_hash *hash, int len)
{
	int avg = hash->entries >> hash->shift;

	if (unlikely(avg > 1) && nl_pid_hash_rehash(hash, 1))
		return 1;

	if (unlikely(len > avg) && time_after(jiffies, hash->rehash_time)) {
		nl_pid_hash_rehash(hash, 0);
		return 1;
	}

	return 0;
}

static struct proto_ops netlink_ops;

static int netlink_insert(struct sock *sk, u32 pid)
{
	struct nl_pid_hash *hash = &nl_table[sk->sk_protocol].hash;
	struct hlist_head *head;
	int err = -EADDRINUSE;
	struct sock *osk;
	struct hlist_node *node;
	int len;

	netlink_table_grab();
	head = nl_pid_hashfn(hash, pid);
	len = 0;
	sk_for_each(osk, node, head) {
		if (nlk_sk(osk)->pid == pid)
			break;
		len++;
	}
	if (node)
		goto err;

	err = -EBUSY;
	if (nlk_sk(sk)->pid)
		goto err;

	err = -ENOMEM;
	if (BITS_PER_LONG > 32 && unlikely(hash->entries >= UINT_MAX))
		goto err;

	if (len && nl_pid_hash_dilute(hash, len))
		head = nl_pid_hashfn(hash, pid);
	hash->entries++;
	nlk_sk(sk)->pid = pid;
	sk_add_node(sk, head);
	err = 0;

err:
	netlink_table_ungrab();
	return err;
}

static void netlink_remove(struct sock *sk)
{
	netlink_table_grab();
	if (sk_del_node_init(sk))
		nl_table[sk->sk_protocol].hash.entries--;
	if (nlk_sk(sk)->subscriptions)
		__sk_del_bind_node(sk);
	netlink_table_ungrab();
}

static struct proto netlink_proto = {
	.name	  = "NETLINK",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct netlink_sock),
};

static int __netlink_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct netlink_sock *nlk;

	sock->ops = &netlink_ops;

	sk = sk_alloc(PF_NETLINK, GFP_KERNEL, &netlink_proto, 1);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	nlk = nlk_sk(sk);
	spin_lock_init(&nlk->cb_lock);
	init_waitqueue_head(&nlk->wait);

	sk->sk_destruct = netlink_sock_destruct;
	sk->sk_protocol = protocol;
	return 0;
}

static int netlink_create(struct socket *sock, int protocol)
{
	struct module *module = NULL;
	struct netlink_sock *nlk;
	unsigned int groups;
	int err = 0;

	sock->state = SS_UNCONNECTED;

	if (sock->type != SOCK_RAW && sock->type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	if (protocol<0 || protocol >= MAX_LINKS)
		return -EPROTONOSUPPORT;

	netlink_lock_table();
#ifdef CONFIG_KMOD
	if (!nl_table[protocol].registered) {
		netlink_unlock_table();
		request_module("net-pf-%d-proto-%d", PF_NETLINK, protocol);
		netlink_lock_table();
	}
#endif
	if (nl_table[protocol].registered &&
	    try_module_get(nl_table[protocol].module))
		module = nl_table[protocol].module;
	groups = nl_table[protocol].groups;
	netlink_unlock_table();

	if ((err = __netlink_create(sock, protocol) < 0))
		goto out_module;

	nlk = nlk_sk(sock->sk);
	nlk->module = module;
out:
	return err;

out_module:
	module_put(module);
	goto out;
}

static int netlink_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct netlink_sock *nlk;

	if (!sk)
		return 0;

	netlink_remove(sk);
	nlk = nlk_sk(sk);

	spin_lock(&nlk->cb_lock);
	if (nlk->cb) {
		if (nlk->cb->done)
			nlk->cb->done(nlk->cb);
		netlink_destroy_callback(nlk->cb);
		nlk->cb = NULL;
	}
	spin_unlock(&nlk->cb_lock);

	/* OK. Socket is unlinked, and, therefore,
	   no new packets will arrive */

	sock_orphan(sk);
	sock->sk = NULL;
	wake_up_interruptible_all(&nlk->wait);

	skb_queue_purge(&sk->sk_write_queue);

	if (nlk->pid && !nlk->subscriptions) {
		struct netlink_notify n = {
						.protocol = sk->sk_protocol,
						.pid = nlk->pid,
					  };
		notifier_call_chain(&netlink_chain, NETLINK_URELEASE, &n);
	}	

	if (nlk->module)
		module_put(nlk->module);

	if (nlk->flags & NETLINK_KERNEL_SOCKET) {
		netlink_table_grab();
		nl_table[sk->sk_protocol].module = NULL;
		nl_table[sk->sk_protocol].registered = 0;
		netlink_table_ungrab();
	}

	kfree(nlk->groups);
	nlk->groups = NULL;

	sock_put(sk);
	return 0;
}

static int netlink_autobind(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct nl_pid_hash *hash = &nl_table[sk->sk_protocol].hash;
	struct hlist_head *head;
	struct sock *osk;
	struct hlist_node *node;
	s32 pid = current->tgid;
	int err;
	static s32 rover = -4097;

retry:
	cond_resched();
	netlink_table_grab();
	head = nl_pid_hashfn(hash, pid);
	sk_for_each(osk, node, head) {
		if (nlk_sk(osk)->pid == pid) {
			/* Bind collision, search negative pid values. */
			pid = rover--;
			if (rover > -4097)
				rover = -4097;
			netlink_table_ungrab();
			goto retry;
		}
	}
	netlink_table_ungrab();

	err = netlink_insert(sk, pid);
	if (err == -EADDRINUSE)
		goto retry;

	/* If 2 threads race to autobind, that is fine.  */
	if (err == -EBUSY)
		err = 0;

	return err;
}

static inline int netlink_capable(struct socket *sock, unsigned int flag) 
{ 
	return (nl_table[sock->sk->sk_protocol].nl_nonroot & flag) ||
	       capable(CAP_NET_ADMIN);
} 

static void
netlink_update_subscriptions(struct sock *sk, unsigned int subscriptions)
{
	struct netlink_sock *nlk = nlk_sk(sk);

	if (nlk->subscriptions && !subscriptions)
		__sk_del_bind_node(sk);
	else if (!nlk->subscriptions && subscriptions)
		sk_add_bind_node(sk, &nl_table[sk->sk_protocol].mc_list);
	nlk->subscriptions = subscriptions;
}

static int netlink_alloc_groups(struct sock *sk)
{
	struct netlink_sock *nlk = nlk_sk(sk);
	unsigned int groups;
	int err = 0;

	netlink_lock_table();
	groups = nl_table[sk->sk_protocol].groups;
	if (!nl_table[sk->sk_protocol].registered)
		err = -ENOENT;
	netlink_unlock_table();

	if (err)
		return err;

	nlk->groups = kmalloc(NLGRPSZ(groups), GFP_KERNEL);
	if (nlk->groups == NULL)
		return -ENOMEM;
	memset(nlk->groups, 0, NLGRPSZ(groups));
	nlk->ngroups = groups;
	return 0;
}

static int netlink_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct netlink_sock *nlk = nlk_sk(sk);
	struct sockaddr_nl *nladdr = (struct sockaddr_nl *)addr;
	int err;
	
	if (nladdr->nl_family != AF_NETLINK)
		return -EINVAL;

	/* Only superuser is allowed to listen multicasts */
	if (nladdr->nl_groups) {
		if (!netlink_capable(sock, NL_NONROOT_RECV))
			return -EPERM;
		if (nlk->groups == NULL) {
			err = netlink_alloc_groups(sk);
			if (err)
				return err;
		}
	}

	if (nlk->pid) {
		if (nladdr->nl_pid != nlk->pid)
			return -EINVAL;
	} else {
		err = nladdr->nl_pid ?
			netlink_insert(sk, nladdr->nl_pid) :
			netlink_autobind(sock);
		if (err)
			return err;
	}

	if (!nladdr->nl_groups && (nlk->groups == NULL || !(u32)nlk->groups[0]))
		return 0;

	netlink_table_grab();
	netlink_update_subscriptions(sk, nlk->subscriptions +
	                                 hweight32(nladdr->nl_groups) -
	                                 hweight32(nlk->groups[0]));
	nlk->groups[0] = (nlk->groups[0] & ~0xffffffffUL) | nladdr->nl_groups; 
	netlink_table_ungrab();

	return 0;
}

static int netlink_connect(struct socket *sock, struct sockaddr *addr,
			   int alen, int flags)
{
	int err = 0;
	struct sock *sk = sock->sk;
	struct netlink_sock *nlk = nlk_sk(sk);
	struct sockaddr_nl *nladdr=(struct sockaddr_nl*)addr;

	if (addr->sa_family == AF_UNSPEC) {
		sk->sk_state	= NETLINK_UNCONNECTED;
		nlk->dst_pid	= 0;
		nlk->dst_group  = 0;
		return 0;
	}
	if (addr->sa_family != AF_NETLINK)
		return -EINVAL;

	/* Only superuser is allowed to send multicasts */
	if (nladdr->nl_groups && !netlink_capable(sock, NL_NONROOT_SEND))
		return -EPERM;

	if (!nlk->pid)
		err = netlink_autobind(sock);

	if (err == 0) {
		sk->sk_state	= NETLINK_CONNECTED;
		nlk->dst_pid 	= nladdr->nl_pid;
		nlk->dst_group  = ffs(nladdr->nl_groups);
	}

	return err;
}

static int netlink_getname(struct socket *sock, struct sockaddr *addr, int *addr_len, int peer)
{
	struct sock *sk = sock->sk;
	struct netlink_sock *nlk = nlk_sk(sk);
	struct sockaddr_nl *nladdr=(struct sockaddr_nl *)addr;
	
	nladdr->nl_family = AF_NETLINK;
	nladdr->nl_pad = 0;
	*addr_len = sizeof(*nladdr);

	if (peer) {
		nladdr->nl_pid = nlk->dst_pid;
		nladdr->nl_groups = netlink_group_mask(nlk->dst_group);
	} else {
		nladdr->nl_pid = nlk->pid;
		nladdr->nl_groups = nlk->groups ? nlk->groups[0] : 0;
	}
	return 0;
}

static void netlink_overrun(struct sock *sk)
{
	if (!test_and_set_bit(0, &nlk_sk(sk)->state)) {
		sk->sk_err = ENOBUFS;
		sk->sk_error_report(sk);
	}
}

static struct sock *netlink_getsockbypid(struct sock *ssk, u32 pid)
{
	int protocol = ssk->sk_protocol;
	struct sock *sock;
	struct netlink_sock *nlk;

	sock = netlink_lookup(protocol, pid);
	if (!sock)
		return ERR_PTR(-ECONNREFUSED);

	/* Don't bother queuing skb if kernel socket has no input function */
	nlk = nlk_sk(sock);
	if ((nlk->pid == 0 && !nlk->data_ready) ||
	    (sock->sk_state == NETLINK_CONNECTED &&
	     nlk->dst_pid != nlk_sk(ssk)->pid)) {
		sock_put(sock);
		return ERR_PTR(-ECONNREFUSED);
	}
	return sock;
}

struct sock *netlink_getsockbyfilp(struct file *filp)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct sock *sock;

	if (!S_ISSOCK(inode->i_mode))
		return ERR_PTR(-ENOTSOCK);

	sock = SOCKET_I(inode)->sk;
	if (sock->sk_family != AF_NETLINK)
		return ERR_PTR(-EINVAL);

	sock_hold(sock);
	return sock;
}

/*
 * Attach a skb to a netlink socket.
 * The caller must hold a reference to the destination socket. On error, the
 * reference is dropped. The skb is not send to the destination, just all
 * all error checks are performed and memory in the queue is reserved.
 * Return values:
 * < 0: error. skb freed, reference to sock dropped.
 * 0: continue
 * 1: repeat lookup - reference dropped while waiting for socket memory.
 */
int netlink_attachskb(struct sock *sk, struct sk_buff *skb, int nonblock, long timeo)
{
	struct netlink_sock *nlk;

	nlk = nlk_sk(sk);

	if (atomic_read(&sk->sk_rmem_alloc) > sk->sk_rcvbuf ||
	    test_bit(0, &nlk->state)) {
		DECLARE_WAITQUEUE(wait, current);
		if (!timeo) {
			if (!nlk->pid)
				netlink_overrun(sk);
			sock_put(sk);
			kfree_skb(skb);
			return -EAGAIN;
		}

		__set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&nlk->wait, &wait);

		if ((atomic_read(&sk->sk_rmem_alloc) > sk->sk_rcvbuf ||
		     test_bit(0, &nlk->state)) &&
		    !sock_flag(sk, SOCK_DEAD))
			timeo = schedule_timeout(timeo);

		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&nlk->wait, &wait);
		sock_put(sk);

		if (signal_pending(current)) {
			kfree_skb(skb);
			return sock_intr_errno(timeo);
		}
		return 1;
	}
	skb_set_owner_r(skb, sk);
	return 0;
}

int netlink_sendskb(struct sock *sk, struct sk_buff *skb, int protocol)
{
	int len = skb->len;

	skb_queue_tail(&sk->sk_receive_queue, skb);
	sk->sk_data_ready(sk, len);
	sock_put(sk);
	return len;
}

void netlink_detachskb(struct sock *sk, struct sk_buff *skb)
{
	kfree_skb(skb);
	sock_put(sk);
}

static inline struct sk_buff *netlink_trim(struct sk_buff *skb,
					   gfp_t allocation)
{
	int delta;

	skb_orphan(skb);

	delta = skb->end - skb->tail;
	if (delta * 2 < skb->truesize)
		return skb;

	if (skb_shared(skb)) {
		struct sk_buff *nskb = skb_clone(skb, allocation);
		if (!nskb)
			return skb;
		kfree_skb(skb);
		skb = nskb;
	}

	if (!pskb_expand_head(skb, 0, -delta, allocation))
		skb->truesize -= delta;

	return skb;
}

int netlink_unicast(struct sock *ssk, struct sk_buff *skb, u32 pid, int nonblock)
{
	struct sock *sk;
	int err;
	long timeo;

	skb = netlink_trim(skb, gfp_any());

	timeo = sock_sndtimeo(ssk, nonblock);
retry:
	sk = netlink_getsockbypid(ssk, pid);
	if (IS_ERR(sk)) {
		kfree_skb(skb);
		return PTR_ERR(sk);
	}
	err = netlink_attachskb(sk, skb, nonblock, timeo);
	if (err == 1)
		goto retry;
	if (err)
		return err;

	return netlink_sendskb(sk, skb, ssk->sk_protocol);
}

static __inline__ int netlink_broadcast_deliver(struct sock *sk, struct sk_buff *skb)
{
	struct netlink_sock *nlk = nlk_sk(sk);

	if (atomic_read(&sk->sk_rmem_alloc) <= sk->sk_rcvbuf &&
	    !test_bit(0, &nlk->state)) {
		skb_set_owner_r(skb, sk);
		skb_queue_tail(&sk->sk_receive_queue, skb);
		sk->sk_data_ready(sk, skb->len);
		return atomic_read(&sk->sk_rmem_alloc) > sk->sk_rcvbuf;
	}
	return -1;
}

struct netlink_broadcast_data {
	struct sock *exclude_sk;
	u32 pid;
	u32 group;
	int failure;
	int congested;
	int delivered;
	gfp_t allocation;
	struct sk_buff *skb, *skb2;
};

static inline int do_one_broadcast(struct sock *sk,
				   struct netlink_broadcast_data *p)
{
	struct netlink_sock *nlk = nlk_sk(sk);
	int val;

	if (p->exclude_sk == sk)
		goto out;

	if (nlk->pid == p->pid || p->group - 1 >= nlk->ngroups ||
	    !test_bit(p->group - 1, nlk->groups))
		goto out;

	if (p->failure) {
		netlink_overrun(sk);
		goto out;
	}

	sock_hold(sk);
	if (p->skb2 == NULL) {
		if (skb_shared(p->skb)) {
			p->skb2 = skb_clone(p->skb, p->allocation);
		} else {
			p->skb2 = skb_get(p->skb);
			/*
			 * skb ownership may have been set when
			 * delivered to a previous socket.
			 */
			skb_orphan(p->skb2);
		}
	}
	if (p->skb2 == NULL) {
		netlink_overrun(sk);
		/* Clone failed. Notify ALL listeners. */
		p->failure = 1;
	} else if ((val = netlink_broadcast_deliver(sk, p->skb2)) < 0) {
		netlink_overrun(sk);
	} else {
		p->congested |= val;
		p->delivered = 1;
		p->skb2 = NULL;
	}
	sock_put(sk);

out:
	return 0;
}

int netlink_broadcast(struct sock *ssk, struct sk_buff *skb, u32 pid,
		      u32 group, gfp_t allocation)
{
	struct netlink_broadcast_data info;
	struct hlist_node *node;
	struct sock *sk;

	skb = netlink_trim(skb, allocation);

	info.exclude_sk = ssk;
	info.pid = pid;
	info.group = group;
	info.failure = 0;
	info.congested = 0;
	info.delivered = 0;
	info.allocation = allocation;
	info.skb = skb;
	info.skb2 = NULL;

	/* While we sleep in clone, do not allow to change socket list */

	netlink_lock_table();

	sk_for_each_bound(sk, node, &nl_table[ssk->sk_protocol].mc_list)
		do_one_broadcast(sk, &info);

	kfree_skb(skb);

	netlink_unlock_table();

	if (info.skb2)
		kfree_skb(info.skb2);

	if (info.delivered) {
		if (info.congested && (allocation & __GFP_WAIT))
			yield();
		return 0;
	}
	if (info.failure)
		return -ENOBUFS;
	return -ESRCH;
}

struct netlink_set_err_data {
	struct sock *exclude_sk;
	u32 pid;
	u32 group;
	int code;
};

static inline int do_one_set_err(struct sock *sk,
				 struct netlink_set_err_data *p)
{
	struct netlink_sock *nlk = nlk_sk(sk);

	if (sk == p->exclude_sk)
		goto out;

	if (nlk->pid == p->pid || p->group - 1 >= nlk->ngroups ||
	    !test_bit(p->group - 1, nlk->groups))
		goto out;

	sk->sk_err = p->code;
	sk->sk_error_report(sk);
out:
	return 0;
}

void netlink_set_err(struct sock *ssk, u32 pid, u32 group, int code)
{
	struct netlink_set_err_data info;
	struct hlist_node *node;
	struct sock *sk;

	info.exclude_sk = ssk;
	info.pid = pid;
	info.group = group;
	info.code = code;

	read_lock(&nl_table_lock);

	sk_for_each_bound(sk, node, &nl_table[ssk->sk_protocol].mc_list)
		do_one_set_err(sk, &info);

	read_unlock(&nl_table_lock);
}

static int netlink_setsockopt(struct socket *sock, int level, int optname,
                              char __user *optval, int optlen)
{
	struct sock *sk = sock->sk;
	struct netlink_sock *nlk = nlk_sk(sk);
	int val = 0, err;

	if (level != SOL_NETLINK)
		return -ENOPROTOOPT;

	if (optlen >= sizeof(int) &&
	    get_user(val, (int __user *)optval))
		return -EFAULT;

	switch (optname) {
	case NETLINK_PKTINFO:
		if (val)
			nlk->flags |= NETLINK_RECV_PKTINFO;
		else
			nlk->flags &= ~NETLINK_RECV_PKTINFO;
		err = 0;
		break;
	case NETLINK_ADD_MEMBERSHIP:
	case NETLINK_DROP_MEMBERSHIP: {
		unsigned int subscriptions;
		int old, new = optname == NETLINK_ADD_MEMBERSHIP ? 1 : 0;

		if (!netlink_capable(sock, NL_NONROOT_RECV))
			return -EPERM;
		if (nlk->groups == NULL) {
			err = netlink_alloc_groups(sk);
			if (err)
				return err;
		}
		if (!val || val - 1 >= nlk->ngroups)
			return -EINVAL;
		netlink_table_grab();
		old = test_bit(val - 1, nlk->groups);
		subscriptions = nlk->subscriptions - old + new;
		if (new)
			__set_bit(val - 1, nlk->groups);
		else
			__clear_bit(val - 1, nlk->groups);
		netlink_update_subscriptions(sk, subscriptions);
		netlink_table_ungrab();
		err = 0;
		break;
	}
	default:
		err = -ENOPROTOOPT;
	}
	return err;
}

static int netlink_getsockopt(struct socket *sock, int level, int optname,
                              char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct netlink_sock *nlk = nlk_sk(sk);
	int len, val, err;

	if (level != SOL_NETLINK)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;
	if (len < 0)
		return -EINVAL;

	switch (optname) {
	case NETLINK_PKTINFO:
		if (len < sizeof(int))
			return -EINVAL;
		len = sizeof(int);
		val = nlk->flags & NETLINK_RECV_PKTINFO ? 1 : 0;
		put_user(len, optlen);
		put_user(val, optval);
		err = 0;
		break;
	default:
		err = -ENOPROTOOPT;
	}
	return err;
}

static void netlink_cmsg_recv_pktinfo(struct msghdr *msg, struct sk_buff *skb)
{
	struct nl_pktinfo info;

	info.group = NETLINK_CB(skb).dst_group;
	put_cmsg(msg, SOL_NETLINK, NETLINK_PKTINFO, sizeof(info), &info);
}

static inline void netlink_rcv_wake(struct sock *sk)
{
	struct netlink_sock *nlk = nlk_sk(sk);

	if (skb_queue_empty(&sk->sk_receive_queue))
		clear_bit(0, &nlk->state);
	if (!test_bit(0, &nlk->state))
		wake_up_interruptible(&nlk->wait);
}

static int netlink_sendmsg(struct kiocb *kiocb, struct socket *sock,
			   struct msghdr *msg, size_t len)
{
	struct sock_iocb *siocb = kiocb_to_siocb(kiocb);
	struct sock *sk = sock->sk;
	struct netlink_sock *nlk = nlk_sk(sk);
	struct sockaddr_nl *addr=msg->msg_name;
	u32 dst_pid;
	u32 dst_group;
	struct sk_buff *skb;
	int err;
	struct scm_cookie scm;

	if (msg->msg_flags&MSG_OOB)
		return -EOPNOTSUPP;

	if (NULL == siocb->scm)
		siocb->scm = &scm;
	err = scm_send(sock, msg, siocb->scm);
	if (err < 0)
		return err;

	if (msg->msg_namelen) {
		if (addr->nl_family != AF_NETLINK)
			return -EINVAL;
		dst_pid = addr->nl_pid;
		dst_group = ffs(addr->nl_groups);
		if (dst_group && !netlink_capable(sock, NL_NONROOT_SEND))
			return -EPERM;
	} else {
		dst_pid = nlk->dst_pid;
		dst_group = nlk->dst_group;
	}

	if (!nlk->pid) {
		err = netlink_autobind(sock);
		if (err)
			goto out;
	}

	err = -EMSGSIZE;
	if (len > sk->sk_sndbuf - 32)
		goto out;
	err = -ENOBUFS;
	skb = alloc_skb(len, GFP_KERNEL);
	if (skb==NULL)
		goto out;

	NETLINK_CB(skb).pid	= nlk->pid;
	NETLINK_CB(skb).dst_pid = dst_pid;
	NETLINK_CB(skb).dst_group = dst_group;
	NETLINK_CB(skb).loginuid = audit_get_loginuid(current->audit_context);
	memcpy(NETLINK_CREDS(skb), &siocb->scm->creds, sizeof(struct ucred));

	/* What can I do? Netlink is asynchronous, so that
	   we will have to save current capabilities to
	   check them, when this message will be delivered
	   to corresponding kernel module.   --ANK (980802)
	 */

	err = -EFAULT;
	if (memcpy_fromiovec(skb_put(skb,len), msg->msg_iov, len)) {
		kfree_skb(skb);
		goto out;
	}

	err = security_netlink_send(sk, skb);
	if (err) {
		kfree_skb(skb);
		goto out;
	}

	if (dst_group) {
		atomic_inc(&skb->users);
		netlink_broadcast(sk, skb, dst_pid, dst_group, GFP_KERNEL);
	}
	err = netlink_unicast(sk, skb, dst_pid, msg->msg_flags&MSG_DONTWAIT);

out:
	return err;
}

static int netlink_recvmsg(struct kiocb *kiocb, struct socket *sock,
			   struct msghdr *msg, size_t len,
			   int flags)
{
	struct sock_iocb *siocb = kiocb_to_siocb(kiocb);
	struct scm_cookie scm;
	struct sock *sk = sock->sk;
	struct netlink_sock *nlk = nlk_sk(sk);
	int noblock = flags&MSG_DONTWAIT;
	size_t copied;
	struct sk_buff *skb;
	int err;

	if (flags&MSG_OOB)
		return -EOPNOTSUPP;

	copied = 0;

	skb = skb_recv_datagram(sk,flags,noblock,&err);
	if (skb==NULL)
		goto out;

	msg->msg_namelen = 0;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	skb->h.raw = skb->data;
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	if (msg->msg_name) {
		struct sockaddr_nl *addr = (struct sockaddr_nl*)msg->msg_name;
		addr->nl_family = AF_NETLINK;
		addr->nl_pad    = 0;
		addr->nl_pid	= NETLINK_CB(skb).pid;
		addr->nl_groups	= netlink_group_mask(NETLINK_CB(skb).dst_group);
		msg->msg_namelen = sizeof(*addr);
	}

	if (NULL == siocb->scm) {
		memset(&scm, 0, sizeof(scm));
		siocb->scm = &scm;
	}
	siocb->scm->creds = *NETLINK_CREDS(skb);
	skb_free_datagram(sk, skb);

	if (nlk->cb && atomic_read(&sk->sk_rmem_alloc) <= sk->sk_rcvbuf / 2)
		netlink_dump(sk);

	scm_recv(sock, msg, siocb->scm, flags);
	if (nlk->flags & NETLINK_RECV_PKTINFO)
		netlink_cmsg_recv_pktinfo(msg, skb);

out:
	netlink_rcv_wake(sk);
	return err ? : copied;
}

static void netlink_data_ready(struct sock *sk, int len)
{
	struct netlink_sock *nlk = nlk_sk(sk);

	if (nlk->data_ready)
		nlk->data_ready(sk, len);
	netlink_rcv_wake(sk);
}

/*
 *	We export these functions to other modules. They provide a 
 *	complete set of kernel non-blocking support for message
 *	queueing.
 */

struct sock *
netlink_kernel_create(int unit, unsigned int groups,
                      void (*input)(struct sock *sk, int len),
                      struct module *module)
{
	struct socket *sock;
	struct sock *sk;
	struct netlink_sock *nlk;

	if (!nl_table)
		return NULL;

	if (unit<0 || unit>=MAX_LINKS)
		return NULL;

	if (sock_create_lite(PF_NETLINK, SOCK_DGRAM, unit, &sock))
		return NULL;

	if (__netlink_create(sock, unit) < 0)
		goto out_sock_release;

	sk = sock->sk;
	sk->sk_data_ready = netlink_data_ready;
	if (input)
		nlk_sk(sk)->data_ready = input;

	if (netlink_insert(sk, 0))
		goto out_sock_release;

	nlk = nlk_sk(sk);
	nlk->flags |= NETLINK_KERNEL_SOCKET;

	netlink_table_grab();
	nl_table[unit].groups = groups < 32 ? 32 : groups;
	nl_table[unit].module = module;
	nl_table[unit].registered = 1;
	netlink_table_ungrab();

	return sk;

out_sock_release:
	sock_release(sock);
	return NULL;
}

void netlink_set_nonroot(int protocol, unsigned int flags)
{ 
	if ((unsigned int)protocol < MAX_LINKS) 
		nl_table[protocol].nl_nonroot = flags;
} 

static void netlink_destroy_callback(struct netlink_callback *cb)
{
	if (cb->skb)
		kfree_skb(cb->skb);
	kfree(cb);
}

/*
 * It looks a bit ugly.
 * It would be better to create kernel thread.
 */

static int netlink_dump(struct sock *sk)
{
	struct netlink_sock *nlk = nlk_sk(sk);
	struct netlink_callback *cb;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int len;
	
	skb = sock_rmalloc(sk, NLMSG_GOODSIZE, 0, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	spin_lock(&nlk->cb_lock);

	cb = nlk->cb;
	if (cb == NULL) {
		spin_unlock(&nlk->cb_lock);
		kfree_skb(skb);
		return -EINVAL;
	}

	len = cb->dump(skb, cb);

	if (len > 0) {
		spin_unlock(&nlk->cb_lock);
		skb_queue_tail(&sk->sk_receive_queue, skb);
		sk->sk_data_ready(sk, len);
		return 0;
	}

	nlh = NLMSG_NEW_ANSWER(skb, cb, NLMSG_DONE, sizeof(len), NLM_F_MULTI);
	memcpy(NLMSG_DATA(nlh), &len, sizeof(len));
	skb_queue_tail(&sk->sk_receive_queue, skb);
	sk->sk_data_ready(sk, skb->len);

	if (cb->done)
		cb->done(cb);
	nlk->cb = NULL;
	spin_unlock(&nlk->cb_lock);

	netlink_destroy_callback(cb);
	return 0;

nlmsg_failure:
	return -ENOBUFS;
}

int netlink_dump_start(struct sock *ssk, struct sk_buff *skb,
		       struct nlmsghdr *nlh,
		       int (*dump)(struct sk_buff *skb, struct netlink_callback*),
		       int (*done)(struct netlink_callback*))
{
	struct netlink_callback *cb;
	struct sock *sk;
	struct netlink_sock *nlk;

	cb = kmalloc(sizeof(*cb), GFP_KERNEL);
	if (cb == NULL)
		return -ENOBUFS;

	memset(cb, 0, sizeof(*cb));
	cb->dump = dump;
	cb->done = done;
	cb->nlh = nlh;
	atomic_inc(&skb->users);
	cb->skb = skb;

	sk = netlink_lookup(ssk->sk_protocol, NETLINK_CB(skb).pid);
	if (sk == NULL) {
		netlink_destroy_callback(cb);
		return -ECONNREFUSED;
	}
	nlk = nlk_sk(sk);
	/* A dump is in progress... */
	spin_lock(&nlk->cb_lock);
	if (nlk->cb) {
		spin_unlock(&nlk->cb_lock);
		netlink_destroy_callback(cb);
		sock_put(sk);
		return -EBUSY;
	}
	nlk->cb = cb;
	spin_unlock(&nlk->cb_lock);

	netlink_dump(sk);
	sock_put(sk);
	return 0;
}

void netlink_ack(struct sk_buff *in_skb, struct nlmsghdr *nlh, int err)
{
	struct sk_buff *skb;
	struct nlmsghdr *rep;
	struct nlmsgerr *errmsg;
	int size;

	if (err == 0)
		size = NLMSG_SPACE(sizeof(struct nlmsgerr));
	else
		size = NLMSG_SPACE(4 + NLMSG_ALIGN(nlh->nlmsg_len));

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb) {
		struct sock *sk;

		sk = netlink_lookup(in_skb->sk->sk_protocol,
				    NETLINK_CB(in_skb).pid);
		if (sk) {
			sk->sk_err = ENOBUFS;
			sk->sk_error_report(sk);
			sock_put(sk);
		}
		return;
	}

	rep = __nlmsg_put(skb, NETLINK_CB(in_skb).pid, nlh->nlmsg_seq,
			  NLMSG_ERROR, sizeof(struct nlmsgerr), 0);
	errmsg = NLMSG_DATA(rep);
	errmsg->error = err;
	memcpy(&errmsg->msg, nlh, err ? nlh->nlmsg_len : sizeof(struct nlmsghdr));
	netlink_unicast(in_skb->sk, skb, NETLINK_CB(in_skb).pid, MSG_DONTWAIT);
}

static int netlink_rcv_skb(struct sk_buff *skb, int (*cb)(struct sk_buff *,
						     struct nlmsghdr *, int *))
{
	unsigned int total_len;
	struct nlmsghdr *nlh;
	int err;

	while (skb->len >= nlmsg_total_size(0)) {
		nlh = (struct nlmsghdr *) skb->data;

		if (skb->len < nlh->nlmsg_len)
			return 0;

		total_len = min(NLMSG_ALIGN(nlh->nlmsg_len), skb->len);

		if (cb(skb, nlh, &err) < 0) {
			/* Not an error, but we have to interrupt processing
			 * here. Note: that in this case we do not pull
			 * message from skb, it will be processed later.
			 */
			if (err == 0)
				return -1;
			netlink_ack(skb, nlh, err);
		} else if (nlh->nlmsg_flags & NLM_F_ACK)
			netlink_ack(skb, nlh, 0);

		skb_pull(skb, total_len);
	}

	return 0;
}

/**
 * nelink_run_queue - Process netlink receive queue.
 * @sk: Netlink socket containing the queue
 * @qlen: Place to store queue length upon entry
 * @cb: Callback function invoked for each netlink message found
 *
 * Processes as much as there was in the queue upon entry and invokes
 * a callback function for each netlink message found. The callback
 * function may refuse a message by returning a negative error code
 * but setting the error pointer to 0 in which case this function
 * returns with a qlen != 0.
 *
 * qlen must be initialized to 0 before the initial entry, afterwards
 * the function may be called repeatedly until qlen reaches 0.
 */
void netlink_run_queue(struct sock *sk, unsigned int *qlen,
		       int (*cb)(struct sk_buff *, struct nlmsghdr *, int *))
{
	struct sk_buff *skb;

	if (!*qlen || *qlen > skb_queue_len(&sk->sk_receive_queue))
		*qlen = skb_queue_len(&sk->sk_receive_queue);

	for (; *qlen; (*qlen)--) {
		skb = skb_dequeue(&sk->sk_receive_queue);
		if (netlink_rcv_skb(skb, cb)) {
			if (skb->len)
				skb_queue_head(&sk->sk_receive_queue, skb);
			else {
				kfree_skb(skb);
				(*qlen)--;
			}
			break;
		}

		kfree_skb(skb);
	}
}

/**
 * netlink_queue_skip - Skip netlink message while processing queue.
 * @nlh: Netlink message to be skipped
 * @skb: Socket buffer containing the netlink messages.
 *
 * Pulls the given netlink message off the socket buffer so the next
 * call to netlink_queue_run() will not reconsider the message.
 */
void netlink_queue_skip(struct nlmsghdr *nlh, struct sk_buff *skb)
{
	int msglen = NLMSG_ALIGN(nlh->nlmsg_len);

	if (msglen > skb->len)
		msglen = skb->len;

	skb_pull(skb, msglen);
}

#ifdef CONFIG_PROC_FS
struct nl_seq_iter {
	int link;
	int hash_idx;
};

static struct sock *netlink_seq_socket_idx(struct seq_file *seq, loff_t pos)
{
	struct nl_seq_iter *iter = seq->private;
	int i, j;
	struct sock *s;
	struct hlist_node *node;
	loff_t off = 0;

	for (i=0; i<MAX_LINKS; i++) {
		struct nl_pid_hash *hash = &nl_table[i].hash;

		for (j = 0; j <= hash->mask; j++) {
			sk_for_each(s, node, &hash->table[j]) {
				if (off == pos) {
					iter->link = i;
					iter->hash_idx = j;
					return s;
				}
				++off;
			}
		}
	}
	return NULL;
}

static void *netlink_seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock(&nl_table_lock);
	return *pos ? netlink_seq_socket_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *netlink_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock *s;
	struct nl_seq_iter *iter;
	int i, j;

	++*pos;

	if (v == SEQ_START_TOKEN)
		return netlink_seq_socket_idx(seq, 0);
		
	s = sk_next(v);
	if (s)
		return s;

	iter = seq->private;
	i = iter->link;
	j = iter->hash_idx + 1;

	do {
		struct nl_pid_hash *hash = &nl_table[i].hash;

		for (; j <= hash->mask; j++) {
			s = sk_head(&hash->table[j]);
			if (s) {
				iter->link = i;
				iter->hash_idx = j;
				return s;
			}
		}

		j = 0;
	} while (++i < MAX_LINKS);

	return NULL;
}

static void netlink_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&nl_table_lock);
}


static int netlink_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq,
			 "sk       Eth Pid    Groups   "
			 "Rmem     Wmem     Dump     Locks\n");
	else {
		struct sock *s = v;
		struct netlink_sock *nlk = nlk_sk(s);

		seq_printf(seq, "%p %-3d %-6d %08x %-8d %-8d %p %d\n",
			   s,
			   s->sk_protocol,
			   nlk->pid,
			   nlk->groups ? (u32)nlk->groups[0] : 0,
			   atomic_read(&s->sk_rmem_alloc),
			   atomic_read(&s->sk_wmem_alloc),
			   nlk->cb,
			   atomic_read(&s->sk_refcnt)
			);

	}
	return 0;
}

static struct seq_operations netlink_seq_ops = {
	.start  = netlink_seq_start,
	.next   = netlink_seq_next,
	.stop   = netlink_seq_stop,
	.show   = netlink_seq_show,
};


static int netlink_seq_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	struct nl_seq_iter *iter;
	int err;

	iter = kmalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	err = seq_open(file, &netlink_seq_ops);
	if (err) {
		kfree(iter);
		return err;
	}

	memset(iter, 0, sizeof(*iter));
	seq = file->private_data;
	seq->private = iter;
	return 0;
}

static struct file_operations netlink_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= netlink_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

#endif

int netlink_register_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&netlink_chain, nb);
}

int netlink_unregister_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&netlink_chain, nb);
}
                
static struct proto_ops netlink_ops = {
	.family =	PF_NETLINK,
	.owner =	THIS_MODULE,
	.release =	netlink_release,
	.bind =		netlink_bind,
	.connect =	netlink_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	netlink_getname,
	.poll =		datagram_poll,
	.ioctl =	sock_no_ioctl,
	.listen =	sock_no_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	netlink_setsockopt,
	.getsockopt =	netlink_getsockopt,
	.sendmsg =	netlink_sendmsg,
	.recvmsg =	netlink_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};

static struct net_proto_family netlink_family_ops = {
	.family = PF_NETLINK,
	.create = netlink_create,
	.owner	= THIS_MODULE,	/* for consistency 8) */
};

extern void netlink_skb_parms_too_large(void);

static int __init netlink_proto_init(void)
{
	struct sk_buff *dummy_skb;
	int i;
	unsigned long max;
	unsigned int order;
	int err = proto_register(&netlink_proto, 0);

	if (err != 0)
		goto out;

	if (sizeof(struct netlink_skb_parms) > sizeof(dummy_skb->cb))
		netlink_skb_parms_too_large();

	nl_table = kmalloc(sizeof(*nl_table) * MAX_LINKS, GFP_KERNEL);
	if (!nl_table) {
enomem:
		printk(KERN_CRIT "netlink_init: Cannot allocate nl_table\n");
		return -ENOMEM;
	}

	memset(nl_table, 0, sizeof(*nl_table) * MAX_LINKS);

	if (num_physpages >= (128 * 1024))
		max = num_physpages >> (21 - PAGE_SHIFT);
	else
		max = num_physpages >> (23 - PAGE_SHIFT);

	order = get_bitmask_order(max) - 1 + PAGE_SHIFT;
	max = (1UL << order) / sizeof(struct hlist_head);
	order = get_bitmask_order(max > UINT_MAX ? UINT_MAX : max) - 1;

	for (i = 0; i < MAX_LINKS; i++) {
		struct nl_pid_hash *hash = &nl_table[i].hash;

		hash->table = nl_pid_hash_alloc(1 * sizeof(*hash->table));
		if (!hash->table) {
			while (i-- > 0)
				nl_pid_hash_free(nl_table[i].hash.table,
						 1 * sizeof(*hash->table));
			kfree(nl_table);
			goto enomem;
		}
		memset(hash->table, 0, 1 * sizeof(*hash->table));
		hash->max_shift = order;
		hash->shift = 0;
		hash->mask = 0;
		hash->rehash_time = jiffies;
	}

	sock_register(&netlink_family_ops);
#ifdef CONFIG_PROC_FS
	proc_net_fops_create("netlink", 0, &netlink_seq_fops);
#endif
	/* The netlink device handler may be needed early. */ 
	rtnetlink_init();
out:
	return err;
}

core_initcall(netlink_proto_init);

EXPORT_SYMBOL(netlink_ack);
EXPORT_SYMBOL(netlink_run_queue);
EXPORT_SYMBOL(netlink_queue_skip);
EXPORT_SYMBOL(netlink_broadcast);
EXPORT_SYMBOL(netlink_dump_start);
EXPORT_SYMBOL(netlink_kernel_create);
EXPORT_SYMBOL(netlink_register_notifier);
EXPORT_SYMBOL(netlink_set_err);
EXPORT_SYMBOL(netlink_set_nonroot);
EXPORT_SYMBOL(netlink_unicast);
EXPORT_SYMBOL(netlink_unregister_notifier);

