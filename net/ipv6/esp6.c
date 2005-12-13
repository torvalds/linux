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
 * 	This file is derived from net/ipv4/esp.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/esp.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/pfkeyv2.h>
#include <linux/random.h>
#include <net/icmp.h>
#include <net/ipv6.h>
#include <linux/icmpv6.h>

static int esp6_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;
	int hdr_len;
	struct ipv6hdr *top_iph;
	struct ipv6_esp_hdr *esph;
	struct crypto_tfm *tfm;
	struct esp_data *esp;
	struct sk_buff *trailer;
	int blksize;
	int clen;
	int alen;
	int nfrags;

	esp = x->data;
	hdr_len = skb->h.raw - skb->data +
		  sizeof(*esph) + esp->conf.ivlen;

	/* Strip IP+ESP header. */
	__skb_pull(skb, hdr_len);

	/* Now skb is pure payload to encrypt */
	err = -ENOMEM;

	/* Round to block size */
	clen = skb->len;

	alen = esp->auth.icv_trunc_len;
	tfm = esp->conf.tfm;
	blksize = ALIGN(crypto_tfm_alg_blocksize(tfm), 4);
	clen = ALIGN(clen + 2, blksize);
	if (esp->conf.padlen)
		clen = ALIGN(clen, esp->conf.padlen);

	if ((nfrags = skb_cow_data(skb, clen-skb->len+alen, &trailer)) < 0) {
		goto error;
	}

	/* Fill padding... */
	do {
		int i;
		for (i=0; i<clen-skb->len - 2; i++)
			*(u8*)(trailer->tail + i) = i+1;
	} while (0);
	*(u8*)(trailer->tail + clen-skb->len - 2) = (clen - skb->len)-2;
	pskb_put(skb, trailer, clen - skb->len);

	top_iph = (struct ipv6hdr *)__skb_push(skb, hdr_len);
	esph = (struct ipv6_esp_hdr *)skb->h.raw;
	top_iph->payload_len = htons(skb->len + alen - sizeof(*top_iph));
	*(u8*)(trailer->tail - 1) = *skb->nh.raw;
	*skb->nh.raw = IPPROTO_ESP;

	esph->spi = x->id.spi;
	esph->seq_no = htonl(++x->replay.oseq);

	if (esp->conf.ivlen)
		crypto_cipher_set_iv(tfm, esp->conf.ivec, crypto_tfm_alg_ivsize(tfm));

	do {
		struct scatterlist *sg = &esp->sgbuf[0];

		if (unlikely(nfrags > ESP_NUM_FAST_SG)) {
			sg = kmalloc(sizeof(struct scatterlist)*nfrags, GFP_ATOMIC);
			if (!sg)
				goto error;
		}
		skb_to_sgvec(skb, sg, esph->enc_data+esp->conf.ivlen-skb->data, clen);
		crypto_cipher_encrypt(tfm, sg, sg, clen);
		if (unlikely(sg != &esp->sgbuf[0]))
			kfree(sg);
	} while (0);

	if (esp->conf.ivlen) {
		memcpy(esph->enc_data, esp->conf.ivec, crypto_tfm_alg_ivsize(tfm));
		crypto_cipher_get_iv(tfm, esp->conf.ivec, crypto_tfm_alg_ivsize(tfm));
	}

	if (esp->auth.icv_full_len) {
		esp->auth.icv(esp, skb, (u8*)esph-skb->data,
			sizeof(struct ipv6_esp_hdr) + esp->conf.ivlen+clen, trailer->tail);
		pskb_put(skb, trailer, alen);
	}

	err = 0;

error:
	return err;
}

