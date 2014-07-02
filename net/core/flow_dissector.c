#include <linux/skbuff.h>
#include <linux/export.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <linux/igmp.h>
#include <linux/icmp.h>
#include <linux/sctp.h>
#include <linux/dccp.h>
#include <linux/if_tunnel.h>
#include <linux/if_pppox.h>
#include <linux/ppp_defs.h>
#include <net/flow_keys.h>

/* copy saddr & daddr, possibly using 64bit load/store
 * Equivalent to :	flow->src = iph->saddr;
 *			flow->dst = iph->daddr;
 */
static void iph_to_flow_copy_addrs(struct flow_keys *flow, const struct iphdr *iph)
{
	BUILD_BUG_ON(offsetof(typeof(*flow), dst) !=
		     offsetof(typeof(*flow), src) + sizeof(flow->src));
	memcpy(&flow->src, &iph->saddr, sizeof(flow->src) + sizeof(flow->dst));
}

/**
 * skb_flow_get_ports - extract the upper layer ports and return them
 * @skb: buffer to extract the ports from
 * @thoff: transport header offset
 * @ip_proto: protocol for which to get port offset
 *
 * The function will try to retrieve the ports at offset thoff + poff where poff
 * is the protocol port offset returned from proto_ports_offset
 */
__be32 skb_flow_get_ports(const struct sk_buff *skb, int thoff, u8 ip_proto)
{
	int poff = proto_ports_offset(ip_proto);

	if (poff >= 0) {
		__be32 *ports, _ports;

		ports = skb_header_pointer(skb, thoff + poff,
					   sizeof(_ports), &_ports);
		if (ports)
			return *ports;
	}

	return 0;
}
EXPORT_SYMBOL(skb_flow_get_ports);

bool skb_flow_dissect(const struct sk_buff *skb, struct flow_keys *flow)
{
	int nhoff = skb_network_offset(skb);
	u8 ip_proto;
	__be16 proto = skb->protocol;

	memset(flow, 0, sizeof(*flow));

again:
	switch (proto) {
	case htons(ETH_P_IP): {
		const struct iphdr *iph;
		struct iphdr _iph;
ip:
		iph = skb_header_pointer(skb, nhoff, sizeof(_iph), &_iph);
		if (!iph || iph->ihl < 5)
			return false;
		nhoff += iph->ihl * 4;

		ip_proto = iph->protocol;
		if (ip_is_fragment(iph))
			ip_proto = 0;

		iph_to_flow_copy_addrs(flow, iph);
		break;
	}
	case htons(ETH_P_IPV6): {
		const struct ipv6hdr *iph;
		struct ipv6hdr _iph;
ipv6:
		iph = skb_header_pointer(skb, nhoff, sizeof(_iph), &_iph);
		if (!iph)
			return false;

		ip_proto = iph->nexthdr;
		flow->src = (__force __be32)ipv6_addr_hash(&iph->saddr);
		flow->dst = (__force __be32)ipv6_addr_hash(&iph->daddr);
		nhoff += sizeof(struct ipv6hdr);
		break;
	}
	case htons(ETH_P_8021AD):
	case htons(ETH_P_8021Q): {
		const struct vlan_hdr *vlan;
		struct vlan_hdr _vlan;

		vlan = skb_header_pointer(skb, nhoff, sizeof(_vlan), &_vlan);
		if (!vlan)
			return false;

		proto = vlan->h_vlan_encapsulated_proto;
		nhoff += sizeof(*vlan);
		goto again;
	}
	case htons(ETH_P_PPP_SES): {
		struct {
			struct pppoe_hdr hdr;
			__be16 proto;
		} *hdr, _hdr;
		hdr = skb_header_pointer(skb, nhoff, sizeof(_hdr), &_hdr);
		if (!hdr)
			return false;
		proto = hdr->proto;
		nhoff += PPPOE_SES_HLEN;
		switch (proto) {
		case htons(PPP_IP):
			goto ip;
		case htons(PPP_IPV6):
			goto ipv6;
		default:
			return false;
		}
	}
	default:
		return false;
	}

	switch (ip_proto) {
	case IPPROTO_GRE: {
		struct gre_hdr {
			__be16 flags;
			__be16 proto;
		} *hdr, _hdr;

		hdr = skb_header_pointer(skb, nhoff, sizeof(_hdr), &_hdr);
		if (!hdr)
			return false;
		/*
		 * Only look inside GRE if version zero and no
		 * routing
		 */
		if (!(hdr->flags & (GRE_VERSION|GRE_ROUTING))) {
			proto = hdr->proto;
			nhoff += 4;
			if (hdr->flags & GRE_CSUM)
				nhoff += 4;
			if (hdr->flags & GRE_KEY)
				nhoff += 4;
			if (hdr->flags & GRE_SEQ)
				nhoff += 4;
			if (proto == htons(ETH_P_TEB)) {
				const struct ethhdr *eth;
				struct ethhdr _eth;

				eth = skb_header_pointer(skb, nhoff,
							 sizeof(_eth), &_eth);
				if (!eth)
					return false;
				proto = eth->h_proto;
				nhoff += sizeof(*eth);
			}
			goto again;
		}
		break;
	}
	case IPPROTO_IPIP:
		proto = htons(ETH_P_IP);
		goto ip;
	case IPPROTO_IPV6:
		proto = htons(ETH_P_IPV6);
		goto ipv6;
	default:
		break;
	}

	flow->n_proto = proto;
	flow->ip_proto = ip_proto;
	flow->ports = skb_flow_get_ports(skb, nhoff, ip_proto);
	flow->thoff = (u16) nhoff;

	return true;
}
EXPORT_SYMBOL(skb_flow_dissect);

