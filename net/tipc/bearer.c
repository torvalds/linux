/*
 * net/tipc/bearer.c: TIPC bearer code
 *
 * Copyright (c) 1996-2006, 2013-2014, Ericsson AB
 * Copyright (c) 2004-2006, 2010-2013, Wind River Systems
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

#include <net/sock.h>
#include "core.h"
#include "bearer.h"
#include "link.h"
#include "discover.h"
#include "bcast.h"
#include "netlink.h"

#define MAX_ADDR_STR 60

static struct tipc_media * const media_info_array[] = {
	&eth_media_info,
#ifdef CONFIG_TIPC_MEDIA_IB
	&ib_media_info,
#endif
#ifdef CONFIG_TIPC_MEDIA_UDP
	&udp_media_info,
#endif
	NULL
};

static void bearer_disable(struct net *net, struct tipc_bearer *b);

/**
 * tipc_media_find - locates specified media object by name
 */
struct tipc_media *tipc_media_find(const char *name)
{
	u32 i;

	for (i = 0; media_info_array[i] != NULL; i++) {
		if (!strcmp(media_info_array[i]->name, name))
			break;
	}
	return media_info_array[i];
}

/**
 * media_find_id - locates specified media object by type identifier
 */
static struct tipc_media *media_find_id(u8 type)
{
	u32 i;

	for (i = 0; media_info_array[i] != NULL; i++) {
		if (media_info_array[i]->type_id == type)
			break;
	}
	return media_info_array[i];
}

/**
 * tipc_media_addr_printf - record media address in print buffer
 */
void tipc_media_addr_printf(char *buf, int len, struct tipc_media_addr *a)
{
	char addr_str[MAX_ADDR_STR];
	struct tipc_media *m;
	int ret;

	m = media_find_id(a->media_id);

	if (m && !m->addr2str(a, addr_str, sizeof(addr_str)))
		ret = scnprintf(buf, len, "%s(%s)", m->name, addr_str);
	else {
		u32 i;

		ret = scnprintf(buf, len, "UNKNOWN(%u)", a->media_id);
		for (i = 0; i < sizeof(a->value); i++)
			ret += scnprintf(buf - ret, len + ret,
					    "-%02x", a->value[i]);
	}
}

/**
 * bearer_name_validate - validate & (optionally) deconstruct bearer name
 * @name: ptr to bearer name string
 * @name_parts: ptr to area for bearer name components (or NULL if not needed)
 *
 * Returns 1 if bearer name is valid, otherwise 0.
 */
static int bearer_name_validate(const char *name,
				struct tipc_bearer_names *name_parts)
{
	char name_copy[TIPC_MAX_BEARER_NAME];
	char *media_name;
	char *if_name;
	u32 media_len;
	u32 if_len;

	/* copy bearer name & ensure length is OK */
	name_copy[TIPC_MAX_BEARER_NAME - 1] = 0;
	/* need above in case non-Posix strncpy() doesn't pad with nulls */
	strncpy(name_copy, name, TIPC_MAX_BEARER_NAME);
	if (name_copy[TIPC_MAX_BEARER_NAME - 1] != 0)
		return 0;

	/* ensure all component parts of bearer name are present */
	media_name = name_copy;
	if_name = strchr(media_name, ':');
	if (if_name == NULL)
		return 0;
	*(if_name++) = 0;
	media_len = if_name - media_name;
	if_len = strlen(if_name) + 1;

	/* validate component parts of bearer name */
	if ((media_len <= 1) || (media_len > TIPC_MAX_MEDIA_NAME) ||
	    (if_len <= 1) || (if_len > TIPC_MAX_IF_NAME))
		return 0;

	/* return bearer name components, if necessary */
	if (name_parts) {
		strcpy(name_parts->media_name, media_name);
		strcpy(name_parts->if_name, if_name);
	}
	return 1;
}

/**
 * tipc_bearer_find - locates bearer object with matching bearer name
 */
struct tipc_bearer *tipc_bearer_find(struct net *net, const char *name)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bearer *b;
	u32 i;

	for (i = 0; i < MAX_BEARERS; i++) {
		b = rtnl_dereference(tn->bearer_list[i]);
		if (b && (!strcmp(b->name, name)))
			return b;
	}
	return NULL;
}

