/*
 * Copyright 2011, Siemens AG
 * written by Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

/*
 * Based on patches from Jon Smirl <jonsmirl@gmail.com>
 * Copyright (c) 2011 Jon Smirl <jonsmirl@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* Jon's code is based on 6lowpan implementation for Contiki which is:
 * Copyright (c) 2008, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <linux/bitops.h>
#include <linux/if_arp.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/6lowpan.h>
#include <net/ipv6.h>
#include <net/af_ieee802154.h>

/*
 * Uncompress address function for source and
 * destination address(non-multicast).
 *
 * address_mode is sam value or dam value.
 */
static int uncompress_addr(struct sk_buff *skb,
				struct in6_addr *ipaddr, const u8 address_mode,
				const u8 *lladdr, const u8 addr_type,
				const u8 addr_len)
{
	bool fail;

	switch (address_mode) {
	case LOWPAN_IPHC_ADDR_00:
		/* for global link addresses */
		fail = lowpan_fetch_skb(skb, ipaddr->s6_addr, 16);
		break;
	case LOWPAN_IPHC_ADDR_01:
		/* fe:80::XXXX:XXXX:XXXX:XXXX */
		ipaddr->s6_addr[0] = 0xFE;
		ipaddr->s6_addr[1] = 0x80;
		fail = lowpan_fetch_skb(skb, &ipaddr->s6_addr[8], 8);
		break;
	case LOWPAN_IPHC_ADDR_02:
		/* fe:80::ff:fe00:XXXX */
		ipaddr->s6_addr[0] = 0xFE;
		ipaddr->s6_addr[1] = 0x80;
		ipaddr->s6_addr[11] = 0xFF;
		ipaddr->s6_addr[12] = 0xFE;
		fail = lowpan_fetch_skb(skb, &ipaddr->s6_addr[14], 2);
		break;
	case LOWPAN_IPHC_ADDR_03:
		fail = false;
		switch (addr_type) {
		case IEEE802154_ADDR_LONG:
			/* fe:80::XXXX:XXXX:XXXX:XXXX
			 *        \_________________/
			 *              hwaddr
			 */
			ipaddr->s6_addr[0] = 0xFE;
			ipaddr->s6_addr[1] = 0x80;
			memcpy(&ipaddr->s6_addr[8], lladdr, addr_len);
			/* second bit-flip (Universe/Local)
			 * is done according RFC2464
			 */
			ipaddr->s6_addr[8] ^= 0x02;
			break;
		case IEEE802154_ADDR_SHORT:
			/* fe:80::ff:fe00:XXXX
			 *		  \__/
			 *	       short_addr
			 *
			 * Universe/Local bit is zero.
			 */
			ipaddr->s6_addr[0] = 0xFE;
			ipaddr->s6_addr[1] = 0x80;
			ipaddr->s6_addr[11] = 0xFF;
			ipaddr->s6_addr[12] = 0xFE;
			ipaddr->s6_addr16[7] = htons(*((u16 *)lladdr));
			break;
		default:
			pr_debug("Invalid addr_type set\n");
			return -EINVAL;
		}
		break;
	default:
		pr_debug("Invalid address mode value: 0x%x\n", address_mode);
		return -EINVAL;
	}

	if (fail) {
		pr_debug("Failed to fetch skb data\n");
		return -EIO;
	}

	raw_dump_inline(NULL, "Reconstructed ipv6 addr is",
			ipaddr->s6_addr, 16);

	return 0;
}

/*
 * Uncompress address function for source context
 * based address(non-multicast).
 */
static int uncompress_context_based_src_addr(struct sk_buff *skb,
						struct in6_addr *ipaddr,
						const u8 sam)
{
	switch (sam) {
	case LOWPAN_IPHC_ADDR_00:
		/* unspec address ::
		 * Do nothing, address is already ::
		 */
		break;
	case LOWPAN_IPHC_ADDR_01:
		/* TODO */
	case LOWPAN_IPHC_ADDR_02:
		/* TODO */
	case LOWPAN_IPHC_ADDR_03:
		/* TODO */
		netdev_warn(skb->dev, "SAM value 0x%x not supported\n", sam);
		return -EINVAL;
	default:
		pr_debug("Invalid sam value: 0x%x\n", sam);
		return -EINVAL;
	}

	raw_dump_inline(NULL,
			"Reconstructed context based ipv6 src addr is",
			ipaddr->s6_addr, 16);

	return 0;
}

