#ifndef MPLS_INTERNAL_H
#define MPLS_INTERNAL_H

#define LABEL_IPV4_EXPLICIT_NULL	0 /* RFC3032 */
#define LABEL_ROUTER_ALERT_LABEL	1 /* RFC3032 */
#define LABEL_IPV6_EXPLICIT_NULL	2 /* RFC3032 */
#define LABEL_IMPLICIT_NULL		3 /* RFC3032 */
#define LABEL_ENTROPY_INDICATOR		7 /* RFC6790 */
#define LABEL_GAL			13 /* RFC5586 */
#define LABEL_OAM_ALERT			14 /* RFC3429 */
#define LABEL_EXTENSION			15 /* RFC7274 */


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
};

struct sk_buff;

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

int nla_put_labels(struct sk_buff *skb, int attrtype,  u8 labels, const u32 label[]);
int nla_get_labels(const struct nlattr *nla, u32 max_labels, u32 *labels, u32 label[]);

#endif /* MPLS_INTERNAL_H */
