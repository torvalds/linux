/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_H323_H
#define _NF_CONNTRACK_H323_H

#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/netfilter/nf_conntrack_h323_asn1.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <uapi/linux/netfilter/nf_conntrack_tuple_common.h>

#define RAS_PORT 1719
#define Q931_PORT 1720
#define H323_RTP_CHANNEL_MAX 4	/* Audio, video, FAX and other */

/* This structure exists only once per master */
struct nf_ct_h323_master {

	/* Original and NATed Q.931 or H.245 signal ports */
	__be16 sig_port[IP_CT_DIR_MAX];

	/* Original and NATed RTP ports */
	__be16 rtp_port[H323_RTP_CHANNEL_MAX][IP_CT_DIR_MAX];

	union {
		/* RAS connection timeout */
		u_int32_t timeout;

		/* Next TPKT length (for separate TPKT header and data) */
		u_int16_t tpkt_len[IP_CT_DIR_MAX];
	};
};

int get_h225_addr(struct nf_conn *ct, unsigned char *data,
		  TransportAddress *taddr, union nf_inet_addr *addr,
		  __be16 *port);

struct nfct_h323_nat_hooks {
	int (*set_h245_addr)(struct sk_buff *skb, unsigned int protoff,
			     unsigned char **data, int dataoff,
			     H245_TransportAddress *taddr,
			     union nf_inet_addr *addr, __be16 port);
	int (*set_h225_addr)(struct sk_buff *skb, unsigned int protoff,
			     unsigned char **data, int dataoff,
			     TransportAddress *taddr,
			     union nf_inet_addr *addr, __be16 port);
	int (*set_sig_addr)(struct sk_buff *skb,
			    struct nf_conn *ct,
			    enum ip_conntrack_info ctinfo,
			    unsigned int protoff, unsigned char **data,
			    TransportAddress *taddr, int count);
	int (*set_ras_addr)(struct sk_buff *skb,
			    struct nf_conn *ct,
			    enum ip_conntrack_info ctinfo,
			    unsigned int protoff, unsigned char **data,
			    TransportAddress *taddr, int count);
	int (*nat_rtp_rtcp)(struct sk_buff *skb,
			    struct nf_conn *ct,
			    enum ip_conntrack_info ctinfo,
			    unsigned int protoff,
			    unsigned char **data, int dataoff,
			    H245_TransportAddress *taddr,
			    __be16 port, __be16 rtp_port,
			    struct nf_conntrack_expect *rtp_exp,
			    struct nf_conntrack_expect *rtcp_exp);
	int (*nat_t120)(struct sk_buff *skb,
			struct nf_conn *ct,
			enum ip_conntrack_info ctinfo,
			unsigned int protoff,
			unsigned char **data, int dataoff,
			H245_TransportAddress *taddr, __be16 port,
			struct nf_conntrack_expect *exp);
	int (*nat_h245)(struct sk_buff *skb,
			struct nf_conn *ct,
			enum ip_conntrack_info ctinfo,
			unsigned int protoff,
			unsigned char **data, int dataoff,
			TransportAddress *taddr, __be16 port,
			struct nf_conntrack_expect *exp);
	int (*nat_callforwarding)(struct sk_buff *skb,
				  struct nf_conn *ct,
				  enum ip_conntrack_info ctinfo,
				  unsigned int protoff,
				  unsigned char **data, int dataoff,
				  TransportAddress *taddr, __be16 port,
				  struct nf_conntrack_expect *exp);
	int (*nat_q931)(struct sk_buff *skb,
			struct nf_conn *ct,
			enum ip_conntrack_info ctinfo,
			unsigned int protoff,
			unsigned char **data, TransportAddress *taddr, int idx,
			__be16 port, struct nf_conntrack_expect *exp);
};
extern const struct nfct_h323_nat_hooks __rcu *nfct_h323_nat_hook;

#endif
