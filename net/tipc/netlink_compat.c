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
#include "bearer.h"
#include "link.h"
#include "name_table.h"
#include "socket.h"
#include "node.h"
#include "net.h"
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
	int rep_type;
	int rep_size;
	int req_type;
	struct net *net;
	struct sk_buff *rep;
	struct tlv_desc *req;
	struct sock *dst_sk;
};

struct tipc_nl_compat_cmd_dump {
	int (*header)(struct tipc_nl_compat_msg *);
	int (*dumpit)(struct sk_buff *, struct netlink_callback *);
	int (*format)(struct tipc_nl_compat_msg *msg, struct nlattr **attrs);
};

struct tipc_nl_compat_cmd_doit {
	int (*doit)(struct sk_buff *skb, struct genl_info *info);
	int (*transcode)(struct tipc_nl_compat_cmd_doit *cmd,
			 struct sk_buff *skb, struct tipc_nl_compat_msg *msg);
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

static inline int TLV_GET_DATA_LEN(struct tlv_desc *tlv)
{
	return TLV_GET_LEN(tlv) - TLV_SPACE(0);
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

static void tipc_tlv_init(struct sk_buff *skb, u16 type)
{
	struct tlv_desc *tlv = (struct tlv_desc *)skb->data;

	TLV_SET_LEN(tlv, 0);
	TLV_SET_TYPE(tlv, type);
	skb_put(skb, sizeof(struct tlv_desc));
}

static int tipc_tlv_sprintf(struct sk_buff *skb, const char *fmt, ...)
{
	int n;
	u16 len;
	u32 rem;
	char *buf;
	struct tlv_desc *tlv;
	va_list args;

	rem = tipc_skb_tailroom(skb);

	tlv = (struct tlv_desc *)skb->data;
	len = TLV_GET_LEN(tlv);
	buf = TLV_DATA(tlv) + len;

	va_start(args, fmt);
	n = vscnprintf(buf, rem, fmt, args);
	va_end(args);

	TLV_SET_LEN(tlv, n + len);
	skb_put(skb, n);

	return n;
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

static inline bool string_is_valid(char *s, int len)
{
	return memchr(s, '\0', len) ? true : false;
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
	if (__tipc_dump_start(&cb, msg->net)) {
		kfree_skb(buf);
		return -ENOMEM;
	}

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
	tipc_dump_done(&cb);
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

	if (msg->req_type && !TLV_CHECK_TYPE(msg->req, msg->req_type))
		return -EINVAL;

	msg->rep = tipc_tlv_alloc(msg->rep_size);
	if (!msg->rep)
		return -ENOMEM;

	if (msg->rep_type)
		tipc_tlv_init(msg->rep, msg->rep_type);

	if (cmd->header)
		(*cmd->header)(msg);

	arg = nlmsg_new(0, GFP_KERNEL);
	if (!arg) {
		kfree_skb(msg->rep);
		msg->rep = NULL;
		return -ENOMEM;
	}

	err = __tipc_nl_compat_dumpit(cmd, msg, arg);
	if (err) {
		kfree_skb(msg->rep);
		msg->rep = NULL;
	}
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

	attrbuf = kmalloc_array(tipc_genl_family.maxattr + 1,
				sizeof(struct nlattr *),
				GFP_KERNEL);
	if (!attrbuf) {
		err = -ENOMEM;
		goto trans_out;
	}

	doit_buf = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!doit_buf) {
		err = -ENOMEM;
		goto attrbuf_out;
	}

	memset(&info, 0, sizeof(info));
	info.attrs = attrbuf;

	rtnl_lock();
	err = (*cmd->transcode)(cmd, trans_buf, msg);
	if (err)
		goto doit_out;

	err = nla_parse(attrbuf, tipc_genl_family.maxattr,
			(const struct nlattr *)trans_buf->data,
			trans_buf->len, NULL, NULL);
	if (err)
		goto doit_out;

	doit_buf->sk = msg->dst_sk;

	err = (*cmd->doit)(doit_buf, &info);
doit_out:
	rtnl_unlock();

	kfree_skb(doit_buf);
attrbuf_out:
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
	int err;

	if (!attrs[TIPC_NLA_BEARER])
		return -EINVAL;

	err = nla_parse_nested(bearer, TIPC_NLA_BEARER_MAX,
			       attrs[TIPC_NLA_BEARER], NULL, NULL);
	if (err)
		return err;

	return tipc_add_tlv(msg->rep, TIPC_TLV_BEARER_NAME,
			    nla_data(bearer[TIPC_NLA_BEARER_NAME]),
			    nla_len(bearer[TIPC_NLA_BEARER_NAME]));
}

static int tipc_nl_compat_bearer_enable(struct tipc_nl_compat_cmd_doit *cmd,
					struct sk_buff *skb,
					struct tipc_nl_compat_msg *msg)
{
	struct nlattr *prop;
	struct nlattr *bearer;
	struct tipc_bearer_config *b;
	int len;

	b = (struct tipc_bearer_config *)TLV_DATA(msg->req);

	bearer = nla_nest_start(skb, TIPC_NLA_BEARER);
	if (!bearer)
		return -EMSGSIZE;

	len = TLV_GET_DATA_LEN(msg->req);
	len -= offsetof(struct tipc_bearer_config, name);
	if (len <= 0)
		return -EINVAL;

	len = min_t(int, len, TIPC_MAX_BEARER_NAME);
	if (!string_is_valid(b->name, len))
		return -EINVAL;

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

static int tipc_nl_compat_bearer_disable(struct tipc_nl_compat_cmd_doit *cmd,
					 struct sk_buff *skb,
					 struct tipc_nl_compat_msg *msg)
{
	char *name;
	struct nlattr *bearer;
	int len;

	name = (char *)TLV_DATA(msg->req);

	bearer = nla_nest_start(skb, TIPC_NLA_BEARER);
	if (!bearer)
		return -EMSGSIZE;

	len = min_t(int, TLV_GET_DATA_LEN(msg->req), TIPC_MAX_BEARER_NAME);
	if (!string_is_valid(name, len))
		return -EINVAL;

	if (nla_put_string(skb, TIPC_NLA_BEARER_NAME, name))
		return -EMSGSIZE;

	nla_nest_end(skb, bearer);

	return 0;
}

static inline u32 perc(u32 count, u32 total)
{
	return (count * 100 + (total / 2)) / total;
}

static void __fill_bc_link_stat(struct tipc_nl_compat_msg *msg,
				struct nlattr *prop[], struct nlattr *stats[])
{
	tipc_tlv_sprintf(msg->rep, "  Window:%u packets\n",
			 nla_get_u32(prop[TIPC_NLA_PROP_WIN]));

	tipc_tlv_sprintf(msg->rep,
			 "  RX packets:%u fragments:%u/%u bundles:%u/%u\n",
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_INFO]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_FRAGMENTS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_FRAGMENTED]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_BUNDLES]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_BUNDLED]));

	tipc_tlv_sprintf(msg->rep,
			 "  TX packets:%u fragments:%u/%u bundles:%u/%u\n",
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_INFO]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_FRAGMENTS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_FRAGMENTED]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_BUNDLES]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_BUNDLED]));

	tipc_tlv_sprintf(msg->rep, "  RX naks:%u defs:%u dups:%u\n",
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_NACKS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_DEFERRED]),
			 nla_get_u32(stats[TIPC_NLA_STATS_DUPLICATES]));

	tipc_tlv_sprintf(msg->rep, "  TX naks:%u acks:%u dups:%u\n",
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_NACKS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_ACKS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RETRANSMITTED]));

	tipc_tlv_sprintf(msg->rep,
			 "  Congestion link:%u  Send queue max:%u avg:%u",
			 nla_get_u32(stats[TIPC_NLA_STATS_LINK_CONGS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_MAX_QUEUE]),
			 nla_get_u32(stats[TIPC_NLA_STATS_AVG_QUEUE]));
}

