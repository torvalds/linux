/*
 * Copyright (c) 2007-2014 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include <linux/uaccess.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <net/llc_pdu.h>
#include <linux/kernel.h>
#include <linux/jhash.h>
#include <linux/jiffies.h>
#include <linux/llc.h>
#include <linux/module.h>
#include <linux/in.h>
#include <linux/rcupdate.h>
#include <linux/cpumask.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/mpls.h>
#include <linux/sctp.h>
#include <linux/smp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/rculist.h>
#include <net/ip.h>
#include <net/ip_tunnels.h>
#include <net/ipv6.h>
#include <net/mpls.h>
#include <net/ndisc.h>
#include <net/nsh.h>

#include "conntrack.h"
#include "datapath.h"
#include "flow.h"
#include "flow_netlink.h"
#include "vport.h"

u64 ovs_flow_used_time(unsigned long flow_jiffies)
{
	struct timespec64 cur_ts;
	u64 cur_ms, idle_ms;

	ktime_get_ts64(&cur_ts);
	idle_ms = jiffies_to_msecs(jiffies - flow_jiffies);
	cur_ms = (u64)(u32)cur_ts.tv_sec * MSEC_PER_SEC +
		 cur_ts.tv_nsec / NSEC_PER_MSEC;

	return cur_ms - idle_ms;
}

#define TCP_FLAGS_BE16(tp) (*(__be16 *)&tcp_flag_word(tp) & htons(0x0FFF))

void ovs_flow_stats_update(struct sw_flow *flow, __be16 tcp_flags,
			   const struct sk_buff *skb)
{
	struct flow_stats *stats;
	unsigned int cpu = smp_processor_id();
	int len = skb->len + (skb_vlan_tag_present(skb) ? VLAN_HLEN : 0);

	stats = rcu_dereference(flow->stats[cpu]);

	/* Check if already have CPU-specific stats. */
	if (likely(stats)) {
		spin_lock(&stats->lock);
		/* Mark if we write on the pre-allocated stats. */
		if (cpu == 0 && unlikely(flow->stats_last_writer != cpu))
			flow->stats_last_writer = cpu;
	} else {
		stats = rcu_dereference(flow->stats[0]); /* Pre-allocated. */
		spin_lock(&stats->lock);

		/* If the current CPU is the only writer on the
		 * pre-allocated stats keep using them.
		 */
		if (unlikely(flow->stats_last_writer != cpu)) {
			/* A previous locker may have already allocated the
			 * stats, so we need to check again.  If CPU-specific
			 * stats were already allocated, we update the pre-
			 * allocated stats as we have already locked them.
			 */
			if (likely(flow->stats_last_writer != -1) &&
			    likely(!rcu_access_pointer(flow->stats[cpu]))) {
				/* Try to allocate CPU-specific stats. */
				struct flow_stats *new_stats;

				new_stats =
					kmem_cache_alloc_node(flow_stats_cache,
							      GFP_NOWAIT |
							      __GFP_THISNODE |
							      __GFP_NOWARN |
							      __GFP_NOMEMALLOC,
							      numa_node_id());
				if (likely(new_stats)) {
					new_stats->used = jiffies;
					new_stats->packet_count = 1;
					new_stats->byte_count = len;
					new_stats->tcp_flags = tcp_flags;
					spin_lock_init(&new_stats->lock);

					rcu_assign_pointer(flow->stats[cpu],
							   new_stats);
					cpumask_set_cpu(cpu, &flow->cpu_used_mask);
					goto unlock;
				}
			}
			flow->stats_last_writer = cpu;
		}
	}

	stats->used = jiffies;
	stats->packet_count++;
	stats->byte_count += len;
	stats->tcp_flags |= tcp_flags;
unlock:
	spin_unlock(&stats->lock);
}

/* Must be called with rcu_read_lock or ovs_mutex. */
void ovs_flow_stats_get(const struct sw_flow *flow,
			struct ovs_flow_stats *ovs_stats,
			unsigned long *used, __be16 *tcp_flags)
{
	int cpu;

	*used = 0;
	*tcp_flags = 0;
	memset(ovs_stats, 0, sizeof(*ovs_stats));

	/* We open code this to make sure cpu 0 is always considered */
	for (cpu = 0; cpu < nr_cpu_ids; cpu = cpumask_next(cpu, &flow->cpu_used_mask)) {
		struct flow_stats *stats = rcu_dereference_ovsl(flow->stats[cpu]);

		if (stats) {
			/* Local CPU may write on non-local stats, so we must
			 * block bottom-halves here.
			 */
			spin_lock_bh(&stats->lock);
			if (!*used || time_after(stats->used, *used))
				*used = stats->used;
			*tcp_flags |= stats->tcp_flags;
			ovs_stats->n_packets += stats->packet_count;
			ovs_stats->n_bytes += stats->byte_count;
			spin_unlock_bh(&stats->lock);
		}
	}
}

