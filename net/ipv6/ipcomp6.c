/*
 * IP Payload Compression Protocol (IPComp) for IPv6 - RFC3173
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Author	Mitsuru KANDA  <mk@linux-ipv6.org>
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
 */
/* 
 * [Memo]
 *
 * Outbound:
 *  The compression of IP datagram MUST be done before AH/ESP processing, 
 *  fragmentation, and the addition of Hop-by-Hop/Routing header. 
 *
 * Inbound:
 *  The decompression of IP datagram MUST be done after the reassembly, 
 *  AH/ESP processing.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/ipcomp.h>
#include <asm/scatterlist.h>
#include <asm/semaphore.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <linux/random.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/rtnetlink.h>
#include <net/icmp.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/mutex.h>

struct ipcomp6_tfms {
	struct list_head list;
	struct crypto_tfm **tfms;
	int users;
};

static DEFINE_MUTEX(ipcomp6_resource_mutex);
static void **ipcomp6_scratches;
static int ipcomp6_scratch_users;
static LIST_HEAD(ipcomp6_tfms_list);

static int ipcomp6_input(struct xfrm_state *x, struct sk_buff *skb)
{
	int err = 0;
	struct ipv6hdr *iph;
	struct ipv6_comp_hdr *ipch;
	int plen, dlen;
	struct ipcomp_data *ipcd = x->data;
	u8 *start, *scratch;
	struct crypto_tfm *tfm;
	int cpu;

	if ((skb_is_nonlinear(skb) || skb_cloned(skb)) &&
		skb_linearize(skb, GFP_ATOMIC) != 0) {
		err = -ENOMEM;
		goto out;
	}

	skb->ip_summed = CHECKSUM_NONE;

	/* Remove ipcomp header and decompress original payload */
	iph = skb->nh.ipv6h;
	ipch = (void *)skb->data;
	skb->h.raw = skb->nh.raw + sizeof(*ipch);
	__skb_pull(skb, sizeof(*ipch));

	/* decompression */
	plen = skb->len;
	dlen = IPCOMP_SCRATCH_SIZE;
	start = skb->data;

	cpu = get_cpu();
	scratch = *per_cpu_ptr(ipcomp6_scratches, cpu);
	tfm = *per_cpu_ptr(ipcd->tfms, cpu);

	err = crypto_comp_decompress(tfm, start, plen, scratch, &dlen);
	if (err) {
		err = -EINVAL;
		goto out_put_cpu;
	}

	if (dlen < (plen + sizeof(struct ipv6_comp_hdr))) {
		err = -EINVAL;
		goto out_put_cpu;
	}

	err = pskb_expand_head(skb, 0, dlen - plen, GFP_ATOMIC);
	if (err) {
		goto out_put_cpu;
	}

	skb_put(skb, dlen - plen);
	memcpy(skb->data, scratch, dlen);
	err = ipch->nexthdr;

out_put_cpu:
	put_cpu();
out:
	return err;
}

static int ipcomp6_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;
	struct ipv6hdr *top_iph;
	int hdr_len;
	struct ipv6_comp_hdr *ipch;
	struct ipcomp_data *ipcd = x->data;
	int plen, dlen;
	u8 *start, *scratch;
	struct crypto_tfm *tfm;
	int cpu;

	hdr_len = skb->h.raw - skb->data;

	/* check whether datagram len is larger than threshold */
	if ((skb->len - hdr_len) < ipcd->threshold) {
		goto out_ok;
	}

	if ((skb_is_nonlinear(skb) || skb_cloned(skb)) &&
		skb_linearize(skb, GFP_ATOMIC) != 0) {
		goto out_ok;
	}

	/* compression */
	plen = skb->len - hdr_len;
	dlen = IPCOMP_SCRATCH_SIZE;
	start = skb->h.raw;

	cpu = get_cpu();
	scratch = *per_cpu_ptr(ipcomp6_scratches, cpu);
	tfm = *per_cpu_ptr(ipcd->tfms, cpu);

	err = crypto_comp_compress(tfm, start, plen, scratch, &dlen);
	if (err || (dlen + sizeof(struct ipv6_comp_hdr)) >= plen) {
		put_cpu();
		goto out_ok;
	}
	memcpy(start + sizeof(struct ip_comp_hdr), scratch, dlen);
	put_cpu();
	pskb_trim(skb, hdr_len + dlen + sizeof(struct ip_comp_hdr));

	/* insert ipcomp header and replace datagram */
	top_iph = (struct ipv6hdr *)skb->data;

	top_iph->payload_len = htons(skb->len - sizeof(struct ipv6hdr));

	ipch = (struct ipv6_comp_hdr *)start;
	ipch->nexthdr = *skb->nh.raw;
	ipch->flags = 0;
	ipch->cpi = htons((u16 )ntohl(x->id.spi));
	*skb->nh.raw = IPPROTO_COMP;