static int tipc_nl_compat_link_stat_dump(struct tipc_nl_compat_msg *msg,
					 struct nlattr **attrs)
{
	char *name;
	struct nlattr *link[TIPC_NLA_LINK_MAX + 1];
	struct nlattr *prop[TIPC_NLA_PROP_MAX + 1];
	struct nlattr *stats[TIPC_NLA_STATS_MAX + 1];
	int err;
	int len;

	if (!attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested(link, TIPC_NLA_LINK_MAX, attrs[TIPC_NLA_LINK],
			       NULL, NULL);
	if (err)
		return err;

	if (!link[TIPC_NLA_LINK_PROP])
		return -EINVAL;

	err = nla_parse_nested(prop, TIPC_NLA_PROP_MAX,
			       link[TIPC_NLA_LINK_PROP], NULL, NULL);
	if (err)
		return err;

	if (!link[TIPC_NLA_LINK_STATS])
		return -EINVAL;

	err = nla_parse_nested(stats, TIPC_NLA_STATS_MAX,
			       link[TIPC_NLA_LINK_STATS], NULL, NULL);
	if (err)
		return err;

	name = (char *)TLV_DATA(msg->req);

	len = min_t(int, TLV_GET_DATA_LEN(msg->req), TIPC_MAX_LINK_NAME);
	if (!string_is_valid(name, len))
		return -EINVAL;

	if (strcmp(name, nla_data(link[TIPC_NLA_LINK_NAME])) != 0)
		return 0;

	tipc_tlv_sprintf(msg->rep, "\nLink <%s>\n",
			 nla_data(link[TIPC_NLA_LINK_NAME]));

	if (link[TIPC_NLA_LINK_BROADCAST]) {
		__fill_bc_link_stat(msg, prop, stats);
		return 0;
	}

	if (link[TIPC_NLA_LINK_ACTIVE])
		tipc_tlv_sprintf(msg->rep, "  ACTIVE");
	else if (link[TIPC_NLA_LINK_UP])
		tipc_tlv_sprintf(msg->rep, "  STANDBY");
	else
		tipc_tlv_sprintf(msg->rep, "  DEFUNCT");

	tipc_tlv_sprintf(msg->rep, "  MTU:%u  Priority:%u",
			 nla_get_u32(link[TIPC_NLA_LINK_MTU]),
			 nla_get_u32(prop[TIPC_NLA_PROP_PRIO]));

	tipc_tlv_sprintf(msg->rep, "  Tolerance:%u ms  Window:%u packets\n",
			 nla_get_u32(prop[TIPC_NLA_PROP_TOL]),
			 nla_get_u32(prop[TIPC_NLA_PROP_WIN]));

	tipc_tlv_sprintf(msg->rep,
			 "  RX packets:%u fragments:%u/%u bundles:%u/%u\n",
			 nla_get_u32(link[TIPC_NLA_LINK_RX]) -
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_INFO]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_FRAGMENTS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_FRAGMENTED]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_BUNDLES]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_BUNDLED]));

	tipc_tlv_sprintf(msg->rep,
			 "  TX packets:%u fragments:%u/%u bundles:%u/%u\n",
			 nla_get_u32(link[TIPC_NLA_LINK_TX]) -
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_INFO]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_FRAGMENTS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_FRAGMENTED]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_BUNDLES]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_BUNDLED]));

	tipc_tlv_sprintf(msg->rep,
			 "  TX profile sample:%u packets  average:%u octets\n",
			 nla_get_u32(stats[TIPC_NLA_STATS_MSG_LEN_CNT]),
			 nla_get_u32(stats[TIPC_NLA_STATS_MSG_LEN_TOT]) /
			 nla_get_u32(stats[TIPC_NLA_STATS_MSG_PROF_TOT]));

	tipc_tlv_sprintf(msg->rep,
			 "  0-64:%u%% -256:%u%% -1024:%u%% -4096:%u%% ",
			 perc(nla_get_u32(stats[TIPC_NLA_STATS_MSG_LEN_P0]),
			      nla_get_u32(stats[TIPC_NLA_STATS_MSG_PROF_TOT])),
			 perc(nla_get_u32(stats[TIPC_NLA_STATS_MSG_LEN_P1]),
			      nla_get_u32(stats[TIPC_NLA_STATS_MSG_PROF_TOT])),
			 perc(nla_get_u32(stats[TIPC_NLA_STATS_MSG_LEN_P2]),
			      nla_get_u32(stats[TIPC_NLA_STATS_MSG_PROF_TOT])),
			 perc(nla_get_u32(stats[TIPC_NLA_STATS_MSG_LEN_P3]),
			      nla_get_u32(stats[TIPC_NLA_STATS_MSG_PROF_TOT])));

	tipc_tlv_sprintf(msg->rep, "-16384:%u%% -32768:%u%% -66000:%u%%\n",
			 perc(nla_get_u32(stats[TIPC_NLA_STATS_MSG_LEN_P4]),
			      nla_get_u32(stats[TIPC_NLA_STATS_MSG_PROF_TOT])),
			 perc(nla_get_u32(stats[TIPC_NLA_STATS_MSG_LEN_P5]),
			      nla_get_u32(stats[TIPC_NLA_STATS_MSG_PROF_TOT])),
			 perc(nla_get_u32(stats[TIPC_NLA_STATS_MSG_LEN_P6]),
			      nla_get_u32(stats[TIPC_NLA_STATS_MSG_PROF_TOT])));

	tipc_tlv_sprintf(msg->rep,
			 "  RX states:%u probes:%u naks:%u defs:%u dups:%u\n",
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_STATES]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_PROBES]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_NACKS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RX_DEFERRED]),
			 nla_get_u32(stats[TIPC_NLA_STATS_DUPLICATES]));

	tipc_tlv_sprintf(msg->rep,
			 "  TX states:%u probes:%u naks:%u acks:%u dups:%u\n",
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_STATES]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_PROBES]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_NACKS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_TX_ACKS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_RETRANSMITTED]));

	tipc_tlv_sprintf(msg->rep,
			 "  Congestion link:%u  Send queue max:%u avg:%u",
			 nla_get_u32(stats[TIPC_NLA_STATS_LINK_CONGS]),
			 nla_get_u32(stats[TIPC_NLA_STATS_MAX_QUEUE]),
			 nla_get_u32(stats[TIPC_NLA_STATS_AVG_QUEUE]));

	return 0;
}

