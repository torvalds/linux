// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/udp.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <net/genetlink.h>
#include <net/gue.h>
#include <net/fou.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>
#include <net/xfrm.h>
#include <uapi/linux/fou.h>
#include <uapi/linux/genetlink.h>

struct fou {
	struct socket *sock;
	u8 protocol;
	u8 flags;
	__be16 port;
	u8 family;
	u16 type;
	struct list_head list;
	struct rcu_head rcu;
};

#define FOU_F_REMCSUM_NOPARTIAL BIT(0)

struct fou_cfg {
	u16 type;
	u8 protocol;
	u8 flags;
	struct udp_port_cfg udp_config;
};

static unsigned int fou_net_id;

struct fou_net {
	struct list_head fou_list;
	struct mutex fou_lock;
};

static inline struct fou *fou_from_sock(struct sock *sk)
{
	return sk->sk_user_data;
}

static int fou_recv_pull(struct sk_buff *skb, struct fou *fou, size_t len)
{
	/* Remove 'len' bytes from the packet (UDP header and
	 * FOU header if present).
	 */
	if (fou->family == AF_INET)
		ip_hdr(skb)->tot_len = htons(ntohs(ip_hdr(skb)->tot_len) - len);
	else
		ipv6_hdr(skb)->payload_len =
		    htons(ntohs(ipv6_hdr(skb)->payload_len) - len);

	__skb_pull(skb, len);
	skb_postpull_rcsum(skb, udp_hdr(skb), len);
	skb_reset_transport_header(skb);
	return iptunnel_pull_offloads(skb);
}

static int fou_udp_recv(struct sock *sk, struct sk_buff *skb)
{
	struct fou *fou = fou_from_sock(sk);

	if (!fou)
		return 1;

	if (fou_recv_pull(skb, fou, sizeof(struct udphdr)))
		goto drop;

	return -fou->protocol;

drop:
	kfree_skb(skb);
	return 0;
}

static struct guehdr *gue_remcsum(struct sk_buff *skb, struct guehdr *guehdr,
				  void *data, size_t hdrlen, u8 ipproto,
				  bool nopartial)
{
	__be16 *pd = data;
	size_t start = ntohs(pd[0]);
	size_t offset = ntohs(pd[1]);
	size_t plen = sizeof(struct udphdr) + hdrlen +
	    max_t(size_t, offset + sizeof(u16), start);

	if (skb->remcsum_offload)
		return guehdr;

	if (!pskb_may_pull(skb, plen))
		return NULL;
	guehdr = (struct guehdr *)&udp_hdr(skb)[1];

	skb_remcsum_process(skb, (void *)guehdr + hdrlen,
			    start, offset, nopartial);

	return guehdr;
}

static int gue_control_message(struct sk_buff *skb, struct guehdr *guehdr)
{
	/* No support yet */
	kfree_skb(skb);
	return 0;
}

static int gue_udp_recv(struct sock *sk, struct sk_buff *skb)
{
	struct fou *fou = fou_from_sock(sk);
	size_t len, optlen, hdrlen;
	struct guehdr *guehdr;
	void *data;
	u16 doffset = 0;
	u8 proto_ctype;

	if (!fou)
		return 1;

	len = sizeof(struct udphdr) + sizeof(struct guehdr);
	if (!pskb_may_pull(skb, len))
		goto drop;

	guehdr = (struct guehdr *)&udp_hdr(skb)[1];

	switch (guehdr->version) {
	case 0: /* Full GUE header present */
		break;

	case 1: {
		/* Direct encapsulation of IPv4 or IPv6 */

		int prot;

		switch (((struct iphdr *)guehdr)->version) {
		case 4:
			prot = IPPROTO_IPIP;
			break;
		case 6:
			prot = IPPROTO_IPV6;
			break;
		default:
			goto drop;
		}

		if (fou_recv_pull(skb, fou, sizeof(struct udphdr)))
			goto drop;

		return -prot;
	}

	default: /* Undefined version */
		goto drop;
	}

	optlen = guehdr->hlen << 2;
	len += optlen;

	if (!pskb_may_pull(skb, len))
		goto drop;

	/* guehdr may change after pull */
	guehdr = (struct guehdr *)&udp_hdr(skb)[1];

	if (validate_gue_flags(guehdr, optlen))
		goto drop;

	hdrlen = sizeof(struct guehdr) + optlen;

	if (fou->family == AF_INET)
		ip_hdr(skb)->tot_len = htons(ntohs(ip_hdr(skb)->tot_len) - len);
	else
		ipv6_hdr(skb)->payload_len =
		    htons(ntohs(ipv6_hdr(skb)->payload_len) - len);

	/* Pull csum through the guehdr now . This can be used if
	 * there is a remote checksum offload.
	 */
	skb_postpull_rcsum(skb, udp_hdr(skb), len);

	data = &guehdr[1];

	if (guehdr->flags & GUE_FLAG_PRIV) {
		__be32 flags = *(__be32 *)(data + doffset);

		doffset += GUE_LEN_PRIV;

		if (flags & GUE_PFLAG_REMCSUM) {
			guehdr = gue_remcsum(skb, guehdr, data + doffset,
					     hdrlen, guehdr->proto_ctype,
					     !!(fou->flags &
						FOU_F_REMCSUM_NOPARTIAL));
			if (!guehdr)
				goto drop;

			data = &guehdr[1];

			doffset += GUE_PLEN_REMCSUM;
		}
	}

	if (unlikely(guehdr->control))
		return gue_control_message(skb, guehdr);

	proto_ctype = guehdr->proto_ctype;
	__skb_pull(skb, sizeof(struct udphdr) + hdrlen);
	skb_reset_transport_header(skb);

	if (iptunnel_pull_offloads(skb))
		goto drop;

	return -proto_ctype;

drop:
	kfree_skb(skb);
	return 0;
}