void tipc_bearer_add_dest(struct net *net, u32 bearer_id, u32 dest)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bearer *b;

	rcu_read_lock();
	b = rcu_dereference_rtnl(tn->bearer_list[bearer_id]);
	if (b)
		tipc_disc_add_dest(b->link_req);
	rcu_read_unlock();
}

void tipc_bearer_remove_dest(struct net *net, u32 bearer_id, u32 dest)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bearer *b;

	rcu_read_lock();
	b = rcu_dereference_rtnl(tn->bearer_list[bearer_id]);
	if (b)
		tipc_disc_remove_dest(b->link_req);
	rcu_read_unlock();
}

/**
 * tipc_enable_bearer - enable bearer with the given name
 */
static int tipc_enable_bearer(struct net *net, const char *name,
			      u32 disc_domain, u32 priority,
			      struct nlattr *attr[])
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bearer *b;
	struct tipc_media *m;
	struct tipc_bearer_names b_names;
	struct sk_buff *skb;
	char addr_string[16];
	u32 bearer_id;
	u32 with_this_prio;
	u32 i;
	int res = -EINVAL;

	if (!tn->own_addr) {
		pr_warn("Bearer <%s> rejected, not supported in standalone mode\n",
			name);
		return -ENOPROTOOPT;
	}
	if (!bearer_name_validate(name, &b_names)) {
		pr_warn("Bearer <%s> rejected, illegal name\n", name);
		return -EINVAL;
	}
	if (tipc_addr_domain_valid(disc_domain) &&
	    (disc_domain != tn->own_addr)) {
		if (tipc_in_scope(disc_domain, tn->own_addr)) {
			disc_domain = tn->own_addr & TIPC_CLUSTER_MASK;
			res = 0;   /* accept any node in own cluster */
		} else if (in_own_cluster_exact(net, disc_domain))
			res = 0;   /* accept specified node in own cluster */
	}
	if (res) {
		pr_warn("Bearer <%s> rejected, illegal discovery domain\n",
			name);
		return -EINVAL;
	}
	if ((priority > TIPC_MAX_LINK_PRI) &&
	    (priority != TIPC_MEDIA_LINK_PRI)) {
		pr_warn("Bearer <%s> rejected, illegal priority\n", name);
		return -EINVAL;
	}

	m = tipc_media_find(b_names.media_name);
	if (!m) {
		pr_warn("Bearer <%s> rejected, media <%s> not registered\n",
			name, b_names.media_name);
		return -EINVAL;
	}

	if (priority == TIPC_MEDIA_LINK_PRI)
		priority = m->priority;

restart:
	bearer_id = MAX_BEARERS;
	with_this_prio = 1;
	for (i = MAX_BEARERS; i-- != 0; ) {
		b = rtnl_dereference(tn->bearer_list[i]);
		if (!b) {
			bearer_id = i;
			continue;
		}
		if (!strcmp(name, b->name)) {
			pr_warn("Bearer <%s> rejected, already enabled\n",
				name);
			return -EINVAL;
		}
		if ((b->priority == priority) &&
		    (++with_this_prio > 2)) {
			if (priority-- == 0) {
				pr_warn("Bearer <%s> rejected, duplicate priority\n",
					name);
				return -EINVAL;
			}
			pr_warn("Bearer <%s> priority adjustment required %u->%u\n",
				name, priority + 1, priority);
			goto restart;
		}
	}
	if (bearer_id >= MAX_BEARERS) {
		pr_warn("Bearer <%s> rejected, bearer limit reached (%u)\n",
			name, MAX_BEARERS);
		return -EINVAL;
	}

	b = kzalloc(sizeof(*b), GFP_ATOMIC);
	if (!b)
		return -ENOMEM;

	strcpy(b->name, name);
	b->media = m;
	res = m->enable_media(net, b, attr);
	if (res) {
		pr_warn("Bearer <%s> rejected, enable failure (%d)\n",
			name, -res);
		return -EINVAL;
	}

	b->identity = bearer_id;
	b->tolerance = m->tolerance;
	b->window = m->window;
	b->domain = disc_domain;
	b->net_plane = bearer_id + 'A';
	b->priority = priority;

	res = tipc_disc_create(net, b, &b->bcast_addr, &skb);
	if (res) {
		bearer_disable(net, b);
		pr_warn("Bearer <%s> rejected, discovery object creation failed\n",
			name);
		return -EINVAL;
	}

	rcu_assign_pointer(tn->bearer_list[bearer_id], b);
	if (skb)
		tipc_bearer_xmit_skb(net, bearer_id, skb, &b->bcast_addr);
	pr_info("Enabled bearer <%s>, discovery domain %s, priority %u\n",
		name,
		tipc_addr_string_fill(addr_string, disc_domain), priority);
	return res;
}