/* Called with ovs_mutex. */
void ovs_flow_stats_clear(struct sw_flow *flow)
{
	int cpu;

	/* We open code this to make sure cpu 0 is always considered */
	for (cpu = 0; cpu < nr_cpu_ids; cpu = cpumask_next(cpu, &flow->cpu_used_mask)) {
		struct flow_stats *stats = ovsl_dereference(flow->stats[cpu]);

		if (stats) {
			spin_lock_bh(&stats->lock);
			stats->used = 0;
			stats->packet_count = 0;
			stats->byte_count = 0;
			stats->tcp_flags = 0;
			spin_unlock_bh(&stats->lock);
		}
	}
}

static int check_header(struct sk_buff *skb, int len)
{
	if (unlikely(skb->len < len))
		return -EINVAL;
	if (unlikely(!pskb_may_pull(skb, len)))
		return -ENOMEM;
	return 0;
}

static bool arphdr_ok(struct sk_buff *skb)
{
	return pskb_may_pull(skb, skb_network_offset(skb) +
				  sizeof(struct arp_eth_header));
}

static int check_iphdr(struct sk_buff *skb)
{
	unsigned int nh_ofs = skb_network_offset(skb);
	unsigned int ip_len;
	int err;

	err = check_header(skb, nh_ofs + sizeof(struct iphdr));
	if (unlikely(err))
		return err;

	ip_len = ip_hdrlen(skb);
	if (unlikely(ip_len < sizeof(struct iphdr) ||
		     skb->len < nh_ofs + ip_len))
		return -EINVAL;

	skb_set_transport_header(skb, nh_ofs + ip_len);
	return 0;
}

static bool tcphdr_ok(struct sk_buff *skb)
{
	int th_ofs = skb_transport_offset(skb);
	int tcp_len;

	if (unlikely(!pskb_may_pull(skb, th_ofs + sizeof(struct tcphdr))))
		return false;

	tcp_len = tcp_hdrlen(skb);
	if (unlikely(tcp_len < sizeof(struct tcphdr) ||
		     skb->len < th_ofs + tcp_len))
		return false;

	return true;
}

static bool udphdr_ok(struct sk_buff *skb)
{
	return pskb_may_pull(skb, skb_transport_offset(skb) +
				  sizeof(struct udphdr));
}

static bool sctphdr_ok(struct sk_buff *skb)
{
	return pskb_may_pull(skb, skb_transport_offset(skb) +
				  sizeof(struct sctphdr));
}

static bool icmphdr_ok(struct sk_buff *skb)
{
	return pskb_may_pull(skb, skb_transport_offset(skb) +
				  sizeof(struct icmphdr));
}

static int parse_ipv6hdr(struct sk_buff *skb, struct sw_flow_key *key)
{
	unsigned int nh_ofs = skb_network_offset(skb);
	unsigned int nh_len;
	int payload_ofs;
	struct ipv6hdr *nh;
	uint8_t nexthdr;
	__be16 frag_off;
	int err;

	err = check_header(skb, nh_ofs + sizeof(*nh));
	if (unlikely(err))
		return err;

	nh = ipv6_hdr(skb);
	nexthdr = nh->nexthdr;
	payload_ofs = (u8 *)(nh + 1) - skb->data;

	key->ip.proto = NEXTHDR_NONE;
	key->ip.tos = ipv6_get_dsfield(nh);
	key->ip.ttl = nh->hop_limit;
	key->ipv6.label = *(__be32 *)nh & htonl(IPV6_FLOWINFO_FLOWLABEL);
	key->ipv6.addr.src = nh->saddr;
	key->ipv6.addr.dst = nh->daddr;

	payload_ofs = ipv6_skip_exthdr(skb, payload_ofs, &nexthdr, &frag_off);

	if (frag_off) {
		if (frag_off & htons(~0x7))
			key->ip.frag = OVS_FRAG_TYPE_LATER;
		else
			key->ip.frag = OVS_FRAG_TYPE_FIRST;
	} else {
		key->ip.frag = OVS_FRAG_TYPE_NONE;
	}

	/* Delayed handling of error in ipv6_skip_exthdr() as it
	 * always sets frag_off to a valid value which may be
	 * used to set key->ip.frag above.
	 */
	if (unlikely(payload_ofs < 0))
		return -EPROTO;

	nh_len = payload_ofs - nh_ofs;
	skb_set_transport_header(skb, nh_ofs + nh_len);
	key->ip.proto = nexthdr;
	return nh_len;
}

