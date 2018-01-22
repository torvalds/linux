/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_ESP_H
#define _NET_ESP_H

#include <linux/skbuff.h>

struct ip_esp_hdr;

static inline struct ip_esp_hdr *ip_esp_hdr(const struct sk_buff *skb)
{
	return (struct ip_esp_hdr *)skb_transport_header(skb);
}

struct esp_info {
	struct	ip_esp_hdr *esph;
	__be64	seqno;
	int	tfclen;
	int	tailen;
	int	plen;
	int	clen;
	int 	len;
	int 	nfrags;
	__u8	proto;
	bool	inplace;
};

int esp_output_head(struct xfrm_state *x, struct sk_buff *skb, struct esp_info *esp);
int esp_output_tail(struct xfrm_state *x, struct sk_buff *skb, struct esp_info *esp);
int esp_input_done2(struct sk_buff *skb, int err);
int esp6_output_head(struct xfrm_state *x, struct sk_buff *skb, struct esp_info *esp);
int esp6_output_tail(struct xfrm_state *x, struct sk_buff *skb, struct esp_info *esp);
int esp6_input_done2(struct sk_buff *skb, int err);
#endif