static int esp6_input(struct xfrm_state *x, struct xfrm_decap_state *decap, struct sk_buff *skb)
{
	struct ipv6hdr *iph;
	struct ipv6_esp_hdr *esph;
	struct esp_data *esp = x->data;
	struct sk_buff *trailer;
	int blksize = ALIGN(crypto_tfm_alg_blocksize(esp->conf.tfm), 4);
	int alen = esp->auth.icv_trunc_len;
	int elen = skb->len - sizeof(struct ipv6_esp_hdr) - esp->conf.ivlen - alen;

	int hdr_len = skb->h.raw - skb->nh.raw;
	int nfrags;
	unsigned char *tmp_hdr = NULL;
	int ret = 0;

	if (!pskb_may_pull(skb, sizeof(struct ipv6_esp_hdr))) {
		ret = -EINVAL;
		goto out_nofree;
	}

	if (elen <= 0 || (elen & (blksize-1))) {
		ret = -EINVAL;
		goto out_nofree;
	}

	tmp_hdr = kmalloc(hdr_len, GFP_ATOMIC);
	if (!tmp_hdr) {
		ret = -ENOMEM;
		goto out_nofree;
	}
	memcpy(tmp_hdr, skb->nh.raw, hdr_len);

	/* If integrity check is required, do this. */
        if (esp->auth.icv_full_len) {
		u8 sum[esp->auth.icv_full_len];
		u8 sum1[alen];

		esp->auth.icv(esp, skb, 0, skb->len-alen, sum);

		if (skb_copy_bits(skb, skb->len-alen, sum1, alen))
			BUG();

		if (unlikely(memcmp(sum, sum1, alen))) {
			x->stats.integrity_failed++;
			ret = -EINVAL;
			goto out;
		}
	}

	if ((nfrags = skb_cow_data(skb, 0, &trailer)) < 0) {
		ret = -EINVAL;
		goto out;
	}

	skb->ip_summed = CHECKSUM_NONE;

	esph = (struct ipv6_esp_hdr*)skb->data;
	iph = skb->nh.ipv6h;

	/* Get ivec. This can be wrong, check against another impls. */
	if (esp->conf.ivlen)
		crypto_cipher_set_iv(esp->conf.tfm, esph->enc_data, crypto_tfm_alg_ivsize(esp->conf.tfm));

        {
		u8 nexthdr[2];
		struct scatterlist *sg = &esp->sgbuf[0];
		u8 padlen;

		if (unlikely(nfrags > ESP_NUM_FAST_SG)) {
			sg = kmalloc(sizeof(struct scatterlist)*nfrags, GFP_ATOMIC);
			if (!sg) {
				ret = -ENOMEM;
				goto out;
			}
		}
		skb_to_sgvec(skb, sg, sizeof(struct ipv6_esp_hdr) + esp->conf.ivlen, elen);
		crypto_cipher_decrypt(esp->conf.tfm, sg, sg, elen);
		if (unlikely(sg != &esp->sgbuf[0]))
			kfree(sg);

		if (skb_copy_bits(skb, skb->len-alen-2, nexthdr, 2))
			BUG();

		padlen = nexthdr[0];
		if (padlen+2 >= elen) {
			LIMIT_NETDEBUG(KERN_WARNING "ipsec esp packet is garbage padlen=%d, elen=%d\n", padlen+2, elen);
			ret = -EINVAL;
			goto out;
		}
		/* ... check padding bits here. Silly. :-) */ 

		pskb_trim(skb, skb->len - alen - padlen - 2);
		skb->h.raw = skb_pull(skb, sizeof(struct ipv6_esp_hdr) + esp->conf.ivlen);
		skb->nh.raw += sizeof(struct ipv6_esp_hdr) + esp->conf.ivlen;
		memcpy(skb->nh.raw, tmp_hdr, hdr_len);
		skb->nh.ipv6h->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
		ret = nexthdr[1];
	}

out:
	kfree(tmp_hdr);
out_nofree:
	return ret;
}

static u32 esp6_get_max_size(struct xfrm_state *x, int mtu)
{
	struct esp_data *esp = x->data;
	u32 blksize = ALIGN(crypto_tfm_alg_blocksize(esp->conf.tfm), 4);

	if (x->props.mode) {
		mtu = ALIGN(mtu + 2, blksize);
	} else {
		/* The worst case. */
		u32 padsize = ((blksize - 1) & 7) + 1;
		mtu = ALIGN(mtu + 2, padsize) + blksize - padsize;
	}
	if (esp->conf.padlen)
		mtu = ALIGN(mtu, esp->conf.padlen);

	return mtu + x->props.header_len + esp->auth.icv_trunc_len;
}

static void esp6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
                     int type, int code, int offset, __u32 info)
{
	struct ipv6hdr *iph = (struct ipv6hdr*)skb->data;
	struct ipv6_esp_hdr *esph = (struct ipv6_esp_hdr*)(skb->data+offset);
	struct xfrm_state *x;

	if (type != ICMPV6_DEST_UNREACH && 
	    type != ICMPV6_PKT_TOOBIG)
		return;

	x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, esph->spi, IPPROTO_ESP, AF_INET6);
	if (!x)
		return;
	printk(KERN_DEBUG "pmtu discovery on SA ESP/%08x/"
			"%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n", 
			ntohl(esph->spi), NIP6(iph->daddr));
	xfrm_state_put(x);
}

static void esp6_destroy(struct xfrm_state *x)
{
	struct esp_data *esp = x->data;

	if (!esp)
		return;

	crypto_free_tfm(esp->conf.tfm);
	esp->conf.tfm = NULL;
	kfree(esp->conf.ivec);
	esp->conf.ivec = NULL;
	crypto_free_tfm(esp->auth.tfm);
	esp->auth.tfm = NULL;
	kfree(esp->auth.work_icv);
	esp->auth.work_icv = NULL;
	kfree(esp);
}