static bool icmp6hdr_ok(struct sk_buff *skb)
{
	return pskb_may_pull(skb, skb_transport_offset(skb) +
				  sizeof(struct icmp6hdr));
}

/**
 * Parse vlan tag from vlan header.
 * Returns ERROR on memory error.
 * Returns 0 if it encounters a non-vlan or incomplete packet.
 * Returns 1 after successfully parsing vlan tag.
 */
static int parse_vlan_tag(struct sk_buff *skb, struct vlan_head *key_vh,
			  bool untag_vlan)
{
	struct vlan_head *vh = (struct vlan_head *)skb->data;

	if (likely(!eth_type_vlan(vh->tpid)))
		return 0;

	if (unlikely(skb->len < sizeof(struct vlan_head) + sizeof(__be16)))
		return 0;

	if (unlikely(!pskb_may_pull(skb, sizeof(struct vlan_head) +
				 sizeof(__be16))))
		return -ENOMEM;

	vh = (struct vlan_head *)skb->data;
	key_vh->tci = vh->tci | htons(VLAN_TAG_PRESENT);
	key_vh->tpid = vh->tpid;

	if (unlikely(untag_vlan)) {
		int offset = skb->data - skb_mac_header(skb);
		u16 tci;
		int err;

		__skb_push(skb, offset);
		err = __skb_vlan_pop(skb, &tci);
		__skb_pull(skb, offset);
		if (err)
			return err;
		__vlan_hwaccel_put_tag(skb, key_vh->tpid, tci);
	} else {
		__skb_pull(skb, sizeof(struct vlan_head));
	}
	return 1;
}

static void clear_vlan(struct sw_flow_key *key)
{
	key->eth.vlan.tci = 0;
	key->eth.vlan.tpid = 0;
	key->eth.cvlan.tci = 0;
	key->eth.cvlan.tpid = 0;
}

static int parse_vlan(struct sk_buff *skb, struct sw_flow_key *key)
{
	int res;

	if (skb_vlan_tag_present(skb)) {
		key->eth.vlan.tci = htons(skb->vlan_tci);
		key->eth.vlan.tpid = skb->vlan_proto;
	} else {
		/* Parse outer vlan tag in the non-accelerated case. */
		res = parse_vlan_tag(skb, &key->eth.vlan, true);
		if (res <= 0)
			return res;
	}

	/* Parse inner vlan tag. */
	res = parse_vlan_tag(skb, &key->eth.cvlan, false);
	if (res <= 0)
		return res;

	return 0;
}

static __be16 parse_ethertype(struct sk_buff *skb)
{
	struct llc_snap_hdr {
		u8  dsap;  /* Always 0xAA */
		u8  ssap;  /* Always 0xAA */
		u8  ctrl;
		u8  oui[3];
		__be16 ethertype;
	};
	struct llc_snap_hdr *llc;
	__be16 proto;

	proto = *(__be16 *) skb->data;
	__skb_pull(skb, sizeof(__be16));

	if (eth_proto_is_802_3(proto))
		return proto;

	if (skb->len < sizeof(struct llc_snap_hdr))
		return htons(ETH_P_802_2);

	if (unlikely(!pskb_may_pull(skb, sizeof(struct llc_snap_hdr))))
		return htons(0);

	llc = (struct llc_snap_hdr *) skb->data;
	if (llc->dsap != LLC_SAP_SNAP ||
	    llc->ssap != LLC_SAP_SNAP ||
	    (llc->oui[0] | llc->oui[1] | llc->oui[2]) != 0)
		return htons(ETH_P_802_2);

	__skb_pull(skb, sizeof(struct llc_snap_hdr));

	if (eth_proto_is_802_3(llc->ethertype))
		return llc->ethertype;

	return htons(ETH_P_802_2);
}

