// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <net/tso.h>
#include <linux/unaligned.h>

void tso_build_hdr(const struct sk_buff *skb, char *hdr, struct tso_t *tso,
		   int size, bool is_last)
{
	int hdr_len = skb_transport_offset(skb) + tso->tlen;
	int mac_hdr_len = skb_network_offset(skb);

	memcpy(hdr, skb->data, hdr_len);
	if (!tso->ipv6) {
		struct iphdr *iph = (void *)(hdr + mac_hdr_len);

		iph->id = htons(tso->ip_id);
		iph->tot_len = htons(size + hdr_len - mac_hdr_len);
		tso->ip_id++;
	} else {
		struct ipv6hdr *iph = (void *)(hdr + mac_hdr_len);

		iph->payload_len = htons(size + tso->tlen);
	}
	hdr += skb_transport_offset(skb);
	if (tso->tlen != sizeof(struct udphdr)) {
		struct tcphdr *tcph = (struct tcphdr *)hdr;

		put_unaligned_be32(tso->tcp_seq, &tcph->seq);

		if (!is_last) {
			/* Clear all special flags for not last packet */
			tcph->psh = 0;
			tcph->fin = 0;
			tcph->rst = 0;
		}
	} else {
		struct udphdr *uh = (struct udphdr *)hdr;

		uh->len = htons(sizeof(*uh) + size);
	}
}
EXPORT_SYMBOL(tso_build_hdr);

void tso_build_data(const struct sk_buff *skb, struct tso_t *tso, int size)
{
	tso->tcp_seq += size; /* not worth avoiding this operation for UDP */
	tso->size -= size;
	tso->data += size;

	if ((tso->size == 0) &&
	    (tso->next_frag_idx < skb_shinfo(skb)->nr_frags)) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[tso->next_frag_idx];

		/* Move to next segment */
		tso->size = skb_frag_size(frag);
		tso->data = skb_frag_address(frag);
		tso->next_frag_idx++;
	}
}
EXPORT_SYMBOL(tso_build_data);

int tso_start(struct sk_buff *skb, struct tso_t *tso)
{
	int tlen = skb_is_gso_tcp(skb) ? tcp_hdrlen(skb) : sizeof(struct udphdr);
	int hdr_len = skb_transport_offset(skb) + tlen;

	tso->tlen = tlen;
	tso->ip_id = ntohs(ip_hdr(skb)->id);
	tso->tcp_seq = (tlen != sizeof(struct udphdr)) ? ntohl(tcp_hdr(skb)->seq) : 0;
	tso->next_frag_idx = 0;
	tso->ipv6 = vlan_get_protocol(skb) == htons(ETH_P_IPV6);

	/* Build first data */
	tso->size = skb_headlen(skb) - hdr_len;
	tso->data = skb->data + hdr_len;
	if ((tso->size == 0) &&
	    (tso->next_frag_idx < skb_shinfo(skb)->nr_frags)) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[tso->next_frag_idx];

		/* Move to next segment */
		tso->size = skb_frag_size(frag);
		tso->data = skb_frag_address(frag);
		tso->next_frag_idx++;
	}
	return hdr_len;
}
EXPORT_SYMBOL(tso_start);