static int esp6_init_state(struct xfrm_state *x)
{
	struct esp_data *esp = NULL;

	/* null auth and encryption can have zero length keys */
	if (x->aalg) {
		if (x->aalg->alg_key_len > 512)
			goto error;
	}
	if (x->ealg == NULL)
		goto error;

	if (x->encap)
		goto error;

	esp = kmalloc(sizeof(*esp), GFP_KERNEL);
	if (esp == NULL)
		return -ENOMEM;

	memset(esp, 0, sizeof(*esp));

	if (x->aalg) {
		struct xfrm_algo_desc *aalg_desc;

		esp->auth.key = x->aalg->alg_key;
		esp->auth.key_len = (x->aalg->alg_key_len+7)/8;
		esp->auth.tfm = crypto_alloc_tfm(x->aalg->alg_name, 0);
		if (esp->auth.tfm == NULL)
			goto error;
		esp->auth.icv = esp_hmac_digest;
 
		aalg_desc = xfrm_aalg_get_byname(x->aalg->alg_name, 0);
		BUG_ON(!aalg_desc);
 
		if (aalg_desc->uinfo.auth.icv_fullbits/8 !=
			crypto_tfm_alg_digestsize(esp->auth.tfm)) {
				printk(KERN_INFO "ESP: %s digestsize %u != %hu\n",
					x->aalg->alg_name,
					crypto_tfm_alg_digestsize(esp->auth.tfm),
					aalg_desc->uinfo.auth.icv_fullbits/8);
				goto error;
		}
 
		esp->auth.icv_full_len = aalg_desc->uinfo.auth.icv_fullbits/8;
		esp->auth.icv_trunc_len = aalg_desc->uinfo.auth.icv_truncbits/8;
 
		esp->auth.work_icv = kmalloc(esp->auth.icv_full_len, GFP_KERNEL);
		if (!esp->auth.work_icv)
			goto error;
	}
	esp->conf.key = x->ealg->alg_key;
	esp->conf.key_len = (x->ealg->alg_key_len+7)/8;
	if (x->props.ealgo == SADB_EALG_NULL)
		esp->conf.tfm = crypto_alloc_tfm(x->ealg->alg_name, CRYPTO_TFM_MODE_ECB);
	else
		esp->conf.tfm = crypto_alloc_tfm(x->ealg->alg_name, CRYPTO_TFM_MODE_CBC);
	if (esp->conf.tfm == NULL)
		goto error;
	esp->conf.ivlen = crypto_tfm_alg_ivsize(esp->conf.tfm);
	esp->conf.padlen = 0;
	if (esp->conf.ivlen) {
		esp->conf.ivec = kmalloc(esp->conf.ivlen, GFP_KERNEL);
		if (unlikely(esp->conf.ivec == NULL))
			goto error;
		get_random_bytes(esp->conf.ivec, esp->conf.ivlen);
	}
	if (crypto_cipher_setkey(esp->conf.tfm, esp->conf.key, esp->conf.key_len))
		goto error;
	x->props.header_len = sizeof(struct ipv6_esp_hdr) + esp->conf.ivlen;
	if (x->props.mode)
		x->props.header_len += sizeof(struct ipv6hdr);
	x->data = esp;
	return 0;

error:
	x->data = esp;
	esp6_destroy(x);
	x->data = NULL;
	return -EINVAL;
}

static struct xfrm_type esp6_type =
{
	.description	= "ESP6",
	.owner	     	= THIS_MODULE,
	.proto	     	= IPPROTO_ESP,
	.init_state	= esp6_init_state,
	.destructor	= esp6_destroy,
	.get_max_size	= esp6_get_max_size,
	.input		= esp6_input,
	.output		= esp6_output
};

static struct inet6_protocol esp6_protocol = {
	.handler 	=	xfrm6_rcv,
	.err_handler	=	esp6_err,
	.flags		=	INET6_PROTO_NOPOLICY,
};

static int __init esp6_init(void)
{
	if (xfrm_register_type(&esp6_type, AF_INET6) < 0) {
		printk(KERN_INFO "ipv6 esp init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (inet6_add_protocol(&esp6_protocol, IPPROTO_ESP) < 0) {
		printk(KERN_INFO "ipv6 esp init: can't add protocol\n");
		xfrm_unregister_type(&esp6_type, AF_INET6);
		return -EAGAIN;
	}

	return 0;
}

static void __exit esp6_fini(void)
{
	if (inet6_del_protocol(&esp6_protocol, IPPROTO_ESP) < 0)
		printk(KERN_INFO "ipv6 esp close: can't remove protocol\n");
	if (xfrm_unregister_type(&esp6_type, AF_INET6) < 0)
		printk(KERN_INFO "ipv6 esp close: can't remove xfrm type\n");
}

module_init(esp6_init);
module_exit(esp6_fini);

MODULE_LICENSE("GPL");
