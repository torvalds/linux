// SPDX-License-Identifier: GPL-2.0-only
/* L2TP netlink layer, for management
 *
 * Copyright (c) 2008,2009,2010 Katalix Systems Ltd
 *
 * Partly based on the IrDA nelink implementation
 * (see net/irda/irnetlink.c) which is:
 * Copyright (c) 2007 Samuel Ortiz <samuel@sortiz.org>
 * which is in turn partly based on the wireless netlink code:
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <net/sock.h>
#include <net/genetlink.h>
#include <net/udp.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/socket.h>
#include <linux/module.h>
#include <linux/list.h>
#include <net/net_namespace.h>

#include <linux/l2tp.h>

#include "l2tp_core.h"

static struct genl_family l2tp_nl_family;

static const struct genl_multicast_group l2tp_multicast_group[] = {
	{
		.name = L2TP_GENL_MCGROUP,
	},
};

static int l2tp_nl_tunnel_send(struct sk_buff *skb, u32 portid, u32 seq,
			       int flags, struct l2tp_tunnel *tunnel, u8 cmd);
static int l2tp_nl_session_send(struct sk_buff *skb, u32 portid, u32 seq,
				int flags, struct l2tp_session *session,
				u8 cmd);

/* Accessed under genl lock */
static const struct l2tp_nl_cmd_ops *l2tp_nl_cmd_ops[__L2TP_PWTYPE_MAX];

static struct l2tp_session *l2tp_nl_session_get(struct genl_info *info)
{
	u32 tunnel_id;
	u32 session_id;
	char *ifname;
	struct l2tp_tunnel *tunnel;
	struct l2tp_session *session = NULL;
	struct net *net = genl_info_net(info);

	if (info->attrs[L2TP_ATTR_IFNAME]) {
		ifname = nla_data(info->attrs[L2TP_ATTR_IFNAME]);
		session = l2tp_session_get_by_ifname(net, ifname);
	} else if ((info->attrs[L2TP_ATTR_SESSION_ID]) &&
		   (info->attrs[L2TP_ATTR_CONN_ID])) {
		tunnel_id = nla_get_u32(info->attrs[L2TP_ATTR_CONN_ID]);
		session_id = nla_get_u32(info->attrs[L2TP_ATTR_SESSION_ID]);
		tunnel = l2tp_tunnel_get(net, tunnel_id);
		if (tunnel) {
			session = l2tp_session_get(net, tunnel->sock, tunnel->version,
						   tunnel_id, session_id);
			l2tp_tunnel_put(tunnel);
		}
	}

	return session;
}

static int l2tp_nl_cmd_noop(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	void *hdr;
	int ret = -ENOBUFS;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq,
			  &l2tp_nl_family, 0, L2TP_CMD_NOOP);
	if (!hdr) {
		ret = -EMSGSIZE;
		goto err_out;
	}

	genlmsg_end(msg, hdr);

	return genlmsg_unicast(genl_info_net(info), msg, info->snd_portid);

err_out:
	nlmsg_free(msg);

out:
	return ret;
}

static int l2tp_tunnel_notify(struct genl_family *family,
			      struct genl_info *info,
			      struct l2tp_tunnel *tunnel,
			      u8 cmd)
{
	struct sk_buff *msg;
	int ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	ret = l2tp_nl_tunnel_send(msg, info->snd_portid, info->snd_seq,
				  NLM_F_ACK, tunnel, cmd);

	if (ret >= 0) {
		ret = genlmsg_multicast_allns(family, msg, 0, 0);
		/* We don't care if no one is listening */
		if (ret == -ESRCH)
			ret = 0;
		return ret;
	}

	nlmsg_free(msg);

	return ret;
}

static int l2tp_session_notify(struct genl_family *family,
			       struct genl_info *info,
			       struct l2tp_session *session,
			       u8 cmd)
{
	struct sk_buff *msg;
	int ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	ret = l2tp_nl_session_send(msg, info->snd_portid, info->snd_seq,
				   NLM_F_ACK, session, cmd);

	if (ret >= 0) {
		ret = genlmsg_multicast_allns(family, msg, 0, 0);
		/* We don't care if no one is listening */
		if (ret == -ESRCH)
			ret = 0;
		return ret;
	}

	nlmsg_free(msg);

	return ret;
}

