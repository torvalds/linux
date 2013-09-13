/*
 * Copyright (C)2002 USAGI/WIDE Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors
 *
 *	Mitsuru KANDA @USAGI       : IPv6 Support
 * 	Kazunori MIYAZAWA @USAGI   :
 * 	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 *
 * 	This file is derived from net/ipv4/ah.c.
 */

#define pr_fmt(fmt) "IPv6: " fmt

#include <crypto/hash.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <net/ip.h>
#include <net/ah.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <linux/string.h>
#include <linux/scatterlist.h>
#include <net/ip6_route.h>
#include <net/icmp.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/xfrm.h>

#define IPV6HDR_BASELEN 8

struct tmp_ext {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
		struct in6_addr saddr;
#endif
		struct in6_addr daddr;
		char hdrs[0];
};

struct ah_skb_cb {
	struct xfrm_skb_cb xfrm;
	void *tmp;
};

#define AH_SKB_CB(__skb) ((struct ah_skb_cb *)&((__skb)->cb[0]))

static void *ah_alloc_tmp(struct crypto_ahash *ahash, int nfrags,
			  unsigned int size)
{
	unsigned int len;

	len = size + crypto_ahash_digestsize(ahash) +
	      (crypto_ahash_alignmask(ahash) &
	       ~(crypto_tfm_ctx_alignment() - 1));

	len = ALIGN(len, crypto_tfm_ctx_alignment());

	len += sizeof(struct ahash_request) + crypto_ahash_reqsize(ahash);
	len = ALIGN(len, __alignof__(struct scatterlist));

	len += sizeof(struct scatterlist) * nfrags;

	return kmalloc(len, GFP_ATOMIC);
}

static inline struct tmp_ext *ah_tmp_ext(void *base)
{
	return base + IPV6HDR_BASELEN;
}

static inline u8 *ah_tmp_auth(u8 *tmp, unsigned int offset)
{
	return tmp + offset;
}

static inline u8 *ah_tmp_icv(struct crypto_ahash *ahash, void *tmp,
			     unsigned int offset)
{
	return PTR_ALIGN((u8 *)tmp + offset, crypto_ahash_alignmask(ahash) + 1);
}

static inline struct ahash_request *ah_tmp_req(struct crypto_ahash *ahash,
					       u8 *icv)
{
	struct ahash_request *req;

	req = (void *)PTR_ALIGN(icv + crypto_ahash_digestsize(ahash),
				crypto_tfm_ctx_alignment());

	ahash_request_set_tfm(req, ahash);

	return req;
}

static inline struct scatterlist *ah_req_sg(struct crypto_ahash *ahash,
					     struct ahash_request *req)
{
	return (void *)ALIGN((unsigned long)(req + 1) +
			     crypto_ahash_reqsize(ahash),
			     __alignof__(struct scatterlist));
}

static bool zero_out_mutable_opts(struct ipv6_opt_hdr *opthdr)
{
	u8 *opt = (u8 *)opthdr;
	int len = ipv6_optlen(opthdr);
	int off = 0;
	int optlen = 0;

	off += 2;
	len -= 2;

	while (len > 0) {

		switch (opt[off]) {

		case IPV6_TLV_PAD1:
			optlen = 1;
			break;
		default:
			if (len < 2)
				goto bad;
			optlen = opt[off+1]+2;
			if (len < optlen)
				goto bad;
			if (opt[off] & 0x20)
				memset(&opt[off+2], 0, opt[off+1]);
			break;
		}

		off += optlen;
		len -= optlen;
	}
	if (len == 0)
		return true;

bad:
	return false;
}

#if IS_ENABLED(CONFIG_IPV6_MIP6)
/**
 *	ipv6_rearrange_destopt - rearrange IPv6 destination options header
 *	@iph: IPv6 header
 *	@destopt: destionation options header
 */
