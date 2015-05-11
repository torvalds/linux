/*
 *	Implements an IPX socket layer.
 *
 *	This code is derived from work by
 *		Ross Biro	: 	Writing the original IP stack
 *		Fred Van Kempen :	Tidying up the TCP/IP
 *
 *	Many thanks go to Keith Baker, Institute For Industrial Information
 *	Technology Ltd, Swansea University for allowing me to work on this
 *	in my own time even though it was in some ways related to commercial
 *	work I am currently employed to do there.
 *
 *	All the material in this file is subject to the Gnu license version 2.
 *	Neither Alan Cox nor the Swansea University Computer Society admit
 *	liability nor provide warranty for any of this software. This material
 *	is provided as is and at no charge.
 *
 *	Portions Copyright (c) 2000-2003 Conectiva, Inc. <acme@conectiva.com.br>
 *	Neither Arnaldo Carvalho de Melo nor Conectiva, Inc. admit liability nor
 *	provide warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 * 	Portions Copyright (c) 1995 Caldera, Inc. <greg@caldera.com>
 *	Neither Greg Page nor Caldera, Inc. admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 *	See net/ipx/ChangeLog.
 */

#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/ipx.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/uio.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/termios.h>

#include <net/ipx.h>
#include <net/p8022.h>
#include <net/psnap.h>
#include <net/sock.h>
#include <net/datalink.h>
#include <net/tcp_states.h>
#include <net/net_namespace.h>

#include <asm/uaccess.h>

/* Configuration Variables */
static unsigned char ipxcfg_max_hops = 16;
static char ipxcfg_auto_select_primary;
static char ipxcfg_auto_create_interfaces;
int sysctl_ipx_pprop_broadcasting = 1;

/* Global Variables */
static struct datalink_proto *p8022_datalink;
static struct datalink_proto *pEII_datalink;
static struct datalink_proto *p8023_datalink;
static struct datalink_proto *pSNAP_datalink;

static const struct proto_ops ipx_dgram_ops;

LIST_HEAD(ipx_interfaces);
DEFINE_SPINLOCK(ipx_interfaces_lock);

struct ipx_interface *ipx_primary_net;
struct ipx_interface *ipx_internal_net;

struct ipx_interface *ipx_interfaces_head(void)
{
	struct ipx_interface *rc = NULL;

	if (!list_empty(&ipx_interfaces))
		rc = list_entry(ipx_interfaces.next,
				struct ipx_interface, node);
	return rc;
}

static void ipxcfg_set_auto_select(char val)
{
	ipxcfg_auto_select_primary = val;
	if (val && !ipx_primary_net)
		ipx_primary_net = ipx_interfaces_head();
}

static int ipxcfg_get_config_data(struct ipx_config_data __user *arg)
{
	struct ipx_config_data vals;

	vals.ipxcfg_auto_create_interfaces = ipxcfg_auto_create_interfaces;
	vals.ipxcfg_auto_select_primary	   = ipxcfg_auto_select_primary;

	return copy_to_user(arg, &vals, sizeof(vals)) ? -EFAULT : 0;
}

/*
 * Note: Sockets may not be removed _during_ an interrupt or inet_bh
 * handler using this technique. They can be added although we do not
 * use this facility.
 */

static void ipx_remove_socket(struct sock *sk)
{
	/* Determine interface with which socket is associated */
	struct ipx_interface *intrfc = ipx_sk(sk)->intrfc;

	if (!intrfc)
		goto out;

	ipxitf_hold(intrfc);
	spin_lock_bh(&intrfc->if_sklist_lock);
	sk_del_node_init(sk);
	spin_unlock_bh(&intrfc->if_sklist_lock);
	ipxitf_put(intrfc);
out:
	return;
}

static void ipx_destroy_socket(struct sock *sk)
{
	ipx_remove_socket(sk);
	skb_queue_purge(&sk->sk_receive_queue);
	sk_refcnt_debug_dec(sk);
}

/*
 * The following code is used to support IPX Interfaces (IPXITF).  An
 * IPX interface is defined by a physical device and a frame type.
 */

/* ipxitf_clear_primary_net has to be called with ipx_interfaces_lock held */

static void ipxitf_clear_primary_net(void)
{
	ipx_primary_net = NULL;
	if (ipxcfg_auto_select_primary)
		ipx_primary_net = ipx_interfaces_head();
}

static struct ipx_interface *__ipxitf_find_using_phys(struct net_device *dev,
						      __be16 datalink)
{
	struct ipx_interface *i;

	list_for_each_entry(i, &ipx_interfaces, node)
		if (i->if_dev == dev && i->if_dlink_type == datalink)
			goto out;
	i = NULL;
out:
	return i;
}

static struct ipx_interface *ipxitf_find_using_phys(struct net_device *dev,
						    __be16 datalink)
{
	struct ipx_interface *i;

	spin_lock_bh(&ipx_interfaces_lock);
	i = __ipxitf_find_using_phys(dev, datalink);
	if (i)
		ipxitf_hold(i);
	spin_unlock_bh(&ipx_interfaces_lock);
	return i;
}

struct ipx_interface *ipxitf_find_using_net(__be32 net)
{
	struct ipx_interface *i;

	spin_lock_bh(&ipx_interfaces_lock);
	if (net) {
		list_for_each_entry(i, &ipx_interfaces, node)
			if (i->if_netnum == net)
				goto hold;
		i = NULL;
		goto unlock;
	}

	i = ipx_primary_net;
	if (i)
hold:
		ipxitf_hold(i);
unlock:
	spin_unlock_bh(&ipx_interfaces_lock);
	return i;
}

/* Sockets are bound to a particular IPX interface. */
static void ipxitf_insert_socket(struct ipx_interface *intrfc, struct sock *sk)
{
	ipxitf_hold(intrfc);
	spin_lock_bh(&intrfc->if_sklist_lock);
	ipx_sk(sk)->intrfc = intrfc;
	sk_add_node(sk, &intrfc->if_sklist);
	spin_unlock_bh(&intrfc->if_sklist_lock);
	ipxitf_put(intrfc);
}

/* caller must hold intrfc->if_sklist_lock */
static struct sock *__ipxitf_find_socket(struct ipx_interface *intrfc,
					 __be16 port)
{
	struct sock *s;

	sk_for_each(s, &intrfc->if_sklist)
		if (ipx_sk(s)->port == port)
			goto found;
	s = NULL;
found:
	return s;
}

/* caller must hold a reference to intrfc */
static struct sock *ipxitf_find_socket(struct ipx_interface *intrfc,
					__be16 port)
{
	struct sock *s;

	spin_lock_bh(&intrfc->if_sklist_lock);
	s = __ipxitf_find_socket(intrfc, port);
	if (s)
		sock_hold(s);
	spin_unlock_bh(&intrfc->if_sklist_lock);

	return s;
}

#ifdef CONFIG_IPX_INTERN
static struct sock *ipxitf_find_internal_socket(struct ipx_interface *intrfc,
						unsigned char *ipx_node,
						__be16 port)
{
	struct sock *s;

	ipxitf_hold(intrfc);
	spin_lock_bh(&intrfc->if_sklist_lock);

	sk_for_each(s, &intrfc->if_sklist) {
		struct ipx_sock *ipxs = ipx_sk(s);

		if (ipxs->port == port &&
		    !memcmp(ipx_node, ipxs->node, IPX_NODE_LEN))
			goto found;
	}
	s = NULL;
found:
	spin_unlock_bh(&intrfc->if_sklist_lock);
	ipxitf_put(intrfc);
	return s;
}
#endif

static void __ipxitf_down(struct ipx_interface *intrfc)
{
	struct sock *s;
	struct hlist_node *t;

	/* Delete all routes associated with this interface */
	ipxrtr_del_routes(intrfc);

	spin_lock_bh(&intrfc->if_sklist_lock);
	/* error sockets */
	sk_for_each_safe(s, t, &intrfc->if_sklist) {
		struct ipx_sock *ipxs = ipx_sk(s);

		s->sk_err = ENOLINK;
		s->sk_error_report(s);
		ipxs->intrfc = NULL;
		ipxs->port   = 0;
		sock_set_flag(s, SOCK_ZAPPED); /* Indicates it is no longer bound */
		sk_del_node_init(s);
	}
	INIT_HLIST_HEAD(&intrfc->if_sklist);
	spin_unlock_bh(&intrfc->if_sklist_lock);

	/* remove this interface from list */
	list_del(&intrfc->node);

	/* remove this interface from *special* networks */
	if (intrfc == ipx_primary_net)
		ipxitf_clear_primary_net();
	if (intrfc == ipx_internal_net)
		ipx_internal_net = NULL;

	if (intrfc->if_dev)
		dev_put(intrfc->if_dev);
	kfree(intrfc);
}