static struct sk_buff *fou_gro_receive(struct sock *sk,
				       struct list_head *head,
				       struct sk_buff *skb)
{
	u8 proto = fou_from_sock(sk)->protocol;
	const struct net_offload **offloads;
	const struct net_offload *ops;
	struct sk_buff *pp = NULL;

	/* We can clear the encap_mark for FOU as we are essentially doing
	 * one of two possible things.  We are either adding an L4 tunnel
	 * header to the outer L3 tunnel header, or we are simply
	 * treating the GRE tunnel header as though it is a UDP protocol
	 * specific header such as VXLAN or GENEVE.
	 */
	NAPI_GRO_CB(skb)->encap_mark = 0;

	/* Flag this frame as already having an outer encap header */
	NAPI_GRO_CB(skb)->is_fou = 1;

	rcu_read_lock();
	offloads = NAPI_GRO_CB(skb)->is_ipv6 ? inet6_offloads : inet_offloads;
	ops = rcu_dereference(offloads[proto]);
	if (!ops || !ops->callbacks.gro_receive)
		goto out_unlock;

	pp = call_gro_receive(ops->callbacks.gro_receive, head, skb);

out_unlock:
	rcu_read_unlock();

	return pp;
}

static int fou_gro_complete(struct sock *sk, struct sk_buff *skb,
			    int nhoff)
{
	const struct net_offload *ops;
	u8 proto = fou_from_sock(sk)->protocol;
	int err = -ENOSYS;
	const struct net_offload **offloads;

	rcu_read_lock();
	offloads = NAPI_GRO_CB(skb)->is_ipv6 ? inet6_offloads : inet_offloads;
	ops = rcu_dereference(offloads[proto]);
	if (WARN_ON(!ops || !ops->callbacks.gro_complete))
		goto out_unlock;

	err = ops->callbacks.gro_complete(skb, nhoff);

	skb_set_inner_mac_header(skb, nhoff);

out_unlock:
	rcu_read_unlock();

	return err;
}

static struct guehdr *gue_gro_remcsum(struct sk_buff *skb, unsigned int off,
				      struct guehdr *guehdr, void *data,
				      size_t hdrlen, struct gro_remcsum *grc,
				      bool nopartial)
{
	__be16 *pd = data;
	size_t start = ntohs(pd[0]);
	size_t offset = ntohs(pd[1]);

	if (skb->remcsum_offload)
		return guehdr;

	if (!NAPI_GRO_CB(skb)->csum_valid)
		return NULL;

	guehdr = skb_gro_remcsum_process(skb, (void *)guehdr, off, hdrlen,
					 start, offset, grc, nopartial);

	skb->remcsum_offload = 1;

	return guehdr;
}

static struct sk_buff *gue_gro_receive(struct sock *sk,
				       struct list_head *head,
				       struct sk_buff *skb)
{
	const struct net_offload **offloads;
	const struct net_offload *ops;
	struct sk_buff *pp = NULL;
	struct sk_buff *p;
	struct guehdr *guehdr;
	size_t len, optlen, hdrlen, off;
	void *data;
	u16 doffset = 0;
	int flush = 1;
	struct fou *fou = fou_from_sock(sk);
	struct gro_remcsum grc;
	u8 proto;

	skb_gro_remcsum_init(&grc);

	off = skb_gro_offset(skb);
	len = off + sizeof(*guehdr);

	guehdr = skb_gro_header_fast(skb, off);
	if (skb_gro_header_hard(skb, len)) {
		guehdr = skb_gro_header_slow(skb, len, off);
		if (unlikely(!guehdr))
			goto out;
	}

