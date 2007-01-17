#ifndef _NF_NAT_HELPER_H
#define _NF_NAT_HELPER_H
/* NAT protocol helper routines. */

#include <net/netfilter/nf_conntrack.h>

struct sk_buff;

/* These return true or false. */
extern int nf_nat_mangle_tcp_packet(struct sk_buff **skb,
				    struct nf_conn *ct,
				    enum ip_conntrack_info ctinfo,
				    unsigned int match_offset,
				    unsigned int match_len,
				    const char *rep_buffer,
				    unsigned int rep_len);
extern int nf_nat_mangle_udp_packet(struct sk_buff **skb,
				    struct nf_conn *ct,
				    enum ip_conntrack_info ctinfo,
				    unsigned int match_offset,
				    unsigned int match_len,
				    const char *rep_buffer,
				    unsigned int rep_len);
extern int nf_nat_seq_adjust(struct sk_buff **pskb,
			     struct nf_conn *ct,
			     enum ip_conntrack_info ctinfo);

/* Setup NAT on this expected conntrack so it follows master, but goes
 * to port ct->master->saved_proto. */
extern void nf_nat_follow_master(struct nf_conn *ct,
				 struct nf_conntrack_expect *this);
#endif