static int l2tp_nl_cmd_tunnel_create_get_addr(struct nlattr **attrs, struct l2tp_tunnel_cfg *cfg)
{
	if (attrs[L2TP_ATTR_UDP_SPORT])
		cfg->local_udp_port = nla_get_u16(attrs[L2TP_ATTR_UDP_SPORT]);
	if (attrs[L2TP_ATTR_UDP_DPORT])
		cfg->peer_udp_port = nla_get_u16(attrs[L2TP_ATTR_UDP_DPORT]);
	cfg->use_udp_checksums = nla_get_flag(attrs[L2TP_ATTR_UDP_CSUM]);

	/* Must have either AF_INET or AF_INET6 address for source and destination */
#if IS_ENABLED(CONFIG_IPV6)
	if (attrs[L2TP_ATTR_IP6_SADDR] && attrs[L2TP_ATTR_IP6_DADDR]) {
		cfg->local_ip6 = nla_data(attrs[L2TP_ATTR_IP6_SADDR]);
		cfg->peer_ip6 = nla_data(attrs[L2TP_ATTR_IP6_DADDR]);
		cfg->udp6_zero_tx_checksums = nla_get_flag(attrs[L2TP_ATTR_UDP_ZERO_CSUM6_TX]);
		cfg->udp6_zero_rx_checksums = nla_get_flag(attrs[L2TP_ATTR_UDP_ZERO_CSUM6_RX]);
		return 0;
	}
#endif
	if (attrs[L2TP_ATTR_IP_SADDR] && attrs[L2TP_ATTR_IP_DADDR]) {
		cfg->local_ip.s_addr = nla_get_in_addr(attrs[L2TP_ATTR_IP_SADDR]);
		cfg->peer_ip.s_addr = nla_get_in_addr(attrs[L2TP_ATTR_IP_DADDR]);
		return 0;
	}
	return -EINVAL;
}

static int l2tp_nl_cmd_tunnel_create(struct sk_buff *skb, struct genl_info *info)
{
	u32 tunnel_id;
	u32 peer_tunnel_id;
	int proto_version;
	int fd = -1;
	int ret = 0;
	struct l2tp_tunnel_cfg cfg = { 0, };
	struct l2tp_tunnel *tunnel;
	struct net *net = genl_info_net(info);
	struct nlattr **attrs = info->attrs;

	if (!attrs[L2TP_ATTR_CONN_ID]) {
		ret = -EINVAL;
		goto out;
	}
	tunnel_id = nla_get_u32(attrs[L2TP_ATTR_CONN_ID]);

	if (!attrs[L2TP_ATTR_PEER_CONN_ID]) {
		ret = -EINVAL;
		goto out;
	}
	peer_tunnel_id = nla_get_u32(attrs[L2TP_ATTR_PEER_CONN_ID]);

	if (!attrs[L2TP_ATTR_PROTO_VERSION]) {
		ret = -EINVAL;
		goto out;
	}
	proto_version = nla_get_u8(attrs[L2TP_ATTR_PROTO_VERSION]);

	if (!attrs[L2TP_ATTR_ENCAP_TYPE]) {
		ret = -EINVAL;
		goto out;
	}
	cfg.encap = nla_get_u16(attrs[L2TP_ATTR_ENCAP_TYPE]);

	/* Managed tunnels take the tunnel socket from userspace.
	 * Unmanaged tunnels must call out the source and destination addresses
	 * for the kernel to create the tunnel socket itself.
	 */
	if (attrs[L2TP_ATTR_FD]) {
		fd = nla_get_u32(attrs[L2TP_ATTR_FD]);
	} else {
		ret = l2tp_nl_cmd_tunnel_create_get_addr(attrs, &cfg);
		if (ret < 0)
			goto out;
	}

	ret = -EINVAL;
	switch (cfg.encap) {
	case L2TP_ENCAPTYPE_UDP:
	case L2TP_ENCAPTYPE_IP:
		ret = l2tp_tunnel_create(fd, proto_version, tunnel_id,
					 peer_tunnel_id, &cfg, &tunnel);
		break;
	}

	if (ret < 0)
		goto out;

	refcount_inc(&tunnel->ref_count);
	ret = l2tp_tunnel_register(tunnel, net, &cfg);
	if (ret < 0) {
		kfree(tunnel);
		goto out;
	}
	ret = l2tp_tunnel_notify(&l2tp_nl_family, info, tunnel,
				 L2TP_CMD_TUNNEL_CREATE);
	l2tp_tunnel_put(tunnel);

out:
	return ret;
}

static int l2tp_nl_cmd_tunnel_delete(struct sk_buff *skb, struct genl_info *info)
{
	struct l2tp_tunnel *tunnel;
	u32 tunnel_id;
	int ret = 0;
	struct net *net = genl_info_net(info);

	if (!info->attrs[L2TP_ATTR_CONN_ID]) {
		ret = -EINVAL;
		goto out;
	}
	tunnel_id = nla_get_u32(info->attrs[L2TP_ATTR_CONN_ID]);

	tunnel = l2tp_tunnel_get(net, tunnel_id);
	if (!tunnel) {
		ret = -ENODEV;
		goto out;
	}

	l2tp_tunnel_notify(&l2tp_nl_family, info,
			   tunnel, L2TP_CMD_TUNNEL_DELETE);

	l2tp_tunnel_delete(tunnel);

	l2tp_tunnel_put(tunnel);

out:
	return ret;
}

