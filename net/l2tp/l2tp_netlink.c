/*
 * L2TP netlink layer, for management
 *
 * Copyright (c) 2008,2009,2010 Katalix Systems Ltd
 *
 * Partly based on the IrDA nelink implementation
 * (see net/irda/irnetlink.c) which is:
 * Copyright (c) 2007 Samuel Ortiz <samuel@sortiz.org>
 * which is in turn partly based on the wireless netlink code:
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

static struct l2tp_session *l2tp_nl_session_find(struct genl_info *info)
{
	u32 tunnel_id;
	u32 session_id;
	char *ifname;
	struct l2tp_tunnel *tunnel;
	struct l2tp_session *session = NULL;
	struct net *net = genl_info_net(info);

	if (info->attrs[L2TP_ATTR_IFNAME]) {
		ifname = nla_data(info->attrs[L2TP_ATTR_IFNAME]);
		session = l2tp_session_find_by_ifname(net, ifname);
	} else if ((info->attrs[L2TP_ATTR_SESSION_ID]) &&
		   (info->attrs[L2TP_ATTR_CONN_ID])) {
		tunnel_id = nla_get_u32(info->attrs[L2TP_ATTR_CONN_ID]);
		session_id = nla_get_u32(info->attrs[L2TP_ATTR_SESSION_ID]);
		tunnel = l2tp_tunnel_find(net, tunnel_id);
		if (tunnel)
			session = l2tp_session_find(net, tunnel, session_id);
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
		ret = genlmsg_multicast_allns(family, msg, 0, 0, GFP_ATOMIC);
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
		ret = genlmsg_multicast_allns(family, msg, 0, 0, GFP_ATOMIC);
		/* We don't care if no one is listening */
		if (ret == -ESRCH)
			ret = 0;
		return ret;
	}

	nlmsg_free(msg);

	return ret;
}

