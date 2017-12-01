#ifndef __LINUX_ERSPAN_H
#define __LINUX_ERSPAN_H

/*
 * GRE header for ERSPAN encapsulation (8 octets [34:41]) -- 8 bytes
 *       0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |0|0|0|1|0|00000|000000000|00000|    Protocol Type for ERSPAN   |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |      Sequence Number (increments per packet per session)      |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *  Note that in the above GRE header [RFC1701] out of the C, R, K, S,
 *  s, Recur, Flags, Version fields only S (bit 03) is set to 1. The
 *  other fields are set to zero, so only a sequence number follows.
 *
 *  ERSPAN Type II header (8 octets [42:49])
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Ver  |          VLAN         | COS | En|T|    Session ID     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      Reserved         |                  Index                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * GRE proto ERSPAN type II = 0x88BE, type III = 0x22EB
 */

#define ERSPAN_VERSION	0x1

#define VER_MASK	0xf000
#define VLAN_MASK	0x0fff
#define COS_MASK	0xe000
#define EN_MASK		0x1800
#define T_MASK		0x0400
#define ID_MASK		0x03ff
#define INDEX_MASK	0xfffff

enum erspan_encap_type {
	ERSPAN_ENCAP_NOVLAN = 0x0,	/* originally without VLAN tag */
	ERSPAN_ENCAP_ISL = 0x1,		/* originally ISL encapsulated */
	ERSPAN_ENCAP_8021Q = 0x2,	/* originally 802.1Q encapsulated */
	ERSPAN_ENCAP_INFRAME = 0x3,	/* VLAN tag perserved in frame */
};

struct erspan_metadata {
	__be32 index;   /* type II */
};

struct erspanhdr {
	__be16 ver_vlan;
#define VER_OFFSET  12
	__be16 session_id;
#define COS_OFFSET  13
#define EN_OFFSET   11
#define T_OFFSET    10
	struct erspan_metadata md;
};

static inline u8 tos_to_cos(u8 tos)
{
	u8 dscp, cos;

	dscp = tos >> 2;
	cos = dscp >> 3;
	return cos;
}

static inline void erspan_build_header(struct sk_buff *skb,
				__be32 id, u32 index,
				bool truncate, bool is_ipv4)
{
	struct ethhdr *eth = eth_hdr(skb);
	enum erspan_encap_type enc_type;
	struct erspanhdr *ershdr;
	struct qtag_prefix {
		__be16 eth_type;
		__be16 tci;
	} *qp;
	u16 vlan_tci = 0;
	u8 tos;

	tos = is_ipv4 ? ip_hdr(skb)->tos :
			(ipv6_hdr(skb)->priority << 4) +
			(ipv6_hdr(skb)->flow_lbl[0] >> 4);

	enc_type = ERSPAN_ENCAP_NOVLAN;

	/* If mirrored packet has vlan tag, extract tci and
	 *  perserve vlan header in the mirrored frame.
	 */
	if (eth->h_proto == htons(ETH_P_8021Q)) {
		qp = (struct qtag_prefix *)(skb->data + 2 * ETH_ALEN);
		vlan_tci = ntohs(qp->tci);
		enc_type = ERSPAN_ENCAP_INFRAME;
	}

	skb_push(skb, sizeof(*ershdr));
	ershdr = (struct erspanhdr *)skb->data;
	memset(ershdr, 0, sizeof(*ershdr));

	ershdr->ver_vlan = htons((vlan_tci & VLAN_MASK) |
				 (ERSPAN_VERSION << VER_OFFSET));
	ershdr->session_id = htons((u16)(ntohl(id) & ID_MASK) |
			   ((tos_to_cos(tos) << COS_OFFSET) & COS_MASK) |
			   (enc_type << EN_OFFSET & EN_MASK) |
			   ((truncate << T_OFFSET) & T_MASK));
	ershdr->md.index = htonl(index & INDEX_MASK);
}

#endif
