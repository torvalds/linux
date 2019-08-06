/*
 * net/tipc/bearer.c: TIPC bearer code
 *
 * Copyright (c) 1996-2006, 2013-2016, Ericsson AB
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
#include "monitor.h"
#include "bcast.h"
#include "netlink.h"
#include "udp_media.h"
#include "trace.h"

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

static struct tipc_bearer *bearer_get(struct net *net, int bearer_id)
{
	struct tipc_net *tn = tipc_net(net);

	return rcu_dereference_rtnl(tn->bearer_list[bearer_id]);
}

static void bearer_disable(struct net *net, struct tipc_bearer *b);
static int tipc_l2_rcv_msg(struct sk_buff *skb, struct net_device *dev,
			   struct packet_type *pt, struct net_device *orig_dev);

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
int tipc_media_addr_printf(char *buf, int len, struct tipc_media_addr *a)
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
			ret += scnprintf(buf + ret, len - ret,
					    "-%x", a->value[i]);
	}
	return ret;
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

/*     tipc_bearer_get_name - get the bearer name from its id.
 *     @net: network namespace
 *     @name: a pointer to the buffer where the name will be stored.
 *     @bearer_id: the id to get the name from.
 */
int tipc_bearer_get_name(struct net *net, char *name, u32 bearer_id)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_bearer *b;

	if (bearer_id >= MAX_BEARERS)
		return -EINVAL;

	b = rtnl_dereference(tn->bearer_list[bearer_id]);
	if (!b)
		return -EINVAL;

	strcpy(name, b->name);
	return 0;
}

void tipc_bearer_add_dest(struct net *net, u32 bearer_id, u32 dest)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bearer *b;

	rcu_read_lock();
	b = rcu_dereference_rtnl(tn->bearer_list[bearer_id]);
	if (b)
		tipc_disc_add_dest(b->disc);
	rcu_read_unlock();
}

void tipc_bearer_remove_dest(struct net *net, u32 bearer_id, u32 dest)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bearer *b;

	rcu_read_lock();
	b = rcu_dereference_rtnl(tn->bearer_list[bearer_id]);
	if (b)
		tipc_disc_remove_dest(b->disc);
	rcu_read_unlock();
}

/**
 * tipc_enable_bearer - enable bearer with the given name
 */
static int tipc_enable_bearer(struct net *net, const char *name,
			      u32 disc_domain, u32 prio,
			      struct nlattr *attr[])
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_bearer_names b_names;
	int with_this_prio = 1;
	struct tipc_bearer *b;
	struct tipc_media *m;
	struct sk_buff *skb;
	int bearer_id = 0;
	int res = -EINVAL;
	char *errstr = "";

	if (!bearer_name_validate(name, &b_names)) {
		errstr = "illegal name";
		goto rejected;
	}

	if (prio > TIPC_MAX_LINK_PRI && prio != TIPC_MEDIA_LINK_PRI) {
		errstr = "illegal priority";
		goto rejected;
	}

	m = tipc_media_find(b_names.media_name);
	if (!m) {
		errstr = "media not registered";
		goto rejected;
	}

	if (prio == TIPC_MEDIA_LINK_PRI)
		prio = m->priority;

	/* Check new bearer vs existing ones and find free bearer id if any */
	while (bearer_id < MAX_BEARERS) {
		b = rtnl_dereference(tn->bearer_list[bearer_id]);
		if (!b)
			break;
		if (!strcmp(name, b->name)) {
			errstr = "already enabled";
			goto rejected;
		}
		bearer_id++;
		if (b->priority != prio)
			continue;
		if (++with_this_prio <= 2)
			continue;
		pr_warn("Bearer <%s>: already 2 bearers with priority %u\n",
			name, prio);
		if (prio == TIPC_MIN_LINK_PRI) {
			errstr = "cannot adjust to lower";
			goto rejected;
		}
		pr_warn("Bearer <%s>: trying with adjusted priority\n", name);
		prio--;
		bearer_id = 0;
		with_this_prio = 1;
	}

	if (bearer_id >= MAX_BEARERS) {
		errstr = "max 3 bearers permitted";
		goto rejected;
	}

	b = kzalloc(sizeof(*b), GFP_ATOMIC);
	if (!b)
		return -ENOMEM;

	strcpy(b->name, name);
	b->media = m;
	res = m->enable_media(net, b, attr);
	if (res) {
		kfree(b);
		errstr = "failed to enable media";
		goto rejected;
	}

	b->identity = bearer_id;
	b->tolerance = m->tolerance;
	b->window = m->window;
	b->domain = disc_domain;
	b->net_plane = bearer_id + 'A';
	b->priority = prio;
	test_and_set_bit_lock(0, &b->up);

	res = tipc_disc_create(net, b, &b->bcast_addr, &skb);
	if (res) {
		bearer_disable(net, b);
		errstr = "failed to create discoverer";
		goto rejected;
	}

	rcu_assign_pointer(tn->bearer_list[bearer_id], b);
	if (skb)
		tipc_bearer_xmit_skb(net, bearer_id, skb, &b->bcast_addr);

	if (tipc_mon_create(net, bearer_id)) {
		bearer_disable(net, b);
		return -ENOMEM;
	}

	pr_info("Enabled bearer <%s>, priority %u\n", name, prio);

	return res;