static int tipc_nl_compat_link_dump(struct tipc_nl_compat_msg *msg,
				    struct nlattr **attrs)
{
	struct nlattr *link[TIPC_NLA_LINK_MAX + 1];
	struct tipc_link_info link_info;
	int err;

	if (!attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested(link, TIPC_NLA_LINK_MAX, attrs[TIPC_NLA_LINK],
			       NULL, NULL);
	if (err)
		return err;

	link_info.dest = nla_get_flag(link[TIPC_NLA_LINK_DEST]);
	link_info.up = htonl(nla_get_flag(link[TIPC_NLA_LINK_UP]));
	nla_strlcpy(link_info.str, link[TIPC_NLA_LINK_NAME],
		    TIPC_MAX_LINK_NAME);

	return tipc_add_tlv(msg->rep, TIPC_TLV_LINK_INFO,
			    &link_info, sizeof(link_info));
}

static int __tipc_add_link_prop(struct sk_buff *skb,
				struct tipc_nl_compat_msg *msg,
				struct tipc_link_config *lc)
{
	switch (msg->cmd) {
	case TIPC_CMD_SET_LINK_PRI:
		return nla_put_u32(skb, TIPC_NLA_PROP_PRIO, ntohl(lc->value));
	case TIPC_CMD_SET_LINK_TOL:
		return nla_put_u32(skb, TIPC_NLA_PROP_TOL, ntohl(lc->value));
	case TIPC_CMD_SET_LINK_WINDOW:
		return nla_put_u32(skb, TIPC_NLA_PROP_WIN, ntohl(lc->value));
	}