/**
 * tipc_reset_bearer - Reset all links established over this bearer
 */
static int tipc_reset_bearer(struct net *net, struct tipc_bearer *b)
{
	pr_info("Resetting bearer <%s>\n", b->name);
	tipc_node_delete_links(net, b->identity);
	tipc_disc_reset(net, b);
	return 0;
}

/* tipc_bearer_reset_all - reset all links on all bearers
 */
void tipc_bearer_reset_all(struct net *net)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_bearer *b;
	int i;

	for (i = 0; i < MAX_BEARERS; i++) {
		b = rcu_dereference_rtnl(tn->bearer_list[i]);
		if (b)
			tipc_reset_bearer(net, b);
	}
}

/**
 * bearer_disable
 *
 * Note: This routine assumes caller holds RTNL lock.
 */
static void bearer_disable(struct net *net, struct tipc_bearer *b)
{
	struct tipc_net *tn = tipc_net(net);
	int bearer_id = b->identity;

	pr_info("Disabling bearer <%s>\n", b->name);
	b->media->disable_media(b);
	tipc_node_delete_links(net, bearer_id);
	RCU_INIT_POINTER(b->media_ptr, NULL);
	if (b->link_req)
		tipc_disc_delete(b->link_req);
	RCU_INIT_POINTER(tn->bearer_list[bearer_id], NULL);
	kfree_rcu(b, rcu);
}

int tipc_enable_l2_media(struct net *net, struct tipc_bearer *b,
			 struct nlattr *attr[])
{
	struct net_device *dev;
	char *driver_name = strchr((const char *)b->name, ':') + 1;

	/* Find device with specified name */
	dev = dev_get_by_name(net, driver_name);
	if (!dev)
		return -ENODEV;

	/* Associate TIPC bearer with L2 bearer */
	rcu_assign_pointer(b->media_ptr, dev);
	memset(&b->bcast_addr, 0, sizeof(b->bcast_addr));
	memcpy(b->bcast_addr.value, dev->broadcast, b->media->hwaddr_len);
	b->bcast_addr.media_id = b->media->type_id;
	b->bcast_addr.broadcast = 1;
	b->mtu = dev->mtu;
	b->media->raw2addr(b, &b->addr, (char *)dev->dev_addr);
	rcu_assign_pointer(dev->tipc_ptr, b);
	return 0;
}

/* tipc_disable_l2_media - detach TIPC bearer from an L2 interface
 *
 * Mark L2 bearer as inactive so that incoming buffers are thrown away
 */
void tipc_disable_l2_media(struct tipc_bearer *b)
{
	struct net_device *dev;

	dev = (struct net_device *)rtnl_dereference(b->media_ptr);
	RCU_INIT_POINTER(dev->tipc_ptr, NULL);
	synchronize_net();
	dev_put(dev);
}

/**
 * tipc_l2_send_msg - send a TIPC packet out over an L2 interface
 * @skb: the packet to be sent
 * @b: the bearer through which the packet is to be sent
 * @dest: peer destination address
 */
int tipc_l2_send_msg(struct net *net, struct sk_buff *skb,
		     struct tipc_bearer *b, struct tipc_media_addr *dest)
{
	struct net_device *dev;
	int delta;
	void *tipc_ptr;

	dev = (struct net_device *)rcu_dereference_rtnl(b->media_ptr);
	if (!dev)
		return 0;

	/* Send RESET message even if bearer is detached from device */
	tipc_ptr = rcu_dereference_rtnl(dev->tipc_ptr);
	if (unlikely(!tipc_ptr && !msg_is_reset(buf_msg(skb))))
		goto drop;

	delta = dev->hard_header_len - skb_headroom(skb);
	if ((delta > 0) &&
	    pskb_expand_head(skb, SKB_DATA_ALIGN(delta), 0, GFP_ATOMIC))
		goto drop;