static int l2tp_nl_cmd_tunnel_modify(struct sk_buff *skb, struct genl_info *info)
{
	struct l2tp_tunnel *tunnel;
	u32 tunnel_id;
	int ret = 0;
	struct net *net = genl_info_net(info);

	if (!info->attrs[L2TP_ATTR_CONN_ID]) {
		ret = -EINVAL;
		goto out;
	}
	tunnel_id = nla_get_u32(info->attrs[L2TP_ATTR_CONN_ID]);

	tunnel = l2tp_tunnel_get(net, tunnel_id);
	if (!tunnel) {
		ret = -ENODEV;
		goto out;
	}

	ret = l2tp_tunnel_notify(&l2tp_nl_family, info,
				 tunnel, L2TP_CMD_TUNNEL_MODIFY);

	l2tp_tunnel_put(tunnel);

out:
	return ret;
}

#if IS_ENABLED(CONFIG_IPV6)
static int l2tp_nl_tunnel_send_addr6(struct sk_buff *skb, struct sock *sk,
				     enum l2tp_encap_type encap)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);

	switch (encap) {
	case L2TP_ENCAPTYPE_UDP:
		if (udp_get_no_check6_tx(sk) &&
		    nla_put_flag(skb, L2TP_ATTR_UDP_ZERO_CSUM6_TX))
			return -1;
		if (udp_get_no_check6_rx(sk) &&
		    nla_put_flag(skb, L2TP_ATTR_UDP_ZERO_CSUM6_RX))
			return -1;
		if (nla_put_u16(skb, L2TP_ATTR_UDP_SPORT, ntohs(inet->inet_sport)) ||
		    nla_put_u16(skb, L2TP_ATTR_UDP_DPORT, ntohs(inet->inet_dport)))
			return -1;
		fallthrough;
	case L2TP_ENCAPTYPE_IP:
		if (nla_put_in6_addr(skb, L2TP_ATTR_IP6_SADDR, &np->saddr) ||
		    nla_put_in6_addr(skb, L2TP_ATTR_IP6_DADDR, &sk->sk_v6_daddr))
			return -1;
		break;
	}
	return 0;
}
#endif

static int l2tp_nl_tunnel_send_addr4(struct sk_buff *skb, struct sock *sk,
				     enum l2tp_encap_type encap)
{
	struct inet_sock *inet = inet_sk(sk);

	switch (encap) {
	case L2TP_ENCAPTYPE_UDP:
		if (nla_put_u8(skb, L2TP_ATTR_UDP_CSUM, !sk->sk_no_check_tx) ||
		    nla_put_u16(skb, L2TP_ATTR_UDP_SPORT, ntohs(inet->inet_sport)) ||
		    nla_put_u16(skb, L2TP_ATTR_UDP_DPORT, ntohs(inet->inet_dport)))
			return -1;
		fallthrough;
	case L2TP_ENCAPTYPE_IP:
		if (nla_put_in_addr(skb, L2TP_ATTR_IP_SADDR, inet->inet_saddr) ||
		    nla_put_in_addr(skb, L2TP_ATTR_IP_DADDR, inet->inet_daddr))
			return -1;
		break;
	}

	return 0;
}

/* Append attributes for the tunnel address, handling the different attribute types
 * used for different tunnel encapsulation and AF_INET v.s. AF_INET6.
 */
static int l2tp_nl_tunnel_send_addr(struct sk_buff *skb, struct l2tp_tunnel *tunnel)
{
	struct sock *sk = tunnel->sock;

	if (!sk)
		return 0;

#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6)
		return l2tp_nl_tunnel_send_addr6(skb, sk, tunnel->encap);
#endif
	return l2tp_nl_tunnel_send_addr4(skb, sk, tunnel->encap);
}

static int l2tp_nl_tunnel_send(struct sk_buff *skb, u32 portid, u32 seq, int flags,
			       struct l2tp_tunnel *tunnel, u8 cmd)
{
	void *hdr;
	struct nlattr *nest;

	hdr = genlmsg_put(skb, portid, seq, &l2tp_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u8(skb, L2TP_ATTR_PROTO_VERSION, tunnel->version) ||
	    nla_put_u32(skb, L2TP_ATTR_CONN_ID, tunnel->tunnel_id) ||
	    nla_put_u32(skb, L2TP_ATTR_PEER_CONN_ID, tunnel->peer_tunnel_id) ||
	    nla_put_u32(skb, L2TP_ATTR_DEBUG, 0) ||
	    nla_put_u16(skb, L2TP_ATTR_ENCAP_TYPE, tunnel->encap))
		goto nla_put_failure;