static u32 hashrnd __read_mostly;
static __always_inline void __flow_hash_secret_init(void)
{
	net_get_random_once(&hashrnd, sizeof(hashrnd));
}

static __always_inline u32 __flow_hash_3words(u32 a, u32 b, u32 c)
{
	__flow_hash_secret_init();
	return jhash_3words(a, b, c, hashrnd);
}

static __always_inline u32 __flow_hash_1word(u32 a)
{
	__flow_hash_secret_init();
	return jhash_1word(a, hashrnd);
}

static inline u32 __flow_hash_from_keys(struct flow_keys *keys)
{
	u32 hash;

	/* get a consistent hash (same value on both flow directions) */
	if (((__force u32)keys->dst < (__force u32)keys->src) ||
	    (((__force u32)keys->dst == (__force u32)keys->src) &&
	     ((__force u16)keys->port16[1] < (__force u16)keys->port16[0]))) {
		swap(keys->dst, keys->src);
		swap(keys->port16[0], keys->port16[1]);
	}

	hash = __flow_hash_3words((__force u32)keys->dst,
				  (__force u32)keys->src,
				  (__force u32)keys->ports);
	if (!hash)
		hash = 1;

	return hash;
}

u32 flow_hash_from_keys(struct flow_keys *keys)
{
	return __flow_hash_from_keys(keys);
}
EXPORT_SYMBOL(flow_hash_from_keys);

/*
 * __skb_get_hash: calculate a flow hash based on src/dst addresses
 * and src/dst port numbers.  Sets hash in skb to non-zero hash value
 * on success, zero indicates no valid hash.  Also, sets l4_hash in skb
 * if hash is a canonical 4-tuple hash over transport ports.
 */
void __skb_get_hash(struct sk_buff *skb)
{
	struct flow_keys keys;

	if (!skb_flow_dissect(skb, &keys))
		return;

	if (keys.ports)
		skb->l4_hash = 1;

	skb->hash = __flow_hash_from_keys(&keys);
}
EXPORT_SYMBOL(__skb_get_hash);

/*
 * Returns a Tx hash based on the given packet descriptor a Tx queues' number
 * to be used as a distribution range.
 */
u16 __skb_tx_hash(const struct net_device *dev, const struct sk_buff *skb,
		  unsigned int num_tx_queues)
{
	u32 hash;
	u16 qoffset = 0;
	u16 qcount = num_tx_queues;

	if (skb_rx_queue_recorded(skb)) {
		hash = skb_get_rx_queue(skb);
		while (unlikely(hash >= num_tx_queues))
			hash -= num_tx_queues;
		return hash;
	}

	if (dev->num_tc) {
		u8 tc = netdev_get_prio_tc_map(dev, skb->priority);
		qoffset = dev->tc_to_txq[tc].offset;
		qcount = dev->tc_to_txq[tc].count;
	}

	if (skb->sk && skb->sk->sk_hash)
		hash = skb->sk->sk_hash;
	else
		hash = (__force u16) skb->protocol;
	hash = __flow_hash_1word(hash);

	return (u16) (((u64) hash * qcount) >> 32) + qoffset;
}
EXPORT_SYMBOL(__skb_tx_hash);

