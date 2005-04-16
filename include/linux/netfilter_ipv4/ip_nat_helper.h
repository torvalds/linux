#ifndef _IP_NAT_HELPER_H
#define _IP_NAT_HELPER_H
/* NAT protocol helper routines. */

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/module.h>

struct sk_buff;

/* These return true or false. */
extern int ip_nat_mangle_tcp_packet(struct sk_buff **skb,
				struct ip_conntrack *ct,
				enum ip_conntrack_info ctinfo,
				unsigned int match_offset,
				unsigned int match_len,
				const char *rep_buffer,
				unsigned int rep_len);
extern int ip_nat_mangle_udp_packet(struct sk_buff **skb,
				struct ip_conntrack *ct,
				enum ip_conntrack_info ctinfo,
				unsigned int match_offset,
				unsigned int match_len,
				const char *rep_buffer,
				unsigned int rep_len);
extern int ip_nat_seq_adjust(struct sk_buff **pskb, 
			     struct ip_conntrack *ct, 
			     enum ip_conntrack_info ctinfo);

/* Setup NAT on this expected conntrack so it follows master, but goes
 * to port ct->master->saved_proto. */
extern void ip_nat_follow_master(struct ip_conntrack *ct,
				 struct ip_conntrack_expect *this);
#endif