	nest = nla_nest_start_noflag(skb, L2TP_ATTR_STATS);
	if (!nest)
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, L2TP_ATTR_TX_PACKETS,
			      atomic_long_read(&tunnel->stats.tx_packets),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_TX_BYTES,
			      atomic_long_read(&tunnel->stats.tx_bytes),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_TX_ERRORS,
			      atomic_long_read(&tunnel->stats.tx_errors),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_PACKETS,
			      atomic_long_read(&tunnel->stats.rx_packets),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_BYTES,
			      atomic_long_read(&tunnel->stats.rx_bytes),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_SEQ_DISCARDS,
			      atomic_long_read(&tunnel->stats.rx_seq_discards),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_COOKIE_DISCARDS,
			      atomic_long_read(&tunnel->stats.rx_cookie_discards),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_OOS_PACKETS,
			      atomic_long_read(&tunnel->stats.rx_oos_packets),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_ERRORS,
			      atomic_long_read(&tunnel->stats.rx_errors),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_INVALID,
			      atomic_long_read(&tunnel->stats.rx_invalid),
			      L2TP_ATTR_STATS_PAD))
		goto nla_put_failure;
	nla_nest_end(skb, nest);

	if (l2tp_nl_tunnel_send_addr(skb, tunnel))
		goto nla_put_failure;

	genlmsg_end(skb, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -1;
}

static int l2tp_nl_cmd_tunnel_get(struct sk_buff *skb, struct genl_info *info)
{
	struct l2tp_tunnel *tunnel;
	struct sk_buff *msg;
	u32 tunnel_id;
	int ret = -ENOBUFS;
	struct net *net = genl_info_net(info);

	if (!info->attrs[L2TP_ATTR_CONN_ID]) {
		ret = -EINVAL;
		goto err;
	}

	tunnel_id = nla_get_u32(info->attrs[L2TP_ATTR_CONN_ID]);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto err;
	}

	tunnel = l2tp_tunnel_get(net, tunnel_id);
	if (!tunnel) {
		ret = -ENODEV;
		goto err_nlmsg;
	}

	ret = l2tp_nl_tunnel_send(msg, info->snd_portid, info->snd_seq,
				  NLM_F_ACK, tunnel, L2TP_CMD_TUNNEL_GET);
	if (ret < 0)
		goto err_nlmsg_tunnel;

	l2tp_tunnel_put(tunnel);

	return genlmsg_unicast(net, msg, info->snd_portid);

err_nlmsg_tunnel:
	l2tp_tunnel_put(tunnel);
err_nlmsg:
	nlmsg_free(msg);
err:
	return ret;
}

struct l2tp_nl_cb_data {
	unsigned long tkey;
	unsigned long skey;
};

static int l2tp_nl_cmd_tunnel_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct l2tp_nl_cb_data *cbd = (void *)&cb->ctx[0];
	unsigned long key = cbd->tkey;
	struct l2tp_tunnel *tunnel;
	struct net *net = sock_net(skb->sk);

	for (;;) {
		tunnel = l2tp_tunnel_get_next(net, &key);
		if (!tunnel)
			goto out;

		if (l2tp_nl_tunnel_send(skb, NETLINK_CB(cb->skb).portid,
					cb->nlh->nlmsg_seq, NLM_F_MULTI,
					tunnel, L2TP_CMD_TUNNEL_GET) < 0) {
			l2tp_tunnel_put(tunnel);
			goto out;
		}
		l2tp_tunnel_put(tunnel);

		key++;
	}

out:
	cbd->tkey = key;

	return skb->len;
}