	switch (guehdr->version) {
	case 0:
		break;
	case 1:
		switch (((struct iphdr *)guehdr)->version) {
		case 4:
			proto = IPPROTO_IPIP;
			break;
		case 6:
			proto = IPPROTO_IPV6;
			break;
		default:
			goto out;
		}
		goto next_proto;
	default:
		goto out;
	}

	optlen = guehdr->hlen << 2;
	len += optlen;

	if (skb_gro_header_hard(skb, len)) {
		guehdr = skb_gro_header_slow(skb, len, off);
		if (unlikely(!guehdr))
			goto out;
	}

	if (unlikely(guehdr->control) || guehdr->version != 0 ||
	    validate_gue_flags(guehdr, optlen))
		goto out;

	hdrlen = sizeof(*guehdr) + optlen;

	/* Adjust NAPI_GRO_CB(skb)->csum to account for guehdr,
	 * this is needed if there is a remote checkcsum offload.
	 */
	skb_gro_postpull_rcsum(skb, guehdr, hdrlen);

	data = &guehdr[1];

	if (guehdr->flags & GUE_FLAG_PRIV) {
		__be32 flags = *(__be32 *)(data + doffset);

		doffset += GUE_LEN_PRIV;

		if (flags & GUE_PFLAG_REMCSUM) {
			guehdr = gue_gro_remcsum(skb, off, guehdr,
						 data + doffset, hdrlen, &grc,
						 !!(fou->flags &
						    FOU_F_REMCSUM_NOPARTIAL));

			if (!guehdr)
				goto out;

			data = &guehdr[1];

			doffset += GUE_PLEN_REMCSUM;
		}
	}

	skb_gro_pull(skb, hdrlen);

	list_for_each_entry(p, head, list) {
		const struct guehdr *guehdr2;

		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		guehdr2 = (struct guehdr *)(p->data + off);

		/* Compare base GUE header to be equal (covers
		 * hlen, version, proto_ctype, and flags.
		 */
		if (guehdr->word != guehdr2->word) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}

		/* Compare optional fields are the same. */
		if (guehdr->hlen && memcmp(&guehdr[1], &guehdr2[1],
					   guehdr->hlen << 2)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}
	}

	proto = guehdr->proto_ctype;

next_proto:

	/* We can clear the encap_mark for GUE as we are essentially doing
	 * one of two possible things.  We are either adding an L4 tunnel
	 * header to the outer L3 tunnel header, or we are simply
	 * treating the GRE tunnel header as though it is a UDP protocol
	 * specific header such as VXLAN or GENEVE.
	 */
	NAPI_GRO_CB(skb)->encap_mark = 0;

	/* Flag this frame as already having an outer encap header */
	NAPI_GRO_CB(skb)->is_fou = 1;

	rcu_read_lock();
	offloads = NAPI_GRO_CB(skb)->is_ipv6 ? inet6_offloads : inet_offloads;
	ops = rcu_dereference(offloads[proto]);
	if (WARN_ON_ONCE(!ops || !ops->callbacks.gro_receive))
		goto out_unlock;

	pp = call_gro_receive(ops->callbacks.gro_receive, head, skb);
	flush = 0;

out_unlock:
	rcu_read_unlock();
out:
	skb_gro_flush_final_remcsum(skb, pp, flush, &grc);

	return pp;
}

static int gue_gro_complete(struct sock *sk, struct sk_buff *skb, int nhoff)
{
	const struct net_offload **offloads;
	struct guehdr *guehdr = (struct guehdr *)(skb->data + nhoff);
	const struct net_offload *ops;
	unsigned int guehlen = 0;
	u8 proto;
	int err = -ENOENT;

	switch (guehdr->version) {
	case 0:
		proto = guehdr->proto_ctype;
		guehlen = sizeof(*guehdr) + (guehdr->hlen << 2);
		break;
	case 1:
		switch (((struct iphdr *)guehdr)->version) {
		case 4:
			proto = IPPROTO_IPIP;
			break;
		case 6:
			proto = IPPROTO_IPV6;
			break;
		default:
			return err;
		}
		break;
	default:
		return err;
	}

	rcu_read_lock();
	offloads = NAPI_GRO_CB(skb)->is_ipv6 ? inet6_offloads : inet_offloads;
	ops = rcu_dereference(offloads[proto]);
	if (WARN_ON(!ops || !ops->callbacks.gro_complete))
		goto out_unlock;

	err = ops->callbacks.gro_complete(skb, nhoff + guehlen);

	skb_set_inner_mac_header(skb, nhoff + guehlen);

out_unlock:
	rcu_read_unlock();
	return err;
}

