#ifndef __LINUX_MROUTE_H
#define __LINUX_MROUTE_H

#include <linux/in.h>
#include <linux/pim.h>
#include <linux/rhashtable.h>
#include <net/sock.h>
#include <uapi/linux/mroute.h>

#ifdef CONFIG_IP_MROUTE
static inline int ip_mroute_opt(int opt)
{
	return opt >= MRT_BASE && opt <= MRT_MAX;
}

int ip_mroute_setsockopt(struct sock *, int, char __user *, unsigned int);
int ip_mroute_getsockopt(struct sock *, int, char __user *, int __user *);
int ipmr_ioctl(struct sock *sk, int cmd, void __user *arg);
int ipmr_compat_ioctl(struct sock *sk, unsigned int cmd, void __user *arg);
int ip_mr_init(void);
#else
static inline int ip_mroute_setsockopt(struct sock *sock, int optname,
				       char __user *optval, unsigned int optlen)
{
	return -ENOPROTOOPT;
}

static inline int ip_mroute_getsockopt(struct sock *sock, int optname,
				       char __user *optval, int __user *optlen)
{
	return -ENOPROTOOPT;
}

static inline int ipmr_ioctl(struct sock *sk, int cmd, void __user *arg)
{
	return -ENOIOCTLCMD;
}

static inline int ip_mr_init(void)
{
	return 0;
}

static inline int ip_mroute_opt(int opt)
{
	return 0;
}
#endif

struct vif_device {
	struct net_device 	*dev;			/* Device we are using */
	unsigned long	bytes_in,bytes_out;
	unsigned long	pkt_in,pkt_out;		/* Statistics 			*/
	unsigned long	rate_limit;		/* Traffic shaping (NI) 	*/
	unsigned char	threshold;		/* TTL threshold 		*/
	unsigned short	flags;			/* Control flags 		*/
	__be32		local,remote;		/* Addresses(remote for tunnels)*/
	int		link;			/* Physical interface index	*/
};

#define VIFF_STATIC 0x8000

#define VIF_EXISTS(_mrt, _idx) ((_mrt)->vif_table[_idx].dev != NULL)

struct mr_table {
	struct list_head	list;
	possible_net_t		net;
	u32			id;
	struct sock __rcu	*mroute_sk;
	struct timer_list	ipmr_expire_timer;
	struct list_head	mfc_unres_queue;
	struct vif_device	vif_table[MAXVIFS];
	struct rhltable		mfc_hash;
	struct list_head	mfc_cache_list;
	int			maxvif;
	atomic_t		cache_resolve_queue_len;
	bool			mroute_do_assert;
	bool			mroute_do_pim;
	int			mroute_reg_vif_num;
};

/* mfc_flags:
 * MFC_STATIC - the entry was added statically (not by a routing daemon)
 */
enum {
	MFC_STATIC = BIT(0),
};

struct mfc_cache_cmp_arg {
	__be32 mfc_mcastgrp;
	__be32 mfc_origin;
};

/**
 * struct mfc_cache - multicast routing entries
 * @mnode: rhashtable list
 * @mfc_mcastgrp: destination multicast group address
 * @mfc_origin: source address
 * @cmparg: used for rhashtable comparisons
 * @mfc_parent: source interface (iif)
 * @mfc_flags: entry flags
 * @expires: unresolved entry expire time
 * @unresolved: unresolved cached skbs
 * @last_assert: time of last assert
 * @minvif: minimum VIF id
 * @maxvif: maximum VIF id
 * @bytes: bytes that have passed for this entry
 * @pkt: packets that have passed for this entry
 * @wrong_if: number of wrong source interface hits
 * @lastuse: time of last use of the group (traffic or update)
 * @ttls: OIF TTL threshold array
 * @list: global entry list
 * @rcu: used for entry destruction
 */
struct mfc_cache {
	struct rhlist_head mnode;
	union {
		struct {
			__be32 mfc_mcastgrp;
			__be32 mfc_origin;
		};
		struct mfc_cache_cmp_arg cmparg;
	};
	vifi_t mfc_parent;
	int mfc_flags;

	union {
		struct {
			unsigned long expires;
			struct sk_buff_head unresolved;
		} unres;
		struct {
			unsigned long last_assert;
			int minvif;
			int maxvif;
			unsigned long bytes;
			unsigned long pkt;
			unsigned long wrong_if;
			unsigned long lastuse;
			unsigned char ttls[MAXVIFS];
		} res;
	} mfc_un;
	struct list_head list;
	struct rcu_head	rcu;
};

struct rtmsg;
int ipmr_get_route(struct net *net, struct sk_buff *skb,
		   __be32 saddr, __be32 daddr,
		   struct rtmsg *rtm, u32 portid);
#endif