static void ipv6_rearrange_destopt(struct ipv6hdr *iph, struct ipv6_opt_hdr *destopt)
{
	u8 *opt = (u8 *)destopt;
	int len = ipv6_optlen(destopt);
	int off = 0;
	int optlen = 0;

	off += 2;
	len -= 2;

	while (len > 0) {

		switch (opt[off]) {

		case IPV6_TLV_PAD1:
			optlen = 1;
			break;
		default:
			if (len < 2)
				goto bad;
			optlen = opt[off+1]+2;
			if (len < optlen)
				goto bad;

			/* Rearrange the source address in @iph and the
			 * addresses in home address option for final source.
			 * See 11.3.2 of RFC 3775 for details.
			 */
			if (opt[off] == IPV6_TLV_HAO) {
				struct in6_addr final_addr;
				struct ipv6_destopt_hao *hao;

				hao = (struct ipv6_destopt_hao *)&opt[off];
				if (hao->length != sizeof(hao->addr)) {
					net_warn_ratelimited("destopt hao: invalid header length: %u\n",
							     hao->length);
					goto bad;
				}
				final_addr = hao->addr;
				hao->addr = iph->saddr;
				iph->saddr = final_addr;
			}
			break;
		}

		off += optlen;
		len -= optlen;
	}
	/* Note: ok if len == 0 */
bad:
	return;
}
#else
static void ipv6_rearrange_destopt(struct ipv6hdr *iph, struct ipv6_opt_hdr *destopt) {}
#endif

/**
 *	ipv6_rearrange_rthdr - rearrange IPv6 routing header
 *	@iph: IPv6 header
 *	@rthdr: routing header
 *
 *	Rearrange the destination address in @iph and the addresses in @rthdr
 *	so that they appear in the order they will at the final destination.
 *	See Appendix A2 of RFC 2402 for details.
 */
static void ipv6_rearrange_rthdr(struct ipv6hdr *iph, struct ipv6_rt_hdr *rthdr)
{
	int segments, segments_left;
	struct in6_addr *addrs;
	struct in6_addr final_addr;

	segments_left = rthdr->segments_left;
	if (segments_left == 0)
		return;
	rthdr->segments_left = 0;

	/* The value of rthdr->hdrlen has been verified either by the system
	 * call if it is locally generated, or by ipv6_rthdr_rcv() for incoming
	 * packets.  So we can assume that it is even and that segments is
	 * greater than or equal to segments_left.
	 *
	 * For the same reason we can assume that this option is of type 0.
	 */
	segments = rthdr->hdrlen >> 1;

	addrs = ((struct rt0_hdr *)rthdr)->addr;
	final_addr = addrs[segments - 1];

	addrs += segments - segments_left;
	memmove(addrs + 1, addrs, (segments_left - 1) * sizeof(*addrs));

	addrs[0] = iph->daddr;
	iph->daddr = final_addr;
}

static int ipv6_clear_mutable_options(struct ipv6hdr *iph, int len, int dir)
{
	union {
		struct ipv6hdr *iph;
		struct ipv6_opt_hdr *opth;
		struct ipv6_rt_hdr *rth;
		char *raw;
	} exthdr = { .iph = iph };
	char *end = exthdr.raw + len;
	int nexthdr = iph->nexthdr;

	exthdr.iph++;

	while (exthdr.raw < end) {
		switch (nexthdr) {
		case NEXTHDR_DEST:
			if (dir == XFRM_POLICY_OUT)
				ipv6_rearrange_destopt(iph, exthdr.opth);
		case NEXTHDR_HOP:
			if (!zero_out_mutable_opts(exthdr.opth)) {
				LIMIT_NETDEBUG(
					KERN_WARNING "overrun %sopts\n",
					nexthdr == NEXTHDR_HOP ?
						"hop" : "dest");
				return -EINVAL;
			}
			break;

		case NEXTHDR_ROUTING:
			ipv6_rearrange_rthdr(iph, exthdr.rth);
			break;

		default :
			return 0;
		}

		nexthdr = exthdr.opth->nexthdr;
		exthdr.raw += ipv6_optlen(exthdr.opth);
	}

	return 0;
}