	return -EINVAL;
}

static int tipc_nl_compat_media_set(struct sk_buff *skb,
				    struct tipc_nl_compat_msg *msg)
{
	struct nlattr *prop;
	struct nlattr *media;
	struct tipc_link_config *lc;
	int len;

	lc = (struct tipc_link_config *)TLV_DATA(msg->req);

	media = nla_nest_start(skb, TIPC_NLA_MEDIA);
	if (!media)
		return -EMSGSIZE;

	len = min_t(int, TLV_GET_DATA_LEN(msg->req), TIPC_MAX_MEDIA_NAME);
	if (!string_is_valid(lc->name, len))
		return -EINVAL;

	if (nla_put_string(skb, TIPC_NLA_MEDIA_NAME, lc->name))
		return -EMSGSIZE;

	prop = nla_nest_start(skb, TIPC_NLA_MEDIA_PROP);
	if (!prop)
		return -EMSGSIZE;

	__tipc_add_link_prop(skb, msg, lc);
	nla_nest_end(skb, prop);
	nla_nest_end(skb, media);

	return 0;
}

static int tipc_nl_compat_bearer_set(struct sk_buff *skb,
				     struct tipc_nl_compat_msg *msg)
{
	struct nlattr *prop;
	struct nlattr *bearer;
	struct tipc_link_config *lc;
	int len;

	lc = (struct tipc_link_config *)TLV_DATA(msg->req);

	bearer = nla_nest_start(skb, TIPC_NLA_BEARER);
	if (!bearer)
		return -EMSGSIZE;

	len = min_t(int, TLV_GET_DATA_LEN(msg->req), TIPC_MAX_MEDIA_NAME);
	if (!string_is_valid(lc->name, len))
		return -EINVAL;

	if (nla_put_string(skb, TIPC_NLA_BEARER_NAME, lc->name))
		return -EMSGSIZE;

	prop = nla_nest_start(skb, TIPC_NLA_BEARER_PROP);
	if (!prop)
		return -EMSGSIZE;

	__tipc_add_link_prop(skb, msg, lc);
	nla_nest_end(skb, prop);
	nla_nest_end(skb, bearer);

	return 0;
}

static int __tipc_nl_compat_link_set(struct sk_buff *skb,
				     struct tipc_nl_compat_msg *msg)
{
	struct nlattr *prop;
	struct nlattr *link;
	struct tipc_link_config *lc;

	lc = (struct tipc_link_config *)TLV_DATA(msg->req);

	link = nla_nest_start(skb, TIPC_NLA_LINK);
	if (!link)
		return -EMSGSIZE;

	if (nla_put_string(skb, TIPC_NLA_LINK_NAME, lc->name))
		return -EMSGSIZE;

	prop = nla_nest_start(skb, TIPC_NLA_LINK_PROP);
	if (!prop)
		return -EMSGSIZE;

	__tipc_add_link_prop(skb, msg, lc);
	nla_nest_end(skb, prop);
	nla_nest_end(skb, link);

	return 0;
}

static int tipc_nl_compat_link_set(struct tipc_nl_compat_cmd_doit *cmd,
				   struct sk_buff *skb,
				   struct tipc_nl_compat_msg *msg)
{
	struct tipc_link_config *lc;
	struct tipc_bearer *bearer;
	struct tipc_media *media;
	int len;

	lc = (struct tipc_link_config *)TLV_DATA(msg->req);

	len = min_t(int, TLV_GET_DATA_LEN(msg->req), TIPC_MAX_LINK_NAME);
	if (!string_is_valid(lc->name, len))
		return -EINVAL;

	media = tipc_media_find(lc->name);
	if (media) {
		cmd->doit = &__tipc_nl_media_set;
		return tipc_nl_compat_media_set(skb, msg);
	}

	bearer = tipc_bearer_find(msg->net, lc->name);
	if (bearer) {
		cmd->doit = &__tipc_nl_bearer_set;
		return tipc_nl_compat_bearer_set(skb, msg);
	}

