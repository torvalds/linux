/*
 * Copyright (c) 2014, Ericsson AB
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "config.h"
#include "bearer.h"
#include <net/genetlink.h>
#include <linux/tipc_config.h>

/* The legacy API had an artificial message length limit called
 * ULTRA_STRING_MAX_LEN.
 */
#define ULTRA_STRING_MAX_LEN 32768

#define TIPC_SKB_MAX TLV_SPACE(ULTRA_STRING_MAX_LEN)

#define REPLY_TRUNCATED "<truncated>\n"

struct tipc_nl_compat_msg {
	u16 cmd;
	int rep_size;
	int req_type;
	struct sk_buff *rep;
	struct tlv_desc *req;
	struct sock *dst_sk;
};

struct tipc_nl_compat_cmd_dump {
	int (*dumpit)(struct sk_buff *, struct netlink_callback *);
	int (*format)(struct tipc_nl_compat_msg *msg, struct nlattr **attrs);
};

struct tipc_nl_compat_cmd_doit {
	int (*doit)(struct sk_buff *skb, struct genl_info *info);
	int (*transcode)(struct sk_buff *skb, struct tipc_nl_compat_msg *msg);
};

static int tipc_skb_tailroom(struct sk_buff *skb)
{
	int tailroom;
	int limit;

	tailroom = skb_tailroom(skb);
	limit = TIPC_SKB_MAX - skb->len;

	if (tailroom < limit)
		return tailroom;

	return limit;
}

static int tipc_add_tlv(struct sk_buff *skb, u16 type, void *data, u16 len)
{
	struct tlv_desc *tlv = (struct tlv_desc *)skb_tail_pointer(skb);

	if (tipc_skb_tailroom(skb) < TLV_SPACE(len))
		return -EMSGSIZE;

	skb_put(skb, TLV_SPACE(len));
	tlv->tlv_type = htons(type);
	tlv->tlv_len = htons(TLV_LENGTH(len));
	if (len && data)
		memcpy(TLV_DATA(tlv), data, len);

	return 0;
}

static struct sk_buff *tipc_tlv_alloc(int size)
{
	int hdr_len;
	struct sk_buff *buf;

	size = TLV_SPACE(size);
	hdr_len = nlmsg_total_size(GENL_HDRLEN + TIPC_GENL_HDRLEN);

	buf = alloc_skb(hdr_len + size, GFP_KERNEL);
	if (!buf)
		return NULL;

	skb_reserve(buf, hdr_len);

	return buf;
}

static struct sk_buff *tipc_get_err_tlv(char *str)
{
	int str_len = strlen(str) + 1;
	struct sk_buff *buf;

	buf = tipc_tlv_alloc(TLV_SPACE(str_len));
	if (buf)
		tipc_add_tlv(buf, TIPC_TLV_ERROR_STRING, str, str_len);

	return buf;
}

static int __tipc_nl_compat_dumpit(struct tipc_nl_compat_cmd_dump *cmd,
				   struct tipc_nl_compat_msg *msg,
				   struct sk_buff *arg)
{
	int len = 0;
	int err;
	struct sk_buff *buf;
	struct nlmsghdr *nlmsg;
	struct netlink_callback cb;

	memset(&cb, 0, sizeof(cb));
	cb.nlh = (struct nlmsghdr *)arg->data;
	cb.skb = arg;

	buf = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf->sk = msg->dst_sk;

	do {
		int rem;

		len = (*cmd->dumpit)(buf, &cb);

		nlmsg_for_each_msg(nlmsg, nlmsg_hdr(buf), len, rem) {
			struct nlattr **attrs;

			err = tipc_nlmsg_parse(nlmsg, &attrs);
			if (err)
				goto err_out;

			err = (*cmd->format)(msg, attrs);
			if (err)
				goto err_out;

			if (tipc_skb_tailroom(msg->rep) <= 1) {
				err = -EMSGSIZE;
				goto err_out;
			}
		}

		skb_reset_tail_pointer(buf);
		buf->len = 0;

	} while (len);

