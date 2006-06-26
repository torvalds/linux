/*
 *	Implements the IPX routing routines.
 *	Code moved from af_ipx.c.
 *
 *	Arnaldo Carvalho de Melo <acme@conectiva.com.br>, 2003
 *
 *	See net/ipx/ChangeLog.
 */

#include <linux/config.h>
#include <linux/list.h>
#include <linux/route.h>
#include <linux/spinlock.h>

#include <net/ipx.h>
#include <net/sock.h>

LIST_HEAD(ipx_routes);
DEFINE_RWLOCK(ipx_routes_lock);

extern struct ipx_interface *ipx_internal_net;

extern __u16 ipx_cksum(struct ipxhdr *packet, int length);
extern struct ipx_interface *ipxitf_find_using_net(__u32 net);
extern int ipxitf_demux_socket(struct ipx_interface *intrfc,
			       struct sk_buff *skb, int copy);
extern int ipxitf_demux_socket(struct ipx_interface *intrfc,
			       struct sk_buff *skb, int copy);
extern int ipxitf_send(struct ipx_interface *intrfc, struct sk_buff *skb,
		       char *node);
extern struct ipx_interface *ipxitf_find_using_net(__u32 net);

struct ipx_route *ipxrtr_lookup(__u32 net)
{
	struct ipx_route *r;

	read_lock_bh(&ipx_routes_lock);
	list_for_each_entry(r, &ipx_routes, node)
		if (r->ir_net == net) {
			ipxrtr_hold(r);
			goto unlock;
		}
	r = NULL;
unlock:
	read_unlock_bh(&ipx_routes_lock);
	return r;
}

/*
 * Caller must hold a reference to intrfc
 */
int ipxrtr_add_route(__u32 network, struct ipx_interface *intrfc,
		     unsigned char *node)
{
	struct ipx_route *rt;
	int rc;

	/* Get a route structure; either existing or create */
	rt = ipxrtr_lookup(network);
	if (!rt) {
		rt = kmalloc(sizeof(*rt), GFP_ATOMIC);
		rc = -EAGAIN;
		if (!rt)
			goto out;

		atomic_set(&rt->refcnt, 1);
		ipxrtr_hold(rt);
		write_lock_bh(&ipx_routes_lock);
		list_add(&rt->node, &ipx_routes);
		write_unlock_bh(&ipx_routes_lock);
	} else {
		rc = -EEXIST;
		if (intrfc == ipx_internal_net)
			goto out_put;
	}

	rt->ir_net 	= network;
	rt->ir_intrfc 	= intrfc;
	if (!node) {
		memset(rt->ir_router_node, '\0', IPX_NODE_LEN);
		rt->ir_routed = 0;
	} else {
		memcpy(rt->ir_router_node, node, IPX_NODE_LEN);
		rt->ir_routed = 1;
	}

	rc = 0;
out_put:
	ipxrtr_put(rt);
out:
	return rc;
}

void ipxrtr_del_routes(struct ipx_interface *intrfc)
{
	struct ipx_route *r, *tmp;

	write_lock_bh(&ipx_routes_lock);
	list_for_each_entry_safe(r, tmp, &ipx_routes, node)
		if (r->ir_intrfc == intrfc) {
			list_del(&r->node);
			ipxrtr_put(r);
		}
	write_unlock_bh(&ipx_routes_lock);
}

static int ipxrtr_create(struct ipx_route_definition *rd)
{
	struct ipx_interface *intrfc;
	int rc = -ENETUNREACH;

	/* Find the appropriate interface */
	intrfc = ipxitf_find_using_net(rd->ipx_router_network);
	if (!intrfc)
		goto out;
	rc = ipxrtr_add_route(rd->ipx_network, intrfc, rd->ipx_router_node);
	ipxitf_put(intrfc);
out:
	return rc;
}

static int ipxrtr_delete(__u32 net)
{
	struct ipx_route *r, *tmp;
	int rc;

	write_lock_bh(&ipx_routes_lock);
	list_for_each_entry_safe(r, tmp, &ipx_routes, node)
		if (r->ir_net == net) {
			/* Directly connected; can't lose route */
			rc = -EPERM;
			if (!r->ir_routed)
				goto out;
			list_del(&r->node);
			ipxrtr_put(r);
			rc = 0;
			goto out;
		}
	rc = -ENOENT;
out:
	write_unlock_bh(&ipx_routes_lock);
	return rc;
}

/*
 * The skb has to be unshared, we'll end up calling ipxitf_send, that'll
 * modify the packet
 */
int ipxrtr_route_skb(struct sk_buff *skb)
{
	struct ipxhdr *ipx = ipx_hdr(skb);
	struct ipx_route *r = ipxrtr_lookup(IPX_SKB_CB(skb)->ipx_dest_net);

	if (!r) {	/* no known route */
		kfree_skb(skb);
		return 0;
	}

	ipxitf_hold(r->ir_intrfc);
	ipxitf_send(r->ir_intrfc, skb, r->ir_routed ?
			r->ir_router_node : ipx->ipx_dest.node);
	ipxitf_put(r->ir_intrfc);
	ipxrtr_put(r);

	return 0;
}