static bool fou_cfg_cmp(struct fou *fou, struct fou_cfg *cfg)
{
	struct sock *sk = fou->sock->sk;
	struct udp_port_cfg *udp_cfg = &cfg->udp_config;

	if (fou->family != udp_cfg->family ||
	    fou->port != udp_cfg->local_udp_port ||
	    sk->sk_dport != udp_cfg->peer_udp_port ||
	    sk->sk_bound_dev_if != udp_cfg->bind_ifindex)
		return false;

	if (fou->family == AF_INET) {
		if (sk->sk_rcv_saddr != udp_cfg->local_ip.s_addr ||
		    sk->sk_daddr != udp_cfg->peer_ip.s_addr)
			return false;
		else
			return true;
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		if (ipv6_addr_cmp(&sk->sk_v6_rcv_saddr, &udp_cfg->local_ip6) ||
		    ipv6_addr_cmp(&sk->sk_v6_daddr, &udp_cfg->peer_ip6))
			return false;
		else
			return true;
#endif
	}

	return false;
}

static int fou_add_to_port_list(struct net *net, struct fou *fou,
				struct fou_cfg *cfg)
{
	struct fou_net *fn = net_generic(net, fou_net_id);
	struct fou *fout;

	mutex_lock(&fn->fou_lock);
	list_for_each_entry(fout, &fn->fou_list, list) {
		if (fou_cfg_cmp(fout, cfg)) {
			mutex_unlock(&fn->fou_lock);
			return -EALREADY;
		}
	}

	list_add(&fou->list, &fn->fou_list);
	mutex_unlock(&fn->fou_lock);

	return 0;
}

static void fou_release(struct fou *fou)
{
	struct socket *sock = fou->sock;

	list_del(&fou->list);
	udp_tunnel_sock_release(sock);

	kfree_rcu(fou, rcu);
}

static int fou_create(struct net *net, struct fou_cfg *cfg,
		      struct socket **sockp)
{
	struct socket *sock = NULL;
	struct fou *fou = NULL;
	struct sock *sk;
	struct udp_tunnel_sock_cfg tunnel_cfg;
	int err;

	/* Open UDP socket */
	err = udp_sock_create(net, &cfg->udp_config, &sock);
	if (err < 0)
		goto error;

	/* Allocate FOU port structure */
	fou = kzalloc(sizeof(*fou), GFP_KERNEL);
	if (!fou) {
		err = -ENOMEM;
		goto error;
	}

	sk = sock->sk;

	fou->port = cfg->udp_config.local_udp_port;
	fou->family = cfg->udp_config.family;
	fou->flags = cfg->flags;
	fou->type = cfg->type;
	fou->sock = sock;

	memset(&tunnel_cfg, 0, sizeof(tunnel_cfg));
	tunnel_cfg.encap_type = 1;
	tunnel_cfg.sk_user_data = fou;
	tunnel_cfg.encap_destroy = NULL;

	/* Initial for fou type */
	switch (cfg->type) {
	case FOU_ENCAP_DIRECT:
		tunnel_cfg.encap_rcv = fou_udp_recv;
		tunnel_cfg.gro_receive = fou_gro_receive;
		tunnel_cfg.gro_complete = fou_gro_complete;
		fou->protocol = cfg->protocol;
		break;
	case FOU_ENCAP_GUE:
		tunnel_cfg.encap_rcv = gue_udp_recv;
		tunnel_cfg.gro_receive = gue_gro_receive;
		tunnel_cfg.gro_complete = gue_gro_complete;
		break;
	default:
		err = -EINVAL;
		goto error;
	}

	setup_udp_tunnel_sock(net, sock, &tunnel_cfg);

	sk->sk_allocation = GFP_ATOMIC;

	err = fou_add_to_port_list(net, fou, cfg);
	if (err)
		goto error;

	if (sockp)
		*sockp = sock;

	return 0;

error:
	kfree(fou);
	if (sock)
		udp_tunnel_sock_release(sock);

	return err;
}

static int fou_destroy(struct net *net, struct fou_cfg *cfg)
{
	struct fou_net *fn = net_generic(net, fou_net_id);
	int err = -EINVAL;
	struct fou *fou;

	mutex_lock(&fn->fou_lock);
	list_for_each_entry(fou, &fn->fou_list, list) {
		if (fou_cfg_cmp(fou, cfg)) {
			fou_release(fou);
			err = 0;
			break;
		}
	}
	mutex_unlock(&fn->fou_lock);

	return err;
}

static struct genl_family fou_nl_family;

static const struct nla_policy fou_nl_policy[FOU_ATTR_MAX + 1] = {
	[FOU_ATTR_PORT]			= { .type = NLA_U16, },
	[FOU_ATTR_AF]			= { .type = NLA_U8, },
	[FOU_ATTR_IPPROTO]		= { .type = NLA_U8, },
	[FOU_ATTR_TYPE]			= { .type = NLA_U8, },
	[FOU_ATTR_REMCSUM_NOPARTIAL]	= { .type = NLA_FLAG, },
	[FOU_ATTR_LOCAL_V4]		= { .type = NLA_U32, },
	[FOU_ATTR_PEER_V4]		= { .type = NLA_U32, },
	[FOU_ATTR_LOCAL_V6]		= { .len = sizeof(struct in6_addr), },
	[FOU_ATTR_PEER_V6]		= { .len = sizeof(struct in6_addr), },
	[FOU_ATTR_PEER_PORT]		= { .type = NLA_U16, },
	[FOU_ATTR_IFINDEX]		= { .type = NLA_S32, },
};