	err = 0;

err_out:
	kfree_skb(buf);

	if (err == -EMSGSIZE) {
		/* The legacy API only considered messages filling
		 * "ULTRA_STRING_MAX_LEN" to be truncated.
		 */
		if ((TIPC_SKB_MAX - msg->rep->len) <= 1) {
			char *tail = skb_tail_pointer(msg->rep);

			if (*tail != '\0')
				sprintf(tail - sizeof(REPLY_TRUNCATED) - 1,
					REPLY_TRUNCATED);
		}

		return 0;
	}

	return err;
}

static int tipc_nl_compat_dumpit(struct tipc_nl_compat_cmd_dump *cmd,
				 struct tipc_nl_compat_msg *msg)
{
	int err;
	struct sk_buff *arg;

	msg->rep = tipc_tlv_alloc(msg->rep_size);
	if (!msg->rep)
		return -ENOMEM;

	arg = nlmsg_new(0, GFP_KERNEL);
	if (!arg) {
		kfree_skb(msg->rep);
		return -ENOMEM;
	}

	err = __tipc_nl_compat_dumpit(cmd, msg, arg);
	if (err)
		kfree_skb(msg->rep);

	kfree_skb(arg);

	return err;
}

static int __tipc_nl_compat_doit(struct tipc_nl_compat_cmd_doit *cmd,
				 struct tipc_nl_compat_msg *msg)
{
	int err;
	struct sk_buff *doit_buf;
	struct sk_buff *trans_buf;
	struct nlattr **attrbuf;
	struct genl_info info;

	trans_buf = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!trans_buf)
		return -ENOMEM;

	err = (*cmd->transcode)(trans_buf, msg);
	if (err)
		goto trans_out;

	attrbuf = kmalloc((tipc_genl_family.maxattr + 1) *
			sizeof(struct nlattr *), GFP_KERNEL);
	if (!attrbuf) {
		err = -ENOMEM;
		goto trans_out;
	}

	err = nla_parse(attrbuf, tipc_genl_family.maxattr,
			(const struct nlattr *)trans_buf->data,
			trans_buf->len, NULL);
	if (err)
		goto parse_out;

	doit_buf = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!doit_buf) {
		err = -ENOMEM;
		goto parse_out;
	}

	doit_buf->sk = msg->dst_sk;

	memset(&info, 0, sizeof(info));
	info.attrs = attrbuf;

	err = (*cmd->doit)(doit_buf, &info);

	kfree_skb(doit_buf);
parse_out:
	kfree(attrbuf);
trans_out:
	kfree_skb(trans_buf);

	return err;
}

static int tipc_nl_compat_doit(struct tipc_nl_compat_cmd_doit *cmd,
			       struct tipc_nl_compat_msg *msg)
{
	int err;

	if (msg->req_type && !TLV_CHECK_TYPE(msg->req, msg->req_type))
		return -EINVAL;

	err = __tipc_nl_compat_doit(cmd, msg);
	if (err)
		return err;

	/* The legacy API considered an empty message a success message */
	msg->rep = tipc_tlv_alloc(0);
	if (!msg->rep)
		return -ENOMEM;

	return 0;
}

static int tipc_nl_compat_bearer_dump(struct tipc_nl_compat_msg *msg,
				      struct nlattr **attrs)
{
	struct nlattr *bearer[TIPC_NLA_BEARER_MAX + 1];

	nla_parse_nested(bearer, TIPC_NLA_BEARER_MAX, attrs[TIPC_NLA_BEARER],
			 NULL);

	return tipc_add_tlv(msg->rep, TIPC_TLV_BEARER_NAME,
			    nla_data(bearer[TIPC_NLA_BEARER_NAME]),
			    nla_len(bearer[TIPC_NLA_BEARER_NAME]));
}

static int tipc_nl_compat_bearer_enable(struct sk_buff *skb,
					struct tipc_nl_compat_msg *msg)
{
	struct nlattr *prop;
	struct nlattr *bearer;
	struct tipc_bearer_config *b;

