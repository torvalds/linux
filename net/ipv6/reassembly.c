// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IPv6 fragment reassembly
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Based on: net/ipv4/ip_fragment.c
 */

/*
 *	Fixes:
 *	Andi Kleen	Make it work with multiple hosts.
 *			More RFC compliance.
 *
 *      Horst von Brand Add missing #include <linux/string.h>
 *	Alexey Kuznetsov	SMP races, threading, cleanup.
 *	Patrick McHardy		LRU queue of frag heads for evictor.
 *	Mitsuru KANDA @USAGI	Register inet6_protocol{}.
 *	David Stevens and
 *	YOSHIFUJI,H. @USAGI	Always remove fragment header to
 *				calculate ICV correctly.
 */

#define pr_fmt(fmt) "IPv6: " fmt

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/jiffies.h>
#include <linux/net.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/random.h>
#include <linux/jhash.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/export.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/ipv6_frag.h>
#include <net/inet_ecn.h>

static const char ip6_frag_cache_name[] = "ip6-frags";

static u8 ip6_frag_ecn(const struct ipv6hdr *ipv6h)
{
	return 1 << (ipv6_get_dsfield(ipv6h) & INET_ECN_MASK);
}

static struct inet_frags ip6_frags;

static int ip6_frag_reasm(struct frag_queue *fq, struct sk_buff *skb,
			  struct sk_buff *prev_tail, struct net_device *dev);

static void ip6_frag_expire(struct timer_list *t)
{
	struct inet_frag_queue *frag = from_timer(frag, t, timer);
	struct frag_queue *fq;
	struct net *net;

	fq = container_of(frag, struct frag_queue, q);
	net = container_of(fq->q.net, struct net, ipv6.frags);

	ip6frag_expire_frag_queue(net, fq);
}

static struct frag_queue *
fq_find(struct net *net, __be32 id, const struct ipv6hdr *hdr, int iif)
{
	struct frag_v6_compare_key key = {
		.id = id,
		.saddr = hdr->saddr,
		.daddr = hdr->daddr,
		.user = IP6_DEFRAG_LOCAL_DELIVER,
		.iif = iif,
	};
	struct inet_frag_queue *q;

	if (!(ipv6_addr_type(&hdr->daddr) & (IPV6_ADDR_MULTICAST |
					    IPV6_ADDR_LINKLOCAL)))
		key.iif = 0;

	q = inet_frag_find(&net->ipv6.frags, &key);
	if (!q)
		return NULL;

	return container_of(q, struct frag_queue, q);
}

static int ip6_frag_queue(struct frag_queue *fq, struct sk_buff *skb,
			  struct frag_hdr *fhdr, int nhoff,
			  u32 *prob_offset)
{
	struct net *net = dev_net(skb_dst(skb)->dev);
	int offset, end, fragsize;
	struct sk_buff *prev_tail;
	struct net_device *dev;
	int err = -ENOENT;
	u8 ecn;

	if (fq->q.flags & INET_FRAG_COMPLETE)
		goto err;

	err = -EINVAL;
	offset = ntohs(fhdr->frag_off) & ~0x7;
	end = offset + (ntohs(ipv6_hdr(skb)->payload_len) -
			((u8 *)(fhdr + 1) - (u8 *)(ipv6_hdr(skb) + 1)));

	if ((unsigned int)end > IPV6_MAXPLEN) {
		*prob_offset = (u8 *)&fhdr->frag_off - skb_network_header(skb);
		/* note that if prob_offset is set, the skb is freed elsewhere,
		 * we do not free it here.
		 */
		return -1;
	}

	ecn = ip6_frag_ecn(ipv6_hdr(skb));

	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		const unsigned char *nh = skb_network_header(skb);
		skb->csum = csum_sub(skb->csum,
				     csum_partial(nh, (u8 *)(fhdr + 1) - nh,
						  0));
	}

	/* Is this the final fragment? */
	if (!(fhdr->frag_off & htons(IP6_MF))) {
		/* If we already have some bits beyond end
		 * or have different end, the segment is corrupted.
		 */
		if (end < fq->q.len ||
		    ((fq->q.flags & INET_FRAG_LAST_IN) && end != fq->q.len))
			goto discard_fq;
		fq->q.flags |= INET_FRAG_LAST_IN;
		fq->q.len = end;
	} else {
		/* Check if the fragment is rounded to 8 bytes.
		 * Required by the RFC.
		 */
		if (end & 0x7) {
			/* RFC2460 says always send parameter problem in
			 * this case. -DaveM
			 */
			*prob_offset = offsetof(struct ipv6hdr, payload_len);
			return -1;
		}
		if (end > fq->q.len) {
			/* Some bits beyond end -> corruption. */
			if (fq->q.flags & INET_FRAG_LAST_IN)
				goto discard_fq;
			fq->q.len = end;
		}
	}

	if (end == offset)
		goto discard_fq;

	err = -ENOMEM;
	/* Point into the IP datagram 'data' part. */
	if (!pskb_pull(skb, (u8 *) (fhdr + 1) - skb->data))
		goto discard_fq;

	err = pskb_trim_rcsum(skb, end - offset);
	if (err)
		goto discard_fq;

	/* Note : skb->rbnode and skb->dev share the same location. */
	dev = skb->dev;
	/* Makes sure compiler wont do silly aliasing games */
	barrier();

	prev_tail = fq->q.fragments_tail;
	err = inet_frag_queue_insert(&fq->q, skb, offset, end);
	if (err)
		goto insert_error;

	if (dev)
		fq->iif = dev->ifindex;

	fq->q.stamp = skb->tstamp;
	fq->q.meat += skb->len;
	fq->ecn |= ecn;
	add_frag_mem_limit(fq->q.net, skb->truesize);

	fragsize = -skb_network_offset(skb) + skb->len;
	if (fragsize > fq->q.max_size)
		fq->q.max_size = fragsize;

	/* The first fragment.
	 * nhoffset is obtained from the first fragment, of course.
	 */
	if (offset == 0) {
		fq->nhoffset = nhoff;
		fq->q.flags |= INET_FRAG_FIRST_IN;
	}

	if (fq->q.flags == (INET_FRAG_FIRST_IN | INET_FRAG_LAST_IN) &&
	    fq->q.meat == fq->q.len) {
		unsigned long orefdst = skb->_skb_refdst;

		skb->_skb_refdst = 0UL;
		err = ip6_frag_reasm(fq, skb, prev_tail, dev);
		skb->_skb_refdst = orefdst;
		return err;
	}

	skb_dst_drop(skb);
	return -EINPROGRESS;

