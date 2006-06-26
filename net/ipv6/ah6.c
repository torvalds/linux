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

#include <linux/config.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/ah.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <linux/string.h>
#include <net/icmp.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/xfrm.h>
#include <asm/scatterlist.h>

static int zero_out_mutable_opts(struct ipv6_opt_hdr *opthdr)
{
	u8 *opt = (u8 *)opthdr;
	int len = ipv6_optlen(opthdr);
	int off = 0;
	int optlen = 0;

	off += 2;
	len -= 2;

	while (len > 0) {

		switch (opt[off]) {

		case IPV6_TLV_PAD0:
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
		return 1;

bad:
	return 0;
}

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
	ipv6_addr_copy(&final_addr, addrs + segments - 1);

	addrs += segments - segments_left;
	memmove(addrs + 1, addrs, (segments_left - 1) * sizeof(*addrs));

	ipv6_addr_copy(addrs, &iph->daddr);
	ipv6_addr_copy(&iph->daddr, &final_addr);
}

static int ipv6_clear_mutable_options(struct ipv6hdr *iph, int len)
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
		case NEXTHDR_HOP:
		case NEXTHDR_DEST:
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

static int ah6_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;
	int extlen;
	struct ipv6hdr *top_iph;
	struct ip_auth_hdr *ah;
	struct ah_data *ahp;
	u8 nexthdr;
	char tmp_base[8];
	struct {
		struct in6_addr daddr;
		char hdrs[0];
	} *tmp_ext;

	top_iph = (struct ipv6hdr *)skb->data;
	top_iph->payload_len = htons(skb->len - sizeof(*top_iph));

	nexthdr = *skb->nh.raw;
	*skb->nh.raw = IPPROTO_AH;

	/* When there are no extension headers, we only need to save the first
	 * 8 bytes of the base IP header.
	 */
	memcpy(tmp_base, top_iph, sizeof(tmp_base));

	tmp_ext = NULL;
	extlen = skb->h.raw - (unsigned char *)(top_iph + 1);
	if (extlen) {
		extlen += sizeof(*tmp_ext);
		tmp_ext = kmalloc(extlen, GFP_ATOMIC);
		if (!tmp_ext) {
			err = -ENOMEM;
			goto error;
		}
		memcpy(tmp_ext, &top_iph->daddr, extlen);
		err = ipv6_clear_mutable_options(top_iph,
						 extlen - sizeof(*tmp_ext) +
						 sizeof(*top_iph));
		if (err)
			goto error_free_iph;
	}

	ah = (struct ip_auth_hdr *)skb->h.raw;
	ah->nexthdr = nexthdr;

	top_iph->priority    = 0;
	top_iph->flow_lbl[0] = 0;
	top_iph->flow_lbl[1] = 0;
	top_iph->flow_lbl[2] = 0;
	top_iph->hop_limit   = 0;

	ahp = x->data;
	ah->hdrlen  = (XFRM_ALIGN8(sizeof(struct ipv6_auth_hdr) + 
				   ahp->icv_trunc_len) >> 2) - 2;

	ah->reserved = 0;
	ah->spi = x->id.spi;
	ah->seq_no = htonl(++x->replay.oseq);
	xfrm_aevent_doreplay(x);
	ahp->icv(ahp, skb, ah->auth_data);

	err = 0;

	memcpy(top_iph, tmp_base, sizeof(tmp_base));
	if (tmp_ext) {
		memcpy(&top_iph->daddr, tmp_ext, extlen);
error_free_iph:
		kfree(tmp_ext);
	}

error:
	return err;
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
	 * Moving the pointer of skb->nh.raw by using skb_pull as long as AH
	 * header length. Then copy back the copy as long as hdr_len
	 * If destination header following AH exists, copy it into after [Ext2].
	 * 
	 * |<>|[IPv6][Ext1][Ext2][Dest][Payload]
	 * There is offset of AH before IPv6 header after the process.
	 */

	struct ipv6_auth_hdr *ah;
	struct ah_data *ahp;
	unsigned char *tmp_hdr = NULL;
	u16 hdr_len;
	u16 ah_hlen;
	int nexthdr;

	if (!pskb_may_pull(skb, sizeof(struct ip_auth_hdr)))
		goto out;

	/* We are going to _remove_ AH header to keep sockets happy,
	 * so... Later this can change. */
	if (skb_cloned(skb) &&
	    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
		goto out;

	hdr_len = skb->data - skb->nh.raw;
	ah = (struct ipv6_auth_hdr*)skb->data;
	ahp = x->data;
	nexthdr = ah->nexthdr;
	ah_hlen = (ah->hdrlen + 2) << 2;

        if (ah_hlen != XFRM_ALIGN8(sizeof(struct ipv6_auth_hdr) + ahp->icv_full_len) &&
            ah_hlen != XFRM_ALIGN8(sizeof(struct ipv6_auth_hdr) + ahp->icv_trunc_len))
                goto out;

	if (!pskb_may_pull(skb, ah_hlen))
		goto out;

	tmp_hdr = kmalloc(hdr_len, GFP_ATOMIC);
	if (!tmp_hdr)
		goto out;
	memcpy(tmp_hdr, skb->nh.raw, hdr_len);
	if (ipv6_clear_mutable_options(skb->nh.ipv6h, hdr_len))
		goto free_out;
	skb->nh.ipv6h->priority    = 0;
	skb->nh.ipv6h->flow_lbl[0] = 0;
	skb->nh.ipv6h->flow_lbl[1] = 0;
	skb->nh.ipv6h->flow_lbl[2] = 0;
	skb->nh.ipv6h->hop_limit   = 0;

        {
		u8 auth_data[MAX_AH_AUTH_LEN];

		memcpy(auth_data, ah->auth_data, ahp->icv_trunc_len);
		memset(ah->auth_data, 0, ahp->icv_trunc_len);
		skb_push(skb, hdr_len);
		ahp->icv(ahp, skb, ah->auth_data);
		if (memcmp(ah->auth_data, auth_data, ahp->icv_trunc_len)) {
			LIMIT_NETDEBUG(KERN_WARNING "ipsec ah authentication error\n");
			x->stats.integrity_failed++;
			goto free_out;
		}
	}

	skb->h.raw = memcpy(skb->nh.raw += ah_hlen, tmp_hdr, hdr_len);
	__skb_pull(skb, ah_hlen + hdr_len);

	kfree(tmp_hdr);

	return nexthdr;

free_out:
	kfree(tmp_hdr);
out:
	return -EINVAL;
}

static void ah6_err(struct sk_buff *skb, struct inet6_skb_parm *opt, 
                    int type, int code, int offset, __u32 info)
{
	struct ipv6hdr *iph = (struct ipv6hdr*)skb->data;
	struct ip_auth_hdr *ah = (struct ip_auth_hdr*)(skb->data+offset);
	struct xfrm_state *x;

	if (type != ICMPV6_DEST_UNREACH &&
	    type != ICMPV6_PKT_TOOBIG)
		return;

	x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, ah->spi, IPPROTO_AH, AF_INET6);
	if (!x)
		return;

	NETDEBUG(KERN_DEBUG "pmtu discovery on SA AH/%08x/" NIP6_FMT "\n",
		 ntohl(ah->spi), NIP6(iph->daddr));

	xfrm_state_put(x);
}