static void ah6_output_done(struct crypto_async_request *base, int err)
{
	int extlen;
	u8 *iph_base;
	u8 *icv;
	struct sk_buff *skb = base->data;
	struct xfrm_state *x = skb_dst(skb)->xfrm;
	struct ah_data *ahp = x->data;
	struct ipv6hdr *top_iph = ipv6_hdr(skb);
	struct ip_auth_hdr *ah = ip_auth_hdr(skb);
	struct tmp_ext *iph_ext;

	extlen = skb_network_header_len(skb) - sizeof(struct ipv6hdr);
	if (extlen)
		extlen += sizeof(*iph_ext);

	iph_base = AH_SKB_CB(skb)->tmp;
	iph_ext = ah_tmp_ext(iph_base);
	icv = ah_tmp_icv(ahp->ahash, iph_ext, extlen);

	memcpy(ah->auth_data, icv, ahp->icv_trunc_len);
	memcpy(top_iph, iph_base, IPV6HDR_BASELEN);

	if (extlen) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
		memcpy(&top_iph->saddr, iph_ext, extlen);
#else
		memcpy(&top_iph->daddr, iph_ext, extlen);
#endif
	}

	kfree(AH_SKB_CB(skb)->tmp);
	xfrm_output_resume(skb, err);
}

static int ah6_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;
	int nfrags;
	int extlen;
	u8 *iph_base;
	u8 *icv;
	u8 nexthdr;
	struct sk_buff *trailer;
	struct crypto_ahash *ahash;
	struct ahash_request *req;
	struct scatterlist *sg;
	struct ipv6hdr *top_iph;
	struct ip_auth_hdr *ah;
	struct ah_data *ahp;
	struct tmp_ext *iph_ext;

	ahp = x->data;
	ahash = ahp->ahash;

	if ((err = skb_cow_data(skb, 0, &trailer)) < 0)
		goto out;
	nfrags = err;

	skb_push(skb, -skb_network_offset(skb));
	extlen = skb_network_header_len(skb) - sizeof(struct ipv6hdr);
	if (extlen)
		extlen += sizeof(*iph_ext);

	err = -ENOMEM;
	iph_base = ah_alloc_tmp(ahash, nfrags, IPV6HDR_BASELEN + extlen);
	if (!iph_base)
		goto out;

	iph_ext = ah_tmp_ext(iph_base);
	icv = ah_tmp_icv(ahash, iph_ext, extlen);
	req = ah_tmp_req(ahash, icv);
	sg = ah_req_sg(ahash, req);

	ah = ip_auth_hdr(skb);
	memset(ah->auth_data, 0, ahp->icv_trunc_len);

	top_iph = ipv6_hdr(skb);
	top_iph->payload_len = htons(skb->len - sizeof(*top_iph));

	nexthdr = *skb_mac_header(skb);
	*skb_mac_header(skb) = IPPROTO_AH;

	/* When there are no extension headers, we only need to save the first
	 * 8 bytes of the base IP header.
	 */
	memcpy(iph_base, top_iph, IPV6HDR_BASELEN);

	if (extlen) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
		memcpy(iph_ext, &top_iph->saddr, extlen);
#else
		memcpy(iph_ext, &top_iph->daddr, extlen);
