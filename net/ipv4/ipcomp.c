/*
 * IP Payload Compression Protocol (IPComp) - RFC3173.
 *
 * Copyright (c) 2003 James Morris <jmorris@intercode.com.au>
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
#include <linux/module.h>
#include <asm/semaphore.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/pfkeyv2.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/rtnetlink.h>
#include <linux/mutex.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/icmp.h>
#include <net/ipcomp.h>
#include <net/protocol.h>

struct ipcomp_tfms {
	struct list_head list;
	struct crypto_comp **tfms;
	int users;
};

static DEFINE_MUTEX(ipcomp_resource_mutex);
static void **ipcomp_scratches;
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

	if (err)
		goto out;

	if (dlen < (plen + sizeof(struct ip_comp_hdr))) {
		err = -EINVAL;
		goto out;
	}

	err = pskb_expand_head(skb, 0, dlen - plen, GFP_ATOMIC);
	if (err)
		goto out;

	skb->truesize += dlen - plen;
	__skb_put(skb, dlen - plen);
	skb_copy_to_linear_data(skb, scratch, dlen);
out:
	put_cpu();
	return err;
}

static int ipcomp_input(struct xfrm_state *x, struct sk_buff *skb)
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

static int ipcomp_compress(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipcomp_data *ipcd = x->data;
	const int plen = skb->len;
	int dlen = IPCOMP_SCRATCH_SIZE;
	u8 *start = skb->data;
	const int cpu = get_cpu();
	u8 *scratch = *per_cpu_ptr(ipcomp_scratches, cpu);
	struct crypto_comp *tfm = *per_cpu_ptr(ipcd->tfms, cpu);
	int err;

	local_bh_disable();
	err = crypto_comp_compress(tfm, start, plen, scratch, &dlen);
	local_bh_enable();
	if (err)
		goto out;

	if ((dlen + sizeof(struct ip_comp_hdr)) >= plen) {
		err = -EMSGSIZE;
		goto out;
	}

	memcpy(start + sizeof(struct ip_comp_hdr), scratch, dlen);
	put_cpu();

	pskb_trim(skb, dlen + sizeof(struct ip_comp_hdr));
	return 0;

out:
	put_cpu();
	return err;
}

static int ipcomp_output(struct xfrm_state *x, struct sk_buff *skb)
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

static void ipcomp4_err(struct sk_buff *skb, u32 info)
{
	__be32 spi;
	struct iphdr *iph = (struct iphdr *)skb->data;
	struct ip_comp_hdr *ipch = (struct ip_comp_hdr *)(skb->data+(iph->ihl<<2));
	struct xfrm_state *x;

	if (icmp_hdr(skb)->type != ICMP_DEST_UNREACH ||
	    icmp_hdr(skb)->code != ICMP_FRAG_NEEDED)
		return;

	spi = htonl(ntohs(ipch->cpi));
	x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr,
			      spi, IPPROTO_COMP, AF_INET);
	if (!x)
		return;
	NETDEBUG(KERN_DEBUG "pmtu discovery on SA IPCOMP/%08x/" NIPQUAD_FMT "\n",
		 spi, NIPQUAD(iph->daddr));
	xfrm_state_put(x);
}

/* We always hold one tunnel user reference to indicate a tunnel */
static struct xfrm_state *ipcomp_tunnel_create(struct xfrm_state *x)
{
	struct xfrm_state *t;

	t = xfrm_state_alloc();
	if (t == NULL)
		goto out;

	t->id.proto = IPPROTO_IPIP;
	t->id.spi = x->props.saddr.a4;
	t->id.daddr.a4 = x->id.daddr.a4;
	memcpy(&t->sel, &x->sel, sizeof(t->sel));
	t->props.family = AF_INET;
	t->props.mode = x->props.mode;
	t->props.saddr.a4 = x->props.saddr.a4;
	t->props.flags = x->props.flags;

	if (xfrm_init_state(t))
		goto error;

	atomic_set(&t->tunnel_users, 1);
out:
	return t;

error:
	t->km.state = XFRM_STATE_DEAD;
	xfrm_state_put(t);
	t = NULL;
	goto out;
}

/*
 * Must be protected by xfrm_cfg_mutex.  State and tunnel user references are
 * always incremented on success.
 */
static int ipcomp_tunnel_attach(struct xfrm_state *x)
{
	int err = 0;
	struct xfrm_state *t;

	t = xfrm_state_lookup((xfrm_address_t *)&x->id.daddr.a4,
			      x->props.saddr.a4, IPPROTO_IPIP, AF_INET);
	if (!t) {
		t = ipcomp_tunnel_create(x);
		if (!t) {
			err = -EINVAL;
			goto out;
		}
		xfrm_state_insert(t);
		xfrm_state_hold(t);
	}
	x->tunnel = t;
	atomic_inc(&t->tunnel_users);
out:
	return err;
}