insert_error:
	if (err == IPFRAG_DUP) {
		kfree_skb(skb);
		return -EINVAL;
	}
	err = -EINVAL;
	__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
			IPSTATS_MIB_REASM_OVERLAPS);
discard_fq:
	inet_frag_kill(&fq->q);
	__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
			IPSTATS_MIB_REASMFAILS);
err:
	kfree_skb(skb);
	return err;
}

/*
 *	Check if this packet is complete.
 *
 *	It is called with locked fq, and caller must check that
 *	queue is eligible for reassembly i.e. it is not COMPLETE,
 *	the last and the first frames arrived and all the bits are here.
 */
static int ip6_frag_reasm(struct frag_queue *fq, struct sk_buff *skb,
			  struct sk_buff *prev_tail, struct net_device *dev)
{
	struct net *net = container_of(fq->q.net, struct net, ipv6.frags);
	unsigned int nhoff;
	void *reasm_data;
	int payload_len;
	u8 ecn;

	inet_frag_kill(&fq->q);

	ecn = ip_frag_ecn_table[fq->ecn];
	if (unlikely(ecn == 0xff))
		goto out_fail;

	reasm_data = inet_frag_reasm_prepare(&fq->q, skb, prev_tail);
	if (!reasm_data)
		goto out_oom;

	payload_len = ((skb->data - skb_network_header(skb)) -
		       sizeof(struct ipv6hdr) + fq->q.len -
		       sizeof(struct frag_hdr));
	if (payload_len > IPV6_MAXPLEN)
		goto out_oversize;

	/* We have to remove fragment header from datagram and to relocate
	 * header in order to calculate ICV correctly. */
	nhoff = fq->nhoffset;
	skb_network_header(skb)[nhoff] = skb_transport_header(skb)[0];
	memmove(skb->head + sizeof(struct frag_hdr), skb->head,
		(skb->data - skb->head) - sizeof(struct frag_hdr));
	if (skb_mac_header_was_set(skb))
		skb->mac_header += sizeof(struct frag_hdr);
	skb->network_header += sizeof(struct frag_hdr);

	skb_reset_transport_header(skb);

	inet_frag_reasm_finish(&fq->q, skb, reasm_data);

	skb->dev = dev;
	ipv6_hdr(skb)->payload_len = htons(payload_len);
	ipv6_change_dsfield(ipv6_hdr(skb), 0xff, ecn);
	IP6CB(skb)->nhoff = nhoff;
	IP6CB(skb)->flags |= IP6SKB_FRAGMENTED;
	IP6CB(skb)->frag_max_size = fq->q.max_size;

