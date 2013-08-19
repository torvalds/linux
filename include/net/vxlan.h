#ifndef __NET_VXLAN_H
#define __NET_VXLAN_H 1

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/udp.h>

#define VNI_HASH_BITS	10
#define VNI_HASH_SIZE	(1<<VNI_HASH_BITS)

struct vxlan_sock;
typedef void (vxlan_rcv_t)(struct vxlan_sock *vh, struct sk_buff *skb, __be32 key);

/* per UDP socket information */
struct vxlan_sock {
	struct hlist_node hlist;
	vxlan_rcv_t	 *rcv;
	void		 *data;
	struct work_struct del_work;
	struct socket	 *sock;
	struct rcu_head	  rcu;
	struct hlist_head vni_list[VNI_HASH_SIZE];
	atomic_t	  refcnt;
};

struct vxlan_sock *vxlan_sock_add(struct net *net, __be16 port,
				  vxlan_rcv_t *rcv, void *data,
				  bool no_share);

void vxlan_sock_release(struct vxlan_sock *vs);
#endif
