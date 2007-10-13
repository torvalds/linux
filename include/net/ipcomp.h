#ifndef _NET_IPCOMP_H
#define _NET_IPCOMP_H

#include <linux/types.h>

#define IPCOMP_SCRATCH_SIZE     65400

struct crypto_comp;

struct ipcomp_data {
	u16 threshold;
	struct crypto_comp **tfms;
};

struct ip_comp_hdr;
struct sk_buff;

static inline struct ip_comp_hdr *ip_comp_hdr(const struct sk_buff *skb)
{
	return (struct ip_comp_hdr *)skb_transport_header(skb);
}

#endif