/*
 * Route an outgoing frame from a socket.
 */
int ipxrtr_route_packet(struct sock *sk, struct sockaddr_ipx *usipx,
			struct iovec *iov, size_t len, int noblock)
{
	struct sk_buff *skb;
	struct ipx_sock *ipxs = ipx_sk(sk);
	struct ipx_interface *intrfc;
	struct ipxhdr *ipx;
	size_t size;
	int ipx_offset;
	struct ipx_route *rt = NULL;
	int rc;

	/* Find the appropriate interface on which to send packet */
	if (!usipx->sipx_network && ipx_primary_net) {
		usipx->sipx_network = ipx_primary_net->if_netnum;
		intrfc = ipx_primary_net;
	} else {
		rt = ipxrtr_lookup(usipx->sipx_network);
		rc = -ENETUNREACH;
		if (!rt)
			goto out;
		intrfc = rt->ir_intrfc;
	}

	ipxitf_hold(intrfc);
	ipx_offset = intrfc->if_ipx_offset;
	size = sizeof(struct ipxhdr) + len + ipx_offset;

	skb = sock_alloc_send_skb(sk, size, noblock, &rc);
	if (!skb)
		goto out_put;

	skb_reserve(skb, ipx_offset);
	skb->sk = sk;

	/* Fill in IPX header */
	skb->h.raw = skb->nh.raw = skb_put(skb, sizeof(struct ipxhdr));
	ipx = ipx_hdr(skb);
	ipx->ipx_pktsize = htons(len + sizeof(struct ipxhdr));
	IPX_SKB_CB(skb)->ipx_tctrl = 0;
	ipx->ipx_type 	 = usipx->sipx_type;

	IPX_SKB_CB(skb)->last_hop.index = -1;
#ifdef CONFIG_IPX_INTERN
	IPX_SKB_CB(skb)->ipx_source_net = ipxs->intrfc->if_netnum;
	memcpy(ipx->ipx_source.node, ipxs->node, IPX_NODE_LEN);
#else
	rc = ntohs(ipxs->port);
	if (rc == 0x453 || rc == 0x452) {
		/* RIP/SAP special handling for mars_nwe */
		IPX_SKB_CB(skb)->ipx_source_net = intrfc->if_netnum;
		memcpy(ipx->ipx_source.node, intrfc->if_node, IPX_NODE_LEN);
	} else {
		IPX_SKB_CB(skb)->ipx_source_net = ipxs->intrfc->if_netnum;
		memcpy(ipx->ipx_source.node, ipxs->intrfc->if_node,
			IPX_NODE_LEN);
	}
#endif	/* CONFIG_IPX_INTERN */
	ipx->ipx_source.sock		= ipxs->port;
	IPX_SKB_CB(skb)->ipx_dest_net	= usipx->sipx_network;
	memcpy(ipx->ipx_dest.node, usipx->sipx_node, IPX_NODE_LEN);
	ipx->ipx_dest.sock		= usipx->sipx_port;

	rc = memcpy_fromiovec(skb_put(skb, len), iov, len);
	if (rc) {
		kfree_skb(skb);
		goto out_put;
	}	

	/* Apply checksum. Not allowed on 802.3 links. */
	if (sk->sk_no_check || intrfc->if_dlink_type == htons(IPX_FRAME_8023))
		ipx->ipx_checksum = 0xFFFF;
	else
		ipx->ipx_checksum = ipx_cksum(ipx, len + sizeof(struct ipxhdr));

	rc = ipxitf_send(intrfc, skb, (rt && rt->ir_routed) ? 
			 rt->ir_router_node : ipx->ipx_dest.node);
out_put:
	ipxitf_put(intrfc);
	if (rt)
		ipxrtr_put(rt);
out:
	return rc;
}

/*
 * We use a normal struct rtentry for route handling
 */
int ipxrtr_ioctl(unsigned int cmd, void __user *arg)
{
	struct rtentry rt;	/* Use these to behave like 'other' stacks */
	struct sockaddr_ipx *sg, *st;
	int rc = -EFAULT;

	if (copy_from_user(&rt, arg, sizeof(rt)))
		goto out;

	sg = (struct sockaddr_ipx *)&rt.rt_gateway;
	st = (struct sockaddr_ipx *)&rt.rt_dst;

	rc = -EINVAL;
	if (!(rt.rt_flags & RTF_GATEWAY) || /* Direct routes are fixed */
	    sg->sipx_family != AF_IPX ||
	    st->sipx_family != AF_IPX)
		goto out;

	switch (cmd) {
	case SIOCDELRT:
		rc = ipxrtr_delete(st->sipx_network);
		break;
	case SIOCADDRT: {
		struct ipx_route_definition f;
		f.ipx_network		= st->sipx_network;
		f.ipx_router_network	= sg->sipx_network;
		memcpy(f.ipx_router_node, sg->sipx_node, IPX_NODE_LEN);
		rc = ipxrtr_create(&f);
		break;
	}
	}

out:
	return rc;
}
