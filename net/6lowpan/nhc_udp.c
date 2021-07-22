// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	6LoWPAN IPv6 UDP compression according to RFC6282
 *
 *	Authors:
 *	Alexander Aring	<aar@pengutronix.de>
 *
 *	Original written by:
 *	Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 *	Jon Smirl <jonsmirl@gmail.com>
 */

#include "nhc.h"

#define LOWPAN_NHC_UDP_MASK		0xF8
#define LOWPAN_NHC_UDP_ID		0xF0
#define LOWPAN_NHC_UDP_IDLEN		1

#define LOWPAN_NHC_UDP_4BIT_PORT	0xF0B0
#define LOWPAN_NHC_UDP_4BIT_MASK	0xFFF0
#define LOWPAN_NHC_UDP_8BIT_PORT	0xF000
#define LOWPAN_NHC_UDP_8BIT_MASK	0xFF00

/* values for port compression, _with checksum_ ie bit 5 set to 0 */

/* all inline */
#define LOWPAN_NHC_UDP_CS_P_00	0xF0
/* source 16bit inline, dest = 0xF0 + 8 bit inline */
#define LOWPAN_NHC_UDP_CS_P_01	0xF1
/* source = 0xF0 + 8bit inline, dest = 16 bit inline */
#define LOWPAN_NHC_UDP_CS_P_10	0xF2
/* source & dest = 0xF0B + 4bit inline */
#define LOWPAN_NHC_UDP_CS_P_11	0xF3
/* checksum elided */
#define LOWPAN_NHC_UDP_CS_C	0x04

static int udp_uncompress(struct sk_buff *skb, size_t needed)
{
	u8 tmp = 0, val = 0;
	struct udphdr uh;
	bool fail;
	int err;

	fail = lowpan_fetch_skb(skb, &tmp, sizeof(tmp));

	pr_debug("UDP header uncompression\n");
	switch (tmp & LOWPAN_NHC_UDP_CS_P_11) {
	case LOWPAN_NHC_UDP_CS_P_00:
		fail |= lowpan_fetch_skb(skb, &uh.source, sizeof(uh.source));
		fail |= lowpan_fetch_skb(skb, &uh.dest, sizeof(uh.dest));
		break;
	case LOWPAN_NHC_UDP_CS_P_01:
		fail |= lowpan_fetch_skb(skb, &uh.source, sizeof(uh.source));
		fail |= lowpan_fetch_skb(skb, &val, sizeof(val));
		uh.dest = htons(val + LOWPAN_NHC_UDP_8BIT_PORT);
		break;
	case LOWPAN_NHC_UDP_CS_P_10:
		fail |= lowpan_fetch_skb(skb, &val, sizeof(val));
		uh.source = htons(val + LOWPAN_NHC_UDP_8BIT_PORT);
		fail |= lowpan_fetch_skb(skb, &uh.dest, sizeof(uh.dest));
		break;
	case LOWPAN_NHC_UDP_CS_P_11:
		fail |= lowpan_fetch_skb(skb, &val, sizeof(val));
		uh.source = htons(LOWPAN_NHC_UDP_4BIT_PORT + (val >> 4));
		uh.dest = htons(LOWPAN_NHC_UDP_4BIT_PORT + (val & 0x0f));
		break;
	default:
		BUG();
	}

	pr_debug("uncompressed UDP ports: src = %d, dst = %d\n",
		 ntohs(uh.source), ntohs(uh.dest));

	/* checksum */
	if (tmp & LOWPAN_NHC_UDP_CS_C) {
		pr_debug_ratelimited("checksum elided currently not supported\n");
		fail = true;
	} else {
		fail |= lowpan_fetch_skb(skb, &uh.check, sizeof(uh.check));
	}

	if (fail)
		return -EINVAL;

	/* UDP length needs to be inferred from the lower layers
	 * here, we obtain the hint from the remaining size of the
	 * frame
	 */
	switch (lowpan_dev(skb->dev)->lltype) {
	case LOWPAN_LLTYPE_IEEE802154:
		if (lowpan_802154_cb(skb)->d_size)
			uh.len = htons(lowpan_802154_cb(skb)->d_size -
				       sizeof(struct ipv6hdr));
		else
			uh.len = htons(skb->len + sizeof(struct udphdr));
		break;
	default:
		uh.len = htons(skb->len + sizeof(struct udphdr));
		break;
	}
	pr_debug("uncompressed UDP length: src = %d", ntohs(uh.len));

	/* replace the compressed UDP head by the uncompressed UDP
	 * header
	 */
	err = skb_cow(skb, needed);
	if (unlikely(err))
		return err;

	skb_push(skb, sizeof(struct udphdr));
	skb_copy_to_linear_data(skb, &uh, sizeof(struct udphdr));

	return 0;
}

static int udp_compress(struct sk_buff *skb, u8 **hc_ptr)
{
	const struct udphdr *uh = udp_hdr(skb);
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

	return 0;
}

static void udp_nhid_setup(struct lowpan_nhc *nhc)
{
	nhc->id[0] = LOWPAN_NHC_UDP_ID;
	nhc->idmask[0] = LOWPAN_NHC_UDP_MASK;
}

LOWPAN_NHC(nhc_udp, "RFC6282 UDP", NEXTHDR_UDP, sizeof(struct udphdr),
	   udp_nhid_setup, LOWPAN_NHC_UDP_IDLEN, udp_uncompress, udp_compress);

module_lowpan_nhc(nhc_udp);
MODULE_DESCRIPTION("6LoWPAN next header RFC6282 UDP compression");
MODULE_LICENSE("GPL");
