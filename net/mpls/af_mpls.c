#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/sysctl.h>
#include <linux/net.h>
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/mpls.h>
#include <linux/vmalloc.h>
#include <net/ip.h>
#include <net/dst.h>
#include <net/sock.h>
#include <net/arp.h>
#include <net/ip_fib.h>
#include <net/netevent.h>
#include <net/netns/generic.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#include <net/addrconf.h>
#endif
#include "internal.h"

#define LABEL_NOT_SPECIFIED (1<<20)
#define MAX_NEW_LABELS 2

/* This maximum ha length copied from the definition of struct neighbour */
#define MAX_VIA_ALEN (ALIGN(MAX_ADDR_LEN, sizeof(unsigned long)))

enum mpls_payload_type {
	MPT_UNSPEC, /* IPv4 or IPv6 */
	MPT_IPV4 = 4,
	MPT_IPV6 = 6,

	/* Other types not implemented:
	 *  - Pseudo-wire with or without control word (RFC4385)
	 *  - GAL (RFC5586)
	 */
};

struct mpls_route { /* next hop label forwarding entry */
	struct net_device __rcu *rt_dev;
	struct rcu_head		rt_rcu;
	u32			rt_label[MAX_NEW_LABELS];
	u8			rt_protocol; /* routing protocol that set this entry */
	u8                      rt_payload_type;
	u8			rt_labels;
	u8			rt_via_alen;
	u8			rt_via_table;
	u8			rt_via[0];
};

static int zero = 0;
static int label_limit = (1 << 20) - 1;

static void rtmsg_lfib(int event, u32 label, struct mpls_route *rt,
		       struct nlmsghdr *nlh, struct net *net, u32 portid,
		       unsigned int nlm_flags);

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

static inline struct mpls_dev *mpls_dev_get(const struct net_device *dev)
{
	return rcu_dereference_rtnl(dev->mpls_ptr);
}

bool mpls_output_possible(const struct net_device *dev)
{
	return dev && (dev->flags & IFF_UP) && netif_carrier_ok(dev);
}
EXPORT_SYMBOL_GPL(mpls_output_possible);

static unsigned int mpls_rt_header_size(const struct mpls_route *rt)
{
	/* The size of the layer 2.5 labels to be added for this route */
	return rt->rt_labels * sizeof(struct mpls_shim_hdr);
}

unsigned int mpls_dev_mtu(const struct net_device *dev)
{
	/* The amount of data the layer 2 frame can hold */
	return dev->mtu;
}
EXPORT_SYMBOL_GPL(mpls_dev_mtu);