void ipxitf_down(struct ipx_interface *intrfc)
{
	spin_lock_bh(&ipx_interfaces_lock);
	__ipxitf_down(intrfc);
	spin_unlock_bh(&ipx_interfaces_lock);
}

static void __ipxitf_put(struct ipx_interface *intrfc)
{
	if (atomic_dec_and_test(&intrfc->refcnt))
		__ipxitf_down(intrfc);
}

static int ipxitf_device_event(struct notifier_block *notifier,
				unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct ipx_interface *i, *tmp;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	if (event != NETDEV_DOWN && event != NETDEV_UP)
		goto out;

	spin_lock_bh(&ipx_interfaces_lock);
	list_for_each_entry_safe(i, tmp, &ipx_interfaces, node)
		if (i->if_dev == dev) {
			if (event == NETDEV_UP)
				ipxitf_hold(i);
			else
				__ipxitf_put(i);
		}
	spin_unlock_bh(&ipx_interfaces_lock);
out:
	return NOTIFY_DONE;
}


static __exit void ipxitf_cleanup(void)
{
	struct ipx_interface *i, *tmp;

	spin_lock_bh(&ipx_interfaces_lock);
	list_for_each_entry_safe(i, tmp, &ipx_interfaces, node)
		__ipxitf_put(i);
	spin_unlock_bh(&ipx_interfaces_lock);
}

static void ipxitf_def_skb_handler(struct sock *sock, struct sk_buff *skb)
{
	if (sock_queue_rcv_skb(sock, skb) < 0)
		kfree_skb(skb);
}

/*
 * On input skb->sk is NULL. Nobody is charged for the memory.
 */

/* caller must hold a reference to intrfc */

#ifdef CONFIG_IPX_INTERN
static int ipxitf_demux_socket(struct ipx_interface *intrfc,
			       struct sk_buff *skb, int copy)
{
	struct ipxhdr *ipx = ipx_hdr(skb);
	int is_broadcast = !memcmp(ipx->ipx_dest.node, ipx_broadcast_node,
				   IPX_NODE_LEN);
	struct sock *s;
	int rc;

	spin_lock_bh(&intrfc->if_sklist_lock);

	sk_for_each(s, &intrfc->if_sklist) {
		struct ipx_sock *ipxs = ipx_sk(s);

		if (ipxs->port == ipx->ipx_dest.sock &&
		    (is_broadcast || !memcmp(ipx->ipx_dest.node,
					     ipxs->node, IPX_NODE_LEN))) {
			/* We found a socket to which to send */
			struct sk_buff *skb1;

			if (copy) {
				skb1 = skb_clone(skb, GFP_ATOMIC);
				rc = -ENOMEM;
				if (!skb1)
					goto out;
			} else {
				skb1 = skb;
				copy = 1; /* skb may only be used once */
			}
			ipxitf_def_skb_handler(s, skb1);

			/* On an external interface, one socket can listen */
			if (intrfc != ipx_internal_net)
				break;
		}
	}

	/* skb was solely for us, and we did not make a copy, so free it. */
	if (!copy)
		kfree_skb(skb);

	rc = 0;
out:
	spin_unlock_bh(&intrfc->if_sklist_lock);
	return rc;
}
#else
static struct sock *ncp_connection_hack(struct ipx_interface *intrfc,
					struct ipxhdr *ipx)
{
	/* The packet's target is a NCP connection handler. We want to hand it
	 * to the correct socket directly within the kernel, so that the
	 * mars_nwe packet distribution process does not have to do it. Here we
	 * only care about NCP and BURST packets.
	 *
	 * You might call this a hack, but believe me, you do not want a
	 * complete NCP layer in the kernel, and this is VERY fast as well. */
	struct sock *sk = NULL;
	int connection = 0;
	u8 *ncphdr = (u8 *)(ipx + 1);

	if (*ncphdr == 0x22 && *(ncphdr + 1) == 0x22) /* NCP request */
		connection = (((int) *(ncphdr + 5)) << 8) | (int) *(ncphdr + 3);
	else if (*ncphdr == 0x77 && *(ncphdr + 1) == 0x77) /* BURST packet */
		connection = (((int) *(ncphdr + 9)) << 8) | (int) *(ncphdr + 8);

	if (connection) {
		/* Now we have to look for a special NCP connection handling
		 * socket. Only these sockets have ipx_ncp_conn != 0, set by
		 * SIOCIPXNCPCONN. */
		spin_lock_bh(&intrfc->if_sklist_lock);
		sk_for_each(sk, &intrfc->if_sklist)
			if (ipx_sk(sk)->ipx_ncp_conn == connection) {
				sock_hold(sk);
				goto found;
			}
		sk = NULL;
	found:
		spin_unlock_bh(&intrfc->if_sklist_lock);
	}
	return sk;
}

static int ipxitf_demux_socket(struct ipx_interface *intrfc,
			       struct sk_buff *skb, int copy)
{
	struct ipxhdr *ipx = ipx_hdr(skb);
	struct sock *sock1 = NULL, *sock2 = NULL;
	struct sk_buff *skb1 = NULL, *skb2 = NULL;
	int rc;

	if (intrfc == ipx_primary_net && ntohs(ipx->ipx_dest.sock) == 0x451)
		sock1 = ncp_connection_hack(intrfc, ipx);
	if (!sock1)
		/* No special socket found, forward the packet the normal way */
		sock1 = ipxitf_find_socket(intrfc, ipx->ipx_dest.sock);

	/*
	 * We need to check if there is a primary net and if
	 * this is addressed to one of the *SPECIAL* sockets because
	 * these need to be propagated to the primary net.
	 * The *SPECIAL* socket list contains: 0x452(SAP), 0x453(RIP) and
	 * 0x456(Diagnostic).
	 */

	if (ipx_primary_net && intrfc != ipx_primary_net) {
		const int dsock = ntohs(ipx->ipx_dest.sock);

		if (dsock == 0x452 || dsock == 0x453 || dsock == 0x456)
			/* The appropriate thing to do here is to dup the
			 * packet and route to the primary net interface via
			 * ipxitf_send; however, we'll cheat and just demux it
			 * here. */
			sock2 = ipxitf_find_socket(ipx_primary_net,
							ipx->ipx_dest.sock);
	}

	/*
	 * If there is nothing to do return. The kfree will cancel any charging.
	 */
	rc = 0;
	if (!sock1 && !sock2) {
		if (!copy)
			kfree_skb(skb);
		goto out;
	}

	/*
	 * This next segment of code is a little awkward, but it sets it up
	 * so that the appropriate number of copies of the SKB are made and
	 * that skb1 and skb2 point to it (them) so that it (they) can be
	 * demuxed to sock1 and/or sock2.  If we are unable to make enough
	 * copies, we do as much as is possible.
	 */

	if (copy)
		skb1 = skb_clone(skb, GFP_ATOMIC);
	else
		skb1 = skb;

	rc = -ENOMEM;
	if (!skb1)
		goto out_put;

	/* Do we need 2 SKBs? */
	if (sock1 && sock2)
		skb2 = skb_clone(skb1, GFP_ATOMIC);
	else
		skb2 = skb1;

	if (sock1)
		ipxitf_def_skb_handler(sock1, skb1);

	if (!skb2)
		goto out_put;

	if (sock2)
		ipxitf_def_skb_handler(sock2, skb2);

	rc = 0;
out_put:
	if (sock1)
		sock_put(sock1);
	if (sock2)
		sock_put(sock2);
out:
	return rc;
}
#endif	/* CONFIG_IPX_INTERN */

static struct sk_buff *ipxitf_adjust_skbuff(struct ipx_interface *intrfc,
					    struct sk_buff *skb)
{
	struct sk_buff *skb2;
	int in_offset = (unsigned char *)ipx_hdr(skb) - skb->head;
	int out_offset = intrfc->if_ipx_offset;
	int len;

