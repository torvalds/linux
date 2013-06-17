/*
 * net/tipc/ib_media.c: Infiniband bearer support for TIPC
 *
 * Copyright (c) 2013 Patrick McHardy <kaber@trash.net>
 *
 * Based on eth_media.c, which carries the following copyright notice:
 *
 * Copyright (c) 2001-2007, Ericsson AB
 * Copyright (c) 2005-2008, 2011, Wind River Systems
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

#include <linux/if_infiniband.h>
#include "core.h"
#include "bearer.h"

#define MAX_IB_BEARERS		MAX_BEARERS

/**
 * struct ib_bearer - Infiniband bearer data structure
 * @bearer: ptr to associated "generic" bearer structure
 * @dev: ptr to associated Infiniband network device
 * @tipc_packet_type: used in binding TIPC to Infiniband driver
 * @cleanup: work item used when disabling bearer
 */

struct ib_bearer {
	struct tipc_bearer *bearer;
	struct net_device *dev;
	struct packet_type tipc_packet_type;
	struct work_struct setup;
	struct work_struct cleanup;
};

static struct tipc_media ib_media_info;
static struct ib_bearer ib_bearers[MAX_IB_BEARERS];
static int ib_started;

/**
 * ib_media_addr_set - initialize Infiniband media address structure
 *
 * Media-dependent "value" field stores MAC address in first 6 bytes
 * and zeroes out the remaining bytes.
 */
static void ib_media_addr_set(const struct tipc_bearer *tb_ptr,
			      struct tipc_media_addr *a, char *mac)
{
	BUILD_BUG_ON(sizeof(a->value) < INFINIBAND_ALEN);
	memcpy(a->value, mac, INFINIBAND_ALEN);
	a->media_id = TIPC_MEDIA_TYPE_IB;
	a->broadcast = !memcmp(mac, tb_ptr->bcast_addr.value, INFINIBAND_ALEN);
}

/**
 * send_msg - send a TIPC message out over an InfiniBand interface
 */
static int send_msg(struct sk_buff *buf, struct tipc_bearer *tb_ptr,
		    struct tipc_media_addr *dest)
{
	struct sk_buff *clone;
	struct net_device *dev;
	int delta;

	clone = skb_clone(buf, GFP_ATOMIC);
	if (!clone)
		return 0;

	dev = ((struct ib_bearer *)(tb_ptr->usr_handle))->dev;
	delta = dev->hard_header_len - skb_headroom(buf);

	if ((delta > 0) &&
	    pskb_expand_head(clone, SKB_DATA_ALIGN(delta), 0, GFP_ATOMIC)) {
		kfree_skb(clone);
		return 0;
	}

	skb_reset_network_header(clone);
	clone->dev = dev;
	clone->protocol = htons(ETH_P_TIPC);
	dev_hard_header(clone, dev, ETH_P_TIPC, dest->value,
			dev->dev_addr, clone->len);
	dev_queue_xmit(clone);
	return 0;
}

/**
 * recv_msg - handle incoming TIPC message from an InfiniBand interface
 *
 * Accept only packets explicitly sent to this node, or broadcast packets;
 * ignores packets sent using InfiniBand multicast, and traffic sent to other
 * nodes (which can happen if interface is running in promiscuous mode).
 */
static int recv_msg(struct sk_buff *buf, struct net_device *dev,
		    struct packet_type *pt, struct net_device *orig_dev)
{
	struct ib_bearer *ib_ptr = (struct ib_bearer *)pt->af_packet_priv;

	if (!net_eq(dev_net(dev), &init_net)) {
		kfree_skb(buf);
		return 0;
	}

	if (likely(ib_ptr->bearer)) {
		if (likely(buf->pkt_type <= PACKET_BROADCAST)) {
			buf->next = NULL;
			tipc_recv_msg(buf, ib_ptr->bearer);
			return 0;
		}
	}
	kfree_skb(buf);
	return 0;
}

/**
 * setup_bearer - setup association between InfiniBand bearer and interface
 */
