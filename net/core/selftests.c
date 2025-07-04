// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Synopsys, Inc. and/or its affiliates.
 * stmmac Selftests Support
 *
 * Author: Jose Abreu <joabreu@synopsys.com>
 *
 * Ported from stmmac by:
 * Copyright (C) 2021 Oleksij Rempel <o.rempel@pengutronix.de>
 */

#include <linux/phy.h>
#include <net/selftests.h>
#include <net/tcp.h>
#include <net/udp.h>

struct net_packet_attrs {
	const unsigned char *src;
	const unsigned char *dst;
	u32 ip_src;
	u32 ip_dst;
	bool tcp;
	u16 sport;
	u16 dport;
	int timeout;
	int size;
	int max_size;
	u8 id;
	u16 queue_mapping;
};

struct net_test_priv {
	struct net_packet_attrs *packet;
	struct packet_type pt;
	struct completion comp;
	int double_vlan;
	int vlan_id;
	int ok;
};

struct netsfhdr {
	__be32 version;
	__be64 magic;
	u8 id;
} __packed;

static u8 net_test_next_id;

#define NET_TEST_PKT_SIZE (sizeof(struct ethhdr) + sizeof(struct iphdr) + \
			   sizeof(struct netsfhdr))
#define NET_TEST_PKT_MAGIC	0xdeadcafecafedeadULL
#define NET_LB_TIMEOUT		msecs_to_jiffies(200)

static struct sk_buff *net_test_get_skb(struct net_device *ndev,
					struct net_packet_attrs *attr)
{
	struct sk_buff *skb = NULL;
	struct udphdr *uhdr = NULL;
	struct tcphdr *thdr = NULL;
	struct netsfhdr *shdr;
	struct ethhdr *ehdr;
	struct iphdr *ihdr;
	int iplen, size;

	size = attr->size + NET_TEST_PKT_SIZE;

	if (attr->tcp)
		size += sizeof(struct tcphdr);
	else
		size += sizeof(struct udphdr);

	if (attr->max_size && attr->max_size > size)
		size = attr->max_size;

	skb = netdev_alloc_skb(ndev, size);
	if (!skb)
		return NULL;

	prefetchw(skb->data);

	ehdr = skb_push(skb, ETH_HLEN);
	skb_reset_mac_header(skb);

	skb_set_network_header(skb, skb->len);
	ihdr = skb_put(skb, sizeof(*ihdr));

	skb_set_transport_header(skb, skb->len);
	if (attr->tcp)
		thdr = skb_put(skb, sizeof(*thdr));
	else
		uhdr = skb_put(skb, sizeof(*uhdr));

	eth_zero_addr(ehdr->h_dest);

	if (attr->src)
		ether_addr_copy(ehdr->h_source, attr->src);
	if (attr->dst)
		ether_addr_copy(ehdr->h_dest, attr->dst);

	ehdr->h_proto = htons(ETH_P_IP);

	if (attr->tcp) {
		memset(thdr, 0, sizeof(*thdr));
		thdr->source = htons(attr->sport);
		thdr->dest = htons(attr->dport);
		thdr->doff = sizeof(struct tcphdr) / 4;
	} else {
		uhdr->source = htons(attr->sport);
		uhdr->dest = htons(attr->dport);
		uhdr->len = htons(sizeof(*shdr) + sizeof(*uhdr) + attr->size);
		if (attr->max_size)
			uhdr->len = htons(attr->max_size -
					  (sizeof(*ihdr) + sizeof(*ehdr)));
		uhdr->check = 0;
	}

	ihdr->ihl = 5;
	ihdr->ttl = 32;
	ihdr->version = 4;
	if (attr->tcp)
		ihdr->protocol = IPPROTO_TCP;
	else
		ihdr->protocol = IPPROTO_UDP;
	iplen = sizeof(*ihdr) + sizeof(*shdr) + attr->size;
	if (attr->tcp)
		iplen += sizeof(*thdr);
	else
		iplen += sizeof(*uhdr);

	if (attr->max_size)
		iplen = attr->max_size - sizeof(*ehdr);

	ihdr->tot_len = htons(iplen);
	ihdr->frag_off = 0;
	ihdr->saddr = htonl(attr->ip_src);
	ihdr->daddr = htonl(attr->ip_dst);
	ihdr->tos = 0;
	ihdr->id = 0;
	ip_send_check(ihdr);