static int parse_nl_config(struct genl_info *info,
			   struct fou_cfg *cfg)
{
	bool has_local = false, has_peer = false;
	struct nlattr *attr;
	int ifindex;
	__be16 port;

	memset(cfg, 0, sizeof(*cfg));

	cfg->udp_config.family = AF_INET;

	if (info->attrs[FOU_ATTR_AF]) {
		u8 family = nla_get_u8(info->attrs[FOU_ATTR_AF]);

		switch (family) {
		case AF_INET:
			break;
		case AF_INET6:
			cfg->udp_config.ipv6_v6only = 1;
			break;
		default:
			return -EAFNOSUPPORT;
		}

		cfg->udp_config.family = family;
	}

	if (info->attrs[FOU_ATTR_PORT]) {
		port = nla_get_be16(info->attrs[FOU_ATTR_PORT]);
		cfg->udp_config.local_udp_port = port;
	}

	if (info->attrs[FOU_ATTR_IPPROTO])
		cfg->protocol = nla_get_u8(info->attrs[FOU_ATTR_IPPROTO]);

	if (info->attrs[FOU_ATTR_TYPE])
		cfg->type = nla_get_u8(info->attrs[FOU_ATTR_TYPE]);

	if (info->attrs[FOU_ATTR_REMCSUM_NOPARTIAL])
		cfg->flags |= FOU_F_REMCSUM_NOPARTIAL;

	if (cfg->udp_config.family == AF_INET) {
		if (info->attrs[FOU_ATTR_LOCAL_V4]) {
			attr = info->attrs[FOU_ATTR_LOCAL_V4];
			cfg->udp_config.local_ip.s_addr = nla_get_in_addr(attr);
			has_local = true;
		}

		if (info->attrs[FOU_ATTR_PEER_V4]) {
			attr = info->attrs[FOU_ATTR_PEER_V4];
			cfg->udp_config.peer_ip.s_addr = nla_get_in_addr(attr);
			has_peer = true;
		}
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		if (info->attrs[FOU_ATTR_LOCAL_V6]) {
			attr = info->attrs[FOU_ATTR_LOCAL_V6];
			cfg->udp_config.local_ip6 = nla_get_in6_addr(attr);
			has_local = true;
		}

		if (info->attrs[FOU_ATTR_PEER_V6]) {
			attr = info->attrs[FOU_ATTR_PEER_V6];
			cfg->udp_config.peer_ip6 = nla_get_in6_addr(attr);
			has_peer = true;
		}
#endif
	}

	if (has_peer) {
		if (info->attrs[FOU_ATTR_PEER_PORT]) {
			port = nla_get_be16(info->attrs[FOU_ATTR_PEER_PORT]);
			cfg->udp_config.peer_udp_port = port;
		} else {
			return -EINVAL;
		}
	}

	if (info->attrs[FOU_ATTR_IFINDEX]) {
		if (!has_local)
			return -EINVAL;

		ifindex = nla_get_s32(info->attrs[FOU_ATTR_IFINDEX]);

		cfg->udp_config.bind_ifindex = ifindex;
	}

	return 0;
}

static int fou_nl_cmd_add_port(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct fou_cfg cfg;
	int err;

	err = parse_nl_config(info, &cfg);
	if (err)
		return err;

	return fou_create(net, &cfg, NULL);
}

static int fou_nl_cmd_rm_port(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct fou_cfg cfg;
	int err;

	err = parse_nl_config(info, &cfg);
	if (err)
		return err;

	return fou_destroy(net, &cfg);
}