rejected:
	pr_warn("Enabling of bearer <%s> rejected, %s\n", name, errstr);
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
	clear_bit_unlock(0, &b->up);
	tipc_node_delete_links(net, bearer_id);
	b->media->disable_media(b);
	RCU_INIT_POINTER(b->media_ptr, NULL);
	if (b->disc)
		tipc_disc_delete(b->disc);
	RCU_INIT_POINTER(tn->bearer_list[bearer_id], NULL);
	kfree_rcu(b, rcu);
	tipc_mon_delete(net, bearer_id);
}

int tipc_enable_l2_media(struct net *net, struct tipc_bearer *b,
			 struct nlattr *attr[])
{
	char *dev_name = strchr((const char *)b->name, ':') + 1;
	int hwaddr_len = b->media->hwaddr_len;
	u8 node_id[NODE_ID_LEN] = {0,};
	struct net_device *dev;

	/* Find device with specified name */
	dev = dev_get_by_name(net, dev_name);
	if (!dev)
		return -ENODEV;
	if (tipc_mtu_bad(dev, 0)) {
		dev_put(dev);
		return -EINVAL;
	}

	/* Autoconfigure own node identity if needed */
	if (!tipc_own_id(net) && hwaddr_len <= NODE_ID_LEN) {
		memcpy(node_id, dev->dev_addr, hwaddr_len);
		tipc_net_init(net, node_id, 0);
	}
	if (!tipc_own_id(net)) {
		dev_put(dev);
		pr_warn("Failed to obtain node identity\n");
		return -EINVAL;
	}

	/* Associate TIPC bearer with L2 bearer */
	rcu_assign_pointer(b->media_ptr, dev);
	b->pt.dev = dev;
	b->pt.type = htons(ETH_P_TIPC);
	b->pt.func = tipc_l2_rcv_msg;
	dev_add_pack(&b->pt);
	memset(&b->bcast_addr, 0, sizeof(b->bcast_addr));
	memcpy(b->bcast_addr.value, dev->broadcast, hwaddr_len);
	b->bcast_addr.media_id = b->media->type_id;
	b->bcast_addr.broadcast = TIPC_BROADCAST_SUPPORT;
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
	dev_remove_pack(&b->pt);
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

	dev = (struct net_device *)rcu_dereference_rtnl(b->media_ptr);
	if (!dev)
		return 0;

	delta = SKB_DATA_ALIGN(dev->hard_header_len - skb_headroom(skb));
	if ((delta > 0) && pskb_expand_head(skb, delta, 0, GFP_ATOMIC)) {
		kfree_skb(skb);
		return 0;
	}
	skb_reset_network_header(skb);
	skb->dev = dev;
	skb->protocol = htons(ETH_P_TIPC);
	dev_hard_header(skb, dev, ETH_P_TIPC, dest->value,
			dev->dev_addr, skb->len);
	dev_queue_xmit(skb);
	return 0;
}