	/* Hopefully, most cases */
	if (in_offset >= out_offset)
		return skb;

	/* Need new SKB */
	len  = skb->len + out_offset;
	skb2 = alloc_skb(len, GFP_ATOMIC);
	if (skb2) {
		skb_reserve(skb2, out_offset);
		skb_reset_network_header(skb2);
		skb_reset_transport_header(skb2);
		skb_put(skb2, skb->len);
		memcpy(ipx_hdr(skb2), ipx_hdr(skb), skb->len);
		memcpy(skb2->cb, skb->cb, sizeof(skb->cb));
	}
	kfree_skb(skb);
	return skb2;
}

/* caller must hold a reference to intrfc and the skb has to be unshared */
int ipxitf_send(struct ipx_interface *intrfc, struct sk_buff *skb, char *node)
{
	struct ipxhdr *ipx = ipx_hdr(skb);
	struct net_device *dev = intrfc->if_dev;
	struct datalink_proto *dl = intrfc->if_dlink;
	char dest_node[IPX_NODE_LEN];
	int send_to_wire = 1;
	int addr_len;

	ipx->ipx_tctrl = IPX_SKB_CB(skb)->ipx_tctrl;
	ipx->ipx_dest.net = IPX_SKB_CB(skb)->ipx_dest_net;
	ipx->ipx_source.net = IPX_SKB_CB(skb)->ipx_source_net;

	/* see if we need to include the netnum in the route list */
	if (IPX_SKB_CB(skb)->last_hop.index >= 0) {
		__be32 *last_hop = (__be32 *)(((u8 *) skb->data) +
				sizeof(struct ipxhdr) +
				IPX_SKB_CB(skb)->last_hop.index *
				sizeof(__be32));
		*last_hop = IPX_SKB_CB(skb)->last_hop.netnum;
		IPX_SKB_CB(skb)->last_hop.index = -1;
	}

	/*
	 * We need to know how many skbuffs it will take to send out this
	 * packet to avoid unnecessary copies.
	 */

	if (!dl || !dev || dev->flags & IFF_LOOPBACK)
		send_to_wire = 0;	/* No non looped */

	/*
	 * See if this should be demuxed to sockets on this interface
	 *
	 * We want to ensure the original was eaten or that we only use
	 * up clones.
	 */

	if (ipx->ipx_dest.net == intrfc->if_netnum) {
		/*
		 * To our own node, loop and free the original.
		 * The internal net will receive on all node address.
		 */
		if (intrfc == ipx_internal_net ||
		    !memcmp(intrfc->if_node, node, IPX_NODE_LEN)) {
			/* Don't charge sender */
			skb_orphan(skb);

			/* Will charge receiver */
			return ipxitf_demux_socket(intrfc, skb, 0);
		}

		/* Broadcast, loop and possibly keep to send on. */
		if (!memcmp(ipx_broadcast_node, node, IPX_NODE_LEN)) {
			if (!send_to_wire)
				skb_orphan(skb);
			ipxitf_demux_socket(intrfc, skb, send_to_wire);
			if (!send_to_wire)
				goto out;
		}
	}

	/*
	 * If the originating net is not equal to our net; this is routed
	 * We are still charging the sender. Which is right - the driver
	 * free will handle this fairly.
	 */
	if (ipx->ipx_source.net != intrfc->if_netnum) {
		/*
		 * Unshare the buffer before modifying the count in
		 * case it's a flood or tcpdump
		 */
		skb = skb_unshare(skb, GFP_ATOMIC);
		if (!skb)
			goto out;
		if (++ipx->ipx_tctrl > ipxcfg_max_hops)
			send_to_wire = 0;
	}

	if (!send_to_wire) {
		kfree_skb(skb);
		goto out;
	}

	/* Determine the appropriate hardware address */
	addr_len = dev->addr_len;
	if (!memcmp(ipx_broadcast_node, node, IPX_NODE_LEN))
		memcpy(dest_node, dev->broadcast, addr_len);
	else
		memcpy(dest_node, &(node[IPX_NODE_LEN-addr_len]), addr_len);

	/* Make any compensation for differing physical/data link size */
	skb = ipxitf_adjust_skbuff(intrfc, skb);
	if (!skb)
		goto out;

	/* set up data link and physical headers */
	skb->dev	= dev;
	skb->protocol	= htons(ETH_P_IPX);

	/* Send it out */
	dl->request(dl, skb, dest_node);
out:
	return 0;
}

static int ipxitf_add_local_route(struct ipx_interface *intrfc)
{
	return ipxrtr_add_route(intrfc->if_netnum, intrfc, NULL);
}

static void ipxitf_discover_netnum(struct ipx_interface *intrfc,
				   struct sk_buff *skb);
static int ipxitf_pprop(struct ipx_interface *intrfc, struct sk_buff *skb);

static int ipxitf_rcv(struct ipx_interface *intrfc, struct sk_buff *skb)
{
	struct ipxhdr *ipx = ipx_hdr(skb);
	int rc = 0;

	ipxitf_hold(intrfc);

	/* See if we should update our network number */
	if (!intrfc->if_netnum) /* net number of intrfc not known yet */
		ipxitf_discover_netnum(intrfc, skb);

	IPX_SKB_CB(skb)->last_hop.index = -1;
	if (ipx->ipx_type == IPX_TYPE_PPROP) {
		rc = ipxitf_pprop(intrfc, skb);
		if (rc)
			goto out_free_skb;
	}

	/* local processing follows */
	if (!IPX_SKB_CB(skb)->ipx_dest_net)
		IPX_SKB_CB(skb)->ipx_dest_net = intrfc->if_netnum;
	if (!IPX_SKB_CB(skb)->ipx_source_net)
		IPX_SKB_CB(skb)->ipx_source_net = intrfc->if_netnum;

	/* it doesn't make sense to route a pprop packet, there's no meaning
	 * in the ipx_dest_net for such packets */
	if (ipx->ipx_type != IPX_TYPE_PPROP &&
	    intrfc->if_netnum != IPX_SKB_CB(skb)->ipx_dest_net) {
		/* We only route point-to-point packets. */
		if (skb->pkt_type == PACKET_HOST) {
			skb = skb_unshare(skb, GFP_ATOMIC);
			if (skb)
				rc = ipxrtr_route_skb(skb);
			goto out_intrfc;
		}

		goto out_free_skb;
	}

	/* see if we should keep it */
	if (!memcmp(ipx_broadcast_node, ipx->ipx_dest.node, IPX_NODE_LEN) ||
	    !memcmp(intrfc->if_node, ipx->ipx_dest.node, IPX_NODE_LEN)) {
		rc = ipxitf_demux_socket(intrfc, skb, 0);
		goto out_intrfc;
	}

	/* we couldn't pawn it off so unload it */
out_free_skb:
	kfree_skb(skb);
out_intrfc:
	ipxitf_put(intrfc);
	return rc;
}

static void ipxitf_discover_netnum(struct ipx_interface *intrfc,
				   struct sk_buff *skb)
{
	const struct ipx_cb *cb = IPX_SKB_CB(skb);

	/* see if this is an intra packet: source_net == dest_net */
	if (cb->ipx_source_net == cb->ipx_dest_net && cb->ipx_source_net) {
		struct ipx_interface *i =
				ipxitf_find_using_net(cb->ipx_source_net);
		/* NB: NetWare servers lie about their hop count so we
		 * dropped the test based on it. This is the best way
		 * to determine this is a 0 hop count packet. */
		if (!i) {
			intrfc->if_netnum = cb->ipx_source_net;
			ipxitf_add_local_route(intrfc);
		} else {
			printk(KERN_WARNING "IPX: Network number collision "
				"%lx\n        %s %s and %s %s\n",
				(unsigned long) ntohl(cb->ipx_source_net),
				ipx_device_name(i),
				ipx_frame_name(i->if_dlink_type),
				ipx_device_name(intrfc),
				ipx_frame_name(intrfc->if_dlink_type));
			ipxitf_put(i);
		}
	}
}

