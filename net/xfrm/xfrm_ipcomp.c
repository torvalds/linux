/*
 * IP Payload Compression Protocol (IPComp) - RFC3173.
 *
 * Copyright (c) 2003 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2003-2008 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Todo:
 *   - Tunable compression parameters.
 *   - Compression stats.
 *   - Adaptive compression.
 */

#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/vmalloc.h>
#include <net/ip.h>
#include <net/ipcomp.h>
#include <net/xfrm.h>

struct ipcomp_tfms {
	struct list_head list;
	struct crypto_comp * __percpu *tfms;
	int users;
};

static DEFINE_MUTEX(ipcomp_resource_mutex);
static void * __percpu *ipcomp_scratches;
static int ipcomp_scratch_users;
static LIST_HEAD(ipcomp_tfms_list);

static int ipcomp_decompress(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipcomp_data *ipcd = x->data;
	const int plen = skb->len;
	int dlen = IPCOMP_SCRATCH_SIZE;
	const u8 *start = skb->data;
	const int cpu = get_cpu();
	u8 *scratch = *per_cpu_ptr(ipcomp_scratches, cpu);
	struct crypto_comp *tfm = *per_cpu_ptr(ipcd->tfms, cpu);
	int err = crypto_comp_decompress(tfm, start, plen, scratch, &dlen);
	int len;

	if (err)
		goto out;

	if (dlen < (plen + sizeof(struct ip_comp_hdr))) {
		err = -EINVAL;
		goto out;
	}

	len = dlen - plen;
	if (len > skb_tailroom(skb))
		len = skb_tailroom(skb);

	__skb_put(skb, len);

	len += plen;
	skb_copy_to_linear_data(skb, scratch, len);

	while ((scratch += len, dlen -= len) > 0) {
		skb_frag_t *frag;
		struct page *page;

		err = -EMSGSIZE;
		if (WARN_ON(skb_shinfo(skb)->nr_frags >= MAX_SKB_FRAGS))
			goto out;

		frag = skb_shinfo(skb)->frags + skb_shinfo(skb)->nr_frags;
		page = alloc_page(GFP_ATOMIC);

		err = -ENOMEM;
		if (!page)
			goto out;

		__skb_frag_set_page(frag, page);

		len = PAGE_SIZE;
		if (dlen < len)
			len = dlen;

		frag->page_offset = 0;
		skb_frag_size_set(frag, len);
		memcpy(skb_frag_address(frag), scratch, len);

		skb->truesize += len;
		skb->data_len += len;
		skb->len += len;

		skb_shinfo(skb)->nr_frags++;
	}

	err = 0;

out:
	put_cpu();
	return err;
}

int ipcomp_input(struct xfrm_state *x, struct sk_buff *skb)
{
	int nexthdr;
	int err = -ENOMEM;
	struct ip_comp_hdr *ipch;

	if (skb_linearize_cow(skb))
		goto out;

	skb->ip_summed = CHECKSUM_NONE;

	/* Remove ipcomp header and decompress original payload */
	ipch = (void *)skb->data;
	nexthdr = ipch->nexthdr;

	skb->transport_header = skb->network_header + sizeof(*ipch);
	__skb_pull(skb, sizeof(*ipch));
	err = ipcomp_decompress(x, skb);
	if (err)
		goto out;

	err = nexthdr;

out:
	return err;
}
EXPORT_SYMBOL_GPL(ipcomp_input);

static int ipcomp_compress(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipcomp_data *ipcd = x->data;
	const int plen = skb->len;
	int dlen = IPCOMP_SCRATCH_SIZE;
	u8 *start = skb->data;
	struct crypto_comp *tfm;
	u8 *scratch;
	int err;

	local_bh_disable();
	scratch = *this_cpu_ptr(ipcomp_scratches);
	tfm = *this_cpu_ptr(ipcd->tfms);
	err = crypto_comp_compress(tfm, start, plen, scratch, &dlen);
	if (err)
		goto out;

	if ((dlen + sizeof(struct ip_comp_hdr)) >= plen) {
		err = -EMSGSIZE;
		goto out;
	}

	memcpy(start + sizeof(struct ip_comp_hdr), scratch, dlen);
	local_bh_enable();

	pskb_trim(skb, dlen + sizeof(struct ip_comp_hdr));
	return 0;

out:
	local_bh_enable();
	return err;
}

int ipcomp_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;
	struct ip_comp_hdr *ipch;
	struct ipcomp_data *ipcd = x->data;

	if (skb->len < ipcd->threshold) {
		/* Don't bother compressing */
		goto out_ok;
	}

	if (skb_linearize_cow(skb))
		goto out_ok;

	err = ipcomp_compress(x, skb);

	if (err) {
		goto out_ok;
	}

	/* Install ipcomp header, convert into ipcomp datagram. */
	ipch = ip_comp_hdr(skb);
	ipch->nexthdr = *skb_mac_header(skb);
	ipch->flags = 0;
	ipch->cpi = htons((u16 )ntohl(x->id.spi));
	*skb_mac_header(skb) = IPPROTO_COMP;
