#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/mpls.h>
#include <net/ip.h>
#include <net/dst.h>
#include <net/sock.h>
#include <net/arp.h>
#include <net/ip_fib.h>
#include <net/netevent.h>
#include <net/netns/generic.h>
#include "internal.h"

#define MAX_NEW_LABELS 2

/* This maximum ha length copied from the definition of struct neighbour */
#define MAX_VIA_ALEN (ALIGN(MAX_ADDR_LEN, sizeof(unsigned long)))

struct mpls_route { /* next hop label forwarding entry */
	struct net_device 	*rt_dev;
	struct rcu_head		rt_rcu;
	u32			rt_label[MAX_NEW_LABELS];
	u8			rt_protocol; /* routing protocol that set this entry */
	u8			rt_labels:2,
				rt_via_alen:6;
	unsigned short		rt_via_family;
	u8			rt_via[0];
};

static struct mpls_route *mpls_route_input_rcu(struct net *net, unsigned index)
{
	struct mpls_route *rt = NULL;

	if (index < net->mpls.platform_labels) {
		struct mpls_route __rcu **platform_label =
			rcu_dereference(net->mpls.platform_label);
		rt = rcu_dereference(platform_label[index]);
	}
	return rt;
}

static bool mpls_output_possible(const struct net_device *dev)
{
	return dev && (dev->flags & IFF_UP) && netif_carrier_ok(dev);
}

static unsigned int mpls_rt_header_size(const struct mpls_route *rt)
{
	/* The size of the layer 2.5 labels to be added for this route */
	return rt->rt_labels * sizeof(struct mpls_shim_hdr);
}

static unsigned int mpls_dev_mtu(const struct net_device *dev)
{
	/* The amount of data the layer 2 frame can hold */
	return dev->mtu;
}

static bool mpls_pkt_too_big(const struct sk_buff *skb, unsigned int mtu)
{
	if (skb->len <= mtu)
		return false;

	if (skb_is_gso(skb) && skb_gso_network_seglen(skb) <= mtu)
		return false;

	return true;
}

static bool mpls_egress(struct mpls_route *rt, struct sk_buff *skb,
			struct mpls_entry_decoded dec)
{
	/* RFC4385 and RFC5586 encode other packets in mpls such that
	 * they don't conflict with the ip version number, making
	 * decoding by examining the ip version correct in everything
	 * except for the strangest cases.
	 *
	 * The strange cases if we choose to support them will require
	 * manual configuration.
	 */
	struct iphdr *hdr4 = ip_hdr(skb);
	bool success = true;

	if (hdr4->version == 4) {
		skb->protocol = htons(ETH_P_IP);
		csum_replace2(&hdr4->check,
			      htons(hdr4->ttl << 8),
			      htons(dec.ttl << 8));
		hdr4->ttl = dec.ttl;
	}
	else if (hdr4->version == 6) {
		struct ipv6hdr *hdr6 = ipv6_hdr(skb);
		skb->protocol = htons(ETH_P_IPV6);
		hdr6->hop_limit = dec.ttl;
	}
	else
		/* version 0 and version 1 are used by pseudo wires */
		success = false;
	return success;
}

static int mpls_forward(struct sk_buff *skb, struct net_device *dev,
			struct packet_type *pt, struct net_device *orig_dev)
{
	struct net *net = dev_net(dev);
	struct mpls_shim_hdr *hdr;
	struct mpls_route *rt;
	struct mpls_entry_decoded dec;
	struct net_device *out_dev;
	unsigned int hh_len;
	unsigned int new_header_size;
	unsigned int mtu;
	int err;

	/* Careful this entire function runs inside of an rcu critical section */

	if (skb->pkt_type != PACKET_HOST)
		goto drop;

	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)
		goto drop;

	if (!pskb_may_pull(skb, sizeof(*hdr)))
		goto drop;

	/* Read and decode the label */
	hdr = mpls_hdr(skb);
	dec = mpls_entry_decode(hdr);

	/* Pop the label */
	skb_pull(skb, sizeof(*hdr));
	skb_reset_network_header(skb);

	skb_orphan(skb);

	rt = mpls_route_input_rcu(net, dec.label);
	if (!rt)
		goto drop;

	/* Find the output device */
	out_dev = rt->rt_dev;
	if (!mpls_output_possible(out_dev))
		goto drop;

	if (skb_warn_if_lro(skb))
		goto drop;

	skb_forward_csum(skb);

	/* Verify ttl is valid */
	if (dec.ttl <= 2)
		goto drop;
	dec.ttl -= 1;

	/* Verify the destination can hold the packet */
	new_header_size = mpls_rt_header_size(rt);
	mtu = mpls_dev_mtu(out_dev);
	if (mpls_pkt_too_big(skb, mtu - new_header_size))
		goto drop;

	hh_len = LL_RESERVED_SPACE(out_dev);
	if (!out_dev->header_ops)
		hh_len = 0;

	/* Ensure there is enough space for the headers in the skb */
	if (skb_cow(skb, hh_len + new_header_size))
		goto drop;

	skb->dev = out_dev;
	skb->protocol = htons(ETH_P_MPLS_UC);

	if (unlikely(!new_header_size && dec.bos)) {
		/* Penultimate hop popping */
		if (!mpls_egress(rt, skb, dec))
			goto drop;
	} else {
		bool bos;
		int i;
		skb_push(skb, new_header_size);
		skb_reset_network_header(skb);
		/* Push the new labels */
		hdr = mpls_hdr(skb);
		bos = dec.bos;
		for (i = rt->rt_labels - 1; i >= 0; i--) {
			hdr[i] = mpls_entry_encode(rt->rt_label[i], dec.ttl, 0, bos);
			bos = false;
		}
	}

	err = neigh_xmit(rt->rt_via_family, out_dev, rt->rt_via, skb);
	if (err)
		net_dbg_ratelimited("%s: packet transmission failed: %d\n",
				    __func__, err);
	return 0;

drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static struct packet_type mpls_packet_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_MPLS_UC),
	.func = mpls_forward,
};

static struct mpls_route *mpls_rt_alloc(size_t alen)
{
	struct mpls_route *rt;

	rt = kzalloc(GFP_KERNEL, sizeof(*rt) + alen);
	if (rt)
		rt->rt_via_alen = alen;
	return rt;
}

static void mpls_rt_free(struct mpls_route *rt)
{
	if (rt)
		kfree_rcu(rt, rt_rcu);
}

static void mpls_route_update(struct net *net, unsigned index,
			      struct net_device *dev, struct mpls_route *new,
			      const struct nl_info *info)
{
	struct mpls_route *rt, *old = NULL;

	ASSERT_RTNL();

	rt = net->mpls.platform_label[index];
	if (!dev || (rt && (rt->rt_dev == dev))) {
		rcu_assign_pointer(net->mpls.platform_label[index], new);
		old = rt;
	}

	/* If we removed a route free it now */
	mpls_rt_free(old);
}

static void mpls_ifdown(struct net_device *dev)
{
	struct net *net = dev_net(dev);
	unsigned index;

	for (index = 0; index < net->mpls.platform_labels; index++) {
		struct mpls_route *rt = net->mpls.platform_label[index];
		if (!rt)
			continue;
		if (rt->rt_dev != dev)
			continue;
		rt->rt_dev = NULL;
	}
}

static int mpls_dev_notify(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch(event) {
	case NETDEV_UNREGISTER:
		mpls_ifdown(dev);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block mpls_dev_notifier = {
	.notifier_call = mpls_dev_notify,
};

static int mpls_net_init(struct net *net)
{
	net->mpls.platform_labels = 0;
	net->mpls.platform_label = NULL;

	return 0;
}

static void mpls_net_exit(struct net *net)
{
	unsigned int index;

	/* An rcu grace period haselapsed since there was a device in
	 * the network namespace (and thus the last in fqlight packet)
	 * left this network namespace.  This is because
	 * unregister_netdevice_many and netdev_run_todo has completed
	 * for each network device that was in this network namespace.
	 *
	 * As such no additional rcu synchronization is necessary when
	 * freeing the platform_label table.
	 */
	rtnl_lock();
	for (index = 0; index < net->mpls.platform_labels; index++) {
		struct mpls_route *rt = net->mpls.platform_label[index];
		rcu_assign_pointer(net->mpls.platform_label[index], NULL);
		mpls_rt_free(rt);
	}
	rtnl_unlock();

	kvfree(net->mpls.platform_label);
}

static struct pernet_operations mpls_net_ops = {
	.init = mpls_net_init,
	.exit = mpls_net_exit,
};

static int __init mpls_init(void)
{
	int err;

	BUILD_BUG_ON(sizeof(struct mpls_shim_hdr) != 4);

	err = register_pernet_subsys(&mpls_net_ops);
	if (err)
		goto out;

	err = register_netdevice_notifier(&mpls_dev_notifier);
	if (err)
		goto out_unregister_pernet;

	dev_add_pack(&mpls_packet_type);

	err = 0;
out:
	return err;

out_unregister_pernet:
	unregister_pernet_subsys(&mpls_net_ops);
	goto out;
}
module_init(mpls_init);

static void __exit mpls_exit(void)
{
	dev_remove_pack(&mpls_packet_type);
	unregister_netdevice_notifier(&mpls_dev_notifier);
	unregister_pernet_subsys(&mpls_net_ops);
}
module_exit(mpls_exit);

MODULE_DESCRIPTION("MultiProtocol Label Switching");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_NETPROTO(PF_MPLS);