	skb_reset_network_header(skb);
	skb->dev = dev;
	skb->protocol = htons(ETH_P_TIPC);
	dev_hard_header(skb, dev, ETH_P_TIPC, dest->value,
			dev->dev_addr, skb->len);
	dev_queue_xmit(skb);
	return 0;
drop:
	kfree_skb(skb);
	return 0;
}

int tipc_bearer_mtu(struct net *net, u32 bearer_id)
{
	int mtu = 0;
	struct tipc_bearer *b;

	rcu_read_lock();
	b = rcu_dereference_rtnl(tipc_net(net)->bearer_list[bearer_id]);
	if (b)
		mtu = b->mtu;
	rcu_read_unlock();
	return mtu;
}

/* tipc_bearer_xmit_skb - sends buffer to destination over bearer
 */
void tipc_bearer_xmit_skb(struct net *net, u32 bearer_id,
			  struct sk_buff *skb,
			  struct tipc_media_addr *dest)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_bearer *b;

	rcu_read_lock();
	b = rcu_dereference_rtnl(tn->bearer_list[bearer_id]);
	if (likely(b))
		b->media->send_msg(net, skb, b, dest);
	else
		kfree_skb(skb);
	rcu_read_unlock();
}

/* tipc_bearer_xmit() -send buffer to destination over bearer
 */
void tipc_bearer_xmit(struct net *net, u32 bearer_id,
		      struct sk_buff_head *xmitq,
		      struct tipc_media_addr *dst)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bearer *b;
	struct sk_buff *skb, *tmp;

	if (skb_queue_empty(xmitq))
		return;

	rcu_read_lock();
	b = rcu_dereference_rtnl(tn->bearer_list[bearer_id]);
	if (unlikely(!b))
		__skb_queue_purge(xmitq);
	skb_queue_walk_safe(xmitq, skb, tmp) {
		__skb_dequeue(xmitq);
		b->media->send_msg(net, skb, b, dst);
	}
	rcu_read_unlock();
}

/* tipc_bearer_bc_xmit() - broadcast buffers to all destinations
 */
void tipc_bearer_bc_xmit(struct net *net, u32 bearer_id,
			 struct sk_buff_head *xmitq)
{
	struct tipc_net *tn = tipc_net(net);
	int net_id = tn->net_id;
	struct tipc_bearer *b;
	struct sk_buff *skb, *tmp;
	struct tipc_msg *hdr;

	rcu_read_lock();
	b = rcu_dereference_rtnl(tn->bearer_list[bearer_id]);
	if (unlikely(!b))
		__skb_queue_purge(xmitq);
	skb_queue_walk_safe(xmitq, skb, tmp) {
		hdr = buf_msg(skb);
		msg_set_non_seq(hdr, 1);
		msg_set_mc_netid(hdr, net_id);
		__skb_dequeue(xmitq);
		b->media->send_msg(net, skb, b, &b->bcast_addr);
	}
	rcu_read_unlock();
}

/**
 * tipc_l2_rcv_msg - handle incoming TIPC message from an interface
 * @buf: the received packet
 * @dev: the net device that the packet was received on
 * @pt: the packet_type structure which was used to register this handler
 * @orig_dev: the original receive net device in case the device is a bond
 *
 * Accept only packets explicitly sent to this node, or broadcast packets;
 * ignores packets sent using interface multicast, and traffic sent to other
 * nodes (which can happen if interface is running in promiscuous mode).
 */
static int tipc_l2_rcv_msg(struct sk_buff *skb, struct net_device *dev,
			   struct packet_type *pt, struct net_device *orig_dev)
{
	struct tipc_bearer *b;

	rcu_read_lock();
	b = rcu_dereference_rtnl(dev->tipc_ptr);
	if (likely(b && (skb->pkt_type <= PACKET_BROADCAST))) {
		skb->next = NULL;
		tipc_rcv(dev_net(dev), skb, b);
		rcu_read_unlock();
		return NET_RX_SUCCESS;
	}
	rcu_read_unlock();
	kfree_skb(skb);
	return NET_RX_DROP;
}