	shdr = skb_put(skb, sizeof(*shdr));
	shdr->version = 0;
	shdr->magic = cpu_to_be64(NET_TEST_PKT_MAGIC);
	attr->id = net_test_next_id;
	shdr->id = net_test_next_id++;

	if (attr->size) {
		void *payload = skb_put(skb, attr->size);

		memset(payload, 0, attr->size);
	}

	if (attr->max_size && attr->max_size > skb->len) {
		size_t pad_len = attr->max_size - skb->len;
		void *pad = skb_put(skb, pad_len);

		memset(pad, 0, pad_len);
	}

	skb->csum = 0;
	skb->ip_summed = CHECKSUM_PARTIAL;
	if (attr->tcp) {
		int l4len = skb->len - skb_transport_offset(skb);

		thdr->check = ~tcp_v4_check(l4len, ihdr->saddr, ihdr->daddr, 0);
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct tcphdr, check);
	} else {
		udp4_hwcsum(skb, ihdr->saddr, ihdr->daddr);
	}

	skb->protocol = htons(ETH_P_IP);
	skb->pkt_type = PACKET_HOST;
	skb->dev = ndev;

	return skb;
}

static int net_test_loopback_validate(struct sk_buff *skb,
				      struct net_device *ndev,
				      struct packet_type *pt,
				      struct net_device *orig_ndev)
{
	struct net_test_priv *tpriv = pt->af_packet_priv;
	const unsigned char *src = tpriv->packet->src;
	const unsigned char *dst = tpriv->packet->dst;
	struct netsfhdr *shdr;
	struct ethhdr *ehdr;
	struct udphdr *uhdr;
	struct tcphdr *thdr;
	struct iphdr *ihdr;

	skb = skb_unshare(skb, GFP_ATOMIC);
	if (!skb)
		goto out;

	if (skb_linearize(skb))
		goto out;
	if (skb_headlen(skb) < (NET_TEST_PKT_SIZE - ETH_HLEN))
		goto out;

	ehdr = (struct ethhdr *)skb_mac_header(skb);
	if (dst) {
		if (!ether_addr_equal_unaligned(ehdr->h_dest, dst))
			goto out;
	}

	if (src) {
		if (!ether_addr_equal_unaligned(ehdr->h_source, src))
			goto out;
	}

	ihdr = ip_hdr(skb);
	if (tpriv->double_vlan)
		ihdr = (struct iphdr *)(skb_network_header(skb) + 4);

	if (tpriv->packet->tcp) {
		if (ihdr->protocol != IPPROTO_TCP)
			goto out;

		thdr = (struct tcphdr *)((u8 *)ihdr + 4 * ihdr->ihl);
		if (thdr->dest != htons(tpriv->packet->dport))
			goto out;

		shdr = (struct netsfhdr *)((u8 *)thdr + sizeof(*thdr));
	} else {
		if (ihdr->protocol != IPPROTO_UDP)
			goto out;

		uhdr = (struct udphdr *)((u8 *)ihdr + 4 * ihdr->ihl);
		if (uhdr->dest != htons(tpriv->packet->dport))
			goto out;

		shdr = (struct netsfhdr *)((u8 *)uhdr + sizeof(*uhdr));
	}

	if (shdr->magic != cpu_to_be64(NET_TEST_PKT_MAGIC))
		goto out;
	if (tpriv->packet->id != shdr->id)
		goto out;

	tpriv->ok = true;
	complete(&tpriv->comp);
out:
	kfree_skb(skb);
	return 0;
}

static int __net_test_loopback(struct net_device *ndev,
			       struct net_packet_attrs *attr)
{
	struct net_test_priv *tpriv;
	struct sk_buff *skb = NULL;
	int ret = 0;

	tpriv = kzalloc(sizeof(*tpriv), GFP_KERNEL);
	if (!tpriv)
		return -ENOMEM;

	tpriv->ok = false;
	init_completion(&tpriv->comp);

	tpriv->pt.type = htons(ETH_P_IP);
	tpriv->pt.func = net_test_loopback_validate;
	tpriv->pt.dev = ndev;
	tpriv->pt.af_packet_priv = tpriv;
	tpriv->packet = attr;
	dev_add_pack(&tpriv->pt);