static void setup_bearer(struct work_struct *work)
{
	struct ib_bearer *ib_ptr =
		container_of(work, struct ib_bearer, setup);

	dev_add_pack(&ib_ptr->tipc_packet_type);
}

/**
 * enable_bearer - attach TIPC bearer to an InfiniBand interface
 */
static int enable_bearer(struct tipc_bearer *tb_ptr)
{
	struct net_device *dev;
	struct ib_bearer *ib_ptr = &ib_bearers[0];
	struct ib_bearer *stop = &ib_bearers[MAX_IB_BEARERS];
	char *driver_name = strchr((const char *)tb_ptr->name, ':') + 1;
	int pending_dev = 0;

	/* Find unused InfiniBand bearer structure */
	while (ib_ptr->dev) {
		if (!ib_ptr->bearer)
			pending_dev++;
		if (++ib_ptr == stop)
			return pending_dev ? -EAGAIN : -EDQUOT;
	}

	/* Find device with specified name */
	dev = dev_get_by_name(&init_net, driver_name);
	if (!dev)
		return -ENODEV;

	/* Create InfiniBand bearer for device */
	ib_ptr->dev = dev;
	ib_ptr->tipc_packet_type.type = htons(ETH_P_TIPC);
	ib_ptr->tipc_packet_type.dev = dev;
	ib_ptr->tipc_packet_type.func = recv_msg;
	ib_ptr->tipc_packet_type.af_packet_priv = ib_ptr;
	INIT_LIST_HEAD(&(ib_ptr->tipc_packet_type.list));
	INIT_WORK(&ib_ptr->setup, setup_bearer);
	schedule_work(&ib_ptr->setup);

	/* Associate TIPC bearer with InfiniBand bearer */
	ib_ptr->bearer = tb_ptr;
	tb_ptr->usr_handle = (void *)ib_ptr;
	memset(tb_ptr->bcast_addr.value, 0, sizeof(tb_ptr->bcast_addr.value));
	memcpy(tb_ptr->bcast_addr.value, dev->broadcast, INFINIBAND_ALEN);
	tb_ptr->bcast_addr.media_id = TIPC_MEDIA_TYPE_IB;
	tb_ptr->bcast_addr.broadcast = 1;
	tb_ptr->mtu = dev->mtu;
	tb_ptr->blocked = 0;
	ib_media_addr_set(tb_ptr, &tb_ptr->addr, (char *)dev->dev_addr);
	return 0;
}

/**
 * cleanup_bearer - break association between InfiniBand bearer and interface
 *
 * This routine must be invoked from a work queue because it can sleep.
 */
static void cleanup_bearer(struct work_struct *work)
{
	struct ib_bearer *ib_ptr =
		container_of(work, struct ib_bearer, cleanup);

	dev_remove_pack(&ib_ptr->tipc_packet_type);
	dev_put(ib_ptr->dev);
	ib_ptr->dev = NULL;
}

/**
 * disable_bearer - detach TIPC bearer from an InfiniBand interface
 *
 * Mark InfiniBand bearer as inactive so that incoming buffers are thrown away,
 * then get worker thread to complete bearer cleanup.  (Can't do cleanup
 * here because cleanup code needs to sleep and caller holds spinlocks.)
 */
static void disable_bearer(struct tipc_bearer *tb_ptr)
{
	struct ib_bearer *ib_ptr = (struct ib_bearer *)tb_ptr->usr_handle;

	ib_ptr->bearer = NULL;
	INIT_WORK(&ib_ptr->cleanup, cleanup_bearer);
	schedule_work(&ib_ptr->cleanup);
}

/**
 * recv_notification - handle device updates from OS
 *
 * Change the state of the InfiniBand bearer (if any) associated with the
 * specified device.
 */