static void ipcomp_free_scratches(void)
{
	int i;
	void **scratches;

	if (--ipcomp_scratch_users)
		return;

	scratches = ipcomp_scratches;
	if (!scratches)
		return;

	for_each_possible_cpu(i)
		vfree(*per_cpu_ptr(scratches, i));

	free_percpu(scratches);
}

static void **ipcomp_alloc_scratches(void)
{
	int i;
	void **scratches;

	if (ipcomp_scratch_users++)
		return ipcomp_scratches;

	scratches = alloc_percpu(void *);
	if (!scratches)
		return NULL;

	ipcomp_scratches = scratches;

	for_each_possible_cpu(i) {
		void *scratch = vmalloc(IPCOMP_SCRATCH_SIZE);
		if (!scratch)
			return NULL;
		*per_cpu_ptr(scratches, i) = scratch;
	}

	return scratches;
}

static void ipcomp_free_tfms(struct crypto_comp **tfms)
{
	struct ipcomp_tfms *pos;
	int cpu;

	list_for_each_entry(pos, &ipcomp_tfms_list, list) {
		if (pos->tfms == tfms)
			break;
	}

	BUG_TRAP(pos);

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

static struct crypto_comp **ipcomp_alloc_tfms(const char *alg_name)
{
	struct ipcomp_tfms *pos;
	struct crypto_comp **tfms;
	int cpu;

	/* This can be any valid CPU ID so we don't need locking. */
	cpu = raw_smp_processor_id();

	list_for_each_entry(pos, &ipcomp_tfms_list, list) {
		struct crypto_comp *tfm;

		tfms = pos->tfms;
		tfm = *per_cpu_ptr(tfms, cpu);

		if (!strcmp(crypto_comp_name(tfm), alg_name)) {
			pos->users++;
			return tfms;
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

static void ipcomp_destroy(struct xfrm_state *x)
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

static int ipcomp_init_state(struct xfrm_state *x)
{
	int err;
	struct ipcomp_data *ipcd;
	struct xfrm_algo_desc *calg_desc;

	err = -EINVAL;
	if (!x->calg)
		goto out;

	if (x->encap)
		goto out;

	x->props.header_len = 0;
	switch (x->props.mode) {
	case XFRM_MODE_TRANSPORT:
		break;
	case XFRM_MODE_TUNNEL:
		x->props.header_len += sizeof(struct iphdr);
		break;
	default:
		goto out;
	}

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

	if (x->props.mode == XFRM_MODE_TUNNEL) {
		err = ipcomp_tunnel_attach(x);
		if (err)
			goto error_tunnel;
	}

	calg_desc = xfrm_calg_get_byname(x->calg->alg_name, 0);
	BUG_ON(!calg_desc);
	ipcd->threshold = calg_desc->uinfo.comp.threshold;
	x->data = ipcd;
	err = 0;
out:
	return err;

error_tunnel:
	mutex_lock(&ipcomp_resource_mutex);
error:
	ipcomp_free_data(ipcd);
	mutex_unlock(&ipcomp_resource_mutex);
	kfree(ipcd);
	goto out;
}

static const struct xfrm_type ipcomp_type = {
	.description	= "IPCOMP4",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_COMP,
	.init_state	= ipcomp_init_state,
	.destructor	= ipcomp_destroy,
	.input		= ipcomp_input,
	.output		= ipcomp_output
};

static struct net_protocol ipcomp4_protocol = {
	.handler	=	xfrm4_rcv,
	.err_handler	=	ipcomp4_err,
	.no_policy	=	1,
};

static int __init ipcomp4_init(void)
{
	if (xfrm_register_type(&ipcomp_type, AF_INET) < 0) {
		printk(KERN_INFO "ipcomp init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (inet_add_protocol(&ipcomp4_protocol, IPPROTO_COMP) < 0) {
		printk(KERN_INFO "ipcomp init: can't add protocol\n");
		xfrm_unregister_type(&ipcomp_type, AF_INET);
		return -EAGAIN;
	}
	return 0;
}

static void __exit ipcomp4_fini(void)
{
	if (inet_del_protocol(&ipcomp4_protocol, IPPROTO_COMP) < 0)
		printk(KERN_INFO "ip ipcomp close: can't remove protocol\n");
	if (xfrm_unregister_type(&ipcomp_type, AF_INET) < 0)
		printk(KERN_INFO "ip ipcomp close: can't remove xfrm type\n");
}

module_init(ipcomp4_init);
module_exit(ipcomp4_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IP Payload Compression Protocol (IPComp) - RFC3173");
MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");

MODULE_ALIAS_XFRM_TYPE(AF_INET, XFRM_PROTO_COMP);
