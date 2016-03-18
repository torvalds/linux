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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors
 *
 *	Mitsuru KANDA @USAGI       : IPv6 Support
 *	Kazunori MIYAZAWA @USAGI   :
 *	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 *
 *	This file is derived from net/ipv4/esp.c
 */

#define pr_fmt(fmt) "IPv6: " fmt

#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <linux/err.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/esp.h>
#include <linux/scatterlist.h>
#include <linux/kernel.h>
#include <linux/pfkeyv2.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <net/ip6_route.h>
#include <net/icmp.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <linux/icmpv6.h>

struct esp_skb_cb {
	struct xfrm_skb_cb xfrm;
	void *tmp;
};

#define ESP_SKB_CB(__skb) ((struct esp_skb_cb *)&((__skb)->cb[0]))

static u32 esp6_get_mtu(struct xfrm_state *x, int mtu);

/*
 * Allocate an AEAD request structure with extra space for SG and IV.
 *
 * For alignment considerations the upper 32 bits of the sequence number are
 * placed at the front, if present. Followed by the IV, the request and finally
 * the SG list.
 *
 * TODO: Use spare space in skb for this where possible.
 */
static void *esp_alloc_tmp(struct crypto_aead *aead, int nfrags, int seqihlen)
{
	unsigned int len;

	len = seqihlen;

	len += crypto_aead_ivsize(aead);

	if (len) {
		len += crypto_aead_alignmask(aead) &
		       ~(crypto_tfm_ctx_alignment() - 1);
		len = ALIGN(len, crypto_tfm_ctx_alignment());
	}

	len += sizeof(struct aead_request) + crypto_aead_reqsize(aead);
	len = ALIGN(len, __alignof__(struct scatterlist));

	len += sizeof(struct scatterlist) * nfrags;

	return kmalloc(len, GFP_ATOMIC);
}

static inline __be32 *esp_tmp_seqhi(void *tmp)
{
	return PTR_ALIGN((__be32 *)tmp, __alignof__(__be32));
}

static inline u8 *esp_tmp_iv(struct crypto_aead *aead, void *tmp, int seqhilen)
{
	return crypto_aead_ivsize(aead) ?
	       PTR_ALIGN((u8 *)tmp + seqhilen,
			 crypto_aead_alignmask(aead) + 1) : tmp + seqhilen;
}

static inline struct aead_request *esp_tmp_req(struct crypto_aead *aead, u8 *iv)
{
	struct aead_request *req;

	req = (void *)PTR_ALIGN(iv + crypto_aead_ivsize(aead),
				crypto_tfm_ctx_alignment());
	aead_request_set_tfm(req, aead);
	return req;
}

static inline struct scatterlist *esp_req_sg(struct crypto_aead *aead,
					     struct aead_request *req)
{
	return (void *)ALIGN((unsigned long)(req + 1) +
			     crypto_aead_reqsize(aead),
			     __alignof__(struct scatterlist));
}

static void esp_output_done(struct crypto_async_request *base, int err)
{
	struct sk_buff *skb = base->data;

	kfree(ESP_SKB_CB(skb)->tmp);
	xfrm_output_resume(skb, err);
}

/* Move ESP header back into place. */
static void esp_restore_header(struct sk_buff *skb, unsigned int offset)
{
	struct ip_esp_hdr *esph = (void *)(skb->data + offset);
	void *tmp = ESP_SKB_CB(skb)->tmp;
	__be32 *seqhi = esp_tmp_seqhi(tmp);

	esph->seq_no = esph->spi;
	esph->spi = *seqhi;
}

static void esp_output_restore_header(struct sk_buff *skb)
{
	esp_restore_header(skb, skb_transport_offset(skb) - sizeof(__be32));
}

static void esp_output_done_esn(struct crypto_async_request *base, int err)
{
	struct sk_buff *skb = base->data;

	esp_output_restore_header(skb);
	esp_output_done(base, err);
}

