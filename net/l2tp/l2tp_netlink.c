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


static struct genl_family l2tp_nl_family = {
	.id		= GENL_ID_GENERATE,
	.name		= L2TP_GENL_NAME,
	.version	= L2TP_GENL_VERSION,
	.hdrsize	= 0,
	.maxattr	= L2TP_ATTR_MAX,
};

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

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out;
	}

	hdr = genlmsg_put(msg, info->snd_pid, info->snd_seq,
			  &l2tp_nl_family, 0, L2TP_CMD_NOOP);
	if (IS_ERR(hdr)) {
		ret = PTR_ERR(hdr);
		goto err_out;
	}

	genlmsg_end(msg, hdr);

	return genlmsg_unicast(genl_info_net(info), msg, info->snd_pid);

err_out:
	nlmsg_free(msg);

out:
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
		if (info->attrs[L2TP_ATTR_IP_SADDR])
			cfg.local_ip.s_addr = nla_get_be32(info->attrs[L2TP_ATTR_IP_SADDR]);
		if (info->attrs[L2TP_ATTR_IP_DADDR])
			cfg.peer_ip.s_addr = nla_get_be32(info->attrs[L2TP_ATTR_IP_DADDR]);
		if (info->attrs[L2TP_ATTR_UDP_SPORT])
			cfg.local_udp_port = nla_get_u16(info->attrs[L2TP_ATTR_UDP_SPORT]);
		if (info->attrs[L2TP_ATTR_UDP_DPORT])
			cfg.peer_udp_port = nla_get_u16(info->attrs[L2TP_ATTR_UDP_DPORT]);
		if (info->attrs[L2TP_ATTR_UDP_CSUM])
			cfg.use_udp_checksums = nla_get_flag(info->attrs[L2TP_ATTR_UDP_CSUM]);
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

out:
	return ret;
}

static int l2tp_nl_tunnel_send(struct sk_buff *skb, u32 pid, u32 seq, int flags,
			       struct l2tp_tunnel *tunnel)
{
	void *hdr;
	struct nlattr *nest;
	struct sock *sk = NULL;
	struct inet_sock *inet;

	hdr = genlmsg_put(skb, pid, seq, &l2tp_nl_family, flags,
			  L2TP_CMD_TUNNEL_GET);
	if (IS_ERR(hdr))
		return PTR_ERR(hdr);

	NLA_PUT_U8(skb, L2TP_ATTR_PROTO_VERSION, tunnel->version);
	NLA_PUT_U32(skb, L2TP_ATTR_CONN_ID, tunnel->tunnel_id);
	NLA_PUT_U32(skb, L2TP_ATTR_PEER_CONN_ID, tunnel->peer_tunnel_id);
	NLA_PUT_U32(skb, L2TP_ATTR_DEBUG, tunnel->debug);
	NLA_PUT_U16(skb, L2TP_ATTR_ENCAP_TYPE, tunnel->encap);

	nest = nla_nest_start(skb, L2TP_ATTR_STATS);
	if (nest == NULL)
		goto nla_put_failure;

	NLA_PUT_U64(skb, L2TP_ATTR_TX_PACKETS, tunnel->stats.tx_packets);
	NLA_PUT_U64(skb, L2TP_ATTR_TX_BYTES, tunnel->stats.tx_bytes);
	NLA_PUT_U64(skb, L2TP_ATTR_TX_ERRORS, tunnel->stats.tx_errors);
	NLA_PUT_U64(skb, L2TP_ATTR_RX_PACKETS, tunnel->stats.rx_packets);
	NLA_PUT_U64(skb, L2TP_ATTR_RX_BYTES, tunnel->stats.rx_bytes);
	NLA_PUT_U64(skb, L2TP_ATTR_RX_SEQ_DISCARDS, tunnel->stats.rx_seq_discards);
	NLA_PUT_U64(skb, L2TP_ATTR_RX_OOS_PACKETS, tunnel->stats.rx_oos_packets);
	NLA_PUT_U64(skb, L2TP_ATTR_RX_ERRORS, tunnel->stats.rx_errors);
	nla_nest_end(skb, nest);

	sk = tunnel->sock;
	if (!sk)
		goto out;

	inet = inet_sk(sk);

	switch (tunnel->encap) {
	case L2TP_ENCAPTYPE_UDP:
		NLA_PUT_U16(skb, L2TP_ATTR_UDP_SPORT, ntohs(inet->inet_sport));
		NLA_PUT_U16(skb, L2TP_ATTR_UDP_DPORT, ntohs(inet->inet_dport));
		NLA_PUT_U8(skb, L2TP_ATTR_UDP_CSUM, (sk->sk_no_check != UDP_CSUM_NOXMIT));
		/* NOBREAK */
	case L2TP_ENCAPTYPE_IP:
		NLA_PUT_BE32(skb, L2TP_ATTR_IP_SADDR, inet->inet_saddr);
		NLA_PUT_BE32(skb, L2TP_ATTR_IP_DADDR, inet->inet_daddr);
		break;
	}

out:
	return genlmsg_end(skb, hdr);

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

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out;
	}

	ret = l2tp_nl_tunnel_send(msg, info->snd_pid, info->snd_seq,
				  NLM_F_ACK, tunnel);
	if (ret < 0)
		goto err_out;

	return genlmsg_unicast(net, msg, info->snd_pid);

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

		if (l2tp_nl_tunnel_send(skb, NETLINK_CB(cb->skb).pid,
					cb->nlh->nlmsg_seq, NLM_F_MULTI,
					tunnel) <= 0)
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

	if (info->attrs[L2TP_ATTR_SEND_SEQ])
		session->send_seq = nla_get_u8(info->attrs[L2TP_ATTR_SEND_SEQ]);

	if (info->attrs[L2TP_ATTR_LNS_MODE])
		session->lns_mode = nla_get_u8(info->attrs[L2TP_ATTR_LNS_MODE]);

	if (info->attrs[L2TP_ATTR_RECV_TIMEOUT])
		session->reorder_timeout = nla_get_msecs(info->attrs[L2TP_ATTR_RECV_TIMEOUT]);

	if (info->attrs[L2TP_ATTR_MTU])
		session->mtu = nla_get_u16(info->attrs[L2TP_ATTR_MTU]);

	if (info->attrs[L2TP_ATTR_MRU])
		session->mru = nla_get_u16(info->attrs[L2TP_ATTR_MRU]);

