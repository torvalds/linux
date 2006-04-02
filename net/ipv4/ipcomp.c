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
#include <linux/config.h>
#include <linux/module.h>
#include <asm/scatterlist.h>
#include <asm/semaphore.h>
#include <linux/crypto.h>
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
	struct crypto_tfm **tfms;
	int users;
};

static DEFINE_MUTEX(ipcomp_resource_mutex);
static void **ipcomp_scratches;
static int ipcomp_scratch_users;
static LIST_HEAD(ipcomp_tfms_list);

static int ipcomp_decompress(struct xfrm_state *x, struct sk_buff *skb)
{
	int err, plen, dlen;
	struct iphdr *iph;
	struct ipcomp_data *ipcd = x->data;
	u8 *start, *scratch;
	struct crypto_tfm *tfm;
	int cpu;
	
	plen = skb->len;
	dlen = IPCOMP_SCRATCH_SIZE;
	start = skb->data;

	cpu = get_cpu();
	scratch = *per_cpu_ptr(ipcomp_scratches, cpu);
	tfm = *per_cpu_ptr(ipcd->tfms, cpu);

	err = crypto_comp_decompress(tfm, start, plen, scratch, &dlen);
	if (err)
		goto out;

	if (dlen < (plen + sizeof(struct ip_comp_hdr))) {
		err = -EINVAL;
		goto out;
	}

	err = pskb_expand_head(skb, 0, dlen - plen, GFP_ATOMIC);
	if (err)
		goto out;
		
	skb_put(skb, dlen - plen);
	memcpy(skb->data, scratch, dlen);
	iph = skb->nh.iph;
	iph->tot_len = htons(dlen + iph->ihl * 4);
out:	
	put_cpu();
	return err;
}

static int ipcomp_input(struct xfrm_state *x, struct sk_buff *skb)
{
	u8 nexthdr;
	int err = 0;
	struct iphdr *iph;
	union {
		struct iphdr	iph;
		char 		buf[60];
	} tmp_iph;


	if ((skb_is_nonlinear(skb) || skb_cloned(skb)) &&
	    skb_linearize(skb, GFP_ATOMIC) != 0) {
	    	err = -ENOMEM;
	    	goto out;
	}

	skb->ip_summed = CHECKSUM_NONE;

	/* Remove ipcomp header and decompress original payload */	
	iph = skb->nh.iph;
	memcpy(&tmp_iph, iph, iph->ihl * 4);
	nexthdr = *(u8 *)skb->data;
	skb_pull(skb, sizeof(struct ip_comp_hdr));
	skb->nh.raw += sizeof(struct ip_comp_hdr);
	memcpy(skb->nh.raw, &tmp_iph, tmp_iph.iph.ihl * 4);
	iph = skb->nh.iph;
	iph->tot_len = htons(ntohs(iph->tot_len) - sizeof(struct ip_comp_hdr));
	iph->protocol = nexthdr;
	skb->h.raw = skb->data;
	err = ipcomp_decompress(x, skb);

out:	
	return err;
}

static int ipcomp_compress(struct xfrm_state *x, struct sk_buff *skb)
{
	int err, plen, dlen, ihlen;
	struct iphdr *iph = skb->nh.iph;
	struct ipcomp_data *ipcd = x->data;
	u8 *start, *scratch;
	struct crypto_tfm *tfm;
	int cpu;
	
	ihlen = iph->ihl * 4;
	plen = skb->len - ihlen;
	dlen = IPCOMP_SCRATCH_SIZE;
	start = skb->data + ihlen;

	cpu = get_cpu();
	scratch = *per_cpu_ptr(ipcomp_scratches, cpu);
	tfm = *per_cpu_ptr(ipcd->tfms, cpu);

	err = crypto_comp_compress(tfm, start, plen, scratch, &dlen);
	if (err)
		goto out;

	if ((dlen + sizeof(struct ip_comp_hdr)) >= plen) {
		err = -EMSGSIZE;
		goto out;
	}
	
	memcpy(start + sizeof(struct ip_comp_hdr), scratch, dlen);
	put_cpu();

	pskb_trim(skb, ihlen + dlen + sizeof(struct ip_comp_hdr));
	return 0;
	
out:	
	put_cpu();
	return err;
}