static int esp6_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;
	struct ip_esp_hdr *esph;
	struct crypto_aead *aead;
	struct aead_request *req;
	struct scatterlist *sg;
	struct sk_buff *trailer;
	void *tmp;
	int blksize;
	int clen;
	int alen;
	int plen;
	int ivlen;
	int tfclen;
	int nfrags;
	int assoclen;
	int seqhilen;
	u8 *iv;
	u8 *tail;
	__be32 *seqhi;
	__be64 seqno;

	/* skb is pure payload to encrypt */
	aead = x->data;
	alen = crypto_aead_authsize(aead);
	ivlen = crypto_aead_ivsize(aead);

	tfclen = 0;
	if (x->tfcpad) {
		struct xfrm_dst *dst = (struct xfrm_dst *)skb_dst(skb);
		u32 padto;

		padto = min(x->tfcpad, esp6_get_mtu(x, dst->child_mtu_cached));
		if (skb->len < padto)
			tfclen = padto - skb->len;
	}
	blksize = ALIGN(crypto_aead_blocksize(aead), 4);
	clen = ALIGN(skb->len + 2 + tfclen, blksize);
	plen = clen - skb->len - tfclen;

	err = skb_cow_data(skb, tfclen + plen + alen, &trailer);
	if (err < 0)
		goto error;
	nfrags = err;

	assoclen = sizeof(*esph);
	seqhilen = 0;

	if (x->props.flags & XFRM_STATE_ESN) {
		seqhilen += sizeof(__be32);
		assoclen += seqhilen;
	}

	tmp = esp_alloc_tmp(aead, nfrags, seqhilen);
	if (!tmp) {
		err = -ENOMEM;
		goto error;
	}

	seqhi = esp_tmp_seqhi(tmp);
	iv = esp_tmp_iv(aead, tmp, seqhilen);
	req = esp_tmp_req(aead, iv);
	sg = esp_req_sg(aead, req);

	/* Fill padding... */
	tail = skb_tail_pointer(trailer);
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
	tail[plen - 1] = *skb_mac_header(skb);
	pskb_put(skb, trailer, clen - skb->len + alen);

	skb_push(skb, -skb_network_offset(skb));
	esph = ip_esp_hdr(skb);
	*skb_mac_header(skb) = IPPROTO_ESP;

	esph->seq_no = htonl(XFRM_SKB_CB(skb)->seq.output.low);

	aead_request_set_callback(req, 0, esp_output_done, skb);

	/* For ESN we move the header forward by 4 bytes to
	 * accomodate the high bits.  We will move it back after
	 * encryption.
	 */
	if ((x->props.flags & XFRM_STATE_ESN)) {
		esph = (void *)(skb_transport_header(skb) - sizeof(__be32));
		*seqhi = esph->spi;
		esph->seq_no = htonl(XFRM_SKB_CB(skb)->seq.output.hi);
		aead_request_set_callback(req, 0, esp_output_done_esn, skb);
	}

	esph->spi = x->id.spi;

	sg_init_table(sg, nfrags);
	skb_to_sgvec(skb, sg,
		     (unsigned char *)esph - skb->data,
		     assoclen + ivlen + clen + alen);

	aead_request_set_crypt(req, sg, sg, ivlen + clen, iv);
	aead_request_set_ad(req, assoclen);

	seqno = cpu_to_be64(XFRM_SKB_CB(skb)->seq.output.low +
			    ((u64)XFRM_SKB_CB(skb)->seq.output.hi << 32));

	memset(iv, 0, ivlen);
	memcpy(iv + ivlen - min(ivlen, 8), (u8 *)&seqno + 8 - min(ivlen, 8),
	       min(ivlen, 8));

	ESP_SKB_CB(skb)->tmp = tmp;
	err = crypto_aead_encrypt(req);

	switch (err) {
	case -EINPROGRESS:
		goto error;

	case -EBUSY:
		err = NET_XMIT_DROP;
		break;

	case 0:
		if ((x->props.flags & XFRM_STATE_ESN))
			esp_output_restore_header(skb);
	}

	kfree(tmp);

error:
	return err;
}