	return __tipc_nl_compat_link_set(skb, msg);
}

static int tipc_nl_compat_link_reset_stats(struct tipc_nl_compat_cmd_doit *cmd,
					   struct sk_buff *skb,
					   struct tipc_nl_compat_msg *msg)
{
	char *name;
	struct nlattr *link;
	int len;

	name = (char *)TLV_DATA(msg->req);

	link = nla_nest_start(skb, TIPC_NLA_LINK);
	if (!link)
		return -EMSGSIZE;

	len = min_t(int, TLV_GET_DATA_LEN(msg->req), TIPC_MAX_LINK_NAME);
	if (!string_is_valid(name, len))
		return -EINVAL;

	if (nla_put_string(skb, TIPC_NLA_LINK_NAME, name))
		return -EMSGSIZE;

	nla_nest_end(skb, link);

	return 0;
}

static int tipc_nl_compat_name_table_dump_header(struct tipc_nl_compat_msg *msg)
{
	int i;
	u32 depth;
	struct tipc_name_table_query *ntq;
	static const char * const header[] = {
		"Type       ",
		"Lower      Upper      ",
		"Port Identity              ",
		"Publication Scope"
	};

	ntq = (struct tipc_name_table_query *)TLV_DATA(msg->req);
	if (TLV_GET_DATA_LEN(msg->req) < sizeof(struct tipc_name_table_query))
		return -EINVAL;

	depth = ntohl(ntq->depth);

	if (depth > 4)
		depth = 4;
	for (i = 0; i < depth; i++)
		tipc_tlv_sprintf(msg->rep, header[i]);
	tipc_tlv_sprintf(msg->rep, "\n");

	return 0;
}

static int tipc_nl_compat_name_table_dump(struct tipc_nl_compat_msg *msg,
					  struct nlattr **attrs)
{
	char port_str[27];
	struct tipc_name_table_query *ntq;
	struct nlattr *nt[TIPC_NLA_NAME_TABLE_MAX + 1];
	struct nlattr *publ[TIPC_NLA_PUBL_MAX + 1];
	u32 node, depth, type, lowbound, upbound;
	static const char * const scope_str[] = {"", " zone", " cluster",
						 " node"};
	int err;

	if (!attrs[TIPC_NLA_NAME_TABLE])
		return -EINVAL;

	err = nla_parse_nested(nt, TIPC_NLA_NAME_TABLE_MAX,
			       attrs[TIPC_NLA_NAME_TABLE], NULL, NULL);
	if (err)
		return err;

	if (!nt[TIPC_NLA_NAME_TABLE_PUBL])
		return -EINVAL;

	err = nla_parse_nested(publ, TIPC_NLA_PUBL_MAX,
			       nt[TIPC_NLA_NAME_TABLE_PUBL], NULL, NULL);
	if (err)
		return err;

	ntq = (struct tipc_name_table_query *)TLV_DATA(msg->req);

	depth = ntohl(ntq->depth);
	type = ntohl(ntq->type);
	lowbound = ntohl(ntq->lowbound);
	upbound = ntohl(ntq->upbound);

	if (!(depth & TIPC_NTQ_ALLTYPES) &&
	    (type != nla_get_u32(publ[TIPC_NLA_PUBL_TYPE])))
		return 0;
	if (lowbound && (lowbound > nla_get_u32(publ[TIPC_NLA_PUBL_UPPER])))
		return 0;
	if (upbound && (upbound < nla_get_u32(publ[TIPC_NLA_PUBL_LOWER])))
		return 0;

	tipc_tlv_sprintf(msg->rep, "%-10u ",
			 nla_get_u32(publ[TIPC_NLA_PUBL_TYPE]));

	if (depth == 1)
		goto out;

	tipc_tlv_sprintf(msg->rep, "%-10u %-10u ",
			 nla_get_u32(publ[TIPC_NLA_PUBL_LOWER]),
			 nla_get_u32(publ[TIPC_NLA_PUBL_UPPER]));

	if (depth == 2)
		goto out;

	node = nla_get_u32(publ[TIPC_NLA_PUBL_NODE]);
	sprintf(port_str, "<%u.%u.%u:%u>", tipc_zone(node), tipc_cluster(node),
		tipc_node(node), nla_get_u32(publ[TIPC_NLA_PUBL_REF]));
	tipc_tlv_sprintf(msg->rep, "%-26s ", port_str);

	if (depth == 3)
		goto out;

	tipc_tlv_sprintf(msg->rep, "%-10u %s",
			 nla_get_u32(publ[TIPC_NLA_PUBL_KEY]),
			 scope_str[nla_get_u32(publ[TIPC_NLA_PUBL_SCOPE])]);
out:
	tipc_tlv_sprintf(msg->rep, "\n");

	return 0;
}

static int __tipc_nl_compat_publ_dump(struct tipc_nl_compat_msg *msg,
				      struct nlattr **attrs)
{
	u32 type, lower, upper;
	struct nlattr *publ[TIPC_NLA_PUBL_MAX + 1];
	int err;

	if (!attrs[TIPC_NLA_PUBL])
		return -EINVAL;