static int skb_deliver(struct sk_buff *skb, struct ipv6hdr *hdr,
		struct net_device *dev, skb_delivery_cb deliver_skb)
{
	struct sk_buff *new;
	int stat;

	new = skb_copy_expand(skb, sizeof(struct ipv6hdr), skb_tailroom(skb),
								GFP_ATOMIC);
	kfree_skb(skb);

	if (!new)
		return -ENOMEM;

	skb_push(new, sizeof(struct ipv6hdr));
	skb_reset_network_header(new);
	skb_copy_to_linear_data(new, hdr, sizeof(struct ipv6hdr));

	new->protocol = htons(ETH_P_IPV6);
	new->pkt_type = PACKET_HOST;
	new->dev = dev;

	raw_dump_table(__func__, "raw skb data dump before receiving",
			new->data, new->len);

	stat = deliver_skb(new, dev);

	kfree_skb(new);

	return stat;
}

/* Uncompress function for multicast destination address,
 * when M bit is set.
 */
static int
lowpan_uncompress_multicast_daddr(struct sk_buff *skb,
		struct in6_addr *ipaddr,
		const u8 dam)
{
	bool fail;

	switch (dam) {
	case LOWPAN_IPHC_DAM_00:
		/* 00:  128 bits.  The full address
		 * is carried in-line.
		 */
		fail = lowpan_fetch_skb(skb, ipaddr->s6_addr, 16);
		break;
	case LOWPAN_IPHC_DAM_01:
		/* 01:  48 bits.  The address takes
		 * the form ffXX::00XX:XXXX:XXXX.
		 */
		ipaddr->s6_addr[0] = 0xFF;
		fail = lowpan_fetch_skb(skb, &ipaddr->s6_addr[1], 1);
		fail |= lowpan_fetch_skb(skb, &ipaddr->s6_addr[11], 5);
		break;
	case LOWPAN_IPHC_DAM_10:
		/* 10:  32 bits.  The address takes
		 * the form ffXX::00XX:XXXX.
		 */
		ipaddr->s6_addr[0] = 0xFF;
		fail = lowpan_fetch_skb(skb, &ipaddr->s6_addr[1], 1);
		fail |= lowpan_fetch_skb(skb, &ipaddr->s6_addr[13], 3);
		break;
	case LOWPAN_IPHC_DAM_11:
		/* 11:  8 bits.  The address takes
		 * the form ff02::00XX.
		 */
		ipaddr->s6_addr[0] = 0xFF;
		ipaddr->s6_addr[1] = 0x02;
		fail = lowpan_fetch_skb(skb, &ipaddr->s6_addr[15], 1);
		break;
	default:
		pr_debug("DAM value has a wrong value: 0x%x\n", dam);
		return -EINVAL;
	}

	if (fail) {
		pr_debug("Failed to fetch skb data\n");
		return -EIO;
	}

	raw_dump_inline(NULL, "Reconstructed ipv6 multicast addr is",
				ipaddr->s6_addr, 16);

	return 0;
}

