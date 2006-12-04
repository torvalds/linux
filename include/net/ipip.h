#ifndef __NET_IPIP_H
#define __NET_IPIP_H 1

#include <linux/if_tunnel.h>

/* Keep error state on tunnel for 30 sec */
#define IPTUNNEL_ERR_TIMEO	(30*HZ)

struct ip_tunnel
{
	struct ip_tunnel	*next;
	struct net_device	*dev;
	struct net_device_stats	stat;

	int			recursion;	/* Depth of hard_start_xmit recursion */
	int			err_count;	/* Number of arrived ICMP errors */
	unsigned long		err_time;	/* Time when the last ICMP error arrived */

	/* These four fields used only by GRE */
	__u32			i_seqno;	/* The last seen seqno	*/
	__u32			o_seqno;	/* The last output seqno */
	int			hlen;		/* Precalculated GRE header length */
	int			mlink;

	struct ip_tunnel_parm	parms;
};

#define IPTUNNEL_XMIT() do {						\
	int err;							\
	int pkt_len = skb->len;						\
									\
	skb->ip_summed = CHECKSUM_NONE;					\
	iph->tot_len = htons(skb->len);					\
	ip_select_ident(iph, &rt->u.dst, NULL);				\
	ip_send_check(iph);						\
									\
	err = NF_HOOK(PF_INET, NF_IP_LOCAL_OUT, skb, NULL, rt->u.dst.dev, dst_output);\
	if (net_xmit_eval(err) == 0) {					\
		stats->tx_bytes += pkt_len;				\
		stats->tx_packets++;					\
	} else {							\
		stats->tx_errors++;					\
		stats->tx_aborted_errors++;				\
	}								\
} while (0)

#endif