	err = nla_parse_nested(publ, TIPC_NLA_PUBL_MAX, attrs[TIPC_NLA_PUBL],
			       NULL, NULL);
	if (err)
		return err;

	type = nla_get_u32(publ[TIPC_NLA_PUBL_TYPE]);
	lower = nla_get_u32(publ[TIPC_NLA_PUBL_LOWER]);
	upper = nla_get_u32(publ[TIPC_NLA_PUBL_UPPER]);

	if (lower == upper)
		tipc_tlv_sprintf(msg->rep, " {%u,%u}", type, lower);
	else
		tipc_tlv_sprintf(msg->rep, " {%u,%u,%u}", type, lower, upper);

	return 0;
}

static int tipc_nl_compat_publ_dump(struct tipc_nl_compat_msg *msg, u32 sock)
{
	int err;
	void *hdr;
	struct nlattr *nest;
	struct sk_buff *args;
	struct tipc_nl_compat_cmd_dump dump;

	args = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!args)
		return -ENOMEM;

	hdr = genlmsg_put(args, 0, 0, &tipc_genl_family, NLM_F_MULTI,
			  TIPC_NL_PUBL_GET);
	if (!hdr) {
		kfree_skb(args);
		return -EMSGSIZE;
	}

	nest = nla_nest_start(args, TIPC_NLA_SOCK);
	if (!nest) {
		kfree_skb(args);
		return -EMSGSIZE;
	}

	if (nla_put_u32(args, TIPC_NLA_SOCK_REF, sock)) {
		kfree_skb(args);
		return -EMSGSIZE;
	}

	nla_nest_end(args, nest);
	genlmsg_end(args, hdr);

	dump.dumpit = tipc_nl_publ_dump;
	dump.format = __tipc_nl_compat_publ_dump;

	err = __tipc_nl_compat_dumpit(&dump, msg, args);

	kfree_skb(args);

	return err;
}

static int tipc_nl_compat_sk_dump(struct tipc_nl_compat_msg *msg,
				  struct nlattr **attrs)
{
	int err;
	u32 sock_ref;
	struct nlattr *sock[TIPC_NLA_SOCK_MAX + 1];

	if (!attrs[TIPC_NLA_SOCK])
		return -EINVAL;

	err = nla_parse_nested(sock, TIPC_NLA_SOCK_MAX, attrs[TIPC_NLA_SOCK],
			       NULL, NULL);
	if (err)
		return err;

	sock_ref = nla_get_u32(sock[TIPC_NLA_SOCK_REF]);
	tipc_tlv_sprintf(msg->rep, "%u:", sock_ref);

	if (sock[TIPC_NLA_SOCK_CON]) {
		u32 node;
		struct nlattr *con[TIPC_NLA_CON_MAX + 1];

		err = nla_parse_nested(con, TIPC_NLA_CON_MAX,
				       sock[TIPC_NLA_SOCK_CON], NULL, NULL);

		if (err)
			return err;

		node = nla_get_u32(con[TIPC_NLA_CON_NODE]);
		tipc_tlv_sprintf(msg->rep, "  connected to <%u.%u.%u:%u>",
				 tipc_zone(node),
				 tipc_cluster(node),
				 tipc_node(node),
				 nla_get_u32(con[TIPC_NLA_CON_SOCK]));

		if (con[TIPC_NLA_CON_FLAG])
			tipc_tlv_sprintf(msg->rep, " via {%u,%u}\n",
					 nla_get_u32(con[TIPC_NLA_CON_TYPE]),
					 nla_get_u32(con[TIPC_NLA_CON_INST]));
		else
			tipc_tlv_sprintf(msg->rep, "\n");
	} else if (sock[TIPC_NLA_SOCK_HAS_PUBL]) {
		tipc_tlv_sprintf(msg->rep, " bound to");

		err = tipc_nl_compat_publ_dump(msg, sock_ref);
		if (err)
			return err;
	}
	tipc_tlv_sprintf(msg->rep, "\n");

	return 0;
}

static int tipc_nl_compat_media_dump(struct tipc_nl_compat_msg *msg,
				     struct nlattr **attrs)
{
	struct nlattr *media[TIPC_NLA_MEDIA_MAX + 1];
	int err;

	if (!attrs[TIPC_NLA_MEDIA])
		return -EINVAL;

	err = nla_parse_nested(media, TIPC_NLA_MEDIA_MAX,
			       attrs[TIPC_NLA_MEDIA], NULL, NULL);
	if (err)
		return err;

	return tipc_add_tlv(msg->rep, TIPC_TLV_MEDIA_NAME,
			    nla_data(media[TIPC_NLA_MEDIA_NAME]),
			    nla_len(media[TIPC_NLA_MEDIA_NAME]));
}

static int tipc_nl_compat_node_dump(struct tipc_nl_compat_msg *msg,
				    struct nlattr **attrs)
{
	struct tipc_node_info node_info;
	struct nlattr *node[TIPC_NLA_NODE_MAX + 1];
	int err;

	if (!attrs[TIPC_NLA_NODE])
		return -EINVAL;