static int parse_icmpv6(struct sk_buff *skb, struct sw_flow_key *key,
			int nh_len)
{
	struct icmp6hdr *icmp = icmp6_hdr(skb);

	/* The ICMPv6 type and code fields use the 16-bit transport port
	 * fields, so we need to store them in 16-bit network byte order.
	 */
	key->tp.src = htons(icmp->icmp6_type);
	key->tp.dst = htons(icmp->icmp6_code);
	memset(&key->ipv6.nd, 0, sizeof(key->ipv6.nd));

	if (icmp->icmp6_code == 0 &&
	    (icmp->icmp6_type == NDISC_NEIGHBOUR_SOLICITATION ||
	     icmp->icmp6_type == NDISC_NEIGHBOUR_ADVERTISEMENT)) {
		int icmp_len = skb->len - skb_transport_offset(skb);
		struct nd_msg *nd;
		int offset;

		/* In order to process neighbor discovery options, we need the
		 * entire packet.
		 */
		if (unlikely(icmp_len < sizeof(*nd)))
			return 0;

		if (unlikely(skb_linearize(skb)))
			return -ENOMEM;

		nd = (struct nd_msg *)skb_transport_header(skb);
		key->ipv6.nd.target = nd->target;

		icmp_len -= sizeof(*nd);
		offset = 0;
		while (icmp_len >= 8) {
			struct nd_opt_hdr *nd_opt =
				 (struct nd_opt_hdr *)(nd->opt + offset);
			int opt_len = nd_opt->nd_opt_len * 8;

			if (unlikely(!opt_len || opt_len > icmp_len))
				return 0;

			/* Store the link layer address if the appropriate
			 * option is provided.  It is considered an error if
			 * the same link layer option is specified twice.
			 */
			if (nd_opt->nd_opt_type == ND_OPT_SOURCE_LL_ADDR
			    && opt_len == 8) {
				if (unlikely(!is_zero_ether_addr(key->ipv6.nd.sll)))
					goto invalid;
				ether_addr_copy(key->ipv6.nd.sll,
						&nd->opt[offset+sizeof(*nd_opt)]);
			} else if (nd_opt->nd_opt_type == ND_OPT_TARGET_LL_ADDR
				   && opt_len == 8) {
				if (unlikely(!is_zero_ether_addr(key->ipv6.nd.tll)))
					goto invalid;
				ether_addr_copy(key->ipv6.nd.tll,
						&nd->opt[offset+sizeof(*nd_opt)]);
			}

			icmp_len -= opt_len;
			offset += opt_len;
		}
	}

	return 0;

invalid:
	memset(&key->ipv6.nd.target, 0, sizeof(key->ipv6.nd.target));
	memset(key->ipv6.nd.sll, 0, sizeof(key->ipv6.nd.sll));
	memset(key->ipv6.nd.tll, 0, sizeof(key->ipv6.nd.tll));

	return 0;
}

