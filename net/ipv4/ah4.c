#include <linux/err.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/ah.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <asm/scatterlist.h>


/* Clear mutable options and find final destination to substitute
 * into IP header for icv calculation. Options are already checked
 * for validity, so paranoia is not required. */

static int ip_clear_mutable_options(struct iphdr *iph, __be32 *daddr)
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
			memset(optptr+2, 0, optlen-2);
		}
		l -= optlen;
		optptr += optlen;
	}
	return 0;
}

static int ah_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;
	struct iphdr *iph, *top_iph;
	struct ip_auth_hdr *ah;
	struct ah_data *ahp;
	union {
		struct iphdr	iph;
		char 		buf[60];
	} tmp_iph;

	top_iph = skb->nh.iph;
	iph = &tmp_iph.iph;

	iph->tos = top_iph->tos;
	iph->ttl = top_iph->ttl;
	iph->frag_off = top_iph->frag_off;

	if (top_iph->ihl != 5) {
		iph->daddr = top_iph->daddr;
		memcpy(iph+1, top_iph+1, top_iph->ihl*4 - sizeof(struct iphdr));
		err = ip_clear_mutable_options(top_iph, &top_iph->daddr);
		if (err)
			goto error;
	}

	ah = (struct ip_auth_hdr *)((char *)top_iph+top_iph->ihl*4);
	ah->nexthdr = top_iph->protocol;

	top_iph->tos = 0;
	top_iph->tot_len = htons(skb->len);
	top_iph->frag_off = 0;
	top_iph->ttl = 0;
	top_iph->protocol = IPPROTO_AH;
	top_iph->check = 0;

	ahp = x->data;
	ah->hdrlen  = (XFRM_ALIGN8(sizeof(struct ip_auth_hdr) +
				   ahp->icv_trunc_len) >> 2) - 2;

	ah->reserved = 0;
	ah->spi = x->id.spi;
	ah->seq_no = htonl(++x->replay.oseq);
	xfrm_aevent_doreplay(x);
	err = ah_mac_digest(ahp, skb, ah->auth_data);
	if (err)
		goto error;
	memcpy(ah->auth_data, ahp->work_icv, ahp->icv_trunc_len);

	top_iph->tos = iph->tos;
	top_iph->ttl = iph->ttl;
	top_iph->frag_off = iph->frag_off;
	if (top_iph->ihl != 5) {
		top_iph->daddr = iph->daddr;
		memcpy(top_iph+1, iph+1, top_iph->ihl*4 - sizeof(struct iphdr));
	}

	ip_send_check(top_iph);

	err = 0;

error:
	return err;
}

static int ah_input(struct xfrm_state *x, struct sk_buff *skb)
{
	int ah_hlen;
	int ihl;
	int err = -EINVAL;
	struct iphdr *iph;
	struct ip_auth_hdr *ah;
	struct ah_data *ahp;
	char work_buf[60];

	if (!pskb_may_pull(skb, sizeof(struct ip_auth_hdr)))
		goto out;

	ah = (struct ip_auth_hdr*)skb->data;
	ahp = x->data;
	ah_hlen = (ah->hdrlen + 2) << 2;

	if (ah_hlen != XFRM_ALIGN8(sizeof(struct ip_auth_hdr) + ahp->icv_full_len) &&
	    ah_hlen != XFRM_ALIGN8(sizeof(struct ip_auth_hdr) + ahp->icv_trunc_len))
		goto out;

	if (!pskb_may_pull(skb, ah_hlen))
		goto out;

	/* We are going to _remove_ AH header to keep sockets happy,
	 * so... Later this can change. */
	if (skb_cloned(skb) &&
	    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
		goto out;

	skb->ip_summed = CHECKSUM_NONE;

	ah = (struct ip_auth_hdr*)skb->data;
	iph = skb->nh.iph;

	ihl = skb->data - skb->nh.raw;
	memcpy(work_buf, iph, ihl);

	iph->ttl = 0;
	iph->tos = 0;
	iph->frag_off = 0;
	iph->check = 0;
	if (ihl > sizeof(*iph)) {
		__be32 dummy;
		if (ip_clear_mutable_options(iph, &dummy))
			goto out;
	}
	{
		u8 auth_data[MAX_AH_AUTH_LEN];

		memcpy(auth_data, ah->auth_data, ahp->icv_trunc_len);
		skb_push(skb, ihl);
		err = ah_mac_digest(ahp, skb, ah->auth_data);
		if (err)
			goto out;
		err = -EINVAL;
		if (memcmp(ahp->work_icv, auth_data, ahp->icv_trunc_len)) {
			x->stats.integrity_failed++;
			goto out;
		}
	}
	((struct iphdr*)work_buf)->protocol = ah->nexthdr;
	skb->h.raw = memcpy(skb->nh.raw += ah_hlen, work_buf, ihl);
	__skb_pull(skb, ah_hlen + ihl);

	return 0;

out:
	return err;
}

static void ah4_err(struct sk_buff *skb, u32 info)
{
	struct iphdr *iph = (struct iphdr*)skb->data;
	struct ip_auth_hdr *ah = (struct ip_auth_hdr*)(skb->data+(iph->ihl<<2));
	struct xfrm_state *x;

	if (skb->h.icmph->type != ICMP_DEST_UNREACH ||
	    skb->h.icmph->code != ICMP_FRAG_NEEDED)
		return;

	x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, ah->spi, IPPROTO_AH, AF_INET);
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
	struct crypto_hash *tfm;

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
	tfm = crypto_alloc_hash(x->aalg->alg_name, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm))
		goto error;

	ahp->tfm = tfm;
	if (crypto_hash_setkey(tfm, ahp->key, ahp->key_len))
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
	    crypto_hash_digestsize(tfm)) {
		printk(KERN_INFO "AH: %s digestsize %u != %hu\n",
		       x->aalg->alg_name, crypto_hash_digestsize(tfm),
		       aalg_desc->uinfo.auth.icv_fullbits/8);
		goto error;
	}

	ahp->icv_full_len = aalg_desc->uinfo.auth.icv_fullbits/8;
	ahp->icv_trunc_len = aalg_desc->uinfo.auth.icv_truncbits/8;

	BUG_ON(ahp->icv_trunc_len > MAX_AH_AUTH_LEN);

	ahp->work_icv = kmalloc(ahp->icv_full_len, GFP_KERNEL);
	if (!ahp->work_icv)
		goto error;

	x->props.header_len = XFRM_ALIGN8(sizeof(struct ip_auth_hdr) + ahp->icv_trunc_len);
	if (x->props.mode == XFRM_MODE_TUNNEL)
		x->props.header_len += sizeof(struct iphdr);
	x->data = ahp;

	return 0;

error:
	if (ahp) {
		kfree(ahp->work_icv);
		crypto_free_hash(ahp->tfm);
		kfree(ahp);
	}
	return -EINVAL;
}

static void ah_destroy(struct xfrm_state *x)
{
	struct ah_data *ahp = x->data;

	if (!ahp)
		return;

	kfree(ahp->work_icv);
	ahp->work_icv = NULL;
	crypto_free_hash(ahp->tfm);
	ahp->tfm = NULL;
	kfree(ahp);
}


static struct xfrm_type ah_type =
{
	.description	= "AH4",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_AH,
	.init_state	= ah_init_state,
	.destructor	= ah_destroy,
	.input		= ah_input,
	.output		= ah_output
};

static struct net_protocol ah4_protocol = {
	.handler	=	xfrm4_rcv,
	.err_handler	=	ah4_err,
	.no_policy	=	1,
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