static int fou_fill_info(struct fou *fou, struct sk_buff *msg)
{
	struct sock *sk = fou->sock->sk;

	if (nla_put_u8(msg, FOU_ATTR_AF, fou->sock->sk->sk_family) ||
	    nla_put_be16(msg, FOU_ATTR_PORT, fou->port) ||
	    nla_put_be16(msg, FOU_ATTR_PEER_PORT, sk->sk_dport) ||
	    nla_put_u8(msg, FOU_ATTR_IPPROTO, fou->protocol) ||
	    nla_put_u8(msg, FOU_ATTR_TYPE, fou->type) ||
	    nla_put_s32(msg, FOU_ATTR_IFINDEX, sk->sk_bound_dev_if))
		return -1;

	if (fou->flags & FOU_F_REMCSUM_NOPARTIAL)
		if (nla_put_flag(msg, FOU_ATTR_REMCSUM_NOPARTIAL))
			return -1;

	if (fou->sock->sk->sk_family == AF_INET) {
		if (nla_put_in_addr(msg, FOU_ATTR_LOCAL_V4, sk->sk_rcv_saddr))
			return -1;

		if (nla_put_in_addr(msg, FOU_ATTR_PEER_V4, sk->sk_daddr))
			return -1;
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		if (nla_put_in6_addr(msg, FOU_ATTR_LOCAL_V6,
				     &sk->sk_v6_rcv_saddr))
			return -1;

		if (nla_put_in6_addr(msg, FOU_ATTR_PEER_V6, &sk->sk_v6_daddr))
			return -1;
#endif
	}

	return 0;
}

static int fou_dump_info(struct fou *fou, u32 portid, u32 seq,
			 u32 flags, struct sk_buff *skb, u8 cmd)
{
	void *hdr;

	hdr = genlmsg_put(skb, portid, seq, &fou_nl_family, flags, cmd);
	if (!hdr)
		return -ENOMEM;

	if (fou_fill_info(fou, skb) < 0)
		goto nla_put_failure;

	genlmsg_end(skb, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int fou_nl_cmd_get_port(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct fou_net *fn = net_generic(net, fou_net_id);
	struct sk_buff *msg;
	struct fou_cfg cfg;
	struct fou *fout;
	__be16 port;
	u8 family;
	int ret;

	ret = parse_nl_config(info, &cfg);
	if (ret)
		return ret;
	port = cfg.udp_config.local_udp_port;
	if (port == 0)
		return -EINVAL;

	family = cfg.udp_config.family;
	if (family != AF_INET && family != AF_INET6)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	ret = -ESRCH;
	mutex_lock(&fn->fou_lock);
	list_for_each_entry(fout, &fn->fou_list, list) {
		if (fou_cfg_cmp(fout, &cfg)) {
			ret = fou_dump_info(fout, info->snd_portid,
					    info->snd_seq, 0, msg,
					    info->genlhdr->cmd);
			break;
		}
	}
	mutex_unlock(&fn->fou_lock);
	if (ret < 0)
		goto out_free;

	return genlmsg_reply(msg, info);

out_free:
	nlmsg_free(msg);
	return ret;
}

static int fou_nl_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct fou_net *fn = net_generic(net, fou_net_id);
	struct fou *fout;
	int idx = 0, ret;

	mutex_lock(&fn->fou_lock);
	list_for_each_entry(fout, &fn->fou_list, list) {
		if (idx++ < cb->args[0])
			continue;
		ret = fou_dump_info(fout, NETLINK_CB(cb->skb).portid,
				    cb->nlh->nlmsg_seq, NLM_F_MULTI,
				    skb, FOU_CMD_GET);
		if (ret)
			break;
	}
	mutex_unlock(&fn->fou_lock);

	cb->args[0] = idx;
	return skb->len;
}

static const struct genl_small_ops fou_nl_ops[] = {
	{
		.cmd = FOU_CMD_ADD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = fou_nl_cmd_add_port,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = FOU_CMD_DEL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = fou_nl_cmd_rm_port,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = FOU_CMD_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = fou_nl_cmd_get_port,
		.dumpit = fou_nl_dump,
	},
};

static struct genl_family fou_nl_family __ro_after_init = {
	.hdrsize	= 0,
	.name		= FOU_GENL_NAME,
	.version	= FOU_GENL_VERSION,
	.maxattr	= FOU_ATTR_MAX,
	.policy = fou_nl_policy,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.small_ops	= fou_nl_ops,
	.n_small_ops	= ARRAY_SIZE(fou_nl_ops),
};

size_t fou_encap_hlen(struct ip_tunnel_encap *e)
{
	return sizeof(struct udphdr);
}
EXPORT_SYMBOL(fou_encap_hlen);

size_t gue_encap_hlen(struct ip_tunnel_encap *e)
{
	size_t len;
	bool need_priv = false;

	len = sizeof(struct udphdr) + sizeof(struct guehdr);

	if (e->flags & TUNNEL_ENCAP_FLAG_REMCSUM) {
		len += GUE_PLEN_REMCSUM;
		need_priv = true;
	}

	len += need_priv ? GUE_LEN_PRIV : 0;

	return len;
}
EXPORT_SYMBOL(gue_encap_hlen);

int __fou_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
		       u8 *protocol, __be16 *sport, int type)
{
	int err;

	err = iptunnel_handle_offloads(skb, type);
	if (err)
		return err;

	*sport = e->sport ? : udp_flow_src_port(dev_net(skb->dev),
						skb, 0, 0, false);

	return 0;
}
EXPORT_SYMBOL(__fou_build_header);

