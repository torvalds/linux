#include <linux/export.h>
#include <net/ip.h>
#include <net/tso.h>

/* Calculate expected number of TX descriptors */
int tso_count_descs(struct sk_buff *skb)
{
	/* The Marvell Way */
	return skb_shinfo(skb)->gso_segs * 2 + skb_shinfo(skb)->nr_frags;
}
EXPORT_SYMBOL(tso_count_descs);

void tso_build_hdr(struct sk_buff *skb, char *hdr, struct tso_t *tso,
		   int size, bool is_last)
{
	struct iphdr *iph;
	struct tcphdr *tcph;
	int hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
	int mac_hdr_len = skb_network_offset(skb);

	memcpy(hdr, skb->data, hdr_len);
	iph = (struct iphdr *)(hdr + mac_hdr_len);
	iph->id = htons(tso->ip_id);
	iph->tot_len = htons(size + hdr_len - mac_hdr_len);
	tcph = (struct tcphdr *)(hdr + skb_transport_offset(skb));
	tcph->seq = htonl(tso->tcp_seq);
	tso->ip_id++;

	if (!is_last) {
		/* Clear all special flags for not last packet */
		tcph->psh = 0;
		tcph->fin = 0;
		tcph->rst = 0;
	}
}
EXPORT_SYMBOL(tso_build_hdr);

void tso_build_data(struct sk_buff *skb, struct tso_t *tso, int size)
{
	tso->tcp_seq += size;
	tso->size -= size;
	tso->data += size;

	if ((tso->size == 0) &&
	    (tso->next_frag_idx < skb_shinfo(skb)->nr_frags)) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[tso->next_frag_idx];

		/* Move to next segment */
		tso->size = frag->size;
		tso->data = page_address(frag->page.p) + frag->page_offset;
		tso->next_frag_idx++;
	}
}
EXPORT_SYMBOL(tso_build_data);

void tso_start(struct sk_buff *skb, struct tso_t *tso)
{
	int hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);

	tso->ip_id = ntohs(ip_hdr(skb)->id);
	tso->tcp_seq = ntohl(tcp_hdr(skb)->seq);
	tso->next_frag_idx = 0;

	/* Build first data */
	tso->size = skb_headlen(skb) - hdr_len;
	tso->data = skb->data + hdr_len;
	if ((tso->size == 0) &&
	    (tso->next_frag_idx < skb_shinfo(skb)->nr_frags)) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[tso->next_frag_idx];

		/* Move to next segment */
		tso->size = frag->size;
		tso->data = page_address(frag->page.p) + frag->page_offset;
		tso->next_frag_idx++;
	}
}
EXPORT_SYMBOL(tso_start);
