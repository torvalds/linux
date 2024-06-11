// SPDX-License-Identifier: GPL-2.0-only
/*
 * IPV4 GSO/GRO offload support
 * Linux INET implementation
 *
 * Copyright (C) 2016 secunet Security Networks AG
 * Author: Steffen Klassert <steffen.klassert@secunet.com>
 *
 * ESP GRO support
 */

#include <linux/skbuff.h>
#include <linux/init.h>
#include <net/protocol.h>
#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <linux/err.h>
#include <linux/module.h>
#include <net/gro.h>
#include <net/gso.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/esp.h>
#include <linux/scatterlist.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <net/udp.h>

static struct sk_buff *esp4_gro_receive(struct list_head *head,
					struct sk_buff *skb)
{
	int offset = skb_gro_offset(skb);
	struct xfrm_offload *xo;
	struct xfrm_state *x;
	int encap_type = 0;
	__be32 seq;
	__be32 spi;

	if (!pskb_pull(skb, offset))
		return NULL;

	if (xfrm_parse_spi(skb, IPPROTO_ESP, &spi, &seq) != 0)
		goto out;

	xo = xfrm_offload(skb);
	if (!xo || !(xo->flags & CRYPTO_DONE)) {
		struct sec_path *sp = secpath_set(skb);

		if (!sp)
			goto out;

		if (sp->len == XFRM_MAX_DEPTH)
			goto out_reset;

		x = xfrm_state_lookup(dev_net(skb->dev), skb->mark,
				      (xfrm_address_t *)&ip_hdr(skb)->daddr,
				      spi, IPPROTO_ESP, AF_INET);

		if (unlikely(x && x->dir && x->dir != XFRM_SA_DIR_IN)) {
			/* non-offload path will record the error and audit log */
			xfrm_state_put(x);
			x = NULL;
		}

		if (!x)
			goto out_reset;

		skb->mark = xfrm_smark_get(skb->mark, x);

		sp->xvec[sp->len++] = x;
		sp->olen++;

		xo = xfrm_offload(skb);
		if (!xo)
			goto out_reset;
	}

	xo->flags |= XFRM_GRO;

	if (NAPI_GRO_CB(skb)->proto == IPPROTO_UDP)
		encap_type = UDP_ENCAP_ESPINUDP;

	XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip4 = NULL;
	XFRM_SPI_SKB_CB(skb)->family = AF_INET;
	XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct iphdr, daddr);
	XFRM_SPI_SKB_CB(skb)->seq = seq;

	/* We don't need to handle errors from xfrm_input, it does all
	 * the error handling and frees the resources on error. */
	xfrm_input(skb, IPPROTO_ESP, spi, encap_type);

	return ERR_PTR(-EINPROGRESS);
out_reset:
	secpath_reset(skb);
out:
	skb_push(skb, offset);
	NAPI_GRO_CB(skb)->same_flow = 0;
	NAPI_GRO_CB(skb)->flush = 1;

	return NULL;
}

static void esp4_gso_encap(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ip_esp_hdr *esph;
	struct iphdr *iph = ip_hdr(skb);
	struct xfrm_offload *xo = xfrm_offload(skb);
	int proto = iph->protocol;

	skb_push(skb, -skb_network_offset(skb));
	esph = ip_esp_hdr(skb);
	*skb_mac_header(skb) = IPPROTO_ESP;

	esph->spi = x->id.spi;
	esph->seq_no = htonl(XFRM_SKB_CB(skb)->seq.output.low);

	xo->proto = proto;
}

static struct sk_buff *xfrm4_tunnel_gso_segment(struct xfrm_state *x,
						struct sk_buff *skb,
						netdev_features_t features)
{
	__be16 type = x->inner_mode.family == AF_INET6 ? htons(ETH_P_IPV6)
						       : htons(ETH_P_IP);

	return skb_eth_gso_segment(skb, features, type);
}

static struct sk_buff *xfrm4_transport_gso_segment(struct xfrm_state *x,
						   struct sk_buff *skb,
						   netdev_features_t features)
{
	const struct net_offload *ops;
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	struct xfrm_offload *xo = xfrm_offload(skb);

	skb->transport_header += x->props.header_len;
	ops = rcu_dereference(inet_offloads[xo->proto]);
	if (likely(ops && ops->callbacks.gso_segment))
		segs = ops->callbacks.gso_segment(skb, features);

	return segs;
}