static int esp_input_done2(struct sk_buff *skb, int err)
{
	struct xfrm_state *x = xfrm_input_state(skb);
	struct crypto_aead *aead = x->data;
	int alen = crypto_aead_authsize(aead);
	int hlen = sizeof(struct ip_esp_hdr) + crypto_aead_ivsize(aead);
	int elen = skb->len - hlen;
	int hdr_len = skb_network_header_len(skb);
	int padlen;
	u8 nexthdr[2];

	kfree(ESP_SKB_CB(skb)->tmp);

	if (unlikely(err))
		goto out;

	if (skb_copy_bits(skb, skb->len - alen - 2, nexthdr, 2))
		BUG();

	err = -EINVAL;
	padlen = nexthdr[0];
	if (padlen + 2 + alen >= elen) {
		net_dbg_ratelimited("ipsec esp packet is garbage padlen=%d, elen=%d\n",
				    padlen + 2, elen - alen);
		goto out;
	}

	/* ... check padding bits here. Silly. :-) */

	pskb_trim(skb, skb->len - alen - padlen - 2);
	__skb_pull(skb, hlen);
	if (x->props.mode == XFRM_MODE_TUNNEL)
		skb_reset_transport_header(skb);
	else
		skb_set_transport_header(skb, -hdr_len);

	err = nexthdr[1];

	/* RFC4303: Drop dummy packets without any error */
	if (err == IPPROTO_NONE)
		err = -EINVAL;

out:
	return err;
}

static void esp_input_done(struct crypto_async_request *base, int err)
{
	struct sk_buff *skb = base->data;

	xfrm_input_resume(skb, esp_input_done2(skb, err));
}

static void esp_input_restore_header(struct sk_buff *skb)
{
	esp_restore_header(skb, 0);
	__skb_pull(skb, 4);
}

static void esp_input_done_esn(struct crypto_async_request *base, int err)
{
	struct sk_buff *skb = base->data;

	esp_input_restore_header(skb);
	esp_input_done(base, err);
}

static int esp6_input(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ip_esp_hdr *esph;
	struct crypto_aead *aead = x->data;
	struct aead_request *req;
	struct sk_buff *trailer;
	int ivlen = crypto_aead_ivsize(aead);
	int elen = skb->len - sizeof(*esph) - ivlen;
	int nfrags;
	int assoclen;
	int seqhilen;
	int ret = 0;
	void *tmp;
	__be32 *seqhi;
	u8 *iv;
	struct scatterlist *sg;

	if (!pskb_may_pull(skb, sizeof(*esph) + ivlen)) {
		ret = -EINVAL;
		goto out;
	}

	if (elen <= 0) {
		ret = -EINVAL;
		goto out;
	}

	nfrags = skb_cow_data(skb, 0, &trailer);
	if (nfrags < 0) {
		ret = -EINVAL;
		goto out;
	}

	ret = -ENOMEM;

	assoclen = sizeof(*esph);
	seqhilen = 0;

	if (x->props.flags & XFRM_STATE_ESN) {
		seqhilen += sizeof(__be32);
		assoclen += seqhilen;
	}

	tmp = esp_alloc_tmp(aead, nfrags, seqhilen);
	if (!tmp)
		goto out;

	ESP_SKB_CB(skb)->tmp = tmp;
	seqhi = esp_tmp_seqhi(tmp);
	iv = esp_tmp_iv(aead, tmp, seqhilen);
	req = esp_tmp_req(aead, iv);
	sg = esp_req_sg(aead, req);

	skb->ip_summed = CHECKSUM_NONE;

	esph = (struct ip_esp_hdr *)skb->data;

	aead_request_set_callback(req, 0, esp_input_done, skb);

	/* For ESN we move the header forward by 4 bytes to
	 * accomodate the high bits.  We will move it back after
	 * decryption.
	 */
	if ((x->props.flags & XFRM_STATE_ESN)) {
		esph = (void *)skb_push(skb, 4);
		*seqhi = esph->spi;
		esph->spi = esph->seq_no;
		esph->seq_no = htonl(XFRM_SKB_CB(skb)->seq.input.hi);
		aead_request_set_callback(req, 0, esp_input_done_esn, skb);
	}

	sg_init_table(sg, nfrags);
	skb_to_sgvec(skb, sg, 0, skb->len);

	aead_request_set_crypt(req, sg, sg, elen + ivlen, iv);
	aead_request_set_ad(req, assoclen);

	ret = crypto_aead_decrypt(req);
	if (ret == -EINPROGRESS)
		goto out;

	if ((x->props.flags & XFRM_STATE_ESN))
		esp_input_restore_header(skb);

	ret = esp_input_done2(skb, ret);

out:
	return ret;
}

