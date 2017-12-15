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
 *  ERSPAN Version 1 (Type II) header (8 octets [42:49])
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Ver  |          VLAN         | COS | En|T|    Session ID     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      Reserved         |                  Index                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *
 *  ERSPAN Version 2 (Type III) header (12 octets [42:49])
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Ver  |          VLAN         | COS |BSO|T|     Session ID    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Timestamp                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |             SGT               |P|    FT   |   Hw ID   |D|Gra|O|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *      Platform Specific SubHeader (8 octets, optional)
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Platf ID |               Platform Specific Info              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  Platform Specific Info                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * GRE proto ERSPAN type II = 0x88BE, type III = 0x22EB
 */

#define ERSPAN_VERSION	0x1	/* ERSPAN type II */
#define VER_MASK	0xf000
#define VLAN_MASK	0x0fff
#define COS_MASK	0xe000
#define EN_MASK		0x1800
#define T_MASK		0x0400
#define ID_MASK		0x03ff
#define INDEX_MASK	0xfffff

#define ERSPAN_VERSION2	0x2	/* ERSPAN type III*/
#define BSO_MASK	EN_MASK
#define SGT_MASK	0xffff0000
#define P_MASK		0x8000
#define FT_MASK		0x7c00
#define HWID_MASK	0x03f0
#define DIR_MASK	0x0008
#define GRA_MASK	0x0006
#define O_MASK		0x0001

/* ERSPAN version 2 metadata header */
struct erspan_md2 {
	__be32 timestamp;
	__be16 sgt;	/* security group tag */
	__be16 flags;
#define P_OFFSET	15
#define FT_OFFSET	10
#define HWID_OFFSET	4
#define DIR_OFFSET	3
#define GRA_OFFSET	1
};

enum erspan_encap_type {
	ERSPAN_ENCAP_NOVLAN = 0x0,	/* originally without VLAN tag */
	ERSPAN_ENCAP_ISL = 0x1,		/* originally ISL encapsulated */
	ERSPAN_ENCAP_8021Q = 0x2,	/* originally 802.1Q encapsulated */
	ERSPAN_ENCAP_INFRAME = 0x3,	/* VLAN tag perserved in frame */
};

#define ERSPAN_V1_MDSIZE	4
#define ERSPAN_V2_MDSIZE	8
struct erspan_metadata {
	union {
		__be32 index;		/* Version 1 (type II)*/
		struct erspan_md2 md2;	/* Version 2 (type III) */
	} u;
	int version;
};

struct erspan_base_hdr {
	__be16 ver_vlan;
#define VER_OFFSET  12
	__be16 session_id;
#define COS_OFFSET  13
#define EN_OFFSET   11
#define BSO_OFFSET  EN_OFFSET
#define T_OFFSET    10
};

static inline int erspan_hdr_len(int version)
{
	return sizeof(struct erspan_base_hdr) +
	       (version == 1 ? ERSPAN_V1_MDSIZE : ERSPAN_V2_MDSIZE);
}

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
	struct erspan_base_hdr *ershdr;
	struct erspan_metadata *ersmd;
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

	skb_push(skb, sizeof(*ershdr) + ERSPAN_V1_MDSIZE);
	ershdr = (struct erspan_base_hdr *)skb->data;
	memset(ershdr, 0, sizeof(*ershdr) + ERSPAN_V1_MDSIZE);

	/* Build base header */
	ershdr->ver_vlan = htons((vlan_tci & VLAN_MASK) |
				 (ERSPAN_VERSION << VER_OFFSET));
	ershdr->session_id = htons((u16)(ntohl(id) & ID_MASK) |
			   ((tos_to_cos(tos) << COS_OFFSET) & COS_MASK) |
			   (enc_type << EN_OFFSET & EN_MASK) |
			   ((truncate << T_OFFSET) & T_MASK));

	/* Build metadata */
	ersmd = (struct erspan_metadata *)(ershdr + 1);
	ersmd->u.index = htonl(index & INDEX_MASK);
}

/* ERSPAN GRA: timestamp granularity
 *   00b --> granularity = 100 microseconds
 *   01b --> granularity = 100 nanoseconds
 *   10b --> granularity = IEEE 1588
 * Here we only support 100 microseconds.
 */
static inline __be32 erspan_get_timestamp(void)
{
	u64 h_usecs;
	ktime_t kt;

	kt = ktime_get_real();
	h_usecs = ktime_divns(kt, 100 * NSEC_PER_USEC);

	/* ERSPAN base header only has 32-bit,
	 * so it wraps around 4 days.
	 */
	return htonl((u32)h_usecs);
}

static inline void erspan_build_header_v2(struct sk_buff *skb,
					  __be32 id, u8 direction, u16 hwid,
					  bool truncate, bool is_ipv4)
{
	struct ethhdr *eth = eth_hdr(skb);
	struct erspan_base_hdr *ershdr;
	struct erspan_metadata *md;
	struct qtag_prefix {
		__be16 eth_type;
		__be16 tci;
	} *qp;
	u16 vlan_tci = 0;
	u16 session_id;
	u8 gra = 0; /* 100 usec */
	u8 bso = 0; /* Bad/Short/Oversized */
	u8 sgt = 0;
	u8 tos;

	tos = is_ipv4 ? ip_hdr(skb)->tos :
			(ipv6_hdr(skb)->priority << 4) +
			(ipv6_hdr(skb)->flow_lbl[0] >> 4);

	/* Unlike v1, v2 does not have En field,
	 * so only extract vlan tci field.
	 */
	if (eth->h_proto == htons(ETH_P_8021Q)) {
		qp = (struct qtag_prefix *)(skb->data + 2 * ETH_ALEN);
		vlan_tci = ntohs(qp->tci);
	}

	skb_push(skb, sizeof(*ershdr) + ERSPAN_V2_MDSIZE);
	ershdr = (struct erspan_base_hdr *)skb->data;
	memset(ershdr, 0, sizeof(*ershdr) + ERSPAN_V2_MDSIZE);

	/* Build base header */
	ershdr->ver_vlan = htons((vlan_tci & VLAN_MASK) |
				 (ERSPAN_VERSION2 << VER_OFFSET));
	session_id = (u16)(ntohl(id) & ID_MASK) |
		     ((tos_to_cos(tos) << COS_OFFSET) & COS_MASK) |
		     (bso << BSO_OFFSET & BSO_MASK) |
		     ((truncate << T_OFFSET) & T_MASK);
	ershdr->session_id = htons(session_id);

	/* Build metadata */
	md = (struct erspan_metadata *)(ershdr + 1);
	md->u.md2.timestamp = erspan_get_timestamp();
	md->u.md2.sgt = htons(sgt);
	md->u.md2.flags = htons(((1 << P_OFFSET) & P_MASK) |
				((hwid << HWID_OFFSET) & HWID_MASK) |
				((direction << DIR_OFFSET) & DIR_MASK) |
				((gra << GRA_OFFSET) & GRA_MASK));
}

#endif
