/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_ESP_H
#define _NET_ESP_H

#include <linux/skbuff.h>

struct ip_esp_hdr;

static inline struct ip_esp_hdr *ip_esp_hdr(const struct sk_buff *skb)
{
	return (struct ip_esp_hdr *)skb_transport_header(skb);
}

static inline void esp_output_fill_trailer(u8 *tail, int tfclen, int plen, __u8 proto)
{
	/* Fill padding... */
	if (tfclen) {
		memset(tail, 0, tfclen);
		tail += tfclen;
	}
	do {
		int i;
		for (i = 0; i < plen - 2; i++)
			tail[i] = i + 1;
	} while (0);
	tail[plen - 2] = plen - 2;
	tail[plen - 1] = proto;
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