	b = (struct tipc_bearer_config *)TLV_DATA(msg->req);

	bearer = nla_nest_start(skb, TIPC_NLA_BEARER);
	if (!bearer)
		return -EMSGSIZE;

	if (nla_put_string(skb, TIPC_NLA_BEARER_NAME, b->name))
		return -EMSGSIZE;

	if (nla_put_u32(skb, TIPC_NLA_BEARER_DOMAIN, ntohl(b->disc_domain)))
		return -EMSGSIZE;

	if (ntohl(b->priority) <= TIPC_MAX_LINK_PRI) {
		prop = nla_nest_start(skb, TIPC_NLA_BEARER_PROP);
		if (!prop)
			return -EMSGSIZE;
		if (nla_put_u32(skb, TIPC_NLA_PROP_PRIO, ntohl(b->priority)))
			return -EMSGSIZE;
		nla_nest_end(skb, prop);
	}
	nla_nest_end(skb, bearer);

	return 0;
}

static int tipc_nl_compat_bearer_disable(struct sk_buff *skb,
					 struct tipc_nl_compat_msg *msg)
{
	char *name;
	struct nlattr *bearer;

	name = (char *)TLV_DATA(msg->req);

	bearer = nla_nest_start(skb, TIPC_NLA_BEARER);
	if (!bearer)
		return -EMSGSIZE;

	if (nla_put_string(skb, TIPC_NLA_BEARER_NAME, name))
		return -EMSGSIZE;

	nla_nest_end(skb, bearer);

	return 0;
}

static int tipc_nl_compat_handle(struct tipc_nl_compat_msg *msg)
{
	struct tipc_nl_compat_cmd_dump dump;
	struct tipc_nl_compat_cmd_doit doit;

	memset(&dump, 0, sizeof(dump));
	memset(&doit, 0, sizeof(doit));

	switch (msg->cmd) {
	case TIPC_CMD_GET_BEARER_NAMES:
		msg->rep_size = MAX_BEARERS * TLV_SPACE(TIPC_MAX_BEARER_NAME);
		dump.dumpit = tipc_nl_bearer_dump;
		dump.format = tipc_nl_compat_bearer_dump;
		return tipc_nl_compat_dumpit(&dump, msg);
	case TIPC_CMD_ENABLE_BEARER:
		msg->req_type = TIPC_TLV_BEARER_CONFIG;
		doit.doit = tipc_nl_bearer_enable;
		doit.transcode = tipc_nl_compat_bearer_enable;
		return tipc_nl_compat_doit(&doit, msg);
	case TIPC_CMD_DISABLE_BEARER:
		msg->req_type = TIPC_TLV_BEARER_NAME;
		doit.doit = tipc_nl_bearer_disable;
		doit.transcode = tipc_nl_compat_bearer_disable;
		return tipc_nl_compat_doit(&doit, msg);
	}

	return -EOPNOTSUPP;
}