/**
 * ipxitf_pprop - Process packet propagation IPX packet type 0x14, used for
 * 		  NetBIOS broadcasts
 * @intrfc: IPX interface receiving this packet
 * @skb: Received packet
 *
 * Checks if packet is valid: if its more than %IPX_MAX_PPROP_HOPS hops or if it
 * is smaller than a IPX header + the room for %IPX_MAX_PPROP_HOPS hops we drop
 * it, not even processing it locally, if it has exact %IPX_MAX_PPROP_HOPS we
 * don't broadcast it, but process it locally. See chapter 5 of Novell's "IPX
 * RIP and SAP Router Specification", Part Number 107-000029-001.
 *
 * If it is valid, check if we have pprop broadcasting enabled by the user,
 * if not, just return zero for local processing.
 *
 * If it is enabled check the packet and don't broadcast it if we have already
 * seen this packet.
 *
 * Broadcast: send it to the interfaces that aren't on the packet visited nets
 * array, just after the IPX header.
 *
 * Returns -EINVAL for invalid packets, so that the calling function drops
 * the packet without local processing. 0 if packet is to be locally processed.
 */
static int ipxitf_pprop(struct ipx_interface *intrfc, struct sk_buff *skb)
{
	struct ipxhdr *ipx = ipx_hdr(skb);
	int i, rc = -EINVAL;
	struct ipx_interface *ifcs;
	char *c;
	__be32 *l;

	/* Illegal packet - too many hops or too short */
	/* We decide to throw it away: no broadcasting, no local processing.
	 * NetBIOS unaware implementations route them as normal packets -
	 * tctrl <= 15, any data payload... */
	if (IPX_SKB_CB(skb)->ipx_tctrl > IPX_MAX_PPROP_HOPS ||
	    ntohs(ipx->ipx_pktsize) < sizeof(struct ipxhdr) +
					IPX_MAX_PPROP_HOPS * sizeof(u32))
		goto out;
	/* are we broadcasting this damn thing? */
	rc = 0;
	if (!sysctl_ipx_pprop_broadcasting)
		goto out;
	/* We do broadcast packet on the IPX_MAX_PPROP_HOPS hop, but we
	 * process it locally. All previous hops broadcasted it, and process it
	 * locally. */
	if (IPX_SKB_CB(skb)->ipx_tctrl == IPX_MAX_PPROP_HOPS)
		goto out;

	c = ((u8 *) ipx) + sizeof(struct ipxhdr);
	l = (__be32 *) c;

	/* Don't broadcast packet if already seen this net */
	for (i = 0; i < IPX_SKB_CB(skb)->ipx_tctrl; i++)
		if (*l++ == intrfc->if_netnum)
			goto out;

	/* < IPX_MAX_PPROP_HOPS hops && input interface not in list. Save the
	 * position where we will insert recvd netnum into list, later on,
	 * in ipxitf_send */
	IPX_SKB_CB(skb)->last_hop.index = i;
	IPX_SKB_CB(skb)->last_hop.netnum = intrfc->if_netnum;
	/* xmit on all other interfaces... */
	spin_lock_bh(&ipx_interfaces_lock);
	list_for_each_entry(ifcs, &ipx_interfaces, node) {
		/* Except unconfigured interfaces */
		if (!ifcs->if_netnum)
			continue;

		/* That aren't in the list */
		if (ifcs == intrfc)
			continue;
		l = (__be32 *) c;
		/* don't consider the last entry in the packet list,
		 * it is our netnum, and it is not there yet */
		for (i = 0; i < IPX_SKB_CB(skb)->ipx_tctrl; i++)
			if (ifcs->if_netnum == *l++)
				break;
		if (i == IPX_SKB_CB(skb)->ipx_tctrl) {
			struct sk_buff *s = skb_copy(skb, GFP_ATOMIC);

			if (s) {
				IPX_SKB_CB(s)->ipx_dest_net = ifcs->if_netnum;
				ipxrtr_route_skb(s);
			}
		}
	}
	spin_unlock_bh(&ipx_interfaces_lock);
out:
	return rc;
}

static void ipxitf_insert(struct ipx_interface *intrfc)
{
	spin_lock_bh(&ipx_interfaces_lock);
	list_add_tail(&intrfc->node, &ipx_interfaces);
	spin_unlock_bh(&ipx_interfaces_lock);

	if (ipxcfg_auto_select_primary && !ipx_primary_net)
		ipx_primary_net = intrfc;
}

static struct ipx_interface *ipxitf_alloc(struct net_device *dev, __be32 netnum,
					  __be16 dlink_type,
					  struct datalink_proto *dlink,
					  unsigned char internal,
					  int ipx_offset)
{
	struct ipx_interface *intrfc = kmalloc(sizeof(*intrfc), GFP_ATOMIC);

	if (intrfc) {
		intrfc->if_dev		= dev;
		intrfc->if_netnum	= netnum;
		intrfc->if_dlink_type 	= dlink_type;
		intrfc->if_dlink 	= dlink;
		intrfc->if_internal 	= internal;
		intrfc->if_ipx_offset 	= ipx_offset;
		intrfc->if_sknum 	= IPX_MIN_EPHEMERAL_SOCKET;
		INIT_HLIST_HEAD(&intrfc->if_sklist);
		atomic_set(&intrfc->refcnt, 1);
		spin_lock_init(&intrfc->if_sklist_lock);
	}

	return intrfc;
}

static int ipxitf_create_internal(struct ipx_interface_definition *idef)
{
	struct ipx_interface *intrfc;
	int rc = -EEXIST;

	/* Only one primary network allowed */
	if (ipx_primary_net)
		goto out;

	/* Must have a valid network number */
	rc = -EADDRNOTAVAIL;
	if (!idef->ipx_network)
		goto out;
	intrfc = ipxitf_find_using_net(idef->ipx_network);
	rc = -EADDRINUSE;
	if (intrfc) {
		ipxitf_put(intrfc);
		goto out;
	}
	intrfc = ipxitf_alloc(NULL, idef->ipx_network, 0, NULL, 1, 0);
	rc = -EAGAIN;
	if (!intrfc)
		goto out;
	memcpy((char *)&(intrfc->if_node), idef->ipx_node, IPX_NODE_LEN);
	ipx_internal_net = ipx_primary_net = intrfc;
	ipxitf_hold(intrfc);
	ipxitf_insert(intrfc);

	rc = ipxitf_add_local_route(intrfc);
	ipxitf_put(intrfc);
out:
	return rc;
}

static __be16 ipx_map_frame_type(unsigned char type)
{
	__be16 rc = 0;

	switch (type) {
	case IPX_FRAME_ETHERII:	rc = htons(ETH_P_IPX);		break;
	case IPX_FRAME_8022:	rc = htons(ETH_P_802_2);	break;
	case IPX_FRAME_SNAP:	rc = htons(ETH_P_SNAP);		break;
	case IPX_FRAME_8023:	rc = htons(ETH_P_802_3);	break;
	}

	return rc;
}