bool mpls_pkt_too_big(const struct sk_buff *skb, unsigned int mtu)
{
	if (skb->len <= mtu)
		return false;

	if (skb_is_gso(skb) && skb_gso_network_seglen(skb) <= mtu)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(mpls_pkt_too_big);

static bool mpls_egress(struct mpls_route *rt, struct sk_buff *skb,
			struct mpls_entry_decoded dec)
{
	enum mpls_payload_type payload_type;
	bool success = false;

	/* The IPv4 code below accesses through the IPv4 header
	 * checksum, which is 12 bytes into the packet.
	 * The IPv6 code below accesses through the IPv6 hop limit
	 * which is 8 bytes into the packet.
	 *
	 * For all supported cases there should always be at least 12
	 * bytes of packet data present.  The IPv4 header is 20 bytes
	 * without options and the IPv6 header is always 40 bytes
	 * long.
	 */
	if (!pskb_may_pull(skb, 12))
		return false;

	payload_type = rt->rt_payload_type;
	if (payload_type == MPT_UNSPEC)
		payload_type = ip_hdr(skb)->version;

	switch (payload_type) {
	case MPT_IPV4: {
		struct iphdr *hdr4 = ip_hdr(skb);
		skb->protocol = htons(ETH_P_IP);
		csum_replace2(&hdr4->check,
			      htons(hdr4->ttl << 8),
			      htons(dec.ttl << 8));
		hdr4->ttl = dec.ttl;
		success = true;
		break;
	}
	case MPT_IPV6: {
		struct ipv6hdr *hdr6 = ipv6_hdr(skb);
		skb->protocol = htons(ETH_P_IPV6);
		hdr6->hop_limit = dec.ttl;
		success = true;
		break;
	}
	case MPT_UNSPEC:
		break;
	}

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
	struct mpls_dev *mdev;
	unsigned int hh_len;
	unsigned int new_header_size;
	unsigned int mtu;
	int err;

	/* Careful this entire function runs inside of an rcu critical section */

	mdev = mpls_dev_get(dev);
	if (!mdev || !mdev->input_enabled)
		goto drop;

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
	out_dev = rcu_dereference(rt->rt_dev);
	if (!mpls_output_possible(out_dev))
		goto drop;

	if (skb_warn_if_lro(skb))
		goto drop;

	skb_forward_csum(skb);

	/* Verify ttl is valid */
	if (dec.ttl <= 1)
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

	err = neigh_xmit(rt->rt_via_table, out_dev, rt->rt_via, skb);
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

static const struct nla_policy rtm_mpls_policy[RTA_MAX+1] = {
	[RTA_DST]		= { .type = NLA_U32 },
	[RTA_OIF]		= { .type = NLA_U32 },
};

struct mpls_route_config {
	u32			rc_protocol;
	u32			rc_ifindex;
	u16			rc_via_table;
	u16			rc_via_alen;
	u8			rc_via[MAX_VIA_ALEN];
	u32			rc_label;
	u32			rc_output_labels;
	u32			rc_output_label[MAX_NEW_LABELS];
	u32			rc_nlflags;
	enum mpls_payload_type	rc_payload_type;
	struct nl_info		rc_nlinfo;
};

static struct mpls_route *mpls_rt_alloc(size_t alen)
{
	struct mpls_route *rt;

	rt = kzalloc(sizeof(*rt) + alen, GFP_KERNEL);
	if (rt)
		rt->rt_via_alen = alen;
	return rt;
}

static void mpls_rt_free(struct mpls_route *rt)
{
	if (rt)
		kfree_rcu(rt, rt_rcu);
}

static void mpls_notify_route(struct net *net, unsigned index,
			      struct mpls_route *old, struct mpls_route *new,
			      const struct nl_info *info)
{
	struct nlmsghdr *nlh = info ? info->nlh : NULL;
	unsigned portid = info ? info->portid : 0;
	int event = new ? RTM_NEWROUTE : RTM_DELROUTE;
	struct mpls_route *rt = new ? new : old;
	unsigned nlm_flags = (old && new) ? NLM_F_REPLACE : 0;
	/* Ignore reserved labels for now */
	if (rt && (index >= MPLS_LABEL_FIRST_UNRESERVED))
		rtmsg_lfib(event, index, rt, nlh, net, portid, nlm_flags);
}

static void mpls_route_update(struct net *net, unsigned index,
			      struct net_device *dev, struct mpls_route *new,
			      const struct nl_info *info)
{
	struct mpls_route __rcu **platform_label;
	struct mpls_route *rt, *old = NULL;

	ASSERT_RTNL();

	platform_label = rtnl_dereference(net->mpls.platform_label);
	rt = rtnl_dereference(platform_label[index]);
	if (!dev || (rt && (rtnl_dereference(rt->rt_dev) == dev))) {
		rcu_assign_pointer(platform_label[index], new);
		old = rt;
	}

	mpls_notify_route(net, index, old, new, info);

	/* If we removed a route free it now */
	mpls_rt_free(old);
}

static unsigned find_free_label(struct net *net)
{
	struct mpls_route __rcu **platform_label;
	size_t platform_labels;
	unsigned index;

	platform_label = rtnl_dereference(net->mpls.platform_label);
	platform_labels = net->mpls.platform_labels;
	for (index = MPLS_LABEL_FIRST_UNRESERVED; index < platform_labels;
	     index++) {
		if (!rtnl_dereference(platform_label[index]))
			return index;
	}
	return LABEL_NOT_SPECIFIED;
}

#if IS_ENABLED(CONFIG_INET)
static struct net_device *inet_fib_lookup_dev(struct net *net, void *addr)
{
	struct net_device *dev;
	struct rtable *rt;
	struct in_addr daddr;

	memcpy(&daddr, addr, sizeof(struct in_addr));
	rt = ip_route_output(net, daddr.s_addr, 0, 0, 0);
	if (IS_ERR(rt))
		return ERR_CAST(rt);

	dev = rt->dst.dev;
	dev_hold(dev);

	ip_rt_put(rt);

	return dev;
}
#else
static struct net_device *inet_fib_lookup_dev(struct net *net, void *addr)
{
	return ERR_PTR(-EAFNOSUPPORT);
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static struct net_device *inet6_fib_lookup_dev(struct net *net, void *addr)
{
	struct net_device *dev;
	struct dst_entry *dst;
	struct flowi6 fl6;
	int err;

	if (!ipv6_stub)
		return ERR_PTR(-EAFNOSUPPORT);

	memset(&fl6, 0, sizeof(fl6));
	memcpy(&fl6.daddr, addr, sizeof(struct in6_addr));
	err = ipv6_stub->ipv6_dst_lookup(net, NULL, &dst, &fl6);
	if (err)
		return ERR_PTR(err);

	dev = dst->dev;
	dev_hold(dev);
	dst_release(dst);

	return dev;
}
#else
static struct net_device *inet6_fib_lookup_dev(struct net *net, void *addr)
{
	return ERR_PTR(-EAFNOSUPPORT);
}
#endif

static struct net_device *find_outdev(struct net *net,
				      struct mpls_route_config *cfg)
{
	struct net_device *dev = NULL;

	if (!cfg->rc_ifindex) {
		switch (cfg->rc_via_table) {
		case NEIGH_ARP_TABLE:
			dev = inet_fib_lookup_dev(net, cfg->rc_via);
			break;
		case NEIGH_ND_TABLE:
			dev = inet6_fib_lookup_dev(net, cfg->rc_via);
			break;
		case NEIGH_LINK_TABLE:
			break;
		}
	} else {
		dev = dev_get_by_index(net, cfg->rc_ifindex);
	}

	if (!dev)
		return ERR_PTR(-ENODEV);

	return dev;
}

static int mpls_route_add(struct mpls_route_config *cfg)
{
	struct mpls_route __rcu **platform_label;
	struct net *net = cfg->rc_nlinfo.nl_net;
	struct net_device *dev = NULL;
	struct mpls_route *rt, *old;
	unsigned index;
	int i;
	int err = -EINVAL;

	index = cfg->rc_label;

	/* If a label was not specified during insert pick one */
	if ((index == LABEL_NOT_SPECIFIED) &&
	    (cfg->rc_nlflags & NLM_F_CREATE)) {
		index = find_free_label(net);
	}

	/* Reserved labels may not be set */
	if (index < MPLS_LABEL_FIRST_UNRESERVED)
		goto errout;

	/* The full 20 bit range may not be supported. */
	if (index >= net->mpls.platform_labels)
		goto errout;

	/* Ensure only a supported number of labels are present */
	if (cfg->rc_output_labels > MAX_NEW_LABELS)
		goto errout;

	dev = find_outdev(net, cfg);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		dev = NULL;
		goto errout;
	}

	/* Ensure this is a supported device */
	err = -EINVAL;
	if (!mpls_dev_get(dev))
		goto errout;

	err = -EINVAL;
	if ((cfg->rc_via_table == NEIGH_LINK_TABLE) &&
	    (dev->addr_len != cfg->rc_via_alen))
		goto errout;

	/* Append makes no sense with mpls */
	err = -EOPNOTSUPP;
	if (cfg->rc_nlflags & NLM_F_APPEND)
		goto errout;

	err = -EEXIST;
	platform_label = rtnl_dereference(net->mpls.platform_label);
	old = rtnl_dereference(platform_label[index]);
	if ((cfg->rc_nlflags & NLM_F_EXCL) && old)
		goto errout;

	err = -EEXIST;
	if (!(cfg->rc_nlflags & NLM_F_REPLACE) && old)
		goto errout;

	err = -ENOENT;
	if (!(cfg->rc_nlflags & NLM_F_CREATE) && !old)
		goto errout;

	err = -ENOMEM;
	rt = mpls_rt_alloc(cfg->rc_via_alen);
	if (!rt)
		goto errout;

	rt->rt_labels = cfg->rc_output_labels;
	for (i = 0; i < rt->rt_labels; i++)
		rt->rt_label[i] = cfg->rc_output_label[i];
	rt->rt_protocol = cfg->rc_protocol;
	RCU_INIT_POINTER(rt->rt_dev, dev);
	rt->rt_payload_type = cfg->rc_payload_type;
	rt->rt_via_table = cfg->rc_via_table;
	memcpy(rt->rt_via, cfg->rc_via, cfg->rc_via_alen);

	mpls_route_update(net, index, NULL, rt, &cfg->rc_nlinfo);

	dev_put(dev);
	return 0;

errout:
	if (dev)
		dev_put(dev);
	return err;
}

static int mpls_route_del(struct mpls_route_config *cfg)
{
	struct net *net = cfg->rc_nlinfo.nl_net;
	unsigned index;
	int err = -EINVAL;

	index = cfg->rc_label;

	/* Reserved labels may not be removed */
	if (index < MPLS_LABEL_FIRST_UNRESERVED)
		goto errout;

	/* The full 20 bit range may not be supported */
	if (index >= net->mpls.platform_labels)
		goto errout;

	mpls_route_update(net, index, NULL, NULL, &cfg->rc_nlinfo);

	err = 0;
errout:
	return err;
}

#define MPLS_PERDEV_SYSCTL_OFFSET(field)	\
	(&((struct mpls_dev *)0)->field)

static const struct ctl_table mpls_dev_table[] = {
	{
		.procname	= "input",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.data		= MPLS_PERDEV_SYSCTL_OFFSET(input_enabled),
	},
	{ }
};

static int mpls_dev_sysctl_register(struct net_device *dev,
				    struct mpls_dev *mdev)
{
	char path[sizeof("net/mpls/conf/") + IFNAMSIZ];
	struct ctl_table *table;
	int i;

	table = kmemdup(&mpls_dev_table, sizeof(mpls_dev_table), GFP_KERNEL);
	if (!table)
		goto out;

	/* Table data contains only offsets relative to the base of
	 * the mdev at this point, so make them absolute.
	 */
	for (i = 0; i < ARRAY_SIZE(mpls_dev_table); i++)
		table[i].data = (char *)mdev + (uintptr_t)table[i].data;

	snprintf(path, sizeof(path), "net/mpls/conf/%s", dev->name);

	mdev->sysctl = register_net_sysctl(dev_net(dev), path, table);
	if (!mdev->sysctl)
		goto free;

	return 0;

free:
	kfree(table);
out:
	return -ENOBUFS;
}

static void mpls_dev_sysctl_unregister(struct mpls_dev *mdev)
{
	struct ctl_table *table;

	table = mdev->sysctl->ctl_table_arg;
	unregister_net_sysctl_table(mdev->sysctl);
	kfree(table);
}

static struct mpls_dev *mpls_add_dev(struct net_device *dev)
{
	struct mpls_dev *mdev;
	int err = -ENOMEM;

	ASSERT_RTNL();

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return ERR_PTR(err);

	err = mpls_dev_sysctl_register(dev, mdev);
	if (err)
		goto free;

	rcu_assign_pointer(dev->mpls_ptr, mdev);

	return mdev;

free:
	kfree(mdev);
	return ERR_PTR(err);
}

static void mpls_ifdown(struct net_device *dev)
{
	struct mpls_route __rcu **platform_label;
	struct net *net = dev_net(dev);
	struct mpls_dev *mdev;
	unsigned index;

	platform_label = rtnl_dereference(net->mpls.platform_label);
	for (index = 0; index < net->mpls.platform_labels; index++) {
		struct mpls_route *rt = rtnl_dereference(platform_label[index]);
		if (!rt)
			continue;
		if (rtnl_dereference(rt->rt_dev) != dev)
			continue;
		rt->rt_dev = NULL;
	}

	mdev = mpls_dev_get(dev);
	if (!mdev)
		return;

	mpls_dev_sysctl_unregister(mdev);

	RCU_INIT_POINTER(dev->mpls_ptr, NULL);

	kfree_rcu(mdev, rcu);
}

static int mpls_dev_notify(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct mpls_dev *mdev;

	switch(event) {
	case NETDEV_REGISTER:
		/* For now just support ethernet devices */
		if ((dev->type == ARPHRD_ETHER) ||
		    (dev->type == ARPHRD_LOOPBACK)) {
			mdev = mpls_add_dev(dev);
			if (IS_ERR(mdev))
				return notifier_from_errno(PTR_ERR(mdev));
		}
		break;

	case NETDEV_UNREGISTER:
		mpls_ifdown(dev);
		break;
	case NETDEV_CHANGENAME:
		mdev = mpls_dev_get(dev);
		if (mdev) {
			int err;

			mpls_dev_sysctl_unregister(mdev);
			err = mpls_dev_sysctl_register(dev, mdev);
			if (err)
				return notifier_from_errno(err);
		}
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block mpls_dev_notifier = {
	.notifier_call = mpls_dev_notify,
};

static int nla_put_via(struct sk_buff *skb,
		       u8 table, const void *addr, int alen)
{
	static const int table_to_family[NEIGH_NR_TABLES + 1] = {
		AF_INET, AF_INET6, AF_DECnet, AF_PACKET,
	};
	struct nlattr *nla;
	struct rtvia *via;
	int family = AF_UNSPEC;

	nla = nla_reserve(skb, RTA_VIA, alen + 2);
	if (!nla)
		return -EMSGSIZE;

	if (table <= NEIGH_NR_TABLES)
		family = table_to_family[table];

	via = nla_data(nla);
	via->rtvia_family = family;
	memcpy(via->rtvia_addr, addr, alen);
	return 0;
}

int nla_put_labels(struct sk_buff *skb, int attrtype,
		   u8 labels, const u32 label[])
{
	struct nlattr *nla;
	struct mpls_shim_hdr *nla_label;
	bool bos;
	int i;
	nla = nla_reserve(skb, attrtype, labels*4);
	if (!nla)
		return -EMSGSIZE;

	nla_label = nla_data(nla);
	bos = true;
	for (i = labels - 1; i >= 0; i--) {
		nla_label[i] = mpls_entry_encode(label[i], 0, 0, bos);
		bos = false;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nla_put_labels);

int nla_get_labels(const struct nlattr *nla,
		   u32 max_labels, u32 *labels, u32 label[])
{
	unsigned len = nla_len(nla);
	unsigned nla_labels;
	struct mpls_shim_hdr *nla_label;
	bool bos;
	int i;

	/* len needs to be an even multiple of 4 (the label size) */
	if (len & 3)
		return -EINVAL;

	/* Limit the number of new labels allowed */
	nla_labels = len/4;
	if (nla_labels > max_labels)
		return -EINVAL;

	nla_label = nla_data(nla);
	bos = true;
	for (i = nla_labels - 1; i >= 0; i--, bos = false) {
		struct mpls_entry_decoded dec;
		dec = mpls_entry_decode(nla_label + i);

		/* Ensure the bottom of stack flag is properly set
		 * and ttl and tc are both clear.
		 */
		if ((dec.bos != bos) || dec.ttl || dec.tc)
			return -EINVAL;

		switch (dec.label) {
		case MPLS_LABEL_IMPLNULL:
			/* RFC3032: This is a label that an LSR may
			 * assign and distribute, but which never
			 * actually appears in the encapsulation.
			 */
			return -EINVAL;
		}

		label[i] = dec.label;
	}
	*labels = nla_labels;
	return 0;
}
EXPORT_SYMBOL_GPL(nla_get_labels);

static int rtm_to_route_config(struct sk_buff *skb,  struct nlmsghdr *nlh,
			       struct mpls_route_config *cfg)
{
	struct rtmsg *rtm;
	struct nlattr *tb[RTA_MAX+1];
	int index;
	int err;

	err = nlmsg_parse(nlh, sizeof(*rtm), tb, RTA_MAX, rtm_mpls_policy);
	if (err < 0)
		goto errout;

	err = -EINVAL;
	rtm = nlmsg_data(nlh);
	memset(cfg, 0, sizeof(*cfg));

	if (rtm->rtm_family != AF_MPLS)
		goto errout;
	if (rtm->rtm_dst_len != 20)
		goto errout;
	if (rtm->rtm_src_len != 0)
		goto errout;
	if (rtm->rtm_tos != 0)
		goto errout;
	if (rtm->rtm_table != RT_TABLE_MAIN)
		goto errout;
	/* Any value is acceptable for rtm_protocol */

	/* As mpls uses destination specific addresses
	 * (or source specific address in the case of multicast)
	 * all addresses have universal scope.
	 */
	if (rtm->rtm_scope != RT_SCOPE_UNIVERSE)
		goto errout;
	if (rtm->rtm_type != RTN_UNICAST)
		goto errout;
	if (rtm->rtm_flags != 0)
		goto errout;

	cfg->rc_label		= LABEL_NOT_SPECIFIED;
	cfg->rc_protocol	= rtm->rtm_protocol;
	cfg->rc_nlflags		= nlh->nlmsg_flags;
	cfg->rc_nlinfo.portid	= NETLINK_CB(skb).portid;
	cfg->rc_nlinfo.nlh	= nlh;
	cfg->rc_nlinfo.nl_net	= sock_net(skb->sk);

	for (index = 0; index <= RTA_MAX; index++) {
		struct nlattr *nla = tb[index];
		if (!nla)
			continue;

		switch(index) {
		case RTA_OIF:
			cfg->rc_ifindex = nla_get_u32(nla);
			break;
		case RTA_NEWDST:
			if (nla_get_labels(nla, MAX_NEW_LABELS,
					   &cfg->rc_output_labels,
					   cfg->rc_output_label))
				goto errout;
			break;
		case RTA_DST:
		{
			u32 label_count;
			if (nla_get_labels(nla, 1, &label_count,
					   &cfg->rc_label))
				goto errout;

			/* Reserved labels may not be set */
			if (cfg->rc_label < MPLS_LABEL_FIRST_UNRESERVED)
				goto errout;

			break;
		}
		case RTA_VIA:
		{
			struct rtvia *via = nla_data(nla);
			if (nla_len(nla) < offsetof(struct rtvia, rtvia_addr))
				goto errout;
			cfg->rc_via_alen   = nla_len(nla) -
				offsetof(struct rtvia, rtvia_addr);
			if (cfg->rc_via_alen > MAX_VIA_ALEN)
				goto errout;

			/* Validate the address family */
			switch(via->rtvia_family) {
			case AF_PACKET:
				cfg->rc_via_table = NEIGH_LINK_TABLE;
				break;
			case AF_INET:
				cfg->rc_via_table = NEIGH_ARP_TABLE;
				if (cfg->rc_via_alen != 4)
					goto errout;
				break;
			case AF_INET6:
				cfg->rc_via_table = NEIGH_ND_TABLE;
				if (cfg->rc_via_alen != 16)
					goto errout;
				break;
			default:
				/* Unsupported address family */
				goto errout;
			}

			memcpy(cfg->rc_via, via->rtvia_addr, cfg->rc_via_alen);
			break;
		}
		default:
			/* Unsupported attribute */
			goto errout;
		}
	}

	err = 0;
errout:
	return err;
}

static int mpls_rtm_delroute(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct mpls_route_config cfg;
	int err;

	err = rtm_to_route_config(skb, nlh, &cfg);
	if (err < 0)
		return err;

	return mpls_route_del(&cfg);
}


static int mpls_rtm_newroute(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct mpls_route_config cfg;
	int err;

	err = rtm_to_route_config(skb, nlh, &cfg);
	if (err < 0)
		return err;

	return mpls_route_add(&cfg);
}

static int mpls_dump_route(struct sk_buff *skb, u32 portid, u32 seq, int event,
			   u32 label, struct mpls_route *rt, int flags)
{
	struct net_device *dev;
	struct nlmsghdr *nlh;
	struct rtmsg *rtm;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*rtm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	rtm = nlmsg_data(nlh);
	rtm->rtm_family = AF_MPLS;
	rtm->rtm_dst_len = 20;
	rtm->rtm_src_len = 0;
	rtm->rtm_tos = 0;
	rtm->rtm_table = RT_TABLE_MAIN;
	rtm->rtm_protocol = rt->rt_protocol;
	rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	rtm->rtm_type = RTN_UNICAST;
	rtm->rtm_flags = 0;

	if (rt->rt_labels &&
	    nla_put_labels(skb, RTA_NEWDST, rt->rt_labels, rt->rt_label))
		goto nla_put_failure;
	if (nla_put_via(skb, rt->rt_via_table, rt->rt_via, rt->rt_via_alen))
		goto nla_put_failure;
	dev = rtnl_dereference(rt->rt_dev);
	if (dev && nla_put_u32(skb, RTA_OIF, dev->ifindex))
		goto nla_put_failure;
	if (nla_put_labels(skb, RTA_DST, 1, &label))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int mpls_dump_routes(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct mpls_route __rcu **platform_label;
	size_t platform_labels;
	unsigned int index;

	ASSERT_RTNL();

	index = cb->args[0];
	if (index < MPLS_LABEL_FIRST_UNRESERVED)
		index = MPLS_LABEL_FIRST_UNRESERVED;

	platform_label = rtnl_dereference(net->mpls.platform_label);
	platform_labels = net->mpls.platform_labels;
	for (; index < platform_labels; index++) {
		struct mpls_route *rt;
		rt = rtnl_dereference(platform_label[index]);
		if (!rt)
			continue;

		if (mpls_dump_route(skb, NETLINK_CB(cb->skb).portid,
				    cb->nlh->nlmsg_seq, RTM_NEWROUTE,
				    index, rt, NLM_F_MULTI) < 0)
			break;
	}
	cb->args[0] = index;

	return skb->len;
}

static inline size_t lfib_nlmsg_size(struct mpls_route *rt)
{
	size_t payload =
		NLMSG_ALIGN(sizeof(struct rtmsg))
		+ nla_total_size(2 + rt->rt_via_alen)	/* RTA_VIA */
		+ nla_total_size(4);			/* RTA_DST */
	if (rt->rt_labels)				/* RTA_NEWDST */
		payload += nla_total_size(rt->rt_labels * 4);
	if (rt->rt_dev)					/* RTA_OIF */
		payload += nla_total_size(4);
	return payload;
}

static void rtmsg_lfib(int event, u32 label, struct mpls_route *rt,
		       struct nlmsghdr *nlh, struct net *net, u32 portid,
		       unsigned int nlm_flags)
{
	struct sk_buff *skb;
	u32 seq = nlh ? nlh->nlmsg_seq : 0;
	int err = -ENOBUFS;

	skb = nlmsg_new(lfib_nlmsg_size(rt), GFP_KERNEL);
	if (skb == NULL)
		goto errout;

	err = mpls_dump_route(skb, portid, seq, event, label, rt, nlm_flags);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in lfib_nlmsg_size */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, portid, RTNLGRP_MPLS_ROUTE, nlh, GFP_KERNEL);

	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_MPLS_ROUTE, err);
}

static int resize_platform_label_table(struct net *net, size_t limit)
{
	size_t size = sizeof(struct mpls_route *) * limit;
	size_t old_limit;
	size_t cp_size;
	struct mpls_route __rcu **labels = NULL, **old;
	struct mpls_route *rt0 = NULL, *rt2 = NULL;
	unsigned index;

	if (size) {
		labels = kzalloc(size, GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY);
		if (!labels)
			labels = vzalloc(size);

		if (!labels)
			goto nolabels;
	}

	/* In case the predefined labels need to be populated */
	if (limit > MPLS_LABEL_IPV4NULL) {
		struct net_device *lo = net->loopback_dev;
		rt0 = mpls_rt_alloc(lo->addr_len);
		if (!rt0)
			goto nort0;
		RCU_INIT_POINTER(rt0->rt_dev, lo);
		rt0->rt_protocol = RTPROT_KERNEL;
		rt0->rt_payload_type = MPT_IPV4;
		rt0->rt_via_table = NEIGH_LINK_TABLE;
		memcpy(rt0->rt_via, lo->dev_addr, lo->addr_len);
	}
	if (limit > MPLS_LABEL_IPV6NULL) {
		struct net_device *lo = net->loopback_dev;
		rt2 = mpls_rt_alloc(lo->addr_len);
		if (!rt2)
			goto nort2;
		RCU_INIT_POINTER(rt2->rt_dev, lo);
		rt2->rt_protocol = RTPROT_KERNEL;
		rt2->rt_payload_type = MPT_IPV6;
		rt2->rt_via_table = NEIGH_LINK_TABLE;
		memcpy(rt2->rt_via, lo->dev_addr, lo->addr_len);
	}

	rtnl_lock();
	/* Remember the original table */
	old = rtnl_dereference(net->mpls.platform_label);
	old_limit = net->mpls.platform_labels;

	/* Free any labels beyond the new table */
	for (index = limit; index < old_limit; index++)
		mpls_route_update(net, index, NULL, NULL, NULL);

	/* Copy over the old labels */
	cp_size = size;
	if (old_limit < limit)
		cp_size = old_limit * sizeof(struct mpls_route *);

	memcpy(labels, old, cp_size);

	/* If needed set the predefined labels */
	if ((old_limit <= MPLS_LABEL_IPV6NULL) &&
	    (limit > MPLS_LABEL_IPV6NULL)) {
		RCU_INIT_POINTER(labels[MPLS_LABEL_IPV6NULL], rt2);
		rt2 = NULL;
	}

	if ((old_limit <= MPLS_LABEL_IPV4NULL) &&
	    (limit > MPLS_LABEL_IPV4NULL)) {
		RCU_INIT_POINTER(labels[MPLS_LABEL_IPV4NULL], rt0);
		rt0 = NULL;
	}

	/* Update the global pointers */
	net->mpls.platform_labels = limit;
	rcu_assign_pointer(net->mpls.platform_label, labels);

	rtnl_unlock();

	mpls_rt_free(rt2);
	mpls_rt_free(rt0);

	if (old) {
		synchronize_rcu();
		kvfree(old);
	}
	return 0;

nort2:
	mpls_rt_free(rt0);
nort0:
	kvfree(labels);
nolabels:
	return -ENOMEM;
}

static int mpls_platform_labels(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net = table->data;
	int platform_labels = net->mpls.platform_labels;
	int ret;
	struct ctl_table tmp = {
		.procname	= table->procname,
		.data		= &platform_labels,
		.maxlen		= sizeof(int),
		.mode		= table->mode,
		.extra1		= &zero,
		.extra2		= &label_limit,
	};

	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);

	if (write && ret == 0)
		ret = resize_platform_label_table(net, platform_labels);

	return ret;
}

static const struct ctl_table mpls_table[] = {
	{
		.procname	= "platform_labels",
		.data		= NULL,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= mpls_platform_labels,
	},
	{ }
};

static int mpls_net_init(struct net *net)
{
	struct ctl_table *table;

	net->mpls.platform_labels = 0;
	net->mpls.platform_label = NULL;

	table = kmemdup(mpls_table, sizeof(mpls_table), GFP_KERNEL);
	if (table == NULL)
		return -ENOMEM;

	table[0].data = net;
	net->mpls.ctl = register_net_sysctl(net, "net/mpls", table);
	if (net->mpls.ctl == NULL) {
		kfree(table);
		return -ENOMEM;
	}

	return 0;
}

static void mpls_net_exit(struct net *net)
{
	struct mpls_route __rcu **platform_label;
	size_t platform_labels;
	struct ctl_table *table;
	unsigned int index;

	table = net->mpls.ctl->ctl_table_arg;
	unregister_net_sysctl_table(net->mpls.ctl);
	kfree(table);

	/* An rcu grace period has passed since there was a device in
	 * the network namespace (and thus the last in flight packet)
	 * left this network namespace.  This is because
	 * unregister_netdevice_many and netdev_run_todo has completed
	 * for each network device that was in this network namespace.
	 *
	 * As such no additional rcu synchronization is necessary when
	 * freeing the platform_label table.
	 */
	rtnl_lock();
	platform_label = rtnl_dereference(net->mpls.platform_label);
	platform_labels = net->mpls.platform_labels;
	for (index = 0; index < platform_labels; index++) {
		struct mpls_route *rt = rtnl_dereference(platform_label[index]);
		RCU_INIT_POINTER(platform_label[index], NULL);
		mpls_rt_free(rt);
	}
	rtnl_unlock();

	kvfree(platform_label);
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

	rtnl_register(PF_MPLS, RTM_NEWROUTE, mpls_rtm_newroute, NULL, NULL);
	rtnl_register(PF_MPLS, RTM_DELROUTE, mpls_rtm_delroute, NULL, NULL);
	rtnl_register(PF_MPLS, RTM_GETROUTE, NULL, mpls_dump_routes, NULL);
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
	rtnl_unregister_all(PF_MPLS);
	dev_remove_pack(&mpls_packet_type);
	unregister_netdevice_notifier(&mpls_dev_notifier);
	unregister_pernet_subsys(&mpls_net_ops);
}
module_exit(mpls_exit);

MODULE_DESCRIPTION("MultiProtocol Label Switching");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_NETPROTO(PF_MPLS);