#endif
		err = ipv6_clear_mutable_options(top_iph,
						 extlen - sizeof(*iph_ext) +
						 sizeof(*top_iph),
						 XFRM_POLICY_OUT);
		if (err)
			goto out_free;
	}

	ah->nexthdr = nexthdr;

	top_iph->priority    = 0;
	top_iph->flow_lbl[0] = 0;
	top_iph->flow_lbl[1] = 0;
	top_iph->flow_lbl[2] = 0;
	top_iph->hop_limit   = 0;

	ah->hdrlen  = (XFRM_ALIGN8(sizeof(*ah) + ahp->icv_trunc_len) >> 2) - 2;

	ah->reserved = 0;
	ah->spi = x->id.spi;
	ah->seq_no = htonl(XFRM_SKB_CB(skb)->seq.output.low);

	sg_init_table(sg, nfrags);
	skb_to_sgvec(skb, sg, 0, skb->len);

	ahash_request_set_crypt(req, sg, icv, skb->len);
	ahash_request_set_callback(req, 0, ah6_output_done, skb);

	AH_SKB_CB(skb)->tmp = iph_base;

	err = crypto_ahash_digest(req);
	if (err) {
		if (err == -EINPROGRESS)
			goto out;

		if (err == -EBUSY)
			err = NET_XMIT_DROP;
		goto out_free;
	}

	memcpy(ah->auth_data, icv, ahp->icv_trunc_len);
	memcpy(top_iph, iph_base, IPV6HDR_BASELEN);

	if (extlen) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
		memcpy(&top_iph->saddr, iph_ext, extlen);
#else
		memcpy(&top_iph->daddr, iph_ext, extlen);
#endif
	}

out_free:
	kfree(iph_base);
out:
	return err;
}

static void ah6_input_done(struct crypto_async_request *base, int err)
{
	u8 *auth_data;
	u8 *icv;
	u8 *work_iph;
	struct sk_buff *skb = base->data;
	struct xfrm_state *x = xfrm_input_state(skb);
	struct ah_data *ahp = x->data;
	struct ip_auth_hdr *ah = ip_auth_hdr(skb);
	int hdr_len = skb_network_header_len(skb);
	int ah_hlen = (ah->hdrlen + 2) << 2;

	work_iph = AH_SKB_CB(skb)->tmp;
	auth_data = ah_tmp_auth(work_iph, hdr_len);
	icv = ah_tmp_icv(ahp->ahash, auth_data, ahp->icv_trunc_len);

	err = memcmp(icv, auth_data, ahp->icv_trunc_len) ? -EBADMSG: 0;
	if (err)
		goto out;

	err = ah->nexthdr;

	skb->network_header += ah_hlen;
	memcpy(skb_network_header(skb), work_iph, hdr_len);
	__skb_pull(skb, ah_hlen + hdr_len);
	if (x->props.mode == XFRM_MODE_TUNNEL)
		skb_reset_transport_header(skb);
	else
		skb_set_transport_header(skb, -hdr_len);
out:
	kfree(AH_SKB_CB(skb)->tmp);
	xfrm_input_resume(skb, err);
}