/**
 * tipc_l2_device_event - handle device events from network device
 * @nb: the context of the notification
 * @evt: the type of event
 * @ptr: the net device that the event was on
 *
 * This function is called by the Ethernet driver in case of link
 * change event.
 */
static int tipc_l2_device_event(struct notifier_block *nb, unsigned long evt,
				void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);
	struct tipc_net *tn = tipc_net(net);
	struct tipc_bearer *b;
	int i;

	b = rtnl_dereference(dev->tipc_ptr);
	if (!b) {
		for (i = 0; i < MAX_BEARERS; b = NULL, i++) {
			b = rtnl_dereference(tn->bearer_list[i]);
			if (b && (b->media_ptr == dev))
				break;
		}
	}
	if (!b)
		return NOTIFY_DONE;

	b->mtu = dev->mtu;

	switch (evt) {
	case NETDEV_CHANGE:
		if (netif_carrier_ok(dev))
			break;
	case NETDEV_UP:
		rcu_assign_pointer(dev->tipc_ptr, b);
		break;
	case NETDEV_GOING_DOWN:
		RCU_INIT_POINTER(dev->tipc_ptr, NULL);
		synchronize_net();
		tipc_reset_bearer(net, b);
		break;
	case NETDEV_CHANGEMTU:
		tipc_reset_bearer(net, b);
		break;
	case NETDEV_CHANGEADDR:
		b->media->raw2addr(b, &b->addr,
				   (char *)dev->dev_addr);
		tipc_reset_bearer(net, b);
		break;
	case NETDEV_UNREGISTER:
	case NETDEV_CHANGENAME:
		bearer_disable(dev_net(dev), b);
		break;
	}
	return NOTIFY_OK;
}

static struct packet_type tipc_packet_type __read_mostly = {
	.type = htons(ETH_P_TIPC),
	.func = tipc_l2_rcv_msg,
};

static struct notifier_block notifier = {
	.notifier_call  = tipc_l2_device_event,
	.priority	= 0,
};

int tipc_bearer_setup(void)
{
	int err;

	err = register_netdevice_notifier(&notifier);
	if (err)
		return err;
	dev_add_pack(&tipc_packet_type);
	return 0;
}

void tipc_bearer_cleanup(void)
{
	unregister_netdevice_notifier(&notifier);
	dev_remove_pack(&tipc_packet_type);
}

void tipc_bearer_stop(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bearer *b;
	u32 i;

	for (i = 0; i < MAX_BEARERS; i++) {
		b = rtnl_dereference(tn->bearer_list[i]);
		if (b) {
			bearer_disable(net, b);
			tn->bearer_list[i] = NULL;
		}
	}
}

/* Caller should hold rtnl_lock to protect the bearer */
static int __tipc_nl_add_bearer(struct tipc_nl_msg *msg,
				struct tipc_bearer *bearer, int nlflags)
{
	void *hdr;
	struct nlattr *attrs;
	struct nlattr *prop;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  nlflags, TIPC_NL_BEARER_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_BEARER);
	if (!attrs)
		goto msg_full;

	if (nla_put_string(msg->skb, TIPC_NLA_BEARER_NAME, bearer->name))
		goto attr_msg_full;

	prop = nla_nest_start(msg->skb, TIPC_NLA_BEARER_PROP);
	if (!prop)
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_PRIO, bearer->priority))
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_TOL, bearer->tolerance))
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_WIN, bearer->window))
		goto prop_msg_full;

	nla_nest_end(msg->skb, prop);
	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

prop_msg_full:
	nla_nest_cancel(msg->skb, prop);
attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

int tipc_nl_bearer_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int err;
	int i = cb->args[0];
	struct tipc_bearer *bearer;
	struct tipc_nl_msg msg;
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	if (i == MAX_BEARERS)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rtnl_lock();
	for (i = 0; i < MAX_BEARERS; i++) {
		bearer = rtnl_dereference(tn->bearer_list[i]);
		if (!bearer)
			continue;

		err = __tipc_nl_add_bearer(&msg, bearer, NLM_F_MULTI);
		if (err)
			break;
	}
	rtnl_unlock();

	cb->args[0] = i;
	return skb->len;
}