	skb = net_test_get_skb(ndev, attr);
	if (!skb) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = dev_direct_xmit(skb, attr->queue_mapping);
	if (ret < 0) {
		goto cleanup;
	} else if (ret > 0) {
		ret = -ENETUNREACH;
		goto cleanup;
	}

	if (!attr->timeout)
		attr->timeout = NET_LB_TIMEOUT;

	wait_for_completion_timeout(&tpriv->comp, attr->timeout);
	ret = tpriv->ok ? 0 : -ETIMEDOUT;

cleanup:
	dev_remove_pack(&tpriv->pt);
	kfree(tpriv);
	return ret;
}

static int net_test_netif_carrier(struct net_device *ndev)
{
	return netif_carrier_ok(ndev) ? 0 : -ENOLINK;
}

static int net_test_phy_phydev(struct net_device *ndev)
{
	return ndev->phydev ? 0 : -EOPNOTSUPP;
}

static int net_test_phy_loopback_enable(struct net_device *ndev)
{
	if (!ndev->phydev)
		return -EOPNOTSUPP;

	return phy_loopback(ndev->phydev, true, 0);
}

static int net_test_phy_loopback_disable(struct net_device *ndev)
{
	if (!ndev->phydev)
		return -EOPNOTSUPP;

	return phy_loopback(ndev->phydev, false, 0);
}

static int net_test_phy_loopback_udp(struct net_device *ndev)
{
	struct net_packet_attrs attr = { };

	attr.dst = ndev->dev_addr;
	return __net_test_loopback(ndev, &attr);
}

static int net_test_phy_loopback_udp_mtu(struct net_device *ndev)
{
	struct net_packet_attrs attr = { };

	attr.dst = ndev->dev_addr;
	attr.max_size = ndev->mtu;
	return __net_test_loopback(ndev, &attr);
}

static int net_test_phy_loopback_tcp(struct net_device *ndev)
{
	struct net_packet_attrs attr = { };

	attr.dst = ndev->dev_addr;
	attr.tcp = true;
	return __net_test_loopback(ndev, &attr);
}

static const struct net_test {
	char name[ETH_GSTRING_LEN];
	int (*fn)(struct net_device *ndev);
} net_selftests[] = {
	{
		.name = "Carrier                       ",
		.fn = net_test_netif_carrier,
	}, {
		.name = "PHY dev is present            ",
		.fn = net_test_phy_phydev,
	}, {
		/* This test should be done before all PHY loopback test */
		.name = "PHY internal loopback, enable ",
		.fn = net_test_phy_loopback_enable,
	}, {
		.name = "PHY internal loopback, UDP    ",
		.fn = net_test_phy_loopback_udp,
	}, {
		.name = "PHY internal loopback, MTU    ",
		.fn = net_test_phy_loopback_udp_mtu,
	}, {
		.name = "PHY internal loopback, TCP    ",
		.fn = net_test_phy_loopback_tcp,
	}, {
		/* This test should be done after all PHY loopback test */
		.name = "PHY internal loopback, disable",
		.fn = net_test_phy_loopback_disable,
	},
};

void net_selftest(struct net_device *ndev, struct ethtool_test *etest, u64 *buf)
{
	int count = net_selftest_get_count();
	int i;

	memset(buf, 0, sizeof(*buf) * count);
	net_test_next_id = 0;

	if (etest->flags != ETH_TEST_FL_OFFLINE) {
		netdev_err(ndev, "Only offline tests are supported\n");
		etest->flags |= ETH_TEST_FL_FAILED;
		return;
	}


	for (i = 0; i < count; i++) {
		buf[i] = net_selftests[i].fn(ndev);
		if (buf[i] && (buf[i] != -EOPNOTSUPP))
			etest->flags |= ETH_TEST_FL_FAILED;
	}
}
EXPORT_SYMBOL_GPL(net_selftest);

int net_selftest_get_count(void)
{
	return ARRAY_SIZE(net_selftests);
}
EXPORT_SYMBOL_GPL(net_selftest_get_count);

void net_selftest_get_strings(u8 *data)
{
	int i;

	for (i = 0; i < net_selftest_get_count(); i++)
		ethtool_sprintf(&data, "%2d. %s", i + 1,
				net_selftests[i].name);
}
EXPORT_SYMBOL_GPL(net_selftest_get_strings);

MODULE_DESCRIPTION("Common library for generic PHY ethtool selftests");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Oleksij Rempel <o.rempel@pengutronix.de>");
