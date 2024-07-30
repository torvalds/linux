// SPDX-License-Identifier: GPL-2.0-only
/*
 * xfrm_nat_keepalive.c
 *
 * (c) 2024 Eyal Birger <eyal.birger@gmail.com>
 */

#include <net/inet_common.h>
#include <net/ip6_checksum.h>
#include <net/xfrm.h>

static DEFINE_PER_CPU(struct sock *, nat_keepalive_sk_ipv4);
#if IS_ENABLED(CONFIG_IPV6)
static DEFINE_PER_CPU(struct sock *, nat_keepalive_sk_ipv6);
#endif

struct nat_keepalive {
	struct net *net;
	u16 family;
	xfrm_address_t saddr;
	xfrm_address_t daddr;
	__be16 encap_sport;
	__be16 encap_dport;
	__u32 smark;
};

static void nat_keepalive_init(struct nat_keepalive *ka, struct xfrm_state *x)
{
	ka->net = xs_net(x);
	ka->family = x->props.family;
	ka->saddr = x->props.saddr;
	ka->daddr = x->id.daddr;
	ka->encap_sport = x->encap->encap_sport;
	ka->encap_dport = x->encap->encap_dport;
	ka->smark = xfrm_smark_get(0, x);
}

static int nat_keepalive_send_ipv4(struct sk_buff *skb,
				   struct nat_keepalive *ka)
{
	struct net *net = ka->net;
	struct flowi4 fl4;
	struct rtable *rt;
	struct sock *sk;
	__u8 tos = 0;
	int err;

	flowi4_init_output(&fl4, 0 /* oif */, skb->mark, tos,
			   RT_SCOPE_UNIVERSE, IPPROTO_UDP, 0,
			   ka->daddr.a4, ka->saddr.a4, ka->encap_dport,
			   ka->encap_sport, sock_net_uid(net, NULL));

	rt = ip_route_output_key(net, &fl4);
	if (IS_ERR(rt))
		return PTR_ERR(rt);

	skb_dst_set(skb, &rt->dst);

	sk = *this_cpu_ptr(&nat_keepalive_sk_ipv4);
	sock_net_set(sk, net);
	err = ip_build_and_send_pkt(skb, sk, fl4.saddr, fl4.daddr, NULL, tos);
	sock_net_set(sk, &init_net);
	return err;
}

#if IS_ENABLED(CONFIG_IPV6)
static int nat_keepalive_send_ipv6(struct sk_buff *skb,
				   struct nat_keepalive *ka,
				   struct udphdr *uh)
{
	struct net *net = ka->net;
	struct dst_entry *dst;
	struct flowi6 fl6;
	struct sock *sk;
	__wsum csum;
	int err;

	csum = skb_checksum(skb, 0, skb->len, 0);
	uh->check = csum_ipv6_magic(&ka->saddr.in6, &ka->daddr.in6,
				    skb->len, IPPROTO_UDP, csum);
	if (uh->check == 0)
		uh->check = CSUM_MANGLED_0;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_mark = skb->mark;
	fl6.saddr = ka->saddr.in6;
	fl6.daddr = ka->daddr.in6;
	fl6.flowi6_proto = IPPROTO_UDP;
	fl6.fl6_sport = ka->encap_sport;
	fl6.fl6_dport = ka->encap_dport;

	sk = *this_cpu_ptr(&nat_keepalive_sk_ipv6);
	sock_net_set(sk, net);
	dst = ipv6_stub->ipv6_dst_lookup_flow(net, sk, &fl6, NULL);
	if (IS_ERR(dst))
		return PTR_ERR(dst);

	skb_dst_set(skb, dst);
	err = ipv6_stub->ip6_xmit(sk, skb, &fl6, skb->mark, NULL, 0, 0);
	sock_net_set(sk, &init_net);
	return err;
}
#endif

static void nat_keepalive_send(struct nat_keepalive *ka)
{
	const int nat_ka_hdrs_len = max(sizeof(struct iphdr),
					sizeof(struct ipv6hdr)) +
				    sizeof(struct udphdr);
	const u8 nat_ka_payload = 0xFF;
	int err = -EAFNOSUPPORT;
	struct sk_buff *skb;
	struct udphdr *uh;

	skb = alloc_skb(nat_ka_hdrs_len + sizeof(nat_ka_payload), GFP_ATOMIC);
	if (unlikely(!skb))
		return;

	skb_reserve(skb, nat_ka_hdrs_len);

	skb_put_u8(skb, nat_ka_payload);

	uh = skb_push(skb, sizeof(*uh));
	uh->source = ka->encap_sport;
	uh->dest = ka->encap_dport;
	uh->len = htons(skb->len);
	uh->check = 0;

	skb->mark = ka->smark;

	switch (ka->family) {
	case AF_INET:
		err = nat_keepalive_send_ipv4(skb, ka);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		err = nat_keepalive_send_ipv6(skb, ka, uh);
		break;
#endif
	}
	if (err)
		kfree_skb(skb);
}