static int ipxitf_create(struct ipx_interface_definition *idef)
{
	struct net_device *dev;
	__be16 dlink_type = 0;
	struct datalink_proto *datalink = NULL;
	struct ipx_interface *intrfc;
	int rc;

	if (idef->ipx_special == IPX_INTERNAL) {
		rc = ipxitf_create_internal(idef);
		goto out;
	}

	rc = -EEXIST;
	if (idef->ipx_special == IPX_PRIMARY && ipx_primary_net)
		goto out;

	intrfc = ipxitf_find_using_net(idef->ipx_network);
	rc = -EADDRINUSE;
	if (idef->ipx_network && intrfc) {
		ipxitf_put(intrfc);
		goto out;
	}

	if (intrfc)
		ipxitf_put(intrfc);

	dev = dev_get_by_name(&init_net, idef->ipx_device);
	rc = -ENODEV;
	if (!dev)
		goto out;

	switch (idef->ipx_dlink_type) {
	case IPX_FRAME_8022:
		dlink_type 	= htons(ETH_P_802_2);
		datalink 	= p8022_datalink;
		break;
	case IPX_FRAME_ETHERII:
		if (dev->type != ARPHRD_IEEE802) {
			dlink_type 	= htons(ETH_P_IPX);
			datalink 	= pEII_datalink;
			break;
		}
		/* fall through */
	case IPX_FRAME_SNAP:
		dlink_type 	= htons(ETH_P_SNAP);
		datalink 	= pSNAP_datalink;
		break;
	case IPX_FRAME_8023:
		dlink_type 	= htons(ETH_P_802_3);
		datalink 	= p8023_datalink;
		break;
	case IPX_FRAME_NONE:
	default:
		rc = -EPROTONOSUPPORT;
		goto out_dev;
	}

	rc = -ENETDOWN;
	if (!(dev->flags & IFF_UP))
		goto out_dev;

	/* Check addresses are suitable */
	rc = -EINVAL;
	if (dev->addr_len > IPX_NODE_LEN)
		goto out_dev;

	intrfc = ipxitf_find_using_phys(dev, dlink_type);
	if (!intrfc) {
		/* Ok now create */
		intrfc = ipxitf_alloc(dev, idef->ipx_network, dlink_type,
				      datalink, 0, dev->hard_header_len +
					datalink->header_length);
		rc = -EAGAIN;
		if (!intrfc)
			goto out_dev;
		/* Setup primary if necessary */
		if (idef->ipx_special == IPX_PRIMARY)
			ipx_primary_net = intrfc;
		if (!memcmp(idef->ipx_node, "\000\000\000\000\000\000",
			    IPX_NODE_LEN)) {
			memset(intrfc->if_node, 0, IPX_NODE_LEN);
			memcpy(intrfc->if_node + IPX_NODE_LEN - dev->addr_len,
				dev->dev_addr, dev->addr_len);
		} else
			memcpy(intrfc->if_node, idef->ipx_node, IPX_NODE_LEN);
		ipxitf_hold(intrfc);
		ipxitf_insert(intrfc);
	}


	/* If the network number is known, add a route */
	rc = 0;
	if (!intrfc->if_netnum)
		goto out_intrfc;

	rc = ipxitf_add_local_route(intrfc);
out_intrfc:
	ipxitf_put(intrfc);
	goto out;
out_dev:
	dev_put(dev);
out:
	return rc;
}

static int ipxitf_delete(struct ipx_interface_definition *idef)
{
	struct net_device *dev = NULL;
	__be16 dlink_type = 0;
	struct ipx_interface *intrfc;
	int rc = 0;

	spin_lock_bh(&ipx_interfaces_lock);
	if (idef->ipx_special == IPX_INTERNAL) {
		if (ipx_internal_net) {
			__ipxitf_put(ipx_internal_net);
			goto out;
		}
		rc = -ENOENT;
		goto out;
	}

	dlink_type = ipx_map_frame_type(idef->ipx_dlink_type);
	rc = -EPROTONOSUPPORT;
	if (!dlink_type)
		goto out;

	dev = __dev_get_by_name(&init_net, idef->ipx_device);
	rc = -ENODEV;
	if (!dev)
		goto out;

	intrfc = __ipxitf_find_using_phys(dev, dlink_type);
	rc = -EINVAL;
	if (!intrfc)
		goto out;
	__ipxitf_put(intrfc);

	rc = 0;
out:
	spin_unlock_bh(&ipx_interfaces_lock);
	return rc;
}

static struct ipx_interface *ipxitf_auto_create(struct net_device *dev,
						__be16 dlink_type)
{
	struct ipx_interface *intrfc = NULL;
	struct datalink_proto *datalink;

	if (!dev)
		goto out;

	/* Check addresses are suitable */
	if (dev->addr_len > IPX_NODE_LEN)
		goto out;

	switch (ntohs(dlink_type)) {
	case ETH_P_IPX:		datalink = pEII_datalink;	break;
	case ETH_P_802_2:	datalink = p8022_datalink;	break;
	case ETH_P_SNAP:	datalink = pSNAP_datalink;	break;
	case ETH_P_802_3:	datalink = p8023_datalink;	break;
	default:		goto out;
	}

	intrfc = ipxitf_alloc(dev, 0, dlink_type, datalink, 0,
				dev->hard_header_len + datalink->header_length);

	if (intrfc) {
		memset(intrfc->if_node, 0, IPX_NODE_LEN);
		memcpy((char *)&(intrfc->if_node[IPX_NODE_LEN-dev->addr_len]),
			dev->dev_addr, dev->addr_len);
		spin_lock_init(&intrfc->if_sklist_lock);
		atomic_set(&intrfc->refcnt, 1);
		ipxitf_insert(intrfc);
		dev_hold(dev);
	}

out:
	return intrfc;
}

static int ipxitf_ioctl(unsigned int cmd, void __user *arg)
{
	int rc = -EINVAL;
	struct ifreq ifr;
	int val;

	switch (cmd) {
	case SIOCSIFADDR: {
		struct sockaddr_ipx *sipx;
		struct ipx_interface_definition f;

		rc = -EFAULT;
		if (copy_from_user(&ifr, arg, sizeof(ifr)))
			break;
		sipx = (struct sockaddr_ipx *)&ifr.ifr_addr;
		rc = -EINVAL;
		if (sipx->sipx_family != AF_IPX)
			break;
		f.ipx_network = sipx->sipx_network;
		memcpy(f.ipx_device, ifr.ifr_name,
			sizeof(f.ipx_device));
		memcpy(f.ipx_node, sipx->sipx_node, IPX_NODE_LEN);
		f.ipx_dlink_type = sipx->sipx_type;
		f.ipx_special = sipx->sipx_special;

		if (sipx->sipx_action == IPX_DLTITF)
			rc = ipxitf_delete(&f);
		else
			rc = ipxitf_create(&f);
		break;
	}
	case SIOCGIFADDR: {
		struct sockaddr_ipx *sipx;
		struct ipx_interface *ipxif;
		struct net_device *dev;

		rc = -EFAULT;
		if (copy_from_user(&ifr, arg, sizeof(ifr)))
			break;
		sipx = (struct sockaddr_ipx *)&ifr.ifr_addr;
		dev  = __dev_get_by_name(&init_net, ifr.ifr_name);
		rc   = -ENODEV;
		if (!dev)
			break;
		ipxif = ipxitf_find_using_phys(dev,
					   ipx_map_frame_type(sipx->sipx_type));
		rc = -EADDRNOTAVAIL;
		if (!ipxif)
			break;

		sipx->sipx_family	= AF_IPX;
		sipx->sipx_network	= ipxif->if_netnum;
		memcpy(sipx->sipx_node, ipxif->if_node,
			sizeof(sipx->sipx_node));
		rc = -EFAULT;
		if (copy_to_user(arg, &ifr, sizeof(ifr)))
			break;
		ipxitf_put(ipxif);
		rc = 0;
		break;
	}
	case SIOCAIPXITFCRT:
		rc = -EFAULT;
		if (get_user(val, (unsigned char __user *) arg))
			break;
		rc = 0;
		ipxcfg_auto_create_interfaces = val;
		break;
	case SIOCAIPXPRISLT:
		rc = -EFAULT;
		if (get_user(val, (unsigned char __user *) arg))
			break;
		rc = 0;
		ipxcfg_set_auto_select(val);
		break;
	}

	return rc;
}

/*
 *	Checksum routine for IPX
 */

/* Note: We assume ipx_tctrl==0 and htons(length)==ipx_pktsize */
/* This functions should *not* mess with packet contents */

__be16 ipx_cksum(struct ipxhdr *packet, int length)
{
	/*
	 *	NOTE: sum is a net byte order quantity, which optimizes the
	 *	loop. This only works on big and little endian machines. (I
	 *	don't know of a machine that isn't.)
	 */
	/* handle the first 3 words separately; checksum should be skipped
	 * and ipx_tctrl masked out */
	__u16 *p = (__u16 *)packet;
	__u32 sum = p[1] + (p[2] & (__force u16)htons(0x00ff));
	__u32 i = (length >> 1) - 3; /* Number of remaining complete words */

	/* Loop through them */
	p += 3;
	while (i--)
		sum += *p++;

	/* Add on the last part word if it exists */
	if (packet->ipx_pktsize & htons(1))
		sum += (__force u16)htons(0xff00) & *p;

	/* Do final fixup */
	sum = (sum & 0xffff) + (sum >> 16);

	/* It's a pity there's no concept of carry in C */
	if (sum >= 0x10000)
		sum++;

	/*
	 * Leave 0 alone; we don't want 0xffff here.  Note that we can't get
	 * here with 0x10000, so this check is the same as ((__u16)sum)
	 */
	if (sum)
		sum = ~sum;

	return (__force __be16)sum;
}