static int recv_notification(struct notifier_block *nb, unsigned long evt,
			     void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct ib_bearer *ib_ptr = &ib_bearers[0];
	struct ib_bearer *stop = &ib_bearers[MAX_IB_BEARERS];

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	while ((ib_ptr->dev != dev)) {
		if (++ib_ptr == stop)
			return NOTIFY_DONE;	/* couldn't find device */
	}
	if (!ib_ptr->bearer)
		return NOTIFY_DONE;		/* bearer had been disabled */

	ib_ptr->bearer->mtu = dev->mtu;

	switch (evt) {
	case NETDEV_CHANGE:
		if (netif_carrier_ok(dev))
			tipc_continue(ib_ptr->bearer);
		else
			tipc_block_bearer(ib_ptr->bearer->name);
		break;
	case NETDEV_UP:
		tipc_continue(ib_ptr->bearer);
		break;
	case NETDEV_DOWN:
		tipc_block_bearer(ib_ptr->bearer->name);
		break;
	case NETDEV_CHANGEMTU:
	case NETDEV_CHANGEADDR:
		tipc_block_bearer(ib_ptr->bearer->name);
		tipc_continue(ib_ptr->bearer);
		break;
	case NETDEV_UNREGISTER:
	case NETDEV_CHANGENAME:
		tipc_disable_bearer(ib_ptr->bearer->name);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block notifier = {
	.notifier_call	= recv_notification,
	.priority	= 0,
};

/**
 * ib_addr2str - convert InfiniBand address to string
 */
static int ib_addr2str(struct tipc_media_addr *a, char *str_buf, int str_size)
{
	if (str_size < 60)	/* 60 = 19 * strlen("xx:") + strlen("xx\0") */
		return 1;

	sprintf(str_buf, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:"
			 "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		a->value[0], a->value[1], a->value[2], a->value[3],
		a->value[4], a->value[5], a->value[6], a->value[7],
		a->value[8], a->value[9], a->value[10], a->value[11],
		a->value[12], a->value[13], a->value[14], a->value[15],
		a->value[16], a->value[17], a->value[18], a->value[19]);

	return 0;
}

/**
 * ib_addr2msg - convert InfiniBand address format to message header format
 */
static int ib_addr2msg(struct tipc_media_addr *a, char *msg_area)
{
	memset(msg_area, 0, TIPC_MEDIA_ADDR_SIZE);
	msg_area[TIPC_MEDIA_TYPE_OFFSET] = TIPC_MEDIA_TYPE_IB;
	memcpy(msg_area, a->value, INFINIBAND_ALEN);
	return 0;
}

/**
 * ib_msg2addr - convert message header address format to InfiniBand format
 */
static int ib_msg2addr(const struct tipc_bearer *tb_ptr,
		       struct tipc_media_addr *a, char *msg_area)
{
	ib_media_addr_set(tb_ptr, a, msg_area);
	return 0;
}

/*
 * InfiniBand media registration info
 */
static struct tipc_media ib_media_info = {
	.send_msg	= send_msg,
	.enable_bearer	= enable_bearer,
	.disable_bearer	= disable_bearer,
	.addr2str	= ib_addr2str,
	.addr2msg	= ib_addr2msg,
	.msg2addr	= ib_msg2addr,
	.priority	= TIPC_DEF_LINK_PRI,
	.tolerance	= TIPC_DEF_LINK_TOL,
	.window		= TIPC_DEF_LINK_WIN,
	.type_id	= TIPC_MEDIA_TYPE_IB,
	.name		= "ib"
};

/**
 * tipc_ib_media_start - activate InfiniBand bearer support
 *
 * Register InfiniBand media type with TIPC bearer code.  Also register
 * with OS for notifications about device state changes.
 */
int tipc_ib_media_start(void)
{
	int res;

	if (ib_started)
		return -EINVAL;

	res = tipc_register_media(&ib_media_info);
	if (res)
		return res;

	res = register_netdevice_notifier(&notifier);
	if (!res)
		ib_started = 1;
	return res;
}

/**
 * tipc_ib_media_stop - deactivate InfiniBand bearer support
 */
void tipc_ib_media_stop(void)
{
	if (!ib_started)
		return;

	flush_scheduled_work();
	unregister_netdevice_notifier(&notifier);
	ib_started = 0;
}