static int
uncompress_udp_header(struct sk_buff *skb, struct udphdr *uh)
{
	bool fail;
	u8 tmp = 0, val = 0;

	if (!uh)
		goto err;

	fail = lowpan_fetch_skb(skb, &tmp, 1);

	if ((tmp & LOWPAN_NHC_UDP_MASK) == LOWPAN_NHC_UDP_ID) {
		pr_debug("UDP header uncompression\n");
		switch (tmp & LOWPAN_NHC_UDP_CS_P_11) {
		case LOWPAN_NHC_UDP_CS_P_00:
			fail |= lowpan_fetch_skb(skb, &uh->source, 2);
			fail |= lowpan_fetch_skb(skb, &uh->dest, 2);
			break;
		case LOWPAN_NHC_UDP_CS_P_01:
			fail |= lowpan_fetch_skb(skb, &uh->source, 2);
			fail |= lowpan_fetch_skb(skb, &val, 1);
			uh->dest = htons(val + LOWPAN_NHC_UDP_8BIT_PORT);
			break;
		case LOWPAN_NHC_UDP_CS_P_10:
			fail |= lowpan_fetch_skb(skb, &val, 1);
			uh->source = htons(val + LOWPAN_NHC_UDP_8BIT_PORT);
			fail |= lowpan_fetch_skb(skb, &uh->dest, 2);
			break;
		case LOWPAN_NHC_UDP_CS_P_11:
			fail |= lowpan_fetch_skb(skb, &val, 1);
			uh->source = htons(LOWPAN_NHC_UDP_4BIT_PORT +
					   (val >> 4));
			uh->dest = htons(LOWPAN_NHC_UDP_4BIT_PORT +
					 (val & 0x0f));
			break;
		default:
			pr_debug("ERROR: unknown UDP format\n");
			goto err;
			break;
		}

		pr_debug("uncompressed UDP ports: src = %d, dst = %d\n",
			 ntohs(uh->source), ntohs(uh->dest));

		/* checksum */
		if (tmp & LOWPAN_NHC_UDP_CS_C) {
			pr_debug_ratelimited("checksum elided currently not supported\n");
			goto err;
		} else {
			fail |= lowpan_fetch_skb(skb, &uh->check, 2);
		}

		/*
		 * UDP lenght needs to be infered from the lower layers
		 * here, we obtain the hint from the remaining size of the
		 * frame
		 */
		uh->len = htons(skb->len + sizeof(struct udphdr));
		pr_debug("uncompressed UDP length: src = %d", ntohs(uh->len));
	} else {
		pr_debug("ERROR: unsupported NH format\n");
		goto err;
	}

	if (fail)
		goto err;

	return 0;
err:
	return -EINVAL;
}

/* TTL uncompression values */
static const u8 lowpan_ttl_values[] = { 0, 1, 64, 255 };