int tipc_nl_bearer_get(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *name;
	struct sk_buff *rep;
	struct tipc_bearer *bearer;
	struct tipc_nl_msg msg;
	struct nlattr *attrs[TIPC_NLA_BEARER_MAX + 1];
	struct net *net = genl_info_net(info);

	if (!info->attrs[TIPC_NLA_BEARER])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_BEARER_MAX,
			       info->attrs[TIPC_NLA_BEARER],
			       tipc_nl_bearer_policy);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_BEARER_NAME])
		return -EINVAL;
	name = nla_data(attrs[TIPC_NLA_BEARER_NAME]);

	rep = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!rep)
		return -ENOMEM;

	msg.skb = rep;
	msg.portid = info->snd_portid;
	msg.seq = info->snd_seq;

	rtnl_lock();
	bearer = tipc_bearer_find(net, name);
	if (!bearer) {
		err = -EINVAL;
		goto err_out;
	}

	err = __tipc_nl_add_bearer(&msg, bearer, 0);
	if (err)
		goto err_out;
	rtnl_unlock();

	return genlmsg_reply(rep, info);
err_out:
	rtnl_unlock();
	nlmsg_free(rep);

	return err;
}

int tipc_nl_bearer_disable(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *name;
	struct tipc_bearer *bearer;
	struct nlattr *attrs[TIPC_NLA_BEARER_MAX + 1];
	struct net *net = sock_net(skb->sk);

	if (!info->attrs[TIPC_NLA_BEARER])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_BEARER_MAX,
			       info->attrs[TIPC_NLA_BEARER],
			       tipc_nl_bearer_policy);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_BEARER_NAME])
		return -EINVAL;

	name = nla_data(attrs[TIPC_NLA_BEARER_NAME]);

	rtnl_lock();
	bearer = tipc_bearer_find(net, name);
	if (!bearer) {
		rtnl_unlock();
		return -EINVAL;
	}

	bearer_disable(net, bearer);
	rtnl_unlock();

	return 0;
}

int tipc_nl_bearer_enable(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *bearer;
	struct nlattr *attrs[TIPC_NLA_BEARER_MAX + 1];
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	u32 domain;
	u32 prio;

	prio = TIPC_MEDIA_LINK_PRI;
	domain = tn->own_addr & TIPC_CLUSTER_MASK;

	if (!info->attrs[TIPC_NLA_BEARER])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_BEARER_MAX,
			       info->attrs[TIPC_NLA_BEARER],
			       tipc_nl_bearer_policy);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_BEARER_NAME])
		return -EINVAL;

	bearer = nla_data(attrs[TIPC_NLA_BEARER_NAME]);

	if (attrs[TIPC_NLA_BEARER_DOMAIN])
		domain = nla_get_u32(attrs[TIPC_NLA_BEARER_DOMAIN]);

	if (attrs[TIPC_NLA_BEARER_PROP]) {
		struct nlattr *props[TIPC_NLA_PROP_MAX + 1];

		err = tipc_nl_parse_link_prop(attrs[TIPC_NLA_BEARER_PROP],
					      props);
		if (err)
			return err;

		if (props[TIPC_NLA_PROP_PRIO])
			prio = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
	}

	rtnl_lock();
	err = tipc_enable_bearer(net, bearer, domain, prio, attrs);
	if (err) {
		rtnl_unlock();
		return err;
	}
	rtnl_unlock();

	return 0;
}

int tipc_nl_bearer_set(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *name;
	struct tipc_bearer *b;
	struct nlattr *attrs[TIPC_NLA_BEARER_MAX + 1];
	struct net *net = sock_net(skb->sk);

	if (!info->attrs[TIPC_NLA_BEARER])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_BEARER_MAX,
			       info->attrs[TIPC_NLA_BEARER],
			       tipc_nl_bearer_policy);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_BEARER_NAME])
		return -EINVAL;
	name = nla_data(attrs[TIPC_NLA_BEARER_NAME]);

	rtnl_lock();
	b = tipc_bearer_find(net, name);
	if (!b) {
		rtnl_unlock();
		return -EINVAL;
	}

	if (attrs[TIPC_NLA_BEARER_PROP]) {
		struct nlattr *props[TIPC_NLA_PROP_MAX + 1];

		err = tipc_nl_parse_link_prop(attrs[TIPC_NLA_BEARER_PROP],
					      props);
		if (err) {
			rtnl_unlock();
			return err;
		}

		if (props[TIPC_NLA_PROP_TOL])
			b->tolerance = nla_get_u32(props[TIPC_NLA_PROP_TOL]);
		if (props[TIPC_NLA_PROP_PRIO])
			b->priority = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
		if (props[TIPC_NLA_PROP_WIN])
			b->window = nla_get_u32(props[TIPC_NLA_PROP_WIN]);
	}
	rtnl_unlock();

	return 0;
}