static int ah6_input(struct xfrm_state *x, struct sk_buff *skb)
{
	/*
	 * Before process AH
	 * [IPv6][Ext1][Ext2][AH][Dest][Payload]
	 * |<-------------->| hdr_len
	 *
	 * To erase AH:
	 * Keeping copy of cleared headers. After AH processing,
	 * Moving the pointer of skb->network_header by using skb_pull as long
	 * as AH header length. Then copy back the copy as long as hdr_len
	 * If destination header following AH exists, copy it into after [Ext2].
	 *
	 * |<>|[IPv6][Ext1][Ext2][Dest][Payload]
	 * There is offset of AH before IPv6 header after the process.
	 */

	u8 *auth_data;
	u8 *icv;
	u8 *work_iph;
	struct sk_buff *trailer;
	struct crypto_ahash *ahash;
	struct ahash_request *req;
	struct scatterlist *sg;
	struct ip_auth_hdr *ah;
	struct ipv6hdr *ip6h;
	struct ah_data *ahp;
	u16 hdr_len;
	u16 ah_hlen;
	int nexthdr;
	int nfrags;
	int err = -ENOMEM;

	if (!pskb_may_pull(skb, sizeof(struct ip_auth_hdr)))
		goto out;

	/* We are going to _remove_ AH header to keep sockets happy,
	 * so... Later this can change. */
	if (skb_unclone(skb, GFP_ATOMIC))
		goto out;

	skb->ip_summed = CHECKSUM_NONE;

	hdr_len = skb_network_header_len(skb);
	ah = (struct ip_auth_hdr *)skb->data;
	ahp = x->data;
	ahash = ahp->ahash;

	nexthdr = ah->nexthdr;
	ah_hlen = (ah->hdrlen + 2) << 2;

	if (ah_hlen != XFRM_ALIGN8(sizeof(*ah) + ahp->icv_full_len) &&
	    ah_hlen != XFRM_ALIGN8(sizeof(*ah) + ahp->icv_trunc_len))
		goto out;

	if (!pskb_may_pull(skb, ah_hlen))
		goto out;


	if ((err = skb_cow_data(skb, 0, &trailer)) < 0)
		goto out;
	nfrags = err;

	ah = (struct ip_auth_hdr *)skb->data;
	ip6h = ipv6_hdr(skb);

	skb_push(skb, hdr_len);

	work_iph = ah_alloc_tmp(ahash, nfrags, hdr_len + ahp->icv_trunc_len);
	if (!work_iph)
		goto out;

	auth_data = ah_tmp_auth(work_iph, hdr_len);
	icv = ah_tmp_icv(ahash, auth_data, ahp->icv_trunc_len);
	req = ah_tmp_req(ahash, icv);
	sg = ah_req_sg(ahash, req);

	memcpy(work_iph, ip6h, hdr_len);
	memcpy(auth_data, ah->auth_data, ahp->icv_trunc_len);
	memset(ah->auth_data, 0, ahp->icv_trunc_len);

	if (ipv6_clear_mutable_options(ip6h, hdr_len, XFRM_POLICY_IN))
		goto out_free;

	ip6h->priority    = 0;
	ip6h->flow_lbl[0] = 0;
	ip6h->flow_lbl[1] = 0;
	ip6h->flow_lbl[2] = 0;
	ip6h->hop_limit   = 0;

	sg_init_table(sg, nfrags);
	skb_to_sgvec(skb, sg, 0, skb->len);

	ahash_request_set_crypt(req, sg, icv, skb->len);
	ahash_request_set_callback(req, 0, ah6_input_done, skb);

	AH_SKB_CB(skb)->tmp = work_iph;

	err = crypto_ahash_digest(req);
	if (err) {
		if (err == -EINPROGRESS)
			goto out;

		goto out_free;
	}

	err = memcmp(icv, auth_data, ahp->icv_trunc_len) ? -EBADMSG: 0;
	if (err)
		goto out_free;

	skb->network_header += ah_hlen;
	memcpy(skb_network_header(skb), work_iph, hdr_len);
	__skb_pull(skb, ah_hlen + hdr_len);

	if (x->props.mode == XFRM_MODE_TUNNEL)
		skb_reset_transport_header(skb);
	else
		skb_set_transport_header(skb, -hdr_len);

	err = nexthdr;

out_free:
	kfree(work_iph);
out:
	return err;
}

static void ah6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		    u8 type, u8 code, int offset, __be32 info)
{
	struct net *net = dev_net(skb->dev);
	struct ipv6hdr *iph = (struct ipv6hdr*)skb->data;
	struct ip_auth_hdr *ah = (struct ip_auth_hdr*)(skb->data+offset);
	struct xfrm_state *x;

	if (type != ICMPV6_DEST_UNREACH &&
	    type != ICMPV6_PKT_TOOBIG &&
	    type != NDISC_REDIRECT)
		return;

	x = xfrm_state_lookup(net, skb->mark, (xfrm_address_t *)&iph->daddr, ah->spi, IPPROTO_AH, AF_INET6);
	if (!x)
		return;

	if (type == NDISC_REDIRECT)
		ip6_redirect(skb, net, skb->dev->ifindex, 0);
	else
		ip6_update_pmtu(skb, net, info, 0, 0);
	xfrm_state_put(x);
}

