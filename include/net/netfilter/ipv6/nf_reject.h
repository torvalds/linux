#ifndef _IPV6_NF_REJECT_H
#define _IPV6_NF_REJECT_H

#include <linux/icmpv6.h>

static inline void
nf_send_unreach6(struct net *net, struct sk_buff *skb_in, unsigned char code,
	     unsigned int hooknum)
{
	if (hooknum == NF_INET_LOCAL_OUT && skb_in->dev == NULL)
		skb_in->dev = net->loopback_dev;

	icmpv6_send(skb_in, ICMPV6_DEST_UNREACH, code, 0);
}

void nf_send_reset6(struct net *net, struct sk_buff *oldskb, int hook);

#endif /* _IPV6_NF_REJECT_H */
