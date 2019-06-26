// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/stddef.h>
#include <linux/err.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_log.h>

#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/route.h>
#include <net/ip.h>

#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>
#include <net/netfilter/ipv6/nf_conntrack_ipv6.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/ipv4/nf_defrag_ipv4.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>

#include <linux/ipv6.h>
#include <linux/in6.h>
#include <net/ipv6.h>
#include <net/inet_frag.h>

extern unsigned int nf_conntrack_net_id;

static DEFINE_MUTEX(nf_ct_proto_mutex);

#ifdef CONFIG_SYSCTL
__printf(5, 6)
void nf_l4proto_log_invalid(const struct sk_buff *skb,
			    struct net *net,
			    u16 pf, u8 protonum,
			    const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (net->ct.sysctl_log_invalid != protonum &&
	    net->ct.sysctl_log_invalid != IPPROTO_RAW)
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	nf_log_packet(net, pf, 0, skb, NULL, NULL, NULL,
		      "nf_ct_proto_%d: %pV ", protonum, &vaf);
	va_end(args);
}
EXPORT_SYMBOL_GPL(nf_l4proto_log_invalid);

__printf(3, 4)
void nf_ct_l4proto_log_invalid(const struct sk_buff *skb,
			       const struct nf_conn *ct,
			       const char *fmt, ...)
{
	struct va_format vaf;
	struct net *net;
	va_list args;

	net = nf_ct_net(ct);
	if (likely(net->ct.sysctl_log_invalid == 0))
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	nf_l4proto_log_invalid(skb, net, nf_ct_l3num(ct),
			       nf_ct_protonum(ct), "%pV", &vaf);
	va_end(args);
}
EXPORT_SYMBOL_GPL(nf_ct_l4proto_log_invalid);
#endif

const struct nf_conntrack_l4proto *nf_ct_l4proto_find(u8 l4proto)
{
	switch (l4proto) {
	case IPPROTO_UDP: return &nf_conntrack_l4proto_udp;
	case IPPROTO_TCP: return &nf_conntrack_l4proto_tcp;
	case IPPROTO_ICMP: return &nf_conntrack_l4proto_icmp;
#ifdef CONFIG_NF_CT_PROTO_DCCP
	case IPPROTO_DCCP: return &nf_conntrack_l4proto_dccp;
#endif
#ifdef CONFIG_NF_CT_PROTO_SCTP
	case IPPROTO_SCTP: return &nf_conntrack_l4proto_sctp;
#endif
#ifdef CONFIG_NF_CT_PROTO_UDPLITE
	case IPPROTO_UDPLITE: return &nf_conntrack_l4proto_udplite;
#endif
#ifdef CONFIG_NF_CT_PROTO_GRE
	case IPPROTO_GRE: return &nf_conntrack_l4proto_gre;
#endif
#if IS_ENABLED(CONFIG_IPV6)
	case IPPROTO_ICMPV6: return &nf_conntrack_l4proto_icmpv6;
#endif /* CONFIG_IPV6 */
	}

	return &nf_conntrack_l4proto_generic;
};
EXPORT_SYMBOL_GPL(nf_ct_l4proto_find);

static unsigned int nf_confirm(struct sk_buff *skb,
			       unsigned int protoff,
			       struct nf_conn *ct,
			       enum ip_conntrack_info ctinfo)
{
	const struct nf_conn_help *help;

	help = nfct_help(ct);
	if (help) {
		const struct nf_conntrack_helper *helper;
		int ret;

		/* rcu_read_lock()ed by nf_hook_thresh */
		helper = rcu_dereference(help->helper);
		if (helper) {
			ret = helper->help(skb,
					   protoff,
					   ct, ctinfo);
			if (ret != NF_ACCEPT)
				return ret;
		}
	}

	if (test_bit(IPS_SEQ_ADJUST_BIT, &ct->status) &&
	    !nf_is_loopback_packet(skb)) {
		if (!nf_ct_seq_adjust(skb, ct, ctinfo, protoff)) {
			NF_CT_STAT_INC_ATOMIC(nf_ct_net(ct), drop);
			return NF_DROP;
		}
	}

	/* We've seen it coming out the other side: confirm it */
	return nf_conntrack_confirm(skb);
}

static unsigned int ipv4_confirm(void *priv,
				 struct sk_buff *skb,
				 const struct nf_hook_state *state)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct || ctinfo == IP_CT_RELATED_REPLY)
		return nf_conntrack_confirm(skb);

	return nf_confirm(skb,
			  skb_network_offset(skb) + ip_hdrlen(skb),
			  ct, ctinfo);
}

static unsigned int ipv4_conntrack_in(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	return nf_conntrack_in(skb, state);
}

