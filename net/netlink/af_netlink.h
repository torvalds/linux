/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AF_NETLINK_H
#define _AF_NETLINK_H

#include <linux/rhashtable.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <net/sock.h>

/* flags */
enum {
	NETLINK_F_KERNEL_SOCKET,
	NETLINK_F_RECV_PKTINFO,
	NETLINK_F_BROADCAST_SEND_ERROR,
	NETLINK_F_RECV_NO_ENOBUFS,
	NETLINK_F_LISTEN_ALL_NSID,
	NETLINK_F_CAP_ACK,
	NETLINK_F_EXT_ACK,
	NETLINK_F_STRICT_CHK,
};

#define NLGRPSZ(x)	(ALIGN(x, sizeof(unsigned long) * 8) / 8)
#define NLGRPLONGS(x)	(NLGRPSZ(x)/sizeof(unsigned long))

struct netlink_sock {
	/* struct sock has to be the first member of netlink_sock */
	struct sock		sk;
	unsigned long		flags;
	u32			portid;
	u32			dst_portid;
	u32			dst_group;
	u32			subscriptions;
	u32			ngroups;
	unsigned long		*groups;
	unsigned long		state;
	size_t			max_recvmsg_len;
	wait_queue_head_t	wait;
	bool			bound;
	bool			cb_running;
	int			dump_done_errno;
	struct netlink_callback	cb;
	struct mutex		*cb_mutex;
	struct mutex		cb_def_mutex;
	void			(*netlink_rcv)(struct sk_buff *skb);
	int			(*netlink_bind)(struct net *net, int group);
	void			(*netlink_unbind)(struct net *net, int group);
	void			(*netlink_release)(struct sock *sk,
						   unsigned long *groups);
	struct module		*module;

	struct rhash_head	node;
	struct rcu_head		rcu;
	struct work_struct	work;
};

static inline struct netlink_sock *nlk_sk(struct sock *sk)
{
	return container_of(sk, struct netlink_sock, sk);
}

#define nlk_test_bit(nr, sk) test_bit(NETLINK_F_##nr, &nlk_sk(sk)->flags)

struct netlink_table {
	struct rhashtable	hash;
	struct hlist_head	mc_list;
	struct listeners __rcu	*listeners;
	unsigned int		flags;
	unsigned int		groups;
	struct mutex		*cb_mutex;
	struct module		*module;
	int			(*bind)(struct net *net, int group);
	void			(*unbind)(struct net *net, int group);
	void                    (*release)(struct sock *sk,
					   unsigned long *groups);
	int			registered;
};

extern struct netlink_table *nl_table;
extern rwlock_t nl_table_lock;

#endif