static int l2tp_nl_cmd_tunnel_create(struct sk_buff *skb, struct genl_info *info)
{
	u32 tunnel_id;
	u32 peer_tunnel_id;
	int proto_version;
	int fd;
	int ret = 0;
	struct l2tp_tunnel_cfg cfg = { 0, };
	struct l2tp_tunnel *tunnel;
	struct net *net = genl_info_net(info);

	if (!info->attrs[L2TP_ATTR_CONN_ID]) {
		ret = -EINVAL;
		goto out;
	}
	tunnel_id = nla_get_u32(info->attrs[L2TP_ATTR_CONN_ID]);

	if (!info->attrs[L2TP_ATTR_PEER_CONN_ID]) {
		ret = -EINVAL;
		goto out;
	}
	peer_tunnel_id = nla_get_u32(info->attrs[L2TP_ATTR_PEER_CONN_ID]);

	if (!info->attrs[L2TP_ATTR_PROTO_VERSION]) {
		ret = -EINVAL;
		goto out;
	}
	proto_version = nla_get_u8(info->attrs[L2TP_ATTR_PROTO_VERSION]);

	if (!info->attrs[L2TP_ATTR_ENCAP_TYPE]) {
		ret = -EINVAL;
		goto out;
	}
	cfg.encap = nla_get_u16(info->attrs[L2TP_ATTR_ENCAP_TYPE]);

	fd = -1;
	if (info->attrs[L2TP_ATTR_FD]) {
		fd = nla_get_u32(info->attrs[L2TP_ATTR_FD]);
	} else {
#if IS_ENABLED(CONFIG_IPV6)
		if (info->attrs[L2TP_ATTR_IP6_SADDR] &&
		    info->attrs[L2TP_ATTR_IP6_DADDR]) {
			cfg.local_ip6 = nla_data(
				info->attrs[L2TP_ATTR_IP6_SADDR]);
			cfg.peer_ip6 = nla_data(
				info->attrs[L2TP_ATTR_IP6_DADDR]);
		} else
#endif
		if (info->attrs[L2TP_ATTR_IP_SADDR] &&
		    info->attrs[L2TP_ATTR_IP_DADDR]) {
			cfg.local_ip.s_addr = nla_get_in_addr(
				info->attrs[L2TP_ATTR_IP_SADDR]);
			cfg.peer_ip.s_addr = nla_get_in_addr(
				info->attrs[L2TP_ATTR_IP_DADDR]);
		} else {
			ret = -EINVAL;
			goto out;
		}
		if (info->attrs[L2TP_ATTR_UDP_SPORT])
			cfg.local_udp_port = nla_get_u16(info->attrs[L2TP_ATTR_UDP_SPORT]);
		if (info->attrs[L2TP_ATTR_UDP_DPORT])
			cfg.peer_udp_port = nla_get_u16(info->attrs[L2TP_ATTR_UDP_DPORT]);
		if (info->attrs[L2TP_ATTR_UDP_CSUM])
			cfg.use_udp_checksums = nla_get_flag(info->attrs[L2TP_ATTR_UDP_CSUM]);

#if IS_ENABLED(CONFIG_IPV6)
		if (info->attrs[L2TP_ATTR_UDP_ZERO_CSUM6_TX])
			cfg.udp6_zero_tx_checksums = nla_get_flag(info->attrs[L2TP_ATTR_UDP_ZERO_CSUM6_TX]);
		if (info->attrs[L2TP_ATTR_UDP_ZERO_CSUM6_RX])
			cfg.udp6_zero_rx_checksums = nla_get_flag(info->attrs[L2TP_ATTR_UDP_ZERO_CSUM6_RX]);
#endif
	}

	if (info->attrs[L2TP_ATTR_DEBUG])
		cfg.debug = nla_get_u32(info->attrs[L2TP_ATTR_DEBUG]);

	tunnel = l2tp_tunnel_find(net, tunnel_id);
	if (tunnel != NULL) {
		ret = -EEXIST;
		goto out;
	}

	ret = -EINVAL;
	switch (cfg.encap) {
	case L2TP_ENCAPTYPE_UDP:
	case L2TP_ENCAPTYPE_IP:
		ret = l2tp_tunnel_create(net, fd, proto_version, tunnel_id,
					 peer_tunnel_id, &cfg, &tunnel);
		break;
	}

	if (ret >= 0)
		ret = l2tp_tunnel_notify(&l2tp_nl_family, info,
					 tunnel, L2TP_CMD_TUNNEL_CREATE);
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

	tunnel = l2tp_tunnel_find(net, tunnel_id);
	if (tunnel == NULL) {
		ret = -ENODEV;
		goto out;
	}

	l2tp_tunnel_notify(&l2tp_nl_family, info,
			   tunnel, L2TP_CMD_TUNNEL_DELETE);

	(void) l2tp_tunnel_delete(tunnel);

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

	tunnel = l2tp_tunnel_find(net, tunnel_id);
	if (tunnel == NULL) {
		ret = -ENODEV;
		goto out;
	}

	if (info->attrs[L2TP_ATTR_DEBUG])
		tunnel->debug = nla_get_u32(info->attrs[L2TP_ATTR_DEBUG]);

	ret = l2tp_tunnel_notify(&l2tp_nl_family, info,
				 tunnel, L2TP_CMD_TUNNEL_MODIFY);

out:
	return ret;
}

static int l2tp_nl_tunnel_send(struct sk_buff *skb, u32 portid, u32 seq, int flags,
			       struct l2tp_tunnel *tunnel, u8 cmd)
{
	void *hdr;
	struct nlattr *nest;
	struct sock *sk = NULL;
	struct inet_sock *inet;
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6_pinfo *np = NULL;
#endif

	hdr = genlmsg_put(skb, portid, seq, &l2tp_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u8(skb, L2TP_ATTR_PROTO_VERSION, tunnel->version) ||
	    nla_put_u32(skb, L2TP_ATTR_CONN_ID, tunnel->tunnel_id) ||
	    nla_put_u32(skb, L2TP_ATTR_PEER_CONN_ID, tunnel->peer_tunnel_id) ||
	    nla_put_u32(skb, L2TP_ATTR_DEBUG, tunnel->debug) ||
	    nla_put_u16(skb, L2TP_ATTR_ENCAP_TYPE, tunnel->encap))
		goto nla_put_failure;

	nest = nla_nest_start(skb, L2TP_ATTR_STATS);
	if (nest == NULL)
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
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_OOS_PACKETS,
			      atomic_long_read(&tunnel->stats.rx_oos_packets),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_ERRORS,
			      atomic_long_read(&tunnel->stats.rx_errors),
			      L2TP_ATTR_STATS_PAD))
		goto nla_put_failure;
	nla_nest_end(skb, nest);

	sk = tunnel->sock;
	if (!sk)
		goto out;

#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6)
		np = inet6_sk(sk);
