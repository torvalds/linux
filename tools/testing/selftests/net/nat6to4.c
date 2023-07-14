// SPDX-License-Identifier: GPL-2.0-only
/*
 * This code is taken from the Android Open Source Project and the author
 * (Maciej Żenczykowski) has gave permission to relicense it under the
 * GPLv2. Therefore this program is free software;
 * You can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 2 as published by the Free Software
 * Foundation

 * The original headers, including the original license headers, are
 * included below for completeness.
 *
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <linux/bpf.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/pkt_cls.h>
#include <linux/swab.h>
#include <stdbool.h>
#include <stdint.h>


#include <linux/udp.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define IP_DF 0x4000  // Flag: "Don't Fragment"

SEC("schedcls/ingress6/nat_6")
int sched_cls_ingress6_nat_6_prog(struct __sk_buff *skb)
{
	const int l2_header_size =  sizeof(struct ethhdr);
	void *data = (void *)(long)skb->data;
	const void *data_end = (void *)(long)skb->data_end;
	const struct ethhdr * const eth = data;  // used iff is_ethernet
	const struct ipv6hdr * const ip6 =  (void *)(eth + 1);

	// Require ethernet dst mac address to be our unicast address.
	if  (skb->pkt_type != PACKET_HOST)
		return TC_ACT_OK;

	// Must be meta-ethernet IPv6 frame
	if (skb->protocol != bpf_htons(ETH_P_IPV6))
		return TC_ACT_OK;

	// Must have (ethernet and) ipv6 header
	if (data + l2_header_size + sizeof(*ip6) > data_end)
		return TC_ACT_OK;

	// Ethertype - if present - must be IPv6
	if (eth->h_proto != bpf_htons(ETH_P_IPV6))
		return TC_ACT_OK;

	// IP version must be 6
	if (ip6->version != 6)
		return TC_ACT_OK;
	// Maximum IPv6 payload length that can be translated to IPv4
	if (bpf_ntohs(ip6->payload_len) > 0xFFFF - sizeof(struct iphdr))
		return TC_ACT_OK;
	switch (ip6->nexthdr) {
	case IPPROTO_TCP:  // For TCP & UDP the checksum neutrality of the chosen IPv6
	case IPPROTO_UDP:  // address means there is no need to update their checksums.
	case IPPROTO_GRE:  // We do not need to bother looking at GRE/ESP headers,
	case IPPROTO_ESP:  // since there is never a checksum to update.
		break;
	default:  // do not know how to handle anything else
		return TC_ACT_OK;
	}

	struct ethhdr eth2;  // used iff is_ethernet

	eth2 = *eth;                     // Copy over the ethernet header (src/dst mac)
	eth2.h_proto = bpf_htons(ETH_P_IP);  // But replace the ethertype

	struct iphdr ip = {
		.version = 4,                                                      // u4
		.ihl = sizeof(struct iphdr) / sizeof(__u32),                       // u4
		.tos = (ip6->priority << 4) + (ip6->flow_lbl[0] >> 4),             // u8
		.tot_len = bpf_htons(bpf_ntohs(ip6->payload_len) + sizeof(struct iphdr)),  // u16
		.id = 0,                                                           // u16
		.frag_off = bpf_htons(IP_DF),                                          // u16
		.ttl = ip6->hop_limit,                                             // u8
		.protocol = ip6->nexthdr,                                          // u8
		.check = 0,                                                        // u16
		.saddr = 0x0201a8c0,                            // u32
		.daddr = 0x0101a8c0,                                         // u32
	};

	// Calculate the IPv4 one's complement checksum of the IPv4 header.
	__wsum sum4 = 0;

	for (int i = 0; i < sizeof(ip) / sizeof(__u16); ++i)
		sum4 += ((__u16 *)&ip)[i];

	// Note that sum4 is guaranteed to be non-zero by virtue of ip.version == 4
	sum4 = (sum4 & 0xFFFF) + (sum4 >> 16);  // collapse u32 into range 1 .. 0x1FFFE
	sum4 = (sum4 & 0xFFFF) + (sum4 >> 16);  // collapse any potential carry into u16
	ip.check = (__u16)~sum4;                // sum4 cannot be zero, so this is never 0xFFFF

	// Calculate the *negative* IPv6 16-bit one's complement checksum of the IPv6 header.
	__wsum sum6 = 0;
	// We'll end up with a non-zero sum due to ip6->version == 6 (which has '0' bits)
	for (int i = 0; i < sizeof(*ip6) / sizeof(__u16); ++i)
		sum6 += ~((__u16 *)ip6)[i];  // note the bitwise negation

	// Note that there is no L4 checksum update: we are relying on the checksum neutrality
	// of the ipv6 address chosen by netd's ClatdController.

	// Packet mutations begin - point of no return, but if this first modification fails
	// the packet is probably still pristine, so let clatd handle it.
	if (bpf_skb_change_proto(skb, bpf_htons(ETH_P_IP), 0))
		return TC_ACT_OK;
	bpf_csum_update(skb, sum6);

	data = (void *)(long)skb->data;
	data_end = (void *)(long)skb->data_end;
	if (data + l2_header_size + sizeof(struct iphdr) > data_end)
		return TC_ACT_SHOT;

	struct ethhdr *new_eth = data;

	// Copy over the updated ethernet header
	*new_eth = eth2;

	// Copy over the new ipv4 header.
	*(struct iphdr *)(new_eth + 1) = ip;
	return bpf_redirect(skb->ifindex, BPF_F_INGRESS);
}

SEC("schedcls/egress4/snat4")
int sched_cls_egress4_snat4_prog(struct __sk_buff *skb)
{
	const int l2_header_size =  sizeof(struct ethhdr);
	void *data = (void *)(long)skb->data;
	const void *data_end = (void *)(long)skb->data_end;
	const struct ethhdr *const eth = data;  // used iff is_ethernet
	const struct iphdr *const ip4 = (void *)(eth + 1);

	// Must be meta-ethernet IPv4 frame
	if (skb->protocol != bpf_htons(ETH_P_IP))
		return TC_ACT_OK;

	// Must have ipv4 header
	if (data + l2_header_size + sizeof(struct ipv6hdr) > data_end)
		return TC_ACT_OK;

	// Ethertype - if present - must be IPv4
	if (eth->h_proto != bpf_htons(ETH_P_IP))
		return TC_ACT_OK;

	// IP version must be 4
	if (ip4->version != 4)
		return TC_ACT_OK;

	// We cannot handle IP options, just standard 20 byte == 5 dword minimal IPv4 header
	if (ip4->ihl != 5)
		return TC_ACT_OK;

	// Maximum IPv6 payload length that can be translated to IPv4
	if (bpf_htons(ip4->tot_len) > 0xFFFF - sizeof(struct ipv6hdr))
		return TC_ACT_OK;

	// Calculate the IPv4 one's complement checksum of the IPv4 header.
	__wsum sum4 = 0;

	for (int i = 0; i < sizeof(*ip4) / sizeof(__u16); ++i)
		sum4 += ((__u16 *)ip4)[i];

	// Note that sum4 is guaranteed to be non-zero by virtue of ip4->version == 4
	sum4 = (sum4 & 0xFFFF) + (sum4 >> 16);  // collapse u32 into range 1 .. 0x1FFFE
	sum4 = (sum4 & 0xFFFF) + (sum4 >> 16);  // collapse any potential carry into u16
	// for a correct checksum we should get *a* zero, but sum4 must be positive, ie 0xFFFF
	if (sum4 != 0xFFFF)
		return TC_ACT_OK;

	// Minimum IPv4 total length is the size of the header
	if (bpf_ntohs(ip4->tot_len) < sizeof(*ip4))
		return TC_ACT_OK;

	// We are incapable of dealing with IPv4 fragments
	if (ip4->frag_off & ~bpf_htons(IP_DF))
		return TC_ACT_OK;

	switch (ip4->protocol) {
	case IPPROTO_TCP:  // For TCP & UDP the checksum neutrality of the chosen IPv6
	case IPPROTO_GRE:  // address means there is no need to update their checksums.
	case IPPROTO_ESP:  // We do not need to bother looking at GRE/ESP headers,
		break;         // since there is never a checksum to update.

	case IPPROTO_UDP:  // See above comment, but must also have UDP header...
		if (data + sizeof(*ip4) + sizeof(struct udphdr) > data_end)
			return TC_ACT_OK;
		const struct udphdr *uh = (const struct udphdr *)(ip4 + 1);
		// If IPv4/UDP checksum is 0 then fallback to clatd so it can calculate the
		// checksum.  Otherwise the network or more likely the NAT64 gateway might
		// drop the packet because in most cases IPv6/UDP packets with a zero checksum
		// are invalid. See RFC 6935.  TODO: calculate checksum via bpf_csum_diff()
		if (!uh->check)
			return TC_ACT_OK;
		break;

	default:  // do not know how to handle anything else
		return TC_ACT_OK;
	}
	struct ethhdr eth2;  // used iff is_ethernet

	eth2 = *eth;                     // Copy over the ethernet header (src/dst mac)
	eth2.h_proto = bpf_htons(ETH_P_IPV6);  // But replace the ethertype

	struct ipv6hdr ip6 = {
		.version = 6,                                    // __u8:4
		.priority = ip4->tos >> 4,                       // __u8:4
		.flow_lbl = {(ip4->tos & 0xF) << 4, 0, 0},       // __u8[3]
		.payload_len = bpf_htons(bpf_ntohs(ip4->tot_len) - 20),  // __be16
		.nexthdr = ip4->protocol,                        // __u8
		.hop_limit = ip4->ttl,                           // __u8
	};
	ip6.saddr.in6_u.u6_addr32[0] = bpf_htonl(0x20010db8);
	ip6.saddr.in6_u.u6_addr32[1] = 0;
	ip6.saddr.in6_u.u6_addr32[2] = 0;
	ip6.saddr.in6_u.u6_addr32[3] = bpf_htonl(1);
	ip6.daddr.in6_u.u6_addr32[0] = bpf_htonl(0x20010db8);
	ip6.daddr.in6_u.u6_addr32[1] = 0;
	ip6.daddr.in6_u.u6_addr32[2] = 0;
	ip6.daddr.in6_u.u6_addr32[3] = bpf_htonl(2);

	// Calculate the IPv6 16-bit one's complement checksum of the IPv6 header.
	__wsum sum6 = 0;
	// We'll end up with a non-zero sum due to ip6.version == 6
	for (int i = 0; i < sizeof(ip6) / sizeof(__u16); ++i)
		sum6 += ((__u16 *)&ip6)[i];

	// Packet mutations begin - point of no return, but if this first modification fails
	// the packet is probably still pristine, so let clatd handle it.
	if (bpf_skb_change_proto(skb, bpf_htons(ETH_P_IPV6), 0))
		return TC_ACT_OK;

	// This takes care of updating the skb->csum field for a CHECKSUM_COMPLETE packet.
	// In such a case, skb->csum is a 16-bit one's complement sum of the entire payload,
	// thus we need to subtract out the ipv4 header's sum, and add in the ipv6 header's sum.
	// However, we've already verified the ipv4 checksum is correct and thus 0.
	// Thus we only need to add the ipv6 header's sum.
	//
	// bpf_csum_update() always succeeds if the skb is CHECKSUM_COMPLETE and returns an error
	// (-ENOTSUPP) if it isn't.  So we just ignore the return code (see above for more details).
	bpf_csum_update(skb, sum6);

	// bpf_skb_change_proto() invalidates all pointers - reload them.
	data = (void *)(long)skb->data;
	data_end = (void *)(long)skb->data_end;

	// I cannot think of any valid way for this error condition to trigger, however I do
	// believe the explicit check is required to keep the in kernel ebpf verifier happy.
	if (data + l2_header_size + sizeof(ip6) > data_end)
		return TC_ACT_SHOT;

	struct ethhdr *new_eth = data;

	// Copy over the updated ethernet header
	*new_eth = eth2;
	// Copy over the new ipv4 header.
	*(struct ipv6hdr *)(new_eth + 1) = ip6;
	return TC_ACT_OK;
}

char _license[] SEC("license") = ("GPL");
