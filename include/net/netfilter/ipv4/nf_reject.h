#ifndef _IPV4_NF_REJECT_H
#define _IPV4_NF_REJECT_H

#include <net/icmp.h>

static inline void nf_send_unreach(struct sk_buff *skb_in, int code)
{
	icmp_send(skb_in, ICMP_DEST_UNREACH, code, 0);
}

void nf_send_reset(struct sk_buff *oldskb, int hook);

#endif /* _IPV4_NF_REJECT_H */