out_ok:
	skb_push(skb, -skb_network_offset(skb));
	return 0;
}
EXPORT_SYMBOL_GPL(ipcomp_output);

static void ipcomp_free_scratches(void)
{
	int i;
	void * __percpu *scratches;

	if (--ipcomp_scratch_users)
		return;

	scratches = ipcomp_scratches;
	if (!scratches)
		return;

	for_each_possible_cpu(i)
		vfree(*per_cpu_ptr(scratches, i));

	free_percpu(scratches);
}

static void * __percpu *ipcomp_alloc_scratches(void)
{
	void * __percpu *scratches;
	int i;

	if (ipcomp_scratch_users++)
		return ipcomp_scratches;

	scratches = alloc_percpu(void *);
	if (!scratches)
		return NULL;

	ipcomp_scratches = scratches;

	for_each_possible_cpu(i) {
		void *scratch;

		scratch = vmalloc_node(IPCOMP_SCRATCH_SIZE, cpu_to_node(i));
		if (!scratch)
			return NULL;
		*per_cpu_ptr(scratches, i) = scratch;
	}

	return scratches;
}

static void ipcomp_free_tfms(struct crypto_comp * __percpu *tfms)
{
	struct ipcomp_tfms *pos;
	int cpu;

	list_for_each_entry(pos, &ipcomp_tfms_list, list) {
		if (pos->tfms == tfms)
			break;
	}

	WARN_ON(!pos);

	if (--pos->users)
		return;

	list_del(&pos->list);
	kfree(pos);

	if (!tfms)
		return;

	for_each_possible_cpu(cpu) {
		struct crypto_comp *tfm = *per_cpu_ptr(tfms, cpu);
		crypto_free_comp(tfm);
	}
	free_percpu(tfms);
}

static struct crypto_comp * __percpu *ipcomp_alloc_tfms(const char *alg_name)
{
	struct ipcomp_tfms *pos;
	struct crypto_comp * __percpu *tfms;
	int cpu;


	list_for_each_entry(pos, &ipcomp_tfms_list, list) {
		struct crypto_comp *tfm;

		/* This can be any valid CPU ID so we don't need locking. */
		tfm = __this_cpu_read(*pos->tfms);

		if (!strcmp(crypto_comp_name(tfm), alg_name)) {
			pos->users++;
			return pos->tfms;
		}
	}

	pos = kmalloc(sizeof(*pos), GFP_KERNEL);
	if (!pos)
		return NULL;

	pos->users = 1;
	INIT_LIST_HEAD(&pos->list);
	list_add(&pos->list, &ipcomp_tfms_list);

	pos->tfms = tfms = alloc_percpu(struct crypto_comp *);
	if (!tfms)
		goto error;

	for_each_possible_cpu(cpu) {
		struct crypto_comp *tfm = crypto_alloc_comp(alg_name, 0,
							    CRYPTO_ALG_ASYNC);
		if (IS_ERR(tfm))
			goto error;
		*per_cpu_ptr(tfms, cpu) = tfm;
	}

	return tfms;

error:
	ipcomp_free_tfms(tfms);
	return NULL;
}

static void ipcomp_free_data(struct ipcomp_data *ipcd)
{
	if (ipcd->tfms)
		ipcomp_free_tfms(ipcd->tfms);
	ipcomp_free_scratches();
}

void ipcomp_destroy(struct xfrm_state *x)
{
	struct ipcomp_data *ipcd = x->data;
	if (!ipcd)
		return;
	xfrm_state_delete_tunnel(x);
	mutex_lock(&ipcomp_resource_mutex);
	ipcomp_free_data(ipcd);
	mutex_unlock(&ipcomp_resource_mutex);
	kfree(ipcd);
}
EXPORT_SYMBOL_GPL(ipcomp_destroy);

int ipcomp_init_state(struct xfrm_state *x)
{
	int err;
	struct ipcomp_data *ipcd;
	struct xfrm_algo_desc *calg_desc;

	err = -EINVAL;
	if (!x->calg)
		goto out;

	if (x->encap)
		goto out;

	err = -ENOMEM;
	ipcd = kzalloc(sizeof(*ipcd), GFP_KERNEL);
	if (!ipcd)
		goto out;

	mutex_lock(&ipcomp_resource_mutex);
	if (!ipcomp_alloc_scratches())
		goto error;

	ipcd->tfms = ipcomp_alloc_tfms(x->calg->alg_name);
	if (!ipcd->tfms)
		goto error;
	mutex_unlock(&ipcomp_resource_mutex);

	calg_desc = xfrm_calg_get_byname(x->calg->alg_name, 0);
	BUG_ON(!calg_desc);
	ipcd->threshold = calg_desc->uinfo.comp.threshold;
	x->data = ipcd;
	err = 0;
out:
	return err;

error:
	ipcomp_free_data(ipcd);
	mutex_unlock(&ipcomp_resource_mutex);
	kfree(ipcd);
	goto out;
}
EXPORT_SYMBOL_GPL(ipcomp_init_state);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IP Payload Compression Protocol (IPComp) - RFC3173");
MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");
