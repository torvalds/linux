#ifndef _NF_NAT_HELPER_H
#define _NF_NAT_HELPER_H
/* NAT protocol helper routines. */

#include <net/netfilter/nf_conntrack.h>

struct sk_buff;

/* These return true or false. */
extern int __nf_nat_mangle_tcp_packet(struct sk_buff *skb,
				      struct nf_conn *ct,
				      enum ip_conntrack_info ctinfo,
				      unsigned int match_offset,
				      unsigned int match_len,
				      const char *rep_buffer,
				      unsigned int rep_len, bool adjust);

static inline int nf_nat_mangle_tcp_packet(struct sk_buff *skb,
					   struct nf_conn *ct,
					   enum ip_conntrack_info ctinfo,
					   unsigned int match_offset,
					   unsigned int match_len,
					   const char *rep_buffer,
					   unsigned int rep_len)
{
	return __nf_nat_mangle_tcp_packet(skb, ct, ctinfo,
					  match_offset, match_len,
					  rep_buffer, rep_len, true);
}

extern int nf_nat_mangle_udp_packet(struct sk_buff *skb,
				    struct nf_conn *ct,
				    enum ip_conntrack_info ctinfo,
				    unsigned int match_offset,
				    unsigned int match_len,
				    const char *rep_buffer,
				    unsigned int rep_len);

extern void nf_nat_set_seq_adjust(struct nf_conn *ct,
				  enum ip_conntrack_info ctinfo,
				  __be32 seq, s16 off);
extern int nf_nat_seq_adjust(struct sk_buff *skb,
			     struct nf_conn *ct,
			     enum ip_conntrack_info ctinfo);
extern int (*nf_nat_seq_adjust_hook)(struct sk_buff *skb,
				     struct nf_conn *ct,
				     enum ip_conntrack_info ctinfo);

/* Setup NAT on this expected conntrack so it follows master, but goes
 * to port ct->master->saved_proto. */
extern void nf_nat_follow_master(struct nf_conn *ct,
				 struct nf_conntrack_expect *this);

extern s16 nf_nat_get_offset(const struct nf_conn *ct,
			     enum ip_conntrack_dir dir,
			     u32 seq);

extern void nf_nat_tcp_seq_adjust(struct sk_buff *skb, struct nf_conn *ct,
				  u32 dir, int off);

#endif
