#ifndef _NET_IPCOMP_H
#define _NET_IPCOMP_H

#include <linux/types.h>

#define IPCOMP_SCRATCH_SIZE     65400

struct crypto_comp;

struct ipcomp_data {
	u16 threshold;
	struct crypto_comp * __percpu *tfms;
};

struct ip_comp_hdr;
struct sk_buff;
struct xfrm_state;

int ipcomp_input(struct xfrm_state *x, struct sk_buff *skb);
int ipcomp_output(struct xfrm_state *x, struct sk_buff *skb);
void ipcomp_destroy(struct xfrm_state *x);
int ipcomp_init_state(struct xfrm_state *x);

static inline struct ip_comp_hdr *ip_comp_hdr(const struct sk_buff *skb)
{
	return (struct ip_comp_hdr *)skb_transport_header(skb);
}

#endif