static int l2tp_nl_cmd_session_create(struct sk_buff *skb, struct genl_info *info)
{
	u32 tunnel_id = 0;
	u32 session_id;
	u32 peer_session_id;
	int ret = 0;
	struct l2tp_tunnel *tunnel;
	struct l2tp_session *session;
	struct l2tp_session_cfg cfg = { 0, };
	struct net *net = genl_info_net(info);

	if (!info->attrs[L2TP_ATTR_CONN_ID]) {
		ret = -EINVAL;
		goto out;
	}

	tunnel_id = nla_get_u32(info->attrs[L2TP_ATTR_CONN_ID]);
	tunnel = l2tp_tunnel_get(net, tunnel_id);
	if (!tunnel) {
		ret = -ENODEV;
		goto out;
	}

	if (!info->attrs[L2TP_ATTR_SESSION_ID]) {
		ret = -EINVAL;
		goto out_tunnel;
	}
	session_id = nla_get_u32(info->attrs[L2TP_ATTR_SESSION_ID]);

	if (!info->attrs[L2TP_ATTR_PEER_SESSION_ID]) {
		ret = -EINVAL;
		goto out_tunnel;
	}
	peer_session_id = nla_get_u32(info->attrs[L2TP_ATTR_PEER_SESSION_ID]);

	if (!info->attrs[L2TP_ATTR_PW_TYPE]) {
		ret = -EINVAL;
		goto out_tunnel;
	}
	cfg.pw_type = nla_get_u16(info->attrs[L2TP_ATTR_PW_TYPE]);
	if (cfg.pw_type >= __L2TP_PWTYPE_MAX) {
		ret = -EINVAL;
		goto out_tunnel;
	}

	/* L2TPv2 only accepts PPP pseudo-wires */
	if (tunnel->version == 2 && cfg.pw_type != L2TP_PWTYPE_PPP) {
		ret = -EPROTONOSUPPORT;
		goto out_tunnel;
	}

	if (tunnel->version > 2) {
		if (info->attrs[L2TP_ATTR_L2SPEC_TYPE]) {
			cfg.l2specific_type = nla_get_u8(info->attrs[L2TP_ATTR_L2SPEC_TYPE]);
			if (cfg.l2specific_type != L2TP_L2SPECTYPE_DEFAULT &&
			    cfg.l2specific_type != L2TP_L2SPECTYPE_NONE) {
				ret = -EINVAL;
				goto out_tunnel;
			}
		} else {
			cfg.l2specific_type = L2TP_L2SPECTYPE_DEFAULT;
		}

		if (info->attrs[L2TP_ATTR_COOKIE]) {
			u16 len = nla_len(info->attrs[L2TP_ATTR_COOKIE]);

			if (len > 8) {
				ret = -EINVAL;
				goto out_tunnel;
			}
			cfg.cookie_len = len;
			memcpy(&cfg.cookie[0], nla_data(info->attrs[L2TP_ATTR_COOKIE]), len);
		}
		if (info->attrs[L2TP_ATTR_PEER_COOKIE]) {
			u16 len = nla_len(info->attrs[L2TP_ATTR_PEER_COOKIE]);

			if (len > 8) {
				ret = -EINVAL;
				goto out_tunnel;
			}
			cfg.peer_cookie_len = len;
			memcpy(&cfg.peer_cookie[0], nla_data(info->attrs[L2TP_ATTR_PEER_COOKIE]), len);
		}
		if (info->attrs[L2TP_ATTR_IFNAME])
			cfg.ifname = nla_data(info->attrs[L2TP_ATTR_IFNAME]);
	}

	if (info->attrs[L2TP_ATTR_RECV_SEQ])
		cfg.recv_seq = nla_get_u8(info->attrs[L2TP_ATTR_RECV_SEQ]);

	if (info->attrs[L2TP_ATTR_SEND_SEQ])
		cfg.send_seq = nla_get_u8(info->attrs[L2TP_ATTR_SEND_SEQ]);

	if (info->attrs[L2TP_ATTR_LNS_MODE])
		cfg.lns_mode = nla_get_u8(info->attrs[L2TP_ATTR_LNS_MODE]);

	if (info->attrs[L2TP_ATTR_RECV_TIMEOUT])
		cfg.reorder_timeout = nla_get_msecs(info->attrs[L2TP_ATTR_RECV_TIMEOUT]);

#ifdef CONFIG_MODULES
	if (!l2tp_nl_cmd_ops[cfg.pw_type]) {
		genl_unlock();
		request_module("net-l2tp-type-%u", cfg.pw_type);
		genl_lock();
	}
#endif
	if (!l2tp_nl_cmd_ops[cfg.pw_type] || !l2tp_nl_cmd_ops[cfg.pw_type]->session_create) {
		ret = -EPROTONOSUPPORT;
		goto out_tunnel;
	}

	ret = l2tp_nl_cmd_ops[cfg.pw_type]->session_create(net, tunnel,
							   session_id,
							   peer_session_id,
							   &cfg);

	if (ret >= 0) {
		session = l2tp_session_get(net, tunnel->sock, tunnel->version,
					   tunnel_id, session_id);
		if (session) {
			ret = l2tp_session_notify(&l2tp_nl_family, info, session,
						  L2TP_CMD_SESSION_CREATE);
			l2tp_session_put(session);
		}
	}

out_tunnel:
	l2tp_tunnel_put(tunnel);
out:
	return ret;
}

