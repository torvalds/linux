/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_NAT_HELPER_H
#define _NF_NAT_HELPER_H
/* NAT protocol helper routines. */

#include <linux/skbuff.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_expect.h>

/* These return true or false. */
bool __nf_nat_mangle_tcp_packet(struct sk_buff *skb, struct nf_conn *ct,
				enum ip_conntrack_info ctinfo,
				unsigned int protoff, unsigned int match_offset,
				unsigned int match_len, const char *rep_buffer,
				unsigned int rep_len, bool adjust);

static inline bool nf_nat_mangle_tcp_packet(struct sk_buff *skb,
					    struct nf_conn *ct,
					    enum ip_conntrack_info ctinfo,
					    unsigned int protoff,
					    unsigned int match_offset,
					    unsigned int match_len,
					    const char *rep_buffer,
					    unsigned int rep_len)
{
	return __nf_nat_mangle_tcp_packet(skb, ct, ctinfo, protoff,
					  match_offset, match_len,
					  rep_buffer, rep_len, true);
}

bool nf_nat_mangle_udp_packet(struct sk_buff *skb, struct nf_conn *ct,
			      enum ip_conntrack_info ctinfo,
			      unsigned int protoff, unsigned int match_offset,
			      unsigned int match_len, const char *rep_buffer,
			      unsigned int rep_len);

/* Setup NAT on this expected conntrack so it follows master, but goes
 * to port ct->master->saved_proto. */
void nf_nat_follow_master(struct nf_conn *ct, struct nf_conntrack_expect *this);

#endif