#endif

	inet = inet_sk(sk);

	switch (tunnel->encap) {
	case L2TP_ENCAPTYPE_UDP:
		if (nla_put_u16(skb, L2TP_ATTR_UDP_SPORT, ntohs(inet->inet_sport)) ||
		    nla_put_u16(skb, L2TP_ATTR_UDP_DPORT, ntohs(inet->inet_dport)) ||
		    nla_put_u8(skb, L2TP_ATTR_UDP_CSUM, !sk->sk_no_check_tx))
			goto nla_put_failure;
		/* NOBREAK */
	case L2TP_ENCAPTYPE_IP:
#if IS_ENABLED(CONFIG_IPV6)
		if (np) {
			if (nla_put_in6_addr(skb, L2TP_ATTR_IP6_SADDR,
					     &np->saddr) ||
			    nla_put_in6_addr(skb, L2TP_ATTR_IP6_DADDR,
					     &sk->sk_v6_daddr))
				goto nla_put_failure;
		} else
#endif
		if (nla_put_in_addr(skb, L2TP_ATTR_IP_SADDR,
				    inet->inet_saddr) ||
		    nla_put_in_addr(skb, L2TP_ATTR_IP_DADDR,
				    inet->inet_daddr))
			goto nla_put_failure;
		break;
	}

out:
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
		goto out;
	}

	tunnel_id = nla_get_u32(info->attrs[L2TP_ATTR_CONN_ID]);

	tunnel = l2tp_tunnel_find(net, tunnel_id);
	if (tunnel == NULL) {
		ret = -ENODEV;
		goto out;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out;
	}

	ret = l2tp_nl_tunnel_send(msg, info->snd_portid, info->snd_seq,
				  NLM_F_ACK, tunnel, L2TP_CMD_TUNNEL_GET);
	if (ret < 0)
		goto err_out;

	return genlmsg_unicast(net, msg, info->snd_portid);

err_out:
	nlmsg_free(msg);

out:
	return ret;
}

static int l2tp_nl_cmd_tunnel_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int ti = cb->args[0];
	struct l2tp_tunnel *tunnel;
	struct net *net = sock_net(skb->sk);

	for (;;) {
		tunnel = l2tp_tunnel_find_nth(net, ti);
		if (tunnel == NULL)
			goto out;

		if (l2tp_nl_tunnel_send(skb, NETLINK_CB(cb->skb).portid,
					cb->nlh->nlmsg_seq, NLM_F_MULTI,
					tunnel, L2TP_CMD_TUNNEL_GET) < 0)
			goto out;

		ti++;
	}