struct nat_keepalive_work_ctx {
	time64_t next_run;
	time64_t now;
};

static int nat_keepalive_work_single(struct xfrm_state *x, int count, void *ptr)
{
	struct nat_keepalive_work_ctx *ctx = ptr;
	bool send_keepalive = false;
	struct nat_keepalive ka;
	time64_t next_run;
	u32 interval;
	int delta;

	interval = x->nat_keepalive_interval;
	if (!interval)
		return 0;

	spin_lock(&x->lock);

	delta = (int)(ctx->now - x->lastused);
	if (delta < interval) {
		x->nat_keepalive_expiration = ctx->now + interval - delta;
		next_run = x->nat_keepalive_expiration;
	} else if (x->nat_keepalive_expiration > ctx->now) {
		next_run = x->nat_keepalive_expiration;
	} else {
		next_run = ctx->now + interval;
		nat_keepalive_init(&ka, x);
		send_keepalive = true;
	}

	spin_unlock(&x->lock);

	if (send_keepalive)
		nat_keepalive_send(&ka);

	if (!ctx->next_run || next_run < ctx->next_run)
		ctx->next_run = next_run;
	return 0;
}

static void nat_keepalive_work(struct work_struct *work)
{
	struct nat_keepalive_work_ctx ctx;
	struct xfrm_state_walk walk;
	struct net *net;

	ctx.next_run = 0;
	ctx.now = ktime_get_real_seconds();

	net = container_of(work, struct net, xfrm.nat_keepalive_work.work);
	xfrm_state_walk_init(&walk, IPPROTO_ESP, NULL);
	xfrm_state_walk(net, &walk, nat_keepalive_work_single, &ctx);
	xfrm_state_walk_done(&walk, net);
	if (ctx.next_run)
		schedule_delayed_work(&net->xfrm.nat_keepalive_work,
				      (ctx.next_run - ctx.now) * HZ);
}

static int nat_keepalive_sk_init(struct sock * __percpu *socks,
				 unsigned short family)
{
	struct sock *sk;
	int err, i;

	for_each_possible_cpu(i) {
		err = inet_ctl_sock_create(&sk, family, SOCK_RAW, IPPROTO_UDP,
					   &init_net);
		if (err < 0)
			goto err;

		*per_cpu_ptr(socks, i) = sk;
	}

	return 0;
err:
	for_each_possible_cpu(i)
		inet_ctl_sock_destroy(*per_cpu_ptr(socks, i));
	return err;
}

static void nat_keepalive_sk_fini(struct sock * __percpu *socks)
{
	int i;

	for_each_possible_cpu(i)
		inet_ctl_sock_destroy(*per_cpu_ptr(socks, i));
}

void xfrm_nat_keepalive_state_updated(struct xfrm_state *x)
{
	struct net *net;

	if (!x->nat_keepalive_interval)
		return;

	net = xs_net(x);
	schedule_delayed_work(&net->xfrm.nat_keepalive_work, 0);
}

int __net_init xfrm_nat_keepalive_net_init(struct net *net)
{
	INIT_DELAYED_WORK(&net->xfrm.nat_keepalive_work, nat_keepalive_work);
	return 0;
}

int xfrm_nat_keepalive_net_fini(struct net *net)
{
	cancel_delayed_work_sync(&net->xfrm.nat_keepalive_work);
	return 0;
}

int xfrm_nat_keepalive_init(unsigned short family)
{
	int err = -EAFNOSUPPORT;

	switch (family) {
	case AF_INET:
		err = nat_keepalive_sk_init(&nat_keepalive_sk_ipv4, PF_INET);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		err = nat_keepalive_sk_init(&nat_keepalive_sk_ipv6, PF_INET6);
		break;
#endif
	}

	if (err)
		pr_err("xfrm nat keepalive init: failed to init err:%d\n", err);
	return err;
}
EXPORT_SYMBOL_GPL(xfrm_nat_keepalive_init);

void xfrm_nat_keepalive_fini(unsigned short family)
{
	switch (family) {
	case AF_INET:
		nat_keepalive_sk_fini(&nat_keepalive_sk_ipv4);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		nat_keepalive_sk_fini(&nat_keepalive_sk_ipv6);
		break;
#endif
	}
}
EXPORT_SYMBOL_GPL(xfrm_nat_keepalive_fini);
