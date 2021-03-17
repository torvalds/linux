/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ATALK_H__
#define __LINUX_ATALK_H__


#include <net/sock.h>
#include <uapi/linux/atalk.h>

struct atalk_route {
	struct net_device  *dev;
	struct atalk_addr  target;
	struct atalk_addr  gateway;
	int		   flags;
	struct atalk_route *next;
};

/**
 *	struct atalk_iface - AppleTalk Interface
 *	@dev - Network device associated with this interface
 *	@address - Our address
 *	@status - What are we doing?
 *	@nets - Associated direct netrange
 *	@next - next element in the list of interfaces
 */
struct atalk_iface {
	struct net_device	*dev;
	struct atalk_addr	address;
	int			status;
#define ATIF_PROBE	1		/* Probing for an address */
#define ATIF_PROBE_FAIL	2		/* Probe collided */
	struct atalk_netrange	nets;
	struct atalk_iface	*next;
};
	
struct atalk_sock {
	/* struct sock has to be the first member of atalk_sock */
	struct sock	sk;
	__be16		dest_net;
	__be16		src_net;
	unsigned char	dest_node;
	unsigned char	src_node;
	unsigned char	dest_port;
	unsigned char	src_port;
};

static inline struct atalk_sock *at_sk(struct sock *sk)
{
	return (struct atalk_sock *)sk;
}

struct ddpehdr {
	__be16	deh_len_hops;	/* lower 10 bits are length, next 4 - hops */
	__be16	deh_sum;
	__be16	deh_dnet;
	__be16	deh_snet;
	__u8	deh_dnode;
	__u8	deh_snode;
	__u8	deh_dport;
	__u8	deh_sport;
	/* And netatalk apps expect to stick the type in themselves */
};

static __inline__ struct ddpehdr *ddp_hdr(struct sk_buff *skb)
{
	return (struct ddpehdr *)skb_transport_header(skb);
}

/* AppleTalk AARP headers */
struct elapaarp {
	__be16	hw_type;
#define AARP_HW_TYPE_ETHERNET		1
#define AARP_HW_TYPE_TOKENRING		2
	__be16	pa_type;
	__u8	hw_len;
	__u8	pa_len;
#define AARP_PA_ALEN			4
	__be16	function;
#define AARP_REQUEST			1
#define AARP_REPLY			2
#define AARP_PROBE			3
	__u8	hw_src[ETH_ALEN];
	__u8	pa_src_zero;
	__be16	pa_src_net;
	__u8	pa_src_node;
	__u8	hw_dst[ETH_ALEN];
	__u8	pa_dst_zero;
	__be16	pa_dst_net;
	__u8	pa_dst_node;
} __attribute__ ((packed));

static __inline__ struct elapaarp *aarp_hdr(struct sk_buff *skb)
{
	return (struct elapaarp *)skb_transport_header(skb);
}

/* Not specified - how long till we drop a resolved entry */
#define AARP_EXPIRY_TIME	(5 * 60 * HZ)
/* Size of hash table */
#define AARP_HASH_SIZE		16
/* Fast retransmission timer when resolving */
#define AARP_TICK_TIME		(HZ / 5)
/* Send 10 requests then give up (2 seconds) */
#define AARP_RETRANSMIT_LIMIT	10
/*
 * Some value bigger than total retransmit time + a bit for last reply to
 * appear and to stop continual requests
 */
#define AARP_RESOLVE_TIME	(10 * HZ)

extern struct datalink_proto *ddp_dl, *aarp_dl;
extern void aarp_proto_init(void);

/* Inter module exports */

/* Give a device find its atif control structure */
#if IS_ENABLED(CONFIG_IRDA) || IS_ENABLED(CONFIG_ATALK)
static inline struct atalk_iface *atalk_find_dev(struct net_device *dev)
{
	return dev->atalk_ptr;
}
#endif

extern struct atalk_addr *atalk_find_dev_addr(struct net_device *dev);
extern struct net_device *atrtr_get_dev(struct atalk_addr *sa);
extern int		 aarp_send_ddp(struct net_device *dev,
				       struct sk_buff *skb,
				       struct atalk_addr *sa, void *hwaddr);
extern void		 aarp_device_down(struct net_device *dev);
extern void		 aarp_probe_network(struct atalk_iface *atif);
extern int 		 aarp_proxy_probe_network(struct atalk_iface *atif,
				     struct atalk_addr *sa);
extern void		 aarp_proxy_remove(struct net_device *dev,
					   struct atalk_addr *sa);

extern void		aarp_cleanup_module(void);

extern struct hlist_head atalk_sockets;
extern rwlock_t atalk_sockets_lock;

extern struct atalk_route *atalk_routes;
extern rwlock_t atalk_routes_lock;

extern struct atalk_iface *atalk_interfaces;
extern rwlock_t atalk_interfaces_lock;

extern struct atalk_route atrtr_default;

struct aarp_iter_state {
	int bucket;
	struct aarp_entry **table;
};

extern const struct seq_operations aarp_seq_ops;

extern int sysctl_aarp_expiry_time;
extern int sysctl_aarp_tick_time;
extern int sysctl_aarp_retransmit_limit;
extern int sysctl_aarp_resolve_time;

#ifdef CONFIG_SYSCTL
extern void atalk_register_sysctl(void);
extern void atalk_unregister_sysctl(void);
#else
#define atalk_register_sysctl()		do { } while(0)
#define atalk_unregister_sysctl()	do { } while(0)
#endif

#ifdef CONFIG_PROC_FS
extern int atalk_proc_init(void);
extern void atalk_proc_exit(void);
#else
#define atalk_proc_init()	({ 0; })
#define atalk_proc_exit()	do { } while(0)
#endif /* CONFIG_PROC_FS */

#endif /* __LINUX_ATALK_H__ */