static unsigned int ipv4_conntrack_local(void *priv,
					 struct sk_buff *skb,
					 const struct nf_hook_state *state)
{
	if (ip_is_fragment(ip_hdr(skb))) { /* IP_NODEFRAG setsockopt set */
		enum ip_conntrack_info ctinfo;
		struct nf_conn *tmpl;

		tmpl = nf_ct_get(skb, &ctinfo);
		if (tmpl && nf_ct_is_template(tmpl)) {
			/* when skipping ct, clear templates to avoid fooling
			 * later targets/matches
			 */
			skb->_nfct = 0;
			nf_ct_put(tmpl);
		}
		return NF_ACCEPT;
	}

	return nf_conntrack_in(skb, state);
}

/* Connection tracking may drop packets, but never alters them, so
 * make it the first hook.
 */
static const struct nf_hook_ops ipv4_conntrack_ops[] = {
	{
		.hook		= ipv4_conntrack_in,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK,
	},
	{
		.hook		= ipv4_conntrack_local,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_CONNTRACK,
	},
	{
		.hook		= ipv4_confirm,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM,
	},
	{
		.hook		= ipv4_confirm,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM,
	},
};

/* Fast function for those who don't want to parse /proc (and I don't
 * blame them).
 * Reversing the socket's dst/src point of view gives us the reply
 * mapping.
 */
static int
getorigdst(struct sock *sk, int optval, void __user *user, int *len)
{
	const struct inet_sock *inet = inet_sk(sk);
	const struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;

	memset(&tuple, 0, sizeof(tuple));

	lock_sock(sk);
	tuple.src.u3.ip = inet->inet_rcv_saddr;
	tuple.src.u.tcp.port = inet->inet_sport;
	tuple.dst.u3.ip = inet->inet_daddr;
	tuple.dst.u.tcp.port = inet->inet_dport;
	tuple.src.l3num = PF_INET;
	tuple.dst.protonum = sk->sk_protocol;
	release_sock(sk);

	/* We only do TCP and SCTP at the moment: is there a better way? */
	if (tuple.dst.protonum != IPPROTO_TCP &&
	    tuple.dst.protonum != IPPROTO_SCTP) {
		pr_debug("SO_ORIGINAL_DST: Not a TCP/SCTP socket\n");
		return -ENOPROTOOPT;
	}

	if ((unsigned int)*len < sizeof(struct sockaddr_in)) {
		pr_debug("SO_ORIGINAL_DST: len %d not %zu\n",
			 *len, sizeof(struct sockaddr_in));
		return -EINVAL;
	}

	h = nf_conntrack_find_get(sock_net(sk), &nf_ct_zone_dflt, &tuple);
	if (h) {
		struct sockaddr_in sin;
		struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);

		sin.sin_family = AF_INET;
		sin.sin_port = ct->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.u.tcp.port;
		sin.sin_addr.s_addr = ct->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.u3.ip;
		memset(sin.sin_zero, 0, sizeof(sin.sin_zero));

		pr_debug("SO_ORIGINAL_DST: %pI4 %u\n",
			 &sin.sin_addr.s_addr, ntohs(sin.sin_port));
		nf_ct_put(ct);
		if (copy_to_user(user, &sin, sizeof(sin)) != 0)
			return -EFAULT;
		else
			return 0;
	}
	pr_debug("SO_ORIGINAL_DST: Can't find %pI4/%u-%pI4/%u.\n",
		 &tuple.src.u3.ip, ntohs(tuple.src.u.tcp.port),
		 &tuple.dst.u3.ip, ntohs(tuple.dst.u.tcp.port));
	return -ENOENT;
}

static struct nf_sockopt_ops so_getorigdst = {
	.pf		= PF_INET,
	.get_optmin	= SO_ORIGINAL_DST,
	.get_optmax	= SO_ORIGINAL_DST + 1,
	.get		= getorigdst,
	.owner		= THIS_MODULE,
};