static u32 esp6_get_mtu(struct xfrm_state *x, int mtu)
{
	struct crypto_aead *aead = x->data;
	u32 blksize = ALIGN(crypto_aead_blocksize(aead), 4);
	unsigned int net_adj;

	if (x->props.mode != XFRM_MODE_TUNNEL)
		net_adj = sizeof(struct ipv6hdr);
	else
		net_adj = 0;

	return ((mtu - x->props.header_len - crypto_aead_authsize(aead) -
		 net_adj) & ~(blksize - 1)) + net_adj - 2;
}

static int esp6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		    u8 type, u8 code, int offset, __be32 info)
{
	struct net *net = dev_net(skb->dev);
	const struct ipv6hdr *iph = (const struct ipv6hdr *)skb->data;
	struct ip_esp_hdr *esph = (struct ip_esp_hdr *)(skb->data + offset);
	struct xfrm_state *x;

	if (type != ICMPV6_PKT_TOOBIG &&
	    type != NDISC_REDIRECT)
		return 0;

	x = xfrm_state_lookup(net, skb->mark, (const xfrm_address_t *)&iph->daddr,
			      esph->spi, IPPROTO_ESP, AF_INET6);
	if (!x)
		return 0;

	if (type == NDISC_REDIRECT)
		ip6_redirect(skb, net, skb->dev->ifindex, 0);
	else
		ip6_update_pmtu(skb, net, info, 0, 0, INVALID_UID);
	xfrm_state_put(x);

	return 0;
}

static void esp6_destroy(struct xfrm_state *x)
{
	struct crypto_aead *aead = x->data;

	if (!aead)
		return;

	crypto_free_aead(aead);
}

static int esp_init_aead(struct xfrm_state *x)
{
	char aead_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_aead *aead;
	int err;

	err = -ENAMETOOLONG;
	if (snprintf(aead_name, CRYPTO_MAX_ALG_NAME, "%s(%s)",
		     x->geniv, x->aead->alg_name) >= CRYPTO_MAX_ALG_NAME)
		goto error;

	aead = crypto_alloc_aead(aead_name, 0, 0);
	err = PTR_ERR(aead);
	if (IS_ERR(aead))
		goto error;

	x->data = aead;

	err = crypto_aead_setkey(aead, x->aead->alg_key,
				 (x->aead->alg_key_len + 7) / 8);
	if (err)
		goto error;

	err = crypto_aead_setauthsize(aead, x->aead->alg_icv_len / 8);
	if (err)
		goto error;

error:
	return err;
}

static int esp_init_authenc(struct xfrm_state *x)
{
	struct crypto_aead *aead;
	struct crypto_authenc_key_param *param;
	struct rtattr *rta;
	char *key;
	char *p;
	char authenc_name[CRYPTO_MAX_ALG_NAME];
	unsigned int keylen;
	int err;

	err = -EINVAL;
	if (!x->ealg)
		goto error;

	err = -ENAMETOOLONG;

	if ((x->props.flags & XFRM_STATE_ESN)) {
		if (snprintf(authenc_name, CRYPTO_MAX_ALG_NAME,
			     "%s%sauthencesn(%s,%s)%s",
			     x->geniv ?: "", x->geniv ? "(" : "",
			     x->aalg ? x->aalg->alg_name : "digest_null",
			     x->ealg->alg_name,
			     x->geniv ? ")" : "") >= CRYPTO_MAX_ALG_NAME)
			goto error;
	} else {
		if (snprintf(authenc_name, CRYPTO_MAX_ALG_NAME,
			     "%s%sauthenc(%s,%s)%s",
			     x->geniv ?: "", x->geniv ? "(" : "",
			     x->aalg ? x->aalg->alg_name : "digest_null",
			     x->ealg->alg_name,
			     x->geniv ? ")" : "") >= CRYPTO_MAX_ALG_NAME)
			goto error;
	}

	aead = crypto_alloc_aead(authenc_name, 0, 0);
	err = PTR_ERR(aead);
	if (IS_ERR(aead))
		goto error;

	x->data = aead;

	keylen = (x->aalg ? (x->aalg->alg_key_len + 7) / 8 : 0) +
		 (x->ealg->alg_key_len + 7) / 8 + RTA_SPACE(sizeof(*param));
	err = -ENOMEM;
	key = kmalloc(keylen, GFP_KERNEL);
	if (!key)
		goto error;

	p = key;
	rta = (void *)p;
	rta->rta_type = CRYPTO_AUTHENC_KEYA_PARAM;
	rta->rta_len = RTA_LENGTH(sizeof(*param));
	param = RTA_DATA(rta);
	p += RTA_SPACE(sizeof(*param));

	if (x->aalg) {
		struct xfrm_algo_desc *aalg_desc;

		memcpy(p, x->aalg->alg_key, (x->aalg->alg_key_len + 7) / 8);
		p += (x->aalg->alg_key_len + 7) / 8;

		aalg_desc = xfrm_aalg_get_byname(x->aalg->alg_name, 0);
		BUG_ON(!aalg_desc);

		err = -EINVAL;
		if (aalg_desc->uinfo.auth.icv_fullbits / 8 !=
		    crypto_aead_authsize(aead)) {
			pr_info("ESP: %s digestsize %u != %hu\n",
				x->aalg->alg_name,
				crypto_aead_authsize(aead),
				aalg_desc->uinfo.auth.icv_fullbits / 8);
			goto free_key;
		}

		err = crypto_aead_setauthsize(
			aead, x->aalg->alg_trunc_len / 8);
		if (err)
			goto free_key;
	}

	param->enckeylen = cpu_to_be32((x->ealg->alg_key_len + 7) / 8);
	memcpy(p, x->ealg->alg_key, (x->ealg->alg_key_len + 7) / 8);

	err = crypto_aead_setkey(aead, key, keylen);

free_key:
	kfree(key);

error:
	return err;
}

