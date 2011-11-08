#include <crypto/hash.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/ah.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <linux/scatterlist.h>
#include <net/icmp.h>
#include <net/protocol.h>

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

static inline u8 *ah_tmp_auth(void *tmp, unsigned int offset)
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

/* Clear mutable options and find final destination to substitute
 * into IP header for icv calculation. Options are already checked
 * for validity, so paranoia is not required. */

static int ip_clear_mutable_options(const struct iphdr *iph, __be32 *daddr)
{
	unsigned char * optptr = (unsigned char*)(iph+1);
	int  l = iph->ihl*4 - sizeof(struct iphdr);
	int  optlen;

	while (l > 0) {
		switch (*optptr) {
		case IPOPT_END:
			return 0;
		case IPOPT_NOOP:
			l--;
			optptr++;
			continue;
		}
		optlen = optptr[1];
		if (optlen<2 || optlen>l)
			return -EINVAL;
		switch (*optptr) {
		case IPOPT_SEC:
		case 0x85:	/* Some "Extended Security" crap. */
		case IPOPT_CIPSO:
		case IPOPT_RA:
		case 0x80|21:	/* RFC1770 */
			break;
		case IPOPT_LSRR:
		case IPOPT_SSRR:
			if (optlen < 6)
				return -EINVAL;
			memcpy(daddr, optptr+optlen-4, 4);
			/* Fall through */
		default:
			memset(optptr, 0, optlen);
		}
		l -= optlen;
		optptr += optlen;
	}
	return 0;
}

static void ah_output_done(struct crypto_async_request *base, int err)
{
	u8 *icv;
	struct iphdr *iph;
	struct sk_buff *skb = base->data;
	struct xfrm_state *x = skb_dst(skb)->xfrm;
	struct ah_data *ahp = x->data;
	struct iphdr *top_iph = ip_hdr(skb);
	struct ip_auth_hdr *ah = ip_auth_hdr(skb);
	int ihl = ip_hdrlen(skb);

	iph = AH_SKB_CB(skb)->tmp;
	icv = ah_tmp_icv(ahp->ahash, iph, ihl);
	memcpy(ah->auth_data, icv, ahp->icv_trunc_len);

	top_iph->tos = iph->tos;
	top_iph->ttl = iph->ttl;
	top_iph->frag_off = iph->frag_off;
	if (top_iph->ihl != 5) {
		top_iph->daddr = iph->daddr;
		memcpy(top_iph+1, iph+1, top_iph->ihl*4 - sizeof(struct iphdr));
	}

	kfree(AH_SKB_CB(skb)->tmp);
	xfrm_output_resume(skb, err);
}