#if IS_ENABLED(CONFIG_IPV6)
static int
ipv6_getorigdst(struct sock *sk, int optval, void __user *user, int *len)
{
	struct nf_conntrack_tuple tuple = { .src.l3num = NFPROTO_IPV6 };
	const struct ipv6_pinfo *inet6 = inet6_sk(sk);
	const struct inet_sock *inet = inet_sk(sk);
	const struct nf_conntrack_tuple_hash *h;
	struct sockaddr_in6 sin6;
	struct nf_conn *ct;
	__be32 flow_label;
	int bound_dev_if;

	lock_sock(sk);
	tuple.src.u3.in6 = sk->sk_v6_rcv_saddr;
	tuple.src.u.tcp.port = inet->inet_sport;
	tuple.dst.u3.in6 = sk->sk_v6_daddr;
	tuple.dst.u.tcp.port = inet->inet_dport;
	tuple.dst.protonum = sk->sk_protocol;
	bound_dev_if = sk->sk_bound_dev_if;
	flow_label = inet6->flow_label;
	release_sock(sk);

	if (tuple.dst.protonum != IPPROTO_TCP &&
	    tuple.dst.protonum != IPPROTO_SCTP)
		return -ENOPROTOOPT;

	if (*len < 0 || (unsigned int)*len < sizeof(sin6))
		return -EINVAL;

	h = nf_conntrack_find_get(sock_net(sk), &nf_ct_zone_dflt, &tuple);
	if (!h) {
		pr_debug("IP6T_SO_ORIGINAL_DST: Can't find %pI6c/%u-%pI6c/%u.\n",
			 &tuple.src.u3.ip6, ntohs(tuple.src.u.tcp.port),
			 &tuple.dst.u3.ip6, ntohs(tuple.dst.u.tcp.port));
		return -ENOENT;
	}

	ct = nf_ct_tuplehash_to_ctrack(h);

	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.tcp.port;
	sin6.sin6_flowinfo = flow_label & IPV6_FLOWINFO_MASK;
	memcpy(&sin6.sin6_addr,
	       &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3.in6,
	       sizeof(sin6.sin6_addr));

	nf_ct_put(ct);
	sin6.sin6_scope_id = ipv6_iface_scope_id(&sin6.sin6_addr, bound_dev_if);
	return copy_to_user(user, &sin6, sizeof(sin6)) ? -EFAULT : 0;
}

static struct nf_sockopt_ops so_getorigdst6 = {
	.pf		= NFPROTO_IPV6,
	.get_optmin	= IP6T_SO_ORIGINAL_DST,
	.get_optmax	= IP6T_SO_ORIGINAL_DST + 1,
	.get		= ipv6_getorigdst,
	.owner		= THIS_MODULE,
};

static unsigned int ipv6_confirm(void *priv,
				 struct sk_buff *skb,
				 const struct nf_hook_state *state)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	unsigned char pnum = ipv6_hdr(skb)->nexthdr;
	__be16 frag_off;
	int protoff;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct || ctinfo == IP_CT_RELATED_REPLY)
		return nf_conntrack_confirm(skb);

	protoff = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &pnum,
				   &frag_off);
	if (protoff < 0 || (frag_off & htons(~0x7)) != 0) {
		pr_debug("proto header not found\n");
		return nf_conntrack_confirm(skb);
	}

	return nf_confirm(skb, protoff, ct, ctinfo);
}

static unsigned int ipv6_conntrack_in(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	return nf_conntrack_in(skb, state);
}

static unsigned int ipv6_conntrack_local(void *priv,
					 struct sk_buff *skb,
					 const struct nf_hook_state *state)
{
	return nf_conntrack_in(skb, state);
}

static const struct nf_hook_ops ipv6_conntrack_ops[] = {
	{
		.hook		= ipv6_conntrack_in,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP6_PRI_CONNTRACK,
	},
	{
		.hook		= ipv6_conntrack_local,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_CONNTRACK,
	},
	{
		.hook		= ipv6_confirm,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP6_PRI_LAST,
	},
	{
		.hook		= ipv6_confirm,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_LAST - 1,
	},
};
#endif

static int nf_ct_tcp_fixup(struct nf_conn *ct, void *_nfproto)
{
	u8 nfproto = (unsigned long)_nfproto;

	if (nf_ct_l3num(ct) != nfproto)
		return 0;

	if (nf_ct_protonum(ct) == IPPROTO_TCP &&
	    ct->proto.tcp.state == TCP_CONNTRACK_ESTABLISHED) {
		ct->proto.tcp.seen[0].td_maxwin = 0;
		ct->proto.tcp.seen[1].td_maxwin = 0;
	}

	return 0;
}

static int nf_ct_netns_do_get(struct net *net, u8 nfproto)
{
	struct nf_conntrack_net *cnet = net_generic(net, nf_conntrack_net_id);
	bool fixup_needed = false;
	int err = 0;

	mutex_lock(&nf_ct_proto_mutex);

	switch (nfproto) {
	case NFPROTO_IPV4:
		cnet->users4++;
		if (cnet->users4 > 1)
			goto out_unlock;
		err = nf_defrag_ipv4_enable(net);
		if (err) {
			cnet->users4 = 0;
			goto out_unlock;
		}

		err = nf_register_net_hooks(net, ipv4_conntrack_ops,
					    ARRAY_SIZE(ipv4_conntrack_ops));
		if (err)
			cnet->users4 = 0;
		else
			fixup_needed = true;
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case NFPROTO_IPV6:
		cnet->users6++;
		if (cnet->users6 > 1)
			goto out_unlock;
		err = nf_defrag_ipv6_enable(net);
		if (err < 0) {
			cnet->users6 = 0;
			goto out_unlock;
		}

		err = nf_register_net_hooks(net, ipv6_conntrack_ops,
					    ARRAY_SIZE(ipv6_conntrack_ops));
		if (err)
			cnet->users6 = 0;
		else
			fixup_needed = true;
		break;
#endif
	default:
		err = -EPROTO;
		break;
	}
 out_unlock:
	mutex_unlock(&nf_ct_proto_mutex);

	if (fixup_needed)
		nf_ct_iterate_cleanup_net(net, nf_ct_tcp_fixup,
					  (void *)(unsigned long)nfproto, 0, 0);

	return err;
}