const char *ipx_frame_name(__be16 frame)
{
	char* rc = "None";

	switch (ntohs(frame)) {
	case ETH_P_IPX:		rc = "EtherII";	break;
	case ETH_P_802_2:	rc = "802.2";	break;
	case ETH_P_SNAP:	rc = "SNAP";	break;
	case ETH_P_802_3:	rc = "802.3";	break;
	}

	return rc;
}

const char *ipx_device_name(struct ipx_interface *intrfc)
{
	return intrfc->if_internal ? "Internal" :
		intrfc->if_dev ? intrfc->if_dev->name : "Unknown";
}

/* Handling for system calls applied via the various interfaces to an IPX
 * socket object. */

static int ipx_setsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	int opt;
	int rc = -EINVAL;

	lock_sock(sk);
	if (optlen != sizeof(int))
		goto out;

	rc = -EFAULT;
	if (get_user(opt, (unsigned int __user *)optval))
		goto out;

	rc = -ENOPROTOOPT;
	if (!(level == SOL_IPX && optname == IPX_TYPE))
		goto out;

	ipx_sk(sk)->type = opt;
	rc = 0;
out:
	release_sock(sk);
	return rc;
}

static int ipx_getsockopt(struct socket *sock, int level, int optname,
	char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	int val = 0;
	int len;
	int rc = -ENOPROTOOPT;

	lock_sock(sk);
	if (!(level == SOL_IPX && optname == IPX_TYPE))
		goto out;

	val = ipx_sk(sk)->type;

	rc = -EFAULT;
	if (get_user(len, optlen))
		goto out;

	len = min_t(unsigned int, len, sizeof(int));
	rc = -EINVAL;
	if(len < 0)
		goto out;

	rc = -EFAULT;
	if (put_user(len, optlen) || copy_to_user(optval, &val, len))
		goto out;

	rc = 0;
out:
	release_sock(sk);
	return rc;
}

static struct proto ipx_proto = {
	.name	  = "IPX",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct ipx_sock),
};

static int ipx_create(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	int rc = -ESOCKTNOSUPPORT;
	struct sock *sk;

	if (!net_eq(net, &init_net))
		return -EAFNOSUPPORT;

	/*
	 * SPX support is not anymore in the kernel sources. If you want to
	 * ressurrect it, completing it and making it understand shared skbs,
	 * be fully multithreaded, etc, grab the sources in an early 2.5 kernel
	 * tree.
	 */
	if (sock->type != SOCK_DGRAM)
		goto out;

	rc = -ENOMEM;
	sk = sk_alloc(net, PF_IPX, GFP_KERNEL, &ipx_proto, kern);
	if (!sk)
		goto out;

	sk_refcnt_debug_inc(sk);
	sock_init_data(sock, sk);
	sk->sk_no_check_tx = 1;		/* Checksum off by default */
	sock->ops = &ipx_dgram_ops;
	rc = 0;
out:
	return rc;
}

static int ipx_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (!sk)
		goto out;

	lock_sock(sk);
	sk->sk_shutdown = SHUTDOWN_MASK;
	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_state_change(sk);

	sock_set_flag(sk, SOCK_DEAD);
	sock->sk = NULL;
	sk_refcnt_debug_release(sk);
	ipx_destroy_socket(sk);
	release_sock(sk);
	sock_put(sk);
out:
	return 0;
}

/* caller must hold a reference to intrfc */

static __be16 ipx_first_free_socketnum(struct ipx_interface *intrfc)
{
	unsigned short socketNum = intrfc->if_sknum;

	spin_lock_bh(&intrfc->if_sklist_lock);

	if (socketNum < IPX_MIN_EPHEMERAL_SOCKET)
		socketNum = IPX_MIN_EPHEMERAL_SOCKET;

	while (__ipxitf_find_socket(intrfc, htons(socketNum)))
		if (socketNum > IPX_MAX_EPHEMERAL_SOCKET)
			socketNum = IPX_MIN_EPHEMERAL_SOCKET;
		else
			socketNum++;

	spin_unlock_bh(&intrfc->if_sklist_lock);
	intrfc->if_sknum = socketNum;

	return htons(socketNum);
}

static int __ipx_bind(struct socket *sock,
			struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct ipx_sock *ipxs = ipx_sk(sk);
	struct ipx_interface *intrfc;
	struct sockaddr_ipx *addr = (struct sockaddr_ipx *)uaddr;
	int rc = -EINVAL;

	if (!sock_flag(sk, SOCK_ZAPPED) || addr_len != sizeof(struct sockaddr_ipx))
		goto out;

	intrfc = ipxitf_find_using_net(addr->sipx_network);
	rc = -EADDRNOTAVAIL;
	if (!intrfc)
		goto out;

	if (!addr->sipx_port) {
		addr->sipx_port = ipx_first_free_socketnum(intrfc);
		rc = -EINVAL;
		if (!addr->sipx_port)
			goto out_put;
	}

	/* protect IPX system stuff like routing/sap */
	rc = -EACCES;
	if (ntohs(addr->sipx_port) < IPX_MIN_EPHEMERAL_SOCKET &&
	    !capable(CAP_NET_ADMIN))
		goto out_put;

	ipxs->port = addr->sipx_port;

#ifdef CONFIG_IPX_INTERN
	if (intrfc == ipx_internal_net) {
		/* The source address is to be set explicitly if the
		 * socket is to be bound on the internal network. If a
		 * node number 0 was specified, the default is used.
		 */

		rc = -EINVAL;
		if (!memcmp(addr->sipx_node, ipx_broadcast_node, IPX_NODE_LEN))
			goto out_put;
		if (!memcmp(addr->sipx_node, ipx_this_node, IPX_NODE_LEN))
			memcpy(ipxs->node, intrfc->if_node, IPX_NODE_LEN);
		else
			memcpy(ipxs->node, addr->sipx_node, IPX_NODE_LEN);

		rc = -EADDRINUSE;
		if (ipxitf_find_internal_socket(intrfc, ipxs->node,
						ipxs->port)) {
			SOCK_DEBUG(sk,
				"IPX: bind failed because port %X in use.\n",
				ntohs(addr->sipx_port));
			goto out_put;
		}
	} else {
		/* Source addresses are easy. It must be our
		 * network:node pair for an interface routed to IPX
		 * with the ipx routing ioctl()
		 */

		memcpy(ipxs->node, intrfc->if_node, IPX_NODE_LEN);

		rc = -EADDRINUSE;
		if (ipxitf_find_socket(intrfc, addr->sipx_port)) {
			SOCK_DEBUG(sk,
				"IPX: bind failed because port %X in use.\n",
				ntohs(addr->sipx_port));
			goto out_put;
		}
	}

#else	/* !def CONFIG_IPX_INTERN */

	/* Source addresses are easy. It must be our network:node pair for
	   an interface routed to IPX with the ipx routing ioctl() */

	rc = -EADDRINUSE;
	if (ipxitf_find_socket(intrfc, addr->sipx_port)) {
		SOCK_DEBUG(sk, "IPX: bind failed because port %X in use.\n",
				ntohs((int)addr->sipx_port));
		goto out_put;
	}

#endif	/* CONFIG_IPX_INTERN */

	ipxitf_insert_socket(intrfc, sk);
	sock_reset_flag(sk, SOCK_ZAPPED);

	rc = 0;
out_put:
	ipxitf_put(intrfc);
out:
	return rc;
}

static int ipx_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	int rc;

	lock_sock(sk);
	rc = __ipx_bind(sock, uaddr, addr_len);
	release_sock(sk);

	return rc;
}