	err = nla_parse_nested(node, TIPC_NLA_NODE_MAX, attrs[TIPC_NLA_NODE],
			       NULL, NULL);
	if (err)
		return err;

	node_info.addr = htonl(nla_get_u32(node[TIPC_NLA_NODE_ADDR]));
	node_info.up = htonl(nla_get_flag(node[TIPC_NLA_NODE_UP]));

	return tipc_add_tlv(msg->rep, TIPC_TLV_NODE_INFO, &node_info,
			    sizeof(node_info));
}

static int tipc_nl_compat_net_set(struct tipc_nl_compat_cmd_doit *cmd,
				  struct sk_buff *skb,
				  struct tipc_nl_compat_msg *msg)
{
	u32 val;
	struct nlattr *net;

	val = ntohl(*(__be32 *)TLV_DATA(msg->req));

	net = nla_nest_start(skb, TIPC_NLA_NET);
	if (!net)
		return -EMSGSIZE;

	if (msg->cmd == TIPC_CMD_SET_NODE_ADDR) {
		if (nla_put_u32(skb, TIPC_NLA_NET_ADDR, val))
			return -EMSGSIZE;
	} else if (msg->cmd == TIPC_CMD_SET_NETID) {
		if (nla_put_u32(skb, TIPC_NLA_NET_ID, val))
			return -EMSGSIZE;
	}
	nla_nest_end(skb, net);

	return 0;
}

static int tipc_nl_compat_net_dump(struct tipc_nl_compat_msg *msg,
				   struct nlattr **attrs)
{
	__be32 id;
	struct nlattr *net[TIPC_NLA_NET_MAX + 1];
	int err;

	if (!attrs[TIPC_NLA_NET])
		return -EINVAL;

	err = nla_parse_nested(net, TIPC_NLA_NET_MAX, attrs[TIPC_NLA_NET],
			       NULL, NULL);
	if (err)
		return err;

	id = htonl(nla_get_u32(net[TIPC_NLA_NET_ID]));

	return tipc_add_tlv(msg->rep, TIPC_TLV_UNSIGNED, &id, sizeof(id));
}

static int tipc_cmd_show_stats_compat(struct tipc_nl_compat_msg *msg)
{
	msg->rep = tipc_tlv_alloc(ULTRA_STRING_MAX_LEN);
	if (!msg->rep)
		return -ENOMEM;

	tipc_tlv_init(msg->rep, TIPC_TLV_ULTRA_STRING);
	tipc_tlv_sprintf(msg->rep, "TIPC version " TIPC_MOD_VER "\n");

	return 0;
}