out:
	return ret;
}

static int l2tp_nl_session_send(struct sk_buff *skb, u32 pid, u32 seq, int flags,
				struct l2tp_session *session)
{
	void *hdr;
	struct nlattr *nest;
	struct l2tp_tunnel *tunnel = session->tunnel;
	struct sock *sk = NULL;

	sk = tunnel->sock;

	hdr = genlmsg_put(skb, pid, seq, &l2tp_nl_family, flags, L2TP_CMD_SESSION_GET);
	if (IS_ERR(hdr))
		return PTR_ERR(hdr);

	NLA_PUT_U32(skb, L2TP_ATTR_CONN_ID, tunnel->tunnel_id);
	NLA_PUT_U32(skb, L2TP_ATTR_SESSION_ID, session->session_id);
	NLA_PUT_U32(skb, L2TP_ATTR_PEER_CONN_ID, tunnel->peer_tunnel_id);
	NLA_PUT_U32(skb, L2TP_ATTR_PEER_SESSION_ID, session->peer_session_id);
	NLA_PUT_U32(skb, L2TP_ATTR_DEBUG, session->debug);
	NLA_PUT_U16(skb, L2TP_ATTR_PW_TYPE, session->pwtype);
	NLA_PUT_U16(skb, L2TP_ATTR_MTU, session->mtu);
	if (session->mru)
		NLA_PUT_U16(skb, L2TP_ATTR_MRU, session->mru);

	if (session->ifname && session->ifname[0])
		NLA_PUT_STRING(skb, L2TP_ATTR_IFNAME, session->ifname);
	if (session->cookie_len)
		NLA_PUT(skb, L2TP_ATTR_COOKIE, session->cookie_len, &session->cookie[0]);
	if (session->peer_cookie_len)
		NLA_PUT(skb, L2TP_ATTR_PEER_COOKIE, session->peer_cookie_len, &session->peer_cookie[0]);
	NLA_PUT_U8(skb, L2TP_ATTR_RECV_SEQ, session->recv_seq);
	NLA_PUT_U8(skb, L2TP_ATTR_SEND_SEQ, session->send_seq);
	NLA_PUT_U8(skb, L2TP_ATTR_LNS_MODE, session->lns_mode);
#ifdef CONFIG_XFRM
	if ((sk) && (sk->sk_policy[0] || sk->sk_policy[1]))
		NLA_PUT_U8(skb, L2TP_ATTR_USING_IPSEC, 1);
#endif
	if (session->reorder_timeout)
		NLA_PUT_MSECS(skb, L2TP_ATTR_RECV_TIMEOUT, session->reorder_timeout);

	nest = nla_nest_start(skb, L2TP_ATTR_STATS);
	if (nest == NULL)
		goto nla_put_failure;
	NLA_PUT_U64(skb, L2TP_ATTR_TX_PACKETS, session->stats.tx_packets);
	NLA_PUT_U64(skb, L2TP_ATTR_TX_BYTES, session->stats.tx_bytes);
	NLA_PUT_U64(skb, L2TP_ATTR_TX_ERRORS, session->stats.tx_errors);
	NLA_PUT_U64(skb, L2TP_ATTR_RX_PACKETS, session->stats.rx_packets);
	NLA_PUT_U64(skb, L2TP_ATTR_RX_BYTES, session->stats.rx_bytes);
	NLA_PUT_U64(skb, L2TP_ATTR_RX_SEQ_DISCARDS, session->stats.rx_seq_discards);
	NLA_PUT_U64(skb, L2TP_ATTR_RX_OOS_PACKETS, session->stats.rx_oos_packets);
	NLA_PUT_U64(skb, L2TP_ATTR_RX_ERRORS, session->stats.rx_errors);
	nla_nest_end(skb, nest);

	return genlmsg_end(skb, hdr);

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

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out;
	}

	ret = l2tp_nl_session_send(msg, info->snd_pid, info->snd_seq,
				   0, session);
	if (ret < 0)
		goto err_out;

	return genlmsg_unicast(genl_info_net(info), msg, info->snd_pid);

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

		if (l2tp_nl_session_send(skb, NETLINK_CB(cb->skb).pid,
					 cb->nlh->nlmsg_seq, NLM_F_MULTI,
					 session) <= 0)
			break;

		si++;
	}

out:
	cb->args[0] = ti;
	cb->args[1] = si;

	return skb->len;
}

static struct nla_policy l2tp_nl_policy[L2TP_ATTR_MAX + 1] = {
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

static struct genl_ops l2tp_nl_ops[] = {
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

out:
	genl_unlock();
err:
	return 0;
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
	int err;

	printk(KERN_INFO "L2TP netlink interface\n");
	err = genl_register_family_with_ops(&l2tp_nl_family, l2tp_nl_ops,
					    ARRAY_SIZE(l2tp_nl_ops));

	return err;
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
MODULE_ALIAS("net-pf-" __stringify(PF_NETLINK) "-proto-" \
	     __stringify(NETLINK_GENERIC) "-type-" "l2tp");