static int ipx_connect(struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct ipx_sock *ipxs = ipx_sk(sk);
	struct sockaddr_ipx *addr;
	int rc = -EINVAL;
	struct ipx_route *rt;

	sk->sk_state	= TCP_CLOSE;
	sock->state 	= SS_UNCONNECTED;

	lock_sock(sk);
	if (addr_len != sizeof(*addr))
		goto out;
	addr = (struct sockaddr_ipx *)uaddr;

	/* put the autobinding in */
	if (!ipxs->port) {
		struct sockaddr_ipx uaddr;

		uaddr.sipx_port		= 0;
		uaddr.sipx_network 	= 0;

#ifdef CONFIG_IPX_INTERN
		rc = -ENETDOWN;
		if (!ipxs->intrfc)
			goto out; /* Someone zonked the iface */
		memcpy(uaddr.sipx_node, ipxs->intrfc->if_node,
			IPX_NODE_LEN);
#endif	/* CONFIG_IPX_INTERN */

		rc = __ipx_bind(sock, (struct sockaddr *)&uaddr,
			      sizeof(struct sockaddr_ipx));
		if (rc)
			goto out;
	}

	/* We can either connect to primary network or somewhere
	 * we can route to */
	rt = ipxrtr_lookup(addr->sipx_network);
	rc = -ENETUNREACH;
	if (!rt && !(!addr->sipx_network && ipx_primary_net))
		goto out;

	ipxs->dest_addr.net  = addr->sipx_network;
	ipxs->dest_addr.sock = addr->sipx_port;
	memcpy(ipxs->dest_addr.node, addr->sipx_node, IPX_NODE_LEN);
	ipxs->type = addr->sipx_type;

	if (sock->type == SOCK_DGRAM) {
		sock->state 	= SS_CONNECTED;
		sk->sk_state 	= TCP_ESTABLISHED;
	}

	if (rt)
		ipxrtr_put(rt);
	rc = 0;
out:
	release_sock(sk);
	return rc;
}


static int ipx_getname(struct socket *sock, struct sockaddr *uaddr,
			int *uaddr_len, int peer)
{
	struct ipx_address *addr;
	struct sockaddr_ipx sipx;
	struct sock *sk = sock->sk;
	struct ipx_sock *ipxs = ipx_sk(sk);
	int rc;

	*uaddr_len = sizeof(struct sockaddr_ipx);

	lock_sock(sk);
	if (peer) {
		rc = -ENOTCONN;
		if (sk->sk_state != TCP_ESTABLISHED)
			goto out;

		addr = &ipxs->dest_addr;
		sipx.sipx_network	= addr->net;
		sipx.sipx_port		= addr->sock;
		memcpy(sipx.sipx_node, addr->node, IPX_NODE_LEN);
	} else {
		if (ipxs->intrfc) {
			sipx.sipx_network = ipxs->intrfc->if_netnum;
#ifdef CONFIG_IPX_INTERN
			memcpy(sipx.sipx_node, ipxs->node, IPX_NODE_LEN);
#else
			memcpy(sipx.sipx_node, ipxs->intrfc->if_node,
				IPX_NODE_LEN);
#endif	/* CONFIG_IPX_INTERN */

		} else {
			sipx.sipx_network = 0;
			memset(sipx.sipx_node, '\0', IPX_NODE_LEN);
		}

		sipx.sipx_port = ipxs->port;
	}

	sipx.sipx_family = AF_IPX;
	sipx.sipx_type	 = ipxs->type;
	sipx.sipx_zero	 = 0;
	memcpy(uaddr, &sipx, sizeof(sipx));

	rc = 0;
out:
	release_sock(sk);
	return rc;
}

static int ipx_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
{
	/* NULL here for pt means the packet was looped back */
	struct ipx_interface *intrfc;
	struct ipxhdr *ipx;
	u16 ipx_pktsize;
	int rc = 0;

	if (!net_eq(dev_net(dev), &init_net))
		goto drop;

	/* Not ours */
	if (skb->pkt_type == PACKET_OTHERHOST)
		goto drop;

	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)
		goto out;

	if (!pskb_may_pull(skb, sizeof(struct ipxhdr)))
		goto drop;

	ipx_pktsize = ntohs(ipx_hdr(skb)->ipx_pktsize);

	/* Too small or invalid header? */
	if (ipx_pktsize < sizeof(struct ipxhdr) ||
	    !pskb_may_pull(skb, ipx_pktsize))
		goto drop;

	ipx = ipx_hdr(skb);
	if (ipx->ipx_checksum != IPX_NO_CHECKSUM &&
	   ipx->ipx_checksum != ipx_cksum(ipx, ipx_pktsize))
		goto drop;

	IPX_SKB_CB(skb)->ipx_tctrl	= ipx->ipx_tctrl;
	IPX_SKB_CB(skb)->ipx_dest_net	= ipx->ipx_dest.net;
	IPX_SKB_CB(skb)->ipx_source_net = ipx->ipx_source.net;

	/* Determine what local ipx endpoint this is */
	intrfc = ipxitf_find_using_phys(dev, pt->type);
	if (!intrfc) {
		if (ipxcfg_auto_create_interfaces &&
		   IPX_SKB_CB(skb)->ipx_dest_net) {
			intrfc = ipxitf_auto_create(dev, pt->type);
			if (intrfc)
				ipxitf_hold(intrfc);
		}

		if (!intrfc)	/* Not one of ours */
				/* or invalid packet for auto creation */
			goto drop;
	}

	rc = ipxitf_rcv(intrfc, skb);
	ipxitf_put(intrfc);
	goto out;
drop:
	kfree_skb(skb);
out:
	return rc;
}

static int ipx_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct ipx_sock *ipxs = ipx_sk(sk);
	DECLARE_SOCKADDR(struct sockaddr_ipx *, usipx, msg->msg_name);
	struct sockaddr_ipx local_sipx;
	int rc = -EINVAL;
	int flags = msg->msg_flags;

	lock_sock(sk);
	/* Socket gets bound below anyway */
/*	if (sk->sk_zapped)
		return -EIO; */	/* Socket not bound */
	if (flags & ~(MSG_DONTWAIT|MSG_CMSG_COMPAT))
		goto out;

	/* Max possible packet size limited by 16 bit pktsize in header */
	if (len >= 65535 - sizeof(struct ipxhdr))
		goto out;

	if (usipx) {
		if (!ipxs->port) {
			struct sockaddr_ipx uaddr;

			uaddr.sipx_port		= 0;
			uaddr.sipx_network	= 0;
#ifdef CONFIG_IPX_INTERN
			rc = -ENETDOWN;
			if (!ipxs->intrfc)
				goto out; /* Someone zonked the iface */
			memcpy(uaddr.sipx_node, ipxs->intrfc->if_node,
				IPX_NODE_LEN);
#endif
			rc = __ipx_bind(sock, (struct sockaddr *)&uaddr,
					sizeof(struct sockaddr_ipx));
			if (rc)
				goto out;
		}

		rc = -EINVAL;
		if (msg->msg_namelen < sizeof(*usipx) ||
		    usipx->sipx_family != AF_IPX)
			goto out;
	} else {
		rc = -ENOTCONN;
		if (sk->sk_state != TCP_ESTABLISHED)
			goto out;

		usipx = &local_sipx;
		usipx->sipx_family 	= AF_IPX;
		usipx->sipx_type 	= ipxs->type;
		usipx->sipx_port 	= ipxs->dest_addr.sock;
		usipx->sipx_network 	= ipxs->dest_addr.net;
		memcpy(usipx->sipx_node, ipxs->dest_addr.node, IPX_NODE_LEN);
	}

	rc = ipxrtr_route_packet(sk, usipx, msg, len, flags & MSG_DONTWAIT);
	if (rc >= 0)
		rc = len;
out:
	release_sock(sk);
	return rc;
}


static int ipx_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
		       int flags)
{
	struct sock *sk = sock->sk;
	struct ipx_sock *ipxs = ipx_sk(sk);
	DECLARE_SOCKADDR(struct sockaddr_ipx *, sipx, msg->msg_name);
	struct ipxhdr *ipx = NULL;
	struct sk_buff *skb;
	int copied, rc;
	bool locked = true;

	lock_sock(sk);
	/* put the autobinding in */
	if (!ipxs->port) {
		struct sockaddr_ipx uaddr;

		uaddr.sipx_port		= 0;
		uaddr.sipx_network 	= 0;

#ifdef CONFIG_IPX_INTERN
		rc = -ENETDOWN;
		if (!ipxs->intrfc)
			goto out; /* Someone zonked the iface */
		memcpy(uaddr.sipx_node, ipxs->intrfc->if_node, IPX_NODE_LEN);
#endif	/* CONFIG_IPX_INTERN */

		rc = __ipx_bind(sock, (struct sockaddr *)&uaddr,
			      sizeof(struct sockaddr_ipx));
		if (rc)
			goto out;
	}

	rc = -ENOTCONN;
	if (sock_flag(sk, SOCK_ZAPPED))
		goto out;

	release_sock(sk);
	locked = false;
	skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT,
				flags & MSG_DONTWAIT, &rc);
	if (!skb) {
		if (rc == -EAGAIN && (sk->sk_shutdown & RCV_SHUTDOWN))
			rc = 0;
		goto out;
	}

	ipx 	= ipx_hdr(skb);
	copied 	= ntohs(ipx->ipx_pktsize) - sizeof(struct ipxhdr);
	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	rc = skb_copy_datagram_msg(skb, sizeof(struct ipxhdr), msg, copied);
	if (rc)
		goto out_free;
	if (skb->tstamp.tv64)
		sk->sk_stamp = skb->tstamp;

	if (sipx) {
		sipx->sipx_family	= AF_IPX;
		sipx->sipx_port		= ipx->ipx_source.sock;
		memcpy(sipx->sipx_node, ipx->ipx_source.node, IPX_NODE_LEN);
		sipx->sipx_network	= IPX_SKB_CB(skb)->ipx_source_net;
		sipx->sipx_type 	= ipx->ipx_type;
		sipx->sipx_zero		= 0;
		msg->msg_namelen	= sizeof(*sipx);
	}
	rc = copied;