int __gue_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
		       u8 *protocol, __be16 *sport, int type)
{
	struct guehdr *guehdr;
	size_t hdrlen, optlen = 0;
	void *data;
	bool need_priv = false;
	int err;

	if ((e->flags & TUNNEL_ENCAP_FLAG_REMCSUM) &&
	    skb->ip_summed == CHECKSUM_PARTIAL) {
		optlen += GUE_PLEN_REMCSUM;
		type |= SKB_GSO_TUNNEL_REMCSUM;
		need_priv = true;
	}

	optlen += need_priv ? GUE_LEN_PRIV : 0;

	err = iptunnel_handle_offloads(skb, type);
	if (err)
		return err;

	/* Get source port (based on flow hash) before skb_push */
	*sport = e->sport ? : udp_flow_src_port(dev_net(skb->dev),
						skb, 0, 0, false);

	hdrlen = sizeof(struct guehdr) + optlen;

	skb_push(skb, hdrlen);

	guehdr = (struct guehdr *)skb->data;

	guehdr->control = 0;
	guehdr->version = 0;
	guehdr->hlen = optlen >> 2;
	guehdr->flags = 0;
	guehdr->proto_ctype = *protocol;

	data = &guehdr[1];

	if (need_priv) {
		__be32 *flags = data;

		guehdr->flags |= GUE_FLAG_PRIV;
		*flags = 0;
		data += GUE_LEN_PRIV;

		if (type & SKB_GSO_TUNNEL_REMCSUM) {
			u16 csum_start = skb_checksum_start_offset(skb);
			__be16 *pd = data;

			if (csum_start < hdrlen)
				return -EINVAL;

			csum_start -= hdrlen;
			pd[0] = htons(csum_start);
			pd[1] = htons(csum_start + skb->csum_offset);

			if (!skb_is_gso(skb)) {
				skb->ip_summed = CHECKSUM_NONE;
				skb->encapsulation = 0;
			}

			*flags |= GUE_PFLAG_REMCSUM;
			data += GUE_PLEN_REMCSUM;
		}

	}

	return 0;
}
EXPORT_SYMBOL(__gue_build_header);

#ifdef CONFIG_NET_FOU_IP_TUNNELS

static void fou_build_udp(struct sk_buff *skb, struct ip_tunnel_encap *e,
			  struct flowi4 *fl4, u8 *protocol, __be16 sport)
{
	struct udphdr *uh;

	skb_push(skb, sizeof(struct udphdr));
	skb_reset_transport_header(skb);

	uh = udp_hdr(skb);

	uh->dest = e->dport;
	uh->source = sport;
	uh->len = htons(skb->len);
	udp_set_csum(!(e->flags & TUNNEL_ENCAP_FLAG_CSUM), skb,
		     fl4->saddr, fl4->daddr, skb->len);

	*protocol = IPPROTO_UDP;
}

static int fou_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
			    u8 *protocol, struct flowi4 *fl4)
{
	int type = e->flags & TUNNEL_ENCAP_FLAG_CSUM ? SKB_GSO_UDP_TUNNEL_CSUM :
						       SKB_GSO_UDP_TUNNEL;
	__be16 sport;
	int err;

	err = __fou_build_header(skb, e, protocol, &sport, type);
	if (err)
		return err;

	fou_build_udp(skb, e, fl4, protocol, sport);

	return 0;
}

static int gue_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
			    u8 *protocol, struct flowi4 *fl4)
{
	int type = e->flags & TUNNEL_ENCAP_FLAG_CSUM ? SKB_GSO_UDP_TUNNEL_CSUM :
						       SKB_GSO_UDP_TUNNEL;
	__be16 sport;
	int err;

	err = __gue_build_header(skb, e, protocol, &sport, type);
	if (err)
		return err;

	fou_build_udp(skb, e, fl4, protocol, sport);

	return 0;
}

static int gue_err_proto_handler(int proto, struct sk_buff *skb, u32 info)
{
	const struct net_protocol *ipprot = rcu_dereference(inet_protos[proto]);

	if (ipprot && ipprot->err_handler) {
		if (!ipprot->err_handler(skb, info))
			return 0;
	}

	return -ENOENT;
}