/* __skb_get_poff() returns the offset to the payload as far as it could
 * be dissected. The main user is currently BPF, so that we can dynamically
 * truncate packets without needing to push actual payload to the user
 * space and can analyze headers only, instead.
 */
u32 __skb_get_poff(const struct sk_buff *skb)
{
	struct flow_keys keys;
	u32 poff = 0;

	if (!skb_flow_dissect(skb, &keys))
		return 0;

	poff += keys.thoff;
	switch (keys.ip_proto) {
	case IPPROTO_TCP: {
		const struct tcphdr *tcph;
		struct tcphdr _tcph;

		tcph = skb_header_pointer(skb, poff, sizeof(_tcph), &_tcph);
		if (!tcph)
			return poff;

		poff += max_t(u32, sizeof(struct tcphdr), tcph->doff * 4);
		break;
	}
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
		poff += sizeof(struct udphdr);
		break;
	/* For the rest, we do not really care about header
	 * extensions at this point for now.
	 */
	case IPPROTO_ICMP:
		poff += sizeof(struct icmphdr);
		break;
	case IPPROTO_ICMPV6:
		poff += sizeof(struct icmp6hdr);
		break;
	case IPPROTO_IGMP:
		poff += sizeof(struct igmphdr);
		break;
	case IPPROTO_DCCP:
		poff += sizeof(struct dccp_hdr);
		break;
	case IPPROTO_SCTP:
		poff += sizeof(struct sctphdr);
		break;
	}

	return poff;
}

static inline int get_xps_queue(struct net_device *dev, struct sk_buff *skb)
{
#ifdef CONFIG_XPS
	struct xps_dev_maps *dev_maps;
	struct xps_map *map;
	int queue_index = -1;

	rcu_read_lock();
	dev_maps = rcu_dereference(dev->xps_maps);
	if (dev_maps) {
		map = rcu_dereference(
		    dev_maps->cpu_map[raw_smp_processor_id()]);
		if (map) {
			if (map->len == 1)
				queue_index = map->queues[0];
			else {
				u32 hash;
				if (skb->sk && skb->sk->sk_hash)
					hash = skb->sk->sk_hash;
				else
					hash = (__force u16) skb->protocol ^
					    skb->hash;
				hash = __flow_hash_1word(hash);
				queue_index = map->queues[
				    ((u64)hash * map->len) >> 32];
			}
			if (unlikely(queue_index >= dev->real_num_tx_queues))
				queue_index = -1;
		}
	}
	rcu_read_unlock();

	return queue_index;
#else
	return -1;
#endif
}

static u16 __netdev_pick_tx(struct net_device *dev, struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	int queue_index = sk_tx_queue_get(sk);

	if (queue_index < 0 || skb->ooo_okay ||
	    queue_index >= dev->real_num_tx_queues) {
		int new_index = get_xps_queue(dev, skb);
		if (new_index < 0)
			new_index = skb_tx_hash(dev, skb);

		if (queue_index != new_index && sk &&
		    rcu_access_pointer(sk->sk_dst_cache))
			sk_tx_queue_set(sk, new_index);

		queue_index = new_index;
	}

	return queue_index;
}

struct netdev_queue *netdev_pick_tx(struct net_device *dev,
				    struct sk_buff *skb,
				    void *accel_priv)
{
	int queue_index = 0;

	if (dev->real_num_tx_queues != 1) {
		const struct net_device_ops *ops = dev->netdev_ops;
		if (ops->ndo_select_queue)
			queue_index = ops->ndo_select_queue(dev, skb, accel_priv,
							    __netdev_pick_tx);
		else
			queue_index = __netdev_pick_tx(dev, skb);

		if (!accel_priv)
			queue_index = netdev_cap_txqueue(dev, queue_index);
	}

	skb_set_queue_mapping(skb, queue_index);
	return netdev_get_tx_queue(dev, queue_index);
}
