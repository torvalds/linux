/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_SEQADJ_H
#define _NF_CONNTRACK_SEQADJ_H

#include <net/netfilter/nf_conntrack_extend.h>

/**
 * struct nf_ct_seqadj - sequence number adjustment information
 *
 * @correction_pos: position of the last TCP sequence number modification
 * @offset_before: sequence number offset before last modification
 * @offset_after: sequence number offset after last modification
 */
struct nf_ct_seqadj {
	u32		correction_pos;
	s32		offset_before;
	s32		offset_after;
};

struct nf_conn_seqadj {
	struct nf_ct_seqadj	seq[IP_CT_DIR_MAX];
};

static inline struct nf_conn_seqadj *nfct_seqadj(const struct nf_conn *ct)
{
	return nf_ct_ext_find(ct, NF_CT_EXT_SEQADJ);
}

static inline struct nf_conn_seqadj *nfct_seqadj_ext_add(struct nf_conn *ct)
{
	return nf_ct_ext_add(ct, NF_CT_EXT_SEQADJ, GFP_ATOMIC);
}

int nf_ct_seqadj_init(struct nf_conn *ct, enum ip_conntrack_info ctinfo,
		      s32 off);
int nf_ct_seqadj_set(struct nf_conn *ct, enum ip_conntrack_info ctinfo,
		     __be32 seq, s32 off);
void nf_ct_tcp_seqadj_set(struct sk_buff *skb, struct nf_conn *ct,
			  enum ip_conntrack_info ctinfo, s32 off);

int nf_ct_seq_adjust(struct sk_buff *skb, struct nf_conn *ct,
		     enum ip_conntrack_info ctinfo, unsigned int protoff);
s32 nf_ct_seq_offset(const struct nf_conn *ct, enum ip_conntrack_dir, u32 seq);

#endif /* _NF_CONNTRACK_SEQADJ_H */