static int l2tp_nl_cmd_session_delete(struct sk_buff *skb, struct genl_info *info)
{
	int ret = 0;
	struct l2tp_session *session;
	u16 pw_type;

	session = l2tp_nl_session_get(info);
	if (!session) {
		ret = -ENODEV;
		goto out;
	}

	l2tp_session_notify(&l2tp_nl_family, info,
			    session, L2TP_CMD_SESSION_DELETE);

	pw_type = session->pwtype;
	if (pw_type < __L2TP_PWTYPE_MAX)
		if (l2tp_nl_cmd_ops[pw_type] && l2tp_nl_cmd_ops[pw_type]->session_delete)
			l2tp_nl_cmd_ops[pw_type]->session_delete(session);

	l2tp_session_put(session);

out:
	return ret;
}

static int l2tp_nl_cmd_session_modify(struct sk_buff *skb, struct genl_info *info)
{
	int ret = 0;
	struct l2tp_session *session;

	session = l2tp_nl_session_get(info);
	if (!session) {
		ret = -ENODEV;
		goto out;
	}

	if (info->attrs[L2TP_ATTR_RECV_SEQ])
		session->recv_seq = nla_get_u8(info->attrs[L2TP_ATTR_RECV_SEQ]);

	if (info->attrs[L2TP_ATTR_SEND_SEQ]) {
		struct l2tp_tunnel *tunnel = session->tunnel;

		session->send_seq = nla_get_u8(info->attrs[L2TP_ATTR_SEND_SEQ]);
		l2tp_session_set_header_len(session, tunnel->version, tunnel->encap);
	}

	if (info->attrs[L2TP_ATTR_LNS_MODE])
		session->lns_mode = nla_get_u8(info->attrs[L2TP_ATTR_LNS_MODE]);

	if (info->attrs[L2TP_ATTR_RECV_TIMEOUT])
		session->reorder_timeout = nla_get_msecs(info->attrs[L2TP_ATTR_RECV_TIMEOUT]);

	ret = l2tp_session_notify(&l2tp_nl_family, info,
				  session, L2TP_CMD_SESSION_MODIFY);

	l2tp_session_put(session);

out:
	return ret;
}

static int l2tp_nl_session_send(struct sk_buff *skb, u32 portid, u32 seq, int flags,
				struct l2tp_session *session, u8 cmd)
{
	void *hdr;
	struct nlattr *nest;
	struct l2tp_tunnel *tunnel = session->tunnel;

	hdr = genlmsg_put(skb, portid, seq, &l2tp_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(skb, L2TP_ATTR_CONN_ID, tunnel->tunnel_id) ||
	    nla_put_u32(skb, L2TP_ATTR_SESSION_ID, session->session_id) ||
	    nla_put_u32(skb, L2TP_ATTR_PEER_CONN_ID, tunnel->peer_tunnel_id) ||
	    nla_put_u32(skb, L2TP_ATTR_PEER_SESSION_ID, session->peer_session_id) ||
	    nla_put_u32(skb, L2TP_ATTR_DEBUG, 0) ||
	    nla_put_u16(skb, L2TP_ATTR_PW_TYPE, session->pwtype))
		goto nla_put_failure;

	if ((session->ifname[0] &&
	     nla_put_string(skb, L2TP_ATTR_IFNAME, session->ifname)) ||
	    (session->cookie_len &&
	     nla_put(skb, L2TP_ATTR_COOKIE, session->cookie_len, session->cookie)) ||
	    (session->peer_cookie_len &&
	     nla_put(skb, L2TP_ATTR_PEER_COOKIE, session->peer_cookie_len, session->peer_cookie)) ||
	    nla_put_u8(skb, L2TP_ATTR_RECV_SEQ, session->recv_seq) ||
	    nla_put_u8(skb, L2TP_ATTR_SEND_SEQ, session->send_seq) ||
	    nla_put_u8(skb, L2TP_ATTR_LNS_MODE, session->lns_mode) ||
	    (l2tp_tunnel_uses_xfrm(tunnel) &&
	     nla_put_u8(skb, L2TP_ATTR_USING_IPSEC, 1)) ||
	    (session->reorder_timeout &&
	     nla_put_msecs(skb, L2TP_ATTR_RECV_TIMEOUT,
			   session->reorder_timeout, L2TP_ATTR_PAD)))
		goto nla_put_failure;