out:
	cb->args[0] = ti;

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
	tunnel = l2tp_tunnel_find(net, tunnel_id);
	if (!tunnel) {
		ret = -ENODEV;
		goto out;
	}

	if (!info->attrs[L2TP_ATTR_SESSION_ID]) {
		ret = -EINVAL;
		goto out;
	}
	session_id = nla_get_u32(info->attrs[L2TP_ATTR_SESSION_ID]);
	session = l2tp_session_find(net, tunnel, session_id);
	if (session) {
		ret = -EEXIST;
		goto out;
	}

	if (!info->attrs[L2TP_ATTR_PEER_SESSION_ID]) {
		ret = -EINVAL;
		goto out;
	}
	peer_session_id = nla_get_u32(info->attrs[L2TP_ATTR_PEER_SESSION_ID]);

	if (!info->attrs[L2TP_ATTR_PW_TYPE]) {
		ret = -EINVAL;
		goto out;
	}
	cfg.pw_type = nla_get_u16(info->attrs[L2TP_ATTR_PW_TYPE]);
	if (cfg.pw_type >= __L2TP_PWTYPE_MAX) {
		ret = -EINVAL;
		goto out;
	}

	if (tunnel->version > 2) {
		if (info->attrs[L2TP_ATTR_OFFSET])
			cfg.offset = nla_get_u16(info->attrs[L2TP_ATTR_OFFSET]);

		if (info->attrs[L2TP_ATTR_DATA_SEQ])
			cfg.data_seq = nla_get_u8(info->attrs[L2TP_ATTR_DATA_SEQ]);

		cfg.l2specific_type = L2TP_L2SPECTYPE_DEFAULT;
		if (info->attrs[L2TP_ATTR_L2SPEC_TYPE])
			cfg.l2specific_type = nla_get_u8(info->attrs[L2TP_ATTR_L2SPEC_TYPE]);

		cfg.l2specific_len = 4;
		if (info->attrs[L2TP_ATTR_L2SPEC_LEN])
			cfg.l2specific_len = nla_get_u8(info->attrs[L2TP_ATTR_L2SPEC_LEN]);

		if (info->attrs[L2TP_ATTR_COOKIE]) {
			u16 len = nla_len(info->attrs[L2TP_ATTR_COOKIE]);
			if (len > 8) {
				ret = -EINVAL;
				goto out;
			}
			cfg.cookie_len = len;
			memcpy(&cfg.cookie[0], nla_data(info->attrs[L2TP_ATTR_COOKIE]), len);
		}
		if (info->attrs[L2TP_ATTR_PEER_COOKIE]) {
			u16 len = nla_len(info->attrs[L2TP_ATTR_PEER_COOKIE]);
			if (len > 8) {
				ret = -EINVAL;
				goto out;
			}
			cfg.peer_cookie_len = len;
			memcpy(&cfg.peer_cookie[0], nla_data(info->attrs[L2TP_ATTR_PEER_COOKIE]), len);
		}
		if (info->attrs[L2TP_ATTR_IFNAME])
			cfg.ifname = nla_data(info->attrs[L2TP_ATTR_IFNAME]);

		if (info->attrs[L2TP_ATTR_VLAN_ID])
			cfg.vlan_id = nla_get_u16(info->attrs[L2TP_ATTR_VLAN_ID]);
	}

	if (info->attrs[L2TP_ATTR_DEBUG])
		cfg.debug = nla_get_u32(info->attrs[L2TP_ATTR_DEBUG]);

	if (info->attrs[L2TP_ATTR_RECV_SEQ])
		cfg.recv_seq = nla_get_u8(info->attrs[L2TP_ATTR_RECV_SEQ]);

	if (info->attrs[L2TP_ATTR_SEND_SEQ])
		cfg.send_seq = nla_get_u8(info->attrs[L2TP_ATTR_SEND_SEQ]);

	if (info->attrs[L2TP_ATTR_LNS_MODE])
		cfg.lns_mode = nla_get_u8(info->attrs[L2TP_ATTR_LNS_MODE]);

	if (info->attrs[L2TP_ATTR_RECV_TIMEOUT])
		cfg.reorder_timeout = nla_get_msecs(info->attrs[L2TP_ATTR_RECV_TIMEOUT]);

	if (info->attrs[L2TP_ATTR_MTU])
		cfg.mtu = nla_get_u16(info->attrs[L2TP_ATTR_MTU]);

	if (info->attrs[L2TP_ATTR_MRU])
		cfg.mru = nla_get_u16(info->attrs[L2TP_ATTR_MRU]);

#ifdef CONFIG_MODULES
	if (l2tp_nl_cmd_ops[cfg.pw_type] == NULL) {
		genl_unlock();
		request_module("net-l2tp-type-%u", cfg.pw_type);
		genl_lock();
	}
#endif
	if ((l2tp_nl_cmd_ops[cfg.pw_type] == NULL) ||
	    (l2tp_nl_cmd_ops[cfg.pw_type]->session_create == NULL)) {
		ret = -EPROTONOSUPPORT;
		goto out;
	}

	/* Check that pseudowire-specific params are present */
	switch (cfg.pw_type) {
	case L2TP_PWTYPE_NONE:
		break;
	case L2TP_PWTYPE_ETH_VLAN:
		if (!info->attrs[L2TP_ATTR_VLAN_ID]) {
			ret = -EINVAL;
			goto out;
		}
		break;
	case L2TP_PWTYPE_ETH:
		break;
	case L2TP_PWTYPE_PPP:
	case L2TP_PWTYPE_PPP_AC:
		break;
	case L2TP_PWTYPE_IP:
	default:
		ret = -EPROTONOSUPPORT;
		break;
	}

	ret = -EPROTONOSUPPORT;
	if (l2tp_nl_cmd_ops[cfg.pw_type]->session_create)
		ret = (*l2tp_nl_cmd_ops[cfg.pw_type]->session_create)(net, tunnel_id,
			session_id, peer_session_id, &cfg);

	if (ret >= 0) {
		session = l2tp_session_find(net, tunnel, session_id);
		if (session)
			ret = l2tp_session_notify(&l2tp_nl_family, info, session,
						  L2TP_CMD_SESSION_CREATE);
	}

out:
	return ret;
}