out_ok:
	return 0;
}

static void ipcomp6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		                int type, int code, int offset, __u32 info)
{
	u32 spi;
	struct ipv6hdr *iph = (struct ipv6hdr*)skb->data;
	struct ipv6_comp_hdr *ipcomph = (struct ipv6_comp_hdr*)(skb->data+offset);
	struct xfrm_state *x;

	if (type != ICMPV6_DEST_UNREACH && type != ICMPV6_PKT_TOOBIG)
		return;

	spi = htonl(ntohs(ipcomph->cpi));
	x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, spi, IPPROTO_COMP, AF_INET6);
	if (!x)
		return;

	printk(KERN_DEBUG "pmtu discovery on SA IPCOMP/%08x/" NIP6_FMT "\n",
			spi, NIP6(iph->daddr));
	xfrm_state_put(x);
}

static struct xfrm_state *ipcomp6_tunnel_create(struct xfrm_state *x)
{
	struct xfrm_state *t = NULL;

	t = xfrm_state_alloc();
	if (!t)
		goto out;

	t->id.proto = IPPROTO_IPV6;
	t->id.spi = xfrm6_tunnel_alloc_spi((xfrm_address_t *)&x->props.saddr);
	if (!t->id.spi)
		goto error;

	memcpy(t->id.daddr.a6, x->id.daddr.a6, sizeof(struct in6_addr));
	memcpy(&t->sel, &x->sel, sizeof(t->sel));
	t->props.family = AF_INET6;
	t->props.mode = 1;
	memcpy(t->props.saddr.a6, x->props.saddr.a6, sizeof(struct in6_addr));

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

static int ipcomp6_tunnel_attach(struct xfrm_state *x)
{
	int err = 0;
	struct xfrm_state *t = NULL;
	u32 spi;

	spi = xfrm6_tunnel_spi_lookup((xfrm_address_t *)&x->props.saddr);
	if (spi)
		t = xfrm_state_lookup((xfrm_address_t *)&x->id.daddr,
					      spi, IPPROTO_IPV6, AF_INET6);
	if (!t) {
		t = ipcomp6_tunnel_create(x);
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

static void ipcomp6_free_scratches(void)
{
	int i;
	void **scratches;

	if (--ipcomp6_scratch_users)
		return;

	scratches = ipcomp6_scratches;
	if (!scratches)
		return;

	for_each_possible_cpu(i) {
		void *scratch = *per_cpu_ptr(scratches, i);

		vfree(scratch);
	}

	free_percpu(scratches);
}

static void **ipcomp6_alloc_scratches(void)
{
	int i;
	void **scratches;

	if (ipcomp6_scratch_users++)
		return ipcomp6_scratches;

	scratches = alloc_percpu(void *);
	if (!scratches)
		return NULL;

	ipcomp6_scratches = scratches;

	for_each_possible_cpu(i) {
		void *scratch = vmalloc(IPCOMP_SCRATCH_SIZE);
		if (!scratch)
			return NULL;
		*per_cpu_ptr(scratches, i) = scratch;
	}

	return scratches;
}

static void ipcomp6_free_tfms(struct crypto_tfm **tfms)
{
	struct ipcomp6_tfms *pos;
	int cpu;

	list_for_each_entry(pos, &ipcomp6_tfms_list, list) {
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
		struct crypto_tfm *tfm = *per_cpu_ptr(tfms, cpu);
		crypto_free_tfm(tfm);
	}
	free_percpu(tfms);
}

static struct crypto_tfm **ipcomp6_alloc_tfms(const char *alg_name)
{
	struct ipcomp6_tfms *pos;
	struct crypto_tfm **tfms;
	int cpu;

	/* This can be any valid CPU ID so we don't need locking. */
	cpu = raw_smp_processor_id();

	list_for_each_entry(pos, &ipcomp6_tfms_list, list) {
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
	list_add(&pos->list, &ipcomp6_tfms_list);

	pos->tfms = tfms = alloc_percpu(struct crypto_tfm *);
	if (!tfms)
		goto error;

	for_each_possible_cpu(cpu) {
		struct crypto_tfm *tfm = crypto_alloc_tfm(alg_name, 0);
		if (!tfm)
			goto error;
		*per_cpu_ptr(tfms, cpu) = tfm;
	}

	return tfms;

error:
	ipcomp6_free_tfms(tfms);
	return NULL;
}

static void ipcomp6_free_data(struct ipcomp_data *ipcd)
{
	if (ipcd->tfms)
		ipcomp6_free_tfms(ipcd->tfms);
	ipcomp6_free_scratches();
}

static void ipcomp6_destroy(struct xfrm_state *x)
{
	struct ipcomp_data *ipcd = x->data;
	if (!ipcd)
		return;
	xfrm_state_delete_tunnel(x);
	mutex_lock(&ipcomp6_resource_mutex);
	ipcomp6_free_data(ipcd);
	mutex_unlock(&ipcomp6_resource_mutex);
	kfree(ipcd);

	xfrm6_tunnel_free_spi((xfrm_address_t *)&x->props.saddr);
}

static int ipcomp6_init_state(struct xfrm_state *x)
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

	x->props.header_len = 0;
	if (x->props.mode)
		x->props.header_len += sizeof(struct ipv6hdr);
	
	mutex_lock(&ipcomp6_resource_mutex);
	if (!ipcomp6_alloc_scratches())
		goto error;

	ipcd->tfms = ipcomp6_alloc_tfms(x->calg->alg_name);
	if (!ipcd->tfms)
		goto error;
	mutex_unlock(&ipcomp6_resource_mutex);

	if (x->props.mode) {
		err = ipcomp6_tunnel_attach(x);
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
	mutex_lock(&ipcomp6_resource_mutex);
error:
	ipcomp6_free_data(ipcd);
	mutex_unlock(&ipcomp6_resource_mutex);
	kfree(ipcd);

	goto out;
}

static struct xfrm_type ipcomp6_type = 
{
	.description	= "IPCOMP6",
	.owner		= THIS_MODULE,
	.proto		= IPPROTO_COMP,
	.init_state	= ipcomp6_init_state,
	.destructor	= ipcomp6_destroy,
	.input		= ipcomp6_input,
	.output		= ipcomp6_output,
};

static struct inet6_protocol ipcomp6_protocol = 
{
	.handler	= xfrm6_rcv,
	.err_handler	= ipcomp6_err,
	.flags		= INET6_PROTO_NOPOLICY,
};

static int __init ipcomp6_init(void)
{
	if (xfrm_register_type(&ipcomp6_type, AF_INET6) < 0) {
		printk(KERN_INFO "ipcomp6 init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (inet6_add_protocol(&ipcomp6_protocol, IPPROTO_COMP) < 0) {
		printk(KERN_INFO "ipcomp6 init: can't add protocol\n");
		xfrm_unregister_type(&ipcomp6_type, AF_INET6);
		return -EAGAIN;
	}
	return 0;
}

static void __exit ipcomp6_fini(void)
{
	if (inet6_del_protocol(&ipcomp6_protocol, IPPROTO_COMP) < 0) 
		printk(KERN_INFO "ipv6 ipcomp close: can't remove protocol\n");
	if (xfrm_unregister_type(&ipcomp6_type, AF_INET6) < 0)
		printk(KERN_INFO "ipv6 ipcomp close: can't remove xfrm type\n");
}

module_init(ipcomp6_init);
module_exit(ipcomp6_fini);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IP Payload Compression Protocol (IPComp) for IPv6 - RFC3173");
MODULE_AUTHOR("Mitsuru KANDA <mk@linux-ipv6.org>");


