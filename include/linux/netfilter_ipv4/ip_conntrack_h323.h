#ifndef _IP_CONNTRACK_H323_H
#define _IP_CONNTRACK_H323_H

#ifdef __KERNEL__

#include <linux/netfilter_ipv4/ip_conntrack_helper_h323_asn1.h>

#define RAS_PORT 1719
#define Q931_PORT 1720
#define H323_RTP_CHANNEL_MAX 4	/* Audio, video, FAX and other */

/* This structure exists only once per master */
struct ip_ct_h323_master {

	/* Original and NATed Q.931 or H.245 signal ports */
	u_int16_t sig_port[IP_CT_DIR_MAX];

	/* Original and NATed RTP ports */
	u_int16_t rtp_port[H323_RTP_CHANNEL_MAX][IP_CT_DIR_MAX];

	union {
		/* RAS connection timeout */
		u_int32_t timeout;

		/* Next TPKT length (for separate TPKT header and data) */
		u_int16_t tpkt_len[IP_CT_DIR_MAX];
	};
};

struct ip_conntrack_expect;

extern int get_h225_addr(unsigned char *data, TransportAddress * addr,
			 u_int32_t * ip, u_int16_t * port);
extern void ip_conntrack_h245_expect(struct ip_conntrack *new,
				     struct ip_conntrack_expect *this);
extern void ip_conntrack_q931_expect(struct ip_conntrack *new,
				     struct ip_conntrack_expect *this);
extern int (*set_h245_addr_hook) (struct sk_buff ** pskb,
				  unsigned char **data, int dataoff,
				  H245_TransportAddress * addr,
				  u_int32_t ip, u_int16_t port);
extern int (*set_h225_addr_hook) (struct sk_buff ** pskb,
				  unsigned char **data, int dataoff,
				  TransportAddress * addr,
				  u_int32_t ip, u_int16_t port);
extern int (*set_sig_addr_hook) (struct sk_buff ** pskb,
				 struct ip_conntrack * ct,
				 enum ip_conntrack_info ctinfo,
				 unsigned char **data,
				 TransportAddress * addr, int count);
extern int (*set_ras_addr_hook) (struct sk_buff ** pskb,
				 struct ip_conntrack * ct,
				 enum ip_conntrack_info ctinfo,
				 unsigned char **data,
				 TransportAddress * addr, int count);
extern int (*nat_rtp_rtcp_hook) (struct sk_buff ** pskb,
				 struct ip_conntrack * ct,
				 enum ip_conntrack_info ctinfo,
				 unsigned char **data, int dataoff,
				 H245_TransportAddress * addr,
				 u_int16_t port, u_int16_t rtp_port,
				 struct ip_conntrack_expect * rtp_exp,
				 struct ip_conntrack_expect * rtcp_exp);
extern int (*nat_t120_hook) (struct sk_buff ** pskb, struct ip_conntrack * ct,
			     enum ip_conntrack_info ctinfo,
			     unsigned char **data, int dataoff,
			     H245_TransportAddress * addr, u_int16_t port,
			     struct ip_conntrack_expect * exp);
extern int (*nat_h245_hook) (struct sk_buff ** pskb, struct ip_conntrack * ct,
			     enum ip_conntrack_info ctinfo,
			     unsigned char **data, int dataoff,
			     TransportAddress * addr, u_int16_t port,
			     struct ip_conntrack_expect * exp);
extern int (*nat_callforwarding_hook) (struct sk_buff ** pskb,
				       struct ip_conntrack * ct,
				       enum ip_conntrack_info ctinfo,
				       unsigned char **data, int dataoff,
				       TransportAddress * addr,
				       u_int16_t port,
				       struct ip_conntrack_expect * exp);
extern int (*nat_q931_hook) (struct sk_buff ** pskb, struct ip_conntrack * ct,
			     enum ip_conntrack_info ctinfo,
			     unsigned char **data, TransportAddress * addr,
			     int idx, u_int16_t port,
			     struct ip_conntrack_expect * exp);

#endif

#endif