out_free:
	skb_free_datagram(sk, skb);
out:
	if (locked)
		release_sock(sk);
	return rc;
}


static int ipx_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	long amount = 0;
	struct sock *sk = sock->sk;
	void __user *argp = (void __user *)arg;

	lock_sock(sk);
	switch (cmd) {
	case TIOCOUTQ:
		amount = sk->sk_sndbuf - sk_wmem_alloc_get(sk);
		if (amount < 0)
			amount = 0;
		rc = put_user(amount, (int __user *)argp);
		break;
	case TIOCINQ: {
		struct sk_buff *skb = skb_peek(&sk->sk_receive_queue);
		/* These two are safe on a single CPU system as only
		 * user tasks fiddle here */
		if (skb)
			amount = skb->len - sizeof(struct ipxhdr);
		rc = put_user(amount, (int __user *)argp);
		break;
	}
	case SIOCADDRT:
	case SIOCDELRT:
		rc = -EPERM;
		if (capable(CAP_NET_ADMIN))
			rc = ipxrtr_ioctl(cmd, argp);
		break;
	case SIOCSIFADDR:
	case SIOCAIPXITFCRT:
	case SIOCAIPXPRISLT:
		rc = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
	case SIOCGIFADDR:
		rc = ipxitf_ioctl(cmd, argp);
		break;
	case SIOCIPXCFGDATA:
		rc = ipxcfg_get_config_data(argp);
		break;
	case SIOCIPXNCPCONN:
		/*
		 * This socket wants to take care of the NCP connection
		 * handed to us in arg.
		 */
		rc = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		rc = get_user(ipx_sk(sk)->ipx_ncp_conn,
			      (const unsigned short __user *)argp);
		break;
	case SIOCGSTAMP:
		rc = sock_get_timestamp(sk, argp);
		break;
	case SIOCGIFDSTADDR:
	case SIOCSIFDSTADDR:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
		rc = -EINVAL;
		break;
	default:
		rc = -ENOIOCTLCMD;
		break;
	}
	release_sock(sk);

	return rc;
}


#ifdef CONFIG_COMPAT
static int ipx_compat_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	/*
	 * These 4 commands use same structure on 32bit and 64bit.  Rest of IPX
	 * commands is handled by generic ioctl code.  As these commands are
	 * SIOCPROTOPRIVATE..SIOCPROTOPRIVATE+3, they cannot be handled by generic
	 * code.
	 */
	switch (cmd) {
	case SIOCAIPXITFCRT:
	case SIOCAIPXPRISLT:
	case SIOCIPXCFGDATA:
	case SIOCIPXNCPCONN:
		return ipx_ioctl(sock, cmd, arg);
	default:
		return -ENOIOCTLCMD;
	}
}
#endif

static int ipx_shutdown(struct socket *sock, int mode)
{
	struct sock *sk = sock->sk;

	if (mode < SHUT_RD || mode > SHUT_RDWR)
		return -EINVAL;
	/* This maps:
	 * SHUT_RD   (0) -> RCV_SHUTDOWN  (1)
	 * SHUT_WR   (1) -> SEND_SHUTDOWN (2)
	 * SHUT_RDWR (2) -> SHUTDOWN_MASK (3)
	 */
	++mode;

	lock_sock(sk);
	sk->sk_shutdown |= mode;
	release_sock(sk);
	sk->sk_state_change(sk);

	return 0;
}

/*
 * Socket family declarations
 */

static const struct net_proto_family ipx_family_ops = {
	.family		= PF_IPX,
	.create		= ipx_create,
	.owner		= THIS_MODULE,
};

static const struct proto_ops ipx_dgram_ops = {
	.family		= PF_IPX,
	.owner		= THIS_MODULE,
	.release	= ipx_release,
	.bind		= ipx_bind,
	.connect	= ipx_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.getname	= ipx_getname,
	.poll		= datagram_poll,
	.ioctl		= ipx_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ipx_compat_ioctl,
#endif
	.listen		= sock_no_listen,
	.shutdown	= ipx_shutdown,
	.setsockopt	= ipx_setsockopt,
	.getsockopt	= ipx_getsockopt,
	.sendmsg	= ipx_sendmsg,
	.recvmsg	= ipx_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage,
};

static struct packet_type ipx_8023_packet_type __read_mostly = {
	.type		= cpu_to_be16(ETH_P_802_3),
	.func		= ipx_rcv,
};

static struct packet_type ipx_dix_packet_type __read_mostly = {
	.type		= cpu_to_be16(ETH_P_IPX),
	.func		= ipx_rcv,
};

static struct notifier_block ipx_dev_notifier = {
	.notifier_call	= ipxitf_device_event,
};

static const unsigned char ipx_8022_type = 0xE0;
static const unsigned char ipx_snap_id[5] = { 0x0, 0x0, 0x0, 0x81, 0x37 };
static const char ipx_EII_err_msg[] __initconst =
	KERN_CRIT "IPX: Unable to register with Ethernet II\n";
static const char ipx_8023_err_msg[] __initconst =
	KERN_CRIT "IPX: Unable to register with 802.3\n";
static const char ipx_llc_err_msg[] __initconst =
	KERN_CRIT "IPX: Unable to register with 802.2\n";
static const char ipx_snap_err_msg[] __initconst =
	KERN_CRIT "IPX: Unable to register with SNAP\n";

static int __init ipx_init(void)
{
	int rc = proto_register(&ipx_proto, 1);

	if (rc != 0)
		goto out;

	sock_register(&ipx_family_ops);

	pEII_datalink = make_EII_client();
	if (pEII_datalink)
		dev_add_pack(&ipx_dix_packet_type);
	else
		printk(ipx_EII_err_msg);

	p8023_datalink = make_8023_client();
	if (p8023_datalink)
		dev_add_pack(&ipx_8023_packet_type);
	else
		printk(ipx_8023_err_msg);

	p8022_datalink = register_8022_client(ipx_8022_type, ipx_rcv);
	if (!p8022_datalink)
		printk(ipx_llc_err_msg);

	pSNAP_datalink = register_snap_client(ipx_snap_id, ipx_rcv);
	if (!pSNAP_datalink)
		printk(ipx_snap_err_msg);

	register_netdevice_notifier(&ipx_dev_notifier);
	ipx_register_sysctl();
	ipx_proc_init();
out:
	return rc;
}

static void __exit ipx_proto_finito(void)
{
	ipx_proc_exit();
	ipx_unregister_sysctl();

	unregister_netdevice_notifier(&ipx_dev_notifier);

	ipxitf_cleanup();

	if (pSNAP_datalink) {
		unregister_snap_client(pSNAP_datalink);
		pSNAP_datalink = NULL;
	}

	if (p8022_datalink) {
		unregister_8022_client(p8022_datalink);
		p8022_datalink = NULL;
	}

	dev_remove_pack(&ipx_8023_packet_type);
	if (p8023_datalink) {
		destroy_8023_client(p8023_datalink);
		p8023_datalink = NULL;
	}

	dev_remove_pack(&ipx_dix_packet_type);
	if (pEII_datalink) {
		destroy_EII_client(pEII_datalink);
		pEII_datalink = NULL;
	}

	proto_unregister(&ipx_proto);
	sock_unregister(ipx_family_ops.family);
}

module_init(ipx_init);
module_exit(ipx_proto_finito);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_IPX);