static int tipc_nl_compat_recv(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	int len;
	struct tipc_nl_compat_msg msg;
	struct nlmsghdr *req_nlh;
	struct nlmsghdr *rep_nlh;
	struct tipc_genlmsghdr *req_userhdr = info->userhdr;
	struct net *net = genl_info_net(info);

	memset(&msg, 0, sizeof(msg));

	req_nlh = (struct nlmsghdr *)skb->data;
	msg.req = nlmsg_data(req_nlh) + GENL_HDRLEN + TIPC_GENL_HDRLEN;
	msg.cmd = req_userhdr->cmd;
	msg.dst_sk = info->dst_sk;

	if ((msg.cmd & 0xC000) && (!netlink_net_capable(skb, CAP_NET_ADMIN))) {
		msg.rep = tipc_get_err_tlv(TIPC_CFG_NOT_NET_ADMIN);
		err = -EACCES;
		goto send;
	}

	len = nlmsg_attrlen(req_nlh, GENL_HDRLEN + TIPC_GENL_HDRLEN);
	if (TLV_GET_LEN(msg.req) && !TLV_OK(msg.req, len)) {
		msg.rep = tipc_get_err_tlv(TIPC_CFG_NOT_SUPPORTED);
		err = -EOPNOTSUPP;
		goto send;
	}

	err = tipc_nl_compat_handle(&msg);
	if (err == -EOPNOTSUPP)
		msg.rep = tipc_get_err_tlv(TIPC_CFG_NOT_SUPPORTED);
	else if (err == -EINVAL)
		msg.rep = tipc_get_err_tlv(TIPC_CFG_TLV_ERROR);
send:
	if (!msg.rep)
		return err;

	len = nlmsg_total_size(GENL_HDRLEN + TIPC_GENL_HDRLEN);
	skb_push(msg.rep, len);
	rep_nlh = nlmsg_hdr(msg.rep);
	memcpy(rep_nlh, info->nlhdr, len);
	rep_nlh->nlmsg_len = msg.rep->len;
	genlmsg_unicast(net, msg.rep, NETLINK_CB(skb).portid);

	return err;
}

static int handle_cmd(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct sk_buff *rep_buf;
	struct nlmsghdr *rep_nlh;
	struct nlmsghdr *req_nlh = info->nlhdr;
	struct tipc_genlmsghdr *req_userhdr = info->userhdr;
	int hdr_space = nlmsg_total_size(GENL_HDRLEN + TIPC_GENL_HDRLEN);
	u16 cmd;

	if ((req_userhdr->cmd & 0xC000) &&
	    (!netlink_net_capable(skb, CAP_NET_ADMIN)))
		cmd = TIPC_CMD_NOT_NET_ADMIN;
	else
		cmd = req_userhdr->cmd;

	rep_buf = tipc_cfg_do_cmd(net, req_userhdr->dest, cmd,
				  nlmsg_data(req_nlh) + GENL_HDRLEN +
				  TIPC_GENL_HDRLEN,
				  nlmsg_attrlen(req_nlh, GENL_HDRLEN +
				  TIPC_GENL_HDRLEN), hdr_space);

	if (rep_buf) {
		skb_push(rep_buf, hdr_space);
		rep_nlh = nlmsg_hdr(rep_buf);
		memcpy(rep_nlh, req_nlh, hdr_space);
		rep_nlh->nlmsg_len = rep_buf->len;
		genlmsg_unicast(net, rep_buf, NETLINK_CB(skb).portid);
	}

	return 0;
}

/* Temporary function to keep functionality throughout the patchset
 * without having to mess with the global variables and other trickery
 * of the old API.
 */
static int tipc_nl_compat_tmp_wrap(struct sk_buff *skb, struct genl_info *info)
{
	struct tipc_genlmsghdr *req = info->userhdr;

	switch (req->cmd) {
	case TIPC_CMD_GET_BEARER_NAMES:
	case TIPC_CMD_ENABLE_BEARER:
	case TIPC_CMD_DISABLE_BEARER:
		return tipc_nl_compat_recv(skb, info);
	}

	return handle_cmd(skb, info);
}

static struct genl_family tipc_genl_compat_family = {
	.id		= GENL_ID_GENERATE,
	.name		= TIPC_GENL_NAME,
	.version	= TIPC_GENL_VERSION,
	.hdrsize	= TIPC_GENL_HDRLEN,
	.maxattr	= 0,
	.netnsok	= true,
};

static struct genl_ops tipc_genl_compat_ops[] = {
	{
		.cmd		= TIPC_GENL_CMD,
		.doit		= tipc_nl_compat_tmp_wrap,
	},
};

int tipc_netlink_compat_start(void)
{
	int res;

	res = genl_register_family_with_ops(&tipc_genl_compat_family,
					    tipc_genl_compat_ops);
	if (res) {
		pr_err("Failed to register legacy compat interface\n");
		return res;
	}

	return 0;
}

void tipc_netlink_compat_stop(void)
{
	genl_unregister_family(&tipc_genl_compat_family);
}