bool tipc_bearer_bcast_support(struct net *net, u32 bearer_id)
{
	bool supp = false;
	struct tipc_bearer *b;

	rcu_read_lock();
	b = bearer_get(net, bearer_id);
	if (b)
		supp = (b->bcast_addr.broadcast == TIPC_BROADCAST_SUPPORT);
	rcu_read_unlock();
	return supp;
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
	struct tipc_msg *hdr = buf_msg(skb);
	struct tipc_bearer *b;

	rcu_read_lock();
	b = bearer_get(net, bearer_id);
	if (likely(b && (test_bit(0, &b->up) || msg_is_reset(hdr))))
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
	struct tipc_bearer *b;
	struct sk_buff *skb, *tmp;

	if (skb_queue_empty(xmitq))
		return;

	rcu_read_lock();
	b = bearer_get(net, bearer_id);
	if (unlikely(!b))
		__skb_queue_purge(xmitq);
	skb_queue_walk_safe(xmitq, skb, tmp) {
		__skb_dequeue(xmitq);
		if (likely(test_bit(0, &b->up) || msg_is_reset(buf_msg(skb))))
			b->media->send_msg(net, skb, b, dst);
		else
			kfree_skb(skb);
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
	b = bearer_get(net, bearer_id);
	if (unlikely(!b || !test_bit(0, &b->up)))
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
	b = rcu_dereference_rtnl(dev->tipc_ptr) ?:
		rcu_dereference_rtnl(orig_dev->tipc_ptr);
	if (likely(b && test_bit(0, &b->up) &&
		   (skb->pkt_type <= PACKET_MULTICAST))) {
		skb_mark_not_on_list(skb);
		tipc_rcv(dev_net(b->pt.dev), skb, b);
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
	struct tipc_bearer *b;

	b = rtnl_dereference(dev->tipc_ptr);
	if (!b)
		return NOTIFY_DONE;

	trace_tipc_l2_device_event(dev, b, evt);
	switch (evt) {
	case NETDEV_CHANGE:
		if (netif_carrier_ok(dev) && netif_oper_up(dev)) {
			test_and_set_bit_lock(0, &b->up);
			break;
		}
		/* fall through */
	case NETDEV_GOING_DOWN:
		clear_bit_unlock(0, &b->up);
		tipc_reset_bearer(net, b);
		break;
	case NETDEV_UP:
		test_and_set_bit_lock(0, &b->up);
		break;
	case NETDEV_CHANGEMTU:
		if (tipc_mtu_bad(dev, 0)) {
			bearer_disable(net, b);
			break;
		}
		b->mtu = dev->mtu;
		tipc_reset_bearer(net, b);
		break;
	case NETDEV_CHANGEADDR:
		b->media->raw2addr(b, &b->addr,
				   (char *)dev->dev_addr);
		tipc_reset_bearer(net, b);
		break;
	case NETDEV_UNREGISTER:
	case NETDEV_CHANGENAME:
		bearer_disable(net, b);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block notifier = {
	.notifier_call  = tipc_l2_device_event,
	.priority	= 0,
};

int tipc_bearer_setup(void)
{
	return register_netdevice_notifier(&notifier);
}

void tipc_bearer_cleanup(void)
{
	unregister_netdevice_notifier(&notifier);
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
	if (bearer->media->type_id == TIPC_MEDIA_TYPE_UDP)
		if (nla_put_u32(msg->skb, TIPC_NLA_PROP_MTU, bearer->mtu))
			goto prop_msg_full;

	nla_nest_end(msg->skb, prop);

#ifdef CONFIG_TIPC_MEDIA_UDP
	if (bearer->media->type_id == TIPC_MEDIA_TYPE_UDP) {
		if (tipc_udp_nl_add_bearer_data(msg, bearer))
			goto attr_msg_full;
	}
#endif

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
			       tipc_nl_bearer_policy, info->extack);
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

int __tipc_nl_bearer_disable(struct sk_buff *skb, struct genl_info *info)
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
			       tipc_nl_bearer_policy, info->extack);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_BEARER_NAME])
		return -EINVAL;

	name = nla_data(attrs[TIPC_NLA_BEARER_NAME]);

	bearer = tipc_bearer_find(net, name);
	if (!bearer)
		return -EINVAL;

	bearer_disable(net, bearer);

	return 0;
}

int tipc_nl_bearer_disable(struct sk_buff *skb, struct genl_info *info)
{
	int err;

	rtnl_lock();
	err = __tipc_nl_bearer_disable(skb, info);
	rtnl_unlock();

	return err;
}

int __tipc_nl_bearer_enable(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *bearer;
	struct nlattr *attrs[TIPC_NLA_BEARER_MAX + 1];
	struct net *net = sock_net(skb->sk);
	u32 domain = 0;
	u32 prio;

	prio = TIPC_MEDIA_LINK_PRI;

	if (!info->attrs[TIPC_NLA_BEARER])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_BEARER_MAX,
			       info->attrs[TIPC_NLA_BEARER],
			       tipc_nl_bearer_policy, info->extack);
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

	return tipc_enable_bearer(net, bearer, domain, prio, attrs);
}

int tipc_nl_bearer_enable(struct sk_buff *skb, struct genl_info *info)
{
	int err;

	rtnl_lock();
	err = __tipc_nl_bearer_enable(skb, info);
	rtnl_unlock();

	return err;
}

int tipc_nl_bearer_add(struct sk_buff *skb, struct genl_info *info)
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
			       tipc_nl_bearer_policy, info->extack);
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

#ifdef CONFIG_TIPC_MEDIA_UDP
	if (attrs[TIPC_NLA_BEARER_UDP_OPTS]) {
		err = tipc_udp_nl_bearer_add(b,
					     attrs[TIPC_NLA_BEARER_UDP_OPTS]);
		if (err) {
			rtnl_unlock();
			return err;
		}
	}