static void nf_ct_netns_do_put(struct net *net, u8 nfproto)
{
	struct nf_conntrack_net *cnet = net_generic(net, nf_conntrack_net_id);

	mutex_lock(&nf_ct_proto_mutex);
	switch (nfproto) {
	case NFPROTO_IPV4:
		if (cnet->users4 && (--cnet->users4 == 0))
			nf_unregister_net_hooks(net, ipv4_conntrack_ops,
						ARRAY_SIZE(ipv4_conntrack_ops));
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case NFPROTO_IPV6:
		if (cnet->users6 && (--cnet->users6 == 0))
			nf_unregister_net_hooks(net, ipv6_conntrack_ops,
						ARRAY_SIZE(ipv6_conntrack_ops));
		break;
#endif
	}

	mutex_unlock(&nf_ct_proto_mutex);
}

int nf_ct_netns_get(struct net *net, u8 nfproto)
{
	int err;

	if (nfproto == NFPROTO_INET) {
		err = nf_ct_netns_do_get(net, NFPROTO_IPV4);
		if (err < 0)
			goto err1;
		err = nf_ct_netns_do_get(net, NFPROTO_IPV6);
		if (err < 0)
			goto err2;
	} else {
		err = nf_ct_netns_do_get(net, nfproto);
		if (err < 0)
			goto err1;
	}
	return 0;

err2:
	nf_ct_netns_put(net, NFPROTO_IPV4);
err1:
	return err;
}
EXPORT_SYMBOL_GPL(nf_ct_netns_get);

void nf_ct_netns_put(struct net *net, uint8_t nfproto)
{
	if (nfproto == NFPROTO_INET) {
		nf_ct_netns_do_put(net, NFPROTO_IPV4);
		nf_ct_netns_do_put(net, NFPROTO_IPV6);
	} else {
		nf_ct_netns_do_put(net, nfproto);
	}
}
EXPORT_SYMBOL_GPL(nf_ct_netns_put);

int nf_conntrack_proto_init(void)
{
	int ret;

	ret = nf_register_sockopt(&so_getorigdst);
	if (ret < 0)
		return ret;

#if IS_ENABLED(CONFIG_IPV6)
	ret = nf_register_sockopt(&so_getorigdst6);
	if (ret < 0)
		goto cleanup_sockopt;
#endif

	return ret;

#if IS_ENABLED(CONFIG_IPV6)
cleanup_sockopt:
	nf_unregister_sockopt(&so_getorigdst6);
#endif
	return ret;
}

void nf_conntrack_proto_fini(void)
{
	nf_unregister_sockopt(&so_getorigdst);
#if IS_ENABLED(CONFIG_IPV6)
	nf_unregister_sockopt(&so_getorigdst6);
#endif
}

void nf_conntrack_proto_pernet_init(struct net *net)
{
	nf_conntrack_generic_init_net(net);
	nf_conntrack_udp_init_net(net);
	nf_conntrack_tcp_init_net(net);
	nf_conntrack_icmp_init_net(net);
#if IS_ENABLED(CONFIG_IPV6)
	nf_conntrack_icmpv6_init_net(net);
#endif
#ifdef CONFIG_NF_CT_PROTO_DCCP
	nf_conntrack_dccp_init_net(net);
#endif
#ifdef CONFIG_NF_CT_PROTO_SCTP
	nf_conntrack_sctp_init_net(net);
#endif
#ifdef CONFIG_NF_CT_PROTO_GRE
	nf_conntrack_gre_init_net(net);
#endif
}

void nf_conntrack_proto_pernet_fini(struct net *net)
{
#ifdef CONFIG_NF_CT_PROTO_GRE
	nf_ct_gre_keymap_flush(net);
#endif
}

module_param_call(hashsize, nf_conntrack_set_hashsize, param_get_uint,
		  &nf_conntrack_htable_size, 0600);

MODULE_ALIAS("ip_conntrack");
MODULE_ALIAS("nf_conntrack-" __stringify(AF_INET));
MODULE_ALIAS("nf_conntrack-" __stringify(AF_INET6));
MODULE_LICENSE("GPL");