static int esp6_init_state(struct xfrm_state *x)
{
	struct crypto_aead *aead;
	u32 align;
	int err;

	if (x->encap)
		return -EINVAL;

	x->data = NULL;

	if (x->aead)
		err = esp_init_aead(x);
	else
		err = esp_init_authenc(x);

	if (err)
		goto error;

	aead = x->data;

	x->props.header_len = sizeof(struct ip_esp_hdr) +
			      crypto_aead_ivsize(aead);
	switch (x->props.mode) {
	case XFRM_MODE_BEET:
		if (x->sel.family != AF_INET6)
			x->props.header_len += IPV4_BEET_PHMAXLEN +
					       (sizeof(struct ipv6hdr) - sizeof(struct iphdr));
		break;
	case XFRM_MODE_TRANSPORT:
		break;
	case XFRM_MODE_TUNNEL:
		x->props.header_len += sizeof(struct ipv6hdr);
		break;
	default:
		goto error;
	}

	align = ALIGN(crypto_aead_blocksize(aead), 4);
	x->props.trailer_len = align + 1 + crypto_aead_authsize(aead);

error:
	return err;
}

static int esp6_rcv_cb(struct sk_buff *skb, int err)
{
	return 0;
}

static const struct xfrm_type esp6_type = {
	.description	= "ESP6",
	.owner		= THIS_MODULE,
	.proto		= IPPROTO_ESP,
	.flags		= XFRM_TYPE_REPLAY_PROT,
	.init_state	= esp6_init_state,
	.destructor	= esp6_destroy,
	.get_mtu	= esp6_get_mtu,
	.input		= esp6_input,
	.output		= esp6_output,
	.hdr_offset	= xfrm6_find_1stfragopt,
};

static struct xfrm6_protocol esp6_protocol = {
	.handler	=	xfrm6_rcv,
	.cb_handler	=	esp6_rcv_cb,
	.err_handler	=	esp6_err,
	.priority	=	0,
};

static int __init esp6_init(void)
{
	if (xfrm_register_type(&esp6_type, AF_INET6) < 0) {
		pr_info("%s: can't add xfrm type\n", __func__);
		return -EAGAIN;
	}
	if (xfrm6_protocol_register(&esp6_protocol, IPPROTO_ESP) < 0) {
		pr_info("%s: can't add protocol\n", __func__);
		xfrm_unregister_type(&esp6_type, AF_INET6);
		return -EAGAIN;
	}

	return 0;
}

static void __exit esp6_fini(void)
{
	if (xfrm6_protocol_deregister(&esp6_protocol, IPPROTO_ESP) < 0)
		pr_info("%s: can't remove protocol\n", __func__);
	if (xfrm_unregister_type(&esp6_type, AF_INET6) < 0)
		pr_info("%s: can't remove xfrm type\n", __func__);
}

module_init(esp6_init);
module_exit(esp6_fini);

MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_TYPE(AF_INET6, XFRM_PROTO_ESP);