static int ah6_init_state(struct xfrm_state *x)
{
	struct ah_data *ahp = NULL;
	struct xfrm_algo_desc *aalg_desc;

	if (!x->aalg)
		goto error;

	/* null auth can use a zero length key */
	if (x->aalg->alg_key_len > 512)
		goto error;

	if (x->encap)
		goto error;

	ahp = kzalloc(sizeof(*ahp), GFP_KERNEL);
	if (ahp == NULL)
		return -ENOMEM;

	ahp->key = x->aalg->alg_key;
	ahp->key_len = (x->aalg->alg_key_len+7)/8;
	ahp->tfm = crypto_alloc_tfm(x->aalg->alg_name, 0);
	if (!ahp->tfm)
		goto error;
	ahp->icv = ah_hmac_digest;
	
	/*
	 * Lookup the algorithm description maintained by xfrm_algo,
	 * verify crypto transform properties, and store information
	 * we need for AH processing.  This lookup cannot fail here
	 * after a successful crypto_alloc_tfm().
	 */
	aalg_desc = xfrm_aalg_get_byname(x->aalg->alg_name, 0);
	BUG_ON(!aalg_desc);

	if (aalg_desc->uinfo.auth.icv_fullbits/8 !=
	    crypto_tfm_alg_digestsize(ahp->tfm)) {
		printk(KERN_INFO "AH: %s digestsize %u != %hu\n",
		       x->aalg->alg_name, crypto_tfm_alg_digestsize(ahp->tfm),
		       aalg_desc->uinfo.auth.icv_fullbits/8);
		goto error;
	}
	
	ahp->icv_full_len = aalg_desc->uinfo.auth.icv_fullbits/8;
	ahp->icv_trunc_len = aalg_desc->uinfo.auth.icv_truncbits/8;
	
	BUG_ON(ahp->icv_trunc_len > MAX_AH_AUTH_LEN);
	
	ahp->work_icv = kmalloc(ahp->icv_full_len, GFP_KERNEL);
	if (!ahp->work_icv)
		goto error;
	
	x->props.header_len = XFRM_ALIGN8(sizeof(struct ipv6_auth_hdr) + ahp->icv_trunc_len);
	if (x->props.mode)
		x->props.header_len += sizeof(struct ipv6hdr);
	x->data = ahp;

	return 0;

error:
	if (ahp) {
		kfree(ahp->work_icv);
		crypto_free_tfm(ahp->tfm);
		kfree(ahp);
	}
	return -EINVAL;
}