static int __tipc_nl_add_media(struct tipc_nl_msg *msg,
			       struct tipc_media *media, int nlflags)
{
	void *hdr;
	struct nlattr *attrs;
	struct nlattr *prop;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  nlflags, TIPC_NL_MEDIA_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_MEDIA);
	if (!attrs)
		goto msg_full;

	if (nla_put_string(msg->skb, TIPC_NLA_MEDIA_NAME, media->name))
		goto attr_msg_full;

	prop = nla_nest_start(msg->skb, TIPC_NLA_MEDIA_PROP);
	if (!prop)
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_PRIO, media->priority))
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_TOL, media->tolerance))
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_WIN, media->window))
		goto prop_msg_full;

	nla_nest_end(msg->skb, prop);
	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

prop_msg_full:
	nla_nest_cancel(msg->skb, prop);
attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

int tipc_nl_media_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int err;
	int i = cb->args[0];
	struct tipc_nl_msg msg;

	if (i == MAX_MEDIA)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rtnl_lock();
	for (; media_info_array[i] != NULL; i++) {
		err = __tipc_nl_add_media(&msg, media_info_array[i],
					  NLM_F_MULTI);
		if (err)
			break;
	}
	rtnl_unlock();

	cb->args[0] = i;
	return skb->len;
}

int tipc_nl_media_get(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *name;
	struct tipc_nl_msg msg;
	struct tipc_media *media;
	struct sk_buff *rep;
	struct nlattr *attrs[TIPC_NLA_BEARER_MAX + 1];

	if (!info->attrs[TIPC_NLA_MEDIA])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_MEDIA_MAX,
			       info->attrs[TIPC_NLA_MEDIA],
			       tipc_nl_media_policy);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_MEDIA_NAME])
		return -EINVAL;
	name = nla_data(attrs[TIPC_NLA_MEDIA_NAME]);

	rep = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!rep)
		return -ENOMEM;

	msg.skb = rep;
	msg.portid = info->snd_portid;
	msg.seq = info->snd_seq;

	rtnl_lock();
	media = tipc_media_find(name);
	if (!media) {
		err = -EINVAL;
		goto err_out;
	}

	err = __tipc_nl_add_media(&msg, media, 0);
	if (err)
		goto err_out;
	rtnl_unlock();

	return genlmsg_reply(rep, info);
err_out:
	rtnl_unlock();
	nlmsg_free(rep);

	return err;
}

int tipc_nl_media_set(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *name;
	struct tipc_media *m;
	struct nlattr *attrs[TIPC_NLA_BEARER_MAX + 1];

	if (!info->attrs[TIPC_NLA_MEDIA])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_MEDIA_MAX,
			       info->attrs[TIPC_NLA_MEDIA],
			       tipc_nl_media_policy);

	if (!attrs[TIPC_NLA_MEDIA_NAME])
		return -EINVAL;
	name = nla_data(attrs[TIPC_NLA_MEDIA_NAME]);

	rtnl_lock();
	m = tipc_media_find(name);
	if (!m) {
		rtnl_unlock();
		return -EINVAL;
	}

	if (attrs[TIPC_NLA_MEDIA_PROP]) {
		struct nlattr *props[TIPC_NLA_PROP_MAX + 1];

		err = tipc_nl_parse_link_prop(attrs[TIPC_NLA_MEDIA_PROP],
					      props);
		if (err) {
			rtnl_unlock();
			return err;
		}

		if (props[TIPC_NLA_PROP_TOL])
			m->tolerance = nla_get_u32(props[TIPC_NLA_PROP_TOL]);
		if (props[TIPC_NLA_PROP_PRIO])
			m->priority = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
		if (props[TIPC_NLA_PROP_WIN])
			m->window = nla_get_u32(props[TIPC_NLA_PROP_WIN]);
	}
	rtnl_unlock();

	return 0;
}