	/* Yes, and fold redundant checksum back. 8) */
	skb_postpush_rcsum(skb, skb_network_header(skb),
			   skb_network_header_len(skb));

	rcu_read_lock();
	__IP6_INC_STATS(net, __in6_dev_stats_get(dev, skb), IPSTATS_MIB_REASMOKS);
	rcu_read_unlock();
	fq->q.rb_fragments = RB_ROOT;
	fq->q.fragments_tail = NULL;
	fq->q.last_run_head = NULL;
	return 1;

out_oversize:
	net_dbg_ratelimited("ip6_frag_reasm: payload len = %d\n", payload_len);
	goto out_fail;
out_oom:
	net_dbg_ratelimited("ip6_frag_reasm: no memory for reassembly\n");
out_fail:
	rcu_read_lock();
	__IP6_INC_STATS(net, __in6_dev_stats_get(dev, skb), IPSTATS_MIB_REASMFAILS);
	rcu_read_unlock();
	inet_frag_kill(&fq->q);
	return -1;
}

static int ipv6_frag_rcv(struct sk_buff *skb)
{
	struct frag_hdr *fhdr;
	struct frag_queue *fq;
	const struct ipv6hdr *hdr = ipv6_hdr(skb);
	struct net *net = dev_net(skb_dst(skb)->dev);
	int iif;

	if (IP6CB(skb)->flags & IP6SKB_FRAGMENTED)
		goto fail_hdr;

	__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)), IPSTATS_MIB_REASMREQDS);

	/* Jumbo payload inhibits frag. header */
	if (hdr->payload_len == 0)
		goto fail_hdr;

	if (!pskb_may_pull(skb, (skb_transport_offset(skb) +
				 sizeof(struct frag_hdr))))
		goto fail_hdr;

	hdr = ipv6_hdr(skb);
	fhdr = (struct frag_hdr *)skb_transport_header(skb);

	if (!(fhdr->frag_off & htons(0xFFF9))) {
		/* It is not a fragmented frame */
		skb->transport_header += sizeof(struct frag_hdr);
		__IP6_INC_STATS(net,
				ip6_dst_idev(skb_dst(skb)), IPSTATS_MIB_REASMOKS);

		IP6CB(skb)->nhoff = (u8 *)fhdr - skb_network_header(skb);
		IP6CB(skb)->flags |= IP6SKB_FRAGMENTED;
		return 1;
	}

	iif = skb->dev ? skb->dev->ifindex : 0;
	fq = fq_find(net, fhdr->identification, hdr, iif);
	if (fq) {
		u32 prob_offset = 0;
		int ret;

		spin_lock(&fq->q.lock);

		fq->iif = iif;
		ret = ip6_frag_queue(fq, skb, fhdr, IP6CB(skb)->nhoff,
				     &prob_offset);

		spin_unlock(&fq->q.lock);
		inet_frag_put(&fq->q);
		if (prob_offset) {
			__IP6_INC_STATS(net, __in6_dev_get_safely(skb->dev),
					IPSTATS_MIB_INHDRERRORS);
			/* icmpv6_param_prob() calls kfree_skb(skb) */
			icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, prob_offset);
		}
		return ret;
	}

	__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)), IPSTATS_MIB_REASMFAILS);
	kfree_skb(skb);
	return -1;

fail_hdr:
	__IP6_INC_STATS(net, __in6_dev_get_safely(skb->dev),
			IPSTATS_MIB_INHDRERRORS);
	icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, skb_network_header_len(skb));
	return -1;
}

static const struct inet6_protocol frag_protocol = {
	.handler	=	ipv6_frag_rcv,
	.flags		=	INET6_PROTO_NOPOLICY,
};

#ifdef CONFIG_SYSCTL

static struct ctl_table ip6_frags_ns_ctl_table[] = {
	{
		.procname	= "ip6frag_high_thresh",
		.data		= &init_net.ipv6.frags.high_thresh,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra1		= &init_net.ipv6.frags.low_thresh
	},
	{
		.procname	= "ip6frag_low_thresh",
		.data		= &init_net.ipv6.frags.low_thresh,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra2		= &init_net.ipv6.frags.high_thresh
	},
	{
		.procname	= "ip6frag_time",
		.data		= &init_net.ipv6.frags.timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{ }
};

/* secret interval has been deprecated */
static int ip6_frags_secret_interval_unused;
static struct ctl_table ip6_frags_ctl_table[] = {
	{
		.procname	= "ip6frag_secret_interval",
		.data		= &ip6_frags_secret_interval_unused,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{ }
};

static int __net_init ip6_frags_ns_sysctl_register(struct net *net)
{
	struct ctl_table *table;
	struct ctl_table_header *hdr;

	table = ip6_frags_ns_ctl_table;
	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(ip6_frags_ns_ctl_table), GFP_KERNEL);
		if (!table)
			goto err_alloc;