static int ah6_init_state(struct xfrm_state *x)
{
	struct ah_data *ahp = NULL;
	struct xfrm_algo_desc *aalg_desc;
	struct crypto_ahash *ahash;

	if (!x->aalg)
		goto error;

	if (x->encap)
		goto error;

	ahp = kzalloc(sizeof(*ahp), GFP_KERNEL);
	if (ahp == NULL)
		return -ENOMEM;

	ahash = crypto_alloc_ahash(x->aalg->alg_name, 0, 0);
	if (IS_ERR(ahash))
		goto error;

	ahp->ahash = ahash;
	if (crypto_ahash_setkey(ahash, x->aalg->alg_key,
			       (x->aalg->alg_key_len + 7) / 8))
		goto error;

	/*
	 * Lookup the algorithm description maintained by xfrm_algo,
	 * verify crypto transform properties, and store information
	 * we need for AH processing.  This lookup cannot fail here
	 * after a successful crypto_alloc_hash().
	 */
	aalg_desc = xfrm_aalg_get_byname(x->aalg->alg_name, 0);
	BUG_ON(!aalg_desc);

	if (aalg_desc->uinfo.auth.icv_fullbits/8 !=
	    crypto_ahash_digestsize(ahash)) {
		pr_info("AH: %s digestsize %u != %hu\n",
			x->aalg->alg_name, crypto_ahash_digestsize(ahash),
			aalg_desc->uinfo.auth.icv_fullbits/8);
		goto error;
	}

	ahp->icv_full_len = aalg_desc->uinfo.auth.icv_fullbits/8;
	ahp->icv_trunc_len = x->aalg->alg_trunc_len/8;

	BUG_ON(ahp->icv_trunc_len > MAX_AH_AUTH_LEN);

	x->props.header_len = XFRM_ALIGN8(sizeof(struct ip_auth_hdr) +
					  ahp->icv_trunc_len);
	switch (x->props.mode) {
	case XFRM_MODE_BEET:
	case XFRM_MODE_TRANSPORT:
		break;
	case XFRM_MODE_TUNNEL:
		x->props.header_len += sizeof(struct ipv6hdr);
		break;
	default:
		goto error;
	}
	x->data = ahp;

	return 0;

error:
	if (ahp) {
		crypto_free_ahash(ahp->ahash);
		kfree(ahp);
	}
	return -EINVAL;
}

static void ah6_destroy(struct xfrm_state *x)
{
	struct ah_data *ahp = x->data;

	if (!ahp)
		return;

	crypto_free_ahash(ahp->ahash);
	kfree(ahp);
}

static const struct xfrm_type ah6_type =
{
	.description	= "AH6",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_AH,
	.flags		= XFRM_TYPE_REPLAY_PROT,
	.init_state	= ah6_init_state,
	.destructor	= ah6_destroy,
	.input		= ah6_input,
	.output		= ah6_output,
	.hdr_offset	= xfrm6_find_1stfragopt,
};

static const struct inet6_protocol ah6_protocol = {
	.handler	=	xfrm6_rcv,
	.err_handler	=	ah6_err,
	.flags		=	INET6_PROTO_NOPOLICY,
};

static int __init ah6_init(void)
{
	if (xfrm_register_type(&ah6_type, AF_INET6) < 0) {
		pr_info("%s: can't add xfrm type\n", __func__);
		return -EAGAIN;
	}

	if (inet6_add_protocol(&ah6_protocol, IPPROTO_AH) < 0) {
		pr_info("%s: can't add protocol\n", __func__);
		xfrm_unregister_type(&ah6_type, AF_INET6);
		return -EAGAIN;
	}

	return 0;
}

static void __exit ah6_fini(void)
{
	if (inet6_del_protocol(&ah6_protocol, IPPROTO_AH) < 0)
		pr_info("%s: can't remove protocol\n", __func__);

	if (xfrm_unregister_type(&ah6_type, AF_INET6) < 0)
		pr_info("%s: can't remove xfrm type\n", __func__);

}

module_init(ah6_init);
module_exit(ah6_fini);

MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_TYPE(AF_INET6, XFRM_PROTO_AH);