static int parse_nsh(struct sk_buff *skb, struct sw_flow_key *key)
{
	struct nshhdr *nh;
	unsigned int nh_ofs = skb_network_offset(skb);
	u8 version, length;
	int err;

	err = check_header(skb, nh_ofs + NSH_BASE_HDR_LEN);
	if (unlikely(err))
		return err;

	nh = nsh_hdr(skb);
	version = nsh_get_ver(nh);
	length = nsh_hdr_len(nh);

	if (version != 0)
		return -EINVAL;

	err = check_header(skb, nh_ofs + length);
	if (unlikely(err))
		return err;

	nh = nsh_hdr(skb);
	key->nsh.base.flags = nsh_get_flags(nh);
	key->nsh.base.ttl = nsh_get_ttl(nh);
	key->nsh.base.mdtype = nh->mdtype;
	key->nsh.base.np = nh->np;
	key->nsh.base.path_hdr = nh->path_hdr;
	switch (key->nsh.base.mdtype) {
	case NSH_M_TYPE1:
		if (length != NSH_M_TYPE1_LEN)
			return -EINVAL;
		memcpy(key->nsh.context, nh->md1.context,
		       sizeof(nh->md1));
		break;
	case NSH_M_TYPE2:
		memset(key->nsh.context, 0,
		       sizeof(nh->md1));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * key_extract - extracts a flow key from an Ethernet frame.
 * @skb: sk_buff that contains the frame, with skb->data pointing to the
 * Ethernet header
 * @key: output flow key
 *
 * The caller must ensure that skb->len >= ETH_HLEN.
 *
 * Returns 0 if successful, otherwise a negative errno value.
 *
 * Initializes @skb header fields as follows:
 *
 *    - skb->mac_header: the L2 header.
 *
 *    - skb->network_header: just past the L2 header, or just past the
 *      VLAN header, to the first byte of the L2 payload.
 *
 *    - skb->transport_header: If key->eth.type is ETH_P_IP or ETH_P_IPV6
 *      on output, then just past the IP header, if one is present and
 *      of a correct length, otherwise the same as skb->network_header.
 *      For other key->eth.type values it is left untouched.
 *
 *    - skb->protocol: the type of the data starting at skb->network_header.
 *      Equals to key->eth.type.
 */
static int key_extract(struct sk_buff *skb, struct sw_flow_key *key)
{
	int error;
	struct ethhdr *eth;

	/* Flags are always used as part of stats */
	key->tp.flags = 0;

	skb_reset_mac_header(skb);

	/* Link layer. */
	clear_vlan(key);
	if (ovs_key_mac_proto(key) == MAC_PROTO_NONE) {
		if (unlikely(eth_type_vlan(skb->protocol)))
			return -EINVAL;

		skb_reset_network_header(skb);
	} else {
		eth = eth_hdr(skb);
		ether_addr_copy(key->eth.src, eth->h_source);
		ether_addr_copy(key->eth.dst, eth->h_dest);

		__skb_pull(skb, 2 * ETH_ALEN);
		/* We are going to push all headers that we pull, so no need to
		* update skb->csum here.
		*/

		if (unlikely(parse_vlan(skb, key)))
			return -ENOMEM;

		skb->protocol = parse_ethertype(skb);
		if (unlikely(skb->protocol == htons(0)))
			return -ENOMEM;

		skb_reset_network_header(skb);
		__skb_push(skb, skb->data - skb_mac_header(skb));
	}
	skb_reset_mac_len(skb);
	key->eth.type = skb->protocol;

	/* Network layer. */
	if (key->eth.type == htons(ETH_P_IP)) {
		struct iphdr *nh;
		__be16 offset;

		error = check_iphdr(skb);
		if (unlikely(error)) {
			memset(&key->ip, 0, sizeof(key->ip));
			memset(&key->ipv4, 0, sizeof(key->ipv4));
			if (error == -EINVAL) {
				skb->transport_header = skb->network_header;
				error = 0;
			}
			return error;
		}

		nh = ip_hdr(skb);
		key->ipv4.addr.src = nh->saddr;
		key->ipv4.addr.dst = nh->daddr;

		key->ip.proto = nh->protocol;
		key->ip.tos = nh->tos;
		key->ip.ttl = nh->ttl;

		offset = nh->frag_off & htons(IP_OFFSET);
		if (offset) {
			key->ip.frag = OVS_FRAG_TYPE_LATER;
			return 0;
		}
		if (nh->frag_off & htons(IP_MF) ||
			skb_shinfo(skb)->gso_type & SKB_GSO_UDP)
			key->ip.frag = OVS_FRAG_TYPE_FIRST;
		else
			key->ip.frag = OVS_FRAG_TYPE_NONE;

		/* Transport layer. */
		if (key->ip.proto == IPPROTO_TCP) {
			if (tcphdr_ok(skb)) {
				struct tcphdr *tcp = tcp_hdr(skb);
				key->tp.src = tcp->source;
				key->tp.dst = tcp->dest;
				key->tp.flags = TCP_FLAGS_BE16(tcp);
			} else {
				memset(&key->tp, 0, sizeof(key->tp));
			}

		} else if (key->ip.proto == IPPROTO_UDP) {
			if (udphdr_ok(skb)) {
				struct udphdr *udp = udp_hdr(skb);
				key->tp.src = udp->source;
				key->tp.dst = udp->dest;
			} else {
				memset(&key->tp, 0, sizeof(key->tp));
			}
		} else if (key->ip.proto == IPPROTO_SCTP) {
			if (sctphdr_ok(skb)) {
				struct sctphdr *sctp = sctp_hdr(skb);
				key->tp.src = sctp->source;
				key->tp.dst = sctp->dest;
			} else {
				memset(&key->tp, 0, sizeof(key->tp));
			}
		} else if (key->ip.proto == IPPROTO_ICMP) {
			if (icmphdr_ok(skb)) {
				struct icmphdr *icmp = icmp_hdr(skb);
				/* The ICMP type and code fields use the 16-bit
				 * transport port fields, so we need to store
				 * them in 16-bit network byte order. */
				key->tp.src = htons(icmp->type);
				key->tp.dst = htons(icmp->code);
			} else {
				memset(&key->tp, 0, sizeof(key->tp));
			}
		}

	} else if (key->eth.type == htons(ETH_P_ARP) ||
		   key->eth.type == htons(ETH_P_RARP)) {
		struct arp_eth_header *arp;
		bool arp_available = arphdr_ok(skb);

		arp = (struct arp_eth_header *)skb_network_header(skb);

		if (arp_available &&
		    arp->ar_hrd == htons(ARPHRD_ETHER) &&
		    arp->ar_pro == htons(ETH_P_IP) &&
		    arp->ar_hln == ETH_ALEN &&
		    arp->ar_pln == 4) {

			/* We only match on the lower 8 bits of the opcode. */
			if (ntohs(arp->ar_op) <= 0xff)
				key->ip.proto = ntohs(arp->ar_op);
			else
				key->ip.proto = 0;

			memcpy(&key->ipv4.addr.src, arp->ar_sip, sizeof(key->ipv4.addr.src));
			memcpy(&key->ipv4.addr.dst, arp->ar_tip, sizeof(key->ipv4.addr.dst));
			ether_addr_copy(key->ipv4.arp.sha, arp->ar_sha);
			ether_addr_copy(key->ipv4.arp.tha, arp->ar_tha);
		} else {
			memset(&key->ip, 0, sizeof(key->ip));
			memset(&key->ipv4, 0, sizeof(key->ipv4));
		}
	} else if (eth_p_mpls(key->eth.type)) {
		size_t stack_len = MPLS_HLEN;

		skb_set_inner_network_header(skb, skb->mac_len);
		while (1) {
			__be32 lse;

			error = check_header(skb, skb->mac_len + stack_len);
			if (unlikely(error))
				return 0;

			memcpy(&lse, skb_inner_network_header(skb), MPLS_HLEN);

			if (stack_len == MPLS_HLEN)
				memcpy(&key->mpls.top_lse, &lse, MPLS_HLEN);

			skb_set_inner_network_header(skb, skb->mac_len + stack_len);
			if (lse & htonl(MPLS_LS_S_MASK))
				break;

			stack_len += MPLS_HLEN;
		}
	} else if (key->eth.type == htons(ETH_P_IPV6)) {
		int nh_len;             /* IPv6 Header + Extensions */

		nh_len = parse_ipv6hdr(skb, key);
		if (unlikely(nh_len < 0)) {
			switch (nh_len) {
			case -EINVAL:
				memset(&key->ip, 0, sizeof(key->ip));
				memset(&key->ipv6.addr, 0, sizeof(key->ipv6.addr));
				/* fall-through */
			case -EPROTO:
				skb->transport_header = skb->network_header;
				error = 0;
				break;
			default:
				error = nh_len;
			}
			return error;
		}

		if (key->ip.frag == OVS_FRAG_TYPE_LATER)
			return 0;
		if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP)
			key->ip.frag = OVS_FRAG_TYPE_FIRST;

		/* Transport layer. */
		if (key->ip.proto == NEXTHDR_TCP) {
			if (tcphdr_ok(skb)) {
				struct tcphdr *tcp = tcp_hdr(skb);
				key->tp.src = tcp->source;
				key->tp.dst = tcp->dest;
				key->tp.flags = TCP_FLAGS_BE16(tcp);
			} else {
				memset(&key->tp, 0, sizeof(key->tp));
			}
		} else if (key->ip.proto == NEXTHDR_UDP) {
			if (udphdr_ok(skb)) {
				struct udphdr *udp = udp_hdr(skb);
				key->tp.src = udp->source;
				key->tp.dst = udp->dest;
			} else {
				memset(&key->tp, 0, sizeof(key->tp));
			}
		} else if (key->ip.proto == NEXTHDR_SCTP) {
			if (sctphdr_ok(skb)) {
				struct sctphdr *sctp = sctp_hdr(skb);
				key->tp.src = sctp->source;
				key->tp.dst = sctp->dest;
			} else {
				memset(&key->tp, 0, sizeof(key->tp));
			}
		} else if (key->ip.proto == NEXTHDR_ICMP) {
			if (icmp6hdr_ok(skb)) {
				error = parse_icmpv6(skb, key, nh_len);
				if (error)
					return error;
			} else {
				memset(&key->tp, 0, sizeof(key->tp));
			}
		}
	} else if (key->eth.type == htons(ETH_P_NSH)) {
		error = parse_nsh(skb, key);
		if (error)
			return error;
	}
	return 0;
}

int ovs_flow_key_update(struct sk_buff *skb, struct sw_flow_key *key)
{
	int res;

	res = key_extract(skb, key);
	if (!res)
		key->mac_proto &= ~SW_FLOW_KEY_INVALID;

	return res;
}

static int key_extract_mac_proto(struct sk_buff *skb)
{
	switch (skb->dev->type) {
	case ARPHRD_ETHER:
		return MAC_PROTO_ETHERNET;
	case ARPHRD_NONE:
		if (skb->protocol == htons(ETH_P_TEB))
			return MAC_PROTO_ETHERNET;
		return MAC_PROTO_NONE;
	}
	WARN_ON_ONCE(1);
	return -EINVAL;
}

int ovs_flow_key_extract(const struct ip_tunnel_info *tun_info,
			 struct sk_buff *skb, struct sw_flow_key *key)
{
	int res, err;

	/* Extract metadata from packet. */
	if (tun_info) {
		key->tun_proto = ip_tunnel_info_af(tun_info);
		memcpy(&key->tun_key, &tun_info->key, sizeof(key->tun_key));

		if (tun_info->options_len) {
			BUILD_BUG_ON((1 << (sizeof(tun_info->options_len) *
						   8)) - 1
					> sizeof(key->tun_opts));

			ip_tunnel_info_opts_get(TUN_METADATA_OPTS(key, tun_info->options_len),
						tun_info);
			key->tun_opts_len = tun_info->options_len;
		} else {
			key->tun_opts_len = 0;
		}
	} else  {
		key->tun_proto = 0;
		key->tun_opts_len = 0;
		memset(&key->tun_key, 0, sizeof(key->tun_key));
	}

	key->phy.priority = skb->priority;
	key->phy.in_port = OVS_CB(skb)->input_vport->port_no;
	key->phy.skb_mark = skb->mark;
	key->ovs_flow_hash = 0;
	res = key_extract_mac_proto(skb);
	if (res < 0)
		return res;
	key->mac_proto = res;
	key->recirc_id = 0;

	err = key_extract(skb, key);
	if (!err)
		ovs_ct_fill_key(skb, key);   /* Must be after key_extract(). */
	return err;
}

int ovs_flow_key_extract_userspace(struct net *net, const struct nlattr *attr,
				   struct sk_buff *skb,
				   struct sw_flow_key *key, bool log)
{
	const struct nlattr *a[OVS_KEY_ATTR_MAX + 1];
	u64 attrs = 0;
	int err;

	err = parse_flow_nlattrs(attr, a, &attrs, log);
	if (err)
		return -EINVAL;

	/* Extract metadata from netlink attributes. */
	err = ovs_nla_get_flow_metadata(net, a, attrs, key, log);
	if (err)
		return err;

	/* key_extract assumes that skb->protocol is set-up for
	 * layer 3 packets which is the case for other callers,
	 * in particular packets received from the network stack.
	 * Here the correct value can be set from the metadata
	 * extracted above.
	 * For L2 packet key eth type would be zero. skb protocol
	 * would be set to correct value later during key-extact.
	 */

	skb->protocol = key->eth.type;
	err = key_extract(skb, key);
	if (err)
		return err;

	/* Check that we have conntrack original direction tuple metadata only
	 * for packets for which it makes sense.  Otherwise the key may be
	 * corrupted due to overlapping key fields.
	 */
	if (attrs & (1 << OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV4) &&
	    key->eth.type != htons(ETH_P_IP))
		return -EINVAL;
	if (attrs & (1 << OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV6) &&
	    (key->eth.type != htons(ETH_P_IPV6) ||
	     sw_flow_key_is_nd(key)))
		return -EINVAL;

	return 0;
}
