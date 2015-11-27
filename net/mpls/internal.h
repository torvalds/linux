#ifndef MPLS_INTERNAL_H
#define MPLS_INTERNAL_H

struct mpls_shim_hdr {
	__be32 label_stack_entry;
};

struct mpls_entry_decoded {
	u32 label;
	u8 ttl;
	u8 tc;
	u8 bos;
};

struct mpls_dev {
	int			input_enabled;

	struct ctl_table_header *sysctl;
	struct rcu_head		rcu;
};

struct sk_buff;

#define LABEL_NOT_SPECIFIED (1 << 20)
#define MAX_NEW_LABELS 2

/* This maximum ha length copied from the definition of struct neighbour */
#define VIA_ALEN_ALIGN sizeof(unsigned long)
#define MAX_VIA_ALEN (ALIGN(MAX_ADDR_LEN, VIA_ALEN_ALIGN))

enum mpls_payload_type {
	MPT_UNSPEC, /* IPv4 or IPv6 */
	MPT_IPV4 = 4,
	MPT_IPV6 = 6,

	/* Other types not implemented:
	 *  - Pseudo-wire with or without control word (RFC4385)
	 *  - GAL (RFC5586)
	 */
};

struct mpls_nh { /* next hop label forwarding entry */
	struct net_device __rcu *nh_dev;
	u32			nh_label[MAX_NEW_LABELS];
	u8			nh_labels;
	u8			nh_via_alen;
	u8			nh_via_table;
};

/* The route, nexthops and vias are stored together in the same memory
 * block:
 *
 * +----------------------+
 * | mpls_route           |
 * +----------------------+
 * | mpls_nh 0            |
 * +----------------------+
 * | ...                  |
 * +----------------------+
 * | mpls_nh n-1          |
 * +----------------------+
 * | alignment padding    |
 * +----------------------+
 * | via[rt_max_alen] 0   |
 * +----------------------+
 * | ...                  |
 * +----------------------+
 * | via[rt_max_alen] n-1 |
 * +----------------------+
 */
struct mpls_route { /* next hop label forwarding entry */
	struct rcu_head		rt_rcu;
	u8			rt_protocol;
	u8			rt_payload_type;
	u8			rt_max_alen;
	unsigned int		rt_nhn;
	struct mpls_nh		rt_nh[0];
};

#define for_nexthops(rt) {						\
	int nhsel; struct mpls_nh *nh;			\
	for (nhsel = 0, nh = (rt)->rt_nh;				\
	     nhsel < (rt)->rt_nhn;					\
	     nh++, nhsel++)

#define change_nexthops(rt) {						\
	int nhsel; struct mpls_nh *nh;				\
	for (nhsel = 0,	nh = (struct mpls_nh *)((rt)->rt_nh);	\
	     nhsel < (rt)->rt_nhn;					\
	     nh++, nhsel++)

#define endfor_nexthops(rt) }

static inline struct mpls_shim_hdr *mpls_hdr(const struct sk_buff *skb)
{
	return (struct mpls_shim_hdr *)skb_network_header(skb);
}

static inline struct mpls_shim_hdr mpls_entry_encode(u32 label, unsigned ttl, unsigned tc, bool bos)
{
	struct mpls_shim_hdr result;
	result.label_stack_entry =
		cpu_to_be32((label << MPLS_LS_LABEL_SHIFT) |
			    (tc << MPLS_LS_TC_SHIFT) |
			    (bos ? (1 << MPLS_LS_S_SHIFT) : 0) |
			    (ttl << MPLS_LS_TTL_SHIFT));
	return result;
}

static inline struct mpls_entry_decoded mpls_entry_decode(struct mpls_shim_hdr *hdr)
{
	struct mpls_entry_decoded result;
	unsigned entry = be32_to_cpu(hdr->label_stack_entry);

	result.label = (entry & MPLS_LS_LABEL_MASK) >> MPLS_LS_LABEL_SHIFT;
	result.ttl = (entry & MPLS_LS_TTL_MASK) >> MPLS_LS_TTL_SHIFT;
	result.tc =  (entry & MPLS_LS_TC_MASK) >> MPLS_LS_TC_SHIFT;
	result.bos = (entry & MPLS_LS_S_MASK) >> MPLS_LS_S_SHIFT;

	return result;
}

int nla_put_labels(struct sk_buff *skb, int attrtype,  u8 labels,
		   const u32 label[]);
int nla_get_labels(const struct nlattr *nla, u32 max_labels, u8 *labels,
		   u32 label[]);
int nla_get_via(const struct nlattr *nla, u8 *via_alen, u8 *via_table,
		u8 via[]);
bool mpls_output_possible(const struct net_device *dev);
unsigned int mpls_dev_mtu(const struct net_device *dev);
bool mpls_pkt_too_big(const struct sk_buff *skb, unsigned int mtu);

#endif /* MPLS_INTERNAL_H */