static struct sk_buff *xfrm4_beet_gso_segment(struct xfrm_state *x,
					      struct sk_buff *skb,
					      netdev_features_t features)
{
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	const struct net_offload *ops;
	u8 proto = xo->proto;

	skb->transport_header += x->props.header_len;

	if (x->sel.family != AF_INET6) {
		if (proto == IPPROTO_BEETPH) {
			struct ip_beet_phdr *ph =
				(struct ip_beet_phdr *)skb->data;

			skb->transport_header += ph->hdrlen * 8;
			proto = ph->nexthdr;
		} else {
			skb->transport_header -= IPV4_BEET_PHMAXLEN;
		}
	} else {
		__be16 frag;

		skb->transport_header +=
			ipv6_skip_exthdr(skb, 0, &proto, &frag);
		if (proto == IPPROTO_TCP)
			skb_shinfo(skb)->gso_type |= SKB_GSO_TCPV4;
	}

	if (proto == IPPROTO_IPV6)
		skb_shinfo(skb)->gso_type |= SKB_GSO_IPXIP4;

	__skb_pull(skb, skb_transport_offset(skb));
	ops = rcu_dereference(inet_offloads[proto]);
	if (likely(ops && ops->callbacks.gso_segment))
		segs = ops->callbacks.gso_segment(skb, features);

	return segs;
}

static struct sk_buff *xfrm4_outer_mode_gso_segment(struct xfrm_state *x,
						    struct sk_buff *skb,
						    netdev_features_t features)
{
	switch (x->outer_mode.encap) {
	case XFRM_MODE_TUNNEL:
		return xfrm4_tunnel_gso_segment(x, skb, features);
	case XFRM_MODE_TRANSPORT:
		return xfrm4_transport_gso_segment(x, skb, features);
	case XFRM_MODE_BEET:
		return xfrm4_beet_gso_segment(x, skb, features);
	}

	return ERR_PTR(-EOPNOTSUPP);
}

static struct sk_buff *esp4_gso_segment(struct sk_buff *skb,
				        netdev_features_t features)
{
	struct xfrm_state *x;
	struct ip_esp_hdr *esph;
	struct crypto_aead *aead;
	netdev_features_t esp_features = features;
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct sec_path *sp;

	if (!xo)
		return ERR_PTR(-EINVAL);

	if (!(skb_shinfo(skb)->gso_type & SKB_GSO_ESP))
		return ERR_PTR(-EINVAL);

	sp = skb_sec_path(skb);
	x = sp->xvec[sp->len - 1];
	aead = x->data;
	esph = ip_esp_hdr(skb);

	if (esph->spi != x->id.spi)
		return ERR_PTR(-EINVAL);

	if (!pskb_may_pull(skb, sizeof(*esph) + crypto_aead_ivsize(aead)))
		return ERR_PTR(-EINVAL);

	__skb_pull(skb, sizeof(*esph) + crypto_aead_ivsize(aead));

	skb->encap_hdr_csum = 1;

	if ((!(skb->dev->gso_partial_features & NETIF_F_HW_ESP) &&
	     !(features & NETIF_F_HW_ESP)) || x->xso.dev != skb->dev)
		esp_features = features & ~(NETIF_F_SG | NETIF_F_CSUM_MASK |
					    NETIF_F_SCTP_CRC);
	else if (!(features & NETIF_F_HW_ESP_TX_CSUM) &&
		 !(skb->dev->gso_partial_features & NETIF_F_HW_ESP_TX_CSUM))
		esp_features = features & ~(NETIF_F_CSUM_MASK |
					    NETIF_F_SCTP_CRC);

	xo->flags |= XFRM_GSO_SEGMENT;

	return xfrm4_outer_mode_gso_segment(x, skb, esp_features);
}

static int esp_input_tail(struct xfrm_state *x, struct sk_buff *skb)
{
	struct crypto_aead *aead = x->data;
	struct xfrm_offload *xo = xfrm_offload(skb);

	if (!pskb_may_pull(skb, sizeof(struct ip_esp_hdr) + crypto_aead_ivsize(aead)))
		return -EINVAL;

	if (!(xo->flags & CRYPTO_DONE))
		skb->ip_summed = CHECKSUM_NONE;

	return esp_input_done2(skb, 0);
}