	nest = nla_nest_start_noflag(skb, L2TP_ATTR_STATS);
	if (!nest)
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, L2TP_ATTR_TX_PACKETS,
			      atomic_long_read(&session->stats.tx_packets),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_TX_BYTES,
			      atomic_long_read(&session->stats.tx_bytes),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_TX_ERRORS,
			      atomic_long_read(&session->stats.tx_errors),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_PACKETS,
			      atomic_long_read(&session->stats.rx_packets),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_BYTES,
			      atomic_long_read(&session->stats.rx_bytes),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_SEQ_DISCARDS,
			      atomic_long_read(&session->stats.rx_seq_discards),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_COOKIE_DISCARDS,
			      atomic_long_read(&session->stats.rx_cookie_discards),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_OOS_PACKETS,
			      atomic_long_read(&session->stats.rx_oos_packets),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_ERRORS,
			      atomic_long_read(&session->stats.rx_errors),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_INVALID,
			      atomic_long_read(&session->stats.rx_invalid),
			      L2TP_ATTR_STATS_PAD))
		goto nla_put_failure;
	nla_nest_end(skb, nest);

	genlmsg_end(skb, hdr);
	return 0;

 nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -1;
}

static int l2tp_nl_cmd_session_get(struct sk_buff *skb, struct genl_info *info)
{
	struct l2tp_session *session;
	struct sk_buff *msg;
	int ret;

	session = l2tp_nl_session_get(info);
	if (!session) {
		ret = -ENODEV;
		goto err;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto err_ref;
	}

	ret = l2tp_nl_session_send(msg, info->snd_portid, info->snd_seq,
				   0, session, L2TP_CMD_SESSION_GET);
	if (ret < 0)
		goto err_ref_msg;

	ret = genlmsg_unicast(genl_info_net(info), msg, info->snd_portid);

	l2tp_session_put(session);

	return ret;

err_ref_msg:
	nlmsg_free(msg);
err_ref:
	l2tp_session_put(session);
err:
	return ret;
}

static int l2tp_nl_cmd_session_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct l2tp_nl_cb_data *cbd = (void *)&cb->ctx[0];
	struct net *net = sock_net(skb->sk);
	struct l2tp_session *session;
	struct l2tp_tunnel *tunnel = NULL;
	unsigned long tkey = cbd->tkey;
	unsigned long skey = cbd->skey;

	for (;;) {
		if (!tunnel) {
			tunnel = l2tp_tunnel_get_next(net, &tkey);
			if (!tunnel)
				goto out;
		}

		session = l2tp_session_get_next(net, tunnel->sock, tunnel->version,
						tunnel->tunnel_id, &skey);
		if (!session) {
			tkey++;
			l2tp_tunnel_put(tunnel);
			tunnel = NULL;
			skey = 0;
			continue;
		}

		if (l2tp_nl_session_send(skb, NETLINK_CB(cb->skb).portid,
					 cb->nlh->nlmsg_seq, NLM_F_MULTI,
					 session, L2TP_CMD_SESSION_GET) < 0) {
			l2tp_session_put(session);
			l2tp_tunnel_put(tunnel);
			break;
		}
		l2tp_session_put(session);

		skey++;
	}

out:
	cbd->tkey = tkey;
	cbd->skey = skey;

	return skb->len;
}

static const struct nla_policy l2tp_nl_policy[L2TP_ATTR_MAX + 1] = {
	[L2TP_ATTR_NONE]		= { .type = NLA_UNSPEC, },
	[L2TP_ATTR_PW_TYPE]		= { .type = NLA_U16, },
	[L2TP_ATTR_ENCAP_TYPE]		= { .type = NLA_U16, },
	[L2TP_ATTR_OFFSET]		= { .type = NLA_U16, },
	[L2TP_ATTR_DATA_SEQ]		= { .type = NLA_U8, },
	[L2TP_ATTR_L2SPEC_TYPE]		= { .type = NLA_U8, },
	[L2TP_ATTR_L2SPEC_LEN]		= { .type = NLA_U8, },
	[L2TP_ATTR_PROTO_VERSION]	= { .type = NLA_U8, },
	[L2TP_ATTR_CONN_ID]		= { .type = NLA_U32, },
	[L2TP_ATTR_PEER_CONN_ID]	= { .type = NLA_U32, },
	[L2TP_ATTR_SESSION_ID]		= { .type = NLA_U32, },
	[L2TP_ATTR_PEER_SESSION_ID]	= { .type = NLA_U32, },
	[L2TP_ATTR_UDP_CSUM]		= { .type = NLA_U8, },
	[L2TP_ATTR_VLAN_ID]		= { .type = NLA_U16, },
	[L2TP_ATTR_DEBUG]		= { .type = NLA_U32, },
	[L2TP_ATTR_RECV_SEQ]		= { .type = NLA_U8, },
	[L2TP_ATTR_SEND_SEQ]		= { .type = NLA_U8, },
	[L2TP_ATTR_LNS_MODE]		= { .type = NLA_U8, },
	[L2TP_ATTR_USING_IPSEC]		= { .type = NLA_U8, },
	[L2TP_ATTR_RECV_TIMEOUT]	= { .type = NLA_MSECS, },
	[L2TP_ATTR_FD]			= { .type = NLA_U32, },
	[L2TP_ATTR_IP_SADDR]		= { .type = NLA_U32, },
	[L2TP_ATTR_IP_DADDR]		= { .type = NLA_U32, },
	[L2TP_ATTR_UDP_SPORT]		= { .type = NLA_U16, },
	[L2TP_ATTR_UDP_DPORT]		= { .type = NLA_U16, },
	[L2TP_ATTR_MTU]			= { .type = NLA_U16, },
	[L2TP_ATTR_MRU]			= { .type = NLA_U16, },
	[L2TP_ATTR_STATS]		= { .type = NLA_NESTED, },
	[L2TP_ATTR_IP6_SADDR] = {
		.type = NLA_BINARY,
		.len = sizeof(struct in6_addr),
	},
	[L2TP_ATTR_IP6_DADDR] = {
		.type = NLA_BINARY,
		.len = sizeof(struct in6_addr),
	},
	[L2TP_ATTR_IFNAME] = {
		.type = NLA_NUL_STRING,
		.len = IFNAMSIZ - 1,
	},
	[L2TP_ATTR_COOKIE] = {
		.type = NLA_BINARY,
		.len = 8,
	},
	[L2TP_ATTR_PEER_COOKIE] = {
		.type = NLA_BINARY,
		.len = 8,
	},
};