static int l2tp_nl_cmd_session_delete(struct sk_buff *skb, struct genl_info *info)
{
	int ret = 0;
	struct l2tp_session *session;
	u16 pw_type;

	session = l2tp_nl_session_find(info);
	if (session == NULL) {
		ret = -ENODEV;
		goto out;
	}

	l2tp_session_notify(&l2tp_nl_family, info,
			    session, L2TP_CMD_SESSION_DELETE);

	pw_type = session->pwtype;
	if (pw_type < __L2TP_PWTYPE_MAX)
		if (l2tp_nl_cmd_ops[pw_type] && l2tp_nl_cmd_ops[pw_type]->session_delete)
			ret = (*l2tp_nl_cmd_ops[pw_type]->session_delete)(session);

out:
	return ret;
}

static int l2tp_nl_cmd_session_modify(struct sk_buff *skb, struct genl_info *info)
{
	int ret = 0;
	struct l2tp_session *session;

	session = l2tp_nl_session_find(info);
	if (session == NULL) {
		ret = -ENODEV;
		goto out;
	}

	if (info->attrs[L2TP_ATTR_DEBUG])
		session->debug = nla_get_u32(info->attrs[L2TP_ATTR_DEBUG]);

	if (info->attrs[L2TP_ATTR_DATA_SEQ])
		session->data_seq = nla_get_u8(info->attrs[L2TP_ATTR_DATA_SEQ]);

	if (info->attrs[L2TP_ATTR_RECV_SEQ])
		session->recv_seq = nla_get_u8(info->attrs[L2TP_ATTR_RECV_SEQ]);

	if (info->attrs[L2TP_ATTR_SEND_SEQ]) {
		session->send_seq = nla_get_u8(info->attrs[L2TP_ATTR_SEND_SEQ]);
		l2tp_session_set_header_len(session, session->tunnel->version);
	}

	if (info->attrs[L2TP_ATTR_LNS_MODE])
		session->lns_mode = nla_get_u8(info->attrs[L2TP_ATTR_LNS_MODE]);

	if (info->attrs[L2TP_ATTR_RECV_TIMEOUT])
		session->reorder_timeout = nla_get_msecs(info->attrs[L2TP_ATTR_RECV_TIMEOUT]);

	if (info->attrs[L2TP_ATTR_MTU])
		session->mtu = nla_get_u16(info->attrs[L2TP_ATTR_MTU]);

	if (info->attrs[L2TP_ATTR_MRU])
		session->mru = nla_get_u16(info->attrs[L2TP_ATTR_MRU]);

	ret = l2tp_session_notify(&l2tp_nl_family, info,
				  session, L2TP_CMD_SESSION_MODIFY);

out:
	return ret;
}

static int l2tp_nl_session_send(struct sk_buff *skb, u32 portid, u32 seq, int flags,
				struct l2tp_session *session, u8 cmd)
{
	void *hdr;
	struct nlattr *nest;
	struct l2tp_tunnel *tunnel = session->tunnel;
	struct sock *sk = NULL;

	sk = tunnel->sock;

	hdr = genlmsg_put(skb, portid, seq, &l2tp_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(skb, L2TP_ATTR_CONN_ID, tunnel->tunnel_id) ||
	    nla_put_u32(skb, L2TP_ATTR_SESSION_ID, session->session_id) ||
	    nla_put_u32(skb, L2TP_ATTR_PEER_CONN_ID, tunnel->peer_tunnel_id) ||
	    nla_put_u32(skb, L2TP_ATTR_PEER_SESSION_ID,
			session->peer_session_id) ||
	    nla_put_u32(skb, L2TP_ATTR_DEBUG, session->debug) ||
	    nla_put_u16(skb, L2TP_ATTR_PW_TYPE, session->pwtype) ||
	    nla_put_u16(skb, L2TP_ATTR_MTU, session->mtu) ||
	    (session->mru &&
	     nla_put_u16(skb, L2TP_ATTR_MRU, session->mru)))
		goto nla_put_failure;