static int esp_xmit(struct xfrm_state *x, struct sk_buff *skb,  netdev_features_t features)
{
	int err;
	int alen;
	int blksize;
	struct xfrm_offload *xo;
	struct ip_esp_hdr *esph;
	struct crypto_aead *aead;
	struct esp_info esp;
	bool hw_offload = true;
	__u32 seq;

	esp.inplace = true;

	xo = xfrm_offload(skb);

	if (!xo)
		return -EINVAL;

	if ((!(features & NETIF_F_HW_ESP) &&
	     !(skb->dev->gso_partial_features & NETIF_F_HW_ESP)) ||
	    x->xso.dev != skb->dev) {
		xo->flags |= CRYPTO_FALLBACK;
		hw_offload = false;
	}

	esp.proto = xo->proto;

	/* skb is pure payload to encrypt */

	aead = x->data;
	alen = crypto_aead_authsize(aead);

	esp.tfclen = 0;
	/* XXX: Add support for tfc padding here. */

	blksize = ALIGN(crypto_aead_blocksize(aead), 4);
	esp.clen = ALIGN(skb->len + 2 + esp.tfclen, blksize);
	esp.plen = esp.clen - skb->len - esp.tfclen;
	esp.tailen = esp.tfclen + esp.plen + alen;

	esp.esph = ip_esp_hdr(skb);


	if (!hw_offload || !skb_is_gso(skb)) {
		esp.nfrags = esp_output_head(x, skb, &esp);
		if (esp.nfrags < 0)
			return esp.nfrags;
	}

	seq = xo->seq.low;

	esph = esp.esph;
	esph->spi = x->id.spi;

	skb_push(skb, -skb_network_offset(skb));

	if (xo->flags & XFRM_GSO_SEGMENT) {
		esph->seq_no = htonl(seq);

		if (!skb_is_gso(skb))
			xo->seq.low++;
		else
			xo->seq.low += skb_shinfo(skb)->gso_segs;
	}

	if (xo->seq.low < seq)
		xo->seq.hi++;

	esp.seqno = cpu_to_be64(seq + ((u64)xo->seq.hi << 32));

	ip_hdr(skb)->tot_len = htons(skb->len);
	ip_send_check(ip_hdr(skb));

	if (hw_offload) {
		if (!skb_ext_add(skb, SKB_EXT_SEC_PATH))
			return -ENOMEM;

		xo = xfrm_offload(skb);
		if (!xo)
			return -EINVAL;

		xo->flags |= XFRM_XMIT;
		return 0;
	}

	err = esp_output_tail(x, skb, &esp);
	if (err)
		return err;

	secpath_reset(skb);

	if (skb_needs_linearize(skb, skb->dev->features) &&
	    __skb_linearize(skb))
		return -ENOMEM;
	return 0;
}

static const struct net_offload esp4_offload = {
	.callbacks = {
		.gro_receive = esp4_gro_receive,
		.gso_segment = esp4_gso_segment,
	},
};

static const struct xfrm_type_offload esp_type_offload = {
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_ESP,
	.input_tail	= esp_input_tail,
	.xmit		= esp_xmit,
	.encap		= esp4_gso_encap,
};

static int __init esp4_offload_init(void)
{
	if (xfrm_register_type_offload(&esp_type_offload, AF_INET) < 0) {
		pr_info("%s: can't add xfrm type offload\n", __func__);
		return -EAGAIN;
	}

	return inet_add_offload(&esp4_offload, IPPROTO_ESP);
}

static void __exit esp4_offload_exit(void)
{
	xfrm_unregister_type_offload(&esp_type_offload, AF_INET);
	inet_del_offload(&esp4_offload, IPPROTO_ESP);
}

module_init(esp4_offload_init);
module_exit(esp4_offload_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Steffen Klassert <steffen.klassert@secunet.com>");
MODULE_ALIAS_XFRM_OFFLOAD_TYPE(AF_INET, XFRM_PROTO_ESP);
MODULE_DESCRIPTION("IPV4 GSO/GRO offload support");