static const struct genl_small_ops l2tp_nl_ops[] = {
	{
		.cmd = L2TP_CMD_NOOP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = l2tp_nl_cmd_noop,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = L2TP_CMD_TUNNEL_CREATE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = l2tp_nl_cmd_tunnel_create,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_TUNNEL_DELETE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = l2tp_nl_cmd_tunnel_delete,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_TUNNEL_MODIFY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = l2tp_nl_cmd_tunnel_modify,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_TUNNEL_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = l2tp_nl_cmd_tunnel_get,
		.dumpit = l2tp_nl_cmd_tunnel_dump,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_SESSION_CREATE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = l2tp_nl_cmd_session_create,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_SESSION_DELETE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = l2tp_nl_cmd_session_delete,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_SESSION_MODIFY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = l2tp_nl_cmd_session_modify,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_SESSION_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = l2tp_nl_cmd_session_get,
		.dumpit = l2tp_nl_cmd_session_dump,
		.flags = GENL_UNS_ADMIN_PERM,
	},
};

static struct genl_family l2tp_nl_family __ro_after_init = {
	.name		= L2TP_GENL_NAME,
	.version	= L2TP_GENL_VERSION,
	.hdrsize	= 0,
	.maxattr	= L2TP_ATTR_MAX,
	.policy = l2tp_nl_policy,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.small_ops	= l2tp_nl_ops,
	.n_small_ops	= ARRAY_SIZE(l2tp_nl_ops),
	.resv_start_op	= L2TP_CMD_SESSION_GET + 1,
	.mcgrps		= l2tp_multicast_group,
	.n_mcgrps	= ARRAY_SIZE(l2tp_multicast_group),
};

int l2tp_nl_register_ops(enum l2tp_pwtype pw_type, const struct l2tp_nl_cmd_ops *ops)
{
	int ret;

	ret = -EINVAL;
	if (pw_type >= __L2TP_PWTYPE_MAX)
		goto err;

	genl_lock();
	ret = -EBUSY;
	if (l2tp_nl_cmd_ops[pw_type])
		goto out;

	l2tp_nl_cmd_ops[pw_type] = ops;
	ret = 0;

out:
	genl_unlock();
err:
	return ret;
}
EXPORT_SYMBOL_GPL(l2tp_nl_register_ops);

void l2tp_nl_unregister_ops(enum l2tp_pwtype pw_type)
{
	if (pw_type < __L2TP_PWTYPE_MAX) {
		genl_lock();
		l2tp_nl_cmd_ops[pw_type] = NULL;
		genl_unlock();
	}
}
EXPORT_SYMBOL_GPL(l2tp_nl_unregister_ops);

static int __init l2tp_nl_init(void)
{
	pr_info("L2TP netlink interface\n");
	return genl_register_family(&l2tp_nl_family);
}

static void l2tp_nl_cleanup(void)
{
	genl_unregister_family(&l2tp_nl_family);
}

module_init(l2tp_nl_init);
module_exit(l2tp_nl_cleanup);

MODULE_AUTHOR("James Chapman <jchapman@katalix.com>");
MODULE_DESCRIPTION("L2TP netlink");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_ALIAS_GENL_FAMILY("l2tp");