static int ah_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;
	int nfrags;
	int ihl;
	u8 *icv;
	struct sk_buff *trailer;
	struct crypto_ahash *ahash;
	struct ahash_request *req;
	struct scatterlist *sg;
	struct iphdr *iph, *top_iph;
	struct ip_auth_hdr *ah;
	struct ah_data *ahp;

	ahp = x->data;
	ahash = ahp->ahash;

	if ((err = skb_cow_data(skb, 0, &trailer)) < 0)
		goto out;
	nfrags = err;

	skb_push(skb, -skb_network_offset(skb));
	ah = ip_auth_hdr(skb);
	ihl = ip_hdrlen(skb);

	err = -ENOMEM;
	iph = ah_alloc_tmp(ahash, nfrags, ihl);
	if (!iph)
		goto out;

	icv = ah_tmp_icv(ahash, iph, ihl);
	req = ah_tmp_req(ahash, icv);
	sg = ah_req_sg(ahash, req);

	memset(ah->auth_data, 0, ahp->icv_trunc_len);

	top_iph = ip_hdr(skb);

	iph->tos = top_iph->tos;
	iph->ttl = top_iph->ttl;
	iph->frag_off = top_iph->frag_off;

	if (top_iph->ihl != 5) {
		iph->daddr = top_iph->daddr;
		memcpy(iph+1, top_iph+1, top_iph->ihl*4 - sizeof(struct iphdr));
		err = ip_clear_mutable_options(top_iph, &top_iph->daddr);
		if (err)
			goto out_free;
	}

	ah->nexthdr = *skb_mac_header(skb);
	*skb_mac_header(skb) = IPPROTO_AH;

	top_iph->tos = 0;
	top_iph->tot_len = htons(skb->len);
	top_iph->frag_off = 0;
	top_iph->ttl = 0;
	top_iph->check = 0;

	if (x->props.flags & XFRM_STATE_ALIGN4)
		ah->hdrlen  = (XFRM_ALIGN4(sizeof(*ah) + ahp->icv_trunc_len) >> 2) - 2;
	else
		ah->hdrlen  = (XFRM_ALIGN8(sizeof(*ah) + ahp->icv_trunc_len) >> 2) - 2;

	ah->reserved = 0;
	ah->spi = x->id.spi;
	ah->seq_no = htonl(XFRM_SKB_CB(skb)->seq.output.low);

	sg_init_table(sg, nfrags);
	skb_to_sgvec(skb, sg, 0, skb->len);

	ahash_request_set_crypt(req, sg, icv, skb->len);
	ahash_request_set_callback(req, 0, ah_output_done, skb);

	AH_SKB_CB(skb)->tmp = iph;

	err = crypto_ahash_digest(req);
	if (err) {
		if (err == -EINPROGRESS)
			goto out;

		if (err == -EBUSY)
			err = NET_XMIT_DROP;
		goto out_free;
	}

	memcpy(ah->auth_data, icv, ahp->icv_trunc_len);

	top_iph->tos = iph->tos;
	top_iph->ttl = iph->ttl;
	top_iph->frag_off = iph->frag_off;
	if (top_iph->ihl != 5) {
		top_iph->daddr = iph->daddr;
		memcpy(top_iph+1, iph+1, top_iph->ihl*4 - sizeof(struct iphdr));
	}

out_free:
	kfree(iph);
out:
	return err;
}

static void ah_input_done(struct crypto_async_request *base, int err)
{
	u8 *auth_data;
	u8 *icv;
	struct iphdr *work_iph;
	struct sk_buff *skb = base->data;
	struct xfrm_state *x = xfrm_input_state(skb);
	struct ah_data *ahp = x->data;
	struct ip_auth_hdr *ah = ip_auth_hdr(skb);
	int ihl = ip_hdrlen(skb);
	int ah_hlen = (ah->hdrlen + 2) << 2;

	work_iph = AH_SKB_CB(skb)->tmp;
	auth_data = ah_tmp_auth(work_iph, ihl);
	icv = ah_tmp_icv(ahp->ahash, auth_data, ahp->icv_trunc_len);

	err = memcmp(icv, auth_data, ahp->icv_trunc_len) ? -EBADMSG: 0;
	if (err)
		goto out;

	err = ah->nexthdr;

	skb->network_header += ah_hlen;
	memcpy(skb_network_header(skb), work_iph, ihl);
	__skb_pull(skb, ah_hlen + ihl);
	skb_set_transport_header(skb, -ihl);
out:
	kfree(AH_SKB_CB(skb)->tmp);
	xfrm_input_resume(skb, err);
}