int lowpan_process_data(struct sk_buff *skb, struct net_device *dev,
		const u8 *saddr, const u8 saddr_type, const u8 saddr_len,
		const u8 *daddr, const u8 daddr_type, const u8 daddr_len,
		u8 iphc0, u8 iphc1, skb_delivery_cb deliver_skb)
{
	struct ipv6hdr hdr = {};
	u8 tmp, num_context = 0;
	int err;

	raw_dump_table(__func__, "raw skb data dump uncompressed",
				skb->data, skb->len);

	/* another if the CID flag is set */
	if (iphc1 & LOWPAN_IPHC_CID) {
		pr_debug("CID flag is set, increase header with one\n");
		if (lowpan_fetch_skb_u8(skb, &num_context))
			goto drop;
	}

	hdr.version = 6;

	/* Traffic Class and Flow Label */
	switch ((iphc0 & LOWPAN_IPHC_TF) >> 3) {
	/*
	 * Traffic Class and FLow Label carried in-line
	 * ECN + DSCP + 4-bit Pad + Flow Label (4 bytes)
	 */
	case 0: /* 00b */
		if (lowpan_fetch_skb_u8(skb, &tmp))
			goto drop;

		memcpy(&hdr.flow_lbl, &skb->data[0], 3);
		skb_pull(skb, 3);
		hdr.priority = ((tmp >> 2) & 0x0f);
		hdr.flow_lbl[0] = ((tmp >> 2) & 0x30) | (tmp << 6) |
					(hdr.flow_lbl[0] & 0x0f);
		break;
	/*
	 * Traffic class carried in-line
	 * ECN + DSCP (1 byte), Flow Label is elided
	 */
	case 2: /* 10b */
		if (lowpan_fetch_skb_u8(skb, &tmp))
			goto drop;

		hdr.priority = ((tmp >> 2) & 0x0f);
		hdr.flow_lbl[0] = ((tmp << 6) & 0xC0) | ((tmp >> 2) & 0x30);
		break;
	/*
	 * Flow Label carried in-line
	 * ECN + 2-bit Pad + Flow Label (3 bytes), DSCP is elided
	 */
	case 1: /* 01b */
		if (lowpan_fetch_skb_u8(skb, &tmp))
			goto drop;

		hdr.flow_lbl[0] = (skb->data[0] & 0x0F) | ((tmp >> 2) & 0x30);
		memcpy(&hdr.flow_lbl[1], &skb->data[0], 2);
		skb_pull(skb, 2);
		break;
	/* Traffic Class and Flow Label are elided */
	case 3: /* 11b */
		break;
	default:
		break;
	}

	/* Next Header */
	if ((iphc0 & LOWPAN_IPHC_NH_C) == 0) {
		/* Next header is carried inline */
		if (lowpan_fetch_skb_u8(skb, &(hdr.nexthdr)))
			goto drop;

		pr_debug("NH flag is set, next header carried inline: %02x\n",
			 hdr.nexthdr);
	}

	/* Hop Limit */
	if ((iphc0 & 0x03) != LOWPAN_IPHC_TTL_I)
		hdr.hop_limit = lowpan_ttl_values[iphc0 & 0x03];
	else {
		if (lowpan_fetch_skb_u8(skb, &(hdr.hop_limit)))
			goto drop;
	}

	/* Extract SAM to the tmp variable */
	tmp = ((iphc1 & LOWPAN_IPHC_SAM) >> LOWPAN_IPHC_SAM_BIT) & 0x03;

	if (iphc1 & LOWPAN_IPHC_SAC) {
		/* Source address context based uncompression */
		pr_debug("SAC bit is set. Handle context based source address.\n");
		err = uncompress_context_based_src_addr(
				skb, &hdr.saddr, tmp);
	} else {
		/* Source address uncompression */
		pr_debug("source address stateless compression\n");
		err = uncompress_addr(skb, &hdr.saddr, tmp, saddr,
					saddr_type, saddr_len);
	}

	/* Check on error of previous branch */
	if (err)
		goto drop;

	/* Extract DAM to the tmp variable */
	tmp = ((iphc1 & LOWPAN_IPHC_DAM_11) >> LOWPAN_IPHC_DAM_BIT) & 0x03;

	/* check for Multicast Compression */
	if (iphc1 & LOWPAN_IPHC_M) {
		if (iphc1 & LOWPAN_IPHC_DAC) {
			pr_debug("dest: context-based mcast compression\n");
			/* TODO: implement this */
		} else {
			err = lowpan_uncompress_multicast_daddr(
						skb, &hdr.daddr, tmp);
			if (err)
				goto drop;
		}
	} else {
		err = uncompress_addr(skb, &hdr.daddr, tmp, daddr,
					daddr_type, daddr_len);
		pr_debug("dest: stateless compression mode %d dest %pI6c\n",
			tmp, &hdr.daddr);
		if (err)
			goto drop;
	}

	/* UDP data uncompression */
	if (iphc0 & LOWPAN_IPHC_NH_C) {
		struct udphdr uh;
		struct sk_buff *new;
		if (uncompress_udp_header(skb, &uh))
			goto drop;

		/*
		 * replace the compressed UDP head by the uncompressed UDP
		 * header
		 */
		new = skb_copy_expand(skb, sizeof(struct udphdr),
				      skb_tailroom(skb), GFP_ATOMIC);
		kfree_skb(skb);

		if (!new)
			return -ENOMEM;

		skb = new;

		skb_push(skb, sizeof(struct udphdr));
		skb_reset_transport_header(skb);
		skb_copy_to_linear_data(skb, &uh, sizeof(struct udphdr));

		raw_dump_table(__func__, "raw UDP header dump",
				      (u8 *)&uh, sizeof(uh));

		hdr.nexthdr = UIP_PROTO_UDP;
	}

	hdr.payload_len = htons(skb->len);

	pr_debug("skb headroom size = %d, data length = %d\n",
		 skb_headroom(skb), skb->len);

	pr_debug("IPv6 header dump:\n\tversion = %d\n\tlength  = %d\n\t"
		 "nexthdr = 0x%02x\n\thop_lim = %d\n\tdest    = %pI6c\n",
		hdr.version, ntohs(hdr.payload_len), hdr.nexthdr,
		hdr.hop_limit, &hdr.daddr);

	raw_dump_table(__func__, "raw header dump", (u8 *)&hdr,
							sizeof(hdr));

	return skb_deliver(skb, &hdr, dev, deliver_skb);

drop:
	kfree_skb(skb);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(lowpan_process_data);

static u8 lowpan_compress_addr_64(u8 **hc_ptr, u8 shift,
				  const struct in6_addr *ipaddr,
				  const unsigned char *lladdr)
{
	u8 val = 0;

	if (is_addr_mac_addr_based(ipaddr, lladdr)) {
		val = 3; /* 0-bits */
		pr_debug("address compression 0 bits\n");
	} else if (lowpan_is_iid_16_bit_compressable(ipaddr)) {
		/* compress IID to 16 bits xxxx::XXXX */
		memcpy(*hc_ptr, &ipaddr->s6_addr16[7], 2);
		*hc_ptr += 2;
		val = 2; /* 16-bits */
		raw_dump_inline(NULL, "Compressed ipv6 addr is (16 bits)",
				*hc_ptr - 2, 2);
	} else {
		/* do not compress IID => xxxx::IID */
		memcpy(*hc_ptr, &ipaddr->s6_addr16[4], 8);
		*hc_ptr += 8;
		val = 1; /* 64-bits */
		raw_dump_inline(NULL, "Compressed ipv6 addr is (64 bits)",
				*hc_ptr - 8, 8);
	}

	return rol8(val, shift);
}

static void compress_udp_header(u8 **hc_ptr, struct sk_buff *skb)
{
	struct udphdr *uh = udp_hdr(skb);
	u8 tmp;

	if (((ntohs(uh->source) & LOWPAN_NHC_UDP_4BIT_MASK) ==
	     LOWPAN_NHC_UDP_4BIT_PORT) &&
	    ((ntohs(uh->dest) & LOWPAN_NHC_UDP_4BIT_MASK) ==
	     LOWPAN_NHC_UDP_4BIT_PORT)) {
		pr_debug("UDP header: both ports compression to 4 bits\n");
		/* compression value */
		tmp = LOWPAN_NHC_UDP_CS_P_11;
		lowpan_push_hc_data(hc_ptr, &tmp, sizeof(tmp));
		/* source and destination port */
		tmp = ntohs(uh->dest) - LOWPAN_NHC_UDP_4BIT_PORT +
		      ((ntohs(uh->source) - LOWPAN_NHC_UDP_4BIT_PORT) << 4);
		lowpan_push_hc_data(hc_ptr, &tmp, sizeof(tmp));
	} else if ((ntohs(uh->dest) & LOWPAN_NHC_UDP_8BIT_MASK) ==
			LOWPAN_NHC_UDP_8BIT_PORT) {
		pr_debug("UDP header: remove 8 bits of dest\n");
		/* compression value */
		tmp = LOWPAN_NHC_UDP_CS_P_01;
		lowpan_push_hc_data(hc_ptr, &tmp, sizeof(tmp));
		/* source port */
		lowpan_push_hc_data(hc_ptr, &uh->source, sizeof(uh->source));
		/* destination port */
		tmp = ntohs(uh->dest) - LOWPAN_NHC_UDP_8BIT_PORT;
		lowpan_push_hc_data(hc_ptr, &tmp, sizeof(tmp));
	} else if ((ntohs(uh->source) & LOWPAN_NHC_UDP_8BIT_MASK) ==
			LOWPAN_NHC_UDP_8BIT_PORT) {
		pr_debug("UDP header: remove 8 bits of source\n");
		/* compression value */
		tmp = LOWPAN_NHC_UDP_CS_P_10;
		lowpan_push_hc_data(hc_ptr, &tmp, sizeof(tmp));
		/* source port */
		tmp = ntohs(uh->source) - LOWPAN_NHC_UDP_8BIT_PORT;
		lowpan_push_hc_data(hc_ptr, &tmp, sizeof(tmp));
		/* destination port */
		lowpan_push_hc_data(hc_ptr, &uh->dest, sizeof(uh->dest));
	} else {
		pr_debug("UDP header: can't compress\n");
		/* compression value */
		tmp = LOWPAN_NHC_UDP_CS_P_00;
		lowpan_push_hc_data(hc_ptr, &tmp, sizeof(tmp));
		/* source port */
		lowpan_push_hc_data(hc_ptr, &uh->source, sizeof(uh->source));
		/* destination port */
		lowpan_push_hc_data(hc_ptr, &uh->dest, sizeof(uh->dest));
	}

	/* checksum is always inline */
	lowpan_push_hc_data(hc_ptr, &uh->check, sizeof(uh->check));

	/* skip the UDP header */
	skb_pull(skb, sizeof(struct udphdr));
}

int lowpan_header_compress(struct sk_buff *skb, struct net_device *dev,
			unsigned short type, const void *_daddr,
			const void *_saddr, unsigned int len)
{
	u8 tmp, iphc0, iphc1, *hc_ptr;
	struct ipv6hdr *hdr;
	u8 head[100] = {};

	if (type != ETH_P_IPV6)
		return -EINVAL;

	hdr = ipv6_hdr(skb);
	hc_ptr = head + 2;

	pr_debug("IPv6 header dump:\n\tversion = %d\n\tlength  = %d\n"
		 "\tnexthdr = 0x%02x\n\thop_lim = %d\n\tdest    = %pI6c\n",
		hdr->version, ntohs(hdr->payload_len), hdr->nexthdr,
		hdr->hop_limit, &hdr->daddr);

	raw_dump_table(__func__, "raw skb network header dump",
		skb_network_header(skb), sizeof(struct ipv6hdr));

	/*
	 * As we copy some bit-length fields, in the IPHC encoding bytes,
	 * we sometimes use |=
	 * If the field is 0, and the current bit value in memory is 1,
	 * this does not work. We therefore reset the IPHC encoding here
	 */
	iphc0 = LOWPAN_DISPATCH_IPHC;
	iphc1 = 0;

	/* TODO: context lookup */

	raw_dump_inline(__func__, "saddr",
			(unsigned char *)_saddr, IEEE802154_ADDR_LEN);
	raw_dump_inline(__func__, "daddr",
			(unsigned char *)_daddr, IEEE802154_ADDR_LEN);

	raw_dump_table(__func__,
			"sending raw skb network uncompressed packet",
			skb->data, skb->len);

	/*
	 * Traffic class, flow label
	 * If flow label is 0, compress it. If traffic class is 0, compress it
	 * We have to process both in the same time as the offset of traffic
	 * class depends on the presence of version and flow label
	 */

	/* hc format of TC is ECN | DSCP , original one is DSCP | ECN */
	tmp = (hdr->priority << 4) | (hdr->flow_lbl[0] >> 4);
	tmp = ((tmp & 0x03) << 6) | (tmp >> 2);

	if (((hdr->flow_lbl[0] & 0x0F) == 0) &&
	     (hdr->flow_lbl[1] == 0) && (hdr->flow_lbl[2] == 0)) {
		/* flow label can be compressed */
		iphc0 |= LOWPAN_IPHC_FL_C;
		if ((hdr->priority == 0) &&
		   ((hdr->flow_lbl[0] & 0xF0) == 0)) {
			/* compress (elide) all */
			iphc0 |= LOWPAN_IPHC_TC_C;
		} else {
			/* compress only the flow label */
			*hc_ptr = tmp;
			hc_ptr += 1;
		}
	} else {
		/* Flow label cannot be compressed */
		if ((hdr->priority == 0) &&
		   ((hdr->flow_lbl[0] & 0xF0) == 0)) {
			/* compress only traffic class */
			iphc0 |= LOWPAN_IPHC_TC_C;
			*hc_ptr = (tmp & 0xc0) | (hdr->flow_lbl[0] & 0x0F);
			memcpy(hc_ptr + 1, &hdr->flow_lbl[1], 2);
			hc_ptr += 3;
		} else {
			/* compress nothing */
			memcpy(hc_ptr, hdr, 4);
			/* replace the top byte with new ECN | DSCP format */
			*hc_ptr = tmp;
			hc_ptr += 4;
		}
	}

	/* NOTE: payload length is always compressed */

	/* Next Header is compress if UDP */
	if (hdr->nexthdr == UIP_PROTO_UDP)
		iphc0 |= LOWPAN_IPHC_NH_C;

	if ((iphc0 & LOWPAN_IPHC_NH_C) == 0) {
		*hc_ptr = hdr->nexthdr;
		hc_ptr += 1;
	}

	/*
	 * Hop limit
	 * if 1:   compress, encoding is 01
	 * if 64:  compress, encoding is 10
	 * if 255: compress, encoding is 11
	 * else do not compress
	 */
	switch (hdr->hop_limit) {
	case 1:
		iphc0 |= LOWPAN_IPHC_TTL_1;
		break;
	case 64:
		iphc0 |= LOWPAN_IPHC_TTL_64;
		break;
	case 255:
		iphc0 |= LOWPAN_IPHC_TTL_255;
		break;
	default:
		*hc_ptr = hdr->hop_limit;
		hc_ptr += 1;
		break;
	}

	/* source address compression */
	if (is_addr_unspecified(&hdr->saddr)) {
		pr_debug("source address is unspecified, setting SAC\n");
		iphc1 |= LOWPAN_IPHC_SAC;
	/* TODO: context lookup */
	} else if (is_addr_link_local(&hdr->saddr)) {
		iphc1 |= lowpan_compress_addr_64(&hc_ptr,
				LOWPAN_IPHC_SAM_BIT, &hdr->saddr, _saddr);
		pr_debug("source address unicast link-local %pI6c "
			"iphc1 0x%02x\n", &hdr->saddr, iphc1);
	} else {
		pr_debug("send the full source address\n");
		memcpy(hc_ptr, &hdr->saddr.s6_addr16[0], 16);
		hc_ptr += 16;
	}

	/* destination address compression */
	if (is_addr_mcast(&hdr->daddr)) {
		pr_debug("destination address is multicast: ");
		iphc1 |= LOWPAN_IPHC_M;
		if (lowpan_is_mcast_addr_compressable8(&hdr->daddr)) {
			pr_debug("compressed to 1 octet\n");
			iphc1 |= LOWPAN_IPHC_DAM_11;
			/* use last byte */
			*hc_ptr = hdr->daddr.s6_addr[15];
			hc_ptr += 1;
		} else if (lowpan_is_mcast_addr_compressable32(&hdr->daddr)) {
			pr_debug("compressed to 4 octets\n");
			iphc1 |= LOWPAN_IPHC_DAM_10;
			/* second byte + the last three */
			*hc_ptr = hdr->daddr.s6_addr[1];
			memcpy(hc_ptr + 1, &hdr->daddr.s6_addr[13], 3);
			hc_ptr += 4;
		} else if (lowpan_is_mcast_addr_compressable48(&hdr->daddr)) {
			pr_debug("compressed to 6 octets\n");
			iphc1 |= LOWPAN_IPHC_DAM_01;
			/* second byte + the last five */
			*hc_ptr = hdr->daddr.s6_addr[1];
			memcpy(hc_ptr + 1, &hdr->daddr.s6_addr[11], 5);
			hc_ptr += 6;
		} else {
			pr_debug("using full address\n");
			iphc1 |= LOWPAN_IPHC_DAM_00;
			memcpy(hc_ptr, &hdr->daddr.s6_addr[0], 16);
			hc_ptr += 16;
		}
	} else {
		/* TODO: context lookup */
		if (is_addr_link_local(&hdr->daddr)) {
			iphc1 |= lowpan_compress_addr_64(&hc_ptr,
				LOWPAN_IPHC_DAM_BIT, &hdr->daddr, _daddr);
			pr_debug("dest address unicast link-local %pI6c "
				"iphc1 0x%02x\n", &hdr->daddr, iphc1);
		} else {
			pr_debug("dest address unicast %pI6c\n", &hdr->daddr);
			memcpy(hc_ptr, &hdr->daddr.s6_addr16[0], 16);
			hc_ptr += 16;
		}
	}

	/* UDP header compression */
	if (hdr->nexthdr == UIP_PROTO_UDP)
		compress_udp_header(&hc_ptr, skb);

	head[0] = iphc0;
	head[1] = iphc1;

	skb_pull(skb, sizeof(struct ipv6hdr));
	skb_reset_transport_header(skb);
	memcpy(skb_push(skb, hc_ptr - head), head, hc_ptr - head);
	skb_reset_network_header(skb);

	pr_debug("header len %d skb %u\n", (int)(hc_ptr - head), skb->len);

	raw_dump_table(__func__, "raw skb data dump compressed",
				skb->data, skb->len);
	return 0;
}
EXPORT_SYMBOL_GPL(lowpan_header_compress);

MODULE_LICENSE("GPL");