	if ((session->ifname[0] &&
	     nla_put_string(skb, L2TP_ATTR_IFNAME, session->ifname)) ||
	    (session->cookie_len &&
	     nla_put(skb, L2TP_ATTR_COOKIE, session->cookie_len,
		     &session->cookie[0])) ||
	    (session->peer_cookie_len &&
	     nla_put(skb, L2TP_ATTR_PEER_COOKIE, session->peer_cookie_len,
		     &session->peer_cookie[0])) ||
	    nla_put_u8(skb, L2TP_ATTR_RECV_SEQ, session->recv_seq) ||
	    nla_put_u8(skb, L2TP_ATTR_SEND_SEQ, session->send_seq) ||
	    nla_put_u8(skb, L2TP_ATTR_LNS_MODE, session->lns_mode) ||
#ifdef CONFIG_XFRM
	    (((sk) && (sk->sk_policy[0] || sk->sk_policy[1])) &&
	     nla_put_u8(skb, L2TP_ATTR_USING_IPSEC, 1)) ||
#endif
	    (session->reorder_timeout &&
	     nla_put_msecs(skb, L2TP_ATTR_RECV_TIMEOUT,
			   session->reorder_timeout, L2TP_ATTR_PAD)))
		goto nla_put_failure;

	nest = nla_nest_start(skb, L2TP_ATTR_STATS);
	if (nest == NULL)
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
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_OOS_PACKETS,
			      atomic_long_read(&session->stats.rx_oos_packets),
			      L2TP_ATTR_STATS_PAD) ||
	    nla_put_u64_64bit(skb, L2TP_ATTR_RX_ERRORS,
			      atomic_long_read(&session->stats.rx_errors),
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

	session = l2tp_nl_session_find(info);
	if (session == NULL) {
		ret = -ENODEV;
		goto out;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out;
	}

	ret = l2tp_nl_session_send(msg, info->snd_portid, info->snd_seq,
				   0, session, L2TP_CMD_SESSION_GET);
	if (ret < 0)
		goto err_out;

	return genlmsg_unicast(genl_info_net(info), msg, info->snd_portid);

err_out:
	nlmsg_free(msg);

out:
	return ret;
}

static int l2tp_nl_cmd_session_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct l2tp_session *session;
	struct l2tp_tunnel *tunnel = NULL;
	int ti = cb->args[0];
	int si = cb->args[1];

	for (;;) {
		if (tunnel == NULL) {
			tunnel = l2tp_tunnel_find_nth(net, ti);
			if (tunnel == NULL)
				goto out;
		}

		session = l2tp_session_find_nth(tunnel, si);
		if (session == NULL) {
			ti++;
			tunnel = NULL;
			si = 0;
			continue;
		}

		if (l2tp_nl_session_send(skb, NETLINK_CB(cb->skb).portid,
					 cb->nlh->nlmsg_seq, NLM_F_MULTI,
					 session, L2TP_CMD_SESSION_GET) < 0)
			break;

		si++;
	}

out:
	cb->args[0] = ti;
	cb->args[1] = si;

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

static const struct genl_ops l2tp_nl_ops[] = {
	{
		.cmd = L2TP_CMD_NOOP,
		.doit = l2tp_nl_cmd_noop,
		.policy = l2tp_nl_policy,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = L2TP_CMD_TUNNEL_CREATE,
		.doit = l2tp_nl_cmd_tunnel_create,
		.policy = l2tp_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_TUNNEL_DELETE,
		.doit = l2tp_nl_cmd_tunnel_delete,
		.policy = l2tp_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_TUNNEL_MODIFY,
		.doit = l2tp_nl_cmd_tunnel_modify,
		.policy = l2tp_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_TUNNEL_GET,
		.doit = l2tp_nl_cmd_tunnel_get,
		.dumpit = l2tp_nl_cmd_tunnel_dump,
		.policy = l2tp_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_SESSION_CREATE,
		.doit = l2tp_nl_cmd_session_create,
		.policy = l2tp_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_SESSION_DELETE,
		.doit = l2tp_nl_cmd_session_delete,
		.policy = l2tp_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_SESSION_MODIFY,
		.doit = l2tp_nl_cmd_session_modify,
		.policy = l2tp_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = L2TP_CMD_SESSION_GET,
		.doit = l2tp_nl_cmd_session_get,
		.dumpit = l2tp_nl_cmd_session_dump,
		.policy = l2tp_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family l2tp_nl_family = {
	.name		= L2TP_GENL_NAME,
	.version	= L2TP_GENL_VERSION,
	.hdrsize	= 0,
	.maxattr	= L2TP_ATTR_MAX,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.ops		= l2tp_nl_ops,
	.n_ops		= ARRAY_SIZE(l2tp_nl_ops),
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

static int l2tp_nl_init(void)
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