static int tipc_nl_compat_handle(struct tipc_nl_compat_msg *msg)
{
	struct tipc_nl_compat_cmd_dump dump;
	struct tipc_nl_compat_cmd_doit doit;

	memset(&dump, 0, sizeof(dump));
	memset(&doit, 0, sizeof(doit));

	switch (msg->cmd) {
	case TIPC_CMD_NOOP:
		msg->rep = tipc_tlv_alloc(0);
		if (!msg->rep)
			return -ENOMEM;
		return 0;
	case TIPC_CMD_GET_BEARER_NAMES:
		msg->rep_size = MAX_BEARERS * TLV_SPACE(TIPC_MAX_BEARER_NAME);
		dump.dumpit = tipc_nl_bearer_dump;
		dump.format = tipc_nl_compat_bearer_dump;
		return tipc_nl_compat_dumpit(&dump, msg);
	case TIPC_CMD_ENABLE_BEARER:
		msg->req_type = TIPC_TLV_BEARER_CONFIG;
		doit.doit = __tipc_nl_bearer_enable;
		doit.transcode = tipc_nl_compat_bearer_enable;
		return tipc_nl_compat_doit(&doit, msg);
	case TIPC_CMD_DISABLE_BEARER:
		msg->req_type = TIPC_TLV_BEARER_NAME;
		doit.doit = __tipc_nl_bearer_disable;
		doit.transcode = tipc_nl_compat_bearer_disable;
		return tipc_nl_compat_doit(&doit, msg);
	case TIPC_CMD_SHOW_LINK_STATS:
		msg->req_type = TIPC_TLV_LINK_NAME;
		msg->rep_size = ULTRA_STRING_MAX_LEN;
		msg->rep_type = TIPC_TLV_ULTRA_STRING;
		dump.dumpit = tipc_nl_node_dump_link;
		dump.format = tipc_nl_compat_link_stat_dump;
		return tipc_nl_compat_dumpit(&dump, msg);
	case TIPC_CMD_GET_LINKS:
		msg->req_type = TIPC_TLV_NET_ADDR;
		msg->rep_size = ULTRA_STRING_MAX_LEN;
		dump.dumpit = tipc_nl_node_dump_link;
		dump.format = tipc_nl_compat_link_dump;
		return tipc_nl_compat_dumpit(&dump, msg);
	case TIPC_CMD_SET_LINK_TOL:
	case TIPC_CMD_SET_LINK_PRI:
	case TIPC_CMD_SET_LINK_WINDOW:
		msg->req_type =  TIPC_TLV_LINK_CONFIG;
		doit.doit = tipc_nl_node_set_link;
		doit.transcode = tipc_nl_compat_link_set;
		return tipc_nl_compat_doit(&doit, msg);
	case TIPC_CMD_RESET_LINK_STATS:
		msg->req_type = TIPC_TLV_LINK_NAME;
		doit.doit = tipc_nl_node_reset_link_stats;
		doit.transcode = tipc_nl_compat_link_reset_stats;
		return tipc_nl_compat_doit(&doit, msg);
	case TIPC_CMD_SHOW_NAME_TABLE:
		msg->req_type = TIPC_TLV_NAME_TBL_QUERY;
		msg->rep_size = ULTRA_STRING_MAX_LEN;
		msg->rep_type = TIPC_TLV_ULTRA_STRING;
		dump.header = tipc_nl_compat_name_table_dump_header;
		dump.dumpit = tipc_nl_name_table_dump;
		dump.format = tipc_nl_compat_name_table_dump;
		return tipc_nl_compat_dumpit(&dump, msg);
	case TIPC_CMD_SHOW_PORTS:
		msg->rep_size = ULTRA_STRING_MAX_LEN;
		msg->rep_type = TIPC_TLV_ULTRA_STRING;
		dump.dumpit = tipc_nl_sk_dump;
		dump.format = tipc_nl_compat_sk_dump;
		return tipc_nl_compat_dumpit(&dump, msg);
	case TIPC_CMD_GET_MEDIA_NAMES:
		msg->rep_size = MAX_MEDIA * TLV_SPACE(TIPC_MAX_MEDIA_NAME);
		dump.dumpit = tipc_nl_media_dump;
		dump.format = tipc_nl_compat_media_dump;
		return tipc_nl_compat_dumpit(&dump, msg);
	case TIPC_CMD_GET_NODES:
		msg->rep_size = ULTRA_STRING_MAX_LEN;
		dump.dumpit = tipc_nl_node_dump;
		dump.format = tipc_nl_compat_node_dump;
		return tipc_nl_compat_dumpit(&dump, msg);
	case TIPC_CMD_SET_NODE_ADDR:
		msg->req_type = TIPC_TLV_NET_ADDR;
		doit.doit = __tipc_nl_net_set;
		doit.transcode = tipc_nl_compat_net_set;
		return tipc_nl_compat_doit(&doit, msg);
	case TIPC_CMD_SET_NETID:
		msg->req_type = TIPC_TLV_UNSIGNED;
		doit.doit = __tipc_nl_net_set;
		doit.transcode = tipc_nl_compat_net_set;
		return tipc_nl_compat_doit(&doit, msg);
	case TIPC_CMD_GET_NETID:
		msg->rep_size = sizeof(u32);
		dump.dumpit = tipc_nl_net_dump;
		dump.format = tipc_nl_compat_net_dump;
		return tipc_nl_compat_dumpit(&dump, msg);
	case TIPC_CMD_SHOW_STATS:
		return tipc_cmd_show_stats_compat(msg);
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

	memset(&msg, 0, sizeof(msg));

	req_nlh = (struct nlmsghdr *)skb->data;
	msg.req = nlmsg_data(req_nlh) + GENL_HDRLEN + TIPC_GENL_HDRLEN;
	msg.cmd = req_userhdr->cmd;
	msg.net = genl_info_net(info);
	msg.dst_sk = skb->sk;

	if ((msg.cmd & 0xC000) && (!netlink_net_capable(skb, CAP_NET_ADMIN))) {
		msg.rep = tipc_get_err_tlv(TIPC_CFG_NOT_NET_ADMIN);
		err = -EACCES;
		goto send;
	}

	len = nlmsg_attrlen(req_nlh, GENL_HDRLEN + TIPC_GENL_HDRLEN);
	if (!len || !TLV_OK(msg.req, len)) {
		msg.rep = tipc_get_err_tlv(TIPC_CFG_NOT_SUPPORTED);
		err = -EOPNOTSUPP;
		goto send;
	}

	err = tipc_nl_compat_handle(&msg);
	if ((err == -EOPNOTSUPP) || (err == -EPERM))
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
	genlmsg_unicast(msg.net, msg.rep, NETLINK_CB(skb).portid);

	return err;
}

static const struct genl_ops tipc_genl_compat_ops[] = {
	{
		.cmd		= TIPC_GENL_CMD,
		.doit		= tipc_nl_compat_recv,
	},
};

static struct genl_family tipc_genl_compat_family __ro_after_init = {
	.name		= TIPC_GENL_NAME,
	.version	= TIPC_GENL_VERSION,
	.hdrsize	= TIPC_GENL_HDRLEN,
	.maxattr	= 0,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.ops		= tipc_genl_compat_ops,
	.n_ops		= ARRAY_SIZE(tipc_genl_compat_ops),
};

int __init tipc_netlink_compat_start(void)
{
	int res;

	res = genl_register_family(&tipc_genl_compat_family);
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