#endif
	rtnl_unlock();

	return 0;
}

int __tipc_nl_bearer_set(struct sk_buff *skb, struct genl_info *info)
{
	struct tipc_bearer *b;
	struct nlattr *attrs[TIPC_NLA_BEARER_MAX + 1];
	struct net *net = sock_net(skb->sk);
	char *name;
	int err;

	if (!info->attrs[TIPC_NLA_BEARER])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_BEARER_MAX,
			       info->attrs[TIPC_NLA_BEARER],
			       tipc_nl_bearer_policy, info->extack);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_BEARER_NAME])
		return -EINVAL;
	name = nla_data(attrs[TIPC_NLA_BEARER_NAME]);

	b = tipc_bearer_find(net, name);
	if (!b)
		return -EINVAL;

	if (attrs[TIPC_NLA_BEARER_PROP]) {
		struct nlattr *props[TIPC_NLA_PROP_MAX + 1];

		err = tipc_nl_parse_link_prop(attrs[TIPC_NLA_BEARER_PROP],
					      props);
		if (err)
			return err;

		if (props[TIPC_NLA_PROP_TOL]) {
			b->tolerance = nla_get_u32(props[TIPC_NLA_PROP_TOL]);
			tipc_node_apply_property(net, b, TIPC_NLA_PROP_TOL);
		}
		if (props[TIPC_NLA_PROP_PRIO])
			b->priority = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
		if (props[TIPC_NLA_PROP_WIN])
			b->window = nla_get_u32(props[TIPC_NLA_PROP_WIN]);
		if (props[TIPC_NLA_PROP_MTU]) {
			if (b->media->type_id != TIPC_MEDIA_TYPE_UDP)
				return -EINVAL;
#ifdef CONFIG_TIPC_MEDIA_UDP
			if (tipc_udp_mtu_bad(nla_get_u32
					     (props[TIPC_NLA_PROP_MTU])))
				return -EINVAL;
			b->mtu = nla_get_u32(props[TIPC_NLA_PROP_MTU]);
			tipc_node_apply_property(net, b, TIPC_NLA_PROP_MTU);
#endif
		}
	}

	return 0;
}

int tipc_nl_bearer_set(struct sk_buff *skb, struct genl_info *info)
{
	int err;

	rtnl_lock();
	err = __tipc_nl_bearer_set(skb, info);
	rtnl_unlock();

	return err;
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
	if (media->type_id == TIPC_MEDIA_TYPE_UDP)
		if (nla_put_u32(msg->skb, TIPC_NLA_PROP_MTU, media->mtu))
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
			       tipc_nl_media_policy, info->extack);
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

int __tipc_nl_media_set(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *name;
	struct tipc_media *m;
	struct nlattr *attrs[TIPC_NLA_BEARER_MAX + 1];

	if (!info->attrs[TIPC_NLA_MEDIA])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_MEDIA_MAX,
			       info->attrs[TIPC_NLA_MEDIA],
			       tipc_nl_media_policy, info->extack);

	if (!attrs[TIPC_NLA_MEDIA_NAME])
		return -EINVAL;
	name = nla_data(attrs[TIPC_NLA_MEDIA_NAME]);

	m = tipc_media_find(name);
	if (!m)
		return -EINVAL;

	if (attrs[TIPC_NLA_MEDIA_PROP]) {
		struct nlattr *props[TIPC_NLA_PROP_MAX + 1];

		err = tipc_nl_parse_link_prop(attrs[TIPC_NLA_MEDIA_PROP],
					      props);
		if (err)
			return err;

		if (props[TIPC_NLA_PROP_TOL])
			m->tolerance = nla_get_u32(props[TIPC_NLA_PROP_TOL]);
		if (props[TIPC_NLA_PROP_PRIO])
			m->priority = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
		if (props[TIPC_NLA_PROP_WIN])
			m->window = nla_get_u32(props[TIPC_NLA_PROP_WIN]);
		if (props[TIPC_NLA_PROP_MTU]) {
			if (m->type_id != TIPC_MEDIA_TYPE_UDP)
				return -EINVAL;
#ifdef CONFIG_TIPC_MEDIA_UDP
			if (tipc_udp_mtu_bad(nla_get_u32
					     (props[TIPC_NLA_PROP_MTU])))
				return -EINVAL;
			m->mtu = nla_get_u32(props[TIPC_NLA_PROP_MTU]);
#endif
		}
	}

	return 0;
}

int tipc_nl_media_set(struct sk_buff *skb, struct genl_info *info)
{
	int err;

	rtnl_lock();
	err = __tipc_nl_media_set(skb, info);
	rtnl_unlock();

	return err;
}
