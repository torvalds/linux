#ifndef __NET_IPIP_H
#define __NET_IPIP_H 1

#include <linux/if_tunnel.h>
#include <net/ip.h>

/* Keep error state on tunnel for 30 sec */
#define IPTUNNEL_ERR_TIMEO	(30*HZ)

struct ip_tunnel
{
	struct ip_tunnel	*next;
	struct net_device	*dev;

	int			recursion;	/* Depth of hard_start_xmit recursion */
	int			err_count;	/* Number of arrived ICMP errors */
	unsigned long		err_time;	/* Time when the last ICMP error arrived */

	/* These four fields used only by GRE */
	__u32			i_seqno;	/* The last seen seqno	*/
	__u32			o_seqno;	/* The last output seqno */
	int			hlen;		/* Precalculated GRE header length */
	int			mlink;

	struct ip_tunnel_parm	parms;

	struct ip_tunnel_prl_entry	*prl;		/* potential router list */
	unsigned int			prl_count;	/* # of entries in PRL */
};

struct ip_tunnel_prl_entry
{
	struct ip_tunnel_prl_entry	*next;
	__be32				addr;
	u16				flags;
};

#define IPTUNNEL_XMIT() do {						\
	int err;							\
	int pkt_len = skb->len;						\
									\
	skb->ip_summed = CHECKSUM_NONE;					\
	ip_select_ident(iph, &rt->u.dst, NULL);				\
									\
	err = ip_local_out(skb);					\
	if (net_xmit_eval(err) == 0) {					\
		stats->tx_bytes += pkt_len;				\
		stats->tx_packets++;					\
	} else {							\
		stats->tx_errors++;					\
		stats->tx_aborted_errors++;				\
	}								\
} while (0)

#endif