static int ah_input(struct xfrm_state *x, struct sk_buff *skb)
{
	int ah_hlen;
	int ihl;
	int nexthdr;
	int nfrags;
	u8 *auth_data;
	u8 *icv;
	struct sk_buff *trailer;
	struct crypto_ahash *ahash;
	struct ahash_request *req;
	struct scatterlist *sg;
	struct iphdr *iph, *work_iph;
	struct ip_auth_hdr *ah;
	struct ah_data *ahp;
	int err = -ENOMEM;

	if (!pskb_may_pull(skb, sizeof(*ah)))
		goto out;

	ah = (struct ip_auth_hdr *)skb->data;
	ahp = x->data;
	ahash = ahp->ahash;

	nexthdr = ah->nexthdr;
	ah_hlen = (ah->hdrlen + 2) << 2;

	if (x->props.flags & XFRM_STATE_ALIGN4) {
		if (ah_hlen != XFRM_ALIGN4(sizeof(*ah) + ahp->icv_full_len) &&
		    ah_hlen != XFRM_ALIGN4(sizeof(*ah) + ahp->icv_trunc_len))
			goto out;
	} else {
		if (ah_hlen != XFRM_ALIGN8(sizeof(*ah) + ahp->icv_full_len) &&
		    ah_hlen != XFRM_ALIGN8(sizeof(*ah) + ahp->icv_trunc_len))
			goto out;
	}

	if (!pskb_may_pull(skb, ah_hlen))
		goto out;

	/* We are going to _remove_ AH header to keep sockets happy,
	 * so... Later this can change. */
	if (skb_cloned(skb) &&
	    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
		goto out;

	skb->ip_summed = CHECKSUM_NONE;


	if ((err = skb_cow_data(skb, 0, &trailer)) < 0)
		goto out;
	nfrags = err;

	ah = (struct ip_auth_hdr *)skb->data;
	iph = ip_hdr(skb);
	ihl = ip_hdrlen(skb);

	work_iph = ah_alloc_tmp(ahash, nfrags, ihl + ahp->icv_trunc_len);
	if (!work_iph)
		goto out;

	auth_data = ah_tmp_auth(work_iph, ihl);
	icv = ah_tmp_icv(ahash, auth_data, ahp->icv_trunc_len);
	req = ah_tmp_req(ahash, icv);
	sg = ah_req_sg(ahash, req);

	memcpy(work_iph, iph, ihl);
	memcpy(auth_data, ah->auth_data, ahp->icv_trunc_len);
	memset(ah->auth_data, 0, ahp->icv_trunc_len);

	iph->ttl = 0;
	iph->tos = 0;
	iph->frag_off = 0;
	iph->check = 0;
	if (ihl > sizeof(*iph)) {
		__be32 dummy;
		err = ip_clear_mutable_options(iph, &dummy);
		if (err)
			goto out_free;
	}

	skb_push(skb, ihl);

	sg_init_table(sg, nfrags);
	skb_to_sgvec(skb, sg, 0, skb->len);

	ahash_request_set_crypt(req, sg, icv, skb->len);
	ahash_request_set_callback(req, 0, ah_input_done, skb);

	AH_SKB_CB(skb)->tmp = work_iph;

	err = crypto_ahash_digest(req);
	if (err) {
		if (err == -EINPROGRESS)
			goto out;

		if (err == -EBUSY)
			err = NET_XMIT_DROP;
		goto out_free;
	}

	err = memcmp(icv, auth_data, ahp->icv_trunc_len) ? -EBADMSG: 0;
	if (err)
		goto out_free;

	skb->network_header += ah_hlen;
	memcpy(skb_network_header(skb), work_iph, ihl);
	__skb_pull(skb, ah_hlen + ihl);
	skb_set_transport_header(skb, -ihl);

	err = nexthdr;

out_free:
	kfree (work_iph);
out:
	return err;
}

static void ah4_err(struct sk_buff *skb, u32 info)
{
	struct net *net = dev_net(skb->dev);
	const struct iphdr *iph = (const struct iphdr *)skb->data;
	struct ip_auth_hdr *ah = (struct ip_auth_hdr *)(skb->data+(iph->ihl<<2));
	struct xfrm_state *x;

	if (icmp_hdr(skb)->type != ICMP_DEST_UNREACH ||
	    icmp_hdr(skb)->code != ICMP_FRAG_NEEDED)
		return;

	x = xfrm_state_lookup(net, skb->mark, (const xfrm_address_t *)&iph->daddr,
			      ah->spi, IPPROTO_AH, AF_INET);
	if (!x)
		return;
	printk(KERN_DEBUG "pmtu discovery on SA AH/%08x/%08x\n",
	       ntohl(ah->spi), ntohl(iph->daddr));
	xfrm_state_put(x);
}

static int ah_init_state(struct xfrm_state *x)
{
	struct ah_data *ahp = NULL;
	struct xfrm_algo_desc *aalg_desc;
	struct crypto_ahash *ahash;

	if (!x->aalg)
		goto error;

	if (x->encap)
		goto error;

	ahp = kzalloc(sizeof(*ahp), GFP_KERNEL);
	if (!ahp)
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
	 * after a successful crypto_alloc_ahash().
	 */
	aalg_desc = xfrm_aalg_get_byname(x->aalg->alg_name, 0);
	BUG_ON(!aalg_desc);

	if (aalg_desc->uinfo.auth.icv_fullbits/8 !=
	    crypto_ahash_digestsize(ahash)) {
		printk(KERN_INFO "AH: %s digestsize %u != %hu\n",
		       x->aalg->alg_name, crypto_ahash_digestsize(ahash),
		       aalg_desc->uinfo.auth.icv_fullbits/8);
		goto error;
	}

	ahp->icv_full_len = aalg_desc->uinfo.auth.icv_fullbits/8;
	ahp->icv_trunc_len = x->aalg->alg_trunc_len/8;

	BUG_ON(ahp->icv_trunc_len > MAX_AH_AUTH_LEN);

	if (x->props.flags & XFRM_STATE_ALIGN4)
		x->props.header_len = XFRM_ALIGN4(sizeof(struct ip_auth_hdr) +
						  ahp->icv_trunc_len);
	else
		x->props.header_len = XFRM_ALIGN8(sizeof(struct ip_auth_hdr) +
						  ahp->icv_trunc_len);
	if (x->props.mode == XFRM_MODE_TUNNEL)
		x->props.header_len += sizeof(struct iphdr);
	x->data = ahp;

	return 0;

error:
	if (ahp) {
		crypto_free_ahash(ahp->ahash);
		kfree(ahp);
	}
	return -EINVAL;
}

static void ah_destroy(struct xfrm_state *x)
{
	struct ah_data *ahp = x->data;

	if (!ahp)
		return;

	crypto_free_ahash(ahp->ahash);
	kfree(ahp);
}


static const struct xfrm_type ah_type =
{
	.description	= "AH4",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_AH,
	.flags		= XFRM_TYPE_REPLAY_PROT,
	.init_state	= ah_init_state,
	.destructor	= ah_destroy,
	.input		= ah_input,
	.output		= ah_output
};

static const struct net_protocol ah4_protocol = {
	.handler	=	xfrm4_rcv,
	.err_handler	=	ah4_err,
	.no_policy	=	1,
	.netns_ok	=	1,
};

static int __init ah4_init(void)
{
	if (xfrm_register_type(&ah_type, AF_INET) < 0) {
		printk(KERN_INFO "ip ah init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (inet_add_protocol(&ah4_protocol, IPPROTO_AH) < 0) {
		printk(KERN_INFO "ip ah init: can't add protocol\n");
		xfrm_unregister_type(&ah_type, AF_INET);
		return -EAGAIN;
	}
	return 0;
}

static void __exit ah4_fini(void)
{
	if (inet_del_protocol(&ah4_protocol, IPPROTO_AH) < 0)
		printk(KERN_INFO "ip ah close: can't remove protocol\n");
	if (xfrm_unregister_type(&ah_type, AF_INET) < 0)
		printk(KERN_INFO "ip ah close: can't remove xfrm type\n");
}

module_init(ah4_init);
module_exit(ah4_fini);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_TYPE(AF_INET, XFRM_PROTO_AH);