static int ipcomp_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;
	struct iphdr *iph;
	struct ip_comp_hdr *ipch;
	struct ipcomp_data *ipcd = x->data;
	int hdr_len = 0;

	iph = skb->nh.iph;
	iph->tot_len = htons(skb->len);
	hdr_len = iph->ihl * 4;
	if ((skb->len - hdr_len) < ipcd->threshold) {
		/* Don't bother compressing */
		goto out_ok;
	}

	if ((skb_is_nonlinear(skb) || skb_cloned(skb)) &&
	    skb_linearize(skb, GFP_ATOMIC) != 0) {
		goto out_ok;
	}
	
	err = ipcomp_compress(x, skb);
	iph = skb->nh.iph;

	if (err) {
		goto out_ok;
	}

	/* Install ipcomp header, convert into ipcomp datagram. */
	iph->tot_len = htons(skb->len);
	ipch = (struct ip_comp_hdr *)((char *)iph + iph->ihl * 4);
	ipch->nexthdr = iph->protocol;
	ipch->flags = 0;
	ipch->cpi = htons((u16 )ntohl(x->id.spi));
	iph->protocol = IPPROTO_COMP;
	ip_send_check(iph);
	return 0;

out_ok:
	if (x->props.mode)
		ip_send_check(iph);
	return 0;
}

static void ipcomp4_err(struct sk_buff *skb, u32 info)
{
	u32 spi;
	struct iphdr *iph = (struct iphdr *)skb->data;
	struct ip_comp_hdr *ipch = (struct ip_comp_hdr *)(skb->data+(iph->ihl<<2));
	struct xfrm_state *x;

	if (skb->h.icmph->type != ICMP_DEST_UNREACH ||
	    skb->h.icmph->code != ICMP_FRAG_NEEDED)
		return;

	spi = ntohl(ntohs(ipch->cpi));
	x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr,
	                      spi, IPPROTO_COMP, AF_INET);
	if (!x)
		return;
	NETDEBUG(KERN_DEBUG "pmtu discovery on SA IPCOMP/%08x/%u.%u.%u.%u\n",
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
	t->props.mode = 1;
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

	for_each_cpu(i) {
		void *scratch = *per_cpu_ptr(scratches, i);
		if (scratch)
			vfree(scratch);
	}

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

	for_each_cpu(i) {
		void *scratch = vmalloc(IPCOMP_SCRATCH_SIZE);
		if (!scratch)
			return NULL;
		*per_cpu_ptr(scratches, i) = scratch;
	}

	return scratches;
}

static void ipcomp_free_tfms(struct crypto_tfm **tfms)
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

	for_each_cpu(cpu) {
		struct crypto_tfm *tfm = *per_cpu_ptr(tfms, cpu);
		crypto_free_tfm(tfm);
	}
	free_percpu(tfms);
}

static struct crypto_tfm **ipcomp_alloc_tfms(const char *alg_name)
{
	struct ipcomp_tfms *pos;
	struct crypto_tfm **tfms;
	int cpu;

	/* This can be any valid CPU ID so we don't need locking. */
	cpu = raw_smp_processor_id();

	list_for_each_entry(pos, &ipcomp_tfms_list, list) {
		struct crypto_tfm *tfm;

		tfms = pos->tfms;
		tfm = *per_cpu_ptr(tfms, cpu);

		if (!strcmp(crypto_tfm_alg_name(tfm), alg_name)) {
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

	pos->tfms = tfms = alloc_percpu(struct crypto_tfm *);
	if (!tfms)
		goto error;

	for_each_cpu(cpu) {
		struct crypto_tfm *tfm = crypto_alloc_tfm(alg_name, 0);
		if (!tfm)
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

	err = -ENOMEM;
	ipcd = kmalloc(sizeof(*ipcd), GFP_KERNEL);
	if (!ipcd)
		goto out;

	memset(ipcd, 0, sizeof(*ipcd));
	x->props.header_len = 0;
	if (x->props.mode)
		x->props.header_len += sizeof(struct iphdr);

	mutex_lock(&ipcomp_resource_mutex);
	if (!ipcomp_alloc_scratches())
		goto error;

	ipcd->tfms = ipcomp_alloc_tfms(x->calg->alg_name);
	if (!ipcd->tfms)
		goto error;
	mutex_unlock(&ipcomp_resource_mutex);

	if (x->props.mode) {
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

static struct xfrm_type ipcomp_type = {
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