static int gue_err(struct sk_buff *skb, u32 info)
{
	int transport_offset = skb_transport_offset(skb);
	struct guehdr *guehdr;
	size_t len, optlen;
	int ret;

	len = sizeof(struct udphdr) + sizeof(struct guehdr);
	if (!pskb_may_pull(skb, transport_offset + len))
		return -EINVAL;

	guehdr = (struct guehdr *)&udp_hdr(skb)[1];

	switch (guehdr->version) {
	case 0: /* Full GUE header present */
		break;
	case 1: {
		/* Direct encapsulation of IPv4 or IPv6 */
		skb_set_transport_header(skb, -(int)sizeof(struct icmphdr));

		switch (((struct iphdr *)guehdr)->version) {
		case 4:
			ret = gue_err_proto_handler(IPPROTO_IPIP, skb, info);
			goto out;
#if IS_ENABLED(CONFIG_IPV6)
		case 6:
			ret = gue_err_proto_handler(IPPROTO_IPV6, skb, info);
			goto out;
#endif
		default:
			ret = -EOPNOTSUPP;
			goto out;
		}
	}
	default: /* Undefined version */
		return -EOPNOTSUPP;
	}

	if (guehdr->control)
		return -ENOENT;

	optlen = guehdr->hlen << 2;

	if (!pskb_may_pull(skb, transport_offset + len + optlen))
		return -EINVAL;

	guehdr = (struct guehdr *)&udp_hdr(skb)[1];
	if (validate_gue_flags(guehdr, optlen))
		return -EINVAL;

	/* Handling exceptions for direct UDP encapsulation in GUE would lead to
	 * recursion. Besides, this kind of encapsulation can't even be
	 * configured currently. Discard this.
	 */
	if (guehdr->proto_ctype == IPPROTO_UDP ||
	    guehdr->proto_ctype == IPPROTO_UDPLITE)
		return -EOPNOTSUPP;

	skb_set_transport_header(skb, -(int)sizeof(struct icmphdr));
	ret = gue_err_proto_handler(guehdr->proto_ctype, skb, info);

out:
	skb_set_transport_header(skb, transport_offset);
	return ret;
}


static const struct ip_tunnel_encap_ops fou_iptun_ops = {
	.encap_hlen = fou_encap_hlen,
	.build_header = fou_build_header,
	.err_handler = gue_err,
};

static const struct ip_tunnel_encap_ops gue_iptun_ops = {
	.encap_hlen = gue_encap_hlen,
	.build_header = gue_build_header,
	.err_handler = gue_err,
};

static int ip_tunnel_encap_add_fou_ops(void)
{
	int ret;

	ret = ip_tunnel_encap_add_ops(&fou_iptun_ops, TUNNEL_ENCAP_FOU);
	if (ret < 0) {
		pr_err("can't add fou ops\n");
		return ret;
	}

	ret = ip_tunnel_encap_add_ops(&gue_iptun_ops, TUNNEL_ENCAP_GUE);
	if (ret < 0) {
		pr_err("can't add gue ops\n");
		ip_tunnel_encap_del_ops(&fou_iptun_ops, TUNNEL_ENCAP_FOU);
		return ret;
	}

	return 0;
}

static void ip_tunnel_encap_del_fou_ops(void)
{
	ip_tunnel_encap_del_ops(&fou_iptun_ops, TUNNEL_ENCAP_FOU);
	ip_tunnel_encap_del_ops(&gue_iptun_ops, TUNNEL_ENCAP_GUE);
}

#else

static int ip_tunnel_encap_add_fou_ops(void)
{
	return 0;
}

static void ip_tunnel_encap_del_fou_ops(void)
{
}

#endif

static __net_init int fou_init_net(struct net *net)
{
	struct fou_net *fn = net_generic(net, fou_net_id);

	INIT_LIST_HEAD(&fn->fou_list);
	mutex_init(&fn->fou_lock);
	return 0;
}

static __net_exit void fou_exit_net(struct net *net)
{
	struct fou_net *fn = net_generic(net, fou_net_id);
	struct fou *fou, *next;

	/* Close all the FOU sockets */
	mutex_lock(&fn->fou_lock);
	list_for_each_entry_safe(fou, next, &fn->fou_list, list)
		fou_release(fou);
	mutex_unlock(&fn->fou_lock);
}

static struct pernet_operations fou_net_ops = {
	.init = fou_init_net,
	.exit = fou_exit_net,
	.id   = &fou_net_id,
	.size = sizeof(struct fou_net),
};

static int __init fou_init(void)
{
	int ret;

	ret = register_pernet_device(&fou_net_ops);
	if (ret)
		goto exit;

	ret = genl_register_family(&fou_nl_family);
	if (ret < 0)
		goto unregister;

	ret = ip_tunnel_encap_add_fou_ops();
	if (ret == 0)
		return 0;

	genl_unregister_family(&fou_nl_family);
unregister:
	unregister_pernet_device(&fou_net_ops);
exit:
	return ret;
}

static void __exit fou_fini(void)
{
	ip_tunnel_encap_del_fou_ops();
	genl_unregister_family(&fou_nl_family);
	unregister_pernet_device(&fou_net_ops);
}

module_init(fou_init);
module_exit(fou_fini);
MODULE_AUTHOR("Tom Herbert <therbert@google.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Foo over UDP");