static void ah6_destroy(struct xfrm_state *x)
{
	struct ah_data *ahp = x->data;

	if (!ahp)
		return;

	kfree(ahp->work_icv);
	ahp->work_icv = NULL;
	crypto_free_tfm(ahp->tfm);
	ahp->tfm = NULL;
	kfree(ahp);
}

static struct xfrm_type ah6_type =
{
	.description	= "AH6",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_AH,
	.init_state	= ah6_init_state,
	.destructor	= ah6_destroy,
	.input		= ah6_input,
	.output		= ah6_output
};

static struct inet6_protocol ah6_protocol = {
	.handler	=	xfrm6_rcv,
	.err_handler	=	ah6_err,
	.flags		=	INET6_PROTO_NOPOLICY,
};

static int __init ah6_init(void)
{
	if (xfrm_register_type(&ah6_type, AF_INET6) < 0) {
		printk(KERN_INFO "ipv6 ah init: can't add xfrm type\n");
		return -EAGAIN;
	}

	if (inet6_add_protocol(&ah6_protocol, IPPROTO_AH) < 0) {
		printk(KERN_INFO "ipv6 ah init: can't add protocol\n");
		xfrm_unregister_type(&ah6_type, AF_INET6);
		return -EAGAIN;
	}

	return 0;
}

static void __exit ah6_fini(void)
{
	if (inet6_del_protocol(&ah6_protocol, IPPROTO_AH) < 0)
		printk(KERN_INFO "ipv6 ah close: can't remove protocol\n");

	if (xfrm_unregister_type(&ah6_type, AF_INET6) < 0)
		printk(KERN_INFO "ipv6 ah close: can't remove xfrm type\n");

}

module_init(ah6_init);
module_exit(ah6_fini);

MODULE_LICENSE("GPL");
