#ifndef _NET_INET_IPX_H_
#define _NET_INET_IPX_H_
/*
 *	The following information is in its entirety obtained from:
 *
 *	Novell 'IPX Router Specification' Version 1.10 
 *		Part No. 107-000029-001
 *
 *	Which is available from ftp.novell.com
 */

#include <linux/netdevice.h>
#include <net/datalink.h>
#include <linux/ipx.h>
#include <linux/list.h>

struct ipx_address {
	__u32   net;
	__u8    node[IPX_NODE_LEN]; 
	__u16   sock;
};

#define ipx_broadcast_node	"\377\377\377\377\377\377"
#define ipx_this_node           "\0\0\0\0\0\0"

#define IPX_MAX_PPROP_HOPS 8

struct ipxhdr {
	__u16			ipx_checksum __attribute__ ((packed));
#define IPX_NO_CHECKSUM	0xFFFF
	__u16			ipx_pktsize __attribute__ ((packed));
	__u8			ipx_tctrl;
	__u8			ipx_type;
#define IPX_TYPE_UNKNOWN	0x00
#define IPX_TYPE_RIP		0x01	/* may also be 0 */
#define IPX_TYPE_SAP		0x04	/* may also be 0 */
#define IPX_TYPE_SPX		0x05	/* SPX protocol */
#define IPX_TYPE_NCP		0x11	/* $lots for docs on this (SPIT) */
#define IPX_TYPE_PPROP		0x14	/* complicated flood fill brdcast */
	struct ipx_address	ipx_dest __attribute__ ((packed));
	struct ipx_address	ipx_source __attribute__ ((packed));
};

static __inline__ struct ipxhdr *ipx_hdr(struct sk_buff *skb)
{
	return (struct ipxhdr *)skb->h.raw;
}

struct ipx_interface {
	/* IPX address */
	__u32			if_netnum;
	unsigned char		if_node[IPX_NODE_LEN];
	atomic_t		refcnt;

	/* physical device info */
	struct net_device	*if_dev;
	struct datalink_proto	*if_dlink;
	unsigned short		if_dlink_type;

	/* socket support */
	unsigned short		if_sknum;
	struct hlist_head	if_sklist;
	spinlock_t		if_sklist_lock;

	/* administrative overhead */
	int			if_ipx_offset;
	unsigned char		if_internal;
	unsigned char		if_primary;
	
	struct list_head	node; /* node in ipx_interfaces list */
};

struct ipx_route {
	__u32			ir_net;
	struct ipx_interface	*ir_intrfc;
	unsigned char		ir_routed;
	unsigned char		ir_router_node[IPX_NODE_LEN];
	struct list_head	node; /* node in ipx_routes list */
	atomic_t		refcnt;
};

#ifdef __KERNEL__
struct ipx_cb {
	u8	ipx_tctrl;
	u32	ipx_dest_net;
	u32	ipx_source_net;
	struct {
		u32 netnum;
		int index;
	} last_hop;
};

#include <net/sock.h>

struct ipx_sock {
	/* struct sock has to be the first member of ipx_sock */
	struct sock		sk;
	struct ipx_address	dest_addr;
	struct ipx_interface	*intrfc;
	unsigned short		port;
#ifdef CONFIG_IPX_INTERN
	unsigned char		node[IPX_NODE_LEN];
#endif
	unsigned short		type;
	/*
	 * To handle special ncp connection-handling sockets for mars_nwe,
 	 * the connection number must be stored in the socket.
	 */
	unsigned short		ipx_ncp_conn;
};

static inline struct ipx_sock *ipx_sk(struct sock *sk)
{
	return (struct ipx_sock *)sk;
}

#define IPX_SKB_CB(__skb) ((struct ipx_cb *)&((__skb)->cb[0]))
#endif

#define IPX_MIN_EPHEMERAL_SOCKET	0x4000
#define IPX_MAX_EPHEMERAL_SOCKET	0x7fff

extern struct list_head ipx_routes;
extern rwlock_t ipx_routes_lock;

extern struct list_head ipx_interfaces;
extern struct ipx_interface *ipx_interfaces_head(void);
extern spinlock_t ipx_interfaces_lock;

extern struct ipx_interface *ipx_primary_net;

extern int ipx_proc_init(void);
extern void ipx_proc_exit(void);

extern const char *ipx_frame_name(unsigned short);
extern const char *ipx_device_name(struct ipx_interface *intrfc);

static __inline__ void ipxitf_hold(struct ipx_interface *intrfc)
{
	atomic_inc(&intrfc->refcnt);
}

extern void ipxitf_down(struct ipx_interface *intrfc);

static __inline__ void ipxitf_put(struct ipx_interface *intrfc)
{
	if (atomic_dec_and_test(&intrfc->refcnt))
		ipxitf_down(intrfc);
}

static __inline__ void ipxrtr_hold(struct ipx_route *rt)
{
	        atomic_inc(&rt->refcnt);
}

static __inline__ void ipxrtr_put(struct ipx_route *rt)
{
	        if (atomic_dec_and_test(&rt->refcnt))
			                kfree(rt);
}
#endif /* _NET_INET_IPX_H_ */