		table[0].data = &net->ipv6.frags.high_thresh;
		table[0].extra1 = &net->ipv6.frags.low_thresh;
		table[1].data = &net->ipv6.frags.low_thresh;
		table[1].extra2 = &net->ipv6.frags.high_thresh;
		table[2].data = &net->ipv6.frags.timeout;
	}

	hdr = register_net_sysctl(net, "net/ipv6", table);
	if (!hdr)
		goto err_reg;

	net->ipv6.sysctl.frags_hdr = hdr;
	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void __net_exit ip6_frags_ns_sysctl_unregister(struct net *net)
{
	struct ctl_table *table;

	table = net->ipv6.sysctl.frags_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->ipv6.sysctl.frags_hdr);
	if (!net_eq(net, &init_net))
		kfree(table);
}

static struct ctl_table_header *ip6_ctl_header;

static int ip6_frags_sysctl_register(void)
{
	ip6_ctl_header = register_net_sysctl(&init_net, "net/ipv6",
			ip6_frags_ctl_table);
	return ip6_ctl_header == NULL ? -ENOMEM : 0;
}

static void ip6_frags_sysctl_unregister(void)
{
	unregister_net_sysctl_table(ip6_ctl_header);
}
#else
static int ip6_frags_ns_sysctl_register(struct net *net)
{
	return 0;
}

static void ip6_frags_ns_sysctl_unregister(struct net *net)
{
}

static int ip6_frags_sysctl_register(void)
{
	return 0;
}

static void ip6_frags_sysctl_unregister(void)
{
}
#endif

static int __net_init ipv6_frags_init_net(struct net *net)
{
	int res;

	net->ipv6.frags.high_thresh = IPV6_FRAG_HIGH_THRESH;
	net->ipv6.frags.low_thresh = IPV6_FRAG_LOW_THRESH;
	net->ipv6.frags.timeout = IPV6_FRAG_TIMEOUT;
	net->ipv6.frags.f = &ip6_frags;

	res = inet_frags_init_net(&net->ipv6.frags);
	if (res < 0)
		return res;

	res = ip6_frags_ns_sysctl_register(net);
	if (res < 0)
		inet_frags_exit_net(&net->ipv6.frags);
	return res;
}

static void __net_exit ipv6_frags_exit_net(struct net *net)
{
	ip6_frags_ns_sysctl_unregister(net);
	inet_frags_exit_net(&net->ipv6.frags);
}

static struct pernet_operations ip6_frags_ops = {
	.init = ipv6_frags_init_net,
	.exit = ipv6_frags_exit_net,
};

static const struct rhashtable_params ip6_rhash_params = {
	.head_offset		= offsetof(struct inet_frag_queue, node),
	.hashfn			= ip6frag_key_hashfn,
	.obj_hashfn		= ip6frag_obj_hashfn,
	.obj_cmpfn		= ip6frag_obj_cmpfn,
	.automatic_shrinking	= true,
};

int __init ipv6_frag_init(void)
{
	int ret;

	ip6_frags.constructor = ip6frag_init;
	ip6_frags.destructor = NULL;
	ip6_frags.qsize = sizeof(struct frag_queue);
	ip6_frags.frag_expire = ip6_frag_expire;
	ip6_frags.frags_cache_name = ip6_frag_cache_name;
	ip6_frags.rhash_params = ip6_rhash_params;
	ret = inet_frags_init(&ip6_frags);
	if (ret)
		goto out;

	ret = inet6_add_protocol(&frag_protocol, IPPROTO_FRAGMENT);
	if (ret)
		goto err_protocol;

	ret = ip6_frags_sysctl_register();
	if (ret)
		goto err_sysctl;

	ret = register_pernet_subsys(&ip6_frags_ops);
	if (ret)
		goto err_pernet;

out:
	return ret;

err_pernet:
	ip6_frags_sysctl_unregister();
err_sysctl:
	inet6_del_protocol(&frag_protocol, IPPROTO_FRAGMENT);
err_protocol:
	inet_frags_fini(&ip6_frags);
	goto out;
}

void ipv6_frag_exit(void)
{
	inet_frags_fini(&ip6_frags);
	ip6_frags_sysctl_unregister();
	unregister_pernet_subsys(&ip6_frags_ops);
	inet6_del_protocol(&frag_protocol, IPPROTO_FRAGMENT);
}
